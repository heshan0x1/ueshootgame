// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanMediaSourceWidget.h"
#include "MetaHumanStringCombo.h"
#include "MetaHumanPipelineMediaPlayerNode.h"
#include "MetaHumanLocalLiveLinkSourceBlueprint.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Widgets/Input/SNumericEntryBox.h"

#if WITH_EDITOR
#include "DetailLayoutBuilder.h"
#endif

#define LOCTEXT_NAMESPACE "MetaHumanLocalLiveLinkSource"



class SMetaHumanMediaSourceWidgetImpl : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMetaHumanMediaSourceWidgetImpl) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, SMetaHumanMediaSourceWidget::EMediaType InMediaType);

	void OnAssetsAddedOrDeleted(TConstArrayView<FAssetData> InAssets);
	void OnAssetRenamed(const FAssetData& InAsset, const FString& InOldObjectPath);
	void PopulateDevices();

	SMetaHumanMediaSourceWidget::EMediaType MediaType;

	TMap<FString, FMetaHumanLiveLinkVideoDevice> VideoDevices;
	TMap<FString, FMetaHumanLiveLinkVideoTrack> VideoTracks;
	TMap<FString, FMetaHumanLiveLinkVideoFormat> VideoTrackFormats;

	TArray<SMetaHumanStringCombo::FComboItemType> VideoDeviceItems;
	TArray<SMetaHumanStringCombo::FComboItemType> VideoTrackItems;
	TArray<SMetaHumanStringCombo::FComboItemType> VideoTrackFormatItems;
	bool bVideoTrackFormatItemsFiltered = true;

	TSharedPtr<SMetaHumanStringCombo> VideoDeviceCombo;
	TSharedPtr<SMetaHumanStringCombo> VideoTrackCombo;
	TSharedPtr<SMetaHumanStringCombo> VideoTrackFormatCombo;

	void OnVideoDeviceSelected(SMetaHumanStringCombo::FComboItemType InItem);
	void OnVideoTrackSelected(SMetaHumanStringCombo::FComboItemType InItem);
	void OnVideoTrackFormatSelected(SMetaHumanStringCombo::FComboItemType InItem);

	TMap<FString, FMetaHumanLiveLinkAudioDevice> AudioDevices;
	TMap<FString, FMetaHumanLiveLinkAudioTrack> AudioTracks;
	TMap<FString, FMetaHumanLiveLinkAudioFormat> AudioTrackFormats;

	TArray<SMetaHumanStringCombo::FComboItemType> AudioDeviceItems;
	TArray<SMetaHumanStringCombo::FComboItemType> AudioTrackItems;
	TArray<SMetaHumanStringCombo::FComboItemType> AudioTrackFormatItems;

	TSharedPtr<SMetaHumanStringCombo> AudioDeviceCombo;
	TSharedPtr<SMetaHumanStringCombo> AudioTrackCombo;
	TSharedPtr<SMetaHumanStringCombo> AudioTrackFormatCombo;

	void OnAudioDeviceSelected(SMetaHumanStringCombo::FComboItemType InItem);
	void OnAudioTrackSelected(SMetaHumanStringCombo::FComboItemType InItem);
	void OnAudioTrackFormatSelected(SMetaHumanStringCombo::FComboItemType InItem);

	TSharedPtr<SCheckBox> AdvancedCheckBox;

	TSharedPtr<SCheckBox> FilteredWidget;
	TSharedPtr<SNumericEntryBox<float>> StartTimeoutWidget;
	TSharedPtr<SNumericEntryBox<float>> FormatWaitTimeWidget;
	TSharedPtr<SNumericEntryBox<float>> SampleTimeoutWidget;

	bool CanCreate() const;

	double StartTimeout = 5;
	double FormatWaitTime = 0.1;
	double SampleTimeout = 5;

	bool IsBundle() const;
	EVisibility GetTrackVisibility() const;
	bool IsTrackEnabled() const;
	FText GetTrackTooltip() const;
	EVisibility GetAdvancedVisibility() const;

	FMetaHumanMediaSourceCreateParams GetCreateParams() const;
};







