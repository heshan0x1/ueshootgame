// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCO/UnrealMutableImageProvider.h"

#include "MuCO/CustomizableObjectSystem.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectPrivate.h"
#include "MuCO/UnrealToMutableTextureConversionUtils.h"
#include "MuCO/UnrealConversionUtils.h"
#include "MuCO/LoadUtils.h"
#include "MuCO/MutableMeshBufferUtils.h"
#include "MuR/Parameters.h"
#include "MuR/ImageTypes.h"
#include "MuR/Model.h"
#include "MuR/Mesh.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/BoneReference.h"
#include "Animation/Skeleton.h"
#include "ProfilingDebugging/IoStoreTrace.h"
#include "ReferenceSkeleton.h"
#include "ImageCoreUtils.h"
#include "TextureResource.h"
#include "Engine/Texture.h"


namespace
{
	void ConvertTextureUnrealPlatformToMutable(UE::Mutable::Private::FImage* OutResult, UTexture2D* Texture, uint8 MipmapsToSkip)
	{		
		check(Texture->GetPlatformData()->Mips[MipmapsToSkip].BulkData.IsBulkDataLoaded());

		int32 LODs = 1;
		int32 SizeX = Texture->GetSizeX() >> MipmapsToSkip;
		int32 SizeY = Texture->GetSizeY() >> MipmapsToSkip;
		check(SizeX > 0 && SizeY > 0);

		EPixelFormat Format = Texture->GetPlatformData()->PixelFormat;
		UE::Mutable::Private::EImageFormat MutableFormat = UE::Mutable::Private::EImageFormat::None;

		switch (Format)
		{
		case EPixelFormat::PF_B8G8R8A8: MutableFormat = UE::Mutable::Private::EImageFormat::BGRA_UByte; break;
			// This format is deprecated and using the enum fails to compile in some cases.
			//case ETextureSourceFormat::TSF_RGBA8: MutableFormat = UE::Mutable::Private::EImageFormat::RGBA_UByte; break;
		case EPixelFormat::PF_G8: MutableFormat = UE::Mutable::Private::EImageFormat::L_UByte; break;
		default:
			break;
		}

		// If not locked ReadOnly the Texture Source's FGuid can change, invalidating the texture's caching/shaders
		// making shader compile and cook times increase
		const void* pSource = Texture->GetPlatformData()->Mips[MipmapsToSkip].BulkData.LockReadOnly();

		if (pSource)
		{
			OutResult->Init(SizeX, SizeY, LODs, MutableFormat, UE::Mutable::Private::EInitializationType::NotInitialized);
			FMemory::Memcpy(OutResult->GetLODData(0), pSource, OutResult->GetLODDataSize(0));
			Texture->GetPlatformData()->Mips[MipmapsToSkip].BulkData.Unlock();
		}
		else
		{
			check(false);
			OutResult->Init(SizeX, SizeY, LODs, MutableFormat, UE::Mutable::Private::EInitializationType::Black);
		}
	}
}


UE::Mutable::Private::EImageFormat GetMutablePixelFormat(EPixelFormat InTextureFormat)
{
	switch (InTextureFormat)
	{
	case PF_B8G8R8A8: return UE::Mutable::Private::EImageFormat::BGRA_UByte;
	case PF_R8G8B8A8: return UE::Mutable::Private::EImageFormat::RGBA_UByte;
	case PF_DXT1: return UE::Mutable::Private::EImageFormat::BC1;
	case PF_DXT3: return UE::Mutable::Private::EImageFormat::BC2;
	case PF_DXT5: return UE::Mutable::Private::EImageFormat::BC3;
	case PF_BC4: return UE::Mutable::Private::EImageFormat::BC4;
	case PF_BC5: return UE::Mutable::Private::EImageFormat::BC5;
	case PF_G8: return UE::Mutable::Private::EImageFormat::L_UByte;
	case PF_ASTC_4x4: return UE::Mutable::Private::EImageFormat::ASTC_4x4_RGBA_LDR;
	case PF_ASTC_6x6: return UE::Mutable::Private::EImageFormat::ASTC_6x6_RGBA_LDR;
	case PF_ASTC_8x8: return UE::Mutable::Private::EImageFormat::ASTC_8x8_RGBA_LDR;
	case PF_ASTC_10x10: return UE::Mutable::Private::EImageFormat::ASTC_10x10_RGBA_LDR;
	case PF_ASTC_12x12: return UE::Mutable::Private::EImageFormat::ASTC_12x12_RGBA_LDR;
	default: return UE::Mutable::Private::EImageFormat::None;
	}
}


