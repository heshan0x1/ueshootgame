// Copyright Epic Games, Inc. All Rights Reserved.

#include "NaniteBuilder.h"
#include "Modules/ModuleManager.h"
#include "Components.h"
#include "StaticMeshResources.h"
#include "Rendering/NaniteResources.h"
#include "Hash/CityHash.h"
#include "Cluster.h"
#include "ClusterDAG.h"
#include "GraphPartitioner.h"
#include "NaniteDefinitions.h"
#include "NaniteIntermediateResources.h"
#include "NaniteAssemblyBuild.h"
#include "Async/ParallelFor.h"
#include "Encode/NaniteEncode.h"
#include "ImposterAtlas.h"
#include "UObject/DevObjectVersion.h"
#include "Compression/OodleDataCompressionUtil.h"
#include "Engine/StaticMesh.h"

#if WITH_MIKKTSPACE
#include "mikktspace.h"
#endif

#define NANITE_LOG_COMPRESSED_SIZES		0
#define NANITE_STRIP_DATA				0

#define NANITE_EVALUATE_COARSE_REPRESENTATION	0

#if NANITE_IMPOSTERS_SUPPORTED
static TAutoConsoleVariable<bool> CVarBuildImposters(
	TEXT("r.Nanite.Builder.Imposters"),
	false,
	TEXT("Build imposters for small/distant object rendering. For scenes with lots of small or distant objects, imposters can sometimes speed up rendering, but they come at the cost of additional runtime memory and disk footprint overhead."),
	ECVF_ReadOnly
);
#endif

static TAutoConsoleVariable<int32> CVarFallbackThreshold(
	TEXT("r.Nanite.Builder.FallbackTriangleThreshold"),
	0,
	TEXT("Triangle count <= to this threshold uses the source mesh unchanged as the fallback."),
	ECVF_ReadOnly );

static const float GFallbackDefaultAutoRelativeError = 1.0f;

static TAutoConsoleVariable<float> CVarFallbackTargetAutoRelativeError(
	TEXT("r.Nanite.Builder.FallbackTargetAutoRelativeError"),
	GFallbackDefaultAutoRelativeError,
	TEXT("Relative error to use when generating fallback mesh for assets with Fallback Target = Auto."),
	ECVF_ReadOnly);

static const float GRayTracingProxyDefaultAutoRelativeError = 2.0f;

static TAutoConsoleVariable<float> CVarRayTracingProxyFallbackTargetAutoRelativeError(
	TEXT("r.Nanite.Builder.RayTracingProxy.FallbackTargetAutoRelativeError"),
	GRayTracingProxyDefaultAutoRelativeError,
	TEXT("Relative error to use when generating ray tracing proxy fallback mesh for assets with Fallback Target = Auto."),
	ECVF_ReadOnly);

#define NANITE_BUILD_TIME_LOG_SCOPE(Name) Nanite::Build::FTimeLogScope TimeLogScope_ ## Name(TEXT(#Name))

namespace Nanite
{

namespace Build
{

class FTimeLogScope
{
public:
	FTimeLogScope(const TCHAR* StaticLabel) : Label(StaticLabel), StartTime(FPlatformTime::Cycles()) {}
	~FTimeLogScope()
	{
		const uint32 EndTime = FPlatformTime::Cycles();
		UE_LOG(LogStaticMesh, Log, TEXT("%s [%.2fs]"), Label, FPlatformTime::ToMilliseconds(EndTime - StartTime) / 1000.0f);
	}

private:
	const TCHAR* Label;
	const uint32 StartTime;
};

} // namespace Build

class FBuilderModule : public IBuilderModule
{
public:
	FBuilderModule() {}

	virtual void StartupModule() override
	{
		// Register any modular features here
	}

	virtual void ShutdownModule() override
	{
		// Unregister any modular features here
	}

	virtual const FString& GetVersionString() const;

	virtual FAssemblyPartResourceRef BuildAssemblyPart(
		FInputMeshData& InputMeshData,
		const FMeshNaniteSettings& Settings) override;

	virtual bool Build(
		FResources& Resources,
		FInputMeshData& InputMeshData,
		FOutputMeshData* OutFallbackMeshData,
		FOutputMeshData* OutRayTracingFallbackMeshData,
		const FRayTracingFallbackBuildSettings* RayTracingFallbackBuildSettings,
		const FMeshNaniteSettings& Settings,
		FInputAssemblyData* InputAssemblyData) override;

