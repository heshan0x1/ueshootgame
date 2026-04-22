// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnDemandHttpClient.h"

#include "Async/UniqueLock.h"
#include "Containers/StringConv.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "IO/IoStoreOnDemand.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Statistics.h"

namespace UE::IoStore
{

extern int32 GIaxHttpMaxInflight;
extern int32 GIaxHttpVersion;

/** Not intended as a long term cvar, only including so that we can revert the new behavior easily if needed */
bool GIaxWarnOnFailure = true;
static FAutoConsoleVariableRef CVar_IaxWarnOnFailure(
	TEXT("iax.HttpWarnOnFailure"),
	GIaxWarnOnFailure,
	TEXT("When enabled FMultiEndpointHttpClient will log failed requests as warnings instead of the config verbosity")
);

/**
 * When enabled, http requests will make a partial content request by adding Range: bytes=<offset>-<offset+length> to the request if a valid
 * FIoOffsetAndLength is provided with the request. When disabled we will always request the full chunk and then use the FIoOffsetAndLength to
 * return a partial slice of the data to the caller.
 */
bool GAllowPartialContentRequests = true;
static FAutoConsoleVariableRef CVar_IaxAllowPartialContentRequests(
	TEXT("iax.HttpAllowPartialContentRequests"),
	GAllowPartialContentRequests,
	TEXT("Enable/disable the use of partial content http requests")
);

///////////////////////////////////////////////////////////////////////////////

FIoBuffer JoinIoBuffers(const FIoBuffer& LHS, const FIoBuffer& RHS)
{
	const uint64 TotalSize = LHS.GetSize() + RHS.GetSize();
	FIoBuffer Output(TotalSize);

	Output.GetMutableView().CopyFrom(LHS.GetView()).CopyFrom(RHS.GetView());

	return Output;
}

static int8 TrackCdnCacheStats(const HTTP::FResponse& Response)
{
	int8 Result = -1;

#if UE_TRACK_CDN_HIT_STATUS
	// All header fields are considered, with the assumption that later fields are
	// more accurate than prior ones (e.g. a caching proxy sits infront if CDNs)
	Response.ReadHeaders([&Result] (FAnsiStringView Key, FAnsiStringView Value)
	{
		if (Key == "CF-Cache-Status" || (Key.StartsWith("X-") && Key.EndsWith("-Cache")))
		{
			Result = Value.Find("HIT", 0, ESearchCase::IgnoreCase) >= 0 ? 1 : 0;
		}
		return true;
	});
#endif //UE_TRACK_CDN_HIT_STATUS

	return Result;
}

static const TCHAR* CDNCacheStatusToString(int8 Status)
{
#if UE_TRACK_CDN_HIT_STATUS
	return Status > 0 ? TEXT("HIT") : Status == 0 ? TEXT("MISS") : TEXT("???");
#else
	return TEXT("???");
#endif //UE_TRACK_CDN_HIT_STATUS
}

FIoStatus LoadDefaultHttpCertificates(bool& bWasLoaded)
{
	using namespace UE::IoStore::HTTP;
	
	bWasLoaded = false;

	static struct FDefaultCerts
	{
		FDefaultCerts(bool& bWasLoadedd)
		{
			bWasLoadedd = true;
			Status = FIoStatus::Ok;

			// The following config option is used when staging to copy root certs PEM
			const TCHAR* CertSection = TEXT("/Script/Engine.NetworkSettings");
			const TCHAR* CertKey = TEXT("n.VerifyPeer");

			bool bVerifyPeer = false;
			if (GConfig != nullptr)
			{
				GConfig->GetBool(CertSection, CertKey, bVerifyPeer, GEngineIni);
			}

			// Open the certs file
			IFileManager& Ifm = IFileManager::Get();
			const FString PemPath = FPaths::EngineContentDir() / TEXT("Certificates/ThirdParty/cacert.pem");
			TUniquePtr<FArchive> Reader(Ifm.CreateFileReader(*PemPath));

			if (Reader.IsValid())
			{
				// Buffer certificate data
				const uint64 Size = Reader->TotalSize();
				FIoBuffer PemData(Size);
				FMutableMemoryView PemView = PemData.GetMutableView();
				Reader->Serialize(PemView.GetData(), Size);

				// Load the certs
				FCertRoots CaRoots(PemData.GetView());

				const uint32 NumCerts = CaRoots.Num();
				FCertRoots::SetDefault(MoveTemp(CaRoots));

				UE_LOG(LogIoStoreOnDemand, Display, TEXT("Loaded %u certificates from '%s'"), NumCerts, *PemPath);
			}
			else if (bVerifyPeer)
			{
				Status = FIoStatusBuilder(EIoErrorCode::FileNotOpen) << TEXT("Failed to open certificates file '") << PemPath << TEXT("'");
			}
		}

		FIoStatus Status;
	} DefaultCerts(bWasLoaded);

	return DefaultCerts.Status;
}

/**
 * Generates a new FHttpTicketId when called.
 * 
 * Calling this should be thread safe and will always return a new unique non zero identifier.
 * In theory identifiers may be reused once the internal counter has overflowed but in practical
 * terms this should never cause us a problem as the identifiers are only used for a relatively
 * short lifespan.
 */
FMultiEndpointHttpClient::FHttpTicketId GenerateTicketId()
{
	static std::atomic<FMultiEndpointHttpClient::FHttpTicketId> NextFreeId = 1;

	FMultiEndpointHttpClient::FHttpTicketId NewId = 0;

	// To protect against the unlikely scenario where we somehow assign all of the ids and end up
	// overflowing we need to make sure that we don't return 0 as an Id.
	do 
	{
		NewId = NextFreeId++;
	} while (NewId == 0);
	
	return NewId;
}

///////////////////////////////////////////////////////////////////////////////

FMultiEndpointHttpClient::FMultiEndpointHttpClient(const FMultiEndpointHttpClientConfig& InConfig)
	: Config(InConfig)
{
	EventLoop.SetFailTimeout(Config.TimeoutMs);
}

FMultiEndpointHttpClient::~FMultiEndpointHttpClient()
{
	checkf(EventLoop.IsIdle(), TEXT("FMultiEndpointHttpClient still has active requests on shutdown"));
	check(TicketLookupMap.IsEmpty());
}

TUniquePtr<FMultiEndpointHttpClient> FMultiEndpointHttpClient::Create(const FMultiEndpointHttpClientConfig& Config)
{
	return TUniquePtr<FMultiEndpointHttpClient>(new FMultiEndpointHttpClient(Config));
}

TIoStatusOr<FMultiEndpointHttpClientResponse> FMultiEndpointHttpClient::Get(FAnsiStringView Url, const FMultiEndpointHttpClientConfig& Config)
{
	using namespace UE::IoStore::HTTP;

	FEventLoop::FRequestParams Params = FEventLoop::FRequestParams
	{
		.bAutoRedirect = Config.Redirects == EHttpRedirects::Follow,
		.HttpVersion = (GIaxHttpVersion == 2) ? EHttpVersion::Two : EHttpVersion::One,
	};
	if (Params.HttpVersion == EHttpVersion::Two)
	{
		Params.VerifyCert = FCertRoots::Default();
	}

	FEventLoop Loop;
	FIoBuffer Body;
	TStringBuilder<128> Reason;
	uint32 StatusCode = 0;

	const uint32 MaxAttempts = Config.MaxRetryCount == -1 ? 3u : static_cast<uint32>(Config.MaxRetryCount);

	const uint64 StartTime = FPlatformTime::Cycles64();

	for (uint32 Attempt = 0; Attempt <= MaxAttempts; ++Attempt)
	{
		Loop.Send(Loop.Request("GET", Url, &Params), [&Body, &Reason, &StatusCode](const FTicketStatus& Status)
			{
				if (Status.GetId() == FTicketStatus::EId::Response)
				{
					Status.GetResponse().SetDestination(&Body);
					StatusCode = Status.GetResponse().GetStatusCode();
					return;
				}

				if (Status.GetId() == FTicketStatus::EId::Error)
				{
					Reason << Status.GetError().Reason;
				}
			});

		while (Loop.Tick(-1))
		{
			// Busy loop
		}

		if (IsHttpStatusOk(StatusCode))
		{
			FMultiEndpointHttpClientResponse Response
			{
				.Body = MoveTemp(Body),
				.DurationMilliseconds = uint64(FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartTime)),
				.StatusCode = StatusCode,
				.RetryCount = Attempt,
			};

			return Response;
		}
	}

