// Copyright Epic Games, Inc. All Rights Reserved.

#include "UfbxScene.h"

#include "InterchangeFbxSettings.h"
#include "UfbxConvert.h"
#include "UfbxParser.h"

#include "InterchangeMeshNode.h"
#include "UfbxAnimation.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Nodes/InterchangeUserDefinedAttribute.h"

#define LOCTEXT_NAMESPACE "InterchangeFbxParser"

namespace UE::Interchange::Private
{
	namespace SceneUtils
	{
		// UE::Interchange::Private::CreateTrackNodeUid
		FString CreateTrackNodeUid(const FString& JointUid, const int32 AnimationIndex)
		{
			FString TrackNodeUid = TEXT("\\SkeletalAnimation\\") + JointUid + TEXT("\\") + FString::FromInt(AnimationIndex);
			return TrackNodeUid;
		}

		UInterchangeSceneNode* CreateTransformNode(FUfbxParser& Parser, UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeDisplayLabel, const FString& NodeUID, const FTransform& LocalTransform, const FString& ParentNodeUID)
		{
			UInterchangeSceneNode* TransformNode = NewObject<UInterchangeSceneNode>(&NodeContainer, NAME_None);
			if (!ensure(TransformNode))
			{
				UInterchangeResultError_Generic* Message = Parser.AddMessage<UInterchangeResultError_Generic>();
				Message->Text = LOCTEXT("TransformNodeAllocationError", "Unable to allocate a node when importing FBX.");
				return nullptr;
			}
			NodeContainer.SetupNode(TransformNode, NodeUID, NodeDisplayLabel, EInterchangeNodeContainerType::TranslatedScene, ParentNodeUID);
			constexpr bool bResetCache = false;
			TransformNode->SetCustomLocalTransform(&NodeContainer, LocalTransform, bResetCache);


			return TransformNode;
		}

		void InitHierarchyRecursively(FUfbxParser& Parser, UInterchangeBaseNodeContainer& NodeContainer, const ufbx_node& Node, UInterchangeSceneNode* ParentSceneNode)
		{
			Convert::FNodeNameAndUid NodeNameAndUid(Parser, ParentSceneNode, Node);
			UInterchangeSceneNode* SceneNode = CreateTransformNode(Parser,
				NodeContainer, NodeNameAndUid.Label, NodeNameAndUid.UniqueID, Convert::ConvertTransform(Node.local_transform), ParentSceneNode ? ParentSceneNode->GetUniqueID() : TEXT(""));

			Parser.ElementIdToSceneNode.Add(Node.element_id, SceneNode);

			for (ufbx_node* Child : Node.children)
			{
				InitHierarchyRecursively(Parser, NodeContainer, *Child, SceneNode);
			}
		};

		// Inspired by UE::Interchange::Private::FFbxScene::FindCommonJointRootNode, to match how FbxParser creates skeleton
		// This function makes root of the skeleton to be a node just above topmost bone(node participating in skinning)
		// Not sure why this is done like this in FBX SDK 
		const ufbx_node* FindSkeletonRoot(const FUfbxParser& Parser, const ufbx_node* Bone)
		{
			while (Bone && Bone->parent)
			{
				ufbx_node* ParentNode = Bone->parent;

				bool bIsBlenderArmatureBone = false;
				ufbx_scene& Scene = *Parser.Scene;
				if (Scene.metadata.exporter == UFBX_EXPORTER_BLENDER_ASCII ||
					Scene.metadata.exporter == UFBX_EXPORTER_BLENDER_BINARY)
				{
					//Hack to support armature dummy node from blender
					//Users do not want the null attribute node named armature which is the parent of the real root bone in blender fbx file
					//This is a hack since if a rigid mesh group root node is named "armature" it will be skip
					const FString ParentName = Parser.GetElementNameRaw(ParentNode->element);

					const ufbx_node* GrandFather = ParentNode->parent;
					
					bIsBlenderArmatureBone = (GrandFather == nullptr || GrandFather == Scene.root_node) && (ParentName.Compare(TEXT("armature"), ESearchCase::IgnoreCase) == 0);
				}
				// Don't drop Armature bone with multiple nodes under armature since this will remove extra bones from the skeleton
				const bool bIgnoreBlenderArmatureBone = ParentNode->children.count > 1;

				if (ParentNode->parent &&
					((ParentNode->attrib_type == UFBX_ELEMENT_EMPTY && (!bIsBlenderArmatureBone||bIgnoreBlenderArmatureBone)) || 
						ParentNode->attrib_type == UFBX_ELEMENT_MESH || 
						ParentNode->attrib_type == UFBX_ELEMENT_BONE)
					&& ParentNode != Scene.root_node)
				{
					if (ParentNode->mesh && ParentNode->mesh->skin_deformers.count > 0)
					{
						break;
					}
					Bone = ParentNode;
				}
				else
				{
					break;
				}
			}
			return Bone;
		}

