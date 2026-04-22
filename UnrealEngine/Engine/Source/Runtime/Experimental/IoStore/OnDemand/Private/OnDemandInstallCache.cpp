// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnDemandInstallCache.h"
#include "DiskCacheGovernor.h"
#include "OnDemandHttpClient.h"
#include "OnDemandIoStore.h"
#include "Statistics.h"

#include "Algo/Accumulate.h"
#include "Algo/Find.h"
#include "Async/Mutex.h"
#include "Async/SharedMutex.h"
#include "Async/UniqueLock.h"
#include "Async/SharedLock.h"
#include "Async/AsyncFileHandle.h"
#include "Containers/UnrealString.h"
#include "GenericHash.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFileManager.h"
#include "IO/IoContainerHeader.h"
#include "IO/IoChunkId.h"
#include "IO/IoChunkEncoding.h"
#include "Logging/StructuredLog.h"
#include "IO/OnDemandError.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "Misc/ScopeExit.h"
#include "Misc/StringBuilder.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Tasks/Task.h"
#include "ProfilingDebugging/IoStoreTrace.h"

#if WITH_IOSTORE_ONDEMAND_TESTS
#include "Algo/Find.h"
#include "TestHarness.h"
#include "TestMacros/Assertions.h"
#include <catch2/generators/catch_generators.hpp>
#endif

#ifndef UE_ONDEMANDINSTALLCACHE_EXCLUSIVE_WRITE
#define UE_ONDEMANDINSTALLCACHE_EXCLUSIVE_WRITE (0)
#endif

#ifndef UE_ONDEMANDINSTALLCACHE_USE_MODTIME
#define UE_ONDEMANDINSTALLCACHE_USE_MODTIME (1)
#endif

#if UE_ONDEMANDINSTALLCACHE_EXCLUSIVE_WRITE
#include "Tasks/Pipe.h"
#endif // UE_ONDEMANDINSTALLCACHE_EXCLUSIVE_WRITE

#ifndef UE_IAD_DEBUG_CONSOLE_CMDS
#define UE_IAD_DEBUG_CONSOLE_CMDS (1 && !NO_CVARS && !UE_BUILD_SHIPPING)
#endif

namespace UE::IoStore
{
///////////////////////////////////////////////////////////////////////////////
namespace CVars
{
	static bool GIoStoreOnDemandEnableDefrag = true;
	static FAutoConsoleVariableRef CVar_IoStoreOnDemandEnableDefrag(
		TEXT("iostore.EnableDefrag"),
		GIoStoreOnDemandEnableDefrag,
		TEXT("Whether to enable defrag when purging")
	);
}

///////////////////////////////////////////////////////////////////////////////
double ToKiB(uint64 Value)
{
	return double(Value) / 1024.0;
}

///////////////////////////////////////////////////////////////////////////////
double ToMiB(uint64 Value)
{
	return double(Value) / 1024.0 / 1024.0;
}

///////////////////////////////////////////////////////////////////////////////
using FUniqueFileHandle				= TUniquePtr<IFileHandle>;
using FSharedFileHandle				= TSharedPtr<IFileHandle>;

using FSharedAsyncFileHandle		= TSharedPtr<IAsyncReadFileHandle>;
using FWeakAsyncFileHandle			= TWeakPtr<IAsyncReadFileHandle>;
using FSharedFileOpenAsyncResult	= TValueOrError<FSharedAsyncFileHandle, FFileSystemError>;

using FCasAddr						= FHash96;
static const FCasAddr& AsCasAddr(const FIoHash& IoHash)
{
	return *reinterpret_cast<const FCasAddr*>(&IoHash);
}

///////////////////////////////////////////////////////////////////////////////
struct FCasBlockId
{
	FCasBlockId() = default;
	explicit FCasBlockId(uint32 InId)
		: Id(InId) { }

	bool IsValid() const { return Id != 0; }

	friend inline bool operator==(FCasBlockId LHS, FCasBlockId RHS)
	{
		return LHS.Id == RHS.Id;
	}

	friend inline uint32 GetTypeHash(FCasBlockId BlockId)
	{
		return GetTypeHash(BlockId.Id);
	}

	friend FArchive& operator<<(FArchive& Ar, FCasBlockId& BlockId)
	{
		Ar << BlockId.Id;
		return Ar;
	}

	static const FCasBlockId Invalid;

	uint32 Id = 0;
};

const FCasBlockId FCasBlockId::Invalid = FCasBlockId();

///////////////////////////////////////////////////////////////////////////////
struct FCasLocation
{
	bool IsValid() const { return BlockId.IsValid() && BlockOffset != MAX_uint32; }

	friend inline bool operator==(FCasLocation LHS, FCasLocation RHS)
	{
		return LHS.BlockId == RHS.BlockId && LHS.BlockOffset == RHS.BlockOffset;
	}

	friend inline uint32 GetTypeHash(FCasLocation Loc)
	{
		return HashCombine(GetTypeHash(Loc.BlockId), GetTypeHash(Loc.BlockOffset));
	}

	friend FArchive& operator<<(FArchive& Ar, FCasLocation& Loc)
	{
		Ar << Loc.BlockId; 
		Ar << Loc.BlockOffset;
		return Ar;
	}

	static const FCasLocation Invalid;

	FCasBlockId	BlockId;
	uint32		BlockOffset = MAX_uint32;
};

const FCasLocation FCasLocation::Invalid = FCasLocation();

///////////////////////////////////////////////////////////////////////////////
struct FCasBlockInfo
{
	uint64	FileSize = 0;
	int64	LastAccess = 0;
	uint64	RefSize = 0;
};

using FCasBlockInfoMap = TMap<FCasBlockId, FCasBlockInfo>;

///////////////////////////////////////////////////////////////////////////////
enum class ECasTrackAccessType : uint8
{
	Always,
	Newer,
	Granular
};

///////////////////////////////////////////////////////////////////////////////
struct FCasSnapshot;
struct FCas
{
	static constexpr uint32		DeleteBlockMaxWaitTimeMs = 10000;
	static constexpr int64		DirtyTimestampMask = std::numeric_limits<int64>::lowest(); // sign bit

	using FLookup				= TMap<FCasAddr, FCasLocation>;
	using FReadHandles			= TMap<FCasBlockId, FWeakAsyncFileHandle>;
	using FLastAccess			= TMap<FCasBlockId, int64>;
	using FBlockIdHandleCounts	= TMap<FCasBlockId, int32>;

	FCas(const FOnDemandInstallCacheConfig& Config);

	void						Lock()		{ Mutex.Lock(); }
	void						Unlock()	{ Mutex.Unlock(); }

	FResult						Initialize(FStringView Directory, bool bDeleteExisting = false);
	FCasLocation				FindChunk(const FIoHash& Hash) const;
	FCasBlockId					CreateBlock();
	FResult						DeleteBlock(FCasBlockId BlockId, TArray<FCasAddr>& OutAddrs);
	FString						GetBlockFilename(FCasBlockId BlockId) const;
	TResult<FSharedFileHandle>	OpenRead(FCasBlockId BlockId);
	FSharedFileOpenAsyncResult	OpenAsyncRead(FCasBlockId BlockId);
	void						OnFileHandleDeleted(FCasBlockId BlockId);
	TResult<FUniqueFileHandle>	OpenWrite(FCasBlockId BlockId, bool bAppend) const;
	bool						TrackAccessIf(ECasTrackAccessType Type, FCasBlockId BlockId, int64 UtcTicks, bool bDirty);
	bool						TrackAccessIf(ECasTrackAccessType Type, FCasBlockId BlockId, bool bDirty)
	{
		return TrackAccessIf(Type, BlockId, FDateTime::UtcNow().GetTicks(), bDirty);
	};
	bool						UnlockedTrackAccessIf(ECasTrackAccessType Type, uint32 BlockIdHash, FCasBlockId BlockId, int64 UtcTicks, bool bDirty);
	uint64						GetBlockInfo(FCasBlockInfoMap& OutBlockInfo);
	void						Compact();
	FResult						Verify(TArray<FCasAddr>& OutAddrs);
	void						LoadSnapshot(FCasSnapshot&& Snapshot);
	FLastAccess					ConsumeLastAcccess()
	{
		FLastAccess Temp;
		{
			TUniqueLock Lock(Mutex);
			Temp = MoveTemp(LastAccess);
			LastAccess = FLastAccess();
		}
		return Temp;
	}
	// Returns the timestamps that are "dirty" and need to be flushed to disk and clears dirty flag
	FCas::FLastAccess			GetAndClearDirtyLastAccess();

	// Returns the place holder timestamp to use if a timestamp is not found in the LastAccess table
	static int64				GetTimestampForMissingLastAccess();
	static uint32				GetMaxBlockSize()
	{
		const uint32 MaxBlockSize = 32 << 20; //TODO: Make configurable	
		return MaxBlockSize;
	}

	static uint32				GetMinBlockSize()
	{
		const uint32 MinBlockSize = 32 << 19; //TODO: Make configurable	
		return MinBlockSize;
	}

	FString						GetRootDirectory() const { return FString(RootDirectory); }

private:
	FStringView				RootDirectory;
public: // TODO: FIXME: encapsulate these members
	FLookup					Lookup;
	FBlockIdHandleCounts	BlockIds;
private:
	FLastAccess				LastAccess;
	FReadHandles			ReadHandles;
	FEventRef				BlockReadsDoneEvent;
	const int64				LastAccessGranularityTicks;
	mutable UE::FMutex		Mutex;
};

///////////////////////////////////////////////////////////////////////////////
FCas::FCas(const FOnDemandInstallCacheConfig& Config)
	: LastAccessGranularityTicks(FTimespan::FromSeconds(Config.LastAccessGranularitySeconds).GetTicks())
{
}

///////////////////////////////////////////////////////////////////////////////
FResult FCas::Initialize(FStringView Directory, bool bDeleteExisting)
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	RootDirectory = Directory;

	Lookup.Empty();
	BlockIds.Empty();
	LastAccess.Empty();

	TStringBuilder<256> Path;
	FPathViews::Append(Path, RootDirectory, TEXT("blocks"));

	IFileManager& Ifm = IFileManager::Get();

	if (bDeleteExisting)
	{
		bool bRequireExists = false;
		const bool bTree	= true;

		if (Ifm.DeleteDirectory(Path.ToString(), bRequireExists, bTree) == false)
		{
			return MakeCasError<void>(
				ECasErrorCode::InitializeFailed, EIoErrorCode::DeleteError, FString::Printf(TEXT("Failed to delete CAS blocks directory '%s'"), Path.ToString()));
		}
	}

	if (Ifm.DirectoryExists(Path.ToString()) == false)
	{
		const bool bTree = true;
		if (Ifm.MakeDirectory(Path.ToString(), bTree) == false)
		{
			return MakeCasError<void>(
				ECasErrorCode::InitializeFailed, EIoErrorCode::WriteError, FString::Printf(TEXT("Failed to create CAS blocks directory '%s'"), Path.ToString()));
		}
	}

	return MakeValue();
};

FCasLocation FCas::FindChunk(const FIoHash& Hash) const
{
	const FCasAddr& Addr	= AsCasAddr(Hash);
	const uint32 TypeHash	= GetTypeHash(Addr);
	{
		UE::TUniqueLock Lock(Mutex);
		if (const FCasLocation* Loc = Lookup.FindByHash(TypeHash, Addr))
		{
			return *Loc;
		}
	}

	return FCasLocation{};
}

FCasBlockId FCas::CreateBlock()
{
	IPlatformFile&	Ipf = FPlatformFileManager::Get().GetPlatformFile();
	FCasBlockId		Out = FCasBlockId::Invalid;

	UE::TUniqueLock Lock(Mutex);

	for (uint32 Id = 1; Id < MAX_uint32 && !Out.IsValid(); Id++)
	{
		const FCasBlockId BlockId(Id);
		if (BlockIds.Contains(BlockId))
		{
			continue;
		}

		const FString Filename = GetBlockFilename(BlockId);
		if (Ipf.FileExists(*Filename))
		{
			UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Unused CAS block id %u already exists on disk"), BlockId.Id);
			continue;
		}

		BlockIds.Add(BlockId, 0);
		Out = BlockId;
	}

	return Out;
}

FResult FCas::DeleteBlock(FCasBlockId BlockId, TArray<FCasAddr>& OutAddrs)
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();
	const FString	Filename = GetBlockFilename(BlockId);

	UE::TDynamicUniqueLock Lock(Mutex, UE::FDeferLock());

	// Wait for pending reads to flush before deleting block
	uint32			StartTimeCycles	= FPlatformTime::Cycles();
	const uint32	WaitTimeMs		= 1000;

	for (;;)
	{
		Lock.Lock();

		const int32 RequestCount = BlockIds.FindRef(BlockId);
		if (RequestCount)
		{
			Lock.Unlock();

			if (FPlatformTime::ToMilliseconds(FPlatformTime::Cycles() - StartTimeCycles) > DeleteBlockMaxWaitTimeMs)
			{
				return MakeCasError<void>(
					ECasErrorCode::DeleteBlockFailed, EIoErrorCode::Timeout, FString::Printf(TEXT("Timed out waiting for pending read(s) when deleting CAS block %u"), BlockId.Id));
			}

			BlockReadsDoneEvent->Wait(WaitTimeMs);
		}
		else
		{
			// Leave mutex locked until it goes out of scope
			break;
		}
	}

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Deleting CAS block '%s'"), *Filename);
	if (Ipf.DeleteFile(*Filename) == false)
	{
		return MakeCasError<void>(
			ECasErrorCode::DeleteBlockFailed, EIoErrorCode::DeleteError, FString::Printf(TEXT("Failed to delete CAS block %u"), BlockId.Id));
	}

	BlockIds.Remove(BlockId);
	ReadHandles.Remove(BlockId);
	LastAccess.Remove(BlockId);
	for (auto It = Lookup.CreateIterator(); It; ++It)
	{
		if (It->Value.BlockId == BlockId)
		{
			OutAddrs.Add(It->Key);
			It.RemoveCurrent();
		}
	}

	return MakeValue();
}

FString FCas::GetBlockFilename(FCasBlockId BlockId) const
{
	check(BlockId.IsValid());
	const uint32 Id = NETWORK_ORDER32(BlockId.Id);
	FString Hex;
	BytesToHexLower(reinterpret_cast<const uint8*>(&Id), sizeof(int32), Hex);
	TStringBuilder<256> Path;
	FPathViews::Append(Path, RootDirectory, TEXT("blocks"), Hex);
	Path << TEXT(".ucas");

	return FString(Path.ToView());
}

TResult<FSharedFileHandle> FCas::OpenRead(FCasBlockId BlockId)
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	const FString	Filename = GetBlockFilename(BlockId);
	IPlatformFile&	Ipf = FPlatformFileManager::Get().GetPlatformFile();

	UE::TUniqueLock Lock(Mutex);

	FFileOpenResult Result = Ipf.OpenRead(*Filename, IPlatformFile::EOpenReadFlags::AllowWrite);
	if (Result.HasValue())
	{
		BlockIds.FindOrAdd(BlockId, 0)++;

		FSharedFileHandle NewHandle(
			Result.GetValue().Release(),
			[this, BlockId](IFileHandle* RawHandle)
			{
				delete RawHandle;
				OnFileHandleDeleted(BlockId);
			}
		);

		return MakeValue(MoveTemp(NewHandle));
	}

	return MakeCasError<FSharedFileHandle>(
		ECasErrorCode::ReadBlockFailed, EIoErrorCode::FileOpenFailed, FString::Printf(TEXT("Failed to open CAS block '%s'"), *Filename));
}

FSharedFileOpenAsyncResult FCas::OpenAsyncRead(FCasBlockId BlockId)
{
	UE::TUniqueLock Lock(Mutex);

	if (FWeakAsyncFileHandle* MaybeHandle = ReadHandles.Find(BlockId))
	{
		if (FSharedAsyncFileHandle Handle = MaybeHandle->Pin(); Handle.IsValid())
		{
			return MakeValue(MoveTemp(Handle));
		}
	}

	IPlatformFile&			Ipf = FPlatformFileManager::Get().GetPlatformFile();
	const FString			Filename = GetBlockFilename(BlockId);
	FFileOpenAsyncResult	HandleResult(Ipf.OpenAsyncRead(*Filename, IPlatformFile::EOpenReadFlags::AllowWrite));

	if (HandleResult.HasValue())
	{
		BlockIds.FindOrAdd(BlockId, 0)++;

		FSharedAsyncFileHandle NewHandle(
			HandleResult.GetValue().Release(),
			[this, BlockId](IAsyncReadFileHandle* RawHandle)
			{
				delete RawHandle;
				OnFileHandleDeleted(BlockId);
			}
		);
		ReadHandles.FindOrAdd(BlockId, NewHandle);
		
		return MakeValue(MoveTemp(NewHandle));
	}

	return MakeError(HandleResult.StealError());
}

void FCas::OnFileHandleDeleted(FCasBlockId BlockId)
{
	UE::TUniqueLock Lock(Mutex);
	const int32 Count = --BlockIds.FindChecked(BlockId);
	check(Count >= 0);
	if (Count == 0)
	{
		BlockReadsDoneEvent->Trigger();
	}
}

TResult<FUniqueFileHandle> FCas::OpenWrite(FCasBlockId BlockId, const bool bAppend) const
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	IPlatformFile&	Ipf = FPlatformFileManager::Get().GetPlatformFile();
	const FString	Filename = GetBlockFilename(BlockId);
	const bool		bAllowRead = true;

	FUniqueFileHandle FileHandle(Ipf.OpenWrite(*Filename, bAppend, bAllowRead));
	if (FileHandle.IsValid())
	{
		return MakeValue(MoveTemp(FileHandle));
	}

	return MakeCasError<FUniqueFileHandle>(
		ECasErrorCode::WriteBlockFailed, EIoErrorCode::FileOpenFailed, FString::Printf(TEXT("Failed to open CAS block '%s' for writing"), *Filename));
}

bool FCas::TrackAccessIf(const ECasTrackAccessType Type, const FCasBlockId BlockId, const int64 UtcTicks, const bool bDirty)
{
	const uint32 BlockIdHash = GetTypeHash(BlockId);

	UE::TUniqueLock Lock(Mutex);

	return UnlockedTrackAccessIf(Type, BlockIdHash, BlockId, UtcTicks, bDirty);
}

bool FCas::UnlockedTrackAccessIf(const ECasTrackAccessType Type, const uint32 BlockIdHash, const FCasBlockId BlockId, int64 UtcTicks, bool bDirty)
{
	check(BlockId.IsValid());

	int64* MaybeFoundTicks = LastAccess.FindByHash(BlockIdHash, BlockId);
	if (MaybeFoundTicks == nullptr)
	{
		if (bDirty)
		{
			UtcTicks |= DirtyTimestampMask;
		}

		LastAccess.AddByHash(BlockIdHash, BlockId, UtcTicks);

		return true;
	}

	int64& FoundTicks = *MaybeFoundTicks;
	const int64 PrevTicks = (FoundTicks & ~DirtyTimestampMask);
	bDirty = bDirty || (FoundTicks & DirtyTimestampMask); // Don't clear dirty flag if already set

	bool bUpdate = true;
	switch (Type)
	{
	case ECasTrackAccessType::Newer:
		bUpdate = PrevTicks < UtcTicks;
		break;

	case ECasTrackAccessType::Granular:
		bUpdate = PrevTicks < UtcTicks && (UtcTicks - PrevTicks > LastAccessGranularityTicks);
		break;
	}

	if (bUpdate)
	{
		if (bDirty)
		{
			UtcTicks |= DirtyTimestampMask;
		}

		FoundTicks = UtcTicks;
		return true;
	}

	return false;
}

