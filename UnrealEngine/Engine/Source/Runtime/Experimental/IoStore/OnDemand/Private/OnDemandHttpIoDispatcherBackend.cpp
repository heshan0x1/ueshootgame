// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnDemandHttpIoDispatcherBackend.h"

#include "Containers/AnsiString.h"
#include "HAL/PlatformTime.h"
#include "IO/HttpIoDispatcher.h"
#include "IO/IoAllocators.h"
#include "IO/IoOffsetLength.h"
#include "IO/IoStatus.h"
#include "IO/IoStoreOnDemand.h"
#include "OnDemandBackendStatus.h"
#include "OnDemandIoStore.h"
#include "OnDemandHttpThread.h" // EHttpRequestType
#include "Statistics.h"

namespace UE::IoStore
{
////////////////////////////////////////////////////////////////////////////////

extern int32 GIasHttpRangeRequestMinSizeKiB;
extern bool GIasEnableWriteOnlyDecoding;

////////////////////////////////////////////////////////////////////////////////
class FHttpIoDispatcherBackend
	: public IOnDemandHttpIoDispatcherBackend
{
	using FSharedBackendCtx = TSharedPtr<const FIoDispatcherBackendContext>;

	struct FResolvedRequest
	{
		FResolvedRequest(FIoRequestImpl& InRequest, FOnDemandChunkInfo&& InChunkInfo, const FIoOffsetAndLength& InChunkRange)
			: DispatcherRequest(InRequest)
			, ChunkInfo(MoveTemp(InChunkInfo))
			, ChunkRange(InChunkRange)
			, StartTimeCycles(FPlatformTime::Cycles64())
		{
			check(DispatcherRequest.BackendData == nullptr);
			DispatcherRequest.BackendData = this;
		}

		static FResolvedRequest* Get(FIoRequestImpl& DispatcherRequest)
		{
			return reinterpret_cast<FResolvedRequest*>(DispatcherRequest.BackendData);
		}

		static FResolvedRequest* Create(FIoRequestImpl& DispatcherRequest, FOnDemandChunkInfo&& ChunkInfo, const FIoOffsetAndLength& ChunkRange)
		{
			return Allocator.Construct(DispatcherRequest, MoveTemp(ChunkInfo), ChunkRange);
		}

		static void Destroy(FResolvedRequest* ResolvedRequest)
		{
			check(ResolvedRequest->DispatcherRequest.BackendData == ResolvedRequest);
			ResolvedRequest->DispatcherRequest.BackendData = nullptr;
			Allocator.Destroy(ResolvedRequest);
		}

		using FAllocator		= TSingleThreadedSlabAllocator<FResolvedRequest, 64>;
		static FAllocator		Allocator;

		FIoRequestImpl&			DispatcherRequest;
		FOnDemandChunkInfo		ChunkInfo;
		FIoOffsetAndLength		ChunkRange;
		FIoHttpRequest			HttpRequest;
		uint64					StartTimeCycles;
	};

public:
											FHttpIoDispatcherBackend(FOnDemandIoStore& IoStore);
	//IOnDemandHttpIoDispatcherBackend
	virtual void							SetOptionalBulkDataEnabled(bool bEnabled) override;
	virtual void 							ReportAnalytics(TArray<FAnalyticsEventAttribute>& OutAnalyticsArray) const override;
	// I/O dispatcher backend
	virtual void							Initialize(TSharedRef<const FIoDispatcherBackendContext> Context) override;
	virtual void							Shutdown() override;
	virtual void							ResolveIoRequests(FIoRequestList Requests, FIoRequestList& OutUnresolved) override;
	virtual FIoRequestImpl*					GetCompletedIoRequests() override;
	virtual void							CancelIoRequest(FIoRequestImpl* Request) override;
	virtual void							UpdatePriorityForIoRequest(FIoRequestImpl* Request) override;
	virtual bool							DoesChunkExist(const FIoChunkId& ChunkId) const override;
	virtual bool							DoesChunkExist(const FIoChunkId& ChunkId, const FIoOffsetAndLength& ChunkRange) const override; 
	virtual TIoStatusOr<uint64> 			GetSizeForChunk(const FIoChunkId& ChunkId) const override;
	virtual TIoStatusOr<FIoMappedRegion>	OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options) override { return FIoStatus(EIoErrorCode::NotFound); }
	virtual const TCHAR*					GetName() const { return TEXT("HttpIoDispatcherBackend"); }

private:
	void									CompleteRequest(FResolvedRequest& ResolvedRequst, FIoHttpResponse&& HttpResponse);
	static void								LogRequest(FResolvedRequest& ResolvedRequest, FIoRequestImpl& DispacherRequest, bool bDecoded, bool bCached, bool bCancelled, uint64 DurationMs);

