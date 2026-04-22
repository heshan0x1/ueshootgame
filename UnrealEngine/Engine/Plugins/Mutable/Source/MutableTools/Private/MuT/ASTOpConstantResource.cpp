// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpConstantResource.h"

#include "MuT/CompilerPrivate.h"
#include "MuR/Layout.h"
#include "MuR/Mesh.h"
#include "MuR/ModelPrivate.h"
#include "MuR/ImagePrivate.h"
#include "MuR/PhysicsBody.h"
#include "MuR/Serialisation.h"
#include "MuR/Skeleton.h"

#include "Containers/Array.h"
#include "HAL/PlatformMath.h"
#include "HAL/PlatformFileManager.h"
#include "Hash/CityHash.h"
#include "Misc/AssertionMacros.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Compression/OodleDataCompression.h"

#include <inttypes.h> // Required for 64-bit printf macros

namespace UE::Mutable::Private
{

	/** Proxy class for a temporary resource while compiling. 
	* The resource may be stored in different ways:
	* - as is, in memory with its own pointer.
	* - in a compressed buffer
	* - saved to a disk file compressed or uncompressed.
	*/
	template<class R>
	class MUTABLETOOLS_API ResourceProxyTempFile : public TResourceProxy<R>
	{
	private:

		/** Actual resource to store. If the pointer is valid, it wasn't worth dumping to disk or compressing. */
		TSharedPtr<const R> Resource;

		/** Temp filename used if it was necessary. */
		FString FileName;

		/** Size of the resource in memory. */
		uint32 UncompressedSize = 0;

		/** Size of the saved file. It may be the size of the resource in memory, or its compressed size. */
		uint32 FileSize = 0;

		/** Valid if the resource was compressed and stored in memory instead of dumped to disk. */
		TArray<uint8> CompressedBuffer;

		/** Shared context with cache settings and stats. */
		FProxyFileContext& Options;

		/** Prevent concurrent access to a signal resource. */
		FCriticalSection Mutex;

	public:

		ResourceProxyTempFile(TSharedPtr<const R> InResource, FProxyFileContext& InOptions)
			: Options(InOptions)
		{
			if (!InResource)
			{
				return;
			}

			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

			FOutputMemoryStream Stream(128*1024);
			FOutputArchive Arch(&Stream);
			R::Serialise(InResource.Get(), Arch);

			UncompressedSize = Stream.GetBufferSize();

			if (Stream.GetBufferSize() <= Options.MinProxyFileSize)
			{
				// Not worth compressing or caching to disk
				Resource = InResource;
			}
			else
			{
				// Compress
				int64 CompressedSize = 0;
				constexpr bool bEnableCompression = true;
				if (bEnableCompression)
				{
					int64 CompressedBufferSize = FOodleDataCompression::CompressedBufferSizeNeeded(Stream.GetBufferSize());
					CompressedBufferSize = FMath::Max(CompressedBufferSize, int64(Stream.GetBufferSize() / 2));
					CompressedBuffer.SetNumUninitialized(CompressedBufferSize);

					CompressedSize = FOodleDataCompression::CompressParallel(
						CompressedBuffer.GetData(), CompressedBufferSize,
						Stream.GetBuffer(), Stream.GetBufferSize(),
						FOodleDataCompression::ECompressor::Kraken,
						FOodleDataCompression::ECompressionLevel::SuperFast,
						true // CompressIndependentChunks
					);
				}

				bool bCompressed = CompressedSize != 0;

				if (bCompressed && uint64(CompressedSize) <= Options.MinProxyFileSize)
				{
					// Keep the compressed data, and don't store to file
					CompressedBuffer.SetNum(CompressedSize,EAllowShrinking::Yes);
				}
				else
				{
					// Save
					FString Prefix = FPlatformProcess::UserTempDir();

					uint32 PID = FPlatformProcess::GetCurrentProcessId();
					Prefix += FString::Printf(TEXT("mut.temp.%u"), PID);

					FString FinalTempPath;
					IFileHandle* ResourceFile = nullptr;
					uint64 AttemptCount = 0;
					while (!ResourceFile && AttemptCount < Options.MaxFileCreateAttempts)
					{
						uint64 ThisThreadFileIndex = Options.CurrentFileIndex.load();
						while (!Options.CurrentFileIndex.compare_exchange_strong(ThisThreadFileIndex, ThisThreadFileIndex + 1));

						FinalTempPath = Prefix + FString::Printf(TEXT(".%.16" PRIx64), ThisThreadFileIndex);
						ResourceFile = PlatformFile.OpenWrite(*FinalTempPath);
						++AttemptCount;
					}

					if (!ResourceFile)
					{
						UE_LOG(LogMutableCore, Error, TEXT("Failed to create temporary file. Disk full?"));
						check(false);
					}

					if (bCompressed)
					{
						FileSize = CompressedSize;
						ResourceFile->Write(CompressedBuffer.GetData(), FileSize);
					}
					else
					{
						FileSize = UncompressedSize;
						ResourceFile->Write(Stream.GetBuffer(), FileSize);
					}

					CompressedBuffer.SetNum(0, EAllowShrinking::Yes);

					delete ResourceFile;

					FileName = FinalTempPath;
					Options.FilesWritten++;
					Options.BytesWritten += FileSize;
				}
			}
		}

