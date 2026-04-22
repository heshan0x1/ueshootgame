// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#define UE_API ASSETDEFINITION_API

/**
 * The asset category path is how we know how to build menus around assets.  For example, Basic is generally the ones
 * we expose at the top level, where as everything else is a category with a pull out menu, and the subcategory would
 * be where it gets placed in a submenu inside of there.
 */
struct FAssetCategoryPath
{
	UE_API FAssetCategoryPath(const FText& InCategory);
	UE_API FAssetCategoryPath(const FText& InCategory, const FText& SubCategory);
	UE_API FAssetCategoryPath(const FAssetCategoryPath& InCategory, const FText& InSubCategory);
	UE_API FAssetCategoryPath(TConstArrayView<FText> InCategoryPath);

	FName GetCategory() const { return CategoryPath[0].Key; }
	FText GetCategoryText() const { return CategoryPath[0].Value; }
	
	bool HasSubCategory() const { return CategoryPath.Num() > 1; }
	int32 NumSubCategories() const { return HasSubCategory() ? (CategoryPath.Num() - 1) : 0; }
	FName GetSubCategory() const { return HasSubCategory() ? CategoryPath[1].Key : NAME_None; }
	FText GetSubCategoryText() const { return HasSubCategory() ? CategoryPath[1].Value : FText::GetEmpty(); }

	UE_API void GetSubCategories(TArray<FName>& SubCategories) const;
	UE_API void GetSubCategoriesText(TArray<FText>& SubCategories) const;
	
	FAssetCategoryPath operator / (const FText& SubCategory) const { return FAssetCategoryPath(*this, SubCategory); }
	
private:
	TArray<TPair<FName, FText>> CategoryPath;
};

#undef UE_API
