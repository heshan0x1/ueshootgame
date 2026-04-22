// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AudioMotorSimDebug : ModuleRules
{
    public AudioMotorSimDebug(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        
        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
	            "AudioMotorSim",
	            "Core",
                "CoreUObject",
                "Engine",
                "SlateCore",
                "SlateIM"
            }
			
        );
        
        if (Target.Type == TargetType.Editor)
        {
	        PrivateDependencyModuleNames.AddRange(
		        new string[] {
			        "UnrealEd",
		        }
	        );
        }
    }
}