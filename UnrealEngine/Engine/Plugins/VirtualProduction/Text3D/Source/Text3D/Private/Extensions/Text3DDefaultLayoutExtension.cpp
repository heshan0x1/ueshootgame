// Copyright Epic Games, Inc. All Rights Reserved.

#include "Extensions/Text3DDefaultLayoutExtension.h"

#include "Algo/Accumulate.h"
#include "Algo/Count.h"
#include "Characters/Text3DCharacterBase.h"
#include "Engine/Font.h"
#include "Extensions/Text3DCharacterExtensionBase.h"
#include "Extensions/Text3DGeometryExtensionBase.h"
#include "Extensions/Text3DStyleExtensionBase.h"
#include "Framework/Text/RichTextLayoutMarshaller.h"
#include "Framework/Text/RichTextMarkupProcessing.h"
#include "LayoutBuilders/Text3DLayout.h"
#include "LayoutBuilders/Text3DLayoutShaper.h"
#include "LayoutBuilders/Text3DShapedGlyphText.h"
#include "Logs/Text3DLogs.h"
#include "Misc/EnumerateRange.h"
#include "Styling/StyleDefaults.h"
#include "Styling/SlateTypes.h"
#include "Subsystems/Text3DEngineSubsystem.h"
#include "Text3DComponent.h"

#if WITH_FREETYPE
#include "Fonts/SlateTextShaper.h"
#endif

FName UText3DDefaultLayoutExtension::GetUseMaxWidthPropertyName()
{
	return GET_MEMBER_NAME_CHECKED(UText3DDefaultLayoutExtension, bUseMaxWidth);
}

FName UText3DDefaultLayoutExtension::GetUseMaxHeightPropertyName()
{
	return GET_MEMBER_NAME_CHECKED(UText3DDefaultLayoutExtension, bUseMaxHeight);
}

FName UText3DDefaultLayoutExtension::GetMaxHeightPropertyName()
{
	return GET_MEMBER_NAME_CHECKED(UText3DDefaultLayoutExtension, MaxHeight);
}

FName UText3DDefaultLayoutExtension::GetMaxWidthPropertyName()
{
	return GET_MEMBER_NAME_CHECKED(UText3DDefaultLayoutExtension, MaxWidth);
}

FName UText3DDefaultLayoutExtension::GetScaleProportionallyPropertyName()
{
	return GET_MEMBER_NAME_CHECKED(UText3DDefaultLayoutExtension, bScaleProportionally);
}

void UText3DDefaultLayoutExtension::SetTracking(const float Value)
{
	if (FMath::IsNearlyEqual(Tracking, Value))
	{
		return;
	}
	
	Tracking = Value;
	OnLayoutOptionsChanged();
}

void UText3DDefaultLayoutExtension::SetLineSpacing(const float Value)
{
	if (FMath::IsNearlyEqual(LineSpacing, Value))
	{
		return;
	}
	
	LineSpacing = Value;
	OnLayoutOptionsChanged();
}

void UText3DDefaultLayoutExtension::SetWordSpacing(const float Value)
{
	if (FMath::IsNearlyEqual(WordSpacing, Value))
	{
		return;
	}

	WordSpacing = Value;
	OnLayoutOptionsChanged();
}

void UText3DDefaultLayoutExtension::SetHorizontalAlignment(const EText3DHorizontalTextAlignment Value)
{
	if (HorizontalAlignment == Value)
	{
		return;
	}

	HorizontalAlignment = Value;
	OnLayoutOptionsChanged();
}

void UText3DDefaultLayoutExtension::SetVerticalAlignment(const EText3DVerticalTextAlignment Value)
{
	if (VerticalAlignment == Value)
	{
		return;
	}
	
	VerticalAlignment = Value;
	OnLayoutOptionsChanged();
}

void UText3DDefaultLayoutExtension::SetUseMaxWidth(const bool Value)
{
	if (bUseMaxWidth == Value)
	{
		return;
	}

	bUseMaxWidth = Value;
	OnLayoutOptionsChanged();
}

