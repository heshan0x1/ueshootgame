// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeGenericMeshPipeline.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Async.h"
#include "CoreMinimal.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeMeshLODContainerNode.h"
#include "InterchangeMeshNode.h"
#include "InterchangePipelineLog.h"
#include "InterchangePipelineMeshesUtilities.h"
#include "InterchangeSceneComponentNodes.h"
#include "InterchangeSceneNode.h"
#include "InterchangeStaticMeshFactoryNode.h"
#include "InterchangeStaticMeshLodDataNode.h"
#include "Mesh/InterchangeMeshHelper.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "Nodes/InterchangeSourceNode.h"
#include "Nodes/InterchangeUserDefinedAttribute.h"

#include "Async/Async.h"
#include "CoreMinimal.h"
#include "Tasks/Task.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

namespace UE::Interchange::Private
{
	FString GetNodeName(TObjectPtr<UInterchangePipelineMeshesUtilities> PipelineMeshesUtilities, const UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeUid)
	{
		const UInterchangeBaseNode* BaseNode = NodeContainer.GetNode(NodeUid);
		if (BaseNode)
		{
			FString NodeName = BaseNode->GetDisplayLabel();
			if (const UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(BaseNode))
			{
				if (SceneNode->IsSpecializedTypeContains(UE::Interchange::FSceneNodeStaticData::GetLodGroupSpecializeTypeString()))
				{
					TArray<FString> LodGroupChildrens = NodeContainer.GetNodeChildrenUids(SceneNode->GetUniqueID());
					if (LodGroupChildrens.Num() > 0)
					{
						return GetNodeName(PipelineMeshesUtilities, NodeContainer, LodGroupChildrens[0]);
					}
				}
			}
			else if (const UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(BaseNode))
			{
				//If this mesh is reference by only one scene node that do not have any children, use the scene node display label
				const FInterchangeMeshGeometry& MeshGeometry = PipelineMeshesUtilities->GetMeshGeometryByUid(NodeUid);
				if (MeshGeometry.ReferencingMeshInstanceUids.Num() == 1
					&& NodeContainer.GetNodeChildrenCount(MeshGeometry.ReferencingMeshInstanceUids[0]) == 0)
				{
					if(const UInterchangeBaseNode* InstanceMeshNode = NodeContainer.GetNode(MeshGeometry.ReferencingMeshInstanceUids[0]))
					{
						return InstanceMeshNode->GetDisplayLabel();
					}
				}
			}

			return NodeName;
		}
		else
		{
			return FString();
		}
	}
}

static TTuple<EInterchangeMeshCollision, FString> GetCollisionMeshType(
	TObjectPtr<UInterchangePipelineMeshesUtilities> PipelineMeshesUtilities,
	const UInterchangeBaseNodeContainer& NodeContainer,
	const FString& NodeUid,
	const TArray<FString>& AllNodeUids,
	bool bImportCollisionAccordingToMeshName
)
{
	EInterchangeMeshCollision CollisionType = EInterchangeMeshCollision::None;

	// Try to find the mesh node we're actually talking about
	const UInterchangeBaseNode* BaseNode = NodeContainer.GetNode(NodeUid);
	const UInterchangeMeshNode* MeshNode = Cast<const UInterchangeMeshNode>(BaseNode);
	if (!MeshNode)
	{
		if (const UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(BaseNode))
		{
			FString MeshDependency;
			if (SceneNode->GetCustomAssetInstanceUid(MeshDependency))
			{
				MeshNode = Cast<const UInterchangeMeshNode>(NodeContainer.GetNode(MeshDependency));
			}
		}
	}

	// If we have an explicit collision type on the mesh, we know the mesh node is a collision mesh itself
	if (MeshNode)
	{
		if (MeshNode->GetCustomCollisionType(CollisionType) && CollisionType != EInterchangeMeshCollision::None)
		{
			return {CollisionType, NodeUid};
		}
	}

	if (bImportCollisionAccordingToMeshName)
	{
		FString MeshName = UE::Interchange::Private::GetNodeName(PipelineMeshesUtilities, NodeContainer, NodeUid);

		// Determine if the mesh name is a potential collision mesh

		if (MeshName.StartsWith(TEXT("UBX_")))
		{
			CollisionType = EInterchangeMeshCollision::Box;
		}
		else if (MeshName.StartsWith(TEXT("UCX_")) || MeshName.StartsWith(TEXT("MCDCX_")))
		{
			CollisionType = EInterchangeMeshCollision::Convex18DOP;
		}
		else if (MeshName.StartsWith(TEXT("USP_")))
		{
			CollisionType = EInterchangeMeshCollision::Sphere;
		}
		else if (MeshName.StartsWith(TEXT("UCP_")))
		{
			CollisionType = EInterchangeMeshCollision::Capsule;
		}
		else
		{
			return { EInterchangeMeshCollision::None, FString() };
		}

		// We have a mesh name with a collision type suffix.
		// However it should only be treated as a collision mesh if its body name corresponds to one of the other meshes.
		// If we get here, we know there is at least one underscore, so we never expect either of these character searches to fail.

		int32 FirstUnderscore = INDEX_NONE;
		verify(MeshName.FindChar(TEXT('_'), FirstUnderscore));

		int32 LastUnderscore = INDEX_NONE;
		verify(MeshName.FindLastChar(TEXT('_'), LastUnderscore));

		auto MatchPredicate = [&PipelineMeshesUtilities , &NodeContainer](FString Body)
		{
			// Generate a predicate to be used by the below Finds.
			return [Body, &PipelineMeshesUtilities, &NodeContainer](const FString& ToCompare) { return Body == UE::Interchange::Private::GetNodeName(PipelineMeshesUtilities, NodeContainer, ToCompare); };
		};

		// If we find a mesh named the same as the collision mesh (following the collision prefix), we have a match
		// e.g. this will match 'UBX_House' with a mesh called 'House'
		{
			const FString RenderMeshName = MeshName.RightChop(FirstUnderscore + 1);
			const FString* CorrespondingMeshUid = AllNodeUids.FindByPredicate(MatchPredicate(RenderMeshName));
			if (CorrespondingMeshUid)
			{
				return { CollisionType, *CorrespondingMeshUid };
			}
		}

		// Otherwise strip the final underscore suffix from the collision mesh name and look again
		// e.g. this will match 'UBX_House_01' with a mesh called 'House'
		if (FirstUnderscore != LastUnderscore)
		{
			const FString RenderMeshName = MeshName.Mid(FirstUnderscore + 1, LastUnderscore - FirstUnderscore - 1);
			const FString* CorrespondingMeshUid = AllNodeUids.FindByPredicate(MatchPredicate(RenderMeshName));
			if (CorrespondingMeshUid)
			{
				return { CollisionType, *CorrespondingMeshUid };
			}
		}

		// It is possible our target mesh has been renamed away by a name sanitizing process (see UE-248981 and
		// FFbxParser::EnsureNodeNameAreValid). Since it is unusual to have a prefixed collision mesh and no target, let's 
		// assume we actually have a render mesh somewhere and try stripping any nameclash-looking suffixes from mesh names 
		// to see if we have a match
		{
			// Like above start by just stripping the prefix (e.g. 'UBX_House_01' --> 'House_01')
			FString RenderMeshName = MeshName.RightChop(FirstUnderscore + 1);

			auto NameCollisionSearchPredicate = [&RenderMeshName, &PipelineMeshesUtilities, &NodeContainer](const FString& Other)
			{
				FString MeshName = UE::Interchange::Private::GetNodeName(PipelineMeshesUtilities, NodeContainer, Other);
				if (!MeshName.StartsWith(RenderMeshName))
				{
					return false;
				}

				FString MeshNameSuffix = MeshName.RightChop(RenderMeshName.Len());

				// Check if everything after the render mesh name is just some combination of "ncl" (nameclash), numbers and/or underscores
				MeshNameSuffix = MeshNameSuffix.Replace(TEXT("ncl"), TEXT(""));

				FString LastChar = MeshNameSuffix.Right(1);
				while ((LastChar.IsNumeric() || LastChar == TEXT("_")) && MeshNameSuffix.Len() > 0)
				{
					MeshNameSuffix.LeftChopInline(1, EAllowShrinking::No);
					LastChar = MeshNameSuffix.Right(1);
				}

				// If there's nothing left, everything after the RenderMeshName was nameclash stuff, so this is likely our mesh
				return MeshNameSuffix.Len() == 0;
			};

			const FString* CorrespondingMeshUid = AllNodeUids.FindByPredicate(NameCollisionSearchPredicate);
			if (CorrespondingMeshUid)
			{
				return {CollisionType, *CorrespondingMeshUid};
			}

			// If we still have nothing, try also stripping numbered suffixes from the render mesh name and repeating the search
			// (e.g. 'UBX_House_01' --> 'House')
			RenderMeshName = MeshName.Mid(FirstUnderscore + 1, LastUnderscore - FirstUnderscore - 1);
			CorrespondingMeshUid = AllNodeUids.FindByPredicate(NameCollisionSearchPredicate);
			if (CorrespondingMeshUid)
			{
				return {CollisionType, *CorrespondingMeshUid};
			}
		}
	}

	return { EInterchangeMeshCollision::None, FString() };
}

