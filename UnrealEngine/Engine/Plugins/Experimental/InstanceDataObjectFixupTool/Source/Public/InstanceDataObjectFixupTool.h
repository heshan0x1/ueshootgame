// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AsyncDetailViewDiff.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Docking/SDockTab.h"

#define UE_API INSTANCEDATAOBJECTFIXUPTOOL_API

class SDetailsSplitter;
class FInstanceDataObjectFixupPanel;
struct FPropertySoftPath;
class IStructureDetailsView;

/**
 * This tool diffs multiple property bags of one format against the same number of property bags of another format.
 * 
 */
class SInstanceDataObjectFixupTool : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInstanceDataObjectFixupTool)
	{}
		SLATE_ARGUMENT(TConstArrayView<TObjectPtr<UObject>>, InstanceDataObjects)
		SLATE_ARGUMENT(TObjectPtr<UObject>, InstanceDataObjectsOwner)
	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	UE_API void Construct(const FArguments& InArgs);
	UE_API virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	
	UE_API void SetDockTab(const TSharedRef<SDockTab>& DockTab);
	UE_API void GenerateDetailsViews();
	static UE_API FLinearColor GetRowHighlightColor(const TUniquePtr<FAsyncDetailViewDiff::DiffNodeType>& DiffNode);
	UE_API bool IsResolved() const;
	UE_API FReply OnAutoMarkForDeletion() const;
	
private:
	UE_API FReply OnConfirmClicked() const;
	
	TWeakPtr<SDockTab> OwningDockTab;
	TStaticArray<TSharedPtr<FInstanceDataObjectFixupPanel>, 2> Panels;
	TSharedPtr<FAsyncDetailViewDiff> PanelDiff;

	TSharedPtr<SDetailsSplitter> Splitter;
};

#undef UE_API
