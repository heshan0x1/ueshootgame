// Copyright Epic Games, Inc. All Rights Reserved.
#include "Cloud/MetaHumanCloudAuthentication.h"
#include "MetaHumanCloudAuthenticationInternal.h"
#include "Cloud/MetaHumanCloudServicesSettings.h"
#include "Logging/StructuredLog.h"
#include "Misc/ScopeLock.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"

#include "EOSShared.h"
#include "eos_auth.h"
#include "eos_sdk.h"
#include "eos_connect.h"
#include "eos_userinfo.h"
#include "IEOSSDKManager.h"

#define USE_EOS_LOGGED_IN_CHECK 1
DEFINE_LOG_CATEGORY_STATIC(LogMetaHumanAuth, Log, All)

namespace UE::MetaHuman::Authentication
{
	using IEOSPlatformHandlePtr = TSharedPtr<class IEOSPlatformHandle>;

	struct FCallbackContext;
	struct FClientState
	{
		FCriticalSection StateLock;
		EOS_HAuth AuthHandle = nullptr;
		EOS_HUserInfo UserInfoHandle = nullptr;
		EOS_EpicAccountId EpicAccountId = nullptr;
		IEOSPlatformHandlePtr PlatformHandle = nullptr;
		EEosEnvironmentType EnvironmentType = EEosEnvironmentType::Prod;
		TSharedPtr<FClient> OuterClient;
		uint64_t LoginFlags = 0;
		
		void Init(TSharedRef<FClient> Outer, EEosEnvironmentType EosEnvironmentType, void* InReserved);
		void LoginUsingPersist(FCallbackContext* CallbackContext);
		void LoginUsingAccountPortal(FCallbackContext* CallbackContext);
		void Login(FOnLoginCompleteDelegate&& OnLoginCompleteDelegate, FOnLoginFailedDelegate&& OnLoginFailedDelegate);
		void Logout(FOnLogoutCompleteDelegate&& OnLogoutCompleteDelegate);
		bool TrySetAuthHeaderForUser(TSharedRef<IHttpRequest> Request);
		void CheckIfLoggedInAsync(FOnCheckLoggedInCompletedDelegate&& OnCheckLoggedInCompletedDelegate);
		FString GetLoggedInAccountId() const
		{
			char AccountIdBuffer[EOS_EPICACCOUNTID_MAX_LENGTH + 1] = {};
			int32 AccountIdBufferLength = sizeof(AccountIdBuffer);
			EOS_EpicAccountId_ToString(EpicAccountId, AccountIdBuffer, &AccountIdBufferLength);
			AccountIdBuffer[AccountIdBufferLength - 1] = 0;
			return ANSI_TO_TCHAR(AccountIdBuffer);
		}
		FString GetLoggedInUserName()
		{
			if (AuthHandle == nullptr)
			{
				return {};
			}
			if (UserInfoHandle == nullptr)
			{
				UserInfoHandle = EOS_Platform_GetUserInfoInterface(*PlatformHandle);
				check(UserInfoHandle != nullptr);
			}
			 
			EOS_UserInfo_CopyBestDisplayNameWithPlatformOptions Options = {};
			Options.ApiVersion = EOS_USERINFO_COPYBESTDISPLAYNAMEWITHPLATFORM_API_LATEST;
			Options.LocalUserId = EpicAccountId;
			Options.TargetUserId = EpicAccountId;
			Options.TargetPlatformType = EOS_OPT_Epic;
			EOS_UserInfo_BestDisplayName* BestDisplayName;
			EOS_EResult Result = EOS_UserInfo_CopyBestDisplayNameWithPlatform(UserInfoHandle, &Options, &BestDisplayName);
			FString DisplayNameString;
			if (Result == EOS_EResult::EOS_Success)
			{
				DisplayNameString = BestDisplayName->DisplayName;
				EOS_UserInfo_BestDisplayName_Release(BestDisplayName);
			}
			return DisplayNameString;
		}
		bool CheckedGetAuthHandle()
		{
			if (!PlatformHandle.IsValid())
			{
				return false;
			}
			AuthHandle = EOS_Platform_GetAuthInterface(*PlatformHandle);
			return true;
		}
	};

