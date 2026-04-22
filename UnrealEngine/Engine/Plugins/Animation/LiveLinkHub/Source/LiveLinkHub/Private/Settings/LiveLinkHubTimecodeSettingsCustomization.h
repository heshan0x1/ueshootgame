// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "LiveLinkHubMessages.h"
#include "PropertyHandle.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "LiveLinkHubTimecodeSettingsCustomization"

/**
 * Customization for the Timecode settings.
 */
class FLiveLinkHubTimecodeSettingsCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShared<FLiveLinkHubTimecodeSettingsCustomization>();
	}

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
	{
	}

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		uint32 NumberOfChild;
		if (PropertyHandle->GetNumChildren(NumberOfChild) == FPropertyAccess::Success)
		{
			for (uint32 Index = 0; Index < NumberOfChild; ++Index)
			{
				TSharedRef<IPropertyHandle> ChildPropertyHandle = PropertyHandle->GetChildHandle(Index).ToSharedRef();

				if (ChildPropertyHandle->GetProperty()->GetFName() == GET_MEMBER_NAME_CHECKED(FLiveLinkHubTimecodeSettings, EvaluationType))
				{
					TSharedPtr<IPropertyHandle> OverrideHandle = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLiveLinkHubTimecodeSettings, OverrideEvaluationType));

					TArray<UObject*> Objects;
					PropertyHandle->GetOuterObjects(Objects);

					if (Objects.Num() == 0)
					{
						continue;
					}

					uint8* BaseAddress = PropertyHandle->GetValueBaseAddress((uint8*)Objects[0]);
					FProperty* Property = FLiveLinkHubTimecodeSettings::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FLiveLinkHubTimecodeSettings, OverrideEvaluationType));

					TOptional<ELiveLinkTimecodeProviderEvaluationType> OptionalOverride;
					Property->GetValue_InContainer(BaseAddress, &OptionalOverride);
						
					if (OptionalOverride.IsSet())
					{
						// Override the EvaluationType dropdown to display the override value in a read-only widget.
						FText OverrideText = StaticEnum<ELiveLinkTimecodeProviderEvaluationType>()->GetDisplayNameTextByValue((int64)OptionalOverride.GetValue());

						ChildBuilder.AddCustomRow(ChildPropertyHandle->GetPropertyDisplayName())
						.NameContent()
						[
							ChildPropertyHandle->CreatePropertyNameWidget()
						]
						.ValueContent()
						[
							SNew(STextBlock)
								.Text(FText::Format(LOCTEXT("EvaluationTypeOverriddenProperty", "{0} (Overridden in playback)"), OverrideText))
						];
					}
					else
					{
						ChildBuilder.AddProperty(ChildPropertyHandle);
					}
				}
				else
				{
					ChildBuilder.AddProperty(ChildPropertyHandle);
				}
			}
		}
	}
};

#undef LOCTEXT_NAMESPACE