	if (Reason.Len() == 0)
	{
		Reason << TEXT("StatusCode: ") << StatusCode;
	}

	return FIoStatus(EIoErrorCode::ReadError, Reason.ToView());
}

FMultiEndpointHttpClient::FHttpTicketId FMultiEndpointHttpClient::Get(const FOnDemandHostGroup& HostGroup, FAnsiStringView RelativeUrl, FOnHttpResponse&& OnResponse)
{
	FIoOffsetAndLength DefaultChunkRange(0,0);
	return Get(HostGroup, RelativeUrl, DefaultChunkRange, MoveTemp(OnResponse));
}

FMultiEndpointHttpClient::FHttpTicketId FMultiEndpointHttpClient::Get(const FOnDemandHostGroup& HostGroup, FAnsiStringView RelativeUrl, const FIoOffsetAndLength& ChunkRange, FOnHttpResponse&& OnResponse)
{
	return Get(HostGroup, RelativeUrl, ChunkRange, TArray<FAnsiString>(), EMultiEndpointRequestFlags::None, MoveTemp(OnResponse));
}

FMultiEndpointHttpClient::FHttpTicketId FMultiEndpointHttpClient::Get(
	const FOnDemandHostGroup& HostGroup,
	FAnsiStringView RelativeUrl,
	const FIoOffsetAndLength& ChunkRange,
	TArray<FAnsiString>&& Headers,
	EMultiEndpointRequestFlags Flags,
	FOnHttpResponse&& OnResponse)
{
	FConnection& Connection = GetConnection(HostGroup);

	FHttpTicketId TicketId = IssueRequest(FRequest
	{
		.OnResponse		= MoveTemp(OnResponse),
		.RequestHeaders	= MoveTemp(Headers),
		.RelativeUrl	= FAnsiString(RelativeUrl),
		.Range			= ChunkRange,
		.Connection		= Connection,
		.StartTime		= FPlatformTime::Cycles64(),
		.Host			= Connection.CurrentHost,
		.Flags			= Flags
	});

	return TicketId;
}

