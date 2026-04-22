// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/AnsiString.h"
#include "Containers/SharedString.h"
#include "Delegates/Delegate.h"
#include "IO/IoBuffer.h"
#include "IO/IoHash.h"
#include "IO/IoOffsetLength.h"
#include "IO/IoStatus.h"
#include "Misc/EnumClassFlags.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"

#define UE_API IOSTOREONDEMANDCORE_API

UE_API DECLARE_LOG_CATEGORY_EXTERN(LogHttpIoDispatcher, Log, All);

namespace UE
{

/** Internal request handle type. */
using FIoHttpRequestHandle = UPTRINT;

/** Flags for controlling the behavior of an HTTP request. */
enum class EIoHttpFlags
{
	/** No additional flags. */
	None			= 0,
	/** Whether to read from the HTTP cache. */
	ReadCache		= (1 << 0),
	/** Whether to include response headers. */
	ResponseHeaders	= (1 << 1),
	/** Default flags. */
	Default			= ReadCache
};
ENUM_CLASS_FLAGS(EIoHttpFlags);

/** Represents a range within a resource. */
struct FIoHttpRange
{
								FIoHttpRange() = default;
	explicit inline				FIoHttpRange(uint32 InMin, uint32 InMax);

	bool						IsValid()	const	{ return Min <= Max; }
	uint32						GetMin()	const	{ return Min; }
	uint32						GetMax()	const	{ return Max; }
	uint32						GetSize()	const	{ return (Max - Min); }
	inline FIoHttpRange&		Expand(const FIoHttpRange& Other);

	inline FIoOffsetAndLength	ToOffsetAndLength() const;
	static inline FIoHttpRange	FromOffsetAndLength(const FIoOffsetAndLength& OffsetAndLength);

	bool						operator==(const FIoHttpRange& Other)	{ return Min == Other.Min && Max == Other.Max; }
	bool						operator!=(const FIoHttpRange& Other)	{ return Min != Other.Min || Max != Other.Max; }
	FIoHttpRange&				operator+(const FIoHttpRange& Other)	{ return Expand(Other); }
	FIoHttpRange&				operator+=(const FIoHttpRange& Other)	{ return Expand(Other); }

private:
	uint32 Min = MAX_uint32;
	uint32 Max = MIN_uint32;
};

FIoHttpRange::FIoHttpRange(uint32 InMin, uint32 InMax)
	: Min(InMin)
	, Max(InMax)
{
}

FIoHttpRange& FIoHttpRange::Expand(const FIoHttpRange& Other)
{
	if (IsValid())
	{
		Min = FMath::Min(Min, Other.Min);
		Max = FMath::Max(Max, Other.Max);
	}
	else
	{
		Min = Other.Min;
		Max = Other.Max;
	}

	return *this;
}

FIoOffsetAndLength FIoHttpRange::ToOffsetAndLength() const
{
	return IsValid() ? FIoOffsetAndLength(Min, Max - Min) : FIoOffsetAndLength(0, 0);
}

FIoHttpRange FIoHttpRange::FromOffsetAndLength(const FIoOffsetAndLength& OffsetAndLength)
{
	const uint64 End = OffsetAndLength.GetOffset() + OffsetAndLength.GetLength();

	if (OffsetAndLength.GetOffset() > MAX_uint32 || End > MAX_uint32)
	{
		return FIoHttpRange();
	}

	return FIoHttpRange(IntCastChecked<uint32>(OffsetAndLength.GetOffset()), IntCastChecked<uint32>(End));
}

/** Options for controlling the behavior of an HTTP request. */
struct FIoHttpOptions
{
public:
	inline						FIoHttpOptions(int32 InPriority, int32 InRetryCount, EIoHttpFlags InFlags, const FIoHttpRange& InRange);
	inline						FIoHttpOptions(int32 InPriority, int32 InRetryCount, EIoHttpFlags InFlags);
	inline						FIoHttpOptions(int32 InPriority, int32 InRetryCount);
	inline explicit				FIoHttpOptions(int32 InPriority);
								FIoHttpOptions() = default;

	const FIoHttpRange&			GetRange()		const { return Range; }
	int32						GetPriority()	const { return Priority; }
	int32						GetRetryCount()	const { return RetryCount; }
	EIoHttpFlags				GetFlags()		const { return Flags; }
	uint8						GetCategory()	const { return Category; }

	void						SetRange(const FIoHttpRange InRange)	{ Range = InRange; }
	void						SetPriority(int32 InPriority)			{ Priority = InPriority; }
	void						SetRetryCount(int32 InRetryCount)		{ RetryCount = InRetryCount; }
	void						SetCategory(uint8 InCategory)			{ Category = InCategory; }

