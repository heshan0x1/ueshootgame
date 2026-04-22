// Copyright Epic Games, Inc. All Rights Reserved.

#include "AIAssistantConfig.h"

#include "Containers/Set.h"
#include "Misc/FileHelper.h"
#include "Misc/Optional.h"
#include "Misc/Paths.h"
#include "Templates/UnrealTemplate.h"

#include "AIAssistantLog.h"

const FString FAIAssistantConfig::DefaultFilename("AIAssistant.json");

const FString FAIAssistantConfig::DefaultMainUrl(
	"https://dev.epicgames.com/community/assistant/embedded");

TArray<FString> FAIAssistantConfig::GetDefaultSearchDirectories()
{
	TArray<FString> BaseSearchPaths =
	{
		FPaths::EngineConfigDir(),
		FPaths::EngineUserDir(),
		FPaths::EngineVersionAgnosticUserDir(),
	};
	TArray<FString> AllPathsToSearch;
	TArray<TOptional<FPaths::EPathConversion>> PathConversions =
	{
		TOptional(FPaths::EPathConversion::Engine_NotForLicensees),
		TOptional(FPaths::EPathConversion::Engine_NoRedist),
		TOptional(FPaths::EPathConversion::Engine_LimitedAccess),
		TOptional<FPaths::EPathConversion>(),
	};
	for (const TOptional<FPaths::EPathConversion>& PathConversion : PathConversions)
	{
		for (const FString& BaseSearchPath : BaseSearchPaths)
		{
			AllPathsToSearch.Emplace(
				PathConversion.IsSet()
				? FPaths::ConvertPath(BaseSearchPath, PathConversion.GetValue())
				: BaseSearchPath);
		}
		
	}
	return AllPathsToSearch;
}

FString FAIAssistantConfig::FindConfigFile(const TArray<FString>& SearchDirectories)
{
	for (auto SearchDirectory : SearchDirectories)
	{
		auto FullFilename = FPaths::Combine(SearchDirectory, DefaultFilename);
		UE_LOG(
			LogAIAssistant, Verbose, TEXT("Searching for AI assistant config in %s"),
			*FullFilename);
		if (FPaths::FileExists(FullFilename))
		{
			return FullFilename;
		}
	}
	return FString();
}

FAIAssistantConfig FAIAssistantConfig::Load(const FString& Filename)
{
	FString Json(TEXT("{}"));
	if (!Filename.IsEmpty() && !FFileHelper::LoadFileToString(Json, *Filename))
	{
		UE_LOG(
			LogAIAssistant, Error, TEXT("Failed to load AI assistant config from \"%s\"."),
			*Filename);
	}
	FAIAssistantConfig Config;
	if (!Config.FromJson(Json))
	{
		UE_LOG(
			LogAIAssistant, Error, TEXT("Failed to load AI assistant config \"%s\" from \"%s\"."),
			*Json, *Filename);
		// Use the default config.
		(void)Config.FromJson(FString());
	}
	return Config;
}