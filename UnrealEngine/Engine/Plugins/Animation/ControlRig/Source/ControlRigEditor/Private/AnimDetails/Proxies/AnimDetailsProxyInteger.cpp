// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimDetailsProxyInteger.h"

#include "AnimDetails/AnimDetailsSettings.h"
#include "ControlRig.h"
#include "DetailLayoutBuilder.h"
#include "MovieSceneCommonHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimDetailsProxyInteger)

FName UAnimDetailsProxyInteger::GetCategoryName() const
{
	return "Integer";
}

TArray<FName> UAnimDetailsProxyInteger::GetPropertyNames() const
{
	return 
	{ 
		GET_MEMBER_NAME_CHECKED(UAnimDetailsProxyInteger, Integer)
	};
}

TSet<ERigControlType> UAnimDetailsProxyInteger::GetSupportedControlTypes() const
{
	static const TSet<ERigControlType> SupportedControlTypes = { ERigControlType::Integer };
	return SupportedControlTypes;
}

bool UAnimDetailsProxyInteger::PropertyIsOnProxy(const FProperty* Property, const FProperty* MemberProperty)
{
	return (Property && Property->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimDetailsInteger, Integer)) ||
		(MemberProperty && MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UAnimDetailsProxyInteger, Integer));
}

void UAnimDetailsProxyInteger::AdoptValues(const ERigControlValueType RigControlValueType)
{
	const int64 Value = [this, RigControlValueType]() -> int64
		{
			UControlRig* ControlRig = GetControlRig();
			FRigControlElement* ControlElement = GetControlElement();
			if (ControlRig && ControlElement)
			{
				const FRigControlValue ControlValue = ControlRig->GetControlValue(ControlElement, RigControlValueType);
				return ControlValue.Get<int32>();
			}
			else if (SequencerItem.IsValid())
			{
				if (const TOptional<int64> OptionalValue = SequencerItem.GetBinding()->GetOptionalValue<int64>(*SequencerItem.GetBoundObject()))
				{
					if (OptionalValue.IsSet())
					{
						return OptionalValue.GetValue();
					}
				}
			}

			return 0;
		}();

	const FAnimDetailsInteger AnimDetailProxyInteger(Value);

	constexpr const TCHAR* PropertyName = TEXT("Integer");
	FTrackInstancePropertyBindings Binding(PropertyName, PropertyName);
	Binding.CallFunction<FAnimDetailsInteger>(*this, AnimDetailProxyInteger);
}

void UAnimDetailsProxyInteger::ResetPropertyToDefault(const FName& PropertyName)
{
	if (const FRigControlElement* ControlElement = GetControlElement())
	{
		AdoptValues(ERigControlValueType::Initial);
	}
	else
	{
		Integer = FAnimDetailsInteger();
	}
}

bool UAnimDetailsProxyInteger::HasDefaultValue(const FName& PropertyName) const
{
	UControlRig* ControlRig = GetControlRig();
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlRig && ControlElement)
	{
		const FRigControlValue ControlValue = ControlRig->GetControlValue(ControlElement, ERigControlValueType::Initial);
		return Integer.Integer == ControlValue.Get<int32>();
	}

	return Integer.Integer == 0;
}

EControlRigContextChannelToKey UAnimDetailsProxyInteger::GetChannelToKeyFromPropertyName(const FName& PropertyName) const
{
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UAnimDetailsProxyInteger, Integer))
	{
		return EControlRigContextChannelToKey::TranslationX;
	}

	return EControlRigContextChannelToKey::AllTransform;
}

EControlRigContextChannelToKey UAnimDetailsProxyInteger::GetChannelToKeyFromChannelName(const FString& InChannelName) const
{
	if (InChannelName == TEXT("Integer"))
	{
		return EControlRigContextChannelToKey::TranslationX;
	}

	const UControlRig* ControlRig = GetControlRig();
	const FRigElementKey ElementKey = GetControlElementKey();

	const FRigControlElement* ControlElement = ControlRig ? ControlRig->FindControl(ElementKey.Name) : nullptr;
	if (ControlElement && ControlElement->GetDisplayName() == InChannelName)
	{
		return EControlRigContextChannelToKey::TranslationX;
	}

	return EControlRigContextChannelToKey::AllTransform;
}

void UAnimDetailsProxyInteger::SetControlRigElementValueFromCurrent(const FRigControlModifiedContext& Context)
{
	UControlRig* ControlRig = GetControlRig();
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlRig && ControlElement)
	{
		int32 Val = (int32)Integer.Integer;

		constexpr bool bNotify = true;
		constexpr bool bSetupUndo = false;
		ControlRig->SetControlValue<int32>(ControlElement->GetKey().Name, Val, bNotify, Context, bSetupUndo);
	}
}

void UAnimDetailsProxyInteger::SetSequencerBindingValueFromCurrent(const FRigControlModifiedContext& Context)
{
	const TSharedPtr<FTrackInstancePropertyBindings> Binding = SequencerItem.GetBinding();
	UObject* BoundObject = SequencerItem.GetBoundObject();
	if (Binding.IsValid() && BoundObject)
	{
		Binding->SetCurrentValue<int64>(*BoundObject, Integer.Integer);
	}
}