bool FMultiEndpointHttpClient::Tick(int32 WaitTimeMs, uint32 MaxKiBPerSecond)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMultiEndpointHttpClient::Tick);
	EventLoop.Throttle(MaxKiBPerSecond);

	const uint32 TicketCount = EventLoop.Tick(WaitTimeMs);

	ProcessRetryAttempts(TicketCount);

	const bool bIsIdle = EventLoop.IsIdle();
	if (bIsIdle)
	{
		// Destroy all non active connection pool(s)
		for (TUniquePtr<FConnection>& Connection : Connections)
		{
			for (int32 Idx = 0, Count = Connection->Pools.Num(); Idx < Count; ++Idx)
			{
				if (Idx != Connection->CurrentHost)
				{
					Connection->Pools[Idx].Reset();
				}
			}
		}
	}

	return bIsIdle == false;
}

void FMultiEndpointHttpClient::CancelRequest(FHttpTicketId TicketId)
{
	if (TicketId == 0)
	{
		return;
	}

	UE::TUniqueLock _(TicketLookupMutex);

	// Note that normally we would expect to find an entry for a valid FHttpTicketId
	// but it is possible that the request just completed and was removed from the
	//  map. See the HttpSink in ::IssueRequest.
	if (FTicketInfo* Ticket = TicketLookupMap.Find(TicketId))
	{
		Ticket->bCancelRequested = true;

		if (Ticket->HttpTicket != 0)
		{
#if !NO_LOGGING
			FMsg::Logf(__FILE__, __LINE__, Config.LogCategory->GetCategoryName(), ELogVerbosity::Verbose, TEXT("Canceling FHttpTicketId %u with FTicket: %llu"), TicketId, Ticket->HttpTicket);
#endif // !NO_LOGGING

			EventLoop.Cancel(Ticket->HttpTicket);
		}
	}
}

