// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorColorPalettePicker.h"

#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "MetaHumanCharacterEditorStyle.h"
#include "Misc/Optional.h"
#include "UI/Widgets/SUVColorPicker.h"
#include "UObject/ObjectKey.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorColorPalettePicker"

namespace UE::MetaHuman::Private
{
	static constexpr float DefaultTileSize = 30.f;
	static constexpr float HoveredTileSize = 32.f;
}

/**
 * A custom colour picker that allows its current colour to be updated externally
 */
class SCustomColorPicker : public SColorPicker
{
public:
	void UpdateColor(const FLinearColor& InNewColor)
	{
		SetNewTargetColorRGB(InNewColor);
	}
};

void SMetaHumanCharacterEditorColorPaletteTile::Construct(const FArguments& InArgs)
{
	Color = InArgs._Color;
	TileIndex = InArgs._TileIndex;

	bIsSelectedAttribute = InArgs._IsSelected;

	OnTileSelectedDelegate = InArgs._OnTileSelected;

	ChildSlot
		[
			SNew(SBorder)
			.Padding(FMargin(1.5f))
			.BorderImage(this, &SMetaHumanCharacterEditorColorPaletteTile::GetTileBorderBrush)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(this, &SMetaHumanCharacterEditorColorPaletteTile::GetTileSize)
				.HeightOverride(this, &SMetaHumanCharacterEditorColorPaletteTile::GetTileSize)
				[
					SNew(SColorBlock)
					.Color(Color)
					.UseSRGB(InArgs._UseSRGBInColorBlock)
					.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
					.AlphaBackgroundBrush(FAppStyle::Get().GetBrush("ColorPicker.RoundedAlphaBackground"))
					.ShowBackgroundForAlpha(true)
					.CornerRadius(FVector4(4.f, 4.f, 4.f, 4.f))
					.OnMouseButtonDown(this, &SMetaHumanCharacterEditorColorPaletteTile::OnTileClicked)
				]
			]
		];
}

FReply SMetaHumanCharacterEditorColorPaletteTile::OnTileClicked(const FGeometry& InGeometry, const FPointerEvent& InPointerEvent)
{
	OnTileSelectedDelegate.ExecuteIfBound(Color, TileIndex);
	return FReply::Handled();
}

const FSlateBrush* SMetaHumanCharacterEditorColorPaletteTile::GetTileBorderBrush() const
{
	if (IsSelected())
	{
		return FMetaHumanCharacterEditorStyle::Get().GetBrush("MetaHumanCharacterEditorTools.Rounded.SelectedBrush");
	}
	else
	{
		if (IsHovered())
		{
			return FMetaHumanCharacterEditorStyle::Get().GetBrush("MetaHumanCharacterEditorTools.Rounded.WhiteBrush");
		}
		else
		{
			return FMetaHumanCharacterEditorStyle::Get().GetBrush("MetaHumanCharacterEditorTools.Rounded.DefaultBrush");
		}
	}
}

FOptionalSize SMetaHumanCharacterEditorColorPaletteTile::GetTileSize() const
{
	using namespace UE::MetaHuman::Private;
	return IsHovered() ? FOptionalSize(HoveredTileSize) : FOptionalSize(DefaultTileSize);
}

void SMetaHumanCharacterEditorColorPalette::Construct(const FArguments& InArgs)
{
	DefaultColor = InArgs._PaletteDefaultColor;
	SelectedColor = InArgs._SelectedColor.IsSet() ? InArgs._SelectedColor : DefaultColor.Get();
	PaletteRows = InArgs._PaletteRows;
	PaletteColumns = InArgs._PaletteColumns;
	bUseSRGBInColorBlock = InArgs._UseSRGBInColorBlock;

	OnPaletteTileSelectedDelegate = InArgs._OnPaletteTileSelected;
	OnComputeVariantColorDelegate = InArgs._OnComputeVariantColor;

	ChildSlot
		[
			SAssignNew(GridPanel, SUniformGridPanel)
			.MinDesiredSlotHeight(40.f)
			.MinDesiredSlotWidth(40.f)
			.SlotPadding(4.f)
		];

	MakeColorPalette();
}

