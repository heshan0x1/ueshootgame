// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>
#include "Containers/Array.h"
#include "Containers/ContainersFwd.h"
#include "DataStorage/CommonTypes.h"
#include "DataStorage/Handles.h"
#include "DataStorage/MapKey.h"
#include "DataStorage/Queries/Description.h"
#include "DataStorage/Queries/Types.h"
#include "DataStorage/Queries/Conditions.h"
#include "Delegates/Delegate.h"
#include "Elements/Framework/TypedElementColumnUtils.h"
#include "Elements/Framework/TypedElementQueryCapabilities.h"
#include "Elements/Framework/TypedElementQueryFunctions.h"
#include "Elements/Interfaces/TypedElementQueryStorageInterfaces.h"
#include "Elements/Framework/TypedElementRowHandleArray.h"
#include "Elements/Framework/TypedElementRowHandleArrayView.h"
#include "Features/IModularFeature.h"
#include "Math/NumericLimits.h"
#include "Misc/EnumClassFlags.h"
#include "Misc/Timespan.h"
#include "Templates/Function.h"
#include "UObject/Interface.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"

class UClass;
class USubsystem;
class UScriptStruct;
class UEditorDataStorageFactory;

namespace UE::Editor::DataStorage
{
using FTypedElementOnDataStorageCreation = FSimpleMulticastDelegate;
using FTypedElementOnDataStorageDestruction = FSimpleMulticastDelegate;
using FTypedElementOnDataStorageUpdate = FSimpleMulticastDelegate;

/**
 * Convenience structure that can be used to pass a list of columns to functions that don't
 * have an dedicate templated version that takes a column list directly, for instance when
 * multiple column lists are used. Note that the returned array view is only available while
 * this object is constructed, so care must be taken with functions that return a const array view.
 */
template<TColumnType... Columns>
struct TTypedElementColumnTypeList
{
	const UScriptStruct* ColumnTypes[sizeof...(Columns)] = { Columns::StaticStruct()... };
	
	operator TConstArrayView<const UScriptStruct*>() const { return ColumnTypes; }
};

struct FHierarchyRegistrationParams
{
	FName Name;

	// Optional column that is added to a row when the parent changes (useful for change detection).
	// This behaves similar to the sync tags and is removed at the end of the frame
	const UScriptStruct* ParentChangedColumn = nullptr;
};
	
class ICoreProvider : public IModularFeature
{
public:
	/**
	 * @section Factories
	 *
	 * @description
	 * Factories are an automated way to register tables, queries and other information with TEDS.
	 */

	/** Finds a factory instance registered with TEDS */
	virtual const UEditorDataStorageFactory* FindFactory(const UClass* FactoryType) const = 0;

	virtual UEditorDataStorageFactory* FindFactory(const UClass* FactoryType) = 0;

	/** Convenience function for FindFactory */
	template<typename FactoryT>
	const FactoryT* FindFactory() const;
	
	template<typename FactoryT>
	FactoryT* FindFactory();

	/**
	 * @section Table management
	 * 
	 * @description
	 * Tables are automatically created by taking an existing table and adding/removing columns. For
	 * performance its however better to create a table before adding objects to the table. This
	 * doesn't prevent those objects from having columns added/removed at a later time.
	 * To make debugging and profiling easier it's also recommended to give tables a name.
	 */

	/** Creates a new table for with the provided columns. Optionally a name can be given which is useful for retrieval later. */
	virtual TableHandle RegisterTable(TConstArrayView<const UScriptStruct*> ColumnList, const FName& Name) = 0;
	template<TColumnType... Columns>
	TableHandle RegisterTable(const FName& Name);
	/** 
	 * Copies the column information from the provided table and creates a new table for with the provided columns. Optionally a 
	 * name can be given which is useful for retrieval later.
	 */
	virtual TableHandle RegisterTable(TableHandle SourceTable,
		TConstArrayView<const UScriptStruct*> ColumnList, const FName& Name) = 0;
	template<TColumnType... Columns>
	TableHandle RegisterTable(TableHandle SourceTable, const FName& Name);

	/** Returns a previously created table with the provided name or TypedElementInvalidTableHandle if not found. */
	virtual TableHandle FindTable(const FName& Name) = 0;
	
	/**
	 * @section Row management
	 */

	/** 
	 * Reserves a row to be assigned to a table at a later point. If the row is no longer needed before it's been assigned
	 * to a table, it should still be released with RemoveRow.
	 */
	virtual RowHandle ReserveRow() = 0;
	/**
	 * Reserve multiple rows at once to be assigned to a table at a later point. If multiple rows are needed, the batch version will
	 * generally have better performance. If a row is no longer needed before it's been assigned to a table, it should still be released 
	 * with RemoveRow.
	 * The reservation callback will be called once per reserved row.
	 */
	virtual void BatchReserveRows(int32 Count, TFunctionRef<void(RowHandle)> ReservationCallback) = 0;
	/**
	 * Reserve multiple rows at once to be assigned to a table at a later point. If multiple rows are needed, the batch version will
	 * generally have better performance. If a row is no longer needed before it's been assigned to a table, it should still be released
	 * with RemoveRow.
	 * The provided range will be have its values set to the reserved row handles.
	 */
	virtual void BatchReserveRows(TArrayView<RowHandle> ReservedRows) = 0;