void SMetaHumanMediaSourceWidgetImpl::Construct(const FArguments& InArgs, SMetaHumanMediaSourceWidget::EMediaType InMediaType)
{
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	AssetRegistryModule.Get().OnAssetsAdded().AddSP(this, &SMetaHumanMediaSourceWidgetImpl::OnAssetsAddedOrDeleted);
	AssetRegistryModule.Get().OnAssetsRemoved().AddSP(this, &SMetaHumanMediaSourceWidgetImpl::OnAssetsAddedOrDeleted);
	AssetRegistryModule.Get().OnAssetRenamed().AddSP(this, &SMetaHumanMediaSourceWidgetImpl::OnAssetRenamed);

	MediaType = InMediaType;

	VideoDeviceCombo = SNew(SMetaHumanStringCombo, &VideoDeviceItems)
					   .OnItemSelected(this, &SMetaHumanMediaSourceWidgetImpl::OnVideoDeviceSelected);

	VideoTrackCombo = SNew(SMetaHumanStringCombo, &VideoTrackItems)
					  .Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility)
					  .IsEnabled(this, &SMetaHumanMediaSourceWidgetImpl::IsTrackEnabled)
					  .ToolTipText(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackTooltip)
					  .OnItemSelected(this, &SMetaHumanMediaSourceWidgetImpl::OnVideoTrackSelected);

	VideoTrackFormatCombo = SNew(SMetaHumanStringCombo, &VideoTrackFormatItems)
							.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility)
							.IsEnabled(this, &SMetaHumanMediaSourceWidgetImpl::IsTrackEnabled)
							.ToolTipText(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackTooltip)
							.OnItemSelected(this, &SMetaHumanMediaSourceWidgetImpl::OnVideoTrackFormatSelected);

	AudioDeviceCombo = SNew(SMetaHumanStringCombo, &AudioDeviceItems)
					   .OnItemSelected(this, &SMetaHumanMediaSourceWidgetImpl::OnAudioDeviceSelected);

	AudioTrackCombo = SNew(SMetaHumanStringCombo, &AudioTrackItems)
					  .Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility)
					  .IsEnabled(this, &SMetaHumanMediaSourceWidgetImpl::IsTrackEnabled)
					  .ToolTipText(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackTooltip)
					  .OnItemSelected(this, &SMetaHumanMediaSourceWidgetImpl::OnAudioTrackSelected);

	AudioTrackFormatCombo = SNew(SMetaHumanStringCombo, &AudioTrackFormatItems)
						    .Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility)
							.IsEnabled(this, &SMetaHumanMediaSourceWidgetImpl::IsTrackEnabled)
							.ToolTipText(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackTooltip)
							.OnItemSelected(this, &SMetaHumanMediaSourceWidgetImpl::OnAudioTrackFormatSelected);

	AdvancedCheckBox = SNew(SCheckBox);

	FilteredWidget = SNew(SCheckBox)
		.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility)
		.ToolTipText(LOCTEXT("FilteredTooltip", "Filter the formats to show only the most relevant ones"))
		.IsChecked_Lambda([this]()
		{
			return bVideoTrackFormatItemsFiltered ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this](ECheckBoxState InState)
		{
			bVideoTrackFormatItemsFiltered = (InState == ECheckBoxState::Checked);

			OnVideoTrackSelected(VideoTrackCombo->CurrentItem);
		});

	StartTimeoutWidget = SNew(SNumericEntryBox<float>)
		.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility)
#if WITH_EDITOR
		.Font(IDetailLayoutBuilder::GetDetailFont())
#endif
		.ToolTipText(LOCTEXT("StartTimeoutTooltip", "Timeout for waiting for media to open"))
		.Value_Lambda([this]()
		{
			return StartTimeout;
		})
		.OnValueCommitted_Lambda([this](float InValue, ETextCommit::Type)
		{
			StartTimeout = InValue;
		});

	FormatWaitTimeWidget = SNew(SNumericEntryBox<float>)
		.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility)
#if WITH_EDITOR
		.Font(IDetailLayoutBuilder::GetDetailFont())
#endif
		.ToolTipText(LOCTEXT("FormatWaitTimeTooltip", "Time to wait for format changes to take effect"))
		.Value_Lambda([this]()
		{
			return FormatWaitTime;
		})
		.OnValueCommitted_Lambda([this](float InValue, ETextCommit::Type)
		{
			FormatWaitTime = InValue;
		});

	SampleTimeoutWidget = SNew(SNumericEntryBox<float>)
		.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility)
#if WITH_EDITOR
		.Font(IDetailLayoutBuilder::GetDetailFont())
