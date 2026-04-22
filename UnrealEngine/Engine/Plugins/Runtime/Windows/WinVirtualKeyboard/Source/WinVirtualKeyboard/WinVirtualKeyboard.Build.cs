// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class WinVirtualKeyboard : ModuleRules
	{
		// for A/B testing the two approaches - COM vs CPPWinRT.
		static readonly bool bUseCOM = true;

		public WinVirtualKeyboard(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.Add("Slate");
			PublicDependencyModuleNames.Add("Core");
			PublicSystemLibraries.Add("WindowsApp.lib");

			if (bUseCOM)
			{
				// use WinRT ABI via COM
				PrivateDefinitions.Add("WITH_CPPWINRT=0");
			}
			else
			{
				PrivateDefinitions.Add("WITH_CPPWINRT=1");

				// CPPWinRT requires /Ehsc
				bEnableExceptions = true;

				// add the CPPWinRT include path if the project isn't using CPPWinRT
				if (!Target.WindowsPlatform.bUseCPPWinRT)
				{
					PrivateIncludePaths.Add( Path.Combine(Target.WindowsPlatform.WindowsSdkDir, "include", Target.WindowsPlatform.WindowsSdkVersion.ToString(), "cppwinrt") );
				}
			}
		}
	}
}