	/** Adds a new row to the provided table. */
	virtual RowHandle AddRow(TableHandle Table) = 0;
	/**
	 * Adds a new row to the provided table. Callers are expected to use the callback to
	 * initialize the row if needed.
	 */
	virtual RowHandle AddRow(TableHandle Table,
		RowCreationCallbackRef OnCreated) = 0;
	/** Adds a new row to the provided table using a previously reserved row. */
	virtual bool AddRow(RowHandle ReservedRow, TableHandle Table) = 0;
	/**
	 * Adds a new row to the provided table using a previously reserved row. Callers are expected to use the callback to
	 * initialize the row if needed.
	 */
	virtual bool AddRow(RowHandle ReservedRow, TableHandle Table,
		RowCreationCallbackRef OnCreated) = 0;

	/**
	 * Add multiple rows at once. For each new row the OnCreated callback is called. Callers are expected to use the callback to
	 * initialize the row if needed.
	 */
	virtual bool BatchAddRow(TableHandle Table, int32 Count, RowCreationCallbackRef OnCreated) = 0;
	/**
	 * Add multiple rows at once. For each new row the OnCreated callback is called. Callers are expected to use the callback to
	 * initialize the row if needed. This version uses a set of previously reserved rows. Any row that can't be used will be 
	 * released.
	 */
	virtual bool BatchAddRow(TableHandle Table, TConstArrayView<RowHandle> ReservedHandles,
		RowCreationCallbackRef OnCreated) = 0;

	/** Removes a previously reserved or added row. If the row handle is invalid or already removed, nothing happens */
	virtual void RemoveRow(RowHandle Row) = 0;

	/** Removes multiple rows at one. If any of the rows are invalid or already removed, nothing happens. */
	virtual void BatchRemoveRows(TConstArrayView<RowHandle> Rows) = 0;

	/** 
	 * Removes all rows that have at least the provided columns.
	 * This can be used to for instance remove all rows with a specific type tag, such as removing all rows with
	 * an entity tag to remove all entities from the data storage.
	 */
	virtual void RemoveAllRowsWithColumns(TConstArrayView<const UScriptStruct*> Columns) = 0;

	/**
	 * Removes all rows that have at least the provided columns as template arguments.
	 * This can be used to for instance remove all rows with a specific type tag, such as removing all rows with
	 * an entity tag to remove all entities from the data storage.
	 */
	template<TColumnType... Columns>
	void RemoveAllRowsWith();

	/** Checks whether or not a row is in use. This is true even if the row has only been reserved. */
	virtual bool IsRowAvailable(RowHandle Row) const = 0;
	/** Checks whether or not a row has been reserved but not yet assigned to a table. */
	virtual bool IsRowAssigned(RowHandle Row) const = 0;

	enum class EFilterOptions : uint8
	{
		/** Include rows that have the columns required for the filter function and the filter function returns true. */
		Inclusive,
		/** Exclude rows that have the columns required for the filter function and the filter function returns true. */
		Exclusive
	};

	/** Filters the provided rows using the query filter and stores the results in the provided list. */
	virtual void FilterRowsBy(
		FRowHandleArray& Result, FRowHandleArrayView Input, EFilterOptions Options, Queries::TQueryFunction<bool>& Filter) = 0;
	/** Filters the provided rows using the query filter and stores the results in the provided list. */
	template<typename FilterFunction>
	void FilterRowsBy(FRowHandleArray& Result, FRowHandleArrayView Input, EFilterOptions Options, FilterFunction&& Filter);

	/**
	 * @section Column management
	 */

	/** Adds a column to a row or does nothing if already added. */
	virtual void AddColumn(RowHandle Row, const UScriptStruct* ColumnType) = 0;
	template<TColumnType ColumnType>
	void AddColumn(RowHandle Row);
	/**
	 * Adds a new data column and initializes it. The relocator will be used to copy or move the column out of
	 * its temporary location into the final table if the addition needs to be deferred.
	 */
	virtual void AddColumnData(RowHandle Row, const UScriptStruct* ColumnType,
		const ColumnCreationCallbackRef Initializer,
		ColumnCopyOrMoveCallback Relocator) = 0;
	template<TDataColumnType ColumnType>
	void AddColumn(RowHandle Row, ColumnType&& Column);

	/**
	 * Adds a ValueTag with the given value to a row
	 * A row can have multiple ValueTags, but only one of each tag type.
	 * Example:
	 *   AddColumn(Row, ValueTag(TEXT("Color"), TEXT("Red));     // Valid
	 *   AddColumn(Row, ValueTag(TEXT("Direction"), TEXT("Up")); // Valid
	 *   AddColumn(Row, ValueTag(TEXT("Color"), TEXT("Blue"));   // Will do nothing since there already exists a Color value tag
	 * Note: Current support for changing a value tag from one value to another requires that the tag is removed before a new one
	 *       is added.  This will likely change in the future to transparently replace the tag to have consistent behaviour with other usages
	 *       of AddColumn
	 */
	virtual void AddColumn(RowHandle Row, const FValueTag& Tag, const FName& Value) = 0;

	template<typename T>
	void AddColumn(RowHandle Row, const FName& Tag) = delete;
	
	template<typename T>
	void AddColumn(RowHandle Row, const FName& Tag, const FName& Value) = delete;
	
