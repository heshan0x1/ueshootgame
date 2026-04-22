// Copyright Epic Games, Inc. All Rights Reserved.

#include "Experimental/BuildServerInterface.h"

#include "Async/Mutex.h"
#include "Containers/StringView.h"
#include "DesktopPlatformModule.h"
#include "Experimental/ZenProjectStoreWriter.h"
#include "Experimental/ZenServerInterface.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformOutputDevices.h"
#include "Http/HttpClient.h"
#include "Http/HttpHostBuilder.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MonitoredProcess.h"
#include "Misc/OutputDeviceFile.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "Serialization/Archive.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Serialization/CompactBinaryValidation.h"
#include "Serialization/CompactBinaryWriter.h"
#include "SocketSubsystem.h"
#include "String/LexFromString.h"
#include "Tasks/Task.h"

#if WITH_SSL
#include "Ssl.h"
#endif

#define UE_BUILDSERVERINSTANCE_MAX_FAILED_LOGIN_ATTEMPTS 16

namespace UE::Zen::Build
{

DEFINE_LOG_CATEGORY_STATIC(LogBuildServiceInstance, Log, All);

namespace Private
{

struct FProgressState
{
	FString Label;
	FString Detail;
	float Percent = 0.0f;
};

struct FBuildTransferState : public TSharedFromThis<FBuildTransferState, ESPMode::ThreadSafe>
{
	FBuildServiceInstance::FBuildTransfer::EType Type;
	FCbObjectId BuildId;
	FString Name;
	FString Namespace;
	FString Bucket;
	FString Destination;
	FString ProjectFilePath;
	FName TargetPlatformName = NAME_None;

	std::atomic<bool> bCancelRequested = false;

	mutable FRWLock Lock;
	TArray<FProgressState> ProgressState;
	const uint32 RecentOutputLinesCapacity = 16;
	TCircularBuffer<FString> RecentOutputLines = TCircularBuffer<FString>(RecentOutputLinesCapacity);
	uint32 NextOutputLineIndex = 0;
	TUniquePtr<FArchive> OutputLogFile;
	UE::Zen::Build::FBuildServiceInstance::EBuildTransferStatus Status = UE::Zen::Build::FBuildServiceInstance::EBuildTransferStatus::Queued;
	int ReturnCode;

