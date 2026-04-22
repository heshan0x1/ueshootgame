// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FMediaProfileEditor;
class SVerticalBox;
class SHorizontalBox;
class UMediaOutput;
class UMediaSource;
class UMediaProfile;

/**
 * Info panel that is displayed above the details panel, and displays a summary of a media item's configuration
 */
class SMediaObjectInfoPanel : public SCompoundWidget
{
private:
	/** Helper struct to maintain two columns of widgets, one for labels and one for value content */
	struct FTwoColumnInfo
	{
	public:
		TSharedRef<SWidget> CreateWidget();

		void AddEntry(TAttribute<FText> Label, TAttribute<FText> Value, TAttribute<FSlateColor> ValueColor = TAttribute<FSlateColor>());
		void AddEntry(TAttribute<FText> Value, const FSlateBrush* Icon);
		void AddEntry(TAttribute<FText> Value);

		void ClearEntries();

	private:
		TSharedPtr<SVerticalBox> LabelColumn;
		TSharedPtr<SVerticalBox> ValueColumn;
	};
	
public:
	SLATE_BEGIN_ARGS(SMediaObjectInfoPanel) {}
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, TWeakPtr<FMediaProfileEditor>& InMediaProfileEditor, UMediaProfile* InMediaProfile);

	/** Sets the media source or output whose information is being displayed in the panel */
	void SetMediaObject(UObject* InMediaObject);

private:
	UMediaSource* ObjectAsMediaSource() const;
	UMediaOutput* ObjectAsMediaOutput() const;
	
	void ClearInfo();
	void FillInfo();

	TSharedRef<SWidget> CreateInfoGroupWidget(const TAttribute<FText>& InHeader, FTwoColumnInfo& InInfo);

	FText GetCaptureMethodText() const;
	FText GetCaptureObjectLabelText() const;
	FText GetCaptureObjectValueText() const;
	FText GetCaptureStatusText() const;
	FSlateColor GetCaptureStatusColor() const;
	FText GetCaptureColorConversionText() const;
	
private:
	/** Media profile editor that owns this widget */
	TWeakPtr<FMediaProfileEditor> MediaProfileEditor;

	/** Media profile that is being edited */
	TWeakObjectPtr<UMediaProfile> MediaProfile;

	/** Media source or output whose info is being displayed */
	TWeakObjectPtr<UObject> MediaObject;

	/** Container for any extra columns of info */
	TSharedPtr<SHorizontalBox> MediaObjectInfoBox;

	/** Basic info column, which displays media object label and proxy status */
	FTwoColumnInfo BasicInfo;

	/** Column that displays details about the type of the media object */
	FTwoColumnInfo TypeInfo;

	/** Column that displays details about the media capture settings if the object is a media output */
	FTwoColumnInfo CaptureInfo;
};