	struct FCallbackContext
	{
		FClientState* ClientState;
		FOnLoginCompleteDelegate OnLoginCompleteDelegate;
		FOnLoginFailedDelegate OnLoginFailedDelegate;
		FOnLogoutCompleteDelegate OnLogoutCompleteDelegate;

		static FCallbackContext* Create()
		{
			return new FCallbackContext;
		}

		template<typename TEOSCallbackInfo>
		static FCallbackContext* Get(const TEOSCallbackInfo* Data)
		{
			return reinterpret_cast<FCallbackContext*>(Data->ClientData);
		}
	};

	class FScopedCallbackContext
	{
	public:
		template<typename T>
		FScopedCallbackContext(const T* Info)		
		{
			Context.Reset(FCallbackContext::Get(Info));
		}
		FCallbackContext* Release()
		{
			return Context.Release();
		}

		FCallbackContext* operator->()
		{
			return Context.operator->();
		}
	private:
		TUniquePtr<FCallbackContext> Context;
	};

	void EOS_CALL CreateUserCallbackInfo(const EOS_Connect_CreateUserCallbackInfo* Info)
	{
		FScopedCallbackContext ScopedCallbackContext(Info);
		FClientState* ClientState = ScopedCallbackContext->ClientState;
		check(ClientState);
		if (ClientState && ClientState->OuterClient.IsValid())
		{
			if (Info->ResultCode == EOS_EResult::EOS_Success)
			{
				ScopedCallbackContext->OnLoginCompleteDelegate.ExecuteIfBound(ClientState->OuterClient.ToSharedRef());
			}
			else
			{
				ScopedCallbackContext->OnLoginFailedDelegate.ExecuteIfBound();
			}
		}
	}

	void EOS_CALL ConnectLoginCallbackFn(const EOS_Connect_LoginCallbackInfo* Info)
	{
		FScopedCallbackContext ScopedCallbackContext(Info);
		FClientState* ClientState = ScopedCallbackContext->ClientState;
		check(ClientState);
		if (ClientState && ClientState->OuterClient.IsValid())
		{
			if (Info->ResultCode == EOS_EResult::EOS_Success)
			{
				ScopedCallbackContext->OnLoginCompleteDelegate.ExecuteIfBound(ClientState->OuterClient.ToSharedRef());
			}
			else if (Info->ResultCode == EOS_EResult::EOS_InvalidUser)
			{
				EOS_Connect_CreateUserOptions Options = { };
				Options.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
				Options.ContinuanceToken = Info->ContinuanceToken;

				EOS_HConnect ConnectHandle = EOS_Platform_GetConnectInterface(*ClientState->PlatformHandle);
				EOS_Connect_CreateUser(ConnectHandle, &Options, ScopedCallbackContext.Release(), CreateUserCallbackInfo);
			}
			else
			{
				ScopedCallbackContext->OnLoginFailedDelegate.ExecuteIfBound();
			}
		}
	}

