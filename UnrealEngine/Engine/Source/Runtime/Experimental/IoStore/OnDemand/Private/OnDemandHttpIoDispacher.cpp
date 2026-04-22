// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnDemandHttpIoDispatcher.h"

#include "Async/Mutex.h"
#include "Async/UniqueLock.h"
#include "AtomicFlags.h"
#include "Containers/AnsiString.h"
#include "CVarUtilities.h"
#include "Experimental/ConcurrentLinearAllocator.h"
#include "HAL/Platform.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "IO/HttpIoDispatcher.h"
#include "IO/IoContainers.h"
#include "IO/IoStoreOnDemand.h"
#include "IasCache.h"
#include "IasHostGroup.h"
#include "Logging/LogVerbosity.h"
#include "Logging/StructuredLog.h"
#include "Misc/CommandLine.h"
#include "Misc/EnumClassFlags.h"
#include "Misc/Fork.h"
#include "Misc/SingleThreadRunnable.h"
#include "OnDemandHttpClient.h"
#include "OnDemandHttpThread.h" // EHttpRequestType
#include "Statistics.h"

#include <atomic>

// When enabled the cvar 'iax.InvalidUrlChance' will be enabled and allow us to simulate invalid urls
#define UE_ALLOW_INVALID_URL_DEBUGGING !UE_BUILD_SHIPPING

#define UE_ALLOW_CACHE_POISONING !UE_BUILD_SHIPPING

// TODO - Maybe do LexToString/LexFromString (dupe code in UnrealEngine.cpp ThreadPriorityToString)
extern const TCHAR* LexToString(EThreadPriority Priority);

namespace UE::IoStore
{
////////////////////////////////////////////////////////////////////////////////

DECLARE_MULTICAST_DELEGATE(FOnRecreateHttpClient);
extern FOnRecreateHttpClient OnRecreateHttpClient;

extern int32 GIasHttpTimeOutMs;
extern int32 GOnDemandBackendThreadPriorityIndex; 
extern bool GIaxHttpEnableInflightCancellation;
extern float GIaxInvalidUrlChance;
#if UE_ALLOW_CACHE_POISONING
extern bool GIasPoisonCache;
#endif // UE_ALLOW_CACHE_POISONING

/** Matches the priority set up found in Engine/Source/Runtime/Online/HTTP/Private/HttpThread.cpp */
static constexpr int32 GThreadPriorities[5] =
{
	EThreadPriority::TPri_Lowest,
	EThreadPriority::TPri_BelowNormal,
	EThreadPriority::TPri_SlightlyBelowNormal,
	EThreadPriority::TPri_Normal,
	EThreadPriority::TPri_AboveNormal
};

bool GHttpCacheEnabled = true;
static FAutoConsoleVariableRef CVar_HttpCacheEnabled(
	TEXT("iax.HttpCacheEnabled"),
	GHttpCacheEnabled,
	TEXT("")
);

namespace HttpIoDispatcher
{
////////////////////////////////////////////////////////////////////////////////

/**
 * Tracks the lifespan of a http request for as long as we want to keep it active. When the token is
 * destroyed we will try to cancel the request if it is still in progress.
 */
class FCancelationToken
{
public:
	FCancelationToken(FMultiEndpointHttpClient& InClient, FMultiEndpointHttpClient::FHttpTicketId InTicketId)
		: Client(InClient)
		, TicketId(InTicketId)
	{

	}

	~FCancelationToken()
	{
		// Note that if the request has already completed then this will do nothing.
		Client.CancelRequest(TicketId);
	}

private:
	FMultiEndpointHttpClient& Client;
	FMultiEndpointHttpClient::FHttpTicketId TicketId;
};

////////////////////////////////////////////////////////////////////////////////
static FIoHash GetCacheKey(const FIoHash& ChunkHash, const FIoHttpRange& Range)
{
	FIoHashBuilder HashBuilder;
	HashBuilder.Update(ChunkHash.GetBytes(), sizeof(FIoHash::ByteArray));
	HashBuilder.Update(&Range, sizeof(FIoHttpRange));

	return HashBuilder.Finalize();
}

////////////////////////////////////////////////////////////////////////////////
static FIoHash GetCacheKey(const FName& HostGroup, const FIoRelativeUrl Url, const FIoHttpHeaders& Headers, const FIoHttpOptions& Options)
{
	FIoHashBuilder HashBuilder;

	TCHAR NameString[NAME_SIZE];
	const uint32 NameLength = HostGroup.GetPlainNameString(NameString);
	HashBuilder.Update(NameString, NameLength * sizeof(TCHAR));
	HashBuilder.Update(Url.ToString(), Url.Len() * sizeof(FIoRelativeUrl::ElementType));

	TConstArrayView<FAnsiString> HeadersView = Headers.ToArrayView();
	check(HeadersView.IsEmpty() || ((HeadersView.Num() % 2) == 0));
	for (int32 Idx = 0; Idx < HeadersView.Num(); Idx += 2)
	{
		const FAnsiString Name	= HeadersView[Idx];
		const FAnsiString Value = HeadersView[Idx + 1];
		HashBuilder.Update(*Name, Name.Len());
		HashBuilder.Update(*Value, Value.Len());
	}

	HashBuilder.Update(&Options.GetRange(), sizeof(FIoHttpRange));
	return HashBuilder.Finalize();
}

////////////////////////////////////////////////////////////////////////////////
struct FHttpRequestBlockAllocationTag : FDefaultBlockAllocationTag
{
	static constexpr uint32			BlockSize = 4 * 1024;
	static constexpr bool			AllowOversizedBlocks = false;
	static constexpr bool			RequiresAccurateSize = false;
	static constexpr bool			InlineBlockAllocation = true;
	static constexpr const char*	TagName = "IoHttpRequestLinear";

