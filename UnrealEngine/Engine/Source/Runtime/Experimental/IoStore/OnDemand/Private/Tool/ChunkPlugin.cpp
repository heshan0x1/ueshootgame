// Copyright Epic Games, Inc. All Rights Reserved.

#if defined(UE_WITH_IAS_TOOL)

#include "Command.h"

#include "Algo/Find.h"
#include "Containers/StringView.h"
#include "HAL/FileManager.h"
#include "IO/IoChunkEncoding.h"
#include "IO/IoContainerHeader.h"
#include "IO/IoStatus.h"
#include "IO/IoStore.h"
#include "IO/IoStoreOnDemand.h"
#include "Misc/Compression.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/KeyChainUtilities.h"
#include "Misc/PathViews.h"
#include "Misc/Paths.h"
#include "Misc/StringBuilder.h"
#include "S3/S3Client.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonSerializerMacros.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "UploadQueue.h"

namespace UE::IoStore::Tool
{

struct FChunkPluginStats : FJsonSerializable
{
	double TotalChunksSizeKb = -1.0;

BEGIN_JSON_SERIALIZER
	JSON_SERIALIZE("TotalChunksSizeKb", TotalChunksSizeKb);
END_JSON_SERIALIZER
};

struct FS3Params
{
public:
	FString S3ServiceUrl;
	FString S3Bucket;
	FString S3BucketPrefix;
	FString S3Region;
	FString S3AccessKey;
	FString S3SecretKey;
	FString S3SessionToken;
	int32 S3MaxConcurrentUploads;

	FS3Params(const FContext& Context)
	{
		S3ServiceUrl = FString(Context.Get<FStringView>(TEXT("-ServiceUrl"), FString()));
		S3Bucket = FString(Context.Get<FStringView>(TEXT("-Bucket"), FString()));
		S3Region = FString(Context.Get<FStringView>(TEXT("-Region"), FString()));
		S3AccessKey = FString(Context.Get<FStringView>(TEXT("-AccessKey"), FString()));
		S3SecretKey = FString(Context.Get<FStringView>(TEXT("-SecretKey"), FString()));
		S3SessionToken = FString(Context.Get<FStringView>(TEXT("-SessionToken"), FString()));


		S3BucketPrefix = FString(Context.Get<FStringView>(TEXT("-BucketPrefix"), FString()));
		S3MaxConcurrentUploads = Context.Get<int32>(TEXT("-MaxConcurrentUploads"), 10);
	}

	bool IsValid()
	{
		return !S3AccessKey.IsEmpty() && !S3SecretKey.IsEmpty() && !S3SessionToken.IsEmpty();
	}
};


////////////////////////////////////////////////////////////////////////////////
struct FChunkPluginSettings
{
	TMap<FString, TArray<FString>> PackageSets;
};

class FChunkWriterInterface
{
public:
	virtual ~FChunkWriterInterface() {}
	virtual FIoStatus WriteChunk(const FString& RelativeDir, FIoBuffer Chunk, const FIoHash& Hash) = 0;

	virtual bool Flush() = 0;
};



class FS3ChunkWriter : public FChunkWriterInterface
{
public:
	FS3ChunkWriter(const FS3Params& S3Params) : 
		BucketPrefix(S3Params.S3BucketPrefix),
		Client(FS3ClientConfig({ S3Params.S3Region, S3Params.S3ServiceUrl }), FS3ClientCredentials(S3Params.S3AccessKey, S3Params.S3SecretKey, S3Params.S3SessionToken)),
		UploadQueue(Client, S3Params.S3Bucket, S3Params.S3MaxConcurrentUploads)
	{
	}

	virtual ~FS3ChunkWriter()
	{
		// this should have already been done but do it now just in case
		UploadQueue.Flush();
	}


	virtual FIoStatus WriteChunk(const FString& RelativeDirectory, FIoBuffer Chunk, const FIoHash& Hash) override
	{
		const FString HashString = LexToString(Hash);

		TStringBuilder<256> Key;
		Key << BucketPrefix << TEXT("/")
			<< RelativeDirectory
			<< TEXT("/") << HashString.Left(2)
			<< TEXT("/") << HashString
			<< TEXT(".iochunk");

		if (UploadQueue.Enqueue(Key, Chunk) == false)
		{
			return FIoStatus(EIoErrorCode::WriteError, TEXT("Failed to upload chunk"));
		}

		return FIoStatus(EIoErrorCode::Ok);
	}

	virtual bool Flush() override
	{
		if (!UploadQueue.Flush())
		{
			return false;
		}
		return true;
	}


private:
	FString BucketPrefix;
	UE::FS3Client Client;
	FUploadQueue UploadQueue;
};


class FDiskChunkWriter : public FChunkWriterInterface
{
public:
	FDiskChunkWriter(const FString& InOutputFolder)
	{
		OutputFolder = InOutputFolder;
		
	}

	virtual ~FDiskChunkWriter()
	{
	}

