// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanVideoBaseLiveLinkSubjectMonitorWidget.h"
#include "MetaHumanVideoBaseLiveLinkSubjectSettings.h"
#include "MetaHumanVideoBaseLiveLinkSubject.h"
#include "MetaHumanVideoLiveLinkSettings.h"
#include "MetaHumanLiveLinkSourceStyle.h"

#include "Nodes/HyprsenseRealtimeNode.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SSpacer.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "MetaHumanVideoBaseLiveLinkSubjectMonitorWidget"



SMetaHumanVideoBaseLiveLinkSubjectMonitorWidget::~SMetaHumanVideoBaseLiveLinkSubjectMonitorWidget()
{
	if (EditorTimerHandle.IsValid())
	{
#if WITH_EDITOR
		if (GEditor)
		{
			GEditor->GetTimerManager()->ClearTimer(EditorTimerHandle);
		}
#endif
	}
}

void SMetaHumanVideoBaseLiveLinkSubjectMonitorWidget::Construct(const FArguments& InArgs, UMetaHumanVideoBaseLiveLinkSubjectSettings* InSettings, bool bInAllowResize)
{
	Settings = InSettings;

	ImageViewer = SNew(SMetaHumanImageViewer);
	ImageViewer->SetImage(&ImageViewerBrush);
	ImageViewer->SetNonConstBrush(&ImageViewerBrush);
	ImageViewerBrush.SetUVRegion(FBox2f{ FVector2f{ 0.0f, 0.0f }, FVector2f{ 1.0f, 1.0f } });
	ImageViewer->OnViewChanged.AddLambda([this](FBox2f InUV) // Lambda that reacts to inputs in the image viewer, used for zooming and panning
	{
		ImageViewerBrush.SetUVRegion(InUV);
	});

	ClearTexture();

	TSharedPtr<SHorizontalBox> Box = SNew(SHorizontalBox);

	Box->AddSlot()
		.FillWidth(1.0f)
		[
			ImageViewer.ToSharedRef()
		];

	if (bInAllowResize)
	{
		const FMargin ContentPadding = FMargin(0.0, 2.0);
		const UMetaHumanVideoLiveLinkSettings* DefaultSettings = GetDefault<UMetaHumanVideoLiveLinkSettings>();
		const FSlateColor ImageColour = FLinearColor(0.5, 0.5, 0.5);

		Box->AddSlot()
			.AutoWidth()
			.Padding(5, 0, 5, 0)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SButton)
					.ContentPadding(ContentPadding)
					.ToolTipText(LOCTEXT("IncreaseHeightTooltip", "Increase height of video monitor widget"))
					.OnClicked_Lambda([this]()
					{
						Settings->MonitorImageSize.Y += 50;

						return FReply::Handled();
					})
					.Content()
					[
						SNew(SImage)
						.ColorAndOpacity(ImageColour)
						.Image(FMetaHumanLiveLinkSourceStyle::Get().GetBrush("IncreaseSize"))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 5, 0, 0)
				[
					SNew(SButton)
					.ContentPadding(ContentPadding)
					.ToolTipText(LOCTEXT("ResetHeightTooltip", "Reset height of video monitor widget"))
					.OnClicked_Lambda([this, DefaultSettings]()
					{
						Settings->MonitorImageSize.Y = DefaultSettings->MonitorImageHeight;

						return FReply::Handled();
					})
					.IsEnabled_Lambda([this, DefaultSettings]()
					{
						return Settings->MonitorImageSize.Y != DefaultSettings->MonitorImageHeight;
					})
					.Content()
					[
						SNew(SImage)
						.ColorAndOpacity(ImageColour)
						.Image(FMetaHumanLiveLinkSourceStyle::Get().GetBrush("RestoreSize"))
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0, 5, 0, 0)
				[
					SNew(SButton)
					.ContentPadding(ContentPadding)
					.ToolTipText(LOCTEXT("DecreaseHeightTooltip", "Decrease height of video monitor widget"))
					.OnClicked_Lambda([this]()
					{
						if (Settings->MonitorImageSize.Y >= 200)
						{
							Settings->MonitorImageSize.Y -= 50;
						}

						return FReply::Handled();
					})
					.Content()
					[
						SNew(SImage)
						.ColorAndOpacity(ImageColour)
						.Image(FMetaHumanLiveLinkSourceStyle::Get().GetBrush("DecreaseSize"))
					]

				]
				+ SVerticalBox::Slot()
				.FillHeight(1)
				[
					SNew(SSpacer)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SButton)
					.ContentPadding(ContentPadding)
					.ToolTipText(LOCTEXT("ResetViewTooltip", "Reset video monitor view"))
					.OnClicked_Lambda([this]()
					{
						ImageViewer->ResetView();
						return FReply::Handled();
					})
					.Content()
					[
						SNew(SImage)
						.ColorAndOpacity(ImageColour)
						.Image(FMetaHumanLiveLinkSourceStyle::Get().GetBrush("RestoreView"))
					]
				]
			];
		}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(0, 10, 0, 10)
		.FillHeight(1.0f)
		[
			Box.ToSharedRef()
		]
	];

	Settings->UpdateDelegate.AddSP(this, &SMetaHumanVideoBaseLiveLinkSubjectMonitorWidget::OnUpdate);
	Settings->Subject->SendLatestUpdate();
}