uint64 FCas::GetBlockInfo(FCasBlockInfoMap& OutBlockInfo)
{
	TStringBuilder<256> Path;
	FPathViews::Append(Path, RootDirectory, TEXT("blocks"));

	struct FDirectoryVisitor final
		: public IPlatformFile::FDirectoryVisitor
	{
		FDirectoryVisitor(IPlatformFile& PlatformFile, FCasBlockInfoMap& InBlockInfo, FLastAccess&& Access)
			: Ipf(PlatformFile)
			, BlockInfo(InBlockInfo)
			, LastAccess(MoveTemp(Access))
		{ }
		
		virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
		{
			if (bIsDirectory)
			{
				return true;
			}

			const FStringView Filename(FilenameOrDirectory);
			if (FPathViews::GetExtension(Filename) == TEXTVIEW("ucas") == false)
			{
				return true;
			}

			const int64			FileSize = Ipf.FileSize(FilenameOrDirectory);
			const FStringView	IndexHex = FPathViews::GetBaseFilename(Filename);
			const FCasBlockId	BlockId(FParse::HexNumber(WriteToString<128>(IndexHex).ToString()));

			if (BlockId.IsValid() == false || FileSize < 0)
			{
				UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Found invalid CAS block '%s', FileSize=%" INT64_FMT),
					FilenameOrDirectory, FileSize);
				return true;
			}

			if (BlockInfo.Contains(BlockId))
			{
				UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Found duplicate CAS block '%s'"), FilenameOrDirectory);
				return true;
			}

			const int64* UtcTicks = LastAccess.Find(BlockId);

			BlockInfo.Add(BlockId, FCasBlockInfo
			{
				.FileSize = uint64(FileSize),
				.LastAccess = UtcTicks ? (*UtcTicks & ~FCas::DirtyTimestampMask) : TimestampForMissingLastAccess
			});
			TotalSize += uint64(FileSize);

			return true;
		}

		IPlatformFile&		Ipf;
		FCasBlockInfoMap&	BlockInfo;
		FLastAccess			LastAccess;
		const int64			TimestampForMissingLastAccess = FCas::GetTimestampForMissingLastAccess();
		uint64				TotalSize = 0;
	};

	FLastAccess Access;
	{
		TUniqueLock Lock(Mutex);
		Access = LastAccess;
	}
	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();
	FDirectoryVisitor Visitor(Ipf, OutBlockInfo, MoveTemp(Access));
	Ipf.IterateDirectory(Path.ToString(), Visitor);

	return Visitor.TotalSize;
}

void FCas::Compact()
{
	UE::TUniqueLock Lock(Mutex);
	Lookup.Compact();
	BlockIds.Compact();
	ReadHandles.Compact();
	LastAccess.Compact();
}

FResult FCas::Verify(TArray<FCasAddr>& OutAddrs)
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	FCasBlockInfoMap	BlockInfo;
	const uint64		TotalSize = GetBlockInfo(BlockInfo);
	uint64				TotalVerifiedBytes = 0;
	FResult				Result = MakeValue();

	const int64 Now = FDateTime::UtcNow().GetTicks();
	const int64 TimestampForMissingLastAccess = FCas::GetTimestampForMissingLastAccess();

	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();

	UE::TUniqueLock Lock(Mutex);

	for (auto BlockIt = BlockIds.CreateIterator(); BlockIt; ++BlockIt)
	{
		const FCasBlockId BlockId = BlockIt->Key;
		if (const FCasBlockInfo* Info = BlockInfo.Find(BlockId))
		{
			if (int64* TimeStamp = LastAccess.Find(BlockId))
			{
				if (*TimeStamp > Now)
				{
					const FString Filename = GetBlockFilename(BlockId);
					UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Found future last access time for CAS block '%s'"), *Filename);
					*TimeStamp = Now;
				}
			}
			else
			{
				const FString Filename = GetBlockFilename(BlockId);
				
#if UE_ONDEMANDINSTALLCACHE_USE_MODTIME
				FDateTime ModTime = Ipf.GetTimeStamp(*Filename);
				if (ensure(ModTime > FDateTime::MinValue()))
				{
					if (ModTime > Now)
					{
						UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Missing last access time and found future mod time so using now time for CAS block '%s'"), *Filename);
						LastAccess.Add(BlockId, Now);
					}
					else
					{
						UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Missing last access time so using file mod time for CAS block '%s'"), *Filename);
						LastAccess.Add(BlockId, ModTime.GetTicks());
					}
				}
				else
#endif // UE_ONDEMANDINSTALLCACHE_USE_MODTIME
				{
					// Failed to get mod time
					UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Missing last access time for CAS block '%s'"), *Filename);
					LastAccess.Add(BlockId, TimestampForMissingLastAccess);
				}
			}

			TotalVerifiedBytes += Info->FileSize;
			continue;
		}

		const FString Filename = GetBlockFilename(BlockId);
		FString ErrorMessage = FString::Printf(TEXT("Missing CAS block '%s'"), *Filename);
		UE_LOG(LogIoStoreOnDemand, Warning, TEXT("%s"), *ErrorMessage);

		LastAccess.Remove(BlockId);
		BlockIt.RemoveCurrent();
		Result = MakeCasError<void>(ECasErrorCode::VerifyFailed, EIoErrorCode::NotFound, MoveTemp(ErrorMessage));
	}

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Verified %d CAS blocks of total %.2lf MiB"),
		BlockIds.Num(), ToMiB(TotalVerifiedBytes));

	for (const TPair<FCasBlockId, FCasBlockInfo>& Kv : BlockInfo)
	{
		const FCasBlockId BlockId = Kv.Key;
		if (BlockIds.Contains(BlockId))
		{
			continue;
		}

		const FString Filename = GetBlockFilename(BlockId);
		if (Ipf.DeleteFile(*Filename))
		{
			UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Deleted orphaned CAS block '%s'"), *Filename);
		}
	}

	TSet<FString> MissingReferencedBlocks;
	for (auto It = Lookup.CreateIterator(); It; ++It)
	{
		if (!BlockIds.Contains(It->Value.BlockId))
		{
			FString Filename		= GetBlockFilename(It->Value.BlockId);
			FString ErrorMessage	= FString::Printf(TEXT("Missing CAS block '%s'"), *Filename);
			MissingReferencedBlocks.Add(MoveTemp(Filename));

			OutAddrs.Add(It->Key);
			It.RemoveCurrent();

			Result = MakeCasError<void>(ECasErrorCode::VerifyFailed, EIoErrorCode::NotFound, MoveTemp(ErrorMessage));
		}
	}

	for (const FString& Filename : MissingReferencedBlocks)
	{
		UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Lookup references missing CAS block '%s'"), *Filename);
	}

	return Result;
}

FCas::FLastAccess FCas::GetAndClearDirtyLastAccess()
{
	FCas::FLastAccess DirtyLastAccess;
	{
		TUniqueLock Lock(Mutex);
		for (TPair<FCasBlockId, int64>& Kv : LastAccess)
		{
			int64& Timestamp = Kv.Value;
			if (Timestamp & FCas::DirtyTimestampMask)
			{
				DirtyLastAccess.Add(Kv.Key, Timestamp);
				Timestamp &= ~FCas::DirtyTimestampMask;
			}
		}
	}

	return DirtyLastAccess;
}

int64 FCas::GetTimestampForMissingLastAccess()
{
	const FDateTime Now = FDateTime::UtcNow();
	const FTimespan FourWeeks = FTimespan::FromDays(4*7);
	const FDateTime FourWeeksAgo = Now - FourWeeks;
	return FourWeeksAgo.GetTicks();
}

///////////////////////////////////////////////////////////////////////////////
struct FCasJournal
{
	enum class EVersion : uint32
	{
		Invalid	= 0,
		Initial,

		LatestPlusOne,
		Latest = LatestPlusOne - 1
	};

	enum class EErrorCode : uint32
	{
		None					= 0,
		Simulated				= 1,
		DefragOutOfDiskSpace	= 2,
		DefragHashMismatch		= 3
	};

	struct FHeader
	{
		static const inline uint8 MagicSequence[16] = {'C', 'A', 'S', 'J', 'O', 'U', 'R', 'N', 'A', 'L', 'H', 'E', 'A', 'D', 'E', 'R'};

		bool			IsValid() const;
		static int64	Size() { return sizeof(FHeader); }

		uint8		Magic[16] = {0};
		EVersion	Version = EVersion::Invalid;
		uint8		Pad[12] = {0};
	};
	static_assert(sizeof(FHeader) == 32);

	struct FFooter
	{
		static const inline uint8 MagicSequence[16] = {'C', 'A', 'S', 'J', 'O', 'U', 'R', 'N', 'A', 'L', 'F', 'O', 'O', 'T', 'E', 'R'};

		bool			IsValid() const;
		static int64	Size() { return sizeof(FFooter); }

		uint8 Magic[16] = {0};
	};
	static_assert(sizeof(FFooter) == 16);

	struct FEntry
	{
		enum class EType : uint8
		{
			None = 0,
			ChunkLocation,
			BlockCreated,
			BlockDeleted,
			BlockAccess,
			CriticalError
		};

		struct FChunkLocation
		{
			EType			Type = EType::ChunkLocation;
			uint8			Pad[3]= {0};
			FCasLocation	CasLocation;
			FCasAddr		CasAddr;
		};
		static_assert(sizeof(FChunkLocation) == 24);

		struct FBlockOperation
		{
			EType		Type = EType::None;
			uint8		Pad[3]= {0};
			FCasBlockId	BlockId;
			int64		UtcTicks = 0;
			uint8		Pad1[8]= {0};
		};
		static_assert(sizeof(FBlockOperation) == 24);

		struct FCriticalError
		{
			EType		Type = EType::CriticalError;
			EErrorCode	ErrorCode = EErrorCode::None;
		};
		static_assert(sizeof(FBlockOperation) == 24);

		union
		{
			FChunkLocation	ChunkLocation;
			FBlockOperation	BlockOperation;
			FCriticalError	CriticalError;
		};

		EType			Type() const { return *reinterpret_cast<const EType*>(this); }
		static int64	Size() { return sizeof(FEntry); }
	};
	static_assert(sizeof(FEntry) == 24);

	struct FTransaction
	{
		void			ChunkLocation(const FCasLocation& Location, const FCasAddr& Addr);
		void			BlockCreated(FCasBlockId BlockId);
		void			BlockDeleted(FCasBlockId BlockId);
		void			BlockAccess(FCasBlockId BlockId, int64 UtcTicks);
		void			CriticalError(FCasJournal::EErrorCode ErrorCode);

		FString			JournalFile;
		TArray<FEntry>	Entries;
	};

	using FEntryHandler		= TFunction<void(const FEntry&)>;

	static FResult			Replay(const FString& JournalFile, FEntryHandler&& Handler);
	static FResult			Create(const FString& JournalFile);
	static FTransaction		Begin(FString&& JournalFile);
	static FTransaction		Begin(const FString& JournalFile) { return Begin(FString(JournalFile)); }
	static FResult			Commit(FTransaction&& Transaction);
	static FResult			Commit(FTransaction&& Transaction, uint64& OutByteCount, uint32& OutOpCount);
};

///////////////////////////////////////////////////////////////////////////////
static void JournalLastAccess(FCasJournal::FTransaction& Transaction, const FCas::FLastAccess& LastAccess)
{
	for (const TPair<FCasBlockId, int64>& Kv : LastAccess)
	{
		int64 Timestamp = Kv.Value;
		if (Timestamp & FCas::DirtyTimestampMask)
		{
			Transaction.BlockAccess(Kv.Key, Timestamp & ~FCas::DirtyTimestampMask);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
static const TCHAR* GetErrorText(FCasJournal::EErrorCode ErrorCode)
{
	switch (ErrorCode)
	{
		case FCasJournal::EErrorCode::None:
			return TEXT("None");
		case FCasJournal::EErrorCode::Simulated:
			return TEXT("Simulated error");
		case FCasJournal::EErrorCode::DefragOutOfDiskSpace:
			return TEXT("Defrag failed due to out of disk space");
		case FCasJournal::EErrorCode::DefragHashMismatch:
			return TEXT("Found corrupt chunk while defragging");
	}

	return TEXT("Unknown");
}

///////////////////////////////////////////////////////////////////////////////
bool FCasJournal::FHeader::IsValid() const
{
	if (FMemory::Memcmp(&Magic, &FHeader::MagicSequence, sizeof(FHeader::MagicSequence)) != 0)
	{
		return false;
	}

	if (static_cast<uint32>(Version) > static_cast<uint32>(EVersion::Latest))
	{
		return false;
	}

	return true;
}

bool FCasJournal::FFooter::IsValid() const
{
	return FMemory::Memcmp(Magic, FFooter::MagicSequence, sizeof(FFooter::MagicSequence)) == 0;
}

FResult FCasJournal::Replay(const FString& JournalFile, FEntryHandler&& Handler)
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();

	if (Ipf.FileExists(*JournalFile) == false)
	{
		return MakeJournalError(ECasErrorCode::ReplayJournalFailed, EIoErrorCode::NotFound, FString::Printf(TEXT("Failed to find '%s'"), *JournalFile));
	}

	TUniquePtr<IFileHandle> FileHandle(Ipf.OpenRead(*JournalFile));
	if (FileHandle.IsValid() == false)
	{
		return MakeJournalError(ECasErrorCode::ReplayJournalFailed, EIoErrorCode::FileOpenFailed, FString::Printf(TEXT("Failed to open '%s'"), *JournalFile));
	}

	FHeader Header;
	if ((FileHandle->Read(reinterpret_cast<uint8*>(&Header), FHeader::Size()) == false) || (Header.IsValid() == false))
	{
		return MakeJournalError(ECasErrorCode::ReplayJournalFailed, EIoErrorCode::ReadError, FString::Printf(TEXT("Failed to validate journal header in '%s'"), *JournalFile));
	}

	const int64 FileSize	= FileHandle->Size();
	const int64 EntryCount	= (FileSize - FHeader::Size() - FFooter::Size()) / FEntry::Size();

	if (EntryCount < 0)
	{
		return MakeJournalError(ECasErrorCode::ReplayJournalFailed, EIoErrorCode::FileCorrupt, FString::Printf(TEXT("Invalid journal file '%s'"), *JournalFile));
	}

	if (EntryCount == 0)
	{
		return MakeValue();
	}

	const int64 FooterPos = FileSize - FFooter::Size();
	if (FooterPos < 0)
	{
		return MakeJournalError(ECasErrorCode::ReplayJournalFailed, EIoErrorCode::FileCorrupt, FString::Printf(TEXT("Invalid journal footer in '%s'"), *JournalFile));
	}

	const int64 EntriesPos = FileHandle->Tell();
	if (FileHandle->Seek(FooterPos) == false)
	{
		return MakeJournalError(ECasErrorCode::ReplayJournalFailed, EIoErrorCode::FileSeekFailed, FString::Printf(TEXT("Failed to seek to footer offset %" INT64_FMT " in '%s'"), FooterPos, *JournalFile));
	}

	FFooter Footer;
	if ((FileHandle->Read(reinterpret_cast<uint8*>(&Footer), FFooter::Size()) == false) || (Footer.IsValid() == false))
	{
		return MakeJournalError(ECasErrorCode::ReplayJournalFailed, EIoErrorCode::ReadError, FString::Printf(TEXT("Failed to validate journal footer in '%s'"), *JournalFile));
	}

	if (FileHandle->Seek(EntriesPos) == false)
	{
		return MakeJournalError(ECasErrorCode::ReplayJournalFailed, EIoErrorCode::FileSeekFailed, FString::Printf(TEXT("Failed to seek to entries offset %" INT64_FMT " in '%s'"), EntriesPos, *JournalFile));
	}

	TArray<FEntry> Entries;
	Entries.SetNumZeroed(IntCastChecked<int32>(EntryCount));

	if (FileHandle->Read(reinterpret_cast<uint8*>(Entries.GetData()), FEntry::Size() * EntryCount) == false)
	{
		return MakeJournalError(ECasErrorCode::ReplayJournalFailed, EIoErrorCode::ReadError, FString::Printf(TEXT("Failed to read journal entries in '%s'"), *JournalFile));
	}

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Replaying %" INT64_FMT " CAS journal entries of total %.2lf KiB from '%s'"),
		EntryCount, ToKiB(FEntry::Size() * EntryCount), *JournalFile);

	for (const FEntry& Entry : Entries)
	{
		if (Entry.Type() == FEntry::EType::CriticalError)
		{
			const FEntry::FCriticalError& Error = Entry.CriticalError;
			UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Found critical error entry '%s' (%d) in journal '%s'"),
				GetErrorText(Error.ErrorCode), Error.ErrorCode, *JournalFile);

			// We append "critical error" entries to the journal when we endup in an unrecoverable error state. This will cause the cache to be reset
			// at startup
			return MakeJournalError(ECasErrorCode::ReplayJournalFailed, EIoErrorCode::InvalidCode, FString::Printf(TEXT("Found critical error journal entry in '%s'"), *JournalFile));
		}

		Handler(Entry);
	}

	return MakeValue();
}

FResult FCasJournal::Create(const FString& JournalFile)
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();
	Ipf.DeleteFile(*JournalFile);

	TUniquePtr<IFileHandle> FileHandle(Ipf.OpenWrite(*JournalFile));
	if (FileHandle.IsValid() == false)
	{
		return MakeJournalError(ECasErrorCode::CreateJournalFailed, EIoErrorCode::FileOpenFailed, FString::Printf(TEXT("Failed to create journal '%s'"), *JournalFile));
	}

	FHeader Header;
	FMemory::Memcpy(&Header.Magic, &FHeader::MagicSequence, sizeof(FHeader::MagicSequence));
	Header.Version = EVersion::Latest;

	if (FileHandle->Write(reinterpret_cast<uint8*>(&Header), FHeader::Size()) == false)
	{
		return MakeJournalError(ECasErrorCode::CreateJournalFailed, EIoErrorCode::WriteError, FString::Printf(TEXT("Failed to write journal header in '%s'"), *JournalFile));
	}

	FFooter Footer;
	FMemory::Memcpy(&Footer.Magic, &FFooter::MagicSequence, sizeof(FFooter::MagicSequence));
	if (FileHandle->Write(reinterpret_cast<uint8*>(&Footer), FFooter::Size()) == false)
	{
		return MakeJournalError(ECasErrorCode::CreateJournalFailed, EIoErrorCode::WriteError, FString::Printf(TEXT("Failed to write journal footer in '%s'"), *JournalFile));
	}

	return MakeValue();
}

FCasJournal::FTransaction FCasJournal::Begin(FString&& JournalFile)
{
	return FTransaction
	{
		.JournalFile = MoveTemp(JournalFile)
	};
}