	template<>
	void AddColumn<FValueTag>(RowHandle Row, const FName& Tag, const FName& Value);

	template<TEnumType EnumT>
	void AddColumn(RowHandle Row, EnumT Value);
	
	template<auto Value, TEnumType EnumT = decltype(Value)>
	void AddColumn(RowHandle Row);

	template<TDynamicColumnTemplate DynamicColumnTemplate>
	void AddColumn(RowHandle Row, const FName& Identifier);
	
	template<TDynamicColumnTemplate DynamicColumnTemplate>
	void AddColumn(RowHandle Row, const FName& Identifier, DynamicColumnTemplate&& TemplateInstance);

	/**
	 * Adds multiple columns from a row. This is typically more efficient than adding columns one 
	 * at a time.
	 */
	virtual void AddColumns(RowHandle Row, TConstArrayView<const UScriptStruct*> Columns) = 0;
	template<TColumnType... Columns>
	void AddColumns(RowHandle Row);

	/** Removes a column from a row or does nothing if already removed. */
	virtual void RemoveColumn(RowHandle Row, const UScriptStruct* ColumnType) = 0;
	template<TColumnType Column>
	void RemoveColumn(RowHandle Row);

	template<TEnumType EnumT>
	void RemoveColumn(RowHandle Row);

	/**
	 * Removes a value tag from the given row
	 * If tag does not exist on row, operation will do nothing.
	 */
	virtual void RemoveColumn(RowHandle Row, const FValueTag& Tag) = 0;

	template<typename T>
	void RemoveColumn(RowHandle Row, const FName& Tag) = delete;

	template<TDynamicColumnTemplate DynamicColumnTemplateType>
	void RemoveColumn(RowHandle Row, const FName& Identifier);
	
	template<>
	void RemoveColumn<FValueTag>(RowHandle Row, const FName& Tag);

	/**
	 * Removes multiple columns from a row. This is typically more efficient than adding columns one
	 * at a time.
	 */
	virtual void RemoveColumns(RowHandle Row, TConstArrayView<const UScriptStruct*> Columns) = 0;
	template<TColumnType... Columns>
	void RemoveColumns(RowHandle Row);

	/** 
	 * Adds and removes the provided column types from the provided row. This is typically more efficient 
	 * than individually adding and removing columns as well as being faster than adding and removing
	 * columns separately.
	 */
	virtual void AddRemoveColumns(RowHandle Row, TConstArrayView<const UScriptStruct*> ColumnsToAdd,
		TConstArrayView<const UScriptStruct*> ColumnsToRemove) = 0;
	
	/** Adds and removes the provided column types from the provided list of rows. */
	virtual void BatchAddRemoveColumns(
		TConstArrayView<RowHandle> Rows,
		TConstArrayView<const UScriptStruct*> ColumnsToAdd,
		TConstArrayView<const UScriptStruct*> ColumnsToRemove) = 0;
	
	/** Retrieves a pointer to the column of the given row or a nullptr if not found or if the column type is a tag. */
	virtual void* GetColumnData(RowHandle Row, const UScriptStruct* ColumnType) = 0;
	virtual const void* GetColumnData(RowHandle Row, const UScriptStruct* ColumnType) const = 0;
	/** Returns a pointer to the column of the given row or a nullptr if the type couldn't be found or the row doesn't exist. */
	template<TDataColumnType ColumnType>
	ColumnType* GetColumn(RowHandle Row);
	template<TDataColumnType ColumnType>
	const ColumnType* GetColumn(RowHandle Row) const;
	// Gets a dynamic column identified by the ColumnTypeTemplate and Identifier
	template<TDynamicColumnTemplate ColumnTypeTemplate>
	ColumnTypeTemplate* GetColumn(RowHandle Row, const FName& Identifer);
	template<TDynamicColumnTemplate ColumnTypeTemplate>
	const ColumnTypeTemplate* GetColumn(RowHandle Row, const FName& Identifer) const;
	
	/** 
	 * Determines if the provided row contains the collection of columns and tags.
	 * Note that for rows that haven't been assigned yet it's not possible to check if a column exists as the table to check for hasn't
	 * been assigned yet. In these cases a list of known additions will be checked as these will be added once the table is assigned.
	 */
	virtual bool HasColumns(RowHandle Row, TConstArrayView<const UScriptStruct*> ColumnTypes) const = 0;
	virtual bool HasColumns(RowHandle Row, TConstArrayView<TWeakObjectPtr<const UScriptStruct>> ColumnTypes) const = 0;
	template<TColumnType... ColumnTypes>
	bool HasColumns(RowHandle Row) const;

	/** 
	 * Lists the columns on a row. This includes data and tag columns.
	 * Note that for rows that haven't been assigned yet it's not possible to return the full list as no table has been assigned yet.
	 * In these cases a list of known additions will be returned as these will be added once the table is assigned.
	 */
	virtual void ListColumns(RowHandle Row, ColumnListCallbackRef Callback) const = 0;

	/** 
	 * Lists the column type and data on a row. This includes data and tag columns. Not all columns may have data so the data pointer in 
	 * the callback can be null.
	 * Note that for rows that haven't been assigned yet it's not possible to return the full list as no table has been assigned yet.
	 * In these cases a list of known additions will be returned.
	 */
	virtual void ListColumns(RowHandle Row, ColumnListWithDataCallbackRef Callback) = 0;