FUnrealMutableResourceProvider::FUnrealMutableResourceProvider(const TSharedRef<FBoneNames>& InBoneNames) : BoneNames(InBoneNames)
{
}


//-------------------------------------------------------------------------------------------------
TTuple<UE::Tasks::FTask, TFunction<void()>> FUnrealMutableResourceProvider::GetImageAsync(UTexture* Texture, uint8 MipmapsToSkip, TFunction<void(TSharedPtr<UE::Mutable::Private::FImage>)>& ResultCallback)
{
	// Thread: worker
	MUTABLE_CPUPROFILER_SCOPE(FUnrealMutableImageProvider::GetImageAsync);

	// Some data that may have to be copied from the GlobalExternalImages while it's locked
	IBulkDataIORequest* IORequest = nullptr;
	const int32 LODs = 1;

	EPixelFormat Format = EPixelFormat::PF_Unknown;
	int32 BulkDataSize = 0;

	UE::Mutable::Private::EImageFormat MutImageFormat = UE::Mutable::Private::EImageFormat::None;
	int32 MutImageDataSize = 0;

	auto TrivialReturn = []() -> TTuple<UE::Tasks::FTask, TFunction<void()>>
	{
		return MakeTuple(UE::Tasks::MakeCompletedTask<void>(), []() -> void {});
	};

	if (!Texture)
	{
		UE_LOG(LogMutable, Warning, TEXT("Invalid Image Parameter. Nullptr"));
		ResultCallback(CreateDummy());
		return Invoke(TrivialReturn);
	}
	
	UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
	if (!Texture2D)
	{
		UE_LOG(LogMutable, Warning, TEXT("Invalid Image Parameter [%s]. Is not a UTexture2D."), *Texture->GetName());
		ResultCallback(CreateDummy());
		return Invoke(TrivialReturn);
	}
	
#if WITH_EDITOR
	FTextureSource Source = Texture->Source.CopyTornOff();
	
	const int32 MipIndex = FMath::Min(static_cast<int32>(MipmapsToSkip), Source.GetNumMips() - 1);
	check(MipIndex >= 0);
		
	// In the editor the src data can be directly accessed
	TSharedPtr<UE::Mutable::Private::FImage> Image = MakeShared<UE::Mutable::Private::FImage>();

	FMutableSourceTextureData Tex(*Texture2D);
	EUnrealToMutableConversionError Error = ConvertTextureUnrealSourceToMutable(Image.Get(), Tex, MipIndex);
	if (Error != EUnrealToMutableConversionError::Success)
	{
		// This could happen in the editor, because some source textures may have changed while there was a background compilation.
		// We just show a warning and move on. This cannot happen during cooks, so it is fine.
		UE_LOG(LogMutable, Warning, TEXT("Failed to load some source texture data for image [%s]. Some materials may look corrupted."), *Texture->GetName());
	}

	ResultCallback(Image);
	return Invoke(TrivialReturn);
		
#else
	// It's safe to access TextureToLoad because ExternalImagesLock guarantees that the data in GlobalExternalImages is valid,
	// not being modified by the game thread at the moment and the texture cannot be GCed because of the AddReferencedObjects
	// in the FUnrealMutableImageProvider

	int32 MipIndex = MipmapsToSkip < Texture2D->GetPlatformData()->Mips.Num() ? MipmapsToSkip : Texture2D->GetPlatformData()->Mips.Num() - 1;
	check (MipIndex >= 0);

	// Mips in the mip tail are inlined and can't be streamed, find the smallest mip available.
	for (; MipIndex > 0; --MipIndex)
	{
		if (Texture2D->GetPlatformData()->Mips[MipIndex].BulkData.CanLoadFromDisk())
		{
			break;
		}
	}
	
	// Texture format and the equivalent mutable format
	Format = Texture2D->GetPlatformData()->PixelFormat;
	MutImageFormat = GetMutablePixelFormat(Format);

	// Check if it's a format we support
	if (MutImageFormat == UE::Mutable::Private::EImageFormat::None)
	{
		UE_LOG(LogMutable, Warning, TEXT("Failed to get Image Parameter [%s]. Unexpected image format. EImageFormat [%s]."), *Texture2D->GetName(), GetPixelFormatString(Format));
		ResultCallback(CreateDummy());
		return Invoke(TrivialReturn);
	}

	int32 SizeX = Texture2D->GetSizeX() >> MipIndex;
	int32 SizeY = Texture2D->GetSizeY() >> MipIndex;

	check(LODs == 1);
	TSharedPtr<UE::Mutable::Private::FImage> Image = MakeShared<UE::Mutable::Private::FImage>(SizeX, SizeY, LODs, MutImageFormat, UE::Mutable::Private::EInitializationType::NotInitialized);
	TArrayView<uint8> MutImageDataView = Image->DataStorage.GetLOD(0);

	// In a packaged game the bulk data has to be loaded
	// Get the actual file to read the mip 0 data, do not keep any reference to Texture2D because once outside of the lock
	// it may be GCed or changed. Just keep the actual file handle and some sizes instead of the texture
	FByteBulkData& BulkData = Texture2D->GetPlatformData()->Mips[MipIndex].BulkData;
	BulkDataSize = BulkData.GetBulkDataSize();
	check(BulkDataSize > 0);

	if (BulkDataSize != MutImageDataView.Num())
	{
		UE_LOG(LogMutable, Warning, TEXT("Failed to get Image Parameter [%s]. Bulk data size is different than the expected size. BulkData size [%d]. Mutable image data size [%d]."),
			*Texture->GetName(), BulkDataSize, MutImageDataSize);

		ResultCallback(CreateDummy());
		return Invoke(TrivialReturn);
	}

	// Create a streaming request if the data is not loaded or copy the mip data
	if (!BulkData.IsBulkDataLoaded())
	{
		UE::Tasks::FTaskEvent IORequestCompletionEvent(TEXT("Mutable_IORequestCompletionEvent"));

		TFunction<void(bool, IBulkDataIORequest*)> IOCallback =
			[
				MutImageDataView,
				MutImageFormat,
				Format,
				Image,
				BulkDataSize,	
				ResultCallback, // Notice ResultCallback is captured by copy
				IORequestCompletionEvent
			](bool bWasCancelled, IBulkDataIORequest* IORequest)
		{
			ON_SCOPE_EXIT
			{
				UE::Tasks::FTaskEvent EventCopy = IORequestCompletionEvent;
				EventCopy.Trigger();
			};
			
			// Should we do someting different than returning a dummy image if cancelled?
			if (bWasCancelled)
			{
				UE_LOG(LogMutable, Warning, TEXT("Failed to get external image. Cancelled IO Request"));
				ResultCallback(CreateDummy());
				return;
			}

			uint8* Results = IORequest->GetReadResults(); // required?

			if (Results && MutImageDataView.Num() == (int32)IORequest->GetSize())
			{
				check(BulkDataSize == (int32)IORequest->GetSize());
				check(Results == MutImageDataView.GetData());

				ResultCallback(Image);
				return;
			}

			if (!Results)
			{
				UE_LOG(LogMutable, Warning, TEXT("Failed to get Image Parameter. IO Request failed. Request results [%hhd]. Format: [%s]. MutableFormat: [%d]."),
					(Results != nullptr),
					GetPixelFormatString(Format),
					(int32)MutImageFormat);
			}
			else if (MutImageDataView.Num() != (int32)IORequest->GetSize())
			{
				UE_LOG(LogMutable, Warning, TEXT("Failed to get Image Parameter. Requested size is different than the expected size. RequestSize: [%lld]. ExpectedSize: [%d]. Format: [%s]. MutableFormat: [%d]."),
					IORequest->GetSize(),
					MutImageDataView.Num(),
					GetPixelFormatString(Format),
					(int32)MutImageFormat);
			}
			else
			{
				UE_LOG(LogMutable, Warning, TEXT("Failed to get Image Parameter."));
			}

			// Something failed when loading the bulk data, just return a dummy
			ResultCallback(CreateDummy());
		};
	
		// Is the resposability of the CreateStreamingRequest caller to delete the IORequest. 
		// This can *not* be done in the IOCallback because it would cause a deadlock so it is deferred to the returned
		// cleanup function. Another solution could be to spwan a new task that depends on the 
		// IORequestComplitionEvent which deletes it.
		TRACE_IOSTORE_METADATA_SCOPE_TAG(FName(Texture->GetPathName()));
		IORequest = BulkData.CreateStreamingRequest(EAsyncIOPriorityAndFlags::AIOP_High, &IOCallback, MutImageDataView.GetData());

		if (IORequest)
		{
			// Make the lambda mutable and set the IORequest pointer to null when deleted so it is safer 
			// agains multiple calls.
			const auto DeleteIORequest = [IORequest]() mutable -> void
			{
				if (IORequest)
				{
					delete IORequest;
				}
				
				IORequest = nullptr;
			};

			return MakeTuple(IORequestCompletionEvent, DeleteIORequest);

		}
		else
		{
			UE_LOG(LogMutable, Warning, TEXT("Failed to create an IORequest for a UTexture2D BulkData for an application-specific image parameter."));

			IORequestCompletionEvent.Trigger();
			
			ResultCallback(CreateDummy());
			return Invoke(TrivialReturn);
		}
	}
	else
	{
		// Bulk data already loaded
		const void* Data = (!BulkData.IsLocked()) ? BulkData.LockReadOnly() : nullptr; // TODO: Retry if it fails?
		
		if (Data)
		{
			FMemory::Memcpy(MutImageDataView.GetData(), Data, BulkDataSize);

			BulkData.Unlock();
			ResultCallback(Image);
			return Invoke(TrivialReturn);
		}
		else
		{
			UE_LOG(LogMutable, Warning, TEXT("Failed to get Image Parameter. Bulk data already locked or null."));
			ResultCallback(CreateDummy());
			return Invoke(TrivialReturn);
		}
	}
#endif
}


