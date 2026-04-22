// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_TESTS

#include "Elements/Framework/TypedElementQueryContext.h"
#include "Elements/Framework/TypedElementQueryContextMock.h"
#include "Elements/Framework/TypedElementQueryFunctions.h"
#include "Elements/Framework/TypedElementTestColumns.h"
#include "Tests/TestHarnessAdapter.h"

namespace UE::Editor::DataStorage::Queries::Tests
{
	struct FBoolAndResultAccumulator : TResult<bool>
	{
		bool Value = true;

		virtual void Add(bool ResultValue) override
		{
			Value = Value && ResultValue;
		}
	};

	struct FBoolOrResultAccumulator : TResult<bool>
	{
		bool Value = false;

		virtual void Add(bool ResultValue) override
		{
			Value = Value || ResultValue;
		}
	};

	struct FInt32ResultAccumulator : TResult<int32>
	{
		int32 Value = 0;

		virtual void Add(int32 ResultValue) override
		{
			Value += ResultValue;
		}
	};
	
	struct FTestQueryFunctionResponse : IQueryFunctionResponse
	{
		virtual ~FTestQueryFunctionResponse() override = default;

		virtual bool NextBatch() override
		{
			CHECK_MESSAGE(TEXT("Row count was not set in test."), RowCount > 0);
			return false;
		}

		virtual bool NextRow() override
		{
			CHECK_MESSAGE(TEXT("Row count was not set in test."), RowCount > 0);
			CurrentRow++;
			return CurrentRow < RowCount;
		}

		virtual void GetConstColumns(TArrayView<const void*> ColumnsData, TConstArrayView<const UScriptStruct*> ColumnTypes) override
		{
			CHECK_MESSAGE(TEXT("Row count was not set in test."), RowCount > 0);
			for (int32 Index = 0, Num = ColumnTypes.Num(); Index < Num; ++Index)
			{
				const void** Data = ConstColumns.Find(ColumnTypes[Index]);
				ColumnsData[Index] = Data ? (static_cast<const char*>(*Data) + ColumnTypes[Index]->GetStructureSize() * CurrentRow) : nullptr;
			}
		}

		virtual void GetMutableColumns(TArrayView<void*> ColumnsData, TConstArrayView<const UScriptStruct*> ColumnTypes) override
		{
			CHECK_MESSAGE(TEXT("Row count was not set in test."), RowCount > 0);
			for (int32 Index = 0, Num = ColumnTypes.Num(); Index < Num; ++Index)
			{
				void** Data = MutableColumns.Find(ColumnTypes[Index]);
				ColumnsData[Index] = Data ? (static_cast<char*>(*Data) + ColumnTypes[Index]->GetStructureSize() * CurrentRow) : nullptr;
			}
		}

		void SetRowCount(int32 InRowCount)
		{
			CHECK_MESSAGE(TEXT("Row count was not set in test."), InRowCount > 0);
			RowCount = InRowCount;
		}

		template<TColumnType... ColumnType>
		void SetConstColumns(const ColumnType*... Columns)
		{
			(ConstColumns.Add(ColumnType::StaticStruct(), Columns), ...);
		}

		template<TColumnType... ColumnType>
		void SetMutableColumns(ColumnType*... Columns)
		{
			(MutableColumns.Add(ColumnType::StaticStruct(), Columns), ...);
		}

		TMap<const UScriptStruct*, const void*> ConstColumns;
		TMap<const UScriptStruct*, void*> MutableColumns;
		int32 CurrentRow = 0;
		int32 RowCount = 0;
	};

