// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaveformTransformationTrimFadeRenderer.h"

#include "AudioWidgetsStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/Geometry.h"
#include "PropertyHandle.h"
#include "Rendering/DrawElements.h"
#include "Sound/SoundWave.h"
#include "WaveformTransformationLog.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"
#include <Blueprint/WidgetLayoutLibrary.h>

FWaveformTransformationTrimFadeRenderer::~FWaveformTransformationTrimFadeRenderer()
{
	FSlateApplication::Get().OnFocusChanging().Remove(PopupHandle);
	FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ApplicationActivationStateHandle);
}

int32 FWaveformTransformationTrimFadeRenderer::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = DrawTrimHandles(AllottedGeometry, OutDrawElements, LayerId);
	LayerId = DrawFadeCurves(AllottedGeometry, OutDrawElements, LayerId);

	return LayerId;
}

int32 FWaveformTransformationTrimFadeRenderer::DrawTrimHandles(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const bool bRenderLowerBound = StartTimeHandleX >= 0.f;
	const bool bRenderUpperBound = EndTimeHandleX <= AllottedGeometry.Size.X;

	const float HandleWidth = InteractionHandleSize;

	const FSlateRoundedBoxBrush RoundedBoxBrush(FLinearColor::Green, 2);
	FPaintGeometry HandleGeometry = AllottedGeometry.ToPaintGeometry(FVector2f(HandleWidth, HandleWidth), FSlateLayoutTransform(FVector2f(StartTimeHandleX, AllottedGeometry.Size.Y - HandleWidth)));

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		++LayerId,
		HandleGeometry,
		&RoundedBoxBrush,
		ESlateDrawEffect::None,
		FLinearColor::Green
	);

	HandleGeometry = AllottedGeometry.ToPaintGeometry(FVector2f(HandleWidth, HandleWidth), FSlateLayoutTransform(FVector2f(EndTimeHandleX - HandleWidth, AllottedGeometry.Size.Y - HandleWidth)));

	FSlateDrawElement::MakeBox(
		OutDrawElements,
		++LayerId,
		HandleGeometry,
		&RoundedBoxBrush,
		ESlateDrawEffect::None,
		FLinearColor::Red
	);

	return LayerId;
}

int32 FWaveformTransformationTrimFadeRenderer::DrawFadeCurves(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 LayerId) const
{
	const FLinearColor FadeColor = FLinearColor::Gray;
	const float HandleWidth = InteractionHandleSize;
	const float HandleHeight = InteractionHandleSize;
	const float HandleOffset = 5.0f;

	const FLinearColor HandleColor = FLinearColor::White;
	const FLinearColor HandleSelectedColor = FLinearColor::Yellow;

	const bool IsFadeInSelected = IsCursorInFadeInInteractionRange(MousePosition, AllottedGeometry);
	const bool IsFadeOutSelected = IsCursorInFadeOutInteractionRange(MousePosition, AllottedGeometry);
	
	if (FadeInCurvePoints.Num() > 0)
	{
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			++LayerId,
			AllottedGeometry.ToPaintGeometry(),
			FadeInCurvePoints,
			ESlateDrawEffect::None,
			FadeColor
		);

		const float	HandleStart = FadeInCurvePoints.Last(0).X;
		const FPaintGeometry HandleGeometry = AllottedGeometry.ToPaintGeometry(FVector2f(HandleWidth, HandleHeight), FSlateLayoutTransform(FVector2f(HandleStart - HandleOffset, 0)));
		const FSlateBrush* HandleBrush = FAppStyle::GetBrush("WorldBrowser.DirectionXPositive");
	
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			HandleGeometry,
			HandleBrush,
			ESlateDrawEffect::DisabledEffect,
			IsFadeInSelected ? HandleSelectedColor : HandleColor
		);
	}
	else
	{
		FPaintGeometry HandleGeometry = AllottedGeometry.ToPaintGeometry(FVector2f(HandleWidth, HandleHeight), FSlateLayoutTransform(FVector2f(StartTimeHandleX - HandleOffset, 0)));
		const FSlateBrush* HandleBrush = FAppStyle::GetBrush("WorldBrowser.DirectionXPositive");

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			HandleGeometry,
			HandleBrush,
			ESlateDrawEffect::DisabledEffect,
			IsFadeInSelected ? HandleSelectedColor : HandleColor
		);
	}

	if (FadeOutCurvePoints.Num() > 0)
	{
		FSlateDrawElement::MakeLines(
			OutDrawElements,
			++LayerId,
			AllottedGeometry.ToPaintGeometry(),
			FadeOutCurvePoints,
			ESlateDrawEffect::None,
			FadeColor
		);

		const float	HandleStart = FadeOutCurvePoints[0].X - HandleWidth;
		const FPaintGeometry HandleGeometry = AllottedGeometry.ToPaintGeometry(FVector2f(HandleWidth, HandleHeight), FSlateLayoutTransform(FVector2f(HandleStart + HandleOffset, 0)));
		const FSlateBrush* HandleBrush = FAppStyle::GetBrush("WorldBrowser.DirectionXNegative");

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			HandleGeometry,
			HandleBrush,
			ESlateDrawEffect::DisabledEffect,
			IsFadeOutSelected ? HandleSelectedColor : HandleColor
		);
	}
	else
	{
		FPaintGeometry HandleGeometry = AllottedGeometry.ToPaintGeometry(FVector2f(HandleWidth, HandleHeight), FSlateLayoutTransform(FVector2f(EndTimeHandleX - HandleWidth + HandleOffset, 0)));
		const FSlateBrush* HandleBrush = FAppStyle::GetBrush("WorldBrowser.DirectionXNegative");

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			HandleGeometry,
			HandleBrush,
			ESlateDrawEffect::DisabledEffect,
			IsFadeOutSelected ? HandleSelectedColor : HandleColor
		);
	}

	return LayerId;
}

