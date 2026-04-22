// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimDetailsProxyBool.h"

#include "AnimDetails/AnimDetailsSettings.h"
#include "ControlRig.h"
#include "DetailLayoutBuilder.h"
#include "MovieSceneCommonHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimDetailsProxyBool)

FName UAnimDetailsProxyBool::GetCategoryName() const
{
	return "Bool";
}

TArray<FName> UAnimDetailsProxyBool::GetPropertyNames() const
{
	return
	{
		GET_MEMBER_NAME_CHECKED(FAnimDetailsBool, Bool)
	};
}

TSet<ERigControlType> UAnimDetailsProxyBool::GetSupportedControlTypes() const
{
	static const TSet<ERigControlType> SupportedControlTypes = { ERigControlType::Bool };
	return SupportedControlTypes;
}

bool UAnimDetailsProxyBool::PropertyIsOnProxy(const FProperty* Property, const FProperty* MemberProperty)
{
	return (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimDetailsBool, Bool)) ||
		(MemberProperty && MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimDetailsProxyBool, Bool));
}

void UAnimDetailsProxyBool::AdoptValues(const ERigControlValueType RigControlValueType)
{
	const bool Value = [RigControlValueType, this]()
		{
			UControlRig* ControlRig = GetControlRig();
			FRigControlElement* ControlElement = GetControlElement();
			if (ControlRig && ControlElement)
			{
				const FRigControlValue ControlValue = ControlRig->GetControlValue(ControlElement, RigControlValueType);
				return ControlValue.Get<bool>();
			}
			else if (SequencerItem.IsValid())
			{
				const TOptional<bool> OptionalValue = SequencerItem.GetBinding()->GetOptionalValue<bool>(*SequencerItem.GetBoundObject());
				return OptionalValue.IsSet() ? OptionalValue.GetValue() : false;
			}

			return false;
		}();

	const FAnimDetailsBool AnimDetailProxyBool(Value);

	constexpr const TCHAR* PropertyName = TEXT("Bool");
	FTrackInstancePropertyBindings Binding(PropertyName, PropertyName);
	Binding.CallFunction<FAnimDetailsBool>(*this, AnimDetailProxyBool);
}

void UAnimDetailsProxyBool::ResetPropertyToDefault(const FName& PropertyName)
{
	if (const FRigControlElement* ControlElement = GetControlElement())
	{
		AdoptValues(ERigControlValueType::Initial);
	}
	else
	{
		Bool = FAnimDetailsBool();
	}
}

bool UAnimDetailsProxyBool::HasDefaultValue(const FName& PropertyName) const
{
	UControlRig* ControlRig = GetControlRig();
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlRig && ControlElement)
	{
		const FRigControlValue ControlValue = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Initial);
		return Bool.Bool == ControlValue.Get<bool>();
	}

	return Bool.Bool == false;
}

EControlRigContextChannelToKey UAnimDetailsProxyBool::GetChannelToKeyFromPropertyName(const FName& PropertyName) const
{
	if (PropertyName == GET_MEMBER_NAME_CHECKED(FAnimDetailsBool, Bool))
	{
		return EControlRigContextChannelToKey::TranslationX;
	}

	return EControlRigContextChannelToKey::AllTransform;
}

EControlRigContextChannelToKey UAnimDetailsProxyBool::GetChannelToKeyFromChannelName(const FString& InChannelName) const
{
	if (InChannelName == TEXT("Bool"))
	{
		return EControlRigContextChannelToKey::TranslationX;
	}

	const UControlRig* ControlRig = GetControlRig();
	const FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement && ControlElement->GetDisplayName() == InChannelName)
	{
		return EControlRigContextChannelToKey::TranslationX;
	}

	return EControlRigContextChannelToKey::AllTransform;
}

void UAnimDetailsProxyBool::SetControlRigElementValueFromCurrent(const FRigControlModifiedContext& Context)
{
	UControlRig* ControlRig = GetControlRig();
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlElement && ControlRig)
	{
		const bool Value = Bool.Bool;

		constexpr bool bNotify = true;
		constexpr bool bSetupUndo = false;
		ControlRig->SetControlValue<bool>(ControlElement->GetKey().Name, Value, bNotify, Context, bSetupUndo);
	}
}

void UAnimDetailsProxyBool::SetSequencerBindingValueFromCurrent(const FRigControlModifiedContext& Context)
{
	const TSharedPtr<FTrackInstancePropertyBindings> Binding = SequencerItem.GetBinding();
	UObject* BoundObject = SequencerItem.GetBoundObject();
	if (Binding.IsValid() && BoundObject)
	{
		Binding->SetCurrentValue<bool>(*BoundObject, Bool.Bool);
	}
}
