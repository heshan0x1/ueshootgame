// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextAssetWorkspaceAssetUserData.h"

#include "AnimNextExports.h"
#include "UncookedOnlyUtils.h"
#include "Variables/AnimNextSharedVariables.h"
#include "Module/AnimNextModule.h"
#include "Module/AnimNextModule_EditorData.h"
#include "UObject/AssetRegistryTagsContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextAssetWorkspaceAssetUserData)

void UAnimNextAssetWorkspaceAssetUserData::GetAssetRegistryTags(FAssetRegistryTagsContext Context) const
{
	Super::GetAssetRegistryTags(Context);

	// Updated cached export data outside of saving call-path, and simply return cached data when actually saving 
	if (!Context.IsSaving())
	{
		CachedExports.Exports.Reset();
		{
			UAnimNextRigVMAsset* Asset = CastChecked<UAnimNextRigVMAsset>(GetOuter());
			if (const UAnimNextRigVMAssetEditorData* GraphEditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset))
			{
				FWorkspaceOutlinerItemExport& RootAssetExport = CachedExports.Exports.Add_GetRef(FWorkspaceOutlinerItemExport(Asset->GetFName(), Asset));

				if(Asset->IsA<UAnimNextModule>())
				{
					RootAssetExport.GetData().InitializeAsScriptStruct(FAnimNextModuleOutlinerData::StaticStruct());
				}
				else if(Asset->IsA<UAnimNextSharedVariables>())
				{
					RootAssetExport.GetData().InitializeAsScriptStruct(FAnimNextSharedVariablesOutlinerData::StaticStruct());
				}
				else
				{
					RootAssetExport.GetData().InitializeAsScriptStruct(FAnimNextRigVMAssetOutlinerData::StaticStruct());
				}
				FAnimNextRigVMAssetOutlinerData& Data = RootAssetExport.GetData().GetMutable<FAnimNextRigVMAssetOutlinerData>();
				Data.SoftAssetPtr = Asset;

				UE::UAF::UncookedOnly::FUtils::GetAssetWorkspaceExports(GraphEditorData, CachedExports, Context);
			}
		}
	}
	
	FString TagValue;
	FWorkspaceOutlinerItemExports::StaticStruct()->ExportText(TagValue, &CachedExports, nullptr, nullptr, PPF_None, nullptr);
	Context.AddTag(FAssetRegistryTag(UE::Workspace::ExportsWorkspaceItemsRegistryTag, TagValue, FAssetRegistryTag::TT_Hidden));
}

