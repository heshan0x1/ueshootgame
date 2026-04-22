using System;
using System.Collections.Generic;
using UnrealBuildTool;

public class FirstPersonServerTarget : TargetRules
{
    public FirstPersonServerTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Server;
        DefaultBuildSettings = BuildSettingsVersion.V6;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("FirstPerson");
    }
}