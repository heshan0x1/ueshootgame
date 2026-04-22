// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeIREECompiler.h"

#ifdef WITH_NNE_RUNTIME_IREE
#if WITH_EDITOR

#include "GenericPlatform/GenericPlatformFile.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "IREEUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NNE.h"
#include "NNERuntimeIREELog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Templates/SharedPointer.h"

namespace UE::NNERuntimeIREE
{
	namespace CPU
	{
		namespace Private
		{
			FString GetSharedLibraryEntryPointName(const FString& HeaderString)
			{
				FString SearchString = "iree_hal_executable_library_header_t**";
				int32 Start = HeaderString.Find(SearchString);
				if (Start == INDEX_NONE)
				{
					return "";
				}
				Start += SearchString.Len();
				int32 End = HeaderString.Find("(", ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
				if (End <= Start)
				{
					return "";
				}
				return HeaderString.Mid(Start, End - Start).TrimStartAndEnd();
			}
		} // Private

		FCompiler::FCompiler(const FString& InImporterCommand, const FString& InImporterArguments, const FString& InCompilerCommand, const FString& InLinkerCommand, const FString& InSharedLibExt, TConstArrayView<FBuildTarget> InBuildTargets) 
			: ImporterCommand(InImporterCommand), ImporterArguments(InImporterArguments), CompilerCommand(InCompilerCommand), LinkerCommand(InLinkerCommand), SharedLibExt(InSharedLibExt), BuildTargets(InBuildTargets)
		{

		}

		TUniquePtr<FCompiler> FCompiler::Make(const FString& InTargetPlatformName)
		{
			using namespace Private;

			FString PluginDir = FPaths::ConvertRelativePathToFull(*IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME)->GetBaseDir());
			FString BuildConfigFileName = FString("IREE_") + UGameplayStatics::GetPlatformName() + "_To_" + InTargetPlatformName + ".json";
			TArray<FString> BuildConfigFilePaths =
			{
				FPaths::Combine(FPaths::ConvertRelativePathToFull(*FPaths::ProjectConfigDir()), BuildConfigFileName),
				FPaths::Combine(PluginDir, "Config", BuildConfigFileName),
				FPaths::Combine(FPaths::ConvertRelativePathToFull(*FPaths::EngineDir()), "Platforms", InTargetPlatformName, "Plugins", UE_PLUGIN_NAME, "Config", BuildConfigFileName),
				FPaths::Combine(FPaths::ConvertRelativePathToFull(*FPaths::EngineDir()), "Platforms", InTargetPlatformName, "Plugins", "Experimental", UE_PLUGIN_NAME, "Config", BuildConfigFileName)
			};

