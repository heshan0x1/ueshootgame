// Copyright Epic Games, Inc. All Rights Reserved.

#include "Verification/VerifyMetaHumanGroom.h"

#include "MetaHumanAssetReport.h"
#include "Verification/MetaHumanCharacterVerification.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "Logging/StructuredLog.h"
#include "MeshAttributes.h"
#include "SkeletalMeshAttributes.h"
#include "Misc/FileHelper.h"
#include "Misc/RuntimeErrors.h"
#include "Spatial/MeshAABBTree3.h"
#include "StaticMeshAttributes.h"
#include "Algo/AnyOf.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VerifyMetaHumanGroom)

#define LOCTEXT_NAMESPACE "VerifyMetaHumanGroom"

namespace UE::MetaHuman::Private
{
// Gets the combined bounding box for all groups of hair strands in the Groom asset
FBox GetStrandsCombinedBoundingBox(const TNotNull<const UGroomAsset*> GroomAsset)
{
	FHairStrandsDatas StrandsData;
	FHairStrandsDatas UnusedGuidesData;
	FBox StrandsBounds;
	for (int32 StrandsGroupIndex = 0; StrandsGroupIndex < GroomAsset->GetNumHairGroups(); StrandsGroupIndex++)
	{
		GroomAsset->GetHairStrandsDatas(StrandsGroupIndex, StrandsData, UnusedGuidesData);
		StrandsBounds += FBox(StrandsData.BoundingBox);
	}
	return StrandsBounds;
}

bool VerifyMeshUVs(const UStaticMesh* Mesh)
{
	// Check we have at least one lod and one UV channel as part of the data
	constexpr uint32 LodIndexZero = 0;
	if (Mesh->GetNumLODs() && Mesh->GetNumUVChannels(LodIndexZero))
	{
		// Simple check that the values aren't all (0,0)
		const FStaticMeshVertexBuffer& Buffer = Mesh->GetRenderData()->LODResources[LodIndexZero].VertexBuffers.StaticMeshVertexBuffer;
		for (uint32 UVIndex = 0; UVIndex < Buffer.GetNumVertices(); UVIndex++)
		{
			constexpr uint32 UvChannelIndexZero = 0;
			const FVector2f& UVValue = Buffer.GetVertexUV(UVIndex, UvChannelIndexZero);
			if (UVValue.X || UVValue.Y)
			{
				return true;
			}
		}
	}
	return false;
}

void VerifyGroomToMeshAlignment(TNotNull<const UGroomAsset*> GroomAsset, TNotNull<const USkeletalMesh*> GroomMesh, const int GroupIndex, FFormatNamedArguments& Args, TNotNull<UMetaHumanAssetReport*> Report)
{
	// Get the strands data for the groom
	FHairStrandsDatas StrandsData;
	FHairStrandsDatas UnusedGuidesData;
	GroomAsset->GetHairStrandsDatas(GroupIndex, StrandsData, UnusedGuidesData);

	// Extract the geometry data from the skeletal mesh
	constexpr uint32 LodIndexZero = 0;
	constexpr uint32 UVIndexZero = 0;
	const FMeshDescription* MeshDescription = GroomMesh->GetMeshDescription(LodIndexZero);
	check(MeshDescription);

	// Check if we have valid UV data for strand to mesh correspondence
	bool bCheckUVs = true;
	const TVertexInstanceAttributesConstRef<FVector2f> UVs = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2f>(MeshAttribute::VertexInstance::TextureCoordinate);
	if (UVs.GetNumChannels() == 0)
	{
		// 3006 - UVs can't align if no UVs, but also binding is unlikely to work correctly.
		Report->AddError({
			FText::Format(LOCTEXT("MeshUVNotPresent", "Source Mesh for Groom Asset {AssetName} does not have valid UVs"), Args),
			GroomAsset
		});
		bCheckUVs = false;
	}

	if (StrandsData.StrandsCurves.CurvesRootUV.Num() == 0)
	{
		// 3006 - UVs can't align if no UVs on the strands
		Report->AddWarning({
			FText::Format(LOCTEXT("StrandsUVNotPresent", "Group {GroupIndex} on Groom Asset {AssetName} does not have strands with valid UVs"), Args),
			GroomAsset
		});
		bCheckUVs = false;
	}

	// Build an FDynamicMesh3 so we can build an AABB tree with the data we need to calculate position and UV correspondence
	TMap<uint32, TArray<FBox2d>> UVRegions;
	TMap<uint32, uint32> IndexRemap;
	Geometry::FDynamicMesh3 MeshData;
	for (const FVertexID VertexIndex : MeshDescription->Vertices().GetElementIDs())
	{
		const uint32 NewIndex = MeshData.AppendVertex(FVector3d(MeshDescription->GetVertexPosition(VertexIndex)));
		IndexRemap.Add(VertexIndex, NewIndex);
	}

	for (const FTriangleID TriangleIndex : MeshDescription->Triangles().GetElementIDs())
	{
		// Add to dynamic Mesh
		const TArrayView<const FVertexID> Triangle = MeshDescription->GetTriangleVertices(TriangleIndex);
		const uint32 NewTriangleIndex = MeshData.AppendTriangle(IndexRemap[Triangle[0]], IndexRemap[Triangle[1]], IndexRemap[Triangle[2]]);
		if (bCheckUVs)
		{
			if (NewTriangleIndex != Geometry::FDynamicMesh3::InvalidID && NewTriangleIndex != Geometry::FDynamicMesh3::NonManifoldID && NewTriangleIndex != Geometry::FDynamicMesh3::DuplicateTriangleID)
			{
				const TArrayView<const FVertexInstanceID> TriangleVertexInstances = MeshDescription->GetTriangleVertexInstances(TriangleIndex);
				// Make bounding box containing triangle UVs and store against DynamicMesh Triangle Index.
				FBox2d UVRange({
					FVector2d(UVs.Get(TriangleVertexInstances[0], UVIndexZero)),
					FVector2d(UVs.Get(TriangleVertexInstances[1], UVIndexZero)),
					FVector2d(UVs.Get(TriangleVertexInstances[2], UVIndexZero))
				});

				// Expand the region to allow for small projection errors for hair roots close to the mesh surface.
				// We only want to warn users about large errors in UV correspondence.
				UVRange = UVRange.ExpandBy(UVRange.GetExtent() * 2);
				UVRange = UVRange.Overlap({{0., 0.}, {1., 1.}}); // clamp to valid UV range
				UVRegions.Add(NewTriangleIndex, {UVRange});
			}
		}
	}

	if (bCheckUVs)
	{
		// Also include the regions for all direct neighbours of the triangle. This is an attempt to account for projection
		// errors that cross uv-wrapping seams (e.g. at the back of the head). We are only attempting to handle simple
		// cases with small errors, so immediate neighbours are sufficient.
		for (const uint32 TriangleIndex : MeshData.TriangleIndicesItr())
		{
			const Geometry::FIndex3i Neighbours = MeshData.GetTriNeighbourTris(TriangleIndex);
			constexpr int OriginalTriangleIndex = 0; // First item in list is the original UVRegion for the triangle
			if (Neighbours.A != IndexConstants::InvalidID)
			{
				UVRegions[TriangleIndex].Add(UVRegions[Neighbours.A][OriginalTriangleIndex]);
			}
			if (Neighbours.B != IndexConstants::InvalidID)
			{
				UVRegions[TriangleIndex].Add(UVRegions[Neighbours.B][OriginalTriangleIndex]);
			}
			if (Neighbours.C != IndexConstants::InvalidID)
			{
				UVRegions[TriangleIndex].Add(UVRegions[Neighbours.C][OriginalTriangleIndex]);
			}
		}
	}

	Geometry::TMeshAABBTree3<Geometry::FDynamicMesh3> Tree(&MeshData);
	Tree.Build();

	bool bBadVerts = false;
	bool bBadUVs = false;

	for (uint32 CurveIndex = 0; CurveIndex < StrandsData.StrandsCurves.Num(); CurveIndex++)
	{
		// Iterate strands getting root position and UV
		const uint32 RootIndex = StrandsData.StrandsCurves.CurvesOffset[CurveIndex];
		check(RootIndex < static_cast<uint32>(StrandsData.StrandsPoints.PointsPosition.Num()));
		const FVector3d RootPos = FVector3d(StrandsData.StrandsPoints.PointsPosition[RootIndex]);

		// compare against the closest triangle for both spatial distance and whether UVs match or not.
		double SquareDistance{0};
		uint32 ClosestTriangleIndex = Tree.FindNearestTriangle(RootPos, SquareDistance);
		bool PositionMatch = ClosestTriangleIndex != IndexConstants::InvalidID && SquareDistance < 0.16; // 4mm ^ 2
		bBadVerts = bBadVerts || !PositionMatch;

		// Only check UVs for roots that lie near the surface
		if (bCheckUVs && !bBadUVs && PositionMatch)
		{
			check(CurveIndex < static_cast<uint32>(StrandsData.StrandsCurves.CurvesRootUV.Num()));
			FVector2d RootUV = FVector2d(StrandsData.StrandsCurves.CurvesRootUV[CurveIndex]);

			// The UVs in the groom roots have the Y axis inverted. See UnpackHairRootUV in HairStrandsPack.ush
			RootUV.Y = 1.f - RootUV.Y;

			bool UVMatch = Algo::AnyOf(UVRegions[ClosestTriangleIndex], [RootUV](const FBox2d& Region) { return Region.IsInsideOrOn(RootUV); });
			bBadUVs = bBadUVs || !UVMatch;
		}

		if (bBadVerts && (bBadUVs || !bCheckUVs))
		{
			// We've found everything that could be wrong - no point checking further
			break;
		}
	}

	if (bBadVerts)
	{
		// 3008
		Report->AddWarning({
			FText::Format(LOCTEXT("StrandRootsNotCloseToSourceMesh", "Group {GroupIndex} on Groom Asset {AssetName} has strand roots that do not lie on the surface of the source mesh"), Args),
			GroomAsset
		});
	}

	if (bBadUVs)
	{
		// 3006
		Report->AddWarning({
			FText::Format(LOCTEXT("StrandRootUVsDoNotMatch", "Group {GroupIndex} on Groom Asset {AssetName} has strand roots with UVs that do not match the source mesh"), Args),
			GroomAsset
		});
	}
}

UStaticMesh* GetTemplateHeadMesh()
{
	// Synchronous load as we are already in a slow operation
	if (UStaticMesh* Template = Cast<UStaticMesh>(FSoftObjectPath(TEXT("/MetaHumanSDK/TemplateAssets/SM_MH_Head.SM_MH_Head")).TryLoad()))
	{
		constexpr uint32 LodIndexZero = 0;
		// Check the template has the data we expect
		FMeshDescription* MeshDescription = Template->GetMeshDescription(LodIndexZero);
		if (MeshDescription && MeshDescription->GetNumUVElementChannels())
		{
			return Template;
		}
	}
	return nullptr;
}

USkeletalMesh* VerifyGroomingMesh(const UGroomBindingAsset* GroomBindingAsset, FFormatNamedArguments Args, UMetaHumanAssetReport* Report)
{
	USkeletalMesh* SourceGroomingMesh = GroomBindingAsset->GetSourceSkeletalMesh();

	if (!SourceGroomingMesh)
	{
		// 3003 Source grooming mesh missing
		Report->AddError({
			FText::Format(LOCTEXT("MissingSourceMesh", "Groom Binding {AssetName} does not have an associated source mesh."), Args),
			GroomBindingAsset
		});
	}

	if (!GroomBindingAsset->GetTargetSkeletalMesh())
	{
		// 3020 Target grooming mesh missing
		Report->AddError({
			FText::Format(LOCTEXT("MissingTargetMesh", "Groom Binding {AssetName} does not have an associated target mesh."), Args),
			GroomBindingAsset
		});
	}

	// Want to test for both source and target, but if we don't have a source we can't continue verification...
	if (!SourceGroomingMesh)
	{
		return nullptr;
	}

	Args.Add(TEXT("SourceSkelMesh"), FText::FromString(SourceGroomingMesh->GetName()));
	Report->AddVerbose({
		FText::Format(LOCTEXT("FoundSourceMesh", "Found {SourceSkelMesh}, using as source mesh for {AssetName}"), Args),
		GroomBindingAsset
	});

	const FBoxSphereBounds& SourceMeshBounds = SourceGroomingMesh->GetBounds();
	UGroomAsset* GroomAsset = GroomBindingAsset->GetGroom();
	check(GroomAsset);
	const FBoxSphereBounds& GroomBounds = FBoxSphereBounds(GroomAsset->GetHairDescriptionGroups().Bounds);

	if (!SourceMeshBounds.SphereRadius || !FBoxSphereBounds::BoxesIntersect(SourceMeshBounds, GroomBounds))
	{
		// 3004 Source grooming mesh not found - get extents of mesh and check they overlap with extents of groom
		Report->AddError({
			FText::Format(LOCTEXT("SourceMeshNotInCorrectLocation", "{AssetName} does not overlap spatially with {SourceSkelMesh} and so it can not be used as a source mesh."), Args),
			GroomAsset
		});
	}

	constexpr uint32 LodIndexZero = 0;
	constexpr uint32 UVIndexZero = 0;

	// Load template mesh
	const TStrongObjectPtr<UStaticMesh> TemplateMesh(GetTemplateHeadMesh());
	if (!TemplateMesh)
	{
		// Can't continue without the template mesh.
		Report->AddError({LOCTEXT("InternalErrorLoadingTemplate", "Internal error. Failed to load a valid template mesh for topology checks.")});
		return nullptr;
	}
	const FMeshDescription* TemplateMeshDescription = TemplateMesh->GetMeshDescription(LodIndexZero);
	check(TemplateMeshDescription);

	// Minimum allowable deviation from the template (in UV-space) for vertices in the mesh we are checking
	constexpr double DistanceThreshold = 0.001;
	constexpr int32 QuantizationConstant = 1.0 / DistanceThreshold;

	// Create a map from a quantized UV to a list of potential UV matches
	TMap<FInt32Vector2, TArray<FVector2f>> QuantizedUVLookup;
	const TVertexInstanceAttributesConstRef<FVector2f> TemplateUVs = TemplateMeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2f>(MeshAttribute::VertexInstance::TextureCoordinate);

	// "Splatting" the value into all bins +-1 avoids aliasing issues along quantisation boundaries
	for (const FVertexInstanceID VertexInstanceIndex : TemplateMeshDescription->VertexInstances().GetElementIDs())
	{
		FVector2f TemplateUV = TemplateUVs.Get(VertexInstanceIndex, UVIndexZero);
		FInt32Vector2 QuantizedUV = {static_cast<int32>(TemplateUV.X * QuantizationConstant), static_cast<int32>(TemplateUV.Y * QuantizationConstant)};
		for (FInt32Vector2 Offset(-1, -1); Offset.X < 2; Offset.X++)
		{
			for (Offset.Y = -1; Offset.Y < 2; Offset.Y++)
			{
				QuantizedUVLookup.FindOrAdd(QuantizedUV + Offset).Add(TemplateUV);
			}
		}
	}

	// Validate the Groom binding Mesh
	const FMeshDescription* MeshDescription = SourceGroomingMesh->GetMeshDescription(LodIndexZero);
	if (!MeshDescription)
	{
		// 3005 Geometry mismatch, groom can not be bound to target geometry - no LOD0 vertices present
		Report->AddError({
			FText::Format(LOCTEXT("SourceMeshMissingLod0", "{SourceSkelMesh} does not have LOD0 which is used for groom binding."), Args),
			SourceGroomingMesh
		});
		return nullptr;
	}

	const TVertexInstanceAttributesConstRef<FVector2f> TargetUVs = MeshDescription->VertexInstanceAttributes().GetAttributesRef<FVector2f>(MeshAttribute::VertexInstance::TextureCoordinate);
	if (!TargetUVs.GetNumChannels())
	{
		// 3005 Geometry mismatch, groom can not be bound to target geometry - no UVs present on the binding mesh
		Report->AddError({
			FText::Format(LOCTEXT("SourceMeshMissingUVs", "{SourceSkelMesh} does not have any UVs which are required to use as a binding mesh."), Args),
			SourceGroomingMesh
		});
		return nullptr;
	}

	TSet<FVertexInstanceID> VerticesToCheck;
	// We are only interested in vertices that are from triangles in Group 0 which is the "skin" group in MetaHuman
	// head meshes. Ignore things like teeth, eyes, etc.
	constexpr uint32 SkinPolygonGroup = 0;
	for (const FTriangleID TriangleIndex : MeshDescription->GetPolygonGroupTriangles(SkinPolygonGroup))
	{
		for (FVertexInstanceID VertexIndex : MeshDescription->GetTriangleVertexInstances(TriangleIndex))
		{
			VerticesToCheck.Add(VertexIndex);
		}
	}

	// Now check each vertex UV co-ordinate matches within the threshold to a UV value from the template
	uint32 MismatchCount = 0;
	for (FVertexInstanceID VertexIndex : VerticesToCheck)
	{
		FVector2f TargetUV = TargetUVs.Get(VertexIndex, UVIndexZero);
		if (TArray<FVector2f>* PotentialMatches = QuantizedUVLookup.Find({static_cast<int>(TargetUV.X * QuantizationConstant), static_cast<int>(TargetUV.Y * QuantizationConstant)}))
		{
			const auto CloseEnoughToTemplate = [TargetUV](const FVector2f& TemplateUV)
			{
				return FVector2f::Distance(TargetUV, TemplateUV) < DistanceThreshold;
			};

			if (!Algo::AnyOf(*PotentialMatches, CloseEnoughToTemplate))
			{
				MismatchCount++;
			}
		}
		else
		{
			// No Matches to check.
			MismatchCount++;
		}
	}

	if (MismatchCount != 0)
	{
		Args.Add(TEXT("MismatchedVertexCount"), MismatchCount);
		// 3005 Geometry mismatch, groom can not be bound to target geometry - vertices present with invalid UVs
		Report->AddError({
			FText::Format(LOCTEXT("SourceMeshIncorrectUVs", "{SourceSkelMesh} does not match the standard MetaHuman topology. {MismatchedVertexCount} vertices detected with incorrect UVs"), Args),
			SourceGroomingMesh
		});
	}
	return SourceGroomingMesh;
}

void VerifyGlobalStrandsInfoValid(const UGroomAsset* GroomAsset, FFormatNamedArguments Args, UMetaHumanAssetReport* Report)
{
	// 3001 groom_width attribute missing from alembic file
	if (!GroomAsset->GetHairDescription().HasAttribute(EHairAttribute::Width))
	{
		Report->AddInfo({
			FText::Format(LOCTEXT("MissingHairWidth", "The groom_width attribute was missing from the Alembic file used to generate Groom Asset {AssetName}"), Args),
			GroomAsset
		});
	}

	// 3002 groom_rootuv attribute missing from alembic file
	if (!GroomAsset->GetHairDescription().HasAttribute(EHairAttribute::RootUV))
	{
		Report->AddWarning({
			FText::Format(LOCTEXT("MissingRootUV", "The root_uv attribute was missing from the Alembic file used to generate Groom Asset {AssetName}"), Args),
			GroomAsset
		});
	}
}

void VerifyGroupsStrandsInfoValid(TNotNull<const UGroomAsset*> GroomAsset, const FHairGroupInfoWithVisibility& GroupInfo, FFormatNamedArguments Args, TNotNull<UMetaHumanAssetReport*> Report, TNotNull<const USkeletalMesh*> GroomMesh)
{
	Args.Add(TEXT("GroupIndex"), GroupInfo.GroupIndex);

	// 3008 Follicle verts not aligned with grooming mesh data
	const FBox& StrandsBounds = GroomAsset->GetHairGroupsPlatformData()[GroupInfo.GroupIndex].Strands.GetBounds();
	const FBox& GroomMeshBounds = GroomMesh->GetBounds().GetBox();
	if (GroomMeshBounds.IsValid && !GroomMeshBounds.Intersect(StrandsBounds))
	{
		Report->AddError({
			FText::Format(LOCTEXT("StrandsNotAlignedToSourceMesh", "Group {GroupIndex} on Groom Asset {AssetName} does not have strands aligned with the source mesh"), Args),
			GroomAsset
		});
	}

	VerifyGroomToMeshAlignment(GroomAsset, GroomMesh, GroupInfo.GroupIndex, Args, Report);

	// 3009 Number of vertices per curve exceeding 255
	const FHairGroupPlatformData& PlatformData = GroomAsset->GetHairGroupsPlatformData()[GroupInfo.GroupIndex];
	const uint32 StrandsFlags = PlatformData.Strands.BulkData.Header.Flags;
	const uint32 GuidesFlags = PlatformData.Guides.BulkData.Header.Flags;
	if (StrandsFlags & FHairStrandsBulkData::DataFlags_HasTrimmedPoint || GuidesFlags & FHairStrandsBulkData::DataFlags_HasTrimmedPoint)
	{
		Report->AddWarning({
			FText::Format(LOCTEXT("TooManyVerticesPerCurve", "Group {GroupIndex} on Groom Asset {AssetName} has more than 255 vertices per curve"), Args),
			GroomAsset
		});
	}

	// 3010 Number of vertices exceeding16 mil per group
	Args.Add(TEXT("MaxNumPoints"), HAIR_MAX_NUM_POINT_PER_GROUP);
	if (GroupInfo.NumCurveVertices + GroupInfo.NumGuideVertices > HAIR_MAX_NUM_POINT_PER_GROUP)
	{
		Report->AddWarning({
			FText::Format(LOCTEXT("TooManyVertices", "Strands for Group {GroupIndex} on Groom Asset {AssetName} have more than {MaxNumPoints} vertices"), Args),
			GroomAsset
		});
	}

	// 3011 Number of curves exceeding 4 mil per group
	Args.Add(TEXT("MaxNumCurves"), HAIR_MAX_NUM_CURVE_PER_GROUP);
	if (GroupInfo.NumCurves + GroupInfo.NumGuides > HAIR_MAX_NUM_CURVE_PER_GROUP)
	{
		Report->AddWarning({
			FText::Format(LOCTEXT("TooManyCurves", "Strands for Group {GroupIndex} on Groom Asset {AssetName} have more than {MaxNumCurves} curves"), Args),
			GroomAsset
		});
	}
}

void VerifyCardsInfoValid(const UGroomAsset* GroomAsset, const FHairGroupsCardsSourceDescription& CardsDescription, FFormatNamedArguments Args, UMetaHumanAssetReport* Report)
{
	Args.Add(TEXT("LodIndex"), CardsDescription.LODIndex);
	Args.Add(TEXT("GroupIndex"), CardsDescription.GroupIndex);

	const UStaticMesh* Mesh = CardsDescription.GetMesh();
	// Cards do not have any mesh data associated
	if (!IsValid(Mesh))
	{
		return;
	}
	Args.Add(TEXT("CardsMesh"), FText::FromName(Mesh->GetFName()));

	// 3013 card mesh not aligned with strand data - get extents and check overlap is 70% of the smallest volume
	const FBox StrandsBounds = GetStrandsCombinedBoundingBox(GroomAsset);
	const FBox CardsBounds = Mesh->GetBoundingBox();
	const double MeshesVolume = CardsBounds.GetVolume();
	const double StrandsVolume = StrandsBounds.GetVolume();
	const double OverlapVolume = StrandsBounds.Overlap(CardsBounds).GetVolume();
	if (OverlapVolume < FMath::Min(MeshesVolume, StrandsVolume) * 0.7)
	{
		Report->AddError({
			FText::Format(LOCTEXT("CardsNotAlignedToStrands", "{CardsMesh} Assigned to LOD {LodIndex} of Group {GroupIndex} on Groom Asset {AssetName} does not have cards aligned with the strands"), Args),
			Mesh
		}); // TODO - should this type of error be a warning and only an error if the overlap is less than 20% or something?
	}

	// 3015 card mesh UVs missing
	if (!VerifyMeshUVs(Mesh))
	{
		Report->AddWarning({
			FText::Format(LOCTEXT("MissingCardUVs", "{CardsMesh} Assigned to LOD {LodIndex} of Group {GroupIndex} on Groom Asset {AssetName} does not have valid UVs."), Args),
			Mesh
		});
	}
}

void VerifyMeshesInfoValid(const UGroomAsset* GroomAsset, const FHairGroupsMeshesSourceDescription& MeshesDescription, FFormatNamedArguments Args, UMetaHumanAssetReport* Report)
{
	Args.Add(TEXT("LodIndex"), MeshesDescription.LODIndex);
	Args.Add(TEXT("GroupIndex"), MeshesDescription.GroupIndex);

	// Helmet does not have any mesh data associated
	if (!MeshesDescription.ImportedMesh)
	{
		return;
	}

	const TObjectPtr<class UStaticMesh>& HairMesh = MeshesDescription.ImportedMesh;
	Args.Add(TEXT("MeshName"), FText::FromName(HairMesh.GetFName()));

	// 3016 helmet mesh not aligned with strand data - get extents and check overlap is 70% of the smallest volume
	const FBox StrandsBounds = GetStrandsCombinedBoundingBox(GroomAsset);
	const FBox MeshesBounds = HairMesh->GetBoundingBox();
	const double MeshesVolume = MeshesBounds.GetVolume();
	const double StrandsVolume = StrandsBounds.GetVolume();
	const double OverlapVolume = StrandsBounds.Overlap(MeshesBounds).GetVolume();
	if (OverlapVolume < FMath::Min(MeshesVolume, StrandsVolume) * 0.7)
	{
		Report->AddError({
			FText::Format(LOCTEXT("MeshesNotAlignedToStrands", "{MeshName} assigned to LOD {LodIndex} of Group {GroupIndex} on Groom Asset {AssetName} is not aligned with the strands"), Args),
			HairMesh
		}); // TODO - should this type of error be a warning and only an error if the overlap is less than 20% or something?
	}

	// 3017 helmet mesh missing UVs
	if (!VerifyMeshUVs(HairMesh))
	{
		Report->AddWarning({
			FText::Format(LOCTEXT("MeshesMissingUVs", "{MeshName} assigned to LOD {LodIndex} of Group {GroupIndex} on Groom Asset {AssetName} is missing UVs"), Args),
			HairMesh
		});
	}
}

void VerifyWardrobeItem(const UGroomBindingAsset* GroomBindingAsset, UMetaHumanAssetReport* Report)
{
	FString RootFolder = FPaths::GetPath(GroomBindingAsset->GetPathName());

	TArray<FAssetData> TopLevelItems;
	IAssetRegistry::GetChecked().GetAssetsByPath(FName(RootFolder), TopLevelItems);

	bool bWardrobeItemFound = false;

	for (const FAssetData& Item : TopLevelItems)
	{
		if (FPaths::GetBaseFilename(Item.PackageName.ToString()).StartsWith(TEXT("WI_")))
		{
			bWardrobeItemFound = true;
			FMetaHumanCharacterVerification::Get().VerifyGroomWardrobeItem(Item.GetAsset(), GroomBindingAsset, Report);
		}
	}

	// 3014 - Check for MetaHuman Wardrobe Item per asset (tbd)
	if (!bWardrobeItemFound)
	{
		Report->AddWarning({LOCTEXT("MissingWardrobeItem", "The package does not contain a Wardrobe Item. Certain features will not work or will be at default values")});
	}
}
} // namespace UE::MetaHuman::Private