void UText3DDefaultLayoutExtension::SetMaxWidth(const float Value)
{
	const float NewValue = FMath::Max(1.0f, Value);
	if (FMath::IsNearlyEqual(MaxWidth, NewValue))
	{
		return;
	}
	
	MaxWidth = NewValue;
	OnLayoutOptionsChanged();
}

void UText3DDefaultLayoutExtension::SetMaxWidthBehavior(const EText3DMaxWidthHandling Value)
{
	if (MaxWidthBehavior == Value)
	{
		return;
	}

	MaxWidthBehavior = Value;
	OnLayoutOptionsChanged();
}

void UText3DDefaultLayoutExtension::SetUseMaxHeight(const bool Value)
{
	if (bUseMaxHeight == Value)
	{
		return;
	}
	
	bUseMaxHeight = Value;
	OnLayoutOptionsChanged();
}

void UText3DDefaultLayoutExtension::SetMaxHeight(const float Value)
{
	const float NewValue = FMath::Max(1.0f, Value);
	if (FMath::IsNearlyEqual(MaxHeight, NewValue))
	{
		return;
	}

	MaxHeight = NewValue;
	OnLayoutOptionsChanged();
}

void UText3DDefaultLayoutExtension::SetScaleProportionally(const bool Value)
{
	if (bScaleProportionally == Value)
	{
		return;
	}

	bScaleProportionally = Value;
	OnLayoutOptionsChanged();
}

float UText3DDefaultLayoutExtension::GetTextHeight() const
{
	using namespace UE::Text3D::Layout;

	float Height = FMath::Max(GlyphText->Lines.Num() - 1, 0) * LineSpacing;

	for (const FGlyphLine& Line : GlyphText->Lines)
	{
		Height += Line.MaxFontHeight;
	}

	return Height;
}

FVector UText3DDefaultLayoutExtension::GetTextScale() const
{
	return TextScale;
}

