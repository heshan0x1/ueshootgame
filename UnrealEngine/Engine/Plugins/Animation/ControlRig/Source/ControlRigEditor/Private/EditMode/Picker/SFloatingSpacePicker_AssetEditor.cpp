// Copyright Epic Games, Inc. All Rights Reserved.

#include "SFloatingSpacePicker_AssetEditor.h"

#include "ControlRigSpaceChannelEditors.h"
#include "ScopedTransaction.h"
#include "Editor/SRigSpacePickerWidget.h"
#include "Rigs/RigHierarchyController.h"
#include "SControlRigDismissDependencyDialog.h"

#define LOCTEXT_NAMESPACE "SFloatingSpacePicker"

namespace UE::ControlRigEditor
{
namespace DirectEditDetails
{
struct FGuardAbsoluteTime
{
	UControlRig* const ControlRig;
	const float AbsoluteTime;
	
	explicit FGuardAbsoluteTime(UControlRig* InControlRig) : ControlRig(InControlRig), AbsoluteTime(ControlRig->GetAbsoluteTime()) {}
	~FGuardAbsoluteTime(){ ControlRig->SetAbsoluteTime(AbsoluteTime); }
};
}
	
void SFloatingSpacePicker_AssetEditor::Construct(const FArguments& InArgs)
{
	constexpr bool bIsEditingDirectly = true;
	SFloatingSpacePicker_Base::Construct(InArgs, bIsEditingDirectly);
}

void SFloatingSpacePicker_AssetEditor::OnActiveSpaceChanged(
	URigHierarchy* InHierarchy, const FRigElementKey& InControlKey, const FRigElementKey& InSpaceKey
	)
{
	check(SelectedControls.Contains(InControlKey));

	UControlRig* RuntimeRig = WeakDisplayedRig.Get();
	if (!ensure(RuntimeRig))
	{
		return;
	}
	
	if (RuntimeRig->IsAdditive())
	{
		const FTransform Transform = RuntimeRig->GetControlGlobalTransform(InControlKey.Name);
		RuntimeRig->SwitchToParent(InControlKey, InSpaceKey, false, true);
		{
			DirectEditDetails::FGuardAbsoluteTime Guard(RuntimeRig);
			RuntimeRig->Evaluate_AnyThread();
		}
		FRigControlValue ControlValue = RuntimeRig->GetControlValueFromGlobalTransform(InControlKey.Name, Transform, ERigTransformType::CurrentGlobal);
		RuntimeRig->SetControlValue(InControlKey.Name, ControlValue);
		{
			DirectEditDetails::FGuardAbsoluteTime Guard(RuntimeRig);
			RuntimeRig->Evaluate_AnyThread();
		}
	}
	else
	{
		constexpr bool bInitial = false, bAffectChildren = true;
		const FTransform Transform = InHierarchy->GetGlobalTransform(InControlKey);
		FRigDependenciesProviderForControlRig DependenciesProvider(RuntimeRig);
		DependenciesProvider.SetInteractiveDialogEnabled(true);
		FControlRigDismissDependencyDialogGuard DependencyDialogGuard(InHierarchy);

		FString OutFailureReason;
		if (InHierarchy->SwitchToParent(InControlKey, InSpaceKey, bInitial, bAffectChildren, DependenciesProvider, &OutFailureReason))
		{
			InHierarchy->SetGlobalTransform(InControlKey, Transform);
		}
		else
		{
			if( URigHierarchyController* Controller = InHierarchy->GetController())
			{
				static constexpr TCHAR MessageFormat[] = TEXT("Could not switch %s to parent %s: %s");
				Controller->ReportAndNotifyErrorf(MessageFormat,
					*InControlKey.Name.ToString(),
					*InSpaceKey.Name.ToString(),
					*OutFailureReason);
			}
		}
	}
}

void SFloatingSpacePicker_AssetEditor::OnSpaceListChanged(
	URigHierarchy* InHierarchy, const FRigElementKey& InControlKey, const TArray<FRigElementKeyWithLabel>& InSpaceList
	)
{
	check(SelectedControls.Contains(InControlKey));

	UControlRig* RuntimeRig = WeakDisplayedRig.Get();
	if (!ensure(RuntimeRig))
	{
		return;
	}

	FControlRigAssetInterfacePtr Blueprint = RuntimeRig->GetClass()->ClassGeneratedBy;
	URigHierarchy* BlueprintHierarchy = Blueprint ? Blueprint->GetHierarchy() : nullptr;
	if (!BlueprintHierarchy)
	{
		return;
	}
	
	// update the settings in the control element
	if (FRigControlElement* ControlElement = BlueprintHierarchy->Find<FRigControlElement>(InControlKey))
	{
		Blueprint->Modify();
		FScopedTransaction Transaction(LOCTEXT("ControlChangeAvailableSpaces", "Edit Available Spaces"));

		ControlElement->Settings.Customization.AvailableSpaces = InSpaceList;
		BlueprintHierarchy->Notify(ERigHierarchyNotification::ControlSettingChanged, ControlElement);
	}

	// also update the debugged instance
	if (BlueprintHierarchy != InHierarchy)
	{
		if(FRigControlElement* ControlElement = InHierarchy->Find<FRigControlElement>(InControlKey))
		{
			ControlElement->Settings.Customization.AvailableSpaces = InSpaceList;
			InHierarchy->Notify(ERigHierarchyNotification::ControlSettingChanged, ControlElement);
		}
	}
}
}

#undef LOCTEXT_NAMESPACE