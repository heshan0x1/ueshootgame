// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanConfig : ModuleRules
{
	public MetaHumanConfig(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Projects",
			"Slate",
			"SlateCore",
			"PlatformCrypto",
			"PlatformCryptoContext",
			"PlatformCryptoTypes",
			"MetaHumanCaptureData",
			"MeshTrackerInterface",
			"MetaHumanCore",
			"Engine",
			"RigLogicModule",
			"CaptureDataCore",
			"MetaHumanCoreTech",
		});
		
		if (Target.bBuildEditor)
        {
			PrivateDependencyModuleNames.Add("MetaHumanCoreTechLib");
		}
	}
}
