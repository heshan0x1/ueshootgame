// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/HttpIoDispatcher.h"
#include "Containers/AnsiString.h"
#include "Misc/StringBuilder.h"

DEFINE_LOG_CATEGORY(LogHttpIoDispatcher);

namespace UE
{

////////////////////////////////////////////////////////////////////////////////
TSharedPtr<IHttpIoDispatcher> GHttpIoDispatcher;
const FIoHttpOptions FIoHttpOptions::Default = FIoHttpOptions();

////////////////////////////////////////////////////////////////////////////////
FIoHttpHeaders& FIoHttpHeaders::Add(FAnsiString&& HeaderName, FAnsiString&& HeaderValue)
{
	Headers.Add(MoveTemp(HeaderName));
	Headers.Add(MoveTemp(HeaderValue));
	return *this;
}

FIoHttpHeaders& FIoHttpHeaders::Add(FAnsiStringView HeaderName, FAnsiStringView HeaderValue)
{
	Headers.Emplace(HeaderName);
	Headers.Emplace(HeaderValue);
	return *this;
}

FAnsiStringView FIoHttpHeaders::Get(FAnsiStringView Key) const
{
	for (int32 Idx = 0, Count = Headers.Num(); Idx < Count; Idx += 2)
	{
		FAnsiStringView Header = Headers[Idx];
		if (Header.Equals(Key, ESearchCase::IgnoreCase))
		{
			return Headers[Idx + 1];
		}
	}

	return FAnsiStringView();
}

FAnsiStringView FIoHttpHeaders::Get(const FAnsiString& Key) const
{
	FAnsiStringView KeyView = Key;
	return Get(KeyView);
}

TArray<FAnsiString> FIoHttpHeaders::ToArray() &&
{
	return MoveTemp(Headers);
}

FIoHttpHeaders FIoHttpHeaders::Create(FAnsiString&& HeaderName, FAnsiString&& HeaderValue)
{
	FIoHttpHeaders Headers;
	Headers.Add(MoveTemp(HeaderName), MoveTemp(HeaderValue));
	return Headers;
}

FIoHttpHeaders FIoHttpHeaders::Create(FAnsiStringView HeaderName, FAnsiStringView HeaderValue)
{
	FIoHttpHeaders Headers;
	Headers.Add(HeaderName, HeaderValue);
	return Headers;
}

FIoHttpHeaders FIoHttpHeaders::Create(TArray<FAnsiString>&& Headers)
{
	check(Headers.IsEmpty() || ((Headers.Num() % 2) == 0));
	return FIoHttpHeaders(MoveTemp(Headers));
}

////////////////////////////////////////////////////////////////////////////////
FIoRelativeUrl FIoRelativeUrl::From(FAnsiStringView Url)
{
	if (Url.IsEmpty())
	{
		return FIoRelativeUrl();
	}

	if (Url[0] != ANSICHAR('/'))
	{
		TAnsiStringBuilder<128> Sb;
		Sb << "/" << Url;
		return FIoRelativeUrl(Sb.ToString());
	}

	return FIoRelativeUrl(Url);
}

////////////////////////////////////////////////////////////////////////////////
FIoHttpRequest::~FIoHttpRequest()
{
	Release();
}

void FIoHttpRequest::Cancel()
{
	if (Handle != 0)
	{
		return GHttpIoDispatcher->CancelRequest(Handle);
	}
}

void FIoHttpRequest::UpdatePriorty(int32 NewPriority)
{
	if (Handle != 0)
	{
		return GHttpIoDispatcher->UpdateRequestPriority(Handle, NewPriority);
	}
}

EIoErrorCode FIoHttpRequest::Status() const
{
	if (Handle != 0)
	{
		return GHttpIoDispatcher->GetRequestStatus(Handle);
	}

	return EIoErrorCode::InvalidCode;
}

void FIoHttpRequest::Release()
{
	if (Handle != 0)
	{
		GHttpIoDispatcher->ReleaseRequest(Handle);
		Handle = 0;
	}
}

FIoHttpRequest&	FIoHttpRequest::operator=(FIoHttpRequest&& Other)
{
	Release();
	Handle = Other.Handle;
	Other.Handle = 0;
	return *this;
}

////////////////////////////////////////////////////////////////////////////////
FIoHttpBatch::~FIoHttpBatch()
{
	if (First != 0)
	{
		ensure(Last != 0);
		GHttpIoDispatcher->ReleaseRequest(First);
		First = Last = 0;
	}
}

FIoHttpRequest FIoHttpBatch::Get(
	const FName& HostGroup,
	const FIoRelativeUrl& RelativeUrl,
	FIoHttpHeaders&& Headers,
	const FIoHttpOptions& Options,
	const FIoHash& ChunkHash,
	FIoHttpRequestCompleted&& OnCompleted)
{
	FIoHttpRequestHandle Handle = GHttpIoDispatcher->CreateRequest(
		First,
		Last,
		HostGroup,
		RelativeUrl,
		Options,
		MoveTemp(Headers),
		MoveTemp(OnCompleted),
		&ChunkHash);

	return FIoHttpRequest(Handle);
}

FIoHttpRequest FIoHttpBatch::Get(
	const FName& HostGroup,
	const FIoRelativeUrl& RelativeUrl,
	FIoHttpHeaders&& Headers,
	const FIoHttpOptions& Options,
	FIoHttpRequestCompleted&& OnCompleted)
{
	FIoHttpRequestHandle Handle = GHttpIoDispatcher->CreateRequest(
		First,
		Last,
		HostGroup,
		RelativeUrl,
		Options,
		MoveTemp(Headers),
		MoveTemp(OnCompleted));

	return FIoHttpRequest(Handle);
}

FIoHttpRequest FIoHttpBatch::Get(
	const FName& HostGroup,
	const FIoRelativeUrl& RelativeUrl,
	FIoHttpHeaders&& Headers,
	FIoHttpRequestCompleted&& OnCompleted)
{
	return Get(HostGroup, RelativeUrl, MoveTemp(Headers), FIoHttpOptions::Default, MoveTemp(OnCompleted));
}

FIoHttpRequest FIoHttpBatch::Get(
	const FName& HostGroup,
	const FIoRelativeUrl& RelativeUrl,
	FIoHttpRequestCompleted&& OnCompleted)
{
	return Get(HostGroup, RelativeUrl, FIoHttpHeaders(), FIoHttpOptions::Default, MoveTemp(OnCompleted));
}

void FIoHttpBatch::Issue()
{
	if (First != 0)
	{
		ensure(Last != 0);
		GHttpIoDispatcher->IssueRequest(First);
		First = Last = 0;
	}
}

////////////////////////////////////////////////////////////////////////////////
bool FHttpIoDispatcher::IsInitialized()
{
	return GHttpIoDispatcher.IsValid();
}

FIoStatus FHttpIoDispatcher::Initialize(TSharedPtr<IHttpIoDispatcher> Dispatcher)
{
	if (GHttpIoDispatcher.IsValid())
	{
		return FIoStatus(EIoErrorCode::InvalidCode);
	}

	UE_LOG(LogHttpIoDispatcher, Log, TEXT("Initializing HTTP I/O dispatcher"));
	GHttpIoDispatcher = Dispatcher;

	return FIoStatus::Ok;
}

FIoStatus FHttpIoDispatcher::Shutdown()
{
	if (GHttpIoDispatcher.IsValid() == false)
	{
		return FIoStatus::Invalid;
	}

	UE_LOG(LogHttpIoDispatcher, Log, TEXT("Shutting down HTTP I/O dispatcher"));
	GHttpIoDispatcher->Shutdown();
	GHttpIoDispatcher.Reset();

	return FIoStatus::Ok;
}

FIoStatus FHttpIoDispatcher::RegisterHostGroup(const FName& HostGroup, TConstArrayView<FAnsiString> HostNames, FAnsiStringView TestUrl)
{
	if (GHttpIoDispatcher.IsValid())
	{
		return GHttpIoDispatcher->RegisterHostGroup(HostGroup, HostNames, TestUrl);
	}

	return FIoStatus(EIoErrorCode::InvalidCode);
}

FIoStatus FHttpIoDispatcher::RegisterHostGroup(const FName& HostGroup, FAnsiStringView HostName, FAnsiStringView TestUrl)
{
	FAnsiString HostNameString(HostName);
	TConstArrayView<FAnsiString> HostNames(&HostNameString, 1);
	return RegisterHostGroup(HostGroup, HostNames, TestUrl);
}

bool FHttpIoDispatcher::IsHostGroupRegistered(const FName& HostGroup)
{
	return GHttpIoDispatcher.IsValid() ? GHttpIoDispatcher->IsHostGroupRegistered(HostGroup) : false;
}

bool FHttpIoDispatcher::IsHostGroupOk(const FName& HostGroup)
{
	return GHttpIoDispatcher.IsValid() ? GHttpIoDispatcher->IsHostGroupOk(HostGroup) : false;
}

FIoHttpBatch FHttpIoDispatcher::NewBatch()
{
	ensure(GHttpIoDispatcher.IsValid());
	return FIoHttpBatch();
}

FIoHttpRequest FHttpIoDispatcher::Get(
	const FName& HostGroup,
	const FIoRelativeUrl& RelativeUrl,
	FIoHttpHeaders&& Headers,
	const FIoHttpOptions& Options,
	FIoHttpRequestCompleted&& OnCompleted)
{
	ensure(GHttpIoDispatcher.IsValid());

	FIoHttpRequestHandle First = 0;
	FIoHttpRequestHandle Last = 0;

	FIoHttpRequestHandle Handle = GHttpIoDispatcher->CreateRequest(
		First,
		Last,
		HostGroup,
		RelativeUrl,
		Options,
		MoveTemp(Headers),
		MoveTemp(OnCompleted));

	GHttpIoDispatcher->IssueRequest(First);
	return FIoHttpRequest(First);
}

FIoHttpRequest FHttpIoDispatcher::Get(
	const FName& HostGroup,
	const FIoRelativeUrl& RelativeUrl,
	FIoHttpHeaders&& Headers,
	FIoHttpRequestCompleted&& OnCompleted)
{
	return Get(HostGroup, RelativeUrl, MoveTemp(Headers), FIoHttpOptions::Default, MoveTemp(OnCompleted));
}

FIoHttpRequest FHttpIoDispatcher::Get(
	const FName& HostGroup,
	const FIoRelativeUrl& RelativeUrl,
	FIoHttpRequestCompleted&& OnCompleted)
{
	return Get(HostGroup, RelativeUrl, FIoHttpHeaders(), FIoHttpOptions::Default, MoveTemp(OnCompleted));
}
 FIoStatus FHttpIoDispatcher::CacheResponse(const FIoHttpResponse& Response)
{
	 ensure(GHttpIoDispatcher.IsValid());
	 return GHttpIoDispatcher->CacheResponse(Response);
}

FIoStatus FHttpIoDispatcher::EvictFromCache(const FIoHttpResponse& Response)
{
	ensure(GHttpIoDispatcher.IsValid());
	return GHttpIoDispatcher->EvictFromCache(Response);
}

FHttpIoDispatcher::FHostGroupRegistered& FHttpIoDispatcher::OnHostGroupRegistered()
{
	ensure(GHttpIoDispatcher.IsValid());
	return GHttpIoDispatcher->OnHostGroupRegistered();
}

} // namespace UE