	void EOS_CALL LoginCompleteCallbackFn(const EOS_Auth_LoginCallbackInfo* Info)
	{
		FScopedCallbackContext ScopedCallbackContext(Info);
		FClientState* ClientState = ScopedCallbackContext->ClientState;
		check(ClientState);
		if (ClientState && ClientState->OuterClient.IsValid())
		{
			const EOS_EResult ResultCode = Info->ResultCode;
			switch (ResultCode)
			{
			case EOS_EResult::EOS_Success:
			{
				{
					FScopeLock Lock(&ClientState->StateLock);
					const int32_t AccountsCount = EOS_Auth_GetLoggedInAccountsCount(ClientState->AuthHandle);
					for (int32_t AccountIdx = 0; AccountIdx < AccountsCount; ++AccountIdx)
					{
						const EOS_EpicAccountId AccountId = EOS_Auth_GetLoggedInAccountByIndex(ClientState->AuthHandle, AccountIdx);
						EOS_ELoginStatus LoginStatus = EOS_Auth_GetLoginStatus(ClientState->AuthHandle, Info->LocalUserId);
						ClientState->EpicAccountId = AccountId;
					}
				}

				UE_LOGFMT(LogMetaHumanAuth, Display, "User name is {BestUserName}", ClientState->GetLoggedInUserName());

				// Perform a "Connect" login to ensure a ProductUserID exists on the EOS back end for this user
				EOS_Auth_Token* UserAuthTokenPtr = nullptr;
				EOS_Auth_CopyUserAuthTokenOptions CopyTokenOptions = { 0 };
				CopyTokenOptions.ApiVersion = EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST;
				if (EOS_Auth_CopyUserAuthToken(ClientState->AuthHandle, &CopyTokenOptions, ClientState->EpicAccountId, &UserAuthTokenPtr) == EOS_EResult::EOS_Success)
				{					
					FString UserAuthToken = UserAuthTokenPtr->AccessToken;
					const FTCHARToUTF8 Utf8UserAuthToken(*UserAuthToken);
					EOS_Auth_Token_Release(UserAuthTokenPtr);

					EOS_Connect_Credentials Credentials = {};
					Credentials.ApiVersion = EOS_CONNECT_CREDENTIALS_API_LATEST;
					Credentials.Token = Utf8UserAuthToken.Get();
					Credentials.Type = EOS_EExternalCredentialType::EOS_ECT_EPIC;
					EOS_Connect_LoginOptions LoginOptions = {};
					LoginOptions.ApiVersion = EOS_CONNECT_LOGIN_API_LATEST;
					LoginOptions.Credentials = &Credentials;
					LoginOptions.UserLoginInfo = nullptr;
					
					EOS_HConnect ConnectHandle = EOS_Platform_GetConnectInterface(*ClientState->PlatformHandle);
					EOS_Connect_Login(ConnectHandle, &LoginOptions, ScopedCallbackContext.Release(), ConnectLoginCallbackFn);
				}
			}
			break;
			case EOS_EResult::EOS_Auth_PinGrantCode:
			{
				UE_LOGFMT(LogMetaHumanAuth, Warning, "Login pin grant code");
			}
			break;
			case EOS_EResult::EOS_Auth_MFARequired:
			{
				UE_LOGFMT(LogMetaHumanAuth, Display, "Login MFA required");
			}
			break;
			case EOS_EResult::EOS_InvalidUser:
			{
				UE_LOGFMT(LogMetaHumanAuth, Display, "Invalid user");
			}
			break;
			case EOS_EResult::EOS_Auth_AccountFeatureRestricted:
			{
				UE_LOGFMT(LogMetaHumanAuth, Display, "Login failed, account is restricted");
			}
			break;
			default:
			{
				const FString Code = EOS_EResult_ToString(Info->ResultCode);
				UE_LOGFMT(LogMetaHumanAuth, Display, "Login failed - error code: {ResultCode}", *Code);
			}
			break;
			}

			if (Info->ResultCode != EOS_EResult::EOS_Success)
			{
				ScopedCallbackContext->OnLoginFailedDelegate.ExecuteIfBound();
			}
		}
	}

	void EOS_CALL LoginPersistCompleteCallbackFn(const EOS_Auth_LoginCallbackInfo* Data)
	{
		FCallbackContext* CallbackContext = FCallbackContext::Get(Data);

		static bool bCanUsePortalAccountFallback = !FParse::Param(FCommandLine::Get(), TEXT("NoMetaHumanAccountPortalLoginFallback"));

		if (Data->ResultCode != EOS_EResult::EOS_Success && ((CallbackContext->ClientState->LoginFlags & EOS_LF_NO_USER_INTERFACE) != EOS_LF_NO_USER_INTERFACE) && bCanUsePortalAccountFallback)
		{
			CallbackContext->ClientState->LoginUsingAccountPortal(CallbackContext);
		}
		else
		{
			LoginCompleteCallbackFn(Data);
		}
	}

