// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnDemandContentInstaller.h"

#include "Algo/Accumulate.h"
#include "Async/UniqueLock.h"
#include "Containers/RingBuffer.h"
#include "Experimental/UnifiedError/CoreErrorTypes.h"
#include "HAL/Platform.h"
#include "IO/IoBuffer.h"
#include "IO/IoChunkId.h"
#include "IO/IoDispatcher.h"
#include "IO/IoStatus.h"
#include "IO/IoStoreOnDemand.h"
#include "IO/OnDemandError.h"
#include "Misc/Timespan.h"
#include "OnDemandHttpThread.h"
#include "OnDemandInstallCache.h"
#include "OnDemandIoStore.h"
#include "OnDemandPackageStoreBackend.h"
#include "Serialization/PackageStore.h"
#include "Statistics.h"
#include "Templates/Overload.h"
#include <atomic>

namespace UE::IoStore
{
	namespace CVars
	{
#if !UE_BUILD_SHIPPING
		static FString IoStoreErrorOnRequest = "";
		static FAutoConsoleVariableRef CVar_IoStoreErrorOnRequest(
			TEXT("iostore.ErrorOnRequest"),
			IoStoreErrorOnRequest,
			TEXT("When the request with a debug name partially matching this cvar is found iostore will error with a random error.")
			);

		FString IoStoreDebugRequest = "";
		static FAutoConsoleVariableRef CVar_IoStoreDebugRequest(
			TEXT("iostore.DebugRequest"),
			IoStoreDebugRequest,
			TEXT("When the request with a debug name partially matching this cvar is found iostore will error with a random error.")
		);
#endif

		static bool bContentInstallerFlushOnIdle = false;
		static FAutoConsoleVariableRef CVar_ContentInstallerFlushOnIdle(
			TEXT("iostore.ContentInstallerFlushOnIdle"), 
			bContentInstallerFlushOnIdle,
			TEXT("When the content installer runs out of tasks, it will flush any pending iochunks to disk.")
			);

		static bool bContentInstallerEnableHttpCancel = true;
		static FAutoConsoleVariableRef CVar_ContentInstallerEnableHttpCancel(
			TEXT("iostore.ContentInstallerEnableHttpCancel"),
			bContentInstallerEnableHttpCancel,
			TEXT("Enable canceling HTTP requests when an install request is canceled")
			);

		// Can't safely change this on the fly. Don't call directly
		static TAutoConsoleVariable<bool> CVar_EnableConcurrentRequests(
			TEXT("iostore.ContentInstallerEnableConcurrentInstallRequests"),
			false,
			TEXT("Enable the content installer to install chunks for multiple requests concurrently."),
			ECVF_SaveForNextBoot
			);

		static bool EnableConcurentInstallRequests()
		{
			static bool bEnableConcurentInstallRequests = CVar_EnableConcurrentRequests.GetValueOnAnyThread();
			return bEnableConcurentInstallRequests;
		}
	}

namespace Private
{

////////////////////////////////////////////////////////////////////////////////
int32 SecondsToMillicseconds(double Seconds)
{
	return Seconds > 0.0 ? int32(Seconds * 1000.0) : 0;
}

////////////////////////////////////////////////////////////////////////////////
void ResolvePackageDependencies(
	const TSet<FPackageId>& PackageIds,
	bool bIncludeSoftReferences,
	TSet<FPackageId>& OutResolved,
	TSet<FPackageId>& OutMissing)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandContentInstaller::ResolvePackageDependencies);

	TRingBuffer<FPackageId> Queue;
	TSet<FPackageId>		Visitied;

	Visitied.Reserve(PackageIds.Num());
	Queue.Reserve(PackageIds.Num());
	for (const FPackageId& PackageId : PackageIds)
	{
		Queue.Add(PackageId);
	}

	FPackageStore& PackageStore = FPackageStore::Get();
	FPackageStoreReadScope _(PackageStore);

	while (!Queue.IsEmpty())
	{
		FPackageId PackageId = Queue.PopFrontValue();
		{
			FName		SourcePackageName;
			FPackageId	RedirectedToPackageId;
			if (PackageStore.GetPackageRedirectInfo(PackageId, SourcePackageName, RedirectedToPackageId))
			{
				PackageId = RedirectedToPackageId;
			}
		}

		bool bIsAlreadyInSet = false;
		Visitied.Add(PackageId, &bIsAlreadyInSet);
		if (bIsAlreadyInSet)
		{
			continue;
		}

		FPackageStoreEntry PackageStoreEntry;
		const EPackageStoreEntryStatus EntryStatus = PackageStore.GetPackageStoreEntry(PackageId, NAME_None, PackageStoreEntry);
		if (EntryStatus != EPackageStoreEntryStatus::Missing)
		{
			OutResolved.Add(PackageId);
			for (const FPackageId& ImportedPackageId : PackageStoreEntry.ImportedPackageIds)
			{
				if (!Visitied.Contains(ImportedPackageId))
				{
					Queue.Add(ImportedPackageId);
				}
			}

			if (bIncludeSoftReferences)
			{
				TConstArrayView<FPackageId> SoftReferences;
				TConstArrayView<uint32> Indices = PackageStore.GetSoftReferences(PackageId, SoftReferences);
				for (uint32 Idx : Indices)
				{
					const FPackageId& SoftRef = SoftReferences[Idx];
					if (!Visitied.Contains(SoftRef))
					{
						Queue.Add(SoftRef);
					}
				}
			}
		}
		else
		{
			OutMissing.Add(PackageId);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
void ResolveChunksToInstall(
	const TSet<FSharedOnDemandContainer>& Containers,
	const TSet<FPackageId>& PackageIds,
	bool bIncludeSoftReferences,
	bool bIncludeOptionalBulkData,
	TArray<FResolvedContainerChunks>& OutResolvedContainerChunks,
	TSet<FPackageId>& OutMissing)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandContentInstaller::ResolveChunksToInstall);

	// For now we always download these required chunks
	for (const FSharedOnDemandContainer& Container : Containers)
	{
		FResolvedContainerChunks& ResolvedChunks = OutResolvedContainerChunks.AddDefaulted_GetRef();
		ResolvedChunks.Container = Container;

		for (int32 EntryIndex = 0; const FIoChunkId& ChunkId : Container->ChunkIds)
		{
			switch(ChunkId.GetChunkType())
			{
				case EIoChunkType::ExternalFile:
				case EIoChunkType::ShaderCodeLibrary:
				case EIoChunkType::ShaderCode:
				{
					ResolvedChunks.EntryIndices.Emplace(EntryIndex); 
					ResolvedChunks.TotalSize += Container->ChunkEntries[EntryIndex].GetDiskSize();
				}
				default:
					break;
			}
			++EntryIndex;
		}
	}

	auto FindChunkEntry = [&OutResolvedContainerChunks](const FIoChunkId& ChunkId, int32& OutIndex) -> FResolvedContainerChunks*
	{
		for (FResolvedContainerChunks& ContainerChunks : OutResolvedContainerChunks)
		{
			if (OutIndex = ContainerChunks.Container->FindChunkEntryIndex(ChunkId); OutIndex != INDEX_NONE)
			{
				return &ContainerChunks; 
			}
		}
		return nullptr; 
	};

	TSet<FPackageId> ResolvedPackageIds;
	ResolvePackageDependencies(
		PackageIds,
		bIncludeSoftReferences,
		ResolvedPackageIds,
		OutMissing);

	// Resolve all chunk entries from the resolved package ID's
	for (const FPackageId& PackageId : ResolvedPackageIds)
	{
		const FIoChunkId PackageChunkId				= CreatePackageDataChunkId(PackageId);
		int32 EntryIndex							= INDEX_NONE; 
		FResolvedContainerChunks* ResolvedChunks	= FindChunkEntry(PackageChunkId, EntryIndex);

		if (ResolvedChunks == nullptr) 
		{
			// The chunk resides in a base game container
			continue;
		}

		check(EntryIndex != INDEX_NONE);
		FOnDemandContainer& Container	= *ResolvedChunks->Container; 

		ResolvedChunks->EntryIndices.Emplace(EntryIndex); 
		ResolvedChunks->TotalSize += Container.ChunkEntries[EntryIndex].GetDiskSize();

		static constexpr const EIoChunkType RequiredChunkTypes[] =
		{
			EIoChunkType::BulkData,
			EIoChunkType::MemoryMappedBulkData 
		};

		static constexpr const EIoChunkType RequiredAndOptionalChunkTypes[] =
		{
			EIoChunkType::BulkData,
			EIoChunkType::OptionalBulkData,
			EIoChunkType::MemoryMappedBulkData 
		};

		TConstArrayView<EIoChunkType> AdditionalChunkTypes = bIncludeOptionalBulkData
			? MakeArrayView<const EIoChunkType>(RequiredAndOptionalChunkTypes, UE_ARRAY_COUNT(RequiredAndOptionalChunkTypes))
			: MakeArrayView<const EIoChunkType>(RequiredChunkTypes, UE_ARRAY_COUNT(RequiredChunkTypes));

		for (EIoChunkType ChunkType : AdditionalChunkTypes)
		{
			// TODO: For Mutable we need to traverse all possible bulk data chunk indices?
			const FIoChunkId ChunkId = CreateBulkDataIoChunkId(PackageId.Value(), 0, 0, ChunkType);
			if (ResolvedChunks = FindChunkEntry(ChunkId, EntryIndex); ResolvedChunks != nullptr)
			{
				check(EntryIndex != INDEX_NONE);
				FOnDemandContainer& OtherContainer = *ResolvedChunks->Container;
				ResolvedChunks->EntryIndices.Emplace(EntryIndex);
				ResolvedChunks->TotalSize += OtherContainer.ChunkEntries[EntryIndex].GetDiskSize();
			}
		}
	}
}

} // namespace Private