FVector2D SMetaHumanVideoBaseLiveLinkSubjectMonitorWidget::ComputeDesiredSize(float InLayoutScaleMultiplier) const
{
	return Settings->MonitorImageSize;
}

void SMetaHumanVideoBaseLiveLinkSubjectMonitorWidget::AddReferencedObjects(FReferenceCollector& InCollector)
{
	InCollector.AddReferencedObject(ImageTexture);
}

FString SMetaHumanVideoBaseLiveLinkSubjectMonitorWidget::GetReferencerName() const
{
	return TEXT("SMetaHumanVideoLiveLinkSubjectMonitorWidget");
}

void SMetaHumanVideoBaseLiveLinkSubjectMonitorWidget::OnUpdate(TSharedPtr<UE::MetaHuman::Pipeline::FPipelineData> InPipelineData)
{
	check(IsInGameThread());

	const UE::MetaHuman::Pipeline::EPipelineExitStatus ExitStatus = InPipelineData->GetExitStatus();
	if (ExitStatus != UE::MetaHuman::Pipeline::EPipelineExitStatus::Unknown)
	{
		ClearTexture();
	}
	else
	{
		const FString ImagePin = TEXT("RealtimeMonoSolver.Debug UE Image Out"); // Not great these being hardwired
		const FString ConfidencePin = TEXT("RealtimeMonoSolver.Confidence Out");
		const FString StatePin = TEXT("RealtimeMonoSolver.State Out");
		const FString DroppedFramePin = TEXT("MediaPlayer.Dropped Frame Count Out");
		const FString ResPin = TEXT("Rotate.UE Image Out");
		const FString ImageSampleTimePin = TEXT("MediaPlayer.UE Image Sample Time Out");

		const UE::MetaHuman::Pipeline::FUEImageDataType& Image = InPipelineData->GetData<UE::MetaHuman::Pipeline::FUEImageDataType>(ImagePin);
		FillTexture(Image);

		EHyprsenseRealtimeNodeState State = static_cast<EHyprsenseRealtimeNodeState>(InPipelineData->GetData<int32>(StatePin));
		switch (State)
		{
			case EHyprsenseRealtimeNodeState::OK:
				Settings->State = "OK";
				Settings->StateLED = FColor::Green;
				break;

			case EHyprsenseRealtimeNodeState::NoFace:
				Settings->State = "No face detected";
				Settings->StateLED = FColor::Orange;
				break;

			case EHyprsenseRealtimeNodeState::SubjectTooFar:
				Settings->State = "Subject too far from camera";
				Settings->StateLED = FColor::Yellow;
				break;

			default:
				Settings->State = "Unknown";
				Settings->StateLED = FColor::Red;
				break;
		}

		Settings->Confidence = FString::Printf(TEXT("%.1f"), InPipelineData->GetData<float>(ConfidencePin));

		const UE::MetaHuman::Pipeline::FUEImageDataType& Res = InPipelineData->GetData<UE::MetaHuman::Pipeline::FUEImageDataType>(ResPin);
		if (Res.Width > 0 && Res.Height > 0)
		{
			Settings->Resolution = FString::Printf(TEXT("%i x %i"), Res.Width, Res.Height);
		}
		else
		{
			Settings->Resolution = TEXT("Unknown");
		}

		double Now = FPlatformTime::Seconds();

		if (DroppedCount == -1)
		{
			DroppedStart = Now;
			DroppedCount = 0;
		}

		DroppedCount += InPipelineData->GetData<int32>(DroppedFramePin);

		if (Now - DroppedStart > 2)
		{
			Dropped = DroppedCount;
			DroppedCount = -1;
		}

		if (Dropped == -1)
		{
			Settings->Dropping = TEXT("Calculating...");
		}
		else if (Dropped == 0)
		{
			Settings->Dropping = TEXT("No");
		}
		else
		{ 
			Settings->Dropping = FString::Printf(TEXT("Yes (%i frame%s)"), Dropped, Dropped == 1 ? TEXT("") : TEXT("s"));
		}

		FTimecode Timecode = InPipelineData->GetData<FQualifiedFrameTime>(ImageSampleTimePin).ToTimecode();
		Timecode.Subframe = 0; // For the purpose of display, ignore subframe - just looks wrong
		Settings->Timecode = Timecode.ToString();
	}
}

