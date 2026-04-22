// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMediaFrameworkTimecodeGenlockPanel.h"

#include "DetailLayoutBuilder.h"
#include "IDetailCustomization.h"
#include "SMediaFrameworkTimecodeGenlockHeader.h"
#include "Engine/EngineCustomTimeStep.h"
#include "Engine/TimecodeProvider.h"
#include "Misc/Timecode.h"
#include "Modules/ModuleManager.h"
#include "Profile/MediaProfile.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMediaFrameworkTimecodeGenlockPanel"

/** Details panel customization to hide any categories unrelated to timecode or genlock within the media profile  */
class FMediaProfileTimecodeGenlockDetailsCustomization : public IDetailCustomization
{
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
	{
		DetailBuilder.HideCategory(TEXT("Inputs"));
		DetailBuilder.HideCategory(TEXT("Outputs"));
	}
};

void SMediaFrameworkTimecodeGenlockPanel::Construct(const FArguments& InArgs)
{
	MediaProfile = InArgs._MediaProfile;
	
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsArgs;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsArgs.bCustomNameAreaLocation = true;
	DetailsArgs.bCustomFilterAreaLocation = true;
	DetailsArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Hide;
	DetailsArgs.bShowSectionSelector = true;
	DetailsArgs.NotifyHook = this;
	
	DetailsView = PropertyModule.CreateDetailView(DetailsArgs);

	DetailsView->RegisterInstancedCustomPropertyLayout(UMediaProfile::StaticClass(), FOnGetDetailCustomizationInstance::CreateLambda([this]
	{
		return MakeShared<FMediaProfileTimecodeGenlockDetailsCustomization>();
	}));

	if (MediaProfile.IsValid())
	{
		DetailsView->SetObject(MediaProfile.Get());
	}
	
	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SMediaFrameworkTimecodeGenlockHeader)
		]

		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
			.Orientation(EOrientation::Orient_Horizontal)
			.Thickness(2)
		]
		
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			DetailsView->GetFilterAreaWidget().ToSharedRef()
		]
		
		+SVerticalBox::Slot()
		[
			DetailsView.ToSharedRef()
		]
	];
}

void SMediaFrameworkTimecodeGenlockPanel::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FEditPropertyChain* PropertyThatChanged)
{
	TStrongObjectPtr<UMediaProfile> PinnedMediaProfile = MediaProfile.Pin();
	if (!PinnedMediaProfile.IsValid())
	{
		return;
	}

	FProperty* HeadProperty = PropertyThatChanged->GetHead()->GetValue();
	if (!HeadProperty)
	{
		return;
	}
	
	if (HeadProperty->GetFName() == TEXT("TimecodeProvider") || HeadProperty->GetFName() == TEXT("bOverrideTimecodeProvider"))
	{
		PinnedMediaProfile->ApplyTimecodeProvider();
	}

	if (HeadProperty->GetFName() == TEXT("CustomTimeStep") || HeadProperty->GetFName() == TEXT("bOverrideCustomTimeStep"))
	{
		PinnedMediaProfile->ApplyCustomTimeStep();
	}
}

#undef LOCTEXT_NAMESPACE
