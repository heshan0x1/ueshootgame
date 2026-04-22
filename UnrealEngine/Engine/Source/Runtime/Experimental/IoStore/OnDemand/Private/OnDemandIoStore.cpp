// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnDemandIoStore.h"

#include "Algo/Accumulate.h"
#include "Algo/AnyOf.h"
#include "Algo/RemoveIf.h"
#include "Algo/Transform.h"
#include "Async/UniqueLock.h"
#include "Containers/RingBuffer.h"
#include "Containers/StringConv.h"
#include "Containers/Ticker.h"
#include "DebugCommands.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/LowLevelMemTracker.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IndexedCacheStorageManager.h"
#include "IO/IoChunkEncoding.h"
#include "IO/IoContainerHeader.h"
#include "IO/IoStatus.h"
#include "IO/IoStoreOnDemand.h"
#include "Serialization/PackageStore.h"
#include "IasCache.h"
#include "Logging/StructuredLog.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CoreDelegatesInternal.h"
#include "Misc/EncryptionKeyManager.h"
#include "Misc/Guid.h"
#include "Misc/PathViews.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "OnDemandConfig.h"
#include "OnDemandHttpClient.h"
#include "OnDemandHttpIoDispatcher.h"
#include "OnDemandHttpIoDispatcherBackend.h"
#include "OnDemandHttpThread.h"
#include "OnDemandInstallCache.h"
#include "OnDemandContentInstaller.h"
#include "OnDemandIoDispatcherBackend.h"
#include "OnDemandPackageStoreBackend.h"
#include "ProfilingDebugging/AssetMetadataTrace.h"
#include "Serialization/MemoryReader.h"
#include "Statistics.h"

#if !(UE_BUILD_SHIPPING|UE_BUILD_TEST)
#include "String/LexFromString.h"
#endif

///////////////////////////////////////////////////////////////////////////////
bool GHttpIoDispatcherEnabled = false;
static FAutoConsoleVariableRef CVar_HttpIoDispatcherEnabled(
	TEXT("iax.HttpIoDispatcherEnabled"),
	GHttpIoDispatcherEnabled,
	TEXT("Whether the HTTP I/O dispatcher should be enabled."),
	ECVF_SaveForNextBoot
);

///////////////////////////////////////////////////////////////////////////////
namespace UE::IoStore
{

///////////////////////////////////////////////////////////////////////////////
namespace Private
{

///////////////////////////////////////////////////////////////////////////////
static FAnsiStringBuilderBase& GetChunkUrl(
	const FAnsiStringView& Host,
	const FOnDemandContainer& Container,
	const FOnDemandChunkEntry& Entry,
	FAnsiStringBuilderBase& OutUrl)
{
	OutUrl.Reset();
	if (Host.IsEmpty() == false)
	{
		OutUrl << Host;
	}

	if (!Container.ChunksDirectory.IsEmpty())
	{
		OutUrl << "/" << Container.ChunksDirectory;
	}

	const FString HashString = LexToString(Entry.Hash);
	OutUrl << "/" << HashString.Left(2) << "/" << HashString << ".iochunk";

	return OutUrl;
}

static FAnsiStringBuilderBase& GetChunkUrl(
	const FOnDemandContainer& Container,
	const FOnDemandChunkEntry& Entry,
	FAnsiStringBuilderBase& OutUrl)
{
	OutUrl.Reset();
	if (!Container.ChunksDirectory.IsEmpty())
	{
		OutUrl << "/" << Container.ChunksDirectory;
	}

	const FString HashString = LexToString(Entry.Hash);
	OutUrl << "/" << HashString.Left(2) << "/" << HashString << ".iochunk";

	return OutUrl;
}

///////////////////////////////////////////////////////////////////////////////
static TIoStatusOr<FIoBuffer> DecodeChunk(const FOnDemandChunkInfo& ChunkInfo, FMemoryView EncodedChunk)
{
	FIoChunkDecodingParams Params;
	Params.CompressionFormat = ChunkInfo.CompressionFormat();
	Params.EncryptionKey = ChunkInfo.EncryptionKey();
	Params.BlockSize = ChunkInfo.BlockSize();
	Params.TotalRawSize = ChunkInfo.RawSize();
	Params.RawOffset = 0;
	Params.EncodedOffset = 0;
	Params.EncodedBlockSize = ChunkInfo.Blocks();
	Params.BlockHash = ChunkInfo.BlockHashes();

	FIoBuffer OutRawChunk = FIoBuffer(ChunkInfo.RawSize());
	if (FIoChunkEncoding::Decode(Params, EncodedChunk, OutRawChunk.GetMutableView()) == false)
	{
		FIoStatus Status = FIoStatusBuilder(EIoErrorCode::ReadError)
			<< TEXT("Failed to decode container chunk");
		return Status;
	}

	return OutRawChunk;
}

///////////////////////////////////////////////////////////////////////////////
static TIoStatusOr<FSharedContainerHeader> DeserializeContainerHeader(const FOnDemandChunkInfo& ChunkInfo, FMemoryView EncodedHeaderChunk, bool bReadSoftRefs)
{
	TIoStatusOr<FIoBuffer> Chunk = DecodeChunk(ChunkInfo, EncodedHeaderChunk);
	if (Chunk.IsOk() == false)
	{
		return Chunk.Status();
	}

	FSharedContainerHeader OutHeader = MakeShared<FIoContainerHeader>();
	FMemoryReaderView Ar(Chunk.ValueOrDie().GetView());
	Ar << *OutHeader;
	if (bReadSoftRefs && OutHeader->SoftPackageReferencesSerialInfo.Size > 0)
	{
		if (OutHeader->SoftPackageReferencesSerialInfo.Offset < 0)
		{
			FIoStatus Status = FIoStatusBuilder(EIoErrorCode::ReadError)
				<< FString::Printf(TEXT("Invalid soft package reference offset '%" INT64_FMT "'"), OutHeader->SoftPackageReferencesSerialInfo.Offset);
			return Status;
		}
		if ((OutHeader->SoftPackageReferencesSerialInfo.Offset + OutHeader->SoftPackageReferencesSerialInfo.Size) > Ar.TotalSize())
		{
			FIoStatus Status = FIoStatusBuilder(EIoErrorCode::ReadError)
				<< FString::Printf(TEXT("Soft package reference offset '%" INT64_FMT "' and size '%" INT64_FMT "' will seek past the end of archive size '%" INT64_FMT "'"),
					OutHeader->SoftPackageReferencesSerialInfo.Offset,
					OutHeader->SoftPackageReferencesSerialInfo.Size,
					Ar.TotalSize());
			return Status;
		}
		Ar.Seek(OutHeader->SoftPackageReferencesSerialInfo.Offset);
		Ar << OutHeader->SoftPackageReferences;
	}
	Ar.Close();

	if (Ar.IsError() || Ar.IsCriticalError())
	{
		FIoStatus Status = FIoStatusBuilder(EIoErrorCode::FileNotOpen) << TEXT("Failed to serialize container header");
		return Status;
	}

	return OutHeader; 
}

} // namespace UE::IoStore::Private

///////////////////////////////////////////////////////////////////////////////
const FOnDemandChunkEntry FOnDemandChunkEntry::Null = {};

///////////////////////////////////////////////////////////////////////////////
static FString OnDemandContainerUniqueName(FStringView MountId, FStringView Name)
{
	return FString::Printf(TEXT("%.*s-%.*s"), MountId.Len(), MountId.GetData(), Name.Len(), Name.GetData());
}

FString FOnDemandContainer::UniqueName() const
{
	return OnDemandContainerUniqueName(MountId, Name);
}

///////////////////////////////////////////////////////////////////////////////
FOnDemandIoStore::FOnDemandIoStore()
{
#if !UE_BUILD_SHIPPING
	DebugCommands = MakeUnique<FOnDemandDebugCommands>(this);
#endif // !UE_BUILD_SHIPPING

	FEncryptionKeyManager::Get().OnKeyAdded().AddRaw(this, &FOnDemandIoStore::OnEncryptionKeyAdded);
}

FOnDemandIoStore::~FOnDemandIoStore()
{
	FEncryptionKeyManager::Get().OnKeyAdded().RemoveAll(this);

	if (OnMountPakHandle.IsValid())
	{
		FCoreInternalDelegates::GetOnPakMountOperation().Remove(OnMountPakHandle);
	}

	if (OnServerPostForkHandle.IsValid())
	{
		FCoreDelegates::OnPostFork.Remove(OnServerPostForkHandle);
	}

	if (TickFuture.IsValid())
	{
		TickFuture.Wait();
	}

	Installer.Reset();
	HttpClient.Reset();

	if (FHttpIoDispatcher::IsInitialized())
	{
		FHttpIoDispatcher::OnHostGroupRegistered().RemoveAll(this);
		FHttpIoDispatcher::Shutdown();
	}
}

FIoStatus FOnDemandIoStore::Initialize()
{
	LLM_SCOPE_BYNAME(TEXT("FileSystem/OnDemandIoStore"));

#if 0
	OnMountPakHandle = FCoreInternalDelegates::GetOnPakMountOperation().AddLambda(
		[this](EMountOperation Operation, const TCHAR* ContainerPath, int32 Order) -> void
		{
			IPlatformFile& Ipf = IPlatformFile::GetPlatformPhysical();
			const FString OnDemandTocPath = FPathViews::ChangeExtension(ContainerPath, *FOnDemandToc::FileExt);

			if (Ipf.FileExists(*OnDemandTocPath) == false)
			{
				return;
			}

			switch (Operation)
			{
			case EMountOperation::Mount:
			{
				UE::FManualResetEvent DoneEvent;
				Mount(FOnDemandMountArgs
				{
					.MountId = OnDemandTocPath,
					.FilePath = OnDemandTocPath
				},
				[&DoneEvent](TIoStatusOr<FOnDemandMountResult> Result)
				{
					UE_CLOG(!Result.IsOk(), LogIoStoreOnDemand, Error,
						TEXT("Failed to mount container, reason '%s'"), *Result.Status().ToString());
					DoneEvent.Notify();
				});
				DoneEvent.Wait();
				break;
			}
			case EMountOperation::Unmount:
			{
				const FIoStatus Status = Unmount(OnDemandTocPath);
				UE_CLOG(!Status.IsOk(), LogIoStoreOnDemand, Error,
					TEXT("Failed to unmount container, reason '%s'"), *Status.ToString());
				break;
			}
			default:
				checkNoEntry();
			}
		}
	);

	{
		FCurrentlyMountedPaksDelegate& Delegate = FCoreInternalDelegates::GetCurrentlyMountedPaksDelegate();
		if (Delegate.IsBound())
		{
			IPlatformFile& Ipf = IPlatformFile::GetPlatformPhysical();
			TArray<FMountedPakInfo> PakInfo = Delegate.Execute();
			for (const FMountedPakInfo& Info : PakInfo)
			{
				check(Info.PakFile != nullptr);
				const FString OnDemandTocPath = FPathViews::ChangeExtension(Info.PakFile->PakGetPakFilename(), *FOnDemandToc::FileExt);
				if (Ipf.FileExists(*OnDemandTocPath))
				{
					FSharedMountRequest MountRequest = MakeShared<FMountRequest>();
					MountRequest->Args = FOnDemandMountArgs
					{
						.MountId	= OnDemandTocPath,
						.FilePath	= OnDemandTocPath
					};
					MountRequest->OnCompleted = [](TIoStatusOr<FOnDemandMountResult> Result)
					{
						UE_CLOG(!Result.IsOk(), LogIoStoreOnDemand, Error, TEXT("Failed to mount container, reason '%s'"),
							*Result.Status().ToString());
					};
					MountRequests.Add(MoveTemp(MountRequest));
				}
			}

			// Process mount requests synchronously at startup
			TickLoop();
		}
	}
#endif


	FIoStatus InstallCacheStatus = InitializeInstallCache();
	if (InstallCacheStatus.GetErrorCode() == EIoErrorCode::PendingFork)
	{
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Deferring initialization of install cache until post server fork"))
		OnServerPostForkHandle = FCoreDelegates::OnPostFork.AddLambda(
			[this](EForkProcessRole ProcessRole)
			{
				if (ProcessRole == EForkProcessRole::Child)
				{
					if (FIoStatus Status = InitializeInstallCache(); !Status.IsOk())
					{
						if (Status.GetErrorCode() == EIoErrorCode::Disabled)
						{
							UE_LOG(LogIoStoreOnDemand, Log, TEXT("Install cache disabled"));
						}
						else
						{
							UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to initialize install cache, reason '%s'"), *Status.ToString());
						}
					}
				}
			});
		InstallCacheStatus = FIoStatus::Ok;
	}
	else if (InstallCacheStatus.GetErrorCode() == EIoErrorCode::Disabled)
	{
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Install cache disabled"));
	}

	const bool bHttpIoDispatcherEnabled = (GHttpIoDispatcherEnabled || FParse::Param(FCommandLine::Get(), TEXT("HttpIoDispatcher")))
		&& !FParse::Param(FCommandLine::Get(), TEXT("NoHttpIoDispatcher"));
	FOnDemandIoBackendStats::Get()->SetHttpIoDispatcherEnabled(bHttpIoDispatcherEnabled);

	if (bHttpIoDispatcherEnabled)
	{
		TUniquePtr<IIasCache> HttpCache;
		FIasCacheConfig CacheConfig = Config::GetStreamingCacheConfig(FCommandLine::Get());

		if (CacheConfig.DiskQuota > 0)
		{
			if (FPaths::HasProjectPersistentDownloadDir())
			{
				FString CacheDir = FPaths::ProjectPersistentDownloadDir();
				HttpCache = MakeIasCache(*CacheDir, CacheConfig, DiskCacheGovernor);

				UE_CLOG(!HttpCache.IsValid(), LogIoStoreOnDemand, Warning, TEXT("HTTP cache disabled"));
			}
			else
			{
				UE_LOG(LogIoStoreOnDemand, Warning, TEXT("HTTP cache disabled - streaming only (project has no persistent download dir enabled for this platform)"));
			}
		}
		else
		{
			UE_LOG(LogIoStoreOnDemand, Log, TEXT("HTTP cache disabled - streaming only (zero-quota)"));
		}

		TSharedPtr<IOnDemandHttpIoDispatcher> HttpDispatcher = MakeOnDemanHttpIoDispatcher(MoveTemp(HttpCache));
		if (FIoStatus Status = FHttpIoDispatcher::Initialize(HttpDispatcher); !Status.IsOk())
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to initialize HTTP I/O dispatcher"));
		}
		
		int32 HttpBackendPriority = -10;
#if !UE_BUILD_SHIPPING
		if (FParse::Param(FCommandLine::Get(), TEXT("Ias")))
		{
			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Setting HTTP backend priority higher than file system backend"));
			HttpBackendPriority = 10;
		}
#endif
		HttpIoBackend = MakeOnDemandHttpIoDispatcherBackend(*this);
		FIoDispatcher::Get().Mount(HttpIoBackend.ToSharedRef(), HttpBackendPriority);
		FHttpIoDispatcher::OnHostGroupRegistered().AddRaw(this, &FOnDemandIoStore::OnHostGroupRegistered);
	}
	else
	{
		HttpClient = MakeUnique<FOnDemandHttpThread>();
	}

