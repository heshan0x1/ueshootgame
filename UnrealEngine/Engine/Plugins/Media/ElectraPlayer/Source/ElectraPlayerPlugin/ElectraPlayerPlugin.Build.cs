// Copyright Epic Games, Inc. All Rights Reserved.
using System.IO;
using UnrealBuildTool;

namespace UnrealBuildTool.Rules
{
	public class ElectraPlayerPlugin: ModuleRules
	{
		public ElectraPlayerPlugin(ReadOnlyTargetRules Target) : base(Target)
		{
			bLegalToDistributeObjectCode = true;

			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"Media",
				});

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"RHI",
					"ElectraPlayerRuntime"
				});

			PrivateIncludePathModuleNames.AddRange(
			    new string[] {
				    "Media",
			});

			if (Target.bCompileAgainstEngine)
			{
				PrivateDependencyModuleNames.Add("Engine");
			}
		}
	}
}