////////////////////////////////////////////////////////////////////////////////
uint32 FOnDemandContentInstaller::FRequest::NextSeqNo = 0;

TOptional<UE::UnifiedError::FError> FOnDemandContentInstaller::FRequest::ConsumeError()
{
	if (Result.HasError())
	{
		return TOptional<UE::UnifiedError::FError>(Result.StealError());
	}
	else if (IsCancelled())
	{
		return TOptional<UE::UnifiedError::FError>(UE::UnifiedError::Core::CancellationError::MakeError());
	}

	return TOptional<UE::UnifiedError::FError>();
}

////////////////////////////////////////////////////////////////////////////////
FOnDemandContentInstaller::FOnDemandContentInstaller(FOnDemandIoStore& InIoStore, FOnDemandHttpThread* InHttpClient)
	: IoStore(InIoStore)
	, HttpClient(InHttpClient)
	, InstallerPipe(TEXT("IoStoreOnDemandInstallerPipe"))
{
}

FOnDemandContentInstaller::~FOnDemandContentInstaller()
{
	Shutdown();
}

FSharedInternalInstallRequest FOnDemandContentInstaller::EnqueueInstallRequest(
	FOnDemandInstallArgs&& Args,
	FOnDemandInstallCompleted&& OnCompleted,
	FOnDemandInstallProgressed&& OnProgress)
{
	FRequest* Request = nullptr;
	{
		TUniqueLock Lock(Mutex);
		Request = RequestAllocator.Construct(MoveTemp(Args), MoveTemp(OnCompleted), MoveTemp(OnProgress));
	}

	FSharedInternalInstallRequest InstallRequest = MakeShared<FOnDemandInternalInstallRequest, ESPMode::ThreadSafe>(UPTRINT(Request));
	Request->AsInstall().Request = InstallRequest;

	FOnDemandContentInstallerStats::OnRequestEnqueued();

	InstallerPipe.Launch(
		TEXT("ProcessIoStoreOnDemandInstallRequest"),
		[this, Request] { ProcessInstallRequest(*Request); },
		UE::Tasks::ETaskPriority::BackgroundLow);

	return InstallRequest;
}

void FOnDemandContentInstaller::EnqueuePurgeRequest(FOnDemandPurgeArgs&& Args, FOnDemandPurgeCompleted&& OnCompleted)
{
	{
		TUniqueLock Lock(Mutex);
		RequestQueue.HeapPush(RequestAllocator.Construct(MoveTemp(Args), MoveTemp(OnCompleted)), RequestSortPredicate);
	}

	TryExecuteNextRequest();
}

void FOnDemandContentInstaller::EnqueueDefragRequest(FOnDemandDefragArgs&& Args, FOnDemandDefragCompleted&& OnCompleted)
{
	{
		TUniqueLock Lock(Mutex);
		RequestQueue.HeapPush(RequestAllocator.Construct(MoveTemp(Args), MoveTemp(OnCompleted)), RequestSortPredicate);
	}

	TryExecuteNextRequest();
}

void FOnDemandContentInstaller::EnqueueVerifyRequest(FOnDemandVerifyCacheCompleted&& OnCompleted)
{
	{
		TUniqueLock Lock(Mutex);
		RequestQueue.HeapPush(RequestAllocator.Construct(MoveTemp(OnCompleted)), RequestSortPredicate);
	}

	TryExecuteNextRequest();
}

void FOnDemandContentInstaller::EnqueueFlushLastAccessRequest(FOnDemandFlushLastAccessCompleted&& OnCompleted)
{
	{
		TUniqueLock Lock(Mutex);
		RequestQueue.HeapPush(RequestAllocator.Construct(MoveTemp(OnCompleted)), RequestSortPredicate);
	}

	TryExecuteNextRequest();
}

void FOnDemandContentInstaller::CancelInstallRequest(FSharedInternalInstallRequest InstallRequest)
{
	InstallerPipe.Launch(
		TEXT("CancelIoStoreOnDemandInstallRequest"),
		[this, InstallRequest]
		{
			FRequest* ToComplete = nullptr;
			{
				if (InstallRequest->InstallerRequest == 0)
				{
					return;
				}

				FRequest* Request = reinterpret_cast<FRequest*>(InstallRequest->InstallerRequest);
				FRequest::FInstall& Install = Request->AsInstall();

				if (Install.bHttpRequestsIssued)
				{
					if (Request->TryCancel() == false)
					{
						return;
					}

					UE_LOG(LogIoStoreOnDemand, Log, TEXT("Cancelling install request, ContentHandle=(%s)"),
						*LexToString(Install.Args.ContentHandle));

					int32 NumCancelled = 0;
					TryCancelHttpRequestsForInstallRequest(Install, NumCancelled);
					UE_LOG(LogIoStoreOnDemand, Verbose, TEXT("Cancelled %d HTTP request(s) due to install request cancellation"), NumCancelled);
				}
				else
				{
					if (Request->TryCancel() == false)
					{
						return;
					}

					UE_LOG(LogIoStoreOnDemand, Log, TEXT("Cancelling install request, ContentHandle=(%s)"),
						*LexToString(Install.Args.ContentHandle));

					TUniqueLock Lock(Mutex);

					if (RequestQueue.Remove(Request) > 0)
					{
						ToComplete = Request;
						RequestQueue.Heapify(RequestSortPredicate);
					}
				}
			}

			if (ToComplete != nullptr)
			{
				CompleteInstallRequest(*ToComplete);
			}
		});
}

void FOnDemandContentInstaller::UpdateInstallRequestPriority(FSharedInternalInstallRequest InstallRequest, const int32 NewPriority)
{
	InstallerPipe.Launch(
		TEXT("UpdateIoStoreOnDemandInstallRequestPriority"),
		[this, InstallRequest, NewPriority]
		{
			if (InstallRequest->InstallerRequest == 0)
			{
				return;
			}

			FRequest&			Request	= *reinterpret_cast<FRequest*>(InstallRequest->InstallerRequest);
			FRequest::FInstall& Install	= Request.AsInstall();

			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Updating install request priority, SeqNo=%u, Priority=%d, NewPriority=%d, ContentHandle=(%s)"),
				Request.SeqNo, Request.Priority, NewPriority, *LexToString(Install.Args.ContentHandle));

			if (Install.bHttpRequestsIssued)
			{
				// The request has definitely left the RequestQueue 

				Request.Priority = NewPriority;

				for (FChunkHttpRequestHandle& PendingHttpRequest : Install.HttpRequestHandles)
				{
					const FSharedOnDemandContainer& Container = Install.ResolvedChunks[PendingHttpRequest.ContainerIndex].Container;
					const FOnDemandChunkEntry& ChunkEntry = Container->ChunkEntries[PendingHttpRequest.EntryIndex];
					const FIoHash& ChunkHash = ChunkEntry.Hash;

					for (auto It = PendingChunkDownloads.CreateKeyIterator(ChunkHash); It; ++It)
					{
						FChunkDownloadRequest& ChunkDownload = It.Value();
						if (ChunkDownload.bChunkCanceled)
						{
							continue;
						}

						// Use the maximum priority of all requests waiting on this chunk
						int32 MaxPriority = std::numeric_limits<int32>::min();
						for (FChunkHttpRequestHandle& Handle : ChunkDownload.ChunkRequestHttpHandles)
						{
							const int32 RequestPriority = Handle.OwnerRequest->Priority;
							if (RequestPriority > MaxPriority)
							{
								MaxPriority = RequestPriority;
							}
						}

						Visit(UE::Overload(
							[MaxPriority](FIoHttpRequest& Handle)
							{
								Handle.UpdatePriorty(MaxPriority);
							},
							[this, MaxPriority](void* Handle)
							{
								check(HttpClient != nullptr);
								if (Handle != nullptr)
								{
									HttpClient->ReprioritizeRequest(Handle, MaxPriority);
								}
							}), 
							ChunkDownload.HttpHandle);

						// Should be a max of one active request for any chunk
						break;
					}
				}
			}
			else
			{
				// The request may or may not still be in the RequestQueue
				TUniqueLock Lock(Mutex);
				Request.Priority = NewPriority;
				RequestQueue.Heapify(RequestSortPredicate);
			}
		});
}

