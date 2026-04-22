// Copyright Epic Games, Inc. All Rights Reserved.

#include "User/SocialUser.h"

#include "Interactions/SocialInteractionHandle.h"
#include "Interfaces/OnlineUserInterface.h"
#include "Misc/CommandLine.h"

#include "Online/OnlineErrorDefinitions.h"
#include "Online/OnlineServices.h"
#include "OnlineFramework/CommonAccount.h"
#include "OnlineFramework/CommonAccountUtils.h"
#include "OnlineFramework/CommonConfig.h"
#include "OnlineSubsystemUtils.h"
#include "Party/PartyMember.h"
#include "Party/SocialParty.h"
#include "SocialManager.h"
#include "SocialQuery.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SocialUser)

#define LOCTEXT_NAMESPACE "SocialUser"

// Debug utils
#if !UE_BUILD_SHIPPING
class USocialUser::FDebugInitializer
{
public:
	FDebugInitializer(USocialUser& InParent)
		: Parent(InParent)
	{
	}
	~FDebugInitializer()
	{
		if (TickHandle.IsValid())
		{
			FTSTicker::RemoveTicker(TickHandle);
		}
	}

	static bool IsEnabled()
	{
		return CVarSocialUserDeferInitializeSeconds.GetValueOnGameThread() > 0 ||
			!CVarSocialUserDeferInitializePauseIds.GetValueOnGameThread().IsEmpty();
	}
	void FinishInitialization()
	{
		if (CVarSocialUserDeferInitializePauseIds.GetValueOnGameThread().Contains(Parent.GetUserId(ESocialSubsystem::Primary).ToString()))
		{
			// Pause until the cvar changes
			UE_LOG(LogParty, Log, TEXT("%hs Deferring initialization of user [%s] until they are removed from party.SocialUserDeferInitializePauseIds"), __FUNCTION__, *Parent.ToDebugString());
			PausedSocialUsers.Emplace(TWeakObjectPtr<USocialUser>(&Parent));
		}
		else if (int32 DelaySeconds = CVarSocialUserDeferInitializeSeconds.GetValueOnGameThread(); 
			DelaySeconds > 0)
		{
			// Pause for config driven number of seconds
			UE_LOG(LogParty, Log, TEXT("%hs Deferring initialization of user [%s] for %d seconds"), __FUNCTION__, *Parent.ToDebugString(), DelaySeconds);
			TickHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("USocialUser::FDebugInitializer"), (double)DelaySeconds, [this](float)->bool
			{
				TickHandle.Reset();
				Parent.FinishInitialization();
				return false;
			});
		}
		else
		{
			Parent.FinishInitialization();
		}
	}

private:
	USocialUser& Parent;
	FTSTicker::FDelegateHandle TickHandle;

	static TAutoConsoleVariable<int32> CVarSocialUserDeferInitializeSeconds;
	static TAutoConsoleVariable<FString> CVarSocialUserDeferInitializePauseIds;
	static TArray<TWeakObjectPtr<USocialUser>> PausedSocialUsers;

	static void OnPausedSocialUserIdsChanged(IConsoleVariable* CVar)
	{
		TArray<FString> PausedSocialUserIds;
		CVar->GetString().ParseIntoArray(PausedSocialUserIds, TEXT(","));
		TArray<TWeakObjectPtr<USocialUser>> PausedSocialUsersCopy = PausedSocialUsers;
		for (TWeakObjectPtr<USocialUser>& WeakSocialUser : PausedSocialUsersCopy)
		{
			USocialUser* SocialUser = WeakSocialUser.Get();
			if (SocialUser == nullptr)
			{
				PausedSocialUsers.Remove(WeakSocialUser);
				continue;
			}
			else if (!PausedSocialUserIds.Contains(SocialUser->GetUserId(ESocialSubsystem::Primary).ToString()))
			{
				UE_LOG(LogParty, Log, TEXT("%hs Finishing initialization of user [%s]"), __FUNCTION__, *SocialUser->ToDebugString());
				PausedSocialUsers.Remove(WeakSocialUser);
				SocialUser->FinishInitialization();
			}
		}
	}
};

TAutoConsoleVariable<int32> USocialUser::FDebugInitializer::CVarSocialUserDeferInitializeSeconds(
	TEXT("party.SocialUserDeferInitializeSeconds"),
	0,
	TEXT("How many seconds to delay finalizing social user initialization"));
TAutoConsoleVariable<FString> USocialUser::FDebugInitializer::CVarSocialUserDeferInitializePauseIds(
	TEXT("party.SocialUserDeferInitializePauseIds"),
	TEXT(""),
	TEXT("Which user ids to pause initializing on (until unset). Should be a comma separated list."),
	FConsoleVariableDelegate::CreateStatic(&FDebugInitializer::OnPausedSocialUserIdsChanged)
);
TArray<TWeakObjectPtr<USocialUser>> USocialUser::FDebugInitializer::PausedSocialUsers;
#endif // !UE_BUILD_SHIPPING

TAutoConsoleVariable<bool> CVarSocialUserRemoveInvitationOnJoinPartyCompleteEnabled(
	TEXT("party.SocialUserRemoveInvitationOnJoinPartyCompleteEnabled"),
	true,
	TEXT("When enabled, will remove invitation on join party complete, instead of remove immediately")
);

//////////////////////////////////////////////////////////////////////////
// FSocialQuery_UserInfo
//////////////////////////////////////////////////////////////////////////

class FSocialQuery_UserInfoV2 : public TSocialQuery<UE::Online::FAccountId, const TValueOrError<TSharedRef<UE::Online::FUserInfo>, UE::Online::FOnlineError>&>
{
public:
	friend class USocialToolkit;

	static FName GetQueryId() { return TEXT("UserInfoV2"); }

	virtual void ExecuteQuery() override
	{
		using namespace UE::Online;
		using namespace UE::OnlineFramework;

		if (const USocialToolkit* StrongToolkit = Toolkit.Get())
		{
			FCommonConfig CommonConfig(StrongToolkit);
			if (IOnlineServicesPtr Services = CommonConfig.GetServices(USocialManager::GetPresenceFrameworkInstance()))
			{
				if (IUserInfoPtr UserInfo = Services->GetUserInfoInterface())
				{
					bHasExecuted = true;

					const FUniqueNetIdRepl& LocalUserNetId = Toolkit->GetLocalUserNetId(SubsystemType);
					if(LocalUserNetId.IsValid())
					{
						if (const FCommonAccountPtr LocalUserCommonAccountPtr = GetCommonAccountFromV1(CommonConfig, LocalUserNetId->AsShared(), GetFirstFrameworkInstanceName(LocalUserNetId)))
						{
							FAccountId LocalUserAccountId = LocalUserCommonAccountPtr->GetId(USocialManager::GetPresenceFrameworkInstance());

							TArray<FAccountId> SocialUserAccountIds;
							CompletionCallbacksByUserId.GenerateKeyArray(SocialUserAccountIds);
							UE_LOG(LogParty, Log, TEXT("FSocialQuery_UserInfoV2 executing for [%d] users on subsystem [%s]"), SocialUserAccountIds.Num(), ToString(SubsystemType));

							UserInfo->QueryUserInfo({ LocalUserAccountId, SocialUserAccountIds })
								.OnComplete([this, LocalUserAccountId, SocialUserAccountIds, UserInfoWeakPtr = UserInfo.ToWeakPtr()](const TOnlineResult<FQueryUserInfo>& Result)
								{
									if (Result.IsOk())
									{
										UE_LOG(LogParty, Log, TEXT("FSocialQuery_UserInfoV2 completed query on services [%s]"), ToString(SubsystemType));
											
										using namespace UE::Online;

										if (IUserInfoPtr UserInfo = UserInfoWeakPtr.Pin())
										{
											// Notify users of the query completion
											for (const FAccountId& UserAccountId : SocialUserAccountIds)
											{
												TOnlineResult<FGetUserInfo> GetUserInfoResult = UserInfo->GetUserInfo({ LocalUserAccountId, UserAccountId });
												if (GetUserInfoResult.IsOk())
												{
													if (const FOnQueryComplete* CompletionCallback = CompletionCallbacksByUserId.Find(UserAccountId))
													{
														CompletionCallback->ExecuteIfBound(SubsystemType, true, MakeValue(GetUserInfoResult.GetOkValue().UserInfo));
													}
												}
												else
												{
													if (const FOnQueryComplete* CompletionCallback = CompletionCallbacksByUserId.Find(UserAccountId))
													{
														CompletionCallback->ExecuteIfBound(SubsystemType, false, MakeError(GetUserInfoResult.GetErrorValue()));
													}
												}
											}
										}

										OnQueryCompleted.ExecuteIfBound(GetQueryId(), AsShared());
									}
									else
									{
										UE_LOG(LogParty, Log, TEXT("FSocialQuery_UserInfoV2 failed query on services [%s] with error [%s]"), ToString(SubsystemType), *Result.GetErrorValue().GetLogString());
									}
								});
						}
					}
				}
			}
		}
	}
};

class FSocialQuery_UserInfo : public TSocialQuery<FUniqueNetIdRef, const TSharedPtr<FOnlineUser>&>
{
public:
	static FName GetQueryId() { return TEXT("UserInfo"); }

	virtual void ExecuteQuery() override
	{
		IOnlineSubsystem* OSS = GetOSS();
		IOnlineUserPtr UserInterface = OSS ? OSS->GetUserInterface() : nullptr;
		if (UserInterface.IsValid() && CompletionCallbacksByUserId.Num() > 0)
		{
			bHasExecuted = true;
			
			const int32 LocalUserNum = Toolkit->GetLocalUserNum();
			UserInterface->AddOnQueryUserInfoCompleteDelegate_Handle(LocalUserNum, FOnQueryUserInfoCompleteDelegate::CreateSP(this, &FSocialQuery_UserInfo::HandleQueryUserInfoComplete));

			TArray<FUniqueNetIdRef> UserIds;
			CompletionCallbacksByUserId.GenerateKeyArray(UserIds);
			UE_LOG(LogParty, Log, TEXT("FSocialQuery_UserInfo executing for [%d] users on subsystem [%s]"), UserIds.Num(), ToString(SubsystemType));

			UserInterface->QueryUserInfo(LocalUserNum, UserIds);
		}
	}

private:
	void HandleQueryUserInfoComplete(int32 LocalUserNum, bool bWasSuccessful, const TArray<FUniqueNetIdRef>& UserIds, const FString& ErrorStr)
	{
		if (!ensure(Toolkit.IsValid()) || Toolkit->GetLocalUserNum() != LocalUserNum)
		{
			return;
		}

		UE_LOG(LogParty, Log, TEXT("FSocialQuery_UserInfo completed query for [%d] users on subsystem [%s] with error [%s]"), UserIds.Num(), ToString(SubsystemType), *ErrorStr);
		
		IOnlineSubsystem* OSS = GetOSS();
		if (IOnlineUserPtr UserInterface = OSS ? OSS->GetUserInterface() : nullptr)
		{
			// Can't just check for array equality - order and exact address of the Ids aren't dependably the same as those given to the query
			bool bIsOurQuery = true;
			for (const TPair<FUniqueNetIdRef, FOnQueryComplete>& IdCallbackPair : CompletionCallbacksByUserId)
			{
				const auto ContainsPred = 
					[&IdCallbackPair] (FUniqueNetIdRef& QueryUserId)
					{
						return *QueryUserId == *IdCallbackPair.Key;
					};

				if (!UserIds.ContainsByPredicate(ContainsPred))
				{
					bIsOurQuery = false;
					break;
				}
			}

			if (bIsOurQuery)
			{
				// Notify users of the query completion
				for (const FUniqueNetIdRef& UserId : UserIds)
				{
					TSharedPtr<FOnlineUser> UserInfo = UserInterface->GetUserInfo(LocalUserNum, *UserId);
					for (auto& IdCallbackPair : CompletionCallbacksByUserId)
					{
						if (*UserId == *IdCallbackPair.Key)
						{
							IdCallbackPair.Value.ExecuteIfBound(SubsystemType, bWasSuccessful, UserInfo);
							break;
						}
					}
				}

				OnQueryCompleted.ExecuteIfBound(GetQueryId(), AsShared());
			}
		}
	}
};

