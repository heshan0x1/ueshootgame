// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsOutlinerModule.h"

#include "LevelEditor.h"
#include "SceneOutlinerPublicTypes.h"
#include "TedsOutlinerMode.h"
#include "WorkspaceMenuStructureModule.h"
#include "WorkspaceMenuStructure.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementPackageColumns.h"
#include "Elements/Columns/TypedElementTypeInfoColumns.h"
#include "Elements/Columns/TypedElementVisibilityColumns.h"
#include "Columns/TedsActorComponentCompatibilityColumns.h"
#include "Columns/TedsActorMobilityColumns.h"
#include "TedsAlertColumns.h"
#include "DataStorage/Features.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Framework/TypedElementQueryContext.h"
#include "Modules/ModuleManager.h"
#include "Compatibility/SceneOutlinerTedsBridge.h"
#include "Compatibility/SceneOutlinerRowHandleColumn.h"
#include "Widgets/Docking/SDockTab.h"

#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"

#define LOCTEXT_NAMESPACE "TedsOutlinerModule"

namespace UE::Editor::Outliner
{
	namespace Private
	{
		static bool bUseNewRevisionControlWidgets = false;
	} // namespace Private

	void RefreshLevelEditorTedsOutliner(bool bAlwaysInvoke)
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		FTedsOutlinerModule& TedsOutlinerModule = FModuleManager::GetModuleChecked<FTedsOutlinerModule>("TedsOutliner");
		FName TabId = TedsOutlinerModule.GetTedsOutlinerTabName();

		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
		if(bAlwaysInvoke || LevelEditorTabManager->FindExistingLiveTab(TabId))
		{
			LevelEditorTabManager->TryInvokeTab(TabId);
		}
	}
	
	static FAutoConsoleVariableRef CVarUseNewRevisionControlWidgets(
		TEXT("TEDS.UI.UseNewRevisionControlWidgets"),
		Private::bUseNewRevisionControlWidgets,
		TEXT("Use new TEDS-based source control widgets in the Outliner (requires TEDS-Outliner to be enabled)")
		, FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*)
		{
			RefreshLevelEditorTedsOutliner(false);
		}));

	// CVar to summon the TEDS-Outliner as a separate tab
	static FAutoConsoleCommand OpenTableViewerConsoleCommand(
		TEXT("TEDS.UI.OpenTedsOutliner"),
		TEXT("Spawn the test TEDS-Outliner Integration."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			RefreshLevelEditorTedsOutliner(true);
		}));

FTedsOutlinerModule::FTedsOutlinerModule()
{
}

TSharedRef<ISceneOutliner> FTedsOutlinerModule::CreateTedsOutliner(const FSceneOutlinerInitializationOptions& InInitOptions, const FTedsOutlinerParams& InInitTedsOptions) const
{
	using namespace UE::Editor::DataStorage;
	ensureMsgf(AreEditorDataStorageFeaturesEnabled(), TEXT("Unable to initialize the Teds-Outliner before TEDS itself is initialized."));
	
	FSceneOutlinerInitializationOptions InitOptions(InInitOptions);
	FTedsOutlinerParams InitTedsOptions(InInitTedsOptions);

	InitOptions.ModeFactory = FCreateSceneOutlinerMode::CreateLambda([&InitTedsOptions](SSceneOutliner* Outliner)
	{
		InitTedsOptions.SceneOutliner = Outliner;
		
		return new FTedsOutlinerMode(InitTedsOptions);
	});

	// Add the custom column that displays row handles
	if (InInitTedsOptions.bShowRowHandleColumn)
	{
		InitOptions.ColumnMap.Add(FSceneOutlinerRowHandleColumn::GetID(),
			FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 2,
				FCreateSceneOutlinerColumn::CreateLambda([](ISceneOutliner& InSceneOutliner)
				{
					return MakeShareable(new FSceneOutlinerRowHandleColumn(InSceneOutliner));
				})));
	}
	
	InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::Label(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 10));
	
	TSharedRef<ISceneOutliner> TedsOutlinerShared = SNew(SSceneOutliner, InitOptions);
	
	return TedsOutlinerShared;
}

void FTedsOutlinerModule::StartupModule()
{
	IModuleInterface::StartupModule();

	TedsOutlinerTabName = TEXT("LevelEditorTedsOutliner");
	RegisterLevelEditorTedsOutlinerTab();
}

void FTedsOutlinerModule::ShutdownModule()
{
	UnregisterLevelEditorTedsOutlinerTab();
	IModuleInterface::ShutdownModule();
}

UE::Editor::DataStorage::FQueryDescription FTedsOutlinerModule::GetLevelEditorTedsOutlinerColumnQueryDescription()
{
	using namespace UE::Editor::DataStorage;
	using namespace UE::Editor::DataStorage::Columns;
	Storage = GetMutableDataStorageFeature<ICoreProvider>(StorageFeatureName);
		
	using namespace DataStorage::Queries;

	static FQueryDescription ColumnQuery = 
		Select()
			.ReadOnly<FTypedElementClassTypeInfoColumn,
					  FAlertColumn, 
					  FChildAlertColumn,
					  FUObjectIdNameColumn,
					  FTedsActorMobilityColumn,
					  FLevelColumn,
					  FVisibleInEditorColumn>()
		.Compile();

	// Query to also include revision control info
	static FQueryDescription RevisionControlQuery = 
		Select()
			.ReadOnly<FTypedElementClassTypeInfoColumn,  
					  FTypedElementPackageReference, 
					  FAlertColumn, 
					  FUObjectIdNameColumn,
					  FTedsActorMobilityColumn,
					  FLevelColumn,
					  FVisibleInEditorColumn>()
		.Compile();

	return Private::bUseNewRevisionControlWidgets ? RevisionControlQuery : ColumnQuery;
}