void FOnDemandContentInstaller::ReportAnalytics(TArray<FAnalyticsEventAttribute>& OutAnalyticsArray) const
{
	FOnDemandContentInstallerStats::ReportAnalytics(OutAnalyticsArray);
}

bool FOnDemandContentInstaller::CanExecuteRequest(FRequest& Request) const
{
	bool bExecute = false;

	if (CVars::EnableConcurentInstallRequests())
	{
		// Allow multiple install requests XOR any other request
		bExecute = RunningRequests.IsEmpty() ||
			(
				Request.IsInstall() &&
					(RunningRequests.Num() > 1 || RunningRequests[0]->IsInstall())
			);
	}
	else
	{
		bExecute = RunningRequests.IsEmpty();
	}

	return bExecute;
}

void FOnDemandContentInstaller::TryExecuteNextRequest()
{
	if (bShuttingDown.load(std::memory_order_relaxed))
	{
		return;
	}

	bool bRequestQueueIsEmpty = false;
	while (bRequestQueueIsEmpty == false)
	{
		FRequest* NextRequest = nullptr;
		{
			TUniqueLock Lock(Mutex);
			bRequestQueueIsEmpty = RequestQueue.IsEmpty();
			if (bRequestQueueIsEmpty == false)
			{
				if (CanExecuteRequest(*RequestQueue.HeapTop()))
				{
					RequestQueue.HeapPop(NextRequest, RequestSortPredicate, EAllowShrinking::No);
					RunningRequests.Add(NextRequest);
				}
			}
		}

		if (NextRequest == nullptr)
		{
			break;
		}

		InstallerPipe.Launch(
			TEXT("OnDemandContentInstaller::ExecuteRequest"),
			[this, NextRequest] { ExecuteRequest(*NextRequest); },
			UE::Tasks::ETaskPriority::BackgroundLow);

		// If we aren't doing an install request, we only allow one running request so 
		// stop trying to dequeue requests.
		if (NextRequest->IsInstall() == false)
		{
			break;
		}
	}


	if (bRequestQueueIsEmpty && UE::IoStore::CVars::bContentInstallerFlushOnIdle)
	{		
		bool bRunningRequestsIsEmpty = false;
		{
			TUniqueLock Lock(Mutex);
			bRunningRequestsIsEmpty = RunningRequests.IsEmpty();
		}

		if (bRunningRequestsIsEmpty)
		{
			// Flush any pending chunks, but only if the installer is otherwise idle
			InstallerPipe.Launch(
				TEXT("OnDemandContentInstaller::Flush"),
				[this]
				{
					bool bExecuteFlush = false;
					{
						TUniqueLock Lock(Mutex);
						bExecuteFlush = RequestQueue.IsEmpty() && RunningRequests.IsEmpty();
					}

					if (bExecuteFlush)
					{
						FResult Result = IoStore.InstallCache->Flush();
						if (Result.HasError())
						{
							UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to flush pending chunks, Reason %s"), *LexToString(Result.GetError()));
						}
					}
				},
				UE::Tasks::ETaskPriority::BackgroundLow);
		}
	}
}

void FOnDemandContentInstaller::ExecuteRequest(FRequest& Request)
{
	struct FVisitor
	{
		void operator()(FEmptyVariantState& Empty)
		{
			ensure(false);
		}

		void operator()(FRequest::FInstall&)
		{
			Installer.ExecuteInstallRequest(Request);
		}

		void operator()(FRequest::FPurge&)
		{
			Installer.ExecutePurgeRequest(Request);
		}

		void operator()(FRequest::FDefrag&)
		{
			Installer.ExecuteDefragRequest(Request);
		}

		void operator()(FRequest::FVerify&)
		{
			Installer.ExecuteVerifyRequest(Request);
		}

		void operator()(FRequest::FFlushLastAccess&)
		{
			Installer.ExectuteFlushLastAccessRequest(Request);
		}

		FOnDemandContentInstaller&	Installer;
		FRequest&					Request;
	};

	FVisitor Visitor { .Installer = *this, .Request = Request };
	Visit(Visitor, Request.Variant);
}

void FOnDemandContentInstaller::PinCachedChunks(FOnDemandContentInstaller::FRequest::FInstall& InstallRequest, TFunctionRef<void(int32, int32, bool)> OnChunkFound) const
{
	const TSharedPtr<FOnDemandInternalContentHandle>& ContentHandle = InstallRequest.Args.ContentHandle.Handle;

	// Find all chunks we need to fetch from the resolved chunk(s)
	
	for (int32 ContainerIndex = 0; Private::FResolvedContainerChunks& ResolvedChunks : InstallRequest.ResolvedChunks)
	{
		TArray<int32, TInlineAllocator<64>> CachedEntryIndices;
		for (int32 EntryIndex : ResolvedChunks.EntryIndices)
		{
			const FOnDemandChunkEntry& Entry = ResolvedChunks.Container->ChunkEntries[EntryIndex];
			const bool bCached = IoStore.InstallCache->IsChunkCached(Entry.Hash);
			if (bCached)
			{
				CachedEntryIndices.Add(EntryIndex);
			}
			
			OnChunkFound(ContainerIndex, EntryIndex, bCached);
		}

		// Add references to existing chunk(s)
		if (CachedEntryIndices.IsEmpty() == false)
		{
			TUniqueLock Lock(IoStore.ContainerMutex);

			FOnDemandChunkEntryReferences& References = ResolvedChunks.Container->FindOrAddChunkEntryReferences(*ContentHandle);
			for (int32 EntryIndex : CachedEntryIndices)
			{
				References.Indices[EntryIndex] = true;
			}
		}

		ContainerIndex++;
	}
}

void FOnDemandContentInstaller::ProcessInstallRequest(FRequest& Request)
{
	using namespace UE::IoStore::Private;

	TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandContentInstaller::ProcessInstallRequest);

	FRequest::FInstall& InstallRequest	= Request.AsInstall();
	Request.Priority					= InstallRequest.Args.Priority;

	UE_LOG(LogIoStoreOnDemand, Verbose, TEXT("Processing install request, SeqNo=%u, Priority=%d, ContentHandle=(%s)"),
		Request.SeqNo, Request.Priority, *LexToString(InstallRequest.Args.ContentHandle));

	if (InstallRequest.Args.ContentHandle.IsValid() == false)
	{
		Request.Result = MakeError(UE::UnifiedError::Core::ArgumentError::MakeError(TEXT("ContentHandle"), TEXT("Invalid content handle")));
		return CompleteInstallRequest(Request);
	}

	const TSharedPtr<FOnDemandInternalContentHandle>& ContentHandle = InstallRequest.Args.ContentHandle.Handle;
	if (ContentHandle->IoStore.IsValid() == false)
	{
		// First time this content handle is used
		ContentHandle->IoStore = IoStore.AsWeak();
	}

	TSet<FSharedOnDemandContainer> ContainersForInstallation;
	TSet<FPackageId> PackageIdsToInstall;
	if (FIoStatus Status = IoStore.GetContainersAndPackagesForInstall(
		InstallRequest.Args.MountId,
		InstallRequest.Args.TagSets,
		InstallRequest.Args.PackageIds,
		ContainersForInstallation,
		PackageIdsToInstall); !Status.IsOk())
	{
		Request.Result = MakeError(UE::UnifiedError::Core::ArgumentError::MakeError(TEXT("InstallArgs"), Status.ToString()));
		return CompleteInstallRequest(Request);
	}