void SMetaHumanCharacterEditorColorPalette::MakeColorPalette()
{
	if (PaletteRows == 0  || PaletteColumns == 0 || !GridPanel.IsValid())
	{
		return;
	}

	for (uint32 Y = 0; Y < PaletteRows; Y++)
	{
		for (uint32 X = 0; X < PaletteColumns; X++)
		{
			const uint32 TileIndex = Y * PaletteColumns + X;
			FLinearColor TileColor = DefaultColor.Get();
			if (OnComputeVariantColorDelegate.IsBound())
			{
				TileColor = OnComputeVariantColorDelegate.Execute(DefaultColor.Get(), TileIndex, PaletteColumns, PaletteRows, .1f, .1f);
			}

			const TSharedRef<SMetaHumanCharacterEditorColorPaletteTile> Tile =
				SNew(SMetaHumanCharacterEditorColorPaletteTile)
				.Color(TileColor)
				.TileIndex(TileIndex)
				.IsSelected_Lambda([this, TileColor]
								   {
									   return SelectedColor.Get() == TileColor;
								   })
				.UseSRGBInColorBlock(bUseSRGBInColorBlock)
				.OnTileSelected(this, &SMetaHumanCharacterEditorColorPalette::OnColorSelectionChanged);

			GridPanel->AddSlot(X, Y)
				[
					SNew(SBox)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.WidthOverride(36.f)
					.HeightOverride(36.f)
					[
						Tile
					]
				];

			PaletteTiles.Add(Tile);
		}	
	}
}

void SMetaHumanCharacterEditorColorPalette::OnColorSelectionChanged(FLinearColor InColor, int32 TileIndex)
{
	OnPaletteTileSelectedDelegate.ExecuteIfBound(InColor, TileIndex);
}

SMetaHumanCharacterEditorColorPalettePicker::~SMetaHumanCharacterEditorColorPalettePicker()
{
	if (Window.IsValid())
	{
		Window->RequestDestroyWindow();
	}
}

void SMetaHumanCharacterEditorColorPalettePicker::Construct(const FArguments& InArgs)
{
	DefaultColor = InArgs._PaletteDefaultColor;
	SelectedColor = InArgs._SelectedColor.IsSet() ? InArgs._SelectedColor : DefaultColor.Get();

	NumPaletteColumns = InArgs._PaletteColumns;
	NumPaletteRows = InArgs._PaletteRows;

	Label = InArgs._PaletteLabel;
	ULabelOverride = InArgs._ULabelOverride;
	VLabelOverride = InArgs._VLabelOverride;
	bUseSRGBInColorBlock = InArgs._UseSRGBInColorBlock;

	OnPaletteColorSelectionChangedDelegate = InArgs._OnPaletteColorSelectionChanged;
	OnComputeVariantColorDelegate = InArgs._OnComputeVariantColor;

	TextureUV = FVector2f(.5f, .5f);
	ColorPickerTexture = TStrongObjectPtr<UTexture2D>{ InArgs._ColorPickerTexture };
	if (ColorPickerTexture.IsValid())
	{
		const bool bImageRead = FImageUtils::GetTexture2DSourceImage(ColorPickerTexture.Get(), TextureImageData);
		check(bImageRead);
	}

	ChildSlot
	[
		SNew(SColorBlock)
		.Color(SelectedColor)
		.UseSRGB(InArgs._UseSRGBInColorBlock)
		.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
		.AlphaBackgroundBrush(FAppStyle::Get().GetBrush("ColorPicker.RoundedAlphaBackground"))
		.ShowBackgroundForAlpha(true)
		.CornerRadius(FVector4(2.f, 2.f, 2.f, 2.f))
		.OnMouseButtonDown(this, &SMetaHumanCharacterEditorColorPalettePicker::OnColorBlockClicked)
	];
}

