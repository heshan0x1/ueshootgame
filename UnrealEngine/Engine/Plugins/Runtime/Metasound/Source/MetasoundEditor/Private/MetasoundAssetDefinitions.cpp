// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundAssetDefinitions.h"

#include "AssetDefinitionRegistry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AudioEditorSettings.h"
#include "Components/AudioComponent.h"
#include "ContentBrowserMenuContexts.h"
#include "ContentBrowserModule.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "IContentBrowserSingleton.h"
#include "ISourceControlModule.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundAssetManager.h"
#include "MetasoundAssetSubsystem.h"
#include "MetasoundBuilderSubsystem.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundEditor.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorModule.h"
#include "MetasoundEditorSettings.h"
#include "MetasoundEditorSubsystem.h"
#include "MetasoundFactory.h"
#include "MetasoundSource.h"
#include "MetasoundUObjectRegistry.h"
#include "Misc/AssetRegistryInterface.h"
#include "Modules/ModuleManager.h"
#include "ObjectEditorUtils.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/ISlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Styling/SlateTypes.h"
#include "ToolMenus.h"
#include "ToolMenuSection.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SOverlay.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetasoundAssetDefinitions)


#define LOCTEXT_NAMESPACE "MetaSoundEditor"


namespace Metasound::Editor
{
	namespace AssetDefinitionsPrivate
	{
		const FSlateBrush* GetClassBrush(const FAssetData& InAssetData, FName InClassName, bool bIsThumbnail = false)
		{
			using namespace Frontend;

			const FMetaSoundAssetClassInfo ClassInfo(InAssetData);
			if (!ClassInfo.bIsValid)
			{
				UE_LOG(LogMetaSound, VeryVerbose,
					TEXT("ClassBrush for asset '%s' may return incorrect preset icon. Asset requires reserialization."),
					*InAssetData.GetObjectPathString());
			}

			FString BrushName = FString::Printf(TEXT("MetasoundEditor.%s"), *InClassName.ToString());
			if (ClassInfo.DocInfo.bIsPreset)
			{
				BrushName += TEXT(".Preset");
			}
			BrushName += bIsThumbnail ? TEXT(".Thumbnail") : TEXT(".Icon");

			return &Metasound::Editor::Style::GetSlateBrushSafe(FName(*BrushName));
		}