	/** Determines if the columns in the row match the query conditions. */
	virtual bool MatchesColumns(RowHandle Row, const Queries::FConditions& Conditions) const = 0;

	/**
	 * Finds the type information for a dynamic column.
	 * If the dynamic column has not been generated, then return nullptr
	 * The TemplateType may be a typed derived from either FColumn or FTag, anything else will return nullptr
	 */
	virtual const UScriptStruct* FindDynamicColumn(const FDynamicColumnDescription& Description) const = 0;

	/**
	 * Generates a new dynamic column from a Template.  A dynamic column is uniquely identified using the given template and an Identifier
	 * This function is idempotent - multiple calls with the same parameters will result in subsequent calls returning the same type
	 * The TemplateType may be a typed derived from either FColumn or FTag
	 */
	virtual const UScriptStruct* GenerateDynamicColumn(const FDynamicColumnDescription& Description) = 0;

	/**
	 * Executes the given callback for each known dynamic column that derives from the base template provided
	 */
	virtual void ForEachDynamicColumn(const UScriptStruct& Template, TFunctionRef<void(const UScriptStruct& Type)> Callback) const = 0;

	/**
	 * Executes the given callback for each known dynamic column that derives from the base template provided
	 */
	template<TDynamicColumnTemplate DynamicColumnTemplateType>
	void ForEachDynamicColumn(TFunctionRef<void(const UScriptStruct& Type)> Callback) const;

	// Hierarchy Interface
	//-----------------------
	
	virtual UE::Editor::DataStorage::FHierarchyHandle RegisterHierarchy(const UE::Editor::DataStorage::FHierarchyRegistrationParams& Params) = 0;
	virtual UE::Editor::DataStorage::FHierarchyHandle FindHierarchyByName(const FName& Name) const = 0;
	virtual bool IsValidHierachyHandle(FHierarchyHandle) const = 0;

	// Gets the tag type indicating the row is a child in this hierarchy and thus has a parent
	virtual const UScriptStruct* GetChildTagType(FHierarchyHandle InHierarchyHandle) const = 0;
	// Gets the tag type indicating the row is a parent in this hierarchy and thus has a child
	virtual const UScriptStruct* GetParentTagType(FHierarchyHandle InHierarchyHandle) const = 0;
	// Gets the column type storing the hierarchy data. Note that the format of this type is opaque
	// and is used internally.  It is the caller's responsibility to provide at least Read access
	// to this column type in query requirements in order to use the HierarchyAccessInterface on the row
	virtual const UScriptStruct* GetHierarchyDataColumnType(FHierarchyHandle InHierarchyHandle) const = 0;
	// List all the currently known hierarchies by name
	virtual void ListHierarchyNames(TFunctionRef<void(const FName& HierarchyName)> Callback) const = 0;

	// CoreProvider Context API
	// =========================

	// Establishes a parent relationship between the Target and the Parent
	virtual void SetParentRow(
		FHierarchyHandle InHierarchyHandle,
		RowHandle Target,
		RowHandle Parent) = 0;

	/**
	 * Establishes a parent relationship between the Target row and a Parent that is not registered in TEDS yet.
	 * Every frame, TEDS will attempt to resolve the missing relation by looking up the parent using TEDS Mapping.
	 * An optional MaxFrameCount can be specified, which will be decremented every frame until the potential relationship is discarded
	 */
	virtual void SetUnresolvedParent(
		FHierarchyHandle InHierarchyHandle,
		RowHandle Target,
		FMapKey ParentId,
		FName MappingDomain) = 0;

	// Gets the parent of a target, if there is one
	virtual RowHandle GetParentRow(FHierarchyHandle InHierarchyHandle, RowHandle Target) const = 0;
	
	// Returns a callable which will extract the parent row from the hierarchy's HierarchyDataColumn
	// Note that the second parameter must be the same as what is returned by GetHierarchyDataColumn()
	// and the first parameter should point to a struct of that type
	virtual TFunction<RowHandle(const void*, const UScriptStruct*)> CreateParentExtractionFunction(FHierarchyHandle InHierarchyHandle) const = 0; 
	
	virtual bool HasChildren(FHierarchyHandle InHierarchyHandle, RowHandle Row) const = 0;

	// Iterates depth first from the passed in Row, calling the VisitFn on each row
	// The Row passed in will have VisitFn called on it
	virtual void WalkDepthFirst(
		FHierarchyHandle InHierarchyHandle,
		RowHandle Row,
		TFunction<void(const ICoreProvider& Context, RowHandle Owner, RowHandle Target)> VisitFn) const = 0;
	
	/**
	 * Outputs the registered query callbacks to the given output device for debugging purposes.
	 */
	virtual void DebugPrintQueryCallbacks(FOutputDevice& Output) = 0;
	
	/**
	 * @section Query
	 * @description
	 * Queries can be constructed using the Query Builder. Note that the Query Builder allows for the creation of queries that
	 * are more complex than the back-end may support. The back-end is allowed to simplify the query, in which case the query
	 * can be used directly in the processor to do additional filtering. This will however impact performance and it's 
	 * therefore recommended to try to simplify the query first before relying on extended query filtering in a processor.
	 */