//////////////////////////////////////////////////////////////////////////
// FSubsystemUserInfo
//////////////////////////////////////////////////////////////////////////

const FOnlineUserPresence* USocialUser::FSubsystemUserInfo::GetPresenceInfo() const
{
	if (IsFriend())
	{
		return &FriendInfo.Pin()->GetPresence();
	}
	return nullptr;
}

//////////////////////////////////////////////////////////////////////////
// USocialUser
//////////////////////////////////////////////////////////////////////////

USocialUser::USocialUser()
{}

void USocialUser::InitLocalUser()
{
	check(IsLocalUser());

	USocialToolkit& OwningToolkit = GetOwningToolkit();
	UE_LOG(LogParty, Log, TEXT("Initializing local SocialUser for Toolkit [%d]"), OwningToolkit.GetLocalUserNum());

	for (ESocialSubsystem SubsystemType : USocialManager::GetDefaultSubsystems())
	{
		IOnlineSubsystem* OSS = OwningToolkit.GetSocialOss(SubsystemType);
		check(OSS);

		if (IOnlineIdentityPtr IdentityInterface = OSS->GetIdentityInterface())
		{
			const int32 LocalUserNum = OwningToolkit.GetLocalUserNum();
			FUniqueNetIdRepl LocalUserSubsystemId = IdentityInterface->GetUniquePlayerId(LocalUserNum);
			if (IdentityInterface->GetLoginStatus(LocalUserNum) == ELoginStatus::LoggedIn && ensure(LocalUserSubsystemId.IsValid()))
			{
				SetSubsystemId(SubsystemType, LocalUserSubsystemId);
			}
			else
			{
				UE_LOG(LogParty, Warning, TEXT("Local SocialUser unable to establish a valid UniqueId on subsystem [%s]"), ToString(SubsystemType));
			}
		}
	}

	TryBroadcastInitializationComplete();
}

void USocialUser::Initialize(const FUniqueNetIdRepl& PrimaryId)
{
	check(PrimaryId.IsValid());
	
	if (ensure(SubsystemInfoByType.Num() == 0 && !bIsInitialized))
	{
		UE_LOG(LogParty, VeryVerbose, TEXT("Initializing new SocialUser with ID [%s]"), *PrimaryId.ToDebugString());

		//@todo DanH: Move out of this layer - currently it's possible when running replays w/ -nomcp to get in this situation. Make the FN team member deal with it instead.
		if (USocialManager::IsSocialSubsystemEnabled(ESocialSubsystem::Primary))
		{
			SetSubsystemId(ESocialSubsystem::Primary, PrimaryId);
			TryBroadcastInitializationComplete();
		}
		else
		{
			UE_LOG(LogParty, Error, TEXT("User cannot be initialized with ID [%s] - no primary OSS available."), *PrimaryId.ToDebugString());
		}
	}
}

void USocialUser::RegisterInitCompleteHandler(const FOnNewSocialUserInitialized& OnInitializationComplete)
{
	if (ensure(OnInitializationComplete.IsBound()))
	{
		if (bIsInitialized)
		{
			OnInitializationComplete.Execute(*this);
		}
		else
		{
			UserInitializedEvents.Add(OnInitializationComplete);
		}
	}
}

void USocialUser::ValidateFriendInfo(ESocialSubsystem SubsystemType)
{
	if (FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(SubsystemType))
	{
		if (!SubsystemInfo->FriendInfo.IsValid())
		{
			const USocialToolkit& OwningToolkit = GetOwningToolkit();
			if (IOnlineSubsystem* OSS = OwningToolkit.GetSocialOss(SubsystemType))
			{
				IOnlineFriendsPtr FriendsInterface = OSS->GetFriendsInterface();
				if (FriendsInterface.IsValid())
				{
					SubsystemInfo->FriendInfo = FriendsInterface->GetFriend(OwningToolkit.GetLocalUserNum(), *SubsystemInfo->GetUserId(), EFriendsLists::ToString(EFriendsLists::Default));
				}
			}
		}
	}
}

TArray<ESocialSubsystem> USocialUser::GetRelationshipSubsystems(ESocialRelationship Relationship) const
{
	static TArray<ESocialSubsystem> RelationshipSubsystems;
	RelationshipSubsystems.Reset();

	if (Relationship == ESocialRelationship::PartyInvite)
	{
		if (HasSentPartyInvite(IOnlinePartySystem::GetPrimaryPartyTypeId()))
		{
			RelationshipSubsystems.Add(ESocialSubsystem::Primary);
		}
	}
	else if (Relationship == ESocialRelationship::JoinRequest)
	{
		if (HasRequestedToJoinUs())
		{
			RelationshipSubsystems.Add(ESocialSubsystem::Primary);
		}
	}
	else
	{
		for (const TPair<ESocialSubsystem, FSubsystemUserInfo>& SubsystemInfoPair : SubsystemInfoByType)
		{
			switch (Relationship)
			{
			case ESocialRelationship::Any:
				if (!IsLocalUser())
				{
					RelationshipSubsystems.Add(SubsystemInfoPair.Key);
				}
				break;
			case ESocialRelationship::FriendInviteReceived:
				if (SubsystemInfoPair.Value.GetFriendInviteStatus() == EInviteStatus::PendingInbound)
				{
					RelationshipSubsystems.Add(SubsystemInfoPair.Key);
				}
				break;
			case ESocialRelationship::FriendInviteSent:
				if (SubsystemInfoPair.Value.GetFriendInviteStatus() == EInviteStatus::PendingOutbound)
				{
					RelationshipSubsystems.Add(SubsystemInfoPair.Key);
				}
				break;
			case ESocialRelationship::SuggestedFriend:
				if (SubsystemInfoPair.Value.GetFriendInviteStatus() == EInviteStatus::Suggested)
				{
					RelationshipSubsystems.Add(SubsystemInfoPair.Key);
				}
				break;
			case ESocialRelationship::Friend:
				if (SubsystemInfoPair.Value.IsFriend())
				{
					RelationshipSubsystems.Add(SubsystemInfoPair.Key);
				}
				break;
			case ESocialRelationship::BlockedPlayer:
				if (SubsystemInfoPair.Value.IsBlocked())
				{
					RelationshipSubsystems.Add(SubsystemInfoPair.Key);
				}
				break;
			case ESocialRelationship::RecentPlayer:
				if (SubsystemInfoPair.Value.RecentPlayerInfo.IsValid() && !IsFriend())
				{
					RelationshipSubsystems.Add(SubsystemInfoPair.Key);
				}
				break;
			}
		}
	}

	return RelationshipSubsystems;
}

bool USocialUser::IsLocalUser() const
{
	return &GetOwningToolkit().GetLocalUser() == this;
}

bool USocialUser::HasNetId(const FUniqueNetIdRepl& UniqueId) const
{
	return GetOwningToolkit().FindUser(UniqueId) == this;
}

USocialToolkit& USocialUser::GetOwningToolkit() const
{
	return *CastChecked<USocialToolkit>(GetOuter());
}

EOnlinePresenceState::Type USocialUser::GetOnlineStatus() const
{
	if (IsLocalUser())
	{
		// FSubsystemUserInfo can only access presence on friends
		// Use the Toolkit to read self presence
		if (const FOnlineUserPresence* LocalPresenceInfo = GetOwningToolkit().GetPresenceInfo(ESocialSubsystem::Primary))
		{
			return LocalPresenceInfo->Status.State;
		}
		return EOnlinePresenceState::Offline;
	}

#if WITH_EDITOR
	if (bDebug_IsPresenceArtificial && !IsBlocked())
	{
		return Debug_RandomPresence;
	}
#endif

	EOnlinePresenceState::Type OnlineStatus = EOnlinePresenceState::Offline;

	// Get the most "present" status available on any of the associated platforms
	for (const TPair<ESocialSubsystem, FSubsystemUserInfo>& SubsystemInfoPair : SubsystemInfoByType)
	{
		if (const FOnlineUserPresence* PresenceInfo = SubsystemInfoPair.Value.GetPresenceInfo())
		{
			if (OnlineStatus == EOnlinePresenceState::Offline || PresenceInfo->Status.State == EOnlinePresenceState::Online ||
				(PresenceInfo->Status.State == EOnlinePresenceState::Away && OnlineStatus != EOnlinePresenceState::Online))
			{
				// Either the best we have is offline, or the new one is either online or away (if necessary we can get into the weeds of prioritizing the other states)
				OnlineStatus = PresenceInfo->Status.State;
			}
		}
	}

	return OnlineStatus;
}

UE::Online::EUserPresenceStatus USocialUser::GetOnlineStatusV2() const
{
	using namespace UE::Online;
	using namespace UE::OnlineFramework;

	EUserPresenceStatus Result = EUserPresenceStatus::Unknown;

	if (IsLocalUser())
	{
		TOnlineResult<FGetCachedPresence> GetCachedPresenceResult = GetOwningToolkit().GetPresenceInfoV2(ESocialSubsystem::Primary);
		if (GetCachedPresenceResult.IsOk())
		{
			Result = GetCachedPresenceResult.GetOkValue().Presence->Status;
		}

		return Result;
	}

#if WITH_EDITOR
	if (bDebug_IsPresenceArtificial && !IsBlocked())
	{
		return Debug_RandomPresenceV2;
	}
#endif

	Result = EUserPresenceStatus::Offline;

	const FName& PresenceFrameworkInstance = USocialManager::GetPresenceFrameworkInstance();
	FCommonConfig CommonConfig(this);
	if (IOnlineServicesPtr OnlineServices = CommonConfig.GetServices(PresenceFrameworkInstance))
	{
		if (IPresencePtr Presence = OnlineServices->GetPresenceInterface())
		{
			// Get the most "present" status available on any of the associated platforms
			for (const TPair<ESocialSubsystem, FSubsystemUserInfo>& SubsystemInfoPair : SubsystemInfoByType)
			{
				const FUniqueNetIdRepl& LocalUserNetId = GetOwningToolkit().GetLocalUserNetId(SubsystemInfoPair.Key);
				if(LocalUserNetId.IsValid())
				{
					if (const FCommonAccountPtr LocalUserCommonAccountPtr = GetCommonAccountFromV1(CommonConfig, LocalUserNetId->AsShared(), GetFirstFrameworkInstanceName(LocalUserNetId)))
					{
						FAccountId LocalUserAccountId = LocalUserCommonAccountPtr->GetId(PresenceFrameworkInstance);

						const FUniqueNetIdRepl& UserId = SubsystemInfoPair.Value.GetUserId();

						if (const FCommonAccountPtr CommonAccountPtr = GetCommonAccountFromV1(CommonConfig, UserId->AsShared(), GetFirstFrameworkInstanceName(UserId)))
						{
							FAccountId AccountId = CommonAccountPtr->GetId(PresenceFrameworkInstance);

							TOnlineResult<FGetCachedPresence> GetCachedPresenceResult = Presence->GetCachedPresence({ LocalUserAccountId, AccountId });
							if (GetCachedPresenceResult.IsOk())
							{
								EUserPresenceStatus CachedPresenceStatus = GetCachedPresenceResult.GetOkValue().Presence->Status;

								if (Result == EUserPresenceStatus::Offline || CachedPresenceStatus == EUserPresenceStatus::Online ||
								(CachedPresenceStatus == EUserPresenceStatus::Away && Result != EUserPresenceStatus::Online))
								{
									// Either the best we have is offline, or the new one is either online or away (if necessary we can get into the weeds of prioritizing the other states)
									Result = CachedPresenceStatus;
								}
							}
						}
					}
				}
			}
		}
	}

	return Result;
}