		bool IsPropTypeSupported(const ufbx_prop_type& PropType)
		{
			// Also see FUfbxParser::ConvertProperty
			switch (PropType)
			{
			case UFBX_PROP_BOOLEAN:
			case UFBX_PROP_INTEGER:
			case UFBX_PROP_NUMBER:
			case UFBX_PROP_VECTOR:
			case UFBX_PROP_COLOR:
			case UFBX_PROP_COLOR_WITH_ALPHA:
			case UFBX_PROP_STRING:
			case UFBX_PROP_DATE_TIME:
				return true;
			// #ufbx_todo: support other useful custom attributes among these types
			case UFBX_PROP_TRANSLATION:
			case UFBX_PROP_ROTATION:
			case UFBX_PROP_SCALING:
			case UFBX_PROP_DISTANCE:
			case UFBX_PROP_COMPOUND:
			case UFBX_PROP_BLOB:
			case UFBX_PROP_REFERENCE:
			case UFBX_PROP_TYPE_FORCE_32BIT:
			default: ;
			}
			return false;
		}

		FMatrix EvaluateNodeTransformGlobal(const FUfbxParser& Parser, const ufbx_node& Node, const double Time=0)
		{
			FMatrix LocalTransform = Convert::ConvertTransform(ufbx_evaluate_transform(Parser.Scene->anim, &Node, Time)).ToMatrixWithScale();

			if (Node.parent)
			{
				FMatrix ParentTransform = EvaluateNodeTransformGlobal(Parser, *Node.parent, Time);
				return LocalTransform * ParentTransform;
			}

			return LocalTransform;
		}
	}

