// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsOutlinerHelpers.h"

#include "Columns/TedsOutlinerColumns.h"
#include "Compatibility/SceneOutlinerTedsBridge.h"
#include "DataStorage/Features.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Modules/ModuleManager.h"
#include "ISceneOutliner.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"

namespace UE::Editor::Outliner::Helpers
{
	FSceneOutlinerTreeItemPtr GetTreeItemFromRowHandle(const DataStorage::ICoreProvider* Storage, TSharedRef<ISceneOutliner> InOutliner, DataStorage::RowHandle InRowHandle)
	{
		// Check if the item is indexed by the row handle for the trivial case
		FSceneOutlinerTreeItemPtr Item = InOutliner->GetTreeItem(InRowHandle);

		if (Item)
		{
			return Item;
		}

		// Otherwise look up any dealiasers that might be bound to this outliner instance
		DataStorage::RowHandle OutlinerRow = Storage->LookupMappedRow(MappingDomain, DataStorage::FMapKey(InOutliner->GetOutlinerIdentifier()));

		if (Storage->IsRowAvailable(OutlinerRow))
		{
			if (const FTedsOutlinerDealiaserColumn* DealiaserColumn = Storage->GetColumn<FTedsOutlinerDealiaserColumn>(OutlinerRow))
			{
				if (DealiaserColumn->Dealiaser.IsBound())
				{
					return InOutliner->GetTreeItem(DealiaserColumn->Dealiaser.Execute(InRowHandle));
				}
			}
		}
		return nullptr;
		
	}
	
	bool RegisterOutlinerDealiaser(DataStorage::ICoreProvider* Storage, TSharedRef<ISceneOutliner> InOutliner, const FTreeItemIDDealiaser& InDealiaser)
	{
		DataStorage::RowHandle OutlinerRow = Storage->LookupMappedRow(MappingDomain, DataStorage::FMapKey(InOutliner->GetOutlinerIdentifier()));

		if (Storage->IsRowAvailable(OutlinerRow))
		{
			Storage->AddColumn(OutlinerRow, FTedsOutlinerDealiaserColumn{.Dealiaser = InDealiaser});
			return true;
		}

		return false;
	}
	
	FName GetTedsOutlinerTableName()
	{
		return TEXT("Editor_TedsOutlinerTable");
	}
	
	void RefreshLevelEditorOutliners()
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

		static const FName TabIDS[] = { LevelEditorTabIds::LevelEditorSceneOutliner, LevelEditorTabIds::LevelEditorSceneOutliner2, LevelEditorTabIds::LevelEditorSceneOutliner3, LevelEditorTabIds::LevelEditorSceneOutliner4 };

		for (const FName& TabID : TabIDS)
		{
			if (LevelEditorTabManager->FindExistingLiveTab(TabID).IsValid())
			{
				LevelEditorTabManager->TryInvokeTab(TabID)->RequestCloseTab();
				LevelEditorTabManager->TryInvokeTab(TabID);
			}
		}
	}
	
	FName FindOutlinerColumnFromTedsColumns(const DataStorage::ICoreProvider* Storage, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> TEDSColumns)
	{
		if(const USceneOutlinerTedsBridgeFactory* Factory = Storage->FindFactory<USceneOutlinerTedsBridgeFactory>())
		{
			return Factory->FindOutlinerColumnFromTedsColumns(TEDSColumns);
		}

		return NAME_None;
	}
	
    bool CheckValidFilterQueryHandle(const DataStorage::QueryHandle& InQueryHandle)
    {
		using namespace UE::Editor::DataStorage;
    	// Check if the given Query Handle is valid for a filter (Not an Observer Query Handle)
    	ICoreProvider* Storage = GetMutableDataStorageFeature<ICoreProvider>(StorageFeatureName);
    	if (ensureMsgf(Storage, TEXT("TEDS must be initialized before TEDS Filters")))
    	{
    		const EQueryCallbackType QueryHandleCallbackType = Storage->GetQueryDescription(InQueryHandle).Callback.Type;
    		return ensureMsgf(QueryHandleCallbackType == EQueryCallbackType::None || QueryHandleCallbackType == EQueryCallbackType::Processor,
    			TEXT("TEDS Filters cannot accept Observer Query Handles."));
    	}
    	return false;
    }
}