void FMultiEndpointHttpClient::UpdateConnections()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FMultiEndpointHttpClient::UpdateConnections);

	for (TUniquePtr<FConnection>& Connection : Connections)
	{
		check(Connection.IsValid());

		Connection->CurrentHost = Connection->HostGroup.PrimaryHostIndex();

		if (Connection->CurrentHost != INDEX_NONE)
		{
			if (Connection->Pools[Connection->CurrentHost].IsValid() == false)
			{
				Connection->Pools[Connection->CurrentHost] = CreateConnection(Connection->HostGroup.PrimaryHost());
			}
		}
	}
}

FMultiEndpointHttpClient::FHttpTicketId FMultiEndpointHttpClient::IssueRequest(FRequest&& Request)
{
	using namespace UE::IoStore::HTTP;

	check(Request.Connection.HostGroup.IsEmpty() == false);
	check(Request.Connection.Pools[Request.Connection.CurrentHost].IsValid());

	FAnsiStringView Url				= Request.RelativeUrl;
	FConnectionPool& ConnectionPool = *Request.Connection.Pools[Request.Connection.CurrentHost];

	FEventLoop::FRequestParams RequestParams = FEventLoop::FRequestParams
	{
		.ContentSizeEst = uint32(Request.Range.GetLength()),
		.bAutoRedirect	= Config.Redirects == EHttpRedirects::Follow,
		.bAllowChunked	= Config.bAllowChunkedTransfer
	};

	UE::IoStore::HTTP::FRequest HttpRequest = EventLoop.Get(Url, ConnectionPool, &RequestParams);

#if !UE_BUILD_TEST
	HttpRequest.Header("pragma", "akamai-x-cache-on");
#endif

	check(Request.RequestHeaders.IsEmpty() || ((Request.RequestHeaders.Num() % 2) == 0));
	if (!Request.RequestHeaders.IsEmpty())
	{
		for (int32 Idx = 0; Idx < Request.RequestHeaders.Num(); Idx += 2)
		{
			HttpRequest.Header(Request.RequestHeaders[Idx], Request.RequestHeaders[Idx + 1]);
		}
	}

	if (GAllowPartialContentRequests && Request.Range.IsValid())
	{
		HttpRequest.Header(ANSITEXTVIEW("range"),
			WriteToAnsiString<64>(ANSITEXTVIEW("bytes="), Request.Range.GetOffset(), ANSITEXTVIEW("-"), Request.Range.GetOffset() + Request.Range.GetLength() - 1));
	}

	// If the request is new then we need to assign an identifier
	if (Request.TicketId == 0)
	{
		Request.TicketId = GenerateTicketId();
	}

	FHttpTicketId RequestId = Request.TicketId;

	auto HttpSink = [this, Request = MoveTemp(Request)](const FTicketStatus& TicketStatus) mutable
	{
		switch (TicketStatus.GetId())
		{
			case FTicketStatus::EId::Response:
			{
				FResponse& HttpResponse = TicketStatus.GetResponse();

				Request.StatusCode = HttpResponse.GetStatusCode();
				Request.bIsChunkedTransfer = HttpResponse.GetContentLength() == -1;

				if (EnumHasAnyFlags(Request.Flags, EMultiEndpointRequestFlags::ResponseHeaders))
				{
					Request.ResponseHeaders.Empty(16);
					HttpResponse.ReadHeaders([&Request] (FAnsiStringView Key, FAnsiStringView Value)
					{
						//TODO: Can we get all headers in one buffer?
						Request.ResponseHeaders.Add(FAnsiString(Key));
						Request.ResponseHeaders.Add(FAnsiString(Value));
						return true;
					});
				}

				if (IsHttpStatusOk(Request.StatusCode))
				{
					Request.CDNCacheStatus = TrackCdnCacheStats(HttpResponse);
				}
				else
				{
					Request.ResponseMessage = FString(HttpResponse.GetStatusMessage().TrimEnd());
				}

				HttpResponse.SetDestination(Request.bIsChunkedTransfer == false ? &Request.Body : &Request.Chunk);

				break;
			}
			case FTicketStatus::EId::Content:
			{
				// ::DataSize of zero means that all the chunks have been transfered and FRequest::Body should be complete
				if (Request.bIsChunkedTransfer && Request.Chunk.DataSize() != 0)
				{
					if (IsHttpStatusOk(Request.StatusCode) || Config.bResponseBodyOnError)
					{
						// Could consider using FRequest::Range to presize FRequest::Body and copy the chunks
						// into it rather than resizing each time.
						Request.Body = JoinIoBuffers(Request.Body, Request.Chunk);
					}
					return;
				}
			}
			case FTicketStatus::EId::Error:
			case FTicketStatus::EId::Cancelled:
			{
#if DO_CHECK
				check(Request.SinkCounter++ == Request.RetryCount);
#endif //DO_CHECK

				Log(Request, TicketStatus);

				// We only want to retry the request if there was an internal or server problem. There is no point retrying a 404 error for example/
				const bool bError = IsHttpServerError(Request.StatusCode) || TicketStatus.GetId() == FTicketStatus::EId::Error;

				if (bError && Request.RetryCount < GetRetryLimitForRequest(Request))
				{
					RetryRequest(MoveTemp(Request));
				}
				else
				{
					CompleteRequest(MoveTemp(Request), TicketStatus);
				}
				break;
			}
		}
	};

	FTicket RequestTicket = EventLoop.Send(MoveTemp(HttpRequest), MoveTemp(HttpSink));
	check(RequestTicket != 0);

	{
		UE::TUniqueLock _(TicketLookupMutex);
		TicketLookupMap.Add(RequestId, FTicketInfo{ .HttpTicket = RequestTicket});
	}

	return RequestId;
}

