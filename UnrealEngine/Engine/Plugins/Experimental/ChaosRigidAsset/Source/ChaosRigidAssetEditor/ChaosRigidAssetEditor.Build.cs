// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ChaosRigidAssetEditor : ModuleRules
{
	public ChaosRigidAssetEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Engine",
				"UnrealEd",
				"SlateCore",
				"Slate",
				"DataflowCore",
				"DataflowEngine",
				"DataflowEditor",
				"ChaosRigidAssetEngine",
				"ChaosRigidAssetNodes",
				"PhysicsAssetEditor"
			}
		);
		
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
			}
		);

		bAllowUETypesInNamespaces = true;

		SetupModulePhysicsSupport(Target);
	}
}