void FWaveformTransformationTrimFadeRenderer::GenerateFadeCurves(const FGeometry& AllottedGeometry)
{
	check(StrongTrimFade);

	check(StartTimeHandleX >= 0);
	check(EndTimeHandleX >= 0);
	FadeInStartX = FMath::RoundToInt32(StartTimeHandleX);
	FadeOutEndX = FMath::RoundToInt32(EndTimeHandleX);

	if (StrongTrimFade->FadeFunctions.FadeIn && StrongTrimFade->FadeFunctions.FadeIn->Duration > 0.f)
	{
		const float FadeInTime = StrongTrimFade->FadeFunctions.FadeIn->Duration;
		const float FadeInFrames = FadeInTime * TransformationWaveInfo.SampleRate;
		const float FadeInPixelLength = FadeInFrames * PixelsPerFrame;
		check(FadeInPixelLength > 0.f);
		check(EndTimeHandleX >= 0);
		
		FadeInEndX = FMath::RoundToInt32(FMath::Clamp(StartTimeHandleX + FadeInPixelLength, StartTimeHandleX, EndTimeHandleX));

		const uint32 DisplayedFadeInPixelLength = FadeInEndX - FadeInStartX;
		FadeInCurvePoints.SetNumUninitialized(DisplayedFadeInPixelLength);

		for (uint32 Pixel = 0; Pixel < DisplayedFadeInPixelLength; ++Pixel)
		{
			const double FadeFraction = static_cast<float>(Pixel) / FadeInPixelLength;
			const double CurveFunction = StrongTrimFade->FadeFunctions.FadeIn->GetFadeInCurveValue(FadeFraction);

			const double CurveValue = Pixel != FadeInPixelLength - 1 ? 1.f - CurveFunction : 0.f;

			const uint32 XCoordinate = Pixel + FadeInStartX;
			FadeInCurvePoints[Pixel] = FVector2D(XCoordinate, CurveValue * AllottedGeometry.Size.Y);
		}
	}
	else
	{
		FadeInEndX = FMath::RoundToInt32(FMath::Clamp(StartTimeHandleX, StartTimeHandleX, EndTimeHandleX));
		FadeInCurvePoints.SetNumUninitialized(0);
	}

	if(StrongTrimFade->FadeFunctions.FadeOut && StrongTrimFade->FadeFunctions.FadeOut->Duration > 0.f)
	{
		const float FadeOutTime = StrongTrimFade->FadeFunctions.FadeOut->Duration;
		const float FadeOutFrames = FadeOutTime * TransformationWaveInfo.SampleRate;
		const float FadeOutPixelLength = FadeOutFrames * PixelsPerFrame;
		check(FadeOutPixelLength > 0.f);

		check(StartTimeHandleX >= 0);
		FadeOutStartX = FMath::RoundToInt32(FMath::Clamp(EndTimeHandleX - FadeOutPixelLength, StartTimeHandleX, EndTimeHandleX));

		const uint32 DisplayedFadeOutPixelLength = FadeOutEndX - FadeOutStartX;
		FadeOutCurvePoints.SetNumUninitialized(DisplayedFadeOutPixelLength);
		const uint32 FadeOutPixelOffset = FMath::RoundToInt32(FMath::Max(FadeOutPixelLength - static_cast<float>(DisplayedFadeOutPixelLength), 0.f));

		for (uint32 Pixel = 0; Pixel < DisplayedFadeOutPixelLength; ++Pixel)
		{
			const double FadeFraction = static_cast<float>(Pixel + FadeOutPixelOffset) / FadeOutPixelLength;
			const double CurveFunction = StrongTrimFade->FadeFunctions.FadeOut->GetFadeOutCurveValue(FadeFraction);

			const double CurveValue = Pixel != FadeOutPixelLength - 1 ? 1.f - CurveFunction : 1.f;

			const uint32 XCoordinate = Pixel + FadeOutStartX;
			FadeOutCurvePoints[Pixel] = FVector2D(XCoordinate, CurveValue * AllottedGeometry.Size.Y);
		}
	}
	else
	{
		FadeOutStartX = FMath::RoundToInt32(FMath::Clamp(EndTimeHandleX, StartTimeHandleX, EndTimeHandleX));
		FadeOutCurvePoints.SetNumUninitialized(0);
	}
}

