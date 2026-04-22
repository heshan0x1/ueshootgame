// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimDetailsProxyManagerDetails.h"

#include "Algo/AnyOf.h"
#include "AnimDetails/AnimDetailsProxyManager.h"
#include "AnimDetails/Proxies/AnimDetailsProxyBase.h"
#include "ControlRig.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailGroup.h"
#include "ISequencer.h"

#define LOCTEXT_NAMESPACE "AnimDetailsProxyManagerDetails"

namespace UE::ControlRigEditor
{
	namespace Private
	{
		/** Adds proxies into their respective groups */
		void AddProxiesToDetails(IDetailLayoutBuilder& DetailLayout, const TArray<UAnimDetailsProxyBase*>& Proxies)
		{
			if (Proxies.IsEmpty())
			{
				return;
			}

			/** A group of attribute proxies, defined by their parent name */
			struct FAnimDetailsGroupedAttributeProxies
			{
				/** The parent name */
				FName ParentName;

				/** The property names to display in this group */
				TSet<FName> PropertyNames;

				/** The proxies in this group */
				TMap<FName, TArray<UObject*>> DetailRowIDToProxiesMap;
			};

			TMap<FName, TArray<UObject*>> TopLevelDetailRowIDToProxiesMap;
			TArray<FAnimDetailsGroupedAttributeProxies> AttributeGroups;

			for (UAnimDetailsProxyBase* Proxy : Proxies)
			{
				if (!Proxy)
				{
					continue;
				}
				
				const FName DetailsRowID = Proxy->GetDetailRowID();
				if (Proxy->bIsIndividual)
				{
					const FName DetailRowID = Proxy->GetDetailRowID();
					const FRigControlElement* ControlElement = Proxy->GetControlElement();
					const FRigElementKey& ControlElementKey = Proxy->GetControlElementKey();

					TOptional<FName> OptionalParentName;
					if (ControlElement &&
						ControlElementKey.IsValid())
					{
						const UControlRig* ControlRig = Proxy->GetControlRig();
						const URigHierarchy* Hierarchy = ControlRig ? ControlRig->GetHierarchy() : nullptr;
						if (Hierarchy)
						{
							FName ParentName = Hierarchy->GetModuleFName(ControlElementKey);
							if (ParentName != NAME_None)
							{
								OptionalParentName = ParentName;
							}
							else if (const FRigBaseElement* Parent = Hierarchy->GetFirstParent(ControlElement))
							{
								ParentName = Parent ? Parent->GetDisplayName() : NAME_None;
								if (ParentName != NAME_None)
								{
									OptionalParentName = ParentName;
								}
							}
						}
					}
					else if (const UObject* BoundObject = Proxy->GetSequencerItem().GetBoundObject())
					{
						if (const AActor* Actor = Cast<AActor>(BoundObject))
						{
							OptionalParentName = Actor->GetFName();
						}
						else if (const UActorComponent* Component = Cast<UActorComponent>(BoundObject))
						{
							OptionalParentName = Component->GetFName();
						}
					}

					FAnimDetailsGroupedAttributeProxies* AttributeGroupPtr = [&AttributeGroups, &OptionalParentName]() -> FAnimDetailsGroupedAttributeProxies*
						{
							if (OptionalParentName.IsSet())
							{
								return AttributeGroups.FindByPredicate(
									[&OptionalParentName](const FAnimDetailsGroupedAttributeProxies& AttributeProxies)
									{
										return AttributeProxies.ParentName == OptionalParentName.GetValue();
									});
							}

							return nullptr;
						}();

		
					if (AttributeGroupPtr)
					{
						for (const FName& PropertyName : Proxy->GetPropertyNames())
						{
							AttributeGroupPtr->PropertyNames.Add(PropertyName);
						}

						AttributeGroupPtr->DetailRowIDToProxiesMap.FindOrAdd(Proxy->GetDetailRowID()).Add(Proxy);
					}
					else
					{
						FAnimDetailsGroupedAttributeProxies NewGroup;
						NewGroup.ParentName = OptionalParentName.IsSet() ? OptionalParentName.GetValue() : NAME_None;
						NewGroup.DetailRowIDToProxiesMap.Add(Proxy->GetDetailRowID(), { Proxy });
						
						for (const FName& PropertyName : Proxy->GetPropertyNames())
						{
							NewGroup.PropertyNames.Add(PropertyName);
						}

						AttributeGroups.Add(NewGroup);
					}
				}
				else
				{
					TopLevelDetailRowIDToProxiesMap.FindOrAdd(Proxy->GetDetailRowID()).Add(Proxy);
				}
			}

			// Top level grouped proxies show first
			IDetailCategoryBuilder& CategoryBuilder_None = DetailLayout.EditCategory("nocategory");
			for (const TTuple<FName, TArray<UObject*>>& TopLevelDetailRowIDToProxiesPair : TopLevelDetailRowIDToProxiesMap)
			{
				CategoryBuilder_None.AddExternalObjects(
					TopLevelDetailRowIDToProxiesPair.Value,
					EPropertyLocation::Default,
					FAddPropertyParams()
					.HideRootObjectNode(true)
				);
			}

			// Attribute proxies are displayed individually, but are still multi-edited across multiple control rig
			const bool bOmitAttributeGroupName = !TopLevelDetailRowIDToProxiesMap.IsEmpty() && AttributeGroups.Num() == 1;
			if (bOmitAttributeGroupName)
			{
				const FText CategoryName = LOCTEXT("AttributeCategoryName", "Attributes");
				IDetailCategoryBuilder& CategoryBuilder_Attributes = DetailLayout.EditCategory("Attributes", CategoryName);
				const FAnimDetailsGroupedAttributeProxies& AttributeGroup = AttributeGroups[0];

				for (const TTuple<FName, TArray<UObject*>>& DetailRowIDToProxiesPair : AttributeGroup.DetailRowIDToProxiesMap)
				{
					CategoryBuilder_Attributes.AddExternalObjects(
						DetailRowIDToProxiesPair.Value,
						EPropertyLocation::Default,
						FAddPropertyParams()
						.HideRootObjectNode(true)
					);
				}
			}
			else
			{
				for (const FAnimDetailsGroupedAttributeProxies& AttributeGroup : AttributeGroups)
				{
					constexpr bool bForAdvanced = false;
					constexpr bool bStartExpanded = true;

					const FText ModuleNameText = FText::FromString(AttributeGroup.ParentName.ToString());

					const FText CategoryName = FText::Format(LOCTEXT("FormatedAttributeCategoryName", "{0} Attributes"), ModuleNameText);
					const FName CategoryID = *(CategoryName.ToString() + FGuid::NewGuid().ToString());
					IDetailCategoryBuilder& CategoryBuilder = DetailLayout.EditCategory(CategoryID, CategoryName);

					for (const TTuple<FName, TArray<UObject*>>& DetailRowIDToProxiesPair : AttributeGroup.DetailRowIDToProxiesMap)
					{
						CategoryBuilder.AddExternalObjects(
							DetailRowIDToProxiesPair.Value,
							EPropertyLocation::Default,
							FAddPropertyParams()
							.HideRootObjectNode(true)
						);
					}
				}
			}
		}
	}

	TSharedRef<IDetailCustomization> FAnimDetailProxyManagerDetails::MakeInstance()
	{
		return MakeShared<FAnimDetailProxyManagerDetails>();
	}

	void FAnimDetailProxyManagerDetails::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
	{
		TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
		DetailLayout.GetObjectsBeingCustomized(ObjectsBeingCustomized);

		for (const TWeakObjectPtr<UObject>& ObjectBeingCustomized : ObjectsBeingCustomized)
		{
			if (UAnimDetailsProxyManager* ProxyManager = Cast<UAnimDetailsProxyManager>(ObjectBeingCustomized.Get()))
			{
				FAnimDetailsFilter& Filter = ProxyManager->GetAnimDetailsFilter();
				const TArray<UAnimDetailsProxyBase*> FilteredProxies = Filter.GetFilteredProxies();

				Private::AddProxiesToDetails(DetailLayout, FilteredProxies);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
