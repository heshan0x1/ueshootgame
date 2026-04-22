// Copyright Epic Games, Inc. All Rights Reserved.

#include "SFloatingSpacePicker_Base.h"

#include "ControlRigSpaceChannelEditors.h"
#include "ScopedTransaction.h"
#include "Editor/SRigSpacePickerWidget.h"
#include "Rigs/RigHierarchyController.h"

#define LOCTEXT_NAMESPACE "SFloatingSpacePicker"

namespace UE::ControlRigEditor
{
void SFloatingSpacePicker_Base::Construct(const FArguments& InArgs, bool bIsEditingDirectly)
{
	check(InArgs._DisplayedRig && InArgs._DisplayedRig->GetHierarchy() && !InArgs._SelectedControls.IsEmpty());
	
	WeakDisplayedRig = InArgs._DisplayedRig;
	SelectedControls = InArgs._SelectedControls;
	
	SRigSpacePickerWidget::Construct(
		SRigSpacePickerWidget::FArguments()
		.Hierarchy(InArgs._DisplayedRig->GetHierarchy())
		.Controls(SelectedControls)
		.Title(LOCTEXT("PickSpace", "Pick Space"))
		.AllowDelete(bIsEditingDirectly)
		.AllowReorder(bIsEditingDirectly)
		.AllowAdd(bIsEditingDirectly)
		.GetControlCustomization_Lambda([this](URigHierarchy*, const FRigElementKey& InControlKey)
		{
			const UControlRig* RuntimeRig = WeakDisplayedRig.Get();
			return ensure(RuntimeRig) ? RuntimeRig->GetControlCustomization(InControlKey) : nullptr;
		})
		.OnActiveSpaceChanged(this, &SFloatingSpacePicker_Base::OnActiveSpaceChanged)
		.OnSpaceListChanged(this, &SFloatingSpacePicker_Base::OnSpaceListChanged)
	);
}

TSharedPtr<SWindow> SFloatingSpacePicker_Base::ShowWindow()
{
	constexpr bool bModal = false;
	return OpenDialog(bModal);
}
}

#undef LOCTEXT_NAMESPACE