//-------------------------------------------------------------------------------------------------
TTuple<UE::Tasks::FTask, TFunction<void()>> FUnrealMutableResourceProvider::GetReferencedImageAsync(int32 Id, uint8 MipmapsToSkip, TFunction<void(TSharedPtr<UE::Mutable::Private::FImage>)>& ResultCallback)
{
	// Thread: worker
	MUTABLE_CPUPROFILER_SCOPE(FUnrealMutableImageProvider::GetReferencedImageAsync);

	auto TrivialReturn = []() -> TTuple<UE::Tasks::FTask, TFunction<void()>>
	{
		return MakeTuple(UE::Tasks::MakeCompletedTask<void>(), []() -> void {});
	};

#if WITH_EDITOR
	FScopeLock Lock(&RuntimeReferencedLock);

	if (!Images.IsValidIndex(Id))
	{
		// This could happen in the editor, because some source textures may have changed while there was a background compilation.
		// We just show a warning and move on. This cannot happen during cooks, so it is fine.
		UE_LOG(LogMutable, Warning, TEXT("Failed to load Referenced Image [%i]. Invalid id."), Id);
		ResultCallback(CreateDummy());
		return Invoke(TrivialReturn);
	}

	UTexture* Texture = Images[Id].Get();
	if (!Texture)
	{
		UE_LOG(LogMutable, Warning, TEXT("Invalid Referenced Image [%i]. Nullptr."), Id);
		ResultCallback(CreateDummy());
		return Invoke(TrivialReturn);
	}
	
	UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
	if (!Texture2D)
	{
		UE_LOG(LogMutable, Warning, TEXT("Invalid Referenced Image [%i, %s]. Is not a UTexture2D."), Id, *Texture->GetName());
		ResultCallback(CreateDummy());
		return Invoke(TrivialReturn);
	}
	
	const int32 MipIndex = FMath::Min(static_cast<int32>(MipmapsToSkip), Texture2D->Source.CopyTornOff().GetNumMips() - 1);
	check(MipIndex >= 0);

	TSharedPtr<UE::Mutable::Private::FImage> Image = MakeShared<UE::Mutable::Private::FImage>();
	
	FMutableSourceTextureData Tex(*Texture2D);
	EUnrealToMutableConversionError Error = ConvertTextureUnrealSourceToMutable(Image.Get(), Tex, MipIndex);
	if (Error != EUnrealToMutableConversionError::Success)
	{
		// This could happen in the editor, because some source textures may have changed while updating.
		// We just show a warning and move on. This cannot happen during cooks, so it is fine.
		UE_LOG(LogMutable, Warning, TEXT("Failed to load some source texture data for Referenced Image [%i, %s]. Some textures may be corrupted."), Id, *Texture->GetName());
		
		ResultCallback(CreateDummy());
		return Invoke(TrivialReturn);
	}

	ResultCallback(Image);
	return Invoke(TrivialReturn);
#else // WITH_EDITOR

	// Not supported outside editor yet.
	UE_LOG(LogMutable, Warning, TEXT("Failed to get Reference Image. Only supported in editor."));

	ResultCallback(CreateDummy());
	return Invoke(TrivialReturn);

#endif
}


