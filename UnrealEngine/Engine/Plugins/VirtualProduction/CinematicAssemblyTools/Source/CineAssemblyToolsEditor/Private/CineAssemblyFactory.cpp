// Copyright Epic Games, Inc. All Rights Reserved.

#include "CineAssemblyFactory.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetViewUtils.h"
#include "AssetToolsModule.h"
#include "CineAssemblyNamingTokens.h"
#include "CineAssemblyToolsAnalytics.h"
#include "ContentBrowserModule.h"
#include "Framework/Application/SlateApplication.h"
#include "IContentBrowserSingleton.h"
#include "Interfaces/IMainFrameModule.h"
#include "Modules/ModuleManager.h"
#include "MovieScene.h"
#include "MovieSceneToolsProjectSettings.h"
#include "UI/CineAssembly/SCineAssemblyConfigWindow.h"

#define LOCTEXT_NAMESPACE "CineAssemblyFactory"

UCineAssemblyFactory::UCineAssemblyFactory()
{
	SupportedClass = UCineAssembly::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

bool UCineAssemblyFactory::CanCreateNew() const
{
	return true;
}

bool UCineAssemblyFactory::ConfigureProperties()
{
	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
	const FString CurrentPath = ContentBrowser.GetCurrentPath().GetInternalPathString();

	TSharedRef<SCineAssemblyConfigWindow> CineAssemblyConfigWindow = SNew(SCineAssemblyConfigWindow, CurrentPath);

	const IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
	const TSharedPtr<SWindow> ParentWindow = MainFrameModule.GetParentWindow();

	if (ParentWindow.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(CineAssemblyConfigWindow, ParentWindow.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(CineAssemblyConfigWindow);
	}

	return false;
}

UObject* UCineAssemblyFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	// Procedural assembly creation that does not use the configuration window will hit this path
	UCineAssembly* NewAssembly = NewObject<UCineAssembly>(InParent, Name, Flags);
	NewAssembly->Initialize();

	if (UMovieScene* MovieScene = NewAssembly->GetMovieScene())
	{
		const UMovieSceneToolsProjectSettings* MovieSceneToolsProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();

		const FFrameRate TickResolution = MovieScene->GetTickResolution();
		const FFrameNumber DefaultStartFrame = (MovieSceneToolsProjectSettings->DefaultStartTime * TickResolution).FloorToFrame();
		const int32 DefaultDuration = (MovieSceneToolsProjectSettings->DefaultDuration * TickResolution).FloorToFrame().Value;
		MovieScene->SetPlaybackRange(DefaultStartFrame, DefaultDuration);
	}

	return NewAssembly;
}

FString UCineAssemblyFactory::MakeAssemblyPackageName(UCineAssembly* ConfiguredAssembly, const FString& CreateAssetPath)
{
	// Resolve the default assembly path
	FString DefaultAssemblyPath;
	if (const UCineAssemblySchema* Schema = ConfiguredAssembly->GetSchema())
	{
		if (!Schema->DefaultAssemblyPath.IsEmpty())
		{
			DefaultAssemblyPath = UCineAssemblyNamingTokens::GetResolvedText(Schema->DefaultAssemblyPath, ConfiguredAssembly).ToString();
		}
	}

	return CreateAssetPath / DefaultAssemblyPath / ConfiguredAssembly->AssemblyName.Resolved.ToString();
}

bool UCineAssemblyFactory::MakeUniqueNameAndPath(UCineAssembly* ConfiguredAssembly, const FString& CreateAssetPath, FString& UniquePackageName, FString& UniqueAssetName)
{
	const FString DesiredPackageName = MakeAssemblyPackageName(ConfiguredAssembly, CreateAssetPath);

	// Ensure the package name length does not exceed the maximum cook path length as this may cause issues later on.
	if (FText OutErrorMessage; !AssetViewUtils::IsValidPackageForCooking(DesiredPackageName, OutErrorMessage))
	{
		return false;
	}

	// Ensure that the resolved assembly name is actually unique
	const FString AssemblyName = ConfiguredAssembly->AssemblyName.Resolved.ToString();
	const FString DesiredSuffix = TEXT("");

	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
	AssetTools.CreateUniqueAssetName(DesiredPackageName, DesiredSuffix, UniquePackageName, UniqueAssetName);

	// If the assembly name was not unique, update the assembly template string
	if (UniqueAssetName != AssemblyName)
	{
		ConfiguredAssembly->AssemblyName.Template = UniqueAssetName;
		ConfiguredAssembly->AssemblyName.Resolved = FText::FromString(UniqueAssetName);

		// It is possible that the default assembly path was dependent on the assembly name (through tokens).
		// By updating the assembly name, we may also need to reevaluate the assembly path.
		// However, once we change the assembly path, we need to verify uniqueness again (in the new path).
		// Therefore, we recursively call this function until we end up with a combination of a unique assembly path and name.
		return MakeUniqueNameAndPath(ConfiguredAssembly, CreateAssetPath, UniquePackageName, UniqueAssetName);
	}

	return true;
}

void UCineAssemblyFactory::CreateConfiguredAssembly(UCineAssembly* ConfiguredAssembly, const FString& CreateAssetPath)
{
	// The intended use of this function is to take a pre-configured, transient assembly, create a valid package for it, and initialize it.
	if (ConfiguredAssembly->GetPackage() != GetTransientPackage())
	{
		return;
	}

	// Evaluate the name of the assembly
	ConfiguredAssembly->AssemblyName.Resolved = UCineAssemblyNamingTokens::GetResolvedText(ConfiguredAssembly->AssemblyName.Template, ConfiguredAssembly);

	// If the assembly name is empty or invalid, assign it a valid default name
	if (ConfiguredAssembly->AssemblyName.Resolved.IsEmpty() || FName(*ConfiguredAssembly->AssemblyName.Resolved.ToString()).IsNone())
	{
		ConfiguredAssembly->AssemblyName.Resolved = LOCTEXT("NewCineAssemblyName", "NewCineAssembly");
		ConfiguredAssembly->AssemblyName.Template = ConfiguredAssembly->AssemblyName.Resolved.ToString();
	}

	FString UniquePackageName;
	FString UniqueAssetName;
	if (!MakeUniqueNameAndPath(ConfiguredAssembly, CreateAssetPath, UniquePackageName, UniqueAssetName))
	{
		return;
	}

	// The input assembly object was created in the transient package while its properties were configured.
	// Now, we can create a real package for it, rename it, and update its object flags.
	UPackage* Package = CreatePackage(*UniquePackageName);
	ConfiguredAssembly->Rename(*UniqueAssetName, Package);

	const EObjectFlags Flags = RF_Public | RF_Standalone | RF_Transactional;
	ConfiguredAssembly->SetFlags(Flags);
	ConfiguredAssembly->ClearFlags(RF_Transient);

	// If the configured assembly already has a MovieScene object (which could happen in case of a duplication), do not overwrite it with a new one
	if (!ConfiguredAssembly->GetMovieScene())
	{
		// if we have a template to build from, setup now
		bool bBuiltFromTemplate = false;
		if (const UCineAssemblySchema* Schema = ConfiguredAssembly->GetSchema())
		{
			if (ULevelSequence* Template = Cast<ULevelSequence>(Schema->Template.TryLoad()))
			{
				ConfiguredAssembly->InitializeFromTemplate(Template);
				// Confirm we have a valid MovieScene built.
				bBuiltFromTemplate = ConfiguredAssembly->GetMovieScene() != nullptr;
			}
		}

		if (!bBuiltFromTemplate)
		{
			// Do the same setup that ULevelSequence assets do when they are created
			ConfiguredAssembly->Initialize();

			if (UMovieScene* MovieScene = ConfiguredAssembly->GetMovieScene())
			{
				const UMovieSceneToolsProjectSettings* MovieSceneToolsProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();

				const FFrameRate TickResolution = MovieScene->GetTickResolution();
				const FFrameNumber DefaultStartFrame = (MovieSceneToolsProjectSettings->DefaultStartTime * TickResolution).FloorToFrame();
				const int32 DefaultDuration = (MovieSceneToolsProjectSettings->DefaultDuration * TickResolution).FloorToFrame().Value;
				MovieScene->SetPlaybackRange(DefaultStartFrame, DefaultDuration);
			}
		}
	}

	// Re-evaluate the assembly's metadata tokens
	if (const UCineAssemblySchema* Schema = ConfiguredAssembly->GetSchema())
	{
		for (const FAssemblyMetadataDesc& MetadataDesc : Schema->AssemblyMetadata)
		{
			if (MetadataDesc.Type == ECineAssemblyMetadataType::String && MetadataDesc.bEvaluateTokens)
			{
				FTemplateString TemplateString;
				if (ConfiguredAssembly->GetMetadataAsTokenString(MetadataDesc.Key, TemplateString))
				{
					TemplateString.Resolved = UCineAssemblyNamingTokens::GetResolvedText(TemplateString.Template, ConfiguredAssembly);
					ConfiguredAssembly->SetMetadataAsTokenString(MetadataDesc.Key, TemplateString);
				}
			}
		}
	}

	ConfiguredAssembly->CreateSubAssemblies();

	// Analytics
	UE::CineAssemblyToolsAnalytics::RecordEvent_CreateAssembly(ConfiguredAssembly);

	// Notify the asset registry about the new assembly
	FAssetRegistryModule::AssetCreated(ConfiguredAssembly);

	// Mark the package dirty
	Package->MarkPackageDirty();

	// Refresh the content browser to make any new assets and folders are immediately visible
	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
	ContentBrowser.SetSelectedPaths({ CreateAssetPath }, true);
}

#undef LOCTEXT_NAMESPACE