	void FUfbxScene::ProcessNode(UInterchangeBaseNodeContainer& NodeContainer, const ufbx_node& InNode)
	{
		UInterchangeSceneNode** SceneNodeFound = Parser.ElementIdToSceneNode.Find(InNode.element_id);
		if (!ensureMsgf(SceneNodeFound, TEXT("Expected to have node already created when processing")))
		{
			return;
		}

		UInterchangeSceneNode* SceneNode = *SceneNodeFound;

		const bool bIsRootNode = &InNode == Parser.Scene->root_node;
		if (bIsRootNode)
		{
			SceneNode->SetCustomIsSceneRoot(true);
		}

		const FTransform GeometryTransform = Convert::ConvertTransform(InNode.geometry_transform);
		if (!GeometryTransform.Equals(FTransform::Identity))
		{
			SceneNode->SetCustomGeometricTransform(GeometryTransform);
		}

		if (InNode.mesh)
		{
			FString MeshUniqueID = Parser.GetMeshUid(InNode.mesh->element);
			
			if (const UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(NodeContainer.GetNode(MeshUniqueID)))
			{
				SceneNode->SetCustomAssetInstanceUid(MeshNode->GetUniqueID());
			}
		}

		auto IsInSkeleton = [this](const ufbx_node& Node)
		{
			if (CommonJointRootNodes.IsEmpty())
			{
				return false;
			}

			for (const ufbx_node* Parent = &Node;Parent;Parent = Parent->parent)
			{
				if (CommonJointRootNodes.Contains(Parent->element_id))
				{
					return true;
				}
			}
			return false;
			
		};
		
		if (InNode.bone || IsInSkeleton(InNode))
		{
			constexpr bool bResetCache = false;

			UInterchangeSceneNode* UnrealNode = SceneNode;

			// To recognize Node as a Joint, see e.g. UInterchangePipelineMeshesUtilities::CreateInterchangePipelineMeshesUtilities
			UnrealNode->AddSpecializedType(FSceneNodeStaticData::GetJointSpecializeTypeString());

			FString JointNodeName = Parser.GetBoneNodeUid(InNode);
			UnrealNode->SetDisplayLabel(JointNodeName);

			//Get the bind pose transform for this joint
			{
				// #ufbx_todo: probably, we don't actually need Global transform, as FBX Parser does since FBX SDK returns only evaluate globally, but source is local
				// so might refactor this carefully to locally evaluated transforms
				FMatrix GlobalBindPoseJointMatrix = SceneUtils::EvaluateNodeTransformGlobal(Parser, InNode, 0);

				if (FMatrix* Found = JointToBindPoseMap.Find(&InNode))
				{
					GlobalBindPoseJointMatrix = *Found;
				}

				FTransform GlobalBindPoseJointTransform(GlobalBindPoseJointMatrix);

				TMap<FString, FMatrix> MeshIdToGlobalBindPoseReferenceMap;
				if (const TMap<FString, FMatrix>* Found = JointToMeshIdToGlobalBindPoseReferenceMap.Find(&InNode))
				{
					MeshIdToGlobalBindPoseReferenceMap = *Found;
				}

				UnrealNode->SetGlobalBindPoseReferenceForMeshUIDs(MeshIdToGlobalBindPoseReferenceMap);

				if (const TMap<FString, FMatrix>* Found = JointToMeshIdToGlobalBindPoseJointMap.Find(&InNode))
				{
					for (const TPair<FString, FMatrix>& MeshIdAndBindPoseJointMatrix : *Found)
					{
						FString AttributeKey = TEXT("JointBindPosePerMesh_") + MeshIdAndBindPoseJointMatrix.Key;
						UnrealNode->RegisterAttribute<FMatrix>(FAttributeKey(AttributeKey), MeshIdAndBindPoseJointMatrix.Value);
					}
				}

				if (InNode.parent)
				{
					FMatrix GlobalFbxParentMatrix = SceneUtils::EvaluateNodeTransformGlobal(Parser, *InNode.parent, 0);
					if (FMatrix* Found = JointToBindPoseMap.Find(InNode.parent))
					{
						GlobalFbxParentMatrix = *Found;
					}

					FMatrix LocalFbxMatrix = GlobalBindPoseJointMatrix * GlobalFbxParentMatrix.Inverse();
					FTransform LocalBindPoseJointTransform(LocalFbxMatrix);

					UnrealNode->SetCustomBindPoseLocalTransform(&NodeContainer, LocalBindPoseJointTransform, bResetCache);
				}
				else
				{
					UnrealNode->SetCustomBindPoseLocalTransform(&NodeContainer, GlobalBindPoseJointTransform, bResetCache);
				}
			}

			//Get time Zero transform for this joint
			{
				FMatrix GlobalFbxMatrix = SceneUtils::EvaluateNodeTransformGlobal(Parser, InNode, 0);
				UnrealNode->SetCustomGlobalMatrixForT0Rebinding(GlobalFbxMatrix);

				FTransform GlobalTransform(GlobalFbxMatrix);

				if (InNode.parent)
				{
					FMatrix GlobalFbxParentMatrix = SceneUtils::EvaluateNodeTransformGlobal(Parser, *InNode.parent, 0);

					FMatrix LocalFbxMatrix = GlobalFbxMatrix * GlobalFbxParentMatrix.Inverse();
					FTransform LocalTransform(LocalFbxMatrix);

					UnrealNode->SetCustomTimeZeroLocalTransform(&NodeContainer, LocalTransform, bResetCache);
				}
				else
				{
					UnrealNode->SetCustomTimeZeroLocalTransform(&NodeContainer, GlobalTransform, bResetCache);
				}
			}

			//Set Default transforms with JointOrientation applied
			{
				FMatrix GlobalFbxMatrix = Convert::ConvertMatrix(InNode.node_to_world);

				if (InNode.parent)
				{
					FMatrix GlobalFbxParentMatrix = Convert::ConvertMatrix(InNode.parent->node_to_world);

					FMatrix	LocalFbxMatrix = GlobalFbxMatrix * GlobalFbxParentMatrix.Inverse();
					FTransform LocalTransform(LocalFbxMatrix);

					UnrealNode->SetCustomLocalTransform(&NodeContainer, LocalTransform, bResetCache);
				}
				else
				{
					FTransform GlobalTransform(GlobalFbxMatrix);
					//No parent, set the same matrix has the global
					UnrealNode->SetCustomLocalTransform(&NodeContainer, GlobalTransform, bResetCache);
				}
			}
		}

		if (InNode.attrib_type == UFBX_ELEMENT_LOD_GROUP)
		{
			SceneNode->AddSpecializedType(FSceneNodeStaticData::GetLodGroupSpecializeTypeString());
		}

		for (const ufbx_prop& Prop : InNode.props.props)
		{
			if (SceneUtils::IsPropTypeSupported(Prop.type) &&
				Prop.flags & UFBX_PROP_FLAG_USER_DEFINED)
			{
				TOptional<FString> PayloadKey;
				if (Prop.flags & UFBX_PROP_FLAG_ANIMATED)
				{
					for (const ufbx_connection& Connection : InNode.element.connections_dst)
					{
						// Property and its connection have same name string pointer in ufbx
						if (Connection.dst_prop.data == Prop.name.data)
						{
							if (ufbx_anim_value* AnimValue = ufbx_as_anim_value(Connection.src))
							{
								FString CurveName;
								TOptional<bool> bIsStepCurve;
								if (FUfbxAnimation::AddNodeAttributeCurvesAnimation(Parser, SceneNode->GetUniqueID(), Prop, *AnimValue, PayloadKey, bIsStepCurve, CurveName))
								{
									EInterchangeAnimationPayLoadType PayloadType = bIsStepCurve.GetValue() ? EInterchangeAnimationPayLoadType::STEPCURVE : EInterchangeAnimationPayLoadType::CURVE;

									const UInterchangeFbxSettings* InterchangeFbxSettings = GetDefault<UInterchangeFbxSettings>();
									if (EInterchangePropertyTracks PropertyTrack = InterchangeFbxSettings->GetPropertyTrack(CurveName); PropertyTrack != EInterchangePropertyTracks::None)
									{
										UInterchangeAnimationTrackNode* AnimTrackNode = NewObject< UInterchangeAnimationTrackNode >(&NodeContainer);
										const FString AnimTrackNodeName = SceneNode->GetDisplayLabel() + CurveName;
										const FString AnimTrackNodeUid = TEXT("\\AnimationTrack\\") + AnimTrackNodeName;

										NodeContainer.SetupNode(AnimTrackNode, AnimTrackNodeUid, AnimTrackNodeName, EInterchangeNodeContainerType::TranslatedAsset);

										AnimTrackNode->SetCustomActorDependencyUid(SceneNode->GetUniqueID());
										AnimTrackNode->SetCustomAnimationPayloadKey(PayloadKey.GetValue(), PayloadType);
										AnimTrackNode->SetCustomPropertyTrack(PropertyTrack);
									}

									SceneNode->SetAnimationCurveTypeForCurveName(CurveName, PayloadType);
								}
							}
							break;
						}
					}
				}
				Parser.ConvertProperty(Prop, SceneNode, PayloadKey);
			}
		}

	}

