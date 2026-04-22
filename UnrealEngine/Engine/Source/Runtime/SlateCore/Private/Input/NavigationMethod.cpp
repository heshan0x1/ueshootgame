// Copyright Epic Games, Inc. All Rights Reserved.

#include "Input/NavigationMethod.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavigationMethod)

#define LOCTEXT_NAMESPACE "NavigationMethod"

#if WITH_SLATE_DEBUGGING

FText FNavigationMethod::FDebuggingText::Valid = LOCTEXT("StateValid", "Valid"); //~ The widget is valid will be consider as the result
FText FNavigationMethod::FDebuggingText::NotCompatibleWithUserIndex = LOCTEXT("StateNotCompatibleWithUserIndex", "User Index not compatible"); //~ The widget is not compatible with the requested user index
FText FNavigationMethod::FDebuggingText::DoesNotIntersect = LOCTEXT("StateDoesNotIntersect", "Does not intersect"); //~ The widget rect is not in the correct direction or is not intersecting with the "swept" rectangle
FText FNavigationMethod::FDebuggingText::PreviousWidgetIsBetter = LOCTEXT("StatePreviousWidgetIsBetter", "Previous Widget was better"); //~ The widget would be valid but the previous valid is closer
FText FNavigationMethod::FDebuggingText::NotADescendant = LOCTEXT("StateNotADescendant", "Not a descendant"); //~ We have a non escape boundary condition and the widget isn't a descendant of our boundary
FText FNavigationMethod::FDebuggingText::Disabled = LOCTEXT("StateNotEnabled", "Disabled"); //~ The widget is not enabled
FText FNavigationMethod::FDebuggingText::ParentDisabled = LOCTEXT("StateParentNotEnabled", "ParentDisabled"); //~ A parent of the widget is disabled
FText FNavigationMethod::FDebuggingText::DoesNotSuportKeyboardFocus = LOCTEXT("StateDoesNotSuportKeyboardFocus", "Keyboard focus unsupported"); //~ THe widget does not support keyboard focus
#endif // WITH_SLATE_DEBUGGING

void FNavigationMethod::Initialize(const FHittestGrid* InHittestGrid, TArray<FDebugWidgetResult>* InIntermediateResultsPtr)
{
	HittestGrid = InHittestGrid;
	IntermediateResultsPtr = InIntermediateResultsPtr;
	DisabledDestinations.Empty();
}

TSharedPtr<SWidget> FNavigationMethod::FindNextFocusableWidget(const FArrangedWidget& StartingWidget, const EUINavigation Direction, const FNavigationReply& NavigationReply, const FArrangedWidget& RuleWidget, int32 InUserIndex) 
{ 
	return StartingWidget.Widget; 
}

FIntPoint FNavigationMethod::GetCellCoordinate(FVector2f Position) const
{
	check(HittestGrid);
	return HittestGrid->GetCellCoordinate(Position);
}

bool FNavigationMethod::IsValidCellCoordinate(int32 X, int32 Y) const
{	
	check(HittestGrid);
	return HittestGrid->IsValidCellCoord(X, Y);
}

bool FNavigationMethod::IsParentsEnabled(const SWidget* Widget)
{
	while (Widget)
	{
		if (!Widget->IsEnabled())
		{
			return false;
		}
		Widget = Widget->Advanced_GetPaintParentWidget().Get();
	}
	return true;
}

void FNavigationMethod::ForEachFocusableWidgetsInCell(int32 X, int32 Y, const FNavigationReply& NavigationReply, int32 UserIndex, FWidgetFunc WidgetFunc)
{
	check(HittestGrid);

	FHittestGrid::FCollapsedWidgetsArray WidgetIndexes;
	HittestGrid->GetCollapsedWidgets(WidgetIndexes, X, Y);

	for (int32 WidgetIndex = WidgetIndexes.Num() - 1; WidgetIndex >= 0; --WidgetIndex)
	{
		const FHittestGrid::FWidgetData& WidgetData = WidgetIndexes[WidgetIndex].GetWidgetData();
		const TSharedPtr<SWidget> Widget = WidgetData.GetWidget();
		if (!Widget.IsValid())
		{
			continue;
		}

		if (!HittestGrid->IsCompatibleUserIndex(UserIndex, WidgetData.UserIndex))
		{
#if WITH_SLATE_DEBUGGING
			AddDebugIntermediateResult(Widget, FDebuggingText::NotCompatibleWithUserIndex);
#endif
			continue;
		}

		// If we have a non escape boundary condition and this widget isn't a descendant of our boundary condition widget then it's invalid so we keep looking.
		if (NavigationReply.GetBoundaryRule() != EUINavigationRule::Escape
			&& NavigationReply.GetHandler().IsValid()
			&& !HittestGrid->IsDescendantOf(NavigationReply.GetHandler().Get(), WidgetData))
		{
#if WITH_SLATE_DEBUGGING
			AddDebugIntermediateResult(Widget, FDebuggingText::NotADescendant);
#endif
			continue;
		}

		if (!Widget->IsEnabled())
		{
#if WITH_SLATE_DEBUGGING
			AddDebugIntermediateResult(Widget, FDebuggingText::Disabled);
#endif
			continue;
		}

		if (!Widget->SupportsKeyboardFocus())
		{
#if WITH_SLATE_DEBUGGING
			AddDebugIntermediateResult(Widget, FDebuggingText::DoesNotSuportKeyboardFocus);
#endif
			continue;
		}

		if (DisabledDestinations.Contains(Widget))
		{
#if WITH_SLATE_DEBUGGING
			AddDebugIntermediateResult(Widget, FDebuggingText::ParentDisabled);
#endif
			continue;
		}

		bool bValid = WidgetFunc(Widget);
		if (bValid)
		{
#if WITH_SLATE_DEBUGGING
			AddDebugIntermediateResult(Widget, FDebuggingText::Valid);
#endif
		}
	}
}

#if WITH_SLATE_DEBUGGING

void FNavigationMethod::DrawDebug(int32 InLayer, const FGeometry& AllottedGeometry, FSlateWindowElementList& WindowElementList)
{
	(void)InLayer;
	(void)AllottedGeometry;
	(void)WindowElementList;
}

void FNavigationMethod::AddDebugIntermediateResult(const TSharedPtr<const SWidget>& InWidget, const FText Result) 
{ 
	if (IntermediateResultsPtr) 
	{ 
		IntermediateResultsPtr->Emplace(InWidget, Result); 
	} 
}

#endif //WITH_SLATE_DEBUGGING

#undef LOCTEXT_NAMESPACE