	virtual bool BuildMaterialIndices(
		const FMeshDataSectionArray& SectionArray,
		const uint32 TriangleCount,
		TArray<int32>& OutMaterialIndices) override;
};

const FString& FBuilderModule::GetVersionString() const
{
	static FString VersionString;

	if (VersionString.IsEmpty())
	{
	#if NANITE_IMPOSTERS_SUPPORTED
		const bool bBuildImposters = CVarBuildImposters.GetValueOnAnyThread();
	#else
		const bool bBuildImposters = false;
	#endif
		const bool bVoxelsSupported = NaniteVoxelsSupported();
		const bool bAssembliesSupported = NaniteAssembliesSupported();
		VersionString = FString::Printf(TEXT("%s_CONSTRAINED%s%s%s%s"), *FDevSystemGuids::GetSystemGuid(FDevSystemGuids::Get().NANITE_DERIVEDDATA_VER).ToString(EGuidFormats::DigitsWithHyphens),
										NANITE_USE_UNCOMPRESSED_VERTEX_DATA ? TEXT("_UNCOMPRESSED") : TEXT(""),
										bVoxelsSupported ? TEXT("_VOXEL") : TEXT(""),
										bAssembliesSupported ? TEXT("_ASSEMBLIES") : TEXT(""),
										bBuildImposters ? TEXT("_IMPOSTERS") : TEXT(""));

		VersionString.Appendf(TEXT("_METIS_%d_%d_%d"), METIS_VER_MAJOR, METIS_VER_MINOR, METIS_VER_SUBMINOR);

		VersionString.Appendf( TEXT("%i"), CVarFallbackThreshold.GetValueOnAnyThread() );
		if (bVoxelsSupported)
		{
			VersionString.Appendf( TEXT("_VOXELBONES%d"), NANITE_MAX_VOXEL_ANIMATION_BONE_INFLUENCES );
		}

		const float AutoRelativeError = CVarFallbackTargetAutoRelativeError.GetValueOnAnyThread();
		if (AutoRelativeError != GFallbackDefaultAutoRelativeError)
		{
			VersionString.Appendf(TEXT("_FRE%.3f"), AutoRelativeError);
		}

		const float RayTracingProxyAutoRelativeError = CVarRayTracingProxyFallbackTargetAutoRelativeError.GetValueOnAnyThread();
		if (RayTracingProxyAutoRelativeError != GRayTracingProxyDefaultAutoRelativeError)
		{
			VersionString.Appendf(TEXT("_RRE%.3f"), RayTracingProxyAutoRelativeError);
		}

	#if NANITE_STRIP_DATA
		VersionString.Append(TEXT("_STRIP"));
	#endif

	#if PLATFORM_CPU_ARM_FAMILY
		// Separate out arm keys as x64 and arm64 clang do not generate the same data for a given
		// input. Add the arm specifically so that a) we avoid rebuilding the current DDC and
		// b) we can remove it once we get arm64 to be consistent.
		VersionString.Append(TEXT("_arm64"));
	#endif
	}

	return VersionString;
}

} // namespace Nanite

IMPLEMENT_MODULE( Nanite::FBuilderModule, NaniteBuilder );