FResult FCasJournal::Commit(FTransaction&& Transaction, uint64& OutByteCount, uint32& OutOpCount)
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	OutByteCount	= 0;
	OutOpCount		= 0;

	if (Transaction.Entries.IsEmpty())
	{
		return MakeValue();
	}

	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();

	FResult Result			= MakeValue();
	uint64 TotalEntrySize	= 0;
	ON_SCOPE_EXIT
	{
		FOnDemandInstallCacheStats::OnJournalCommit(Result, TotalEntrySize);
	};

	// Validate header and footer
	{
		TUniquePtr<IFileHandle> FileHandle(Ipf.OpenRead(*Transaction.JournalFile));
		if (FileHandle.IsValid() == false)
		{
			return Result = MakeJournalError(
				ECasErrorCode::CommitJournalFailed, EIoErrorCode::FileOpenFailed, FString::Printf(TEXT("Failed to open journal '%s'"), *Transaction.JournalFile));
		}

		const int64 FileSize = FileHandle->Size();
		if (FileSize < FHeader::Size())
		{
			return Result = MakeJournalError(
				ECasErrorCode::CommitJournalFailed, EIoErrorCode::FileCorrupt, FString::Printf(TEXT("Failed to validate journal header in '%s'"), *Transaction.JournalFile));
		}

		FHeader Header;
		if ((FileHandle->Read(reinterpret_cast<uint8*>(&Header), FHeader::Size()) == false) || (Header.IsValid() == false))
		{
			return Result = MakeJournalError(
				ECasErrorCode::CommitJournalFailed, EIoErrorCode::ReadError, FString::Printf(TEXT("Failed to validate journal header in '%s'"), *Transaction.JournalFile));
		}

		const int64 FooterPos = FileSize - FFooter::Size();
		if (FileHandle->Seek(FooterPos) == false)
		{
			return Result = MakeJournalError(
				ECasErrorCode::CommitJournalFailed, EIoErrorCode::FileSeekFailed, FString::Printf(TEXT("Failed to validate journal footer in '%s'"), *Transaction.JournalFile));
		}

		FFooter Footer;
		if ((FileHandle->Read(reinterpret_cast<uint8*>(&Footer), FFooter::Size()) == false) || (Footer.IsValid() == false))
		{
			return Result = MakeJournalError(
				ECasErrorCode::CommitJournalFailed, EIoErrorCode::ReadError, FString::Printf(TEXT("Found to validate journal footer in '%s'"), *Transaction.JournalFile));
		}
	}

	// Append entries
	{
		const bool				bAppend = true;
		TUniquePtr<IFileHandle> FileHandle(Ipf.OpenWrite(*Transaction.JournalFile, bAppend));
		const int64				FileSize	= FileHandle.IsValid() ? FileHandle->Size() : -1;
		const int64				EntriesPos	= FileSize > 0 ? FileSize - FFooter::Size() : -1;

		if (EntriesPos < 0) 
		{
			return Result = MakeJournalError(
				ECasErrorCode::CommitJournalFailed, EIoErrorCode::FileOpenFailed, FString::Printf(TEXT("Failed to open journal '%s'"), *Transaction.JournalFile));
		}

		if (FileHandle->Seek(EntriesPos) == false)
		{
			return Result = MakeJournalError(
				ECasErrorCode::CommitJournalFailed, EIoErrorCode::FileSeekFailed, FString::Printf(TEXT("Failed to seek to journal entries offset %" INT64_FMT " in '%s'"), EntriesPos, *Transaction.JournalFile));
		}

		TotalEntrySize = Transaction.Entries.Num() * FEntry::Size();
		if (FileHandle->Write(
			reinterpret_cast<const uint8*>(Transaction.Entries.GetData()),
			TotalEntrySize) == false)
		{
			return Result = MakeJournalError(
				ECasErrorCode::CommitJournalFailed, EIoErrorCode::WriteError, FString::Printf(TEXT("Failed to write journal entries to '%s'"), *Transaction.JournalFile));
		}
		OutOpCount++;
		OutByteCount += TotalEntrySize;

		FFooter Footer;
		FMemory::Memcpy(&Footer.Magic, &FFooter::MagicSequence, sizeof(FFooter::MagicSequence));
		if (FileHandle->Write(reinterpret_cast<uint8*>(&Footer), FFooter::Size()) == false)
		{
			return Result = MakeJournalError(
				ECasErrorCode::CommitJournalFailed, EIoErrorCode::WriteError, FString::Printf(TEXT("Failed to write journal footer to '%s'"), *Transaction.JournalFile));
		}
		OutOpCount++;
		OutByteCount += uint64(FFooter::Size());

		if (FileHandle->Flush() == false)
		{
			return Result = MakeJournalError(
				ECasErrorCode::CommitJournalFailed, EIoErrorCode::FileFlushFailed, FString::Printf(TEXT("Failed to flush journal entries to '%s'"), *Transaction.JournalFile));
		}
		OutOpCount++;

		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Committed %d CAS journal entries of total %.2lf KiB to '%s'"),
			Transaction.Entries.Num(), ToKiB(TotalEntrySize), *Transaction.JournalFile);

		check(Result.HasValue());
		return Result;
	}
}

FResult FCasJournal::Commit(FTransaction&& Transaction)
{
	uint64 ByteCount	= 0;
	uint32 OpCount		= 0;
	return Commit(MoveTemp(Transaction), ByteCount, OpCount);
}

void FCasJournal::FTransaction::ChunkLocation(const FCasLocation& Location, const FCasAddr& Addr)
{
	Entries.AddZeroed_GetRef().ChunkLocation = FEntry::FChunkLocation
	{
		.CasLocation	= Location,
		.CasAddr		= Addr
	};
}

void FCasJournal::FTransaction::BlockCreated(FCasBlockId BlockId)
{
	Entries.AddZeroed_GetRef().BlockOperation = FEntry::FBlockOperation
	{
		.Type		= FEntry::EType::BlockCreated,
		.BlockId	= BlockId,
		.UtcTicks	= FDateTime::UtcNow().GetTicks()
	};
}

void FCasJournal::FTransaction::BlockDeleted(FCasBlockId BlockId)
{
	Entries.AddZeroed_GetRef().BlockOperation = FEntry::FBlockOperation
	{
		.Type		= FEntry::EType::BlockDeleted,
		.BlockId	= BlockId,
		.UtcTicks	= FDateTime::UtcNow().GetTicks()
	};
}

void FCasJournal::FTransaction::BlockAccess(FCasBlockId BlockId, int64 UtcTicks)
{
	Entries.AddZeroed_GetRef().BlockOperation = FEntry::FBlockOperation
	{
		.Type		= FEntry::EType::BlockAccess,
		.BlockId	= BlockId,
		.UtcTicks	= UtcTicks
	};
}

void FCasJournal::FTransaction::CriticalError(FCasJournal::EErrorCode ErrorCode)
{
	Entries.AddZeroed_GetRef().CriticalError = FEntry::FCriticalError
	{
		.Type		= FEntry::EType::CriticalError,
		.ErrorCode	= ErrorCode
	};
}

///////////////////////////////////////////////////////////////////////////////
struct FCasSnapshot
{
	enum class EVersion : uint32
	{
		Invalid = 0,
		Initial,

		LatestPlusOne,
		Latest = LatestPlusOne - 1
	};

	struct FHeader
	{
		static const inline uint8 MagicSequence[16] = {'+', 'S', 'N', 'A', 'P', 'S', 'H', 'O', 'T', 'H', 'E', 'A', 'D', 'E', 'R', '+'};

		bool			IsValid() const;
		static int64	Size() { return sizeof(FHeader); }

		uint8		Magic[16] = {0};
		EVersion	Version = EVersion::Invalid;
		uint8		Pad[12] = {0};
	};
	static_assert(sizeof(FHeader) == 32);

	struct FFooter
	{
		static const inline uint8 MagicSequence[16] = {'+', 'S', 'N', 'A', 'P', 'S', 'H', 'O', 'T', 'F', 'O', 'O', 'T', 'E', 'R', '+'};

		static int64	Size() { return sizeof(FFooter); }
		bool			IsValid() const;

		uint8 Magic[16] = {0};
	};
	static_assert(sizeof(FFooter) == 16);

	struct FBlock
	{
		friend FArchive& operator<<(FArchive& Ar, FBlock& Block)
		{
			Ar << Block.BlockId;
			Ar << Block.LastAccess;
			return Ar;
		}

		FCasBlockId BlockId;
		int64		LastAccess = 0;
	};

	using FChunkLocation = TPair<FCasAddr, FCasLocation>;

	static TResult<FCasSnapshot>		FromJournal(const FString& JournalFile);
	static TResult<FCasSnapshot>		Load(const FString& SnapshotFile, int64* OutFileSize = nullptr);
	static TResult<int64>				Save(const FCasSnapshot& Snapshot, const FString& SnapshotFile);
	static TResult<int64>				TryCreateAndResetJournal(const FString& SnapshotFile, const FString& JournalFile);

	TArray<FBlock>						Blocks;
	TArray<FChunkLocation>				ChunkLocations;
	FCasBlockId							CurrentBlockId;
};

///////////////////////////////////////////////////////////////////////////////
bool FCasSnapshot::FHeader::IsValid() const
{
	if (FMemory::Memcmp(&Magic, &FHeader::MagicSequence, sizeof(FHeader::MagicSequence)) != 0)
	{
		return false;
	}

	if (static_cast<uint32>(Version) > static_cast<uint32>(EVersion::Latest))
	{
		return false;
	}

	return true;
}

bool FCasSnapshot::FFooter::IsValid() const
{
	return FMemory::Memcmp(Magic, FFooter::MagicSequence, sizeof(FFooter::MagicSequence)) == 0;
}

TResult<FCasSnapshot> FCasSnapshot::FromJournal(const FString& JournalFile)
{
	FCas::FLookup		CasLookup;
	FCas::FLastAccess	LastAccess;
	TSet<FCasBlockId>	BlockIds;
	FCasBlockId			CurrentBlockId;

	FResult ReplayResult = FCasJournal::Replay(
		JournalFile,
		[&CasLookup, &LastAccess, &BlockIds, &CurrentBlockId](const FCasJournal::FEntry& JournalEntry)
		{
			switch(JournalEntry.Type())
			{
			case FCasJournal::FEntry::EType::ChunkLocation:
			{
				const FCasJournal::FEntry::FChunkLocation& ChunkLocation = JournalEntry.ChunkLocation;
				if (ChunkLocation.CasLocation.IsValid())
				{
					FCasLocation& Loc = CasLookup.FindOrAdd(ChunkLocation.CasAddr);
					Loc = ChunkLocation.CasLocation;
				}
				else
				{
					CasLookup.Remove(ChunkLocation.CasAddr);
				}
				break;
			}
			case FCasJournal::FEntry::EType::BlockCreated:
			{
				const FCasJournal::FEntry::FBlockOperation& Op = JournalEntry.BlockOperation;
				CurrentBlockId = Op.BlockId;
				BlockIds.Add(Op.BlockId);
				break;
			}
			case FCasJournal::FEntry::EType::BlockDeleted:
			{
				const FCasJournal::FEntry::FBlockOperation& Op = JournalEntry.BlockOperation;
				BlockIds.Remove(Op.BlockId);
				if (CurrentBlockId == Op.BlockId)
				{
					CurrentBlockId = FCasBlockId::Invalid; 
				}
				break;
			}
			case FCasJournal::FEntry::EType::BlockAccess:
			{
				const FCasJournal::FEntry::FBlockOperation& Op = JournalEntry.BlockOperation;
				LastAccess.Add(Op.BlockId, Op.UtcTicks);
				break;
			}
			};
		});

	if (ReplayResult.HasError())
	{
		return MakeError(ReplayResult.StealError());
	}

	const int64 TimestampForMissingLastAccess = FCas::GetTimestampForMissingLastAccess();

	FCasSnapshot Snapshot;
	Snapshot.Blocks.Reserve(BlockIds.Num());
	for (FCasBlockId BlockId : BlockIds)
	{
		const int64* AccessTime = LastAccess.Find(BlockId);
		Snapshot.Blocks.Add(FBlock
		{
			.BlockId	= BlockId,
			.LastAccess = AccessTime != nullptr ? *AccessTime : TimestampForMissingLastAccess
		});
	}

	Snapshot.ChunkLocations = CasLookup.Array();
	Snapshot.CurrentBlockId = CurrentBlockId;

	return MakeValue(Snapshot);
}

TResult<int64> FCasSnapshot::Save(const FCasSnapshot& Snapshot, const FString& SnapshotFile)
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	IFileManager& Ifm = IFileManager::Get();

	const FString TmpSnapshotFile = FPaths::ChangeExtension(SnapshotFile, TEXT(".snptmp"));

	TUniquePtr<FArchive> Ar(Ifm.CreateFileWriter(*TmpSnapshotFile));
	if (Ar.IsValid() == false)
	{
		return MakeSnapshotError<int64>(ECasErrorCode::SaveSnapshotFailed, EIoErrorCode::FileOpenFailed, FString::Printf(TEXT("Failed to open file '%s' for writing"), *SnapshotFile));
	}

	FHeader Header;
	FMemory::Memcpy(&Header.Magic, &FHeader::MagicSequence, sizeof(FHeader::MagicSequence));
	Header.Version = EVersion::Latest;

	Ar->Serialize(reinterpret_cast<uint8*>(&Header), FHeader::Size());
	if (Ar->IsError())
	{
		const uint32 LastError = FPlatformMisc::GetLastError();
		Ar.Reset();
		Ifm.Delete(*TmpSnapshotFile);
		return MakeSnapshotError<int64>(ECasErrorCode::SaveSnapshotFailed, EIoErrorCode::WriteError, FString::Printf(TEXT("Failed to write snapshot header to '%s'"), *SnapshotFile), LastError);
	}

	FCasSnapshot& NonConst = *const_cast<FCasSnapshot*>(&Snapshot);
	*Ar << NonConst.Blocks;
	*Ar << NonConst.ChunkLocations;
	*Ar << NonConst.CurrentBlockId;

	if (Ar->IsError())
	{
		const uint32 LastError = FPlatformMisc::GetLastError();
		Ar.Reset();
		Ifm.Delete(*TmpSnapshotFile);
		return MakeSnapshotError<int64>(ECasErrorCode::SaveSnapshotFailed, EIoErrorCode::WriteError, FString::Printf(TEXT("Failed to write snapshot to '%s'"), *SnapshotFile), LastError);
	}

	FFooter Footer;
	FMemory::Memcpy(&Footer.Magic, &FFooter::MagicSequence, sizeof(FFooter::MagicSequence));
	Ar->Serialize(reinterpret_cast<uint8*>(&Footer), FFooter::Size());
	if (Ar->IsError())
	{
		const uint32 LastError = FPlatformMisc::GetLastError();
		Ar.Reset();
		Ifm.Delete(*TmpSnapshotFile);
		return MakeSnapshotError<int64>(ECasErrorCode::SaveSnapshotFailed, EIoErrorCode::WriteError, FString::Printf(TEXT("Failed to write snapshot footer to '%s'"), *SnapshotFile), LastError);
	}

	const int64 FileSize = Ar->TotalSize();
	if (Ar->Close() == false)
	{
		const uint32 LastError = FPlatformMisc::GetLastError();
		Ar.Reset();
		Ifm.Delete(*TmpSnapshotFile);
		return MakeSnapshotError<int64>(ECasErrorCode::SaveSnapshotFailed, EIoErrorCode::WriteError, FString::Printf(TEXT("Failed to close snapshot footer to '%s'"), *SnapshotFile), LastError);
	}

	if (Ifm.Move(*SnapshotFile, *TmpSnapshotFile) == false)
	{
		const uint32 LastError = FPlatformMisc::GetLastError();
		Ifm.Delete(*TmpSnapshotFile);
		return MakeSnapshotError<int64>(
			ECasErrorCode::SaveSnapshotFailed,
			EIoErrorCode::FileMoveFailed,
			FString::Printf(TEXT("Failed to move snapshot '%s' -> '%s'"), *TmpSnapshotFile, *SnapshotFile), LastError);
	}

	return MakeValue(FileSize);
}

TResult<FCasSnapshot> FCasSnapshot::Load(const FString& SnapshotFile, int64* OutFileSize)
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	IFileManager& Ifm = IFileManager::Get();

	TUniquePtr<FArchive> Ar(Ifm.CreateFileReader(*SnapshotFile));
	if (Ar.IsValid() == false)
	{
		return MakeSnapshotError<FCasSnapshot>(ECasErrorCode::LoadSnapshotFailed, EIoErrorCode::FileOpenFailed, FString::Printf(TEXT("Failed open snapshot '%s'"), *SnapshotFile));
	}

	FHeader Header;
	Ar->Serialize(reinterpret_cast<uint8*>(&Header), FHeader::Size());
	if (Ar->IsError() || Header.IsValid() == false)
	{
		return MakeSnapshotError<FCasSnapshot>(ECasErrorCode::LoadSnapshotFailed, EIoErrorCode::ReadError, FString::Printf(TEXT("Failed to validate snapshot header in '%s'"), *SnapshotFile));
	}

	FCasSnapshot Snapshot;
	*Ar << Snapshot.Blocks;
	*Ar << Snapshot.ChunkLocations;
	*Ar << Snapshot.CurrentBlockId;

	FFooter Footer;
	Ar->Serialize(reinterpret_cast<uint8*>(&Footer), FFooter::Size());
	if (Ar->IsError() || Footer.IsValid() == false)
	{
		return MakeSnapshotError<FCasSnapshot>(ECasErrorCode::LoadSnapshotFailed, EIoErrorCode::ReadError, FString::Printf(TEXT("Failed to validate snapshot footer in '%s'"), *SnapshotFile));
	}

	if (OutFileSize != nullptr)
	{
		*OutFileSize = Ar->Tell();
	}

	return MakeValue(Snapshot);
}

TResult<int64> FCasSnapshot::TryCreateAndResetJournal(const FString& SnapshotFile, const FString& JournalFile)
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	IFileManager& Ifm = IFileManager::Get();

	const int64 JournalFileSize = Ifm.FileSize(*JournalFile);
	if (JournalFileSize < 0)
	{
		return MakeSnapshotError<int64>(ECasErrorCode::CreateSnapshotFailed, EIoErrorCode::NotFound, FString::Printf(TEXT("Failed to find snapshot '%s'"), *SnapshotFile));
	}

	// Load the snapshot from the journal
	TResult<FCasSnapshot> SnapshotResult = FCasSnapshot::FromJournal(JournalFile);
	if (SnapshotResult.HasError())
	{
		return MakeError(SnapshotResult.StealError());
	}

	// Save the snapshot
	int64 SnapshotSize			= -1;
	FCasSnapshot Snapshot		= SnapshotResult.StealValue();
	TResult<int64> SaveResult	= FCasSnapshot::Save(Snapshot, SnapshotFile);
	if (SaveResult.HasValue())
	{
		SnapshotSize = SaveResult.GetValue();
	}
	else
	{
		return SaveResult;
	}

	// Try create a new empty journal 
	const FString TmpJournalFile = FPaths::ChangeExtension(JournalFile, TEXT(".jrntmp"));
	if (FResult Result = FCasJournal::Create(TmpJournalFile); Result.HasError())
	{
		if (Ifm.Delete(*SnapshotFile) == false)
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to delete CAS snapshot '%s'"), *SnapshotFile); 
		}

		return MakeError(Result.StealError());
	}

	if (Ifm.Move(*JournalFile , *TmpJournalFile) == false)
	{
		const uint32 LastError = FPlatformMisc::GetLastError();
		if (Ifm.Delete(*SnapshotFile) == false)
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to delete CAS snapshot '%s'"), *SnapshotFile); 
		}

		return MakeSnapshotError<int64>(
			ECasErrorCode::CreateSnapshotFailed,
			EIoErrorCode::FileMoveFailed,
			FString::Printf(TEXT("Failed to move tmp journal file '%s' -> '%s'"), *TmpJournalFile, *JournalFile));
	}

	return MakeValue(SnapshotSize);
}

///////////////////////////////////////////////////////////////////////////////
void FCas::LoadSnapshot(FCasSnapshot&& Snapshot)
{
	TUniqueLock Lock(Mutex);

	Lookup.Reserve(Snapshot.ChunkLocations.Num());
	for (TPair<FCasAddr, FCasLocation>& Kv : Snapshot.ChunkLocations)
	{
		Lookup.Add(MoveTemp(Kv));
	}

	BlockIds.Reserve(Snapshot.Blocks.Num());
	LastAccess.Reserve(Snapshot.Blocks.Num());
	for (const FCasSnapshot::FBlock& Block : Snapshot.Blocks)
	{
		BlockIds.Add(Block.BlockId, 0);
		LastAccess.Add(Block.BlockId, Block.LastAccess);
	}
}

