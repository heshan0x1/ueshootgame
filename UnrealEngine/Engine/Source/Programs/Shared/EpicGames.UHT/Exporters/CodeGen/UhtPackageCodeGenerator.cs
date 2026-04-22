// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Linq;
using System.Text;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Exporters.CodeGen
{
	class UhtPackageCodeGenerator : IUhtSingletonNameProvider
	{
		public static string HeaderCopyright =
			"// Copyright Epic Games, Inc. All Rights Reserved.\r\n" +
			"/*===========================================================================\r\n" +
			"\tGenerated code exported from UnrealHeaderTool.\r\n" +
			"\tDO NOT modify this manually! Edit the corresponding .h files instead!\r\n" +
			"===========================================================================*/\r\n" +
			"\r\n";

		public static string RequiredCPPIncludes = "#include \"UObject/GeneratedCppIncludes.h\"\r\n";

		public static string EnableDeprecationWarnings = "PRAGMA_ENABLE_DEPRECATION_WARNINGS";
		public static string DisableDeprecationWarnings = "PRAGMA_DISABLE_DEPRECATION_WARNINGS";

		public readonly UhtCodeGenerator CodeGenerator;
		public readonly UhtModule Module;
		public bool SaveExportedHeaders => Module.Module.SaveExportedHeaders;

		public Utils.UhtSession Session => CodeGenerator.Session;
		public UhtCodeGenerator.PackageInfo[] PackageInfos => CodeGenerator.PackageInfos;
		public UhtCodeGenerator.HeaderInfo[] HeaderInfos => CodeGenerator.HeaderInfos;
		public UhtCodeGenerator.ObjectInfo[] ObjectInfos => CodeGenerator.ObjectInfos;

		public UhtPackageCodeGenerator(UhtCodeGenerator codeGenerator, UhtModule module)
		{
			CodeGenerator = codeGenerator;
			Module = module;
		}

		#region Utility functions

		/// <summary>
		/// Return the singleton name for an object
		/// </summary>
		/// <param name="obj">The object in question.</param>
		/// <param name="type">Type of declaration to get</param>
		/// <returns>Singleton name of "nullptr" if Object is null</returns>
		public string GetSingletonName(UhtObject? obj, UhtSingletonType type)
		{
			return CodeGenerator.GetSingletonName(obj, type);
		}

		/// <summary>
		/// Return the external declaration for an object
		/// </summary>
		/// <param name="obj">The object in question.</param>
		/// <param name="type">Type of declaration to get</param>
		/// <returns>External declaration</returns>
		public string GetExternalDecl(UhtObject obj, UhtSingletonType type)
		{
			return CodeGenerator.GetExternalDecl(obj, type);
		}

		/// <summary>
		/// Return the external declaration for an object
		/// </summary>
		/// <param name="objectIndex">The object in question.</param>
		/// <param name="type">Type of declaration to get</param>
		/// <returns>External declaration</returns>
		public string GetExternalDecl(int objectIndex, UhtSingletonType type)
		{
			return CodeGenerator.GetExternalDecl(objectIndex, type);
		}

		/// <summary>
		/// Test to see if the given field is a delegate function
		/// </summary>
		/// <param name="field">Field to be tested</param>
		/// <returns>True if the field is a delegate function</returns>
		public static bool IsDelegateFunction(UhtField field)
		{
			if (field is UhtFunction function)
			{
				return function.FunctionType.IsDelegate();
			}
			return false;
		}

		/// <summary>
		/// Combines two hash values to get a third.
		/// Note - this function is not commutative.
		///
		/// This function cannot change for backward compatibility reasons.
		/// You may want to choose HashCombineFast for a better in-memory hash combining function.
		/// 
		/// NOTE: This is a copy of the method in TypeHash.h
		/// </summary>
		/// <param name="A">Hash to merge</param>
		/// <param name="C">Previously combined hash</param>
		/// <returns>Resulting hash value</returns>
		[System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE1006:Naming Styles", Justification = "<Pending>")]
		[System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE2001:Embedded statements must be on their own line", Justification = "<Pending>")]
		public static uint HashCombine(uint A, uint C)
		{
			uint B = 0x9e3779b9;
			A += B;

			A -= B; A -= C; A ^= (C >> 13);
			B -= C; B -= A; B ^= (A << 8);
			C -= A; C -= B; C ^= (B >> 13);
			A -= B; A -= C; A ^= (C >> 12);
			B -= C; B -= A; B ^= (A << 16);
			C -= A; C -= B; C ^= (B >> 5);
			A -= B; A -= C; A ^= (C >> 3);
			B -= C; B -= A; B ^= (A << 10);
			C -= A; C -= B; C ^= (B >> 15);

			return C;
		}
		#endregion
	}

	/// <summary>
	/// Helper formatting methods
	/// </summary>
	public static class UhtPackageCodeGeneratorExtensions
	{
		/// <summary>
		/// Append a metadata definition for a type without properties (e.g. an enum)
		/// </summary>
		/// <param name="builder">Destination builder</param>
		/// <param name="type">Source type containing the meta data</param>
		/// <param name="settings">Access to code generation settings</param>
		/// <param name="name">Name</param>
		/// <param name="tabs">Number of tabs to indent</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendMetaDataDecl(this StringBuilder builder, UhtType type, IUhtSessionSettingsProvider settings, string name, int tabs)
		{
			if (!type.MetaData.IsEmpty())
			{
				builder.Append("#if WITH_METADATA\r\n");
				builder.AppendMetaDataDefinition(type, settings, /*namePrefix*/null, name, /*nameSuffix*/ null, /*metaNameSuffix*/ null, tabs); // Appends an entire array definition
				builder.Append("#endif // WITH_METADATA\r\n");
			}
			return builder;
		}

		/// <summary>
		/// Append a metadata ddefinition for a type with properties (e.g. a class or struct)
		/// </summary>
		/// <param name="builder">Destination builder</param>
		/// <param name="type">Source type containing the meta data</param>
		/// <param name="propertyContext">Context for formatting properties</param>
		/// <param name="properties">Optional collection of properties to output</param>
		/// <param name="name">Name</param>
		/// <param name="tabs">Number of tabs to indent</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendMetaDataDecl(this StringBuilder builder, UhtType type, IUhtPropertyMemberContext propertyContext, UhtUsedDefineScopes<UhtProperty>? properties, string name, int tabs)
		{
			if (!type.MetaData.IsEmpty() || (properties != null && properties.Instances.Any(x => !x.MetaData.IsEmpty())))
			{
				builder.Append("#if WITH_METADATA\r\n");
				builder.AppendMetaDataDefinition(type, propertyContext, /*namePrefix*/null, name, /*nameSuffix*/ null, /*metaNameSuffix*/ null, tabs); // Appends an entire array definition
				if (propertyContext != null && properties != null)
				{
					builder.AppendInstances(properties, UhtDefineScopeNames.Standard,
						(builder, property) =>
						{
							property.AppendMetaDataDecl(builder, propertyContext, property.EngineName, "", tabs);
						});
				}
				builder.Append("#endif // WITH_METADATA\r\n");
			}
			return builder;
		}



		/// <summary>
		/// Append an expression to reference a singleton. i.e. the name of a factory function, or an address-of expression for a constinit variable ref. 
		/// </summary>
		/// <param name="builder">String builder</param>
		/// <param name="provider">Interface through which to access singleton names</param>
		/// <param name="obj">Singleton to insert a reference to, or null</param>
		/// <param name="singletonType">Type of singleton</param>
		/// <returns></returns>
		internal static StringBuilder AppendSingletonRef(this StringBuilder builder, IUhtSingletonNameProvider provider, UhtObject? obj, UhtSingletonType singletonType)
		{
			if (obj == null)
			{
				return builder.Append("nullptr");
			}
			return builder.Append(singletonType == UhtSingletonType.ConstInit ? '&' : null)
				.Append(provider.GetSingletonName(obj, singletonType));
		}

		/// <summary>
		/// Append nullptr or an address-of expression for the constinit singleton for the given object
		/// </summary>
		/// <param name="builder">Stringbuillder to append to</param>
		/// <param name="provider">Interface to access singleton names</param>
		/// <param name="obj">Object to append the singleton for, e.g. a package or class</param>
		/// <returns></returns>
		internal static StringBuilder AppendConstInitSingletonRef(this StringBuilder builder, IUhtSingletonNameProvider provider, UhtObject? obj)
		{
			return builder.AppendSingletonRef(provider, obj, UhtSingletonType.ConstInit);
		}

		/// <summary>
		/// Append nullptr or an address-of expression for the constinit singleton of a built in type
		/// </summary>
		/// <param name="builder">Stringbuillder to append to</param>
		/// <param name="session">Session object to retrieve built-in types from</param>
		/// <param name="provider">Interface to access singleton names</param>
		/// <param name="type">Object to append the singleton for</param>
		/// <returns></returns>
		internal static StringBuilder AppendConstInitSingletonRef(this StringBuilder builder, UhtSession session, IUhtSingletonNameProvider provider, UhtEngineType type)
		{
			switch (type)
			{
			case UhtEngineType.Package:
				return builder.AppendSingletonRef(provider, session.UPackage, UhtSingletonType.ConstInit);
			case UhtEngineType.Class:
				return builder.AppendSingletonRef(provider, session.UClass, UhtSingletonType.ConstInit);
			case UhtEngineType.Delegate:
				return builder.AppendSingletonRef(provider, session.UDelegateFunction, UhtSingletonType.ConstInit); 
			case UhtEngineType.SparseDelegate:
				return builder.AppendSingletonRef(provider, session.USparseDelegateFunction, UhtSingletonType.ConstInit); 
			case UhtEngineType.Enum:
				return builder.AppendSingletonRef(provider, session.UEnum, UhtSingletonType.ConstInit);
			case UhtEngineType.Function:
				return builder.AppendSingletonRef(provider, session.UFunction, UhtSingletonType.ConstInit);
			case UhtEngineType.Interface:
				return builder.AppendSingletonRef(provider, session.UInterface, UhtSingletonType.ConstInit);
			case UhtEngineType.ScriptStruct:
				return builder.AppendSingletonRef(provider, session.UScriptStruct, UhtSingletonType.ConstInit);
			case UhtEngineType.NativeInterface:
				throw new NotImplementedException("ConstInit singleton for NativeInterface not implemented");
			case UhtEngineType.Property:
				throw new ArgumentException("Cannot insert singleton ref for property type because properties are not UObjects");
			default:
				throw new ArgumentException($"Invalid value ${type} for UhtEngineType");
			}
		}
	}
}
