// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanBatchProcessor : ModuleRules
{
	public MetaHumanBatchProcessor(ReadOnlyTargetRules Target) : base(Target)
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
			"CoreUObject",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Engine",
			"MetaHumanCore",
			"MetaHumanPerformance",
			"MetaHumanPipelineCore",
			"MetaHumanPipeline",
			"MetaHumanCoreTech",
			"MetaHumanCoreTechLib",
			"MetaHumanSpeech2Face"
		});

		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"AssetDefinition",
				"AudioEditor",
				"SlateCore",
				"Slate",
				"ToolMenus",
				"ContentBrowser",
				"ContentBrowserData",
				"UnrealEd"
			});
		}
	}
}