void USocialUser::TryBroadcastInitializationComplete()
{
	if (!bIsInitialized && NumPendingQueries == 0)
	{
		// We consider a social user to be initialized when it has valid primary OSS user info and no pending queries
		const FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(ESocialSubsystem::Primary);
		if (SubsystemInfo)
		{
			if (SubsystemInfo->UserInfo.IsValid())
			{
#if !UE_BUILD_SHIPPING
				if (FDebugInitializer::IsEnabled())
				{
					DebugInitializer = MakeUnique<FDebugInitializer>(*this);
					DebugInitializer->FinishInitialization();
				}
				else
#endif
				{
					FinishInitialization();
				}
			}
			else
			{
				//ensureAlwaysMsgf(!IsLocalUser(), TEXT("%s LocalUser %s does not have UserInfo!"), ANSI_TO_TCHAR(__FUNCTION__), *ToDebugString());
				UE_LOG(LogParty, Warning, TEXT("%s User %s does not have UserInfo!"), ANSI_TO_TCHAR(__FUNCTION__), *ToDebugString());

				// User is invalid with no open queries
				// Assume that this means the sought user doesn't exist
				UserInitializedEvents.Reset();

				// Remove Toolkit's reference to the SocialUser, GC will clean it
				GetOwningToolkit().HandleUserInvalidated(*this);
			}
		}
	}
}

void USocialUser::FinishInitialization()
{
	UE_LOG(LogParty, VeryVerbose, TEXT("SocialUser [%s] fully initialized."), *ToDebugString());
	bIsInitialized = true;
#if !UE_BUILD_SHIPPING
	DebugInitializer.Reset();
#endif

	TArray<FOnNewSocialUserInitialized> InitEvents = MoveTemp(UserInitializedEvents);
	UserInitializedEvents.Reset();
	for (FOnNewSocialUserInitialized& InitEvent : InitEvents)
	{
		InitEvent.ExecuteIfBound(*this);
	}
}

USocialUser::FSubsystemUserInfo& USocialUser::FindOrCreateSubsystemInfo(const FUniqueNetIdRepl& SubsystemId, ESocialSubsystem SubsystemType)
{
	FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(SubsystemType);
	if (!SubsystemInfo)
	{
		SubsystemInfo = &SubsystemInfoByType.Add(SubsystemType, FSubsystemUserInfo(SubsystemId));
	}
	// Make damn sure we never try to create subsystem info with an ID that doesn't match what's already there
	check(SubsystemId == SubsystemInfo->GetUserId());

	return *SubsystemInfo;
}

bool USocialUser::ShouldReadUserInfoFromOnlineServices() const
{
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("onlineframework.party.shouldreaduserinfofromonlineservices")))
	{
		return CVar->GetBool();
	}

	return false;
}

FString USocialUser::GetDisplayName() const
{
	FString DisplayName;

	if(const FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(ESocialSubsystem::Primary))
	{
		// We always retrieve the V1 name during the transition period, for the redundancy check
		if (TSharedPtr<FOnlineUser> PrimaryUserInfo = SubsystemInfo->UserInfo.Pin())
		{
			// The primary user info has knowledge of display names on all linked accounts
			const FUserPlatform& UserCurrentPlatform = GetCurrentPlatform();
			DisplayName = PrimaryUserInfo->GetDisplayName(UserCurrentPlatform.ToString());
		}
		else
		{
			// We don't have primary user info (so we're not even initialized yet!), but a good-faith effort is to see if we have a platform name
			DisplayName = GetDisplayName(ESocialSubsystem::Platform);
		}

		if (ShouldReadUserInfoFromOnlineServices())
		{
			using namespace UE::Online;
			using namespace UE::OnlineFramework;

			const FName& PresenceFrameworkInstance = USocialManager::GetPresenceFrameworkInstance();
			FCommonConfig CommonConfig(this);

			const FUniqueNetIdRepl& LocalUserNetId = GetOwningToolkit().GetLocalUserNetId(ESocialSubsystem::Primary);
			if (const FCommonAccountPtr LocalUserCommonAccountPtr = GetCommonAccountFromV1(CommonConfig, LocalUserNetId->AsShared(), GetFirstFrameworkInstanceName(LocalUserNetId)))
			{
				FAccountId LocalUserAccountId = LocalUserCommonAccountPtr->GetId(PresenceFrameworkInstance);

				const FUniqueNetIdRepl& SocialUserId = SubsystemInfo->GetUserId();

				if (const FCommonAccountPtr SocialUserCommonAccountPtr = GetCommonAccountFromV1(CommonConfig, SocialUserId->AsShared(), GetFirstFrameworkInstanceName(SocialUserId)))
				{
					FAccountId SocialUserAccountId = SocialUserCommonAccountPtr->GetId(PresenceFrameworkInstance);

					if (IOnlineServicesPtr OnlineServices = CommonConfig.GetServices(PresenceFrameworkInstance))
					{
						// We get the user platform
						EOnlinePlatformType OnlinePlatformType = EOnlinePlatformType::Unknown;
						if (IPresencePtr Presence = OnlineServices->GetPresenceInterface())
						{
							TOnlineResult<FGetCachedPresence> GetCachedPresenceResult = Presence->GetCachedPresence({ LocalUserAccountId, SocialUserAccountId });
							if (GetCachedPresenceResult.IsOk())
							{
								OnlinePlatformType = GetCachedPresenceResult.GetOkValue().Presence->PlatformType;
							}
						}

						if (IUserInfoPtr UserInfo = OnlineServices->GetUserInfoInterface())
						{
							FString DisplayNameV2;

							TOnlineResult<FGetUserInfo> GetUserInfoResult = UserInfo->GetUserInfo({ LocalUserAccountId, SocialUserAccountId, OnlinePlatformType });
							if (GetUserInfoResult.IsOk())
							{
								DisplayNameV2 = GetUserInfoResult.GetOkValue().UserInfo->DisplayName;
							}
							else
							{
								// We don't have primary user info (so we're not even initialized yet!), but a good-faith effort is to see if we have a platform name
								DisplayNameV2 = GetDisplayName(ESocialSubsystem::Platform);
							}
#if !UE_BUILD_SHIPPING
							// Temporary redundancy check
							if (DisplayName != DisplayNameV2)
							{
								UE_LOG(LogParty, Warning, TEXT("DisplayName disparity: DisplayNameV1: %s. DisplayNameV2: %s"), *DisplayName, *DisplayNameV2);
							}
#endif
							if (!DisplayNameV2.IsEmpty())
							{
								DisplayName = DisplayNameV2;
							}
						}
					}
				}
			}
		}
	}

	return DisplayName;
}

