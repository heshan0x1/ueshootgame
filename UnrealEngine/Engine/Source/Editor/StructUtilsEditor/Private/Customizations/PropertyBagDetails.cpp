// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyBagDetails.h"

#include "StructUtilsEditorUtils.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "PropertyBagDragDropHandler.h"
#include "ScopedTransaction.h"
#include "SDraggableBox.h" // Custom widget for drag and drop sections
#include "StructUtilsMetadata.h"
#include "STypeSelector.h" // Custom widget for pill type selector
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Templates/ValueOrError.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"

#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PropertyBagDetails)

#define LOCTEXT_NAMESPACE "StructUtilsEditor"

////////////////////////////////////

namespace UE::StructUtils
{
/** Sets property descriptor based on a Blueprint pin type. */
void SetPropertyDescFromPin(FPropertyBagPropertyDesc& Desc, const FEdGraphPinType& PinType)
{
	const UPropertyBagSchema* Schema = GetDefault<UPropertyBagSchema>();
	check(Schema);

	// remove any existing containers
	Desc.ContainerTypes.Reset();

	// Fill Container types, if any
	switch (PinType.ContainerType)
	{
	case EPinContainerType::Array:
		Desc.ContainerTypes.Add(EPropertyBagContainerType::Array);
		break;
	case EPinContainerType::Set:
		Desc.ContainerTypes.Add(EPropertyBagContainerType::Set);
		break;
	case EPinContainerType::Map:
		ensureMsgf(false, TEXT("Unsuported container type [Map] "));
		break;
	default:
		break;
	}

	// Value type
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
	{
		Desc.ValueType = EPropertyBagPropertyType::Bool;
		Desc.ValueTypeObject = nullptr;
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
	{
		if (UEnum* Enum = Cast<UEnum>(PinType.PinSubCategoryObject))
		{
			Desc.ValueType = EPropertyBagPropertyType::Enum;
			Desc.ValueTypeObject = PinType.PinSubCategoryObject.Get();
		}
		else
		{
			Desc.ValueType = EPropertyBagPropertyType::Byte;
			Desc.ValueTypeObject = nullptr;
		}
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int)
	{
		Desc.ValueType = EPropertyBagPropertyType::Int32;
		Desc.ValueTypeObject = nullptr;
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int64)
	{
		Desc.ValueType = EPropertyBagPropertyType::Int64;
		Desc.ValueTypeObject = nullptr;
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
	{
		if (PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float)
		{
			Desc.ValueType = EPropertyBagPropertyType::Float;
			Desc.ValueTypeObject = nullptr;
		}
		else if (PinType.PinSubCategory == UEdGraphSchema_K2::PC_Double)
		{
			Desc.ValueType = EPropertyBagPropertyType::Double;
			Desc.ValueTypeObject = nullptr;
		}
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Name)
	{
		Desc.ValueType = EPropertyBagPropertyType::Name;
		Desc.ValueTypeObject = nullptr;
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_String)
	{
		Desc.ValueType = EPropertyBagPropertyType::String;
		Desc.ValueTypeObject = nullptr;
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
	{
		Desc.ValueType = EPropertyBagPropertyType::Text;
		Desc.ValueTypeObject = nullptr;
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Enum)
	{
		Desc.ValueType = EPropertyBagPropertyType::Enum;
		Desc.ValueTypeObject = PinType.PinSubCategoryObject.Get();
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		Desc.ValueType = EPropertyBagPropertyType::Struct;
		Desc.ValueTypeObject = PinType.PinSubCategoryObject.Get();
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		Desc.ValueType = EPropertyBagPropertyType::Object;
		Desc.ValueTypeObject = PinType.PinSubCategoryObject.Get();
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject)
	{
		Desc.ValueType = EPropertyBagPropertyType::SoftObject;
		Desc.ValueTypeObject = PinType.PinSubCategoryObject.Get();
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_Class)
	{
		Desc.ValueType = EPropertyBagPropertyType::Class;
		Desc.ValueTypeObject = PinType.PinSubCategoryObject.Get();
	}
	else if (PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
	{
		Desc.ValueType = EPropertyBagPropertyType::SoftClass;
		Desc.ValueTypeObject = PinType.PinSubCategoryObject.Get();
	}
	else
	{
		ensureMsgf(false, TEXT("Unhandled pin category %s"), *PinType.PinCategory.ToString());
	}
}

/** @return Blueprint pin type from property descriptor. */
FEdGraphPinType GetPropertyDescAsPin(const FPropertyBagPropertyDesc& Desc)
{
	UEnum* PropertyTypeEnum = StaticEnum<EPropertyBagPropertyType>();
	check(PropertyTypeEnum);
	const UPropertyBagSchema* Schema = GetDefault<UPropertyBagSchema>();
	check(Schema);

	FEdGraphPinType PinType;
	PinType.PinSubCategory = NAME_None;

	// Container type
	//@todo: Handle nested containers in property selection.
	const EPropertyBagContainerType ContainerType = Desc.ContainerTypes.GetFirstContainerType();
	switch (ContainerType)
	{
	case EPropertyBagContainerType::Array:
		PinType.ContainerType = EPinContainerType::Array;
		break;
	case EPropertyBagContainerType::Set:
		PinType.ContainerType = EPinContainerType::Set;
		break;
	default:
		PinType.ContainerType = EPinContainerType::None;
	}

	// Value type
	switch (Desc.ValueType)
	{
	case EPropertyBagPropertyType::Bool:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		break;
	case EPropertyBagPropertyType::Byte:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		break;
	case EPropertyBagPropertyType::Int32:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		break;
	case EPropertyBagPropertyType::Int64:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
		break;
	case EPropertyBagPropertyType::Float:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		break;
	case EPropertyBagPropertyType::Double:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
		break;
	case EPropertyBagPropertyType::Name:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		break;
	case EPropertyBagPropertyType::String:
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
		break;
	case EPropertyBagPropertyType::Text:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
		break;
	case EPropertyBagPropertyType::Enum:
		// @todo: some pin coloring is not correct due to this (byte-as-enum vs enum). 
		PinType.PinCategory = UEdGraphSchema_K2::PC_Enum;
		PinType.PinSubCategoryObject = const_cast<UObject*>(Desc.ValueTypeObject.Get());
		break;
	case EPropertyBagPropertyType::Struct:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = const_cast<UObject*>(Desc.ValueTypeObject.Get());
		break;
	case EPropertyBagPropertyType::Object:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = const_cast<UObject*>(Desc.ValueTypeObject.Get());
		break;
	case EPropertyBagPropertyType::SoftObject:
		PinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
		PinType.PinSubCategoryObject = const_cast<UObject*>(Desc.ValueTypeObject.Get());
		break;
	case EPropertyBagPropertyType::Class:
		PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
		PinType.PinSubCategoryObject = const_cast<UObject*>(Desc.ValueTypeObject.Get());
		break;
	case EPropertyBagPropertyType::SoftClass:
		PinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
		PinType.PinSubCategoryObject = const_cast<UObject*>(Desc.ValueTypeObject.Get());
		break;
	case EPropertyBagPropertyType::UInt32:	// Warning : Type only partially supported (Blueprint does not support unsigned type)
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
		break;
	case EPropertyBagPropertyType::UInt64:	// Warning : Type only partially supported (Blueprint does not support unsigned type)
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
		break;
	default:
		ensureMsgf(false, TEXT("Unhandled value type %s"), *UEnum::GetValueAsString(Desc.ValueType));
		break;
	}

	return PinType;
}

namespace Private
{
/** @return true if the property is one of the known missing types. */
bool HasMissingType(const TSharedPtr<IPropertyHandle>& PropertyHandle)
{
	if (!PropertyHandle)
	{
		return false;
	}

	// Handles Struct
	if (FStructProperty* StructProperty = CastField<FStructProperty>(PropertyHandle->GetProperty()))
	{
		return StructProperty->Struct == FPropertyBagMissingStruct::StaticStruct();
	}
	// Handles Object, SoftObject, Class, SoftClass.
	if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(PropertyHandle->GetProperty()))
	{
		return ObjectProperty->PropertyClass == UPropertyBagMissingObject::StaticClass();
	}
	// Handles Enum
	if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(PropertyHandle->GetProperty()))
	{
		return EnumProperty->GetEnum() == StaticEnum<EPropertyBagMissingEnum>();
	}

	return false;
}

/** @return property bag struct common to all edited properties. */
const UPropertyBag* GetCommonBagStruct(TSharedPtr<IPropertyHandle> StructProperty)
{
	const UPropertyBag* CommonBagStruct = nullptr;

	if (ensure(IsScriptStruct<FInstancedPropertyBag>(StructProperty)))
	{
		StructProperty->EnumerateConstRawData([&CommonBagStruct](const void* RawData, const int32 /*DataIndex*/, const int32 /*NumDatas*/)
		{
			if (RawData)
			{
				const FInstancedPropertyBag* Bag = static_cast<const FInstancedPropertyBag*>(RawData);

				const UPropertyBag* BagStruct = Bag->GetPropertyBagStruct();
				if (CommonBagStruct && CommonBagStruct != BagStruct)
				{
					// Multiple struct types on the sources - show nothing set
					CommonBagStruct = nullptr;
					return false;
				}
				CommonBagStruct = BagStruct;
			}

			return true;
		});
	}

	return CommonBagStruct;
}

/** @return property descriptors of the property bag struct common to all edited properties. */
TArray<FPropertyBagPropertyDesc> GetCommonPropertyDescs(const TSharedPtr<IPropertyHandle>& StructProperty)
{
	TArray<FPropertyBagPropertyDesc> PropertyDescs;

	if (const UPropertyBag* BagStruct = GetCommonBagStruct(StructProperty))
	{
		PropertyDescs = BagStruct->GetPropertyDescs();
	}

	return PropertyDescs;
}

/** Creates new property bag struct and sets all properties to use it, migrating over old values. */
void SetPropertyDescs(const TSharedPtr<IPropertyHandle>& StructProperty, const TConstArrayView<FPropertyBagPropertyDesc> PropertyDescs)
{
	if (ensure(IsScriptStruct<FInstancedPropertyBag>(StructProperty)))
	{
		// Create new bag struct
		const UPropertyBag* NewBagStruct = UPropertyBag::GetOrCreateFromDescs(PropertyDescs);

		// Migrate structs to the new type, copying values over.
		StructProperty->EnumerateRawData([&NewBagStruct](void* RawData, const int32 /*DataIndex*/, const int32 /*NumDatas*/)
		{
			if (RawData)
			{
				if (FInstancedPropertyBag* Bag = static_cast<FInstancedPropertyBag*>(RawData))
				{
					Bag->MigrateToNewBagStruct(NewBagStruct);
				}
			}

			return true;
		});
	}
}

FName GetPropertyNameSafe(const TSharedPtr<IPropertyHandle>& PropertyHandle)
{
	const FProperty* Property = PropertyHandle ? PropertyHandle->GetProperty() : nullptr;
	if (Property != nullptr)
	{
		return Property->GetFName();
	}
	return FName();
}

/** @return true of the property name is not used yet by the property bag structure common to all edited properties. */
bool IsUniqueName(const FName NewName, const FName OldName, const TSharedPtr<IPropertyHandle>& StructProperty)
{
	if (NewName == OldName)
	{
		return false;
	}

	if (!StructProperty || !StructProperty->IsValidHandle())
	{
		return false;
	}

	bool bFound = false;

	if (ensure(IsScriptStruct<FInstancedPropertyBag>(StructProperty)))
	{
		StructProperty->EnumerateConstRawData([&bFound, NewName](const void* RawData, const int32 /*DataIndex*/, const int32 /*NumDatas*/)
		{
			if (const FInstancedPropertyBag* Bag = static_cast<const FInstancedPropertyBag*>(RawData))
			{
				if (const UPropertyBag* BagStruct = Bag->GetPropertyBagStruct())
				{
					const bool bContains = BagStruct->GetPropertyDescs().ContainsByPredicate([NewName](const FPropertyBagPropertyDesc& Desc)
					{
						return Desc.Name == NewName;
					});
					if (bContains)
					{
						bFound = true;
						return false; // Stop iterating
					}
				}
			}

			return true;
		});
	}

	return !bFound;
}

template<typename TFunc>
void ApplyChangesToPropertyDescs(const FText& SessionName, const TSharedPtr<IPropertyHandle>& StructProperty, const TSharedPtr<IPropertyUtilities>& PropUtils, TFunc&& Function)
{
	if (!StructProperty || !StructProperty.Get()->IsValidHandle() || !PropUtils)
	{
		return;
	}

	FScopedTransaction Transaction(SessionName);
	TArray<FPropertyBagPropertyDesc> PropertyDescs = GetCommonPropertyDescs(StructProperty);
	StructProperty->NotifyPreChange();

	Function(PropertyDescs);

	SetPropertyDescs(StructProperty, PropertyDescs);

	StructProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
	StructProperty->NotifyFinishedChangingProperties();
	if (PropUtils)
	{
		PropUtils->ForceRefresh();
	}
}

template <typename TFunc>
void ApplyChangesToSinglePropertyDesc(const FText& SessionName, const TSharedPtr<IPropertyHandle> PropertyHandle, const TSharedPtr<IPropertyHandle>& StructProperty, const TSharedPtr<IPropertyUtilities>& PropUtils, TFunc Function)
{
	ApplyChangesToPropertyDescs(SessionName, StructProperty, PropUtils, [Function = std::move(Function), Property = PropertyHandle->GetProperty()](TArray<FPropertyBagPropertyDesc>& PropertyDescs)
	{
		if (FPropertyBagPropertyDesc* Desc = PropertyDescs.FindByPredicate([Property](const FPropertyBagPropertyDesc& OutDesc) { return OutDesc.CachedProperty == Property; }))
		{
			Function(*Desc);
		}
	});
}

template <typename TFunc>
void ApplyChangesToSinglePropertyDesc(const FText& SessionName, const FPropertyBagPropertyDesc& PropertyDesc, const TSharedPtr<IPropertyHandle>& StructProperty, const TSharedPtr<IPropertyUtilities>& PropUtils, TFunc Function)
{
	ApplyChangesToPropertyDescs(SessionName, StructProperty, PropUtils, [Function = std::move(Function), &PropertyDesc](TArray<FPropertyBagPropertyDesc>& PropertyDescs)
	{
		if (FPropertyBagPropertyDesc* Desc = PropertyDescs.FindByPredicate([&PropertyDesc](const FPropertyBagPropertyDesc& OutDesc) { return OutDesc == PropertyDesc; }))
		{
			Function(*Desc);
		}
	});
}

bool CanHaveMemberVariableOfType(const FEdGraphPinType& PinType)
{
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Exec
		|| PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard
		|| PinType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate
		|| PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate
		|| PinType.PinCategory == UEdGraphSchema_K2::PC_Interface)
	{
		return false;
	}

	return true;
}

FText GetAccessSpecifierNameFromFlags(const EPropertyFlags Flags)
{
	// TODO: Support 'protected'. For now treat protected and private the same.
	if (!!(Flags & (CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected)))
	{
		return LOCTEXT("AccessSpecifierPrivate", "Private");
	}
	else // Public flag or not, should be treated as public.
	{
		return LOCTEXT("AccessSpecifierPublic", "Public");
	}
}

/** Checks if the value for a source property in a source struct has the same value that the target property in the target struct. */
bool ArePropertiesIdentical(
	const FPropertyBagPropertyDesc* InSourcePropertyDesc,
	const FInstancedPropertyBag& InSourceInstance,
	const FPropertyBagPropertyDesc* InTargetPropertyDesc,
	const FInstancedPropertyBag& InTargetInstance)
{
	if (!InSourceInstance.IsValid()
		|| !InTargetInstance.IsValid()
		|| !InSourcePropertyDesc
		|| !InSourcePropertyDesc->CachedProperty
		|| !InTargetPropertyDesc
		|| !InTargetPropertyDesc->CachedProperty)
	{
		return false;
	}

	if (!InSourcePropertyDesc->CompatibleType(*InTargetPropertyDesc))
	{
		return false;
	}

	const uint8* SourceValueAddress = InSourceInstance.GetValue().GetMemory() + InSourcePropertyDesc->CachedProperty->GetOffset_ForInternal();
	const uint8* TargetValueAddress = InTargetInstance.GetValue().GetMemory() + InTargetPropertyDesc->CachedProperty->GetOffset_ForInternal();

	return InSourcePropertyDesc->CachedProperty->Identical(SourceValueAddress, TargetValueAddress);
}

/** Copy the value for a source property in a source struct to the target property in the target struct. */
void CopyPropertyValue(const FPropertyBagPropertyDesc* InSourcePropertyDesc, const FInstancedPropertyBag& InSourceInstance, const FPropertyBagPropertyDesc* InTargetPropertyDesc, FInstancedPropertyBag& InTargetInstance)
{
	if (!InSourceInstance.IsValid() || !InTargetInstance.IsValid() || !InSourcePropertyDesc || !InSourcePropertyDesc->CachedProperty || !InTargetPropertyDesc || !InTargetPropertyDesc->CachedProperty)
	{
		return;
	}

	// Can't copy if they are not compatible.
	if (!InSourcePropertyDesc->CompatibleType(*InTargetPropertyDesc))
	{
		return;
	}

	const uint8* SourceValueAddress = InSourceInstance.GetValue().GetMemory() + InSourcePropertyDesc->CachedProperty->GetOffset_ForInternal();
	uint8* TargetValueAddress = InTargetInstance.GetMutableValue().GetMemory() + InTargetPropertyDesc->CachedProperty->GetOffset_ForInternal();

	InSourcePropertyDesc->CachedProperty->CopyCompleteValue(TargetValueAddress, SourceValueAddress);
}

void GetFilteredVariableTypeTree(const TSharedPtr<IPropertyHandle>& BagStructProperty, TArray<TSharedPtr<UEdGraphSchema_K2::FPinTypeTreeInfo>>& TypeTree, const ETypeTreeFilter TypeTreeFilter)
{
	// The type selector popup might outlive this details view, so bag struct property can be invalid here.
	if (!BagStructProperty || !BagStructProperty->IsValidHandle())
	{
		return;
	}

	DECLARE_DELEGATE_RetVal_TwoParams(bool, FIsPinTypeAccepted, FEdGraphPinType, bool);
	FIsPinTypeAccepted IsPinTypeAcceptedDelegate{};

	if (TOptional<FFindUserFunctionResult> Result = FindUserFunction(BagStructProperty, Metadata::IsPinTypeAcceptedName); Result.IsSet())
	{
		check(Result.GetValue().Function && Result.GetValue().Target);
		IsPinTypeAcceptedDelegate = FIsPinTypeAccepted::CreateUFunction(Result.GetValue().Target, Result.GetValue().Function->GetFName());
	}

	auto IsPinTypeAccepted = [&IsPinTypeAcceptedDelegate](const FEdGraphPinType& InPinType, bool bInIsChild) -> bool
	{
		if (IsPinTypeAcceptedDelegate.IsBound())
		{
			return IsPinTypeAcceptedDelegate.Execute(InPinType, bInIsChild);
		}
		else
		{
			return true;
		}
	};

	check(GetDefault<UEdGraphSchema_K2>());
	TArray<TSharedPtr<UEdGraphSchema_K2::FPinTypeTreeInfo>> TempTypeTree;
	GetDefault<UPropertyBagSchema>()->GetVariableTypeTree(TempTypeTree, TypeTreeFilter);

	// Filter
	for (TSharedPtr<UEdGraphSchema_K2::FPinTypeTreeInfo>& PinType : TempTypeTree)
	{
		if (!PinType.IsValid() || !IsPinTypeAccepted(PinType->GetPinType(/*bForceLoadSubCategoryObject*/false), /*bInIsChild=*/ false))
		{
			continue;
		}

		for (int32 ChildIndex = 0; ChildIndex < PinType->Children.Num();)
		{
			TSharedPtr<UEdGraphSchema_K2::FPinTypeTreeInfo> Child = PinType->Children[ChildIndex];
			if (Child.IsValid())
			{
				const FEdGraphPinType& ChildPinType = Child->GetPinType(/*bForceLoadSubCategoryObject*/false);

				if (!CanHaveMemberVariableOfType(ChildPinType) || !IsPinTypeAccepted(ChildPinType, /*bInIsChild=*/ true))
				{
					PinType->Children.RemoveAt(ChildIndex);
					continue;
				}
			}
			++ChildIndex;
		}

		TypeTree.Add(PinType);
	}
}

bool CanDeleteProperty(const TSharedPtr<IPropertyHandle>& InStructProperty, const TSharedPtr<IPropertyHandle>& ChildPropertyHandle)
{
	if (!InStructProperty || !InStructProperty->IsValidHandle() || !ChildPropertyHandle || !ChildPropertyHandle->IsValidHandle())
	{
		return false;
	}

	// Extra check provided by the user to cancel a remove action. Useful to provide the user a possibility to cancel the action if
	// the given property is in use elsewhere.
	if (TOptional<FFindUserFunctionResult> Result = FindUserFunction(InStructProperty, Metadata::CanRemovePropertyName); Result.IsSet())
	{
		check(Result.GetValue().Function && Result.GetValue().Target);

		FName PropertyName = ChildPropertyHandle->GetProperty()->GetFName();
		const UPropertyBag* PropertyBag = GetCommonBagStruct(InStructProperty);
		const FPropertyBagPropertyDesc* PropertyDesc = PropertyBag ? PropertyBag->FindPropertyDescByName(PropertyName) : nullptr;

		if (!PropertyDesc)
		{
			return false;
		}

		DECLARE_DELEGATE_RetVal_TwoParams(bool, FGetCanDeleteProperty, FGuid, FName);
		return FGetCanDeleteProperty::CreateUFunction(Result.GetValue().Target, Result.GetValue().Function->GetFName()).Execute(PropertyDesc->ID, PropertyDesc->Name);
	}
	else
	{
		return true;
	}
}

void DeleteProperty(TSharedPtr<IPropertyHandle> InStructProperty, TSharedPtr<IPropertyHandle> ChildPropertyHandle, const TSharedPtr<IPropertyUtilities>& PropUtils)
{
	if (!InStructProperty || !InStructProperty->IsValidHandle() || !ChildPropertyHandle || !ChildPropertyHandle->IsValidHandle())
	{
		return;
	}

	if (!CanDeleteProperty(InStructProperty, ChildPropertyHandle))
	{
		return;
	}

	ApplyChangesToPropertyDescs(
		FText::Format(LOCTEXT("OnPropertyDeleted", "Deleted property: {0}"), ChildPropertyHandle->GetPropertyDisplayName()),
		InStructProperty,
		PropUtils,
		[&ChildPropertyHandle](TArray<FPropertyBagPropertyDesc>& PropertyDescs)
		{
			const FProperty* Property = ChildPropertyHandle ? ChildPropertyHandle->GetProperty() : nullptr;
			PropertyDescs.RemoveAll([Property](const FPropertyBagPropertyDesc& Desc) { return Desc.CachedProperty == Property; });
		});
}

FEdGraphPinType GetPinInfo(const TSharedPtr<IPropertyHandle>& ChildPropertyHandle, const TSharedPtr<IPropertyHandle>& InBagStructProperty)
{
	// The SPinTypeSelector popup might outlive this details view, so bag struct property can be invalid here.
	if (!InBagStructProperty || !InBagStructProperty->IsValidHandle() || !ChildPropertyHandle || !ChildPropertyHandle->IsValidHandle())
	{
		return FEdGraphPinType();
	}

	TArray<FPropertyBagPropertyDesc> PropertyDescs = Private::GetCommonPropertyDescs(InBagStructProperty);

	const FProperty* Property = ChildPropertyHandle->GetProperty();
	if (FPropertyBagPropertyDesc* Desc = PropertyDescs.FindByPredicate([Property](const FPropertyBagPropertyDesc& Desc){ return Desc.CachedProperty == Property; }))
	{
		return GetPropertyDescAsPin(*Desc);
	}

	return FEdGraphPinType();
}

void PinInfoChanged(TSharedPtr<IPropertyHandle> ChildPropertyHandle, const TSharedPtr<IPropertyHandle>& InBagStructProperty, const TSharedPtr<IPropertyUtilities>& InPropUtils, const FEdGraphPinType& PinType)
{
	// The SPinTypeSelector popup might outlive this details view, so bag struct property can be invalid here.
	if (!InBagStructProperty || !InBagStructProperty->IsValidHandle() || !ChildPropertyHandle || !ChildPropertyHandle->IsValidHandle())
	{
		return;
	}

	Private::ApplyChangesToPropertyDescs(
		FText::Format(LOCTEXT("OnPropertyTypeChanged", "Changed property type: {0}"), ChildPropertyHandle->GetPropertyDisplayName()),
		InBagStructProperty,
		InPropUtils,
		[&PinType, &ChildPropertyHandle](TArray<FPropertyBagPropertyDesc>& PropertyDescs)
		{
			// Find and change struct type
			const FProperty* Property = ChildPropertyHandle ? ChildPropertyHandle->GetProperty() : nullptr;
			if (FPropertyBagPropertyDesc* Desc = PropertyDescs.FindByPredicate([Property](const FPropertyBagPropertyDesc& Desc){ return Desc.CachedProperty == Property; }))
			{
				SetPropertyDescFromPin(*Desc, PinType);
			}
		});
}

FOptionalSize GetDesiredInputWidgetSizeByType(const TSharedPtr<IPropertyHandle>& ChildPropertyHandle)
{
	static constexpr float StringPropertyDesiredSize = 120.f;
	static constexpr float NamePropertyDesiredSize = 50.f;

	if (const FProperty* Property = ChildPropertyHandle ? ChildPropertyHandle->GetProperty() : nullptr)
	{
		if (Property->IsA(FStrProperty::StaticClass()))
		{
			return StringPropertyDesiredSize;
		}
		else if (Property->IsA(FNameProperty::StaticClass()))
		{
			return NamePropertyDesiredSize;
		}
	}

	return {};
}
} // UE::StructUtils::Private

TSharedRef<SWidget> CreateTypeSelectionWidget(TSharedPtr<IPropertyHandle> ChildPropertyHandle, const TSharedPtr<IPropertyHandle>& InBagStructProperty, const TSharedPtr<IPropertyUtilities>& InPropUtils, SPinTypeSelector::ESelectorType SelectorType, const bool bAllowContainers)
{
	return SNew(SBox)
		.HAlign(HAlign_Right)
		.Padding(FMargin(4, 0))
		[
			SNew(STypeSelector, FGetPinTypeTree::CreateLambda(
				[BagStructProperty = InBagStructProperty](TArray<TSharedPtr<UEdGraphSchema_K2::FPinTypeTreeInfo>>& TypeTree, const ETypeTreeFilter TypeTreeFilter)
				{
				UE::StructUtils::Private::GetFilteredVariableTypeTree(BagStructProperty, TypeTree, TypeTreeFilter);
				}))
			.TargetPinType_Lambda([ChildPropertyHandle = ChildPropertyHandle, BagStructProperty = InBagStructProperty]()
			{
				return Private::GetPinInfo(ChildPropertyHandle, BagStructProperty);
			})
			.OnPinTypeChanged_Lambda([ChildPropertyHandle = ChildPropertyHandle, BagStructProperty = InBagStructProperty, PropUtils = InPropUtils](const FEdGraphPinType& PinType)
			{
				return Private::PinInfoChanged(ChildPropertyHandle, BagStructProperty, PropUtils, PinType);
			})
			.Schema(GetDefault<UPropertyBagSchema>())
			.bAllowContainers(bAllowContainers)
			.SelectorType(SelectorType)
			.TypeTreeFilter(ETypeTreeFilter::None)
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];
}

namespace Constants
{
	static constexpr int32 MaxCategoryLength = 70;
	static constexpr int32 MinSubMenuTextBoxWidth = 40;
	static constexpr int32 MaxSubMenuTextBoxWidth = 800;
	static constexpr int32 SubMenuTextBoxPadding = 5;

	// Special case for categories. Alphanumeric, but including spaces and `|` for nested categories.
	static constexpr TCHAR InvalidCategoryCharacters[] = TEXT("\"',/.:&!?~\\\n\r\t@#(){}[]<>=;^%$`*+-");
}
} // UE::StructUtils

//----------------------------------------------------------------//
//  FPropertyBagInstanceDataDetails
//  - StructProperty is FInstancedPropertyBag
//  - ChildPropertyHandle a child property of the FInstancedPropertyBag::Value (FInstancedStruct)
//----------------------------------------------------------------//

/** Primary constructor. Values passed by parameter struct. */
FPropertyBagInstanceDataDetails::FPropertyBagInstanceDataDetails(const FPropertyBagInstanceDataDetails::FConstructParams& ConstructParams)
	: FInstancedStructDataDetails(ConstructParams.BagStructProperty.IsValid() ? ConstructParams.BagStructProperty->GetChildHandle(TEXT("Value")) : nullptr)
	, BagStructProperty(ConstructParams.BagStructProperty)
	, PropUtils(ConstructParams.PropUtils)
	, bAllowContainers(ConstructParams.bAllowContainers)
	, ChildRowFeatures(ConstructParams.ChildRowFeatures)
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	, bFixedLayout(false)
	, bAllowArrays(true)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
	ensure(UE::StructUtils::Private::IsScriptStruct<FInstancedPropertyBag>(BagStructProperty));
	ensure(PropUtils != nullptr);
}