	using Allocator = TBlockAllocationCache<BlockSize, FAlignedAllocator>;
};

////////////////////////////////////////////////////////////////////////////////
enum class EHttpRequestFlags : uint32
{
	None				= 0,
	Issued				= (1 << 0),
	CacheInflight		= (1 << 1),
	HttpQueued			= (1 << 2),
	HttpInflight		= (1 << 3),
	CancelRequested		= (1 << 4),
	Completing			= (1 << 5),
	Completed			= (1 << 6)
};
ENUM_CLASS_FLAGS(EHttpRequestFlags);

////////////////////////////////////////////////////////////////////////////////
struct FHttpRequest final
	: TConcurrentLinearObject<FHttpRequest, FHttpRequestBlockAllocationTag>
	, TIntrusiveListElement<FHttpRequest>
{
	using FFlags				= TAtomicFlags<EHttpRequestFlags>;
	static uint32				NextSeqNo;

	EHttpRequestType			GetHttpRequestType() const { return Options.GetCategory() == 1 ? EHttpRequestType::Installed : EHttpRequestType::Streaming; }
	void						AddRef() { RefCount.fetch_add(1, std::memory_order_relaxed); }
	inline void					Release();

	FIoHash						ChunkHash;
	FIoHash						CacheKey; //TODO make this smaller the IAS cache only uses 64 bit cache keys
	FIoHttpRequestCompleted 	OnCompleted;
	FIoHttpHeaders				Headers;
	FIoHttpOptions				Options;
	FIoBuffer					Buffer;
	FIoRelativeUrl				RelativeUrl;
	FHttpRequest*				Next = nullptr;
	FName						HostGroupName;
	EIoErrorCode				CacheStatus = EIoErrorCode::Unknown;
	FFlags						Flags;
	std::atomic<int32>			RefCount{0};
	std::atomic<EIoErrorCode>	CompletionStatus{EIoErrorCode::Unknown};
	uint32						SeqNo = ++NextSeqNo;

	TSharedPtr<FCancelationToken> CancelationToken;
};
using FHttpRequestList = TIntrusiveList<FHttpRequest>;

////////////////////////////////////////////////////////////////////////////////
void FHttpRequest::Release()
{
	if (RefCount.fetch_sub(1, std::memory_order_acq_rel) == 1)
	{
		delete this;
	}
}

////////////////////////////////////////////////////////////////////////////////
static bool HttpRequestSortPredicate(const FHttpRequest& LHS, const FHttpRequest& RHS)
{
	if (LHS.Options.GetPriority() == RHS.Options.GetPriority())
	{
		return LHS.SeqNo < RHS.SeqNo;
	}

	return LHS.Options.GetPriority() > RHS.Options.GetPriority();
}

uint32 FHttpRequest::NextSeqNo = 0;

////////////////////////////////////////////////////////////////////////////////
FIoHttpRange GetTotalRange(FHttpRequest* Request)
{
	FIoHttpRange TotalRange;
	for (FHttpRequest* It = Request; It != nullptr; It = It->Next)
	{
		// If the range is invalid we fetch the entire resource
		if (It->Options.GetRange().IsValid() == false)
		{
			return FIoHttpRange();
		}
		TotalRange += It->Options.GetRange();
	}

	return TotalRange;
}

////////////////////////////////////////////////////////////////////////////////
void DebugPrintRange(FHttpRequest* Requests, const UE::FIoHttpRange& TotalRange)
{
#if !UE_BUILD_SHIPPING 
	if (Requests->Next == nullptr)
	{
		return; 
	}

	int32 Count = 0;
	TStringBuilder<256> Sb;
	for (FHttpRequest* It = Requests; It != nullptr; It = It->Next)
	{
		const UE::FIoHttpRange& Range = It->Options.GetRange();
		Sb << TEXT("[") << Range.GetMin() << TEXT("-") << Range.GetMax() << TEXT("] ");
		++Count;
	}

	Sb << TEXT("-> ") << TEXT("[") << TotalRange.GetMin() << TEXT("-") << TotalRange.GetMax() << TEXT("]");
	UE_LOG(LogHttpIoDispatcher, VeryVerbose, TEXT("Merging %d requests, %s, total size = %.2lf KiB"),
		Count, Sb.ToString(), double(TotalRange.GetSize()) / 1024.0);
#endif
}

////////////////////////////////////////////////////////////////////////////////
class FHttpQueue
{
public:
	FHttpQueue();
	~FHttpQueue() = default;

	void			Reprioritize(FHttpRequest* Request, int32 NewPriority);
	bool			Cancel(FHttpRequest* Request);
	void			Enqueue(FHttpRequest* Request);
	/**
	* Returns one or more requests. Multiple requests will returned as a linked list (FHttpRequest::Next)
	* and will all be for the same chunk.
	*/
	FHttpRequest*	Dequeue();

#if UE_ALLOW_HTTP_PAUSE
	void			OnTogglePause(bool bPause, EHttpRequestTypeFilter Filter);
	bool			IsPaused(FHttpRequest* Request) const;
#endif // UE_ALLOW_HTTP_PAUSE

private:

	void			EnqueueInternal(FHttpRequest* Request);

	TArray<FHttpRequest*>			Heap;
	TMap<FIoHash, FHttpRequestList>	ByChunkKey;
	FMutex							Mutex;
#if UE_ALLOW_HTTP_PAUSE
	EHttpRequestTypeFilter			PauseFilter = EHttpRequestTypeFilter::None;
	TArray<FHttpRequest*>			PausedRequests;
#endif // UE_ALLOW_HTTP_PAUSE
	bool							bReprioritize = false;