	UE_API static const FIoHttpOptions Default;

private:
	FIoHttpRange	Range;
	int32			Priority = 0;
	int32			RetryCount = 0;
	EIoHttpFlags	Flags = EIoHttpFlags::Default;
	uint8			Category = 0;
};

FIoHttpOptions::FIoHttpOptions(int32 InPriority, int32 InRetryCount, EIoHttpFlags InFlags, const FIoHttpRange& InRange)
	: Range(InRange)
	, Priority(InPriority)
	, RetryCount(InRetryCount)
	, Flags(InFlags)
{
}

FIoHttpOptions::FIoHttpOptions(int32 InPriority, int32 InRetryCount, EIoHttpFlags InFlags)
	: Priority(InPriority)
	, RetryCount(InRetryCount)
	, Flags(InFlags)
{
}

FIoHttpOptions::FIoHttpOptions(int32 InPriority, int32 InRetryCount)
	: Priority(InPriority)
	, RetryCount(InRetryCount)
{
}

FIoHttpOptions::FIoHttpOptions(int32 InPriority)
	: Priority(InPriority)
{
}

/** HTTP headers. */
class FIoHttpHeaders
{
public:
									FIoHttpHeaders() = default;
									FIoHttpHeaders(const FIoHttpHeaders&) = default;
									FIoHttpHeaders(FIoHttpHeaders&&) = default;
	UE_API FIoHttpHeaders&			Add(FAnsiString&& HeaderName, FAnsiString&& HeaderValue);
	UE_API FIoHttpHeaders&			Add(FAnsiStringView HeaderName, FAnsiStringView HeaderValue);
	UE_API FAnsiStringView			Get(FAnsiStringView Key) const;
	UE_API FAnsiStringView			Get(const FAnsiString& Key) const;
	UE_API TArray<FAnsiString>		ToArray() &&;
	TConstArrayView<FAnsiString>	ToArrayView() const { return Headers; }

	UE_API static FIoHttpHeaders	Create(FAnsiString&& HeaderName, FAnsiString&& HeaderValue);
	UE_API static FIoHttpHeaders	Create(FAnsiStringView HeaderName, FAnsiStringView HeaderValue);
	UE_API static FIoHttpHeaders	Create(TArray<FAnsiString>&& Headers);
	
	FIoHttpHeaders&					operator=(const FIoHttpHeaders& Other)	{ Headers = Other.Headers; return *this; }
	FIoHttpHeaders&					operator=(FIoHttpHeaders&& Other)		{ Headers = MoveTemp(Other.Headers); return *this; }

private:
									FIoHttpHeaders(TArray<FAnsiString>&& InHeaders)
										: Headers(MoveTemp(InHeaders))
									{ }
	//TODO: Make this better
	TArray<FAnsiString> Headers;
};

/** A relative URL from the host. */
class FIoRelativeUrl
{
public:
									using ElementType = ANSICHAR;

									FIoRelativeUrl() = default;
	bool							IsEmpty() const		{ return Url.IsEmpty(); }
	int32							Len() const			{ return Url.Len(); }
	const ANSICHAR*					ToString() const	{ return *Url; }
	FAnsiStringView					GetView() const		{ return FAnsiStringView(*Url, Url.Len()); }

	const ANSICHAR*					operator*() const	{ return *Url; }
	friend inline bool				operator==(const FIoRelativeUrl& Lhs, const FIoRelativeUrl& Rhs)	{ return Lhs.Url == Rhs.Url; }
	friend inline bool				operator!=(const FIoRelativeUrl& Lhs, const FIoRelativeUrl& Rhs)	{ return Lhs.Url != Rhs.Url; }
	friend inline bool				operator<(const FIoRelativeUrl& Lhs, const FIoRelativeUrl& Rhs)		{ return Lhs.Url < Rhs.Url; }

	friend inline uint32			GetTypeHash(const FIoRelativeUrl& RelativeUrl) { return GetTypeHash(RelativeUrl.Url); }
	UE_API static FIoRelativeUrl	From(FAnsiStringView Url); 

private:
	explicit FIoRelativeUrl(FAnsiStringView InUrl)
		: Url(InUrl) { }

	TSharedString<ElementType> Url;
};

/**
 * An HTTP request handle.
 *
 * The handle needs to be keept alive until the completion callback has been triggered.
 */
class FIoHttpRequest
{
public:
							FIoHttpRequest() = default;
	explicit 				FIoHttpRequest(FIoHttpRequestHandle InHandle)
								: Handle(InHandle) { }
							FIoHttpRequest(FIoHttpRequest&& Other)
								: Handle(Other.Handle) { Other.Handle = 0; }
							FIoHttpRequest(const FIoHttpRequest&) = delete;
	UE_API					~FIoHttpRequest();

