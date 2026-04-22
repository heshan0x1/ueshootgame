// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosClothAsset/USDImportNode_v2.h"
#include "Chaos/CollectionEmbeddedSpringConstraintFacade.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothCollectionGroup.h"
#include "ChaosClothAsset/ClothDataflowTools.h"
#include "ChaosClothAsset/ClothGeometryTools.h"
#include "ChaosClothAsset/CollectionClothFacade.h"
#include "ChaosClothAsset/CollectionClothSelectionFacade.h"
#include "ChaosClothAsset/RenderMeshImport.h"
#include "ChaosClothAsset/USDImportNode.h"
#include "ChaosClothAsset/UsdPrimAttributeAccessor.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Chaos/CollectionPropertyFacade.h"
#include "Dataflow/DataflowInputOutput.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "StaticMeshAttributes.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"
#include "UObject/Package.h"
#include "UnrealUSDWrapper.h"
#include "USDAssetUserData.h"
#include "USDConversionUtils.h"
#include "USDProjectSettings.h"
#include "USDStageImportContext.h"
#include "USDStageImporter.h"
#include "USDStageImportOptions.h"
#include "USDTypesConversion.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdGeomSubset.h"
#include "UsdWrappers/UsdRelationship.h"
#include "UsdWrappers/VtValue.h"
#include "AssetViewUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(USDImportNode_v2)

#define LOCTEXT_NAMESPACE "ChaosClothAssetUSDImportNode_v2"

namespace UE::Chaos::ClothAsset::Private
{
	// User attribute names
	static const FName OriginalIndicesName(TEXT("OriginalIndices"));

	// Cloth USD API names
	static const FName ClothRootAPI(TEXT("ClothRootAPI"));
	static const FName RenderPatternAPI(TEXT("RenderPatternAPI"));
	static const FName SimMeshDataAPI(TEXT("SimMeshDataAPI"));
	static const FName SimPatternAPI(TEXT("SimPatternAPI"));
	static const FName SewingAPI(TEXT("SewingAPI"));
	static const FName SpringAPI(TEXT("SpringAPI"));
	static const FName CloSolverPropertiesAPI(TEXT("CloSolverPropertiesAPI"));

	// USD import material overrides
	static TArray<FSoftObjectPath> UsdClothOverrideMaterials_v2 =
	{
		FSoftObjectPath(TEXT("/ChaosClothAsset/Materials/USDImportMaterial.USDImportMaterial")),
		FSoftObjectPath(TEXT("/ChaosClothAsset/Materials/USDImportTranslucentMaterial.USDImportTranslucentMaterial")),
		FSoftObjectPath(TEXT("/ChaosClothAsset/Materials/USDImportTwoSidedMaterial.USDImportTwoSidedMaterial")),
		FSoftObjectPath(TEXT("/ChaosClothAsset/Materials/USDImportTranslucentTwoSidedMaterial.USDImportTranslucentTwoSidedMaterial")),
		FSoftObjectPath(TEXT("/ChaosClothAsset/Materials/USDImportDisplayColorMaterial.USDImportDisplayColorMaterial")),
	};

	// Return the specified UObject's dependencies for top level UAssets that are not in the engine folders
	static TArray<UObject*> GetAssetDependencies(const UObject* Asset)
	{
		constexpr bool bInRequireDirectOuter = true;
		constexpr bool bShouldIgnoreArchetype = true;
		constexpr bool bInSerializeRecursively = false;  // Ignored if LimitOuter is nullptr
		constexpr bool bShouldIgnoreTransient = true;
		constexpr UObject* LimitOuter = nullptr;
		TArray<UObject*> References;
		FReferenceFinder ReferenceFinder(References, LimitOuter, bInRequireDirectOuter, bShouldIgnoreArchetype, bInSerializeRecursively, bShouldIgnoreTransient);
		ReferenceFinder.FindReferences(const_cast<UObject*>(Asset));

		TArray<UObject*> Dependencies;
		Dependencies.Reserve(References.Num());
		for (UObject* const Reference : References)
		{
			constexpr bool bEnginePluginIsAlsoEngine = true;  // Only includes non Engine or non Engine plugins assets (e.g. no USD materials)
			if (FAssetData::IsUAsset(Reference) &&
				FAssetData::IsTopLevelAsset(Reference) &&
				!AssetViewUtils::IsEngineFolder(Reference->GetPackage()->GetName(), bEnginePluginIsAlsoEngine))
			{
				Dependencies.Emplace(Reference);
			}
		}
		return Dependencies;
	}

	static TArray<TObjectPtr<UObject>> GetImportedAssetDependencies(const UObject* StaticMesh)
	{
		using namespace UE::Chaos::ClothAsset::Private;

		TSet<TObjectPtr<UObject>> ImportedAssets;
		if (StaticMesh)
		{
			TQueue<const UObject*> AssetsToVisit;
			AssetsToVisit.Enqueue(StaticMesh);

			const UObject* VisitedAsset;
			while (AssetsToVisit.Dequeue(VisitedAsset))
			{
				const FName VisitedAssetPackageName(VisitedAsset->GetPackage()->GetName());
				const TArray<UObject*> AssetDependencies = GetAssetDependencies(VisitedAsset);

				UE_CLOG(AssetDependencies.Num(), LogChaosClothAssetDataflowNodes, Verbose, TEXT("Dependencies for Object %s - %s:"), *VisitedAsset->GetName(), *VisitedAssetPackageName.ToString());
				for (UObject* const AssetDependency : AssetDependencies)
				{
					if (!ImportedAssets.Contains(AssetDependency))
					{
						// Add the dependency
						UE_LOG(LogChaosClothAssetDataflowNodes, Verbose, TEXT("Found %s"), *AssetDependency->GetPackage()->GetName());
						ImportedAssets.Emplace(AssetDependency);

						// Visit this asset too
						AssetsToVisit.Enqueue(AssetDependency);
					}
				}
			}
		}
		return ImportedAssets.Array();
	}

	static void OverrideUsdImportMaterials_v2(const TArray<FSoftObjectPath>& Materials, TArray<FSoftObjectPath>* SavedValues = nullptr)
	{
		if (UUsdProjectSettings* UsdProjectSettings = GetMutableDefault<UUsdProjectSettings>())
		{
			// Check to see if we should save the existing values
			if (SavedValues)
			{
				SavedValues->Push(UsdProjectSettings->ReferencePreviewSurfaceMaterial);
				SavedValues->Push(UsdProjectSettings->ReferencePreviewSurfaceTranslucentMaterial);
				SavedValues->Push(UsdProjectSettings->ReferencePreviewSurfaceTwoSidedMaterial);
				SavedValues->Push(UsdProjectSettings->ReferencePreviewSurfaceTranslucentTwoSidedMaterial);
				SavedValues->Push(UsdProjectSettings->ReferenceDisplayColorMaterial);
			}
			UsdProjectSettings->ReferencePreviewSurfaceMaterial = Materials[0];
			UsdProjectSettings->ReferencePreviewSurfaceTranslucentMaterial = Materials[1];
			UsdProjectSettings->ReferencePreviewSurfaceTwoSidedMaterial = Materials[2];
			UsdProjectSettings->ReferencePreviewSurfaceTranslucentTwoSidedMaterial = Materials[3];
			UsdProjectSettings->ReferenceDisplayColorMaterial = Materials[4];
		}
	}

	static TArray<TObjectPtr<UObject>> ImportStaticMeshesFromUsdStage(const FUsdStage& UsdStage, const FString& UsdFilePath, const FString& PackagePath)
	{
		// Import recognised assets
		FUsdStageImportContext ImportContext;

		const TObjectPtr<UUsdStageImportOptions>& ImportOptions = ImportContext.ImportOptions;
		{
			check(ImportOptions);
			// Data to import
			ImportOptions->bImportActors = false;
			ImportOptions->bImportGeometry = true;
			ImportOptions->bImportSkeletalAnimations = false;
			ImportOptions->bImportLevelSequences = false;
			ImportOptions->bImportMaterials = true;
			ImportOptions->bImportGroomAssets = false;
			ImportOptions->bImportOnlyUsedMaterials = true;
			// Prims to import
			ImportOptions->PrimsToImport = TArray<FString>{ TEXT("/") };
			// USD options
			ImportOptions->PurposesToImport = (int32)(EUsdPurpose::Render | EUsdPurpose::Guide);
			ImportOptions->NaniteTriangleThreshold = TNumericLimits<int32>::Max();  // Don't enable Nanite
			ImportOptions->RenderContextToImport = NAME_None;
			ImportOptions->MaterialPurpose = NAME_None;  // *UnrealIdentifiers::MaterialPreviewPurpose ???
			ImportOptions->RootMotionHandling = EUsdRootMotionHandling::NoAdditionalRootMotion;
			ImportOptions->SubdivisionLevel = 0;
			ImportOptions->bOverrideStageOptions = false;
			ImportOptions->bImportAtSpecificTimeCode = false;
			ImportOptions->ImportTimeCode = 0.f;
			// Groom
			ImportOptions->GroomInterpolationSettings = TArray<FHairGroupsInterpolation>();
			// Collision
			ImportOptions->ExistingActorPolicy = EReplaceActorPolicy::Replace;
			ImportOptions->ExistingAssetPolicy = EReplaceAssetPolicy::Replace;
			// Processing
			ImportOptions->bPrimPathFolderStructure = false;
			ImportOptions->KindsToCollapse = (int32)EUsdDefaultKind::Component;
			ImportOptions->bMergeIdenticalMaterialSlots = true;
			ImportOptions->bInterpretLODs = false;
		}

		constexpr bool bIsAutomated = true;
		constexpr bool bIsReimport = false;
		constexpr bool bAllowActorImport = false;

		ImportContext.Stage = UsdStage;  // Set the stage first to prevent re-opening it in the Init function
		ImportContext.Init(TEXT(""), UsdFilePath, PackagePath, RF_NoFlags, bIsAutomated, bIsReimport, bAllowActorImport);

		TArray<FSoftObjectPath> OriginalUsdMaterials;
		// Override the project settings to point the USD importer to cloth specific parent materials.
		// This is because we want the materials to import into UEFN and the default USD ones
		// use operations that are not allowed.
		OverrideUsdImportMaterials_v2(UsdClothOverrideMaterials_v2, &OriginalUsdMaterials);

		UUsdStageImporter UsdStageImporter;
		UsdStageImporter.ImportFromFile(ImportContext);

		// Restore Original USD Materials
		OverrideUsdImportMaterials_v2(OriginalUsdMaterials);

		return ImportContext.ImportedAssets;
	}