EText3DExtensionResult UText3DDefaultLayoutExtension::PreRendererUpdate(const UE::Text3D::Renderer::FUpdateParameters& InParameters)
{
	using namespace UE::Text3D::Layout;

	if (InParameters.CurrentFlag != EText3DRendererFlags::Geometry && InParameters.CurrentFlag != EText3DRendererFlags::Layout)
	{
		return EText3DExtensionResult::Active;
	}

	const UText3DComponent* Text3DComponent = GetText3DComponent();
	
	if (!GlyphText.IsValid())
	{
		GlyphText = MakeShared<FGlyphText>();
	}

	if (!TextLayout.IsValid())
	{
		TextLayout = MakeShared<FText3DLayout>();
	}

	if (!TextLayoutMarshaller.IsValid())
	{
		const TSharedPtr<IRichTextMarkupParser> Parser = FDefaultRichTextMarkupParser::GetStaticInstance();
		TextLayoutMarshaller = FRichTextLayoutMarshaller::Create(Parser, nullptr, TArray<TSharedRef<ITextDecorator>>(), nullptr);
	}

	GlyphText->Reset();

	const UText3DStyleExtensionBase* FormatStyleExtension = Text3DComponent->GetStyleExtension();
	FText3DLayoutShaper::Get()->ShapeBidirectionalText(FormatStyleExtension->GetCustomStyles(), *FormatStyleExtension->GetDefaultStyle(), Text3DComponent->GetFormattedText().ToString(), TextLayout, TextLayoutMarshaller, GlyphText->Lines);

	const int32 CharacterCount = Algo::TransformAccumulate(GlyphText->Lines, [](const FGlyphLine& InLine)
	{
		return Algo::CountIf(InLine.Glyphs, [](const FGlyphEntry& InGlyph)
		{
			return FGlyphText::IsValidGlyph(InGlyph);
		});
	},0);

	if (EnumHasAnyFlags(InParameters.UpdateFlags, EText3DRendererFlags::Geometry))
	{
		if (UText3DCharacterExtensionBase* CharacterExtension = Text3DComponent->GetCharacterExtension())
		{
			CharacterExtension->AllocateCharacters(CharacterCount);
		}
		else
		{
			UE_LOG(LogText3D, Warning, TEXT("Text3D character extension is invalid, cannot proceed without it"))
			return EText3DExtensionResult::Failed;
		}
	}

	GlyphText->Kernings.SetNum(CharacterCount);
	GlyphText->FontFaces.SetNum(CharacterCount);

	const UText3DGeometryExtensionBase* GeometryExtension = Text3DComponent->GetGeometryExtension();
	uint16 CharacterIndex = 0;

#if WITH_FREETYPE
	for (const TConstEnumerateRef<FGlyphLine> GlyphLine : EnumerateRange(GlyphText->Lines))
	{
		for (const TConstEnumerateRef<FGlyphEntry> GlyphEntry : EnumerateRange(GlyphLine->Glyphs))
		{
			if (FGlyphText::IsValidGlyph(*GlyphEntry))
			{
				const TSharedPtr<FFreeTypeFace> FontFacePtr = GlyphEntry->Entry.FontFaceData->FontFace.Pin();
				if (const UText3DCharacterBase* Character = Text3DComponent->GetCharacter(CharacterIndex))
				{
					GlyphText->Kernings[CharacterIndex] = Character->GetCharacterKerning();
                    GlyphText->FontFaces[CharacterIndex] = FontFacePtr;
                    CharacterIndex++;
				}
				else
				{
					UE_LOG(LogText3D, Warning, TEXT("Invalid character object returned for index %i before layout calculations, cannot proceed"), CharacterIndex)
					return EText3DExtensionResult::Failed;
				}
			}
		}
	}
#else
	UE_LOG(LogText3D, Warning, TEXT("FreeType is not available, cannot proceed without it"))
	return EText3DExtensionResult::Failed;
#endif

	GlyphText->Tracking = Tracking;
	GlyphText->WordSpacing = WordSpacing;
	GlyphText->MaxWidth = MaxWidth;
	GlyphText->bWrap = bUseMaxWidth && MaxWidthBehavior == EText3DMaxWidthHandling::WrapAndScale;
	GlyphText->CalculateWidth();
	CalculateTextScale();

	CharacterIndex = 0;
	UText3DEngineSubsystem* TextSubsystem = UText3DEngineSubsystem::Get();
	for (TConstEnumerateRef<FGlyphLine> GlyphLine : EnumerateRange(GlyphText->Lines))
	{
		FVector Location = GetLineLocation(GlyphLine.GetIndex());
		for (TConstEnumerateRef<FGlyphEntry> GlyphEntry : EnumerateRange(GlyphLine->Glyphs))
		{
			if (FGlyphText::IsValidGlyph(*GlyphEntry))
			{
				UText3DCharacterBase* Character = Text3DComponent->GetCharacter(CharacterIndex);

				if (!Character)
				{
					UE_LOG(LogText3D, Warning, TEXT("Invalid character object returned for index %i after layout calculations, cannot proceed"), CharacterIndex)
					return EText3DExtensionResult::Failed;
				}

				if (EnumHasAnyFlags(InParameters.UpdateFlags, EText3DRendererFlags::Geometry))
				{
#if WITH_FREETYPE
					TSharedPtr<FFreeTypeFace> CharTypeface = GlyphText->FontFaces[CharacterIndex];
					const uint32 FontFaceHash = FText3DFontFaceCache::GetFontFaceHash(CharTypeface);
					// Cache font face to keep it alive and use inner FT_Face to build glyphs
					FText3DFontFaceCache* FontFaceCache = TextSubsystem->FindOrAddCachedFontFace(CharTypeface);
#else
					uint32 FontFaceHash = 0;
					FText3DFontFaceCache* FontFaceCache = nullptr;
#endif

					if (FontFaceCache)
					{
						// Load glyph here to get proper mesh metrics
						UE::Text3D::Geometry::FCachedFontFaceGlyphHandle GlyphHandle(FontFaceCache, GlyphEntry->Entry.GlyphIndex, *GeometryExtension->GetGlyphMeshParameters());
						Character->SetFontFaceGlyphHandle(GlyphHandle);
						Character->SetStyleTag(GlyphEntry->StyleTag);
					}
					else
					{
						UE_LOG(LogText3D, Warning, TEXT("Failed to get cached font data for '%u %i' in Text3D geometry extension"), FontFaceHash, GlyphEntry->Entry.GlyphIndex);
						return EText3DExtensionResult::Failed;
					}
				}

				const FText3DCachedMesh* CachedMesh = Character->GetGlyphMesh();

				if (!CachedMesh)
				{
					UE_LOG(LogText3D, Warning, TEXT("Invalid character cached mesh returned for index %i and glyph %i, cannot proceed"), CharacterIndex, GlyphEntry->Entry.GlyphIndex)
					return EText3DExtensionResult::Failed;
				}

				const FBox GlyphBounds = CachedMesh->MeshBounds;
				const FVector GlyphSize = GlyphBounds.GetSize();

				FVector Shift = FVector::ZeroVector;
				switch (GeometryExtension->GetGlyphHAlignment())
				{
				case EText3DHorizontalTextAlignment::Left:
					Shift = FVector::ZeroVector;
					break;
				case EText3DHorizontalTextAlignment::Center:
					Shift.Y = (GlyphSize.Y / 2);
					break;
				case EText3DHorizontalTextAlignment::Right:
					Shift.Y = GlyphSize.Y;
					break;
				}

				if (GlyphEntry.GetIndex() != 0)
				{
					Shift.Y += Tracking + GlyphText->Kernings[CharacterIndex];
				}

				// Handle special characters with no advance but offset
				if (GlyphEntry->Entry.XAdvance == 0)
				{
					Shift.Y += GlyphEntry->Entry.XOffset;
					Shift.Z -= GlyphEntry->Entry.YOffset;
				}

				FTransform Transform(Location + Shift);
				const FVector2D FaceSize = CachedMesh->FontFaceGlyphSize;

				if (FaceSize.IsNearlyZero())
				{
					UE_LOG(LogText3D, Warning, TEXT("Font face glyph size is nearly zero, cannot scale mesh properly to match font size"))
				}
				else
				{
					const FVector Ratio(1.f, GlyphEntry->Size / FaceSize.X, GlyphEntry->Size / FaceSize.Y);
					Transform.SetScale3D(Ratio);
				}

				constexpr bool bReset = true;
				FTransform& CharacterTransform = Character->GetTransform(bReset);
				CharacterTransform.Accumulate(Transform);
				CharacterIndex++;
			}

			Location.Y += GlyphLine->GlyphAdvances[GlyphEntry.GetIndex()];
		}
	}

	return EText3DExtensionResult::Finished;
}

