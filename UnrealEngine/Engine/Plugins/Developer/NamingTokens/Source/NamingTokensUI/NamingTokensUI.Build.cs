// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NamingTokensUI : ModuleRules
{
    public NamingTokensUI(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "CoreUObject",
                "NamingTokens",
                "UMG"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "Engine",
                "InputCore",
                "Slate",
                "SlateCore"
            }
        );
        
        if (Target.Type == TargetType.Editor)
        {
	        PublicDependencyModuleNames.AddRange(
		        new string[] {
			        "UnrealEd",
		        }
	        );
        }
    }
}