	bool					IsValid() const { return Handle != 0; }
	UE_API void				Cancel();
	UE_API void				UpdatePriorty(int32 NewPriority);
	UE_API EIoErrorCode		Status() const;

	FIoHttpRequest&			operator=(const FIoHttpRequest&) = delete;
	UE_API FIoHttpRequest&	operator=(FIoHttpRequest&& Other);

private:
	void					Release();
	FIoHttpRequestHandle	Handle = 0;
};

/** Flags describing a HTTP response. */
enum class EIoHttpResponseFlags : uint8
{
	/** No additional flags. */
	None			= 0,
	/** The response was retrieved from the cache. */ 
	Cached			= (1 << 0),
};
ENUM_CLASS_FLAGS(EIoHttpResponseFlags);

/** A HTTP response. */
class FIoHttpResponse
{
public:
							FIoHttpResponse() = default;
	inline					FIoHttpResponse(const FIoHash& InCacheKey, const FIoBuffer& InBody, EIoErrorCode InErrorCode, uint32 InStatusCode, EIoHttpResponseFlags InFlags);
	inline					FIoHttpResponse(const FIoHash& InCacheKey, FIoHttpHeaders&& InHeaders, const FIoBuffer& InBody, EIoErrorCode InErrorCode, uint32 InStatusCode, EIoHttpResponseFlags InFlags);
							FIoHttpResponse(EIoErrorCode InErrorCode, uint32 InStatusCode)
								: ErrorCode(InErrorCode)
								, StatusCode(InStatusCode) { }

	const FIoHttpHeaders&	GetHeaders()	const { return Headers; }
	const FIoBuffer&		GetBody()		const { return Body; }
	const FIoHash&			GetCacheKey()	const { return CacheKey; }
	EIoErrorCode			GetErrorCode()	const { return ErrorCode; }
	uint32					GetStatusCode()	const { return StatusCode; }
	EIoHttpResponseFlags	GetFlags()		const { return Flags; }
	bool					IsOk()			const { return ErrorCode == EIoErrorCode::Ok && StatusCode > 199 && StatusCode < 300; }
	bool					IsCancelled()	const { return ErrorCode == EIoErrorCode::Cancelled; }
	bool					IsCached()		const { return EnumHasAnyFlags(Flags, EIoHttpResponseFlags::Cached); } 

private:
	FIoHash					CacheKey;
	FIoHttpHeaders			Headers;
	FIoBuffer				Body;
	EIoErrorCode			ErrorCode;
	uint32					StatusCode = 0;
	EIoHttpResponseFlags	Flags = EIoHttpResponseFlags::None;
};

FIoHttpResponse::FIoHttpResponse(const FIoHash& InCacheKey, const FIoBuffer& InBody, EIoErrorCode InErrorCode, uint32 InStatusCode, EIoHttpResponseFlags InFlags)
	: CacheKey(InCacheKey)
	, Body(InBody)
	, ErrorCode(InErrorCode)
	, StatusCode(InStatusCode)
	, Flags(InFlags)
{
}

FIoHttpResponse::FIoHttpResponse(const FIoHash& InCacheKey, FIoHttpHeaders&& InHeaders, const FIoBuffer& InBody, EIoErrorCode InErrorCode, uint32 InStatusCode, EIoHttpResponseFlags InFlags)
	: CacheKey(InCacheKey)
	, Headers(MoveTemp(InHeaders))
	, Body(InBody)
	, ErrorCode(InErrorCode)
	, StatusCode(InStatusCode)
	, Flags(InFlags)
{
}

/** HTTP completion callback. */
using FIoHttpRequestCompleted = TUniqueFunction<void(FIoHttpResponse&&)>;

/** Issue one or more HTTP request in a batch. */
class FIoHttpBatch
{
public:
							FIoHttpBatch(const FIoHttpRequest&) = delete;
							FIoHttpBatch(FIoHttpBatch&& Other)
								: First(Other.First), Last(Other.Last)
							{
								Other.First = 0;
								Other.Last = 0;
							}

	UE_API					~FIoHttpBatch();

	UE_API FIoHttpRequest	Get(const FName& HostGroup,
								const FIoRelativeUrl& RelativeUrl,
								FIoHttpHeaders&& Headers,
								const FIoHttpOptions& Options,
								const FIoHash& ChunkHash,
								FIoHttpRequestCompleted&& OnCompleted);
	UE_API FIoHttpRequest	Get(const FName& HostGroup,
								const FIoRelativeUrl& RelativeUrl,
								FIoHttpHeaders&& Headers,
								const FIoHttpOptions& Options,
								FIoHttpRequestCompleted&& OnCompleted);
	UE_API FIoHttpRequest	Get(const FName& HostGroup,
								const FIoRelativeUrl& RelativeUrl,
								FIoHttpHeaders&& Headers,
								FIoHttpRequestCompleted&& OnCompleted);
	UE_API FIoHttpRequest	Get(const FName& HostGroup,
								const FIoRelativeUrl& RelativeUrl,
								FIoHttpRequestCompleted&& OnCompleted);
	UE_API void				Issue();

