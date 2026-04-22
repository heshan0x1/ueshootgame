// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cloud/MetaHumanTDSUtils.h"

#include "HttpModule.h"
#include "HttpManager.h"
#include "PlatformHttp.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "Logging/StructuredLog.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogMetaHumanTDSUtils);

namespace UE::MetaHuman::TDSUtils
{

namespace Private
{
	const TSharedRef<IHttpRequest> CreateGetRequest(const FString& InUrl)
	{
		const TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
		Request->SetTimeout(30.f);
		Request->SetVerb(TEXT("GET"));
		Request->SetHeader(TEXT("Content-Type"), TEXT("application/json; charset=utf-8"));
		Request->SetURL(InUrl);

		return Request;
	}
}

TFuture<FTdsAcquireAccountResult> AcquireAccount(const FString& InTemplateHost, const FString& InTemplateName)
{
	if (InTemplateHost.IsEmpty() || InTemplateName.IsEmpty())
	{
		return {};
	}

	TSharedPtr<TPromise<FTdsAcquireAccountResult>> Promise = MakeShared<TPromise<FTdsAcquireAccountResult>>();

	const TSharedRef<IHttpRequest> HttpRequest = Private::CreateGetRequest
	(
		InTemplateHost / FString::Format(TEXT("/v1/pool/setAccountInUse?templateName={0}"),
		{
			FPlatformHttp::UrlEncode(InTemplateName)
		})
	);

	HttpRequest->OnProcessRequestComplete().BindLambda([Promise](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
	{
		if (!bSuccess || !Response.IsValid())
		{
			Promise->SetValue(MakeError(TEXT("Invalid response")));
			return;
		}

		if (!EHttpResponseCodes::IsOk(Response->GetResponseCode()))
		{
			Promise->SetValue(MakeError(FString::Format(TEXT("Invalid response code, got {0}"), { Response->GetResponseCode() })));
			return;
		}

		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(Response->GetContentAsString());
		TSharedPtr<FJsonObject> ResponseJson;

		if (!FJsonSerializer::Deserialize(JsonReader, ResponseJson))
		{
			Promise->SetValue(MakeError(TEXT("Unable to parse response")));
			return;
		}

		FMetaHumanTDSAcquiredAccountInfo AccountInfo;

		// Retrieve Epic Account ID from the response
		const TArray<TSharedPtr<FJsonValue>>* ResultsJsonArray;

		if (!ResponseJson->TryGetArrayField(TEXT("results"), ResultsJsonArray))
		{
			Promise->SetValue(MakeError(TEXT("Missing 'results'")));
			return;
		}

		if (ResultsJsonArray->Num() != 1)
		{
			Promise->SetValue(MakeError(TEXT("Expected exactly one object in the 'results' response")));
			return;
		}

		const TSharedPtr<FJsonObject>& ResultObject = ResultsJsonArray->operator[](0)->AsObject();

		FString AccessToken;

		// Only verify that access token actually exists, no need to record it
		if (!ResultObject->TryGetStringField(TEXT("accessToken"), AccessToken))
		{
			Promise->SetValue(MakeError(TEXT("Missing `accessToken`")));
			return;
		}

		const TSharedPtr<FJsonObject>* EpicAccountInfoObject;

		if (!ResultObject->TryGetObjectField(TEXT("epicAccountInfo"), EpicAccountInfoObject))
		{
			Promise->SetValue(MakeError(TEXT("Missing `epicAccountInfo`")));
			return;
		}

		if (!(*EpicAccountInfoObject)->TryGetStringField(TEXT("epicAccountId"), AccountInfo.EpicAccountId))
		{
			Promise->SetValue(MakeError(TEXT("Missing `epicAccountId`")));
			return;
		}

		if (!(*EpicAccountInfoObject)->TryGetStringField(TEXT("email"), AccountInfo.Email))
		{
			Promise->SetValue(MakeError(TEXT("Missing `email`")));
			return;
		}

		if (!(*EpicAccountInfoObject)->TryGetStringField(TEXT("password"), AccountInfo.Password))
		{
			Promise->SetValue(MakeError(TEXT("Missing `password`")));
			return;
		}

		Promise->SetValue(MakeValue(AccountInfo));
	});
	HttpRequest->ProcessRequest();

	return Promise->GetFuture();
}

TFuture<FTdsGetExchangeCodeResult> GetExchangeCode(const FString& InTemplateHost, const FString& InAccountId, const FString& InClientId)
{
	if (InTemplateHost.IsEmpty() || InAccountId.IsEmpty() || InClientId.IsEmpty())
	{
		return {};
	}

	TSharedPtr<TPromise<FTdsGetExchangeCodeResult>> Promise = MakeShared<TPromise<FTdsGetExchangeCodeResult>>();

	const TSharedRef<IHttpRequest> HttpRequest = Private::CreateGetRequest
	(
		InTemplateHost / FString::Format(TEXT("/v1/account/{0}/exchange_code?consumingClientId={1}"),
		{
			FPlatformHttp::UrlEncode(InAccountId),
			FPlatformHttp::UrlEncode(InClientId)
		})
	);

	HttpRequest->OnProcessRequestComplete().BindLambda([Promise](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
	{
		if (!bSuccess || !Response.IsValid())
		{
			Promise->SetValue(MakeError(TEXT("Invalid response")));
			return;
		}

		if (!EHttpResponseCodes::IsOk(Response->GetResponseCode()))
		{
			Promise->SetValue(MakeError(FString::Format(TEXT("Invalid response code, got {0}"), { Response->GetResponseCode() })));
			return;
		}

		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(Response->GetContentAsString());
		TSharedPtr<FJsonObject> ResponseJson;

		if (!FJsonSerializer::Deserialize(JsonReader, ResponseJson))
		{
			Promise->SetValue(MakeError(TEXT("Unable to parse response")));
			return;
		}

		FString ExchangeCode;

		if (!ResponseJson->TryGetStringField(TEXT("exchange_code"), ExchangeCode))
		{
			Promise->SetValue(MakeError(TEXT("Missing `exchange_code`")));
			return;
		}

		Promise->SetValue(MakeValue(ExchangeCode));
	});
	HttpRequest->ProcessRequest();

	return Promise->GetFuture();
}

TFuture<FTdsReleaseAccountResult> ReleaseAccount(const FString& InTemplateHost, const FString& InAccountId)
{
	if (InTemplateHost.IsEmpty() || InAccountId.IsEmpty())
	{
		return {};
	}

	TSharedPtr<TPromise<FTdsReleaseAccountResult>> Promise = MakeShared<TPromise<FTdsReleaseAccountResult>>();

	const TSharedRef<IHttpRequest> HttpRequest = Private::CreateGetRequest
	(
		InTemplateHost / FString::Format(TEXT("/v1/pool/setPoolInformation?epicAccountId={0}"),
		{
			FPlatformHttp::UrlEncode(InAccountId)
		})
	);

	HttpRequest->OnProcessRequestComplete().BindLambda([Promise](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
	{
		if (!bSuccess || !Response.IsValid())
		{
			Promise->SetValue(MakeError(TEXT("Invalid response")));
			return;
		}

		if (!EHttpResponseCodes::IsOk(Response->GetResponseCode()))
		{
			Promise->SetValue(MakeError(FString::Format(TEXT("Invalid response code, got {0}"), { Response->GetResponseCode() })));
			return;
		}

		Promise->SetValue(MakeValue());
	});
	HttpRequest->ProcessRequest();

	return Promise->GetFuture();
}


/**
 * Actual implementation of TDS exchange authentication.
 */
class FExchangeCodeHandler::FImpl
{
public:
	FImpl(const FString& InTemplateHost, const FString& InTemplateName, const FString& InClientID)
		: TemplateHost(InTemplateHost)
		, TemplateName(InTemplateName)
		, ClientID(InClientID)
	{
	}

	void Acquire()
	{
		SetExchangeCode(TEXT(""));
		EpicAccountId.Empty();

		// Set account to be used
		PutInFlight(TEXT("AcquireAccount"), AcquireAccount(TemplateHost, TemplateName), [this](const FTdsAcquireAccountResult& Result)
		{
			if (Result.HasError())
			{
				UE_LOGFMT(LogMetaHumanTDSUtils, Error, "TDS acquire account error: {ErrorMessage}", Result.GetError());
				return;
			}

			const FMetaHumanTDSAcquiredAccountInfo& AccountInfo = Result.GetValue();

			EpicAccountId = AccountInfo.EpicAccountId;
			UE_LOGFMT(LogMetaHumanTDSUtils, Display, "Using TDS account ID: {AccountId}, email: {Email}", EpicAccountId, AccountInfo.Email);

			// Get the exchange code for the account ID
			PutInFlight(TEXT("GetExchangeCode"), GetExchangeCode(TemplateHost, EpicAccountId, ClientID), [this](const FTdsGetExchangeCodeResult& Result)
			{
				if (Result.HasError())
				{
					UE_LOGFMT(LogMetaHumanTDSUtils, Error, "TDS get exchange code error: {ErrorMessage}", Result.GetError());
					return;
				}

				UE_LOGFMT(LogMetaHumanTDSUtils, Display, "Exchange code successfully obtained");
				SetExchangeCode(Result.GetValue());
			});
		});
	}

	bool HasExchangeCode() const
	{
		return !ExchangeCode.IsEmpty();
	}

	void Release()
	{
		// Clear the exchange code just so we don't end up using stale value
		SetExchangeCode(TEXT(""));

		// If no account was acquired, nothing to release
		if (EpicAccountId.IsEmpty())
		{
			return;
		}

		PutInFlight(TEXT("ReleaseAccount"), ReleaseAccount(TemplateHost, EpicAccountId), [this](const FTdsReleaseAccountResult& Result)
		{
			if (Result.HasError())
			{
				UE_LOGFMT(LogMetaHumanTDSUtils, Error, "TDS release account error: {ErrorMessage}", Result.GetError());
			}
			else
			{
				EpicAccountId.Empty();
				UE_LOGFMT(LogMetaHumanTDSUtils, Display, "TDS account successfully released");
			}
		});
	}

private:
	FString TemplateHost;
	FString TemplateName;
	FString ClientID;

	// Account that we're currently operating on.
	FString EpicAccountId;

	// Token needed for the authentication
	FString ExchangeCode;

	/**
	 * Assigns exchange code value to this struct and MetaHumanSDK.
	 */
	void SetExchangeCode(const FString& InExchangeCode)
	{
		if (ExchangeCode == InExchangeCode)
		{
			return;
		}

		ExchangeCode = InExchangeCode;

		if (IConsoleVariable* const ExchangeCodeCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("MetaHuman.Cloud.Config.ExchangeCode")))
		{
			ExchangeCodeCVar->Set(*ExchangeCode);
		}
	}

	/**
	 * Puts given TDS future into flight and waits until it's completed.
	 */
	template<typename TFutureType, typename TFunc>
	void PutInFlight(const FString& InMethodName, TFuture<TFutureType>&& InFuture, TFunc InCallback)
	{
		UE_LOGFMT(LogMetaHumanTDSUtils, Display, "Calling {TDSMethod}", InMethodName);

		if (!InFuture.IsValid())
		{
			UE_LOGFMT(LogMetaHumanTDSUtils, Error, "Unable to execute call {TDSMethod}, invalid arguments", InMethodName);
			return;
		}

		bool bInFlight = true;

		InFuture.Next([&bInFlight, InCallback](const auto&... Args) mutable
		{
			bInFlight = false;
			InCallback(Forward<decltype(Args)>(Args)...);
		});

		while (bInFlight)
		{
			FHttpModule::Get().GetHttpManager().Tick(0.1f);
			FPlatformProcess::Sleep(0.05f);
		}
	}
};

FExchangeCodeHandler::FExchangeCodeHandler(const FString& InTemplateHost, const FString& InTemplateName, const FString& InClientID)
	: Impl(MakeUnique<FExchangeCodeHandler::FImpl>(InTemplateHost, InTemplateName, InClientID))
{
}

FExchangeCodeHandler::~FExchangeCodeHandler() = default;

void FExchangeCodeHandler::Acquire()
{
	Impl->Acquire();
}

void FExchangeCodeHandler::Release()
{
	Impl->Release();
}

bool FExchangeCodeHandler::HasExchangeCode() const
{
	return Impl->HasExchangeCode();
}

} // namespace UE::MetaHuman::TDSUtils