	/** Slack to leave in containers when emptying them */
	static constexpr int32			ContainerSlackSize = 16;
};

////////////////////////////////////////////////////////////////////////////////

FHttpQueue::FHttpQueue()
{
	Heap.Reserve(ContainerSlackSize);
	ByChunkKey.Reserve(ContainerSlackSize);
}

void FHttpQueue::Reprioritize(FHttpRequest* Request, int32 NewPriority)
{
	TUniqueLock Lock(Mutex);

#if UE_ALLOW_HTTP_PAUSE
	if (IsPaused(Request))
	{
		Request->Options.SetPriority(NewPriority);
		return;
	}
#endif //UE_ALLOW_HTTP_PAUSE

	const bool bGenericHttpRequest = Request->ChunkHash.IsZero();
	if (bGenericHttpRequest)
	{
		if (int32 Index = Heap.Find(Request); Index != INDEX_NONE)
		{
			Request->Options.SetPriority(NewPriority);
			bReprioritize = true;
		}
		return;
	}

	FHttpRequestList* List = ByChunkKey.Find(Request->ChunkHash);
	if (List == nullptr)
	{
		ensure(Heap.Find(Request) == INDEX_NONE);
		return;
	}

	for (FHttpRequest& Existing : *List)
	{
		if (NewPriority > Existing.Options.GetPriority())
		{
			Existing.Options.SetPriority(NewPriority);
			bReprioritize = true;
		}
	}
}

bool FHttpQueue::Cancel(FHttpRequest* Request)
{
	TUniqueLock Lock(Mutex);

#if UE_ALLOW_HTTP_PAUSE
	if (IsPaused(Request))
	{
		if(PausedRequests.Remove(Request) != 0)
		{
			// Cancel stats assume that the request was enqueued, so we have to add that back
			FOnDemandIoBackendStats::Get()->OnHttpEnqueue(Request->GetHttpRequestType());

			FOnDemandIoBackendStats::Get()->OnHttpCancel(Request->GetHttpRequestType());
			FOnDemandIoBackendStats::Get()->OnHttpUnpaused(Request->GetHttpRequestType());
			return true;
		}
	}
#endif //UE_ALLOW_HTTP_PAUSE

	const bool bGenericHttpRequest = Request->ChunkHash.IsZero();
	if (bGenericHttpRequest)
	{
		ensure(Request->Next == nullptr);
		if (int32 Index = Heap.Find(Request); Index != INDEX_NONE)
		{
			Heap.HeapRemoveAt(Index, HttpRequestSortPredicate);
			FOnDemandIoBackendStats::Get()->OnHttpDequeue(Request->GetHttpRequestType());
			FOnDemandIoBackendStats::Get()->OnHttpCancel(Request->GetHttpRequestType());
			return true;
		}
		return false;
	}

	// Note: Even if a batch exists for the given chunk hash, it doesn't necessarily mean this request is included in that batch.
	FHttpRequestList* List = ByChunkKey.Find(Request->ChunkHash);
	if ((List == nullptr) || (List->Remove(Request) == false))
	{
		ensure(Heap.Find(Request) == INDEX_NONE);
		return false;
	}

	// If the request was the first for this chunk we push any of the remaining requests to the heap
	if (int32 Index = Heap.Find(Request); Index != INDEX_NONE)
	{
		Heap.HeapRemoveAt(Index, HttpRequestSortPredicate);
		FOnDemandIoBackendStats::Get()->OnHttpDequeue(Request->GetHttpRequestType());
		FOnDemandIoBackendStats::Get()->OnHttpCancel(Request->GetHttpRequestType());

		if (!List->IsEmpty())
		{
			FHttpRequest* Head = List->PeekHead();
			ensure(Heap.Find(Head) == INDEX_NONE);
			Heap.HeapPush(Head, HttpRequestSortPredicate);
			FOnDemandIoBackendStats::Get()->OnHttpEnqueue(Head->GetHttpRequestType());
		}
	}

	if (List->IsEmpty())
	{
		ByChunkKey.Remove(Request->ChunkHash);
	}

	return true;
}

void FHttpQueue::Enqueue(FHttpRequest* Request)
{
	TUniqueLock Lock(Mutex);

#if UE_ALLOW_HTTP_PAUSE
	if (IsPaused(Request))
	{
		PausedRequests.Add(Request);
		FOnDemandIoBackendStats::Get()->OnHttpPaused(Request->GetHttpRequestType());

		return;
	}
#endif //UE_ALLOW_HTTP_PAUSE

	EnqueueInternal(Request);
}

void FHttpQueue::EnqueueInternal(FHttpRequest* Request)
{
	check(Request->Next == nullptr);

	Request->Flags.Remove(EHttpRequestFlags::CacheInflight);

	const bool bGenericHttpRequest = Request->ChunkHash.IsZero();
	if (bGenericHttpRequest)
	{
		Heap.HeapPush(Request, HttpRequestSortPredicate);
		Request->Flags.Add(EHttpRequestFlags::HttpQueued);
		FOnDemandIoBackendStats::Get()->OnHttpEnqueue(Request->GetHttpRequestType());
		return;
	}

	if (FHttpRequestList* List = ByChunkKey.Find(Request->ChunkHash))
	{
		ensure(List->IsEmpty() == false);
		FHttpRequest& Head = *List->PeekHead();
		if (Request->Options.GetPriority() > Head.Options.GetPriority())
		{
			Head.Options.SetPriority(Request->Options.GetPriority());
			bReprioritize = true;
		}
		else
		{
			Request->Options.SetPriority(Head.Options.GetPriority());
		}
		List->AddOrInsertBefore(Request, [](const FHttpRequest& New, const FHttpRequest& Existing)
		{
			return New.Options.GetRange().GetMin() < Existing.Options.GetRange().GetMin();
		});
	}
	else
	{
		Heap.HeapPush(Request, HttpRequestSortPredicate);
		ByChunkKey.Add(Request->ChunkHash, FHttpRequestList(Request));
		FOnDemandIoBackendStats::Get()->OnHttpEnqueue(Request->GetHttpRequestType());
	}

	Request->Flags.Add(EHttpRequestFlags::HttpQueued);
}

FHttpRequest* FHttpQueue::Dequeue()
{
	TUniqueLock Lock(Mutex);

	if (Heap.IsEmpty())
	{
		ensure(ByChunkKey.IsEmpty());
		return nullptr;
	}

	if (bReprioritize)
	{
		bReprioritize = false;
		Heap.Heapify(HttpRequestSortPredicate);
	}

	FHttpRequest* Next = nullptr;
	Heap.HeapPop(Next, HttpRequestSortPredicate, EAllowShrinking::No);
	if (Heap.IsEmpty())
	{
		Heap.Empty(ContainerSlackSize);
	}

	if (Next == nullptr)
	{
		return Next;
	}

	//TODO: Return N request up to some range limit
	if (FHttpRequestList List; ByChunkKey.RemoveAndCopyValue(Next->ChunkHash, List))
	{
		// The first request that was enqueued for the chunk is not necessarily the first request in the list
		Next = List.PeekHead();

		if (ByChunkKey.IsEmpty())
		{
			ByChunkKey.Empty(ContainerSlackSize);
		}
	}

	for (FHttpRequest* It = Next; It != nullptr; It = It->Next)
	{
		It->Flags.Add(EHttpRequestFlags::HttpInflight);
		It->Flags.Remove(EHttpRequestFlags::HttpQueued);
	}

	if (Next != nullptr)
	{
		FOnDemandIoBackendStats::Get()->OnHttpDequeue(Next->GetHttpRequestType());
	}

	return Next;
}

#if UE_ALLOW_HTTP_PAUSE

void FHttpQueue::OnTogglePause(bool bPause, EHttpRequestTypeFilter Filter)
{
	UE::TUniqueLock _(Mutex);

	if (bPause)
	{
		EnumAddFlags(PauseFilter, Filter);

		// Remove any queued requests which now be paused
		TArray<FHttpRequest*> NewHeap;
		for (FHttpRequest* HeadRequest :Heap)
		{
			if (FHttpRequestList* List = ByChunkKey.Find(HeadRequest->ChunkHash))
			{
				bool bRemovedHeadRequest = false;

				// Strip paused requests from the list
				FHttpRequest* Iterator = List->PeekHead();
				while (Iterator != nullptr)
				{
					FHttpRequest* ListRequest = Iterator;
					Iterator = ListRequest->Next;

					if (IsPaused(ListRequest))
					{
						if (ListRequest == HeadRequest)
						{
							bRemovedHeadRequest = true;
						}

						List->Remove(ListRequest);

						check(ListRequest->Next == nullptr);
						PausedRequests.Add(ListRequest);
						FOnDemandIoBackendStats::Get()->OnHttpPaused(ListRequest->GetHttpRequestType());
					}
				}

				// If the list is now empty we can remove the entire request from Heap
				if (List->IsEmpty())
				{
					FOnDemandIoBackendStats::Get()->OnHttpOnRemovedPending(HeadRequest->GetHttpRequestType());
					ByChunkKey.Remove(HeadRequest->ChunkHash);
				}
				// If we removed the head request we need to replace it in Heap with a non paused request
				else if (bRemovedHeadRequest)
				{
					FOnDemandIoBackendStats::Get()->OnHttpOnRemovedPending(HeadRequest->GetHttpRequestType());

					FHttpRequest* NewHeadRequest = List->PeekHead();
					NewHeap.HeapPush(NewHeadRequest, HttpRequestSortPredicate);
					FOnDemandIoBackendStats::Get()->OnHttpEnqueue(NewHeadRequest->GetHttpRequestType());
				}
			}
			else if (IsPaused(HeadRequest))
			{
				check(HeadRequest->Next == nullptr);
				PausedRequests.Add(HeadRequest);

				FOnDemandIoBackendStats::Get()->OnHttpOnRemovedPending(HeadRequest->GetHttpRequestType());
				FOnDemandIoBackendStats::Get()->OnHttpPaused(HeadRequest->GetHttpRequestType());
			}
			else
			{
				NewHeap.HeapPush(HeadRequest, HttpRequestSortPredicate);
			}
		}

		Heap = MoveTemp(NewHeap);
	}
	else
	{
		EnumRemoveFlags(PauseFilter, Filter);

		for (int32 Index = 0; Index < PausedRequests.Num(); ++Index)
		{
			FHttpRequest* Request = PausedRequests[Index];

			if (IsPaused(Request) == false)
			{
				// Add the newly unpaused request back to the queue
				EnqueueInternal(Request);
				FOnDemandIoBackendStats::Get()->OnHttpUnpaused(Request->GetHttpRequestType());

				PausedRequests.RemoveAt(Index);
				--Index;
			}
		}
	}
}

bool FHttpQueue::IsPaused(FHttpRequest* Request) const
{
	check(Request != nullptr);

	switch (Request->GetHttpRequestType())
	{
		case EHttpRequestType::Installed:
		{
			return EnumHasAnyFlags(PauseFilter, EHttpRequestTypeFilter::Installed);
		}
		case EHttpRequestType::Streaming:
		{
			return EnumHasAnyFlags(PauseFilter, EHttpRequestTypeFilter::Streaming);
		}
		default:
		{
			checkNoEntry();
			return false;
		}
	}
}

#endif // UE_ALLOW_HTTP_PAUSE

////////////////////////////////////////////////////////////////////////////////
class FHttpDispatcher final
	: public FRunnable
	, public FSingleThreadRunnable
	, public IOnDemandHttpIoDispatcher
{
public:
									FHttpDispatcher(TUniquePtr<IIasCache>&& Cache);
									~FHttpDispatcher();

private:
	// FRunnable
	virtual bool					Init() override;
	virtual uint32					Run() override;
	virtual void					Stop() override;
	virtual void					Exit() override;
	virtual FSingleThreadRunnable*	GetSingleThreadInterface() override
	{
		return this;
	}

	//FSingleThreadRunnable
	virtual void					Tick() override;
	// IHttpIoDispatcher
	virtual void					Shutdown() override;
	virtual FIoStatus				RegisterHostGroup(const FName& HostGroupName, TConstArrayView<FAnsiString> HostNames, FAnsiStringView TestUrl) override;
	virtual bool					IsHostGroupRegistered(const FName& HostGroup) override;
	virtual bool					IsHostGroupOk(const FName& HostGroup) override;
	virtual FHostGroupRegistered&	OnHostGroupRegistered() override { return HostGroupRegisterd; }
	virtual FIoHttpRequestHandle	CreateRequest(
										FIoHttpRequestHandle& First,
										FIoHttpRequestHandle& Last,
										const FName& HostGroupName,
										const FIoRelativeUrl& RelativeUrl,
										const FIoHttpOptions& Options,
										FIoHttpHeaders&& Headers,
										FIoHttpRequestCompleted&& OnCompleted,
										const FIoHash* ChunkHash) override;
	virtual void					IssueRequest(FIoHttpRequestHandle RequestHandle) override;
	virtual void					ReleaseRequest(FIoHttpRequestHandle Handle) override;
	virtual void					CancelRequest(FIoHttpRequestHandle Handle) override;
	virtual void					UpdateRequestPriority(FIoHttpRequestHandle Handle, int32 NewPriority) override;
	virtual EIoErrorCode			GetRequestStatus(FIoHttpRequestHandle Handle) override;

	virtual FIoStatus				CacheResponse(const FIoHttpResponse& Response) override;
	virtual FIoStatus				EvictFromCache(const FIoHttpResponse& Response) override;

	void							UpdateThreadPriorityIfNeeded();

	bool							TryCreateHttpClient();
	void							RecreateHttpClientIfNeeded();

	bool							TryReadFromCache(FHttpRequest* Request);
	bool							ProcessHttpRequests();
	FHttpRequest*					GetNextRequest();
	void							CompleteHttpRequest(FHttpRequest& Request, FMultiEndpointHttpClientResponse&& HttpResponse, FIASHostGroup& HostGroup, const FIoHttpRange& TotalRange);
	void							CompleteRequest(FHttpRequest& Request, TArray<FAnsiString>&& Headers, FIoBuffer& Body, const FIoHttpRange& TotalRange, uint32 StatusCode, bool bCached);
	void							CompleteRequest(FHttpRequest& Request, FIoBuffer& Body, const FIoHttpRange& TotalRange, uint32 StatusCode, bool bCached)
									{
										CompleteRequest(Request, TArray<FAnsiString>(), Body, TotalRange, StatusCode, bCached);
									}

#if UE_ALLOW_HTTP_PAUSE
	void OnPauseCommand(bool bShouldPause, const TArray<FString>& Args, FOutputDevice& Ar);
#endif //UE_ALLOW_HTTP_PAUSE

	TUniquePtr<IIasCache>					Cache;
	TUniquePtr<FMultiEndpointHttpClient>	HttpClient;
	TUniquePtr<FRunnableThread>				Thread;
	EThreadPriority							ThreadPriority = EThreadPriority::TPri_Normal;
	FHttpQueue								HttpQueue;
	FHostGroupRegistered					HostGroupRegisterd;
	FEventRef								WakeUp;
	/** Keeps assignment of the CancelationToken safe between ::CancelRequest and ::ProcessHttpRequests */
	FMutex									CancelationMutex;
	FDelegateHandle							OnRecreateHttpClientHandle;
	std::atomic_bool						bRecreateHttpClient = false;
	std::atomic_bool						bStopRequested = false;

#if UE_ALLOW_HTTP_PAUSE
	FConsoleCommandPtr						PauseCommand;
	FConsoleCommandPtr						UnpauseCommand;
#endif // UE_ALLOW_HTTP_PAUSE

#if UE_ALLOW_DROP_CACHE
	FConsoleCommandPtr						DropCacheCommand;
#endif // UE_ALLOW_DROP_CACHE
};

static TUniquePtr<FHttpDispatcher> HttpDispatcher;

////////////////////////////////////////////////////////////////////////////////
FHttpDispatcher::FHttpDispatcher(TUniquePtr<IIasCache>&& InCache)
	: Cache(MoveTemp(InCache))
{
#if UE_ALLOW_HTTP_PAUSE
	PauseCommand = FConsoleCommandPtr(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("iax.HttpPause"),
		TEXT("Pause all http requests. Passing in 'IAD' or 'IAS' as an arg to only pause requests of that type"),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateLambda([this](const TArray<FString>& Args, FOutputDevice& Ar) -> void
			{
				OnPauseCommand(/*bShouldPause*/ true, Args, Ar);
			}),
		ECVF_Default));

