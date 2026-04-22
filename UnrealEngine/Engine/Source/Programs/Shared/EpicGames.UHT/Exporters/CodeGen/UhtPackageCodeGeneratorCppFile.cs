// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using EpicGames.Core;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Exporters.CodeGen
{
	internal class UhtPackageCodeGeneratorCppFile : UhtPackageCodeGenerator
	{
		/// <summary>
		/// Construct an instance of this generator object
		/// </summary>
		/// <param name="codeGenerator">The base code generator</param>
		/// <param name="module">Module being generated</param>
		public UhtPackageCodeGeneratorCppFile(UhtCodeGenerator codeGenerator, UhtModule module)
			: base(codeGenerator, module)
		{
		}

		/// <summary>
		/// For a given UE header file, generated the generated cpp file
		/// </summary>
		/// <param name="factory">Requesting factory</param>
		/// <param name="packageSortedHeaders">Sorted list of headers by name of all headers in the package</param>
		public void Generate(IUhtExportFactory factory, List<UhtHeaderFile> packageSortedHeaders)
		{
			{
				using BorrowStringBuilder borrower = new(StringBuilderCache.Big);
				const string MetaDataParamsName = "Package_MetaDataParams";
				StringBuilder builder = borrower.StringBuilder;

				// Collect information from all of the headers
				List<UhtField> singletons = new();
				StringBuilder declarations = new();
				uint bodiesHash = 0;
				foreach (UhtHeaderFile headerFile in packageSortedHeaders)
				{
					ref UhtCodeGenerator.HeaderInfo headerInfo = ref HeaderInfos[headerFile.HeaderFileTypeIndex];
					IReadOnlyList<string> sorted = headerFile.References.Declaration.GetSortedReferences(
						(objectIndex, type) => type switch
						{
							UhtSingletonType.Registered or UhtSingletonType.Unregistered => GetExternalDecl(objectIndex, type),
							_ => null
						});
					foreach (string declaration in sorted)
					{
						declarations.Append(declaration);
					}

					singletons.AddRange(headerFile.References.Singletons);

					uint bodyHash = HeaderInfos[headerFile.HeaderFileTypeIndex].BodyHash;
					if (bodiesHash == 0)
					{
						// Don't combine in the first case because it keeps GUID backwards compatibility
						bodiesHash = bodyHash;
					}
					else
					{
						bodiesHash = HashCombine(bodyHash, bodiesHash);
					}
				}

				// No need to generate output if we have no declarations
				if (declarations.Length == 0)
				{
					if (SaveExportedHeaders)
					{
						// We need to create the directory, otherwise UBT will think that this module has not been properly updated and won't write a Timestamp file
						System.IO.Directory.CreateDirectory(Module.Module.OutputDirectory);
					}
					return;
				}
				uint declarationsHash = UhtHash.GenenerateTextHash(declarations.ToString());

				singletons.Sort((UhtField lhs, UhtField rhs) =>
				{
					bool lhsIsDel = IsDelegateFunction(lhs);
					bool rhsIsDel = IsDelegateFunction(rhs);
					if (lhsIsDel != rhsIsDel)
					{
						return !lhsIsDel ? -1 : 1;
					}
					return StringComparerUE.OrdinalIgnoreCase.Compare(
						ObjectInfos[lhs.ObjectTypeIndex].RegisteredSingletonName,
						ObjectInfos[rhs.ObjectTypeIndex].RegisteredSingletonName);
				});

				builder.Append(HeaderCopyright);
				builder.Append(RequiredCPPIncludes);
				if (Module.Module.HasVerse)
				{
					builder.Append("#include \"VerseInteropMacros.h\"\r\n");
				}
				builder.Append(DisableDeprecationWarnings).Append("\r\n");
				builder.Append("void EmptyLinkFunctionForGeneratedCode").Append(Module.ShortName).Append("_init() {}\r\n");

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

				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
				{
					using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					builder.Append(GetExternalDecl(Session.UPackage, UhtSingletonType.ConstInit));
				}

				if (Session.IncludeDebugOutput)
				{
					builder.Append("#if 0\r\n");
					foreach (UhtHeaderFile headerFile in packageSortedHeaders)
					{
						builder.Append('\t').Append(headerFile.FilePath).Append("\r\n");
					}
					builder.Append(declarations);
					builder.Append("#endif\r\n");
				}


				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
				{
					using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					foreach (UhtObject obj in singletons)
					{
						builder.Append(GetExternalDecl(obj, UhtSingletonType.ConstInit));
					}
				}
				if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
				{
					using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
					foreach (UhtObject obj in singletons)
					{
						builder.Append(GetExternalDecl(obj, UhtSingletonType.Registered));
					}
				}

				foreach (UhtPackage package in Module.Packages)
				{
					string strippedName = PackageInfos[package.PackageTypeIndex].StrippedName;
					string singletonName = GetSingletonName(package, UhtSingletonType.Registered);
					string staticsName = GetSingletonName(package, UhtSingletonType.Statics);
					UhtField[] packageSingletons = singletons.Where(x => x.Package == package).ToArray();
					EPackageFlags flags = package.PackageFlags & (EPackageFlags.ClientOptional | EPackageFlags.ServerSideOnly | EPackageFlags.EditorOnly | EPackageFlags.Developer | EPackageFlags.UncookedOnly);

					// Entire loop body is #if/#else/#endif
					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.ConstInit))
					{
						using UhtConditionalMacroBlock block = new(builder, "UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
						string? singletonsArray = null;
						if (packageSingletons.Length != 0)
						{
							// List of objects of mixed types to register with the package
							singletonsArray = $"Z_Singletons_UPackage_{strippedName}";
							builder.AppendEach(packageSingletons,
								(builder) => builder.Append($"\tconstinit UObject* {singletonsArray}[] = {{\r\n"),
								(builder, field) => builder.Append($"\t\t&{GetSingletonName(field, UhtSingletonType.ConstInit)}, \r\n"),
								(builder) => builder.Append("\t};\r\n")
							);
						}
						builder.Append($"\tconstinit UPackage {GetSingletonName(package, UhtSingletonType.ConstInit)}( \r\n")
							.Append("\t\tUE::CodeGen::ConstInit::FPackageParams{\r\n")
							.Append("\t\t\t.Object = UE::CodeGen::ConstInit::FObjectParams{\r\n")
							.Append("\t\t\t\t.Flags = RF_Public,\r\n")
							.Append($"\t\t\t\t.Class = &{GetSingletonName(Session.UPackage, UhtSingletonType.ConstInit)},\r\n")
							.Append("\t\t\t\t.NameUTF8 = UTF8TEXT(").AppendUTF8LiteralString(package.SourceName).Append("),\r\n")
							.Append("\t\t\t\t.Outer = nullptr,\r\n")
							.Append("\t\t\t},\r\n")
							.Append($"\t\t\t.Flags = EPackageFlags(PKG_CompiledIn | 0x{(uint)flags:X8}),\r\n")
							.Append($"\t\t\tIF_WITH_METADATA(.MetaData = ").AppendMetaDataArrayView(package.MetaData, null, MetaDataParamsName, 2, ",)\r\n")
							.Append($"\t\t\t.BodiesHash = 0x{bodiesHash:X8},\r\n")
							.Append($"\t\t\t.DeclarationsHash = 0x{declarationsHash:X8},\r\n")
							.Append("\t});\r\n")
							.Append("\r\n")
							.Append($"\tstatic FRegisterCompiledInObjects Z_CompiledInDeferPackage_UPackage_{strippedName}{{\r\n")
							.Append($"\t\t&{GetSingletonName(package, UhtSingletonType.ConstInit)}, \r\n")
							.AppendArrayView(null, singletonsArray, 2, "\r\n")
							.Append("};\r\n");
					}

					if (Session.IsUsingCompiledInObjectFormat(UhtCompiledInObjectFormat.Params))
					{
						using UhtConditionalMacroBlock block = new(builder, "!UE_WITH_CONSTINIT_UOBJECT", Session.IsUsingMultipleCompiledInObjectFormats);
						builder.Append("\tstatic FPackageRegistrationInfo Z_Registration_Info_UPackage_").Append(strippedName).Append(";\r\n")
							.Append($"\tFORCENOINLINE UPackage* {singletonName}()\r\n")
							.Append("\t{\r\n")
							.Append($"\t\tif (!Z_Registration_Info_UPackage_{strippedName}.OuterSingleton)\r\n")
							.Append("\t\t{\r\n");

						if (packageSingletons.Length != 0)
						{
							builder.Append("\t\tstatic UObject* (*const SingletonFuncArray[])() = {\r\n");
							foreach (UhtField field in packageSingletons)
							{
								builder.Append("\t\t\t(UObject* (*)())").Append(ObjectInfos[field.ObjectTypeIndex].RegisteredSingletonName).Append(",\r\n");
							}
							builder.Append("\t\t};\r\n");
						}

						builder.Append("\t\tstatic const UECodeGen_Private::FPackageParams PackageParams = {\r\n");
						builder.Append("\t\t\t").AppendUTF8LiteralString(package.SourceName).Append(",\r\n");
						builder.Append("\t\t\t").Append(packageSingletons.Length != 0 ? "SingletonFuncArray" : "nullptr").Append(",\r\n");
						builder.Append("\t\t\t").Append(packageSingletons.Length != 0 ? "UE_ARRAY_COUNT(SingletonFuncArray)" : "0").Append(",\r\n");
						builder.Append("\t\t\tPKG_CompiledIn | ").Append($"0x{(uint)flags:X8}").Append(",\r\n");
						builder.Append("\t\t\t").Append($"0x{bodiesHash:X8}").Append(",\r\n");
						builder.Append("\t\t\t").Append($"0x{declarationsHash:X8}").Append(",\r\n");
						builder.Append("\t\t\t").AppendMetaDataParams(package, null, MetaDataParamsName).Append("\r\n");
						builder.Append("\t\t};\r\n");
						builder.Append("\t\tUECodeGen_Private::ConstructUPackage(Z_Registration_Info_UPackage_").Append(strippedName).Append(".OuterSingleton, PackageParams);\r\n");
						builder.Append("\t}\r\n");
						builder.Append("\treturn Z_Registration_Info_UPackage_").Append(strippedName).Append(".OuterSingleton;\r\n");
						builder.Append("}\r\n");

						// Do not change the Z_CompiledInDeferPackage_UPackage_ without changing LC_SymbolPatterns
						builder.Append("static FRegisterCompiledInInfo Z_CompiledInDeferPackage_UPackage_").Append(strippedName).Append('(').Append(singletonName)
							.Append(", TEXT(\"").Append(package.SourceName).Append("\"), Z_Registration_Info_UPackage_").Append(strippedName).Append(", CONSTRUCT_RELOAD_VERSION_INFO(FPackageReloadVersionInfo, ")
							.Append($"0x{bodiesHash:X8}, 0x{declarationsHash:X8}").Append("));\r\n");
					}
				}

				// Verse registration
				if (Module.Module.HasVerse)
				{
					string staticsName = $"Z_VerseRegistration_{Module.Module.Name}_Statics";
					builder.Append($"namespace {staticsName}\r\n");
					builder.Append("{\r\n");
					if (Module.Module.VerseDependencies.Count > 0)
					{
						builder.Append("\tconst FVniPackageName Dependencies[] = {\r\n");
						foreach (string dependency in Module.Module.VerseDependencies)
						{
							builder.Append($"\t\t{GetVniPackageName(dependency)},\r\n");
						}
						builder.Append("\t};\r\n");
					}
					builder.Append($"\tconst FVniPackageName Name = {GetVniPackageName(Module.Module.VersePackageName)};\r\n");
					builder.Append("\tV_DEFINE_CPP_MODULE_REGISTRAR(\r\n");
					builder.Append("\t\tUHT,\r\n");
					builder.Append($"\t\t{Module.Module.Name},\r\n");
					builder.Append("\t\tName,\r\n");
					builder.Append($"\t\tTEXT(\"{Module.Module.VersePath}\"),\r\n");
					builder.Append($"\t\tEVerseScope::{Module.Module.VerseScope},\r\n");
					builder.Append($"\t\tTEXT(\"{Module.Module.VerseDirectoryPath}\"),\r\n");
					builder.Append($"\t\t").Append(Module.Module.VerseDependencies.Count > 0 ? "Dependencies" : "nullptr").Append(",\r\n");
					builder.Append($"\t\t{Module.Module.VerseDependencies.Count},\r\n");
					builder.Append($"\t\tnullptr);\r\n");
					builder.Append("}\r\n");
				}

				builder.Append(EnableDeprecationWarnings).Append("\r\n");

				if (SaveExportedHeaders)
				{
					string cppFilePath = factory.MakePath(Module, ".init.gen.cpp");
					factory.CommitOutput(cppFilePath, builder);
				}
			}
		}

		private static string GetVniPackageName(ReadOnlySpan<char> packageName)
		{
			int slashIndex = packageName.IndexOf('/');
			if (slashIndex == -1)
			{
				throw new UhtIceException("Verse package name expects a string containing a \"/\"");
			}
			ReadOnlySpan<char> mountPointName = packageName[..slashIndex];
			ReadOnlySpan<char> cppModuleName = packageName[(slashIndex + 1)..];
			return $"{{ TEXT(\"{mountPointName}\"), TEXT(\"{cppModuleName}\") }}";
		}
	}
}