		void ExecuteBrowseToPresetParentAsset(const FToolMenuContext& InContext)
		{
			using namespace Metasound;

			if (UObject* MetaSound = UContentBrowserAssetContextMenuContext::LoadSingleSelectedAsset<UObject>(InContext))
			{
				if (UAssetEditorSubsystem* AssetSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
				{
					FMetaSoundFrontendDocumentBuilder& DocumentBuilder = Frontend::IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(MetaSound);
					if (const FMetasoundAssetBase* MetaSoundAsset = DocumentBuilder.GetReferencedPresetAsset())
					{
						if (GEditor)
						{
							GEditor->SyncBrowserToObjects({ MetaSoundAsset->GetOwningAsset() });
						}
					}
				}
			}
		}

		void ExecuteOpenPresetParentAsset(const FToolMenuContext& InContext)
		{
			using namespace Metasound;

			if (UObject* MetaSound = UContentBrowserAssetContextMenuContext::LoadSingleSelectedAsset<UObject>(InContext))
			{
				if (UAssetEditorSubsystem* AssetSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
				{
					FMetaSoundFrontendDocumentBuilder& DocumentBuilder = Frontend::IDocumentBuilderRegistry::GetChecked().FindOrBeginBuilding(MetaSound);
					if (const FMetasoundAssetBase* MetaSoundAsset = DocumentBuilder.GetReferencedPresetAsset())
					{
						AssetSubsystem->OpenEditorForAsset(MetaSoundAsset->GetOwningAsset());
					}
				}
			}
		}
		
		template <typename TClass, typename TFactoryClass>
		void ExecuteCreateMetaSoundPreset(const FToolMenuContext& MenuContext)
		{
			if (const UContentBrowserAssetContextMenuContext* Context = UContentBrowserAssetContextMenuContext::FindContextWithAssets(MenuContext))
			{
				using namespace Metasound::Editor;
				TArray<UObject*> ObjectsToSync;

				for (TClass* ReferencedMetaSound : Context->LoadSelectedObjects<TClass>())
				{
					FString PackagePath;
					FString AssetName;
					UObject* NewMetaSound = nullptr;

					IAssetTools::Get().CreateUniqueAssetName(ReferencedMetaSound->GetOutermost()->GetName(), TEXT("_Preset"), PackagePath, AssetName);

					Metasound::Editor::IMetasoundEditorModule& MetaSoundEditorModule = FModuleManager::GetModuleChecked<Metasound::Editor::IMetasoundEditorModule>("MetaSoundEditor");
					if (MetaSoundEditorModule.IsRestrictedMode())
					{
						// Cannot duplicate cooked assets in restricted mode, so create new object and copy over properties
						// for sources in InitAsset below. Since copying properties is done manually, 
						// SetSoundWaveSettingsFromTemplate may need to be updated with properties to be copied. 
						UMetaSoundBaseFactory* Factory = NewObject<TFactoryClass>();
						Factory->ReferencedMetaSoundObject = ReferencedMetaSound;

						NewMetaSound = IAssetTools::Get().CreateAssetWithDialog(AssetName, FPackageName::GetLongPackagePath(PackagePath), Factory->GetSupportedClass(), Factory);
					}
					else
					{
						// Duplicate asset to preserve properties from referenced asset (ex. quality settings, soundwave properties)
						NewMetaSound = IAssetTools::Get().DuplicateAssetWithDialogAndTitle(AssetName, FPackageName::GetLongPackagePath(PackagePath), ReferencedMetaSound, LOCTEXT("CreateMetaSoundPresetTitle", "Create MetaSound Preset"));
					}
				
					if (NewMetaSound)
					{
						UMetaSoundEditorSubsystem::GetChecked().InitAsset(*NewMetaSound, ReferencedMetaSound, /*bClearDocument=*/true);

						FGraphBuilder::RegisterGraphWithFrontend(*NewMetaSound);
						ObjectsToSync.Add(NewMetaSound);
					}
					else
					{
						UE_LOG(LogMetaSound, Display, TEXT("Error creating new asset when creating preset '%s' or asset creation was canceled by user."), *AssetName);
					}
				}

				// Sync content browser to newly created valid assets
				// Assets can be invalid if multiple assets are created with the same name 
				// then force overwritten within the same operation
				ObjectsToSync.RemoveAllSwap([](const UObject* InObject)
				{
					return !InObject || !InObject->IsValidLowLevelFast();
				});

				if (ObjectsToSync.Num() > 0)
				{
					FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
					ContentBrowserModule.Get().SyncBrowserToAssets(ObjectsToSync);
					
					// Save happened in Create/Duplicate asset calls above but
					// need to save again after register graph with frontend (which applies preset transform)
					for (UObject* InObject : ObjectsToSync)
					{
						if (ISourceControlModule::Get().IsEnabled())
						{
							constexpr bool bCheckDirty = false;
							constexpr bool bPromptToSave = false;
							TArray<UPackage*> OutermostPackagesToSave;
							OutermostPackagesToSave.Add(InObject->GetOutermost());
							FEditorFileUtils::PromptForCheckoutAndSave(OutermostPackagesToSave, bCheckDirty, bPromptToSave);
						}
					};
				}
			}
		}

		bool IsPreset(const FToolMenuContext& InContext)
		{
			using namespace Metasound::Frontend;

			if (const UContentBrowserAssetContextMenuContext* CBContext = UContentBrowserAssetContextMenuContext::FindContextWithAssets(InContext))
			{
				if (CBContext->SelectedAssets.Num() == 1)
				{
					return FMetaSoundAssetClassInfo(CBContext->SelectedAssets.Last()).DocInfo.bIsPreset;
				}
			}

			return false;
		}

		void AddMetaSoundActions(const FSlateIcon& AssetIcon, const UClass& Class, FToolUIAction CreatePresetAction)
		{
			const FText ClassName = Class.GetDisplayNameText();

			const UAssetDefinition* AssetDefinition = UAssetDefinitionRegistry::Get()->GetAssetDefinitionForClass(&Class);
			FToolMenuSection* Section = CastChecked<UAssetDefinition_SoundAssetBase>(AssetDefinition)->FindSoundContextMenuSection("MetaSound");
			check(Section);

			Section->AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([CreateAction = MoveTemp(CreatePresetAction), ClassName, AssetIcon](FToolMenuSection& InSection)
			{
				using namespace Metasound::Editor;

				{
					const TAttribute<FText> Label = FText::Format(LOCTEXT("MetaSound_CreatePreset", "Create Preset..."), ClassName);
					const TAttribute<FText> ToolTip = FText::Format(LOCTEXT("MetaSound_CreatePresetToolTipFormat", "Creates a {0} Preset using the selected MetaSound as a reference (parent)."), ClassName);

					InSection.AddMenuEntry("MetaSound_CreatePreset", Label, ToolTip, AssetIcon, CreateAction);
				}
				{
					const TAttribute<FText> Label = LOCTEXT("MetaSound_BrowseToPresetParentFormat", "Browse To Preset Parent");
					const TAttribute<FText> ToolTip = FText::Format(LOCTEXT("MetaSound_BrowseToPresetParentToolTipFormat", "Browses to the selected {0} preset's referenced parent asset in the content browser."), ClassName);
					const FSlateIcon FindInContentBrowserIcon(FAppStyle::GetAppStyleSetName(), "SystemWideCommands.FindInContentBrowser");

					FToolUIAction UIAction;
					UIAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&AssetDefinitionsPrivate::ExecuteBrowseToPresetParentAsset);
					UIAction.CanExecuteAction = FToolMenuCanExecuteAction::CreateStatic(&AssetDefinitionsPrivate::IsPreset);
					UIAction.IsActionVisibleDelegate = FToolMenuIsActionButtonVisible::CreateStatic(&AssetDefinitionsPrivate::IsPreset);
					InSection.AddMenuEntry("MetaSoundSource_BrowseToPresetParent", Label, ToolTip, FindInContentBrowserIcon, UIAction);
				}

				IMetasoundEditorModule& EditorModule = FModuleManager::GetModuleChecked<Metasound::Editor::IMetasoundEditorModule>("MetaSoundEditor");
				if (!EditorModule.IsRestrictedMode())
				{
					const TAttribute<FText> Label = LOCTEXT("MetaSound_EditPresetParentFormat", "Edit Preset Parent...");
					const TAttribute<FText> ToolTip = FText::Format(LOCTEXT("MetaSound_OpenPresetParentToolTipFormat", "Opens the selected {0} preset's parent MetaSound asset."), ClassName);

					FToolUIAction UIAction;
					UIAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&AssetDefinitionsPrivate::ExecuteOpenPresetParentAsset);
					UIAction.CanExecuteAction = FToolMenuCanExecuteAction::CreateStatic(&AssetDefinitionsPrivate::IsPreset);
					UIAction.IsActionVisibleDelegate = FToolMenuIsActionButtonVisible::CreateStatic(&AssetDefinitionsPrivate::IsPreset);
					InSection.AddMenuEntry("MetaSoundSource_OpenToPresetParent", Label, ToolTip, FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"), UIAction);
				}
			}));
		}

