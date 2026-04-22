// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

namespace UnrealBuildTool.Rules
{
	public class StateTreeDeveloper : ModuleRules
	{
		public StateTreeDeveloper(ReadOnlyTargetRules Target) : base(Target)
		{
			CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Warning;

			bAllowUETypesInNamespaces = true;

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"SlateCore",
				}
			);
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"CoreUObject",
					"InputCore",
					"RewindDebuggerRuntimeInterface",
					"Slate",
					"StateTreeModule",
					"TraceLog"
				}
			);

			// Allow debugger traces on all non-shipping desktop targets and shipping editors (UEFN)
			if (Target.Configuration != UnrealTargetConfiguration.Shipping || Target.bBuildEditor)
			{
				PublicDefinitions.Add("WITH_STATETREE_TRACE=1");

				if (Target.Platform.IsInGroup(UnrealPlatformGroup.Desktop))
				{
					PublicDefinitions.Add("WITH_STATETREE_TRACE_DEBUGGER=1");
				}
				else
				{
					PublicDefinitions.Add("WITH_STATETREE_TRACE_DEBUGGER=0");
				}
			}
			else
			{
				PublicDefinitions.Add("WITH_STATETREE_TRACE=0");
				PublicDefinitions.Add("WITH_STATETREE_TRACE_DEBUGGER=0");
			}
		}
	}
}