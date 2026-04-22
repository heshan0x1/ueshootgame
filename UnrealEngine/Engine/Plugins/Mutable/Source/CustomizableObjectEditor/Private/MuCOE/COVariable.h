// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"

#include "COVariable.generated.h"


class UCustomizableObjectNode;
class SWidget;

namespace ETextCommit { enum Type : int; }

/** This struct can beused to add new variables to nodes. */

USTRUCT()
struct FCOVariable
{
	GENERATED_USTRUCT_BODY()

	/** Name of this variable. Can be eddited from any details. */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	FName Name;

	/** The type of this variable. Ususally a pin category */
	UPROPERTY(EditAnywhere, Category = CustomizableObject)
	FName Type;

	/** Id to identify this variable. */
	UPROPERTY()
	FGuid Id;

};


class FCOVariableCustomzation: public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	// IPropertyTypeCustomization interface
	void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override {};

private:

	/** Returns the name of the specified variable. Needed to update automatically the text if the name of the variable changes. */
	FText GetVariableName() const;

	void ResetToDefault();

	/** Editable Text Box Callbacks */
	void OnVariableNameChanged(const FText& InNewText);
	void OnVariableNameCommitted(const FText& InNewText, ETextCommit::Type InTextCommit);

	/** ComboBox Callbacks */
	TSharedRef<SWidget> OnGenerateVariableTypeRow(FName Option);
	TSharedRef<SWidget> GenerateCurrentSelectectedTypeWidget();

private:

	/** Weak pointer to the Customizable Object node that contains this property */
	TWeakObjectPtr<UCustomizableObjectNode> BaseObjectNode;

	TArray<FName> AllowedVariableTypes;

	/** Property handle of the variable name */
	TSharedPtr<IPropertyHandle> NamePropertyHandle;

	/** Property handle of the variable type */
	TSharedPtr<IPropertyHandle> TypePropertyHandle;

};