// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterAnalytics.h"

#include "Editor/EditorEngine.h"
#include "Algo/Find.h"
#include "Algo/Transform.h"
#include "Engine/World.h"
#include "MetaHumanCharacterEditorActorInterface.h"
#include "MetaHumanCharacterEditorActor.h"
#include "MetaHumanCharacterEditorLog.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterTextureSynthesis.h"
#include "MetaHumanCharacterBodyTextureUtils.h"
#include "MetaHumanCharacterSkelMeshUtils.h"
#include "MetaHumanRigEvaluatedState.h"
#include "MetaHumanCharacterInstance.h"
#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanCharacterPaletteUnpackHelpers.h"
#include "MetaHumanCharacterThumbnailRenderer.h"
#include "MetaHumanCollection.h"
#include "MetaHumanCollectionEditorPipeline.h"
#include "MetaHumanWardrobeItem.h"
#include "MetaHumanIdentity.h"
#include "MetaHumanIdentityParts.h"
#include "MetaHumanIdentityPose.h"
#include "MetaHumanInvisibleDrivingActor.h"
#include "MetaHumanCharacterEditorWardrobeSettings.h"
#include "MetaHumanCharacterPalette.h"
#include "MetaHumanCollectionPipeline.h"
#include "MetaHumanDefaultPipelineBase.h"
#include "MetaHumanGeometryRemoval.h"
#include "MetaHumanCommonDataUtils.h"
#include "Subsystem/MetaHumanCharacterBuild.h"
#include "DCC/MetaHumanCharacterDCCExport.h"
#include "Verification/MetaHumanCharacterValidation.h"
#include "Interfaces/IPluginManager.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "Engine/AssetManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Components/SkeletalMeshComponent.h"
#include "GroomComponent.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "MeshDescription.h"
#include "SkelMeshDNAUtils.h"
#include "DNAUtils.h"
#include "Cloud/MetaHumanTextureSynthesisServiceRequest.h"
#include "Cloud/MetaHumanARServiceRequest.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Misc/ScopedSlowTask.h"
#include "ImageCoreUtils.h"
#include "AssetToolsModule.h"
#include "ObjectTools.h"
#include "InterchangeDnaModule.h"
#include "Logging/StructuredLog.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "StaticMeshAttributes.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "Misc/ObjectThumbnail.h"
#include "UObject/ObjectSaveContext.h"
#include "RenderingThread.h"
#include "DNAUtilities.h"
#include "Tasks/Task.h"
#include "ChaosOutfitAsset/BodyUserData.h"
#include "ScopedTransaction.h"
#include "TextureResource.h"
#include "ImageUtils.h"
#include "Animation/MorphTarget.h"
#include "DNAReaderAdapter.h"
#include "ChaosOutfitAsset/OutfitAsset.h"
#include "AnimationRuntime.h"
#include "MetaHumanCoreTechMeshUtils.h"
#include "Subsystems/AssetEditorSubsystem.h"

extern UNREALED_API UEditorEngine* GEditor;

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditor"

static const TCHAR* AutoriggingTransactionContext = TEXT("AutoriggingTransaction");
static constexpr bool bOutfitResizingRequiresNormals = false;

namespace UE::MetaHuman
{
	static TAutoConsoleVariable<bool> CVarMHCharacterSaveAutoRiggedDNA
	{
		TEXT("mh.Character.SaveAutoRiggedDNA"),
		false,
		TEXT("Set to true to save the DNA file returned by the Auto-Rigging service to a file in the Saved directory."),
		ECVF_Default
	};

	static TAutoConsoleVariable<bool> CvarUpdateAllLODsOnFaceEdit
	{
		TEXT("mh.Character.UpdateAllLODsOnFaceEdit"),
		false,
		TEXT("Set to true to update all LODs on skeletal mesh during face edit. Otherwise only LOD0 is updated."),
		ECVF_Default
	};

	static TAutoConsoleVariable<bool> CvarMHLoadMeshesFromDNA
	{
		TEXT("mh.Character.LoadFaceFromDNA"),
		false,
		TEXT("If enabled, Skeletal Meshes will be created from the DNA files when opening MHC asset editor."),
		ECVF_Default
	};

	static TAutoConsoleVariable<bool> CVarMHCharacterPreviewHiddenFaces
	{
		TEXT("mh.Character.PreviewHiddenFaces"),
		true,
		TEXT("If enabled, hidden faces will be applied to the editor preview."),
		ECVF_Default
	};

	static TSharedPtr<IDNAReader> GetDNAReader(USkeletalMesh* InSkeletalMesh)
	{
		UAssetUserData* UserData = InSkeletalMesh->GetAssetUserDataOfClass(UDNAAsset::StaticClass());
		if (!UserData)
		{
			return nullptr;
		}

		UDNAAsset* DNAAsset = CastChecked<UDNAAsset>(UserData);
		return DNAAsset->GetGeometryReader();
	}

	static TSharedPtr<SNotificationItem> ShowNotification(const FText& InMessage, SNotificationItem::ECompletionState InState, float InExpireDuration = 3.5f)
	{
		if (InState == SNotificationItem::ECompletionState::CS_Fail)
		{
			UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("%s"), *InMessage.ToString());
		}
		else
		{
			UE_LOG(LogMetaHumanCharacterEditor, Display, TEXT("%s"), *InMessage.ToString());
		}

		FNotificationInfo Info{ InMessage };
		Info.FadeInDuration = 0.1f;
		Info.FadeOutDuration = 3.0f;

		if (InState == SNotificationItem::CS_Pending)
		{
			Info.bFireAndForget = false;
			Info.bUseThrobber = true;
		}
		else
		{
			Info.ExpireDuration = InExpireDuration;
			Info.bFireAndForget = true;
			Info.bUseThrobber = false;
		}

		Info.bUseSuccessFailIcons = true;
		Info.bUseLargeFont = true;

		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

		if (!NotificationItem)
		{
			return nullptr;
		}

		NotificationItem->SetCompletionState(InState);

		if (InState != SNotificationItem::CS_Pending)
		{
			NotificationItem->ExpireAndFadeout();
		}

		return NotificationItem;
	}

	static int32 MapTextureHFToStateHFIndex(const FMetaHumanCharacterIdentity::FState& InFaceState, int32 InTextureHFIndex)
	{
		// Ensure index does not exceed variant count
		if (InTextureHFIndex >= InFaceState.GetNumHighFrequencyVariants())
		{
			UE_LOG(LogMetaHumanCharacterEditor, Warning, TEXT("Number of character Identity variants %i less than input texture index %i, max variant will be used"), InFaceState.GetNumHighFrequencyVariants(), InTextureHFIndex);
			return InFaceState.GetNumHighFrequencyVariants() - 1;
		}

		// Assume all variants are in the same order
		return InTextureHFIndex;
	}

	static int32 GetEyelashesVariantIndex(const FMetaHumanCharacterIdentity::FState& InFaceState, EMetaHumanCharacterEyelashesType InEyelashesType)
	{
		int32 VariantIndex = INDEX_NONE;
		// Check if eyelashes count is the same as the number of types in the enum, not counting "None".
		if (InFaceState.GetVariantsCount("eyelashes") == static_cast<int32>(EMetaHumanCharacterEyelashesType::Count) - 1 && InEyelashesType != EMetaHumanCharacterEyelashesType::Count)
		{
			VariantIndex = static_cast<int32>(InEyelashesType) - 1; // -1 when None is selected.
		}
		else
		{
			UE_LOG(LogMetaHumanCharacterEditor, Warning, TEXT("Number of character identity eyelashes variants does not match input eyelahses Type, None will be used"));
		}
		return VariantIndex;
	}

	/** Makes a map of all Joints from DNA to Bones in Reference Skeleton. */
	static void MapJoints(const USkeletalMesh* InTargetSkelMesh, const dna::Reader* InDNAReader, TArray<int32>& OutRLJointToUEBoneIndices)
	{
		const FReferenceSkeleton* RefSkeleton = &InTargetSkelMesh->GetRefSkeleton();
		uint32 JointCount = InDNAReader->getJointCount();

		// Map Joints to Bones.
		OutRLJointToUEBoneIndices.Empty(JointCount);
		for (uint32 JntIndex = 0; JntIndex < JointCount; ++JntIndex)
		{
			const FString BoneNameFStr = InDNAReader->getJointName(JntIndex).c_str();
			const FName BoneName = FName{ *BoneNameFStr };
			const int32 BoneIndex = RefSkeleton->FindBoneIndex(BoneName);

			// BoneIndex can be INDEX_NONE;
			// We can safely put it into the map with other indices, it will be handled in the Evaluate method.
			OutRLJointToUEBoneIndices.Add(BoneIndex);
		}
	}

	/**
	 * Ticks internal systems waiting for the cloud requests to complete, effectively blocking until InWaitCondition returns false
	 */
	static void WaitForCloudRequests(TFunction<bool()> InWaitCondition)
	{
		while (InWaitCondition())
		{
			const float DeltaTime = 0.1f;

			// Handles HTTP requests waiting to be processed
			FHttpModule::Get().GetHttpManager().Tick(DeltaTime);

			// Handles authentication requests waiting to be processed
			ServiceAuthentication::TickAuthClient();

			// Update tasks in the task graph as MH service request uses AsyncTask to defer callbacks onto the game thread
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

			// Update tickers and delegates
			FTSTicker::GetCoreTicker().Tick(DeltaTime);

			// Waits a little bit to give time for background tasks to finish
			FPlatformProcess::Sleep(DeltaTime);
		}
	}

	static TArray<uint8> GetTextureRawBytes(const UTexture2D* InTexture)
	{
		// get the raw data from the supplied texture
		TArray<uint8> OutBytes;

		if (!InTexture)
		{
			return OutBytes;
		}

		const FTexturePlatformData* PlatformData = InTexture->GetPlatformData();

		if (!PlatformData || PlatformData->Mips.Num() == 0)
		{
			return OutBytes;
		}

		const FTexture2DMipMap& Mip = PlatformData->Mips[0];
		const int32 Width = Mip.SizeX;
		const int32 Height = Mip.SizeY;
		OutBytes.SetNumUninitialized(Width * Height * 4);

		const void* RawData = Mip.BulkData.LockReadOnly();
		FMemory::Memcpy(OutBytes.GetData(), RawData, OutBytes.Num());
		Mip.BulkData.Unlock();

		return OutBytes;
	}

	bool CompareBuffers(const FSharedBuffer& InBuffer1, const FSharedBuffer& InBuffer2, int32 InTolerance = 1)
	{
		if (InBuffer1.GetSize() != InBuffer2.GetSize())
		{
			return false;
		}

		const uint8* Data1 = static_cast<const uint8*>(InBuffer1.GetData());
		const uint8* Data2 = static_cast<const uint8*>(InBuffer2.GetData());

		for (int32 I = 0; I < InBuffer1.GetSize(); ++I)
		{
			if (FMath::Abs(int32(Data1[I]) - int32(Data2[I])) > InTolerance)
			{
				return false; 
			}
		}

		return true;
	}
}

FMetaHumanCharacterEditorData::FMetaHumanCharacterEditorData()
	: FaceDnaToSkelMeshMap(MakeShared<FDNAToSkelMeshMap>())
	, BodyDnaToSkelMeshMap(MakeShared<FDNAToSkelMeshMap>())
	, FaceState(MakeShared<FMetaHumanCharacterIdentity::FState>())
	, BodyState(MakeShared<FMetaHumanCharacterBodyIdentity::FState>())
{
}

FRemoveRigCommandChange::FRemoveRigCommandChange(
	const TArray<uint8>& InOldDNABuffer,
	TSharedRef<const FMetaHumanCharacterIdentity::FState> InOldState,
	const TArray<uint8>& InOldBodyDNABuffer,
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InOldBodyState,
	TNotNull<UMetaHumanCharacter*> InCharacter)
	: OldDNABuffer{ InOldDNABuffer }
	, NewDNABuffer{ InCharacter->GetFaceDNABuffer() }
	, OldBodyDNABuffer(InOldBodyDNABuffer)
	, NewBodyDNABuffer(InCharacter->GetBodyDNABuffer())
	, OldState{ InOldState }
	, NewState{ UMetaHumanCharacterEditorSubsystem::Get()->CopyFaceState(InCharacter) }
	, OldBodyState{ InOldBodyState }
	, NewBodyState{ UMetaHumanCharacterEditorSubsystem::Get()->CopyBodyState(InCharacter)}
	, bFixedBodyType(InCharacter->bFixedBodyType)
{}

bool FRemoveRigCommandChange::HasExpired(UObject* InObject) const
{
	bool bEditorExpired = true;
	UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(InObject);

	if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		IAssetEditorInstance* AssetEditor = AssetEditorSubsystem->FindEditorForAsset(Character, false /*bFocusIfOpen=*/);
		bEditorExpired = AssetEditor == nullptr;
	}

	return bEditorExpired;
}

void FRemoveRigCommandChange::ApplyChange(UObject* InObject,
	const TArray<uint8>& InDNABuffer,
	TSharedRef<const FMetaHumanCharacterIdentity::FState> InState,
	const TArray<uint8>& InBodyDNABuffer,
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InBodyState)
{
	UMetaHumanCharacter* Character = CastChecked<UMetaHumanCharacter>(InObject);

	if (InBodyDNABuffer.IsEmpty())
	{
		UMetaHumanCharacterEditorSubsystem::Get()->RemoveBodyRig(Character);
	}
	else
	{
		TArray<uint8> BufferCopy;
		BufferCopy.SetNumUninitialized(InBodyDNABuffer.Num());
		FMemory::Memcpy(BufferCopy.GetData(), InBodyDNABuffer.GetData(), InBodyDNABuffer.Num());
		UMetaHumanCharacterEditorSubsystem::Get()->CommitBodyDNA(Character, ReadDNAFromBuffer(&BufferCopy, EDNADataLayer::All).ToSharedRef(), bFixedBodyType);
	}
	
	UMetaHumanCharacterEditorSubsystem::Get()->CommitBodyState(Character, InBodyState, UMetaHumanCharacterEditorSubsystem::EBodyMeshUpdateMode::Minimal);

	
	// if an empty buffer, remove the rig from the character (special case)
	if (InDNABuffer.IsEmpty())
	{
		UMetaHumanCharacterEditorSubsystem::Get()->RemoveFaceRig(Character);
	}
	else
	{
		TArray<uint8> BufferCopy;
		BufferCopy.SetNumUninitialized(InDNABuffer.Num());
		FMemory::Memcpy(BufferCopy.GetData(), InDNABuffer.GetData(), InDNABuffer.Num());
		UMetaHumanCharacterEditorSubsystem::Get()->CommitFaceDNA(Character, ReadDNAFromBuffer(&BufferCopy, EDNADataLayer::All).ToSharedRef());
	}

	// reset the face state
	UMetaHumanCharacterEditorSubsystem::Get()->CommitFaceState(Character, InState);
}

FAutoRigCommandChange::FAutoRigCommandChange(
	const TArray<uint8>& InOldDNABuffer,
	TSharedRef<const FMetaHumanCharacterIdentity::FState> InOldState,
	const TArray<uint8>& InOldBodyDNABuffer,
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InOldBodyState,
	TNotNull<UMetaHumanCharacter*> InCharacter)
	: FRemoveRigCommandChange(InOldDNABuffer, InOldState, InOldBodyDNABuffer, InOldBodyState, InCharacter)
{
}

void UMetaHumanCharacterEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	FCoreUObjectDelegates::OnObjectPreSave.AddWeakLambda(this, [this](UObject* InObject, FObjectPreSaveContext InObjectPreSaveContext)
	{
		if (UMetaHumanCharacter* Character = Cast<UMetaHumanCharacter>(InObject))
		{
			SaveCharacterThumbnails(Character);
		}
	});
}

void UMetaHumanCharacterEditorSubsystem::SaveCharacterThumbnails(TNotNull<UMetaHumanCharacter*> InCharacter)
{
	if (!CharacterDataMap.Contains(InCharacter))
	{
		return;
	}

	UMetaHumanCharacterThumbnailRenderer* ThumbnailRenderer = nullptr;

	if (FThumbnailRenderingInfo* RenderInfo = UThumbnailManager::Get().GetRenderingInfo(InCharacter))
	{
		ThumbnailRenderer = Cast<UMetaHumanCharacterThumbnailRenderer>(RenderInfo->Renderer);
	}

	if (!ThumbnailRenderer)
	{
		return;
	}

	const TArray<EMetaHumanCharacterThumbnailCameraPosition> ThumbnailPositionsToUpdate =
	{
		EMetaHumanCharacterThumbnailCameraPosition::Face,
		EMetaHumanCharacterThumbnailCameraPosition::Body,
		EMetaHumanCharacterThumbnailCameraPosition::Character_Face,
		EMetaHumanCharacterThumbnailCameraPosition::Character_Body,
	};

	UPackage* CharacterPackage = InCharacter->GetPackage();
	FThumbnailMap ThumbnailMap;
	TArray<FName> ObjectNames;
	Algo::Transform(
		ThumbnailPositionsToUpdate,
		ObjectNames,
		[ObjectPath = InCharacter->GetPathName()](EMetaHumanCharacterThumbnailCameraPosition Position)
		{
			return UMetaHumanCharacter::GetThumbnailPathInPackage(ObjectPath, Position);
		});

	for (int32 i = 0; i < ThumbnailPositionsToUpdate.Num(); ++i)
	{
		// Empty thumbnail object to write to
		FObjectThumbnail ThumbnailObject;

		ThumbnailRenderer->CameraPosition = ThumbnailPositionsToUpdate[i];

		ThumbnailTools::RenderThumbnail(
			InCharacter,
			ThumbnailTools::DefaultThumbnailSize,
			ThumbnailTools::DefaultThumbnailSize,
			ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush,
			nullptr,
			&ThumbnailObject
		);

		// Save thumbnail to the package
		ThumbnailTools::CacheThumbnail(
			ObjectNames[i].ToString(),
			&ThumbnailObject,
			CharacterPackage);
	}

	// Thumbnail rendering enqueues a rendering command, wait until it's complete
	FlushRenderingCommands();
}

void UMetaHumanCharacterEditorSubsystem::RunCharacterEditorPipelineForPreview(UMetaHumanCharacter* InCharacter) const
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "RunCharacterEditorPipelineForPreview called with invalid character");
		return;
	}

	if (!ensure(InCharacter->GetInternalCollection()))
	{
		// This should never be null, but avoid crashing if it is
		return;
	}

	if (!InCharacter->GetInternalCollection()->GetEditorPipeline())
	{
		return;
	}
	
	FInstancedStruct BuildInput;
	{
		const TObjectPtr<UScriptStruct> BuildInputStruct = InCharacter->GetInternalCollection()->GetEditorPipeline()->GetSpecification()->BuildInputStruct;
		if (BuildInputStruct
			&& BuildInputStruct->IsChildOf(FMetaHumanBuildInputBase::StaticStruct()))
		{
			// Initialize to the struct that the pipeline is expecting.
			//
			// Any properties defined in sub-structs of FMetaHumanBuildInputBase will be left as
			// their default values.
			BuildInput.InitializeAs(BuildInputStruct);

			FMetaHumanBuildInputBase& TypedBuildInput = BuildInput.GetMutable<FMetaHumanBuildInputBase>();
			TypedBuildInput.EditorPreviewCharacter = InCharacter->GetInternalCollectionKey();
		}
	}

	InCharacter->GetMutableInternalCollection()->Build(
		BuildInput,
		EMetaHumanCharacterPaletteBuildQuality::Preview,
		GetTargetPlatformManagerRef().GetRunningTargetPlatform(),
		UMetaHumanCollection::FOnBuildComplete(),
		InCharacter->GetInternalCollection()->GetDefaultInstance()->ToPinnedSlotSelections(EMetaHumanUnusedSlotBehavior::PinnedToEmpty));
}

const TSharedRef<FMetaHumanCharacterEditorData>* UMetaHumanCharacterEditorSubsystem::GetMetaHumanCharacterEditorData(TNotNull<const UMetaHumanCharacter*> InCharacter) const
{
	return CharacterDataMap.Find(InCharacter);
}

bool UMetaHumanCharacterEditorSubsystem::IsTickable() const
{
	// Only tick if we are waiting on texture data from being loaded
	for (const TPair<TObjectKey<UMetaHumanCharacter>, TSharedRef<FMetaHumanCharacterEditorData>>& CharacterDataPair : CharacterDataMap)
	{
		TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataPair.Value;

		if (!CharacterData->SynthesizedFaceTexturesFutures.IsEmpty() || !CharacterData->HighResBodyTexturesFutures.IsEmpty())
		{
			return true;
		}
	}

	return false;
}

void UMetaHumanCharacterEditorSubsystem::Tick(float InDeltaTime)
{
	for (TPair<TObjectKey<UMetaHumanCharacter>, TSharedRef<FMetaHumanCharacterEditorData>>& CharacterDataPair : CharacterDataMap)
	{
		TObjectKey<UMetaHumanCharacter> CharacterKey = CharacterDataPair.Key;
		TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataPair.Value;
		
		UMetaHumanCharacter* Character = CharacterKey.ResolveObjectPtr();
		if (!Character)
		{
			continue;
		}

		if (!CharacterData->SynthesizedFaceTexturesFutures.IsEmpty())
		{
			UpdatePendingSynthesizedTextures(Character, CharacterData, Character->SynthesizedFaceTextures);
		}

		if (!CharacterData->HighResBodyTexturesFutures.IsEmpty())
		{
			UpdatePendingHighResBodyTextures(Character, CharacterData, Character->BodyTextures);
		}
	}
}

void UMetaHumanCharacterEditorSubsystem::UpdatePendingSynthesizedTextures(
	TNotNull<const UMetaHumanCharacter*> InCharacter,
	TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
	TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& OutSynthesizedFaceTextures)
{
	TArray<EFaceTextureType> ReadyTextures;

	for (TPair<EFaceTextureType, TSharedFuture<FSharedBuffer>>& FaceTextureFuturePair : InCharacterData->SynthesizedFaceTexturesFutures)
	{
		EFaceTextureType TextureType = FaceTextureFuturePair.Key;
		TSharedFuture<FSharedBuffer> PayloadFuture = FaceTextureFuturePair.Value;

		if (PayloadFuture.IsReady())
		{
			ReadyTextures.Add(TextureType);

			const FSharedBuffer& PayloadData = PayloadFuture.Get();

			if (InCharacter->HasHighResolutionTextures())
			{
				if (const FMetaHumanCharacterTextureInfo* TextureInfo = InCharacter->SynthesizedFaceTexturesInfo.Find(TextureType))
				{
					OutSynthesizedFaceTextures.FindOrAdd(TextureType) = 
						FMetaHumanCharacterTextureSynthesis::CreateFaceTextureFromSource(TextureType, FImageView(
							TextureInfo->ToImageInfo(), const_cast<void*>(PayloadData.GetData())));
				}
			}
			else
			{
				// Update the cached image for the texture type to keep it consistent with the data that was stored in the character.
				// This prevents uninitialized texture data from being stored in the character when applying the skin settings
				if (FImage* CachedSynthesizedImage = InCharacterData->CachedSynthesizedImages.Find(TextureType))
				{
					FMemory::Memcpy(CachedSynthesizedImage->RawData.GetData(), PayloadData.GetData(), PayloadData.GetSize());
				}

				check(OutSynthesizedFaceTextures.Contains(TextureType));
				FMetaHumanCharacterTextureSynthesis::UpdateTexture(MakeConstArrayView((const uint8*) PayloadData.GetData(), PayloadData.GetSize() / sizeof(uint8)),
																	   OutSynthesizedFaceTextures[TextureType]);
			}

		}
	}

	for (EFaceTextureType ReadyTexture : ReadyTextures)
	{
		InCharacterData->SynthesizedFaceTexturesFutures.Remove(ReadyTexture);
	}

	// Textures were updated so call PostEditChange to refresh the material
	InCharacterData->HeadMaterials.ForEachSkinMaterial<UMaterialInstanceDynamic>(
		[](EMetaHumanCharacterSkinMaterialSlot, UMaterialInstanceDynamic* SkinMaterial)
		{
			SkinMaterial->PostEditChange();
		}
	);
}

void UMetaHumanCharacterEditorSubsystem::UpdatePendingHighResBodyTextures(
		TNotNull<const UMetaHumanCharacter*> InCharacter,
		TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
		TMap<EBodyTextureType, TObjectPtr<UTexture2D>>& OutBodyTextures)
{
	TArray<EBodyTextureType> ReadyTextures;

	for (TPair<EBodyTextureType, TSharedFuture<FSharedBuffer>>& BodyTextureFuturePair : InCharacterData->HighResBodyTexturesFutures)
	{
		EBodyTextureType TextureType = BodyTextureFuturePair.Key;
		TSharedFuture<FSharedBuffer> PayloadFuture = BodyTextureFuturePair.Value;

		if (PayloadFuture.IsReady())
		{
			ReadyTextures.Add(TextureType);

			const FSharedBuffer& PayloadData = PayloadFuture.Get();
			if (!PayloadData.IsNull())
			{
				// Local textures are initialized in FMetaHumanCharacterBodyTextureUtils::InitBodyTextureData(), only high rez should be loaded here

				// Body textures can "by pass" the HasHighResolutionTextures() check and store both downloaded and local textures
				if (const FMetaHumanCharacterTextureInfo* TextureInfo = InCharacter->HighResBodyTexturesInfo.Find(TextureType))
				{
					OutBodyTextures.FindOrAdd(TextureType) =
						FMetaHumanCharacterBodyTextureUtils::CreateBodyTextureFromSource(TextureType, FImageView(
							TextureInfo->ToImageInfo(), const_cast<void*>(PayloadData.GetData())));
				}
			}
		}
	}

	for (EBodyTextureType ReadyTexture : ReadyTextures)
	{
		InCharacterData->HighResBodyTexturesFutures.Remove(ReadyTexture);
	}

	// Textures were updated so call PostEditChange to refresh the material
	InCharacterData->BodyMaterial->PostEditChange();
}

void UMetaHumanCharacterEditorSubsystem::WaitForSynthesizedTextures(
	TNotNull<const UMetaHumanCharacter*> InCharacter,
	TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
	TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& OutSynthesizedFaceTextures,
	TMap<EBodyTextureType, TObjectPtr<UTexture2D>>& OutBodyTextures)
{
	for (TPair<EFaceTextureType, TSharedFuture<FSharedBuffer>>& FaceTextureFuturePair : InCharacterData->SynthesizedFaceTexturesFutures)
	{
		TSharedFuture<FSharedBuffer> PayloadFuture = FaceTextureFuturePair.Value;

		// No timeout needed here, as the information is coming from disk and should
		// complete within a reasonable amount of time.
		PayloadFuture.Wait();
	}

	UpdatePendingSynthesizedTextures(InCharacter, InCharacterData, OutSynthesizedFaceTextures);

	for (TPair<EBodyTextureType, TSharedFuture<FSharedBuffer>>& BodyTextureFuturePair : InCharacterData->HighResBodyTexturesFutures)
	{
		TSharedFuture<FSharedBuffer> PayloadFuture = BodyTextureFuturePair.Value;

		// No timeout needed here, as the information is coming from disk and should
		// complete within a reasonable amount of time.
		PayloadFuture.Wait();
	}

	UpdatePendingHighResBodyTextures(InCharacter, InCharacterData, OutBodyTextures);
}

TStatId UMetaHumanCharacterEditorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMetaHumanCharacterEditorSubsystem, STATGROUP_Tickables);
}

UMetaHumanCharacterEditorSubsystem* UMetaHumanCharacterEditorSubsystem::Get()
{
	check(GEditor);
	return GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>();
}

bool UMetaHumanCharacterEditorSubsystem::TryAddObjectToEdit(UMetaHumanCharacter* InCharacter)
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "TryAddObjectToEdit called with invalid character");
		return false;
	}

	// TryAddObjectToEdit should only be called once for a character if it succeeds, until RemoveObjectToEdit is called
	if (IsObjectAddedForEditing(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "TryAddObjectToEdit called for '{Character}' which is already added for editing", InCharacter->GetName());
		return false;
	}

	TGuardValue<bool> TryAddObjectGuard{ bIsAddingObjectToEdit, true };

	// Start loading the texture synthesis data in an async task
	UE::Tasks::FTask FaceTextureSynthesizerLoadTask = {};
	if (!FaceTextureSynthesizer.IsValid())
	{
		FaceTextureSynthesizerLoadTask = UE::Tasks::Launch(UE_SOURCE_LOCATION, [this]
			{
				FMetaHumanCharacterTextureSynthesis::InitFaceTextureSynthesizer(FaceTextureSynthesizer);
			});
	}

	// When loading texture sources, we need to ensure everything is loaded since the texture objects of the preview actor 
	// are populated at load time only and CachedSynthesizedImages are not used
	bool bBlockUntilComplete = InCharacter->HasHighResolutionTextures();

	const FEditorDataForCharacterCreationParams EditorDataForCharacterCreationParams =
	{
		.bBlockUntilComplete = bBlockUntilComplete,
		.bCreateMeshFromDNA = false,
		.OuterForGeneratedAssets = this,
		.PreviewMaterial = InCharacter->PreviewMaterialType,
	};
	TSharedPtr<FMetaHumanCharacterEditorData> CharacterData = CreateEditorDataForCharacter(InCharacter,
																						   EditorDataForCharacterCreationParams,
																						   InCharacter->SynthesizedFaceTextures,
																						   InCharacter->BodyTextures,
																						   FaceTextureSynthesizerLoadTask);
	if (CharacterData.IsValid())
	{
		CharacterDataMap.Add(InCharacter, CharacterData.ToSharedRef());

		OnBodyStateChanged(InCharacter).AddWeakLambda(this, [this, InCharacter]
		{
			constexpr bool bImportingFromDNA = false;
			UpdateCharacterIsFixedBodyType(InCharacter, bImportingFromDNA);
		});
	}
	else
	{
		UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Failed to create editing state for %s"), *InCharacter->GetFullName());
	}

	if (!InCharacter->HasSynthesizedTextures())
	{
		StoreSynthesizedTextures(InCharacter);
		ApplySkinSettings(InCharacter, InCharacter->SkinSettings);
	}

	if (InCharacter->HasHighResolutionTextures())
	{
		// High rez textures may have been loaded late so re-apply here
		ApplySkinSettings(InCharacter, InCharacter->SkinSettings);

		// Remove any texture object sources since these are preview data and will not get cooked
		for (const TPair<EFaceTextureType, TObjectPtr<UTexture2D>>& Pair : InCharacter->SynthesizedFaceTextures)
		{
			if (Pair.Value && Pair.Value->Source.IsValid())
			{
				Pair.Value->Source.Reset();
			}
		}

		for (const TPair<EBodyTextureType, TObjectPtr<UTexture2D>>& Pair : InCharacter->BodyTextures)
		{
			if (Pair.Value && Pair.Value->Source.IsValid())
			{
				Pair.Value->Source.Reset();
			}
		}
	}

	return CharacterData.IsValid();
}

bool UMetaHumanCharacterEditorSubsystem::IsObjectAddedForEditing(const UMetaHumanCharacter* InCharacter) const
{
	return CharacterDataMap.Contains(InCharacter);
}

void UMetaHumanCharacterEditorSubsystem::GetFaceAndBodySkeletalMeshes(TNotNull<const UMetaHumanCharacter*> InCharacter,
	const FEditorDataForCharacterCreationParams& InParams, USkeletalMesh*& OutFaceMesh, USkeletalMesh*& OutBodyMesh)
{
	const FName FaceMeshName = MakeUniqueObjectName(InParams.OuterForGeneratedAssets, USkeletalMesh::StaticClass(), TEXT("FaceMesh"), EUniqueObjectNameOptions::GloballyUnique);
	const FName BodyMeshName = MakeUniqueObjectName(InParams.OuterForGeneratedAssets, USkeletalMesh::StaticClass(), TEXT("BodyMesh"), EUniqueObjectNameOptions::GloballyUnique);
	bool bFaceMeshCreatedFromDNA = false;
	bool bBodyMeshCreatedFromDNA = false;
	
	// Re-create Face mesh from DNA if blendshapes are present
	if (InCharacter->HasFaceDNA())
	{
		TArray<uint8> FaceDNABuffer = InCharacter->GetFaceDNABuffer();
		TSharedPtr<IDNAReader> FaceDNAReader = ReadDNAFromBuffer(&FaceDNABuffer);

		if(FaceDNAReader.IsValid() && FaceDNAReader->GetBlendShapeChannelCount() > 0)
		{
			FString FullPackagePath = InParams.OuterForGeneratedAssets->GetPackage()->GetName();			
			OutFaceMesh = FMetaHumanCharacterSkelMeshUtils::GetSkeletalMeshAssetFromDNA(FaceDNAReader, FullPackagePath, FaceMeshName.ToString(), EMetaHumanImportDNAType::Face);
			FMetaHumanCharacterSkelMeshUtils::PopulateSkelMeshData(OutFaceMesh, FaceDNAReader, true /*bIsFace*/);
			bFaceMeshCreatedFromDNA = true;
		}
	}

	// Re-create Body mesh from DNA if fixed body type
	if (InCharacter->bFixedBodyType && InCharacter->HasBodyDNA())
	{
		TArray<uint8> BodyDNABuffer = InCharacter->GetBodyDNABuffer();
		if (TSharedPtr<IDNAReader> BodyDNAReader = ReadDNAFromBuffer(&BodyDNABuffer); BodyDNAReader.IsValid())
		{
			FString FullPackagePath = InParams.OuterForGeneratedAssets->GetPackage()->GetName();			
			OutBodyMesh = FMetaHumanCharacterSkelMeshUtils::GetSkeletalMeshAssetFromDNA(BodyDNAReader, FullPackagePath, BodyMeshName.ToString(), EMetaHumanImportDNAType::Body);
			FMetaHumanCharacterSkelMeshUtils::PopulateSkelMeshData(OutBodyMesh, BodyDNAReader, false /*bIsFace*/);
			bBodyMeshCreatedFromDNA = true;
		}
	}

	//TODO: Optimize the loading from DNA. For now divert editor loading to use skelmesh assets
	if (UE::MetaHuman::CvarMHLoadMeshesFromDNA.GetValueOnAnyThread() || InParams.bCreateMeshFromDNA)
	{
		if (!bFaceMeshCreatedFromDNA)
		{
			OutFaceMesh = GetFaceArchetypeMesh(InCharacter->TemplateType);
		}

		if (!bBodyMeshCreatedFromDNA)
		{
			OutBodyMesh = GetBodyArchetypeMesh(InCharacter->TemplateType);
		}
	}
	else
	{
		if (!bFaceMeshCreatedFromDNA)
		{
			USkeletalMesh* FaceArchetypeMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Script/Engine.SkeletalMesh'/" UE_PLUGIN_NAME "/Face/SKM_Face.SKM_Face'"));
			OutFaceMesh = DuplicateObject(FaceArchetypeMesh, InParams.OuterForGeneratedAssets, FaceMeshName);
		}

		if (!bBodyMeshCreatedFromDNA)
		{
			USkeletalMesh* BodyArchetypeMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Script/Engine.SkeletalMesh'/" UE_PLUGIN_NAME "/Body/IdentityTemplate/SKM_Body.SKM_Body'"));
			OutBodyMesh = DuplicateObject(BodyArchetypeMesh, InParams.OuterForGeneratedAssets, BodyMeshName);
		}
	}

	if (bFaceMeshCreatedFromDNA)
	{
		UE::MetaHuman::Analytics::RecordCreateMeshFromDNAEvent(InCharacter);
	}

	check(OutFaceMesh && OutBodyMesh);
}

bool UMetaHumanCharacterEditorSubsystem::InitializeIdentityStateForFaceAndBody(TNotNull<const UMetaHumanCharacter*> InCharacter,
	TSharedPtr<FMetaHumanCharacterIdentity::FState>& OutFaceState, TSharedPtr<FMetaHumanCharacterBodyIdentity::FState>& OutBodyState)
{
	// Initialize the states for face and body and from the values stored in the character
	const FMetaHumanCharacterIdentityModels& IdentityModels = GetOrCreateCharacterIdentity(InCharacter->TemplateType);

	OutFaceState = IdentityModels.Face->CreateState();
	OutBodyState = IdentityModels.Body->CreateState();
	
	if (!OutFaceState.IsValid())
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "{Name}: Identity model failed to create face state", *InCharacter->GetFullName());
		return false;
	}

	if (!OutBodyState.IsValid())
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "{Name}: Identity model failed to create body state", *InCharacter->GetFullName());
		return false;
	}

	// Set body delta evaluation to true
	FMetaHumanCharacterIdentity::FSettings FaceStateSettings = OutFaceState->GetSettings();
	FaceStateSettings.SetBodyDeltaInEvaluation(true);
	FaceStateSettings.SetGlobalVertexDeltaScale(InCharacter->FaceEvaluationSettings.GlobalDelta);
	FaceStateSettings.SetGlobalHighFrequencyScale(InCharacter->FaceEvaluationSettings.HighFrequencyDelta);
	OutFaceState->SetSettings(FaceStateSettings);

	// Initialize the face state
	const FSharedBuffer FaceStateData = InCharacter->GetFaceStateData();
	if (!OutFaceState->Deserialize(FaceStateData))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "{Name}: Failed to deserialize face state stored in Character asset", *InCharacter->GetFullName());
		return false;
	}

	// Set the texture index for the face state
	OutFaceState->SetHighFrequencyVariant(InCharacter->SkinSettings.Skin.FaceTextureIndex);
	OutFaceState->SetFaceScale(InCharacter->FaceEvaluationSettings.HeadScale);

	// Apply body state
	const FSharedBuffer BodyStateData = InCharacter->GetBodyStateData();
	if (!OutBodyState->Deserialize(BodyStateData))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "{Name}: Failed to deserialize body state stored in Character asset", *InCharacter->GetFullName());
		return false;
	}
	
	//This accounts for <UE5.7 where the neck set up is using different algorithm
	if (OutFaceState->GetInternalSerializationVersion() == 1)
	{
		FMetaHumanCharacterIdentity::FSettings MHCISettings = OutFaceState->GetSettings();
		MHCISettings.SetCompatibilityEvaluation(true);
		OutFaceState->SetSettings(MHCISettings);
		// This also updates the face state and applies it
		OutFaceState->SetBodyJointsAndBodyFaceVertices(OutBodyState->CopyBindPose(), OutBodyState->GetVerticesAndVertexNormals().Vertices);
		FMetaHumanRigEvaluatedState BodyVertices = OutBodyState->GetVerticesAndVertexNormals();
		BodyVertices.Vertices.SetNum(OutBodyState->GetNumVerticesPerLOD()[0]);
		FMetaHumanRigEvaluatedState FaceVertices = OutFaceState->Evaluate();

		const TMap<EHeadFitToTargetMeshes, int32> PartMeshMapping =
		{
			{ EHeadFitToTargetMeshes::Head, 0 },
			{ EHeadFitToTargetMeshes::LeftEye, 3 },
			{ EHeadFitToTargetMeshes::RightEye, 4 },
			{ EHeadFitToTargetMeshes::Teeth, 1 }
		};
		TMap<int32, TArray<FVector3f>> FacePartsVertices;
		for (const TPair<EHeadFitToTargetMeshes, int32>& Pair : PartMeshMapping)
		{
			const EHeadFitToTargetMeshes MeshType = Pair.Key;
			const int32 DNAMeshIndex = Pair.Value;
			const int32 NumVerts = IdentityModels.Face->GetNumLOD0MeshVertices(MeshType);
			TArray<FVector3f>& PartVerts = FacePartsVertices.FindOrAdd(DNAMeshIndex);
			PartVerts.SetNumUninitialized(NumVerts);
			for (int32 v = 0; v < NumVerts; ++v)
			{
				PartVerts[v] = OutFaceState->GetVertex(FaceVertices.Vertices, DNAMeshIndex, v);
			}
		}
		for (int32 i = 0; i < BodyVertices.Vertices.Num(); i++)
		{
			FVector3f bodyVertex;
			if (i < FacePartsVertices[0].Num())
			{
				FVector3f FaceVertex = FacePartsVertices[0][i];
				bodyVertex = FaceVertex;	
			}
			else 
			{
				bodyVertex = BodyVertices.Vertices[i];
				bodyVertex.Z = BodyVertices.Vertices[i].Y;
				bodyVertex.Y = BodyVertices.Vertices[i].Z;
			}
			BodyVertices.Vertices[i] = bodyVertex;
		}
		TArray<FFloatTriplet> BodySkinning;
		OutBodyState->GetCombinedModelVertexInfluenceWeightsLOD0(BodySkinning);
		OutBodyState->SetMesh(BodyVertices.Vertices, false);
		OutBodyState->SetCombinedModelVertexInfluenceWeightsLOD0(BodySkinning);
		MHCISettings.SetCompatibilityEvaluation(false);
		OutFaceState->SetSettings(MHCISettings);
		OutFaceState->SetBodyJointsAndBodyFaceVertices(OutBodyState->CopyBindPose(), OutBodyState->GetVerticesAndVertexNormals().Vertices);
		FFitToTargetOptions options;
		options.AlignmentOptions = EAlignmentOptions::None;
		OutFaceState->FitToTarget(FacePartsVertices, options);
	}
	return true;
}

