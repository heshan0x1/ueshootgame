// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Widgets/AssetEditorViewport/SPCGEditorViewport.h"
#include "Widgets/Text/SRichTextBlock.h"

#include "SPVEditorViewport.generated.h"

UENUM()
enum class EPVVisualizationMode : uint8
{
	Default			UMETA(DisplayName = "Default"),
	PointData		UMETA(DisplayName = "Point data"),
	Mesh			UMETA(DisplayName = "Mesh"),
	FoliageGrid		UMETA(DisplayName = "Foliage grid"),
	Bones			UMETA(DisplayName = "Bones"),
	PointDataMesh	UMETA(DisplayName = "Point data + Mesh"),
	FoliageMesh		UMETA(DisplayName = "Foliage + Mesh"),
	BonesMesh		UMETA(DisplayName = "Bones + Mesh")
};

class UPVBaseSettings;
class UPVEditorSettings;
class UPVScaleVisualizationComponent;
struct FToolMenuEntry;
enum class EPVRenderType : uint8;

UCLASS(MinimalAPI)
class UPVMannequinWidgetContext : public UObject
{
	GENERATED_BODY()

public:
	TSharedPtr<SWidget> MannequinOffsetWidget = nullptr;
};

class SPVEditorViewport : public SPCGEditorViewport
{
public:
	SPVEditorViewport();
	virtual ~SPVEditorViewport() override;

	void Construct(const FArguments& InArgs);
	FToolMenuEntry CreateSettingsToolbarMenu();
	FToolMenuEntry CreateVisualizationModeToolbarMenu();

	void OnNodeInspectionChanged(UPVBaseSettings* InSettings);

	void SetOverlayText(const FText& CurrentlyLockedNodeName = FText::GetEmpty());
	void PopulateStatsOverlayText(const TArrayView<FText> TextItems);

protected:
	//~ Begin SEditorViewport Interface
	virtual TSharedPtr<SWidget> BuildViewportToolbar() override;
	virtual void BindCommands() override;
	//~ End SEditorViewport Interface

	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual void PopulateViewportOverlays(TSharedRef<SOverlay> Overlay) override;

private:
	void InitVisualizationScene();
	void ResetVisualizationScene();

	TSharedRef<SWidget> CreateMannequinOffsetWidget() const;

	void ToggleShowMannequin();
	bool IsShowMannequinChecked() const;
	void SetMannequinState(bool InEnable);

	void ToggleShowScaleVis();
	bool IsShowScaleVisChecked() const;
	void SetScaleVisState(bool InEnable);

	void ToggleAutofocusViewport();
	bool IsAutoFocusViewportChecked() const;

	float GetMannequinOffset() const;
	void SetMannequinOffset(float NewValue, bool bSaveConfig = false) const;

	static UPVMannequinWidgetContext* CreateMannequinWidgetContext(TSharedPtr<SWidget> InOffsetWidget);

	TArray<EPVRenderType> SupportedRenderTypes;
	EPVVisualizationMode CurrentVisualizationMode = EPVVisualizationMode::Default;
	TObjectPtr<UPVBaseSettings> InspectedNodeSettings = nullptr;

	void OnVisualizationModeChanged(EPVVisualizationMode InMode);

	bool bFocusOnNextUpdate = true;

protected:
	virtual void OnSetupScene() override;
	virtual void OnResetScene() override;

private:
	TObjectPtr<UStaticMeshComponent> MannequinComponent = nullptr;
	TObjectPtr<UPVScaleVisualizationComponent> ScaleVisualizationComponent = nullptr;

	bool bIsPreviewingLockedNode = false;
	TSharedPtr<SRichTextBlock> OverlayText;
	TSharedPtr<SRichTextBlock> StatsOverlayText;

	TSharedPtr<FSlateBrush> PreviewNodeBackgroundBrush;
};
