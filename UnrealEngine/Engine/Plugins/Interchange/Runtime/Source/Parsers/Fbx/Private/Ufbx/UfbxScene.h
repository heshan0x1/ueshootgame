// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UInterchangeBaseNodeContainer;
class UInterchangeSceneNode;

struct ufbx_node;typedef ufbx_node ufbx_node;
struct ufbx_mesh;typedef ufbx_mesh ufbx_mesh;
struct ufbx_blend_shape;typedef ufbx_blend_shape ufbx_blend_shape;

namespace UE::Interchange::Private
{
	class FUfbxParser;

	class FUfbxScene
	{
	public:
		explicit FUfbxScene(FUfbxParser& InParser);

		void InitHierarchy(UInterchangeBaseNodeContainer& NodeContainer);
		void ProcessNodes(UInterchangeBaseNodeContainer& NodeContainer);

		FString GetSkeletonRoot(ufbx_node* Node);;

	private:
		void ProcessNode(UInterchangeBaseNodeContainer& NodeContainer, const ufbx_node& Node);

		FUfbxParser& Parser;

		TSet<uint32> CommonJointRootNodes;
		TMap<ufbx_node*, FString> SkeletonRootPerBone;

		TMap<const ufbx_node*, TMap<FString, FMatrix>> JointToMeshIdToGlobalBindPoseReferenceMap;
		TMap<const ufbx_node*, TMap<FString, FMatrix>> JointToMeshIdToGlobalBindPoseJointMap;
		TMap<const ufbx_node*, FMatrix> JointToBindPoseMap;
	};

}
