// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariableReferencePropertyCustomization.h"

#include "PropertyHandle.h"
#include "DetailWidgetRow.h"
#include "IPropertyUtilities.h"
#include "SVariablePickerCombo.h"
#include "UncookedOnlyUtils.h"
#include "Param/ParamCompatibility.h"
#include "Param/ParamUtils.h"
#include "RigVMCore/RigVMRegistry.h"
#include "Variables/AnimNextVariableReference.h"
#include "Variables/VariablePickerArgs.h"
#include "AnimNextRigVMAsset.h"

#define LOCTEXT_NAMESPACE "VariableReferencePropertyCustomization"

namespace UE::UAF::Editor
{

void FVariableReferencePropertyCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	PropertyHandle = InPropertyHandle;

	const FString AllowedType = PropertyHandle->GetMetaData("AllowedType");
	FilterType = FAnimNextParamType::FromString(AllowedType);

	bIsContextSensitive = !PropertyHandle->HasMetaData("ShowAll");

	TArray<UObject*> Objects;
	InPropertyHandle->GetOuterObjects(Objects);
	for (UObject* Object : Objects)
	{
		if (UAnimNextRigVMAsset* Asset = Object->GetTypedOuter<UAnimNextRigVMAsset>())
		{
			FilterAssets.Add(Asset);
		}
	}

	FVariablePickerArgs Args;
	Args.OnVariablePicked = FOnVariablePicked::CreateLambda([this](const FAnimNextSoftVariableReference& InVariableReference, const FAnimNextParamType& InType)
	{
		PropertyHandle->NotifyPreChange();

		PropertyHandle->EnumerateRawData([this, &InVariableReference](void* RawData, const int32 DataIndex, const int32 NumDatas)
		{
			SetValue(InVariableReference, RawData);
			return true;
		});

		PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
		PropertyHandle->NotifyFinishedChangingProperties();

		UpdateCachedData();
	});

	Args.OnFilterVariableType = FOnFilterVariableType::CreateLambda([this](const FAnimNextParamType& InParamType)-> EFilterVariableResult
	{
		if(FilterType.IsValid())
		{
			if(!FParamUtils::GetCompatibility(FilterType, InParamType).IsCompatibleWithDataLoss())
			{
				return EFilterVariableResult::Exclude;
			}
		}

		if(InParamType.IsValid())
		{
			const FRigVMTemplateArgumentType RigVMType = InParamType.ToRigVMTemplateArgument();
			if(!RigVMType.IsValid() || FRigVMRegistry::Get().GetTypeIndex(RigVMType) == INDEX_NONE)
			{
				return EFilterVariableResult::Exclude;
			}
		}

		return EFilterVariableResult::Include;
	});

	if (FilterAssets.Num() > 0)
	{
		OnIsContextSensitiveDelegate = FOnIsContextSensitive::CreateLambda([this]()
		{
			return bIsContextSensitive;
		});

		Args.OnIsContextSensitive = &OnIsContextSensitiveDelegate;
		Args.OnContextSensitivityChanged = FOnContextSensitivityChanged::CreateLambda([this](bool bInIsContextSensitive)
		{
			bIsContextSensitive = bInIsContextSensitive;
		});
	}

	Args.OnFilterVariable = FOnFilterVariable::CreateLambda([this](const FAnimNextSoftVariableReference& InVariableReference)
	{
		if (FilterAssets.Num() == 0 || !bIsContextSensitive)
		{
			return EFilterVariableResult::Include;
		}

		for (TWeakObjectPtr<UAnimNextRigVMAsset> WeakFilterAsset : FilterAssets)
		{
			if (const UAnimNextRigVMAsset* FilterAsset = WeakFilterAsset.Get())
			{
				FSoftObjectPath FilterAssetPath(FilterAsset);
				if (InVariableReference.GetSoftObjectPath() == FilterAssetPath)
				{
					return EFilterVariableResult::Include;
				}
			}
		}

		return EFilterVariableResult::Exclude;
	});

	InHeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SVariablePickerCombo)
		.PickerArgs(Args)
		.VariableName_Lambda([this]()
		{
			return bMultipleValues ? LOCTEXT("MultipleValues", "Multiple Values") : FText::FromName(CachedVariableReference.GetName());
		})
		.VariableTooltip_Lambda([this]()
		{
			return bMultipleValues ? LOCTEXT("MultipleValues", "Multiple Values") : FText::Format(LOCTEXT("VariableTooltipFormat", "{0}\n{1}"), FText::FromName(CachedVariableReference.GetName()), FText::FromString(CachedVariableReference.GetSoftObjectPath().ToString()));
		})
		.OnGetVariableReference_Lambda([this]()
		{
			return CachedVariableReference;
		})
		.OnGetVariableType_Lambda([this]()
		{
			return CachedType;
		})
	];

	UpdateCachedData();
}

void FVariableReferencePropertyCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
}

void FVariableReferencePropertyCustomization::SetValue(const FAnimNextSoftVariableReference& InVariableReference, void* InValue) const
{
	FAnimNextVariableReference& VariableReference = *static_cast<FAnimNextVariableReference*>(InValue);
	VariableReference = FAnimNextVariableReference(InVariableReference);
}

FAnimNextSoftVariableReference FVariableReferencePropertyCustomization::GetValue(const void* InValue) const
{
	return FAnimNextSoftVariableReference(*static_cast<const FAnimNextVariableReference*>(InValue));
}

void FVariableReferencePropertyCustomization::UpdateCachedData()
{
	bMultipleValues = false;
	CachedVariableReference = FAnimNextSoftVariableReference();
	CachedType = FAnimNextParamType();

	TOptional<FAnimNextParamType> CommonType;
	TOptional<FAnimNextSoftVariableReference> CommonVariableReference;
	PropertyHandle->EnumerateConstRawData([this, &CommonType, &CommonVariableReference](const void* RawData, const int32 DataIndex, const int32 NumDatas)
	{
		FAnimNextSoftVariableReference VariableReference = GetValue(RawData);
		if(!CommonVariableReference.IsSet())
		{
			CommonVariableReference = VariableReference;
		}
		else if(CommonVariableReference.GetValue() != VariableReference)
		{
			// No common variable reference
			CommonVariableReference = FAnimNextSoftVariableReference();
		}

		FAnimNextParamType Type = UncookedOnly::FUtils::FindVariableType(VariableReference);
		if(!CommonType.IsSet())
		{
			CommonType = Type;
		}
		else if(CommonType.GetValue() != Type)
		{
			// No common type
			CommonType = FAnimNextParamType();
		}
		return true;
	});

	if (CommonVariableReference.IsSet())
	{
		CachedVariableReference = CommonVariableReference.GetValue();
	}
	else
	{
		bMultipleValues = true;
	}

	if (CommonType.IsSet())
	{
		CachedType = CommonType.GetValue();
	}
}

void FSoftVariableReferencePropertyCustomization::SetValue(const FAnimNextSoftVariableReference& InVariableReference, void* InValue) const
{
	FAnimNextSoftVariableReference& VariableReference = *static_cast<FAnimNextSoftVariableReference*>(InValue);
	VariableReference = InVariableReference;
}

FAnimNextSoftVariableReference FSoftVariableReferencePropertyCustomization::GetValue(const void* InValue) const
{
	return *static_cast<const FAnimNextSoftVariableReference*>(InValue);
}

}

#undef LOCTEXT_NAMESPACE