	/** 
	 * Registers a query with the data storage. The description is processed into an internal format and may be changed. If no valid
	 * could be created an invalid query handle will be returned. It's recommended to use the Query Builder for a more convenient
	 * and safer construction of a query.
	 */
	virtual QueryHandle RegisterQuery(FQueryDescription&& Query) = 0;
	/** Removes a previous registered. If the query handle is invalid or the query has already been deleted nothing will happen. */
	virtual void UnregisterQuery(QueryHandle Query) = 0;
	/** Returns the description of a previously registered query. If the query no longer exists an empty description will be returned. */
	virtual const FQueryDescription& GetQueryDescription(QueryHandle Query) const = 0;
	/**
	 * Tick groups for queries can be given any name and the Data Storage will figure out the order of execution based on found
	 * dependencies. However keeping processors within the same query group can help promote better performance through parallelization.
	 * Therefore a collection of common tick group names is provided to help create consistent tick group names.
	 */
	virtual FName GetQueryTickGroupName(EQueryTickGroups Group) const = 0;
	/** Directly runs a query. If the query handle is invalid or has been deleted nothing will happen. */
	virtual FQueryResult RunQuery(QueryHandle Query) = 0;
	/**
	 * Directly runs a query. The callback will be called for batches of matching rows. During a single call to RunQuery the callback
	 * may be called multiple times. If the query handle is invalid or has been deleted nothing happens and the callback won't be called.
	 */
	virtual FQueryResult RunQuery(QueryHandle Query, DirectQueryCallbackRef Callback) = 0;
	/**
	 * Directly runs a query. The callback will be called for batches of matching rows. During a single call to RunQuery the callback
	 * may be called multiple times. If the query handle is invalid or has been deleted nothing happens and the callback won't be called.
	 */
	virtual FQueryResult RunQuery(QueryHandle Query, EDirectQueryExecutionFlags Flags, DirectQueryCallbackRef Callback) = 0;
	/**
	 * Directly runs a query. The callback will be called for batches of matching rows. During a single call to RunQuery the callback
	 * may be called multiple times. If the query handle is invalid or has been deleted nothing happens and the callback won't be called.
	 */
	virtual FQueryResult RunQuery(QueryHandle Query, ERunQueryFlags Flags, const Queries::TQueryFunction<void>& Callback) = 0;
	/**
	 * Directly runs a query. The callback will be called for batches of matching rows. During a single call to RunQuery the callback
	 * may be called multiple times. If the query handle is invalid or has been deleted nothing happens and the callback won't be called.
	 */
	template<Queries::FunctionType Function>
	FQueryResult RunQuery(QueryHandle Query, ERunQueryFlags Flags, Function&& Callback);
	/**
	 * Directly runs a query. The callback will be called for batches of matching rows. During a single call to RunQuery the callback
	 * may be called multiple times. If the query handle is invalid or has been deleted nothing happens and the callback won't be called.
	 */
	template<typename ResultType, Queries::FunctionType Function>
	FQueryResult RunQuery(QueryHandle Query, ERunQueryFlags Flags, Queries::TResult<ResultType>& Result, Function&& Callback);
	
	/**
	 * Triggers all queries registered under the activation name to run for one update cycle. The activatable queries will be activated at
	 * start of the cycle and disabled at the end of the cycle and act like regular queries for that cycle. This includes not running
	 * if there are no columns to match against.
	 */
	virtual void ActivateQueries(FName ActivationName) = 0;

	/**
	 * @section Mapping
	 * @description
	 * In order for rows to reference each other it's often needed to find a row based on the content of one of its columns. This can be
	 * done by linearly searching through columns, though this comes at a performance cost. As an alternative the data storage allows
	 * one or more key to be created for a row for fast retrieval.
	 */

	/**
	 * Retrieves the row for a mapped object. Returns an invalid row handle if the no row with the provided key was found in the provided 
	 * domain. 
	 */
	virtual RowHandle LookupMappedRow(const FName& Domain, const FMapKeyView& Key) const = 0;
	/**
	 * Registers a row under a key in the provided domain. The same row can be registered multiple, but an key can only be associated with
	 * a single row.
	 */
	virtual void MapRow(const FName& Domain, FMapKey Key, RowHandle Row) = 0;
	/**
	 * Register multiple rows under their key in the provided domain. The same row can be registered multiple times, but the key can only
	 * be associated with a single row in the domain. During processing the keys will be moved out into the final location.
	 */
	virtual void BatchMapRows(const FName& Domain, TArrayView<TPair<FMapKey, RowHandle>> MapRowPairs) = 0;
	/** Updates the key of a row in the provided domain to a new value. Effectively this is the same as removing a key and adding a new one. */
	virtual void RemapRow(const FName& Domain, const FMapKeyView& OriginalKey, FMapKey NewKey) = 0;
	/** Removes a previously registered key in the provided domain from the mapping table or does nothing if the key no longer exists. */
	virtual void RemoveRowMapping(const FName& Domain, const FMapKeyView& Key) = 0;
	
	/**
	 * @section Tick
	 * Includes callbacks to respond to various steps during TEDS' tick.
	 */
	
	/**
	 * Called periodically when the storage is available. This provides an opportunity to do any repeated processing
	 * for the data storage.
	 */
	virtual FTypedElementOnDataStorageUpdate& OnUpdate() = 0;
	/**
	 * Called periodically when the storage is available. This provides an opportunity clean up after processing and
	 * to get ready for the next batch up updates.
	 */
	virtual FTypedElementOnDataStorageUpdate& OnUpdateCompleted() = 0;