			FString ImporterCommand;
			FString ImporterArguments;
			FString CompilerCommand;
			FString LinkerCommand;
			FString SharedLibExt;
			TArray<FBuildTarget> BuildTargets;
			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
			for (FString BuildConfigFilePath : BuildConfigFilePaths)
			{
				if (PlatformFile.FileExists(*BuildConfigFilePath))
				{
					FString BuildConfigFileString;
					if (FFileHelper::LoadFileToString(BuildConfigFileString, *BuildConfigFilePath))
					{
						TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(BuildConfigFileString);
						TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
						FBuildConfig BuildConfig;
						if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid() && BuildConfig.FromJson(JsonObject))
						{
							if (BuildConfig.BuildTargets.IsEmpty())
							{
								UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu could not find targets in %s"), *BuildConfigFilePath);
								continue;
							}

							FString TmpImporterCommand;
							for (int32 i = 0; i < BuildConfig.ImporterCommand.Num(); i++)
							{
								if (IREEUtils::ResolveEnvironmentVariables(BuildConfig.ImporterCommand[i]))
								{
									BuildConfig.ImporterCommand[i].ReplaceInline(*FString("${PLUGIN_DIR}"), *PluginDir);
									BuildConfig.ImporterCommand[i].ReplaceInline(*FString("${PROJECT_DIR}"), *FPaths::ProjectDir());
									if (PlatformFile.FileExists(*BuildConfig.ImporterCommand[i]))
									{
										TmpImporterCommand = BuildConfig.ImporterCommand[i];
										break;
									}
								}
								else
								{
									UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu could not replace environment variables in %s"), *BuildConfig.ImporterCommand[i]);
								}
							}
							if (TmpImporterCommand.IsEmpty())
							{
								UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu could not find the importer executable in %s"), *BuildConfigFilePath);
								continue;
							}

							FString TmpCompilerCommand;
							for (int32 i = 0; i < BuildConfig.CompilerCommand.Num(); i++)
							{
								if (IREEUtils::ResolveEnvironmentVariables(BuildConfig.CompilerCommand[i]))
								{
									BuildConfig.CompilerCommand[i].ReplaceInline(*FString("${PLUGIN_DIR}"), *PluginDir);
									BuildConfig.CompilerCommand[i].ReplaceInline(*FString("${PROJECT_DIR}"), *FPaths::ProjectDir());
									if (PlatformFile.FileExists(*BuildConfig.CompilerCommand[i]))
									{
										TmpCompilerCommand = BuildConfig.CompilerCommand[i];
										break;
									}
								}
								else
								{
									UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu could not replace environment variables in %s"), *BuildConfig.CompilerCommand[i]);
								}
							}
							if (TmpCompilerCommand.IsEmpty())
							{
								UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu could not find the compiler executable in %s"), *BuildConfigFilePath);
								continue;
							}

							FString TmpLinkerCommand;
							for (int32 i = 0; i < BuildConfig.LinkerCommand.Num(); i++)
							{
								if (IREEUtils::ResolveEnvironmentVariables(BuildConfig.LinkerCommand[i]))
								{
									BuildConfig.LinkerCommand[i].ReplaceInline(*FString("${PLUGIN_DIR}"), *PluginDir);
									BuildConfig.LinkerCommand[i].ReplaceInline(*FString("${PROJECT_DIR}"), *FPaths::ProjectDir());
									if (PlatformFile.FileExists(*BuildConfig.LinkerCommand[i]))
									{
										TmpLinkerCommand = BuildConfig.LinkerCommand[i];
										break;
									}
								}
								else
								{
									UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu could not replace environment variables in %s"), *BuildConfig.LinkerCommand[i]);
								}
							}
							if (TmpLinkerCommand.IsEmpty())
							{
								UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu could not find the linker executable in %s"), *BuildConfigFilePath);
								continue;
							}

							ImporterCommand = TmpImporterCommand;
							ImporterArguments = BuildConfig.ImporterArguments;
							CompilerCommand = TmpCompilerCommand;
							LinkerCommand = TmpLinkerCommand;
							SharedLibExt = BuildConfig.SharedLibExt;
							BuildTargets = BuildConfig.BuildTargets;
							break;
						}
						else
						{
							UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu could not parse build config file %s"), *BuildConfigFilePath);
						}
					}
					else
					{
						UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu could not read build config file %s"), *BuildConfigFilePath);
					}
				}
			}
			if (CompilerCommand.IsEmpty() || LinkerCommand.IsEmpty() || BuildTargets.IsEmpty())
			{
				return TUniquePtr<FCompiler>();
			}
			return TUniquePtr<FCompiler>(new FCompiler(ImporterCommand, ImporterArguments, CompilerCommand, LinkerCommand, SharedLibExt, BuildTargets));
		}

		bool FCompiler::ImportOnnx(TConstArrayView<uint8> InFileData, const FString& InModelName, const FString& InOutputDir, TArray64<uint8>& OutMlirData)
		{
			return IREEUtils::ImportOnnx(ImporterCommand, ImporterArguments, InFileData, InModelName, InOutputDir, OutMlirData);
		}

		bool FCompiler::CompileMlir(TConstArrayView<uint8> InFileData, const FString& InModelName, const FString& InOutputDir, FNNERuntimeIREECompilerResultCPU& OutCompilerResult, UNNERuntimeIREEModuleMetaData& ModuleMetaData)
		{
			SCOPED_NAMED_EVENT_TEXT("FCompiler::CompileMlir", FColor::Magenta);

			using namespace Private;

			{
				SCOPED_NAMED_EVENT_TEXT("Metadata", FColor::Magenta);

				ModuleMetaData.ParseFromBuffer(InFileData);
			}

			IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

			FString InputFilePath = FPaths::Combine(InOutputDir, InModelName) + ".mlir";
			if (!PlatformFile.FileExists(*InputFilePath))
			{
				SCOPED_NAMED_EVENT_TEXT("InputFile", FColor::Magenta);

				FFileHelper::SaveArrayToFile(InFileData, *InputFilePath);
			}

			const FString PluginDir = FPaths::ConvertRelativePathToFull(*IPluginManager::Get().FindPlugin(UE_PLUGIN_NAME)->GetBaseDir());

			bool bResult = true;
			for (int32 i = 0; i < BuildTargets.Num(); i++)
			{
				FString IntermediateDirPath = FPaths::Combine(InOutputDir, BuildTargets[i].Architecture);
				PlatformFile.CreateDirectoryTree(*IntermediateDirPath);
				FString IntermediateFilePathNoExt = FPaths::Combine(IntermediateDirPath, InModelName);
				FString ObjectFilePath = IntermediateFilePathNoExt + ".o";
				FString VmfbFilePath = IntermediateFilePathNoExt + ".vmfb";
				FString SharedLibFilePath = IntermediateFilePathNoExt + SharedLibExt;

				FString CompilerArguments = BuildTargets[i].CompilerArguments;
				if (!IREEUtils::ResolveEnvironmentVariables(CompilerArguments))
				{
					UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu could not replace environment variables in %s"), *BuildTargets[i].CompilerArguments);
					bResult = false;
					continue;
				}
				CompilerArguments.ReplaceInline(*FString("${OBJECT_PATH}"), *(FString("\"") + ObjectFilePath + "\""));
				CompilerArguments.ReplaceInline(*FString("${VMFB_PATH}"), *(FString("\"") + VmfbFilePath + "\""));
				CompilerArguments.ReplaceInline(*FString("${INPUT_PATH}"), *(FString("\"") + InputFilePath + "\""));

				{
					SCOPED_NAMED_EVENT_TEXT("Compile", FColor::Magenta);

					IREEUtils::RunCommand(CompilerCommand, CompilerArguments, PluginDir, IntermediateFilePathNoExt + "_compile-log.txt");
				}

				if (!PlatformFile.FileExists(*ObjectFilePath) || !PlatformFile.FileExists(*VmfbFilePath))
				{
					UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu failed to compile the model \"%s\" using the command:"), *InputFilePath);
					UE_LOG(LogNNERuntimeIREE, Warning, TEXT("\"%s\" %s"), *CompilerCommand, *CompilerArguments);
					bResult = false;
					continue;
				}