	Installer = MakeUnique<FOnDemandContentInstaller>(*this, HttpClient.Get());

	return InstallCacheStatus;
}

FIoStatus FOnDemandIoStore::InitializePostHotfix()
{
	const TCHAR* CommandLine = FCommandLine::Get();

	TIoStatusOr<FOnDemandEndpointConfig> EndpointConfigStatus = Config::TryParseEndpointConfig(CommandLine);
	if (EndpointConfigStatus.IsOk() == false)
	{
		// Temporary solution: Delete the ias cache directory if the game hasn't been configured to use IAS
		if (FPaths::HasProjectPersistentDownloadDir())
		{
			TStringBuilder<256> CachePath;
			CachePath << FPaths::ProjectPersistentDownloadDir();
			FPathViews::Append(CachePath, TEXTVIEW("ias"));

			IFileManager& Ifm = IFileManager::Get();
			if (Ifm.DirectoryExists(CachePath.ToString()))
			{
				Ifm.DeleteDirectory(CachePath.ToString(), false, true);
			}
		}

		return EndpointConfigStatus.Status();
	}

	FOnDemandEndpointConfig EndpointConfig = EndpointConfigStatus.ConsumeValueOrDie();

	{
		bool bFirstAttempt = false;
		if (FIoStatus CertStatus = LoadDefaultHttpCertificates(bFirstAttempt); !CertStatus.IsOk())
		{
			if (EndpointConfig.RequiresTls())
			{
				FIoStatus Status = FIoStatusBuilder(EIoErrorCode::ReadError)
					<< TEXT("Endpoint requires TLS but certificates failed to load due to '")
					<< CertStatus.ToString()
					<< TEXT("'");
				return Status;
			}
		}
	}

	if (FHttpIoDispatcher::IsInitialized() == false)
	{
		if (FIoStatus Status = InitializeStreamingBackend(EndpointConfig); Status.IsOk() == false)
		{
			return Status;
		}
	}

	bool bUsePerContainerTocsConfigValue = false;
	if (GConfig)
	{
		GConfig->GetBool(TEXT("Ias"), TEXT("UsePerContainerTocs"), bUsePerContainerTocsConfigValue, GEngineIni);
	}
	bool bUsePerContainerTocsParam = false;
#if !UE_BUILD_SHIPPING
	bUsePerContainerTocsParam = FParse::Param(CommandLine, TEXT("Ias.UsePerContainerTocs"));
#endif

	const bool bUsePerContainerTocs = bUsePerContainerTocsConfigValue || bUsePerContainerTocsParam;
#if !UE_BUILD_SHIPPING
	UE_LOG(LogIas, Log, TEXT("Using per container TOCs=%s"), bUsePerContainerTocs ? TEXT("True") : TEXT("False"));
#endif

	TOptional<FOnDemandMountArgs> MountArgs;
	if (EndpointConfig.TocFilePath.IsEmpty() == false)
	{
		FOnDemandHostGroup HostGroup;
		if (EndpointConfig.ServiceUrls.IsEmpty() == false)
		{
			TIoStatusOr<FOnDemandHostGroup> HostGroupStatus = FOnDemandHostGroup::Create(EndpointConfig.ServiceUrls);
			if (HostGroupStatus.IsOk())
			{
				HostGroup = HostGroupStatus.ConsumeValueOrDie();
			}
		}
		
		if (bUsePerContainerTocs == false)
		{
			MountArgs.Emplace(FOnDemandMountArgs
			{
				.MountId = EndpointConfig.TocFilePath,
				.FilePath = EndpointConfig.TocFilePath,
				.HostGroup = HostGroup,
				.Options = EOnDemandMountOptions::StreamOnDemand
			});
		}
	}
	else if (!EndpointConfig.ServiceUrls.IsEmpty() && !EndpointConfig.TocPath.IsEmpty())
	{
		TIoStatusOr<FOnDemandHostGroup> HostGroup = FOnDemandHostGroup::Create(EndpointConfig.ServiceUrls);
		if (HostGroup.IsOk() == false)
		{
			return HostGroup.Status();
		}
		MountArgs.Emplace(FOnDemandMountArgs
		{
			.MountId = EndpointConfig.ServiceUrls[0] / EndpointConfig.TocPath,
			.TocRelativeUrl = EndpointConfig.TocPath,
			.HostGroup = HostGroup.ConsumeValueOrDie(),
			.Options = EOnDemandMountOptions::StreamOnDemand
		});
	}

#if !UE_BUILD_SHIPPING
	TOptional<FOnDemandInstallArgs> InstallArgs;
	if (FParse::Param(FCommandLine::Get(), TEXT("Iad.InstallAllContent")))
	{
		TIoStatusOr<FOnDemandHostGroup> HostGroup = FOnDemandHostGroup::Create(EndpointConfig.ServiceUrls);
		check(HostGroup.IsOk());

		if (MountArgs)
		{
			MountArgs.GetValue().HostGroup = HostGroup.ConsumeValueOrDie(); 
			MountArgs.GetValue().Options = EOnDemandMountOptions::InstallOnDemand;
			MountArgs.GetValue().TocRelativeUrl = EndpointConfig.TocPath;

			static FOnDemandContentHandle ContentHandle = FOnDemandContentHandle::Create(TEXT("AllContent"));
			InstallArgs.Emplace();
			InstallArgs->MountId = MountArgs.GetValue().MountId;
			InstallArgs->ContentHandle = ContentHandle;
		}
	}
#endif

	if (MountArgs)
	{
		Mount(MoveTemp(MountArgs.GetValue()), [](FOnDemandMountResult MountResult)
		{
			MountResult.LogResult();
		});
#if !UE_BUILD_SHIPPING
		if (InstallArgs)
		{
			FEventRef InstallDone;
			Install(MoveTemp(InstallArgs.GetValue()), [&InstallDone](FOnDemandInstallResult InstallResult)
			{
				UE_CLOG(!InstallResult.IsOk(), LogIoStoreOnDemand, Error,
					TEXT("Failed to install content, reason '%s'"), *LexToString(InstallResult.Error.GetValue()));
				InstallDone->Trigger();
			});
			InstallDone->Wait();
		}
#endif
	}

	return FIoStatus::Ok;
}

FIoStatus FOnDemandIoStore::InitializeStreamingBackend(const FOnDemandEndpointConfig& EndpointConfig)
{
	TUniquePtr<IIasCache> StreamingCache;
	FIasCacheConfig CacheConfig = Config::GetStreamingCacheConfig(FCommandLine::Get());

	if (CacheConfig.DiskQuota > 0)
	{
		if (FPaths::HasProjectPersistentDownloadDir())
		{
			FString CacheDir = FPaths::ProjectPersistentDownloadDir();
			StreamingCache = MakeIasCache(*CacheDir, CacheConfig, DiskCacheGovernor);

			UE_CLOG(!StreamingCache.IsValid(), LogIas, Warning, TEXT("File cache disabled - streaming only (init-fail)"));
		}
		else
		{
			UE_LOG(LogIas, Warning, TEXT("File cache disabled - streaming only (project has no persistent download dir enabled for this platform)"));
		}
	}
	else
	{
		UE_LOG(LogIas, Log, TEXT("File cache disabled - streaming only (zero-quota)"));
	}

	check(HttpClient.IsValid());
	StreamingBackend = MakeOnDemandIoDispatcherBackend(EndpointConfig, *this, *HttpClient, MoveTemp(StreamingCache));
	if (StreamingBackend.IsValid() == false)
	{
		return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("Failed to create streaming backend"));
	}

	StreamingBackend->SetBulkOptionalEnabled(EnumHasAnyFlags(StreamingOptions, EOnDemandStreamingOptions::OptionalBulkDataDisabled) == false);

	int32 BackendPriority = -10;
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("Ias")))
	{
		UE_LOG(LogIas, Log, TEXT("Setting streaming backend priority higher than file system backend"));
		BackendPriority = 10;
	}
