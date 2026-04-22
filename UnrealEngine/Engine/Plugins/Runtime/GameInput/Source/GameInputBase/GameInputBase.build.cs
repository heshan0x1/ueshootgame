// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GameInputBase : ModuleRules
	{		
		/// <summary>
		/// True if this platform has support for the Game Input library.
		/// 
		/// Overriden per-platform implement of the Game Input Base module.
		/// </summary>
		protected virtual bool HasGameInputSupport(ReadOnlyTargetRules Target)
		{
			// Console platforms will override this function and determine if they have GameInput support on their own.
			// For the base functionality, we can only support Game Input on windows platforms.
			if (!Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
			{
				return false;
			}

			// GameInput on Windows only supports x64 platforms.
			if (!Target.Architecture.bIsX64)
			{
				return false;
			}

			// Windows x64 targets should have support as of UE 5.5+ because we have the GameInputWindowsLibrary
			// module to compile GameInput into the package
			return true;
		}

		/// <summary>
		/// An extension point for subclasses of this module to add any additional required include/library files
		/// that may be necessary to add the GameInput SDK to their platform. This will only be called if
		/// HasGameInputSupport returns true.
		/// </summary>
		protected virtual void AddRequiredDeps(ReadOnlyTargetRules Target)
		{
			// Console platforms will override this function and determine if they have GameInput support on their own.
			// For the base functionality, we can only support Game Input on windows platforms.
			if (!Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
			{
				return;
			}

			// GameInput on Windows only supports x64 platforms.
			if (!Target.Architecture.bIsX64)
			{
				return;
			}

			// Add dependency to this third party module which contains the GameInput source 
			// code and static library
			PublicDependencyModuleNames.Add("GameInputWindowsLibrary");
		}

		public GameInputBase(ReadOnlyTargetRules Target) : base(Target)
		{
			// Enable truncation warnings in this plugin
			CppCompileWarningSettings.UnsafeTypeCastWarningLevel = WarningLevel.Error;

			// Uncomment this line to make for easier debugging
			//OptimizeCode = CodeOptimization.Never;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"ApplicationCore",
					"SlateCore",
					"Slate",
					"Engine",
					"InputCore",
					"InputDevice",				
					"CoreUObject",
					"DeveloperSettings",
				}
			);

			bool bHasGameInputSupport = HasGameInputSupport(Target);
			// Define this as 0 in the base module to avoid compilation errors when building
			// without any Game Input support. It is up to the platform-specific submodules to define 
			PublicDefinitions.Add("GAME_INPUT_SUPPORT=" + (bHasGameInputSupport ? "1" : "0"));

			// Give platforms extensions a chance to add any required dependencies that may be necessary to compile game input
			if (bHasGameInputSupport)
			{
				AddRequiredDeps(Target);
			}
		}
	}
}