				FString LinkerArguments = BuildTargets[i].LinkerArguments;
				if (!IREEUtils::ResolveEnvironmentVariables(LinkerArguments))
				{
					UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu could not replace environment variables in %s"), *BuildTargets[i].LinkerArguments);
					bResult = false;
					continue;
				}
				LinkerArguments.ReplaceInline(*FString("${OBJECT_PATH}"), *(FString("\"") + ObjectFilePath + "\""));
				LinkerArguments.ReplaceInline(*FString("${SHARED_LIB_PATH}"), *(FString("\"") + SharedLibFilePath + "\""));

				{
					SCOPED_NAMED_EVENT_TEXT("Link", FColor::Magenta);

					IREEUtils::RunCommand(LinkerCommand, LinkerArguments, PluginDir, IntermediateFilePathNoExt + "_link-log.txt");
				}

				if (!PlatformFile.FileExists(*SharedLibFilePath))
				{
					UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu failed to link the model \"%s\" using the command:"), *InputFilePath);
					UE_LOG(LogNNERuntimeIREE, Warning, TEXT("\"%s\" %s"), *LinkerCommand, *LinkerArguments);
					bResult = false;
					continue;
				}

				FString SharedLibraryEntryPointName = "";
				FString HeaderPath = IntermediateFilePathNoExt + ".h";
				if (!PlatformFile.FileExists(*HeaderPath))
				{
					UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu could not find the model header \"%s\""), *HeaderPath);
					bResult = false;
					continue;
				}
				FString HeaderString;
				if (!FFileHelper::LoadFileToString(HeaderString, *HeaderPath) || HeaderString.IsEmpty())
				{
					UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu could not read the model header \"%s\""), *HeaderPath);
					bResult = false;
					continue;
				}
				SharedLibraryEntryPointName = GetSharedLibraryEntryPointName(HeaderString);
				if (SharedLibraryEntryPointName.IsEmpty())
				{
					UE_LOG(LogNNERuntimeIREE, Warning, TEXT("UNNERuntimeIREECpu could not find the entry point in model header \"%s\""), *HeaderPath);
					bResult = false;
					continue;
				}

				FNNERuntimeIREEArchitectureInfoCPU ArchitectureInfo;
				ArchitectureInfo.Architecture = BuildTargets[i].Architecture;
				ArchitectureInfo.RelativeDirPath = BuildTargets[i].Architecture;
				ArchitectureInfo.SharedLibraryFileName = InModelName + SharedLibExt;
				ArchitectureInfo.VmfbFileName = InModelName + ".vmfb";
				ArchitectureInfo.SharedLibraryEntryPointName = SharedLibraryEntryPointName;
				OutCompilerResult.ArchitectureInfos.Add(MoveTemp(ArchitectureInfo));
			}

			bResult &= !OutCompilerResult.ArchitectureInfos.IsEmpty();

			if (!bResult)
			{
				OutCompilerResult.ArchitectureInfos.Empty();
			}

			return bResult;
		}
	} // CPU
} // UE::NNERuntimeIREE

#endif // WITH_EDITOR
#endif // WITH_NNE_RUNTIME_IREE