// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorModule.h"

#include "Framework/Notifications/NotificationManager.h"
#include "ILauncherPlatform.h"
#include "Interfaces/IPluginManager.h"
#include "LauncherPlatformModule.h"
#include "Logging/StructuredLog.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterThumbnailRenderer.h"
#include "MetaHumanCharacterEditorCommands.h"
#include "MetaHumanCharacterEditorLog.h"
#include "MetaHumanCharacterEditorStyle.h"
#include "MetaHumanCharacterAssetObserver.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SMetaHumanCharacterEditorPreviewSettingsView.h"
#include "SMetaHumanPreviewSceneDetailCustomization.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "MetaHumanWardrobeItem.h"
#include "MetaHumanWardrobeItemThumbnailRenderer.h"
#include "Tools/MetaHumanCharacterEditorBodyConformTool.h"
#include "Tools/MetaHumanCharacterEditorBodyEditingTools.h"
#include "Tools/MetaHumanCharacterEditorConformTool.h"
#include "Tools/MetaHumanCharacterEditorCostumeTools.h"
#include "Tools/MetaHumanCharacterEditorMakeupTool.h"
#include "Tools/MetaHumanCharacterEditorPipelineTools.h"
#include "Tools/MetaHumanCharacterEditorSkinTool.h"
#include "Tools/MetaHumanCharacterEditorSubTools.h"
#include "Tools/MetaHumanCharacterEditorWardrobeTools.h"
#include "Tools/Customizations/MetaHumanCharacterImportTemplatePropertiesCustomization.h"
#include "Tools/Customizations/MetaHumanCharacterEditorCostumeItemDetailCustomization.h"
#include "Tools/Customizations/MetaHumanCharacterEditorPipelineToolPropertiesCustomization.h"
#include "Tools/Customizations/MetaHumanCharacterImportBodyDNAPropertiesCustomization.h"
#include "Tools/Customizations/MetaHumanCharacterImportBodyTemplatePropertiesCustomization.h"
#include "UI/Viewport/SMetaHumanCharacterEditorViewportToolBar.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY(LogMetaHumanCharacterEditor)

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorModule"

namespace UE::MetaHuman
{
	static void CheckMetaHumanContentInstallation()
	{
		if (!FMetaHumanCharacterEditorModule::IsOptionalMetaHumanContentInstalled())
		{
			static TWeakPtr<SNotificationItem> MetaHumanContentWarningNotification;

			const FText MetaHumanContentWarningText = LOCTEXT("OptionalContentMissingWarning", "The MetaHuman Creator plugin requires that the MetaHuman Creator Core Data be installed alongside the Engine. Its functionality will be significantly limited without it.");
			const FText LoadEGLButtonText = LOCTEXT("OptionalContentMissingOpenEGLButton", "Open the Epic Games Launcher");

			FNotificationInfo Info(MetaHumanContentWarningText);
			Info.ExpireDuration = 30.0f;
			Info.bFireAndForget = true;
			Info.bUseLargeFont = false;
			Info.bUseThrobber = false;
			Info.bUseSuccessFailIcons = true;
			Info.ButtonDetails.Add(FNotificationButtonInfo(LoadEGLButtonText, FText(), FSimpleDelegate::CreateLambda([]()
				{
					if (FLauncherPlatformModule::Get()->CanOpenLauncher(/*Install=*/false))
					{
						const FOpenLauncherOptions OpenOptions(TEXT("ue/library"));
						FLauncherPlatformModule::Get()->OpenLauncher(OpenOptions);
					}
					else
					{
						UE_LOGFMT(LogMetaHumanCharacterEditor, Error, "Failed to open Epic Games Launcher");
					}

					if (MetaHumanContentWarningNotification.IsValid())
					{
						MetaHumanContentWarningNotification.Pin()->SetCompletionState(SNotificationItem::CS_None);
						MetaHumanContentWarningNotification.Pin()->ExpireAndFadeout();
						MetaHumanContentWarningNotification.Reset();
					}
				})));

			if (MetaHumanContentWarningNotification.IsValid())
			{
				MetaHumanContentWarningNotification.Pin()->ExpireAndFadeout();
				MetaHumanContentWarningNotification.Reset();
			}

			MetaHumanContentWarningNotification = FSlateNotificationManager::Get().AddNotification(Info);

			if (MetaHumanContentWarningNotification.IsValid())
			{
				MetaHumanContentWarningNotification.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
			}
		}
	}
}

FMetaHumanCharacterEditorModule& FMetaHumanCharacterEditorModule::GetChecked()
{
	return FModuleManager::GetModuleChecked<FMetaHumanCharacterEditorModule>(UE_MODULE_NAME);
}

