// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NaniteAssemblyEditorUtils : ModuleRules
{
    public NaniteAssemblyEditorUtils(ReadOnlyTargetRules Target) : base(Target)
    {
		PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "AssetTools"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "CoreUObject",
                "Engine",
                "UnrealEd",
                "Renderer",
            }
        );

		DynamicallyLoadedModuleNames.AddRange(
            new string[] {
                "AssetTools"
            }
        );
    }
}
