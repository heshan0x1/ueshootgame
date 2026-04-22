// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.Eventing.Reader;
using System.Linq;
using System.Text;
using EpicGames.Core;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Exporters.CodeGen
{
	/// <summary>
	/// Collection of all registrations for a specific package
	/// </summary>
	internal class UhtRegistrations
	{
		public UhtUsedDefineScopes<UhtEnum> Enumerations { get; } = new();
		public UhtUsedDefineScopes<UhtScriptStruct> ScriptStructs { get; } = new();
		public UhtUsedDefineScopes<UhtScriptStruct> RigVMStructs { get; } = new();
		public UhtUsedDefineScopes<UhtClass> Classes { get; } = new();
	}

	internal class UhtHeaderCodeGeneratorCppFile : UhtHeaderCodeGenerator
	{

		/// <summary>
		/// Construct an instance of this generator object
		/// </summary>
		/// <param name="codeGenerator">The base code generator</param>
		/// <param name="headerFile">Header file being generated</param>
		public UhtHeaderCodeGeneratorCppFile(UhtCodeGenerator codeGenerator, UhtHeaderFile headerFile)
			: base(codeGenerator,  headerFile)
		{
		}

		/// <summary>
		/// For a given UE header file, generated the generated H file
		/// </summary>
		/// <param name="factory">Requesting factory</param>
		public void Generate(IUhtExportFactory factory)
		{
			ref UhtCodeGenerator.HeaderInfo headerInfo = ref HeaderInfos[HeaderFile.HeaderFileTypeIndex];
			{
				using BorrowStringBuilder borrower = new(StringBuilderCache.Big);
				StringBuilder builder = borrower.StringBuilder;

				builder.Append(HeaderCopyright);
				builder.Append(RequiredCPPIncludes);
				builder.Append("#include \"").Append(headerInfo.IncludePath).Append("\"\r\n");

				bool addedStructuredArchiveFromArchiveHeader = false;
				bool addedArchiveUObjectFromStructuredArchiveHeader = false;
				bool addedCoreNetHeader = false;
				bool addedRigVMHeaders = false;
				HashSet<UhtHeaderFile> addedIncludes = new();
				List<string> includesToAdd = new();
				addedIncludes.Add(HeaderFile);

				if (headerInfo.NeedsFastArrayHeaders)
				{
					includesToAdd.Add("Net/Serialization/FastArraySerializerImplementation.h");
				}
				if (headerInfo.NeedsVerseCodeGen)
				{
					includesToAdd.Add("VerseVM/VVMUECodeGen.h");
					includesToAdd.Add("VerseInteropUtils.h");
				}

				if (HeaderFile.Children.Any(x => x is UhtScriptStruct structObj && structObj.IsVerseField))
				{
					includesToAdd.Add("VerseVM/VVMVerseStruct.h");
				}
				if (HeaderFile.Children.Any(x => x is UhtClass classObj && classObj.IsVerseField))
				{
					includesToAdd.Add("VerseVM/VVMVerseClass.h");
					includesToAdd.Add("VerseVM/VVMVerseFunction.h");
				}
				if (HeaderFile.Children.Any(x => x is UhtEnum enumObj && enumObj.IsVerseField))
				{
					includesToAdd.Add("VerseVM/VVMVerseEnum.h");
				}
				if (HeaderFile.Children.Any(x => x is UhtStruct structObj && x.Children.Any(y => y is UhtVerseValueProperty)))
				{
					includesToAdd.Add("VerseVM/VBPVMDynamicProperty.h");  // Only needed when WITH_VERSE_BPVM
					includesToAdd.Add("UObject/VerseValueProperty.h"); 
				}

				foreach (UhtType type in HeaderFile.Children)
				{
					if (type is UhtStruct structObj)
					{
						// Functions
						foreach (UhtFunction function in structObj.Functions)
						{
							if (!function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.CppStatic) && function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetValidate))
							{
								if (!addedCoreNetHeader)
								{
									includesToAdd.Add("UObject/CoreNet.h");
									addedCoreNetHeader = true;
								}
							}

							bool requireIncludeForClasses = IsRpcFunction(function) && ShouldExportFunction(function);

							foreach (UhtProperty property in function.Properties)
							{
								AddIncludeForProperty(property, requireIncludeForClasses, addedIncludes, includesToAdd);
							}

							foreach (UhtType parameter in function.ParameterProperties.Span)
							{
								if (parameter is UhtProperty property
										&& property.NeedsGCBarrierWhenPassedToFunction(function)
										&& property is UhtObjectProperty objectProperty)
								{
									UhtClass uhtClass = objectProperty.Class;
									if (!uhtClass.HeaderFile.IsNoExportTypes && addedIncludes.Add(uhtClass.HeaderFile))
									{
										includesToAdd.Add(HeaderInfos[uhtClass.HeaderFile.HeaderFileTypeIndex].IncludePath);
									}
								}
							}
						}

						// Properties
						foreach (UhtProperty property in structObj.Properties)
						{
							AddIncludeForProperty(property, false, addedIncludes, includesToAdd);
						}
					}

					if (type is UhtScriptStruct scriptStruct)
					{
						if (!addedRigVMHeaders && scriptStruct.RigVMStructInfo is not null)
						{
							includesToAdd.Add("RigVMCore/RigVMFunction.h");
							includesToAdd.Add("RigVMCore/RigVMRegistry.h");
							addedRigVMHeaders = true;
						}
					}

					if (type is UhtClass classObj)
					{
						if (classObj.ClassWithin != Session.UObject && !classObj.ClassWithin.HeaderFile.IsNoExportTypes)
						{
							if (addedIncludes.Add(classObj.ClassWithin.HeaderFile))
							{
								includesToAdd.Add(HeaderInfos[classObj.ClassWithin.HeaderFile.HeaderFileTypeIndex].IncludePath);
							}
						}

						switch (classObj.SerializerArchiveType)
						{
							case UhtSerializerArchiveType.None:
								break;

							case UhtSerializerArchiveType.Archive:
								if (!addedArchiveUObjectFromStructuredArchiveHeader)
								{
									includesToAdd.Add("Serialization/ArchiveUObjectFromStructuredArchive.h");
									addedArchiveUObjectFromStructuredArchiveHeader = true;
								}
								break;

							case UhtSerializerArchiveType.StructuredArchiveRecord:
								if (!addedStructuredArchiveFromArchiveHeader)
								{
									includesToAdd.Add("Serialization/StructuredArchive.h");
									addedStructuredArchiveFromArchiveHeader = true;
								}
								break;
						}
					}
					else
					{
						if (!type.HeaderFile.IsNoExportTypes && addedIncludes.Add(type.HeaderFile))
						{
							includesToAdd.Add(HeaderInfos[type.HeaderFile.HeaderFileTypeIndex].IncludePath);
						}
					}
				}

				includesToAdd.Sort(StringComparerUE.OrdinalIgnoreCase);
				foreach (string include in includesToAdd)
				{
					builder.Append("#include \"").Append(include).Append("\"\r\n");
				}

				builder.Append("\r\n");
				builder.Append(DisableDeprecationWarnings).Append("\r\n");

				if (!Session.IsUsingMultipleCompiledInObjectFormats)
				{
					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
					{
						builder.Append("static_assert(UE_WITH_CONSTINIT_UOBJECT, \"This generated code can only be compiled with UE_WITH_CONSTINIT_OBJECT\");");
					}
					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
					{
						builder.Append("static_assert(!UE_WITH_CONSTINIT_UOBJECT, \"This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT\");");
					}
				}

				builder.Append("\r\n");
				string cleanFileName = HeaderFile.FileNameWithoutExtension.Replace('.', '_');
				builder.Append("void EmptyLinkFunctionForGeneratedCode").Append(cleanFileName).Append("() {}\r\n");

				if (!HeaderFile.References.CrossModule.IsEmpty)
				{
					using UhtCodeBlockComment blockComment = new(builder, "Cross Module References");
					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
					{
						using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
						// Cross-references based on constinit variables
						IReadOnlyList<string> sorted = HeaderFile.References.CrossModule.GetSortedReferences(
							(objectIndex, type) => type switch
							{
								UhtSingletonType.Statics => null,
								_ => GetExternalDecl(objectIndex, UhtSingletonType.ConstInit)
							});
						foreach (string crossReference in sorted)
						{
							builder.Append(crossReference.AsSpan().TrimStart());
						}
					}
					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
					{
						using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
						// Cross-references based on factory functions
						IReadOnlyList<string> sorted = HeaderFile.References.CrossModule.GetSortedReferences(
							(objectIndex, type) => type switch
							{
								UhtSingletonType.ConstInit or UhtSingletonType.Statics => null,
								_ => GetExternalDecl(objectIndex, type)
							});
						foreach (string crossReference in sorted)
						{
							builder.Append(crossReference.AsSpan().TrimStart());
						}
					}
				}

				int generatedBodyStart = builder.Length;

				// We need to emit the internal UClass needed for VMODULEs
				foreach (UhtField field in HeaderFile.References.ExportTypes)
				{
					if (field is UhtClass classObj)
					{
						if (!classObj.ClassFlags.HasAnyFlags(EClassFlags.Intrinsic))
						{
							AppendClassDeclaration(builder, classObj);
						}
					}
				}

				Dictionary<UhtPackage, UhtRegistrations> packageRegistrations = new();
				foreach (UhtField field in HeaderFile.References.ExportTypes)
				{
					if (field is UhtEnum enumObj)
					{
						using UhtCodeBlockComment blockComment = new(builder, field);
						using UhtMacroBlockEmitter macroBlockEmitter = new(builder, UhtDefineScopeNames.Standard, enumObj.DefineScope);
						AppendEnum(builder, enumObj);
						GetRegistrations(packageRegistrations, field).Enumerations.Add(enumObj);
					}
					else if (field is UhtScriptStruct scriptStruct)
					{
						using UhtCodeBlockComment blockComment = new(builder, field);
						using UhtMacroBlockEmitter macroBlockEmitter = new(builder, UhtDefineScopeNames.Standard, scriptStruct.DefineScope);
						AppendScriptStruct(builder, scriptStruct);
						if (scriptStruct.ScriptStructFlags.HasAnyFlags(EStructFlags.Native))
						{
							GetRegistrations(packageRegistrations, field).ScriptStructs.Add(scriptStruct);
							if (scriptStruct.RigVMStructInfo != null && scriptStruct.RigVMStructInfo.Methods.Count > 0)
							{
								GetRegistrations(packageRegistrations, field).RigVMStructs.Add(scriptStruct);
							}
						}
					}
					else if (field is UhtFunction function)
					{
						using UhtCodeBlockComment blockComment = new(builder, field);
						using UhtMacroBlockEmitter macroBlockEmitter = new(builder, UhtDefineScopeNames.Standard, function.DefineScope);
						AppendDelegate(builder, function);
					}
					else if (field is UhtClass classObj)
					{
						if (!classObj.ClassFlags.HasAnyFlags(EClassFlags.Intrinsic))
						{
							// Collect the functions to be exported
							UhtUsedDefineScopes<UhtFunction> functions = new(classObj.Functions);
							functions.Instances.Sort((x, y) => StringComparerUE.OrdinalIgnoreCase.Compare(x.EngineName, y.EngineName));

							// Output any functions
							foreach (UhtFunction classFunction in functions.Instances)
							{
								AppendClassFunction(builder, classObj, classFunction);
							}

							using UhtCodeBlockComment blockComment = new(builder, field);
							using UhtMacroBlockEmitter macroBlockEmitter = new(builder, UhtDefineScopeNames.Standard, classObj.DefineScope);
							AppendClass(builder, classObj, functions);
							GetRegistrations(packageRegistrations, field).Classes.Add(classObj);

							if (classObj.ClassType == UhtClassType.Interface && classObj.IsVerseField)
							{
								foreach (UhtClass baseClass in classObj.FlattenedVerseInterfaces)
								{
									AppendNativeInterfaceVerseProxyFunctions(builder, classObj, baseClass.AlternateObject as UhtClass);
								}
							}
						}
					}
				}

				foreach (UhtPackage package in Module.Packages)
				{
					if (!packageRegistrations.TryGetValue(package, out UhtRegistrations? registrations))
					{
						continue;
					}

					string name = $"Z_CompiledInDeferFile_{headerInfo.FileId}_{PackageInfos[package.PackageTypeIndex].StrippedName}";
					string staticsName = $"{name}_Statics";

					uint combinedHash = UInt32.MaxValue;

					using UhtCodeBlockComment blockComment = new(builder, "Registration");
					builder.Append($"struct {staticsName}\r\n");
					builder.Append("{\r\n");

					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
					{
						using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
						// Constinit versions 
						// Array of pointers to constinit objects
						builder.AppendInstances(registrations.Enumerations, UhtDefineScopeNames.Standard,
							builder => builder.Append("\tstatic inline constinit UEnum* EnumInfo[] = {\r\n"),
							(builder, enumObj) => builder.Append("\t\t").AppendConstInitSingletonRef(this, enumObj).Append(",\r\n"),
							builder => builder.Append("\t};\r\n"));

						builder.AppendInstances(registrations.ScriptStructs, UhtDefineScopeNames.Standard,
							builder => builder.Append("\tstatic inline constinit UScriptStruct* ScriptStructInfo[] = {\r\n"),
							(builder, scriptStruct) => builder.Append("\t\t").AppendConstInitSingletonRef(this, scriptStruct).Append(",\r\n"),
							builder => builder.Append("\t};\r\n"));

						builder.AppendInstances(registrations.Classes, UhtDefineScopeNames.Standard,
							builder => builder.Append("\tstatic inline constinit UClass* ClassInfo[] = {\r\n"),
							(builder, classObj) => builder.Append("\t\t").AppendConstInitSingletonRef(this, classObj).Append(",\r\n"),
							builder => builder.Append("\t};\r\n"));

						// Registration for RigVM functions
						builder.AppendInstances(registrations.RigVMStructs, UhtDefineScopeNames.Standard,
							builder => builder.Append("\tstatic inline constinit FRigVMCompiledInStruct RigVMStructs[] = {\r\n"),
							(builder, scriptStruct) => builder.Append("\t\t{ .Struct = ").AppendConstInitSingletonRef(this, scriptStruct).Append(", ")
								 .Append(".Functions = MakeArrayView(").AppendSingletonName(this, scriptStruct, UhtSingletonType.Statics).Append("::RigVMFunctions), ")
								.Append(" },\r\n"),
							builder => builder.Append("\t};\r\n"));
					}

					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
					{ 
						using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
						// Non-constinit version
						builder.AppendInstances(registrations.Enumerations, UhtDefineScopeNames.Standard, 
							builder => builder.Append("\tstatic constexpr FEnumRegisterCompiledInInfo EnumInfo[] = {\r\n"),
							(builder, enumObj) =>
							{ 
								uint hash = ObjectInfos[enumObj.ObjectTypeIndex].Hash;
								builder
									.Append($"\t\t{{ {enumObj.SourceName}_StaticEnum, ")
									.Append($"TEXT(\"{enumObj.EngineName}\"), ")
									.Append($"&Z_Registration_Info_UEnum_{enumObj.SourceName}, ")
									.Append($"CONSTRUCT_RELOAD_VERSION_INFO(FEnumReloadVersionInfo, {hash}U) }},\r\n");
								combinedHash = HashCombine(combinedHash, hash);
							},
							builder => builder.Append("\t};\r\n"));

						builder.AppendInstances(registrations.ScriptStructs, UhtDefineScopeNames.Standard,
							builder => builder.Append("\tstatic constexpr FStructRegisterCompiledInInfo ScriptStructInfo[] = {\r\n"),
							(builder, scriptStruct) =>
							{
								uint hash = ObjectInfos[scriptStruct.ObjectTypeIndex].Hash;
								builder
									.Append($"\t\t{{ {scriptStruct.FullyQualifiedSourceName}::StaticStruct, ")
									.Append($"{GetSingletonName(scriptStruct, UhtSingletonType.Statics)}::NewStructOps, ")
									.Append($"TEXT(\"{scriptStruct.EngineName}\"),")
									.Append($"&Z_Registration_Info_UScriptStruct_{scriptStruct.SourceName}, ")
									.Append($"CONSTRUCT_RELOAD_VERSION_INFO(FStructReloadVersionInfo, sizeof({scriptStruct.FullyQualifiedSourceName}), {hash}U) }},\r\n");
								combinedHash = HashCombine(combinedHash, hash);
							},
							builder => builder.Append("\t};\r\n"));

						builder.AppendInstances(registrations.Classes, UhtDefineScopeNames.Standard,
							builder => builder.Append("\tstatic constexpr FClassRegisterCompiledInInfo ClassInfo[] = {\r\n"),
							(builder, classObj) =>
							{
								uint hash = ObjectInfos[classObj.ObjectTypeIndex].Hash;
								builder
									.Append($"\t\t{{ {GetSingletonName(classObj, UhtSingletonType.Registered)}, ")
									.Append($"{classObj.FullyQualifiedSourceName}::StaticClass, ")
									.Append($"TEXT(\"{(classObj.IsVerseField ? classObj.EngineName : classObj.SourceName)}\"), ")
									.Append($"&Z_Registration_Info_UClass_{classObj.SourceName}, ")
									.Append($"CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof({classObj.FullyQualifiedSourceName}), {hash}U) }},\r\n");
								combinedHash = HashCombine(combinedHash, hash);
							},
							builder => builder.Append("\t};\r\n"));
					}

					builder.Append($"}}; // {staticsName} \r\n");

					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
					{ 
						using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
						builder.Append($"static FRegisterCompiledInObjects {name}_{combinedHash}{{\r\n")
							.AppendArrayView(registrations.Classes, UhtDefineScopeNames.Standard, staticsName, "ClassInfo", 1, ",\r\n")
							.AppendArrayView(registrations.ScriptStructs, UhtDefineScopeNames.Standard, staticsName, "ScriptStructInfo", 1, ",\r\n")
							.AppendArrayView(registrations.Enumerations, UhtDefineScopeNames.Standard, staticsName, "EnumInfo", 1, ",\r\n")
							.Append("};\r\n");
						if (!registrations.RigVMStructs.IsEmpty)
						{
							builder.Append($"static FRegisterRigVMStructs RigVM_{name}{{\r\n")
								.AppendArrayView(registrations.RigVMStructs, UhtDefineScopeNames.Standard, staticsName, "RigVMStructs", 1, "\r\n")
								.Append("};\r\n");
						}
					}
					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
					{
						using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
						builder.Append("static FRegisterCompiledInInfo ")
							.Append(name)
							.Append($"_{combinedHash}{{\r\n")
							.Append("\tTEXT(\"")
							.Append(package.EngineName)
							.Append("\"),\r\n")
							.AppendArrayPtrAndCountLine(registrations.Classes, UhtDefineScopeNames.Standard, staticsName, "ClassInfo", 1, ",\r\n")
							.AppendArrayPtrAndCountLine(registrations.ScriptStructs, UhtDefineScopeNames.Standard, staticsName, "ScriptStructInfo", 1, ",\r\n")
							.AppendArrayPtrAndCountLine(registrations.Enumerations, UhtDefineScopeNames.Standard, staticsName, "EnumInfo", 1, ",\r\n")
							.Append("};\r\n");
					}
				}

				if (Session.IncludeDebugOutput)
				{
					builder.Append("#if 0\r\n");
					IReadOnlyList<string> sorted = HeaderFile.References.Declaration.GetSortedReferences(GetExternalDecl);
					foreach (string declaration in sorted)
					{
						builder.Append(declaration);
					}
					builder.Append("#endif\r\n");
				}

				int generatedBodyEnd = builder.Length;

				builder.Append("\r\n");
				builder.Append(EnableDeprecationWarnings).Append("\r\n");

				{
					using UhtRentedPoolBuffer<char> borrowBuffer = builder.RentPoolBuffer();
					string cppFilePath = factory.MakePath(HeaderFile, ".gen.cpp");
					StringView generatedBody = new(borrowBuffer.Buffer.Memory);
					if (SaveExportedHeaders)
					{
						factory.CommitOutput(cppFilePath, generatedBody);
					}

					// Save the hash of the generated body 
					HeaderInfos[HeaderFile.HeaderFileTypeIndex].BodyHash = UhtHash.GenenerateTextHash(generatedBody.Span[generatedBodyStart..generatedBodyEnd]);
				}
			}
		}

		private void AddIncludeForType(UhtProperty uhtProperty, bool requireIncludeForClasses, HashSet<UhtHeaderFile> addedIncludes, IList<string> includesToAdd)
		{
			if (uhtProperty is UhtStructProperty structProperty)
			{
				UhtScriptStruct scriptStruct = structProperty.ScriptStruct;
				if (!scriptStruct.HeaderFile.IsNoExportTypes && addedIncludes.Add(scriptStruct.HeaderFile))
				{
					includesToAdd.Add(HeaderInfos[scriptStruct.HeaderFile.HeaderFileTypeIndex].IncludePath);
				}
			}
			else if (requireIncludeForClasses && uhtProperty is UhtClassProperty classProperty)
			{
				UhtClass uhtClass = classProperty.Class;
				if (!uhtClass.HeaderFile.IsNoExportTypes && addedIncludes.Add(uhtClass.HeaderFile))
				{
					includesToAdd.Add(HeaderInfos[uhtClass.HeaderFile.HeaderFileTypeIndex].IncludePath);
				}
			}
		}

		private void AddIncludeForProperty(UhtProperty property, bool requireIncludeForClasses, HashSet<UhtHeaderFile> addedIncludes, IList<string> includesToAdd)
		{
			AddIncludeForType(property, requireIncludeForClasses, addedIncludes, includesToAdd);

			if (property is UhtContainerBaseProperty containerProperty)
			{
				AddIncludeForType(containerProperty.ValueProperty, false, addedIncludes, includesToAdd);
			}

			if (property is UhtMapProperty mapProperty)
			{
				AddIncludeForType(mapProperty.KeyProperty, false, addedIncludes, includesToAdd);
			}
		}

		private StringBuilder AppendEnum(StringBuilder builder, UhtEnum enumObj)
		{
			const string MetaDataParamsName = "Enum_MetaDataParams";
			const string ObjectFlags = "RF_Public|RF_Transient|RF_MarkAsNative";
			string singletonName = GetSingletonName(enumObj, UhtSingletonType.Registered);
			string staticsName = singletonName + "_Statics";
			string registrationName = $"Z_Registration_Info_UEnum_{enumObj.SourceName}";

			string enumDisplayNameFn = enumObj.MetaData.GetValueOrDefault(UhtNames.EnumDisplayNameFn);
			if (enumDisplayNameFn.Length == 0)
			{
				enumDisplayNameFn = "nullptr";
			}

			// If we don't have a zero 0 then we emit a static assert to verify we have one
			if (!enumObj.IsValidEnumValue(0) && enumObj.MetaData.ContainsKey(UhtNames.BlueprintType))
			{
				bool hasUnparsedValue = enumObj.EnumValues.Exists(x => x.Value == -1);
				if (hasUnparsedValue)
				{
					builder.Append("static_assert(");
					bool doneFirst = false;
					foreach (UhtEnumValue value in enumObj.EnumValues)
					{
						if (value.Value == -1)
						{
							if (doneFirst)
							{
								builder.Append("||");
							}
							doneFirst = true;
							builder.Append("!int64(").Append(value.Name).Append(')');
						}
					}
					builder.Append(", \"'").Append(enumObj.SourceName).Append("' does not have a 0 entry!(This is a problem when the enum is initialized by default)\");\r\n");
				}
			}

			if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
			{
				using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
				builder.Append($"static UEnum* {enumObj.SourceName}_StaticEnum()\r\n");
				builder.Append("{\r\n");
				builder.Append("\treturn ").AppendConstInitSingletonRef(this, enumObj).Append(";\r\n");
				builder.Append("}\r\n");
			}
			if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
			{
				using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
				builder.Append("static FEnumRegistrationInfo ").Append(registrationName).Append(";\r\n");
				builder.Append("static UEnum* ").Append(enumObj.SourceName).Append("_StaticEnum()\r\n");
				builder.Append("{\r\n");
				builder.Append("\tif (!").Append(registrationName).Append(".OuterSingleton)\r\n");
				builder.Append("\t{\r\n");
				builder.Append("\t\t").Append(registrationName).Append(".OuterSingleton = GetStaticEnum(").Append(singletonName).Append(", (UObject*)")
					.Append(GetSingletonName(enumObj.Package, UhtSingletonType.Registered)).Append("(), TEXT(\"").Append(enumObj.SourceName).Append("\"));\r\n");
				builder.Append("\t}\r\n");
				builder.Append("\treturn ").Append(registrationName).Append(".OuterSingleton;\r\n");
				builder.Append("}\r\n");
			}

			if (Module.Module.AlwaysExportEnums)
			{
				builder.Append("template<> ").Append(Module.NonAttributedApi).Append("UEnum* StaticEnum<").Append(enumObj.Namespace.FullSourceName).Append(enumObj.CppType).Append(">()\r\n");
			}
			else
			{
				builder.Append("template<> ").Append("UEnum* StaticEnum<").Append(enumObj.Namespace.FullSourceName).Append(enumObj.CppType).Append(">()\r\n");
			}
			builder.Append("{\r\n");
			builder.Append("\treturn ").Append(enumObj.SourceName).Append("_StaticEnum();\r\n");
			builder.Append("}\r\n");

			if (enumObj.IsVerseField)
			{
				builder.Append("V_DEFINE_IMPORTED_ENUM(").Append(Module.Api.TrimEnd()).Append(", \"").AppendVerseUEPackageName(enumObj).Append("\", \"").Append(enumObj.GetVerseScopeAndName(UhtVerseNameMode.Default)).Append("\", ").Append(enumObj.SourceName).Append(");\r\n");
			}

			// Everything from this point on will be part of the definition hash
			int hashCodeBlockStart = builder.Length;

			// Statics declaration
			string? enumeratorNamesArray = null;
			string? enumeratorValuesArray = null;
			string enumFlags = enumObj.EnumFlags.HasAnyFlags(EEnumFlags.Flags) ? "EEnumFlags::Flags" : "EEnumFlags::None";
			bool hasEnumValues = enumObj.EnumValues.Count != 0 || enumObj.GeneratedMaxName is not null;
			{
				builder.Append("struct ").Append(staticsName).Append("\r\n");
				builder.Append("{\r\n");
				builder.AppendMetaDataDecl(enumObj, CodeGenerator, MetaDataParamsName, 1);

				// Enumerators
				if (hasEnumValues)
				{
					enumeratorNamesArray = "EnumeratorNamesUTF8";
					enumeratorValuesArray = "EnumeratorValues";

					if (enumObj.GeneratedMaxValue is not null)
					{
						builder.Append("\tstatic inline constexpr int64 GeneratedMaxValue = ").Append(enumObj.GeneratedMaxValue).Append(";\r\n");
					}
					else if (enumObj.GeneratedMaxName is not null)
					{
						// Use helper function to generate max value if UHT couldn't parse all values
						builder.Append("\tstatic inline constexpr int64 GeneratedMaxValue = UEnum::CalculateMaxEnumValue({\r\n")
							.AppendEach(enumObj.EnumValues, (builder, value) => builder.Append($"\t\t(int64){enumObj.Namespace.FullSourceName}{value.Name},\r\n"))
							.Append("\t\t(int64)0\r\n") // Emit 0 to handle empty enums and avoid skipping the last comma in the loop above 
							.Append($"\t}}, {enumFlags});\r\n");
					}

					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
					{
						using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
						builder.Append("\tstatic constexpr const UTF8CHAR* EnumeratorNamesUTF8[]= {\r\n");
						int enumIndex = 0;
						foreach (UhtEnumValue value in enumObj.EnumValues)
						{
							if (!enumObj.MetaData.TryGetValue("OverrideName", enumIndex, out string? keyName))
							{
								keyName = value.Name.ToString();
							}
							builder.Append("\t\tUTF8TEXT(").AppendUTF8LiteralString(keyName).Append("),\r\n");
							++enumIndex;
						}
						if (enumObj.GeneratedMaxName is not null)
						{
							builder.Append("\t\tUTF8TEXT(").AppendUTF8LiteralString(enumObj.GeneratedMaxName).Append("),\r\n");
						}
						builder.Append("\t};\r\n");

						builder.Append("\tstatic inline UE_CONSTINIT_UOBJECT_DECL int64 EnumeratorValues[]= {\r\n")
							.AppendEach(enumObj.EnumValues, (builder, value) => builder.Append($"\t\t(int64){enumObj.Namespace.FullSourceName}{value.Name},\r\n"));
						if (enumObj.GeneratedMaxName is not null)
						{
							builder.Append("\t\tGeneratedMaxValue,\r\n");
						}
						builder.Append("\t};\r\n");
					}
					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
					{
						using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
						builder.Append("\tstatic constexpr UECodeGen_Private::FEnumeratorParam Enumerators[] = {\r\n");
						int enumIndex = 0;
						foreach (UhtEnumValue value in enumObj.EnumValues)
						{
							if (!enumObj.MetaData.TryGetValue("OverrideName", enumIndex, out string? keyName))
							{
								keyName = value.Name.ToString();
							}
							builder.Append("\t\t{ ").AppendUTF8LiteralString(keyName).Append(", (int64)").Append(enumObj.Namespace.FullSourceName).Append(value.Name).Append(" },\r\n");
							++enumIndex;
						}
						if (enumObj.GeneratedMaxName is not null)
						{
							builder.Append("\t\t{ ").AppendUTF8LiteralString(enumObj.GeneratedMaxName).Append(", GeneratedMaxValue").Append(" },\r\n");
						}
						builder.Append("\t};\r\n");
					}
				}

				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
				{
					builder.Append("\tstatic const UECodeGen_Private::").AppendEnumParamsType(enumObj).Append(" EnumParams;\r\n");
				}

				builder.Append($"}}; // struct {staticsName} \r\n");
			}

			// Statics definition
			{
				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
				{ 
					using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append($"UE_CONSTINIT_UOBJECT_DECL TNoDestroy<U{enumObj.EngineClassName}> {GetSingletonName(enumObj, UhtSingletonType.ConstInit)}{{NoDestroyConstEval,\r\n");
					builder.AppendConstInitObjectParams(enumObj, this, 1);
					builder.Append("\tUE::CodeGen::ConstInit::FUFieldParams{},\r\n");
					builder.Append("\tUE::CodeGen::ConstInit::FEnumParams{\r\n");
					builder.Append("\t\t.CppTypeStaticUTF8 = UTF8TEXT(").AppendUTF8LiteralString(enumObj.CppType).Append("),\r\n");
					builder.Append(enumeratorNamesArray is null ? null : $"\t\t.StaticNamesUTF8 = MakeArrayView({staticsName}::{enumeratorNamesArray}),\r\n"); 
					builder.Append(enumeratorValuesArray is null ? null : $"\t\t.EnumValues = MakeArrayView({staticsName}::{enumeratorValuesArray}),\r\n");
					builder.Append($"\t\t.CppForm = (uint8)UEnum::ECppForm::{enumObj.CppForm.ToString()},\r\n");
					builder.Append($"\t\t.EnumFlags = {(enumObj.EnumFlags.HasAnyFlags(EEnumFlags.Flags) ? "EEnumFlags::Flags" : "EEnumFlags::None")},\r\n");
					builder.Append($"\t\t.DisplayNameFn = {enumDisplayNameFn},\r\n");
					builder.Append("\t\tIF_WITH_METADATA(.MetaData = ").AppendMetaDataArrayView(enumObj.MetaData, staticsName, MetaDataParamsName, 0, ",)\r\n");
					builder.Append("\t},");
					if (enumObj.IsVerseField)
					{
						builder.Append("\tUE::CodeGen::ConstInit::FVerseEnumParams{\r\n")
							.Append("\t\t.QualifiedName = UTF8TEXT(\"").AppendVerseScopeAndName(enumObj, UhtVerseNameMode.Qualified).Append("\"),\r\n")
							.Append("\t},\r\n");
					}
					builder.Append("};\r\n");
				}
				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
				{
					using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append("const UECodeGen_Private::").AppendEnumParamsType(enumObj).Append(' ').Append(staticsName).Append("::EnumParams = {\r\n");
					if (enumObj.IsVerseField)
					{
						builder.Append("\t{\r\n");
					}
					builder.Append("\t(UObject*(*)())").Append(GetSingletonName(enumObj.Package, UhtSingletonType.Registered)).Append(",\r\n");
					builder.Append('\t').Append(enumDisplayNameFn).Append(",\r\n");
					builder.Append('\t').AppendUTF8LiteralString(enumObj.SourceName).Append(",\r\n");
					builder.Append('\t').AppendUTF8LiteralString(enumObj.CppType).Append(",\r\n");
					if (hasEnumValues)
					{
						builder.Append('\t').Append(staticsName).Append("::Enumerators,\r\n");
						builder.Append('\t').Append(ObjectFlags).Append(",\r\n");
						builder.Append("\tUE_ARRAY_COUNT(").Append(staticsName).Append("::Enumerators),\r\n");
					}
					else
					{
						builder.Append('\t').Append("nullptr,\r\n");
						builder.Append('\t').Append(ObjectFlags).Append(",\r\n");
						builder.Append("\t0,\r\n");
					}
					builder.Append('\t').Append(enumObj.EnumFlags.HasAnyFlags(EEnumFlags.Flags) ? "EEnumFlags::Flags" : "EEnumFlags::None").Append(",\r\n");
					builder.Append("\t(uint8)UEnum::ECppForm::").Append(enumObj.CppForm.ToString()).Append(",\r\n");
					builder.Append('\t').AppendMetaDataParams(enumObj, staticsName, MetaDataParamsName).Append("\r\n");
					if (enumObj.IsVerseField)
					{
						builder.Append("\t},\r\n");
						builder.Append("\t\"").AppendVerseScopeAndName(enumObj, UhtVerseNameMode.Qualified).Append("\"\r\n");
					}
					builder.Append("};\r\n");
				}
			}

			// Registration singleton
			if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
			{ 
				using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
				builder.Append("UEnum* ").Append(singletonName).Append("()\r\n");
				builder.Append("{\r\n");
				builder.Append("\tif (!").Append(registrationName).Append(".InnerSingleton)\r\n");
				builder.Append("\t{\r\n");
				if (enumObj.IsVerseField)
				{
					builder.Append("\t\tVerse::CodeGen::Private::ConstructUVerseEnum(").Append(registrationName).Append(".InnerSingleton, ").Append(staticsName).Append("::EnumParams);\r\n");
				}
				else
				{
					builder.Append("\t\tUECodeGen_Private::ConstructUEnum(").Append(registrationName).Append(".InnerSingleton, ").Append(staticsName).Append("::EnumParams);\r\n");
				}
				builder.Append("\t}\r\n");
				builder.Append("\treturn ").Append(registrationName).Append(".InnerSingleton;\r\n");
				builder.Append("}\r\n");
			}

			{
				using UhtRentedPoolBuffer<char> borrowBuffer = builder.RentPoolBuffer(hashCodeBlockStart, builder.Length - hashCodeBlockStart);
				ObjectInfos[enumObj.ObjectTypeIndex].Hash = UhtHash.GenenerateTextHash(borrowBuffer.Buffer.Memory.Span);
			}
			return builder;
		}

		private StringBuilder AppendScriptStruct(StringBuilder builder, UhtScriptStruct scriptStruct)
		{
			const string MetaDataParamsName = "Struct_MetaDataParams";
			string singletonName = GetSingletonName(scriptStruct, UhtSingletonType.Registered);
			string constinitName = GetSingletonName(scriptStruct, UhtSingletonType.ConstInit);
			string staticsName = GetSingletonName(scriptStruct, UhtSingletonType.Statics);
			string registrationName = $"Z_Registration_Info_UScriptStruct_{scriptStruct.SourceName}";
			List<UhtScriptStruct> noExportStructs = FindNoExportStructs(scriptStruct);

			// Everything from this point on will be part of the definition hash
			int hashCodeBlockStart = builder.Length;

			// Collect the properties
			PropertyMemberContextImpl context = new(CodeGenerator, scriptStruct, staticsName);
			UhtUsedDefineScopes<UhtProperty> properties = new(scriptStruct.Properties);

			// Declare the statics structure
			{
				builder.Append("struct ").Append(staticsName).Append("\r\n");
				builder.Append("{\r\n");

				foreach (UhtScriptStruct noExportStruct in noExportStructs)
				{
					AppendMirrorsForNoexportStruct(builder, noExportStruct, 1);
					builder.Append("\tstatic_assert(sizeof(").Append(noExportStruct.NamespaceExportName).Append(noExportStruct.SourceName).Append(") < MAX_uint16);\r\n");
					builder.Append("\tstatic_assert(alignof(").Append(noExportStruct.NamespaceExportName).Append(noExportStruct.SourceName).Append(") < MAX_uint8);\r\n");
				}

				// Declare a function to access the size/alignment for NoExport cases as well, consteval should prevent this function having code in the binary
				builder.Append("\tstatic inline consteval int32 GetStructSize() { return sizeof(").Append(scriptStruct.NamespaceExportName).Append(scriptStruct.SourceName).Append("); }\r\n");
				builder.Append("\tstatic inline consteval int16 GetStructAlignment() { return alignof(").Append(scriptStruct.NamespaceExportName).Append(scriptStruct.SourceName).Append("); }\r\n");

				// Meta data
				builder.AppendMetaDataDecl(scriptStruct, context, properties, MetaDataParamsName, 1);

				// Properties
				AppendPropertiesDecl(builder, context, properties, 1);

				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
				{ 
					using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append("\tstatic UE_CONSTINIT_UOBJECT_DECL inline const FStructBaseChain* StructBases[] = {\r\n");
					if (scriptStruct.SuperScriptStruct is not null) 
					{
						List<UhtScriptStruct> supers = new();
						for (UhtScriptStruct? super = scriptStruct.SuperScriptStruct; super is not null; super = super.SuperScriptStruct)
						{
							supers.Add(super);
						}
						foreach (UhtScriptStruct super in supers.AsEnumerable().Reverse())
						{
							builder.Append("\t\tUE::Private::AsStructBaseChain(").AppendConstInitSingletonRef(this, super).Append("),\r\n");
						}
					}
					builder.Append("\t\tUE::Private::AsStructBaseChain(").AppendConstInitSingletonRef(this, scriptStruct).Append("),\r\n");
					builder.Append("\t};\r\n");
				}

				if (scriptStruct.RigVMStructInfo != null && scriptStruct.RigVMStructInfo.Methods.Count > 0)
				{
					// Arrays of parameters per method - note that predicates and other functions read different lists
					foreach (UhtRigVMMethodInfo methodInfo in scriptStruct.RigVMStructInfo.Methods)
					{
						if (methodInfo.IsPredicate)
						{
							if (methodInfo.Parameters.Count == 0)
							{
								continue;
							}

							builder.Append($"\tstatic inline constexpr FRigVMCompiledInFunctionArgument RigVMFunctionParameters_{methodInfo.Name}[] = {{\r\n");
							foreach (UhtRigVMParameter parameter in methodInfo.Parameters)
							{
								builder.Append($"\t\t{{ .Name = TEXT(\"{parameter.NameOriginal()}\"), ")
									.Append($".Type = TEXT(\"{parameter.TypeOriginal()}\"), ")
									.Append($".Direction = ERigVMFunctionArgumentDirection::Input }},\r\n");
							}
							builder.Append($"\t\t{{ .Name = TEXT(\"Return\"),")
								.Append($".Type = TEXT(\"{methodInfo.ReturnType}\"),")
								.Append($".Direction = ERigVMFunctionArgumentDirection::Output, }}\r\n");
							builder.Append("\t};\r\n");
						}
						else
						{
							if (scriptStruct.RigVMStructInfo.Members.Count == 0)
							{
								continue;
							}

							builder.Append($"\tstatic inline constexpr FRigVMCompiledInFunctionArgument RigVMFunctionParameters_{methodInfo.Name}[] = {{\r\n");
							foreach (UhtRigVMParameter parameter in scriptStruct.RigVMStructInfo.Members)
							{
								builder.Append($"\t\t{{ .Name = TEXT(\"{parameter.NameOriginal()}\"), ")
									.Append($".Type = TEXT(\"{parameter.TypeOriginal()}\"), ")
									.Append($".Direction = ERigVMFunctionArgumentDirection::Input }},\r\n");
							}
							builder.Append("\t};\r\n");
						}
					}

					builder.Append("\tstatic inline constexpr FRigVMCompiledInFunction RigVMFunctions[] = {\r\n");
					foreach (UhtRigVMMethodInfo methodInfo in scriptStruct.RigVMStructInfo.Methods)
					{
						if (methodInfo.IsPredicate)
						{
							builder.Append($"\t\t{{ .MethodName = TEXT(\"{methodInfo.Name}\"), .Function = nullptr, \r\n");
						}
						else
						{
							builder.Append($"\t\t{{ .MethodName = TEXT(\"{scriptStruct::SourceName}::{methodInfo.Name}\"), \r\n")
								.Append($"\t\t\t.Function = &{scriptStruct.SourceName}::RigVM{methodInfo.Name}, \r\n");
						}

						if ((methodInfo.IsPredicate ? methodInfo.Parameters.Count : scriptStruct.RigVMStructInfo.Members.Count) > 0)
						{
							builder.Append($"\t\t\t.Parameters = MakeArrayView(RigVMFunctionParameters_{methodInfo.Name}),\r\n");
						}
						builder.Append("\t\t},\r\n");
					}
					builder.Append("\t};\r\n");
				}

				// New struct ops
				if (scriptStruct.ScriptStructFlags.HasAnyFlags(EStructFlags.Native))
				{
					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
					{ 
						using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
						builder.Append($"\tstatic inline UE_CONSTINIT_UOBJECT_DECL UScriptStruct::TCppStructOps<{scriptStruct.Namespace.FullSourceName}{scriptStruct.SourceName}> StructOps").Append("{};\r\n");
					}

					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
					{
						using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
						builder.Append("\tstatic void* NewStructOps()\r\n");
						builder.Append("\t{\r\n");
						builder.Append($"\t\treturn (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<{scriptStruct.Namespace.FullSourceName}{scriptStruct.SourceName}>();\r\n");
						builder.Append("\t}\r\n");
					}
				}

				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
				{
					using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append("\tstatic const UECodeGen_Private::").AppendStructParamsType(scriptStruct).Append(" StructParams;\r\n");
				}
				builder.Append($"}}; // struct {staticsName}\r\n");
			}

			if (scriptStruct.ScriptStructFlags.HasAnyFlags(EStructFlags.Native))
			{
				// Inject static assert to verify that we do not add vtable
				if (scriptStruct.SuperScriptStruct != null)
				{
					builder.Append("static_assert(std::is_polymorphic<")
						.Append(scriptStruct.FullyQualifiedSourceName)
						.Append(">() == std::is_polymorphic<")
						.Append(scriptStruct.SuperScriptStruct.FullyQualifiedSourceName)
						.Append(">(), \"USTRUCT ")
						.Append(scriptStruct.FullyQualifiedSourceName)
						.Append(" cannot be polymorphic unless super ")
						.Append(scriptStruct.SuperScriptStruct.FullyQualifiedSourceName)
						.Append(" is polymorphic\");\r\n");
				}

				// Outer singleton
				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
				{ 
					using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append($"class UScriptStruct* {scriptStruct.FullyQualifiedSourceName}::StaticStruct()\r\n");
					builder.Append("{\r\n");
					builder.Append("\treturn &").Append(constinitName).Append(";\r\n");
					builder.Append("\t}\r\n");
				}
				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
				{
					using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append("static FStructRegistrationInfo ").Append(registrationName).Append(";\r\n");
					builder.Append($"class UScriptStruct* {scriptStruct.FullyQualifiedSourceName}::StaticStruct()\r\n");
					builder.Append("{\r\n");
					builder.Append("\tif (!").Append(registrationName).Append(".OuterSingleton)\r\n");
					builder.Append("\t{\r\n");
					builder.Append("\t\t")
						.Append(registrationName)
						.Append(".OuterSingleton = GetStaticStruct(")
						.Append(singletonName)
						.Append(", (UObject*)")
						.Append(GetSingletonName(scriptStruct.Package, UhtSingletonType.Registered))
						.Append("(), TEXT(\"")
						.Append(scriptStruct.EngineName)
						.Append("\"));\r\n");

					// if this struct has RigVM methods - we need to register the method to our central
					// registry on construction of the static struct
					if (scriptStruct.RigVMStructInfo != null && scriptStruct.RigVMStructInfo.Methods.Count > 0)
					{
						builder.Append($"\t\tFRigVMRegistry::Get().RegisterCompiledInStruct({registrationName}.OuterSingleton, MakeArrayView({staticsName}::RigVMFunctions));\r\n");
					}

					builder.Append("\t}\r\n");
					builder.Append("\treturn ").Append(registrationName).Append(".OuterSingleton;\r\n");
					builder.Append("\t}\r\n");
				}
				

				// Inject implementation needed to support auto bindings of fast arrays
				if (ObjectInfos[scriptStruct.ObjectTypeIndex].FastArrayProperty != null)
				{
					// The preprocessor conditional is written here instead of in FastArraySerializerImplementation.h
					// since it may evaluate differently in different modules, triggering warnings in IncludeTool.
					builder.Append("#if defined(UE_NET_HAS_IRIS_FASTARRAY_BINDING) && UE_NET_HAS_IRIS_FASTARRAY_BINDING\r\n");
					builder.Append("UE_NET_IMPLEMENT_FASTARRAY(").Append(scriptStruct.SourceName).Append(");\r\n");
					builder.Append("#else\r\n");
					builder.Append("UE_NET_IMPLEMENT_FASTARRAY_STUB(").Append(scriptStruct.SourceName).Append(");\r\n");
					builder.Append("#endif\r\n");
				}
			}

			// Populate the elements of the static structure
			{
				AppendPropertiesDefs(builder, context, properties, 0);

				// NOTE: This is temporary while the new VM doesn't match the value in the old VM
				string? verseDefaultName = scriptStruct.IsVerseField ? scriptStruct.GetVerseScopeAndName(UhtVerseNameMode.Default) : null;
				string? verseQualifiedName = scriptStruct.IsVerseField ? scriptStruct.GetVerseScopeAndName(UhtVerseNameMode.Qualified) : null;

				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
				{ 
					using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append($"UE_CONSTINIT_UOBJECT_DECL TNoDestroy<U{scriptStruct.EngineClassName}> {constinitName}{{NoDestroyConstEval,\r\n");
					builder.AppendConstInitObjectParams(scriptStruct, this, 1);
					builder.Append("\tUE::CodeGen::ConstInit::FUFieldParams{},\r\n");
					builder.Append("\tUE::CodeGen::ConstInit::FStructParams{\r\n")
						.Append($"\t\t.Super = ").AppendConstInitSingletonRef(this, scriptStruct.SuperScriptStruct).Append(",\r\n")
						.Append($"\t\t.BaseChain = MakeArrayView({staticsName}::StructBases),\r\n")
						.Append($"\t\t.ChildProperties =  {staticsName}::GetFirstProperty(),\r\n")
						.Append($"\t\t.PropertiesSize = {staticsName}::GetStructSize(),\r\n")
						.Append($"\t\t.MinAlignment = {staticsName}::GetStructAlignment(),\r\n")
						.Append("\t\tIF_WITH_METADATA(.MetaData = ").AppendMetaDataArrayView(scriptStruct.MetaData, staticsName, MetaDataParamsName, 0, ",)\r\n")
						.Append("\t},\r\n");
					builder.Append("\tUE::CodeGen::ConstInit::FScriptStructParams{\r\n")
						.Append($"\t\t.StructFlags = EStructFlags(0x{(uint)(scriptStruct.ScriptStructFlags & ~EStructFlags.ComputedFlags):X8}),\r\n")
						.Append(scriptStruct.ScriptStructFlags.HasAnyFlags(EStructFlags.Native)
							? $"\t\t.StructOps = &{staticsName}::StructOps,\r\n" 
							: null)
						.Append("\t},\r\n");
					if (scriptStruct.IsVerseField)
					{
						uint guidA = Crc.Compute(new StringView(scriptStruct.EngineName));
						uint guidB = Crc.Compute(new StringView(scriptStruct.Package.EngineName));
						builder.Append("\tUE::CodeGen::ConstInit::FVerseStructParams{\r\n")
							.Append($"\t\t.GuidA = {guidA},\r\n")
							.Append($"\t\t.GuidB = {guidB},\r\n")
							.Append("\t\t.QualifiedName = ");
						if (verseDefaultName!.Equals(verseQualifiedName, StringComparison.Ordinal))
						{
							builder.Append($"UTF8TEXT(\"{verseQualifiedName}\")\r\n");
						}
						else
						{
							builder.Append($"IF_WITH_VERSE_VM(UTF8TEXT(\"{verseDefaultName}\"), UTF8TEXT(\"{verseQualifiedName}\"))\r\n");
						}
						builder.Append("},\r\n");
					}
					builder.Append("};\r\n");
				}
				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
				{ 
					using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append("const UECodeGen_Private::").AppendStructParamsType(scriptStruct).Append(' ').Append(staticsName).Append("::StructParams = {\r\n");
					if (scriptStruct.IsVerseField)
					{
						builder.Append("\t{\r\n");
					}
					builder.Append("\t(UObject* (*)())").Append(GetSingletonName(scriptStruct.Package, UhtSingletonType.Registered)).Append(",\r\n");
					builder.Append('\t').Append(GetSingletonName(scriptStruct.SuperScriptStruct, UhtSingletonType.Registered)).Append(",\r\n");
					if (scriptStruct.ScriptStructFlags.HasAnyFlags(EStructFlags.Native))
					{
						builder.Append("\t&NewStructOps,\r\n");
					}
					else
					{
						builder.Append("\tnullptr,\r\n");
					}
					builder.Append('\t').AppendUTF8LiteralString(scriptStruct.EngineName).Append(",\r\n");
					builder.AppendArrayPtrLine(properties, UhtDefineScopeNames.Standard, staticsName, "PropPointers", 1, ",\r\n");
					builder.AppendArrayCountLine(properties, UhtDefineScopeNames.Standard, staticsName, "PropPointers", 1, ",\r\n");
					builder.Append("\tsizeof(").Append(scriptStruct.NamespaceExportName).Append(scriptStruct.SourceName).Append("),\r\n");
					builder.Append("\talignof(").Append(scriptStruct.NamespaceExportName).Append(scriptStruct.SourceName).Append("),\r\n");
					builder.Append("\tRF_Public|RF_Transient|RF_MarkAsNative,\r\n");
					builder.Append($"\tEStructFlags(0x{(uint)(scriptStruct.ScriptStructFlags & ~EStructFlags.ComputedFlags):X8}),\r\n");
					builder.Append('\t').AppendMetaDataParams(scriptStruct, staticsName, MetaDataParamsName).Append("\r\n");
					if (scriptStruct.IsVerseField)
					{
						builder.Append("\t},\r\n");

						if (verseDefaultName!.Equals(verseQualifiedName, StringComparison.Ordinal))
						{
							builder.Append($"\t\"{verseQualifiedName}\"\r\n");
						}
						else
						{
							builder.Append("\t#if WITH_VERSE_VM\r\n");
							builder.Append($"\t\"{verseDefaultName}\"\r\n");
							builder.Append("\t#else\r\n");
							builder.Append($"\t\"{verseQualifiedName}\"\r\n");
							builder.Append("\t#endif\r\n");
						}
					}
					builder.Append("};\r\n");
				}
			}

			// Generate the registration function
			if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
			{
				using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
				builder.Append($"U{scriptStruct.EngineLinkClassName}* {singletonName}()\r\n");
				builder.Append("{\r\n");
				string innerSingletonName;
				if (scriptStruct.ScriptStructFlags.HasAnyFlags(EStructFlags.Native))
				{
					innerSingletonName = $"{registrationName}.InnerSingleton";
				}
				else
				{
					builder.Append("\tstatic UScriptStruct* ReturnStruct = nullptr;\r\n");
					innerSingletonName = "ReturnStruct";
				}
				builder.Append("\tif (!").Append(innerSingletonName).Append(")\r\n");
				builder.Append("\t{\r\n");
				if (scriptStruct.IsVerseField)
				{
					builder.Append("\t\tVerse::CodeGen::Private::ConstructUVerseStruct(").Append(innerSingletonName).Append(", ").Append(staticsName).Append("::StructParams);\r\n");
				}
				else 
				{
					builder.Append("\t\tUECodeGen_Private::ConstructUScriptStruct(").Append(innerSingletonName).Append(", ").Append(staticsName).Append("::StructParams);\r\n");
				}
				builder.Append("\t}\r\n");
				builder.Append($"\treturn CastChecked<U{scriptStruct.EngineClassName}>({innerSingletonName});\r\n");
				builder.Append("}\r\n");
			}

			using (UhtRentedPoolBuffer<char> borrowBuffer = builder.RentPoolBuffer(hashCodeBlockStart, builder.Length - hashCodeBlockStart))
			{
				ObjectInfos[scriptStruct.ObjectTypeIndex].Hash = UhtHash.GenenerateTextHash(borrowBuffer.Buffer.Memory.Span);
			}

			// if this struct has RigVM methods we need to implement both the 
			// virtual function as well as the stub method here.
			// The static method is implemented by the user using a macro.
			if (scriptStruct.RigVMStructInfo != null)
			{
				string constPrefix = "";
				if (!scriptStruct.RigVMStructInfo.HasAnyExecuteContextMember)
				{
					constPrefix = "const ";
				}
				
				foreach (UhtRigVMMethodInfo methodInfo in scriptStruct.RigVMStructInfo.Methods)
				{
					if (methodInfo.IsPredicate)
					{
						continue;
					}
					
					builder
						.Append(methodInfo.ReturnType)
						.Append(' ')
						.Append(scriptStruct.Namespace.FullSourceName)
						.Append(scriptStruct.SourceName)
						.Append("::")
						.Append(methodInfo.Name)
						.Append("()\r\n");
					builder.Append("{\r\n");
					if (String.IsNullOrEmpty(scriptStruct.RigVMStructInfo.ExecuteContextMember))
					{
						builder.Append('\t').Append(scriptStruct.RigVMStructInfo.ExecuteContextType).Append(" TemporaryExecuteContext;\r\n");
					}
					else
					{
						builder.Append('\t').Append(scriptStruct.RigVMStructInfo.ExecuteContextType).Append("& TemporaryExecuteContext = ").Append(scriptStruct.RigVMStructInfo.ExecuteContextMember).Append(";\r\n");
					}

					builder.Append("\tTemporaryExecuteContext.Initialize();\r\n");
					builder.Append('\t');
					if (methodInfo.ReturnType != "void")
					{
						builder.Append("return ");
					}
					builder
						.Append(methodInfo.Name)
						.Append("(TemporaryExecuteContext);\r\n")
						.Append("}\r\n");

					builder
						.Append(methodInfo.ReturnType)
						.Append(' ')
						.Append(scriptStruct.Namespace.FullSourceName)
						.Append(scriptStruct.SourceName)
						.Append("::")
						.Append(methodInfo.Name)
						.Append('(')
						.Append(constPrefix)
						.Append(scriptStruct.RigVMStructInfo.ExecuteContextType)
						.Append("& InExecuteContext)\r\n");
					builder.Append("{\r\n");

					foreach (UhtRigVMParameter parameter in scriptStruct.RigVMStructInfo.Members)
					{
						if (!parameter.RequiresCast())
						{
							continue;
						}
						builder.Append('\t').Append(parameter.CastType).Append(' ').Append(parameter.CastName).Append('(').Append(parameter.Name).Append(");\r\n");
					}
					
					foreach (UhtRigVMMethodInfo predicateInfo in scriptStruct.RigVMStructInfo.Methods)
					{
						if (predicateInfo.IsPredicate)
						{
							builder.Append('\t').Append(predicateInfo.Name).Append("Struct ").Append(predicateInfo.Name).Append("Predicate; \r\n");
						}
					}

					builder.Append('\t').Append(methodInfo.ReturnPrefix()).Append("Static").Append(methodInfo.Name).Append("(\r\n");
					builder.Append("\t\tInExecuteContext");
					builder.AppendParameterNames(scriptStruct.RigVMStructInfo.Members, true, ",\r\n\t\t", true);
					
					foreach (UhtRigVMMethodInfo predicateInfo in scriptStruct.RigVMStructInfo.Methods)
					{
						if (predicateInfo.IsPredicate)
						{
							builder.Append(", \r\n\t\t").Append(predicateInfo.Name).Append("Predicate");
						}
					}
					
					builder.Append("\r\n");
					builder.Append("\t);\r\n");
					builder.Append("}\r\n");
				}
			}
			return builder;
		}

		private StringBuilder AppendDelegate(StringBuilder builder, UhtFunction function)
		{
			AppendFunction(builder, function, false, false);

			int tabs = 0;
			string strippedFunctionName = function.StrippedFunctionName;
			string exportFunctionName = GetDelegateFunctionExportName(function);
			string extraParameter = GetDelegateFunctionExtraParameter(function);

			AppendNativeFunctionHeader(builder, function, UhtPropertyTextType.EventFunctionArgOrRetVal, false, exportFunctionName, extraParameter, UhtFunctionExportFlags.None, 0, "\r\n");
			AppendEventFunctionPrologue(builder, function, strippedFunctionName, tabs, "\r\n", true);
			builder
				.Append('\t')
				.Append(strippedFunctionName)
				.Append('.')
				.Append(function.FunctionFlags.HasAnyFlags(EFunctionFlags.MulticastDelegate) ? "ProcessMulticastDelegate" : "ProcessDelegate")
				.Append("<UObject>(")
				.Append(function.Children.Count > 0 ? "&Parms" : "NULL")
				.Append(");\r\n");
			AppendEventFunctionEpilogue(builder, function, tabs, "\r\n");

			return builder;
		}

		private StringBuilder AppendFunction(StringBuilder builder, UhtFunction function, bool isNoExport, bool isVerseNativeCallable)
		{
			const string MetaDataParamsName = "Function_MetaDataParams";
			string singletonName = GetSingletonName(function, UhtSingletonType.Registered);
			string staticsName = GetSingletonName(function, UhtSingletonType.Statics); 
			string constinitName = GetSingletonName(function, UhtSingletonType.ConstInit);
			bool paramsInStatic = isNoExport || !IsCallbackFunction(function);
			bool isNet = function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetRequest | EFunctionFlags.NetResponse);
			UhtClass? outerClass = function.Outer as UhtClass;

			string strippedFunctionName = function.StrippedFunctionName;
			string eventParameters = GetEventStructParametersName(function.Outer, strippedFunctionName);

			// Everything from this point on will be part of the definition hash
			int hashCodeBlockStart = builder.Length;

			// Collect the properties
			PropertyMemberContextImpl context = new(CodeGenerator, function, "", eventParameters, staticsName);
			UhtUsedDefineScopes<UhtProperty> properties = new(function.Properties);

			// Statics declaration
			{
				builder.Append("struct ").Append(staticsName).Append("\r\n");
				builder.Append("{\r\n");

				if (paramsInStatic)
				{
					List<UhtScriptStruct> noExportStructs = FindNoExportStructs(function);
					foreach (UhtScriptStruct noExportStruct in noExportStructs)
					{
						AppendMirrorsForNoexportStruct(builder, noExportStruct, 1);
					}
					AppendEventParameter(builder, function, strippedFunctionName, UhtPropertyTextType.EventParameterFunctionMember, false, 1, "\r\n");
				}

				builder.AppendMetaDataDecl(function, context, properties, MetaDataParamsName, 1);

				AppendPropertiesDecl(builder, context, properties, 1);
				
				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
				{ 
					using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					if (function.FunctionFlags.HasExactFlags(EFunctionFlags.Native | EFunctionFlags.NetRequest, EFunctionFlags.Native)
						&& function.FunctionExportFlags.HasAllFlags(UhtFunctionExportFlags.CustomThunk))
					{
						// Add an accessor for the probably-private static memeber function ptr in the static struct which can be declared as a friend
						builder.Append($"\tUE_CONSTEVAL static FNativeFuncPtr GetCustomThunk_{function.SourceName}()\r\n")
							.Append("\t{\r\n")
							.Append($"\t\treturn &{(function.Outer as UhtClass)!.NativeFunctionCallName}::exec{function.SourceName};\r\n")
							.Append("\t}\r\n");
					}
				}
				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
				{ 
					using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append("\tstatic const UECodeGen_Private::").AppendFunctionParamsType(function).Append(" FuncParams;\r\n");
				}
				

				builder.Append("};\r\n");
			}

			// Statics definition
			{
				AppendPropertiesDefs(builder, context, properties, 0);
				
				string functionEngineName = isVerseNativeCallable ? function.GetEncodedVerseScopeAndName(UhtVerseNameMode.Default) : function.EngineName;

				string? sizeOfStatic = null;
				string? alignOfStatic = null;
				if (function.Children.Count > 0)
				{
					UhtFunction tempFunction = function;
					while (tempFunction.SuperFunction != null)
					{
						tempFunction = tempFunction.SuperFunction;
					}

					string eventStructName = $"{(paramsInStatic ? staticsName : "")}{(paramsInStatic ? "::" : "")}{GetEventStructParametersName(tempFunction.Outer, tempFunction.StrippedFunctionName)}";
					sizeOfStatic = $"sizeof({eventStructName})";
					alignOfStatic = $"alignof({eventStructName})";
				}

				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
				{ 
					using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append($"UE_CONSTINIT_UOBJECT_DECL TNoDestroy<U{function.EngineClassName}> {constinitName}{{NoDestroyConstEval,\r\n")
						.AppendConstInitObjectParams(function, this, 1);
					builder.Append("\tUE::CodeGen::ConstInit::FUFieldParams{ \r\n")
						.AppendScopeLink(function.NextFunction, UhtDefineScopeNames.WithEditor, context, 2, ".Next = ", ",\r\n")
						.Append("\t},\r\n");
					builder.Append("\tUE::CodeGen::ConstInit::FStructParams{\r\n")
						.Append($"\t\t.Super = ").AppendConstInitSingletonRef(this, function.SuperFunction).Append(",\r\n")
						.Append($"\t\t.ChildProperties = {staticsName}::GetFirstProperty(),\r\n")
						.Append(sizeOfStatic is null ? null : $"\t\t.PropertiesSize = {sizeOfStatic},\r\n")
						.Append(alignOfStatic is null ? null : $"\t\t.MinAlignment = {alignOfStatic},\r\n")
						.Append("\t\tIF_WITH_METADATA(.MetaData = ").AppendMetaDataArrayView(function.MetaData, staticsName, MetaDataParamsName, 0, ",)\r\n")
						.Append("\t},\r\n");
					builder.Append("\tUE::CodeGen::ConstInit::FFunctionParams{\r\n")
						.Append((function.FunctionFlags & EFunctionFlags.Native, function.FunctionFlags & EFunctionFlags.NetRequest, function.FunctionExportFlags & UhtFunctionExportFlags.CustomThunk) switch
						{
							// Native non-service with custom thunk
							(EFunctionFlags.Native, EFunctionFlags.None, UhtFunctionExportFlags.CustomThunk)
								=> $"\t\t.NativeFunction = {staticsName}::GetCustomThunk_{function.SourceName}(),\r\n",
							// Native without custom thunk
							(EFunctionFlags.Native, EFunctionFlags.None, _)
								=> $"\t\t.NativeFunction = &{outerClass!.Namespace.FullSourceName}{outerClass!.NativeFunctionCallName}::exec{function.SourceName},\r\n",
							// Non-native function 
							(EFunctionFlags.None, _, _) => $"\t\t.NativeFunction = &UObject::ProcessInternal,\r\n",
							// No thunk for whatever reason - e.g. net service function
							_ => $"",
						})
						.Append($"\t\t.FunctionFlags = (EFunctionFlags)0x{(uint)function.FunctionFlags:X8}, \r\n")
						.Append(isNet ? $"\t\t.RPCId = {function.RPCId}, \r\n" : null)
						.Append(isNet ? $"\t\t.RPCResponseId = {function.RPCResponseId}, \r\n" : null)
						.Append("\t},\r\n");
					if (function.FunctionType == UhtFunctionType.SparseDelegate)
					{
						builder.Append("\tUE::CodeGen::ConstInit::FSparseDelegateParams{\r\n")
						.Append("\t\tUTF8TEXT(").AppendUTF8LiteralString(function.SparseOwningClassName).Append("), \r\n")
						.Append("\t\tUTF8TEXT(").AppendUTF8LiteralString(function.SparseDelegateName).Append("), \r\n")
						.Append("\t},\r\n");
					}
					if (function.IsVerseField)
					{
						builder.Append("\tUE::CodeGen::ConstInit::FVerseFunctionParams{\r\n")
							.Append("\t\t.AlternateNameUTF8 = IF_WITH_VERSE_VM(")
							.Append("UTF8TEXT(").AppendUTF8LiteralString(function.GetVerseScopeAndName(UhtVerseNameMode.Default)).Append("), ")
							.Append("UTF8TEXT(").AppendUTF8LiteralString(function.GetEncodedVerseScopeAndName(UhtVerseNameMode.Default)).Append(") ")
							.Append("), \r\n")
							.Append("\t},\r\n");
					}
					builder.Append(" };\r\n");
				}

				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
				{ 
					using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append("const UECodeGen_Private::").AppendFunctionParamsType(function).Append(' ')
						.Append(staticsName).Append("::FuncParams = { { ")
						.Append("(UObject*(*)())").Append(GetFunctionOuterFunc(function)).Append(", ")
						.Append(GetSingletonName(function.SuperFunction, UhtSingletonType.Registered)).Append(", ")
						.AppendUTF8LiteralString(functionEngineName).Append(", ")
						.AppendArrayPtrLine(properties, UhtDefineScopeNames.Standard, staticsName, "PropPointers", 1, ", \r\n")
						.AppendArrayCountLine(properties, UhtDefineScopeNames.Standard, staticsName, "PropPointers", 1, ", \r\n")
						.Append(sizeOfStatic ?? "0").Append(",\r\n")
						.Append("RF_Public|RF_Transient|RF_MarkAsNative, ")
						.Append($"(EFunctionFlags)0x{(uint)function.FunctionFlags:X8}, ")
						.Append(isNet ? function.RPCId : 0).Append(", ")
						.Append(isNet ? function.RPCResponseId : 0).Append(", ")
						.AppendMetaDataParams(function, staticsName, MetaDataParamsName)
						.Append("}, ");

					switch (function.FunctionType)
					{
						case UhtFunctionType.Function:
							if (function.IsVerseField)
							{
								builder.Append("\r\n");
								builder.Append("#if WITH_VERSE_VM\r\n");
								builder.AppendUTF8LiteralString(function.GetVerseScopeAndName(UhtVerseNameMode.Default)).Append(",\r\n");
								builder.Append("#else\r\n");
								builder.AppendUTF8LiteralString(function.GetEncodedVerseScopeAndName(UhtVerseNameMode.Default)).Append(",\r\n");
								builder.Append("#endif\r\n");
							}
							break;

						case UhtFunctionType.Delegate:
							break;

						case UhtFunctionType.SparseDelegate:
							builder
								.AppendUTF8LiteralString(function.SparseOwningClassName).Append(", ")
								.AppendUTF8LiteralString(function.SparseDelegateName).Append(", ");
							break;
					}

					builder.Append(" };\r\n");
				}

				if (sizeOfStatic is not null)
				{
					builder.Append("static_assert(").Append(sizeOfStatic).Append(" < MAX_uint16);\r\n");
				}
			}

			// Registration function
			if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
			{
				using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
				builder.Append("UFunction* ").Append(singletonName).Append("()\r\n");
				builder.Append("{\r\n");
				builder.Append("\tstatic UFunction* ReturnFunction = nullptr;\r\n");
				builder.Append("\tif (!ReturnFunction)\r\n");
				builder.Append("\t{\r\n");
				builder.Append("\t\t").AppendFunctionConstructorName(function).Append("(&ReturnFunction, ").Append(staticsName).Append("::FuncParams);\r\n");
				builder.Append("\t}\r\n");
				builder.Append("\treturn ReturnFunction;\r\n");
				builder.Append("}\r\n");
			}

			using (UhtRentedPoolBuffer<char> borrowBuffer = builder.RentPoolBuffer(hashCodeBlockStart, builder.Length - hashCodeBlockStart))
			{
				ObjectInfos[function.ObjectTypeIndex].Hash = UhtHash.GenenerateTextHash(borrowBuffer.Buffer.Memory.Span);
			}
			return builder;
		}

		private string GetFunctionOuterFunc(UhtFunction function)
		{
			if (function.Outer == null)
			{
				return "nullptr";
			}
			else if (function.Outer is UhtPackage package)
			{
				return GetSingletonName(package, UhtSingletonType.Registered);
			}
			else
			{
				return GetSingletonName((UhtObject)function.Outer, UhtSingletonType.Registered);
			}
		}

		private static StringBuilder AppendMirrorsForNoexportStruct(StringBuilder builder, UhtScriptStruct noExportStruct, int tabs)
		{
			builder.AppendTabs(tabs).Append("struct ").Append(noExportStruct.SourceName);
			if (noExportStruct.SuperScriptStruct != null)
			{
				builder.Append(" : public ").Append(noExportStruct.SuperScriptStruct.SourceName);
			}
			builder.Append("\r\n");
			builder.AppendTabs(tabs).Append("{\r\n");

			// Export the struct's CPP properties
			AppendExportProperties(builder, noExportStruct, tabs + 1);

			builder.AppendTabs(tabs).Append("};\r\n");
			builder.Append("\r\n");
			return builder;
		}

		private static StringBuilder AppendExportProperties(StringBuilder builder, UhtScriptStruct scriptStruct, int tabs)
		{
			using (UhtMacroBlockEmitter emitter = new(builder, UhtDefineScopeNames.Standard, UhtDefineScope.None))
			{
				foreach (UhtProperty property in scriptStruct.Properties)
				{
					emitter.Set(property.DefineScope);
					builder.AppendTabs(tabs).AppendFullDecl(property, UhtPropertyTextType.ExportMember, false).Append(";\r\n");
				}
			}
			return builder;
		}

		private StringBuilder AppendPropertiesDecl(StringBuilder builder, IUhtPropertyMemberContext context, UhtUsedDefineScopes<UhtProperty> properties, int tabs)
		{
			using UhtCodeBlockComment block = new(builder, context.OuterStruct, "constinit property declarations");
			if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
			{
				using UhtConditionalMacroBlock macroBlock = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
				if (properties.IsEmpty)
				{
					builder.AppendTabs(tabs).Append("static consteval FProperty* GetFirstProperty() { return nullptr; }\r\n");
				}
				else
				{
					builder.AppendInstances(properties, UhtDefineScopeNames.Standard,
							(builder, property) => builder.AppendConstInitMemberDecl(property, context, property.EngineName, "", tabs))
						// declare consteval function to provide first property for constructor uncondtionally - body will be empty if there are no properties
						.AppendTabs(tabs).Append("static consteval FProperty* GetFirstProperty();\r\n")
						// for properties which have more than one 'next' property depending on preprocessor definitions, define a function to return the correct property
						.AppendInstances(properties, UhtDefineScopeNames.Standard, (builder, property) =>
						{
							property.AppendGetNextProperty(builder, context, tabs);
						});
				}
			}
			if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
			{
				using UhtConditionalMacroBlock macroBlock = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
				builder.AppendInstances(properties, UhtDefineScopeNames.Standard,
					builder => { },
					(builder, property) => builder.AppendMemberDecl(property, context, property.EngineName, "", tabs),
					builder => builder.AppendTabs(tabs).Append("static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];\r\n")
					);
			}
		
			return builder;
		}

		private StringBuilder AppendPropertiesDefs(StringBuilder builder, IUhtPropertyMemberContext context, UhtUsedDefineScopes<UhtProperty> properties, int tabs)
		{
			if (properties.IsEmpty)
			{
				return builder;
			}

			using UhtCodeBlockComment block = new(builder, context.OuterStruct, "Property Definitions");
			if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
			{
				using UhtConditionalMacroBlock macroBlock = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
				builder.AppendInstances(properties, UhtDefineScopeNames.Standard,
						(builder, property) => builder.AppendConstInitMemberDef(property, context, property.EngineName, "", null, null, tabs))
					.AppendTabs(tabs).Append($"consteval FProperty* {context.StaticsName}::GetFirstProperty() {{\r\n")
					.AppendScopeLink(context.OuterStruct.FirstProperty, UhtDefineScopeNames.Standard, context, tabs+1, "return ", ";\r\n")
					.Append("}\r\n");
			}
			if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
			{ 
				using UhtConditionalMacroBlock macroBlock = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
				builder.AppendInstances(properties, UhtDefineScopeNames.Standard,
					(builder, property) => builder.AppendMemberDef(property, context, property.EngineName, "", null, tabs));

				builder.AppendInstances(properties, UhtDefineScopeNames.Standard,
					builder => builder.AppendTabs(tabs).Append("const UECodeGen_Private::FPropertyParamsBase* const ").Append(context.StaticsName).Append("::PropPointers[] = {\r\n"),
					(builder, property) => builder.AppendMemberPtr(property, context, property.EngineName, "", tabs + 1),
					builder =>
					{
						builder.AppendTabs(tabs).Append("};\r\n");
						builder.AppendTabs(tabs).Append("static_assert(UE_ARRAY_COUNT(").Append(context.StaticsName).Append("::PropPointers) < 2048);\r\n");
					});
			}
			return builder;
		}

		private StringBuilder AppendClassFunction(StringBuilder builder, UhtClass classObj, UhtFunction function)
		{
			bool isNotDelegate = !function.FunctionFlags.HasAnyFlags(EFunctionFlags.Delegate);
			bool isRpc = IsRpcFunction(function) && ShouldExportFunction(function);
			bool isCallback = IsCallbackFunction(function);
			bool isVerseNativeCallable = function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.VerseNativeCallable);
			bool isVerseCallable = function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.VerseCallable);

			if (isNotDelegate || isRpc || isCallback)
			{
				using UhtCodeBlockComment blockComment = new(builder, classObj, function);
				using UhtMacroBlockEmitter blockEmitter = new(builder, UhtDefineScopeNames.WithEditor, function.DefineScope);
				if (isCallback)
				{
					AppendEventParameter(builder, function, function.StrippedFunctionName, UhtPropertyTextType.EventParameterMember, true, 0, "\r\n");
					AppendClassFunctionCallback(builder, classObj, function);
					if (classObj.ClassFlags.HasAnyFlags(EClassFlags.Interface))
					{
						AppendInterfaceCallFunction(builder, classObj, function);
					}
				}
				if (!function.FunctionFlags.HasAnyFlags(EFunctionFlags.Delegate))
				{
					AppendFunction(builder, function, classObj.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.NoExport), false);
					if (isVerseNativeCallable)
					{
						AppendVerseNativeCallableFunctionStub(builder, classObj, function);
						AppendVerseNativeCallableFunction(builder, classObj, function, "");
						if (classObj.ClassFlags.HasAnyFlags(EClassFlags.Interface))
						{
							AppendVerseNativeCallableFunction(builder, classObj, function, UhtNames.VerseProxySuffix);
						}
					}
					if (isVerseCallable)
					{
						AppendVerseCallableFunction(builder, classObj, function, "verse_exe_");
					}
				}
				if (isRpc)
				{
					builder.Append($"DEFINE_FUNCTION({classObj.Namespace.FullSourceName}{classObj.NativeFunctionCallName}::{function.UnMarshalAndCallName})\r\n");
					builder.Append("{\r\n");
					AppendFunctionThunk(builder, classObj, function);
					builder.Append("}\r\n");
				}
			}
			return builder;
		}

		private static StringBuilder AppendNativeInterfaceVerseProxyFunctions(StringBuilder builder, UhtClass classObj, UhtClass? funcClassObj)
		{
			if (funcClassObj != null)
			{
				UhtUsedDefineScopes<UhtFunction> verseNativeCallableFunctions = new(funcClassObj.Functions.Where(x => x.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.VerseNativeCallable)));
				builder.Append("\r\n");
				builder.Append("// Verse native callable thunks for ").Append(classObj.SourceName).Append("\r\n");
				AppendVerseNativeCallableProxyFunctions(builder, classObj, verseNativeCallableFunctions);
			}
			return builder;
		}

		private static StringBuilder AppendVerseNativeCallableProxyFunctions(StringBuilder builder, UhtClass classObj, UhtUsedDefineScopes<UhtFunction> verseNativeCallableFunctions)
		{
			return builder.AppendInstances(verseNativeCallableFunctions, UhtDefineScopeNames.Standard,
				(StringBuilder builder, UhtFunction function) =>
				{
					AppendVerseNativeCallableFunction(builder, classObj, function, UhtNames.VerseProxySuffix);
				});
		}

		private static StringBuilder AppendVerseNativeCallableFunctionStub(StringBuilder builder, UhtClass classObj, UhtFunction function)
		{
			UhtProperty? returnProperty = function.ReturnProperty;
			bool isSuspends = function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.VerseSuspends);

			if (returnProperty != null)
			{
				builder.AppendTokens(returnProperty.TypeTokens.AllTokens).Append(' ');
			}
			else
			{
				builder.Append(isSuspends ? "TVerseTask<void> " : "void ");
			}

			builder.Append(classObj.Namespace.FullSourceName).AppendClassSourceNameOrInterfaceName(classObj).Append("::").Append(function.SourceName).Append('(');
			bool needsComma = false;
			foreach (UhtType type in function.ParameterProperties.Span)
			{
				if (type is UhtProperty property)
				{
					if (needsComma)
					{
						builder.Append(", ");
					}
					needsComma = true;
					builder.AppendTokens(property.TypeTokens.AllTokens).Append(' ').Append(property.SourceName);
				}
			}
			builder.Append(")\r\n");
			builder.Append("{\r\n");
			if (returnProperty != null || isSuspends)
			{
				builder.Append("\treturn ");
			}
			else
			{
				builder.Append('\t');
			}
			builder.Append(function.SourceName).Append("(verse::FExecutionContext::GetActiveContext()");
			foreach (UhtType type in function.ParameterProperties.Span)
			{
				if (type is UhtProperty property)
				{
					builder.Append(", std::forward<decltype(").Append(property.SourceName).Append(")>(").Append(property.SourceName).Append(')');
				}
			}
			if (returnProperty != null)
			{
				if (function.Session.IsIncompleteReturn(returnProperty))
				{
					builder.Append(");\r\n");
				}
				else
				{
					builder.Append(").GetValue();\r\n");
				}
			}
			else
			{
				builder.Append(");\r\n");
			}
			builder.Append("}\r\n");
			return builder;
		}

		private static StringBuilder AppendVerseNativeCallableFunction(StringBuilder builder, UhtClass classObj, UhtFunction function, string classNameSuffix)
		{
			UhtProperty? returnProperty = function.ReturnProperty;
			bool isSuspends = returnProperty != null || function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.VerseSuspends);

			builder.AppendVerseNativeCallableSignature(classObj, function, classNameSuffix).Append("\r\n");
			builder.Append("{\r\n");

			// Result value declaration
			if (isSuspends)
			{
				builder.Append('\t').AppendVerseNativeCallableReturnType(function).Append("Result;\r\n");
			}

			// The bodies must differ based on the VM
			builder.Append("#if WITH_VERSE_VM\r\n");
			AppendVerseNativeCallableFunctionBody(builder, classObj, function, classNameSuffix, returnProperty, true);
			builder.Append("#else\r\n");
			AppendVerseNativeCallableFunctionBody(builder, classObj, function, classNameSuffix, returnProperty, false);
			builder.Append("#endif\r\n");

			if (isSuspends)
			{
				builder.Append("\treturn Result;\r\n");
			}
			builder.Append("}\r\n");
			return builder;
		}

		private static StringBuilder AppendVerseNativeCallableFunctionBody(StringBuilder builder, UhtClass classObj, UhtFunction function, string classNameSuffix, UhtProperty? returnProperty, bool isVerseVM)
		{
			builder.Append(isVerseVM ? "\tAutoRTFM::Open([&] {\r\n" : "\t{\r\n");

			// Callee type declaration
			builder.Append("\t\tusing CalleeType = ").AppendVerseNativeCallableTypeDef(function).Append(";\r\n");

			// Callee paths
			string verseScopeAndName = function.GetVerseScopeAndName(UhtVerseNameMode.Default);
			if (isVerseVM)
			{
				builder.Append("\t\tconst char* CalleePath = \"").Append(verseScopeAndName).Append("\";\r\n");
			}
			else
			{
				builder.Append("\t\tconst TCHAR* CalleePath = TEXT(\"").AppendEncodedVerseName(verseScopeAndName).Append("\");\r\n");
			}

			// Callee declaration
			if (classObj.ClassType == UhtClassType.Interface)
			{
				builder.Append("\t\tCalleeType Callee{_V_EXEC_CONTEXT_PARAM_NAME, _getUObject(), CalleePath};\r\n");
			}
			else if (classObj.ClassType == UhtClassType.VModule)
			{
				builder.Append("\t\tCalleeType Callee{_V_EXEC_CONTEXT_PARAM_NAME, V_SAFE_STATIC_CONTEXT(), CalleePath};\r\n");
			}
			else
			{
				builder.Append("\t\tCalleeType Callee{_V_EXEC_CONTEXT_PARAM_NAME, this, CalleePath};\r\n");
			}

			// Invocation
			builder.Append("\t\t");
			if (returnProperty != null || function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.VerseSuspends))
			{
				builder.Append("Result = ");
			}
			builder.Append("Callee(_V_EXEC_CONTEXT_PARAM_NAME");
			builder.AppendVerseFunctionArgs(function, true, false, property => builder.Append(property.SourceName), () => builder.Append("FDecidesContext(FDecidesContext::EDefaultContruct::UnsafeDoNotUse)"));
			builder.Append(");\r\n");

			builder.Append(isVerseVM ? "\t});\r\n" : "\t}\r\n");
			return builder;
		}

		private static StringBuilder AppendVerseCallableFunction(StringBuilder builder, UhtClass classObj, UhtFunction function, string functionPrefix)
		{
			UhtProperty? returnProperty = function.ReturnProperty;

			bool isCoroutine = function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.VerseSuspends);
			bool isNoRollback = function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.VerseNoRollback);

			// Write _exec_ wrapper that checks for native method implementation
			if (isNoRollback)
			{
				builder.Append($"V_DEFINE_EXEC_NOROLLBACK({classObj.Namespace.FullSourceName}{classObj.SourceName}, {function.VerseName})\r\n");
			}
			else if (isCoroutine)
			{
				builder.Append($"V_DEFINE_EXEC_SUSPENDS({classObj.Namespace.FullSourceName}{classObj.SourceName}, {function.VerseName})\r\n");
			}
			else
			{
				builder.Append($"V_DEFINE_EXEC({classObj.Namespace.FullSourceName}{classObj.SourceName}, {function.VerseName})\r\n");
			}

			builder.Append("{\r\n");
			builder.Append($"\tV_CALL_IMPL_NO_PRED({function.VerseName});\r\n");
			builder.Append("}\r\n");

			builder.Append($"V_DEFINE_IMPL_NO_PRED({classObj.Namespace.FullSourceName}{classObj.SourceName}, {function.VerseName})\r\n");
			builder.Append("{\r\n");
			// The bodies must differ based on the VM
			builder.Append("#if WITH_VERSE_VM\r\n");
			AppendVerseCallableFunctionBody(builder, classObj, function, returnProperty, true);
			builder.Append("#else\r\n");
			AppendVerseCallableFunctionBody(builder, classObj, function, returnProperty, false);
			builder.Append("#endif\r\n");
			builder.Append("}\r\n");
			return builder;
		}

		private static StringBuilder AppendVerseCallableFunctionBody(StringBuilder builder, UhtClass classObj, UhtFunction function, UhtProperty? returnProperty, bool isVerseVM)
		{
			bool isModule = classObj.ClassType == UhtClassType.VModule;
			bool isDecides = function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.VerseDecides);
			bool isCoroutine = function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.VerseSuspends);
			bool isExtensionMethod = function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.VerseExtensionMethod);
			bool isIncompleteReturn = function.Session.IsIncompleteReturn(returnProperty);

			// V_MARSHALLING_...
			{
				builder.Append("\tV_MARSHALLING_PARAM_BEGIN\r\n");
				if (isVerseVM || !isCoroutine)
				{
					ReadOnlySpan<UhtType> arguments = function.ParameterProperties.Span;
					if (arguments.Length != 1)
					{
						builder.Append("\tV_MARSHAL_TUPLE_BEGIN\r\n");
					}
					foreach (UhtType argumentType in arguments)
					{
						UhtProperty argument = (UhtProperty)argumentType;
						builder.Append('\t').AppendVerseArgumentUnmarshal(argument, isVerseVM).Append("\r\n");
					}
					if (arguments.Length != 1)
					{
						builder.Append("\tV_MARSHAL_TUPLE_END\r\n");
					}
				}
				builder.Append("\tV_MARSHALLING_END\r\n");
			}

			// V_NATIVE_BEGIN
			{
				builder.Append("\tV_NATIVE_BEGIN(").Append(classObj.SourceName).Append(", V_COMMA_SEPARATED(");
				if (!isVerseVM && isCoroutine)
				{
					builder.Append("FVerseResult");
				}
				else if (returnProperty == null)
				{
					builder.Append("EVerseTrue");
				}
				else
				{
					UhtProperty adjustedReturnProperty = returnProperty;
					if (isVerseVM && adjustedReturnProperty is UhtOptionalProperty optionalProperty)
					{
						adjustedReturnProperty = optionalProperty.ValueProperty;
					}
					builder.AppendTokens(adjustedReturnProperty.TypeTokens.FullDeclarationTokens);
				}
				builder.Append("))\r\n");
			}

			// Coroutine FTask
			if (!isVerseVM && isCoroutine)
			{
				ReadOnlySpan<UhtType> arguments = function.ParameterProperties.Span;
				builder.Append("\tstruct FTask : verse::task\r\n");
				builder.Append("\t{\r\n");
				if (!isModule)
				{
					builder.Append("\t\tV_THIS_CLASS* _Self;\r\n");
				}
				if (isModule && isExtensionMethod && arguments.Length > 0)
				{
					UhtProperty thisProperty = (UhtProperty)arguments[0];
					arguments = arguments[1..];
					builder.Append("\t\t").AppendTokens(thisProperty.TypeTokens.FullDeclarationTokens).Append($" {thisProperty.SourceName};\r\n");
				}
				builder.Append("\t\tstruct\r\n");
				builder.Append("\t\t{\r\n");
				foreach (UhtType argument in arguments)
				{
					UhtProperty property = (UhtProperty)argument;
					builder.Append("\t\t\t").AppendTokens(property.TypeTokens.FullDeclarationTokens).Append($" {property.SourceName};\r\n");
				}
				builder.Append("\t\t};\r\n");
				if (returnProperty != null)
				{
					builder.Append("\t\t").AppendTokens(returnProperty.TypeTokens.FullDeclarationTokens).Append(" _RetVal;\r\n");
				}
				builder.Append("\t};\r\n");
			}

			if (isVerseVM && !isCoroutine && !isDecides && (isIncompleteReturn || returnProperty == null))
			{
				builder.Append("\t__NativeReturnValue.Emplace();");
			}

			// Invocation
			{
				builder.Append('\t');
				if (isCoroutine)
				{
					if (!isVerseVM)
					{
						builder.Append("*__NativeReturnValue = ");
					}
					else
					{
						builder.Append("__ControlFlow = ");
					}
				}
				else if (!isIncompleteReturn && (isDecides || returnProperty != null))
				{
					if (!isVerseVM)
					{
						builder.Append('*');
					}
					builder.Append("__NativeReturnValue = ");
				}
				if (isModule)
				{
					builder.Append($"{classObj.Namespace.FullSourceName}");
				}
				else
				{
					if (!isVerseVM && isCoroutine)
					{
						builder.Append("((FTask*)V_THIS)->_Self->");
					}
					else
					{
						builder.Append("V_THIS->");
					}
				}

				builder.Append(function.SourceName).Append('(');

				bool needsComma = false;
				if (isCoroutine)
				{
					if (!isVerseVM)
					{
						builder.Append("(FTask*)V_THIS");
					}
					else
					{
						builder.Append("{__Context}");
					}
					needsComma = true;
				}

				foreach (UhtType argumentType in function.ParameterProperties.Span)
				{
					if (argumentType is UhtProperty argument)
					{
						if (needsComma)
						{
							builder.Append(", ");
						}
						if (!isVerseVM && isCoroutine)
						{
							builder.Append("((FTask*)V_THIS)->");
						}
						builder.Append(argument.SourceName); // InteropBlock << GetRemappedSymbolString(Param->GetName());
						needsComma = true;
					}
				}
				if (!isCoroutine && isIncompleteReturn)
				{
					if (needsComma)
					{
						builder.Append(", ");
					}
					if (!isVerseVM)
					{
						builder.Append("*__NativeReturnValue");
					}
					else
					{
						if (isDecides)
						{
							builder.Append("__NativeReturnValue");
						}
						else
						{
							builder.Append("*__NativeReturnValue");
						}
					}
				}
				builder.Append(");\r\n");
			}

			if (!isVerseVM && isCoroutine)
			{
				builder.Append("\t((FTask*)V_THIS)->_bEverSuspended = true;\r\n");
			}
			builder.Append("\tV_NATIVE_END(").Append(function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.VerseRTFMAlwaysOpen) ? "true" : "false").Append(")\r\n");
			builder.Append("\tV_REPORT_FUNCTION_CALL(").AppendUTF8LiteralString(function.GetVerseName(UhtVerseNameMode.Default)).Append(", ").AppendUTF8LiteralString(function.GetVerseScope(UhtVerseNameMode.Default)).Append(");\r\n");
			return builder;
		}

		private StringBuilder AppendClassFunctionCallback(StringBuilder builder, UhtClass classObj, UhtFunction function)
		{
			// Net response functions don't go into the VM
			if (classObj.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.NoExport) || function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetResponse))
			{
				return builder;

			}

			bool isInterfaceClass = classObj.ClassFlags.HasAnyFlags(EClassFlags.Interface);
			using UhtMacroBlockEmitter blockEmitter = new(builder, UhtDefineScopeNames.WithEditor, function.DefineScope);

			if (!isInterfaceClass)
			{
				// Do not make this a static const.  It causes issues with live coding
				builder.Append("static FName NAME_").Append(classObj.SourceName).Append('_').Append(function.EngineName).Append(" = FName(TEXT(\"").Append(function.EngineName).Append("\"));\r\n");
			}

			AppendNativeFunctionHeader(builder, function, UhtPropertyTextType.EventFunctionArgOrRetVal, false, null, null, UhtFunctionExportFlags.None, 0, "\r\n");

			if (isInterfaceClass)
			{
				builder.Append("{\r\n");

				// assert if this is ever called directly
				builder
					.Append("\tcheck(0 && \"Do not directly call Event functions in Interfaces. Call Execute_")
					.Append(function.EngineName)
					.Append(" instead.\");\r\n");

				// satisfy compiler if it's expecting a return value
				if (function.ReturnProperty != null)
				{
					string eventParmStructName = GetEventStructParametersName(classObj, function.EngineName);
					builder.Append('\t').Append(eventParmStructName).Append(" Parms;\r\n");
					builder.Append("\treturn Parms.ReturnValue;\r\n");
				}
				builder.Append("}\r\n");
			}
			else
			{
				bool isBlueprintEvent = function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent);
				bool isNetEvent = function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net);
				bool isNetRemoteEvent = isNetEvent && !function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetClient | EFunctionFlags.NetServer | EFunctionFlags.NetMulticast);
				bool isCallInEditor = function.MetaData.ContainsKey(UhtNames.CallInEditor);
				bool isEditorFunction = function.FunctionFlags.HasAnyFlags(EFunctionFlags.EditorOnly);
				bool hasValidCppImpl = (function.StrippedFunctionName != function.CppImplName);

				// This is a small optimization that we can do for BlueprintNativeEvents.
				// If there is a native "_Implementation" of the current function, then we can do a small
				// optimization where we only call "ProcessEvent" if there is actually a BP script override
				// of the native implementation. This saves us the cost of unnecessarily copying the function
				// params to the BPVM if we don't have to. We can only do this optimization
				// if the implementation function name is not the same as the actual C++ function name, as that
				// would just call the function recursively. We cannot do this optimization for networked events
				// because ProcessEvent does some important replication behavior which we do not want to lose.
				bool doNativeImplOptimization = 
					isBlueprintEvent && 
					!isNetEvent && 
					!isCallInEditor && 
					!isEditorFunction &&
					hasValidCppImpl;				
				
				if (!doNativeImplOptimization)
				{
					AppendEventFunctionPrologue(builder, function, function.EngineName, 0, "\r\n", false);

					AppendFindUFunction(builder, classObj, function, 1, "\r\n");
				}				
				else
				{
					builder.Append("{\r\n");

					AppendFindUFunction(builder, classObj, function, 1, "\r\n");

					builder
						.Append("\tif (!Func->GetOwnerClass()->HasAnyClassFlags(CLASS_Native))\r\n")
						.Append("\t{\r\n");

					AppendEventFunctionPrologue(builder, function, function.EngineName, /*tabs*/ 1, "\r\n", /*addEventParameterStruct=*/false,/*addEventParameterStruct*/ false);
				}

				// For remote net functions add them to our tracking stack
				if (isNetRemoteEvent)
				{
					builder.Append("\tUE::Net::Private::FScopedRemoteRPCMode CallingRemoteRPC(Func, UE::Net::Private::ERemoteFunctionMode::Sending);\r\n");
				}

				// Cast away const just in case, because ProcessEvent isn't const
				builder.Append('\t');
				if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.Const))
				{
					builder.AppendTabs(1).Append("const_cast<").Append(classObj.SourceName).Append("*>(this)->");
				}

				builder
					.Append("ProcessEvent(Func,")
					.Append(function.Children.Count > 0 ? "&Parms" : "NULL")
					.Append(");\r\n");

				// Call into the native implementation of the function if there is one
				// We don't want to do this all the time, like for BlueprintInternalUseOnly functions
				// which will not have a "_Implementation" appended to their name
				if (doNativeImplOptimization)
				{
					AppendEventFunctionEpilogue(builder, function, /*tabs*/ 1, "\r\n", /*bAddFunctionScopeBracket=*/true);

					builder
						.AppendTabs(1)
						.Append("else\r\n")
						.AppendTabs(1)
						.Append("{\r\n")
						.AppendTabs(2)
						.Append(function.HasReturnProperty ? "return " : "");

					// Cast away const just in case, because ProcessEvent isn't const
					if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.Const))
					{
						builder.Append("const_cast<").Append(classObj.SourceName).Append("*>(this)->");
					}

					// Begin the native function call...
					builder
						.Append(function.CppImplName)
						.Append('(');

					// For every param in this function call, pass it to our native C++ function
					int numParams = function.ParameterProperties.Length;
					ReadOnlySpan<UhtType> paramSpan = function.ParameterProperties.Span;
					for (int i = 0; i < numParams; i++)
					{
						UhtType parameter = paramSpan[i];

						if (parameter is UhtProperty property)
						{
							builder.Append(property.SourceName);
						}

						// Add a "," between function params as long as it isn't the last one
						if ((i + 1) != numParams)
						{
							builder.Append(", ");
						}
					}

					// ...close the function call
					builder
						.Append(");\r\n")
						.AppendTabs(1)
						.Append("}\r\n}\r\n");
				}
				else
				{
					AppendEventFunctionEpilogue(builder, function, /*tabs*/ 0, "\r\n", /*bAddFunctionScopeBracket=*/true);
				}			
			}
			
			return builder;
		}

		private StringBuilder AppendInterfaceCallFunction(StringBuilder builder, UhtClass classObj, UhtFunction function)
		{
			builder.Append("static FName NAME_").Append(function.Outer?.SourceName).Append('_').Append(function.SourceName).Append(" = FName(TEXT(\"").Append(function.EngineName).Append("\"));\r\n");
			string extraParameter = function.FunctionFlags.HasAnyFlags(EFunctionFlags.Const) ? "const UObject* O" : "UObject* O";
			AppendNativeFunctionHeader(builder, function, UhtPropertyTextType.InterfaceFunctionArgOrRetVal, false, null, extraParameter, UhtFunctionExportFlags.None, 0, "\r\n");
			builder.Append("{\r\n");
			builder.Append("\tcheck(O != NULL);\r\n");
			builder.Append("\tcheck(O->GetClass()->ImplementsInterface(").Append(classObj.SourceName).Append("::StaticClass()));\r\n");
			if (function.Children.Count > 0)
			{
				builder.Append('\t').Append(GetEventStructParametersName(classObj, function.StrippedFunctionName)).Append(" Parms;\r\n");
			}
			builder.Append("\tUFunction* const Func = O->FindFunction(NAME_").Append(function.Outer?.SourceName).Append('_').Append(function.SourceName).Append(");\r\n");
			builder.Append("\tif (Func)\r\n");
			builder.Append("\t{\r\n");
			foreach (UhtType parameter in function.ParameterProperties.Span)
			{
				if (parameter is UhtProperty property)
				{
					builder.Append("\t\tParms.").Append(property.SourceName).Append("=std::move(").Append(property.SourceName).Append(");\r\n");
				}
			}
			builder
				.Append("\t\t")
				.Append(function.FunctionFlags.HasAnyFlags(EFunctionFlags.Const) ? "const_cast<UObject*>(O)" : "O")
				.Append("->ProcessEvent(Func, ")
				.Append(function.Children.Count > 0 ? "&Parms" : "NULL")
				.Append(");\r\n");
			foreach (UhtType parameter in function.ParameterProperties.Span)
			{
				if (parameter is UhtProperty property)
				{
					if (property.PropertyFlags.HasExactFlags(EPropertyFlags.OutParm | EPropertyFlags.ConstParm, EPropertyFlags.OutParm))
					{
						builder.Append("\t\t").Append(property.SourceName).Append("=std::move(Parms.").Append(property.SourceName).Append(");\r\n");
					}
				}
			}
			builder.Append("\t}\r\n");

			// else clause to call back into native if it's a BlueprintNativeEvent
			if (function.FunctionFlags.HasAnyFlags(EFunctionFlags.Native))
			{
				builder
					.Append("\telse if (auto I = (")
					.Append(function.FunctionFlags.HasAnyFlags(EFunctionFlags.Const) ? "const I" : "I")
					.Append(classObj.SourceName[1..])
					.Append("*)(O->GetNativeInterfaceAddress(")
					.Append(classObj.SourceName)
					.Append("::StaticClass())))\r\n");
				builder.Append("\t{\r\n");
				builder.Append("\t\t");
				if (function.HasReturnProperty)
				{
					builder.Append("Parms.ReturnValue = ");
				}
				builder.Append("I->").Append(function.SourceName).Append("_Implementation(");

				bool first = true;
				foreach (UhtType parameter in function.ParameterProperties.Span)
				{
					if (parameter is UhtProperty property)
					{
						if (!first)
						{
							builder.Append(',');
						}
						first = false;
						builder.Append(property.SourceName);
					}
				}
				builder.Append(");\r\n");
				builder.Append("\t}\r\n");
			}

			if (function.HasReturnProperty)
			{
				builder.Append("\treturn Parms.ReturnValue;\r\n");
			}
			builder.Append("}\r\n");
			return builder;
		}

		private StringBuilder AppendClassDeclaration(StringBuilder builder, UhtClass classObj)
		{

			// If we are a VMODULE, we need to emit the declaration
			if (classObj.ClassType == UhtClassType.VModule)
			{
				classObj.Namespace.AppendMultipleLines(builder, (builder) =>
				{
					builder.Append($"class {classObj.SourceName} : public UObject\r\n");
					builder.Append("{\r\n");
					builder.Append('\t').AppendMacroName(this, classObj, GeneratedBodyMacroSuffix).Append("\r\n");
					builder.Append("};\r\n");
				});
			}
			return builder;
		}

		private StringBuilder AppendClass(StringBuilder builder, UhtClass classObj, UhtUsedDefineScopes<UhtFunction> functions)
		{
			// Add the auto getters/setters
			AppendAutoGettersSetters(builder, classObj);

			// Add the accessors
			AppendPropertyAccessors(builder, classObj);

			// Add sparse accessors
			AppendSparseAccessors(builder, classObj);

			AppendNativeGeneratedInitCode(builder, classObj, functions);

			if (!classObj.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.HasConstructor))
			{
				if (!classObj.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.UsesGeneratedBodyLegacy))
				{
					switch (GetConstructorType(classObj))
					{
						case ConstructorType.ObjectInitializer:
							builder.Append(classObj.Namespace.FullSourceName).Append(classObj.SourceName).Append("::").Append(classObj.SourceName).Append("(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}\r\n");
							break;

						case ConstructorType.Default:
							builder.Append(classObj.Namespace.FullSourceName).Append(classObj.SourceName).Append("::").Append(classObj.SourceName).Append("() {}\r\n");
							break;
					}
				}
			}

			if (!classObj.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.HasCustomVTableHelperConstructor))
			{
				builder.Append($"DEFINE_VTABLE_PTR_HELPER_CTOR_NS({classObj.Namespace.FullSourceName}, {classObj.SourceName});\r\n");
			}

			if (!classObj.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.HasDestructor) && !classObj.ClassFlags.HasAnyFlags(EClassFlags.Interface))
			{
				builder.Append($"{classObj.Namespace.FullSourceName}{classObj.SourceName}::~{classObj.SourceName}() {{}}\r\n");
			}

			AppendFieldNotify(builder, classObj);

			// Only write out adapters if the user has provided one or the other of the Serialize overloads
			if (classObj.SerializerArchiveType != UhtSerializerArchiveType.None && classObj.SerializerArchiveType != UhtSerializerArchiveType.All)
			{
				AppendSerializer(builder, classObj, UhtSerializerArchiveType.Archive, "IMPLEMENT_FARCHIVE_SERIALIZER");
				AppendSerializer(builder, classObj, UhtSerializerArchiveType.StructuredArchiveRecord, "IMPLEMENT_FSTRUCTUREDARCHIVE_SERIALIZER");
			}
			return builder;
		}

		private static StringBuilder AppendFieldNotify(StringBuilder builder, UhtClass classObj)
		{
			if (!NeedFieldNotifyCodeGen(classObj))
			{
				return builder;
			}

			UhtUsedDefineScopes<UhtType> notifyTypes = GetFieldNotifyTypes(classObj);

			//UE_FIELD_NOTIFICATION_DECLARE_FIELD
			builder.AppendInstances(notifyTypes, UhtDefineScopeNames.Standard,
				(builder, notifyType) =>
				{
					builder.Append($"\tUE_FIELD_NOTIFICATION_IMPLEMENT_FIELD({classObj.SourceName}, {GetNotifyTypeName(notifyType)})\r\n");
				});

			//UE_FIELD_NOTIFICATION_DECLARE_ENUM_FIELD
			builder.AppendInstances(notifyTypes, UhtDefineScopeNames.Standard,
				builder => builder.Append($"\tUE_FIELD_NOTIFICATION_IMPLEMENTATION_BEGIN({classObj.SourceName})\r\n"),
				(builder, notifyType) =>
				{
					builder.Append($"\tUE_FIELD_NOTIFICATION_IMPLEMENT_ENUM_FIELD({classObj.SourceName}, {GetNotifyTypeName(notifyType)})\r\n");
				}, 
				builder => builder.Append($"\tUE_FIELD_NOTIFICATION_IMPLEMENTATION_END({classObj.SourceName});\r\n"));

			return builder;
		}

		private static StringBuilder AppendAutoGettersSetters(StringBuilder builder, UhtClass classObj)
		{
			UhtUsedDefineScopes<UhtProperty> autoGetterSetterProperties = GetAutoGetterSetterProperties(classObj);
			if (autoGetterSetterProperties.IsEmpty)
			{
				return builder;
			}

			return builder.AppendInstances(autoGetterSetterProperties, UhtDefineScopeNames.Standard,
				(builder, property) =>
				{
					if (property.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.GetterSpecifiedAuto))
					{
						string getterCallText = property.Getter ?? "Get" + property.SourceName;
						builder.AppendPropertyText(property, UhtPropertyTextType.GetterRetVal).Append(classObj.Namespace.FullSourceName).Append(classObj.SourceName).Append("::").Append(getterCallText).Append("() const\r\n");
						builder.Append("{\r\n");
						builder.Append("\treturn ").Append(property.SourceName).Append(";\r\n");
						builder.Append("}\r\n");
					}
					if (property.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.SetterSpecifiedAuto))
					{
						string setterCallText = property.Setter ?? "Set" + property.SourceName;
						builder.Append("void ").Append(classObj.Namespace.FullSourceName).Append(classObj.SourceName).Append("::").Append(setterCallText).Append('(').AppendPropertyText(property, UhtPropertyTextType.SetterParameterArgType).Append("InValue").Append(")\r\n");
						builder.Append("{\r\n");
						// @todo: setter defn
						builder.Append("}\r\n");
					}
				});
		}
		
		private static StringBuilder AppendPropertyAccessors(StringBuilder builder, UhtClass classObj)
		{
			foreach (UhtType type in classObj.Children)
			{
				if (type is UhtProperty property)
				{
					if (property.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.GetterFound))
					{
						builder.Append("void ").Append(classObj.Namespace.FullSourceName).Append(classObj.SourceName).Append("::").AppendPropertyGetterWrapperName(property).Append("(const void* Object, void* OutValue)\r\n");
						builder.Append("{\r\n");
						using (UhtMacroBlockEmitter blockEmitter = new(builder, UhtDefineScopeNames.Standard, property.DefineScope))
						{
							builder.Append("\tconst ").Append(classObj.SourceName).Append("* Obj = (const ").Append(classObj.SourceName).Append("*)Object;\r\n");
							if (property.IsStaticArray)
							{
								builder
									.Append('\t')
									.AppendPropertyText(property, UhtPropertyTextType.GetterSetterArg)
									.Append("* Source = (")
									.AppendPropertyText(property, UhtPropertyTextType.GetterSetterArg)
									.Append("*)Obj->")
									.Append(property.Getter!)
									.Append("();\r\n");
								builder
									.Append('\t')
									.AppendPropertyText(property, UhtPropertyTextType.GetterSetterArg)
									.Append("* Result = (")
									.AppendPropertyText(property, UhtPropertyTextType.GetterSetterArg)
									.Append("*)OutValue;\r\n");
								builder
									.Append("\tCopyAssignItems(Result, Source, ")
									.Append(property.ArrayDimensions)
									.Append(");\r\n");
							}
							else if (property is UhtByteProperty byteProperty && byteProperty.Enum != null)
							{
								// If someone passed in a TEnumAsByte instead of the actual enum value, the cast in the else clause would cause an issue.
								// Since this is known to be a TEnumAsByte, we just fetch the first byte.  *HOWEVER* on MSB machines where 
								// the actual enum value is passed in this will fail and return zero if the native size of the enum > 1 byte.
								builder
									.Append('\t')
									.Append("uint8")
									.Append("& Result = *(")
									.Append("uint8")
									.Append("*)OutValue;\r\n");
								builder
									.Append("\tResult = (")
									.Append("uint8")
									.Append(")Obj->")
									.Append(property.Getter!)
									.Append("();\r\n");
							}
							else
							{
								builder
									.Append('\t')
									.AppendPropertyText(property, UhtPropertyTextType.GetterSetterArg)
									.Append("& Result = *(")
									.AppendPropertyText(property, UhtPropertyTextType.GetterSetterArg)
									.Append("*)OutValue;\r\n");
								builder
									.Append("\tResult = (")
									.AppendPropertyText(property, UhtPropertyTextType.GetterSetterArg)
									.Append(")Obj->")
									.Append(property.Getter!)
									.Append("();\r\n");
							}
						}
						builder.Append("}\r\n");
					}
					if (property.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.SetterFound))
					{
						builder.Append("void ").Append(classObj.Namespace.FullSourceName).Append(classObj.SourceName).Append("::").AppendPropertySetterWrapperName(property).Append("(void* Object, const void* InValue)\r\n");
						builder.Append("{\r\n");
						using (UhtMacroBlockEmitter blockEmitter = new(builder, UhtDefineScopeNames.Standard, property.DefineScope))
						{
							builder.Append('\t').Append(classObj.SourceName).Append("* Obj = (").Append(classObj.SourceName).Append("*)Object;\r\n");
							if (property.IsStaticArray)
							{
								builder
									.Append('\t')
									.AppendPropertyText(property, UhtPropertyTextType.GetterSetterArg)
									.Append("* Value = (")
									.AppendPropertyText(property, UhtPropertyTextType.GetterSetterArg)
									.Append("*)InValue;\r\n");
							}
							else if (property is UhtByteProperty byteProperty && byteProperty.Enum != null)
							{
								// If someone passed in a TEnumAsByte instead of the actual enum value, the cast in the else clause would cause an issue.
								// Since this is known to be a TEnumAsByte, we just fetch the first byte.  *HOWEVER* on MSB machines where 
								// the actual enum value is passed in this will fail and return zero if the native size of the enum > 1 byte.
								builder
									.Append('\t')
									.AppendPropertyText(property, UhtPropertyTextType.GetterSetterArg)
									.Append(" Value = (")
									.AppendPropertyText(property, UhtPropertyTextType.GetterSetterArg)
									.Append(")*(uint8*)InValue;\r\n");
							}
							else
							{
								builder
									.Append('\t')
									.AppendPropertyText(property, UhtPropertyTextType.GetterSetterArg)
									.Append("& Value = *(")
									.AppendPropertyText(property, UhtPropertyTextType.GetterSetterArg)
									.Append("*)InValue;\r\n");
							}
							builder
								.Append("\tObj->")
								.Append(property.Setter!)
								.Append("(Value);\r\n");
						}
						builder.Append("}\r\n");
					}
				}
			}
			return builder;
		}

		private static StringBuilder AppendSparseAccessors(StringBuilder builder, UhtClass classObj)
		{
			foreach (UhtScriptStruct sparseScriptStruct in GetSparseDataStructsToExport(classObj))
			{
				string sparseDataType = sparseScriptStruct.EngineName;

				builder.Append('F').Append(sparseDataType).Append("* ").Append(classObj.SourceName).Append("::Get").Append(sparseDataType).Append("() const \r\n");
				builder.Append("{\r\n");
				builder.Append("\treturn static_cast<F").Append(sparseDataType).Append("*>(GetClass()->GetOrCreateSparseClassData());\r\n");
				builder.Append("}\r\n");

				builder.Append('F').Append(sparseDataType).Append("* ").Append(classObj.SourceName).Append("::GetMutable").Append(sparseDataType).Append("() const \r\n");
				builder.Append("{\r\n");
				builder.Append("\treturn static_cast<F").Append(sparseDataType).Append("*>(GetClass()->GetOrCreateSparseClassData());\r\n");
				builder.Append("}\r\n");

				builder.Append("const F").Append(sparseDataType).Append("* ").Append(classObj.SourceName).Append("::Get").Append(sparseDataType).Append("(EGetSparseClassDataMethod GetMethod) const\r\n");
				builder.Append("{\r\n");
				builder.Append("\treturn static_cast<const F").Append(sparseDataType).Append("*>(GetClass()->GetSparseClassData(GetMethod));\r\n");
				builder.Append("}\r\n");

				builder.Append("UScriptStruct* ").Append(classObj.SourceName).Append("::StaticGet").Append(sparseDataType).Append("ScriptStruct()\r\n");
				builder.Append("{\r\n");
				builder.Append("\treturn F").Append(sparseDataType).Append("::StaticStruct();\r\n");
				builder.Append("}\r\n");
			}
			return builder;
		}

		private static StringBuilder AppendSerializer(StringBuilder builder, UhtClass classObj, UhtSerializerArchiveType serializerType, string macroText)
		{
			if (!classObj.SerializerArchiveType.HasAnyFlags(serializerType))
			{
				builder.AppendScoped(UhtDefineScopeNames.Standard, classObj.SerializerDefineScope,
					builder => builder.Append(macroText).Append('(').Append(classObj.Namespace.FullSourceName).Append(classObj.SourceName).Append(")\r\n"));
			}
			return builder;
		}