FCursorReply FWaveformTransformationTrimFadeRenderer::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	const FVector2D LocalCursorPosition = GetLocalCursorPosition(CursorEvent, MyGeometry);

	if (TrimFadeInteractionType == ETrimFadeInteractionType::ScrubbingFadeIn || TrimFadeInteractionType == ETrimFadeInteractionType::ScrubbingFadeOut)
	{
		return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
	}

	if (IsCursorInFadeInInteractionRange(LocalCursorPosition, MyGeometry) || IsCursorInFadeOutInteractionRange(LocalCursorPosition, MyGeometry))
	{
		return FCursorReply::Cursor(EMouseCursor::GrabHand);
	}

	if ((TrimFadeInteractionType == ETrimFadeInteractionType::ScrubbingLeftHandle || TrimFadeInteractionType == ETrimFadeInteractionType::ScrubbingRightHandle) 
		|| (EndTimeInteractionXRange.Contains(LocalCursorPosition.X) && EndTimeInteractionYRange.Contains(LocalCursorPosition.Y))
		|| (StartTimeInteractionXRange.Contains(LocalCursorPosition.X) && StartTimeInteractionYRange.Contains(LocalCursorPosition.Y)))
	{
		return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
	}

	return FCursorReply::Unhandled();
}

void FWaveformTransformationTrimFadeRenderer::SetWaveformTransformation(TObjectPtr<UWaveformTransformationBase> InTransformation)
{
	FWaveformTransformationRendererBase::SetWaveformTransformation(InTransformation);
	StrongTrimFade = TStrongObjectPtr<UWaveformTransformationTrimFade>(CastChecked<UWaveformTransformationTrimFade>(InTransformation.Get()));
}

