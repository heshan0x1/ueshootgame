// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosRigidAssetEditorModule.h"
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "ContentBrowserModule.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Dataflow/DataflowEditor.h"
#include "DataflowAttachment.h"
#include "DataflowRendering.h"
#include "Dataflow/DataflowRenderingFactory.h"
#include "Features/IModularFeatures.h"

IMPLEMENT_MODULE(FChaosRigidAssetEditorModule, ChaosRigidAssetEditor);

bool GRigidAssetModuleOverridePhysicsAssetEditor = false;
static FAutoConsoleVariableRef GRigidAssetModuleOverridePhysicsAssetEditorCVar(
	TEXT("p.rigidasset.enableeditoroverride"),
	GRigidAssetModuleOverridePhysicsAssetEditor,
	TEXT("Whether to enable the editor override for physics asset such that physics assets using dataflow will no longer open in the base physics asset editor, preferring the dataflow editor instead"));

/**
 * Override for physics asset editor to instead route to the dataflow editor if there
 * is a dataflow attachment present in the asset.
 */
class FDataflowPhysicsAssetEditorOverride : public IPhysicsAssetEditorOverride
{
public:

	virtual ~FDataflowPhysicsAssetEditorOverride() = default;

	bool OpenAsset(UPhysicsAsset* InAsset) override;
};

UPhysicsAsset* GetPhysAsset(const FAssetData& InAsset)
{
	if(UObject* AssetBase = InAsset.GetAsset())
	{
		return Cast<UPhysicsAsset>(AssetBase);
	}
	return nullptr;
}

UDataflowAttachment* GetDataflowAttachment(IInterface_AssetUserData* UserDataHolder)
{
	if(!UserDataHolder)
	{
		return nullptr;
	}

	return UserDataHolder->GetAssetUserData<UDataflowAttachment>();
}

bool HasDataflowAttachment(IInterface_AssetUserData* UserDataHolder)
{
	if(!UserDataHolder)
	{
		return false;
	}

	return UserDataHolder->HasAssetUserDataOfClass(UDataflowAttachment::StaticClass());
}

void AddDataflow(FAssetData InAsset)
{
	UPhysicsAsset* Asset = GetPhysAsset(InAsset);

	if(!Asset)
	{
		return;
	}

	Asset->AddAssetUserData(NewObject<UDataflowAttachment>(Asset));
}

void RemoveDataflow(FAssetData InAsset)
{
	UPhysicsAsset* Asset = GetPhysAsset(InAsset);

	if(!Asset)
	{
		return;
	}

	Asset->RemoveUserDataOfClass(UDataflowAttachment::StaticClass());
}

void SpawnEditorFor(UPhysicsAsset* InAsset)
{
	UDataflowAttachment* Attachment = GetDataflowAttachment(InAsset);
	if(Attachment)
	{
		if(!Attachment->GetDataflowInstance().GetDataflowAsset())
		{
			Attachment->GetDataflowInstance().SetDataflowAsset(Cast<UDataflow>(UE::DataflowAssetDefinitionHelpers::NewOrOpenDataflowAsset(InAsset)));
		}

		UAssetEditorSubsystem* const AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();

		UDataflowEditor* const AssetEditor = NewObject<UDataflowEditor>(AssetEditorSubsystem, NAME_None, RF_Transient);
		AssetEditor->RegisterToolCategories({ "General" });
		const TSubclassOf<AActor> ActorClass = StaticLoadClass(AActor::StaticClass(), nullptr, TEXT("/ChaosRigidAsset/BP_PhysicsAssetPreview.BP_PhysicsAssetPreview_C"), nullptr, LOAD_None, nullptr);

		AssetEditor->Initialize({ GetDataflowAttachment(InAsset) }, ActorClass);
	}
}

void EditDataflow(FAssetData InAsset)
{
	SpawnEditorFor(GetPhysAsset(InAsset));
}

bool CanAddDataflow(FAssetData InAsset)
{
	UPhysicsAsset* Asset = GetPhysAsset(InAsset);
	return Asset && !HasDataflowAttachment(Asset);
}

bool CanRemoveDataflow(FAssetData InAsset)
{
	UPhysicsAsset* Asset = GetPhysAsset(InAsset);
	return Asset && HasDataflowAttachment(Asset);
}

bool CanEditDataflow(FAssetData InAsset)
{
	UPhysicsAsset* Asset = GetPhysAsset(InAsset);
	return Asset && HasDataflowAttachment(Asset);
}