// Returns true if MeshUid describes a Mesh node that is purely the collision mesh of some other mesh
static bool IsJustCollisionMesh(TObjectPtr<UInterchangePipelineMeshesUtilities> PipelineMeshesUtilities, const UInterchangeBaseNodeContainer& NodeContainer, const FString& MeshUid, const TArray<FString>& MeshUids, bool bImportCollisionAccordingToMeshName)
{
	TTuple<EInterchangeMeshCollision, FString> Tuple = GetCollisionMeshType(PipelineMeshesUtilities, NodeContainer, MeshUid, MeshUids, bImportCollisionAccordingToMeshName);

	// If MeshUid is a collision mesh but *of itself*, then it is both a collision and a render mesh, and we should return false
	return Tuple.Key != EInterchangeMeshCollision::None && Tuple.Value != MeshUid;
}


static void BuildMeshToCollisionMeshMap(TObjectPtr<UInterchangePipelineMeshesUtilities> PipelineMeshesUtilities, const UInterchangeBaseNodeContainer& NodeContainer, const TArray<FString>& MeshUids, TMap<FString, TArray<FString>>& MeshToCollisionMeshMap, bool bImportCollisionAccordingToMeshName)
{
	for (const FString& MeshUid : MeshUids)
	{
		TTuple<EInterchangeMeshCollision, FString> CollisionType = GetCollisionMeshType(PipelineMeshesUtilities, NodeContainer, MeshUid, MeshUids, bImportCollisionAccordingToMeshName);
		if (CollisionType.Get<0>() != EInterchangeMeshCollision::None)
		{
			MeshToCollisionMeshMap.FindOrAdd(FString(CollisionType.Get<1>())).Emplace(MeshUid);
		}
	}
}


