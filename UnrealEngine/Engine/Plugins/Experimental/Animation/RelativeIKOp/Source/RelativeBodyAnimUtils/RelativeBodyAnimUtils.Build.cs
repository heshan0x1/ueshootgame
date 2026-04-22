// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO; // for Path

namespace UnrealBuildTool.Rules
{
	public class RelativeBodyAnimUtils : ModuleRules
	{
		public RelativeBodyAnimUtils(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
			CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;

			PublicIncludePaths.AddRange(
				new string[] {
					// ... add public include paths required here ...
				}
				);


			PrivateIncludePaths.AddRange(
				new string[] {
					// ... add other private include paths required here ...
				}
				);


			PublicDependencyModuleNames.AddRange(
				new string[]
				{
				}
				);


			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
				"Core",
				"CoreUObject",
				"Engine",
				"AnimationCore",
				"MeshDescription",
				"StaticMeshDescription",
				"SkeletalMeshDescription",
				"AnimationModifiers",
				"AnimationBlueprintLibrary",
				"RelativeBodyAnimInfo"
				}
				);


			DynamicallyLoadedModuleNames.AddRange(
				new string[]
				{
					// ... add any modules that your module loads dynamically here ...
				}
				);
		}
	}

}