// Copyright Epic Games, Inc. All Rights Reserved.

#include "TedsTypeInfoFactory.h"

#include "Elements/Columns/TedsTypeInfoColumns.h"

#include "Modules/ModuleManager.h"
#include "ClassViewerFilter.h"
#include "ClassViewerModule.h"

#include "TedsTypeInfoModule.h"

#include "DataStorage/Features.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"

#include "HAL/IConsoleManager.h"

#include "UObject/UObjectIterator.h"
#include "VerseVM/VVMVerseClass.h"

const FName UTypeInfoFactory::TypeTableName = "Type Information";
const FName UTypeInfoFactory::ClassSetupActionName = "ClassSetupAction";

void UTypeInfoFactory::PreRegister(UE::Editor::DataStorage::ICoreProvider& DataStorage)
{
	DataStorage.OnUpdateCompleted().AddUObject(this, &UTypeInfoFactory::OnDatabaseUpdateCompleted);
}

void UTypeInfoFactory::RegisterTables(UE::Editor::DataStorage::ICoreProvider& DataStorage, UE::Editor::DataStorage::ICompatibilityProvider& DataStorageCompatability)
{
	using namespace UE::Editor::DataStorage;

	Super::RegisterTables(DataStorage, DataStorageCompatability);

	const FHierarchyRegistrationParams Params
	{
		.Name = TEXT("ClassHierarchy"),
	};

	FHierarchyHandle Handle = DataStorage.RegisterHierarchy(Params);

	TypeTable = DataStorage.RegisterTable({
		FTypedElementLabelColumn::StaticStruct(),
		FTypeInfoTag::StaticStruct(),
		FTypeInfoRequiresHierarchyUpdateTag::StaticStruct()
		}, TypeTableName);
}

void UTypeInfoFactory::RegisterQueries(UE::Editor::DataStorage::ICoreProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage;
	using namespace UE::Editor::DataStorage::Queries;

	DataStorage.RegisterQuery(
		Select(
			TEXT("Setup Class Hierarchy Info"),
			FProcessor(EQueryTickPhase::DuringPhysics, DataStorage.GetQueryTickGroupName(EQueryTickGroups::Default))
				.MakeActivatable(ClassSetupActionName),
			[this](IQueryContext& Context, RowHandle Row, const FTypedElementClassTypeInfoColumn& ClassTypeColumn)
			{
				if (const UClass* ClassInfo = ClassTypeColumn.TypeInfo.Get())
				{
					const UClass* ParentClass = ClassInfo->GetSuperClass();

					RowHandle ParentClassRow = Context.LookupMappedRow(TypeInfo::TypeMappingDomain, FMapKey(ParentClass));

					Context.SetParentRow(Row, ParentClassRow);
				}

				Context.RemoveColumns(Row, { FTypeInfoRequiresHierarchyUpdateTag::StaticStruct() });
			})
			.AccessesHierarchy(TEXT("ClassHierarchy"))
			.Where()
				.All<FTypeInfoRequiresHierarchyUpdateTag>()
			.Compile());

	// Populate our type info on register if our CVar is enabled
	static const auto TedsTypeInfoEnabledCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("TEDS.Feature.TypeInfoIntegration.Enable"));
	if (TedsTypeInfoEnabledCVar && TedsTypeInfoEnabledCVar->GetBool())
	{
		RefreshAllTypeInfo();
	}

	TypeInfo::FTedsTypeInfoModule::GetChecked().SetTypeInfoFactoryEnabled();
}

void UTypeInfoFactory::PreShutdown(UE::Editor::DataStorage::ICoreProvider& DataStorage)
{
	DataStorage.OnUpdateCompleted().RemoveAll(this);
}

void UTypeInfoFactory::OnDatabaseUpdateCompleted()
{
	if (bRefreshTypeInfoQueued)
	{
		PopulateTypeInfo<UClass>();
		bRefreshTypeInfoQueued = false;
	}
}