	static TObjectPtr<UStaticMesh> FindImportedStaticMesh(const TArrayView<TObjectPtr<UObject>> ImportedAssets, const FString& MeshPrimPath)
	{
		for (TObjectPtr<UObject>& ImportedAsset : ImportedAssets)
		{
			if (const TObjectPtr<UStaticMesh> ImportedStaticMesh = Cast<UStaticMesh>(ImportedAsset))
			{
				if (const UUsdMeshAssetUserData* const AssetUserData = Cast<UUsdMeshAssetUserData>(
					ImportedStaticMesh->GetAssetUserDataOfClass(UUsdMeshAssetUserData::StaticClass())))
				{
					if (AssetUserData->PrimPaths.Find(MeshPrimPath) != INDEX_NONE)
					{
						return ImportedStaticMesh;
					}
				}
			}
		}
		return nullptr;
	}

	static FUsdPrim FindAPIPrim(const FUsdPrim& RootPrim, const FName APIName)
	{
		for (FUsdPrim& ChildPrim : RootPrim.GetChildren())
		{
			if (ChildPrim.HasAPI(APIName))
			{
				return ChildPrim;
			}
		}
		return FUsdPrim();
	}

	static bool RemoveMaterialOpacity(const FUsdPrim& Prim)
	{
		bool bHasOpacity = false;
		for (const FUsdPrim& ChildPrim : Prim.GetChildren())
		{
			if (ChildPrim.IsA(TEXT("Material")))
			{
				for (const FUsdPrim& GrandChildPrim : ChildPrim.GetChildren())
				{
					if (GrandChildPrim.IsA(TEXT("Shader")))
					{
						if (const FUsdAttribute OpacityAttr = GrandChildPrim.GetAttribute(TEXT("inputs:opacity")))
						{
							OpacityAttr.ClearConnections();
							OpacityAttr.Clear();
							bHasOpacity = true;
						}
					}
				}
			}
			else
			{
				bHasOpacity = RemoveMaterialOpacity(ChildPrim) || bHasOpacity;
			}
		}
		return bHasOpacity;
	}

	static FUsdPrim FindSimMeshPrim(const FUsdPrim& ClothPrim)
	{
		for (FUsdPrim& ClothChildPrim : ClothPrim.GetChildren())
		{
			if (ClothChildPrim.IsA(TEXT("Mesh")))
			{
				if (ClothChildPrim.HasAPI(SimMeshDataAPI))
				{
					// Check that the sim mesh has at least one valid geomsubset patern
					for (FUsdPrim& SimMeshChildPrim : ClothChildPrim.GetChildren())
					{
						if (SimMeshChildPrim.IsA(TEXT("GeomSubset")) && SimMeshChildPrim.HasAPI(SimPatternAPI))
						{
							return ClothChildPrim;
						}
					}
				}
			}
		}
		return FUsdPrim();
	}

	static FUsdPrim FindRenderMeshPrim(const FUsdPrim& ClothPrim)
	{
		for (FUsdPrim& ClothChildPrim : ClothPrim.GetChildren())
		{
			if (ClothChildPrim.IsA(TEXT("Mesh")))
			{
				// Look for all GeomSubsets to see if this is a suitable render mesh prim
				for (FUsdPrim& RenderMeshChildPrim : ClothChildPrim.GetChildren())
				{
					if (RenderMeshChildPrim.IsA(TEXT("GeomSubset")) && RenderMeshChildPrim.HasAPI(RenderPatternAPI))
					{
						return ClothChildPrim;
					}
				}
			}
		}
		return FUsdPrim();
	}

	static TArray<FUsdPrim> FindRenderMeshPrims(const FUsdPrim& ClothPrim)
	{
		TArray<FUsdPrim> RenderMeshPrims;
		const TArray<FUsdPrim> ClothChildPrims = ClothPrim.GetChildren();
		RenderMeshPrims.Reserve(ClothChildPrims.Num());
		for (const FUsdPrim& ClothChildPrim : ClothChildPrims)
		{
			if (ClothChildPrim.IsA(TEXT("Mesh")))
			{
				// Look for all GeomSubsets to see if this is a suitable render mesh prim
				for (const FUsdPrim& RenderMeshChildPrim : ClothChildPrim.GetChildren())
				{
					if (RenderMeshChildPrim.IsA(TEXT("GeomSubset")) && RenderMeshChildPrim.HasAPI(RenderPatternAPI))
					{
						RenderMeshPrims.Emplace(ClothChildPrim);
						break;
					}
				}
			}
		}
		return RenderMeshPrims;
	}

	static FVector2f GetSimMeshUVScale(const FUsdPrim& SimMeshPrim)
	{
		FVector2f UVScale(1.f);
		const FUsdAttribute RestPositionScaleAttr = SimMeshPrim.GetAttribute(TEXT("restPositionScale"));
		if (RestPositionScaleAttr.HasValue() && RestPositionScaleAttr.GetTypeName() == TEXT("float2"))
		{
			FVtValue Value;
			RestPositionScaleAttr.Get(Value);
			UsdUtils::FConvertedVtValue ConvertedVtValue;
			if (UsdToUnreal::ConvertValue(Value, ConvertedVtValue) &&
				!ConvertedVtValue.bIsArrayValued && !ConvertedVtValue.bIsEmpty &&
				ConvertedVtValue.Entries.Num() == 1 && ConvertedVtValue.Entries[0].Num() == 2 && ConvertedVtValue.Entries[0][0].IsType<float>())
			{
				UVScale = FVector2f(
					ConvertedVtValue.Entries[0][0].Get<float>(),
					ConvertedVtValue.Entries[0][1].Get<float>());
			}
		}
		return UVScale;
	}

	static FString GetStringValue(const FUsdAttribute& UsdAttribute)
	{
		if (UsdAttribute.HasValue())
		{
			FVtValue Value;
			UsdAttribute.Get(Value);
			return UsdUtils::Stringify(Value);
		}
		return FString();
	}

	static TArray<int32> GetIntArrayValues(const FUsdAttribute& UsdAttribute)
	{
		using namespace UsdUtils;
		TArray<int32> IntArray;
		if (UsdAttribute.HasValue())
		{
			FVtValue Value;
			UsdAttribute.Get(Value);
			FConvertedVtValue ConvertedVtValue;
			if (UsdToUnreal::ConvertValue(Value, ConvertedVtValue) && ConvertedVtValue.bIsArrayValued && !ConvertedVtValue.bIsEmpty)
			{
				IntArray.Reserve(ConvertedVtValue.Entries.Num());
				for (const FConvertedVtValueEntry& ValueEntry : ConvertedVtValue.Entries)
				{
					IntArray.Emplace(ValueEntry[0].Get<int32>());
				}
			}
		}
		return MoveTemp(IntArray);
	}

	static bool CheckSimMeshPrimTriangles(const FUsdPrim& SimMeshPrim, FText& OutErrorText)
	{
		const FUsdAttribute FaceVertexCountsAttr = SimMeshPrim.GetAttribute(TEXT("faceVertexCounts"));
		if (!FaceVertexCountsAttr)
		{
			OutErrorText = LOCTEXT("MissingSimMeshFaceCountAttribute", "Missing simulation mesh faceVertexCounts attribute.");
		}
		else if (FaceVertexCountsAttr.GetTypeName() != TEXT("int[]"))
		{
			OutErrorText = LOCTEXT("WrongSimMeshFaceCountTypeName", "Wrong simulation mesh faceVertexCounts type name. Needs to be 'int[]'.");
		}
		else
		{
			bool bIsTriangleMesh = true;
			const TArray<int32> FaceVertexCounts = GetIntArrayValues(FaceVertexCountsAttr);
			for (int32 FaceVertexCount : FaceVertexCounts)
			{
				if (FaceVertexCount != 3)
				{
					OutErrorText = LOCTEXT("WrongSimMeshFaceCount", "Wrong simulation mesh face vertex count. The simulation mesh only supports '3' for triangles.");
					bIsTriangleMesh = false;
					break;
				}
			}
			return bIsTriangleMesh;
		}
		return false;
	}

	static bool GetPatternGeomSubsetsFromMeshPrim(const FUsdPrim& MeshPrim, const FName PatternAPI, TMap<FName, FUsdGeomSubset>& GeomSubsets, FText& OutErrorText)
	{
		GeomSubsets.Reset();
		for (const FUsdPrim& MeshChildPrim : MeshPrim.GetChildren())
		{
			if (MeshChildPrim.IsA(TEXT("GeomSubset")) && MeshChildPrim.HasAPI(PatternAPI))
			{
				if (GeomSubsets.Contains(MeshChildPrim.GetName()))
				{
					OutErrorText = FText::Format(LOCTEXT("DuplicatePatternGeomSubsetName", "Duplicate pattern name for GeomSubset '{0}'. The name needs to be unique."), FText::FromString(MeshChildPrim.GetPrimPath().GetString()));
					return false;
				}

				const FUsdGeomSubset GeomSubset(MeshChildPrim);

				// Read FamillyName
				const FUsdAttribute FamilyNameAttr = GeomSubset.GetFamilyNameAttr();
				if (GetStringValue(FamilyNameAttr) != TEXT("pattern"))
				{
					OutErrorText = FText::Format(LOCTEXT("WrongPatternGeomSubsetFamilyName", "Wrong pattern family name for GeomSubset '{0}'. Needs to be 'pattern'."), FText::FromString(MeshChildPrim.GetPrimPath().GetString()));
					return false;
				}

				// Read ElementTypeAttr
				const FUsdAttribute ElementTypeAttr = GeomSubset.GetElementTypeAttr();
				if (GetStringValue(ElementTypeAttr) != TEXT("face"))
				{
					OutErrorText = FText::Format(LOCTEXT("WrongPatternGeomSubsetType", "Wrong pattern type for GeomSubset '{0}'. Needs to be 'face'."), FText::FromString(MeshChildPrim.GetPrimPath().GetString()));
					return false;
				}

				// Read indices 
				const FUsdAttribute IndicesAttr = GeomSubset.GetIndicesAttr();
				if (IndicesAttr.GetTypeName() != TEXT("int[]"))
				{
					OutErrorText = FText::Format(LOCTEXT("WrongPatternGeomSubsetIndexType", "Wrong pattern index type for GeomSubset '{0}'. Needs to be 'int[]'."), FText::FromString(MeshChildPrim.GetPrimPath().GetString()));
					return false;
				}

				// Make a pattern name that won't collide between sim mesh and render mesh
				const FName PatternName(MeshChildPrim.GetParent().GetName().ToString() + TEXT("__") + MeshChildPrim.GetName().ToString());

				GeomSubsets.Emplace(PatternName, GeomSubset);
			}
		}
		return true;
	}