#endif
	FIoDispatcher::Get().Mount(StreamingBackend.ToSharedRef(), BackendPriority);

	return FIoStatus::Ok;
}

FOnDemandRegisterHostGroupResult FOnDemandIoStore::RegisterHostGroup(FOnDemandRegisterHostGroupArgs&& Args)
{
	using namespace UE::UnifiedError::Core;

	auto SanitizeHostNames = [bUseSecureHttp = Args.bUseSecureHttp](TArrayView<FString> HostNames) -> TArray<FAnsiString>
	{
		TArray<FAnsiString> OutHostNames;
		for (FString& HostName : HostNames)
		{
			if (HostName.StartsWith(TEXT("http")) == false)
			{
				HostName = TEXT("https://") + HostName;
			}
			if (bUseSecureHttp == false)
			{
				HostName.ReplaceInline(TEXT("https"), TEXT("http"));
			}
			HostName.RemoveFromEnd(TEXT("/"));
			OutHostNames.Add(FAnsiString(StringCast<ANSICHAR>(*HostName)));
		}

		return OutHostNames;
	};

	if (Args.HostGroupName.IsNone())
	{
		return FOnDemandRegisterHostGroupResult
		{
			.Error = ArgumentError::MakeError(TEXT("HostGroupName"), TEXT("Host group name cannot be empty"))
		};
	}

	if (Args.HostNames.IsEmpty())
	{
		return FOnDemandRegisterHostGroupResult
		{
			.Error = ArgumentError::MakeError(TEXT("HostNames"), TEXT("No host name(s) specified"))
		};
	}

	TArray<FAnsiString> HostNames	= SanitizeHostNames(Args.HostNames);
	FAnsiString AnsiTestUrl			= FAnsiString(StringCast<ANSICHAR>(*Args.TestUrl));

	FIoStatus Status = FIoStatus::Invalid;
	FIASHostGroup RegisteredHostGroup;

	if (FHttpIoDispatcher::IsInitialized())
	{
		Status				= FHttpIoDispatcher::RegisterHostGroup(Args.HostGroupName, HostNames, AnsiTestUrl); 
		RegisteredHostGroup = FHostGroupManager::Get().Find(Args.HostGroupName); // The HTTP I/O dispatcher currenlty uses the host group manager
	}
	else
	{
		if (TIoStatusOr<FIASHostGroup> RegisterStatus = FHostGroupManager::Get().Register(Args.HostGroupName, AnsiTestUrl); RegisterStatus.IsOk())
		{
			RegisteredHostGroup = RegisterStatus.ConsumeValueOrDie();
			Status				= RegisteredHostGroup.Resolve(HostNames);
		}
		else
		{
			Status = RegisterStatus.Status();
		}
	}

	if (Status.IsOk() == false)
	{
		return FOnDemandRegisterHostGroupResult
		{
			.Error = ArgumentError::MakeError(TEXT("Args"), Status.ToString())
		};
	}

	return FOnDemandRegisterHostGroupResult
	{
		.HostGroup = RegisteredHostGroup.GetUnderlyingHostGroup()
	};
}

void FOnDemandIoStore::Mount(FOnDemandMountArgs&& Args, FOnDemandMountCompleted&& OnCompleted)
{
	FSharedMountRequest MountRequest = MakeShared<FMountRequest>();
	MountRequest->Args = MoveTemp(Args);
	MountRequest->OnCompleted = MoveTemp(OnCompleted);

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Enqueing mount request, MountId='%s'"), *MountRequest->Args.MountId);
	{
		UE::TUniqueLock Lock(RequestMutex);
		MountRequests.Add(MoveTemp(MountRequest));
	}

	TryEnterTickLoop();
}

FOnDemandInstallRequest FOnDemandIoStore::Install(
	FOnDemandInstallArgs&& Args,
	FOnDemandInstallCompleted&& OnCompleted,
	FOnDemandInstallProgressed&& OnProgress)
{
	LLM_SCOPE_BYNAME(TEXT("FileSystem/OnDemandIoStore"));
	
	FSharedInternalInstallRequest InstallRequest = Installer->EnqueueInstallRequest(
		MoveTemp(Args),
		MoveTemp(OnCompleted),
		MoveTemp(OnProgress));

	return FOnDemandInstallRequest(AsWeak(), InstallRequest);
}

void FOnDemandIoStore::Purge(FOnDemandPurgeArgs&& Args, FOnDemandPurgeCompleted&& OnCompleted)
{
	LLM_SCOPE_BYNAME(TEXT("FileSystem/OnDemandIoStore"));
	Installer->EnqueuePurgeRequest(MoveTemp(Args), MoveTemp(OnCompleted));
}

void FOnDemandIoStore::Defrag(FOnDemandDefragArgs&& Args, FOnDemandDefragCompleted&& OnCompleted)
{
	LLM_SCOPE_BYNAME(TEXT("FileSystem/OnDemandIoStore"));
	Installer->EnqueueDefragRequest(MoveTemp(Args), MoveTemp(OnCompleted));
}

void FOnDemandIoStore::Verify(FOnDemandVerifyCacheCompleted&& OnCompleted)
{
	LLM_SCOPE_BYNAME(TEXT("FileSystem/OnDemandIoStore"));
	Installer->EnqueueVerifyRequest(MoveTemp(OnCompleted));
}

FIoStatus FOnDemandIoStore::Unmount(FStringView MountId)
{
	LLM_SCOPE_BYNAME(TEXT("FileSystem/OnDemandIoStore"));
	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Unmounting '%s'"), *WriteToString<256>(MountId));

	bool bPendingMount = false;

	{
		UE::TUniqueLock Lock(RequestMutex);
		bPendingMount = Algo::AnyOf(MountRequests, 
			[MountId](const FSharedMountRequest& Request) { return Request->Args.MountId == MountId; });
	}

	if (bPendingMount)
	{
		return FIoStatusBuilder(EIoErrorCode::InvalidParameter) << TEXT("Mount requests pending for MountId");
	}

	{
		TUniqueLock Lock(ContainerMutex);

		bool bRemoved = false;
		Containers.SetNum(Algo::RemoveIf(Containers, [this, &MountId, &bRemoved](const FSharedOnDemandContainer& Container)
		{
			if (Container->MountId == MountId)
			{
				ensureMsgf(Container->ChunkEntryReferences.IsEmpty(), 
					TEXT("Container is still referenced when unmounting, ContainerName='%s', MountId='%s'"), 
					*Container->Name, *Container->MountId);

				UE_LOG(LogIoStoreOnDemand, Log, TEXT("Unmounting container, ContainerName='%s', MountId='%s'"),
					*Container->Name, *Container->MountId);

				bRemoved = true;

				const FString ContainerName = Container->UniqueName();
				PendingContainerHeaders.Remove(ContainerName);

				return true;
			}

			return false;
		}));

		if (bRemoved && ensureMsgf(PackageStoreBackend, TEXT("PackageStoreBackend is null, is this a prefork server?")))
		{
			PackageStoreBackend->NeedsUpdate();
		}
	}

	return EIoErrorCode::Ok;
}

TIoStatusOr<FOnDemandInstallSizeResult> FOnDemandIoStore::GetInstallSize(const FOnDemandGetInstallSizeArgs& Args) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandIoStore::GetInstallSize);

	using namespace UE::IoStore::Private;

	TSet<FSharedOnDemandContainer> ContainersForInstallation;
	TSet<FPackageId> PackageIdsToInstall;

	if (FIoStatus Status = GetContainersAndPackagesForInstall(
		Args.MountId,
		Args.TagSets,
		Args.PackageIds,
		ContainersForInstallation,
		PackageIdsToInstall); !Status.IsOk())
	{
		return Status;
	}

	const bool bIncludeSoftReferences	= EnumHasAnyFlags(Args.Options, EOnDemandGetInstallSizeOptions::IncludeSoftReferences);
	const bool bIncludeOptionalBulkData = EnumHasAnyFlags(Args.Options, EOnDemandGetInstallSizeOptions::IncludeOptionalBulkData);
	TArray<FResolvedContainerChunks> ResolvedChunks;
	TSet<FPackageId> Missing;

	ResolveChunksToInstall(
		ContainersForInstallation,
		PackageIdsToInstall,
		bIncludeSoftReferences,
		bIncludeOptionalBulkData,
		ResolvedChunks,
		Missing);
	
	FOnDemandInstallSizeResult Result;

	const bool bPin = Args.ContentHandle.IsValid();
	if (bPin == false)
	{
		Result.InstallSize = Algo::TransformAccumulate(
			ResolvedChunks,
			[](const FResolvedContainerChunks& R) { return R.TotalSize; },
			uint64(0));

		return Result;
	}

	TArray<int32>						Uncached;
	TArray<int32, TInlineAllocator<64>> Unreferenced;

	Result.DownloadSize.Emplace(0);
	for (const FResolvedContainerChunks& Resolved : ResolvedChunks)
	{
		Result.InstallSize += Resolved.TotalSize;

		Uncached.Reset();
		Unreferenced.Reset();
		{
			TUniqueLock Lock(ContainerMutex);
			FOnDemandChunkEntryReferences& Refs = Resolved.Container->FindOrAddChunkEntryReferences(*Args.ContentHandle.Handle);
			for (int32 EntryIndex : Resolved.EntryIndices)
			{
				const bool bReferenced = Refs.Indices[EntryIndex];
				if (bReferenced == false)
				{
					Unreferenced.Add(EntryIndex);
				}
			}
		}

		if (Unreferenced.IsEmpty() == false)
		{
			const bool bOk = InstallCache->TryPinChunks(
					Resolved.Container.ToSharedRef(),
					Unreferenced,
					Args.ContentHandle,
					Uncached);
			UE_CLOG(!bOk, LogIoStoreOnDemand, Log, TEXT("Failed to pin install cache chunks due to purging/defragging"))

			for (int32 EntryIndex : Uncached)
			{
				const FOnDemandChunkEntry& Entry = Resolved.Container->ChunkEntries[EntryIndex];
				Result.DownloadSize.GetValue() += Entry.GetDiskSize(); 
			}
		}
	}

	return Result;
}

