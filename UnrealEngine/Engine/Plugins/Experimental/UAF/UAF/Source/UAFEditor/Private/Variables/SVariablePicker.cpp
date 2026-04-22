// Copyright Epic Games, Inc. All Rights Reserved.

#include "SVariablePicker.h"

#include "UncookedOnlyUtils.h"
#include "Param/ParamType.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Images/SImage.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/PropertyViewer/SPropertyViewer.h"
#include "Variables/AnimNextSoftVariableReference.h"
#include "Widgets/Input/SCheckBox.h"

#define LOCTEXT_NAMESPACE "SVariablePicker"

namespace UE::UAF::Editor
{

void SVariablePicker::Construct(const FArguments& InArgs)
{
	Args = InArgs._Args;

	FieldIterator = MakeUnique<FFieldIterator>(Args.OnFilterVariableType);

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(400.0f)
		.HeightOverride(400.0f)
		.Padding(2.0f)
		[
			SNew(SVerticalBox)
			+SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(PropertyViewer, UE::PropertyViewer::SPropertyViewer)
				.OnSelectionChanged(this, &SVariablePicker::HandleFieldPicked)
				.OnGenerateContainer(this, &SVariablePicker::HandleGenerateContainer)
				.FieldIterator(FieldIterator.Get())
				.FieldExpander(&FieldExpander)
				.bShowSearchBox(true)
				.bFocusSearchBox(Args.bFocusSearchWidget)
			]
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0, 2.0f, 0.0f, 0.0f)
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("ContextSensitiveTooltip", "Whether to only display variables that are valid in the current context"))
				.Visibility_Lambda([this]()
				{
					return Args.OnIsContextSensitive && Args.OnIsContextSensitive->IsBound() ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.IsChecked_Lambda([this]()
				{
					if (Args.OnIsContextSensitive && Args.OnIsContextSensitive->IsBound())
					{
						return Args.OnIsContextSensitive->Execute() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}
					return ECheckBoxState::Undetermined;
				})
				.OnCheckStateChanged_Lambda([this](ECheckBoxState InState)
				{
					Args.OnContextSensitivityChanged.ExecuteIfBound(InState == ECheckBoxState::Checked);
					RefreshEntries();
				})
				.Content()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ContextSensitive", "Context Sensitive"))
				]
			]
		]
	];

	RefreshEntries();
}

void SVariablePicker::RefreshEntries()
{
	PropertyViewer->RemoveAll();
	CachedContainers.Reset();
	ContainerMap.Reset();

	// Add variables exposed on assets
	TMap<FAssetData, FAnimNextAssetRegistryExports> Exports;
	if(UncookedOnly::FUtils::GetExportedVariablesFromAssetRegistry(Exports))
	{
		for(const TPair<FAssetData, FAnimNextAssetRegistryExports>& ExportPair : Exports)
		{
			if(ExportPair.Value.Exports.Num() > 0)
			{
				// Add a placeholder struct for this asset's properties
				TArray<FPropertyBagPropertyDesc> PropertyDescs;
				PropertyDescs.Reserve(ExportPair.Value.Exports.Num());
				for(const FAnimNextExport& Export : ExportPair.Value.Exports)
				{
					if (const FAnimNextVariableDeclarationData* VariableEntry = Export.Data.GetPtr<FAnimNextVariableDeclarationData>())
					{
						const bool bHasInclusionFlags = EnumHasAllFlags(static_cast<EAnimNextExportedVariableFlags>(VariableEntry->Flags), Args.FlagInclusionFilter);
						const bool bHasExclusionFlags = EnumHasAnyFlags(static_cast<EAnimNextExportedVariableFlags>(VariableEntry->Flags), Args.FlagExclusionFilter);
						
						if(bHasInclusionFlags && !bHasExclusionFlags && Export.Identifier != NAME_None && VariableEntry->Type.IsValid())
						{
							FAnimNextSoftVariableReference VariableReference(Export.Identifier, ExportPair.Key.ToSoftObjectPath());

							if (!Args.OnFilterVariable.IsBound() || Args.OnFilterVariable.Execute(VariableReference) == EFilterVariableResult::Include)
							{
								if (!Args.OnFilterVariableType.IsBound() || Args.OnFilterVariableType.Execute(VariableEntry->Type) == EFilterVariableResult::Include)
								{
									PropertyDescs.Emplace(Export.Identifier, VariableEntry->Type.GetContainerType(), VariableEntry->Type.GetValueType(), VariableEntry->Type.GetValueTypeObject());
								}
							}
						}
					}
				}

				if(PropertyDescs.Num() > 0)
				{
					const FText DisplayName = FText::FromName(ExportPair.Key.AssetName);
					const FText TooltipText = FText::FromString(ExportPair.Key.GetObjectPathString());
					FContainerInfo& ContainerInfo = CachedContainers.Emplace_GetRef(DisplayName, TooltipText, ExportPair.Key, MakeUnique<FInstancedPropertyBag>());
					ContainerInfo.PropertyBag->AddProperties(PropertyDescs);
					UE::PropertyViewer::SPropertyViewer::FHandle Handle = PropertyViewer->AddContainer(ContainerInfo.PropertyBag.Get()->GetPropertyBagStruct(), DisplayName);
					ContainerMap.Add(Handle, CachedContainers.Num() - 1);
				}
			}
		}
	}

}