	FString GetRecentOutput() const;
	FString GetLogFilename() const;
};

class FAccessToken
{
public:
	void SetToken(FStringView Scheme, FStringView Token);
	inline uint32 GetSerial() const { return Serial.load(std::memory_order_relaxed); }
	friend FAnsiStringBuilderBase& operator<<(FAnsiStringBuilderBase& Builder, const FAccessToken& Token);

private:
	mutable FRWLock Lock;
	TArray<ANSICHAR> Header;
	std::atomic<uint32> Serial;
};

FAnsiStringBuilderBase& operator<<(FAnsiStringBuilderBase& Builder, const Private::FAccessToken& Token)
{
	FReadScopeLock ReadLock(Token.Lock);
	return Builder.Append(Token.Header);
}

FString
FBuildTransferState::GetRecentOutput() const
{
	TStringBuilder<256> StringBuilder;
	if (NextOutputLineIndex == 0)
	{
		return FString();
	}

	uint32 StartIndex = 0;
	if (NextOutputLineIndex > RecentOutputLinesCapacity)
	{
		StartIndex = NextOutputLineIndex - RecentOutputLinesCapacity + 1;
	}

	for (uint32 Index = StartIndex; Index < NextOutputLineIndex; ++Index)
	{
		StringBuilder.Append(RecentOutputLines[Index]);
		if (Index < (NextOutputLineIndex - 1))
		{
			StringBuilder.Append(LINE_TERMINATOR);
		}
	}

	return StringBuilder.ToString();
}

FString
FBuildTransferState::GetLogFilename() const
{
	const FString LogDirectory = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(
		*FPaths::GetPath(FPlatformOutputDevices::GetAbsoluteLogFilename())
	);
	return FPaths::Combine(LogDirectory, TEXT("Transfers"), Name) + TEXT(".log");
}

void FAccessToken::SetToken(const FStringView Scheme, const FStringView Token)
{
	FWriteScopeLock WriteLock(Lock);
	const int32 TokenLen = FPlatformString::ConvertedLength<ANSICHAR>(Token.GetData(), Token.Len());

	Header.Empty(TokenLen);

	const int32 TokenIndex = Header.AddUninitialized(TokenLen);
	FPlatformString::Convert(Header.GetData() + TokenIndex, TokenLen, Token.GetData(), Token.Len());
	Serial.fetch_add(1, std::memory_order_relaxed);
}
} // namespace Private

bool LoadFromCompactBinary(FCbFieldView Field, FBuildServiceInstance::FBuildRecord& OutBuildRecord)
{
	if (!Field.IsObject())
	{
		return false;
	}
	bool bOk = true;
	FString BuildIdString;
	FCbObjectId::ByteArray BuildIdBytes;
	bOk &= LoadFromCompactBinary(Field["buildId"], BuildIdString);
	bOk &= UE::String::HexToBytes(BuildIdString, BuildIdBytes) == sizeof(BuildIdBytes);
	OutBuildRecord.BuildId = FCbObjectId(BuildIdBytes);

	FCbFieldView MetadataField = Field["metadata"];
	if (!MetadataField.IsObject())
	{
		return false;
	}

	OutBuildRecord.Metadata = FCbObject::Clone(MetadataField.AsObjectView());
	return bOk;
}

bool
FServiceSettings::ReadFromConfig()
{
	check(GConfig && GConfig->IsReadyForUse());
	FString Config;
	if (!GConfig->GetString(TEXT("StorageServers"), TEXT("Cloud"), Config, GEngineIni))
	{
		return false;
	}
	bool bRetVal = false;

	bRetVal |= FParse::Value(*Config, TEXT("Host="), Host);
	bRetVal |= FParse::Value(*Config, TEXT("OAuthProviderIdentifier="), OAuthProviderIdentifier);

	FParse::Value(*Config, TEXT("AuthScheme="), AuthScheme);
	if (AuthScheme.IsEmpty())
	{
		AuthScheme = "Bearer";
	}

	return bRetVal;
}

bool
FServiceSettings::ReadFromURL(FStringView InstanceURL)
{
	Host = InstanceURL;
	return true;
}

FBuildServiceInstance::FBuildServiceInstance()
{
	Settings.ReadFromConfig();
	Initialize();
}

FBuildServiceInstance::FBuildServiceInstance(FStringView InstanceURL)
{
	Settings.ReadFromURL(InstanceURL);
	Initialize();
}

FBuildServiceInstance::~FBuildServiceInstance()
{
	bBuildTransferThreadStopping = true;
	if (BuildTransferThread.IsJoinable())
	{
		BuildTransferThread.Join();
	}
}

void
FBuildServiceInstance::Connect(bool bAllowInteractive, FOnConnectionComplete&& OnConnectionComplete)
{
	const FString Host = Settings.GetHost();
	if (Host.IsEmpty() || (Host == TEXT("None")))
	{
		if (OnConnectionComplete)
		{
			OnConnectionComplete(EConnectionState::ConnectionFailed, EConnectionFailureReason::MissingHost);
		}
		return;
	}

	if (ConnectionState.exchange(EConnectionState::ConnectionInProgress, std::memory_order_relaxed) == EConnectionState::ConnectionInProgress)
	{
		if (OnConnectionComplete)
		{
			OnConnectionComplete(EConnectionState::ConnectionFailed, EConnectionFailureReason::UnexpectedState);
		}
		return;
	}

	UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, bAllowInteractive, OnConnectionComplete = MoveTemp(OnConnectionComplete)]()
	{
		TAnsiStringBuilder<256> ResolvedHost;
		double ResolvedLatency;
		FHttpHostBuilder HostBuilder;
		HostBuilder.AddFromString(Settings.GetHost());
		if (HostBuilder.ResolveHost(/* Warning timeout */ 1.0, 4.0 /* Max duration timeout*/, ResolvedHost, ResolvedLatency))
		{
			EffectiveDomain.Reset();
			EffectiveDomain.Append(ResolvedHost);

			EDesktopLoginInteractionLevel InteractionLevel = bAllowInteractive ? EDesktopLoginInteractionLevel::Interactive : EDesktopLoginInteractionLevel::None;
			if (AcquireAccessToken(InteractionLevel))
			{
				ConnectionState.store(EConnectionState::ConnectionSucceeded, std::memory_order_relaxed);
				if (OnConnectionComplete)
				{
					OnConnectionComplete(EConnectionState::ConnectionSucceeded, EConnectionFailureReason::None);
				}
			}
			else
			{
				UE_LOG(LogBuildServiceInstance, Warning, TEXT("Unable to acquire access token."));
				ConnectionState.store(EConnectionState::ConnectionFailed, std::memory_order_relaxed);
				if (OnConnectionComplete)
				{
					OnConnectionComplete(EConnectionState::ConnectionFailed, EConnectionFailureReason::FailedToAcquireAccessToken);
				}
			}
		}
		else
		{
			// even if we fail to resolve a host to use the returned host will at least contain the first of the possible hosts which we can attempt to use
			EffectiveDomain.Reset();
			EffectiveDomain.Append(ResolvedHost);

			FString HostCandidates = HostBuilder.GetHostCandidatesString();
			UE_LOG(LogBuildServiceInstance, Warning, TEXT("Unable to resolve best host candidate to use, most likely none of the suggested hosts was reachable. Attempted hosts were: '%s' ."), *HostCandidates);
			ConnectionState.store(EConnectionState::ConnectionFailed, std::memory_order_relaxed);
			if (OnConnectionComplete)
			{
				OnConnectionComplete(EConnectionState::ConnectionFailed, EConnectionFailureReason::FailedToResolveHost);
			}
		}

	});
}

