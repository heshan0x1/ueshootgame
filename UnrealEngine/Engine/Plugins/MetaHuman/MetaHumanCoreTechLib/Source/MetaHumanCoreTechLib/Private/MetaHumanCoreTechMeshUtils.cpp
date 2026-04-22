// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCoreTechMeshUtils.h"
#include "MetaHumanCoreTechLibGlobals.h"
#include "DNAReader.h"
#include <carbon/geometry/KdTree.h>
#include <rig/RigGeometry.h>

#include "rig/BodyGeometry.h"

namespace UE::MetaHuman
{
	TArray<TPair<int32, float>> GetClosestUVIndices(const TArray<FVector2f>& InMeshUVs, const TArray<FVector2f>& InInputUVs)
	{
		TArray<TPair<int32, float>> CorrespondingIndices;

		const Eigen::Map<const Eigen::Matrix<float, 2, -1>> MeshUVsEigen((const float*)InMeshUVs.GetData(), 2, InMeshUVs.Num());
		const Eigen::Map<const Eigen::Matrix<float, 2, -1>> InputUVsEigen((const float*)InInputUVs.GetData(), 2, InInputUVs.Num());

		TITAN_NAMESPACE::KdTree<float, TITAN_NAMESPACE::NoCompatibility<float>, 2> MeshUVsKdTree(MeshUVsEigen.transpose());

		for (int Index = 0; Index < InputUVsEigen.cols(); ++Index)
		{
			Eigen::Vector2f InputUV = InputUVsEigen.col(Index).transpose();
			float Distance = TNumericLimits<float>::Max();
			std::pair<int64_t, float> Result = MeshUVsKdTree.getClosestPoint(InputUV, Distance);
			CorrespondingIndices.Emplace(static_cast<int32>(Result.first), Result.second);
		}
		return CorrespondingIndices;
	}

	TMap<int32, TArray<int32>> GetNeighbouringVertices(const TSharedPtr<IDNAReader>& InDNAReader, int32 InDNAMeshIndex, const TArray<int32>& InVertexIds)
	{
		TMap<int32, TArray<int32>> NeighbouringVertices;

		TITAN_NAMESPACE::RigGeometry<float> RigGeometry;
		RigGeometry.Init(InDNAReader->Unwrap());

		if (!RigGeometry.IsValid(InDNAMeshIndex))
		{
			UE_LOG(LogMetaHumanCoreTechLib, Warning, TEXT("DNA mesh index is not valid for DNA reader"));
			return {};
		}

		const TITAN_NAMESPACE::Mesh<float>& Mesh = RigGeometry.GetMesh(InDNAMeshIndex);
		std::vector<std::vector<int>> MeshVertexNeighbours;
		MeshVertexNeighbours.resize(Mesh.NumVertices());
		for (const std::pair<int, int>& Edge : Mesh.GetEdges({}))
		{
			MeshVertexNeighbours[Edge.first].push_back(Edge.second);
			MeshVertexNeighbours[Edge.second].push_back(Edge.first);
		}

		for (int32 VertexId : InVertexIds)
		{
			TArray<int32> MeshNeighbours;
			if (VertexId >= 0 && VertexId < MeshVertexNeighbours.size())
			{
				MeshNeighbours.Append(MeshVertexNeighbours[VertexId].data(), MeshVertexNeighbours[VertexId].size());
			}
			NeighbouringVertices.Add(VertexId, MoveTemp(MeshNeighbours));
		}

		return NeighbouringVertices;
	}
	
	TArray<FVector3f> GetJointWorldTranslations(const TSharedPtr<IDNAReader>& InDNAReader)
	{
		TITAN_NAMESPACE::BodyGeometry<float> BodyGeometry;
		BodyGeometry.Init(InDNAReader->Unwrap());
		TArray<FVector3f> JointTranslations; 
		JointTranslations.SetNumUninitialized(InDNAReader->GetJointCount());
		TITAN_NAMESPACE::BodyGeometry<float> Geometry;
		Geometry.Init(InDNAReader->Unwrap());
		for (uint16 ji = 0; ji < InDNAReader->GetJointCount(); ++ji)
		{
			Eigen::Vector3f joint = Geometry.GetBindMatrices()[ji].translation(); 
			// DNAWrapper is returning wrong values for UE Space so we have to do this
			JointTranslations[ji] = {static_cast<float>(joint.x()), static_cast<float>(joint.z()), static_cast<float>(joint.y())};	
		}
		return JointTranslations;
	}
}
