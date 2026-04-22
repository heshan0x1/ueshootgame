// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ImageCore.h"
#include "Widgets/SCompoundWidget.h"

struct FSlateBrush;
class SUniformGridPanel;
class SWindow;
class UTexture2D;

DECLARE_DELEGATE_TwoParams(FOnColorTileSelected, FLinearColor Color, int32 TileIndex);
DECLARE_DELEGATE_RetVal_SixParams(FLinearColor, FOnComputeVariantColor, const FLinearColor& InColor, int32 InColorIndex, int32 InShowColumns, int32 InShowRows, float InSaturationShift, float InValueShift);

/** The available color palette picker modes. */
enum class EColorPalettePickerMode : uint8
{
	Preset,
	Custom
};

/** Widget that displays a single color palette tile. */
class SMetaHumanCharacterEditorColorPaletteTile : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMetaHumanCharacterEditorColorPaletteTile)
		: _Color(FLinearColor::Transparent)
		, _TileIndex(INDEX_NONE)
		, _UseSRGBInColorBlock(true)
		, _IsSelected(false)
		{}

		/** The color displayed by the tile. */
		SLATE_ARGUMENT(FLinearColor, Color)

		/** The index this tile represents */
		SLATE_ARGUMENT(int32, TileIndex)

		/** Whether or not to use sRGB in the color block */
		SLATE_ARGUMENT(bool, UseSRGBInColorBlock)

		/** Whether or not this tile is selected */
		SLATE_ATTRIBUTE(bool, IsSelected)

		/** Called when this tile is selected. */
		SLATE_EVENT(FOnColorTileSelected, OnTileSelected)

	SLATE_END_ARGS()

	/** Constructs the widget. */
	void Construct(const FArguments& InArgs);

	/** Gets the tile color. */
	const FLinearColor& GetColor() const { return Color; }

	/** True if this tile is selected. */
	bool IsSelected() const { return bIsSelectedAttribute.Get(); }

private:
	/** Called when this tile is clicked. */
	FReply OnTileClicked(const FGeometry& InGeometry, const FPointerEvent& InPointerEvent);

	/** Gets the tile border brush according to its state. */
	const FSlateBrush* GetTileBorderBrush() const;

	/** Gets the tile size according to its state. */
	FOptionalSize GetTileSize() const;

	// Slate Arguments
	FOnColorTileSelected OnTileSelectedDelegate;
	FLinearColor Color;
	int32 TileIndex = INDEX_NONE;

	TAttribute<bool> bIsSelectedAttribute;
};

/**
 * Widget that displays a color palette made of palette tiles. 
 * It allows selecting a color from the palette.
 */
class SMetaHumanCharacterEditorColorPalette : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMetaHumanCharacterEditorColorPalette) 
		: _PaletteDefaultColor(FLinearColor::Transparent)
		, _SelectedColor(FLinearColor::Transparent)
		, _PaletteRows(5)
		, _PaletteColumns(7)
		, _UseSRGBInColorBlock(true)
	{}
		/** The default color used for deriving the palette colors from. */
		SLATE_ATTRIBUTE(FLinearColor, PaletteDefaultColor)

		/** The selected color of the palette. */
		SLATE_ATTRIBUTE(FLinearColor, SelectedColor)

		/** The number of rows in the palette. */
		SLATE_ARGUMENT(uint32, PaletteRows)

		/** The number of columns in the palette. */
		SLATE_ARGUMENT(uint32, PaletteColumns)

		/** Whether or not to use sRGB in the color block. */
		SLATE_ARGUMENT(bool, UseSRGBInColorBlock)

		/** Called when a palette tile is selected. */
		SLATE_EVENT(FOnColorTileSelected, OnPaletteTileSelected)

		/** Called to compute the variants of the palette default color. */
		SLATE_EVENT(FOnComputeVariantColor, OnComputeVariantColor)

	SLATE_END_ARGS()

	/** Constructs the widget. */
	void Construct(const FArguments& InArgs);

private:
	/** Makes the color palette deriving the colors from the DefaultColor. */
	void MakeColorPalette();

	/** Called when the selected color has changed. */
	void OnColorSelectionChanged(FLinearColor InColor, int32 TileIndex = INDEX_NONE);

	/** The array of tiles of the palette. */
	TArray<TSharedPtr<SMetaHumanCharacterEditorColorPaletteTile>> PaletteTiles;

	/** The grid panel of the palette. */
	TSharedPtr<SUniformGridPanel> GridPanel;

	// Slate Attributes
	TAttribute<FLinearColor> DefaultColor;
	TAttribute<FLinearColor> SelectedColor;

	// Slate Arguments
	FOnColorTileSelected OnPaletteTileSelectedDelegate;
	FOnComputeVariantColor OnComputeVariantColorDelegate;
	uint32 PaletteRows;
	uint32 PaletteColumns;
	bool bUseSRGBInColorBlock;
};

