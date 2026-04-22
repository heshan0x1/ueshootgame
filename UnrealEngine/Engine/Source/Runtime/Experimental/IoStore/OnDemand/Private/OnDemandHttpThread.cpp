// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnDemandHttpThread.h"

#include "Async/UniqueLock.h"
#include "HAL/IConsoleManager.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/RunnableThread.h"
#include "IO/IoStoreOnDemand.h"
#include "IasHostGroup.h"
#include "Logging/StructuredLog.h"
#include "Misc/CommandLine.h"
#include "Misc/Fork.h"
#include "OnDemandHttpClient.h"
#include "OnDemandIoStore.h"
#include "Statistics.h"

#ifndef UE_IOSTORE_ONDEMAND_NO_HTTP_THREAD
#define UE_IOSTORE_ONDEMAND_NO_HTTP_THREAD 0
#endif

// When enabled the cvar 'iax.InvalidUrlChance' will be enabled and allow us to simulate invalid urls
#define UE_ALLOW_INVALID_URL_DEBUGGING !UE_BUILD_SHIPPING

// TODO - Maybe do LexToString/LexFromString (dupe code in UnrealEngine.cpp ThreadPriorityToString)
const TCHAR* LexToString(EThreadPriority Priority)
{
	switch (Priority)
	{
		case EThreadPriority::TPri_Normal:
			return TEXT("TPri_Normal");
		case EThreadPriority::TPri_AboveNormal:
			return TEXT("TPri_AboveNormal");
		case EThreadPriority::TPri_BelowNormal:
			return TEXT("TPri_BelowNormal");
		case EThreadPriority::TPri_Highest:
			return TEXT("TPri_Highest");
		case EThreadPriority::TPri_Lowest:
			return TEXT("TPri_Lowest");
		case EThreadPriority::TPri_SlightlyBelowNormal:
			return TEXT("TPri_SlightlyBelowNormal");
		case EThreadPriority::TPri_TimeCritical:
			return TEXT("TPri_TimeCritical");
		case EThreadPriority::TPri_Num:
		default:
			return TEXT("TPri_Undefined");
			break;
	};
}

#if UE_ALLOW_HTTP_PAUSE

void FConsoleCommandUnregister::operator()(IConsoleCommand* ConsoleCommandPtr) const
{
	IConsoleManager::Get().UnregisterConsoleObject(ConsoleCommandPtr);
};

#endif // UE_ALLOW_HTTP_PAUSE

