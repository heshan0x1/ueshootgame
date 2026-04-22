// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Runtime.InteropServices;

namespace EpicGames.UBA
{
	internal class TraceImpl : ITrace
	{
		nint _handle = IntPtr.Zero;
		readonly bool _ownsHandle = false;

		#region DllImport
		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern uint Trace_TaskBegin(nint trace, string description, string details);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern void Trace_TaskHint(nint trace, uint taskId, string hint);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern void Trace_TaskEnd(nint trace, uint taskId, bool success);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern void Trace_UpdateStatus(nint trace, uint statusRow, uint statusColumn, string statusText, byte statusType, string? statusLink);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern nint Trace_Create(string? name);

		[DllImport("UbaHost", CharSet = CharSet.Auto)]
		static extern void Trace_Destroy(nint server, string? writeFile);
		#endregion

		internal TraceImpl(nint handle)
		{
			_handle = handle;
			_ownsHandle = false;
		}

		internal TraceImpl(string? name)
		{
			_handle = Trace_Create(name);
		}

		#region IDisposable
		~TraceImpl() => Dispose(false);

		public void Dispose()
		{
			Dispose(true);
			GC.SuppressFinalize(this);
		}

		protected virtual void Dispose(bool disposing)
		{
			if (disposing)
			{
			}

			if (_handle != IntPtr.Zero)
			{
				if (_ownsHandle)
				{
					Trace_Destroy(_handle, null);
				}
				_handle = IntPtr.Zero;
			}
		}
		#endregion

		public nint GetHandle() => _handle;

		public uint TaskBegin(string description, string details) => Trace_TaskBegin(_handle, description, details);

		public void TaskHint(uint taskId, string hint) => Trace_TaskHint(_handle, taskId, hint);

		public void TaskEnd(uint taskId) => Trace_TaskEnd(_handle, taskId, true);

		public void UpdateStatus(uint statusRow, uint statusColumn, string statusText, LogEntryType statusType, string? statusLink) => Trace_UpdateStatus(_handle, statusRow, statusColumn, statusText, (byte)statusType, statusLink);

		public void CloseAndWrite(string traceFileName)
		{
			if (_ownsHandle && _handle != IntPtr.Zero)
			{
				Trace_Destroy(_handle, traceFileName);
				_handle = IntPtr.Zero;
			}
		}
	}
}