FIoStatus FOnDemandIoStore::GetInstallSizesByMountId(const FOnDemandGetInstallSizeArgs& Args, TMap<FString, uint64>& OutSizesByMountId) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandIoStore::GetInstallSizesByMountId);

	using namespace UE::IoStore::Private;

	TSet<FSharedOnDemandContainer> ContainersForInstallation;
	TSet<FPackageId> PackageIdsToInstall;

	if (FIoStatus Status = GetContainersAndPackagesForInstall(
		Args.MountId,
		Args.TagSets,
		Args.PackageIds,
		ContainersForInstallation,
		PackageIdsToInstall); !Status.IsOk())
	{
		return Status;
	}

	const bool bIncludeSoftReferences	= EnumHasAnyFlags(Args.Options, EOnDemandGetInstallSizeOptions::IncludeSoftReferences);
	const bool bIncludeOptionalBulkData = EnumHasAnyFlags(Args.Options, EOnDemandGetInstallSizeOptions::IncludeOptionalBulkData);
	TArray<FResolvedContainerChunks> ResolvedChunks;
	TSet<FPackageId> Missing;

	ResolveChunksToInstall(
		ContainersForInstallation,
		PackageIdsToInstall,
		bIncludeSoftReferences,
		bIncludeOptionalBulkData,
		ResolvedChunks,
		Missing);

	for (const FResolvedContainerChunks& R : ResolvedChunks)
	{
		OutSizesByMountId.FindOrAdd(R.Container->MountId, 0) += R.TotalSize;
	}

	return EIoErrorCode::Ok;
}

FOnDemandChunkInfo FOnDemandIoStore::GetStreamingChunkInfo(const FIoChunkId& ChunkId)
{
	const EOnDemandContainerFlags ContainerFlags = EOnDemandContainerFlags::Mounted | EOnDemandContainerFlags::StreamOnDemand;

	TUniqueLock Lock(ContainerMutex);

	for (FSharedOnDemandContainer& Container : Containers)
	{
		if (!EnumHasAllFlags(Container->Flags, ContainerFlags))
		{
			continue;
		}

		if (const FOnDemandChunkEntry* Entry = Container->FindChunkEntry(ChunkId))
		{
			return FOnDemandChunkInfo(Container, *Entry);
		}
	}

	return FOnDemandChunkInfo();
}

FOnDemandChunkInfo FOnDemandIoStore::GetInstalledChunkInfo(const FIoChunkId& ChunkId, EIoErrorCode& OutErrorCode)
{
	const EOnDemandContainerFlags ContainerFlags = EOnDemandContainerFlags::Mounted | EOnDemandContainerFlags::InstallOnDemand;

	TUniqueLock Lock(ContainerMutex);

	for (FSharedOnDemandContainer& Container : Containers)
	{
		if (!EnumHasAllFlags(Container->Flags, ContainerFlags))
		{
			continue;
		}

		int32 EntryIndex = INDEX_NONE;
		if (const FOnDemandChunkEntry* Entry = Container->FindChunkEntry(ChunkId, &EntryIndex))
		{
			if (Container->IsReferenced(EntryIndex))
			{
				OutErrorCode = EIoErrorCode::Ok;
				return FOnDemandChunkInfo(Container, *Entry);
			}

			OutErrorCode = EIoErrorCode::NotInstalled;
			return FOnDemandChunkInfo();
		}
	}

	OutErrorCode = EIoErrorCode::UnknownChunkID;
	return FOnDemandChunkInfo();
}

#if !UE_BUILD_SHIPPING
TArray<FIoChunkId> FOnDemandIoStore::DebugFindStreamingChunkIds(int32 NumToFind)
{
	TArray<FIoChunkId> IdsContainer;
	IdsContainer.Reserve(NumToFind);

	TUniqueLock Lock(ContainerMutex);

	for (FSharedOnDemandContainer& Container : Containers)
	{
		if (EnumHasAllFlags(Container->Flags, EOnDemandContainerFlags::Mounted | EOnDemandContainerFlags::StreamOnDemand))
		{
			for(const FIoChunkId& Id : Container->ChunkIds)
			{
				IdsContainer.Add(Id);

				if (IdsContainer.Num() == NumToFind)
				{
					return IdsContainer;
				}
			}
		}
	}

	return IdsContainer;
}
#endif

FIoStatus FOnDemandIoStore::InitializeInstallCache()
{
	if (FForkProcessHelper::IsForkRequested() && !FForkProcessHelper::IsForkedChildProcess())
	{
		return FIoStatusBuilder(EIoErrorCode::PendingFork) << TEXT("Install cache waiting for fork");
	}

	TIoStatusOr<FOnDemandInstallCacheConfig> CacheConfig = Config::TryParseInstallCacheConfig(FCommandLine::Get());
	if (CacheConfig.IsOk() == false)
	{
		return CacheConfig.Status();
	}

	FOnDemandInstallCacheConfig OnDemandInstallCacheConfig = CacheConfig.ConsumeValueOrDie();

	// Check if we use ICS for IAD and update the RootDirectory accordingly
	int32 SelectedCacheIndex = 0;
	{
		// OnDemandInstallCacheConfig.DiskQuota is the quota to store the data, but we also need some room for metadata, see GetJournalFilename/GetSnapshotFilename
		uint64 MetaDataSize = 2 * OnDemandInstallCacheConfig.JournalMaxSize + 2 * 32;
		uint64 OnDemandInstallRequestedSize = OnDemandInstallCacheConfig.DiskQuota + MetaDataSize;

		if (OnDemandInstallRequestedSize > 0)
		{
			FModuleManager::Get().LoadModule(TEXT("IndexedCacheStorage"));

			const FString IADStorageName(TEXT("IAD"));
			SelectedCacheIndex = Experimental::FIndexedCacheStorageManager::Get().GetStorageIndex(IADStorageName);

			if (SelectedCacheIndex > 0)
			{
				uint64 EarlyStartupSize = Experimental::FIndexedCacheStorageManager::Get().GetCacheEarlyStartupSize(SelectedCacheIndex);
				if (EarlyStartupSize > 0)
				{
					OnDemandInstallRequestedSize = EarlyStartupSize;

					uint64 MinimumDiskQuota = 4 << 20;
					check(EarlyStartupSize > MetaDataSize + MinimumDiskQuota);

					// Update DiskQuota so that EarlyStartupSize can contain the whole request;
					OnDemandInstallCacheConfig.DiskQuota = EarlyStartupSize - MetaDataSize;
				}

				// When IAD is assigned to an indexed cache, we *have* to get enough disk space.
				while (true)
				{
					if (!Experimental::FIndexedCacheStorageManager::Get().CreateCacheStorage(OnDemandInstallRequestedSize, SelectedCacheIndex))
					{
						continue;
					}

					FString MountPath = Experimental::FIndexedCacheStorageManager::Get().MountCacheStorage(SelectedCacheIndex);
					if (MountPath.IsEmpty())
					{
						UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to mount indexed cache storage %d (%s) even though it was created"), SelectedCacheIndex, *IADStorageName);
						Experimental::FIndexedCacheStorageManager::Get().DestroyCacheStorage(SelectedCacheIndex);
						continue;
					}

					FString RelativeDir(MountPath / OnDemandInstallCacheConfig.RootDirectory.Replace(*FPaths::ProjectPersistentDownloadDir(), TEXT("")));
					OnDemandInstallCacheConfig.RootDirectory = RelativeDir;
					break;
				}
			}
		}
	}

	InstallCache = MakeOnDemandInstallCache(*this, OnDemandInstallCacheConfig, DiskCacheGovernor);
	if (InstallCache.IsValid())
	{
		int32 BackendPriority = -5; // Lower than file (zero) but higher than streaming backend (-10)
#if !UE_BUILD_SHIPPING
		if (FParse::Param(FCommandLine::Get(), TEXT("Iad")))
		{
			// Bump the priority to be higher then the file system backend
			BackendPriority = 5;
		}
#endif
		FIoDispatcher::Get().Mount(InstallCache.ToSharedRef(), BackendPriority);
		PackageStoreBackend = MakeOnDemandPackageStoreBackend(AsShared().ToWeakPtr());
		FPackageStore::Get().Mount(PackageStoreBackend.ToSharedRef());
	}
	else
	{
		UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to initialize install cache"));

		if (SelectedCacheIndex > 0)
		{
			Experimental::FIndexedCacheStorageManager::Get().DestroyCacheStorage(SelectedCacheIndex);
		}

		return FIoStatusBuilder(EIoErrorCode::InvalidParameter) << TEXT("Failed to initialize install cache");
	}

#if !(UE_BUILD_SHIPPING|UE_BUILD_TEST)
	FString ParamValue;
	if (FParse::Value(FCommandLine::Get(), TEXT("-Iad.Fill="), ParamValue))
	{
		ParamValue.TrimStartAndEndInline();
		int64 FillSize = -1;
		LexFromString(FillSize, ParamValue);

		if (FillSize > 0)
		{
			if (ParamValue.EndsWith(TEXT("GB")))
			{
				FillSize = FillSize << 30;
			}
			if (ParamValue.EndsWith(TEXT("MB")))
			{
				FillSize = FillSize << 20;
			}

			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Filling install cache with %.2lf MiB of dummy data"), double(FillSize) / 1024.0 / 1024.0);

			FResult		Result = MakeValue();
			uint64		Seed = 1;
			while (FillSize >= 0 && Result.HasError() == false)
			{
				const uint64		ChunkSize = 256 << 10;
				FIoBuffer			Chunk(ChunkSize);
				TArrayView<uint64>	Values(reinterpret_cast<uint64*>(Chunk.GetData()), ChunkSize / sizeof(uint64));

				for (uint64& Value : Values)
				{
					Value = Seed;
				}

				const FIoHash ChunkHash = FIoHash::HashBuffer(Chunk.GetView());
				Result = InstallCache->PutChunk(MoveTemp(Chunk), ChunkHash);
				Seed++;
				FillSize -= ChunkSize;
			}

			if (Result.HasError() == false)
			{
				Result = InstallCache->Flush();
			}

			UE_CLOG(Result.HasError(), LogIoStoreOnDemand, Warning, TEXT("Failed to fill install cache with dummy data, reason '%s'"), *LexToString(Result.GetError()));
		}
	}
#endif

	return EIoErrorCode::Ok;
}

void FOnDemandIoStore::TryEnterTickLoop()
{
	bool bEnterTickLoop = false;
	{
		UE::TUniqueLock Lock(RequestMutex);
		bTickRequested = true;
		if (bTicking == false)
		{
			bTicking = bEnterTickLoop = true;
		}
	}

	if (bEnterTickLoop == false)
	{
		UE_LOG(LogIoStoreOnDemand, Verbose, TEXT("I/O store already ticking"));
		return;
	}

	if (FPlatformProcess::SupportsMultithreading() && GIOThreadPool != nullptr)
	{
		TickFuture = AsyncPool(*GIOThreadPool, [this] { TickLoop(); }, nullptr, EQueuedWorkPriority::Low);
	}
	else
	{
		TickLoop();
	}
}

void FOnDemandIoStore::TickLoop()
{
	LLM_SCOPE_BYNAME(TEXT("FileSystem/OnDemandIoStore"));
	ON_SCOPE_EXIT { UE_LOG(LogIoStoreOnDemand, Verbose, TEXT("Exiting I/O store tick loop")); };

	UE_LOG(LogIoStoreOnDemand, Verbose, TEXT("Entering I/O store tick loop"));
	for (;;)
	{
		const bool bTicked = Tick();
		if (bTicked == false)
		{
			UE::TUniqueLock Lock(RequestMutex);
			if (bTickRequested == false)
			{
				bTicking = false;
				break;
			}
			bTickRequested = false;
		}
	}
}

