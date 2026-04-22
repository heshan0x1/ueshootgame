// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCollectionBlueprintLibrary.h"

#include "MetaHumanCharacterInstance.h"
#include "MetaHumanCharacterPaletteEditorLog.h"

#include "Logging/StructuredLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanCollectionBlueprintLibrary)

namespace UE::MetaHuman::Private
{
	/**
	 * @brief Template function to get a value of an instance parameter from a property bag
	 * 
	 * @tparam ParamType the type of parameter to get
	 * @param InInstanceParam the instance parameter to get the parameter value for
	 * @param OutValue The value of the parameter
	 * @param InGetFunc a pointer to a Get function that can be called on a FInstancedPropertyBag object
	 * @returns true if the value can be obtained, false otherwise
	 */
	template<typename ParamType, typename PropertyBagGetFunc>
	bool GetInstanceParam(const FMetaHumanCharacterInstanceParameter& InInstanceParam, ParamType& OutValue, PropertyBagGetFunc InGetFunc)
	{
		if (!InInstanceParam.Instance.IsValid())
		{
			UE_LOGFMT(LogMetaHumanCharacterPaletteEditor, Error, "GetInstanceParam called with invalid character instance");
			return false;
		}

		FInstancedPropertyBag PropertyBag = InInstanceParam.Instance->GetCurrentInstanceParametersForItem(InInstanceParam.ItemPath);

		if (!PropertyBag.IsValid())
		{
			UE_LOGFMT(LogMetaHumanCharacterPaletteEditor,
					  Error,
					  "Failed to find parameters for item '{Item}' in character instance '{Instance}'",
					  InInstanceParam.ItemPath.ToDebugString(),
					  InInstanceParam.Instance->GetPathName());
			return false;
		}

		// const TValueOrError<ParamType, EPropertyBagResult> Result = (PropertyBag.*InGetFunc)(InInstanceParam.Name);
		const TValueOrError<ParamType, EPropertyBagResult> Result = InGetFunc(PropertyBag, InInstanceParam.Name);

		if (Result.HasError())
		{
			const EPropertyBagResult Error = Result.GetError();
			UE_LOGFMT(LogMetaHumanCharacterPaletteEditor,
					  Error,
					  "Failed to get '{Param}' of type '{Type}' for item '{Item}' in character instance '{Instance}: {Error}'",
					  InInstanceParam.Name.ToString(),
					  UEnum::GetDisplayValueAsText(InInstanceParam.Type).ToString(),
					  InInstanceParam.ItemPath.ToDebugString(),
					  InInstanceParam.Instance->GetPathName(),
					  UEnum::GetDisplayValueAsText(Error).ToString());
			return false;
		}

		OutValue = Result.GetValue();

		return true;
	}

	/**
	 * @brief Template function to set a value of an instance parameter to a property bag and apply it back to the character instance
	 * 
	 * @param InInstanceParam The instance parameter to set the value for
	 * @param InValue The value to be set
	 * @param InSetFunc Pointer to a Set function that can be called on a FInstancedPropertyBag object
	 * @return true if the value was set and false otherwise
	 */
	template<typename ParamType, typename PropertyBagSetFunc>
	bool SetInstanceParam(const FMetaHumanCharacterInstanceParameter& InInstanceParam, ParamType InValue, PropertyBagSetFunc InSetFunc)
	{
		if (!InInstanceParam.Instance.IsValid())
		{
			UE_LOGFMT(LogMetaHumanCharacterPaletteEditor, Error, "SetInstanceParam called with invalid character instance");
			return false;
		}

		FInstancedPropertyBag PropertyBag = InInstanceParam.Instance->GetCurrentInstanceParametersForItem(InInstanceParam.ItemPath);

		if (!PropertyBag.IsValid())
		{
			UE_LOGFMT(LogMetaHumanCharacterPaletteEditor,
					  Error,
					  "Failed to find parameters for item '{Item}' in character instance '{Instance}'",
					  InInstanceParam.ItemPath.ToDebugString(),
					  InInstanceParam.Instance->GetPathName());
			return false;
		}

		// Calls the setter function in the property bag instance
		// const EPropertyBagResult Result = (PropertyBag.*InSetFunc)(InInstanceParam.Name, InValue);
		const EPropertyBagResult Result = InSetFunc(PropertyBag, InInstanceParam.Name, InValue);

		if (Result != EPropertyBagResult::Success)
		{
			UE_LOGFMT(LogMetaHumanCharacterPaletteEditor,
					  Error,
					  "Failed to set '{Param}' of type '{Type}' for item '{Item}' in character instance '{Instance}: {Error}'",
					  InInstanceParam.Name.ToString(),
					  UEnum::GetDisplayValueAsText(InInstanceParam.Type).ToString(),
					  InInstanceParam.ItemPath.ToDebugString(),
					  InInstanceParam.Instance->GetPathName(),
					  UEnum::GetDisplayValueAsText(Result).ToString());
			return false;
		}

		InInstanceParam.Instance->OverrideInstanceParameters(InInstanceParam.ItemPath, PropertyBag);

		return true;
	}
}

bool UMetaHumanPaletteKeyBlueprintLibrary::ReferencesSameAsset(const FMetaHumanPaletteItemKey& InKey, const FMetaHumanPaletteItemKey& InOther)
{
	return InKey.ReferencesSameAsset(InOther);
}