#if !UE_BUILD_SHIPPING
	if (!UE::IoStore::CVars::IoStoreErrorOnRequest.IsEmpty())
	{
		if (FCString::Strstr(*ContentHandle->DebugName, *UE::IoStore::CVars::IoStoreErrorOnRequest) != nullptr || 
			FCString::Strstr(*InstallRequest.Args.DebugName, *UE::IoStore::CVars::IoStoreErrorOnRequest) != nullptr)
		{
			Request.Result = MakeError(
				UE::UnifiedError::Core::ArgumentError::MakeError(
					TEXT("InstallArgs"),
					FString::Printf(TEXT("Debug error requested on debug name %s"), *UE::IoStore::CVars::IoStoreErrorOnRequest)));
			return CompleteInstallRequest(Request);
		}
	}

	if (!UE::IoStore::CVars::IoStoreDebugRequest.IsEmpty())
	{
		if (FCString::Strstr(*ContentHandle->DebugName, *UE::IoStore::CVars::IoStoreDebugRequest) != nullptr ||
			FCString::Strstr(*InstallRequest.Args.DebugName, *UE::IoStore::CVars::IoStoreDebugRequest) != nullptr)
		{
			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Place breakpoint here to debug FOnDemandContentInstaller::ProcessInstallRequest for asset %s"), *ContentHandle->DebugName);
		}
	}
#endif

	if (Request.IsCancelled())
	{
		return CompleteInstallRequest(Request);
	}

	TSet<FPackageId> Missing;
	const bool bIncludeSoftReferences	= EnumHasAnyFlags(InstallRequest.Args.Options, EOnDemandInstallOptions::InstallSoftReferences);
	const bool bIncludeOptionalBulkData = EnumHasAnyFlags(InstallRequest.Args.Options, EOnDemandInstallOptions::InstallOptionalBulkData);
	Private::ResolveChunksToInstall(
		ContainersForInstallation,
		PackageIdsToInstall,
		bIncludeSoftReferences,
		bIncludeOptionalBulkData,
		InstallRequest.ResolvedChunks,
		Missing);

	// Check the other I/O backends for missing package chunks
	if (Missing.IsEmpty() == false)
	{
		FIoDispatcher& IoDispatcher = FIoDispatcher::Get();
		TArray<FIoChunkId> MissingChunkIds;
		for (const FPackageId& PackageId : Missing)
		{
			const FIoChunkId ChunkId = CreatePackageDataChunkId(PackageId);
			if (IoDispatcher.DoesChunkExist(ChunkId) == false)
			{
				UE_CLOG(MissingChunkIds.Num() == 0, LogIoStoreOnDemand, Warning, TEXT("Failed to resolve the following chunk(s) for content handle '%s':"),
					*LexToString(InstallRequest.Args.ContentHandle));

				UE_LOG(LogIoStoreOnDemand, Warning, TEXT("\tChunkId='%s'"), *LexToString(ChunkId));
				MissingChunkIds.Add(ChunkId);
			}
		}

		if (MissingChunkIds.IsEmpty() == false && !EnumHasAnyFlags(InstallRequest.Args.Options, EOnDemandInstallOptions::AllowMissingDependencies))
		{
			Request.Result = MakeError(UE::UnifiedError::IoStoreOnDemand::ChunkMissingError::MakeError(
				UE::UnifiedError::IoStoreOnDemand::FChunkMissingErrorContext
				{
					.ChunkIds = MoveTemp(MissingChunkIds)
				}));
			return CompleteInstallRequest(Request);
		}
	}

	if (Request.IsCancelled())
	{
		return CompleteInstallRequest(Request);
	}

	bool bExecuteRequest = false;
	{
		TUniqueLock Lock(Mutex);
		if (CanExecuteRequest(Request))
		{
			RunningRequests.Add(&Request);
			bExecuteRequest = true;
		}
	}

	if (bExecuteRequest)
	{
		// Execute immediately, let ExecuteInstallRequest handle chunk pinning
		ExecuteInstallRequest(Request);
		return;
	}

	uint64 TotalContentSize	= 0;
	uint64 TotalInstallSize	= 0;

	// Pin any already cached chunks while waiting for execution
	PinCachedChunks(InstallRequest, [&InstallRequest, &TotalContentSize, &TotalInstallSize](const int32 ContainerIndex, const int32 EntryIndex, const bool bCached)
	{
		const FOnDemandChunkEntry& Entry = InstallRequest.ResolvedChunks[ContainerIndex].Container->ChunkEntries[EntryIndex];
		TotalContentSize += Entry.GetDiskSize();

		if (bCached)
		{
			return;
		}
		
		TotalInstallSize += Entry.GetDiskSize();
	});

	InstallRequest.Progress.TotalContentSize	= TotalContentSize;
	InstallRequest.Progress.TotalInstallSize	= TotalInstallSize;
	InstallRequest.Progress.CurrentInstallSize	= 0;

	if (TotalInstallSize == 0)
	{
		return CompleteInstallRequest(Request);
	}

	if (Request.IsCancelled())
	{
		return CompleteInstallRequest(Request);
	}

	{
		TUniqueLock Lock(Mutex);
		RequestQueue.HeapPush(&Request, RequestSortPredicate);
	}
}

