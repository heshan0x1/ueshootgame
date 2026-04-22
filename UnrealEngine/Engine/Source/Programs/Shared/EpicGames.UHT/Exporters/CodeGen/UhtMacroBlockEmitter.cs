// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Text;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Exporters.CodeGen
{
	internal struct UhtMacroBlockEmitter : IDisposable
	{
		private readonly StringBuilder _builder;
		private readonly UhtDefineScopeNames _defineScopeNames;
		private UhtDefineScope _current = UhtDefineScope.None;

		public UhtMacroBlockEmitter(StringBuilder builder, UhtDefineScopeNames defineScopeNames, UhtDefineScope initialState)
		{
			_builder = builder;
			_defineScopeNames = defineScopeNames;
			Set(initialState);
		}

		public void Set(UhtDefineScope defineScope)
		{
			if (defineScope == UhtDefineScope.Invalid)
			{
				defineScope = UhtDefineScope.None;
			}
			if (_current == defineScope)
			{
				return;
			}
			_builder.AppendEndIfPreprocessor(_current, _defineScopeNames);
			_builder.AppendIfPreprocessor(defineScope, _defineScopeNames);
			_current = defineScope;
		}

		public void Dispose()
		{
			Set(UhtDefineScope.None);
		}
	}

	/// <summary>
	/// Utility class to wrap blocks of generated code in #if/#endif for a literal string conditionally.
	/// i.e. used to conditionally wrap code in #if UE_WITH_CONSTINIT_UOBJECT depending on the value of 
	/// Session.IsUsingMultipleCompiledInObjectFormats
	/// </summary>
	internal struct UhtConditionalMacroBlock : IDisposable
	{
		private readonly StringBuilder _builder;
		private readonly string _macroName;
		private bool _enabled;

		public UhtConditionalMacroBlock(StringBuilder builder, string macroName, bool isEnabled)
		{
			_builder = builder;
			_macroName = macroName;
			_enabled = isEnabled;
			if (isEnabled)
			{
				_builder.Append($"#if {_macroName}\r\n");
			}
		}

		public void Dispose()
		{
			if (_enabled)
			{
				_builder.Append($"#endif // {_macroName}\r\n");
				_enabled = false;
			}
		}
	}
}
