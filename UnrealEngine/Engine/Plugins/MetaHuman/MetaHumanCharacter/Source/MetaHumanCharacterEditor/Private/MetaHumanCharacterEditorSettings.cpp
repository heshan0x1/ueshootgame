// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanSDKSettings.h"
#include "ObjectTools.h"
#include "Misc/TransactionObjectEvent.h"
#include "Engine/Texture.h"
#include "Editor/EditorEngine.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Algo/Transform.h"
#include "Algo/AllOf.h"
#include "Misc/MessageDialog.h"
#include "Interfaces/IProjectManager.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorSettings"

extern UNREALED_API UEditorEngine* GEditor;

namespace UE::MetaHuman::Private
{
	static bool CanUpdateVirtualTextureSupport()
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();

		const TArray<UObject*> OpenAssets = AssetEditorSubsystem->GetAllEditedAssets();
		TArray<UMetaHumanCharacter*> OpenCharacters;

		Algo::TransformIf(OpenAssets,
						  OpenCharacters,
						  [](UObject* Asset)
						  {
							  return Asset->IsA<UMetaHumanCharacter>();
						  },
						  [](UObject* Asset)
						  {
							  return Cast<UMetaHumanCharacter>(Asset);
						  });

		if (!OpenCharacters.IsEmpty())
		{
			// Get confirmation from the user that its ok to proceed
			const FText Title = LOCTEXT("UpdateVTSupport_Title", "Change Virtual Texture Support");
			const FText Message = LOCTEXT("UpdateVTSupport_CloseAssetsMessage", "Changing Virtual Texture support for MetaHuman Characters requires"
																				" currently opened editors to be closed. Would you like to continue?");
			const EAppReturnType::Type Response = FMessageDialog::Open(EAppMsgCategory::Warning, EAppMsgType::YesNo, Message, Title);
			if (Response == EAppReturnType::No)
			{
				return false;
			}

			for (UMetaHumanCharacter* Character : OpenCharacters)
			{
				AssetEditorSubsystem->CloseAllEditorsForAsset(Character);
			}			
		}

		return true;
	}
}

UMetaHumanCharacterEditorSettings::UMetaHumanCharacterEditorSettings()
{
	SculptManipulatorMesh = FSoftObjectPath(TEXT("/Script/Engine.StaticMesh'/" UE_PLUGIN_NAME "/Tools/SM_SculptTool_Gizmo.SM_SculptTool_Gizmo'"));
	MoveManipulatorMesh = FSoftObjectPath(TEXT("/Script/Engine.StaticMesh'/" UE_PLUGIN_NAME "/Tools/SM_MoveTool_Gizmo.SM_MoveTool_Gizmo'"));

	// Set the initial value of MigratePackagePath to be same as the one defined for cinematic characters in the SDK settings
	const UMetaHumanSDKSettings* MetaHumanSDKSettings = GetDefault<UMetaHumanSDKSettings>();
	MigratedPackagePath = MetaHumanSDKSettings->CinematicImportPath;

	// Add the default and optional template animation data table object paths.
	TemplateAnimationDataTableAssets.Add(FSoftObjectPath(TEXT("/Script/Engine.DataTable'/" UE_PLUGIN_NAME "/Animation/TemplateAnimations/DT_MH_TemplateAnimations.DT_MH_TemplateAnimations'")));
	TemplateAnimationDataTableAssets.Add(FSoftObjectPath(TEXT("/Script/Engine.DataTable'/" UE_PLUGIN_NAME "/Optional/Animation/TemplateAnimations/DT_MH_TemplateAnimations.DT_MH_TemplateAnimations'")));

	DefaultRenderingQualities.Add(EMetaHumanCharacterRenderingQuality::Medium);
	DefaultRenderingQualities.Add(EMetaHumanCharacterRenderingQuality::High);
	DefaultRenderingQualities.Add(EMetaHumanCharacterRenderingQuality::Epic);
}