		void AddMetaSoundContextMenuAction(const UClass& MetaSoundClass, TArray<FToolMenuSection*>& OutSections)
		{
			const UAudioEditorSettings* EdSettings = GetDefault<UAudioEditorSettings>();
			check(EdSettings);

			auto GetSection = [&](FName SectionName) -> FToolMenuSection*
			{
				auto MatchesName = [&SectionName](const FToolMenuSection* Section)
				{
					return Section->Name == SectionName;
				};
				if (FToolMenuSection** SectionPtr = OutSections.FindByPredicate(MatchesName))
				{
					check(*SectionPtr);
					return *SectionPtr;
				}

				return nullptr;
			};

			FToolMenuSection* PlaybackSection = GetSection("Playback");
			FToolMenuSection* SoundSection = GetSection("Sound");

			UToolMenu* Menu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(&MetaSoundClass);
			check(Menu);

			const UEnum* EnumClass = StaticEnum<EToolMenuInsertType>();
			check(EnumClass);
			const EToolMenuInsertType InsertType = static_cast<EToolMenuInsertType>(EnumClass->GetValueByName(EdSettings->MenuPosition));

			FToolMenuSection& MetaSoundSection = Menu->FindOrAddSection("MetaSound");
			MetaSoundSection.Label = MetaSoundClass.GetDisplayNameText();

			if (SoundSection)
			{
				if (PlaybackSection)
				{
					MetaSoundSection.InsertPosition = FToolMenuInsert(
						PlaybackSection->Name,
						InsertType == EToolMenuInsertType::Last
						? EToolMenuInsertType::Before
						: EToolMenuInsertType::After);

					SoundSection->InsertPosition = FToolMenuInsert(
						MetaSoundSection.Name,
						InsertType == EToolMenuInsertType::Last
							? EToolMenuInsertType::Before
							: EToolMenuInsertType::After);
				}
				else
				{
					MetaSoundSection.InsertPosition = FToolMenuInsert({ }, InsertType);
					SoundSection->InsertPosition = FToolMenuInsert(
						MetaSoundSection.Name,
						InsertType == EToolMenuInsertType::Last
						? EToolMenuInsertType::Before
						: EToolMenuInsertType::After);
				}
			}
			else
			{
				MetaSoundSection.InsertPosition = FToolMenuInsert({ }, InsertType);
			}

			OutSections.Add(&MetaSoundSection);
		}
	} // namespace AssetDefinitionsPrivate
} // namespace Metasound::Editor


FLinearColor UAssetDefinition_MetaSoundPatch::GetAssetColor() const
{
	if (const ISlateStyle* MetasoundStyle = FSlateStyleRegistry::FindSlateStyle("MetaSoundStyle"))
	{
		return MetasoundStyle->GetColor("MetaSoundPatch.Color").ToFColorSRGB();
	}

	return FColor::White;
}

