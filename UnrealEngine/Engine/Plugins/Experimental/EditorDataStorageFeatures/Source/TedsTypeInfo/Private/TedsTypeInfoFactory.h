// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "DataStorage/CommonTypes.h"

#include "DataStorage/Handles.h"
#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "UObject/ObjectMacros.h"

#include "TedsTypeInfoFactory.generated.h"

struct FSolBuildResults;

class UStruct;
class UScriptStruct;
class UClass;
class UVerseClass;

namespace UE::Editor::DataStorage
{
	class ICoreProvider;
	class ICompatibilityProvider;
	class IHierarchyAccessInterface;
} // UE::Editor::DataStorage

USTRUCT(meta = (DisplayName = "Row requires a pass to assign its type hierarchy info"))
struct FDataStorageTypeInfoRequiresHierarchyUpdateTag : public FEditorDataStorageTag
{
	GENERATED_BODY()
};

UCLASS()
class UTypeInfoFactory final : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	static const FName TypeTableName;
	static const FName ClassSetupActionName;

	virtual ~UTypeInfoFactory() override = default;

	//~ Begin UEditorDataStorageFactory interface
	void PreRegister(UE::Editor::DataStorage::ICoreProvider& DataStorage) override;
	void RegisterTables(UE::Editor::DataStorage::ICoreProvider& DataStorage, UE::Editor::DataStorage::ICompatibilityProvider& DataStorageCompatability) override;
	void RegisterQueries(UE::Editor::DataStorage::ICoreProvider& DataStorage) override;
	void PreShutdown(UE::Editor::DataStorage::ICoreProvider& DataStorage) override;
	//~ End UEditorDataStorageFactory interface

	UE::Editor::DataStorage::TableHandle GetTypeTable() const;

	TEDSTYPEINFO_API void ClearAllTypeInfo();
	TEDSTYPEINFO_API void RefreshAllTypeInfo();

private:

	void OnDatabaseUpdateCompleted();

	template <class Type>
	void PopulateTypeInfo();

	template <class TypeToClear>
	void ClearTypeInfoByTag();

	bool TryAddTypeInfoRow(const UStruct* TypeInfo);

	bool FilterStructInfo(const UStruct* StructInfo);
	bool FilterClassInfo(const UClass* ClassInfo);

	void AddCommonColumns(UE::Editor::DataStorage::ICoreProvider& DataStorage, UE::Editor::DataStorage::RowHandle TypeRowHandle, const UStruct* InTypeInfo);
	void AddStructColumns(UE::Editor::DataStorage::ICoreProvider& DataStorage, UE::Editor::DataStorage::RowHandle TypeRowHandle, const UScriptStruct* InStructInfo);
	void AddClassColumns(UE::Editor::DataStorage::ICoreProvider& DataStorage, UE::Editor::DataStorage::RowHandle TypeRowHandle, const UClass* InClassInfo);
	void AddVerseColumns(UE::Editor::DataStorage::ICoreProvider& DataStorage, UE::Editor::DataStorage::RowHandle TypeRowHandle, const UVerseClass* InVerseTypeInfo);

	bool bRefreshTypeInfoQueued = false;

	const UE::Editor::DataStorage::IHierarchyAccessInterface* ClassHierarchyAccessInterface = nullptr;

	UE::Editor::DataStorage::TableHandle TypeTable = UE::Editor::DataStorage::InvalidTableHandle;
};

namespace UE::Editor::DataStorage
{
	using FTypeInfoRequiresHierarchyUpdateTag = FDataStorageTypeInfoRequiresHierarchyUpdateTag;
}