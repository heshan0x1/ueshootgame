// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorConfigBase.h"
#include "TaggedAssetBrowserConfig.generated.h"

USTRUCT()
struct FPerTaggedAssetBrowserSavedState
{
	GENERATED_BODY()
	
	UPROPERTY()
	TArray<FName> PrimaryFilterSelection;
};

UCLASS(EditorConfig="TaggedAssetBrowser")
class USERASSETTAGSEDITOR_API UTaggedAssetBrowserConfig : public UEditorConfigBase
{
	GENERATED_BODY()
public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPropertyChanged, const FPropertyChangedEvent&);
		
	static UTaggedAssetBrowserConfig* Get();
	static void Shutdown();
	
	FOnPropertyChanged& OnPropertyChanged() { return OnPropertyChangedDelegate; }
	
	UPROPERTY(meta=(EditorConfig))
	TMap<FName, FPerTaggedAssetBrowserSavedState> PerTaggedAssetBrowserSettings;
	
	UPROPERTY(meta=(EditorConfig))
    bool bShowHiddenAssets = false;
    	
	UPROPERTY(meta=(EditorConfig))
	bool bShowDeprecatedAssets = false;

	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;	
private:
	static TStrongObjectPtr<UTaggedAssetBrowserConfig> Instance;

	FOnPropertyChanged OnPropertyChangedDelegate;
};
