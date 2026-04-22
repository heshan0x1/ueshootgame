// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Parsers
{

	/// <summary>
	/// Collection of UENUM specifiers
	/// </summary>
	[UnrealHeaderTool]
	[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
	public static class UhtFieldSpecifiers
	{
		private static bool VerseCommonFieldKeys(UhtField fieldObj, StringView key, StringView value)
		{
			if (key.Equals("name", StringComparison.OrdinalIgnoreCase))
			{
				fieldObj.VerseName = value.ToString();
				return true;
			}
			else if (key.Equals("module", StringComparison.OrdinalIgnoreCase))
			{
				fieldObj.VerseModule = value.ToString();
				return true;
			}
			else if (key.Equals("noalias", StringComparison.OrdinalIgnoreCase))
			{
				fieldObj.FieldExportFlags |= UhtFieldExportFlags.NoVerseAlias;
				return true;
			}
			return false;
		}

		private static void VerseCommonSpecifier(UhtField fieldObj, string what, List<KeyValuePair<StringView, StringView>> values, Func<StringView, StringView, bool> keyAndValueFunc)
		{
			// Extract all the elements of the meta data
			foreach (KeyValuePair<StringView, StringView> kvp in values)
			{
				if (!keyAndValueFunc(kvp.Key, kvp.Value))
				{
					fieldObj.LogError($"Verse specifier option '{kvp.Key}' is unknown or not valid on a {what}");
				}
			}

			if (String.IsNullOrEmpty(fieldObj.VerseName))
			{
				fieldObj.LogError($"A verse name must be specified containing the name of the verse type");
				return;
			}

			// Generate the engine name
			if (String.IsNullOrEmpty(fieldObj.VerseModule))
			{
				fieldObj.EngineName = fieldObj.VerseName!;
			}
			else
			{
				fieldObj.EngineName = $"{fieldObj.VerseModule}_{fieldObj.VerseName!}";
			}

			using BorrowStringBuilder borrowBuilder = new(StringBuilderCache.Small);
			borrowBuilder.StringBuilder.AppendVerseUEVNIPackageName(fieldObj);
			fieldObj.Outer = fieldObj.Module.CreatePackage(borrowBuilder.StringBuilder.ToString());
		}

		[UhtSpecifier(Extends = UhtTableNames.Enum, Name = "Verse", ValueType = UhtSpecifierValueType.OptionalEqualsKeyValuePairList)]
		private static void VerseEnumSpecifier(UhtSpecifierContext specifierContext, List<KeyValuePair<StringView, StringView>> values)
		{
			UhtEnum enumObj = (UhtEnum)specifierContext.Type;
			VerseCommonSpecifier(enumObj, "enum", values, (key, value) =>
			{
				if (VerseCommonFieldKeys(enumObj, key, value))
				{
					return true;
				}
				return false;
			});
		}

		[UhtSpecifier(Extends = UhtTableNames.ScriptStruct, Name = "Verse", ValueType = UhtSpecifierValueType.OptionalEqualsKeyValuePairList)]
		private static void VerseScriptStructSpecifier(UhtSpecifierContext specifierContext, List<KeyValuePair<StringView, StringView>> values)
		{
			UhtScriptStruct scriptStructObj = (UhtScriptStruct)specifierContext.Type;
			VerseCommonSpecifier(scriptStructObj, "script struct", values, (key, value) =>
			{
				if (VerseCommonFieldKeys(scriptStructObj, key, value))
				{
					return true;
				}
				else if (key.Equals("parametric", StringComparison.OrdinalIgnoreCase))
				{
					scriptStructObj.ScriptStructExportFlags |= UhtScriptStructExportFlags.IsVerseParametric;
					return true;
				}
				return false;
			});
		}

		[UhtSpecifier(Extends = UhtTableNames.Class, Name = "Verse", ValueType = UhtSpecifierValueType.OptionalEqualsKeyValuePairList)]
		private static void VerseClassSpecifier(UhtSpecifierContext specifierContext, List<KeyValuePair<StringView, StringView>> values)
		{
			UhtClass classObj = (UhtClass)specifierContext.Type;
			VerseCommonSpecifier(classObj, "class", values, (key, value) =>
			{
				if (VerseCommonFieldKeys(classObj, key, value))
				{
					return true;
				}
				else if (key.Equals("experimental", StringComparison.OrdinalIgnoreCase))
				{
					classObj.MetaData.Add(classObj.Session.Config!.ValkyrieDevelopmentStatusKey, classObj.Session.Config!.ValkyrieDevelopmentStatusValueExperimental);
					return true;
				}
				else if (key.Equals("deprecated", StringComparison.OrdinalIgnoreCase))
				{
					classObj.MetaData.Add(classObj.Session.Config!.ValkyrieDeprecationStatusKey, classObj.Session.Config!.ValkyrieDeprecationStatusValueDeprecated);
					return true;
				}
				return false;
			});
		}

		[UhtSpecifier(Extends = UhtTableNames.Interface, Name = "Verse", ValueType = UhtSpecifierValueType.OptionalEqualsKeyValuePairList)]
		private static void VerseInterfaceSpecifier(UhtSpecifierContext specifierContext, List<KeyValuePair<StringView, StringView>> values)
		{
			UhtClass classObj = (UhtClass)specifierContext.Type;
			VerseCommonSpecifier(classObj, "interface", values, (key, value) =>
			{
				if (VerseCommonFieldKeys(classObj, key, value))
				{
					return true;
				}
				return false;
			});
		}

		[UhtSpecifier(Extends = UhtTableNames.NativeInterface, Name = "Verse", ValueType = UhtSpecifierValueType.OptionalEqualsKeyValuePairList)]
		private static void VerseNativeInterfaceSpecifier(UhtSpecifierContext specifierContext, List<KeyValuePair<StringView, StringView>> values)
		{
			UhtClass classObj = (UhtClass)specifierContext.Type;
			VerseCommonSpecifier(classObj, "native interface", values, (key, value) =>
			{
				if (VerseCommonFieldKeys(classObj, key, value))
				{
					return true;
				}
				return false;
			});
		}

		[UhtSpecifier(Extends = UhtTableNames.VModule, Name = "Verse", ValueType = UhtSpecifierValueType.OptionalEqualsKeyValuePairList)]
		private static void VerseVModuleSpecifier(UhtSpecifierContext specifierContext, List<KeyValuePair<StringView, StringView>> values)
		{
			UhtClass classObj = (UhtClass)specifierContext.Type;
			VerseCommonSpecifier(classObj, "verse module", values, (key, value) =>
			{
				if (VerseCommonFieldKeys(classObj, key, value))
				{
					return true;
				}
				return false;
			});
		}
	}
}
