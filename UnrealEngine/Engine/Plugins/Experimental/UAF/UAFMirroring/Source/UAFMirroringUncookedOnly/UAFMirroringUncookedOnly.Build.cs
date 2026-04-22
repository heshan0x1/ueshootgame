// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class UAFMirroringUncookedOnly : ModuleRules
{
    public UAFMirroringUncookedOnly(ReadOnlyTargetRules Target) : base(Target)
    {
	    PrivateDependencyModuleNames.AddRange(
		    new string[]
		    {
			    "Core",
			    "CoreUObject",
			    "AnimationCore",
			    "Engine",
			    "UAF",
			    "UAFUncookedOnly",
			    "UAFAnimGraph",
			    "UAFAnimGraphUncookedOnly",
			    "UAFMirroring",
			    "RigVMDeveloper",
			    "RigVM",
			    "Slate",
			    "SlateCore",
		    }
	    );
    }
}