// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TaggedAssetBrowserMenuFilters.h"
#include "UObject/Object.h"
#include "MRUFavoritesList.h"
#include "TaggedAssetBrowser_CommonFilters.generated.h"

class UTaggedAssetBrowserSection;

UCLASS()
class UTaggedAssetBrowserFilterRoot : public UHierarchyRoot
{
	GENERATED_BODY()
};

USTRUCT()
struct FTaggedAssetBrowserSectionIconData
{
	GENERATED_BODY()

	/** If true, will use a texture. */
	UPROPERTY(EditAnywhere, Category="Icon")
	bool bUseTextureForIcon = false;

	/** The actual name of the icon to use within the styleset. */
	UPROPERTY(EditAnywhere, Category="Icon", meta=(EditCondition="bUseTextureForIcon==false"))
	FName StyleName;

	/** The icon to display for this section. */
	UPROPERTY(EditAnywhere, Category = "Icon", meta=(EditCondition="bUseTextureForIcon==true"))
	TObjectPtr<UTexture2D> Icon;

	const FSlateBrush* GetImageBrush() const;
private:
	mutable FSlateBrush TextureBrush;
};

/** The section class for the tagged asset browser. Can contain additional filters.*/
UCLASS(MinimalAPI)
class UTaggedAssetBrowserSection : public UHierarchySection
{
	GENERATED_BODY()

public:
	UTaggedAssetBrowserSection() = default;

	UPROPERTY(EditAnywhere, Category = "Section", meta=(ShowOnlyInnerProperties))
	FTaggedAssetBrowserSectionIconData IconData;

	/** A list of filters that can be specified optionally. This way, a section can display its associated elements, OR additionally filter assets by itself. */
	UPROPERTY(EditAnywhere, Category = "Section", Instanced)
	TArray<TObjectPtr<UTaggedAssetBrowserFilterBase>> Filters;
};

/** Show all assets. */
UCLASS(DisplayName="All")
class USERASSETTAGSEDITOR_API UTaggedAssetBrowserFilter_All : public UTaggedAssetBrowserFilterBase
{
	GENERATED_BODY()
public:
	UTaggedAssetBrowserFilter_All();
};

/** A filter that returns assets that have the specified User Asset Tag. Nested User Asset Tag filters will be combined. */
UCLASS(DisplayName="User Asset Tag")
class USERASSETTAGSEDITOR_API UTaggedAssetBrowserFilter_UserAssetTag : public UTaggedAssetBrowserFilterBase
{
	GENERATED_BODY()
public:
	UTaggedAssetBrowserFilter_UserAssetTag() = default;
	
	virtual FString ToString() const override;
	virtual TOptional<FString> GetInstanceIdentifier() const override { return ToString(); }
	virtual FSlateIcon GetIcon() const override;
	virtual void ModifyARFilterInternal(FARFilter& Filter) const override;
	virtual bool ShouldFilterAssetInternal(const FAssetData& InAssetData) const override;

	void SetUserAssetTag(FName InUserAssetTag);
	bool DoesAssetHaveTag(const FAssetData& AssetCandidate) const;

protected:
	UPROPERTY(EditAnywhere, Category="Filter")
	FName UserAssetTag;
};

/** A collection of multiple User Asset Tags. Will show all assets that own at least one of the contained tags. */
UCLASS(DisplayName="User Asset Tag Collection")
class USERASSETTAGSEDITOR_API UTaggedAssetBrowserFilter_UserAssetTagCollection : public UTaggedAssetBrowserFilterBase
{
	GENERATED_BODY()
public:
	UTaggedAssetBrowserFilter_UserAssetTagCollection();

	virtual FString ToString() const override;
	virtual TOptional<FString> GetInstanceIdentifier() const override { return ToString(); }
	virtual FText GetTooltip() const override;
	virtual FSlateIcon GetIcon() const override;
	virtual void CreateAdditionalWidgets(TSharedPtr<SHorizontalBox> ExtensionBox) override;

	virtual void ModifyARFilterInternal(FARFilter& Filter) const override;
	virtual bool ShouldFilterAssetInternal(const FAssetData& InAssetData) const override;
	
	void SetCollectionName(FName InName);
	
protected:
	UPROPERTY(EditAnywhere, Category="Filter")
	FName Name;
	
	UPROPERTY(EditAnywhere, Category="Filter", meta=(MultiLine))
	FText Description;
	
private:
	FText GetContainedChildrenNumberText() const;
	EVisibility GetContainedChildrenNumberTextVisibility() const;
	FText GetContainedChildrenNumberTooltip() const;
};

/** Filter for recently used assets. */
UCLASS(DisplayName="Recent")
class USERASSETTAGSEDITOR_API UTaggedAssetBrowserFilter_Recent : public UTaggedAssetBrowserFilterBase
{
	GENERATED_BODY()
public:
	UTaggedAssetBrowserFilter_Recent() = default;
	
	virtual void InitializeInternal(const FTaggedAssetBrowserContext& InContext) override;
	virtual bool ShouldFilterAssetInternal(const FAssetData& InAssetData) const override;
	virtual FSlateIcon GetIcon() const override;

	bool IsAssetRecent(const FAssetData& AssetCandidate) const;

	TAttribute<const FMainMRUFavoritesList*> ListAttribute;
};

/** Filter for assets based on directories. */
UCLASS(DisplayName="Directories")
class USERASSETTAGSEDITOR_API UTaggedAssetBrowserFilter_Directories : public UTaggedAssetBrowserFilterBase
{
	GENERATED_BODY()
public:
	UTaggedAssetBrowserFilter_Directories() = default;
	
	virtual void ModifyARFilterInternal(FARFilter& Filter) const override;
	virtual FSlateIcon GetIcon() const override;

	UPROPERTY(EditAnywhere, Category="Filter", meta=(ContentDir))
	TArray<FDirectoryPath> DirectoryPaths;
};

/** Filter for assets based on class. */
UCLASS(DisplayName="Classes")
class USERASSETTAGSEDITOR_API UTaggedAssetBrowserFilter_Class : public UTaggedAssetBrowserFilterBase
{
	GENERATED_BODY()
public:
	UTaggedAssetBrowserFilter_Class() = default;
	
	virtual void ModifyARFilterInternal(FARFilter& Filter) const override;

	UPROPERTY(EditAnywhere, Category="Filter")
	TArray<TObjectPtr<UClass>> Classes;
};
