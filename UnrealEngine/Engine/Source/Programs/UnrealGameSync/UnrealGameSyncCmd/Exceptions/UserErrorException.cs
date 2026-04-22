// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace UnrealGameSyncCmd.Exceptions
{
	public sealed class UserErrorException : Exception
	{
		public LogEvent Event { get; }
		public int Code { get; }

		public UserErrorException(LogEvent evt)
			: base(evt.ToString())
		{
			Event = evt;
			Code = 1;
		}

		public UserErrorException(string message, params object[] args)
			: this(LogEvent.Create(LogLevel.Error, message, args))
		{
		}
	}
}