FReply FWaveformTransformationTrimFadeRenderer::OnMouseWheel(SWidget& OwnerWidget, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	check(StrongTrimFade);
	const FVector2D LocalCursorPosition = GetLocalCursorPosition(MouseEvent, MyGeometry);

	if (IsCursorInFadeInInteractionRange(LocalCursorPosition, MyGeometry) && StrongTrimFade->FadeFunctions.FadeIn && StrongTrimFade->FadeFunctions.FadeIn.IsA<UFadeCurveFunction>())
	{
		const float FadeCurveDelta = MouseEvent.GetWheelDelta() * MouseWheelStep;
		FText OutPropertyName = FText::FromName(NAME_None);

		TObjectPtr<UFadeCurveFunction> FadeIn = CastChecked<UFadeCurveFunction>(StrongTrimFade->FadeFunctions.FadeIn);
		OutPropertyName = FadeIn->GetFadeCurvePropertyName();

		if (BeginTransaction(TEXT("PropertyEditor"), FText::Format(NSLOCTEXT("PropertyEditor", "SetNumericPropertyTransaction", "Edit {0}"), OutPropertyName), StrongTrimFade.Get()) == INDEX_NONE)
		{
			UE_LOG(LogWaveformTransformation, Warning, TEXT("Begin TrimFadeTransformation OnMouseWheel Transaction Failed"));
		}

		StrongTrimFade->Modify();
		StrongTrimFade->FadeFunctions.FadeIn->Modify();

		const float FadeCurveValue = FadeIn->GetFadeCurve() + FadeCurveDelta;
		FadeIn->SetFadeCurve(FadeCurveValue);

		EndTransaction();
		StrongTrimFade->OnTransformationChanged.ExecuteIfBound(true);

		return FReply::Handled();
	}

	if (IsCursorInFadeOutInteractionRange(LocalCursorPosition, MyGeometry) && StrongTrimFade->FadeFunctions.FadeOut && StrongTrimFade->FadeFunctions.FadeOut.IsA<UFadeCurveFunction>())
	{
		const float FadeCurveDelta = MouseEvent.GetWheelDelta() * MouseWheelStep;
		FText OutPropertyName = FText::FromName(NAME_None);

		TObjectPtr<UFadeCurveFunction> FadeOut = CastChecked<UFadeCurveFunction>(StrongTrimFade->FadeFunctions.FadeOut);
		OutPropertyName = FadeOut->GetFadeCurvePropertyName();
		if (BeginTransaction(TEXT("PropertyEditor"), FText::Format(NSLOCTEXT("PropertyEditor", "SetNumericPropertyTransaction", "Edit {0}"), OutPropertyName), StrongTrimFade.Get()) == INDEX_NONE)
		{
			UE_LOG(LogWaveformTransformation, Warning, TEXT("Begin TrimFadeTransformation OnMouseWheel Transaction Failed"));
		}

		StrongTrimFade->Modify();
		StrongTrimFade->FadeFunctions.FadeOut->Modify();

		const float FadeCurveValue = FadeOut->GetFadeCurve() + FadeCurveDelta;
		FadeOut->SetFadeCurve(FadeCurveValue);

		EndTransaction();
		StrongTrimFade->OnTransformationChanged.ExecuteIfBound(true);

		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FVector2D FWaveformTransformationTrimFadeRenderer::GetLocalCursorPosition(const FPointerEvent& MouseEvent, const FGeometry& EventGeometry) const
{
	const FVector2D ScreenSpacePosition = MouseEvent.GetScreenSpacePosition();
	return  EventGeometry.AbsoluteToLocal(ScreenSpacePosition);
}

double FWaveformTransformationTrimFadeRenderer::ConvertXRatioToTime(const float InRatio) const
{
	check(TransformationWaveInfo.NumChannels > 0);

	const float NumFrames = TransformationWaveInfo.TotalNumSamples / TransformationWaveInfo.NumChannels;
	const float FrameSelected = NumFrames * InRatio;
	return FrameSelected / TransformationWaveInfo.SampleRate;
}

void FWaveformTransformationTrimFadeRenderer::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{ 
	check(TransformationWaveInfo.NumChannels > 0);
	check(StrongTrimFade);

	const float NumFrames = TransformationWaveInfo.TotalNumSamples / TransformationWaveInfo.NumChannels;
	const double FirstFrame = FMath::Clamp((StrongTrimFade->StartTime * TransformationWaveInfo.SampleRate) , 0.f, NumFrames);
	const double EndFrame = FMath::Clamp((StrongTrimFade->EndTime * TransformationWaveInfo.SampleRate), FirstFrame, NumFrames);

	check(NumFrames > 0);
	PixelsPerFrame = FMath::Max(AllottedGeometry.GetLocalSize().X / NumFrames, 0.0);

	StartTimeHandleX = FirstFrame * PixelsPerFrame;
	EndTimeHandleX = EndFrame * PixelsPerFrame;

	FVector2D MouseAbsolutePosition = UWidgetLayoutLibrary::GetMousePositionOnPlatform();
	MousePosition = AllottedGeometry.AbsoluteToLocal(MouseAbsolutePosition);

	GenerateFadeCurves(AllottedGeometry);
	UpdateInteractionRange(AllottedGeometry);
}

void FWaveformTransformationTrimFadeRenderer::UpdateInteractionRange(const FGeometry& AllottedGeometry)
{
	StartTimeInteractionXRange.SetLowerBoundValue(StartTimeHandleX - InteractionHandleSize);
	StartTimeInteractionXRange.SetUpperBoundValue(StartTimeHandleX + InteractionHandleSize);
	StartTimeInteractionYRange.SetLowerBoundValue(AllottedGeometry.GetLocalSize().Y - InteractionHandleSize);
	StartTimeInteractionYRange.SetUpperBoundValue(AllottedGeometry.GetLocalSize().Y);

	EndTimeInteractionXRange.SetLowerBoundValue(EndTimeHandleX - InteractionHandleSize);
	EndTimeInteractionXRange.SetUpperBoundValue(EndTimeHandleX + InteractionHandleSize);
	EndTimeInteractionYRange.SetLowerBoundValue(AllottedGeometry.GetLocalSize().Y - InteractionHandleSize);
	EndTimeInteractionYRange.SetUpperBoundValue(AllottedGeometry.GetLocalSize().Y);

	FadeInInteractionXRange.SetLowerBoundValue(FadeInEndX - InteractionHandleSize);
	FadeInInteractionXRange.SetUpperBoundValue(FadeInEndX + InteractionHandleSize);
	FadeOutInteractionXRange.SetLowerBoundValue(FadeOutStartX - InteractionHandleSize);
	FadeOutInteractionXRange.SetUpperBoundValue(FadeOutStartX + InteractionHandleSize);
}

FReply FWaveformTransformationTrimFadeRenderer::OnMouseButtonDown(SWidget& OwnerWidget, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	check(StrongTrimFade);
	const FVector2D LocalCursorPosition = GetLocalCursorPosition(MouseEvent, MyGeometry);

	const FKey MouseButton = MouseEvent.GetEffectingButton();

	TrimFadeInteractionType = GetInteractionTypeFromCursorPosition(LocalCursorPosition, MouseButton, MyGeometry);

	if (TrimFadeInteractionType != ETrimFadeInteractionType::None)
	{
		if (BeginTransaction(TEXT("PropertyEditor"), FText::Format(NSLOCTEXT("PropertyEditor", "SetNumericPropertyTransaction", "Edit {0}"), GetPropertyEditedByCurrentInteraction()), StrongTrimFade.Get()) == INDEX_NONE)
		{
			UE_LOG(LogWaveformTransformation, Warning, TEXT("Begin TrimFadeTransformation OnMouseButtonDown Transaction Failed"));
		}

		if (TrimFadeInteractionType == ETrimFadeInteractionType::ScrubbingFadeIn && StrongTrimFade->FadeFunctions.FadeIn)
		{
			StrongTrimFade->FadeFunctions.FadeIn->Modify();
		}
		else if (TrimFadeInteractionType == ETrimFadeInteractionType::ScrubbingFadeOut && StrongTrimFade->FadeFunctions.FadeOut)
		{
			StrongTrimFade->FadeFunctions.FadeOut->Modify();
		}
		
		StrongTrimFade->Modify();

		return FReply::Handled().CaptureMouse(OwnerWidget.AsShared()).PreventThrottling();
	}

	return FReply::Unhandled();
}


FReply FWaveformTransformationTrimFadeRenderer::OnMouseMove(SWidget& OwnerWidget, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	check(StrongTrimFade);
	if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && TrimFadeInteractionType != ETrimFadeInteractionType::None)
	{
		SetPropertyValueDependingOnInteractionType(MouseEvent, MyGeometry);

		StrongTrimFade->PostEditChange();

		StrongTrimFade->OnTransformationRenderChanged.ExecuteIfBound();

		return FReply::Handled().CaptureMouse(OwnerWidget.AsShared());
	}

	return FReply::Unhandled();
}

FReply FWaveformTransformationTrimFadeRenderer::OnMouseButtonUp(SWidget& OwnerWidget, const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	check(StrongTrimFade);
	if (TrimFadeInteractionType != ETrimFadeInteractionType::None)
	{
		SetPropertyValueDependingOnInteractionType(MouseEvent, MyGeometry);

		EndTransaction();

		StrongTrimFade->PostEditChange();

		StrongTrimFade->OnTransformationChanged.ExecuteIfBound(true);

		TrimFadeInteractionType = ETrimFadeInteractionType::None;
		
		return FReply::Handled().ReleaseMouseCapture();
	}

	return FReply::Unhandled();
}

void FWaveformTransformationTrimFadeRenderer::SetPropertyValueDependingOnInteractionType(const FPointerEvent& MouseEvent, const FGeometry& WidgetGeometry)
{
	check(StrongTrimFade);
	const FVector2D LocalCursorPosition = GetLocalCursorPosition(MouseEvent, WidgetGeometry);
	const float LocalCursorXRatio = FMath::Clamp(LocalCursorPosition.X / WidgetGeometry.GetLocalSize().X, 0.f, 1.f);
	const double SelectedTime = ConvertXRatioToTime(LocalCursorXRatio);

	switch (TrimFadeInteractionType)
	{
	case FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType::None:
		break;
	case FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType::ScrubbingLeftHandle:
		StrongTrimFade->StartTime = FMath::Min(SelectedTime, StrongTrimFade->EndTime);
		break;
	case FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType::ScrubbingRightHandle:
		StrongTrimFade->EndTime = FMath::Max(SelectedTime, StrongTrimFade->StartTime);
		break;
	case FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType::ScrubbingFadeIn:
		{
			float StartFadeTimeValue = 0;
			if (StrongTrimFade->FadeFunctions.FadeIn)
			{
				StartFadeTimeValue = FMath::Clamp(SelectedTime - StrongTrimFade->StartTime, 0.f, TNumericLimits<float>().Max());
				StrongTrimFade->FadeFunctions.FadeIn->Duration = StartFadeTimeValue;
			}
		}
		break;
	case FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType::ScrubbingFadeOut:
		{	
			float EndFadeTimeValue = 0;
			if (StrongTrimFade->FadeFunctions.FadeOut)
			{
				EndFadeTimeValue = FMath::Clamp(StrongTrimFade->EndTime - SelectedTime, 0.f, TNumericLimits<float>().Max());
				StrongTrimFade->FadeFunctions.FadeOut->Duration = EndFadeTimeValue;
			}
		}
		break;
	case FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType::RightClickFadeIn:
	case FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType::RightClickFadeOut:
	{
		ShowSelectFadeModeMenuAtCursor(WidgetGeometry, MouseEvent);
	}
		break;
	default:
		break;
	}
}

void FWaveformTransformationTrimFadeRenderer::ShowSelectFadeModeMenuAtCursor(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const FVector2D LocalWindowMaxPosition = MyGeometry.GetAbsolutePosition() + MyGeometry.GetAbsoluteSize();
	const FVector2D LocalCursorPosition = MouseEvent.GetScreenSpacePosition();

	if (TSharedPtr<SWindow> LockedFadeModeMenuWindow = FadeModeMenuWindow.Pin())
	{
		FSlateApplication::Get().RequestDestroyWindow(LockedFadeModeMenuWindow.ToSharedRef());
		FSlateApplication::Get().OnFocusChanging().Remove(PopupHandle); 
		FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ApplicationActivationStateHandle);
	}

	TSharedRef<SVerticalBox> MenuContent = SNew(SVerticalBox);

	TSharedRef<SWindow> MenuWindow = SNew(SWindow)
		.AutoCenter(EAutoCenter::None)
		.ScreenPosition(LocalCursorPosition)
		.SizingRule(ESizingRule::Autosized)
		.SupportsTransparency(EWindowTransparency::None)
		.IsPopupWindow(true)
		.CreateTitleBar(false)
		[
			MenuContent
		];

	FadeModeMenuWindow = MenuWindow;
	TWeakObjectPtr<UWaveformTransformationTrimFade> InWeakTrimFade = StrongTrimFade.Get();

	for (const TPair<EWaveEditorFadeMode, TSubclassOf<UFadeFunction>>& FadeOptionPair : UWaveformTransformationTrimFade::FadeModeToFadeFunctionMap)
	{
		MenuContent->AddSlot()
			.Padding(5)
			.AutoHeight()
			[
				SNew(SButton)
					.OnClicked_Lambda([InWeakFadeModeMenuWindow = FadeModeMenuWindow, InWeakTrimFade, InteractionType = TrimFadeInteractionType, FadeOptionPair, PropertyName = GetPropertyEditedByCurrentInteraction()]() -> FReply
					{
						if (TStrongObjectPtr<UWaveformTransformationTrimFade> PinnedTrimFade = InWeakTrimFade.Pin())
						{
							if (GEditor && GEditor->Trans)
							{
								GEditor->BeginTransaction(TEXT("PropertyEditor"), FText::Format(NSLOCTEXT("PropertyEditor", "SetCurveProperty", "Set Curve"), PropertyName), PinnedTrimFade.Get());
							}

							PinnedTrimFade->Modify();

							if (InteractionType == FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType::RightClickFadeIn)
							{
								if (PinnedTrimFade->FadeFunctions.FadeIn)
								{
									const float Duration = PinnedTrimFade->FadeFunctions.FadeIn->Duration;
									PinnedTrimFade->FadeFunctions.FadeIn = NewObject<UFadeFunction>(PinnedTrimFade.Get(), FadeOptionPair.Value.Get(), NAME_None, RF_Transactional);
									PinnedTrimFade->FadeFunctions.FadeIn->Duration =  Duration;
								}
							}
							else if (InteractionType == FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType::RightClickFadeOut)
							{
								if (PinnedTrimFade->FadeFunctions.FadeOut)
								{
									const float Duration = PinnedTrimFade->FadeFunctions.FadeOut->Duration;
									PinnedTrimFade->FadeFunctions.FadeOut = NewObject<UFadeFunction>(PinnedTrimFade.Get(), FadeOptionPair.Value.Get(), NAME_None, RF_Transactional);
									PinnedTrimFade->FadeFunctions.FadeOut->Duration = Duration;
								}
							}

							if (GEditor && GEditor->Trans)
							{
								GEditor->EndTransaction();
							}

							PinnedTrimFade->PostEditChange();
							PinnedTrimFade->OnTransformationChanged.ExecuteIfBound(true);
						}

						TSharedPtr<SWindow> LockedFadeModeMenuWindow = InWeakFadeModeMenuWindow.Pin();
						if (LockedFadeModeMenuWindow)
						{
							FSlateApplication::Get().RequestDestroyWindow(LockedFadeModeMenuWindow.ToSharedRef());
						}

						return FReply::Handled();
					})
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Left)
						[
							SNew(SImage)
							.Image(GetFadeModeIcon(FadeOptionPair.Key).GetIcon())
						]
						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						.Padding(5)
						[
							SNew(STextBlock)
							.Text(FText::FromString(StaticEnum<EWaveEditorFadeMode>()->GetNameStringByValue(static_cast<int64>(FadeOptionPair.Key))))
						]
					]
			];
	}

	FSlateApplication::Get().AddWindow(MenuWindow);

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([InFadeModeMenuWindow = FadeModeMenuWindow, LocalCursorPosition, LocalWindowMaxPosition](float DeltaTime)
		{
			if (TSharedPtr<SWindow> LockedFadeModeMenuWindow = InFadeModeMenuWindow.Pin())
			{
				const FVector2D WindowSize = LockedFadeModeMenuWindow->GetSizeInScreen();

				if (WindowSize.X > 0.0 && LocalCursorPosition.X + WindowSize.X > LocalWindowMaxPosition.X)
				{
					check(WindowSize.Y > 0.0);

					const double XPosition = FMath::Max(0.0, LocalCursorPosition.X - WindowSize.X);
					LockedFadeModeMenuWindow->MoveWindowTo(FVector2D(XPosition, LocalCursorPosition.Y));
				}
			}

			return false; // return false to only execute once
		}));

	TSharedPtr<FDelegateHandle> ApplicationActivationStateHandlePtr = MakeShared<FDelegateHandle>();
	*ApplicationActivationStateHandlePtr = FSlateApplication::Get().OnApplicationActivationStateChanged().AddLambda([InFadeModeMenuWindow = FadeModeMenuWindow, ApplicationActivationStateHandlePtr](bool isActive)
		{
			if (!isActive)
			{
				if (TSharedPtr<SWindow> LockedFadeModeMenuWindow = InFadeModeMenuWindow.Pin())
				{
					FSlateApplication::Get().RequestDestroyWindow(LockedFadeModeMenuWindow.ToSharedRef());
					FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(*ApplicationActivationStateHandlePtr);
				}
			}
		});

	if (ApplicationActivationStateHandle.IsValid())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ApplicationActivationStateHandle);
	}

	ApplicationActivationStateHandle = *ApplicationActivationStateHandlePtr;

	// If focus is lost on the popup, destroy it to prevent popups hanging around
	TSharedPtr<FDelegateHandle> PopupHandlePtr = MakeShared<FDelegateHandle>();
	*PopupHandlePtr = FSlateApplication::Get().OnFocusChanging().AddLambda([InFadeModeMenuWindow = FadeModeMenuWindow, PopupHandlePtr](const FFocusEvent& FocusEvent, const FWeakWidgetPath& WeakWidgetPath
		, const TSharedPtr<SWidget>& OldWidget, const FWidgetPath& WidgetPath, const TSharedPtr<SWidget>& NewWidget)
		{
			if (InFadeModeMenuWindow != nullptr && InFadeModeMenuWindow.IsValid())
			{
				if (TSharedPtr<SWindow> LockedFadeModeMenuWindow = InFadeModeMenuWindow.Pin())
				{
					if (OldWidget && !OldWidget->IsHovered() && LockedFadeModeMenuWindow == OldWidget)
					{
						FSlateApplication::Get().RequestDestroyWindow(LockedFadeModeMenuWindow.ToSharedRef());
						FSlateApplication::Get().OnFocusChanging().Remove(*PopupHandlePtr);
					}
				}
			}
		});

	if (PopupHandle.IsValid())
	{
		FSlateApplication::Get().OnFocusChanging().Remove(PopupHandle);
	}

	PopupHandle = *PopupHandlePtr;
}