		~ResourceProxyTempFile()
		{
			FScopeLock Lock(&Mutex);

			if (!FileName.IsEmpty())
			{
				// Delete temp file
				FPlatformFileManager::Get().GetPlatformFile().DeleteFile(*FileName);
				FileName.Empty();
			}
		}

		TSharedPtr<const R> Get() override
		{
			FScopeLock Lock(&Mutex);

			TSharedPtr<const R> Result;
			if (Resource)
			{
				// Cached as is
				Result = Resource;
			}
			else if (!CompressedBuffer.Num() && !FileName.IsEmpty())
			{
				IFileHandle* ResourceFile = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*FileName);
				check(ResourceFile);

				CompressedBuffer.SetNumUninitialized(FileSize);
				ResourceFile->Read(CompressedBuffer.GetData(), FileSize);
				delete ResourceFile;

				bool bCompressed = FileSize != UncompressedSize;

				if (!bCompressed)
				{
					FInputMemoryStream stream(CompressedBuffer.GetData(), FileSize);
					FInputArchive arch(&stream);
					Result = R::StaticUnserialise(arch);

					CompressedBuffer.SetNum(0, EAllowShrinking::Yes);
				}

				Options.FilesRead++;
				Options.BytesRead += FileSize;
			}

			if (CompressedBuffer.Num())
			{
				// Cached compressed
				TArray<uint8> UncompressedBuf;
				UncompressedBuf.SetNumUninitialized(UncompressedSize);

				bool bSuccess = FOodleDataCompression::DecompressParallel(
					UncompressedBuf.GetData(), UncompressedSize,
					CompressedBuffer.GetData(), CompressedBuffer.Num());
				check(bSuccess);

				if (bSuccess)
				{
					FInputMemoryStream stream(UncompressedBuf.GetData(), UncompressedSize);
					FInputArchive arch(&stream);
					Result = R::StaticUnserialise(arch);
				}

				if (!FileName.IsEmpty())
				{
					CompressedBuffer.SetNum(0, EAllowShrinking::Yes);
				}
			}