///////////////////////////////////////////////////////////////////////////////
class FOnDemandInstallCache final
	: public IOnDemandInstallCache 
{
	using FSharedBackendContextRef	= TSharedRef<const FIoDispatcherBackendContext>;
	using FSharedBackendContext		= TSharedPtr<const FIoDispatcherBackendContext>;

	struct FChunkRequest
	{
		explicit FChunkRequest(
			FSharedAsyncFileHandle FileHandle,
			FIoRequestImpl* Request,
			FOnDemandChunkInfo&& Info,
			FIoOffsetAndLength Range,
			uint64 RequestedRawSize)
				: SharedFileHandle(FileHandle)
				, DispatcherRequest(Request)
				, ChunkInfo(MoveTemp(Info))
				, ChunkRange(Range)
				, EncodedChunk(ChunkRange.GetLength())
				, RawSize(RequestedRawSize)
		{
			check(DispatcherRequest != nullptr);
			check(ChunkInfo.IsValid());
			check(Request->NextRequest == nullptr);
			check(Request->BackendData == nullptr);
		}

		static FChunkRequest* Get(FIoRequestImpl& Request)
		{
			return reinterpret_cast<FChunkRequest*>(Request.BackendData);
		}

		static FChunkRequest& GetRef(FIoRequestImpl& Request)
		{
			check(Request.BackendData);
			return *reinterpret_cast<FChunkRequest*>(Request.BackendData);
		}

		static FChunkRequest& Attach(FIoRequestImpl& Request, FChunkRequest* ChunkRequest)
		{
			check(Request.BackendData == nullptr);
			check(ChunkRequest != nullptr);
			Request.BackendData = ChunkRequest;
			return *ChunkRequest;
		}

		static TUniquePtr<FChunkRequest> Detach(FIoRequestImpl& Request)
		{
			void* ChunkRequest = nullptr;
			Swap(ChunkRequest, Request.BackendData);
			return TUniquePtr<FChunkRequest>(reinterpret_cast<FChunkRequest*>(ChunkRequest));
		}

		FSharedAsyncFileHandle			SharedFileHandle;
		TUniquePtr<IAsyncReadRequest>	FileReadRequest;
		FIoRequestImpl*					DispatcherRequest;
		FOnDemandChunkInfo				ChunkInfo;
		FIoOffsetAndLength				ChunkRange;
		FIoBuffer						EncodedChunk;
		uint64							RawSize;
	};

	struct FPendingChunks
	{
		static constexpr uint64 MaxPendingBytes = 4ull << 20;

		bool IsEmpty() const
		{
			check(Chunks.Num() == ChunkHashes.Num());
			return TotalSize == 0 && Chunks.IsEmpty() && ChunkHashes.IsEmpty();
		}

		void Append(FIoBuffer&& Chunk, const FIoHash& ChunkHash)
		{
			check(Chunks.Num() == ChunkHashes.Num());
			TotalSize += Chunk.GetSize();
			ChunkHashes.Add(ChunkHash);
			Chunks.Add(MoveTemp(Chunk));
		}

		void Reset()
		{
			Chunks.Reset();
			ChunkHashes.Reset();
			TotalSize = 0;
		}

		const FIoBuffer* FindChunk(const FIoHash& ChunkHash) const
		{
			const int32 PendingIdx = ChunkHashes.IndexOfByKey(ChunkHash);
			return (PendingIdx == INDEX_NONE) ? nullptr : &Chunks[PendingIdx];
		}

		bool ContainsChunk(const FIoHash& ChunkHash) const
		{
			return ChunkHashes.Contains(ChunkHash);
		}

		TArray<FIoBuffer>		Chunks;
		TArray<FIoHash>			ChunkHashes;
		uint64					TotalSize = 0;
		mutable FSharedMutex	SharedMutex;
	};

public:
	FOnDemandInstallCache(const FOnDemandInstallCacheConfig& Config, FOnDemandIoStore& IoStore, FDiskCacheGovernor& Governor);
	virtual ~FOnDemandInstallCache();

	// IIoDispatcherBackend
	virtual void								Initialize(FSharedBackendContextRef Context) override;
	virtual void								Shutdown() override;
	virtual void								ResolveIoRequests(FIoRequestList Requests, FIoRequestList& OutUnresolved) override;
	virtual FIoRequestImpl*						GetCompletedIoRequests() override;
	virtual void								CancelIoRequest(FIoRequestImpl* Request) override;
	virtual void								UpdatePriorityForIoRequest(FIoRequestImpl* Request) override;
	virtual bool								DoesChunkExist(const FIoChunkId& ChunkId) const override;
	virtual TIoStatusOr<uint64>					GetSizeForChunk(const FIoChunkId& ChunkId) const override;
	virtual TIoStatusOr<FIoMappedRegion>		OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options) override;
	virtual const TCHAR*						GetName() const override;

	// IOnDemandInstallCache
	virtual bool								IsChunkCached(const FIoHash& ChunkHash) override;
	virtual bool								TryPinChunks(
													const FSharedOnDemandContainer& Container,
													TConstArrayView<int32> EntryIndices,
													FOnDemandContentHandle ContentHandle,
													TArray<int32>& OutMissing) override;
	virtual FResult								PutChunk(FIoBuffer&& Chunk, const FIoHash& ChunkHash) override;
	virtual FResult								Purge(uint64 BytesToInstall) override;
	virtual FResult								PurgeAllUnreferenced(bool bDefrag, const uint64* BytesToPurge = nullptr) override;
	virtual FResult								DefragAll(const uint64* BytesToFree = nullptr) override;
	virtual FResult								Verify() override;
	virtual FResult								Flush() override;
	virtual FResult								FlushLastAccess() override;
	virtual void								UpdateLastAccess(TConstArrayView<FIoHash> ChunkHashes) override;
	virtual FOnDemandInstallCacheUsage			GetCacheUsage() override;

private:
	void										RegisterConsoleCommands();
	FResult										Reset();
	FResult										InitialVerify();
	void										AddReferencesToBlocks(
													const TArray<FSharedOnDemandContainer>& Containers, 
													const TArray<TBitArray<>>& ChunkEntryIndices,
													FCasBlockInfoMap& BlockInfoMap,
													uint64& OutTotalReferencedBytes) const;
	FResult										PurgeInternal(FCasBlockInfoMap& BlockInfo, uint64 TotalBytesToPurge, uint64& OutTotalPurgedBytes);
	FResult										Defrag(
													const TArray<FSharedOnDemandContainer>& Containers,
													const TArray<TBitArray<>>& ChunkEntryIndices,
													FCasBlockInfoMap& BlockInfo, 
													const uint64* TotalBytesToFree = nullptr,
													uint64* OutTotalFreedBytes = nullptr);
	bool										Resolve(FIoRequestImpl* Request);
	void										CompleteRequest(FIoRequestImpl* Request, EIoErrorCode Status);
	FResult										FlushPendingChunks(FPendingChunks& Chunks, int64 UtcAccessTicks = 0);
	FResult										FlushPendingChunksImpl(const FPendingChunks& Chunks, int64 UtcAccessTicks = 0);
	FString										GetJournalFilename() const { return CacheDirectory / TEXT("cas.jrn"); }
	FString										GetSnapshotFilename() const { return CacheDirectory / TEXT("cas.snp"); }
	UE::UnifiedError::IoStoreOnDemand::FInstallCacheErrorContext MakeInstallCacheErrorContext(uint64 TotalCachedBytes = 0, uint32 LineNo = __builtin_LINE());

	FOnDemandIoStore&			IoStore;
	FDiskCacheGovernor&			Governor;
	FString						CacheDirectory;
	FCas						Cas;
	std::atomic<FCasBlockId>	CurrentBlock{ FCasBlockId::Invalid };
	FPendingChunks				PendingChunks;
	FSharedBackendContext		BackendContext;
	FIoRequestList				CompletedRequests;
	FMutex						Mutex;
	FSharedMutex				PurgeDefragMutex;
	uint64						MaxCacheSize{ 0 };
	uint64						MaxJournalSize;

#if UE_ONDEMANDINSTALLCACHE_EXCLUSIVE_WRITE
	UE::Tasks::FPipe			ExclusivePipe{ UE_SOURCE_LOCATION };
#endif // UE_ONDEMANDINSTALLCACHE_EXCLUSIVE_WRITE

#if UE_IAD_DEBUG_CONSOLE_CMDS
	TArray<IConsoleCommand*> ConsoleCommands;
#endif // UE_IAD_DEBUG_CONSOLE_CMDS
};

///////////////////////////////////////////////////////////////////////////////
FOnDemandInstallCache::FOnDemandInstallCache(const FOnDemandInstallCacheConfig& Config, FOnDemandIoStore& InIoStore, FDiskCacheGovernor& InGovernor)
	: IoStore(InIoStore)
	, Governor(InGovernor)
	, CacheDirectory(Config.RootDirectory)
	, Cas(Config)
	, MaxCacheSize(Config.DiskQuota)
	, MaxJournalSize(Config.JournalMaxSize)
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Initializing install cache, MaxCacheSize=%.2lf MiB, MaxJournalSize=%.2lf KiB"),
		ToMiB(MaxCacheSize), ToKiB(MaxJournalSize));

	const uint64 MinDiskQuota = 2 * Cas.GetMaxBlockSize();
	if (MaxCacheSize < MinDiskQuota)
	{
		UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to initialize install cache - disk quota must be at least %.2lf MiB"), ToMiB(MinDiskQuota));
		return;
	}

	// Reserve one block of space for defragmentation overhead
	MaxCacheSize -= Cas.GetMaxBlockSize();
	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Effective MaxCacheSize without defragmentation space is MaxCacheSize=%.2lf MiB"), ToMiB(MaxCacheSize));

	FResult InitResult = Cas.Initialize(CacheDirectory);
	if (InitResult.HasError())
	{
		UE_LOGFMT(LogIoStoreOnDemand, Error, "Failed to initialize install cache, error: {Error}", InitResult.GetError());
		return;
	}

	// Try read the journal snapshot
	{
		const FString SnapshotFile = GetSnapshotFilename();
		int64 SnapshotSize = -1;
		TResult<FCasSnapshot> SnapshotResult = FCasSnapshot::Load(SnapshotFile, &SnapshotSize);
		if (SnapshotResult.HasValue())
		{
			FCasSnapshot Snapshot = SnapshotResult.StealValue();
			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Loaded CAS snapshot '%s' %.2lf KiB with %d blocks and %d chunk locations"),
					*SnapshotFile, ToKiB(SnapshotSize), Snapshot.Blocks.Num(), Snapshot.ChunkLocations.Num());

			Cas.LoadSnapshot(MoveTemp(Snapshot));
		}
		else if (const FCasErrorContext* Ctx = SnapshotResult.GetError().GetErrorContext<FCasErrorContext>())
		{
			// It's ok for the snapshot to not exist, other errors are significant
			if (Ctx->IoErrorCode == EIoErrorCode::NotFound)
			{
				InitResult = MakeValue();
			}
		}
	}

	const FString JournalFile = GetJournalFilename();
	if (InitResult.HasValue())
	{
		// Replay the journal 
		InitResult = FCasJournal::Replay(JournalFile, [this](const FCasJournal::FEntry& JournalEntry)
		{
			switch(JournalEntry.Type())
			{
			case FCasJournal::FEntry::EType::ChunkLocation:
			{
				const FCasJournal::FEntry::FChunkLocation& ChunkLocation = JournalEntry.ChunkLocation;
				if (ChunkLocation.CasLocation.IsValid())
				{
					FCasLocation& Loc = Cas.Lookup.FindOrAdd(ChunkLocation.CasAddr);
					Loc = ChunkLocation.CasLocation;
				}
				else
				{
					Cas.Lookup.Remove(ChunkLocation.CasAddr);
				}
				break;
			}
			case FCasJournal::FEntry::EType::BlockCreated:
			{
				const FCasJournal::FEntry::FBlockOperation& Op = JournalEntry.BlockOperation;
				CurrentBlock = Op.BlockId;
				Cas.BlockIds.Add(Op.BlockId, 0);
				break;
			}
			case FCasJournal::FEntry::EType::BlockDeleted:
			{
				const FCasJournal::FEntry::FBlockOperation& Op = JournalEntry.BlockOperation;
				Cas.BlockIds.Remove(Op.BlockId);
				FCasBlockId MaybeCurrentBlock = Op.BlockId;
				CurrentBlock.compare_exchange_strong(MaybeCurrentBlock, FCasBlockId::Invalid);
				break;
			}
			case FCasJournal::FEntry::EType::BlockAccess:
			{
				const FCasJournal::FEntry::FBlockOperation& Op = JournalEntry.BlockOperation;
				constexpr const bool bDirty = false;
				check(Cas.TrackAccessIf(ECasTrackAccessType::Always, Op.BlockId, Op.UtcTicks, bDirty));
				break;
			}
			};
		});
	}

	// If the CAS journal is not found we assume we are initializing the cache for the first time
	if (InitResult.HasError())
	{
		if (const FCasErrorContext* Ctx = InitResult.GetError().GetErrorContext<FCasErrorContext>())
		{
			if (Ctx->IoErrorCode == EIoErrorCode::NotFound)
			{
				if (InitResult = FCasJournal::Create(JournalFile); InitResult.HasValue())
				{
					UE_LOG(LogIoStoreOnDemand, Log, TEXT("Created CAS journal '%s'"), *JournalFile);

					// Make sure that there are no existing blocks when starting from an empty cache
					const bool bDeleteExisting = true;
					InitResult = Cas.Initialize(CacheDirectory, bDeleteExisting);
				}
			}
		}
	}

	// Verify the current state of the cache
	if (InitResult.HasValue())
	{
		InitResult = InitialVerify();
	}

	// Try to reset the cache if something has gone wrong 
	if (InitResult.HasError())
	{
		UE_LOGFMT(LogIoStoreOnDemand, Error, "Resetting install cache, reason: {Error}", InitResult.GetError());

		FOnDemandInstallCacheStats::OnStartupError(InitResult);
		InitResult = InitialVerify();
	}

	if (InitResult.HasValue())
	{
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Install cache Ok!"));
		RegisterConsoleCommands();
		Cas.Compact();
	}
	else
	{
		UE_LOGFMT(LogIoStoreOnDemand, Error, "Failed to initialize install cache, reason: {Error}", InitResult.GetError());
	}
}

FOnDemandInstallCache::~FOnDemandInstallCache()
{
}

void FOnDemandInstallCache::Initialize(FSharedBackendContextRef Context)
{
	BackendContext = Context;
}

void FOnDemandInstallCache::Shutdown()
{
	if (FResult Result = FlushPendingChunksImpl(PendingChunks); Result.HasError())
	{
		UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to flush pending chunks on shutdown, reason '%s'"), *LexToString(Result.GetError()));
	}

	FCas::FLastAccess LastAccess = Cas.ConsumeLastAcccess();

	const FString JournalFile = GetJournalFilename();
	FCasJournal::FTransaction Transaction = FCasJournal::Begin(JournalFile);
	JournalLastAccess(Transaction, LastAccess);

	if (FResult Result = FCasJournal::Commit(MoveTemp(Transaction)); Result.HasError())
	{
		UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to update CAS journal '%s' with block timestamp(s), error '%s'"),
			*JournalFile, *LexToString(Result.GetError()));
	}

	// TODO: journal snapshot is also only made on shutdown? Feels like this would be better done on startup as well
	IFileManager& Ifm = IFileManager::Get();
	const FString JournalFilename = GetJournalFilename();
	if (Ifm.FileSize(*JournalFile) > int64(MaxJournalSize))
	{
		const FString SnapshotFilename = GetSnapshotFilename();
		TResult<int64> SnapshotResult = FCasSnapshot::TryCreateAndResetJournal(SnapshotFilename, JournalFilename);
		if (SnapshotResult.HasValue())
		{
			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Saved CAS snapshot '%s' %.2lf KiB"), *SnapshotFilename, ToKiB(SnapshotResult.GetValue()));
		}
		else
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to create CAS snapshot from journal '%s', error '%s'"),
				*JournalFile, *LexToString(SnapshotResult.GetError()));
		}
	}

#if UE_IAD_DEBUG_CONSOLE_CMDS
	for (IConsoleCommand* Cmd : ConsoleCommands)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Cmd);
	}
#endif // UE_IAD_DEBUG_CONSOLE_CMDS
}

void FOnDemandInstallCache::ResolveIoRequests(FIoRequestList Requests, FIoRequestList& OutUnresolved)
{
	while (FIoRequestImpl* Request = Requests.PopHead())
	{
		if (Resolve(Request) == false)
		{
			OutUnresolved.AddTail(Request);
		}
	}
}

FIoRequestImpl* FOnDemandInstallCache::GetCompletedIoRequests()
{
	FIoRequestImpl* FirstCompleted = nullptr;
	{
		UE::TUniqueLock Lock(Mutex);
		for (FIoRequestImpl& Completed : CompletedRequests)
		{
			TUniquePtr<FChunkRequest> Detached = FChunkRequest::Detach(Completed);
		}
		FirstCompleted = CompletedRequests.GetHead();
		CompletedRequests = FIoRequestList();
	}

	return FirstCompleted;
}

void FOnDemandInstallCache::CancelIoRequest(FIoRequestImpl* Request)
{
	check(Request != nullptr);
	UE::TUniqueLock Lock(Mutex);
	if (FChunkRequest* ChunkRequest = FChunkRequest::Get(*Request))
	{
		if (ChunkRequest->FileReadRequest.IsValid())
		{
			ChunkRequest->FileReadRequest->Cancel();
		}
	}
}

void FOnDemandInstallCache::UpdatePriorityForIoRequest(FIoRequestImpl* Request)
{
}

bool FOnDemandInstallCache::DoesChunkExist(const FIoChunkId& ChunkId) const
{
	EIoErrorCode ErrorCode = EIoErrorCode::UnknownChunkID;
	if (FOnDemandChunkInfo ChunkInfo = IoStore.GetInstalledChunkInfo(ChunkId, ErrorCode))
	{
		{
			TSharedLock SharedLock(PendingChunks.SharedMutex);

			if (PendingChunks.ContainsChunk(ChunkInfo.Hash()))
			{
				return true;
			}
		}

		const FCasLocation CasLoc = Cas.FindChunk(ChunkInfo.Hash());
		return CasLoc.IsValid();
	}

	return false;
}

TIoStatusOr<uint64> FOnDemandInstallCache::GetSizeForChunk(const FIoChunkId& ChunkId) const
{
	EIoErrorCode ErrorCode = EIoErrorCode::UnknownChunkID;
	if (FOnDemandChunkInfo ChunkInfo = IoStore.GetInstalledChunkInfo(ChunkId, ErrorCode))
	{
		return ChunkInfo.RawSize();
	}

	return FIoStatus(ErrorCode);
}

TIoStatusOr<FIoMappedRegion> FOnDemandInstallCache::OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options)
{
	return FIoStatus(EIoErrorCode::FileOpenFailed, TEXT("On-demand install cache does not support memory mapped files."));
}

const TCHAR* FOnDemandInstallCache::GetName() const
{
	return TEXT("OnDemandInstallCache");
}

bool FOnDemandInstallCache::Resolve(FIoRequestImpl* Request)
{
	EIoErrorCode ErrorCode = EIoErrorCode::UnknownChunkID;
	FOnDemandChunkInfo ChunkInfo = IoStore.GetInstalledChunkInfo(Request->ChunkId, ErrorCode);
	if (ChunkInfo.IsValid() == false)
	{
		if (ErrorCode == EIoErrorCode::NotInstalled)
		{
			CompleteRequest(Request, EIoErrorCode::NotInstalled);
			return true;
		}
		return false;
	}

	const uint64 RequestSize = FMath::Min<uint64>(
		Request->Options.GetSize(),
		ChunkInfo.RawSize() - Request->Options.GetOffset());

	TIoStatusOr<FIoOffsetAndLength> ChunkRange = FIoChunkEncoding::GetChunkRange(
		ChunkInfo.RawSize(),
		ChunkInfo.BlockSize(),
		ChunkInfo.Blocks(),
		Request->Options.GetOffset(),
		RequestSize);

	if (ChunkRange.IsOk() == false)
	{
		UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to get chunk range"));
		CompleteRequest(Request, ChunkRange.Status().GetErrorCode());
		return true;
	}

	// Must check pending chunks before CAS because otherwise
	// a chunk could be flushed after checking the CAS and before checking pending.
	// Otherise we'd need take both locks.
	{
		TDynamicSharedLock SharedLock(PendingChunks.SharedMutex);

		const FIoBuffer* PendingChunk = PendingChunks.FindChunk(ChunkInfo.Hash());
		if (PendingChunk != nullptr)
		{
			FChunkRequest& ChunkRequest = FChunkRequest::Attach(*Request, new FChunkRequest(
				FSharedAsyncFileHandle(),
				Request,
				MoveTemp(ChunkInfo),
				ChunkRange.ConsumeValueOrDie(),
				RequestSize));

			FMemoryView BytesToRead = PendingChunk->GetView().Mid(ChunkRequest.ChunkRange.GetOffset(), ChunkRequest.EncodedChunk.GetSize());
			check(BytesToRead.GetSize() == ChunkRequest.EncodedChunk.GetSize());
			FMemory::Memcpy(ChunkRequest.EncodedChunk.GetData(), BytesToRead.GetData(), ChunkRequest.EncodedChunk.GetSize());

			SharedLock.Unlock();

			UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, Request]
			{
				CompleteRequest(Request, EIoErrorCode::Ok);
			});

			return true;
		}
	}

	const FCasLocation CasLoc = Cas.FindChunk(ChunkInfo.Hash());
	if (CasLoc.IsValid() == false)
	{
		CompleteRequest(Request, EIoErrorCode::NotInstalled);
		return true;
	}

	TRACE_IOSTORE_BACKEND_REQUEST_STARTED(Request, this);
	constexpr const bool bDirty = true;
	if (Cas.TrackAccessIf(ECasTrackAccessType::Granular, CasLoc.BlockId, bDirty))
	{
		IoStore.FlushLastAccess(nullptr);
	}

