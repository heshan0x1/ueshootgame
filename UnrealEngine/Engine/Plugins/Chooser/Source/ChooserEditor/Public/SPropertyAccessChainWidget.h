// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IPropertyAccessEditor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Widgets/SCompoundWidget.h"
#include "IHasContext.h"
#include "ChooserPropertyAccess.h"

#define UE_API CHOOSEREDITOR_API

namespace UE::ChooserEditor
{
DECLARE_DELEGATE(FPropertyAccessChainChanged)

		
// Wrapper widget for Property access widget, which can update when the target Class changes
class SPropertyAccessChainWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPropertyAccessChainWidget)
	{}

	SLATE_ARGUMENT(IHasContextClass*, ContextClassOwner)
	SLATE_ARGUMENT(FString, TypeFilter);
	SLATE_ARGUMENT(FString, BindingColor);
	SLATE_ARGUMENT(bool, AllowFunctions);
	SLATE_EVENT(FOnAddBinding, OnAddBinding);
	SLATE_EVENT(FPropertyAccessChainChanged, OnValueChanged);
	SLATE_ATTRIBUTE(FChooserPropertyBinding*, PropertyBindingValue);
            
	SLATE_END_ARGS()

	UE_API void Construct( const FArguments& InArgs);
	UE_API virtual ~SPropertyAccessChainWidget();
	
	// Assign result from Property Access Widget to an FChooserPropertyBinding (Helper for OnAddBinding implementations)
	static UE_API void SetPropertyBinding(IHasContextClass* HasContext, const TArray<FBindingChainElement>& InBindingChain, FChooserPropertyBinding& OutPropertyBinding);

private:
	UE_API TSharedRef<SWidget> CreatePropertyAccessWidget();
	UE_API void UpdateWidget();
	UE_API void ContextClassChanged();
		
	FString TypeFilter;
	FString AlternateTypeFilter;
	FString BindingColor;
	UE_API FDelegateHandle ContextClassChangedHandle();
	IHasContextClass* ContextClassOwner = nullptr;
	FOnAddBinding OnAddBinding;
	FPropertyAccessChainChanged OnValueChanged;
	TAttribute<FChooserPropertyBinding*> PropertyBindingValue;
	bool bAllowFunctions;
};
	

}

#undef UE_API