TSoftClassPtr<UObject> UAssetDefinition_MetaSoundPatch::GetAssetClass() const
{
	return UMetaSoundPatch::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_MetaSoundPatch::GetAssetCategories() const
{
	static const auto Pinned_Categories = { EAssetCategoryPaths::Audio };
	static const auto Categories = { EAssetCategoryPaths::Audio / LOCTEXT("AssetSoundMetaSoundsSubMenu", "MetaSounds") };

	if (GetDefault<UMetasoundEditorSettings>()->bPinMetaSoundPatchInAssetMenu)
	{
		return Pinned_Categories;
	}

	return Categories;
}

EAssetCommandResult UAssetDefinition_MetaSoundPatch::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	Metasound::Editor::IMetasoundEditorModule& MetaSoundEditorModule = FModuleManager::GetModuleChecked<Metasound::Editor::IMetasoundEditorModule>("MetaSoundEditor");
	if (!MetaSoundEditorModule.IsRestrictedMode())
	{
		for (UMetaSoundPatch* Metasound : OpenArgs.LoadObjects<UMetaSoundPatch>())
		{
			TSharedRef<Metasound::Editor::FEditor> NewEditor = MakeShared<Metasound::Editor::FEditor>();
			NewEditor->InitMetasoundEditor(OpenArgs.GetToolkitMode(), OpenArgs.ToolkitHost, Metasound);
		}
	}
	return EAssetCommandResult::Handled;
}

const FSlateBrush* UAssetDefinition_MetaSoundPatch::GetThumbnailBrush(const FAssetData& InAssetData, const FName InClassName) const
{
	using namespace Metasound::Editor;
	constexpr bool bIsThumbnail = true;
	return AssetDefinitionsPrivate::GetClassBrush(InAssetData, InClassName, bIsThumbnail);
}

const FSlateBrush* UAssetDefinition_MetaSoundPatch::GetIconBrush(const FAssetData& InAssetData, const FName InClassName) const
{
	using namespace Metasound::Editor;
	return AssetDefinitionsPrivate::GetClassBrush(InAssetData, InClassName);
}

TArray<FToolMenuSection*> UAssetDefinition_MetaSoundPatch::RebuildSoundContextMenuSections() const
{
	using namespace Metasound::Editor;

	TArray<FToolMenuSection*> Sections = Super::RebuildSoundContextMenuSections();
	if (const UClass* MetaSoundClass = GetAssetClass().Get())
	{
		AssetDefinitionsPrivate::AddMetaSoundContextMenuAction(*MetaSoundClass, Sections);
	}
	return Sections;
}

FLinearColor UAssetDefinition_MetaSoundSource::GetAssetColor() const
{
 	if (const ISlateStyle* MetasoundStyle = FSlateStyleRegistry::FindSlateStyle("MetaSoundStyle"))
 	{
 		return MetasoundStyle->GetColor("MetaSoundSource.Color").ToFColorSRGB();
 	}
 
 	return FColor::White;
}

TSoftClassPtr<UObject> UAssetDefinition_MetaSoundSource::GetAssetClass() const
{
	return UMetaSoundSource::StaticClass();
}

TConstArrayView<FAssetCategoryPath> UAssetDefinition_MetaSoundSource::GetAssetCategories() const
{
	static const auto Pinned_Categories = { EAssetCategoryPaths::Audio };
	static const auto Categories = { EAssetCategoryPaths::Audio / LOCTEXT("AssetSoundMetaSoundSourceSubMenu", "MetaSounds") };

	if (GetDefault<UMetasoundEditorSettings>()->bPinMetaSoundSourceInAssetMenu)
	{
		return Pinned_Categories;
	}

	return Categories;
}

EAssetCommandResult UAssetDefinition_MetaSoundSource::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	using namespace Metasound;

	for (const FAssetData& AssetData : OpenArgs.Assets)
	{
		const UClass* AssetClass = AssetData.GetClass();
		if (AssetClass && IMetasoundUObjectRegistry::Get().IsRegisteredClass(*AssetClass))
		{
			Engine::FMetaSoundAssetManager& AssetManager = Engine::FMetaSoundAssetManager::GetChecked();
			TWeakPtr<IToolkitHost> ToolkitHost = OpenArgs.ToolkitHost;
			const bool bHostNull = !OpenArgs.ToolkitHost.IsValid();
			AssetManager.AddOrLoadAndUpdateFromObjectAsync(AssetData, [ToolkitMode = OpenArgs.GetToolkitMode(), bHostNull, ToolkitHost](FMetaSoundAssetKey, UObject& MetaSoundObject)
			{
				TSharedPtr<IToolkitHost> HostPtr = ToolkitHost.Pin();
				if (bHostNull || HostPtr)
				{
					Editor::IMetasoundEditorModule* EditorModule = FModuleManager::GetModulePtr<Editor::IMetasoundEditorModule>("MetaSoundEditor");
					if (EditorModule)
					{
						if (EditorModule->IsRestrictedMode())
						{
							TScriptInterface<const IMetaSoundDocumentInterface> DocInterface(&MetaSoundObject);
							check(DocInterface.GetObject());
							const Frontend::FMetaSoundAssetClassInfo ClassInfo(*DocInterface.GetInterface());
							if (!ClassInfo.bIsValid || !ClassInfo.DocInfo.bIsPreset)
							{
								return;
							}
						}

						TSharedRef<Editor::FEditor> NewEditor = MakeShared<Editor::FEditor>();
						NewEditor->InitMetasoundEditor(ToolkitMode, HostPtr, &MetaSoundObject);
					}
				}
			});
		}
	}

	return EAssetCommandResult::Handled;
}