TSharedPtr<FMetaHumanCharacterEditorData> UMetaHumanCharacterEditorSubsystem::CreateEditorDataForCharacter(
	TNotNull<const UMetaHumanCharacter*> InCharacter,
	const FEditorDataForCharacterCreationParams& InParams,
	TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& OutSynthesizedFaceTextures,
	TMap<EBodyTextureType, TObjectPtr<class UTexture2D>>& OutBodyTextures,
	UE::Tasks::FTask InFaceTextureSynthesizerLoadTask)
{
	USkeletalMesh* FaceMesh = nullptr;
	USkeletalMesh* BodyMesh = nullptr;
	GetFaceAndBodySkeletalMeshes(InCharacter, InParams, FaceMesh, BodyMesh);

	if (!FaceMesh || !BodyMesh)
	{
		return nullptr;
	}

	TSharedRef<FDNAToSkelMeshMap> FaceDnaToSkelMeshMap = MakeShareable(USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh(FaceMesh));
	const TSharedRef<FDNAToSkelMeshMap> BodyDnaToSkelMeshMap = MakeShareable(USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh(BodyMesh));

	if (InCharacter->HasFaceDNA())
	{
		TArray<uint8> FaceDNABuffer = InCharacter->GetFaceDNABuffer();
		TSharedPtr<IDNAReader> FaceDNAReader = ReadDNAFromBuffer(&FaceDNABuffer);
		
		// Update Face Mesh from DNA for joint only AR
		if (FaceDNAReader.IsValid() && FaceDNAReader->GetBlendShapeChannelCount() == 0)
		{
			FMetaHumanCharacterSkelMeshUtils::UpdateSkelMeshFromDNA(FaceDNAReader.ToSharedRef(),
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::All & ~FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::BaseMesh,
				FaceDnaToSkelMeshMap, EMetaHumanCharacterOrientation::Y_UP, FaceMesh);
		}
	}
	
	TSharedPtr<FMetaHumanCharacterIdentity::FState> FaceState;
	TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> BodyState;
	if (!InitializeIdentityStateForFaceAndBody(InCharacter, FaceState, BodyState))
	{
		return nullptr;
	}

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = MakeShared<FMetaHumanCharacterEditorData>(FaceMesh, BodyMesh,	FaceDnaToSkelMeshMap,
		BodyDnaToSkelMeshMap, FaceState.ToSharedRef(), BodyState.ToSharedRef());



	if (InCharacter->HasBodyDNA() && InCharacter->bFixedBodyType)
	{
		// Already loaded mesh in GetFaceAndBodySkeletalMeshes via interchange - do nothing 
	}
	else if (InCharacter->HasBodyDNA() && !InCharacter->bFixedBodyType)
	{
		// If body DNA exists for non fixed types, update archetype from DNA
		TArray<uint8> DNABuffer = InCharacter->GetBodyDNABuffer();
		TSharedPtr<IDNAReader> BodyDnaReader = ReadDNAFromBuffer(&DNABuffer, EDNADataLayer::All);
		
		const FMetaHumanCharacterSkelMeshUtils::EUpdateFlags UpdateFlags =
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::BaseMesh |
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::Joints |
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::SkinWeights |
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::DNABehavior |
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::DNAGeometry;
	
		FMetaHumanCharacterSkelMeshUtils::UpdateSkelMeshFromDNA(
			BodyDnaReader.ToSharedRef(),
			UpdateFlags,
			CharacterData->BodyDnaToSkelMeshMap,
			EMetaHumanCharacterOrientation::Y_UP,
			CharacterData->BodyMesh);
	
		// Currently we need update the vertex normals from the state
		const FMetaHumanRigEvaluatedState BodyVerticesAndVertexNormals = CharacterData->BodyState->GetVerticesAndVertexNormals();
		FMetaHumanCharacterSkelMeshUtils::UpdateLODModelVertexPositions(BodyMesh, BodyVerticesAndVertexNormals,
			*CharacterData->BodyState, *CharacterData->BodyDnaToSkelMeshMap, ELodUpdateOption::All, EVertexPositionsAndNormals::NormalsOnly);
		USkelMeshDNAUtils::RebuildRenderData(BodyMesh);
		FMetaHumanCharacterSkelMeshUtils::UpdateMeshDescriptionFromLODModel(BodyMesh);
	}
	else
	{
		// Update from state
		const FMetaHumanRigEvaluatedState BodyVerticesAndVertexNormals = CharacterData->BodyState->GetVerticesAndVertexNormals();
		UpdateBodyMeshInternal(CharacterData, BodyVerticesAndVertexNormals, ELodUpdateOption::All, true);
	}
	
	// Updates face from state with updates from body state applied
	UpdateFaceFromBodyInternal(CharacterData, ELodUpdateOption::All, /*bInUpdateNeutral*/ true);
	FMetaHumanCharacterSkelMeshUtils::UpdateMeshDescriptionFromLODModelVerticesNormalsAndTangents(CharacterData->FaceMesh);
	// Make sure FaceTextureSynthesizer has been initialized before setting up the face textures
	InFaceTextureSynthesizerLoadTask.Wait();
	if (!InCharacter->HasHighResolutionTextures())
	{
		// Only need to initialize data for texture synthesis when the asset does not contain downloaded textures
		FMetaHumanCharacterTextureSynthesis::InitSynthesizedFaceData(FaceTextureSynthesizer,
			InCharacter->SynthesizedFaceTexturesInfo,
			OutSynthesizedFaceTextures,
			CharacterData->CachedSynthesizedImages);
	}
	FMetaHumanCharacterBodyTextureUtils::InitBodyTextureData(InCharacter->SkinSettings.Skin, InCharacter->HighResBodyTexturesInfo, OutBodyTextures);

	if (InCharacter->HasSynthesizedTextures())
	{
		// If we have synthesized textures, make an async request to load the data.
		//
		// The textures currently on the CharacterData will have their image data overwritten by
		// the async task, but they won't be replaced with different textures, so it's safe to take
		// references to them before the async work is done.
		for (const TPair<EFaceTextureType, FMetaHumanCharacterTextureInfo>& TextureInfoPair : InCharacter->SynthesizedFaceTexturesInfo)
		{
			const EFaceTextureType TextureType = TextureInfoPair.Key;

			CharacterData->SynthesizedFaceTexturesFutures.FindOrAdd(TextureType) = InCharacter->GetSynthesizedFaceTextureDataAsync(TextureType);
		}
	}

	for (const TPair<EBodyTextureType, FMetaHumanCharacterTextureInfo>& TextureInfoPair : InCharacter->HighResBodyTexturesInfo)
	{
		const EBodyTextureType TextureType = TextureInfoPair.Key;

		CharacterData->HighResBodyTexturesFutures.FindOrAdd(TextureType) = InCharacter->GetHighResBodyTextureDataAsync(TextureType);
	}

	// Apply the head model data to the character
	ApplyEyelashesAndTeethPropertiesToFaceState(CharacterData, InCharacter->HeadModelSettings.Eyelashes, InCharacter->HeadModelSettings.Teeth,
		/*bInUpdateEyelashes*/ true, /*bInUpdateTeeth*/ true, ELodUpdateOption::All);
	USkelMeshDNAUtils::RebuildRenderData(FaceMesh);
	FMetaHumanCharacterSkelMeshUtils::UpdateMeshDescriptionFromLODModel(FaceMesh);

	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();

	// Apply the preview material type in the actor so it spawns with the correct preview materials
	// and update all the skin material parameters
	const bool bUseFaceMaterialsWithVTSupport = Settings->ShouldUseVirtualTextures() && InCharacter->HasHighResolutionTextures();
	UpdateActorsSkinPreviewMaterial(CharacterData, InParams.PreviewMaterial, bUseFaceMaterialsWithVTSupport);

	const bool bForceUseExistingTextures = InCharacter->HasSynthesizedTextures() || InCharacter->SkinSettings.bEnableTextureOverrides;
	bool bTexturesHaveBeenRegenerated = false;

	// Build a texture set considering any overrides in the skin settings
	const FMetaHumanCharacterSkinTextureSet FinalSkinTextureSet =
		InCharacter->SkinSettings.GetFinalSkinTextureSet(FMetaHumanCharacterSkinTextureSet
														 {
															 .Face = InCharacter->GetValidFaceTextures(),
															 .Body = InCharacter->BodyTextures
														 });

	// Wait for any async tasks to complete, if requested by the caller
	if (InParams.bBlockUntilComplete)
	{
		WaitForSynthesizedTextures(InCharacter, CharacterData, OutSynthesizedFaceTextures, OutBodyTextures);
	}

	ApplySkinSettings(CharacterData, InCharacter->SkinSettings, bForceUseExistingTextures, FinalSkinTextureSet, OutSynthesizedFaceTextures, OutBodyTextures, bTexturesHaveBeenRegenerated);
	ApplyMakeupSettings(CharacterData, InCharacter->MakeupSettings, bUseFaceMaterialsWithVTSupport);
	ApplyEyesSettings(CharacterData, InCharacter->EyesSettings);
	FMetaHumanCharacterSkinMaterials::ApplyEyelashesPropertiesToMaterial(CharacterData->HeadMaterials, InCharacter->HeadModelSettings.Eyelashes);
	FMetaHumanCharacterSkinMaterials::ApplyTeethPropertiesToMaterial(CharacterData->HeadMaterials, InCharacter->HeadModelSettings.Teeth);
	// If there were existing textures, they should not have been regenerated
	ensure(!bForceUseExistingTextures || !bTexturesHaveBeenRegenerated);

	return CharacterData;
}

void UMetaHumanCharacterEditorSubsystem::UpdateCharacterFaceMeshFromDNA(TNotNull<UObject*> InGeneratedAssetsOuter, const TSharedPtr<IDNAReader>& InDNAReader, TSharedRef<FMetaHumanCharacterEditorData>& OutCharacterData)
{
	const FName FaceMeshName = MakeUniqueObjectName(InGeneratedAssetsOuter, USkeletalMesh::StaticClass(), TEXT("FaceMesh"), EUniqueObjectNameOptions::GloballyUnique);
	const FString FullPackagePath = InGeneratedAssetsOuter->GetPackage()->GetName();
	
	USkeletalMesh* CreatedFaceMesh = FMetaHumanCharacterSkelMeshUtils::GetSkeletalMeshAssetFromDNA(InDNAReader, FullPackagePath, FaceMeshName.ToString(), EMetaHumanImportDNAType::Face);
	if (CreatedFaceMesh)
	{
		FMetaHumanCharacterSkelMeshUtils::PopulateSkelMeshData(CreatedFaceMesh, InDNAReader, true /*bIsFace*/);
		OutCharacterData->FaceMesh = CreatedFaceMesh;
		OutCharacterData->FaceDnaToSkelMeshMap = MakeShareable(USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh(CreatedFaceMesh));

		FMetaHumanCharacterSkinMaterials::SetHeadMaterialsOnMesh(OutCharacterData->HeadMaterials, OutCharacterData->FaceMesh);
		ForEachCharacterActor(OutCharacterData, [OutCharacterData](TScriptInterface<IMetaHumanCharacterEditorActorInterface> Actor)
			{
				Actor->UpdateFaceComponentMesh(OutCharacterData->FaceMesh);
				Actor->OnFaceMeshUpdated();
			});
	}
	else
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to generate Face Skeletal Mesh from DNA");
	}
}

void UMetaHumanCharacterEditorSubsystem::UpdateCharacterBodyMeshFromDNA(TNotNull<UObject*> InGeneratedAssetsOuter, const TSharedPtr<IDNAReader>& InDNAReader, TSharedRef<FMetaHumanCharacterEditorData>& OutCharacterData)
{
	const FName BodyMeshName = MakeUniqueObjectName(InGeneratedAssetsOuter, USkeletalMesh::StaticClass(), TEXT("BodyMesh"), EUniqueObjectNameOptions::GloballyUnique);
	const FString FullPackagePath = InGeneratedAssetsOuter->GetPackage()->GetName();
	
	USkeletalMesh* CreatedBodyMesh = FMetaHumanCharacterSkelMeshUtils::GetSkeletalMeshAssetFromDNA(InDNAReader, FullPackagePath, BodyMeshName.ToString(), EMetaHumanImportDNAType::Body);
	FMetaHumanCharacterSkelMeshUtils::PopulateSkelMeshData(CreatedBodyMesh, InDNAReader, false /*bIsFace*/);
	OutCharacterData->BodyMesh = CreatedBodyMesh;
	OutCharacterData->BodyDnaToSkelMeshMap = MakeShareable(USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh(CreatedBodyMesh));

	FMetaHumanCharacterSkinMaterials::SetBodyMaterialOnMesh(OutCharacterData->BodyMaterial, OutCharacterData->BodyMesh);
	ForEachCharacterActor(OutCharacterData,
	   [OutCharacterData](TScriptInterface<IMetaHumanCharacterEditorActorInterface> Actor)
	   {
		   Actor->UpdateBodyComponentMesh(OutCharacterData->BodyMesh);
		   Actor->OnBodyMeshUpdated();
	   });
}

void UMetaHumanCharacterEditorSubsystem::ResetTextureSynthesis()
{
	if (FaceTextureSynthesizer.IsValid())
	{
		FaceTextureSynthesizer.Clear();
	}
	FMetaHumanCharacterTextureSynthesis::InitFaceTextureSynthesizer(FaceTextureSynthesizer);
}

void UMetaHumanCharacterEditorSubsystem::RemoveObjectToEdit(const UMetaHumanCharacter* InCharacter)
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "RemoveObjectToEdit called with invalid character");
		return;
	}

	if (!IsObjectAddedForEditing(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to remove object to edit. '{Character}' was not previously opened for edit", InCharacter->GetName());
		return;
	}

	ForEachCharacterActor(InCharacter, [](TScriptInterface<IMetaHumanCharacterEditorActorInterface> ActorInterface)
	{
		if (AActor* Actor = Cast<AActor>(ActorInterface.GetObject()))
		{
			Actor->Destroy();
		}
	});

	CharacterDataMap.Remove(InCharacter);

	// This function can has the potential to be called during the call to TryAddObjectToEdit
	// in which case CharacterDataMap may be leading leading to the destruction of the FaceTextureSynthesizer object
	// while TryAddObjectToEdit expects it to be alive. Checking bIsAddingObjectToEdit here to prevent TS from
	// being unloaded here. FaceTextureSynthesizer will eventually be released when this function is called for
	// the new character being opened to edit

	if (CharacterDataMap.IsEmpty() && !bIsAddingObjectToEdit)
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Display, "Finalizing Face Texture Synthesizer");
		FaceTextureSynthesizer.Clear();
	}
}

void UMetaHumanCharacterEditorSubsystem::InitializeMetaHumanCharacter(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::InitializeMetaHumanCharacter");

	const FMetaHumanCharacterIdentityModels& IdentityModels = GetOrCreateCharacterIdentity(InMetaHumanCharacter->TemplateType);

	TSharedPtr<FMetaHumanCharacterIdentity::FState> FaceState = IdentityModels.Face->CreateState();
	TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> BodyState = IdentityModels.Body->CreateState();

	FSharedBuffer FaceStateData;
	FaceState->Serialize(FaceStateData);
	InMetaHumanCharacter->SetFaceStateData(FaceStateData);

	FSharedBuffer BodyStateData;
	BodyState->Serialize(BodyStateData);
	InMetaHumanCharacter->SetBodyStateData(BodyStateData);

	// Set the Character's internal collection to use the default Pipeline, so that the Character can 
	// be built without the user having to manually assign a pipeline.
	InMetaHumanCharacter->GetMutableInternalCollection()->SetDefaultPipeline();

	// Initialize the default eye parameters
	FMetaHumanCharacterSkinMaterials::GetDefaultEyeSettings(InMetaHumanCharacter->EyesSettings);

}

TScriptInterface<IMetaHumanCharacterEditorActorInterface> UMetaHumanCharacterEditorSubsystem::CreateMetaHumanCharacterEditorActor(TNotNull<UMetaHumanCharacter*> InCharacter, TNotNull<UWorld*> InWorld)
{
	FText FailureReason;
	TSubclassOf<AActor> ActorClass;
	if (!TryGetMetaHumanCharacterEditorActorClass(InCharacter, ActorClass, FailureReason))
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("FailureReason"), FailureReason);

		FText Message = FText::Format(LOCTEXT("ActorSpawnFailedMessage", "Failed to spawn the Character Pipeline's specified actor. The default actor will be used instead.\n\nReason: {FailureReason}"), Args);

		// This message is quite long, so give a longer expire duration
		UE::MetaHuman::ShowNotification(Message, SNotificationItem::ECompletionState::CS_Fail, 12.0f);
		
		// Fall back to default actor
		ActorClass = AMetaHumanCharacterEditorActor::StaticClass();
	}
	
	check(ActorClass);
	check(ActorClass->ImplementsInterface(UMetaHumanCharacterEditorActorInterface::StaticClass()));

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags = RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.bDeferConstruction = true;

	AActor* SpawnedActor = InWorld->SpawnActor(ActorClass, nullptr, nullptr, SpawnParameters);
	check(SpawnedActor);

	TScriptInterface<IMetaHumanCharacterEditorActorInterface> ActorInterface = SpawnedActor;

	const UMetaHumanCollection* Collection = InCharacter->GetInternalCollection();
	// Internal collection should always exist
	check(Collection);
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	const TArray<int32> FaceLODMapping{ 0, 1, 2, 3, 4, 5, 6, 7 };
	const TArray<int32> BodyLODMapping{ 0, 0, 1, 1, 2, 2, 3, 3 };

	// Set the meshes before the construction script runs, so that it can access them
	ActorInterface->InitializeMetaHumanCharacterEditorActor(
		Collection->GetDefaultInstance(), 
		InCharacter, 
		CharacterData->FaceMesh, 
		CharacterData->BodyMesh,
		FaceLODMapping.Num(),
		FaceLODMapping,
		BodyLODMapping);

	SpawnedActor->FinishSpawning(FTransform::Identity);

	ActorInterface->SetForcedLOD((int32)InCharacter->ViewportSettings.LevelOfDetail);

	CharacterData->CharacterActorList.Add(ActorInterface);

	Collection->GetDefaultInstance()->OnInstanceUpdatedNative.Remove(CharacterInstanceUpdatedDelegateHandle);
	CharacterInstanceUpdatedDelegateHandle = Collection->GetDefaultInstance()->OnInstanceUpdatedNative.AddWeakLambda(this, [this, InCharacter]
	{
		OnCharacterInstanceUpdated(InCharacter);
	});

	return ActorInterface;
}

AActor* UMetaHumanCharacterEditorSubsystem::SpawnMetaHumanActor(UMetaHumanCharacter* InCharacter)
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "SpawnMetaHumanActor called with invalid character");
		return nullptr;
	}

	if (!IsObjectAddedForEditing(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "SpawnMetaHumanActor called with {Character} that is not not added for editing", InCharacter->GetName());
		return nullptr;
	}

	TScriptInterface<IMetaHumanCharacterEditorActorInterface> ActorInterface = CreateMetaHumanCharacterEditorActor(InCharacter, GWorld);

	// The actor was spawned in the level, so make sure the animation mode is set to manual as it will likely be driven by Sequencer or Animation Sequences
	ActorInterface->SetActorDrivingAnimationMode(EMetaHumanActorDrivingAnimationMode::Manual);

	// Clear the transient flag of the actor so it show up in PIE worlds
	AActor* Actor = CastChecked<AActor>(ActorInterface.GetObject());
	Actor->ClearFlags(RF_Transient);

	return Actor;
}

void UMetaHumanCharacterEditorSubsystem::CreateMetaHumanInvisibleDrivingActor(TNotNull<const UMetaHumanCharacter*> InCharacter, TScriptInterface<IMetaHumanCharacterEditorActorInterface> InEditorActorInterface, TNotNull<class UWorld*> InWorld)
{
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.ObjectFlags = RF_Transient;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	TObjectPtr<AMetaHumanInvisibleDrivingActor> InvisibleDrivingActor = InWorld->SpawnActor<AMetaHumanInvisibleDrivingActor>(SpawnParameters);
	InvisibleDrivingActor->SetDefaultBodySkeletalMesh();

	// Initialize the preview AnimBP.
	InvisibleDrivingActor->InitPreviewAnimInstance();

	USkeletalMeshComponent* SkelMeshComponent = InvisibleDrivingActor->GetSkeletalMeshComponent();
	ensureMsgf(SkelMeshComponent, TEXT("Cannot create invisible driving actor. Skeletal mesh component is invalid."));
	if (SkelMeshComponent)
	{
		InEditorActorInterface->SetDrivingSkeletalMesh(SkelMeshComponent);

		// Place the actor right next to the preview actor for debugging cases when making it visible.
		FVector DrivingActorLocation = InvisibleDrivingActor->GetActorLocation();
		const FBoxSphereBounds SkelMeshBounds = SkelMeshComponent->GetLocalBounds();
		DrivingActorLocation.X += SkelMeshBounds.GetBox().GetExtent().X * 1.5f;
		InvisibleDrivingActor->SetActorLocation(DrivingActorLocation);
	}

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	CharacterData->InvisibleDrivingActor = InvisibleDrivingActor;
}

TObjectPtr<AMetaHumanInvisibleDrivingActor> UMetaHumanCharacterEditorSubsystem::GetInvisibleDrivingActor(TNotNull<const UMetaHumanCharacter*> InCharacter)
{
	const TSharedRef<FMetaHumanCharacterEditorData>* FoundCharacterData = CharacterDataMap.Find(InCharacter);
	if (FoundCharacterData)
	{
		return FoundCharacterData->Get().InvisibleDrivingActor;
	}

	return {};
}

bool UMetaHumanCharacterEditorSubsystem::RemoveTexturesAndRigs(TNotNull<UMetaHumanCharacter*> InCharacter)
{
	if (CharacterDataMap.Contains(InCharacter))
	{
		return false;
	}

	// Remove the Rigs
	const bool bInHasFaceDNABlendShapes = false;
	InCharacter->SetFaceDNABuffer({}, bInHasFaceDNABlendShapes);
	InCharacter->SetBodyDNABuffer({});

	// Remove all textures
	InCharacter->RemoveAllTextures();

	if (!InCharacter->SkinSettings.bEnableTextureOverrides)
	{
		InCharacter->SkinSettings.TextureOverrides.Face.Empty();
		InCharacter->SkinSettings.TextureOverrides.Body.Empty();
	}

	InCharacter->MarkPackageDirty();

	return true;
}

bool UMetaHumanCharacterEditorSubsystem::TryGetMetaHumanCharacterEditorActorClass(TNotNull<const UMetaHumanCharacter*> InCharacter, TSubclassOf<AActor>& OutActorClass, FText& OutFailureReason) const
{
	const UMetaHumanCollection* Collection = InCharacter->GetInternalCollection();
	// Internal collection should always exist
	check(Collection);

	const UMetaHumanCollectionEditorPipeline* Pipeline = Collection->GetEditorPipeline();
	if (!Pipeline)
	{
		OutFailureReason = LOCTEXT("NoPipelineOnCollection", "The Character's Collection has no Pipeline set.");
		return false;
	}

	OutActorClass = Pipeline->GetEditorActorClass();
	if (!OutActorClass)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("PipelinePath"), FText::FromString(Pipeline->GetPathName()));

		OutFailureReason = FText::Format(LOCTEXT("NoEditorActorClass", "The Character's Pipeline ({PipelinePath}) doesn't specify an editor actor."), Args);

		return false;
	}
	
	if (!OutActorClass->ImplementsInterface(UMetaHumanCharacterEditorActorInterface::StaticClass()))
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("PipelinePath"), FText::FromString(Pipeline->GetPathName()));

		OutFailureReason = FText::Format(
			LOCTEXT("InvalidEditorActorClass", "The editor actor specified by the Character's Pipeline ({PipelinePath}) doesn't implement IMetaHumanCharacterEditorActorInterface."),
			Args);

		return false;
	}

	return true;
}

bool UMetaHumanCharacterEditorSubsystem::TryGenerateCharacterAssets(
	TNotNull<const UMetaHumanCharacter*> InCharacter,
	UObject* InOuterForGeneratedAssets,
	FMetaHumanCharacterGeneratedAssets& OutGeneratedAssets)
{
	return TryGenerateCharacterAssets(InCharacter, InOuterForGeneratedAssets, FMetaHumanCharacterGeneratedAssetOptions(), OutGeneratedAssets);
}

bool UMetaHumanCharacterEditorSubsystem::TryGenerateCharacterAssets(
	TNotNull<const UMetaHumanCharacter*> InCharacter,
	UObject* InOuterForGeneratedAssets,
	const FMetaHumanCharacterGeneratedAssetOptions& InOptions,
	FMetaHumanCharacterGeneratedAssets& OutGeneratedAssets)
{
	if (!InOuterForGeneratedAssets)
	{
		InOuterForGeneratedAssets = GetTransientPackage();
	}

	OutGeneratedAssets = FMetaHumanCharacterGeneratedAssets();

	const FEditorDataForCharacterCreationParams EditorDataForCharacterCreationParams =
	{ 
		.bBlockUntilComplete = true,
		.bCreateMeshFromDNA = true,
		.OuterForGeneratedAssets = InOuterForGeneratedAssets,
		.PreviewMaterial = EMetaHumanCharacterSkinPreviewMaterial::Editable,
	};
	TSharedPtr<FMetaHumanCharacterEditorData> CharacterData = CreateEditorDataForCharacter(InCharacter,
																						   EditorDataForCharacterCreationParams,
																						   OutGeneratedAssets.SynthesizedFaceTextures,
																						   OutGeneratedAssets.BodyTextures);

	if (!CharacterData.IsValid())
	{
		return false;
	}

	check(CharacterData->BodyMesh);
	OutGeneratedAssets.BodyMesh = CharacterData->BodyMesh;
	OutGeneratedAssets.Metadata.Emplace(CharacterData->BodyMesh, TEXT("Body"), FString::Format(TEXT("SKM_{0}_BodyMesh"), { InCharacter->GetName() }));

	check(CharacterData->FaceMesh);
	OutGeneratedAssets.FaceMesh = CharacterData->FaceMesh;
	OutGeneratedAssets.Metadata.Emplace(CharacterData->FaceMesh, TEXT("Face"), FString::Format(TEXT("SKM_{0}_FaceMesh"), { InCharacter->GetName() }));

	OutGeneratedAssets.PhysicsAsset = CreatePhysicsAssetForCharacter(InCharacter, InOuterForGeneratedAssets, CharacterData->BodyState);
	OutGeneratedAssets.Metadata.Emplace(OutGeneratedAssets.PhysicsAsset, TEXT("Body"), FString::Format(TEXT("PHYS_{0}"), { InCharacter->GetName() }));

	// Face and body textures and materials
	{
		const FMetaHumanCharacterFaceMaterialSet& HeadMaterialSetDynamic = CharacterData->HeadMaterials;

		// Convert the Head material set from dynamic to constant instances

		// Create a new face material set to be applied in the face mesh being built
		FMetaHumanCharacterFaceMaterialSet FaceMaterialSet =
		{
			.EyeLeft = UE::MetaHuman::PaletteUnpackHelpers::CreateMaterialInstanceCopy(HeadMaterialSetDynamic.EyeLeft, InOuterForGeneratedAssets),
			.EyeRight = UE::MetaHuman::PaletteUnpackHelpers::CreateMaterialInstanceCopy(HeadMaterialSetDynamic.EyeRight, InOuterForGeneratedAssets),
			.EyeShell = UE::MetaHuman::PaletteUnpackHelpers::CreateMaterialInstanceCopy(HeadMaterialSetDynamic.EyeShell, InOuterForGeneratedAssets),
			.LacrimalFluid = UE::MetaHuman::PaletteUnpackHelpers::CreateMaterialInstanceCopy(HeadMaterialSetDynamic.LacrimalFluid, InOuterForGeneratedAssets),
			.Teeth = UE::MetaHuman::PaletteUnpackHelpers::CreateMaterialInstanceCopy(HeadMaterialSetDynamic.Teeth, InOuterForGeneratedAssets),
			.Eyelashes = UE::MetaHuman::PaletteUnpackHelpers::CreateMaterialInstanceCopy(HeadMaterialSetDynamic.Eyelashes, InOuterForGeneratedAssets),
			.EyelashesHiLods = UE::MetaHuman::PaletteUnpackHelpers::CreateMaterialInstanceCopy(HeadMaterialSetDynamic.EyelashesHiLods, InOuterForGeneratedAssets)
		};

		HeadMaterialSetDynamic.ForEachSkinMaterial<UMaterialInstance>(
			[&FaceMaterialSet, InOuterForGeneratedAssets](EMetaHumanCharacterSkinMaterialSlot Slot, UMaterialInstance* Material)
			{
				FaceMaterialSet.Skin.FindOrAdd(Slot) = UE::MetaHuman::PaletteUnpackHelpers::CreateMaterialInstanceCopy(Material, InOuterForGeneratedAssets);
			}
		);

		// Assign the material instance constants to the output meshes
		FMetaHumanCharacterSkinMaterials::SetHeadMaterialsOnMesh(FaceMaterialSet, OutGeneratedAssets.FaceMesh);

		{
			// Generate the metadata for each material in the set
			FaceMaterialSet.ForEachSkinMaterial<UMaterialInstance>(
				[&OutGeneratedAssets](EMetaHumanCharacterSkinMaterialSlot Slot, UMaterialInstance* Material)
				{
					const FString AssetName = FString::Format(TEXT("MI_Face_Skin_{0}"), { StaticEnum<EMetaHumanCharacterSkinMaterialSlot>()->GetAuthoredNameStringByValue((int64) Slot) });
					OutGeneratedAssets.Metadata.Emplace(Material, TEXT("Face/Materials"), AssetName);
				}
			);

			OutGeneratedAssets.Metadata.Emplace(FaceMaterialSet.Eyelashes, TEXT("Face/Materials"), TEXT("MI_Face_Eyelashes"));
			OutGeneratedAssets.Metadata.Emplace(FaceMaterialSet.EyelashesHiLods, TEXT("Face/Materials"), TEXT("MI_Face_EyelashesHiLODs"));
			OutGeneratedAssets.Metadata.Emplace(FaceMaterialSet.EyeLeft, TEXT("Face/Materials"), TEXT("MI_Face_Eye_Left"));
			OutGeneratedAssets.Metadata.Emplace(FaceMaterialSet.EyeRight, TEXT("Face/Materials"), TEXT("MI_Face_Eye_Right"));
			OutGeneratedAssets.Metadata.Emplace(FaceMaterialSet.EyeShell, TEXT("Face/Materials"), TEXT("MI_Face_EyeShell"));
			OutGeneratedAssets.Metadata.Emplace(FaceMaterialSet.LacrimalFluid, TEXT("Face/Materials"), TEXT("MI_Face_LacrimalFluid"));
			OutGeneratedAssets.Metadata.Emplace(FaceMaterialSet.Teeth, TEXT("Face/Materials"), TEXT("MI_Face_Teeth"));
		}

		UMaterialInstanceDynamic* BodyPreviewMaterialInstance = CharacterData->BodyMaterial;
		check(BodyPreviewMaterialInstance);

		UMaterialInstance* BodySkinMaterial = UE::MetaHuman::PaletteUnpackHelpers::CreateMaterialInstanceCopy(BodyPreviewMaterialInstance, InOuterForGeneratedAssets);
		OutGeneratedAssets.Metadata.Emplace(BodySkinMaterial, TEXT("Body/Materials"), TEXT("MI_Body_Skin"));

		// Assign the body material
		check(!OutGeneratedAssets.BodyMesh->GetMaterials().IsEmpty());
		OutGeneratedAssets.BodyMesh->GetMaterials()[0].MaterialInterface = BodySkinMaterial;

		for (TPair<EFaceTextureType, TObjectPtr<UTexture2D>>& Pair : OutGeneratedAssets.SynthesizedFaceTextures)
		{
			if (Pair.Value)
			{
				FString TextureName(TEXT("T_Face_"));
				TextureName += StaticEnum<EFaceTextureType>()->GetAuthoredNameStringByValue(static_cast<int64>(Pair.Key));

				// TODO: If user doesn't set the preview material type to be Editable (aka still uses gray textures) then "editable"
				// materials and textures will still get exported, but the texture data buffer will be null causing the texture
				// source to be empty, effectively making textures black on reload.

				const TFuture<FSharedBuffer>& TextureDataFuture = InCharacter->GetSynthesizedFaceTextureDataAsync(Pair.Key);
				TextureDataFuture.Wait();

				// Textures generated for the assembly should contain the source data
				ensureAlwaysMsgf(Pair.Value->Source.IsValid(), TEXT("Texture generated for assembly without source data."));
				if (!Pair.Value->Source.IsValid())
				{
					// Initialize the source from the original data if needed
					Pair.Value->Source.Init(FImageView(
						const_cast<void*>(TextureDataFuture.Get().GetData()), // ImageView expects non-const, but Source.Init uses const void*
						Pair.Value->GetSizeX(),
						Pair.Value->GetSizeY(),
						FImageCoreUtils::GetRawImageFormatForPixelFormat(Pair.Value->GetPixelFormat())));
				}

				OutGeneratedAssets.Metadata.Emplace(Pair.Value, TEXT("Face/Textures"), TextureName);
			}
		}

		for (TPair<EBodyTextureType, TObjectPtr<UTexture2D>>& Pair : OutGeneratedAssets.BodyTextures)
		{
			if (Pair.Value && InCharacter->HighResBodyTexturesInfo.Contains(Pair.Key))
			{
				FString TextureName(TEXT("T_"));
				TextureName += StaticEnum<EFaceTextureType>()->GetAuthoredNameStringByValue(static_cast<int64>(Pair.Key));

				// TODO: If user doesn't set the preview material type to be Editable (aka still uses gray textures) then "editable"
				// materials and textures will still get exported, but the texture data buffer will be null causing the texture
				// source to be empty, effectively making textures black on reload.

				const TFuture<FSharedBuffer>& PayloadData = InCharacter->GetHighResBodyTextureDataAsync(Pair.Key);
				PayloadData.Wait();
				const FSharedBuffer& Payload = PayloadData.Get();
				if (!Payload.IsNull())
				{
					ensureAlwaysMsgf(Pair.Value->Source.IsValid(), TEXT("Texture generated for assembly without source data."));
					if (!Pair.Value->Source.IsValid())
					{
						// Initialize the source from the original data if needed
						Pair.Value->Source.Init(FImageView(
							const_cast<void*>(Payload.GetData()), // ImageView expects non-const, but Source.Init uses const void*
							Pair.Value->GetSizeX(),
							Pair.Value->GetSizeY(),
							FImageCoreUtils::GetRawImageFormatForPixelFormat(Pair.Value->GetPixelFormat())));
					}

					OutGeneratedAssets.Metadata.Emplace(Pair.Value, TEXT("Body/Textures"), TextureName);
				}
			}
		}
	}

	if (InOptions.bGenerateMergedHeadAndBodyMesh)
	{
		OutGeneratedAssets.MergedHeadAndBodyMesh = FMetaHumanCharacterEditorBuild::MergeHeadAndBody_CreateTransient(
			CharacterData->FaceMesh,
			CharacterData->BodyMesh,
			InOuterForGeneratedAssets,
			ELodUpdateOption::All
		);

		if (OutGeneratedAssets.MergedHeadAndBodyMesh)
		{
			OutGeneratedAssets.Metadata.Emplace(OutGeneratedAssets.MergedHeadAndBodyMesh, TEXT("Body"), TEXT("SKM_MergedHeadAndBody"));
		}
	}

	// Clear the transient flag from all generated objects, and move them into the designated outer, 
	// so that they can be saved.
	for (FMetaHumanGeneratedAssetMetadata& Metadata : OutGeneratedAssets.Metadata)
	{
		if (Metadata.Object->GetOuter() != InOuterForGeneratedAssets)
		{
			// Try to keep the same name in the new outer package to improve debugging
			const FName NewName = MakeUniqueObjectName(InOuterForGeneratedAssets, Metadata.Object->GetClass(), Metadata.Object->GetFName());
			Metadata.Object->Rename(*NewName.ToString(), InOuterForGeneratedAssets);
		}

		Metadata.Object->ClearFlags(RF_Transient);
	}

	FMetaHumanCommonDataUtils::SetPostProcessAnimBP(OutGeneratedAssets.FaceMesh, FMetaHumanCommonDataUtils::GetCharacterPluginFacePostProcessABPPath());
	FMetaHumanCommonDataUtils::SetPostProcessAnimBP(OutGeneratedAssets.BodyMesh, FMetaHumanCommonDataUtils::GetCharacterPluginBodyPostProcessABPPath());

	// Set preview mesh on physics asset after it has been moved
	OutGeneratedAssets.PhysicsAsset->SetPreviewMesh(OutGeneratedAssets.BodyMesh);
	OutGeneratedAssets.BodyMesh->SetPhysicsAsset(OutGeneratedAssets.PhysicsAsset);

	if (InOptions.bGenerateBodyMeasurements)
	{
		// Generate measurements using the model
		const bool bGetMeasuresFromDNA = GetRiggingState(InCharacter) == EMetaHumanCharacterRigState::Rigged;
		OutGeneratedAssets.BodyMeasurements = GetFaceAndBodyMeasurements(CharacterData.ToSharedRef(), bGetMeasuresFromDNA);

		// If we're also producing a merged mesh, attach the measurements to the mesh
		if (OutGeneratedAssets.MergedHeadAndBodyMesh)
		{
			UChaosOutfitAssetBodyUserData* BodyUserData = OutGeneratedAssets.MergedHeadAndBodyMesh->GetAssetUserData<UChaosOutfitAssetBodyUserData>();

			if (!BodyUserData)
			{
				BodyUserData = NewObject<UChaosOutfitAssetBodyUserData>(OutGeneratedAssets.MergedHeadAndBodyMesh);
				OutGeneratedAssets.MergedHeadAndBodyMesh->AddAssetUserData(BodyUserData);
			}

			BodyUserData->Measurements = OutGeneratedAssets.BodyMeasurements;
		}
	}

	return true;
}