#pragma warning disable CA1505 //  'AppendNativeGeneratedInitCode' has a maintainability index of '5'. Rewrite or refactor the code to increase its maintainability index (MI) above '9'. 

		private StringBuilder AppendNativeGeneratedInitCode(StringBuilder builder, UhtClass classObj, UhtUsedDefineScopes<UhtFunction> functions)
		{
			const string MetaDataParamsName = "Class_MetaDataParams";
			string singletonName = GetSingletonName(classObj, UhtSingletonType.Registered);
			string staticsName = singletonName + "_Statics";
			string constinitName = GetSingletonName(classObj, UhtSingletonType.ConstInit);
			string registrationName = $"Z_Registration_Info_UClass_{classObj.SourceName}";
			string[]? sparseDataTypes = classObj.MetaData.GetStringArray(UhtNames.SparseClassDataTypes);

			PropertyMemberContextImpl context = new(CodeGenerator, classObj, staticsName);

			bool hasInterfaces = classObj.Bases.Any(x => x is UhtClass baseClass && baseClass.ClassFlags.HasAnyFlags(EClassFlags.Interface)) || classObj.FlattenedVerseInterfaces.Count > 0;

			// This block of code is the contents of IMPLEMENT_CLASS_NO_AUTO_REGISTRATION 
			if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
			{
				using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
				builder.Append($"UClass* {classObj.Namespace.FullSourceName}{classObj.SourceName}::GetPrivateStaticClass()\r\n");
				builder.Append("{\r\n");
				builder.Append($"\treturn &{GetSingletonName(classObj, UhtSingletonType.ConstInit)};\r\n");
				builder.Append("}\r\n");
			}
			if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
			{
				using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
				builder.Append("FClassRegistrationInfo ").Append(registrationName).Append(";\r\n");
				builder.Append("UClass* ").Append(classObj.Namespace.FullSourceName).Append(classObj.SourceName).Append("::GetPrivateStaticClass()\r\n");
				builder.Append("{\r\n");
				builder.Append("\tusing TClass = ").Append(classObj.Namespace.FullSourceName).Append(classObj.SourceName).Append(";\r\n");
				builder.Append("\tif (!").Append(registrationName).Append(".InnerSingleton)\r\n");
				builder.Append("\t{\r\n");
				builder.Append("\t\t").Append(classObj.IsVerseField ? "Verse::CodeGen::Private::ConstructUVerseClassNoInit" : "GetPrivateStaticClassBody").Append("(\r\n");
				builder.Append("\t\t\tTClass::StaticPackage(),\r\n");
				builder.Append("\t\t\tTEXT(\"").Append(classObj.EngineName).Append("\"),\r\n");
				builder.Append("\t\t\t").Append(registrationName).Append(".InnerSingleton,\r\n");
				builder.Append("\t\t\tStaticRegisterNatives").Append(classObj.SourceName).Append(",\r\n");
				builder.Append("\t\t\tsizeof(TClass),\r\n");
				builder.Append("\t\t\talignof(TClass),\r\n");
				builder.Append("\t\t\tTClass::StaticClassFlags,\r\n");
				builder.Append("\t\t\tTClass::StaticClassCastFlags(),\r\n");
				builder.Append("\t\t\tTClass::StaticConfigName(),\r\n");
				builder.Append("\t\t\t(UClass::ClassConstructorType)InternalConstructor<TClass>,\r\n");
				builder.Append("\t\t\t(UClass::ClassVTableHelperCtorCallerType)InternalVTableHelperCtorCaller<TClass>,\r\n");
				builder.Append("\t\t\tUOBJECT_CPPCLASS_STATICFUNCTIONS_FORCLASS(TClass),\r\n");
				builder.Append("\t\t\t&TClass::Super::StaticClass,\r\n");
				builder.Append("\t\t\t&TClass::WithinClass::StaticClass\r\n");
				builder.Append("\t\t);\r\n");
				builder.Append("\t}\r\n");
				builder.Append("\treturn ").Append(registrationName).Append(".InnerSingleton;\r\n");
				builder.Append("}\r\n");
			}

			// Everything from this point on will be part of the definition hash
			int hashCodeBlockStart = builder.Length;

			// simple ::StaticClass wrapper to avoid header, link and DLL hell
			{
				builder.Append("UClass* ").Append(GetSingletonName(classObj, UhtSingletonType.Unregistered)).Append("()\r\n");
				builder.Append("{\r\n");
				builder.Append("\treturn ").Append(classObj.Namespace.FullSourceName).Append(classObj.SourceName).Append("::GetPrivateStaticClass();\r\n");
				builder.Append("}\r\n");
			}

			// Collect the properties
			UhtUsedDefineScopes<UhtProperty> properties = new(classObj.Properties);
			UhtUsedDefineScopes<UhtFunction> nativeFunctions = new();
			bool hasNativeFunctions = false;
			bool hasVerseCallableFunctions = false;

			// Declare the statics object
			{
				builder.Append("struct ").Append(staticsName).Append("\r\n");
				builder.Append("{\r\n");

				builder.AppendMetaDataDecl(classObj, context, properties, MetaDataParamsName, 1);

				AppendPropertiesDecl(builder, context, properties, 1);

				if (!classObj.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.NoExport))
				{
					// Filter to only native non-net functions for exporting
					nativeFunctions.AddRange(functions.Instances.Where(x => x.FunctionFlags.HasExactFlags(EFunctionFlags.Native | EFunctionFlags.NetRequest, EFunctionFlags.Native)));
					hasNativeFunctions = !nativeFunctions.IsEmpty;
					hasVerseCallableFunctions = nativeFunctions.Instances.Any(x => x.IsVerseField && x.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.VerseCallable));
					if (hasNativeFunctions)
					{
						using UhtMacroBlockEmitter blockEmitter = new(builder, UhtDefineScopeNames.WithEditor, nativeFunctions.SoleScope);
						builder.Append("\tstatic constexpr UE::CodeGen::FClassNativeFunction Funcs[] = {\r\n");

						foreach (UhtFunction function in nativeFunctions.Instances)
						{
							blockEmitter.Set(function.DefineScope);
							builder
								.Append("\t\t{ .NameUTF8 = UTF8TEXT(")
								.AppendUTF8LiteralString(function.EngineName)
								.Append("), .Pointer = &")
								.AppendClassSourceNameOrInterfaceName(classObj)
								.Append($"::exec{function.EngineName} }},\r\n");
						}

						if (hasVerseCallableFunctions)
						{
							builder.Append("#if WITH_VERSE_BPVM\r\n");
							foreach (UhtFunction function in nativeFunctions.Instances)
							{
								if (function.IsVerseField && function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.VerseCallable))
								{
									blockEmitter.Set(function.DefineScope);
									builder
										.Append("\t\t{ UTF8TEXT(")
										.AppendUTF8LiteralString(function.GetEncodedVerseScopeAndName(UhtVerseNameMode.Default))
										.Append("), &")
										.AppendClassSourceNameOrInterfaceName(classObj)
										.Append($"::_exec_{function.SourceName}___ }},\r\n");
								}
							}
							builder.Append("#endif\r\n");
						}

						// This will close the block if we have one that isn't editor only
						blockEmitter.Set(nativeFunctions.SoleScope);

						builder.Append("\t};\r\n");
					}

					if (hasVerseCallableFunctions)
					{
						builder.Append("#if WITH_VERSE_VM\r\n");
						using UhtMacroBlockEmitter blockEmitter = new(builder, UhtDefineScopeNames.WithEditor, nativeFunctions.SoleScope);
						builder.Append("\tstatic constexpr FVerseCallableThunk VerseFuncs[] = {\r\n");

						foreach (UhtFunction function in nativeFunctions.Instances)
						{
							if (function.IsVerseField && function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.VerseCallable))
							{
								blockEmitter.Set(function.DefineScope);
								builder
									.Append("\t\t{ ")
									.AppendUTF8LiteralString(function.GetVerseScopeAndName(UhtVerseNameMode.Default))
									.Append(", &")
									.AppendClassSourceNameOrInterfaceName(classObj)
									.Append($"::_exec_{function.SourceName}___ }},\r\n");
							}
						}

						// This will close the block if we have one that isn't editor only
						blockEmitter.Set(nativeFunctions.SoleScope);

						builder.Append("\t};\r\n");
						builder.Append("#endif\r\n");
					}
				}

				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
				{
					using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append("\tstatic UObject* (*const DependentSingletons[])();\r\n");
				}

				// Functions
				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
				{
					using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append("\tstatic consteval UFunction* GetFirstFunction()\r\n");
					builder.Append("\t{\r\n");
					builder.AppendScopeLink(classObj.FirstFunction, UhtDefineScopeNames.WithEditor, context, 2, "return ", ";\r\n");
					builder.Append("\t}\r\n");
				}
				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
				{
					using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.AppendInstances(functions, UhtDefineScopeNames.WithEditor,
						builder =>
						{
							builder.Append("\tstatic constexpr FClassFunctionLinkInfo ").Append("FuncInfo[] = {\r\n");
						},
						(builder, function) =>
						{
							builder
								.Append("\t\t{ &")
								.Append(GetSingletonName(function, UhtSingletonType.Registered))
								.Append(", ")
								.AppendUTF8LiteralString(function.EngineName)
								.Append(" },")
								.AppendObjectHash(classObj, context, function)
								.Append("\r\n");
						},
						builder =>
						{
							builder.Append("\t};\r\n");
							builder.Append("\tstatic_assert(UE_ARRAY_COUNT(").Append("FuncInfo) < 2048);\r\n");
						});
				}

				if (hasInterfaces)
				{
					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
					{
						using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
						builder.Append("\tstatic const UE::CodeGen::ConstInit::FClassImplementedInterface Interfaces[];\r\n");
					}
					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
					{
						using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
						builder.Append("\tstatic const UECodeGen_Private::FImplementedInterfaceParams InterfaceParams[];\r\n");
					}
				}

				builder.Append("\tstatic constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {\r\n");
				builder.Append($"\t\tTCppClassTypeTraits<{classObj.Namespace.FullSourceName}{classObj.NativeFunctionCallName}>::IsAbstract,\r\n");
				builder.Append("\t};\r\n");

				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
				{
					using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append("\tstatic UE_CONSTINIT_UOBJECT_DECL inline const FStructBaseChain* StructBases[] = {\r\n");
					if (classObj.SuperClass is not null)
					{
						List<UhtClass> supers = new();
						for (UhtClass? super = classObj.SuperClass; super is not null; super = super.SuperClass)
						{
							supers.Add(super);
						}
						foreach (UhtClass super in supers.AsEnumerable().Reverse())
						{
							builder.Append("\t\tUE::Private::AsStructBaseChain(").AppendConstInitSingletonRef(this, super).Append("),\r\n");
						}
					}
					builder.Append("\t\tUE::Private::AsStructBaseChain(").AppendConstInitSingletonRef(this, classObj).Append("),\r\n");
					builder.Append("\t};\r\n");
				}

				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
				{
					using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append("\tstatic const UECodeGen_Private::").AppendClassParamsType(classObj).Append(" ClassParams;\r\n");
				}

				builder.Append($"}}; // struct {staticsName}\r\n");
			}

			// Define the statics object
			{
				AppendPropertiesDefs(builder, context, properties, 0);

				// Dependent singletons
				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
				{
					using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append("UObject* (*const ").Append(staticsName).Append("::DependentSingletons[])() = {\r\n");
					if (classObj.SuperClass != null && classObj.SuperClass != classObj)
					{
						builder.Append("\t(UObject* (*)())").Append(GetSingletonName(classObj.SuperClass, UhtSingletonType.Registered)).Append(",\r\n");
					}
					builder.Append("\t(UObject* (*)())").Append(GetSingletonName(classObj.Package, UhtSingletonType.Registered)).Append(",\r\n");
					builder.Append("};\r\n");
					builder.Append("static_assert(UE_ARRAY_COUNT(").Append(staticsName).Append("::DependentSingletons) < 16);\r\n");
				}

				// Implemented interfaces
				if (hasInterfaces)
				{
					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
					{
						using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
						builder.Append($"const UE::CodeGen::ConstInit::FClassImplementedInterface {staticsName}::Interfaces[] = {{\r\n");
						Action<UhtClass> appendConstInitInterfaceParam = (interfaceObj) =>
						{
							if (interfaceObj.ClassFlags.HasAnyFlags(EClassFlags.Interface))
							{
								string isVerseDirectInterface = interfaceObj.IsVerseField && classObj.VerseInterfaces.Contains(interfaceObj) ? "true" : "false";
								builder.Append("\t{ ")
									.Append(".Class = ")
									.AppendSingletonRef(context, interfaceObj.AlternateObject, UhtSingletonType.ConstInit).Append(", ")
									.Append(interfaceObj.IsVerseField
										? ".PointerOffset= 0, .bImplementedByK2 = true, "
										: $".PointerOffset = (int32)VTABLE_OFFSET({classObj.FullyQualifiedSourceName}, {interfaceObj.FullyQualifiedSourceName}), .bImplementedByK2 = false, ")
									.Append($".bVerseDirectInterface = {isVerseDirectInterface}, ")
									.Append("},")
									.AppendObjectHash(classObj, context, interfaceObj.AlternateObject)
									.Append("\r\n");
							}
						};
						foreach (UhtStruct structObj in classObj.Bases)
						{
							if (structObj is UhtClass interfaceObj)
							{
								appendConstInitInterfaceParam(interfaceObj);
							}
						}
						foreach (UhtClass interfaceObj in classObj.FlattenedVerseInterfaces)
						{
							appendConstInitInterfaceParam(interfaceObj);
						}
						builder.Append("};\r\n");
					}
					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
					{
						using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
						builder.Append($"const UECodeGen_Private::FImplementedInterfaceParams {staticsName}::InterfaceParams[] = {{\r\n");
						foreach (UhtStruct structObj in classObj.Bases)
						{
							if (structObj is UhtClass interfaceObj)
							{
								AppendImplementedInterfaceParam(builder, context, classObj, interfaceObj, UhtSingletonType.Unregistered);
							}
						}
						foreach (UhtClass interfaceObj in classObj.FlattenedVerseInterfaces)
						{
							AppendImplementedInterfaceParam(builder, context, classObj, interfaceObj, UhtSingletonType.Unregistered);
						}
						builder.Append("};\r\n");
					}
				}

				EClassFlags classFlags = classObj.ClassFlags & EClassFlags.SaveInCompiledInClasses;
				// Propagate class flags without allowing removal for UClass::ClassFlags
				for (UhtClass? superClass = classObj.SuperClass; superClass is not null; superClass = superClass.SuperClass)
				{
					classFlags |= superClass.ClassFlags & EClassFlags.SaveInCompiledInClasses & EClassFlags.Inherit;
				}
				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
				{
					// constinit class definition
					using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append($"UE_CONSTINIT_UOBJECT_DECL TNoDestroy<U{classObj.EngineClassName}> {constinitName}{{NoDestroyConstEval,\r\n")
						.AppendConstInitObjectParams(classObj, this, 1, "|RF_Standalone")
						.Append("\tUE::CodeGen::ConstInit::FUFieldParams{},\r\n")
						.Append("\tUE::CodeGen::ConstInit::FStructParams{\r\n")
						.Append("\t\t.Super = ").AppendConstInitSingletonRef(this, classObj.Super).Append(",\r\n")
						.Append($"\t\t.FirstChild = {staticsName}::GetFirstFunction(),\r\n")
						.Append($"\t\t.BaseChain = MakeArrayView({staticsName}::StructBases),\r\n")
						.Append($"\t\t.ChildProperties = {staticsName}::GetFirstProperty(),\r\n")
						.Append($"\t\t.PropertiesSize = sizeof({classObj.Namespace.FullSourceName}{classObj.SourceName}),\r\n")
						.Append($"\t\t.MinAlignment = alignof({classObj.Namespace.FullSourceName}{classObj.SourceName}),\r\n")
						.Append("\t\tIF_WITH_METADATA(.MetaData = ").AppendMetaDataArrayView(classObj.MetaData, staticsName, MetaDataParamsName, 0, ",)\r\n")
						.Append("},\r\n")
						.Append("\tUE::CodeGen::ConstInit::FClassParams{\r\n")
						.Append($"\t\t.ConfigName = {classObj.Namespace.FullSourceName}{classObj.SourceName}::StaticConfigName(),\r\n")
						.Append($"\t\t.ClassConstructor = InternalConstructor<{classObj.Namespace.FullSourceName}{classObj.SourceName}>,\r\n")
						.Append($"\t\t.ClassVTableHelperCtorCaller = InternalVTableHelperCtorCaller<{classObj.Namespace.FullSourceName}{classObj.SourceName}>,\r\n")
						.Append($"\t\t.CppClassStaticFunctions = FUObjectCppClassStaticFunctions(({classObj.Namespace.FullSourceName}{classObj.SourceName}*)nullptr),\r\n")
						.Append($"\t\t.ClassFlags = (EClassFlags)0x{(uint)classFlags:X8}u,\r\n")
						.Append($"\t\t.ClassCastFlags = {classObj.Namespace.FullSourceName}{classObj.SourceName}::StaticAllClassCastFlags(),\r\n") // Propagate cast flags at compile time
						.Append($"\t\t.CppClassTypeInfo = &{staticsName}::StaticCppClassTypeInfo,\r\n");
					if (hasNativeFunctions)
					{
						builder.Append("\t\t.NativeFunctions = ").AppendArrayView(nativeFunctions, UhtDefineScopeNames.WithEditor, staticsName, "Funcs", 2, ",\r\n");
					}
					if (classObj.ClassWithin is not null && classObj.ClassWithin != Session.UObject)
					{
						builder.Append($"\t\t.ClassWithin = &{GetSingletonName(classObj.ClassWithin, UhtSingletonType.ConstInit)},\r\n");
					}
					if (hasInterfaces)
					{
						builder.Append($"\t\t.Interfaces = MakeConstArrayView({staticsName}::Interfaces),\r\n");
					}
					if (classObj.SparseClassDataStruct is not null)
					{
						builder.Append($"\t\t.SparseClassDataStruct = &{GetSingletonName(classObj.SparseClassDataStruct, UhtSingletonType.ConstInit)}\r\n");
					}
					builder.Append("\t},\r\n");
					if (classObj.IsVerseField)
					{
						builder.Append("\tUE::CodeGen::ConstInit::FVerseClassParams{\r\n")
							.Append("\t\t.PackageRelativeVersePath = UTF8TEXT(\"").AppendVerseScopeAndName(classObj, UhtVerseNameMode.PackageRelative).Append("\"),\r\n")
							.Append("\t\t.MangledPackageVersePath = UTF8TEXT(\"").Append(VerseNameMangling.MangleCasedName(classObj.Module.Module.VersePath).Result).Append("\"),\r\n");
						if (hasVerseCallableFunctions)
						{
							builder.Append("\t\tIF_WITH_VERSE_VM(.VerseCallableThunks = ").AppendArrayView(staticsName, "VerseFuncs", 0, ",)\r\n");
						}
						builder.Append("},\r\n");
					}
					builder.Append("};\r\n");
				}

				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
				{
					// Class parameters
					using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append("const UECodeGen_Private::").AppendClassParamsType(classObj).Append($" {staticsName}::ClassParams = {{\r\n");
					if (classObj.IsVerseField)
					{
						builder.Append("\t{\r\n");
					}
					builder.Append("\t&").Append(classObj.Namespace.FullSourceName).Append(classObj.SourceName).Append("::StaticClass,\r\n");
					if (classObj.Config.Length > 0)
					{
						builder.Append('\t').AppendUTF8LiteralString(classObj.Config).Append(",\r\n");
					}
					else
					{
						builder.Append("\tnullptr,\r\n");
					}
					builder.Append("\t&StaticCppClassTypeInfo,\r\n");
					builder.Append("\tDependentSingletons,\r\n");
					builder.AppendArrayPtrLine(functions, UhtDefineScopeNames.WithEditor, null, "FuncInfo", 1, ",\r\n");
					builder.AppendArrayPtrLine(properties, UhtDefineScopeNames.Standard, staticsName, "PropPointers", 1, ",\r\n");
					builder.Append('\t').Append(hasInterfaces ? "InterfaceParams" : "nullptr").Append(",\r\n");
					builder.Append("\tUE_ARRAY_COUNT(DependentSingletons),\r\n");
					builder.AppendArrayCountLine(functions, UhtDefineScopeNames.WithEditor, null, "FuncInfo", 1, ",\r\n");
					builder.AppendArrayCountLine(properties, UhtDefineScopeNames.Standard, staticsName, "PropPointers", 1, ",\r\n");
					builder.Append('\t').Append(hasInterfaces ? "UE_ARRAY_COUNT(InterfaceParams)" : "0").Append(",\r\n");
					builder.Append($"\t0x{(uint)classFlags:X8}u,\r\n");
					builder.Append('\t').AppendMetaDataParams(classObj, staticsName, MetaDataParamsName).Append("\r\n");
					if (classObj.IsVerseField)
					{
						EVerseClassFlags verseClassFlags = classObj.ClassType == UhtClassType.VModule ? EVerseClassFlags.Module : EVerseClassFlags.None;
						builder.Append("\t},\r\n");
						if (classObj.ClassType == UhtClassType.Class || classObj.ClassType == UhtClassType.VModule || classObj.ClassType == UhtClassType.Interface)
						{
							builder.Append("\t\"").AppendVerseScopeAndName(classObj, UhtVerseNameMode.PackageRelative).Append("\",\r\n");
						}
						else
						{
							builder.Append("\t\"\",\r\n");
						}
						builder.Append("\t\"").Append(VerseNameMangling.MangleCasedName(classObj.Module.Module.VersePath).Result).Append("\",\r\n");
						builder.Append($"\t0x{(uint)verseClassFlags:X8}u,\r\n");
					}
					builder.Append("};\r\n");
				}
			}

			// Native function registration
			if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
			{
				using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
				builder.Append($"void {classObj.Namespace.FullSourceName}{classObj.SourceName}::StaticRegisterNatives{classObj.SourceName}()\r\n");
				builder.Append("{\r\n");

				if (hasNativeFunctions)
				{
					builder.Append($"\tUClass* Class = {classObj.SourceName}::StaticClass();\r\n");
					builder.Append($"\tFNativeFunctionRegistrar::RegisterFunctions(Class, ").AppendArrayView(nativeFunctions, UhtDefineScopeNames.WithEditor, staticsName, "Funcs", 0, ");\r\n");

					if (hasVerseCallableFunctions)
					{
						builder.Append("#if WITH_VERSE_VM\r\n");
						builder.Append($"\tVerse::CodeGen::Private::RegisterVerseCallableThunks(Class, {staticsName}::VerseFuncs, UE_ARRAY_COUNT({staticsName}::VerseFuncs));\r\n");
						builder.Append("#endif\r\n");
					}
				}

				builder.Append("}\r\n"); // Close StaticRegisterNatives function

			}

			// Class registration
			if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
			{
				using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
				builder.Append("UClass* ").Append(singletonName).Append("()\r\n");
				builder.Append("{\r\n");
				builder.Append("\tif (!").Append(registrationName).Append(".OuterSingleton)\r\n");
				builder.Append("\t{\r\n");
				builder.Append("\t\t")
					.Append(classObj.IsVerseField ? "Verse::CodeGen::Private::ConstructUVerseClass(" : "UECodeGen_Private::ConstructUClass(")
					.Append(registrationName).Append(".OuterSingleton, ").Append(staticsName).Append("::ClassParams);\r\n");
				if (sparseDataTypes != null)
				{
					foreach (string sparseClass in sparseDataTypes)
					{
						builder.Append("\t\t").Append(registrationName).Append(".OuterSingleton->SetSparseClassDataStruct(").Append(classObj.SourceName).Append("::StaticGet").Append(sparseClass).Append("ScriptStruct());\r\n");
					}
				}
				builder.Append("\t}\r\n");
				builder.Append("\treturn ").Append(registrationName).Append(".OuterSingleton;\r\n");
				builder.Append("}\r\n");
			}

			// At this point, we can compute the hash... HOWEVER, in the old UHT extra data is appended to the hash block that isn't emitted to the actual output
			using (BorrowStringBuilder hashBorrower = new(StringBuilderCache.Small))
			{
				StringBuilder hashBuilder = hashBorrower.StringBuilder;
				hashBuilder.Append(builder, hashCodeBlockStart, builder.Length - hashCodeBlockStart);

				int saveLength = hashBuilder.Length;

				// Append base class' hash at the end of the generated code, this will force update derived classes
				// when base class changes during hot-reload.
				uint baseClassHash = 0;
				if (classObj.SuperClass != null && !classObj.SuperClass.ClassFlags.HasAnyFlags(EClassFlags.Intrinsic))
				{
					baseClassHash = ObjectInfos[classObj.SuperClass.ObjectTypeIndex].Hash;
				}
				hashBuilder.Append($"\r\n// {baseClassHash}\r\n");

				// Append info for the sparse class data struct onto the text to be hashed
				if (sparseDataTypes != null)
				{
					foreach (string sparseDataType in sparseDataTypes)
					{
						UhtType? type = Session.FindType(classObj, UhtFindOptions.ScriptStruct | UhtFindOptions.EngineName, sparseDataType);
						if (type != null)
						{
							hashBuilder.Append(type.EngineName).Append("\r\n");
							for (UhtScriptStruct? sparseStruct = type as UhtScriptStruct; sparseStruct != null; sparseStruct = sparseStruct.SuperScriptStruct)
							{
								foreach (UhtProperty property in sparseStruct.Properties)
								{
									hashBuilder.AppendPropertyText(property, UhtPropertyTextType.SparseShort).Append(' ').Append(property.SourceName).Append("\r\n");
								}
							}
						}
					}
				}

				if (classObj.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.NoExport))
				{
					builder.Append("/* friend declarations for pasting into noexport class ").Append(classObj.SourceName).Append("\r\n");
					builder.Append("friend struct ").Append(staticsName).Append(";\r\n");
					builder.Append("*/\r\n");
				}

				if (Session.IncludeDebugOutput)
				{
					using UhtRentedPoolBuffer<char> borrowBuffer = hashBuilder.RentPoolBuffer(saveLength, hashBuilder.Length - saveLength);
					builder.Append("#if 0\r\n");
					builder.Append(borrowBuffer.Buffer.Memory);
					builder.Append("#endif\r\n");
				}

				// Calculate generated class initialization code hash so that we know when it changes after hot-reload
				{
					using UhtRentedPoolBuffer<char> borrowBuffer = hashBuilder.RentPoolBuffer();
					ObjectInfos[classObj.ObjectTypeIndex].Hash = UhtHash.GenenerateTextHash(borrowBuffer.Buffer.Memory.Span);
				}
			}

			if (classObj.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.SelfHasReplicatedProperties))
			{
				builder.Append("#if VALIDATE_CLASS_REPS\r\n");
				builder.Append("void ").Append(classObj.SourceName).Append("::ValidateGeneratedRepEnums(const TArray<struct FRepRecord>& ClassReps) const\r\n");
				builder.Append("{\r\n");

				foreach (UhtProperty property in classObj.Properties)
				{
					if (property.PropertyFlags.HasAnyFlags(EPropertyFlags.Net))
					{
						// Do not make this a static const.  It causes issues with live coding
						builder.Append("\tstatic FName Name_").Append(property.SourceName).Append("(TEXT(\"").Append(property.SourceName).Append("\"));\r\n");
					}
				}
				builder.Append("\tconst bool bIsValid = true");
				foreach (UhtProperty property in classObj.Properties)
				{
					if (property.PropertyFlags.HasAnyFlags(EPropertyFlags.Net))
					{
						if (!property.IsStaticArray)
						{
							builder.Append("\r\n\t\t&& Name_").Append(property.SourceName).Append(" == ClassReps[(int32)ENetFields_Private::").Append(property.SourceName).Append("].Property->GetFName()");
						}
						else
						{
							builder.Append("\r\n\t\t&& Name_").Append(property.SourceName).Append(" == ClassReps[(int32)ENetFields_Private::").Append(property.SourceName).Append("_STATIC_ARRAY].Property->GetFName()");
						}
					}
				}
				builder.Append(";\r\n");
				builder.Append("\tcheckf(bIsValid, TEXT(\"UHT Generated Rep Indices do not match runtime populated Rep Indices for properties in ").Append(classObj.SourceName).Append("\"));\r\n");
				builder.Append("}\r\n");
				builder.Append("#endif\r\n");
			}
			return builder;
		}