bool FOnDemandIoStore::Tick()
{
	TArray<FSharedMountRequest> LocalMountRequests;
	{
		UE::TUniqueLock Lock(RequestMutex);
		LocalMountRequests = MountRequests;
	}

	bool bTicked = LocalMountRequests.IsEmpty() == false;

	// Tick mount request(s)
	for (FSharedMountRequest& Request : LocalMountRequests)
	{
		FIoStatus MountStatus = TickMountRequest(*Request);

		{
			UE::TUniqueLock Lock(RequestMutex);
			MountRequests.Remove(Request);
		}

		CompleteMountRequest(*Request, 
			FOnDemandMountResult
			{
				.MountId = MoveTemp(Request->Args.MountId),
				.Status = MoveTemp(MountStatus),
				.DurationInSeconds = Request->DurationInSeconds
			});
	}

	return bTicked;
}

FIoStatus FOnDemandIoStore::TickMountRequest(FMountRequest& MountRequest)
{
	UE_LOG(LogIoStoreOnDemand, Verbose, TEXT("Ticking mount request, MountId='%s'"), *MountRequest.Args.MountId);

	bool bWasLoaded = false;
	if (FIoStatus CertStatus = LoadDefaultHttpCertificates(bWasLoaded); CertStatus.IsOk() == false) 
	{
		if (bWasLoaded)
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to load certificates, reason '%s'"), *CertStatus.ToString());
		}
	}

	const double StartTime = FPlatformTime::Seconds();
	ON_SCOPE_EXIT
	{
		MountRequest.DurationInSeconds = FPlatformTime::Seconds() - StartTime;
	};

	FOnDemandMountArgs& Args = MountRequest.Args;

	if (Args.MountId.IsEmpty())
	{
		return FIoStatusBuilder(EIoErrorCode::InvalidParameter) << TEXT("Invalid mount ID");
	}

	EIoErrorCode MountResult = EIoErrorCode::Ok;
	TArray<FString> ExistingContainers;

	// Find containers matching the mount ID
	{
		UE::TUniqueLock Lock(ContainerMutex);
		for (const FSharedOnDemandContainer& Container : Containers)
		{
			if (Container->MountId != Args.MountId)
			{
				continue;
			}
			
			ExistingContainers.Add(Container->Name);
			
			if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::PendingEncryptionKey))
			{
				MountResult = EIoErrorCode::PendingEncryptionKey;
			}

			if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::PendingHostGroup))
			{
				MountResult = EIoErrorCode::PendingHostGroup;
			}
		}
	}

	// Containers haven't been created yet, do it now
	TArray<FSharedOnDemandContainer> RequestedContainers;
	const FStringView TocPath = FPathViews::GetExtension(Args.TocRelativeUrl).IsEmpty() ? Args.TocRelativeUrl : FPathViews::GetPath(Args.TocRelativeUrl);

	if (Args.Toc)
	{
		CreateContainersFromToc(Args.MountId, TocPath, *Args.Toc, ExistingContainers, RequestedContainers);
	}
	else if (Args.FilePath.IsEmpty() == false)
	{
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Loading TOC from file '%s'"), *Args.FilePath);

		// TODO: Enable validation when the sentinal is included in all serialization paths
		const bool bValidate = false;
		TIoStatusOr<FOnDemandToc> TocStatus = FOnDemandToc::LoadFromFile(Args.FilePath, bValidate);
		if (!TocStatus.IsOk())
		{
			return TocStatus.Status();
		}

		Args.Toc = MakeUnique<FOnDemandToc>(TocStatus.ConsumeValueOrDie());

		CreateContainersFromToc(Args.MountId, TocPath, *Args.Toc, ExistingContainers, RequestedContainers);
	}
	else if (Args.HostGroup.IsEmpty() == false && Args.TocRelativeUrl.IsEmpty() == false)
	{
		TStringBuilder<512> Url;
		Url.Append(Args.HostGroup.PrimaryHost());
		if (Args.TocRelativeUrl[0] != TEXT('/'))
		{
			Url.AppendChar(TEXT('/'));
		}
		Url.Append(Args.TocRelativeUrl);

		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Loading TOC from URL '%s'"), Url.ToString());

		auto AnsiUrl = StringCast<ANSICHAR>(Url.GetData(), Url.Len());
		TIoStatusOr<FMultiEndpointHttpClientResponse> Response = FMultiEndpointHttpClient::Get(AnsiUrl, FMultiEndpointHttpClientConfig
		{ 
			.MaxRetryCount	= 2,
			.Redirects		= EHttpRedirects::Follow
		});

		if (!Response.IsOk())
		{
			FIoStatus Status = FIoStatusBuilder(EIoErrorCode::ReadError) << TEXT("Failed to fetch TOC from URL");
			return Status;
		}

		FMemoryReaderView Ar(Response.ValueOrDie().Body.GetView());
		Args.Toc = MakeUnique<FOnDemandToc>();
		Ar << *Args.Toc;

		if (Ar.IsError() || Ar.IsCriticalError())
		{
			Args.Toc.Reset();
			FIoStatus Status = FIoStatusBuilder(EIoErrorCode::ReadError) << TEXT("Failed to serialize TOC from HTTP response");
			return Status;
		}

		CreateContainersFromToc(Args.MountId, TocPath, *Args.Toc, ExistingContainers, RequestedContainers);
	}
	
	TAnsiStringBuilder<512> ChunkUrlBuilder;
	TMap<FString, FIoBuffer> RequestedContainerHeaders;

	for (const FSharedOnDemandContainer& Container : RequestedContainers)
	{
		if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::StreamOnDemand | EOnDemandContainerFlags::InstallOnDemand) == false)
		{
			return FIoStatusBuilder(EIoErrorCode::InvalidParameter) << TEXT("Does not have either a 'StreamOnDemand' or 'InstallOnDemand' EOnDemandContainerFlags flag");
		}

		if (EnumHasAnyFlags(Args.Options, EOnDemandMountOptions::WithSoftReferences))
		{
			EnumAddFlags(Container->Flags, EOnDemandContainerFlags::WithSoftReferences);
		}

		if (const FIoStatus Status = SetupHostGroup(Container, Args); !Status.IsOk())
		{
			if (Status.GetErrorCode() == EIoErrorCode::PendingHostGroup)
			{
				EnumAddFlags(Container->Flags, EOnDemandContainerFlags::PendingHostGroup);
				MountResult = EIoErrorCode::PendingHostGroup;
				UE_LOG(LogIoStoreOnDemand, Log, TEXT("Deferring container '%s' until host group '%s' becomes available"),
					*Container->Name, *Container->HostGroupName.ToString());
			}
			else
			{
				return Status;
			}
		}

		if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::Encrypted) &&
			Container->EncryptionKey.IsValid() == false)
		{
			FGuid KeyGuid;
			ensure(FGuid::Parse(Container->EncryptionKeyGuid, KeyGuid));
			if (FEncryptionKeyManager::Get().TryGetKey(KeyGuid, Container->EncryptionKey) == false)
			{
				EnumAddFlags(Container->Flags, EOnDemandContainerFlags::PendingEncryptionKey);
				MountResult = EIoErrorCode::PendingEncryptionKey;
				UE_LOG(LogIoStoreOnDemand, Log, TEXT("Deferring container '%s' until encryption key '%s' becomes available"),
					*Container->Name, *Container->EncryptionKeyGuid);
			}
		}

		// Try fetch and deserialize the container header if the container is used for installing content.
		if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::InstallOnDemand))
		{
			const FIoChunkId ChunkId			= CreateContainerHeaderChunkId(Container->ContainerId);
			const FOnDemandChunkInfo ChunkInfo	= FOnDemandChunkInfo::Find(Container, ChunkId);

			if (ChunkInfo.IsValid())
			{
				const FOnDemandTocContainerEntry* ContainerEntry = Args.Toc->Containers.FindByPredicate(
						[&Container](const FOnDemandTocContainerEntry& E) { return E.ContainerId == Container->ContainerId; });
				UE_CLOG(ContainerEntry == nullptr, LogIoStoreOnDemand, Error, TEXT("Failed to find TOC container entry for container '%s'"), *Container->Name);

				if (ContainerEntry != nullptr)
				{
					FIoBuffer HeaderBuffer;
					if (ContainerEntry->Header.IsEmpty() == false)
					{
						HeaderBuffer = FIoBuffer(FIoBuffer::Wrap, ContainerEntry->Header.GetData(), ContainerEntry->Header.Num());
					}
					else
					{
						if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::PendingHostGroup))
						{
							continue;
						}

						if (Container->HostGroup.IsResolved() == false)
						{
							return FIoStatusBuilder(EIoErrorCode::InvalidParameter) << TEXT("No valid endpoint(s)");
						}

						UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Fetching container header, ContainerName='%s', ChunkId='%s'"),
								*Container->Name, *LexToString(ChunkId));

						const FAnsiStringView PrimaryHost = Container->HostGroup.GetUnderlyingHostGroup().PrimaryHost();
						const uint32 RetryCount = 2;
						TIoStatusOr<FMultiEndpointHttpClientResponse> Response = FMultiEndpointHttpClient::Get(
								Private::GetChunkUrl(PrimaryHost, *Container, ChunkInfo.ChunkEntry(), ChunkUrlBuilder).ToView(),
								FMultiEndpointHttpClientConfig{.MaxRetryCount = RetryCount});

						if (Response.IsOk() == false)
						{
							return Response.Status();
						}
						HeaderBuffer = Response.ConsumeValueOrDie().Body;
					}

					if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::PendingEncryptionKey))
					{
						HeaderBuffer.EnsureOwned();
						RequestedContainerHeaders.Add(Container->UniqueName(), HeaderBuffer);
						continue;
					}

					TIoStatusOr<FSharedContainerHeader> Header = Private::DeserializeContainerHeader(
							ChunkInfo,
							HeaderBuffer.GetView(),
							EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::WithSoftReferences));

					if (Header.IsOk() == false)
					{
						return Header.Status();
					}
					Container->Header = Header.ConsumeValueOrDie();
				}
			}
		}

		if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::PendingEncryptionKey | EOnDemandContainerFlags::PendingHostGroup))
		{
			continue;
		}

		TStringBuilder<128> Sb;
		Sb << Container->Flags;
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Mounting container '%s', Entries=%d, Flags='%s', HostGroup='%s'"),
			*Container->Name, Container->ChunkEntries.Num(), Sb.ToString(), *Container->HostGroupName.ToString());
		EnumAddFlags(Container->Flags, EOnDemandContainerFlags::Mounted);
		Container->EncryptionKeyGuid.Empty();
	}

	{
		UE::TUniqueLock Lock(ContainerMutex);
		Containers.Append(MoveTemp(RequestedContainers));
		PendingContainerHeaders.Append(MoveTemp(RequestedContainerHeaders));
	}

	if (ensureMsgf(PackageStoreBackend, TEXT("PackageStoreBackend is null, is this a prefork server?")))
	{
		PackageStoreBackend->NeedsUpdate();
	}

	return MountResult; 
}

