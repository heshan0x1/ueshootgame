// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanGeometryRemoval.h"

#include "MetaHumanCharacterEditorLog.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "ImageCoreUtils.h"
#include "Logging/StructuredLog.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "SkeletalMeshTypes.h"

#define LOCTEXT_NAMESPACE "MetaHumanGeometryRemoval"

namespace UE::MetaHuman::GeometryRemoval
{

bool TryCombineHiddenFaceMaps(TConstArrayView<FHiddenFaceMapImage> SourceMaps, FHiddenFaceMapImage& OutDestinationMap, FText& OutFailureReason)
{
	if (SourceMaps.Num() == 0)
	{
		OutFailureReason = LOCTEXT("NoSourceMaps", "No source maps provided");
		return false;
	}

	// For simplicity, MaxCullValue and MinKeepValue are left on their defaults, rather than trying
	// to minimise scaling artifacts.
	OutDestinationMap.Settings.MaxShrinkDistance = 0.0f;
	OutDestinationMap.DebugName = TEXT("Combined map");

	int64 MaxWidth = 0;
	int64 MaxHeight = 0;
	for (const FHiddenFaceMapImage& SourceMap : SourceMaps)
	{
		if (SourceMap.Image.GetNumPixels() == 0)
		{
			OutFailureReason = FText::Format(LOCTEXT("NoSourceData", "Source map {0} has no source data and can't be used"), FText::FromString(SourceMap.DebugName));
			return false;
		}

		if (SourceMap.Settings.MaxCullValue < 0.0f
			|| SourceMap.Settings.MaxCullValue > SourceMap.Settings.MinKeepValue
			|| SourceMap.Settings.MinKeepValue > 1.0f)
		{
			OutFailureReason = FText::Format(LOCTEXT("InvalidSourceSettings", "Source map {0} has invalid values for MinKeepValue ({1}) and MaxCullValue ({2}) and can't be used"), 
				FText::FromString(SourceMap.DebugName), SourceMap.Settings.MinKeepValue, SourceMap.Settings.MaxCullValue);

			return false;
		}

		MaxWidth = FMath::Max(MaxWidth, SourceMap.Image.GetWidth());
		MaxHeight = FMath::Max(MaxHeight, SourceMap.Image.GetHeight());
		OutDestinationMap.Settings.MaxShrinkDistance = FMath::Max(OutDestinationMap.Settings.MaxShrinkDistance, SourceMap.Settings.MaxShrinkDistance);
	}

	// The checks above guarantee this
	check(MaxWidth > 0 && MaxHeight > 0);

	OutDestinationMap.Image = FImage(MaxWidth, MaxHeight, ERawImageFormat::G8, EGammaSpace::Linear);

	// G8 should have 1 byte per pixel
	check(OutDestinationMap.Image.GetBytesPerPixel() == 1);

	TArrayView64<uint8> DestinationPixels = OutDestinationMap.Image.AsG8();

	// Stores information about the source map needed in the loop below, and handles locking it for reading.
	struct FSourceHiddenFaceMapData
	{
		FSourceHiddenFaceMapData(const FHiddenFaceMapImage& InSourceMap)
			: ResX(InSourceMap.Image.GetWidth())
			, ResY(InSourceMap.Image.GetHeight())
			, ResXAsDouble(static_cast<double>(ResX))
			, ResYAsDouble(static_cast<double>(ResY))
			, SourceMap(InSourceMap)
		{
		}

		void EvaluatePixel(int64 PosX, int64 PosY, bool& bOutKeepGeometry, float& OutShrinkDistance) const
		{
			check(PosX >= 0 && PosX < ResX);
			check(PosY >= 0 && PosY < ResY);

			const FLinearColor Color = SourceMap.Image.GetOnePixelLinear(PosX, PosY);

			// For backwards compatibility with the previous implementation, all 3 channels have to 
			// be under the threshold in order to cull the geometry.
			const float SelectedValue = FMath::Max3(Color.R, Color.G, Color.B);

			bOutKeepGeometry = SelectedValue >= SourceMap.Settings.MaxCullValue;

			OutShrinkDistance = 0.0f;
			if (SourceMap.Settings.MaxShrinkDistance > 0.0f
				&& SourceMap.Settings.MinKeepValue > SourceMap.Settings.MaxCullValue
				&& bOutKeepGeometry
				&& SelectedValue < SourceMap.Settings.MinKeepValue)
			{
				const float ShrinkFactor = 1.0f - (SelectedValue - SourceMap.Settings.MaxCullValue) / (SourceMap.Settings.MinKeepValue - SourceMap.Settings.MaxCullValue);
				
				// This should be guaranteed by the logic above
				check(ShrinkFactor >= 0.0f && ShrinkFactor <= 1.0f);

				OutShrinkDistance = ShrinkFactor * SourceMap.Settings.MaxShrinkDistance;
			}
		}

		const int64 ResX;
		const int64 ResY;
		// Store as a double as well, to avoid converting from int64 in inner loop
		const double ResXAsDouble;
		const double ResYAsDouble;

	private:
		const FHiddenFaceMapImage& SourceMap;
	};

	TArray<FSourceHiddenFaceMapData> SourceMapData;
	for (const FHiddenFaceMapImage& SourceMap : SourceMaps)
	{
		SourceMapData.Emplace(SourceMap);
	}

	// For every pixel in the destination map, read from the source maps to determine the result
	const double DestResX = (double)MaxWidth;
	const double DestResY = (double)MaxHeight;
	for (int64 DestPosY = 0; DestPosY < MaxHeight; DestPosY++)
	{
		const double V = ((double)DestPosY + 0.5) / DestResY;

		for (int64 DestPosX = 0; DestPosX < MaxWidth; DestPosX++)
		{
			float AccumulatedPixelValue = 1.0f;
			for (const FSourceHiddenFaceMapData& SourceMap : SourceMapData)
			{
				const double U = ((double)DestPosX + 0.5) / DestResX;

				const int64 SourcePosX = FMath::FloorToInt64(U * SourceMap.ResXAsDouble);
				const int64 SourcePosY = FMath::FloorToInt64(V * SourceMap.ResYAsDouble);
				check(SourcePosX >= 0 && SourcePosX < SourceMap.ResX);
				check(SourcePosY >= 0 && SourcePosY < SourceMap.ResY);

				bool bKeepGeometry = false;
				float ShrinkDistance = 0.0f;
				SourceMap.EvaluatePixel(SourcePosX, SourcePosY, bKeepGeometry, ShrinkDistance);

				float NewPixelValue;
				if (bKeepGeometry)
				{
					if (ShrinkDistance == 0.0f)
					{
						// Geometry should be unmodified
						NewPixelValue = 1.0f;
					}
					else
					{
						const float ShrinkFactor = ShrinkDistance / OutDestinationMap.Settings.MaxShrinkDistance;
						NewPixelValue = FMath::Lerp(OutDestinationMap.Settings.MinKeepValue, OutDestinationMap.Settings.MaxCullValue, ShrinkFactor);
					}
				}
				else
				{
					// Cull geometry
					NewPixelValue = 0.0f;
				}

				// If any of the source maps wants to hide this pixel, it will be hidden.
				//
				// If any of them want to shrink it, it will be shrunk by the highest amount requested.
				AccumulatedPixelValue = FMath::Min(AccumulatedPixelValue, NewPixelValue);
			}

			// No need to multiply by format data size here, as it's guaranteed to be 1
			const int64 DestArrayPos = DestPosX + (DestPosY * DestResX);
			DestinationPixels[DestArrayPos] = static_cast<uint8>(FMath::Clamp(AccumulatedPixelValue * MAX_uint8, 0.0f, static_cast<float>(MAX_uint8)));
		}
	}

	return true;
}

bool TryConvertHiddenFaceMapTexturesToImages(TConstArrayView<FHiddenFaceMapTexture> SourceMaps, TArray<FHiddenFaceMapImage>& OutImages, FText& OutFailureReason)
{
	OutImages.Reset(SourceMaps.Num());

	for (const FHiddenFaceMapTexture& SourceMap : SourceMaps)
	{
		if (!SourceMap.Texture)
		{
			continue;
		}
		
		FHiddenFaceMapImage& DestinationMap = OutImages.AddDefaulted_GetRef();

		if (!SourceMap.Texture->Source.GetMipImage(DestinationMap.Image, 0)
			|| DestinationMap.Image.GetNumPixels() == 0)
		{
			OutFailureReason = FText::Format(LOCTEXT("FailedToGetMipData", "Failed to get mip data from hidden face map {0}"), FText::FromString(SourceMap.Texture->GetPathName()));

			return false;
		}

		DestinationMap.DebugName = SourceMap.Texture->GetPathName();
		DestinationMap.Settings = SourceMap.Settings;
	}

	return true;
}

void UpdateHiddenFaceMapTextureFromImage(const FImage& Image, TNotNull<UTexture2D*> Texture)
{
	Texture->PreEditChange(nullptr);

	Texture->Source.Init(Image);
	// Hidden face maps are linear
	Texture->SRGB = false;

	Texture->PostEditChange();
}

// The following code has been adapted from FLODUtilities::StripLODGeometry

namespace TriangleStripHelper
{
	struct FTriangle2D
	{
		FVector2D Vertices[3];
	};