	virtual  FIoStatus WriteChunk(const FString& Directory, FIoBuffer Chunk, const FIoHash& Hash) override 
	{
		IFileManager& FileMgr = IFileManager::Get();
		const FString HashString = LexToString(Hash);

		TStringBuilder<256> Sb;
		Sb << OutputFolder << TEXT("/") << Directory << TEXT("/") << HashString.Left(2);

		bool bTree = true;
		if (FileMgr.MakeDirectory(Sb.ToString(), bTree) == false)
		{
			return FIoStatusBuilder(EIoErrorCode::WriteError)
				<< TEXT("Failed to create directory '")
				<< FString(Sb.ToString())
				<< TEXT("'");
		}

		Sb << TEXT("/") << HashString << TEXT(".iochunk");

		if (TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(Sb.ToString())); Ar.IsValid())
		{
			UE_LOG(LogIoStoreOnDemand, Display, TEXT("Writing file '%s' (%.2lf KiB)"),
				Sb.ToString(), double(Chunk.GetView().GetSize()) / 1024.0);
			Ar->Serialize((void*)Chunk.GetView().GetData(), Chunk.GetView().GetSize());

			return EIoErrorCode::Ok;
		}

		return FIoStatusBuilder(EIoErrorCode::WriteError)
			<< TEXT("Failed to write file '")
			<< FString(Sb.ToString())
			<< TEXT("'");
	}

	bool Flush() override
	{
		return true;
	}
private:
	FString OutputFolder;
};

////////////////////////////////////////////////////////////////////////////////
static FIoStatus WriteChunk(const FString& Directory, FMemoryView Chunk, const FIoHash& Hash)
{
	IFileManager& FileMgr = IFileManager::Get();
	const FString HashString = LexToString(Hash);

	TStringBuilder<256> Sb;
	Sb << Directory << TEXT("/") << HashString.Left(2);

	bool bTree = true;
	if (FileMgr.MakeDirectory(Sb.ToString(), bTree) == false)
	{
		return FIoStatusBuilder(EIoErrorCode::WriteError)
			<< TEXT("Failed to create directory '")
			<< FString(Sb.ToString())
			<< TEXT("'");
	}

	Sb << TEXT("/") << HashString << TEXT(".iochunk");

	if (TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(Sb.ToString())); Ar.IsValid())
	{
		UE_LOG(LogIoStoreOnDemand, Display, TEXT("Writing file '%s' (%.2lf KiB)"),
			Sb.ToString(), double(Chunk.GetSize()) / 1024.0);
		Ar->Serialize((void*)Chunk.GetData(), Chunk.GetSize());

		return EIoErrorCode::Ok;
	}

	return FIoStatusBuilder(EIoErrorCode::WriteError)
		<< TEXT("Failed to write file '")
		<< FString(Sb.ToString())
		<< TEXT("'");
}

static bool ShouldValidateChunk(FName CompressionFormat, uint64 ChunkRawSize)
{
	static TMap<FName, bool> ValidatedFormats;

	if (!CompressionFormat.IsNone())
	{
		if (bool* bCachedResult = ValidatedFormats.Find(CompressionFormat))
		{
			if (*bCachedResult == false)
			{
				return false;
			}
		}
		else
		{
			// Note that if IsFormatValid returns false it will log a message about it. This is one of the
			// reasons we must make sure to only check once per format to avoid  log spam. The other reason
			// is that the check is fairly slow.
			bool bValid = FCompression::IsFormatValid(CompressionFormat);
			ValidatedFormats.Add(CompressionFormat, bValid);

			if (bValid == false)
			{
				UE_LOG(LogIoStoreOnDemand, Display, TEXT("Skipping validation tests for chunks with the compression format '%s'"), *CompressionFormat.ToString())
				return false;
			}
		}
	}

	const uint64 MaxChunkSize = 32 * 1024 * 1024;	// We do not want to validate any chunk that will decompress to more than 32 MiB
													// as we don't want to use too much memory.
	if (ChunkRawSize >= MaxChunkSize)
	{
		return false;
	}

	return true;
}