// This should mantain parity with the descriptor of the images generated by GetImageAsync 
UE::Mutable::Private::FExtendedImageDesc FUnrealMutableResourceProvider::GetImageDesc(UTexture* Texture)
{
	MUTABLE_CPUPROFILER_SCOPE(FUnrealMutableImageProvider::GetImageDesc);

	if (!Texture)
	{
		return CreateDummyDesc();
	}

	UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
	if (!Texture2D)
	{
		return CreateDummyDesc();
	}
	
#if WITH_EDITOR
	const FTextureSource& Source = Texture->Source.CopyTornOff();

	const UE::Mutable::Private::FImageSize ImageSize = UE::Mutable::Private::FImageSize(Source.GetSizeX(), Source.GetSizeY());
	const uint8 LODs = 1;

	return UE::Mutable::Private::FExtendedImageDesc { UE::Mutable::Private::FImageDesc { ImageSize, UE::Mutable::Private::EImageFormat::None, LODs }, 0 };
#else
	UE::Mutable::Private::FExtendedImageDesc Result;	

	// It's safe to access TextureToLoad because ExternalImagesLock guarantees that the data in GlobalExternalImages is valid,
	// not being modified by the game thread at the moment and the texture cannot be GCed because of the AddReferencedObjects
	// in the FUnrealMutableImageProvider

	const int32 TextureToLoadNumMips = Texture2D->GetPlatformData()->Mips.Num();

	int32 FirstLODAvailable = 0;
	for (; FirstLODAvailable < TextureToLoadNumMips; ++FirstLODAvailable)
	{
		if (Texture2D->GetPlatformData()->Mips[FirstLODAvailable].BulkData.DoesExist())
		{
			break;
		}
	}
	
	// Texture format and the equivalent mutable format
	const EPixelFormat Format = Texture2D->GetPlatformData()->PixelFormat;
	const UE::Mutable::Private::EImageFormat MutableFormat = GetMutablePixelFormat(Format);

	// Check if it's a format we support
	if (MutableFormat == UE::Mutable::Private::EImageFormat::None)
	{
		UE_LOG(LogMutable, Warning, TEXT("Failed to get Image Parameter descriptor. Unexpected image format. EImageFormat [%s]."), GetPixelFormatString(Format));
		return CreateDummyDesc();
	}

	const UE::Mutable::Private::FImageDesc ImageDesc = UE::Mutable::Private::FImageDesc 
		{ UE::Mutable::Private::FImageSize(Texture2D->GetSizeX(), Texture2D->GetSizeY()), MutableFormat, 1 }; 

	Result = UE::Mutable::Private::FExtendedImageDesc { ImageDesc, (uint8)FirstLODAvailable }; 

	return Result;
#endif
}