/**
 * Widget that displays a color block that when clicked creates
 * a pop-up window that allows the user to select a color from
 * a multi-modal widget.
 */
class SMetaHumanCharacterEditorColorPalettePicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMetaHumanCharacterEditorColorPalettePicker)
		: _PaletteDefaultColor(FLinearColor::Transparent)
		, _SelectedColor(FLinearColor::Transparent)
		, _PaletteLabel(NSLOCTEXT("SMetaHumanCharacterEditorColorPalettePicker", "PalettePickerLabel", "Palette Picker"))
		, _PaletteColumns(7)
		, _PaletteRows(5)
		, _ColorPickerTexture{ nullptr }
		, _UseSRGBInColorBlock(true)
	{}

		/** The default color used for deriving the palette colors from. */
		SLATE_ATTRIBUTE(FLinearColor, PaletteDefaultColor)

		/** The selected color of the palette. */
		SLATE_ATTRIBUTE(FLinearColor, SelectedColor)

		/** The label of the palette picker, also used as the main window title. */
		SLATE_ATTRIBUTE(FText, PaletteLabel)

		/** The number of rows and columns to display in the preset mode*/
		SLATE_ARGUMENT(int32, PaletteColumns)
		SLATE_ARGUMENT(int32, PaletteRows)

		/** Override for the U label in the picker window. */
		SLATE_ATTRIBUTE(FText, ULabelOverride)

		/** Override for the V label in th picker window. */
		SLATE_ATTRIBUTE(FText, VLabelOverride)

		/** The texture to use in the color swatch. */
		SLATE_ARGUMENT(class UTexture2D*, ColorPickerTexture)

		/** Whether or not to use sRGB in the color block. */
		SLATE_ARGUMENT(bool, UseSRGBInColorBlock)

		/** Called when a new color is selected in the picker. */
		SLATE_EVENT(FOnColorTileSelected, OnPaletteColorSelectionChanged)

		/** Called to compute the variants of the palette default color. */
		SLATE_EVENT(FOnComputeVariantColor, OnComputeVariantColor)

	SLATE_END_ARGS()

	/** Destructor. */
	virtual ~SMetaHumanCharacterEditorColorPalettePicker() override;

	/** Constructs the widget. */
	void Construct(const FArguments& InArgs);
	
private:
	/** Generates a texture UV picker, if the ColorPickerTexture is valid. */
	TSharedRef<SWidget> GenerateCustomTextureUVPicker();

	/** Called when the color block is clicked. */
	FReply OnColorBlockClicked(const FGeometry& InGeometry, const FPointerEvent& InPointerEvent);

	/** Called when the mode of the palette picker has changed. */
	void OnPalettePickerModeChanged(EColorPalettePickerMode NewMode);

	/** Called when the selected color of the palette picker has changed. */
	void OnPaletteColorSelectionChanged(FLinearColor InColor, int32 InTileIndex = INDEX_NONE);

	/** Called when the texture's UV coordinates of the UV picker have changed. */
	void OnTextureUVChanged(const FVector2f& InUV, bool bInIsDragging);

	/** Called when the picker window has been closed. */
	void OnWindowClosed(const TSharedRef<SWindow>& InWindow);

	/** Gets the visibility of the pickers, according to the given palette picker mode. */
	EVisibility GetColorPalettePickerModeVisibility(EColorPalettePickerMode InMode) const;

	/** The current texture UV value. */
	TAttribute<FVector2f> TextureUV;

	/** The image data of the given ColorPickerTexture, if valid. */
	FImage TextureImageData;

	/** The current palette picker mode. */
	EColorPalettePickerMode Mode = EColorPalettePickerMode::Preset;

	/** The custom color picker widget */
	TSharedPtr<class SCustomColorPicker> CustomColorPicker;

	/** The color palette widget. */
	TSharedPtr<SMetaHumanCharacterEditorColorPalette> ColorPalette;

	/** The main window to display the color palette picker. */
	TSharedPtr<SWindow> Window;

	// Slate Attributes
	TAttribute<FLinearColor> DefaultColor;
	TAttribute<FLinearColor> SelectedColor;
	TAttribute<FText> Label;
	TAttribute<FText> ULabelOverride;
	TAttribute<FText> VLabelOverride;

	// Slate Arguments
	FOnColorTileSelected OnPaletteColorSelectionChangedDelegate;
	FOnComputeVariantColor OnComputeVariantColorDelegate;
	TStrongObjectPtr<UTexture2D> ColorPickerTexture;
	bool bUseSRGBInColorBlock;
	int32 NumPaletteColumns;
	int32 NumPaletteRows;
};