/** For backwards compatibility. */
FPropertyBagInstanceDataDetails::FPropertyBagInstanceDataDetails(TSharedPtr<IPropertyHandle> InStructProperty, const TSharedPtr<IPropertyUtilities>& InPropUtils, const bool bInFixedLayout, const bool bInAllowContainers)
	: FInstancedStructDataDetails(InStructProperty.IsValid() ? InStructProperty->GetChildHandle(TEXT("Value")) : nullptr)
	, BagStructProperty(InStructProperty)
	, PropUtils(InPropUtils)
	, bAllowContainers(bInAllowContainers)
	, ChildRowFeatures(bInFixedLayout ? EPropertyBagChildRowFeatures::Fixed : EPropertyBagChildRowFeatures::Default)
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	, bFixedLayout(bInFixedLayout) // For backwards compatibility
	, bAllowArrays(bInAllowContainers) // For backwards compatibility
PRAGMA_ENABLE_DEPRECATION_WARNINGS
{
	ensure(UE::StructUtils::Private::IsScriptStruct<FInstancedPropertyBag>(BagStructProperty));
	ensure(PropUtils != nullptr);
}

void FPropertyBagInstanceDataDetails::OnGroupRowAdded(IDetailGroup& GroupRow, int32 Level, const FString& Category) const
{
	using namespace UE::StructUtils;

	FDetailWidgetRow& FolderRow = GroupRow.HeaderRow();
	TWeakPtr<const FPropertyBagInstanceDataDetails> WeakSelf = SharedThis<const FPropertyBagInstanceDataDetails>(this);
	FString FullCategoryName = GroupRow.GetGroupName().ToString();

	/*** DRAG AND DROP HANDLER ***/
	if (EnumHasAllFlags(ChildRowFeatures, EPropertyBagChildRowFeatures::DragAndDrop | EPropertyBagChildRowFeatures::Menu_Categories))
	{
		TSharedPtr<FPropertyBagDetailsDragDropHandlerTarget> DragDropHandler = MakeShared<FPropertyBagDetailsDragDropHandlerTarget>();

		DragDropHandler->BindCanAcceptDragDrop(
			FCanAcceptPropertyBagDetailsRowDropOp::CreateLambda(
				[FullCategoryName](const TSharedPtr<FPropertyBagDetailsDragDropOp>& DropOp, EItemDropZone DropZone) -> TOptional<EItemDropZone>
				{
					if (!DropOp.IsValid() || DropZone != EItemDropZone::OntoItem)
					{
						DropOp->SetDecoration(EPropertyBagDropState::Invalid);
						return TOptional<EItemDropZone>();
					}

					TOptional<FPropertyBagDetailsDragDropOp::FDecoration> DecorationOverride;

					if (Metadata::AreCategoriesEnabled(DropOp->PropertyDesc)
						&& Metadata::GetCategory(DropOp->PropertyDesc).Equals(FullCategoryName))
					{
						const FSlateBrush* Brush = FAppStyle::Get().GetBrush("Graph.ConnectorFeedback.OKWarn");
						DecorationOverride.Emplace(LOCTEXT("OnSameCategoryDragDropDecoratorMessage", "Already in this category"), Brush);
						DropOp->SetDecoration(EPropertyBagDropState::SourceIsTarget, std::move(DecorationOverride));
						return TOptional<EItemDropZone>();
					}

					const FSlateBrush* Brush = FAppStyle::Get().GetBrush("Graph.ConnectorFeedback.OK");
					DecorationOverride.Emplace(LOCTEXT("OnNewCategoryDragDropDecoratorMessage", "Move to this category"), Brush);
					DropOp->SetDecoration(EPropertyBagDropState::Valid, std::move(DecorationOverride));
					return DropZone;
				}));

		DragDropHandler->BindOnHandleDragDrop(
			FOnPropertyBagDetailsRowDropOp::CreateLambda(
				[WeakSelf, FullCategoryName, BagStructProperty = BagStructProperty, PropUtils = PropUtils](const FPropertyBagPropertyDesc& DroppedPropertyDesc, const EItemDropZone DropZone) -> FReply
				{
					if (ensure(DroppedPropertyDesc.CachedProperty && DropZone == EItemDropZone::OntoItem))
					{
						const TSharedPtr<const FPropertyBagInstanceDataDetails> DetailsSP = WeakSelf.Pin();
						const UPropertyBag* ChildBagStruct = DetailsSP ? Private::GetCommonBagStruct(DetailsSP->BagStructProperty) : nullptr;
						// Validate these properties are still part of the bag.
						if (!ChildBagStruct || !ChildBagStruct->FindPropertyDescByProperty(DroppedPropertyDesc.CachedProperty))
						{
							return FReply::Unhandled();
						}

						Private::ApplyChangesToSinglePropertyDesc(
							LOCTEXT("DragToChangeCategory", "Change property category"),
							DroppedPropertyDesc,
							BagStructProperty,
							PropUtils,
							[&DroppedPropertyDesc, &FullCategoryName](FPropertyBagPropertyDesc& Desc)
							{
								Metadata::SetCategory(Desc, FullCategoryName);
							});

						return FReply::Handled();
					}

					return FReply::Unhandled();
				}));

		// Add the drag and drop handler as a target for the folder row.
		FolderRow.DragDropHandler(std::move(DragDropHandler));
	}

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const bool bIsFixed = bFixedLayout || (ChildRowFeatures == EPropertyBagChildRowFeatures::Fixed);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	/*** EDITABLE NAME BLOCK ***/
	const TSharedPtr<SInlineEditableTextBlock> EditableInlineNameWidget = SNew(SInlineEditableTextBlock)
		.MultiLine(false)
		.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		.Font(IDetailLayoutBuilder::GetDetailFontBold())
		.Text(FText::FromString(Category))
		.OnVerifyTextChanged_Lambda([](const FText& InText, FText& OutErrorMessage)
		{
			if (InText.IsEmpty())
			{
				OutErrorMessage = LOCTEXT("InlineEmptyCategoryName", "Name is empty");
				return false;
			}
			else if (InText.ToString().Len() > Constants::MaxCategoryLength)
			{
				OutErrorMessage = LOCTEXT("InlineInvalidCategoryLength", "Too many characters");
				return false;
			}
			else if (!FName::IsValidXName(InText.ToString(), Constants::InvalidCategoryCharacters))
			{
				OutErrorMessage = LOCTEXT("InlineInvalidCategoryName", "Invalid character(s)");
				return false;
			}

			return true;
		})
		.OnTextCommitted_Lambda([FullCategoryName, Category = Category, BagStructProperty = BagStructProperty, PropUtils = PropUtils](const FText& InNewText, const ETextCommit::Type InCommitType)
		{
			if (InCommitType == ETextCommit::OnEnter || InCommitType == ETextCommit::OnUserMovedFocus)
			{
				using namespace UE::StructUtils;

				Private::ApplyChangesToPropertyDescs(
					LOCTEXT("InlineRenameCategory", "Rename category"),
					BagStructProperty,
					PropUtils,
					[&InNewText, OldCategory = FullCategoryName, &Category](TArray<FPropertyBagPropertyDesc>& PropertyDescs)
					{
						FString NewCategory = OldCategory;
						NewCategory.ReplaceInline(*Category, *InNewText.ToString(), ESearchCase::CaseSensitive);
						for (FPropertyBagPropertyDesc& Desc : PropertyDescs)
						{
							if (Metadata::AreCategoriesEnabled(Desc))
							{
								FString DescCategory = Metadata::GetCategory(Desc);
								if (DescCategory.StartsWith(OldCategory))
								{
									DescCategory.ReplaceInline(*OldCategory, *NewCategory);
									Metadata::SetCategory(Desc, DescCategory);
								}
							}
						}
					});
			}
		})
		.IsReadOnly(bIsFixed || !EnumHasAnyFlags(ChildRowFeatures, EPropertyBagChildRowFeatures::Renaming));

	/*** CATEGORY NAME AND BUTTONS ***/
	const TSharedPtr<SBorder> NameContent = SNew(SBorder)
		.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
		.Content()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(1, 0)
			[
				EditableInlineNameWidget.ToSharedRef()
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1)
			[
				SNew(SSpacer).Size(1)
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.AutoWidth()
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("DeleteCategory", "Delete this category."))
				.Visibility(bIsFixed ? EVisibility::Collapsed : EVisibility::Visible)
				.OnClicked_Lambda([GroupName = GroupRow.GetGroupName(), BagStructProperty = BagStructProperty, PropUtils = PropUtils]()
				{
					Private::ApplyChangesToPropertyDescs(
						LOCTEXT("OnCategoryDeleted", "Delete category"),
						BagStructProperty,
						PropUtils,
						[GroupName](TArray<FPropertyBagPropertyDesc>& PropertyDescs)
						{
							for (FPropertyBagPropertyDesc& Desc : PropertyDescs)
							{
								if (Metadata::GetCategory(Desc).Equals(GroupName.ToString()))
								{
									Metadata::RemoveCategory(Desc);
								}
							}
						});

					return FReply::Handled();
				})
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				[
					SNew(SImage)
					.DesiredSizeOverride(FVector2D(16))
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FAppStyle::Get().GetBrush("Icons.Delete"))
				]
			]
		];

	// Mirrors PropertyEditorConstants::GetRowBackgroundColor, which is private.
	NameContent->SetBorderBackgroundColor(TAttribute<FSlateColor>::CreateLambda([NameContent, Level]()
	{
		int32 ColorIndex = 0;
		int32 Increment = 1;

		for (int i = 0; i < Level + 1; ++i)
		{
			ColorIndex += Increment;

			if (ColorIndex == 0 || ColorIndex == 3)
			{
				Increment = -Increment;
			}
		}

		static constexpr uint8 ColorOffsets[] =
		{
			0, 4, (4 + 2), (6 + 4), (10 + 6)
		};

		const FSlateColor BaseSlateColor = NameContent->IsHovered() ? FAppStyle::Get().GetSlateColor("Colors.Header") : FAppStyle::Get().GetSlateColor("Colors.Panel");

		const FColor BaseColor = BaseSlateColor.GetSpecifiedColor().ToFColor(true);

		const FColor ColorWithOffset(
			BaseColor.R + ColorOffsets[ColorIndex],
			BaseColor.G + ColorOffsets[ColorIndex],
			BaseColor.B + ColorOffsets[ColorIndex]);

		return FSlateColor(FLinearColor::FromSRGBColor(ColorWithOffset));
	}));

	FolderRow
		.ShouldAutoExpand(true)
		.WholeRowContent()
		.HAlign(HAlign_Fill)
		[
			NameContent.ToSharedRef()
		];
}

