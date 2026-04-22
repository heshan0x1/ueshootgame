// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class NiagaraNaniteShader : ModuleRules
{
    public NiagaraNaniteShader(ReadOnlyTargetRules Target) : base(Target)
    {
		PrivateDependencyModuleNames.AddRange(
            new string[] {
                "CoreUObject",
                "Engine",
                "Renderer",
            }
        );

        PublicDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "RenderCore",
                "VectorVM",
                "RHI",
                "Projects",
                "NiagaraCore",
            }
        );
    }
}
