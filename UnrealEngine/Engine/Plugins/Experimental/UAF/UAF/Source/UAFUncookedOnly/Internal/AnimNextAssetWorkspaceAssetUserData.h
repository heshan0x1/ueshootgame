// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Entries/AnimNextVariableEntry.h"
#include "WorkspaceAssetRegistryInfo.h"
#include "Param/ParamType.h"
#include "IAnimNextRigVMGraphInterface.h"
#include "RigVMModel/Nodes/RigVMFunctionReferenceNode.h"
#include "Module/AnimNextModule.h"
#include "Variables/AnimNextSharedVariables.h"

#include "AnimNextAssetWorkspaceAssetUserData.generated.h"

class UAnimNextRigVMAssetEntry;
class URigVMEdGraphNode;

// Base struct used to identify asset entries
USTRUCT()
struct FAnimNextRigVMAssetOutlinerData : public FWorkspaceOutlinerItemData
{
	GENERATED_BODY()

	FAnimNextRigVMAssetOutlinerData() = default;
	
	UAnimNextRigVMAsset* GetAsset() const
	{
		return SoftAssetPtr.LoadSynchronous();
	}

	UPROPERTY(VisibleAnywhere, Category=AnimNext)
	TSoftObjectPtr<UAnimNextRigVMAsset> SoftAssetPtr;
};

USTRUCT()
struct FAnimNextModuleOutlinerData : public FAnimNextRigVMAssetOutlinerData
{
	GENERATED_BODY()

	FAnimNextModuleOutlinerData() = default;

	UAnimNextModule* GetModule() const
	{
		return Cast<UAnimNextModule>(GetAsset());
	}
};

USTRUCT()
struct FAnimNextSharedVariablesOutlinerData : public FAnimNextRigVMAssetOutlinerData
{
	GENERATED_BODY()

	FAnimNextSharedVariablesOutlinerData() = default;

	UAnimNextSharedVariables* GetSharedVariables() const
	{
		return Cast<UAnimNextSharedVariables>(GetAsset());
	}
};

// Base struct used to identify asset sub-entries
USTRUCT()
struct FAnimNextAssetEntryOutlinerData : public FWorkspaceOutlinerItemData
{
	GENERATED_BODY()
	
	FAnimNextAssetEntryOutlinerData() = default;
	
	UAnimNextRigVMAssetEntry* GetEntry() const
	{
		return SoftEntryPtr.LoadSynchronous();
	}

	UPROPERTY(VisibleAnywhere, Category=AnimNext)
	TSoftObjectPtr<UAnimNextRigVMAssetEntry> SoftEntryPtr;
};

USTRUCT()
struct FAnimNextVariableOutlinerData : public FAnimNextAssetEntryOutlinerData
{
	GENERATED_BODY()
	
	FAnimNextVariableOutlinerData() = default;

	UPROPERTY(VisibleAnywhere, Category=AnimNext)
	FAnimNextParamType Type;
};

USTRUCT()
struct FAnimNextCollapseGraphsOutlinerDataBase : public FWorkspaceOutlinerItemData
{
	GENERATED_BODY()
	
	FAnimNextCollapseGraphsOutlinerDataBase() = default;

	UPROPERTY(VisibleAnywhere, Category=AnimNext)
	TSoftObjectPtr<URigVMEdGraph> SoftEditorObject;

	static bool IsCollapsedGraphBase(const FWorkspaceOutlinerItemExport& InExport)
	{
		return InExport.HasData() && InExport.GetData().GetScriptStruct()->IsChildOf(FAnimNextCollapseGraphsOutlinerDataBase::StaticStruct());
	}
};

USTRUCT()
struct FAnimNextCollapseGraphOutlinerData : public FAnimNextCollapseGraphsOutlinerDataBase
{
	GENERATED_BODY()
	
	FAnimNextCollapseGraphOutlinerData() = default;
};

USTRUCT()
struct FAnimNextGraphFunctionOutlinerData : public FAnimNextCollapseGraphsOutlinerDataBase
{
	GENERATED_BODY()
	
	FAnimNextGraphFunctionOutlinerData() = default;

	UPROPERTY(VisibleAnywhere, Category=AnimNext)
	TSoftObjectPtr<URigVMEdGraphNode> SoftEdGraphNode;
};

USTRUCT()
struct FAnimNextGraphOutlinerData : public FAnimNextAssetEntryOutlinerData
{
	GENERATED_BODY()
	
	FAnimNextGraphOutlinerData() = default;
	
	IAnimNextRigVMGraphInterface* GetGraphInterface() const
	{
		if (UAnimNextRigVMAssetEntry* Entry = GetEntry())
		{
			if (IAnimNextRigVMGraphInterface* GraphInterface = CastChecked<IAnimNextRigVMGraphInterface>(Entry))
			{
				return GraphInterface;
			}
		}

		return nullptr;
	}
};

UCLASS(MinimalAPI)
class UAnimNextAssetWorkspaceAssetUserData : public UAssetUserData
{
public:
	virtual bool IsEditorOnly() const override { return true; }

private:
	GENERATED_BODY()

	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

	mutable FWorkspaceOutlinerItemExports CachedExports;
};