void FOnDemandContentInstaller::ExecuteInstallRequest(FRequest& Request)
{
	check(Request.IsInstall());
	check(RunningRequests.Contains(&Request));

	FRequest::FInstall& InstallRequest = Request.AsInstall();
	check(InstallRequest.HttpRequestHandles.IsEmpty());

	UE_LOG(LogIoStoreOnDemand, Verbose, TEXT("Executing install request, SeqNo=%u, Priority=%d, ContentHandle=(%s)"),
		Request.SeqNo, Request.Priority, *LexToString(InstallRequest.Args.ContentHandle));

	if (Request.IsCancelled())
	{
		return CompleteInstallRequest(Request);
	}

	uint64 TotalContentSize = 0;
	uint64 TotalInstallSize	= 0;

	PinCachedChunks(InstallRequest, [&Request, &TotalContentSize, &TotalInstallSize](const int32 ContainerIndex, const int32 EntryIndex, const bool bCached)
	{
		FRequest::FInstall& InstallRequest = Request.AsInstall();
		const FOnDemandChunkEntry& Entry = InstallRequest.ResolvedChunks[ContainerIndex].Container->ChunkEntries[EntryIndex];
		TotalContentSize += Entry.GetDiskSize();

		if (bCached)
		{
			return;
		}

		InstallRequest.HttpRequestHandles.Add(FChunkHttpRequestHandle
		{
			.OwnerRequest	= &Request,
			.ContainerIndex = ContainerIndex,
			.EntryIndex		= EntryIndex
		});
		TotalInstallSize += Entry.GetDiskSize();
	});

	// TotalInstallSize may be different now
	InstallRequest.Progress.TotalContentSize = TotalContentSize;
	InstallRequest.Progress.TotalInstallSize = TotalInstallSize;
	InstallRequest.Progress.CurrentInstallSize = 0;

	if (InstallRequest.HttpRequestHandles.IsEmpty())
	{
		return CompleteInstallRequest(Request);
	}

	if (EnumHasAnyFlags(InstallRequest.Args.Options, EOnDemandInstallOptions::DoNotDownload))
	{
		Request.Result = MakeError(UE::UnifiedError::Core::ArgumentError::MakeError(TEXT("Options"), TEXT("DoNotDownload flag specified when download required")));
		return CompleteInstallRequest(Request);
	}

	
	if (CVars::EnableConcurentInstallRequests() == false)
	{
		// Make sure we have enough space in the cache
		uint64 BytesToInstall = 0;
		{
			TSet<FIoHash> ChunksToInstall;
			ChunksToInstall.Reserve(InstallRequest.HttpRequestHandles.Num());
			for (FChunkHttpRequestHandle& HttpRequest : InstallRequest.HttpRequestHandles)
			{
				FSharedOnDemandContainer& Container = InstallRequest.ResolvedChunks[HttpRequest.ContainerIndex].Container;
				const FOnDemandChunkEntry& ChunkEntry = Container->ChunkEntries[HttpRequest.EntryIndex];

				bool bAlreadyInSet = false;
				ChunksToInstall.Add(ChunkEntry.Hash, &bAlreadyInSet);
				if (!bAlreadyInSet)
				{
					BytesToInstall += ChunkEntry.GetDiskSize();
				}
			}
		}

		if (Request.Result = IoStore.InstallCache->Purge(BytesToInstall); Request.Result.HasError())
		{
			return CompleteInstallRequest(Request);
		}
	}

	if (Request.IsCancelled())
	{
		return CompleteInstallRequest(Request);
	}

	NotifyInstallProgress(Request);

	const bool bUseHttpIoDispatcher = FHttpIoDispatcher::IsInitialized();
	check(bUseHttpIoDispatcher || HttpClient != nullptr);

	TOptional<FIoHttpBatch> Batch;
	if (bUseHttpIoDispatcher)
	{
		Batch.Emplace(FHttpIoDispatcher::NewBatch());
	}

	constexpr const int32 HttpRetryCount = 2;
	for (FChunkHttpRequestHandle& HttpRequest : InstallRequest.HttpRequestHandles)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandContentInstaller::IssueRequest);
		FSharedOnDemandContainer& Container		= InstallRequest.ResolvedChunks[HttpRequest.ContainerIndex].Container;
		FOnDemandChunkInfo ChunkInfo			= FOnDemandChunkInfo(Container, Container->ChunkEntries[HttpRequest.EntryIndex]);
		const uint8 HttpCategory				= 1; // see FOnDemandHttpThread

		FChunkDownloadRequest* PendingChunkRequest = nullptr;
		for (auto It = PendingChunkDownloads.CreateKeyIterator(ChunkInfo.ChunkEntry().Hash); It; ++It)
		{
			FChunkDownloadRequest& ChunkDownload = It.Value();
			if (ChunkDownload.bChunkCanceled == false)
			{
				PendingChunkRequest = &ChunkDownload;
				break; // Should be a max of one active request for any chunk
			}
		}

		if (PendingChunkRequest)
		{
			PendingChunkRequest->ChunkRequestHttpHandles.AddHead(&HttpRequest);
		}
		else
		{
			const FIoHash& ChunkHash = ChunkInfo.ChunkEntry().Hash;

			PendingChunkRequest = &PendingChunkDownloads.Add(ChunkHash);
			PendingChunkRequest->RequestId = Request.SeqNo;
			PendingChunkRequest->ChunkRequestHttpHandles.AddHead(&HttpRequest);

			// Setting HTTP flags to None to not use the HTTP cache
			FIoHttpOptions HttpOptions = FIoHttpOptions(Request.Priority, HttpRetryCount, EIoHttpFlags::None);
			HttpOptions.SetCategory(HttpCategory);

			UE_LOG(LogIoStoreOnDemand, VeryVerbose, TEXT("Created request for chunk: ChunkHash=%s, RequestId=%u"),
				*TStringBuilder<64>(InPlace, ChunkHash), PendingChunkRequest->RequestId);

			if (bUseHttpIoDispatcher)
			{
				PendingChunkRequest->HttpHandle.Emplace<FIoHttpRequest>(Batch->Get(
					ChunkInfo.HostGroupName(),
					ChunkInfo.RelativeUrl(),
					FIoHttpHeaders(),
					HttpOptions,
					ChunkHash,
					[this, ChunkHash, RequestId = PendingChunkRequest->RequestId](FIoHttpResponse&& HttpResponse)
					{
						InstallerPipe.Launch(
							TEXT("ProcessIoStoreOnDemandDownloadedChunk"),
							[this, ChunkHash, RequestId, HttpResponse = MoveTemp(HttpResponse)]() mutable
							{
								FIoBuffer Chunk = HttpResponse.GetBody();
								ProcessDownloadedChunk(ChunkHash, RequestId, HttpResponse.GetErrorCode(), HttpResponse.GetStatusCode(), MoveTemp(Chunk));
							},
							UE::Tasks::ETaskPriority::BackgroundLow);
					})
				);
			}
			else
			{
				PendingChunkRequest->HttpHandle.Emplace<void*>(HttpClient->IssueRequest(
					MoveTemp(ChunkInfo),
					FIoOffsetAndLength(),
					Request.Priority,
					[this, ChunkHash, RequestId = PendingChunkRequest->RequestId](uint32 HttpStatusCode, FStringView ErrorReason, FIoBuffer&& Chunk)
					{
						EIoErrorCode CompletionStatus = (HttpStatusCode > 199 && HttpStatusCode < 300) ? EIoErrorCode::Ok : EIoErrorCode::ReadError;
						if (ErrorReason.Contains(TEXTVIEW("cancelled"), ESearchCase::IgnoreCase) ||
							ErrorReason.Contains(TEXTVIEW("canceled"),  ESearchCase::IgnoreCase)) // Yes we spell it both ways (╯°□°）╯︵ ┻━┻
						{
							CompletionStatus = EIoErrorCode::Cancelled;
						}
						else
						{
							UE_CLOG(ErrorReason.IsEmpty() == false, LogIoStoreOnDemand, Warning, TEXT("Failed to download chunk: %.*s"), ErrorReason.Len(), ErrorReason.GetData());
						}

						InstallerPipe.Launch(
							TEXT("ProcessIoStoreOnDemandDownloadedChunk"),
							[this, ChunkHash, RequestId, CompletionStatus, HttpStatusCode, Chunk = MoveTemp(Chunk)]() mutable
							{
								ProcessDownloadedChunk(ChunkHash, RequestId, CompletionStatus, HttpStatusCode, MoveTemp(Chunk));
							},
							UE::Tasks::ETaskPriority::BackgroundLow);
					},
					EHttpRequestType::Installed)
				);
			}
		}

	}

	if (Batch)
	{
		Batch->Issue();
	}
	
	InstallRequest.bHttpRequestsIssued = true;
}

void FOnDemandContentInstaller::ExecutePurgeRequest(FRequest& Request)
{
	check(Request.IsPurge());
	check(RunningRequests.Num() == 1 && RunningRequests.Contains(&Request));

	FRequest::FPurge& PurgeRequest	= Request.AsPurge();
	const bool bDefrag				= EnumHasAnyFlags(PurgeRequest.Args.Options, EOnDemandPurgeOptions::Defrag);
	const uint64* BytesToPurge		= PurgeRequest.Args.BytesToPurge.GetPtrOrNull();

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Executing purge request, BytesToPurge=%llu, Defrag='%s'"),
		BytesToPurge != nullptr ? *BytesToPurge : -1, bDefrag ? TEXT("True") : TEXT("False"));

	if (Request.IsCancelled() == false)
	{
		Request.Result = IoStore.InstallCache->PurgeAllUnreferenced(bDefrag, BytesToPurge);
	}

	CompletePurgeRequest(Request);
}

void FOnDemandContentInstaller::ExecuteDefragRequest(FRequest& Request)
{
	check(Request.IsDefrag());
	check(RunningRequests.Num() == 1 && RunningRequests.Contains(&Request));

	FRequest::FDefrag& DefragRequest	= Request.AsDefrag();
	const uint64* BytesToFree			= DefragRequest.Args.BytesToFree.GetPtrOrNull();

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Executing defrag request, BytesToFree=%llu"),
		BytesToFree != nullptr ? *BytesToFree : -1); 

	if (Request.IsCancelled() == false)
	{
		Request.Result = IoStore.InstallCache->DefragAll(BytesToFree);
	}

	CompleteDefragRequest(Request);
}

void FOnDemandContentInstaller::ExecuteVerifyRequest(FRequest& Request)
{
	check(Request.IsVerify());
	check(RunningRequests.Num() == 1 && RunningRequests.Contains(&Request));

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Executing verify cache request"));

	if (Request.IsCancelled() == false)
	{
		Request.Result = IoStore.InstallCache->Verify();
	}

	CompleteVerifyRequest(Request); 
}

void FOnDemandContentInstaller::ExectuteFlushLastAccessRequest(FRequest& Request)
{
	check(Request.IsFlushLastAccess());
	check(RunningRequests.Num() == 1 && RunningRequests.Contains(&Request));

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Executing flush last access request"));

	if (Request.IsCancelled() == false)
	{
		Request.Result = IoStore.InstallCache->FlushLastAccess();
	}

	CompleteFlushLastAccessRequest(Request);
}

