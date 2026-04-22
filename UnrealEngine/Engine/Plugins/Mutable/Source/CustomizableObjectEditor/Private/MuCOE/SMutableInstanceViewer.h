// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Instance.h"
#include "MuR/System.h"
#include "UObject/GCObject.h"
#include "Widgets/SCompoundWidget.h"

class FUnrealMutableResourceProvider;
template <typename ItemType> class SListView;

class ITableRow;
class SBorder;
class SMutableMeshViewport;
class SScrollBox;
class SSplitter;
class STableViewBase;
class SWidget;


/** */
struct FMutableInstanceViewerSurfaceElement
{
	FText Id;
	FText SharedId;
	FText CustomId;
	FText ImageCount;
	FText VectorCount;
	FText ScalarCount;
	FText StringCount;
};


/** */
struct FMutableInstanceViewerLODElement
{
	FText LODIndex;
	FText MeshId;
	TSharedPtr<TArray<TSharedPtr<FMutableInstanceViewerSurfaceElement>>> Surfaces;
};


/** */
struct FMutableInstanceViewerComponentElement
{
	FText ComponentIndex;
	TSharedPtr<TArray<TSharedPtr<FMutableInstanceViewerLODElement>>> LODs;
};


/** Widget designed to show the statistical data from a Mutable Instance*/
class SMutableInstanceViewer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMutableInstanceViewer) {}
	SLATE_END_ARGS()

	/** Builds the widget */
	void Construct(const FArguments& InArgs, const TSharedPtr<FUnrealMutableResourceProvider>& InExternalResourceProvider);

	/** Set the Mutable Instance to be used for this widget */
	void SetInstance(const TSharedPtr<const UE::Mutable::Private::FInstance>&, const TSharedPtr<const UE::Mutable::Private::FModel>&, const TSharedPtr<const UE::Mutable::Private::FParameters>&, const UE::Mutable::Private::FSystem&);
	
	/** */
	TSharedRef<SWidget> GenerateLODsListView(const TSharedPtr<TArray<TSharedPtr<FMutableInstanceViewerLODElement>>>& InBufferChannelElements);
	TSharedRef<SWidget> GenerateSurfaceListView(const TSharedPtr<TArray<TSharedPtr<FMutableInstanceViewerSurfaceElement>>>& InBufferChannelElements);

private:
	TSharedPtr<FUnrealMutableResourceProvider> ExternalResourceProvider;
	
	/** Data backend for the widget. It represents the Instance that is being "displayed" */
	TSharedPtr<const UE::Mutable::Private::FInstance> MutableInstance = nullptr;

	/** Splitter used to separate the two sides of the slate (tables and viewport) */
	TSharedPtr<SSplitter> SpaceSplitter;

	/** Slate object containing all the buffer tables alongside with the bone tree */
	TSharedPtr<SScrollBox> DataSpaceSlate;

	/** Viewport object to preview the current Instance inside an actual Unreal scene */
	TSharedPtr<SMutableMeshViewport> InstanceViewport;
	
	/** */	
	TSharedPtr<SListView<TSharedPtr<FMutableInstanceViewerComponentElement>>> ComponentsSlateView;

private:

	/** Generates all slate objects related with the Instance Viewport Slate */
	TSharedRef<SWidget> GenerateViewportSlates();

	/** Generates the tables showing the buffer data on the Instance alongside with the bone tree found on the mutable Instance */
	TSharedRef<SWidget> GenerateDataTableSlates();
	
	/** */	
	TArray<TSharedPtr<FMutableInstanceViewerComponentElement>> Components;
	
	/** */
	TSharedRef<SWidget> GenerateComponentsListView();
	
	/**  */
	TSharedRef<ITableRow> OnGenerateComponentRow(TSharedPtr<FMutableInstanceViewerComponentElement>, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> OnGenerateLODRow(TSharedPtr<FMutableInstanceViewerLODElement>, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> OnGenerateSurfaceRow(TSharedPtr<FMutableInstanceViewerSurfaceElement>, const TSharedRef<STableViewBase>& OwnerTable);

};