	static bool AddPatternsFromMeshPrim(const FUsdPrim& MeshPrim, const FName PatternAPI, TMap<FName, TSet<int32>>& Patterns, FText& OutErrorText, int32 Offset = 0)
	{
		TMap<FName, FUsdGeomSubset> GeomSubsets;
		if (GetPatternGeomSubsetsFromMeshPrim(MeshPrim, PatternAPI, GeomSubsets, OutErrorText))
		{
			for (const TPair<FName, FUsdGeomSubset>& GeomSubset : GeomSubsets)
			{
				// Read indices 
				const FUsdAttribute IndicesAttr = GeomSubset.Value.GetIndicesAttr();
				if (IndicesAttr.GetTypeName() != TEXT("int[]"))
				{
					OutErrorText = FText::Format(LOCTEXT("WrongPatternGeomSubsetIndexType", "Wrong pattern index type for GeomSubset '{0}'. Needs to be 'int[]'."), FText::FromString(GeomSubset.Value.GetPrim().GetPrimPath().GetString()));
					return false;
				}
				TArray<int32> Indices = GetIntArrayValues(IndicesAttr);
				if (Offset)
				{
					for (int32& Index : Indices)
					{
						Index += Offset;
					}
				}
				Patterns.Emplace(GeomSubset.Key, MoveTemp(Indices));
			}
			return true;
		}
		return false;
	}

	static bool ImportPatternsFromRenderMeshPrims(const TArray<FUsdPrim>& RenderMeshPrims, const FUsdPrim& SimMeshPrim, TMap<FName, TSet<int32>>& Patterns, TMap<FName, TSet<FName>>& RenderToSimPatterns, FText& OutErrorText)
	{
		Patterns.Reset();
		RenderToSimPatterns.Reset();

		const FSdfPath SimMeshPath = SimMeshPrim.GetPrimPath();
		int32 Offset = 0;
		for (const FUsdPrim& RenderMeshPrim : RenderMeshPrims)
		{
			// Import the patterns indices
			if (!AddPatternsFromMeshPrim(RenderMeshPrim, RenderPatternAPI, Patterns, OutErrorText, Offset))
			{
				return false;
			}
			// Add the sim mesh pattern relationships
			for (const FUsdPrim& MeshChildPrim : RenderMeshPrim.GetChildren())
			{
				if (MeshChildPrim.IsA(TEXT("GeomSubset")) && MeshChildPrim.HasAPI(RenderPatternAPI))
				{
					// Read simPattern relationship
					const FUsdRelationship Relationship = MeshChildPrim.GetRelationship(TEXT("simPattern"));
					TArray<FSdfPath> Targets;
					Relationship.GetTargets(Targets);

					// Add a new set of sim mesh patterns for this render pattern
					TSet<FName>& SimMeshPatterns = RenderToSimPatterns.Add(MeshChildPrim.GetName());

					// Add all sim mesh targets
					for (const FSdfPath& Target : Targets)
					{
						if (Target.GetParentPath() == SimMeshPath)
						{
							SimMeshPatterns.Emplace(Target.GetName());
						}
						else
						{
							OutErrorText = FText::Format(LOCTEXT("UnknownOrMultipleSimMesh", "Unknown or more than one simulation mesh found while getting simPattern relationship of render pattern '{0}'."), FText::FromString(MeshChildPrim.GetPrimPath().GetString()));
							return false;
						}
					}
				}
			}
			// Increment offset to match the indices in the collapsed (merged) mesh
			const FUsdAttribute FaceVertexCountsAttr = RenderMeshPrim.GetAttribute(TEXT("faceVertexCounts"));
			if (!FaceVertexCountsAttr)
			{
				OutErrorText = LOCTEXT("MissingRenderMeshFaceCountAttribute", "Missing render mesh faceVertexCounts attribute.");
				return false;
			}
			else if (FaceVertexCountsAttr.GetTypeName() != TEXT("int[]"))
			{
				OutErrorText = LOCTEXT("WrongRenderMeshFaceCountTypeName", "Wrong render mesh faceVertexCounts type name. Needs to be 'int[]'.");
				return false;
			}
			const TArray<int32> FaceVertexCounts = GetIntArrayValues(FaceVertexCountsAttr);
			Offset += FaceVertexCounts.Num();
		}
		return true;
	}

	static bool ImportPatternsFromSimMeshPrim(const FUsdPrim& SimMeshPrim, TMap<FName, TSet<int32>>& Patterns, FText& OutErrorText)
	{
		Patterns.Reset();
		return AddPatternsFromMeshPrim(SimMeshPrim, SimPatternAPI, Patterns, OutErrorText);
	}

	static bool ImportSewingsFromSimMeshPrim(const FUsdPrim& SimMeshPrim, TMap<FName, TSet<FIntVector2>>& Sewings, FText& OutErrorText)
	{
		Sewings.Reset();
		for (const FUsdPrim& SimMeshChildPrim : SimMeshPrim.GetChildren())
		{
			if (SimMeshChildPrim.IsA(TEXT("GeomSubset")) && SimMeshChildPrim.HasAPI(SewingAPI))
			{
				const FUsdGeomSubset GeomSubset(SimMeshChildPrim);

				// Read FamilyName
				const FUsdAttribute FamilyNameAttr = GeomSubset.GetFamilyNameAttr();
				if (GetStringValue(FamilyNameAttr) != TEXT("sewing"))
				{
					OutErrorText = LOCTEXT("WrongSewingGeomSubsetFamilyName", "Wrong sewing GeomSubset family name. Needs to be 'sewing'.");
					return false;
				}

				// Read ElementTypeAttr
				const FUsdAttribute ElementTypeAttr = GeomSubset.GetElementTypeAttr();
				if (GetStringValue(ElementTypeAttr) != TEXT("edge"))
				{
					OutErrorText = LOCTEXT("WrongSewingGeomSubsetType", "Wrong sewing GeomSubset type. Needs to be edge.");
					return false;
				}

				// Read indices 
				const FUsdAttribute IndicesAttr = GeomSubset.GetIndicesAttr();
				if (IndicesAttr.GetTypeName() != TEXT("int[]"))
				{
					OutErrorText = LOCTEXT("WrongSewingGeomSubsetIndexType", "Wrong sewing GeomSubset index type. Needs to be int[].");
					return false;
				}

				if (Sewings.Contains(SimMeshChildPrim.GetName()))
				{
					OutErrorText = LOCTEXT("DuplicateSewingGeomSubsetName", "Duplicate sewing GeomSubset name. The name needs to be unique.");
					return false;
				}

				const TArray<int32> IntArrayValues = GetIntArrayValues(IndicesAttr);
				const int32 NumStitches = IntArrayValues.Num() / 2;
				if (NumStitches * 2 != IntArrayValues.Num())
				{
					OutErrorText = LOCTEXT("OddSewingGeomSubsetIndices", "Odd number of indices for the sewing edges.");
					return false;
				}

				TSet<FIntVector2>& Stitches = Sewings.Emplace(SimMeshChildPrim.GetName());
				Stitches.Reserve(NumStitches);
				for (int32 Index = 0; Index < NumStitches; ++Index)
				{
					const int32 Index0 = IntArrayValues[Index * 2];
					const int32 Index1 = IntArrayValues[Index * 2 + 1];
					Stitches.Emplace(Index0 <= Index1 ? FIntVector2(Index0, Index1) : FIntVector2(Index1, Index0));
				}
			}
		}
		return true;
	}

