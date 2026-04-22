// Copyright Epic Games, Inc. All Rights Reserved.

#include "SVariablesView.h"

#include "AnimNextAssetWorkspaceAssetUserData.h"
#include "AnimNextRigVMAsset.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "SAssetDropTarget.h"
#include "SceneOutlinerPublicTypes.h"
#include "SceneOutlinerSourceControlColumn.h"
#include "ScopedTransaction.h"
#include "SSceneOutliner.h"
#include "Outliner/VariablesOutlinerColumns.h"
#include "Outliner/VariablesOutlinerMode.h"
#include "Outliner/VariablesOutlinerEntryItem.h"
#include "IWorkspaceEditor.h"
#include "IWorkspaceEditorModule.h"
#include "WorkspaceAssetRegistryInfo.h"
#include "Outliner/VariablesOutlinerAssetItem.h"
#include "Entries/AnimNextSharedVariablesEntry.h"

#define LOCTEXT_NAMESPACE "SVariablesView"

namespace UE::UAF::Editor
{

const FLazyName VariablesTabName("VariablesTab");

void SVariablesOutliner::RegisterAssetDelegates(const UAnimNextRigVMAsset* InAsset)
{	
	// Bind for any modification callbacks
	UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(InAsset);
	if(EditorData != nullptr)
	{
		EditorData->ModifiedDelegate.AddSP(this, &SVariablesOutliner::OnEditorDataModified);

		for (const TObjectPtr<UAnimNextRigVMAssetEntry>& Entry : EditorData->Entries)
		{
			if (UAnimNextSharedVariablesEntry* SharedVariablesEntry = Cast<UAnimNextSharedVariablesEntry>(Entry.Get()))
			{
				if (TObjectPtr<const UAnimNextSharedVariables> SharedVariablesAsset = SharedVariablesEntry->GetAsset())
				{
					RegisterAssetDelegates(SharedVariablesAsset);
					Assets.AddUnique(Cast<UAnimNextRigVMAsset>(const_cast<UAnimNextSharedVariables*>(SharedVariablesAsset.Get())));					
				}
			}
		}
	}
}

void SVariablesOutliner::UnregisterAssetDelegates(const UAnimNextRigVMAsset* InAsset)
{
	UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(InAsset);
	if(EditorData != nullptr)
	{
		EditorData->ModifiedDelegate.RemoveAll(this);

		for (const TObjectPtr<UAnimNextRigVMAssetEntry>& Entry : EditorData->Entries)
		{
			if (UAnimNextSharedVariablesEntry* SharedVariablesEntry = Cast<UAnimNextSharedVariablesEntry>(Entry.Get()))
			{
				if (TObjectPtr<const UAnimNextSharedVariables> SharedVariablesAsset = SharedVariablesEntry->GetAsset())
				{
					UnregisterAssetDelegates(SharedVariablesAsset);					
				}
			}
		}
	}
}

void SVariablesOutliner::SetAssets(TConstArrayView<TSoftObjectPtr<UAnimNextRigVMAsset>> InAssets)
{
	for(const TSoftObjectPtr<UAnimNextRigVMAsset>& CurrentSoftAsset : Assets)
	{
		if(UAnimNextRigVMAsset* CurrentAsset = CurrentSoftAsset.Get())
		{
			UnregisterAssetDelegates(CurrentAsset);
		}
	}

	Assets = InAssets;

	for(const TSoftObjectPtr<UAnimNextRigVMAsset>& NewSoftAsset : InAssets)
	{
		if(UAnimNextRigVMAsset* NewAsset = NewSoftAsset.Get())
		{
			RegisterAssetDelegates(NewAsset);
		}
	}

	FullRefresh();
}

void SVariablesOutliner::UpdateAssets()
{
	TArray<FSoftObjectPath> AssetsToAsyncLoad;
	TArray<TSoftObjectPtr<UAnimNextRigVMAsset>> ExportAssets;

	auto HandleAssetPath = [this, &AssetsToAsyncLoad, &ExportAssets](const FSoftObjectPath& Path)
	{
		ExportAssets.Add(TSoftObjectPtr<UAnimNextRigVMAsset>(Path));

		if (Path.ResolveObject() == nullptr)
		{
			AssetsToAsyncLoad.Add(Path);
		}
	};
	
	if (FWorkspaceOutlinerAssetReferenceItemData::IsAssetReference(Export)
		|| FWorkspaceOutlinerGroupItemData::IsGroupItem(Export)
		|| FAnimNextCollapseGraphsOutlinerDataBase::IsCollapsedGraphBase(Export))
	{
		// Reference item
		TArray<FSoftObjectPath> AssetPaths;
		Export.GetAssetPaths(AssetPaths);

		for (const FSoftObjectPath& Path : AssetPaths)
		{
			HandleAssetPath(Path);
		}
	}
	else
	{
		const FSoftObjectPath FirstAssetPath = Export.GetFirstAssetPath();
		if(FirstAssetPath.IsAsset())
		{
			HandleAssetPath(FirstAssetPath);
		}
		else if(Export.HasData() && Export.GetData().GetScriptStruct()->IsChildOf(FAnimNextAssetEntryOutlinerData::StaticStruct()))
		{
			const FAnimNextAssetEntryOutlinerData& EntryData = Export.GetData().Get<FAnimNextAssetEntryOutlinerData>();
			if(EntryData.SoftEntryPtr.IsValid())
			{
				if(UAnimNextRigVMAsset* Asset = EntryData.GetEntry()->GetTypedOuter<UAnimNextRigVMAsset>())
				{
					HandleAssetPath(Asset);
				}
			}
		}
	}

	SetAssets(ExportAssets);

	// Try async load any missing assets
	for(const FSoftObjectPath& AssetPath : AssetsToAsyncLoad)
	{
		TWeakPtr<SVariablesOutliner> WeakOutliner = StaticCastSharedRef<SVariablesOutliner>(AsShared());
		
		AssetPath.LoadAsync(FLoadSoftObjectPathAsyncDelegate::CreateLambda([WeakOutliner](const FSoftObjectPath& InSoftObjectPath, UObject* InObject)
		{
			UAnimNextRigVMAsset* Asset = Cast<UAnimNextRigVMAsset>(InObject);
			if(Asset == nullptr)
			{
				return;
			}

			TSharedPtr<SVariablesOutliner> PinnedVariablesOutliner = WeakOutliner.Pin();
			if(!PinnedVariablesOutliner.IsValid())
			{
				return;
			}

			PinnedVariablesOutliner->HandleAssetLoaded(InSoftObjectPath, Asset);
		}));
	}
}

void SVariablesOutliner::SetExport(const FWorkspaceOutlinerItemExport& InExport)
{
	Export = InExport;

	UpdateAssets();
}

void SVariablesOutliner::HandleAssetLoaded(const FSoftObjectPath& InSoftObjectPath, UAnimNextRigVMAsset* InAsset)
{	
	if(InAsset && Assets.Contains(TSoftObjectPtr<UAnimNextRigVMAsset>(InAsset)))
	{
		RegisterAssetDelegates(InAsset);

		FullRefresh();
	}
}

void SVariablesOutliner::OnEditorDataModified(UAnimNextRigVMAssetEditorData* InEditorData, EAnimNextEditorDataNotifType InType, UObject* InSubject)
{	
	ensure(Assets.Contains(TSoftObjectPtr<UAnimNextRigVMAsset>(UncookedOnly::FUtils::GetAsset<UAnimNextRigVMAsset>(InEditorData))));

	switch(InType)
	{
	case EAnimNextEditorDataNotifType::UndoRedo:
	case EAnimNextEditorDataNotifType::EntryAdded:
	case EAnimNextEditorDataNotifType::VariableCategoryChanged:
	case EAnimNextEditorDataNotifType::CategoryAdded:
	case EAnimNextEditorDataNotifType::CategoryChanged:
		FullRefresh();
		break;
	case EAnimNextEditorDataNotifType::EntryRemoved:
		UpdateAssets();
		break;
	default:
		break;
	}
}

void SVariablesOutliner::SetHighlightedItem(FSceneOutlinerTreeItemPtr Item) const
{
	GetTreeView()->Private_SetItemHighlighted(Item, true);
}

void SVariablesOutliner::ClearHighlightedItem(FSceneOutlinerTreeItemPtr Item) const
{
	GetTreeView()->Private_SetItemHighlighted(Item, false);
}

bool SVariablesOutliner::HasAssets() const
{
	return Assets.Num() > 0;
}

void SVariablesView::Construct(const FArguments& InArgs, TSharedRef<UE::Workspace::IWorkspaceEditor> InWorkspaceEditor)
{
	FSceneOutlinerInitializationOptions InitOptions;
	InitOptions.OutlinerIdentifier = TEXT("AnimNextVariablesOutliner");
	InitOptions.bShowHeaderRow = true;
	InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::Label(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 0, FCreateSceneOutlinerColumn(), false, 0.5f));
	InitOptions.ColumnMap.Add(FVariablesOutlinerTypeColumn::GetID(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 10, FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& InSceneOutliner) { return MakeShared<FVariablesOutlinerTypeColumn>(InSceneOutliner); }), false,
		TOptional<float>(), TAttribute<FText>(), EHeaderComboVisibility::Ghosted));
	InitOptions.ColumnMap.Add(FVariablesOutlinerValueColumn::GetID(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 20, FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& InSceneOutliner) { return MakeShared<FVariablesOutlinerValueColumn>(InSceneOutliner); }), true, 0.5f));
	InitOptions.ColumnMap.Add(FSceneOutlinerSourceControlColumn::GetID(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 30, FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& InSceneOutliner) { return MakeShared<FSceneOutlinerSourceControlColumn>(InSceneOutliner); }), true));
	InitOptions.ColumnMap.Add(FVariablesOutlinerAccessSpecifierColumn::GetID(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 40, FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& InSceneOutliner) { return MakeShared<FVariablesOutlinerAccessSpecifierColumn>(InSceneOutliner); }), false));
	InitOptions.ModeFactory = FCreateSceneOutlinerMode::CreateLambda([this, WeakWorkspaceEditor=InWorkspaceEditor.ToWeakPtr()](SSceneOutliner* InOutliner) { return new FVariablesOutlinerMode(static_cast<SVariablesOutliner*>(InOutliner), WeakWorkspaceEditor.Pin().ToSharedRef()); });

	VariablesOutliner = SNew(SVariablesOutliner, InitOptions);
	VariablesOutliner->SetEnabled(MakeAttributeSP(VariablesOutliner.Get(), &SVariablesOutliner::HasAssets));

	InWorkspaceEditor->OnFocusedDocumentChanged().AddSP(this, &SVariablesView::HandleFocusedDocumentChanged);
	const Workspace::FWorkspaceDocument& Document = InWorkspaceEditor->GetFocusedWorkspaceDocument();
	HandleFocusedDocumentChanged(Document);

	ChildSlot
	[
		VariablesOutliner.ToSharedRef()
	];
}