	UnpauseCommand = FConsoleCommandPtr(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("iax.HttpUnpause"),
		TEXT("Unpause all http requests. Passing in 'IAD' or 'IAS' as an arg to only unpause requests of that type"),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateLambda([this](const TArray<FString>& Args, FOutputDevice& Ar) -> void
			{
				OnPauseCommand(/*bShouldPause*/ false, Args, Ar);
			}),
		ECVF_Default));
#endif // UE_ALLOW_HTTP_PAUSE

#if UE_ALLOW_DROP_CACHE
	DropCacheCommand = FConsoleCommandPtr(IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("ias.DropCache"),
		TEXT("Resets the IAS cache and deletes the data from disk."),
		FConsoleCommandDelegate::CreateLambda([this]() -> void
			{
				if (Cache)
				{
					Cache->Drop();
				}
			}),
		ECVF_Default));
#endif // UE_ALLOW_DROP_CACHE

	const int32 ThreadPriorityIndex			= FMath::Clamp(GOnDemandBackendThreadPriorityIndex, 0, (int32)UE_ARRAY_COUNT(GThreadPriorities) - 1);
	EThreadPriority DesiredThreadPriority	= (EThreadPriority)GThreadPriorities[ThreadPriorityIndex];
	ThreadPriority							= DesiredThreadPriority;

	const uint32 StackSize					= 0; // Use default stack size
	const uint64 ThreadAffinityMask			= FGenericPlatformAffinity::GetNoAffinityMask();
	const EThreadCreateFlags CreateFlags	= EThreadCreateFlags::None;
	const bool bAllowPreFork				= FParse::Param(FCommandLine::IsInitialized() ? FCommandLine::Get() : TEXT(""), TEXT("-Ias.EnableHttpThreadPreFork"));

	Thread.Reset(FForkProcessHelper::CreateForkableThread(this, TEXT("IoService.Http"), StackSize, ThreadPriority, ThreadAffinityMask, CreateFlags, bAllowPreFork));
};