namespace Nanite
{

struct FMeshData
{
	FMeshBuildVertexView Verts;
	TArray<uint32>& Indexes;
};

static int MikkGetNumFaces( const SMikkTSpaceContext* Context )
{
	FMeshData *UserData = (FMeshData*)Context->m_pUserData;
	return UserData->Indexes.Num() / 3;
}

static int MikkGetNumVertsOfFace( const SMikkTSpaceContext* Context, const int FaceIdx )
{
	return 3;
}

static void MikkGetPosition( const SMikkTSpaceContext* Context, float Position[3], const int FaceIdx, const int VertIdx )
{
	FMeshData* UserData = (FMeshData*)Context->m_pUserData;
	for( int32 i = 0; i < 3; i++ )
		Position[i] = UserData->Verts.Position[UserData->Indexes[FaceIdx * 3 + VertIdx]][i];
}

static void MikkGetNormal( const SMikkTSpaceContext* Context, float Normal[3], const int FaceIdx, const int VertIdx )
{
	FMeshData* UserData = (FMeshData*)Context->m_pUserData;
	for( int32 i = 0; i < 3; i++ )
		Normal[i] = UserData->Verts.TangentZ[UserData->Indexes[FaceIdx * 3 + VertIdx]][i];
}

static void MikkSetTSpaceBasic( const SMikkTSpaceContext* Context, const float Tangent[3], const float BitangentSign, const int FaceIdx, const int VertIdx )
{
	FMeshData* UserData = (FMeshData*)Context->m_pUserData;
	for( int32 i = 0; i < 3; i++ )
		UserData->Verts.TangentX[ UserData->Indexes[FaceIdx * 3 + VertIdx]][i] = Tangent[i];

	FVector3f Bitangent = BitangentSign * FVector3f::CrossProduct(
		UserData->Verts.TangentZ[UserData->Indexes[FaceIdx * 3 + VertIdx]],
		UserData->Verts.TangentX[UserData->Indexes[FaceIdx * 3 + VertIdx]]
	);

	for( int32 i = 0; i < 3; i++ )
		UserData->Verts.TangentY[UserData->Indexes[FaceIdx * 3 + VertIdx]][i] = -Bitangent[i];
}

static void MikkGetTexCoord( const SMikkTSpaceContext* Context, float UV[2], const int FaceIdx, const int VertIdx )
{
	FMeshData *UserData = (FMeshData*)Context->m_pUserData;
	for( int32 i = 0; i < 2; i++ )
		UV[i] = UserData->Verts.UVs[0][UserData->Indexes[ FaceIdx * 3 + VertIdx ]][i];
}

void CalcTangents(
	FMeshBuildVertexView& Verts,
	TArray< uint32 >& Indexes )
{
#if WITH_MIKKTSPACE
	FMeshData MeshData = { Verts, Indexes };

	SMikkTSpaceInterface MikkTInterface;
	MikkTInterface.m_getNormal				= MikkGetNormal;
	MikkTInterface.m_getNumFaces			= MikkGetNumFaces;
	MikkTInterface.m_getNumVerticesOfFace	= MikkGetNumVertsOfFace;
	MikkTInterface.m_getPosition			= MikkGetPosition;
	MikkTInterface.m_getTexCoord			= MikkGetTexCoord;
	MikkTInterface.m_setTSpaceBasic			= MikkSetTSpaceBasic;
	MikkTInterface.m_setTSpace				= nullptr;

	SMikkTSpaceContext MikkTContext;
	MikkTContext.m_pInterface				= &MikkTInterface;
	MikkTContext.m_pUserData				= (void*)(&MeshData);
	MikkTContext.m_bIgnoreDegenerates		= true;
	genTangSpaceDefault( &MikkTContext );
#else
	ensureMsgf(false, TEXT("MikkTSpace tangent generation is not supported on this platform."));
#endif //WITH_MIKKTSPACE
}

#if NANITE_EVALUATE_COARSE_REPRESENTATION

#include "MonteCarlo.h"

static void EvaluateCoarseRepresentation(const FRayTracingScene& Source, const FRayTracingScene& Coarse, FBounds3f Bounds, const FCluster* SourceCluster)
{
	const uint32 NumProbes = 4096;

	const int32 NumProbesPerDim = FMath::CeilToInt32(FMath::Pow(NumProbes, 1 / 3.0f));
	const FVector3f ProbeDelta = Bounds.GetSize() / NumProbesPerDim;

	const uint32 NumRaysPerProbe = 64;

	const uint32 NumRayPackets = FMath::DivideAndRoundUp(NumRaysPerProbe, 16u);
	const uint32 NumRayVariations = 64;

	TArray< FRay16 > Rays;
	Rays.AddZeroed(NumRayPackets * NumRayVariations);

	for (int32 i = 0; i < Rays.Num(); i++)
	{
		for (uint32 j = 0; j < 16; j++)
		{
			uint32 SampleIndex = ReverseBits(16 * i + j);
			uint32 Seed = 0;

			FVector3f Origin = FVector3f(0.0f);
			FVector3f Direction = UniformSampleSphere(SobolSampler(SampleIndex, Seed));
			FVector2f Time = FVector2f(0.0f, 100000.0f);

			Rays[i].SetRay(j, Origin, Direction, Time);
		}
	}

	uint32 RayCount = 0;
	uint32 MatchCount = 0;

	uint32 NumHitsSource = 0;
	uint32 NumMissesSource = 0;

	uint32 NumHitsCoarse = 0;
	uint32 NumMissesCoarse = 0;

	uint32 HitMatchCount = 0;
	uint32 MissMatchCount = 0;

	FRandomStream RandStream(853615);
	FClusterAreaWeightedTriangleSampler SourceAreaWeightedTriangleSampler;

	if (SourceCluster != nullptr)
	{
		SourceAreaWeightedTriangleSampler.Init(SourceCluster);
	}

	for (uint32 ProbeIndex = 0; ProbeIndex < NumProbes; ++ProbeIndex)
	{
		FVector3f ProbePosition;

		if (SourceCluster != nullptr)
		{
			const uint32 RandTriangle = SourceAreaWeightedTriangleSampler.GetEntryIndex(RandStream.GetFraction(), RandStream.GetFraction());

			const FVector2f r = FVector2f(RandStream.GetFraction(), RandStream.GetFraction());
			const float sqrt0 = FMath::Sqrt(r.X);
			const FVector3f RandBarycentrics = FVector3f(1.0f - sqrt0, sqrt0 * (1.0f - r.Y), r.Y * sqrt0);

			TConstArrayView<uint32> Indexes = SourceCluster->Indexes;
			const FVector3f V0 = SourceCluster->Verts.GetPosition(Indexes[RandTriangle * 3 + 0]);
			const FVector3f V1 = SourceCluster->Verts.GetPosition(Indexes[RandTriangle * 3 + 1]);
			const FVector3f V2 = SourceCluster->Verts.GetPosition(Indexes[RandTriangle * 3 + 2]);

			ProbePosition = V0 * RandBarycentrics.X + V1 * RandBarycentrics.Y + V2 * RandBarycentrics.Z;
		}
		else
		{
			uint32 ProbeX = ProbeIndex % NumProbesPerDim;
			uint32 ProbeY = (ProbeIndex / NumProbesPerDim) % NumProbesPerDim;
			uint32 ProbeZ = (ProbeIndex / NumProbesPerDim / NumProbesPerDim) % NumProbesPerDim;
			ProbePosition = Bounds.Min + ProbeDelta * FVector3f(float(ProbeX), float(ProbeY), float(ProbeZ));
		}

		uint32		HitInstanceIndex = 0;
		uint32		HitClusterIndex = 0;
		uint32		HitTriIndex = 0;
		FVector3f	HitBarycentrics;

		uint32 ProbeRayCount = 0;
		uint32 ProbeHitCountSource = 0;
		uint32 ProbeHitCountCoarse = 0;

		int32 VaritationIndex = ProbeIndex & (NumRayVariations - 1);
		for (uint32 PacketIndex = 0; PacketIndex < NumRayPackets; PacketIndex++)
		{
			FRay16 Ray16Source = Rays[PacketIndex + VaritationIndex * NumRayPackets];
			for (int j = 0; j < 16; j++)
			{
				Ray16Source.ray.org_x[j] += ProbePosition.X;
				Ray16Source.ray.org_y[j] += ProbePosition.Y;
				Ray16Source.ray.org_z[j] += ProbePosition.Z;
			}

			FRay16 Ray16Coarse = Ray16Source;

			Source.Intersect16(Ray16Source);
			Coarse.Intersect16(Ray16Coarse);
			RayCount += 16;
			ProbeRayCount += 16;

			for (int j = 0; j < 16; j++)
			{
				const bool bHitSource = Source.GetHit(Ray16Source, j, HitInstanceIndex, HitClusterIndex, HitTriIndex, HitBarycentrics);
				const bool bHitCoarse = Coarse.GetHit(Ray16Coarse, j, HitInstanceIndex, HitClusterIndex, HitTriIndex, HitBarycentrics);

				if (bHitSource == bHitCoarse)
				{
					++MatchCount;

					HitMatchCount += bHitSource ? 1 : 0;
					MissMatchCount += !bHitSource ? 1 : 0;
				}

				NumHitsSource += bHitSource ? 1 : 0;
				NumMissesSource += bHitSource ? 0 : 1;

				NumHitsCoarse += bHitCoarse ? 1 : 0;
				NumMissesCoarse += bHitCoarse ? 0 : 1;

				ProbeHitCountSource += bHitSource ? 1 : 0;
				ProbeHitCountCoarse += bHitCoarse ? 1 : 0;
			}
		}

		float ProbeOcclusionSource = float(ProbeHitCountSource) / float(ProbeRayCount);
		float ProbeOcclusionCoarse = float(ProbeHitCountCoarse) / float(ProbeRayCount);
	}

	UE_LOG(LogStaticMesh, Log, TEXT("CoarseRepresentation accuracy = %.1f%% (%d / %d)"), float(MatchCount) / float(RayCount) * 100.0f, MatchCount, RayCount);
	UE_LOG(LogStaticMesh, Log, TEXT("Hit accuracy = %.1f%% (%d / %d)"), float(HitMatchCount) / float(NumHitsSource) * 100.0f, HitMatchCount, NumHitsSource);
	UE_LOG(LogStaticMesh, Log, TEXT("Miss accuracy = %.1f%% (%d / %d)"), float(MissMatchCount) / float(NumMissesSource) * 100.0f, MissMatchCount, NumMissesSource);
	UE_LOG(LogStaticMesh, Log, TEXT("Occlusion = %.1f%% / Source Occlusion = %.1f%%"), float(NumHitsCoarse) / float(RayCount) * 100.0f, float(NumHitsSource) / float(RayCount) * 100.0f);
}

#endif // EVALUATE_COARSE_REPRESENTATION

static float BuildCoarseRepresentation(
	const FClusterDAG& ClusterDAG,
	FMeshBuildVertexData& Verts,
	TArray<uint32>& Indexes,
	FMeshDataSectionArray& Sections,
	uint8& NumTexCoords,
	uint32 TargetNumTris,
	float TargetError,
	const FRayTracingFallbackBuildSettings* RayTracingFallbackBuildSettings )
{
	TargetNumTris = FMath::Max( TargetNumTris, 64u );

	TArray< FClusterRef >	Clusters;
	TSet< uint32 >			Groups;
	ClusterDAG.FindCut( Clusters, Groups, TargetNumTris, TargetError, 4096 );

	FCluster CoarseRepresentation( ClusterDAG, Clusters );

	// FindDAGCut also produces error when TargetError is non-zero but this only happens for LOD0 whose MaxDeviation is always zero.
	// Don't use the old weights for LOD0 since they change the error calculation and hence, change the meaning of TargetError.
	const float OutError = CoarseRepresentation.Simplify( ClusterDAG, TargetNumTris, TargetError, FMath::Min( TargetNumTris, 256u ), RayTracingFallbackBuildSettings );

#if NANITE_EVALUATE_COARSE_REPRESENTATION
	{
		FRayTracingScene CoarseRayTracingScene;
		CoarseRayTracingScene.AddCluster(CoarseRepresentation);
		rtcCommitScene(CoarseRayTracingScene.Scene);

		TArray< FClusterRef >	SourceClusters;
		TSet< uint32 >			SourceGroups;
		ClusterDAG.FindCut(SourceClusters, SourceGroups, 99999999, 0, 4096);

		FCluster SourceRepresentation(ClusterDAG, SourceClusters);

		FRayTracingScene SourceRayTracingScene;
		SourceRayTracingScene.AddCluster(SourceRepresentation);
		rtcCommitScene(SourceRayTracingScene.Scene);

		EvaluateCoarseRepresentation(SourceRayTracingScene, CoarseRayTracingScene, ClusterDAG.TotalBounds, &SourceRepresentation);
	}
#endif // EVALUATE_COARSE_REPRESENTATION

	FMeshDataSectionArray OldSections = Sections;

	const uint8 NumBoneInfluences = CoarseRepresentation.Verts.Format.NumBoneInfluences;

	// Need to update coarse representation UV count to match new data.
	NumTexCoords = CoarseRepresentation.Verts.Format.NumTexCoords;

	// Rebuild vertex data
	Verts.Empty(CoarseRepresentation.Verts.Num(), NumTexCoords, NumBoneInfluences);

	for( uint32 Iter = 0, Num = CoarseRepresentation.Verts.Num(); Iter < Num; ++Iter )
	{
		Verts.Position.Emplace( CoarseRepresentation.Verts.GetPosition(Iter) );
		Verts.TangentX.Emplace( FVector3f::ZeroVector );
		Verts.TangentY.Emplace( FVector3f::ZeroVector );
		Verts.TangentZ.Emplace( CoarseRepresentation.Verts.GetNormal(Iter) );

		if( NumTexCoords > 0 )
		{
			const FVector2f* UVs = CoarseRepresentation.Verts.GetUVs(Iter);
			for( uint32 UVIndex = 0; UVIndex < NumTexCoords; ++UVIndex )
			{
				Verts.UVs[UVIndex].Emplace( UVs[UVIndex].ContainsNaN() ? FVector2f::ZeroVector : UVs[UVIndex] );
			}
		}

		if (NumBoneInfluences > 0)
		{
			const FVector2f* BoneInfluences = CoarseRepresentation.Verts.GetBoneInfluences(Iter);
			for (uint32 InfluenceIndex = 0; InfluenceIndex < NumBoneInfluences; ++InfluenceIndex)
			{
				const uint32 BoneIndex	= (uint32)BoneInfluences[InfluenceIndex].X;
				const float fBoneWeight = BoneInfluences[InfluenceIndex].Y;
				const uint32 BoneWeight = FMath::RoundToInt(fBoneWeight);

				Verts.BoneIndices[InfluenceIndex].Emplace(uint16(BoneIndex));
				Verts.BoneWeights[InfluenceIndex].Emplace(uint16(BoneWeight));
			}
		}
		
		if( CoarseRepresentation.Verts.Format.bHasColors )
		{
			Verts.Color.Emplace( CoarseRepresentation.Verts.GetColor(Iter).ToFColor(false /* sRGB */) );
		}
	}

	// Compute material ranges for coarse representation.
	CoarseRepresentation.BuildMaterialRanges();
	check( CoarseRepresentation.MaterialRanges.Num() <= OldSections.Num() );

	// Rebuild section data.
	Sections.Reset( CoarseRepresentation.MaterialRanges.Num() );
	for( const FMeshDataSection& OldSection : OldSections )
	{
		// Add new sections based on the computed material ranges
		// Enforce the same material order as OldSections
		const FMaterialRange* FoundRange = CoarseRepresentation.MaterialRanges.FindByPredicate(
			[&OldSection]( const FMaterialRange& Range ) { return Range.MaterialIndex == OldSection.MaterialIndex; }
		);

		// Sections can actually be removed from the coarse mesh if their source data doesn't contain enough triangles
		if( FoundRange )
		{
			// Copy properties from original mesh sections.
			FMeshDataSection Section( OldSection );

			// Range of vertices and indices used when rendering this section.
			Section.FirstIndex = FoundRange->RangeStart * 3;
			Section.NumTriangles = FoundRange->RangeLength;
			Section.MinVertexIndex = TNumericLimits< uint32 >::Max();
			Section.MaxVertexIndex = TNumericLimits< uint32 >::Min();

			for( uint32 TriIndex = FoundRange->RangeStart; TriIndex < FoundRange->RangeStart + FoundRange->RangeLength; TriIndex++ )
			{
				for( int32 k = 0; k < 3; k++ )
				{
					Section.MinVertexIndex = FMath::Min( Section.MinVertexIndex, CoarseRepresentation.Indexes[ TriIndex * 3 + k ] );
					Section.MaxVertexIndex = FMath::Max( Section.MaxVertexIndex, CoarseRepresentation.Indexes[ TriIndex * 3 + k ] );
				}
			}

			Sections.Add( Section );
		}
	}
	Swap( Indexes, CoarseRepresentation.Indexes );

	// If we don't have explicit tangents, calculate them
	if( !ClusterDAG.bHasTangents && NumTexCoords > 0 )
	{
		FMeshBuildVertexView VertexView = MakeMeshBuildVertexView(Verts);
		CalcTangents( VertexView, Indexes );
	}

	return OutError;
}

#if NANITE_LOG_COMPRESSED_SIZES
static void CalculateCompressedNaniteDiskSize(FResources& Resources, int32& OutUncompressedSize, int32& OutCompressedSize)
{
	TArray<uint8> Data;
	FMemoryWriter Ar(Data, true);
	Resources.Serialize(Ar, nullptr, true);
	OutUncompressedSize = Data.Num();

	TArray<uint8> CompressedData;
	FOodleCompressedArray::CompressTArray(CompressedData, Data, FOodleDataCompression::ECompressor::Mermaid, FOodleDataCompression::ECompressionLevel::Optimal2);
	OutCompressedSize = CompressedData.Num();
}
#endif

void TessellateAndDisplace(
	FMeshBuildVertexData& Verts,
	TArray< uint32 >& Indexes,
	TArray< int32 >& MaterialIndexes,
	const FBounds3f& MeshBounds,
	const FMeshNaniteSettings& Settings );

static void PreprocessMesh(
	IBuilderModule::FInputMeshData& InputMeshData,
	const FMeshNaniteSettings& Settings )
{
	if( Settings.DisplacementMaps.Num() && InputMeshData.TriangleCounts.Num() == 1 && Settings.TrimRelativeError != 0.0f )
	{
		uint32 Time0 = FPlatformTime::Cycles();

		TessellateAndDisplace( InputMeshData.Vertices, InputMeshData.TriangleIndices, InputMeshData.MaterialIndices, InputMeshData.VertexBounds, Settings );
		InputMeshData.TriangleCounts[0] = InputMeshData.TriangleIndices.Num() / 3;

		uint32 Time1 = FPlatformTime::Cycles();
		UE_LOG( LogStaticMesh, Log, TEXT("Adaptive tessellate [%.2fs], tris: %i"), FPlatformTime::ToMilliseconds( Time1 - Time0 ) / 1000.0f, InputMeshData.TriangleCounts[0] );
	}
}

static bool BuildIntermediateResources(
	FIntermediateResources& Resources,
	IBuilderModule::FInputMeshData& InputMeshData,
	FInputAssemblyData* InputAssemblyData,
	const FMeshNaniteSettings& Settings,
	bool bCanFreeInputMeshData,
	bool bAssemblyPart )
{
	PreprocessMesh( InputMeshData, Settings );

	const bool bIsAssembly = InputAssemblyData != nullptr && InputAssemblyData->IsValid();
	if (InputMeshData.TriangleIndices.Num() == 0 && !bIsAssembly)
	{
		UE_LOG(LogStaticMesh, Warning, TEXT("Failed to build Nanite mesh. Input has 0 triangles."));
		return false;
	}

	Resources.Sections			= InputMeshData.Sections;
	Resources.NumInputVertices	= InputMeshData.Vertices.Position.Num();
	Resources.NumInputTriangles	= InputMeshData.TriangleIndices.Num() / 3;
	Resources.ResourceFlags = 0x0;

	FClusterDAG::FSettings& DAGSettings = Resources.ClusterDAG.Settings;
	DAGSettings.ExtraVoxelLevels	= bAssemblyPart ? 0u : 2u;	// Allow non-assembly part root to go to 32 voxel root cluster.
	DAGSettings.MaxEdgeLengthFactor	= Settings.MaxEdgeLengthFactor;
	DAGSettings.NumRays				= FMath::Min( Settings.NumRays, 1024 );
	DAGSettings.VoxelLevel			= Settings.VoxelLevel;
	DAGSettings.RayBackUp			= Settings.RayBackUp;
	DAGSettings.ShapePreservation	= Settings.ShapePreservation;
	DAGSettings.bLerpUVs			= Settings.bLerpUVs;
	DAGSettings.bSeparable			= Settings.bSeparable;
	DAGSettings.bVoxelNDF			= Settings.bVoxelNDF;
	DAGSettings.bVoxelOpacity		= Settings.bVoxelOpacity;

	if( DAGSettings.NumRays > 1 )
		DAGSettings.NumRays = ( DAGSettings.NumRays + 15 ) & ~15;

	FVertexFormat VertexFormat;
	VertexFormat.NumTexCoords		= (uint8)FMath::Min< uint32 >( InputMeshData.NumTexCoords, NANITE_MAX_UVS );
	VertexFormat.NumBoneInfluences	= InputMeshData.NumBoneInfluences;
	VertexFormat.bHasTangents		= Settings.bExplicitTangents;
	VertexFormat.bHasColors			= InputMeshData.Vertices.Color.Num() == InputMeshData.Vertices.Position.Num();
	
	{
		FConstMeshBuildVertexView VertexView = MakeConstMeshBuildVertexView(InputMeshData.Vertices);

		uint32 BaseTriangle = 0;
		for (uint32 NumTriangles : InputMeshData.TriangleCounts)
		{
			if (NumTriangles)
			{
				Resources.ClusterDAG.AddMesh(
					VertexView,
					TArrayView< const uint32 >( &InputMeshData.TriangleIndices[BaseTriangle * 3], NumTriangles * 3 ),
					TArrayView< const int32 >( &InputMeshData.MaterialIndices[BaseTriangle], NumTriangles ),
					InputMeshData.VertexBounds, VertexFormat );
			}
			else
			{
				Resources.ClusterDAG.AddEmptyMesh();
			}
			BaseTriangle += NumTriangles;
		}
	}
	
	// If we're going to replace the original vertex buffer with a coarse representation, get rid of the old copies
	// now that we copied it into the cluster representation. We do it before the longer DAG reduce phase to shorten peak memory duration.
	// This is especially important when building multiple huge Nanite meshes in parallel.
	if( bCanFreeInputMeshData )
	{
		InputMeshData.Vertices.Empty();
		InputMeshData.TriangleIndices.Empty();
	}
	InputMeshData.MaterialIndices.Empty();

	if (UE::Tasks::FCancellationTokenScope::IsCurrentWorkCanceled())
	{
		return false;
	}

	if (bIsAssembly)
	{
		NANITE_BUILD_TIME_LOG_SCOPE(NaniteAssemblyBuild);

		if (!AddAssemblyParts(Resources, *InputAssemblyData))
		{
			return false;
		}
	}

	{
		NANITE_BUILD_TIME_LOG_SCOPE(Reduce);
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::Build::DAG.Reduce);

		for( int32 MeshIndex = 0; MeshIndex < InputMeshData.TriangleCounts.Num(); MeshIndex++ )
		{
			Resources.ClusterDAG.ReduceMesh( MeshIndex );
		}
	}