#if UE_ONDEMANDINSTALLCACHE_EXCLUSIVE_WRITE
	const bool bIsLocationInCurrentBlock = CasLoc.BlockId == CurrentBlock;
	if (bIsLocationInCurrentBlock)
	{
		// The current block may have open writes which may cause async reads to fail
		// on some platforms. Schedule the reads to happen on the same pipe as writes

		// The internal request parameters are attached/owned by the I/O request via
		// the backend data parameter. The chunk request is deleted in GetCompletedRequests
		FChunkRequest::Attach(*Request, new FChunkRequest(
			FSharedAsyncFileHandle(),
			Request,
			MoveTemp(ChunkInfo),
			ChunkRange.ConsumeValueOrDie(),
			RequestSize));

		ExclusivePipe.Launch(UE_SOURCE_LOCATION, [this, Request, CasLoc]
		{
			FChunkRequest& ChunkRequest = FChunkRequest::GetRef(*Request);
			EIoErrorCode Status = EIoErrorCode::FileOpenFailed;

			const FString Filename = Cas.GetBlockFilename(CasLoc.BlockId);

			TResult<FSharedFileHandle> FileOpenResult = Cas.OpenRead(CasLoc.BlockId);
			if (FileOpenResult.HasValue())
			{
				Status = EIoErrorCode::ReadError;

				TSharedPtr<IFileHandle> FileHandle = FileOpenResult.StealValue();
				const int64 CasBlockOffset = CasLoc.BlockOffset + ChunkRequest.ChunkRange.GetOffset();
				if (Request->IsCancelled())
				{
					UE_LOG(LogIoStoreOnDemand, Verbose, TEXT("Cancelled request - skipped seek to offset %" INT64_FMT " in CAS block '%s'"), CasBlockOffset, *Filename);
				}
				else if (FileHandle->Seek(CasBlockOffset))
				{
					const bool bOk = FileHandle->Read(ChunkRequest.EncodedChunk.GetData(), ChunkRequest.EncodedChunk.GetSize());
					if (bOk)
					{
						Status = EIoErrorCode::Ok;
					}
					else
					{
						UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to read %" UINT64_FMT " bytes at offset %" INT64_FMT " in CAS block '%s'"),
							ChunkRequest.EncodedChunk.GetSize(),
							CasBlockOffset,
							*Filename);
					}
				}
				else
				{
					UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to seek to offset %" INT64_FMT " in CAS block '%s'"), CasBlockOffset, *Filename);
				}
			}
			else
			{
				UE_LOGFMT(LogIoStoreOnDemand, Error, "Failed to open CAS block '{Filename}' for reading ({Error})", *Filename, FileOpenResult.GetError());
			}

			UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, Request, Status]
			{
				CompleteRequest(Request, Status);
			});
		}, UE::Tasks::ETaskPriority::BackgroundHigh);

		return true;
	}
#endif // UE_ONDEMANDINSTALLCACHE_EXCLUSIVE_WRITE

	FSharedFileOpenAsyncResult FileOpenResult = Cas.OpenAsyncRead(CasLoc.BlockId);
	if (FileOpenResult.HasError())
	{
		const FString Filename = Cas.GetBlockFilename(CasLoc.BlockId);
		UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to open CAS block '%s' for async reading, reason '%s'"), *Filename, *FileOpenResult.GetError().GetMessage());
		TUniquePtr<FChunkRequest> Detached = FChunkRequest::Detach(*Request);
		FOnDemandInstallCacheStats::OnReadCompleted(EIoErrorCode::FileOpenFailed);
		CompleteRequest(Request, EIoErrorCode::FileOpenFailed);
		return true;
	}
	
	FSharedAsyncFileHandle FileHandle(FileOpenResult.StealValue());

	// The internal request parameters are attached/owned by the I/O request via
	// the backend data parameter. The chunk request is deleted in GetCompletedRequests
	FChunkRequest& ChunkRequest = FChunkRequest::Attach(*Request, new FChunkRequest(
		FileHandle,
		Request,
		MoveTemp(ChunkInfo),
		ChunkRange.ConsumeValueOrDie(),
		RequestSize));

	FAsyncFileCallBack Callback = [this, Request](bool bWasCancelled, IAsyncReadRequest* ReadRequest)
	{
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [this, Request, bWasCancelled]
		{
			const EIoErrorCode Status = bWasCancelled ? EIoErrorCode::ReadError : EIoErrorCode::Ok;
			CompleteRequest(Request, Status);
		});
	};

	ChunkRequest.FileReadRequest.Reset(FileHandle->ReadRequest(
		CasLoc.BlockOffset + ChunkRequest.ChunkRange.GetOffset(),
		ChunkRequest.ChunkRange.GetLength(),
		EAsyncIOPriorityAndFlags::AIOP_BelowNormal,
		&Callback,
		ChunkRequest.EncodedChunk.GetData()));

	if (ChunkRequest.FileReadRequest.IsValid() == false)
	{
		TRACE_IOSTORE_BACKEND_REQUEST_FAILED(Request);
		TUniquePtr<FChunkRequest> Detached = FChunkRequest::Detach(*Request);
		CompleteRequest(Request, EIoErrorCode::ReadError);
		return true;
	}

	return true;
}

bool FOnDemandInstallCache::IsChunkCached(const FIoHash& ChunkHash)
{
	{
		TSharedLock SharedLock(PendingChunks.SharedMutex);

		if (PendingChunks.ContainsChunk(ChunkHash))
		{
			return true;
		}
	}

	const FCasLocation Loc = Cas.FindChunk(ChunkHash);
	return Loc.IsValid();
}

bool FOnDemandInstallCache::TryPinChunks(
	const FSharedOnDemandContainer& Container,
	TConstArrayView<int32> EntryIndices,
	FOnDemandContentHandle ContentHandle,
	TArray<int32>& OutMissing)
{
	TDynamicSharedLock SharedLock(PurgeDefragMutex, UE::DeferLock);

	// Currently we can only pin chunks if we are not purging/defragging the cache 
	if (SharedLock.TryLock() == false)
	{
		OutMissing = EntryIndices;
		return false;
	}

	for (int32 EntryIndex : EntryIndices)
	{
		const FOnDemandChunkEntry& ChunkEntry = Container->ChunkEntries[EntryIndex];

		{
			TSharedLock PendingSharedLock(PendingChunks.SharedMutex);

			if (PendingChunks.ContainsChunk(ChunkEntry.Hash))
			{
				IoStore.AddReference(Container, EntryIndex, ContentHandle);
				continue;
			}
		}

		if (FCasLocation Loc = Cas.FindChunk(ChunkEntry.Hash); Loc.IsValid())
		{
			IoStore.AddReference(Container, EntryIndex, ContentHandle);
		}
		else
		{
			OutMissing.Add(EntryIndex);
		}
	}

	return true;
}

FResult FOnDemandInstallCache::PutChunk(FIoBuffer&& Chunk, const FIoHash& ChunkHash)
{
	if (PendingChunks.TotalSize > FPendingChunks::MaxPendingBytes)
	{
		if (FResult Result = FlushPendingChunks(PendingChunks); Result.HasError())
		{
			return Result;
		}
		check(PendingChunks.IsEmpty());
	}

	TUniqueLock Lock(PendingChunks.SharedMutex);

	PendingChunks.Append(MoveTemp(Chunk), ChunkHash);
	return MakeValue();
}

FResult FOnDemandInstallCache::Purge(const uint64 BytesToInstall)
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	TUniqueLock Lock(PurgeDefragMutex);

	FCasBlockInfoMap	BlockInfo;
	const uint64		TotalCachedBytes = Cas.GetBlockInfo(BlockInfo);

	TArray<FSharedOnDemandContainer>	Containers;
	TArray<TBitArray<>>					ChunkEntryIndices;

	IoStore.GetReferencedContent(Containers, ChunkEntryIndices);
	check(Containers.Num() == ChunkEntryIndices.Num());

	uint64 ReferencedBytes = 0;
	uint64 FragmentedBytes = 0;
	uint64 TotalReferencedBlockBytes = 0;
	int64 OldestBlockAccess = FDateTime::MaxValue().GetTicks();

	AddReferencesToBlocks(Containers, ChunkEntryIndices, BlockInfo, ReferencedBytes);
	for (const TPair<FCasBlockId, FCasBlockInfo>& Kv : BlockInfo)
	{
		const FCasBlockInfo& Info = Kv.Value;

		if (Info.FileSize > Cas.GetMaxBlockSize())
		{
			UE_LOGFMT(LogIoStoreOnDemand, Error, "CAS Block {BlockId} has total size {BlockSize} which is greater than max block size {MaxBlockSize}", Kv.Key.Id, Info.FileSize, Cas.GetMaxBlockSize());
			ensure(false);
		}

		if (Info.RefSize < Info.FileSize)
		{
			FragmentedBytes += (Info.FileSize - Info.RefSize);
		}
		else if (Info.RefSize > Info.FileSize)
		{
			UE_LOGFMT(LogIoStoreOnDemand, Error, "CAS Block {BlockId} has RefSize {RefSize} which is greater than total size {BlockSize}", Kv.Key.Id, Info.RefSize, Info.FileSize);
			ensure(false);
		}

		if (Info.RefSize > 0)
		{
			TotalReferencedBlockBytes += Info.FileSize;
		}

		if (Info.LastAccess < OldestBlockAccess)
		{
			OldestBlockAccess = Info.LastAccess;
		}
	}

	FOnDemandInstallCacheStats::OnCacheUsage(
		MaxCacheSize, TotalCachedBytes, TotalReferencedBlockBytes, ReferencedBytes, FragmentedBytes, OldestBlockAccess);

	const uint64 TotalUncachedBytes = BytesToInstall + PendingChunks.TotalSize;
	const uint64 TotalRequiredBytes = TotalCachedBytes + TotalUncachedBytes;
	if (TotalRequiredBytes <= MaxCacheSize)
	{
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Skipping cache purge, MaxCacheSize=%.2lf MiB, CacheSize=%.2lf MiB, ReferencedBlockSize=%.2lf MiB, ReferencedSize=%.2lf MiB, FragmentedBytes=%.2lf MiB, UncachedSize=%.2lf MiB"),
			ToMiB(MaxCacheSize), ToMiB(TotalCachedBytes), ToMiB(TotalReferencedBlockBytes), ToMiB(ReferencedBytes), ToMiB(FragmentedBytes), ToMiB(TotalUncachedBytes));
		return MakeValue();
	}

	//TODO: Compute fragmentation metric and redownload chunks when this number gets too high

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Purging install cache, MaxCacheSize=%.2lf MiB, CacheSize=%.2lf MiB, ReferencedBlockSize=%.2lf MiB, ReferencedSize=%.2lf MiB, FragmentedBytes=%.2lf MiB, UncachedSize=%.2lf MiB"),
		ToMiB(MaxCacheSize), ToMiB(TotalCachedBytes), ToMiB(TotalReferencedBlockBytes), ToMiB(ReferencedBytes), ToMiB(FragmentedBytes), ToMiB(TotalUncachedBytes));

	const uint64	TotalBytesToPurge	= TotalRequiredBytes - MaxCacheSize;
	uint64			TotalPurgedBytes	= 0;
	FResult			Result				= PurgeInternal(BlockInfo, TotalBytesToPurge, TotalPurgedBytes);

	if (TotalPurgedBytes > 0)
	{
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Purged %.2lf MiB (%.2lf%%) from install cache"),
			ToMiB(TotalPurgedBytes), 100.0 * (double(TotalPurgedBytes) / double(TotalCachedBytes)));
	}

	const uint64 NewCachedBytes = TotalCachedBytes - TotalPurgedBytes;
	UE_CLOG(NewCachedBytes > MaxCacheSize,
		LogIoStoreOnDemand, Warning, TEXT("Max install cache size exceeded by %.2lf MiB (%.2lf%%)"),
			ToMiB(NewCachedBytes - MaxCacheSize), 100.0 * (double(NewCachedBytes - MaxCacheSize) / double(MaxCacheSize)));

	uint64 DefragPurgedBytes = 0;
	if (Result.HasError() == false && TotalPurgedBytes < TotalBytesToPurge)
	{
		if (UE::IoStore::CVars::GIoStoreOnDemandEnableDefrag)
		{
			// Attempt to defrag
			const uint64 DefragBytesToPurge = TotalBytesToPurge - TotalPurgedBytes;
			Result = Defrag(Containers, ChunkEntryIndices, BlockInfo, &DefragBytesToPurge, &DefragPurgedBytes);
		}
		else
		{
			FInstallCacheErrorContext Ctx = MakeInstallCacheErrorContext(TotalCachedBytes);
			Ctx.IoError = FIoStatus(EIoErrorCode::InvalidCode, TEXT("Failed to purge required size from install cache"));
			Result = MakeError(InstallCachePurgeError::MakeError(MoveTemp(Ctx), UE::UnifiedError::EDetailFilter::All));
		}
	}

	FOnDemandInstallCacheStats::OnPurge(Result, MaxCacheSize, NewCachedBytes, TotalBytesToPurge, TotalPurgedBytes + DefragPurgedBytes);
	return Result;
}

FResult FOnDemandInstallCache::PurgeAllUnreferenced(bool bDefrag, const uint64* BytesToPurge /*= nullptr*/)
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	TUniqueLock Lock(PurgeDefragMutex);

	FCasBlockInfoMap	BlockInfo;
	const uint64		TotalCachedBytes = Cas.GetBlockInfo(BlockInfo);

	TArray<FSharedOnDemandContainer>	Containers;
	TArray<TBitArray<>>					ChunkEntryIndices;

	IoStore.GetReferencedContent(Containers, ChunkEntryIndices);
	check(Containers.Num() == ChunkEntryIndices.Num());

	uint64 ReferencedBytes = 0;
	AddReferencesToBlocks(Containers, ChunkEntryIndices, BlockInfo, ReferencedBytes);

	const uint64 TotalReferencedBytes = Algo::TransformAccumulate(BlockInfo,
		[](const TPair<FCasBlockId, FCasBlockInfo>& Kv) { return (Kv.Value.RefSize > 0) ? Kv.Value.FileSize : uint64(0); },
		uint64(0));

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Purging install cache, MaxCacheSize=%.2lf MiB, CacheSize=%.2lf MiB, ReferencedBytes=%.2lf MiB"),
		ToMiB(MaxCacheSize), ToMiB(TotalCachedBytes), ToMiB(TotalReferencedBytes));

	const uint64	TotalBytesToPurge	= BytesToPurge ? *BytesToPurge : MaxCacheSize;
	uint64			TotalPurgedBytes	= 0;
	FResult			Result				= PurgeInternal(BlockInfo, TotalBytesToPurge, TotalPurgedBytes);

	if (TotalPurgedBytes > 0)
	{
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Purged %.2lf MiB (%.2lf%%) from install cache"),
			ToMiB(TotalPurgedBytes), 100.0 * (double(TotalPurgedBytes) / double(TotalCachedBytes)));
	}

	const uint64 NewCachedBytes = TotalCachedBytes - TotalPurgedBytes;
	UE_CLOG(NewCachedBytes > MaxCacheSize,
		LogIoStoreOnDemand, Warning, TEXT("Max install cache size exceeded by %.2lf MiB (%.2lf%%)"),
			ToMiB(NewCachedBytes - MaxCacheSize), 100.0 * (double(NewCachedBytes - MaxCacheSize) / double(MaxCacheSize)));

	if (BytesToPurge)
	{
		if (bDefrag)
		{
			// Attempt to defrag
			const uint64 DefragBytesToPurge = TotalBytesToPurge - TotalPurgedBytes;
			Result = Defrag(Containers, ChunkEntryIndices, BlockInfo, &DefragBytesToPurge);
		}
		else
		{
			FInstallCacheErrorContext Ctx = MakeInstallCacheErrorContext();
			Result = MakeError(InstallCachePurgeError::MakeError(MoveTemp(Ctx), UE::UnifiedError::EDetailFilter::All));
		}
	}
	else if (bDefrag)
	{
		Result = Defrag(Containers, ChunkEntryIndices, BlockInfo);
	}

	FOnDemandInstallCacheStats::OnPurge(Result, MaxCacheSize, NewCachedBytes, TotalBytesToPurge, TotalPurgedBytes);
	return Result;
}

FResult FOnDemandInstallCache::DefragAll(const uint64* BytesToFree /*= nullptr*/)
{
	TUniqueLock Lock(PurgeDefragMutex);

	FCasBlockInfoMap	BlockInfo;
	const uint64		TotalCachedBytes = Cas.GetBlockInfo(BlockInfo);

	TArray<FSharedOnDemandContainer>	Containers;
	TArray<TBitArray<>>					ChunkEntryIndices;

	IoStore.GetReferencedContent(Containers, ChunkEntryIndices);
	check(Containers.Num() == ChunkEntryIndices.Num());

	uint64 ReferencedBytes = 0;
	AddReferencesToBlocks(Containers, ChunkEntryIndices, BlockInfo, ReferencedBytes);

	const uint64 TotalReferencedBlockBytes = Algo::TransformAccumulate(BlockInfo,
		[](const TPair<FCasBlockId, FCasBlockInfo>& Kv) { return (Kv.Value.RefSize > 0) ? Kv.Value.FileSize : uint64(0); },
		uint64(0));

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Defragmenting install cache, MaxCacheSize=%.2lf MiB, CacheSize=%.2lf MiB, ReferencedBlockSize=%.2lf MiB, ReferencedSize=%.2lf MiB"),
		ToMiB(MaxCacheSize), ToMiB(TotalCachedBytes), ToMiB(TotalReferencedBlockBytes), ToMiB(ReferencedBytes));

	return Defrag(Containers, ChunkEntryIndices, BlockInfo, BytesToFree);
}

