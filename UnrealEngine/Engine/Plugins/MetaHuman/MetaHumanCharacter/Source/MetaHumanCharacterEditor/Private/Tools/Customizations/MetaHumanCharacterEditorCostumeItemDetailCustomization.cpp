// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterEditorCostumeItemDetailCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Engine/Texture2D.h"
#include "Item/MetaHumanMaterialPipelineCommon.h"
#include "SEnumCombo.h"
#include "UI/Widgets/SUVColorPicker.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorCostumeItemDetailCustomization"

namespace UE::MetaHuman::Private
{
	static const FName ColorPickerPropertyTag = TEXT("ColorPickerID");
	static const FName ColorPickerChannelTag = TEXT("ColorPickerChannel");
	static const FName ColorPickerTextureTag = TEXT("ColorPickerTexture");
}

void FCostumeParametersOverridesDetails::OnChildRowAdded(IDetailPropertyRow& ChildRow)
{
	const TSharedPtr<IPropertyHandle> PropertyHandle = ChildRow.GetPropertyHandle();
	if (!PropertyHandle.IsValid() || !PropertyHandle->GetProperty())
	{
		return;
	}

	const FName Name = PropertyHandle->GetProperty()->GetFName();
	if (Name == TEXT("HighlightsVariation"))
	{
		ChildRow.CustomWidget()
			.NameContent()
			[
				PropertyHandle->CreatePropertyNameWidget()
			]
			.ValueContent()
			[
				MakeHighlightsVariationWidget(PropertyHandle.ToSharedRef())
			];
	}

	HandleUVColorPickerProperties(PropertyHandle.ToSharedRef(), ChildRow);
}

TSharedRef<SWidget> FCostumeParametersOverridesDetails::MakeHighlightsVariationWidget(const TSharedRef<IPropertyHandle> PropertyHandle) const
{
	const FStructProperty* InstanceBagProperty = CastField<FStructProperty>(BagStructProperty->GetProperty());
	FInstancedPropertyBag* SourcePropertyBag = InstanceBagProperty ? InstanceBagProperty->ContainerPtrToValuePtr<FInstancedPropertyBag>(Item.Get()) : nullptr;
	const void* PropertyBagContainerAddress = SourcePropertyBag ? SourcePropertyBag->GetValue().GetMemory() : nullptr;

	const UEnum* Enum = StaticEnum<EMetaHumanCharacterEditorHighlightsVariation>();

	return
		SNew(SEnumComboBox, Enum)
		.CurrentValue_Lambda([PropertyHandle, PropertyBagContainerAddress]()
			{
				int32 PropertyValue = 0;
				const FNumericProperty* Property = CastField<FNumericProperty>(PropertyHandle->GetProperty());
				if (Property && PropertyBagContainerAddress)
				{
					const float* ValuePtr = Property->ContainerPtrToValuePtr<float>(PropertyBagContainerAddress);
					const float Value = Property->GetFloatingPointPropertyValue(ValuePtr);
					PropertyValue = FMath::FloorToInt(Value);
				}

				return PropertyValue;
			})
		.OnEnumSelectionChanged_Lambda([PropertyHandle, PropertyBagContainerAddress](int32 NewValue, ESelectInfo::Type SelectInfo)
			{
				FNumericProperty* Property = CastField<FNumericProperty>(PropertyHandle->GetProperty());
				if (Property && PropertyBagContainerAddress)
				{
					float* ValuePtr = Property->ContainerPtrToValuePtr<float>(const_cast<void*>(PropertyBagContainerAddress));
					Property->SetFloatingPointPropertyValue(ValuePtr, (float)NewValue);
					PropertyHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
				}
			});
}