void
FBuildServiceInstance::RefreshNamespacesAndBuckets(FOnRefreshNamespacesAndBucketsComplete&& InOnRefreshNamespacesAndBucketsComplete)
{
	{
		FWriteScopeLock _(NamespacesAndBucketsLock);
		NamespacesAndBuckets.Empty();
	}
	if (ConnectionState.load(std::memory_order_relaxed) != EConnectionState::ConnectionSucceeded)
	{
		CallOnRefreshNamespacesAndBucketsComplete(MoveTemp(InOnRefreshNamespacesAndBucketsComplete));
		return;
	}

	FString OutputFilePath = FPaths::CreateTempFilename(FPlatformProcess::UserTempDir(), TEXT("BuildContainers"), TEXT(".cbo"));
	FPaths::MakePlatformFilename(OutputFilePath);

	FString CommandLineArgs = FString::Printf(TEXT("builds list-namespaces --recursive \"%s\""),
		*OutputFilePath);
	CommandLineArgs = AddZenUtilityBuildServerArguments(*CommandLineArgs);

	InvokeZenUtility(CommandLineArgs, nullptr,
	[this, OutputFilePath = MoveTemp(OutputFilePath), InOnRefreshNamespacesAndBucketsComplete = MoveTemp(InOnRefreshNamespacesAndBucketsComplete)](bool bSuccessful) mutable
	{
		if (bSuccessful)
		{
			TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileReader(*OutputFilePath));
			if (!Ar)
			{
				UE_LOG(LogBuildServiceInstance, Warning, TEXT("Missing output file from zen utility when gathering build data: '%s'"), *OutputFilePath);
				CallOnRefreshNamespacesAndBucketsComplete(MoveTemp(InOnRefreshNamespacesAndBucketsComplete));
				return;
			}

			FCbObject OutputObject = LoadCompactBinary(*Ar).AsObject();
			{
				FWriteScopeLock _(NamespacesAndBucketsLock);
				for (FCbFieldView ResultField : OutputObject["results"])
				{
					FString Namespace = *WriteToString<64>(ResultField["name"].AsString());
					for (FCbFieldView Item : ResultField["items"])
					{
						NamespacesAndBuckets.Add(Namespace, *WriteToString<64>(Item.AsString()));
					}
				}
			}
		}
		CallOnRefreshNamespacesAndBucketsComplete(MoveTemp(InOnRefreshNamespacesAndBucketsComplete));
	});
}

void
FBuildServiceInstance::ListBuilds(FStringView Namespace, FStringView Bucket, FOnListBuildsComplete&& InOnListBuildsComplete)
{
	if (ConnectionState.load(std::memory_order_relaxed) != EConnectionState::ConnectionSucceeded)
	{
		if (InOnListBuildsComplete)
		{
			InOnListBuildsComplete({});
		}
		return;
	}

	FString QueryFilePath = FPaths::CreateTempFilename(FPlatformProcess::UserTempDir(), TEXT("BuildListQuery"), TEXT(".cbo"));
	FPaths::MakePlatformFilename(QueryFilePath);

	FString OutputFilePath = FPaths::CreateTempFilename(FPlatformProcess::UserTempDir(), TEXT("BuildList"), TEXT(".cbo"));
	FPaths::MakePlatformFilename(OutputFilePath);

	FString CommandLineArgs = FString::Printf(TEXT("builds list --namespace \"%s\" --bucket \"%s\" \"%s\" \"%s\""),
		*WriteToString<64>(Namespace), *WriteToString<64>(Bucket), *QueryFilePath, *OutputFilePath);
	CommandLineArgs = AddZenUtilityBuildServerArguments(*CommandLineArgs);

	auto PreInvoke = [QueryFilePath](FString& CommandLineArgs)
	{
		TUniquePtr<FArchive> QueryFileArchive(IFileManager::Get().CreateFileWriter(*QueryFilePath, FILEWRITE_NoFail));
		FCbWriter Writer;
		Writer.BeginObject("query");
		Writer.EndObject();
		Writer.Save(*QueryFileArchive);
	};

	InvokeZenUtility(CommandLineArgs, MoveTemp(PreInvoke),
	[OutputFilePath = MoveTemp(OutputFilePath), InOnListBuildsComplete = MoveTemp(InOnListBuildsComplete)](bool bSuccessful) mutable
	{
		if (!bSuccessful)
		{
			if (InOnListBuildsComplete)
			{
				InOnListBuildsComplete({});
			}
			return;
		}

		TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileReader(*OutputFilePath));
		if (!Ar)
		{
			UE_LOG(LogBuildServiceInstance, Warning, TEXT("Missing output file from zen utility when gathering build data: '%s'"), *OutputFilePath);
			if (InOnListBuildsComplete)
			{
				InOnListBuildsComplete({});
			}
			return;
		}

		if (InOnListBuildsComplete)
		{
			FCbObject OutputObject = LoadCompactBinary(*Ar).AsObject();
			TArray<FBuildRecord> BuildRecords;
			for (FCbFieldView ResultField : OutputObject["results"].AsArrayView())
			{
				FBuildRecord BuildRecord;
				if (LoadFromCompactBinary(ResultField, BuildRecord))
				{
					BuildRecords.Emplace(MoveTemp(BuildRecord));
				}
			}
			InOnListBuildsComplete(MoveTemp(BuildRecords));
		}

	});
}

FBuildServiceInstance::FBuildTransfer
FBuildServiceInstance::StartBuildTransfer(const FCbObjectId& BuildId, FStringView Name, FStringView DestinationFolder, FStringView Namespace, FStringView Bucket)
{
	TSharedPtr<Private::FBuildTransferState> NewState = MakeShared<Private::FBuildTransferState>();
	NewState->Type = FBuildTransfer::EType::Files;
	NewState->BuildId = BuildId;
	NewState->Name = Name;
	NewState->Namespace = Namespace;
	NewState->Bucket = Bucket;
	NewState->Destination = DestinationFolder;
	BuildTransferThreadStates.Enqueue(NewState.ToSharedRef());
	KickBuildTransferThread();

	FBuildTransfer NewTransfer;
	NewTransfer.State = NewState;
	return NewTransfer;
}