	FBackendStatus			BackendStatus;
	FOnDemandIoBackendStats	Stats;
	FOnDemandIoStore&		IoStore;
	FIoRequestList			CompletedRequests;
	FMutex					Mutex;
	FSharedBackendCtx		BackendContext;
};

FHttpIoDispatcherBackend::FResolvedRequest::FAllocator FHttpIoDispatcherBackend::FResolvedRequest::Allocator;

////////////////////////////////////////////////////////////////////////////////
FHttpIoDispatcherBackend::FHttpIoDispatcherBackend(FOnDemandIoStore& InIoStore)
	: Stats(BackendStatus)
	, IoStore(InIoStore)
{
	BackendStatus.SetHttpEnabled(true);
	BackendStatus.SetCacheEnabled(true);
}

void FHttpIoDispatcherBackend::SetOptionalBulkDataEnabled(bool bEnabled)
{
	BackendStatus.SetHttpOptionalBulkEnabled(bEnabled);
}

void FHttpIoDispatcherBackend::ReportAnalytics(TArray<FAnalyticsEventAttribute>& OutAnalyticsArray) const
{
	Stats.ReportGeneralAnalytics(OutAnalyticsArray);
	Stats.ReportEndPointAnalytics(OutAnalyticsArray);
}

void FHttpIoDispatcherBackend::Initialize(TSharedRef<const FIoDispatcherBackendContext> Context)
{
	BackendContext = Context;
}

void FHttpIoDispatcherBackend::Shutdown()
{
	BackendContext.Reset();
}

void FHttpIoDispatcherBackend::ResolveIoRequests(FIoRequestList Requests, FIoRequestList& OutUnresolved)
{
	const int32 HttpRetryCount = 2;

	FIoHttpBatch Batch = FHttpIoDispatcher::NewBatch();
	while (FIoRequestImpl* Request = Requests.PopHead())
	{
		FOnDemandChunkInfo ChunkInfo = IoStore.GetStreamingChunkInfo(Request->ChunkId);
		if (!ChunkInfo.IsValid())
		{
			OutUnresolved.AddTail(Request);
			continue;
		}

		if (FHttpIoDispatcher::IsHostGroupOk(ChunkInfo.HostGroupName()) == false)
		{
			OutUnresolved.AddTail(Request);
			continue;
		}

		if (BackendStatus.IsHttpEnabled(Request->ChunkId.GetChunkType()) == false)
		{
			OutUnresolved.AddTail(Request);
			continue;
		}

		const uint64 ResolvedSize = FMath::Min<uint64>(Request->Options.GetSize(), ChunkInfo.RawSize() - Request->Options.GetOffset());
		Request->Options.SetRange(Request->Options.GetOffset(), ResolvedSize);
		
		FIoOffsetAndLength ChunkRange;
		const uint64 ChunkSize = ChunkInfo.ChunkEntry().GetDiskSize(); // AES aligned encoded size
		if (ChunkSize <= (uint64(GIasHttpRangeRequestMinSizeKiB) << 10))
		{
			ChunkRange = FIoOffsetAndLength(0, ChunkSize);
		}
		else
		{
			ChunkRange = FIoChunkEncoding::GetChunkRange(
				ChunkInfo.RawSize(),
				ChunkInfo.BlockSize(),
				ChunkInfo.Blocks(),
				Request->Options.GetOffset(),
				ResolvedSize).ConsumeValueOrDie();
		}

		const FIoHttpRange HttpRange = FIoHttpRange::FromOffsetAndLength(ChunkRange);
		FResolvedRequest* ResolvedRequest = FResolvedRequest::Create(*Request, MoveTemp(ChunkInfo), ChunkRange);
		ResolvedRequest->HttpRequest = Batch.Get(
			ResolvedRequest->ChunkInfo.HostGroupName(),
			ResolvedRequest->ChunkInfo.RelativeUrl(),
			FIoHttpHeaders(),
			FIoHttpOptions(Request->Priority, HttpRetryCount, EIoHttpFlags::Default, HttpRange),
			ResolvedRequest->ChunkInfo.ChunkEntry().Hash,
			[this, ResolvedRequest](FIoHttpResponse&& HttpResponse)
			{
				// Callbacks are always triggered from the task pool
				CompleteRequest(*ResolvedRequest, MoveTemp(HttpResponse));
			});
	}

	Batch.Issue();
}