FHttpDispatcher::~FHttpDispatcher()
{
	if (OnRecreateHttpClientHandle.IsValid())
	{
		OnRecreateHttpClient.Remove(OnRecreateHttpClientHandle);
		OnRecreateHttpClientHandle.Reset();
	}

	Thread.Reset();
}

bool FHttpDispatcher::Init()
{
	OnRecreateHttpClientHandle = OnRecreateHttpClient.AddLambda([this]()
		{
			bRecreateHttpClient = true;
			WakeUp->Trigger();
		});

	return TryCreateHttpClient();
}

uint32 FHttpDispatcher::Run()
{
	check(HttpClient.IsValid());

	for (;;)
	{
		UpdateThreadPriorityIfNeeded();

		Tick();

		if (bStopRequested.load(std::memory_order_relaxed))
		{
			break;
		}

		WakeUp->Wait(FHostGroupManager::Get().GetNumDisconnctedHosts() > 0 ? GIasHttpHealthCheckWaitTime : MAX_uint32);
	}

	return 0;
}

void FHttpDispatcher::Stop()
{
	bool bExpected = false;
	if (bStopRequested.compare_exchange_strong(bExpected, true))
	{
		WakeUp->Trigger();
	}
}

void FHttpDispatcher::Exit()
{
	
}

void FHttpDispatcher::Tick()
{
	FHostGroupManager::Get().Tick(GIasHttpTimeOutMs, bStopRequested);

	// TODO: It would be better to only update connections as they need it, consider doing this on
	// hostgroup connect/disconnect events.
	HttpClient->UpdateConnections();

	ProcessHttpRequests();
}

void FHttpDispatcher::Shutdown()
{
	bool bExpected = false;
	if (bStopRequested.compare_exchange_strong(bExpected, true))
	{
		WakeUp->Trigger();
	}
}

FIoStatus FHttpDispatcher::RegisterHostGroup(const FName& HostGroupName, TConstArrayView<FAnsiString> HostNames, FAnsiStringView TestUrl)
{
	FIASHostGroup Existing = FHostGroupManager::Get().Find(HostGroupName);
	if (Existing.GetName() == HostGroupName)
	{
		return FIoStatus::Ok;
	}

	TIoStatusOr<FIASHostGroup> NewGroup = FHostGroupManager::Get().Register(HostGroupName, TestUrl);
	if (NewGroup.IsOk() == false)
	{
		return NewGroup.Status();
	}

#if !UE_BUILD_SHIPPING
	TArray<FAnsiString> HostsOverride;
	if (HostGroupName == FOnDemandHostGroup::DefaultName)
	{
		FString HostValue;
		if (FParse::Value(FCommandLine::Get(), TEXT("Iax.DefaultHostGroup="), HostValue))
		{
			TArray<FString> HostArray;
			HostValue.ParseIntoArray(HostArray, TEXT(","), true);
			for (FString& Host : HostArray)
			{
				Host.TrimStartAndEndInline();
				if (!Host.StartsWith(TEXT("http")))
				{
					Host = TEXT("http://") + Host;
				}
				HostsOverride.Add(FAnsiString(StringCast<ANSICHAR>(*Host)));
			}
			HostNames = HostsOverride; 
		}
	}
#endif

	if (FIoStatus Status = NewGroup.ConsumeValueOrDie().Resolve(HostNames); !Status.IsOk())
	{
		UE_LOG(LogHttpIoDispatcher, Warning, TEXT("Failed to create host group '%s'"), *HostGroupName.ToString());
		return Status;
	}

	UE_LOG(LogHttpIoDispatcher, Log, TEXT("Registered new host group '%s'"), *HostGroupName.ToString());
	for (const FAnsiString& HostName : HostNames) 
	{
		UE_LOG(LogHttpIoDispatcher, Log, TEXT("\t* %s"), *FString(HostName));
	}

	if (HostGroupRegisterd.IsBound())
	{
		HostGroupRegisterd.Broadcast(HostGroupName);
	}

	return FIoStatus::Ok;
}

bool FHttpDispatcher::IsHostGroupRegistered(const FName& HostGroup)
{
	FIASHostGroup Existing = FHostGroupManager::Get().Find(HostGroup);
	return Existing.GetName() == HostGroup;
}

bool FHttpDispatcher::IsHostGroupOk(const FName& HostGroup)
{
	FIASHostGroup Existing = FHostGroupManager::Get().Find(HostGroup);
	return Existing.IsConnected();
}

FIoHttpRequestHandle FHttpDispatcher::CreateRequest(
	FIoHttpRequestHandle& First,
	FIoHttpRequestHandle& Last,
	const FName& HostGroupName,
	const FIoRelativeUrl& RelativeUrl,
	const FIoHttpOptions& Options,
	FIoHttpHeaders&& Headers,
	FIoHttpRequestCompleted&& OnCompleted,
	const FIoHash* ChunkHash)
	
{
	FHttpRequest* Request = new FHttpRequest();
	Request->OnCompleted	= MoveTemp(OnCompleted);
	Request->Headers		= MoveTemp(Headers);
	Request->Options		= Options;
	Request->RelativeUrl	= RelativeUrl;
	Request->HostGroupName	= HostGroupName;
	check(Request->Next == nullptr);

	if (ChunkHash != nullptr)
	{
		Request->ChunkHash	= *ChunkHash;
		Request->CacheKey	= GetCacheKey(*ChunkHash, Options.GetRange());
	}
	else
	{
		Request->ChunkHash	= FIoHash::Zero;
		Request->CacheKey	= GetCacheKey(HostGroupName, Request->RelativeUrl, Request->Headers, Request->Options);
	}

	if (Last == 0)
	{
		ensure(First == 0);
		First = UPTRINT(Request);
	}
	else
	{
		FHttpRequest* BatchLast = reinterpret_cast<FHttpRequest*>(Last);
		BatchLast->Next = Request;
	}
	Last = UPTRINT(Request);

	Request->AddRef();
	return reinterpret_cast<FIoHttpRequestHandle>(Request);
}

void FHttpDispatcher::IssueRequest(FIoHttpRequestHandle RequestHandle)
{
	ensure(RequestHandle != 0);
	if (RequestHandle == 0)
	{
		return;
	}

	bool bWakeUp				= false;
	FHttpRequest* NextRequest	= reinterpret_cast<FHttpRequest*>(RequestHandle);

	while (NextRequest != nullptr)
	{
		FHttpRequest* Request	= NextRequest;
		NextRequest				= Request->Next;
		Request->Next			= nullptr;

		Request->AddRef();
		Request->Flags.Add(EHttpRequestFlags::Issued);
		if (TryReadFromCache(Request) == false)
		{
			HttpQueue.Enqueue(Request);
			bWakeUp = true;
		}
	}

	if (bWakeUp)
	{
		WakeUp->Trigger();
	}
}

