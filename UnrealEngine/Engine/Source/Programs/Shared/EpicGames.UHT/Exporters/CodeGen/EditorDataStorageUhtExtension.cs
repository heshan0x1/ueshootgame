// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Text;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Utils;
using EpicGames.UHT.Exporters.CodeGen;
using EpicGames.UHT.Types;

namespace EditorDataStorageUhtExtension
{
	[UnrealHeaderTool]
	class Extension
	{
		private const string DynamicTemplateMetadata = "EditorDataStorage_DynamicColumnTemplate";
	    
		[UhtCodeGeneratorInjector(
			UhtType = typeof(UhtScriptStruct), 
			Location = UhtCodeGeneratorInjectionLocation.GeneratedMacro)]
		public static void InjectDynamicColumnTemplateStaticAttribute(StringBuilder builder, UhtType uhtType, int leadingTabs, string eolSequence)
		{
			if (uhtType.MetaData.ContainsKey(DynamicTemplateMetadata))
			{
				builder.Append('\t', leadingTabs).Append("struct EditorDataStorage_DynamicColumnTemplate{};").Append(eolSequence);
			}
		}
	}
}