	void EOS_CALL LogoutCompletedCallbackFn(const EOS_Auth_LogoutCallbackInfo* Data)
	{
		// ensure cleanup on leaving this function 
		TUniquePtr<FCallbackContext> CallbackContext;
		CallbackContext.Reset(FCallbackContext::Get(Data));

		FClientState* ClientState = CallbackContext->ClientState;
		check(ClientState);
		if (ClientState && ClientState->OuterClient.IsValid())
		{
			if (Data->ResultCode == EOS_EResult::EOS_Success)
			{
				{
					FScopeLock Lock(&ClientState->StateLock);
					// use this to signal that we're no longer logged in
					ClientState->EpicAccountId = nullptr;
				}
				CallbackContext->OnLogoutCompleteDelegate.ExecuteIfBound(ClientState->OuterClient.ToSharedRef());
			}
		}
	}

	void EOS_CALL DeletePersistentAuthCompletedCallbackFn(const EOS_Auth_DeletePersistentAuthCallbackInfo*)
	{
		/* NOP but we need the callback for the EOS function to succeed */
	}

	static TAutoConsoleVariable<FString> CVarMhCloudUserEmail(
		TEXT("MetaHuman.Cloud.Config.UserEmail"),
		{},
		TEXT("User email for login to MH Cloud, requires that MetaHuman.Cloud.Config.Password is also set"),
		ECVF_Default
	);

	static TAutoConsoleVariable<FString> CVarMhCloudPassword(
		TEXT("MetaHuman.Cloud.Config.Password"),
		{},
		TEXT("Password for login to MH Cloud, requires that MetaHuman.Cloud.Config.UserEmail is also set"),
		ECVF_Default
	);

	static TAutoConsoleVariable<FString> CVarMhCloudExchangeCode(
		TEXT("MetaHuman.Cloud.Config.ExchangeCode"),
		{},
		TEXT("Exchange code to use for login to MH Cloud"),
		ECVF_Default
	);

	void FClientState::LoginUsingPersist(FCallbackContext* CallbackContext)
	{
		if (!CheckedGetAuthHandle())
		{
			CallbackContext->OnLoginFailedDelegate.ExecuteIfBound();
			return;
		}

		EOS_Auth_Credentials Credentials = { 0 };
		Credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;

		const FString UserEmail = CVarMhCloudUserEmail.GetValueOnAnyThread();
		const FString Password = CVarMhCloudPassword.GetValueOnAnyThread();
		const FString ExchangeCode = CVarMhCloudExchangeCode.GetValueOnAnyThread();

		if (!UserEmail.IsEmpty() && !Password.IsEmpty())
		{
			UE_LOGFMT(LogMetaHumanAuth, Display, "Logging in user from set CVars");
			Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_Password;
			const FTCHARToUTF8 Utf8UserEmail(*UserEmail);
			Credentials.Id = Utf8UserEmail.Get();
			const FTCHARToUTF8 Utf8Password(*Password);
			Credentials.Token = Utf8Password.Get();
		}
		else if (!ExchangeCode.IsEmpty())
		{
			UE_LOGFMT(LogMetaHumanAuth, Display, "Logging in user from exchange code");
			Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_ExchangeCode;
			Credentials.Id = nullptr;
			const FTCHARToUTF8 Utf8ExchangeCode(*ExchangeCode);
			Credentials.Token = Utf8ExchangeCode.Get();
		}
		else
		{
			Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_PersistentAuth;
		}

		EOS_Auth_LoginOptions LoginOptions = { 0 };
		LoginOptions.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
		LoginOptions.ScopeFlags = EOS_EAuthScopeFlags::EOS_AS_BasicProfile;
		LoginOptions.Credentials = &Credentials;
		LoginFlags = 0;

		EOS_Auth_Login(AuthHandle, &LoginOptions, CallbackContext, LoginPersistCompleteCallbackFn);
	}