bool FMetaHumanCharacterEditorModule::IsOptionalMetaHumanContentInstalled()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME);
	const FString MetaHumanContentFolderPath = Plugin->GetContentDir() + TEXT("/Optional");
	const FString MetaHumanBodyTexturesFolderPath = MetaHumanContentFolderPath / TEXT("BodyTextures");
	IFileManager& FileManager = IFileManager::Get();

	// Perform a series of checks on folders and files in the Optional folder that are more likely to cause editor issues when not present 

	if (!FPaths::DirectoryExists(MetaHumanContentFolderPath)
		|| !FPaths::DirectoryExists(MetaHumanBodyTexturesFolderPath))
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Warning, "MetaHuman Optional Content folder not found. MetaHuman Creator plugin initialized with limited features.");
		return false;
	}

	TArray<FString> FoundFiles;
	FileManager.FindFilesRecursive(FoundFiles, *MetaHumanContentFolderPath, TEXT("*.ar"), /*Files=*/true, /*Directories=*/false);
	if (FoundFiles.IsEmpty())
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Warning, "MetaHuman textures synthesis model data not found. Skin editing in the MetaHuman Creator plugin will not function correctly.");
		return false;
	}

	FoundFiles.Reset();
	FileManager.FindFiles(FoundFiles, *MetaHumanBodyTexturesFolderPath, TEXT("*.uasset"));
	if (FoundFiles.IsEmpty())
	{
		UE_LOGFMT(LogMetaHumanCharacterEditor, Warning, "MetaHuman default textures for body skeletal meshes not found. Skin editing in the MetaHuman Creator plugin will not function correctly.");
		return false;
	}

	return true;
}

void FMetaHumanCharacterEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	PropertyModule.RegisterCustomClassLayout(UMetaHumanCharacterImportTemplateProperties::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FMetaHumanCharacterImportTemplatePropertiesCustomization::MakeInstance));

	PropertyModule.RegisterCustomClassLayout(UMetaHumanCharacterImportBodyDNAProperties::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FMetaHumanCharacterImportBodyDNAPropertiesCustomization::MakeInstance));
	
	PropertyModule.RegisterCustomClassLayout(UMetaHumanCharacterImportBodyTemplateProperties::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FMetaHumanCharacterImportBodyTemplatePropertiesCustomization::MakeInstance));
	
	PropertyModule.RegisterCustomClassLayout(UMetaHumanCharacterEditorPipelineToolProperties::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FMetaHumanCharacterEditorPipelineToolPropertiesCustomization::MakeInstance));

	PropertyModule.RegisterCustomClassLayout(UMetaHumanCharacterEditorPreviewSceneDescription::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FMetaHumanPreviewSceneCustomization::MakeInstance));

	PropertyModule.RegisterCustomClassLayout(UMetaHumanCharacterEditorCostumeItem::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FMetaHumanCharacterEditorCostumeItemDetailCustomization::MakeInstance));

	FMetaHumanCharacterEditorCommands::Register();
	FMetaHumanCharacterEditorDebugCommands::Register();
	FMetaHumanCharacterEditorToolCommands::Register();

	FMetaHumanCharacterEditorStyle::Register();

	// Register the thumbnail renderer
	UThumbnailManager::Get().RegisterCustomRenderer(UMetaHumanCharacter::StaticClass(), UMetaHumanCharacterThumbnailRenderer::StaticClass());
	UThumbnailManager::Get().RegisterCustomRenderer(UMetaHumanWardrobeItem::StaticClass(), UMetaHumanWardrobeItemThumbnailRenderer::StaticClass());
	UE::MetaHuman::CheckMetaHumanContentInstallation();
}

void FMetaHumanCharacterEditorModule::ShutdownModule()
{
	FMetaHumanCharacterEditorCommands::Unregister();
	FMetaHumanCharacterEditorDebugCommands::Unregister();
	FMetaHumanCharacterEditorToolCommands::Unregister();
	FMetaHumanCharacterEditorStyle::Unregister();
	if (UObjectInitialized())
	{
		if (FPropertyEditorModule* PropertyModulePtr = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
		{
			PropertyModulePtr->UnregisterCustomClassLayout(UMetaHumanCharacterEditorSkinToolProperties::StaticClass()->GetFName());
			PropertyModulePtr->UnregisterCustomClassLayout(UMetaHumanCharacterEditorMakeupToolProperties::StaticClass()->GetFName());
			PropertyModulePtr->UnregisterCustomClassLayout(UMetaHumanCharacterEditorCostumeToolProperties::StaticClass()->GetFName());
			PropertyModulePtr->UnregisterCustomClassLayout(UMetaHumanCharacterEditorSubToolsProperties::StaticClass()->GetFName());
			PropertyModulePtr->UnregisterCustomClassLayout(UMetaHumanCharacterImportTemplateProperties::StaticClass()->GetFName());
			PropertyModulePtr->UnregisterCustomClassLayout(UMetaHumanCharacterEditorPreviewSceneDescription::StaticClass()->GetFName());
			PropertyModulePtr->UnregisterCustomClassLayout(UMetaHumanCharacterEditorCostumeItem::StaticClass()->GetFName());
		}

		UThumbnailManager::Get().UnregisterCustomRenderer(UMetaHumanCharacter::StaticClass());
		UThumbnailManager::Get().UnregisterCustomRenderer(UMetaHumanWardrobeItem::StaticClass());
	}
}

IMPLEMENT_MODULE(FMetaHumanCharacterEditorModule, MetaHumanCharacterEditor);

#undef LOCTEXT_NAMESPACE