void FPropertyBagInstanceDataDetails::OnChildRowAdded(IDetailPropertyRow& ChildRow)
{
	using namespace UE::StructUtils;
	TSharedPtr<SWidget> NameWidget;
	TSharedPtr<SWidget> PropertyValueWidget;
	FDetailWidgetRow DetailWidgetRow;

	TSharedPtr<IPropertyHandle> ChildPropertyHandle = ChildRow.GetPropertyHandle();
	check(ChildPropertyHandle);

	ChildRow.GetDefaultWidgets(NameWidget, PropertyValueWidget, DetailWidgetRow);
	// Wrap the widget in a box of the desired size, based on the property type.
	PropertyValueWidget = SNew(SBox)
		.MinDesiredWidth(Private::GetDesiredInputWidgetSizeByType(ChildPropertyHandle))
		.Content()
		[
			PropertyValueWidget.ToSharedRef()
		];

	TWeakPtr<FPropertyBagInstanceDataDetails> WeakSelf = SharedThis<FPropertyBagInstanceDataDetails>(this);

	const UPropertyBag* BagStruct = BagStructProperty ? Private::GetCommonBagStruct(BagStructProperty) : nullptr;
	const FProperty* ChildProperty = ChildPropertyHandle->GetProperty();
	const FPropertyBagPropertyDesc* PropertyDesc = BagStruct ? BagStruct->FindPropertyDescByProperty(ChildProperty) : nullptr;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const bool bIsFixed = bFixedLayout || ChildRowFeatures == EPropertyBagChildRowFeatures::Fixed;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	// Validate data and check if it's editable
	if (!ChildProperty || ChildProperty->HasMetaData(Metadata::HideInDetailPanelsName))
	{
		ChildRow.Visibility(EVisibility::Collapsed);
		return;
	}

	bool bEditable = BagStructProperty->IsEditable();

	/*** WARNINGS FOR PROPERTY ISSUES ***/
	FText WarningOnProperty; // This message will supplement a warning icon on the details view child row, which will show if not empty.
	if (!ensure(PropertyDesc) || PropertyDesc->ContainerTypes.Num() > 1)
	{
		// The property editing for nested containers is not supported.
		WarningOnProperty = LOCTEXT("NestedContainersWarning", "This property type (nested container) is not supported in the property bag UI.");
	}
	else if ((PropertyDesc->ValueType == EPropertyBagPropertyType::UInt32 || PropertyDesc->ValueType == EPropertyBagPropertyType::UInt64) && !bIsFixed)
	{
		// Warn that the unsigned types cannot be set via the type selection.
		WarningOnProperty = LOCTEXT("UnsignedTypesWarning", "Unsigned types are not supported through the property type selection. If you change the type, you will not be able to change it back.");
	}
	else if (Private::HasMissingType(ChildPropertyHandle))
	{
		WarningOnProperty = LOCTEXT("MissingTypeWarning", "The property is missing type. The Struct, Enum, or Object may have been removed.");
	}
	else if (!FInstancedPropertyBag::IsPropertyNameValid(Private::GetPropertyNameSafe(ChildPropertyHandle)))
	{
		WarningOnProperty = LOCTEXT("InvalidNameWarning", "The property's name contains invalid characters. Dynamically named properties with invalid characters may be rejected in future releases.");
	}

	/*** OVERRIDE RESET TO DEFAULT ACTION FOR BAG OVERRIDES ***/
	if (HasPropertyOverrides())
	{
		TAttribute<bool> EditConditionValue = TAttribute<bool>::CreateLambda(
			[WeakSelf, ChildPropertyHandle]() -> bool
			{
				if (const TSharedPtr<FPropertyBagInstanceDataDetails> Self = WeakSelf.Pin())
				{
					return Self->IsPropertyOverridden(ChildPropertyHandle) == EPropertyOverrideState::Yes;
				}
				return true;
			});

		FOnBooleanValueChanged OnEditConditionChanged = FOnBooleanValueChanged::CreateLambda([WeakSelf, ChildPropertyHandle](bool bNewValue)
		{
			if (const TSharedPtr<FPropertyBagInstanceDataDetails> Self = WeakSelf.Pin())
			{
				Self->SetPropertyOverride(ChildPropertyHandle, bNewValue);
			}
		});

		ChildRow.EditCondition(std::move(EditConditionValue), std::move(OnEditConditionChanged));

		FIsResetToDefaultVisible IsResetVisible = FIsResetToDefaultVisible::CreateLambda([WeakSelf](TSharedPtr<IPropertyHandle> PropertyHandle)
		{
			if (const TSharedPtr<FPropertyBagInstanceDataDetails> Self = WeakSelf.Pin())
			{
				return !Self->IsDefaultValue(PropertyHandle);
			}
			return false;
		});
		FResetToDefaultHandler ResetHandler = FResetToDefaultHandler::CreateLambda([WeakSelf](TSharedPtr<IPropertyHandle> PropertyHandle)
		{
			if (const TSharedPtr<FPropertyBagInstanceDataDetails> Self = WeakSelf.Pin())
			{
				Self->ResetToDefault(PropertyHandle);
			}
		});
		FResetToDefaultOverride ResetOverride = FResetToDefaultOverride::Create(IsResetVisible, ResetHandler);

		ChildRow.OverrideResetToDefault(ResetOverride);
	}

	if (!bIsFixed)
	{
		/*** BUILD PROPERTY NAME WIDGET ***/
		TSharedRef<SHorizontalBox> PropertyDetailsWidget = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.Padding(3, 0)
			.AutoWidth()
			[
				SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.ToolTipText(WarningOnProperty)
					.Visibility_Lambda([WarningOnProperty]()
					{
						return WarningOnProperty.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
					})
					.DesiredSizeOverride(FVector2D(12))
					.ColorAndOpacity(FLinearColor(1.f, 0.8f, 0.f, 1.f))
					.Image(FAppStyle::GetBrush("Icons.Error"))
				]
			];

		if (EnumHasAnyFlags(ChildRowFeatures, EPropertyBagChildRowFeatures::CompactTypeSelector))
		{
			PropertyDetailsWidget->AddSlot()
				.HAlign(HAlign_Left)
				.Padding(1, 0)
				.AutoWidth()
				[
					CreateTypeSelectionWidget(ChildPropertyHandle, BagStructProperty, PropUtils, SPinTypeSelector::ESelectorType::Compact, bAllowContainers)
				];
		}

		/*** EDITABLE NAME BLOCK ***/
		TSharedPtr<SInlineEditableTextBlock> EditableInlineNameWidget = SNew(SInlineEditableTextBlock)
			.IsReadOnly(!EnumHasAnyFlags(ChildRowFeatures, EPropertyBagChildRowFeatures::Renaming))
			.ToolTipText_Lambda([ChildPropertyHandle]() -> FText
			{
				return ChildPropertyHandle->HasMetaData(Metadata::Specifiers::ToolTipName)
						   ? FText::FromString(ChildPropertyHandle->GetMetaData(Metadata::Specifiers::ToolTipName))
						   : FText::GetEmpty();
			})
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.MultiLine(false)
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			.Text_Lambda([ChildPropertyHandle]()
			{
				const FName PropertyName = Private::GetPropertyNameSafe(ChildPropertyHandle);
				return FText::FromName(PropertyName);
			})
			.OnVerifyTextChanged_Lambda([BagStructProperty = BagStructProperty, ChildPropertyHandle](const FText& InText, FText& OutErrorMessage)
			{
				if (InText.IsEmpty())
				{
					OutErrorMessage = LOCTEXT("InlineEmptyPropertyName", "Name is empty");
					return false;
				}

				// Check for invalid characters upon renaming.
				if (!FInstancedPropertyBag::IsPropertyNameValid(InText.ToString()))
				{
					OutErrorMessage = LOCTEXT("InlineInvalidPropertyName", "Invalid character(s)");
					return false;
				}

				const FName OldName = Private::GetPropertyNameSafe(ChildPropertyHandle);
				// Bypass if the name is the exact same.
				if (InText.ToString().Equals(OldName.ToString()))
				{
					return true;
				}

				// Sanitize out any other characters that we allowed for convenience but are not valid, like spaces.
				const FName NewName = FInstancedPropertyBag::SanitizePropertyName(InText.ToString());

				// Bypass if sanitized name is the same.
				if (NewName == OldName)
				{
					return true;
				}

				if (!Private::IsUniqueName(NewName, OldName, BagStructProperty))
				{
					OutErrorMessage = LOCTEXT("InlinePropertyUniqueName", "Property must have unique name");
					return false;
				}

				// Name is OK.
				return true;
			})
			.OnTextCommitted_Lambda([BagStructProperty = BagStructProperty, PropUtils = PropUtils, ChildPropertyHandle](const FText& InNewText, ETextCommit::Type InCommitType)
			{
				if (InCommitType == ETextCommit::OnCleared)
				{
					return;
				}

				const FName NewName = FInstancedPropertyBag::SanitizePropertyName(InNewText.ToString());
				const FName OldName = Private::GetPropertyNameSafe(ChildPropertyHandle);

				if (NewName == OldName)
				{
					return;
				}

				if (!ensureMsgf(Private::IsUniqueName(NewName, OldName, BagStructProperty), TEXT("Should have already been addressed in OnVerifyTextChanged.")))
				{
					return;
				}

				Private::ApplyChangesToPropertyDescs(
					FText::Format(LOCTEXT("OnPropertyNameChanged", "Change property name: {0} -> {1}"), FText::FromName(OldName), FText::FromName(NewName)),
					BagStructProperty,
					PropUtils,
					[&NewName, &ChildPropertyHandle](TArray<FPropertyBagPropertyDesc>& PropertyDescs)
					{
						const FProperty* Property = ChildPropertyHandle->GetProperty();
						if (FPropertyBagPropertyDesc* Desc = PropertyDescs.FindByPredicate([Property](const FPropertyBagPropertyDesc& Desc) { return Desc.CachedProperty == Property; }))
						{
							Desc->Name = NewName;
						}
					});
			});

		/*** CURRENT UI AS IT BECOMES DEPRECATED ***/
		// Deprecated in 5.6 - the combo button on the name widget will be removed in favor of the new drop-down menu.
		if (EnumHasAnyFlags(ChildRowFeatures, EPropertyBagChildRowFeatures::Deprecated))
		{
			// Add the widget to the property bar.
			PropertyDetailsWidget->AddSlot()
				.HAlign(HAlign_Left)
				.AutoWidth()
				.Padding(0, 0, 3, 0)
				[
					SNew(SBox)
					.Content()
					[
						SNew(SComboButton)
						.MenuContent()
						[
							PRAGMA_DISABLE_DEPRECATION_WARNINGS
							OnPropertyNameContent(ChildPropertyHandle, EditableInlineNameWidget)
							PRAGMA_ENABLE_DEPRECATION_WARNINGS
						]
						.ContentPadding(FMargin(0, 0, 2, 0))
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ButtonContent()
						[
							EditableInlineNameWidget.ToSharedRef()
						]
					]
				];
		}
		else // No deprecated combo box. Just add the name.
		{
			PropertyDetailsWidget->AddSlot()
				.HAlign(HAlign_Left)
				.AutoWidth()
				.Padding(0, 0, 3, 0)
				[
					EditableInlineNameWidget.ToSharedRef()
				];
		}

		// Extendable spacer between the name and the drop-down
		PropertyDetailsWidget->AddSlot()
			.FillWidth(1)
			[
				SNew(SSpacer)
				.Size(1)
			];

		/*** ACCESS SPECIFIER BUTTON ***/
		if (EnumHasAnyFlags(ChildRowFeatures, EPropertyBagChildRowFeatures::AccessSpecifierButton))
		{
			using namespace UE::StructUtils::Metadata;
			PropertyDetailsWidget->AddSlot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(1, 0)
				[
					SNew(SButton)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.ToolTipText(LOCTEXT("SetAccessSpecifier", "Set the access specifier on the property to Public or Private."))
					.OnClicked_Lambda([ChildPropertyHandle, BagStructProperty = BagStructProperty, PropUtils = PropUtils]()
					{
						FProperty* Property = ChildPropertyHandle->GetProperty();
						Private::ApplyChangesToPropertyDescs(
							LOCTEXT("OnPropertyAccessSpecifierChanged", "Set access specifier."),
							BagStructProperty,
							PropUtils,
							[Property, BagStructProperty](TArray<FPropertyBagPropertyDesc>& PropertyDescs)
							{
								if (FPropertyBagPropertyDesc* Desc = PropertyDescs.FindByPredicate([Property, BagStructProperty](const FPropertyBagPropertyDesc& Desc) { return Desc.CachedProperty == Property; }))
								{
									BagStructProperty->NotifyPreChange();
									const bool bIsPrivate = Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected);
									Desc->PropertyFlags &= ~CPF_NativeAccessSpecifiers;
									if (bIsPrivate)
									{
										Desc->PropertyFlags |= CPF_NativeAccessSpecifierPublic;
									}
									else
									{
										Desc->PropertyFlags |= CPF_NativeAccessSpecifierPrivate;
									}
									BagStructProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
									BagStructProperty->NotifyFinishedChangingProperties();
								}
							});

						return FReply::Handled();
					})
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					[
						SNew(SImage)
						.DesiredSizeOverride(FVector2D(16))
						.ColorAndOpacity(FSlateColor::UseForeground())
						.Image_Lambda([ChildPropertyHandle]()
						{
							check(ChildPropertyHandle);
							if (const FProperty* Property = ChildPropertyHandle->GetProperty())
							{
								// For now, treat protected as private. TODO: Add toggle for protected.
								if (Property->HasAnyPropertyFlags(CPF_NativeAccessSpecifierPrivate | CPF_NativeAccessSpecifierProtected))
								{
									return FAppStyle::Get().GetBrush("Icons.Visible");
								}
							}

							return FAppStyle::Get().GetBrush("Icons.Hidden");
						})
					]
				];
		}

		/*** DROP-DOWN MENU OPTIONS ***/
		// Check drop-down is enabled and at least one option as well.
		if (EnumHasAnyFlags(ChildRowFeatures, EPropertyBagChildRowFeatures::DropDownMenuButton)
			&& EnumHasAnyFlags(ChildRowFeatures, EPropertyBagChildRowFeatures::AllMenuOptions))
		{
			static constexpr bool bInShouldCloseWindowAfterMenuSelection = true;
			FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, nullptr);

			if (EnumHasAnyFlags(ChildRowFeatures, EPropertyBagChildRowFeatures::Menu_TypeSelector))
			{
				MenuBuilder.BeginSection(/*InExtensionHook=*/NAME_None, LOCTEXT("DropDownMenuSectionTypeSelector", "Type"));
				MenuBuilder.AddWidget(
					CreateTypeSelectionWidget(
						ChildPropertyHandle,
						BagStructProperty,
						PropUtils,
						SPinTypeSelector::ESelectorType::Full,
						bAllowContainers),
					/*InLabel=*/FText::GetEmpty());
				MenuBuilder.EndSection();
			}

			const bool bMenuRenameEnabled = EnumHasAllFlags(ChildRowFeatures, EPropertyBagChildRowFeatures::Renaming | EPropertyBagChildRowFeatures::Menu_Rename);
			const bool bMenuDeleteEnabled = EnumHasAllFlags(ChildRowFeatures, EPropertyBagChildRowFeatures::Deletion | EPropertyBagChildRowFeatures::Menu_Delete);

			if (bMenuRenameEnabled | bMenuDeleteEnabled)
			{
				MenuBuilder.BeginSection(/*InExtensionHook=*/NAME_None, LOCTEXT("DropDownMenuSectionGeneral", "General"));

				// Must have property renaming enabled or the editable inline widget will be invalid.
				if (bMenuRenameEnabled)
				{
					MenuBuilder.AddMenuEntry(
						LOCTEXT("DropDownMenuRenameProperty", "Rename property"),
						LOCTEXT("DropDownMenuRenamePropertyToolTip", "Enable the inline property renaming."),
						FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"),
						FUIAction(FExecuteAction::CreateLambda([NameWidgetWeak = EditableInlineNameWidget.ToWeakPtr()]()
						{
							if (TSharedPtr<SInlineEditableTextBlock> NameWidget = NameWidgetWeak.Pin())
							{
								NameWidget->EnterEditingMode();
							}
						})));
				}

				if (bMenuDeleteEnabled)
				{
					MenuBuilder.AddMenuEntry(
						LOCTEXT("DropDownMenuRemoveProperty", "Remove property"),
						LOCTEXT("DropDownMenuRemovePropertyToolTip", "Delete the property."),
						FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
						FUIAction(FExecuteAction::CreateLambda([BagStructProperty = BagStructProperty, PropUtils = PropUtils, ChildPropertyHandle]()
						{
							Private::DeleteProperty(BagStructProperty, ChildPropertyHandle, PropUtils);
						}))
					);
				}

				MenuBuilder.EndSection();
			}

			// BagStructProperty and PropUtils are forwarded straight to OnTextCommitted, so they save a ref count and be refs here.
			auto BuildMetadataSpecifierSubMenu = [](
				TAttribute<FText>&& TextAttribute,
				FOnVerifyTextChanged&& OnVerifyTextDelegate,
				FOnTextCommitted&& OnTextCommittedDelegate) -> FNewMenuDelegate
			{
				TSharedRef<SWidget> TextBox = SNew(SMultiLineEditableTextBox)
					.WrapTextAt(400)
					.Text(MoveTemp(TextAttribute))
					.OnVerifyTextChanged(MoveTemp(OnVerifyTextDelegate))
					.OnTextCommitted(OnTextCommittedDelegate);

				return FNewMenuDelegate::CreateLambda(
					[TextBox](FMenuBuilder& OutMenuBuilder)
					{
						OutMenuBuilder.AddWidget(
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.HAlign(HAlign_Fill)
							.MinWidth(Constants::MinSubMenuTextBoxWidth)
							.MaxWidth(Constants::MaxSubMenuTextBoxWidth)
							.Padding(Constants::SubMenuTextBoxPadding, 0)
							.AutoWidth()
							[
								TextBox
							],
							FText::GetEmpty(),
							/*bNoIndent=*/true,
							/*bInSearchable=*/true);
					});
			};

			// The property's category (grouping) can be edited here.
			if (EnumHasAllFlags(ChildRowFeatures, EPropertyBagChildRowFeatures::Categories | EPropertyBagChildRowFeatures::Menu_Categories))
			{
				MenuBuilder.BeginSection(/*InExtensionHook=*/NAME_None, LOCTEXT("DropDownMenuSectionCategory", "Category"));

				if (ChildPropertyHandle->HasMetaData(Metadata::CategoryName))
				{
					MenuBuilder.AddMenuEntry(
						LOCTEXT("DropDownMenuClearCategory", "Clear category"),
						LOCTEXT("DropDownMenuClearCategoryToolTip", "Remove the property from its current category."),
						FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
						FUIAction(FExecuteAction::CreateLambda([ChildPropertyHandle, StructProperty = BagStructProperty, PropUtils = PropUtils]()
						{
							Private::ApplyChangesToSinglePropertyDesc(
								LOCTEXT("DropDownMenuOnCategoryCleared", "Clear property category"),
								ChildPropertyHandle,
								StructProperty,
								PropUtils,
								[](FPropertyBagPropertyDesc& Desc)
								{
									Metadata::RemoveCategory(Desc);
								});
						})));
				}

				FOnVerifyTextChanged ValidateCategoryNameDelegate = FOnVerifyTextChanged::CreateLambda([](const FText& InText, FText& OutMessage)
				{
					if (InText.ToString().Len() > Constants::MaxCategoryLength)
					{
						OutMessage = LOCTEXT("DropDownMenuInvalidCategoryName", "Invalid category name");
						return false;
					}
					else
					{
						return true;
					}
				});

				MenuBuilder.AddSubMenu(
					FText::FromName(Metadata::CategoryName),
					LOCTEXT("DropDownMenuSubMenuCategoryTooltip", "Set the category of this property. Subcategories can be created with the '|' character."),
					BuildMetadataSpecifierSubMenu(
						TAttribute<FText>::CreateLambda([ChildPropertyHandle]()
						{
							FText GroupLabel = FText::FromString("");
							if (ChildPropertyHandle && ChildPropertyHandle->HasMetaData(Metadata::CategoryName))
							{
								GroupLabel = FText::FromString(ChildPropertyHandle->GetMetaData(Metadata::CategoryName));
							}

							return GroupLabel;
						}),
						FOnVerifyTextChanged::CreateLambda([](const FText& InText, FText& OutMessage)
						{
							if (InText.ToString().Len() > Constants::MaxCategoryLength)
							{
								OutMessage = LOCTEXT("DropDownMenuInvalidCategoryName", "Invalid category name");
								return false;
							}
							else
							{
								return true;
							}
						}),

						FOnTextCommitted::CreateLambda([StructProperty = BagStructProperty, PropUtils = PropUtils, ChildPropertyHandle](const FText& CommittedText, const ETextCommit::Type CommitType)
						{
							if (CommitType == ETextCommit::OnEnter || CommitType == ETextCommit::OnUserMovedFocus)
							{
								Private::ApplyChangesToSinglePropertyDesc(
									LOCTEXT("DropDownMenuOnCategoryEdited", "Edit property category"),
									ChildPropertyHandle,
									StructProperty,
									PropUtils,
									[&CommittedText](FPropertyBagPropertyDesc& Desc)
									{
										Metadata::SetCategory(Desc, CommittedText.ToString());
									});
							}
						})),
					/*bInOpenSubMenuOnClick=*/true);

				MenuBuilder.EndSection();
			}

			// The property's optional metadata specifiers can be edited here.
			if (EnumHasAllFlags(ChildRowFeatures, EPropertyBagChildRowFeatures::Menu_MetadataSpecifiers))
			{
				MenuBuilder.BeginSection(/*InExtensionHook=*/NAME_None, LOCTEXT("DropDownMenuSectionMetadataSpecifiers", "Specifiers"));

				auto BuildGenericSpecifierSubMenu = [this, ChildPropertyHandle, BuildMetadataSpecifierSubMenu](const FName& SpecifierName, const bool bIsNumeric = false)
				{
					return BuildMetadataSpecifierSubMenu(
						TAttribute<FText>::CreateLambda([SpecifierName, ChildPropertyHandle]()
						{
							return ChildPropertyHandle.IsValid()
									   ? FText::FromString(ChildPropertyHandle->GetMetaData(SpecifierName))
									   : FText::GetEmpty();
						}),
						!bIsNumeric
							? FOnVerifyTextChanged()
							: FOnVerifyTextChanged::CreateLambda([](const FText& InText, FText& OutMessage) -> bool
							{
								if (!InText.IsEmpty() && !InText.IsNumeric())
								{
									OutMessage = LOCTEXT("DropDownMenuSpecifierNumericValueExpected", "Value is not numeric");
									return false;
								}
								else
								{
									return true;
								}
							}),
						FOnTextCommitted::CreateLambda(
							[SpecifierName, StructProperty = BagStructProperty, PropUtils = PropUtils, ChildPropertyHandle]
							(const FText& CommittedText, const ETextCommit::Type CommitType)
							{
								if (CommitType != ETextCommit::OnCleared)
								{
									Private::ApplyChangesToSinglePropertyDesc(
										FText::Format(LOCTEXT("DropDownMenuOnSetSpecifier", "Set metadata specifier '{0}' value"), FText::FromName(SpecifierName)),
										ChildPropertyHandle,
										StructProperty,
										PropUtils,
										[SpecifierName, &CommittedText](FPropertyBagPropertyDesc& Desc)
										{
											// If empty, remove the specifier
											if (CommittedText.IsEmpty())
											{
												Desc.RemoveMetadata(SpecifierName);
											}
											else
											{
												Desc.SetMetaData(SpecifierName, CommittedText.ToString());
											}
										});
								}
							}));
				};

				MenuBuilder.AddSubMenu(
					FText::FromName(Metadata::Specifiers::ToolTipName),
					LOCTEXT("DropDownMenuSubMenuTooltipTooltip", "Set the tooltip for this property."),
					BuildGenericSpecifierSubMenu(Metadata::Specifiers::ToolTipName),
					/*bInOpenSubMenuOnClick=*/true);

				if (Metadata::Specifiers::SupportsClamping(*PropertyDesc))
				{
					MenuBuilder.AddSubMenu(
						FText::FromName(Metadata::Specifiers::ClampMinName),
						LOCTEXT("DropDownMenuSubMenuClampMinTooltip", "Set the min value for this property."),
						BuildGenericSpecifierSubMenu(Metadata::Specifiers::ClampMinName, /*bIsNumeric=*/true),
						/*bInOpenSubMenuOnClick=*/true);

					MenuBuilder.AddSubMenu(
						FText::FromName(Metadata::Specifiers::ClampMaxName),
						LOCTEXT("DropDownMenuSubMenuClampMaxTooltip", "Set the max value for this property."),
						BuildGenericSpecifierSubMenu(Metadata::Specifiers::ClampMaxName, /*bIsNumeric=*/true),
						/*bInOpenSubMenuOnClick=*/true);

					MenuBuilder.AddSubMenu(
						FText::FromName(Metadata::Specifiers::UIMinName),
						LOCTEXT("DropDownMenuSubMenuUIMinTooltip", "Set the min UI value for this property."),
						BuildGenericSpecifierSubMenu(Metadata::Specifiers::UIMinName, /*bIsNumeric=*/true),
						/*bInOpenSubMenuOnClick=*/true);

					MenuBuilder.AddSubMenu(
						FText::FromName(Metadata::Specifiers::UIMaxName),
						LOCTEXT("DropDownMenuSubMenuUIMaxTooltip", "Set the max UI value for this property."),
						BuildGenericSpecifierSubMenu(Metadata::Specifiers::UIMaxName, /*bIsNumeric=*/true),
						/*bInOpenSubMenuOnClick=*/true);
				}

				if (Metadata::Specifiers::SupportsSettingUnits(*PropertyDesc))
				{
					MenuBuilder.AddSubMenu(
						FText::FromName(Metadata::Specifiers::UnitsName),
						LOCTEXT("DropDownMenuSubMenuUnitsTooltip", "Set the units descriptor for this property."),
						BuildGenericSpecifierSubMenu(Metadata::Specifiers::UnitsName),
						/*bInOpenSubMenuOnClick=*/true);
				}

				MenuBuilder.EndSection();
			}

			/*** DROP-DOWN ARROW MENU ***/
			PropertyDetailsWidget->AddSlot()
				.HAlign(HAlign_Right)
				.AutoWidth()
				.Padding(0, 0, 5, 0)
				[
					SNew(SBox)
					.Content()
					[
						SNew(SComboButton)
						.MenuContent()
						[
							MenuBuilder.MakeWidget()
						]
						.HasDownArrow(true)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.ButtonContent()
						[
							SNullWidget::NullWidget
						]
					]
				];
		}

		/*** DRAG AND DROP HANDLER ***/
		if (EnumHasAnyFlags(ChildRowFeatures, EPropertyBagChildRowFeatures::DragAndDrop) && PropertyDesc != nullptr)
		{
			TSharedPtr<FPropertyBagDetailsDragDropHandler> DragDropHandler = MakeShared<FPropertyBagDetailsDragDropHandler>(*PropertyDesc);

			// Can accept drag and drop check for if this is a valid drop
			DragDropHandler->BindCanAcceptDragDrop(
				FCanAcceptPropertyBagDetailsRowDropOp::CreateLambda(
					[PropertyDesc](const TSharedPtr<FPropertyBagDetailsDragDropOp>& DropOp, EItemDropZone DropZone) -> TOptional<EItemDropZone>
					{
						if (!PropertyDesc || !DropOp.IsValid())
						{
							DropOp->SetDecoration(EPropertyBagDropState::Invalid);
							return TOptional<EItemDropZone>();
						}

						if (DropZone == EItemDropZone::OntoItem && PropertyDesc->ID != DropOp->PropertyDesc.ID)
						{
							DropOp->SetDecoration(EPropertyBagDropState::Invalid);
							return TOptional<EItemDropZone>();
						}

						// No effect to drop in these cases. Either source == target, or moving source above/below target puts source in same location.
						if (*PropertyDesc == DropOp->PropertyDesc
							|| (DropZone == EItemDropZone::AboveItem && DropOp->PropertyDesc.GetCachedIndex() == PropertyDesc->GetCachedIndex() - 1)
							|| (DropZone == EItemDropZone::BelowItem && DropOp->PropertyDesc.GetCachedIndex() == PropertyDesc->GetCachedIndex() + 1))
						{
							DropOp->SetDecoration(EPropertyBagDropState::SourceIsTarget);
							return TOptional<EItemDropZone>();
						}

						DropOp->SetDecoration(EPropertyBagDropState::Valid);
						return DropZone;
					}));

			DragDropHandler->BindOnHandleDragDrop(
				FOnPropertyBagDetailsRowDropOp::CreateLambda(
					[WeakSelf, PropertyDesc = *PropertyDesc, BagStructProperty = BagStructProperty, PropUtils = PropUtils](const FPropertyBagPropertyDesc& DroppedPropertyDesc, EItemDropZone DropZone) -> FReply
					{
						using namespace UE::StructUtils;

						const TSharedPtr<FPropertyBagInstanceDataDetails> DetailsSP = WeakSelf.Pin();
						const UPropertyBag* ChildBagStruct = DetailsSP ? Private::GetCommonBagStruct(DetailsSP->BagStructProperty) : nullptr;
						// Validate these properties are still part of the bag.
						if (!ChildBagStruct
							|| !ChildBagStruct->FindPropertyDescByProperty(PropertyDesc.CachedProperty)
							|| !ChildBagStruct->FindPropertyDescByProperty(DroppedPropertyDesc.CachedProperty))
						{
							return FReply::Unhandled();
						}

						EPropertyBagAlterationResult Result = EPropertyBagAlterationResult::InternalError;

						DetailsSP->BagStructProperty->EnumerateRawData([DropZone, &PropertyDesc, &DroppedPropertyDesc, &Result](void* RawData, const int32 /*DataIndex*/, const int32 /*NumData*/)
						{
							if (FInstancedPropertyBag* PropertyBag = RawData ? static_cast<FInstancedPropertyBag*>(RawData) : nullptr)
							{
								Result = PropertyBag->ReorderProperty(DroppedPropertyDesc.Name, PropertyDesc.Name, DropZone == EItemDropZone::AboveItem);
							}

							return true;
						});

						if (Result == EPropertyBagAlterationResult::Success)
						{
							Private::ApplyChangesToSinglePropertyDesc(
								LOCTEXT("DragDropReorderProperties", "Reordered properties"),
								DroppedPropertyDesc,
								BagStructProperty,
								PropUtils,
								[PropertyDesc](FPropertyBagPropertyDesc& Desc)
								{
									Metadata::SetCategory(Desc, Metadata::GetCategory(PropertyDesc));
								});

							return FReply::Handled();
						}
						else
						{
							return FReply::Unhandled();
						}
					}));

			// Bind the drag and drop handler for receiving.
			ChildRow.DragDropHandler(DragDropHandler);

			// Add draggability for the name widget.
			NameWidget = SNew(StructUtilsEditor::SDraggableBox)
				.DragDropHandler(DragDropHandler)
				.RequireDirectHover(true)
				.Content()
				[
					PropertyDetailsWidget
				];

			// Add draggability for the value widget, maximizing draggable space, but not at the cost of the value widget.
			PropertyValueWidget = SNew(StructUtilsEditor::SDraggableBox)
				.DragDropHandler(DragDropHandler)
				.RequireDirectHover(true)
				.Content()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					.AutoWidth()
					[
						PropertyValueWidget.ToSharedRef()
					]
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					.FillWidth(1)
					[
						SNullWidget::NullWidget
					]
				];
		}
		else // Update the name widget with our new property details composition.
		{
			NameWidget = PropertyDetailsWidget;
		}
	}

	/*** FINAL WIDGET ***/
	ChildRow
		.IsEnabled(bEditable)
		.CustomWidget(/*bShowChildren=*/true)
		.NameContent()
		.HAlign(HAlign_Fill)
		[
			NameWidget.ToSharedRef()
		]
		.ValueContent()
		.HAlign(HAlign_Fill)
		[
			PropertyValueWidget.ToSharedRef()
		];
}