FString USocialUser::GetDisplayName(ESocialSubsystem SubsystemType) const
{
	FString DisplayName;

	if (const FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(SubsystemType))
	{
		if (TSharedPtr<FOnlineUser> PrimaryUserInfo = SubsystemInfo->UserInfo.Pin())
		{
			DisplayName = PrimaryUserInfo->GetDisplayName();

			if (ShouldReadUserInfoFromOnlineServices() && SubsystemType == ESocialSubsystem::Primary) // We don't want V2 functionality in Platform for now
			{
				using namespace UE::Online;
				using namespace UE::OnlineFramework;

				const FName& PresenceFrameworkInstance = USocialManager::GetPresenceFrameworkInstance();
				FCommonConfig CommonConfig(this);

				const FUniqueNetIdRepl& LocalUserNetId = GetOwningToolkit().GetLocalUserNetId(ESocialSubsystem::Primary);
				if (const FCommonAccountPtr LocalUserCommonAccountPtr = GetCommonAccountFromV1(CommonConfig, LocalUserNetId->AsShared(), GetFirstFrameworkInstanceName(LocalUserNetId)))
				{
					FAccountId LocalUserAccountId = LocalUserCommonAccountPtr->GetId(PresenceFrameworkInstance);

					const FUniqueNetIdRepl& SocialUserId = SubsystemInfo->GetUserId();

					if (const FCommonAccountPtr SocialUserCommonAccountPtr = GetCommonAccountFromV1(CommonConfig, SocialUserId->AsShared(), GetFirstFrameworkInstanceName(SocialUserId)))
					{
						FAccountId SocialUserAccountId = SocialUserCommonAccountPtr->GetId(PresenceFrameworkInstance);

						if (IOnlineServicesPtr OnlineServices = CommonConfig.GetServices(PresenceFrameworkInstance))
						{
							if (IUserInfoPtr UserInfo = OnlineServices->GetUserInfoInterface())
							{
								TOnlineResult<FGetUserInfo> GetUserInfoResult = UserInfo->GetUserInfo({ LocalUserAccountId, SocialUserAccountId });
								if (GetUserInfoResult.IsOk())
								{
									FString DisplayNameV2 = GetUserInfoResult.GetOkValue().UserInfo->DisplayName;
#if !UE_BUILD_SHIPPING
									// Temporary redundancy check
									if (DisplayName != DisplayNameV2)
									{
										UE_LOG(LogParty, Warning, TEXT("DisplayName disparity: DisplayNameV1: %s. DisplayNameV2: %s"), *DisplayName, *DisplayNameV2);
									}
#endif
									if (!DisplayNameV2.IsEmpty())
									{
										DisplayName = DisplayNameV2;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	return DisplayName;
}

FString USocialUser::GetNickname() const
{
	if (IsFriend())
	{
#if WITH_EDITOR
		bool bRandomNickname = FParse::Param(FCommandLine::Get(), TEXT("RandomNickname"));
		if (bRandomNickname && !IsLocalUser())
		{
			if (GetTypeHash(GetUserId(ESocialSubsystem::Primary)) % 2)
			{
				FString RandomNickname = GetDisplayName(ESocialSubsystem::Primary);
				for (int i = 1; i < RandomNickname.Len(); i += 2)
				{
					RandomNickname.RemoveAt(i);
				}
				return RandomNickname;
			}
		}
#endif

		if (const FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(ESocialSubsystem::Primary))
		{
			if (SubsystemInfo->FriendInfo.IsValid())
			{
				TSharedPtr<FOnlineFriend> OnlineFriend = SubsystemInfo->FriendInfo.Pin();
				FString Nickname;
				OnlineFriend->GetUserAttribute(USER_ATTR_ALIAS, Nickname);
				return Nickname;
			}
		}
	}
	return TEXT("");
}

bool USocialUser::SetNickname(const FString& InNickname)
{
	if (IsFriend())
	{
		IOnlineFriendsPtr FriendsInterface = GetOwningToolkit().GetSocialOss(ESocialSubsystem::Primary)->GetFriendsInterface();
		check(FriendsInterface.IsValid());

		if (!InNickname.IsEmpty())
		{
			FriendsInterface->SetFriendAlias(GetOwningToolkit().GetLocalUserNum(), *GetUserId(ESocialSubsystem::Primary), EFriendsLists::ToString(EFriendsLists::Default), InNickname, FOnSetFriendAliasComplete::CreateUObject(const_cast<USocialUser*>(this), &USocialUser::HandleSetNicknameComplete));
			return true;
		}
		else if (!GetNickname().IsEmpty())
		{
			FriendsInterface->DeleteFriendAlias(GetOwningToolkit().GetLocalUserNum(), *GetUserId(ESocialSubsystem::Primary), EFriendsLists::ToString(EFriendsLists::Default), FOnDeleteFriendAliasComplete::CreateUObject(const_cast<USocialUser*>(this), &USocialUser::HandleSetNicknameComplete));
			return true;
		}
	}
	OnSetNicknameCompleted().Broadcast(FText::GetEmpty());
	return false;
}

EInviteStatus::Type USocialUser::GetFriendInviteStatus(ESocialSubsystem SubsystemType) const
{
	if (const FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(SubsystemType))
	{
		return SubsystemInfo->GetFriendInviteStatus();
	}
	return EInviteStatus::Unknown;
}

bool USocialUser::IsFriend(ESocialSubsystem SubsystemType) const
{
	if (!IsBlocked())
	{
		if (const FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(SubsystemType))
		{
			return SubsystemInfo->IsFriend();
		}
	}
	return false;
}

bool USocialUser::IsFriend() const
{
	if (!IsBlocked())
	{
		for (const TPair<ESocialSubsystem, FSubsystemUserInfo>& SubsystemInfoPair : SubsystemInfoByType)
		{
			if (SubsystemInfoPair.Value.IsFriend())
			{
				return true;
			}
		}
	}
	return false;
}

bool USocialUser::IsFriendshipPending(ESocialSubsystem SubsystemType) const
{
	const EInviteStatus::Type FriendInviteStatus = GetFriendInviteStatus(SubsystemType);
	return FriendInviteStatus == EInviteStatus::PendingInbound || FriendInviteStatus == EInviteStatus::PendingOutbound;
}

bool USocialUser::IsAnyInboundFriendshipPending() const
{
	for (const TPair<ESocialSubsystem, FSubsystemUserInfo>& SubsystemInfoPair : SubsystemInfoByType)
	{
		if (const FOnlineFriend* OnlineFriend = SubsystemInfoPair.Value.FriendInfo.Pin().Get())
		{
			const EInviteStatus::Type FriendInviteStatus = OnlineFriend->GetInviteStatus();
			if (FriendInviteStatus == EInviteStatus::PendingInbound)
			{
				return true;
			}
		}
	}
	return false;
}

const FOnlineUserPresence* USocialUser::GetFriendPresenceInfo(ESocialSubsystem SubsystemType) const
{
	const FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(SubsystemType);
	if (const FOnlineUserPresence* PresenceInfo = SubsystemInfo ? SubsystemInfo->GetPresenceInfo() : nullptr)
	{
		return PresenceInfo;
	}
	else if (IsLocalUser())
	{
		IOnlineSubsystem* SocialOss = GetOwningToolkit().GetSocialOss(SubsystemType);
		if (IOnlinePresencePtr PresenceInterface = SocialOss ? SocialOss->GetPresenceInterface() : nullptr)
		{
			TSharedPtr<FOnlineUserPresence> LocalUserPresence;
			const FUniqueNetIdRepl& UserId = GetUserId(SubsystemType);
			if (UserId.IsValid() && PresenceInterface->GetCachedPresence(*UserId, LocalUserPresence) == EOnlineCachedResult::Success)
			{
				return LocalUserPresence.Get();
			}
		}
	}
	return nullptr;
}

UE::Online::TOnlineResult<UE::Online::FGetCachedPresence> USocialUser::GetFriendPresenceInfoV2(ESocialSubsystem SubsystemType) const
{	
	using namespace UE::Online;
	using namespace UE::OnlineFramework;

	TOnlineResult<FGetCachedPresence> Result(Errors::NotFound());

	FAccountId LocalUserAccountId, FriendAccountId;

	const FName& PresenceFrameworkInstance = USocialManager::GetPresenceFrameworkInstance();
	FCommonConfig CommonConfig(this);
	FUniqueNetIdRepl LocalUserNetId = GetOwningToolkit().GetLocalUserNetId(SubsystemType);
	if (const FCommonAccountPtr LocalUserCommonAccountPtr = GetCommonAccountFromV1(CommonConfig, LocalUserNetId->AsShared(), GetFirstFrameworkInstanceName(LocalUserNetId)))
	{
		LocalUserAccountId = LocalUserCommonAccountPtr->GetId(PresenceFrameworkInstance);
	}

	if(const FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(SubsystemType))
	{
		FUniqueNetIdRepl FriendUserId = SubsystemInfo->GetUserId();

		if (const FCommonAccountPtr FriendCommonAccountPtr = GetCommonAccountFromV1(CommonConfig, FriendUserId->AsShared(), GetFirstFrameworkInstanceName(FriendUserId)))
		{
			FriendAccountId = FriendCommonAccountPtr->GetId(PresenceFrameworkInstance);
		}
	}

	if(LocalUserAccountId.IsValid() && FriendAccountId.IsValid())
	{
		if (IOnlineServicesPtr OnlineServices = CommonConfig.GetServices(PresenceFrameworkInstance))
		{
			if (IPresencePtr Presence = OnlineServices->GetPresenceInterface())
			{
				FGetCachedPresence::Params Params = { LocalUserAccountId, FriendAccountId };

				Result = Presence->GetCachedPresence(MoveTemp(Params));
			}
		}
	}

	return Result;
}

FDateTime USocialUser::GetFriendshipCreationDate() const
{
	if (const FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(ESocialSubsystem::Primary))
	{
		if (SubsystemInfo->FriendInfo.IsValid())
		{
			TSharedPtr<FOnlineFriend> OnlineFriend = SubsystemInfo->FriendInfo.Pin();
			FString FriendshipCreationDateString;
			if (OnlineFriend->GetUserAttribute(TEXT("created"), FriendshipCreationDateString))
			{
				FDateTime CreatedDate;
				if (ensure(FDateTime::ParseIso8601(*FriendshipCreationDateString, CreatedDate)))
				{
					return CreatedDate;
				}
			}
		}
	}

	return FDateTime::MaxValue();
}

FDateTime USocialUser::GetLastOnlineDate() const
{
	if (IsFriend(ESocialSubsystem::Primary))
	{
		const FOnlineUserPresence* PrimaryPresence = GetFriendPresenceInfo(ESocialSubsystem::Primary);
		if (PrimaryPresence)
		{
			return PrimaryPresence->LastOnline;
		}
	}

	return FDateTime::MaxValue();
}

FText USocialUser::GetSocialName() const
{
	if (ensure(IsFriend(ESocialSubsystem::Primary)))
	{
		if (const FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(ESocialSubsystem::Primary))
		{
			if (SubsystemInfo->FriendInfo.IsValid())
			{
				TSharedPtr<FOnlineFriend> OnlineFriend = SubsystemInfo->FriendInfo.Pin();
				FString FriendSocialName;
				OnlineFriend->GetUserAttribute(TEXT("socialname:facebook"), FriendSocialName);

				if (!FriendSocialName.IsEmpty())
				{
					return FText::FromString(FriendSocialName);
				}
			}
		}
	}

	return FText::GetEmpty();
}

FUserPlatform USocialUser::GetCurrentPlatform() const
{
	FString PlatformString;

	// Local user is obviously on the local platform
	if (IsLocalUser())
	{
		PlatformString = IOnlineSubsystem::GetLocalPlatformName();
	}
	else
	{
		bool bIsPrimaryPresenceValid = false;
		bool bIsPrimaryPresenceUserOnline = false;
		bool bIsPrimariPresenceUserPlayingThisGame = false;
		FString PrimaryPresencePlatformStr;

		if (ShouldReadUserInfoFromOnlineServices())
		{
			using namespace UE::Online;

			const TOnlineResult<FGetCachedPresence>& GetCachedPresenceResult = GetFriendPresenceInfoV2(ESocialSubsystem::Primary);
			if (GetCachedPresenceResult.IsOk())
			{
				const TSharedRef<const FUserPresence> CachedPresence = GetCachedPresenceResult.GetOkValue().Presence;

				bIsPrimaryPresenceValid = true;
				bIsPrimaryPresenceUserOnline = CachedPresence->Status != UE::Online::EUserPresenceStatus::Offline;
				bIsPrimariPresenceUserPlayingThisGame = CachedPresence->GameStatus == UE::Online::EUserPresenceGameStatus::PlayingThisGame;
				PrimaryPresencePlatformStr = LexToString(CachedPresence->PlatformType);
			}
		}
		else
		{
			// "Current" in the function name isn't a misnomer - it is possible for a user to log in and out of multiple accounts while maintaining just 1 (or 0) that is actually playing the same game.
			const FOnlineUserPresence* PrimaryPresence = GetFriendPresenceInfo(ESocialSubsystem::Primary);

			bIsPrimaryPresenceValid = PrimaryPresence != nullptr;
			if (bIsPrimaryPresenceValid)
			{
				bIsPrimaryPresenceUserOnline = PrimaryPresence->bIsOnline;
				bIsPrimariPresenceUserPlayingThisGame = PrimaryPresence->bIsPlayingThisGame;
				PrimaryPresencePlatformStr = PrimaryPresence->GetPlatform();
			}
		}

		const FOnlineUserPresence* PlatformPresence = GetFriendPresenceInfo(ESocialSubsystem::Platform);

// jniman: Switch and Switch2 must be differentiated from one another, but they have the same OSS, so some of these checks will return bad results and cannot be ran on these platforms
// jntodo: this will need to get moved to a platform-specific uplugin once we have the time
#if !((defined(PLATFORM_SWITCH) && PLATFORM_SWITCH) || (defined(PLATFORM_SWITCH2) && PLATFORM_SWITCH2))
		if (PlatformPresence && PlatformPresence->bIsOnline && PlatformPresence->bIsPlayingThisGame)
		{
			// Platform friends that are playing the same game are on the local platform
			PlatformString = IOnlineSubsystem::GetLocalPlatformName();
		}
		else if (bIsPrimaryPresenceValid && bIsPrimaryPresenceUserOnline && bIsPrimariPresenceUserPlayingThisGame)
		{
			// Respect the current platform reported by the primary presence if the user is playing the same game
			PlatformString = PrimaryPresencePlatformStr;
		}
		else if (PlatformPresence && PlatformPresence->bIsOnline)
		{
			// Not playing the same game on either account, but we have presence on the platform, so let that win regardless of whether the primary is valid
			PlatformString = IOnlineSubsystem::GetLocalPlatformName();
		}
		else 
#endif //!PLATFORM_NINTENDO
		if (bIsPrimaryPresenceValid && bIsPrimaryPresenceUserOnline)
		{
			// We have no platform presence, but we do have primary, so get the platform from that
			PlatformString = PrimaryPresencePlatformStr;
		}
		else
		{
			UE_LOG(LogOnline, VeryVerbose, TEXT("%s - No Presence found for user, searching Party..."), ANSI_TO_TCHAR(__FUNCTION__));
			if (USocialParty* Party = GetOwningToolkit().GetSocialManager().GetPersistentParty())
			{
				if (UPartyMember* PartyMember = Party->GetPartyMember(GetUserId(ESocialSubsystem::Primary)))
				{
					const FUserPlatform& PartyMemberPlatform = PartyMember->GetRepData().GetPlatformDataPlatform();
					UE_LOG(LogOnline, VeryVerbose, TEXT("%s - Party Member Found for user! RepDataPlatform: %s"), ANSI_TO_TCHAR(__FUNCTION__), *PartyMemberPlatform.ToString());
					if (PartyMemberPlatform.IsValid())
					{
						PlatformString = PartyMemberPlatform;
					}
				}
			}

			if (PlatformString.IsEmpty())
			{
				// We don't have any presence for this user (or we do and they're offline) and they aren't the local player, so we really don't have any idea what their current platform is
				// Intentionally empty (besides comments)
			}
		}
	}

	FUserPlatform UserPlatform;
	if (!PlatformString.IsEmpty())
	{
		UserPlatform = MoveTemp(PlatformString);
	}

	return UserPlatform;
}

FString USocialUser::GetPlatformIconMarkupTag(EPlatformIconDisplayRule DisplayRule) const
{
	FString DummyString;
	return GetPlatformIconMarkupTag(DisplayRule, DummyString);
}

FString USocialUser::GetPlatformIconMarkupTag(EPlatformIconDisplayRule DisplayRule, FString& OutLegacyString) const
{
	const FUserPlatform UserPlatform = GetCurrentPlatform();
	const FUserPlatform LocalPlatform = FUserPlatform(IOnlineSubsystem::GetLocalPlatformName());

	OutLegacyString = UserPlatform;
	FString MarkupTag = GetMarkupTagForPlatform(UserPlatform);

	switch (DisplayRule)
	{
	case EPlatformIconDisplayRule::Always:
		UE_LOG(LogOnline, VeryVerbose, TEXT("    %s - User: %s - Returning Platform Tag %s due to ALWAYS"), ANSI_TO_TCHAR(__FUNCTION__), *GetDisplayName(), *MarkupTag);
		return MarkupTag;
	case EPlatformIconDisplayRule::AlwaysIfDifferent:
		UE_LOG(LogOnline, VeryVerbose, TEXT("    %s - User: %s - CrossplayLocalPlatform? %d Returning %s due to ALWAYSIFDIFFERENT"), ANSI_TO_TCHAR(__FUNCTION__), *GetDisplayName(), UserPlatform.IsCrossplayWithLocalPlatform(), UserPlatform.IsCrossplayWithLocalPlatform() ? *MarkupTag : TEXT(""));
		if (!UserPlatform.IsCrossplayWithLocalPlatform())
		{
			OutLegacyString = TEXT("");
			return TEXT("");
		}
		return MarkupTag;
	case EPlatformIconDisplayRule::Never:
		UE_LOG(LogOnline, VeryVerbose, TEXT("    %s - User: %s - Returning nothing due to NEVER"), ANSI_TO_TCHAR(__FUNCTION__), *GetDisplayName());
		OutLegacyString = TEXT("");
		return TEXT("");
	}

	if (DisplayRule == EPlatformIconDisplayRule::AlwaysWhenInCrossplayParty ||
		DisplayRule == EPlatformIconDisplayRule::AlwaysIfDifferentWhenInCrossplayParty)
	{
		if (USocialParty* Party = GetOwningToolkit().GetSocialManager().GetPersistentParty())
		{
			if (!Party->IsCurrentlyCrossplaying())
			{
				UE_LOG(LogOnline, VeryVerbose, TEXT("    %s - User: %s - Returning nothing due to Not Being In Crossplay with WHENINCROSSPLAYPARTY specified"), ANSI_TO_TCHAR(__FUNCTION__), *GetDisplayName());
				OutLegacyString = TEXT("");
				return TEXT("");
			}
		}
	}

	const bool bIsCrossplayWithMe = UserPlatform.IsCrossplayWithLocalPlatform();
	if (DisplayRule == EPlatformIconDisplayRule::AlwaysWhenInCrossplayParty ||
		(DisplayRule == EPlatformIconDisplayRule::AlwaysIfDifferentWhenInCrossplayParty && bIsCrossplayWithMe))
	{
		UE_LOG(LogOnline, VeryVerbose, TEXT("    %s - User: %s - Returning Platform Tag %s, DisplayRule: %d, bIsCrossplayWithMe: %d"), ANSI_TO_TCHAR(__FUNCTION__), *GetDisplayName(), *MarkupTag, (int32)DisplayRule, bIsCrossplayWithMe);
		return MarkupTag;
	}

	UE_LOG(LogOnline, VeryVerbose, TEXT("    %s - User: %s - Returning nothing, likely due to AlwaysIfDifferentWhenInCrossplayParty and not being different, being in a Crossplay Party, and not being Different"), ANSI_TO_TCHAR(__FUNCTION__), *GetDisplayName());
	OutLegacyString = TEXT("");
	return TEXT("");
}

void USocialUser::GetRichPresenceText(FText& OutRichPresence) const
{
	if (IsBlocked())
	{
		OutRichPresence = LOCTEXT("UserStatus_Blocked", "Blocked");
	}
	else if (IsFriend() || IsLocalUser())
	{
		bool bIsPrimaryPresenceStatusStrValid = false;
		if (ShouldReadUserInfoFromOnlineServices())
		{
			using namespace UE::Online;

			TOnlineResult<FGetCachedPresence> GetCachedPresenceResult = GetFriendPresenceInfoV2(ESocialSubsystem::Primary);
			if (GetCachedPresenceResult.IsOk() && !GetCachedPresenceResult.GetOkValue().Presence->RichPresenceString.IsEmpty())
			{
				OutRichPresence = FText::FromString(SanitizePresenceString(GetCachedPresenceResult.GetOkValue().Presence->RichPresenceString));
				bIsPrimaryPresenceStatusStrValid = true;
			}
		}
		else
		{
			const FOnlineUserPresence* PrimaryPresence = GetFriendPresenceInfo(ESocialSubsystem::Primary);
			if (PrimaryPresence && !PrimaryPresence->Status.StatusStr.IsEmpty())
			{
				OutRichPresence = FText::FromString(SanitizePresenceString(PrimaryPresence->Status.StatusStr));
				bIsPrimaryPresenceStatusStrValid = true;
			}
		}
		
		if(!bIsPrimaryPresenceStatusStrValid)
		{
			const FOnlineUserPresence* PlatformPresence = GetFriendPresenceInfo(ESocialSubsystem::Platform);
			if (PlatformPresence && !PlatformPresence->Status.StatusStr.IsEmpty())
			{
				OutRichPresence = FText::FromString(SanitizePresenceString(PlatformPresence->Status.StatusStr)); 
			}
			else
			{
				if (ShouldReadUserInfoFromOnlineServices())
				{
					OutRichPresence = ToLocText(GetOnlineStatusV2());
				}
				else
				{
					OutRichPresence = EOnlinePresenceState::ToLocText(GetOnlineStatus());
				}
			}
		}
	}
}

bool USocialUser::IsRecentPlayer(ESocialSubsystem SubsystemType) const
{
	if (const FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(SubsystemType))
	{
		return SubsystemInfo->RecentPlayerInfo.IsValid();
	}
	return false;
}

bool USocialUser::IsRecentPlayer() const
{
	for (const TPair<ESocialSubsystem, FSubsystemUserInfo>& SubsystemInfoPair : SubsystemInfoByType)
	{
		if (SubsystemInfoPair.Value.RecentPlayerInfo.IsValid())
		{
			return true;
		}
	}
	return false;
}

bool USocialUser::IsBlocked(ESocialSubsystem SubsystemType) const
{
	if (const FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(SubsystemType))
	{
		return SubsystemInfo->IsBlocked();
	}
	return false;
}

bool USocialUser::IsBlocked() const
{
	for (const TPair<ESocialSubsystem, FSubsystemUserInfo>& SubsystemInfoPair : SubsystemInfoByType)
	{
		if (SubsystemInfoPair.Value.IsBlocked())
		{
			return true;
		}
	}
	return false;
}

bool USocialUser::IsOnline() const
{
	// If any presence says we're online, count it (note also that only friends have presence info, so non-friends will always count as offline)
	for (const TPair<ESocialSubsystem, FSubsystemUserInfo>& SubsystemInfoPair : SubsystemInfoByType)
	{
		if (ShouldReadUserInfoFromOnlineServices())
		{
			using namespace UE::Online;
			using namespace UE::OnlineFramework;

			FAccountId LocalUserAccountId, TargetAccountId;

			const FName& PresenceFrameworkInstance = USocialManager::GetPresenceFrameworkInstance();
			FCommonConfig CommonConfig(this);
			const FUniqueNetIdRepl& LocalUserNetId = GetOwningToolkit().GetLocalUserNetId(SubsystemInfoPair.Key);
			if (LocalUserNetId.IsValid())
			{
				if (const FCommonAccountPtr LocalUserCommonAccountPtr = GetCommonAccountFromV1(CommonConfig, LocalUserNetId->AsShared(), GetFirstFrameworkInstanceName(LocalUserNetId)))
				{
					LocalUserAccountId = LocalUserCommonAccountPtr->GetId(PresenceFrameworkInstance);
				}
			}

			const FUniqueNetIdRepl& UserId = SubsystemInfoPair.Value.GetUserId();
			if(UserId.IsValid())
			{
				if (const FCommonAccountPtr CommonAccountPtr = GetCommonAccountFromV1(CommonConfig, UserId->AsShared(), GetFirstFrameworkInstanceName(UserId)))
				{
					TargetAccountId = CommonAccountPtr->GetId(PresenceFrameworkInstance);
				}
			}

			if(LocalUserAccountId.IsValid() && TargetAccountId.IsValid())
			{
				if (IOnlineServicesPtr OnlineServices = CommonConfig.GetServices(PresenceFrameworkInstance))
				{
					if (IPresencePtr Presence = OnlineServices->GetPresenceInterface())
					{
						TOnlineResult<FGetCachedPresence> GetCachedPresenceResult = Presence->GetCachedPresence({ LocalUserAccountId, TargetAccountId });
						if (GetCachedPresenceResult.IsOk())
						{
							return GetCachedPresenceResult.GetOkValue().Presence->Status == EUserPresenceStatus::Online
								|| GetCachedPresenceResult.GetOkValue().Presence->Status == EUserPresenceStatus::Away
								|| GetCachedPresenceResult.GetOkValue().Presence->Status == EUserPresenceStatus::ExtendedAway
								|| GetCachedPresenceResult.GetOkValue().Presence->Status == EUserPresenceStatus::DoNotDisturb;
						}
					}
				}
			}
		}
		else
		{
			const FOnlineUserPresence* PresenceInfo = SubsystemInfoPair.Value.GetPresenceInfo();
			if (PresenceInfo && PresenceInfo->bIsOnline)
			{
				return true;
			}
		}		
	}
	return false;
}

bool USocialUser::IsPlayingThisGame() const
{
	for (const TPair<ESocialSubsystem, FSubsystemUserInfo>& SubsystemInfoPair : SubsystemInfoByType)
	{
		if (ShouldReadUserInfoFromOnlineServices())
		{
			using namespace UE::Online;
			using namespace UE::OnlineFramework;

			FAccountId LocalUserAccountId, TargetAccountId;

			const FName& PresenceFrameworkInstance = USocialManager::GetPresenceFrameworkInstance();
			FCommonConfig CommonConfig(this);
			const FUniqueNetIdRepl& LocalUserNetId = GetOwningToolkit().GetLocalUserNetId(SubsystemInfoPair.Key);
			if (LocalUserNetId.IsValid())
			{
				if (const FCommonAccountPtr LocalUserCommonAccountPtr = GetCommonAccountFromV1(CommonConfig, LocalUserNetId->AsShared(), GetFirstFrameworkInstanceName(LocalUserNetId)))
				{
					LocalUserAccountId = LocalUserCommonAccountPtr->GetId(PresenceFrameworkInstance);
				}
			}

			const FUniqueNetIdRepl& UserId = SubsystemInfoPair.Value.GetUserId();
			if(UserId.IsValid())
			{
				if (const FCommonAccountPtr CommonAccountPtr = GetCommonAccountFromV1(CommonConfig, UserId->AsShared(), GetFirstFrameworkInstanceName(UserId)))
				{
					TargetAccountId = CommonAccountPtr->GetId(PresenceFrameworkInstance);
				}
			}

			if(LocalUserAccountId.IsValid() && TargetAccountId.IsValid())
			{
				if (IOnlineServicesPtr OnlineServices = CommonConfig.GetServices(PresenceFrameworkInstance))
				{
					if (IPresencePtr Presence = OnlineServices->GetPresenceInterface())
					{
						TOnlineResult<FGetCachedPresence> GetCachedPresenceResult = Presence->GetCachedPresence({ LocalUserAccountId, TargetAccountId });
						if (GetCachedPresenceResult.IsOk())
						{
							EUserPresenceGameStatus CachedGameStatus = GetCachedPresenceResult.GetOkValue().Presence->GameStatus;
							return CachedGameStatus == EUserPresenceGameStatus::PlayingThisGame;
						}
					}
				}
			}
		}
		else
		{
			const FOnlineUserPresence* PresenceInfo = SubsystemInfoPair.Value.GetPresenceInfo();
			if (PresenceInfo && PresenceInfo->bIsPlayingThisGame)
			{
				return true;
			}
		}
	}
	return false;
}

bool USocialUser::SetUserLocalAttribute(ESocialSubsystem SubsystemType, const FString& AttrName, const FString& AttrValue)
{
	const FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(SubsystemType);
	if (SubsystemInfo && SubsystemInfo->UserInfo.IsValid())
	{
		return SubsystemInfo->UserInfo.Pin()->SetUserLocalAttribute(AttrName, AttrValue);
	}
	return false;
}

bool USocialUser::GetUserAttribute(ESocialSubsystem SubsystemType, const FString& AttrName, FString& OutAttrValue) const
{
	const FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(SubsystemType);
	if (SubsystemInfo && SubsystemInfo->UserInfo.IsValid())
	{
		return SubsystemInfo->UserInfo.Pin()->GetUserAttribute(AttrName, OutAttrValue);
	}
	return false;
}

bool USocialUser::HasAnyInteractionsAvailable() const
{
	for (const FSocialInteractionHandle& Interaction : USocialManager::GetRegisteredInteractions())
	{
		if (Interaction.IsAvailable(*this))
		{
			return true;
		}
	}
	return false;
}

TArray<FSocialInteractionHandle> USocialUser::GetAllAvailableInteractions() const
{
	static TArray<FSocialInteractionHandle> AvailableInteractions;
	AvailableInteractions.Reset();

	for (const FSocialInteractionHandle& Interaction : USocialManager::GetRegisteredInteractions())
	{
		if (Interaction.IsAvailable(*this))
		{
			AvailableInteractions.Add(Interaction);
		}
	}
	return AvailableInteractions;
}

bool USocialUser::CanSendFriendInvite(ESocialSubsystem SubsystemType) const
{
	if (SubsystemType == ESocialSubsystem::Platform)
	{
		//@todo DanH: Really need OssCaps or something to be able to just ask an OSS if it supports a given feature. For now, we just magically know that we only support sending XB, PSN, and WeGame invites
		const FName PlatformOssName = USocialManager::GetSocialOssName(ESocialSubsystem::Platform);
		if (PlatformOssName != PS4_SUBSYSTEM && !USocialSettings::IsSonyOSS(PlatformOssName) && PlatformOssName != TENCENT_SUBSYSTEM)
		{
			return false;
		}
	}

	return HasSubsystemInfo(SubsystemType) && !IsLocalUser() && !IsFriend(SubsystemType) && !IsBlocked(SubsystemType) && !IsFriendshipPending(SubsystemType);
}

void USocialUser::JoinParty(const FOnlinePartyTypeId& PartyTypeId, const FName& JoinMethod) const
{
	IOnlinePartyJoinInfoConstPtr SentInvite = GetSentPartyInvite(PartyTypeId);

	GetOwningToolkit().GetSocialManager().JoinParty(*this, PartyTypeId, USocialManager::FOnJoinPartyAttemptComplete::CreateUObject(this, &USocialUser::OnJoinPartyComplete, PartyTypeId, SentInvite), JoinMethod);

	// Regardless of the outcome, note that the invite was accepted (deletes it from the OSS party system)
	if (SentInvite && !CVarSocialUserRemoveInvitationOnJoinPartyCompleteEnabled.GetValueOnAnyThread())
	{
		IOnlinePartyPtr PartyInterface = Online::GetPartyInterfaceChecked(GetWorld());
		PartyInterface->ClearInvitations(*GetOwningToolkit().GetLocalUserNetId(ESocialSubsystem::Primary), *GetUserId(ESocialSubsystem::Primary), nullptr);
		OnPartyInviteAcceptedInternal(PartyTypeId, *SentInvite);
	}
}

void USocialUser::OnJoinPartyComplete(const FJoinPartyResult& JoinPartyResult, FOnlinePartyTypeId PartyTypeId, IOnlinePartyJoinInfoConstPtr SentInvite) const
{
	if (SentInvite && CVarSocialUserRemoveInvitationOnJoinPartyCompleteEnabled.GetValueOnAnyThread() && JoinPartyResult.GetResult() != EJoinPartyCompletionResult::NoResponse)
	{
		IOnlinePartyPtr PartyInterface = Online::GetPartyInterfaceChecked(GetWorld());
		PartyInterface->ClearInvitations(*GetOwningToolkit().GetLocalUserNetId(ESocialSubsystem::Primary), *GetUserId(ESocialSubsystem::Primary), nullptr);
		OnPartyInviteAcceptedInternal(PartyTypeId, *SentInvite);
	}
}

void USocialUser::RejectPartyInvite(const FOnlinePartyTypeId& PartyTypeId)
{
	if (HasSentPartyInvite(PartyTypeId))
	{
		IOnlinePartyPtr PartyInterface = Online::GetPartyInterfaceChecked(GetWorld());
		PartyInterface->RejectInvitation(*GetOwningToolkit().GetLocalUserNetId(ESocialSubsystem::Primary), *GetUserId(ESocialSubsystem::Primary));
		OnPartyInviteRejectedInternal(PartyTypeId);
	}
}

bool USocialUser::HasBeenInvitedToParty(const FOnlinePartyTypeId& PartyTypeId) const
{
	if (USocialParty* Party = GetOwningToolkit().GetSocialManager().GetParty(PartyTypeId))
	{
		return Party->HasUserBeenInvited(*this); 
	}
	return false;
}

bool USocialUser::CanInviteToParty(const FOnlinePartyTypeId& PartyTypeId) const
{
	if (!IsBlocked())
	{
		const USocialParty* Party = GetOwningToolkit().GetSocialManager().GetParty(PartyTypeId);
		return Party && Party->CanInviteUser(*this);
	}
	
	return false;
}

bool USocialUser::InviteToParty(const FOnlinePartyTypeId& PartyTypeId, const ESocialPartyInviteMethod InviteMethod, const FString& MetaData) const
{
	if (USocialParty* Party = GetOwningToolkit().GetSocialManager().GetParty(PartyTypeId))
	{
		return Party->TryInviteUser(*this, InviteMethod, MetaData);
	}
	return false;
}

bool USocialUser::BlockUser(ESocialSubsystem Subsystem) const
{
	if (IOnlineSubsystem* Oss = GetOwningToolkit().GetSocialOss(Subsystem))
	{
		IOnlineFriendsPtr FriendsInterface = Oss->GetFriendsInterface();
		if (FriendsInterface.IsValid())
		{
			FUniqueNetIdPtr UniqueNetId = GetUserId(Subsystem).GetUniqueNetId();
			if (UniqueNetId.IsValid())
			{
				return FriendsInterface->BlockPlayer(GetOwningToolkit().GetLocalUserNum(), *UniqueNetId.Get());
			}
		}
	}

	return false;
}

bool USocialUser::UnblockUser(ESocialSubsystem Subsystem) const
{
	if (IOnlineSubsystem* Oss = GetOwningToolkit().GetSocialOss(Subsystem))
	{
		IOnlineFriendsPtr FriendsInterface = Oss->GetFriendsInterface();
		if (FriendsInterface.IsValid())
		{
			FUniqueNetIdPtr UniqueNetId = GetUserId(Subsystem).GetUniqueNetId();
			if (UniqueNetId.IsValid())
			{
				return FriendsInterface->UnblockPlayer(GetOwningToolkit().GetLocalUserNum(), *UniqueNetId.Get());
			}
		}
	}

	return false;
}

UPartyMember* USocialUser::GetPartyMember(const FOnlinePartyTypeId& PartyTypeId) const
{
	if (const USocialParty* Party = GetOwningToolkit().GetSocialManager().GetParty(PartyTypeId))
	{
		return Party->GetPartyMember(GetUserId(ESocialSubsystem::Primary));
	}
	return nullptr;
}

FString USocialUser::ToDebugString() const
{
#if UE_BUILD_SHIPPING
	return GetUserId(ESocialSubsystem::Primary).ToDebugString();
#else
	// It's a whole lot easier to debug with real names when it's ok to do so
	return FString::Printf(TEXT("%s (%s)"), *GetDisplayName(), *GetUserId(ESocialSubsystem::Primary).ToDebugString());
#endif
}

void USocialUser::WithContext(const TMap<FString, FString>& InAnalyticsContext, void(*Func)(USocialUser&))
{
	TMap<FString, FString> PreviousContext = MoveTemp(AnalyticsContext);
	AnalyticsContext = InAnalyticsContext;
	Func(*this);
	AnalyticsContext = MoveTemp(PreviousContext);
}

bool USocialUser::SendFriendInvite(ESocialSubsystem SubsystemType)
{
	return GetOwningToolkit().TrySendFriendInvite(*this, SubsystemType);
}

bool USocialUser::AcceptFriendInvite(ESocialSubsystem SocialSubsystem) const
{
	return GetOwningToolkit().AcceptFriendInvite(*this, SocialSubsystem);
}

bool USocialUser::RejectFriendInvite(ESocialSubsystem SocialSubsystem) const
{
	if (GetFriendInviteStatus(SocialSubsystem) == EInviteStatus::PendingInbound)
	{
		IOnlineFriendsPtr FriendsInterface = GetOwningToolkit().GetSocialOss(SocialSubsystem)->GetFriendsInterface();
		check(FriendsInterface.IsValid());

		return FriendsInterface->RejectInvite(GetOwningToolkit().GetLocalUserNum(), *GetUserId(SocialSubsystem), EFriendsLists::ToString(EFriendsLists::Default));
	}
	return false;
}

bool USocialUser::EndFriendship(ESocialSubsystem SocialSubsystem) const
{
	if (IsFriend(SocialSubsystem))
	{
		IOnlineFriendsPtr FriendsInterface = GetOwningToolkit().GetSocialOss(SocialSubsystem)->GetFriendsInterface();
		check(FriendsInterface.IsValid());

		return FriendsInterface->DeleteFriend(GetOwningToolkit().GetLocalUserNum(), *GetUserId(SocialSubsystem), EFriendsLists::ToString(EFriendsLists::Default));
	}
	return false;
}

FJoinPartyResult USocialUser::CheckPartyJoinability(const FOnlinePartyTypeId& PartyTypeId, bool bCheckPlatformSession/*= true*/) const
{
	return GetOwningToolkit().GetSocialManager().ValidateJoinTarget(*this, PartyTypeId, bCheckPlatformSession);
}

bool USocialUser::ShowPlatformProfile()
{	
	const FUniqueNetIdRepl LocalUserPlatformId = GetOwningToolkit().GetLocalUserNetId(ESocialSubsystem::Platform);
	const FUniqueNetIdRepl PlatformId = GetUserId(ESocialSubsystem::Platform);
	if (LocalUserPlatformId.IsValid() && PlatformId.IsValid())
	{
		const IOnlineExternalUIPtr ExternalUI = Online::GetExternalUIInterface(GetWorld(), USocialManager::GetSocialOssName(ESocialSubsystem::Platform));
		if (ExternalUI.IsValid())
		{
			return ExternalUI->ShowProfileUI(*LocalUserPlatformId, *PlatformId);
		}
	}
	return false;
}

void USocialUser::HandlePartyInviteReceived(const IOnlinePartyJoinInfo& Invite)
{
	ReceivedPartyInvites.Emplace(Invite.AsShared());
	GetOwningToolkit().NotifyPartyInviteReceived(*this, Invite);
}

void USocialUser::HandlePartyInviteRemoved(const IOnlinePartyJoinInfo& Invite, EPartyInvitationRemovedReason Reason)
{
	ReceivedPartyInvites.Remove(Invite.AsShared());
	GetOwningToolkit().NotifyPartyInviteRemoved(*this, Invite);
}

void USocialUser::HandleRequestToJoinReceived(const IOnlinePartyRequestToJoinInfo& Request)
{
	NotifyRequestToJoinReceived(Request);
}

void USocialUser::HandleRequestToJoinRemoved(const IOnlinePartyRequestToJoinInfo& Request, EPartyRequestToJoinRemovedReason Reason)
{
	NotifyRequestToJoinRemoved(Request, Reason);
}

void USocialUser::RequestToJoinParty(const FName& JoinMethod)
{
	IOnlinePartyPtr PartyInterface = Online::GetPartyInterfaceChecked(GetWorld());
	FPartyInvitationRecipient Recipient(*GetUserId(ESocialSubsystem::Primary));
	Recipient.PlatformData = GetRequestToJoinMetadata(JoinMethod.ToString());
	PartyInterface->RequestToJoinParty(*GetOwningToolkit().GetLocalUserNetId(ESocialSubsystem::Primary), IOnlinePartySystem::GetPrimaryPartyTypeId(), Recipient, FOnRequestToJoinPartyComplete::CreateUObject(this, &USocialUser::HandlePartyRequestToJoinSent, JoinMethod, Recipient.PlatformData));
}

FString USocialUser::GetRequestToJoinMetadata(const FString& ExistingMetadata)
{
	return ExistingMetadata;
}

void USocialUser::AcceptRequestToJoinParty() const
{
	if (USocialParty* Party = GetOwningToolkit().GetSocialManager().GetParty(IOnlinePartySystem::GetPrimaryPartyTypeId()))
	{
		Party->TryInviteUser(*this, ESocialPartyInviteMethod::AcceptRequestToJoin, TEXT("RequestToJoin"));
		IOnlinePartyPtr PartyInterface = Online::GetPartyInterfaceChecked(GetWorld());
		PartyInterface->ClearRequestToJoinParty(*GetOwningToolkit().GetLocalUserNetId(ESocialSubsystem::Primary), Party->GetPartyId(), *GetUserId(ESocialSubsystem::Primary), EPartyRequestToJoinRemovedReason::Accepted);
	}
}

void USocialUser::DismissRequestToJoinParty() const
{
	if (USocialParty* Party = GetOwningToolkit().GetSocialManager().GetParty(IOnlinePartySystem::GetPrimaryPartyTypeId()))
	{
		IOnlinePartyPtr PartyInterface = Online::GetPartyInterfaceChecked(GetWorld());
		PartyInterface->ClearRequestToJoinParty(*GetOwningToolkit().GetLocalUserNetId(ESocialSubsystem::Primary), Party->GetPartyId(), *GetUserId(ESocialSubsystem::Primary), EPartyRequestToJoinRemovedReason::Dismissed);
	}
}

void USocialUser::HandlePartyRequestToJoinSent(const FUniqueNetId& LocalUserId, const FUniqueNetId& PartyLeaderId, const FDateTime& ExpiresAt, const ERequestToJoinPartyCompletionResult Result, FName JoinMethod, FString Metadata)
{
	UE_LOG(LogParty, VeryVerbose, TEXT("%s - User [%s] sent a join request to [%s] with result[%s]"), ANSI_TO_TCHAR(__FUNCTION__), *LocalUserId.ToDebugString(), *PartyLeaderId.ToDebugString(), ToString(Result));

	if (Result == ERequestToJoinPartyCompletionResult::Succeeded)
	{
		GetOwningToolkit().OnPartyRequestToJoinSent().Broadcast(*this);
	}
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	HandlePartyRequestToJoinSent(LocalUserId, PartyLeaderId, ExpiresAt, Result, JoinMethod);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void USocialUser::HandlePartyRequestToJoinSent(const FUniqueNetId& LocalUserId, const FUniqueNetId& PartyLeaderId, const FDateTime& ExpiresAt, const ERequestToJoinPartyCompletionResult Result, FName JoinMethod)
{
}

TSharedPtr<const IOnlinePartyJoinInfo> USocialUser::GetPartyJoinInfo(const FOnlinePartyTypeId& PartyTypeId) const
{
	TSharedPtr<const IOnlinePartyJoinInfo> JoinInfo = nullptr;

	if (IOnlinePartyPtr PartyInterface = Online::GetPartyInterface(GetWorld()))
	{
		const FUniqueNetIdRepl LocalUserId = GetOwningToolkit().GetLocalUserNetId(ESocialSubsystem::Primary);
		const FUniqueNetIdRepl UserId = GetUserId(ESocialSubsystem::Primary);
		if (ensure(LocalUserId.IsValid()) && ensure(UserId.IsValid()))
		{
			JoinInfo = PartyInterface->GetAdvertisedParty(*LocalUserId, *UserId, PartyTypeId);
		}
	}

	// If no advertised party info, check to see if this user has sent an invite
	if (!JoinInfo.IsValid())
	{
		for (const IOnlinePartyJoinInfoConstRef& Invite : ReceivedPartyInvites)
		{
			if (Invite->GetPartyTypeId() == PartyTypeId)
			{
				JoinInfo = Invite;
				break;
			}
		}
	}

	return JoinInfo;
}

bool USocialUser::HasSentPartyInvite(const FOnlinePartyTypeId& PartyTypeId) const
{
	for (const IOnlinePartyJoinInfoConstRef& Invite : ReceivedPartyInvites)
	{
		if (Invite->GetPartyTypeId() == PartyTypeId)
		{
			return true;
		}
	}
	return false;
}

IOnlinePartyJoinInfoConstPtr USocialUser::GetSentPartyInvite(const FOnlinePartyTypeId& PartyTypeId) const
{
	for (const IOnlinePartyJoinInfoConstRef& Invite : ReceivedPartyInvites)
	{
		if (Invite->GetPartyTypeId() == PartyTypeId)
		{
			return Invite;
		}
	}
	return nullptr;
}

FUniqueNetIdRepl USocialUser::GetUserId(ESocialSubsystem SubsystemType) const
{
	if (const FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(SubsystemType))
	{
		return SubsystemInfo->GetUserId();
	}
	return FUniqueNetIdRepl();
}

TArray<ESocialSubsystem> USocialUser::GetRelevantSubsystems() const
{
	TArray<ESocialSubsystem> OutSubsystemTypes;
	SubsystemInfoByType.GenerateKeyArray(OutSubsystemTypes);
	return OutSubsystemTypes;
}

bool USocialUser::HasSubsystemInfo(const TSet<ESocialSubsystem>& SubsystemTypes, bool bRequireAll)
{
	if (SubsystemTypes.Num() > 0)
	{
		for (ESocialSubsystem Subsystem : SubsystemTypes)
		{
			const bool bHasInfo = SubsystemInfoByType.Contains(Subsystem);
			if (bHasInfo && !bRequireAll)
			{
				return true;
			}
			else if (bRequireAll && !bHasInfo)
			{
				return false;
			}
		}

		return bRequireAll;
	}
	return false;
}

bool USocialUser::HasSubsystemInfo(ESocialSubsystem Subsystem) const
{
	return SubsystemInfoByType.Contains(Subsystem);
}

void USocialUser::NotifyUserUnblocked(ESocialSubsystem SubsystemType)
{
	if (FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(SubsystemType))
	{
		// Make sure the existing blocked player info isn't still valid
		if (SubsystemInfo->BlockedPlayerInfo.IsValid())
		{
			UE_LOG(LogParty, Log, TEXT("SocialUser [%s] has been unblocked on [%s], but still has valid blocked player info. Possible leak via hard ref somewhere."),*ToDebugString(), ToString(SubsystemType));
			SubsystemInfo->BlockedPlayerInfo.Reset();
		}
		OnBlockedStatusChanged().Broadcast(SubsystemType, false);
	}
}

void USocialUser::NotifyFriendInviteRemoved(ESocialSubsystem SubsystemType)
{
	if (FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(SubsystemType))
	{
		// Make sure the existing friend info isn't valid
		if (SubsystemInfo->FriendInfo.IsValid())
		{
			UE_LOG(LogParty, Log, TEXT("SocialUser [%s] has cancelled a friend invite on [%s], but still has valid friend info. Possible leak via hard ref somewhere."), *ToDebugString(), ToString(SubsystemType));
			SubsystemInfo->FriendInfo.Reset();
		}
		OnFriendInviteRemoved().Broadcast(SubsystemType);
	}
}

void USocialUser::NotifyUserUnfriended(ESocialSubsystem SubsystemType)
{
	if (FSubsystemUserInfo* SubsystemInfo = SubsystemInfoByType.Find(SubsystemType))
	{
		// Make sure the existing friend info isn't valid
		if (SubsystemInfo->FriendInfo.IsValid())
		{
			UE_LOG(LogParty, Log, TEXT("SocialUser [%s] has been unfriended on [%s], but still has valid friend info. Possible leak via hard ref somewhere."), *ToDebugString(), ToString(SubsystemType));
			SubsystemInfo->FriendInfo.Reset();
		}
		OnFriendRemoved().Broadcast(SubsystemType);
	}
}

void USocialUser::EstablishOssInfo(const TSharedRef<FOnlineFriend>& InFriendInfo, ESocialSubsystem SubsystemType)
{
	FSubsystemUserInfo& SubsystemInfo = FindOrCreateSubsystemInfo(InFriendInfo->GetUserId(), SubsystemType);

	if (InFriendInfo != SubsystemInfo.FriendInfo)
	{
		/*UE_CLOG(!SubsystemInfo.FriendInfo.IsValid(), LogParty, Warning, TEXT("SocialUser [%s] is establishing new friend info on [%s], but the existing info is still valid."),
			*ToDebugString(), ToString(SubsystemType));*/

		SubsystemInfo.FriendInfo = InFriendInfo;

		// Presence information on a user comes from the friend info, so if we have new friend info, we likely have wholly new presence info
		if (USocialManager::ShouldReadPresenceFromOnlineServices())
		{
			OnPresenceChangedInternalV2(SubsystemType);
		}
		else
		{
			OnPresenceChangedInternal(SubsystemType);
		}
	}
}

void USocialUser::EstablishOssInfo(const TSharedRef<FOnlineBlockedPlayer>& InBlockedPlayerInfo, ESocialSubsystem SubsystemType)
{
	FSubsystemUserInfo& SubsystemInfo = FindOrCreateSubsystemInfo(InBlockedPlayerInfo->GetUserId(), SubsystemType);

	if (InBlockedPlayerInfo != SubsystemInfo.BlockedPlayerInfo)
	{
		UE_CLOG(SubsystemInfo.BlockedPlayerInfo.IsValid(), LogParty, Warning, TEXT("SocialUser [%s] is establishing new blocked player info on [%s], but the existing info is still valid."),
			*ToDebugString(), ToString(SubsystemType));

		SubsystemInfo.BlockedPlayerInfo = InBlockedPlayerInfo;
		OnBlockedStatusChanged().Broadcast(SubsystemType, true);
	}
}

void USocialUser::EstablishOssInfo(const TSharedRef<FOnlineRecentPlayer>& InRecentPlayerInfo, ESocialSubsystem SubsystemType)
{
	FSubsystemUserInfo& SubsystemInfo = FindOrCreateSubsystemInfo(InRecentPlayerInfo->GetUserId(), SubsystemType);

	if (InRecentPlayerInfo != SubsystemInfo.RecentPlayerInfo)
	{
		UE_CLOG(SubsystemInfo.RecentPlayerInfo.IsValid(), LogParty, Warning, TEXT("SocialUser [%s] is establishing new recent player info on [%s], but the existing info is still valid."),
			*ToDebugString(), ToString(SubsystemType));

		SubsystemInfo.RecentPlayerInfo = InRecentPlayerInfo;
	}
}

#if WITH_EDITOR
void USocialUser::Debug_RandomizePresence()
{
	bDebug_IsPresenceArtificial = true;
	Debug_RandomPresence = static_cast<EOnlinePresenceState::Type>(FMath::RandRange((int32)EOnlinePresenceState::Online, (int32)EOnlinePresenceState::Away));
	Debug_RandomPresenceV2 = static_cast<UE::Online::EUserPresenceStatus>(FMath::RandRange((int32)UE::Online::EUserPresenceStatus::Offline, (int32)UE::Online::EUserPresenceStatus::Unknown));
}
#endif

void USocialUser::OnPresenceChangedInternalV2(ESocialSubsystem SubsystemType)
{
	OnUserPresenceChanged().Broadcast(SubsystemType);
}

void USocialUser::OnPresenceChangedInternal(ESocialSubsystem SubsystemType)
{
	OnUserPresenceChanged().Broadcast(SubsystemType);
}

void USocialUser::OnPartyInviteAcceptedInternal(const FOnlinePartyTypeId& PartyTypeId, const IOnlinePartyJoinInfo& Invite) const
{
	// Call deprecated method
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	OnPartyInviteAcceptedInternal(PartyTypeId);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	OnPartyInviteAccepted().Broadcast();
}

void USocialUser::OnPartyInviteRejectedInternal(const FOnlinePartyTypeId& PartyTypeId) const
{
	OnPartyInviteRejected().Broadcast();
}

void USocialUser::NotifyPresenceChanged(ESocialSubsystem SubsystemType)
{
	if (USocialManager::ShouldReadPresenceFromOnlineServices())
	{
		OnPresenceChangedInternalV2(SubsystemType);
	}
	else
	{
		OnPresenceChangedInternal(SubsystemType);
	}
}

void USocialUser::SetSubsystemId(ESocialSubsystem SubsystemType, const FUniqueNetIdRepl& SubsystemId)
{
	if (ensure(!SubsystemInfoByType.Contains(SubsystemType)) && ensure(SubsystemId.IsValid()))
	{
		SubsystemInfoByType.Add(SubsystemType, FSubsystemUserInfo(SubsystemId));

		USocialToolkit& OwningToolkit = GetOwningToolkit();
		OwningToolkit.NotifySubsystemIdEstablished(*this, SubsystemType, SubsystemId);

		IOnlineSubsystem* OSS = OwningToolkit.GetSocialOss(SubsystemType);
		if (ensure(OSS))
		{
			IOnlineUserPtr UserInterface = OSS->GetUserInterface();
			if (UserInterface.IsValid())
			{
				TSharedPtr<FOnlineUser> UserInfo = UserInterface->GetUserInfo(OwningToolkit.GetLocalUserNum(), *SubsystemId);
				//If this is our local user, we always want to get the freshest data, to make sure any updates to the account (linking, name changes, etc.) are seen on re-login
				if (UserInfo.IsValid() && !IsLocalUser())
				{
					SetUserInfo(SubsystemType, UserInfo.ToSharedRef());
				}
				else
				{
					UE_LOG(LogParty, VeryVerbose, TEXT("SocialUser [%s] querying user info on subsystem [%s]"), *ToDebugString(), ToString(SubsystemType));

					// No valid user info exists on this subsystem, so queue up a query for it
					auto QueryCompleteHandler = FSocialQuery_UserInfo::FOnQueryComplete::CreateUObject(this, &USocialUser::HandleQueryUserInfoComplete);
					FSocialQueryManager::AddUserId<FSocialQuery_UserInfo>(OwningToolkit, SubsystemType, SubsystemId.GetUniqueNetId().ToSharedRef(), QueryCompleteHandler);
					NumPendingQueries++;
				}
			}
		}
	}
}

void USocialUser::SetUserInfo(ESocialSubsystem SubsystemType, const TSharedRef<FOnlineUser>& UserInfo)
{
	FSubsystemUserInfo& SubsystemInfo = SubsystemInfoByType.FindChecked(SubsystemType);
	SubsystemInfo.UserInfo = UserInfo;

	if (SubsystemType == ESocialSubsystem::Primary)
	{
		// This is our primary user info, so we can interrogate it for all other external ids
		for (ESocialSubsystem Subsystem : USocialManager::GetDefaultSubsystems())
		{
			// If we haven't already accounted for the id on this subsystem, look it up now
			if (!SubsystemInfoByType.Contains(Subsystem))
			{
				if (IOnlineSubsystem* MissingOSS = GetOwningToolkit().GetSocialOss(Subsystem))
				{
					if (const FSocialPlatformDescription* PlatformDescription = USocialSettings::GetSocialPlatformDescriptionForOnlineSubsystem(MissingOSS->GetSubsystemName()))
					{
						if (!PlatformDescription->ExternalAccountType.IsEmpty())
						{
							const FString SubsystemIdKey = FString::Printf(TEXT("%s:id"), *PlatformDescription->ExternalAccountType);
							FString SubsystemIdStr;
							if (UserInfo->GetUserAttribute(SubsystemIdKey, SubsystemIdStr) && !SubsystemIdStr.IsEmpty())
							{
								const FString IdPrefix = USocialSettings::GetUniqueIdEnvironmentPrefix(Subsystem);
								if (!IdPrefix.IsEmpty())
								{
									// Wipe the environment prefix from the stored ID string before converting it to a proper UniqueId
									SubsystemIdStr.RemoveFromStart(IdPrefix);
								}

								FUniqueNetIdRepl SubsystemId = MissingOSS->GetIdentityInterface()->CreateUniquePlayerId(SubsystemIdStr);
								if (SubsystemId.IsValid())
								{
									SetSubsystemId(Subsystem, SubsystemId);
								}
								else 
								{
									UE_LOG(LogParty, Error, TEXT("SocialUser [%s] Failed to create SubsystemId for SubsystemType: [%s] SubsystemIdKey [%s] IdPrefix: [%s] SubsystemIdStr: [%s] for OnlineUser [%s]"), *ToDebugString(), ToString(SubsystemType), *SubsystemIdKey, *IdPrefix, *SubsystemIdStr, *UserInfo->GetUserId()->ToDebugString());
								}
							}
						}
					}
				}
			}
		}
	}
}

TFuture<void> USocialUser::SetupCommonAccount()
{
	return MakeFulfilledPromise<void>().GetFuture();
}

void USocialUser::HandleQueryUserInfoComplete(ESocialSubsystem SubsystemType, bool bWasSuccessful, const TSharedPtr<FOnlineUser>& UserInfo)
{
	--NumPendingQueries;

	if (UserInfo.IsValid())
	{
		SetUserInfo(SubsystemType, UserInfo.ToSharedRef());
	}

	if (ShouldReadUserInfoFromOnlineServices())
	{
		SetupCommonAccount()
		.Then([WeakThis = TWeakObjectPtr<USocialUser>(this), SubsystemType](TFuture<void> Result)
		{
			if (USocialUser* StrongThis = WeakThis.Get())
			{
				using namespace UE::OnlineFramework;

				UE_LOG(LogParty, VeryVerbose, TEXT("SocialUser [%s] querying user info on services [%s]"), *StrongThis->ToDebugString(), ToString(SubsystemType));

				const FUniqueNetIdRepl UserId = StrongThis->GetUserId(SubsystemType);

				const FName& PresenceFrameworkInstance = USocialManager::GetPresenceFrameworkInstance();
				FCommonConfig CommonConfig(StrongThis);
				if (const FCommonAccountPtr CommonAccountPtr = GetCommonAccountFromV1(CommonConfig, UserId->AsShared(), GetFirstFrameworkInstanceName(UserId)))
				{
					UE::Online::FAccountId AccountId = CommonAccountPtr->GetId(PresenceFrameworkInstance);
					if (AccountId.IsValid())
					{
						// No valid user info exists on this subsystem, so queue up a query for it
						FSocialQuery_UserInfoV2::FOnQueryComplete QueryCompleteHandler = FSocialQuery_UserInfoV2::FOnQueryComplete::CreateUObject(StrongThis, &USocialUser::HandleQueryUserInfoV2Complete);
						FSocialQueryManager::AddUserId<FSocialQuery_UserInfoV2>(StrongThis->GetOwningToolkit(), SubsystemType, AccountId, QueryCompleteHandler);
						StrongThis->NumPendingQueries++;

						return;
					}
				}

				StrongThis->TryBroadcastInitializationComplete();
			}
		});
		
		return;	
	}

	UE_LOG(LogParty, VeryVerbose, TEXT("User [%s] finished querying user info on subsystem [%s] with result [%d]. [%d] queries still pending."), *ToDebugString(), ToString(SubsystemType), UserInfo.IsValid(), NumPendingQueries);
	TryBroadcastInitializationComplete();
}

void USocialUser::HandleQueryUserInfoV2Complete(ESocialSubsystem SubsystemType, bool bWasSuccessful, const TValueOrError<TSharedRef<UE::Online::FUserInfo>, UE::Online::FOnlineError>& Result)
{
	--NumPendingQueries;

	UE_LOG(LogParty, VeryVerbose, TEXT("User [%s] finished querying user info on subsystem [%s] with result [%d]. [%d] queries still pending."), *ToDebugString(), ToString(SubsystemType), bWasSuccessful, NumPendingQueries);
	TryBroadcastInitializationComplete();
}

void USocialUser::HandleSetNicknameComplete(int32 LocalUserNum, const FUniqueNetId& FriendId, const FString& ListName, const FOnlineError& Error)
{
	if (!Error.WasSuccessful())
	{
		UE_LOG(LogOnline, Log, TEXT("Set nickname request failed for user: %s with error message: %s"), *FriendId.ToDebugString(), *Error.GetErrorMessage().ToString());
	}
	OnSetNicknameCompleted().Broadcast(Error.GetErrorMessage());
}

FString USocialUser::SanitizePresenceString(FString InString) const
{
	return InString;
}

void USocialUser::PopulateSortParameterList(TArray<int64>& OutSortParams) const
{
	const int32 TotalSortParams = 3;
	OutSortParams.Reset(TotalSortParams);

	// Parameters are prioritized by the first member added to the list being the highest priority
	OutSortParams.Add(GetCustomSortValuePrimary());
	OutSortParams.Add(GetCustomSortValueSecondary());
	OutSortParams.Add(GetCustomSortValueTertiary());
}

#undef LOCTEXT_NAMESPACE