	if (UE::Tasks::FCancellationTokenScope::IsCurrentWorkCanceled())
	{
		return false;
	}

	if( Settings.KeepPercentTriangles < 1.0f || Settings.TrimRelativeError > 0.0f )
	{
		int32 TargetNumTris = int32((float)Resources.NumInputTriangles * Settings.KeepPercentTriangles);
		float TargetError = Settings.TrimRelativeError * 0.01f * FMath::Sqrt( FMath::Min( 2.0f * Resources.ClusterDAG.SurfaceArea, InputMeshData.VertexBounds.GetSurfaceArea() ) );

		TArray< FClusterRef >	Clusters;
		TSet< uint32 >			Groups;
		Resources.ClusterDAG.FindCut( Clusters, Groups, TargetNumTris, TargetError, 0 );

		for( auto& Group : Resources.ClusterDAG.Groups )
			Group.bTrimmed = true;
		for( uint32 GroupIndex : Groups )
			Resources.ClusterDAG.Groups[ GroupIndex ].bTrimmed = false;
	
		uint32 NumVerts = 0;
		uint32 NumTris = 0;
		for( FClusterRef ClusterRef : Clusters )
		{
			FCluster& Cluster = ClusterRef.GetCluster( Resources.ClusterDAG );

			Cluster.GeneratingGroupIndex = MAX_uint32;
			Cluster.EdgeLength = -FMath::Abs( Cluster.EdgeLength );
			NumVerts += Cluster.Verts.Num();
			NumTris  += Cluster.NumTris;
		}

		Resources.NumInputVertices	= FMath::Min( NumVerts, Resources.NumInputVertices );
		Resources.NumInputTriangles	= NumTris;

		UE_LOG( LogStaticMesh, Log, TEXT("Trimmed to %u tris"), NumTris );
	}

