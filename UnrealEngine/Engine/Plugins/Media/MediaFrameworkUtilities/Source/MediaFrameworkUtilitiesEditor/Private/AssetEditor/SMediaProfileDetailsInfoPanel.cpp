// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMediaProfileDetailsInfoPanel.h"

#include "DetailLayoutBuilder.h"
#include "DummyMediaObject.h"
#include "MediaFrameworkWorldSettingsAssetUserData.h"
#include "MediaOutput.h"
#include "MediaProfileEditor.h"
#include "MediaProfileEditorUserSettings.h"
#include "MediaSource.h"
#include "Engine/TextureRenderTarget2D.h"
#include "MediaAssets/ProxyMediaOutput.h"
#include "MediaAssets/ProxyMediaSource.h"
#include "Profile/MediaProfile.h"
#include "Profile/MediaProfilePlaybackManager.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMediaObjectInfoPanel"

TSharedRef<SWidget> SMediaObjectInfoPanel::FTwoColumnInfo::CreateWidget()
{
	return SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 12.0f, 2.0f)
		[
			SAssignNew(LabelColumn, SVerticalBox)
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SAssignNew(ValueColumn, SVerticalBox)
		];
}

void SMediaObjectInfoPanel::FTwoColumnInfo::AddEntry(TAttribute<FText> Label, TAttribute<FText> Value, TAttribute<FSlateColor> ValueColor)
{
	LabelColumn->AddSlot()
	    .Padding(0.0, 2.0f)
		[
			SNew(STextBlock)
			.Text(Label)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];

	ValueColumn->AddSlot()
	    .Padding(0.0, 2.0f)
		[
			SNew(STextBlock)
			.Text(Value)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(ValueColor)
		];
}

void SMediaObjectInfoPanel::FTwoColumnInfo::AddEntry(TAttribute<FText> Value, const FSlateBrush* Icon)
{
	LabelColumn->AddSlot()
		.Padding(0.0, 2.0f)
		[
			SNullWidget::NullWidget
		];

	ValueColumn->AddSlot()
		.Padding(0.0, 2.0f)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(16.0f)
				.HeightOverride(16.0f)
				[
					SNew(SImage).Image(Icon)
				]
			]

			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(Value)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];
}

void SMediaObjectInfoPanel::FTwoColumnInfo::AddEntry(TAttribute<FText> Value)
{
	LabelColumn->AddSlot()
		.Padding(0.0, 2.0f)
		[
			SNullWidget::NullWidget
		];

	ValueColumn->AddSlot()
		.Padding(0.0, 2.0f)
		[
			SNew(STextBlock)
			.Text(Value)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];
}

void SMediaObjectInfoPanel::FTwoColumnInfo::ClearEntries()
{
	if (LabelColumn.IsValid())
	{
		LabelColumn->ClearChildren();
	}

	if (ValueColumn.IsValid())
	{
		ValueColumn->ClearChildren();
	}
}

void SMediaObjectInfoPanel::Construct(const FArguments& InArgs, TWeakPtr<FMediaProfileEditor>& InMediaProfileEditor, UMediaProfile* InMediaProfile)
{
	MediaProfileEditor = InMediaProfileEditor;
	MediaProfile = InMediaProfile;
		
	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 16.0f)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.FillWidth(0.5f)
			[
				CreateInfoGroupWidget(LOCTEXT("BasicInfoHeader", "Info"), BasicInfo)
			]

			+SHorizontalBox::Slot()
			.FillWidth(0.5f)
			[
				CreateInfoGroupWidget(TAttribute<FText>::CreateLambda([this]()
				{
					return ObjectAsMediaSource() ? LOCTEXT("MediaSourceInfoHeader", "Media Source") : LOCTEXT("MediaOutputInfoHeader", "Media Output");
				}), TypeInfo)
			]
		]
			
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(MediaObjectInfoBox, SHorizontalBox)
		]
	];
}

void SMediaObjectInfoPanel::SetMediaObject(UObject* InMediaObject)
{
	MediaObject = InMediaObject;
	ClearInfo();
	FillInfo();

	if (!MediaObject.IsValid())
	{
		SetVisibility(EVisibility::Collapsed);
	}
	else
	{
		SetVisibility(EVisibility::Visible);
	}
}

