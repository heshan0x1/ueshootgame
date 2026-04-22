// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace UnrealBuildTool.Tests.TestUtilities
{
	internal class TestSDK : UEBuildPlatformSDK
	{
		public TestSDK(ILogger inLogger) : base(inLogger)
		{
		}

		public override bool TryConvertVersionToInt(string? stringValue, out ulong outValue, string? hint = null)
		{
			outValue = 1;
			return true;
		}

		protected override string? GetInstalledSDKVersion()
		{
			return "1.0.0";
		}
	}
}