void UInterchangeGenericMeshPipeline::ExecutePreImportPipelineStaticMesh()
{
	check(CommonMeshesProperties.IsValid());

#if WITH_EDITOR
	//Make sure the generic pipeline will cover all staticmesh build settings when we import
	Async(EAsyncExecution::TaskGraphMainThread, []()
		{
			static bool bVerifyBuildProperties = false;
			if (!bVerifyBuildProperties)
			{
				bVerifyBuildProperties = true;
				TArray<const UClass*> Classes;
				Classes.Add(UInterchangeGenericCommonMeshesProperties::StaticClass());
				Classes.Add(UInterchangeGenericMeshPipeline::StaticClass());
				if (!DoClassesIncludeAllEditableStructProperties(Classes, FMeshBuildSettings::StaticStruct()))
				{
					UE_LOG(LogInterchangePipeline, Log, TEXT("UInterchangeGenericMeshPipeline: The generic pipeline does not cover all static mesh build options."));
				}
			}
		});
#endif

	if (UInterchangeSourceNode* SourceNode = UInterchangeSourceNode::FindOrCreateUniqueInstance(BaseNodeContainer))
	{
		// Keep a single triangle threshold for the entire scene
		SourceNode->SetCustomNaniteTriangleThreshold(NaniteTriangleThreshold);
	}

	if (bImportStaticMeshes && (CommonMeshesProperties->ForceAllMeshAsType == EInterchangeForceMeshType::IFMT_None || CommonMeshesProperties->ForceAllMeshAsType == EInterchangeForceMeshType::IFMT_StaticMesh))
	{
		if (bCombineStaticMeshes)
		{
			// Combine all the static meshes

			bool bFoundMeshes = false;
			{
				// If baking transforms, get all the static mesh instance nodes, and group them by LOD
				TArray<FString> MeshUids;
				PipelineMeshesUtilities->GetAllStaticMeshInstance(MeshUids);

				TMap<int32, TArray<FString>> MeshUidsPerLodIndex;

				for (const FString& MeshUid : MeshUids)
				{
					const FInterchangeMeshInstance& MeshInstance = PipelineMeshesUtilities->GetMeshInstanceByUid(MeshUid);
					for (const auto& LodIndexAndSceneNodeContainer : MeshInstance.SceneNodePerLodIndex)
					{
						const int32 LodIndex = LodIndexAndSceneNodeContainer.Key;
						const FInterchangeLodSceneNodeContainer& SceneNodeContainer = LodIndexAndSceneNodeContainer.Value;

						TArray<FString>& TranslatedNodes = MeshUidsPerLodIndex.FindOrAdd(LodIndex);
						for (const UInterchangeBaseNode* BaseNode : SceneNodeContainer.BaseNodes)
						{
							TranslatedNodes.Add(BaseNode->GetUniqueID());
						}
					}
				}

				//We must add all nodes that are not part of any lod to all lods upper then 0
				if (MeshUidsPerLodIndex.Num() > 1)
				{
					constexpr int32 LodIndexZero = 0;
					for (const FString& MeshUid : MeshUids)
					{
						const FInterchangeMeshInstance& MeshInstance = PipelineMeshesUtilities->GetMeshInstanceByUid(MeshUid);
						if (MeshInstance.LodGroupNode == nullptr
							&& MeshInstance.SceneNodePerLodIndex.Num() == 1
							&& MeshInstance.SceneNodePerLodIndex.Contains(LodIndexZero)
							&& MeshInstance.SceneNodePerLodIndex[LodIndexZero].BaseNodes.Num() == 1)
						{
							const UInterchangeBaseNode* BaseNode = MeshInstance.SceneNodePerLodIndex[LodIndexZero].BaseNodes[0];
							//This mesh must be added to all existing LODs over 0
							for (TPair<int32, TArray<FString>>& MeshUidsPerLodIndexPair : MeshUidsPerLodIndex)
							{
								if (MeshUidsPerLodIndexPair.Key > LodIndexZero)
								{
									TArray<FString>& TranslatedNodes = MeshUidsPerLodIndexPair.Value;
									TranslatedNodes.AddUnique(BaseNode->GetUniqueID());
								}
							}
						}
					}
				}

				// If we got some instances, create a static mesh factory node
				if (MeshUidsPerLodIndex.Num() > 0)
				{
					UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = CreateStaticMeshFactoryNode(MeshUidsPerLodIndex);
					StaticMeshFactoryNodes.Add(StaticMeshFactoryNode);
					bFoundMeshes = true;
				}
			}

			if (!bFoundMeshes)
			{
				// If we haven't yet managed to build a factory node, look at static mesh geometry directly.
				TArray<FString> MeshUids;
				PipelineMeshesUtilities->GetAllStaticMeshGeometry(MeshUids);

				TMap<int32, TArray<FString>> MeshUidsPerLodIndex;

				for (const FString& MeshUid : MeshUids)
				{
					// MeshGeometry cannot have Lod since LODs are defined in the scene node
					const FInterchangeMeshGeometry& MeshGeometry = PipelineMeshesUtilities->GetMeshGeometryByUid(MeshUid);
					const int32 LodIndex = 0;
					TArray<FString>& TranslatedNodes = MeshUidsPerLodIndex.FindOrAdd(LodIndex);
					TranslatedNodes.Add(MeshGeometry.MeshUid);
				}

				if (MeshUidsPerLodIndex.Num() > 0)
				{
					UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = CreateStaticMeshFactoryNode(MeshUidsPerLodIndex);
					StaticMeshFactoryNodes.Add(StaticMeshFactoryNode);
				}
			}

		}
		else
		{
			// Do not combine static meshes

			auto CreateMeshFactoryNodeUnCombined = [this](const TArray<FString>& MeshUids, const bool bInstancedMesh)->bool
				{
					constexpr int32 LodIndexZero = 0;
					bool bFoundMeshes = false;

					// Work out which meshes are collision meshes which correspond to another mesh
					TMap<FString, TArray<FString>> MeshToCollisionMeshMap;
					BuildMeshToCollisionMeshMap(PipelineMeshesUtilities, *BaseNodeContainer, MeshUids, MeshToCollisionMeshMap, bImportCollisionAccordingToMeshName);

					// Now iterate through each mesh UID, creating a new factory for each one
					for (const FString& MeshUid : MeshUids)
					{
						// If this is just a collision mesh, don't add a factory node; it will be added as part of another factory node
						if (IsJustCollisionMesh(PipelineMeshesUtilities, *BaseNodeContainer, MeshUid, MeshUids, bImportCollisionAccordingToMeshName))
						{
							continue;
						}

						TMap<int32, TArray<FString>> MeshUidsPerLodIndex;
						if (bInstancedMesh)
						{
							//Instanced geometry can have lods
							const FInterchangeMeshInstance& MeshInstance = PipelineMeshesUtilities->GetMeshInstanceByUid(MeshUid);
							for (const auto& LodIndexAndSceneNodeContainer : MeshInstance.SceneNodePerLodIndex)
							{
								const int32 LodIndex = LodIndexAndSceneNodeContainer.Key;
								const FInterchangeLodSceneNodeContainer& SceneNodeContainer = LodIndexAndSceneNodeContainer.Value;

								TArray<FString>& TranslatedNodes = MeshUidsPerLodIndex.FindOrAdd(LodIndex);
								for (const UInterchangeBaseNode* BaseNode : SceneNodeContainer.BaseNodes)
								{
									TranslatedNodes.Add(BaseNode->GetUniqueID());
								}
							}
						}
						else
						{
							// #interchange_lod_rework
							//Original presumption was: 'Non instanced geometry cannot have lods', which is not quite correct,
							// will have to fix when refactoring LOD handling.
							const FInterchangeMeshGeometry& MeshGeometry = PipelineMeshesUtilities->GetMeshGeometryByUid(MeshUid);
							TArray<FString>& TranslatedNodes = MeshUidsPerLodIndex.FindOrAdd(LodIndexZero);
							TranslatedNodes.Add(MeshGeometry.MeshUid);
						}

						if (MeshUidsPerLodIndex.Num() > 0)
						{
							if (bCollision)
							{
								if (const TArray<FString>* CorrespondingCollisionMeshes = MeshToCollisionMeshMap.Find(MeshUid))
								{
									TArray<FString>& TranslatedNodes = MeshUidsPerLodIndex.FindOrAdd(LodIndexZero);
									for (const FString& CollisionMesh : *CorrespondingCollisionMeshes)
									{
										// AddUnique here because now we support meshes being both collision AND render meshes at the
										// same time we may already have these entries in MeshUidsPerLodIndex
										TranslatedNodes.AddUnique(CollisionMesh);
									}
								}
							}

							if (UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = CreateStaticMeshFactoryNode(MeshUidsPerLodIndex, MeshUid))
							{
								StaticMeshFactoryNodes.Add(StaticMeshFactoryNode);
								UpdateAssemblyPartDependencyTable(StaticMeshFactoryNode, MeshUidsPerLodIndex);
								bFoundMeshes = true;
							}
						}
					}
					return bFoundMeshes;
				};

			TArray<FString> MeshUids;
			PipelineMeshesUtilities->GetAllStaticMeshInstance(MeshUids);
			if (!CreateMeshFactoryNodeUnCombined(MeshUids, true/*bInstancedMesh*/))
			{
				MeshUids.Reset();
				PipelineMeshesUtilities->GetAllStaticMeshGeometry(MeshUids);
				CreateMeshFactoryNodeUnCombined(MeshUids, false/*bInstancedMesh*/);
			}
		}
	}

	CreateAssemblyPartDependencies();
}

namespace UE::InterchangeStaticMeshFactory::Private
{
	void ApplyNaniteTriangleThreshold(
		const UInterchangeStaticMeshFactoryNode* FactoryNode,
		UStaticMesh* StaticMesh,
		const UInterchangeBaseNodeContainer* NodeContainer
	)
	{
#if WITH_EDITORONLY_DATA
		if (!FactoryNode || !NodeContainer || !StaticMesh || !StaticMesh->GetNaniteSettings().bEnabled)
		{
			return;
		}

		int64 TriangleThreshold = 0;
		if (const UInterchangeSourceNode* SourceNode = UInterchangeSourceNode::GetUniqueInstance(NodeContainer))
		{
			SourceNode->GetCustomNaniteTriangleThreshold(TriangleThreshold);
		}
		if (TriangleThreshold <= 0)
		{
			return;
		}

		const int32 LodIndex = 0;
		FMeshDescription* MeshDescription = StaticMesh->GetMeshDescription(LodIndex);
		if (!MeshDescription)
		{
			return;
		}

		if (MeshDescription->Triangles().Num() < TriangleThreshold)
		{
			StaticMesh->GetNaniteSettings().bEnabled = false;
		}
#endif	  // #if WITH_EDITORONLY_DATA
	}
}	 // namespace UE::InterchangeStaticMeshFactory::Private