void UMetaHumanCharacterEditorSettings::PreEditChange(FProperty* InPropertyAboutToChange)
{
	Super::PreEditChange(InPropertyAboutToChange);

	if (InPropertyAboutToChange && InPropertyAboutToChange->GetName() == GET_MEMBER_NAME_CHECKED(ThisClass, bUseVirtualTextures))
	{
		bPreEditChangeUseVirtualTextures = bUseVirtualTextures;
	}
}

void UMetaHumanCharacterEditorSettings::PostEditChangeProperty(struct FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	const FName PropertyName = InPropertyChangedEvent.GetPropertyName();
	const FName MemberPropertyName = InPropertyChangedEvent.GetMemberPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, MigratedPackagePath))
	{
		ObjectTools::SanitizeInvalidCharsInline(MigratedPackagePath.Path, INVALID_LONGPACKAGE_CHARACTERS);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, MigratedNamePrefix))
	{
		ObjectTools::SanitizeInvalidCharsInline(MigratedNamePrefix, INVALID_LONGPACKAGE_CHARACTERS);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, MigratedNameSuffix))
	{
		ObjectTools::SanitizeInvalidCharsInline(MigratedNameSuffix, INVALID_LONGPACKAGE_CHARACTERS);
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, bEnableExperimentalWorkflows))
	{
		OnExperimentalAssemblyOptionsStateChanged.ExecuteIfBound();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, WardrobePaths)
		|| MemberPropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, WardrobePaths))
	{
		ObjectTools::SanitizeInvalidCharsInline(MigratedPackagePath.Path, INVALID_LONGPACKAGE_CHARACTERS);
		OnWardrobePathsChanged.Broadcast();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, PresetsDirectories)
		|| MemberPropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, PresetsDirectories))
	{
		for (FDirectoryPath& PresetDirectory : PresetsDirectories)
		{
			ObjectTools::SanitizeInvalidCharsInline(PresetDirectory.Path, INVALID_LONGPACKAGE_CHARACTERS);
		}

		OnPresetsDirectoriesChanged.ExecuteIfBound();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(ThisClass, bUseVirtualTextures))
	{
		if (!UE::MetaHuman::Private::CanUpdateVirtualTextureSupport())
		{
			// Revert back to the value from PreEditChange
			bUseVirtualTextures = bPreEditChangeUseVirtualTextures;
		}
	}
}

void UMetaHumanCharacterEditorSettings::PostTransacted(const FTransactionObjectEvent& InTransactionEvent)
{
	Super::PostTransacted(InTransactionEvent);

	if (InTransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		TArray<FName> PropertiesChanged = InTransactionEvent.GetChangedProperties();

		if (PropertiesChanged.Contains(GET_MEMBER_NAME_CHECKED(ThisClass, WardrobePaths)))
		{
			OnWardrobePathsChanged.Broadcast();
		}
	}
}

bool UMetaHumanCharacterEditorSettings::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, bUseVirtualTextures))
	{
		return  UTexture::IsVirtualTexturingEnabled();
	}

	return Super::CanEditChange(InProperty);
}

FText UMetaHumanCharacterEditorSettings::GetSectionText() const
{
	return LOCTEXT("MetaHumanCharacterEditorSettingsName", "MetaHuman Character");
}

FText UMetaHumanCharacterEditorSettings::GetSectionDescription() const
{
	return LOCTEXT("MetaHumanCharacterEditorSettingsDescription", "Configure the MetaHuman Character Editor plugin");
}

const bool UMetaHumanCharacterEditorSettings::ShouldUseVirtualTextures() const
{
	ITargetPlatformManagerModule& TargetPlatformManagerModule = GetTargetPlatformManagerRef();

	const TArray<ITargetPlatform*>& TargetPlatforms = TargetPlatformManagerModule.GetActiveTargetPlatforms();
	const bool bAllEnabledPlatformsSupportVTs = Algo::AllOf(TargetPlatforms, UTexture::IsVirtualTexturingEnabled);

	return bAllEnabledPlatformsSupportVTs && bUseVirtualTextures;
}

#undef LOCTEXT_NAMESPACE
