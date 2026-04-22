// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cooker/GlobalCookArtifact.h"

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Cooker/CookTypes.h"
#include "CookOnTheSide/CookLog.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "HAL/PlatformFileManager.h"
#include "Hash/xxhash.h"
#include "Interfaces/ITargetPlatform.h"
#include "Logging/LogMacros.h"
#include "Misc/ConfigAccessTracking.h"
#include "Misc/StringBuilder.h"
#include "Modules/ModuleManager.h"
#include "Templates/UniquePtr.h"
#include "UObject/NameTypes.h"

namespace UE::Cook
{

FGlobalCookArtifact::FGlobalCookArtifact(UCookOnTheFlyServer& InCOTFS)
	: COTFS(InCOTFS)
{
}

FString FGlobalCookArtifact::GetArtifactName() const
{
	return FString(TEXT("global"));
}

static const TCHAR* TEXT_CookSettings(TEXT("CookSettings"));
static const TCHAR* TEXT_CookInProgress(TEXT("CookInProgress"));
static FName ExecutableHashName(TEXT("ExecutableHash"));
static FName ExecutableHashInvalidModuleName(TEXT("ExecutableHashInvalidModule"));

// TODO: Move into GlobalCookArtifact.cpp after review
FConfigFile FGlobalCookArtifact::CalculateCurrentSettings(ICookInfo& CookInfo, const ITargetPlatform* TargetPlatform)
{
	TMap<FName, FString> CookSettingStrings;
	const FName NAME_CookMode(TEXT("CookMode"));

	TArray<FModuleStatus> Modules;
	FModuleManager::Get().QueryModules(Modules);
	Modules.Sort([](const FModuleStatus& A, const FModuleStatus& B)
		{
			return A.FilePath.Compare(B.FilePath, ESearchCase::IgnoreCase) < 0;
		});
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	CookSettingStrings.Add(FName(TEXT("Version")), TEXT("21F52B9EDD4D456AB1AF381CA172BD28"));
	CookSettingStrings.Add(FName(TEXT("LegacyBuildDependencies")), ::LexToString(COTFS.bLegacyBuildDependencies));

	if (!COTFS.bLegacyBuildDependencies)
	{
		// Store the CookIncrementalVersion in the global settings, so that it will cause deletion of all artifacts
		// when it changes, in addition to invalidating all cooked packages.
		CookSettingStrings.Add(FName(TEXT("CookIncrementalVersion")), CookIncrementalVersion.ToString());
	}
	else
	{
		// Calculate the executable hash by combining the module file hash of every loaded module
		// TODO: Write the module file hash from UnrealBuildTool into the .modules file and read it
		// here from the .modules file instead of calculating it on every cook.
		if (COTFS.bLegacyIterativeCalculateExe)
		{
			FString InvalidModule;
			bool bValid = true;
			FXxHash64Builder Hasher;
			TArray<uint8> Buffer;
			for (FModuleStatus& ModuleStatus : Modules)
			{
				TUniquePtr<IFileHandle> FileHandle(PlatformFile.OpenRead(*ModuleStatus.FilePath));
				if (!FileHandle)
				{
					InvalidModule = ModuleStatus.FilePath;
					break;
				}
				int64 FileSize = FileHandle->Size();
				Buffer.SetNumUninitialized(FileSize, EAllowShrinking::No);
				if (!FileHandle->Read(Buffer.GetData(), FileSize))
				{
					InvalidModule = ModuleStatus.FilePath;
					break;
				}
				Hasher.Update(Buffer.GetData(), FileSize);
			}
			if (InvalidModule.IsEmpty())
			{
				CookSettingStrings.Add(ExecutableHashName, FString(*WriteToString<64>(Hasher.Finalize())));
			}
			else
			{
				CookSettingStrings.Add(ExecutableHashInvalidModuleName, InvalidModule);
			}
		}
	}

	if (CookInfo.GetCookType() == ECookType::ByTheBook)
	{
		CookSettingStrings.Add(NAME_CookMode, TEXT("CookByTheBook"));
		CookSettingStrings.Add(FName(TEXT("DLCName")), CookInfo.GetDLCName());
		if (!CookInfo.GetDLCName().IsEmpty() || !CookInfo.GetCreateReleaseVersion().IsEmpty())
		{
			CookSettingStrings.Add(FName(TEXT("BasedOnReleaseVersion")), CookInfo.GetBasedOnReleaseVersion());
		}
	}
	else
	{
		check(CookInfo.GetCookType() == ECookType::OnTheFly);
		CookSettingStrings.Add(NAME_CookMode, TEXT("CookOnTheFly"));
	}

	CookSettingStrings.Add(FName(TEXT_CookInProgress), TEXT("true"));

	FConfigSection ConfigSection;
	FConfigFile ConfigFile;
	for (TPair<FName, FString>& CurrentSetting : CookSettingStrings)
	{
		ConfigSection.Add(CurrentSetting.Key, FConfigValue(MoveTemp(CurrentSetting.Value)));
	}
	ConfigFile.Add(TEXT_CookSettings, MoveTemp(ConfigSection));
	return ConfigFile;
}

void FGlobalCookArtifact::CompareSettings(UE::Cook::Artifact::FCompareSettingsContext& Context)
{
	// For the global settings, we only use RequestFullRecook; we have no files to invalidate.
	const ITargetPlatform* TargetPlatform = Context.GetTargetPlatform();

	const FConfigSection* PreviousSettings = Context.GetPrevious().FindSection(TEXT_CookSettings);
	if (PreviousSettings == nullptr)
	{
		UE_LOG(LogCook, Display, TEXT("Cook invalidated for platform %s because CookSettings file %s is invalid. Clearing previously cooked packages."),
			*TargetPlatform->PlatformName(), *Context.GetPreviousFileName());
		Context.RequestFullRecook(true);
		return;
	}

	TSet<FName> IgnoreKeys;
	IgnoreKeys.Add(FName(TEXT_CookInProgress));
	IgnoreKeys.Add(ExecutableHashName);
	IgnoreKeys.Add(ExecutableHashInvalidModuleName);

	const FConfigSection* CurrentSettings = Context.GetCurrent().FindSection(TEXT_CookSettings);
	check(CurrentSettings);
	for (const TPair<FName, FConfigValue>& CurrentSetting : *CurrentSettings)
	{
		if (IgnoreKeys.Contains(CurrentSetting.Key))
		{
			continue;
		}
		const FConfigValue* PreviousSetting = PreviousSettings->Find(CurrentSetting.Key);
		if (!PreviousSetting || PreviousSetting->GetValue() != CurrentSetting.Value.GetValue())
		{
			UE_LOG(LogCook, Display, TEXT("Cook invalidated for platform %s because %s has changed. Old: %s, New: %s. Clearing previously cooked packages."),
				*TargetPlatform->PlatformName(), *CurrentSetting.Key.ToString(),
				PreviousSetting ? *PreviousSetting->GetValue() : TEXT(""),
				*CurrentSetting.Value.GetValue());
			Context.RequestFullRecook(true);
			return;
		}
	}

	if (GIsBuildMachine)
	{
		bool bCookInProgress;
		if (Context.GetPrevious().GetBool(TEXT_CookSettings, TEXT_CookInProgress, bCookInProgress) && bCookInProgress)
		{
			UE_LOG(LogCook, Display, TEXT("Cook invalidated for platform %s because the previous cook crashed (or otherwise did not report completion).")
				TEXT(" CookSettings file %s still has [%s]:%s=true. Clearing previously cooked packages."),
				*TargetPlatform->PlatformName(), *Context.GetPreviousFileName(), TEXT_CookSettings, TEXT_CookInProgress);
			Context.RequestFullRecook(true);
			return;
		}
	}

	if (!COTFS.bLegacyIterativeIgnoreIni && COTFS.bLegacyBuildDependencies && COTFS.IniSettingsOutOfDate(TargetPlatform))
	{
		UE_LOG(LogCook, Display, TEXT("Cook invalidated for platform %s because ini settings have changed. Clearing previously cooked packages."),
			*TargetPlatform->PlatformName());
		Context.RequestFullRecook(true);
		return;
	}

	if (!COTFS.bLegacyIterativeIgnoreExe && COTFS.bLegacyBuildDependencies)
	{
		const FConfigValue* CurrentHash = CurrentSettings->Find(ExecutableHashName);
		if (!CurrentHash)
		{
			UE_LOG(LogCook, Display, TEXT("Cook invalidated for platform %s because current executable hash is invalid. Invalid module=%s. Clearing previously cooked packages."),
				*TargetPlatform->PlatformName(), *CurrentSettings->FindRef(ExecutableHashInvalidModuleName).GetValue());
			Context.RequestFullRecook(true);
			return;
		}
		const FConfigValue* PreviousHash = PreviousSettings->Find(ExecutableHashName);
		if (!PreviousHash)
		{
			const FConfigValue* InvalidModuleName = PreviousSettings->Find(ExecutableHashInvalidModuleName);
			UE_LOG(LogCook, Display, TEXT("Cook invalidated for platform %s because old executable hash is invalid. Invalid module=%s. Clearing previously cooked packages."),
				*TargetPlatform->PlatformName(), InvalidModuleName ? *InvalidModuleName->GetValue() : TEXT(""));
			Context.RequestFullRecook(true);
			return;
		}
		if (!CurrentHash->GetValue().Equals(*PreviousHash->GetValue(), ESearchCase::CaseSensitive))
		{
			UE_LOG(LogCook, Display, TEXT("Cook invalidated for platform %s because executable hash has changed. Old: %s, New: %s. Clearing previously cooked packages."),
				*TargetPlatform->PlatformName(), *PreviousHash->GetValue(), *CurrentHash->GetValue());
			Context.RequestFullRecook(true);
			return;
		}
	}
}

} // namespace UE::Cook

void UCookOnTheFlyServer::ClearCookInProgressFlagFromGlobalCookSettings(const ITargetPlatform* TargetPlatform) const
{
	using namespace UE::Cook;

	UE::ConfigAccessTracking::FIgnoreScope IgnoreScope;
	FConfigFile ConfigFile;
	check(GlobalArtifact);
	FString ArtifactName = GlobalArtifact->GetArtifactName();
	FString Filename = GetCookSettingsFileName(TargetPlatform, ArtifactName);
	ConfigFile.Read(Filename);
	ConfigFile.RemoveKeyFromSection(TEXT_CookSettings, TEXT_CookInProgress);
	SaveCookSettings(ConfigFile, TargetPlatform, ArtifactName);
}