FIoRequestImpl* FHttpIoDispatcherBackend::GetCompletedIoRequests()
{
	FIoRequestList Out;
	{
		TUniqueLock Lock(Mutex);
		Out = MoveTemp(CompletedRequests);
		CompletedRequests = FIoRequestList();
	}

	for (FIoRequestImpl& R : Out)
	{
		FResolvedRequest::Destroy(reinterpret_cast<FResolvedRequest*>(R.BackendData));
		check(R.BackendData == nullptr);
	}

	return Out.GetHead();
}

void FHttpIoDispatcherBackend::CancelIoRequest(FIoRequestImpl* Request)
{
	if (FResolvedRequest* Resolved = FResolvedRequest::Get(*Request))
	{
		Resolved->HttpRequest.Cancel();
	}
}

void FHttpIoDispatcherBackend::UpdatePriorityForIoRequest(FIoRequestImpl* Request)
{
	if (FResolvedRequest* Resolved = FResolvedRequest::Get(*Request))
	{
		Resolved->HttpRequest.UpdatePriorty(Request->Priority);
	}
}

bool FHttpIoDispatcherBackend::DoesChunkExist(const FIoChunkId& ChunkId) const
{
	const TIoStatusOr<uint64> ChunkSize = GetSizeForChunk(ChunkId);
	return ChunkSize.IsOk();
}

bool FHttpIoDispatcherBackend::DoesChunkExist(const FIoChunkId& ChunkId, const FIoOffsetAndLength& ChunkRange) const
{
	return DoesChunkExist(ChunkId);
}

TIoStatusOr<uint64> FHttpIoDispatcherBackend::GetSizeForChunk(const FIoChunkId& ChunkId) const
{
	const FOnDemandChunkInfo ChunkInfo = IoStore.GetStreamingChunkInfo(ChunkId);
	if (!ChunkInfo.IsValid() || !BackendStatus.IsHttpEnabled(ChunkId.GetChunkType()))
	{
		return FIoStatus(EIoErrorCode::UnknownChunkID);
	}

	return ChunkInfo.RawSize();
}

void FHttpIoDispatcherBackend::CompleteRequest(FResolvedRequest& ResolvedRequest, FIoHttpResponse&& HttpResponse)
{
	FIoBuffer Chunk						= HttpResponse.GetBody();
	FOnDemandChunkInfo& ChunkInfo		= ResolvedRequest.ChunkInfo;
	FIoRequestImpl& DispatcherRequest	= ResolvedRequest.DispatcherRequest;

	const bool bCancelled				= DispatcherRequest.IsCancelled();
	bool bDecoded						= false;

	if (!bCancelled && HttpResponse.IsOk() && Chunk.GetSize() > 0)
	{
		FIoChunkDecodingParams Params;
		Params.EncryptionKey				= ChunkInfo.EncryptionKey();
		Params.CompressionFormat			= ChunkInfo.CompressionFormat();
		Params.BlockSize					= ChunkInfo.BlockSize();
		Params.TotalRawSize					= ChunkInfo.RawSize();
		Params.EncodedBlockSize				= ChunkInfo.Blocks();
		Params.BlockHash					= ChunkInfo.BlockHashes();
		Params.EncodedOffset				= ResolvedRequest.ChunkRange.GetOffset();
		Params.RawOffset					= DispatcherRequest.Options.GetOffset();

		DispatcherRequest.CreateBuffer(DispatcherRequest.Options.GetSize());

		const EIoDecodeFlags Options = GIasEnableWriteOnlyDecoding && EnumHasAnyFlags(DispatcherRequest.Options.GetFlags(), EIoReadOptionsFlags::HardwareTargetBuffer)
			? EIoDecodeFlags::WriteOnly : EIoDecodeFlags::None;

		bDecoded = FIoChunkEncoding::Decode(Params, Chunk.GetView(), DispatcherRequest.GetBuffer().GetMutableView(), Options);

		if (!bDecoded)
		{
			if (HttpResponse.IsCached())
			{
				Stats.OnCacheDecodeError();

				if (FIoStatus Status = FHttpIoDispatcher::EvictFromCache(HttpResponse); !Status.IsOk())
				{
					UE_LOG(LogHttpIoDispatcher, Error, TEXT("Evict HTTP cache failed, reason '%s'"), *Status.ToString());
				}
			}
			else
			{
				Stats.OnHttpDecodeError(EHttpRequestType::Streaming);
			}
		}
	}

	const uint64 DurationMs = ResolvedRequest.StartTimeCycles > 0
		? uint64(FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - ResolvedRequest.StartTimeCycles))
		: 0;

	if (bDecoded)
	{
		if (!HttpResponse.IsCached())
		{
			FIoStatus Status = FHttpIoDispatcher::CacheResponse(HttpResponse);
			UE_CLOG(!Status.IsOk(), LogHttpIoDispatcher, Error, TEXT("Put HTTP cache failed, reason '%s'"), *Status.ToString());
		}

		Stats.OnIoRequestComplete(DispatcherRequest.GetBuffer().GetSize(), DurationMs);
	}
	else if (bCancelled)
	{
		Stats.OnIoRequestCancel();
	}
	else
	{
		DispatcherRequest.SetFailed();
		Stats.OnIoRequestError();
	}
	
	LogRequest(ResolvedRequest, DispatcherRequest, bDecoded, HttpResponse.IsCached(), bCancelled, DurationMs);

	{
		TUniqueLock Lock(Mutex);
		CompletedRequests.AddTail(&DispatcherRequest);
	}

	BackendContext->WakeUpDispatcherThreadDelegate.Execute();
}

