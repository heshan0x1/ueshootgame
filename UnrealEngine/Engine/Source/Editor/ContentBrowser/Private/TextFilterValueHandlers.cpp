// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextFilterValueHandlers.h"

#include "TextFilterValueHandler.h"
#include "UObject/Class.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TextFilterValueHandlers)

class FTextFilterString;
struct FContentBrowserItem;

bool UTextFilterValueHandlers::HandleTextFilterValue(const FContentBrowserItem& InContentBrowserItem, const FTextFilterString& InValue, const ETextFilterTextComparisonMode InTextComparisonMode, bool& bOutIsMatch)
{

	return false;
}