const FSlateBrush* UAssetDefinition_MetaSoundSource::GetThumbnailBrush(const FAssetData& InAssetData, const FName InClassName) const
{
	using namespace Metasound::Editor;
	constexpr bool bIsThumbnail = true;
	return AssetDefinitionsPrivate::GetClassBrush(InAssetData, InClassName, bIsThumbnail);
}

const FSlateBrush* UAssetDefinition_MetaSoundSource::GetIconBrush(const FAssetData& InAssetData, const FName InClassName) const
{
	using namespace Metasound::Editor;
	return AssetDefinitionsPrivate::GetClassBrush(InAssetData, InClassName);
}

void UAssetDefinition_MetaSoundSource::ExecutePlaySound(const FToolMenuContext& InContext)
{
	if (UMetaSoundSource* MetaSoundSource = UContentBrowserAssetContextMenuContext::LoadSingleSelectedAsset<UMetaSoundSource>(InContext))
	{
		// If editor is open, call into it to play to start all visualization requirements therein
		// specific to auditioning MetaSounds (ex. priming audio bus used for volume metering, playtime
		// widget, etc.)
		TSharedPtr<Metasound::Editor::FEditor> Editor = Metasound::Editor::FGraphBuilder::GetEditorForMetasound(*MetaSoundSource);
		if (Editor.IsValid())
		{
			Editor->Play();
			return;
		}

		Metasound::Editor::FGraphBuilder::FGraphBuilder::RegisterGraphWithFrontend(*MetaSoundSource);
		UAssetDefinition_SoundBase::ExecutePlaySound(InContext);
	}
}

void UAssetDefinition_MetaSoundSource::ExecuteStopSound(const FToolMenuContext& InContext)
{
	if (UMetaSoundSource* MetaSoundSource = UContentBrowserAssetContextMenuContext::LoadSingleSelectedAsset<UMetaSoundSource>(InContext))
	{
		TSharedPtr<Metasound::Editor::FEditor> Editor = Metasound::Editor::FGraphBuilder::GetEditorForMetasound(*MetaSoundSource);
		if (Editor.IsValid())
		{
			Editor->Stop();
			return;
		}

		UAssetDefinition_SoundBase::ExecuteStopSound(InContext);
	}
}

bool UAssetDefinition_MetaSoundSource::CanExecutePlayCommand(const FToolMenuContext& InContext)
{
	return UAssetDefinition_SoundBase::CanExecutePlayCommand(InContext);
}

ECheckBoxState UAssetDefinition_MetaSoundSource::IsActionCheckedMute(const FToolMenuContext& InContext)
{
	return UAssetDefinition_SoundBase::IsActionCheckedMute(InContext);
}

ECheckBoxState UAssetDefinition_MetaSoundSource::IsActionCheckedSolo(const FToolMenuContext& InContext)
{
	return UAssetDefinition_SoundBase::IsActionCheckedSolo(InContext);
}

void UAssetDefinition_MetaSoundSource::ExecuteMuteSound(const FToolMenuContext& InContext)
{
	UAssetDefinition_SoundBase::ExecuteMuteSound(InContext);
}

void UAssetDefinition_MetaSoundSource::ExecuteSoloSound(const FToolMenuContext& InContext)
{
	UAssetDefinition_SoundBase::ExecuteSoloSound(InContext);
}

bool UAssetDefinition_MetaSoundSource::CanExecuteMuteCommand(const FToolMenuContext& InContext)
{
	return UAssetDefinition_SoundBase::CanExecuteMuteCommand(InContext);
}

bool UAssetDefinition_MetaSoundSource::CanExecuteSoloCommand(const FToolMenuContext& InContext)
{
	return UAssetDefinition_SoundBase::CanExecuteSoloCommand(InContext);
}

TSharedPtr<SWidget> UAssetDefinition_MetaSoundSource::GetThumbnailOverlay(const FAssetData& InAssetData) const
{
	auto OnClickedLambdaOverride = [InAssetData]() -> FReply
	{
		TSharedPtr<Metasound::Editor::FEditor> Editor = Metasound::Editor::FGraphBuilder::GetEditorForMetasound(*InAssetData.GetAsset());
		if (UE::AudioEditor::IsSoundPlaying(InAssetData))
		{
			if (Editor.IsValid())
			{
				Editor->Stop();
			}
			else
			{
				UE::AudioEditor::StopSound();
			}
		}
		else
		{
			if (Editor.IsValid())
			{
				Editor->Play();
			}
			else
			{
				// Load and play sound
				UE::AudioEditor::PlaySound(Cast<USoundBase>(InAssetData.GetAsset()));
			}
		}
		return FReply::Handled();
	};
	return UAssetDefinition_SoundBase::GetSoundBaseThumbnailOverlay(InAssetData, MoveTemp(OnClickedLambdaOverride));
}