const FSlateIcon FWaveformTransformationTrimFadeRenderer::GetFadeModeIcon(const EWaveEditorFadeMode& FadeMode)
{
	if (TrimFadeInteractionType == FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType::RightClickFadeIn)
	{
		switch (FadeMode)
		{
		case EWaveEditorFadeMode::Linear:
			return FSlateIcon(FAudioWidgetsStyle::StyleName, "AudioWidgetsStyle.FadeInLinear");
		case EWaveEditorFadeMode::Exponential:
			return FSlateIcon(FAudioWidgetsStyle::StyleName, "AudioWidgetsStyle.FadeInExponential");
		case EWaveEditorFadeMode::Logarithmic:
			return FSlateIcon(FAudioWidgetsStyle::StyleName, "AudioWidgetsStyle.FadeInLogarithmic");
		case EWaveEditorFadeMode::Sigmoid:
			return FSlateIcon(FAudioWidgetsStyle::StyleName, "AudioWidgetsStyle.FadeInSigmoid");
		default:
			return FSlateIcon();
		}
	}
	else if (TrimFadeInteractionType == FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType::RightClickFadeOut)
	{
		switch (FadeMode)
		{
		case EWaveEditorFadeMode::Linear:
			return FSlateIcon(FAudioWidgetsStyle::StyleName, "AudioWidgetsStyle.FadeOutLinear");
		case EWaveEditorFadeMode::Exponential:
			return FSlateIcon(FAudioWidgetsStyle::StyleName, "AudioWidgetsStyle.FadeOutExponential");
		case EWaveEditorFadeMode::Logarithmic:
			return FSlateIcon(FAudioWidgetsStyle::StyleName, "AudioWidgetsStyle.FadeOutLogarithmic");
		case EWaveEditorFadeMode::Sigmoid:
			return FSlateIcon(FAudioWidgetsStyle::StyleName, "AudioWidgetsStyle.FadeOutSigmoid");
		default:
			return FSlateIcon();
		}
	}

	return FSlateIcon();
}

FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType FWaveformTransformationTrimFadeRenderer::GetInteractionTypeFromCursorPosition(const FVector2D& InLocalCursorPosition, const FKey MouseButton, const FGeometry& WidgetGeometry) const
{
	if (MouseButton == EKeys::LeftMouseButton)
	{
		if (IsCursorInFadeInInteractionRange(InLocalCursorPosition, WidgetGeometry))
		{
			return ETrimFadeInteractionType::ScrubbingFadeIn;
		}

		if (IsCursorInFadeOutInteractionRange(InLocalCursorPosition, WidgetGeometry))
		{
			return ETrimFadeInteractionType::ScrubbingFadeOut;
		}

		if (StartTimeInteractionXRange.Contains(InLocalCursorPosition.X) && StartTimeInteractionYRange.Contains(InLocalCursorPosition.Y))
		{
			return ETrimFadeInteractionType::ScrubbingLeftHandle;
		}

		if (EndTimeInteractionXRange.Contains(InLocalCursorPosition.X) && EndTimeInteractionYRange.Contains(InLocalCursorPosition.Y))
		{
			return ETrimFadeInteractionType::ScrubbingRightHandle;
		}
	}
	else if (MouseButton == EKeys::RightMouseButton)
	{
		if (IsCursorInFadeInInteractionRange(InLocalCursorPosition, WidgetGeometry))
		{
			return ETrimFadeInteractionType::RightClickFadeIn;
		}

		if (IsCursorInFadeOutInteractionRange(InLocalCursorPosition, WidgetGeometry))
		{
			return ETrimFadeInteractionType::RightClickFadeOut;
		}
	}

	return ETrimFadeInteractionType::None;
}

