// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeGenericGroomPipeline.h"

#include "GroomAsset.h"
#include "GroomCache.h"
#include "InterchangeGroomNode.h"
#include "InterchangeGroomCacheFactoryNode.h"
#include "InterchangeGroomFactoryNode.h"
#include "InterchangePipelineHelper.h"
#include "Nodes/InterchangeFactoryBaseNode.h"
#include "Nodes/InterchangeSourceNode.h"
#include "Nodes/InterchangeUserDefinedAttribute.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeGenericGroomPipeline)

namespace UE::InterchangeGroomPipeline::Private
{
	UInterchangeFactoryBaseNode* CreateGroomFactoryNode(UInterchangeBaseNodeContainer* InBaseNodeContainer, const UInterchangeGroomNode* GroomNode, EInterchangeGroomCacheImportType Type)
	{
		FString NodeID = GroomNode->GetUniqueID();
		FString FactoryNodeUid;

		UInterchangeFactoryBaseNode* FactoryNode = nullptr;
		if (Type == EInterchangeGroomCacheImportType::None)
		{
			// Create groom asset node
			FactoryNodeUid = UInterchangeFactoryBaseNode::BuildFactoryNodeUid(NodeID);
			FactoryNode = NewObject<UInterchangeGroomFactoryNode>(InBaseNodeContainer);
		}
		else
		{
			// Create groom cache node
			const FString CacheType = Type == EInterchangeGroomCacheImportType::Strands ? TEXT("_strands_") : TEXT("_guides_");
			static const FString Suffix(TEXT("cache"));

			FactoryNodeUid = UInterchangeFactoryBaseNode::BuildFactoryNodeUid(NodeID + CacheType + Suffix);
			UInterchangeGroomCacheFactoryNode* GroomCacheFactoryNode = NewObject<UInterchangeGroomCacheFactoryNode>(InBaseNodeContainer);

			GroomCacheFactoryNode->SetCustomGroomCacheImportType(Type);
			GroomCacheFactoryNode->SetAssetName(GroomNode->GetAssetName() + CacheType + Suffix);

			int32 Value = 0;
			if (GroomNode->GetCustomNumFrames(Value))
			{
				GroomCacheFactoryNode->SetCustomNumFrames(Value);
			}

			if (GroomNode->GetCustomStartFrame(Value))
			{
				GroomCacheFactoryNode->SetCustomStartFrame(Value);
			}

			if (GroomNode->GetCustomEndFrame(Value))
			{
				GroomCacheFactoryNode->SetCustomEndFrame(Value);
			}

			double SecsPerFrames = 0;
			if (GroomNode->GetCustomFrameRate(SecsPerFrames))
			{
				GroomCacheFactoryNode->SetCustomFrameRate(SecsPerFrames);
			}

			EInterchangeGroomCacheAttributes Attributes;
			if (GroomNode->GetCustomGroomCacheAttributes(Attributes))
			{
				GroomCacheFactoryNode->SetCustomGroomCacheAttributes(Attributes);
			}

			FactoryNode = GroomCacheFactoryNode;
		}

		InBaseNodeContainer->SetupNode(FactoryNode, FactoryNodeUid, GroomNode->GetDisplayLabel(), EInterchangeNodeContainerType::FactoryData);

		UInterchangeSourceNode* SourceNode = UInterchangeSourceNode::FindOrCreateUniqueInstance(InBaseNodeContainer);
		UE::Interchange::PipelineHelper::FillSubPathFromSourceNode(FactoryNode, SourceNode);

		const bool bAddSourceNodeName = false;
		UInterchangeUserDefinedAttributesAPI::DuplicateAllUserDefinedAttribute(GroomNode, FactoryNode, bAddSourceNodeName);

		FactoryNode->AddTargetNodeUid(GroomNode->GetUniqueID());
		GroomNode->AddTargetNodeUid(FactoryNode->GetUniqueID());

		return FactoryNode;
	}
}

FString UInterchangeGenericGroomPipeline::GetPipelineCategory(UClass* AssetClass)
{
	return TEXT("Grooms");
}

void UInterchangeGenericGroomPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* InBaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
	using namespace UE::InterchangeGroomPipeline::Private;

	if (!bEnableGroomTypesImport)
	{
		return;
	}

	if (!InBaseNodeContainer)
	{
		return;
	}

	// Find all the translated groom nodes we need for this pipeline
	TArray<UInterchangeGroomNode*> GroomNodes;
	if (bImportGrooms || bImportGroomCaches)
	{
		InBaseNodeContainer->IterateNodes(
			[&GroomNodes](const FString& NodeUid, UInterchangeBaseNode* Node)
			{
				if (UInterchangeGroomNode* GroomNode = Cast<UInterchangeGroomNode>(Node))
				{
					GroomNodes.Add(GroomNode);
				}
			}
		);

		// Create groom factory nodes
		for (const UInterchangeGroomNode* GroomNode : GroomNodes)
		{
			const TOptional<FInterchangeGroomPayloadKey> PayloadKey = GroomNode->GetPayloadKey();
			if (!PayloadKey.IsSet())
			{
				continue;
			}

			if (bImportGrooms && PayloadKey->Type == EInterchangeGroomPayLoadType::STATIC)
			{
				UInterchangeGroomFactoryNode* GroomFactoryNode = Cast<UInterchangeGroomFactoryNode>(CreateGroomFactoryNode(InBaseNodeContainer, GroomNode, EInterchangeGroomCacheImportType::None));
				if (GroomFactoryNode)
				{
					GroomFactoryNode->SetCustomGroupInterpolationSettings(GroupInterpolationSettings);
				}
			}
			else
			{
				UInterchangeGroomFactoryNode* GroomFactoryNode = nullptr;
				if (bImportGrooms)
				{
					// A groom asset can be generated from the first frame of a groom cache
					GroomFactoryNode = Cast<UInterchangeGroomFactoryNode>(CreateGroomFactoryNode(InBaseNodeContainer, GroomNode, EInterchangeGroomCacheImportType::None));
					if (GroomFactoryNode)
					{
						GroomFactoryNode->SetCustomGroupInterpolationSettings(GroupInterpolationSettings);
					}
				}

				if (bImportGroomCaches)
				{
					// To import a groom cache, either a groom asset must be imported from its first frame or a previously imported groom asset must be specified
					// The groom asset is used to validate that the cache is compatible with the asset
					if (!bImportGrooms && !GroomAsset.IsValid())
					{
						continue;
					}

					auto InitializeGroomCacheFactoryNode = [this, InBaseNodeContainer, GroomNode, GroomFactoryNode](EInterchangeGroomCacheImportType CacheType)
					{
						if (EnumHasAnyFlags(ImportGroomCacheType, CacheType))
						{
							UInterchangeGroomCacheFactoryNode* GroomCacheFactoryNode = Cast<UInterchangeGroomCacheFactoryNode>(CreateGroomFactoryNode(InBaseNodeContainer, GroomNode, CacheType));
							if (GroomCacheFactoryNode)
							{
								if (GroomFactoryNode)
								{
									// If the groom asset is imported at the same time, set its factory node as a dependency
									GroomCacheFactoryNode->AddFactoryDependencyUid(GroomFactoryNode->GetUniqueID());
									if (bIsReimportContext && Cast<UGroomCache>(CacheContextParam.ReimportAsset))
									{
										// When reimporting a groom cache, the associated groom asset will not be reimported
										// but it's still needed as a dependency. Disabling its factory node will try to
										// fetch it instead of importing it
										GroomFactoryNode->SetEnabled(false);
									}
								}
								else
								{
									// Not importing the groom asset, so set the path to a previously imported groom asset
									GroomCacheFactoryNode->SetCustomGroomAssetPath(GroomAsset);
								}

								if (bOverrideTimeRange)
								{
									GroomCacheFactoryNode->SetCustomStartFrame(FrameStart);
									// Make sure the end is after the start and it's at least 2 frames
									FrameEnd = FMath::Max(FrameEnd, FrameStart + 1);
									GroomCacheFactoryNode->SetCustomEndFrame(FrameEnd);
									GroomCacheFactoryNode->SetCustomNumFrames(FrameEnd - FrameStart + 1);
								}
							}
						}
					};

					InitializeGroomCacheFactoryNode(EInterchangeGroomCacheImportType::Strands);
					InitializeGroomCacheFactoryNode(EInterchangeGroomCacheImportType::Guides);
				}
			}
		}
	}
}