bool UAssetDefinition_MetaSoundSource::GetThumbnailActionOverlay(const FAssetData& InAssetData, FAssetActionThumbnailOverlayInfo& OutActionOverlayInfo) const
{
	auto OnGetDisplayBrushLambda = [InAssetData]() -> const FSlateBrush*
	{
		if (UE::AudioEditor::IsSoundPlaying(InAssetData))
		{
			return FAppStyle::GetBrush("ContentBrowser.AssetAction.StopIcon");
		}

		return FAppStyle::GetBrush("ContentBrowser.AssetAction.PlayIcon");
	};

	OutActionOverlayInfo.ActionImageWidget = SNew(SImage).Image_Lambda(OnGetDisplayBrushLambda);

	auto OnToolTipTextLambda = [InAssetData]() -> FText
	{
		if (UE::AudioEditor::IsSoundPlaying(InAssetData))
		{
			return LOCTEXT("Thumbnail_StopSoundToolTip", "Stop selected sound");
		}

		return LOCTEXT("Thumbnail_PlaySoundToolTip", "Play selected sound");
	};

	auto OnClickedLambda = [InAssetData]() -> FReply
	{
		TSharedPtr<Metasound::Editor::FEditor> Editor = Metasound::Editor::FGraphBuilder::GetEditorForMetasound(*InAssetData.GetAsset());
		if (UE::AudioEditor::IsSoundPlaying(InAssetData))
		{
			if (Editor.IsValid())
			{
				Editor->Stop();
			}
			else
			{
				UE::AudioEditor::StopSound();
			}
		}
		else
		{
			if (Editor.IsValid())
			{
				Editor->Play();
			}
			else
			{
				// Load and play sound
				UE::AudioEditor::PlaySound(Cast<USoundBase>(InAssetData.GetAsset()));
			}
		}
		return FReply::Handled();
	};

	OutActionOverlayInfo.ActionButtonArgs = SButton::FArguments()
		.ToolTipText_Lambda(OnToolTipTextLambda)
		.OnClicked_Lambda(OnClickedLambda);

	return true;
}

EAssetCommandResult UAssetDefinition_MetaSoundSource::ActivateAssets(const FAssetActivateArgs& ActivateArgs) const
{
	if (ActivateArgs.ActivationMethod == EAssetActivationMethod::Previewed)
	{
		if (UMetaSoundSource* MetaSoundSource = ActivateArgs.LoadFirstValid<UMetaSoundSource>())
		{
			TSharedPtr<Metasound::Editor::FEditor> Editor = Metasound::Editor::FGraphBuilder::GetEditorForMetasound(*MetaSoundSource);
			UAudioComponent* PreviewComp = GEditor->GetPreviewAudioComponent();
	
			// If the editor is open, we need to stop or start the editor so it can light up while previewing it in the CB
			if (Editor.IsValid())
			{
				if (PreviewComp && PreviewComp->IsPlaying())
				{
					if (!MetaSoundSource || PreviewComp->Sound == MetaSoundSource)
					{
						Editor->Stop();
					}
				}
				else
				{
					Editor->Play();
				}

				return EAssetCommandResult::Handled;
			}
			else
			{
				return UAssetDefinition_SoundBase::ActivateSoundBase(ActivateArgs);
			}
		}
	}
	return EAssetCommandResult::Unhandled;
}

void UAssetDefinition_MetaSoundSource::GetAssetActionButtonExtensions(const FAssetData& InAssetData, TArray<FAssetButtonActionExtension>& OutExtensions) const
{
	UAssetDefinition_SoundBase::GetSoundBaseAssetActionButtonExtensions(InAssetData, OutExtensions);
}

TArray<FToolMenuSection*> UAssetDefinition_MetaSoundSource::RebuildSoundContextMenuSections() const
{
	using namespace Metasound::Editor;

	TArray<FToolMenuSection*> Sections = Super::RebuildSoundContextMenuSections();
	if (const UClass* MetaSoundClass = GetAssetClass().Get())
	{
		AssetDefinitionsPrivate::AddMetaSoundContextMenuAction(*MetaSoundClass, Sections);
	}
	return Sections;
}