	void FClientState::LoginUsingAccountPortal(FCallbackContext* CallbackContext)
	{
		if (!CheckedGetAuthHandle())
		{
			CallbackContext->OnLoginFailedDelegate.ExecuteIfBound();
			return;
		}

		EOS_Auth_Credentials Credentials = { 0 };
		Credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;

		Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_AccountPortal;

		EOS_Auth_LoginOptions LoginOptions = { 0 };
		LoginOptions.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
		LoginOptions.ScopeFlags = EOS_EAuthScopeFlags::EOS_AS_BasicProfile;
		LoginOptions.Credentials = &Credentials;
		LoginFlags = 0;

		EOS_Auth_Login(AuthHandle, &LoginOptions, CallbackContext, LoginCompleteCallbackFn);
	}

	struct FEosEnvironmentInitializationParams
	{
		FString ProductId;
		FString SandboxId;
		FString ClientId;
		FString ClientSecret;
		FString DeploymentId;
		void* ReservedData;
	};

	void FClientState::Init(TSharedRef<FClient> Outer, EEosEnvironmentType EosEnvironmentType, void* InReservedData)
	{
		EnvironmentType = EosEnvironmentType;
		OuterClient = Outer;

		IEOSSDKManager* SDKManager = IEOSSDKManager::Get();
		if (SDKManager && SDKManager->IsInitialized())
		{
			EOS_Platform_Options PlatformOptions = {};
			PlatformOptions.ApiVersion = EOS_PLATFORM_OPTIONS_API_LATEST;

			const UMetaHumanCloudServicesSettings* Settings = GetDefault<UMetaHumanCloudServicesSettings>();
			FString ProductId;
			FString SandboxId;
			FString ClientId;
			FString ClientSecret;
			FString DeploymentId;
			switch (EnvironmentType)
			{
			case EEosEnvironmentType::Prod:
			{
				ProductId = Settings->ProdEosConstants.ProductId;
				SandboxId = Settings->ProdEosConstants.SandboxId;
				ClientId = Settings->ProdEosConstants.ClientCredentialsId;
				ClientSecret = Settings->ProdEosConstants.ClientCredentialsSecret;
				DeploymentId = Settings->ProdEosConstants.DeploymentId;
				PlatformOptions.Reserved = nullptr;
			}
			break;
			case EEosEnvironmentType::GameDev:
			{
				ProductId = Settings->GameDevEosConstants.ProductId;
				SandboxId = Settings->GameDevEosConstants.SandboxId;
				ClientId = Settings->GameDevEosConstants.ClientCredentialsId;
				ClientSecret = Settings->GameDevEosConstants.ClientCredentialsSecret;
				DeploymentId = Settings->GameDevEosConstants.DeploymentId;
				PlatformOptions.Reserved = InReservedData;
			}
			break;
			default:;
			}

			const FTCHARToUTF8 Utf8ProductId(*ProductId);
			const FTCHARToUTF8 Utf8SandboxId(*SandboxId);
			const FTCHARToUTF8 Utf8ClientId(*ClientId);
			const FTCHARToUTF8 Utf8ClientSecret(*ClientSecret);
			const FTCHARToUTF8 Utf8DeploymentId(*DeploymentId);

			PlatformOptions.ClientCredentials.ClientId = Utf8ClientId.Get();
			PlatformOptions.ClientCredentials.ClientSecret = Utf8ClientSecret.Get();
			PlatformOptions.ProductId = Utf8ProductId.Get();
			PlatformOptions.SandboxId = Utf8SandboxId.Get();
			PlatformOptions.DeploymentId = Utf8DeploymentId.Get();
			PlatformOptions.bIsServer = EOS_FALSE;
			PlatformOptions.Flags = EOS_PF_DISABLE_OVERLAY;
			PlatformOptions.TickBudgetInMilliseconds = 0;
			PlatformOptions.IntegratedPlatformOptionsContainerHandle = nullptr;

			PlatformHandle = SDKManager->CreatePlatform(PlatformOptions);
		}
	}