FResult FOnDemandInstallCache::Verify()
{
	struct FChunkLookup
	{
		TMap<FCasAddr, int32> AddrToIndex;
	};

	struct FCasAddrLocation
	{
		FCasAddr		Addr;
		FCasLocation	Location;

		bool operator<(const FCasAddrLocation& Other) const
		{
			if (Location.BlockId == Other.Location.BlockId)
			{
				return Location.BlockOffset < Other.Location.BlockOffset;
			}
			return Location.BlockId.Id < Other.Location.BlockId.Id;
		}
	};

	TArray<FSharedOnDemandContainer>	Containers = IoStore.GetContainers(EOnDemandContainerFlags::InstallOnDemand);
	TArray<FCasAddrLocation>			ChunkLocations;
	TArray<FChunkLookup>				ChunkLookups;

	{
		TUniqueLock Lock(Cas);
		ChunkLocations.Reserve(Cas.Lookup.Num());
		for (const TPair<FCasAddr, FCasLocation>& Kv : Cas.Lookup)
		{
			ChunkLocations.Add(FCasAddrLocation
			{
				.Addr		= Kv.Key,
				.Location	= Kv.Value
			});
		}
	}
	ChunkLocations.Sort();

	ChunkLookups.Reserve(Containers.Num());
	for (int32 Idx = 0; Idx < Containers.Num(); ++Idx)
	{
		FSharedOnDemandContainer& Container = Containers[Idx];
		FChunkLookup& Lookup				= ChunkLookups.AddDefaulted_GetRef();

		Lookup.AddrToIndex.Reserve(Container->ChunkEntries.Num());
		for (int32 EntryIndex = 0; const FOnDemandChunkEntry& Entry : Container->ChunkEntries)
		{
			const FCasAddr& Addr = AsCasAddr(Entry.Hash);
			Lookup.AddrToIndex.Add(Addr, EntryIndex++);
		}
	}

	auto FindChunkEntry = [&Containers, &ChunkLookups](const FCasAddr& Addr, int32& OutContainerIndex) -> int32
	{
		OutContainerIndex = INDEX_NONE;
		for (int32 Idx = 0; Idx < Containers.Num(); ++Idx)
		{
			FChunkLookup& Lookup = ChunkLookups[Idx];
			if (const int32* EntryIndex = Lookup.AddrToIndex.Find(Addr))
			{
				OutContainerIndex = Idx;
				return *EntryIndex;
			}
		}

		return INDEX_NONE;
	};

	const int32	ChunkCount			= ChunkLocations.Num();
	uint32		CorruptChunkCount	= 0;
	uint32		MissingChunkCount	= 0;
	uint32		ReadErrorCount		= 0;
	uint64		TotalVerifiedBytes	= 0;
	FIoBuffer	Chunk(1 << 20);

	if (ChunkCount == 0)
	{
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Verify skipped, install cache is empty"));
		return MakeValue();
	}

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Verifying %d installed chunks..."), ChunkCount);
	for (int32 ChunkIndex = 0; const FCasAddrLocation& ChunkLocation : ChunkLocations)
	{
		TResult<FSharedFileHandle> OpenResult = Cas.OpenRead(ChunkLocation.Location.BlockId);
		if (OpenResult.HasError())
		{
			UE_LOGFMT(LogIoStoreOnDemand, Error, "Failed to open block {BlockId} for reading ({Error})", ChunkLocation.Location.BlockId.Id, OpenResult.GetError());

			ReadErrorCount++;
			ChunkIndex++;
			continue;
		}

		int32 ContainerIndex	= INDEX_NONE;
		int32 EntryIndex		= FindChunkEntry(ChunkLocation.Addr, ContainerIndex);

		if (EntryIndex == INDEX_NONE)
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to find chunk entry for CAS address '%s'"), *LexToString(ChunkLocation.Addr));

			MissingChunkCount++;
			ChunkIndex++;
			continue;
		}

		const FSharedOnDemandContainer& Container	= Containers[ContainerIndex];
		const FIoChunkId& ChunkId					= Container->ChunkIds[EntryIndex];
		const FOnDemandChunkEntry& ChunkEntry		= Container->ChunkEntries[EntryIndex];
		FSharedFileHandle FileHandle				= OpenResult.GetValue();
		const int64 ChunkSize						= ChunkEntry.GetDiskSize();
		TotalVerifiedBytes							+= ChunkSize;

		if (int64(Chunk.GetSize()) < ChunkSize)
		{
			Chunk = FIoBuffer(ChunkSize);
		}

		if (FileHandle->Seek(int64(ChunkLocation.Location.BlockOffset)) == false)
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Chunk %d/%d SEEK FAILED, Container='%s', ChunkId='%s', ChunkSize=%" INT64_FMT ", Hash='%s', Block=%u, BlockOffset=%u"),
				ChunkIndex + 1, ChunkCount, *Container->Name, *LexToString(ChunkId), ChunkSize, *LexToString(ChunkEntry.Hash),
				ChunkLocation.Location.BlockId.Id, ChunkLocation.Location.BlockOffset);

			ReadErrorCount++;
			ChunkIndex++;
			continue;
		}

		if (FileHandle->Read(reinterpret_cast<uint8*>(Chunk.GetData()), ChunkSize) == false)
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Chunk %d/%d READ FAILED, Container='%s', ChunkId='%s', ChunkSize=%" INT64_FMT ", Hash='%s', Block=%u, BlockOffset=%u"),
				ChunkIndex + 1, ChunkCount, *Container->Name, *LexToString(ChunkId), ChunkSize, *LexToString(ChunkEntry.Hash),
				ChunkLocation.Location.BlockId.Id, ChunkLocation.Location.BlockOffset);

			ReadErrorCount++;
			ChunkIndex++;
			continue;
		}

		const FIoHash ChunkHash = FIoHash::HashBuffer(Chunk.GetView().Left(ChunkSize));

		if (ChunkHash == ChunkEntry.Hash)
		{
			UE_LOG(LogIoStoreOnDemand, VeryVerbose, TEXT("Chunk %d/%d OK, Container='%s', ChunkId='%s', ChunkSize=%" INT64_FMT ", Hash='%s', Block=%u, BlockOffset=%u"),
				ChunkIndex + 1, ChunkCount, *Container->Name, *LexToString(ChunkId), ChunkSize, *LexToString(ChunkEntry.Hash),
				ChunkLocation.Location.BlockId.Id, ChunkLocation.Location.BlockOffset);
		}
		else
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Chunk %d/%d CORRUPT, Container='%s', ChunkId='%s', ChunkSize=%" INT64_FMT ", Hash='%s', ActualHash='%s', Block=%u, BlockOffset=%u"),
				ChunkIndex + 1, ChunkCount, *Container->Name, *LexToString(ChunkId), ChunkSize, *LexToString(ChunkEntry.Hash), *LexToString(ChunkHash),
				ChunkLocation.Location.BlockId.Id, ChunkLocation.Location.BlockOffset);

			CorruptChunkCount++;
		}

		ChunkIndex++;
	}

	if (CorruptChunkCount > 0 || MissingChunkCount > 0 || ReadErrorCount > 0)
	{
		const FString Reason = FString::Printf(TEXT("Verify install cache failed, Corrupt=%u, Missing=%u, ReadErrors=%u"),
			CorruptChunkCount, MissingChunkCount, ReadErrorCount);

		if (CorruptChunkCount > 0 || ReadErrorCount > 0)
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("%s"), *Reason);
		}
		else
		{
			UE_LOG(LogIoStoreOnDemand, Warning, TEXT("%s"), *Reason);
		}

		return MakeError(UE::UnifiedError::IoStoreOnDemand::InstallCacheVerificationError::MakeError(
			UE::UnifiedError::IoStoreOnDemand::FVerificationErrorContext
			{
				.CorruptChunkCount	= CorruptChunkCount,
				.MissingChunkCount	= MissingChunkCount,
				.ReadErrorCount		= ReadErrorCount
			}));
	}

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Successfully verified %d chunk(s) of total %.2lf MiB"),
		ChunkCount, ToMiB(TotalVerifiedBytes));

	return MakeValue();
}

void FOnDemandInstallCache::RegisterConsoleCommands()
{
#if UE_IAD_DEBUG_CONSOLE_CMDS
	ConsoleCommands.Emplace(
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("iostore.SimulateCriticalInstallCacheError"),
		TEXT(""),
		FConsoleCommandDelegate::CreateLambda([this]()
		{
			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Simulating critical install cache error"));

			FCasJournal::FTransaction Transaction = FCasJournal::Begin(GetJournalFilename());
			Transaction.CriticalError(FCasJournal::EErrorCode::Simulated);
			if (FResult Result = FCasJournal::Commit(MoveTemp(Transaction)); Result.HasError())
			{
				UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Failed to append critical error to journal, error '%s'"),
					*LexToString(Result.GetError()));
			}
		}),
		ECVF_Default)
	);
#endif // UE_IAD_DEBUG_CONSOLE_CMDS
}

FResult FOnDemandInstallCache::Reset()
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Resetting install cache in directory '%s'"), *CacheDirectory);

	IFileManager& Ifm	= IFileManager::Get();
	const bool bTree	= true;

	if (Ifm.DeleteDirectory(*CacheDirectory, false, bTree) == false)
	{
		return MakeCasError<void>(
			ECasErrorCode::InitializeFailed, EIoErrorCode::DeleteError, FString::Printf(TEXT("Failed to delete cache directory '%s'"), *CacheDirectory));
	}

	if (Ifm.MakeDirectory(*CacheDirectory, bTree) == false)
	{
		return MakeCasError<void>(
			ECasErrorCode::InitializeFailed, EIoErrorCode::WriteError, FString::Printf(TEXT("Failed to create cache directory '%s'"), *CacheDirectory));
	}

	if (FResult Result = Cas.Initialize(CacheDirectory); Result.HasError())
	{
		return Result;
	}

	const FString JournalFile = GetJournalFilename();
	if (FResult Result = FCasJournal::Create(JournalFile); Result.HasError())
	{
		return Result;
	}

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Created CAS journal '%s'"), *JournalFile);
	return MakeValue();
}

FResult FOnDemandInstallCache::InitialVerify()
{
	// Verify the blocks on disk with the current state of the CAS
	{
		TArray<FCasAddr> RemovedChunks;
		if (FResult Verify = Cas.Verify(RemovedChunks); Verify.HasError())
		{
			FOnDemandInstallCacheStats::OnCasVerificationError(RemovedChunks.Num());

			// Remove all entries that doesn't have a valid cache block 
			FCasJournal::FTransaction Transaction = FCasJournal::Begin(GetJournalFilename());
			for (const FCasAddr& Addr : RemovedChunks)
			{
				Transaction.ChunkLocation(FCasLocation::Invalid, Addr);
			}

			if (FResult Result = FCasJournal::Commit(MoveTemp(Transaction)); Result.HasError())
			{
				return Result;
			}
		}
	}

	// Check if the cache is over budget
	{
		FCasBlockInfoMap	BlockInfo;
		uint64				CacheSize = Cas.GetBlockInfo(BlockInfo);

		if (CacheSize > MaxCacheSize)
		{
			const uint64	TotalBytesToPurge = CacheSize - MaxCacheSize;
			uint64			TotalPurgedBytes = 0;

			UE_LOG(LogIoStoreOnDemand, Warning, TEXT("Cache size is greater than disk quota - Purging install cache, MaxCacheSize=%.2lf MiB, TotalSize=%.2lf MiB, TotalBytesToPurge=%.2lf MiB"),
				ToMiB(MaxCacheSize), ToMiB(CacheSize), ToMiB(TotalBytesToPurge));

			if (FResult Result = PurgeInternal(BlockInfo, TotalBytesToPurge, TotalPurgedBytes); Result.HasError())
			{
				const FString ErrorMessage = FString::Printf(TEXT("Failed to purge %.2lf MiB from install cache reason '%s'"),
					ToMiB(TotalBytesToPurge), *LexToString(Result.GetError()));

				UE_LOG(LogIoStoreOnDemand, Error, TEXT("%s"), *ErrorMessage);
				return Result;
			}

			if (TotalPurgedBytes < TotalBytesToPurge)
			{
				// This should never happen since we don't have any referenced cache blocks at startup
				const FString ErrorMessage = FString::Printf(TEXT("Failed to purge %.2lf MiB from install cache. Actually purged %.2lf MiB from install cache"),
					ToMiB(TotalBytesToPurge), ToMiB(TotalPurgedBytes));

				UE_LOG(LogIoStoreOnDemand, Error, TEXT("%s"), *ErrorMessage);
				return MakeError(UE::UnifiedError::IoStoreOnDemand::InstallCachePurgeError::MakeError());
			}

			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Successfully purged %.2lf MiB from install cache"), ToMiB(TotalPurgedBytes));
		}
	}

	return MakeValue();
}

void FOnDemandInstallCache::AddReferencesToBlocks(
	const TArray<FSharedOnDemandContainer>& Containers, 
	const TArray<TBitArray<>>& ChunkEntryIndices, 
	FCasBlockInfoMap& BlockInfoMap, 
	uint64& OutTotalReferencedBytes) const
{
	OutTotalReferencedBytes = 0;

	// TODO: this could use the CAS address to be smaller
	TSet<FIoHash> VisitedReferencedChunks;
	{
		int32 ReserveSize = 0;
		for (int32 Index = 0; FSharedOnDemandContainer Container : Containers)
		{
			const TBitArray<>& IsReferenced = ChunkEntryIndices[Index++];
			ReserveSize += IsReferenced.CountSetBits();
		}

		VisitedReferencedChunks.Reserve(ReserveSize);
	}

	for (int32 Index = 0; FSharedOnDemandContainer Container : Containers)
	{
		const TBitArray<>& IsReferenced = ChunkEntryIndices[Index++];
		for (int32 EntryIndex = 0; const FOnDemandChunkEntry& Entry : Container->ChunkEntries)
		{
			const bool bIsReferenced = IsReferenced[EntryIndex];
			if (!bIsReferenced)
			{
				EntryIndex++;
				continue;
			}

			bool bAlreadyVisited = false;
			VisitedReferencedChunks.Add(Entry.Hash, &bAlreadyVisited);
			if (bAlreadyVisited)
			{
				EntryIndex++;
				continue;
			}

			const uint64 ChunkDiskSize = Entry.GetDiskSize();
			OutTotalReferencedBytes += ChunkDiskSize;

			if (FCasLocation Loc = Cas.FindChunk(Entry.Hash); Loc.IsValid())
			{
				FCasBlockInfo* BlockInfo = BlockInfoMap.Find(Loc.BlockId);
				if (!BlockInfo)
				{
					UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to find CAS block info for referenced chunk, ChunkId='%s', Container='%s'"),
						*LexToString(Container->ChunkIds[EntryIndex]), *Container->Name);
				}
				else
				{
					BlockInfo->RefSize += ChunkDiskSize;
				}
			}
			else
			{
				// Check pending list
				bool bChunkIsPending = false;
				{
					TSharedLock SharedLock(PendingChunks.SharedMutex);
					bChunkIsPending = PendingChunks.ContainsChunk(Entry.Hash);
				}

				UE_CLOG(!bChunkIsPending, LogIoStoreOnDemand, Error, TEXT("Failed to find CAS location or pending chunk for chunk reference, ChunkId='%s', Container='%s'"),
					*LexToString(Container->ChunkIds[EntryIndex]), *Container->Name);

				ensure(bChunkIsPending);
			}

			EntryIndex++;
		}
	}

	return;
}

FResult FOnDemandInstallCache::PurgeInternal(FCasBlockInfoMap& BlockInfo, const uint64 TotalBytesToPurge, uint64& OutTotalPurgedBytes)
{
	BlockInfo.ValueSort([](const FCasBlockInfo& LHS, const FCasBlockInfo& RHS)
	{
		return LHS.LastAccess < RHS.LastAccess;
	});

	OutTotalPurgedBytes = 0;

	for (auto It = BlockInfo.CreateIterator(); It; ++It)
	{
		const FCasBlockId BlockId = It->Key;
		const FCasBlockInfo& Info = It->Value;
		if (Info.RefSize > 0)
		{
			continue;
		}

		FCasJournal::FTransaction	Transaction = FCasJournal::Begin(GetJournalFilename());
		TArray<FCasAddr>			RemovedChunks;

		if (FResult Result = Cas.DeleteBlock(BlockId, RemovedChunks); Result.HasError())
		{
			return Result;
		}

		FOnDemandInstallCacheStats::OnBlockDeleted(Info.LastAccess, false /*FromDefrag*/);

		// This should be the only thread writing to CurrentBlock
		FCasBlockId MaybeCurrentBlock = BlockId;
		CurrentBlock.compare_exchange_strong(MaybeCurrentBlock, FCasBlockId::Invalid);

		OutTotalPurgedBytes += Info.FileSize;

		It.RemoveCurrent();

		for (const FCasAddr& Addr : RemovedChunks)
		{
			Transaction.ChunkLocation(FCasLocation::Invalid, Addr);
		}
		Transaction.BlockDeleted(BlockId);

		if (FResult Result = FCasJournal::Commit(MoveTemp(Transaction)); Result.HasError())
		{
			return Result;
		}
		
		if (OutTotalPurgedBytes >= TotalBytesToPurge)
		{
			break;
		}
	}

	return MakeValue();
}