void FHttpDispatcher::CancelRequest(FIoHttpRequestHandle Handle)
{
	ensure(Handle != 0);
	if (Handle == 0)
	{
		return;
	}

	FHttpRequest* Request = reinterpret_cast<FHttpRequest*>(Handle);

	const EHttpRequestFlags CurrentFlags = Request->Flags.Get();
	if (EnumHasAnyFlags(CurrentFlags, EHttpRequestFlags::CancelRequested | EHttpRequestFlags::Completing | EHttpRequestFlags::Completed))
	{
		return;
	}
	Request->Flags.Add(EHttpRequestFlags::CancelRequested);

	UE_LOG(LogHttpIoDispatcher, Verbose, TEXT("Cancelling request, SeqNo=%u"), Request->SeqNo);

	if (EnumHasAnyFlags(CurrentFlags, EHttpRequestFlags::CacheInflight))
	{
		Cache->Cancel(Request->Buffer);
	}

	if (HttpQueue.Cancel(Request))
	{
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, Request]
		{
			CompleteRequest(*Request, Request->Buffer, FIoHttpRange(), 0, false);
		}, UE::Tasks::ETaskPriority::BackgroundLow);
	}

	if (GIaxHttpEnableInflightCancellation)
	{
		TUniqueLock _(CancelationMutex);
		Request->CancelationToken.Reset();
	}
}

void FHttpDispatcher::UpdateRequestPriority(FIoHttpRequestHandle Handle, int32 NewPriority)
{
	ensure(Handle != 0);
	if (Handle == 0)
	{
		return;
	}

	FHttpRequest* Request = reinterpret_cast<FHttpRequest*>(Handle);
	UE_LOG(LogHttpIoDispatcher, Verbose, TEXT("Updating request priority, SeqNo=%u, Priority=%d, NewPriority=%d"),
		Request->SeqNo, Request->Options.GetPriority(), NewPriority);
	HttpQueue.Reprioritize(Request, NewPriority);
}

EIoErrorCode FHttpDispatcher::GetRequestStatus(FIoHttpRequestHandle Handle)
{
	if (Handle != 0)
	{
		return reinterpret_cast<FHttpRequest*>(Handle)->CompletionStatus.load(std::memory_order_relaxed);
	}

	return EIoErrorCode::InvalidCode;
}

FIoStatus FHttpDispatcher::CacheResponse(const FIoHttpResponse& Response)
{
	if (Response.IsCached())
	{
		return EIoErrorCode::Ok;
	}

	// If the cache is disabled we consider the caching to be a success, otherwise the calling code will
	// have to special case this.
	if (!GHttpCacheEnabled || !Cache.IsValid())
	{
		return EIoErrorCode::Ok;
	}

	if (Response.GetErrorCode() != EIoErrorCode::Ok || Response.GetCacheKey().IsZero() || Response.GetBody().GetSize() == 0)
	{
		return EIoErrorCode::InvalidParameter;
	}

	FIoBuffer Copy(Response.GetBody());
	return Cache->Put(Response.GetCacheKey(), Copy);
}

void FHttpDispatcher::UpdateThreadPriorityIfNeeded()
{
	// TODO - We should wake up the thread when 'ias.onDemandBackendThreadPriority' is updated
	int32 ThreadPriorityIndex = FMath::Clamp(GOnDemandBackendThreadPriorityIndex, 0, (int32)UE_ARRAY_COUNT(GThreadPriorities) - 1);
	EThreadPriority DesiredThreadPriority = (EThreadPriority)GThreadPriorities[ThreadPriorityIndex];
	if (DesiredThreadPriority != ThreadPriority)
	{
		UE_LOGFMT(LogHttpIoDispatcher, Log, "Updated IoService.Http thread priority to '{Priority}'", LexToString(DesiredThreadPriority));

		FPlatformProcess::SetThreadPriority(DesiredThreadPriority);
		ThreadPriority = DesiredThreadPriority;
	}
}

bool FHttpDispatcher::TryCreateHttpClient()
{
	// Make sure that 'ias.HttpConnectionCount' is within a range that the client will accept
	const int32 MaxNumberOfConnections = FMath::Clamp(GIasHttpConnectionCount, 1, (int32)MAX_uint16);
	UE_CLOG(MaxNumberOfConnections != GIasHttpConnectionCount, LogHttpIoDispatcher, Error, TEXT("ias.HttpConnectionCount (%d) outside of valid range 1-%d"), GIasHttpConnectionCount, MAX_uint16);

	HttpClient = FMultiEndpointHttpClient::Create(FMultiEndpointHttpClientConfig
		{
			.MaxConnectionCount = MaxNumberOfConnections,
			.ReceiveBufferSize = GIasHttpRecvBufKiB >= 0 ? GIasHttpRecvBufKiB << 10 : -1,
			.SendBufferSize = GIasHttpSendBufKiB >= 0 ? GIasHttpSendBufKiB << 10 : -1,
			.MaxRetryCount = GIasHttpRetryCount,
			.TimeoutMs = GIasHttpFailTimeOutMs,
			.Redirects = EHttpRedirects::Follow,
			.bAllowChunkedTransfer = true,
			.LogCategory = &LogHttpIoDispatcher,
			.LogVerbosity = ELogVerbosity::VeryVerbose
		});

	return HttpClient.IsValid();
}

void FHttpDispatcher::RecreateHttpClientIfNeeded()
{
	if (bRecreateHttpClient.exchange(false))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FHttpDispatcher::RecreateHttpClientIfNeeded);

		// Keep the old client at first in case we fail to create the new one so that we can restore it
		// this way the game can keep on functioning correctly.
		TUniquePtr<FMultiEndpointHttpClient> OldHttpClient = MoveTemp(HttpClient);
		if (TryCreateHttpClient())
		{
			OldHttpClient.Reset();
			UE_LOGFMT(LogIas, Display, "FHttpDispatcher: Successfully created a new http client for use");
		}
		else
		{
			HttpClient = MoveTemp(OldHttpClient);
			UE_LOGFMT(LogIas, Warning, "FHttpDispatcher: Failed to create a new http client, the existing client will continue to be used");
		}
	}
}

FIoStatus FHttpDispatcher::EvictFromCache(const FIoHttpResponse& Response)
{
	// If the cache is disabled we consider the eviction to be a success, otherwise the calling code will
	// have to special case this.
	if (!GHttpCacheEnabled || !Cache.IsValid())
	{
		return EIoErrorCode::Ok;
	}

	if (Response.GetErrorCode() != EIoErrorCode::Ok || Response.GetCacheKey().IsZero())
	{
		return EIoErrorCode::InvalidParameter;
	}

	return Cache->Evict(Response.GetCacheKey());
}