	return true;
}

static void BuildFallbackMesh(
	const FIntermediateResources& Intermediate,
	IBuilderModule::FInputMeshData& InputMeshData,
	const FInputAssemblyData* InputAssemblyData,
	const FMeshNaniteSettings& Settings,
	bool bFallbackIsReduced,
	IBuilderModule::FOutputMeshData& OutFallbackMeshData,
	const FRayTracingFallbackBuildSettings* RayTracingFallbackBuildSettings = nullptr)
{
	// Determine fallback parameters
	float FallbackPercentTriangles = Settings.FallbackPercentTriangles;
	float FallbackRelativeError = Settings.FallbackRelativeError;
	if (RayTracingFallbackBuildSettings)
	{
		FallbackPercentTriangles = RayTracingFallbackBuildSettings->FallbackPercentTriangles;
		FallbackRelativeError = RayTracingFallbackBuildSettings->FallbackRelativeError;
	}
	const uint32 FallbackTargetNumTris = uint32((float)Intermediate.NumInputTriangles * FallbackPercentTriangles);
	const float FallbackTargetError = FallbackRelativeError * 0.01f * FMath::Sqrt( FMath::Min( 2.0f * Intermediate.ClusterDAG.SurfaceArea, InputMeshData.VertexBounds.GetSurfaceArea() ) );

	const uint32 FallbackStartTime = FPlatformTime::Cycles();

	if( !bFallbackIsReduced )
	{
		Swap(OutFallbackMeshData.Vertices, InputMeshData.Vertices);
		Swap(OutFallbackMeshData.TriangleIndices, InputMeshData.TriangleIndices);
		OutFallbackMeshData.Sections = InputMeshData.Sections;
	}
	else
	{
		// Create a flat list of empty mesh sections, which will then be filled in after the simplification
		auto MergeSectionArray = [&Dest = OutFallbackMeshData.Sections](
			const TConstArrayView<FMeshDataSection>& Other,
			const FMaterialRemapTable* Remap = nullptr)
			{
				for (const FMeshDataSection& OtherSection : Other)
				{
					const int32 MaterialIndex = Remap ? (*Remap)[OtherSection.MaterialIndex] : OtherSection.MaterialIndex;
					if (!Remap || !Dest.ContainsByPredicate([MaterialIndex](const FMeshDataSection& S) { return S.MaterialIndex == MaterialIndex; }))
					{
						FMeshDataSection& NewSection = Dest.Emplace_GetRef();
						FMemory::Memzero(NewSection);

						NewSection.MaterialIndex = MaterialIndex;
						NewSection.FirstIndex = 0;
						NewSection.NumTriangles = 0;
						NewSection.MinVertexIndex = 0;
						NewSection.MaxVertexIndex = 0;
					}
				}
			};

		OutFallbackMeshData.Sections.Reset();
		MergeSectionArray(InputMeshData.Sections);
		if (InputAssemblyData)
		{
			for (const FInputAssemblyData::FBuiltPartData& Part : InputAssemblyData->Parts)
			{
				MergeSectionArray(Part.Resource->Sections, &Part.MaterialRemap);
			}
		}

		FMeshDataSectionArray FallbackSections = OutFallbackMeshData.Sections;
		const float ReductionError = BuildCoarseRepresentation(
			Intermediate.ClusterDAG,
			OutFallbackMeshData.Vertices,
			OutFallbackMeshData.TriangleIndices,
			FallbackSections,
			InputMeshData.NumTexCoords,
			FallbackTargetNumTris,
			FallbackTargetError,
			RayTracingFallbackBuildSettings
		);

		// Fixup mesh section info with new coarse mesh ranges, while respecting original ordering and keeping materials
		// that do not end up with any assigned triangles (due to decimation process).
		uint64 HandledMaterials = 0;
		for (FMeshDataSection& Section : OutFallbackMeshData.Sections)
		{
			const uint64 MaterialBit = 1ull << uint64(Section.MaterialIndex);
			if ((HandledMaterials & MaterialBit) != 0ull)
			{
				// FallbackSections merges all triangles of a given material together, so any other mesh sections
				// with that material should be empty.
				// NOTE: This has a known limitation that the mesh section settings of the first mesh section will
				// prevail over any other sections' settings.
				continue;
			}

			// For each section info, try to find a matching entry in the coarse version.
			const FMeshDataSection* FallbackSection = FallbackSections.FindByPredicate(
				[&Section](const FMeshDataSection& CoarseSectionIter)
				{
					return CoarseSectionIter.MaterialIndex == Section.MaterialIndex;
				});

			if (FallbackSection != nullptr)
			{
				// Matching entry found
				Section.FirstIndex		= FallbackSection->FirstIndex;
				Section.NumTriangles	= FallbackSection->NumTriangles;
				Section.MinVertexIndex	= FallbackSection->MinVertexIndex;
				Section.MaxVertexIndex	= FallbackSection->MaxVertexIndex;
			}

			HandledMaterials |= MaterialBit;
		}
	}

	const uint32 FallbackEndTime = FPlatformTime::Cycles();
	UE_LOG(LogStaticMesh, Log, TEXT("Fallback [%.2fs], num tris: %d"), FPlatformTime::ToMilliseconds(FallbackEndTime - FallbackStartTime) / 1000.0f, OutFallbackMeshData.TriangleIndices.Num() / 3);
}