void UInterchangeGenericMeshPipeline::ExecutePostFactoryPipelineStaticMesh(
	const UInterchangeBaseNodeContainer* InBaseNodeContainer,
	const FString& NodeKey,
	UObject* CreatedAsset,
	bool bIsAReimport
)
{
	UStaticMesh* StaticMesh = Cast<UStaticMesh>(CreatedAsset);
	if (!StaticMesh)
	{
		return;
	}

	UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = Cast<UInterchangeStaticMeshFactoryNode>(InBaseNodeContainer->GetFactoryNode(NodeKey));
	if (!StaticMeshFactoryNode)
	{
		return;
	}

	// If we're reimporting and being told to leave the properties alone, just early out instead
	if (bIsAReimport)
	{
		EReimportStrategyFlags Strategy = StaticMeshFactoryNode->GetReimportStrategyFlags();
		if (Strategy == EReimportStrategyFlags::ApplyNoProperties)
		{
			return;
		}
	}

	UE::InterchangeStaticMeshFactory::Private::ApplyNaniteTriangleThreshold(StaticMeshFactoryNode, StaticMesh, InBaseNodeContainer);
}

bool UInterchangeGenericMeshPipeline::MakeMeshFactoryNodeUidAndDisplayLabel(const TMap<int32, TArray<FString>>& MeshUidsPerLodIndex, int32 LodIndex, FString& NewNodeUid, FString& DisplayLabel, const FString& BaseMeshUid)
{
	int32 SceneNodeCount = 0;

	if (!ensure(LodIndex >= 0 && MeshUidsPerLodIndex.Num() > LodIndex))
	{
		return false;
	}

	auto SetUidLabel = [&NewNodeUid, &DisplayLabel](const UInterchangeBaseNode* BaseNode)
		{
			NewNodeUid = BaseNode->GetUniqueID();
			DisplayLabel = BaseNode->GetDisplayLabel();
		};

	auto SetUidLabelFromSceneNode = [&NewNodeUid, &DisplayLabel](const UInterchangeSceneNode* SceneNode, bool& bIsLodGroup)
		{
			bIsLodGroup = SceneNode->IsSpecializedTypeContains(UE::Interchange::FSceneNodeStaticData::GetLodGroupSpecializeTypeString());
			//We can't have the ScenNode's id become a FactoryNodeUID as is, so we are adding a \\Mesh\\ prefix.
			NewNodeUid = (bIsLodGroup ? TEXT("\\MeshLODGroupInstance\\") : TEXT("\\MeshInstance\\")) + SceneNode->GetUniqueID();
			DisplayLabel = SceneNode->GetDisplayLabel();
		};

	if (!BaseMeshUid.IsEmpty())
	{
		//BaseMeshUid represents either:
		//	- UInterchangeSceneNode's UID that is a LOD Group.
		//	- UInterchangeSceneNode's UID that is instantiating a UInterchangeMeshNode. (via CustomAssetInstanceUid).
		//	- UInterchangeMeshNode's UID.
		//	- UInterchangeMeshLODContainerNode's UID.
		//	- in case of bCombineStaticMeshes == true this ID should be empty for now.

		const UInterchangeBaseNode* BaseNode = BaseNodeContainer->GetNode(BaseMeshUid);
		if (BaseNode)
		{
			if (const UInterchangeSceneNode* SceneNode = Cast<const UInterchangeSceneNode>(BaseNode))
			{
				//Regardless of the SceneNode being a LOD Group, or just instantiating a Mesh,
				// if it was marked as the baseMeshUid we should use it's UID and Name:

				bool bIsLodGroup = false;
				SetUidLabelFromSceneNode(SceneNode, bIsLodGroup);

				if (!CommonMeshesProperties->bBakeMeshes && !bIsLodGroup)
				{
					//We recursively check the AssetInstanceUid's chain to make sure we find LOD Group or MeshUID.
					FString AssetInstanceUid;
					while (SceneNode && SceneNode->GetCustomAssetInstanceUid(AssetInstanceUid))
					{
						const UInterchangeBaseNode* AssetInstanceNode = BaseNodeContainer->GetNode(AssetInstanceUid);
						SceneNode = Cast<UInterchangeSceneNode>(AssetInstanceNode); //if its a sceneNode without a LOD group specialization we continue the search
						if (SceneNode && SceneNode->IsSpecializedTypeContains(UE::Interchange::FSceneNodeStaticData::GetLodGroupSpecializeTypeString()))
						{
							//if it is a LOD Group specialization then we use the original NewNodeUid. (as then we want the 'instantiation of the LOD Group)
							NewNodeUid = TEXT("\\MeshLODGroupInstance\\") + BaseMeshUid;
							return true;
						}
						if (const UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(AssetInstanceNode))
						{
							//if it is a Mesh Node then we confirmed that it is an instanced Mesh
							// but since we are not baking meshes we want the MeshNode
							//It cannot be part of a LOD group as that would have broken out sooner due to the LodGroup check.
							//For this case we are using MeshNode's UID and DisplayName.
							SetUidLabel(AssetInstanceNode);

							return true;
						}
					}
				}
				else
				{
					return true;
				}
			}
			else if (const UInterchangeMeshLODContainerNode* LODContainerNode = Cast<const UInterchangeMeshLODContainerNode>(BaseNode))
			{
				SetUidLabel(BaseNode);

				return true;
			}
			else if (const UInterchangeMeshNode* MeshNode = Cast<const UInterchangeMeshNode>(BaseNode))
			{
				SetUidLabel(BaseNode);

				return true;
			}
		}
	}

	//If BaseMeshUID is not provided and or BaseMeshUID is not a LOD Group, fallback onto LOD=0 UID[0] to generate UID and Name.
	for (const TPair<int32, TArray<FString>>& LodIndexAndMeshUids : MeshUidsPerLodIndex)
	{
		if (LodIndex == LodIndexAndMeshUids.Key)
		{
			const TArray<FString>& Uids = LodIndexAndMeshUids.Value;
			if (Uids.Num() > 0)
			{
				const FString& Uid = Uids[0];
				const UInterchangeBaseNode* BaseNode = BaseNodeContainer->GetNode(Uid);

				if (const UInterchangeMeshNode* MeshNode = Cast<const UInterchangeMeshNode>(BaseNode))
				{
					SetUidLabel(BaseNode);

					return true;
				}

				if (const UInterchangeSceneNode* SceneNode = Cast<const UInterchangeSceneNode>(BaseNode))
				{
					bool bIsLodGroup = false;
					SetUidLabelFromSceneNode(SceneNode, bIsLodGroup);

					if (!CommonMeshesProperties->bBakeMeshes && !bIsLodGroup)
					{
						//We recursively check the AssetInstanceUid's chain to make sure we find LOD Group or MeshUID.
						FString AssetInstanceUid;
						while (SceneNode && SceneNode->GetCustomAssetInstanceUid(AssetInstanceUid))
						{
							const UInterchangeBaseNode* AssetInstanceNode = BaseNodeContainer->GetNode(AssetInstanceUid);
							SceneNode = Cast<UInterchangeSceneNode>(AssetInstanceNode); //if its a sceneNode without a LOD group specialization we continue the search
							if (SceneNode && SceneNode->IsSpecializedTypeContains(UE::Interchange::FSceneNodeStaticData::GetLodGroupSpecializeTypeString()))
							{
								//if it is a LOD Group specialization then we use the original NewNodeUid. (as then we want the 'instantiation' of the LOD Group)
								NewNodeUid = TEXT("\\MeshLODGroupInstance\\") + Uid;
								
								return true;
							}
							if (const UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(AssetInstanceNode))
							{
								//if it is a Mesh Node then we confirmed that it is an instanced Mesh
								// but since we are not baking meshes we want the MeshNode
								//It cannot be part of a LOD group as that would have broken out sooner due to the LodGroup check.
								//For this case we are using MeshNode's UID and DisplayName.
								SetUidLabel(AssetInstanceNode);

								return true;
							}
						}
					}
					else
					{
						return true;
					}
				}

				if (const UInterchangeInstancedStaticMeshComponentNode* ISMComponentNode = Cast<const UInterchangeInstancedStaticMeshComponentNode>(BaseNode))
				{
					FString RefMeshUid;
					if (ISMComponentNode->GetCustomInstancedAssetUid(RefMeshUid))
					{
						const UInterchangeBaseNode* MeshNode = BaseNodeContainer->GetNode(RefMeshUid);
						if (MeshNode)
						{
							SetUidLabel(MeshNode);

							return true;
						}
					}
				}
			}

			// We found the lod but there is no valid Mesh node to return the Uid
			break;
		}
	}

	return false;
}

