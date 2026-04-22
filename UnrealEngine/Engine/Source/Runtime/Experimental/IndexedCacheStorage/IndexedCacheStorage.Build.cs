// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class IndexedCacheStorage : ModuleRules
{
	public IndexedCacheStorage(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.Add("Core");

		CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error; 
	}
}
