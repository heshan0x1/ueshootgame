// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeaturesSubsystemSettings.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameFeaturesSubsystemSettings)

const FName UGameFeaturesSubsystemSettings::LoadStateClient(TEXT("Client"));
const FName UGameFeaturesSubsystemSettings::LoadStateServer(TEXT("Server"));

UGameFeaturesSubsystemSettings::UGameFeaturesSubsystemSettings()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	BuiltInGameFeaturePluginsFolder = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir() + TEXT("GameFeatures/"));
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

bool UGameFeaturesSubsystemSettings::IsValidGameFeaturePlugin(const FString& PluginDescriptorFilename) const
{
	// Build the cache of game feature plugin folders the first time this is called
	static struct FBuiltInGameFeaturePluginsFolders
	{
		FBuiltInGameFeaturePluginsFolders()
		{
			const FPaths::EGetExtensionDirsFlags ExtensionFlags =
				FPaths::EGetExtensionDirsFlags::WithBase |
				FPaths::EGetExtensionDirsFlags::WithRestricted;

			// Get all the existing game feature paths
			TArray<FString> RelativePaths = FPaths::GetExtensionDirs(
				FPaths::ProjectDir(), FPaths::Combine(TEXT("Plugins"), TEXT("GameFeatures")), ExtensionFlags);

			// The base directory may not exist yet, add it if empty
			if (RelativePaths.IsEmpty())
			{
				RelativePaths.Add(FPaths::Combine(FPaths::ProjectDir(), TEXT("Plugins"), TEXT("GameFeatures")));
			}

			BuiltInGameFeaturePluginsFolders.Reserve(2 * RelativePaths.Num());
			for (FString& BuiltInFolder : RelativePaths)
			{
				BuiltInFolder /= TEXT(""); // Add trailing slash if needed
				BuiltInGameFeaturePluginsFolders.Add(FPaths::ConvertRelativePathToFull(BuiltInFolder));
				BuiltInGameFeaturePluginsFolders.Add(MoveTemp(BuiltInFolder));
			}
		}

		TArray<FString> BuiltInGameFeaturePluginsFolders;
	} Lazy;

	// Check to see if the filename is rooted in a game feature plugin folder
	for (const FString& BuiltInFolder : Lazy.BuiltInGameFeaturePluginsFolders)
	{
		if (FPathViews::IsParentPathOf(BuiltInFolder, PluginDescriptorFilename))
		{
			return true;
		}
	}

	return false;
}