void SMetaHumanVideoBaseLiveLinkSubjectMonitorWidget::FillTexture(const UE::MetaHuman::Pipeline::FUEImageDataType& InImage)
{
	if (InImage.Width > 0 && InImage.Height > 0)
	{
		if (!EditorTimerHandle.IsValid())
		{
#if WITH_EDITOR
			EditorTimerHandle = GEditor->GetTimerManager()->SetTimerForNextTick([this, InImage]()
			{
#endif
				check(IsInGameThread());

				if (!ImageTexture || ImageTexture->GetSizeX() != InImage.Width || ImageTexture->GetSizeY() != InImage.Height)
				{
					ImageTexture = UTexture2D::CreateTransient(InImage.Width, InImage.Height);
					ImageViewerBrush.SetResourceObject(ImageTexture);
					ImageViewerBrush.SetImageSize(FVector2f(ImageTexture->GetSizeX(), ImageTexture->GetSizeY()));
					ImageViewer->ResetView();
				}

				FTexture2DMipMap& Mip0 = ImageTexture->GetPlatformData()->Mips[0];
				if (FColor* TextureData = (FColor*)Mip0.BulkData.Lock(LOCK_READ_WRITE))
				{
					FMemory::Memcpy(TextureData, InImage.Data.GetData(), InImage.Width * InImage.Height * 4);
				}

				Mip0.BulkData.Unlock();
				ImageTexture->UpdateResource();

				EditorTimerHandle.Invalidate();
#if WITH_EDITOR
			});
#endif
		}
	}
	else
	{
		ClearTexture();
	}
}

void SMetaHumanVideoBaseLiveLinkSubjectMonitorWidget::ClearTexture()
{
	UE::MetaHuman::Pipeline::FUEImageDataType Image;

	Image.Width = 256;
	Image.Height = 256;
	Image.Data.SetNumZeroed(Image.Width * Image.Height * 4);

	FillTexture(Image);
}

#undef LOCTEXT_NAMESPACE
