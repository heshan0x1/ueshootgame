// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanCaptureProtocolStack : ModuleRules
{

	public MetaHumanCaptureProtocolStack(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

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
			"MetaHumanCaptureUtils"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
            "Json",
            "JsonUtilities",
            "Sockets",
            "Networking",
            "UnrealEd"
        });
	}
}