	TEST_CASE_NAMED(TQueryFunction_Tests, "Editor::DataStorage::Queries::Query Function (TQueryFunction)", "[ApplicationContextMask][EngineFilter]")
	{
		using namespace UE::Editor::DataStorage;
		using namespace UE::Editor::DataStorage::Queries;
		
		SECTION("Empty function (void)")
		{
			TQueryFunction<void> Result = BuildQueryFunction<void>([](){});
			
			CHECK_MESSAGE(TEXT("Empty query still has capabilities."), Result.Capabilities.IsEmpty());
			CHECK_MESSAGE(TEXT("Empty query still has const columns."), Result.ConstColumnTypes.IsEmpty());
			CHECK_MESSAGE(TEXT("Empty query still has mutable columns."), Result.MutableColumnTypes.IsEmpty());
		}

		SECTION("Empty function (bool)")
		{
			TQueryFunction<bool> Result = BuildQueryFunction<bool>([]() { return false; });

			CHECK_MESSAGE(TEXT("Empty query still has capabilities."), Result.Capabilities.IsEmpty());
			CHECK_MESSAGE(TEXT("Empty query still has const columns."), Result.ConstColumnTypes.IsEmpty());
			CHECK_MESSAGE(TEXT("Empty query still has mutable columns."), Result.MutableColumnTypes.IsEmpty());
		}

		SECTION("Empty function with capture")
		{
			bool bResult = false;
			TQueryFunction<bool> Result = BuildQueryFunction<bool>([&bResult]() { return bResult; });

			CHECK_MESSAGE(TEXT("Empty query still has capabilities."), Result.Capabilities.IsEmpty());
			CHECK_MESSAGE(TEXT("Empty query still has const columns."), Result.ConstColumnTypes.IsEmpty());
			CHECK_MESSAGE(TEXT("Empty query still has mutable columns."), Result.MutableColumnTypes.IsEmpty());
		}

		SECTION("Function with context")
		{
			TQueryFunction<void> Result = BuildQueryFunction<void>([](TQueryContext<SingleRowInfo> Context) {});

			CHECK_MESSAGE(TEXT("Expected exactly one capability."), Result.Capabilities.Num() == 1);
			CHECK_MESSAGE(TEXT("Query with only context shouldn't have const columns."), Result.ConstColumnTypes.IsEmpty());
			CHECK_MESSAGE(TEXT("Query with only context shouldn't have mutable columns."), Result.MutableColumnTypes.IsEmpty());
		}

		SECTION("Function with columns")
		{
			TQueryFunction<void> Result = BuildQueryFunction<void>(
				[](FTestColumnInt& ColumnA, const FTestColumnString& ColumnB)
				{
				});

			CHECK_MESSAGE(TEXT("Query with only columns shouldn't have capabilities."), Result.Capabilities.IsEmpty());
			CHECK_MESSAGE(TEXT("Expected exactly one const column."), Result.ConstColumnTypes.Num() == 1);
			CHECK_MESSAGE(TEXT("Expected exactly one mutable column."), Result.MutableColumnTypes.Num() == 1);
		}

		SECTION("Function with context and columns")
		{
			TQueryFunction<void> Result = BuildQueryFunction<void>(
				[](TQueryContext<SingleRowInfo> Context, FTestColumnInt& ColumnA, const FTestColumnString& ColumnB)
				{
				});

			CHECK_MESSAGE(TEXT("Expected exactly one capability."), Result.Capabilities.Num() == 1);
			CHECK_MESSAGE(TEXT("Expected exactly one const column."), Result.ConstColumnTypes.Num() == 1);
			CHECK_MESSAGE(TEXT("Expected exactly one mutable column."), Result.MutableColumnTypes.Num() == 1);

			CHECK_MESSAGE(TEXT("Incorrect const column set."), Result.ConstColumnTypes[0] == FTestColumnString::StaticStruct());
			CHECK_MESSAGE(TEXT("Incorrect mutable column set."), Result.MutableColumnTypes[0] == FTestColumnInt::StaticStruct());
		}

		SECTION("Function with context and columns at random location")
		{
			TQueryFunction<void> Result = BuildQueryFunction<void>(
				[](FTestColumnInt& ColumnA, TQueryContext<SingleRowInfo> Context, const FTestColumnString& ColumnB)
				{
				});

			CHECK_MESSAGE(TEXT("Expected exactly one capability."), Result.Capabilities.Num() == 1);
			CHECK_MESSAGE(TEXT("Expected exactly one const column."), Result.ConstColumnTypes.Num() == 1);
			CHECK_MESSAGE(TEXT("Expected exactly one mutable column."), Result.MutableColumnTypes.Num() == 1);

			CHECK_MESSAGE(TEXT("Incorrect const column set."), Result.ConstColumnTypes[0] == FTestColumnString::StaticStruct());
			CHECK_MESSAGE(TEXT("Incorrect mutable column set."), Result.MutableColumnTypes[0] == FTestColumnInt::StaticStruct());
		}

		SECTION("Function with context and column batch")
		{
			TQueryFunction<void> Result = BuildQueryFunction<void>(
				[](TQueryContext<RowBatchInfo> Context, TBatch<FTestColumnInt> ColumnsA, TConstBatch<FTestColumnString> ColumnB)
				{
				});

			CHECK_MESSAGE(TEXT("Expected exactly one capability."), Result.Capabilities.Num() == 1);
			CHECK_MESSAGE(TEXT("Expected exactly one const column."), Result.ConstColumnTypes.Num() == 1);
			CHECK_MESSAGE(TEXT("Expected exactly one mutable column."), Result.MutableColumnTypes.Num() == 1);

			CHECK_MESSAGE(TEXT("Incorrect const column set."), Result.ConstColumnTypes[0] == FTestColumnString::StaticStruct());
			CHECK_MESSAGE(TEXT("Incorrect mutable column set."), Result.MutableColumnTypes[0] == FTestColumnInt::StaticStruct());
		}

		SECTION("Check context compatibility without arguments")
		{
			TQueryFunction<void> Function = BuildQueryFunction<void>([](){});

			QueryContextMock Context;
			bool bResult = Context.CheckCompatiblity(Function);
			CHECK_MESSAGE(TEXT("Function without context incorrectly found to not be matching."), bResult);
		}

		SECTION("Check context compatibility with context")
		{
			TQueryFunction<void> Function = BuildQueryFunction<void>([](TQueryContext<SingleRowInfo> Context) {});

			QueryContextMock Context;
			bool bResult = Context.CheckCompatiblity(Function);
			CHECK_MESSAGE(TEXT("Function with context incorrectly found to not be matching."), bResult);
		}

		SECTION("Check context compatibility with restricted context")
		{
			TQueryFunction<void> Function = BuildQueryFunction<void>([](TQueryContext<SingleRowInfo> Context) {});

			TQueryContextImpl<FQueryContextMock, SingleRowInfo> Context;
			bool bResult = Context.CheckCompatiblity(Function);
			CHECK_MESSAGE(TEXT("Function with context incorrectly found to not be matching."), bResult);
		}

		SECTION("Call without arguments")
		{
			TQueryFunction<bool> Function = BuildQueryFunction<bool>(
				[]()
				{
					return true;
				});
			
			QueryContextMock Context;
			FTestQueryFunctionResponse Response;
			Response.SetRowCount(1);
			
			FBoolOrResultAccumulator Result;
			Function.Call<EFunctionCallConfig::VerifyColumns>(Result, Context, Response);
			CHECK_MESSAGE(TEXT("Query function wasn't called."), Result.Value);
		}

		SECTION("Call setting column value")
		{
			TQueryFunction<void> Function = BuildQueryFunction<void>(
				[](FTestColumnString& Column)
				{
					Column.TestString = TEXT("Callback");
				});

			QueryContextMock Context;
			FTestColumnString Column;
			
			FTestQueryFunctionResponse Response;
			Response.SetRowCount(1);
			Response.SetMutableColumns(&Column);
			Function.Call<EFunctionCallConfig::VerifyColumns>(Context, Response);
			
			CHECK_MESSAGE(TEXT("Query function wasn't called."), Column.TestString == TEXT("Callback"));
		}

		SECTION("Call setting const and mutable columns")
		{
			TQueryFunction<void> Function = BuildQueryFunction<void>(
				[](FTestColumnString& ColumnA, const FTestColumnInt& ColumnB)
				{
					ColumnA.TestString = FString::Printf(TEXT("Test: %i"), ColumnB.TestInt);
				});

			QueryContextMock Context;
			FTestColumnString ColumnA;
			FTestColumnInt ColumnB;
			ColumnB.TestInt = 42;

			FTestQueryFunctionResponse Response;
			Response.SetRowCount(1);
			Response.SetMutableColumns(&ColumnA);
			Response.SetConstColumns(&ColumnB);
			Function.Call<EFunctionCallConfig::VerifyColumns>(Context, Response);

			CHECK_MESSAGE(TEXT("Query function wasn't called."), ColumnA.TestString == TEXT("Test: 42"));
		}

		SECTION("Call setting const and mutable batch columns")
		{
			TQueryFunction<void> Function = BuildQueryFunction<void>(
				[](TQueryContext<RowBatchInfo> Context, TBatch<FTestColumnString> ColumnsA, TConstBatch<FTestColumnInt> ColumnsB)
				{
					Context.ForEachRow([](FTestColumnString& ColumnA, const FTestColumnInt& ColumnB)
						{
							ColumnA.TestString = FString::Printf(TEXT("Test: %i"), ColumnB.TestInt);
						}, ColumnsA, ColumnsB);
				});

			static constexpr uint32 RowCount = 2;

			QueryContextMock Context;
			Context.GetContextImplementation().RowBatchInfo_GetBatchRowCount_Mock = []() { return RowCount; };
			FTestColumnString ColumnsA[RowCount];
			FTestColumnInt ColumnsB[] =
			{
				FTestColumnInt{ .TestInt = 42 },
				FTestColumnInt{ .TestInt = 88 }
			};

			FTestQueryFunctionResponse Response;
			Response.SetRowCount(RowCount);
			Response.SetMutableColumns(ColumnsA);
			Response.SetConstColumns(ColumnsB);
			Function.Call<EFunctionCallConfig::VerifyColumns>(Context, Response);

			CHECK_MESSAGE(TEXT("Query function wasn't called."), ColumnsA[0].TestString == TEXT("Test: 42"));
			CHECK_MESSAGE(TEXT("Query function wasn't called."), ColumnsA[1].TestString == TEXT("Test: 88"));
		}

		SECTION("Call and use context")
		{
			TQueryFunction<void> Function = BuildQueryFunction<void>(
				[](TQueryContext<SingleRowInfo> Context)
				{
					Context.GetCurrentRow();
					Context.CurrentRowHasColumns<FTestColumnString>();
				});

			QueryContextMock Context;
			
			bool bGetCurrentRowCalled = false;
			Context.GetContextImplementation().SingleRowInfo_GetCurrentRow_Mock =
				[&bGetCurrentRowCalled]()
				{
					bGetCurrentRowCalled = true;
					return 0;
				};
			
			bool bCurrentRowHasColumnsCalled = false;
			Context.GetContextImplementation().SingleRowInfo_CurrentRowHasColumns_Mock =
				[&bCurrentRowHasColumnsCalled](TConstArrayView<const UScriptStruct*>)
				{
					bCurrentRowHasColumnsCalled = true;
					return false;
				};

			FTestQueryFunctionResponse Response;
			Response.SetRowCount(1);
			Function.Call(Context, Response);

			CHECK_MESSAGE(TEXT("GetCurrentRow not called on context."), bGetCurrentRowCalled);
			CHECK_MESSAGE(TEXT("CurrentRowHasColumns not called on context."), bCurrentRowHasColumnsCalled);
		}

		SECTION("Call and use restricted context")
		{
			TQueryFunction<void> Function = BuildQueryFunction<void>(
				[](TQueryContext<SingleRowInfo> Context)
				{
					Context.GetCurrentRow();
					Context.CurrentRowHasColumns<FTestColumnString>();
				});

			TQueryContextImpl<FQueryContextMock, SingleRowInfo> Context;

			bool bGetCurrentRowCalled = false;
			Context.GetContextImplementation().SingleRowInfo_GetCurrentRow_Mock =
				[&bGetCurrentRowCalled]()
				{
					bGetCurrentRowCalled = true;
					return 0;
				};

			bool bCurrentRowHasColumnsCalled = false;
			Context.GetContextImplementation().SingleRowInfo_CurrentRowHasColumns_Mock =
				[&bCurrentRowHasColumnsCalled](TConstArrayView<const UScriptStruct*>)
				{
					bCurrentRowHasColumnsCalled = true;
					return false;
				};

			FTestQueryFunctionResponse Response;
			Response.SetRowCount(1);
			Function.Call(Context, Response);

			CHECK_MESSAGE(TEXT("GetCurrentRow not called on context."), bGetCurrentRowCalled);
			CHECK_MESSAGE(TEXT("CurrentRowHasColumns not called on context."), bCurrentRowHasColumnsCalled);
		}

		SECTION("Call with result from callback")
		{
			TQueryFunction<bool> Function = BuildQueryFunction<bool>([]() { return true; });

			QueryContextMock Context;
			FTestColumnString Column;

			FBoolOrResultAccumulator Result;

			FTestQueryFunctionResponse Response;
			Response.SetRowCount(1);
			Response.SetMutableColumns(&Column);
			Function.Call<EFunctionCallConfig::VerifyColumns>(Result, Context, Response);

			CHECK_MESSAGE(TEXT("Result wasn't passed back in."), Result.Value);
		}

		SECTION("Call with result through TResult")
		{
			TQueryFunction<bool> Function = BuildQueryFunction<bool>(
				[](TResult<bool>& Result)
				{ 
					Result.Add(true);
				});

			QueryContextMock Context;
			FTestColumnString Column;

			FBoolOrResultAccumulator Result;

			FTestQueryFunctionResponse Response;
			Response.SetRowCount(1);
			Response.SetMutableColumns(&Column);
			Function.Call<EFunctionCallConfig::VerifyColumns>(Result, Context, Response);

			CHECK_MESSAGE(TEXT("Result wasn't passed back in."), Result.Value);
		}

		SECTION("Call with batching results.")
		{
			TQueryFunction<int32> Function = BuildQueryFunction<int32>(
				[](TQueryContext<RowBatchInfo> Context, TResult<int32>& Result, TConstBatch<FTestColumnInt> Columns)
				{
					Context.ForEachRow([&Result](const FTestColumnInt& Column)
						{
							Result.Add(Column.TestInt);
						}, Columns);
				});

			static constexpr int32 RowCount = 4;

			QueryContextMock Context;
			Context.GetContextImplementation().RowBatchInfo_GetBatchRowCount_Mock = []() { return RowCount; };

			FTestColumnInt Columns[] =
			{
				FTestColumnInt{.TestInt = 1 },
				FTestColumnInt{.TestInt = 2 },
				FTestColumnInt{.TestInt = 4 },
				FTestColumnInt{.TestInt = 8 }
			};

			FInt32ResultAccumulator Result;

			FTestQueryFunctionResponse Response;
			Response.SetRowCount(RowCount);
			Response.SetConstColumns(Columns);
			Function.Call<EFunctionCallConfig::VerifyColumns>(Result, Context, Response);

			CHECK_MESSAGE(TEXT("Result wasn't passed back in."), Result.Value == 1 + 2 + 4 + 8);
		}

		SECTION("Call and break on first row")
		{
			int32 LastValue = 0;
			TQueryFunction<void> Function = BuildQueryFunction<void>(
				[&LastValue](EFlowControl& Flow, const FTestColumnInt& Column)
				{
					LastValue = Column.TestInt;
					Flow = EFlowControl::Break;
				});

			QueryContextMock Context;

			FTestColumnInt Columns[] =
			{
				FTestColumnInt{.TestInt = 1 },
				FTestColumnInt{.TestInt = 2 }
			};

			FTestQueryFunctionResponse Response;
			Response.SetRowCount(2);
			Response.SetConstColumns(Columns);

			Function.Call(Context, Response);

			CHECK_MESSAGE(TEXT("Iteration didn't stop on the first row."), LastValue == 1);
		}
	}
} // namespace UE::Editor::DataStorage::Queries::Tests

#endif // WITH_TEST