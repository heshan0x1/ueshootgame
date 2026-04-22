// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorConfigBase.h"
#include "UserAssetTagProvider.h"
#include "UObject/StrongObjectPtrTemplates.h"
#include "UserAssetTagsEditorConfig.generated.h"

USTRUCT()
struct FPerUserAssetTagProviderViewOptions
{
	GENERATED_BODY()

	UPROPERTY()
	bool bEnabled = true;

	/** 0: Section; 1: SubMenu. */
	UPROPERTY()
	EUserAssetTagProviderMenuType MenuType = EUserAssetTagProviderMenuType::Section;
};

USTRUCT()
struct FUserAssetTagProviderViewOptions
{
	GENERATED_BODY()
	
	UPROPERTY()
	TMap<FName, FPerUserAssetTagProviderViewOptions> PerProviderViewOptions;
};
/**
 * 
 */
UCLASS(EditorConfig=UserAssetTags)
class USERASSETTAGSEDITOR_API UUserAssetTagsEditorConfig : public UEditorConfigBase
{
	GENERATED_BODY()

public:
	void ToggleSortByAlphabet();
	bool ShouldSortByAlphabet() const;
	
	bool IsProviderEnabled(const UClass* ProviderClass) const;
	void ToggleProviderEnabled(const UClass* ProviderClass);
	
	EUserAssetTagProviderMenuType GetProviderMenuType(const UClass* ProviderClass) const;
	void SetProviderMenuType(const UClass* ProviderClass, EUserAssetTagProviderMenuType InMenuType);

	static UUserAssetTagsEditorConfig* Get();
	static void Shutdown();
private:
	UPROPERTY(meta=(EditorConfig))
	bool bSortByAlphabet = false;
	
	UPROPERTY(meta=(EditorConfig))
	FUserAssetTagProviderViewOptions ProviderViewOptions;
private:
    static TStrongObjectPtr<UUserAssetTagsEditorConfig> Instance;
};
