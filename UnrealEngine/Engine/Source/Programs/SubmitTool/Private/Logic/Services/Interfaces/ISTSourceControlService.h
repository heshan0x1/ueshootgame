// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISubmitToolService.h"
#include "ISourceControlProvider.h"
#include "Memory/SharedBuffer.h"
#include "Tasks/Task.h"
#include "Misc/Base64.h"

struct FAuthTicket
{
public:
	FAuthTicket() = default;
	FAuthTicket(const FString& InUsername, const FString& InPassword) : Username(InUsername), Password(InPassword) {}
	FAuthTicket(FStringView TicketString)
	{
		int32 ChopIndex;
		if(TicketString.FindChar(':', ChopIndex))
		{
			Username = TicketString.Left(ChopIndex);
			Password = TicketString.RightChop(ChopIndex + 1);
		}
	}
	virtual ~FAuthTicket() {}

	virtual FString ToString() const
	{
		return TEXT("Basic ") + FBase64::Encode(Username + TEXT(":") + Password);
	}

	bool IsValid() const
	{
		return !Username.IsEmpty() && !Password.IsEmpty();
	}
	FString Username;
private:
	FString Password;
};

struct FUserData
{
	FUserData(const FString& Username, const FString& Name, const FString& Email) : Name(Name), Username(Username), Email(Email)
	{}

	FString Name;
	FString Username;
	FString Email;

	bool Contains(const FString& SubString) const
	{
		return Name.Contains(SubString, ESearchCase::IgnoreCase) ||
			Username.Contains(SubString, ESearchCase::IgnoreCase) ||
			Email.Contains(SubString, ESearchCase::IgnoreCase);
	}

	FString GetDisplayText()
	{
		return Name + " - " + Username + " - " + Email;
	}
};


struct FSCCStream
{
public:
	FSCCStream(const FString& InName, const FString& InParent, const FString& InType)
		: Name(InName), Parent(InParent), Type(InType)
	{}
	const FString Name;
	const FString Parent;
	const FString Type;

	TArray<const FString> AdditionalImportPaths;
};

template<typename UserType>
struct FSCCResultRetVal
{
	bool bRequestSucceed;
	TSharedPtr<UserType> UserData;
};

using FSCCResultNoRet = FSCCResultRetVal<void>;

using FSCCRecordSet = TArray<TMap<FString, FString>>;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnUsersGet, TArray<TSharedPtr<FUserData>>& /*Users*/)
DECLARE_MULTICAST_DELEGATE_OneParam(FOnGroupsGet, TArray<TSharedPtr<FString>>& /*Groups*/)
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnUsersAndGroupsGet, TArray<TSharedPtr<FUserData>>& /*Users*/, TArray<TSharedPtr<FString>>& /*Groups*/)

template<typename Type>
using FOnSCCCommandCompleteRetVal = TDelegate<Type(bool /*bSuccess*/, const FSCCRecordSet& /*InResultValues*/, const FSourceControlResultInfo& /*InResultsInfo*/)>;
using FOnSCCCommandCompleteNoRet = FOnSCCCommandCompleteRetVal<void>;

class ISTSourceControlService : public ISubmitToolService
{
public:
	virtual const TUniquePtr<ISourceControlProvider>& GetProvider() const = 0;
	virtual void GetUsers(const FOnUsersGet::FDelegate& Callback) = 0;
	virtual void GetGroups(const FOnGroupsGet::FDelegate& Callback) = 0;
	virtual void GetUsersAndGroups(const FOnUsersAndGroupsGet::FDelegate& Callback) = 0;

	virtual UE::Tasks::TTask<FSCCResultNoRet> DownloadFiles(const FString& InFilepath, TArray<FSharedBuffer>& OutFileBuffers) = 0;

	virtual bool IsAvailable() const = 0;

	virtual bool Tick(float InDeltaTime) = 0;

	virtual const TArray<TSharedPtr<FUserData>>& GetRecentUsers() const = 0;
	virtual void AddRecentUser(TSharedPtr<FUserData>& User) = 0;
	virtual const TArray<TSharedPtr<FString>>& GetRecentGroups() const = 0;
	virtual void AddRecentGroup(TSharedPtr<FString>& Group) = 0;

	virtual TSharedPtr<FUserData> GetUserDataFromCache(const FString& Username) const = 0;

	virtual const TArray<FSCCStream*>& GetClientStreams() const = 0;
	virtual const FSCCStream* GetSCCStream(const FString& InStreamName) = 0;
	virtual const FString GetRootStreamName() = 0;
	virtual const FString GetCurrentStreamName() = 0;
	virtual const size_t GetDepotStreamLength(const FString& InDepotName) = 0;
	virtual const FAuthTicket& GetAuthTicket() = 0;


	template<typename T>
	UE::Tasks::TTask<FSCCResultRetVal<T>> RunCommand(const FString& InCommand, const TArray<FString>& InAdditionalArgs, FOnSCCCommandCompleteRetVal<T> InCompleteCallback, TArray<FSharedBuffer>* OutData = nullptr)
	{
		return UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, InCommand, InAdditionalArgs, InCompleteCallback, OutData] {
			FSCCRecordSet ResultValues;
			FSourceControlResultInfo ResultInfo;

			bool bResult = RunCommandInternal(InCommand, InAdditionalArgs, ResultValues, ResultInfo, OutData);

			TSharedPtr<T> UserTypeResult;

			if (InCompleteCallback.IsBound())
			{
				if constexpr (!std::is_same<T, void>::value)
				{
					UserTypeResult = MakeShared<T>(InCompleteCallback.Execute(bResult, ResultValues, ResultInfo));
				}
				else
				{
					InCompleteCallback.Execute(bResult, ResultValues, ResultInfo);
				}
			}

			return FSCCResultRetVal<T>(bResult, MoveTemp(UserTypeResult));
			});
	};

	UE::Tasks::TTask<FSCCResultNoRet> RunCommand(const FString& InCommand, const TArray<FString>& InAdditionalArgs, TArray<FSharedBuffer>* OutData = nullptr)
	{
		return RunCommand<void>(InCommand, InAdditionalArgs, nullptr, OutData);
	}

protected:
	virtual bool RunCommandInternal(const FString& InCommand, const TArray<FString>& InAdditionalArgs, FSCCRecordSet& OutResultValues, FSourceControlResultInfo& OutResultInfo, TArray<FSharedBuffer>* OutData = nullptr) = 0;

};

Expose_TNameOf(ISTSourceControlService);