static int32 ChunkContainer(const FString& ContainerUTocPath, const FChunkPluginSettings& Settings, const TMap<FGuid, FAES::FAESKey>& EncryptionKeys, FChunkWriterInterface* ChunkWriter, const FString& ChunksRelativeFolder, bool bIgnoreContainerHeader,
	FOnDemandToc& OutOnDemandToc, TMap<FString, TArray<FOnDemandTocTagSetPackageList>>* OutFoundTagSets, TArray<FString>& OutContainerFiles, FChunkPluginStats& OutStats)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ChunkPluginCommand::ChunkContainer);

	check(ChunkWriter != nullptr);

	FIoStoreReader ContainerFileReader;
	{
		FIoStatus Status = ContainerFileReader.Initialize(*FPaths::ChangeExtension(ContainerUTocPath, TEXT("")), EncryptionKeys);
		if (!Status.IsOk())
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to open container '%s' for reading due to: %s"), *ContainerUTocPath, *Status.ToString());
			return -1;
		}
	}

	UE_LOG(LogIoStoreOnDemand, Display, TEXT("Processing container '%s'"), *ContainerUTocPath);

	/*TArray<FString> ContainerFileList;
	ContainerFileReader.GetFilenames(ContainerFileList);
	for (const FString& File : ContainerFileList)
	{
		UE_LOG(LogIoStoreOnDemand, Display, TEXT("Found file in container '%s'"), *File);
	}*/

	const uint32 BlockSize = ContainerFileReader.GetCompressionBlockSize();
	if (OutOnDemandToc.Header.BlockSize == 0)
	{
		OutOnDemandToc.Header.BlockSize = ContainerFileReader.GetCompressionBlockSize();
	}

	check(OutOnDemandToc.Header.BlockSize == ContainerFileReader.GetCompressionBlockSize());

	const int32 ContainerIndex = OutOnDemandToc.Containers.AddDefaulted();
	FOnDemandTocContainerEntry& ContainerEntry = OutOnDemandToc.Containers[ContainerIndex];
	ContainerEntry.ContainerId = ContainerFileReader.GetContainerId();
	ContainerEntry.ContainerName = FPaths::GetBaseFilename(ContainerUTocPath);

	const bool bIsContainerEncrypted = EnumHasAnyFlags(ContainerFileReader.GetContainerFlags(), EIoContainerFlags::Encrypted);
	if (bIsContainerEncrypted)
	{
		ContainerEntry.EncryptionKeyGuid = LexToString(ContainerFileReader.GetEncryptionKeyGuid());
		ContainerEntry.ContainerFlags |= static_cast<uint8>(EIoContainerFlags::Encrypted);
	}

	if (Settings.PackageSets.Num() > 0 && OutFoundTagSets != nullptr)
	{
		TArray<FOnDemandTocTagSet> TagSets;

		const FIoChunkId HeaderChunkId = CreateContainerHeaderChunkId(ContainerFileReader.GetContainerId());
		TIoStatusOr<FIoBuffer> Status = ContainerFileReader.Read(HeaderChunkId, FIoReadOptions());
		if (!Status.IsOk())
		{
			if (Status.Status().GetErrorCode() == EIoErrorCode::NotFound)
			{
				UE_LOG(LogIoStoreOnDemand, Display, TEXT("No header chunk for container '%s'"), *ContainerUTocPath);
			}
			else
			{
				UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to read header chunk for container '%s'"), *ContainerUTocPath);
				return -1;
			}
		}
		else
		{
			FIoContainerHeader ContainerHeader;
			FMemoryReaderView Ar(Status.ValueOrDie().GetView());
			Ar << ContainerHeader;
			Ar.Close();
			if (Ar.IsError() || Ar.IsCriticalError())
			{
				UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to deserialize header chunk for container '%s'"), *ContainerUTocPath);
				return -1;
			}

			for (const TPair<FString, TArray<FString>>& Pair : Settings.PackageSets)
			{
				const FString& Tag = Pair.Key;

				FOnDemandTocTagSetPackageList TagSetPackageList;
				TagSetPackageList.ContainerIndex = ContainerIndex;

				for (const FString& Package : Pair.Value)
				{
					const FPackageId PackageId = FPackageId::FromName(FName(Package));
					int32 PackageIndex = INDEX_NONE;
					if (ContainerHeader.PackageIds.Find(PackageId, PackageIndex))
					{
						UE_LOG(LogIoStoreOnDemand, Display, TEXT("Found package %s for tag %s in container %s"), *Package, *Tag, *ContainerUTocPath);
						TagSetPackageList.PackageIndicies.Add(PackageIndex);
					}
				}

				if (TagSetPackageList.PackageIndicies.Num() > 0)
				{
					OutFoundTagSets->FindOrAdd(Tag).Add(MoveTemp(TagSetPackageList));
				}
			}
		}
	}

	TArray<FIoStoreTocChunkInfo> ChunkInfos;
	ContainerFileReader.EnumerateChunks([&ChunkInfos](FIoStoreTocChunkInfo&& Info)
		{
			ChunkInfos.Emplace(MoveTemp(Info));
			return true;
		});

	UE_LOG(LogIoStoreOnDemand, Display, TEXT("Serializing %d chunks"), ChunkInfos.Num());

	for (const FIoStoreTocChunkInfo& ChunkInfo : ChunkInfos)
	{
		const bool bDecrypt = false; // We do not want to decrypt the results. We will upload encrypted chunks and decrypt on the client
		TIoStatusOr<FIoStoreCompressedReadResult> Status = ContainerFileReader.ReadCompressed(ChunkInfo.Id, FIoReadOptions(), bDecrypt);
		if (!Status.IsOk())
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to read container chunk, Container='%s', Reason='%s'"),
				*ContainerUTocPath, *Status.Status().ToString());
			return -1;
		}

		FIoStoreCompressedReadResult ReadResult = Status.ConsumeValueOrDie();

		const uint32 BlockOffset = ContainerEntry.BlockSizes.Num();
		const uint32 BlockCount = ReadResult.Blocks.Num();
		const FIoHash ChunkHash = FIoHash::HashBuffer(ReadResult.IoBuffer.GetView());
		const FString HashString = LexToString(ChunkHash);

		FMemoryView EncodedBlocks = ReadResult.IoBuffer.GetView();
		uint64 RawChunkSize = 0;
		uint64 EncodedChunkSize = 0;
		FName CompressionFormat = NAME_None;
		for (const FIoStoreCompressedBlockInfo& BlockInfo : ReadResult.Blocks)
		{
			check(Align(BlockInfo.CompressedSize, FAES::AESBlockSize) == BlockInfo.AlignedSize);
			const uint64 EncodedBlockSize = BlockInfo.AlignedSize;
			ContainerEntry.BlockSizes.Add(uint32(BlockInfo.CompressedSize));

			FMemoryView EncodedBlock = EncodedBlocks.Left(EncodedBlockSize);
			EncodedBlocks += EncodedBlock.GetSize();

			check(((char*)EncodedBlock.GetData() + EncodedBlock.GetSize()) <= ((char*)ReadResult.IoBuffer.GetView().GetData() + ReadResult.IoBuffer.GetView().GetSize()));
			
			FIoBlockHash Hash = FIoChunkEncoding::HashBlock(EncodedBlock);
			ContainerEntry.BlockHashes.Add(Hash);

			EncodedChunkSize += EncodedBlockSize;
			RawChunkSize += BlockInfo.UncompressedSize;

			if (BlockInfo.CompressionMethod != NAME_None)
			{
				CompressionFormat = BlockInfo.CompressionMethod;
				if (OutOnDemandToc.Header.CompressionFormat.IsEmpty())
				{
					OutOnDemandToc.Header.CompressionFormat = BlockInfo.CompressionMethod.ToString();
				}
			}
		}

		if (EncodedChunkSize != ReadResult.IoBuffer.GetSize())
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Chunk size mismatch, Container='%s', ChunkId='%s'"),
				*ContainerUTocPath, *LexToString(ChunkInfo.Id));
			return -1;
		}

		// Additional validation to make sure that the chunks can be decrypted and decoded
		if (ShouldValidateChunk(CompressionFormat, RawChunkSize))
		{
			FIoChunkDecodingParams DecodingParams;
			DecodingParams.CompressionFormat = CompressionFormat;
			DecodingParams.TotalRawSize = RawChunkSize;
			DecodingParams.EncodedBlockSize = TConstArrayView<uint32>(ContainerEntry.BlockSizes.GetData() + BlockOffset, BlockCount);
			DecodingParams.BlockHash = TConstArrayView<FIoBlockHash>(ContainerEntry.BlockHashes.GetData() + BlockOffset, BlockCount);

			if (bIsContainerEncrypted)
			{
				const FAES::FAESKey* EncryptionKey = EncryptionKeys.Find(ContainerFileReader.GetEncryptionKeyGuid());
				if (EncryptionKey == nullptr)
				{
					UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to find encryption key '%s', Container='%s"), *LexToString(ContainerFileReader.GetEncryptionKeyGuid()), *ContainerUTocPath);
					return -1;
				}
				DecodingParams.EncryptionKey = FMemoryView(EncryptionKey->Key, EncryptionKey->KeySize);
			}

			FIoBuffer OutputBuffer(RawChunkSize);
			bool bDecoded = FIoChunkEncoding::Decode(DecodingParams, ReadResult.IoBuffer.GetView(), OutputBuffer.GetMutableView(), EIoDecodeFlags::None);
			if (!bDecoded)
			{
				UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to decode chunk from disk, Container='%s', ChunkId='%s'"), *ContainerUTocPath, *LexToString(ChunkInfo.Id));
				return -1;
			}
		}

		FIoStatus WriteStatus = ChunkWriter->WriteChunk(ChunksRelativeFolder, ReadResult.IoBuffer, ChunkHash);
		if (WriteStatus.IsOk() == false)
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("%s"), *WriteStatus.ToString());
			return -1;
		}

		FOnDemandTocEntry& TocEntry = ContainerEntry.Entries.AddDefaulted_GetRef();
		TocEntry.ChunkId = ChunkInfo.Id;
		TocEntry.Hash = ChunkHash;
		TocEntry.RawSize = RawChunkSize;
		TocEntry.EncodedSize = EncodedChunkSize;
		TocEntry.BlockOffset = BlockOffset;
		TocEntry.BlockCount = BlockCount;

		OutStats.TotalChunksSizeKb += (double)EncodedChunkSize / 1024.0;
	}

	if (bIgnoreContainerHeader)
	{
		UE_LOG(LogIoStoreOnDemand, Display, TEXT("Ignoring container header for '%s'"), *ContainerUTocPath)
	}
	else
	{
		const bool bDecrypt = false;
		const FIoChunkId ChunkId = CreateContainerHeaderChunkId(ContainerFileReader.GetContainerId());
		TIoStatusOr<FIoStoreCompressedReadResult> ReadResult = ContainerFileReader.ReadCompressed(ChunkId, FIoReadOptions(), bDecrypt);
		if (ReadResult.IsOk())
		{
			const FIoBuffer& Chunk = ReadResult.ValueOrDie().IoBuffer;
			ContainerEntry.Header = MakeArrayView<const uint8>(Chunk.GetData(), static_cast<int32>(Chunk.GetSize()));
		}
		else
		{
			const EIoErrorCode Err = ReadResult.Status().GetErrorCode();
			if (Err != EIoErrorCode::UnknownChunkID && Err != EIoErrorCode::NotFound)
			{
				UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to read container header, reason '%s'"), *ReadResult.Status().ToString())
					return -1;
			}
		}
	}

	OutContainerFiles.Add(ContainerUTocPath);
	ContainerFileReader.GetContainerFilePaths(OutContainerFiles);

	return 0;
}

