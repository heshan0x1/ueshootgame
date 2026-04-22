// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MetaHumanPlatform : ModuleRules
{
	public MetaHumanPlatform(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
		});

		PrivateDependencyModuleNames.AddRange(new string[] {
			"MeshTrackerInterface",
			"MetaHumanCore",
			"RHI",
		});

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"D3D12RHI",
				"DX12"
			});
		}
	}
}
