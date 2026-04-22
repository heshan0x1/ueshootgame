// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanAudioBaseLiveLinkSubjectCustomization.h"
#include "MetaHumanAudioBaseLiveLinkSubjectSettings.h"
#include "MetaHumanAudioBaseLiveLinkSubjectMonitorWidget.h"
#include "MetaHumanLocalLiveLinkSubjectMonitorWidget.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"

#define LOCTEXT_NAMESPACE "MetaHumanAudioBaseLiveLinkSource"



TSharedRef<IDetailCustomization> FMetaHumanAudioBaseLiveLinkSubjectCustomization::MakeInstance()
{
	return MakeShared<FMetaHumanAudioBaseLiveLinkSubjectCustomization>();
}

void FMetaHumanAudioBaseLiveLinkSubjectCustomization::CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	InDetailBuilder.GetObjectsBeingCustomized(Objects);

	check(Objects.Num() == 1);
	UMetaHumanAudioBaseLiveLinkSubjectSettings* Settings = Cast<UMetaHumanAudioBaseLiveLinkSubjectSettings>(Objects[0]);

	if (!Settings->bIsLiveProcessing)
	{
		return;
	}

	IDetailCategoryBuilder& MonitorCategory = InDetailBuilder.EditCategory("Audio", LOCTEXT("Audio", "Audio"), ECategoryPriority::Important);

	TSharedPtr<SMetaHumanLocalLiveLinkSubjectMonitorWidget> LocalLiveLinkSubjectMonitorWidget = SNew(SMetaHumanLocalLiveLinkSubjectMonitorWidget, Settings);

	TSharedRef<IPropertyHandle> LevelProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanAudioBaseLiveLinkSubjectSettings, Level));
	IDetailPropertyRow* LevelRow = InDetailBuilder.EditDefaultProperty(LevelProperty);
	check(LevelRow);

	TSharedPtr<SWidget> NameWidget, ValueWidget;
	LevelRow->GetDefaultWidgets(NameWidget, ValueWidget, false);

	LevelRow->CustomWidget()
		.NameContent()
		[
			NameWidget.ToSharedRef()
		]
		.ValueContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SMetaHumanAudioBaseLiveLinkSubjectMonitorWidget, Settings)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				LocalLiveLinkSubjectMonitorWidget.ToSharedRef()
			]
		];

	TSharedRef<IPropertyHandle> RealtimeAudioMoodProperty = InDetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanAudioBaseLiveLinkSubjectSettings, Mood));
	IDetailPropertyRow* RealtimeAudioMoodRow = InDetailBuilder.EditDefaultProperty(RealtimeAudioMoodProperty);
	check(RealtimeAudioMoodRow);

	RealtimeAudioMoodRow->GetDefaultWidgets(NameWidget, ValueWidget);

	RealtimeAudioMoodRow->CustomWidget()
		.NameContent()
		[
			NameWidget.ToSharedRef()
		]
		.ValueContent()
		[
			SNew(SAudioDrivenAnimationMood, false, RealtimeAudioMoodProperty)
		];

	// Hide the unused calibration, smoothing and head translation
	IDetailCategoryBuilder& ControlsCategory = InDetailBuilder.EditCategory("Controls", LOCTEXT("Controls", "Controls"));
	ControlsCategory.SetCategoryVisibility(false);

	// Ideally we would like the properties that control the audio solve, like mood selection, to be in the "Controls"
	// category to match the video case. However, we cant do that directly since the "Controls" category is hidden by the line above.
	// The workaround is to define the properties that control the audio solve to be in the "AudioControls" category and rename
	// the display name of that to be just "Controls".
	InDetailBuilder.EditCategory("AudioControls", LOCTEXT("Controls", "Controls"));
}

#undef LOCTEXT_NAMESPACE