void FOnDemandIoStore::CompleteMountRequest(FMountRequest& Request, FOnDemandMountResult&& MountResult)
{
	if (!Request.OnCompleted)
	{
		return;
	}

	if (EnumHasAnyFlags(Request.Args.Options, EOnDemandMountOptions::CallbackOnGameThread))
	{
		ExecuteOnGameThread(
			UE_SOURCE_LOCATION,
			[OnCompleted = MoveTemp(Request.OnCompleted), MountResult = MoveTemp(MountResult)]() mutable
			{
				OnCompleted(MoveTemp(MountResult));
			});
	}
	else
	{
		FOnDemandMountCompleted OnCompleted = MoveTemp(Request.OnCompleted);
		OnCompleted(MoveTemp(MountResult));
	}
}

void FOnDemandIoStore::OnEncryptionKeyAdded(const FGuid& Id, const FAES::FAESKey& Key)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandIoStore::OnEncryptionKeyAdded);

	TUniqueLock Lock(ContainerMutex);

	bool bAddedContainerHeaders = false;

	for (FSharedOnDemandContainer& Container : Containers)
	{
		if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::PendingEncryptionKey) == false)
		{
			continue;
		}

		FGuid KeyGuid;
		ensure(FGuid::Parse(Container->EncryptionKeyGuid, KeyGuid));

		if (FEncryptionKeyManager::Get().TryGetKey(KeyGuid, Container->EncryptionKey) == false)
		{
			continue;
		}

		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Mounting container (found encryption key) '%s', Entries=%d, Flags='%s'"),
				*Container->Name, Container->ChunkEntries.Num(), *LexToString(Container->Flags));

		EnumRemoveFlags(Container->Flags, EOnDemandContainerFlags::PendingEncryptionKey);
		EnumAddFlags(Container->Flags, EOnDemandContainerFlags::Mounted);
		
		Container->EncryptionKeyGuid.Empty();

		const FString ContainerName		= Container->UniqueName();
		const uint32 ContainerNameHash	= GetTypeHash(ContainerName);

		if (FIoBuffer* EncodedHeader = PendingContainerHeaders.FindByHash(ContainerNameHash, ContainerName))
		{
			const FIoChunkId ChunkId			= CreateContainerHeaderChunkId(Container->ContainerId);
			const FOnDemandChunkInfo ChunkInfo	= FOnDemandChunkInfo::Find(Container, ChunkId);

			if (ensure(ChunkInfo.IsValid()))
			{
				TIoStatusOr<FSharedContainerHeader> Header = Private::DeserializeContainerHeader(
					ChunkInfo,
					EncodedHeader->GetView(),
					EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::WithSoftReferences));

				if (ensure(Header.IsOk()))
				{
					Container->Header = Header.ConsumeValueOrDie();
					bAddedContainerHeaders = true;
				}
				else
				{
					EnumRemoveFlags(Container->Flags, EOnDemandContainerFlags::Mounted);
					UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to deserialize header when mounting container '%s', Entries=%d, Flags='%s'"),
						*Container->Name, Container->ChunkEntries.Num(), *LexToString(Container->Flags));
				}
			}

			PendingContainerHeaders.RemoveByHash(ContainerNameHash, ContainerName);
		}
	}

	if (bAddedContainerHeaders && ensureMsgf(PackageStoreBackend, TEXT("PackageStoreBackend is null, is this a prefork server?")))
	{
		PackageStoreBackend->NeedsUpdate();
	}
}

void FOnDemandIoStore::OnHostGroupRegistered(const FName& HostGroup)
{
	TUniqueLock Lock(ContainerMutex);
	for (FSharedOnDemandContainer& Container : Containers)
	{
		if (!EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::PendingHostGroup) || Container->HostGroupName != HostGroup)
		{
			continue;
		}

		EnumRemoveFlags(Container->Flags, EOnDemandContainerFlags::PendingHostGroup);
		EnumAddFlags(Container->Flags, EOnDemandContainerFlags::Mounted);
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Mounting container '%s', Entries=%d, Flags='%s', HostGroup='%s'"),
			*Container->Name, Container->ChunkEntries.Num(), *LexToString(Container->Flags), *HostGroup.ToString());
	}
}

void FOnDemandIoStore::CreateContainersFromToc(
	FStringView MountId,
	FStringView TocPath,
	FOnDemandToc& Toc,
	const TArray<FString>& ExistingContainers,
	TArray<FSharedOnDemandContainer>& Out)
{
	static FName AssetClassName(TEXT("OnDemandIoStore"));
	LLM_SCOPE_BYNAME(TEXT("FileSystem/OnDemandIoStore"));
	LLM_TAGSET_SCOPE(FName(MountId), ELLMTagSet::Assets);
	LLM_TAGSET_SCOPE(AssetClassName, ELLMTagSet::AssetClasses);
	UE_TRACE_METADATA_SCOPE_ASSET_FNAME(FName(MountId), AssetClassName, FName(TocPath));
	const FOnDemandTocHeader& Header = Toc.Header;
	const FName CompressionFormat(Header.CompressionFormat);

	TStringBuilder<128> Sb;
	FStringView ChunksDirectory;
	{
		if (TocPath.IsEmpty() == false)
		{
			//Algo::Transform(TocPath, AppendChars(Sb), FChar::ToLower);
			FPathViews::Append(Sb, TocPath);
		}
		else
		{
			// Algo::Transform(Toc.Header.ChunksDirectory, AppendChars(Sb), FChar::ToLower);
			FPathViews::Append(Sb, Toc.Header.ChunksDirectory);
		}
		FPathViews::Append(Sb, TEXT("chunks"));

		ChunksDirectory = Sb;
		if (ChunksDirectory.StartsWith('/'))
		{
			ChunksDirectory.RemovePrefix(1);
		}
		if (ChunksDirectory.EndsWith('/'))
		{
			ChunksDirectory.RemoveSuffix(1);
		}
	}

	const EOnDemandTocFlags TocFlags = static_cast<EOnDemandTocFlags>(Toc.Header.Flags);
	for (FOnDemandTocContainerEntry& ContainerEntry : Toc.Containers)
	{
		if (ExistingContainers.Contains(ContainerEntry.ContainerName))
		{
			continue;
		}

		FSharedOnDemandContainer Container = MakeShared<FOnDemandContainer>();
		Container->Name					= MoveTemp(ContainerEntry.ContainerName);
		Container->MountId				= MountId;
		Container->ChunksDirectory		= StringCast<ANSICHAR>(ChunksDirectory.GetData(), ChunksDirectory.Len());
		Container->EncryptionKeyGuid	= MoveTemp(ContainerEntry.EncryptionKeyGuid);
		Container->BlockSize			= Header.BlockSize;
		Container->BlockSizes			= MoveTemp(ContainerEntry.BlockSizes);
		Container->BlockHashes			= MoveTemp(ContainerEntry.BlockHashes);
		Container->ContainerId			= ContainerEntry.ContainerId;
		Container->HostGroupName		= Toc.Header.HostGroupName.IsEmpty() ? FOnDemandHostGroup::DefaultName : FName(Toc.Header.HostGroupName);
		Container->RelativeUrl			= UE::FIoRelativeUrl::From(Container->ChunksDirectory);

		Container->CompressionFormats.Reserve(1);
		Container->CompressionFormats.Add(CompressionFormat);

		const EIoContainerFlags ContainerFlags = static_cast<EIoContainerFlags>(ContainerEntry.ContainerFlags);
		if (EnumHasAnyFlags(ContainerFlags, EIoContainerFlags::Encrypted))
		{
			EnumAddFlags(Container->Flags, EOnDemandContainerFlags::Encrypted);
		}
		if (EnumHasAnyFlags(TocFlags, EOnDemandTocFlags::InstallOnDemand))
		{
			EnumAddFlags(Container->Flags, EOnDemandContainerFlags::InstallOnDemand);
		}
		else if (EnumHasAnyFlags(TocFlags, EOnDemandTocFlags::StreamOnDemand))
		{
			EnumAddFlags(Container->Flags, EOnDemandContainerFlags::StreamOnDemand);
		}

		//TODO: Sort before uploading
		ContainerEntry.Entries.Sort([](const FOnDemandTocEntry& Lhs, const FOnDemandTocEntry& Rhs)
		{
			return Lhs.ChunkId < Rhs.ChunkId;
		});

		const int32	EntryCount		= ContainerEntry.Entries.Num();
		const uint64 TotalEntrySize = (sizeof(FIoChunkId) + sizeof(FOnDemandTocEntry)) * EntryCount;
		Container->ChunkEntryData	= MakeUnique<uint8[]>(TotalEntrySize);
		Container->ChunkIds			= MakeArrayView<FIoChunkId>(reinterpret_cast<FIoChunkId*>(Container->ChunkEntryData.Get()), EntryCount);
		Container->ChunkEntries		= MakeArrayView<FOnDemandChunkEntry>(
			reinterpret_cast<FOnDemandChunkEntry*>(Container->ChunkIds.GetData() + EntryCount), EntryCount);

		for (int32 EntryIndex = 0; EntryIndex < EntryCount; EntryIndex++)
		{
			const FOnDemandTocEntry& TocEntry = ContainerEntry.Entries[EntryIndex];

			Container->ChunkIds[EntryIndex]		= TocEntry.ChunkId;
			Container->ChunkEntries[EntryIndex]	= FOnDemandChunkEntry
			{
				.Hash					= TocEntry.Hash,
				.RawSize				= uint32(TocEntry.RawSize),
				.EncodedSize			= uint32(TocEntry.EncodedSize),
				.BlockOffset			= TocEntry.BlockOffset,
				.BlockCount				= TocEntry.BlockCount,
				.CompressionFormatIndex	= 0
			};
		}

		const uint32 ContainerIndex = Out.Num();
		for (FOnDemandTocTagSet& TagSet : Toc.TagSets)
		{
			for (FOnDemandTocTagSetPackageList& ContainerPackageIndices : TagSet.Packages)
			{
				if (ContainerPackageIndices.ContainerIndex == ContainerIndex)
				{
					FOnDemandTagSet& NewTagSet = Container->TagSets.AddDefaulted_GetRef();
					NewTagSet.Tag = TagSet.Tag;
					NewTagSet.PackageIndicies = MoveTemp(ContainerPackageIndices.PackageIndicies);
					break;
				}
			}
		}

		// Do not count output container memory as being allocated for utocs
		LLM_TAGSET_SCOPE_CLEAR(ELLMTagSet::Assets);
		LLM_TAGSET_SCOPE_CLEAR(ELLMTagSet::AssetClasses);
		UE_TRACE_METADATA_CLEAR_SCOPE();
		Out.Add(MoveTemp(Container));
	}
}

