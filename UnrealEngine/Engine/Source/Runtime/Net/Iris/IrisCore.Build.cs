// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class IrisCore : ModuleRules
	{
		public IrisCore(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"NetCore",
					"TraceLog"
				}
			);

			// To be removed once UE_WITH_IRIS has been removed in code.
			PublicDefinitions.Add("UE_WITH_IRIS=1");

			CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;
		}
	}
}