bool FHttpDispatcher::TryReadFromCache(FHttpRequest* Request)
{
	if (!GHttpCacheEnabled || !Cache.IsValid())
	{
		return false;
	}

	if (EnumHasAnyFlags(Request->Options.GetFlags(), EIoHttpFlags::ReadCache) == false)
	{
		return false;
	}

	Request->Flags.Add(EHttpRequestFlags::CacheInflight);
	const EIoErrorCode CacheStatus = Cache->Get(Request->CacheKey, Request->Buffer);
	if (CacheStatus == EIoErrorCode::Ok)
	{
		FOnDemandIoBackendStats::Get()->OnCacheGet(Request->Buffer.GetSize());
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, Request]
		{
			CompleteRequest(*Request, Request->Buffer, FIoHttpRange(), 200, true);
		}, UE::Tasks::ETaskPriority::BackgroundLow);

		return true;
	}

	if (CacheStatus == EIoErrorCode::FileNotOpen)
	{
		UE::Tasks::FTaskEvent OnReadyEvent(TEXT("HttpCacheReadComplete"));
		Launch(UE_SOURCE_LOCATION, [this, Request]
		{
			if (Request->CacheStatus == EIoErrorCode::Ok)
			{
				FOnDemandIoBackendStats::Get()->OnCacheGet(Request->Buffer.GetSize());
				CompleteRequest(*Request, Request->Buffer, FIoHttpRange(), 200, true);
			}
			else if ((Request->CacheStatus == EIoErrorCode::Cancelled) || Request->Flags.HasAny(EHttpRequestFlags::CancelRequested))
			{
				CompleteRequest(*Request, Request->Buffer, FIoHttpRange(), 0, false);
			}
			else
			{
				if (Request->CacheStatus == EIoErrorCode::ReadError)
				{
					FOnDemandIoBackendStats::Get()->OnCacheError();
				}

				HttpQueue.Enqueue(Request);
				WakeUp->Trigger();
			}
		}, OnReadyEvent, UE::Tasks::ETaskPriority::BackgroundLow);

		Cache->Materialize(Request->CacheKey, Request->Buffer, Request->CacheStatus, MoveTemp(OnReadyEvent));

		return true;
	}

	return false;
}

void FHttpDispatcher::ReleaseRequest(FIoHttpRequestHandle Handle)
{
	ensure(Handle != 0);
	FHttpRequest* NextRequest = reinterpret_cast<FHttpRequest*>(Handle);
	while (NextRequest != nullptr)
	{
		FHttpRequest* Request	= NextRequest;
		NextRequest				= Request->Next;
		Request->Next			= nullptr;

#if !UE_BUILD_SHIPPING 
		const EHttpRequestFlags Flags = Request->Flags.Get();
		ensureMsgf(Flags == EHttpRequestFlags::None || EnumHasAnyFlags(Flags, EHttpRequestFlags::Completing | EHttpRequestFlags::Completed),
			TEXT("The HTTP request handle must not be released before the completion callback has been triggered."));
#endif
		Request->Release();
	}
}

bool FHttpDispatcher::ProcessHttpRequests()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FHttpDispatcher::ProcessHttpRequests);

	RecreateHttpClientIfNeeded();

	const int32 MaxConcurrentRequests = FMath::Min(GIasHttpConcurrentRequests, 32);
	int32		NumConcurrentRequests = 0;

	FHttpRequest* NextHttpRequest = HttpQueue.Dequeue();
	if (NextHttpRequest == nullptr)
	{
		return false;
	}

	TAnsiStringBuilder<128> Url;
	TAnsiStringBuilder<41>	HashString;

	while (NextHttpRequest)
	{
		while (NextHttpRequest)
		{
			FHttpRequest* HttpRequest	= NextHttpRequest;
			NextHttpRequest				= nullptr;

			ensure(!HttpRequest->Flags.HasAny(EHttpRequestFlags::HttpQueued | EHttpRequestFlags::CacheInflight));

			FIASHostGroup HostGroup = FHostGroupManager::Get().Find(HttpRequest->HostGroupName);
			if (HostGroup.IsConnected() == false)
			{
				// Technically this request is being skipped because of a pre-existing error. It is not
				// an error itself and it is not being canceled by higher level code. However we do not
				// currently have a statistic for that and we have to call one of the existing types in
				// order to correctly reduce the pending count.
				FOnDemandIoBackendStats::Get()->OnHttpCancel(HttpRequest->GetHttpRequestType());
				CompleteRequest(*HttpRequest, HttpRequest->Buffer, FIoHttpRange(), 0, false);

				check(NextHttpRequest == nullptr);
				NextHttpRequest = GetNextRequest();
				continue;
			}

			Url.Reset();
			HashString.Reset();

			TArray<FAnsiString> Headers;
			FIoHttpRange		TotalRange;

			const bool bGenericHttpRequest = HttpRequest->ChunkHash.IsZero();
			if (bGenericHttpRequest)
			{
				ensure(HttpRequest->Next == nullptr);
				Url << *HttpRequest->RelativeUrl;
				Headers = MoveTemp(HttpRequest->Headers).ToArray();
			}
			else
			{
				// I/O store chunk request 
				HashString << HttpRequest->ChunkHash;
				Url << *HttpRequest->RelativeUrl << "/" << HashString.ToView().Left(2) << "/" << HashString << ANSITEXTVIEW(".iochunk");
				TotalRange = GetTotalRange(HttpRequest);
				//DebugPrintRange(HttpRequest, TotalRange);
			}

#if UE_ALLOW_INVALID_URL_DEBUGGING
			// Avoid the rand call if there is no chance
			if (GIaxInvalidUrlChance > 0.0 && (FMath::FRand() * 100.0f) < GIaxInvalidUrlChance)
			{
				Url << "-DebugInvalidUrl";
			}
#endif // UE_ALLOW_INVALID_URL_DEBUGGING

			NumConcurrentRequests++;
			FOnDemandIoBackendStats::Get()->OnHttpRequestStarted();

			EMultiEndpointRequestFlags Flags = EnumHasAnyFlags(HttpRequest->Options.GetFlags(), EIoHttpFlags::ResponseHeaders)
				? EMultiEndpointRequestFlags::ResponseHeaders
				: EMultiEndpointRequestFlags::None;

			// Note that the requests issued here must be completed before this method can return so the capturing of locals is safe.
			FMultiEndpointHttpClient::FHttpTicketId TicketId =  HttpClient->Get(HostGroup.GetUnderlyingHostGroup(), Url, TotalRange.ToOffsetAndLength(), MoveTemp(Headers), Flags,
				[this, HttpRequest, TotalRange, &NumConcurrentRequests, HostGroup = MoveTemp(HostGroup)]
				(FMultiEndpointHttpClientResponse&& HttpResponse) mutable
				{
					FOnDemandIoBackendStats::Get()->OnHttpRequestCompleted();
					NumConcurrentRequests--;

					UE::Tasks::Launch(
						UE_SOURCE_LOCATION,
						[this, HttpRequest, HttpResponse = MoveTemp(HttpResponse), TotalRange, HostGroup = MoveTemp(HostGroup)]() mutable
						{
							CompleteHttpRequest(*HttpRequest, MoveTemp(HttpResponse), HostGroup, TotalRange);
						}, UE::Tasks::ETaskPriority::BackgroundLow);
				});

			{
				TUniqueLock _(CancelationMutex);
			
				TSharedPtr<FCancelationToken> Token = MakeShared<FCancelationToken>(*HttpClient.Get(), TicketId);
				FHttpRequest* RequestList = HttpRequest;
				while (RequestList)
				{
					if (!RequestList->Flags.HasAny(EHttpRequestFlags::CancelRequested))
					{
						RequestList->CancelationToken = Token;
					}

					RequestList = RequestList->Next;
				}
			}

			if (NumConcurrentRequests >= MaxConcurrentRequests)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FHttpDispatcher::TickHttpSaturated);

				while (NumConcurrentRequests >= MaxConcurrentRequests) //-V654
				{
					HttpClient->Tick(MAX_uint32, GIasHttpRateLimitKiBPerSecond); 
				}
			}

			check(NextHttpRequest == nullptr);
			NextHttpRequest = GetNextRequest();
		} // Inner

		{
			check(NextHttpRequest == nullptr);

			// Keep processing pending connections until all requests are completed or a new one is issued
			TRACE_CPUPROFILER_EVENT_SCOPE(FHttpDispatcher::TickHttp);
			while (NextHttpRequest == nullptr && HttpClient->Tick(GIasHttpPollTimeoutMs, GIasHttpRateLimitKiBPerSecond))
			{
				NextHttpRequest = GetNextRequest();
			}
		}
	} // Outer

	// All requests from HttpClient should be completed at this point and all callbacks processed!.
	check(NumConcurrentRequests == 0);

	return true;
}