#endif
		.ToolTipText(LOCTEXT("SampleTimeoutTooltip", "Timeout for waiting on first media sample to arrive"))
		.Value_Lambda([this]()
		{
			return SampleTimeout;
		})
		.OnValueCommitted_Lambda([this](float InValue, ETextCommit::Type)
		{
			SampleTimeout = InValue;
		});

	PopulateDevices();

	const float Padding = 5;
	const float FirstColWidth = 140;

	TSharedPtr<SVerticalBox> Layout = SNew(SVerticalBox);

	if (MediaType == SMetaHumanMediaSourceWidget::EMediaType::Video || MediaType == SMetaHumanMediaSourceWidget::EMediaType::VideoAndAudio)
	{
		Layout->AddSlot()
			.AutoHeight()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.Padding(Padding)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("VideoDevice", "Video Device"))
						.MinDesiredWidth(FirstColWidth)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						VideoDeviceCombo.ToSharedRef()
					]
				]
				+ SVerticalBox::Slot()
				.Padding(Padding)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("VideoTrack", "Video Track"))
						.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility)
						.MinDesiredWidth(FirstColWidth)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						VideoTrackCombo.ToSharedRef()
					]
				]
				+ SVerticalBox::Slot()
				.Padding(Padding)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("VideoTrackFormat", "Video Track Format"))
						.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility)
						.MinDesiredWidth(FirstColWidth)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						VideoTrackFormatCombo.ToSharedRef()
					]
				]
			];
	}

	if (MediaType == SMetaHumanMediaSourceWidget::EMediaType::Audio || MediaType == SMetaHumanMediaSourceWidget::EMediaType::VideoAndAudio)
	{
		Layout->AddSlot()
			.AutoHeight()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.Padding(Padding)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AudioDevice", "Audio Device"))
						.MinDesiredWidth(FirstColWidth)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						AudioDeviceCombo.ToSharedRef()
					]
				]
				+ SVerticalBox::Slot()
				.Padding(Padding)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AudioTrack", "Audio Track"))
						.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility)
						.MinDesiredWidth(FirstColWidth)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						AudioTrackCombo.ToSharedRef()
					]
				]
				+ SVerticalBox::Slot()
				.Padding(Padding)
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("AudioTrackFormat", "Audio Track Format"))
						.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility)
						.MinDesiredWidth(FirstColWidth)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						AudioTrackFormatCombo.ToSharedRef()
					]
				]
			];
	}

	Layout->AddSlot()
		.AutoHeight()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(Padding)
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Advanced", "Advanced"))
					.MinDesiredWidth(FirstColWidth)
				]
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Top)
				.AutoWidth()
				[
					AdvancedCheckBox.ToSharedRef()
				]
				+ SHorizontalBox::Slot()
				.Padding(Padding * 6, 0)
				.AutoWidth()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.Padding(Padding)
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("Filtered", "Filter Format List"))
							.ToolTipText(LOCTEXT("FilteredTooltip", "Filter the formats to show only the most relevant ones"))
							.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility)
							.MinDesiredWidth(FirstColWidth)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							FilteredWidget.ToSharedRef()
						]
					]
					+ SVerticalBox::Slot()
					.Padding(Padding)
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("StartTimeout", "Start Timeout"))
							.ToolTipText(LOCTEXT("StartTimeoutTooltip", "Timeout for waiting for media to open"))
							.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility)
							.MinDesiredWidth(FirstColWidth)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							StartTimeoutWidget.ToSharedRef()
						]
					]
					+ SVerticalBox::Slot()
					.Padding(Padding)
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("FormatWaitTime", "Format Wait Time"))
							.ToolTipText(LOCTEXT("FormatWaitTimeTooltip", "Time to wait for format changes to take effect"))
							.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility)
							.MinDesiredWidth(FirstColWidth)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							FormatWaitTimeWidget.ToSharedRef()
						]
					]
					+ SVerticalBox::Slot()
					.Padding(Padding)
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("SampleTimeout", "Sample Timeout"))
							.ToolTipText(LOCTEXT("SampleTimeoutTooltip", "Timeout for waiting on first media sample to arrive"))
							.Visibility(this, &SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility)
							.MinDesiredWidth(FirstColWidth)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SampleTimeoutWidget.ToSharedRef()
						]
					]
				]
			]
		];

	ChildSlot
	[
		Layout.ToSharedRef()
	];
}

void SMetaHumanMediaSourceWidgetImpl::OnAssetsAddedOrDeleted(TConstArrayView<FAssetData> InAssets)
{
	PopulateDevices();
}

void SMetaHumanMediaSourceWidgetImpl::OnAssetRenamed(const FAssetData& InAsset, const FString& InOldObjectPath)
{
	PopulateDevices();
}