FBuildServiceInstance::FBuildTransfer
FBuildServiceInstance::StartOplogBuildTransfer(const FCbObjectId& BuildId, FStringView Name, FStringView DestinationProjectId, FStringView DestinationOplogId, FStringView ProjectFilePath, FStringView Namespace, FStringView Bucket, FName TargetPlatformName)
{
	TSharedPtr<Private::FBuildTransferState> NewState = MakeShared<Private::FBuildTransferState>();
	NewState->Type = FBuildTransfer::EType::Oplog;
	NewState->BuildId = BuildId;
	NewState->Name = Name;
	NewState->Namespace = Namespace;
	NewState->Bucket = Bucket;
	NewState->Destination = *WriteToString<64>(DestinationProjectId, TEXT("/"), DestinationOplogId);
	NewState->ProjectFilePath = ProjectFilePath;
	NewState->TargetPlatformName = TargetPlatformName;
	BuildTransferThreadStates.Enqueue(NewState.ToSharedRef());
	KickBuildTransferThread();

	FBuildTransfer NewTransfer;
	NewTransfer.State = NewState;
	return NewTransfer;
}

FBuildServiceInstance::FBuildTransfer
FBuildServiceInstance::RepeatBuildTransfer(FBuildTransfer BuildTransfer)
{
	TSharedPtr<Private::FBuildTransferState> NewState = MakeShared<Private::FBuildTransferState>();

	{
		FReadScopeLock _(BuildTransfer.State->Lock);
		NewState->Type = BuildTransfer.State->Type;
		NewState->BuildId = BuildTransfer.State->BuildId;
		NewState->Name = BuildTransfer.State->Name;
		NewState->Namespace = BuildTransfer.State->Namespace;
		NewState->Bucket = BuildTransfer.State->Bucket;
		NewState->Destination = BuildTransfer.State->Destination;
		NewState->ProjectFilePath = BuildTransfer.State->ProjectFilePath;
		NewState->TargetPlatformName = BuildTransfer.State->TargetPlatformName;
	}
	
	BuildTransferThreadStates.Enqueue(NewState.ToSharedRef());
	KickBuildTransferThread();

	FBuildTransfer NewTransfer;
	NewTransfer.State = NewState;
	return NewTransfer;
}

FBuildServiceInstance::FBuildTransfer::EType
FBuildServiceInstance::FBuildTransfer::GetType() const
{
	if (!State)
	{
		return EType::Count;
	}
	return State->Type;
}

FString FBuildServiceInstance::FBuildTransfer::GetDestination() const
{
	if (!State)
	{
		return FString();
	}
	return State->Destination;
}

FString FBuildServiceInstance::FBuildRecord::GetCommitIdentifier() const
{
	FString CommitIdentifier;
	if (FCbFieldView ChangelistField = Metadata["changelist"]; ChangelistField.HasValue() && !ChangelistField.HasError())
	{
		if (ChangelistField.IsString())
		{
			CommitIdentifier = FUTF8ToTCHAR(ChangelistField.AsString());
		}
		else if (ChangelistField.IsInteger())
		{
			CommitIdentifier = *WriteToString<64>(ChangelistField.AsUInt64());
		}
		else if (ChangelistField.IsFloat())
		{
			CommitIdentifier = *WriteToString<64>((uint64)ChangelistField.AsDouble());
		}
	}
	else if (FCbFieldView CommitField = Metadata["commit"]; CommitField.HasValue() && !CommitField.HasError())
	{
		if (CommitField.IsString())
		{
			CommitIdentifier = FUTF8ToTCHAR(CommitField.AsString());
		}
		else if (CommitField.IsInteger())
		{
			CommitIdentifier = *WriteToString<64>(CommitField.AsUInt64());
		}
		else if (CommitField.IsFloat())
		{
			CommitIdentifier = *WriteToString<64>((uint64)CommitField.AsDouble());
		}
	}

	return CommitIdentifier;
}

FString
FBuildServiceInstance::FBuildTransfer::GetDescription() const
{
	if (!State)
	{
		return TEXT("null");
	}

	FReadScopeLock _(State->Lock);
	if (State->NextOutputLineIndex == 0)
	{
		return FString();
	}
	return State->RecentOutputLines[State->RecentOutputLines.GetPreviousIndex(State->NextOutputLineIndex)];
}

FString
FBuildServiceInstance::FBuildTransfer::GetRecentOutput() const
{
	if (!State)
	{
		return TEXT("null");
	}

	FReadScopeLock _(State->Lock);
	return State->GetRecentOutput();
}

FBuildServiceInstance::EBuildTransferStatus
FBuildServiceInstance::FBuildTransfer::GetStatus() const
{
	if (!State)
	{
		return EBuildTransferStatus::Invalid;
	}

	FReadScopeLock _(State->Lock);
	return State->Status;
}

FString
FBuildServiceInstance::FBuildTransfer::GetLogFilename() const
{
	if (!State)
	{
		return FString();
	}

	FReadScopeLock _(State->Lock);
	return State->GetLogFilename();
}

bool
FBuildServiceInstance::FBuildTransfer::GetCurrentProgress(FString& OutLabel, FString& OutDetail, float& OutPercent) const
{
	if (!State)
	{
		return false;
	}

	FReadScopeLock _(State->Lock);
	if (State->ProgressState.IsEmpty())
	{
		if (State->NextOutputLineIndex == 0)
		{
			OutLabel.Empty();
		}
		else
		{
			OutLabel = State->RecentOutputLines[State->RecentOutputLines.GetPreviousIndex(State->NextOutputLineIndex)];
		}
		OutDetail = State->GetRecentOutput();
		OutPercent = 0.f;
		return true;
	}
	OutLabel = State->ProgressState.Top().Label;
	OutDetail = State->ProgressState.Top().Detail;
	OutPercent = State->ProgressState.Top().Percent;
	return true;
}