	void FClientState::Login(FOnLoginCompleteDelegate&& InOnLoginCompleteDelegate, FOnLoginFailedDelegate&& InOnLoginFailedDelegate)
	{		
		if (PlatformHandle.IsValid())
		{
			FCallbackContext* CallbackContext = FCallbackContext::Create();
			CallbackContext->ClientState = this;
			CallbackContext->OnLoginCompleteDelegate = MoveTemp(InOnLoginCompleteDelegate);
			CallbackContext->OnLoginFailedDelegate = MoveTemp(InOnLoginFailedDelegate);
			// always do this first, if it fails we chain to the portal
			LoginUsingPersist(CallbackContext);
		}
	}

	void FClientState::Logout(FOnLogoutCompleteDelegate&& InOnLogoutCompleteDelegate)
	{
		if (PlatformHandle.IsValid() && EpicAccountId != nullptr)
		{
			// If we've just logged in with Persistent Auth
			EOS_Auth_LogoutOptions LogoutOptions = {};
			LogoutOptions.ApiVersion = EOS_AUTH_LOGOUT_API_LATEST;
			LogoutOptions.LocalUserId = EpicAccountId;

			FCallbackContext* CallbackContext = FCallbackContext::Create();
			CallbackContext->ClientState = this;
			CallbackContext->OnLogoutCompleteDelegate = MoveTemp(InOnLogoutCompleteDelegate);
			EOS_Auth_Logout(AuthHandle, &LogoutOptions, CallbackContext, LogoutCompletedCallbackFn);

			// And if we've also logged in with account portal (we need both to properly clean things up)
			EOS_Auth_DeletePersistentAuthOptions LogoutPersistentOptions = {};
			LogoutPersistentOptions.ApiVersion = EOS_AUTH_DELETEPERSISTENTAUTH_API_LATEST;
			EOS_Auth_DeletePersistentAuth(AuthHandle, &LogoutPersistentOptions, this, DeletePersistentAuthCompletedCallbackFn);
		}
	}