UInterchangeStaticMeshFactoryNode* UInterchangeGenericMeshPipeline::CreateStaticMeshFactoryNode(const TMap<int32, TArray<FString>>& MeshUidsPerLodIndex, const FString& BaseMeshUid)
{
	check(CommonMeshesProperties.IsValid());
	if (MeshUidsPerLodIndex.Num() == 0)
	{
		return nullptr;
	}

	// Create the static mesh factory node, name it according to the first mesh node compositing the meshes
	FString StaticMeshUid_MeshNamePart;
	FString DisplayLabel;
	const int32 BaseLodIndex = 0;
	if (!MakeMeshFactoryNodeUidAndDisplayLabel(MeshUidsPerLodIndex, BaseLodIndex, StaticMeshUid_MeshNamePart, DisplayLabel, BaseMeshUid))
	{
		// Log an error
		return nullptr;
	}

	// #interchange_lod_rework: if rework will be calling out LOD Groups directly this won't be needed.
	auto CheckAndStoreBaseMeshUidForLODGroup = [this, BaseMeshUid](UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode)
		{
			if (const UInterchangeSceneNode* BaseMeshSceneNode = Cast<UInterchangeSceneNode>(BaseNodeContainer->GetNode(BaseMeshUid)))
			{
				if (BaseMeshSceneNode->IsSpecializedTypeContains(UE::Interchange::FSceneNodeStaticData::GetLodGroupSpecializeTypeString()))
				{
					StaticMeshFactoryNode->RegisterAttribute<FString>(UE::Interchange::FAttributeKey(FString(TEXT("LODGroupSceneNodeUid"))), BaseMeshUid);
				}
				else
				{
					bool bLODGroup_WithoutLODSpecialization = false;
					if (BaseMeshSceneNode->GetAttribute<bool>(TEXT("LODGroup_WithoutLODSpecialization"), bLODGroup_WithoutLODSpecialization))
					{
						if (bLODGroup_WithoutLODSpecialization)
						{
							StaticMeshFactoryNode->RegisterAttribute<FString>(UE::Interchange::FAttributeKey(FString(TEXT("LODGroupSceneNodeUid"))), BaseMeshUid);
						}
					}
				}
			}
		};

	const FString StaticMeshUid = UInterchangeFactoryBaseNode::BuildFactoryNodeUid(StaticMeshUid_MeshNamePart);

	UInterchangeFactoryBaseNode* FactoryBaseNode = BaseNodeContainer->GetFactoryNode(StaticMeshUid);
	UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode = Cast<UInterchangeStaticMeshFactoryNode>(FactoryBaseNode);

	if (StaticMeshFactoryNode)
	{
		CheckAndStoreBaseMeshUidForLODGroup(StaticMeshFactoryNode);
		return StaticMeshFactoryNode;
	}

	StaticMeshFactoryNode = NewObject<UInterchangeStaticMeshFactoryNode>(BaseNodeContainer, NAME_None);
	if (!ensure(StaticMeshFactoryNode))
	{
		return nullptr;
	}

	StaticMeshFactoryNode->InitializeStaticMeshNode(StaticMeshUid, DisplayLabel, UStaticMesh::StaticClass()->GetName(), BaseNodeContainer);
	CheckAndStoreBaseMeshUidForLODGroup(StaticMeshFactoryNode);

	//Set the pipeline import sockets property
	StaticMeshFactoryNode->SetCustomImportSockets(CommonMeshesProperties->bImportSockets);

	if (CommonMeshesProperties->bKeepSectionsSeparate)
	{
		StaticMeshFactoryNode->SetCustomKeepSectionsSeparate(CommonMeshesProperties->bKeepSectionsSeparate);
	}

	AddLodDataToStaticMesh(StaticMeshFactoryNode, MeshUidsPerLodIndex);

	switch (CommonMeshesProperties->VertexColorImportOption)
	{
		case EInterchangeVertexColorImportOption::IVCIO_Replace:
		{
			StaticMeshFactoryNode->SetCustomVertexColorReplace(true);
		}
		break;
		case EInterchangeVertexColorImportOption::IVCIO_Ignore:
		{
			StaticMeshFactoryNode->SetCustomVertexColorIgnore(true);
		}
		break;
		case EInterchangeVertexColorImportOption::IVCIO_Override:
		{
			StaticMeshFactoryNode->SetCustomVertexColorOverride(CommonMeshesProperties->VertexOverrideColor);
		}
		break;
	}

	StaticMeshFactoryNode->SetCustomLODGroup(LodGroup);

	//Common meshes build options
	StaticMeshFactoryNode->SetCustomRecomputeNormals(CommonMeshesProperties->bRecomputeNormals);
	StaticMeshFactoryNode->SetCustomRecomputeTangents(CommonMeshesProperties->bRecomputeTangents);
	StaticMeshFactoryNode->SetCustomUseMikkTSpace(CommonMeshesProperties->bUseMikkTSpace);
	StaticMeshFactoryNode->SetCustomComputeWeightedNormals(CommonMeshesProperties->bComputeWeightedNormals);
	StaticMeshFactoryNode->SetCustomUseHighPrecisionTangentBasis(CommonMeshesProperties->bUseHighPrecisionTangentBasis);
	StaticMeshFactoryNode->SetCustomUseFullPrecisionUVs(CommonMeshesProperties->bUseFullPrecisionUVs);
	StaticMeshFactoryNode->SetCustomUseBackwardsCompatibleF16TruncUVs(CommonMeshesProperties->bUseBackwardsCompatibleF16TruncUVs);
	StaticMeshFactoryNode->SetCustomRemoveDegenerates(CommonMeshesProperties->bRemoveDegenerates);

	//Static meshes build options
	StaticMeshFactoryNode->SetCustomBuildReversedIndexBuffer(bBuildReversedIndexBuffer);
	StaticMeshFactoryNode->SetCustomGenerateLightmapUVs(bGenerateLightmapUVs);
	StaticMeshFactoryNode->SetCustomGenerateDistanceFieldAsIfTwoSided(bGenerateDistanceFieldAsIfTwoSided);
	StaticMeshFactoryNode->SetCustomSupportFaceRemap(bSupportFaceRemap);
	StaticMeshFactoryNode->SetCustomMinLightmapResolution(MinLightmapResolution);
	StaticMeshFactoryNode->SetCustomSrcLightmapIndex(SrcLightmapIndex);
	StaticMeshFactoryNode->SetCustomDstLightmapIndex(DstLightmapIndex);
	StaticMeshFactoryNode->SetCustomBuildScale3D(BuildScale3D);
	StaticMeshFactoryNode->SetCustomDistanceFieldResolutionScale(DistanceFieldResolutionScale);
	StaticMeshFactoryNode->SetCustomDistanceFieldReplacementMesh(DistanceFieldReplacementMesh.Get());
	StaticMeshFactoryNode->SetCustomMaxLumenMeshCards(MaxLumenMeshCards);
	StaticMeshFactoryNode->SetCustomBuildNanite(bBuildNanite);
	StaticMeshFactoryNode->SetCustomAutoComputeLODScreenSizes(bAutoComputeLODScreenSizes);
	StaticMeshFactoryNode->SetLODScreenSizes(LODScreenSizes);

	return StaticMeshFactoryNode;
}