bool
FBuildServiceInstance::FBuildTransfer::GetOverallProgress(FString& OutLabel, FString& OutDetail, float& OutPercent) const
{
	if (!State)
	{
		return false;
	}

	FReadScopeLock _(State->Lock);
	if (State->ProgressState.IsEmpty())
	{
		if (State->NextOutputLineIndex == 0)
		{
			OutLabel.Empty();
		}
		else
		{
			OutLabel = State->RecentOutputLines[State->RecentOutputLines.GetPreviousIndex(State->NextOutputLineIndex)];
		}
		OutDetail = State->GetRecentOutput();
		OutPercent = 0.f;
		return true;
	}
	OutLabel = State->ProgressState[0].Label;
	OutDetail = State->ProgressState[0].Detail;
	OutPercent = State->ProgressState[0].Percent;
	return true;
}

void
FBuildServiceInstance::FBuildTransfer::RequestCancel()
{
	if (!State)
	{
		return;
	}
	State->bCancelRequested.store(true);
	FWriteScopeLock _(State->Lock);
	State->Status = EBuildTransferStatus::Canceled;
}

FBuildServiceInstance::EConnectionState
FBuildServiceInstance::GetConnectionState() const
{
	return ConnectionState.load(std::memory_order_relaxed);
}

FAnsiStringView
FBuildServiceInstance::GetEffectiveDomain() const
{
	return EffectiveDomain;
}

void
FBuildServiceInstance::Initialize()
{
#if WITH_SSL
	// Load SSL module during HTTP module's StatupModule() to make sure module manager figures out the dependencies correctly
	// and doesn't unload SSL before unloading HTTP module at exit
	FSslModule::Get();
#endif
	ISocketSubsystem::Get();
	FDesktopPlatformModule::TryGet();
	ZenService = MakePimpl<FScopeZenService>();
}

bool
FBuildServiceInstance::AcquireAccessToken(EDesktopLoginInteractionLevel InteractionLevel)
{
	if (GetEffectiveDomain().StartsWith("http://localhost"))
	{
		UE_LOG(LogBuildServiceInstance, Log, TEXT("Skipping authorization for connection to localhost."));
		return true;
	}

	LoginAttempts++;

	// Avoid spamming this if the service is down.
	if (FailedLoginAttempts > UE_BUILDSERVERINSTANCE_MAX_FAILED_LOGIN_ATTEMPTS)
	{
		return false;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(BuildServiceInstance_AcquireAccessToken);

	// In case many requests wants to update the token at the same time
	// get the current serial while we wait to take the CS.
	const uint32 WantsToUpdateTokenSerial = Access ? Access->GetSerial() : 0;

	FScopeLock Lock(&AccessCs);

	// If the token was updated while we waited to take the lock, then it should now be valid.
	if (Access && Access->GetSerial() > WantsToUpdateTokenSerial)
	{
		return true;
	}

	FString AccessTokenString;
	FDateTime TokenExpiresAt;
	bool bWasInteractiveLogin = false;

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::TryGet();
	if (DesktopPlatform && DesktopPlatform->GetOidcAccessTokenWithRemoteConfig(FPaths::RootDir(), *WriteToString<64>(EffectiveDomain), InteractionLevel, GWarn, AccessTokenString, TokenExpiresAt, bWasInteractiveLogin))
	{
		if (bWasInteractiveLogin)
		{
			InteractiveLoginAttempts++;
		}

		const double ExpiryTimeSeconds = (TokenExpiresAt - FDateTime::UtcNow()).GetTotalSeconds();
		UE_LOG(LogBuildServiceInstance, Display,
			TEXT("OidcToken: Logged in to HTTP DDC services. Expires at %s which is in %.0f seconds."),
			*TokenExpiresAt.ToString(), ExpiryTimeSeconds);
		SetAccessTokenAndUnlock(Lock, AccessTokenString, ExpiryTimeSeconds);
		return true;
	}
	else if (DesktopPlatform)
	{
		UE_LOG(LogBuildServiceInstance, Warning, TEXT("OidcToken: Failed to log in to HTTP services."));
		FailedLoginAttempts++;
		return false;
	}
	else
	{
		UE_LOG(LogBuildServiceInstance, Warning, TEXT("OidcToken: Use of OAuthProviderIdentifier requires that the target depend on DesktopPlatform."));
		FailedLoginAttempts++;
		return false;
	}
	return false;
}

void
FBuildServiceInstance::SetAccessTokenAndUnlock(FScopeLock& Lock, FStringView Token, double RefreshDelay)
{
	// Cache the expired refresh handle.
	FTSTicker::FDelegateHandle ExpiredRefreshAccessTokenHandle = MoveTemp(RefreshAccessTokenHandle);
	RefreshAccessTokenHandle.Reset();

	if (!Access)
	{
		Access = MakeUnique<Private::FAccessToken>();
	}
	Access->SetToken(Settings.GetAuthScheme(), Token);

	constexpr double RefreshGracePeriod = 20.0f;
	if (RefreshDelay > RefreshGracePeriod)
	{
		// Schedule a refresh of the token ahead of expiry time (this will not work in commandlets)
		if (!IsRunningCommandlet())
		{
			RefreshAccessTokenHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
				[this](float DeltaTime)
				{
					AcquireAccessToken(EDesktopLoginInteractionLevel::None);
					return false;
				}
			), float(FMath::Min(RefreshDelay - RefreshGracePeriod, MAX_flt)));
		}

		// Schedule a forced refresh of the token when the scheduled refresh is starved or unavailable.
		RefreshAccessTokenTime = FPlatformTime::Seconds() + RefreshDelay - RefreshGracePeriod * 0.5f;
	}
	else
	{
		RefreshAccessTokenTime = 0.0;
	}

	// Reset failed login attempts, the service is indeed alive.
	FailedLoginAttempts = 0;

	// Unlock the critical section before attempting to remove the expired refresh handle.
	// The associated ticker delegate could already be executing, which could cause a
	// hang in RemoveTicker when the critical section is locked.
	Lock.Unlock();
	if (ExpiredRefreshAccessTokenHandle.IsValid())
	{
		FTSTicker::RemoveTicker(MoveTemp(ExpiredRefreshAccessTokenHandle));
	}
}