FAssemblyPartResourceRef FBuilderModule::BuildAssemblyPart(
	FInputMeshData& InputMeshData,
	const FMeshNaniteSettings& Settings)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::BuildData);
	NANITE_BUILD_TIME_LOG_SCOPE(BuildAssemblyPart);

	FAssemblyPartResourceRef Intermediate = MakeShared<FIntermediateResources, ESPMode::NotThreadSafe>();

	if (!BuildIntermediateResources(*Intermediate, InputMeshData, nullptr, Settings, true, true))
	{
		return nullptr;
	}

	return MoveTemp(Intermediate);
}

bool FBuilderModule::Build(
	FResources& Resources,
	IBuilderModule::FInputMeshData& InputMeshData,
	IBuilderModule::FOutputMeshData* OutFallbackMeshData,
	IBuilderModule::FOutputMeshData* OutRayTracingFallbackMeshData,
	const FRayTracingFallbackBuildSettings* RayTracingFallbackBuildSettings,
	const FMeshNaniteSettings& Settings,
	FInputAssemblyData* InputAssemblyData)
{
	NANITE_BUILD_TIME_LOG_SCOPE(NaniteBuild);
	TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::BuildData);

	const bool bIsAssembly = InputAssemblyData != nullptr && InputAssemblyData->IsValid();

	// NOTE: The fallback is reduced if the base Nanite mesh will also reduce the input. (For assemblies, it's always "reduced" even if
	// it's not because we can't use only the input mesh data to generate a fallback)
	const bool bFallbackIsReduced = 
		bIsAssembly ||
		Settings.FallbackPercentTriangles < 1.0f ||
		Settings.KeepPercentTriangles < 1.0f ||
		Settings.FallbackRelativeError > 0.0f ||
		Settings.TrimRelativeError > 0.0f;

	const bool bRayTracingFallbackIsReduced = 
		RayTracingFallbackBuildSettings != nullptr
		&& (bIsAssembly ||
			Settings.KeepPercentTriangles < 1.0f ||
			Settings.TrimRelativeError > 0.0f ||
			RayTracingFallbackBuildSettings->IsFallbackReduced());

	const bool bCanFreeInputMeshData = bFallbackIsReduced && bRayTracingFallbackIsReduced;

	FIntermediateResources Intermediate;
	if (!BuildIntermediateResources(Intermediate, InputMeshData, InputAssemblyData, Settings, bCanFreeInputMeshData, false))
	{
		return false;
	}

	if (UE::Tasks::FCancellationTokenScope::IsCurrentWorkCanceled())
	{
		return false;
	}

	if (OutFallbackMeshData != nullptr)
	{
		BuildFallbackMesh(Intermediate, InputMeshData, InputAssemblyData, Settings, bFallbackIsReduced, *OutFallbackMeshData);
	}

	if (UE::Tasks::FCancellationTokenScope::IsCurrentWorkCanceled())
	{
		return false;
	}

	if (OutRayTracingFallbackMeshData != nullptr)
	{
		check(RayTracingFallbackBuildSettings);

		if (bRayTracingFallbackIsReduced || OutFallbackMeshData == nullptr || bFallbackIsReduced)
		{
			BuildFallbackMesh(Intermediate, InputMeshData, InputAssemblyData, Settings, bRayTracingFallbackIsReduced, *OutRayTracingFallbackMeshData, RayTracingFallbackBuildSettings);
		}
		else
		{
			// if neither ray tracing nor main fallback are reduced, reuse the same data since the first BuildFallbackMesh(...) swapped input data to OutFallbackMeshData
			// so it's not available for the second BuildFallbackMesh(...) any more.
			*OutRayTracingFallbackMeshData = *OutFallbackMeshData;
		}
	}

	if (UE::Tasks::FCancellationTokenScope::IsCurrentWorkCanceled())
	{
		return false;
	}

	uint32 TotalGPUSize;
	{
		NANITE_BUILD_TIME_LOG_SCOPE(Encode);

		Resources.NumInputTriangles		= Intermediate.NumInputTriangles;
		Resources.NumInputVertices		= Intermediate.NumInputVertices;
		Resources.ResourceFlags			= Intermediate.ResourceFlags;

		Encode(Resources, Intermediate.ClusterDAG, Settings, InputMeshData.TriangleCounts.Num(), &TotalGPUSize);
	}