void FOnDemandContentInstaller::ProcessDownloadedChunk(
	const FIoHash& ChunkHash, uint32 RequestId, EIoErrorCode InErrorCode, uint32 HttpStatusCode, FIoBuffer&& Chunk)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandContentInstaller::ProcessDownloadedChunk);

	FResult Result = MakeValue();

	const bool bHttpOk = HttpStatusCode > 199 && HttpStatusCode < 300 && Chunk.GetSize() > 0;
	if (bHttpOk)
	{
		const FIoHash VerifyChunkHash = FIoHash::HashBuffer(Chunk.GetView());
		if (ChunkHash == VerifyChunkHash)
		{
			Result = IoStore.InstallCache->PutChunk(MoveTemp(Chunk), ChunkHash);
			if (CVars::EnableConcurentInstallRequests())
			{
				// TODO: Cache needs to track size of blocks in memory so we can quickly compute cache usage
				// TODO: if cache is full, Defrag here based on size of the pending http chunks
			}
		}
		else
		{
			Result = MakeError(UnifiedError::IoStoreOnDemand::ChunkHashError::MakeError(
				UnifiedError::IoStoreOnDemand::FChunkHashMismatchErrorContext
				{
					.ChunkId = FIoChunkId::InvalidChunkId, // Filled in later by OnProcessDownloadedChunkNotifyRequest
					.ExpectedHash = ChunkHash,
					.ActualHash = VerifyChunkHash
				}));
		}
	}
	else
	{
		Result = MakeError(UnifiedError::IoStoreOnDemand::HttpError::MakeError(HttpStatusCode));
	}

	const bool bCancelled = (InErrorCode == EIoErrorCode::Cancelled);

	bool bFoundRequest = false;
	for (auto It = PendingChunkDownloads.CreateKeyIterator(ChunkHash); It; ++It)
	{
		FChunkDownloadRequest& ChunkRequest = It.Value();
		if (ChunkRequest.RequestId != RequestId)
		{
			continue;
		}

		static constexpr const TCHAR LogFmt[] = TEXT("ProcessDownloadedChunk: ChunkHash=%s, RequestId=%u, DownloadResult=%s");
		if (bCancelled)
		{
			UE_LOG(LogIoStoreOnDemand, Verbose, LogFmt, *TStringBuilder<64>(InPlace, ChunkHash), ChunkRequest.RequestId, TEXT("Cancelled"));
		}
		else if (Result.HasError())
		{
			UE_LOG(LogIoStoreOnDemand, Warning, LogFmt, *TStringBuilder<64>(InPlace, ChunkHash), ChunkRequest.RequestId, *Result.GetError().GetErrorMessage(true).ToString());
		}
		else
		{
			UE_LOG(LogIoStoreOnDemand, VeryVerbose, LogFmt, *TStringBuilder<64>(InPlace, ChunkHash), ChunkRequest.RequestId, TEXT("OK"));
		}

		bFoundRequest = true;
		while (FChunkHttpRequestHandle* ChunkHttpRequestHandle = ChunkRequest.ChunkRequestHttpHandles.PopHead())
		{
			OnProcessDownloadedChunkNotifyRequest(*ChunkHttpRequestHandle, Result, bCancelled);
		}

		It.RemoveCurrent();
		break;
	}

	check(bFoundRequest);
}

void FOnDemandContentInstaller::OnProcessDownloadedChunkNotifyRequest(
	const FChunkHttpRequestHandle& HttpRequest, const FResult& ChunkDownloadResult, bool bChunkCancelled)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandContentInstaller::OnProcessDownloadedChunkNotifyRequest);

	FRequest& Request = *HttpRequest.OwnerRequest;
	FRequest::FInstall& InstallRequest = Request.AsInstall();
	FSharedOnDemandContainer& Container = InstallRequest.ResolvedChunks[HttpRequest.ContainerIndex].Container;
	const FOnDemandChunkEntry& ChunkEntry = Container->ChunkEntries[HttpRequest.EntryIndex];
	const FIoChunkId& ChunkId = Container->ChunkIds[HttpRequest.EntryIndex];

	bool bSetErrorOnRequest = false;
	if (Request.IsOk())
	{
		if (!ensureMsgf(!bChunkCancelled, TEXT("Recieved cancelled http callback for active Request %s ! This should never happen."), *InstallRequest.Args.DebugName))
		{
			bSetErrorOnRequest = true;
			Request.Result = ChunkDownloadResult;
		}
		else if (const UnifiedError::FError* Error = ChunkDownloadResult.TryGetError())
		{
			bSetErrorOnRequest = true;

			// Set the chunk Id from this container if needed
			const UnifiedError::IoStoreOnDemand::FChunkHashMismatchErrorContext* ErrorContext =
				Error->GetErrorContext<UnifiedError::IoStoreOnDemand::FChunkHashMismatchErrorContext>();
			if (ErrorContext)
			{
				// FError does not deep copy by design.
				// We need a different error per-request so we need to re-instance it.
				Request.Result = MakeError(UnifiedError::IoStoreOnDemand::ChunkHashError::MakeError(
					UnifiedError::IoStoreOnDemand::FChunkHashMismatchErrorContext
					{
						.ChunkId = ChunkId,
						.ExpectedHash = ErrorContext->ExpectedHash,
						.ActualHash = ErrorContext->ActualHash
					}));
			}
			else
			{
				Request.Result = ChunkDownloadResult;
			}
		}
	}

	static constexpr const TCHAR LogFmt[] = TEXT("Install progress %.2lf/%.2lf MiB, SeqNo=%u, Priority=%d, ContentHandle=(%s), ChunkId='%s', ChunkSize=%.2lf KiB, DownloadResult=%s");
	if (bChunkCancelled)
	{
		UE_LOG(LogIoStoreOnDemand, Verbose, LogFmt,
			double(InstallRequest.Progress.CurrentInstallSize) / 1024.0 / 1024.0, double(InstallRequest.Progress.TotalInstallSize) / 1024.0 / 1024.0,
			Request.SeqNo, Request.Priority, *LexToString(InstallRequest.Args.ContentHandle), *LexToString(ChunkId), double(ChunkEntry.GetDiskSize()) / 1024.0,
			TEXT("Cancelled"));
	}
	else if (ChunkDownloadResult.HasError())
	{
		UE_LOG(LogIoStoreOnDemand, Warning, LogFmt,
			double(InstallRequest.Progress.CurrentInstallSize) / 1024.0 / 1024.0, double(InstallRequest.Progress.TotalInstallSize) / 1024.0 / 1024.0,
			Request.SeqNo, Request.Priority, *LexToString(InstallRequest.Args.ContentHandle), *LexToString(ChunkId), double(ChunkEntry.GetDiskSize()) / 1024.0,
			*ChunkDownloadResult.GetError().GetErrorMessage(true).ToString());
	}
	else
	{
		InstallRequest.Progress.CurrentInstallSize += ChunkEntry.GetDiskSize();

		UE_LOG(LogIoStoreOnDemand, Verbose, LogFmt,
			double(InstallRequest.Progress.CurrentInstallSize) / 1024.0 / 1024.0, double(InstallRequest.Progress.TotalInstallSize) / 1024.0 / 1024.0,
			Request.SeqNo, Request.Priority, *LexToString(InstallRequest.Args.ContentHandle), *LexToString(ChunkId), double(ChunkEntry.GetDiskSize()) / 1024.0,
			TEXT("OK"));
	}

	const bool bCompleted = ++InstallRequest.DownloadedChunkCount >= InstallRequest.HttpRequestHandles.Num();
	if (bCompleted)
	{
		CompleteInstallRequest(Request);
	}
	else
	{
		NotifyInstallProgress(Request);

		// Only try to cancel requests the first time an error is set
		if (bSetErrorOnRequest) 
		{
			int32 NumCancelled = 0;
			TryCancelHttpRequestsForInstallRequest(InstallRequest, NumCancelled);
			UE_CLOG(NumCancelled == 0, LogIoStoreOnDemand, Warning, TEXT("Cancelled %d HTTP request(s) due to install error"), NumCancelled);
		}
	}
}

