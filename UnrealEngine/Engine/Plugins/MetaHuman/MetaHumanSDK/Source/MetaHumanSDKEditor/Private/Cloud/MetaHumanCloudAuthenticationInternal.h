// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Cloud/MetaHumanCloudAuthentication.h"
#include "HttpModule.h"
#include "Templates/PimplPtr.h"

#define UE_API METAHUMANSDKEDITOR_API

namespace UE::MetaHuman::Authentication
{
	struct FClientState;

	DECLARE_DELEGATE_ThreeParams(FOnCheckLoggedInCompletedDelegate, bool, FString, FString);
	DECLARE_DELEGATE_OneParam(FOnLoginCompleteDelegate, TSharedRef<class FClient>);
	DECLARE_DELEGATE(FOnLoginFailedDelegate);
	DECLARE_DELEGATE_OneParam(FOnLogoutCompleteDelegate, TSharedRef<class FClient>);

	class FClient final
		: public TSharedFromThis<FClient>
	{
		struct FPrivateToken
		{
			explicit FPrivateToken() = default;
		};
	public:
		UE_API explicit FClient(FPrivateToken PrivateToken);
		// create an instance of a MetaHuman cloud client for the given environment
		// NOTE: if GameDev the correct reserved data needs to be passed in
		static UE_API TSharedRef<FClient> CreateClient(EEosEnvironmentType EnvironmentType, void* InReserved = nullptr);
		// returns true if there's at least one logged in user for this client (will check auth token status)
		UE_API void HasLoggedInUser(FOnCheckLoggedInCompletedDelegate&& OnCheckLoggedInCompletedDelegate);
		// Log in asynchronously and invoke delegate when done
		UE_API void LoginAsync(FOnLoginCompleteDelegate&& OnLoginCompleteDelegate, FOnLoginFailedDelegate&& OnLoginFailedDelegate);
		// Log out asynchronously and invoke delegate when done
		UE_API void LogoutAsync(FOnLogoutCompleteDelegate&& OnLogoutCompleteDelegate);
		// Set the authorization header for this user in the given request
		// NOTE: this might block waiting for an in-progress authentication process
		UE_API bool SetAuthHeaderForUserBlocking(TSharedRef<IHttpRequest> Request);
		// return the currently logged in user's account id, or ""
		UE_API FString GetLoggedInAccountId() const;
		// returns the currently logged in user's user name, or ""
		UE_API FString GetLoggedInAccountUserName() const;
		// Tick client state to allow any background tasks to be completed. Useful for blocking calls that need to wait on the authentication flow
		UE_API void Tick();		
	private:
		TPimplPtr<FClientState> ClientState;
	};
}

#undef UE_API
