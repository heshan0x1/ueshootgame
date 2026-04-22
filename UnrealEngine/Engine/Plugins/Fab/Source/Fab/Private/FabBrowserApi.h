// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Workflows/FabWorkflow.h"

#include "FabBrowserApi.generated.h"

USTRUCT()
struct FFabApiVersion
{
	GENERATED_BODY()

	// These three uproperties need to be in this case (NOT PascalCase)
	// otherwise we will break compatibility with the engine plugin 
	// Fab's JS on the macOS WebKit WebBrowser plugin backend.
	UPROPERTY()
	FString ue;

	UPROPERTY()
	FString api;

	UPROPERTY()
	FString pluginversion;

	UPROPERTY()
	FString platform;
};

USTRUCT()
struct FFabFrontendSettings
{
	GENERATED_BODY()

	// These two uproperties need to be in this case (NOT PascalCase)
	// otherwise we will break compatibility with the engine plugin 
	// Fab's JS on the macOS WebKit WebBrowser plugin backend.
	UPROPERTY()
	FString preferredformat;

	UPROPERTY()
	FString preferredquality;
};

UCLASS()
class UFabBrowserApi : public UObject
{
	GENERATED_BODY()
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnSignedUrlGenerated, const FString& /*DownloadUrl*/, FFabAssetMetadata /*Metadata*/);

private:
	FOnSignedUrlGenerated OnSignedUrlGeneratedDelegate;
	void CompleteWorkflow(const FString& Id);

public:
	TArray<TSharedPtr<IFabWorkflow>> ActiveWorkflows;

public:
	UFUNCTION()
	void AddToProject(const FString& DownloadUrl, const FFabAssetMetadata& AssetMetadata);

	UFUNCTION()
	void DragStart(const FFabAssetMetadata& AssetMetadata);

	UFUNCTION()
	void OnDragInfoSuccess(const FString& DownloadUrl, const FFabAssetMetadata& AssetMetadata);

	UFUNCTION()
	void OnDragInfoFailure(const FString& AssetId);

	UFUNCTION()
	void Login();

	UFUNCTION()
	void Logout();

	UFUNCTION()
	FString GetAuthToken();

	UFUNCTION()
	FString GetRefreshToken();

	UFUNCTION()
	void OpenPluginSettings();

	UFUNCTION()
	FFabFrontendSettings GetSettings();

	UFUNCTION()
	void SetPreferredQualityTier(const FString& PreferredQuality);

	UFUNCTION()
	static FFabApiVersion GetApiVersion();

	UFUNCTION()
	void OpenUrlInBrowser(const FString& Url);

	UFUNCTION()
	void CopyToClipboard(const FString& Content);

	UFUNCTION()
	void PluginOpened();

	UFUNCTION()
	FString GetUrl();

	FDelegateHandle AddSignedUrlCallback(TFunction<void(const FString&, const FFabAssetMetadata&)> Callback);
	FOnSignedUrlGenerated& OnSignedUrlGenerated() { return OnSignedUrlGeneratedDelegate; }
	void RemoveSignedUrlHandle(const FDelegateHandle& Handle) { OnSignedUrlGeneratedDelegate.Remove(Handle); }
};