	static bool ImportSpringsFromSimMeshPrim(
		const FUsdPrim& SimMeshPrim,
		FManagedArrayCollection& OutSimulationCollection,
		FText& OutErrorText)
	{
		using namespace ::Chaos::Softs;

		const TSharedRef<FManagedArrayCollection> SimulationCollection = MakeShared<FManagedArrayCollection>(MoveTemp(OutSimulationCollection));
		ON_SCOPE_EXIT
		{
			OutSimulationCollection = MoveTemp(*SimulationCollection);
		};

		TOptional<FEmbeddedSpringConstraintFacade> SpringConstraintFacade;
		TOptional<FCollectionPropertyMutableFacade> PropertyFacade;

		for (const FUsdPrim& SimMeshChildPrim : SimMeshPrim.GetChildren())
		{
			if (SimMeshChildPrim.IsA(TEXT("GeomSubset")) && SimMeshChildPrim.HasAPI(SpringAPI))
			{
				const FUsdGeomSubset GeomSubset(SimMeshChildPrim);

				// Read FamilyName
				const FUsdAttribute FamilyNameAttr = GeomSubset.GetFamilyNameAttr();
				if (GetStringValue(FamilyNameAttr) != TEXT("spring"))
				{
					OutErrorText = LOCTEXT("WrongSpringGeomSubsetFamilyName", "Wrong spring GeomSubset family name. Needs to be 'spring'.");
					return false;
				}

				// Read ElementTypeAttr
				const FUsdAttribute ElementTypeAttr = GeomSubset.GetElementTypeAttr();
				if (GetStringValue(ElementTypeAttr) != TEXT("edge"))
				{
					OutErrorText = LOCTEXT("WrongSpringGeomSubsetType", "Wrong spring GeomSubset type. Needs to be edge.");
					return false;
				}

				// Read indices 
				const FUsdAttribute IndicesAttr = GeomSubset.GetIndicesAttr();
				if (IndicesAttr.GetTypeName() != TEXT("int[]"))
				{
					OutErrorText = LOCTEXT("WrongSpringGeomSubsetIndexType", "Wrong spring GeomSubset index type. Needs to be int[].");
					return false;
				}

				const TArray<int32> IntArrayValues = GetIntArrayValues(IndicesAttr);
				const int32 NumSprings = IntArrayValues.Num() / 2;
				if (NumSprings * 2 != IntArrayValues.Num())
				{
					OutErrorText = LOCTEXT("OddSpringGeomSubsetIndices", "Odd number of indices for the spring edges.");
					return false;
				}

				if (NumSprings)
				{
					// Read the primvars
					FUsdPrimAttributeAccessor UsdPrimAttributeAccessor(SimMeshChildPrim);
					const TArray<float> RestLengths = UsdPrimAttributeAccessor.GetArray<float>(TEXT("primvars:restLength"));
					if (RestLengths.Num() != NumSprings)
					{
						OutErrorText = LOCTEXT("MistmatchedSpringGeomSubsetRestLengths", "The number of rest length values doesn't match the number of springs.");
						return false;
					}
					const TArray<float> CloSpringDamp = UsdPrimAttributeAccessor.GetArray<float>(TEXT("primvars:clo:springDamp"));
					const TArray<float> CloSpringStiffness = UsdPrimAttributeAccessor.GetArray<float>(TEXT("primvars:clo:springStiffness"));
					const bool bHasCloSpringPrimvars = CloSpringDamp.Num() && CloSpringStiffness.Num();  // Only use the first value of the array, that's how it is currently exported

					// Initialize the spring constraint facade
					if (!SpringConstraintFacade.IsSet())
					{
						FEmbeddedSpringFacade SpringFacade(*SimulationCollection, ClothCollectionGroup::SimVertices3D);
						checkf(SpringFacade.IsValid(), TEXT("FEmbeddedSpringFacade constructor should have defined the schema."));
						for (int32 ConstraintIndex = 0; ConstraintIndex < SpringFacade.GetNumSpringConstraints(); ++ConstraintIndex)
						{
							FEmbeddedSpringConstraintFacade TempSpringFacade = SpringFacade.GetSpringConstraint(ConstraintIndex);
							if (TempSpringFacade.GetConstraintEndPointNumIndices() == FUintVector2(1, 1) && TempSpringFacade.GetConstraintName() == TEXT("VertexSpringConstraint"))
							{
								SpringConstraintFacade.Emplace(MoveTemp(TempSpringFacade));
								break;
							}
						}
						if (!SpringConstraintFacade.IsSet())
						{
							SpringConstraintFacade.Emplace(SpringFacade.AddGetSpringConstraint());
							SpringConstraintFacade->Initialize(
								TConstArrayView<FIntVector2>(),
								TConstArrayView<float>(),
								TConstArrayView<float>(),
								TConstArrayView<float>(),
								TConstArrayView<float>(),
								TEXT("VertexSpringConstraint"));
						}
					}

					TArray<FIntVector2> ConstraintVertices;
					ConstraintVertices.Reserve(NumSprings);
					for (int32 Index = 0; Index < NumSprings; ++Index)
					{
						const int32 Index0 = IntArrayValues[Index * 2];
						const int32 Index1 = IntArrayValues[Index * 2 + 1];
						ConstraintVertices.Emplace(Index0 <= Index1 ? FIntVector2(Index0, Index1) : FIntVector2(Index1, Index0));
					}

					SpringConstraintFacade->Append(
						TConstArrayView<FIntVector2>(ConstraintVertices.GetData(), NumSprings),
						TConstArrayView<float>(RestLengths.GetData(), NumSprings));

					// Initialize the property facade
					if (bHasCloSpringPrimvars)
					{
						if (!PropertyFacade.IsSet())
						{
							PropertyFacade.Emplace(SimulationCollection);
							PropertyFacade->DefineSchema();
						}

						auto SetPropertyValue = [&PropertyFacade](const FName PropertyName, const float Value)
							{
								constexpr ECollectionPropertyFlags CollectionPropertyFlags = ECollectionPropertyFlags::Interpolable | ECollectionPropertyFlags::Animatable | ECollectionPropertyFlags::Enabled;
								int32 VertexSpringExtensionStiffnessIndex = PropertyFacade->GetKeyNameIndex(PropertyName);
								if (VertexSpringExtensionStiffnessIndex == INDEX_NONE)
								{
									VertexSpringExtensionStiffnessIndex = PropertyFacade->AddProperty(PropertyName, CollectionPropertyFlags);
								}
								PropertyFacade->SetValue(VertexSpringExtensionStiffnessIndex, Value);
							};

						SetPropertyValue("VertexSpringExtensionStiffness", CloSpringStiffness[0]);
						SetPropertyValue("VertexSpringCompressionStiffness", CloSpringStiffness[0]);
						SetPropertyValue("VertexSpringDamping", CloSpringDamp[0]);
					}
					break;  // Only does one springs geomesubset since that's how it is currently exported, additional ones would also require creating a new weight map (see fabric code)
				}
			}
		}
		return true;
	}

	static bool ImportSimulationProperties(
		const FUsdPrim& ClothPrim,
		const FUsdPrim& SimMeshPrim,
		const EUsdUpAxis UsdUpAxis,
		FManagedArrayCollection& OutSimulationCollection,
		TMap<FName, int32>& OutSimPatternFabricIndices,
		FText& OutErrorText)
	{
		const TSharedRef<FManagedArrayCollection> SimulationCollection = MakeShared<FManagedArrayCollection>(MoveTemp(OutSimulationCollection));
		ON_SCOPE_EXIT
		{
			OutSimulationCollection = MoveTemp(*SimulationCollection);
		};

		FCollectionClothFacade ClothFacade(SimulationCollection);
		ClothFacade.DefineSchema(EClothCollectionExtendedSchemas::Solvers | EClothCollectionExtendedSchemas::Fabrics);

		// Import the solver properties
		auto ImportSolverProperties = [&UsdUpAxis, &ClothFacade](const FUsdPrim& ParentPrim) -> bool
			{
				// Find the CloSolverPropertiesAPI prim
				const FUsdPrim CloSolverPropertiesPrim = FindAPIPrim(ParentPrim, CloSolverPropertiesAPI);
				if (!CloSolverPropertiesPrim)
				{
					return false;
				}

				// Read the attributes
				const FUsdPrimAttributeAccessor UsdPrimAttributeAccessor(CloSolverPropertiesPrim, UsdUpAxis);

				float AirDamping = 0.1f;
				AirDamping = UsdPrimAttributeAccessor.GetValue(TEXT("airDamping"), AirDamping);

				FVector3f Gravity(0.f, 0.f, -980.f);
				constexpr float CentimetersPerMillimeters = 0.1f;  // CloSolverPropertiesAPI Gravity is always given in mm regardless of UsdStageInfo
				Gravity = UsdPrimAttributeAccessor.GetValue(TEXT("gravity"), Gravity / CentimetersPerMillimeters) * CentimetersPerMillimeters;

				float TimeStep = 0.033333f;
				TimeStep = UsdPrimAttributeAccessor.GetValue(TEXT("timeStep"), TimeStep);

				uint32 SubStepCount = 1;
				SubStepCount = UsdPrimAttributeAccessor.GetValue(TEXT("subStepCount"), SubStepCount);

				ClothFacade.SetSolverGravity(Gravity);
				ClothFacade.SetSolverAirDamping(AirDamping);
				ClothFacade.SetSolverTimeStep(TimeStep);
				ClothFacade.SetSolverSubSteps(SubStepCount);

				return true;
			};

		if (!ImportSolverProperties(ClothPrim))  // Try under the cloth prim first
		{
			ImportSolverProperties(SimMeshPrim);  // If that doesn't work, try under the sim mesh prim instead
		}

		// Import the fabric properties
		auto ImportFabricProperties = [&UsdUpAxis, &ClothFacade](const FUsdGeomSubset& GeomSubset) -> int32
			{
				// Read the attributes
				const FUsdPrimAttributeAccessor UsdPrimAttributeAccessor(GeomSubset.GetPrim(), UsdUpAxis);

				constexpr float BendingScaling = 1e-5f;  // From g.mm2/s2 to kg.cm2/s2
				constexpr float StretchShearScaling = 1e-3f;  // From g/s2 to kg/s2
				constexpr float DensityScaling = 1e+3f;  // From g/mm2 to kg/m2
				constexpr float ThicknessScaling = 1e-1f;  // From mm to cm

				float BendingBiasLeft = 0.0f;
				BendingBiasLeft = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:bendingBiasLeft"), BendingBiasLeft) * BendingScaling;

				float BendingBiasRight = 0.0f;
				BendingBiasRight = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:bendingBiasRight"), BendingBiasRight) * BendingScaling;

				float BendingWarp = 0.0f;
				BendingWarp = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:bendingWarp"), BendingWarp) * BendingScaling;

				float BendingWeft = 0.0f;
				BendingWeft = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:bendingWeft"), BendingWeft) * BendingScaling;

				float BucklingRatioBiasLeft = 0.0f;
				BucklingRatioBiasLeft = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:bucklingRatioBiasLeft"), BucklingRatioBiasLeft);

				float BucklingRatioBiasRight = 0.0f;
				BucklingRatioBiasRight = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:bucklingRatioBiasRight"), BucklingRatioBiasRight);

				float BucklingRatioWarp = 0.0f;
				BucklingRatioWarp = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:bucklingRatioWarp"), BucklingRatioWarp);

				float BucklingRatioWeft = 0.0f;
				BucklingRatioWeft = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:bucklingRatioWeft"), BucklingRatioWeft);

				float BucklingStiffnessBiasLeft = 0.0f;
				BucklingStiffnessBiasLeft = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:bucklingStiffnessBiasLeft"), BucklingStiffnessBiasLeft);

				float BucklingStiffnessBiasRight = 0.0f;
				BucklingStiffnessBiasRight = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:bucklingStiffnessBiasRight"), BucklingStiffnessBiasRight);

				float BucklingStiffnessWarp = 0.0f;
				BucklingStiffnessWarp = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:bucklingStiffnessWarp"), BucklingStiffnessWarp);

				float BucklingStiffnessWeft = 0.0f;
				BucklingStiffnessWeft = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:bucklingStiffnessWeft"), BucklingStiffnessWeft);

				float Density = 0.0f;
				Density = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:density"), Density) * DensityScaling;

				float Friction = 0.0f;
				Friction = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:friction"), Friction);

				float Damping = 0.0f;
				Damping = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:internalDamping"), Damping);

				float ShearLeft = 0.0f;
				ShearLeft = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:shearLeft"), ShearLeft) * StretchShearScaling;

				float ShearRight = 0.0f;
				ShearRight = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:shearRight"), ShearRight) * StretchShearScaling;

				float StretchWarp = 0.0f;
				StretchWarp = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:stretchWarp"), StretchWarp) * StretchShearScaling;

				float StretchWeft = 0.0f;
				StretchWeft = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:stretchWeft"), StretchWeft) * StretchShearScaling;

				float Thickness = 0.0f;
				Thickness = UsdPrimAttributeAccessor.GetArrayValue<float>(TEXT("primvars:clo:thickness"), Thickness)* ThicknessScaling;

				// Initialize a new fabric with these parameters
				const FCollectionClothFabricFacade::FAnisotropicData BendingStiffness(
					BendingWeft,
					BendingWarp,
					0.5f * (BendingBiasLeft + BendingBiasRight));

				const FCollectionClothFabricFacade::FAnisotropicData StretchStiffness(
					StretchWeft,
					StretchWarp,
					0.5f * (ShearLeft + ShearRight));

				const float BucklingRatio =
					(BucklingRatioWeft + BucklingRatioWarp +
						0.5f * (BucklingRatioBiasLeft + BucklingRatioBiasRight)) / 3.f;

				const FCollectionClothFabricFacade::FAnisotropicData BucklingStiffness =
					BucklingRatio < UE_SMALL_NUMBER ? BendingStiffness :
					FCollectionClothFabricFacade::FAnisotropicData(
						BendingStiffness.Weft * BucklingStiffnessWeft,
						BendingStiffness.Warp * BucklingStiffnessWarp,
						BendingStiffness.Bias * 0.5f * (BucklingStiffnessBiasLeft + BucklingStiffnessBiasRight));

				const int32 FabricIndex = ClothFacade.AddFabric();
				FCollectionClothFabricFacade Fabric = ClothFacade.GetFabric(FabricIndex);

				Fabric.Initialize(
					BendingStiffness,
					BucklingRatio,
					BucklingStiffness,
					StretchStiffness,
					Density,
					Friction,
					Damping,
					0.0f,
					0,
					Thickness);

				return FabricIndex;
			};

		TMap<FName, FUsdGeomSubset> GeomSubsets;
		if (!GetPatternGeomSubsetsFromMeshPrim(SimMeshPrim, SimPatternAPI, GeomSubsets, OutErrorText))
		{
			return false;
		}

		for (const TPair<FName, FUsdGeomSubset>& GeomSubset : GeomSubsets)
		{
			// Setup a new fabric for this pattern
			const int32 FabricIndex = ImportFabricProperties(GeomSubset.Value);
			OutSimPatternFabricIndices.Emplace(GeomSubset.Key, FabricIndex);
		}
		return true;
	}
}  // End namespace Private