bool UMetaHumanCharacterEditorSubsystem::TryGetCharacterPreviewAssets(
	TNotNull<const UMetaHumanCharacter*> InCharacter,
	FMetaHumanCharacterPreviewAssets& OutPreviewAssets)
{
	return TryGetCharacterPreviewAssets(InCharacter, FMetaHumanCharacterPreviewAssetOptions(), OutPreviewAssets);
}

bool UMetaHumanCharacterEditorSubsystem::TryGetCharacterPreviewAssets(
	TNotNull<const UMetaHumanCharacter*> InCharacter,
	const FMetaHumanCharacterPreviewAssetOptions& InOptions,
	FMetaHumanCharacterPreviewAssets& OutPreviewAssets)
{
	OutPreviewAssets = FMetaHumanCharacterPreviewAssets();

	TSharedRef<FMetaHumanCharacterEditorData>* CharacterDataPtr = CharacterDataMap.Find(InCharacter);
	if (!CharacterDataPtr)
	{
		return false;
	}

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = *CharacterDataPtr;

	OutPreviewAssets.FaceMesh = CharacterData->FaceMesh;
	OutPreviewAssets.BodyMesh = CharacterData->BodyMesh;

	if (InOptions.bGenerateMergedHeadAndBodyMesh)
	{
		if (InCharacter->HasFaceDNA())
		{
			OutPreviewAssets.MergedHeadAndBodyMesh = FMetaHumanCharacterEditorBuild::MergeHeadAndBody_CreateTransient(
					CharacterData->FaceMesh,
					CharacterData->BodyMesh,
					GetTransientPackage(),
					ELodUpdateOption::All
				);
		}
		else
		{
			if (!CharacterData->PreviewMergedHeadAndBody)
			{
				CharacterData->PreviewMergedHeadAndBody = FMetaHumanCharacterEditorBuild::MergeHeadAndBody_CreateTransient(
					CharacterData->FaceMesh,
					CharacterData->BodyMesh,
					GetTransientPackage(),
					ELodUpdateOption::LOD0Only,
					&CharacterData->PreviewMergedMeshMapping
				);
			}

			OutPreviewAssets.MergedHeadAndBodyMesh = CharacterData->PreviewMergedHeadAndBody;
		}
	}

	if (InOptions.bGenerateBodyMeasurements)
	{
		// Generate measurements using the model
		const bool bGetMeasuresFromDNA = GetRiggingState(InCharacter) == EMetaHumanCharacterRigState::Rigged;
		OutPreviewAssets.BodyMeasurements = GetFaceAndBodyMeasurements(CharacterData, bGetMeasuresFromDNA);

		// If we're also producing a merged mesh, attach the measurements to the mesh
		if (OutPreviewAssets.MergedHeadAndBodyMesh)
		{
			UChaosOutfitAssetBodyUserData* BodyUserData = OutPreviewAssets.MergedHeadAndBodyMesh->GetAssetUserData<UChaosOutfitAssetBodyUserData>();

			if (!BodyUserData)
			{
				BodyUserData = NewObject<UChaosOutfitAssetBodyUserData>(OutPreviewAssets.MergedHeadAndBodyMesh);
				OutPreviewAssets.MergedHeadAndBodyMesh->AddAssetUserData(BodyUserData);
			}

			BodyUserData->Measurements = OutPreviewAssets.BodyMeasurements;
		}
	}

	return true;
}

EMetaHumanCharacterRigState UMetaHumanCharacterEditorSubsystem::GetRiggingState(TNotNull<const UMetaHumanCharacter*> InCharacter) const
{
	if (InCharacter->HasFaceDNA())
	{
		return EMetaHumanCharacterRigState::Rigged;
	}
	
	if (IsAutoRiggingFace(InCharacter))
	{
		return EMetaHumanCharacterRigState::RigPending;
	}

	return EMetaHumanCharacterRigState::Unrigged;
}


bool UMetaHumanCharacterEditorSubsystem::CanBuildMetaHuman(TNotNull<const UMetaHumanCharacter*> InCharacter, FText& OutErrorMessage)
{
	OutErrorMessage = FText::GetEmpty();

	if (!IsObjectAddedForEditing(InCharacter))
	{
		OutErrorMessage = LOCTEXT("BuildDisabled_CharacterNotLoaded", "Character data is not loaded.");
		return false;
	}

	if (IsRequestingHighResolutionTextures(InCharacter))
	{
		OutErrorMessage = LOCTEXT("BuildDisabled_RequestingHighResTextures", "Requesting HighRes texture in progress.");
		return false;
	}

	if (IsAutoRiggingFace(InCharacter))
	{
		OutErrorMessage = LOCTEXT("BuildDisabled_FaceAutoRigInProgress", "Face auto rig in progress.");
		return false;
	}

	if (GetRiggingState(InCharacter) != EMetaHumanCharacterRigState::Rigged)
	{
		OutErrorMessage = LOCTEXT("BuildDisabled_NoFaceDNA", "Character is not rigged.");
		return false;
	}

	// TODO: ensure override textures set this property
	// This is a restriction at the moment since the assembly expects the animated maps which become available once high rez textures are downloaded
	if (!InCharacter->HasHighResolutionTextures())
	{
		OutErrorMessage = LOCTEXT("BuildDisabled_NoHighResolutionTextures", "The Character is missing textures, use Download Texture Sources to create them before assembling");
		return false;
	}

	return true;
}

bool UMetaHumanCharacterEditorSubsystem::CanBuildMetaHuman(const UMetaHumanCharacter* InCharacter, bool bInLogError)
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CanBuildMetaHuman called with invalid character");
		return false;
	}

	FText ErrorMessage;
	if (!CanBuildMetaHuman(InCharacter, ErrorMessage))
	{
		if (bInLogError)
		{
			UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to build MetaHuman Character '{Character}: {Reason}", InCharacter->GetName(), ErrorMessage.ToString());
		}
		return false;
	}

	return true;
}

void UMetaHumanCharacterEditorSubsystem::BuildMetaHuman(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterEditorBuildParameters& InParams)
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "BuildDefaultMetaHuman called with invalid character");
		return;
	}

	FText ErrorMessage;
	if (!CanBuildMetaHuman(InCharacter, ErrorMessage))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "MetaHuman Character '{Character}' not ready for assembly: {Reason}", InCharacter->GetName(), ErrorMessage.ToString());
		return;
	}

	if (InParams.PipelineType == EMetaHumanDefaultPipelineType::DCC)
	{
		FMetaHumanCharacterEditorDCCExportParameters DDCExportParams;
		DDCExportParams.OutputFolderPath = InParams.AbsoluteBuildPath;
		DDCExportParams.bBakeFaceMakeup = InParams.bBakeMakeup;
		DDCExportParams.bExportZipFile = InParams.bExportZipFile;
		DDCExportParams.ArchiveName = InParams.NameOverride;

		FMetaHumanCharacterEditorDCCExport::ExportCharacterForDCC(InCharacter, DDCExportParams);
	}
	else
	{	
		FMetaHumanCharacterEditorBuildParameters DefaultBuildParams = InParams;

		if (DefaultBuildParams.PipelineOverride == nullptr)
		{
			TSoftClassPtr<UMetaHumanCollectionPipeline> PipelineClass = FMetaHumanCharacterEditorBuild::GetDefaultPipelineClass(InParams.PipelineType, InParams.PipelineQuality);
			if (!PipelineClass.IsNull())
			{
				DefaultBuildParams.PipelineOverride = NewObject<UMetaHumanCollectionPipeline>(InCharacter, PipelineClass.LoadSynchronous());
			}
			else
			{
				const FString PipelineTypeStr = StaticEnum<EMetaHumanDefaultPipelineType>()->GetAuthoredNameStringByValue((int64) InParams.PipelineType);
				const FString QualityStr = StaticEnum<EMetaHumanQualityLevel>()->GetAuthoredNameStringByValue((int64) InParams.PipelineQuality);
				UE_LOGFMT(LogMetaHumanCharacterEditor,
						  Error,
						  "Failed to build MetaHuman Character '{Character}: Default pipeline not found for type '{Pipeline}' and quality '{Quality}'",
						  InCharacter->GetName(),
						  PipelineTypeStr,
						  QualityStr);
				return;
			}
		}

		FMetaHumanCharacterEditorBuild::BuildMetaHumanCharacter(InCharacter, DefaultBuildParams);

		// Reinit Animation for MH Actor
		if (AMetaHumanInvisibleDrivingActor* InvisibleActor = GetInvisibleDrivingActor(InCharacter))
		{
			if (USkeletalMeshComponent* SkelMeshComponent = InvisibleActor->GetSkeletalMeshComponent())
			{
				ForEachCharacterActor(CharacterDataMap[InCharacter], [SkelMeshComponent](TScriptInterface<IMetaHumanCharacterEditorActorInterface> Actor)
									  {
										  Actor->SetDrivingSkeletalMesh(SkelMeshComponent);
										  Actor->ReinitAnimation();
									  });
			}
		}
	}
}

void UMetaHumanCharacterEditorSubsystem::GetMaterialSetForCharacter(TNotNull<const UMetaHumanCharacter*> InCharacter, FMetaHumanCharacterFaceMaterialSet& OutFaceMaterials, UMaterialInstanceDynamic*& OutBodyMaterial)
{
	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();

	const bool bUseFaceMaterialsWithVTSupport = Settings->ShouldUseVirtualTextures() && InCharacter->HasHighResolutionTextures();
	const bool bUseBodyMaterialsWithVTSupport = Settings->ShouldUseVirtualTextures();

	OutFaceMaterials = FMetaHumanCharacterSkinMaterials::GetHeadPreviewMaterialInstance(EMetaHumanCharacterSkinPreviewMaterial::Editable, bUseFaceMaterialsWithVTSupport);
	OutBodyMaterial = FMetaHumanCharacterSkinMaterials::GetBodyPreviewMaterialInstance(EMetaHumanCharacterSkinPreviewMaterial::Editable, bUseBodyMaterialsWithVTSupport);

	FMetaHumanCharacterSkinMaterials::ApplySynthesizedTexturesToFaceMaterial(OutFaceMaterials, InCharacter->SynthesizedFaceTextures);
	FMetaHumanCharacterSkinMaterials::ApplySkinParametersToMaterials(OutFaceMaterials, OutBodyMaterial, InCharacter->SkinSettings);
	FMetaHumanCharacterSkinMaterials::ApplyEyeSettingsToMaterial(OutFaceMaterials, InCharacter->EyesSettings);
	FMetaHumanCharacterSkinMaterials::ApplyFoundationMakeupToMaterial(OutFaceMaterials, InCharacter->MakeupSettings.Foundation, bUseFaceMaterialsWithVTSupport);
	FMetaHumanCharacterSkinMaterials::ApplyEyeMakeupToMaterial(OutFaceMaterials, InCharacter->MakeupSettings.Eyes, bUseFaceMaterialsWithVTSupport);
	FMetaHumanCharacterSkinMaterials::ApplyBlushMakeupToMaterial(OutFaceMaterials, InCharacter->MakeupSettings.Blush, bUseFaceMaterialsWithVTSupport);
	FMetaHumanCharacterSkinMaterials::ApplyLipsMakeupToMaterial(OutFaceMaterials, InCharacter->MakeupSettings.Lips, bUseFaceMaterialsWithVTSupport);

	// Update the Body Material Parameters to match
	FMetaHumanCharacterBodyTextureUtils::GetSkinToneAndUpdateMaterials(InCharacter->SkinSettings.Skin,
																	   FaceTextureSynthesizer,
																	   InCharacter->BodyTextures,
																	   OutFaceMaterials,
																	   OutBodyMaterial);
}

UMaterialInterface* UMetaHumanCharacterEditorSubsystem::GetTranslucentClothingMaterial() const
{
	return LoadObject<UMaterialInterface>(nullptr, TEXT("/" UE_PLUGIN_NAME "/Clothing/MI_TranslucentClothing.MI_TranslucentClothing"));
}

bool UMetaHumanCharacterEditorSubsystem::IsCharacterOutfitSelected(TNotNull<const UMetaHumanCharacter*> InMetaHumanCharacter)
{
	bool bIsOutfitSelected = false;

	if (const UMetaHumanCollection* CharacterCollection = InMetaHumanCharacter->GetInternalCollection())
	{
		// Find the slot names for any slots supporting outfits
		TArray<FName> OutfitSlots;
		if (CharacterCollection->GetPipeline())
		{
			TNotNull<const UMetaHumanCharacterPipelineSpecification*> Specification = CharacterCollection->GetPipeline()->GetSpecification();
			for (const TPair<FName, FMetaHumanCharacterPipelineSlot>& Pair : Specification->Slots)
			{
				if (Pair.Value.SupportsAssetType(UChaosOutfitAsset::StaticClass()))
				{
					OutfitSlots.Add(Pair.Key);
				}
			}
		}

		// Check if the outfit slots have any built data
		const FMetaHumanCollectionBuiltData& CollectionBuiltData = CharacterCollection->GetBuiltData(EMetaHumanCharacterPaletteBuildQuality::Preview);
		if (!OutfitSlots.IsEmpty() && CollectionBuiltData.IsValid())
		{
			for (const FMetaHumanCharacterPaletteItem& Item : CharacterCollection->GetItems())
			{
				if (Item.GetItemKey() != FMetaHumanPaletteItemKey()
					&& OutfitSlots.Contains(Item.SlotName))
				{
					if (CollectionBuiltData.PaletteBuiltData.HasBuildOutputForItem(FMetaHumanPaletteItemPath{ Item.GetItemKey() }))
					{
						bIsOutfitSelected = true;
						break;
					}
				}
			}
		}
	}

	return bIsOutfitSelected;
}

void UMetaHumanCharacterEditorSubsystem::ApplyFaceState(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterIdentity::FState> InState)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	// Since this function is publicly accessible, we copy the passed-in state so that the caller
	// can't keep a mutable reference to the subsystem's edit state.
	ApplyFaceState(CharacterData, MakeShared<FMetaHumanCharacterIdentity::FState>(*InState));

}

void UMetaHumanCharacterEditorSubsystem::ApplyFaceState(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, TSharedRef<FMetaHumanCharacterIdentity::FState> InState)
{
	InCharacterData->FaceState = InState;

	FMetaHumanCharacterFaceEvaluationSettings FaceEvaluationSettings = InCharacterData->FaceEvaluationSettings.Get({});
	FaceEvaluationSettings.GlobalDelta = InState->GetSettings().GlobalVertexDeltaScale();
	FaceEvaluationSettings.HighFrequencyDelta = InState->GetSettings().GlobalHighFrequencyScale();
	FaceEvaluationSettings.HeadScale = InState->GetFaceScale();
	InCharacterData->FaceEvaluationSettings = FaceEvaluationSettings;
	
	UpdateFaceMeshInternal(InCharacterData, InState->Evaluate(), ELodUpdateOption::All);
	
	FMetaHumanCharacterSkelMeshUtils::UpdateMeshDescriptionFromLODModelVerticesNormalsAndTangents(InCharacterData->FaceMesh);
	
	InCharacterData->OnFaceStateChangedDelegate.Broadcast();
}

TSharedRef<const FMetaHumanCharacterIdentity::FState> UMetaHumanCharacterEditorSubsystem::GetFaceState(TNotNull<const UMetaHumanCharacter*> InCharacter) const
{
	TSharedRef<const FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	return CharacterData->FaceState;
}

TSharedRef<FMetaHumanCharacterIdentity::FState> UMetaHumanCharacterEditorSubsystem::CopyFaceState(TNotNull<const UMetaHumanCharacter*> InCharacter) const
{
	return MakeShared<FMetaHumanCharacterIdentity::FState>(*GetFaceState(InCharacter));
}

void UMetaHumanCharacterEditorSubsystem::CommitFaceState(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterIdentity::FState> InState)
{
	FSharedBuffer FaceStateData;
	InState->Serialize(FaceStateData);
	
	InCharacter->SetFaceStateData(FaceStateData);
	InCharacter->FaceEvaluationSettings.GlobalDelta = InState->GetSettings().GlobalVertexDeltaScale();
	InCharacter->FaceEvaluationSettings.HighFrequencyDelta = InState->GetSettings().GlobalHighFrequencyScale();
	InCharacter->FaceEvaluationSettings.HeadScale = InState->GetFaceScale();
	InCharacter->MarkPackageDirty();

	ApplyFaceState(InCharacter, InState);
}

void UMetaHumanCharacterEditorSubsystem::CommitFaceState(UMetaHumanCharacter* InCharacter)
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CommitState called with invalid character");
		return;
	}

	if (!IsObjectAddedForEditing(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CommitState called with {Character} that is not not added for editing", InCharacter->GetName());
		return;
	}

	CommitFaceState(InCharacter, GetFaceState(InCharacter));
}

bool UMetaHumanCharacterEditorSubsystem::CompareFaceTextures(const UMetaHumanCharacter* InCharacter1, const UMetaHumanCharacter* InCharacter2, int32 InPixelTolerance)
{
	if (!IsValid(InCharacter1))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CompareFaceTextures called with invalid character 1");
		return false;
	}

	if (!IsValid(InCharacter2))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CompareFaceTextures called with invalid character 2");
		return false;
	}

	for (const TPair<EFaceTextureType, FMetaHumanCharacterTextureInfo>& FaceTextureInfoPair : InCharacter1->SynthesizedFaceTexturesInfo)
	{
		EFaceTextureType TextureType = FaceTextureInfoPair.Key;
		const FString TextureTypeString = StaticEnum<EFaceTextureType>()->GetNameStringByValue((int64)TextureType);

		FSharedBuffer SynthesizedImageBuffer1 = InCharacter1->GetSynthesizedFaceTextureDataAsync(TextureType).Get();
		FSharedBuffer SynthesizedImageBuffer2 = InCharacter2->GetSynthesizedFaceTextureDataAsync(TextureType).Get();

		if (!SynthesizedImageBuffer1.IsNull() && !SynthesizedImageBuffer2.IsNull())
		{
			const bool bBuffersSame = UE::MetaHuman::CompareBuffers(SynthesizedImageBuffer1, SynthesizedImageBuffer2, InPixelTolerance);
			if (!bBuffersSame)
			{
				UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Synthesized face textures for {TextureType} are different for characters {Character1} and {Character2}", TextureTypeString, InCharacter1->GetName(), InCharacter2->GetName());
				return false;
			}
		}
	}

	return true;
}


bool UMetaHumanCharacterEditorSubsystem::CompareFaceState(const UMetaHumanCharacter* InCharacter1, const UMetaHumanCharacter* InCharacter2, float InTolerance) const
{
	if (!IsValid(InCharacter1))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CompareFaceState called with invalid character 1");
		return false;
	}

	if (!IsObjectAddedForEditing(InCharacter1))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CompareFaceState called with {Character} that is not not added for editing", InCharacter1->GetName());
		return false;
	}

	if (!IsValid(InCharacter2))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CompareFaceState called with invalid character 2");
		return false;
	}

	if (!IsObjectAddedForEditing(InCharacter2))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CompareFaceState called with {Character} that is not not added for editing", InCharacter2->GetName());
		return false;
	} 

	TSharedRef<const FMetaHumanCharacterIdentity::FState> FaceState1 = GetFaceState(InCharacter1);
	TSharedRef<const FMetaHumanCharacterIdentity::FState> FaceState2 = GetFaceState(InCharacter2);

	const FMetaHumanRigEvaluatedState VerticesAndNormals1 = FaceState1->Evaluate();
	const FMetaHumanRigEvaluatedState VerticesAndNormals2 = FaceState2->Evaluate();

	if (VerticesAndNormals1.Vertices.Num() != VerticesAndNormals2.Vertices.Num() || VerticesAndNormals1.VertexNormals.Num() != VerticesAndNormals2.VertexNormals.Num())
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CompareFaceState vertex and vertex normal data contains inconsistent numbers of elements for the current data and gold data");
		return false;
	}

	bool bValidVertices = true;
	bool bValidNormals = true;

	for (int32 V = 0; V < VerticesAndNormals1.Vertices.Num(); ++V)
	{
		const bool bVerticesEqual = VerticesAndNormals1.Vertices[V].Equals(VerticesAndNormals2.Vertices[V], InTolerance);
		if (!bVerticesEqual)
		{
			const float Diff = (VerticesAndNormals1.Vertices[V] - VerticesAndNormals2.Vertices[V]).Length();
			UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Vertex {VertexIndex} does not match between current and gold data, difference = {Difference}", V, Diff);
			bValidVertices = false;
		}
	}

	for (int32 V = 0; V < VerticesAndNormals1.VertexNormals.Num(); ++V)
	{
		const bool bNormalsEqual = VerticesAndNormals1.VertexNormals[V].Equals(VerticesAndNormals2.VertexNormals[V], InTolerance);
		if (!bNormalsEqual)
		{
			const float Diff = (VerticesAndNormals1.VertexNormals[V] - VerticesAndNormals2.VertexNormals[V]).Length();
			UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Vertex Normal {VertexIndex} does not match between current and gold data, difference = {Difference}", V, Diff);
			bValidNormals = false;
		}
	}

	return bValidVertices && bValidNormals;
}


bool UMetaHumanCharacterEditorSubsystem::CompareBodyState(const UMetaHumanCharacter* InCharacter1, const UMetaHumanCharacter* InCharacter2, float InTolerance) const
{
	if (!IsValid(InCharacter1))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CompareBodyState called with invalid character 1");
		return false;
	}

	if (!IsObjectAddedForEditing(InCharacter1))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CompareBodyState called with {Character} that is not not added for editing", InCharacter1->GetName());
		return false;
	}

	if (!IsValid(InCharacter2))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CompareBodyState called with invalid character 2");
		return false;
	}

	if (!IsObjectAddedForEditing(InCharacter2))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CompareBodyState called with {Character} that is not not added for editing", InCharacter2->GetName());
		return false;
	}

	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> BodyState1 = GetBodyState(InCharacter1);
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> BodyState2 = GetBodyState(InCharacter2);

	const FMetaHumanRigEvaluatedState VerticesAndNormals1 = BodyState1->GetVerticesAndVertexNormals();
	const FMetaHumanRigEvaluatedState VerticesAndNormals2 = BodyState2->GetVerticesAndVertexNormals();

	if (VerticesAndNormals1.Vertices.Num() != VerticesAndNormals2.Vertices.Num() || VerticesAndNormals1.VertexNormals.Num() != VerticesAndNormals2.VertexNormals.Num())
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CompareBodyState vertex and vertex normal data contains inconsistent numbers of elements for the current data and gold data");
		return false;
	}

	for (int32 V = 0; V < VerticesAndNormals1.Vertices.Num(); ++V)
	{
		const bool bVerticesEqual = VerticesAndNormals1.Vertices[V].Equals(VerticesAndNormals2.Vertices[V], InTolerance);
		if (!bVerticesEqual)
		{
			const float Diff = (VerticesAndNormals1.Vertices[V] - VerticesAndNormals2.Vertices[V]).Length();
			UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Vertex {VertexIndex} does not match between current and gold data, difference = {Difference}", V, Diff);
			return false;
		}
	}

	for (int32 V = 0; V < VerticesAndNormals1.VertexNormals.Num(); ++V)
	{
		const bool bNormalsEqual = VerticesAndNormals1.VertexNormals[V].Equals(VerticesAndNormals2.VertexNormals[V], InTolerance);
		if (!bNormalsEqual)
		{
			const float Diff = (VerticesAndNormals1.VertexNormals[V] - VerticesAndNormals2.VertexNormals[V]).Length();
			UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Vertex Normal {VertexIndex} does not match between current and gold data, difference = {Difference}", V, Diff);
			return false;
		}
	}

	return true;
}



TSharedPtr<IDNAReader> UMetaHumanCharacterEditorSubsystem::ApplyFaceDNA(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedPtr<IDNAReader> InFaceDNAReader, ELodUpdateOption InLodUpdateOption, bool bInResettingToArchetypeDNA)
{
	bool bDnaApplied = false;

	UE::MetaHuman::ERigType RigType = UE::MetaHuman::ERigType::JointsOnly;

	if (InFaceDNAReader->GetBlendShapeChannelCount() > 0)
	{
		RigType = UE::MetaHuman::ERigType::JointsAndBlendshapes;
	}

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	TSharedPtr<IDNAReader> Dna;

	USkeletalMesh* FaceSkeletalMesh = CharacterData->FaceMesh;

	if (FMetaHumanCharacterSkelMeshUtils::CheckDNACompatibility(UE::MetaHuman::GetDNAReader(FaceSkeletalMesh).Get(), InFaceDNAReader.Get()))
	{
		if (UDNAAsset* BodyDNAAsset = CharacterData->BodyMesh->GetAssetUserData<UDNAAsset>())
		{
			TSharedPtr<IDNAReader> BodyDna;
			if (InCharacter->bFixedBodyType && InCharacter->HasBodyDNA())
			{
				TArray<uint8> DNABuffer = InCharacter->GetBodyDNABuffer();
				BodyDna = ReadDNAFromBuffer(&DNABuffer, EDNADataLayer::All);
			}
			else
			{
				BodyDna = CharacterData->BodyState->StateToDna(BodyDNAAsset); // ensure body DNA is updated from body state
			}

			
			TSharedPtr<FMetaHumanCharacterIdentity> FaceCharacterIdentity = GetOrCreateCharacterIdentity(InCharacter->TemplateType).Face;
			const bool bUpdateDescendentFaceJoints = bInResettingToArchetypeDNA; 
			Dna = FaceCharacterIdentity->CopyBodyJointsToFace(BodyDna->Unwrap(), InFaceDNAReader->Unwrap(), bUpdateDescendentFaceJoints);
			// copy the vertex influence weights from the combined model
			TArray<TPair<int32, TArray<FFloatTriplet>>>  CombinedModelVertexInfluenceWeights;
			CharacterData->BodyState->CopyCombinedModelVertexInfluenceWeights(CombinedModelVertexInfluenceWeights);
			Dna = FaceCharacterIdentity->UpdateFaceSkinWeightsFromBodyAndVertexNormals(CombinedModelVertexInfluenceWeights, Dna->Unwrap(), CharacterData->FaceState.Get());
		}

		if (RigType == UE::MetaHuman::ERigType::JointsOnly)
		{
			const FMetaHumanRigEvaluatedState VerticesAndNormals = CharacterData->FaceState->Evaluate();

			// Check that DNA vertices are at the same position as current face state.
			// we do not perform this test if we are resetting to the placeholder archetype DNA, as we are not updating the base mesh, and 
			// the state vertices will not match in this case; this does not matter
			if (!bInResettingToArchetypeDNA) 
			{
				if (!FMetaHumanCharacterSkelMeshUtils::CompareDnaToStateVerticesAndNormals(Dna, VerticesAndNormals.Vertices, VerticesAndNormals.VertexNormals, CharacterData->FaceState, /*Tolerance=*/ 0.001f)) 
				{
					UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "DNA vertices or vertex normals mismatch with the State");
				}				
				//return?
			}

			// TODO: verify whether we should actually update everything in the Skel Mesh, if not we need to combine the local archetype with the AR one
			// For now test updating only the new data since the base mesh should match and the skinning may not be correct
			const FMetaHumanCharacterSkelMeshUtils::EUpdateFlags SkelMeshUpdateFlags =
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::Joints |
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::DNABehavior |
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::DNAGeometry |
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::SkinWeights;

			FMetaHumanCharacterSkelMeshUtils::UpdateSkelMeshFromDNA(Dna.ToSharedRef(), SkelMeshUpdateFlags, CharacterData->FaceDnaToSkelMeshMap, EMetaHumanCharacterOrientation::Y_UP, FaceSkeletalMesh);
		

			// this is needed to update the normals
			UpdateFaceMeshInternal(CharacterData, VerticesAndNormals, InLodUpdateOption);

		}
		else
		{
			UpdateCharacterFaceMeshFromDNA(GetTransientPackage(), Dna, CharacterData);
		}
		bDnaApplied = true;
	}

	if (bDnaApplied)
	{
		return Dna;
	}

	return nullptr;
}

void UMetaHumanCharacterEditorSubsystem::EnableSkeletalPostProcessing(TNotNull<const UMetaHumanCharacter*> InCharacter)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	if (USkeletalMesh* FaceMesh = CharacterData->FaceMesh)
	{
		FMetaHumanCommonDataUtils::SetPostProcessAnimBP(FaceMesh, FMetaHumanCommonDataUtils::GetCharacterPluginFacePostProcessABPPath());
	}

	if (USkeletalMesh* BodyMesh = CharacterData->BodyMesh)
	{
		FMetaHumanCommonDataUtils::SetPostProcessAnimBP(BodyMesh, FMetaHumanCommonDataUtils::GetCharacterPluginBodyPostProcessABPPath());
	}
}

void UMetaHumanCharacterEditorSubsystem::DisableSkeletalPostProcessing(TNotNull<const UMetaHumanCharacter*> InCharacter)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	if (USkeletalMesh* FaceMesh = CharacterData->FaceMesh)
	{
		FMetaHumanCommonDataUtils::SetPostProcessAnimBP(FaceMesh, {});
	}

	if (USkeletalMesh* BodyMesh = CharacterData->BodyMesh)
	{
		FMetaHumanCommonDataUtils::SetPostProcessAnimBP(BodyMesh, {});
	}
}

void UMetaHumanCharacterEditorSubsystem::EnableAnimation(TNotNull<const UMetaHumanCharacter*> InCharacter)
{
	// This function is not needed anymore as enabling/disabling animation is handled internally by the actors.
	// Any code using this function will continue to compile and work as expected
}

void UMetaHumanCharacterEditorSubsystem::DisableAnimation(TNotNull<const UMetaHumanCharacter*> InCharacter)
{
	// This function is not needed anymore as enabling/disabling animation is handled internally by the actors.
	// Any code using this function will continue to compile and work as expected
}

void UMetaHumanCharacterEditorSubsystem::ImportFaceDNA(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedPtr<IDNAReader> InFaceDNAReader)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	FString NewRigAssetName, NewRigPath, DefaultSuffix;

	const FString SanitizedBasePackageName = InCharacter->GetOutermost()->GetName();
	const FString PackagePath = FPackageName::GetLongPackagePath(SanitizedBasePackageName);
	const FString BaseAssetNameWithPrefix = TEXT("SK_") + FPackageName::GetLongPackageAssetName(InFaceDNAReader->GetName());
	const FString SanitizedBaseAssetNameWithPrefix = ObjectTools::SanitizeObjectName(BaseAssetNameWithPrefix);

	AssetTools.CreateUniqueAssetName(PackagePath / SanitizedBaseAssetNameWithPrefix, DefaultSuffix, NewRigPath, NewRigAssetName);
	NewRigPath = FPackageName::GetLongPackagePath(NewRigPath);

	FInterchangeDnaModule& DNAImportModule = FInterchangeDnaModule::GetModule();
	USkeleton* FaceSkel = LoadObject<USkeleton>(nullptr, FMetaHumanCommonDataUtils::GetCharacterPluginFaceSkeletonPath());
	
	USkeletalMesh* RigSkeletalMesh = DNAImportModule.ImportSync(NewRigAssetName, NewRigPath, InFaceDNAReader, FaceSkel);
	FMetaHumanCharacterSkelMeshUtils::PopulateSkelMeshData(RigSkeletalMesh, InFaceDNAReader, true /*bIsFace*/);

	UPackage* NewPackage = CreatePackage(*NewRigPath);
	USkeletalMesh* NewAsset = DuplicateObject(RigSkeletalMesh, NewPackage, FName(NewRigAssetName));

	FAssetRegistryModule::AssetCreated(NewAsset);
}

void UMetaHumanCharacterEditorSubsystem::CommitFaceDNA(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedRef<IDNAReader> InFaceDNAReader)
{
	// Update the actor skeletal meshes
	TSharedPtr<IDNAReader> InDna(InFaceDNAReader);
	TSharedPtr<IDNAReader> OutDna = ApplyFaceDNA(InCharacter, InDna);

	if (OutDna.IsValid())
	{
		InCharacter->SetFaceDNABuffer(ReadStreamFromDNA(OutDna.Get(), EDNADataLayer::All), InFaceDNAReader->GetBlendShapeChannelCount() > 0);
		InCharacter->MarkPackageDirty();

		RunCharacterEditorPipelineForPreview(InCharacter);
	}

	InCharacter->NotifyRiggingStateChanged();
}

void UMetaHumanCharacterEditorSubsystem::ResetCharacterFace(TNotNull<const UMetaHumanCharacter*> InCharacter)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	CharacterData->FaceState->Reset();

	UpdateFaceMeshInternal(CharacterData, CharacterData->FaceState->Evaluate(), GetUpdateOptionForEditing());
}

TArray<FVector3f> UMetaHumanCharacterEditorSubsystem::GetFaceGizmos(TNotNull<const UMetaHumanCharacter*> InCharacter) const
{
	TSharedRef<const FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	return CharacterData->FaceState->EvaluateGizmos(CharacterData->FaceState->Evaluate().Vertices);
}

TArray<FVector3f> UMetaHumanCharacterEditorSubsystem::SetFaceGizmoPosition(
	TNotNull<const UMetaHumanCharacter*> InCharacter,
	TSharedRef<const FMetaHumanCharacterIdentity::FState> InState,
	int32 InGizmoIndex,
	const FVector3f& InPosition,
	bool bInSymmetric,
	bool bInEnforceBounds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::SetFaceGizmoPosition");

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	CharacterData->FaceState = MakeShared<FMetaHumanCharacterIdentity::FState>(*InState);
	CharacterData->FaceState->SetGizmoPosition(InGizmoIndex, InPosition, bInSymmetric, bInEnforceBounds);

	const FMetaHumanRigEvaluatedState FaceVerticesAndVertexNormals = CharacterData->FaceState->Evaluate();
	UpdateFaceMeshInternal(CharacterData, FaceVerticesAndVertexNormals, GetUpdateOptionForEditing());

	CharacterData->OnFaceStateChangedDelegate.Broadcast();

	return CharacterData->FaceState->EvaluateGizmos(FaceVerticesAndVertexNormals.Vertices);
}

TArray<FVector3f> UMetaHumanCharacterEditorSubsystem::SetFaceGizmoRotation(
	TNotNull<const UMetaHumanCharacter*> InCharacter,
	TSharedRef<const FMetaHumanCharacterIdentity::FState> InState,
	int32 InGizmoIndex,
	const FVector3f& InRotation,
	bool bInSymmetric,
	bool bInEnforceBounds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::SetFaceGizmoRotation");

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	CharacterData->FaceState = MakeShared<FMetaHumanCharacterIdentity::FState>(*InState);
	CharacterData->FaceState->SetGizmoRotation(InGizmoIndex, InRotation, bInSymmetric, bInEnforceBounds);

	const FMetaHumanRigEvaluatedState FaceVerticesAndVertexNormals = CharacterData->FaceState->Evaluate();
	UpdateFaceMeshInternal(CharacterData, FaceVerticesAndVertexNormals, GetUpdateOptionForEditing());

	CharacterData->OnFaceStateChangedDelegate.Broadcast();

	return CharacterData->FaceState->EvaluateGizmos(FaceVerticesAndVertexNormals.Vertices);
}

TArray<FVector3f> UMetaHumanCharacterEditorSubsystem::SetFaceGizmoScale(
	TNotNull<const UMetaHumanCharacter*> InCharacter,
	TSharedRef<const FMetaHumanCharacterIdentity::FState> InState,
	int32 InGizmoIndex,
	float InScale,
	bool bInSymmetric,
	bool bInEnforceBounds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::SetFaceGizmoScale");

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	CharacterData->FaceState = MakeShared<FMetaHumanCharacterIdentity::FState>(*InState);
	CharacterData->FaceState->SetGizmoScale(InGizmoIndex, InScale, bInSymmetric, bInEnforceBounds);

	const FMetaHumanRigEvaluatedState FaceVerticesAndVertexNormals = CharacterData->FaceState->Evaluate();
	UpdateFaceMeshInternal(CharacterData, FaceVerticesAndVertexNormals, GetUpdateOptionForEditing());

	CharacterData->OnFaceStateChangedDelegate.Broadcast();

	return CharacterData->FaceState->EvaluateGizmos(FaceVerticesAndVertexNormals.Vertices);
}

TArray<FVector3f> UMetaHumanCharacterEditorSubsystem::GetFaceLandmarks(TNotNull<const UMetaHumanCharacter*> InCharacter) const
{
	TSharedRef<const FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	return CharacterData->FaceState->EvaluateLandmarks(CharacterData->FaceState->Evaluate().Vertices);
}

void UMetaHumanCharacterEditorSubsystem::GetFaceLandmarks(const UMetaHumanCharacter* InCharacter, TArray<FVector>& OutFaceLandmarks) const
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "GetFaceLandmarks called with an invalid character");
		return;
	}

	if (!CharacterDataMap.Contains(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "GetFaceLandmarks called with a character that was not opened for edit. Make sure to call TryAddObjectToEdit before calling this function");
		return;
	}

	const TArray<FVector3f> Landmarks = GetFaceLandmarks(InCharacter);
	Algo::Transform(Landmarks, OutFaceLandmarks, [](const FVector3f& Landmark)
					{
						return FVector(Landmark);
					});
}

TArray<FVector3f> UMetaHumanCharacterEditorSubsystem::TranslateFaceLandmark(
	TNotNull<const UMetaHumanCharacter*> InCharacter, 
	TSharedRef<const FMetaHumanCharacterIdentity::FState> InState, 
	int32 InLandmarkIndex, 
	const FVector3f& InDelta,
	bool bInTranslateSymmetrically)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::TranslateFaceLandmark");

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	CharacterData->FaceState = MakeShared<FMetaHumanCharacterIdentity::FState>(*InState);
	CharacterData->FaceState->TranslateLandmark(InLandmarkIndex, InDelta, bInTranslateSymmetrically);
	
	const FMetaHumanRigEvaluatedState FaceVerticesAndVertexNormals = CharacterData->FaceState->Evaluate();
	UpdateFaceMeshInternal(CharacterData, FaceVerticesAndVertexNormals, GetUpdateOptionForEditing());

	CharacterData->OnFaceStateChangedDelegate.Broadcast();

	return CharacterData->FaceState->EvaluateLandmarks(FaceVerticesAndVertexNormals.Vertices);
}