FString
FBuildServiceInstance::GetAccessToken() const
{
	TAnsiStringBuilder<128> AccessTokenBuilder;
	if (Access.IsValid())
	{
		AccessTokenBuilder << *Access;
	}
	return FString(AccessTokenBuilder);
}

FString
FBuildServiceInstance::AddZenUtilityBuildServerArguments(const TCHAR* InCommandLineArgs) const
{
	return FString::Printf(TEXT("%s --host %s --access-token \"%s\""),
			InCommandLineArgs, *WriteToString<64>(EffectiveDomain), *GetAccessToken());
}

bool
FBuildServiceInstance::InvokeZenUtilitySync(FStringView InCommandLineArgs)
{
	FString ZenUtilityPath = UE::Zen::GetLocalInstallUtilityPath();

	FString CommandLineArgs(InCommandLineArgs);

	FString AbsoluteUtilityPath = FPaths::ConvertRelativePathToFull(ZenUtilityPath);
	FMonitoredProcess MonitoredUtilityProcess(AbsoluteUtilityPath, *CommandLineArgs, FPaths::GetPath(AbsoluteUtilityPath), true);
	if (!MonitoredUtilityProcess.Launch())
	{
		UE_LOG(LogBuildServiceInstance, Warning, TEXT("Failed to launch zen utility to gather build data: '%s'."), *AbsoluteUtilityPath);
		return false;
	}

	const uint64 StartTime = FPlatformTime::Cycles64();
	while (MonitoredUtilityProcess.Update())
	{
		double Duration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);
		if (Duration > 120.0)
		{
			MonitoredUtilityProcess.Cancel(true);
			UE_LOG(LogBuildServiceInstance, Warning, TEXT("Cancelled launch of zen utility for gathering build data: '%s' due to timeout."), *AbsoluteUtilityPath);

			// Wait for execution to be terminated
			while (MonitoredUtilityProcess.Update())
			{
				Duration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);
				if (Duration > 15.0)
				{
					UE_LOG(LogBuildServiceInstance, Warning, TEXT("Cancelled launch of zen utility for gathering build data: '%s'. Failed waiting for termination."), *AbsoluteUtilityPath);
					break;
				}
				FPlatformProcess::Sleep(0.2f);
			}

			FString OutputString = MonitoredUtilityProcess.GetFullOutputWithoutDelegate();
			UE_LOG(LogBuildServiceInstance, Warning, TEXT("Launch of zen utility for gathering build data: '%s' failed. Output: '%s'"), *AbsoluteUtilityPath, *OutputString);
			return false;
		}
		FPlatformProcess::Sleep(0.1f);
	}

	FString OutputString = MonitoredUtilityProcess.GetFullOutputWithoutDelegate();
	if (MonitoredUtilityProcess.GetReturnCode() != 0)
	{
		UE_LOG(LogBuildServiceInstance, Warning, TEXT("Unexpected return code after launch of zen utility for gathering build data: '%s' (%d). Output: '%s'"), *AbsoluteUtilityPath, MonitoredUtilityProcess.GetReturnCode(), *OutputString);
		return false;
	}

	return true;
}

void
FBuildServiceInstance::InvokeZenUtility(FStringView InCommandLineArgs, FPreZenUtilityInvocation&& PreInvocation, FOnZenUtilityInvocationComplete&& OnComplete)
{
	UE::Tasks::Launch(UE_SOURCE_LOCATION,
	[this, InCommandLineArgs = FString(InCommandLineArgs), PreInvocation = MoveTemp(PreInvocation), OnComplete = MoveTemp(OnComplete)]() mutable
	{
		if (PreInvocation)
		{
			PreInvocation(InCommandLineArgs);
		}

		bool bSuccessfulInvocation = InvokeZenUtilitySync(InCommandLineArgs);

		if (OnComplete)
		{
			OnComplete(bSuccessfulInvocation);
		}
	});
}

void
FBuildServiceInstance::CallOnRefreshNamespacesAndBucketsComplete(FOnRefreshNamespacesAndBucketsComplete&& InOnRefreshNamespacesAndBucketsComplete)
{
	if (InOnRefreshNamespacesAndBucketsComplete)
	{
		InOnRefreshNamespacesAndBucketsComplete();
	}

	RefreshNamespacesAndBucketsComplete.Broadcast();
}

void
FBuildServiceInstance::KickBuildTransferThread()
{
	if (!bBuildTransferThreadStarting.load(std::memory_order_relaxed) && !bBuildTransferThreadStarting.exchange(true, std::memory_order_relaxed))
	{
		BuildTransferThread = FThread(TEXT("BuildTransfer"), [this] { BuildTransferThreadLoop(); }, 128 * 1024);
	}
}