void SMetaHumanMediaSourceWidgetImpl::PopulateDevices()
{
	VideoDevices.Reset();
	VideoDeviceItems.Reset();

	TArray<FMetaHumanLiveLinkVideoDevice> VideoDevicesArray;
	UMetaHumanLocalLiveLinkSourceBlueprint::GetVideoDevices(VideoDevicesArray);

	for (const FMetaHumanLiveLinkVideoDevice& VideoDevice : VideoDevicesArray)
	{
		VideoDevices.Add(VideoDevice.Url, VideoDevice);
		VideoDeviceItems.Add(MakeShared<TPair<FString, FString>>(VideoDevice.Name, VideoDevice.Url));
	}

	VideoDeviceCombo->RefreshOptions();

	OnVideoDeviceSelected(VideoDeviceItems.IsEmpty() ? nullptr : VideoDeviceItems[0]);


	AudioDevices.Reset();
	AudioDeviceItems.Reset();

	TArray<FMetaHumanLiveLinkAudioDevice> AudioDevicesArray;
	UMetaHumanLocalLiveLinkSourceBlueprint::GetAudioDevices(AudioDevicesArray);

	for (const FMetaHumanLiveLinkAudioDevice& AudioDevice : AudioDevicesArray)
	{
		AudioDevices.Add(AudioDevice.Url, AudioDevice);
		AudioDeviceItems.Add(MakeShared<TPair<FString, FString>>(AudioDevice.Name, AudioDevice.Url));
	}

	AudioDeviceCombo->RefreshOptions();

	OnAudioDeviceSelected(AudioDeviceItems.IsEmpty() ? nullptr : AudioDeviceItems[0]);
}

void SMetaHumanMediaSourceWidgetImpl::OnVideoDeviceSelected(SMetaHumanStringCombo::FComboItemType InItem)
{
	VideoDeviceCombo->CurrentItem = InItem;

	VideoTracks.Reset();
	VideoTrackItems.Reset();

	if (InItem)
	{
		TArray<FMetaHumanLiveLinkVideoTrack> VideoTracksArray;
		bool bTimedOut = false;
		UMetaHumanLocalLiveLinkSourceBlueprint::GetVideoTracks(VideoDevices[InItem->Value], VideoTracksArray, bTimedOut);

		for (const FMetaHumanLiveLinkVideoTrack& VideoTrack : VideoTracksArray)
		{
			VideoTracks.Add(FString::FromInt(VideoTrack.Index), VideoTrack);
			VideoTrackItems.Add(MakeShared<TPair<FString, FString>>(VideoTrack.Name, FString::FromInt(VideoTrack.Index)));
		}
	}

	VideoTrackCombo->RefreshOptions();

	if (VideoTrackItems.IsEmpty())
	{
		OnVideoTrackSelected(nullptr);
	}
	else
	{
		for (int32 VideoTrack = 0; VideoTrack < VideoTrackItems.Num(); ++VideoTrack)
		{
			OnVideoTrackSelected(VideoTrackItems[VideoTrack]);

			if (!VideoTrackFormatItems.IsEmpty())
			{
				break;
			}
		}
	}
}

void SMetaHumanMediaSourceWidgetImpl::OnVideoTrackSelected(SMetaHumanStringCombo::FComboItemType InItem)
{
	VideoTrackCombo->CurrentItem = InItem;

	VideoTrackFormats.Reset();
	VideoTrackFormatItems.Reset();

	if (InItem)
	{
		TArray<FMetaHumanLiveLinkVideoFormat> VideoFormatsArray;
		bool bTimedOut = false;
		UMetaHumanLocalLiveLinkSourceBlueprint::GetVideoFormats(VideoTracks[InItem->Value], VideoFormatsArray, bTimedOut, bVideoTrackFormatItemsFiltered);

		for (const FMetaHumanLiveLinkVideoFormat& VideoFormat : VideoFormatsArray)
		{
			VideoTrackFormats.Add(FString::FromInt(VideoFormat.Index), VideoFormat);
			VideoTrackFormatItems.Add(MakeShared<TPair<FString, FString>>(VideoFormat.Name, FString::FromInt(VideoFormat.Index)));
		}
	}

	VideoTrackFormatCombo->RefreshOptions();

	OnVideoTrackFormatSelected(VideoTrackFormatItems.IsEmpty() ? nullptr : VideoTrackFormatItems[0]);
}

void SMetaHumanMediaSourceWidgetImpl::OnVideoTrackFormatSelected(SMetaHumanStringCombo::FComboItemType InItem)
{
	VideoTrackFormatCombo->CurrentItem = InItem;
}

