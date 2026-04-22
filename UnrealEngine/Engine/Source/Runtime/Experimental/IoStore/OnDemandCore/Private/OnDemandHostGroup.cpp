// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/OnDemandHostGroup.h"

#include "Containers/AnsiString.h"
#include "Containers/Array.h"
#include "Containers/StringView.h"
#include "HAL/IConsoleManager.h"

#if !UE_BUILD_SHIPPING
#include "IO/IoStoreOnDemand.h"
#include "Internationalization/Regex.h"
#include "Logging/StructuredLog.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#endif // !UE_BUILD_SHIPPING

namespace UE::IoStore
{

namespace Private
{

static bool ValidateUrl(FAnsiStringView Url, FString& Reason)
{
	//TODO: Add better validation
	return Url.StartsWith("http") || Url.StartsWith("https");
}

#if !UE_BUILD_SHIPPING

static bool ShouldSkipUrl(const FString& Url)
{
	struct FDevCDNCheck
	{
		FDevCDNCheck()
		{
			if (FParse::Param(FCommandLine::Get(), TEXT("Ias.SkipDevCDNs")))
			{
				FString ConfigRegex;
				GConfig->GetString(TEXT("Ias"), TEXT("DevelopmentCDNPattern"), ConfigRegex, GEngineIni);

				if (!ConfigRegex.IsEmpty())
				{
					Pattern = MakeUnique<FRegexPattern>(MoveTemp(ConfigRegex));
				}
			}
		}

		bool IsDevCNDUrl(const FString& Url) const
		{
			if (Pattern == nullptr)
			{
				return false;
			}
			else
			{
				FRegexMatcher Regex = FRegexMatcher(*Pattern, Url);
				return Regex.FindNext();
			}
		}
	private:

		TUniquePtr<FRegexPattern> Pattern;
	} static DevCDNCheck;

	return DevCDNCheck.IsDevCNDUrl(Url);
}

static bool ShouldSkipUrl(const FStringView& Url)
{
	return ShouldSkipUrl(FString(Url));
}

static bool ShouldSkipUrl(FAnsiStringView Url)
{
	return ShouldSkipUrl(FString(Url));
}

#endif // !UE_BUILD_SHIPPING

} // namespace Private

struct FOnDemandHostGroup::FImpl
{
	TArray<FAnsiString> HostUrls;
	int32				PrimaryIndex = INDEX_NONE;
};

FOnDemandHostGroup::FOnDemandHostGroup()
	: Impl(MakeShared<FImpl>())
{
}

FOnDemandHostGroup::FOnDemandHostGroup(FOnDemandHostGroup::FSharedImpl&& InImpl)
	: Impl(MoveTemp(InImpl))
{
}

FOnDemandHostGroup::~FOnDemandHostGroup()
{
}

TConstArrayView<FAnsiString> FOnDemandHostGroup::Hosts() const
{
	return Impl->HostUrls;
}

FAnsiStringView FOnDemandHostGroup::Host(int32 Index) const
{
	return Impl->HostUrls.IsEmpty() ? FAnsiStringView() : Impl->HostUrls[Index];
}

FAnsiStringView FOnDemandHostGroup::CycleHost(int32& InOutIndex) const
{
	InOutIndex = (InOutIndex + 1) % Impl->HostUrls.Num();
	return Host(InOutIndex);
}

void FOnDemandHostGroup::SetPrimaryHost(int32 Index)
{
	check(Index >= 0 && Index < Impl->HostUrls.Num() || Index == INDEX_NONE);
	Impl->PrimaryIndex = Index;
}

FAnsiStringView FOnDemandHostGroup::PrimaryHost() const
{
	if (Impl->PrimaryIndex != INDEX_NONE)
	{
		check(Impl->PrimaryIndex >= 0 && Impl->PrimaryIndex < Impl->HostUrls.Num());
		return Impl->HostUrls[Impl->PrimaryIndex];
	}

	return FAnsiStringView();
}

int32 FOnDemandHostGroup::PrimaryHostIndex() const
{
	return Impl->PrimaryIndex;
}

bool FOnDemandHostGroup::IsEmpty() const
{
	return Impl->HostUrls.IsEmpty();
}

TIoStatusOr<FOnDemandHostGroup> FOnDemandHostGroup::Create(FAnsiStringView Url)
{
	if (Url.EndsWith('/'))
	{
		Url.RemoveSuffix(1);
	}

	FString Reason;
	if (Private::ValidateUrl(Url, Reason) == false)
	{
		FIoStatus(EIoErrorCode::InvalidParameter, Reason);
	}

	FSharedImpl Impl = MakeShared<FImpl>();
	Impl->HostUrls.Emplace(Url);
	Impl->PrimaryIndex = 0;

	return FOnDemandHostGroup(MoveTemp(Impl)); 
}

TIoStatusOr<FOnDemandHostGroup> FOnDemandHostGroup::Create(FStringView Url)
{
	return Create(FAnsiString(StringCast<ANSICHAR>(Url.GetData(), Url.Len()))); 
}

TIoStatusOr<FOnDemandHostGroup> FOnDemandHostGroup::Create(TConstArrayView<FAnsiString> Urls)
{
	if (Urls.IsEmpty())
	{
		return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("No URLs specified"));
	}

	FSharedImpl Impl = MakeShared<FImpl>();
	FString		Reason;

	for (const FAnsiString& Url : Urls)
	{
		FAnsiStringView UrlView = Url;

		if (UrlView.EndsWith('/'))
		{
			UrlView.RemoveSuffix(1);
		}

		if (Private::ValidateUrl(UrlView, Reason) == false)
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, Reason);
		}

#if !UE_BUILD_SHIPPING
		if (Private::ShouldSkipUrl(UrlView))
		{
			UE_LOGFMT(LogIas, Log, "Skipping development CDN '{Url}' when creating HostGroup", UrlView);
			continue;
		}
#endif // !UE_BUILD_SHIPPING

		Impl->HostUrls.Emplace(Url);
	}

	Impl->PrimaryIndex = 0; 

	return FOnDemandHostGroup(MoveTemp(Impl)); 
}

TIoStatusOr<FOnDemandHostGroup> FOnDemandHostGroup::Create(TConstArrayView<FString> Urls)
{
	if (Urls.IsEmpty())
	{
		return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("No URLs specified"));
	}

	FSharedImpl Impl = MakeShared<FImpl>();
	FString		Reason;

	for (const FString& Url : Urls)
	{
		FStringView UrlView = Url;

		if (UrlView.EndsWith(TEXT('/')))
		{
			UrlView.RemoveSuffix(1);
		}

		FAnsiString AnsiUrl(StringCast<ANSICHAR>(UrlView.GetData(), UrlView.Len()));

		if (Private::ValidateUrl(AnsiUrl, Reason) == false)
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, Reason);
		}

#if !UE_BUILD_SHIPPING
		if (Private::ShouldSkipUrl(UrlView))
		{
			UE_LOGFMT(LogIas, Log, "Skipping development CDN '{Url}' when creating HostGroup", UrlView);
			continue;
		}
#endif // !UE_BUILD_SHIPPING

		Impl->HostUrls.Emplace(MoveTemp(AnsiUrl));
	}
	Impl->PrimaryIndex = 0;

	return FOnDemandHostGroup(MoveTemp(Impl));
}

FName FOnDemandHostGroup::DefaultName = FName("Default");

} //namespace UE::IoStore