void FCostumeParametersOverridesDetails::HandleUVColorPickerProperties(const TSharedRef<IPropertyHandle> PropertyHandle, IDetailPropertyRow& ChildRow)
{
	if (!PropertyHandle->GetProperty())
	{
		return;
	}

	using namespace UE::MetaHuman::Private;
	const TMap<FName, FString> MetadataMap = UE::MetaHuman::MaterialUtils::CopyMetadataFromProperty(TNotNull<FProperty*>(PropertyHandle->GetProperty()));
	if (!MetadataMap.Contains(ColorPickerPropertyTag) || 
		!MetadataMap.Contains(ColorPickerChannelTag) ||
		!MetadataMap.Contains(ColorPickerTextureTag))
	{
		return;
	}

	const FString& TextureTag = MetadataMap.FindChecked(ColorPickerTextureTag);
	const FString AssetName = FPackageName::GetLongPackageAssetName(TextureTag);
	const FString TexturePath = TEXT("/Script/Engine.Texture2D'") + TextureTag + TEXT(".") + AssetName + TEXT("'");
	UTexture2D* Texture = LoadObject<UTexture2D>(nullptr, TexturePath);
	if (!Texture)
	{
		return;
	}

	const FString& PropertyTag = MetadataMap.FindChecked(ColorPickerPropertyTag);
	if (UVPropertyName.IsNone())
	{
		UVPropertyName = *PropertyTag;
	}

	const FString& ChannelTag = MetadataMap.FindChecked(ColorPickerChannelTag);
	if (ChannelTag == TEXT("U") && UVPropertyName == PropertyTag)
	{
		UPropertyHandle = PropertyHandle;
		UPropertyRow = &ChildRow;
		TryMakeUVColorPicker(TNotNull<UTexture2D*>(Texture));
	}
	else if (ChannelTag == TEXT("V") && UVPropertyName == PropertyTag)
	{
		VPropertyHandle = PropertyHandle;
		VPropertyRow = &ChildRow;
		TryMakeUVColorPicker(TNotNull<UTexture2D*>(Texture));
	}
}

void FCostumeParametersOverridesDetails::TryMakeUVColorPicker(TNotNull<UTexture2D*> ColorPickerTexture)
{
	if (!BagStructProperty.IsValid() || !Item.IsValid() || 
		!UPropertyHandle.IsValid() || !UPropertyHandle->GetProperty() ||
		!VPropertyHandle.IsValid() || !VPropertyHandle->GetProperty() ||
		!UPropertyRow || !VPropertyRow)
	{
		return;
	}

	const TTuple<TSharedPtr<IPropertyHandle>, TSharedPtr<IPropertyHandle>> UVHandles(UPropertyHandle, VPropertyHandle);
	UVPropertyNameToHandlesMap.Add(UVPropertyName, UVHandles);

	const FStructProperty* InstanceBagProperty = CastField<FStructProperty>(BagStructProperty->GetProperty());
	FInstancedPropertyBag* SourcePropertyBag = InstanceBagProperty ? InstanceBagProperty->ContainerPtrToValuePtr<FInstancedPropertyBag>(Item.Get()) : nullptr;
	const void* PropertyBagContainerAddress = SourcePropertyBag ? SourcePropertyBag->GetValue().GetMemory() : nullptr;

	UPropertyRow->CustomWidget()
		.NameContent()
		[
			SNew(STextBlock)
			.Text(FText::FromName(UVPropertyName))
			.Font(FAppStyle::Get().GetFontStyle("PropertyWindow.NormalFont"))
		]
		.ValueContent()
		.MaxDesiredWidth(160.f)
		[
			SNew(SUVColorPicker)
			.ULabelOverride(UPropertyHandle->GetPropertyDisplayName())
			.VLabelOverride(VPropertyHandle->GetPropertyDisplayName())
			.ColorPickerTexture(ColorPickerTexture)
			.UV(this, &FCostumeParametersOverridesDetails::GetPropertyUV, UVPropertyName, PropertyBagContainerAddress)
			.OnUVChanged(this, &FCostumeParametersOverridesDetails::OnPropertyUVChanged, UVPropertyName, const_cast<void*>(PropertyBagContainerAddress))
		];

	VPropertyHandle->MarkHiddenByCustomization();
	VPropertyRow->Visibility(EVisibility::Collapsed);

	UVPropertyName = NAME_None;
	UPropertyHandle = nullptr;
	VPropertyHandle = nullptr;
	UPropertyRow = nullptr;
	VPropertyRow = nullptr;
}

FVector2f FCostumeParametersOverridesDetails::GetPropertyUV(FName PropertyName, const void* PropertyBagContainerAddress) const
{
	FVector2f UVColorValue = FVector2f(0.f, 0.f);
	if (!UVPropertyNameToHandlesMap.Contains(PropertyName))
	{
		return UVColorValue;
	}

	const TTuple<TSharedPtr<IPropertyHandle>, TSharedPtr<IPropertyHandle>>& UVHandles = UVPropertyNameToHandlesMap.FindChecked(PropertyName);
	const TSharedPtr<IPropertyHandle> UHandle = UVHandles.Key;
	const TSharedPtr<IPropertyHandle> VHandle = UVHandles.Value;

	const FNumericProperty* UProperty = UHandle.IsValid() ? CastField<FNumericProperty>(UHandle->GetProperty()) : nullptr;
	const FNumericProperty* VProperty = VHandle.IsValid() ? CastField<FNumericProperty>(VHandle->GetProperty()) : nullptr;
	if (UProperty && VProperty && PropertyBagContainerAddress)
	{
		const float* UValuePtr = UProperty->ContainerPtrToValuePtr<float>(PropertyBagContainerAddress);
		const float UValue = UProperty->GetFloatingPointPropertyValue(UValuePtr);

		const float* VValuePtr = VProperty->ContainerPtrToValuePtr<float>(PropertyBagContainerAddress);
		const float VValue = VProperty->GetFloatingPointPropertyValue(VValuePtr);

		UVColorValue = FVector2f(UValue, VValue);
	}

	return UVColorValue;
}