TSharedRef<SWidget> FTedsOutlinerModule::CreateLevelEditorTedsOutliner()
{
	if(!DataStorage::AreEditorDataStorageFeaturesEnabled())
	{
		return SNew(STextBlock)
		.Text(LOCTEXT("TEDSPluginNotEnabledText", "You need to enable the Typed Element Data Storage plugin to see the table viewer!"));
	}

	using namespace DataStorage::Queries;

	// The Outliner is populated with Actors and Entities
	DataStorage::FQueryDescription OutlinerQueryDescription =
		Select()
		.Where()
			.All<FTypedElementClassTypeInfoColumn>() // TEDS-Outliner TODO: Currently looking at all entries with type info in TEDS
		.Compile();

	FSceneOutlinerInitializationOptions InitOptions;
	InitOptions.bShowHeaderRow = true;
	InitOptions.FilterBarOptions.bHasFilterBar = true;
	InitOptions.bShowTransient = true;
	InitOptions.OutlinerIdentifier = "TEDSOutliner";

	FTedsOutlinerParams Params(nullptr);
	Params.QueryDescription = OutlinerQueryDescription;
	Params.ColumnQueryDescription = GetLevelEditorTedsOutlinerColumnQueryDescription();

	Params.ClassFilters.Add(AActor::StaticClass());
	Params.ClassFilters.Add(AStaticMeshActor::StaticClass());
	Params.ClassFilters.Add(ADirectionalLight::StaticClass());
	
	// Example ways to create filters using query descriptions and functions
	// Note QueryDescriptions should always be used over QueryFunctions when possible as they are more performant
	Params.Filters.Emplace(
		"ActorComponents", 
		LOCTEXT("ActorFilterDisplayName", "Actor Components"), 
		LOCTEXT("ActorFilterToolTip", "Filters for actor components"), 
		"ClassIcon.Actor", 
		nullptr,
		Storage->RegisterQuery(
			Select()
			.Where()
				.All<FActorComponentTypeTag>()
			.Compile()));

	// TODO: Make it possible to set EFunctionCallConfig::VerifyColumns in BuildQueryFunction as we will usually want to exclude rows that 
	// don't have the columns we want to filter (MobilityColumn=Static will only filter on columns that have the Mobility Column and
	// return true for rows that don't have that column), perhaps add it to EFilterOptions?
	Params.Filters.Emplace(
		"Static",
		LOCTEXT("StaticFilterDisplayName", "Static"), 
		LOCTEXT("StaticFilterToolTip", "Filter for Static Actors"),
		FName(), 
		nullptr,
		[](TQueryContext<SingleRowInfo> Context, const FTedsActorMobilityColumn& MobilityColumn)
		{
			return MobilityColumn.Mobility == EComponentMobility::Static;
		});
		
	// Empty selection set name is currently the level editor
	Params.SelectionSetOverride = FName();
	
	TSharedRef<ISceneOutliner> TEDSOutlinerShared = CreateTedsOutliner(InitOptions, Params);
	
	return TEDSOutlinerShared;
}

TSharedRef<SDockTab> FTedsOutlinerModule::OpenLevelEditorTedsOutliner(const FSpawnTabArgs& SpawnTabArgs)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[
			CreateLevelEditorTedsOutliner()
		];
}

// The TEDS-Outliner as a separate tab
void FTedsOutlinerModule::RegisterLevelEditorTedsOutlinerTab()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		
	LevelEditorTabManagerChangedHandle = LevelEditorModule.OnTabManagerChanged().AddLambda([this]()
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");

		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

		LevelEditorTabManager->RegisterTabSpawner(TedsOutlinerTabName, FOnSpawnTab::CreateRaw(this, &FTedsOutlinerModule::OpenLevelEditorTedsOutliner))
		.SetDisplayName(LOCTEXT("TedsTableVIewerTitle", "Table Viewer (Experimental)"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorOutlinerCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"))
		.SetAutoGenerateMenuEntry(false); // This can only be summoned from the Cvar now
	
	});
}

void FTedsOutlinerModule::UnregisterLevelEditorTedsOutlinerTab()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.OnTabManagerChanged().Remove(LevelEditorTabManagerChangedHandle);
	TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
}

FName FTedsOutlinerModule::GetTedsOutlinerTabName()
{
	return TedsOutlinerTabName;
}
} // namsepace UE::Editor::Outliner

IMPLEMENT_MODULE(UE::Editor::Outliner::FTedsOutlinerModule, TedsOutliner);

#undef LOCTEXT_NAMESPACE // TedsOutlinerModule