	bool IntersectTriangleAndAABB(const FTriangle2D& Triangle, const FBox2D& Box)
	{
		FBox2D TriangleBox(Triangle.Vertices[0], Triangle.Vertices[0]);
		TriangleBox += Triangle.Vertices[1];
		TriangleBox += Triangle.Vertices[2];

		auto IntersectBoxes = [&TriangleBox, &Box]()-> bool
		{
			if ((FMath::RoundToInt(TriangleBox.Min.X) >= FMath::RoundToInt(Box.Max.X)) || (FMath::RoundToInt(Box.Min.X) >= FMath::RoundToInt(TriangleBox.Max.X)))
			{
				return false;
			}

			if ((FMath::RoundToInt(TriangleBox.Min.Y) >= FMath::RoundToInt(Box.Max.Y)) || (FMath::RoundToInt(Box.Min.Y) >= FMath::RoundToInt(TriangleBox.Max.Y)))
			{
				return false;
			}

			return true;
		};

		//If the triangle box do not intersect, return false
		if (!IntersectBoxes())
		{
			return false;
		}

		auto IsInsideBox = [&Box](const FVector2D& TestPoint)->bool
		{
			return ((FMath::RoundToInt(TestPoint.X) >= FMath::RoundToInt(Box.Min.X)) &&
					(FMath::RoundToInt(TestPoint.X) <= FMath::RoundToInt(Box.Max.X)) &&
					(FMath::RoundToInt(TestPoint.Y) >= FMath::RoundToInt(Box.Min.Y)) &&
					(FMath::RoundToInt(TestPoint.Y) <= FMath::RoundToInt(Box.Max.Y)) );
		};

		if( IsInsideBox(Triangle.Vertices[0]) ||
			IsInsideBox(Triangle.Vertices[1]) ||
			IsInsideBox(Triangle.Vertices[2]) )
		{
			return true;
		}

		auto SegmentIntersection2D = [](const FVector2D & SegmentStartA, const FVector2D & SegmentEndA, const FVector2D & SegmentStartB, const FVector2D & SegmentEndB)
		{
			const FVector2D VectorA = SegmentEndA - SegmentStartA;
			const FVector2D VectorB = SegmentEndB - SegmentStartB;

			const double S = (-VectorA.Y * (SegmentStartA.X - SegmentStartB.X) + VectorA.X * (SegmentStartA.Y - SegmentStartB.Y)) / (-VectorB.X * VectorA.Y + VectorA.X * VectorB.Y);
			const double T = (VectorB.X * (SegmentStartA.Y - SegmentStartB.Y) - VectorB.Y * (SegmentStartA.X - SegmentStartB.X)) / (-VectorB.X * VectorA.Y + VectorA.X * VectorB.Y);

			return (S >= 0 && S <= 1 && T >= 0 && T <= 1);
		};

		auto IsInsideTriangle = [&Triangle, &SegmentIntersection2D, &Box, &TriangleBox](const FVector2D& TestPoint)->bool
		{
			double Extent = (2.0 * Box.GetSize().Size()) + (2.0 * TriangleBox.GetSize().Size());
			FVector2D TestPointExtend(Extent, Extent);
			int32 IntersectionCount = SegmentIntersection2D(Triangle.Vertices[0], Triangle.Vertices[1], TestPoint, TestPoint + TestPointExtend) ? 1 : 0;
			IntersectionCount += SegmentIntersection2D(Triangle.Vertices[1], Triangle.Vertices[2], TestPoint, TestPoint + TestPointExtend) ? 1 : 0;
			IntersectionCount += SegmentIntersection2D(Triangle.Vertices[2], Triangle.Vertices[0], TestPoint, TestPoint + TestPointExtend) ? 1 : 0;
			return (IntersectionCount == 1);
		};
	
		if (IsInsideTriangle(Box.Min) ||
			IsInsideTriangle(Box.Max) ||
			IsInsideTriangle(FVector2D(Box.Min.X, Box.Max.Y)) ||
			IsInsideTriangle(FVector2D(Box.Max.X, Box.Min.Y)))
		{
			return true;
		}

		auto IsTriangleEdgeIntersectBoxEdges = [&SegmentIntersection2D, &Box]( const FVector2D& EdgeStart, const FVector2D& EdgeEnd)->bool
		{
			//Triangle Edges 0-1 intersection with box
			if( SegmentIntersection2D(EdgeStart, EdgeEnd, Box.Min, FVector2D(Box.Min.X, Box.Max.Y)) ||
				SegmentIntersection2D(EdgeStart, EdgeEnd, Box.Max, FVector2D(Box.Min.X, Box.Max.Y)) ||
				SegmentIntersection2D(EdgeStart, EdgeEnd, Box.Max, FVector2D(Box.Max.X, Box.Min.Y)) ||
				SegmentIntersection2D(EdgeStart, EdgeEnd, Box.Min, FVector2D(Box.Max.X, Box.Min.Y)) )
			{
				return true;
			}
			return false;
		};

		if( IsTriangleEdgeIntersectBoxEdges(Triangle.Vertices[0], Triangle.Vertices[1]) ||
			IsTriangleEdgeIntersectBoxEdges(Triangle.Vertices[1], Triangle.Vertices[2]) || 
			IsTriangleEdgeIntersectBoxEdges(Triangle.Vertices[2], Triangle.Vertices[0]))
		{
			return true;
		}
		return false;
	}
} //End namespace TriangleStripHelper

bool RemoveAndShrinkGeometry(
	TNotNull<USkeletalMesh*> SkeletalMesh, 
	const int32 LODIndex,
	const FHiddenFaceMapImage& HiddenFaceMap,
	TConstArrayView<FName> MaterialSlotsToProcess)
{
	if (LODIndex < 0 || LODIndex >= SkeletalMesh->GetLODNum() || !SkeletalMesh->GetImportedModel() || !SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(LODIndex))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Cannot strip triangle for skeletal mesh {Mesh} LOD {LOD}", SkeletalMesh->GetPathName(), LODIndex);
		return false;
	}