void FHttpIoDispatcherBackend::LogRequest(FResolvedRequest& ResolvedRequest, FIoRequestImpl& DispacherRequest, bool bDecoded, bool bCached, bool bCancelled, uint64 DurationMs)
{
	if (bCancelled)
	{
		return;
	}

	const TCHAR* Prefix = [bDecoded, bCached]() -> const TCHAR*
	{
		if (bDecoded)
		{
			return bCached ? TEXT("io-cache") : TEXT("io-http ");
		}
		return bCached ? TEXT("io-cache-error") : TEXT("io-http-error ");
	}();

	auto PrioToString = [](int32 Prio) -> const TCHAR*
	{
		if (Prio < IoDispatcherPriority_Low)
		{
			return TEXT("Min");
		}
		if (Prio < IoDispatcherPriority_Medium)
		{
			return TEXT("Low");
		}
		if (Prio < IoDispatcherPriority_High)
		{
			return TEXT("Medium");
		}
		if (Prio < IoDispatcherPriority_Max)
		{
			return TEXT("High");
		}

		return TEXT("Max");
	};

	FOnDemandChunkInfo& ChunkInfo			= ResolvedRequest.ChunkInfo;
	const uint64 UncompressedSize			= bDecoded ? DispacherRequest.GetBuffer().GetSize() : 0;
	const FIoOffsetAndLength& ChunkRange	= ResolvedRequest.ChunkRange;
	const uint64 ChunkSize					= ChunkInfo.ChunkEntry().GetDiskSize();

	UE_LOG(LogIas, VeryVerbose, TEXT("%s: %5" UINT64_FMT "ms %5" UINT64_FMT "KiB[%8" UINT64_FMT "] % s: % s | Range: %" UINT64_FMT "-%" UINT64_FMT "/%" UINT64_FMT " (%.2f%%) | Prio: %s"),
		Prefix,
		DurationMs,
		UncompressedSize >> 10,
		DispacherRequest.Options.GetOffset(),
		*LexToString(DispacherRequest.ChunkId),
		*LexToString(ChunkInfo.Hash()),
		ChunkRange.GetOffset(), (ChunkRange.GetOffset() + ChunkRange.GetLength() - 1), ChunkSize,
		100.0f * (float(ChunkRange.GetLength()) / float(ChunkSize)),
		PrioToString(DispacherRequest.Priority));
}

////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IOnDemandHttpIoDispatcherBackend> MakeOnDemandHttpIoDispatcherBackend(FOnDemandIoStore& IoStore)
{
	return TSharedPtr<IOnDemandHttpIoDispatcherBackend>(new FHttpIoDispatcherBackend(IoStore));
}

} // namespace UE::IoStore