namespace UE::IoStore
{

///////////////////////////////////////////////////////////////////////////////

DECLARE_MULTICAST_DELEGATE(FOnRecreateHttpClient);
FOnRecreateHttpClient OnRecreateHttpClient;

ENUM_CLASS_FLAGS(EHttpRequestTypeFilter);

void OnHttpClientCVarChanged(IConsoleVariable* CVar)
{
	if (ensure(CVar != nullptr))
	{
		const FString CVarName = IConsoleManager::Get().FindConsoleObjectName(CVar);
		UE_LOGFMT(LogIas, Log, "Existing http client config invalidated as cvar '{CvarName}' has changed", CVarName);

		OnRecreateHttpClient.Broadcast();
	}
}
///////////////////////////////////////////////////////////////////////////////

int32 GIasHttpHealthCheckWaitTime = 3000;
static FAutoConsoleVariableRef CVar_IasHttpHealthCheckWaitTime(
	TEXT("ias.HttpHealthCheckWaitTime"),
	GIasHttpHealthCheckWaitTime,
	TEXT("Number of milliseconds to wait before reconnecting to avaiable endpoint(s)")
);

int32 GOnDemandBackendThreadPriorityIndex = 4; // EThreadPriority::TPri_AboveNormal
FAutoConsoleVariableRef CVarOnDemandBackendThreadPriority(
	TEXT("ias.onDemandBackendThreadPriority"),
	GOnDemandBackendThreadPriorityIndex,
	TEXT("Thread priority of the on demand backend thread: 0=Lowest, 1=BelowNormal, 2=SlightlyBelowNormal, 3=Normal, 4=AboveNormal\n")
	TEXT("Note that this is switchable at runtime"),
	ECVF_Default);

int32 GIaxHttpVersion = 1;
static FAutoConsoleVariableRef CVar_IaxHttpVersion(
	TEXT("iax.HttpVersion"),
	GIaxHttpVersion,
	TEXT("Protocol version to use, either '1' or '2'")
);

int32 GIasHttpConnectionCount = 4;
static FAutoConsoleVariableRef CVar_IasHttpConnectionCount(
	TEXT("ias.HttpConnectionCount"),
	GIasHttpConnectionCount,
	TEXT("Number of open HTTP connections to the on demand endpoint(s)."),
	FConsoleVariableDelegate::CreateStatic(&OnHttpClientCVarChanged)
);

int32 GIaxHttpMaxInflight = 1;
static FAutoConsoleVariableRef CVar_IaxHttpMaxInflight(
	TEXT("iax.HttpMaxInflight"),
	GIaxHttpMaxInflight,
	TEXT("Number of requests to issue at once per connection")
);

int32 GIasHttpRecvBufKiB = -1;
static FAutoConsoleVariableRef CVar_GIasHttpRecvBufKiB(
	TEXT("ias.HttpRecvBufKiB"),
	GIasHttpRecvBufKiB,
	TEXT("Recv buffer size"),
	FConsoleVariableDelegate::CreateStatic(&OnHttpClientCVarChanged)
);

int32 GIasHttpSendBufKiB = -1;
static FAutoConsoleVariableRef CVar_GIasHttpSendBufKiB(
	TEXT("ias.HttpSendBufKiB"),
	GIasHttpSendBufKiB,
	TEXT("Send buffer size"),
	FConsoleVariableDelegate::CreateStatic(&OnHttpClientCVarChanged)
);

int32 GIasHttpRetryCount = -1;
static FAutoConsoleVariableRef CVar_IasHttpRetryCount(
	TEXT("ias.HttpRetryCount"),
	GIasHttpRetryCount,
	TEXT("Number of times that a request should be retried before being considered failed. A negative value will use the default behaviour, which is one retry per host url provided."),
	FConsoleVariableDelegate::CreateStatic(&OnHttpClientCVarChanged)
);

int32 GIasHttpFailTimeOutMs = 4 * 1000;
static FAutoConsoleVariableRef CVar_IasHttpFailTimeOutMs(
	TEXT("ias.HttpFailTimeOutMs"),
	GIasHttpFailTimeOutMs,
	TEXT("Fail infinite network waits that take longer than this (in ms, a value of zero will use the default)"),
	FConsoleVariableDelegate::CreateStatic(&OnHttpClientCVarChanged)
);

bool GIasHttpAllowChunkedXfer = true;
static FAutoConsoleVariableRef CVar_IasHttpAllowChunkedXfer(
	TEXT("ias.HttpAllowChunkedXfer"),
	GIasHttpAllowChunkedXfer,
	TEXT("Enable/disable IAS' support for chunked transfer encoding"),
	FConsoleVariableDelegate::CreateStatic(&OnHttpClientCVarChanged)
);

int32 GIasHttpConcurrentRequests = 8;
static FAutoConsoleVariableRef CVar_IasHttpConcurrentRequests(
	TEXT("ias.HttpConcurrentRequests"),
	GIasHttpConcurrentRequests,
	TEXT("Number of concurrent requests in the http client.")
);

int32 GIasHttpRateLimitKiBPerSecond = 0;
static FAutoConsoleVariableRef CVar_GIasHttpRateLimitKiBPerSecond(
	TEXT("ias.HttpRateLimitKiBPerSecond"),
	GIasHttpRateLimitKiBPerSecond,
	TEXT("Http throttle limit in KiBPerSecond")
);

int32 GIasHttpPollTimeoutMs = 17;
static FAutoConsoleVariableRef CVar_GIasHttpPollTimeoutMs(
	TEXT("ias.HttpPollTimeoutMs"),
	GIasHttpPollTimeoutMs,
	TEXT("Http tick poll timeout in milliseconds")
);

bool GIaxHttpEnableInflightCancellation = true;
static FAutoConsoleVariableRef CVar_IaxHttpEnableInflightCancellation(
	TEXT("iax.HttpEnableInflightCancellation"),
	GIaxHttpEnableInflightCancellation,
	TEXT("When enabled the system will attempt to cancel inflight http requests")
);

#if UE_ALLOW_INVALID_URL_DEBUGGING
float GIaxInvalidUrlChance = 0.0;
static FAutoConsoleVariableRef CVar_GIaxInvalidUrlChance(
	TEXT("iax.InvalidUrlChance"),
	GIaxInvalidUrlChance,
	TEXT("Chance that a url for a GET request will be invalid (0-100%)")
);
#endif // UE_ALLOW_INVALID_URL_DEBUGGING

const int32 GOnDemandBackendThreadPriorities[5] =
{
	EThreadPriority::TPri_Lowest,
	EThreadPriority::TPri_BelowNormal,
	EThreadPriority::TPri_SlightlyBelowNormal,
	EThreadPriority::TPri_Normal,
	EThreadPriority::TPri_AboveNormal
};

////////////////////////////////////////
// START EXTERN CVARS
////////////////////////////////////////

extern int32 GIasHttpTimeOutMs;

////////////////////////////////////////
// END EXTERN CVARS
////////////////////////////////////////

struct FHttpRequest
{
	FHttpRequest() = default;
	FHttpRequest(FOnDemandChunkInfo&& InChunkInfo)
		: ChunkInfo(MoveTemp(InChunkInfo))
	{

	}