EText3DExtensionResult UText3DDefaultLayoutExtension::PostRendererUpdate(const UE::Text3D::Renderer::FUpdateParameters& InParameters)
{
	return EText3DExtensionResult::Active;
}

void UText3DDefaultLayoutExtension::CalculateTextScale()
{
	using namespace UE::Text3D::Layout;

	FVector Scale(1.0f, 1.0f, 1.0f);

	float TextMaxWidth = 0.0f;
	for (const FGlyphLine& ShapedLine : GlyphText->Lines)
	{
		TextMaxWidth = FMath::Max(TextMaxWidth, ShapedLine.Width);
	}

	if (bUseMaxWidth && TextMaxWidth > MaxWidth && TextMaxWidth > 0.0f)
	{
		Scale.Y *= MaxWidth / TextMaxWidth;
		if (bScaleProportionally)
		{
			Scale.Z = Scale.Y;
		}
	}

	const float TotalHeight = GetTextHeight();
	if (bUseMaxHeight && TotalHeight > MaxHeight && TotalHeight > 0.0f)
	{
		Scale.Z *= MaxHeight / TotalHeight;
		if (bScaleProportionally)
		{
			Scale.Y = Scale.Z;
		}
	}

	if (bScaleProportionally)
	{
		Scale.X = Scale.Y;
	}

	TextScale = Scale;
}

