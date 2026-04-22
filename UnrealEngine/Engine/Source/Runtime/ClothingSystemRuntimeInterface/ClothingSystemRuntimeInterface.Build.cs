// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ClothingSystemRuntimeInterface : ModuleRules
{
	public ClothingSystemRuntimeInterface(ReadOnlyTargetRules Target) : base(Target)
	{
		ShortName = "ClothSysRuntimeIntrfc";

        SetupModulePhysicsSupport(Target);

        PublicDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "CoreUObject"
            }
        );

		if (Target.bBuildEditor || Target.bCompileAgainstEditor)
		{
			PrivateDependencyModuleNames.Add("Slate");  // For editor notifications
		}
	}
}