	~FHttpRequest() = default;

	void OnRequestCompleted(uint32 StatusCode, FStringView ErrorReason, FIoBuffer&& Data)
	{
		CompletionCallback(StatusCode, ErrorReason, MoveTemp(Data));
	}

	FIASHostGroup HostGroup()
	{
		return ChunkInfo.HostGroup();
	}

	void GetUrl(FAnsiStringBuilderBase& Url) const
	{
		ChunkInfo.GetUrl(Url);
	}

	FHttpRequest* NextRequest = nullptr;
	FMultiEndpointHttpClient::FHttpTicketId HttpTicketId = 0;
	FOnDemandHttpThread::FCompletionCallback CompletionCallback;
	FOnDemandChunkInfo ChunkInfo;
	FIoOffsetAndLength ChunkRange;
	EHttpRequestType Type;
	bool bCancelled = false;
	int32 Priority = 0;
};

FOnDemandHttpThread::FOnDemandHttpThread()
{
	LLM_SCOPE_BYTAG(Ias);

#if UE_IOSTORE_ONDEMAND_NO_HTTP_THREAD
	ensure(TryCreateHttpClient());
#else
	const int32 ThreadPriorityIndex			= FMath::Clamp(GOnDemandBackendThreadPriorityIndex, 0, (int32)UE_ARRAY_COUNT(GOnDemandBackendThreadPriorities) - 1);
	EThreadPriority DesiredThreadPriority	= (EThreadPriority)GOnDemandBackendThreadPriorities[ThreadPriorityIndex];
	ThreadPriority							= DesiredThreadPriority;
	const uint32 StackSize					= 0; // Use default stack size
	const uint64 ThreadAffinityMask			= FGenericPlatformAffinity::GetNoAffinityMask();
	const EThreadCreateFlags CreateFlags	= EThreadCreateFlags::None;
	const bool bAllowPreFork				= FParse::Param(FCommandLine::IsInitialized() ? FCommandLine::Get() : TEXT(""), TEXT("-Ias.EnableHttpThreadPreFork"));

	Thread.Reset(FForkProcessHelper::CreateForkableThread(this, TEXT("IoStoreOnDemand.Http"), StackSize, ThreadPriority, ThreadAffinityMask, CreateFlags, bAllowPreFork));
#endif

	OnRecreateHttpClientHandle = OnRecreateHttpClient.AddLambda([this]()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandHttpThread::OnRecreateHttpClient);

		UE::TUniqueLock _(RecreateMutex);
		bRecreateHttpClient = true;
		TickThreadEvent->Trigger();
	});

	OnHostGroupDisconnectedHandle = FHostGroupManager::Get().OnHostGroupDisconncted().AddLambda([this]()
	{
		TickThreadEvent->Trigger();
	});

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
}