void UVerifyMetaHumanGroom::Verify_Implementation(const UObject* ToVerify, UMetaHumanAssetReport* Report, const FMetaHumanVerificationOptions& Options) const
{
	using namespace UE::MetaHuman::Private;

	if (!ensureAsRuntimeWarning(ToVerify) || !ensureAsRuntimeWarning(Report))
	{
		return;
	}

	FFormatNamedArguments Args;
	Args.Add(TEXT("AssetName"), FText::FromString(ToVerify->GetName()));
	const UGroomBindingAsset* GroomBindingAsset = Cast<UGroomBindingAsset>(ToVerify);
	if (!GroomBindingAsset)
	{
		return;
	}


	const UGroomAsset* GroomAsset = GroomBindingAsset->GetGroom();

	// 3000 groom asset missing
	if (!GroomAsset)
	{
		Report->AddError({
			FText::Format(LOCTEXT("MissingGroom", "The Groom Binding {AssetName} does not have a valid Groom assigned"), Args),
			GroomBindingAsset
		});
		return;
	}

	// Basic validity test
	if (!GroomAsset->IsValid())
	{
		Report->AddError({
			FText::Format(LOCTEXT("GroomNotValid", "The Groom Asset {AssetName} is not a valid Groom"), Args),
			GroomAsset
		});
		return;
	}

	// Verify basic consistency
	if (!GroomAsset->AreGroupsValid())
	{
		Report->AddError({
			FText::Format(LOCTEXT("GroomGroupsNotValid", "The Groom Asset {AssetName} does not have valid Groups"), Args),
			GroomAsset
		});
	}

	// Check that Grooming mesh is present and correct
	USkeletalMesh* GroomMesh = VerifyGroomingMesh(GroomBindingAsset, Args, Report);
	if (!GroomMesh)
	{
		return;
	}

	if (Options.bVerifyPackagingRules)
	{
		//Check that any WardrobeItems present are correct
		VerifyWardrobeItem(GroomBindingAsset, Report);
	}

	// Checks for global properties that affect strands
	VerifyGlobalStrandsInfoValid(GroomAsset, Args, Report);

	// Check all the parts of the groom for validity against the various rules
	uint32 TotalGuideCount = 0;
	for (const FHairGroupInfoWithVisibility& GroupInfo : GroomAsset->GetHairGroupsInfo())
	{
		// Check per-group strands info
		VerifyGroupsStrandsInfoValid(GroomAsset, GroupInfo, Args, Report, GroomMesh);
		TotalGuideCount += GroupInfo.NumGuides;
	}
	for (const FHairGroupsCardsSourceDescription& Cards : GroomAsset->GetHairGroupsCards())
	{
		// Check per-group cards info
		VerifyCardsInfoValid(GroomAsset, Cards, Args, Report);
	}
	for (const FHairGroupsMeshesSourceDescription& Meshes : GroomAsset->GetHairGroupsMeshes())
	{
		// Check per-group meshes (helmets) info
		VerifyMeshesInfoValid(GroomAsset, Meshes, Args, Report);
	}

	const TArray<FHairGroupsMaterial>& Materials = GroomAsset->GetHairGroupsMaterials();
	for (int MaterialIndex = 0; MaterialIndex < Materials.Num(); MaterialIndex++)
	{
		if (!Materials[MaterialIndex].Material)
		{
			Args.Add(TEXT("MaterialIndex"), MaterialIndex);
			// 3012 Groom asset missing material
			Report->AddWarning({
				FText::Format(LOCTEXT("MissingMaterial", "The Material {MaterialIndex} on Groom Asset {AssetName} has not got a material set"), Args),
				GroomAsset
			});
		}
	}

	// 3018 Too many guides (UEFN specific)
	if (TotalGuideCount > 2000)
	{
		Report->AddWarning({
			FText::Format(LOCTEXT("UEFNStrandCountExceeded", "Groom Asset {AssetName} has more than 2000 guide curves making it unsuitable for use in UEFN"), Args),
			GroomAsset
		});
	}

	// 3019 LODs incomplete (UEFN specific)
}

#undef LOCTEXT_NAMESPACE
