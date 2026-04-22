// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AvalancheDataLinkEditor : ModuleRules
{
    public AvalancheDataLinkEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "AvalancheDataLink",
                "AvalancheInteractiveTools",
                "Core",
                "CoreUObject",
                "Engine",
                "InteractiveToolsFramework",
                "PropertyEditor",
                "Slate",
                "SlateCore",
                "UnrealEd",
            }
        );
    }
}