FOnDemandHttpThread::~FOnDemandHttpThread()
{
	if (OnHostGroupDisconnectedHandle.IsValid())
	{
		FHostGroupManager::Get().OnHostGroupDisconncted().Remove(OnHostGroupDisconnectedHandle);
		OnHostGroupDisconnectedHandle.Reset();
	}

	if (OnRecreateHttpClientHandle.IsValid())
	{
		OnRecreateHttpClient.Remove(OnRecreateHttpClientHandle);
		OnRecreateHttpClientHandle.Reset();
	}

	Thread.Reset();

	// TODO: Not 100% sure this is still needed?
	DrainHttpRequests();
}

FOnDemandHttpThread::FRequestHandle FOnDemandHttpThread::IssueRequest(const FOnDemandChunkInfo& ChunkInfo, const FIoOffsetAndLength& ReadRange, int32 Priority, FCompletionCallback&& CompletionCallback, EHttpRequestType Type)
{
	return IssueRequest(FOnDemandChunkInfo(ChunkInfo), ReadRange, Priority, MoveTemp(CompletionCallback), Type);
}

FOnDemandHttpThread::FRequestHandle FOnDemandHttpThread::IssueRequest(FOnDemandChunkInfo&& ChunkInfo, const FIoOffsetAndLength& ReadRange, int32 Priority, FCompletionCallback&& CompletionCallback, EHttpRequestType Type)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandHttpThread::IssueRequest);

	FHttpRequest* Request = AllocateRequest(MoveTemp(ChunkInfo));

	Request->ChunkRange = ReadRange;
	Request->CompletionCallback = MoveTemp(CompletionCallback);
	Request->Type = Type;
	Request->Priority = Priority;

	FOnDemandIoBackendStats::Get()->OnHttpEnqueue(Request->Type);

#if UE_ALLOW_HTTP_PAUSE
	if (!PauseRequest(Request))
	{
		HttpRequests.EnqueueByPriority(Request);
	}
#else
	HttpRequests.EnqueueByPriority(Request);
#endif // UE_ALLOW_HTTP_PAUSE

#if UE_IOSTORE_ONDEMAND_NO_HTTP_THREAD 
	// When threading is disabled the completion callback has already been triggered and the handle is destroyed before returned to the caller.
	Tick();
#else
	TickThreadEvent->Trigger();
#endif
	return Request;
}

void FOnDemandHttpThread::ReprioritizeRequest(FRequestHandle Request, int32 NewPriority)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandHttpThread::ReprioritizeRequest);
	if (Request != 0)
	{
		HttpRequests.Reprioritize(static_cast<FHttpRequest*>(Request), NewPriority);
	}
}

void FOnDemandHttpThread::CancelRequest(FRequestHandle RequestHandle)
{
	if (RequestHandle != 0)
	{
		FHttpRequest* Request = static_cast<FHttpRequest*>(RequestHandle);
		
		Request->bCancelled = true;

		if (GIaxHttpEnableInflightCancellation)
		{
			HttpClient->CancelRequest(Request->HttpTicketId);
		}
	}
}

bool FOnDemandHttpThread::Init()
{
	LLM_SCOPE_BYTAG(Ias);
	if (!TryCreateHttpClient())
	{
		return false;
	}

	return true;
}

uint32 FOnDemandHttpThread::Run()
{
	LLM_SCOPE_BYTAG(Ias);

	check(HttpClient.IsValid());

	while (!bStopRequested)
	{
		UpdateThreadPriorityIfNeeded();

		if(!bStopRequested)
		{
			Tick();
			TickThreadEvent->Wait(FHostGroupManager::Get().GetNumDisconnctedHosts() > 0 ? GIasHttpHealthCheckWaitTime : MAX_uint32);
		}
	}

	return 0;
}

void FOnDemandHttpThread::Stop()
{
	bStopRequested = true;
	TickThreadEvent->Trigger();
}

void FOnDemandHttpThread::Exit()
{
	HttpClient.Reset();
}