FHttpRequest* FHttpDispatcher::GetNextRequest()
{
	if (!bRecreateHttpClient)
	{
		return HttpQueue.Dequeue();
	}
	else
	{
		return nullptr;
	}
}

void FHttpDispatcher::CompleteHttpRequest(FHttpRequest& Request, FMultiEndpointHttpClientResponse&& HttpResponse, FIASHostGroup& HostGroup, const FIoHttpRange& TotalRange)
{
	if (HttpResponse.RetryCount > 0)
	{
		FOnDemandIoBackendStats::Get()->OnHttpRetry(Request.GetHttpRequestType());
	}

	if (HttpResponse.IsOk())
	{
		HostGroup.OnSuccessfulResponse();
		FOnDemandIoBackendStats::Get()->OnHttpCdnCacheReply(Request.GetHttpRequestType(), HttpResponse.CDNCacheStatus);
		FOnDemandIoBackendStats::Get()->OnHttpGet(Request.GetHttpRequestType(), HttpResponse.Body.DataSize(), HttpResponse.Sample.GetTotalMs());
	}
	else if (HttpResponse.IsCanceled())
	{
		FOnDemandIoBackendStats::Get()->OnHttpCancel(Request.GetHttpRequestType());
	}
	else
	{
		FOnDemandIoBackendStats::Get()->OnHttpError(Request.GetHttpRequestType());
		if (HostGroup.OnFailedResponse())
		{
			// A disconnect was triggered
			FOnDemandIoBackendStats::Get()->OnHttpDisconnected();
		}
	}

	CompleteRequest(Request, MoveTemp(HttpResponse.Headers), HttpResponse.Body, TotalRange, HttpResponse.StatusCode, false);
}

void FHttpDispatcher::CompleteRequest(FHttpRequest& Request, TArray<FAnsiString>&& Headers, FIoBuffer& Body, const FIoHttpRange& TotalRange, const uint32 StatusCode, bool bCached)
{
#if UE_ALLOW_CACHE_POISONING
	if (GIasPoisonCache && bCached)
	{
		for (uint64 Index = 0; Index < Body.GetSize(); Index++)
		{
			Body.GetData()[Index] = 0x4d;
		}
	}
#endif // UE_ALLOW_CACHE_POISONING

	FHttpRequest* NextRequest = &Request;
	while (NextRequest != nullptr)
	{
		FHttpRequest* ToComplete	= NextRequest;
		NextRequest					= ToComplete->Next;
		ToComplete->Next			= nullptr;

		uint32 RequestStatusCode = StatusCode; // The status code may be modified per request, so take a copy.

		ensure(!ToComplete->Flags.HasAny(EHttpRequestFlags::Completing | EHttpRequestFlags::Completed));
		ToComplete->Flags.Add(EHttpRequestFlags::Completing);
		ToComplete->Flags.Remove(EHttpRequestFlags::CacheInflight | EHttpRequestFlags::HttpInflight);

		const FIoHttpRange& Range		= ToComplete->Options.GetRange();
		EIoErrorCode CompletionStatus	= IsHttpStatusOk(StatusCode) ? EIoErrorCode::Ok : EIoErrorCode::ReadError;
		FIoBuffer PartialBody			= Body;

		if (ToComplete->Flags.HasAny(EHttpRequestFlags::CancelRequested))
		{
			CompletionStatus	= EIoErrorCode::Cancelled;
			PartialBody			= FIoBuffer();
			RequestStatusCode = 0; // Mark as a none http response related error
		}

		// If the request is NOT cached and specified a range we retrieve the partial body
		if (!bCached && CompletionStatus == EIoErrorCode::Ok && Range.IsValid())
		{
			uint64			Offset	= Range.GetMin();
			const uint64	Size	= Range.GetSize();

			// If the total range is NOT valid we fetched the entire resource
			if (TotalRange.IsValid())
			{
				ensure(Range.GetMin() >= TotalRange.GetMin());
				ensure(Range.GetMax() <= TotalRange.GetMax());
				Offset -= TotalRange.GetMin();
			}

			PartialBody = FIoBuffer(Body.GetView().Mid(Offset, Size), Body);
		}

		const EIoHttpResponseFlags Flags	= bCached ? EIoHttpResponseFlags::Cached : EIoHttpResponseFlags::None;
		FIoHttpRequestCompleted OnCompleted = MoveTemp(ToComplete->OnCompleted);
		OnCompleted(FIoHttpResponse(ToComplete->CacheKey, FIoHttpHeaders::Create(MoveTemp(Headers)), PartialBody, CompletionStatus, RequestStatusCode, Flags));

		ToComplete->Flags.Add(EHttpRequestFlags::Completed);
		ToComplete->CompletionStatus.store(CompletionStatus, std::memory_order_seq_cst);
		ToComplete->Release();
	}
}

#if UE_ALLOW_HTTP_PAUSE

void FHttpDispatcher::OnPauseCommand(bool bShouldPause, const TArray<FString>& Args, FOutputDevice& Ar)
{
	if (Args.Num() > 1)
	{
#if !NO_LOGGING
		Ar.Log(LogHttpIoDispatcher.GetCategoryName(), ELogVerbosity::Error, TEXT("Too many args for command, either 0 (all) or 1 (IAD/IAS) expected"));
#endif // !NO_LOGGING
		return;
	}

	EHttpRequestTypeFilter Filter = EHttpRequestTypeFilter::All;

	if (!Args.IsEmpty())
	{
		if (Args[0] == TEXT("IAD"))
		{
			Filter = EHttpRequestTypeFilter::Installed;
		}
		else if (Args[0] == TEXT("IAS"))
		{
			Filter = EHttpRequestTypeFilter::Streaming;
		}
		else
		{
#if !NO_LOGGING
			Ar.Log(LogHttpIoDispatcher.GetCategoryName(), ELogVerbosity::Error, TEXT("Invalid arg for command, expecting 'IAD' or 'IAS'"));
#endif // !NO_LOGGING
			return;
		}
	}

	HttpQueue.OnTogglePause(bShouldPause, Filter);

	if (!bShouldPause)
	{
		// Since we are unpausing we might have moved previously paused requests back to the active queue
		// so we need the thread to start runnning again.
		WakeUp->Trigger();
	}
}

#endif // UE_ALLOW_HTTP_PAUSE

} // namespace UE::IoStore::HttpIoDispatcher

///////////////////////////////////////////////////////////////////////////////
TSharedPtr<IOnDemandHttpIoDispatcher> MakeOnDemanHttpIoDispatcher(TUniquePtr<class IIasCache>&& Cache)
{
	return MakeShared<HttpIoDispatcher::FHttpDispatcher>(MoveTemp(Cache));
}

} // UE::IoStore

#undef UE_ALLOW_CACHE_POISONING

#undef UE_ALLOW_INVALID_URL_DEBUGGING

