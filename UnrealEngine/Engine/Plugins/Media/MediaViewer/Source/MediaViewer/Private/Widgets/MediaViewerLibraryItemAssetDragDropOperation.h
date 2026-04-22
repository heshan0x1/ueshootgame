// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DragAndDrop/AssetDragDropOp.h"

#include "Library/IMediaViewerLibrary.h"
#include "Templates/SharedPointer.h"

class SBorder;
enum class EItemDropZone;
struct FMediaViewerLibraryEntry;
struct FMediaViewerLibraryGroup;
struct FMediaViewerLibraryItem;
struct FSlateBrush;

namespace UE::MediaViewer
{
	enum class EMediaImageViewerPosition : uint8;
}

namespace UE::MediaViewer::Private
{

class FMediaViewerLibraryItemAssetDragDropOperation : public FAssetDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FMediaViewerLibraryItemAssetDragDropOperation, FAssetDragDropOp)

	FMediaViewerLibraryItemAssetDragDropOperation(const TSharedRef<IMediaViewerLibrary>& InLibrary,
		const IMediaViewerLibrary::FGroupItem& InGroupItem);
	
	virtual ~FMediaViewerLibraryItemAssetDragDropOperation() override = default;

	const IMediaViewerLibrary::FGroupItem& GetGroupItem() const;

	//~ Begin FDragDropOp
	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;
	virtual FCursorReply OnCursorQuery() override;
	//~ End FDragDropOp

protected:
	IMediaViewerLibrary::FGroupItem GroupItem;
	TSharedPtr<FSlateBrush> ThumbnailBrush;
	TSharedRef<SBorder> Decorator;

	void CreateDecorator(const TSharedPtr<FMediaViewerLibraryItem>& InItem);
};

} // UE::MediaViewer::Private