UMediaSource* SMediaObjectInfoPanel::ObjectAsMediaSource() const
{
	return Cast<UMediaSource>(MediaObject);
}

UMediaOutput* SMediaObjectInfoPanel::ObjectAsMediaOutput() const
{
	return Cast<UMediaOutput>(MediaObject);
}

void SMediaObjectInfoPanel::ClearInfo()
{
	BasicInfo.ClearEntries();
	TypeInfo.ClearEntries();
	CaptureInfo.ClearEntries();
	
	MediaObjectInfoBox->ClearChildren();
}

void SMediaObjectInfoPanel::FillInfo()
{
	if (!MediaObject.IsValid() || !MediaProfile.IsValid())
	{
		return;
	}

	UObject* ActualMediaObject = MediaObject.Get();
	if (UProxyMediaSource* ProxySource = Cast<UProxyMediaSource>(ActualMediaObject))
	{
		ActualMediaObject = ProxySource->GetLeafMediaSource();
	}
	else if (UProxyMediaOutput* ProxyOutput = Cast<UProxyMediaOutput>(ActualMediaObject))
	{
		ActualMediaObject = ProxyOutput->GetLeafMediaOutput();
	}
		
	FText Label;
	if (UMediaSource* MediaSource = ObjectAsMediaSource())
	{
		int32 Index = MediaProfile->FindMediaSourceIndex(MediaSource);
		if (MediaSource->IsA<UDummyMediaSource>())
		{
			Index = Cast<UDummyMediaSource>(MediaSource)->MediaProfileIndex;
		}
		
		Label = FText::FromString(MediaProfile->GetLabelForMediaSource(Index));
	}
	else if (UMediaOutput* MediaOutput = ObjectAsMediaOutput())
	{
		int32 Index = MediaProfile->FindMediaOutputIndex(MediaOutput);
		if (MediaOutput->IsA<UDummyMediaOutput>())
		{
			Index = Cast<UDummyMediaOutput>(MediaOutput)->MediaProfileIndex;
		}
		
		Label = FText::FromString(MediaProfile->GetLabelForMediaOutput(Index));
	}
		
	BasicInfo.AddEntry(LOCTEXT("MediaObjectNameLabel", "Label:"), Label);

	const bool bIsProxy = MediaObject->IsA<UProxyMediaSource>() || MediaObject->IsA<UProxyMediaOutput>();
	BasicInfo.AddEntry(LOCTEXT("MediaObjectIsProxyLabel", "Is Proxy:"), bIsProxy ? LOCTEXT("Yes", "Yes") : LOCTEXT("No", "No"));

	if (ActualMediaObject)
	{
		TypeInfo.AddEntry(ActualMediaObject->GetClass()->GetDisplayNameText(), FSlateIconFinder::FindIconForClass(ActualMediaObject->GetClass()).GetIcon());
	}
	else
	{
		const FText NoObjectSet = ObjectAsMediaSource() ? LOCTEXT("NoMediaSourceSet", "No media source set") : LOCTEXT("NoMediaOutputSet", "No media output set");
		TypeInfo.AddEntry(NoObjectSet);
	}
		
	TArray<TTuple<FText, FText, FText>> InfoElements;

	if (UMediaSource* MediaSource = Cast<UMediaSource>(ActualMediaObject))
	{
		MediaSource->GetDetailsPanelInfoElements(InfoElements);
	}
	else if (UMediaOutput* MediaOutput = Cast<UMediaOutput>(ActualMediaObject))
	{
		MediaOutput->GetDetailsPanelInfoElements(InfoElements);
	}
		
	TMap<FString, FTwoColumnInfo> InfoWidgets;
	for (TTuple<FText, FText, FText>& InfoElement : InfoElements)
	{
		const FText InfoGroup = InfoElement.Get<0>();
		const FText InfoLabel = InfoElement.Get<1>();
		const FText InfoValue = InfoElement.Get<2>();
			
		if (!InfoWidgets.Contains(InfoGroup.ToString()))
		{
			InfoWidgets.Add(InfoGroup.ToString(), FTwoColumnInfo());
			MediaObjectInfoBox->AddSlot()
			[
				CreateInfoGroupWidget(InfoGroup, InfoWidgets[InfoGroup.ToString()])
			];
		}

		InfoWidgets[InfoGroup.ToString()].AddEntry(FText::Format(LOCTEXT("MediaObjectInfoElementLabel", "{0}:"), InfoLabel), InfoValue);
	}
	
	if (Cast<UMediaOutput>(ActualMediaObject))
	{
		MediaObjectInfoBox->AddSlot()
		[
			CreateInfoGroupWidget(LOCTEXT("CaptureSettingsGroupLabel", "Capture Settings"), CaptureInfo)
		];
		
		CaptureInfo.AddEntry(LOCTEXT("CaptureMethodLabel", "Capture Method:"), TAttribute<FText>::CreateSP(this, &SMediaObjectInfoPanel::GetCaptureMethodText));
		CaptureInfo.AddEntry(TAttribute<FText>::CreateSP(this, &SMediaObjectInfoPanel::GetCaptureObjectLabelText), TAttribute<FText>::CreateSP(this, &SMediaObjectInfoPanel::GetCaptureObjectValueText));
		CaptureInfo.AddEntry(LOCTEXT("CaptureStatsLabel", "Status:"),
			TAttribute<FText>::CreateSP(this, &SMediaObjectInfoPanel::GetCaptureStatusText),
			TAttribute<FSlateColor>::CreateSP(this, &SMediaObjectInfoPanel::GetCaptureStatusColor));
		CaptureInfo.AddEntry(LOCTEXT("CaptureColorConversionLabel", "Color Conversion:"), TAttribute<FText>::CreateSP(this, &SMediaObjectInfoPanel::GetCaptureColorConversionText));
	}
}