	void FUfbxScene::ProcessNodes(UInterchangeBaseNodeContainer& NodeContainer)
	{
		for (ufbx_node* Node : Parser.Scene->nodes)
		{
			ProcessNode(NodeContainer, *Node);
		}
	}

	FUfbxScene::FUfbxScene(FUfbxParser& InParser): Parser(InParser)
	{
		// Collect bind pose matrices
		for (ufbx_mesh* Mesh : Parser.Scene->meshes)
		{
			// #ufbx_todo: fix getting existing mesh element Uid using ufbx_mesh param instead of element
			FString MeshUid = Parser.GetMeshUid(Mesh->element);
			for (const ufbx_skin_deformer* Deformer : Mesh->skin_deformers)
			{
					
				for (const ufbx_skin_cluster* Cluster : Deformer->clusters)
				{
					const FMatrix GeometryToBone = Convert::ConvertMatrix(Cluster->geometry_to_bone);
					const FMatrix BindToWorld = Convert::ConvertMatrix(Cluster->bind_to_world);
					JointToMeshIdToGlobalBindPoseReferenceMap.FindOrAdd(Cluster->bone_node).Add(MeshUid, GeometryToBone*BindToWorld);
				}
			}
			for (ufbx_skin_deformer* Deformer : Mesh->skin_deformers)
			{
				for (ufbx_skin_cluster* Cluster : Deformer->clusters)
				{
					
					TMap<FString, FMatrix>& MeshIdToGlobalBindPoseJointMap = JointToMeshIdToGlobalBindPoseJointMap.FindOrAdd(Cluster->bone_node);

					const FMatrix BindMatrix = Convert::ConvertMatrix(Cluster->bind_to_world);
					MeshIdToGlobalBindPoseJointMap.Add(MeshUid, BindMatrix);

					if (!JointToBindPoseMap.Contains(Cluster->bone_node))
					{
						JointToBindPoseMap.Add(Cluster->bone_node, BindMatrix);
					}
					else
					{
						ensureMsgf(JointToBindPoseMap[Cluster->bone_node].Equals(BindMatrix, KINDA_SMALL_NUMBER), TEXT("Expected to have same joint node in different clusters to have same Bind Pose matrix!"));
					}
				}
			}
		}
	}