FPropertyBagInstanceDataDetails::EPropertyOverrideState FPropertyBagInstanceDataDetails::IsPropertyOverridden(TSharedPtr<IPropertyHandle> ChildPropertyHandle) const
{
	if (!ChildPropertyHandle)
	{
		return EPropertyOverrideState::Undetermined;
	}

	const FProperty* Property = ChildPropertyHandle->GetProperty();
	if (!Property)
	{
		return EPropertyOverrideState::Undetermined;
	}

	int32 NumValues = 0;
	int32 NumOverrides = 0;

	EnumeratePropertyBags(BagStructProperty,
		[Property, &NumValues, &NumOverrides]
		(const FInstancedPropertyBag& DefaultPropertyBag, const FInstancedPropertyBag& PropertyBag, const IPropertyBagOverrideProvider& OverrideProvider)
		{
			NumValues++;
			if (const UPropertyBag* Bag = PropertyBag.GetPropertyBagStruct())
			{
				const FPropertyBagPropertyDesc* PropertyDesc = Bag->FindPropertyDescByProperty(Property);
				if (PropertyDesc && OverrideProvider.IsPropertyOverridden(PropertyDesc->ID))
				{
					NumOverrides++;
				}
			}

			return true;
		});

	if (NumOverrides == 0)
	{
		return EPropertyOverrideState::No;
	}
	else if (NumOverrides == NumValues)
	{
		return EPropertyOverrideState::Yes;
	}
	return EPropertyOverrideState::Undetermined;
}