void UMetaHumanCharacterEditorSubsystem::TranslateFaceLandmarks(const UMetaHumanCharacter* InCharacter, const TArray<int32>& InLandmarkIndices, const TArray<FVector>& InDeltas)
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "TranslateFaceLandmark called with invalid character");
		return;
	}

	if (!IsObjectAddedForEditing(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "TranslateFaceLandmark called with {Character} that is not not added for editing", InCharacter->GetName());
		return;
	}

	if (InLandmarkIndices.Num() != InDeltas.Num())
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "TranslateFaceLandmark called with inconsistent number of Indices ({NumLandmarks}) and Deltas ({NumDeltas})", InLandmarkIndices.Num(), InDeltas.Num());
		return;
	}

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	for (int32 Index = 0; Index < InLandmarkIndices.Num(); ++Index)
	{
		const int32 LandmarkIndex = InLandmarkIndices[Index];
		const FVector& Delta = InDeltas[Index];
		const bool bSymmetric = false;
		CharacterData->FaceState->TranslateLandmark(LandmarkIndex, FVector3f(Delta), bSymmetric);
	}

	const FMetaHumanRigEvaluatedState FaceVerticesAndVertexNormals = CharacterData->FaceState->Evaluate();
	UpdateFaceMeshInternal(CharacterData, FaceVerticesAndVertexNormals, GetUpdateOptionForEditing());

	CharacterData->OnFaceStateChangedDelegate.Broadcast();
}

int32 UMetaHumanCharacterEditorSubsystem::SelectFaceVertex(TNotNull<const UMetaHumanCharacter*> InCharacter, const FRay& InRay, FVector& OutHitVertex, FVector& OutHitNormal)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::SelectFaceVertex");

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	FVector3f HitVertex;
	FVector3f HitNormal;
	FVector3f RayOrigin{ (float)InRay.Origin.X, (float)InRay.Origin.Y, (float)InRay.Origin.Z };
	FVector3f RayDirection{ (float)InRay.Direction.X, (float)InRay.Direction.Y, (float)InRay.Direction.Z };
	int32 HitVertexID = CharacterData->FaceState->SelectFaceVertex(RayOrigin, RayDirection, HitVertex, HitNormal);
	if (HitVertexID != INDEX_NONE)
	{
		OutHitVertex = FVector{ HitVertex.X, HitVertex.Y, HitVertex.Z };
		OutHitNormal = FVector{ HitNormal.X, HitNormal.Y, HitNormal.Z };
	}
	return HitVertexID;
}

void UMetaHumanCharacterEditorSubsystem::AddFaceLandmark(TNotNull<const UMetaHumanCharacter*> InCharacter, int32 InMeshVertexIndex)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	CharacterData->FaceState->AddLandmark(InMeshVertexIndex);
}

void UMetaHumanCharacterEditorSubsystem::RemoveFaceLandmark(TNotNull<const UMetaHumanCharacter*> InCharacter, int32 InLandmarkIndex)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	CharacterData->FaceState->RemoveLandmark(InLandmarkIndex);
}

TArray<FVector3f> UMetaHumanCharacterEditorSubsystem::BlendFaceRegion(TNotNull<const UMetaHumanCharacter*> InCharacter, int32 InRegionIndex, const TSharedPtr<const FMetaHumanCharacterIdentity::FState>& InStartState, const TArray<TSharedPtr<const FMetaHumanCharacterIdentity::FState>>& InPresetStates, TConstArrayView<float> InPresetWeights, EBlendOptions InBlendOptions, bool bInBlendSymmetrically)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::BlendFaceRegion");

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	if (InPresetStates.Num() <= InPresetWeights.Num() && InPresetStates.Num() > 0)
	{
		TArray<TPair<float, const FMetaHumanCharacterIdentity::FState*>> PresetStateWeights;
		for (int32 PresetIndex = 0; PresetIndex < InPresetStates.Num(); PresetIndex++)
		{
			if (InPresetStates[PresetIndex])
			{
				PresetStateWeights.Add({ InPresetWeights[PresetIndex], InPresetStates[PresetIndex].Get() });
			}
		}

		CharacterData->FaceState = MakeShared<FMetaHumanCharacterIdentity::FState>(*InStartState);
		CharacterData->FaceState->BlendPresets(InRegionIndex, PresetStateWeights, InBlendOptions, bInBlendSymmetrically);

		const FMetaHumanRigEvaluatedState FaceVerticesAndVertexNormals = CharacterData->FaceState->Evaluate();
		UpdateFaceMeshInternal(CharacterData, FaceVerticesAndVertexNormals, GetUpdateOptionForEditing());
	}
	return CharacterData->FaceState->EvaluateGizmos(CharacterData->FaceState->Evaluate().Vertices);
}

void UMetaHumanCharacterEditorSubsystem::RemoveFaceRig(TNotNull<UMetaHumanCharacter*> InCharacter)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	// stop any animation if we have any
	AMetaHumanInvisibleDrivingActor* DrivingActor = GetInvisibleDrivingActor(InCharacter);
	if (DrivingActor)
	{
		DrivingActor->StopAnimation();
	}

	// reset the bulk data
	InCharacter->SetFaceDNABuffer({}, /*bInHasFaceDNABlendshapes*/ false);
	InCharacter->MarkPackageDirty();

	// delete morph targets if there are any
	if (!CharacterData->FaceMesh->GetMorphTargets().IsEmpty())
	{
		CharacterData->FaceMesh->UnregisterAllMorphTarget();
	}

	// set the face mesh DNA back to the archetype
	UDNAAsset* FaceArchetypeDNA = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAseet(EMetaHumanImportDNAType::Face, GetTransientPackage());
	check(FaceArchetypeDNA);
	// Revert the map to match archetype DNA.
	TSharedPtr<IDNAReader> ArchetypeDNAReader = FaceArchetypeDNA->GetDnaReaderFromAsset();
	CharacterData->FaceDnaToSkelMeshMap = MakeShareable(USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh(ArchetypeDNAReader.Get(), CharacterData->FaceMesh));
	ApplyFaceDNA(InCharacter, ArchetypeDNAReader, ELodUpdateOption::All, /*bInResettingToArchetypeDNA*/ true);


	CharacterData->FaceMesh->PostEditChange();
	CharacterData->FaceMesh->MarkPackageDirty();

	// Update map since building the Skeletal Mesh can result in re-ordering of the render vertices
	CharacterData->FaceDnaToSkelMeshMap =  MakeShareable(USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh(ArchetypeDNAReader.Get(), CharacterData->FaceMesh));

	// set LOD to LOD0
	UpdateCharacterLOD(InCharacter, EMetaHumanCharacterLOD::LOD0);

	UE::MetaHuman::Analytics::RecordRemoveFaceRigEvent(InCharacter);
}

void UMetaHumanCharacterEditorSubsystem::RemoveBodyRig(TNotNull<UMetaHumanCharacter*> InCharacter)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	// stop any animation if we have any
	AMetaHumanInvisibleDrivingActor* DrivingActor = GetInvisibleDrivingActor(InCharacter);
	if (DrivingActor)
	{
		DrivingActor->StopAnimation();
	}

	// reset the bulk data
	InCharacter->SetBodyDNABuffer({});

	constexpr bool bImportingFromDNA = false;
	UpdateCharacterIsFixedBodyType(InCharacter, bImportingFromDNA);
	InCharacter->MarkPackageDirty();

	// delete morph targets if there are any
	if (!CharacterData->BodyMesh->GetMorphTargets().IsEmpty())
	{
		CharacterData->BodyMesh->UnregisterAllMorphTarget();
	}

	// set the body mesh DNA back to the archetype
	UDNAAsset* BodyArchetypeDNA = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAseet(EMetaHumanImportDNAType::Body, GetTransientPackage());
	check(BodyArchetypeDNA);
	// Revert the map to match archetype DNA.
	TSharedPtr<IDNAReader> ArchetypeDNAReader = BodyArchetypeDNA->GetDnaReaderFromAsset();
	CharacterData->BodyDnaToSkelMeshMap = MakeShareable(USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh(ArchetypeDNAReader.Get(), CharacterData->BodyMesh));
	constexpr bool bImportingAsFixedBodyType = false;
	ApplyBodyDNA(InCharacter, BodyArchetypeDNA->GetDnaReaderFromAsset(), bImportingAsFixedBodyType);
	
	CharacterData->BodyMesh->PostEditChange();
	CharacterData->BodyMesh->MarkPackageDirty();

	// Update map since building the Skeletal Mesh can result in re-ordering of the render vertices
	CharacterData->BodyDnaToSkelMeshMap = MakeShareable(USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh(ArchetypeDNAReader.Get(), CharacterData->BodyMesh));
	
	// set LOD to LOD0
	UpdateCharacterLOD(InCharacter, EMetaHumanCharacterLOD::LOD0);

	// Set body mesh to state
	const FMetaHumanRigEvaluatedState VerticesAndVertexNormals = CharacterData->BodyState->GetVerticesAndVertexNormals();
	UpdateBodyMeshInternal(CharacterData, VerticesAndVertexNormals, ELodUpdateOption::All, /*bInUpdateFromStateDna*/ true);
}

void UMetaHumanCharacterEditorSubsystem::AutoRigFace(TNotNull<UMetaHumanCharacter*> InCharacter, const UE::MetaHuman::ERigType InRigType)
{
	FMetaHumanCharacterAutoRiggingRequestParams Params;
	Params.bBlocking = false;
	Params.bReportProgress = true;
	
	if (InRigType == UE::MetaHuman::ERigType::JointsOnly)
	{
		Params.RigType = EMetaHumanRigType::JointsOnly;
	}
	else
	{
		Params.RigType = EMetaHumanRigType::JointsAndBlendShapes;
	}

	RequestAutoRigging(InCharacter, Params);
}

void UMetaHumanCharacterEditorSubsystem::RequestAutoRigging(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterAutoRiggingRequestParams& InParams)
{
	if (!IsInGameThread())
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "RequestAutoRigging must be called on the Game Thread");
		return;
	}

	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "RequestAutoRigging called with invalid character");
		return;
	}

	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "RequestAutoRigging called with {Character} that is not not added for editing", InCharacter->GetName());
		return;
	}

	if (IsAutoRiggingFace(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "RequestAutoRigging called for character {Character} that already has an active auto-rigging request", InCharacter->GetName());
		return;
	}

	UE_LOGFMT(LogMetaHumanCharacterEditor,
			  Display,
			  "Requesting Auto-Rigging for {Character}. Rig Type: {RigType}. Report Progress: {ReportProgress}. Blocking: {Blocking}",
			  InCharacter->GetName(),
			  UEnum::GetDisplayValueAsText(InParams.RigType).ToString(),
			  InParams.bReportProgress,
			  InParams.bBlocking);

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	// Prepare AutoRig parameters.
	TSharedPtr<IDNAReader> FaceDNAReader = UE::MetaHuman::GetDNAReader(CharacterData->FaceMesh);
	UE::MetaHuman::FTargetSolveParameters AutoRigParameters;
	const FMetaHumanCharacterIdentity::FState& FaceState = *CharacterData->FaceState;
	FMetaHumanCharacterEditorCloudRequests::InitFaceAutoRigParams(FaceState, FaceDNAReader.ToSharedRef(), AutoRigParameters);	

	if (InParams.RigType == EMetaHumanRigType::JointsAndBlendShapes)
	{
		AutoRigParameters.RigType = UE::MetaHuman::ERigType::JointsAndBlendshapes;
		AutoRigParameters.RigRefinementLevel = UE::MetaHuman::ERigRefinementLevel::Medium;
	}
	else if (InParams.RigType == EMetaHumanRigType::JointsOnly)
	{
		AutoRigParameters.RigType = UE::MetaHuman::ERigType::JointsOnly;
		AutoRigParameters.RigRefinementLevel = UE::MetaHuman::ERigRefinementLevel::None;
	}
	else
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor,
				  Error,
				  "Failed to map EMetaHumanRigType '{RigType}' when Auto-Rigging '{Character}'",
				  UEnum::GetDisplayValueAsText(InParams.RigType).ToString(),
				  InCharacter->GetName());
		return;
	}

	FMetaHumanCharacterEditorCloudRequests& CloudRequests = CharacterCloudRequests.FindOrAdd(InCharacter);

	CloudRequests.AutoRig = UE::MetaHuman::FAutoRigServiceRequest::CreateRequest(AutoRigParameters);
	CloudRequests.AutoRig->AutorigRequestCompleteDelegate.BindUObject(this, &UMetaHumanCharacterEditorSubsystem::OnAutoRigFaceRequestCompleted, TObjectKey<UMetaHumanCharacter>(InCharacter), AutoRigParameters.RigType);
	CloudRequests.AutoRig->OnMetaHumanServiceRequestFailedDelegate.BindUObject(this, &UMetaHumanCharacterEditorSubsystem::OnAutoRigFaceRequestFailed, TObjectKey<UMetaHumanCharacter>(InCharacter));

	if (InParams.bReportProgress)
	{
		CloudRequests.AutoRig->MetaHumanServiceRequestProgressDelegate.BindUObject(this, &UMetaHumanCharacterEditorSubsystem::OnAutoRigFaceProgressUpdated, TObjectKey<UMetaHumanCharacter>(InCharacter));
		CloudRequests.AutoRiggingProgressHandle = FSlateNotificationManager::Get().StartProgressNotification(LOCTEXT("AutoRiggingProgress", "Auto-Rigging"), 100);

		CloudRequests.AutoRiggingNotificationItem = UE::MetaHuman::ShowNotification(LOCTEXT("StartAutoRiggingMessage", "Auto-Rigging Face Mesh"), SNotificationItem::ECompletionState::CS_Pending);
	}

	// Fire the request.
	CloudRequests.AutoRig->RequestSolveAsync();
	CloudRequests.AutoRiggingStartTime = FPlatformTime::Seconds();

	InCharacter->NotifyRiggingStateChanged();

	UE::MetaHuman::Analytics::RecordRequestAutorigEvent(InCharacter, AutoRigParameters.RigType);

	if (InParams.bBlocking)
	{
		UE::MetaHuman::WaitForCloudRequests([this, InCharacter]
											{
												return IsAutoRiggingFace(InCharacter);
											});
	}
}

bool UMetaHumanCharacterEditorSubsystem::IsAutoRiggingFace(TNotNull<const UMetaHumanCharacter*> InCharacter) const
{
	check(IsInGameThread());

	if (const FMetaHumanCharacterEditorCloudRequests* Requests = CharacterCloudRequests.Find(InCharacter))
	{
		return Requests->AutoRig.IsValid();
	}

	return false;
}

void UMetaHumanCharacterEditorSubsystem::OnAutoRigFaceRequestCompleted(const UE::MetaHuman::FAutorigResponse& InResponse, TObjectKey<UMetaHumanCharacter> InCharacterKey, const UE::MetaHuman::ERigType InRigType)
{
	check(IsInGameThread());
	// The cloud request should exist, even if the character is no longer valid
	check(CharacterCloudRequests.Contains(InCharacterKey));

	using namespace UE::MetaHuman;

	FMetaHumanCharacterEditorCloudRequests& CloudRequests = CharacterCloudRequests[InCharacterKey];

	ON_SCOPE_EXIT
	{
		CloudRequests.AutoRiggingRequestFinished();

		if (!CloudRequests.HasActiveRequest())
		{
			CharacterCloudRequests.Remove(InCharacterKey);
		}
	};

	UMetaHumanCharacter* Character = InCharacterKey.ResolveObjectPtr();
	if (!Character || !IsObjectAddedForEditing(Character))
	{
		// Character has been forcibly deleted or the asset editor was closed
		return;
	}

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacterKey];

	const double ElapsedTime = FPlatformTime::Seconds() - CloudRequests.AutoRiggingStartTime;

	bool bDnaApplied = false;

	if (InResponse.IsValid())
	{
		FScopedSlowTask ApplyDNATask{ 0.0f, LOCTEXT("ApplyDNATask", "Applying DNA from Auto-Rigging service") };
		ApplyDNATask.MakeDialog();

		TArray<uint8> DNABuffer = Character->GetFaceDNABuffer();
		TSharedRef<FMetaHumanCharacterIdentity::FState> OriginalFaceState = CopyFaceState(Character);


		TSharedPtr<IDNAReader> OutDna = ApplyFaceDNA(Character, InResponse.Dna, ELodUpdateOption::All);

		if (OutDna.IsValid())
		{
			TArray<uint8> BodyDNABuffer = Character->GetBodyDNABuffer();
			TSharedRef<FMetaHumanCharacterBodyIdentity::FState> OriginalBodyState = CopyBodyState(Character);
			
			// Get body dna
			TSharedPtr<IDNAReader> BodyDna;
			if (!Character->bFixedBodyType)
			{
				if (UDNAAsset* BodyDNAAsset = CharacterData->BodyMesh->GetAssetUserData<UDNAAsset>())
				{
					BodyDna = CharacterData->BodyState->StateToDna(BodyDNAAsset);
				}		
			}
			
			// Scope for the undo transactions
			{
				FScopedTransaction Transaction(AutoriggingTransactionContext, LOCTEXT("CharacterAutorigSuccessTransaction", "Apply Auto-rig"), Character);
				Character->Modify();

				// Store the DNA into the character asset
				if (BodyDna)
				{
					Character->SetBodyDNABuffer(ReadStreamFromDNA(BodyDna.Get(), EDNADataLayer::All));	
				}
				Character->SetFaceDNABuffer(ReadStreamFromDNA(OutDna.Get(), EDNADataLayer::All), OutDna->GetBlendShapeChannelCount() > 0);
				Character->MarkPackageDirty();


				TUniquePtr<FAutoRigCommandChange> Change = MakeUnique<FAutoRigCommandChange>(
					DNABuffer,
					OriginalFaceState,
					BodyDNABuffer,
					OriginalBodyState,
					Character);

				if (GUndo != nullptr)
				{
					GUndo->StoreUndo(Character, MoveTemp(Change));
				}
			}

			RunCharacterEditorPipelineForPreview(Character);

			bDnaApplied = true;
		}
		else
		{
			UE::MetaHuman::ShowNotification(LOCTEXT("AutoRiggingCheckDNAFailed", "Auto-Rigging failed due to incompatible DNA"), SNotificationItem::CS_Fail);
		}

		if (CVarMHCharacterSaveAutoRiggedDNA.GetValueOnAnyThread())
		{
			TSharedPtr<IDNAReader> BodyDna;
			USkeletalMesh* BodySkeletalMesh = CharacterData->BodyMesh;
			if (UDNAAsset* BodyDNA = BodySkeletalMesh->GetAssetUserData<UDNAAsset>())
			{
				BodyDna = BodyDNA->GetGeometryReader();
			}

			WriteDNAToFile(InResponse.Dna.Get(), EDNADataLayer::All, *(FPaths::ProjectSavedDir() + FString(TEXT("MetaHumanCharacterAutorig.dna"))));
			WriteDNAToFile(BodyDna.Get(), EDNADataLayer::All, *(FPaths::ProjectSavedDir() + FString(TEXT("MetaHumanCharacterBody.dna"))));
			WriteDNAToFile(OutDna.Get(), EDNADataLayer::All, *(FPaths::ProjectSavedDir() + FString(TEXT("MetaHumanCharacterAutorigApplied.dna"))));
		}
	}
	else
	{
		UE::MetaHuman::ShowNotification(LOCTEXT("AutoRiggingInvalidResponse", "Auto-Rigging failed due to invalid response from the service"), SNotificationItem::CS_Fail);
	}

	if (bDnaApplied)
	{
		const FText Message = FText::Format(LOCTEXT("AutoRiggingRequestCompleted", "Auto-Rigging finished in {0} seconds"), ElapsedTime);
		UE::MetaHuman::ShowNotification(Message, SNotificationItem::ECompletionState::CS_Success);
	}

	if (TSharedPtr<SNotificationItem> AutoRiggingNotificationItem = CloudRequests.AutoRiggingNotificationItem.Pin())
	{
		AutoRiggingNotificationItem->SetCompletionState(bDnaApplied ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		AutoRiggingNotificationItem->ExpireAndFadeout();
	}

	Character->NotifyRiggingStateChanged();
}

void UMetaHumanCharacterEditorSubsystem::OnAutoRigFaceProgressUpdated(float InPercentage, TObjectKey<UMetaHumanCharacter> InCharacterKey)
{
	// The cloud request should exist, even if the character is no longer valid
	check(CharacterCloudRequests.Contains(InCharacterKey));
	check(IsInGameThread());

	FMetaHumanCharacterEditorCloudRequests& CloudRequests = CharacterCloudRequests[InCharacterKey];
	FSlateNotificationManager::Get().UpdateProgressNotification(CloudRequests.AutoRiggingProgressHandle,
																100.0f * InPercentage);
}

void UMetaHumanCharacterEditorSubsystem::OnAutoRigFaceRequestFailed(EMetaHumanServiceRequestResult InResult, TObjectKey<UMetaHumanCharacter> InCharacterKey)
{
	// The cloud request should exist, even if the character is no longer valid
	check(CharacterCloudRequests.Contains(InCharacterKey));
	check(IsInGameThread());

	FMetaHumanCharacterEditorCloudRequests& CloudRequests = CharacterCloudRequests[InCharacterKey];

	// Close the notifications
	FSlateNotificationManager::Get().CancelProgressNotification(CloudRequests.AutoRiggingProgressHandle);
	if (TSharedPtr<SNotificationItem> AutoRiggingNotificationItem = CloudRequests.AutoRiggingNotificationItem.Pin())
	{
		AutoRiggingNotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
		AutoRiggingNotificationItem->ExpireAndFadeout();
	}

	const FText Message = FText::Format(LOCTEXT("AutoRigFailedMessage", "Auto-Rigging of Face failed with code '{0}'"), UEnum::GetDisplayValueAsText(InResult));
	UE::MetaHuman::ShowNotification(Message, SNotificationItem::ECompletionState::CS_Fail);

	CloudRequests.AutoRiggingRequestFinished();

	if (!CloudRequests.HasActiveRequest())
	{
		CharacterCloudRequests.Remove(InCharacterKey);
	}

	UMetaHumanCharacter* Character = InCharacterKey.ResolveObjectPtr();
	if (!Character || !IsObjectAddedForEditing(Character))
	{
		// Character has been forcibly deleted or the asset editor was closed
		return;
	}

	Character->NotifyRiggingStateChanged();
}

void UMetaHumanCharacterEditorSubsystem::ApplyEyelashesAndTeethPropertiesToFaceState(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, const FMetaHumanCharacterEyelashesProperties& InEyelashesProperties, const FMetaHumanCharacterTeethProperties& InTeethProperties, bool bInUpdateEyelashes, bool bInUpdateTeeth, ELodUpdateOption InUpdateOption) const
{
	if (bInUpdateEyelashes)
	{
		UpdateEyelashesVariantFromProperties(InCharacterData->FaceState, InEyelashesProperties);
	}

	if (bInUpdateTeeth)
	{
		// set the expression activations (add this also as a method of the TeethProperties)
		TMap<FString, float> ExpressionActivations;
	#if WITH_EDITOR // if in the editor tool, we only want to enable the show teeth expression when the tool is enabled
		if (InTeethProperties.EnableShowTeethExpression)
	#endif
		{
			ExpressionActivations.Add("jaw_open", InTeethProperties.JawOpen);
			ExpressionActivations.Add("McornerPull_Mstretch_MupperLipRaise_MlowerLipDepress_tgt", 1.0f);
		}
	#if WITH_EDITOR
		else

		{
			ExpressionActivations.Add("jaw_open", 0.0f);
			ExpressionActivations.Add("McornerPull_Mstretch_MupperLipRaise_MlowerLipDepress_tgt", 0.0f);
		}
	#endif
		InCharacterData->FaceState->SetExpressionActivations(ExpressionActivations);
		if (!InCharacterData->HeadModelSettings.IsSet() || InTeethProperties.IsVariantUpdated(InCharacterData->HeadModelSettings.GetValue().Teeth))
		{
			UpdateTeethVariantFromProperties(InCharacterData->FaceState, InTeethProperties);
		}
	}
	
	// Update the actor face skel mesh to apply eyelashes and/or teeth geometry.
	const FMetaHumanRigEvaluatedState FaceVerticesAndVertexNormals = InCharacterData->FaceState->Evaluate();
	UpdateFaceMeshInternal(InCharacterData, FaceVerticesAndVertexNormals, InUpdateOption);
}


void UMetaHumanCharacterEditorSubsystem::UpdateEyelashesVariantFromProperties(TSharedRef<FMetaHumanCharacterIdentity::FState> InOutFaceState, const FMetaHumanCharacterEyelashesProperties& InEyelashesProperties) const
{
	// Set the Eyelashes variant to the actor face state
	
	TArray<float> EyelashesVariantsWeights = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	int32 EyelashesIndex = UE::MetaHuman::GetEyelashesVariantIndex(*InOutFaceState, InEyelashesProperties.Type);
	if (EyelashesIndex > INDEX_NONE)
	{
		EyelashesVariantsWeights[EyelashesIndex] = 1.0f;
	}
	InOutFaceState->SetVariant("eyelashes", EyelashesVariantsWeights);
}

void UMetaHumanCharacterEditorSubsystem::UpdateTeethVariantFromProperties(TSharedRef<FMetaHumanCharacterIdentity::FState> InOutFaceState, const FMetaHumanCharacterTeethProperties& InTeethProperties, bool bInUseExpressions) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::UpdateTeethVariantFromProperties");

	// set the variant weights
	TArray<float> TeethVariantsWeights;
	TeethVariantsWeights.SetNumZeroed(InOutFaceState->GetVariantsCount("teeth"));

	const int32 VariationsStartIndex = 15;
	const int32 TeethCharacterCount = 8;

	// We need to include the default (archetype) character, since it has valid teeth data.
	float Value = InTeethProperties.Variation * TeethCharacterCount;
	float MinValue = FMath::Floor(Value);
	float MaxValue = MinValue + 1.0f;

	float HighValue = FMath::GetRangePct(MinValue, MaxValue, Value);
	float LowValue = 1.0f - HighValue;

	// Note that HigherIndex will be out of array bounds when Variation = 1.0 and
	// LowerIndex won't be teeth variation when Variation = 0.0, so don't use them
	// for array access.
	int32 LowerIndex = (int32)MinValue + VariationsStartIndex - 1;
	int32 HigherIndex = LowerIndex + 1;

	for (int32 CharIndex = VariationsStartIndex; CharIndex < VariationsStartIndex + TeethCharacterCount; CharIndex++)
	{
		if (CharIndex == LowerIndex && !FMath::IsNearlyZero(LowValue))
		{
			TeethVariantsWeights[CharIndex] = LowValue;
		}
		else if (CharIndex == HigherIndex && !FMath::IsNearlyZero(HighValue))
		{
			TeethVariantsWeights[CharIndex] = HighValue;
		}
		else
		{
			TeethVariantsWeights[CharIndex] = 0.0f;
		}
	}

	// update the properties
	const int32 ShortOption = 0;
	const int32 LongOption = 1;
	const int32 CrowdedOption = 2;
	const int32 SpacedOption = 3;
	const int32 UpperShiftRightOption = 4;
	const int32 UpperShiftLeftOption = 5;
	const int32 LowerShiftRightOption = 6;
	const int32 LowerShiftLeftOption = 7;
	const int32 UnderbiteOption = 8;
	const int32 OverbiteOption = 9;
	const int32 OverjetOption = 10;
	const int32 WornDownOption = 11;
	const int32 PolycanineOption = 12;
	const int32 RecedingGumsOption = 13;
	const int32 NarrowOption = 14;
	TeethVariantsWeights[ShortOption] = FMath::Clamp(-InTeethProperties.ToothLength, 0.0f, 1.0f);
	TeethVariantsWeights[LongOption] = FMath::Clamp(InTeethProperties.ToothLength, 0.0f, 1.0f);
	TeethVariantsWeights[CrowdedOption] = FMath::Clamp(-InTeethProperties.ToothSpacing, 0.0f, 1.0f);
	TeethVariantsWeights[SpacedOption] = FMath::Clamp(InTeethProperties.ToothSpacing, 0.0f, 1.0f);
	TeethVariantsWeights[UpperShiftRightOption] = FMath::Clamp(-InTeethProperties.UpperShift, 0.0f, 1.0f);
	TeethVariantsWeights[UpperShiftLeftOption] = FMath::Clamp(InTeethProperties.UpperShift, 0.0f, 1.0f);
	TeethVariantsWeights[LowerShiftRightOption] = FMath::Clamp(-InTeethProperties.LowerShift, 0.0f, 1.0f);
	TeethVariantsWeights[LowerShiftLeftOption] = FMath::Clamp(InTeethProperties.LowerShift, 0.0f, 1.0f);
	TeethVariantsWeights[UnderbiteOption] = FMath::Clamp(-InTeethProperties.Overbite, 0.0f, 1.0f);
	TeethVariantsWeights[OverbiteOption] = FMath::Clamp(InTeethProperties.Overbite, 0.0f, 1.0f);
	TeethVariantsWeights[OverjetOption] = InTeethProperties.Overjet;
	TeethVariantsWeights[WornDownOption] = InTeethProperties.WornDown;
	TeethVariantsWeights[PolycanineOption] = InTeethProperties.Polycanine;
	TeethVariantsWeights[RecedingGumsOption] = InTeethProperties.RecedingGums;
	TeethVariantsWeights[NarrowOption] = InTeethProperties.Narrowness;

	InOutFaceState->SetVariant("teeth", TeethVariantsWeights);

}


void UMetaHumanCharacterEditorSubsystem::ApplySkinPropertiesToFaceState(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, const FMetaHumanCharacterSkinProperties& InSkinProperties) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::ApplySkinPropertiesToFaceState");

	UpdateHFVariantFromSkinProperties(InCharacterData->FaceState, InSkinProperties);

	// Update the actor face skel mesh
	const FMetaHumanRigEvaluatedState FaceVerticesAndVertexNormals = InCharacterData->FaceState->Evaluate();
	UpdateFaceMeshInternal(InCharacterData, FaceVerticesAndVertexNormals, GetUpdateOptionForEditing());
}

void UMetaHumanCharacterEditorSubsystem::UpdateHFVariantFromSkinProperties(TSharedRef<FMetaHumanCharacterIdentity::FState> InOutFaceState, const FMetaHumanCharacterSkinProperties& InSkinProperties) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::UpdateHFVariantFromSkinProperties");

	const int32 HighFrequencyIndex = FMath::Clamp(InSkinProperties.FaceTextureIndex, 0, FaceTextureSynthesizer.GetMaxHighFrequencyIndex() - 1);

	// Set the HF variant to the actor face state
	InOutFaceState->SetHighFrequencyVariant(UE::MetaHuman::MapTextureHFToStateHFIndex(*InOutFaceState, HighFrequencyIndex));
}

TSharedRef<const FDNAToSkelMeshMap> UMetaHumanCharacterEditorSubsystem::GetFaceDnaToSkelMeshMap(TNotNull<const UMetaHumanCharacter*> InCharacter) const
{
	TSharedRef<const FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	return CharacterData->FaceDnaToSkelMeshMap;
}

FSimpleMulticastDelegate& UMetaHumanCharacterEditorSubsystem::OnFaceStateChanged(TNotNull<const UMetaHumanCharacter*> InCharacter)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	return CharacterData->OnFaceStateChangedDelegate;
}

FLinearColor UMetaHumanCharacterEditorSubsystem::GetSkinTone(const FVector2f& UV) const
{
	FLinearColor SkinToneColor = FLinearColor::Black;
	if (ensureMsgf(IsTextureSynthesisEnabled(), TEXT("Face Texture Synthesizer not initialized prior to call to UMetaHumanCharacterEditorSubsystem::GetSkinTone")))
	{
		SkinToneColor = FaceTextureSynthesizer.GetSkinTone(UV);
	}
	return SkinToneColor;
}

TWeakObjectPtr<UTexture2D> UMetaHumanCharacterEditorSubsystem::GetOrCreateSkinToneTexture()
{
	if (!SkinToneTexture.IsValid())
	{
		const int32 TextureSize = 256;

		TArray<FColor> SkinToneColorData;
		SkinToneColorData.Reserve(TextureSize * TextureSize);

		const FVector2f SkinToneTextureSize(TextureSize, TextureSize);

		// Generates the texture with the skin tones the user can use to pick up
		for (int32 Y = 0; Y < TextureSize; ++Y)
		{
			for (int32 X = 0; X < TextureSize; ++X)
			{				
				// The skin tone color is already in sRGB so don't perform the conversion here
				const bool bSRGB = false;
				const FVector2f UV = FVector2f(X, Y) / SkinToneTextureSize;
				SkinToneColorData.Add(FaceTextureSynthesizer.GetSkinTone(UV).ToFColor(bSRGB));
			}
		}

		const FString TextureName = MakeUniqueObjectName(this, UTexture2D::StaticClass(), TEXT("SkinToneTexture")).ToString();
		FCreateTexture2DParameters CreateTextureParams;
		CreateTextureParams.bSRGB = true;
		SkinToneTexture = FImageUtils::CreateTexture2D(TextureSize, TextureSize, SkinToneColorData, this, TextureName, RF_NoFlags, CreateTextureParams);
	}

	return SkinToneTexture.Get();
}

FVector2f UMetaHumanCharacterEditorSubsystem::EstimateSkinTone(const FLinearColor& InSkinTone, const int HFIndex) const
{
	return FaceTextureSynthesizer.ProjectSkinTone(InSkinTone, HFIndex);
}

int32 UMetaHumanCharacterEditorSubsystem::GetMaxHighFrequencyIndex() const
{
	return FaceTextureSynthesizer.GetMaxHighFrequencyIndex();
}

void UMetaHumanCharacterEditorSubsystem::UpdateCharacterPreviewMaterial(TNotNull<UMetaHumanCharacter*> InCharacter, EMetaHumanCharacterSkinPreviewMaterial InPreviewMaterial) const
{
	InCharacter->PreviewMaterialType = InPreviewMaterial;
	InCharacter->MarkPackageDirty();

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();

	const bool bUseFaceMaterialsWithVTSupport = Settings->ShouldUseVirtualTextures() && InCharacter->HasHighResolutionTextures();
	UpdateActorsSkinPreviewMaterial(CharacterData, InPreviewMaterial, bUseFaceMaterialsWithVTSupport);

	// When switching to editable, ensure that all textures are updated for the material
	// Also consider the clay material to apply the normal maps in the material
	if (InPreviewMaterial == EMetaHumanCharacterSkinPreviewMaterial::Editable ||
		InPreviewMaterial == EMetaHumanCharacterSkinPreviewMaterial::Clay)
	{
		const FMetaHumanCharacterSkinTextureSet FinalSkinTextureSet =
			InCharacter->SkinSettings.GetFinalSkinTextureSet(FMetaHumanCharacterSkinTextureSet
				{
					.Face = InCharacter->GetValidFaceTextures(),
					.Body = InCharacter->BodyTextures
				});

		const FMetaHumanCharacterSkinSettings& SkinSettings = CharacterData->SkinSettings.IsSet() ? CharacterData->SkinSettings.GetValue() : InCharacter->SkinSettings;
		UpdateSkinTextures(CharacterData, SkinSettings.Skin, FinalSkinTextureSet);

		const FMetaHumanCharacterMakeupSettings& MakeupSettings = CharacterData->MakeupSettings.IsSet() ? CharacterData->MakeupSettings.GetValue() : InCharacter->MakeupSettings;
		ApplyMakeupSettings(CharacterData, MakeupSettings, bUseFaceMaterialsWithVTSupport);

		const FMetaHumanCharacterEyesSettings& EyesSettings = CharacterData->EyesSettings.IsSet() ? CharacterData->EyesSettings.GetValue() : InCharacter->EyesSettings;
		ApplyEyesSettings(CharacterData, EyesSettings);

		FMetaHumanCharacterSkinMaterials::ApplySkinParametersToMaterials(CharacterData->HeadMaterials, CharacterData->BodyMaterial, SkinSettings);
		FMetaHumanCharacterSkinMaterials::ApplyEyelashesPropertiesToMaterial(CharacterData->HeadMaterials, InCharacter->HeadModelSettings.Eyelashes);
		FMetaHumanCharacterSkinMaterials::ApplyTeethPropertiesToMaterial(CharacterData->HeadMaterials, InCharacter->HeadModelSettings.Teeth);
	}

	UpdateCharacterPreviewMaterialHiddenFacesMask(InCharacter);
}

void UMetaHumanCharacterEditorSubsystem::StoreSynthesizedTextures(TNotNull<UMetaHumanCharacter*> InCharacter) const
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	if (!InCharacter->HasHighResolutionTextures())
	{
		// Store the textures as compressed data in the character asset
		for (EFaceTextureType TextureType : TEnumRange<EFaceTextureType>())
		{
			if (CharacterData->CachedSynthesizedImages.Contains(TextureType))
			{
				InCharacter->StoreSynthesizedFaceTexture(TextureType, CharacterData->CachedSynthesizedImages[TextureType]);
			}
		}
	}
}

void UMetaHumanCharacterEditorSubsystem::UpdateActorsSkinPreviewMaterial(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, EMetaHumanCharacterSkinPreviewMaterial InPreviewMaterialType, bool bInUseFaceMaterialsWithVTSupport)
{
	FMetaHumanCharacterFaceMaterialSet HeadMaterialSet = FMetaHumanCharacterSkinMaterials::GetHeadPreviewMaterialInstance(InPreviewMaterialType, bInUseFaceMaterialsWithVTSupport);

	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	const bool bUseBodyMaterialsWithVTSupport = Settings->ShouldUseVirtualTextures();

	UMaterialInstanceDynamic* BodyPreviewMaterialInstance = FMetaHumanCharacterSkinMaterials::GetBodyPreviewMaterialInstance(InPreviewMaterialType, bUseBodyMaterialsWithVTSupport);
	check(BodyPreviewMaterialInstance);

	if (InPreviewMaterialType == EMetaHumanCharacterSkinPreviewMaterial::Clay)
	{
		// The Clay material is just a parameter of the full editable material, so just enable it here
		HeadMaterialSet.ForEachSkinMaterial<UMaterialInstanceDynamic>(
			[](EMetaHumanCharacterSkinMaterialSlot, UMaterialInstanceDynamic* Material)
			{
				Material->SetScalarParameterValue(TEXT("ClayMaterial"), 1.0f);
			}
		);

		BodyPreviewMaterialInstance->SetScalarParameterValue(TEXT("ClayMaterial"), 1.0f);
	}

	InCharacterData->HeadMaterials = HeadMaterialSet;
	InCharacterData->BodyMaterial = BodyPreviewMaterialInstance;

	FMetaHumanCharacterSkinMaterials::SetHeadMaterialsOnMesh(InCharacterData->HeadMaterials, InCharacterData->FaceMesh);
	FMetaHumanCharacterSkinMaterials::SetBodyMaterialOnMesh(InCharacterData->BodyMaterial, InCharacterData->BodyMesh);

	ForEachCharacterActor(InCharacterData, [](TScriptInterface<IMetaHumanCharacterEditorActorInterface> Actor)
		{
			Actor->OnFaceMeshUpdated();
			Actor->OnBodyMeshUpdated();
		});
}