TTuple<UE::Tasks::FTask, TFunction<void()>> FUnrealMutableResourceProvider::GetMeshAsync(USkeletalMesh* SkeletalMesh, int32 LODIndex, int32 SectionIndex, TFunction<void(TSharedPtr<UE::Mutable::Private::FMesh>)>& ResultCallback)
{
	// Thread: worker
	MUTABLE_CPUPROFILER_SCOPE(FUnrealMutableImageProvider::GetMeshAsync);

	auto TrivialReturn = []() -> TTuple<UE::Tasks::FTask, TFunction<void()>>
	{
		return MakeTuple(UE::Tasks::MakeCompletedTask<void>(), []() -> void {});
	};

	TSharedPtr<UE::Mutable::Private::FMesh> Result = MakeShared<UE::Mutable::Private::FMesh>();

	if (!SkeletalMesh)
	{
		UE_LOG(LogMutable, Warning, TEXT("Invalid Mesh Parameter. Nullptr."));
		ResultCallback(Result);
		return Invoke(TrivialReturn);
	}
	
	UE::Tasks::FTask ConversionTask = UnrealConversionUtils::ConvertSkeletalMeshFromRuntimeData(SkeletalMesh, LODIndex, SectionIndex, Result, BoneNames);

	return MakeTuple(
		// Some post-game conversion stuff can happen here in a worker thread
		UE::Tasks::Launch(TEXT("MutableMeshParameterLoadPostGame"),
			[ResultCallback, Result]()
			{
				ResultCallback(Result);
			},
			ConversionTask),

		// Cleanup code that will be called after the result is received in calling code.
		[]()
		{
		}
	);
}