void FPropertyBagInstanceDataDetails::SetPropertyOverride(TSharedPtr<IPropertyHandle> ChildPropertyHandle, const bool bIsOverridden)
{
	if (!ChildPropertyHandle)
	{
		return;
	}

	const FProperty* Property = ChildPropertyHandle->GetProperty();
	check(Property);

	FScopedTransaction Transaction(FText::Format(LOCTEXT("OverrideChange", "Change Override for {0}"), FText::FromName(ChildPropertyHandle->GetProperty()->GetFName())));

	PreChangeOverrides();

	EnumeratePropertyBags(
		BagStructProperty,
		[Property, bIsOverridden]
		(const FInstancedPropertyBag& DefaultPropertyBag, const FInstancedPropertyBag& PropertyBag, const IPropertyBagOverrideProvider& OverrideProvider)
		{
			if (const UPropertyBag* Bag = PropertyBag.GetPropertyBagStruct())
			{
				if (const FPropertyBagPropertyDesc* PropertyDesc = Bag->FindPropertyDescByProperty(Property))
				{
					OverrideProvider.SetPropertyOverride(PropertyDesc->ID, bIsOverridden);
				}
			}

			return true;
		});

	PostChangeOverrides();
}

bool FPropertyBagInstanceDataDetails::IsDefaultValue(TSharedPtr<IPropertyHandle> ChildPropertyHandle) const
{
	if (!ChildPropertyHandle)
	{
		return true;
	}

	int32 NumValues = 0;
	int32 NumOverridden = 0;
	int32 NumIdentical = 0;

	const FProperty* Property = ChildPropertyHandle->GetProperty();
	check(Property);

	EnumeratePropertyBags(
		BagStructProperty,
		[Property, &NumValues, &NumOverridden, &NumIdentical]
		(const FInstancedPropertyBag& DefaultPropertyBag, const FInstancedPropertyBag& PropertyBag, const IPropertyBagOverrideProvider& OverrideProvider)
		{
			NumValues++;

			const UPropertyBag* DefaultBag = DefaultPropertyBag.GetPropertyBagStruct();
			const UPropertyBag* Bag = PropertyBag.GetPropertyBagStruct();
			if (Bag && DefaultBag)
			{
				const FPropertyBagPropertyDesc* PropertyDesc = Bag->FindPropertyDescByProperty(Property);
				const FPropertyBagPropertyDesc* DefaultPropertyDesc = DefaultBag->FindPropertyDescByProperty(Property);
				if (PropertyDesc
					&& DefaultPropertyDesc
					&& OverrideProvider.IsPropertyOverridden(PropertyDesc->ID))
				{
					NumOverridden++;
					if (UE::StructUtils::Private::ArePropertiesIdentical(DefaultPropertyDesc, DefaultPropertyBag, PropertyDesc, PropertyBag))
					{
						NumIdentical++;
					}
				}
			}
			return true;
		});

	if (NumOverridden == NumIdentical)
	{
		return true;
	}

	return false;
}