void FChaosClothAssetUsdClothData::Reset()
{
	SimPatterns.Reset();
	Sewings.Reset();
	RenderPatterns.Reset();
	RenderToSimPatterns.Reset();
	SimPatternFabricIndices.Reset();
	SimulationCollection.Reset();
}

bool FChaosClothAssetUsdClothData::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);

	Ar << SimPatterns;
	Ar << Sewings;
	Ar << RenderPatterns;
	Ar << RenderToSimPatterns;

	if (Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID) >= FUE5ReleaseStreamObjectVersion::AddSimulationPropertySupportToClothUSDImportNodeV2)
	{
		Ar << SimPatternFabricIndices;
		SimulationCollection.Serialize(Ar);
	}
	
	return true;
}

FChaosClothAssetUSDImportNode_v2::FChaosClothAssetUSDImportNode_v2(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid)
	: FDataflowNode(InParam, InGuid)
	, UsdFile(
		FDataflowFunctionProperty::FDelegate::CreateLambda([this, OwningObject = InParam.OwningObject](UE::Dataflow::FContext& /*Context*/)
			{
				const FString AssetPath = OwningObject ? OwningObject->GetPackage()->GetPathName() : FString();
				FText ErrorText;
				if (!ImportUsdFile(UsdFile.FilePath, AssetPath, ErrorText))
				{
					UE_LOG(LogChaosClothAssetDataflowNodes, Display, TEXT("No valid USD Cloth Schema was found in file '%s': %s. Will now fallback to the legacy schema-less USD import."), *UsdFile.FilePath, *ErrorText.ToString());

					if (!ImportUsdFile_Schemaless(UsdFile.FilePath, AssetPath, ErrorText))
					{
						UE::Chaos::ClothAsset::FClothDataflowTools::LogAndToastWarning(*this,
							LOCTEXT("FailedToImportUsdFileHeadline", "Failed to import USD file from file."),
							FText::Format(LOCTEXT("FailedToImportUsdDetails", "Error while importing USD cloth from file '{0}':\n{1}"), FText::FromString(UsdFile.FilePath), ErrorText));
					}
				}
			}))
	, ReimportUsdFile(
		FDataflowFunctionProperty::FDelegate::CreateLambda([this](UE::Dataflow::FContext& Context)
			{
				UsdFile.Execute(Context);
			}))
	, ReloadSimStaticMesh(
		FDataflowFunctionProperty::FDelegate::CreateLambda([this](UE::Dataflow::FContext& /*Context*/)
			{
				const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(Collection));
				FText ErrorText;
				if (!ImportSimStaticMesh(ClothCollection, ErrorText))
				{
					UE::Chaos::ClothAsset::FClothDataflowTools::LogAndToastWarning(*this,
						LOCTEXT("FailedToImportSimMeshHeadline", "Failed to reload the simulation static mesh."),
						FText::Format(LOCTEXT("FailedToImportSimMeshDetails", "Error while re-importing the simulation mesh from static mesh '{0}':\n{1}"), FText::FromString(ImportedSimStaticMesh->GetName()), ErrorText));
				}
				Collection = MoveTemp(*ClothCollection);
			}))
	, ReloadRenderStaticMesh(
		FDataflowFunctionProperty::FDelegate::CreateLambda([this](UE::Dataflow::FContext& /*Context*/)
			{
				const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(Collection));
				FText ErrorText;
				if (!ImportRenderStaticMesh(ClothCollection, ErrorText))
				{
					UE::Chaos::ClothAsset::FClothDataflowTools::LogAndToastWarning(*this,
						LOCTEXT("FailedToImportRenderMeshHeadline", "Failed to reload the render static mesh."),
						FText::Format(LOCTEXT("FailedToImportRenderMeshDetails", "Error while re-importing the render mesh from static mesh '{0}':\n{1}"), FText::FromString(ImportedRenderStaticMesh->GetName()), ErrorText));
				}
				Collection = MoveTemp(*ClothCollection);
			}))
{
	using namespace UE::Chaos::ClothAsset;

	// Initialize to a valid collection
	const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(Collection));
	FCollectionClothFacade(ClothCollection).DefineSchema();
	Collection = MoveTemp(*ClothCollection);

	// Register connections
	RegisterOutputConnection(&Collection);
}

void FChaosClothAssetUSDImportNode_v2::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	using namespace UE::Chaos::ClothAsset;

	if (Out->IsA<FManagedArrayCollection>(&Collection))
	{
		SetValue(Context, Collection, &Collection);
	}
}

void FChaosClothAssetUSDImportNode_v2::Serialize(FArchive& Ar)
{
	using namespace UE::Chaos::ClothAsset;

	if (Ar.IsLoading() && !Ar.IsTransacting())
	{
		// Make sure to always have a valid cloth collection on reload, some new attributes could be missing from the cached collection
		// Must be executed before ImportRenderStaticMesh below, and after serializing the collection above, and even if the serialized version hasn't changed
		const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(Collection));
		FCollectionClothFacade ClothFacade(ClothCollection);
		if (!ClothFacade.IsValid())
		{
			ClothFacade.DefineSchema();
		}

		// Also apply any required fixup (e.g. soft object path names)
		ClothFacade.PostSerialize(Ar);
		
		Collection = MoveTemp(*ClothCollection);

		// Regenerate correct dependencies if needed
		if (!ImportedAssets_DEPRECATED.IsEmpty())
		{
			ImportedAssets_DEPRECATED.Empty();
			ImportedSimAssets = Private::GetImportedAssetDependencies(ImportedSimStaticMesh);
			ImportedRenderAssets = Private::GetImportedAssetDependencies(ImportedRenderStaticMesh);
		}
	}
}

void FChaosClothAssetUSDImportNode_v2::ResetImport()
{
	Collection.Reset();
	PackagePath = FString();
	ImportedRenderStaticMesh = nullptr;
	ImportedSimStaticMesh = nullptr;
	ImportedUVScale = { 1.f, 1.f };
	ImportedRenderAssets.Reset();
	ImportedSimAssets.Reset();
	UsdClothData.Reset();
}

// V1 of the USD importer (schemaless)
bool FChaosClothAssetUSDImportNode_v2::ImportUsdFile_Schemaless(const FString& UsdFilePath, const FString& AssetPath, FText& OutErrorText)
{
	using namespace UE::Chaos::ClothAsset;

	ResetImport();

	// Temporary borrow the collection to make the shared ref
	const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(Collection));
	ON_SCOPE_EXIT{ Collection = MoveTemp(*ClothCollection); };

	const float NumSteps = bImportRenderMesh ? 2.f : 1.f;  // Sim mesh is always imported
	FScopedSlowTask SlowTask(NumSteps, LOCTEXT("ImportingUSDFile", "Importing USD file..."));

	SlowTask.EnterProgressFrame(1.f, LOCTEXT("CreatingAssets", "Creating assets and importing simulation mesh..."));
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FChaosClothAssetUSDImportNode::ImportFromFile(UsdFilePath, AssetPath, bImportSimMesh, ClothCollection, PackagePath, OutErrorText);
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	static const FString SchemalessSimStaticMeshName = TEXT("");
	static const FString SchemalessRenderStaticMeshName = TEXT("SM_Mesh");
	UpdateImportedAssets(SchemalessSimStaticMeshName, SchemalessRenderStaticMeshName);

	// Add the render mesh to the collection, since it wasn't originally cached in the collection in the first importer
	if (bImportRenderMesh)
	{
		SlowTask.EnterProgressFrame(1.f, LOCTEXT("ImportingRenderMesh", "Importing render mesh..."));
		if (!ImportRenderStaticMesh(ClothCollection, OutErrorText))
		{
			return false;
		}
	}

	return true;
}