void
FBuildServiceInstance::BuildTransferThreadLoop()
{
	while (!BuildTransferThreadStates.IsEmpty() || !bBuildTransferThreadStopping.load(std::memory_order_relaxed))
	{
		TOptional<TSharedRef<Private::FBuildTransferState>> OptionalState = BuildTransferThreadStates.Dequeue();
		if (!OptionalState)
		{
			FPlatformProcess::Sleep(0.2f);
			continue;
		}
		TSharedRef<Private::FBuildTransferState> State(OptionalState.GetValue());

		if (State->bCancelRequested.load(std::memory_order_relaxed))
		{
			FWriteScopeLock _(State->Lock);
			State->Status = EBuildTransferStatus::Canceled;
			continue;
		}

 		FString ZenUtilityPath = FPaths::ConvertRelativePathToFull(UE::Zen::GetLocalInstallUtilityPath());
 		FString OidcTokenExecutableFilename = FPaths::ConvertRelativePathToFull(FDesktopPlatformModule::TryGet()->GetOidcTokenExecutableFilename(FPaths::RootDir()));

 		FString DestinationDirectory;
		FString CommandLineArgs;
		switch (State->Type)
		{
		case FBuildTransfer::EType::Files:
			DestinationDirectory = FPaths::ConvertRelativePathToFull(State->Destination);
			CommandLineArgs = FString::Printf(TEXT("builds download --log-progress --host %s --local-path \"%s\" --namespace %s --bucket %s --build-id %s --oidctoken-exe-path \"%s\""),
				*WriteToString<64>(EffectiveDomain), *DestinationDirectory, *State->Namespace, *State->Bucket,
				*WriteToString<64>(State->BuildId), *OidcTokenExecutableFilename);
			break;
		case FBuildTransfer::EType::Oplog:
			{
				FString DestinationProjectId, DestinationOplogId;
				FString SplitDelim(TEXT("/"));
				State->Destination.Split(SplitDelim, &DestinationProjectId, &DestinationOplogId);

				UE::Zen::FZenLocalServiceRunContext RunContext;
				uint16 LocalPort = 8558;
				if (UE::Zen::TryGetLocalServiceRunContext(RunContext))
				{
					if (!UE::Zen::IsLocalServiceRunning(*RunContext.GetDataPath(), &LocalPort))
					{
						UE::Zen::StartLocalService(RunContext);
						UE::Zen::IsLocalServiceRunning(*RunContext.GetDataPath(), &LocalPort);
					}
				}

				if (!State->ProjectFilePath.IsEmpty())
				{
					DestinationDirectory = FPaths::Combine(FPaths::GetPath(State->ProjectFilePath),
														TEXT("Saved"),
														TEXT("Cooked"),
														DestinationOplogId);

					FString OplogLifetimeMarkerPath = FPaths::Combine(DestinationDirectory,
														TEXT("ue.projectstore"));

					Zen::FZenProjectStoreWriter ProjectStoreWriter(*OplogLifetimeMarkerPath);
					ProjectStoreWriter.Write(TEXT("[::1]"), LocalPort, true, DestinationProjectId, DestinationOplogId, State->TargetPlatformName);

					FString RootDir = FPaths::RootDir();
					FString EngineDir = FPaths::EngineDir();
					FPaths::NormalizeDirectoryName(EngineDir);
					FString ProjectDir = FPaths::GetPath(State->ProjectFilePath);
					FPaths::NormalizeDirectoryName(ProjectDir);
					FString ProjectPath = State->ProjectFilePath;
					FPaths::NormalizeFilename(ProjectPath);

					IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
					FString AbsServerRoot = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*RootDir);
					FString AbsEngineDir = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*EngineDir);
					FString AbsProjectDir = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*ProjectDir);
					FString ProjectFilePath = PlatformFile.ConvertToAbsolutePathForExternalAppForRead(*ProjectPath);

					InvokeZenUtilitySync(FString::Printf(TEXT("project-create --project \"%s\" --rootdir \"%s\" --enginedir \"%s\" --projectdir \"%s\" --projectfile \"%s\""),
						*DestinationProjectId, *AbsServerRoot, *AbsEngineDir, *AbsProjectDir, *ProjectFilePath));

					InvokeZenUtilitySync(FString::Printf(TEXT("oplog-create --project \"%s\" --oplog \"%s\" --gcpath \"%s\""),
						*DestinationProjectId, *DestinationOplogId, *OplogLifetimeMarkerPath));
				}
				else
				{
					InvokeZenUtilitySync(FString::Printf(TEXT("project-create --project \"%s\""),
						*DestinationProjectId));

					InvokeZenUtilitySync(FString::Printf(TEXT("oplog-create --project \"%s\" --oplog \"%s\""),
						*DestinationProjectId, *DestinationOplogId));
				}

				CommandLineArgs = FString::Printf(TEXT("oplog-import %s %s --clean --builds %s --namespace %s --bucket %s --builds-id %s --access-token \"%s\""),
					*DestinationProjectId, *DestinationOplogId, *WriteToString<64>(EffectiveDomain),
					*State->Namespace, *State->Bucket,
					*WriteToString<64>(State->BuildId), *GetAccessToken());


			}
			break;
		}

		FMonitoredProcess MonitoredUtilityProcess(ZenUtilityPath, *CommandLineArgs, FPaths::GetPath(ZenUtilityPath), true);
		MonitoredUtilityProcess.OnOutput().BindSPLambda(State, [State](FString Output)
		{
			FReadScopeLock _(State->Lock);
			if (Output.StartsWith(TEXT("@progress push")))
			{
				Output = Output.RightChop(15);
				Private::FProgressState NewState;
				NewState.Label = Output;
				State->ProgressState.Push(MoveTemp(NewState));
				return;
			}
			else if (Output.StartsWith(TEXT("@progress pop")))
			{
				State->ProgressState.Pop();
				return;
			}
			else if (Output.StartsWith(TEXT("@progress")))
			{
				if (State->ProgressState.IsEmpty())
				{
					Private::FProgressState NewState;
					State->ProgressState.Push(MoveTemp(NewState));
				}
				Private::FProgressState& CurrentState = State->ProgressState.Top();
				Output = Output.RightChop(10);
				if (Output.EndsWith(TEXT("%")))
				{
					FString PossiblePercentText = Output.LeftChop(1);
					if (!PossiblePercentText.IsEmpty())
					{
						float Percent = FCString::Atof(*PossiblePercentText);
						if ((Percent != 0.0f) || (PossiblePercentText.Len() == 1 && PossiblePercentText[0] == TCHAR('0')))
						{
							CurrentState.Percent = Percent;
							return; // Do not print the percent progress alone
						}
					}
				}
				CurrentState.Detail = Output;
				Output = FString::Printf(TEXT("[%s %d%%] %s"), *CurrentState.Label, (int32)CurrentState.Percent, *Output);
				// Deliberate fall-through so that the text is printed;
			}
			if (State->OutputLogFile)
			{
				State->OutputLogFile->Logf(TEXT("%s"), *Output);
				State->OutputLogFile->Flush();
			}
			State->RecentOutputLines[State->NextOutputLineIndex++] = MoveTemp(Output);
		});

		{
			FWriteScopeLock _(State->Lock);
			FString LogFilename = State->GetLogFilename();
			FOutputDeviceFile::CreateBackupCopy(*LogFilename);
			State->OutputLogFile = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*LogFilename, FILEWRITE_Silent | FILEWRITE_AllowRead));
			State->Status = EBuildTransferStatus::Active;
		}

		if (!MonitoredUtilityProcess.Launch())
		{
			UE_LOG(LogBuildServiceInstance, Warning, TEXT("Failed to launch zen utility to download build: '%s'."), *ZenUtilityPath);
			FWriteScopeLock _(State->Lock);
			State->Status = EBuildTransferStatus::Failed;
			State->OutputLogFile.Reset();
			continue;
		}

		const uint64 StartTime = FPlatformTime::Cycles64();
		while (MonitoredUtilityProcess.Update())
		{
			if (bBuildTransferThreadStopping.load(std::memory_order_relaxed))
			{
				return;
			}
			double Duration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);
			if (bBuildTransferThreadStopping.load(std::memory_order_relaxed) || State->bCancelRequested.load(std::memory_order_relaxed))
			{
				MonitoredUtilityProcess.Cancel(true);
				UE_LOG(LogBuildServiceInstance, Display, TEXT("Canceled launch of zen utility for downloading build data: '%s'."), *ZenUtilityPath);

				// Wait for execution to be terminated
				while (MonitoredUtilityProcess.Update())
				{
					if (bBuildTransferThreadStopping.load(std::memory_order_relaxed))
					{
						return;
					}
					Duration = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);
					if (Duration > 15.0)
					{
						UE_LOG(LogBuildServiceInstance, Warning, TEXT("Canceled launch of zen utility for downloading build data: '%s'. Failed waiting for termination."), *ZenUtilityPath);
						break;
					}
					FPlatformProcess::Sleep(0.2f);
				}

				UE_LOG(LogBuildServiceInstance, Display, TEXT("Launch of zen utility for downloading build data: '%s' canceled."), *ZenUtilityPath);
				FWriteScopeLock _(State->Lock);
				State->Status = EBuildTransferStatus::Canceled;
				State->OutputLogFile.Reset();
				break;
			}
			FPlatformProcess::Sleep(0.1f);
		}

		if (!State->bCancelRequested.load(std::memory_order_relaxed))
		{
			if (MonitoredUtilityProcess.GetReturnCode() == 0)
			{
				FWriteScopeLock _(State->Lock);
				State->ReturnCode = MonitoredUtilityProcess.GetReturnCode();
				State->Status = EBuildTransferStatus::Succeeded;
				State->OutputLogFile.Reset();
			}
			else
			{
				FWriteScopeLock _(State->Lock);
				UE_LOG(LogBuildServiceInstance, Warning, TEXT("Unexpected return code after launch of zen utility for downloading build data: '%s' (%d). Output: '%s'"), *ZenUtilityPath, MonitoredUtilityProcess.GetReturnCode(), *State->GetRecentOutput());
				State->ReturnCode = MonitoredUtilityProcess.GetReturnCode();
				State->Status = EBuildTransferStatus::Failed;
				State->OutputLogFile.Reset();
			}
		}

		bool bCleanDestination = false;
		{
			FReadScopeLock _(State->Lock);
			bCleanDestination = (State->Status == EBuildTransferStatus::Succeeded) &&
				(State->Type == FBuildTransfer::EType::Oplog) &&
				!DestinationDirectory.IsEmpty();
		}

		if (bCleanDestination)
		{
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			PlatformFile.IterateDirectory(*DestinationDirectory,
				[&PlatformFile](const TCHAR* FoundFullPath, bool bDirectory)
				{
					if (bDirectory)
					{
						PlatformFile.DeleteDirectoryRecursively(FoundFullPath);
					}
					else
					{
						if (FPathViews::GetCleanFilename(FoundFullPath) != TEXT("ue.projectstore"))
						{
							PlatformFile.DeleteFile(FoundFullPath);
						}
					}
					return true;
				});
		}
	}
}

} // namespace UE::StorageService::Build
