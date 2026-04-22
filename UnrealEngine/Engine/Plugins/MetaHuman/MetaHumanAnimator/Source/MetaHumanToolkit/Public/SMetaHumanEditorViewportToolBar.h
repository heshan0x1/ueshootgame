// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SViewportToolBar.h"
#include "SEditorViewportViewMenuContext.h"
#include "MetaHumanABCommandList.h"

#include "Widgets/SWidget.h"
#include "Widgets/Input/SSpinBox.h"

#include "SMetaHumanEditorViewportToolBar.generated.h"

#define UE_API METAHUMANTOOLKIT_API

/** A delegate that is executed when adding menu content. */
DECLARE_DELEGATE_OneParam(FOnCamSpeedChanged, int32);
DECLARE_DELEGATE_OneParam(FOnCamSpeedScalarChanged, float);
DECLARE_DELEGATE_TwoParams(FOnGetABMenuContents, enum class EABImageViewMode InABViewMode, FMenuBuilder& InMenuBuilder)

class SMetaHumanEditorViewportToolBar
	: public SViewportToolBar
{
public:
	SLATE_BEGIN_ARGS(SMetaHumanEditorViewportToolBar) {}

		SLATE_ARGUMENT(TSharedPtr<class FUICommandList>, ViewportCommandList)

		SLATE_ARGUMENT(FMetaHumanABCommandList, ABCommandList)

		SLATE_ARGUMENT(TSharedPtr<class FMetaHumanEditorViewportClient>, ViewportClient)

		SLATE_EVENT(FOnGetABMenuContents, OnGetABMenuContents)

	SLATE_END_ARGS()

	UE_API void Construct(const FArguments& InArgs);

	UE_API EVisibility GetShowAVisibility() const;
	UE_API EVisibility GetShowBVisibility() const;

private:
	UE_API TSharedRef<SWidget> CreateViewMenuWidget(enum class EABImageViewMode InViewMode);
	UE_API TSharedRef<SWidget> CreateViewMixToggleWidget();
	UE_API TSharedRef<SWidget> CreateABToggleWidget();
	UE_API TSharedRef<SWidget> CreateCameraOptionsToolbarButtonWidget();
	UE_API TSharedRef<SWidget> FillDisplayOptionsForViewMenu(enum class EABImageViewMode InViewMode);

	/** View Mix */
	void EnterSingleScreenView() {}
	void EnterMultiScreenView() {}
	void EnterSplitScreenView() {}

	bool MultiScreenViewIsChecked() const { return false; }
	bool SplitScreenViewIsChecked() const { return false; }
	bool SingleViewIsChecked() const { return false; }

	/** Field of View options */
	UE_API bool CanChangeFOV() const;
	UE_API TOptional<float> GetFOVValue() const;
	UE_API void HandleFOVValueChanged(float InNewValue);

	/** Depth data */
	UE_API bool CanChangeFootageDepthData() const;
	UE_API TOptional<float> GetFootageDepthDataNear() const;
	UE_API void HandleFootageDepthDataNearChanged(float InNewValue);
	UE_API TOptional<float> GetFootageDepthDataFar() const;
	UE_API void HandleFootageDepthDataFarChanged(float InNewValue);

private:

	static UE_API const FMargin ToolbarSlotPadding;

	TSharedPtr<class SHorizontalBox> ToolbarMenuHorizontalBox;
	TSharedPtr<class FUICommandList> ViewportCommandList;
	FMetaHumanABCommandList ABCommandList;
	TSharedPtr<class FMetaHumanEditorViewportClient> ViewportClient;

	FOnGetABMenuContents OnGetABMenuContentsDelegate;

	/** Camera speed Label callback */
	UE_API FText GetCameraSpeedLabel() const;

	/** Creates the widget to display the camera controls */
	UE_API TSharedRef<SWidget> CreateCameraOptionsDropDownMenuWidget();

	/** Reference to the camera slider used to display current camera speed */
	TSharedPtr<class SSlider> CamSpeedSlider;

	/** Reference to the camera spinbox used to display current camera speed scalar */
	mutable TSharedPtr<SSpinBox<float>> CamSpeedScalarBox;

	UE_API FText GetABToggleButtonATooltip() const;
	UE_API FText GetABToggleButtonBTooltip() const;
	UE_API FText GetABToggleButtonTooltip(FText InDefaultTooltipText) const;
};

UCLASS()
class UMetaHumanEditorViewportViewMenuContext
	: public UEditorViewportViewMenuContext
{
	GENERATED_BODY()

public:

	TWeakPtr<const class SMetaHumanViewportViewMenu> MetaHumanViewportViewMenu;
};

#undef UE_API