// V2 of the USD importer (using cloth schema)
bool FChaosClothAssetUSDImportNode_v2::ImportUsdFile(const FString& UsdFilePath, const FString& AssetPath, FText& OutErrorText)
{
	using namespace UE;
	using namespace UE::Chaos::ClothAsset;
	using namespace UE::Chaos::ClothAsset::Private;

	ResetImport();

#if USE_USD_SDK
	// Temporary borrow the collection to make the shared ref
	const TSharedRef<FManagedArrayCollection> ClothCollection = MakeShared<FManagedArrayCollection>(MoveTemp(Collection));
	ON_SCOPE_EXIT{ Collection = MoveTemp(*ClothCollection); };

	FCollectionClothFacade ClothFacade(ClothCollection);
	ClothFacade.DefineSchema();

	// Empty file
	if (UsdFilePath.IsEmpty())
	{
		return true;
	}

	// Start slow task
	const float NumSteps = bImportSimMesh ? bImportRenderMesh ? 2.f : 1.f : bImportRenderMesh ? 1.f : 0.f;
	FScopedSlowTask SlowTask(NumSteps, LOCTEXT("ImportingUSDFile", "Importing USD file..."));
	SlowTask.MakeDialogDelayed(1.f);

	// Open stage
	constexpr bool bUseStageCache = false;  // Reload from disk, not from cache
	constexpr EUsdInitialLoadSet UsdInitialLoadSet = EUsdInitialLoadSet::LoadAll;  // TODO: Ideally we should only use LoadNone to start with and load what's needed once the Schema is defined

	FUsdStage UsdStage = UnrealUSDWrapper::OpenStage(*UsdFilePath, UsdInitialLoadSet, bUseStageCache);
	if (!UsdStage)
	{
		OutErrorText = LOCTEXT("CantCreateNewStage", "Failed to open the specified USD file.");
		return false;
	}

	// Find the cloth prim
	FUsdPrim ClothPrim = FindAPIPrim(UsdStage.GetPseudoRoot(), ClothRootAPI);
	if (!ClothPrim)
	{
		OutErrorText = LOCTEXT("CantFindClothRootAPI", "Can't find a cloth root inside the specified USD file.");
		return false;
	}

	// Find SimMesh and Render Mesh prims
	FUsdPrim SimMeshPrim = FindSimMeshPrim(ClothPrim);
	TArray<FUsdPrim> RenderMeshPrims = FindRenderMeshPrims(ClothPrim);
	if (!SimMeshPrim && !RenderMeshPrims.Num())
	{
		OutErrorText = LOCTEXT("CantFindMeshPrims", "Can't find a sim mesh or render mesh prim with valid pattern data.");
		return false;
	}

	// Remove Opacity from the stage before import since otherwise it messes up all materials
	if (!bImportWithOpacity)
	{
		RemoveMaterialOpacity(UsdStage.GetPseudoRoot());
	}

	// Read UVScale attribute
	ImportedUVScale = GetSimMeshUVScale(SimMeshPrim);

	// Update import location
	const uint32 UsdPathHash = GetTypeHash(UsdFile.FilePath);  // Path hash to store all import from the same file/same path to the same content folder
	const FString UsdFileName = SlugStringForValidName(FPaths::GetBaseFilename(UsdFile.FilePath));
	const FString PackageName = FString::Printf(TEXT("%s_%08X"), *UsdFileName, UsdPathHash);
	PackagePath = FPaths::Combine(AssetPath + TEXT("_Import"), PackageName);

	// Mesh selector lambda
	TArray<FUsdPrim> ClothChildren = ClothPrim.GetChildren();
	auto SetMeshClothChildrenActive = [&ClothChildren](auto Predicate)
		{
			for (FUsdPrim& ClothChildPrim : ClothChildren)
			{
				if (ClothChildPrim.IsA(TEXT("Mesh")))
				{
					ClothChildPrim.SetActive(Predicate(ClothChildPrim));
				}
			}
		};

	// Import sim mesh from the static mesh
	if (bImportSimMesh)
	{
		SlowTask.EnterProgressFrame(1.f);

		// Check that the entire mesh is made of triangles
		if (!CheckSimMeshPrimTriangles(SimMeshPrim, OutErrorText))
		{
			return false;
		}

		// Import the simulation patterns
		if (!ImportPatternsFromSimMeshPrim(SimMeshPrim, UsdClothData.SimPatterns, OutErrorText))
		{
			return false;
		}

		// Import the sewings
		if (!ImportSewingsFromSimMeshPrim(SimMeshPrim, UsdClothData.Sewings, OutErrorText))
		{
			return false;
		}

		// Import the springs
		if (!ImportSpringsFromSimMeshPrim(SimMeshPrim, UsdClothData.SimulationCollection, OutErrorText))
		{
			return false;
		}

		// Import the sim static mesh selectively by disabling the render prims
		SetMeshClothChildrenActive([&SimMeshPrim](FUsdPrim& ClothChildPrim) { return ClothChildPrim == SimMeshPrim; });
		TArray<TObjectPtr<UObject>> ImportedAssets = ImportStaticMeshesFromUsdStage(UsdStage, UsdFilePath, PackagePath);
		ImportedSimStaticMesh = FindImportedStaticMesh(ImportedAssets, SimMeshPrim.GetPrimPath().GetString());

		// Import the simulation properties if they exist
		const FUsdStageInfo UsdStageInfo(UsdStage);
		if (!ImportSimulationProperties(
			ClothPrim,
			SimMeshPrim,
			UsdStageInfo.UpAxis,
			UsdClothData.SimulationCollection,
			UsdClothData.SimPatternFabricIndices,
			OutErrorText))
		{
			return false;
		}

		// Import the geometry and finalize the patterns
		if (!ImportSimStaticMesh(ClothCollection, OutErrorText))
		{
			return false;
		}
	}

	// Import render mesh from the static mesh
	if (bImportRenderMesh)
	{
		SlowTask.EnterProgressFrame(1.f);

		// Import the render mesh selectively by disabling the sim prim
		ClothPrim.SetTypeName(TEXT("Xform"));  // Won't collapse the meshes unless the root is an Xform prim, so make it one
		UsdUtils::SetDefaultKind(ClothPrim, EUsdDefaultKind::Component);  // Set the cloth prim kind to enable KindsToCollapse

		SetMeshClothChildrenActive([&RenderMeshPrims](FUsdPrim& ClothChildPrim) { return RenderMeshPrims.Contains(ClothChildPrim); });
		TArray<TObjectPtr<UObject>> ImportedAssets = ImportStaticMeshesFromUsdStage(UsdStage, UsdFilePath, PackagePath);
		ImportedRenderStaticMesh = FindImportedStaticMesh(ImportedAssets, ClothPrim.GetPrimPath().GetString());  // Collapsed mesh uses the root ClothPrim name

		// Import the render patterns
		if (!ImportPatternsFromRenderMeshPrims(RenderMeshPrims, SimMeshPrim, UsdClothData.RenderPatterns, UsdClothData.RenderToSimPatterns, OutErrorText))
		{
			return false;
		}

		if (!ImportRenderStaticMesh(ClothCollection, OutErrorText))
		{
			return false;
		}
	}

	return true;

#else  // #if USE_USD_SDK

	OutErrorText = LOCTEXT("NoUsdSdk", "The ChaosClothAssetDataflowNodes module has been compiled without the USD SDK enabled.");
	return false;

#endif  // #else #if USE_USD_SDK
}

void FChaosClothAssetUSDImportNode_v2::UpdateImportedAssets(const FString& SimMeshName, const FString& RenderMeshName)
{
	ImportedSimStaticMesh = nullptr;
	ImportedRenderStaticMesh = nullptr;

	if (!PackagePath.IsEmpty() && (!SimMeshName.IsEmpty() || !RenderMeshName.IsEmpty()))
	{
		const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

		constexpr bool bRecursive = true;
		constexpr bool bIncludeOnlyOnDiskAssets = false;
		TArray<FAssetData> AssetData;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*PackagePath), AssetData, bRecursive, bIncludeOnlyOnDiskAssets);

		// Find sim mesh and render mesh (static meshes) dependencies
		for (const FAssetData& AssetDatum : AssetData)
		{
			if (AssetDatum.IsUAsset() && AssetDatum.IsTopLevelAsset() && AssetDatum.GetClass() == UStaticMesh::StaticClass())  // IsUAsset returns false for redirects
			{
				if (AssetDatum.AssetName == SimMeshName)
				{
					ImportedSimStaticMesh = CastChecked<UStaticMesh>(AssetDatum.GetAsset());
					UE_LOG(LogChaosClothAssetDataflowNodes, Display, TEXT("Imported USD Sim Mesh %s, path: %s"), *AssetDatum.AssetName.ToString(), *AssetDatum.GetFullName());
				}
				else if (AssetDatum.AssetName == RenderMeshName)
				{
					ImportedRenderStaticMesh = CastChecked<UStaticMesh>(AssetDatum.GetAsset());
					UE_LOG(LogChaosClothAssetDataflowNodes, Display, TEXT("Imported USD Render Mesh %s, path: %s"), *AssetDatum.AssetName.ToString(), *AssetDatum.GetFullName());
				}
				if ((ImportedSimStaticMesh || SimMeshName.IsEmpty()) &&
					(ImportedRenderStaticMesh || RenderMeshName.IsEmpty()))
				{
					break;
				}
			}
		}
	}
}

