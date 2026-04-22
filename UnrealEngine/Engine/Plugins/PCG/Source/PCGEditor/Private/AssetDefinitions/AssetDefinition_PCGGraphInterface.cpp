// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetDefinition_PCGGraphInterface.h"

#include "PCGGraph.h"

#include "PCGDefaultExecutionSource.h"
#include "PCGEditorCommon.h"
#include "PCGEditorStyle.h"
#include "PCGGraphFactory.h"
#include "Subsystems/PCGEngineSubsystem.h"

#include "ContentBrowserMenuContexts.h"
#include "Editor.h"
#include "IAssetTools.h"
#include "ToolMenu.h"
#include "ToolMenuEntry.h"
#include "ToolMenuSection.h"
#include "ToolMenus.h"
#include "Misc/DelayedAutoRegister.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AssetDefinition_PCGGraphInterface)

#define LOCTEXT_NAMESPACE "AssetDefinition_PCGGraphInterface"

FText UAssetDefinition_PCGGraphInterface::GetAssetDisplayName() const
{
	return LOCTEXT("DisplayName", "PCG Graph Interface");
}

FLinearColor UAssetDefinition_PCGGraphInterface::GetAssetColor() const
{
	return FColor::Turquoise;
}

TSoftClassPtr<UObject> UAssetDefinition_PCGGraphInterface::GetAssetClass() const
{
	return UPCGGraphInterface::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_PCGGraphInterface::GetAssetCategories() const
{
	static const FAssetCategoryPath Categories[] = { FPCGEditorCommon::PCGAssetCategoryPath };
	return Categories;
}

// Menu Extensions
//--------------------------------------------------------------------

namespace MenuExtension_PCGGraphInterface
{
	static void ExecuteNewPCGGraphInstance(const FToolMenuContext& MenuContext)
	{
		const UContentBrowserAssetContextMenuContext* CBContext = UContentBrowserAssetContextMenuContext::FindContextWithAssets(MenuContext);

		IAssetTools::Get().CreateAssetsFrom<UPCGGraphInterface>(
			CBContext->LoadSelectedObjects<UPCGGraphInterface>(), UPCGGraphInstance::StaticClass(), TEXT("_Inst"), [](UPCGGraphInterface* SourceObject)
			{
				UPCGGraphInstanceFactory* Factory = NewObject<UPCGGraphInstanceFactory>();
				Factory->ParentGraph = SourceObject;
				return Factory;
			}
		);
	}

	static FDelayedAutoRegisterHelper DelayedAutoRegister(EDelayedRegisterRunPhase::EndOfEngineInit, []{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
		{
			FToolMenuOwnerScoped OwnerScoped(UE_MODULE_NAME);
			UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UPCGGraphInterface::StaticClass());

			FToolMenuSection& Section = Menu->FindOrAddSection("GetAssetActions");
			Section.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
			{
				{
					const TAttribute<FText> Label = LOCTEXT("PCGGraph_NewInstance", "Create PCG Graph Instance");
					const TAttribute<FText> ToolTip = LOCTEXT("PCGGraph_NewInstanceToolTip", "Creates a parameterized PCG graph using this graph as a base.");
					const FToolMenuExecuteAction UIAction = FToolMenuExecuteAction::CreateStatic(&ExecuteNewPCGGraphInstance);

					InSection.AddMenuEntry("PCGGraph_NewInstance", Label, ToolTip, FSlateIcon(FPCGEditorStyle::Get().GetStyleSetName(), "ClassIcon.PCGGraphInstance"), UIAction);
				}
			}));

			Section.AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
			{
				const UContentBrowserAssetContextMenuContext* CBContext = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InSection);

				const bool bStandaloneGraphSelected = CBContext->SelectedAssets.ContainsByPredicate([](const FAssetData& InAsset)
				{
					return UPCGGraphInterface::IsStandaloneGraphAsset(InAsset);
				});

				if (bStandaloneGraphSelected)
				{
					const TAttribute<FText> Label = LOCTEXT("GenerateStandaloneGraphsMenuLabel", "Generate Standalone Graph(s)");
					const TAttribute<FText> ToolTip = LOCTEXT("GenerateStandaloneGraphsMenuToolTip", "Will generate every standalone graph in the selection.");
					const FSlateIcon Icon = FSlateIcon(FPCGEditorStyle::Get().GetStyleSetName(), "ClassIcon.PCGGraph");

					FToolUIAction UIAction;
					UIAction.ExecuteAction.BindLambda([](const FToolMenuContext& InContext)
					{
						const UContentBrowserAssetContextMenuContext* CBContext = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InContext);

						if (UPCGEngineSubsystem* EngineSubsystem = UPCGEngineSubsystem::Get())
						{
							for (const FAssetData& AssetData : CBContext->SelectedAssets)
							{
								bool bTagValue = false;
								const bool bIsStandaloneGraph = UPCGGraphInterface::IsStandaloneGraphAsset(AssetData);
								const bool bExportAssetActionAvailable = AssetData.IsInstanceOf<UPCGGraphInterface>() && AssetData.GetTagValue(GET_MEMBER_NAME_CHECKED(UPCGGraphInterface, bExposeGenerationInAssetExplorer), bTagValue) && bTagValue;
								if (!bIsStandaloneGraph || !bExportAssetActionAvailable)
								{
									continue;
								}

								if (UPCGGraphInterface* GraphInterface = Cast<UPCGGraphInterface>(AssetData.GetAsset()))
								{
									EngineSubsystem->GenerateGraph({ GraphInterface });
								}
							}
						}
					});
					InSection.AddMenuEntry("GenerateStandaloneGraphsMenu", Label, ToolTip, Icon, UIAction);
				}
			}));
		}));
	});
}

#undef LOCTEXT_NAMESPACE
