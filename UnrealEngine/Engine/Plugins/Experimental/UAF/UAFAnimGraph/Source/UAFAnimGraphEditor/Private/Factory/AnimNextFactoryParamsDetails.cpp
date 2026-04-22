// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextFactoryParamsDetails.h"

#include "UAFStyle.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "InstancedStructDetails.h"
#include "IStructureDataProvider.h"
#include "PropertyHandle.h"
#include "StructViewerFilter.h"
#include "StructViewerModule.h"
#include "Algo/Compare.h"
#include "Factory/AnimGraphFactory.h"
#include "Factory/AnimNextFactoryParams.h"
#include "Modules/ModuleManager.h"
#include "Styling/StyleColors.h"
#include "TraitCore/TraitRegistry.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Graph/AnimNextAnimationGraph.h"

#define LOCTEXT_NAMESPACE "AnimNextFactoryParamsDetails"

namespace UE::UAF::Editor
{

void FAnimNextFactoryParamsDetails::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
}

void FAnimNextFactoryParamsDetails::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	TSharedPtr<IPropertyHandle> BuilderHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAnimNextFactoryParams, Builder));
	BuilderHandle->MarkHiddenByCustomization();

	TSharedPtr<IPropertyHandle> StacksHandle = BuilderHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAnimNextSimpleAnimGraphBuilder, Stacks));
	TSharedPtr<IPropertyHandle> StackHandle = StacksHandle->GetChildHandle(0);
	if (StackHandle.IsValid())
	{
		TraitStructsHandle = StackHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAnimNextSimpleAnimGraphBuilderTraitStackDesc, TraitStructs));
	}
	
	bool bIsInvalidOrAnimGraph = false; 

	// Find property for the factory source & common default params set
	TOptional<FAnimNextFactoryParams> CommonDefaults;
	FString FactorySourcePropertyName = InPropertyHandle->GetMetaData("FactorySourceProperty");
	TSharedPtr<IPropertyHandle> FactorySourceHandle = InPropertyHandle->GetParentHandle()->GetChildHandle(FName(*FactorySourcePropertyName));
	FactorySourceHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([WeakPropertyHandle = TWeakPtr<IPropertyHandle>(InPropertyHandle)]()
	{
		if (TSharedPtr<IPropertyHandle> PinnedPropertyHandle = WeakPropertyHandle.Pin())
		{
			PinnedPropertyHandle->RequestRebuildChildren();
		}
	}));
	
	if (FactorySourceHandle.IsValid() && FactorySourceHandle->GetProperty()->IsA<FObjectProperty>())
	{
		FactorySourceHandle->EnumerateRawData([&CommonDefaults, &bIsInvalidOrAnimGraph](void* RawData, const int32 DataIndex, const int32 NumDatas)
		{
			UObject* Object = *(static_cast<UObject**>(RawData));
		
			if (!CommonDefaults.IsSet())
			{
				if (Object == nullptr || Object->IsA<UAnimNextAnimationGraph>())
				{
					// TODO: At the moment we dont have 'details panel pin overrides' for assets, so we reset the params to not be confusing
					// In the end we will want the params to also provide overrides like we supply to RunGraph nodes
					CommonDefaults = FAnimNextFactoryParams();
					bIsInvalidOrAnimGraph = true;
				}
				else
				{
					CommonDefaults = FAnimGraphFactory::GetDefaultParamsForObject(Object);
				}
			}
			else
			{
				const FAnimNextFactoryParams& DefaultParams = FAnimGraphFactory::GetDefaultParamsForObject(Object);
				if (!Algo::Compare(DefaultParams.Builder.Stacks[0].TraitStructs, CommonDefaults->Builder.Stacks[0].TraitStructs))
				{
					CommonDefaults.Reset();
					return false;
				}
			}
			return true;
		});
	}

	TSet<const UScriptStruct*> AddedStructs;

	if (TraitStructsHandle.IsValid())
	{
		// Find common 'current' set
		TSet<const UScriptStruct*> CommonStructs;
		TraitStructsHandle->EnumerateRawData([&CommonStructs](void* RawData, const int32 DataIndex, const int32 NumDatas)
		{
			TArray<FInstancedStruct>* InitializePayloads = static_cast<TArray<FInstancedStruct>*>(RawData);
			if (CommonStructs.IsEmpty())
			{
				for (const FInstancedStruct& InitializePayload : *InitializePayloads)
				{
					CommonStructs.Add(InitializePayload.GetScriptStruct());
				}
			}
			else
			{
				for (auto It = CommonStructs.CreateIterator(); It; ++It)
				{
					const UScriptStruct* CommonStruct = *It;
					if (!InitializePayloads->ContainsByPredicate([CommonStruct](const FInstancedStruct& InInitializePayload){ return CommonStruct == InInitializePayload.GetScriptStruct(); }))
					{
						It.RemoveCurrent();
					}
				}
			}
			return true;
		});

		// No common structs, cannot edit (all struct types for edited objects are mutually exclusive)
		if (CommonStructs.IsEmpty())
		{
			return;
		}

		// Add entries for instanced structs that are common to all objects and have a common type at this index
		uint32 NumChildren = 0;
		if (TraitStructsHandle->GetNumChildren(NumChildren) == FPropertyAccess::Success)
		{
			for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
			{
				TSharedRef<IPropertyHandle> InstancedStructHandle = TraitStructsHandle->GetChildHandle(ChildIndex).ToSharedRef();
				TOptional<const UScriptStruct*> CommonType;
				InstancedStructHandle->EnumerateRawData([&CommonType](void* RawData, const int32 DataIndex, const int32 NumDatas)
				{
					FInstancedStruct* InstancedStruct = static_cast<FInstancedStruct*>(RawData);
					if (!CommonType.IsSet())
					{
						CommonType = InstancedStruct->GetScriptStruct();
					}
					else if (CommonType.GetValue() != InstancedStruct->GetScriptStruct())
					{
						CommonType.Reset();
						return false;
					}
					return true;
				});

				if (CommonType.IsSet() && CommonStructs.Contains(CommonType.GetValue()))
				{
					AddedStructs.Add(CommonType.GetValue());

					const FName UniqueName(InstancedStructHandle->GetPropertyPath());
					IDetailPropertyRow* Row = InChildBuilder.AddChildStructure(InstancedStructHandle, MakeShared<FInstancedStructProvider>(InstancedStructHandle), UniqueName);
					FDetailWidgetRow& CustomWidget = Row->CustomWidget(true);

					CustomWidget
					.NameContent()
					[
						SNew(SBorder)
						.Padding(FMargin(6.0f, 2.0f))
						.BorderImage(FUAFStyle::Get().GetBrush("AnimNext.TraitDetails.Background"))
						[
							SNew(STextBlock)
							.TextStyle(FUAFStyle::Get(), "AnimNext.TraitDetails.Label")
							.ColorAndOpacity(FStyleColors::Foreground)
							.Text(CommonType.GetValue()->GetDisplayNameText())
						]
					];

					const FTraitRegistry& TraitRegistry = FTraitRegistry::Get();
					const FTrait* Trait = TraitRegistry.Find(CommonType.GetValue());
					if (Trait)
					{
						// If this is an additive trait, allow it to be removed
						if (Trait->GetTraitMode() == ETraitMode::Additive)
						{
							CustomWidget
							.ValueContent()
							[
								SNew(SCheckBox)
								.ToolTipText(LOCTEXT("EnableOptionalTrait", "Enable/disable this optional trait"))
								.IsChecked(true)
								.OnCheckStateChanged_Lambda([ChildIndex, WeakTraitStructsHandle = TWeakPtr<IPropertyHandle>(TraitStructsHandle), WeakPropertyHandle = TWeakPtr<IPropertyHandle>(InPropertyHandle)](ECheckBoxState InState)
								{
									TSharedPtr<IPropertyHandle> PinnedTraitStructsHandle = WeakTraitStructsHandle.Pin();
									TSharedPtr<IPropertyHandle> PinnedPropertyHandle = WeakPropertyHandle.Pin();
									if (PinnedTraitStructsHandle.IsValid() && PinnedPropertyHandle.IsValid())
									{
										PinnedTraitStructsHandle->AsArray()->DeleteItem(ChildIndex);
										PinnedPropertyHandle->RequestRebuildChildren();
									}
								})
							];
						}
						else if (FactorySourceHandle.IsValid())
						{
							// If its a base trait, hide any child property that has the appropriate metadata (if we have a factory source)
							uint32 NumStructProperties = 0;
							TSharedPtr<IPropertyHandle> StructHandle = InstancedStructHandle->GetChildHandle(0);
							if (StructHandle->GetNumChildren(NumStructProperties) == FPropertyAccess::Success)
							{
								for (uint32 StructPropertyIndex = 0; StructPropertyIndex < NumStructProperties; ++StructPropertyIndex)
								{
									TSharedPtr<IPropertyHandle> StructPropertyHandle = StructHandle->GetChildHandle(StructPropertyIndex);
									if (StructPropertyHandle->HasMetaData("FactorySource"))
									{
										StructPropertyHandle->MarkHiddenByCustomization();
									}
								}
							}
						}
					}
				}
			}
		}
	}

	if (CommonDefaults.IsSet() && CommonDefaults.GetValue().Builder.GetNumStacks() > 0)
	{
		// Now add any entries for defaults we didnt find above to allow them to be added
		for (const TInstancedStruct<FAnimNextTraitSharedData>& DefaultStruct : CommonDefaults.GetValue().Builder.Stacks[0].TraitStructs)
		{
			if (!AddedStructs.Contains(DefaultStruct.GetScriptStruct()))
			{
				InChildBuilder.AddCustomRow(DefaultStruct.GetScriptStruct()->GetDisplayNameText())
				.NameContent()
				[
					SNew(SBorder)
					.Padding(FMargin(6.0f, 2.0f))
					.BorderImage(FUAFStyle::Get().GetBrush("AnimNext.TraitDetails.Background"))
					[
						SNew(STextBlock)
						.TextStyle(FUAFStyle::Get(), "AnimNext.TraitDetails.Label")
						.ColorAndOpacity(FStyleColors::Foreground)
						.Text(DefaultStruct.GetScriptStruct()->GetDisplayNameText())
					]
				]
				.ValueContent()
				[
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("EnableOptionalTrait", "Enable/disable this optional trait"))
					.IsChecked(false)
					.OnCheckStateChanged_Lambda([DefaultStruct, WeakTraitStructsHandle = TWeakPtr<IPropertyHandle>(TraitStructsHandle), WeakPropertyHandle = TWeakPtr<IPropertyHandle>(InPropertyHandle)](ECheckBoxState InState)
					{
						TSharedPtr<IPropertyHandle> PinnedTraitStructsHandle = WeakTraitStructsHandle.Pin();
						TSharedPtr<IPropertyHandle> PinnedPropertyHandle = WeakPropertyHandle.Pin();
						if (PinnedTraitStructsHandle.IsValid() && PinnedPropertyHandle.IsValid())
						{
							TSharedPtr<IPropertyHandleArray> ArrayHandle = PinnedTraitStructsHandle->AsArray();
							FPropertyHandleItemAddResult Result = ArrayHandle->AddItem();
							TSharedPtr<IPropertyHandle> NewEntryHandle = PinnedTraitStructsHandle->GetChildHandle(Result.GetIndex());
							NewEntryHandle->EnumerateRawData([&DefaultStruct](void* RawData, const int32 DataIndex, const int32 NumDatas)
							{
								TInstancedStruct<FAnimNextTraitSharedData>* InstancedStruct = static_cast<TInstancedStruct<FAnimNextTraitSharedData>*>(RawData);
								*InstancedStruct = DefaultStruct;
								return true;
							});
							PinnedPropertyHandle->RequestRebuildChildren();
						}
					})
				];
			}
		}
	}

	if (!bIsInvalidOrAnimGraph)
	{
		// Add an 'add' widget for any custom behaviors users want to opt into
		InChildBuilder.AddCustomRow(LOCTEXT("AddTraitWidget", "Add Trait"))
		.NameContent()
		[
			SNew(SComboButton)
			.ToolTipText(LOCTEXT("AddTraitTooltip", "Add a trait to modify how this asset is played"))
			.HasDownArrow(false)
			.ComboButtonStyle(FUAFStyle::Get(), "AnimNext.TraitDetails.AddButton")
			.ButtonContent()
			[
				SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(0.0f, 0.0f, 3.0f, 0.0f)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Plus"))
				]
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(0.0f, 2.0f)
				[
					SNew(STextBlock)
					.TextStyle(FUAFStyle::Get(), "AnimNext.TraitDetails.Label")
					.ColorAndOpacity(FStyleColors::Foreground)
					.Text(LOCTEXT("AddTraitButton", "Add"))
				]
			]
			.OnGetMenuContent_Lambda([WeakTraitStructsHandle = TWeakPtr<IPropertyHandle>(TraitStructsHandle), WeakPropertyHandle = TWeakPtr<IPropertyHandle>(InPropertyHandle)]()
			{
				FStructViewerModule& StructViewerModule = FModuleManager::LoadModuleChecked<FStructViewerModule>("StructViewer");

				class FStructFilter : public IStructViewerFilter
				{
				public:
					virtual bool IsStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const UScriptStruct* InStruct, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
					{
						if (InStruct->HasMetaData(TEXT("Hidden")))
						{
							return false;
						}

						if (!InStruct->IsChildOf(FAnimNextTraitSharedData::StaticStruct()))
						{
							return false;
						}
						
						const FTraitRegistry& TraitRegistry = FTraitRegistry::Get();
						const FTrait* Trait = TraitRegistry.Find(InStruct);
						return Trait && Trait->GetTraitMode() == ETraitMode::Additive;
					}

					virtual bool IsUnloadedStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const FSoftObjectPath& InStructPath, TSharedRef<class FStructViewerFilterFuncs> InFilterFuncs)
					{
						return false;
					};
				};

				FStructViewerInitializationOptions Options;
				Options.StructFilter = MakeShared<FStructFilter>();
				Options.Mode = EStructViewerMode::StructPicker;
				Options.NameTypeToDisplay = EStructViewerNameTypeToDisplay::DisplayName;
				Options.bShowNoneOption = false;

				return
					SNew(SBox)
					.WidthOverride(400.0f)
					.HeightOverride(400.0f)
					[
						StructViewerModule.CreateStructViewer(Options, FOnStructPicked::CreateLambda([WeakPropertyHandle](const UScriptStruct* InStruct)
						{
							TSharedPtr<IPropertyHandle> PinnedPropertyHandle = WeakPropertyHandle.Pin();
							if (PinnedPropertyHandle.IsValid())
							{
								TSharedPtr<IPropertyHandle> BuilderHandle = PinnedPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAnimNextFactoryParams, Builder));
								TSharedPtr<IPropertyHandle> StacksHandle = BuilderHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAnimNextSimpleAnimGraphBuilder, Stacks));
								TSharedPtr<IPropertyHandle> StackHandle = StacksHandle->GetChildHandle(0);
								if (!StackHandle.IsValid())
								{
									StacksHandle->AsArray()->AddItem();
									StackHandle = StacksHandle->GetChildHandle(0);
								}

								TSharedPtr<IPropertyHandle> TraitStructsHandle = StackHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FAnimNextSimpleAnimGraphBuilderTraitStackDesc, TraitStructs));
								TSharedPtr<IPropertyHandleArray> ArrayHandle = TraitStructsHandle->AsArray();
								FPropertyHandleItemAddResult Result = ArrayHandle->AddItem();
								TSharedPtr<IPropertyHandle> NewEntryHandle = TraitStructsHandle->GetChildHandle(Result.GetIndex());
								NewEntryHandle->EnumerateRawData([InStruct](void* RawData, const int32 DataIndex, const int32 NumDatas)
								{
									FInstancedStruct* InstancedStruct = static_cast<FInstancedStruct*>(RawData);
									InstancedStruct->InitializeAs(InStruct);
									return true;
								});
								PinnedPropertyHandle->RequestRebuildChildren();
							}
						}))
					];
			})
		];
	}
}

}

#undef LOCTEXT_NAMESPACE