bool SVariablePicker::GetFieldInfo(UE::PropertyViewer::SPropertyViewer::FHandle InHandle, const FFieldVariant& InField, FAnimNextSoftVariableReference& OutVariableReference, FAnimNextParamType& OutType) const
{
	if(const int32* ContainerIndexPtr = ContainerMap.Find(InHandle))
	{
		const FContainerInfo& ContainerInfo = CachedContainers[*ContainerIndexPtr];
		if(FProperty* Property = InField.Get<FProperty>())
		{
			OutVariableReference = FAnimNextSoftVariableReference(Property->GetFName(), ContainerInfo.AssetData.ToSoftObjectPath());
			OutType = FAnimNextParamType::FromProperty(Property);
		}

		return true;
	}

	return false;
}

void SVariablePicker::HandleFieldPicked(UE::PropertyViewer::SPropertyViewer::FHandle InHandle, TArrayView<const FFieldVariant> InFields, ESelectInfo::Type InSelectionType)
{
	if(InFields.Num() == 1)
	{
		FAnimNextParamType Type;
		FAnimNextSoftVariableReference VariableReference;
		if(GetFieldInfo(InHandle, InFields.Last(), VariableReference, Type))
		{
			if(ensure(Type.IsValid() && !VariableReference.IsNone()))
			{
				Args.OnVariablePicked.ExecuteIfBound(VariableReference, Type);
			}
		}
	}
}

TSharedRef<SWidget> SVariablePicker::HandleGenerateContainer(UE::PropertyViewer::SPropertyViewer::FHandle InHandle, TOptional<FText> InDisplayName)
{
	if(int32* ContainerIndexPtr = ContainerMap.Find(InHandle))
	{
		if(CachedContainers.IsValidIndex(*ContainerIndexPtr))
		{
			FContainerInfo& ContainerInfo = CachedContainers[*ContainerIndexPtr];

			return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("ClassIcon.Object"))
			]
			+SHorizontalBox::Slot()
			.Padding(4.0f)
			[
				SNew(STextBlock)
				.Text(ContainerInfo.DisplayName)
				.ToolTipText(ContainerInfo.TooltipText)
			];
		}
	}

	return SNullWidget::NullWidget;
}

TArray<FFieldVariant> SVariablePicker::FFieldIterator::GetFields(const UStruct* Struct, const FName FieldName, const UStruct* ContainerStruct) const
{
	auto PassesFilterChecks = [this](const FProperty* InProperty)
	{
		if(InProperty && OnFilterVariableType.IsBound())
		{
			FAnimNextParamType Type = FAnimNextParamType::FromProperty(InProperty);
			return Type.IsValid() && OnFilterVariableType.Execute(Type) == EFilterVariableResult::Include;
		}

		return false;
	};

	TArray<FFieldVariant> Result;
	for (TFieldIterator<FProperty> PropertyIt(Struct); PropertyIt; ++PropertyIt)
	{
		FProperty* Property = *PropertyIt;
		if(PassesFilterChecks(Property))
		{
			Result.Add(FFieldVariant(Property));
		}
	}

	return Result;
}

TOptional<const UClass*> SVariablePicker::FFieldExpander::CanExpandObject(const FObjectPropertyBase* Property, const UObject* Instance) const
{
	return TOptional<const UClass*>();
}

bool SVariablePicker::FFieldExpander::CanExpandScriptStruct(const FStructProperty* StructProperty) const
{
	return false;
}

TOptional<const UStruct*> SVariablePicker::FFieldExpander::GetExpandedFunction(const UFunction* Function) const
{
	return TOptional<const UStruct*>();
}

}

#undef LOCTEXT_NAMESPACE