FString UMetaHumanPaletteKeyBlueprintLibrary::ToAssetNameString(const FMetaHumanPaletteItemKey& InKey)
{
	return InKey.ToAssetNameString();
}

FMetaHumanPaletteItemPath UMetaHumanPaletteItemPathBlueprintLibrary::MakeItemPath(const FMetaHumanPaletteItemKey& InItemKey)
{
	return FMetaHumanPaletteItemPath{ InItemKey };
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::GetBool(const FMetaHumanCharacterInstanceParameter& InInstanceParam, bool& OutValue)
{
	using namespace UE::MetaHuman::Private;
	return GetInstanceParam(InInstanceParam, OutValue, UE_PROJECTION_MEMBER(FInstancedPropertyBag, GetValueBool));
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::SetBool(const FMetaHumanCharacterInstanceParameter& InInstanceParam, bool InValue)
{
	using namespace UE::MetaHuman::Private;
	return SetInstanceParam(InInstanceParam, InValue, UE_PROJECTION_MEMBER(FInstancedPropertyBag, SetValueBool));
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::GetFloat(const FMetaHumanCharacterInstanceParameter& InInstanceParam, float& OutValue)
{
	using namespace UE::MetaHuman::Private;
	return GetInstanceParam(InInstanceParam, OutValue, UE_PROJECTION_MEMBER(FInstancedPropertyBag, GetValueFloat));
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::SetFloat(const FMetaHumanCharacterInstanceParameter& InInstanceParam, float InValue)
{
	using namespace UE::MetaHuman::Private;
	return SetInstanceParam(InInstanceParam, InValue, UE_PROJECTION_MEMBER(FInstancedPropertyBag, SetValueFloat));
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::GetColor(const FMetaHumanCharacterInstanceParameter& InInstanceParam, FLinearColor& OutColor)
{
	using namespace UE::MetaHuman::Private;

	FLinearColor* ColorPtr = nullptr;
	if (!GetInstanceParam(InInstanceParam, ColorPtr, UE_PROJECTION_MEMBER(FInstancedPropertyBag, template GetValueStruct<FLinearColor>)))
	{	
		return false;
	}

	// This should never happen, so make it a hard error in case the pointer is ever nullptr
	check(ColorPtr);
	
	OutColor = *ColorPtr;

	return true;
}

bool UMetaHumanCharacterInstanceParameterBlueprintLibrary::SetColor(const FMetaHumanCharacterInstanceParameter& InInstanceParam, const FLinearColor& InColor)
{
	using namespace UE::MetaHuman::Private;
	return SetInstanceParam(InInstanceParam, InColor, UE_PROJECTION_MEMBER(FInstancedPropertyBag, template SetValueStruct<FLinearColor>));
}

FMetaHumanPaletteItemPath UMetaHumanPipelineSlotSelectionBlueprintLibrary::GetSelectedItemPath(const FMetaHumanPipelineSlotSelection& InSlotSelection)
{
	return InSlotSelection.GetSelectedItemPath();
}

TArray<FMetaHumanCharacterInstanceParameter> UMetaHumanCharacterInstanceBlueprintLibrary::GetInstanceParameters(UMetaHumanCharacterInstance* InInstance, const FMetaHumanPaletteItemPath& ItemPath)
{
	if (!IsValid(InInstance))
	{
		UE_LOGFMT(LogMetaHumanCharacterPaletteEditor, Error, "GetInstanceParameters called with an invalid character instance");
		return {};
	}
	
	FInstancedPropertyBag InstanceParametersBag = InInstance->GetCurrentInstanceParametersForItem(ItemPath);

	if (!InstanceParametersBag.IsValid())
	{
		UE_LOGFMT(LogMetaHumanCharacterPaletteEditor,
				  Error, "Failed to find parameters for item '{Item}' in character instance '{Instance}'",
				  ItemPath.ToDebugString(),
				  InInstance->GetPathName());
		return {};
	}

	const UPropertyBag* PropertyBagStruct = InstanceParametersBag.GetPropertyBagStruct();

	TArray<FMetaHumanCharacterInstanceParameter> InstanceParameters;
	InstanceParameters.Reserve(InstanceParametersBag.GetNumPropertiesInBag());

	for (const FPropertyBagPropertyDesc& PropertyDesc : PropertyBagStruct->GetPropertyDescs())
	{
		if (PropertyDesc.ValueType != EPropertyBagPropertyType::Bool &&
			PropertyDesc.ValueType != EPropertyBagPropertyType::Float &&
			PropertyDesc.ValueType != EPropertyBagPropertyType::Struct)
		{
			UE_LOGFMT(LogMetaHumanCharacterPaletteEditor,
					  Warning,
					  "Property '{Property}' of item '{Item}' is of an unsupported type. Make sure '{Type}' is defined in EMetaHumanCharacterInstanceParameterType",
					  PropertyDesc.Name.ToString(),
					  ItemPath.ToDebugString(),
					  UEnum::GetDisplayValueAsText(PropertyDesc.ValueType).ToString());
			continue;
		}

		FMetaHumanCharacterInstanceParameter& InstanceParam = InstanceParameters.AddDefaulted_GetRef();
		InstanceParam.Name = PropertyDesc.Name;
		InstanceParam.Type = (EMetaHumanCharacterInstanceParameterType) PropertyDesc.ValueType;
		InstanceParam.ItemPath = ItemPath;
		InstanceParam.Instance = InInstance;
	}

	return InstanceParameters;
}