FIoStatus FOnDemandIoStore::SetupHostGroup(const FSharedOnDemandContainer& Container, const FOnDemandMountArgs& MountArgs)
{
	auto SanitizeHostNames = [MountOptions = MountArgs.Options](TConstArrayView<FAnsiString> HostNames) -> TArray<FAnsiString>
	{
		TArray<FAnsiString> OutHostNames;
		for (const FAnsiString& HostName : HostNames)
		{
			FAnsiString Tmp = HostName;
			Tmp.ToLowerInline();
			if (EnumHasAnyFlags(MountOptions, EOnDemandMountOptions::ForceNonSecureHttp))
			{
				Tmp.ReplaceInline("https", "http");
			}
			OutHostNames.Add(Tmp);
		}

		return OutHostNames;
	};

	if (FHttpIoDispatcher::IsInitialized())
	{
		UE_CLOG(Container->HostGroupName != MountArgs.HostGroupName, LogIoStoreOnDemand, Log,
			TEXT("Overriding container host group '%s' with '%s' from mount option"),
			*Container->HostGroupName.ToString(), *MountArgs.HostGroupName.ToString());

		Container->HostGroupName = MountArgs.HostGroupName;
		if (FHttpIoDispatcher::IsHostGroupRegistered(Container->HostGroupName))
		{
			return FIoStatus::Ok;
		}

		if (MountArgs.HostGroup.IsEmpty() == false)
		{
			TArray<FAnsiString> HostNames = SanitizeHostNames(MountArgs.HostGroup.Hosts());
			return FHttpIoDispatcher::RegisterHostGroup(Container->HostGroupName, HostNames, Container->GetTestUrl());
		}

		return FIoStatus(EIoErrorCode::PendingHostGroup); 
	}

	// This path will be removed once the HTTP I/O dispatcher is on by default
	// StreamOnDemand (IAS) uses a different default hostgroup in this code path so if the MountArgs are requesting 'Default' we need
	// to route that to the IAS default instead.
	if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::StreamOnDemand) && MountArgs.HostGroupName == TEXT("Default"))
	{
		//TODO: Remove this special case for streaming containers
		Container->HostGroupName	= FName("DefaultOnDemand");
		Container->HostGroup		= FHostGroupManager::Get().Find(Container->HostGroupName);

		return FIoStatus::Ok;
	}

	UE_CLOG(Container->HostGroupName != MountArgs.HostGroupName, LogIoStoreOnDemand, Log,
		TEXT("Overriding container host group '%s' with '%s' from mount option"),
		*Container->HostGroupName.ToString(), *MountArgs.HostGroupName.ToString());

	Container->HostGroupName	= MountArgs.HostGroupName;
	Container->HostGroup		= FHostGroupManager::Get().Find(Container->HostGroupName);

	if (Container->HostGroup.IsResolved())
	{
		return FIoStatus::Ok;
	}

	if (MountArgs.HostGroup.IsEmpty())
	{
		return FIoStatus(EIoErrorCode::PendingHostGroup);
	}
	
	TIoStatusOr<FIASHostGroup> NewHostGroup = FHostGroupManager::Get().Register(Container->HostGroupName, Container->GetTestUrl());
	if (NewHostGroup.IsOk() == false)
	{
		return NewHostGroup.Status();
	}

	Container->HostGroup = NewHostGroup.ConsumeValueOrDie();

	TArray<FAnsiString> HostNames = SanitizeHostNames(MountArgs.HostGroup.Hosts());
#if !UE_BUILD_SHIPPING
	if (Container->HostGroupName == FOnDemandHostGroup::DefaultName)
	{
		FString HostValue;
		if (FParse::Value(FCommandLine::Get(), TEXT("Iax.DefaultHostGroup="), HostValue))
		{
			HostNames.Reset();
			TArray<FString> HostArray;
			HostValue.ParseIntoArray(HostArray, TEXT(","), true);
			for (FString& Host : HostArray)
			{
				Host.TrimStartAndEndInline();
				HostNames.Add(FAnsiString(*Host));
			}
		}
	}
#endif
	if (FIoStatus Status = Container->HostGroup.Resolve(HostNames); Status.IsOk() == false)
	{
		return Status;
	}

	{
		const FOnDemandHostGroup& HostGroup = Container->HostGroup.GetUnderlyingHostGroup();
		TConstArrayView<FAnsiString> Hosts	= HostGroup.Hosts();
		TStringBuilder<128> Sb;

		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Created new host group '%s'"), *Container->HostGroupName.ToString());
		for (int32 HostIndex = 0; HostIndex < Hosts.Num(); ++HostIndex) 
		{
			Sb.Reset();
			Sb << Hosts[HostIndex];
			if (HostIndex == HostGroup.PrimaryHostIndex())
			{
				Sb << TEXT(" (primary)");
			}
			UE_LOG(LogIoStoreOnDemand, Log, TEXT("\t%s"), Sb.ToString()); 
		}
	}

	return FIoStatus::Ok;
}

TArray<FSharedOnDemandContainer> FOnDemandIoStore::GetContainers(EOnDemandContainerFlags ContainerFlags) const
{
	TArray<FSharedOnDemandContainer> Out;
	Out.Reserve(Containers.Num());
	{
		UE::TUniqueLock Lock(ContainerMutex);
		for (const FSharedOnDemandContainer& Container : Containers)
		{
			if (ContainerFlags == EOnDemandContainerFlags::None || Container->HasAllFlags(ContainerFlags))
			{
				Out.Add(Container);
			}
		}
	}

	return Out;
}

void FOnDemandIoStore::FlushLastAccess(FOnDemandFlushLastAccessCompleted&& OnCompleted)
{
	LLM_SCOPE_BYNAME(TEXT("FileSystem/OnDemandIoStore"));
	Installer->EnqueueFlushLastAccessRequest(MoveTemp(OnCompleted));
}

FIoStatus FOnDemandIoStore::GetContainersAndPackagesForInstall(
	FStringView MountId,
	const TArray<FString>& TagSets,
	const TArray<FPackageId>& PackageIds,
	TSet<FSharedOnDemandContainer>& OutContainersForInstallation, 
	TSet<FPackageId>& OutPackageIdsToInstall) const
{
	OutContainersForInstallation.Reset();
	{
		UE::TUniqueLock Lock(ContainerMutex);

		OutContainersForInstallation.Reserve(Containers.Num());
		for (const FSharedOnDemandContainer& Container : Containers)
		{
			if (Container->HasAnyFlags(EOnDemandContainerFlags::InstallOnDemand) == false)
			{
				continue;
			}

			if (Container->HasAnyFlags(EOnDemandContainerFlags::PendingEncryptionKey))
			{
				check(Container->HasAnyFlags(EOnDemandContainerFlags::Mounted) == false);
				if (MountId == Container->MountId)
				{
					check(EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::Mounted) == false);
					FIoStatus Status = FIoStatusBuilder(EIoErrorCode::PendingEncryptionKey)
						<< TEXT("Trying to install content from encrypted container '")
						<< Container->Name
						<< TEXT("'");
					return Status;
				}

				continue;
			}
			check(Container->HasAnyFlags(EOnDemandContainerFlags::Mounted));
			OutContainersForInstallation.Add(Container);
		}
	}

	// It's not allowed to install all content
	if (MountId.IsEmpty() && TagSets.IsEmpty() && PackageIds.IsEmpty())
	{
		OutContainersForInstallation.Reset();
		FIoStatus Status = FIoStatusBuilder(EIoErrorCode::InvalidCode)
			<< TEXT("Trying to install content from all mounted containers");
		return Status;
	}

	const bool bInstallAllPackages = TagSets.IsEmpty() && PackageIds.IsEmpty();
	if (bInstallAllPackages)
	{
		check(MountId.IsEmpty() == false);
		for (const FSharedOnDemandContainer& Container : OutContainersForInstallation)
		{
			if (Container->Header.IsValid() == false || MountId != Container->MountId)
			{
				continue;
			}
			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Installing all %d package(s) from container '%s'"),
				Container->ChunkIds.Num(), *Container->UniqueName());

			OutPackageIdsToInstall.Append(Container->Header->PackageIds);
		}

		return FIoStatus::Ok;
	}

	// Add packages from matching tags
	for (const FSharedOnDemandContainer& Container : OutContainersForInstallation)
	{
		if (Container->Header.IsValid() == false || (!MountId.IsEmpty() && MountId != Container->MountId))
		{
			continue;
		}

		for (const FString& Tag : TagSets)
		{
			const int32 NumBeforeTag = OutPackageIdsToInstall.Num();
			for (const FOnDemandTagSet& TagSet : Container->TagSets)
			{
				if (TagSet.Tag == Tag)
				{
					UE_LOG(LogIoStoreOnDemand, Log, TEXT("Installing %d package(s) with tag '%s' from container '%s'"),
						TagSet.PackageIndicies.Num(), *TagSet.Tag, *Container->UniqueName());

					OutPackageIdsToInstall.Reserve(TagSet.PackageIndicies.Num());
					for (const uint32 PackageIndex : TagSet.PackageIndicies)
					{
						const FPackageId PackageId = Container->Header->PackageIds[IntCastChecked<int32>(PackageIndex)];
						OutPackageIdsToInstall.Add(PackageId);
					}
				}
			}
			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Installing total %d package(s) with tag '%s'"), OutPackageIdsToInstall.Num() - NumBeforeTag, *Tag)
		}
	}

	// Finally add any specified package ID(s) 
	OutPackageIdsToInstall.Append(PackageIds);

	return FIoStatus::Ok;
}

void FOnDemandIoStore::ReleaseContent(FOnDemandInternalContentHandle& ContentHandle)
{
	LLM_SCOPE_BYNAME(TEXT("FileSystem/OnDemandIoStore"));
	UE_LOG(LogIoStoreOnDemand, Verbose, TEXT("Releasing content handle, %s"), *LexToString(ContentHandle));

	TArray<FIoHash> ReleasedChunks;

	{
		UE::TUniqueLock Lock(ContainerMutex);

		for (FSharedOnDemandContainer& Container : Containers)
		{
			if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::StreamOnDemand))
			{
				continue;
			}

			const int32 RemoveIndex = Container->ChunkEntryReferences.IndexOfByPredicate(
				[ContentHandleId = ContentHandle.HandleId()](const FOnDemandChunkEntryReferences& Refs)
				{
					return Refs.ContentHandleId == ContentHandleId;
				}
			);

			if (RemoveIndex == INDEX_NONE)
			{
				continue;
			}

			// Note: A referencer is only added to the first container found that has a chunk, and the
			// referencer is not added the same container more than once. So once the refrencer is removed,
			// we only need to inspect the current container for unreffed chunks.

			FOnDemandChunkEntryReferences RemovedRefs = MoveTemp(Container->ChunkEntryReferences[RemoveIndex]);
			Container->ChunkEntryReferences.RemoveAtSwap(RemoveIndex);
			check(RemovedRefs.Indices.Num() == Container->ChunkEntries.Num());

			TBitArray<> ReffedChunks = Container->GetReferencedChunkEntries();

			for (int32 i = 0; i < RemovedRefs.Indices.Num(); ++i)
			{
				if (RemovedRefs.Indices[i] && (ReffedChunks.IsEmpty() || !ReffedChunks[i]))
				{
					ReleasedChunks.Add(Container->ChunkEntries[i].Hash);
				}
			}
		}
	}

	// Package store needs to be aware of install state
	if (ensureMsgf(PackageStoreBackend, TEXT("PackageStoreBackend is null, is this a prefork server?")))
	{
		PackageStoreBackend->NeedsUpdate(EOnDemandPackageStoreUpdateMode::ReferencedPackages);
	}

	if (ensureMsgf(InstallCache, TEXT("InstallCache is null, is this a prefork server?")))
	{
		InstallCache->UpdateLastAccess(ReleasedChunks);
	}
}

