// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Tests;

namespace HordeServer.Analytics.Integration.Tests.Telemetry
{
	[TestClass]
	public static class AssemblyHooks
	{
		[AssemblyCleanup]
		public static void Cleanup()
		{
			DatabaseIntegrationTest.Cleanup();
		}
	}
}