void SMetaHumanMediaSourceWidgetImpl::OnAudioDeviceSelected(SMetaHumanStringCombo::FComboItemType InItem)
{
	AudioDeviceCombo->CurrentItem = InItem;

	AudioTracks.Reset();
	AudioTrackItems.Reset();

	if (InItem)
	{
		TArray<FMetaHumanLiveLinkAudioTrack> AudioTracksArray;
		bool bTimedOut = false;
		UMetaHumanLocalLiveLinkSourceBlueprint::GetAudioTracks(AudioDevices[InItem->Value], AudioTracksArray, bTimedOut);

		for (const FMetaHumanLiveLinkAudioTrack& AudioTrack : AudioTracksArray)
		{
			AudioTracks.Add(FString::FromInt(AudioTrack.Index), AudioTrack);
			AudioTrackItems.Add(MakeShared<TPair<FString, FString>>(AudioTrack.Name, FString::FromInt(AudioTrack.Index)));
		}
	}

	AudioTrackCombo->RefreshOptions();

	OnAudioTrackSelected(AudioTrackItems.IsEmpty() ? nullptr : AudioTrackItems[0]);
}

void SMetaHumanMediaSourceWidgetImpl::OnAudioTrackSelected(SMetaHumanStringCombo::FComboItemType InItem)
{
	AudioTrackCombo->CurrentItem = InItem;
	
	AudioTrackFormats.Reset();
	AudioTrackFormatItems.Reset();

	if (InItem)
	{
		TArray<FMetaHumanLiveLinkAudioFormat> AudioFormatsArray;
		bool bTimedOut = false;
		UMetaHumanLocalLiveLinkSourceBlueprint::GetAudioFormats(AudioTracks[InItem->Value], AudioFormatsArray, bTimedOut);

		for (const FMetaHumanLiveLinkAudioFormat& AudioFormat : AudioFormatsArray)
		{
			AudioTrackFormats.Add(FString::FromInt(AudioFormat.Index), AudioFormat);
			AudioTrackFormatItems.Add(MakeShared<TPair<FString, FString>>(AudioFormat.Name, FString::FromInt(AudioFormat.Index)));
		}
	}

	AudioTrackFormatCombo->RefreshOptions();

	OnAudioTrackFormatSelected(AudioTrackFormatItems.IsEmpty() ? nullptr : AudioTrackFormatItems[0]);
}

void SMetaHumanMediaSourceWidgetImpl::OnAudioTrackFormatSelected(SMetaHumanStringCombo::FComboItemType InItem)
{
	AudioTrackFormatCombo->CurrentItem = InItem;
}

bool SMetaHumanMediaSourceWidgetImpl::CanCreate() const
{
	if (MediaType == SMetaHumanMediaSourceWidget::EMediaType::Video || MediaType == SMetaHumanMediaSourceWidget::EMediaType::VideoAndAudio)
	{
		return VideoDeviceCombo->CurrentItem.IsValid() && (IsBundle() || (VideoTrackCombo->CurrentItem.IsValid() && VideoTrackFormatCombo->CurrentItem.IsValid()));
	}
	else
	{
		return AudioDeviceCombo->CurrentItem.IsValid() && (IsBundle() || (AudioTrackCombo->CurrentItem.IsValid() && AudioTrackFormatCombo->CurrentItem.IsValid()));
	}
}

bool SMetaHumanMediaSourceWidgetImpl::IsBundle() const
{
	SMetaHumanStringCombo::FComboItemType CurrentItem;
	
	if (MediaType == SMetaHumanMediaSourceWidget::EMediaType::Video || MediaType == SMetaHumanMediaSourceWidget::EMediaType::VideoAndAudio)
	{
		CurrentItem = VideoDeviceCombo->CurrentItem;
	}
	else
	{
		CurrentItem = AudioDeviceCombo->CurrentItem;
	}

	return CurrentItem.IsValid() ? CurrentItem->Value.StartsWith(UE::MetaHuman::Pipeline::FMediaPlayerNode::BundleURL) : false;
}

EVisibility SMetaHumanMediaSourceWidgetImpl::GetTrackVisibility() const
{
	return EVisibility::Visible;
}

EVisibility SMetaHumanMediaSourceWidgetImpl::GetAdvancedVisibility() const
{
	return EVisibility::Visible;
}

bool SMetaHumanMediaSourceWidgetImpl::IsTrackEnabled() const 
{ 
	return !IsBundle(); 
}