#if NANITE_IMPOSTERS_SUPPORTED
	const bool bHasImposter = CVarBuildImposters.GetValueOnAnyThread() && (InputMeshData.TriangleCounts.Num() == 1);
	if (bHasImposter)
	{
		NANITE_BUILD_TIME_LOG_SCOPE(Imposter);
		auto& RootChildren = Intermediate.Groups.Last().Children;
	
		FImposterAtlas ImposterAtlas( Resources.ImposterAtlas, Intermediate.MeshBounds );

		UE::Tasks::FCancellationToken* CancellationToken = UE::Tasks::FCancellationTokenScope::GetCurrentCancellationToken();
		ParallelFor( TEXT("Nanite.BuildData.PF"), FMath::Square(FImposterAtlas::AtlasSize), 1,
			[&]( int32 TileIndex )
			{
				FIntPoint TilePos(
					TileIndex % FImposterAtlas::AtlasSize,
					TileIndex / FImposterAtlas::AtlasSize);

				if (CancellationToken && CancellationToken->IsCanceled())
				{
					return;
				}

				for( int32 ClusterIndex = 0; ClusterIndex < RootChildren.Num(); ClusterIndex++ )
				{
					ImposterAtlas.Rasterize( TilePos, Intermediate.Clusters[ RootChildren[ ClusterIndex ] ], ClusterIndex) ;
				}
			} );
	}
