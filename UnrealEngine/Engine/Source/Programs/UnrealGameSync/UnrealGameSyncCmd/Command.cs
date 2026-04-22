// Copyright Epic Games, Inc. All Rights Reserved.

using System.Threading.Tasks;
using UnrealGameSync;

namespace UnrealGameSyncCmd
{
	internal abstract class Command
	{
		internal static BuildConfig EditorConfig => BuildConfig.Development;

		public abstract Task ExecuteAsync(CommandContext context);
	}
}