void FMultiEndpointHttpClient::CompleteRequest(FRequest&& Request, const UE::IoStore::HTTP::FTicketStatus& TicketStatus)
{
	using namespace UE::IoStore::HTTP;

	FMultiEndpointHttpClientResponse Response
	{
		.Headers				= MoveTemp(Request.ResponseHeaders),
		.DurationMilliseconds	= uint64(FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Request.StartTime)),
		.StatusCode				= Request.StatusCode,
		.RetryCount				= Request.RetryCount,
		.HostIndex				= Request.Host,
		.CDNCacheStatus			= Request.CDNCacheStatus
	};

	if (IsHttpStatusOk(Request.StatusCode) || Config.bResponseBodyOnError)
	{
		if (Request.StatusCode != 206 && Request.Range.IsValid())
		{
			// A partial read was requested but the server sent us the full response body so we need to cut down
			// the data that we return. Note that the partial buffer we return will retain a ref count on the full
			// sized buffer until it is also destroyed, so we don't need to do any allocations/copies at this point.
			FMemoryView PartialRange = Request.Body.GetView().Mid(Request.Range.GetOffset(), Request.Range.GetLength());
			Response.Body = FIoBuffer(PartialRange, Request.Body);

			// Try to limit how frequently we log warnings about this to avoid spam
			const uint64 LogSpamLimit = 50; 
			static uint64 LogCounter = 0;
			UE_CLOG(LogCounter++ % LogSpamLimit == 0, LogIoStoreOnDemand, Warning, TEXT("A partial content request failed and the full data blob was downloaded, check if the CDN supports this"));
		}
		else
		{
			Response.Body = MoveTemp(Request.Body);
		}
	}
	else if (Config.bResponseBodyOnError)
	{
		Response.Body = MoveTemp(Request.Body);
	}

	if (TicketStatus.GetId() == FTicketStatus::EId::Content)
	{
		Response.Sample = TicketStatus.GetPerf().GetSample();
	}
	
	if (TicketStatus.GetId() == FTicketStatus::EId::Error)
	{
		Response.Reason = TicketStatus.GetError().Reason;
	}
	else if (TicketStatus.GetId() == FTicketStatus::EId::Cancelled)
	{
		Response.Reason = TEXT("Canceled");
		Response.bCanceled = true;
	}
	else if (!Request.ResponseMessage.IsEmpty())
	{
		Response.Reason = MoveTemp(Request.ResponseMessage);
	}

	{
		UE::TUniqueLock _(TicketLookupMutex);
		TicketLookupMap.Remove(Request.TicketId);
	}

	FOnHttpResponse OnResponse = MoveTemp(Request.OnResponse);
	OnResponse(MoveTemp(Response));
}