TSharedRef<SWidget> SMediaObjectInfoPanel::CreateInfoGroupWidget(const TAttribute<FText>& InHeader, FTwoColumnInfo& InInfo)
{
	return SNew(SVerticalBox)

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(InHeader)
			.Font(IDetailLayoutBuilder::GetDetailFontBold())
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			InInfo.CreateWidget()
		];			
}

FText SMediaObjectInfoPanel::GetCaptureMethodText() const
{
	UMediaOutput* MediaOutput = ObjectAsMediaOutput();
	UMediaProfileEditorCaptureSettings* CaptureSettings = FMediaProfileEditor::GetMediaFrameworkCaptureSettings();
	if (!MediaOutput || !CaptureSettings)
	{
		return FText::GetEmpty();
	}

	const bool bHasCurrentViewportCapture = CaptureSettings->CurrentViewportMediaOutput.MediaOutput == MediaOutput;
	const bool bHasViewportCaptures = CaptureSettings->ViewportCaptures.ContainsByPredicate([MediaOutput](const FMediaFrameworkCaptureCameraViewportCameraOutputInfo& Info)
	{
		return Info.MediaOutput == MediaOutput;
	});

	const bool bHasRenderTargetCaptures = CaptureSettings->RenderTargetCaptures.ContainsByPredicate([MediaOutput](const FMediaFrameworkCaptureRenderTargetCameraOutputInfo& Info)
	{
		return Info.MediaOutput == MediaOutput;
	});

	const bool bHasMultipleValues = (bHasCurrentViewportCapture && (bHasViewportCaptures || bHasRenderTargetCaptures)) || (bHasViewportCaptures && bHasRenderTargetCaptures);
	if (bHasMultipleValues)
	{
		return LOCTEXT("MultipleCapturesValue", "Multiple Captures");
	}

	if (bHasCurrentViewportCapture)
	{
		return LOCTEXT("CurrentViewportCaptureValue", "Current Viewport");	
	}

	if (bHasViewportCaptures)
	{
		return LOCTEXT("ViewportCaptureValue", "Media Viewport");
	}

	if (bHasRenderTargetCaptures)
	{
		return LOCTEXT("RenderTargetCaptureValue", "Render Target");
	}

	return LOCTEXT("NoCapturesValues", "No Captures Configured");
}