#if WITH_EDITOR
void FUnrealMutableResourceProvider::CacheRuntimeReferencedImages(const TArray<TSoftObjectPtr<UTexture2D>>& RuntimeReferencedTextures)
{
	check(IsInGameThread());
	
	MUTABLE_CPUPROFILER_SCOPE(FUnrealMutableImageProvider::CacheRuntimeReferencedImages);
	
	FScopeLock Lock(&RuntimeReferencedLock);

	Images.Reset();
	for (const TSoftObjectPtr<UTexture2D>& RuntimeReferencedTexture : RuntimeReferencedTextures)
	{
		UTexture2D* Texture = RuntimeReferencedTexture.Get(); // Is already loaded.
		if (!Texture)
		{
			UE_LOG(LogMutable, Warning, TEXT("Runtime Referenced Texture [%s] was not async loaded. Forcing load sync."), *RuntimeReferencedTexture->GetPathName());
			
			Texture = UE::Mutable::Private::LoadObject(RuntimeReferencedTexture);
			if (!Texture)
			{
				UE_LOG(LogMutable, Warning, TEXT("Failed to force load sync [%s]."), *RuntimeReferencedTexture->GetPathName());
				continue;
			}
		}

		Images.Add(TStrongObjectPtr(Texture)); // Perform a CopyTornOff. Once done, we no longer need the texture loaded.
	}
}
#endif


TSharedPtr<UE::Mutable::Private::FImage> FUnrealMutableResourceProvider::CreateDummy()
{
	// Create a dummy image
	const int32 Size = DUMMY_IMAGE_DESC.m_size[0];
	const int32 CheckerSize = 4;
	constexpr int32 CheckerTileCount = 2;
	
#if !UE_BUILD_SHIPPING
	uint8 Colors[CheckerTileCount][4] = {{255, 255, 0, 255}, {0, 0, 255, 255}};
#else
	uint8 Colors[CheckerTileCount][4] = {{255, 255, 0, 0}, {0, 0, 255, 0}};
#endif

	TSharedPtr<UE::Mutable::Private::FImage> pResult = MakeShared<UE::Mutable::Private::FImage>(Size, Size, DUMMY_IMAGE_DESC.m_lods, DUMMY_IMAGE_DESC.m_format, UE::Mutable::Private::EInitializationType::NotInitialized);

	check(pResult->GetLODCount() == 1);
	check(pResult->GetFormat() == UE::Mutable::Private::EImageFormat::RGBA_UByte || pResult->GetFormat() == UE::Mutable::Private::EImageFormat::BGRA_UByte);
	uint8* pData = pResult->GetLODData(0);
	for (int32 X = 0; X < Size; ++X)
	{
		for (int32 Y = 0; Y < Size; ++Y)
		{
			int32 CheckerIndex = ((X / CheckerSize) + (Y / CheckerSize)) % CheckerTileCount;
			pData[0] = Colors[CheckerIndex][0];
			pData[1] = Colors[CheckerIndex][1];
			pData[2] = Colors[CheckerIndex][2];
			pData[3] = Colors[CheckerIndex][3];
			pData += 4;
		}
	}

	return pResult;
}


UE::Mutable::Private::FExtendedImageDesc FUnrealMutableResourceProvider::CreateDummyDesc()
{
	return UE::Mutable::Private::FExtendedImageDesc{ {DUMMY_IMAGE_DESC}, 0 };
}