void FMultiEndpointHttpClient::RetryRequest(FRequest&& Request)
{
	FConnection& Connection = Request.Connection;

	// Try a different host URL after the first retry
	if (Request.RetryCount > 0 && Request.Host == Request.Connection.CurrentHost)
	{
		FAnsiStringView HostUrl = Request.Connection.HostGroup.CycleHost(Connection.CurrentHost);
		if (Connection.Pools[Connection.CurrentHost].IsValid() == false)
		{
			Connection.Pools[Connection.CurrentHost] = CreateConnection(HostUrl);
		}
	}

	// Clear the ticket association, note that we don't check if the request is canceled as we'd have to
	// check it again anyway when the retry attempts are being reissued so we might as well wait and do
	// it in one place.
	{
		UE::TUniqueLock _(TicketLookupMutex);
		if (FTicketInfo* TicketInfo = TicketLookupMap.Find(Request.TicketId))
		{
			TicketInfo->HttpTicket = 0;
		}
		else
		{
			checkNoEntry();
		}
	}

	Request.StatusCode = 0;
	Request.RetryCount++;
	Request.Host = Connection.CurrentHost;
	Retries.Emplace(MoveTemp(Request));
}

void FMultiEndpointHttpClient::ProcessRetryAttempts(uint32 TicketCount)
{
	if(Retries.IsEmpty())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(FMultiEndpointHttpClient::ProcessRetryAttempts);
	UE::TUniqueLock _(TicketLookupMutex);

	const int32 RequestCount = FMath::Min(Retries.Num(), int32(HTTP::FEventLoop::MaxActiveTickets - TicketCount));
	for (int32 Idx = 0; Idx < RequestCount; Idx++)
	{
		FRequest& Request = Retries[Idx];

		if (FTicketInfo* TicketInfo = TicketLookupMap.Find(Request.TicketId))
		{
			check(TicketInfo->HttpTicket == 0);

			if (!TicketInfo->bCancelRequested)
			{
				IssueRequest(MoveTemp(Request));
			}
			else
			{
				FMultiEndpointHttpClientResponse Response
				{
					.Reason = TEXT("Canceled"),
					.DurationMilliseconds = uint64(FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Request.StartTime)),
					.StatusCode = Request.StatusCode,
					.RetryCount = Request.RetryCount -1, // Reduce retry count by 1 as this retry attempt didn't really do anything
					.HostIndex = Request.Host,
					.bCanceled = true,
					.CDNCacheStatus = Request.CDNCacheStatus
				};

				// The request is completed so we can remove it from the lookup map
				TicketLookupMap.Remove(Request.TicketId);

				// TODO: Consider moving this outside of the TicketLookupMutex lock?
				FOnHttpResponse OnResponse = MoveTemp(Request.OnResponse);
				OnResponse(MoveTemp(Response));	
			}
		}
		else
		{
			checkNoEntry(); // If the request is ongoing then we should always have a valid entry
		}
	}

	Retries.RemoveAtSwap(0, RequestCount);
}