void FPropertyBagInstanceDataDetails::ResetToDefault(TSharedPtr<IPropertyHandle> ChildPropertyHandle)
{
	if (!ChildPropertyHandle)
	{
		return;
	}

	const FProperty* Property = ChildPropertyHandle->GetProperty();
	check(Property);

	FScopedTransaction Transaction(FText::Format(LOCTEXT("ResetToDefault", "Reset {0} to default value"), FText::FromName(ChildPropertyHandle->GetProperty()->GetFName())));
	ChildPropertyHandle->NotifyPreChange();

	EnumeratePropertyBags(
		BagStructProperty,
		[Property]
		(const FInstancedPropertyBag& DefaultPropertyBag, FInstancedPropertyBag& PropertyBag, const IPropertyBagOverrideProvider& OverrideProvider)
		{
			const UPropertyBag* DefaultBag = DefaultPropertyBag.GetPropertyBagStruct();
			const UPropertyBag* Bag = PropertyBag.GetPropertyBagStruct();
			if (Bag && DefaultBag)
			{
				const FPropertyBagPropertyDesc* PropertyDesc = Bag->FindPropertyDescByProperty(Property);
				const FPropertyBagPropertyDesc* DefaultPropertyDesc = DefaultBag->FindPropertyDescByProperty(Property);
				if (PropertyDesc
					&& DefaultPropertyDesc
					&& OverrideProvider.IsPropertyOverridden(PropertyDesc->ID))
				{
					UE::StructUtils::Private::CopyPropertyValue(DefaultPropertyDesc, DefaultPropertyBag, PropertyDesc, PropertyBag);
				}
			}
			return true;
		});

	ChildPropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
	ChildPropertyHandle->NotifyFinishedChangingProperties();
}