void UMetaHumanCharacterEditorSubsystem::CommitSkinSettings(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterSkinSettings& InSkinSettings)
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CommitSkinSettings called with invalid character");
		return;
	}

	if (!IsObjectAddedForEditing(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CommitSkinSettings called with {Character} that is not not added for editing", InCharacter->GetName());
		return;
	}

	InCharacter->SkinSettings = InSkinSettings;

	FMetaHumanCharacterBodyTextureUtils::UpdateBodySkinBiasGain(FaceTextureSynthesizer, InCharacter->SkinSettings.Skin);
	InCharacter->MarkPackageDirty();

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	ApplySkinSettings(InCharacter, InCharacter->SkinSettings);
	StoreSynthesizedTextures(InCharacter);
}

void UMetaHumanCharacterEditorSubsystem::RequestHighResolutionTextures(TNotNull<UMetaHumanCharacter*> InCharacter, ERequestTextureResolution InResolution)
{
	// Backup the desired resolutions from the character and restore it after requesting.
	// This is to keeps the deprecated function working as is
	const FMetaHumanCharacterTextureSourceResolutions CurrentResolutions = InCharacter->SkinSettings.DesiredTextureSourcesResolutions;

	// Set all desired resolutions to be the same
	InCharacter->SkinSettings.DesiredTextureSourcesResolutions.SetAllResolutionsTo(InResolution);

	RequestTextureSources(InCharacter,
						  FMetaHumanCharacterTextureRequestParams
						  {
						   .bReportProgress = true,
						   .bBlocking = false,
						  });

	// Restore the resolutions so the character is left unchanged
	InCharacter->SkinSettings.DesiredTextureSourcesResolutions = CurrentResolutions;
}

void UMetaHumanCharacterEditorSubsystem::RequestTextureSources(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterTextureRequestParams& InParams)
{
	if (!IsInGameThread())
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "RequestTextureSources must be called on the Game Thread");
		return;
	}

	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "RequestTextureSources called with invalid character");
		return;
	}

	if (!IsObjectAddedForEditing(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "RequestTextureSources called with {Character} that is not added for editing", InCharacter->GetName());
		return;
	}

	if (IsRequestingHighResolutionTextures(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "RequestTextureSources called for character {Character} that already has an active request", InCharacter->GetName());
		return;
	}

	const FMetaHumanCharacterTextureSourceResolutions& Resolutions = InCharacter->SkinSettings.DesiredTextureSourcesResolutions;

	UE_LOGFMT(LogMetaHumanCharacterEditor,
			  Display,
			  "Requesting texture sources for {Character}."
			  " Face: Albedo {FaceAlbedo}, Normal {FaceNormal}, Cavity {FaceCavity}, Anim. Maps: {FaceAnimMaps}"
			  " Body: Albedo {BodyAlbedo}, Normal {BodyNormal}, Cavity {BodyCavity}, Masks: {BodyMasks}"
			  " Report Progress : {ReportProgress} Blocking: {Blocking}",
			  InCharacter->GetName(),
			  UEnum::GetDisplayValueAsText(Resolutions.FaceAlbedo).ToString(),
			  UEnum::GetDisplayValueAsText(Resolutions.FaceNormal).ToString(),
			  UEnum::GetDisplayValueAsText(Resolutions.FaceCavity).ToString(),
			  UEnum::GetDisplayValueAsText(Resolutions.FaceAnimatedMaps).ToString(),
			  UEnum::GetDisplayValueAsText(Resolutions.BodyAlbedo).ToString(),
			  UEnum::GetDisplayValueAsText(Resolutions.BodyNormal).ToString(),
			  UEnum::GetDisplayValueAsText(Resolutions.BodyCavity).ToString(),
			  UEnum::GetDisplayValueAsText(Resolutions.BodyMasks).ToString(),
			  InParams.bReportProgress,
			  InParams.bBlocking);

	const FMetaHumanCharacterSkinProperties& SkinProperties = InCharacter->SkinSettings.Skin;

	FMetaHumanCharacterEditorCloudRequests& CloudRequests = CharacterCloudRequests.FindOrAdd(InCharacter);

	// Set up face request
	const FMetaHumanFaceTextureSynthesizer::FTextureSynthesisParams TextureSynthesisParams = FMetaHumanCharacterTextureSynthesis::SkinPropertiesToSynthesizerParams(SkinProperties, FaceTextureSynthesizer);
	UE::MetaHuman::FFaceTextureRequestCreateParams FaceTextureRequestCreateParams;
	FaceTextureRequestCreateParams.HighFrequency = TextureSynthesisParams.HighFrequencyIndex;
	CloudRequests.TextureSynthesis = UE::MetaHuman::FFaceTextureSynthesisServiceRequest::CreateRequest(FaceTextureRequestCreateParams);

	CloudRequests.TextureSynthesis->FaceTextureSynthesisRequestCompleteDelegate.BindUObject(this, &UMetaHumanCharacterEditorSubsystem::OnHighResolutionTexturesRequestCompleted, TObjectKey<UMetaHumanCharacter>(InCharacter));
	CloudRequests.TextureSynthesis->OnMetaHumanServiceRequestFailedDelegate.BindUObject(this, &UMetaHumanCharacterEditorSubsystem::OnHighResolutionTexturesRequestFailed, TObjectKey<UMetaHumanCharacter>(InCharacter));

	if (InParams.bReportProgress)
	{
		CloudRequests.TextureSynthesis->MetaHumanServiceRequestProgressDelegate.BindUObject(this, &UMetaHumanCharacterEditorSubsystem::OnHighResolutionTexturesProgressUpdated, TObjectKey<UMetaHumanCharacter>(InCharacter));
	}

	// The request completion delegates will be called when *all* textures are downloaded
	const TArray<UE::MetaHuman::FFaceTextureRequestParams, TInlineAllocator<static_cast<int32>(EFaceTextureType::Count)>> FaceTextureTypesToRequest =
	{
		{ EFaceTextureType::Basecolor, (int32) Resolutions.FaceAlbedo },
		{ EFaceTextureType::Basecolor_Animated_CM1, (int32) Resolutions.FaceAnimatedMaps },
		{ EFaceTextureType::Basecolor_Animated_CM2, (int32) Resolutions.FaceAnimatedMaps },
		{ EFaceTextureType::Basecolor_Animated_CM3, (int32) Resolutions.FaceAnimatedMaps },
		{ EFaceTextureType::Normal, (int32) Resolutions.FaceNormal },
		{ EFaceTextureType::Normal_Animated_WM1, (int32) Resolutions.FaceAnimatedMaps },
		{ EFaceTextureType::Normal_Animated_WM2, (int32) Resolutions.FaceAnimatedMaps },
		{ EFaceTextureType::Normal_Animated_WM3, (int32) Resolutions.FaceAnimatedMaps },
		{ EFaceTextureType::Cavity, (int32) Resolutions.FaceCavity },
	};

	const int32 NumSteps = 100;

	if (InParams.bReportProgress)
	{
		CloudRequests.TextureSynthesisProgressHandle = FSlateNotificationManager::Get().StartProgressNotification(LOCTEXT("DownloadSourceFaceTextures", "Downloading source face textures"), NumSteps);
		CloudRequests.TextureSynthesisNotificationItem = UE::MetaHuman::ShowNotification(LOCTEXT("StartSourceFaceTexturesDownload", "Downloading source face textures"), SNotificationItem::ECompletionState::CS_Pending);
	}

	// Set up body request
	UE::MetaHuman::FBodyTextureRequestCreateParams BodyTextureRequestCreateParams;
	BodyTextureRequestCreateParams.Tone = FMetaHumanCharacterBodyTextureUtils::GetSkinToneIndex(SkinProperties);
	BodyTextureRequestCreateParams.SurfaceMap = FMetaHumanCharacterBodyTextureUtils::GetBodySurfaceMapId(SkinProperties);
	CloudRequests.BodyTextures = UE::MetaHuman::FBodyTextureSynthesisServiceRequest::CreateRequest(BodyTextureRequestCreateParams);

	CloudRequests.BodyTextures->BodyTextureSynthesisRequestCompleteDelegate.BindUObject(this, &UMetaHumanCharacterEditorSubsystem::OnHighResolutionBodyTexturesRequestCompleted, TObjectKey<UMetaHumanCharacter>(InCharacter));
	CloudRequests.BodyTextures->OnMetaHumanServiceRequestFailedDelegate.BindUObject(this, &UMetaHumanCharacterEditorSubsystem::OnHighResolutionBodyTexturesRequestFailed, TObjectKey<UMetaHumanCharacter>(InCharacter));

	if (InParams.bReportProgress)
	{
		CloudRequests.BodyTextures->MetaHumanServiceRequestProgressDelegate.BindUObject(this, &UMetaHumanCharacterEditorSubsystem::OnHighResolutionBodyTexturesProgressUpdated, TObjectKey<UMetaHumanCharacter>(InCharacter));
	}
	
	// The request completion delegates will be called when *all* textures are downloaded
	const TArray<UE::MetaHuman::FBodyTextureRequestParams, TInlineAllocator<static_cast<int32>(EBodyTextureType::Count)>> BodyTextureTypesToRequest =
	{
		{ EBodyTextureType::Body_Basecolor, (int32) Resolutions.BodyAlbedo },
		{ EBodyTextureType::Body_Normal, (int32) Resolutions.BodyNormal },
		{ EBodyTextureType::Body_Cavity, (int32) Resolutions.BodyCavity },
		{ EBodyTextureType::Body_Underwear_Basecolor, (int32) Resolutions.BodyAlbedo },
		{ EBodyTextureType::Body_Underwear_Normal, (int32) Resolutions.BodyNormal },
		{ EBodyTextureType::Body_Underwear_Mask, (int32) Resolutions.BodyMasks },
		{ EBodyTextureType::Chest_Basecolor, (int32) Resolutions.BodyAlbedo },
		{ EBodyTextureType::Chest_Normal, (int32) Resolutions.BodyNormal },
		{ EBodyTextureType::Chest_Cavity, (int32) Resolutions.BodyCavity },
		{ EBodyTextureType::Chest_Underwear_Basecolor, (int32) Resolutions.BodyAlbedo },
		{ EBodyTextureType::Chest_Underwear_Normal, (int32) Resolutions.BodyNormal },
	};

	if (InParams.bReportProgress)
	{
		CloudRequests.BodyTextureProgressHandle = FSlateNotificationManager::Get().StartProgressNotification(LOCTEXT("DownloadSourceBodyTextures", "Downloading source body textures"), NumSteps);
		CloudRequests.BodyTextureNotificationItem = UE::MetaHuman::ShowNotification(LOCTEXT("StartSourceBodyTexturesDownload", "Downloading source body textures"), SNotificationItem::ECompletionState::CS_Pending);
	}

	OnDownloadingTexturesStateChanged.Broadcast(InCharacter);
	CloudRequests.TextureSynthesisStartTime = FPlatformTime::Seconds();
	CloudRequests.TextureSynthesis->RequestTexturesAsync(MakeConstArrayView<UE::MetaHuman::FFaceTextureRequestParams>(FaceTextureTypesToRequest.GetData(), FaceTextureTypesToRequest.Num()));

	CloudRequests.BodyTextureStartTime = FPlatformTime::Seconds();
	CloudRequests.BodyTextures->RequestTexturesAsync(MakeConstArrayView<UE::MetaHuman::FBodyTextureRequestParams>(BodyTextureTypesToRequest.GetData(), BodyTextureTypesToRequest.Num()));

	// TODO: Upgrade analytics to support granular resolution requests
	UE::MetaHuman::Analytics::RecordRequestHighResolutionTexturesEvent(InCharacter, InCharacter->SkinSettings.DesiredTextureSourcesResolutions.FaceAlbedo);

	if (InParams.bBlocking)
	{
		UE::MetaHuman::WaitForCloudRequests([this, InCharacter]
											{
												return IsRequestingHighResolutionTextures(InCharacter);
											});
	}
}

bool UMetaHumanCharacterEditorSubsystem::IsRequestingHighResolutionTextures(TNotNull<const UMetaHumanCharacter*> InCharacter) const
{
	check(IsInGameThread());

	if (const FMetaHumanCharacterEditorCloudRequests* Requests = CharacterCloudRequests.Find(InCharacter))
	{
		return Requests->TextureSynthesis.IsValid() || Requests->BodyTextures.IsValid();
	}

	return false;
}

void UMetaHumanCharacterEditorSubsystem::OnHighResolutionTexturesRequestCompleted(TSharedPtr<UE::MetaHuman::FFaceHighFrequencyData> InResponse, TObjectKey<UMetaHumanCharacter> InCharacterKey)
{
	//NOTE: This delegate is *only* invoked if the TS download is complete and all the images have been received. Hence it can assert that the image data is present and valid
	//		If any of the downloaded images are found to be incorrect (for whatever reason) this delegate is never invoked
	
	check(IsInGameThread());
	// The cloud request should exist, even if the character is no longer valid
	check(CharacterCloudRequests.Contains(InCharacterKey));

	FMetaHumanCharacterEditorCloudRequests& CloudRequests = CharacterCloudRequests[InCharacterKey];

	ON_SCOPE_EXIT
	{
		CloudRequests.TextureSynthesisRequestFinished();

		if (!CloudRequests.HasActiveRequest())
		{
			OnDownloadingTexturesStateChanged.Broadcast(InCharacterKey.ResolveObjectPtr());
			CharacterCloudRequests.Remove(InCharacterKey);
		}
	};

	UMetaHumanCharacter* InMetaHumanCharacter = InCharacterKey.ResolveObjectPtr();
	if (!InMetaHumanCharacter || !IsObjectAddedForEditing(InMetaHumanCharacter))
	{
		// Character has been forcibly deleted or the asset editor was closed
		return;
	}

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InMetaHumanCharacter];

	// Calculate how much time it took to get the textures
	const double ElapsedTime = FPlatformTime::Seconds() - CloudRequests.TextureSynthesisStartTime;
	
	if (FMetaHumanCharacterEditorCloudRequests::GenerateTexturesFromResponse(InResponse, FaceTextureSynthesizer, CharacterData, InMetaHumanCharacter))
	{
		UpdateCharacterPreviewMaterial(InMetaHumanCharacter, InMetaHumanCharacter->PreviewMaterialType);
	}

	const FText Message = FText::Format(LOCTEXT("DownloadSourceFaceTexturesCompleted", "Download of source face textures finished in {0} seconds"), ElapsedTime);
	UE::MetaHuman::ShowNotification(Message, SNotificationItem::ECompletionState::CS_Success);

	if (TSharedPtr<SNotificationItem> TextureSynthesisNotificationItem = CloudRequests.TextureSynthesisNotificationItem.Pin())
	{
		TextureSynthesisNotificationItem->SetCompletionState(SNotificationItem::CS_Success);
		TextureSynthesisNotificationItem->ExpireAndFadeout();
	}
}

void UMetaHumanCharacterEditorSubsystem::OnHighResolutionTexturesRequestFailed(EMetaHumanServiceRequestResult InResult, TObjectKey<UMetaHumanCharacter> InCharacterKey)
{
	check(CharacterCloudRequests.Contains(InCharacterKey));
	check(IsInGameThread());

	FMetaHumanCharacterEditorCloudRequests& CloudRequests = CharacterCloudRequests[InCharacterKey];

	FText Message;
	if(InResult != EMetaHumanServiceRequestResult::Unauthorized)
	{
		Message = FText::Format(LOCTEXT("DownloadFailedMessage", "Download of source face textures failed with code '{0}'"), UEnum::GetDisplayValueAsText(InResult));
	}
	else
	{
		Message = LOCTEXT("DownloadLoginFailedMessage", "User not logged in, please autorig before downloading source face textures");
	}
	UE::MetaHuman::ShowNotification(Message, SNotificationItem::ECompletionState::CS_Fail);
	FSlateNotificationManager::Get().CancelProgressNotification(CloudRequests.TextureSynthesisProgressHandle);

	// always try to stop the "spinner"
	if (TSharedPtr<SNotificationItem> TextureSynthesisNotificationItem = CloudRequests.TextureSynthesisNotificationItem.Pin())
	{
		TextureSynthesisNotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
		TextureSynthesisNotificationItem->ExpireAndFadeout();
	}

	CloudRequests.TextureSynthesisRequestFinished();
	if (!CloudRequests.HasActiveRequest())
	{
		OnDownloadingTexturesStateChanged.Broadcast(InCharacterKey.ResolveObjectPtr());
		CharacterCloudRequests.Remove(InCharacterKey);
	}
}

void UMetaHumanCharacterEditorSubsystem::OnHighResolutionTexturesProgressUpdated(float InPercentage, TObjectKey<UMetaHumanCharacter> InCharacterKey)
{
	check(CharacterCloudRequests.Contains(InCharacterKey));
	check(IsInGameThread());

	FMetaHumanCharacterEditorCloudRequests& CloudRequests = CharacterCloudRequests[InCharacterKey];

	UMetaHumanCharacter* Character = InCharacterKey.ResolveObjectPtr();
	if (!Character || !IsObjectAddedForEditing(Character))
	{
		// Character has been forcibly deleted or the asset editor was closed
		return;
	}

	float Percentage = InPercentage * 100.f;
	FSlateNotificationManager::Get().UpdateProgressNotification(CloudRequests.TextureSynthesisProgressHandle, Percentage);
}

void UMetaHumanCharacterEditorSubsystem::OnHighResolutionBodyTexturesRequestCompleted(TSharedPtr<UE::MetaHuman::FBodyHighFrequencyData> InResponse, TObjectKey<UMetaHumanCharacter> InCharacterKey)
{
	//NOTE: This delegate is *only* invoked if the TS download is complete and all the images have been received. Hence it can assert that the image data is present and valid
	//		If any of the downloaded images are found to be incorrect (for whatever reason) this delegate is never invoked
	
	check(IsInGameThread());
	// The cloud request should exist, even if the character is no longer valid
	check(CharacterCloudRequests.Contains(InCharacterKey));

	FMetaHumanCharacterEditorCloudRequests& CloudRequests = CharacterCloudRequests[InCharacterKey];

	ON_SCOPE_EXIT
	{
		CloudRequests.BodyTextureRequestFinished();

		if (!CloudRequests.HasActiveRequest())
		{
			OnDownloadingTexturesStateChanged.Broadcast(InCharacterKey.ResolveObjectPtr());
			CharacterCloudRequests.Remove(InCharacterKey);
		}
	};

	UMetaHumanCharacter* InMetaHumanCharacter = InCharacterKey.ResolveObjectPtr();
	if (!InMetaHumanCharacter || !IsObjectAddedForEditing(InMetaHumanCharacter))
	{
		// Character has been forcibly deleted or the asset editor was closed
		return;
	}

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InMetaHumanCharacter];

	// Calculate how much time it took to get the textures
	const double ElapsedTime = FPlatformTime::Seconds() - CloudRequests.BodyTextureStartTime;

	if (FMetaHumanCharacterEditorCloudRequests::GenerateBodyTexturesFromResponse(InResponse, InMetaHumanCharacter))
	{
		// Build a texture set with only the body textures considering their overrides
		const FMetaHumanCharacterSkinTextureSet FinalFaceTextureSet =
			InMetaHumanCharacter->SkinSettings.GetFinalSkinTextureSet(FMetaHumanCharacterSkinTextureSet
				{
					.Body = InMetaHumanCharacter->BodyTextures
				});

		// Update the Body Material Parameters
		FMetaHumanCharacterBodyTextureUtils::GetSkinToneAndUpdateMaterials( InMetaHumanCharacter->SkinSettings.Skin,
																		   FaceTextureSynthesizer,
																		   FinalFaceTextureSet.Body,
																		   CharacterData->HeadMaterials,
																		   CharacterData->BodyMaterial);
	}

	const FText Message = FText::Format(LOCTEXT("DownloadSourceBodyTexturesCompleted", "Download of source body textures finished in {0} seconds"), ElapsedTime);
	UE::MetaHuman::ShowNotification(Message, SNotificationItem::ECompletionState::CS_Success);

	if (TSharedPtr<SNotificationItem> BodyTextureNotificationItem = CloudRequests.BodyTextureNotificationItem.Pin())
	{
		BodyTextureNotificationItem->SetCompletionState(SNotificationItem::CS_Success);
		BodyTextureNotificationItem->ExpireAndFadeout();
	}
}

void UMetaHumanCharacterEditorSubsystem::OnHighResolutionBodyTexturesRequestFailed(EMetaHumanServiceRequestResult InResult, TObjectKey<UMetaHumanCharacter> InCharacterKey)
{
	check(CharacterCloudRequests.Contains(InCharacterKey));
	check(IsInGameThread());

	FMetaHumanCharacterEditorCloudRequests& CloudRequests = CharacterCloudRequests[InCharacterKey];

	FText Message;
	if (InResult != EMetaHumanServiceRequestResult::Unauthorized)
	{
		Message = FText::Format(LOCTEXT("DownloadBodyFailedMessage", "Download of source body textures failed with code '{0}'"), UEnum::GetDisplayValueAsText(InResult));
	}
	else
	{
		Message = LOCTEXT("DownloadBodyLoginFailedMessage", "User not logged in, please autorig before downloading source body textures");
	}
	UE::MetaHuman::ShowNotification(Message, SNotificationItem::ECompletionState::CS_Fail);
	FSlateNotificationManager::Get().CancelProgressNotification(CloudRequests.BodyTextureProgressHandle);

	// always try to stop the "spinner"
	if (TSharedPtr<SNotificationItem> BodyTextureNotificationItem = CloudRequests.BodyTextureNotificationItem.Pin())
	{
		BodyTextureNotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
		BodyTextureNotificationItem->ExpireAndFadeout();
	}

	CloudRequests.BodyTextureRequestFinished();
	if (!CloudRequests.HasActiveRequest())
	{
		OnDownloadingTexturesStateChanged.Broadcast(InCharacterKey.ResolveObjectPtr());
		CharacterCloudRequests.Remove(InCharacterKey);
	}
}

void UMetaHumanCharacterEditorSubsystem::OnHighResolutionBodyTexturesProgressUpdated(float InPercentage, TObjectKey<UMetaHumanCharacter> InCharacterKey)
{
	if (!CharacterCloudRequests.Contains(InCharacterKey))
	{
		// this can happen if some textures are in the DDC and others are not, while the user is not logged in
		return;
	}
	check(IsInGameThread());

	FMetaHumanCharacterEditorCloudRequests& CloudRequests = CharacterCloudRequests[InCharacterKey];

	UMetaHumanCharacter* Character = InCharacterKey.ResolveObjectPtr();
	if (!Character || !IsObjectAddedForEditing(Character))
	{
		// Character has been forcibly deleted or the asset editor was closed
		return;
	}

	float Percentage = InPercentage * 100.f;
	FSlateNotificationManager::Get().UpdateProgressNotification(CloudRequests.BodyTextureProgressHandle, Percentage);
}

void UMetaHumanCharacterEditorSubsystem::ApplyFaceEvaluationSettings(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, const FMetaHumanCharacterFaceEvaluationSettings& InFaceEvaluationSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::ApplyFaceEvaluationSettings");

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InMetaHumanCharacter];

	FMetaHumanCharacterIdentity::FSettings Settings = CharacterData->FaceState->GetSettings();
	Settings.SetGlobalVertexDeltaScale(InFaceEvaluationSettings.GlobalDelta);
	Settings.SetGlobalHighFrequencyScale(InFaceEvaluationSettings.HighFrequencyDelta);
	CharacterData->FaceState->SetSettings(Settings);
	CharacterData->FaceState->SetFaceScale(InFaceEvaluationSettings.HeadScale);
	
	// Update the actor face skel mesh to apply face settings
	const FMetaHumanRigEvaluatedState FaceVerticesAndVertexNormals = CharacterData->FaceState->Evaluate();
	UpdateFaceMeshInternal(CharacterData, FaceVerticesAndVertexNormals, GetUpdateOptionForEditing());

	CharacterData->FaceEvaluationSettings = InFaceEvaluationSettings;
}

void UMetaHumanCharacterEditorSubsystem::CommitFaceEvaluationSettings(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, const FMetaHumanCharacterFaceEvaluationSettings& InFaceEvaluationSettings)
{
	InMetaHumanCharacter->FaceEvaluationSettings = InFaceEvaluationSettings;
	InMetaHumanCharacter->MarkPackageDirty();

	ApplyFaceEvaluationSettings(InMetaHumanCharacter, InMetaHumanCharacter->FaceEvaluationSettings);
}

const FMetaHumanFaceTextureAttributeMap& UMetaHumanCharacterEditorSubsystem::GetFaceTextureAttributeMap() const
{
	return FaceTextureSynthesizer.GetFaceTextureAttributeMap();
}

void UMetaHumanCharacterEditorSubsystem::ApplyHeadModelSettings(TNotNull<UMetaHumanCharacter*> InCharacter, const FMetaHumanCharacterHeadModelSettings& InHeadModelSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::ApplyHeadModelSettings");

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	const bool bUpdateEyelashesVariant = !CharacterData->HeadModelSettings.IsSet() || CharacterData->HeadModelSettings.GetValue().Eyelashes.Type != InHeadModelSettings.Eyelashes.Type;
	const bool bUpdateTeethVariant = !CharacterData->HeadModelSettings.IsSet() || InHeadModelSettings.Teeth != CharacterData->HeadModelSettings.GetValue().Teeth;
	const bool bUpdateEyelashesMaterials = !CharacterData->HeadModelSettings.IsSet() || InHeadModelSettings.Eyelashes.AreMaterialsUpdated(CharacterData->HeadModelSettings.GetValue().Eyelashes);
	const bool bUpdateTeethMaterials = !CharacterData->HeadModelSettings.IsSet() || InHeadModelSettings.Teeth.AreMaterialsUpdated(CharacterData->HeadModelSettings.GetValue().Teeth);
	const bool bToggleEyelashesGrooms = !CharacterData->HeadModelSettings.IsSet() || CharacterData->HeadModelSettings.GetValue().Eyelashes.bEnableGrooms != InHeadModelSettings.Eyelashes.bEnableGrooms;
	if (bUpdateEyelashesVariant || bUpdateTeethVariant)
	{
		// ensure that we only call EvaluateState once by combining eyelash and teeth updates
		ApplyEyelashesAndTeethPropertiesToFaceState(CharacterData, InHeadModelSettings.Eyelashes, InHeadModelSettings.Teeth, bUpdateEyelashesVariant, bUpdateTeethVariant, GetUpdateOptionForEditing());
	}
	if (bUpdateEyelashesMaterials || bUpdateEyelashesVariant)
	{
		FMetaHumanCharacterSkinMaterials::ApplyEyelashesPropertiesToMaterial(CharacterData->HeadMaterials, InHeadModelSettings.Eyelashes);
	}
	if ((bUpdateEyelashesVariant && InHeadModelSettings.Eyelashes.bEnableGrooms) || bToggleEyelashesGrooms)
	{
		ToggleEyelashesGrooms(InCharacter, InHeadModelSettings.Eyelashes);
	}
	if (bUpdateTeethMaterials)
	{
		FMetaHumanCharacterSkinMaterials::ApplyTeethPropertiesToMaterial(CharacterData->HeadMaterials, InHeadModelSettings.Teeth);
	}
	CharacterData->HeadModelSettings = InHeadModelSettings;
}

void UMetaHumanCharacterEditorSubsystem::CommitHeadModelSettings(UMetaHumanCharacter* InMetaHumanCharacter, const FMetaHumanCharacterHeadModelSettings& InHeadModelSettings)
{
	if (!IsValid(InMetaHumanCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CommitHeadModelSettings called with invalid character");
		return;
	}

	if (!IsValid(InMetaHumanCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CommitHeadModelSettings called with {Character} that is not not added for editing", InMetaHumanCharacter->GetName());
		return;
	}

	InMetaHumanCharacter->HeadModelSettings = InHeadModelSettings;
	InMetaHumanCharacter->MarkPackageDirty();
	
	ApplyHeadModelSettings(InMetaHumanCharacter, InMetaHumanCharacter->HeadModelSettings);
}

void UMetaHumanCharacterEditorSubsystem::ToggleEyelashesGrooms(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, const FMetaHumanCharacterEyelashesProperties& InEyelashesProperties) const
{
	const FName SlotName = TEXT("Eyelashes");
	TNotNull<UMetaHumanCollection*> Collection = InMetaHumanCharacter->GetMutableInternalCollection();

	// Check if we have requested slot
	const FMetaHumanCharacterPipelineSlot* Slot = Collection->GetPipeline()->GetSpecification()->Slots.Find(SlotName);
	if (!Slot)
	{
		// Slot not found.
		return;
	}

	if (InEyelashesProperties.Type != EMetaHumanCharacterEyelashesType::None && InEyelashesProperties.bEnableGrooms)
	{
		const UMetaHumanCharacterEditorWardrobeSettings* WardrobeSettings = GetDefault<UMetaHumanCharacterEditorWardrobeSettings>();
		if (const FSoftObjectPath* FoundBinding = WardrobeSettings->EyelashesTypeToAssetPath.Find(InEyelashesProperties.Type))
		{
			// First check if the asset already exists.
			const FMetaHumanCharacterPaletteItem* FoundItem = Collection->GetItems().FindByPredicate(
				[FoundBinding, SlotName](const FMetaHumanCharacterPaletteItem& Item)
				{
					return Item.SlotName == SlotName
						&& Item.WardrobeItem
						&& Item.WardrobeItem->IsExternal()
						&& FSoftObjectPath(Item.WardrobeItem) == *FoundBinding;
				});


			FMetaHumanPaletteItemKey PaletteItemKey;
			if (FoundItem)
			{
				// Eyelashes groom is already attached.
				PaletteItemKey = FoundItem->GetItemKey();
			}
			else
			{
				TSoftObjectPtr<UMetaHumanWardrobeItem> WordrobeItemRef{ *FoundBinding };
				if (UMetaHumanWardrobeItem* WardrobeItem = WordrobeItemRef.LoadSynchronous())
				{
					if (!Collection->TryAddItemFromWardrobeItem(SlotName, WardrobeItem, PaletteItemKey))
					{
						UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to add eyelashes groom {WardrobeItem}", GetFullNameSafe(WardrobeItem));
						return;
					}
				}
			}
			Collection->GetMutableDefaultInstance()->SetSingleSlotSelection(SlotName, PaletteItemKey);
		}
	}
	else
	{
		Collection->GetMutableDefaultInstance()->SetSingleSlotSelection(SlotName, FMetaHumanPaletteItemKey());
	}

	RunCharacterEditorPipelineForPreview(InMetaHumanCharacter);
}


void UMetaHumanCharacterEditorSubsystem::ApplySkinSettings(
	TNotNull<UMetaHumanCharacter*> InCharacter,
	const FMetaHumanCharacterSkinSettings& InSkinSettings) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::ApplySkinSettings");

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	// Update texture set in character if changed
	FMetaHumanCharacterBodyTextureUtils::UpdateBodyTextureSet(CharacterData->SkinSettings, InSkinSettings.Skin, InCharacter->HighResBodyTexturesInfo, InCharacter->BodyTextures);

	// Build a texture set considering any overrides in the skin settings
	const FMetaHumanCharacterSkinTextureSet FinalSkinTextureSet =
		InSkinSettings.GetFinalSkinTextureSet(FMetaHumanCharacterSkinTextureSet
											  {
												  .Face = InCharacter->GetValidFaceTextures(),
												  .Body = InCharacter->BodyTextures
											  });

	// Update the Foundation Color based on the skin tone if a preset was selected
	const int32 FoundationColorIndex = InCharacter->MakeupSettings.Foundation.PresetIndex;
	if (FoundationColorIndex != INDEX_NONE && FaceTextureSynthesizer.IsValid())
	{
		const FVector2f UV{ InSkinSettings.Skin.U, InSkinSettings.Skin.V };
		const FLinearColor SkinToneColor = GetSkinTone(UV);

		const FLinearColor FoundationColor = FMetaHumanCharacterSkinMaterials::ShiftFoundationColor(SkinToneColor,
																									FoundationColorIndex,
																									FMetaHumanCharacterSkinMaterials::FoundationPaletteColumns,
																									FMetaHumanCharacterSkinMaterials::FoundationPaletteRows,
																									FMetaHumanCharacterSkinMaterials::FoundationSaturationShift,
																									FMetaHumanCharacterSkinMaterials::FoundationValueShift);
		InCharacter->MakeupSettings.Foundation.Color = FoundationColor;
	}

	const bool bForceUseExistingTextures = false;
	bool bTexturesHaveBeenRegenerated = false;
	ApplySkinSettings(CharacterData, InSkinSettings, bForceUseExistingTextures, FinalSkinTextureSet, InCharacter->SynthesizedFaceTextures, InCharacter->BodyTextures, bTexturesHaveBeenRegenerated);

	if (bTexturesHaveBeenRegenerated)
	{
		const bool bHadHighResolutionTextures = InCharacter->HasHighResolutionTextures();
		InCharacter->SetHasHighResolutionTextures(false);
		InCharacter->ResetUnreferencedHighResTextureData();

		if (bHadHighResolutionTextures)
		{
			// Update the preview material to ensure the Virtual/Regular version is loaded
			UpdateCharacterPreviewMaterial(InCharacter, InCharacter->PreviewMaterialType);
		}
	}	

	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	const bool bUseFaceMaterialsWithVTSupport = Settings->ShouldUseVirtualTextures() && InCharacter->HasHighResolutionTextures();


	// Update the Sclera tint based on the skin tone if needed and reapply the eye settings
	FMetaHumanCharacterSkinMaterials::ApplyEyeScleraTintBasedOnSkinTone(InSkinSettings, InCharacter->EyesSettings);

	const FMetaHumanCharacterEyesSettings& EyesSettings = CharacterData->EyesSettings.IsSet() ? CharacterData->EyesSettings.GetValue() : InCharacter->EyesSettings;
	ApplyEyesSettings(CharacterData, EyesSettings);

	const FMetaHumanCharacterMakeupSettings& MakeupSettings = CharacterData->MakeupSettings.IsSet() ? CharacterData->MakeupSettings.GetValue() : InCharacter->MakeupSettings;
	ApplyMakeupSettings(CharacterData, MakeupSettings, bUseFaceMaterialsWithVTSupport);
}

void UMetaHumanCharacterEditorSubsystem::ApplySkinSettings(
	TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
	const FMetaHumanCharacterSkinSettings& InSkinSettings,
	bool bInForceUseExistingTextures,
	const FMetaHumanCharacterSkinTextureSet& InFinalSkinTextureSet,
	TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& InOutSynthesizedFaceTextures,
	TMap<EBodyTextureType, TObjectPtr<UTexture2D>>& InOutBodyTextures,
	bool& bOutTexturesHaveBeenRegenerated) const
{
	bOutTexturesHaveBeenRegenerated = false;

	// If the properties that affect texture synthesis have changed, re-run TS now. Any high
	// res textures that have been downloaded will be discarded.
	//
	// Callers should detect when high res textures will be discarded and prompt the user to
	// confirm before calling this function.
	if (bInForceUseExistingTextures
		|| (InCharacterData->SkinSettings.IsSet()
			&& InCharacterData->SkinSettings.GetValue().Skin == InSkinSettings.Skin))
	{
		UpdateSkinTextures(InCharacterData, InSkinSettings.Skin, InFinalSkinTextureSet);
	}
	else if (FaceTextureSynthesizer.IsValid())
	{
		ApplySkinProperties(InCharacterData, InSkinSettings.Skin, InOutSynthesizedFaceTextures, InOutBodyTextures);
		bOutTexturesHaveBeenRegenerated = true;
	}

	// Apply the skin material parameters to the face and body materials
	FMetaHumanCharacterSkinMaterials::ApplySkinParametersToMaterials(InCharacterData->HeadMaterials, InCharacterData->BodyMaterial, InSkinSettings);

	InCharacterData->SkinSettings = InSkinSettings;
}

void UMetaHumanCharacterEditorSubsystem::UpdateSkinTextures(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
															const FMetaHumanCharacterSkinProperties& InSkinProperties,
															const FMetaHumanCharacterSkinTextureSet& InSkinTextureSet) const
{
	// Set the face textures to the face material
	FMetaHumanCharacterSkinMaterials::ApplySynthesizedTexturesToFaceMaterial(InCharacterData->HeadMaterials, InSkinTextureSet.Face);

	// Update the Body Material Parameters to match
	FMetaHumanCharacterBodyTextureUtils::GetSkinToneAndUpdateMaterials(InSkinProperties,
																	   FaceTextureSynthesizer,
																	   InSkinTextureSet.Body,
																	   InCharacterData->HeadMaterials,
																	   InCharacterData->BodyMaterial);
}

void UMetaHumanCharacterEditorSubsystem::ApplyEyesSettings(TNotNull<UMetaHumanCharacter*> InCharacter, const FMetaHumanCharacterEyesSettings& InEyesSettings) const
{
	check(CharacterDataMap.Contains(InCharacter));
	ApplyEyesSettings(CharacterDataMap[InCharacter], InEyesSettings);
}

void UMetaHumanCharacterEditorSubsystem::ApplyEyesSettings(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, const FMetaHumanCharacterEyesSettings& InEyesSettings)
{
	// Store the eye settings in the character data
	InCharacterData->EyesSettings = InEyesSettings;

	FMetaHumanCharacterSkinMaterials::ApplyEyeSettingsToMaterial(InCharacterData->HeadMaterials, InEyesSettings);
}

void UMetaHumanCharacterEditorSubsystem::CommitEyesSettings(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterEyesSettings& InEyesSettings) const
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CommitEyesSettings called with invalid character");
		return;
	}

	if (!IsObjectAddedForEditing(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CommitEyesSettings called with {Character} that is not not added for editing", InCharacter->GetName());
		return;
	}

	InCharacter->EyesSettings = InEyesSettings;
	InCharacter->MarkPackageDirty();

	ApplyEyesSettings(InCharacter, InCharacter->EyesSettings);
}

void UMetaHumanCharacterEditorSubsystem::ApplyMakeupSettings(TNotNull<UMetaHumanCharacter*> InCharacter, const FMetaHumanCharacterMakeupSettings& InMakeupSettings) const
{
	check(CharacterDataMap.Contains(InCharacter));

	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	const bool bUseFaceMaterialWithVTSupport = Settings->ShouldUseVirtualTextures() && InCharacter->HasHighResolutionTextures();
	ApplyMakeupSettings(CharacterDataMap[InCharacter], InMakeupSettings, bUseFaceMaterialWithVTSupport);
}

void UMetaHumanCharacterEditorSubsystem::ApplyMakeupSettings(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, const FMetaHumanCharacterMakeupSettings& InMakeupSettings, bool bInWithVTSupport)
{
	InCharacterData->MakeupSettings = InMakeupSettings;

	FMetaHumanCharacterSkinMaterials::ApplyFoundationMakeupToMaterial(InCharacterData->HeadMaterials, InMakeupSettings.Foundation, bInWithVTSupport);
	FMetaHumanCharacterSkinMaterials::ApplyEyeMakeupToMaterial(InCharacterData->HeadMaterials, InMakeupSettings.Eyes, bInWithVTSupport);
	FMetaHumanCharacterSkinMaterials::ApplyBlushMakeupToMaterial(InCharacterData->HeadMaterials, InMakeupSettings.Blush, bInWithVTSupport);
	FMetaHumanCharacterSkinMaterials::ApplyLipsMakeupToMaterial(InCharacterData->HeadMaterials, InMakeupSettings.Lips, bInWithVTSupport);
}

void UMetaHumanCharacterEditorSubsystem::CommitMakeupSettings(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterMakeupSettings& InMakeupSettings) const
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CommitMakeupSettings called with invalid character");
		return;
	}

	if (!IsObjectAddedForEditing(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CommitMakeupSettings called with {Character} that is not not added for editing", InCharacter->GetName());
		return;
	}

	InCharacter->MakeupSettings = InMakeupSettings;
	InCharacter->MarkPackageDirty();

	ApplyMakeupSettings(InCharacter, InCharacter->MakeupSettings);
}

void UMetaHumanCharacterEditorSubsystem::ApplySkinProperties(
	TSharedRef<FMetaHumanCharacterEditorData> InCharacterData,
	const FMetaHumanCharacterSkinProperties& InSkinProperties,
	TMap<EFaceTextureType, TObjectPtr<UTexture2D>>& InOutSynthesizedFaceTextures,
	TMap<EBodyTextureType, TObjectPtr<UTexture2D>>& InOutBodyTextures) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::ApplySkinSynthesizeProperties");

	const bool bNeedToRecreateTextures = !FMetaHumanCharacterTextureSynthesis::AreTexturesAndImagesSuitableForSynthesis(FaceTextureSynthesizer,
																													InOutSynthesizedFaceTextures,
																													InCharacterData->CachedSynthesizedImages);
	if (bNeedToRecreateTextures)
	{
		// Recreate the textures so that they match the size and format generated by the TS model
		// Note that this can cause a "de-sync" between the face texture info and the texture objects but 
		// they will be updated on the next call to commit the skin settings
		// TODO: should/can we clear up some texture memory here?

		InOutSynthesizedFaceTextures.Reset();
		InCharacterData->CachedSynthesizedImages.Reset();

		FMetaHumanCharacterTextureSynthesis::InitSynthesizedFaceData(FaceTextureSynthesizer,
																	 TMap<EFaceTextureType, FMetaHumanCharacterTextureInfo>{},
																	 InOutSynthesizedFaceTextures,
																	 InCharacterData->CachedSynthesizedImages);

		FMetaHumanCharacterTextureSynthesis::CreateSynthesizedFaceTextures(FaceTextureSynthesizer.GetTextureSizeX(),
																		   InOutSynthesizedFaceTextures);
	}

	// TS data should have been initialized by this point
	if (!FMetaHumanCharacterTextureSynthesis::SynthesizeFaceTextures(
		InSkinProperties,
		FaceTextureSynthesizer,
		InCharacterData->CachedSynthesizedImages))
	{
		// TODO: Should we clear any synthesized textures here to get back to a consistent state?
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to synthesize face textures");
	}

	if (!InCharacterData->SkinSettings.IsSet() || bNeedToRecreateTextures 
		|| InCharacterData->SkinSettings.GetValue().Skin.FaceTextureIndex != InSkinProperties.FaceTextureIndex)
	{
		// Only need to update face state if texture has changed
		if (!FMetaHumanCharacterTextureSynthesis::SelectFaceTextures(
			InSkinProperties,
			FaceTextureSynthesizer,
			InCharacterData->CachedSynthesizedImages))
		{
			UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to select face textures");
		}

		ApplySkinPropertiesToFaceState(InCharacterData, InSkinProperties);
	}

	if (!FMetaHumanCharacterTextureSynthesis::UpdateFaceTextures(InCharacterData->CachedSynthesizedImages, InOutSynthesizedFaceTextures))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to update face textures");
	}

	// Update the Body Material Parameters to match
	FMetaHumanCharacterBodyTextureUtils::GetSkinToneAndUpdateMaterials(InSkinProperties,
																	   FaceTextureSynthesizer,
																	   InOutBodyTextures,
																	   InCharacterData->HeadMaterials,
																	   InCharacterData->BodyMaterial);
}

void UMetaHumanCharacterEditorSubsystem::UpdateFaceMeshInternal(
	TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, 
	const FMetaHumanRigEvaluatedState& InVerticesAndNormals, 
	ELodUpdateOption InUpdateOption)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::UpdateFaceMeshInternal");

	FMetaHumanCharacterSkelMeshUtils::UpdateLODModelVertexPositions(
		InCharacterData->FaceMesh,
		InVerticesAndNormals,
		*InCharacterData->FaceState,
		*InCharacterData->FaceDnaToSkelMeshMap,
		InUpdateOption,
		EVertexPositionsAndNormals::Both,
		InCharacterData->PreviewMergedHeadAndBody,
		&InCharacterData->PreviewMergedMeshMapping,
		bOutfitResizingRequiresNormals);

	const bool bRebuildTangents = true;
	USkelMeshDNAUtils::RebuildRenderData_VertexPosition(InCharacterData->FaceMesh, bRebuildTangents);

	ForEachCharacterActor(InCharacterData,
		[](TScriptInterface<IMetaHumanCharacterEditorActorInterface> Actor)
		{
			Actor->OnFaceMeshUpdated();
		});
}

const UMetaHumanCharacterEditorSubsystem::FMetaHumanCharacterIdentityModels& UMetaHumanCharacterEditorSubsystem::GetOrCreateCharacterIdentity(EMetaHumanCharacterTemplateType InTemplateType)
{
	FMetaHumanCharacterIdentityModels& IdentityModels = CharacterIdentities.FindOrAdd(InTemplateType);

	const FString BodyModelPath = GetBodyIdentityModelPath();

	if (!IdentityModels.Face.IsValid())
	{
		const FString ModelPath = GetFaceIdentityTemplateModelPath(InTemplateType);
		UDNAAsset* FaceArchetypeDNA = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAseet(EMetaHumanImportDNAType::Face, GetTransientPackage());
		check(FaceArchetypeDNA);

		const EMetaHumanCharacterOrientation HeadOrientation = EMetaHumanCharacterOrientation::Y_UP;

		IdentityModels.Face = MakeShared<FMetaHumanCharacterIdentity>();
		const bool bIsInitialized = IdentityModels.Face->Init(ModelPath, BodyModelPath, FaceArchetypeDNA, HeadOrientation);
		check(bIsInitialized);
	}

	if (!IdentityModels.Body.IsValid())
	{
		const FString LegacyBodiesPath = GetLegacyBodiesPath();

		IdentityModels.Body = MakeShared<FMetaHumanCharacterBodyIdentity>();
		const bool bIsInitialized = IdentityModels.Body->Init(BodyModelPath, LegacyBodiesPath);
		check(bIsInitialized);
	}

	return IdentityModels;
}

USkeletalMesh* UMetaHumanCharacterEditorSubsystem::GetFaceArchetypeMesh(EMetaHumanCharacterTemplateType InTemplateType)
{
	USkeletalMesh* FaceArchetypeMesh = nullptr;

	if (InTemplateType == EMetaHumanCharacterTemplateType::MetaHuman)
	{
		TSharedPtr<IDNAReader> ArchetypeDnaReader = nullptr;
		FaceArchetypeMesh = FMetaHumanCharacterSkelMeshUtils::CreateArchetypeSkelMeshFromDNA(EMetaHumanImportDNAType::Face, ArchetypeDnaReader);
		if (FaceArchetypeMesh)
		{
			const bool bIsFace = true;
			FMetaHumanCharacterSkelMeshUtils::PopulateSkelMeshData(FaceArchetypeMesh, ArchetypeDnaReader, bIsFace);
		}
		else
		{
			UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to load the face archetype DNA file from plugin content");
		}
	}

	return FaceArchetypeMesh;
}

USkeletalMesh* UMetaHumanCharacterEditorSubsystem::GetBodyArchetypeMesh(EMetaHumanCharacterTemplateType InTemplateType)
{
	USkeletalMesh* BodyArchetypeMesh = nullptr;

	if (ensureAlways(InTemplateType == EMetaHumanCharacterTemplateType::MetaHuman))
	{
		TSharedPtr<IDNAReader> ArchetypeDnaReader = nullptr;
		BodyArchetypeMesh = FMetaHumanCharacterSkelMeshUtils::CreateArchetypeSkelMeshFromDNA(EMetaHumanImportDNAType::Body, ArchetypeDnaReader);
		if (BodyArchetypeMesh)
		{
			FMetaHumanCharacterSkelMeshUtils::PopulateSkelMeshData(BodyArchetypeMesh, ArchetypeDnaReader, false /*bIsFace*/);
		}
		else
		{
			UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to load the body archetype DNA file from plugin content");
		}
	}

	return BodyArchetypeMesh;
}

USkeletalMesh* UMetaHumanCharacterEditorSubsystem::CreateCombinedFaceAndBodyMesh(TNotNull<const UMetaHumanCharacter*> InCharacter, const FString& InAssetPathAndName)
{
	check(CharacterDataMap.Contains(InCharacter));
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	USkeletalMesh* CombinedSkelMesh = FMetaHumanCharacterEditorBuild::MergeHeadAndBody_CreateAsset(
		CharacterData->FaceMesh,
		CharacterData->BodyMesh,
		InAssetPathAndName
	);

	if (CombinedSkelMesh)
	{
		// Body data that we want to record as asset user data
		UChaosOutfitAssetBodyUserData* BodyUserData = CombinedSkelMesh->GetAssetUserData<UChaosOutfitAssetBodyUserData>();

		if (!BodyUserData)
		{
			BodyUserData = NewObject<UChaosOutfitAssetBodyUserData>(CombinedSkelMesh);
			CombinedSkelMesh->AddAssetUserData(BodyUserData);
		}

		const bool bGetMeasuresFromDNA = GetRiggingState(InCharacter) == EMetaHumanCharacterRigState::Rigged;
		BodyUserData->Measurements = GetFaceAndBodyMeasurements(CharacterData, bGetMeasuresFromDNA);
	}

	return CombinedSkelMesh;
}

bool UMetaHumanCharacterEditorSubsystem::IsTextureSynthesisEnabled() const
{
	return FaceTextureSynthesizer.IsValid();
}

FString UMetaHumanCharacterEditorSubsystem::GetFaceIdentityTemplateModelPath(EMetaHumanCharacterTemplateType InTemplateType)
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME);
	check(Plugin.IsValid());
	const FString PluginContentDir = FPaths::ConvertRelativePathToFull(Plugin->GetContentDir());

	FString ModelPath;

	if (InTemplateType == EMetaHumanCharacterTemplateType::MetaHuman)
	{
		ModelPath = PluginContentDir / TEXT("Face/IdentityTemplate");
	}
	else
	{
		check(false);
	}

	return ModelPath;
}

FString UMetaHumanCharacterEditorSubsystem::GetBodyIdentityModelPath()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME);
	check(Plugin.IsValid());
	const FString PluginContentDir = FPaths::ConvertRelativePathToFull(Plugin->GetContentDir());

	return PluginContentDir / TEXT("Body/IdentityTemplate");
}

FString UMetaHumanCharacterEditorSubsystem::GetLegacyBodiesPath()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME);
	check(Plugin.IsValid());
	const FString PluginContentDir = FPaths::ConvertRelativePathToFull(Plugin->GetContentDir());

	return PluginContentDir / TEXT("Optional/Body/FixedCompatibility");
}

void UMetaHumanCharacterEditorSubsystem::ApplyBodyState(
	TNotNull<const UMetaHumanCharacter*> InCharacter,
	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InState,
	EBodyMeshUpdateMode InUpdateMode)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	// Take a copy of the passed-in state so that the caller can't retain a non-const reference to it
	ApplyBodyState(CharacterData, MakeShared<FMetaHumanCharacterBodyIdentity::FState>(*InState), InUpdateMode);
}

void UMetaHumanCharacterEditorSubsystem::ApplyBodyState(
	TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, 
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> InState,
	EBodyMeshUpdateMode InUpdateMode)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::ApplyBodyState");

	InCharacterData->BodyState = InState;

	const FMetaHumanRigEvaluatedState VerticesAndVertexNormals = InCharacterData->BodyState->GetVerticesAndVertexNormals();

	if (InUpdateMode == EBodyMeshUpdateMode::Minimal)
	{
		UpdateBodyMeshInternal(InCharacterData, VerticesAndVertexNormals, ELodUpdateOption::LOD0Only, /*bInUpdateFromStateDna*/ false);
		UpdateFaceFromBodyInternal(InCharacterData, ELodUpdateOption::LOD0AndLOD1Only, /*bInUpdateNeutral*/ false);
	}
	else
	{
		check(InUpdateMode == EBodyMeshUpdateMode::Full);

		UpdateBodyMeshInternal(InCharacterData, VerticesAndVertexNormals, ELodUpdateOption::All, /*bInUpdateFromStateDna*/ true);
		UpdateFaceFromBodyInternal(InCharacterData, ELodUpdateOption::All, /*bInUpdateNeutral*/ true);
		FMetaHumanCharacterSkelMeshUtils::UpdateMeshDescriptionFromLODModelVerticesNormalsAndTangents(InCharacterData->FaceMesh);
	}

	InCharacterData->OnBodyStateChangedDelegate.Broadcast();
}

void UMetaHumanCharacterEditorSubsystem::CommitBodyState(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InState, EBodyMeshUpdateMode InUpdateMode)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::CommitBodyState");

	FSharedBuffer BodyStateData;
	InState->Serialize(BodyStateData);
	
	InCharacter->SetBodyStateData(BodyStateData);
	InCharacter->MarkPackageDirty();

	// If the character has any outfit, we need to run the preview pipeline when committing body changes 
	// so that the outfit is refitted to the new body shape
	if (UMetaHumanCharacterEditorSubsystem::IsCharacterOutfitSelected(InCharacter))
	{
		ApplyBodyState(InCharacter, InState, InUpdateMode);
		RunCharacterEditorPipelineForPreview(InCharacter);
	}
	else
	{
		ApplyBodyState(InCharacter, InState, InUpdateMode);
	}
}

void UMetaHumanCharacterEditorSubsystem::CommitBodyState(UMetaHumanCharacter* InCharacter)
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CommitBodyState called with invalid character");
		return;
	}

	if (!IsObjectAddedForEditing(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "CommitBodyState called with {Character} that is not not added for editing", InCharacter->GetName());
		return;
	}

	CommitBodyState(InCharacter, GetBodyState(InCharacter));
}

FSimpleMulticastDelegate& UMetaHumanCharacterEditorSubsystem::OnBodyStateChanged(TNotNull<const UMetaHumanCharacter*> InCharacter)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	return CharacterData->OnBodyStateChangedDelegate;
}

TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> UMetaHumanCharacterEditorSubsystem::GetBodyState(TNotNull<const UMetaHumanCharacter*> InCharacter) const
{
	TSharedRef<const FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	return CharacterData->BodyState;
}

TSharedRef<FMetaHumanCharacterBodyIdentity::FState> UMetaHumanCharacterEditorSubsystem::CopyBodyState(TNotNull<const UMetaHumanCharacter*> InCharacter) const
{
	return MakeShared<FMetaHumanCharacterBodyIdentity::FState>(*GetBodyState(InCharacter));
}

void UMetaHumanCharacterEditorSubsystem::SetBodyGlobalDeltaScale(TNotNull<UMetaHumanCharacter*> InCharacter, float InBodyGlobalDelta) const
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	CharacterData->BodyState->SetGlobalDeltaScale(InBodyGlobalDelta);

	const FMetaHumanRigEvaluatedState VerticesAndVertexNormals = CharacterData->BodyState->GetVerticesAndVertexNormals();
	UpdateBodyMeshInternal(CharacterData, VerticesAndVertexNormals, ELodUpdateOption::All, /*bInUpdateFromStateDna*/ false);
	UpdateFaceFromBodyInternal(CharacterData, ELodUpdateOption::All, /*bInUpdateNeutral*/ false);
}

float UMetaHumanCharacterEditorSubsystem::GetBodyGlobalDeltaScale(TNotNull<const UMetaHumanCharacter*> InCharacter) const
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	return CharacterData->BodyState->GetGlobalDeltaScale();
}

TSharedPtr<IDNAReader> UMetaHumanCharacterEditorSubsystem::ApplyBodyDNA(TNotNull<const UMetaHumanCharacter*> InCharacter, TSharedRef<IDNAReader> InBodyDNAReader, bool bImportMesh)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	bool bDnaApplied = false;
	if (FMetaHumanCharacterSkelMeshUtils::CheckDNACompatibility(UE::MetaHuman::GetDNAReader(CharacterData->BodyMesh).Get(), InBodyDNAReader.ToSharedPtr().Get()))
	{
		if (bImportMesh)
		{
			UpdateCharacterBodyMeshFromDNA(GetTransientPackage(), InBodyDNAReader.ToSharedPtr(), CharacterData);
			bDnaApplied = true;
		}
		else
		{
			constexpr FMetaHumanCharacterSkelMeshUtils::EUpdateFlags UpdateFlags =
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::BaseMesh |
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::Joints |
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::SkinWeights |
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::DNABehavior |
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::DNAGeometry;

			FMetaHumanCharacterSkelMeshUtils::UpdateSkelMeshFromDNA(
				InBodyDNAReader,
				UpdateFlags,
				CharacterData->BodyDnaToSkelMeshMap,
				EMetaHumanCharacterOrientation::Y_UP,
				CharacterData->BodyMesh);

			bDnaApplied = true;
		}
	}

	if (bDnaApplied)
	{
		return InBodyDNAReader;
	}

	return nullptr;
}

void UMetaHumanCharacterEditorSubsystem::CommitBodyDNA(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedRef<IDNAReader> InBodyDNAReader, bool bFixedBodyType)
{
	TSharedPtr<IDNAReader> OutDna = ApplyBodyDNA(InCharacter, InBodyDNAReader, bFixedBodyType);

	if (OutDna.IsValid())
	{
		InCharacter->SetBodyDNABuffer(ReadStreamFromDNA(OutDna.Get(), EDNADataLayer::All));
		UpdateCharacterIsFixedBodyType(InCharacter, bFixedBodyType);
		InCharacter->MarkPackageDirty();
	}
}

bool UMetaHumanCharacterEditorSubsystem::ParametricFitToDnaBody(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter)
{
	if (InMetaHumanCharacter->HasBodyDNA())
	{
		TArray<uint8> DNABuffer = InMetaHumanCharacter->GetBodyDNABuffer();
		TSharedPtr<IDNAReader> DnaReader = ReadDNAFromBuffer(&DNABuffer, EDNADataLayer::All);

		TSharedPtr<IDNAReader> HeadDna(nullptr);	
		if (InMetaHumanCharacter->HasFaceDNA())
		{
			TArray<uint8> DNAFaceBuffer = InMetaHumanCharacter->GetFaceDNABuffer();
			HeadDna = ReadDNAFromBuffer(&DNAFaceBuffer, EDNADataLayer::All);
		}
		TArray<FVector3f> Vertices;
		if (GetMeshForBodyConforming(InMetaHumanCharacter, DnaReader.ToSharedRef(), HeadDna, Vertices) != EImportErrorCode::Success)
		{
			return false;
		}
		TArray<FVector3f> JointTranslations;
		TArray<FVector3f> JointRotations;
		if (GetJointsForBodyConforming(DnaReader.ToSharedRef(), JointTranslations, JointRotations) != EImportErrorCode::Success)
		{
			return false;
		}
		if (ConformBody(InMetaHumanCharacter, Vertices, JointRotations, true, false))
		{
			SetBodyJoints(InMetaHumanCharacter, JointTranslations, JointRotations, true);
			TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InMetaHumanCharacter];
			CharacterData->BodyState->SetMetaHumanBodyType(EMetaHumanBodyType::BlendableBody);
			InMetaHumanCharacter->SetBodyDNABuffer({});
			constexpr bool bImportingFromDNA = false;
			UpdateCharacterIsFixedBodyType(InMetaHumanCharacter, bImportingFromDNA);
			return true;
		}
	}

	return false;
}

bool UMetaHumanCharacterEditorSubsystem::ParametricFitToCompatibilityBody(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InMetaHumanCharacter];
	if (CharacterData->BodyState->GetMetaHumanBodyType() != EMetaHumanBodyType::BlendableBody)
	{
		const bool bFitFromCompatibilityBody = true;
		CharacterData->BodyState->SetMetaHumanBodyType(EMetaHumanBodyType::BlendableBody, bFitFromCompatibilityBody);
		constexpr bool bImportingFromDNA = false;
		UpdateCharacterIsFixedBodyType(InMetaHumanCharacter, bImportingFromDNA);
		CharacterData->OnBodyStateChangedDelegate.Broadcast();
		return true;
	}

	return false;
}

EImportErrorCode UMetaHumanCharacterEditorSubsystem::ImportFromBodyDna(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TSharedRef<class IDNAReader> InBodyDna, const FImportBodyFromDNAParams& InImportOptions)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InMetaHumanCharacter];
	EImportErrorCode ErrorCode = EImportErrorCode::Success;

	if (InImportOptions.bImportWholeRig)
	{
		if (FMetaHumanCharacterSkelMeshUtils::CheckDNACompatibility(UE::MetaHuman::GetDNAReader(CharacterData->BodyMesh).Get(), &InBodyDna.Get()))
		{
			TArray<FVector3f> Vertices;
			if (GetMeshForBodyConforming(InMetaHumanCharacter, InBodyDna, nullptr, Vertices) != EImportErrorCode::Success)
			{
				return EImportErrorCode::InvalidInputData;
			}
			TArray<FVector3f> JointTranslations;
			TArray<FVector3f> JointRotations;
			if (GetJointsForBodyConforming(InBodyDna, JointTranslations, JointRotations) != EImportErrorCode::Success)
			{
				return EImportErrorCode::InvalidInputData;
			}
			if (ConformBody(InMetaHumanCharacter, Vertices, JointRotations, true, false))
			{
				SetBodyJoints(InMetaHumanCharacter, JointTranslations, JointRotations, true);
				CommitBodyState(InMetaHumanCharacter, GetBodyState(InMetaHumanCharacter));
				constexpr bool bImportingAsFixedBodyType = true;
				CommitBodyDNA(InMetaHumanCharacter, InBodyDna, bImportingAsFixedBodyType);
			}
			else
			{
				ErrorCode = EImportErrorCode::InvalidInputData;
			}
		}
		else
		{
			FString CombinedBodyModelPath = FMetaHumanCommonDataUtils::GetArchetypeDNAPath(EMetaHumanImportDNAType::Combined);
			TSharedPtr<IDNAReader> CombinedArchetypeDnaReader = ReadDNAFromFile(CombinedBodyModelPath);
			if (FMetaHumanCharacterSkelMeshUtils::CheckDNACompatibility(CombinedArchetypeDnaReader.Get(), &InBodyDna.Get()))
			{
				ErrorCode = EImportErrorCode::CombinedBodyCannotBeImportedAsWholeRig;
			}
			else
			{
				ErrorCode = EImportErrorCode::InvalidInputData;
			}
		}

	}
	else
	{
		TArray<FVector3f> Vertices;
		if (GetMeshForBodyConforming(InMetaHumanCharacter, InBodyDna, nullptr, Vertices) != EImportErrorCode::Success)
		{
			return EImportErrorCode::InvalidInputData;
		}
		TArray<FVector3f> JointTranslations;
		TArray<FVector3f> JointRotations;
		if (GetJointsForBodyConforming(InBodyDna, JointTranslations, JointRotations) != EImportErrorCode::Success)
		{
			return EImportErrorCode::InvalidInputData;
		}
		if (ConformBody(InMetaHumanCharacter, Vertices, JointRotations, true, false))
		{
			SetBodyJoints(InMetaHumanCharacter, JointTranslations, JointRotations, true);
			TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyState = CopyBodyState(InMetaHumanCharacter);
			CommitBodyState(InMetaHumanCharacter, GetBodyState(InMetaHumanCharacter));
		}
		else
		{
			ErrorCode = EImportErrorCode::FittingError;
		}
	}

	if (ErrorCode == EImportErrorCode::Success)
	{
		UE::MetaHuman::Analytics::RecordImportBodyDNAEvent(InMetaHumanCharacter);
	}

	return ErrorCode;
}

#if WITH_EDITORONLY_DATA
EImportErrorCode UMetaHumanCharacterEditorSubsystem::ImportFromBodyTemplate(UMetaHumanCharacter* InMetaHumanCharacter, UObject* InTemplateMesh, EMetaHumanCharacterBodyFitOptions InBodyFitOptions)
{
	if (!IsValid(InMetaHumanCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "ImportFromBodyTemplate called with invalid character");
		return EImportErrorCode::InvalidInputData;
	}

	if (!IsObjectAddedForEditing(InMetaHumanCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "ImportFromBodyTemplate called with {Character} that is not not added for editing", InMetaHumanCharacter->GetName());
		return EImportErrorCode::InvalidInputData;
	}

	if (!IsValid(InTemplateMesh))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "ImportFromTemplate called with invalid template mesh");
		return EImportErrorCode::InvalidHeadMesh;
	}

	TArray<FVector3f> ConformalVertices;
	EImportErrorCode ErrorCode = UMetaHumanCharacterEditorSubsystem::GetMeshForBodyConforming(InMetaHumanCharacter, InTemplateMesh, nullptr,  false, ConformalVertices);

	if (ErrorCode != EImportErrorCode::Success)
	{
		UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Selected asset must be a SkelMesh or Static Mesh consistent with MetaHuman topology to be imported into MetaHumanCharacter asset"));
		return ErrorCode;
	}

	TArray<FVector3f> ComponentJointTranslations;
	if (InBodyFitOptions == EMetaHumanCharacterBodyFitOptions::FitFromMeshAndSkeleton)
	{
		if (USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(InTemplateMesh))
		{
			TArray<FVector3f> TemplateMeshComponentJointTranslations = FMetaHumanCharacterSkelMeshUtils::GetComponentSpaceJointTranslations(SkelMesh);

			if (UDNAAsset* ArchetypeDna = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAseet(EMetaHumanImportDNAType::Body, SkelMesh))
			{
				TArray<int32> RLJointToUEBoneIndices;
				const dna::Reader* DNAReader = ArchetypeDna->GetBehaviorReader()->Unwrap();
				UE::MetaHuman::MapJoints(SkelMesh, DNAReader, RLJointToUEBoneIndices);

				ComponentJointTranslations.AddUninitialized(DNAReader->getJointCount());
				for (uint16_t JointIndex = 0; JointIndex < DNAReader->getJointCount(); JointIndex++)
				{
					int32 BoneIndex = RLJointToUEBoneIndices[JointIndex];
					if (BoneIndex == INDEX_NONE)
					{
						FString BoneName = FString(ANSI_TO_TCHAR(DNAReader->getJointName(JointIndex).data()));
						UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Selected skel mesh must be consistent with MetaHuman topology to be imported into MetaHumanCharacter asset. Bone: %s not found in template mesh."), *BoneName);
						return EImportErrorCode::InvalidInputBones;
					}
					ComponentJointTranslations[JointIndex] = TemplateMeshComponentJointTranslations[BoneIndex];
				}
			}
		}
	}

	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyState = CopyBodyState(InMetaHumanCharacter);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	bool bFitted = BodyState->FitToTarget(ConformalVertices, ComponentJointTranslations, InBodyFitOptions);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (bFitted)
	{
		// commit the body state and update the body mesh
		CommitBodyState(InMetaHumanCharacter, BodyState);
		return EImportErrorCode::Success;
	}

	return EImportErrorCode::FittingError;
}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITORONLY_DATA
bool UMetaHumanCharacterEditorSubsystem::FitToBodyDna(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedRef<IDNAReader> InBodyDna, EMetaHumanCharacterBodyFitOptions InBodyFitOptions)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyState = CopyBodyState(InCharacter);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	bool bFitted = BodyState->FitToBodyDna(InBodyDna, InBodyFitOptions);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (bFitted)
	{
		ApplyBodyState(CharacterData, BodyState, EBodyMeshUpdateMode::Full);
		return true;
	}

	return false;
}
#endif // WITH_EDITORONLY_DATA

TArray<FMetaHumanCharacterBodyConstraint> UMetaHumanCharacterEditorSubsystem::GetBodyConstraints(const UMetaHumanCharacter* InCharacter, bool bScaleMeasurementRangesWithHeight) const
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "GetBodyConstraints called with invalid character");
		return {};
	}

	if (!IsObjectAddedForEditing(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "GetBodyConstraints called with {Character} that is not not added for editing", InCharacter->GetName());
		return {};
	}

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	return CharacterData->BodyState->GetBodyConstraints(bScaleMeasurementRangesWithHeight);
}

void UMetaHumanCharacterEditorSubsystem::SetBodyConstraints(const UMetaHumanCharacter* InCharacter, const TArray<FMetaHumanCharacterBodyConstraint>& InBodyConstraints)
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "SetBodyConstraints called with invalid character");
		return;
	}

	if (!IsObjectAddedForEditing(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "SetBodyConstraints called with {Character} that is not not added for editing", InCharacter->GetName());
		return;
	}

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	CharacterData->BodyState->EvaluateBodyConstraints(InBodyConstraints);

	// Update mesh
	const FMetaHumanRigEvaluatedState VerticesAndVertexNormals = CharacterData->BodyState->GetVerticesAndVertexNormals();
	UpdateBodyMeshInternal(CharacterData, VerticesAndVertexNormals, ELodUpdateOption::All, /*bInUpdateFromStateDna*/ false);
	UpdateFaceFromBodyInternal(CharacterData, ELodUpdateOption::All, /*bInUpdateNeutral*/ false);
}

void UMetaHumanCharacterEditorSubsystem::ResetParametricBody(TNotNull<const UMetaHumanCharacter*> InCharacter)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	CharacterData->BodyState->Reset();

	CharacterData->OnBodyStateChangedDelegate.Broadcast();
}

void UMetaHumanCharacterEditorSubsystem::SetMetaHumanBodyType(TNotNull<const UMetaHumanCharacter*> InCharacter, EMetaHumanBodyType InBodyType, EBodyMeshUpdateMode InUpdateMode)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::SetMetaHumanBodyType");

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	CharacterData->BodyState->SetMetaHumanBodyType(InBodyType);

	// Update mesh
	const FMetaHumanRigEvaluatedState VerticesAndVertexNormals = CharacterData->BodyState->GetVerticesAndVertexNormals();
	if (InUpdateMode == EBodyMeshUpdateMode::Minimal)
	{
		UpdateBodyMeshInternal(CharacterData, VerticesAndVertexNormals, ELodUpdateOption::LOD0Only, /*bInUpdateFromStateDna*/ false);
		UpdateFaceFromBodyInternal(CharacterData, ELodUpdateOption::LOD0Only, /*bInUpdateNeutral*/ false);
	}
	else
	{
		UpdateBodyMeshInternal(CharacterData, VerticesAndVertexNormals, ELodUpdateOption::All,  /*bInUpdateFromStateDna*/ true);
		UpdateFaceFromBodyInternal(CharacterData, ELodUpdateOption::All, /*bInUpdateNeutral*/ true);
	}

	CharacterData->OnBodyStateChangedDelegate.Broadcast();
}

void UMetaHumanCharacterEditorSubsystem::UpdateCharacterIsFixedBodyType(TNotNull<UMetaHumanCharacter*> InCharacter, bool bImportedFromDNA)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	bool bIsFixedCompatibilityType = CharacterData->BodyState->GetMetaHumanBodyType() != EMetaHumanBodyType::BlendableBody;			
	InCharacter->bFixedBodyType = bImportedFromDNA || bIsFixedCompatibilityType;
}

void UMetaHumanCharacterEditorSubsystem::UpdateBodyMeshInternal(
	TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, 
	const FMetaHumanRigEvaluatedState& InVerticesAndNormals, 
	ELodUpdateOption InUpdateOption, 
	bool bInUpdateFromStateDna)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::UpdateBodyMeshInternal");

	FMetaHumanCharacterSkelMeshUtils::UpdateLODModelVertexPositions(
		InCharacterData->BodyMesh,
		InVerticesAndNormals,
		*InCharacterData->BodyState,
		*InCharacterData->BodyDnaToSkelMeshMap,
		InUpdateOption,
		EVertexPositionsAndNormals::Both,
		InCharacterData->PreviewMergedHeadAndBody,
		&InCharacterData->PreviewMergedMeshMapping,
		bOutfitResizingRequiresNormals);

	const bool bRebuildTangents = true;
	USkelMeshDNAUtils::RebuildRenderData_VertexPosition(InCharacterData->BodyMesh, bRebuildTangents);

	if (bInUpdateFromStateDna)
	{
		// Get dna from state and update skel mesh
		if (UDNAAsset* BodyDNA = InCharacterData->BodyMesh->GetAssetUserData<UDNAAsset>())
		{
			TSharedRef<IDNAReader> StateDnaReader = InCharacterData->BodyState->StateToDna(BodyDNA);

			// Already updated vertex positions and don't need to rebuild skel mesh
			const FMetaHumanCharacterSkelMeshUtils::EUpdateFlags UpdateFlags =
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::Joints |
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::SkinWeights |
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::DNABehavior |
				FMetaHumanCharacterSkelMeshUtils::EUpdateFlags::DNAGeometry;

			FMetaHumanCharacterSkelMeshUtils::UpdateSkelMeshFromDNA(
				StateDnaReader,
				UpdateFlags,
				InCharacterData->BodyDnaToSkelMeshMap,
				EMetaHumanCharacterOrientation::Y_UP,
				InCharacterData->BodyMesh);
		}
		else
		{
			UE_LOG(LogMetaHumanCharacterEditor, Warning, TEXT("Unable to update body DNA. Body skeletal mesh does not contain DNA Asset User Data."));
		}
	}
	else
	{
		// Update the face joints to ensure groom physics are affected correctly
		// 
		// Mostly copied from UpdateJoints in MetaHumanCharacterSkelMeshUtils.cpp

		if (UDNAAsset* BodyDNA = InCharacterData->BodyMesh->GetAssetUserData<UDNAAsset>())
		{
			USkeletalMesh* InSkelMesh = InCharacterData->BodyMesh;
			TArray<int32> RLJointToUEBoneIndices;
			const dna::Reader* DNAReader = BodyDNA->GetBehaviorReader()->Unwrap();
			UE::MetaHuman::MapJoints(InSkelMesh, DNAReader, RLJointToUEBoneIndices);

			TArray<FTransform> RawBonePose;

			const EMetaHumanCharacterOrientation InCharacterOrientation = EMetaHumanCharacterOrientation::Y_UP;

			{	// Scoping of RefSkelModifier
				FReferenceSkeletonModifier RefSkelModifier(InSkelMesh->GetRefSkeleton(), InSkelMesh->GetSkeleton());

				// copy here
				RawBonePose = InSkelMesh->GetRefSkeleton().GetRawRefBonePose();

				// calculate component space ahead of current transform
				TArray<FTransform> ComponentTransforms;
				FAnimationRuntime::FillUpComponentSpaceTransforms(InSkelMesh->GetRefSkeleton(), RawBonePose, ComponentTransforms);

				const TArray<FMeshBoneInfo>& RawBoneInfo = InSkelMesh->GetRefSkeleton().GetRawRefBoneInfo();

				// Skipping root joint (index 0) to avoid blinking of the mesh due to bounding box issue
				for (uint16 JointIndex = 0; JointIndex < InCharacterData->BodyState->GetNumberOfJoints(); JointIndex++)
				{
					int32 BoneIndex = RLJointToUEBoneIndices[JointIndex];

					FTransform DNATransform = FTransform::Identity;

					FVector3f FloatTranslation;
					FRotator3f FloatRotation;
					InCharacterData->BodyState->GetNeutralJointTransform(JointIndex, FloatTranslation, FloatRotation);

					// Mappings from FDNAReader<TWrappedReader>::GetNeutralJointTranslation and GetNeutralJointRotation
					//
					// Would be neater to move this to FMetaHumanCharacterBodyIdentity::FState::GetNeutralJointTransform
					const FVector Translation(FloatTranslation.X, -FloatTranslation.Y, FloatTranslation.Z);
					const FRotator Rotation(-FloatRotation.Yaw, -FloatRotation.Roll, FloatRotation.Pitch);

					if (DNAReader->getJointParentIndex(JointIndex) == JointIndex) // This is the highest joint of the dna - not necessarily the UE root bone  
					{
						if (InCharacterOrientation == EMetaHumanCharacterOrientation::Y_UP)
						{
							FQuat YUpToZUpRotation = FQuat(FRotator(0, 0, 90));
							FQuat ComponentRotation = YUpToZUpRotation * FQuat(Rotation);

							DNATransform.SetTranslation(FVector(Translation.X, Translation.Z, -Translation.Y));
							DNATransform.SetRotation(ComponentRotation);
						}
						else if (InCharacterOrientation == EMetaHumanCharacterOrientation::Z_UP)
						{
							DNATransform.SetTranslation(Translation);
							DNATransform.SetRotation(Rotation.Quaternion());
						}
						else
						{
							check(false);
						}

						ComponentTransforms[BoneIndex] = DNATransform;
					}
					else
					{
						DNATransform.SetTranslation(Translation);
						DNATransform.SetRotation(Rotation.Quaternion());

						if (ensure(RawBoneInfo[BoneIndex].ParentIndex != INDEX_NONE))
						{
							ComponentTransforms[BoneIndex] = DNATransform * ComponentTransforms[RawBoneInfo[BoneIndex].ParentIndex];
						}
					}

					ComponentTransforms[BoneIndex].NormalizeRotation();
				}

				for (uint16 BoneIndex = 0; BoneIndex < RawBoneInfo.Num(); BoneIndex++)
				{
					FTransform LocalTransform;

					if (BoneIndex == 0)
					{
						LocalTransform = ComponentTransforms[BoneIndex];
					}
					else
					{
						LocalTransform = ComponentTransforms[BoneIndex].GetRelativeTransform(ComponentTransforms[RawBoneInfo[BoneIndex].ParentIndex]);
					}

					LocalTransform.NormalizeRotation();

					RefSkelModifier.UpdateRefPoseTransform(BoneIndex, LocalTransform);
				}

				// given that we revert the bones further below, we do not need to update the ref matrices
				// but keep it here for consistency in case we decide to update the body joints
				//InSkelMesh->GetRefBasesInvMatrix().Reset();
				//InSkelMesh->CalculateInvRefMatrices(); // Needs to be called after RefSkelModifier is destroyed
			}

			FMetaHumanCharacterSkelMeshUtils::UpdateBindPoseFromSource(InCharacterData->BodyMesh, InCharacterData->FaceMesh);

			// revert back the body, only the head is required for grooms
			{
				// Scoping of RefSkelModifier
				FReferenceSkeletonModifier RefSkelModifier(InSkelMesh->GetRefSkeleton(), InSkelMesh->GetSkeleton());

				for (uint16 BoneIndex = 0; BoneIndex < RawBonePose.Num(); BoneIndex++)
				{
					RefSkelModifier.UpdateRefPoseTransform(BoneIndex, RawBonePose[BoneIndex]);
				}
			}

		}
	}
}