bool FWaveformTransformationTrimFadeRenderer::IsCursorInFadeInInteractionRange(const FVector2D& InLocalCursorPosition, const FGeometry& WidgetGeometry) const
{
	return FadeInInteractionXRange.Contains(InLocalCursorPosition.X)
		&& InLocalCursorPosition.Y < InteractionHandleSize;
}

bool FWaveformTransformationTrimFadeRenderer::IsCursorInFadeOutInteractionRange(const FVector2D& InLocalCursorPosition, const FGeometry& WidgetGeometry) const
{
	return FadeOutInteractionXRange.Contains(InLocalCursorPosition.X)
		&& InLocalCursorPosition.Y < InteractionHandleSize;
}

FText FWaveformTransformationTrimFadeRenderer::GetPropertyEditedByCurrentInteraction() const
{
	FText OutPropertyName = FText::FromName(NAME_None);

	switch (TrimFadeInteractionType)
	{
	case FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType::None:
		break;
	case FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType::ScrubbingLeftHandle:
		OutPropertyName = FText::FromName(GET_MEMBER_NAME_CHECKED(UWaveformTransformationTrimFade, StartTime));
		break;
	case FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType::ScrubbingRightHandle:
		OutPropertyName = FText::FromName(GET_MEMBER_NAME_CHECKED(UWaveformTransformationTrimFade, EndTime));
		break;
	case FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType::ScrubbingFadeIn:
		if (StrongTrimFade->FadeFunctions.FadeIn)
		{
			OutPropertyName = FText::Format(FText::FromString("{0}::{1}"),
				FText::FromName(GET_MEMBER_NAME_CHECKED(FFadeFunctionData, FadeIn)),
				FText::FromName(GET_MEMBER_NAME_CHECKED(UFadeFunction, Duration)));
		}
		break;
	case FWaveformTransformationTrimFadeRenderer::ETrimFadeInteractionType::ScrubbingFadeOut:
		if (StrongTrimFade->FadeFunctions.FadeOut)
		{
			OutPropertyName = FText::Format(FText::FromString("{0}::{1}"),
				FText::FromName(GET_MEMBER_NAME_CHECKED(FFadeFunctionData, FadeOut)),
				FText::FromName(GET_MEMBER_NAME_CHECKED(UFadeFunction, Duration)));
		}
		break;
	default:
		break;
	}

	return MoveTemp(OutPropertyName);

}