void FOnDemandIoStore::GetReferencedContent(TArray<FSharedOnDemandContainer>& OutContainers, TArray<TBitArray<>>& OutChunkEntryIndices, bool bPackageStore)
{
	UE::TUniqueLock Lock(ContainerMutex);
	for (FSharedOnDemandContainer& Container : Containers)
	{
		if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::StreamOnDemand))
		{
			continue;
		}

		if (Container->HasAnyFlags(EOnDemandContainerFlags::PendingEncryptionKey))
		{
			continue;
		}

		if (bPackageStore && Container->Header.IsValid() == false)
		{
			// Package store doesn't care about containers without packages
			continue;
		}

		TBitArray<> Indices = Container->GetReferencedChunkEntries();
		if (Indices.IsEmpty() && bPackageStore)
		{
			// Package store wants to know about about containers with packages even if there are not references yet
			Indices.SetNum(Container->ChunkEntries.Num(), false);
		}

		if (Indices.IsEmpty() == false)
		{
			OutContainers.Add(Container);
			OutChunkEntryIndices.Add(MoveTemp(Indices));
		}
	}
}

TBitArray<> FOnDemandIoStore::GetReferencedContent(const FSharedOnDemandContainer& Container)
{
	UE::TUniqueLock Lock(ContainerMutex);
	return Container->GetReferencedChunkEntries();
}

void FOnDemandIoStore::GetReferencedContentByHandle(TMap<FOnDemandWeakContentHandle, TArray<FOnDemandContainerChunkEntryReferences>>& OutReferencesByHandle) const
{
	UE::TUniqueLock Lock(ContainerMutex);
	for (const FSharedOnDemandContainer& Container : Containers)
	{
		for (const FOnDemandChunkEntryReferences& Refs : Container->ChunkEntryReferences)
		{
			FOnDemandWeakContentHandle WeakHandle = FOnDemandWeakContentHandle::FromUnsafeHandle(Refs.ContentHandleId);
			TArray<FOnDemandContainerChunkEntryReferences>& Entries = OutReferencesByHandle.FindOrAdd(WeakHandle);
			Entries.Add(FOnDemandContainerChunkEntryReferences
			{
				.Container	= Container,
				.Indices	= Refs.Indices
			});
		}
	}
}

void FOnDemandIoStore::AddReference(const FSharedOnDemandContainer& Container, int32 EntryIndex, FOnDemandContentHandle ContentHandle)
{
	UE::TUniqueLock Lock(ContainerMutex);
	FOnDemandChunkEntryReferences& Refs = Container->FindOrAddChunkEntryReferences(*ContentHandle.Handle);
	Refs.Indices[EntryIndex] = true;
}

void FOnDemandIoStore::CancelInstallRequest(FSharedInternalInstallRequest InstallRequest)
{
	check(InstallRequest.IsValid());
	Installer->CancelInstallRequest(InstallRequest);
}

void FOnDemandIoStore::UpdateInstallRequestPriority(FSharedInternalInstallRequest InstallRequest, int32 NewPriority)
{
	Installer->UpdateInstallRequestPriority(InstallRequest, NewPriority);
}

FOnDemandCacheUsage FOnDemandIoStore::GetCacheUsage(const FOnDemandGetCacheUsageArgs& Args) const
{
	FOnDemandCacheUsage CacheUsage;

	if (InstallCache.IsValid())
	{
		CacheUsage.InstallCache = InstallCache->GetCacheUsage();

		if (EnumHasAnyFlags(Args.Options, EOnDemandGetCacheUsageOptions::DumpHandlesToLog | EOnDemandGetCacheUsageOptions::DumpHandlesToResults))
		{
			TMap<FOnDemandWeakContentHandle, TArray<FOnDemandContainerChunkEntryReferences>> RefsByHandle;
			GetReferencedContentByHandle(RefsByHandle);

			if (EnumHasAnyFlags(Args.Options, EOnDemandGetCacheUsageOptions::DumpHandlesToResults))
			{
				CacheUsage.InstallCache.ReferencedBytesByHandle.Reserve(RefsByHandle.Num());
			}

			for (const TPair<FOnDemandWeakContentHandle, TArray<FOnDemandContainerChunkEntryReferences>>& Kv : RefsByHandle)
			{
				const FOnDemandWeakContentHandle& WeakHandle = Kv.Key;
				const TArray<FOnDemandContainerChunkEntryReferences>& ContainerReferences = Kv.Value;

				uint64 ReferencedBytesByHandle = 0;
				for (const FOnDemandContainerChunkEntryReferences& Refs : ContainerReferences)
				{
					for (int32 EntryIndex = 0; const FOnDemandChunkEntry & ChunkEntry : Refs.Container->ChunkEntries)
					{
						if (Refs.Indices[EntryIndex])
						{
							ReferencedBytesByHandle += ChunkEntry.GetDiskSize();
						}
						EntryIndex++;
					}
				}

				if (ReferencedBytesByHandle > 0)
				{
					if (EnumHasAnyFlags(Args.Options, EOnDemandGetCacheUsageOptions::DumpHandlesToLog))
					{
						UE_LOG(LogIoStoreOnDemand, Display, TEXT("HandleId=0x%llX, DebugName='%s', ReferencedBytes=%.2lf KiB"),
							WeakHandle.HandleId, *WeakHandle.DebugName, double(ReferencedBytesByHandle) / 1024.0);
					}

					if (EnumHasAnyFlags(Args.Options, EOnDemandGetCacheUsageOptions::DumpHandlesToResults))
					{
						CacheUsage.InstallCache.ReferencedBytesByHandle.Add(
							FOnDemandInstallHandleCacheUsage
							{
								.HandleId = WeakHandle.HandleId,
								.DebugName = WeakHandle.DebugName,
								.ReferencedBytes = ReferencedBytesByHandle
							}
						);
					}
				}
			}
		}
	}

	if (StreamingBackend.IsValid())
	{
		CacheUsage.StreamingCache = StreamingBackend->GetCacheUsage();
	}

	return CacheUsage;
}

void FOnDemandIoStore::DumpMountedContainersToLog() const
{
	TStringBuilder<128> FlagsSb;

	TUniqueLock Lock(ContainerMutex);

	UE_LOG(LogIoStoreOnDemand, Display, TEXT("Containers:"));
	for (const FSharedOnDemandContainer& Container : Containers)
	{
		FlagsSb.Reset();
		bool bFirst = true;

		if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::StreamOnDemand))
		{
			FlagsSb << TEXT("StreamOnDemand");
			bFirst = false;
		}
		if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::InstallOnDemand))
		{
			if (!bFirst)
			{
				FlagsSb << TEXT(", ");
			}

			FlagsSb << TEXT("InstallOnDemand");
			bFirst = false;
		}
		if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::Mounted))
		{
			if (!bFirst)
			{
				FlagsSb << TEXT(", ");
			}

			FlagsSb << TEXT("Mounted");
			bFirst = false;
		}
		if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::PendingEncryptionKey))
		{
			if (!bFirst)
			{
				FlagsSb << TEXT(", ");
			}

			FlagsSb << TEXT("PendingEncryptionKey");
			bFirst = false;
		}
		if (EnumHasAnyFlags(Container->Flags, EOnDemandContainerFlags::PendingHostGroup))
		{
			if (!bFirst)
			{
				FlagsSb << TEXT(", ");
			}

			FlagsSb << TEXT("PendingHostGroup");
			bFirst = false;
		}

		UE_LOGFMT(LogIoStoreOnDemand, Display, "\t Container: {Name}, Flags: {Flags}", *Container->UniqueName(), FlagsSb);
	}
}

bool FOnDemandIoStore::IsOnDemandStreamingEnabled() const
{
	return StreamingBackend != nullptr;
}

void FOnDemandIoStore::SetStreamingOptions(EOnDemandStreamingOptions Options) 
{
	StreamingOptions = Options;
	if (StreamingBackend.IsValid())
	{
		StreamingBackend->SetBulkOptionalEnabled(EnumHasAnyFlags(Options, EOnDemandStreamingOptions::OptionalBulkDataDisabled) == false);
	}

	if (HttpIoBackend.IsValid())
	{
		HttpIoBackend->SetOptionalBulkDataEnabled(EnumHasAnyFlags(Options, EOnDemandStreamingOptions::OptionalBulkDataDisabled) == false);
	}
}

void FOnDemandIoStore::ReportAnalytics(TArray<FAnalyticsEventAttribute>& OutAnalyticsArray) const
{
	if (Installer.IsValid())
	{
		Installer->ReportAnalytics(OutAnalyticsArray);
	}

	if (StreamingBackend.IsValid())
	{
		return StreamingBackend->ReportAnalytics(OutAnalyticsArray);
	}

	if (HttpIoBackend.IsValid())
	{
		HttpIoBackend->ReportAnalytics(OutAnalyticsArray);
	}
}

TUniquePtr<IAnalyticsRecording> FOnDemandIoStore::StartAnalyticsRecording() const
{
	if (StreamingBackend.IsValid())
	{
		return StreamingBackend->StartAnalyticsRecording();
	}

	return TUniquePtr<IAnalyticsRecording>();
}

void FOnDemandIoStore::OnImmediateAnalytic(FOnDemandImmediateAnalyticHandler EventHandler)
{
	OnDemandSetImmediateAnalyticHandler(MoveTemp(EventHandler));
}

} // namespace UE::IoStore

////////////////////////////////////////////////////////////////////////////////
FStringBuilderBase& operator<<(FStringBuilderBase& Sb, UE::IoStore::EOnDemandContainerFlags Flags)
{
	using namespace UE::IoStore;

	static const TCHAR* Names[]
	{
		TEXT("None"),
		TEXT("PendingEncryptionKey"),
		TEXT("Mounted"),
		TEXT("StreamOnDemand"),
		TEXT("InstallOnDemand"),
		TEXT("Encrypted"),
		TEXT("WithSoftReferences"),
		TEXT("PendingHostGroup")
	};

	if (Flags == EOnDemandContainerFlags::None)
	{
		Sb << TEXT("None");
		return Sb;
	}

	constexpr uint32 BitCount = 1 + FMath::CountTrailingZeros(
		static_cast<std::underlying_type_t<EOnDemandContainerFlags>>(EOnDemandContainerFlags::Last));
	static_assert(UE_ARRAY_COUNT(Names) == BitCount + 1, "Please update names list");

	for (int32 Idx = 0; Idx < BitCount; ++Idx)
	{
		const EOnDemandContainerFlags FlagToTest = static_cast<EOnDemandContainerFlags>(1 << Idx);
		if (EnumHasAnyFlags(Flags, FlagToTest))
		{
			if (Sb.Len())
			{
				Sb << TEXT("|");
			}
			Sb << Names[Idx + 1];
		}
	}

	return Sb;
}

FString LexToString(UE::IoStore::EOnDemandContainerFlags Flags)
{
	TStringBuilder<128> Sb;
	Sb << Flags;
	return FString::ConstructFromPtrSize(Sb.ToString(), Sb.Len());
}