bool FChaosClothAssetUSDImportNode_v2::ImportSimStaticMesh(const TSharedRef<FManagedArrayCollection> ClothCollection, FText& OutErrorText)
{
	using namespace UE::Chaos::ClothAsset;
	using namespace ::Chaos::Softs;

	FCollectionClothFacade ClothFacade(ClothCollection);
	check(ClothFacade.IsValid());  // The Cloth Collection schema must be valid at this point

	// Define the selection schema if needed
	FCollectionClothSelectionFacade ClothSelectionFacade(ClothCollection);
	if (!ClothSelectionFacade.IsValid())
	{
		ClothSelectionFacade.DefineSchema();
	}

	// Empty the current sim mesh and any previously created selection set
	FClothGeometryTools::DeleteSimMesh(ClothCollection);
	FClothGeometryTools::DeleteSelections(ClothCollection, ClothCollectionGroup::SimFaces);

	ON_SCOPE_EXIT
		{
			// Bind to root bone on exit
			constexpr bool bBindSimMesh = true;
			constexpr bool bBindRenderMesh = false;
			FClothGeometryTools::BindMeshToRootBone(ClothCollection, bBindSimMesh, bBindRenderMesh);

			// Make sure to clean the dependencies whatever the import status is
			ImportedSimAssets = Private::GetImportedAssetDependencies(ImportedSimStaticMesh);
		};

	if (!ImportedSimStaticMesh)
	{
		return true;  // Nothing to import
	}

	// Init the static mesh attributes
	constexpr int32 LODIndex = 0;
	const FMeshDescription* const MeshDescription = ImportedSimStaticMesh->GetMeshDescription(LODIndex);
	check(MeshDescription);
	const FStaticMeshConstAttributes StaticMeshAttributes(*MeshDescription);

	if (!StaticMeshAttributes.GetVertexInstanceUVs().GetNumChannels())
	{
		OutErrorText = LOCTEXT("CantFindUVs", "Missing UV layer to initialize sim mesh data.");
		return false;
	}

	TArray<FVector2f> RestPositions2D;
	TArray<FVector3f> DrapedPositions3D;
	TArray<FIntVector3> TriangleToVertexIndex;

	// Retrieve 3D drapped positions
	DrapedPositions3D = StaticMeshAttributes.GetVertexPositions().GetRawArray();

	// Retrieve triangle indices and 2D rest positions
	RestPositions2D.SetNumZeroed(DrapedPositions3D.Num());

	const TConstArrayView<FVertexID> VertexInstanceVertexIndices = StaticMeshAttributes.GetVertexInstanceVertexIndices().GetRawArray();
	const TConstArrayView<FVertexInstanceID> TriangleVertexInstanceIndices = StaticMeshAttributes.GetTriangleVertexInstanceIndices().GetRawArray();
	const TConstArrayView<FVector2f> VertexInstanceUVs = StaticMeshAttributes.GetVertexInstanceUVs().GetRawArray();

	check(TriangleVertexInstanceIndices.Num() % 3 == 0);
	TriangleToVertexIndex.SetNumUninitialized(TriangleVertexInstanceIndices.Num() / 3);

	auto SetRestPositions2D = [&RestPositions2D, &VertexInstanceUVs](FVertexID VertexID, FVertexInstanceID VertexInstanceID) -> bool
		{
			if (RestPositions2D[VertexID] == FVector2f::Zero())
			{
				RestPositions2D[VertexID] = VertexInstanceUVs[VertexInstanceID];
			}
			else if (!RestPositions2D[VertexID].Equals(VertexInstanceUVs[VertexInstanceID]))
			{
				return false;
			}
			return true;
		};

	for (int32 TriangleIndex = 0; TriangleIndex < TriangleToVertexIndex.Num(); ++TriangleIndex)
	{
		const FVertexInstanceID VertexInstanceID0 = TriangleVertexInstanceIndices[TriangleIndex * 3];
		const FVertexInstanceID VertexInstanceID1 = TriangleVertexInstanceIndices[TriangleIndex * 3 + 1];
		const FVertexInstanceID VertexInstanceID2 = TriangleVertexInstanceIndices[TriangleIndex * 3 + 2];

		const FVertexID VertexID0 = VertexInstanceVertexIndices[VertexInstanceID0];
		const FVertexID VertexID1 = VertexInstanceVertexIndices[VertexInstanceID1];
		const FVertexID VertexID2 = VertexInstanceVertexIndices[VertexInstanceID2];

		TriangleToVertexIndex[TriangleIndex] = FIntVector3(VertexID0, VertexID1, VertexID2);

		if (!SetRestPositions2D(VertexID0, VertexInstanceID0) ||
			!SetRestPositions2D(VertexID1, VertexInstanceID1) ||
			!SetRestPositions2D(VertexID2, VertexInstanceID2))
		{
			OutErrorText = LOCTEXT("UsdSimMeshWelded", "The sim mesh has already been welded. This importer needs an unwelded sim mesh.");
			// TODO: unweld vertices, generate seams(?), and reindex all constraints
			return false;
		}
	}

	// Rescale the 2D mesh with the UV scale, and flip the UV's Y coordinates
	for (FVector2f& Pos : RestPositions2D)
	{
		Pos.Y = 1.f - Pos.Y;
		Pos *= ImportedUVScale;
	}

	// Save pattern to the collection cache
	check(RestPositions2D.Num() == DrapedPositions3D.Num());  // Should have already exited with the UsdSimMeshWelded error in this case
	if (TriangleToVertexIndex.Num() && RestPositions2D.Num())
	{
		// Cleanup sim mesh
		FClothDataflowTools::FSimMeshCleanup SimMeshCleanup(TriangleToVertexIndex, RestPositions2D, DrapedPositions3D);

		bool bHasRepairedTriangles = SimMeshCleanup.RemoveDegenerateTriangles();
		bHasRepairedTriangles = SimMeshCleanup.RemoveDuplicateTriangles() || bHasRepairedTriangles;

		const TArray<int32> OriginalToNewTriangles = FClothDataflowTools::GetOriginalToNewIndices<TSet<int32>>(SimMeshCleanup.OriginalTriangles, TriangleToVertexIndex.Num());

		// Add support for original indices
		ClothFacade.AddUserDefinedAttribute<TArray<int32>>(Private::OriginalIndicesName, ClothCollectionGroup::SimFaces);
		ClothFacade.AddUserDefinedAttribute<TArray<int32>>(Private::OriginalIndicesName, ClothCollectionGroup::SimVertices2D);

		// Add the patterns from the clean mesh
		for (const TPair<FName, TSet<int32>>& PatternNameFaces : UsdClothData.SimPatterns)
		{
			// Filter the pattern selection set using the remaining triangles from the cleaned triangle list
			TSet<int32> PatternSet;
			PatternSet.Reserve(PatternNameFaces.Value.Num());
			for (const int32 Face : PatternNameFaces.Value)
			{
				if (OriginalToNewTriangles.IsValidIndex(Face) && OriginalToNewTriangles[Face] != INDEX_NONE)
				{
					PatternSet.Emplace(OriginalToNewTriangles[Face]);
				}
			}

			// Add the new pattern
			if (PatternSet.Num())
			{
				TArray<FIntVector3> PatternTriangleToVertexIndex;
				TArray<TArray<int32>> PatternOriginalTriangles;
				PatternTriangleToVertexIndex.Reserve(PatternSet.Num());
				PatternOriginalTriangles.Reserve(PatternSet.Num());
				{
					for (const int32 Index : PatternSet)
					{
						PatternTriangleToVertexIndex.Emplace(SimMeshCleanup.TriangleToVertexIndex[Index]);
						PatternOriginalTriangles.Emplace(SimMeshCleanup.OriginalTriangles[Index].Array());
					}
				}

				TArray<FVector2f> PatternRestPositions2D;
				TArray<FVector3f> PatternDrapedPositions3D;
				TArray<TArray<int32>> PatternOriginalVertices;
				TArray<int32> PatternVertexReindex;
				const int32 MaxNumVertices = SimMeshCleanup.RestPositions2D.Num();
				PatternRestPositions2D.Reserve(MaxNumVertices);
				PatternDrapedPositions3D.Reserve(MaxNumVertices);
				PatternOriginalVertices.Reserve(MaxNumVertices);
				PatternVertexReindex.Init(INDEX_NONE, MaxNumVertices);

				int32 NewIndex = -1;
				for (FIntVector3& Triangle : PatternTriangleToVertexIndex)
				{
					for (int32 Vertex = 0; Vertex < 3; ++Vertex)
					{
						// Add the new vertex
						int32& Index = Triangle[Vertex];
						if (PatternVertexReindex[Index] == INDEX_NONE)
						{
							PatternVertexReindex[Index] = ++NewIndex;
							PatternRestPositions2D.Emplace(SimMeshCleanup.RestPositions2D[Index]);
							PatternDrapedPositions3D.Emplace(SimMeshCleanup.DrapedPositions3D[Index]);
							PatternOriginalVertices.Emplace(SimMeshCleanup.OriginalVertices[Index].Array());
						}
						// Reindex the triangle vertex with the new index
						Index = PatternVertexReindex[Index];
					}
				}

				// Find this pattern's fabric if any
				const int32 FabricIndex = UsdClothData.SimPatternFabricIndices.Contains(PatternNameFaces.Key) ?
					UsdClothData.SimPatternFabricIndices[PatternNameFaces.Key] :
					INDEX_NONE;

				// Add this pattern to the cloth collection
				const int32 SimPatternIndex = ClothFacade.AddSimPattern();
				FCollectionClothSimPatternFacade SimPattern = ClothFacade.GetSimPattern(SimPatternIndex);
				SimPattern.Initialize(PatternRestPositions2D, PatternDrapedPositions3D, PatternTriangleToVertexIndex, FabricIndex);

				// Keep track of the original triangle indices
				const TArrayView<TArray<int32>> OriginalTriangles =
					ClothFacade.GetUserDefinedAttribute<TArray<int32>>(Private::OriginalIndicesName, ClothCollectionGroup::SimFaces);  // Don't move outside the loop, the array might get re-allocated
				const int32 SimFacesOffset = SimPattern.GetSimFacesOffset();
				for (int32 Index = 0; Index < PatternOriginalTriangles.Num(); ++Index)
				{
					OriginalTriangles[SimFacesOffset + Index] = PatternOriginalTriangles[Index];
				}

				// Keep track of the original vertex indices
				const TArrayView<TArray<int32>> OriginalVertices =
					ClothFacade.GetUserDefinedAttribute<TArray<int32>>(Private::OriginalIndicesName, ClothCollectionGroup::SimVertices2D);  // Don't move outside the loop, the array might get re-allocated
				const int32 SimVertices2DOffset = SimPattern.GetSimVertices2DOffset();
				for (int32 Index = 0; Index < PatternOriginalVertices.Num(); ++Index)
				{
					OriginalVertices[SimVertices2DOffset + Index] = PatternOriginalVertices[Index];
				}

				// Add the pattern triangle list as a selection set
				TSet<int32>& SelectionSet = ClothSelectionFacade.FindOrAddSelectionSet(PatternNameFaces.Key, ClothCollectionGroup::SimFaces);
				SelectionSet.Empty(PatternSet.Num());
				for (int32 Index = SimFacesOffset; Index < SimFacesOffset + PatternTriangleToVertexIndex.Num(); ++Index)
				{
					SelectionSet.Emplace(Index);
				}
			}
		}

		// Check the resulting cleaned mesh
		const int32 NumSimVertices2D = ClothFacade.GetNumSimVertices2D();
		const int32 NumSimFaces = ClothFacade.GetNumSimFaces();
		if (!NumSimVertices2D || !NumSimFaces)
		{
			return true;  // Empty mesh
		}

		const TConstArrayView<TArray<int32>> OriginalTriangles =
			ClothFacade.GetUserDefinedAttribute<TArray<int32>>(Private::OriginalIndicesName, ClothCollectionGroup::SimFaces);
		const TArray<int32> OriginalToNewFaceIndices = FClothDataflowTools::GetOriginalToNewIndices(OriginalTriangles, TriangleToVertexIndex.Num());

		const TConstArrayView<TArray<int32>> OriginalVertices =
			ClothFacade.GetUserDefinedAttribute<TArray<int32>>(Private::OriginalIndicesName, ClothCollectionGroup::SimVertices2D);
		const TArray<int32> OriginalToNewVertexIndices = FClothDataflowTools::GetOriginalToNewIndices(OriginalVertices, RestPositions2D.Num());

		// Add the sewings
		for (const TPair<FName, TSet<FIntVector2>>& SewingNameIndices : UsdClothData.Sewings)
		{
			TSet<FIntVector2> Indices;
			for (const FIntVector2& Stitch : SewingNameIndices.Value)
			{
				if (!OriginalToNewVertexIndices.IsValidIndex(Stitch[0]) || !OriginalToNewVertexIndices.IsValidIndex(Stitch[1]))
				{
					OutErrorText = LOCTEXT("BadSewingIndex", "An out of range sewing index has been found.");
					return false;
				}
				const int32 StitchIndex0 = OriginalToNewVertexIndices[Stitch[0]];
				const int32 StitchIndex1 = OriginalToNewVertexIndices[Stitch[1]];
				if (StitchIndex0 != INDEX_NONE && StitchIndex1 != INDEX_NONE)
				{
					Indices.Emplace(StitchIndex0 < StitchIndex1 ? FIntVector2(StitchIndex0, StitchIndex1) : FIntVector2(StitchIndex1, StitchIndex0));
				}
			}

			FCollectionClothSeamFacade ClothSeamFacade = ClothFacade.AddGetSeam();
			ClothSeamFacade.Initialize(Indices.Array());
		}

		// Add the springs
		const TSharedRef<FManagedArrayCollection> UsdClothDataCollection = MakeShared<FManagedArrayCollection>(UsdClothData.SimulationCollection);
		const FEmbeddedSpringFacade UsdClothDataSpringFacade(*UsdClothDataCollection, ClothCollectionGroup::SimVertices3D);
		if (UsdClothDataSpringFacade.IsValid())
		{
			// Initialize the spring facade
			FEmbeddedSpringFacade SpringFacade(*ClothCollection, ClothCollectionGroup::SimVertices3D);
			checkf(SpringFacade.IsValid(), TEXT("FEmbeddedSpringFacade constructor should have defined the schema."));
			FEmbeddedSpringConstraintFacade SpringConstraintFacade = SpringFacade.AddGetSpringConstraint();
			SpringConstraintFacade.Initialize(
				TConstArrayView<FIntVector2>(),
				TConstArrayView<float>(),
				TConstArrayView<float>(),
				TConstArrayView<float>(),
				TConstArrayView<float>(),
				TEXT("VertexSpringConstraint"));

			for (int32 ConstraintIndex = 0; ConstraintIndex < UsdClothDataSpringFacade.GetNumSpringConstraints(); ++ConstraintIndex)
			{
				const FEmbeddedSpringConstraintFacade UsdClothDataSpringConstraintFacade = UsdClothDataSpringFacade.GetSpringConstraintConst(ConstraintIndex);
				if (UsdClothDataSpringConstraintFacade.GetConstraintEndPointNumIndices() == FUintVector2(1, 1) &&
					UsdClothDataSpringConstraintFacade.GetConstraintName() == TEXT("VertexSpringConstraint"))
				{
					const TConstArrayView<int32> SimVertex3DLookup = ClothFacade.GetSimVertex3DLookup();

					TArray<TArray<int32>> SourceIndices(UsdClothDataSpringConstraintFacade.GetSourceIndexConst());
					TArray<TArray<int32>> TargetIndices(UsdClothDataSpringConstraintFacade.GetTargetIndexConst());
					for (TArray<int32>& SourceIndex : SourceIndices)
					{
						SourceIndex[0] = OriginalToNewVertexIndices.IsValidIndex(SourceIndex[0]) && OriginalToNewVertexIndices[SourceIndex[0]] != INDEX_NONE ?
							SimVertex3DLookup[OriginalToNewVertexIndices[SourceIndex[0]]] : INDEX_NONE;
					}
					for (TArray<int32>& TargetIndex : TargetIndices)
					{
						TargetIndex[0] = OriginalToNewVertexIndices.IsValidIndex(TargetIndex[0]) && OriginalToNewVertexIndices[TargetIndex[0]] != INDEX_NONE ?
							SimVertex3DLookup[OriginalToNewVertexIndices[TargetIndex[0]]] : INDEX_NONE;
					}
					SpringConstraintFacade.Append(
						SourceIndices,
						UsdClothDataSpringConstraintFacade.GetSourceWeightsConst(),
						TargetIndices,
						UsdClothDataSpringConstraintFacade.GetTargetWeightsConst(),
						UsdClothDataSpringConstraintFacade.GetSpringLengthConst());
				}
			}
			// Copy the properties
			const FCollectionPropertyConstFacade UsdClothDataPropertyFacade(UsdClothDataCollection);
			if (UsdClothDataPropertyFacade.IsValid())
			{
				FCollectionPropertyMutableFacade PropertyFacade(ClothCollection);
				PropertyFacade.Copy(UsdClothData.SimulationCollection);
			}
		}

		// Add the solver properties
		const FCollectionClothConstFacade SimulationClothFacade(UsdClothDataCollection);
		if (SimulationClothFacade.IsValid(EClothCollectionExtendedSchemas::Solvers))
		{
			ClothFacade.DefineSchema(EClothCollectionExtendedSchemas::Solvers);
			ClothFacade.SetSolverAirDamping(SimulationClothFacade.GetSolverAirDamping());
			ClothFacade.SetSolverGravity(SimulationClothFacade.GetSolverGravity());
			ClothFacade.SetSolverSubSteps(SimulationClothFacade.GetSolverSubSteps());
			ClothFacade.SetSolverTimeStep(SimulationClothFacade.GetSolverTimeStep());
		}

		// Add the fabric properties
		if (SimulationClothFacade.IsValid(EClothCollectionExtendedSchemas::Fabrics))
		{
			ClothFacade.DefineSchema(EClothCollectionExtendedSchemas::Fabrics);

			for (int32 FabricIndex = 0; FabricIndex < SimulationClothFacade.GetNumFabrics(); ++FabricIndex)
			{
				verify(ClothFacade.AddFabric() == FabricIndex);
				FCollectionClothFabricFacade ClothFabricFacade = ClothFacade.GetFabric(FabricIndex);
				const FCollectionClothFabricConstFacade SimulationClothFabricFacade = SimulationClothFacade.GetFabric(FabricIndex);

				ClothFabricFacade.Initialize(
					SimulationClothFabricFacade.GetBendingStiffness(),
					SimulationClothFabricFacade.GetBucklingRatio(),
					SimulationClothFabricFacade.GetBucklingStiffness(),
					SimulationClothFabricFacade.GetStretchStiffness(),
					SimulationClothFabricFacade.GetDensity(),
					SimulationClothFabricFacade.GetFriction(),
					SimulationClothFabricFacade.GetDamping(),
					SimulationClothFabricFacade.GetPressure(),
					SimulationClothFabricFacade.GetLayer(),
					SimulationClothFabricFacade.GetCollisionThickness());
			}
		}
	}
	return true;
}