TSharedRef<SWidget> SMetaHumanCharacterEditorColorPalettePicker::GenerateCustomTextureUVPicker()
{
	TSharedPtr<SWidget> CustomPickerWidget = SNullWidget::NullWidget;
	if (ColorPickerTexture.IsValid())
	{
		SAssignNew(CustomPickerWidget, SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
		.Padding(FMargin{ 16.0, 16.0 })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SUVColorSwatch)
				.UV_Lambda([this]() {return TextureUV.Get(); })
				.ColorPickerTexture(ColorPickerTexture.Get())
				.OnUVChanged(this, &SMetaHumanCharacterEditorColorPalettePicker::OnTextureUVChanged)
			]

			// U Slider and Label
			+SVerticalBox::Slot()
			.Padding(1.0f, 5.0f)
			[
				SNew(SHorizontalBox)

				// U Label Section
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.FillWidth(0.2f)
				.Padding(10.0f, 0.0f)
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text(ULabelOverride)
				]

				// U Slider Section
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				.FillWidth(0.8f)
				.Padding(4.0f, 2.0f, 80.0f, 2.0f)
				[
					SNew(SNumericEntryBox<float>)
					.AllowSpin(true)
					.EditableTextBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.SpinBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox"))
					.MinValue(0.0f)
					.MaxValue(1.0f)
					.MinSliderValue(0.0f)
					.MaxSliderValue(1.0f)
					.PreventThrottling(true)
					.MaxFractionalDigits(2)
					.LinearDeltaSensitivity(1.0)
					.Value_Lambda([this] { return TextureUV.Get().X; })
					.OnValueChanged_Lambda([this](float NewUValue)
						{
							const bool bIsInteractive = true;
							OnTextureUVChanged(FVector2f(NewUValue, TextureUV.Get().Y), bIsInteractive);
						})
					.OnValueCommitted_Lambda([this](float NewUValue, ETextCommit::Type InType)
						{
							const bool bIsInteractive = false;
							OnTextureUVChanged(FVector2f(NewUValue, TextureUV.Get().Y), bIsInteractive);
						})
					]
				]

				// V Label and Slider
				+SVerticalBox::Slot()
				.Padding(1.0f, 5.0f)
				[
					SNew(SHorizontalBox)

					// V Label Section
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Center)
					.FillWidth(0.2f)
					.Padding(10.0f, 0.0f)
					[
						SNew(STextBlock)
						.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						.Text(VLabelOverride)
					]

					// V Slider Section
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Center)
					.FillWidth(0.8f)
					.Padding(4.0f, 2.0f, 80.0f, 2.0f)
					[
						SNew(SNumericEntryBox<float>)
						.AllowSpin(true)
						.EditableTextBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
						.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						.SpinBoxStyle(&FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox"))
						.MinValue(0.0f)
						.MaxValue(1.0f)
						.MinSliderValue(0.0f)
						.MaxSliderValue(1.0f)
						.PreventThrottling(true)
						.MaxFractionalDigits(2)
						.LinearDeltaSensitivity(1.0)
						.Value_Lambda([this] {return TextureUV.Get().Y; })
						.OnValueChanged_Lambda([this](float NewVValue)
							{
								const bool bIsInteractive = true;
								OnTextureUVChanged(FVector2f{ TextureUV.Get().X, NewVValue }, bIsInteractive);
							})
						.OnValueCommitted_Lambda([this](float NewVValue, ETextCommit::Type InType)
							{
								const bool bIsInteractive = false;
								OnTextureUVChanged(FVector2f{ TextureUV.Get().X, NewVValue }, bIsInteractive);
							})
					]
				]
			];
	}

	return CustomPickerWidget.ToSharedRef();
}