TSharedRef<SWidget> FPropertyBagInstanceDataDetails::OnPropertyNameContent(TSharedPtr<IPropertyHandle> ChildPropertyHandle, TSharedPtr<SInlineEditableTextBlock> InlineWidget) const
{
	constexpr bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, nullptr);

	auto MoveProperty = [BagStructProperty = BagStructProperty, PropUtils = PropUtils, ChildPropertyHandle](const int32 Delta)
	{
		if (!BagStructProperty || !BagStructProperty->IsValidHandle() || !ChildPropertyHandle || !ChildPropertyHandle->IsValidHandle())
		{
			return;
		}

		UE::StructUtils::Private::ApplyChangesToPropertyDescs(
		LOCTEXT("OnPropertyMoved", "Move Property"), BagStructProperty, PropUtils,
		[&ChildPropertyHandle, &Delta](TArray<FPropertyBagPropertyDesc>& PropertyDescs)
		{
			// Move
			if (PropertyDescs.Num() > 1)
			{
				const FProperty* Property = ChildPropertyHandle ? ChildPropertyHandle->GetProperty() : nullptr;
				const int32 PropertyIndex = PropertyDescs.IndexOfByPredicate([Property](const FPropertyBagPropertyDesc& Desc){ return Desc.CachedProperty == Property; });
				if (PropertyIndex != INDEX_NONE)
				{
					const int32 NewPropertyIndex = FMath::Clamp(PropertyIndex + Delta, 0, PropertyDescs.Num() - 1);
					PropertyDescs.Swap(PropertyIndex, NewPropertyIndex);
				}
			}
		});
	};

	MenuBuilder.AddWidget(
		SNew(SBox)
		.HAlign(HAlign_Right)
		.Padding(FMargin(12, 0, 12, 0))
		[
			UE::StructUtils::CreateTypeSelectionWidget(ChildPropertyHandle,
				BagStructProperty,
				PropUtils,
				SPinTypeSelector::ESelectorType::Full,
				bAllowContainers)
		],
		FText::GetEmpty());

	MenuBuilder.AddSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("Rename", "Rename"),
		LOCTEXT("Rename_ToolTip", "Rename property"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"),
		FUIAction(FExecuteAction::CreateLambda([InlineWidget]()  { InlineWidget->EnterEditingMode(); }))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("Remove", "Remove"),
		LOCTEXT("Remove_ToolTip", "Remove property"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Delete"),
		FUIAction(FExecuteAction::CreateLambda([BagStructProperty = BagStructProperty, PropUtils = PropUtils, ChildPropertyHandle]()
		{
			UE::StructUtils::Private::DeleteProperty(BagStructProperty, ChildPropertyHandle, PropUtils);
		}))
	);

	MenuBuilder.AddSeparator();

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MoveUp", "Move Up"),
		LOCTEXT("MoveUp_ToolTip", "Move property up in the list"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.ArrowUp"),
		FUIAction(FExecuteAction::CreateLambda(MoveProperty, -1))
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MoveDown", "Move Down"),
		LOCTEXT("MoveDown_ToolTip", "Move property down in the list"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.ArrowDown"),
		FUIAction(FExecuteAction::CreateLambda(MoveProperty, +1))
	);

	return MenuBuilder.MakeWidget();
}

////////////////////////////////////

TSharedRef<IPropertyTypeCustomization> FPropertyBagDetails::MakeInstance()
{
	return MakeShared<FPropertyBagDetails>();
}

void FPropertyBagDetails::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	PropUtils = StructCustomizationUtils.GetPropertyUtilities();

	StructProperty = StructPropertyHandle;
	check(StructProperty);

	if (const FProperty* MetaDataProperty = StructProperty->GetMetaDataProperty())
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		bFixedLayout = MetaDataProperty->HasMetaData(UE::StructUtils::Metadata::FixedLayoutName);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		bAllowContainers = MetaDataProperty->HasMetaData(UE::StructUtils::Metadata::AllowContainersName)
			? MetaDataProperty->GetBoolMetaData(UE::StructUtils::Metadata::AllowContainersName)
			: true;

		if (MetaDataProperty->HasMetaData(UE::StructUtils::Metadata::DefaultTypeName))
		{
			if (UEnum* Enum = StaticEnum<EPropertyBagPropertyType>())
			{
				int32 EnumIndex = Enum->GetIndexByNameString(MetaDataProperty->GetMetaData(UE::StructUtils::Metadata::DefaultTypeName));
				if (EnumIndex != INDEX_NONE)
				{
					DefaultType = EPropertyBagPropertyType(Enum->GetValueByIndex(EnumIndex));
				}
			}
		}

		// Load the feature set by the metadata set on the FPropertyBag. Can only accept explicit enum values currently.
		const FString* ChildRowFeaturesString = StructProperty->GetInstanceMetaData(UE::StructUtils::Metadata::ChildRowFeaturesName);
		if (!ChildRowFeaturesString)
		{
			ChildRowFeaturesString = MetaDataProperty->FindMetaData(UE::StructUtils::Metadata::ChildRowFeaturesName);
		}

		if (ChildRowFeaturesString)
		{
			if (const UEnum* Enum = StaticEnum<EPropertyBagChildRowFeatures>())
			{
				const int32 EnumIndex = Enum->GetIndexByNameString(*ChildRowFeaturesString);
				if (EnumIndex != INDEX_NONE)
				{
					ChildRowFeatures = static_cast<EPropertyBagChildRowFeatures>(Enum->GetValueByIndex(EnumIndex));
				}
			}
		}

		// Don't show the header if ShowOnlyInnerProperties is set
		if (MetaDataProperty->HasMetaData(UE::StructUtils::Metadata::ShowOnlyInnerPropertiesName) ||
			StructProperty->GetInstanceMetaData(UE::StructUtils::Metadata::ShowOnlyInnerPropertiesName))
		{
			return;
		}
	}

	TSharedPtr<SWidget> ValueWidget = SNullWidget::NullWidget;
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (!bFixedLayout && ChildRowFeatures != EPropertyBagChildRowFeatures::Fixed)
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		SAssignNew(ValueWidget, SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			MakeAddPropertyWidget(StructProperty, PropUtils, DefaultType).ToSharedRef()
		];
	}

	HeaderRow
		.NameContent()
		[
			StructPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.VAlign(VAlign_Center)
		[
			ValueWidget.ToSharedRef()
		]
		.ShouldAutoExpand(true);
}