void SVariablesView::SetExportDirectly(const FWorkspaceOutlinerItemExport& InExport) const
{
	if (VariablesOutliner.IsValid())
	{
		VariablesOutliner->SetExport(InExport);
	}
}

void SVariablesView::HandleFocusedDocumentChanged(const UE::Workspace::FWorkspaceDocument& InDocument) const
{
	const FWorkspaceOutlinerItemExport& Export = InDocument.Export;
	VariablesOutliner->SetExport(Export);
}

FAnimNextVariablesTabSummoner::FAnimNextVariablesTabSummoner(TSharedPtr<UE::Workspace::IWorkspaceEditor> InHostingApp)
	: FWorkflowTabFactory(VariablesTabName, StaticCastSharedPtr<FAssetEditorToolkit>(InHostingApp))
{
	TabLabel = LOCTEXT("AnimNextVariablesTabLabel", "Variables");
	TabIcon = FSlateIcon("EditorStyle", "LevelEditor.Tabs.Outliner");
	ViewMenuDescription = LOCTEXT("AnimNextVariablesTabMenuDescription", "Variables");
	ViewMenuTooltip = LOCTEXT("AnimNextVariablesTabToolTip", "Shows the Variables tab.");
	bIsSingleton = true;

	VariablesView = SNew(SVariablesView, InHostingApp.ToSharedRef());

	const Workspace::FWorkspaceDocument& Document = InHostingApp->GetFocusedWorkspaceDocument();
	VariablesView->VariablesOutliner->SetExport(Document.Export);
}

TSharedRef<SWidget> FAnimNextVariablesTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	return VariablesView.ToSharedRef();
}

FText FAnimNextVariablesTabSummoner::GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const
{
	return ViewMenuTooltip;
}

}

#undef LOCTEXT_NAMESPACE