	using FOnCooperativeUpdate = TFunction<void(FTimespan TimeAllowance)>;
	/** The higher the priority, the more frequently tasks get called. See RegisterCooperativeUpdate. */
	enum class ECooperativeTaskPriority : uint8
	{
		High,
		Medium,
		Low
	};
	/**
	 * Each tick TEDS will use the remaining time in a frame to execute task in the cooperative queue. A time slicer uses a 
	 * cooperative threading model so once a task has been started it will not be interrupted and it's up to the registered 
	 * function to try to stay within the provided time window.
	 * Tasks with a higher priority will on average be run more frequently, but other priority tasks will be given a chance to
	 * do work so guarantees are given that lower priority aren't starved out by higher priority tasks.
	 */
	virtual void RegisterCooperativeUpdate(const FName& TaskName, ECooperativeTaskPriority Priority, FOnCooperativeUpdate Callback) = 0;
	/** Removes a previously registered time sliced callback. */
	virtual void UnregisterCooperativeUpdate(const FName& TaskName) = 0;

	/**
	 * @section Miscellaneous
	 */
	
	 /**
	 * Whether or not the data storage is available. The data storage is available most of the time, but can be
	 * unavailable for a brief time between being destroyed and a new one created.
	 */
	virtual bool IsAvailable() const = 0;

	/** Returns a pointer to the registered external system if found, otherwise null. */
	virtual void* GetExternalSystemAddress(UClass* Target) = 0;
	/** Returns a pointer to the registered external system if found, otherwise null. */
	template<typename SystemType>
	SystemType* GetExternalSystem();

	/** Check if a custom extension is supported. This can be used to check for in-development features, custom extensions, etc. */
	virtual bool SupportsExtension(FName Extension) const = 0;
	/** Provides a list of all extensions that are enabled. */
	virtual void ListExtensions(TFunctionRef<void(FName)> Callback) const = 0;


	/**
	 * @section Deprecated
	 */
	UE_DEPRECATED(5.6, "Use 'LookUpMappedRow' instead and use the new FMapKey(View) instead of the explicit 'GenerateIndexHash'-functions.")
	inline RowHandle FindIndexedRow(IndexHash Index) const;
	UE_DEPRECATED(5.6, "Use 'MapRow' instead and use the new FMapKey(View) instead of the explicit 'GenerateIndexHash'-functions.")
	inline void IndexRow(IndexHash Index, RowHandle Row);
	UE_DEPRECATED(5.6, "Use 'BatchMapRows' instead and use the new FMapKey(View) instead of the explicit 'GenerateIndexHash'-functions.")
	inline void BatchIndexRows(TConstArrayView<TPair<IndexHash, RowHandle>> IndexRowPairs);
	UE_DEPRECATED(5.6, "Use 'RemapRow' instead and use the new FMapKey(View) instead of the explicit 'GenerateIndexHash'-functions.")
	inline void Reindex(IndexHash OriginalIndex, IndexHash NewIndex);
	UE_DEPRECATED(5.6, "Use 'RemovedRowMapping' instead and use the new FMapKey(View) instead of the explicit 'GenerateIndexHash'-functions.")
	inline void RemoveIndex(IndexHash Index);
	UE_DEPRECATED(5.6, "Use 'RemapRow' instead and use the new FMapKey(View) instead of the explicit 'GenerateIndexHash'-functions. The Row argument is no longer used.")
	inline void ReindexRow(IndexHash OriginalIndex, IndexHash NewIndex, RowHandle Row);