void FOnDemandContentInstaller::TryCancelHttpRequestsForInstallRequest(FRequest::FInstall& InstallRequest, int32& OutNumCancelled)
{
	int32 NumCancelled = 0;

	if (CVars::bContentInstallerEnableHttpCancel == false)
	{
		OutNumCancelled = NumCancelled;
		return;
	}

	for (FChunkHttpRequestHandle& PendingHttpRequest : InstallRequest.HttpRequestHandles)
	{
		const FSharedOnDemandContainer& PendingContainer = InstallRequest.ResolvedChunks[PendingHttpRequest.ContainerIndex].Container;
		const FOnDemandChunkEntry& PendingChunkEntry = PendingContainer->ChunkEntries[PendingHttpRequest.EntryIndex];
		const FIoHash& PendingChunkHash = PendingChunkEntry.Hash;

		FChunkDownloadRequest* ChunkDownload = nullptr;
		for (auto It = PendingChunkDownloads.CreateKeyIterator(PendingChunkHash); It; ++It)
		{
			FChunkDownloadRequest& FoundChunkDownload = It.Value();
			if (FoundChunkDownload.bChunkCanceled == false)
			{
				ChunkDownload = &FoundChunkDownload;
				break; // Should be a max of one active request for any chunk
			}
		}

		if (ChunkDownload)
		{
			bool bCancelHttpRequest = true;
			for (FChunkHttpRequestHandle& Handle : ChunkDownload->ChunkRequestHttpHandles)
			{
				if (Handle.OwnerRequest->IsOk())
				{
					bCancelHttpRequest = false;
					break;
				}
			}

			if (bCancelHttpRequest)
			{
				// No requests are waiting on this chunk, cancel it
				Visit(UE::Overload(
					[&NumCancelled](FIoHttpRequest& Handle)
					{
						if (Handle.IsValid())
						{
							Handle.Cancel();
							++NumCancelled;
						}
					},
					[this, &NumCancelled](void* Handle)
					{
						check(HttpClient != nullptr);
						if (Handle != nullptr)
						{
							HttpClient->CancelRequest(Handle);
							++NumCancelled;
						}
					}),
					ChunkDownload->HttpHandle);

				ChunkDownload->bChunkCanceled = true;
				UE_LOG(LogIoStoreOnDemand, Verbose, TEXT("Cancelled chunk download: hash - %s, Id - %u"), *TStringBuilder<64>(InPlace, PendingChunkHash), ChunkDownload->RequestId);

				// ProcessDownloadedChunk will remove the chunk request
			}
		}
	}

	OutNumCancelled = NumCancelled;
}

void FOnDemandContentInstaller::NotifyInstallProgress(FRequest& Request)
{
	ensure(Request.IsInstall());

	FRequest::FInstall& InstallRequest = Request.AsInstall();

	if (!InstallRequest.OnProgress)
	{
		return;
	}

	const uint64 Cycles = FPlatformTime::Cycles64();
	const double SecondsSinceLastProgress = FPlatformTime::ToSeconds64(Cycles - InstallRequest.LastProgressCycles);
	if (InstallRequest.bNotifyingProgressOnGameThread.load(std::memory_order_seq_cst) || SecondsSinceLastProgress < .25)
	{
		return;
	}
	InstallRequest.LastProgressCycles = Cycles;

	//TODO: Remove support for notifying progress on the game thread
	FOnDemandInstallProgress Progress = InstallRequest.Progress;
	if (EnumHasAnyFlags(InstallRequest.Args.Options, EOnDemandInstallOptions::CallbackOnGameThread))
	{
		InstallRequest.bNotifyingProgressOnGameThread = true;
		ExecuteOnGameThread(
			UE_SOURCE_LOCATION,
			[&InstallRequest, Progress]()
			{
				InstallRequest.OnProgress(InstallRequest.Progress);
				InstallRequest.bNotifyingProgressOnGameThread = false;
			});
	}
	else
	{
		InstallRequest.OnProgress(Progress);
	}
}

void FOnDemandContentInstaller::DestroyRequest(FRequest& Request)
{
	// Call destructor outside the lock to avoid double locking
	Request.~FRequest();

	TUniqueLock Lock(Mutex);
	RequestAllocator.Free(&Request);
}

void FOnDemandContentInstaller::CompleteInstallRequest(FRequest& Request)
{
	using namespace UE::IoStore::Private;

	FRequest::FInstall& InstallRequest = Request.AsInstall();

#if !UE_BUILD_SHIPPING
	TArray<FString> DebugInfo;
#endif

	const uint64 ResolvedChunkCount = Algo::TransformAccumulate(
		InstallRequest.ResolvedChunks, 
		[](const Private::FResolvedContainerChunks& ContainerChunks)
		{
			return ContainerChunks.EntryIndices.Num();
		},
		uint64(0)
	);

	// Mark all resolved chunk(s) as referenced by the content handle and notify the package store to update
	if (Request.IsOk() && ResolvedChunkCount > 0)
	{
		{
			FOnDemandContentHandle& ContentHandle = InstallRequest.Args.ContentHandle;

			TUniqueLock Lock(IoStore.ContainerMutex);
			for (FResolvedContainerChunks& ResolvedChunks : InstallRequest.ResolvedChunks)
			{
				const FSharedOnDemandContainer& Container = ResolvedChunks.Container;
				FOnDemandChunkEntryReferences& References = Container->FindOrAddChunkEntryReferences(*ContentHandle.Handle);
				for (int32 EntryIndex : ResolvedChunks.EntryIndices)
				{
					References.Indices[EntryIndex] = true;
#if !UE_BUILD_SHIPPING
					if (UE_LOG_ACTIVE(LogIoStoreOnDemand, Verbose))
					{
						DebugInfo.Add(FString::Printf(TEXT("ID:%s, Size:%d"), *LexToString(Container->ChunkEntries[EntryIndex].Hash), Container->ChunkEntries[EntryIndex].GetDiskSize() ));
					}
#endif
				}
			}
		}

		IoStore.PackageStoreBackend->NeedsUpdate(EOnDemandPackageStoreUpdateMode::ReferencedPackages);
	}

	const bool bCancelled			= Request.IsCancelled();
	const uint64 DurationCycles		= FPlatformTime::Cycles64() - Request.StartTimeCycles;
	const double DurationInSeconds	= FPlatformTime::ToSeconds64(DurationCycles);
	const double CacheHitRatio		= InstallRequest.Progress.TotalContentSize > 0
		? double(InstallRequest.Progress.TotalContentSize - InstallRequest.Progress.TotalInstallSize) / double(InstallRequest.Progress.TotalContentSize)
		: 0.0;

#if !UE_BUILD_SHIPPING
	FString DebugInfoString = FString::Join(DebugInfo, TEXT(","));
	UE_LOG(LogIoStoreOnDemand, Verbose, TEXT("ContentHandle=(%s), ResolvedChunks(%s)"), *LexToString(InstallRequest.Args.ContentHandle), *DebugInfoString);
#endif
	UE_LOG(LogIoStoreOnDemand, Verbose, TEXT("Install request completed, Result='%s', SeqNo=%u, Priority=%d, ContentHandle=(%s), ContentSize=%.2lf MiB, InstallSize=%.2lf MiB, CacheHitRatio=%d%%, Duration=%dms"),
		*Request.GetErrorString(), Request.SeqNo, Request.Priority, *LexToString(InstallRequest.Args.ContentHandle), double(InstallRequest.Progress.TotalContentSize) / 1024.0 / 1024.0,
		double(InstallRequest.Progress.TotalInstallSize) / 1024.0 / 1024.0, int32(CacheHitRatio * 100), Private::SecondsToMillicseconds(DurationInSeconds));

	FOnDemandContentInstallerStats::OnRequestCompleted(
		Request.Result,
		ResolvedChunkCount,
		InstallRequest.Progress.TotalContentSize,
		static_cast<uint64>(InstallRequest.HttpRequestHandles.Num()),
		InstallRequest.Progress.CurrentInstallSize,
		CacheHitRatio,
		DurationCycles);

	FOnDemandInstallResult InstallResult;
	InstallResult.Progress			= InstallRequest.Progress;
	InstallResult.DurationInSeconds	= DurationInSeconds;
	InstallResult.Error				= Request.ConsumeError();

	{
		TUniqueLock Lock(Mutex);

		InstallRequest.Request->InstallerRequest = 0;
		RunningRequests.RemoveSingleSwap(&Request);
	}

	TryExecuteNextRequest();

	const FOnDemandInstallRequest::EStatus FinalRequestStatus = bCancelled
		? FOnDemandInstallRequest::EStatus::Cancelled
		: InstallResult.Error.IsSet()
			? FOnDemandInstallRequest::EStatus::Error
			: FOnDemandInstallRequest::Ok;

	if (!InstallRequest.OnCompleted)
	{
		InstallRequest.Request->Status.store(FinalRequestStatus);
		DestroyRequest(Request);
		return;
	}

	// Do this before the callback in case the callback triggers any further API calls
	InstallRequest.Request->Status.store(FOnDemandInstallRequest::PendingCallbacks);

	const bool bCallbackOnGameThread = EnumHasAnyFlags(InstallRequest.Args.Options, EOnDemandInstallOptions::CallbackOnGameThread);
	if (bCallbackOnGameThread)
	{
		ExecuteOnGameThread(
			UE_SOURCE_LOCATION,
			[this, &Request, InstallResult = MoveTemp(InstallResult), FinalRequestStatus]() mutable
			{
				FRequest::FInstall& InstallRequest		= Request.AsInstall();
				FOnDemandInstallCompleted OnCompleted	= MoveTemp(InstallRequest.OnCompleted);

				ensure(InstallRequest.bNotifyingProgressOnGameThread == false);
				OnCompleted(MoveTemp(InstallResult));
				InstallRequest.Request->Status.store(FinalRequestStatus);
				DestroyRequest(Request);
			});
	}
	else
	{
		FOnDemandInstallCompleted OnCompleted = MoveTemp(InstallRequest.OnCompleted);
		OnCompleted(MoveTemp(InstallResult));
		InstallRequest.Request->Status.store(FinalRequestStatus);
		DestroyRequest(Request);
	}
}