UE::Editor::DataStorage::TableHandle UTypeInfoFactory::GetTypeTable() const
{
	return TypeTable;
}

void UTypeInfoFactory::ClearAllTypeInfo()
{
	using namespace UE::Editor::DataStorage;
	ClearTypeInfoByTag<FTypeInfoTag>();
}

void UTypeInfoFactory::RefreshAllTypeInfo()
{
	ClearAllTypeInfo();
	bRefreshTypeInfoQueued = true;
}

template <class Type>
void UTypeInfoFactory::PopulateTypeInfo()
{
	using namespace UE::Editor::DataStorage;

	// Block adding anything to the data table if our CVar is off
	static const auto TedsTypeInfoEnabledCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("TEDS.Feature.TypeInfoIntegration.Enable"));
	if (TedsTypeInfoEnabledCVar && !TedsTypeInfoEnabledCVar->GetBool())
	{
		return;
	}

	for (TObjectIterator<Type> TypeIterator; TypeIterator; ++TypeIterator)
	{
		TryAddTypeInfoRow(*TypeIterator);
	}

	ICoreProvider* DataStorage = GetMutableDataStorageFeature<ICoreProvider>(StorageFeatureName);
	DataStorage->ActivateQueries(ClassSetupActionName);
}

bool UTypeInfoFactory::TryAddTypeInfoRow(const UStruct* TypeInfo)
{
	using namespace UE::Editor::DataStorage;

	ICoreProvider* DataStorage = GetMutableDataStorageFeature<ICoreProvider>(StorageFeatureName);
	checkf(DataStorage, TEXT("Attempted to add type info to Teds with a null DataStorage"));

	// We have 2 major type cases: Classes and Structs
	
	// Case 1: Classes and Verse Classes
	const UClass* ClassInfo = Cast<UClass>(TypeInfo);
	if (ClassInfo && FilterClassInfo(ClassInfo))
	{
		if (RowHandle TypeRowHandle = DataStorage->AddRow(TypeTable))
		{
			AddCommonColumns(*DataStorage, TypeRowHandle, ClassInfo);
			AddClassColumns(*DataStorage, TypeRowHandle, ClassInfo);

			// Only Classes can be UVerseClasses
			if (const UVerseClass* VerseClassInfo = Cast<UVerseClass>(ClassInfo))
			{
				AddVerseColumns(*DataStorage, TypeRowHandle, VerseClassInfo);
			}

			DataStorage->MapRow(TypeInfo::TypeMappingDomain, FMapKey(ClassInfo), TypeRowHandle);
			return true;
		}
	}

	// Case 2: Structs
	// Currently unsupported as filter will return false
	const UScriptStruct* StructData = Cast<UScriptStruct>(TypeInfo);
	if (StructData && FilterStructInfo(StructData))
	{
		if (RowHandle TypeRowHandle = DataStorage->AddRow(TypeTable))
		{
			AddCommonColumns(*DataStorage, TypeRowHandle, StructData);
			AddStructColumns(*DataStorage, TypeRowHandle, StructData);

			DataStorage->MapRow(TypeInfo::TypeMappingDomain, FMapKey(StructData), TypeRowHandle);
			return true;
		}
	}

	return false;
}

bool UTypeInfoFactory::FilterStructInfo(const UStruct* /*StructInfo*/)
{
	// Structs not supported currently
	return false;
}

bool UTypeInfoFactory::FilterClassInfo(const UClass* ClassInfo)
{
	if (!ClassInfo)
	{
		return false;
	}

	FClassViewerModule& ClassViewerModule = FModuleManager::LoadModuleChecked<FClassViewerModule>("ClassViewer");
	const TSharedPtr<IClassViewerFilter>& GlobalClassFilter = ClassViewerModule.GetGlobalClassViewerFilter();
	TSharedRef<FClassViewerFilterFuncs> ClassFilterFuncs = ClassViewerModule.CreateFilterFuncs();

	FClassViewerInitializationOptions ClassViewerOptions = {};
	const bool bPassesClassViewerFilter = !GlobalClassFilter.IsValid() || GlobalClassFilter->IsClassAllowed(ClassViewerOptions, ClassInfo, ClassFilterFuncs);

	return bPassesClassViewerFilter;
}

