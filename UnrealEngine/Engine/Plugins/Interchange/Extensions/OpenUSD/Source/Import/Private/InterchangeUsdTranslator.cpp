// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeUsdTranslator.h"

#include "Objects/USDInfoCache.h"
#include "Objects/USDSchemaTranslator.h"
#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDGeomMeshConversion.h"
#include "USDGroomConversion.h"
#include "USDLayerUtils.h"
#include "USDLightConversion.h"
#include "USDLog.h"
#include "USDMaterialXShaderGraph.h"
#include "USDNaniteAssemblyUtils.h"
#include "USDObjectUtils.h"
#include "USDPrimConversion.h"
#include "USDProjectSettings.h"
#include "USDShadeConversion.h"
#include "USDSkeletalDataConversion.h"
#include "USDStageOptions.h"
#include "USDTypesConversion.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdAttribute.h"
#include "UsdWrappers/UsdEditContext.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdRelationship.h"
#include "UsdWrappers/UsdSkelAnimQuery.h"
#include "UsdWrappers/UsdSkelBinding.h"
#include "UsdWrappers/UsdSkelBlendShape.h"
#include "UsdWrappers/UsdSkelBlendShapeQuery.h"
#include "UsdWrappers/UsdSkelCache.h"
#include "UsdWrappers/UsdSkelInbetweenShape.h"
#include "UsdWrappers/UsdSkelSkeletonQuery.h"
#include "UsdWrappers/UsdSkelSkinningQuery.h"
#include "UsdWrappers/UsdStage.h"
#include "UsdWrappers/UsdTyped.h"
#include "UsdWrappers/UsdVariantSets.h"
#include "UsdWrappers/VtValue.h"

#include "InterchangeAudioSoundWaveNode.h"
#include "InterchangeCameraNode.h"
#include "InterchangeGroomNode.h"
#include "InterchangeLightNode.h"
#include "InterchangeMeshLODContainerNode.h"
#include "InterchangeManager.h"
#include "InterchangeMaterialDefinitions.h"
#include "InterchangeMaterialInstanceNode.h"
#include "InterchangeMaterialReferenceNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangeSceneComponentNodes.h"
#include "InterchangeSceneNode.h"
#include "InterchangeShaderGraphNode.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeTextureCubeNode.h"
#include "InterchangeTranslatorHelper.h"
#include "InterchangeUsdContext.h"
#include "InterchangeVolumeNode.h"
#include "Mesh/InterchangeMeshPayload.h"
#include "Nodes/InterchangeSourceNode.h"
#include "Nodes/InterchangeUserDefinedAttribute.h"
#include "Usd/InterchangeUsdDefinitions.h"
#include "Volume/InterchangeVolumeDefinitions.h"
#include "Volume/InterchangeVolumeTranslatorSettings.h"

#include "MaterialX/InterchangeMaterialXTranslator.h"
#include "MaterialX/MaterialXUtils/MaterialXManager.h"

#include "Async/ParallelFor.h"
#include "Components/HeterogeneousVolumeComponent.h"
#include "GroomCacheData.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Regex.h"
#include "Modules/ModuleManager.h"
#include "MovieSceneSection.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "StaticMeshAttributes.h"
#include "UDIMUtilities.h"
#include "UObject/GCObjectScopeGuard.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/usd/usdLux/tokens.h"
#include "pxr/usd/usdShade/tokens.h"
#include "pxr/usd/usdMedia/tokens.h"
#include "USDIncludesEnd.h"
#endif	  // USE_USD_SDK

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeUsdTranslator)

#define LOCTEXT_NAMESPACE "InterchangeUSDTranslator"

static bool GInterchangeEnableUSDImport = true;
static FAutoConsoleVariableRef CVarInterchangeEnableUSDImport(
	TEXT("Interchange.FeatureFlags.Import.USD"),
	GInterchangeEnableUSDImport,
	TEXT("Whether USD support is enabled.")
);

static bool GInterchangeEnableUSDLevelImport = false;
// Import into level via USD Interchange is disabled for 5.5 as it's still a work in progress
static FAutoConsoleVariableRef CVarInterchangeEnableUSDLevelImport(
	TEXT("Interchange.FeatureFlags.Import.USD.ToLevel"),
	GInterchangeEnableUSDLevelImport,
	TEXT("Whether support for USD level import is enabled.")
);


static bool GInterchangeEnableUSDAudioImport = true;
static FAutoConsoleVariableRef CVarInterchangeEnableUSDAudioImport(
	TEXT("Interchange.FeatureFlags.Import.USD.Audio"),
	GInterchangeEnableUSDAudioImport,
	TEXT("Whether support for USD Media Spatial Audio is enabled.")
);

namespace UE::InterchangeUsdTranslator::Private
{
	using namespace UE::Interchange::USD;

#if USE_USD_SDK
	const static FString AnimationPrefix = TEXT("\\Animation\\");
	const static FString AnimationTrackPrefix = TEXT("\\AnimationTrack\\");
	const static FString CameraPrefix = TEXT("\\Camera\\");
	const static FString GroomPrefix = TEXT("\\Groom\\");
	const static FString LightPrefix = TEXT("\\Light\\");
	const static FString LODPrefix = TEXT("\\LOD\\");
	const static FString MaterialPrefix = TEXT("\\Material\\");
	const static FString MaterialReferencePrefix = TEXT("\\MaterialReference\\");
	const static FString MeshPrefix = TEXT("\\Mesh\\");
	const static FString MorphTargetPrefix = TEXT("\\MorphTarget\\");
	const static FString PrimitiveShapePrefix = TEXT("\\PrimitiveShape\\");
	const static FString SpatialAudioPrefix = TEXT("\\SpatialAudio\\");
	const static FString TexturePrefix = TEXT("\\Texture\\");
	const static FString VolumePrefix = TEXT("\\Volume\\");
	const static FString ComponentPrefix = TEXT("\\Component\\");

	const static FString LODContainerSuffix = TEXT("LODContainer");
	const static FString TwoSidedSuffix = TEXT("_TwoSided");

	const static FString LODString = TEXT("LOD");

	// Information intended to be passed down from parent to children (by value) as we traverse the stage
	struct FTraversalInfo
	{
		using FNaniteAssemblyTraversalResult = UsdToUnreal::NaniteAssemblyUtils::FNaniteAssemblyTraversalResult;

		UInterchangeBaseNode* ParentNode = nullptr;

		FTransform SceneGlobalTransform;

		TSharedPtr<UE::FUsdSkelCache> FurthestSkelCache;
		TSharedPtr<FString> FurthestParentSkelRootPath;	   // Used to populate the skel cache
		TSharedPtr<FString> ClosestParentSkelRootPath;
		TSharedPtr<FString> BoundSkeletonPrimPath;
		TSharedPtr<TArray<FString>> SkelJointUsdNames;	   // Needed for skel mesh payloads

		// Nanite Assembly info
		TSharedPtr<FString> ClosestNaniteAssemblySkelBindingPath;
		TSharedPtr<const FNaniteAssemblyTraversalResult> NaniteAssemblyTraversalResult;

		bool bVisible = true;
		bool bInsideLOD = false;
		bool bIsLODContainer = false;

	public:
		void UpdateWithCurrentPrim(const UE::FUsdPrim& CurrentPrim)
		{
			if (!CurrentPrim)
			{
				return;
			}

			bVisible = bVisible && UsdUtils::HasInheritedVisibility(CurrentPrim);

			// Check this first so that we go bInsideLOD if our parent was a LODContainer.
			// bInsideLOD is only set for the immediate children of the prim that sets bIsLODContainer,
			// as we only handle LOD variant meshes that are direct children of the LOD container prim anyway
			bInsideLOD = bIsLODContainer;

			// We only want this to be true when we're traversing the exact prim that owns
			// the LOD: Once we step into any of its children it should go back to false
			bIsLODContainer = CurrentPrim.GetVariantSets().HasVariantSet(LODString);

			if (CurrentPrim.IsA(TEXT("SkelRoot")))
			{
				if (!ClosestParentSkelRootPath)
				{
					// The root-most skel cache should handle any nested UsdSkel prims as well
					FurthestSkelCache = MakeShared<UE::FUsdSkelCache>();

					const bool bTraverseInstanceProxies = true;
					FurthestSkelCache->Populate(CurrentPrim, bTraverseInstanceProxies);
					FurthestParentSkelRootPath = MakeShared<FString>(CurrentPrim.GetPrimPath().GetString());
				}

				ClosestParentSkelRootPath = MakeShared<FString>(CurrentPrim.GetPrimPath().GetString());
			}

			if (ClosestParentSkelRootPath && CurrentPrim.HasAPI(TEXT("SkelBindingAPI")))
			{
				UE::FUsdStage Stage = CurrentPrim.GetStage();

				if (UE::FUsdRelationship SkelRel = CurrentPrim.GetRelationship(TEXT("skel:skeleton")))
				{
					TArray<UE::FSdfPath> Targets;
					if (SkelRel.GetTargets(Targets) && Targets.Num() > 0)
					{
						UE::FUsdPrim TargetSkeleton = Stage.GetPrimAtPath(Targets[0]);
						if (TargetSkeleton && TargetSkeleton.IsA(TEXT("Skeleton")))
						{
							BoundSkeletonPrimPath = MakeShared<FString>(TargetSkeleton.GetPrimPath().GetString());
						}
					}
				}
			}

			if (CurrentPrim.HasAPI(*UsdToUnreal::ConvertToken(UnrealIdentifiers::NaniteAssemblySkelBindingAPI)))
			{
				ClosestNaniteAssemblySkelBindingPath = MakeShared<FString>(CurrentPrim.GetPrimPath().GetString());
			}
		}

		UE::FUsdSkelSkeletonQuery ResolveSkelQuery(const UE::FUsdStage& Stage) const
		{
			if (!Stage || !BoundSkeletonPrimPath || BoundSkeletonPrimPath->IsEmpty())
			{
				return {};
			}

			UE::FUsdPrim SkeletonPrim = Stage.GetPrimAtPath(UE::FSdfPath{**BoundSkeletonPrimPath});
			if (!SkeletonPrim)
			{
				return {};
			}

			return FurthestSkelCache->GetSkelQuery(SkeletonPrim);
		}

		UE::FUsdPrim ResolveClosestParentSkelRoot(const UE::FUsdStage& Stage) const
		{
			if (!Stage || !ClosestParentSkelRootPath || ClosestParentSkelRootPath->IsEmpty())
			{
				return {};
			}

			return Stage.GetPrimAtPath(UE::FSdfPath{**ClosestParentSkelRootPath});
		}

		void RepopulateSkelCache(const UE::FUsdStage& Stage)
		{
			if (!FurthestSkelCache.IsValid() || !FurthestParentSkelRootPath.IsValid() || FurthestParentSkelRootPath->IsEmpty())
			{
				return;
			}

			UE::FUsdPrim SkelRootPrim = Stage.GetPrimAtPath(UE::FSdfPath{**FurthestParentSkelRootPath});
			if (!SkelRootPrim)
			{
				return;
			}

			const bool bTraverseInstanceProxies = true;
			ensure(FurthestSkelCache->Populate(SkelRootPrim, bTraverseInstanceProxies));
		}
	};

	void Traverse(
		const UE::FUsdPrim& Prim,
		UInterchangeUSDTranslatorImpl& TranslatorImpl,
		UInterchangeBaseNodeContainer& NodeContainer,
		UInterchangeUsdTranslatorSettings* TranslatorSettings,
		FTraversalInfo Info
	);

	// clang-format off
	const static TMap<FName, EInterchangePropertyTracks> PropertyNameToTrackType = {
		// Common properties
		{UnrealIdentifiers::HiddenInGamePropertyName, 			EInterchangePropertyTracks::ActorHiddenInGame}, // Binding visibility to the actor works better for cameras

		// Camera properties
		// TODO: Need to add support for SensorHorizontalOffset and SensorVerticalOffset, once Interchange supports those
		{UnrealIdentifiers::CurrentFocalLengthPropertyName, 		EInterchangePropertyTracks::CameraCurrentFocalLength},
		{UnrealIdentifiers::ManualFocusDistancePropertyName, 		EInterchangePropertyTracks::CameraFocusSettingsManualFocusDistance},
		{UnrealIdentifiers::CurrentAperturePropertyName, 			EInterchangePropertyTracks::CameraCurrentAperture},
		{UnrealIdentifiers::SensorWidthPropertyName, 				EInterchangePropertyTracks::CameraFilmbackSensorWidth},
		{UnrealIdentifiers::SensorHeightPropertyName, 				EInterchangePropertyTracks::CameraFilmbackSensorHeight},
		{UnrealIdentifiers::ExposureCompensationPropertyName, 		EInterchangePropertyTracks::CameraPostProcessSettingsAutoExposureBias},
		{UnrealIdentifiers::ProjectionModePropertyName, 			EInterchangePropertyTracks::CameraProjectionMode},
		{UnrealIdentifiers::OrthoFarClipPlanePropertyName, 			EInterchangePropertyTracks::CameraOrthoFarClipPlane},
		{UnrealIdentifiers::OrthoNearClipPlanePropertyName, 		EInterchangePropertyTracks::CameraOrthoNearClipPlane},
		{UnrealIdentifiers::CustomNearClipppingPlanePropertyName,	EInterchangePropertyTracks::CameraCustomNearClippingPlane},

		// Light properties
		{UnrealIdentifiers::LightColorPropertyName, 		EInterchangePropertyTracks::LightColor},
		{UnrealIdentifiers::TemperaturePropertyName, 		EInterchangePropertyTracks::LightTemperature},
		{UnrealIdentifiers::UseTemperaturePropertyName, 	EInterchangePropertyTracks::LightUseTemperature},
		{UnrealIdentifiers::SourceHeightPropertyName, 		EInterchangePropertyTracks::LightSourceHeight},
		{UnrealIdentifiers::SourceWidthPropertyName, 		EInterchangePropertyTracks::LightSourceWidth},
		{UnrealIdentifiers::SourceRadiusPropertyName, 		EInterchangePropertyTracks::LightSourceRadius},
		{UnrealIdentifiers::OuterConeAnglePropertyName, 	EInterchangePropertyTracks::LightOuterConeAngle},
		{UnrealIdentifiers::InnerConeAnglePropertyName, 	EInterchangePropertyTracks::LightInnerConeAngle},
		{UnrealIdentifiers::LightSourceAnglePropertyName, 	EInterchangePropertyTracks::LightSourceAngle},
		{UnrealIdentifiers::IntensityPropertyName, 			EInterchangePropertyTracks::LightIntensity},
	};
	// clang-format on

	// Small container that we can use Pimpl with so we don't have to include too many USD includes on the header file.
	//
	// It also skirts around a small complication where UInterchangeUSDTranslator::Translate is const, and yet we must
	// keep and modify some members (like UsdStage) for when the payload functions get called later... The other translators
	// use mutable or const casts, but with the Impl we don't need to!
	class UInterchangeUSDTranslatorImpl
	{
	public:
		/** Add a material instance to the node container, otherwise it will add a material if it comes from a Translator (for example coming from
		 * MaterialX which cannot handle material instances) */
		FString AddMaterialNode(
			const UE::FUsdPrim& Prim,
			UInterchangeUsdTranslatorSettings* TranslatorSettings,
			UInterchangeBaseNodeContainer& NodeContainer,
			bool bForceTwoSided = false
		);

		FString AddMeshNode(
			const UE::FUsdPrim& Prim,
			UInterchangeUsdTranslatorSettings* TranslatorSettings,
			UInterchangeBaseNodeContainer& NodeContainer,
			const FTraversalInfo& Info,
			bool bPrimitiveShape = false,
			const TArray<FString>& AssemblyDependencies = {}
		);

		void AddLODMeshNodes(
			const UE::FUsdPrim& Prim,
			UInterchangeBaseNodeContainer& NodeContainer,
			UInterchangeSceneNode& LODContainerSceneNode,
			UInterchangeUsdTranslatorSettings* TranslatorSettings,
			FTraversalInfo Info
		);

		TArray<FString> AddVolumeNodes(
			const UE::FUsdPrim& InPrim,
			UInterchangeUsdTranslatorSettings* TranslatorSettings,
			UInterchangeBaseNodeContainer& InNodeContainer,
			FString& OutMaterialInstanceUid,
			bool& bOutNeedsFrameTrack
		);

		TOptional<FString> AddGroomNode(
			const UE::FUsdPrim& Prim,
			UInterchangeBaseNodeContainer& NodeContainer
		);

		FString AddSpatialAudioNode(
			const UE::FUsdPrim& Prim,
			UInterchangeBaseNodeContainer& NodeContainer
		);

		void AddSkeletonNodes(
			const UE::FUsdPrim& Prim,
			UInterchangeSceneNode& SkeletonPrimNode,
			UInterchangeBaseNodeContainer& NodeContainer,
			FTraversalInfo& Info
		);

		/**
		 * Search for a MaterialX file embedded in the file, in case there's none, create a ShaderGraph using MaterialX
		 * @param GeomPropValueNames - Array of the <geompropvalue> node names from the shader graph that has been converted to <image> nodes (used
		 * for baking later on by the Factory)
		 */
		bool AddMaterialXShaderGraph(
			const UE::FUsdPrim& InPrim,
			UInterchangeUsdTranslatorSettings* TranslatorSettings,
			UInterchangeBaseNodeContainer& NodeContainer,
			TArray<FUsdMaterialXShaderGraph::FGeomProp>& OutGeomPropValues
		);

		void AddNaniteAssemblyHierarchy(const UE::FUsdPrim& Prim,
			UInterchangeUsdTranslatorSettings* TranslatorSettings,
			UInterchangeBaseNodeContainer& NodeContainer,
			UInterchangeSceneNode* ParentSceneNode,
			FTraversalInfo& Info);

		/**
		 * If we're not translating a decompressed USD root, returns TexturePathOnDisk.
		 * If we are translating a decompressed USD root, returns the path to the USDZ file itself.
		 *
		 * The intent here is that in the USDZ case the texture filepath will point at a temp file on disk, that we may dispose
		 * of later after importing. In order to allow reimporting the texture at a later time, we'll just put the USDZ path
		 * itself as it's source path, and tweak the USD translator to know what to do with this
		 */
		FString GetTextureSourcePath(const FString& TexturePathOnDisk);

		/** If we decompressed a USDZ file to a temp folder this will delete everything from that folder */
		void CleanUpDecompressedUSDZFolder();

		void SetupTranslationContext(const UInterchangeUsdTranslatorSettings& Settings);

		void SetupInfoCache();

		UE::FUsdPrim TryGettingInactiveLODPrim(const FString& PrimPath);

		void FinalizeLODContainerTraversal(
			UInterchangeBaseNodeContainer& NodeContainer,
			const FTraversalInfo& Info,
			const UInterchangeSceneNode* SceneNodeWithLODs
		);

		template<typename MeshOrSceneNode>
		void SetSlotMaterialDependencies(
			MeshOrSceneNode* MeshNode,
			const UsdUtils::FUsdPrimMaterialAssignmentInfo& MaterialAssignments,
			UInterchangeUsdTranslatorSettings* TranslatorSettings,
			UInterchangeBaseNodeContainer& NodeContainer
		);

	public:
		// We have to keep a stage reference so that we can parse the payloads after Translate() complete.
		// ReleaseSource() clears this member, once translation is complete.
		UE::FUsdStage UsdStage;
		TSharedPtr<FUsdSchemaTranslationContext> TranslationContext;
		FUsdInfoCache* InfoCache = nullptr;

		// Owned by the translator itself
		TObjectPtr<UInterchangeResultsContainer> ResultsContainer;

		mutable FRWLock SkeletalMeshDescriptionsLock;
		TMap<FString, FMeshDescription> PayloadKeyToSkeletalMeshDescriptions;

		// We store temp stages in here that we open in order to parse stuff inside of inactive variants of our main UsdStage.
		// We do this because the payload data are retrieved concurrently, and toggling variants mutates the current stage.
		mutable FRWLock PrimPathToVariantToStageLock;
		TMap<FString, TMap<FString, UE::FUsdStage>> PrimPathToVariantToStage;

		FString USDZFilePath;
		FString DecompressedUSDZRoot;

		// On UInterchangeUSDTranslator::Translate we set this up based on our TranslatorSettings, and then
		// we can reuse it (otherwise we have to keep converting the FNames into Tokens all the time)
		UsdToUnreal::FUsdMeshConversionOptions CachedMeshConversionOptions;

		TMap<FString, UsdUtils::FUsdPrimMaterialAssignmentInfo> CachedMaterialAssignments;

		// We fill this in while we're translating a LOD container so that we can do some post-processing inside FinalizeLODContainerTraversal
		TMap<UInterchangeSceneNode*, TArray<UInterchangeSceneNode*>> LODVariantSetNodeToLodMeshNodes;

		// When traversing we'll generate FTraversalInfo objects. If we need to (e.g. for skinned meshes),
		// we'll store the info for that translated node here, so we don't have to recompute it when returning
		// the payload data.
		// Note: We only do this when needed: This shouldn't have data for every prim in the stage.
		TMap<FString, FTraversalInfo> NodeUidToCachedTraversalInfo;
		mutable FRWLock CachedTraversalInfoLock;

		// This node eventually becomes a LevelSequence, and all track nodes are connected to it.
		// For now we only generate a single LevelSequence per stage though, so we'll keep track of this
		// here for easy access when parsing the tracks
		UInterchangeAnimationTrackSetNode* CurrentTrackSet = nullptr;

		// Map of translators that we call in the GetTexturePayload, the key has no real meaning, it's just here to avoid having duplicates and
		// calling several times the Translate function
		TMap<FString, TStrongObjectPtr<UInterchangeTranslatorBase>> Translators;

		// We stash the info we collected from each Volume prim path here, as we'll reuse it between translation and retrieving the payloads
		TMap<FString, TArray<UsdUtils::FVolumePrimInfo>> PrimPathToVolumeInfo;

	private:
		TMap<FString, TArray<FUsdMaterialXShaderGraph::FGeomProp>> MaterialUidToGeomProps;

		// Used within a translation. We cache these because we make a volume node *per .vdb file*, and on the USD side we may have
		// any number of Volume prims internally using the same .vdb file, and we want to share these whenever possible
		TMap<FString, TMap<FString, UInterchangeVolumeNode*>> VolumeFilepathToAnimationIDToNode;
	};

	// Adds a numbered suffix (if needed) to NodeUid to make sure there is nothing with that ID within NodeContainer.
	// Does not add the ID to the NodeContainer.
	void MakeNodeUidUniqueInContainer(FString& NodeUid, const UInterchangeBaseNodeContainer& NodeContainer)
	{
		if (!NodeContainer.IsNodeUidValid(NodeUid) || NodeUid == UInterchangeBaseNode::InvalidNodeUid())
		{
			return;
		}

		int32 Suffix = 0;
		FString Result;
		do
		{
			Result = FString::Printf(TEXT("%s_%d"), *NodeUid, Suffix++);
		} while (NodeContainer.IsNodeUidValid(Result));

		NodeUid = Result;
	}

	template<typename USDType, typename InterchangeType = USDType>
	InterchangeType CoerceToInterchangeType(const USDType& USDValue)
	{
		if constexpr (std::is_same_v<USDType, FMatrix2D> && std::is_same_v<InterchangeType, FMatrix44d>)
		{
			FMatrix44d Result{ EForceInit::ForceInitToZero };
			Result.M[0][0] = USDValue.Row0.X;
			Result.M[0][1] = USDValue.Row0.Y;

			Result.M[1][0] = USDValue.Row1.X;
			Result.M[1][1] = USDValue.Row1.Y;
			return Result;
		}
		else if constexpr (std::is_same_v<USDType, FMatrix3D> && std::is_same_v<InterchangeType, FMatrix44d>)
		{
			FMatrix44d Result{ EForceInit::ForceInitToZero };
			Result.M[0][0] = USDValue.Row0.X;
			Result.M[0][1] = USDValue.Row0.Y;
			Result.M[0][2] = USDValue.Row0.Z;

			Result.M[1][0] = USDValue.Row1.X;
			Result.M[1][1] = USDValue.Row1.Y;
			Result.M[1][2] = USDValue.Row1.Z;

			Result.M[2][0] = USDValue.Row2.X;
			Result.M[2][1] = USDValue.Row2.Y;
			Result.M[2][2] = USDValue.Row2.Z;
			return Result;
		}
		else
		{
			return (InterchangeType)USDValue;
		}
	};

	template<typename USDType, typename InterchangeType = USDType>
	void AddUserAttribute(const FString& KeyName, const UE::FVtValue& VtValue, UInterchangeSceneNode* Node)
	{
		const USDType USDValue = VtValue.Get<USDType>();
		const TOptional<FString> PayloadKey;

		if constexpr (std::is_same_v<InterchangeType, USDType>)
		{
			UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(Node, KeyName, USDValue, PayloadKey);
		}
		else if constexpr (TIsTArray<USDType>::Value)
		{
			static_assert(TIsTArray<InterchangeType>::Value);

			InterchangeType InterchangeArray;
			InterchangeArray.SetNum(USDValue.Num());
			for (int32 Index = 0; Index < USDValue.Num(); ++Index)
			{
				InterchangeArray[Index] = CoerceToInterchangeType<typename USDType::ElementType, typename InterchangeType::ElementType>(USDValue[Index]);
			}

			UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(Node, KeyName, InterchangeArray, PayloadKey);
		}
		else
		{
			const InterchangeType InterchangeValue = CoerceToInterchangeType<USDType, InterchangeType>(USDValue);
			UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(Node, KeyName, InterchangeValue, PayloadKey);
		}
	};

	// Note: We could have just used the Cpp type names here instead of the value type token, but doing the
	// latter lets us handle the color types as FLinearColor, which is probably more useful
	// References:
	// - https://openusd.org/docs/api/_usd__page__datatypes.html
	// - EAttributeTypes declaration
	// - Engine/Source/Runtime/Interchange/Core/Private/Tests/StorageTest.cpp
	using AddUserAttributeFunc = TFunction<void(const FString&, const UE::FVtValue&, UInterchangeSceneNode*)>;
	static TMap<FName, AddUserAttributeFunc> AddUserAttributeFuncs = {
		{TEXT("bool"),  					AddUserAttribute<bool>},

		{TEXT("uchar"), 					AddUserAttribute<uint8>},
		{TEXT("unsigned char"), 			AddUserAttribute<uint8>},
		{TEXT("int"), 						AddUserAttribute<int32>},
		{TEXT("unsigned int"), 				AddUserAttribute<uint32>},
		{TEXT("uint"), 						AddUserAttribute<uint32>},
		{TEXT("int64"), 					AddUserAttribute<int64>},
		{TEXT("__int64"), 					AddUserAttribute<int64>},
		{TEXT("unsigned __int64"), 			AddUserAttribute<uint64>},
		{TEXT("uint64"), 					AddUserAttribute<uint64>},

		{TEXT("half"),	 					AddUserAttribute<FFloat16>},
		{TEXT("pxr_half::half"),	 		AddUserAttribute<FFloat16>},
		{TEXT("float"), 					AddUserAttribute<float>},
		{TEXT("double"), 					AddUserAttribute<double>},
		{TEXT("timecode"), 					AddUserAttribute<FSdfTimeCode, double>},
		{TEXT("SdfTimeCode"), 				AddUserAttribute<FSdfTimeCode, double>},

		{TEXT("string"), 					AddUserAttribute<FString>},
		{TEXT("token"), 					AddUserAttribute<FName>},
		{TEXT("TfToken"), 					AddUserAttribute<FName>},
		{TEXT("asset"), 					AddUserAttribute<FSdfAssetPath, FString>},
		{TEXT("SdfAssetPath"), 				AddUserAttribute<FSdfAssetPath, FString>},

		{TEXT("matrix2d"), 					AddUserAttribute<FMatrix2D, FMatrix44d>},
		{TEXT("GfMatrix2d"), 				AddUserAttribute<FMatrix2D, FMatrix44d>},
		{TEXT("matrix3d"), 					AddUserAttribute<FMatrix3D, FMatrix44d>},
		{TEXT("GfMatrix3d"), 				AddUserAttribute<FMatrix3D, FMatrix44d>},
		{TEXT("matrix4d"), 					AddUserAttribute<FMatrix44d>},
		{TEXT("GfMatrix4d"), 				AddUserAttribute<FMatrix44d>},
		{TEXT("frame4d"), 					AddUserAttribute<FMatrix44d>},

		{TEXT("quath"), 					AddUserAttribute<FQuat4h, FQuat4f>},
		{TEXT("GfQuath"), 					AddUserAttribute<FQuat4h, FQuat4f>},
		{TEXT("quatf"), 					AddUserAttribute<FQuat4f>},
		{TEXT("GfQuatf"), 					AddUserAttribute<FQuat4f>},
		{TEXT("quatd"), 					AddUserAttribute<FQuat4d>},
		{TEXT("GfQuatd"), 					AddUserAttribute<FQuat4d>},

		{TEXT("half2"), 					AddUserAttribute<FVector2DHalf>},
		{TEXT("GfVec2h"), 					AddUserAttribute<FVector2DHalf>},
		{TEXT("float2"), 					AddUserAttribute<FVector2f>},
		{TEXT("GfVec2f"), 					AddUserAttribute<FVector2f>},
		{TEXT("double2"), 					AddUserAttribute<FVector2d>},
		{TEXT("GfVec2d"), 					AddUserAttribute<FVector2d>},
		{TEXT("int2"), 						AddUserAttribute<FIntPoint>},
		{TEXT("GfVec2i"), 					AddUserAttribute<FIntPoint>},

		{TEXT("half3"), 					AddUserAttribute<FVector3h, FVector3f>},
		{TEXT("GfVec3h"), 					AddUserAttribute<FVector3h, FVector3f>},
		{TEXT("point3h"), 					AddUserAttribute<FVector3h, FVector3f>},
		{TEXT("normal3h"), 					AddUserAttribute<FVector3h, FVector3f>},
		{TEXT("vector3h"), 					AddUserAttribute<FVector3h, FVector3f>},
		{TEXT("color3h"), 					AddUserAttribute<FVector3h, FLinearColor>},

		{TEXT("float3"), 					AddUserAttribute<FVector3f>},
		{TEXT("GfVec3f"), 					AddUserAttribute<FVector3f>},
		{TEXT("point3f"), 					AddUserAttribute<FVector3f>},
		{TEXT("normal3f"), 					AddUserAttribute<FVector3f>},
		{TEXT("vector3f"), 					AddUserAttribute<FVector3f>},
		{TEXT("color3f"), 					AddUserAttribute<FVector3f, FLinearColor>},

		{TEXT("double3"), 					AddUserAttribute<FVector3d>},
		{TEXT("GfVec3d"), 					AddUserAttribute<FVector3d>},
		{TEXT("point3d"), 					AddUserAttribute<FVector3d>},
		{TEXT("normal3d"), 					AddUserAttribute<FVector3d>},
		{TEXT("vector3d"), 					AddUserAttribute<FVector3d>},
		{TEXT("color3d"), 					AddUserAttribute<FVector3d, FLinearColor>},

		{TEXT("int3"), 						AddUserAttribute<FIntVector>},
		{TEXT("GfVec3i"), 					AddUserAttribute<FIntVector>},

		{TEXT("half4"), 					AddUserAttribute<FVector4h, FVector4f>},
		{TEXT("GfVec4h"), 					AddUserAttribute<FVector4h, FVector4f>},
		{TEXT("float4"), 					AddUserAttribute<FVector4f>},
		{TEXT("GfVec4f"), 					AddUserAttribute<FVector4f>},
		{TEXT("double4"), 					AddUserAttribute<FVector4d>},
		{TEXT("GfVec4d"), 					AddUserAttribute<FVector4d>},
		{TEXT("int4"), 						AddUserAttribute<FIntRect>},
		{TEXT("GfVec4i"), 					AddUserAttribute<FIntRect>},
		{TEXT("color4h"), 					AddUserAttribute<FVector4h, FLinearColor>},
		{TEXT("color4f"), 					AddUserAttribute<FVector4f, FLinearColor>},
		{TEXT("color4d"), 					AddUserAttribute<FVector4d, FLinearColor>},

		{TEXT("bool[]"),  					AddUserAttribute<TArray<bool>>},
		{TEXT("VtArray<bool>"),  			AddUserAttribute<TArray<bool>>},

		{TEXT("uchar[]"), 					AddUserAttribute<TArray<uint8>>},
		{TEXT("VtArray<unsigned char>"), 	AddUserAttribute<TArray<uint8>>},
		{TEXT("int[]"), 					AddUserAttribute<TArray<int32>>},
		{TEXT("VtArray<int>"), 				AddUserAttribute<TArray<int32>>},
		{TEXT("uint[]"), 					AddUserAttribute<TArray<uint32>>},
		{TEXT("VtArray<unsigned int>"), 	AddUserAttribute<TArray<uint32>>},
		{TEXT("int64[]"), 					AddUserAttribute<TArray<int64>>},
		{TEXT("VtArray<__int64>"), 			AddUserAttribute<TArray<int64>>},
		{TEXT("uint64[]"), 					AddUserAttribute<TArray<uint64>>},
		{TEXT("VtArray<unsigned __int64>"), AddUserAttribute<TArray<uint64>>},

		{TEXT("half[]"),	 				AddUserAttribute<TArray<FFloat16>>},
		{TEXT("VtArray<pxr_half::half>"),	AddUserAttribute<TArray<FFloat16>>},
		{TEXT("float[]"), 					AddUserAttribute<TArray<float>>},
		{TEXT("VtArray<float>"), 			AddUserAttribute<TArray<float>>},
		{TEXT("double[]"), 					AddUserAttribute<TArray<double>>},
		{TEXT("VtArray<double>"), 			AddUserAttribute<TArray<double>>},
		{TEXT("timecode[]"), 				AddUserAttribute<TArray<FSdfTimeCode>, TArray<double>>},
		{TEXT("VtArray<SdfTimeCode>"), 		AddUserAttribute<TArray<FSdfTimeCode>, TArray<double>>},

		{TEXT("string[]"), 					AddUserAttribute<TArray<FString>>},
		{TEXT("VtArray<string>"), 			AddUserAttribute<TArray<FString>>},
		{TEXT("token[]"), 					AddUserAttribute<TArray<FName>>},
		{TEXT("VtArray<TfToken>"), 			AddUserAttribute<TArray<FName>>},
		{TEXT("asset[]"), 					AddUserAttribute<TArray<FSdfAssetPath>, TArray<FString>>},
		{TEXT("VtArray<SdfAssetPath>"), 	AddUserAttribute<TArray<FSdfAssetPath>, TArray<FString>>},

		{TEXT("matrix2d[]"), 				AddUserAttribute<TArray<FMatrix2D>, TArray<FMatrix44d>>},
		{TEXT("VtArray<GfMatrix2d>"), 		AddUserAttribute<TArray<FMatrix2D>, TArray<FMatrix44d>>},
		{TEXT("matrix3d[]"), 				AddUserAttribute<TArray<FMatrix3D>, TArray<FMatrix44d>>},
		{TEXT("VtArray<GfMatrix3d>"), 		AddUserAttribute<TArray<FMatrix3D>, TArray<FMatrix44d>>},
		{TEXT("matrix4d[]"), 				AddUserAttribute<TArray<FMatrix44d>>},
		{TEXT("VtArray<GfMatrix4d>"), 		AddUserAttribute<TArray<FMatrix44d>>},
		{TEXT("frame4d[]"), 				AddUserAttribute<TArray<FMatrix44d>>},

		{TEXT("quath[]"), 					AddUserAttribute<TArray<FQuat4h>, TArray<FQuat4f>>},
		{TEXT("VtArray<GfQuath>"), 			AddUserAttribute<TArray<FQuat4h>, TArray<FQuat4f>>},
		{TEXT("quatf[]"), 					AddUserAttribute<TArray<FQuat4f>>},
		{TEXT("VtArray<GfQuatf>"), 			AddUserAttribute<TArray<FQuat4f>>},
		{TEXT("quatd[]"), 					AddUserAttribute<TArray<FQuat4d>>},
		{TEXT("VtArray<GfQuatd>"), 			AddUserAttribute<TArray<FQuat4d>>},

		{TEXT("half2[]"), 					AddUserAttribute<TArray<FVector2DHalf>>},
		{TEXT("VtArray<GfVec2h>"), 			AddUserAttribute<TArray<FVector2DHalf>>},
		{TEXT("float2[]"), 					AddUserAttribute<TArray<FVector2f>>},
		{TEXT("VtArray<GfVec2f>"), 			AddUserAttribute<TArray<FVector2f>>},
		{TEXT("double2[]"), 				AddUserAttribute<TArray<FVector2d>>},
		{TEXT("VtArray<GfVec2d>"), 			AddUserAttribute<TArray<FVector2d>>},
		{TEXT("int2[]"), 					AddUserAttribute<TArray<FIntPoint>>},
		{TEXT("VtArray<GfVec2i>"), 			AddUserAttribute<TArray<FIntPoint>>},

		{TEXT("half3[]"), 					AddUserAttribute<TArray<FVector3h>, TArray<FVector3f>>},
		{TEXT("VtArray<GfVec3h>"), 			AddUserAttribute<TArray<FVector3h>, TArray<FVector3f>>},
		{TEXT("point3h[]"), 				AddUserAttribute<TArray<FVector3h>, TArray<FVector3f>>},
		{TEXT("normal3h[]"), 				AddUserAttribute<TArray<FVector3h>, TArray<FVector3f>>},
		{TEXT("vector3h[]"), 				AddUserAttribute<TArray<FVector3h>, TArray<FVector3f>>},
		{TEXT("color3h[]"), 				AddUserAttribute<TArray<FVector3h>, TArray<FLinearColor>>},

		{TEXT("float3[]"), 					AddUserAttribute<TArray<FVector3f>>},
		{TEXT("VtArray<GfVec3f>"), 			AddUserAttribute<TArray<FVector3f>>},
		{TEXT("point3f[]"), 				AddUserAttribute<TArray<FVector3f>>},
		{TEXT("normal3f[]"), 				AddUserAttribute<TArray<FVector3f>>},
		{TEXT("vector3f[]"), 				AddUserAttribute<TArray<FVector3f>>},
		{TEXT("color3f[]"), 				AddUserAttribute<TArray<FVector3f>, TArray<FLinearColor>>},

		{TEXT("double3[]"), 				AddUserAttribute<TArray<FVector3d>>},
		{TEXT("VtArray<GfVec3d>"), 			AddUserAttribute<TArray<FVector3d>>},
		{TEXT("point3d[]"), 				AddUserAttribute<TArray<FVector3d>>},
		{TEXT("normal3d[]"), 				AddUserAttribute<TArray<FVector3d>>},
		{TEXT("vector3d[]"), 				AddUserAttribute<TArray<FVector3d>>},
		{TEXT("color3d[]"), 				AddUserAttribute<TArray<FVector3d>, TArray<FLinearColor>>},

		{TEXT("int3[]"), 					AddUserAttribute<TArray<FIntVector>>},
		{TEXT("VtArray<GfVec3i>"), 			AddUserAttribute<TArray<FIntVector>>},

		{TEXT("half4[]"), 					AddUserAttribute<TArray<FVector4h>, TArray<FVector4f>>},
		{TEXT("VtArray<GfVec4h>"), 			AddUserAttribute<TArray<FVector4h>, TArray<FVector4f>>},
		{TEXT("float4[]"), 					AddUserAttribute<TArray<FVector4f>>},
		{TEXT("VtArray<GfVec4f>"), 			AddUserAttribute<TArray<FVector4f>>},
		{TEXT("double4[]"), 				AddUserAttribute<TArray<FVector4d>>},
		{TEXT("VtArray<GfVec4d>"), 			AddUserAttribute<TArray<FVector4d>>},
		{TEXT("int4[]"), 					AddUserAttribute<TArray<FIntRect>>},
		{TEXT("VtArray<GfVec4i>"), 			AddUserAttribute<TArray<FIntRect>>},
		{TEXT("color4h[]"), 				AddUserAttribute<TArray<FVector4h>, TArray<FLinearColor>>},
		{TEXT("color4f[]"), 				AddUserAttribute<TArray<FVector4f>, TArray<FLinearColor>>},
		{TEXT("color4d[]"), 				AddUserAttribute<TArray<FVector4d>, TArray<FLinearColor>>},
	};

	void TranslateAttributes(const UE::FUsdPrim& Prim, UInterchangeSceneNode* Node, const FString& AllowedAttributeRegex)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TranslateAttributes)

			FRegexPattern RegexPattern{ AllowedAttributeRegex };

		for (const UE::FUsdAttribute& Attr : Prim.GetAttributes())
		{
			if (!Attr.HasAuthoredValue())
			{
				continue;
			}

			const FString AttrName = Attr.GetName().ToString();

			FRegexMatcher RegexMatcher{ RegexPattern, AttrName };
			if (!RegexMatcher.FindNext())
			{
				continue;
			}

			if (AddUserAttributeFunc Func = AddUserAttributeFuncs.FindRef(Attr.GetTypeName()))
			{
				UE::FVtValue Value;
				if (Attr.Get(Value)) // Always check for an opinion on the default time code
				{
					Func(AttrName, Value, Node);
				}
			}
		}
	}

	void TranslateMetadata(const UE::FUsdPrim& Prim, UInterchangeSceneNode* Node, const FString& AllowedMetadataRegex)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(TranslateMetadata)

			FRegexPattern RegexPattern{ AllowedMetadataRegex };

		TFunction<void(const TMap<FString, UE::FVtValue>&, const FString&)> ParseMetadataMap;
		ParseMetadataMap = [&ParseMetadataMap, &RegexPattern, Node](const TMap<FString, UE::FVtValue>& MetadataMap, const FString& ConcatParentKey)
		{
			for (const TPair<FString, UE::FVtValue>& Pair : MetadataMap)
			{
				const FString FullKey = ConcatParentKey.IsEmpty() ? Pair.Key : FString::Printf(TEXT("%s:%s"), *ConcatParentKey, *Pair.Key);
				const FString TypeName = Pair.Value.GetTypeName();

				if (TypeName == TEXT("VtDictionary"))
				{
					TMap<FString, UE::FVtValue> NestedMap = Pair.Value.Get<TMap<FString, UE::FVtValue>>();
					ParseMetadataMap(NestedMap, FullKey);
				}
				else if (AddUserAttributeFunc Func = AddUserAttributeFuncs.FindRef(*TypeName))
				{
					// Only match when parsing leaf keys, because a key may not match just the parent nested map key directly and
					// would have otherwise prevented us from stepping into it (e.g. the regex "customData:my" will not match 
					// "customData", but it would match "customData:myNestedMap")
					FRegexMatcher RegexMatcher{ RegexPattern, FullKey };
					if (!RegexMatcher.FindNext())
					{
						continue;
					}

					Func(FullKey, Pair.Value, Node);
				}
			}
		};

		const FString ConcatParentKey;
		ParseMetadataMap(Prim.GetAllAuthoredMetadata(), ConcatParentKey);
	}

	UInterchangeSceneNode* CreateSceneNode(
		const UE::FUsdPrim& Prim, 
		UInterchangeUsdTranslatorSettings* TranslatorSettings,
		UInterchangeBaseNodeContainer& NodeContainer, 
		const FString& SceneNodeUid, 
		const FString& DisplayLabel,
		const FString& ParentNodeUid
	)
	{
		UInterchangeSceneNode* SceneNode = NewObject<UInterchangeSceneNode>(&NodeContainer);
		NodeContainer.SetupNode(SceneNode, SceneNodeUid, DisplayLabel, EInterchangeNodeContainerType::TranslatedScene, ParentNodeUid);

		// Store our purpose here to be filtered on the pipeline
		const EUsdPurpose PrimPurpose = IUsdPrim::GetPurpose(Prim);
		if (PrimPurpose != EUsdPurpose::Default)
		{
			SceneNode->AddInt32Attribute(UE::Interchange::USD::GeometryPurposeIdentifier, (int32)PrimPurpose);
		}

		// Store our prim kind as well, if we have any (this becomes the empty string if the prim has no authored kind)
		const FString KindString = UsdToUnreal::ConvertToken(IUsdPrim::GetKind(Prim));
		if (!KindString.IsEmpty())
		{
			const FString UserDefinedAttributeName = TEXT("kind");
			TOptional<FString> PayloadKey;
			UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(SceneNode, UserDefinedAttributeName, KindString, PayloadKey);
		}

		if (TranslatorSettings->bTranslatePrimAttributes)
		{
			TranslateAttributes(Prim, SceneNode, TranslatorSettings->AttributeRegexFilter);
		}

		if (TranslatorSettings->bTranslatePrimMetadata)
		{
			TranslateMetadata(Prim, SceneNode, TranslatorSettings->MetadataRegexFilter);
		}

		return SceneNode;
	}

	bool IsValidLODName(const FString& PrimName)
	{
		return PrimName.Len() > LODString.Len() && PrimName.StartsWith(LODString) && PrimName.RightChop(LODString.Len()).IsNumeric();
	}

	int32 GetLODIndexFromName(FString Name)
	{
		if (!Name.RemoveFromStart(LODString))
		{
			return INDEX_NONE;
		}

		if (!Name.IsNumeric())
		{
			return INDEX_NONE;
		}

		int32 Index = INDEX_NONE;
		LexFromString(Index, *Name);
		return Index;
	}

	UE::FUsdPrim GetLODMesh(const UE::FUsdPrim& LODContainerPrim, const FString& LODName)
	{
		const UE::FSdfPath IdealMeshPrimPath = LODContainerPrim.GetPrimPath().AppendChild(*LODName);

		UE::FUsdPrim Prim = LODContainerPrim.GetStage().GetPrimAtPath(IdealMeshPrimPath);
		if (Prim && Prim.IsActive() && Prim.IsA(TEXT("Mesh")))
		{
			return Prim;
		}

		return {};
	};

	TArray<UE::FUsdPrim> CheckLodAPIAndGetChildren(const UE::FUsdPrim& Prim, UInterchangeSceneNode* SceneNode)
	{
		if (!SceneNode || !Prim.HasAPI(*UsdToUnreal::ConvertToken(UnrealIdentifiers::LodSubtreeAPI)))
		{
			constexpr bool bTraverseInstanceProxies = true;
			return  Prim.GetFilteredChildren(bTraverseInstanceProxies);
		}

		UE::FUsdRelationship LevelsRel = Prim.GetRelationship(*UsdToUnreal::ConvertToken(UnrealIdentifiers::UnrealLodSubtreeLevels));
		if (!LevelsRel)
		{   
			UE_LOG(
				LogUsd, Warning, TEXT("LOD subtree '%s' is missing required relationship '%s'"),
				*Prim.GetPrimPath().GetString(),
				*UsdToUnreal::ConvertToken(UnrealIdentifiers::UnrealLodSubtreeLevels)
			);
			return {};
		}

		TArray<UE::FSdfPath> Targets;
		if (!LevelsRel.GetTargets(Targets) || Targets.IsEmpty())
		{
			UE_LOG(
				LogUsd, Warning, TEXT("LOD subtree '%s' has no LODs specified by relationship '%s'"),
				*Prim.GetPrimPath().GetString(),
				*UsdToUnreal::ConvertToken(UnrealIdentifiers::UnrealLodSubtreeLevels)
			);
			return {};
		}

		TArray<UE::FUsdPrim> SubtreePrims;

		for (const UE::FSdfPath& TargetPath : Targets)
		{
			// Members must be direct children
			if (TargetPath.GetParentPath() != Prim.GetPrimPath())
			{
				UE_LOG(
					LogUsd, Warning, TEXT("Ignoring LOD '%s' that is not a direct child of '%s'"),
					*TargetPath.GetString(), *Prim.GetPrimPath().GetString()
				);
				continue;
			}
				
			FUsdPrim SubtreePrim = Prim.GetStage().GetPrimAtPath(TargetPath);
			if (!SubtreePrim)
			{
				UE_LOG(
					LogUsd, Warning, TEXT("Ignoring invalid or missing LOD '%s' specified by '%s'"),
					*TargetPath.GetString(), *Prim.GetPrimPath().GetString()
				);
			}
			
			SubtreePrims.Emplace(SubtreePrim);
		}

		if (!SubtreePrims.IsEmpty())
		{
			SceneNode->AddSpecializedType(UE::Interchange::FSceneNodeStaticData::GetLodGroupSpecializeTypeString());
		}

		return SubtreePrims;
	}

	bool CheckAndChopPayloadPrefix(FString& PayloadKey, const FString& Prefix)
	{
		if (PayloadKey.StartsWith(Prefix))
		{
			// Remove the prefix from the payload key
			PayloadKey = PayloadKey.RightChop(Prefix.Len());

			return true;
		}

		return false;
	}

	FString HashAnimPayloadQuery(const Interchange::FAnimationPayloadQuery& Query)
	{
		FSHAHash Hash;
		FSHA1 SHA1;

		// TODO: Is there a StringView alternative?
		FString SkeletonPrimPath;
		FString JointIndexStr;
		bool bSplit = Query.PayloadKey.UniqueId.Split(TEXT("\\"), &SkeletonPrimPath, &JointIndexStr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (!bSplit)
		{
			return {};
		}

		SHA1.UpdateWithString(*SkeletonPrimPath, SkeletonPrimPath.Len());

		SHA1.Update(reinterpret_cast<const uint8*>(&Query.TimeDescription.BakeFrequency), sizeof(Query.TimeDescription.BakeFrequency));
		SHA1.Update(reinterpret_cast<const uint8*>(&Query.TimeDescription.RangeStartSecond), sizeof(Query.TimeDescription.RangeStartSecond));
		SHA1.Update(reinterpret_cast<const uint8*>(&Query.TimeDescription.RangeStopSecond), sizeof(Query.TimeDescription.RangeStopSecond));

		SHA1.Final();
		SHA1.GetHash(&Hash.Hash[0]);
		return Hash.ToString();
	}

	FString GetMorphTargetMeshNodeUid(const FString& MeshPrimPath, int32 MeshBlendShapeIndex, const FString& InbetweenName = FString{})
	{
		return MakeNodeUid(FString::Printf(TEXT("%s%s\\%d\\%s"), *MorphTargetPrefix, *MeshPrimPath, MeshBlendShapeIndex, *InbetweenName));
	}

	FString GetMorphTargetMeshPayloadKey(
		bool bIsInsideLOD,
		const FString& MeshPrimPath,
		int32 MeshBlendShapeIndex,
		const FString& InbetweenName = FString{}
	)
	{
		return FString::Printf(TEXT("%s%s\\%d\\%s"), bIsInsideLOD ? *LODPrefix : TEXT(""), *MeshPrimPath, MeshBlendShapeIndex, *InbetweenName);
	}

	// TODO: Cleanup/unify/standardize these payload manipulating functions (don't add/remove prefixes everywhere but have a standard format, etc.)
	bool ParseMorphTargetMeshPayloadKey(
		FString InPayloadKey,
		bool& bOutIsLODMesh,
		FString& OutMeshPrimPath,
		int32& OutBlendShapeIndex,
		FString& OutInbetweenName
	)
	{
		bool bIsLODMesh = CheckAndChopPayloadPrefix(InPayloadKey, LODPrefix);

		// These payload keys are generated by GetMorphTargetMeshPayloadKey(), and so should take the form
		// "<mesh prim path>\<mesh blend shape index>\<optional inbetween name>"
		const bool bCullEmpty = false;
		TArray<FString> PayloadKeyTokens;
		InPayloadKey.ParseIntoArray(PayloadKeyTokens, TEXT("\\"), bCullEmpty);
		if (PayloadKeyTokens.Num() != 3)
		{
			return false;
		}

		const FString& MeshPrimPath = PayloadKeyTokens[0];
		const FString& BlendShapeIndexStr = PayloadKeyTokens[1];
		const FString& InbetweenName = PayloadKeyTokens[2];

		int32 BlendShapeIndex = INDEX_NONE;
		bool bLexed = LexTryParseString(BlendShapeIndex, *BlendShapeIndexStr);
		if (!bLexed)
		{
			return false;
		}

		bOutIsLODMesh = bIsLODMesh;
		OutMeshPrimPath = MeshPrimPath;
		OutBlendShapeIndex = BlendShapeIndex;
		OutInbetweenName = InbetweenName;
		return true;
	}

	FString GetMorphTargetCurvePayloadKey(const FString& SkeletonPrimPath, int32 SkelAnimChannelIndex, const FString& BlendShapePath)
	{
		return FString::Printf(TEXT("%s\\%d\\%s"), *SkeletonPrimPath, SkelAnimChannelIndex, *BlendShapePath);
	}

	FString EncodeTexturePayloadKey(const UsdToUnreal::FTextureParameterValue& Value)
	{
		// Encode the compression settings onto the payload key as we need to move that into the
		// payload data within UInterchangeUSDTranslator::GetTexturePayloadData.
		//
		// This should be a temporary thing, and in the future we'll be able to store compression
		// settings directly on the texture translated node
		return Value.TextureFilePath + TEXT("\\") + LexToString((int32)Value.Group);
	}

	bool DecodeTexturePayloadKey(const FString& PayloadKey, FString& OutTextureFilePath, TextureGroup& OutTextureGroup)
	{
		// Use split from end here so that we ignore any backslashes within the file path itself
		FString FilePath;
		FString TextureGroupStr;
		bool bSplit = PayloadKey.Split(TEXT("\\"), &FilePath, &TextureGroupStr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (!bSplit)
		{
			return false;
		}

		OutTextureFilePath = FilePath;

		int32 TempInt;
		if (LexTryParseString<int32>(TempInt, *TextureGroupStr))
		{
			OutTextureGroup = (TextureGroup)TempInt;
		}

		return true;
	}

	void FixMaterialSlotNames(FMeshDescription& MeshDescription, const TArray<UsdUtils::FUsdPrimMaterialSlot>& MeshAssingmentSlots)
	{
		// Fixup material slot names to match the material that is assigned. For Interchange it is better to have the material
		// slot names match what is assigned into them, as it will use those names to "merge identical slots" depending on the
		// import options.
		//
		// Note: These names must also match what is set via MeshNode->SetSlotMaterialDependencyUid(SlotName, MaterialUid)
		FStaticMeshAttributes StaticMeshAttributes(MeshDescription);
		for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < StaticMeshAttributes.GetPolygonGroupMaterialSlotNames().GetNumElements();
			 ++MaterialSlotIndex)
		{
			int32 MaterialIndex = 0;
			LexFromString(MaterialIndex, *StaticMeshAttributes.GetPolygonGroupMaterialSlotNames()[MaterialSlotIndex].ToString());

			if (MeshAssingmentSlots.IsValidIndex(MaterialIndex))
			{
				const FString Source = MeshAssingmentSlots[MaterialIndex].MaterialSource;
				StaticMeshAttributes.GetPolygonGroupMaterialSlotNames()[MaterialSlotIndex] = *Source;
			}
		}
	}

	bool ReadBools(
		const UE::FUsdStage& UsdStage,
		const TArray<double>& UsdTimeSamples,
		const TFunction<bool(double)>& ReaderFunc,
		Interchange::FAnimationPayloadData& OutPayloadData
	)
	{
		OutPayloadData.StepCurves.SetNum(1);
		FInterchangeStepCurve& Curve = OutPayloadData.StepCurves[0];
		TArray<float>& KeyTimes = Curve.KeyTimes;
		TArray<bool>& BooleanKeyValues = Curve.BooleanKeyValues.Emplace();

		KeyTimes.Reserve(UsdTimeSamples.Num());
		BooleanKeyValues.Reserve(UsdTimeSamples.Num());

		const FFrameRate StageFrameRate{static_cast<uint32>(UsdStage.GetTimeCodesPerSecond()), 1};

		double LastTimeSample = TNumericLimits<double>::Lowest();
		for (const double UsdTimeSample : UsdTimeSamples)
		{
			// We never want to evaluate the same time twice
			if (FMath::IsNearlyEqual(UsdTimeSample, LastTimeSample))
			{
				continue;
			}
			LastTimeSample = UsdTimeSample;

			int32 FrameNumber = FMath::FloorToInt(UsdTimeSample);
			float SubFrameNumber = UsdTimeSample - FrameNumber;

			FFrameTime FrameTime{FrameNumber, SubFrameNumber};
			double FrameTimeSeconds = static_cast<float>(StageFrameRate.AsSeconds(FrameTime));

			bool UEValue = ReaderFunc(UsdTimeSample);

			KeyTimes.Add(FrameTimeSeconds);
			BooleanKeyValues.Add(UEValue);
		}

		return true;
	}

	bool ReadFloats(
		const UE::FUsdStage& UsdStage,
		const TArray<double>& UsdTimeSamples,
		const TFunction<float(double)>& ReaderFunc,
		Interchange::FAnimationPayloadData& OutPayloadData
	)
	{
		OutPayloadData.Curves.SetNum(1);
		FRichCurve& Curve = OutPayloadData.Curves[0];

		const FFrameRate StageFrameRate{static_cast<uint32>(UsdStage.GetTimeCodesPerSecond()), 1};
		const ERichCurveInterpMode InterpMode = (UsdStage.GetInterpolationType() == EUsdInterpolationType::Linear)
													? ERichCurveInterpMode::RCIM_Linear
													: ERichCurveInterpMode::RCIM_Constant;

		double LastTimeSample = TNumericLimits<double>::Lowest();
		for (const double UsdTimeSample : UsdTimeSamples)
		{
			// We never want to evaluate the same time twice
			if (FMath::IsNearlyEqual(UsdTimeSample, LastTimeSample))
			{
				continue;
			}
			LastTimeSample = UsdTimeSample;

			int32 FrameNumber = FMath::FloorToInt(UsdTimeSample);
			float SubFrameNumber = UsdTimeSample - FrameNumber;

			FFrameTime FrameTime{FrameNumber, SubFrameNumber};
			double FrameTimeSeconds = static_cast<float>(StageFrameRate.AsSeconds(FrameTime));

			float UEValue = ReaderFunc(UsdTimeSample);

			FKeyHandle Handle = Curve.AddKey(FrameTimeSeconds, UEValue);
			Curve.SetKeyInterpMode(Handle, InterpMode);
		}

		return true;
	}

	bool ReadColors(
		const UE::FUsdStage& UsdStage,
		const TArray<double>& UsdTimeSamples,
		const TFunction<FLinearColor(double)>& ReaderFunc,
		Interchange::FAnimationPayloadData& OutPayloadData
	)
	{
		OutPayloadData.Curves.SetNum(4);
		FRichCurve& RCurve = OutPayloadData.Curves[0];
		FRichCurve& GCurve = OutPayloadData.Curves[1];
		FRichCurve& BCurve = OutPayloadData.Curves[2];
		FRichCurve& ACurve = OutPayloadData.Curves[3];

		const FFrameRate StageFrameRate{static_cast<uint32>(UsdStage.GetTimeCodesPerSecond()), 1};
		const ERichCurveInterpMode InterpMode = (UsdStage.GetInterpolationType() == EUsdInterpolationType::Linear)
													? ERichCurveInterpMode::RCIM_Linear
													: ERichCurveInterpMode::RCIM_Constant;

		double LastTimeSample = TNumericLimits<double>::Lowest();
		for (const double UsdTimeSample : UsdTimeSamples)
		{
			// We never want to evaluate the same time twice
			if (FMath::IsNearlyEqual(UsdTimeSample, LastTimeSample))
			{
				continue;
			}
			LastTimeSample = UsdTimeSample;

			int32 FrameNumber = FMath::FloorToInt(UsdTimeSample);
			float SubFrameNumber = UsdTimeSample - FrameNumber;

			FFrameTime FrameTime{FrameNumber, SubFrameNumber};
			double FrameTimeSeconds = static_cast<float>(StageFrameRate.AsSeconds(FrameTime));

			FLinearColor UEValue = ReaderFunc(UsdTimeSample);

			FKeyHandle RHandle = RCurve.AddKey(FrameTimeSeconds, UEValue.R);
			FKeyHandle GHandle = GCurve.AddKey(FrameTimeSeconds, UEValue.G);
			FKeyHandle BHandle = BCurve.AddKey(FrameTimeSeconds, UEValue.B);
			FKeyHandle AHandle = ACurve.AddKey(FrameTimeSeconds, UEValue.A);

			RCurve.SetKeyInterpMode(RHandle, InterpMode);
			GCurve.SetKeyInterpMode(GHandle, InterpMode);
			BCurve.SetKeyInterpMode(BHandle, InterpMode);
			ACurve.SetKeyInterpMode(AHandle, InterpMode);
		}

		return true;
	}

	bool ReadTransforms(
		const UE::FUsdStage& UsdStage,
		const TArray<double>& UsdTimeSamples,
		const TFunction<FTransform(double)>& ReaderFunc,
		Interchange::FAnimationPayloadData& OutPayloadData
	)
	{
		OutPayloadData.Curves.SetNum(9);
		FRichCurve& TransXCurve = OutPayloadData.Curves[0];
		FRichCurve& TransYCurve = OutPayloadData.Curves[1];
		FRichCurve& TransZCurve = OutPayloadData.Curves[2];
		FRichCurve& RotXCurve = OutPayloadData.Curves[3];
		FRichCurve& RotYCurve = OutPayloadData.Curves[4];
		FRichCurve& RotZCurve = OutPayloadData.Curves[5];
		FRichCurve& ScaleXCurve = OutPayloadData.Curves[6];
		FRichCurve& ScaleYCurve = OutPayloadData.Curves[7];
		FRichCurve& ScaleZCurve = OutPayloadData.Curves[8];

		const FFrameRate StageFrameRate{static_cast<uint32>(UsdStage.GetTimeCodesPerSecond()), 1};
		const ERichCurveInterpMode InterpMode = (UsdStage.GetInterpolationType() == EUsdInterpolationType::Linear)
													? ERichCurveInterpMode::RCIM_Linear
													: ERichCurveInterpMode::RCIM_Constant;
		struct FHeadingHelper
		{
			FRotator Heading;
			bool bHeadingSet = false;
		};
		FHeadingHelper HeadingHelper;

		double LastTimeSample = TNumericLimits<double>::Lowest();
		for (const double UsdTimeSample : UsdTimeSamples)
		{
			// We never want to evaluate the same time twice
			if (FMath::IsNearlyEqual(UsdTimeSample, LastTimeSample))
			{
				continue;
			}
			LastTimeSample = UsdTimeSample;

			int32 FrameNumber = FMath::FloorToInt(UsdTimeSample);
			float SubFrameNumber = UsdTimeSample - FrameNumber;

			FFrameTime FrameTime{FrameNumber, SubFrameNumber};
			double FrameTimeSeconds = static_cast<float>(StageFrameRate.AsSeconds(FrameTime));

			FTransform UEValue = ReaderFunc(UsdTimeSample);
			FVector Location = UEValue.GetLocation();
			FRotator Rotator = UEValue.Rotator();
			FVector Scale = UEValue.GetScale3D();

			if (HeadingHelper.bHeadingSet)
			{
				FMath::WindRelativeAnglesDegrees(HeadingHelper.Heading.Roll, Rotator.Roll);
				FMath::WindRelativeAnglesDegrees(HeadingHelper.Heading.Pitch, Rotator.Pitch);
				FMath::WindRelativeAnglesDegrees(HeadingHelper.Heading.Yaw, Rotator.Yaw);

				FRotator OtherChoice = Rotator.GetEquivalentRotator().GetNormalized();
				float FirstDiff = HeadingHelper.Heading.GetManhattanDistance(Rotator);
				float SecondDiff = HeadingHelper.Heading.GetManhattanDistance(OtherChoice);
				if (SecondDiff < FirstDiff)
				{
					Rotator = OtherChoice;
				}
			}
			else
			{
				HeadingHelper.bHeadingSet = true;
			}

			HeadingHelper.Heading = Rotator;

			FKeyHandle HandleTransX = TransXCurve.AddKey(FrameTimeSeconds, Location.X);
			FKeyHandle HandleTransY = TransYCurve.AddKey(FrameTimeSeconds, Location.Y);
			FKeyHandle HandleTransZ = TransZCurve.AddKey(FrameTimeSeconds, Location.Z);
			FKeyHandle HandleRotX = RotXCurve.AddKey(FrameTimeSeconds, Rotator.Roll);
			FKeyHandle HandleRotY = RotYCurve.AddKey(FrameTimeSeconds, Rotator.Pitch);
			FKeyHandle HandleRotZ = RotZCurve.AddKey(FrameTimeSeconds, Rotator.Yaw);
			FKeyHandle HandleScaleX = ScaleXCurve.AddKey(FrameTimeSeconds, Scale.X);
			FKeyHandle HandleScaleY = ScaleYCurve.AddKey(FrameTimeSeconds, Scale.Y);
			FKeyHandle HandleScaleZ = ScaleZCurve.AddKey(FrameTimeSeconds, Scale.Z);

			TransXCurve.SetKeyInterpMode(HandleTransX, InterpMode);
			TransYCurve.SetKeyInterpMode(HandleTransY, InterpMode);
			TransZCurve.SetKeyInterpMode(HandleTransZ, InterpMode);
			RotXCurve.SetKeyInterpMode(HandleRotX, InterpMode);
			RotYCurve.SetKeyInterpMode(HandleRotY, InterpMode);
			RotZCurve.SetKeyInterpMode(HandleRotZ, InterpMode);
			ScaleXCurve.SetKeyInterpMode(HandleScaleX, InterpMode);
			ScaleYCurve.SetKeyInterpMode(HandleScaleY, InterpMode);
			ScaleZCurve.SetKeyInterpMode(HandleScaleZ, InterpMode);
		}

		return true;
	}

	bool ReadRawTransforms(
		const UE::FUsdStage& UsdStage,
		const TArray<double>& UsdTimeSamples,
		const TFunction<FTransform(double)>& ReaderFunc,
		Interchange::FAnimationPayloadData& OutPayloadData
	)
	{
		OutPayloadData.Transforms.Reserve(UsdTimeSamples.Num());

		double LastTimeSample = TNumericLimits<double>::Lowest();
		for (const double UsdTimeSample : UsdTimeSamples)
		{
			// We never want to evaluate the same time twice
			if (FMath::IsNearlyEqual(UsdTimeSample, LastTimeSample))
			{
				continue;
			}
			LastTimeSample = UsdTimeSample;

			OutPayloadData.Transforms.Emplace(ReaderFunc(UsdTimeSample));
		}

		return true;
	}

	void AddTextureNode(
		const UE::FUsdPrim& Prim,
		const FString& NodeUid,
		const UsdToUnreal::FTextureParameterValue& Value,
		UInterchangeBaseNodeContainer& NodeContainer
	)
	{
		FString PrimPath = UsdUtils::GetPrototypePrimPath(Prim).GetString();
		FString NodeName{FPaths::GetCleanFilename(Value.TextureFilePath)};

		// Check if Node already exist with this ID
		if (const UInterchangeTexture2DNode* Node = Cast<UInterchangeTexture2DNode>(NodeContainer.GetNode(NodeUid)))
		{
			return;
		}

		UInterchangeTexture2DNode* Node = NewObject<UInterchangeTexture2DNode>(&NodeContainer);
		NodeContainer.SetupNode(Node, NodeUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset);
		Node->SetPayLoadKey(EncodeTexturePayloadKey(Value));

		static_assert((int)TextureAddress::TA_Wrap == (int)EInterchangeTextureWrapMode::Wrap);
		static_assert((int)TextureAddress::TA_Clamp == (int)EInterchangeTextureWrapMode::Clamp);
		static_assert((int)TextureAddress::TA_Mirror == (int)EInterchangeTextureWrapMode::Mirror);
		Node->SetCustomWrapU((EInterchangeTextureWrapMode)Value.AddressX);
		Node->SetCustomWrapV((EInterchangeTextureWrapMode)Value.AddressY);

		Node->SetCustomSRGB(Value.GetSRGBValue());

		// Provide the other UDIM tiles
		//
		// Note: There is an bImportUDIM option on UInterchangeGenericTexturePipeline that is exclusively used within
		// UInterchangeGenericTexturePipeline::HandleCreationOfTextureFactoryNode in order to essentially do the exact same
		// thing as we do here. In theory, we shouldn't need to do this then, and in fact it is a bit bad to do so because
		// we will always parse these UDIMs whether the option is enabled or disabled. The issue however is that (as of the
		// time of this writing) UInterchangeGenericTexturePipeline::HandleCreationOfTextureFactoryNode is hard-coded to expect
		// the texture payload key to be just the texture file path. We can't do that, because we need to also encode
		// the texture compression settings onto payload key...
		//
		// All of that is to say that everything will actually work fine, but if you uncheck "bImportUDIM" on the import options
		// you will still get UDIMs (for now).
		if (Value.bIsUDIM)
		{
			TMap<int32, FString> TileIndexToPath = UE::TextureUtilitiesCommon::GetUDIMBlocksFromSourceFile(
				Value.TextureFilePath,
				UE::TextureUtilitiesCommon::DefaultUdimRegexPattern
			);
			Node->SetSourceBlocks(MoveTemp(TileIndexToPath));
		}
	}

	FString AddTextureCubeNode(const FString& TextureFilePath, UInterchangeBaseNodeContainer& NodeContainer)
	{
		const FString NodeUid = MakeNodeUid(FString::Printf(TEXT("%s%s"), *TexturePrefix, *TextureFilePath));

		// If we created an actual TextureCubeNode here we'd be queried for the sliced payload later. Our HDR image is
		// just a latlong 2d texture though, so we instead need to create a Texture2DNode and then set SetForceLongLatCubemap(),
		// which will cause UInterchangeGenericTexturePipeline::HandleCreationOfTextureFactoryNode() to produce a
		// UInterchangeTextureCubeFactoryNode from this translated node, producing our TextureCube
		if (const UInterchangeTexture2DNode* Node = Cast<UInterchangeTexture2DNode>(NodeContainer.GetNode(NodeUid)))
		{
			return NodeUid;
		}

		FString NodeName{FPaths::GetCleanFilename(TextureFilePath)};

		UInterchangeTexture2DNode* Node = NewObject<UInterchangeTexture2DNode>(&NodeContainer);
		NodeContainer.SetupNode(Node, NodeUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset);

		Node->SetForceLongLatCubemap(true);

		UsdToUnreal::FTextureParameterValue TextureParameter;
		TextureParameter.TextureFilePath = TextureFilePath;
		TextureParameter.Group = TextureGroup::TEXTUREGROUP_Skybox;
		Node->SetPayLoadKey(EncodeTexturePayloadKey(TextureParameter));

		return NodeUid;
	}

	FString AddDisplayColorMaterialInstanceNodeIfNeeded(UInterchangeBaseNodeContainer& NodeContainer, const UsdUtils::FUsdPrimMaterialSlot& Slot)
	{
		using namespace UsdUnreal::MaterialUtils;

		FString DisplayColorDesc = Slot.MaterialSource;
		FString NodeUid = MakeNodeUid(MaterialPrefix + Slot.MaterialSource);

		// We'll treat the DisplayColorDesc (something like "!DisplayColor_1_0") as the material instance UID here
		const UInterchangeMaterialInstanceNode* Node = Cast<UInterchangeMaterialInstanceNode>(NodeContainer.GetNode(NodeUid));
		if (Node)
		{
			return NodeUid;
		}

		// Need to create a new instance
		TOptional<FDisplayColorMaterial> ParsedMat = FDisplayColorMaterial::FromString(DisplayColorDesc);
		if (!ParsedMat)
		{
			return {};
		}
		FString NodeName = ParsedMat->ToPrettyString();

		UInterchangeMaterialInstanceNode* NewNode = NewObject<UInterchangeMaterialInstanceNode>(&NodeContainer);
		NodeContainer.SetupNode(NewNode, NodeUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset);

		if (const FSoftObjectPath* ParentMaterialPath = GetReferenceMaterialPath(ParsedMat.GetValue()))
		{
			NewNode->SetCustomParent(ParentMaterialPath->GetAssetPathString());
		}

		return NodeUid;
	}

	FString AddUnrealMaterialReferenceNodeIfNeeded(UInterchangeBaseNodeContainer& NodeContainer, const FString& ContentPath)
	{
		using namespace UsdUnreal::MaterialUtils;

		// e.g. "\\MaterialReference\\/Game/MyFolder/Red.Red"
		const FString NodeUid = MakeNodeUid(MaterialReferencePrefix + ContentPath);

		const UInterchangeMaterialReferenceNode* Node = Cast<UInterchangeMaterialReferenceNode>(NodeContainer.GetNode(NodeUid));
		if (Node)
		{
			return NodeUid;
		}

		const FString DisplayName = FPaths::GetBaseFilename(ContentPath);

		UInterchangeMaterialReferenceNode* NewNode = NewObject<UInterchangeMaterialReferenceNode>(&NodeContainer);
		NodeContainer.SetupNode(NewNode, NodeUid, DisplayName, EInterchangeNodeContainerType::TranslatedAsset);
		NewNode->SetCustomContentPath(ContentPath);

		return NodeUid;
	}

	// Returns the UID of the material translated node that was generated from the MaterialX translation of a particular
	// material prim.
	//
	// This works because when parsing MaterialX files we generate shader graph nodes with UIDs that match the original
	// material prim name in the USD file (e.g. on USD we have a binding relationship to </MaterialX/Materials/Marble_3D>,
	// and we end up generating a shader graph node with Uid \\Shaders\\Marble_3D).
	FString GetMaterialXMaterialUid(const FString& PrimName, UInterchangeBaseNodeContainer& NodeContainer)
	{
		FString Result;

		NodeContainer.BreakableIterateNodesOfType<UInterchangeShaderGraphNode>(
			[&NodeContainer, &PrimName, &Result](const FString&, UInterchangeShaderGraphNode* ShaderGraphNode)
			{
				FString NodeUid = ShaderGraphNode->GetUniqueID();
				// Because USD allows having the same name between a parent and its child (unlike MaterialX)
				// When building the shader graph we may end up with a name not totally equal to the USD one (MaterialX will append an index to the material name)
				// e.g: prim -> mtlx_volume
				//      mtlx -> mtlx_volume2
				// see the egg_jade file from https://github.com/stehrani3d/MaterialEggs
				FString BaseName = FPaths::GetBaseFilename(NodeUid);
				if (BaseName == PrimName)
				{
					Result = NodeUid;
					return true;
				}
				else
				{
					if (BaseName.StartsWith(PrimName, ESearchCase::CaseSensitive))
					{
						if (FString Suffix = BaseName.Mid(PrimName.Len()); Suffix.IsNumeric())
						{
							Result = NodeUid;
							return true;
						}
					}
					return false;
				}
			}
		);

		return Result;
	}

	FString GetOrCreateTwoSidedShaderGraphNode(const FString& OneSidedShaderGraphNodeUid, UInterchangeBaseNodeContainer& NodeContainer)
	{
		FString TwoSidedUid = OneSidedShaderGraphNodeUid + TwoSidedSuffix;

		// We already created this, just return it
		if (const UInterchangeShaderGraphNode* Node = Cast<UInterchangeShaderGraphNode>(NodeContainer.GetNode(TwoSidedUid)))
		{
			return TwoSidedUid;
		}

		const UInterchangeShaderGraphNode* OneSidedNode = Cast<UInterchangeShaderGraphNode>(NodeContainer.GetNode(OneSidedShaderGraphNodeUid));
		if (!OneSidedNode)
		{
			UE_LOG(
				LogUsd,
				Warning,
				TEXT("Failed to create TwoSided material, as the UID '%s' does not point to a one-sided material!"),
				*OneSidedShaderGraphNodeUid
			);
			return OneSidedShaderGraphNodeUid;
		}

		// One-sided node is already two-sided, so just return that.
		// This can happen e.g. if the MaterialX translator internally sets the node as two-sided because the shader graph says to do that.
		// Note that we don't have anything caring for the exact opposite: If the USD Mesh is explicitly one-sided and the MaterialX material is
		// two-sided in this way we'll just use the two-sided material on the mesh. For now let's presume that's what the user intended, as you
		// have to explicitly set the MaterialX material as two-sided for that
		bool bIsTwoSided = false;
		if (OneSidedNode->GetCustomTwoSided(bIsTwoSided) && bIsTwoSided)
		{
			return OneSidedShaderGraphNodeUid;
		}

		FString TwoSidedNodeName = OneSidedNode->GetDisplayLabel() + TwoSidedSuffix;
		UInterchangeShaderGraphNode* TwoSidedNode = NewObject<UInterchangeShaderGraphNode>(&NodeContainer);

		UInterchangeBaseNode::CopyStorage(OneSidedNode, TwoSidedNode);

		NodeContainer.SetupNode(TwoSidedNode, TwoSidedUid, TwoSidedNodeName, EInterchangeNodeContainerType::TranslatedAsset);

		TwoSidedNode->SetCustomTwoSided(true);
		TwoSidedNode->SetCustomTwoSidedTransmission(true);

		return TwoSidedUid;
	}

	// Sets on the provided MeshNode custom attributes needed to bake the provided GeomPropValue geomprops / primvars
	// into textures, for the provided MaterialNodeUid
	void AddPrimvarBakingAttributes(
		UInterchangeBaseNode* BaseNode,
		const FString& MaterialNodeUid,
		UInterchangeBaseNodeContainer& NodeContainer,
		const TArray<FUsdMaterialXShaderGraph::FGeomProp>& GeomPropValues
	)
	{
#if WITH_EDITOR
		using namespace UE::Interchange;

		// TODO: Is it OK that this function is called repeatedly for the same Mesh? (e.g. from multiple material assignments,
		// from multiple scene node instancing the same Mesh asset node, etc.)

		UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(BaseNode);
		if (!MeshNode)
		{
			// We also use this function when setting material overrides on scene nodes. In that case our scene node should have a
			// Mesh asset node UID
			if (UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(BaseNode))
			{
				FString AssetInstanceUid;
				SceneNode->GetCustomAssetInstanceUid(AssetInstanceUid);

				if (const UInterchangeMeshNode* SceneMeshNode = Cast<UInterchangeMeshNode>(NodeContainer.GetNode(AssetInstanceUid)))
				{
					MeshNode = const_cast<UInterchangeMeshNode*>(SceneMeshNode);
				}
			}
		}
		if (!MeshNode)
		{
			return;
		}

		// Let's iterate over the Shader Nodes, the TextureSample ones, more specifically,
		// to see if we have to retrieve any attributes related to the conversion of geompropvalues
		// we'll store the UID of the shader node in order to retrieve it during the baking phase (in the post factory import)
		TArray<FString> ShaderNodesTextureSampleUIDs;
		NodeContainer.IterateNodesOfType<UInterchangeShaderNode>(
			[&ShaderNodesTextureSampleUIDs, &MaterialNodeUid](const FString& ShaderUid, UInterchangeShaderNode* ShaderNode)
			{
				using namespace UE::Interchange::Materials::Standard::Nodes;

				// We only care about baking the geomprop nodes that were generated when parsing this Material, and
				// they should always have the Uid of the material as a prefix
				if (!ShaderUid.StartsWith(MaterialNodeUid))
				{
					return;
				}

				if (FString ShaderType; ShaderNode->GetCustomShaderType(ShaderType) && ShaderType == TextureSample::Name.ToString())
				{
					if (bool bIsGeomProp; ShaderNode->GetBooleanAttribute(::MaterialX::Attributes::GeomPropImage, bIsGeomProp) && bIsGeomProp)
					{
						ShaderNodesTextureSampleUIDs.Emplace(ShaderNode->GetUniqueID());
					}
				}
			}
		);

		if (GeomPropValues.Num() != ShaderNodesTextureSampleUIDs.Num())
		{
			UE_LOG(
				LogUsd,
				Warning,
				TEXT("Failed to bake primvars for mesh '%s' and material '%s': Encountered %d GeomPropValues but %d geomprop shader nodes!"),
				*MeshNode->GetUniqueID(),
				*MaterialNodeUid,
				GeomPropValues.Num(),
				ShaderNodesTextureSampleUIDs.Num()
			);
			return;
		}

		MeshNode->AddInt32Attribute(USD::Primvar::Number, GeomPropValues.Num());

		for (int32 Index = 0; Index < GeomPropValues.Num(); ++Index)
		{
			MeshNode->AddStringAttribute(USD::Primvar::Name + FString::FromInt(Index), GeomPropValues[Index].Name);
			MeshNode->AddBooleanAttribute(USD::Primvar::TangentSpace + FString::FromInt(Index), GeomPropValues[Index].bTangentSpace);
			MeshNode->AddStringAttribute(USD::Primvar::ShaderNodeTextureSample + FString::FromInt(Index), ShaderNodesTextureSampleUIDs[Index]);
		}
#endif	  // WITH_EDITOR
	}

	bool UInterchangeUSDTranslatorImpl::AddMaterialXShaderGraph(
		const UE::FUsdPrim& Prim,
		UInterchangeUsdTranslatorSettings* TranslatorSettings,
		UInterchangeBaseNodeContainer& NodeContainer,
		TArray<FUsdMaterialXShaderGraph::FGeomProp>& GeomPropValues
	)
	{
		FName RenderContext = TranslatorSettings ? TranslatorSettings->RenderContext : UnrealIdentifiers::UnrealRenderContext;

		// Check for any references of MaterialX
#if WITH_EDITOR
		if (RenderContext == UnrealIdentifiers::MaterialXRenderContext &&
			(UsdUtils::HasSurfaceOutput(Prim, UnrealIdentifiers::MaterialXRenderContext) ||
			UsdUtils::HasVolumeOutput(Prim, UnrealIdentifiers::MaterialXRenderContext) ||
			UsdUtils::HasDisplacementOutput(Prim, UnrealIdentifiers::MaterialXRenderContext)))
		{
			TArray<FString> FilePaths = UsdUtils::GetMaterialXFilePaths(Prim);
			for (const FString& File : FilePaths)
			{
				// the file has already been handled no need to do a Translate again
				if (!Translators.Find(File))
				{
					UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();
					UInterchangeSourceData* SourceData = UInterchangeManager::CreateSourceData(File);

					UInterchangeTranslatorBase* Translator = InterchangeManager.GetTranslatorForSourceData(SourceData);
					// check on the Translator, it might return nullptr in case of reimport
					if (Translator)
					{
						Translator->Results = ResultsContainer;
						Translator->Translate(NodeContainer);
						Translators.Emplace(File, Translator);
					}
				}
			}

			if (!FilePaths.IsEmpty())
			{
				return true;
			}

			// Conditionally enable the shader graph for the time being as it crashes on Linux cause of a probable double free
#if ENABLE_USD_MATERIALX
			bool bHasShaderGraph = false;
			FUsdMaterialXShaderGraph ShaderGraph(Prim, *RenderContext.ToString());
			GeomPropValues = ShaderGraph.GetGeomPropValueNames();
			if (MaterialX::DocumentPtr Document = ShaderGraph.GetDocument())
			{
				return FMaterialXManager::GetInstance().Translate(Document, NodeContainer);
			}
#endif // ENABLE_USD_MATERIALX
		}
#endif	  // WITH_EDITOR
		return false;
	}

	// We use this visitor to set UsdToUnreal::FParameterValue TVariant values onto UInterchangeMaterialInstanceNode.
	//
	// For now we only set attributes meant to be parsed as material instance parameters.
	// If/whenever we want to support generating full material shader graphs from USD, we likely don't want to just fill out inputs
	// into a rigid material function structures based on the shading model like the GLTF translator does, as USD materials can have
	// custom shader graphs themselves. We'd either need to truly generate arbitrary interchange shader graphs here to be useful, or
	// to delegate this work to MaterialX somehow (c.f. MaterialX materials baked into USD shader graphs)
	struct FMaterialInstanceParameterValueVisitor
	{
		FMaterialInstanceParameterValueVisitor(
			const UE::FUsdPrim& InPrim,
			UInterchangeBaseNodeContainer& InNodeContainer,
			UInterchangeMaterialInstanceNode& InMaterialNode,
			const TMap<FString, int32>& InPrimvarToUVIndex
		)
			: Prim(InPrim)
			, NodeContainer(InNodeContainer)
			, MaterialNode(InMaterialNode)
			, PrimvarToUVIndex(InPrimvarToUVIndex)
		{
		}

		void EnableTextureForChannel(bool bEnable) const
		{
			using namespace UE::Interchange::USD;

			MaterialNode.AddScalarParameterValue(UseTextureParameterPrefix + *BaseParameterName + UseTextureParameterSuffix, bEnable ? 1.0f : 0.0f);
		}

		void operator()(const float Value) const
		{
			MaterialNode.AddScalarParameterValue(*BaseParameterName, Value);
			EnableTextureForChannel(false);
		}

		void operator()(const FVector& Value) const
		{
			MaterialNode.AddVectorParameterValue(*BaseParameterName, FLinearColor{Value});
			EnableTextureForChannel(false);
		}

		void operator()(const UsdToUnreal::FTextureParameterValue& Value) const
		{
			// Emit texture node itself (this is the main place where this happens)
			// Note that the node name isn't just the texture path, as we may have multiple material users of this texture
			// with different settings, and so we need separate translated nodes for each material and parameter
			// TODO: Encode the different settings on the node uid as well to automatically deduplicate?
			const FString TextureUid = FString::Printf(TEXT("%s%s:%s"), *TexturePrefix, *UsdUtils::GetPrototypePrimPath(Prim).GetString(), **BaseParameterName);
			AddTextureNode(Prim, TextureUid, Value, NodeContainer);

			// Actual texture assignment
			MaterialNode.AddTextureParameterValue(	  //
				FString::Printf(TEXT("%sTexture"), **BaseParameterName),
				TextureUid
			);
			EnableTextureForChannel(true);

			// UV transform
			FLinearColor ScaleAndTranslation = FLinearColor{
				Value.UVScale.GetVector()[0],
				Value.UVScale.GetVector()[1],
				Value.UVTranslation[0],
				Value.UVTranslation[1]
			};
			MaterialNode.AddVectorParameterValue(FString::Printf(TEXT("%sScaleTranslation"), **BaseParameterName), ScaleAndTranslation);
			MaterialNode.AddScalarParameterValue(	 //
				FString::Printf(TEXT("%sRotation"), **BaseParameterName),
				Value.UVRotation
			);

			// UV index
			if (const int32* FoundIndex = PrimvarToUVIndex.Find(Value.Primvar))
			{
				MaterialNode.AddScalarParameterValue(	 //
					*BaseParameterName + UE::Interchange::USD::UVIndexParameterSuffix,
					*FoundIndex
				);
			}
			else
			{
				UE_LOG(
					LogUsd,
					Warning,
					TEXT("Failed to find primvar '%s' when setting material parameter '%s' on material '%s'. Available primvars and UV "
						 "indices: %s.%s"),
					*Value.Primvar,
					**BaseParameterName,
					*Prim.GetPrimPath().GetString(),
					*UsdUtils::StringifyMap(PrimvarToUVIndex),
					Value.Primvar.IsEmpty()
						? TEXT(
							  " Is your UsdUVTexture Shader missing the 'inputs:st' attribute? (It specifies which UV set to sample the texture with)"
						  )
						: TEXT("")
				);
			}

			// Component mask (which channel of the texture to use)
			FLinearColor ComponentMask = FLinearColor::Black;
			switch (Value.OutputIndex)
			{
				case 0:	   // RGB
					ComponentMask = FLinearColor{1.f, 1.f, 1.f, 0.f};
					break;
				case 1:	   // R
					ComponentMask = FLinearColor{1.f, 0.f, 0.f, 0.f};
					break;
				case 2:	   // G
					ComponentMask = FLinearColor{0.f, 1.f, 0.f, 0.f};
					break;
				case 3:	   // B
					ComponentMask = FLinearColor{0.f, 0.f, 1.f, 0.f};
					break;
				case 4:	   // A
					ComponentMask = FLinearColor{0.f, 0.f, 0.f, 1.f};
					break;
			}
			MaterialNode.AddVectorParameterValue(FString::Printf(TEXT("%sTextureComponent"), **BaseParameterName), ComponentMask);
		}

		void operator()(const UsdToUnreal::FPrimvarReaderParameterValue& Value) const
		{
			MaterialNode.AddVectorParameterValue(*BaseParameterName, FLinearColor{Value.FallbackValue});

			if (Value.PrimvarName == TEXT("displayColor"))
			{
				MaterialNode.AddScalarParameterValue(TEXT("UseVertexColorForBaseColor"), 1.0f);
			}
		}

		void operator()(const bool Value) const
		{
			// Actual booleans are only meant for static switches on Interchange
			MaterialNode.AddScalarParameterValue(*BaseParameterName, static_cast<float>(Value));
		}

	public:
		const UE::FUsdPrim& Prim;
		UInterchangeBaseNodeContainer& NodeContainer;
		UInterchangeMaterialInstanceNode& MaterialNode;
		const TMap<FString, int32>& PrimvarToUVIndex;
		const FString* BaseParameterName = nullptr;
	};

	// Helper to traverse a Nanite assembly root subtree (i.e. a prim with NaniteAssemblyRootAPI schema applied)
	class FNaniteAssemblyTraversalHelper
	{
		using FNaniteAssemblyTraversalResult = UsdToUnreal::NaniteAssemblyUtils::FNaniteAssemblyTraversalResult;
		using ENaniteAssemblyMeshType = UsdToUnreal::NaniteAssemblyUtils::ENaniteAssemblyMeshType;
		using EMeshCategory = UsdToUnreal::NaniteAssemblyUtils::EMeshCategory;
		using FPrimPrototypeEntry = UsdToUnreal::NaniteAssemblyUtils::FPrimPrototypeEntry;
		using FMeshEntry = UsdToUnreal::NaniteAssemblyUtils::FMeshEntry;

		UInterchangeUSDTranslatorImpl& TranslatorImpl;
		UInterchangeBaseNodeContainer& NodeContainer;
		UInterchangeUsdTranslatorSettings* TranslatorSettings;
		UInterchangeSceneNode* ParentSceneNode;
		const FString ParentSceneNodeUid;

		// Our root assembly prim with the NaniteAssemblyRootAPI schema applied
		UE::FUsdPrim AssemblyRootPrim;

		// The type of assembly mesh - skeletal or static
		ENaniteAssemblyMeshType AssemblyMeshType;

		// Base skeleton path (required for skeletal mesh assemblies only)
		UE::FSdfPath BaseSkeletonPrimPath;

		// Base skeleton skel-root prim path.
		UE::FSdfPath BaseSkelRootPrimPath;

		// Computed traversal result that we'll attach to the base mesh's TraversalInfo
		// and access again during mesh payload retrieval
		TSharedPtr<FNaniteAssemblyTraversalResult> Result;

		// The FTraversalInfo's for each base/part mesh
		TMap<UE::FSdfPath, FTraversalInfo> TraversalInfoByCachedPrimPath;

		// The USD implicit prototype path to generated mesh uid. We use this to work-around 
		// interchange not currently handling instanced skeletal meshes too well.
		TMap<UE::FSdfPath, FString> ImplicitPrototypePrimPathToCachedMeshUid;

		// In the skeletal mesh case, parts must have the NaniteAssemblySkelBindingAPI applied. 
		TArray<UE::FSdfPath> InvalidPartMeshNoSkelBindingPaths;

	public:

		FNaniteAssemblyTraversalHelper(
			UInterchangeUSDTranslatorImpl& InTranslatorImpl,
			UInterchangeBaseNodeContainer& InNodeContainer,
			UInterchangeUsdTranslatorSettings* InTranslatorSettings,
			UInterchangeSceneNode* InParentSceneNode)
			: TranslatorImpl(InTranslatorImpl)
			, NodeContainer(InNodeContainer)
			, TranslatorSettings(InTranslatorSettings)
			, ParentSceneNode(InParentSceneNode)
			, ParentSceneNodeUid(ParentSceneNode->GetUniqueID())
		{

		}

	private:

		bool MeshTypeIs(ENaniteAssemblyMeshType MeshType) const
		{
			return MeshType == AssemblyMeshType;
		}

		bool IsCachableSkeletalMeshPrim(const UE::FUsdPrim& Prim, EMeshCategory Category)
		{
			return 
				MeshTypeIs(ENaniteAssemblyMeshType::SkeletalMesh)
				&& Category == EMeshCategory::Part
				&& Prim.IsInstanceProxy();
		}

		bool GetCachedSkeletalMeshUid(const UE::FUsdPrim& Prim, EMeshCategory Category, FString& OutMeshUid)
		{
			if (IsCachableSkeletalMeshPrim(Prim, Category))
			{
				UE::FUsdPrim PrototypePrim = UsdUtils::GetPrototypePrim(Prim);
				if (const FString* CachedMeshUid = ImplicitPrototypePrimPathToCachedMeshUid.Find(PrototypePrim.GetPrimPath()))
				{
					OutMeshUid = *CachedMeshUid;
					return true;
				}
			}
			return false;
		}

		void SetCachedSkeletalMeshUid(const UE::FUsdPrim& Prim, EMeshCategory Category, const FString& MeshUid)
		{
			if (IsCachableSkeletalMeshPrim(Prim, Category))
			{
				UE::FUsdPrim PrototypePrim = UsdUtils::GetPrototypePrim(Prim);
				ImplicitPrototypePrimPathToCachedMeshUid.Add(PrototypePrim.GetPrimPath(), MeshUid);
			}
		}

		FString CreateMeshNode(const UE::FSdfPath& Path, EMeshCategory Category, const TArray<FString>& AssemblyDependencies = {})
		{
			FTraversalInfo* Info = TraversalInfoByCachedPrimPath.Find(Path);
			if (!ensure(Info))
			{
				return {};
			}

			const UE::FUsdPrim Prim = AssemblyRootPrim.GetStage().GetPrimAtPath(Path);
			if (!ensure(Prim.IsA(TEXT("Mesh"))))
			{
				return {};
			}

			// If this is a repeated instanced skeletal mesh part, reuse the previously generated uid. This is
			// a workaround for the current lack of instanced skeletal mesh support in Interchange.
			if (FString CachedMeshUid; GetCachedSkeletalMeshUid(Prim, Category, CachedMeshUid))
			{
				return CachedMeshUid;
			}

			// If the mesh node we're creating has assembly dependencies, it must be the first base mesh, and so 
            // attach the traversal result to our traversal info for retrieval during payload creation.
			if (AssemblyDependencies.Num() > 0 && ensure(Result.IsValid()))
			{
				Info->NaniteAssemblyTraversalResult = Result;
			}
			
			FString MeshUid = TranslatorImpl.AddMeshNode(Prim, TranslatorSettings, NodeContainer, *Info, false, AssemblyDependencies);

			const FString SceneNodeUid = MakeNodeUid(Prim.GetPrimPath().GetString());
			const FString DisplayLabel(Prim.GetName().ToString());
			static const FString NoParentUid = TEXT("");
			UInterchangeSceneNode* SceneNode = CreateSceneNode(Prim, TranslatorSettings, NodeContainer, SceneNodeUid, DisplayLabel, NoParentUid);

			constexpr bool bResetCache = false;
			SceneNode->SetCustomLocalTransform(&NodeContainer, FTransform::Identity, bResetCache);
			SceneNode->SetCustomAssetInstanceUid(MeshUid);

			// Store the mesh uid for the instanced skeletal mesh case
			SetCachedSkeletalMeshUid(Prim, Category, MeshUid);

			return MeshUid;
		}

		UE::FSdfPath GetSkelBindingPrimPathForPart(const UE::FUsdPrim& Prim, const FTraversalInfo& Info)
		{
			// This should only be called for skeletal mesh parts.
			// 
			// Check and get the path to the (possibly ancestor) prim holding skel binding data

			UE::FSdfPath EmptyPath;
			if (!MeshTypeIs(ENaniteAssemblyMeshType::SkeletalMesh))
			{
				return EmptyPath;
			}

			if (Info.ClosestNaniteAssemblySkelBindingPath)
			{
				const UE::FSdfPath SkelBindingPrimPath = UE::FSdfPath(**Info.ClosestNaniteAssemblySkelBindingPath);
				if (SkelBindingPrimPath.IsPrimPath())
				{
					return SkelBindingPrimPath;
				}
			}

			InvalidPartMeshNoSkelBindingPaths.Add(Prim.GetPrimPath());

			return EmptyPath;
		}

		EMeshCategory GetMeshCategory(const UE::FUsdPrim& Prim, const FTraversalInfo& Info, TArray<FPrimPrototypeEntry>& PrototypeStack)
		{
			if (!Prim.IsA(TEXT("Mesh")))
			{
				return EMeshCategory::None;
			}

			// A mesh in a skeletal assembly must:
			//  i: have a bound skeleton 
			// ii: if it's a 'base' mesh (i.e. connected to the designated assembly root skeleton)
			//      - must NOT be inside a pointinstancer
			//      - must NOT be USD native instanced
			//     or, if it's a 'part' mesh (i.e. attached to some other skeleton)
			//      - may be inside a pointinstancer, but, not nested, or
			//      - must be USD native instanced (because we don't want to encourage users to create tons of unique non-instanced parts) 
			//
			if (MeshTypeIs(ENaniteAssemblyMeshType::SkeletalMesh) && Info.BoundSkeletonPrimPath)
			{
				if (BaseSkeletonPrimPath.GetString() == **Info.BoundSkeletonPrimPath) // base skeleton mesh
				{
					if (PrototypeStack.IsEmpty() && !Prim.IsInstanceProxy())
					{
						return EMeshCategory::Base;
					}
				}
				else // part skeleton mesh
				{
					if (PrototypeStack.Num() == 1 || Prim.IsInstanceProxy())
					{
						return EMeshCategory::Part;
					}
				}
			}

			// A part mesh in a static assembly must 
			//  i: not have skel binding data
			// ii: must be inside a pointinstancer prototype, or, be USD native instanced.
			//
			else if (MeshTypeIs(ENaniteAssemblyMeshType::StaticMesh))
			{
				if (!Prim.HasAPI(TEXT("SkelBindingAPI")) && (PrototypeStack.Num() > 0 || Prim.IsInstanceProxy()))
				{
					return EMeshCategory::Part;
				}
			}

			// TODO: communicate all of the above to the user, somehow
			return EMeshCategory::None;
		}

		void HandleOrIgnoreMesh(
			const UE::FUsdPrim& Prim, 
			FTraversalInfo Info, 
			TArray<FTransform>& TransformStack, 
			TArray<FPrimPrototypeEntry>& PrototypeStack)
		{
			if (!Prim.IsA(TEXT("Mesh")) || !Result.IsValid())
			{
				return;
			}

			EMeshCategory Category = GetMeshCategory(Prim, Info, PrototypeStack);
			if (Category == EMeshCategory::None)
			{
				return;
			}

			// If this is a skeletal mesh part, check we have a prim with binding data
			UE::FSdfPath SkelBindingPrimPath;
			if (MeshTypeIs(ENaniteAssemblyMeshType::SkeletalMesh) && Category == EMeshCategory::Part)
			{
				SkelBindingPrimPath = GetSkelBindingPrimPathForPart(Prim, Info);
				if (!SkelBindingPrimPath.IsPrimPath())
				{
					return;
				}
			}

			ensure(
				Result->AddEntry(MakeShared<FMeshEntry>(
					Category, 
					Prim.GetPrimPath(), 
					SkelBindingPrimPath, 
					TransformStack, 
					PrototypeStack)));

			TraversalInfoByCachedPrimPath.Add(Prim.GetPrimPath(), Info);
		}

		void HandleExternalAssetReference(
			const UE::FUsdPrim& Prim,
			FTraversalInfo Info,
			TArray<FTransform>& TransformStack,
			TArray<FPrimPrototypeEntry>& PrototypeStack,
			const FString& AssetReference)
		{
			if (!Result.IsValid())
			{
				return;
			}

			UE::FSdfPath SkelBindingPrimPath;

			// External asset references are only supported on parts.
			EMeshCategory Category = EMeshCategory::Part;

			if (MeshTypeIs(ENaniteAssemblyMeshType::SkeletalMesh))
			{
				// If we found the schema applied to a descendant of the base skelroot prim
				// we should skip as we're not a part.
				if (Prim.GetPrimPath().HasPrefix(BaseSkelRootPrimPath))
				{
					return;
				}

				// This is a skeletal mesh part - check we have prim with binding data
				SkelBindingPrimPath = GetSkelBindingPrimPathForPart(Prim, Info);
				if (!SkelBindingPrimPath.IsPrimPath())
				{
					return;
				}
			}

			TSharedPtr<FMeshEntry> Entry = MakeShared<FMeshEntry>(
				Category,
				Prim.GetPrimPath(),
				SkelBindingPrimPath,
				TransformStack,
				PrototypeStack);

			if (!ensure(Result->AddEntry(Entry)))
			{
				return;
			}
			
			Entry->NodeUid = AssetReference;

			TraversalInfoByCachedPrimPath.Add(Prim.GetPrimPath(), Info);
		}

		void HandleSkeleton(const UE::FUsdPrim& Prim, FTraversalInfo Info, TArray<FTransform>& TransformStack) const
		{
			const bool bIsBaseSkeleton = Prim.GetPrimPath() == BaseSkeletonPrimPath;

			const FString SceneNodeUid = MakeNodeUid(Prim.GetPrimPath().GetString());
			const FString DisplayLabel(Prim.GetName().ToString());
			const FString ThisParentSceneUid = bIsBaseSkeleton ? ParentSceneNodeUid : TEXT("");
			UInterchangeSceneNode* SceneNode = CreateSceneNode(Prim, TranslatorSettings, NodeContainer, SceneNodeUid, DisplayLabel, ThisParentSceneUid);


			const FTransform Transform = bIsBaseSkeleton ? TransformStack.Last() : FTransform::Identity;
			constexpr bool bResetCache = false;
			SceneNode->SetCustomLocalTransform(&NodeContainer, Transform, bResetCache);

			TranslatorImpl.AddSkeletonNodes(Prim, *SceneNode, NodeContainer, Info);
		}

		bool FindAndSetBaseSkeleton()
		{
			if (!MeshTypeIs(ENaniteAssemblyMeshType::SkeletalMesh))
			{
				return true;
			}

			// Get the "base" skeleton prim as specified by the schemas "unreal:naniteAssembly:skeleton" relationship
			
			const UE::FUsdPrim SkelPrim = UsdToUnreal::NaniteAssemblyUtils::GetBaseSkeletonPrimForSkeletalMeshAssembly(AssemblyRootPrim);
			if (!SkelPrim)
			{
				// Note reason for failure will have been logged by utility call above.
				return false;
			}
			BaseSkeletonPrimPath = SkelPrim.GetPrimPath();

			// We got the skeleton prim, so now check we have an ancestor SkelRoot prim before the assembly root prim in order to try
			// detect weird or broken hierarchy configurations.

			UE::FUsdPrim CurrentPrim = SkelPrim;
			while (!CurrentPrim.IsPseudoRoot())
			{
				if (CurrentPrim.IsA(TEXT("SkelRoot")))
				{
					BaseSkelRootPrimPath = CurrentPrim.GetPrimPath();
					break;
				}
				CurrentPrim = CurrentPrim.GetParent();
			}

			if (BaseSkelRootPrimPath.IsEmpty())
			{
				UE_LOG(LogUsd, Warning, 
					TEXT("Failed to find SkelRoot prim for base Skeleton prim '%s' while processing NaniteAssemblyRoot prim '%s'")
					, *BaseSkeletonPrimPath.GetString()
					, *AssemblyRootPrim.GetPrimPath().GetString()
				);
				return false;
			}
			
			return true;
		}

		bool ComputeTraversalResult(FTraversalInfo Info, const FTransform& ParentGlobalTransform)
		{
			Result = MakeShared<FNaniteAssemblyTraversalResult>(AssemblyMeshType, AssemblyRootPrim.GetPrimPath());

			TArray<FTransform> TransformStack = { ParentGlobalTransform };
			TArray<FPrimPrototypeEntry> PrototypeStack;

			if (MeshTypeIs(ENaniteAssemblyMeshType::StaticMesh) && AssemblyRootPrim.IsA(TEXT("PointInstancer")))
			{
				TraversePointInstancerHierarchy(AssemblyRootPrim, Info, TransformStack, PrototypeStack);
			}
			else
			{
				constexpr bool bTraverseInstanceProxies = true;
				for (const UE::FUsdPrim& ChildPrim : AssemblyRootPrim.GetFilteredChildren(bTraverseInstanceProxies))
				{
					TraverseImpl(ChildPrim, Info, TransformStack, PrototypeStack);
				}
			}

			if (!ValidateTraversalResult())
			{
				Result.Reset();
				return false;
			}

			return true;
		}

		void TraversePointInstancerHierarchy(
			const UE::FUsdPrim& Prim,
			FTraversalInfo Info,
			TArray<FTransform> TransformStack,
			TArray<FPrimPrototypeEntry>& PrototypeStack
		)
		{
			const TArray<UE::FSdfPath> PrototypePaths = UsdToUnreal::NaniteAssemblyUtils::GetPointInstancerPrototypePaths(Prim);
			if (PrototypePaths.IsEmpty())
			{
				return;
			}

			for (int32 PrototypeIndex = 0; PrototypeIndex < PrototypePaths.Num(); ++PrototypeIndex)
			{
				UE::FUsdPrim PrototypePrim = Prim.GetStage().GetPrimAtPath(PrototypePaths[PrototypeIndex]);
				if (!PrototypePrim)
				{
					continue;
				}

				FTraversalInfo PrototypePrimInfo = Info;
				PrototypePrimInfo.UpdateWithCurrentPrim(PrototypePrim);

				// Push transforms and prototype before entering each pointinstancer prototype
				TransformStack.Add(FTransform::Identity);
				PrototypeStack.Emplace(Prim.GetPrimPath(), PrototypePrim.GetPrimPath(), PrototypeIndex, PrototypePaths.Num());

				TraverseImpl(PrototypePrim, PrototypePrimInfo, TransformStack, PrototypeStack);

				// Pop before entering the next
				PrototypeStack.Pop();
				TransformStack.Pop();
			}
		}

		void TraverseImpl(const UE::FUsdPrim& Prim, FTraversalInfo Info, TArray<FTransform> TransformStack, TArray<FPrimPrototypeEntry>& PrototypeStack)
		{
			if (!ensure(TransformStack.Num() > 0))
			{
				return;
			}

			if (Prim.HasAPI(TEXT("NaniteAssemblyRootAPI")))
			{
				UE_LOG(LogUsd, Warning,
					TEXT("Skipping nested NaniteAssemblyRootAPI prim '%s' found below current root '%s'")
					, *Prim.GetPrimPath().GetString()
					, *AssemblyRootPrim.GetPrimPath().GetString()
				);
				return;
			}

			if (Prim.IsA(TEXT("Material")))
			{
				return;
			}

			Info.UpdateWithCurrentPrim(Prim);

			if (Prim.IsA(TEXT("Xformable")))
			{
				FTransform LocalTransform = FTransform::Identity;
				bool bResetTransformStack = false;
				UsdToUnreal::ConvertXformable(Prim.GetStage(), UE::FUsdTyped(Prim), LocalTransform, UsdUtils::GetEarliestTimeCode(), &bResetTransformStack);
				
				FTransform& CurrentTransform = TransformStack.Last();
				CurrentTransform = LocalTransform * CurrentTransform;
			}

			if (FString AssetRef; UsdToUnreal::NaniteAssemblyUtils::GetExternalAssetReference(Prim, AssetRef))
			{
				HandleExternalAssetReference(Prim, Info, TransformStack, PrototypeStack, AssetRef);
				// The user is mostly likely expecting the subtree below the prim with the external
				// reference applied to be pruned, so we stop the traversal.
				return;
			}

			if (Prim.IsA(TEXT("Mesh")))
			{
				HandleOrIgnoreMesh(Prim, Info, TransformStack, PrototypeStack);
			}
			else if (Prim.IsA(TEXT("PointInstancer")))
			{
				TraversePointInstancerHierarchy(Prim, Info, TransformStack, PrototypeStack);
			}
			else if (Prim.IsA(TEXT("Skeleton")))
			{
				HandleSkeleton(Prim, Info, TransformStack);
			}
			else
			{
				constexpr bool bTraverseInstanceProxies = true;
				for (const UE::FUsdPrim& ChildPrim : Prim.GetFilteredChildren(bTraverseInstanceProxies))
				{
					TraverseImpl(ChildPrim, Info, TransformStack, PrototypeStack);
				}
			}
		}

		bool ValidateHierarchy(const FTraversalInfo& Info) const
		{
			if (AssemblyMeshType == ENaniteAssemblyMeshType::None)
			{
				return false;
			}

			// Nanite assemblies cannot be inside LODs.
			if (Info.bInsideLOD || Info.bIsLODContainer)
			{
				UE_LOG(LogUsd, Warning,
					TEXT("Ignoring NaniteAssemblyRootAPI prim '%s' which is inside an LOD (not supported).")
					, *AssemblyRootPrim.GetPrimPath().GetString()
				);
				return false;
			}

			// Check ancestors for configurations that we know will cause problems:
			
			UE::FUsdPrim CurrentPrim = AssemblyRootPrim;

			while (!CurrentPrim.IsPseudoRoot())
			{
				if (CurrentPrim != AssemblyRootPrim && CurrentPrim.IsA(TEXT("PointInstancer")))
				{
					UE_LOG(LogUsd, Warning,
						TEXT("NaniteAssemblyRoot '%s' cannot currently be a descendant of a pointinstancer - '%s'")
						, *AssemblyRootPrim.GetPrimPath().GetString()
						, *CurrentPrim.GetPrimPath().GetString()
					);
					return false;
				}
				else if (CurrentPrim.HasAPI(*UsdToUnreal::ConvertToken(UnrealIdentifiers::LodSubtreeAPI)))
				{
					UE_LOG(LogUsd, Warning,
						TEXT("NaniteAssemblyRoot '%s' cannot be nested inside an %s container.")
						, *AssemblyRootPrim.GetPrimPath().GetString()
						, *UsdToUnreal::ConvertToken(UnrealIdentifiers::LodSubtreeAPI)
					);
					return false;
				}
				CurrentPrim = CurrentPrim.GetParent();
			}

			return true;
		}

		bool ValidateTraversalResult() const
		{
			if (!Result)
			{
				return false;
			}

			// Report skel mesh parts that were dropped due to missing bindings
			if (InvalidPartMeshNoSkelBindingPaths.Num() > 0)
			{
				FString Message = FString::Printf(
					TEXT("Ignoring (%d) skeletal mesh prims missing joint binding data while generating parts for Nanite assembly ")
					TEXT("prim '%s'.\nPlease check that the %s schema is applied to an ancestor prim and valid primvar data ")
					TEXT("authored for attributes '%s' and '%s'...\n")
					, InvalidPartMeshNoSkelBindingPaths.Num()
					, *AssemblyRootPrim.GetPrimPath().GetString()
					, *UsdToUnreal::ConvertToken(UnrealIdentifiers::NaniteAssemblySkelBindingAPI)
					, *UsdToUnreal::ConvertToken(UnrealIdentifiers::PrimvarsUnrealNaniteAssemblyBindJoints)
					, *UsdToUnreal::ConvertToken(UnrealIdentifiers::PrimvarsUnrealNaniteAssemblyBindJointWeights)
				);
				for (int32 Index = 0; Index < InvalidPartMeshNoSkelBindingPaths.Num(); ++Index)
				{
					// there might be a massive number of prims here, so only print the first few
					int32 constexpr MaxEntries = 50;
					if (Index < MaxEntries)
					{
						Message.Append(FString::Printf(TEXT("... %s\n"), *InvalidPartMeshNoSkelBindingPaths[Index].GetString()));
					}
					else
					{
						Message.Append(FString::Printf(TEXT("... (skipped %d more)"), InvalidPartMeshNoSkelBindingPaths.Num() - MaxEntries));
						break;
					}
				}

				UE_LOG(LogUsd, Warning, TEXT("%s"), *Message);
			}

			// Check we found some parts, and, if this is a skeletal mesh assembly, we also require at least one base mesh
			const bool bHasBaseMeshes = Result->HasEntriesForCategory(EMeshCategory::Base);
			const bool bHasPartMeshes = Result->HasEntriesForCategory(EMeshCategory::Part);
			if (!bHasPartMeshes || (MeshTypeIs(ENaniteAssemblyMeshType::SkeletalMesh) && !bHasBaseMeshes))
			{
				UE_LOG(LogUsd, Warning,
					TEXT("Failed to find any valid base/part meshes for NaniteAssemblyRootAPI prim '%s'")
					, *AssemblyRootPrim.GetPrimPath().GetString()
				);

				return false;
			}

			return true;
		}

	public:

		void Traverse(const UE::FUsdPrim& InAssemblyRootPrim, FTraversalInfo Info, const FTransform& ParentGlobalTransform)
		{
			AssemblyRootPrim = InAssemblyRootPrim;

			if (!AssemblyRootPrim.HasAPI(TEXT("NaniteAssemblyRootAPI")))
			{
				return;
			}

			// Setup and initial validation

			AssemblyMeshType = UsdToUnreal::NaniteAssemblyUtils::GetNaniteAssemblyMeshType(AssemblyRootPrim);

			if (!ValidateHierarchy(Info) || !FindAndSetBaseSkeleton())
			{
				return;
			}

			// Compute the traversal result that will be attached to the traversal info object of
			// the skeletal or static assembly base mesh. 
			if (!ComputeTraversalResult(Info, ParentGlobalTransform) || !Result.IsValid())
			{
				return;
			}
			
			// Now create mesh and scene nodes for parts. Do these first because we will need the resulting node 
			// uids when creating the base meshes in a moment, in order to setup the correct dependencies.
			for (TSharedPtr<FMeshEntry>& Entry : Result->GetEntriesForCategory(EMeshCategory::Part))
			{
				// Note that external asset ref part entries will have already set the uid to whatever asset.
				if (Entry->NodeUid.IsEmpty()) 
				{
					// If create mesh fails here we'll wind up with an invalid/empty node uid for the entry.
					// That's ok however because the payload processing will validate all entries anyway.
					Entry->NodeUid = CreateMeshNode(Entry->PrimPath, Entry->Category);
				}
			}

			// Skeletal mesh - now create base meshes and register dependencies on the parts we created
			// above. Note also that we only need to apply the dependencies to one of the base meshes
			// since Interchange will currently combine all the meshes anyway.
			if (MeshTypeIs(ENaniteAssemblyMeshType::SkeletalMesh))
			{
				static const TArray<FString> EmptyDependencyArray;
				bool bIsFirst = true;
				for (TSharedPtr<FMeshEntry>& Entry : Result->GetEntriesForCategory(EMeshCategory::Base))
				{
					const TArray<FString> PartDependencies = bIsFirst 
						? Result->GetMeshUidsForCategory(EMeshCategory::Part) 
						: EmptyDependencyArray;

					Entry->NodeUid = CreateMeshNode(Entry->PrimPath, Entry->Category, PartDependencies);

					bIsFirst = false;
				}
			}
			// Static mesh - the NaniteAssemblyRootAPI schema doesn't currently support specifying a base 
			// mesh when in static mesh mode. So here we create a dummy mesh node that will result in an 
			// empty mesh that the mesh factory can eventually add the assembly data to. 
			else if (MeshTypeIs(ENaniteAssemblyMeshType::StaticMesh))
			{
				const FString MeshUid = MakeNodeUid(MeshPrefix + AssemblyRootPrim.GetPrimPath().GetString());
				const FString NodeName(AssemblyRootPrim.GetPrimPath().GetName());

				UInterchangeMeshNode* MeshNode = NewObject<UInterchangeMeshNode>(&NodeContainer);
				NodeContainer.SetupNode(MeshNode, MeshUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset);
				MeshNode->SetAssetName(NodeName);
				MeshNode->SetPayLoadKey(AssemblyRootPrim.GetPrimPath().GetString(), EInterchangeMeshPayLoadType::STATIC);

				for (const FString& PartUid : Result->GetMeshUidsForCategory(EMeshCategory::Part))
				{
					MeshNode->AddAssemblyPartDependencyUid(PartUid);
				}

				ParentSceneNode->SetCustomAssetInstanceUid(MeshUid);

				// Store the traversal result and cache the info for the mesh
				Info.NaniteAssemblyTraversalResult = Result;
				FWriteScopeLock ScopedInfoWriteLock{ TranslatorImpl.CachedTraversalInfoLock };
				TranslatorImpl.NodeUidToCachedTraversalInfo.Add(MeshUid, Info);
			}
		}
	};

	void UInterchangeUSDTranslatorImpl::AddNaniteAssemblyHierarchy(
		const UE::FUsdPrim& Prim,
		UInterchangeUsdTranslatorSettings* TranslatorSettings,
		UInterchangeBaseNodeContainer& NodeContainer,
		UInterchangeSceneNode* ParentSceneNode,
		FTraversalInfo& Info)
	{
		FNaniteAssemblyTraversalHelper NaniteAssemblyTraversalHelper(*this, NodeContainer, TranslatorSettings, ParentSceneNode);
		NaniteAssemblyTraversalHelper.Traverse(Prim, Info, FTransform());
	}

	//Helper to traverse PointInstancer subhierarchy and process it into InstancedStaticMeshComponents (only).
	struct FISMComponentTraversalHelper
	{
		UInterchangeUSDTranslatorImpl& TranslatorImpl;
		UInterchangeBaseNodeContainer& NodeContainer;
		UInterchangeUsdTranslatorSettings* TranslatorSettings;
		UInterchangeSceneNode* ParentSceneNode;
		const FString ParentSceneNodeUid;

		//While we could lookup from the NodeContainer, 
		// we are implementing a LookUp table specifically for ISMComponentNodes for speed:
		TMap<FString, UInterchangeInstancedStaticMeshComponentNode*> UidToComponentNode;

		FISMComponentTraversalHelper(
			UInterchangeUSDTranslatorImpl& InTranslatorImpl,
			UInterchangeBaseNodeContainer& InNodeContainer,
			UInterchangeUsdTranslatorSettings* InTranslatorSettings,
			UInterchangeSceneNode* InParentSceneNode)
			: TranslatorImpl(InTranslatorImpl)
			, NodeContainer(InNodeContainer)
			, TranslatorSettings(InTranslatorSettings)
			, ParentSceneNode(InParentSceneNode)
			, ParentSceneNodeUid(ParentSceneNode->GetUniqueID())
		{

		}

		UInterchangeInstancedStaticMeshComponentNode* CreateSceneComponent(const FString& MeshUid)
		{
			FString UniqueId = MakeNodeUid(ComponentPrefix + TEXT("\\") + ParentSceneNodeUid + TEXT("\\") + MeshUid);

			UInterchangeInstancedStaticMeshComponentNode* ISMComponentNode = UidToComponentNode.FindRef(UniqueId);

			if (ISMComponentNode)
			{
				return ISMComponentNode;
			}

			const UInterchangeBaseNode* MeshNodeOrLODContainer = NodeContainer.GetNode(MeshUid);
			if (!MeshNodeOrLODContainer)
			{
				return nullptr;
			}

			ISMComponentNode = NewObject<UInterchangeInstancedStaticMeshComponentNode>(&NodeContainer);
			FString DisplayLabel = MeshNodeOrLODContainer->GetDisplayLabel();

			NodeContainer.SetupNode(ISMComponentNode, UniqueId, DisplayLabel, EInterchangeNodeContainerType::TranslatedScene, ParentSceneNode->GetUniqueID());

			ParentSceneNode->AddComponentUid(ISMComponentNode->GetUniqueID());

			UidToComponentNode.Add(UniqueId, ISMComponentNode);

			return ISMComponentNode;
		}

		FString ConvertLodContainerPrim(const UE::FUsdPrim& Prim, FString& ActiveVariantUSDPrimPath, const FTraversalInfo& Info)
		{
			FString LODContainerUid = LODContainerSuffix + TEXT("\\") + Prim.GetPrimPath().GetString();
			FString ComponentDisplayLabel(Prim.GetName().ToString());

			UE::FUsdVariantSets VariantSets = Prim.GetVariantSets();
			const UE::FUsdVariantSet LODVariantSet = VariantSets.GetVariantSet(LODString);
			if (!LODVariantSet.IsValid())
			{
				return TEXT("");
			}

			const FString ActiveVariant = LODVariantSet.GetVariantSelection();
			if (!IsValidLODName(ActiveVariant))
			{
				return TEXT("");
			}

			if (const UInterchangeBaseNode* Node = NodeContainer.GetNode(LODContainerUid))
			{
				if (UE::FUsdPrim ActiveLODMeshPrim = GetLODMesh(Prim, ActiveVariant))
				{
					ActiveVariantUSDPrimPath = ActiveLODMeshPrim.GetPrimPath().GetString();
				}

				return LODContainerUid;
			}

			UInterchangeMeshLODContainerNode* LODContainerComponentNode = NewObject<UInterchangeMeshLODContainerNode>(&NodeContainer);
			NodeContainer.SetupNode(LODContainerComponentNode, LODContainerUid, ComponentDisplayLabel, EInterchangeNodeContainerType::TranslatedScene);

			bool bSwitchedFromInitialVariant = false;
			TArray<FString> VariantNames = LODVariantSet.GetVariantNames();
			for (const FString& VariantName : VariantNames)
			{
				// The active variant will be parsed via regular traversal
				if (!IsValidLODName(VariantName))
				{
					continue;
				}

				{
					// For creating the scene nodes themselves we'll switch the active variant on the currently opened stage
					// (still using the session layer to minimize impact to the actual layer). This mainly so that we can retrieve
					// and fetch and cache the correct material bindings for the LOD meshes. Later on we'll use separate stages
					// with population masks to read the LODs concurrently, and we won't be able to resolve material bindings
					UE::FUsdEditContext Context{ TranslatorImpl.UsdStage, TranslatorImpl.UsdStage.GetSessionLayer() };

					bool bSwitchedVariant = VariantSets.SetSelection(LODString, VariantName);
					if (!bSwitchedVariant)
					{
						continue;
					}
					bSwitchedFromInitialVariant = true;
				}

				if (UE::FUsdPrim LODMeshPrim = GetLODMesh(Prim, VariantName))
				{
					LODContainerComponentNode->AddMeshLODNodeUid(TranslatorImpl.AddMeshNode(LODMeshPrim, TranslatorSettings, NodeContainer, Info));

					if (VariantName == ActiveVariant)
					{
						ActiveVariantUSDPrimPath = LODMeshPrim.GetPrimPath().GetString();
					}
				}
				else
				{
					UE_LOG(LogUsd, Warning, TEXT("Failed to parse a LOD Mesh from variant '%s' of prim '%s'. For automatic parsing of LODs, make sure there is a single Mesh prim within the variant, named exactly as the variant itself (e.g. 'LOD0', 'LOD1', etc.)"), *VariantName, *Prim.GetPrimPath().GetString());
				}
			}

			// Put the active variant back to what it originally was
			if (bSwitchedFromInitialVariant)
			{
				{
					UE::FUsdEditContext Context{ TranslatorImpl.UsdStage, TranslatorImpl.UsdStage.GetSessionLayer() };

					const bool bRestoredSelection = VariantSets.SetSelection(LODString, ActiveVariant);
					ensure(bRestoredSelection);
				}
			}

			return LODContainerUid;
		}

		void TraverseNestedPointInstancerHierarchy(
			const UE::FUsdPrim& Prim,
			FTraversalInfo Info,
			const FTransform& ParentGlobalTransform
		)
		{
			const FUsdStageInfo StageInfo(Prim.GetStage());

			FTransform xFormTransform = FTransform::Identity;
			bool bResetTransformStack = false;
			UsdToUnreal::ConvertXformable(Prim.GetStage(), UE::FUsdTyped(Prim), xFormTransform, UsdUtils::GetEarliestTimeCode(), &bResetTransformStack);

			Info.UpdateWithCurrentPrim(Prim);

			FTransform CurrentGlobalTransform = xFormTransform * ParentGlobalTransform;

			if (Prim.IsA(TEXT("PointInstancer")))
			{
				TraversePointInstancerHierarchy(Prim, Info, CurrentGlobalTransform);
			}
			else if (Info.bIsLODContainer)
			{
				FString ActiveVariantUSDPrimPath;
				FString LODContainerUid = ConvertLodContainerPrim(Prim, ActiveVariantUSDPrimPath, Info);

				if (Info.bVisible)
				{
					UInterchangeInstancedStaticMeshComponentNode* ISMComponentNode = CreateSceneComponent(LODContainerUid);

					if (!ISMComponentNode)
					{
						return;
					}

					ISMComponentNode->AddInstanceTransform(CurrentGlobalTransform);
					if (LODContainerUid.Len())
					{
						ISMComponentNode->SetCustomInstancedAssetUid(LODContainerUid);
					}
				}

				TArray<FUsdPrim> Children = Prim.GetChildren();
				if (Children.Num())
				{
					for (FUsdPrim& Child : Children)
					{
						if (ActiveVariantUSDPrimPath == Child.GetPrimPath().GetString())
						{
							TArray<FUsdPrim> ActiveVariantChildren = Child.GetChildren();
							for (const FUsdPrim& ActiveVariantChild : ActiveVariantChildren)
							{
								TraverseNestedPointInstancerHierarchy(ActiveVariantChild, Info, CurrentGlobalTransform);
							}
						}
						else
						{
							TraverseNestedPointInstancerHierarchy(Child, Info, CurrentGlobalTransform);
						}
					}
				}
			}
			else if (Prim.IsA(TEXT("Mesh")))
			{
				if (Info.bVisible)
				{
					FString MeshUid = TranslatorImpl.AddMeshNode(Prim, TranslatorSettings, NodeContainer, Info);
					//This could be a basic StaticMesh, but currently we only have support for InstancedStaticMeshes. (within the context of Components)
					UInterchangeInstancedStaticMeshComponentNode* ISMComponentNode = Cast<UInterchangeInstancedStaticMeshComponentNode>(CreateSceneComponent(MeshUid));

					if (!ISMComponentNode)
					{
						return;
					}

					ISMComponentNode->AddInstanceTransform(CurrentGlobalTransform);
					ISMComponentNode->SetCustomInstancedAssetUid(MeshUid);
				}
			}
			else
			{
				TArray<FUsdPrim> Children = Prim.GetChildren();
				for (const FUsdPrim& Child : Children)
				{
					TraverseNestedPointInstancerHierarchy(Child, Info, CurrentGlobalTransform);
				}
			}
		}

		void TraversePointInstancerHierarchy(
			const UE::FUsdPrim& Prim,
			FTraversalInfo Info,
			const FTransform& ParentGlobalTransform
		)
		{
			UsdUtils::FUsdPointInstancerInstancesData InstancesData;
			UsdUtils::GetPointInstancerInstancesData(Prim, InstancesData);

			const FUsdStageInfo StageInfo(Prim.GetStage());

			for (int32 PrototypeIndex = 0; PrototypeIndex < InstancesData.Prototypes.Num(); ++PrototypeIndex)
			{
				UsdUtils::FUsdPointInstancerInstancesData::FUsdPrototypeInstances& PrototypeInstanceData = InstancesData.Prototypes[PrototypeIndex];
				TArray<FTransform>& PrototypeInstanceTransforms = PrototypeInstanceData.InstanceTransforms;
				FString PrototypePathString = PrototypeInstanceData.PathString;

				UE::FUsdPrim PrototypePrim = Prim.GetStage().GetPrimAtPath(UE::FSdfPath{ *PrototypePathString });
				if (!PrototypePrim)
				{
					UE_LOG(LogUsd, Warning, TEXT("Failed to find prototype '%s' for point instancer prim '%s'"), *PrototypePathString, *Prim.GetPrimPath().GetString());
					continue;
				}

				FTraversalInfo PrototypePrimInfo = Info;
				PrototypePrimInfo.UpdateWithCurrentPrim(PrototypePrim);

				if (PrototypePrim.IsA(TEXT("Mesh")))
				{
					if (PrototypePrimInfo.bVisible)
					{
						FTransform xFormTransform = FTransform::Identity;
						bool bResetTransformStack = false;
						UsdToUnreal::ConvertXformable(PrototypePrim.GetStage(), UE::FUsdTyped(PrototypePrim), xFormTransform, UsdUtils::GetEarliestTimeCode(), &bResetTransformStack);

						for (FTransform& Transform : PrototypeInstanceTransforms)
						{
							Transform = xFormTransform * Transform * ParentGlobalTransform;
						}

						FString MeshUid = TranslatorImpl.AddMeshNode(PrototypePrim, TranslatorSettings, NodeContainer, PrototypePrimInfo);
						UInterchangeInstancedStaticMeshComponentNode* ISMComponentNode = CreateSceneComponent(MeshUid);

						if (!ISMComponentNode)
						{
							continue;
						}

						ISMComponentNode->AddInstanceTransforms(PrototypeInstanceTransforms);
						ISMComponentNode->SetCustomInstancedAssetUid(MeshUid);
					}
				}
				else
				{
					for (int32 InstanceCounter = 0; InstanceCounter < PrototypeInstanceTransforms.Num(); ++InstanceCounter)
					{
						//Note: For handling the point instancer's instance's transform we create a component here. This is an extra introduced component compared to the original hierarchy.
						FTransform CurrentGlobalTransform = PrototypeInstanceTransforms[InstanceCounter] * ParentGlobalTransform;

						TraverseNestedPointInstancerHierarchy(PrototypePrim, PrototypePrimInfo, CurrentGlobalTransform);
					}
				}
			}
		}
	};


	FString UInterchangeUSDTranslatorImpl::AddMaterialNode(
		const UE::FUsdPrim& Prim,
		UInterchangeUsdTranslatorSettings* TranslatorSettings,
		UInterchangeBaseNodeContainer& NodeContainer,
		bool bForceTwoSided
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeUSDTranslatorImpl::AddMaterialNode)

		FName RenderContext = TranslatorSettings ? TranslatorSettings->RenderContext : UnrealIdentifiers::UniversalRenderContext;

		// If this material has an unreal surface output and we're in the unreal render context, just emit a material reference,
		// as we never want this to become a UMaterial / UMaterialInstance anyway.
		//
		// We could just early out here completely and not emit anything, as we also emit the material reference node on-demand, whenever
		// we parse an actual material assignment from a Mesh. The user may have custom pipelines that expect to find these though,
		// even if no mesh is actually using the materials
		if (RenderContext == UnrealIdentifiers::UnrealRenderContext)
		{
			TOptional<FString> UnrealContentPath = UsdUtils::GetUnrealSurfaceOutput(Prim);
			if (UnrealContentPath.IsSet())
			{
				return {UE::InterchangeUsdTranslator::Private::AddUnrealMaterialReferenceNodeIfNeeded(NodeContainer, UnrealContentPath.GetValue())};
			}
		}

		const FString PrimPath = UsdUtils::GetPrototypePrimPath(Prim).GetString();
		FString MaterialPrimName = Prim.GetName().ToString();
		FString MaterialUid = MaterialPrefix + PrimPath;
		FString MaterialNodeName = MaterialPrimName;

		if (bForceTwoSided)
		{
			MaterialUid += TwoSidedSuffix;
			MaterialNodeName += TwoSidedSuffix;
		}

		if (const UInterchangeMaterialInstanceNode* Node = Cast<UInterchangeMaterialInstanceNode>(NodeContainer.GetNode(MaterialUid)))
		{
			return MaterialUid;
		}

		// we only create material instances if we didn't find any MaterialX instances (including a shader graph)
		if (TArray<FUsdMaterialXShaderGraph::FGeomProp> GeomPropValues;
			AddMaterialXShaderGraph(Prim, TranslatorSettings, NodeContainer, GeomPropValues))
		{
			FString MaterialXMaterialUid = GetMaterialXMaterialUid(MaterialPrimName, NodeContainer);
			if (bForceTwoSided)
			{
				MaterialXMaterialUid = GetOrCreateTwoSidedShaderGraphNode(MaterialXMaterialUid, NodeContainer);
			}

			if (GeomPropValues.Num() > 0)
			{
				MaterialUidToGeomProps.Add(MaterialXMaterialUid, GeomPropValues);
			}

			return MaterialXMaterialUid;
		}

		UInterchangeMaterialInstanceNode* MaterialNode = NewObject<UInterchangeMaterialInstanceNode>(&NodeContainer);
		NodeContainer.SetupNode(MaterialNode, MaterialUid, MaterialNodeName, EInterchangeNodeContainerType::TranslatedAsset);
		MaterialNode->SetAssetName(MaterialNodeName);

		UsdToUnreal::FUsdPreviewSurfaceMaterialData MaterialData;
		const bool bSuccess = UsdToUnreal::ConvertMaterial(Prim, MaterialData, *RenderContext.ToString());

		// Set all the parameter values to the interchange node
		bool bNeedsVTParent = false;
		FMaterialInstanceParameterValueVisitor Visitor{Prim, NodeContainer, *MaterialNode, MaterialData.PrimvarToUVIndex};
		for (TPair<FString, UsdToUnreal::FParameterValue>& Pair : MaterialData.Parameters)
		{
			Visitor.BaseParameterName = &Pair.Key;
			Visit(Visitor, Pair.Value);

			// Also simultaneously check if any of these parameters wants to be an UDIM texture so that we can use the VT reference material later
			if (!bNeedsVTParent)
			{
				if (UsdToUnreal::FTextureParameterValue* TextureParameter = Pair.Value.TryGet<UsdToUnreal::FTextureParameterValue>())
				{
					if (TextureParameter->bIsUDIM)
					{
						bNeedsVTParent = true;
					}
				}
			}
		}

		// Also set our parameter to uv index mapping as-is also as custom attributes, so that the USD Pipeline can make
		// primvar-compatible materials
		{
			// Parameter to primvar
			for (const TPair<FString, UsdToUnreal::FParameterValue>& Parameter : MaterialData.Parameters)
			{
				if (const UsdToUnreal::FTextureParameterValue* TextureParameterValue = Parameter.Value.TryGet<UsdToUnreal::FTextureParameterValue>())
				{
					const FString& MaterialParameter = Parameter.Key;
					const FString& Primvar = TextureParameterValue->Primvar;

					MaterialNode->AddStringAttribute(UE::Interchange::USD::ParameterToPrimvarAttributePrefix + MaterialParameter, Primvar);
				}
			}

			// Primvar to UVIndex
			for (const TPair<FString, int32>& Pair : MaterialData.PrimvarToUVIndex)
			{
				const FString& Primvar = Pair.Key;
				int32 UVIndex = Pair.Value;

				MaterialNode->AddInt32Attribute(UE::Interchange::USD::PrimvarUVIndexAttributePrefix + Primvar, UVIndex);
			}

			// Let the pipeline know that it should process this node and handle these attributes we just added
			if (MaterialData.PrimvarToUVIndex.Num() > 0 || MaterialData.Parameters.Num() > 0)
			{
				MaterialNode->AddBooleanAttribute(UE::Interchange::USD::ParseMaterialIdentifier, true);
			}
		}

		EUsdReferenceMaterialProperties Properties = EUsdReferenceMaterialProperties::None;
		if (UsdUtils::IsMaterialTranslucent(MaterialData))
		{
			Properties |= EUsdReferenceMaterialProperties::Translucent;
		}
		if (bForceTwoSided)
		{
			Properties |= EUsdReferenceMaterialProperties::TwoSided;
		}
		if (bNeedsVTParent)
		{
			// TODO: Proper VT texture support (we'd need to know the texture resolution at this point, and we haven't parsed them yet...).
			// The way it currently works on Interchange is that the factory will create a VT or nonVT version of the texture to match the
			// material parameter slot. Since we'll currently never set the VT reference material, it essentially means it will always
			// downgrade our VT textures to non-VT.
			// The only exception is how we upgrade the reference material to VT in case we have any UDIM textures a few lines above,
			// as those are trivial to check for (we don't have to actually load the textures to do it)
			Properties |= EUsdReferenceMaterialProperties::VT;
		}

		FSoftObjectPath ParentMaterial = UsdUnreal::MaterialUtils::GetReferencePreviewSurfaceMaterial(Properties);
		if (ParentMaterial.IsValid())
		{
			MaterialNode->SetCustomParent(ParentMaterial.GetAssetPathString());
		}

		return MaterialUid;
	}

	FString UInterchangeUSDTranslatorImpl::GetTextureSourcePath(const FString& TexturePathOnDisk)
	{
		if (!USDZFilePath.IsEmpty())
		{
			return USDZFilePath;
		}
		else
		{
			return TexturePathOnDisk;
		}
	}

	void UInterchangeUSDTranslatorImpl::CleanUpDecompressedUSDZFolder()
	{
		if (!DecompressedUSDZRoot.IsEmpty())
		{
			const bool bRequireExists = false;
			const bool bTree = true;
			IFileManager::Get().DeleteDirectory(*FPaths::GetPath(DecompressedUSDZRoot), bRequireExists, bTree);
		}

		USDZFilePath.Reset();
		DecompressedUSDZRoot.Reset();
	}

	void UInterchangeUSDTranslatorImpl::SetupTranslationContext(const UInterchangeUsdTranslatorSettings& Settings)
	{
		if (!TranslationContext)
		{
			TranslationContext = MakeShared<FUsdSchemaTranslationContext>(UsdStage);
		}

		TranslationContext->bIsImporting = true;
		TranslationContext->Time = UsdUtils::GetDefaultTimeCode();
		TranslationContext->bMergeIdenticalMaterialSlots = true;	// Interchange always does this
		TranslationContext->bAllowInterpretingLODs = false;			// We don't support USD LODs yet

		TranslationContext->PurposesToLoad = EUsdPurpose::Default | EUsdPurpose::Proxy | EUsdPurpose::Render | EUsdPurpose::Guide;
		TranslationContext->RenderContext = Settings.RenderContext;
		TranslationContext->MaterialPurpose = Settings.MaterialPurpose;
	}

	bool DecompressUSDZFileToTempFolder(const FString& InUSDZFilePath, FString& OutDecompressedUSDZRoot)
	{
		const bool bIncludeDot = false;
		FString Extension = FPaths::GetExtension(InUSDZFilePath, bIncludeDot).ToLower();
		if (Extension == TEXT("usdz"))
		{
			const FString Prefix = FPaths::GetBaseFilename(InUSDZFilePath);
			const FString EmptyExtension = TEXT("");
			const FString TempFolder = FPaths::CreateTempFilename(FPlatformProcess::UserTempDir(), *Prefix, *EmptyExtension);
			FString DecompressedRoot;
			const bool bSuccess = UsdUtils::DecompressUSDZFile(InUSDZFilePath, TempFolder, &DecompressedRoot);
			if (bSuccess && !DecompressedRoot.IsEmpty())
			{
				OutDecompressedUSDZRoot = DecompressedRoot;
				return true;
			}
			else
			{
				UE_LOG(LogUsd, Warning, TEXT("Failed to decompress USDZ file '%s': Textures may not be handled correctly."), *InUSDZFilePath);
			}
		}

		return false;
	}

	UE::FUsdPrim UInterchangeUSDTranslatorImpl::TryGettingInactiveLODPrim(const FString& PrimPathString)
	{
		if (PrimPathString.IsEmpty())
		{
			return {};
		}

		UE::FSdfPath PrimPath{*PrimPathString};

		const FString PrimName = PrimPath.GetName();
		if (!IsValidLODName(PrimName))
		{
			return {};
		}

		const UE::FSdfPath LODContainerPath = PrimPath.GetParentPath();
		const FString LODContainerPathString = LODContainerPath.GetString();
		const FString& VariantName = PrimName;	  // Our convention for LODs is that the prim name matches the variant (e.g. "LOD2")

		UE::FUsdStage TempStage;

		{
			FWriteScopeLock Lock{PrimPathToVariantToStageLock};

			// Check if we have the stage we want already
			if (TMap<FString, UE::FUsdStage>* TempStagesForPrim = PrimPathToVariantToStage.Find(LODContainerPathString))
			{
				if (UE::FUsdStage* TempStagesForVariant = TempStagesForPrim->Find(VariantName))
				{
					TempStage = *TempStagesForVariant;
				}
			}

			// Open a brand new stage we can freely flip our variants in without invalidating other prim references from concurrent tasks.
			// We won't use the stage cache for these, so our strong reference right here is the only thing holding the stage opened
			if (!TempStage)
			{
				// We used to use a population mask here to only compose the prim subtree of the LOD prim we want to parse, but
				// that is too prone to edge case issues as any relationship to prims outside the loaded subtree would fail and 
				// emit errors (blend shapes, material assignments, etc.)
				const bool bUseStageCache = false;
				TempStage = UnrealUSDWrapper::OpenStage(*UsdStage.GetRootLayer().GetIdentifier(), EUsdInitialLoadSet::LoadAll, bUseStageCache);

				if (!TempStage)
				{
					return {};
				}

				UE::FUsdPrim LODContainerPrim = TempStage.GetPrimAtPath(LODContainerPath);
				if (!LODContainerPrim)
				{
					return {};
				}

				// We have to edit the session layer here, and not the root layer directly. This because USD only opens a layer
				// once in memory, so if we have multiple of these temp stages all trying to set the variant to different values
				// on the root layer itself, they'd be actually trying to overwrite each other and could even lead to threading issues.
				//
				// The session layer however is unique to each of these temp stages so we won't have that problem, and it still
				// should compose the variant switch just the same.
				UE::FUsdEditContext Context{TempStage, TempStage.GetSessionLayer()};

				UE::FUsdVariantSets VariantSets = LODContainerPrim.GetVariantSets();
				bool bSwitched = VariantSets.SetSelection(LODString, VariantName);
				if (!bSwitched)
				{
					return {};
				}

				// We finally have our stage for the particular lod container and with the variant we want: Cache it for later, if needed.
				PrimPathToVariantToStage.FindOrAdd(LODContainerPathString).Add(VariantName, TempStage);
			}
		}

		return TempStage.GetPrimAtPath(PrimPath);
	}

	void UInterchangeUSDTranslatorImpl::FinalizeLODContainerTraversal(
		UInterchangeBaseNodeContainer& NodeContainer,
		const FTraversalInfo& Info,
		const UInterchangeSceneNode* SceneNodeWithLODs
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeUSDTranslatorImpl::FinalizeLODContainerTraversal)

		if (!SceneNodeWithLODs)
		{
			return;
		}

		// Add a dedicated LODContainer node. We need this because every child of a LodGroup node
		// will be interpreted as a LOD mesh, and we could have ended up with any additional children so far
		// since any regular prim can contain the LOD variant (it could have any number of other children like
		// other transforms, lights, skeleton prims, etc.)
		const FString LODContainerUid = SceneNodeWithLODs->GetUniqueID() + LODContainerSuffix;
		UInterchangeSceneNode* LODContainer = NewObject<UInterchangeSceneNode>(&NodeContainer);
		NodeContainer.SetupNode(LODContainer, LODContainerUid, SceneNodeWithLODs->GetDisplayLabel(), EInterchangeNodeContainerType::TranslatedScene, SceneNodeWithLODs->GetUniqueID());
		LODContainer->AddSpecializedType(UE::Interchange::FSceneNodeStaticData::GetLodGroupSpecializeTypeString());

		struct FLODSortHelper
		{
			UInterchangeSceneNode* SceneNode = nullptr;
			int32 LODIndex;	   // Using actual ints in here instead of sorting on the node DisplayLabel directly so we correctly sort LOD10 > LOD2
		};

		// If we have less than two LODs, then we don't need to spawn a multi-LOD mesh and can just early out
		TArray<UInterchangeSceneNode*>* CurrentLODSceneNodes = LODVariantSetNodeToLodMeshNodes.Find(SceneNodeWithLODs);
		if (!CurrentLODSceneNodes || CurrentLODSceneNodes->Num() < 2)
		{
			LODVariantSetNodeToLodMeshNodes.Remove(SceneNodeWithLODs);
			return;
		}

		TArray<FLODSortHelper> SortedLODs;
		SortedLODs.Reserve(CurrentLODSceneNodes->Num());
		for (UInterchangeSceneNode* Node : *CurrentLODSceneNodes)
		{
			SortedLODs.Add({Node, GetLODIndexFromName(Node->GetDisplayLabel())});
		}
		SortedLODs.Sort(
			[](const FLODSortHelper& LHS, const FLODSortHelper& RHS)
			{
				return LHS.LODIndex < RHS.LODIndex;
			}
		);

		// Parent our LOD nodes to the LODContainer in the right order, because Interchange will try assigning
		// those meshes to LOD numbers in the order it traverses the children, and there's no guarantee about the order we run
		// into the variants when traversing the stage (or the order with which they were authored)
		for (int32 Index = 0; Index < CurrentLODSceneNodes->Num(); ++Index)
		{
			const FLODSortHelper& PackedNode = SortedLODs[Index];
			const FString& NodeUID = PackedNode.SceneNode->GetUniqueID();

			NodeContainer.SetNodeParentUid(NodeUID, LODContainerUid);
			NodeContainer.SetNodeDesiredChildIndex(NodeUID, Index);
		}

		// Detect invalid setup of LOD morph targets: Morph targets used by LOD2 must be present in LOD1, and all of those must be present in LOD0.
		// On legacy USD support we used a hack to work around that, but it's not so trivial to do in Interchange and it's probably the wrong approach
		// anyway: we should just let the user know how to correct their data for Unreal instead
		bool bIsFirstLOD = true;
		TSet<const UInterchangeMeshNode*> AllowedMorphTargets;
		for (const FLODSortHelper& SortedLODNode : SortedLODs)
		{
			FString AssetInstanceUid;
			if (!SortedLODNode.SceneNode->GetCustomAssetInstanceUid(AssetInstanceUid))
			{
				continue;
			}

			const UInterchangeMeshNode* MeshNode = Cast<const UInterchangeMeshNode>(NodeContainer.GetNode(AssetInstanceUid));
			if (!MeshNode)
			{
				continue;
			}

			// Only skinned meshes have blend shapes / morph targets in USD
			if (!MeshNode->IsSkinnedMesh())
			{
				continue;
			}

			TArray<FString> LODMorphTargetUids;
			MeshNode->GetMorphTargetDependencies(LODMorphTargetUids);

			TSet<const UInterchangeMeshNode*> LODMorphTargets;
			LODMorphTargets.Reserve(LODMorphTargetUids.Num());

			for (const FString& MorphTargetUid : LODMorphTargetUids)
			{
				if (const UInterchangeMeshNode* MorphTargetNode = Cast<const UInterchangeMeshNode>(NodeContainer.GetNode(MorphTargetUid)))
				{
					LODMorphTargets.Add(MorphTargetNode);
				}
			}

			if (!bIsFirstLOD && !AllowedMorphTargets.Includes(LODMorphTargets))
			{
				UE_LOG(
					LogUsd,
					Warning,
					TEXT(
						"Invalid blend shape configuration for skeletal mesh LOD '%s': The set of blend shapes names used by lower LODs should include all blend shape names used by higher LODs"
					),
					*MeshNode->GetUniqueID()
				);
				break;
			}

			Swap(AllowedMorphTargets, LODMorphTargets);
			bIsFirstLOD = false;
		}

		LODVariantSetNodeToLodMeshNodes.Remove(SceneNodeWithLODs);
	}

	template<typename MeshOrSceneNodeType>
	void UInterchangeUSDTranslatorImpl::SetSlotMaterialDependencies(
		MeshOrSceneNodeType* MeshOrSceneNode,
		const UsdUtils::FUsdPrimMaterialAssignmentInfo& MaterialAssignments,
		UInterchangeUsdTranslatorSettings* TranslatorSettings,
		UInterchangeBaseNodeContainer& NodeContainer
	)
	{
		for (const UsdUtils::FUsdPrimMaterialSlot& Slot : MaterialAssignments.Slots)
		{
			// We do this because Interchange will, in some scenarios, merge material slots with identical slot names.
			// By using the source (which is the displaycolor desc / material prim path / unreal material content path)
			// we do end up with a goofy looking super long material slot names, but it will have Interchange only combine
			// slots if they really are pointing at the exact same thing
			FString SlotName = Slot.MaterialSource;

			// Get the Uid of the material instance that we'll end up assigning to this slot
			FString MaterialUid;
			switch (Slot.AssignmentType)
			{
				case UsdUtils::EPrimAssignmentType::DisplayColor:
				{
					// MaterialSource here is e.g. "!DisplayColor_0_1"
					MaterialUid = AddDisplayColorMaterialInstanceNodeIfNeeded(NodeContainer, Slot);
					break;
				}
				case UsdUtils::EPrimAssignmentType::MaterialPrim:
				{
					// MaterialSource here is the material prim path
					UE::FUsdPrim MaterialPrim = UsdStage.GetPrimAtPath(UE::FSdfPath{*Slot.MaterialSource});

					// Go through a prototype in case this material is inside an instance proxy, as this needs to match up with
					// the polygon group name we're going to add to our mesh description.
					UE::FUsdPrim MaterialPrototypePrim = UsdUtils::GetPrototypePrim(MaterialPrim);
					if (MaterialPrototypePrim != MaterialPrim)
					{
						SlotName = MaterialPrototypePrim.GetPrimPath().GetString();
					}

					MaterialUid = AddMaterialNode(MaterialPrototypePrim, TranslatorSettings, NodeContainer, Slot.bMeshIsDoubleSided);

					if (TArray<FUsdMaterialXShaderGraph::FGeomProp>* FoundGeomProps = MaterialUidToGeomProps.Find(MaterialUid))
					{
						AddPrimvarBakingAttributes(MeshOrSceneNode, MaterialUid, NodeContainer, *FoundGeomProps);
					}
					break;
				}
				case UsdUtils::EPrimAssignmentType::UnrealMaterial:
				{
					// MaterialSource here is the content path, e.g. "/Game/MyFolder/Red.Red"
					MaterialUid = AddUnrealMaterialReferenceNodeIfNeeded(NodeContainer, Slot.MaterialSource);
					break;
				}
				default:
				{
					ensure(false);
					break;
				}
			}

			MeshOrSceneNode->SetSlotMaterialDependencyUid(SlotName, *MaterialUid);
		}
	}

	FString AddLightNode(const UE::FUsdPrim& Prim, UInterchangeBaseNodeContainer& NodeContainer)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AddLightNode)

		FString NodeUid;

		NodeUid = MakeNodeUid(LightPrefix + Prim.GetPrimPath().GetString());
		FString NodeName(Prim.GetName().ToString());

		// Ref. UsdToUnreal::ConvertLight
		static const FString IntensityToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsIntensity);
		static const FString ExposureToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsExposure);
		static const FString ColorToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsColor);

		float Intensity = UsdUtils::GetAttributeValue<float>(Prim, IntensityToken);
		float Exposure = UsdUtils::GetAttributeValue<float>(Prim, ExposureToken);

		// Temporarily a bit awkward here as we updated our VtValue::Get() to not perform any type coercion, so we
		// need to do that ourselves. In the future we'll have a dedicated templated function to perform this process
		FLinearColor Color;
		UE::FUsdAttribute ColorAttribute = Prim.GetAttribute(*ColorToken);
		if (ColorAttribute && ColorAttribute.HasAuthoredValue())
		{
			UE::FVtValue Value;
			if (ColorAttribute.Get(Value))
			{
				const FString TypeName = Value.GetTypeName();
				if (TypeName == TEXT("GfVec3f"))
				{
					Color = FLinearColor{Value.Get<FVector3f>()};
				}
				else if (TypeName == TEXT("GfVec3h"))
				{
					Color = FLinearColor{Value.Get<FVector3h>()};
				}
				else if (TypeName == TEXT("GfVec3d"))
				{
					Color = FLinearColor{Value.Get<FVector3d>()};
				}
				if (TypeName == TEXT("GfVec4f"))
				{
					Color = FLinearColor{Value.Get<FVector4f>()};
				}
				else if (TypeName == TEXT("GfVec4h"))
				{
					Color = FLinearColor{Value.Get<FVector4h>()};
				}
				else if (TypeName == TEXT("GfVec4d"))
				{
					Color = FLinearColor{Value.Get<FVector4d>()};
				}
			}
		}

		const bool bSRGB = true;
		Color = Color.ToFColor(bSRGB);

		static const FString TemperatureToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsColorTemperature);
		static const FString UseTemperatureToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsEnableColorTemperature);

		float Temperature = UsdUtils::GetAttributeValue<float>(Prim, TemperatureToken);
		bool UseTemperature = UsdUtils::GetAttributeValue<bool>(Prim, UseTemperatureToken);

		// "Shadow enabled" currently not supported

		auto SetBaseLightProperties = [&NodeUid, &NodeName, Color, Temperature, UseTemperature, &NodeContainer](UInterchangeBaseLightNode* LightNode)
		{
			NodeContainer.SetupNode(LightNode, NodeUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset);
			LightNode->SetAssetName(NodeName);

			// TODO: Maybe only author these in case there were in fact authored opinions on the prims? Otherwise we're just authoring default values
			LightNode->SetCustomLightColor(Color);
			LightNode->SetCustomTemperature(Temperature);
			LightNode->SetCustomUseTemperature(UseTemperature);
		};

		static const FString RadiusToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsRadius);

		if (Prim.IsA(TEXT("DistantLight")))
		{
			UInterchangeDirectionalLightNode* LightNode = NewObject<UInterchangeDirectionalLightNode>(&NodeContainer);
			SetBaseLightProperties(LightNode);

			Intensity = UsdToUnreal::ConvertLightIntensityAttr(Intensity, Exposure);
			LightNode->SetCustomIntensity(Intensity);

			// LightSourceAngle currently not supported by UInterchangeDirectionalLightNode
			// float Angle = UsdUtils::GetAttributeValue<float>(Prim, TEXTVIEW("inputs:angle"));
		}
		else if (Prim.IsA(TEXT("SphereLight")))
		{
			const FUsdStageInfo StageInfo(Prim.GetStage());

			const float Radius = UsdUtils::GetAttributeValue<float>(Prim, RadiusToken);
			const float SourceRadius = UsdToUnreal::ConvertDistance(StageInfo, Radius);	   // currently not supported

			if (Prim.HasAPI(TEXT("ShapingAPI")))
			{
				UInterchangeSpotLightNode* LightNode = NewObject<UInterchangeSpotLightNode>(&NodeContainer);
				SetBaseLightProperties(LightNode);

				LightNode->SetCustomIntensityUnits(EInterchangeLightUnits::Lumens);

				static const FString ConeAngleToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsShapingConeAngle);
				static const FString ConeSoftnessToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsShapingConeSoftness);

				float ConeAngle = UsdUtils::GetAttributeValue<float>(Prim, ConeAngleToken);
				float ConeSoftness = UsdUtils::GetAttributeValue<float>(Prim, ConeSoftnessToken);

				float InnerConeAngle = 0.0f;
				const float OuterConeAngle = UsdToUnreal::ConvertConeAngleSoftnessAttr(ConeAngle, ConeSoftness, InnerConeAngle);

				Intensity = UsdToUnreal::ConvertLuxShapingAPIIntensityAttr(Intensity, Exposure, Radius, ConeAngle, ConeSoftness, StageInfo);
				LightNode->SetCustomIntensity(Intensity);

				LightNode->SetCustomInnerConeAngle(InnerConeAngle);
				LightNode->SetCustomOuterConeAngle(OuterConeAngle);
			}
			else
			{
				UInterchangePointLightNode* LightNode = NewObject<UInterchangePointLightNode>(&NodeContainer);
				SetBaseLightProperties(LightNode);

				LightNode->SetCustomIntensityUnits(EInterchangeLightUnits::Lumens);

				Intensity = UsdToUnreal::ConvertSphereLightIntensityAttr(Intensity, Exposure, Radius, StageInfo);
				LightNode->SetCustomIntensity(Intensity);
			}
		}
		else if (Prim.IsA(TEXT("RectLight")) || Prim.IsA(TEXT("DiskLight")))
		{
			UInterchangeRectLightNode* LightNode = NewObject<UInterchangeRectLightNode>(&NodeContainer);
			SetBaseLightProperties(LightNode);

			LightNode->SetCustomIntensityUnits(EInterchangeLightUnits::Lumens);

			static const FString WidthToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsWidth);
			static const FString HeightToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsHeight);

			float Width = UsdUtils::GetAttributeValue<float>(Prim, WidthToken);
			float Height = UsdUtils::GetAttributeValue<float>(Prim, HeightToken);

			const FUsdStageInfo StageInfo(Prim.GetStage());

			if (Prim.IsA(TEXT("RectLight")))
			{
				Width = UsdToUnreal::ConvertDistance(StageInfo, Width);
				Height = UsdToUnreal::ConvertDistance(StageInfo, Height);
				Intensity = UsdToUnreal::ConvertRectLightIntensityAttr(Intensity, Exposure, Width, Height, StageInfo);
			}
			else
			{
				float Radius = UsdUtils::GetAttributeValue<float>(Prim, RadiusToken);
				Width = UsdToUnreal::ConvertDistance(StageInfo, Radius) * 2.f;
				Height = Width;

				Intensity = UsdToUnreal::ConvertDiskLightIntensityAttr(Intensity, Exposure, Radius, StageInfo);
			}
			LightNode->SetCustomIntensity(Intensity);
			LightNode->SetCustomSourceWidth(Width);
			LightNode->SetCustomSourceHeight(Height);
		}
		else if (Prim.IsA(TEXT("DomeLight")))
		{
			UInterchangeSkyLightNode* LightNode = NewObject<UInterchangeSkyLightNode>(&NodeContainer);
			SetBaseLightProperties(LightNode);

			// This doesn't translate very well as SkyLightComponents seem to have an "IntensityScale", and there's never any clear
			// indication of what the dome light intensity actually is... But I don't think there's much we can do
			Intensity = UsdToUnreal::ConvertLightIntensityAttr(Intensity, Exposure);
			LightNode->SetCustomIntensity(Intensity);

			// Show some warning in case the file specifies a different DomeLight format, as we only support latlong
			static const FString FormatToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsTextureFormat);
			UE::FUsdAttribute FormatAttribute = Prim.GetAttribute(*FormatToken);
			FString Format;
			if (FormatAttribute.HasAuthoredValue() &&
				FormatAttribute.Get<FString>(Format) &&
				!Format.IsEmpty() &&
				Format != TEXT("automatic") &&
				Format != TEXT("latlong"))
			{
				UE_LOG(
					LogUsd,
					Warning,
					TEXT(
						"DomeLight '%s' specifies format '%s', but only 'latlong' DomeLights are supported for now. The texture will be treated as 'latlong'"
					),
					*Prim.GetPrimPath().GetString(),
					*Format
				);
			}

			static const FString FileToken = UsdToUnreal::ConvertToken(pxr::UsdLuxTokens->inputsTextureFile);
			UE::FUsdAttribute FileAttribute = Prim.GetAttribute(*FileToken);
			const FString ResolvedDomeTexturePath = UsdUtils::GetResolvedAssetPath(FileAttribute);

			FString TextureCubeNodeUid = AddTextureCubeNode(ResolvedDomeTexturePath, NodeContainer);
			if (!TextureCubeNodeUid.IsEmpty())
			{
				LightNode->SetCustomCubemapDependency(TextureCubeNodeUid);
				LightNode->SetCustomSourceType(EInterchangeSkyLightSourceType::SpecifiedCubemap);
			}

			// If this file doesn't exist then retrieving the payload will fail, but we emit the warning from here as
			// we can provide a better error message than the the payload retrieval function
			if (!FPaths::FileExists(ResolvedDomeTexturePath))
			{
				// Prefer to retrieve the original filepath string as this ResolvedDomeTexturePath will likely just
				// be the empty string if we failed to resolve
				FString RawFilePath = ResolvedDomeTexturePath;
				if (FileAttribute)
				{
					FileAttribute.Get(RawFilePath);
				}

				UE_LOG(
					LogUsd,
					Warning,
					TEXT("Failed to find the file '%s' specified by DomeLight prim '%s'"),
					*RawFilePath,
					*Prim.GetPrimPath().GetString()
				);
			}
		}

		return NodeUid;
	}

	FString AddCameraNode(const UE::FUsdPrim& Prim, UInterchangeBaseNodeContainer& NodeContainer)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AddCameraNode)

		FString NodeUid;

		NodeUid = CameraPrefix + MakeNodeUid(Prim.GetPrimPath().GetString());
		FString NodeName(Prim.GetName().ToString());

		UInterchangePhysicalCameraNode* CameraNode = NewObject<UInterchangePhysicalCameraNode>(&NodeContainer);
		NodeContainer.SetupNode(CameraNode, NodeUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset);

		// ref. UsdToUnreal::ConvertGeomCamera
		UE::FUsdStage Stage = Prim.GetStage();
		FUsdStageInfo StageInfo(Stage);

		static const FString FocalLengthToken = UsdToUnreal::ConvertToken(pxr::UsdGeomTokens->focalLength);
		static const FString HorizontalApertureToken = UsdToUnreal::ConvertToken(pxr::UsdGeomTokens->horizontalAperture);
		static const FString VerticalApertureToken = UsdToUnreal::ConvertToken(pxr::UsdGeomTokens->verticalAperture);

		float FocalLength = UsdUtils::GetAttributeValue<float>(Prim, FocalLengthToken);
		FocalLength = UsdToUnreal::ConvertDistance(StageInfo, FocalLength);
		CameraNode->SetCustomFocalLength(FocalLength);

		float SensorWidth = UsdUtils::GetAttributeValue<float>(Prim, HorizontalApertureToken);
		SensorWidth = UsdToUnreal::ConvertDistance(StageInfo, SensorWidth);
		CameraNode->SetCustomSensorWidth(SensorWidth);

		float SensorHeight = UsdUtils::GetAttributeValue<float>(Prim, VerticalApertureToken);
		SensorHeight = UsdToUnreal::ConvertDistance(StageInfo, SensorHeight);
		CameraNode->SetCustomSensorHeight(SensorHeight);

		// Focus distance and FStop not currently supported

		return NodeUid;
	}

	void AddMorphTargetNodes(
		const UE::FUsdPrim& MeshPrim,
		UInterchangeUSDTranslatorImpl& TranslatorImpl,
		UInterchangeMeshNode& MeshNode,
		UInterchangeBaseNodeContainer& NodeContainer,
		const FTraversalInfo& Info
	)
	{
		UE::FUsdSkelBlendShapeQuery Query{MeshPrim};
		if (!Query)
		{
			return;
		}

		const FString MeshPrimPath = UsdUtils::GetPrototypePrimPath(MeshPrim).GetString();

		TFunction<void(const FString&, int32, const FString&)> AddMorphTargetNode =
			[&MeshNode, &MeshPrimPath, &NodeContainer, &Info](const FString& MorphTargetName, int32 BlendShapeIndex, const FString& InbetweenName)
		{
			// Note: We identify a blend shape by its Mesh prim path and the blend shape index, even though
			// the blend shape itself is a full standalone prim. This is for two reasons:
			//  - We need to also read the Mesh prim's mesh data when emitting the payload, so having the Mesh path on the payload key is handy;
			//  - It could be possible for different meshes to share the same BlendShape (possibly?), so we really want a separate version of
			//    a blend shape for each mesh that uses it.
			//
			// Despite of that though, we won't use the blendshape's full path as the morph target name, so that users can get different
			// blendshapes across the model to combine into a single morph target. Interchange has an import option to let you control
			// whether they become separate morph targets or not anyway ("Merge Morph Targets with Same Name")
			const FString NodeUid = GetMorphTargetMeshNodeUid(MeshPrimPath, BlendShapeIndex, InbetweenName);
			const FString PayloadKey = GetMorphTargetMeshPayloadKey(Info.bInsideLOD, MeshPrimPath, BlendShapeIndex, InbetweenName);

			UInterchangeMeshNode* MorphTargetMeshNode = NewObject<UInterchangeMeshNode>(&NodeContainer);
			NodeContainer.SetupNode(MorphTargetMeshNode, NodeUid, MorphTargetName, EInterchangeNodeContainerType::TranslatedAsset);
			MorphTargetMeshNode->SetPayLoadKey(PayloadKey, EInterchangeMeshPayLoadType::MORPHTARGET);
			MorphTargetMeshNode->SetMorphTarget(true);
			MorphTargetMeshNode->SetMorphTargetName(MorphTargetName);
			MeshNode.SetMorphTargetDependencyUid(NodeUid);
		};

		for (size_t Index = 0; Index < Query.GetNumBlendShapes(); ++Index)
		{
			UE::FUsdSkelBlendShape BlendShape = Query.GetBlendShape(Index);
			if (!BlendShape)
			{
				continue;
			}
			UE::FUsdPrim BlendShapePrim = BlendShape.GetPrim();
			const FString BlendShapeName = BlendShapePrim.GetName().ToString();

			const FString UnusedInbetweenName;
			AddMorphTargetNode(BlendShapeName, Index, UnusedInbetweenName);

			for (const UE::FUsdSkelInbetweenShape& Inbetween : BlendShape.GetInbetweens())
			{
				const FString InbetweenName = Inbetween.GetAttr().GetName().ToString();
				const FString MorphTargetName = BlendShapeName + TEXT("_") + InbetweenName;
				AddMorphTargetNode(MorphTargetName, Index, InbetweenName);
			}
		}
	}

	EInterchangeMeshCollision ConvertApproximationType(EUsdCollisionType Approximation)
	{
		// References:
		// - InterchangeGenericStaticMeshPipeline.cpp, GetCollisionMeshType()
		// - InterchangeGenericStaticMeshPipeline.cpp, AddLodDataToStaticMesh()

		switch (Approximation)
		{
			// EInterchangeMeshCollision::None means no collision, so treat EUsdCollisionType::None
			// as convex collision instead
			case EUsdCollisionType::None:
			case EUsdCollisionType::ConvexDecomposition:
			case EUsdCollisionType::ConvexHull:
			case EUsdCollisionType::MeshSimplification:
			case EUsdCollisionType::CustomMesh:
			{
				return EInterchangeMeshCollision::Convex18DOP;
			}
			case EUsdCollisionType::Sphere:
			{
				return EInterchangeMeshCollision::Sphere;
			}
			case EUsdCollisionType::Cube:
			{
				return EInterchangeMeshCollision::Box;
			}
			case EUsdCollisionType::Capsule:
			{
				return EInterchangeMeshCollision::Capsule;
			}
			default:
			{
				ensure(false);
				break;
			}
		}

		return EInterchangeMeshCollision::None;
	}

	FString UInterchangeUSDTranslatorImpl::AddMeshNode(
		const UE::FUsdPrim& InstanceProxyPrim,
		UInterchangeUsdTranslatorSettings* TranslatorSettings,
		UInterchangeBaseNodeContainer& NodeContainer,
		const FTraversalInfo& Info,
		bool bPrimitiveShape,
		const TArray<FString>& AssemblyDependencies
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeUSDTranslatorImpl::AddMeshNode)

		const bool bIsSkinned = static_cast<bool>(Info.ClosestParentSkelRootPath) && InstanceProxyPrim.HasAPI(TEXT("SkelBindingAPI"));

		UE::FUsdPrim PrototypePrim;
		if (bIsSkinned || Info.bInsideLOD)
		{
			// Don't try to reuse asset nodes across instances for variant-style LODs: USD generates the prototype
			// paths on-demand, and as we flip through LODs one at a time, and later retrieve LOD mesh payload data
			// by opening other stages with population masks, there's zero guarantee our prototype paths will match
			//
			// Also temporarily disable instancing for skinned meshes as Interchange doesn't support that very well
			// yet (see UE-175330)
			PrototypePrim = InstanceProxyPrim;
		}
		else
		{
			// Go through the prototype (if any) so that we try reusing the same mesh asset node for different USD instances
			PrototypePrim = UsdUtils::GetPrototypePrim(InstanceProxyPrim);
		}

		const UE::FUsdStage Stage = PrototypePrim.GetStage();
		const FString PrototypePrimPath = PrototypePrim.GetPrimPath().GetString();
		const FString NodeUid = MakeNodeUid((bPrimitiveShape ? PrimitiveShapePrefix : MeshPrefix) + PrototypePrimPath);
		const FString NodeName(PrototypePrim.GetName().ToString());

		// Check if Node already exist with this ID
		if (const UInterchangeMeshNode* Node = Cast<UInterchangeMeshNode>(NodeContainer.GetNode(NodeUid)))
		{
			return NodeUid;
		}

		// Fill in the MeshNode itself
		const bool bIsAnimated = UsdUtils::IsAnimatedMesh(PrototypePrim);
		UInterchangeMeshNode* MeshNode = bIsAnimated ? NewObject<UInterchangeGeometryCacheNode>(&NodeContainer)
													 : NewObject<UInterchangeMeshNode>(&NodeContainer);
		NodeContainer.SetupNode(MeshNode, NodeUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset);
		MeshNode->SetAssetName(NodeName);

		FString PayloadKey = PrototypePrimPath;
		if (Info.bInsideLOD)
		{
			PayloadKey = LODPrefix + PayloadKey;
		}
		if (bPrimitiveShape)
		{
			// We are currently not supporting Skinned Primitive Shapes.
			// In theory a Skinned Mesh needs Joint influences and weights information provided,
			// However, there does not seem to be a ruleset against a Primitive Shape having SkelBindingAPI set.
			// Which means, that on a theoretical level there could be such a scenario.
			ensureMsgf(!bIsSkinned, TEXT("Unexpected scenario: Primitive Shape is skinned."));

			// For Primitive Shapes we add PrimitiveShapePrefix for the PayLoadKey,
			// in order to be able to identify the PrimitiveShape in the PayloadData acquisition phase,
			// as the Primitive Shapes require a different MeshDescription acquisition path, compared to Static Meshes.
			PayloadKey = PrimitiveShapePrefix + PayloadKey;
		}

		if (UInterchangeGeometryCacheNode* GeometryCacheNode = Cast<UInterchangeGeometryCacheNode>(MeshNode))
		{
			GeometryCacheNode->SetPayLoadKey(PrototypePrimPath, EInterchangeMeshPayLoadType::ANIMATED);

			int32 StartFrame = FMath::FloorToInt(Stage.GetStartTimeCode());
			int32 EndFrame = FMath::CeilToInt(Stage.GetEndTimeCode());
			UsdUtils::GetAnimatedMeshTimeCodes(Stage, PrototypePrimPath, StartFrame, EndFrame);

			double TimeCodesPerSecond = Stage.GetTimeCodesPerSecond();
			if (TimeCodesPerSecond <= 0)
			{
				UE_LOG(
					LogUsd,
					Warning,
					TEXT(
						"Stage '%s' has TimeCodesPerSecond set to '%f' which is not supported for GeometryCaches, which need values greater than zero. The GeometryCache assets will be parsed as if TimeCodesPerSecond was set to 1.0"
					),
					*Stage.GetRootLayer().GetIdentifier(),
					TimeCodesPerSecond
				);
				TimeCodesPerSecond = 1;
			}

			// The GeometryCache module expects the end frame to be one past the last animation frame
			EndFrame += 1;

			GeometryCacheNode->SetCustomStartFrame(StartFrame);
			GeometryCacheNode->SetCustomEndFrame(EndFrame);
			GeometryCacheNode->SetCustomFrameRate(TimeCodesPerSecond);

			const bool bConstantTopology = UsdUtils::GetMeshTopologyVariance(PrototypePrim) != UsdUtils::EMeshTopologyVariance::Heterogenous;
			GeometryCacheNode->SetCustomHasConstantTopology(bConstantTopology);
		}
		else if (bIsSkinned && !bPrimitiveShape)
		{
			MeshNode->SetSkinnedMesh(true);
			MeshNode->SetPayLoadKey(PayloadKey, EInterchangeMeshPayLoadType::SKELETAL);
			if (Info.BoundSkeletonPrimPath && !Info.BoundSkeletonPrimPath->IsEmpty())
			{
				MeshNode->SetSkeletonDependencyUid(MakeRootBoneNodeUid(*Info.BoundSkeletonPrimPath));
			}

			AddMorphTargetNodes(PrototypePrim, *this, *MeshNode, NodeContainer, Info);

			// Set skeletal mesh Nanite assembly dependencies, if any
			for (const FString& PartUid : AssemblyDependencies)
			{
				MeshNode->AddAssemblyPartDependencyUid(PartUid);
			}

			// When returning the payload data later, we'll need at the very least our SkeletonQuery,
			// and possibly Nanite assembly info, so here we store the Info object into the Impl
			{
				FWriteScopeLock ScopedInfoWriteLock{CachedTraversalInfoLock};
				NodeUidToCachedTraversalInfo.Add(NodeUid, Info);
			}
		}
		else
		{
			MeshNode->SetPayLoadKey(PayloadKey, EInterchangeMeshPayLoadType::STATIC);

			if (UsdUtils::IsCollisionEnabledForPrim(PrototypePrim))
			{
				// If the mesh prim is flagged for collision schemas AND also setup to be an FBX-style collision mesh, prefer
				// the FBX style. We do this here because if we setup both styles at the same time, then GetCollisionMeshType() from
				// InterchangeGenericStaticMeshPipeline.cpp would prefer the explicit collisions described on the translated node.
				// That seems like the right thing in general, but is not what we want for USD due to compatibility with the legacy
				// USD Importer code
				bool bSetCustomCollisionType = true;
				{
					const static TSet<FString> CollisionPrefixes = {TEXT("UBX"), TEXT("UCX"), TEXT("MCDCX"), TEXT("USP"), TEXT("UCP")};

					// Check if this mesh is an FBX-style collider
					{
						int32 FirstUnderscoreIndex = INDEX_NONE;
						if (NodeName.FindChar(TEXT('_'), FirstUnderscoreIndex) && FirstUnderscoreIndex != INDEX_NONE)
						{
							FString Prefix = NodeName.Left(FirstUnderscoreIndex);
							if (CollisionPrefixes.Contains(Prefix))
							{
								bSetCustomCollisionType = false;
							}
						}
					}

					// Check if we have any siblings that are also FBX-style colliders pointing at this mesh prim.
					// In that case we want to disable the collider for this mesh prim itself, so that it matches legacy USD behavior
					if (bSetCustomCollisionType)
					{
						TArray<UE::FUsdPrim> Siblings = PrototypePrim.GetParent().GetChildren();
						for (const UE::FUsdPrim& Sibling : Siblings)
						{
							if (Sibling == PrototypePrim || !UsdUtils::IsCollisionMesh(Sibling))
							{
								continue;
							}

							FString SiblingName = Sibling.GetName().ToString();

							int32 FirstUnderscoreIndex = INDEX_NONE;
							if (SiblingName.FindChar(TEXT('_'), FirstUnderscoreIndex) && FirstUnderscoreIndex != INDEX_NONE)
							{
								FString PotentialNodeName = SiblingName.RightChop(FirstUnderscoreIndex + 1);
								if (PotentialNodeName.StartsWith(NodeName))
								{
									bSetCustomCollisionType = false;
									break;
								}
							}
						}
					}
				}

				if (bSetCustomCollisionType)
				{
					EUsdCollisionType Approximation = UsdUtils::GetCollisionApproximationType(PrototypePrim);
					EInterchangeMeshCollision InterchangeApproximation = ConvertApproximationType(Approximation);
					if (InterchangeApproximation != EInterchangeMeshCollision::None)
					{
						MeshNode->SetCustomCollisionType(InterchangeApproximation);
					}
				}
			}

			// Set static mesh Nanite assembly dependencies, if any
			for (const FString& PartUid : AssemblyDependencies)
			{
				MeshNode->AddAssemblyPartDependencyUid(PartUid);
			}
		}

		// Material assignments
		{
			double TimeCode = UsdUtils::GetDefaultTimeCode();
			const bool bProvideMaterialIndices = false;
			UsdUtils::FUsdPrimMaterialAssignmentInfo PrototypeAssignments = UsdUtils::GetPrimMaterialAssignments(
				PrototypePrim,
				TimeCode,
				bProvideMaterialIndices,
				CachedMeshConversionOptions.RenderContext,
				CachedMeshConversionOptions.MaterialPurpose
			);

			if (Info.bInsideLOD)
			{
				CachedMaterialAssignments.Add(PrototypePrimPath, PrototypeAssignments);
			}

			// Move these into the asset node because the USD Pipeline will compare these with the assigned material's
			// parameter-to-primvar mapping in order to make sure the mesh is using a primvar-compatible material.
			//
			// Note that ideally we'd cache this mapping and reuse it on the payload retrieval step. Instead, we will
			// just end up calling the same function again during payload retrieval and hoping that it produces the same
			// primvar-to-UVIndex mapping. It should though, as the mesh conversion options are the same. We can't cache
			// the mapping because we run into USD allocator issues, given that all the strings contained in the
			// FUsdPrimMaterialAssignmentInfo object are allocated inside an USD allocator scope
			for (const TPair<FString, int32>& PrimvarPair : UsdUtils::GetPrimvarToUVIndexMap(PrototypePrim))
			{
				const FString& PrimvarName = PrimvarPair.Key;
				int32 UVIndex = PrimvarPair.Value;

				MeshNode->AddInt32Attribute(UE::Interchange::USD::PrimvarUVIndexAttributePrefix + PrimvarName, UVIndex);
			}

			SetSlotMaterialDependencies(MeshNode, PrototypeAssignments, TranslatorSettings, NodeContainer);
		}
		return NodeUid;
	}

	void UInterchangeUSDTranslatorImpl::AddLODMeshNodes(
		const UE::FUsdPrim& Prim,
		UInterchangeBaseNodeContainer& NodeContainer,
		UInterchangeSceneNode& ParentSceneNode,
		UInterchangeUsdTranslatorSettings* TranslatorSettings,
		FTraversalInfo Info
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeUSDTranslatorImpl::AddLODMeshNodes)

		UE::FUsdVariantSets VariantSets = Prim.GetVariantSets();
		const UE::FUsdVariantSet LODVariantSet = VariantSets.GetVariantSet(LODString);
		if (!LODVariantSet.IsValid())
		{
			return;
		}

		const FString ActiveVariant = LODVariantSet.GetVariantSelection();
		if (!IsValidLODName(ActiveVariant))
		{
			return;
		}

		Info.ParentNode = &ParentSceneNode;

		bool bSwitchedFromInitialVariant = false;
		for (const FString& VariantName : LODVariantSet.GetVariantNames())
		{
			// The active variant will be parsed via regular traversal
			if (VariantName == ActiveVariant || !IsValidLODName(VariantName))
			{
				continue;
			}

			{
				// For creating the scene nodes themselves we'll switch the active variant on the currently opened stage
				// (still using the session layer to minimize impact to the actual layer). This mainly so that we can retrieve
				// and fetch and cache the correct material bindings for the LOD meshes. Later on we'll use separate stages
				// with population masks to read the LODs concurrently, and we won't be able to resolve material bindings
				UE::FUsdEditContext Context{UsdStage, UsdStage.GetSessionLayer()};

				bool bSwitchedVariant = VariantSets.SetSelection(LODString, VariantName);
				if (!bSwitchedVariant)
				{
					continue;
				}
				bSwitchedFromInitialVariant = true;
			}

			if (UE::FUsdPrim LODMeshPrim = GetLODMesh(Prim, VariantName))
			{
				Traverse(LODMeshPrim, *this, NodeContainer, TranslatorSettings, Info);
			}
			else
			{
				UE_LOG(
					LogUsd,
					Warning,
					TEXT(
						"Failed to parse a LOD Mesh from variant '%s' of prim '%s'. For automatic parsing of LODs, make sure there is a single Mesh prim within the variant, named exactly as the variant itself (e.g. 'LOD0', 'LOD1', etc.)"
					),
					*VariantName,
					*Prim.GetPrimPath().GetString()
				);
			}
		}

		// Put the active variant back to what it originally was
		if (bSwitchedFromInitialVariant)
		{
			{
				UE::FUsdEditContext Context{UsdStage, UsdStage.GetSessionLayer()};

				const bool bRestoredSelection = VariantSets.SetSelection(LODString, ActiveVariant);
				ensure(bRestoredSelection);
			}

			// Recompute our skel cache here if we have any as ancestor, because switching variants could have
			// invalidated some of its internal state about its descendant prims, which we'll need to be OK
			// when handling the payloads
			Info.RepopulateSkelCache(UsdStage);
		}
	}

	void AddVolumeCustomAttributesToNode(const UsdUtils::FVolumePrimInfo& VolumePrimInfo, UInterchangeVolumeNode* VolumeNode)
	{
		using namespace UE::Interchange::USD;

		// Convert from the {'velocity': {'AttributesA.R': 'X', 'AttributesA.G': 'Y', 'AttributesA.B': 'Z'}} style of mapping
		// from grid info into {"AttributesA.X": "velocity_0", "AttributesA.G": "velocity_1", "AttributesA.B": "velocity_2"}
		// mapping into the VolumeNode custom attributes
		for (const TPair<FString, TMap<FString, FString>>& GridPair : VolumePrimInfo.GridNameToChannelComponentMapping)
		{
			const FString& GridName = GridPair.Key;	   // "density", "temperature", etc.

			const TMap<FString, FString>& AttributesChannelToGridChannel = GridPair.Value;
			for (const TPair<FString, FString>& AssignmentPair : AttributesChannelToGridChannel)
			{
				const FString& AttributeChannel = AssignmentPair.Key;	 // "AttributesA.R", "AttributesB.B", etc.
				const FString& GridChannel = AssignmentPair.Value;		 // "X", "Y", "Z", etc.

				const static TMap<FString, FString> AttributeChannelToAttributeKey{
					{TEXT("AttributesA.R"), SparseVolumeTexture::AttributesAChannelR},
					{TEXT("AttributesA.G"), SparseVolumeTexture::AttributesAChannelG},
					{TEXT("AttributesA.B"), SparseVolumeTexture::AttributesAChannelB},
					{TEXT("AttributesA.A"), SparseVolumeTexture::AttributesAChannelA},
					{TEXT("AttributesB.R"), SparseVolumeTexture::AttributesBChannelR},
					{TEXT("AttributesB.G"), SparseVolumeTexture::AttributesBChannelG},
					{TEXT("AttributesB.B"), SparseVolumeTexture::AttributesBChannelB},
					{TEXT("AttributesB.A"), SparseVolumeTexture::AttributesBChannelA},
				};

				const static TMap<FString, FString> GridChannelToComponentIndex{
					{TEXT("X"), TEXT("0")},
					{TEXT("Y"), TEXT("1")},
					{TEXT("Z"), TEXT("2")},
					{TEXT("W"), TEXT("3")},
					{TEXT("R"), TEXT("0")},
					{TEXT("G"), TEXT("1")},
					{TEXT("B"), TEXT("2")},
					{TEXT("A"), TEXT("3")},
				};

				const FString* FoundAttributeKey = AttributeChannelToAttributeKey.Find(AttributeChannel);
				if (!FoundAttributeKey)
				{
					UE_LOG(LogUsd, Warning, TEXT("Failing to parse unreal:SVT:mappedAttributeChannels value '%s'"), *AttributeChannel);
					continue;
				}

				const FString* FoundComponentIndex = GridChannelToComponentIndex.Find(GridChannel);
				if (!FoundComponentIndex)
				{
					UE_LOG(LogUsd, Warning, TEXT("Failing to parse unreal:SVT:mappedGridComponents value '%s'"), *GridChannel);
					continue;
				}

				VolumeNode->AddStringAttribute(
					*FoundAttributeKey,
					GridName + UE::Interchange::Volume::GridNameAndComponentIndexSeparator + *FoundComponentIndex
				);
			}
		}

		// Convert texture format
		static_assert((int)UsdUtils::ESparseVolumeAttributesFormat::Unorm8 == (int)EInterchangeSparseVolumeTextureFormat::Unorm8);
		static_assert((int)UsdUtils::ESparseVolumeAttributesFormat::Float16 == (int)EInterchangeSparseVolumeTextureFormat::Float16);
		static_assert((int)UsdUtils::ESparseVolumeAttributesFormat::Float32 == (int)EInterchangeSparseVolumeTextureFormat::Float32);
		if (VolumePrimInfo.AttributesAFormat.IsSet())
		{
			VolumeNode->AddInt32Attribute(SparseVolumeTexture::AttributesAFormat, static_cast<int32>(VolumePrimInfo.AttributesAFormat.GetValue()));
		}
		if (VolumePrimInfo.AttributesBFormat.IsSet())
		{
			VolumeNode->AddInt32Attribute(SparseVolumeTexture::AttributesBFormat, static_cast<int32>(VolumePrimInfo.AttributesBFormat.GetValue()));
		}
	}

	UInterchangeMaterialInstanceNode* CreateVolumetricMaterialInstanceNode(
		const UE::FUsdPrim& VolumePrim,
		UInterchangeBaseNodeContainer& NodeContainer,
		const UsdToUnreal::FUsdMeshConversionOptions& ConversionOptions
	)
	{
		FString ParentMaterialPath;

		// Priority 1: Explicit material assignment on the Volume prim (Unreal materials).
		{
			double TimeCode = UsdUtils::GetDefaultTimeCode();	 // Not relevant as material bindings can't be time sampled
			bool bProvideMaterialIndices = false;
			UsdUtils::FUsdPrimMaterialAssignmentInfo Assignments = UsdUtils::GetPrimMaterialAssignments(
				VolumePrim,
				TimeCode,
				bProvideMaterialIndices,
				UnrealIdentifiers::Unreal,	  // Unreal materials should always be on the unreal render context
				ConversionOptions.MaterialPurpose
			);

			for (const UsdUtils::FUsdPrimMaterialSlot& Slot : Assignments.Slots)
			{
				if (Slot.AssignmentType == UsdUtils::EPrimAssignmentType::UnrealMaterial)
				{
					ParentMaterialPath = Slot.MaterialSource;
					break;
				}
			}
		}

		// Priority 2: The USD default volumetric material on the UsdProjectSettings.
		bool bIsFallback = false;
		if (ParentMaterialPath.IsEmpty())
		{
			if (const UUsdProjectSettings* ProjectSettings = GetDefault<UUsdProjectSettings>())
			{
				bIsFallback = true;
				ParentMaterialPath = ProjectSettings->ReferenceDefaultSVTMaterial.ToString();
			}
		}

		// Priority 3: Hard-coded fallback volumetric material that ships with the engine.
		if (ParentMaterialPath.IsEmpty())
		{
			bIsFallback = true;
			ParentMaterialPath = TEXT("/Engine/EngineMaterials/SparseVolumeMaterial.SparseVolumeMaterial");
		}

		FString MaterialDisplayLabel = bIsFallback ? TEXT("USDVolumetricFallbackMaterial") : FPaths::GetBaseFilename(ParentMaterialPath);
		FString MaterialNodeUid = MakeNodeUid(MaterialPrefix + MaterialDisplayLabel);
		MakeNodeUidUniqueInContainer(MaterialNodeUid, NodeContainer);

		// We'll always spawn a new material instance for each volume prim. Realistically a stage is only going to have a handful
		// of volumes at most, and material instances should be pretty cheap. This should be a more predictable result for the user,
		// and it prevents us from needing some bespoke code to reuse these material instance nodes depending on their volume assignment
		UInterchangeMaterialInstanceNode* MaterialInstance = NewObject<UInterchangeMaterialInstanceNode>(&NodeContainer);
		NodeContainer.SetupNode(MaterialInstance, MaterialNodeUid, MaterialDisplayLabel, EInterchangeNodeContainerType::TranslatedAsset);
		MaterialInstance->SetCustomParent(ParentMaterialPath);

		return MaterialInstance;
	}

	TArray<FString> UInterchangeUSDTranslatorImpl::AddVolumeNodes(
		const UE::FUsdPrim& InPrim,
		UInterchangeUsdTranslatorSettings* TranslatorSettings,
		UInterchangeBaseNodeContainer& InNodeContainer,
		FString& OutMaterialInstanceUid,
		bool& bOutNeedsFrameTrack
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeUSDTranslatorImpl::AddVolumeNodes)

		using namespace UE::Interchange::USD;

		// Each Volume prim can reference multiple FieldAsset prims. Each FieldAsset itself can point to a particular grid within
		// a .vdb file... USD is probably too flexible here, allowing us to reference grids from separate .vdb files in the same Volume prim,
		// or letting us refer to the same grid more than once, etc.
		//
		// Our end goal is to make each .vdb file into a single SparseVolumeTexture, combining all the grids that need to be read from it.
		// We'll do our best to satisfy all the requirements, but emit some warnings if we fail on an edge case. Then we want to spawn a single
		// HeterogeneousVolumeActor for each Volume prim, generate an instance of the right material, and assign all these generated SVTs to it.
		//
		// In Interchange terms, this means that we'll emit a single InterchangeVolumeNode for each .vdb file, but will
		// emit a new UInterchangeVolumeGridNode for each grid reference within that file. Note that USD's flexibility means we may have
		// separate Volume prims all referencing the same shared FieldAsset prim, so we need to presume a InterchangeVolumeNode for this .vdb
		// file has potentially already been created when parsing another Volume prim...
		//
		// Finally, since we may use the same .vdb file in multiple animations, and we want to end up with separate animated SVTs, we need
		// separate factory nodes. If we want to keep the expected mapping of factory node / volume node Uids (just an added "Factory_" prefix)
		// this means we need a separate volume node *per animation*, so we'll also use "AnimationIDs" to differentiate them

		TStrongObjectPtr<UInterchangeTranslatorBase> Translator = nullptr;

		// This is collected by path hash here because for animated SVTs we want to still have a single UsdUtils::FVolumePrimInfo for each
		// group of animated volume frames, since they will become a separate SVT. If we just collected them by filepath we could run into
		// trouble if we had a volume prim with 3 frames starting at "file.vdb", and a separate volume prim that just wants one frame, "frame.vdb"
		TMap<FString, UsdUtils::FVolumePrimInfo> VolumeInfoByFilePathHash = UsdUtils::GetVolumeInfoByFilePathHash(InPrim);

		TSet<FString> VolumeAssetNodeUids;
		TMap<FString, FString> VolumeFieldNameToNodeUids;

		bOutNeedsFrameTrack = false;

		// Stash these as we may need this info later when retrieving animation pipelines
		TArray<UsdUtils::FVolumePrimInfo>& CollectedInfoForPrim = PrimPathToVolumeInfo.FindOrAdd(InPrim.GetPrimPath().GetString());

		for (const TPair<FString, UsdUtils::FVolumePrimInfo>& Pair : VolumeInfoByFilePathHash)
		{
			const FString& AnimationID = Pair.Key;
			const UsdUtils::FVolumePrimInfo& VolumePrimInfo = Pair.Value;
			CollectedInfoForPrim.Add(VolumePrimInfo);

			TArray<FString> VDBFilePaths;
			VDBFilePaths.Reserve(VolumePrimInfo.TimeSamplePathIndices.Num() + 1);

			// In case we have both timeSamples and different default opinion, add the default opinion as the first frame so that's what it shows
			// on the level. The LevelSequence Frame track will factor this in, and have the LevelSequence only go through the TimeSamplePaths frames
			// though.
			bool bInsertedDefaultOpinion = false;
			if (VolumePrimInfo.TimeSamplePaths.Num() > 0 && VolumePrimInfo.TimeSamplePaths[0] != VolumePrimInfo.SourceVDBFilePath)
			{
				VDBFilePaths.Add(VolumePrimInfo.SourceVDBFilePath);
				bInsertedDefaultOpinion = true;
			}
			// No time samples at all
			else if (VolumePrimInfo.TimeSamplePaths.Num() == 0)
			{
				VDBFilePaths.Add(VolumePrimInfo.SourceVDBFilePath);
			}

			// Add the file paths going through TimeSamplePathIndices because it's possible that GetVolumeInfoByFilePathHash
			// deduplicated volume frames already. It's fine to add duplicate entries to VDBFilePaths though, because we'll check
			// for an existing volume node for that path every time anyway
			for (int32 PathIndex : VolumePrimInfo.TimeSamplePathIndices)
			{
				VDBFilePaths.Add(VolumePrimInfo.TimeSamplePaths[PathIndex]);
			}

			if (VolumePrimInfo.TimeSamplePathTimeCodes.Num() > 0)
			{
				bOutNeedsFrameTrack = true;
			}

			// First volume is special as that is what will "become" the animated factory node if we have animation. We'll also only stash
			// our custom attributes on this first volume node. The filepaths are sorted according to timeSamples, so this is always the first
			// frame of animation, or the default opinion if we have that
			UInterchangeVolumeNode* FirstVolumeNode = nullptr;

			TSet<UInterchangeVolumeNode*> ResetVolumeNodes;
			ResetVolumeNodes.Reserve(VDBFilePaths.Num());

			for (int32 Index = 0; Index < VDBFilePaths.Num(); ++Index)
			{
				const FString& FilePath = VDBFilePaths[Index];

				TMap<FString, UInterchangeVolumeNode*>& AnimationIDToVolumeNode = VolumeFilepathToAnimationIDToNode.FindOrAdd(FilePath);
				UInterchangeVolumeNode* VolumeNode = AnimationIDToVolumeNode.FindRef(AnimationID);

				// Need to translate the volume for this animation ID.
				// Note: It may seem wasteful to translate the same volume more than once in case it is used by multiple animations,
				// but keep in mind that:
				//  - Multiple animations for the same volume frame in the same import will realistically never happen in practice;
				//  - The VDB translator will cache the read file bytes from the first translation;
				//  - "Translating" the volume just involves returning some header information, which should be pretty fast;
				//  - Doing this saves us from having to manually duplicate other volume and grid nodes, and manually patching up
				//    their unique IDs and / or making some sort of mistake;
				if (!VolumeNode)
				{
					UInterchangeSourceData* SourceData = UInterchangeManager::CreateSourceData(FilePath);

					if (!Translator || !Translator->CanImportSourceData(SourceData))
					{
						// Pass a USD context object to the translator, which is a signal that lets the OpenVDB translator
						// be considered, even if its cvar is off (c.f. UInterchangeOpenVDBTranslator::CanImportSourceData)
						UInterchangeUsdContext* Context = NewObject<UInterchangeUsdContext>();
						SourceData->SetContextObjectByTag(UE::Interchange::USD::USDContextTag, Context);

						UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();
						Translator.Reset(InterchangeManager.GetTranslatorForSourceData(SourceData));
					}
					if (!Translator)
					{
						const bool bIncludeDot = false;
						const FString Extension = FPaths::GetExtension(FilePath, bIncludeDot).ToLower();
						const bool bIsOpenVDB = Extension == TEXT("vdb");

						UE_LOG(
							LogUsd,
							Error,
							TEXT("Failed to find a compatible translator for file '%s'.%s"),
							*FilePath,
							bIsOpenVDB ? TEXT(" Is the 'Interchange OpenVDB' plugin enabled?") : TEXT("")
						);
						continue;
					}

					if (UInterchangeVolumeTranslatorSettings* Settings = Cast<UInterchangeVolumeTranslatorSettings>(Translator->GetSettings()))
					{
						// We never want to discover new .vdb files via the OpenVDB translator for animations.
						// If we should have animations via USD they will be described on the USD file explicictly
						Settings->bTranslateAdjacentNumberedFiles = false;

						// If the volume prim describes an animation, let's add the same AnimationID to the volume nodes that the translator
						// will output, so that the SVT pipeline groups them up into a single factory node
						Settings->AnimationID = VolumePrimInfo.TimeSamplePathTimeCodes.Num() > 0 ? AnimationID : FString{};
					}

					Translator->SourceData = SourceData;
					Translator->Translate(InNodeContainer);

					// Cache that we used this translator for this filepath. If we keep it, we don't have to open the file again to
					// retrieve the payload. Note: This is likely the same translator we used for all .vdb files during this import
					Translators.Add(FilePath, Translator);

					// Note the slight exploit: We can get non-const access to the translated nodes in this way, which we need
					InNodeContainer.BreakableIterateNodesOfType<UInterchangeVolumeNode>(
						[&InNodeContainer, &VolumeNode, &FilePath](const FString& NodeUid, UInterchangeVolumeNode* Node)
						{
							if (NodeUid.Contains(FilePath))
							{
								VolumeNode = Node;
								return true;
							}

							return false;
						}
					);

					if (VolumeNode)
					{						
						// The USD shader graph, in case of a volume material, reference the OpenVDBAsset as a primvar
						// In the MaterialX Translator we create a SparseVolumeTextureSampleParameter that references this 'primvar' name, the SparseVolumeTexture asset
						VolumeNode->AddInt32Attribute(Primvar::Number, VolumePrimInfo.VolumeFieldNames.Num());
						for (int32 IndexPrimvar = 0; IndexPrimvar < VolumePrimInfo.VolumeFieldNames.Num(); ++IndexPrimvar)
						{
							VolumeNode->AddStringAttribute(Primvar::ShaderNodeSparseVolumeTextureSample + FString::FromInt(IndexPrimvar), VolumePrimInfo.VolumeFieldNames[IndexPrimvar]);
						}
						AnimationIDToVolumeNode.Add(AnimationID, VolumeNode);
					}
				}

				if (!VolumeNode)
				{
					UE_LOG(LogUsd, Warning, TEXT("Failed to produce a volume node from file '%s'"), *FilePath);
					continue;
				}

				// Remove any animation index that the OpenVDB translator (or a translation of another instance of this whole volume prim in the
				// stage) may have set on this volume node, as we want to set the animation indices exclusively during this pass through VDBFilePaths
				if (!ResetVolumeNodes.Contains(VolumeNode))
				{
					ResetVolumeNodes.Add(VolumeNode);

					TArray<int32> ExistingAnimationIndices;
					VolumeNode->GetCustomFrameIndicesInAnimation(ExistingAnimationIndices);
					for (const int32 ExistingIndex : ExistingAnimationIndices)
					{
						VolumeNode->RemoveCustomFrameIndexInAnimation(ExistingIndex);
					}
				}
				VolumeNode->AddCustomFrameIndexInAnimation(Index);

				if (!FirstVolumeNode)
				{
					FirstVolumeNode = VolumeNode;
				}

				VolumeAssetNodeUids.Add(VolumeNode->GetUniqueID());
			}

			if (FirstVolumeNode)
			{
				// Collect all of our custom-schema-based assignment info to be used as as custom attributes on the first volume node.
				// The USD Pipeline will handle these, and move them into the factory nodes.
				AddVolumeCustomAttributesToNode(VolumePrimInfo, FirstVolumeNode);

				for (const FString& FieldName : VolumePrimInfo.VolumeFieldNames)
				{
					VolumeFieldNameToNodeUids.Add(FieldName, FirstVolumeNode->GetUniqueID());
				}
			}
		}

		// In case of MaterialX context, the volume material will be created from a shader graph
		if (TranslatorSettings && (TranslatorSettings->RenderContext == UnrealIdentifiers::MaterialXRenderContext))
		{
			UsdUtils::FUsdPrimMaterialAssignmentInfo Assignments = 
				UsdUtils::GetPrimMaterialAssignments(InPrim, UsdUtils::GetDefaultTimeCode(), false, CachedMeshConversionOptions.RenderContext, CachedMeshConversionOptions.MaterialPurpose);

			for (const UsdUtils::FUsdPrimMaterialSlot& Slot : Assignments.Slots)
			{
				FString SlotName = Slot.MaterialSource;

				if (Slot.AssignmentType != UsdUtils::EPrimAssignmentType::MaterialPrim)
				{
					continue;
				}

				// MaterialSource here is the material prim path
				UE::FUsdPrim MaterialPrim = UsdStage.GetPrimAtPath(UE::FSdfPath{ *Slot.MaterialSource });

				// Go through a prototype in case this material is inside an instance proxy, as this needs to match up with
				// the polygon group name we're going to add to our mesh description.
				UE::FUsdPrim MaterialPrototypePrim = UsdUtils::GetPrototypePrim(MaterialPrim);
				if (MaterialPrototypePrim != MaterialPrim)
				{
					SlotName = MaterialPrototypePrim.GetPrimPath().GetString();
				}

				OutMaterialInstanceUid = AddMaterialNode(MaterialPrototypePrim, TranslatorSettings, InNodeContainer, Slot.bMeshIsDoubleSided);

				if (!OutMaterialInstanceUid.IsEmpty())
				{
					break;
				}
			}

			if (!OutMaterialInstanceUid.IsEmpty())
			{
				return VolumeAssetNodeUids.Array();
			}
		}
		// Setup a new material instance node for this volume, which will be used by the Heterogeneous Volume Actor we'll also
		// spawn for this volume prim.
		//
		// Note that there's only one material slot per actor, but that we really do need some kind of material to be put in there,
		// so that we can at least assign our SVTs somewhere.
		UInterchangeMaterialInstanceNode* MaterialInstance = CreateVolumetricMaterialInstanceNode(
			InPrim,
			InNodeContainer,
			CachedMeshConversionOptions
		);

		// Assign SVTs as material parameters on our new material instance
		if (ensure(MaterialInstance))
		{
			OutMaterialInstanceUid = MaterialInstance->GetUniqueID();

			TMultiMap<FString, FString> MaterialParameterToFieldName = UsdUtils::GetVolumeMaterialParameterToFieldNameMap(InPrim);

			// Prim doesn't have the attributes specifying an explicit material parameter name to volume mapping (this is probably
			// the more common case)
			if (MaterialParameterToFieldName.Num() == 0)
			{
				// Consider that the field names may match material parameter names. We can't tell if that's the case or not from here,
				// and this may cause us to assign a VolumeUid more than once (as multiple fields may target the same SVT), but the
				// USDPipeline will clean this up
				for (const TPair<FString, FString>& Pair : VolumeFieldNameToNodeUids)
				{
					const FString& FieldName = Pair.Key;
					const FString& VolumeUid = Pair.Value;

					MaterialInstance->AddTextureParameterValue(UE::Interchange::USD::VolumeFieldNameMaterialParameterPrefix + FieldName, VolumeUid);
				}
			}
			// Prim has custom attributes specifying exactly which volume should be assigned to which material parameter
			else
			{
				TMap<FString, FString> ParameterNameToVolume;
				ParameterNameToVolume.Reserve(MaterialParameterToFieldName.Num());

				for (const TPair<FString, FString>& Pair : MaterialParameterToFieldName)
				{
					const FString& ParameterName = Pair.Key;
					const FString& FieldName = Pair.Value;

					const FString* FoundVolumeUid = VolumeFieldNameToNodeUids.Find(FieldName);
					if (!FoundVolumeUid)
					{
						continue;
					}

					// Show a warning in case we have conflicting assignments, because the legacy schema translator also did it
					if (FString* FoundAssignedVolume = ParameterNameToVolume.Find(ParameterName))
					{
						if (*FoundAssignedVolume != *FoundVolumeUid)
							UE_LOG(
								LogUsd,
								Warning,
								TEXT(
									"Trying to assign different Sparse Volume Textures to the same material parameter '%s' on the material instantiated for Volume '%s' and field name '%s'! Only a single texture can be assigned to a material parameter at a time."
								),
								*ParameterName,
								**FoundVolumeUid,
								*FieldName
							);
						continue;
					}
					ParameterNameToVolume.Add(ParameterName, *FoundVolumeUid);

					MaterialInstance->AddTextureParameterValue(ParameterName, *FoundVolumeUid);
				}
			}
		}

		return VolumeAssetNodeUids.Array();
	}

	TOptional<FString> UInterchangeUSDTranslatorImpl::AddGroomNode(
		const UE::FUsdPrim& Prim,
		UInterchangeBaseNodeContainer& NodeContainer
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeUSDTranslatorImpl::AddGroomNode)

		const FString PrimPath = UsdUtils::GetPrototypePrimPath(Prim).GetString();
		const FString NodeUid = MakeNodeUid(GroomPrefix + PrimPath);
		const FString NodeName(Prim.GetName().ToString());

		// Check if Node already exist with this ID
		if (const UInterchangeGroomNode* Node = Cast<UInterchangeGroomNode>(NodeContainer.GetNode(NodeUid)))
		{
			return NodeUid;
		}

		// Check if there's any actual groom data
		FGroomAnimationInfo AnimInfo;
		FHairDescription HairDescription;
		if (!UsdToUnreal::ConvertGroomHierarchy(Prim, UsdUtils::GetEarliestTimeCode(), FTransform::Identity, HairDescription, &AnimInfo))
		{
			return {};
		}

		const double StageTimeCodesPerSecond = Prim.GetStage().GetTimeCodesPerSecond();
		AnimInfo.SecondsPerFrame = static_cast<float>(1 / StageTimeCodesPerSecond);

		const bool bIsValidGroomCache = AnimInfo.IsValid();

		UInterchangeGroomNode* GroomNode = NewObject<UInterchangeGroomNode>(&NodeContainer);
		NodeContainer.SetupNode(GroomNode, NodeUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset);
		GroomNode->SetAssetName(NodeName);
		GroomNode->SetPayloadKey(PrimPath, bIsValidGroomCache ? EInterchangeGroomPayLoadType::ANIMATED : EInterchangeGroomPayLoadType::STATIC);

		if (bIsValidGroomCache)
		{
			GroomNode->SetCustomNumFrames(AnimInfo.NumFrames);
			GroomNode->SetCustomStartFrame(AnimInfo.StartFrame);
			GroomNode->SetCustomEndFrame(AnimInfo.EndFrame);
			GroomNode->SetCustomFrameRate(StageTimeCodesPerSecond);

			static_assert((int)EGroomCacheAttributes::None == (int)EInterchangeGroomCacheAttributes::None);
			static_assert((int)EGroomCacheAttributes::Position == (int)EInterchangeGroomCacheAttributes::Position);
			static_assert((int)EGroomCacheAttributes::Width == (int)EInterchangeGroomCacheAttributes::Width);
			static_assert((int)EGroomCacheAttributes::Color == (int)EInterchangeGroomCacheAttributes::Color);

			GroomNode->SetCustomGroomCacheAttributes(static_cast<EInterchangeGroomCacheAttributes>(AnimInfo.Attributes));
		}

		return NodeUid;
	}

	FString UInterchangeUSDTranslatorImpl::AddSpatialAudioNode(
		const UE::FUsdPrim& Prim,
		UInterchangeBaseNodeContainer& NodeContainer
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeUSDTranslatorImpl::AddSpatialAudioNode)

		const FString PrimPath = UsdUtils::GetPrototypePrimPath(Prim).GetString();
		const FString NodeUid = MakeNodeUid(SpatialAudioPrefix + PrimPath);
		const FString NodeName(Prim.GetName().ToString());

		// Check if Node already exist with this ID
		if (const UInterchangeAudioSoundWaveNode* Node = Cast<UInterchangeAudioSoundWaveNode>(NodeContainer.GetNode(NodeUid)))
		{
			return NodeUid;
		}

		UInterchangeAudioSoundWaveNode* SoundWaveNode = NewObject<UInterchangeAudioSoundWaveNode>(&NodeContainer);
		NodeContainer.SetupNode(SoundWaveNode, NodeUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset);
		SoundWaveNode->SetAssetName(NodeName);
		
		static const FString FilePathToken = UsdToUnreal::ConvertToken(pxr::UsdMediaTokens->filePath);
		UE::FUsdAttribute FilePathAttribute = Prim.GetAttribute(*FilePathToken);
		const FString ResolvedAudioFilepath = UsdUtils::GetResolvedAssetPath(FilePathAttribute);
		SoundWaveNode->SetPayloadKey(ResolvedAudioFilepath);

		// TODO: @vjavdekar - Get all the attributes from the media spatial audio
		// and add those to SoundWaveNode.

		return NodeUid;
	}

	void AddTrackSetNode(UInterchangeUSDTranslatorImpl& Impl, UInterchangeBaseNodeContainer& NodeContainer)
	{
		// For now we only want a single track set (i.e. LevelSequence) per stage.
		// TODO: One track set per layer, and add the tracks to the tracksets that correspond to layers where the opinions came from
		// (similar to LevelSequenceHelper). Then we can use UInterchangeAnimationTrackSetInstanceNode to create "subsequences"
		if (Impl.CurrentTrackSet)
		{
			return;
		}

		UE::FSdfLayer Layer = Impl.UsdStage.GetRootLayer();
		const FString AnimTrackSetNodeUid = MakeNodeUid(AnimationPrefix + Layer.GetIdentifier());
		const FString AnimTrackSetNodeDisplayName = FPaths::GetBaseFilename(Layer.GetDisplayName());	// Strip extension

		// We should only have one track set node per scene for now
		const UInterchangeAnimationTrackSetNode* ExistingNode = Cast<UInterchangeAnimationTrackSetNode>(NodeContainer.GetNode(AnimTrackSetNodeUid));
		if (!ensure(ExistingNode == nullptr))
		{
			return;
		};

		UInterchangeAnimationTrackSetNode* TrackSetNode = NewObject<UInterchangeAnimationTrackSetNode>(&NodeContainer);
		NodeContainer.SetupNode(TrackSetNode, AnimTrackSetNodeUid, AnimTrackSetNodeDisplayName, EInterchangeNodeContainerType::TranslatedAsset);
		
		// This ends up as the LevelSequence frame rate, so it should probably match the stage's frame rate like legacy USD does
		TrackSetNode->SetCustomFrameRate(Layer.GetFramesPerSecond());

		Impl.CurrentTrackSet = TrackSetNode;
	}

	void AddTransformAnimationNode(const UE::FUsdPrim& Prim, UInterchangeUSDTranslatorImpl& Impl, UInterchangeBaseNodeContainer& NodeContainer)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AddTransformAnimationNode)

		const FString PrimPath = Prim.GetPrimPath().GetString();
		const FString UniquePath = PrimPath + TEXT("\\") + UnrealIdentifiers::TransformPropertyName.ToString();
		const FString AnimTrackNodeUid = MakeNodeUid(AnimationTrackPrefix + UniquePath);
		const FString SceneNodeUid = MakeNodeUid(PrimPath);

		const UInterchangeTransformAnimationTrackNode* ExistingNode = Cast<UInterchangeTransformAnimationTrackNode>(
			NodeContainer.GetNode(AnimTrackNodeUid)
		);
		if (ExistingNode)
		{
			return;
		}

		UInterchangeTransformAnimationTrackNode* TransformAnimTrackNode = NewObject<UInterchangeTransformAnimationTrackNode>(&NodeContainer);
		NodeContainer.SetupNode(TransformAnimTrackNode, AnimTrackNodeUid, UniquePath, EInterchangeNodeContainerType::TranslatedAsset);
		TransformAnimTrackNode->SetCustomActorDependencyUid(SceneNodeUid);
		TransformAnimTrackNode->SetCustomAnimationPayloadKey(UniquePath, EInterchangeAnimationPayLoadType::CURVE);
		TransformAnimTrackNode->SetCustomUsedChannels((int32)EMovieSceneTransformChannel::AllTransform);

		AddTrackSetNode(Impl, NodeContainer);
		Impl.CurrentTrackSet->AddCustomAnimationTrackUid(AnimTrackNodeUid);
	}

	void AddPropertyAnimationNode(
		const FString& PrimPath,
		const FString& UEPropertyName,
		EInterchangePropertyTracks TrackType,
		EInterchangeAnimationPayLoadType PayloadType,
		UInterchangeUSDTranslatorImpl& Impl,
		UInterchangeBaseNodeContainer& NodeContainer
	)
	{
		// We don't use the USD attribute path here because we want one unique node per UE track name,
		// so that if e.g. both "intensity" and "exposure" are animated we make a single track for
		// the Intensity UE property
		const FString PayloadKey = PrimPath + TEXT("\\") + UEPropertyName;
		const FString SceneNodeUid = MakeNodeUid(PrimPath);
		const FString AnimTrackNodeUid = MakeNodeUid(AnimationTrackPrefix + PayloadKey);

		const UInterchangeAnimationTrackNode* ExistingNode = Cast<UInterchangeAnimationTrackNode>(NodeContainer.GetNode(AnimTrackNodeUid));
		if (ExistingNode)
		{
			return;
		}

		UInterchangeAnimationTrackNode* AnimTrackNode = NewObject<UInterchangeAnimationTrackNode>(&NodeContainer);
		NodeContainer.SetupNode(AnimTrackNode, AnimTrackNodeUid, PayloadKey, EInterchangeNodeContainerType::TranslatedAsset);
		AnimTrackNode->SetCustomActorDependencyUid(*SceneNodeUid);
		AnimTrackNode->SetCustomPropertyTrack(TrackType);
		AnimTrackNode->SetCustomAnimationPayloadKey(PayloadKey, PayloadType);

		AddTrackSetNode(Impl, NodeContainer);
		Impl.CurrentTrackSet->AddCustomAnimationTrackUid(AnimTrackNodeUid);
	}

	void AddPropertyAnimationNodes(const UE::FUsdPrim& Prim, UInterchangeUSDTranslatorImpl& Impl, UInterchangeBaseNodeContainer& NodeContainer)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AddPropertyAnimationNodes)

		using namespace UE::InterchangeUsdTranslator::Private;

		if (!Prim)
		{
			return;
		}
		const FString PrimPath = Prim.GetPrimPath().GetString();

		for (UE::FUsdAttribute Attr : Prim.GetAttributes())
		{
			if (!Attr || !Attr.ValueMightBeTimeVarying() || Attr.GetNumTimeSamples() == 0)
			{
				continue;
			}

			// Emit a STEPCURVE in case of a bool track: CURVE is only for floats/doubles (c.f. FLevelSequenceHelper::PopulateAnimationTrack).
			// For now we're lucky in that all possible results from GetPropertiesForAttribute() are either all not bool, or either all bool,
			// so we can reuse this for all the different UEAttrNames we get from the same attribute
			const FName AttrTypeName = Attr.GetTypeName();
			const bool bIsBoolTrack = AttrTypeName == TEXT("bool") || AttrTypeName == TEXT("token");	// Visibility is a token track

			TArray<FName> UEPropertyNames = UsdUtils::GetPropertiesForAttribute(Prim, Attr.GetName().ToString());
			for (const FName& UEPropertyName : UEPropertyNames)
			{
				const EInterchangePropertyTracks* FoundTrackType = PropertyNameToTrackType.Find(UEPropertyName);
				if (!FoundTrackType)
				{
					continue;
				}

				const EInterchangeAnimationPayLoadType PayloadType = bIsBoolTrack ? EInterchangeAnimationPayLoadType::STEPCURVE
																				  : EInterchangeAnimationPayLoadType::CURVE;

				AddPropertyAnimationNode(PrimPath, UEPropertyName.ToString(), *FoundTrackType, PayloadType, Impl, NodeContainer);
			}
		}
	}

	// Some of the volume prim info is meant for the HeterogeneousVolume (HV) actor and the volumetric material,
	// so we need to add it to the scene node (it's possible separate HV actors with different values for these
	// end up sharing identical volume nodes)
	void AddVolumeSceneNodeAttributes(
		const UE::FUsdPrim& Prim,
		UInterchangeSceneNode* SceneNode,
		const TArray<FString>& AssetNodeUids,
		const FString& VolumeMaterialInstanceUid,
		bool bNeedsAnimationTrack,
		UInterchangeUSDTranslatorImpl& Impl,
		UInterchangeBaseNodeContainer& NodeContainer
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AddVolumeSceneNodeAttributes)

		if (!SceneNode)
		{
			return;
		}
		const FString& PrimPath = Prim.GetPrimPath().GetString();

		// Target the scene node from all the volume nodes to make it easy to find everything we need on the USD Pipeline
		for (const FString& AssetNodeUid : AssetNodeUids)
		{
			if (const UInterchangeVolumeNode* VolumeNode = Cast<const UInterchangeVolumeNode>(NodeContainer.GetNode(AssetNodeUid)))
			{
				SceneNode->AddTargetNodeUid(AssetNodeUid);
			}
		}

		// Set our volumetric material as a "material override" directly on the scene node, which the USD Pipeline will also use
		if (!VolumeMaterialInstanceUid.IsEmpty())
		{
			SceneNode->SetSlotMaterialDependencyUid(UE::Interchange::Volume::VolumetricMaterial, VolumeMaterialInstanceUid);
		}

		if (bNeedsAnimationTrack)
		{
			// Ideally we'd write some step curves, but Interchange doesn't support float step curves
			const EInterchangeAnimationPayLoadType PayloadType = EInterchangeAnimationPayLoadType::CURVE;
			const EInterchangePropertyTracks TrackType = EInterchangePropertyTracks::HeterogeneousVolumeFrame;
			const static FName UEPropertyName = GET_MEMBER_NAME_CHECKED(UHeterogeneousVolumeComponent, Frame);

			AddPropertyAnimationNode(PrimPath, UEPropertyName.ToString(), TrackType, PayloadType, Impl, NodeContainer);
		}
	}

	const UInterchangeSkeletalAnimationTrackNode* AddSkeletalAnimationNode(
		const UE::FUsdSkelSkeletonQuery& SkeletonQuery,
		const TMap<FString, TPair<FString, int32>>& UsdBoneToUidAndUsdBoneIndex,
		UInterchangeUSDTranslatorImpl& TranslatorImpl,
		UInterchangeSceneNode& RootJointSceneNode,
		UInterchangeBaseNodeContainer& NodeContainer,
		const FTraversalInfo& Info
	)
	{
		UE::FUsdSkelAnimQuery AnimQuery = SkeletonQuery.GetAnimQuery();
		if (!AnimQuery)
		{
			return nullptr;
		}

		UE::FUsdPrim SkelAnimationPrim = AnimQuery.GetPrim();
		if (!SkelAnimationPrim)
		{
			return nullptr;
		}

		UE::FUsdPrim SkeletonPrim = SkeletonQuery.GetSkeleton();
		if (!SkeletonPrim)
		{
			return nullptr;
		}

		UE::FUsdStage Stage = SkeletonPrim.GetStage();

		const FString SkelAnimationName = SkelAnimationPrim.GetName().ToString();
		const FString SkelAnimationPrimPath = UsdUtils::GetPrototypePrimPath(SkelAnimationPrim).GetString();
		const FString SkeletonPrimPath = SkeletonPrim.GetPrimPath().GetString();
		const FString UniquePath = SkelAnimationPrimPath + TEXT("\\") + SkeletonPrimPath;
		const FString NodeUid = MakeNodeUid(AnimationTrackPrefix + UniquePath);

		const UInterchangeSkeletalAnimationTrackNode* ExistingNode = Cast<UInterchangeSkeletalAnimationTrackNode>(NodeContainer.GetNode(NodeUid));
		if (ExistingNode)
		{
			return ExistingNode;
		};

		UInterchangeSkeletalAnimationTrackNode* SkelAnimNode = NewObject<UInterchangeSkeletalAnimationTrackNode>(&NodeContainer);
		NodeContainer.SetupNode(SkelAnimNode, NodeUid, SkelAnimationName, EInterchangeNodeContainerType::TranslatedAsset);
		SkelAnimNode->SetCustomSkeletonNodeUid(RootJointSceneNode.GetUniqueID());

		// TODO: Uncomment this whenever Interchange supports skeletal animation sections, because
		// currently it seems that InterchangeLevelSequenceFactory.cpp doesn't even have the string "skel" anywhere.
		// If we were to add this all we'd get is a warning on the output log about "all referenced actors being missing",
		// in case it failed to find anything else (e.g. other actual property/transform track) to put on the LevelSequence.
		// AddTrackSetNode(TranslatorImpl, NodeContainer);
		// TranslatorImpl->CurrentTrackSet->AddCustomAnimationTrackUid(NodeUid);

		// Time info
		{
			const double TimeCodesPerSecond = Stage.GetTimeCodesPerSecond();			
			SkelAnimNode->SetCustomAnimationSampleRate(TimeCodesPerSecond);

			TOptional<double> StartTimeCode;
			TOptional<double> StopTimeCode;

			// For now we don't generate LevelSequences for sublayers and will instead put everything on a single
			// LevelSequence for the entire stage, so we don't need to care so much about sublayer offset/scale like
			// UsdToUnreal::ConvertSkelAnim does
			TArray<double> JointTimeSamples;
			if (AnimQuery.GetJointTransformTimeSamples(JointTimeSamples) && JointTimeSamples.Num() > 0)
			{
				StartTimeCode = JointTimeSamples[0];
				StopTimeCode = JointTimeSamples[JointTimeSamples.Num() - 1];
			}
			TArray<double> BlendShapeTimeSamples;
			if (AnimQuery.GetBlendShapeWeightTimeSamples(BlendShapeTimeSamples) && BlendShapeTimeSamples.Num() > 0)
			{
				StartTimeCode = FMath::Min(BlendShapeTimeSamples[0], StartTimeCode.Get(TNumericLimits<double>::Max()));
				StopTimeCode = FMath::Max(BlendShapeTimeSamples[BlendShapeTimeSamples.Num() - 1], StopTimeCode.Get(TNumericLimits<double>::Lowest()));
			}

			if (StartTimeCode.IsSet())
			{
				SkelAnimNode->SetCustomAnimationStartTime(StartTimeCode.GetValue() / TimeCodesPerSecond);
			}
			if (StopTimeCode.IsSet())
			{
				SkelAnimNode->SetCustomAnimationStopTime(StopTimeCode.GetValue() / TimeCodesPerSecond);
			}
		}

		// Joint animation
		TArray<FString> UsdJointOrder = AnimQuery.GetJointOrder();
		for (const FString& FullAnimatedBoneName : UsdJointOrder)
		{
			const TPair<FString, int32>* FoundPair = UsdBoneToUidAndUsdBoneIndex.Find(FullAnimatedBoneName);
			if (!FoundPair)
			{
				continue;
			}

			const FString& BoneSceneNodeUid = FoundPair->Key;
			int32 UsdSkeletonOrderBoneIndex = FoundPair->Value;

			const FString BoneAnimPayloadKey = SkeletonPrimPath + TEXT("\\") + LexToString(UsdSkeletonOrderBoneIndex);

			// When retrieving the payload later, We'll need that bone's index within the Skeleton prim to index into the
			// InUsdSkeletonQuery.ComputeJointLocalTransforms() results.
			// Note that we're describing joint transforms with baked frames here. It would have been possible to use transform
			// curves, but that may have lead to issues when interpolating problematic joint transforms. Instead, we'll bake
			// using USD, and let it interpolate the transforms however it wants
			SkelAnimNode->SetAnimationPayloadKeyForSceneNodeUid(BoneSceneNodeUid, BoneAnimPayloadKey, EInterchangeAnimationPayLoadType::BAKED);
		}

		// Morph targets
		{
			UE::FUsdSkelBinding SkelBinding;
			const bool bTraverseInstanceProxies = true;
			bool bSuccess = Info.FurthestSkelCache->ComputeSkelBinding(	   //
				Info.ResolveClosestParentSkelRoot(Stage),
				SkeletonPrim,
				SkelBinding,
				bTraverseInstanceProxies
			);
			if (!bSuccess)
			{
				return SkelAnimNode;
			}

			TArray<FString> SkelAnimChannelOrder = AnimQuery.GetBlendShapeOrder();

			TMap<FString, int32> SkelAnimChannelIndices;
			SkelAnimChannelIndices.Reserve(SkelAnimChannelOrder.Num());
			for (int32 ChannelIndex = 0; ChannelIndex < SkelAnimChannelOrder.Num(); ++ChannelIndex)
			{
				const FString& ChannelName = SkelAnimChannelOrder[ChannelIndex];
				SkelAnimChannelIndices.Add(ChannelName, ChannelIndex);
			}

			TArray<UE::FUsdSkelSkinningQuery> SkinningTargets = SkelBinding.GetSkinningTargets();
			for (const UE::FUsdSkelSkinningQuery& SkinningTarget : SkinningTargets)
			{
				// USD lets you "skin" anything that can take the SkelBindingAPI, but we only care about Mesh here as
				// those are the only ones that can have blendshapes
				UE::FUsdPrim Prim = SkinningTarget.GetPrim();
				if (!Prim.IsA(TEXT("Mesh")))
				{
					continue;
				}
				const FString MeshPrimPath = UsdUtils::GetPrototypePrimPath(Prim).GetString();

				TArray<FString> BlendShapeChannels;
				bool bInnerSucces = SkinningTarget.GetBlendShapeOrder(BlendShapeChannels);
				if (!bInnerSucces)
				{
					continue;
				}

				TArray<UE::FSdfPath> Targets;
				{
					UE::FUsdRelationship BlendShapeTargetsRel = SkinningTarget.GetBlendShapeTargetsRel();
					if (!BlendShapeTargetsRel)
					{
						continue;
					}
					bInnerSucces = BlendShapeTargetsRel.GetTargets(Targets);
					if (!bInnerSucces)
					{
						continue;
					}
				}

				if (BlendShapeChannels.Num() != Targets.Num())
				{
					UE_LOG(
						LogUsd,
						Warning,
						TEXT(
							"Skipping morph target curves for animation of skinned mesh '%s' because the number of entries in the 'skel:blendShapes' attribute (%d) doesn't match the number of entries in the 'skel:blendShapeTargets' attribute (%d)"
						),
						*MeshPrimPath,
						BlendShapeChannels.Num(),
						Targets.Num()
					);
					continue;
				}

				for (int32 BlendShapeIndex = 0; BlendShapeIndex < Targets.Num(); ++BlendShapeIndex)
				{
					const FString& ChannelName = BlendShapeChannels[BlendShapeIndex];
					int32* FoundSkelAnimChannelIndex = SkelAnimChannelIndices.Find(ChannelName);
					if (!FoundSkelAnimChannelIndex)
					{
						// This channel is not animated by this SkelAnimation prim
						continue;
					}

					// Note that we put no inbetween name on the MorphTargetUid: We only need to emit the morph target curve payloads
					// for the main shapes: We'll provide the inbetween "positions" when providing the curve and Interchange computes
					// the inbetween curves automatically
					const FString BlendShapePath = Targets[BlendShapeIndex].GetString();
					const FString MorphTargetUid = GetMorphTargetMeshNodeUid(MeshPrimPath, BlendShapeIndex);
					const FString PayloadKey = GetMorphTargetCurvePayloadKey(SkeletonPrimPath, *FoundSkelAnimChannelIndex, BlendShapePath);

					SkelAnimNode->SetAnimationPayloadKeyForMorphTargetNodeUid(	  //
						MorphTargetUid,
						PayloadKey,
						EInterchangeAnimationPayLoadType::MORPHTARGETCURVE
					);
				}
			}
		}

		return SkelAnimNode;
	}

	void UInterchangeUSDTranslatorImpl::AddSkeletonNodes(
		const UE::FUsdPrim& Prim,
		UInterchangeSceneNode& SkeletonPrimNode,
		UInterchangeBaseNodeContainer& NodeContainer,
		FTraversalInfo& Info
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AddSkeletonNodes)

		// If we're not inside of a SkelRoot, the skeleton shouldn't really do anything
		if (!Info.ClosestParentSkelRootPath.IsValid())
		{
			return;
		}

		// By the time we get here we've already emitted a scene node for the skeleton prim itself, so we just
		// need to emit a node hierarchy that mirrors the joints.

		constexpr bool bResetCache = false;
		SkeletonPrimNode.SetCustomBindPoseLocalTransform(&NodeContainer, FTransform::Identity, bResetCache);
		SkeletonPrimNode.SetCustomTimeZeroLocalTransform(&NodeContainer, FTransform::Identity, bResetCache);

		const FString SkeletonPrimPath = Prim.GetPrimPath().GetString();

#if WITH_EDITOR
		// Convert the skeleton bones/joints into ConvertedData
		UE::FUsdSkelSkeletonQuery SkelQuery = Info.FurthestSkelCache->GetSkelQuery(Prim);
		const bool bEnsureAtLeastOneBone = false;
		const bool bEnsureSingleRootBone = false;
		UsdToUnreal::FUsdSkeletonData ConvertedData;
		const bool bSuccess = UsdToUnreal::ConvertSkeleton(SkelQuery, ConvertedData, bEnsureAtLeastOneBone, bEnsureSingleRootBone);
		if (!bSuccess || ConvertedData.Bones.Num() == 0)
		{
			return;
		}

		// If we have multiple root bones, we need to add a single "true" root bone here, and then just add all the
		// input root bones as children. We need to make sure our root bone has a unique name, but once that is done things
		// should work out fine (including offsetting the vertex bone influence indices) because Interchange will compare these
		// bone names with the bone names we return from GetSkeletalMeshPayloadData() and remap the indices by name
		bool bHadMultipleRootBones = false;
		{
			TArray<int32> RootBoneIndices;
			for (int32 BoneIndex = 0; BoneIndex < ConvertedData.Bones.Num(); ++BoneIndex)
			{
				const UsdToUnreal::FUsdSkeletonData::FBone& Bone = ConvertedData.Bones[BoneIndex];
				if (Bone.ParentIndex == INDEX_NONE)
				{
					RootBoneIndices.Add(BoneIndex); 
				}
			}

			if (RootBoneIndices.Num() > 1)
			{
				bHadMultipleRootBones = true;

				UsdToUnreal::FUsdSkeletonData::FBone TrueRoot;
				TrueRoot.Name = UE::Interchange::USD::RootBoneUidSuffix;

				TrueRoot.ChildIndices = RootBoneIndices;
				ConvertedData.Bones.Insert(TrueRoot, 0);

				// This will also patch up our TrueRoot bone, which is great as it fixes
				// up its ChildIndices too
				for (UsdToUnreal::FUsdSkeletonData::FBone& Bone : ConvertedData.Bones)
				{
					Bone.ParentIndex += 1;
					for (int32& ChildIndex : Bone.ChildIndices)
					{
						ChildIndex += 1;
					}
				}

				// We need to fixup the true root's parentindex here though
				ConvertedData.Bones[0].ParentIndex = INDEX_NONE;
			}
		}

		// Maps from the USD-style full bone name (e.g. "shoulder/elbow/hand") to the Uid we used for
		// the corresponding scene node, and the bone's index on the skeleton's joint order.
		// We'll need this to parse skeletal animations, if any
		TMap<FString, TPair<FString, int32>> UsdBoneToUidAndUsdBoneIndex;
		UInterchangeSceneNode* RootBoneSceneNode = nullptr;

		// Recursively traverse ConvertedData spawning the joint translated nodes
		TFunction<void(int32, UInterchangeSceneNode&, const FString&)> RecursiveTraverseBones = nullptr;
		RecursiveTraverseBones = [&RecursiveTraverseBones,
								  &UsdBoneToUidAndUsdBoneIndex,
								  &SkeletonPrimPath,
								  &ConvertedData,
								  &NodeContainer,
								  &RootBoneSceneNode,
								  bHadMultipleRootBones
		](int32 BoneIndex, UInterchangeSceneNode& ParentNode, const FString& UsdBonePath)
		{
			const UsdToUnreal::FUsdSkeletonData::FBone& Bone = ConvertedData.Bones[BoneIndex];

			const bool bIsRootBone = BoneIndex == 0;

			// Name of the individual bone, as-is from USD. Our "true root" becomes just "" here, as it doesn't exist in USD
			const FString UsdBoneName = (bIsRootBone && bHadMultipleRootBones) ? TEXT("") : Bone.Name;

			// Concatenate a full "bone path" here for uniqueness
			// This now matches USD bone paths (e.g. "shoulder/elbow/hand")
			const FString ConcatUsdBonePath = UsdBonePath.IsEmpty() ? UsdBoneName : UsdBonePath + TEXT("/") + UsdBoneName;

			// Append our "true root" bone if we have one (e.g. shoulder/elbow/hand -> Root/shoulder/elbow/hand)
			const FString FullConcatUsdBonePath = (bHadMultipleRootBones)
				? RootBoneUidSuffix + (bIsRootBone ? TEXT("") : TEXT("/") + ConcatUsdBonePath)
				: ConcatUsdBonePath;

			// The UID for a root bone (whether "true root" or not) is always \Bone\<SkeletonPrimPath>/RootBoneUidSuffix (e.g. "\Bone\<SkeletonPrimPath>/Root")
			// given our call to MakeRootBoneNodeUid().
			// For any other bone, it becomes \Bone\<SkeletonPrimPath>/FullConcat (e.g. "\Bone\<SkeletonPrimPath>/<UsdRootBoneName>/Elbow/Hand")
			//
			// Note that this may look slightly odd for non-multiple-root-bone cases, because the root bone UID will be e.g. "\Bone\<SkeletonPrimPath>/Root", and its
			// immediate child's will be "\Bone\<SkeletonPrimPath>/<UsdRootBoneName>/Elbow", so *not* "\Bone\<SkeletonPrimPath>/Root/Elbow".
			// This simply because it's the easiest to manipulate/construct here, but either would have worked: These are just UIDs after all.
			// The display labels are still just sensible bone names
			const FString BoneNodeUid = bIsRootBone
				? MakeRootBoneNodeUid(SkeletonPrimPath)
				: MakeBoneNodeUid(SkeletonPrimPath, FullConcatUsdBonePath);

			UInterchangeSceneNode* BoneNode = NewObject<UInterchangeSceneNode>(&NodeContainer);
			NodeContainer.SetupNode(BoneNode, BoneNodeUid, Bone.Name, EInterchangeNodeContainerType::TranslatedScene, ParentNode.GetUniqueID());
			BoneNode->AddSpecializedType(UE::Interchange::FSceneNodeStaticData::GetJointSpecializeTypeString());

			// Note that we use our rest transforms for the Interchange bind pose as well: This because Interchange
			// will put this on the RefSkeleton and so it will make its way to the Skeleton asset. We already kind
			// of bake in our skeleton bind pose directly into our skinned mesh, so we really just want to put the
			// rest pose on the skeleton asset/ReferenceSkeleton
			constexpr bool bResetCache = false;
			BoneNode->SetCustomBindPoseLocalTransform(&NodeContainer, Bone.LocalBindTransform, bResetCache);
			BoneNode->SetCustomTimeZeroLocalTransform(&NodeContainer, Bone.LocalBindTransform, bResetCache);
			BoneNode->SetCustomLocalTransform(&NodeContainer, Bone.LocalBindTransform, bResetCache);

			// These will be used to index into the baked transforms from USD, so we have to pretend our "true root" bone doesn't exist,
			// if we added one
			const int32 UsdBoneIndex = bHadMultipleRootBones ? BoneIndex - 1 : BoneIndex;

			UsdBoneToUidAndUsdBoneIndex.Add(ConcatUsdBonePath, {BoneNodeUid, UsdBoneIndex});
			if (bIsRootBone)
			{
				RootBoneSceneNode = BoneNode;
			}

			for (int32 ChildIndex : Bone.ChildIndices)
			{
				RecursiveTraverseBones(ChildIndex, *BoneNode, ConcatUsdBonePath);
			}
		};

		// Start traversing from the root bone (at this point we know we have exactly one)
		{
			const FString BonePathRoot = TEXT("");
			const int32 BoneIndex = 0;
			RecursiveTraverseBones(BoneIndex, SkeletonPrimNode, BonePathRoot);
		}
		if (!RootBoneSceneNode)
		{
			return;
		}

		// Interchange will abort parsing skeletons that don't have unique names for each bone. If the user has that
		// on their actual skeleton, then that's just invalid data and we can just let it fail and emit the error message.
		// However, we don't want to end up with duplicate bone names and fail to parse when the duplicate bone is due to
		// our extra true root bone... In this case, here we just change the display label of that node itself to be unique
		// (which is used for the bone name)
		if (bHadMultipleRootBones && RootBoneSceneNode)
		{
			TSet<FString> UsedBoneNames;
			for (int32 BoneIndex = 1; BoneIndex < ConvertedData.Bones.Num(); ++BoneIndex)
			{
				const UsdToUnreal::FUsdSkeletonData::FBone& Bone = ConvertedData.Bones[BoneIndex];
				UsedBoneNames.Add(Bone.Name);
			}

			FString NewSkeletonPrimName = UsdUnreal::ObjectUtils::GetUniqueName(UE::Interchange::USD::RootBoneUidSuffix, UsedBoneNames);
			if (NewSkeletonPrimName != UE::Interchange::USD::RootBoneUidSuffix)
			{
				const FString RootBoneUid = MakeRootBoneNodeUid(SkeletonPrimPath);
				RootBoneSceneNode->SetDisplayLabel(NewSkeletonPrimName);
			}
		}

		// Handle SkelAnimation prims, if we have any bound for this Skeleton
		const UInterchangeSkeletalAnimationTrackNode* SkelAnimNode = AddSkeletalAnimationNode(
			SkelQuery,
			UsdBoneToUidAndUsdBoneIndex,
			*this,
			*RootBoneSceneNode,
			NodeContainer,
			Info
		);
		if (SkelAnimNode)
		{
			RootBoneSceneNode->SetCustomAnimationAssetUidToPlay(SkelAnimNode->GetUniqueID());
		}

		// Cache our joint names in order, as this is needed when generating skeletal mesh payloads
		Info.SkelJointUsdNames = MakeShared<TArray<FString>>();
		Info.SkelJointUsdNames->Reserve(ConvertedData.Bones.Num());
		for (const UsdToUnreal::FUsdSkeletonData::FBone& Bone : ConvertedData.Bones)
		{
			// The joint names that the skeletal mesh payload needs to retrieve should match exactly the USD bone
			// structure (and so correspond to the vertex bone influence indices). We really don't want our extra
			// "true root" bone to show up in there, or else we will mislead Interchange
			if (bHadMultipleRootBones && Bone.ParentIndex == INDEX_NONE)
			{
				continue;
			}

			Info.SkelJointUsdNames->Add(Bone.Name);
		}

		// Prefer flagging the Skeleton prim itself as the BoundSkeletonPrimPath at this point in the hierarchy, even
		// preferring it over any explicit skel:skeleton relationship.
		// This does not seem technically correct, but is useful in case the Skeleton prim has a skel:animationSource
		// relationship directly on it, which seems to animate in usdview and is advertised as a supported case.
		// References:
		// - https://github.com/usd-wg/assets/blob/main/test_assets/USDZ/CesiumMan/CesiumMan.usdz
		// - https://openusd.org/release/api/_usd_skel__o_m.html
		// - https://github.com/PixarAnimationStudios/OpenUSD/issues/3532
		if (UE::FUsdPrim SkeletonPrim = SkelQuery.GetSkeleton())
		{
			Info.BoundSkeletonPrimPath = MakeShared<FString>(SkeletonPrim.GetPrimPath().GetString());
		}

		{
			// Cache the skeleton global transform, because if we need to bake our skeletal mesh later then we'll need to use this
			// and it's awkward to have Interchange provide this to us natively, as the "skeleton prim node" here, which corresponds
			// to the "first scene node parent of the root joint transform" doesn't really mean anything for the other formats...
			const FTransform GlobalOffsetTransform;
			SkeletonPrimNode.GetCustomGlobalTransform(&NodeContainer, GlobalOffsetTransform, Info.SceneGlobalTransform);

			FWriteScopeLock ScopedInfoWriteLock{CachedTraversalInfoLock};
			NodeUidToCachedTraversalInfo.Add(SkeletonPrimNode.GetUniqueID(), Info);
		}
#endif	  // WITH_EDITOR
	}

	void Traverse(
		const UE::FUsdPrim& Prim,
		UInterchangeUSDTranslatorImpl& TranslatorImpl,
		UInterchangeBaseNodeContainer& NodeContainer,
		UInterchangeUsdTranslatorSettings* TranslatorSettings,
		FTraversalInfo Info
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Traverse)

		const FName TypeName = Prim.GetTypeName();
		const FString SceneNodeUid = MakeNodeUid(Prim.GetPrimPath().GetString());
		FString DisplayLabel;
		if (Prim.IsPseudoRoot())
		{
			DisplayLabel = Prim.GetStage().GetRootLayer().GetDisplayName();
		}
		else
		{
			DisplayLabel = Prim.HasAuthoredDisplayName() ? Prim.GetDisplayName() : Prim.GetName().ToString();
		}

		// Do this before generating other nodes as they may need the updated info
		Info.UpdateWithCurrentPrim(Prim);

		// TODO: Maybe refactor this function to create the scene node first, and then pass it into the AddXNode functions?
		// That way they can assign their assets to the scene nodes however they want to, an we don't have to store these
		// random return types here
		FString VolumeMaterialInstanceUid;
		bool bNeedsVolumeTrack = false;

		// Generate asset node(s) if applicable
		TArray<FString> AssetNodeUids;
		if (Prim.IsA(TEXT("Material")))
		{
			AssetNodeUids = {TranslatorImpl.AddMaterialNode(Prim, TranslatorSettings, NodeContainer)};
		}
		else if (Prim.IsA(TEXT("Mesh")))
		{
			AssetNodeUids = {TranslatorImpl.AddMeshNode(Prim, TranslatorSettings, NodeContainer, Info)};
		}
		else if (Prim.IsA(TEXT("Camera")))
		{
			AssetNodeUids = {AddCameraNode(Prim, NodeContainer)};
		}
		else if (Prim.HasAPI(TEXT("LightAPI")))
		{
			AssetNodeUids = {AddLightNode(Prim, NodeContainer)};
		}
		else if (Prim.IsA(TEXT("Gprim")) && !Prim.IsA(TEXT("PointBased")) && !Prim.IsA(TEXT("Volume")))
		{
			// PointBased prims are currently not supported apart from Meshes (which is taken care of in a previous branch).
			// Volumes are also not currently supported.

			constexpr bool bPrimitiveShape_True = true;
			AssetNodeUids = {TranslatorImpl.AddMeshNode(Prim, TranslatorSettings, NodeContainer, Info, bPrimitiveShape_True)};
		}
		else if (Prim.IsA(TEXT("Volume")))
		{
			AssetNodeUids = TranslatorImpl.AddVolumeNodes(Prim, TranslatorSettings, NodeContainer, VolumeMaterialInstanceUid, bNeedsVolumeTrack);
		}
		else if ((Prim.IsA(TEXT("BasisCurves")) || Prim.IsA(TEXT("Xform"))) && Prim.HasAPI(TEXT("GroomAPI")))
		{
			TOptional<FString> AssetNodeUid = TranslatorImpl.AddGroomNode(Prim, NodeContainer);
			if (AssetNodeUid.IsSet())
			{
				AssetNodeUids = {*AssetNodeUid};
			}
		}
		else if (Prim.IsA(TEXT("SpatialAudio")))
		{
			if (GInterchangeEnableUSDAudioImport)
			{
				AssetNodeUids = { TranslatorImpl.AddSpatialAudioNode(Prim, NodeContainer) };
			}
			else
			{
				UInterchangeResultWarning_Generic* Message = TranslatorImpl.ResultsContainer->Add<UInterchangeResultWarning_Generic>();
				Message->SourceAssetName = TranslatorImpl.UsdStage.GetRootLayer().GetRealPath();
				Message->Text = FText::Format(
					LOCTEXT("UnsupportedSchemaType", "Prim '{0}' has schema '{1}', which is not yet supported via USD Interchange."),
					FText::FromString(SceneNodeUid),
					FText::FromName(TypeName)
				);
			}
		}

		if (Prim.IsA(TEXT("Gprim")) && Prim.GetParent().IsA(TEXT("Gprim")))
		{
			UInterchangeResultWarning_Generic* Message = TranslatorImpl.ResultsContainer->Add<UInterchangeResultWarning_Generic>();
			Message->SourceAssetName = TranslatorImpl.UsdStage.GetRootLayer().GetRealPath();
			Message->Text = FText::Format(
				LOCTEXT(
					"NestedGprims",
					"Prim '{0}' is a Gprim child of another Gprim. This configuration is not recommended or supported very well, and may lead to unexpected results."
				),
				FText::FromString(Prim.GetPrimPath().GetString())
			);
		}

		// Only prims that require rendering (and have a renderable parent) get a scene node.
		// This includes Xforms but also Scopes, which are not Xformable.
		// Also allow typeless prims to get a scene node otherwise some assets like geometry cache
		// would not get processed and they need to bake the transforms into the meshes.
		//
		// We add a scene node for the pseudoroot in order to make tree traversal easier on the pipeline,
		// but the pipeline will strip the pseudoroot node as its final step (if desired)
		const bool bIsImageable = Prim.IsA(TEXT("Imageable"));
		const bool bIsTypeless = TypeName.IsNone();
		const bool bNeedsSceneNode = Prim.IsPseudoRoot()
									 || bIsTypeless 
									 || (bIsImageable && (Info.ParentNode || Prim.GetParent().IsPseudoRoot()))
									 || Info.bIsLODContainer;	 // We allow typeless prims (so not imageable) to be LOD containers

		UInterchangeSceneNode* SceneNode = nullptr;
		if (bNeedsSceneNode)
		{
			SceneNode = CreateSceneNode(Prim, TranslatorSettings, NodeContainer, SceneNodeUid, DisplayLabel, Info.ParentNode ? Info.ParentNode->GetUniqueID() : TEXT(""));

			// If we're an Xformable, get our transform.
			// All SceneNodes should have their LocalTransform set though.
			// Not setting will cause ensure hits in Skeleton generations for example.
			FTransform Transform = FTransform::Identity;
			bool bResetTransformStack = false;
			UsdToUnreal::ConvertXformable(Prim.GetStage(), UE::FUsdTyped(Prim), Transform, UsdUtils::GetEarliestTimeCode(), &bResetTransformStack);

			constexpr bool bResetCache = false;
			SceneNode->SetCustomLocalTransform(&NodeContainer, Transform, bResetCache);

			// Hide our scene node if it is meant to be hidden in USD
			//
			// We use actor visibility so that it matches how we map visibility timeSamples to actor visibility tracks.
			// We do *that*, because it matches how Interchange always puts transform animations on the actors directly (so
			// "scene component stuff" ends up as actor tracks), and also due to how it behaves better for cameras: Component
			// visibility for camera nodes would hide the camera component itself, which has no effect. Actor visibility for
			// camera actors does hide the entire camera actor however
			if (!Info.bVisible)
			{
				SceneNode->SetCustomActorVisibility(Info.bVisible);
			}

			// Connect scene node and primary asset node
			if (AssetNodeUids.Num() != 0)
			{
				SceneNode->SetCustomAssetInstanceUid(AssetNodeUids[0]);
			}

			if (Info.bIsLODContainer)
			{
				TranslatorImpl.AddLODMeshNodes(Prim, NodeContainer, *SceneNode, TranslatorSettings, Info);
			}

			if (Info.bInsideLOD && Prim.IsA(TEXT("Mesh")) && GetLODIndexFromName(SceneNode->GetDisplayLabel()) != INDEX_NONE)
			{
				UInterchangeSceneNode* ParentSceneNode = Cast<UInterchangeSceneNode>(Info.ParentNode);
				ensure(ParentSceneNode);

				TranslatorImpl.LODVariantSetNodeToLodMeshNodes.FindOrAdd(ParentSceneNode).Add(SceneNode);
			}

			// Skeleton joints are separate scene nodes in Interchange, so we need to emit that node hierarchy now
			if (Prim.IsA(TEXT("Skeleton")))
			{
				TranslatorImpl.AddSkeletonNodes(Prim, *SceneNode, NodeContainer, Info);
			}

			if (Prim.IsA(TEXT("Volume")))
			{
				AddVolumeSceneNodeAttributes(
					Prim,
					SceneNode,
					AssetNodeUids,
					VolumeMaterialInstanceUid,
					bNeedsVolumeTrack,
					TranslatorImpl,
					NodeContainer
				);
			}

			// Set component material overrides
			//
			// For now we only need material overrides in case of USD instanceables.
			//
			// TODO: While the USD translator and pipeline's side of this should be working, material overrides don't quite work just yet,
			// as ActorHelper::ApplySlotMaterialDependencies (the thing responsible for setting material overrides on the actor components)
			// will try to set these overrides according to slot name, while we always just place the (mesh's) material path itself as the
			// slot name... We need some different mechanism for setting slot name (maybe a translator/pipeline setting to just use the
			// GeomSubset prim name, or the slot index?) that can also work for setting material overrides. Also, see FixMaterialSlotNames()
			if (Prim.IsA(TEXT("Mesh")) && (Prim.IsInstance() || Prim.IsInstanceProxy()))
			{
				// This is in charge of picking up on collection-based material assignments, as we'll use the instance proxy prim here
				const double TimeCode = UsdUtils::GetDefaultTimeCode();
				const bool bProvideMaterialIndices = false;
				UsdUtils::FUsdPrimMaterialAssignmentInfo InstanceAssignments = UsdUtils::GetPrimMaterialAssignments(
					Prim,
					TimeCode,
					bProvideMaterialIndices,
					TranslatorImpl.CachedMeshConversionOptions.RenderContext,
					TranslatorImpl.CachedMeshConversionOptions.MaterialPurpose
				);

				TranslatorImpl.SetSlotMaterialDependencies(SceneNode, InstanceAssignments, TranslatorSettings, NodeContainer);
			}

			// Add animation tracks
			if (bIsImageable)
			{
				AddPropertyAnimationNodes(Prim, TranslatorImpl, NodeContainer);
				if (UsdUtils::HasAnimatedTransform(Prim))
				{
					AddTransformAnimationNode(Prim, TranslatorImpl, NodeContainer);
				}
			}
		}

		// Recurse into child prims
		{
			Info.ParentNode = SceneNode;

			if (Prim.HasAPI(TEXT("NaniteAssemblyRootAPI")))
			{
				TranslatorImpl.AddNaniteAssemblyHierarchy(Prim, TranslatorSettings, NodeContainer, SceneNode, Info);
			}
			else if (Prim.IsA(TEXT("PointInstancer")))
			{
				FISMComponentTraversalHelper ISMComponentTraversalHelper(TranslatorImpl, NodeContainer, TranslatorSettings, SceneNode);
				ISMComponentTraversalHelper.TraversePointInstancerHierarchy(Prim, Info, FTransform());
			}
			else
			{
				for (const FUsdPrim& ChildPrim : CheckLodAPIAndGetChildren(Prim, SceneNode))
				{
					Traverse(ChildPrim, TranslatorImpl, NodeContainer, TranslatorSettings, Info);
				}
			}
		}

		// Finalize the LOD container after recursing normally, because we'll rely on the regular traversal to
		// process the mesh of the active LOD variant. AddLODMeshNodes only handles the inactive variants
		if (Info.bIsLODContainer)
		{
			TranslatorImpl.FinalizeLODContainerTraversal(NodeContainer, Info, SceneNode);
		}
	}

	TArray<UE::Interchange::FNaniteAssemblyDescription::FBoneInfluence> CreateBoneInfluences(
		const TArray<int32>& JointIndices, 
		const TArray<float>& JointWeights)
	{
		// Create the bone influence (joint index + weight) array from the primvar attributes.
		TArray<UE::Interchange::FNaniteAssemblyDescription::FBoneInfluence> BoneInfluences;
		if (ensure(JointIndices.Num() == JointWeights.Num()))
		{
			BoneInfluences.Reserve(JointIndices.Num());
			for (int32 InfluenceIndex = 0; InfluenceIndex < JointIndices.Num(); ++InfluenceIndex)
			{
				BoneInfluences.Emplace(JointIndices[InfluenceIndex], JointWeights[InfluenceIndex]);
			}
		}

		return BoneInfluences;
	}

	void GetNaniteAssemblyPayloadDataForPrims(
		const UE::FUsdStage& UsdStage,
		const UsdToUnreal::FUsdMeshConversionOptions& Options,
		const UsdToUnreal::NaniteAssemblyUtils::FNaniteAssemblyTraversalResult& TraversalResult,
		const FString& SkelIdentifier,
		const TArray<FString>& JointNames,
		TOptional<UE::Interchange::FNaniteAssemblyDescription>& OutNaniteAssemblyDescription)
	{
		using namespace UsdToUnreal::NaniteAssemblyUtils;

		const bool bIsNewDescription = !OutNaniteAssemblyDescription.IsSet();
		const int32 PartIndexStartOffset = bIsNewDescription ? 0 : OutNaniteAssemblyDescription.GetValue().PartUids.Num();

		TArray<TSharedPtr<FMeshEntry>> NativeInstancedEntries;
		TMap<FString, int32> MeshUidToPartIndexTable;
		TArray<TArray<FString>> PartUids;

		if (!TraversalResult.CheckAndGetNativeInstancedPartMeshEntries(NativeInstancedEntries, MeshUidToPartIndexTable, PartUids)
			|| NativeInstancedEntries.IsEmpty())
		{
			return;
		}

		if (bIsNewDescription)
		{
			OutNaniteAssemblyDescription = UE::Interchange::FNaniteAssemblyDescription{};
		}
		UE::Interchange::FNaniteAssemblyDescription& Description = OutNaniteAssemblyDescription.GetValue();

		FJointBindingHelper JointBindings{ SkelIdentifier, JointNames, static_cast<float>(Options.TimeCode.GetValue()) };

		// For each entry add an instance to the description
		int32 NumAdded = 0;
		for (const TSharedPtr<FMeshEntry>& Entry : NativeInstancedEntries)
		{
			int32* PartIndex = MeshUidToPartIndexTable.Find(Entry->NodeUid);
			if (!PartIndex)
			{
				continue;
			}
			
			// If we have a skeleton get joint bindings
			if (!JointNames.IsEmpty())
			{
				UE::FUsdPrim Prim = UsdStage.GetPrimAtPath(Entry->PrimPath);
				UE::FUsdPrim SkelBindingPrim = UsdStage.GetPrimAtPath(Entry->SkelBindingPath);
				if (!(Prim && SkelBindingPrim))
				{
					continue;
				}

				TArray<int32> JointIndices;
				TArray<float> JointWeights;
				int32 ElementSize;
				if (!JointBindings.Get(SkelBindingPrim, JointIndices, JointWeights, ElementSize))
				{
					continue;
				}

				TArray<UE::Interchange::FNaniteAssemblyDescription::FBoneInfluence> BoneInfluences
					= CreateBoneInfluences(JointIndices, JointWeights);

				Description.NumInfluencesPerInstance.Add(ElementSize);
				Description.BoneInfluences.Append(BoneInfluences);
			}
	
			Description.Transforms.Add(Entry->TransformStack[0] * Options.AdditionalTransform);
			Description.PartIndices.Add(*PartIndex + PartIndexStartOffset);
			NumAdded++;
		}

		if (NumAdded > 0)
		{
			Description.PartUids.Append(PartUids);
		}
		
		if (NumAdded != NativeInstancedEntries.Num())
		{
			const int32 NumFailed = NativeInstancedEntries.Num() - NumAdded;
			UE_LOG(LogUsd, Warning,
				TEXT("Failed to add (%d) skeletal mesh instances for Nanite assembly '%s'")
				, NumFailed
				, *TraversalResult.AssemblyRootPrimPath.GetString()
			);
		}
	}

	void GetNaniteAssemblyPayloadDataForPointInstancer(
		const UE::FUsdPrim& PointInstancerPrim,
		const UsdToUnreal::FUsdMeshConversionOptions& Options,
		const UsdToUnreal::NaniteAssemblyUtils::FNaniteAssemblyTraversalResult& TraversalResult,
		const FString& SkelIdentifier,
		const TArray<FString>& JointNames,
		TOptional<UE::Interchange::FNaniteAssemblyDescription>& OutNaniteAssemblyDescription)
	{
		using namespace UsdToUnreal::NaniteAssemblyUtils;

		const bool bIsNewDescription = !OutNaniteAssemblyDescription.IsSet();
		const int32 PartIndexStartOffset = bIsNewDescription ? 0 : OutNaniteAssemblyDescription.GetValue().PartUids.Num();

		FNaniteAssemblyPointInstancerData PointInstancerData =
			GetPointInstancerData(
				PointInstancerPrim,
				Options,
				TraversalResult,
				SkelIdentifier,
				JointNames,
				PartIndexStartOffset
			);

		if (!PointInstancerData.bIsValid)
		{
			return;
		}

		TArray<UE::Interchange::FNaniteAssemblyDescription::FBoneInfluence> BoneInfluences 
			= CreateBoneInfluences(PointInstancerData.JointIndices, PointInstancerData.JointWeights);

		// Now we have everything needed to create a new, or append to an existing, output description...

		if (bIsNewDescription)
		{
			OutNaniteAssemblyDescription = UE::Interchange::FNaniteAssemblyDescription{};
		}
		UE::Interchange::FNaniteAssemblyDescription& Description = OutNaniteAssemblyDescription.GetValue();

		// Helper to move or append to the description arrays
		auto UpdateDescriptionArray = [bIsNewDescription]<typename T>(T& Dst, T& Src)
		{
			if (bIsNewDescription) { Dst = MoveTemp(Src); }
			else { Dst.Append(Src); }
		};

		// If all the transform stacks leading to the part meshes were i) at identity and ii) not nested, we can 
		// use the arrays of part uids as-is.
		if (!PointInstancerData.ProtoRemappingInfo.IsRemapped())
		{
			UpdateDescriptionArray(Description.Transforms, PointInstancerData.Transforms);
			UpdateDescriptionArray(Description.PartIndices, PointInstancerData.ProtoIndices);
			UpdateDescriptionArray(Description.PartUids, PointInstancerData.ProtoRemappingInfo.PartUids);
			UpdateDescriptionArray(Description.BoneInfluences, BoneInfluences);
			UpdateDescriptionArray(Description.NumInfluencesPerInstance, PointInstancerData.NumInfluencesPerInstance);
			return;
		}
		else if (TraversalResult.IsMeshType(ENaniteAssemblyMeshType::SkeletalMesh))
		{
			// Remapped prototypes is unexpected for skeletal mesh assemblies here because Interchange always
			// combines the USD meshes bound to the same skeleton into a single uasset. As a result there
			// should only be a single part/uasset slot required for each prototype.
			return;
		}
		
		// Otherwise generate expanded arrays incorporating the appropriate transform stack for each part.
		TArray<FTransform> RemappedTransforms;
		TArray<int32> RemappedPartIndices;
		TArray<TArray<FString>>& RemappedPartUids = PointInstancerData.ProtoRemappingInfo.PartUids;

		TArray<FTransform>& LocalTransforms = PointInstancerData.ProtoRemappingInfo.LocalTransforms;
		TMap<int32, FInt32Range>& RangeTable = PointInstancerData.ProtoRemappingInfo.OriginalPrototypeIndexToNewPartRangeTable;

		// Submit each original transform/prototype multiple times, once per part...
		for (int32 Index = 0; Index < PointInstancerData.Transforms.Num(); ++Index)
		{
			const FTransform& InstanceTransform = PointInstancerData.Transforms[Index];
			int32 OriginalProtoIndex = PointInstancerData.ProtoIndices[Index] - PartIndexStartOffset;

			if (OriginalProtoIndex >= 0 && ensure(RangeTable.Contains(OriginalProtoIndex)))
			{
				const FInt32Range& PartIndexRange = RangeTable.FindChecked(OriginalProtoIndex);
				const int32 Start = PartIndexRange.GetLowerBound().GetValue();
				const int32 End = PartIndexRange.GetUpperBound().GetValue();
				for (int32 CurrentIndex = Start; CurrentIndex < End; ++CurrentIndex)
				{
					const int32 OriginalCurrentIndex = CurrentIndex - PartIndexStartOffset;
					if (ensure(LocalTransforms.IsValidIndex(OriginalCurrentIndex)))
					{
						RemappedTransforms.Add(LocalTransforms[OriginalCurrentIndex] * InstanceTransform);
						RemappedPartIndices.Add(CurrentIndex);
					}
				}
			}
		}

		UpdateDescriptionArray(Description.Transforms, RemappedTransforms);
		UpdateDescriptionArray(Description.PartIndices, RemappedPartIndices);
		UpdateDescriptionArray(Description.PartUids, RemappedPartUids);
	}

	bool GetStaticMeshPayloadData(
		FString PayloadKey,
		UInterchangeUSDTranslatorImpl& Impl,
		UsdToUnreal::FUsdMeshConversionOptions& Options,
		FMeshDescription& OutMeshDescription,
		TOptional<UE::Interchange::FNaniteAssemblyDescription>& OutNaniteAssemblyDescription
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetStaticMeshPayloadData)

		const bool bIsLODMesh = CheckAndChopPayloadPrefix(PayloadKey, LODPrefix);
		const bool bIsPrimitiveShape = CheckAndChopPayloadPrefix(PayloadKey, PrimitiveShapePrefix);

		const FString& PrimPath = PayloadKey;
		UE::FUsdPrim Prim = Impl.UsdStage.GetPrimAtPath(UE::FSdfPath{*PrimPath});
		if (!Prim)
		{
			if (bIsLODMesh)
			{
				Prim = Impl.TryGettingInactiveLODPrim(PrimPath);
			}

			if (!Prim)
			{
				return false;
			}
		}

		FMeshDescription TempMeshDescription;
		FStaticMeshAttributes StaticMeshAttributes(TempMeshDescription);
		StaticMeshAttributes.Register();

		// Get Nanite assembly static mesh data, if available
		
		const FString MeshNodeUid = MakeNodeUid(MeshPrefix + Prim.GetPrimPath().GetString());
		FTraversalInfo* MeshInfo;
		{
			FReadScopeLock ReadLock{ Impl.CachedTraversalInfoLock };
			MeshInfo = Impl.NodeUidToCachedTraversalInfo.Find(MeshNodeUid);
		}

		if (MeshInfo && MeshInfo->NaniteAssemblyTraversalResult)
		{
			if (const UsdToUnreal::NaniteAssemblyUtils::FNaniteAssemblyTraversalResult* TraversalResult = MeshInfo->NaniteAssemblyTraversalResult.Get())
			{
				static const FString UnusedSkelIdentifier;
				static const TArray<FString> UnusedJointNames;
				
				// Add native instance data to the output description
				GetNaniteAssemblyPayloadDataForPrims(
					Impl.UsdStage,
					Options,
					*TraversalResult,
					UnusedSkelIdentifier,
					UnusedJointNames,
					OutNaniteAssemblyDescription);

				// Add pointinstancer data to the output description
				for (const UE::FSdfPath& TopLevelPointInstancerPath : TraversalResult->GetTopLevelPointInstancerPaths())
				{
					UE::FUsdPrim PointInstancerPrim = Impl.UsdStage.GetPrimAtPath(TopLevelPointInstancerPath);
					GetNaniteAssemblyPayloadDataForPointInstancer(
						PointInstancerPrim,
						Options,
						*TraversalResult,
						UnusedSkelIdentifier,
						UnusedJointNames,
						OutNaniteAssemblyDescription);
				}

				// Right now we only support empty static mesh base meshes, since the NaniteAssemblyRootAPI schema only 
				// allows the user to specify a base mesh for the skeletal mesh case.
				OutMeshDescription = MoveTemp(TempMeshDescription);
				return true;
			}
		}
		
		UsdUtils::FUsdPrimMaterialAssignmentInfo TempMaterialInfo;

		bool bSuccess = false;
		if (bIsPrimitiveShape)
		{
			bSuccess = UsdToUnreal::ConvertGeomPrimitive(Prim, TempMeshDescription, TempMaterialInfo, Options);
		}
		else
		{
			bSuccess = UsdToUnreal::ConvertGeomMesh(Prim, TempMeshDescription, TempMaterialInfo, Options);
		}
		if (!bSuccess)
		{
			return false;
		}

		UsdUtils::FUsdPrimMaterialAssignmentInfo* AssignmentPtr = &TempMaterialInfo;
		if (bIsLODMesh)
		{
			// Use our cached material assignments instead of whatever we pull from ConvertGeomMesh
			// because if we're in an LOD mesh then we may be reading from a temp stage, that has a
			// population mask that may not include the material, meaning ConvertGeomMesh may have failed
			// to resolve all the bindings. The cached assignments come from AddMeshNode step, where
			// we switch the active variant on the current stage and so get nice material bindings that
			// resolve normally.
			// TODO: We may not need this anymore, as we don't use population masks to open the LOD stages anymore
			//
			// Note that we can't even use the info cache here, because it wouldn't have cached info
			// about the inactive LOD variants
			if (UsdUtils::FUsdPrimMaterialAssignmentInfo* CachedInfo = Impl.CachedMaterialAssignments.Find(PrimPath))
			{
				AssignmentPtr = CachedInfo;
			}
		}

		OutMeshDescription = MoveTemp(TempMeshDescription);

		FixMaterialSlotNames(OutMeshDescription, AssignmentPtr->Slots);

		return true;
	}

	bool GetSkeletalMeshPayloadData(
		FString PayloadKey,
		bool bBakeMeshes,
		FTransform GlobalOffsetTransform,
		UInterchangeUSDTranslatorImpl& Impl,
		const UsdToUnreal::FUsdMeshConversionOptions& Options,
		FMeshDescription& OutMeshDescription,
		TArray<FString>& OutJointNames,
		TOptional<UE::Interchange::FNaniteAssemblyDescription>& OutNaniteAssemblyDescription
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetSkeletalMeshPayloadData)

#if WITH_EDITOR
		const bool bIsLODMesh = CheckAndChopPayloadPrefix(PayloadKey, LODPrefix);

		const FString& PrimPath = PayloadKey;
		UE::FUsdPrim Prim = Impl.UsdStage.GetPrimAtPath(UE::FSdfPath{*PrimPath});
		if (!Prim)
		{
			if (bIsLODMesh)
			{
				Prim = Impl.TryGettingInactiveLODPrim(PrimPath);
			}

			if (!Prim)
			{
				return false;
			}
		}

		// Note: skeletal mesh are currently always added with an instance proxy prim based node uid,
		// not prototype prim based uid, because instanced skeletal meshes are not well supported. Once
		// Interchange can instance skeletal meshes we should use the line below to obtain the node uid
		//  (in the same way static mesh does already).
		// const FString& MeshNodeUid = MeshPrefix + UsdUtils::GetPrototypePrimPath(Prim).GetString();
		const FString& MeshNodeUid = MakeNodeUid(MeshPrefix + Prim.GetPrimPath().GetString());

		UsdToUnreal::FUsdMeshConversionOptions OptionsCopy = Options;

		// Read these variables from the data we cached during traversal for translation
		TSharedPtr<TArray<FString>> JointNames = nullptr;
		UE::FUsdSkelSkeletonQuery SkelQuery;
		{
			FReadScopeLock ReadLock{Impl.CachedTraversalInfoLock};

			const FTraversalInfo* MeshInfo = Impl.NodeUidToCachedTraversalInfo.Find(MeshNodeUid);
			if (!MeshInfo)
			{
				return false;
			}

			SkelQuery = MeshInfo->ResolveSkelQuery(Impl.UsdStage);
			if (!SkelQuery)
			{
				return false;
			}

			// The above fields are associated to the mesh *asset* node Uid (hence the prefix),
			// while the joint names are associated to the skeleton *scene* node Uid, so no prefix
			const FString& SkeletonNodeUid = MakeNodeUid(*MeshInfo->BoundSkeletonPrimPath);
			const FTraversalInfo* SkeletonInfo = Impl.NodeUidToCachedTraversalInfo.Find(SkeletonNodeUid);
			if (!SkeletonInfo)
			{
				return false;
			}
			JointNames = SkeletonInfo->SkelJointUsdNames;
			if (!JointNames)
			{
				return false;
			}

			if (bBakeMeshes)
			{
				OptionsCopy.AdditionalTransform = SkeletonInfo->SceneGlobalTransform * GlobalOffsetTransform;
			}

			// Get skeletal mesh Nanite assembly data, if available
			if (MeshInfo && MeshInfo->NaniteAssemblyTraversalResult)
			{
				if (const UsdToUnreal::NaniteAssemblyUtils::FNaniteAssemblyTraversalResult* TraversalResult = MeshInfo->NaniteAssemblyTraversalResult.Get())
				{
					// Add native instance data to the output description
					GetNaniteAssemblyPayloadDataForPrims(
						Impl.UsdStage,
						OptionsCopy,
						*TraversalResult,
						SkeletonNodeUid,
						*JointNames,
						OutNaniteAssemblyDescription);

					// Add pointinstancer to the output description
					for (const UE::FSdfPath& TopLevelPointInstancerPath : TraversalResult->GetTopLevelPointInstancerPaths())
					{
						UE::FUsdPrim PointInstancerPrim = Impl.UsdStage.GetPrimAtPath(TopLevelPointInstancerPath);
						GetNaniteAssemblyPayloadDataForPointInstancer(
							PointInstancerPrim, 
							OptionsCopy,
							*TraversalResult,
							SkeletonNodeUid, 
							*JointNames, 
							OutNaniteAssemblyDescription);
					}
				}
			}
		}

		// We cache these because we may need to retrieve these again when computing morph target mesh descriptions
		FWriteScopeLock WriteLock{Impl.SkeletalMeshDescriptionsLock};
		if (FMeshDescription* FoundMeshDescription = Impl.PayloadKeyToSkeletalMeshDescriptions.Find(PayloadKey))
		{
			OutMeshDescription = *FoundMeshDescription;
			OutJointNames = *JointNames;
			return true;
		}

		UE::FUsdSkelSkinningQuery SkinningQuery = UsdUtils::CreateSkinningQuery(Prim, SkelQuery);
		if (!SkinningQuery)
		{
			return false;
		}

		FSkeletalMeshImportData SkelMeshImportData;
		UsdUtils::FUsdPrimMaterialAssignmentInfo TempMaterialInfo;
		FMeshDescription TempMeshDescription;

		pxr::UsdSkelSkinningQuery& PxrSkelSkinningQueryPtr = SkinningQuery;
		pxr::UsdSkelSkeletonQuery& PxrSkelSkeletonQueryPtr = SkelQuery;

		bool bSuccess = UsdToUnreal::ConvertGeomMesh(
			Prim,
			TempMeshDescription,
			TempMaterialInfo,
			OptionsCopy,
			&PxrSkelSkinningQueryPtr,
			&PxrSkelSkeletonQueryPtr
		);
		if (!bSuccess)
		{
			return false;
		}

		OutMeshDescription = MoveTemp(TempMeshDescription);

		UsdUtils::FUsdPrimMaterialAssignmentInfo* AssignmentPtr = &TempMaterialInfo;
		if (bIsLODMesh)
		{
			// Use our cached material assignments instead of whatever we pull from ConvertSkinnedMesh
			// because if we're in an LOD mesh then we may be reading from a temp stage, that has a
			// population mask that may not include the material, meaning ConvertSkinnedMesh may have failed
			// to resolve all the bindings. The cached assignments come from AddMeshNode step, where
			// we switch the active variant on the current stage and so get nice material bindings that
			// resolve normally.
			// TODO: We may not need this anymore, as we don't use population masks to open the LOD stages anymore
			//
			// Note that we can't even use the info cache here, because it wouldn't have cached info
			// about the inactive LOD variants
			if (UsdUtils::FUsdPrimMaterialAssignmentInfo* CachedInfo = Impl.CachedMaterialAssignments.Find(PrimPath))
			{
				AssignmentPtr = CachedInfo;
			}
		}

		FixMaterialSlotNames(OutMeshDescription, AssignmentPtr->Slots);

		OutJointNames = *JointNames;

		Impl.PayloadKeyToSkeletalMeshDescriptions.Add(PayloadKey, OutMeshDescription);

		return true;
#else
		return false;
#endif	  // WITH_EDITOR
	}

	bool GetMorphTargetPayloadData(
		FString PayloadKey,
		bool bBakeMeshes,
		FTransform GlobalOffsetTransform,
		UInterchangeUSDTranslatorImpl& Impl,
		const UsdToUnreal::FUsdMeshConversionOptions& Options,
		FMeshDescription& OutMeshDescription,
		FString& OutMorphTargetName
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetMorphTargetPayloadData)

		bool bIsLODMesh = false;
		FString MeshPrimPath;
		int32 BlendShapeIndex = INDEX_NONE;
		FString InbetweenName;
		if (!ParseMorphTargetMeshPayloadKey(PayloadKey, bIsLODMesh, MeshPrimPath, BlendShapeIndex, InbetweenName))
		{
			return false;
		}

		UE::FUsdPrim MeshPrim = Impl.UsdStage.GetPrimAtPath(FSdfPath{*MeshPrimPath});
		if (!MeshPrim)
		{
			if (bIsLODMesh)
			{
				MeshPrim = Impl.TryGettingInactiveLODPrim(MeshPrimPath);
			}

			if (!MeshPrim)
			{
				return false;
			}
		}

		UE::FUsdSkelBlendShapeQuery Query{MeshPrim};
		if (!Query)
		{
			return false;
		}

		UE::FUsdSkelBlendShape BlendShape = Query.GetBlendShape(BlendShapeIndex);
		if (!BlendShape)
		{
			return false;
		}

		TArray<FString> UnusedJointNames;
		TOptional<UE::Interchange::FNaniteAssemblyDescription> UnusedNaniteData;
		const FString MeshPayloadKey = bIsLODMesh ? LODPrefix + MeshPrimPath : MeshPrimPath;
		bool bConverted = GetSkeletalMeshPayloadData(
			MeshPayloadKey,
			bBakeMeshes,
			GlobalOffsetTransform,
			Impl,
			Options,
			OutMeshDescription,
			UnusedJointNames,
			UnusedNaniteData
		);
		if (!bConverted || OutMeshDescription.IsEmpty())
		{
			return false;
		}

		OutMorphTargetName = BlendShape.GetPrim().GetName().ToString();
		if (!InbetweenName.IsEmpty())
		{
			OutMorphTargetName += TEXT("_") + InbetweenName;
		}

		// Collect GeomBindTransform if we have one
		FMatrix GeomBindTransform = FMatrix::Identity;
		{
			UE::FUsdSkelSkeletonQuery SkelQuery;
			{
				FReadScopeLock ReadLock{Impl.CachedTraversalInfoLock};

				const FString& MeshNodeUid = MakeNodeUid(MeshPrefix + UsdUtils::GetPrototypePrimPath(MeshPrim).GetString());
				const FTraversalInfo* MeshInfo = Impl.NodeUidToCachedTraversalInfo.Find(MeshNodeUid);
				if (MeshInfo)
				{
					SkelQuery = MeshInfo->ResolveSkelQuery(Impl.UsdStage);
				}
			}

			if (SkelQuery)
			{
				if (UE::FUsdSkelSkinningQuery SkinningQuery = UsdUtils::CreateSkinningQuery(MeshPrim, SkelQuery))
				{
					GeomBindTransform = SkinningQuery.GetGeomBindTransform(Options.TimeCode.GetValue());
				}
			}
		}

		const float Weight = 1.0f;
		return UsdUtils::ApplyBlendShape(
			OutMeshDescription,
			BlendShape.GetPrim(),
			GeomBindTransform,
			Options.AdditionalTransform,
			Weight,
			InbetweenName
		);
	}

	// Volume animations need a special property reader because in USD it's an animation of the file path attribute within
	// the asset prims, while in UE we want a float track flipping through the volume frame indices
	UsdToUnreal::FPropertyTrackReader CreateVolumeTrackReader(
		const FString& InPrimPath,
		const UInterchangeUSDTranslatorImpl& InImpl,
		TArray<double>& OutTimeSampleUnion
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CreateVolumeTrackReader)

		UsdToUnreal::FPropertyTrackReader Result;

		const TArray<UsdUtils::FVolumePrimInfo>* CollectedInfoForPrim = InImpl.PrimPathToVolumeInfo.Find(InPrimPath);
		if (!CollectedInfoForPrim)
		{
			return Result;
		}

		const UsdUtils::FVolumePrimInfo* AnimatedInfo = nullptr;

		for (const UsdUtils::FVolumePrimInfo& Info : *CollectedInfoForPrim)
		{
			if (Info.TimeSamplePathTimeCodes.Num() > 0)
			{
				if (AnimatedInfo != nullptr)
				{
					UE_LOG(
						LogUsd,
						Log,
						TEXT(
							"Only one animated SparseVolumeTexture can be driven via LevelSequences for each prim, for now. Prim '%s' has multiple, so the animation may not be correct."
						),
						*InPrimPath
					);
				}

				AnimatedInfo = &Info;
			}
		}

		if (AnimatedInfo)
		{
			// Detect whether we inserted the default opinion volume as the first frame of the animation
			bool bInsertedDefaultOpinion = AnimatedInfo->TimeSamplePaths.Num() > 0
										   && AnimatedInfo->TimeSamplePaths[0] != AnimatedInfo->SourceVDBFilePath;

			OutTimeSampleUnion = AnimatedInfo->TimeSamplePathTimeCodes;
			Result.FloatReader = [AnimatedInfo, bInsertedDefaultOpinion](double TimeCode) -> float
			{
				int32 FrameIndex = 0;
				for (; FrameIndex + 1 < AnimatedInfo->TimeSamplePathTimeCodes.Num(); ++FrameIndex)
				{
					if (AnimatedInfo->TimeSamplePathTimeCodes[FrameIndex + 1] > TimeCode)
					{
						break;
					}
				}
				FrameIndex = FMath::Clamp(FrameIndex, 0, AnimatedInfo->TimeSamplePathTimeCodes.Num() - 1);

				if (bInsertedDefaultOpinion)
				{
					FrameIndex += 1;
				}

				return static_cast<float>(FrameIndex);
			};
		}

		return Result;
	}

	bool GetPropertyAnimationCurvePayloadData(
		const UInterchangeUSDTranslatorImpl& Impl,
		const FString& PayloadKey,
		Interchange::FAnimationPayloadData& OutPayloadData
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetPropertyAnimationCurvePayloadData)

		FString PrimPath;
		FString UEPropertyNameStr;
		bool bSplit = PayloadKey.Split(TEXT("\\"), &PrimPath, &UEPropertyNameStr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (!bSplit)
		{
			return false;
		}

		UE::FUsdPrim Prim = Impl.UsdStage.GetPrimAtPath(UE::FSdfPath{*PrimPath});
		FName UEPropertyName = *UEPropertyNameStr;
		if (!Prim || UEPropertyName == NAME_None)
		{
			return false;
		}

		TArray<double> TimeSampleUnion;
		UsdToUnreal::FPropertyTrackReader Reader;

		if (Prim.IsA(TEXT("Volume")) && UEPropertyNameStr == GET_MEMBER_NAME_CHECKED(UHeterogeneousVolumeComponent, Frame))
		{
			Reader = CreateVolumeTrackReader(PrimPath, Impl, TimeSampleUnion);
		}
		else
		{
			TArray<UE::FUsdAttribute> Attrs = UsdUtils::GetAttributesForProperty(Prim, UEPropertyName);
			bool bSuccess = UE::FUsdAttribute::GetUnionedTimeSamples(Attrs, TimeSampleUnion);
			if (!bSuccess)
			{
				return false;
			}

			const bool bIgnorePrimLocalTransform = false;
			Reader = UsdToUnreal::CreatePropertyTrackReader(Prim, UEPropertyName, bIgnorePrimLocalTransform);
		}

		if (Reader.BoolReader)
		{
			return ReadBools(Impl.UsdStage, TimeSampleUnion, Reader.BoolReader, OutPayloadData);
		}
		else if (Reader.ColorReader)
		{
			return ReadColors(Impl.UsdStage, TimeSampleUnion, Reader.ColorReader, OutPayloadData);
		}
		else if (Reader.FloatReader)
		{
			return ReadFloats(Impl.UsdStage, TimeSampleUnion, Reader.FloatReader, OutPayloadData);
		}
		else if (Reader.TransformReader)
		{
			return ReadTransforms(Impl.UsdStage, TimeSampleUnion, Reader.TransformReader, OutPayloadData);
		}

		return false;
	}

	bool GetTransformAnimationPayloadData(
		const UInterchangeUSDTranslatorImpl& Impl,
		const FString& PayloadKey,
		Interchange::FAnimationPayloadData& OutPayloadData
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetTransformAnimationPayloadData)

		FString PrimPath;
		FString UEPropertyNameStr;
		bool bSplit = PayloadKey.Split(TEXT("\\"), &PrimPath, &UEPropertyNameStr, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (!bSplit)
		{
			return false;
		}

		UE::FUsdPrim Prim = Impl.UsdStage.GetPrimAtPath(UE::FSdfPath{*PrimPath});
		FName UEPropertyName = *UEPropertyNameStr;
		if (!Prim || UEPropertyName == NAME_None)
		{
			return false;
		}

		TArray<double> TimeSampleUnion;

		TArray<UE::FUsdAttribute> Attrs = UsdUtils::GetAttributesForProperty(Prim, UEPropertyName);
		bool bSuccess = UE::FUsdAttribute::GetUnionedTimeSamples(Attrs, TimeSampleUnion);
		if (!bSuccess)
		{
			return false;
		}

		// Use the first timecode as an offset when indexing
		if (!TimeSampleUnion.IsEmpty())
		{
			OutPayloadData.RangeStartTime = TimeSampleUnion[0];
		}

		const bool bIgnorePrimLocalTransform = false;
		UsdToUnreal::FPropertyTrackReader Reader = UsdToUnreal::CreatePropertyTrackReader(Prim, UEPropertyName, bIgnorePrimLocalTransform);
		if (Reader.TransformReader)
		{
			return ReadRawTransforms(Impl.UsdStage, TimeSampleUnion, Reader.TransformReader, OutPayloadData);
		}

		return false;
	}

	bool GetJointAnimationCurvePayloadData(
		const UInterchangeUSDTranslatorImpl& Impl,
		const TArray<const UE::Interchange::FAnimationPayloadQuery*>& Queries,
		TArray<UE::Interchange::FAnimationPayloadData>& OutPayloadData
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetJointAnimationCurvePayloadData)

		if (Queries.Num() == 0)
		{
			return false;
		}

		// We expect all queries to be for the same skeleton, and have the same timing parameters,
		// since they were grouped up by HashAnimPayloadQuery, so let's just grab one for the params
		const UE::Interchange::FAnimationPayloadQuery* FirstQuery = Queries[0];

		// Parse payload key.
		// Here is takes the form "<skeleton prim path>\<joint index in skeleton order>"
		TArray<FString> PayloadKeyTokens;
		FirstQuery->PayloadKey.UniqueId.ParseIntoArray(PayloadKeyTokens, TEXT("\\"));
		if (PayloadKeyTokens.Num() != 2)
		{
			return false;
		}

		// Fetch our cached skeleton query
		const FString& SkeletonPrimPath = PayloadKeyTokens[0];
		UE::FUsdSkelSkeletonQuery SkelQuery;
		{
			FReadScopeLock ReadLock{Impl.CachedTraversalInfoLock};

			const FTraversalInfo* Info = Impl.NodeUidToCachedTraversalInfo.Find(MakeNodeUid(SkeletonPrimPath));
			if (!Info)
			{
				return false;
			}

			SkelQuery = Info->ResolveSkelQuery(Impl.UsdStage);
			if (!SkelQuery)
			{
				return false;
			}
		}

		UE::FUsdPrim SkeletonPrim = SkelQuery.GetPrim();
		UE::FUsdStage Stage = SkeletonPrim.GetStage();
		FUsdStageInfo StageInfo{Stage};

		// Compute the bake ranges and intervals
		double TimeCodesPerSecond = Stage.GetTimeCodesPerSecond();
		double BakeFrequency = FirstQuery->TimeDescription.BakeFrequency;
		double RangeStartSeconds = FirstQuery->TimeDescription.RangeStartSecond;
		double RangeStopSeconds = FirstQuery->TimeDescription.RangeStopSecond;
		double SectionLengthSeconds = RangeStopSeconds - RangeStartSeconds;
		double StartTimeCode = RangeStartSeconds * TimeCodesPerSecond;
		const int32 NumBakedFrames = FMath::RoundToInt(FMath::Max(SectionLengthSeconds * TimeCodesPerSecond + 1.0, 1.0));
		double TimeCodeIncrement = (1.0 / BakeFrequency) * TimeCodesPerSecond;

		// Bake all joint transforms via USD into arrays for each separate joint (in whatever order SkelQuery gives us)
		TArray<TArray<FTransform>> BakedTransforms;
		for (int32 FrameIndex = 0; FrameIndex < NumBakedFrames; ++FrameIndex)
		{
			const double FrameTimeCode = StartTimeCode + FrameIndex * TimeCodeIncrement;

			TArray<FTransform> TransformsForTimeCode;
			bool bSuccess = SkelQuery.ComputeJointLocalTransforms(TransformsForTimeCode, FrameTimeCode);
			if (!bSuccess)
			{
				break;
			}

			for (FTransform& Transform : TransformsForTimeCode)
			{
				Transform = UsdUtils::ConvertTransformToUESpace(StageInfo, Transform);
			}

			// Setup our BakedTransforms in here, because we may actually get more or less transforms
			// from the SkeletonQuery than our AnimSequence wants/expects, given that it can specify
			// its own animated joint order
			int32 NumSkelJoints = TransformsForTimeCode.Num();
			if (FrameIndex == 0)
			{
				BakedTransforms.SetNum(NumSkelJoints);
				for (int32 JointIndex = 0; JointIndex < NumSkelJoints; ++JointIndex)
				{
					BakedTransforms[JointIndex].SetNum(NumBakedFrames);
				}
			}

			// Transpose our baked transforms into the arrays we'll eventually return
			for (int32 JointIndex = 0; JointIndex < NumSkelJoints; ++JointIndex)
			{
				BakedTransforms[JointIndex][FrameIndex] = TransformsForTimeCode[JointIndex];
			}
		}

		// Finally build our payload data return values by picking the desired baked arrays with the payload joint indices
		OutPayloadData.Reset(Queries.Num());
		for (int32 QueryIndex = 0; QueryIndex < Queries.Num(); ++QueryIndex)
		{
			const UE::Interchange::FAnimationPayloadQuery* Query = Queries[QueryIndex];

			const FString IndexStr = Query->PayloadKey.UniqueId.RightChop(SkeletonPrimPath.Len() + 1);	// Also skip the '\'
			int32 JointIndex = INDEX_NONE;
			bool bLexed = LexTryParseString(JointIndex, *IndexStr);
			if (!bLexed)
			{
				continue;
			}

			UE::Interchange::FAnimationPayloadData& PayloadData = OutPayloadData.Emplace_GetRef(Query->SceneNodeUniqueID, Query->PayloadKey);
			PayloadData.BakeFrequency = BakeFrequency;
			PayloadData.RangeStartTime = RangeStartSeconds;
			PayloadData.RangeEndTime = RangeStopSeconds;

			if (BakedTransforms.IsValidIndex(JointIndex))
			{
				PayloadData.Transforms = MoveTemp(BakedTransforms[JointIndex]);
			}
		}

		return true;
	}

	bool GetMorphTargetAnimationCurvePayloadData(
		const UInterchangeUSDTranslatorImpl& Impl,
		const FString& PayloadKey,
		Interchange::FAnimationPayloadData& OutPayloadData
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GetMorphTargetAnimationCurvePayloadData)

		// Here we must output the morph target curve for a particular channel and skinning target, i.e.
		// the connection of a SkelAnimation blend shape channel to a particular Mesh prim.

		// These payload keys were generated from GetMorphTargetCurvePayloadKey(), so they take the form
		// "<skeleton prim path>\<skel anim channel index>\<blend shape path>"
		TArray<FString> PayloadKeyTokens;
		PayloadKey.ParseIntoArray(PayloadKeyTokens, TEXT("\\"));
		if (PayloadKeyTokens.Num() != 3)
		{
			return false;
		}
		const FString& SkeletonPrimPath = PayloadKeyTokens[0];
		const FString& AnimChannelIndexStr = PayloadKeyTokens[1];
		const FString& BlendShapePath = PayloadKeyTokens[2];

		const UE::FUsdStage& UsdStage = Impl.UsdStage;

		int32 SkelAnimChannelIndex = INDEX_NONE;
		bool bLexed = LexTryParseString(SkelAnimChannelIndex, *AnimChannelIndexStr);

		UE::FUsdPrim BlendShapePrim = UsdStage.GetPrimAtPath(UE::FSdfPath{*BlendShapePath});
		UE::FUsdSkelBlendShape BlendShape{BlendShapePrim};
		if (!BlendShape || !bLexed || SkelAnimChannelIndex == INDEX_NONE)
		{
			return false;
		}
		const FString BlendShapeName = BlendShapePrim.GetName().ToString();

		// Fill in the actual morph target curve
		UE::FUsdSkelAnimQuery AnimQuery;
		{
			UE::FUsdSkelSkeletonQuery SkelQuery;
			{
				FReadScopeLock ReadLock{Impl.CachedTraversalInfoLock};

				const FTraversalInfo* Info = Impl.NodeUidToCachedTraversalInfo.Find(MakeNodeUid(SkeletonPrimPath));
				if (!Info)
				{
					return false;
				}

				SkelQuery = Info->ResolveSkelQuery(Impl.UsdStage);
				if (!SkelQuery)
				{
					return false;
				}
			}

			AnimQuery = SkelQuery.GetAnimQuery();
			if (!AnimQuery)
			{
				return false;
			}

			TArray<double> TimeCodes;
			bool bSuccess = AnimQuery.GetBlendShapeWeightTimeSamples(TimeCodes);
			if (!bSuccess)
			{
				return false;
			}

			OutPayloadData.Curves.SetNum(1);
			FRichCurve& Curve = OutPayloadData.Curves[0];
			Curve.ReserveKeys(TimeCodes.Num());

			const FFrameRate StageFrameRate{static_cast<uint32>(UsdStage.GetTimeCodesPerSecond()), 1};
			const ERichCurveInterpMode InterpMode = (UsdStage.GetInterpolationType() == EUsdInterpolationType::Linear)
														? ERichCurveInterpMode::RCIM_Linear
														: ERichCurveInterpMode::RCIM_Constant;

			TArray<float> Weights;
			for (double TimeCode : TimeCodes)
			{
				bSuccess = AnimQuery.ComputeBlendShapeWeights(Weights, TimeCode);
				if (!bSuccess || !Weights.IsValidIndex(SkelAnimChannelIndex))
				{
					break;
				}

				int32 FrameNumber = FMath::FloorToInt(TimeCode);
				float SubFrameNumber = TimeCode - FrameNumber;

				FFrameTime FrameTime{FrameNumber, SubFrameNumber};
				double FrameTimeSeconds = static_cast<float>(StageFrameRate.AsSeconds(FrameTime));

				FKeyHandle Handle = Curve.AddKey(FrameTimeSeconds, Weights[SkelAnimChannelIndex]);
				Curve.SetKeyInterpMode(Handle, InterpMode);
			}
		}

		TArray<FString> SkelAnimChannels = AnimQuery.GetBlendShapeOrder();

		// Provide inbetween names/positions for this morph target payload
		TArray<UE::FUsdSkelInbetweenShape> Inbetweens = BlendShape.GetInbetweens();
		if (Inbetweens.Num() > 0)
		{
			// Let's store them into this temp struct so that we can sort them by weight first,
			// as Interchange seems to expect that given how it will pass these right along into
			// ResolveWeightsForBlendShape inside InterchangeAnimSequenceFactory.cpp
			struct FInbetweenAndPosition
			{
				FString Name;
				float Position;
			};
			TArray<FInbetweenAndPosition> ParsedInbetweens;
			ParsedInbetweens.Reset(Inbetweens.Num());

			for (const UE::FUsdSkelInbetweenShape& Inbetween : Inbetweens)
			{
				float Position = 0.5f;
				bool bSuccess = Inbetween.GetWeight(&Position);
				if (!bSuccess)
				{
					continue;
				}

				// Skip invalid positions. Note that technically positions outside the [0, 1] range seem to be allowed, but
				// they don't seem to work very well with our inbetween weights resolution function for some reason.
				// The legacy USD workflows have this exact same check though, so for consistency let's just do the same, and
				// if becomes an issue we should fix both
				if (Position > 1.0f || Position < 0.0f || FMath::IsNearlyZero(Position) || FMath::IsNearlyEqual(Position, 1.0f))
				{
					continue;
				}

				const FString MorphTargetName = BlendShapeName + TEXT("_") + Inbetween.GetAttr().GetName().ToString();
				FInbetweenAndPosition& NewEntry = ParsedInbetweens.Emplace_GetRef();
				NewEntry.Name = MorphTargetName;
				NewEntry.Position = Position;
			}

			ParsedInbetweens.Sort(
				[](const FInbetweenAndPosition& LHS, const FInbetweenAndPosition& RHS)
				{
					// It's invalid USD to author two inbetweens with the same weight, so let's ignore that case here.
					// (Reference: https://openusd.org/release/api/_usd_skel__schemas.html#UsdSkel_BlendShape)
					return LHS.Position < RHS.Position;
				}
			);

			OutPayloadData.InbetweenCurveNames.Reset(Inbetweens.Num() + 1);
			OutPayloadData.InbetweenFullWeights.Reset(Inbetweens.Num());

			// We add the main morph target curve name to InbetweenCurveNames too (having it end up one size bigger than
			// InbetweenFullWeights) as it seems like that's what Interchange expects. See CreateMorphTargetCurve within
			// InterchangeAnimSequenceFactory.cpp, and the very end of FFbxMesh::AddAllMeshes within FbxMesh.cpp
			OutPayloadData.InbetweenCurveNames.Add(BlendShapeName);

			for (const FInbetweenAndPosition& InbetweenAndPosition : ParsedInbetweens)
			{
				OutPayloadData.InbetweenCurveNames.Add(InbetweenAndPosition.Name);
				OutPayloadData.InbetweenFullWeights.Add(InbetweenAndPosition.Position);
			}
		}

		return true;
	}

	void ProcessExtraInformation(UInterchangeBaseNodeContainer& NodeContainer, UE::FUsdStage Stage)
	{
		UInterchangeSourceNode* SourceNode = UInterchangeSourceNode::FindOrCreateUniqueInstance(&NodeContainer);

		TMap<FString, FString> MetadataMap;
		UsdUtils::ReadStageMetaData(Stage, MetadataMap);

		for (TPair<FString, FString>& MetaDataEntry : MetadataMap)
		{
			SourceNode->SetExtraInformation(MetaDataEntry.Key, MetaDataEntry.Value);
		}
	}

#else
	class UInterchangeUSDTranslatorImpl
	{
	};
#endif	  // USE_USD_SDK
}	 // namespace UE::InterchangeUsdTranslator::Private

UInterchangeUsdTranslatorSettings::UInterchangeUsdTranslatorSettings()
	: GeometryPurpose((int32)(EUsdPurpose::Default | EUsdPurpose::Proxy | EUsdPurpose::Render | EUsdPurpose::Guide))
	, RenderContext(UnrealIdentifiers::UnrealRenderContext)
	, MaterialPurpose(*UnrealIdentifiers::MaterialPreviewPurpose)
	, InterpolationType(EUsdInterpolationType::Linear)
	, bOverrideStageOptions(false)
	, StageOptions{
		  0.01,				   // MetersPerUnit
		  EUsdUpAxis::ZAxis	   // UpAxis
	  }
	, bTranslatePrimAttributes(false) // False by default as it could be expensive to traverse all attributes of all prims running the regex
	, AttributeRegexFilter(TEXT("."))
	, bTranslatePrimMetadata(false)
	, MetadataRegexFilter(TEXT("."))
{
}

UInterchangeUSDTranslator::UInterchangeUSDTranslator()
	: Impl(MakeUnique<UE::InterchangeUsdTranslator::Private::UInterchangeUSDTranslatorImpl>())
{
}

EInterchangeTranslatorType UInterchangeUSDTranslator::GetTranslatorType() const
{
	return GInterchangeEnableUSDLevelImport ? EInterchangeTranslatorType::Scenes : EInterchangeTranslatorType::Assets;
}

EInterchangeTranslatorAssetType UInterchangeUSDTranslator::GetSupportedAssetTypes() const
{
	return EInterchangeTranslatorAssetType::Materials | EInterchangeTranslatorAssetType::Meshes | EInterchangeTranslatorAssetType::Animations | EInterchangeTranslatorAssetType::Grooms;
}

TArray<FString> UInterchangeUSDTranslator::GetSupportedFormats() const
{
	TArray<FString> Extensions;
	if (GInterchangeEnableUSDImport)
	{
		if (IsInGameThread())
		{
			// ensure that MaterialX material functions are loaded in the Game Thread
			UE::Interchange::MaterialX::AreMaterialFunctionPackagesLoaded();
		}
		FModuleManager::Get().LoadModuleChecked(TEXT("UnrealUSDWrapper"));
		UnrealUSDWrapper::AddUsdImportFileFormatDescriptions(Extensions);
	}
	return Extensions;
}

bool UInterchangeUSDTranslator::Translate(UInterchangeBaseNodeContainer& NodeContainer) const
{
#if USE_USD_SDK
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeUSDTranslator::Translate)

	using namespace UE;
	using namespace UE::InterchangeUsdTranslator::Private;
	using namespace UsdToUnreal;

	// Reset impl as we don't want to share internal state with a previous translation, as the file
	// (or our settings) may have changed, which should lead to different nodes / data
	Impl = MakeUnique<UE::InterchangeUsdTranslator::Private::UInterchangeUSDTranslatorImpl>();
	UInterchangeUSDTranslatorImpl* ImplPtr = Impl.Get();
	if (!ImplPtr)
	{
		return false;
	}
	ImplPtr->ResultsContainer = Results;

	UInterchangeUsdTranslatorSettings* Settings = Cast<UInterchangeUsdTranslatorSettings>(GetSettings());
	if (!Settings)
	{
		return false;
	}

	if (!SourceData)
	{
		return false;
	}

	// Setup context
	UObject* ContextObject = SourceData->GetContextObjectByTag(UE::Interchange::USD::USDContextTag);
	UInterchangeUsdContext* Context = Cast<UInterchangeUsdContext>(ContextObject);
	if (!Context)
	{
		Context = NewObject<UInterchangeUsdContext>();

		ensureMsgf(
			!ContextObject,
			TEXT("Invalid ContextObject with tag '%s' will be removed and replaced with an UInterchangeUsdContext object"),
			*UE::Interchange::USD::USDContextTag
		);
		SourceData->SetContextObjectByTag(UE::Interchange::USD::USDContextTag, Context);
	}

	// Setup stage
	FString USDZFilePath;
	FString DecompressedUSDZRoot;
	UE::FUsdStage StageToImport = Context->GetUsdStage();
	{
		// Context didn't provide a stage: Try loading one from the provided file path
		if (!StageToImport)
		{
			FString FilePath = SourceData->GetFilename();
			if (!FPaths::FileExists(FilePath))
			{
				return false;
			}

			// If we're provided a USDZ path, for now we will decompress it into a temp dir and redirect our paths.
			//
			// This is mainly because the texture factories must receive a simple file path in order to produce the
			// their payloads. It's not practical to make them handle USDZ files, and it's not yet possible
			// to provide them with raw binary buffers directly either
			if (DecompressUSDZFileToTempFolder(FilePath, DecompressedUSDZRoot))
			{
				USDZFilePath = FilePath;
				FilePath = DecompressedUSDZRoot;
			}

			// Import should always feel like it's directly from disk, so we ignore already loaded layers and stage cache
			const bool bUseStageCache = false;
			const bool bForceReloadLayersFromDisk = true;
			StageToImport = UnrealUSDWrapper::OpenStage(*FilePath, EUsdInitialLoadSet::LoadAll, bUseStageCache, bForceReloadLayersFromDisk);
		}

		if (!StageToImport)
		{
			UInterchangeResultError_Generic* Message = ImplPtr->ResultsContainer->Add<UInterchangeResultError_Generic>();
			Message->SourceAssetName = SourceData->GetFilename();
			Message->Text = FText::Format(
				LOCTEXT("FailedToOpenStage", "Failed to get a valid stage from file '{0}'. Does the file exist and is it a valid USD file?"),
				FText::FromString(SourceData->GetFilename())
			);

			return false;
		}

		if (Settings)
		{
			// Apply coordinate system conversion to the stage if we have one
			if (Settings->bOverrideStageOptions)
			{
				UsdUtils::SetUsdStageMetersPerUnit(StageToImport, Settings->StageOptions.MetersPerUnit);
				UsdUtils::SetUsdStageUpAxis(StageToImport, Settings->StageOptions.UpAxis);
			}

			StageToImport.SetInterpolationType(Settings->InterpolationType);
		}

		ProcessExtraInformation(NodeContainer, StageToImport);
	}

	// Setup info cache
	FUsdInfoCache* InfoCache = Context->GetInfoCache();
	if (!InfoCache)
	{
		InfoCache = Context->CreateOwnedInfoCache();
	}

	// Fill in our context with our potentially-created-on-demand stage and info cache
	ensure(Context->SetUsdStage(StageToImport));
	Context->SetExternalInfoCache(*InfoCache);

	// Setup impl
	ImplPtr->UsdStage = StageToImport;
	ImplPtr->InfoCache = InfoCache;
	ImplPtr->SetupTranslationContext(*Settings);
	ImplPtr->TranslationContext->UsdInfoCache = ImplPtr->InfoCache;
	ImplPtr->CurrentTrackSet = nullptr;
	ImplPtr->USDZFilePath = USDZFilePath;
	ImplPtr->DecompressedUSDZRoot = DecompressedUSDZRoot;

	// Cache these so we don't have to keep converting these tokens over and over during translation
	{
		FUsdMeshConversionOptions& MeshOptions = ImplPtr->CachedMeshConversionOptions;

		// We filter for this on the pipeline now
		MeshOptions.PurposesToLoad = EUsdPurpose::Default | EUsdPurpose::Proxy | EUsdPurpose::Render | EUsdPurpose::Guide;

		// TODO: Change FUsdMeshConversionOptions to not hold USD types directly, so we don't have to the conversion below everywhere.
		// We can't use UsdToUnreal::ConvertToken() here because it returns a TUsdStore, and the template instantiation created in this module doesn't
		// really do anything anyway as the module doesn't use IMPLEMENT_MODULE_USD!
		// Luckily we can get around this here because pxr::TfToken doesn't allocate on its own: At most USD makes a copy of the string, which it
		// should allocate/deallocate on its own allocator.
		MeshOptions.RenderContext = Settings->RenderContext == UnrealIdentifiers::UniversalRenderContext
										? pxr::UsdShadeTokens->universalRenderContext
										: pxr::TfToken{TCHAR_TO_UTF8(*Settings->RenderContext.ToString())};
		MeshOptions.MaterialPurpose = Settings->MaterialPurpose.IsNone() ? pxr::UsdShadeTokens->allPurpose
																		 : pxr::TfToken{TCHAR_TO_UTF8(*Settings->MaterialPurpose.ToString())};
	}

	// Traverse stage and emit translated nodes
	{
		ImplPtr->InfoCache->RebuildCacheForSubtrees({UE::FSdfPath::AbsoluteRootPath()}, *ImplPtr->TranslationContext);

		FTraversalInfo Info;
		Traverse(ImplPtr->UsdStage.GetPseudoRoot(), *ImplPtr, NodeContainer, TranslatorSettings, Info);
	}

	return true;
#else
	return false;
#endif	  // USE_USD_SDK
}

void UInterchangeUSDTranslator::ImportFinish()
{
	// These objects are only set by Translate so they are released here instead of ReleaseSource
	if (TranslatorSettings)
	{
		TranslatorSettings->ClearFlags(RF_Standalone);
		TranslatorSettings = nullptr;
	}

#if USE_USD_SDK
	// The USD context is caching the stage so it must be released once the import is done
	UInterchangeUsdContext* ContextObject = Cast<UInterchangeUsdContext>(SourceData->GetContextObjectByTag(UE::Interchange::USD::USDContextTag));
	if (ContextObject)
	{
		ContextObject->Reset();
	}

	UE::InterchangeUsdTranslator::Private::UInterchangeUSDTranslatorImpl* ImplPtr = Impl.Get();
	if (!ImplPtr)
	{
		return;
	}

	ImplPtr->TranslationContext.Reset();	// This holds a reference to the stage
	ImplPtr->UsdStage = UE::FUsdStage{};
	ImplPtr->InfoCache = nullptr;
	ImplPtr->CurrentTrackSet = nullptr;

	{
		FWriteScopeLock Lock{ ImplPtr->PrimPathToVariantToStageLock };
		ImplPtr->PrimPathToVariantToStage.Reset();
	}

	ImplPtr->CleanUpDecompressedUSDZFolder();
#endif	  // USE_USD_SDK
}

UInterchangeTranslatorSettings* UInterchangeUSDTranslator::GetSettings() const
{
	using namespace UE::InterchangeUsdTranslator::Private;

	if (!TranslatorSettings)
	{
		TranslatorSettings = DuplicateObject<UInterchangeUsdTranslatorSettings>(
			UInterchangeUsdTranslatorSettings::StaticClass()->GetDefaultObject<UInterchangeUsdTranslatorSettings>(),
			GetTransientPackage()
		);
		TranslatorSettings->LoadSettings();
		TranslatorSettings->ClearFlags(RF_ArchetypeObject);
		TranslatorSettings->SetFlags(RF_Standalone);
		TranslatorSettings->ClearInternalFlags(EInternalObjectFlags::Async);
	}
	return TranslatorSettings;
}

void UInterchangeUSDTranslator::SetSettings(const UInterchangeTranslatorSettings* InterchangeTranslatorSettings)
{
	using namespace UE::InterchangeUsdTranslator::Private;

	if (TranslatorSettings)
	{
		TranslatorSettings->ClearFlags(RF_Standalone);
		TranslatorSettings->ClearInternalFlags(EInternalObjectFlags::Async);
		TranslatorSettings = nullptr;
	}
	if (const UInterchangeUsdTranslatorSettings* USDTranslatorSettings = Cast<UInterchangeUsdTranslatorSettings>(InterchangeTranslatorSettings))
	{
		TranslatorSettings = DuplicateObject<UInterchangeUsdTranslatorSettings>(USDTranslatorSettings, GetTransientPackage());
		TranslatorSettings->ClearInternalFlags(EInternalObjectFlags::Async);
		TranslatorSettings->SetFlags(RF_Standalone);
	}
}

TOptional<UE::Interchange::FMeshPayloadData> UInterchangeUSDTranslator::GetMeshPayloadData(
	const FInterchangeMeshPayLoadKey& PayloadKey,
	const UE::Interchange::FAttributeStorage& PayloadAttributes
) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeUSDTranslator::GetMeshPayloadData)

	using namespace UE::InterchangeUsdTranslator::Private;
	bool bSuccess = false;
	TOptional<UE::Interchange::FMeshPayloadData> Result;
#if USE_USD_SDK
	UInterchangeUSDTranslatorImpl* ImplPtr = Impl.Get();
	if (!ImplPtr)
	{
		return Result;
	}
	using namespace UE::Interchange;
	using namespace UsdUtils;
	using namespace UsdToUnreal;

	UsdToUnreal::FUsdMeshConversionOptions OptionsCopy = ImplPtr->CachedMeshConversionOptions;
	OptionsCopy.bMergeIdenticalMaterialSlots = false;	 // This must always be false, because we need the material assignments we read from the
														 // meshes to match up with whatever we cached from AddMeshNode, in order to fixup LOD
														 // material slots

	static_assert(uint8(EInterchangeUsdPrimvar::All) == FUsdMeshConversionOptions::EImportPrimvar::All, "FUsdMeshConversionOptions::EImportPrimvar::All is different from EInterchangeUsdPrimvar::All");
	static_assert(uint8(EInterchangeUsdPrimvar::Bake) == FUsdMeshConversionOptions::EImportPrimvar::Bake, "FUsdMeshConversionOptions::EImportPrimvar::Bake is different from EInterchangeUsdPrimvar::Bake");
	static_assert(uint8(EInterchangeUsdPrimvar::Standard) == FUsdMeshConversionOptions::EImportPrimvar::Standard, "FUsdMeshConversionOptions::EImportPrimvar::Standard is different from EInterchangeUsdPrimvar::Standard");

	PayloadAttributes.GetAttribute(UE::Interchange::FAttributeKey{ USD::SubdivisionLevelAttributeKey }, OptionsCopy.SubdivisionLevel);
	PayloadAttributes.GetAttribute(UE::Interchange::FAttributeKey{ USD::Primvar::Import }, reinterpret_cast<int32&>(OptionsCopy.ImportPrimvars));

	if (EInterchangeUsdPrimvar(OptionsCopy.ImportPrimvars) != EInterchangeUsdPrimvar::Standard)
	{
		if (int32 PrimvarNumber; PayloadAttributes.GetAttribute(UE::Interchange::FAttributeKey{ USD::Primvar::Number }, PrimvarNumber) == EAttributeStorageResult::Operation_Success)
		{
			OptionsCopy.PrimvarNames.Reserve(PrimvarNumber);
			for (int32 Index = 0; Index < PrimvarNumber; ++Index)
			{
				if (FString PrimvarName; PayloadAttributes.GetAttribute(UE::Interchange::FAttributeKey{ USD::Primvar::Name + FString::FromInt(Index) }, PrimvarName) == EAttributeStorageResult::Operation_Success)
				{
					OptionsCopy.PrimvarNames.Emplace(MoveTemp(PrimvarName));
				}
			}
		}
	}

	UE::Interchange::FMeshPayloadData MeshPayloadData;
	switch (PayloadKey.Type)
	{
		case EInterchangeMeshPayLoadType::STATIC:
		{
			PayloadAttributes.GetAttribute(UE::Interchange::FAttributeKey{ MeshPayload::Attributes::MeshGlobalTransform }, OptionsCopy.AdditionalTransform);

			bSuccess = UE::InterchangeUsdTranslator::Private::GetStaticMeshPayloadData(
				PayloadKey.UniqueId,
				*ImplPtr,
				OptionsCopy,
				MeshPayloadData.MeshDescription,
				MeshPayloadData.NaniteAssemblyDescription
			);
			break;
		}
		case EInterchangeMeshPayLoadType::SKELETAL:
		{
			// Don't use MeshGlobalTransform here as that will be the scene transform of our Mesh prims, which is not relevant for USD skinning.
			// With baking, we want to first apply geomBindTransform, and then apply the skeleton's localToWorld transform. ConvertGeomMesh can 
			// sort out the geomBindTransform (which should always be applied), so here we set the baking transform to the skeleton prim's transform if needed
			bool bBakeMeshes = false;
			FTransform GlobalOffsetTransform;
			PayloadAttributes.GetAttribute(UE::Interchange::FAttributeKey{MeshPayload::Attributes::BakeMeshes}, bBakeMeshes);
			PayloadAttributes.GetAttribute(UE::Interchange::FAttributeKey{MeshPayload::Attributes::GlobalOffsetTransform}, GlobalOffsetTransform);
			
			// The skeletal mesh factory will already remap our bones by name from RetrieveAllSkeletalMeshPayloads() anyway,
			// so we shouldn't offset anything ourselves
			OptionsCopy.bOffsetJointIndicesForMultipleRootBones = false;

			bSuccess = UE::InterchangeUsdTranslator::Private::GetSkeletalMeshPayloadData(
				PayloadKey.UniqueId,
				bBakeMeshes,
				GlobalOffsetTransform,
				*ImplPtr,
				OptionsCopy,
				MeshPayloadData.MeshDescription,
				MeshPayloadData.JointNames,
				MeshPayloadData.NaniteAssemblyDescription
			);
			break;
		}
		case EInterchangeMeshPayLoadType::MORPHTARGET:
		{
			// See the ::SKELETAL case
			bool bBakeMeshes = false;
			FTransform GlobalOffsetTransform;
			PayloadAttributes.GetAttribute(UE::Interchange::FAttributeKey{MeshPayload::Attributes::BakeMeshes}, bBakeMeshes);
			PayloadAttributes.GetAttribute(UE::Interchange::FAttributeKey{MeshPayload::Attributes::GlobalOffsetTransform}, GlobalOffsetTransform);

			bSuccess = UE::InterchangeUsdTranslator::Private::GetMorphTargetPayloadData(
				PayloadKey.UniqueId,
				bBakeMeshes,
				GlobalOffsetTransform,
				*ImplPtr,
				OptionsCopy,
				MeshPayloadData.MeshDescription,
				MeshPayloadData.MorphTargetName
			);
			break;
		}
		case EInterchangeMeshPayLoadType::ANIMATED:	   // Geometry caches
		{
			PayloadAttributes.GetAttribute(UE::Interchange::FAttributeKey{ MeshPayload::Attributes::MeshGlobalTransform }, OptionsCopy.AdditionalTransform);

			OptionsCopy.TimeCode = PayloadKey.FrameNumber;

			bSuccess = UE::InterchangeUsdTranslator::Private::GetStaticMeshPayloadData(
				PayloadKey.UniqueId,
				*ImplPtr,
				OptionsCopy,
				MeshPayloadData.MeshDescription,
				MeshPayloadData.NaniteAssemblyDescription
			);
			break;
		}
		case EInterchangeMeshPayLoadType::NONE:	   // Fallthrough
		default:
			break;
	}

	if (bSuccess)
	{
		Result.Emplace(MeshPayloadData);
	}
#endif	  // USE_USD_SDK
	return Result;
}

TOptional<UE::Interchange::FImportImage> UInterchangeUSDTranslator::GetTexturePayloadData(
	const FString& PayloadKey,
	TOptional<FString>& AlternateTexturePath
) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeUSDTranslator::GetTexturePayloadData)

	using namespace UE::InterchangeUsdTranslator::Private;

	TOptional<UE::Interchange::FImportImage> TexturePayloadData;
#if USE_USD_SDK

	UInterchangeUSDTranslatorImpl* ImplPtr = Impl.Get();
	if (!ImplPtr)
	{
		return TexturePayloadData;
	}

	FString FilePath;
	TextureGroup TextureGroup;
	AlternateTexturePath = ImplPtr->GetTextureSourcePath(FilePath);
	bool bDecoded = DecodeTexturePayloadKey(PayloadKey, FilePath, TextureGroup);
	if (bDecoded)
	{
		// Defer back to another translator to actually parse the texture raw data
		//
		// Note: For DomeLights even though we produce texture cube nodes, we'll rely on a common texture payload here (i.e. not sliced),
		// as Unreal handles only latlong / equirectangular, single slice .HDRs anyway. Via the .dds format you can provide sliced payloads
		// for texture cubes, but we do not handle that via USD for now
		UE::Interchange::Private::FScopedTranslator ScopedTranslator(FilePath, Results, AnalyticsHelper);
		const IInterchangeTexturePayloadInterface* TextureTranslator = ScopedTranslator.GetPayLoadInterface<IInterchangeTexturePayloadInterface>();
		if (TextureTranslator)
		{
			// The texture translators don't use the payload key, and read the texture directly from the SourceData's file path
			const FString UnusedPayloadKey = {};
			TexturePayloadData = TextureTranslator->GetTexturePayloadData(UnusedPayloadKey, AlternateTexturePath);

			// The TextureTranslator doesn't know how we plan on using this texture: If we know it's meant to be a normal map,
			// then override the compression settings here. Leave the rest untouched as the TextureTranslator is going to set
			// that according to the file itself
			if (TextureGroup == TEXTUREGROUP_WorldNormalMap)
			{
				TexturePayloadData->CompressionSettings = TC_Normalmap;
			}
		}
	}

	// We did not find a suitable Payload in USD Translator, let's find one in one of the Translators (MaterialX for the moment)
	// The best way would be to have a direct association between the payload and the right Translator, but we don't have a suitable way of knowing
	// which Payload belongs to which Translator So let's just loop over them all
	if (!TexturePayloadData.IsSet())
	{
		for (const TPair<FString, TStrongObjectPtr<UInterchangeTranslatorBase>>& Pair : Impl->Translators)
		{
			if (IInterchangeTexturePayloadInterface* TexturePayloadInterface = Cast<IInterchangeTexturePayloadInterface>(Pair.Value.Get()))
			{
				TexturePayloadData = TexturePayloadInterface->GetTexturePayloadData(PayloadKey, AlternateTexturePath);
				if (TexturePayloadData.IsSet())
				{
					break;
				}
			}
		}
	}

	// If we couldn't find a texture in either the USD Translator nor the Translators, then it's most likely coming from reading an mtlx in memory
	if (!TexturePayloadData.IsSet())
	{
		TexturePayloadData = UInterchangeMaterialXTranslator::GetTexturePayloadData(PayloadKey, AlternateTexturePath, Results, AnalyticsHelper);
	}

#endif	  // USE_USD_SDK
	return TexturePayloadData;
}

TOptional<UE::Interchange::FImportBlockedImage> UInterchangeUSDTranslator::GetBlockedTexturePayloadData(
	const FString& PayloadKey,
	TOptional<FString>& AlternateTexturePath
) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeUSDTranslator::GetBlockedTexturePayloadData)

	using namespace UE::InterchangeUsdTranslator::Private;

	UE::Interchange::FImportBlockedImage BlockData;

#if USE_USD_SDK
	UInterchangeUSDTranslatorImpl* ImplPtr = Impl.Get();
	if (!ImplPtr)
	{
		return {};
	}

	FString FilePath;
	TextureGroup TextureGroup;
	AlternateTexturePath = ImplPtr->GetTextureSourcePath(FilePath);
	bool bDecoded = DecodeTexturePayloadKey(PayloadKey, FilePath, TextureGroup);
	if (!bDecoded)
	{
		TOptional<UE::Interchange::FImportBlockedImage> BlockedTexturePayloadData;
		// We did not find a suitable Payload in USD Translator, let's find one in one of the Translators (MaterialX for the moment)
		// The best way would be to have a direct association between the payload and the right Translator, but we don't have a suitable way of knowing
		// which Payload belongs to which Translator So let's just loop over them all
		for (const TPair<FString, TStrongObjectPtr<UInterchangeTranslatorBase>>& Pair : Impl->Translators)
		{
			if (IInterchangeBlockedTexturePayloadInterface* TexturePayloadInterface = Cast<IInterchangeBlockedTexturePayloadInterface>(Pair.Value.Get()))
			{
				BlockedTexturePayloadData = TexturePayloadInterface->GetBlockedTexturePayloadData(PayloadKey, AlternateTexturePath);
				if (BlockedTexturePayloadData.IsSet())
				{
					break;
				}
			}
		}

		// If we couldn't find a texture in either the USD Translator nor the Translators, then it's most likely coming from reading an mtlx in memory
		if (!BlockedTexturePayloadData.IsSet())
		{
			BlockedTexturePayloadData = UInterchangeMaterialXTranslator::GetBlockedTexturePayloadData(PayloadKey, AlternateTexturePath, Results, AnalyticsHelper);
		}

		return BlockedTexturePayloadData;
	}


	// Collect all the UDIM tile filepaths similar to this current tile. If we've been asked to translate
	// a blocked texture then we must have some
	TMap<int32, FString> TileIndexToPath = UE::TextureUtilitiesCommon::GetUDIMBlocksFromSourceFile(
		FilePath,
		UE::TextureUtilitiesCommon::DefaultUdimRegexPattern
	);
	if (!ensure(TileIndexToPath.Num() > 0))
	{
		return {};
	}

	bool bInitializedBlockData = false;

	TArray<UE::Interchange::FImportImage> TileImages;
	TileImages.Reserve(TileIndexToPath.Num());

	for (const TPair<int32, FString>& TileIndexAndPath : TileIndexToPath)
	{
		int32 UdimTile = TileIndexAndPath.Key;
		const FString& TileFilePath = TileIndexAndPath.Value;

		int32 BlockX = INDEX_NONE;
		int32 BlockY = INDEX_NONE;
		UE::TextureUtilitiesCommon::ExtractUDIMCoordinates(UdimTile, BlockX, BlockY);
		if (BlockX == INDEX_NONE || BlockY == INDEX_NONE)
		{
			continue;
		}

		// Find another translator that actually supports that filetype to handle the texture
		UE::Interchange::Private::FScopedTranslator ScopedTranslator(TileFilePath, Results, AnalyticsHelper);
		const IInterchangeTexturePayloadInterface* TextureTranslator = ScopedTranslator.GetPayLoadInterface<IInterchangeTexturePayloadInterface>();
		if (!ensure(TextureTranslator))
		{
			continue;
		}

		// Invoke the translator to actually load the texture and parse it
		const FString UnusedPayloadKey = {};
		TOptional<UE::Interchange::FImportImage> TexturePayloadData;
		TexturePayloadData = TextureTranslator->GetTexturePayloadData(UnusedPayloadKey, AlternateTexturePath);
		if (!TexturePayloadData)
		{
			continue;
		}
		const UE::Interchange::FImportImage& Image = TileImages.Emplace_GetRef(MoveTemp(TexturePayloadData.GetValue()));
		TexturePayloadData.Reset();

		// Initialize the settings on the BlockData itself based on the first image we parse
		if (!bInitializedBlockData)
		{
			bInitializedBlockData = true;

			BlockData.Format = Image.Format;
			BlockData.CompressionSettings = TextureGroup == TEXTUREGROUP_WorldNormalMap ? TC_Normalmap : TC_Default;
			BlockData.bSRGB = Image.bSRGB;
			BlockData.MipGenSettings = Image.MipGenSettings;
		}

		// Prepare the BlockData to receive this image data (later)
		BlockData.InitBlockFromImage(BlockX, BlockY, Image);
	}

	// Move all of the FImportImage buffers into the BlockData itself
	BlockData.MigrateDataFromImagesToRawData(TileImages);
#endif	  // USE_USD_SDK

	return BlockData;
}

TArray<UE::Interchange::FAnimationPayloadData> UInterchangeUSDTranslator::GetAnimationPayloadData(
	const TArray<UE::Interchange::FAnimationPayloadQuery>& PayloadQueries
) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeUSDTranslator::GetAnimationPayloadData)

	using namespace UE::Interchange;
	using namespace UE::InterchangeUsdTranslator::Private;
	// This is the results we return
	TArray<UE::Interchange::FAnimationPayloadData> AnimationPayloads;

#if USE_USD_SDK
	// Maps to help sorting the queries by payload type
	TArray<int32> BakeQueryIndexes;
	TArray<TArray<UE::Interchange::FAnimationPayloadData>> BakeAnimationPayloads;
	TArray<int32> CurveQueryIndexes;
	TArray<TArray<UE::Interchange::FAnimationPayloadData>> CurveAnimationPayloads;
	TArray<int32> TransformQueryIndexes;
	TArray<TArray<UE::Interchange::FAnimationPayloadData>> TransformAnimationPayloads;

	// Get all curves with a parallel for
	int32 PayloadCount = PayloadQueries.Num();
	for (int32 PayloadIndex = 0; PayloadIndex < PayloadCount; ++PayloadIndex)
	{
		const UE::Interchange::FAnimationPayloadQuery& PayloadQuery = PayloadQueries[PayloadIndex];
		EInterchangeAnimationPayLoadType QueryType = PayloadQuery.PayloadKey.Type;
		if (QueryType == EInterchangeAnimationPayLoadType::BAKED)
		{
			BakeQueryIndexes.Add(PayloadIndex);
		}
		else if (QueryType == EInterchangeAnimationPayLoadType::GEOMETRY_CACHE_TRANSFORM)
		{
			TransformQueryIndexes.Add(PayloadIndex);
		}
		else
		{
			CurveQueryIndexes.Add(PayloadIndex);
		}
	}

	// Import the Baked curve payloads
	if (BakeQueryIndexes.Num() > 0)
	{
		int32 BakePayloadCount = BakeQueryIndexes.Num();
		TMap<FString, TArray<const UE::Interchange::FAnimationPayloadQuery*>> BatchedBakeQueries;
		BatchedBakeQueries.Reserve(BakePayloadCount);

		// Get the BAKED transform synchronously, since there is some interchange task that parallel them
		for (int32 BakePayloadIndex = 0; BakePayloadIndex < BakePayloadCount; ++BakePayloadIndex)
		{
			if (!ensure(BakeQueryIndexes.IsValidIndex(BakePayloadIndex)))
			{
				continue;
			}
			int32 PayloadIndex = BakeQueryIndexes[BakePayloadIndex];
			if (!PayloadQueries.IsValidIndex(PayloadIndex))
			{
				continue;
			}
			const UE::Interchange::FAnimationPayloadQuery& PayloadQuery = PayloadQueries[PayloadIndex];
			check(PayloadQuery.PayloadKey.Type == EInterchangeAnimationPayLoadType::BAKED);
			// Joint transform animation queries.
			//
			// Currently we'll receive the PayloadQueries for all joints of a skeletal animation on the same GetAnimationPayloadData
			// call. Unfortunately in USD we must compute all joint transforms every time, even if all we need is data for a single
			// joint. For efficiency then, we group up all the queries for the separate joints of the same skeleton into one batch
			// task that we can resolve in one pass
			const FString BakedQueryHash = HashAnimPayloadQuery(PayloadQuery);
			TArray<const UE::Interchange::FAnimationPayloadQuery*>& Queries = BatchedBakeQueries.FindOrAdd(BakedQueryHash);
			Queries.Add(&PayloadQuery);
		}
		// Emit the batched joint transform animation tasks
		for (const TPair<FString, TArray<const UE::Interchange::FAnimationPayloadQuery*>>& BatchedBakedQueryPair : BatchedBakeQueries)
		{
			const TArray<const UE::Interchange::FAnimationPayloadQuery*>& Queries = BatchedBakedQueryPair.Value;
			TArray<UE::Interchange::FAnimationPayloadData> Result;
			GetJointAnimationCurvePayloadData(*Impl, Queries, Result);
			BakeAnimationPayloads.Add(Result);
		}

		// Append the bake curves results
		for (TArray<UE::Interchange::FAnimationPayloadData>& AnimationPayload : BakeAnimationPayloads)
		{
			AnimationPayloads.Append(AnimationPayload);
		}
	}

	// Import normal curves
	if (CurveQueryIndexes.Num() > 0)
	{
		auto GetAnimPayloadLambda = [this, &PayloadQueries, &CurveAnimationPayloads](int32 PayloadIndex)
		{
			if (!PayloadQueries.IsValidIndex(PayloadIndex))
			{
				return;
			}
			const UE::Interchange::FAnimationPayloadQuery& PayloadQuery = PayloadQueries[PayloadIndex];
			EInterchangeAnimationPayLoadType PayloadType = PayloadQuery.PayloadKey.Type;
			if (PayloadType == EInterchangeAnimationPayLoadType::CURVE || PayloadType == EInterchangeAnimationPayLoadType::STEPCURVE)
			{
				// Property track animation queries.
				//
				// We're fine handling these in isolation (currently GetAnimationPayloadData is called with
				// a single query at a time for these): Emit a separate task for each right away
				FAnimationPayloadData AnimationPayLoadData{PayloadQuery.SceneNodeUniqueID, PayloadQuery.PayloadKey};
				if (GetPropertyAnimationCurvePayloadData(*Impl, PayloadQuery.PayloadKey.UniqueId, AnimationPayLoadData))
				{
					CurveAnimationPayloads[PayloadIndex].Emplace(AnimationPayLoadData);
				}
			}
			else if (PayloadType == EInterchangeAnimationPayLoadType::MORPHTARGETCURVE)
			{
				// Morph target curve queries.
				FAnimationPayloadData AnimationPayLoadData{PayloadQuery.SceneNodeUniqueID, PayloadQuery.PayloadKey};
				if (GetMorphTargetAnimationCurvePayloadData(*Impl, PayloadQuery.PayloadKey.UniqueId, AnimationPayLoadData))
				{
					CurveAnimationPayloads[PayloadIndex].Emplace(AnimationPayLoadData);
				}
			}
		};

		// Get all curves with a parallel for if there is many
		int32 CurvePayloadCount = CurveQueryIndexes.Num();
		CurveAnimationPayloads.AddDefaulted(CurvePayloadCount);
		const int32 BatchSize = 10;
		if (CurvePayloadCount > BatchSize)
		{
			const int32 NumBatches = (CurvePayloadCount / BatchSize) + 1;
			ParallelFor(
				NumBatches,
				[&CurveQueryIndexes, &GetAnimPayloadLambda](int32 BatchIndex)
				{
					int32 PayloadIndexOffset = BatchIndex * BatchSize;
					for (int32 PayloadIndex = PayloadIndexOffset; PayloadIndex < PayloadIndexOffset + BatchSize; ++PayloadIndex)
					{
						// The last batch can be incomplete
						if (!CurveQueryIndexes.IsValidIndex(PayloadIndex))
						{
							break;
						}
						GetAnimPayloadLambda(CurveQueryIndexes[PayloadIndex]);
					}
				},
				EParallelForFlags::BackgroundPriority	 // ParallelFor
			);
		}
		else
		{
			for (int32 PayloadIndex = 0; PayloadIndex < CurvePayloadCount; ++PayloadIndex)
			{
				int32 PayloadQueriesIndex = CurveQueryIndexes[PayloadIndex];
				if (PayloadQueries.IsValidIndex(PayloadQueriesIndex))
				{
					GetAnimPayloadLambda(PayloadQueriesIndex);
				}
			}
		}

		// Append the curves results
		for (TArray<UE::Interchange::FAnimationPayloadData>& AnimationPayload : CurveAnimationPayloads)
		{
			AnimationPayloads.Append(AnimationPayload);
		}
	}

	// Import raw transforms payloads
	if (int32 TransformPayloadCount = TransformQueryIndexes.Num(); TransformPayloadCount > 0)
	{
		TransformAnimationPayloads.Reserve(TransformPayloadCount);
		for (int32 QueryIndex : TransformQueryIndexes)
		{
			const UE::Interchange::FAnimationPayloadQuery& PayloadQuery = PayloadQueries[QueryIndex];
			FAnimationPayloadData AnimationPayLoadData{PayloadQuery.SceneNodeUniqueID, PayloadQuery.PayloadKey};
			if (GetTransformAnimationPayloadData(*Impl, PayloadQuery.PayloadKey.UniqueId, AnimationPayLoadData))
			{
				TransformAnimationPayloads.Emplace(TArray<UE::Interchange::FAnimationPayloadData>{AnimationPayLoadData});
			}
		}

		// Append the transforms results
		for (TArray<UE::Interchange::FAnimationPayloadData>& AnimationPayload : TransformAnimationPayloads)
		{
			AnimationPayloads.Append(AnimationPayload);
		}
	}
#endif	  // USE_USD_SDK

	return AnimationPayloads;
}

TOptional<UE::Interchange::FVolumePayloadData> UInterchangeUSDTranslator::GetVolumePayloadData(const UE::Interchange::FVolumePayloadKey& PayloadKey
) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeUSDTranslator::GetVolumePayloadData)

#if USE_USD_SDK
	TStrongObjectPtr<UInterchangeTranslatorBase> ExistingTranslator = Impl->Translators.FindRef(PayloadKey.FileName);
	const IInterchangeVolumePayloadInterface* VolumeInterface = Cast<IInterchangeVolumePayloadInterface>(ExistingTranslator.Get());
	if (VolumeInterface)
	{
		if (TOptional<UE::Interchange::FVolumePayloadData> Result = VolumeInterface->GetVolumePayloadData(PayloadKey))
		{
			// Bake our stage coordinate system transform into the volume, just like we do for geometry.
			// We do this here because OpenVDB doesn't really specify a fixed coordinate system, and so we interpret it as just meaning
			// "units" in general. This means we must go through a metersPerUnit / upAxis conversion to get it into UE's world space
			const FUsdStageInfo StageInfo{Impl->UsdStage};
			FTransform StageInfoTransform = UsdUtils::GetUsdToUESpaceTransform(StageInfo);
			Result.GetValue().Transform = Result->Transform * StageInfoTransform;

			return Result;
		}
	}
#endif	  // USE_USD_SDK

	return {};
}

TOptional<UE::Interchange::FGroomPayloadData> UInterchangeUSDTranslator::GetGroomPayloadData(const FInterchangeGroomPayloadKey& PayloadKey) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UInterchangeUSDTranslator::GetGroomPayloadData)

#if USE_USD_SDK
	const FString& PrimPath = PayloadKey.UniqueId;
	UE::FUsdPrim Prim = Impl->UsdStage.GetPrimAtPath(UE::FSdfPath{ *PrimPath });

	FGroomAnimationInfo AnimInfo;
	UE::Interchange::FGroomPayloadData PayloadData;
	const double TimeCode = PayloadKey.Type == EInterchangeGroomPayLoadType::ANIMATED ? PayloadKey.FrameNumber : UsdUtils::GetEarliestTimeCode();

	const bool bSuccess = UsdToUnreal::ConvertGroomHierarchy(
		Prim,
		TimeCode,
		FTransform::Identity,
		PayloadData.HairDescription,
		&AnimInfo
	);

	if (bSuccess && PayloadData.HairDescription.IsValid())
	{
		return PayloadData;
	}
#endif // USE_USD_SDK

	return {};
}

#undef LOCTEXT_NAMESPACE