#if WITH_EDITOR

void UInterchangeGenericGroomPipeline::GetSupportAssetClasses(TArray<UClass*>& PipelineSupportAssetClasses) const
{
	PipelineSupportAssetClasses.Add(UGroomAsset::StaticClass());
	PipelineSupportAssetClasses.Add(UGroomCache::StaticClass());
}

void UInterchangeGenericGroomPipeline::FilterPropertiesFromTranslatedData(UInterchangeBaseNodeContainer* InBaseNodeContainer)
{
	int32 NumGroomNodes = 0;
	int32 NumGroomCaches = 0;
	InBaseNodeContainer->BreakableIterateNodesOfType<UInterchangeGroomNode>([&NumGroomNodes, &NumGroomCaches](const FString& NodeUid, UInterchangeGroomNode* GroomNode)
	{
		++NumGroomNodes;
		const TOptional<FInterchangeGroomPayloadKey> PayloadKey = GroomNode->GetPayloadKey();
		if (PayloadKey.IsSet() && PayloadKey->Type == EInterchangeGroomPayLoadType::ANIMATED)
		{
			++NumGroomCaches;
			return true;
		}
		return false;
	});

	UInterchangePipelineBase* OuterMostPipeline = GetMostPipelineOuter();
	if (NumGroomNodes == 0)
	{
		HidePropertiesOfCategory(OuterMostPipeline, this, GetPipelineCategory(nullptr));
	}
	else if (NumGroomCaches == 0)
	{
		HidePropertiesOfSubCategory(OuterMostPipeline, this, TEXT("Caches"));
	}
}

bool UInterchangeGenericGroomPipeline::IsPropertyChangeNeedRefresh(const FPropertyChangedEvent& PropertyChangedEvent) const
{
	static const TSet<FName> NeedRefreshProperties =
	{
		GET_MEMBER_NAME_CHECKED(UInterchangeGenericGroomPipeline, bEnableGroomTypesImport),
		GET_MEMBER_NAME_CHECKED(UInterchangeGenericGroomPipeline, bImportGroomCaches),
		GET_MEMBER_NAME_CHECKED(UInterchangeGenericGroomPipeline, bImportGrooms),
		GET_MEMBER_NAME_CHECKED(UInterchangeGenericGroomPipeline, GroomAsset),
	};

	if (NeedRefreshProperties.Contains(PropertyChangedEvent.GetPropertyName()))
	{
		return true;
	}

	return Super::IsPropertyChangeNeedRefresh(PropertyChangedEvent);
}

#endif //WITH_EDITOR

bool UInterchangeGenericGroomPipeline::IsSettingsAreValid(TOptional<FText>& OutInvalidReason) const
{
	if (bEnableGroomTypesImport && bImportGroomCaches && !bImportGrooms && !GroomAsset.IsValid())
	{
		int32 NumGroomCaches = 0;
		CacheContextParam.BaseNodeContainer->BreakableIterateNodesOfType<UInterchangeGroomNode>([&NumGroomCaches](const FString& NodeUid, UInterchangeGroomNode* GroomNode)
		{
			const TOptional<FInterchangeGroomPayloadKey> PayloadKey = GroomNode->GetPayloadKey();
			if (PayloadKey.IsSet() && PayloadKey->Type == EInterchangeGroomPayLoadType::ANIMATED)
			{
				++NumGroomCaches;
				return true;
			}
			return false;
		});

		// If there's no groom cache, then this validation is not needed
		if (NumGroomCaches == 0)
		{
			return Super::IsSettingsAreValid(OutInvalidReason);
		}

		OutInvalidReason = NSLOCTEXT("UInterchangeGenericGroomPipeline", "GroomAssetMustBeSpecified", "When importing a groom cache without importing its associated groom asset, a previously imported and compatible groom asset must be specified.");
		return false;
	}
	return Super::IsSettingsAreValid(OutInvalidReason);
}