void UTypeInfoFactory::AddCommonColumns(UE::Editor::DataStorage::ICoreProvider& DataStorage, UE::Editor::DataStorage::RowHandle TypeRowHandle, const UStruct* InTypeInfo)
{
	using namespace UE::Editor::DataStorage;
	checkf(InTypeInfo, TEXT("Attempted to add type info columns with null TypeInfo"));

	DataStorage.AddColumn(TypeRowHandle, FTypedElementLabelColumn{ .Label = InTypeInfo->GetDisplayNameText().ToString() });
	DataStorage.AddColumns<FTypeInfoTag, FTypeInfoRequiresHierarchyUpdateTag>(TypeRowHandle);
}

void UTypeInfoFactory::AddStructColumns(UE::Editor::DataStorage::ICoreProvider& DataStorage, UE::Editor::DataStorage::RowHandle TypeRowHandle, const UScriptStruct* InStructInfo)
{
	using namespace UE::Editor::DataStorage;
	checkf(InStructInfo, TEXT("Attempted to add type info columns with null StructInfo"));

	DataStorage.AddColumn(TypeRowHandle, FTypedElementScriptStructTypeInfoColumn{ .TypeInfo = InStructInfo });
	DataStorage.AddColumn<FStructTypeInfoTag>(TypeRowHandle);
}

void UTypeInfoFactory::AddClassColumns(UE::Editor::DataStorage::ICoreProvider& DataStorage, UE::Editor::DataStorage::RowHandle TypeRowHandle, const UClass* InClassInfo)
{
	using namespace UE::Editor::DataStorage;
	checkf(InClassInfo, TEXT("Attempted to add type info columns with null ClassInfo"));

	DataStorage.AddColumn(TypeRowHandle, FTypedElementClassTypeInfoColumn{ .TypeInfo = InClassInfo });

	if (InClassInfo->HasAnyClassFlags(CLASS_Interface))
	{
		DataStorage.AddColumns<FClassTypeInfoTag, FTypeInfoInterfaceTag>(TypeRowHandle);
	}
	else
	{
		DataStorage.AddColumn<FClassTypeInfoTag>(TypeRowHandle);
	}
}

void UTypeInfoFactory::AddVerseColumns(UE::Editor::DataStorage::ICoreProvider& DataStorage, UE::Editor::DataStorage::RowHandle TypeRowHandle, const UVerseClass* InVerseTypeInfo)
{
	using namespace UE::Editor::DataStorage;
	checkf(InVerseTypeInfo, TEXT("Attempted to add type info columns with null VerseTypeInfo"));

	DataStorage.AddColumn<FVerseTypeInfoTag>(TypeRowHandle);

	if (InVerseTypeInfo->IsUniversallyAccessible())
	{
		DataStorage.AddColumn<FVerseTypeInfoAccessLevel>(TypeRowHandle, TEXT("Public"));
	}
	else if (InVerseTypeInfo->IsEpicInternal())
	{
		DataStorage.AddColumn<FVerseTypeInfoAccessLevel>(TypeRowHandle, TEXT("Epic_Internal"));
	}
}

template <class TypeToClear>
void UTypeInfoFactory::ClearTypeInfoByTag()
{
	using namespace UE::Editor::DataStorage;
	ICoreProvider* DataStorage = GetMutableDataStorageFeature<ICoreProvider>(StorageFeatureName);

	checkf(DataStorage, TEXT("Attempted to clear type info from Teds with a null DataStorage"));

	DataStorage->RemoveAllRowsWith<TypeToClear>();
}