			return Result;
		}

	};


	//-------------------------------------------------------------------------------------------------
	void ASTOpConstantResource::ForEachChild(const TFunctionRef<void(ASTChild&)> f)
	{
		/** Image Parameters are evaluated at runtime. */
		for (TPair<FName, ASTChild >& Element : ImageOperations)
		{
			f(Element.Value);
		}
	}


	//-------------------------------------------------------------------------------------------------
	bool ASTOpConstantResource::IsEqual(const ASTOp& OtherUntyped) const
	{
		if (OtherUntyped.GetOpType()==GetOpType())
		{
			const ASTOpConstantResource* Other = static_cast<const ASTOpConstantResource*>(&OtherUntyped);
			return Type == Other->Type && ValueHash == Other->ValueHash &&
				LoadedValue == Other->LoadedValue && Proxy == Other->Proxy
				&& SourceDataDescriptor == Other->SourceDataDescriptor
				&& ImageOperations == Other->ImageOperations;
		}
		return false;
	}


	//-------------------------------------------------------------------------------------------------
	UE::Mutable::Private::Ptr<ASTOp> ASTOpConstantResource::Clone(MapChildFuncRef MapChild) const
	{
		Ptr<ASTOpConstantResource> n = new ASTOpConstantResource();
		n->Type = Type;
		n->Proxy = Proxy;
		n->LoadedValue = LoadedValue;
		n->ValueHash = ValueHash;
		n->SourceDataDescriptor = SourceDataDescriptor;

		for (const TPair<FName, ASTChild>& ImageOperation : ImageOperations)
		{
			n->ImageOperations.Add(ImageOperation.Key, ASTChild(n, MapChild(ImageOperation.Value.child())));
		}
		
		return n;
	}


	//-------------------------------------------------------------------------------------------------
	uint64 ASTOpConstantResource::Hash() const
	{
		uint64 res = std::hash<uint64>()(uint64(Type));
		hash_combine(res, ValueHash);
		return res;
	}


	namespace
	{
 		/** Adds a constant mesh data to a program and returns its constant index. */
		int32 AddConstantMesh(FProgram& Program, const TSharedPtr<const FMesh>& MeshData, FLinkerOptions& Options)
		{
			auto AddMeshToProgram = [&Options, &Program](const TSharedPtr<FMesh>& Mesh)
			{
				// Use a map-based deduplication
				int32 MeshIndex = -1;
				TSharedPtr<const FMesh> MeshKey = Mesh;
				const int32* MeshIndexPtr = Options.MeshConstantMap.Find(MeshKey);
				if (!MeshIndexPtr)
				{
					MeshIndex = Program.ConstantMeshesPermanent.Add(Mesh);
					Options.MeshConstantMap.Add(Mesh, MeshIndex);
				}
				else
				{
					MeshIndex = *MeshIndexPtr;
				}

				check(MeshIndex >= 0)
				return Program.ConstantMeshContentIndices.Add(FConstantResourceIndex{(uint32)MeshIndex, 0});
			};
			
			// Split generated mesh data in 4 parts. Geometry and Pose and Physiscs and Metadata.
			// Inidices for a given rom are sorted by content flag value.
			static_assert(EMeshContentFlags::GeometryData < EMeshContentFlags::PoseData);
			static_assert(EMeshContentFlags::PoseData     < EMeshContentFlags::PhysicsData);
			static_assert(EMeshContentFlags::PhysicsData  < EMeshContentFlags::MetaData);

			int32 FirstIndex = Program.ConstantMeshContentIndices.Num();
			EMeshContentFlags MeshContentFlags = EMeshContentFlags::None;	
			// GeometryMesh
			{
				const EMeshCopyFlags GeometryDataCopyFlags = 
						EMeshCopyFlags::WithSurfaces      | 
						EMeshCopyFlags::WithVertexBuffers |
						EMeshCopyFlags::WithIndexBuffers  |
						EMeshCopyFlags::WithLayouts;

				TSharedPtr<FMesh> MeshGeometryData = MeshData->Clone(GeometryDataCopyFlags);

				// Copy geometry related additional buffers.
				for (const TPair<EMeshBufferType, FMeshBufferSet>& AdditionalBuffer : MeshData->AdditionalBuffers)
				{
					const bool bIsGeometryBufferType = 
							AdditionalBuffer.Key == EMeshBufferType::MeshLaplacianData    ||
							AdditionalBuffer.Key == EMeshBufferType::MeshLaplacianOffsets ||
							AdditionalBuffer.Key == EMeshBufferType::UniqueVertexMap;

					if (bIsGeometryBufferType)
					{
						MeshGeometryData->AdditionalBuffers.Emplace(AdditionalBuffer);
					}
				}

				MeshGeometryData->MeshIDPrefix = 0;
				AddMeshToProgram(MeshGeometryData);	
				EnumAddFlags(MeshContentFlags, EMeshContentFlags::GeometryData);
			}

			// Pose Mesh
			{
				const EMeshCopyFlags PoseDataCopyFlags = 
						EMeshCopyFlags::WithPoses  |
						EMeshCopyFlags::WithBoneMap;

				TSharedPtr<FMesh> MeshPoseData = MeshData->Clone(PoseDataCopyFlags);

				for (const TPair<EMeshBufferType, FMeshBufferSet>& AdditionalBuffer : MeshData->AdditionalBuffers)
				{
					const bool bIsPoseBufferType = 	
							AdditionalBuffer.Key == EMeshBufferType::SkeletonDeformBinding;

					if (bIsPoseBufferType)
					{
						MeshPoseData->AdditionalBuffers.Emplace(AdditionalBuffer);
					}
				}

				MeshPoseData->MeshIDPrefix = 0;
				AddMeshToProgram(MeshPoseData);	
				EnumAddFlags(MeshContentFlags, EMeshContentFlags::PoseData);
			}

			// PhysicsMeshData
			{
				const EMeshCopyFlags PhysicsDataCopyFlags = 
						EMeshCopyFlags::WithAdditionalPhysics |
						EMeshCopyFlags::WithPhysicsBody;

				TSharedPtr<FMesh> MeshPhysicsData = MeshData->Clone(PhysicsDataCopyFlags);
			
				// Copy components related additional buffers.
				for (const TPair<EMeshBufferType, FMeshBufferSet>& AdditionalBuffer : MeshData->AdditionalBuffers)
				{
					const bool bIsPhysicsBufferType = 
							AdditionalBuffer.Key == EMeshBufferType::PhysicsBodyDeformBinding   ||
							AdditionalBuffer.Key == EMeshBufferType::PhysicsBodyDeformSelection ||
							AdditionalBuffer.Key == EMeshBufferType::PhysicsBodyDeformOffsets;

					if (bIsPhysicsBufferType)
					{
						MeshPhysicsData->AdditionalBuffers.Emplace(AdditionalBuffer);
					}
				}

				MeshPhysicsData->MeshIDPrefix = 0;
				AddMeshToProgram(MeshPhysicsData);	
				EnumAddFlags(MeshContentFlags, EMeshContentFlags::PhysicsData);
			}

			// MetadataMesh Mesh
			{
				const EMeshCopyFlags MetadataDataCopyFlags = 
						EMeshCopyFlags::WithSurfaces         |
						EMeshCopyFlags::WithTags             |
						EMeshCopyFlags::WithSkeletonIDs      |
						EMeshCopyFlags::WithStreamedResources;

				TSharedPtr<FMesh> MeshMetadataData = MeshData->Clone(MetadataDataCopyFlags);
			
				// Add a descriptor MeshBufferSet to the metadata part to have formating info.
				{
					FMeshBufferSet VertexMeshFormat;
					const FMeshBufferSet& VertexBufferSet = MeshData->VertexBuffers;
					
					VertexMeshFormat.ElementCount = VertexBufferSet.ElementCount;

					const int32 NumVertexBuffers = VertexBufferSet.Buffers.Num();
					VertexMeshFormat.Buffers.SetNum(NumVertexBuffers);

					for (int32 BufferIndex = 0; BufferIndex < NumVertexBuffers; ++BufferIndex)
					{
						VertexMeshFormat.Buffers[BufferIndex].Channels = VertexBufferSet.Buffers[BufferIndex].Channels;	
						VertexMeshFormat.Buffers[BufferIndex].ElementSize = VertexBufferSet.Buffers[BufferIndex].ElementSize;	
					}
				
					MeshMetadataData->VertexBuffers = MoveTemp(VertexMeshFormat);
					EnumAddFlags(MeshMetadataData->VertexBuffers.Flags, EMeshBufferSetFlags::IsDescriptor);
				}

				{
					FMeshBufferSet IndexMeshFormat;
					const FMeshBufferSet& IndexBufferSet = MeshData->IndexBuffers;
					
					IndexMeshFormat.ElementCount = IndexBufferSet.ElementCount;

					const int32 NumIndexBuffers = IndexBufferSet.Buffers.Num();
					IndexMeshFormat.Buffers.SetNum(NumIndexBuffers);

					for (int32 BufferIndex = 0; BufferIndex < NumIndexBuffers; ++BufferIndex)
					{
						IndexMeshFormat.Buffers[BufferIndex].Channels = IndexBufferSet.Buffers[BufferIndex].Channels;	
						IndexMeshFormat.Buffers[BufferIndex].ElementSize = IndexBufferSet.Buffers[BufferIndex].ElementSize;	
					}
					
					MeshMetadataData->IndexBuffers = MoveTemp(IndexMeshFormat);
					EnumAddFlags(MeshMetadataData->IndexBuffers.Flags, EMeshBufferSetFlags::IsDescriptor);
				}

				MeshMetadataData->MeshIDPrefix = 0;
				AddMeshToProgram(MeshMetadataData);	
				EnumAddFlags(MeshContentFlags, EMeshContentFlags::MetaData);
			}


			// For now empty meshes are not discarded. A mesh rom index will be used even if empty.
			check(Program.ConstantMeshContentIndices.Num() - FirstIndex == 4);

			FMeshContentRange MeshContentRange;

			FMemory::Memzero(MeshContentRange);
			MeshContentRange.SetFirstIndex((uint32)FirstIndex);
			MeshContentRange.SetContentFlags(MeshContentFlags);
			MeshContentRange.MeshIDPrefix = MeshData->MeshIDPrefix;

			return Program.ConstantMeshes.Add(MeshContentRange);
		}   

		/** Adds a constant image data to a program and returns its constant index. */
		uint32 AddConstantImage(FProgram& Program, const TSharedPtr<const FImage>& Image, FLinkerOptions& Options)
		{
			MUTABLE_CPUPROFILER_SCOPE(AddConstantImage);

			check(Image->GetSizeX() * Image->GetSizeY() > 0);
		
			FImageOperator& ImOp = Options.ImageOperator;

			if (!Options.bSeparateImageMips)
			{	
				// Use a map-based deduplication only if we are splitting mips.
				int32 ImageIndex = Program.ConstantImageLODsPermanent.Add(Image);
				Options.ImageConstantMipMap.Add(Image, ImageIndex);
				
				FImageLODRange LODRange
				{
					.FirstIndex    = Program.ConstantImageLODIndices.Add({ uint32(ImageIndex), 0 }),
					.ImageSizeX    = Image->GetSizeX(),
					.ImageSizeY    = Image->GetSizeY(),
					.LODCount      = (uint8)Image->GetLODCount(),
					.NumLODsInTail = (uint8)Image->GetLODCount(),
					.Flags         = Image->Flags,
					.ImageFormat   = Image->GetFormat(),
				};

				return Program.ConstantImages.Add(LODRange);
			}

			auto AddImageToProgram = [&Program, &Options](const TSharedPtr<FImage>& Image) -> int32
			{
				MUTABLE_CPUPROFILER_SCOPE(Deduplicate);
				const int32* FoundStoreIndex = Options.ImageConstantMipMap.Find(Image);
				if (FoundStoreIndex)
				{
					return Program.ConstantImageLODIndices.Add({ uint32(*FoundStoreIndex), 0 });
				}
				else
				{
					int32 StoreIndex = Program.ConstantImageLODsPermanent.Add(Image);
					Options.ImageConstantMipMap.Add(Image, StoreIndex);

					return Program.ConstantImageLODIndices.Add({ uint32(StoreIndex), 0 });
				}
			};

			
			// Compute number of LODs to store.
			const int32 LODsToStore = Image->Flags & FImage::IF_CANNOT_BE_SCALED 
					? Image->GetLODCount()
					: FImage::GetMipmapCount(Image->GetSizeX(), Image->GetSizeY());

			// If the image cannot be scaled prevent from storing LODs that are not present.
			const int32 FirstLODToStoreInTail = 
					FMath::Min(LODsToStore, Image->DataStorage.ComputeFirstCompactedTailLOD());

			FImageLODRange LODRange
			{
				.FirstIndex    = Program.ConstantImageLODIndices.Num(),
				.ImageSizeX    = Image->GetSizeX(),
				.ImageSizeY    = Image->GetSizeY(),
				.LODCount      = (uint8)LODsToStore,
				.NumLODsInTail = (uint8)FMath::Max(0, LODsToStore - FirstLODToStoreInTail),
				.Flags         = Image->Flags,
				.ImageFormat   = Image->GetFormat(),
			};

			constexpr int32 MaxQuality = 4;

			int32 LOD = 0;

			// Special case for the LODs available in Image that will be stored as a single rom per LOD, just a copy. 
			for (; LOD < FirstLODToStoreInTail && LOD < Image->GetLODCount(); ++LOD)
			{
				AddImageToProgram(ImOp.ExtractMip(Image.Get(), LOD));
			}

			EImageFormat ImageUnCompressedFormat = GetUncompressedFormat(Image->GetFormat());

			// Extract Mip does not support RGB images if it needs to resize.
			ImageUnCompressedFormat = ImageUnCompressedFormat == EImageFormat::RGB_UByte
					? EImageFormat::RGBA_UByte
					: ImageUnCompressedFormat;

			TSharedPtr<const FImage> RawImage = Image;
			if (ImageUnCompressedFormat != Image->GetFormat())
			{
				int32 FirstLODToDecompress = FMath::Min(Image->GetLODCount() - 1, LOD);
				RawImage = ImOp.ImagePixelFormat(MaxQuality, RawImage.Get(), ImageUnCompressedFormat, FirstLODToDecompress);
				RawImage = ImOp.ExtractMip(RawImage.Get(), LOD - FirstLODToDecompress);
			}
			else
			{
				RawImage = ImOp.ExtractMip(RawImage.Get(), LOD);
			}

			// Store LODs that will be stored as a single rom per LOD and need to be generated.
			for (; LOD < FirstLODToStoreInTail; ++LOD)
			{
				AddImageToProgram(ImOp.ImagePixelFormat(MaxQuality, RawImage.Get(), Image->GetFormat()));

				// This is wrong, we are losing signal, but compilation performance gets badly affected on
				// models with lots of images if we scale down from the original image.  
				RawImage = ImOp.ExtractMip(RawImage.Get(), 1);
			}

			// Store LODs that will be stored together in a single rom.
			const int32 RawImageLOD = LOD;
			if (LOD < LODsToStore)
			{
				TSharedPtr<FImage> ImageToStore = ImOp.ExtractMip(RawImage.Get(), LOD - RawImageLOD);
				ImageToStore->DataStorage.SetNumLODs(LODRange.NumLODsInTail);
				++LOD;
				
				for (; LOD < LODsToStore; ++LOD)
				{
					TSharedPtr<FImage> SrcLODImage = ImOp.ExtractMip(RawImage.Get(), LOD - RawImageLOD);

					const int32 DestLOD = LOD - FirstLODToStoreInTail;
					
					// Some formats that are not block based will not allocate data when setting the number of LODs.
					// NOTE: We are working with uncompressed formats, probably this is not needed.
					if (ImageToStore->DataStorage.GetLOD(DestLOD).IsEmpty())
					{
						ImageToStore->DataStorage.ResizeLOD(DestLOD, SrcLODImage->DataStorage.GetLOD(0).Num());
					}
					
					TArrayView<uint8> SrcView  = SrcLODImage->DataStorage.GetLOD(0);
					TArrayView<uint8> DestView = ImageToStore->DataStorage.GetLOD(DestLOD);

					check(DestView.Num() == SrcView.Num());
					FMemory::Memcpy(DestView.GetData(), SrcView.GetData(), DestView.Num());	
				}

				AddImageToProgram(ImOp.ImagePixelFormat(MaxQuality, ImageToStore.Get(), Image->GetFormat()));
			}

			return Program.ConstantImages.Add(LODRange);
		}
	}

	//-------------------------------------------------------------------------------------------------
	void ASTOpConstantResource::Link(FProgram& Program, FLinkerOptions* Options)
	{
		MUTABLE_CPUPROFILER_SCOPE(ASTOpConstantResource_Link);

		if (!linkedAddress && !bLinkedAndNull)
		{
			if (Type == EOpType::ME_CONSTANT)
			{
				OP::MeshConstantArgs Args;
				FMemory::Memzero(Args);

				TSharedPtr<FMesh> MeshValue = StaticCastSharedPtr<const FMesh>(GetValue())->Clone();
				check(MeshValue);

				Args.Skeleton = -1;
				if (TSharedPtr<const FSkeleton> MeshSkeleton = MeshValue->GetSkeleton())
				{
					Args.Skeleton = Program.AddConstant(MeshSkeleton);
					MeshValue->SetSkeleton(nullptr);
				}

				Args.Value = AddConstantMesh(Program, MeshValue, *Options);
				int32 DataDescIndex = Options->AdditionalData.SourceMeshPerConstant.Add(SourceDataDescriptor);
				check(DataDescIndex == Args.Value);

				linkedAddress = (OP::ADDRESS)Program.OpAddress.Num();
				Program.OpAddress.Add((uint32)Program.ByteCode.Num());
				AppendCode(Program.ByteCode, Type);
				AppendCode(Program.ByteCode, Args);
			}
			else
			{
				OP::ResourceConstantArgs Args;
				FMemory::Memzero(Args);

				bool bValidData = true;

				switch (Type)
				{
				case EOpType::IM_CONSTANT:
				{
					TSharedPtr<const FImage> pTyped = StaticCastSharedPtr<const FImage>(GetValue());
					check(pTyped);

					if (pTyped->GetSizeX() * pTyped->GetSizeY() == 0)
					{
						// It's an empty or degenerated image, return a null operation.
						bValidData = false;
					}
					else
					{
						Args.value = AddConstantImage( Program, pTyped, *Options);

						int32 DataDescIndex = Options->AdditionalData.SourceImagePerConstant.Add(SourceDataDescriptor);
						check(DataDescIndex == Args.value);
					}

					break;
				}
				case EOpType::LA_CONSTANT:
				{
					TSharedPtr<const FLayout> pTyped = StaticCastSharedPtr<const FLayout>(GetValue());
					check(pTyped);
					Args.value = Program.AddConstant(pTyped);
					break;
				}
				case EOpType::MI_CONSTANT:
				{
					TSharedPtr<FMaterial> MaterialValue = StaticCastSharedPtr<const FMaterial>(GetValue())->Clone();
					check(MaterialValue);

					// Store linked child images into the material constant
					for (const TPair<FName, ASTChild>& ImageOperation : ImageOperations)
					{
						TVariant<OP::ADDRESS, TSharedPtr<const FImage>> NewImage;
						NewImage.Set<OP::ADDRESS>(ImageOperation.Value->linkedAddress);

						MaterialValue->ImageParameters.Add(FName(ImageOperation.Key), NewImage);
					}

					Args.value = Program.AddConstant(MaterialValue);
					
					break;
				}
				default:
					check(false);
				}

				if (bValidData)
				{
					linkedAddress = (OP::ADDRESS)Program.OpAddress.Num();
					Program.OpAddress.Add((uint32)Program.ByteCode.Num());
					AppendCode(Program.ByteCode, Type);
					AppendCode(Program.ByteCode, Args);
				}
				else
				{
					// Null op
					linkedAddress = 0;
					bLinkedAndNull = true;
				}
			}

			// Clear stored value to reduce memory usage.
			LoadedValue = nullptr;
			Proxy = nullptr;
		}
	}


	//-------------------------------------------------------------------------------------------------
	FImageDesc ASTOpConstantResource::GetImageDesc(bool, class FGetImageDescContext*) const
	{
		FImageDesc Result;

		if (Type == EOpType::IM_CONSTANT)
		{
			// TODO: cache to avoid disk loading
			TSharedPtr<const FImage> ConstImage = StaticCastSharedPtr<const FImage>(GetValue());
			Result.m_format = ConstImage->GetFormat();
			Result.m_lods = ConstImage->GetLODCount();
			Result.m_size = ConstImage->GetSize();
		}
		else if (Type == EOpType::MI_CONSTANT)
		{
			if (TMap<FName, ASTChild>::TConstIterator Iterator = ImageOperations.CreateConstIterator())
			{
				Result = Iterator.Value()->GetImageDesc();
			}
		}
		else
		{
			check(false);
		}

		return Result;
	}

	//-------------------------------------------------------------------------------------------------
	void ASTOpConstantResource::GetBlockLayoutSize(uint64 BlockId, int32* BlockX, int32* BlockY, FBlockLayoutSizeCache*)
	{
		switch (Type)
		{
		case EOpType::LA_CONSTANT:
		{
			TSharedPtr<const FLayout> pLayout = StaticCastSharedPtr<const FLayout>(GetValue());
			check(pLayout);

			if (pLayout)
			{
				int relId = pLayout->FindBlock(BlockId);
				if (relId >= 0)
				{
					*BlockX = pLayout->Blocks[relId].Size[0];
					*BlockY = pLayout->Blocks[relId].Size[1];
				}
				else
				{
					*BlockX = 0;
					*BlockY = 0;
				}
			}

			break;
		}
		default:
			check(false);
		}
	}


	//-------------------------------------------------------------------------------------------------
	void ASTOpConstantResource::GetLayoutBlockSize(int32* pBlockX, int32* pBlockY)
	{
		switch (Type)
		{

		case EOpType::IM_CONSTANT:
		{
			// We didn't find any layout.
			*pBlockX = 0;
			*pBlockY = 0;
			break;
		}

		default:
			checkf(false, TEXT("Instruction not supported"));
		}
	}


	//-------------------------------------------------------------------------------------------------
	bool ASTOpConstantResource::GetNonBlackRect(FImageRect& maskUsage) const
	{
		if (Type == EOpType::IM_CONSTANT)
		{
			// TODO: cache
			TSharedPtr<const FImage> pMask = StaticCastSharedPtr<const FImage>(GetValue());
			pMask->GetNonBlackRect(maskUsage);
			return true;
		}

		return false;
	}


	//-------------------------------------------------------------------------------------------------
	bool ASTOpConstantResource::IsImagePlainConstant(FVector4f& colour) const
	{
		bool res = false;
		switch (Type)
		{

		case EOpType::IM_CONSTANT:
		{
			TSharedPtr<const FImage> pImage = StaticCastSharedPtr<const FImage>(GetValue());
			if (pImage->GetSizeX() <= 0 || pImage->GetSizeY() <= 0)
			{
				res = true;
				colour = FVector4f(0.0f,0.0f,0.0f,1.0f);
			}
			else if (pImage->Flags & FImage::IF_IS_PLAIN_COLOUR_VALID)
			{
				if (pImage->Flags & FImage::IF_IS_PLAIN_COLOUR)
				{
					res = true;
					colour = pImage->Sample(FVector2f(0, 0));
				}
				else
				{
					res = false;
				}
			}
			else
			{
				if (pImage->IsPlainColour(colour))
				{
					res = true;
					pImage->Flags |= FImage::IF_IS_PLAIN_COLOUR;
				}

				pImage->Flags |= FImage::IF_IS_PLAIN_COLOUR_VALID;
			}
			break;
		}

		default:
			break;
		}

		return res;
	}


	//-------------------------------------------------------------------------------------------------
	ASTOpConstantResource::~ASTOpConstantResource()
	{
	}


	//-------------------------------------------------------------------------------------------------
	uint64 ASTOpConstantResource::GetValueHash() const
	{
		return ValueHash;
	}


	//-------------------------------------------------------------------------------------------------
	TSharedPtr<const FResource> ASTOpConstantResource::GetValue() const
	{
		if (LoadedValue)
		{
			return LoadedValue;
		}
		else
		{
			switch (Type)
			{

			case EOpType::IM_CONSTANT:
			{
				Ptr<TResourceProxy<FImage>> typedProxy = static_cast<TResourceProxy<FImage>*>(Proxy.get());
				TSharedPtr<const FImage> Resource = typedProxy->Get();
				return Resource;
			}

			default:
				check(false);
				break;
			}
		}

		return nullptr;
	}


	//-------------------------------------------------------------------------------------------------
	void ASTOpConstantResource::SetValue(const TSharedPtr<const FResource>& Value, FProxyFileContext* DiskCacheContext)
	{
		MUTABLE_CPUPROFILER_SCOPE(ASTOpConstantResource_SetValue);

		switch (Type)
		{
		case EOpType::IM_CONSTANT:
		{
			TSharedPtr<const FImage> Resource = StaticCastSharedPtr<const FImage>(Value);

			FOutputHashStream stream;
			{
				MUTABLE_CPUPROFILER_SCOPE(Serialize);
				FOutputArchive arch(&stream);
				FImage::Serialise(Resource.Get(), arch);
			}

			ValueHash = stream.GetHash();

			if (DiskCacheContext)
			{
				Proxy = new ResourceProxyTempFile<FImage>(Resource, *DiskCacheContext);
			}
			else
			{
				LoadedValue = Resource;
			}
			break;
		}

		case EOpType::ME_CONSTANT:
		{
			TSharedPtr<const FMesh> Resource = StaticCastSharedPtr<const FMesh>(Value);

			FOutputHashStream stream;
			{
				MUTABLE_CPUPROFILER_SCOPE(Serialize);
				FOutputArchive arch(&stream);
				FMesh::Serialise(Resource.Get(), arch);
			}

			ValueHash = stream.GetHash();

			LoadedValue = Resource;
			break;
		}

		case EOpType::LA_CONSTANT:
		{
			TSharedPtr<const FLayout> Resource = StaticCastSharedPtr<const FLayout>(Value);

			FOutputHashStream stream;
			{
				MUTABLE_CPUPROFILER_SCOPE(Serialize);
				FOutputArchive arch(&stream);
				FLayout::Serialise(Resource.Get(), arch);
			}

			ValueHash = stream.GetHash();

			LoadedValue = Resource;
			break;
		}
		
		case EOpType::MI_CONSTANT:
		{
			TSharedPtr<const FMaterial> Resource = StaticCastSharedPtr<const FMaterial>(Value);

			FOutputHashStream stream;
			{
				MUTABLE_CPUPROFILER_SCOPE(Serialize);
				FOutputArchive arch(&stream);
				FMaterial::Serialise(Resource.Get(), arch);
			}

			ValueHash = stream.GetHash();
			LoadedValue = Resource;

			break;
		}

		default:
			LoadedValue = Value;
			break;
		}
	}


	UE::Mutable::Private::Ptr<ImageSizeExpression> ASTOpConstantResource::GetImageSizeExpression() const
	{
		if (Type==EOpType::IM_CONSTANT)
		{
			Ptr<ImageSizeExpression> Res = new ImageSizeExpression;
			Res->type = ImageSizeExpression::ISET_CONSTANT;
			TSharedPtr<const FImage> Const = StaticCastSharedPtr<const FImage>(GetValue());
			Res->size = Const->GetSize();
			return Res;
		}

		return nullptr;
	}


	FSourceDataDescriptor ASTOpConstantResource::GetSourceDataDescriptor(FGetSourceDataDescriptorContext*) const
	{
		return SourceDataDescriptor;
	}

	
	ASTOp::EClosedMeshTest ASTOpConstantResource::IsClosedMesh(TMap<const ASTOp*, EClosedMeshTest>* Cache) const
	{
		if (Cache)
		{
			const ASTOp::EClosedMeshTest* Cached = Cache->Find(this);
			if (Cached)
			{
				return *Cached;
			}
		}

		ASTOp::EClosedMeshTest Result = EClosedMeshTest::Unknown;
		if (Type == EOpType::ME_CONSTANT)
		{
			TSharedPtr<const FMesh> Mesh = StaticCastSharedPtr<const FMesh>(GetValue());
			if (Mesh)
			{
				if (Mesh->IsClosed())
				{
					Result = EClosedMeshTest::Yes;
				}
				else
				{
					Result = EClosedMeshTest::No;
				}
			}
		}

		if (Cache)
		{
			Cache->Add(this,Result);
		}

		return Result;
	}

}