void FOnDemandContentInstaller::CompletePurgeRequest(FRequest& Request)
{
	check(RunningRequests.Num() == 1 && RunningRequests.Contains(&Request));

	const double DurationInSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - Request.StartTimeCycles);
	
	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Purge request completed, Result='%s', Duration=%d ms"),
		*Request.GetErrorString(), Private::SecondsToMillicseconds(DurationInSeconds));
	
	FOnDemandPurgeResult PurgeResult;
	PurgeResult.DurationInSeconds	= DurationInSeconds;
	PurgeResult.Error				= Request.ConsumeError();
	
	const bool bCallbackOnGameThread	= EnumHasAnyFlags(Request.AsPurge().Args.Options, EOnDemandPurgeOptions::CallbackOnGameThread);
	FOnDemandPurgeCompleted OnCompleted = MoveTemp(Request.AsPurge().OnCompleted);
	{
		TUniqueLock Lock(Mutex);
		RunningRequests.RemoveSingleSwap(&Request);
	}
	DestroyRequest(Request);

	TryExecuteNextRequest();

	if (!OnCompleted)
	{
		return;
	}

	if (bCallbackOnGameThread)
	{
		ExecuteOnGameThread(
			UE_SOURCE_LOCATION,
			[OnCompleted = MoveTemp(OnCompleted), PurgeResult = MoveTemp(PurgeResult)]() mutable
			{
				OnCompleted(MoveTemp(PurgeResult));
			});
	}
	else
	{
		OnCompleted(MoveTemp(PurgeResult));
	}
}

void FOnDemandContentInstaller::CompleteDefragRequest(FRequest& Request)
{
	check(RunningRequests.Num() == 1 && RunningRequests.Contains(&Request));

	const double DurationInSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - Request.StartTimeCycles);

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Defrag request completed, Result='%s', Duration=%d ms"),
		*Request.GetErrorString(), Private::SecondsToMillicseconds(DurationInSeconds));

	FOnDemandDefragResult DefragResult;
	DefragResult.DurationInSeconds	= DurationInSeconds;
	DefragResult.Error				= Request.ConsumeError();

	const bool bCallbackOnGameThread		= EnumHasAnyFlags(Request.AsDefrag().Args.Options, EOnDemandDefragOptions::CallbackOnGameThread);
	FOnDemandDefragCompleted OnCompleted	= MoveTemp(Request.AsDefrag().OnCompleted);
	{
		TUniqueLock Lock(Mutex);
		RunningRequests.RemoveSingleSwap(&Request);
	}
	DestroyRequest(Request);

	TryExecuteNextRequest();

	if (!OnCompleted)
	{
		return;
	}

	if (bCallbackOnGameThread)
	{
		ExecuteOnGameThread(
			UE_SOURCE_LOCATION,
			[OnCompleted = MoveTemp(OnCompleted), DefragResult = MoveTemp(DefragResult)]() mutable
			{
				OnCompleted(MoveTemp(DefragResult));
			});
	}
	else
	{
		OnCompleted(MoveTemp(DefragResult));
	}
}

void FOnDemandContentInstaller::CompleteVerifyRequest(FRequest& Request)
{
	const double DurationInSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - Request.StartTimeCycles);

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Verify request completed, Result='%s', Duration=%d ms"),
		*Request.GetErrorString(), Private::SecondsToMillicseconds(DurationInSeconds));

	FOnDemandVerifyCacheResult VerifyResult;
	VerifyResult.DurationInSeconds	= DurationInSeconds;
	VerifyResult.Error				= Request.ConsumeError();

	FOnDemandVerifyCacheCompleted OnCompleted = MoveTemp(Request.AsVerify().OnCompleted);
	{
		TUniqueLock Lock(Mutex);
		RunningRequests.RemoveSingleSwap(&Request);
	}
	DestroyRequest(Request);

	TryExecuteNextRequest();

	if (!OnCompleted)
	{
		return;
	}

	OnCompleted(MoveTemp(VerifyResult));
}

void FOnDemandContentInstaller::CompleteFlushLastAccessRequest(FRequest& Request)
{
	const double DurationInSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - Request.StartTimeCycles);

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Flush last access request completed, Result='%s', Duration=%d ms"),
		*Request.GetErrorString(), Private::SecondsToMillicseconds(DurationInSeconds));

	FOnDemandFlushLastAccessResult FlushResult;
	FlushResult.DurationInSeconds	= DurationInSeconds;
	FlushResult.Error				= Request.ConsumeError();

	FOnDemandFlushLastAccessCompleted OnCompleted = MoveTemp(Request.AsFlushLastAccess().OnCompleted);
	{
		TUniqueLock Lock(Mutex);
		RunningRequests.RemoveSingleSwap(&Request);
	}
	DestroyRequest(Request);

	TryExecuteNextRequest();

	if (!OnCompleted)
	{
		return;
	}

	OnCompleted(MoveTemp(FlushResult));
}

void FOnDemandContentInstaller::Shutdown()
{
	bShuttingDown					= true;
	const double WaitTimeoutSeconds	= 5.0;
	const uint64 StartTimeCycles	= FPlatformTime::Cycles64();

	// Cancel current requests
	{
		TUniqueLock Lock(Mutex);
		for (FRequest* Request : RunningRequests)
		{
			Request->TryCancel();
		}
	}

	// Wait for the current request(s) to finish
	for (;;)
	{
		double WaitTimeSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTimeCycles);
		if (WaitTimeSeconds > WaitTimeoutSeconds)
		{
			UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Content installer shutdown cancelled after %.2lf"), WaitTimeSeconds);
			break;
		}

		InstallerPipe.WaitUntilEmpty(FTimespan::FromSeconds(1.0));
		{
			TUniqueLock Lock(Mutex);
			if (RunningRequests.IsEmpty())
			{
				break;
			}
		}
	}

	{
		TUniqueLock Lock(Mutex);
		UE_CLOG(RunningRequests.IsEmpty() == false, LogIoStoreOnDemand, Error, TEXT("Content installer has still inflight request(s) while shutting down"));
		RunningRequests.Reset();
	}

	// Cancel all remaining request(s)
	for (;;)
	{
		FRequest* NextRequest = nullptr;
		{
			TUniqueLock Lock(Mutex);
			if (RequestQueue.IsEmpty() == false)
			{
				RequestQueue.HeapPop(NextRequest, RequestSortPredicate, EAllowShrinking::No);
				RunningRequests.Add(NextRequest);
			}
		}

		if (NextRequest == nullptr)
		{
			break;
		}

		NextRequest->TryCancel();
		ExecuteRequest(*NextRequest);
		check(RunningRequests.IsEmpty());
	}
}

} // namespace UE::IoStore