void FOnDemandHttpThread::Tick()
{
	FHostGroupManager::Get().Tick(GIasHttpTimeOutMs, bStopRequested);
	
	// TODO: It would be better to only update connections as they need it, consider doing this on
	// hostgroup connect/disconnect events.
	HttpClient->UpdateConnections();

	TickRequests();

	OnTickIdleDelegate.Broadcast();
}

void FOnDemandHttpThread::TickRequests()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandHttpThread::TickRequests);

	if (bRecreateHttpClient)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandHttpThread::RecreateHttpClient);
		UE::TUniqueLock _(RecreateMutex);

		// Keep the old client at first in case we fail to create the new one so that we can restore it
		TUniquePtr<FMultiEndpointHttpClient> OldHttpClient = MoveTemp(HttpClient);
		if (TryCreateHttpClient())
		{
			OldHttpClient.Reset();
			UE_LOGFMT(LogIas, Display, "FOnDemandHttpThread: Successfully created a new http client for use");
		}
		else
		{
			HttpClient = MoveTemp(OldHttpClient);
			UE_LOGFMT(LogIas, Warning, "FOnDemandHttpThread: Failed to create a new http client, the existing client will continue to be used");
		}

		bRecreateHttpClient = false;
	}

	// We can't limit cvars so add a hard coded cap to prevent concurrent requests from getting out of hand.
	const int32 MaxConcurrentRequests = FMath::Min(GIasHttpConcurrentRequests, 32);

	int32 NumConcurrentRequests = 0;
	FHttpRequest* NextHttpRequest = HttpRequests.Dequeue(MaxConcurrentRequests);

	while (NextHttpRequest)
	{
		while (NextHttpRequest)
		{
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandHttpThread::IssueHttpGet);
				FHttpRequest* HttpRequest = NextHttpRequest;
				NextHttpRequest = HttpRequest->NextRequest;
				HttpRequest->NextRequest = nullptr;

#if UE_ALLOW_HTTP_PAUSE
				if (PauseRequest(HttpRequest))
				{
					continue;
				}
#endif // UE_ALLOW_HTTP_PAUSE

				FOnDemandIoBackendStats::Get()->OnHttpDequeue(HttpRequest->Type);

				if (HttpRequest->bCancelled)
				{
					HttpRequest->OnRequestCompleted(0, TEXTVIEW("Request cancelled"), FIoBuffer());
					FOnDemandIoBackendStats::Get()->OnHttpCancel(HttpRequest->Type);

					DestroyRequest(HttpRequest);
				}
				else if (!HttpRequest->HostGroup().IsConnected() || !bHttpEnabled)
				{
					HttpRequest->OnRequestCompleted(0, TEXTVIEW("Hostgroup is disconnected"), FIoBuffer());

					// Technically this request is being skipped because of a pre-existing error. It is not
					// an error itself and it is not being canceled by higher level code. However we do not
					// currently have a statistic for that and we have to call one of the existing types in
					// order to correctly reduce the pending count.
					FOnDemandIoBackendStats::Get()->OnHttpCancel(HttpRequest->Type);

					DestroyRequest(HttpRequest);
				}
				else
				{
					NumConcurrentRequests++;

					TAnsiStringBuilder<128> ChunkUrl;
					HttpRequest->GetUrl(ChunkUrl);

#if UE_ALLOW_INVALID_URL_DEBUGGING
					// Avoid the rand call if there is no chance
					if (GIaxInvalidUrlChance > 0.0 && (FMath::FRand() * 100.0f) < GIaxInvalidUrlChance)
					{
						ChunkUrl << "-DebugInvalidUrl";
					}
#endif // UE_ALLOW_INVALID_URL_DEBUGGING

					FOnDemandIoBackendStats::Get()->OnHttpRequestStarted();

					HttpRequest->HttpTicketId = HttpClient->Get(HttpRequest->HostGroup().GetUnderlyingHostGroup(), ChunkUrl, HttpRequest->ChunkRange,
						[this, HttpRequest, &NumConcurrentRequests]
						(FMultiEndpointHttpClientResponse&& HttpResponse)
						{
							TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandHttpThread::RequestCallback);

							NumConcurrentRequests--;
							FOnDemandIoBackendStats::Get()->OnHttpRequestCompleted();

							if (HttpResponse.RetryCount > 0)
							{
								FOnDemandIoBackendStats::Get()->OnHttpRetry(HttpRequest->Type);
							}

							if (HttpResponse.IsOk())
							{
								HttpRequest->HostGroup().OnSuccessfulResponse();

								FOnDemandIoBackendStats::Get()->OnHttpCdnCacheReply(HttpRequest->Type, HttpResponse.CDNCacheStatus);
								FOnDemandIoBackendStats::Get()->OnHttpGet(HttpRequest->Type, HttpResponse.Body.DataSize(), HttpResponse.DurationMilliseconds);

								HttpRequest->OnRequestCompleted(HttpResponse.StatusCode, HttpResponse.Reason, MoveTemp(HttpResponse.Body));
							}
							else if (HttpResponse.IsCanceled())
							{
								FOnDemandIoBackendStats::Get()->OnHttpCancel(HttpRequest->Type);

								HttpRequest->OnRequestCompleted(HttpResponse.StatusCode, HttpResponse.Reason, MoveTemp(HttpResponse.Body));
							}
							else
							{
								FOnDemandIoBackendStats::Get()->OnHttpError(HttpRequest->Type);

								if (HttpRequest->HostGroup().OnFailedResponse())
								{
									// A disconnect was triggered
									FOnDemandIoBackendStats::Get()->OnHttpDisconnected();
								}

								HttpRequest->OnRequestCompleted(HttpResponse.StatusCode, HttpResponse.Reason, FIoBuffer());
							}

							DestroyRequest(HttpRequest);
						});
				}
			}

			if (NumConcurrentRequests >= MaxConcurrentRequests)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandHttpThread::TickHttpSaturated);
				while (NumConcurrentRequests >= MaxConcurrentRequests) //-V654
				{
					HttpClient->Tick(MAX_uint32, GIasHttpRateLimitKiBPerSecond);
				}
			}

			if (!NextHttpRequest && !bRecreateHttpClient)
			{
				NextHttpRequest = HttpRequests.Dequeue(MaxConcurrentRequests - NumConcurrentRequests);
			}
		}

		{
			// Keep processing pending connections until all requests are completed or a new one is issued
			TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandHttpThread::TickHttp);
			while (HttpClient->Tick(GIasHttpPollTimeoutMs, GIasHttpRateLimitKiBPerSecond))
			{
				if (!NextHttpRequest && !bRecreateHttpClient)
				{
					NextHttpRequest = HttpRequests.Dequeue(MaxConcurrentRequests - NumConcurrentRequests);
				}

				if (NextHttpRequest)
				{
					break;
				}
			}
		}
	}
}