int32 WriteOnDemandToc(FOnDemandToc& OnDemandToc, const FString& ContainerName, const FString& OutputFolder)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ChunkPluginCommand::WriteOnDemandToc);

	IFileManager& FileMgr = IFileManager::Get();

	FString Filename = FPathViews::SetExtension(ContainerName, TEXT(".uondemandtoc"));
	Filename.ToLowerInline();

	const FString TocPath = OutputFolder / Filename;

	if (TUniquePtr<FArchive> Ar(FileMgr.CreateFileWriter(*TocPath)); Ar.IsValid())
	{
		*Ar << OnDemandToc;
		Ar->Close();
		if (Ar->IsError())
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to serialize TOC '%s'"), *TocPath);
			return -1;
		}
		else
		{
			const int64 TocSize = Ar->Tell();
			UE_LOG(LogIoStoreOnDemand, Display, TEXT("Writing file '%s' (%.2lf KiB)"),
				*TocPath, double(TocSize) / 1024);

			return 0;
		}
	}
	else
	{
		UE_LOG(LogIoStoreOnDemand, Display, TEXT("Failed writing file '%s'"), *TocPath);
		return -1;
	}
}

static FKeyChain LoadCryptoKeys(const FContext& Context)
{
	FKeyChain Ret;

	auto Path = Context.Get<FStringView>(TEXT("-CryptoKeys"));
	if (Path.IsEmpty())
	{
		return Ret;
	}

	KeyChainUtilities::LoadKeyChainFromFile(Path.GetData(), Ret);
	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
static int32 ChunkPluginCommandEntry(const FContext& Context)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ChunkPluginCommand);
	//while (!FPlatformMisc::IsDebuggerPresent());
	//UE_DEBUG_BREAK();

	const FString Platform				= FString(Context.Get<FStringView>(TEXT("-Platform"), FString()));
	const FString BuildVersion			= FString(Context.Get<FStringView>(TEXT("-BuildVersion"), FString()));
	const FString OnDemandTocName		= FString(Context.Get<FStringView>(TEXT("-OnDemandTocName"), FString()));
	const FString InputFolder			= FString(Context.Get<FStringView>(TEXT("-InputFolder"), FString()));
	const FString InOutputFolder		= FString(Context.Get<FStringView>(TEXT("-OutputFolder"), FString()));
	const FString IntermediateFolder	= FString(Context.Get<FStringView>(TEXT("-IntermediateFolder"), FString()));
	FString SettingsFile				= FString(Context.Get<FStringView>(TEXT("-SettingsFile"), FString()));
	FString OutputStatsJson				= FString(Context.Get<FStringView>(TEXT("-OutputStatsJson"), FString()));
	const FString HostGroupName			= FString(Context.Get<FStringView>(TEXT("-HostGroupName"), FString()));

	const bool bIgnoreContainerHeader	= Context.Get<bool>(TEXT("-IgnoreContainerHeader"), false);
	const bool bStreamOnDemand			= Context.Get<bool>(TEXT("-StreamOnDemand"), false);
	const bool bInstallOnDemand			= Context.Get<bool>(TEXT("-InstallOnDemand"), false);

	bool bSupportInstalledContainers = !Context.Get<bool>(TEXT("-SkipInstallOnDemand"), false);
	bool bSupportStreamingContainers = !Context.Get<bool>(TEXT("-SkipStreamOnDemand"), false);

	EOnDemandTocFlags TocFlags = EOnDemandTocFlags::None;
	if (bStreamOnDemand)
	{
		TocFlags |= EOnDemandTocFlags::StreamOnDemand;
	}
	else if (bInstallOnDemand)
	{
		TocFlags |= EOnDemandTocFlags::InstallOnDemand;
	}
	else
	{
		const EOnDemandTocFlags FallbackFlags = EOnDemandTocFlags::InstallOnDemand;
		UE_LOG(LogIoStoreOnDemand, Warning, TEXT("No TOC flags was specified, falling back to '%s'"), *LexToString(FallbackFlags));
		TocFlags = FallbackFlags;
	}

	FS3Params S3Params(Context);

	const bool bIncludeSigPak			= Context.Get<bool>(TEXT("-IncludeSigPak"), false);
	const bool bDeleteContainerFiles	= !Context.Get<bool>(TEXT("-KeepContainerFiles"), false);
	FString OutputFolder				= InOutputFolder;
	FString ContainerFolder				= InputFolder;
	FString IoStoreRelativeFolder		= TEXT("iostore");
	FString ChunksRelativeFolder		= IoStoreRelativeFolder / TEXT("chunks");

	FPaths::NormalizeDirectoryName(ContainerFolder);
	FPaths::NormalizeDirectoryName(OutputFolder);
	FPaths::NormalizeFilename(SettingsFile);
	FPaths::NormalizeFilename(OutputStatsJson);

	UE_LOG(LogIoStoreOnDemand, Display, TEXT("I/O store chunk plugin:"));
	UE_LOG(LogIoStoreOnDemand, Display, TEXT("----------------------------------------"));
	UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tBuildVersion: %s"), *BuildVersion);
	UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tPlatform: %s"), *Platform);
	UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tOnDemandTocName: %s"), *OnDemandTocName);
	UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tInputFolder: %s"), *InputFolder);
	UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tOutputFolder: %s"), *OutputFolder);
	UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tIntermediateFolder: %s"), *IntermediateFolder);
	UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tSkipInstallOnDemand: %s"), !bSupportInstalledContainers ? TEXT("true") : TEXT("false"));
	UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tSkipStreamOnDemand: %s"), !bSupportStreamingContainers ? TEXT("true") : TEXT("false"));
	UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tSettingsFile: %s"), *SettingsFile);
	UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tOutputStatsJson: %s"), *OutputStatsJson);
	UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tIncludeSigPak: %s"), bIncludeSigPak ? TEXT("true") : TEXT("false"));
	UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tDeleteContainerFiles: %s"), bDeleteContainerFiles ? TEXT("true") : TEXT("false"));
	UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tIgnoreContainerHeader: %s"), bIgnoreContainerHeader ? TEXT("true") : TEXT("false"));
	UE_LOG(LogIoStoreOnDemand, Display, TEXT("\tTOC flags: '%s'"), *LexToString(TocFlags));

	IFileManager& FileMgr = IFileManager::Get();

	TUniquePtr<FChunkWriterInterface> ChunkWriter;
	bool bUseS3 = false;
	if (S3Params.IsValid())
	{
		ChunkWriter = MakeUnique<FS3ChunkWriter>(S3Params);
	}
	else
	{
		ChunkWriter = MakeUnique<FDiskChunkWriter>(OutputFolder);
	}
	check(ChunkWriter != nullptr)

	FChunkPluginSettings Settings;
	if (!SettingsFile.IsEmpty())
	{
		TUniquePtr<FArchive> Ar(FileMgr.CreateFileReader(*SettingsFile));
		if (!Ar || Ar->IsError())
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to open settings file '%s'"), *SettingsFile);
			return -1;
		}

		TSharedRef<TJsonReader<UTF8CHAR>> JsonReader = TJsonReaderFactory<UTF8CHAR>::Create(Ar.Get());

		TSharedPtr<FJsonValue> JsonSettings;
		if (!FJsonSerializer::Deserialize(*JsonReader, JsonSettings))
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to read settings file '%s'"), *SettingsFile);
			return -1;
		}

		TSharedPtr<FJsonObject> JsonSettingsObject = JsonSettings->AsObject();
		if (!JsonSettingsObject)
		{
			UE_LOG(LogIoStoreOnDemand, Error, TEXT("Bad settings file '%s'"), *SettingsFile);
			return -1;
		}

		if (TSharedPtr<FJsonValue> PackageSets = JsonSettingsObject->TryGetField(TEXT("PackageSets")))
		{
			TSharedPtr<FJsonObject> PackageSetsObject = PackageSets->AsObject();
			if (!PackageSetsObject)
			{
				UE_LOG(LogIoStoreOnDemand, Error, TEXT("Bad settings file '%s'"), *SettingsFile);
				return -1;
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : PackageSetsObject->Values)
			{
				TArray<FString> Packages;
				if (!PackageSetsObject->TryGetStringArrayField(Pair.Key, Packages))
				{
					UE_LOG(LogIoStoreOnDemand, Error, TEXT("Bad settings file '%s'"), *SettingsFile);
					return -1;
				}

				UE_LOG(LogIoStoreOnDemand, Display, TEXT("Found Package Set '%s'"), *Pair.Key);
				for (const FString& Package : Packages)
				{
					UE_LOG(LogIoStoreOnDemand, Display, TEXT("-> \t'%s'"), *Package);
				}

				Settings.PackageSets.Add(Pair.Key, MoveTemp(Packages));
			}
		}
	}

	FChunkPluginStats Stats;

	TMap<FGuid, FAES::FAESKey> EncryptionKeys;

	FKeyChain KeyChain = LoadCryptoKeys(Context);
	for (const TPair<FGuid, FNamedAESKey>& KeyPair : KeyChain.GetEncryptionKeys())
	{
		EncryptionKeys.Add(KeyPair.Key, KeyPair.Value.Key);
	}

	if (FileMgr.DirectoryExists(*ContainerFolder) == false)
	{
		UE_LOG(LogIoStoreOnDemand, Error, TEXT("Directory '%s' does not exist"), *ContainerFolder);
		return -1;
	}

	TArray<FString> ContainerFilenames;
	FileMgr.FindFiles(ContainerFilenames, *ContainerFolder, TEXT("*.utoc"));
	UE_LOG(LogIoStoreOnDemand, Display, TEXT("Found %d container files(s)"), ContainerFilenames.Num());

	if (!bSupportInstalledContainers && !bSupportStreamingContainers)
	{
		UE_LOG(LogIoStoreOnDemand, Error, TEXT("Neither Installed or Streaming containers were requested"));
		return -1;
	}

	FOnDemandToc OnDemandToc;
	OnDemandToc.Header.Flags = static_cast<uint32>(TocFlags);
	OnDemandToc.Header.HostGroupName = HostGroupName;
	OnDemandToc.Header.ChunksDirectory = TEXT(""); // TODO?
	OnDemandToc.Containers.Reserve(ContainerFilenames.Num());

	TMap<FString, TArray<FOnDemandTocTagSetPackageList>> FoundTagSets;
	FoundTagSets.Reserve(Settings.PackageSets.Num());

	FOnDemandToc StreamingOnDemandToc;
	StreamingOnDemandToc.Header.Flags = static_cast<uint32>(EOnDemandTocFlags::StreamOnDemand);
	StreamingOnDemandToc.Header.HostGroupName = HostGroupName;
	StreamingOnDemandToc.Header.ChunksDirectory = TEXT(""); // TODO?
	StreamingOnDemandToc.Containers.Reserve(ContainerFilenames.Num());

	TArray<FString> ContainerFiles;

	for (const FString& Filename : ContainerFilenames)
	{
		const FString FullPath = ContainerFolder / Filename;

		// TODO: At the moment the only way we can identify an OnDemand container is by the name. Might be better to pass this info into the tool
		const bool bIsStreamOnDemandContainer = FullPath.Contains(TEXT("OnDemand"));

		if (bSupportStreamingContainers && bIsStreamOnDemandContainer)
		{
			// Attempt to add to IAS output container
			int32 Result = ChunkContainer(FullPath, Settings, EncryptionKeys, ChunkWriter.Get(), ChunksRelativeFolder, bIgnoreContainerHeader, StreamingOnDemandToc, nullptr /*OutFoundTagSets*/, ContainerFiles, Stats);
			if (Result < 0)
			{
				return Result;
			}
		}
		else if(bSupportInstalledContainers && !bIsStreamOnDemandContainer)
		{
			// Attempt to add to IAD output container
			int32 Result = ChunkContainer(FullPath, Settings, EncryptionKeys, ChunkWriter.Get(), ChunksRelativeFolder, bIgnoreContainerHeader, OnDemandToc, &FoundTagSets, ContainerFiles, Stats);
			if (Result < 0)
			{
				return Result;
			}
		}
	}

	if (bSupportInstalledContainers)
	{
		OnDemandToc.TagSets.Reserve(FoundTagSets.Num());
		for (TPair<FString, TArray<FOnDemandTocTagSetPackageList>>& Pair : FoundTagSets)
		{
			FOnDemandTocTagSet& TocTagSet = OnDemandToc.TagSets.Emplace_GetRef();
			TocTagSet.Tag = Pair.Key;
			TocTagSet.Packages = MoveTemp(Pair.Value);
		}
	}

	if (bDeleteContainerFiles)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ChunkPluginCommand::DeleteContainerFiles);

		UE_LOG(LogIoStoreOnDemand, Display, TEXT("Deleting chunked  containers..."));

		for (const FString& Path : ContainerFiles)
		{
			if (FileMgr.FileExists(*Path))
			{
				UE_LOG(LogIoStoreOnDemand, Display, TEXT("Deleting '%s'"), *Path);
				if (!FileMgr.Delete(*Path, /*RequireExists*/true))
				{
					UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to delete '%s'"), *Path);
				}
			}
		}
	}

	// Write additional file(s)
	if (bIncludeSigPak)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ChunkPluginCommand::IncludeSigPak);

		const FStringView AllowedExt[]
		{
			TEXTVIEW("pak"),
			TEXTVIEW("sig"),
		};

		TStringBuilder<256> Sb;
		TArray<FString> AdditionalFiles;
		FileMgr.FindFiles(AdditionalFiles, *ContainerFolder, TEXT("*.*"));

		UE_LOG(LogIoStoreOnDemand, Display, TEXT("Serializing %d additional file(s)"), AdditionalFiles.Num());
		for (const FString& Filename : AdditionalFiles)
		{
			const bool bIncludeDot = false;
			const FStringView Ext = FPathViews::GetExtension(Filename, bIncludeDot);

			if (const FStringView* Result =
				Algo::FindByPredicate(AllowedExt, [&Ext](const FStringView& E) { return Ext == E; });
				Result == nullptr)
			{
				continue;
			}

			const FString FullPath = ContainerFolder / Filename;
			TArray<uint8> FileData;
			if (!FFileHelper::LoadFileToArray(FileData, *FullPath))
			{
				UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed reading file '%s'"), *FullPath);
				return -1;
			}

			FMemoryView Chunk = MakeMemoryView(FileData.GetData(), FileData.Num());
			const FIoHash ChunkHash = FIoHash::HashBuffer(Chunk);

			FIoBuffer Buffer(FIoBuffer::Clone, Chunk);

			FIoStatus WriteStatus = ChunkWriter->WriteChunk(ChunksRelativeFolder, Buffer, ChunkHash);

			if (WriteStatus.IsOk() == false)
			{
				UE_LOG(LogIoStoreOnDemand, Error, TEXT("%s"), *WriteStatus.ToString());
				return -1;
			}

			UE_LOG(LogIoStoreOnDemand, Display, TEXT("Adding additional file '%s'"), *Filename);
			OnDemandToc.AdditionalFiles.Add(FOnDemandTocAdditionalFile
			{
				.Hash = ChunkHash,
				.Filename = Filename,
				.FileSize = IntCastChecked<uint64>(FileData.Num())
			});
		}
	}

	const int64 Timestamp = FDateTime::UtcNow().ToUnixTimestamp();
	// todo, we actually want the .uondemandtoc files in the base directory
	// const FString TocPath = OutputFolder / Filename; // like this yo
	const FString OnDemandTocOutputFolder = OutputFolder / IoStoreRelativeFolder;

	if (bSupportInstalledContainers && !OnDemandToc.Containers.IsEmpty())
	{
		OnDemandToc.Meta.EpochTimestamp = Timestamp;
		OnDemandToc.Meta.TargetPlatform = Platform;
		OnDemandToc.Meta.BuildVersion = BuildVersion;

		int32 Result = WriteOnDemandToc(OnDemandToc, OnDemandTocName, OnDemandTocOutputFolder);
		if (Result < 0)
		{
			return Result;
		}
	}

	if (bSupportStreamingContainers && !StreamingOnDemandToc.Containers.IsEmpty())
	{
		StreamingOnDemandToc.Meta.EpochTimestamp = Timestamp;
		StreamingOnDemandToc.Meta.TargetPlatform = Platform;
		StreamingOnDemandToc.Meta.BuildVersion = BuildVersion;

		// TODO: OnDemandTocName
		int32 Result = WriteOnDemandToc(StreamingOnDemandToc, OnDemandTocName + TEXT("ondemand"), OnDemandTocOutputFolder);
		if (Result < 0)
		{
			return Result;
		}
	}

	// Write dummy containers if necessary
	if (bDeleteContainerFiles)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ChunkPluginCommand::WriteDummyContainerFiles);

		UE_LOG(LogIoStoreOnDemand, Display, TEXT("Replacing .utoc for chunked  containers..."));

		for (const FString& SourcePath : ContainerFiles)
		{
			if (!SourcePath.EndsWith(TEXT(".utoc")))
			{
				continue;
			}

			FNameBuilder NameBuilder;
			NameBuilder << BuildVersion << FPathViews::GetBaseFilename(SourcePath) << TEXTVIEW("dummy");

			FIoContainerSettings ContainerSettings;
			ContainerSettings.ContainerId = FIoContainerId::FromName(FName(NameBuilder));

			const FString FullPath = ContainerFolder / FPaths::GetCleanFilename(SourcePath);

			FIoStoreTocResource Toc;

			TIoStatusOr<uint64> Status = FIoStoreTocResource::Write(*FullPath, Toc, 0, 0, ContainerSettings);
			if (!Status.IsOk())
			{
				UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed to write dummy container '%s' (%s)"), *FullPath, *Status.Status().ToString());
				return -1;
			}

			UE_LOG(LogIoStoreOnDemand, Display, TEXT("Wrote dummy file '%s' (%.2lf KiB)"), *FullPath, double(Status.ValueOrDie()) / 1024);
		}
	}


	if (!ChunkWriter->Flush())
	{
		UE_LOG(LogIoStoreOnDemand, Error, TEXT("Writer error: Failed to upload chunk(s)"));
		return -1; 
	}

	if (!OutputStatsJson.IsEmpty())
	{
		TUniquePtr<FArchive> FileWriter(FileMgr.CreateFileWriter(*OutputStatsJson));
		if (!FileWriter)
		{
			UE_LOG(LogIoStoreOnDemand, Display, TEXT("Failed writing stats file '%s'"), *OutputStatsJson);
			return -1;
		}
		TSharedRef<TJsonWriter<UTF8CHAR>> JsonWriter = TJsonWriterFactory<UTF8CHAR>::Create(FileWriter.Get());
		Stats.ToJson(JsonWriter, false);
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////
extern FArgumentSet S3Arguments;

static FCommand ChunkPluginCommand(
	ChunkPluginCommandEntry,
	TEXT("ChunkPlugin"),
	TEXT(""),
	{
		TArgument<FStringView>(TEXT("-Platform"),			TEXT("Platform name.")),
		TArgument<FStringView>(TEXT("-BuildVersion"),		TEXT("Build version")),
		TArgument<FStringView>(TEXT("-OnDemandTocName"),	TEXT("On Demand TOC Name")),
		TArgument<FStringView>(TEXT("-InputFolder"),		TEXT("Input folder to plugin information.")),
		TArgument<FStringView>(TEXT("-OutputFolder"),		TEXT("Ouptut folder.")),
		TArgument<FStringView>(TEXT("-IntermediateFolder"),	TEXT("Intermediate folder.")),
		TArgument<FStringView>(TEXT("-SettingsFile"),		TEXT("Optional settings file.")),
		TArgument<FStringView>(TEXT("-OutputStatsJson"),	TEXT("Path to write a json file with statistics.")),
		TArgument<FStringView>(TEXT("-HostGroupName"),		TEXT("Host group name or URL")),
		TArgument<FStringView>(TEXT("-CryptoKeys"),			TEXT("")),
		TArgument<bool>(TEXT("-IncludeSigPak"),				TEXT("Include .sig and .pak file in the uondemandtoc")),
		TArgument<bool>(TEXT("-KeepContainerFiles"),		TEXT("Should we keep the container files after processing them.")),
		TArgument<bool>(TEXT("-StreamOnDemand"),			TEXT("Set the content to be streamed on-demand")),
		TArgument<bool>(TEXT("-InstallOnDemand"),			TEXT("Set the content to be installed on-demand")),
		TArgument<bool>(TEXT("-SkipStreamOnDemand"),		TEXT("Do not chunk any stream on-demand containers")),
		TArgument<bool>(TEXT("-SkipInstallOnDemand"),		TEXT("Do not chunk any install on-demand containers")),
		TArgument<bool>(TEXT("-IgnoreContainerHeader"),		TEXT("Ignore the container header?")),
		TArgument<FStringView>(TEXT("-BucketPrefix"),		TEXT("Path to prefix to bucket objects")),
		TArgument<int32>(TEXT("-MaxConcurrentUploads"),		TEXT("Number of simultaneous uploads")),
		S3Arguments,
	}
);

} // namespace UE::IoStore::Tool

#endif // UE_WITH_IAS_TOOL