FVector UText3DDefaultLayoutExtension::GetLineLocation(int32 LineIndex) const
{
	using namespace UE::Text3D::Layout;

	float HorizontalOffset = 0.0f;
	float VerticalOffset = 0.0f;

	if (LineIndex < 0 || LineIndex >= GlyphText->Lines.Num())
	{
		return FVector::ZeroVector;
	}

	const FGlyphLine& ShapedLine = GlyphText->Lines[LineIndex];

	if (HorizontalAlignment == EText3DHorizontalTextAlignment::Center)
	{
		HorizontalOffset = -ShapedLine.Width * 0.5f;
	}
	else if (HorizontalAlignment == EText3DHorizontalTextAlignment::Right)
	{
		HorizontalOffset = -ShapedLine.Width;
	}

	const float TotalHeight = GetTextHeight();
	if (VerticalAlignment != EText3DVerticalTextAlignment::FirstLine)
	{
		// First align it to Top
		VerticalOffset -= ShapedLine.MaxFontAscender;

		if (VerticalAlignment == EText3DVerticalTextAlignment::Center)
		{
			VerticalOffset += TotalHeight * 0.5f;
		}
		else if (VerticalAlignment == EText3DVerticalTextAlignment::Bottom)
		{
			VerticalOffset += TotalHeight - ShapedLine.MaxFontDescender;
		}
	}

	// Offset line based on cumulative height above it
	for (int32 Index = 0; Index < LineIndex; ++Index)
	{
		const FGlyphLine& PrevLine = GlyphText->Lines[Index];
		VerticalOffset -= (PrevLine.MaxFontHeight + LineSpacing);
	}

	return FVector(0.0f, HorizontalOffset, VerticalOffset);
}

#if WITH_EDITOR
void UText3DDefaultLayoutExtension::PostEditUndo()
{
	Super::PostEditUndo();
	OnLayoutOptionsChanged();
}

void UText3DDefaultLayoutExtension::PostEditChangeProperty(FPropertyChangedEvent& InEvent)
{
	Super::PostEditChangeProperty(InEvent);

	static const TSet<FName> LayoutPropertyNames
	{
		GET_MEMBER_NAME_CHECKED(UText3DDefaultLayoutExtension, Tracking),
		GET_MEMBER_NAME_CHECKED(UText3DDefaultLayoutExtension, LineSpacing),
		GET_MEMBER_NAME_CHECKED(UText3DDefaultLayoutExtension, WordSpacing),
		GET_MEMBER_NAME_CHECKED(UText3DDefaultLayoutExtension, HorizontalAlignment),
		GET_MEMBER_NAME_CHECKED(UText3DDefaultLayoutExtension, VerticalAlignment),
		GET_MEMBER_NAME_CHECKED(UText3DDefaultLayoutExtension, bUseMaxWidth),
		GET_MEMBER_NAME_CHECKED(UText3DDefaultLayoutExtension, MaxWidth),
		GET_MEMBER_NAME_CHECKED(UText3DDefaultLayoutExtension, bUseMaxHeight),
		GET_MEMBER_NAME_CHECKED(UText3DDefaultLayoutExtension, MaxHeight),
		GET_MEMBER_NAME_CHECKED(UText3DDefaultLayoutExtension, bScaleProportionally),
	};

	if (LayoutPropertyNames.Contains(InEvent.GetMemberPropertyName()))
	{
		OnLayoutOptionsChanged();
	}
}
#endif

void UText3DDefaultLayoutExtension::OnLayoutOptionsChanged()
{
	using namespace UE::Text3D::Layout;

	MaxWidth = FMath::Max(1.0f, MaxWidth);
	MaxHeight = FMath::Max(1.0f, MaxHeight);

	EText3DRendererFlags Flags = EText3DRendererFlags::Material;
	EnumAddFlags(Flags, EText3DRendererFlags::Layout);

	if (MaxWidthBehavior == EText3DMaxWidthHandling::WrapAndScale && GlyphText.IsValid())
	{
		for (const TConstEnumerateRef<FGlyphLine> ShapedLine : EnumerateRange(GlyphText->Lines))
		{
			if (ShapedLine->TextDirection == TextBiDi::ETextDirection::RightToLeft)
			{
				EnumAddFlags(Flags, EText3DRendererFlags::Geometry);
				break;
			}
		}
	}

	RequestUpdate(Flags);
}