void UMetaHumanCharacterEditorSubsystem::UpdateFaceFromBodyInternal(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, ELodUpdateOption InUpdateOption, bool bInUpdateNeutral)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::UpdateFaceFromBodyInternal");

	const FMetaHumanRigEvaluatedState VerticesAndVertexNormals = InCharacterData->BodyState->GetVerticesAndVertexNormals();
	// Update face state from body
	InCharacterData->FaceState->SetBodyJointsAndBodyFaceVertices(InCharacterData->BodyState->CopyBindPose(), VerticesAndVertexNormals.Vertices);
	// set the body vertex normals into the face state
	InCharacterData->FaceState->SetBodyVertexNormals(VerticesAndVertexNormals.VertexNormals, InCharacterData->BodyState->GetNumVerticesPerLOD());

	// Update face mesh
	const FMetaHumanRigEvaluatedState FaceVerticesAndVertexNormals = InCharacterData->FaceState->Evaluate();
	UpdateFaceMeshInternal(InCharacterData, FaceVerticesAndVertexNormals, InUpdateOption);

	// Update face mesh neutral
	if (bInUpdateNeutral)
	{
		FMetaHumanCharacterSkelMeshUtils::UpdateBindPoseFromSource(InCharacterData->BodyMesh, InCharacterData->FaceMesh);
	}
}

TArray<FVector3f> UMetaHumanCharacterEditorSubsystem::GetBodyGizmos(TNotNull<const UMetaHumanCharacter*> InCharacter) const
{
	TSharedRef<const FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	return CharacterData->BodyState->GetRegionGizmos();
}

TArray<FVector3f> UMetaHumanCharacterEditorSubsystem::BlendBodyRegion(TNotNull<const UMetaHumanCharacter*> InCharacter, int32 InRegionIndex, EBodyBlendOptions InBodyBlendOptions, const TSharedPtr<const FMetaHumanCharacterBodyIdentity::FState>& InStartState, const TArray<TSharedPtr<const FMetaHumanCharacterBodyIdentity::FState>>& InPresetStates, TConstArrayView<float> InPresetWeights)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::BlendBodyRegion");

	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	if (InPresetStates.Num() <= InPresetWeights.Num() && InPresetStates.Num() > 0)
	{
		TArray<TPair<float, const FMetaHumanCharacterBodyIdentity::FState*>> PresetStateWeights;
		for (int32 PresetIndex = 0; PresetIndex < InPresetStates.Num(); PresetIndex++)
		{
			if (InPresetStates[PresetIndex].Get())
			{
				PresetStateWeights.Add({ InPresetWeights[PresetIndex], InPresetStates[PresetIndex].Get() });
			}
		}

		CharacterData->BodyState = MakeShared<FMetaHumanCharacterBodyIdentity::FState>(*InStartState);
		CharacterData->BodyState->BlendPresets(InRegionIndex, PresetStateWeights, InBodyBlendOptions);

		const FMetaHumanRigEvaluatedState BodyVerticesAndVertexNormals = CharacterData->BodyState->GetVerticesAndVertexNormals();

		UpdateBodyMeshInternal(CharacterData, BodyVerticesAndVertexNormals, ELodUpdateOption::LOD0Only, /*bInUpdateFromStateDna*/ false);
		UpdateFaceFromBodyInternal(CharacterData, ELodUpdateOption::LOD0Only, /*bInUpdateNeutral*/ false);
	}

	return CharacterData->BodyState->GetRegionGizmos();
}

TNotNull<const USkeletalMesh*> UMetaHumanCharacterEditorSubsystem::Debug_GetFaceEditMesh(TNotNull<const UMetaHumanCharacter*> InCharacter) const
{
	TSharedRef<const FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	return CharacterData->FaceMesh;
}

TNotNull<const USkeletalMesh*> UMetaHumanCharacterEditorSubsystem::Debug_GetBodyEditMesh(TNotNull<const UMetaHumanCharacter*> InCharacter) const
{
	TSharedRef<const FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];
	return CharacterData->BodyMesh;
}

TObjectPtr<UPhysicsAsset> UMetaHumanCharacterEditorSubsystem::CreatePhysicsAssetForCharacter(TNotNull<const UMetaHumanCharacter*> InCharacter,
																							 TNotNull<UObject*> InOuter,
																							 TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InBodyState)
{
	const FString CandidateName = FString::Format(TEXT("{0}_Physics"), { InCharacter->GetName() });
	const FName AssetName = MakeUniqueObjectName(InOuter, UPhysicsAsset::StaticClass(), FName{ CandidateName }, EUniqueObjectNameOptions::GloballyUnique);

	TObjectPtr<UPhysicsAsset> PhysicsArchetype = FMetaHumanCharacterSkelMeshUtils::GetBodyArchetypePhysicsAsset(InCharacter->TemplateType);
	TObjectPtr<UPhysicsAsset> PhysicsAsset = DuplicateObject(PhysicsArchetype, InOuter, AssetName);

	UpdatePhysicsAssetFromBodyState(PhysicsAsset, InBodyState);
	return PhysicsAsset;
}

void UMetaHumanCharacterEditorSubsystem::UpdatePhysicsAssetFromBodyState(TNotNull<UPhysicsAsset*> InPhysicsAsset, TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> InBodyState)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("UMetaHumanCharacterEditorSubsystem::UpdatePhysicsAssetFromBodyState");

	// Update collision shapes
	for (TObjectPtr<USkeletalBodySetup> BodySetup : InPhysicsAsset->SkeletalBodySetups)
	{
		TArray<PhysicsBodyVolume> PhysicsBodyVolumes = InBodyState->GetPhysicsBodyVolumes(BodySetup->BoneName);
		
		for (int32 PhysicsBodyIndex = 0; PhysicsBodyIndex < PhysicsBodyVolumes.Num(); PhysicsBodyIndex++)
		{
			if (PhysicsBodyIndex < BodySetup->AggGeom.SphylElems.Num())
			{
				FKSphylElem& SphylElem = BodySetup->AggGeom.SphylElems[PhysicsBodyIndex];
				FTransform BodyTransform = SphylElem.GetTransform();
				BodyTransform.SetTranslation(PhysicsBodyVolumes[PhysicsBodyIndex].Center);
				SphylElem.SetTransform(BodyTransform);

				FVector BoxExtentsVector = PhysicsBodyVolumes[PhysicsBodyIndex].Extent;
				
				// Use rotation of archetype capsule to determine dominant axis
				FVector AxisZ = BodyTransform.GetRotation().GetAxisZ().GetAbs();
				if (AxisZ.Z > AxisZ.Y && AxisZ.Z > AxisZ.X)
				{
					SphylElem.Radius = FMath::Max(FMath::Abs(BoxExtentsVector[0]), FMath::Abs(BoxExtentsVector[1])) * 0.5;
					SphylElem.Length = FMath::Max(FMath::Abs(BoxExtentsVector[2]) - (2 * SphylElem.Radius), 0);
				}
				else if (AxisZ.Y > AxisZ.X && AxisZ.Y > AxisZ.Z)
				{
					SphylElem.Radius = FMath::Max(FMath::Abs(BoxExtentsVector[0]), FMath::Abs(BoxExtentsVector[2])) * 0.5;
					SphylElem.Length = FMath::Max(FMath::Abs(BoxExtentsVector[1]) - (2 * SphylElem.Radius), 0);
				}
				else
				{
					SphylElem.Radius = FMath::Max(FMath::Abs(BoxExtentsVector[1]), FMath::Abs(BoxExtentsVector[2])) * 0.5;
					SphylElem.Length = FMath::Max(FMath::Abs(BoxExtentsVector[0]) - (2 * SphylElem.Radius), 0);
				}
			}
			else if (PhysicsBodyIndex < BodySetup->AggGeom.BoxElems.Num())
			{
				FKBoxElem& BoxElem = BodySetup->AggGeom.BoxElems[PhysicsBodyIndex];
				FTransform BodyTransform;
				BodyTransform.SetTranslation(PhysicsBodyVolumes[PhysicsBodyIndex].Center);
				BoxElem.SetTransform(BodyTransform);

				BoxElem.X = PhysicsBodyVolumes[PhysicsBodyIndex].Extent.X;
				BoxElem.Y = PhysicsBodyVolumes[PhysicsBodyIndex].Extent.Y;
				BoxElem.Z = PhysicsBodyVolumes[PhysicsBodyIndex].Extent.Z;
			}
		}
	}

	// Update constraint positions
	for (UPhysicsConstraintTemplate* ConstraintSetup : InPhysicsAsset->ConstraintSetup)
	{
		bool bIsUserConstraint = (ConstraintSetup->DefaultInstance.JointName == TEXT("UserConstraint")) || (ConstraintSetup->DefaultInstance.JointName == TEXT("UserConstraint_0"));
		if (!bIsUserConstraint)
		{
			ConstraintSetup->Modify();
			ConstraintSetup->DefaultInstance.SnapTransformsToDefault(EConstraintTransformComponentFlags::AllPosition, InPhysicsAsset);
		}
	}
}

void UMetaHumanCharacterEditorSubsystem::SetClothingVisibilityState(TNotNull<UMetaHumanCharacter*> InCharacter, EMetaHumanClothingVisibilityState InState, bool bUpdateMaterialHiddenFaces)
{
	ForEachCharacterActor(InCharacter,
		[InState](TScriptInterface<IMetaHumanCharacterEditorActorInterface> Actor)
		{
			Actor->SetClothingVisibilityState(InState);
		});

	CharacterDataMap[InCharacter]->bClothingVisible = (InState == EMetaHumanClothingVisibilityState::Shown);
	

	if (bUpdateMaterialHiddenFaces)
	{
		UpdateCharacterPreviewMaterialHiddenFacesMask(InCharacter);
	}
}

EMetaHumanClothingVisibilityState UMetaHumanCharacterEditorSubsystem::GetClothingVisibilityState(TNotNull<UMetaHumanCharacter*> InCharacter)
{
	return CharacterDataMap[InCharacter]->bClothingVisible ? EMetaHumanClothingVisibilityState::Shown : EMetaHumanClothingVisibilityState::Hidden;
}

void UMetaHumanCharacterEditorSubsystem::OnCharacterInstanceUpdated(TNotNull<const UMetaHumanCharacter*> InCharacter)
{
	using namespace UE::MetaHuman::GeometryRemoval;

	check(CharacterDataMap.Contains(InCharacter));
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	const FMetaHumanDefaultAssemblyOutput* AssemblyStruct = InCharacter->GetInternalCollection()->GetDefaultInstance()->GetAssemblyOutput().GetPtr<FMetaHumanDefaultAssemblyOutput>();

	TArray<FHiddenFaceMapTexture> HeadHiddenFaceMaps;
	TArray<FHiddenFaceMapTexture> BodyHiddenFaceMaps;
	{
		for (const FMetaHumanOutfitPipelineAssemblyOutput& ClothOutput : AssemblyStruct->ClothData)
		{
			if (ClothOutput.HeadHiddenFaceMap.Texture)
			{
				HeadHiddenFaceMaps.Add(ClothOutput.HeadHiddenFaceMap);
			}

			if (ClothOutput.BodyHiddenFaceMap.Texture)
			{
				BodyHiddenFaceMaps.Add(ClothOutput.BodyHiddenFaceMap);
			}
		}

		for (const FMetaHumanSkeletalMeshPipelineAssemblyOutput& SkelMeshOutput : AssemblyStruct->SkeletalMeshData)
		{
			if (SkelMeshOutput.HeadHiddenFaceMap.Texture)
			{
				HeadHiddenFaceMaps.Add(SkelMeshOutput.HeadHiddenFaceMap);
			}

			if (SkelMeshOutput.BodyHiddenFaceMap.Texture)
			{
				BodyHiddenFaceMaps.Add(SkelMeshOutput.BodyHiddenFaceMap);
			}
		}
	}

	auto BuildHiddenFaceMask = [InCharacter, OuterForTempTexture = this]
		(const TArray<FHiddenFaceMapTexture>& SourceHiddenFaceMaps, TObjectPtr<UTexture2D>& InOutTempCombinedHiddenFaceMap) -> FHiddenFaceMapTexture
	{
		if (SourceHiddenFaceMaps.Num() == 0)
		{
			// No hidden face map
			return FHiddenFaceMapTexture();
		}

		if (SourceHiddenFaceMaps.Num() == 1)
		{
			return SourceHiddenFaceMaps[0];
		}

		// Need to combine multiple maps into one

		if (!InOutTempCombinedHiddenFaceMap)
		{
			InOutTempCombinedHiddenFaceMap = NewObject<UTexture2D>(OuterForTempTexture, NAME_None, RF_Transient);
		}

		TArray<FHiddenFaceMapImage> Images;
		FText FailureReason;
		if (!TryConvertHiddenFaceMapTexturesToImages(SourceHiddenFaceMaps, Images, FailureReason))
		{
			UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to convert hidden face maps to images for {Character}: {Reason}", InCharacter->GetName(), FailureReason.ToString());
			return FHiddenFaceMapTexture();
		}

		FHiddenFaceMapImage CombinedImage;
		if (TryCombineHiddenFaceMaps(Images, CombinedImage, FailureReason))
		{
			UpdateHiddenFaceMapTextureFromImage(CombinedImage.Image, InOutTempCombinedHiddenFaceMap);

			FHiddenFaceMapTexture Result;
			Result.Texture = InOutTempCombinedHiddenFaceMap;
			Result.Settings = CombinedImage.Settings;
			return Result;
		}
		else
		{
			UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to combine hidden face maps for {Character}: {Reason}", InCharacter->GetName(), FailureReason.ToString());
			return FHiddenFaceMapTexture();
		}
	};
	CharacterData->HeadHiddenFaceMap = BuildHiddenFaceMask(HeadHiddenFaceMaps, CharacterData->TempCombinedHeadHiddenFaceMap);
	CharacterData->BodyHiddenFaceMap = BuildHiddenFaceMask(BodyHiddenFaceMaps, CharacterData->TempCombinedBodyHiddenFaceMap);

	UpdateCharacterPreviewMaterialHiddenFacesMask(InCharacter);
}

void UMetaHumanCharacterEditorSubsystem::UpdateCharacterPreviewMaterialHiddenFacesMask(TNotNull<const UMetaHumanCharacter*> InCharacter) const
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

	const bool bApplyHiddenFaces = UE::MetaHuman::CVarMHCharacterPreviewHiddenFaces.GetValueOnAnyThread() && CharacterData->bClothingVisible;

	const UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture& HeadHiddenFaceMap = CharacterData->HeadHiddenFaceMap;

	CharacterData->HeadMaterials.ForEachSkinMaterial<UMaterialInstanceDynamic>(
		[bApplyHiddenFaces, &HeadHiddenFaceMap](EMetaHumanCharacterSkinMaterialSlot, UMaterialInstanceDynamic* Material)
		{
			if (bApplyHiddenFaces && HeadHiddenFaceMap.Texture)
			{
				FMetaHumanCharacterBodyTextureUtils::SetMaterialHiddenFacesTexture(Material, HeadHiddenFaceMap);
			}
			else
			{
				FMetaHumanCharacterBodyTextureUtils::SetMaterialHiddenFacesTextureNoOp(Material);
			}
		});

	const UE::MetaHuman::GeometryRemoval::FHiddenFaceMapTexture& BodyHiddenFaceMap = CharacterData->BodyHiddenFaceMap;
	if (bApplyHiddenFaces && BodyHiddenFaceMap.Texture)
	{
		FMetaHumanCharacterBodyTextureUtils::SetMaterialHiddenFacesTexture(CharacterData->BodyMaterial, BodyHiddenFaceMap);
	}
	else
	{
		FMetaHumanCharacterBodyTextureUtils::SetMaterialHiddenFacesTextureNoOp(CharacterData->BodyMaterial);
	}
}

TMap<FString, float> UMetaHumanCharacterEditorSubsystem::GetFaceAndBodyMeasurements(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, bool bGetMeasuresFromDNA) const
{
	TMap<FString, float> BodyMeasurements;

	TSharedPtr<IDNAReader> FaceDNAReader = UE::MetaHuman::GetDNAReader(InCharacterData->FaceMesh);
	TSharedPtr<IDNAReader> BodyDNAReader = UE::MetaHuman::GetDNAReader(InCharacterData->BodyMesh);
	if (bGetMeasuresFromDNA && FaceDNAReader && BodyDNAReader)
	{
		InCharacterData->BodyState->GetMeasurementsForFaceAndBody(
			FaceDNAReader.ToSharedRef(),
			BodyDNAReader.ToSharedRef(),
			BodyMeasurements);
	}
	else
	{
		TArray<FVector3f> FaceRawVertices;
		if (TSharedPtr<IDNAReader> ArchetypeDNAReader = UMetaHumanIdentityFace::GetPluginArchetypeDNAReader())
		{
			constexpr uint16 DNAHeadMeshIndex = 0;
			const int32 NumHeadMeshVertices = ArchetypeDNAReader->GetVertexPositionCount(DNAHeadMeshIndex);
			FaceRawVertices.Reserve(NumHeadMeshVertices);
			FMetaHumanRigEvaluatedState VerticesAndNormals = InCharacterData->FaceState->Evaluate();
			for (int32 VertexIndex = 0; VertexIndex < NumHeadMeshVertices; VertexIndex++)
			{
				FaceRawVertices.Add(InCharacterData->FaceState->GetRawVertex(VerticesAndNormals.Vertices, DNAHeadMeshIndex, VertexIndex));
			}
		}
		InCharacterData->BodyState->GetMeasurementsForFaceAndBody(
			FaceRawVertices,
			BodyMeasurements);
	}
	return BodyMeasurements;
}

void UMetaHumanCharacterEditorSubsystem::ForEachCharacterActor(TSharedRef<FMetaHumanCharacterEditorData> InCharacterData, TFunction<void(TScriptInterface<IMetaHumanCharacterEditorActorInterface>)> InFunc)
{
	for (TWeakInterfacePtr<IMetaHumanCharacterEditorActorInterface> CharacterActor : InCharacterData->CharacterActorList)
	{
		if (CharacterActor.IsValid())
		{
			InFunc(CharacterActor.ToScriptInterface());
		}
	}
}

ELodUpdateOption UMetaHumanCharacterEditorSubsystem::GetUpdateOptionForEditing()
{
	ELodUpdateOption UpdateOption = ELodUpdateOption::LOD0Only;
	if (UE::MetaHuman::CvarUpdateAllLODsOnFaceEdit.GetValueOnAnyThread())
	{
		UpdateOption = ELodUpdateOption::All;
	}

	return UpdateOption;
}

void UMetaHumanCharacterEditorSubsystem::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	UMetaHumanCharacterEditorSubsystem* This = CastChecked<UMetaHumanCharacterEditorSubsystem>(InThis);

	for (TPair<TObjectKey<UMetaHumanCharacter>, TSharedRef<FMetaHumanCharacterEditorData>>& Pair : This->CharacterDataMap)
	{
		Collector.AddPropertyReferences(FMetaHumanCharacterEditorData::StaticStruct(), &Pair.Value.Get(), This);
	}
}

void UMetaHumanCharacterEditorSubsystem::ForEachCharacterActor(TNotNull<const UMetaHumanCharacter*> InCharacter, TFunction<void(TScriptInterface<class IMetaHumanCharacterEditorActorInterface>)> InFunc)
{
	check(CharacterDataMap.Contains(InCharacter));
	TSharedRef<FMetaHumanCharacterEditorData> InCharacterData = CharacterDataMap[InCharacter];
	for (TWeakInterfacePtr<IMetaHumanCharacterEditorActorInterface> CharacterActor : InCharacterData->CharacterActorList)
	{
		if (CharacterActor.IsValid())
		{
			InFunc(CharacterActor.ToScriptInterface());
		}
	}
}

FOnNotifyLightingEnvironmentChanged& UMetaHumanCharacterEditorSubsystem::OnLightEnvironmentChanged(TNotNull<const UMetaHumanCharacter*> InCharacter)
{
	check(CharacterDataMap.Contains(InCharacter));
	return CharacterDataMap[InCharacter]->NotifyLightingEnvironmentChangedDelegate;
}

FOnNotifyLightingEnvironmentChanged& UMetaHumanCharacterEditorSubsystem::OnLightTonemapperChanged(TNotNull<const UMetaHumanCharacter*> InCharacter)
{
	check(CharacterDataMap.Contains(InCharacter));
	return CharacterDataMap[InCharacter]->NotifyLightingEnvironmentChangedDelegate;
}

FOnNotifyLightingEnvironmentChanged& UMetaHumanCharacterEditorSubsystem::OnNotifyLightingEnvironmentChanged(TNotNull<const UMetaHumanCharacter*> InCharacter)
{
	check(CharacterDataMap.Contains(InCharacter));
	return CharacterDataMap[InCharacter]->NotifyLightingEnvironmentChangedDelegate;
}

FOnStudioLightRotationChanged& UMetaHumanCharacterEditorSubsystem::OnLightRotationChanged(TNotNull<const UMetaHumanCharacter*> InCharacter)
{
	check(CharacterDataMap.Contains(InCharacter));
	return CharacterDataMap[InCharacter]->EnvironmentLightRotationChangedDelegate;
}

FOnStudioBackgroundColorChanged& UMetaHumanCharacterEditorSubsystem::OnBackgroundColorChanged(TNotNull<const UMetaHumanCharacter*> InCharacter)
{
	check(CharacterDataMap.Contains(InCharacter));
	return CharacterDataMap[InCharacter]->EnvironmentBackgroundColorChangedDelegate;
}

void UMetaHumanCharacterEditorSubsystem::UpdateLightingEnvironment(TNotNull<UMetaHumanCharacter*> InCharacter, EMetaHumanCharacterEnvironment InLightingEnvironment) const
{
	check(CharacterDataMap.Contains(InCharacter));

	InCharacter->ViewportSettings.CharacterEnvironment = InLightingEnvironment;
	InCharacter->MarkPackageDirty();
	CharacterDataMap[InCharacter]->NotifyLightingEnvironmentChangedDelegate.ExecuteIfBound();
}

void UMetaHumanCharacterEditorSubsystem::UpdateTonemapperOption(TNotNull<UMetaHumanCharacter*> InCharacter, bool bInToneMapperEnabled) const
{
	check(CharacterDataMap.Contains(InCharacter));

	InCharacter->ViewportSettings.bTonemapperEnabled = bInToneMapperEnabled;
	InCharacter->MarkPackageDirty();
	CharacterDataMap[InCharacter]->NotifyLightingEnvironmentChangedDelegate.ExecuteIfBound();
}

void UMetaHumanCharacterEditorSubsystem::NotifyLightingEnvironmentChanged(TNotNull<UMetaHumanCharacter*> InCharacter) const
{
	check(CharacterDataMap.Contains(InCharacter));

	InCharacter->MarkPackageDirty();
	CharacterDataMap[InCharacter]->NotifyLightingEnvironmentChangedDelegate.ExecuteIfBound();
}

void UMetaHumanCharacterEditorSubsystem::UpdateLightRotation(TNotNull<UMetaHumanCharacter*> InCharacter, float InRotation) const
{
	check(CharacterDataMap.Contains(InCharacter));

	InCharacter->ViewportSettings.LightRotation = InRotation;
	InCharacter->MarkPackageDirty();
	CharacterDataMap[InCharacter]->EnvironmentLightRotationChangedDelegate.ExecuteIfBound(InRotation);
}

void UMetaHumanCharacterEditorSubsystem::UpdateBackgroundColor(TNotNull<UMetaHumanCharacter*> InCharacter, const FLinearColor& InBackgroundColor) const
{
	check(CharacterDataMap.Contains(InCharacter));

	InCharacter->ViewportSettings.BackgroundColor = InBackgroundColor;
	InCharacter->MarkPackageDirty();
	CharacterDataMap[InCharacter]->EnvironmentBackgroundColorChangedDelegate.ExecuteIfBound(InBackgroundColor);
}

void UMetaHumanCharacterEditorSubsystem::UpdateCharacterLOD(TNotNull<UMetaHumanCharacter*> InCharacter, const EMetaHumanCharacterLOD NewLODValue) const
{
	if (InCharacter->ViewportSettings.LevelOfDetail != NewLODValue)
	{
		InCharacter->ViewportSettings.LevelOfDetail = NewLODValue;
		InCharacter->MarkPackageDirty();
	}

	ForEachCharacterActor(CharacterDataMap[InCharacter], [this, NewLODValue](TScriptInterface<IMetaHumanCharacterEditorActorInterface> MetaHumanCharacterActor)
		{
			if (NewLODValue == EMetaHumanCharacterLOD::Auto)
			{
				MetaHumanCharacterActor->SetForcedLOD(-1);
			}
			else
			{
				MetaHumanCharacterActor->SetForcedLOD((int32)NewLODValue);
			}
		});
}

void UMetaHumanCharacterEditorSubsystem::UpdateAlwaysUseHairCardsOption(TNotNull<UMetaHumanCharacter*> InCharacter, bool bInAlwaysUseHairCards) const
{
	ForEachCharacterActor(CharacterDataMap[InCharacter], [this, bInAlwaysUseHairCards](TScriptInterface<IMetaHumanCharacterEditorActorInterface> MetaHumanCharacterActor)
		{
			// Update groom settings on each groom component for each actor.			
			AActor* CharacterActor = Cast<AActor>(MetaHumanCharacterActor.GetObject());
			TArray<UGroomComponent*> GroomComponents;
			CharacterActor->GetComponents(UGroomComponent::StaticClass(), GroomComponents);
			for (UGroomComponent* GroomComponent : GroomComponents)
			{
				GroomComponent->SetUseCards(bInAlwaysUseHairCards);
			}
		});
}

bool UMetaHumanCharacterEditorSubsystem::FitToFaceDna(TNotNull<UMetaHumanCharacter*> InCharacter, TSharedRef<IDNAReader> InFaceDna, const FFitToTargetOptions& InFitToTargetOptions)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];


	TSharedRef<FMetaHumanCharacterIdentity::FState> FaceState = CopyFaceState(InCharacter);

	const bool bFitted = FaceState->FitToFaceDna(InFaceDna, InFitToTargetOptions);

	// apply the face state and update the body from the face
	if (bFitted)
	{
		ApplyFaceState(CharacterData, FaceState);
	}

	return bFitted;
}


bool UMetaHumanCharacterEditorSubsystem::FitStateToTargetVertices(TNotNull<UMetaHumanCharacter*> InCharacter, const TMap<EHeadFitToTargetMeshes, TArray<FVector3f>>& InTargetVertices, const FFitToTargetOptions& InFitToTargetOptions)
{
	if (!InTargetVertices.Find(EHeadFitToTargetMeshes::Head))
	{
		UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Selected identity must contain a conformed head mesh in order to be imported into MetaHumanCharacter asset"));
		return false;
	}
	else
	{
		TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InCharacter];

		const TMap<EHeadFitToTargetMeshes, int32> PartMeshMapping =
		{
			{ EHeadFitToTargetMeshes::Head, 0 },
			{ EHeadFitToTargetMeshes::LeftEye, 3 },
			{ EHeadFitToTargetMeshes::RightEye, 4 },
			{ EHeadFitToTargetMeshes::Teeth, 1 }
		};

		TMap<int32, TArray<FVector3f>> TargetMeshVertices;
		
		for (const TPair< EHeadFitToTargetMeshes, TArray<FVector3f>>& PartMesh : InTargetVertices)
		{
			TargetMeshVertices.Add(PartMeshMapping[PartMesh.Key], PartMesh.Value);
		}

		TSharedRef<FMetaHumanCharacterIdentity::FState> FaceState = CopyFaceState(InCharacter);

		const bool bFitted = FaceState->FitToTarget(TargetMeshVertices, InFitToTargetOptions);

		if (bFitted)
		{
			// apply the face state and update the body from the face
			ApplyFaceState(CharacterData, FaceState);
		}
		else
		{
			return false;
		}
	}
	
	return true;
}

bool UMetaHumanCharacterEditorSubsystem::FitStateToTargetVertices(UMetaHumanCharacter* InCharacter, const FMetaHumanCharacterFitToVerticesParams& InParams)
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "FitStateToTargetVertices called with invalid character");
		return false;
	}

	if (!IsObjectAddedForEditing(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "FitStateToTargetVertices called with {Character} that is not not added for editing", InCharacter->GetName());
		return false;
	}

	TMap<EHeadFitToTargetMeshes, TArray<FVector3f>> TargetVerticesMap;

	auto SetTargetVertices = [&TargetVerticesMap](EHeadFitToTargetMeshes TargetMesh, const TArray<FVector>& Vertices)
	{
		if (!Vertices.IsEmpty())
		{
			TArray<FVector3f>& TargetVertices = TargetVerticesMap.FindOrAdd(TargetMesh);
			Algo::Transform(Vertices, TargetVertices, [](const FVector& Vector)
							{
								return FVector3f(Vector);
							});
		}
	};

	SetTargetVertices(EHeadFitToTargetMeshes::Head, InParams.HeadVertices);
	SetTargetVertices(EHeadFitToTargetMeshes::LeftEye, InParams.LeftEyeVertices);
	SetTargetVertices(EHeadFitToTargetMeshes::RightEye, InParams.RightEyeVertices);
	SetTargetVertices(EHeadFitToTargetMeshes::Teeth, InParams.TeethVertices);

	return FitStateToTargetVertices(InCharacter, TargetVerticesMap, InParams.Options);
}


EImportErrorCode UMetaHumanCharacterEditorSubsystem::ImportFromFaceDna(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TSharedRef<class IDNAReader> InFaceDna, const FImportFromDNAParams& InImportParams)
{
	bool bSuccess = true;

	// first check that the dna is consistent with MH head
	if (TSharedPtr<IDNAReader> ArchetypeDnaReader = UMetaHumanIdentityFace::GetPluginArchetypeDNAReader())
	{
		FString OutCompatibilityMsg;
		if (!FDNAUtilities::CheckCompatibility(ArchetypeDnaReader.Get(), &InFaceDna.Get(), EDNARigCompatiblityFlags::All, OutCompatibilityMsg))
		{
			return EImportErrorCode::InvalidInputData;
		}
	}


	if (InImportParams.bImportWholeRig)
	{
		FFitToTargetOptions FitToTargetOptions{ EAlignmentOptions::None, /*bDisableHighFrequencyDelta*/ true };

		const bool bFitted = FitToFaceDna(InMetaHumanCharacter, InFaceDna, FitToTargetOptions);

		if (bFitted)
		{
			CommitFaceState(InMetaHumanCharacter, GetFaceState(InMetaHumanCharacter));
			CommitFaceDNA(InMetaHumanCharacter, InFaceDna);
		}
		else
		{
			bSuccess = false;
		}
	}
	else
	{
		FFitToTargetOptions FitToTargetOptions{ InImportParams.AlignmentOptions, /*bDisableHighFrequencyDelta*/ true };

		const bool bFitted = FitToFaceDna(InMetaHumanCharacter, InFaceDna, FitToTargetOptions);

		if (bFitted)
		{
			CommitFaceState(InMetaHumanCharacter, GetFaceState(InMetaHumanCharacter));
		}
		else
		{
			bSuccess = false;
		}
	}

	if (bSuccess)
	{
		UE::MetaHuman::Analytics::RecordImportFaceDNAEvent(InMetaHumanCharacter);

		return EImportErrorCode::Success;
	}
	return EImportErrorCode::FittingError;
}

EImportErrorCode UMetaHumanCharacterEditorSubsystem::ImportFromFaceDna(UMetaHumanCharacter* InCharacter, const FString& InDNAFilePath, const FImportFromDNAParams& InImportParams)
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "ImportFromFaceDna called with invalid character");
		return EImportErrorCode::GeneralError;
	}

	if (!IsObjectAddedForEditing(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "ImportFromFaceDna called with {Character} that is not not added for editing", InCharacter->GetName());
		return EImportErrorCode::GeneralError;
	}

	if (!FPaths::FileExists(InDNAFilePath))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "ImportFromFaceDna called for {Character} with DNA file not found '{DNAFile}'", InCharacter->GetName(), InDNAFilePath);
		return EImportErrorCode::GeneralError;
	}

	TSharedPtr<IDNAReader> DNAReader = ReadDNAFromFile(InDNAFilePath);

	if (!DNAReader.IsValid())
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "ImportFromFaceDNA called for {Character} but failed to read DNA file '{DNAFile}'", InCharacter->GetName(), InDNAFilePath);
		return EImportErrorCode::GeneralError;
	}

	return ImportFromFaceDna(InCharacter, DNAReader.ToSharedRef(), InImportParams);
}

EImportErrorCode UMetaHumanCharacterEditorSubsystem::ImportFromIdentity(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TNotNull<const UMetaHumanIdentity*> InMetaHumanIdentity, const FImportFromIdentityParams& InImportParams)
{
	if (UMetaHumanIdentityFace* FacePart = InMetaHumanIdentity->FindPartOfClass<UMetaHumanIdentityFace>())
	{
		// Fit to the conformed head mesh of the Identity asset
		if (FacePart->IsConformalRigValid())
		{

			// Fit to face the head lod0 of the conformed mesh
			const TMap<EIdentityPartMeshes, TArray<FVector>> ConformalVertices = FacePart->GetConformalVerticesWorldPos(EIdentityPoseType::Neutral);
			if (!ConformalVertices.Find(EIdentityPartMeshes::Head))
			{
				UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Selected identity must contain a conformed head mesh in order to be imported into MetaHumanCharacter asset"));
				return EImportErrorCode::NoHeadMeshPresent;
			}
			else
			{
				auto ConvertArrayToFVector3f = [] (const TArray<FVector>& InVectorArray) -> TArray<FVector3f>
				{
					TArray<FVector3f> OutVectorArray;
					OutVectorArray.AddUninitialized(InVectorArray.Num());
					for (int32 I = 0; I < InVectorArray.Num(); I++)
					{
						OutVectorArray[I] = FVector3f(InVectorArray[I][0], InVectorArray[I][1], InVectorArray[I][2]);
					}
					return OutVectorArray;
				};
				
				TMap<EHeadFitToTargetMeshes, TArray<FVector3f>> ConformalVerticesToUse;
				ConformalVerticesToUse.Add(EHeadFitToTargetMeshes::Head, ConvertArrayToFVector3f(ConformalVertices[EIdentityPartMeshes::Head]));

				if (InImportParams.bUseEyeMeshes)
				{
					if (!ConformalVertices.Find(EIdentityPartMeshes::LeftEye) || !ConformalVertices.Find(EIdentityPartMeshes::RightEye))
					{
						UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Selected identity must contain conformed eye meshes in order to be imported into MetaHumanCharacter asset with the eye meshes option selected"));
						return EImportErrorCode::NoEyeMeshesPresent;
					}
					else
					{
						ConformalVerticesToUse.Add(EHeadFitToTargetMeshes::LeftEye, ConvertArrayToFVector3f(ConformalVertices[EIdentityPartMeshes::LeftEye]));
						ConformalVerticesToUse.Add(EHeadFitToTargetMeshes::RightEye, ConvertArrayToFVector3f(ConformalVertices[EIdentityPartMeshes::RightEye]));
					}
				}

				if (InImportParams.bUseTeethMesh)
				{
					if (!ConformalVertices.Find(EIdentityPartMeshes::Teeth))
					{
						UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Selected identity must contain conformed teeth mesh in order to be imported into MetaHumanCharacter asset with the eye meshes option selected"));
						return EImportErrorCode::NoTeethMeshPresent;
					}
					else
					{
						ConformalVerticesToUse.Add(EHeadFitToTargetMeshes::Teeth, ConvertArrayToFVector3f(ConformalVertices[EIdentityPartMeshes::Teeth]));
					}
				}

				FFitToTargetOptions FitToTargetOptions{ EAlignmentOptions::ScalingRotationTranslation, /*bDisableHighFrequencyDelta*/ true };

				if (InImportParams.bUseMetricScale)
				{
					FitToTargetOptions.AlignmentOptions = EAlignmentOptions::RotationTranslation;
				}

				const bool bFitted = FitStateToTargetVertices(InMetaHumanCharacter, ConformalVerticesToUse, FitToTargetOptions);

				if (bFitted)
				{
					CommitFaceState(InMetaHumanCharacter, GetFaceState(InMetaHumanCharacter));
					return EImportErrorCode::Success;
				}
				else
				{
					return	EImportErrorCode::FittingError;
				}
			}
		}
		else
		{
			UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Identity has not been conformed."));
			return	EImportErrorCode::IdentityNotConformed;
		}
	}

	return EImportErrorCode::IdentityNotConformed;
}

EImportErrorCode UMetaHumanCharacterEditorSubsystem::ImportFromTemplate(UMetaHumanCharacter* InCharacter, UObject* InTemplateMesh, UObject* InTemplateLeftEyeMesh, UObject* InTemplateRightEyeMesh, UObject* InTemplateTeethMesh,
	const FImportFromTemplateParams& InImportParams)
{
	if (!IsValid(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "ImportFromTemplate called with invalid character");
		return EImportErrorCode::InvalidInputData;
	}

	if (!IsObjectAddedForEditing(InCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "ImportFromTemplate called with {Character} that is not not added for editing", InCharacter->GetName());
		return EImportErrorCode::InvalidInputData;
	}

	if (!IsValid(InTemplateMesh))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "ImportFromTemplate called with invalid template mesh");
		return EImportErrorCode::InvalidHeadMesh;
	}

	TMap<EHeadFitToTargetMeshes, TArray<FVector3f>> ConformalVertices;
	EImportErrorCode ErrorCode = GetDataForConforming(InCharacter, InTemplateMesh, InTemplateLeftEyeMesh, InTemplateRightEyeMesh, InTemplateTeethMesh, InImportParams, ConformalVertices);

	if (ErrorCode != EImportErrorCode::Success)
	{
		UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Selected asset must be a SkelMesh or Static Mesh consistent with MetaHuman topology to be imported into MetaHumanCharacter asset"));
		return ErrorCode;
	}

	FFitToTargetOptions FitToTargetOptions{ InImportParams.AlignmentOptions, /*bDisableHighFrequencyDelta*/ true};

	const bool bFitted = FitStateToTargetVertices(InCharacter, ConformalVertices, FitToTargetOptions);

	if (bFitted)
	{
		CommitFaceState(InCharacter, GetFaceState(InCharacter));
		return EImportErrorCode::Success;
	}
	else
	{
		return EImportErrorCode::FittingError;
	}
}