	FIoHttpBatch&			operator=(const FIoHttpBatch&) = delete;
	FIoHttpBatch&			operator=(FIoHttpBatch&& Other) = delete;

private:
							friend class FHttpIoDispatcher;
							FIoHttpBatch() = default;
	FIoHttpRequestHandle	First = 0;
	FIoHttpRequestHandle 	Last = 0;
};

/** HTTP I/O dispatcher . */
class FHttpIoDispatcher
{
public:
	UE_API static bool				IsInitialized();
	UE_API static FIoStatus			Initialize(TSharedPtr<class IHttpIoDispatcher> Dispatcher);
	UE_API static FIoStatus			Shutdown();

	UE_API static FIoStatus			RegisterHostGroup(const FName& HostGroup, TConstArrayView<FAnsiString> HostNames, FAnsiStringView TestUrl);
	UE_API static FIoStatus			RegisterHostGroup(const FName& HostGroup, FAnsiStringView HostName, FAnsiStringView TestUrl);
	UE_API static bool				IsHostGroupRegistered(const FName& HostGroup);
	UE_API static bool				IsHostGroupOk(const FName& HostGroup);

	UE_API static FIoHttpBatch		NewBatch();
	UE_API static FIoHttpRequest	Get(const FName& HostGroup,
										const FIoRelativeUrl& RelativeUrl,
										FIoHttpHeaders&& Headers,
										const FIoHttpOptions& Options,
										FIoHttpRequestCompleted&& OnCompleted);
	UE_API static FIoHttpRequest	Get(const FName& HostGroup,
										const FIoRelativeUrl& RelativeUrl,
										FIoHttpHeaders&& Headers,
										FIoHttpRequestCompleted&& OnCompleted);
	UE_API static FIoHttpRequest	Get(const FName& HostGroup,
										const FIoRelativeUrl& RelativeUrl,
										FIoHttpRequestCompleted&& OnCompleted);

	/** */
	UE_API static FIoStatus			CacheResponse(const FIoHttpResponse& Response);
	UE_API static FIoStatus			EvictFromCache(const FIoHttpResponse& Response);

	DECLARE_MULTICAST_DELEGATE_OneParam(FHostGroupRegistered, const FName&);
	UE_API static FHostGroupRegistered& OnHostGroupRegistered();
};

/** HTTP I/O dispatcher interface. */
class IHttpIoDispatcher
{
public:
	using FHostGroupRegistered		= FHttpIoDispatcher::FHostGroupRegistered;

	virtual							~IHttpIoDispatcher() = default;
	virtual void					Shutdown() = 0;
	
	virtual FIoStatus				RegisterHostGroup(const FName& HostGroup, TConstArrayView<FAnsiString> HostNames, FAnsiStringView TestUrl) = 0;
	virtual bool					IsHostGroupRegistered(const FName& HostGroup) = 0;
	virtual bool					IsHostGroupOk(const FName& HostGroup) = 0;
	virtual FHostGroupRegistered&	OnHostGroupRegistered() = 0;

	virtual FIoHttpRequestHandle	CreateRequest(
										FIoHttpRequestHandle& First,
										FIoHttpRequestHandle& Last,
										const FName& HostGroup,
										const FIoRelativeUrl& RelativeUrl,
										const FIoHttpOptions& Options,
										FIoHttpHeaders&& Headers,
										FIoHttpRequestCompleted&& OnCompleted,
										const FIoHash* ChunkHash = nullptr) = 0;
	virtual void					IssueRequest(FIoHttpRequestHandle RequestHandle) = 0;
	virtual void					CancelRequest(FIoHttpRequestHandle Handle) = 0;
	virtual void					UpdateRequestPriority(FIoHttpRequestHandle Handle, int32 NewPriority) = 0;
	virtual EIoErrorCode			GetRequestStatus(FIoHttpRequestHandle Handle) = 0;
	virtual void					ReleaseRequest(FIoHttpRequestHandle Handle) = 0;

	virtual FIoStatus				CacheResponse(const FIoHttpResponse& Response) = 0;
	virtual FIoStatus				EvictFromCache(const FIoHttpResponse& Response) = 0;
};

} // namespace UE

#undef UE_API