UInterchangeStaticMeshLodDataNode* UInterchangeGenericMeshPipeline::CreateStaticMeshLodDataNode(const FString& NodeName, const FString& NodeUniqueID)
{
	FString DisplayLabel(NodeName);
	FString NodeUID(NodeUniqueID);
	UInterchangeStaticMeshLodDataNode* StaticMeshLodDataNode = NewObject<UInterchangeStaticMeshLodDataNode>(BaseNodeContainer, NAME_None);
	if (!ensure(StaticMeshLodDataNode))
	{
		// @TODO: log error
		return nullptr;
	}

	BaseNodeContainer->SetupNode(StaticMeshLodDataNode, NodeUID, DisplayLabel, EInterchangeNodeContainerType::FactoryData);

	StaticMeshLodDataNode->SetOneConvexHullPerUCX(bOneConvexHullPerUCX);
	StaticMeshLodDataNode->SetImportCollision(bCollision);
	StaticMeshLodDataNode->SetImportCollisionType(Collision);
	StaticMeshLodDataNode->SetForceCollisionPrimitiveGeneration(bForceCollisionPrimitiveGeneration);

	return StaticMeshLodDataNode;
}


void UInterchangeGenericMeshPipeline::AddLodDataToStaticMesh(UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode, const TMap<int32, TArray<FString>>& NodeUidsPerLodIndex)
{
	check(CommonMeshesProperties.IsValid());
	const FString StaticMeshFactoryUid = StaticMeshFactoryNode->GetUniqueID();

	int32 MaxLodIndex = 0;
	for (const TPair<int32, TArray<FString>>& LodIndexAndNodeUids : NodeUidsPerLodIndex)
	{
		MaxLodIndex = FMath::Max(MaxLodIndex, LodIndexAndNodeUids.Key);
	}

	TArray<FString> EmptyLodData;
	const int32 LodCount = MaxLodIndex+1;
	for(int32 LodIndex = 0; LodIndex < LodCount; ++LodIndex)
	{
		if (!CommonMeshesProperties->bImportLods && LodIndex > 0)
		{
			// If the pipeline should not import lods, skip any lod over base lod
			continue;
		}

		const TArray<FString>& NodeUids = NodeUidsPerLodIndex.Contains(LodIndex) ? NodeUidsPerLodIndex.FindChecked(LodIndex) : EmptyLodData;
		// Create a lod data node with all the meshes for this LOD
		const FString StaticMeshLodDataName = TEXT("LodData") + FString::FromInt(LodIndex);
		const FString LODDataPrefix = TEXT("\\LodData") + (LodIndex > 0 ? FString::FromInt(LodIndex) : TEXT(""));
		const FString StaticMeshLodDataUniqueID = LODDataPrefix + StaticMeshFactoryUid;

		// Create LodData node if it doesn't already exist
		UInterchangeStaticMeshLodDataNode* LodDataNode = Cast<UInterchangeStaticMeshLodDataNode>(BaseNodeContainer->GetFactoryNode(StaticMeshLodDataUniqueID));
		if (!LodDataNode)
		{
			// Add the data for the LOD (all the mesh node fbx path, so we can find them when we will create the payload data)
			LodDataNode = CreateStaticMeshLodDataNode(StaticMeshLodDataName, StaticMeshLodDataUniqueID);
			BaseNodeContainer->SetNodeParentUid(StaticMeshLodDataUniqueID, StaticMeshFactoryUid);
			StaticMeshFactoryNode->AddLodDataUniqueId(StaticMeshLodDataUniqueID);
		}
		TMap<FString, FString> ExistingLodSlotMaterialDependencies;
		constexpr bool bAddSourceNodeName = true;
		for (const FString& NodeUid : NodeUids)
		{
			TMap<FString, FString> SlotMaterialDependencies;
			if (const UInterchangeSceneNode* SceneNode = Cast<UInterchangeSceneNode>(BaseNodeContainer->GetNode(NodeUid)))
			{
				FString MeshDependency;
				SceneNode->GetCustomAssetInstanceUid(MeshDependency);
				if (BaseNodeContainer->IsNodeUidValid(MeshDependency))
				{
					const UInterchangeMeshNode* MeshDependencyNode = Cast<UInterchangeMeshNode>(BaseNodeContainer->GetNode(MeshDependency));
					UInterchangeUserDefinedAttributesAPI::DuplicateAllUserDefinedAttribute(MeshDependencyNode, StaticMeshFactoryNode, bAddSourceNodeName);
					StaticMeshFactoryNode->AddTargetNodeUid(MeshDependency);
					StaticMeshFactoryNode->AddSocketUids(PipelineMeshesUtilities->GetMeshGeometryByUid(MeshDependency).AttachedSocketUids);
					MeshDependencyNode->AddTargetNodeUid(StaticMeshFactoryNode->GetUniqueID());

					MeshDependencyNode->GetSlotMaterialDependencies(SlotMaterialDependencies);
				}
				else
				{
					SceneNode->GetSlotMaterialDependencies(SlotMaterialDependencies);
				}

				UInterchangeUserDefinedAttributesAPI::DuplicateAllUserDefinedAttribute(SceneNode, StaticMeshFactoryNode, bAddSourceNodeName);
			}
			else if (const UInterchangeMeshNode* MeshNode = Cast<UInterchangeMeshNode>(BaseNodeContainer->GetNode(NodeUid)))
			{
				UInterchangeUserDefinedAttributesAPI::DuplicateAllUserDefinedAttribute(MeshNode, StaticMeshFactoryNode, bAddSourceNodeName);
				StaticMeshFactoryNode->AddTargetNodeUid(NodeUid);
				StaticMeshFactoryNode->AddSocketUids(PipelineMeshesUtilities->GetMeshGeometryByUid(NodeUid).AttachedSocketUids);
				MeshNode->AddTargetNodeUid(StaticMeshFactoryNode->GetUniqueID());

				MeshNode->GetSlotMaterialDependencies(SlotMaterialDependencies);
			}
			else if (const UInterchangeInstancedStaticMeshComponentNode* ISMComponentNode = Cast<UInterchangeInstancedStaticMeshComponentNode>(BaseNodeContainer->GetNode(NodeUid)))
			{
				FString MeshDependency;
				ISMComponentNode->GetCustomInstancedAssetUid(MeshDependency);
				if (BaseNodeContainer->IsNodeUidValid(MeshDependency))
				{
					const UInterchangeMeshNode* MeshDependencyNode = Cast<UInterchangeMeshNode>(BaseNodeContainer->GetNode(MeshDependency));
					UInterchangeUserDefinedAttributesAPI::DuplicateAllUserDefinedAttribute(MeshDependencyNode, StaticMeshFactoryNode, bAddSourceNodeName);
					StaticMeshFactoryNode->AddTargetNodeUid(MeshDependency);
					StaticMeshFactoryNode->AddSocketUids(PipelineMeshesUtilities->GetMeshGeometryByUid(MeshDependency).AttachedSocketUids);
					MeshDependencyNode->AddTargetNodeUid(StaticMeshFactoryNode->GetUniqueID());

					MeshDependencyNode->GetSlotMaterialDependencies(SlotMaterialDependencies);
				}

				UInterchangeUserDefinedAttributesAPI::DuplicateAllUserDefinedAttribute(ISMComponentNode, StaticMeshFactoryNode, bAddSourceNodeName);
			}

			UE::Interchange::MeshesUtilities::ApplySlotMaterialDependencies(*StaticMeshFactoryNode, SlotMaterialDependencies, *BaseNodeContainer, &ExistingLodSlotMaterialDependencies);

			const FString MeshName = UE::Interchange::Private::GetNodeName(PipelineMeshesUtilities, *BaseNodeContainer, NodeUid);
			// Ignore the "_" so we can detect potential typos done by the user.
			const bool bHasCollisionPrefix = MeshName.StartsWith(TEXT("UBX")) || MeshName.StartsWith(TEXT("UCX")) || MeshName.StartsWith(TEXT("USP")) || MeshName.StartsWith(TEXT("UCP")) || MeshName.StartsWith(TEXT("MCDCX"));

			auto LogWarning = [this, &StaticMeshFactoryNode](const FText& Text)
				{
					UInterchangeResultWarning_Generic* Message = AddMessage<UInterchangeResultWarning_Generic>();
					Message->SourceAssetName = SourceDatas[0]->GetFilename();
					Message->AssetFriendlyName = StaticMeshFactoryNode->GetAssetName();
					Message->AssetType = StaticMeshFactoryNode->GetObjectClass();
					Message->Text = Text;
				};

			if (bImportCollisionAccordingToMeshName)
			{
				TTuple<EInterchangeMeshCollision, FString> MeshType = GetCollisionMeshType(PipelineMeshesUtilities, *BaseNodeContainer, NodeUid, NodeUids, bImportCollisionAccordingToMeshName);
				EInterchangeMeshCollision CollisionType = MeshType.Key;
				const FString& RenderMeshUid = MeshType.Value;
				const FString& ColliderMeshUid = NodeUid;

				switch (CollisionType)
				{
				case EInterchangeMeshCollision::None:
					LodDataNode->AddMeshUid(NodeUid);

					// If the user is attempting to import a collision mesh using a naming convention but the mesh doesn't match, warn them that it won't be imported as collision.
					if (bHasCollisionPrefix)
					{
						LogWarning(FText::Format(NSLOCTEXT("UInterchangeGenericStaticMeshPipeline", "NoMatchingRenderMeshForCustomCollision",
							"No render mesh found for custom collision '{0}'. The name must follow the pattern 'UCX_[ExactRenderMeshName]', and a matching render mesh with that name must exist in the source file."),
							FText::FromString(MeshName)));
					}

					break;

				case EInterchangeMeshCollision::Box:
					LodDataNode->AddBoxCollisionMeshUids(ColliderMeshUid, RenderMeshUid);
					break;

				case EInterchangeMeshCollision::Sphere:
					LodDataNode->AddSphereCollisionMeshUids(ColliderMeshUid, RenderMeshUid);
					break;

				case EInterchangeMeshCollision::Capsule:
					LodDataNode->AddCapsuleCollisionMeshUids(ColliderMeshUid, RenderMeshUid);
					break;

				case EInterchangeMeshCollision::Convex10DOP_X:
				case EInterchangeMeshCollision::Convex10DOP_Y:
				case EInterchangeMeshCollision::Convex10DOP_Z:
				case EInterchangeMeshCollision::Convex18DOP:
				case EInterchangeMeshCollision::Convex26DOP:
					LodDataNode->AddConvexCollisionMeshUids(ColliderMeshUid, RenderMeshUid);
					break;
				}

				// If the Mesh is both render AND collision mesh let's also add it as a render mesh,
				// as the above switch will have only added the collision mesh to the LodDataNode
				if (RenderMeshUid == NodeUid && CollisionType != EInterchangeMeshCollision::None)
				{
					LodDataNode->AddMeshUid(NodeUid);
				}
			}
			else
			{
				LodDataNode->AddMeshUid(NodeUid);

				// Warn the user that this mesh appears to be a custom collision mesh but that the pipeline is not set to import it as such
				if (bHasCollisionPrefix)
				{
					LogWarning(FText::Format(NSLOCTEXT("UInterchangeGenericStaticMeshPipeline", "ImportCollisionAccordingToMeshNameDisabled",
						"The mesh '{0}' appears to use custom collision naming pattern, but the setting 'Import Collisions According to Mesh Name' is disabled. It will be treated as a render mesh."),
						FText::FromString(MeshName)));
				}
			}
		}
		UE::Interchange::MeshesUtilities::ReorderSlotMaterialDependencies(*StaticMeshFactoryNode, *BaseNodeContainer);
	}
}