void FOnDemandHttpThread::DrainHttpRequests()
{
#if UE_ALLOW_HTTP_PAUSE
	OnTogglePause(false, EHttpRequestTypeFilter::All);
#endif // UE_ALLOW_HTTP_PAUSE

	FHttpRequest* Iterator = HttpRequests.Dequeue();
	while (Iterator != nullptr)
	{
		FHttpRequest* Request = Iterator;
		Iterator = Iterator->NextRequest;

		FOnDemandIoBackendStats::Get()->OnHttpDequeue(Request->Type);
		Request->OnRequestCompleted(0, TEXTVIEW("Request cancelled due to shutdown"), FIoBuffer());
		DestroyRequest(Request);
		FOnDemandIoBackendStats::Get()->OnHttpCancel(Request->Type);
	}
}

bool FOnDemandHttpThread::TryCreateHttpClient()
{
	HttpClient = FMultiEndpointHttpClient::Create(FMultiEndpointHttpClientConfig
		{
			.MaxConnectionCount = GIasHttpConnectionCount,
			.ReceiveBufferSize = GIasHttpRecvBufKiB >= 0 ? GIasHttpRecvBufKiB << 10 : -1,
			.SendBufferSize = GIasHttpSendBufKiB >= 0 ? GIasHttpSendBufKiB << 10 : -1,
			.MaxRetryCount = GIasHttpRetryCount,
			.TimeoutMs = GIasHttpFailTimeOutMs,
			.Redirects = EHttpRedirects::Disabled,
			.bEnableThreadSafetyChecks = true,
			.bAllowChunkedTransfer = GIasHttpAllowChunkedXfer,
			.LogCategory = &LogIas,
			.LogVerbosity = ELogVerbosity::VeryVerbose
		});

	return HttpClient.IsValid();
}

