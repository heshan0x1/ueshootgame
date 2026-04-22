// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;
using EpicGames.Core;

public class VKQuality : ModuleRules
{
	public VKQuality(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;
		string VKQualityBasePath = Target.UEThirdPartySourceDirectory + "vkquality";

		if (Target.Platform == UnrealTargetPlatform.Android)
		{
			PublicSystemIncludePaths.Add(VKQualityBasePath + "/vkq_library/vkquality/src/main/cpp");

			string PluginPath = Utils.MakePathRelativeTo(ModuleDirectory, Target.RelativeEnginePath);
			AdditionalPropertiesForReceipt.Add("AndroidPlugin", Path.Combine(PluginPath, "VKQuality_UPL.xml"));
		}
	}
}