FText SMediaObjectInfoPanel::GetCaptureObjectLabelText() const
{
	UMediaOutput* MediaOutput = ObjectAsMediaOutput();
	UMediaProfileEditorCaptureSettings* CaptureSettings = FMediaProfileEditor::GetMediaFrameworkCaptureSettings();
	if (!MediaOutput || !CaptureSettings)
	{
		return FText::GetEmpty();
	}

	const bool bHasCurrentViewportCapture = CaptureSettings->CurrentViewportMediaOutput.MediaOutput == MediaOutput;
	bool bHasViewportCaptures = CaptureSettings->ViewportCaptures.ContainsByPredicate([MediaOutput](const FMediaFrameworkCaptureCameraViewportCameraOutputInfo& Info)
	{
		return Info.MediaOutput == MediaOutput;
	});

	bool bHasRenderTargetCaptures = CaptureSettings->RenderTargetCaptures.ContainsByPredicate([MediaOutput](const FMediaFrameworkCaptureRenderTargetCameraOutputInfo& Info)
	{
		return Info.MediaOutput == MediaOutput;
	});

	const bool bHasMultipleValues = (bHasCurrentViewportCapture && (bHasViewportCaptures || bHasRenderTargetCaptures)) || (bHasViewportCaptures && bHasRenderTargetCaptures);
	if (bHasMultipleValues)
	{
		return LOCTEXT("MultipleCaptureObjectsLabel", "Multiple Objects:");
	}

	if (bHasCurrentViewportCapture)
	{
		return FText::GetEmpty();
	}

	if (bHasViewportCaptures)
	{
		return LOCTEXT("CameraObjectLabel", "Cameras:");
	}

	if (bHasRenderTargetCaptures)
	{
		return LOCTEXT("RenderTargetObjectLabel", "Render Target:");
	}

	return LOCTEXT("NoObjectsLabel", "No Objects:");
}

FText SMediaObjectInfoPanel::GetCaptureObjectValueText() const
{
	UMediaOutput* MediaOutput = ObjectAsMediaOutput();
	UMediaProfileEditorCaptureSettings* CaptureSettings = FMediaProfileEditor::GetMediaFrameworkCaptureSettings();
	if (!MediaOutput || !CaptureSettings)
	{
		return FText::GetEmpty();
	}

	const bool bHasCurrentViewportCapture = CaptureSettings->CurrentViewportMediaOutput.MediaOutput == MediaOutput;

	bool bHasViewportCaptures = false;
	FText CamerasText;
	for (const FMediaFrameworkCaptureCameraViewportCameraOutputInfo& Info : CaptureSettings->ViewportCaptures)
	{
		if (Info.MediaOutput != MediaOutput)
		{
			continue;
		}

		bHasViewportCaptures = true;
		
		for (const TSoftObjectPtr<AActor>& Camera : Info.Cameras)
		{
			AActor* Actor = Camera.Get();
			if (Actor)
			{
				if (CamerasText.IsEmpty())
				{
					CamerasText = FText::FromString(Actor->GetActorNameOrLabel());
				}
				else
				{
					CamerasText = FText::Format(LOCTEXT("AppendCameraNameFormat", "{0}, {1}"), CamerasText, FText::FromString(Actor->GetActorNameOrLabel()));
				}
			}
		}
	}

	bool bHasRenderTargetCaptures = false;
	FText RenderTargetsText;
	for (const FMediaFrameworkCaptureRenderTargetCameraOutputInfo& Info : CaptureSettings->RenderTargetCaptures)
	{
		if (Info.MediaOutput != MediaOutput)
		{
			continue;
		}

		bHasRenderTargetCaptures = true;
		
		
		FText RenderTargetText = Info.RenderTarget ? FText::FromString(Info.RenderTarget->GetName()) : LOCTEXT("NoRenderTarget", "No Render Target");
		
		if (RenderTargetsText.IsEmpty())
		{
			RenderTargetsText = RenderTargetText;
		}
		else
		{
			RenderTargetsText = FText::Format(LOCTEXT("AppendRenderTargetNameFormat", "{0}, {1}"), RenderTargetsText, RenderTargetText);
		}
	}

	const bool bHasMultipleValues = (bHasCurrentViewportCapture && (bHasViewportCaptures || bHasRenderTargetCaptures)) || (bHasViewportCaptures && bHasRenderTargetCaptures);
	if (bHasMultipleValues)
	{
		return FText::GetEmpty();
	}

	if (bHasViewportCaptures)
	{
		return CamerasText;
	}

	if (bHasRenderTargetCaptures)
	{
		return RenderTargetsText;
	}

	return FText::GetEmpty();
}