void FOnDemandHttpThread::UpdateThreadPriorityIfNeeded()
{
	// Read the thread priority from the cvar
	int32 ThreadPriorityIndex = FMath::Clamp(GOnDemandBackendThreadPriorityIndex, 0, (int32)UE_ARRAY_COUNT(GOnDemandBackendThreadPriorities) - 1);
	EThreadPriority DesiredThreadPriority = (EThreadPriority)GOnDemandBackendThreadPriorities[ThreadPriorityIndex];
	if (DesiredThreadPriority != ThreadPriority)
	{
		UE_LOGFMT(LogIas, Log, "Updated IoStoreOnDemand.Http thread priority to '{Priority}'", LexToString(DesiredThreadPriority));
		
		FPlatformProcess::SetThreadPriority(DesiredThreadPriority);
		ThreadPriority = DesiredThreadPriority;
	}
}

FHttpRequest* FOnDemandHttpThread::AllocateRequest(FOnDemandChunkInfo&& ChunkInfo)
{
	UE::TUniqueLock _(AllocatorMutex);
	return RequestAllocator.Construct(MoveTemp(ChunkInfo));
}

void FOnDemandHttpThread::DestroyRequest(FHttpRequest* Request)
{
	UE::TUniqueLock _(AllocatorMutex);
	RequestAllocator.Destroy(Request);
}

#if UE_ALLOW_HTTP_PAUSE

bool FOnDemandHttpThread::PauseRequest(FHttpRequest* Request)
{
	UE::TUniqueLock _(PausedMutex);

	if (IsPaused(Request->Type))
	{
		HttpPausedRequests.Enqueue(Request);
		return true;
	}
	else
	{
		return false;
	}
}

void FOnDemandHttpThread::OnPauseCommand(bool bShouldPause, const TArray<FString>& Args, FOutputDevice& Ar)
{
	if (Args.Num() > 1)
	{
#if !NO_LOGGING
		Ar.Log(LogIas.GetCategoryName(), ELogVerbosity::Error, TEXT("Too many args for command, either 0 (all) or 1 (IAD/IAS) expected"));
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
			Ar.Log(LogIas.GetCategoryName(), ELogVerbosity::Error, TEXT("Invalid arg for command, expecting 'IAD' or 'IAS'"));
#endif // !NO_LOGGING
			return;
		}
	}

	OnTogglePause(bShouldPause, Filter);
}

void FOnDemandHttpThread::OnTogglePause(bool bPause, EHttpRequestTypeFilter Filter)
{
	UE::TUniqueLock _(PausedMutex);
	
	if (bPause)
	{
		EnumAddFlags(PauseFilter, Filter);


	}
	else
	{
		EnumRemoveFlags(PauseFilter, Filter);

		FHttpRequest* PendingRequests = HttpPausedRequests.Dequeue();
		while (PendingRequests != nullptr)
		{
			FHttpRequest* RequestToQueue = PendingRequests;
			PendingRequests = PendingRequests->NextRequest;

			RequestToQueue->NextRequest = nullptr;

			if (IsPaused(RequestToQueue->Type))
			{
				HttpPausedRequests.Enqueue(RequestToQueue);
			}
			else
			{
				HttpRequests.EnqueueByPriority(RequestToQueue);
			}
		}

		TickThreadEvent->Trigger();
	}
}

bool FOnDemandHttpThread::IsPaused(EHttpRequestType RequestType) const
{
	switch (RequestType)
	{
		case EHttpRequestType::Installed:
			return EnumHasAnyFlags(PauseFilter, EHttpRequestTypeFilter::Installed);
		case EHttpRequestType::Streaming:
			return EnumHasAnyFlags(PauseFilter, EHttpRequestTypeFilter::Streaming);
		default:
		{
			checkNoEntry();
			return false;
		}
	}
}

#endif // UE_ALLOW_HTTP_PAUSE

} //namespace UE::IoStore

#undef UE_ALLOW_INVALID_URL_DEBUGGING