void FPropertyBagDetails::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FPropertyBagInstanceDataDetails::FConstructParams Params
	{
		.BagStructProperty = StructProperty,
		.PropUtils = PropUtils,
		.bAllowContainers = bAllowContainers,
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		.ChildRowFeatures = bFixedLayout ? EPropertyBagChildRowFeatures::Fixed : ChildRowFeatures
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	};

	// Show the Value (FInstancedStruct) as child rows.
	const TSharedRef<FPropertyBagInstanceDataDetails> InstanceDetails = MakeShared<FPropertyBagInstanceDataDetails>(Params);
	StructBuilder.AddCustomBuilder(InstanceDetails);
}

TSharedPtr<SWidget> FPropertyBagDetails::MakeAddPropertyWidget(TSharedPtr<IPropertyHandle> InStructProperty, TSharedPtr<IPropertyUtilities> InPropUtils, EPropertyBagPropertyType DefaultType, const FSlateColor IconColor)
{
	return SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("AddProperty_Tooltip", "Add new property"))
			.OnClicked_Lambda([StructProperty = InStructProperty, PropUtils = InPropUtils, DefaultType]()
			{
				constexpr int32 MaxIterations = 100;
				FName NewName(TEXT("NewProperty"));
				int32 Number = 1;
				while (!UE::StructUtils::Private::IsUniqueName(NewName, FName(), StructProperty) && Number < MaxIterations)
				{
					Number++;
					NewName.SetNumber(Number);
				}
				if (Number == MaxIterations)
				{
					return FReply::Handled();
				}

				UE::StructUtils::Private::ApplyChangesToPropertyDescs(
					LOCTEXT("OnPropertyAdded", "Add Property"), StructProperty, PropUtils,
					[&NewName, DefaultType](TArray<FPropertyBagPropertyDesc>& PropertyDescs)
					{
						PropertyDescs.Emplace(NewName, DefaultType);
					});

				return FReply::Handled();

			})
			.Content()
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.PlusCircle"))
				.ColorAndOpacity(IconColor)
			]
		];

}

////////////////////////////////////

bool UPropertyBagSchema::SupportsPinTypeContainer(TWeakPtr<const FEdGraphSchemaAction> SchemaAction,
		const FEdGraphPinType& PinType, const EPinContainerType& ContainerType) const
{
	return ContainerType == EPinContainerType::None || ContainerType == EPinContainerType::Array || ContainerType == EPinContainerType::Set;
}

#undef LOCTEXT_NAMESPACE