FResult FOnDemandInstallCache::Defrag(
	const TArray<FSharedOnDemandContainer>& Containers,
	const TArray<TBitArray<>>& ChunkEntryIndices,
	FCasBlockInfoMap& BlockInfo, 
	const uint64* TotalBytesToFree /*= nullptr*/,
	uint64* OutTotalFreedBytes /*= nullptr*/)
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	if (TotalBytesToFree && *TotalBytesToFree == 0)
	{
		return MakeValue();
	}

	FResult Result			= MakeValue();
	uint64 FragmentedBytes	= 0;

	ON_SCOPE_EXIT
	{
		if (OutTotalFreedBytes != nullptr)
		{
			*OutTotalFreedBytes = Result.HasError() ? 0 : FragmentedBytes;
		}
		FOnDemandInstallCacheStats::OnDefrag(Result, FragmentedBytes);
	};

	const uint64 TotalCachedBytes = Algo::TransformAccumulate(BlockInfo,
		[](const TPair<FCasBlockId, FCasBlockInfo>& Kv) { return Kv.Value.FileSize; },
		uint64(0));

	if (TotalCachedBytes > MaxCacheSize)
	{
		// Ruh-Roh! There's not enough of the disk quota left to run a defrag!
		const FString ErrorMsg = FString::Printf(TEXT("Cache size is greater than disk quota - Cannot Defragment!, MaxCacheSize=%.2lf MiB"), 
			ToMiB(MaxCacheSize));
		UE_LOG(LogIoStoreOnDemand, Error, TEXT("%s"), *ErrorMsg);

		FInstallCacheErrorContext Ctx = MakeInstallCacheErrorContext(TotalCachedBytes);
		Ctx.IoError = FIoStatus(EIoErrorCode::OutOfDiskSpace, ErrorMsg),
		Result = MakeError(InstallCacheDefragError::MakeError(MoveTemp(Ctx), UE::UnifiedError::EDetailFilter::All));

		// Append a critical error entry to clear the cache at next startup
		FCasJournal::FTransaction Transaction = FCasJournal::Begin(GetJournalFilename());
		Transaction.CriticalError(FCasJournal::EErrorCode::DefragOutOfDiskSpace);
		if (FResult CommitResult = FCasJournal::Commit(MoveTemp(Transaction)); CommitResult.HasError())
		{
			UE_LOGFMT(LogIoStoreOnDemand, Error, "Failed to commit critical errors to CAS journal ({Error})", CommitResult.GetError());
		}

		return Result;
	}

	struct FDefragBlockReferencedChunk
	{
		FIoHash		Hash;
		FIoChunkId	ChunkId; 
		uint32		BlockOffset = 0;
		uint32		DiskSize = 0;
	};

	struct FDefragBlock
	{
		FCasBlockId BlockId;
		int64 LastAccess = 0;
		TArray<FDefragBlockReferencedChunk> ReferencedChunks;
	};

	// Build the list of blocks to defrag and determine if its possible to free enough data through defragging
	TArray<FDefragBlock> BlocksToDefrag;
	
	// Start with the least referenced blocks
	BlockInfo.ValueSort([](const FCasBlockInfo& LHS, const FCasBlockInfo& RHS)
	{
		return LHS.RefSize < RHS.RefSize;
	});

	uint64 TotalBlockSize = 0;

	if (TotalBytesToFree)
	{
		// Partial defrag
		bool bPossibleToFreeBytes = false;

		uint64 FreedBlockBytes = 0;
		uint64 NewBlockBytes = 0;

		for (const TPair<FCasBlockId, FCasBlockInfo>& Kv : BlockInfo)
		{
			const FCasBlockId BlockId = Kv.Key;
			const FCasBlockInfo& Info = Kv.Value;

			if (!bPossibleToFreeBytes && Info.RefSize < Info.FileSize)
			{
				// Block is fragmented
				FragmentedBytes += (Info.FileSize - Info.RefSize);
				TotalBlockSize += Info.FileSize;

				FreedBlockBytes += Info.FileSize;
				NewBlockBytes += Info.RefSize; // For now, assume that nothing will be moved to the current block

				BlocksToDefrag.Add(FDefragBlock{ .BlockId = BlockId, .LastAccess = Info.LastAccess });

				if (FreedBlockBytes >= NewBlockBytes && FreedBlockBytes - NewBlockBytes >= *TotalBytesToFree)
				{
					bPossibleToFreeBytes = true;
				}
			}
			else if (Info.FileSize < Cas.GetMinBlockSize())
			{
				// Block is too small whether or not its fragmented
				if (ensure(Info.RefSize <= Info.FileSize))
				{
					FragmentedBytes += (Info.FileSize - Info.RefSize);
				}

				TotalBlockSize += Info.FileSize;

				BlocksToDefrag.Add(FDefragBlock{ .BlockId = BlockId, .LastAccess = Info.LastAccess });
			}
		}

		if (!bPossibleToFreeBytes)
		{
			FString ErrorMessage = TEXT("Failed to defrag the install cache due to all data being referenced by the game");
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("%s"), *ErrorMessage);
			FInstallCacheErrorContext Ctx = MakeInstallCacheErrorContext(TotalCachedBytes);
			Ctx.IoError = FIoStatus(EIoErrorCode::OutOfDiskSpace, ErrorMessage);
			return Result = MakeError(InstallCacheDefragError::MakeError(MoveTemp(Ctx), UE::UnifiedError::EDetailFilter::All));
		}
	}
	else
	{
		// Full defrag
		for (const TPair<FCasBlockId, FCasBlockInfo>& Kv : BlockInfo)
		{
			const FCasBlockId BlockId = Kv.Key;
			const FCasBlockInfo& Info = Kv.Value;

			if (Info.RefSize < Info.FileSize)
			{
				// Block is fragmented
				FragmentedBytes += (Info.FileSize - Info.RefSize);
				TotalBlockSize += Info.FileSize;

				BlocksToDefrag.Add(FDefragBlock{ .BlockId = BlockId, .LastAccess = Info.LastAccess });
			}
			else if (Info.FileSize < Cas.GetMinBlockSize())
			{
				// Block is too small whether or not its fragmented
				if (ensure(Info.RefSize <= Info.FileSize))
				{
					FragmentedBytes += (Info.FileSize - Info.RefSize);
				}

				TotalBlockSize += Info.FileSize;

				BlocksToDefrag.Add(FDefragBlock{ .BlockId = BlockId, .LastAccess = Info.LastAccess });
			}
		}

		if (BlocksToDefrag.IsEmpty())
		{
			// Already defragged
			UE_LOG(LogIoStoreOnDemand, Display, TEXT("Cache not fragmented."));
			return Result = MakeValue();
		}
	}

	UE_LOG(LogIoStoreOnDemand, Display, TEXT("Defrag found %" UINT64_FMT " fragmented bytes of %" UINT64_FMT " total bytes in %i blocks."), FragmentedBytes, TotalBlockSize, BlocksToDefrag.Num());

	// Right now, don't allow moving chunks to the current block for defrag. Its somewhat dangerous and hard to reason about.
	// - Currently, the slack in the current block cannot be determined without opening a write handle to the block.
	// - If we defrag the current block itself, then we would need additional tracking so we don't lose any chunks moved into it.
	// - Additionally, this would also depend on the order blocks are defragged.
	// This should be the only thread writing to CurrentBlock.
	CurrentBlock = FCasBlockId::Invalid;

	uint64 TotalRefBytes = 0;

	// Determine chunks that need to be moved for each defrag block
	for (int32 Index = 0; FSharedOnDemandContainer Container : Containers)
	{
		const TBitArray<>& IsReferenced = ChunkEntryIndices[Index++];
		for (int32 EntryIndex = 0; const FOnDemandChunkEntry& Entry : Container->ChunkEntries)
		{
			const FIoChunkId& ChunkId = Container->ChunkIds[EntryIndex];
			if (bool bIsReferenced = IsReferenced[EntryIndex++]; bIsReferenced == false)
			{
				continue;
			}

			if (FCasLocation Loc = Cas.FindChunk(Entry.Hash); Loc.IsValid())
			{
				if (FDefragBlock* DefragBlock = Algo::FindBy(BlocksToDefrag, Loc.BlockId, &FDefragBlock::BlockId))
				{
					// TODO: Should this be a map?
					if (nullptr == Algo::FindBy(DefragBlock->ReferencedChunks, Entry.Hash, &FDefragBlockReferencedChunk::Hash))
					{
						DefragBlock->ReferencedChunks.Add(FDefragBlockReferencedChunk
						{
							.Hash = Entry.Hash,
							.ChunkId = ChunkId,  // May not be unique, just return the first found for debugging purposes
							.BlockOffset = Loc.BlockOffset,
							.DiskSize = Entry.GetDiskSize(),
						});
						TotalRefBytes += Entry.GetDiskSize();
					}
				}
			}
		}
	}

	if (TotalBlockSize - FragmentedBytes != TotalRefBytes)
	{
		UE_LOGFMT(LogIoStoreOnDemand, Error, "Possibly corrupt CAS blocks - TotalBlockSize {TotalBlockSize}, FragmentedBytes {FragmentedBytes}, TotalRefBytes {TotalRefBytes}", TotalBlockSize,  FragmentedBytes, TotalRefBytes);
		ensure(false);
	}

	// Move chunks to new blocks and delete old blocks
	FPendingChunks DefragPendingChunks;
	for (const FDefragBlock& DefragBlock : BlocksToDefrag)
	{
		if (DefragBlock.ReferencedChunks.IsEmpty() == false)
		{
			TResult<FSharedFileHandle> FileOpenResult = Cas.OpenRead(DefragBlock.BlockId);
			if (FileOpenResult.HasError())
			{
				UE_LOGFMT(LogIoStoreOnDemand, Error, "Defrag failed to open CAS block for reading {Error}", FileOpenResult.GetError());

				FInstallCacheErrorContext Ctx = MakeInstallCacheErrorContext(TotalCachedBytes);
				FileOpenResult.GetError().PushErrorContext(MoveTemp(Ctx));
				return Result = MakeError(FileOpenResult.StealError());
			}

			FSharedFileHandle FileHandle = FileOpenResult.StealValue();

			Algo::SortBy(DefragBlock.ReferencedChunks, &FDefragBlockReferencedChunk::BlockOffset);

			for (const FDefragBlockReferencedChunk& ReffedChunk : DefragBlock.ReferencedChunks)
			{
				FIoBuffer	Buffer(ReffedChunk.DiskSize);
				FResult		ReadResult = MakeValue();

				for (int32 Attempt = 0, MaxAttempts = 3; Attempt < MaxAttempts; ++Attempt)
				{
					if (FileHandle->Seek(ReffedChunk.BlockOffset) == false)
					{
						ReadResult = MakeCasError<void>(
							ECasErrorCode::ReadBlockFailed, EIoErrorCode::FileSeekFailed, FString::Printf(TEXT("Failed to seek to offset %u in cache block %u"),
								ReffedChunk.BlockOffset, DefragBlock.BlockId.Id));
						continue;
					}
					if (FileHandle->Read(Buffer.GetData(), Buffer.GetSize()) == false)
					{
						ReadResult = MakeCasError<void>(
							ECasErrorCode::ReadBlockFailed, EIoErrorCode::ReadError, FString::Printf(TEXT("Failed to read %" UINT64_FMT " bytes at offset %u in cache block %u"),
								Buffer.GetSize(), ReffedChunk.BlockOffset, DefragBlock.BlockId.Id));
						continue;
					}

					ReadResult = MakeValue();
					break;
				}

				if (ReadResult.HasError())
				{
					FInstallCacheErrorContext Ctx = MakeInstallCacheErrorContext(TotalCachedBytes);
					ReadResult.GetError().PushErrorContext(MoveTemp(Ctx));
					return Result = MakeError(ReadResult.StealError());
				}

				const FIoHash ChunkHash = FIoHash::HashBuffer(Buffer.GetView());
				if (ChunkHash != ReffedChunk.Hash)
				{
					UE_LOG(LogIoStoreOnDemand, Error, TEXT("Found chunk with invalid hash while defragging block, ChunkId='%s', BlockId=%u, BlockOffset=%u"),
						*LexToString(ReffedChunk.ChunkId), DefragBlock.BlockId.Id, ReffedChunk.BlockOffset);

					// Append a critical error entry to clear the cache at next startup
					FCasJournal::FTransaction Transaction = FCasJournal::Begin(GetJournalFilename());
					Transaction.CriticalError(FCasJournal::EErrorCode::DefragHashMismatch);
					if (FResult CommitResult = FCasJournal::Commit(MoveTemp(Transaction)); CommitResult.HasError())
					{
						UE_LOGFMT(LogIoStoreOnDemand, Error, "Failed to commit critical errors to CAS journal ({Error})", CommitResult.GetError());
					}

					if (Result = FlushPendingChunks(DefragPendingChunks, DefragBlock.LastAccess); Result.HasError())
					{
						return Result;
					}
					check(DefragPendingChunks.IsEmpty());
	
					return Result = MakeError(InstallCacheDefragError::MakeError(
						FChunkHashMismatchErrorContext
						{
							.ChunkId		= ReffedChunk.ChunkId, // May not be unique, just return the first found for debugging purposes
							.ExpectedHash	= ReffedChunk.Hash,
							.ActualHash		= ChunkHash
						}));
				}

				if (DefragPendingChunks.TotalSize > FPendingChunks::MaxPendingBytes)
				{
					if (Result = FlushPendingChunks(DefragPendingChunks, DefragBlock.LastAccess); Result.HasError())
					{
						return Result;
					}
					check(DefragPendingChunks.IsEmpty());
				}

				DefragPendingChunks.Append(MoveTemp(Buffer), ReffedChunk.Hash);
			}

			FileHandle.Reset();

			if (Result = FlushPendingChunks(DefragPendingChunks, DefragBlock.LastAccess); Result.HasError()) 
			{
				return Result;
			}
			check(DefragPendingChunks.IsEmpty());
		}

		FCasJournal::FTransaction Transaction = FCasJournal::Begin(GetJournalFilename());

		// Flushing should overwrite the lookup info for the cas addr to point at the new block.
		// Can now remove the old block
		TArray<FCasAddr> DeletedChunkAddresses;
		if (Result = Cas.DeleteBlock(DefragBlock.BlockId, DeletedChunkAddresses); Result.HasError())
		{
			return Result;
		}

		FOnDemandInstallCacheStats::OnBlockDeleted(DefragBlock.LastAccess, true /*FromDefrag*/);

		for (const FCasAddr& Addr : DeletedChunkAddresses)
		{
			Transaction.ChunkLocation(FCasLocation::Invalid, Addr);
		}
		Transaction.BlockDeleted(DefragBlock.BlockId);

		if (Result = FCasJournal::Commit(MoveTemp(Transaction)); Result.HasError())
		{
			return Result;
		}
	}

	UE_LOG(LogIoStoreOnDemand, Display, TEXT("Defrag removed %" UINT64_FMT " fragmented bytes of %" UINT64_FMT " total bytes in %i blocks."), FragmentedBytes, TotalBlockSize, BlocksToDefrag.Num());

	return Result;
}

FResult FOnDemandInstallCache::Flush()
{
	if (FResult Result = FlushPendingChunks(PendingChunks); Result.HasError())
	{
		return Result;
	}
	check(PendingChunks.IsEmpty());

	Cas.Compact();
	return MakeValue();
}

FResult FOnDemandInstallCache::FlushLastAccess()
{
	const FCas::FLastAccess DirtyLastAccess = Cas.GetAndClearDirtyLastAccess();

	if (DirtyLastAccess.IsEmpty())
	{
		return MakeValue();
	}

	const FString JournalFile = GetJournalFilename();
	FCasJournal::FTransaction Transaction = FCasJournal::Begin(JournalFile);
	JournalLastAccess(Transaction, DirtyLastAccess);

	if (FResult Result = FCasJournal::Commit(MoveTemp(Transaction)); Result.HasError())
	{
		UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to update CAS journal '%s' with block timestamp(s), reason '%s'"),
			*JournalFile, *LexToString(Result.GetError()));
		Result.GetError().PushErrorContext(MakeInstallCacheErrorContext());
		return Result;
	}

	return MakeValue();
}

void FOnDemandInstallCache::UpdateLastAccess(TConstArrayView<FIoHash> ChunkHashes)
{
	bool bLastAccessDirty = false;
	{
		const int64 Now = FDateTime::UtcNow().GetTicks();
		constexpr const bool bDirty = true;

		TUniqueLock Lock(Cas);

		for (const FIoHash& ChunkHash : ChunkHashes)
		{
			const FCasAddr& Addr = AsCasAddr(ChunkHash);
			if (FCasLocation* CasLoc = Cas.Lookup.Find(Addr))
			{
				const FCasBlockId BlockId = CasLoc->BlockId;
				const uint32 BlockIdHash = GetTypeHash(BlockId);
				const bool bUpdatedLastAccess = Cas.UnlockedTrackAccessIf(
					ECasTrackAccessType::Granular, BlockIdHash, BlockId, Now, bDirty);
				bLastAccessDirty = bLastAccessDirty || bUpdatedLastAccess;
			}
		}
	}

	if (bLastAccessDirty)
	{
		IoStore.FlushLastAccess(nullptr);
	}
}

FOnDemandInstallCacheUsage FOnDemandInstallCache::GetCacheUsage()
{
	// If this is called from a thread other than the OnDemandIoStore tick thread
	// then its possible the block info and containers may not be in sync with each other
	// or the current state of the tick thread.
	// This should only be used for debugging and telemetry purposes.

	FCasBlockInfoMap	BlockInfo;
	const uint64		TotalCachedBytes = Cas.GetBlockInfo(BlockInfo);

	TArray<FSharedOnDemandContainer>	Containers;
	TArray<TBitArray<>>					ChunkEntryIndices;
	IoStore.GetReferencedContent(Containers, ChunkEntryIndices);
	check(Containers.Num() == ChunkEntryIndices.Num());

	uint64 ReferencedBytes = 0;
	AddReferencesToBlocks(Containers, ChunkEntryIndices, BlockInfo, ReferencedBytes);

	uint64 FragmentedBytes = 0;
	uint64 ReferencedBlockBytes = 0;
	for (const TPair<FCasBlockId, FCasBlockInfo>& Kv : BlockInfo)
	{
		const FCasBlockId BlockId = Kv.Key;
		const FCasBlockInfo& Info = Kv.Value;

		if (Info.RefSize < Info.FileSize)
		{
			FragmentedBytes += (Info.FileSize - Info.RefSize);
		}

		if (Info.RefSize > 0)
		{
			ReferencedBlockBytes += Info.FileSize;
		}
	}

	return FOnDemandInstallCacheUsage
	{
		.MaxSize = MaxCacheSize,
		.TotalSize = TotalCachedBytes,
		.ReferencedBlockSize = ReferencedBlockBytes,
		.ReferencedSize = ReferencedBytes,
		.FragmentedChunksSize = FragmentedBytes,
	};
}

FResult FOnDemandInstallCache::FlushPendingChunks(FPendingChunks& Chunks, int64 UtcAccessTicks)
{
	if (Chunks.IsEmpty())
	{
		return MakeValue();
	}

#if UE_ONDEMANDINSTALLCACHE_EXCLUSIVE_WRITE
	UE::Tasks::TTask<FResult> Task = ExclusivePipe.Launch(UE_SOURCE_LOCATION, [this, &Chunks, UtcAccessTicks]
	{
		ON_SCOPE_EXIT 
		{ 
			TUniqueLock Lock(Chunks.SharedMutex);
			Chunks.Reset();
		};

		return FlushPendingChunksImpl(Chunks, UtcAccessTicks);
	}, UE::Tasks::ETaskPriority::BackgroundHigh);

	Task.Wait();

	return Task.GetResult();

#else
	ON_SCOPE_EXIT
	{
		TUniqueLock Lock(Chunks.SharedMutex);
		Chunks.Reset();
	};

	return FlushPendingChunksImpl(Chunks, UtcAccessTicks);
#endif // UE_ONDEMANDINSTALLCACHE_EXCLUSIVE_WRITE
}

FResult FOnDemandInstallCache::FlushPendingChunksImpl(const FPendingChunks& Chunks, int64 UtcAccessTicks)
{
	using namespace UE::UnifiedError::IoStoreOnDemand;

	FResult Result				= MakeValue();
	uint64 TotalCasBytes		= 0;
	uint64 TotalJournalBytes	= 0;
	uint32 TotalOpCount			= 0;

	ON_SCOPE_EXIT 
	{ 
		FOnDemandInstallCacheStats::OnFlush(Result, TotalCasBytes);
		Governor.OnInstallCacheFlushed(TotalCasBytes + TotalJournalBytes, TotalOpCount);
	};

	FLargeMemoryWriter Ar(FMath::Min(Chunks.TotalSize, static_cast<uint64>(Cas.GetMaxBlockSize())));

	// This should be the only thread writing to CurrentBlock
	FCasBlockId CurrentBlockId = CurrentBlock;

	int32 ChunkIdx = 0;

	while (ChunkIdx < Chunks.Chunks.Num())
	{
		FCasJournal::FTransaction Transaction = FCasJournal::Begin(GetJournalFilename());
		
		// Only open for append if continuing a block.
		const bool bAppendToBlock = CurrentBlockId.IsValid();

		if (CurrentBlockId.IsValid() == false)
		{
			CurrentBlockId = Cas.CreateBlock();
			ensure(CurrentBlockId.IsValid());
			CurrentBlock = CurrentBlockId;
			Transaction.BlockCreated(CurrentBlockId);
		}

		TResult<FUniqueFileHandle> OpenWriteResult = Cas.OpenWrite(CurrentBlockId, bAppendToBlock);
		if (OpenWriteResult.HasError())
		{
			FInstallCacheErrorContext Ctx = MakeInstallCacheErrorContext();
			OpenWriteResult.GetError().PushErrorContext(MoveTemp(Ctx));
			return Result = MakeError(OpenWriteResult.StealError());
		}
		
		FUniqueFileHandle CasFileHandle = OpenWriteResult.StealValue();
		const int64 CasBlockOffset = CasFileHandle->Tell();
		
		TArray<int64>		Offsets;
		const FIoHash*		ChunkHashBegin = &Chunks.ChunkHashes[ChunkIdx];

		while (ChunkIdx < Chunks.Chunks.Num())
		{
			if (CasBlockOffset > 0 && CasBlockOffset + Ar.Tell() + Chunks.Chunks[ChunkIdx].GetSize() > Cas.GetMaxBlockSize())
			{
				break;
			}

			const FIoBuffer& Chunk = Chunks.Chunks[ChunkIdx];
			Offsets.Add(CasBlockOffset + Ar.Tell());
			Ar.Serialize(const_cast<uint8*>(Chunk.GetData()), Chunk.GetSize());

			++ChunkIdx;
		}

		TConstArrayView<FIoHash> ChunkHashes = MakeArrayView(ChunkHashBegin, Offsets.Num());
		const int64 BytesToWrite = Ar.Tell();

		if (BytesToWrite > 0)
		{
			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Writing %.2lf MiB to CAS block %u"),
				ToMiB(Ar.Tell()), CurrentBlockId.Id);

			check(CasBlockOffset + BytesToWrite <= Cas.GetMaxBlockSize());

			if (CasFileHandle->Write(Ar.GetData(), BytesToWrite) == false)
			{
				Result = MakeCasError<void>(
					ECasErrorCode::WriteBlockFailed, EIoErrorCode::WriteError, FString::Printf(TEXT("Failed to write %" INT64_FMT " bytes to CAS block %u"), BytesToWrite, CurrentBlockId.Id));
				FInstallCacheErrorContext Ctx = MakeInstallCacheErrorContext();
				Result.GetError().PushErrorContext(MoveTemp(Ctx));
				return Result;
			}
			TotalOpCount++;
			TotalCasBytes += uint64(BytesToWrite);

			if (CasFileHandle->Flush() == false)
			{
				Result = MakeCasError<void>(
					ECasErrorCode::WriteBlockFailed, EIoErrorCode::FileFlushFailed, FString::Printf(TEXT("Failed to flush %" INT64_FMT " bytes to CAS block %u"), BytesToWrite, CurrentBlockId.Id));
				FInstallCacheErrorContext Ctx = MakeInstallCacheErrorContext();
				Result.GetError().PushErrorContext(MoveTemp(Ctx));
				return Result;
			}
			TotalOpCount++;

			constexpr const bool bDirty = false;
			if (UtcAccessTicks)
			{
				if (Cas.TrackAccessIf(ECasTrackAccessType::Newer, CurrentBlockId, UtcAccessTicks, bDirty))
				{
					Transaction.BlockAccess(CurrentBlockId, UtcAccessTicks);
				}
			}
			else
			{
				const int64 Now = FDateTime::UtcNow().GetTicks();
				check(Cas.TrackAccessIf(ECasTrackAccessType::Always, CurrentBlockId, Now, bDirty));
				Transaction.BlockAccess(CurrentBlockId, Now);
			}

			check(ChunkHashes.Num() == Offsets.Num());
			check(CurrentBlockId.IsValid());
			{
				TUniqueLock Lock(Cas);
				for (int32 Idx = 0, Count = Offsets.Num(); Idx < Count; ++Idx)
				{
					const FCasAddr	CasAddr = FCasAddr::From(ChunkHashes[Idx]);
					const uint32	ChunkOffset = IntCastChecked<uint32>(Offsets[Idx]);

					FCasLocation& Loc = Cas.Lookup.FindOrAdd(CasAddr);
					Loc.BlockId	= CurrentBlockId;
					Loc.BlockOffset	= ChunkOffset;
					Transaction.ChunkLocation(Loc, CasAddr);
				}
			}

			Ar.Seek(0);
		}

		uint64 JrnlBytesWritten	= 0;
		uint32 JrnlOps			= 0;
		if (Result = FCasJournal::Commit(MoveTemp(Transaction), JrnlBytesWritten, JrnlOps); Result.HasError())
		{
			FInstallCacheErrorContext Ctx = MakeInstallCacheErrorContext();
			Result.GetError().PushErrorContext(MoveTemp(Ctx));
			return Result;
		}

		TotalJournalBytes	+= JrnlBytesWritten;
		TotalOpCount		+= JrnlOps;

		if (ChunkIdx < Chunks.Chunks.Num())
		{
			CurrentBlockId = FCasBlockId::Invalid;
		}
	}

	return Result;
}