	bool FClientState::TrySetAuthHeaderForUser(TSharedRef<IHttpRequest> Request)
	{
		bool bWasSet = false;
		// we need to lock this entire call so that we don't clash with logouts
		FScopeLock Lock(&StateLock);
		if (EpicAccountId != nullptr)
		{
			EOS_Auth_Token* UserAuthTokenPtr = nullptr;
			EOS_Auth_CopyUserAuthTokenOptions CopyTokenOptions = { 0 };
			CopyTokenOptions.ApiVersion = EOS_AUTH_COPYUSERAUTHTOKEN_API_LATEST;

			FString UserAuthToken;
			if (EOS_Auth_CopyUserAuthToken(AuthHandle, &CopyTokenOptions, EpicAccountId, &UserAuthTokenPtr) == EOS_EResult::EOS_Success)
			{
				UserAuthToken = UserAuthTokenPtr->AccessToken;
				EOS_Auth_Token_Release(UserAuthTokenPtr);
			}
			
			if (!UserAuthToken.IsEmpty())
			{
				Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + UserAuthToken);
				bWasSet = true;
			}
		}
		return bWasSet;
	}

	void FClientState::CheckIfLoggedInAsync(FOnCheckLoggedInCompletedDelegate&& OnCheckLoggedInCompletedDelegate)
	{
#if USE_EOS_LOGGED_IN_CHECK
		if (CheckedGetAuthHandle() && EOS_Auth_GetLoggedInAccountsCount(AuthHandle) == 0)
		{
			EOS_Auth_Credentials Credentials = { 0 };
			Credentials.ApiVersion = EOS_AUTH_CREDENTIALS_API_LATEST;
			Credentials.Type = EOS_ELoginCredentialType::EOS_LCT_PersistentAuth;
			
			EOS_Auth_LoginOptions LoginOptions = { 0 };
			LoginOptions.ApiVersion = EOS_AUTH_LOGIN_API_LATEST;
			LoginOptions.ScopeFlags = EOS_EAuthScopeFlags::EOS_AS_BasicProfile;
			// don't trigger a UI flow if persistent log-in doesn't work
			LoginOptions.LoginFlags = LoginFlags = EOS_LF_NO_USER_INTERFACE;
			LoginOptions.Credentials = &Credentials;

			FCallbackContext* CallbackContext = new FCallbackContext;
			CallbackContext->ClientState = this;
			CallbackContext->OnLoginCompleteDelegate = FOnLoginCompleteDelegate::CreateLambda([OnCheckLoggedInCompletedDelegate](TSharedRef<FClient> Client)
				{
					OnCheckLoggedInCompletedDelegate.ExecuteIfBound(true, Client->GetLoggedInAccountId(), Client->GetLoggedInAccountUserName());
				});
			CallbackContext->OnLoginFailedDelegate = FOnLoginFailedDelegate::CreateLambda([OnCheckLoggedInCompletedDelegate]()
				{
					OnCheckLoggedInCompletedDelegate.ExecuteIfBound(false, {}, {});
				});
			EOS_Auth_Login(AuthHandle, &LoginOptions, CallbackContext, LoginPersistCompleteCallbackFn);
		}
		else
		{
			// note that if the auth handle is 0 the account ID is just an empty string
			OnCheckLoggedInCompletedDelegate.ExecuteIfBound(AuthHandle != nullptr, GetLoggedInAccountId(), GetLoggedInUserName());
		}
#else
		OnCheckLoggedInCompletedDelegate.ExecuteIfBound(CheckedGetAuthHandle() && EOS_Auth_GetLoggedInAccountsCount(AuthHandle) > 0);
#endif // USE_EOS_LOGGED_IN_CHECK
	}

	FClient::FClient(FPrivateToken)
	{
		ClientState = MakePimpl<FClientState>();
	}

	TSharedRef<FClient> FClient::CreateClient(EEosEnvironmentType EnvironmentType, void* InReserved)
	{
		TSharedRef<FClient> Client = MakeShared<FClient>(FClient::FPrivateToken());
		Client->ClientState->Init(Client, EnvironmentType, InReserved);
		return Client;
	}

	void FClient::HasLoggedInUser(FOnCheckLoggedInCompletedDelegate&& OnCheckLoggedInCompletedDelegate)
	{
		return ClientState->CheckIfLoggedInAsync(Forward<FOnCheckLoggedInCompletedDelegate>(OnCheckLoggedInCompletedDelegate));
	}

	void FClient::LoginAsync(FOnLoginCompleteDelegate&& OnLoginCompleteDelegate, FOnLoginFailedDelegate&& OnLoginFailedDelegate)
	{
		ClientState->Login(Forward<FOnLoginCompleteDelegate>(OnLoginCompleteDelegate), Forward<FOnLoginFailedDelegate>(OnLoginFailedDelegate));
	}

	void FClient::LogoutAsync(FOnLogoutCompleteDelegate &&OnLogoutCompleteDelegate)
	{
		ClientState->Logout(Forward<FOnLogoutCompleteDelegate>(OnLogoutCompleteDelegate));
	}

	bool FClient::SetAuthHeaderForUserBlocking(TSharedRef<IHttpRequest> Request)
	{
		return ClientState->TrySetAuthHeaderForUser(Request);
	}

	FString FClient::GetLoggedInAccountId() const
	{
		return ClientState->GetLoggedInAccountId();
	}

	FString FClient::GetLoggedInAccountUserName() const
	{
		return ClientState->GetLoggedInUserName();
	}

	void FClient::Tick()
	{
		check(ClientState->PlatformHandle);
		ClientState->PlatformHandle->Tick();
	}
}