void FillMenu(FMenuBuilder& MenuBuilder, const TArray<FAssetData> Assets)
{
	if(Assets.Num() == 1 && Assets[0].IsInstanceOf<UPhysicsAsset>())
	{
		UPhysicsAsset* PhysAsset = GetPhysAsset(Assets[0]);

		if(!PhysAsset)
		{
			return;
		}

		const bool bHasAttachment = HasDataflowAttachment(PhysAsset);

		MenuBuilder.BeginSection("PhysicsAssetMenu_Dataflow", NSLOCTEXT("RigidAsset", "MenuSection", "DataFlow"));

		// Add Dataflow
		{
			FMenuEntryParams Params;
			Params.DirectActions.ExecuteAction = FExecuteAction::CreateStatic(&AddDataflow, Assets[0]);
			Params.DirectActions.CanExecuteAction = FCanExecuteAction::CreateStatic(&CanAddDataflow, Assets[0]);
			Params.LabelOverride = NSLOCTEXT("ChaosRigidAsset", "PhysicsAssetContextAddDataflow", "Add Dataflow");
			MenuBuilder.AddMenuEntry(Params);
		}

		// Remove Dataflow
		{
			FMenuEntryParams Params;
			Params.DirectActions.ExecuteAction = FExecuteAction::CreateStatic(&RemoveDataflow, Assets[0]);
			Params.DirectActions.CanExecuteAction = FCanExecuteAction::CreateStatic(&CanRemoveDataflow, Assets[0]);
			Params.LabelOverride = NSLOCTEXT("ChaosRigidAsset", "PhysicsAssetContextRemoveDataflow", "Remove Dataflow");
			MenuBuilder.AddMenuEntry(Params);
		}

		// Edit Dataflow
		{
			FMenuEntryParams Params;
			Params.DirectActions.ExecuteAction = FExecuteAction::CreateStatic(&EditDataflow, Assets[0]);
			Params.DirectActions.CanExecuteAction = FCanExecuteAction::CreateStatic(&CanEditDataflow, Assets[0]);
			Params.LabelOverride = NSLOCTEXT("ChaosRigidAsset", "PhysicsAssetContextEditDataflow", "Edit Dataflow");
			MenuBuilder.AddMenuEntry(Params);
		}

		MenuBuilder.EndSection();
	}
}

TSharedRef<FExtender> ExtendAssetContextMenu(const TArray<FAssetData>& Assets)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();

	Extender->AddMenuExtension("GetAssetActions", EExtensionHook::After, nullptr, FMenuExtensionDelegate::CreateStatic(&FillMenu, Assets));

	return Extender;
}

void FChaosRigidAssetEditorModule::StartupModule()
{
	FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
	TArray<FContentBrowserMenuExtender_SelectedAssets>& AssetMenuExtenders = ContentBrowser.GetAllAssetViewContextMenuExtenders();
	AssetMenuExtenders.Add(FContentBrowserMenuExtender_SelectedAssets::CreateStatic(&ExtendAssetContextMenu));
	AssetMenuExtenderHandle = AssetMenuExtenders.Last().GetHandle();

	EditorFeature = MakeUnique<FDataflowPhysicsAssetEditorOverride>();

	IModularFeatures::Get().RegisterModularFeature(IPhysicsAssetEditorOverride::ModularFeatureName, EditorFeature.Get());

	UE::Dataflow::FRenderingFactory::GetInstance()->RegisterCallbacks(MakeUnique<UE::Chaos::RigidAsset::FAggregateGeometryGeomRenderCallbacks>());
	UE::Dataflow::FRenderingFactory::GetInstance()->RegisterCallbacks(MakeUnique<UE::Chaos::RigidAsset::FBoneSelectionRenderCallbacks>());
	UE::Dataflow::FRenderingFactory::GetInstance()->RegisterCallbacks(MakeUnique<UE::Chaos::RigidAsset::FPhysAssetStateRenderCallbacks>());
}

void FChaosRigidAssetEditorModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(IPhysicsAssetEditorOverride::ModularFeatureName, EditorFeature.Get());
	EditorFeature = nullptr;

	if(AssetMenuExtenderHandle.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("ContentBrowser")))
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));
		TArray<FContentBrowserMenuExtender_SelectedAssets>& AssetMenuExtenders = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();

		AssetMenuExtenders.RemoveAll([this](const FContentBrowserMenuExtender_SelectedAssets& InDelegate)
			{
				return InDelegate.GetHandle() == AssetMenuExtenderHandle;
			});

		AssetMenuExtenderHandle.Reset();
	}

	UE::Dataflow::FRenderingFactory::GetInstance()->DeregisterCallbacks(UE::Chaos::RigidAsset::FPhysAssetStateRenderCallbacks::StaticGetRenderKey());
	UE::Dataflow::FRenderingFactory::GetInstance()->DeregisterCallbacks(UE::Chaos::RigidAsset::FBoneSelectionRenderCallbacks::StaticGetRenderKey());
	UE::Dataflow::FRenderingFactory::GetInstance()->DeregisterCallbacks(UE::Chaos::RigidAsset::FAggregateGeometryGeomRenderCallbacks::StaticGetRenderKey());
}

bool FDataflowPhysicsAssetEditorOverride::OpenAsset(UPhysicsAsset* InAsset)
{
	if(!GRigidAssetModuleOverridePhysicsAssetEditor)
	{
		return false;
	}

	UDataflowAttachment* Attachment = GetDataflowAttachment(InAsset);
	if(Attachment)
	{
		SpawnEditorFor(InAsset);

		return true;
	}

	return false;
}