#pragma warning restore CA1505 //  'AppendNativeGeneratedInitCode' has a maintainability index of '5'. Rewrite or refactor the code to increase its maintainability index (MI) above '9'. 

		private static StringBuilder AppendImplementedInterfaceParam(StringBuilder builder, PropertyMemberContextImpl context, UhtClass classObj, UhtClass interfaceObj, UhtSingletonType singletonType)
		{
			if (interfaceObj.ClassFlags.HasAnyFlags(EClassFlags.Interface))
			{
				builder
					.Append("\t{ ")
					.AppendSingletonRef(context, interfaceObj.AlternateObject, singletonType);
				if (interfaceObj.IsVerseField)
				{
					builder
						.Append(", 0, true");
					if (classObj.VerseInterfaces.Contains(interfaceObj))
					{
						builder.Append(", true");
					}
				}
				else
				{
					builder
						.Append(", (int32)VTABLE_OFFSET(")
						.Append(classObj.FullyQualifiedSourceName)
						.Append(", ")
						.Append(interfaceObj.FullyQualifiedSourceName)
						.Append("), false");
				}
				builder
					.Append(" }, ")
					.AppendObjectHash(classObj, context, interfaceObj.AlternateObject)
					.Append("\r\n");
			}
			return builder;
		}

		private static StringBuilder AppendFunctionThunk(StringBuilder builder, UhtClass classObj, UhtFunction function)
		{

			// TEMPORARY: Verse suspends methods can't be called but must exist.  So generate an error
			if (function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.VerseSuspends))
			{
				builder.Append("\tcheckf(false, TEXT(\"Verse coroutines can not be invoked from their UFunction\"));\r\n");
				return builder;
			}

			// Export the GET macro for the parameters
			foreach (UhtType parameter in function.ParameterProperties.Span)
			{
				if (parameter is UhtProperty property)
				{
					builder.Append('\t').AppendFunctionThunkParameterGet(property).Append(";\r\n");
				}
			}

			builder.Append("\tP_FINISH;\r\n");
			builder.Append("\tP_NATIVE_BEGIN;\r\n");

			// Call the validate function if there is one
			if (!function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.CppStatic) && function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetValidate))
			{
				builder.Append("\tif (!P_THIS->").Append(function.CppValidationImplName).Append('(').AppendFunctionThunkParameterNames(function).Append("))\r\n");
				builder.Append("\t{\r\n");
				builder.Append("\t\tRPC_ValidateFailed(TEXT(\"").Append(function.CppValidationImplName).Append("\"));\r\n");
				builder.Append("\t\treturn;\r\n");   // If we got here, the validation function check failed
				builder.Append("\t}\r\n");
			}

			// Write out the return value
			builder.Append('\t');
			UhtProperty? returnProperty = function.ReturnProperty;
			if (returnProperty != null)
			{
				builder.Append("*(").AppendFunctionThunkReturn(returnProperty).Append("*)Z_Param__Result=");
			}

			// Export the call to the C++ version
			if (function.FunctionExportFlags.HasAnyFlags(UhtFunctionExportFlags.CppStatic))
			{
				if (classObj.ClassType == UhtClassType.VModule)
				{
					builder.Append($"{classObj.Namespace.FullSourceName}{function.CppImplName}(").AppendFunctionThunkParameterNames(function).Append(");\r\n");
				}
				else
				{
					builder.Append($"{classObj.SourceName}::{function.CppImplName}(").AppendFunctionThunkParameterNames(function).Append(");\r\n");
				}
			}
			else
			{
				builder.Append("P_THIS->").Append(function.CppImplName).Append('(').AppendFunctionThunkParameterNames(function).Append(");\r\n");
			}
			builder.Append("\tP_NATIVE_END;\r\n");
			return builder;
		}

		private static void FindNoExportStructsRecursive(List<UhtScriptStruct> outScriptStructs, UhtStruct structObj)
		{
			for (UhtStruct? current = structObj; current != null; current = current.SuperStruct)
			{
				// Is isn't true for noexport structs
				if (current is UhtScriptStruct scriptStruct)
				{
					if (scriptStruct.ScriptStructFlags.HasAnyFlags(EStructFlags.Native))
					{
						break;
					}

					// these are a special cases that already exists and if wrong if exported naively
					if (!scriptStruct.IsAlwaysAccessible)
					{
						outScriptStructs.Remove(scriptStruct);
						outScriptStructs.Add(scriptStruct);
					}
				}

				foreach (UhtType type in current.Children)
				{
					if (type is UhtProperty property)
					{
						foreach (UhtType referenceType in property.EnumerateReferencedTypes())
						{
							if (referenceType is UhtScriptStruct propertyScriptStruct)
							{
								FindNoExportStructsRecursive(outScriptStructs, propertyScriptStruct);
							}
						}
					}
				}
			}
		}

		private static List<UhtScriptStruct> FindNoExportStructs(UhtStruct structObj)
		{
			List<UhtScriptStruct> outScriptStructs = new();
			FindNoExportStructsRecursive(outScriptStructs, structObj);
			outScriptStructs.Reverse();
			return outScriptStructs;
		}

		private static UhtRegistrations GetRegistrations(Dictionary<UhtPackage, UhtRegistrations> packageRegistrations, UhtField fieldObj)
		{
			UhtPackage package = fieldObj.Package;
			if (packageRegistrations.TryGetValue(package, out UhtRegistrations? registrations))
			{
				return registrations;
			}
			registrations = new();
			packageRegistrations.Add(package, registrations);
			return registrations;
		}

		private class PropertyMemberContextImpl : IUhtPropertyMemberContext
		{
			private readonly UhtCodeGenerator _codeGenerator;
			private readonly UhtStruct _outerStruct;
			private readonly string _outerStructNamespaceName;
			private readonly string _outerStructSourceName;
			private readonly string _staticsName;

			public PropertyMemberContextImpl(UhtCodeGenerator codeGenerator, UhtStruct outerStruct, string outerStructNamespaceName, string outerStructSourceName, string staticsName)
			{
				_codeGenerator = codeGenerator;
				_outerStruct = outerStruct;
				_staticsName = staticsName;
				_outerStructNamespaceName = outerStructNamespaceName;
				_outerStructSourceName = outerStructSourceName;
			}

			public PropertyMemberContextImpl(UhtCodeGenerator codeGenerator, UhtStruct outerStruct, string staticsName)
			{
				_codeGenerator = codeGenerator;
				_outerStruct = outerStruct;
				_staticsName = staticsName;
				_outerStructNamespaceName = _outerStruct is UhtScriptStruct scriptStruct ? scriptStruct.NamespaceExportName : _outerStruct.Namespace.FullSourceName;
				_outerStructSourceName = _outerStruct.SourceName;
			}

			public UhtStruct OuterStruct => _outerStruct;
			public string OuterStructNamespaceName => _outerStructNamespaceName;
			public string OuterStructSourceName => _outerStructSourceName;
			public string StaticsName => _staticsName;
			public string NamePrefix => "NewProp_";
			public string MetaDataSuffix => "_MetaData";

			public string GetSingletonName(UhtObject? obj, UhtSingletonType type)
			{
				return _codeGenerator.GetSingletonName(obj, type);
			}

			public uint GetTypeHash(UhtObject obj)
			{
				return _codeGenerator.ObjectInfos[obj.ObjectTypeIndex].Hash;
			}

			public bool IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat format) => _codeGenerator.Session.IsUsingCompiledInObjectFormat(format);

			public bool IsUsingMultipleCompiledInObjectFormats => _codeGenerator.Session.IsUsingMultipleCompiledInObjectFormats;
		}
	}

	/// <summary>
	/// Collection of string builder extensions used to generate the cpp files for individual headers.
	/// </summary>
	public static class UhtHeaderCodeGeneratorCppFileStringBuilderExtensions
	{
		/// <summary>
		/// Appends a structure initialization expression for a constant-initialized UObject, followed
		/// by a comma and newline for use in a larger aggregate initialization expression
		/// </summary>
		/// <param name="builder"></param>
		/// <param name="uhtObject"></param>
		/// <param name="names"></param>
		/// <param name="tabs"></param>
		/// <param name="additionalObjectFlags"></param>
		/// <returns></returns>
		public static StringBuilder AppendConstInitObjectParams(this StringBuilder builder, UhtObject uhtObject, IUhtSingletonNameProvider names, int tabs, string? additionalObjectFlags = null)
		{
			const string ObjectFlags = "RF_Public|RF_Transient|RF_MarkAsNative";
			builder.AppendTabs(tabs).Append("UE::CodeGen::ConstInit::FObjectParams{\r\n");
			builder.AppendTabs(tabs + 1).Append($".Flags = {ObjectFlags}{additionalObjectFlags},\r\n");
			// Can't take a pointer to Class to initialize itself because of TNoDestroy, null class signals to take the address of Class in the constructor 
			if (uhtObject != uhtObject.EngineClass)
			{
				builder.AppendTabs(tabs + 1).Append($".Class =").AppendConstInitSingletonRef(names, uhtObject.EngineClass).Append(",\r\n");
			}
			builder.AppendTabs(tabs + 1).Append($".NameUTF8 = UTF8TEXT(").AppendUTF8LiteralString(uhtObject.EngineName).Append("),\r\n");
			builder.AppendTabs(tabs + 1).Append($".Outer = ").AppendConstInitSingletonRef(names, uhtObject.Outer as UhtObject).Append(",\r\n");
			builder.AppendTabs(tabs).Append("},\r\n");
			return builder;
		}

		/// <summary>
		/// Append the parameter names for a function
		/// </summary>
		/// <param name="builder">Destination builder</param>
		/// <param name="function">Function in question</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendFunctionThunkParameterNames(this StringBuilder builder, UhtFunction function)
		{
			bool first = true;
			foreach (UhtType parameter in function.ParameterProperties.Span)
			{
				if (parameter is UhtProperty property)
				{
					if (first)
					{
						first = false;
					}
					else
					{
						builder.Append(',');
					}

					bool needsGCBarrier = property.NeedsGCBarrierWhenPassedToFunction(function);
					if (needsGCBarrier)
					{
						builder.Append("P_ARG_GC_BARRIER(");
					}
					builder.AppendFunctionThunkParameterArg(property);
					if (needsGCBarrier)
					{
						builder.Append(')');
					}
				}
			}
			return builder;
		}

		/// <summary>
		/// Append the name of the function params type
		/// </summary>
		/// <param name="builder">String builder</param>
		/// <param name="function">Function in question</param>
		/// <returns></returns>
		public static StringBuilder AppendFunctionParamsType(this StringBuilder builder, UhtFunction function)
		{
			switch (function.FunctionType)
			{
				case UhtFunctionType.Function:
					builder.Append(function.IsVerseField ? "FVerseFunctionParams" : "FFunctionParams");
					break;
				case UhtFunctionType.Delegate:
					builder.Append("FDelegateFunctionParams");
					break;
				case UhtFunctionType.SparseDelegate:
					builder.Append("FSparseDelegateFunctionParams");
					break;
			}
			return builder;
		}

		/// <summary>
		/// Append the name of the function constructor
		/// </summary>
		/// <param name="builder">String builder</param>
		/// <param name="function">Function in question</param>
		/// <returns></returns>
		public static StringBuilder AppendFunctionConstructorName(this StringBuilder builder, UhtFunction function)
		{
			switch (function.FunctionType)
			{
				case UhtFunctionType.Function:
					builder.Append(function.IsVerseField ? "Verse::CodeGen::Private::ConstructUVerseFunction" : "UECodeGen_Private::ConstructUFunction");
					break;
				case UhtFunctionType.Delegate:
					builder.Append("UECodeGen_Private::ConstructUDelegateFunction");
					break;
				case UhtFunctionType.SparseDelegate:
					builder.Append("UECodeGen_Private::ConstructUSparseDelegateFunction");
					break;
			}
			return builder;
		}

		/// <summary>
		/// Append the name of the class params
		/// </summary>
		/// <param name="builder">Destination builder</param>
		/// <param name="classObj">Class in question</param>
		/// <returns></returns>
		public static StringBuilder AppendClassParamsType(this StringBuilder builder, UhtClass classObj)
		{
			builder.Append(classObj.IsVerseField ? "FVerseClassParams" : "FClassParams");
			return builder;
		}

		/// <summary>
		/// Append the name of the struct params
		/// </summary>
		/// <param name="builder">Destination builder</param>
		/// <param name="structObj">Struct in question</param>
		/// <returns></returns>
		public static StringBuilder AppendStructParamsType(this StringBuilder builder, UhtStruct structObj)
		{
			builder.Append(structObj.IsVerseField ? "FVerseStructParams" : "FStructParams");
			return builder;
		}

		/// <summary>
		/// Append the name of the enum params
		/// </summary>
		/// <param name="builder">Destination builder</param>
		/// <param name="enumObj">enum in question</param>
		/// <returns></returns>
		public static StringBuilder AppendEnumParamsType(this StringBuilder builder, UhtEnum enumObj)
		{
			builder.Append(enumObj.IsVerseField ? "FVerseEnumParams" : "FEnumParams");
			return builder;
		}
	}
}