#endif
	
	if (UE::Tasks::FCancellationTokenScope::IsCurrentWorkCanceled())
	{
		return false;
	}

#if NANITE_STRIP_DATA
	Resources = FResources();
#endif

#if NANITE_LOG_COMPRESSED_SIZES
	int32 UncompressedSize, CompressedSize;
	CalculateCompressedNaniteDiskSize(Resources, UncompressedSize, CompressedSize);
	UE_LOG(LogStaticMesh, Log, TEXT("Compressed size: %.2fMB -> %.2fMB"), UncompressedSize / 1048576.0f, CompressedSize / 1048576.0f);

	{
		static FCriticalSection CriticalSection;
		FScopeLock Lock(&CriticalSection);
		static uint32 TotalMeshes = 0;
		static uint64 TotalMeshUncompressedSize = 0;
		static uint64 TotalMeshCompressedSize = 0;
		static uint64 TotalMeshGPUSize = 0;

		TotalMeshes++;
		TotalMeshUncompressedSize += UncompressedSize;
		TotalMeshCompressedSize += CompressedSize;
		TotalMeshGPUSize += TotalGPUSize;
		UE_LOG(LogStaticMesh, Log, TEXT("Total: %d Meshes, GPU: %.2fMB, Uncompressed: %.2fMB, Compressed: %.2fMB"), TotalMeshes, TotalMeshGPUSize / 1048576.0f, TotalMeshUncompressedSize / 1048576.0f, TotalMeshCompressedSize / 1048576.0f);
	}
#endif

	return true;
}

bool FBuilderModule::BuildMaterialIndices(const FMeshDataSectionArray& SectionArray, const uint32 TriangleCount, TArray<int32>& OutMaterialIndices)
{
	if (SectionArray.IsEmpty() || SectionArray.Num() > MaxSectionArraySize)
	{
		UE_LOG(LogStaticMesh, Log, TEXT("Unable to build Nanite data. Unsupported number of sections: %d."), SectionArray.Num());
		return false;
	}

	// Build associated array of triangle index and material index.
	OutMaterialIndices.Reset(TriangleCount);
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Nanite::BuildSections);
		for (int32 SectionIndex = 0; SectionIndex < SectionArray.Num(); SectionIndex++)
		{
			const FMeshDataSection& Section = SectionArray[SectionIndex];
			checkSlow(Section.MaterialIndex != INDEX_NONE);
			for (uint32 Tri = 0; Tri < Section.NumTriangles; ++Tri)
			{
				OutMaterialIndices.Add(Section.MaterialIndex);
			}
		}
	}

	// Make sure there is 1 material index per triangle.
	check(OutMaterialIndices.Num() == TriangleCount);

	return true;
}

} // namespace Nanite