void UMetaHumanCharacterEditorSubsystem::InitializeFromPreset(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TNotNull<UMetaHumanCharacter*> InPresetCharacter)
{
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InMetaHumanCharacter];

	FScopedSlowTask InitFromPresetTask{ 4.0f, LOCTEXT("InitFromPresetTask", "Initializing character from a Preset") };
	InitFromPresetTask.MakeDialog();

	InitFromPresetTask.EnterProgressFrame();

	// Apply face state
	if (!CharacterData->FaceState->Deserialize(InPresetCharacter->GetFaceStateData()))
	{
		UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("%s: Failed to deserialize face state stored in Preset Character asset"), *InPresetCharacter->GetFullName());
		return;
	}
	FMetaHumanCharacterIdentity::FSettings Settings = CharacterData->FaceState->GetSettings();
	Settings.SetGlobalVertexDeltaScale(InPresetCharacter->FaceEvaluationSettings.GlobalDelta);
	Settings.SetGlobalHighFrequencyScale(InPresetCharacter->FaceEvaluationSettings.HighFrequencyDelta);
	CharacterData->FaceState->SetSettings(Settings);
	CharacterData->FaceState->SetHighFrequencyVariant(InPresetCharacter->SkinSettings.Skin.FaceTextureIndex);
	CharacterData->FaceState->SetFaceScale(InPresetCharacter->FaceEvaluationSettings.HeadScale);
	CommitFaceState(InMetaHumanCharacter, CharacterData->FaceState);

	InitFromPresetTask.EnterProgressFrame();
	// Apply body state
	if (!CharacterData->BodyState->Deserialize(InPresetCharacter->GetBodyStateData()))
	{
		UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("%s: Failed to deserialize body state stored in Preset Character asset"), *InPresetCharacter->GetFullName());
		return;
	}
	CommitBodyState(InMetaHumanCharacter, CharacterData->BodyState);

	if (InPresetCharacter->HasBodyDNA())
	{
		// If there is a body DNA available, then apply to actor
		TArray<uint8> BodyDNABuffer = InPresetCharacter->GetBodyDNABuffer();
		TSharedPtr<IDNAReader> BodyDNAReader = ReadDNAFromBuffer(&BodyDNABuffer);
		if (!BodyDNAReader.IsValid())
		{
			UE_LOG(LogMetaHumanCharacterEditor, Warning, TEXT("%s: Failed to read body DNA stored in Preset Character asset"), *InPresetCharacter->GetFullName());
		}
		// Set the Behavior part of DNA in skeletal mesh AssetUserData
		if (UAssetUserData* UserData = CharacterData->BodyMesh->GetAssetUserDataOfClass(UDNAAsset::StaticClass()))
		{
			UDNAAsset* DNAAsset = CastChecked<UDNAAsset>(UserData);
			DNAAsset->SetBehaviorReader(BodyDNAReader);
			DNAAsset->SetGeometryReader(BodyDNAReader);
			InMetaHumanCharacter->SetBodyDNABuffer(ReadStreamFromDNA(BodyDNAReader.Get(), EDNADataLayer::All));
			InMetaHumanCharacter->MarkPackageDirty();
		}
	}

	if (InPresetCharacter->HasFaceDNA())
	{
		// then update the dna
		TArray<uint8> FaceDNABuffer = InPresetCharacter->GetFaceDNABuffer();
		TSharedPtr<IDNAReader> FaceDNAReader = ReadDNAFromBuffer(&FaceDNABuffer);
		if (!FaceDNAReader.IsValid())
		{
			UE_LOG(LogMetaHumanCharacterEditor, Warning, TEXT("%s: Failed to read face DNA stored in Preset Character asset"), *InPresetCharacter->GetFullName());
		}
		else
		{
			TSharedPtr<IDNAReader> OutDna = ApplyFaceDNA(InMetaHumanCharacter, FaceDNAReader, ELodUpdateOption::All);

			if (OutDna.IsValid())
			{
				// Store the DNA into the character asset
				InMetaHumanCharacter->SetFaceDNABuffer(ReadStreamFromDNA(OutDna.Get(), EDNADataLayer::All), OutDna->GetBlendShapeChannelCount() > 0);
				InMetaHumanCharacter->MarkPackageDirty();
			}
		}
	}

	InitFromPresetTask.EnterProgressFrame();

	CharacterData->SkinSettings.Reset();
	if (InPresetCharacter->HasSynthesizedTextures())
	{
		// If the preset has synthesized texture copy the skin settings to the character data
		// and initialize the synthesized texture info in the character to match
		CharacterData->SkinSettings = InPresetCharacter->SkinSettings;
		InMetaHumanCharacter->SynthesizedFaceTexturesInfo = InPresetCharacter->SynthesizedFaceTexturesInfo;

		// Empty this arrays so they can be initialized by InitSynthesizedFaceData
		CharacterData->CachedSynthesizedImages.Empty();
		InMetaHumanCharacter->SynthesizedFaceTextures.Empty();

		// Also initialize the cached image array to match the sizes of the textures to be copied from the preset
		FMetaHumanCharacterTextureSynthesis::InitSynthesizedFaceData(FaceTextureSynthesizer,
			InMetaHumanCharacter->SynthesizedFaceTexturesInfo,
			InMetaHumanCharacter->SynthesizedFaceTextures,
			CharacterData->CachedSynthesizedImages);

		// If we have synthesized textures on a preset character, make an async request to load the data.
		for (const TPair<EFaceTextureType, FMetaHumanCharacterTextureInfo>& TextureInfoPair : InPresetCharacter->SynthesizedFaceTexturesInfo)
		{
			const EFaceTextureType TextureType = TextureInfoPair.Key;

			CharacterData->SynthesizedFaceTexturesFutures.FindOrAdd(TextureType) = InPresetCharacter->GetSynthesizedFaceTextureDataAsync(TextureType);
		}
	}

	// Do the same for the body textures
	InMetaHumanCharacter->HighResBodyTexturesInfo = InPresetCharacter->HighResBodyTexturesInfo;
	InMetaHumanCharacter->BodyTextures.Empty();
	FMetaHumanCharacterBodyTextureUtils::InitBodyTextureData(InPresetCharacter->SkinSettings.Skin, InMetaHumanCharacter->HighResBodyTexturesInfo, InMetaHumanCharacter->BodyTextures);

	for (const TPair<EBodyTextureType, FMetaHumanCharacterTextureInfo>& TextureInfoPair : InPresetCharacter->HighResBodyTexturesInfo)
	{
		const EBodyTextureType TextureType = TextureInfoPair.Key;

		CharacterData->HighResBodyTexturesFutures.FindOrAdd(TextureType) = InPresetCharacter->GetHighResBodyTextureDataAsync(TextureType);
	}

	// Need to wait for textures to be loaded here since CommitSettings will store the textures from the cache into the character
	WaitForSynthesizedTextures(InMetaHumanCharacter, CharacterData, InMetaHumanCharacter->SynthesizedFaceTextures, InMetaHumanCharacter->BodyTextures);

	UpdateCharacterPreviewMaterial(InMetaHumanCharacter, InPresetCharacter->PreviewMaterialType);
	CommitSkinSettings(InMetaHumanCharacter, InPresetCharacter->SkinSettings);
	CommitMakeupSettings(InMetaHumanCharacter, InPresetCharacter->MakeupSettings);
	CommitEyesSettings(InMetaHumanCharacter, InPresetCharacter->EyesSettings);
	CommitHeadModelSettings(InMetaHumanCharacter, InPresetCharacter->HeadModelSettings);

	TNotNull<UMetaHumanCollection*> TargetCollection = InMetaHumanCharacter->GetMutableInternalCollection();
	TArray<FName> SpecificationSlotNames;
	TargetCollection->GetPipeline()->GetSpecification()->Slots.GetKeys(SpecificationSlotNames);
	// Remove all the existing palette items from the editing character.
	for (FName SlotName : SpecificationSlotNames)
	{
		if (SlotName == UE::MetaHuman::CharacterPipelineSlots::Character)
		{
			continue;
		}
		TargetCollection->RemoveAllItemsForSlot(SlotName);
	}
	// Remove all existing selections from Target instance.
	TNotNull<UMetaHumanCharacterInstance*> TargetInstance = TargetCollection->GetMutableDefaultInstance();
	TArray<FMetaHumanPipelineSlotSelectionData> ExistingSlotSelections = TargetInstance->GetSlotSelectionData();
	for (const FMetaHumanPipelineSlotSelectionData& ExistingSelection : ExistingSlotSelections)
	{
		if (ExistingSelection.Selection.SlotName == UE::MetaHuman::CharacterPipelineSlots::Character)
		{
			continue;
		}
		if (!TargetInstance->TryRemoveSlotSelection(ExistingSelection.Selection))
		{
			UE_LOG(LogMetaHumanCharacterEditor, Warning, TEXT("Could not remove slot selection %s from the character mutable instance."), *ExistingSelection.Selection.SlotName.ToString());
		}
	}
	TNotNull<UMetaHumanCollection*> SourceCollection = InPresetCharacter->GetMutableInternalCollection();
	const TArray<FMetaHumanCharacterPaletteItem>& PresetItems = SourceCollection->GetItems();
	InitFromPresetTask.EnterProgressFrame();
	// Copy all used collection items.
	for (int32 ItemIndex = 0; ItemIndex < PresetItems.Num(); ItemIndex++)
	{
		const FMetaHumanCharacterPaletteItem& PresetItem = PresetItems[ItemIndex];
		if (PresetItem.SlotName == UE::MetaHuman::CharacterPipelineSlots::Character
			|| PresetItem.SlotName == NAME_None
			|| !PresetItem.WardrobeItem)
		{
			continue;
		}

		FMetaHumanPaletteItemKey PaletteItemKey = PresetItem.GetItemKey();
		const FMetaHumanPipelineSlotSelection SlotSelectionItem(PresetItem.SlotName, PaletteItemKey);
		if (SourceCollection->GetMutableDefaultInstance()->ContainsSlotSelection(SlotSelectionItem))
		{
			FMetaHumanCharacterPaletteItem CopyItem;
			CopyItem.DisplayName = PresetItem.DisplayName;
			CopyItem.SlotName = PresetItem.SlotName;
			CopyItem.Variation = PresetItem.Variation;

			if (PresetItem.WardrobeItem->IsExternal())
			{
				CopyItem.WardrobeItem = PresetItem.WardrobeItem;
			}
			else
			{
				CopyItem.WardrobeItem = DuplicateObject<UMetaHumanWardrobeItem>(PresetItem.WardrobeItem, TargetCollection);
			}

			if ((!TargetCollection->TryAddItem(CopyItem)))
			{
				UE_LOG(LogMetaHumanCharacterEditor, Warning, TEXT("Failed to copy wardobe item %s from a preset."), *CopyItem.DisplayName.ToString());
			}
		}
	}

	// Copy over all selections from Source instance.
	TNotNull<const UMetaHumanCharacterInstance*> SourceInstance = SourceCollection->GetMutableDefaultInstance();
	for (const FMetaHumanPipelineSlotSelectionData& SourceSelectionData : SourceInstance->GetSlotSelectionData())
	{
		if (SourceSelectionData.Selection.SlotName != UE::MetaHuman::CharacterPipelineSlots::Character)
		{
			if (!TargetInstance->TryAddSlotSelection(SourceSelectionData.Selection))
			{
				UE_LOG(LogMetaHumanCharacterEditor, Warning, TEXT("Failed to copy wardrobe %s selection from a preset."), *SourceSelectionData.Selection.SlotName.ToString());
			}
		}
	}
	// Copy parameter overrides from Source instance.
	for (const TPair<FMetaHumanPaletteItemPath, FInstancedPropertyBag>& Pair : SourceInstance->GetOverriddenInstanceParameters())
	{
		TargetInstance->OverrideInstanceParameters(Pair.Key, Pair.Value);
	}

	RunCharacterEditorPipelineForPreview(InMetaHumanCharacter);
}

static int32 GetNumberOfVertices(const UStaticMesh* InTemplateStaticMesh, int32 InLodIndex)
{
	int32 NumVertices = 0;
	if (FMeshDescription* MeshDescription = InTemplateStaticMesh->GetMeshDescription(InLodIndex))
	{
		FStaticMeshAttributes Attributes(*MeshDescription);
		NumVertices = Attributes.GetVertexPositions().GetNumElements();
	}
	return NumVertices;
}

EImportErrorCode UMetaHumanCharacterEditorSubsystem::GetDataForConforming(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TNotNull<UObject*> InTemplateMesh, UObject* InTemplateLeftEyeMesh, UObject* InTemplateRightEyeMesh, UObject* InTemplateTeethMesh, const FImportFromTemplateParams& InImportParams, TMap<EHeadFitToTargetMeshes, TArray<FVector3f>>& OutVertices)
{
#if WITH_EDITOR
	const int32 Template2MHLODIndex = 0;
	const int32 Template2MHHeadMeshIndex = 0;
	const int32 Template2MHTeethIndex = 1;
	const int32 Template2MHEyeLeftIndex = 3;
	const int32 Template2MHEyeRightIndex = 4;
	
	const FMetaHumanCharacterIdentityModels& IdentityModels = GetOrCreateCharacterIdentity(InMetaHumanCharacter->TemplateType);
	TSharedPtr<IDNAReader> ArchetypeDnaReader = UMetaHumanIdentityFace::GetPluginArchetypeDNAReader();
	if (!ArchetypeDnaReader)
	{
		UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Failed to get archetype DNA reader"));
		return EImportErrorCode::GeneralError;
	}

	const int32 NumHeadMeshVertices = IdentityModels.Face->GetNumLOD0MeshVertices(EHeadFitToTargetMeshes::Head);
	const int32 NumEyeMeshVertices = IdentityModels.Face->GetNumLOD0MeshVertices(EHeadFitToTargetMeshes::LeftEye);
	const int32 NumTeethMeshVertices = IdentityModels.Face->GetNumLOD0MeshVertices(EHeadFitToTargetMeshes::Teeth);

	OutVertices.Empty();

	TArray<int32> MeshIndices = { Template2MHHeadMeshIndex };
	TArray<EHeadFitToTargetMeshes> MeshTypes{ EHeadFitToTargetMeshes::Head };
	if (InImportParams.bUseEyeMeshes)
	{
		MeshIndices.Add(Template2MHEyeLeftIndex);
		MeshIndices.Add(Template2MHEyeRightIndex);
		MeshTypes.Add(EHeadFitToTargetMeshes::LeftEye);
		MeshTypes.Add(EHeadFitToTargetMeshes::RightEye);
	}
	if (InImportParams.bUseTeethMesh)
	{
		MeshIndices.Add(Template2MHTeethIndex);
		MeshTypes.Add(EHeadFitToTargetMeshes::Teeth);
	}

	if (USkeletalMesh* TemplateSkeletalMesh = Cast<USkeletalMesh>(InTemplateMesh))
	{
		if (FDNAToSkelMeshMap* DNAToSkelMeshMap = USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh(ArchetypeDnaReader.Get(), TemplateSkeletalMesh))
		{
			for (int32 Mesh = 0; Mesh < MeshIndices.Num(); ++Mesh)
			{
				TArray<FVector3f> CurVertices;
				if (InImportParams.bMatchVerticesByUVs)
				{
					if (!FMetaHumanCharacterSkelMeshUtils::GetUVCorrespondingSkeletalMeshVertices(TemplateSkeletalMesh, Template2MHLODIndex, DNAToSkelMeshMap, ArchetypeDnaReader, MeshIndices[Mesh], CurVertices))
					{
						return EImportErrorCode::InvalidInputData;
					}
				}
				else
				{
					if (!FMetaHumanCharacterSkelMeshUtils::GetSkeletalMeshVertices(TemplateSkeletalMesh, Template2MHLODIndex, DNAToSkelMeshMap, MeshIndices[Mesh], CurVertices))
					{
						return EImportErrorCode::InvalidInputData;
					}
				}
			
				OutVertices.Add(MeshTypes[Mesh], CurVertices);
			}
		}
		else
		{
			UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Failed to create DNA to skel mesh map"));
			return EImportErrorCode::GeneralError;
		}
		
	}
	else if (UStaticMesh* TemplateStaticMesh = Cast<UStaticMesh>(InTemplateMesh))
	{
		auto GetStaticMeshVertices = [&](UStaticMesh* InStaticMesh, int32 InDNAMeshIndex, int32 InExpectedNumVertices, TArray<FVector3f>& OutStaticMeshVertices) -> bool
		{
			if (InImportParams.bMatchVerticesByUVs)
			{
				return FMetaHumanCharacterSkelMeshUtils::GetUVCorrespondingStaticMeshVertices(InStaticMesh, Template2MHLODIndex, ArchetypeDnaReader, InDNAMeshIndex, OutStaticMeshVertices);
			}

			if (GetNumberOfVertices(InStaticMesh, Template2MHLODIndex) != InExpectedNumVertices)
			{
				return false;
			}

			return FMetaHumanCharacterSkelMeshUtils::GetStaticMeshVertices(InStaticMesh, Template2MHLODIndex, OutStaticMeshVertices);
		};
		
		// Check number of vertices for template mesh matches MH topology before getting vertices via UVs
		TArray<FVector3f> CurVertices;
		if (GetStaticMeshVertices(TemplateStaticMesh, Template2MHHeadMeshIndex, NumHeadMeshVertices, CurVertices))
		{
			OutVertices.Add(EHeadFitToTargetMeshes::Head, CurVertices);
		}
		else
		{
			return EImportErrorCode::InvalidHeadMesh;
		}

		// add optional eye and teeth meshes
		if (UStaticMesh* TemplateLeftEyeStaticMesh = Cast<UStaticMesh>(InTemplateLeftEyeMesh))
		{
			if (GetStaticMeshVertices(TemplateLeftEyeStaticMesh, Template2MHEyeLeftIndex, NumEyeMeshVertices, CurVertices))
			{
				OutVertices.Add(EHeadFitToTargetMeshes::LeftEye, CurVertices);
			}
			else
			{
				return EImportErrorCode::InvalidLeftEyeMesh;
			}
		}

		if (UStaticMesh* TemplateRightEyeStaticMesh = Cast<UStaticMesh>(InTemplateRightEyeMesh))
		{
			if (GetStaticMeshVertices(TemplateRightEyeStaticMesh, Template2MHEyeRightIndex, NumEyeMeshVertices, CurVertices))
			{
				OutVertices.Add(EHeadFitToTargetMeshes::RightEye, CurVertices);
			}
			else
			{
				return EImportErrorCode::InvalidRightEyeMesh;
			}
		}

		if (UStaticMesh* TeethStaticMesh = Cast<UStaticMesh>(InTemplateTeethMesh))
		{
			if (GetStaticMeshVertices(TeethStaticMesh, Template2MHTeethIndex, NumTeethMeshVertices, CurVertices))
			{
				OutVertices.Add(EHeadFitToTargetMeshes::Teeth, CurVertices);
			}
			else
			{
				return EImportErrorCode::InvalidTeethMesh;
			}
		}
	}
	else
	{
		UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Failed to get data for conforming as Template Mesh is invalid"));
		return EImportErrorCode::InvalidInputData;
	}
#endif

	return EImportErrorCode::Success;
}

EImportErrorCode UMetaHumanCharacterEditorSubsystem::GetMeshForBodyConforming(UMetaHumanCharacter* InMetaHumanCharacter, UObject* InBodyTemplateMesh, UObject* InHeadTemplateMesh, bool bInMatchVerticesByUVs, TArray<FVector>& OutVertices)

{
	if (!IsValid(InMetaHumanCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "GetMeshForBodyConforming called with invalid character");
		return {};
	}

	if (!IsObjectAddedForEditing(InMetaHumanCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "GetMeshForBodyConforming called with {Character} that is not not added for editing", InMetaHumanCharacter->GetName());
		return {};
	}

	if (!IsValid(InBodyTemplateMesh))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "GetMeshForBodyConforming called with invalid body template");
		return {};
	}
	
	TArray<FVector3f> Vertices;
	EImportErrorCode ErrorCode = GetMeshForBodyConforming(InMetaHumanCharacter, InBodyTemplateMesh, InHeadTemplateMesh, bInMatchVerticesByUVs, Vertices);

	if (ErrorCode == EImportErrorCode::Success)
	{
		// convert to TArray<FVector>
		OutVertices.Reserve(Vertices.Num());
		Algo::Transform(Vertices, OutVertices, [](const FVector3f& V) { return FVector(V); });
	}
	
	return ErrorCode;
}


EImportErrorCode UMetaHumanCharacterEditorSubsystem::GetMeshForBodyConforming(UMetaHumanCharacter* InMetaHumanCharacter, TSharedRef<IDNAReader> InBodyDna, TSharedPtr<IDNAReader> InHeadDna, TArray<FVector3f>& OutVertices)
{
	
	TSharedPtr<FMetaHumanCharacterBodyIdentity> BodyIdentity = GetOrCreateCharacterIdentity(InMetaHumanCharacter->TemplateType).Body;
	auto PackToVectorView = [](TArrayView<const float> xs,
							   TArrayView<const float> ys,
							   TArrayView<const float> zs)
		{
			check(xs.Num() == ys.Num() && ys.Num() == zs.Num());
			TArray<FVector3f> out;
			out.SetNumUninitialized(xs.Num());
			for (int32 i = 0; i < out.Num(); ++i)
			{
				out[i] = FVector3f(xs[i], ys[i], zs[i]);
			}
			return out;
		};
	if (InBodyDna->GetVertexPositionCount(0) != BodyIdentity->GetNumLOD0MeshVertices(false))
	{
		return EImportErrorCode::InvalidInputData;
	}
	OutVertices = PackToVectorView(InBodyDna->GetVertexPositionXs(0),InBodyDna->GetVertexPositionYs(0),InBodyDna->GetVertexPositionZs(0));

	if (InHeadDna.IsValid())
	{
		TSharedPtr<IDNAReader> HeadArchetypeReader = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAseet(EMetaHumanImportDNAType::Face, GetTransientPackage())->GetGeometryReader();
		if (InHeadDna->GetVertexPositionCount(0) == HeadArchetypeReader->GetVertexPositionCount(0))
		{
			
			TArray<FVector3f> CombinedVertices = PackToVectorView(InHeadDna->GetVertexPositionXs(0),InHeadDna->GetVertexPositionYs(0),InHeadDna->GetVertexPositionZs(0));
			TArray<int32> Mapping = BodyIdentity->GetBodyToCombinedMapping();
			CombinedVertices.SetNumUninitialized(BodyIdentity->GetNumLOD0MeshVertices(true));
			for (int32 i = 0; i < Mapping.Num(); ++i)
			{
				CombinedVertices[Mapping[i]] = OutVertices[i];	
			}
			OutVertices = CombinedVertices;
		}
		else
		{
			return  EImportErrorCode::InvalidHeadMesh;
		}
	}
	return EImportErrorCode::Success;	
}

EImportErrorCode UMetaHumanCharacterEditorSubsystem::GetJointsForBodyConforming(TSharedRef<IDNAReader> InBodyDna, TArray<FVector3f>& OutJointWorldTranslations, TArray<FVector3f>& OutJointRotations)
{
	OutJointRotations.Empty();
	OutJointWorldTranslations.Empty();
#if WITH_EDITOR
	TSharedPtr<IDNAReader> ArchetypeDnaReader = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAseet(EMetaHumanImportDNAType::Body, GetTransientPackage())->GetGeometryReader();
	if (ArchetypeDnaReader->GetJointCount() != InBodyDna->GetJointCount())
	{
		return  EImportErrorCode::InvalidInputBones;
	}
	OutJointWorldTranslations.SetNumUninitialized( InBodyDna->GetJointCount());
	OutJointRotations.SetNumUninitialized(InBodyDna->GetJointCount());
	for (int i = 0; i < OutJointRotations.Num(); ++i)
	{
		FVector DnaRotation = InBodyDna->GetNeutralJointRotation(i);
		// DNAWrapper is returning wrong values for UE Space so we have to do this
		OutJointRotations[i] = FVector3f(FMath::DegreesToRadians(DnaRotation.Z), FMath::DegreesToRadians(DnaRotation.X), FMath::DegreesToRadians(DnaRotation.Y));	
	}
	OutJointWorldTranslations = UE::MetaHuman::GetJointWorldTranslations(InBodyDna);	
#endif
	return EImportErrorCode::Success;
}

EImportErrorCode UMetaHumanCharacterEditorSubsystem::GetJointsForBodyConforming(USkeletalMesh* InSkelMesh, TArray<FVector>& OutJointWorldTranslations, TArray<FVector>& OutJointRotations)
{
	TArray<FVector3f> JointRotations, JointWorldTranslations;
	EImportErrorCode ErrorCode = GetJointsForBodyConforming(InSkelMesh, JointWorldTranslations, JointRotations);
	if (ErrorCode == EImportErrorCode::Success)
	{
		// convert to TArray<FVector>
		OutJointWorldTranslations.Reserve(JointWorldTranslations.Num());
		Algo::Transform(JointWorldTranslations, OutJointWorldTranslations, [](const FVector3f& V) { return FVector(V); });
		OutJointRotations.Reserve(JointRotations.Num());
		Algo::Transform(JointRotations, OutJointRotations, [](const FVector3f& V) { return FVector(V); });
	}
	return ErrorCode;
}

EImportErrorCode UMetaHumanCharacterEditorSubsystem::GetJointsForBodyConforming(USkeletalMesh* InSkelMesh, TArray<FVector3f>& OutJointWorldTranslations, TArray<FVector3f>& OutJointRotations)
{
	OutJointRotations.Empty();
	OutJointWorldTranslations.Empty();
#if WITH_EDITOR
	if (!IsValid(InSkelMesh))
	{
		UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Input body skeletal mesh template not provided."));
		return EImportErrorCode::GeneralError;
	}
	TArray<int32> RLJointToUEBoneIndices;
	TSharedPtr<IDNAReader> ArchetypeDnaReader = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAseet(EMetaHumanImportDNAType::Body, GetTransientPackage())->GetGeometryReader();
	UE::MetaHuman::MapJoints(InSkelMesh, ArchetypeDnaReader->Unwrap(), RLJointToUEBoneIndices);
	TArray<FVector3f> TemplateMeshComponentJointTranslations = FMetaHumanCharacterSkelMeshUtils::GetComponentSpaceJointTranslations(InSkelMesh);
	const TArray<FTransform>& RawBonePose = InSkelMesh->GetRefSkeleton().GetRawRefBonePose();
	OutJointRotations.SetNumUninitialized(RawBonePose.Num());

	int32 JointNum = ArchetypeDnaReader->GetJointCount();
	OutJointWorldTranslations.AddUninitialized(JointNum);
	for (uint16_t JointIndex = 0; JointIndex < JointNum; JointIndex++)
	{
		int32 BoneIndex = RLJointToUEBoneIndices[JointIndex];
		if (BoneIndex == INDEX_NONE)
		{
			FString BoneName = ArchetypeDnaReader->GetJointName(JointIndex);
			UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Selected skel mesh must be consistent with MetaHuman topology to be imported into MetaHumanCharacter asset. Bone: %s not found in template mesh."), *BoneName);
			return EImportErrorCode::InvalidInputBones;
		}
		OutJointWorldTranslations[JointIndex] = TemplateMeshComponentJointTranslations[BoneIndex];
		FVector Euler = FVector::DegreesToRadians(RawBonePose[BoneIndex].GetRotation().Rotator().Euler());
		OutJointRotations[JointIndex].X = Euler.X;
		OutJointRotations[JointIndex].Y = Euler.Y;
		OutJointRotations[JointIndex].Z = Euler.Z;
	}
#endif
	return EImportErrorCode::Success;
}

EImportErrorCode UMetaHumanCharacterEditorSubsystem::GetMeshForBodyConforming(UMetaHumanCharacter* InMetaHumanCharacter, UObject* InBodyTemplateMesh, UObject* InHeadTemplateMesh, bool bInMatchVerticesByUVs, TArray<FVector3f>& OutVertices)
{
	OutVertices.Empty();
#if WITH_EDITOR
	
	if (!IsValid(InMetaHumanCharacter) || !IsValid(InBodyTemplateMesh))
	{
		
		UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Metahuman character or body template not provided."));
		return EImportErrorCode::GeneralError;
	}
	TSharedPtr<IDNAReader> BodyDnaReader = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAseet(EMetaHumanImportDNAType::Body, GetTransientPackage())->GetGeometryReader();
	TSharedPtr<IDNAReader> CombinedDnaReader = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAseet(EMetaHumanImportDNAType::Combined, GetTransientPackage())->GetGeometryReader();
	TSharedPtr<IDNAReader> HeadDnaReader = FMetaHumanCharacterSkelMeshUtils::GetArchetypeDNAAseet(EMetaHumanImportDNAType::Face, GetTransientPackage())->GetGeometryReader();
	if (!BodyDnaReader || !CombinedDnaReader)
	{
		UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Failed to get archetype DNA reader"));
		return EImportErrorCode::GeneralError;
	}
	const int32 Template2MHLODIndex = 0;
	const int32 DNAArchetypeMeshIndex = 0;
	const int32 NumHeadMeshVertices = HeadDnaReader->GetVertexPositionCount(0);
	const int32 NumBodyMeshVertices = BodyDnaReader->GetVertexPositionCount(0); 
	const int32 NumCombinedBodyMeshVertices = CombinedDnaReader->GetVertexPositionCount(0);

	if (USkeletalMesh* TemplateSkeletalMesh = Cast<USkeletalMesh>(InBodyTemplateMesh))
	{
		if (FDNAToSkelMeshMap* DNAToSkelMeshMap = USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh(BodyDnaReader.Get(), TemplateSkeletalMesh))
		{
			if (bInMatchVerticesByUVs)
			{
				if (!FMetaHumanCharacterSkelMeshUtils::GetUVCorrespondingSkeletalMeshVertices(TemplateSkeletalMesh, Template2MHLODIndex, DNAToSkelMeshMap, BodyDnaReader, DNAArchetypeMeshIndex, OutVertices))
				{
					return EImportErrorCode::InvalidInputData;
				}
			}
			else
			{
				if (!FMetaHumanCharacterSkelMeshUtils::GetSkeletalMeshVertices(TemplateSkeletalMesh, Template2MHLODIndex, DNAToSkelMeshMap, DNAArchetypeMeshIndex, OutVertices))
				{
					return EImportErrorCode::InvalidInputData;
				}
			}
		}
		else
		{
			UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Failed to create DNA to skel mesh map"));
			return EImportErrorCode::GeneralError;
		}
	}
	else if (UStaticMesh* TemplateStaticMesh = Cast<UStaticMesh>(InBodyTemplateMesh))
	{
		TArray<FVector3f> CurVertices;
		if (bInMatchVerticesByUVs)
		{
			if (FMetaHumanCharacterSkelMeshUtils::GetUVCorrespondingStaticMeshVertices(TemplateStaticMesh, Template2MHLODIndex, BodyDnaReader, DNAArchetypeMeshIndex, CurVertices))
			{
				OutVertices = CurVertices;
			}
			else
			{
				return EImportErrorCode::InvalidInputData;
			}
		}
		else
		{
			int32 NumMeshVertices = GetNumberOfVertices(TemplateStaticMesh, Template2MHLODIndex);
			if (NumMeshVertices != NumBodyMeshVertices && NumMeshVertices != NumCombinedBodyMeshVertices)
			{
				return EImportErrorCode::InvalidInputData;
			}

			if (FMetaHumanCharacterSkelMeshUtils::GetStaticMeshVertices(TemplateStaticMesh, Template2MHLODIndex, CurVertices))
			{
				OutVertices = CurVertices;
			}
			else
			{
				return EImportErrorCode::InvalidInputData;
			}
		}
	}
	else
	{
		UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Failed to get data for conforming as Template Mesh is invalid"));
		return EImportErrorCode::InvalidInputData;
	}

	if (USkeletalMesh* TemplateSkeletalMesh = Cast<USkeletalMesh>(InHeadTemplateMesh))
	{
		if (FDNAToSkelMeshMap* DNAToSkelMeshMap = USkelMeshDNAUtils::CreateMapForUpdatingNeutralMesh(HeadDnaReader.Get(), TemplateSkeletalMesh))
		{
				TArray<FVector3f> CurVertices;
				if (bInMatchVerticesByUVs)
				{
					if (!FMetaHumanCharacterSkelMeshUtils::GetUVCorrespondingSkeletalMeshVertices(TemplateSkeletalMesh, Template2MHLODIndex, DNAToSkelMeshMap, HeadDnaReader, 0, CurVertices))
					{
						return EImportErrorCode::InvalidInputData;
					}
				}
				else
				{
					if (!FMetaHumanCharacterSkelMeshUtils::GetSkeletalMeshVertices(TemplateSkeletalMesh, Template2MHLODIndex, DNAToSkelMeshMap, 0, CurVertices))
					{
						return EImportErrorCode::InvalidInputData;
					}
				}
				
			//We want to combine head and mesh into one mesh, OutVertices already contains body
			TArray<int32> Mapping = GetOrCreateCharacterIdentity(InMetaHumanCharacter->TemplateType).Body->GetBodyToCombinedMapping();
			CurVertices.SetNumUninitialized(CombinedDnaReader->GetVertexPositionCount(0));
			for (int32 i = 0; i < Mapping.Num(); ++i)
			{
				CurVertices[Mapping[i]] = OutVertices[i];	
			}
			OutVertices = CurVertices;
		}
		else
		{
			UE_LOG(LogMetaHumanCharacterEditor, Error, TEXT("Failed to create DNA to skel mesh map"));
			return EImportErrorCode::GeneralError;
		}
	} else if (UStaticMesh* TemplateStaticMesh = Cast<UStaticMesh>(InHeadTemplateMesh))
	{
		TArray<FVector3f> CurVertices;
		if (bInMatchVerticesByUVs)
		{
			if (!FMetaHumanCharacterSkelMeshUtils::GetUVCorrespondingStaticMeshVertices(TemplateStaticMesh, Template2MHLODIndex, HeadDnaReader, DNAArchetypeMeshIndex, CurVertices))
			{
				return EImportErrorCode::InvalidInputData;
			}
		}
		else
		{
			int32 NumMeshVertices = GetNumberOfVertices(TemplateStaticMesh, Template2MHLODIndex);
			if (NumMeshVertices != NumHeadMeshVertices)
			{
				return EImportErrorCode::InvalidInputData;
			}

			if (!FMetaHumanCharacterSkelMeshUtils::GetStaticMeshVertices(TemplateStaticMesh, Template2MHLODIndex, CurVertices))
			{
				return EImportErrorCode::InvalidInputData;
			}
		}
		//We want to combine head and mesh into one mesh, OutVertices already contains body
		TArray<int32> Mapping = GetOrCreateCharacterIdentity(InMetaHumanCharacter->TemplateType).Body->GetBodyToCombinedMapping();
		CurVertices.SetNumUninitialized(CombinedDnaReader->GetVertexPositionCount(0));
		for (int32 i = 0; i < Mapping.Num(); ++i)
		{
			CurVertices[Mapping[i]] = OutVertices[i];	
		}
		OutVertices = CurVertices;
	}

#endif

	return EImportErrorCode::Success;
}

/* Conforms the body mesh to target parameters */
bool UMetaHumanCharacterEditorSubsystem::ConformBody(UMetaHumanCharacter* InMetaHumanCharacter, const TArray<FVector3f>& InVertices, const TArray<FVector3f>& InJointRotations, bool bRepose, bool bEstimateJointsFromMesh)
{
	if (!IsValid(InMetaHumanCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "ConformBody called with invalid character");
		return false;
	}
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyState = CopyBodyState(InMetaHumanCharacter);
	if (BodyState->Conform(InVertices, InJointRotations, bRepose, bEstimateJointsFromMesh))
	{
		CommitBodyState(InMetaHumanCharacter, BodyState);
		return true;
	}
	return false;
}

bool UMetaHumanCharacterEditorSubsystem::ConformBody(UMetaHumanCharacter* InMetaHumanCharacter, const TArray<FVector>& InVertices, const TArray<FVector>& InJointRotations, bool bRepose, bool bEstimateJointsFromMesh)
{
	TArray<FVector3f> Vertices, JointRotations;

	// convert to TArray<FVector3f>
	Vertices.Reserve(InVertices.Num());
	Algo::Transform(InVertices,Vertices, [](const FVector& V) { return FVector3f(V); });
	JointRotations.Reserve(InJointRotations.Num());
	Algo::Transform(InJointRotations, JointRotations, [](const FVector& V) { return FVector3f(V); });

	return ConformBody(InMetaHumanCharacter, Vertices, JointRotations, bRepose, bEstimateJointsFromMesh);
}

	
/* Set custom body joint positions */
bool UMetaHumanCharacterEditorSubsystem::SetBodyJoints(UMetaHumanCharacter* InMetaHumanCharacter, const TArray<FVector3f>& InComponentJointTranslations, const TArray<FVector3f>& InJointRotations, bool bImportHelperJoints)
{
	if (!IsValid(InMetaHumanCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "SetBodyJointTranslations called with invalid character");
		return false;
	}
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyState = CopyBodyState(InMetaHumanCharacter);
	if (BodyState->SetJointTranslations(InComponentJointTranslations, bImportHelperJoints) && BodyState->SetJointRotations(InJointRotations, bImportHelperJoints))
	{
		CommitBodyState(InMetaHumanCharacter, BodyState);
		return true;
	}
	return false;
}

/* Set custom body neutral mesh */ 
bool UMetaHumanCharacterEditorSubsystem::SetBodyMesh(UMetaHumanCharacter* InMetaHumanCharacter, const TArray<FVector3f>& InVertices, bool bRepositionHelperJoints)
{
	if (!IsValid(InMetaHumanCharacter))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "SetBodyMesh called with invalid character");
		return false;
	}
	TSharedRef<FMetaHumanCharacterBodyIdentity::FState> BodyState = CopyBodyState(InMetaHumanCharacter);
	if (BodyState->SetMesh(InVertices, bRepositionHelperJoints))
	{
		CommitBodyState(InMetaHumanCharacter, BodyState);
		return true;
	}
	return false;
}

EImportErrorCode UMetaHumanCharacterEditorSubsystem::ImportBodyWholeRig(TNotNull<UMetaHumanCharacter*> InMetaHumanCharacter, TSharedRef<class IDNAReader> InBodyDna, TSharedPtr<IDNAReader> InHeadDna)
{
	EImportErrorCode ErrorCode = EImportErrorCode::Success;
	TSharedRef<FMetaHumanCharacterEditorData> CharacterData = CharacterDataMap[InMetaHumanCharacter];
	
	if (FMetaHumanCharacterSkelMeshUtils::CheckDNACompatibility(UE::MetaHuman::GetDNAReader(CharacterData->BodyMesh).Get(), &InBodyDna.Get()))
	{
		TArray<FVector3f> Vertices;
		if (GetMeshForBodyConforming(InMetaHumanCharacter, InBodyDna, InHeadDna, Vertices) != EImportErrorCode::Success)
		{
			return EImportErrorCode::InvalidInputData;
		}
		TArray<FVector3f> JointTranslations;
		TArray<FVector3f> JointRotations;
		if (GetJointsForBodyConforming(InBodyDna, JointTranslations, JointRotations) != EImportErrorCode::Success)
		{
			return EImportErrorCode::InvalidInputData;
		}
		if (ConformBody(InMetaHumanCharacter, Vertices, JointRotations, true, false))
		{
			SetBodyJoints(InMetaHumanCharacter, JointTranslations, JointRotations, true);
			CommitBodyState(InMetaHumanCharacter, GetBodyState(InMetaHumanCharacter));
			constexpr bool bImportingAsFixedBodyType = true;
			CommitBodyDNA(InMetaHumanCharacter, InBodyDna, bImportingAsFixedBodyType);
		}
		else
		{
			ErrorCode = EImportErrorCode::InvalidInputData;
		}
	}
	else
	{
		FString CombinedBodyModelPath = FMetaHumanCommonDataUtils::GetArchetypeDNAPath(EMetaHumanImportDNAType::Combined);
		TSharedPtr<IDNAReader> CombinedArchetypeDnaReader = ReadDNAFromFile(CombinedBodyModelPath);
		if (FMetaHumanCharacterSkelMeshUtils::CheckDNACompatibility(CombinedArchetypeDnaReader.Get(), &InBodyDna.Get()))
		{
			ErrorCode = EImportErrorCode::CombinedBodyCannotBeImportedAsWholeRig;
		}
		else
		{
			ErrorCode = EImportErrorCode::InvalidInputData;
		}
	}

	if (ErrorCode == EImportErrorCode::Success)
	{
		UE::MetaHuman::Analytics::RecordImportBodyDNAEvent(InMetaHumanCharacter);
	}

	return ErrorCode;
}

#undef LOCTEXT_NAMESPACE