void FOnDemandInstallCache::CompleteRequest(FIoRequestImpl* Request, EIoErrorCode Status)
{
	if (Status == EIoErrorCode::Ok && !Request->IsCancelled())
	{
		FChunkRequest& ChunkRequest = FChunkRequest::GetRef(*Request);
		const FOnDemandChunkInfo& ChunkInfo = ChunkRequest.ChunkInfo;
		FIoBuffer EncodedChunk = MoveTemp(ChunkRequest.EncodedChunk);

		if (EncodedChunk.GetSize() > 0)
		{
			FIoChunkDecodingParams Params;
			Params.CompressionFormat = ChunkInfo.CompressionFormat();
			Params.EncryptionKey = ChunkInfo.EncryptionKey();
			Params.BlockSize = ChunkInfo.BlockSize();
			Params.TotalRawSize = ChunkInfo.RawSize();
			Params.RawOffset = Request->Options.GetOffset();
			Params.EncodedOffset = ChunkRequest.ChunkRange.GetOffset();
			Params.EncodedBlockSize = ChunkInfo.Blocks();
			Params.BlockHash = ChunkInfo.BlockHashes();

			Request->CreateBuffer(ChunkRequest.RawSize);
			FMutableMemoryView RawChunk = Request->GetBuffer().GetMutableView();

			if (FIoChunkEncoding::Decode(Params, EncodedChunk.GetView(), RawChunk) == false)
			{
				UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to decode chunk, ChunkId='%s'"), *LexToString(Request->ChunkId));
				Status = EIoErrorCode::CompressionError;
			}
		}
	}

	if (Status != EIoErrorCode::Ok)
	{
		Request->SetLastBackendError(Status);
		Request->SetResult(FIoBuffer());
		TRACE_IOSTORE_BACKEND_REQUEST_FAILED(Request);
	}
	else
	{
		TRACE_IOSTORE_BACKEND_REQUEST_COMPLETED(Request, Request->GetBuffer().GetSize());
	}

	{
		UE::TUniqueLock Lock(Mutex);
		CompletedRequests.AddTail(Request);
		FOnDemandInstallCacheStats::OnReadCompleted(Status);
	}

	BackendContext->WakeUpDispatcherThreadDelegate.Execute();
}

UE::UnifiedError::IoStoreOnDemand::FInstallCacheErrorContext FOnDemandInstallCache::MakeInstallCacheErrorContext(uint64 TotalCachedBytes, uint32 LineNo)
{
	UE::UnifiedError::IoStoreOnDemand::FInstallCacheErrorContext OutCtx;

	const FString CachRootDirectory = Cas.GetRootDirectory();
	OutCtx.bDiskQuerySucceeded		= FPlatformMisc::GetDiskTotalAndFreeSpace(CachRootDirectory, OutCtx.DiskTotalBytes, OutCtx.DiskFreeBytes);
	OutCtx.MaxCacheSize				= MaxCacheSize;
	OutCtx.CacheSize				= TotalCachedBytes;
	OutCtx.LineNo					= LineNo;

	return OutCtx;
}

///////////////////////////////////////////////////////////////////////////////
TSharedPtr<IOnDemandInstallCache> MakeOnDemandInstallCache(
	FOnDemandIoStore& IoStore,
	const FOnDemandInstallCacheConfig& Config,
	FDiskCacheGovernor& Governor)
{
	IFileManager& Ifm = IFileManager::Get();
	if (Config.bDropCache)
	{
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Deleting install cache directory '%s'"), *Config.RootDirectory);
		Ifm.DeleteDirectory(*Config.RootDirectory, false, true);
	}

	const bool bTree = true;
	if (!Ifm.MakeDirectory(*Config.RootDirectory, bTree))
	{
		UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to create directory '%s'"), *Config.RootDirectory);
		return TSharedPtr<IOnDemandInstallCache>();
	}

	return MakeShared<FOnDemandInstallCache>(Config, IoStore, Governor);
}

///////////////////////////////////////////////////////////////////////////////
#if WITH_IOSTORE_ONDEMAND_TESTS

class FTmpDirectoryScope
{
public:
	explicit FTmpDirectoryScope(const FString& InDir)
		: Ifm(IFileManager::Get())
		, Dir(InDir)
	{
		const bool bTree			= true;
		const bool bRequireExists	= false;
		Ifm.DeleteDirectory(*Dir, bRequireExists, bTree);
		Ifm.MakeDirectory(*Dir, bTree);
	}

	~FTmpDirectoryScope()
	{
		const bool bTree			= true;
		const bool bRequireExists	= false;
		Ifm.DeleteDirectory(*Dir, bRequireExists, bTree);
	}
private:
	IFileManager& Ifm;
	FString Dir;
};

FCasAddr CreateCasTestAddr(uint64 Value)
{
	return FCasAddr::From(reinterpret_cast<const uint8*>(&Value), sizeof(uint64));
}

TEST_CASE("IoStore::OnDemand::InstallCache::Journal", "[IoStoreOnDemand][InstallCache]")
{
	const FString TestBaseDir = TEXT("TestTmpDir");

	SECTION("CreateJournalFile")
	{
		FTmpDirectoryScope _(TestBaseDir);
		const FString JournalFile = TestBaseDir / TEXT("test.jrn");
		FResult Result = FCasJournal::Create(JournalFile);
		CHECK(Result.HasValue());
	}

	SECTION("SimpleTransaction")
	{
		FTmpDirectoryScope _(TestBaseDir);
		const FString JournalFile = TestBaseDir / TEXT("test.jrn");
		FResult Result = FCasJournal::Create(JournalFile);
		CHECK(Result.HasValue());

		FCasJournal::FTransaction Transaction = FCasJournal::Begin(JournalFile);
		Transaction.BlockCreated(FCasBlockId(1));
		Result = FCasJournal::Commit(MoveTemp(Transaction));
		CHECK(Result.HasValue());
	}

	SECTION("ReplayChunkLocations")
	{
		//Arrange
		TArray<FCasAddr>	ExpectedAddresses;
		TArray<uint32>		ExpectedBlockOffsets;
		const FCasBlockId	ExpectedBlockId(42);
		
		for (int32 Idx = 1; Idx < 33; ++Idx)
		{
			ExpectedAddresses.Add(FCasAddr::From(reinterpret_cast<const uint8*>(&Idx), sizeof(uint32)));
			ExpectedBlockOffsets.Add(Idx);
		}

		// Act
		FTmpDirectoryScope _(TestBaseDir);
		const FString JournalFile = TestBaseDir / TEXT("test.jrn");
		FResult Result = FCasJournal::Create(JournalFile);
		CHECK(Result.HasValue());

		FCasJournal::FTransaction Transaction = FCasJournal::Begin(JournalFile);
		for (int32 Idx = 0; const FCasAddr& Addr : ExpectedAddresses)
		{
			Transaction.ChunkLocation(
				FCasLocation
				{
					.BlockId = ExpectedBlockId,
					.BlockOffset = ExpectedBlockOffsets[Idx]
				},
				Addr);
		}

		Result = FCasJournal::Commit(MoveTemp(Transaction));
		CHECK(Result.HasValue());

		// Assert
		TArray<FCasJournal::FEntry::FChunkLocation> Locs;
		Result = FCasJournal::Replay(
			JournalFile,
			[&Locs](const FCasJournal::FEntry& JournalEntry)
			{
				switch(JournalEntry.Type())
				{
				case FCasJournal::FEntry::EType::ChunkLocation:
				{
					Locs.Add(JournalEntry.ChunkLocation);
					break;
				}
				default:
					CHECK(false);
					break;
				};
			});
		CHECK(Result.HasValue());
		CHECK(Locs.Num() == ExpectedAddresses.Num());
		for (int32 Idx = 0; const FCasJournal::FEntry::FChunkLocation& Loc : Locs)
		{
			const FCasLocation ExpectedLoc = FCasLocation
			{
				.BlockId = ExpectedBlockId,
				.BlockOffset = uint32(Idx + 1)
			};
			CHECK(Loc.CasLocation.BlockId == ExpectedLoc.BlockId);
			CHECK(Loc.CasLocation.BlockOffset == ExpectedLoc.BlockOffset);
		}
	}

	SECTION("ReplayBlockCreatedAndDeleted")
	{
		// Arrange
		const FCasBlockId ExpectedBlockId(42);

		// Act
		FTmpDirectoryScope _(TestBaseDir);
		const FString JournalFile = TestBaseDir / TEXT("test.jrn");

		FResult Result = FCasJournal::Create(JournalFile);
		CHECK(Result.HasValue());

		FCasJournal::FTransaction Tx = FCasJournal::Begin(JournalFile);
		Tx.BlockCreated(ExpectedBlockId);
		Tx.BlockDeleted(ExpectedBlockId);

		Result = FCasJournal::Commit(MoveTemp(Tx));
		CHECK(Result.HasValue());

		// Assert
		FCasBlockId CreatedBlockId;
		FCasBlockId DeletedBlockId;

		Result = FCasJournal::Replay(
			JournalFile,
			[&CreatedBlockId, &DeletedBlockId](const FCasJournal::FEntry& JournalEntry)
			{
				switch(JournalEntry.Type())
				{
				case FCasJournal::FEntry::EType::BlockCreated:
				{
					CreatedBlockId = JournalEntry.BlockOperation.BlockId;
					break;
				}
				case FCasJournal::FEntry::EType::BlockDeleted:
				{
					DeletedBlockId = JournalEntry.BlockOperation.BlockId;
					break;
				}
				default:
					CHECK(false);
					break;
				};
			});

		CHECK(Result.HasValue());
		CHECK(CreatedBlockId == ExpectedBlockId);
		CHECK(DeletedBlockId == ExpectedBlockId);
	}

	SECTION("ReplayBlockAccess")
	{
		// Arrange
		const FCasBlockId ExpectedBlockId(462);
		const uint64 ExpectedTicks = FDateTime::UtcNow().GetTicks();

		// Act
		FTmpDirectoryScope _(TestBaseDir);
		const FString JournalFile = TestBaseDir / TEXT("test.jrn");

		FResult Result = FCasJournal::Create(JournalFile);
		CHECK(Result.HasValue());

		FCasJournal::FTransaction Tx = FCasJournal::Begin(JournalFile);
		Tx.BlockAccess(ExpectedBlockId, ExpectedTicks);

		Result = FCasJournal::Commit(MoveTemp(Tx));
		CHECK(Result.HasValue());

		// Assert
		FCasBlockId BlockId;
		uint64 Ticks = 0;

		Result = FCasJournal::Replay(
			JournalFile,
			[&BlockId, &Ticks](const FCasJournal::FEntry& JournalEntry)
			{
				switch(JournalEntry.Type())
				{
				case FCasJournal::FEntry::EType::BlockAccess:
				{
					const FCasJournal::FEntry::FBlockOperation& Op = JournalEntry.BlockOperation;
					BlockId	= Op.BlockId;
					Ticks	= Op.UtcTicks;
					break;
				}
				default:
					CHECK(false);
					break;
				};
			});

		CHECK(Result.HasValue());
		CHECK(BlockId == ExpectedBlockId);
		CHECK(Ticks == ExpectedTicks);
	}
}

TEST_CASE("IoStore::OnDemand::InstallCache::Snapshot", "[IoStoreOnDemand][InstallCache]")
{
	const FString TestBaseDir = TEXT("TestTmpDir");

	SECTION("SaveLoadRoundtrip")
	{
		// Arrange
		FCasSnapshot ExpectedSnapshot;

		for (uint32 Id = 1; Id <= 10; ++Id)
		{
			ExpectedSnapshot.Blocks.Add(FCasSnapshot::FBlock
			{
				.BlockId	= FCasBlockId(Id),
				.LastAccess = FDateTime::UtcNow().GetTicks()
			});

			for (uint32 Idx = 1; Idx <= 10; ++Idx)
			{
				FCasAddr CasAddr = CreateCasTestAddr(Idx);
				FCasLocation Loc = FCasLocation
				{
					.BlockId		= FCasBlockId(Id),
					.BlockOffset	= Idx * 256
				};
				ExpectedSnapshot.ChunkLocations.Emplace(CasAddr, Loc);
			}
		}
		ExpectedSnapshot.CurrentBlockId = FCasBlockId(1);

		// Act
		FTmpDirectoryScope _(TestBaseDir);
		const FString SnapshotFile = TestBaseDir / TEXT("test.snp");
		TResult<int64> Result = FCasSnapshot::Save(ExpectedSnapshot, SnapshotFile);
		CHECK(Result.HasValue());
		const FCasSnapshot Snapshot = FCasSnapshot::Load(SnapshotFile).StealValue();

		// Assert
		CHECK(Snapshot.Blocks.Num() == ExpectedSnapshot.Blocks.Num());
		for (int32 Idx = 0; Idx < Snapshot.Blocks.Num(); ++Idx)
		{
			CHECK(Snapshot.Blocks[Idx].BlockId == ExpectedSnapshot.Blocks[Idx].BlockId);
			CHECK(Snapshot.Blocks[Idx].LastAccess == ExpectedSnapshot.Blocks[Idx].LastAccess);
		}
		CHECK(Snapshot.ChunkLocations.Num() == ExpectedSnapshot.ChunkLocations.Num());
		for (int32 Idx = 0; Idx < Snapshot.ChunkLocations.Num(); ++Idx)
		{
			CHECK(Snapshot.ChunkLocations[Idx].Get<0>() == ExpectedSnapshot.ChunkLocations[Idx].Get<0>());
			CHECK(Snapshot.ChunkLocations[Idx].Get<1>() == ExpectedSnapshot.ChunkLocations[Idx].Get<1>());
		}
		CHECK(Snapshot.CurrentBlockId == ExpectedSnapshot.CurrentBlockId);
	}

	SECTION("CreateFromJournal")
	{
		// Arrange
		FTmpDirectoryScope _(TestBaseDir);
		const FString JournalFile = TestBaseDir / TEXT("test.jrn");
		const FCasBlockId ExpectedCurrentBlockId(2);

		FResult Result = FCasJournal::Create(JournalFile);
		CHECK(Result.HasValue());

		FCasJournal::FTransaction Tx = FCasJournal::Begin(JournalFile);

		// Add a block and some chunk locations
		Tx.BlockCreated(FCasBlockId(1));
		for (int32 Idx = 1; Idx <= 10; ++Idx)
		{
			Tx.ChunkLocation(FCasLocation
			{
				.BlockId		= FCasBlockId(1),
				.BlockOffset	= 256
			},
			CreateCasTestAddr(uint64(Idx) << 32 | 1ull));
		}

		// Remove the block and the corresponding chunk locations
		for (int32 Idx = 1; Idx <= 10; ++Idx)
		{
			Tx.ChunkLocation(FCasLocation::Invalid, CreateCasTestAddr(uint64(Idx) << 32 | 1ull));
		}
		Tx.BlockDeleted(FCasBlockId(1));

		// Add a second block and some chunk locations
		Tx.BlockCreated(ExpectedCurrentBlockId);
		for (int32 Idx = 1; Idx <= 10; ++Idx)
		{
			Tx.ChunkLocation(FCasLocation
			{
				.BlockId		= ExpectedCurrentBlockId,
				.BlockOffset	= uint32(Idx) * 256
			},
			CreateCasTestAddr(Idx));
		}

		Result = FCasJournal::Commit(MoveTemp(Tx));
		CHECK(Result.HasValue());

		// Act
		const FCasSnapshot Snapshot = FCasSnapshot::FromJournal(JournalFile).StealValue();

		// Assert
		CHECK(Snapshot.CurrentBlockId == ExpectedCurrentBlockId);
		CHECK(Snapshot.Blocks.Num() == 1); 
		CHECK(Snapshot.ChunkLocations.Num() == 10); 
		for (int32 Idx = 1; Idx < Snapshot.ChunkLocations.Num(); ++Idx)
		{
			const FCasAddr Addr = CreateCasTestAddr(Idx);
			const FCasSnapshot::FChunkLocation* Loc =
				Algo::FindByPredicate(
					Snapshot.ChunkLocations,
					[&Addr](const FCasSnapshot::FChunkLocation& L) { return L.Get<0>() == Addr; });
			CHECK(Loc != nullptr);
			if (Loc != nullptr)
			{
				CHECK(Loc->Get<1>().BlockId == ExpectedCurrentBlockId);
				CHECK(Loc->Get<1>().BlockOffset == uint32(Idx) * 256); 
			}
		}
	}
}

TEST_CASE("IoStore::OnDemand::InstallCache::ErrorHandling", "[IoStoreOnDemand][InstallCache]")
{
	using namespace UE::UnifiedError;
	using namespace UE::UnifiedError::IoStoreOnDemand;

	SECTION("YesWeNeedToTestSerializeErrorDetailsToCbToMakeSureTheGameDoesNotCrashAtRuntime")
	{
		FResult Result = MakeCasError<void>(
			ECasErrorCode::InitializeFailed, EIoErrorCode::DeleteError, TEXT("Test Message"));
		UE_LOGFMT(LogIoStoreOnDemand, Display, "{Error}", Result.GetError());

		{
			FError Error = IoStoreOnDemand::InstallCacheFlushError::MakeError(FInstallCacheErrorContext{}, EDetailFilter::All);
			UE_LOGFMT(LogIoStoreOnDemand, Display, "{Short}", Error.GetModuleIdAndErrorCodeString());
			UE_LOGFMT(LogIoStoreOnDemand, Display, "{Error}", Error); }
		{
			FError Error = IoStoreOnDemand::InstallCacheFlushError::MakeError(FChunkMissingErrorContext{}, EDetailFilter::All);
			UE_LOGFMT(LogIoStoreOnDemand, Display, "{Error}", Error);
		}
		{
			FError Error = IoStoreOnDemand::InstallCachePurgeError::MakeError(FChunkHashMismatchErrorContext{}, EDetailFilter::All);
			UE_LOGFMT(LogIoStoreOnDemand, Display, "{Error}", Error);
		}
		{
			FError Error = IoStoreOnDemand::InstallCachePurgeError::MakeError(FVerificationErrorContext{}, EDetailFilter::All);
			UE_LOGFMT(LogIoStoreOnDemand, Display, "{Error}", Error);
		}
	}
}

#endif // WITH_IOSTORE_ONDEMAND_TESTS

} // namespace UE::IoStore
