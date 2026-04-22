// Copyright Epic Games, Inc. All Rights Reserved.

#include "STabButton.h"

#include "ControlRigEditorStyle.h"
#include "Widgets/Input/SCheckBox.h"

namespace UE::ControlRigEditor
{
void STabButton::Construct(const FArguments& InArgs)
{
	OnActivatedDelegate = InArgs._OnActivated;
	
	ChildSlot
	[
		SNew(SCheckBox)
		.Style(FControlRigEditorStyle::Get(), TEXT("ControlRig.TabButton"))
		.IsChecked(this, &STabButton::IsChecked)
        .OnCheckStateChanged(this, &STabButton::OnButtonClicked)
		[
			InArgs._ButtonContent.Widget
		]
	];
}

ECheckBoxState STabButton::IsChecked() const
{
	return bIsActive ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void STabButton::OnButtonClicked(ECheckBoxState NewState)
{
	if (!bIsActive)
	{
		bIsActive = true;
		OnActivatedDelegate.ExecuteIfBound();
	}
}
}