	void FUfbxScene::InitHierarchy(UInterchangeBaseNodeContainer& NodeContainer)
	{
		ufbx_node* RootNode = Parser.Scene->root_node;
		Parser.ElementIdToSceneNode.Reserve(Parser.Scene->nodes.count);

		SceneUtils::InitHierarchyRecursively(Parser, NodeContainer, *RootNode, nullptr);

		for (const ufbx_node* Node : Parser.Scene->nodes)
		{
			if (!ensureMsgf(Node, TEXT("Not expected ufbx scene nodes to contain a nullptr")))
			{
				continue;
			}
			
			if (Node->bone)
			{
				if (uint32 SkeletonRoot = SceneUtils::FindSkeletonRoot(Parser, Node)->element_id)
				{
					CommonJointRootNodes.FindOrAdd(SkeletonRoot);
				}
			}

			if (!Node->parent && (Node->element_id != RootNode->element_id)) 
			{
				SceneUtils::InitHierarchyRecursively(Parser, NodeContainer, *Node, nullptr);
			}
		}
	}

	FString FUfbxScene::GetSkeletonRoot(ufbx_node* Node)
	{
		FString* SkeletonRootFound = SkeletonRootPerBone.Find(Node);
		return SkeletonRootFound ? *SkeletonRootFound : Parser.GetNodeUid(*SceneUtils::FindSkeletonRoot(Parser, Node));
	}
}

#undef LOCTEXT_NAMESPACE