FReply SMetaHumanCharacterEditorColorPalettePicker::OnColorBlockClicked(const FGeometry& InGeometry, const FPointerEvent& InPointerEvent)
{
	if (Window.IsValid())
	{
		return FReply::Handled();
	}

	// Determine the position of the window so that it will spawn near the mouse, but not go off the screen.
	const FVector2D CursorPos = FSlateApplication::Get().GetCursorPos();
	const FSlateRect Anchor(CursorPos.X, CursorPos.Y, CursorPos.X, CursorPos.Y);

	const FVector2D DefaultWindowSize{ 450.f, 250.f };
	const bool bAutoAdjustForDPIScale = true;
	const FVector2D ProposedPlacement = FVector2D::ZeroVector;
	const FVector2D AdjustedSummonLocation = FSlateApplication::Get().CalculatePopupWindowPosition(Anchor, DefaultWindowSize, bAutoAdjustForDPIScale, ProposedPlacement, Orient_Horizontal);

	SAssignNew(Window, SWindow)
		.AutoCenter(EAutoCenter::None)
		.ScreenPosition(AdjustedSummonLocation)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.MinWidth(450.f)
		.MinHeight(250.f)
		.SizingRule(ESizingRule::Autosized)
		.ClientSize(DefaultWindowSize)
		.Title(Label)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
			.Padding(FMargin{ 8.f, 8.f })
			[
				SNew(SVerticalBox)

				// Toolbar section
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					+SHorizontalBox::Slot()
					.MinWidth(450.f)
					.AutoWidth()
					[
						SNew(SSegmentedControl<EColorPalettePickerMode>)
						.UniformPadding(FMargin{ 4.f, 8.f })
						.Value_Lambda([this]() { return Mode; })
						.OnValueChanged(this, &SMetaHumanCharacterEditorColorPalettePicker::OnPalettePickerModeChanged)
						+SSegmentedControl<EColorPalettePickerMode>::Slot(EColorPalettePickerMode::Preset)
						.Text(LOCTEXT("MetaHumanCharacterColorPalettePicker_PresetLabel", "PRESET"))
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						+SSegmentedControl<EColorPalettePickerMode>::Slot(EColorPalettePickerMode::Custom)
						.Text(LOCTEXT("MetaHumanCharacterColorPalettePicker_CustomLabel", "CUSTOM"))
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
					]
				]

				// Palette section
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8.f)
				[
					SNew(SHorizontalBox)

					// Preset Palette section
					+SHorizontalBox::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
						SNew(SBox)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Visibility(this, &SMetaHumanCharacterEditorColorPalettePicker::GetColorPalettePickerModeVisibility, EColorPalettePickerMode::Preset)
						[
							SAssignNew(ColorPalette,SMetaHumanCharacterEditorColorPalette)
							.PaletteDefaultColor(DefaultColor)
							.PaletteColumns(NumPaletteColumns)
							.PaletteRows(NumPaletteRows)
							.SelectedColor(SelectedColor)
							.UseSRGBInColorBlock(bUseSRGBInColorBlock)
							.OnPaletteTileSelected(this, &SMetaHumanCharacterEditorColorPalettePicker::OnPaletteColorSelectionChanged)
							.OnComputeVariantColor(OnComputeVariantColorDelegate)
						]
					]

					// Custom Palette section
					+SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SBox)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Visibility(this, &SMetaHumanCharacterEditorColorPalettePicker::GetColorPalettePickerModeVisibility, EColorPalettePickerMode::Custom)
						[
							SNew(SVerticalBox)

							// Color picker section
							+SVerticalBox::Slot()
							.AutoHeight()
							.Padding(8.f)
							[
								SAssignNew(CustomColorPicker, SCustomColorPicker)
								.TargetColorAttribute(SelectedColor)
								.UseAlpha(true)
								.ExpandAdvancedSection(false)
								.OnlyRefreshOnMouseUp(false)
								.OnlyRefreshOnOk(false)
								.ParentWindow(Window)
								.sRGBOverride(bUseSRGBInColorBlock)
								.OnColorPickerWindowClosed(this, &SMetaHumanCharacterEditorColorPalettePicker::OnWindowClosed)
								.OnColorCommitted_Lambda([this](FLinearColor NewColor)
														 {
															 OnPaletteColorSelectionChanged(NewColor, INDEX_NONE);
														 })
							]

							// Custom Texture section
							+ SVerticalBox::Slot()
							.AutoHeight()
							[
								GenerateCustomTextureUVPicker()
							]
						]
					]
				]
			]
		];

	Window->SetOnWindowClosed(FOnWindowClosed::CreateSP(this, &SMetaHumanCharacterEditorColorPalettePicker::OnWindowClosed));

	// Find the window of the parent widget
	FWidgetPath WidgetPath;
	FSlateApplication::Get().GeneratePathToWidgetChecked(SharedThis(this), WidgetPath);
	Window = FSlateApplication::Get().AddWindowAsNativeChild(Window.ToSharedRef(), WidgetPath.GetWindow());

	return FReply::Handled();
}

void SMetaHumanCharacterEditorColorPalettePicker::OnPalettePickerModeChanged(EColorPalettePickerMode NewMode)
{
	Mode = NewMode;

	if (ColorPalette.IsValid() && Mode == EColorPalettePickerMode::Custom)
	{
		if (CustomColorPicker.IsValid())
		{
			// Update the color of the custom color picker to match
			CustomColorPicker->UpdateColor(SelectedColor.Get());
		}
	}
}

void SMetaHumanCharacterEditorColorPalettePicker::OnPaletteColorSelectionChanged(FLinearColor InColor, int32 TileIndex)
{
	OnPaletteColorSelectionChangedDelegate.ExecuteIfBound(InColor, TileIndex);
}

void SMetaHumanCharacterEditorColorPalettePicker::OnTextureUVChanged(const FVector2f& InUV, bool bInIsDragging)
{
	const float U = FMath::Clamp(InUV.X, 0.f, 1.f);
	const float V = FMath::Clamp(InUV.Y, 0.f, 1.f);
	TextureUV = FVector2f(U, V);

	const int32 Width = TextureImageData.GetWidth();
	const int32 Height = TextureImageData.GetHeight();
	if (Width <= 0 || Height <= 0)
	{
		return;
	}

	const int32 X = FMath::Min(FMath::FloorToInt(U * Width), Width - 1);
	const int32 Y = FMath::Min(FMath::FloorToInt(V * Height), Height - 1);

	const TConstArrayView<FColor> Pixels = TextureImageData.AsBGRA8();
	const int32 Index = Y * Width + X;
	const FLinearColor NewSelectedColor = Pixels[Index];
	OnPaletteColorSelectionChanged(NewSelectedColor);
}

void SMetaHumanCharacterEditorColorPalettePicker::OnWindowClosed(const TSharedRef<SWindow>& InWindow)
{
	Window = nullptr;
}

EVisibility SMetaHumanCharacterEditorColorPalettePicker::GetColorPalettePickerModeVisibility(EColorPalettePickerMode InMode) const
{
	const bool bIsVisible = Mode == InMode;
	return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE