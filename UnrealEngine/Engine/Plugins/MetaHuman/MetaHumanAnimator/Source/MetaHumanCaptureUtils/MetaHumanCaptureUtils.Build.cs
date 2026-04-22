// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanCaptureUtils : ModuleRules
{
	public MetaHumanCaptureUtils(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.AddRange(new string[] 
		{
			// ... add public include paths required here ...
        });


        PrivateIncludePaths.AddRange(new string[] 
		{
			// ... add other private include paths required here ...
		});

        PublicDependencyModuleNames.AddRange(new string[]
		{
            "Core",
			"CoreUObject",
			"Engine",
        });

		PrivateDependencyModuleNames.AddRange(new string[]
		{
        });
	}
}
