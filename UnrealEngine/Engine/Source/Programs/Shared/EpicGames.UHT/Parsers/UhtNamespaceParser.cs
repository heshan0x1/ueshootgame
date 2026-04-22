// Copyright Epic Games, Inc. All Rights Reserved.

using System.Diagnostics.CodeAnalysis;
using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Parsers
{

	/// <summary>
	/// namespace parser
	/// </summary>
	[UnrealHeaderTool]
	class UhtNamespaceParser
	{
		[UhtKeyword(Extends = UhtTableNames.Global, Keyword = "namespace", DisableUsageError = true)]
		[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
		[SuppressMessage("Style", "IDE0060:Remove unused parameter", Justification = "Attribute accessed method")]
		private static UhtParseResult NamespaceKeyword(UhtParsingScope topScope, UhtParsingScope actionScope, ref UhtToken token)
		{
			return topScope.Module.Module.AllowUETypesInNamespaces ? ParseNamespace(topScope) : UhtParseResult.Unhandled;
		}

		[UhtKeyword(Extends = UhtTableNames.Global)]
		[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
		[SuppressMessage("Style", "IDE0060:Remove unused parameter", Justification = "Attribute accessed method")]
		private static UhtParseResult VMODULEKeyword(UhtParsingScope topScope, UhtParsingScope actionScope, ref UhtToken token)
		{
			return ParseVModule(topScope, ref token);
		}

		private static int ParseNamespaceIdentifiers(UhtParsingScope topScope)
		{
			// Parse the namespace identifiers
			int namespaceCount = 0;
			if (topScope.TokenReader.TryOptionalIdentifier(out UhtToken token))
			{
				namespaceCount++;
				topScope.HeaderParser.PushNamespace(token);
				while (topScope.TokenReader.TryOptional("::"))
				{
					namespaceCount++;
					topScope.TokenReader.Optional("inline");
					topScope.HeaderParser.PushNamespace(topScope.TokenReader.GetIdentifier());
				}
			}
			return namespaceCount;
		}

		private static UhtParseResult ParseNamespace(UhtParsingScope parentScope)
		{
			using UhtParsingScope topScope = new(parentScope, parentScope.ScopeType, parentScope.Session.GetKeywordTable(UhtTableNames.Global), UhtAccessSpecifier.Public);
			const string ScopeName = "namespace";
			using UhtMessageContext tokenContext = new(ScopeName);

			// Handle any attributes
			topScope.TokenReader.OptionalAttributes(false);

			// Parse the namespace identifiers
			int namespaceCount = ParseNamespaceIdentifiers(topScope);

			// if this is a namespace id = ... statement
			if (topScope.TokenReader.TryOptional('='))
			{
				while (true)
				{
					UhtToken skipped = topScope.TokenReader.GetToken();
					if (skipped.IsSymbol(';'))
					{
						break;
					}
				}
			}
			else
			{
				// Parse the namespace body
				topScope.HeaderParser.ParseStatements('{', '}', true);

				// TODO: The parse statement code expects to have any trailing ';' removed.  Remove any here
				topScope.TokenReader.Optional(';');
			}

			// Remove the namespaces from the active namespace
			topScope.HeaderParser.PopNamespaces(namespaceCount);
			return UhtParseResult.Handled;
		}

		private static UhtParseResult ParseVModule(UhtParsingScope parentScope, ref UhtToken token)
		{
			UhtClass classObj = new(parentScope.HeaderFile, parentScope.HeaderParser.GetNamespace(), parentScope.ScopeType, token.InputLine);
			classObj.ClassType = UhtClassType.VModule;

			using UhtParsingScope topScope = new(parentScope, classObj, parentScope.Session.GetKeywordTable(UhtTableNames.VModule), UhtAccessSpecifier.Public);
			const string ScopeName = "VMODULE";
			using UhtMessageContext tokenContext = new(ScopeName);

			// Parse the specifiers
			UhtSpecifierContext specifierContext = new(topScope, topScope.TokenReader, classObj.MetaData);
			UhtSpecifierParser specifiers = UhtSpecifierParser.GetThreadInstance(specifierContext, ScopeName, parentScope.Session.GetSpecifierTable(UhtTableNames.Class));
			specifiers.ParseSpecifiers();
			classObj.GeneratedBodyLineNumber = classObj.LineNumber;
			classObj.GeneratedBodyAccessSpecifier = UhtAccessSpecifier.Public;
			classObj.PrologLineNumber = topScope.TokenReader.InputLine;
			classObj.ClassFlags |= EClassFlags.Native;
			classObj.SuperIdentifier = [new UhtToken(UhtTokenType.Identifier, token.UngetPos, token.UngetLine, token.InputStartPos, token.InputLine, "UObject")];

			topScope.AddFormattedCommentsAsTooltipMetaData();

			topScope.TokenReader.Require("namespace");

			// Handle any attributes
			topScope.TokenReader.OptionalAttributes(false);

			// Parse the namespace identifiers
			int namespaceCount = ParseNamespaceIdentifiers(topScope);

			// Remap the namespace
			classObj.Namespace = topScope.HeaderParser.GetNamespace();

			// Use the namespace name as the source name
			string moduleName = classObj.Namespace.SourceName;
			classObj.SourceName = $"VMODULE_{classObj.Module.Module.Name}_{classObj.Namespace.SourceName}";
			classObj.ClassFlags |= EClassFlags.EditInlineNew | EClassFlags.HasInstancedReference;

			specifiers.ParseDeferred();

			{
				using BorrowStringBuilder borrowBuilder = new(StringBuilderCache.Small);
				borrowBuilder.StringBuilder.AppendVerseUEVNIPackageName(classObj);
				classObj.Outer = classObj.Module.CreatePackage(borrowBuilder.StringBuilder.ToString());
			}

			UhtCompilerDirective compilerDirective = topScope.HeaderParser.GetCurrentCompositeCompilerDirective();
			classObj.DefineScope |= compilerDirective.GetDefaultDefineScopes();
			classObj.Outer?.AddChild(classObj);

			topScope.HeaderParser.ParseStatements('{', '}', true);

			// Mark all functions as static
			foreach (UhtType child in classObj.Children)
			{
				if (child is UhtFunction functionObj)
				{
					functionObj.FunctionFlags |= EFunctionFlags.Static;
					functionObj.FunctionExportFlags |= UhtFunctionExportFlags.CppStatic;
				}
			}

			// Remove the namespaces from the active namespace
			topScope.HeaderParser.PopNamespaces(namespaceCount);
			return UhtParseResult.Handled;
		}
	}
}
