// Copyright Epic Games, Inc. All Rights Reserved.


#include "Generators/BoneGeometryGenerators.h"

#include "Dataflow/DataflowDebugDrawInterface.h"
#include "Dataflow/DataflowNode.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Engine/SkeletalMesh.h"
#include "Math/Box.h"
#include "PhysicsEngine/ConvexElem.h"
#include "ReferenceSkeleton.h"
#include "Rendering/PositionVertexBuffer.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Rendering/StaticMeshVertexBuffer.h"

namespace UE::Dataflow
{
	void RegisterBoneGeometryGeneratorNodes()
	{
		// Body Generators
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeBoxBoneGeometryGenerator);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeSphereBoneGeometryGenerator);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeCapsuleBoneGeometryGenerator);
		DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeConvexBoneGeometryGenerator);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FMakeBoxBoneGeometryGenerator::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if(Out->IsA(&Generator))
	{
		TObjectPtr<UBoneGeometryGenerator> OutGenerator = CastChecked<UBoneGeometryGenerator>(StaticDuplicateObject(Generator, GetTransientPackage()));
		SetValue(Context, OutGenerator, &Generator);
	}
}

void FMakeBoxBoneGeometryGenerator::Register()
{
	AddOutputs(Generator);

	Generator = NewObject<UBoneGeometryGenerator_Box>(GetOwner());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FMakeSphereBoneGeometryGenerator::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if(Out->IsA(&Generator))
	{
		TObjectPtr<UBoneGeometryGenerator> OutGenerator = CastChecked<UBoneGeometryGenerator>(StaticDuplicateObject(Generator, GetTransientPackage()));
		SetValue(Context, OutGenerator, &Generator);
	}
}

void FMakeSphereBoneGeometryGenerator::Register()
{
	AddOutputs(Generator);

	Generator = NewObject<UBoneGeometryGenerator_Sphere>(GetOwner());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FMakeCapsuleBoneGeometryGenerator::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if(Out->IsA(&Generator))
	{
		TObjectPtr<UBoneGeometryGenerator> OutGenerator = CastChecked<UBoneGeometryGenerator>(StaticDuplicateObject(Generator, GetTransientPackage()));
		SetValue(Context, OutGenerator, &Generator);
	}
}