FText SMetaHumanMediaSourceWidgetImpl::GetTrackTooltip() const 
{ 
	return IsBundle() ? LOCTEXT("DisabledBundle", "Disabled for Media Bundles") : FText();
}

FMetaHumanMediaSourceCreateParams SMetaHumanMediaSourceWidgetImpl::GetCreateParams() const
{
	FMetaHumanMediaSourceCreateParams CreateParams;

	CreateParams.VideoName = VideoDeviceCombo->CurrentItem.IsValid() ? VideoDeviceCombo->CurrentItem->Key : TEXT("");
	CreateParams.VideoURL = VideoDeviceCombo->CurrentItem.IsValid() ? VideoDeviceCombo->CurrentItem->Value : TEXT("");
	CreateParams.VideoTrack = VideoTrackCombo->CurrentItem.IsValid() ? FCString::Atoi(*VideoTrackCombo->CurrentItem->Value) : -1;
	CreateParams.VideoTrackFormat = VideoTrackFormatCombo->CurrentItem.IsValid() ? FCString::Atoi(*VideoTrackFormatCombo->CurrentItem->Value) : -1;
	CreateParams.VideoTrackFormatName = VideoTrackFormatCombo->CurrentItem.IsValid() ? VideoTrackFormatCombo->CurrentItem->Key : TEXT("");

	CreateParams.AudioName = AudioDeviceCombo->CurrentItem.IsValid() ? AudioDeviceCombo->CurrentItem->Key : TEXT("");
	CreateParams.AudioURL = AudioDeviceCombo->CurrentItem.IsValid() ? AudioDeviceCombo->CurrentItem->Value : TEXT("");
	CreateParams.AudioTrack = AudioTrackCombo->CurrentItem.IsValid() ? FCString::Atoi(*AudioTrackCombo->CurrentItem->Value) : -1;
	CreateParams.AudioTrackFormat = AudioTrackFormatCombo->CurrentItem.IsValid() ? FCString::Atoi(*AudioTrackFormatCombo->CurrentItem->Value) : -1;
	CreateParams.AudioTrackFormatName = AudioTrackFormatCombo->CurrentItem.IsValid() ? AudioTrackFormatCombo->CurrentItem->Key : TEXT("");

	CreateParams.StartTimeout = StartTimeout;
	CreateParams.FormatWaitTime = FormatWaitTime;
	CreateParams.SampleTimeout = SampleTimeout;

	return CreateParams;
}



void SMetaHumanMediaSourceWidget::Construct(const FArguments& InArgs, EMediaType InMediaType)
{
	Impl = SNew(SMetaHumanMediaSourceWidgetImpl, InMediaType);

	ChildSlot
	[
		Impl.ToSharedRef()
	];
}

bool SMetaHumanMediaSourceWidget::CanCreate() const
{
	return Impl->CanCreate();
}

FMetaHumanMediaSourceCreateParams SMetaHumanMediaSourceWidget::GetCreateParams() const
{
	return Impl->GetCreateParams();
}

TSharedPtr<SWidget> SMetaHumanMediaSourceWidget::GetWidget(EWidgetType InWidgetType) const
{
	TSharedPtr<SWidget> Widget;

	switch (InWidgetType)
	{
		case EWidgetType::VideoDevice:
			Widget = Impl->VideoDeviceCombo;
			break;

		case EWidgetType::VideoTrack:
			Widget = Impl->VideoTrackCombo;
			break;

		case EWidgetType::VideoTrackFormat:
			Widget = Impl->VideoTrackFormatCombo;
			break;

		case EWidgetType::AudioDevice:
			Widget = Impl->AudioDeviceCombo;
			break;

		case EWidgetType::AudioTrack:
			Widget = Impl->AudioTrackCombo;
			break;

		case EWidgetType::AudioTrackFormat:
			Widget = Impl->AudioTrackFormatCombo;
			break;

		case EWidgetType::Filtered:
			Widget = Impl->FilteredWidget;
			break;

		case EWidgetType::StartTimeout:
			Widget = Impl->StartTimeoutWidget;
			break;

		case EWidgetType::FormatWaitTime:
			Widget = Impl->FormatWaitTimeWidget;
			break;

		case EWidgetType::SampleTimeout:
			Widget = Impl->SampleTimeoutWidget;
			break;

		default:
			check(false);
			break;
	}

	return Widget;
}

void SMetaHumanMediaSourceWidget::Repopulate()
{
	Impl->PopulateDevices();
}

#undef LOCTEXT_NAMESPACE