	// Aliases to reduce code verbosity
	const float MaxCullValue = HiddenFaceMap.Settings.MaxCullValue;
	const float MinKeepValue = HiddenFaceMap.Settings.MinKeepValue;
	const float MaxShrinkDistance = HiddenFaceMap.Settings.MaxShrinkDistance;

	if (MaxCullValue < 0.0f
		|| MaxCullValue > MinKeepValue
		|| MinKeepValue > 1.0f)
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Cannot strip triangle for skeletal mesh {Mesh} LOD {LOD}. MaxCullValue and MinKeepValue must be in the range 0..1, and MinKeepValue must be greater than or equal to MaxCullValue.",
			SkeletalMesh->GetPathName(), LODIndex);
		return false;
	}
	
	// Grab the reference data
	FSkeletalMeshLODModel& LODModel = SkeletalMesh->GetImportedModel()->LODModels[LODIndex];
	const FSkeletalMeshLODInfo& LODInfo = *(SkeletalMesh->GetLODInfo(LODIndex));
	const bool bIsReductionActive = SkeletalMesh->IsReductionActive(LODIndex);
	if (bIsReductionActive && LODInfo.ReductionSettings.BaseLOD < LODIndex)
	{
		// No need to strip if the LOD is reduce using another LOD as the source data
		UE_LOGFMT(LogMetaHumanCharacterEditor, Warning, "Cannot strip triangle for skeletal mesh {Mesh} LOD {LOD}, because this LOD is generated. Strip the source instead.",
			SkeletalMesh->GetPathName(), LODIndex);
		return false;
	}

	// Check that the image is valid
	const float ResX = static_cast<float>(HiddenFaceMap.Image.SizeX);
	const float ResY = static_cast<float>(HiddenFaceMap.Image.SizeY);
	if (HiddenFaceMap.Image.GetNumPixels() == 0)
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Warning, "Cannot strip triangle for skeletal mesh {Mesh} LOD {LOD}, because the image size is 0.", SkeletalMesh->GetPathName(), LODIndex);
		return false;
	}

	const ERawImageFormat::Type RawFormat = HiddenFaceMap.Image.Format;
	if (RawFormat >= ERawImageFormat::MAX)
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Warning, "Cannot strip triangle for skeletal mesh {Mesh} LOD {LOD}, because the image format is invalid.", SkeletalMesh->GetPathName(), LODIndex);
		return false;
	}

	// Post edit change scope
	{
		FScopedSkeletalMeshPostEditChange ScopePostEditChange(SkeletalMesh);
		// This is like a re-import, we must force to use a new DDC key
		SkeletalMesh->InvalidateDeriveDataCacheGUID();
		const bool bBuildAvailable = SkeletalMesh->HasMeshDescription(LODIndex);
		FSkeletalMeshImportData ImportedData;
		// Get the imported data if available
		if (bBuildAvailable)
		{
			if (const FMeshDescription* MeshDescription = SkeletalMesh->GetMeshDescription(LODIndex))
			{
				if (!MeshDescription->IsEmpty())
				{
					ImportedData = FSkeletalMeshImportData::CreateFromMeshDescription(*MeshDescription);
				}
			}
		}
		else
		{
			UE_LOGFMT(LogMetaHumanCharacterEditor, Warning, "Cannot strip triangle for skeletalmesh {Mesh} LOD {LOD}, because it has no mesh description", SkeletalMesh->GetPathName(), LODIndex);
			return false;
		}
		SkeletalMesh->Modify();
		
		auto GetPixelValue = [&HiddenFaceMap](int32 PosX, int32 PosY) -> float
		{
			const FLinearColor Color = HiddenFaceMap.Image.GetOnePixelLinear(PosX, PosY);

			// Note that the alpha value is not used
			return Color.R;
		};

		auto ShouldStripTriangle = [&](const FVector2f& UvA, const FVector2f& UvB, const FVector2f& UvC) -> bool
		{
			// Note that the vertex UVs are wrapped to the 0-1 range here.
			//
			// Triangles that cross an integer boundary will be handled incorrectly, e.g. if two
			// neighboring verts have U values 0.9 and 1.1, they will be wrapped to 0.9 and 0.1, so
			// the region of the hidden face map covered by that triangle will be calculated 
			// incorrectly.
			//
			// This is a known issue that doesn't affect MetaHuman faces or bodies, which don't 
			// contain any triangles that cross integer UV boundaries. It could be addressed in 
			// future, but it's not a priority for now.
			const FIntVector2 PixelUvA(FMath::FloorToInt32(FMath::Wrap(UvA.X * ResX, 0.0f, ResX - 1.0f)), FMath::FloorToInt32(FMath::Wrap(UvA.Y * ResY, 0.0f, ResY - 1.0f)));
			const FIntVector2 PixelUvB(FMath::FloorToInt32(FMath::Wrap(UvB.X * ResX, 0.0f, ResX - 1.0f)), FMath::FloorToInt32(FMath::Wrap(UvB.Y * ResY, 0.0f, ResY - 1.0f)));
			const FIntVector2 PixelUvC(FMath::FloorToInt32(FMath::Wrap(UvC.X * ResX, 0.0f, ResX - 1.0f)), FMath::FloorToInt32(FMath::Wrap(UvC.Y * ResY, 0.0f, ResY - 1.0f)));

			const int32 MinU = FMath::Min3<int32>(PixelUvA.X, PixelUvB.X, PixelUvC.X);
			const int32 MinV = FMath::Min3<int32>(PixelUvA.Y, PixelUvB.Y, PixelUvC.Y);
			const int32 MaxU = FMath::Max3<int32>(PixelUvA.X, PixelUvB.X, PixelUvC.X);
			const int32 MaxV = FMath::Max3<int32>(PixelUvA.Y, PixelUvB.Y, PixelUvC.Y);

			// Triangle smaller or equal to one pixel. Just need to test the pixel color value
			if (MinU == MaxU || MinV == MaxV)
			{
				return GetPixelValue(MinU, MinV) <= MaxCullValue;
			}

			for (int32 PosY = MinV; PosY <= MaxV; ++PosY)
			{
				for (int32 PosX = MinU; PosX <= MaxU; ++PosX)
				{
					const bool bStripPixel = GetPixelValue(PosX, PosY) <= MaxCullValue;

					// If any non-zeroed pixel intersects the triangle, prevent stripping of this triangle
					if (!bStripPixel)
					{
						FVector2D StartPixel(PosX, PosY);
						FVector2D EndPixel(PosX + 1, PosY + 1);
						FBox2D Box2D(StartPixel, EndPixel);
						// Test if the triangle UV touches this pixel
						TriangleStripHelper::FTriangle2D Triangle;
						Triangle.Vertices[0] = FVector2D(PixelUvA.X, PixelUvA.Y);
						Triangle.Vertices[1] = FVector2D(PixelUvB.X, PixelUvB.Y);
						Triangle.Vertices[2] = FVector2D(PixelUvC.X, PixelUvC.Y);
						if(TriangleStripHelper::IntersectTriangleAndAABB(Triangle, Box2D))
						{
							return false;
						}
					}
				}
			}
			return true;
		};

		const TArray<uint32>& SoftVertexIndexToImportDataPointIndex = LODModel.GetRawPointIndices();

		TMap<FInt32Vector3, int32> OptimizedFaceFinder;

		for (int32 FaceIndex = 0; FaceIndex < ImportedData.Faces.Num(); ++FaceIndex)
		{
			const SkeletalMeshImportData::FTriangle& Face = ImportedData.Faces[FaceIndex];
			int32 FaceVertexA = ImportedData.Wedges[Face.WedgeIndex[0]].VertexIndex;
			int32 FaceVertexB = ImportedData.Wedges[Face.WedgeIndex[1]].VertexIndex;
			int32 FaceVertexC = ImportedData.Wedges[Face.WedgeIndex[2]].VertexIndex;

			OptimizedFaceFinder.Add(FInt32Vector3(FaceVertexA, FaceVertexB, FaceVertexC), FaceIndex);
		}

		TArray<bool> ShouldStripSection;
		ShouldStripSection.Reserve(LODModel.Sections.Num());
		for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
		{
			bool bShouldStripThisSection;
			if (MaterialSlotsToProcess.Num() == 0)
			{
				// Apply to all sections if no material slots were specified
				bShouldStripThisSection = true;
			}
			else
			{
				const TArray<FSkeletalMaterial>& Materials = SkeletalMesh->GetMaterials();
				if (Materials.IsValidIndex(LODModel.Sections[SectionIndex].MaterialIndex))
				{
					bShouldStripThisSection = MaterialSlotsToProcess.Contains(Materials[LODModel.Sections[SectionIndex].MaterialIndex].MaterialSlotName);
				}
				else
				{
					bShouldStripThisSection = false;
				}
			}

			ShouldStripSection.Add(bShouldStripThisSection);
		}

		int32 RemovedFaceCount = 0;
		TBitArray<> FaceToRemove;
		FaceToRemove.Init(false, ImportedData.Faces.Num());
		int32 NumTriangleIndex = LODModel.IndexBuffer.Num();
		for (int32 TriangleIndex = NumTriangleIndex - 1; TriangleIndex >= 0; TriangleIndex -= 3)
		{
			int32 VertexIndexA = LODModel.IndexBuffer[TriangleIndex - 2];
			int32 VertexIndexB = LODModel.IndexBuffer[TriangleIndex - 1];
			int32 VertexIndexC = LODModel.IndexBuffer[TriangleIndex];
			int32 SectionIndex;
			int32 SectionVertexIndexA;
			int32 SectionVertexIndexB;
			int32 SectionVertexIndexC;
			LODModel.GetSectionFromVertexIndex(VertexIndexA, SectionIndex, SectionVertexIndexA);
			LODModel.GetSectionFromVertexIndex(VertexIndexB, SectionIndex, SectionVertexIndexB);
			LODModel.GetSectionFromVertexIndex(VertexIndexC, SectionIndex, SectionVertexIndexC);

			if (!ShouldStripSection[SectionIndex])
			{
				continue;
			}

			FSkelMeshSection& Section = LODModel.Sections[SectionIndex];
			//Get the UV triangle, add the small number that will act like threshold when converting the UV into pixel coordinate.
			const FVector2f UvA = Section.SoftVertices[SectionVertexIndexA].UVs[0] + UE_KINDA_SMALL_NUMBER;
			const FVector2f UvB = Section.SoftVertices[SectionVertexIndexB].UVs[0] + UE_KINDA_SMALL_NUMBER;
			const FVector2f UvC = Section.SoftVertices[SectionVertexIndexC].UVs[0] + UE_KINDA_SMALL_NUMBER;

			if (ShouldStripTriangle(UvA, UvB, UvC))
			{
				// Find the face in the imported data
				int32 ImportedPointIndexA = SoftVertexIndexToImportDataPointIndex[VertexIndexA];
				int32 ImportedPointIndexB = SoftVertexIndexToImportDataPointIndex[VertexIndexB];
				int32 ImportedPointIndexC = SoftVertexIndexToImportDataPointIndex[VertexIndexC];
				int32 FaceIndex = OptimizedFaceFinder.FindChecked(FInt32Vector3(ImportedPointIndexA, ImportedPointIndexB, ImportedPointIndexC));
				if (!FaceToRemove[FaceIndex])
				{
					FaceToRemove[FaceIndex] = true;
					RemovedFaceCount++;
				}
			}
		}

		// Stores data about a point in the unmodified ImportedData
		struct FPointData
		{
			// The delta from the original point to the shrunk point
			FVector3f ShrinkDelta = FVector3f::ZeroVector;
			// The number of times the point has been shrunk
			int32 ShrinkCount = 0;

			// The index of the shrunk version of this point in StrippedImportedData
			int32 StrippedVertexIndex_Shrunk = INDEX_NONE;
			// The index of the unmodified version of this point in StrippedImportedData
			int32 StrippedVertexIndex_Unshrunk = INDEX_NONE;

			// True if this point's value in the hidden face map would cause it to be removed.
			// 
			// Note that the point will only actually be removed if all triangles using it are 
			// removed. If there's a triangle that uses this point, and other points on that 
			// triangle are not removed, this point will be kept as well.
			bool bShouldBeCulled = false;
		};

		bool bAppliedAnyShrinking = false;
		// Tracks info about how points in ImportedData are being modified. Indices match ImportedData.Points
		TArray<FPointData> PointData;
		PointData.Reserve(ImportedData.Points.Num());
		PointData.AddDefaulted(ImportedData.Points.Num());

		if (MaxShrinkDistance > 0.0f && MinKeepValue > MaxCullValue)
		{
			check(LODModel.NumVertices <= (uint32)MAX_int32);
			for (int32 VertexIndex = 0; VertexIndex < (int32)LODModel.NumVertices; VertexIndex++)
			{
				int32 SectionIndex = 0;
				int32 SectionVertexIndex = 0;
				LODModel.GetSectionFromVertexIndex(VertexIndex, SectionIndex, SectionVertexIndex);

				if (!ShouldStripSection[SectionIndex])
				{
					continue;
				}

				const FSoftSkinVertex& Vertex = LODModel.Sections[SectionIndex].SoftVertices[SectionVertexIndex];

				// Get the UV triangle, add the small number that will act like threshold when converting the UV into pixel coordinate.
				const FVector2f UV = Vertex.UVs[0] + KINDA_SMALL_NUMBER;
				const FIntVector2 PixelUV(FMath::FloorToInt32(FMath::Wrap(UV.X * ResX, 0.0f, ResX - 1.0f)), FMath::FloorToInt32(FMath::Wrap(UV.Y * ResY, 0.0f, ResY - 1.0f)));

				const float PixelValue = GetPixelValue(PixelUV.X, PixelUV.Y);
				
				// A value at or below the MaxCullValue, should translate to a ShrinkWeight of 1.
				//
				// A value at or above the MinKeepValue, should give a ShrinkWeight of 0.
				//
				// Values in between the thresholds should linearly interpolate from 0 to 1.
				const float ShrinkWeight = 1.0f - FMath::Clamp((PixelValue - MaxCullValue) / (MinKeepValue - MaxCullValue), 0.0f, 1.0f);

				if (ShrinkWeight > 0.0f)
				{
					FPointData& Point = PointData[SoftVertexIndexToImportDataPointIndex[VertexIndex]];
					Point.ShrinkDelta -= Vertex.TangentZ * (MaxShrinkDistance * ShrinkWeight);
					Point.ShrinkCount++;
					// This assumes that different verts referencing the same point (e.g. verts on 
					// a hard edge) have the same UV. This isn't guaranteed to be true for all 
					// meshes, but should be true for the meshes this function is intended to be 
					// used on.
					Point.bShouldBeCulled = PixelValue <= MaxCullValue;

					bAppliedAnyShrinking = true;
				}
			}
		}
		
		if (RemovedFaceCount > 0 || bAppliedAnyShrinking)
		{
			// Recreate a new imported data with only the remaining faces
			FSkeletalMeshImportData StrippedImportedData;
			StrippedImportedData = ImportedData;
			StrippedImportedData.Faces.Reset();
			StrippedImportedData.Wedges.Reset();
			StrippedImportedData.Points.Reset();
			StrippedImportedData.PointToRawMap.Reset();
			StrippedImportedData.Influences.Reset();

			for (int32 MorphTargetIndex = 0; MorphTargetIndex < ImportedData.MorphTargets.Num(); MorphTargetIndex++)
			{
				StrippedImportedData.MorphTargets[MorphTargetIndex].Empty();
				StrippedImportedData.MorphTargets[MorphTargetIndex].Points.Reserve(ImportedData.MorphTargets[MorphTargetIndex].Points.Num());
				
				StrippedImportedData.MorphTargetModifiedPoints[MorphTargetIndex].Reset();
			}

			StrippedImportedData.Faces.AddDefaulted(ImportedData.Faces.Num() - RemovedFaceCount);
			StrippedImportedData.Wedges.AddDefaulted(StrippedImportedData.Faces.Num() * 3);
			int32 NewFaceIndex = 0;
			int32 NewWedgeIndex = 0;
			for (int32 FaceIndex = 0; FaceIndex < ImportedData.Faces.Num(); ++FaceIndex)
			{
				// Skip removed faces
				if (FaceToRemove[FaceIndex])
				{
					continue;
				}

				bool bHasAnyUnculledShrunkPoints = false;
				for (int32 FaceWedgeIndex = 0; FaceWedgeIndex < 3; ++FaceWedgeIndex)
				{
					const FPointData& Point = PointData[ImportedData.Wedges[ImportedData.Faces[FaceIndex].WedgeIndex[FaceWedgeIndex]].VertexIndex];

					if (!Point.bShouldBeCulled
						&& Point.ShrinkCount > 0)
					{
						bHasAnyUnculledShrunkPoints = true;
						break;
					}
				}

				SkeletalMeshImportData::FTriangle& NewFace = StrippedImportedData.Faces[NewFaceIndex++];
				NewFace = ImportedData.Faces[FaceIndex];
				for (int32 FaceWedgeIndex = 0; FaceWedgeIndex < 3; ++FaceWedgeIndex)
				{
					SkeletalMeshImportData::FVertex& NewWedge = StrippedImportedData.Wedges[NewWedgeIndex];
					NewWedge = ImportedData.Wedges[NewFace.WedgeIndex[FaceWedgeIndex]];
					NewFace.WedgeIndex[FaceWedgeIndex] = NewWedgeIndex;
					const int32 VertexIndex = NewWedge.VertexIndex;

					FPointData& Point = PointData[VertexIndex];

					// Importantly, a culled point should only be shrunk if any of the visible 
					// points it's connected to have also been shrunk. 
					//
					// This is because there are two cases that need to be handled differently:
					//
					// 1. 	On a hard border between unmodified geometry and completely culled 
					//		geometry, we don't want geometry on the edge to be unintentionally 
					// 		shrunk, as the hidden face map has not requested any shrinking.
					//
					//		This is particularly an issue on meshes that recompute normals at
					//		runtime, since the unintentionally shrunk edge points will affect
					//		the normals of neighbouring visible points, changing their appearance.
					//
					// 2.	On a border between shrunk geometry and culled geometry, we want the
					//		geometry on the edge to be shrunk as well. 
					//
					//		If we forced all culled points not to be shrunk in order to satisfy 
					// 		case 1, then on triangles that span shrunk and culled points (and are 
					// 		not themselves culled, because the shrunk points are visible), the 
					// 		culled points would be in their original positions and therefore might 
					// 		intersect with geometry in front of them.
					//
					// This logic ensures that each case is handled correctly. Sometimes this can 
					// mean that a single point in the original mesh needs both shrunk and unshrunk
					// versions in the new mesh, so in some cases the new mesh can end up with more
					// points than the original.
					const bool bShouldApplyShrink = Point.ShrinkCount > 0 && bHasAnyUnculledShrunkPoints;
					int32& NewVertexIndex = bShouldApplyShrink ? Point.StrippedVertexIndex_Shrunk : Point.StrippedVertexIndex_Unshrunk;

					if (NewVertexIndex == INDEX_NONE)
					{
						FVector3f NewPointPosition = ImportedData.Points[VertexIndex];

						// Apply accumulated shrink if any
						if (bShouldApplyShrink)
						{
							NewPointPosition += Point.ShrinkDelta / Point.ShrinkCount;
						}

						StrippedImportedData.PointToRawMap.Add(ImportedData.PointToRawMap[VertexIndex]);
						NewWedge.VertexIndex = StrippedImportedData.Points.Add(NewPointPosition);
						NewVertexIndex = NewWedge.VertexIndex;
					}
					else
					{
						NewWedge.VertexIndex = NewVertexIndex;
					}
					NewWedgeIndex++;
				}
			}
			
			// Fix the influences with the RemapVertexIndex
			for (int32 InfluenceIndex = 0; InfluenceIndex < ImportedData.Influences.Num(); ++InfluenceIndex)
			{
				auto AddInfluence = [&ImportedData, &StrippedImportedData, InfluenceIndex](int32 RemappedVertexIndex)
				{
					if (RemappedVertexIndex != INDEX_NONE)
					{
						SkeletalMeshImportData::FRawBoneInfluence& Influence = StrippedImportedData.Influences.Add_GetRef(ImportedData.Influences[InfluenceIndex]);
						Influence.VertexIndex = RemappedVertexIndex;
					}
				};
				
				const FPointData& Point = PointData[ImportedData.Influences[InfluenceIndex].VertexIndex];
				AddInfluence(Point.StrippedVertexIndex_Shrunk);
				AddInfluence(Point.StrippedVertexIndex_Unshrunk);
			}

			for (int32 MorphTargetIndex = ImportedData.MorphTargets.Num() - 1; MorphTargetIndex >= 0; MorphTargetIndex--)
			{
				int32 MorphTargetPointIndex = 0;
				for (const uint32 ModifiedPointIndex : ImportedData.MorphTargetModifiedPoints[MorphTargetIndex])
				{
					// Any shrunk points are assumed to be hidden under other geometry and are 
					// therefore excluded from morph targets.
					if (PointData[ModifiedPointIndex].StrippedVertexIndex_Unshrunk != INDEX_NONE
						&& PointData[ModifiedPointIndex].ShrinkCount == 0)
					{
						// Copy the point over from the original morph target
						StrippedImportedData.MorphTargetModifiedPoints[MorphTargetIndex].Add(PointData[ModifiedPointIndex].StrippedVertexIndex_Unshrunk);
						StrippedImportedData.MorphTargets[MorphTargetIndex].Points.Add(ImportedData.MorphTargets[MorphTargetIndex].Points[MorphTargetPointIndex]);
					}

					MorphTargetPointIndex++;
				}

				if (StrippedImportedData.MorphTargetModifiedPoints[MorphTargetIndex].Num() == 0)
				{
					StrippedImportedData.MorphTargets.RemoveAtSwap(MorphTargetIndex);
					StrippedImportedData.MorphTargetModifiedPoints.RemoveAtSwap(MorphTargetIndex);
					StrippedImportedData.MorphTargetNames.RemoveAtSwap(MorphTargetIndex);
				}
			}

			FMeshDescription MeshDescription;
			if (StrippedImportedData.GetMeshDescription(nullptr, &SkeletalMesh->GetLODInfo(LODIndex)->BuildSettings, MeshDescription))
			{
				SkeletalMesh->CreateMeshDescription(LODIndex, MoveTemp(MeshDescription));
				SkeletalMesh->CommitMeshDescription(LODIndex);
			}
		}
	}
	return true;
}

} // namespace UE::MetaHuman::GeometryRemoval

#undef LOCTEXT_NAMESPACE