void FCostumeParametersOverridesDetails::OnPropertyUVChanged(const FVector2f& InUV, bool bIsDragging, FName PropertyName, void* PropertyBagContainerAddress)
{
	if (!UVPropertyNameToHandlesMap.Contains(PropertyName))
	{
		return;
	}

	const TTuple<TSharedPtr<IPropertyHandle>, TSharedPtr<IPropertyHandle>> UVHandles = UVPropertyNameToHandlesMap.FindChecked(PropertyName);
	const TSharedPtr<IPropertyHandle> UHandle = UVHandles.Key;
	const TSharedPtr<IPropertyHandle> VHandle = UVHandles.Value;

	FNumericProperty* UProperty = UHandle.IsValid() ? CastField<FNumericProperty>(UHandle->GetProperty()) : nullptr;
	FNumericProperty* VProperty = VHandle.IsValid() ? CastField<FNumericProperty>(VHandle->GetProperty()) : nullptr;
	if (UProperty && VProperty && PropertyBagContainerAddress)
	{
		using namespace UE::MetaHuman::Private;

		// U
		const TMap<FName, FString> UMetadataMap = UE::MetaHuman::MaterialUtils::CopyMetadataFromProperty(TNotNull<FProperty*>(UProperty));
		const float UMinValue = UMetadataMap.Contains(TEXT("UIMin")) ? FCString::Atof(*UMetadataMap.FindChecked(TEXT("UIMin"))) : 0.f;
		const float UMaxValue = UMetadataMap.Contains(TEXT("UIMax")) ? FCString::Atof(*UMetadataMap.FindChecked(TEXT("UIMax"))) : 1.f;
		const float UClampedValue = FMath::Clamp(InUV.X, UMinValue, UMaxValue);
		float* UValuePtr = UProperty->ContainerPtrToValuePtr<float>(const_cast<void*>(PropertyBagContainerAddress));
		UProperty->SetFloatingPointPropertyValue(UValuePtr, UClampedValue);

		// V
		const TMap<FName, FString> VMetadataMap = UE::MetaHuman::MaterialUtils::CopyMetadataFromProperty(TNotNull<FProperty*>(VProperty));
		const float VMinValue = VMetadataMap.Contains(TEXT("UIMin")) ? FCString::Atof(*VMetadataMap.FindChecked(TEXT("UIMin"))) : 0.f;
		const float VMaxValue = VMetadataMap.Contains(TEXT("UIMax")) ? FCString::Atof(*VMetadataMap.FindChecked(TEXT("UIMax"))) : 1.f;
		const float VClampedValue = FMath::Clamp(InUV.Y, VMinValue, VMaxValue);
		float* VValuePtr = VProperty->ContainerPtrToValuePtr<float>(const_cast<void*>(PropertyBagContainerAddress));
		VProperty->SetFloatingPointPropertyValue(VValuePtr, VClampedValue);

		const EPropertyChangeType::Type PropertyFlags = bIsDragging ? EPropertyChangeType::Interactive : EPropertyChangeType::ValueSet;
		UHandle->NotifyPostChange(PropertyFlags);
		VHandle->NotifyPostChange(PropertyFlags);
	}
}

void FMetaHumanCharacterEditorCostumeItemDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	InDetailLayout.GetObjectsBeingCustomized(Objects);
	if (Objects.IsEmpty())
	{
		return;
	}

	UMetaHumanCharacterEditorCostumeItem* Item = Cast<UMetaHumanCharacterEditorCostumeItem>(Objects[0].Get());
	if (Item)
	{
		const TSharedRef<IPropertyHandle> InstanceParametersHandle = InDetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UMetaHumanCharacterEditorCostumeItem, InstanceParameters));
		InstanceParametersHandle->MarkHiddenByCustomization();

		IDetailCategoryBuilder& OverridesCategory = InDetailLayout.EditCategory(TEXT("Overrides"));
		const TSharedRef<FCostumeParametersOverridesDetails> OverridesDetails = MakeShared<FCostumeParametersOverridesDetails>(InstanceParametersHandle, InDetailLayout.GetPropertyUtilities(), Item);
		OverridesCategory.AddCustomBuilder(OverridesDetails);
	}
}

#undef LOCTEXT_NAMESPACE