FText SMediaObjectInfoPanel::GetCaptureStatusText() const
{
	UMediaOutput* MediaOutput = ObjectAsMediaOutput();
	if (!MediaOutput || !MediaProfile.IsValid())
	{
		return LOCTEXT("UnknownCaptureStatusText", "Unknown");
	}

	return MediaProfile.Pin()->GetPlaybackManager()->IsOutputCapturing(MediaOutput) ? LOCTEXT("CapturingStatusText", "Capturing") : LOCTEXT("NotCapturingStatusText", "Not Capturing");
}

FSlateColor SMediaObjectInfoPanel::GetCaptureStatusColor() const
{
	UMediaOutput* MediaOutput = ObjectAsMediaOutput();
	if (!MediaOutput || !MediaProfile.IsValid())
	{
		return FSlateColor::UseForeground();
	}

	return MediaProfile.Pin()->GetPlaybackManager()->IsOutputCapturing(MediaOutput) ? FSlateColor(FLinearColor(0.0, 1.0, 0.0)) : FSlateColor::UseForeground();
}

FText SMediaObjectInfoPanel::GetCaptureColorConversionText() const
{
	UMediaOutput* MediaOutput = ObjectAsMediaOutput();
	UMediaProfileEditorCaptureSettings* CaptureSettings = FMediaProfileEditor::GetMediaFrameworkCaptureSettings();
	if (!MediaOutput || !CaptureSettings)
	{
		return LOCTEXT("NoneText", "None");
	}

	FText ColorConversionText;
	if (CaptureSettings->CurrentViewportMediaOutput.MediaOutput == MediaOutput)
	{
		if (ColorConversionText.IsEmpty())
		{
			ColorConversionText = FText::FromString(CaptureSettings->CurrentViewportMediaOutput.CaptureOptions.GetColorConversionSettingsString());
		}
		else
		{
			ColorConversionText = FText::Format(
				LOCTEXT("AppendColorConversionFormat", "{0}, {1}"),
				ColorConversionText,
				FText::FromString(CaptureSettings->CurrentViewportMediaOutput.CaptureOptions.GetColorConversionSettingsString()));
		}
	}
	
	for (const FMediaFrameworkCaptureCameraViewportCameraOutputInfo& Info : CaptureSettings->ViewportCaptures)
	{
		if (Info.MediaOutput != MediaOutput)
		{
			continue;
		}

		if (ColorConversionText.IsEmpty())
		{
			ColorConversionText = FText::FromString(Info.CaptureOptions.GetColorConversionSettingsString());
		}
		else
		{
			ColorConversionText = FText::Format(LOCTEXT("AppendColorConversionFormat", "{0}, {1}"), ColorConversionText, FText::FromString(Info.CaptureOptions.GetColorConversionSettingsString()));
		}
	}
	
	for (const FMediaFrameworkCaptureRenderTargetCameraOutputInfo& Info : CaptureSettings->RenderTargetCaptures)
	{
		if (Info.MediaOutput != MediaOutput)
		{
			continue;
		}

		if (ColorConversionText.IsEmpty())
		{
			ColorConversionText = FText::FromString(Info.CaptureOptions.GetColorConversionSettingsString());
		}
		else
		{
			ColorConversionText = FText::Format(LOCTEXT("AppendColorConversionFormat", "{0}, {1}"), ColorConversionText, FText::FromString(Info.CaptureOptions.GetColorConversionSettingsString()));
		}
	}

	if (ColorConversionText.IsEmpty())
	{
		return LOCTEXT("NoneText", "None");
	}

	return ColorConversionText;
}

#undef LOCTEXT_NAMESPACE