// #interchange_lod_rework
void UInterchangeGenericMeshPipeline::FindAndProcessIdenticalStaticMeshLODGroups(const UInterchangeBaseNodeContainer* InBaseNodeContainer)
{
	if (!CommonMeshesProperties->bBakeMeshes)
	{
		auto GetGlobalTransform = [&InBaseNodeContainer](
			const FString& InMeshOrSceneNodeUid,
			FTransform& OutGlobalTransform,
			bool& bMeshNode,
			FString& LODMeshAssetUid
			)
			{
				bMeshNode = true;
				const UInterchangeBaseNode* Node = InBaseNodeContainer->GetNode(InMeshOrSceneNodeUid);
				const UInterchangeMeshNode* MeshNode = Cast<const UInterchangeMeshNode>(Node);
				if (MeshNode == nullptr)
				{
					bMeshNode = false;
					// MeshUid must refer to a scene node
					const UInterchangeSceneNode* SceneNode = Cast<const UInterchangeSceneNode>(Node);
					if (!ensure(SceneNode))
					{
						return;
					}

					// Get the transform from the scene node
					FTransform SceneNodeGlobalTransform;
					if (SceneNode->GetCustomGlobalTransform(InBaseNodeContainer, FTransform::Identity, SceneNodeGlobalTransform))
					{
						OutGlobalTransform = SceneNodeGlobalTransform;
					}
					UE::Interchange::Private::MeshHelper::AddSceneNodeGeometricAndPivotToGlobalTransform(OutGlobalTransform, SceneNode, true /*bakeMeshes*/, true /*bakePivotMeshes*/);
					// And get the mesh node which it references
					FString MeshDependencyUid;
					SceneNode->GetCustomAssetInstanceUid(MeshDependencyUid);
					LODMeshAssetUid = MeshDependencyUid;
				}
				else
				{
					LODMeshAssetUid = InMeshOrSceneNodeUid;
				}
			};

		//Iterate through all UInterchangeStaticMeshFactoryNode and build a helper struct that will help us identify identical LOD Group Static Meshes.
		struct FLODGroupIIHelper //Identical Identifier
		{
			TMap<FString, FTransform> Meshes;
			UInterchangeStaticMeshFactoryNode* LODGroupStaticMeshFactoryNode;

			TArray<UInterchangeStaticMeshFactoryNode*> DisabledIdenticals;

			bool DataEquals(const FLODGroupIIHelper& LODGroupIIHelper)
			{
				if (Meshes.Num() != LODGroupIIHelper.Meshes.Num())
				{
					return false;
				}

				for (const TPair<FString, FTransform>& Entry : Meshes)
				{
					if (!LODGroupIIHelper.Meshes.Contains(Entry.Key))
					{
						return false;
					}
					if (!LODGroupIIHelper.Meshes[Entry.Key].Equals(Entry.Value))
					{
						return false;
					}
				}

				return true;
			}
		};
		TArray<FLODGroupIIHelper> LODGroupIdenticalIdentifier;
		auto ProcessEntryCandidate = [&LODGroupIdenticalIdentifier](FLODGroupIIHelper& ToCompare)
			{
				for (FLODGroupIIHelper& LODGroupIIHelper : LODGroupIdenticalIdentifier)
				{
					//TODO/Note: we should compare all attributes of the UInterchangeStaticMeshFactoryNode to make sure that regardless of Mesh and Transforms they are identical and can be subsituted.
					//For now however we are only checking Mesh and Transform.
					//For now this is fine as the rest of the UInterchangeStaticMeshFactoryNode attributes are set from the Pipeline and Utilities properties which are shared across.
					if (LODGroupIIHelper.DataEquals(ToCompare))
					{
						LODGroupIIHelper.DisabledIdenticals.Add(ToCompare.LODGroupStaticMeshFactoryNode);
						ToCompare.LODGroupStaticMeshFactoryNode->SetEnabled(false);
						ToCompare.LODGroupStaticMeshFactoryNode->SetCustomSubstituteUID(LODGroupIIHelper.LODGroupStaticMeshFactoryNode->GetUniqueID());
						return;
					}
				}
				LODGroupIdenticalIdentifier.Add(ToCompare);
			};

		InBaseNodeContainer->IterateNodesOfType<UInterchangeStaticMeshFactoryNode>(
			[&InBaseNodeContainer, &GetGlobalTransform, &LODGroupIdenticalIdentifier, &ProcessEntryCandidate](const FString& NodeUid, UInterchangeStaticMeshFactoryNode* StaticMeshFactoryNode)
			{
				FString LODGroupSceneUid;
				if (StaticMeshFactoryNode->GetAttribute<FString>(TEXT("LODGroupSceneNodeUid"), LODGroupSceneUid))
				{
					const UInterchangeSceneNode* LODGroupSceneNode = Cast<UInterchangeSceneNode>(InBaseNodeContainer->GetNode(LODGroupSceneUid));
					if (!LODGroupSceneNode)
					{
						return;
					}

					FLODGroupIIHelper LODGroupIIHelper;
					LODGroupIIHelper.LODGroupStaticMeshFactoryNode = StaticMeshFactoryNode;

					//Get LOD Group's Global Transform:
					FTransform LODGroupGlobalTransform;
					bool bLODGroupMeshNode; //Dummy
					FString LODMeshAssetUid; //Dummy
					GetGlobalTransform(LODGroupSceneUid, LODGroupGlobalTransform, bLODGroupMeshNode, LODMeshAssetUid);
					FTransform LODGroupGlobalTransformInverse = LODGroupGlobalTransform.Inverse();

					TArray<FString> LodDataUniqueIds;
					StaticMeshFactoryNode->GetLodDataUniqueIds(LodDataUniqueIds);

					for (size_t LODIndex = 0; LODIndex < LodDataUniqueIds.Num(); LODIndex++)
					{
						const FString& LodUniqueId = LodDataUniqueIds[LODIndex];
						const FString LODIndexString = FString::FromInt(LODIndex);
						const UInterchangeStaticMeshLodDataNode* LodDataNode = Cast<UInterchangeStaticMeshLodDataNode>(InBaseNodeContainer->GetNode(LodUniqueId));
						if (!ensure(LodDataNode))
						{
							continue;
						}

						TArray<FString> MeshUids;
						LodDataNode->GetMeshUids(MeshUids);

						for (const FString& MeshUid : MeshUids)
						{
							FTransform MeshGlobalTransform;
							bool bMeshNode;
							FString MeshAssetUid;
							GetGlobalTransform(MeshUid, MeshGlobalTransform, bMeshNode, MeshAssetUid);

							if (!MeshAssetUid.IsEmpty())
							{
								if (bMeshNode)
								{
									LODGroupIIHelper.Meshes.Add(LODIndexString + TEXT("_") + MeshAssetUid, FTransform::Identity);
								}
								else
								{
									//Note: Need the relative transform from the LODGroup not from the root for equal check purposes.
									LODGroupIIHelper.Meshes.Add(LODIndexString + TEXT("_") + MeshAssetUid, LODGroupGlobalTransformInverse * MeshGlobalTransform);
								}
							}
						}
					}

					ProcessEntryCandidate(LODGroupIIHelper);
				}
			});
	}
}