namespace MenuExtension_MetaSoundSourceTemplate
{
 	static FDelayedAutoRegisterHelper DelayedAutoRegister(EDelayedRegisterRunPhase::EndOfEngineInit, []
	{
		if (!UToolMenus::IsToolMenuUIEnabled())
		{
			return;
		}

 		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([]()
 		{
			using namespace Metasound::Editor;

 			FToolMenuOwnerScoped OwnerScoped(UE_MODULE_NAME);

			{
				const UClass* MetaSoundClass = UMetaSoundSource::StaticClass();
				check(MetaSoundClass);
				FToolUIAction CreateAction;
				CreateAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&AssetDefinitionsPrivate::ExecuteCreateMetaSoundPreset<UMetaSoundSource, UMetaSoundSourceFactory>);
				const FSlateIcon AssetIcon = Metasound::Editor::Style::CreateSlateIcon("ClassIcon.MetasoundSource");
				AssetDefinitionsPrivate::AddMetaSoundActions(AssetIcon, *MetaSoundClass, MoveTemp(CreateAction));
				{
					const UAssetDefinition* AssetDefinition = UAssetDefinitionRegistry::Get()->GetAssetDefinitionForClass(MetaSoundClass);
					FToolMenuSection* PlaybackSection = CastChecked<UAssetDefinition_SoundAssetBase>(AssetDefinition)->FindSoundContextMenuSection("Playback");
					check(PlaybackSection);

					PlaybackSection->AddDynamicEntry(NAME_None, FNewToolMenuSectionDelegate::CreateLambda([AssetIcon](FToolMenuSection& InSection)
					{
						auto IsPlayingThis = [](const FToolMenuContext& InContext) { return UAssetDefinition_SoundBase::IsPlayingContextAsset(InContext, true /* bMustMatchContext */); };
						auto IsNotPlayingThis = [](const FToolMenuContext& InContext) { return !UAssetDefinition_SoundBase::IsPlayingContextAsset(InContext, true /* bMustMatchContext */); };
						auto IsNotPlayingAny = [](const FToolMenuContext& InContext) { return !UAssetDefinition_SoundBase::IsPlayingContextAsset(InContext, false /* bMustMatchContext */); };
						auto IsPlayingAny = [](const FToolMenuContext& InContext) { return UAssetDefinition_SoundBase::IsPlayingContextAsset(InContext, false /* bMustMatchContext */); };
						auto IsPlayingOther = [](const FToolMenuContext& InContext) { return UAssetDefinition_SoundBase::IsPlayingContextAsset(InContext, false /* bMustMatchContext */) && !UAssetDefinition_SoundBase::IsPlayingContextAsset(InContext, true /* bMustMatchContext */); };

						{
							const TAttribute<FText> Label = LOCTEXT("Sound_PlaySound" , "Play");
							const TAttribute<FText> ToolTip = LOCTEXT("Sound_PlaySoundTooltip", "Plays the selected sound.");
							const FSlateIcon Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "MediaAsset.AssetActions.Play.Small");

							FToolUIAction UIAction;
							UIAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&UAssetDefinition_MetaSoundSource::ExecutePlaySound);
							UIAction.CanExecuteAction = FToolMenuCanExecuteAction::CreateStatic(&UAssetDefinition_MetaSoundSource::CanExecutePlayCommand);
							UIAction.IsActionVisibleDelegate = FToolMenuIsActionButtonVisible::CreateLambda(IsNotPlayingThis);
							InSection.AddMenuEntry("Sound_PlaySound", Label, ToolTip, Icon, UIAction);
						}
						{
							const TAttribute<FText> Label = LOCTEXT("Sound_RestartSound", "Restart");
							const TAttribute<FText> ToolTip = LOCTEXT("Sound_RestartSoundTooltip", "Restarts the selected sound.");
							const FSlateIcon Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "Cascade.RestartInLevel.Small");

							FToolUIAction UIAction;
							UIAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&UAssetDefinition_MetaSoundSource::ExecutePlaySound);
							UIAction.CanExecuteAction = FToolMenuCanExecuteAction::CreateStatic(&UAssetDefinition_MetaSoundSource::CanExecutePlayCommand);
							UIAction.IsActionVisibleDelegate = FToolMenuIsActionButtonVisible::CreateLambda(IsPlayingThis);
							InSection.AddMenuEntry("Sound_RestartSound", Label, ToolTip, Icon, UIAction);
						}

						{ // Stop
							const FSlateIcon Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "MediaAsset.AssetActions.Stop.Small");
							{ // Selected
								const TAttribute<FText> StopSelectedToolTip = LOCTEXT("Sound_StopSoundTooltip", "Stops the selected sound.");
								{
									const TAttribute<FText> Label = LOCTEXT("Sound_StopSoundDisabled", "Stop");

									FToolUIAction UIAction;
									UIAction.CanExecuteAction = FToolMenuCanExecuteAction::CreateLambda(IsPlayingThis);
									UIAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&UAssetDefinition_MetaSoundSource::ExecuteStopSound);
									UIAction.IsActionVisibleDelegate = FToolMenuIsActionButtonVisible::CreateLambda([IsNotPlayingAny, IsPlayingThis](const FToolMenuContext& Context) { return IsNotPlayingAny(Context) || IsPlayingThis(Context); });
									InSection.AddMenuEntry("Sound_StopSound", Label, StopSelectedToolTip, Icon, UIAction);
								}
							}
							{ // Other
								const TAttribute<FText> Label = TAttribute<FText>::CreateLambda([]()
								{
									if (const USoundBase* OtherSound = UAssetDefinition_SoundBase::GetPlayingSound())
									{
										return FText::Format(LOCTEXT("Sound_StopSoundOtherFormat", "Stop ({0})"), FText::FromName(OtherSound->GetFName()));
									}
									return LOCTEXT("Sound_StopSoundOther", "Stop (Other)");
								});
								const TAttribute<FText> ToolTip = LOCTEXT("Sound_StopOtherSoundTooltip", "Stops the currently previewing (other) sound.");
								FToolUIAction UIAction;
								UIAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&UAssetDefinition_MetaSoundSource::ExecuteStopSound);
								UIAction.IsActionVisibleDelegate = FToolMenuIsActionButtonVisible::CreateLambda(IsPlayingOther);
								InSection.AddMenuEntry("Sound_StopOtherSound", Label, ToolTip, Icon, UIAction);
							}
						}

						{
							const TAttribute<FText> Label = LOCTEXT("Sound_MuteSound", "Mute");
							const TAttribute<FText> ToolTip = LOCTEXT("Sound_MuteSoundTooltip", "Mutes the selected sounds.");
							const FSlateIcon Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "MediaAsset.AssetActions.Mute.Small");
				
							FToolUIAction UIAction;
							UIAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&UAssetDefinition_MetaSoundSource::ExecuteMuteSound);
							UIAction.CanExecuteAction = FToolMenuCanExecuteAction::CreateStatic(&UAssetDefinition_MetaSoundSource::CanExecuteMuteCommand);
							UIAction.GetActionCheckState = FToolMenuGetActionCheckState::CreateStatic(&UAssetDefinition_MetaSoundSource::IsActionCheckedMute);
							InSection.AddMenuEntry("Sound_SoundMute", Label, ToolTip, Icon, UIAction, EUserInterfaceActionType::ToggleButton);
						}
						{
							const TAttribute<FText> Label = LOCTEXT("Sound_SoloSound", "Solo");
							const TAttribute<FText> ToolTip = LOCTEXT("Sound_SoloSoundTooltip", "Solos the selected sounds.");
							const FSlateIcon Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "MediaAsset.AssetActions.Solo.Small");
				
							FToolUIAction UIAction;
							UIAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&UAssetDefinition_MetaSoundSource::ExecuteSoloSound);
							UIAction.CanExecuteAction = FToolMenuCanExecuteAction::CreateStatic(&UAssetDefinition_MetaSoundSource::CanExecuteSoloCommand);
							UIAction.GetActionCheckState = FToolMenuGetActionCheckState::CreateStatic(&UAssetDefinition_MetaSoundSource::IsActionCheckedSolo);
							InSection.AddMenuEntry("Sound_StopSolo", Label, ToolTip, Icon, UIAction, EUserInterfaceActionType::ToggleButton);
						}
						{
							const TAttribute<FText> Label = LOCTEXT("Sound_ClearMutedSoloed", "Clear Muted/Soloed");
							const TAttribute<FText> ToolTip = LOCTEXT("Sound_ClearMutedSoloedTooltip", "Clear all flags to mute/solo specific assets.");
							const FSlateIcon Icon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "PropertyWindow.DiffersFromDefault");

							FToolUIAction UIAction;
							UIAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&UAssetDefinition_SoundBase::ExecuteClearMutesAndSolos);
							UIAction.IsActionVisibleDelegate = FToolMenuCanExecuteAction::CreateStatic(&UAssetDefinition_SoundBase::CanExecuteClearMutesAndSolos);
							InSection.AddMenuEntry("Sound_ClearMuteSoloSettings", Label, ToolTip, Icon, UIAction);
						}
					}));
				}
 			}
	
			{
				const UClass* MetaSoundClass = UMetaSoundPatch::StaticClass();
				check(MetaSoundClass);
				FToolUIAction CreateAction;
				CreateAction.ExecuteAction = FToolMenuExecuteAction::CreateStatic(&AssetDefinitionsPrivate::ExecuteCreateMetaSoundPreset<UMetaSoundPatch, UMetaSoundFactory>);
				const FSlateIcon AssetIcon = Metasound::Editor::Style::CreateSlateIcon("ClassIcon.MetasoundPatch");
				AssetDefinitionsPrivate::AddMetaSoundActions(AssetIcon, *MetaSoundClass, MoveTemp(CreateAction));
			}
 		}));
	});
} // namespace MenuExtension_MetaSoundSourceTemplate
#undef LOCTEXT_NAMESPACE //MetaSoundEditor