TUniquePtr<HTTP::FConnectionPool> FMultiEndpointHttpClient::CreateConnection(FAnsiStringView HostUrl) const
{
	using namespace HTTP;

	FConnectionPool::FParams Params;
	ensure(Params.SetHostFromUrl(HostUrl) >= 0);

	if (Config.ReceiveBufferSize >= 0)
	{
		Params.RecvBufSize = Config.ReceiveBufferSize;
	}

	if (Config.SendBufferSize >= 0)
	{
		Params.SendBufSize = Config.SendBufferSize;
	}

	Params.ConnectionCount = uint16(Config.MaxConnectionCount);
	Params.HttpVersion = (GIaxHttpVersion == 2) ? EHttpVersion::Two : EHttpVersion::One;
	Params.MaxInflight = uint8(FMath::Clamp(GIaxHttpMaxInflight, 1, 64));
	if (Params.HttpVersion == EHttpVersion::Two)
	{
		Params.ConnectionCount = 1; // rfc9113 conformance
		Params.VerifyCert = HTTP::FCertRoots::Default();
	}

	return MakeUnique<HTTP::FConnectionPool>(Params);
}

FMultiEndpointHttpClient::FConnection& FMultiEndpointHttpClient::GetConnection(const FOnDemandHostGroup& HostGroup)
{
	for (TUniquePtr<FConnection>& Conn : Connections)
	{
		check(Conn.IsValid());
		if (Conn->HostGroup == HostGroup)
		{
			return *Conn;
		}
	}

	FConnection& Conn = *Connections.Emplace_GetRef(new FConnection
		{
			.HostGroup = HostGroup,
		});

	Conn.Pools.SetNum(HostGroup.Hosts().Num());
	Conn.CurrentHost = HostGroup.PrimaryHostIndex();

	Conn.Pools[Conn.CurrentHost] = CreateConnection(HostGroup.PrimaryHost());
	return Conn;
}

FMultiEndpointHttpClient::FConnection* FMultiEndpointHttpClient::FindConnection(const FOnDemandHostGroup& HostGroup)
{
	for (TUniquePtr<FConnection>& Conn : Connections)
	{
		check(Conn.IsValid());
		if (Conn->HostGroup == HostGroup)
		{
			return Conn.Get();
		}
	}

	return nullptr;
}

uint32 FMultiEndpointHttpClient::GetRetryLimitForRequest(const FRequest& Request) const
{
	return Config.MaxRetryCount == -1 ? Request.Connection.HostGroup.Hosts().Num() : Config.MaxRetryCount;
}

void FMultiEndpointHttpClient::Log(const FRequest& Request, const HTTP::FTicketStatus& TicketStatus) const
{
#if !NO_LOGGING

	using namespace UE::IoStore::HTTP;

	ELogVerbosity::Type Verbosity = Config.LogVerbosity;
	// We count  a request as failed if it's status code is bad AND the request was not canceled
	if (GIaxWarnOnFailure && !IsHttpStatusOk(Request.StatusCode) && TicketStatus.GetId() != FTicketStatus::EId::Cancelled)
	{
		Verbosity = ELogVerbosity::Type::Warning;
	}

	if (Config.LogCategory == nullptr || Config.LogCategory->IsSuppressed(Verbosity))
	{
		return;
	}

	const uint64 DurationMs = (uint64)FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Request.StartTime);
	const uint64 Size = Request.Body.GetSize() >> 10;

	TStringBuilder<256> Reason;
	if (TicketStatus.GetId() == FTicketStatus::EId::Error)
	{
		Reason << TEXT(" ") << TicketStatus.GetError().Reason;
	}
	else if (TicketStatus.GetId() == FTicketStatus::EId::Cancelled)
	{
		Reason << TEXT(" Canceled");
	}
	else if (!Request.ResponseMessage.IsEmpty())
	{
		Reason << TEXT(" ") << Request.ResponseMessage;
	}

	FMsg::Logf(__FILE__, __LINE__, Config.LogCategory->GetCategoryName(), Verbosity,
		TEXT("http-%3u: %5" UINT64_FMT "ms %5" UINT64_FMT "KiB [%4s] %s (Attempt %u/%u)%s"),
		Request.StatusCode,
		DurationMs,
		Size,
		CDNCacheStatusToString(Request.CDNCacheStatus),
		WriteToString<512>(Request.Connection.HostGroup.Host(Request.Host), Request.RelativeUrl).ToString(),
		Request.RetryCount,
		GetRetryLimitForRequest(Request),
		*Reason
	);

#endif //!NO_LOGGING
}

} // namespace UE::IoStore
