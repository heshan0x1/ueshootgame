// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/Nodes/CustomizableObjectNodeMaterialSwitch.h"

#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCO/CustomizableObjectCustomVersion.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CustomizableObjectNodeMaterialSwitch)

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


FName UCustomizableObjectNodeMaterialSwitch::GetCategory() const
{
	return UEdGraphSchema_CustomizableObject::PC_MeshSection;
}

void UCustomizableObjectNodeMaterialSwitch::BackwardsCompatibleFixup(int32 CustomizableObjectCustomVersion)
{
	Super::BackwardsCompatibleFixup(CustomizableObjectCustomVersion);

	if (CustomizableObjectCustomVersion == FCustomizableObjectCustomVersion::FixMaterialPinsRename)
	{
		const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

		// if there are pins that were manually fixed and re-saved after the FCustomizableObjectCustomVersion::MaterialPinsRename
		// conversion, convert them to the new fixed version.

		// In the new fix we only change the friendy name, but the actual name remains "material..."

		// Output
		UEdGraphPin* OldPin = FindPin(TEXT("Mesh Section"),EEdGraphPinDirection::EGPD_Output);
		if (OldPin)
		{
			UEdGraphPin* NewPin = FindPin(TEXT("Material"), EEdGraphPinDirection::EGPD_Output);
			if (!NewPin)
			{
				NewPin = CustomCreatePin(EGPD_Output, Schema->PC_MeshSection, FName("Material"));
			}

			FGuid PinId = NewPin->PinId;
			NewPin->CopyPersistentDataFromOldPin(*OldPin);
			NewPin->PinId = PinId;
			NewPin->bHidden = OldPin->bHidden;

			OutputPinReference = NewPin;

			CustomRemovePin(*OldPin);
		}

		// Inputs
		int32 InputCount = ReloadingElementsNames.Num();
		for (int32 InputIndex=0; InputIndex<InputCount; ++InputIndex)
		{
			OldPin = FindPin(FString::Printf(TEXT("Mesh Section %d "),InputIndex), EEdGraphPinDirection::EGPD_Input);
			if (OldPin)
			{
				FString NewName = FString::Printf(TEXT("Material %d "), InputIndex);
				UEdGraphPin* NewPin = FindPin(NewName, EEdGraphPinDirection::EGPD_Input);
				if (!NewPin)
				{
					NewPin = CustomCreatePin(EEdGraphPinDirection::EGPD_Input, Schema->PC_MeshSection, FName(*NewName));
				}

				FGuid PinId = NewPin->PinId;
				NewPin->CopyPersistentDataFromOldPin(*OldPin);
				NewPin->PinId = PinId;
				NewPin->bHidden = OldPin->bHidden;

				CustomRemovePin(*OldPin);
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE

