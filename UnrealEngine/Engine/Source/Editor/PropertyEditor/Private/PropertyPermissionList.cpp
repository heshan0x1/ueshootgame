// Copyright Epic Games, Inc. All Rights Reserved.

#include "PropertyPermissionList.h"

#include "Editor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"
#include "UObject/UnrealType.h"
#include "DetailTreeNode.h"

DEFINE_LOG_CATEGORY(LogPropertyEditorPermissionList);

namespace PropertyEditorPermissionList
{
	const FName PropertyPermissionListOwner = "PropertyPermissionList";
	static const FName NAME_CallInEditor("CallInEditor");

	static bool bSupportAllowAllFunctions = true;
	static FAutoConsoleVariableRef CVarSupportAllowAllFunctions(
		TEXT("PropertyEditor.PropertyEditorPermissionList.SupportAllowAllFunctions"),
		bSupportAllowAllFunctions,
		TEXT("Enable allowing all zero parameter call in editor functions when allowing all properties"));

	static FString ClassToDebug;
	static FAutoConsoleVariableRef CVarClassToDebug(
		TEXT("PropertyEditor.PropertyEditorPermissionList.ClassToDebug"),
		ClassToDebug,
		TEXT("Debug when a class that contains this string is cached"));
}

bool operator==(const FPropertyPermissionList::FPermissionListUpdate& A, const FPropertyPermissionList::FPermissionListUpdate& B)
{
	return A.ObjectStruct == B.ObjectStruct && A.OwnerName == B.OwnerName;
}

uint32 GetTypeHash(const FPropertyPermissionList::FPermissionListUpdate& PermisisonList)
{
	return HashCombine(
		GetTypeHash(PermisisonList.ObjectStruct), 
		GetTypeHash(PermisisonList.OwnerName));
}

FPropertyPermissionList::FPropertyPermissionList()
{
	if (GEditor)
	{
		RegisterOnBlueprintCompiled();
	}
	else
	{
		FCoreDelegates::OnPostEngineInit.AddRaw(this, &FPropertyPermissionList::RegisterOnBlueprintCompiled);
	}
	OnTickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FPropertyPermissionList::Tick), 1.0f);
}

FPropertyPermissionList::~FPropertyPermissionList()
{
	FTSTicker::RemoveTicker(OnTickHandle);
	if (GEditor)
	{
		GEditor->OnBlueprintCompiled().RemoveAll(this);
	}
}

bool FPropertyPermissionList::Tick(float DeltaTime)
{
	if (PendingUpdates.Num() == 0)
	{
		return true;
	}

	TArray<FPermissionListUpdate> PendingUpdatesCopy = PendingUpdates.Array();
	PendingUpdates.Reset();
	for (const FPermissionListUpdate& PermissionListUpdate : PendingUpdatesCopy)
	{
		PermissionListUpdatedDelegate.Broadcast(PermissionListUpdate.ObjectStruct, PermissionListUpdate.OwnerName);
	}
	return true;
}

void FPropertyPermissionList::RegisterOnBlueprintCompiled()
{
	if (GEditor)
	{
		GEditor->OnBlueprintCompiled().AddRaw(this, &FPropertyPermissionList::ClearCache);
	}
}

void FPropertyPermissionList::ClearCacheAndBroadcast(TSoftObjectPtr<const UStruct> ObjectStruct, FName OwnerName)
{
	// The cache isn't too expensive to recompute, so it is cleared
	// and lazily repopulated any time the raw PermissionList changes.
	ClearCache();

	if (!bSuppressUpdateDelegate)
	{
		PendingUpdates.Add({ObjectStruct, OwnerName});
	}
}

void FPropertyPermissionList::AddPermissionList(TSoftObjectPtr<const UStruct> Struct, const FNamePermissionList& PermissionList, const EPropertyPermissionListRules Rules, const TConstArrayView<FName> InAdditionalOwnerNames)
{
	FPropertyPermissionListEntry& Entry = RawPropertyPermissionList.FindOrAdd(Struct);
	Entry.PermissionList = PermissionList;

	// Track additional owners to prevent entry from being removed by FPropertyPermissionList::UnregisterOwner()
	// PermissionList without AllowList/DenyList/DenyAll entries will have no owners and be deleted from RawPropertyPermissionList unless it provides AdditionalOwnerNames here
	Entry.AdditionalOwnerNames = InAdditionalOwnerNames;

	// Always use the most permissive rule previously set
	if (Entry.Rules > Rules)
	{
		Entry.Rules = Rules;
	}

	ClearCacheAndBroadcast(Struct);
}

void FPropertyPermissionList::AddPermissionList(TSoftObjectPtr<const UStruct> Struct, const FNamePermissionList& PermissionList, const EPropertyPermissionListRules Rules)
{
	AddPermissionList(Struct, PermissionList, Rules, {});
}

void FPropertyPermissionList::RemovePermissionList(TSoftObjectPtr<const UStruct> Struct)
{
	if (RawPropertyPermissionList.Remove(Struct) > 0)
	{
		ClearCacheAndBroadcast(Struct);
	}
}

void FPropertyPermissionList::ClearPermissionList()
{
	TArray<TSoftObjectPtr<UStruct>> Keys;
	RawPropertyPermissionList.Reset();
	ClearCacheAndBroadcast();
}

void FPropertyPermissionList::UnregisterOwner(const FName Owner)
{
	TArray<TSoftObjectPtr<const UStruct>> StructsToRemove;

	for (TPair<TSoftObjectPtr<const UStruct>, FPropertyPermissionListEntry>& Pair : RawPropertyPermissionList)
	{
		Pair.Value.PermissionList.UnregisterOwner(Owner);
		Pair.Value.AdditionalOwnerNames.Remove(Owner);
		if (Pair.Value.AdditionalOwnerNames.Num() == 0 && Pair.Value.PermissionList.GetOwnerNames().Num() == 0)
		{
			StructsToRemove.Add(Pair.Key);
		}
	}

	{
		TGuardValue<bool> SuppressGuard(bSuppressUpdateDelegate, true);
		for (TSoftObjectPtr<const UStruct>& StructToRemove : StructsToRemove)
		{
			RemovePermissionList(StructToRemove);
		}
	}

	ClearCacheAndBroadcast(nullptr, Owner);
}

void FPropertyPermissionList::AddToAllowList(TSoftObjectPtr<const UStruct> Struct, const FName PropertyName, const FName Owner)
{
	FPropertyPermissionListEntry& Entry = RawPropertyPermissionList.FindOrAdd(Struct);
	if (Entry.PermissionList.AddAllowListItem(Owner, PropertyName))
	{
		ClearCacheAndBroadcast(Struct, Owner);
	}
}

void FPropertyPermissionList::AddToAllowList(TSoftObjectPtr<const UStruct> Struct, const TArray<FName>& PropertyNames, const FName Owner)
{
	FPropertyPermissionListEntry& Entry = RawPropertyPermissionList.FindOrAdd(Struct);
	bool bAddedItem = false;
	for (const FName& PropertyName : PropertyNames)
	{
		if (Entry.PermissionList.AddAllowListItem(Owner, PropertyName))
		{
			bAddedItem = true;
		}
	}

	if (bAddedItem)
	{
		ClearCacheAndBroadcast(Struct, Owner);
	}
}

void FPropertyPermissionList::RemoveFromAllowList(TSoftObjectPtr<const UStruct> Struct, const FName PropertyName, const FName Owner)
{
	FPropertyPermissionListEntry& Entry = RawPropertyPermissionList.FindOrAdd(Struct);
	if (Entry.PermissionList.RemoveAllowListItem(Owner, PropertyName))
	{
		ClearCacheAndBroadcast(Struct, Owner);
	}
}

void FPropertyPermissionList::AddToDenyList(TSoftObjectPtr<const UStruct> Struct, const FName PropertyName, const FName Owner)
{
	FPropertyPermissionListEntry& Entry = RawPropertyPermissionList.FindOrAdd(Struct);
	if (Entry.PermissionList.AddDenyListItem(Owner, PropertyName))
	{
		ClearCacheAndBroadcast(Struct, Owner);
	}
}

void FPropertyPermissionList::RemoveFromDenyList(TSoftObjectPtr<const UStruct> Struct, const FName PropertyName, const FName Owner)
{
	FPropertyPermissionListEntry& Entry = RawPropertyPermissionList.FindOrAdd(Struct);
	if (Entry.PermissionList.RemoveDenyListItem(Owner, PropertyName))
	{
		ClearCacheAndBroadcast(Struct, Owner);
	}
}

void FPropertyPermissionList::RegisterCustomDetailsTreeNodePermissionDelegate(TSoftObjectPtr<const UStruct> Struct, FCustomDetailTreeNodePermissionDelegate InDelegate)
{
	if(Struct && InDelegate.IsBound())
	{
		CustomDetailTreeNodePermissionDelegates.Add(Struct, InDelegate);
	}
}

void FPropertyPermissionList::UnregisterCustomDetailsTreeNodePermissionDelegate(TSoftObjectPtr<const UStruct> Struct)
{
	if(Struct && CustomDetailTreeNodePermissionDelegates.Contains(Struct))
	{
		CustomDetailTreeNodePermissionDelegates.Remove(Struct);
	}
}

void FPropertyPermissionList::SetEnabled(bool bEnable, bool bAnnounce)
{
	if (bEnablePermissionList != bEnable)
	{
		bEnablePermissionList = bEnable;
		if (bAnnounce)
		{
			PermissionListEnabledDelegate.Broadcast();
		}
	}
}

void FPropertyPermissionList::ClearCache()
{
	CachedPropertyPermissionList.Reset();
}

bool FPropertyPermissionList::HasFiltering(const UStruct* ObjectStruct) const
{
	return ObjectStruct ? GetCachedPermissionListForStruct(ObjectStruct).HasFiltering() : false;
}

bool FPropertyPermissionList::DoesPropertyPassFilter(const UStruct* ObjectStruct, FName PropertyName) const
{
	if (bEnablePermissionList && ObjectStruct)
	{
		return GetCachedPermissionListForStruct(ObjectStruct).PassesFilter(PropertyName);
	}
	return true;
}

bool FPropertyPermissionList::DoesDetailTreeNodePassFilter(const UStruct* ObjectStruct, TSharedRef<FDetailTreeNode> DetailTreeNode)
{
	if (bEnablePermissionList && ObjectStruct)
	{
		if(CustomDetailTreeNodePermissionDelegates.Contains(ObjectStruct) && CustomDetailTreeNodePermissionDelegates[ObjectStruct].IsBound())
		{
			TOptional<bool> CustomPermissionResult = CustomDetailTreeNodePermissionDelegates[ObjectStruct].Execute(DetailTreeNode);
			if(CustomPermissionResult.IsSet())
			{
				return CustomPermissionResult.GetValue();
			}
		}

		return GetCachedPermissionListForStruct(ObjectStruct).PassesFilter(DetailTreeNode->GetNodeName());
	}
	
	return true;
}

const FNamePermissionList& FPropertyPermissionList::GetCachedPermissionListForStruct(const UStruct* Struct) const
{
	return GetCachedPermissionListEntryForStructHelper(Struct).PermissionList;
}

const FPropertyPermissionListEntry& FPropertyPermissionList::GetCachedPermissionListEntryForStructHelper(const UStruct* Struct) const
{
	check(Struct);

#if !UE_BUILD_SHIPPING
	if (!PropertyEditorPermissionList::ClassToDebug.IsEmpty())
	{
		FNameBuilder StructPathName;
		Struct->GetPathName(nullptr, StructPathName);
		if (StructPathName.ToView().Contains(PropertyEditorPermissionList::ClassToDebug))
		{
			bool bSetBreakpointHere = true;
		}
	}
#endif

	// Is this struct already cached? If so, we can just return its result
	if (const FPropertyPermissionListEntry* CachedPermissionList = CachedPropertyPermissionList.Find(Struct))
	{
		return *CachedPermissionList;
	}

	const FPropertyPermissionListEntry* Entry = RawPropertyPermissionList.Find(Struct);

	FNamePermissionList NewPermissionList;

	auto AllowAllFields =
		[&NewPermissionList](const UStruct* InStruct, const EFieldIterationFlags InIterationFlags)
		{
			for (TFieldIterator<FProperty> Property(InStruct, InIterationFlags); Property; ++Property)
			{
				NewPermissionList.AddAllowListItem(PropertyEditorPermissionList::PropertyPermissionListOwner, Property->GetFName());
			}

			if (PropertyEditorPermissionList::bSupportAllowAllFunctions)
			{
				for (TFieldIterator<UFunction> FunctionIter(InStruct, InIterationFlags); FunctionIter; ++FunctionIter)
				{
					UFunction* TestFunction = *FunctionIter;
					if ((TestFunction->ParmsSize == 0) && TestFunction->GetBoolMetaData(PropertyEditorPermissionList::NAME_CallInEditor))
					{
						NewPermissionList.AddAllowListItem(PropertyEditorPermissionList::PropertyPermissionListOwner, TestFunction->GetFName());
					}
				}
			}
		};

	// Recursively fill the cache for all parent structs
	UStruct* SuperStruct = Struct->GetSuperStruct();
	EPropertyPermissionListRules SuperEntryRule = EPropertyPermissionListRules::UseExistingPermissionList;
	if (SuperStruct)
	{
		const FPropertyPermissionListEntry& SuperPermissionList = GetCachedPermissionListEntryForStructHelper(SuperStruct);
		SuperEntryRule = SuperPermissionList.Rules;
		NewPermissionList.Append(SuperPermissionList.PermissionList);
	}

	// Resolve the permission list rule for this entry
	EPropertyPermissionListRules EntryRule = EPropertyPermissionListRules::UseExistingPermissionList;
	if (Entry)
	{
		// This causes an issue in the case where a struct should have no AllowList properties but wants use AllowListAllSubclassProperties
		// In this case, simply add a dummy AllowList entry that (likely) won't ever collide with a real property name
		if (Entry->PermissionList.GetAllowList().Num() == 0 || Entry->Rules == EPropertyPermissionListRules::AllowListAllProperties)
		{
			EntryRule = EPropertyPermissionListRules::AllowListAllProperties;
		}
		else if (Entry->Rules == EPropertyPermissionListRules::AllowListAllSubclassProperties)
		{
			EntryRule = EPropertyPermissionListRules::AllowListAllSubclassProperties;
		}
	}
	else
	{
		// If we don't have a raw entry then we propagate the rule from our parent struct to avoid breaking AllowListAllProperties or AllowListAllSubclassProperties chains
		EntryRule = SuperEntryRule;
	}

	// If this entry has explicit rules, append them on-top of any permission rules inherited from our parent(s)
	if (Entry)
	{
		if (EntryRule == EPropertyPermissionListRules::AllowListAllProperties)
		{
			// Allow all fields if requested
			// If the allow list inherited from our parent(s) is empty then that already implies all fields are visible, so we can just keep the list as empty
			if (NewPermissionList.GetAllowList().Num() > 0)
			{
				AllowAllFields(Struct, EFieldIterationFlags::None);
			}

			// If the AllowList is empty, we only want to append the DenyLists
			FNamePermissionList DuplicatePermissionList = Entry->PermissionList;
			// Hack to get around the fact that there's no easy way to only clear an AllowList
			TMap<FName, FPermissionListOwners>& AllowList = const_cast<TMap<FName, FPermissionListOwners>&>(DuplicatePermissionList.GetAllowList());
			AllowList.Empty();
			NewPermissionList.Append(DuplicatePermissionList);
		}
		else
		{
			check(Entry->PermissionList.GetAllowList().Num() > 0);

			// If the parent struct is explicitly allowing all properties but has an empty allow list, then we need to explicitly allow all of its fields before appending our permissions
			if (SuperEntryRule == EPropertyPermissionListRules::AllowListAllProperties && NewPermissionList.GetAllowList().Num() == 0)
			{
				check(SuperStruct);
				AllowAllFields(SuperStruct, EFieldIterationFlags::IncludeSuper);
			}
			NewPermissionList.Append(Entry->PermissionList);
		}
	}

	// Did our super class ask for sub-classes to expose all their properties? If so, respect that request unless we define our own permission rules
	EPropertyPermissionListRules EntryRuleToPropagate = EntryRule;
	if ((!Entry || EntryRule == EPropertyPermissionListRules::AllowListAllProperties) && SuperEntryRule == EPropertyPermissionListRules::AllowListAllSubclassProperties)
	{
		EntryRuleToPropagate = EPropertyPermissionListRules::AllowListAllSubclassProperties;
		if (NewPermissionList.GetAllowList().Num() > 0)
		{
			AllowAllFields(Struct, EFieldIterationFlags::None);
		}
	}

	return CachedPropertyPermissionList.Add(Struct, {
		MoveTemp(NewPermissionList),
		TArray<FName>(), // Note: The AdditionalOwnerNames are only used to prevent removal from RawPropertyPermissionList by calls to RemoveOwner()
		EntryRuleToPropagate
		});
}

bool FPropertyPermissionList::HasSpecificList(const UStruct* ObjectStruct) const
{
	return RawPropertyPermissionList.Find(ObjectStruct) != nullptr;
}

bool FPropertyPermissionList::IsSpecificPropertyAllowListed(const UStruct* ObjectStruct, FName PropertyName) const
{
	const FPropertyPermissionListEntry* Entry = RawPropertyPermissionList.Find(ObjectStruct);
	if (Entry)
	{
		return Entry->PermissionList.GetAllowList().Contains(PropertyName);
	}
	return false;
}

bool FPropertyPermissionList::IsSpecificPropertyDenyListed(const UStruct* ObjectStruct, FName PropertyName) const
{
	const FPropertyPermissionListEntry* Entry = RawPropertyPermissionList.Find(ObjectStruct);
	if (Entry)
	{
		return Entry->PermissionList.GetDenyList().Contains(PropertyName);
	}
	return false;
}

FPropertyEditorPermissionList& FPropertyEditorPermissionList::Get()
{
	static FPropertyEditorPermissionList PermissionList;
	return PermissionList;
}

FHiddenPropertyPermissionList& FHiddenPropertyPermissionList::Get()
{
	static FHiddenPropertyPermissionList PermissionList;
	return PermissionList;
}


void FEnumValuePermissionList::AddPermissionList(TSoftObjectPtr<const UEnum> Enum, const FNamePermissionList& InPermissionList)
{
	FNamePermissionList& Entry = PermissionList.FindOrAdd(Enum);
	Entry = InPermissionList;
}

void FEnumValuePermissionList::RemovePermissionList(TSoftObjectPtr<const UEnum> Enum)
{
	PermissionList.Remove(Enum);
}

void FEnumValuePermissionList::ClearPermissionList()
{
	PermissionList.Reset();
}

bool FEnumValuePermissionList::DoesEnumValuePassFilter(const UEnum* Enum, FName ValueName) const
{
	if (!bEnablePermissionList)
	{
		return true;
	}

	const FNamePermissionList* Entry = PermissionList.Find(Enum);
	if (Entry)
	{
		return Entry->PassesFilter(ValueName);
	}
	return true;
}

bool FEnumValuePermissionList::HasFiltering(const UEnum* Enum) const
{
	if (!bEnablePermissionList)
	{
		return false;
	}

	const FNamePermissionList* Entry = PermissionList.Find(Enum);
	return Entry != nullptr;
}

FEnumValuePermissionList& FEnumValuePermissionList::Get()
{
	static FEnumValuePermissionList Instance;
	return Instance;
}