	UE_DEPRECATED(5.7, "Use the version of 'LookupMappedRow' that uses a domain.")
	inline RowHandle LookupMappedRow(const FMapKeyView& Key) const;
	UE_DEPRECATED(5.7, "Use the version of 'MapRow' that uses a domain.")
	inline void MapRow(FMapKey Key, RowHandle Row);
	UE_DEPRECATED(5.7, "Use the version of 'BatchMapRows' that uses a domain.")
	inline void BatchMapRows(TArrayView<TPair<FMapKey, RowHandle>> MapRowPairs);
	UE_DEPRECATED(5.7, "Use the version of 'RemapRow' that uses a domain.")
	inline void RemapRow(const FMapKeyView& OriginalKey, FMapKey NewKey);
	UE_DEPRECATED(5.7, "Use the version of 'RemoveRowMapping' that uses a domain.")
	inline void RemoveRowMapping(const FMapKeyView& Key);
};

// Implementations

template <typename FactoryT>
const FactoryT* ICoreProvider::FindFactory() const
{
	return static_cast<const FactoryT*>(FindFactory(FactoryT::StaticClass()));
}

template <typename FactoryT>
FactoryT* ICoreProvider::FindFactory()
{
	return static_cast<FactoryT*>(FindFactory(FactoryT::StaticClass()));
}

template<TColumnType... Columns>
TableHandle ICoreProvider::RegisterTable(const FName& Name)
{
	return RegisterTable({ Columns::StaticStruct()... }, Name);
}

template<TColumnType... Columns>
TableHandle ICoreProvider::RegisterTable(
	TableHandle SourceTable, const FName& Name)
{
	return RegisterTable(SourceTable, { Columns::StaticStruct()... }, Name);
}

template<typename FilterFunction>
void ICoreProvider::FilterRowsBy(FRowHandleArray& Result, FRowHandleArrayView Input, EFilterOptions Options, FilterFunction&& Filter)
{
	FilterRowsBy(Result, Input, Options, Queries::BuildQueryFunction<bool>(Forward<FilterFunction>(Filter)));
}

template<TColumnType Column>
void ICoreProvider::AddColumn(RowHandle Row)
{
	AddColumn(Row, Column::StaticStruct());
}

template<TColumnType Column>
void ICoreProvider::RemoveColumn(RowHandle Row)
{
	RemoveColumn(Row, Column::StaticStruct());
}

template<TColumnType... Columns>
void ICoreProvider::AddColumns(RowHandle Row)
{
	AddColumns(Row, { Columns::StaticStruct()...});
}

template <>
inline void ICoreProvider::AddColumn<FValueTag>(RowHandle Row, const FName& Tag, const FName& Value)
{
	AddColumn(Row, FValueTag(Tag), Value);
}

template <>
inline void ICoreProvider::RemoveColumn<FValueTag>(RowHandle Row, const FName& Tag)
{
	using namespace UE::Editor::DataStorage;
	RemoveColumn(Row, FValueTag(Tag));
}

template<TDynamicColumnTemplate DynamicColumnTemplate>
void ICoreProvider::RemoveColumn(RowHandle Row, const FName& Identifier)
{
	const FDynamicColumnDescription Description
	{
		.TemplateType = DynamicColumnTemplate::StaticStruct(),
		.Identifier = Identifier
	};
	const UScriptStruct* StructInfo = FindDynamicColumn(Description);
	RemoveColumn(Row, StructInfo);
}

template<TEnumType EnumT>
void ICoreProvider::AddColumn(RowHandle Row, EnumT Value)
{
	const UEnum* Enum = StaticEnum<EnumT>();
	const FName ValueAsFName = *Enum->GetNameStringByValue(static_cast<int64>(Value));
	if (ValueAsFName != NAME_None)
	{
		AddColumn(Row, FValueTag(Enum->GetFName()), ValueAsFName);
	}
}

template<TColumnType... Columns>
void ICoreProvider::RemoveAllRowsWith()
{
	RemoveAllRowsWithColumns({ Columns::StaticStruct()... });
}

template<auto Value, TEnumType EnumT>
void ICoreProvider::AddColumn(RowHandle Row)
{
	AddColumn<EnumT>(Row, Value);
}

template <TDynamicColumnTemplate DynamicColumnTemplate>
void ICoreProvider::AddColumn(RowHandle Row, const FName& Identifier)
{
	const FDynamicColumnDescription Description
	{
		.TemplateType = DynamicColumnTemplate::StaticStruct(),
		.Identifier = Identifier
	};
	const UScriptStruct* StructInfo = GenerateDynamicColumn(Description);
	AddColumn(Row, StructInfo);
}

template <TDynamicColumnTemplate DynamicColumnTemplate>
void ICoreProvider::AddColumn(RowHandle Row, const FName& Identifier, DynamicColumnTemplate&& TemplateInstance)
{
	const FDynamicColumnDescription Description
	{
		.TemplateType = DynamicColumnTemplate::StaticStruct(),
		.Identifier = Identifier
	};
	const UScriptStruct* StructInfo = GenerateDynamicColumn(Description);
	AddColumnData(Row, StructInfo,
		[&TemplateInstance](void* ColumnData, const UScriptStruct&)
		{
			if constexpr (std::is_move_constructible_v<DynamicColumnTemplate>)
			{
				new(ColumnData) DynamicColumnTemplate(MoveTemp(TemplateInstance));
			}
			else
			{
				new(ColumnData) DynamicColumnTemplate(TemplateInstance);
			}
		},
		[](const UScriptStruct&, void* Destination, void* Source)
		{
			if constexpr (std::is_move_assignable_v<DynamicColumnTemplate>)
			{
				*static_cast<DynamicColumnTemplate*>(Destination) = MoveTemp(*static_cast<DynamicColumnTemplate*>(Source));
			}
			else
			{
				*static_cast<DynamicColumnTemplate*>(Destination) = *static_cast<DynamicColumnTemplate*>(Source);
			}
		});
	
}

template<TEnumType EnumT>
void ICoreProvider::RemoveColumn(RowHandle Row)
{
	const UEnum* Enum = StaticEnum<EnumT>();
	RemoveColumn(Row, FValueTag(Enum->GetFName()));
}

template<TColumnType... Columns>
void ICoreProvider::RemoveColumns(RowHandle Row)
{
	RemoveColumns(Row, { Columns::StaticStruct()...});
}

template<TDataColumnType ColumnType>
void ICoreProvider::AddColumn(RowHandle Row, ColumnType&& Column)
{
	AddColumnData(Row, ColumnType::StaticStruct(),
		[&Column](void* ColumnData, const UScriptStruct&)
		{
			if constexpr (std::is_move_constructible_v<ColumnType>)
			{
				new(ColumnData) ColumnType(MoveTemp(Column));
			}
			else
			{
				new(ColumnData) ColumnType(Column);
			}
		},
		[](const UScriptStruct&, void* Destination, void* Source)
		{
			if constexpr (std::is_move_assignable_v<ColumnType>)
			{
				*reinterpret_cast<ColumnType*>(Destination) = MoveTemp(*reinterpret_cast<ColumnType*>(Source));
			}
			else
			{
				*reinterpret_cast<ColumnType*>(Destination) = *reinterpret_cast<ColumnType*>(Source);
			}
		});
}

template<TDataColumnType ColumnType>
ColumnType* ICoreProvider::GetColumn(RowHandle Row)
{
	return reinterpret_cast<ColumnType*>(GetColumnData(Row, ColumnType::StaticStruct()));
}

template<TDataColumnType ColumnType>
const ColumnType* ICoreProvider::GetColumn(RowHandle Row) const
{
	return reinterpret_cast<const ColumnType*>(GetColumnData(Row, ColumnType::StaticStruct()));
}

template <TDynamicColumnTemplate DynamicColumnTemplate>
DynamicColumnTemplate* ICoreProvider::GetColumn(RowHandle Row, const FName& Identifier)
{
	const FDynamicColumnDescription Description
	{
		.TemplateType = DynamicColumnTemplate::StaticStruct(),
		.Identifier = Identifier
	};
	const UScriptStruct* StructInfo = GenerateDynamicColumn(Description);
	if (StructInfo)
	{
		return static_cast<DynamicColumnTemplate*>(GetColumnData(Row, StructInfo));
	}
	return nullptr;
}

template <TDynamicColumnTemplate DynamicColumnTemplate>
const DynamicColumnTemplate* ICoreProvider::GetColumn(RowHandle Row, const FName& Identifier) const
{
	const FDynamicColumnDescription Description
	{
		.TemplateType = DynamicColumnTemplate::StaticStruct(),
		.Identifier = Identifier
	};
	const UScriptStruct* StructInfo = FindDynamicColumn(Description);
	if (StructInfo)
	{
		return static_cast<const DynamicColumnTemplate*>(GetColumnData(Row, StructInfo));
	}
	return nullptr;
}

template<TColumnType... ColumnType>
bool ICoreProvider::HasColumns(RowHandle Row) const
{
	return HasColumns(Row, TConstArrayView<const UScriptStruct*>({ ColumnType::StaticStruct()... }));
}

template<typename SystemType>
SystemType* ICoreProvider::GetExternalSystem()
{
	return reinterpret_cast<SystemType*>(GetExternalSystemAddress(SystemType::StaticClass()));
}

void ICoreProvider::ReindexRow(
	IndexHash OriginalIndex, IndexHash NewIndex, RowHandle Row)
{
	RemapRow(NAME_None, FMapKeyView(OriginalIndex), FMapKey(NewIndex));
}

RowHandle ICoreProvider::FindIndexedRow(IndexHash Index) const
{
	return LookupMappedRow(NAME_None, FMapKeyView(Index));
}

void ICoreProvider::IndexRow(IndexHash Index, RowHandle Row)
{
	MapRow(NAME_None, FMapKey(Index), Row);
}

void ICoreProvider::BatchIndexRows(TConstArrayView<TPair<IndexHash, RowHandle>> IndexRowPairs)
{
	for (const TPair<IndexHash, RowHandle>& Pair : IndexRowPairs)
	{
		MapRow(NAME_None, FMapKey(Pair.Key), Pair.Value);
	}
}

void ICoreProvider::Reindex(IndexHash OriginalIndex, IndexHash NewIndex)
{
	RemapRow(NAME_None, FMapKeyView(OriginalIndex), FMapKey(NewIndex));
}

void ICoreProvider::RemoveIndex(IndexHash Index)
{
	RemoveRowMapping(NAME_None, FMapKeyView(Index));
}

RowHandle ICoreProvider::LookupMappedRow(const FMapKeyView& Key) const
{
	return LookupMappedRow(NAME_None, Key);
}

void ICoreProvider::MapRow(FMapKey Key, RowHandle Row)
{
	MapRow(NAME_None, Key, Row);
}

void ICoreProvider::BatchMapRows(TArrayView<TPair<FMapKey, RowHandle>> MapRowPairs)
{
	BatchMapRows(NAME_None, MapRowPairs);
}

void ICoreProvider::RemapRow(const FMapKeyView& OriginalKey, FMapKey NewKey)
{
	RemapRow(NAME_None, OriginalKey, NewKey);
}

void ICoreProvider::RemoveRowMapping(const FMapKeyView& Key)
{
	RemoveRowMapping(NAME_None, Key);
}

template<TDynamicColumnTemplate DynamicColumnTemplateType>
void ICoreProvider::ForEachDynamicColumn(TFunctionRef<void(const UScriptStruct& Type)> Callback) const
{
	ForEachDynamicColumn(DynamicColumnTemplateType::StaticStruct(), Callback);
}

template<Queries::FunctionType Function>
FQueryResult ICoreProvider::RunQuery(QueryHandle Query, ERunQueryFlags Flags, Function&& Callback)
{
	return RunQuery(Query, Flags, Queries::BuildQueryFunction<void>(Forward<Function>(Callback)));
}

template<typename ResultType, Queries::FunctionType Function>
FQueryResult ICoreProvider::RunQuery(QueryHandle Query, ERunQueryFlags Flags, Queries::TResult<ResultType>& Result, Function&& Callback)
{
	return RunQuery(Query, Flags, Queries::BuildQueryFunction(Result, Forward<Function>(Callback)));
}

} // namespace UE::Editor::DataStorage