void FMakeCapsuleBoneGeometryGenerator::Register()
{
	AddOutputs(Generator);

	Generator = NewObject<UBoneGeometryGenerator_Capsule>(GetOwner());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FMakeConvexBoneGeometryGenerator::Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const
{
	if(Out->IsA(&Generator))
	{
		TObjectPtr<UBoneGeometryGenerator> OutGenerator = CastChecked<UBoneGeometryGenerator>(StaticDuplicateObject(Generator, GetTransientPackage()));
		SetValue(Context, OutGenerator, &Generator);
	}
}

void FMakeConvexBoneGeometryGenerator::Register()
{
	AddOutputs(Generator);

	Generator = NewObject<UBoneGeometryGenerator_Convex>(GetOwner());
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if WITH_EDITOR
bool UBoneGeometryGenerator::CanDebugDraw() const
{
	return false;
}

bool UBoneGeometryGenerator::CanDebugDrawViewMode(const FName& ViewModeName) const
{
	return false;
}

void UBoneGeometryGenerator::DebugDraw(UE::Dataflow::FContext& Context, IDataflowDebugDrawInterface& DataflowRenderingInterface, const FDataflowNode::FDebugDrawParameters& DebugDrawParameters, FRigidAssetBoneSelection Bones) const
{
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Data extracted from an LOD for body generation
 */
struct FBoneVertData
{
	TArray<FVector3f> Positions;
	TArray<FVector3f> Normals;
	TArray<float> Weights;
};

FBoneVertData GetBoneVertexData(FName BoneName, USkeletalMesh* Mesh, int32 Lod, EVertexSelectMode Mode)
{
	if(!Mesh)
	{
		return {};
	}

	const FSkeletalMeshRenderData* MeshRenderData = Mesh->GetResourceForRendering();
	if(!MeshRenderData || !MeshRenderData->LODRenderData.IsValidIndex(Lod))
	{
		return {};
	}

	FBoneVertData Result;

	// Make sure we have initialised the mesh in order to get vertex data
	Mesh->CalculateInvRefMatrices();
	const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
	const FSkeletalMeshLODRenderData& LodData = MeshRenderData->LODRenderData[Lod];

	constexpr float WeightsToFloatMult = (1.0f / 65535.0f);
	const FSkinWeightVertexBuffer* Weights = LodData.GetSkinWeightVertexBuffer();
	check(Weights);

	// Mesh buffer for normals, position buffer for positions
	const FStaticMeshVertexBuffer& MeshVertexBuffer = LodData.StaticVertexBuffers.StaticMeshVertexBuffer;
	const FPositionVertexBuffer& Positions = LodData.StaticVertexBuffers.PositionVertexBuffer;

	const uint32 NumLodVerts = Weights->GetNumVertices();
	const uint32 MaxInfluences = LodData.GetVertexBufferMaxBoneInfluences();

	if(MaxInfluences == 0)
	{
		return {};
	}

	const int32 NumSections = LodData.RenderSections.Num();

	for(int32 SectionIndex = 0; SectionIndex < NumSections; ++SectionIndex)
	{
		const FSkelMeshRenderSection& Section = LodData.RenderSections[SectionIndex];
		const uint32 SectionMaxInfluences = Section.MaxBoneInfluences;

		for(uint32 VertIndex = Section.BaseVertexIndex; VertIndex < Section.BaseVertexIndex + Section.NumVertices; ++VertIndex)
		{
			if(Mode == EVertexSelectMode::Any)
			{
				for(uint32 InfluenceIndex = 0; InfluenceIndex < SectionMaxInfluences; InfluenceIndex++)
				{
					const uint32 RenderBoneIndex = Weights->GetBoneIndex(VertIndex, InfluenceIndex);
					const uint32 MeshBoneIndex = Section.BoneMap[RenderBoneIndex];

					uint16 BoneWeight = Weights->GetBoneWeight(VertIndex, InfluenceIndex);

					if(BoneWeight > 0 && RefSkeleton.GetBoneName(MeshBoneIndex) == BoneName)
					{
						const FMatrix44f& RefMatrix = Mesh->GetRefBasesInvMatrix()[MeshBoneIndex];

						Result.Positions.Add(RefMatrix.TransformPosition(Positions.VertexPosition(VertIndex)));
						Result.Normals.Add(RefMatrix.TransformVector(MeshVertexBuffer.VertexTangentZ(VertIndex)));
						Result.Weights.Add(BoneWeight * WeightsToFloatMult);

						// Vertex will only have one influence for a given bone, skip the rest
						break;
					}
				}
			}
			else
			{
				int32 MaxInfluenceIndex = 0;
				uint16 MaxInfluence = Weights->GetBoneWeight(VertIndex, 0);
				for(uint32 InfluenceIndex = 1; InfluenceIndex < SectionMaxInfluences; InfluenceIndex++)
				{
					uint16 BoneWeight = Weights->GetBoneWeight(VertIndex, InfluenceIndex);

					if(BoneWeight > MaxInfluence)
					{
						MaxInfluenceIndex = InfluenceIndex;
						MaxInfluence = BoneWeight;
					}
				}

				const uint32 RenderBoneIndex = Weights->GetBoneIndex(VertIndex, MaxInfluenceIndex);
				const uint32 MeshBoneIndex = Section.BoneMap[RenderBoneIndex];

				if(RefSkeleton.GetBoneName(MeshBoneIndex) == BoneName)
				{
					const FMatrix44f& RefMatrix = Mesh->GetRefBasesInvMatrix()[MeshBoneIndex];

					Result.Positions.Add(RefMatrix.TransformPosition(Positions.VertexPosition(VertIndex)));
					Result.Normals.Add(RefMatrix.TransformVector(MeshVertexBuffer.VertexTangentZ(VertIndex)));
					Result.Weights.Add(MaxInfluence * WeightsToFloatMult);
				}
			}
		}
	}

	return Result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TArray<UE::Chaos::RigidAsset::FSimpleGeometry> UBoneGeometryGenerator_Box::Build(FRigidAssetBoneSelection Bones)
{
	if(!Bones.Mesh || !Bones.Skeleton)
	{
		return {};
	}

	TArray<UE::Chaos::RigidAsset::FSimpleGeometry> Result;

	for(const FRigidAssetBoneInfo& Bone : Bones.SelectedBones)
	{
		FBoneVertData BoneVerts = GetBoneVertexData(Bone.Name, Bones.Mesh, BaseSettings.SourceLod, BaseSettings.VertexMode);

		if(BoneVerts.Positions.Num() == 0)
		{
			// No influences, make a small sphere and warn
			// #TODO pass context through for dataflow warnings
			Result.AddDefaulted();
			Result.Last().GeometryElement = MakeShared<FKSphereElem>(5.0f);
			continue;
		}

		FBox3f VertBox(BoneVerts.Positions);

		TSharedPtr<FKBoxElem> NewBox = MakeShared<FKBoxElem>();
		NewBox->X = VertBox.GetExtent().X * 2.0f;
		NewBox->Y = VertBox.GetExtent().Y * 2.0f;
		NewBox->Z = VertBox.GetExtent().Z * 2.0f;
		NewBox->Center = static_cast<FVector>(VertBox.GetCenter());

		Result.AddDefaulted();
		Result.Last().GeometryElement = NewBox;
	}

	return Result;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TArray<UE::Chaos::RigidAsset::FSimpleGeometry> UBoneGeometryGenerator_Sphere::Build(FRigidAssetBoneSelection Bones)
{
	if(!Bones.Mesh || !Bones.Skeleton)
	{
		return {};
	}

	TArray<UE::Chaos::RigidAsset::FSimpleGeometry> Result;

	for(const FRigidAssetBoneInfo& Bone : Bones.SelectedBones)
	{
		FBoneVertData BoneVerts = GetBoneVertexData(Bone.Name, Bones.Mesh, BaseSettings.SourceLod, BaseSettings.VertexMode);

		if(BoneVerts.Positions.Num() == 0)
		{
			// No influences, make a small sphere and warn
			// #TODO pass context through for dataflow warnings
			Result.AddDefaulted();
			Result.Last().GeometryElement = MakeShared<FKSphereElem>(5.0f);
			continue;
		}

		FBox3f VertBox(BoneVerts.Positions);

		TSharedPtr<FKSphereElem> NewSphere = MakeShared<FKSphereElem>();
		NewSphere->Radius = VertBox.GetExtent().Length();
		NewSphere->Center = static_cast<FVector>(VertBox.GetCenter());

		Result.AddDefaulted();
		Result.Last().GeometryElement = NewSphere;
	}

	return Result;
}

#if WITH_EDITOR
bool UBoneGeometryGenerator_Sphere::CanDebugDraw() const
{
	return true;
}

bool UBoneGeometryGenerator_Sphere::CanDebugDrawViewMode(const FName& ViewModeName) const
{
	return true;
}

void UBoneGeometryGenerator_Sphere::DebugDraw(UE::Dataflow::FContext& Context, IDataflowDebugDrawInterface& DataflowRenderingInterface, const FDataflowNode::FDebugDrawParameters& DebugDrawParameters, FRigidAssetBoneSelection Bones) const
{
	if(!Bones.Mesh || !Bones.Skeleton || !bDrawVerts)
	{
		return;
	}

	TArray<FTransform> BoneTransforms;
	const FReferenceSkeleton& RefSkel = Bones.Mesh->GetRefSkeleton();
	RefSkel.GetBoneAbsoluteTransforms(BoneTransforms);

	int32 BoneColorSeed = 0;
	for(const FRigidAssetBoneInfo& Bone : Bones.SelectedBones)
	{
		if(DrawVertsForBoneName != NAME_None && Bone.Name == DrawVertsForBoneName)
		{
			continue;
		}

		FBoneVertData BoneVerts = GetBoneVertexData(Bone.Name, Bones.Mesh, BaseSettings.SourceLod, BaseSettings.VertexMode);

		DataflowRenderingInterface.SetColor(FLinearColor::MakeRandomSeededColor(BoneColorSeed++));
		for(const FVector3f& Pos : BoneVerts.Positions)
		{
			DataflowRenderingInterface.DrawPoint(BoneTransforms[Bone.Index].TransformPosition(static_cast<FVector>(Pos)));
		}
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Private
{
	// Reproduced from PhysicsAssetUtils module to enable runtime module compilation
	// TODO: Refector PhyiscsAssetUtils into Runtime and Editor modules to better centralise utils.
	template<typename RealType>
	FMatrix ComputeViewCovarianceMatrix(TArrayView<const UE::Math::TVector<RealType>, int> PointView)
	{
		using FVectorType = UE::Math::TVector<RealType>;

		if(PointView.Num() == 0)
		{
			return FMatrix::Identity;
		}

		//get average
		const RealType N = PointView.Num();
		FVectorType U = FVectorType::ZeroVector;

		for(const FVectorType& Point : PointView)
		{
			U += Point;
		}

		U = U / N;

		//compute error terms
		TArray<FVectorType> Errors;
		Errors.AddUninitialized(N);

		for(int32 i = 0; i < N; ++i)
		{
			Errors[i] = PointView[i] - U;
		}

		FMatrix Covariance = FMatrix::Identity;
		for(int32 j = 0; j < 3; ++j)
		{
			FVectorType Axis = FVectorType::ZeroVector;
			RealType* Cj = &Axis.X;
			for(int32 k = 0; k < 3; ++k)
			{
				RealType Cjk = 0.f;
				for(int32 i = 0; i < N; ++i)
				{
					const RealType* Error = &Errors[i].X;
					Cj[k] += Error[j] * Error[k];
				}
				Cj[k] /= N;
			}

			Covariance.SetAxis(j, UE::Math::TVector<double>(Axis));
		}

		return Covariance;
	}

	FVector ComputeEigenVector(const FMatrix& A)
	{
		//using the power method: this is ok because we only need the dominate eigenvector and speed is not critical: http://en.wikipedia.org/wiki/Power_iteration
		FVector Bk = FVector(0, 0, 1);
		for(int32 i = 0; i < 32; ++i)
		{
			float Length = Bk.Size();
			if(Length > 0.f)
			{
				Bk = A.TransformVector(Bk) / Length;
			}
		}

		return Bk.GetSafeNormal();
	}
}

TArray<UE::Chaos::RigidAsset::FSimpleGeometry> UBoneGeometryGenerator_Capsule::Build(FRigidAssetBoneSelection Bones)
{
	if(!Bones.Mesh || !Bones.Skeleton)
	{
		return {};
	}

	TArray<UE::Chaos::RigidAsset::FSimpleGeometry> Result;

	for(const FRigidAssetBoneInfo& Bone : Bones.SelectedBones)
	{
		FBoneVertData BoneVerts = GetBoneVertexData(Bone.Name, Bones.Mesh, BaseSettings.SourceLod, BaseSettings.VertexMode);

		if(BoneVerts.Positions.Num() == 0)
		{
			// No influences, make a small sphere and warn
			// #TODO pass context through for dataflow warnings
			Result.AddDefaulted();
			Result.Last().GeometryElement = MakeShared<FKSphereElem>(5.0f);
			continue;
		}

		// Y and Z are flipped in the default case deliberately to line up a capsule
		// correctly with the bone extent, otherwise it appears on its side
		FVector3f XAxis(1, 0, 0);
		FVector3f YAxis(0, 0, 1);
		FVector3f ZAxis(0, 1, 0);

		if(Alignment == EBodyAlignment::Verts)
		{
			FMatrix CovarianceMat = Private::ComputeViewCovarianceMatrix(MakeConstArrayView(BoneVerts.Positions));
			ZAxis = static_cast<FVector3f>(Private::ComputeEigenVector(CovarianceMat));
			ZAxis.FindBestAxisVectors(XAxis, YAxis);
		}

		auto AveragePosition = [](TArrayView<const FVector3f> Verts)
			{
				FVector3f Avg(0);

				if(Verts.Num() == 0)
				{
					return Avg;
				}

				for(const FVector3f Vert : Verts)
				{
					Avg += Vert;
				}

				return Avg / static_cast<float>(Verts.Num());
			};

		auto AxisInterval = [](TArrayView<const FVector3f> Verts, FVector3f Axis)
			{
				float Min = std::numeric_limits<float>::max();
				float Max = std::numeric_limits<float>::lowest();

				for(const FVector3f Vert : Verts)
				{
					const float Projected = FVector3f::DotProduct(Vert, Axis);
					if(Projected < Min)
					{
						Min = Projected;
					}

					if(Projected > Max)
					{
						Max = Projected;
					}
				}

				return Max - Min;
			};

		TArrayView<const FVector3f> PositionView = MakeConstArrayView(BoneVerts.Positions);
		FVector3f Extent;
		Extent.X = AxisInterval(PositionView, XAxis);
		Extent.Y = AxisInterval(PositionView, YAxis);
		Extent.Z = AxisInterval(PositionView, ZAxis);

		// Work out length and radius, length is the longest extent, radius is half the diagonal of the other two.
		float Length, Radius;
		if(Extent.X > Extent.Y && Extent.X > Extent.Z)
		{
			Length = Extent.X;
			Radius = FMath::Sqrt(Extent.Y * Extent.Y + Extent.Z * Extent.Z);
		}
		else if(Extent.Y > Extent.Z)
		{
			Length = Extent.Y;
			Radius = FMath::Sqrt(Extent.X * Extent.X + Extent.Z * Extent.Z);
		}
		else
		{
			Length = Extent.Z;
			Radius = FMath::Sqrt(Extent.X * Extent.X + Extent.Y * Extent.Y);
		}

		FMatrix ElemMatrix(static_cast<FVector>(XAxis),
			static_cast<FVector>(YAxis),
			static_cast<FVector>(ZAxis),
			FVector::ZeroVector);

		TSharedPtr<FKSphylElem> NewCapsule = MakeShared<FKSphylElem>();
		NewCapsule->Center = static_cast<FVector>(AveragePosition(BoneVerts.Positions));
		NewCapsule->Length = Length;
		NewCapsule->Radius = Radius * 0.5f;
		NewCapsule->Rotation = ElemMatrix.Rotator();

		Result.AddDefaulted();
		Result.Last().GeometryElement = NewCapsule;
	}

	return Result;
}

#if WITH_EDITOR
bool UBoneGeometryGenerator_Capsule::CanDebugDraw() const
{
	return true;
}

bool UBoneGeometryGenerator_Capsule::CanDebugDrawViewMode(const FName& ViewModeName) const
{
	return true;
}

void UBoneGeometryGenerator_Capsule::DebugDraw(UE::Dataflow::FContext& Context, IDataflowDebugDrawInterface& DataflowRenderingInterface, const FDataflowNode::FDebugDrawParameters& DebugDrawParameters, FRigidAssetBoneSelection Bones) const
{
	if(!Bones.Mesh || !Bones.Skeleton || !bDrawVerts)
	{
		return;
	}

	TArray<FTransform> BoneTransforms;
	const FReferenceSkeleton& RefSkel = Bones.Mesh->GetRefSkeleton();
	RefSkel.GetBoneAbsoluteTransforms(BoneTransforms);

	int32 BoneColorSeed = 0;
	for(const FRigidAssetBoneInfo& Bone : Bones.SelectedBones)
	{
		if(DrawVertsForBoneName != NAME_None && Bone.Name == DrawVertsForBoneName)
		{
			continue;
		}

		FBoneVertData BoneVerts = GetBoneVertexData(Bone.Name, Bones.Mesh, BaseSettings.SourceLod, BaseSettings.VertexMode);

		DataflowRenderingInterface.SetColor(FLinearColor::MakeRandomSeededColor(BoneColorSeed++));
		for(const FVector3f& Pos : BoneVerts.Positions)
		{
			DataflowRenderingInterface.DrawPoint(BoneTransforms[Bone.Index].TransformPosition(static_cast<FVector>(Pos)));
		}
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TArray<UE::Chaos::RigidAsset::FSimpleGeometry> UBoneGeometryGenerator_Convex::Build(FRigidAssetBoneSelection Bones)
{
	if(!Bones.Mesh || !Bones.Skeleton)
	{
		return {};
	}

	TArray<UE::Chaos::RigidAsset::FSimpleGeometry> Result;

	for(const FRigidAssetBoneInfo& Bone : Bones.SelectedBones)
	{
		FBoneVertData BoneVerts = GetBoneVertexData(Bone.Name, Bones.Mesh, BaseSettings.SourceLod, BaseSettings.VertexMode);

		if(BoneVerts.Positions.Num() == 0)
		{
			// No influences, make a small sphere and warn
			// #TODO pass context through for dataflow warnings
			Result.AddDefaulted();
			Result.Last().GeometryElement = MakeShared<FKSphereElem>(5.0f);
			continue;
		}

		FBox3f VertBox(BoneVerts.Positions);

		TSharedPtr<FKConvexElem> NewConvex = MakeShared<FKConvexElem>();
		TArray<Chaos::FConvexBuilder::FVec3Type> InPositions;
		TArray<Chaos::FConvexBuilder::FPlaneType> Planes;
		TArray<TArray<int32>> Indices;
		TArray<Chaos::FConvexBuilder::FVec3Type> ConvexVerts;
		Chaos::FConvexBuilder::FAABB3Type Aabb;

		InPositions.Reserve(BoneVerts.Positions.Num());
		for(const FVector3f& Vert : BoneVerts.Positions)
		{
			InPositions.Add(Vert);
		}

		Chaos::FConvexBuilder::Build(InPositions, Planes, Indices, ConvexVerts, Aabb);

		NewConvex->VertexData.Reset(ConvexVerts.Num());

		for(const Chaos::FConvexBuilder::FVec3Type& ConvexVert : ConvexVerts)
		{
			NewConvex->VertexData.Add(static_cast<FVector>(ConvexVert));
		}

		NewConvex->UpdateElemBox();

		Result.AddDefaulted();
		Result.Last().GeometryElement = NewConvex;
	}

	return Result;
}