bool FChaosClothAssetUSDImportNode_v2::ImportRenderStaticMesh(const TSharedRef<FManagedArrayCollection> ClothCollection, FText& OutErrorText)
{
	using namespace UE::Chaos::ClothAsset;

	FCollectionClothFacade ClothFacade(ClothCollection);
	check(ClothFacade.IsValid());  // The Cloth Collection schema must be valid at this point

	// Define the selection schema if needed
	FCollectionClothSelectionFacade ClothSelectionFacade(ClothCollection);
	if (!ClothSelectionFacade.IsValid())
	{
		ClothSelectionFacade.DefineSchema();
	}

	// Empty the current render mesh and previously create selections
	FClothGeometryTools::DeleteRenderMesh(ClothCollection);
	FClothGeometryTools::DeleteSelections(ClothCollection, ClothCollectionGroup::RenderFaces);

	// Make sure to clean the dependencies whatever the import status is
	ON_SCOPE_EXIT
		{
			// Bind to root bone on exit
			constexpr bool bBindSimMesh = false;
			constexpr bool bBindRenderMesh = true;
			FClothGeometryTools::BindMeshToRootBone(ClothCollection, bBindSimMesh, bBindRenderMesh);

			// Make sure to clean the dependencies whatever the import status is
			ImportedRenderAssets = Private::GetImportedAssetDependencies(ImportedRenderStaticMesh);
		};

	// Import the LOD 0
	if (ImportedRenderStaticMesh && ImportedRenderStaticMesh->GetNumSourceModels())
	{
		constexpr int32 LODIndex = 0;
		if (const FMeshDescription* const MeshDescription = ImportedRenderStaticMesh->GetMeshDescription(LODIndex))
		{
			const FMeshBuildSettings& BuildSettings = ImportedRenderStaticMesh->GetSourceModel(LODIndex).BuildSettings;
			FRenderMeshImport RenderMeshImport(*MeshDescription, BuildSettings);

			const TArray<FStaticMaterial>& StaticMaterials = ImportedRenderStaticMesh->GetStaticMaterials();
			RenderMeshImport.AddRenderSections(ClothCollection, StaticMaterials, Private::OriginalIndicesName, Private::OriginalIndicesName);

			// Create pattern selection sets
			const TConstArrayView<TArray<int32>> OriginalTriangles =
				ClothFacade.GetUserDefinedAttribute<TArray<int32>>(Private::OriginalIndicesName, ClothCollectionGroup::RenderFaces);

			if (OriginalTriangles.Num())
			{
				const TConstArrayView<FIntVector3> TriangleToVertexIndex = ClothFacade.GetRenderIndices();
				const TArray<int32> OriginalToNewTriangles = FClothDataflowTools::GetOriginalToNewIndices<TArray<int32>>(OriginalTriangles, TriangleToVertexIndex.Num());

				for (const TPair<FName, TSet<int32>>& PatternNameFaces : UsdClothData.RenderPatterns)
				{
					// Add the pattern triangle list as a selection set
					TSet<int32>& SelectionSet = ClothSelectionFacade.FindOrAddSelectionSet(PatternNameFaces.Key, ClothCollectionGroup::RenderFaces);
					SelectionSet.Empty(PatternNameFaces.Value.Num());
					for (const int32 Index : PatternNameFaces.Value)
					{
						SelectionSet.Emplace(OriginalToNewTriangles[Index]);
					}
				}
			}
			// TODO: Proxy deformer
		}
		else
		{
			OutErrorText = LOCTEXT("MissingMeshDescription", "An imported render static mesh has no mesh description!");
			return false;
		}
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
