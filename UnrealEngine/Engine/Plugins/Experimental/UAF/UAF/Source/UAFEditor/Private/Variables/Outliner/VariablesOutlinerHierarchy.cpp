// Copyright Epic Games, Inc. All Rights Reserved.

#include "VariablesOutlinerHierarchy.h"

#include "AnimNextRigVMAsset.h"
#include "ISceneOutlinerMode.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "VariablesOutlinerAssetItem.h"
#include "VariablesOutlinerCategoryItem.h"
#include "VariablesOutlinerStructSharedVariablesItem.h"
#include "VariablesOutlinerMode.h"
#include "VariablesOutlinerEntryItem.h"
#include "Entries/AnimNextSharedVariablesEntry.h"
#include "Entries/AnimNextRigVMAssetEntry.h"
#include "Entries/AnimNextVariableEntry.h"
#include "Variables/SVariablesView.h"
#include "Variables/AnimNextSharedVariables.h"

namespace UE::UAF::Editor
{

TAutoConsoleVariable<bool> GOutputHierarchyDebugInformationCvar(TEXT("VariablesOutliner.OutputHierarchyDebugInformation"), false, TEXT("If enabled will output debug information about the hierarchy generation to the output log")); 

FVariablesOutlinerHierarchy::FVariablesOutlinerHierarchy(ISceneOutlinerMode* Mode)
	: ISceneOutlinerHierarchy(Mode)
{
}

void FVariablesOutlinerHierarchy::CreateItems(TArray<FSceneOutlinerTreeItemPtr>& OutItems) const
{
	SVariablesOutliner* Outliner = static_cast<FVariablesOutlinerMode* const>(Mode)->GetOutliner();

	WeakProcessedAssets.Reset();
	WeakProcessedStructs.Reset();

	if(Outliner->Assets.Num())
	{
		// Process first asset only for now
		const int32 NumAssets = Outliner->Assets.Num();
		for(int32 Index = 0; Index < NumAssets; Index++)
		{
			const TSoftObjectPtr<UAnimNextRigVMAsset>& SoftAsset = Outliner->Assets[Index];
			TArray<const FSoftObjectPath> SharedVariableSourcePaths;
			ProcessAsset(SoftAsset, (NumAssets - Index), OutItems, SharedVariableSourcePaths);
		}
	}
}

FSceneOutlinerTreeItemPtr FVariablesOutlinerHierarchy::FindOrCreateParentItem(const ISceneOutlinerTreeItem& Item, const TMap<FSceneOutlinerTreeItemID, FSceneOutlinerTreeItemPtr>& Items, bool bCreate)
{
	uint32 ParentHash = INDEX_NONE;
	
	if (const FVariablesOutlinerEntryItem* EntryItem = Item.CastTo<FVariablesOutlinerEntryItem>())
	{
		const UAnimNextSharedVariablesEntry* SharedVariablesEntry = EntryItem->WeakSharedVariablesEntry.Get();
		
		FSoftObjectPath OwnerPath;
		if(SharedVariablesEntry != nullptr)
		{
			OwnerPath = SharedVariablesEntry->GetObjectPath();
		}
		else
		{
			UAnimNextRigVMAssetEntry* Entry = EntryItem->WeakEntry.Get();
			if (Entry == nullptr)
			{
				return nullptr;
			}

			UAnimNextRigVMAsset* Asset = Entry->GetTypedOuter<UAnimNextRigVMAsset>();
			if (Entry == nullptr)
			{
				return nullptr;
			}

			TSoftObjectPtr<UAnimNextRigVMAsset> SoftObjectPtr(Asset);
			OwnerPath = Asset;
			
		}

		uint32 CategoryHash = INDEX_NONE;

		// Now we have the parent asset hash, determine if we need to be child-ed to a category
		if (UAnimNextVariableEntry* VariableEntry = EntryItem->WeakEntry.Get())
		{
			if (!VariableEntry->GetVariableCategory().IsEmpty())
			{
				CategoryHash = GetTypeHash(VariableEntry->GetVariableCategory());
			}
		}

		check(OwnerPath.IsValid());
		ParentHash = CategoryHash != INDEX_NONE ? HashCombine(GetTypeHash(OwnerPath), CategoryHash) : GetTypeHash(OwnerPath);		
		
	}

	if (const FVariablesOutlinerCategoryItem* CategoryItem = Item.CastTo<FVariablesOutlinerCategoryItem>())
	{
		if (UAnimNextRigVMAsset* Asset = CategoryItem->WeakOwner.Get())
		{
			const FSoftObjectPath OwnerPath = Asset;
			const uint32 AssetHash = GetTypeHash(OwnerPath);
			const FStringView ParentCategory = CategoryItem->ParentCategoryName;
			ParentHash = ParentCategory.Len() ? HashCombine(AssetHash, GetTypeHash(ParentCategory)) : AssetHash;
		}
	}

	if (const FSceneOutlinerTreeItemPtr* ParentItem = Items.Find(ParentHash))
	{
		return *ParentItem;
	}

	return nullptr;
}

void FVariablesOutlinerHierarchy::ProcessAsset(const TSoftObjectPtr<UAnimNextRigVMAsset>& InSoftAsset, int32 Depth, TArray<FSceneOutlinerTreeItemPtr>& OutItems, TArray<const FSoftObjectPath>& OutSharedVariableSourcePaths) const
{
	UAnimNextRigVMAsset* Asset = InSoftAsset.Get();
	if(Asset == nullptr)
	{
		return;
	}
		
	const UAnimNextRigVMAssetEditorData* EditorData = UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(Asset);
	if (EditorData == nullptr)
	{
		return;
	}

	if (WeakProcessedAssets.Contains(Asset))
	{
		return;
	}

	WeakProcessedAssets.Add(Asset);

	UE_CLOG(GOutputHierarchyDebugInformationCvar.GetValueOnAnyThread(),LogAnimation, Warning, TEXT("ProcessAsset: %s %i"), *InSoftAsset.GetAssetName(), Depth);
		
	TArray<FSceneOutlinerTreeItemPtr> ParentItems;
	EditorData->ForEachEntryOfType<UAnimNextSharedVariablesEntry>([this, &OutSharedVariableSourcePaths, &ParentItems, Depth, &OutItems](UAnimNextSharedVariablesEntry* InSharedVariablesEntry)
	{
		SVariablesOutliner* Outliner = static_cast<FVariablesOutlinerMode* const>(Mode)->GetOutliner();

		switch (InSharedVariablesEntry->Type)
		{
		case EAnimNextSharedVariablesType::Asset:
			{
				if (const UAnimNextSharedVariables* SharedVariables = InSharedVariablesEntry->GetAsset())
				{
					OutSharedVariableSourcePaths.Add(SharedVariables);
				
					// Would have expected shared variable (assets) to be part of the outer export chain, so should have been handled as regular assets.
					if (!Outliner->Assets.Contains(SharedVariables))
					{	
						TArray<const FSoftObjectPath> SharedVariableSourcePaths;
						ProcessAsset( const_cast<UAnimNextSharedVariables*>(SharedVariables), Depth - 1, ParentItems, SharedVariableSourcePaths);
					}
				}				
			}
			break;

		case EAnimNextSharedVariablesType::Struct:
			{				
				if (const UScriptStruct* Struct = InSharedVariablesEntry->GetStruct())
				{
					if (WeakProcessedStructs.Contains(Struct))
					{
						return true;
					}

					WeakProcessedStructs.Add(Struct);

					if (FSceneOutlinerTreeItemPtr Item = Mode->CreateItemFor<FVariablesOutlinerStructSharedVariablesItem>(InSharedVariablesEntry))
					{
						OutItems.Add(Item);
					}
					
					UE_CLOG(GOutputHierarchyDebugInformationCvar.GetValueOnAnyThread(), LogAnimation, Warning, TEXT("~ FVariablesOutlinerStructSharedVariablesItem: %s %i"), *InSharedVariablesEntry->GetObjectPath().ToString(), Depth);
					
					OutSharedVariableSourcePaths.Add(Struct);
					int32 Index = 0;
					for (TFieldIterator<FProperty> It(Struct); It; ++It)
					{
						const FProperty* Property = *It;
						if (FSceneOutlinerTreeItemPtr Item = Mode->CreateItemFor<FVariablesOutlinerEntryItem>(Property))
						{
							FVariablesOutlinerEntryItem* EntryItem = Item->CastTo<FVariablesOutlinerEntryItem>();
							EntryItem->WeakSharedVariablesEntry = InSharedVariablesEntry;
							EntryItem->SortValue = Index++;
							OutItems.Add(Item);

							UE_CLOG(GOutputHierarchyDebugInformationCvar.GetValueOnAnyThread(), LogAnimation, Warning, TEXT("\t- FVariablesOutlinerEntryItem: %s %i"), *Property->GetName(), EntryItem->SortValue);
						}
					}
				}
			}
			break;
		}		
		
		return true;
	});

	OutItems.Append(ParentItems);

	TMap<FString, TSharedPtr<FVariablesOutlinerCategoryItem>> CategoryItems;

	for (int32 CategoryIndex = 0; CategoryIndex < EditorData->VariableAndFunctionCategories.Num(); ++CategoryIndex)
	{
		const FString& CategoryName = EditorData->VariableAndFunctionCategories[CategoryIndex];
		TArray<FString> SubCategories;
		CategoryName.ParseIntoArray(SubCategories, TEXT("|"));

		FString RebuildCategories;
		for (const FString& SubCategoryName : SubCategories)
		{
			const FString ParentCategoryName = RebuildCategories;

			if (!RebuildCategories.IsEmpty())
			{
				RebuildCategories.Append(TEXT("|"));
			}
			RebuildCategories.Append(SubCategoryName);
			
			const FString FullCategoryName = RebuildCategories;

			if (!CategoryItems.Contains(FullCategoryName))
			{
				if (TSharedPtr<FVariablesOutlinerCategoryItem> Item = StaticCastSharedPtr<FVariablesOutlinerCategoryItem>( Mode->CreateItemFor<FVariablesOutlinerCategoryItem>(FVariablesOutlinerCategoryItem::FItemData{Asset, SubCategoryName, ParentCategoryName, FullCategoryName})))
				{
					Item->CastTo<FVariablesOutlinerCategoryItem>()->SortValue = CategoryIndex;
					CategoryItems.Add(FullCategoryName, Item);

					UE_CLOG(GOutputHierarchyDebugInformationCvar.GetValueOnAnyThread(),LogAnimation, Warning, TEXT("+ FVariablesOutlinerCategoryItem: %s  %i"), *FullCategoryName, CategoryIndex);
				}
			}
		}
	}

	// Adjust asset depth to ensure SharedVariables are always pinned to 0 depth
	const int32 AssetDepth = Asset->GetClass() && Asset->GetClass() == UAnimNextSharedVariables::StaticClass() ? 0 : Depth;

	constexpr int32 NumberOfVariableSortValueEntriesInCategory = 1024;

	auto EnsureCategoryItemExists = [&CategoryItems, &OutItems](const FString& CategoryName)
	{		 
		TSharedPtr<FVariablesOutlinerCategoryItem>& CategoryItemToAdd = CategoryItems.FindChecked(CategoryName);
		OutItems.AddUnique(CategoryItemToAdd);

		auto CategoryPtr = CategoryItems.Find(CategoryItemToAdd->ParentCategoryName);
		while (CategoryPtr)
		{
			OutItems.AddUnique(*CategoryPtr);
			CategoryPtr = CategoryItems.Find((*CategoryPtr)->ParentCategoryName);
		}
	};

	if (FSceneOutlinerTreeItemPtr AssetItem = Mode->CreateItemFor<FVariablesOutlinerAssetItem>(FVariablesOutlinerAssetItem::FItemData{InSoftAsset, AssetDepth, OutSharedVariableSourcePaths}))
	{
		OutItems.Add(AssetItem);

		UE_CLOG(GOutputHierarchyDebugInformationCvar.GetValueOnAnyThread(), LogAnimation, Warning, TEXT("* FVariablesOutlinerAssetItem: %s %i"), *InSoftAsset.GetAssetName(), AssetDepth);

		int32 Index = 1;
		EditorData->ForEachEntryOfType<UAnimNextVariableEntry>([this, &OutItems, EnsureCategoryItemExists, &Index, EditorData](UAnimNextVariableEntry* InVariable)
		{
			if (FSceneOutlinerTreeItemPtr Item = Mode->CreateItemFor<FVariablesOutlinerEntryItem>(CastChecked<UAnimNextVariableEntry>(InVariable)))
			{
				FVariablesOutlinerEntryItem* EntryItem = Item->CastTo<FVariablesOutlinerEntryItem>();
				const int32 CategoryIndex = EditorData->VariableAndFunctionCategories.IndexOfByKey(InVariable->Category);
				EntryItem->SortValue = (CategoryIndex != INDEX_NONE ? ((CategoryIndex + 1) * NumberOfVariableSortValueEntriesInCategory) : 0) + Index++;
				OutItems.Add(Item);
				
				UE_CLOG(GOutputHierarchyDebugInformationCvar.GetValueOnAnyThread(), LogAnimation, Warning, TEXT("\t- FVariablesOutlinerEntryItem: %s %i %s"), *InVariable->GetEntryName().ToString(), Item->CastTo<FVariablesOutlinerEntryItem>()->SortValue, InVariable->GetVariableCategory().GetData());

				// Add category item on demand (to ensure we do not end up with empty category item hierarchies)
				if (!InVariable->Category.IsEmpty())
				{
					EnsureCategoryItemExists(InVariable->Category);
				}
			}
			return true;
		});
	}
}
}
