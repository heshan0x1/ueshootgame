// Copyright Epic Games, Inc. All Rights Reserved.

#include <functional>
#include <type_traits>
#include "DataStorage/CommonTypes.h"
#include "DataStorage/Handles.h"
#include "Elements/Framework/TypedElementQueryCapabilities.h"
#include "Elements/Framework/TypedElementQueryCapabilityForwarder.h"
#include "Elements/Framework/TypedElementQueryContract.h"
#include "Elements/Framework/TypedElementQueryFunctionArguments.h"

namespace UE::Editor::DataStorage::Queries
{
	namespace Private
	{
		template<typename T> concept FunctorType = requires{ &T::operator(); };
		
		template<typename T>
		struct TFunctionInfoImpl
		{
			using ArgumentInfo = TArgumentInfo<>;

			static constexpr bool bIsValidQueryFunction = false;
			using ReturnType = void;
		};

		template<typename Return, typename... Args>
		struct TFunctionInfoImpl<Return(*)(Args...)>
		{
			using ArgumentInfo = TArgumentInfo<Args...>;

			static constexpr bool bIsValidQueryFunction = true;
			using ReturnType = Return;			
		};

		template<typename Class, typename Return, typename... Args>
		struct TFunctionInfoImpl<Return(Class::*)(Args...)>
		{
			using ArgumentInfo = TArgumentInfo<Args...>;

			static constexpr bool bIsValidQueryFunction = true;
			using ReturnType = Return;
		};

		template<typename Class, typename Return, typename... Args>
		struct TFunctionInfoImpl<Return(Class::*)(Args...) const>
		{
			using ArgumentInfo = TArgumentInfo<Args...>;

			static constexpr bool bIsValidQueryFunction = true;
			using ReturnType = Return;
		};

		template<bool IsFunctor, typename FunctionType>
		struct TFunctionInfoSelection {};

		template<typename FunctionType>
		struct TFunctionInfoSelection<true, FunctionType>
		{
			using Type = TFunctionInfoImpl<decltype(&FunctionType::operator())>;
		};

		template<typename FunctionType>
		struct TFunctionInfoSelection<false, FunctionType>
		{
			using Type = TFunctionInfoImpl<FunctionType>;
		};

		template<typename FunctionType>
		struct TFunctionInfo
		{
			using Base = TFunctionInfoSelection<FunctorType<FunctionType>, FunctionType>::Type;

			using ReturnType = typename Base::ReturnType;
			using ArgumentInfo = typename Base::ArgumentInfo;
			using ContextType = typename ArgumentInfo::ContextType;

			static constexpr bool bIsValidQueryFunction =
				Base::bIsValidQueryFunction &&
				(ArgumentInfo::bIsSingle || ArgumentInfo::bIsBatch);
			static constexpr bool bIsSingleRowProcessor = ArgumentInfo::bIsSingle;

			template<typename RequestedReturnType>
			static constexpr void StaticValidation()
			{
				static_assert(bIsValidQueryFunction, "The provided function is not compatible with a query callback. "
					"One or more arguments are possibly incompatible.");

				static_assert(ArgumentInfo::CountContexts() <= 1, "Only zero or one context can be added as an argument.");
				static_assert(ArgumentInfo::CountFlowControls() <= 1, "Only zero or one flow control arguments can be added.");

				if constexpr (std::is_same_v<RequestedReturnType, void>)
				{
					static_assert(ArgumentInfo::ResultIndex == INDEX_NONE,
						"The query function being build has no return value and can't therefore accept a TResult.");
					static_assert(std::is_same_v<ReturnType, void>,
						"The return type of the provided query is expected to be void.");
				}
				else
				{
					if constexpr (bIsSingleRowProcessor)
					{
						if constexpr (ArgumentInfo::ResultIndex >= 0)
						{
							static_assert(ArgumentInfo::CountResults() == 1, "Only one TResult can be present.");
							static_assert(std::is_same_v<typename ArgumentInfo::ResultType, RequestedReturnType>,
								"The type used for TResult is not compatible with the result type expected for the query function.");
							static_assert(std::is_same_v<ReturnType, void>,
								"The return type of the provided query is expected to be void when a TResult is provided.");
						}
						else
						{
							static_assert(std::is_convertible_v<ReturnType, RequestedReturnType>,
								"The type returned is not compatible with the result type expected for the query function.");
						}
					}
					else
					{
						static_assert(ArgumentInfo::ResultIndex >= 0,
							"Batch processing a function that expects a result it's required that a TResult is used.");
						static_assert(ArgumentInfo::CountResults() == 1, "Only one TResult can be present.");
						static_assert(std::is_same_v<typename ArgumentInfo::ResultType, RequestedReturnType>,
							"The type used for TResult is not compatible with the result type expected for the query function.");
						static_assert(std::is_same_v<ReturnType, void>,
							"The return type of the provided query is expected to be void for batch processing functions.");
					}
				}
			}

			template<typename RequestedReturnType>
			static constexpr void SetupResult(TQueryFunction<RequestedReturnType>& Result)
			{
				if constexpr (!std::is_same_v<void, ContextType>)
				{
					Result.Capabilities = IContextContract::SupportedCapabilitiesList<typename ContextType::Capabilities>();
				}
				Result.ConstColumnTypes = ArgumentInfo::ListConstColumns();
				Result.MutableColumnTypes = ArgumentInfo::ListMutableColumns();
				Result.bIsSingleRowProcessor = bIsSingleRowProcessor;
			}
		};

		template<typename FunctionInfo, typename ArgumentInfo, typename ReturnType, typename FunctionType>
		void CallBody(
			TResult<ReturnType>& QueryResult,
			IContextContract& Contract,
			IQueryFunctionResponse& Response,
			typename TQueryFunction<ReturnType>::FunctionSpecializationCallback Specialization,
			FunctionType&& Callback)
		{
			using ArgumentList = typename ArgumentInfo::ArgumentList;

			TConstArrayView<const UScriptStruct*> ConstColumnTypes = ArgumentInfo::ListConstColumns();
			TConstArrayView<const UScriptStruct*> MutableColumnTypes = ArgumentInfo::ListMutableColumns();

			const void** ConstColumnsData = static_cast<const void**>(FMemory_Alloca(sizeof(void*) * ConstColumnTypes.Num()));
			void** MutableColumnsData = static_cast<void**>(FMemory_Alloca(sizeof(void*) * MutableColumnTypes.Num()));

			TArrayView<const void*> ConstColumns(ConstColumnsData, ConstColumnTypes.Num());
			TArrayView<void*> MutableColumns(MutableColumnsData, MutableColumnTypes.Num());

			EFlowControl Flow = EFlowControl::Continue;

			ArgumentList Arguments;
			ArgumentInfo::SetResult(Arguments, QueryResult);
			ArgumentInfo::SetContext(Arguments, Contract);
			ArgumentInfo::SetFlowControl(Arguments, Flow);

			do
			{
				Response.GetConstColumns(ConstColumns, ConstColumnTypes);
				Response.GetMutableColumns(MutableColumns, MutableColumnTypes);

				if (Specialization(Response, ConstColumns, MutableColumns))
				{
					ArgumentInfo::SetConstColumns(Arguments, ConstColumns);
					ArgumentInfo::SetMutableColumns(Arguments, MutableColumns);

					if constexpr (FunctionInfo::bIsSingleRowProcessor)
					{
						do
						{
							if constexpr (ArgumentInfo::ResultIndex >= 0 || std::is_same_v<ReturnType, void>)
							{
								Arguments.ApplyBefore(Callback);
							}
							else
							{
								QueryResult.Add(Arguments.ApplyBefore(Callback));
							}
							ArgumentInfo::IncrementColumns(Arguments);
						} while (Flow == EFlowControl::Continue && Response.NextRow());
					}
					else
					{
						Arguments.ApplyBefore(Callback);
					}
				}
				else
				{
					break;
				}
			} while (Flow == EFlowControl::Continue && Response.NextBatch());
		}
	} // namespace Private

	template<typename ReturnType>
	template<EFunctionCallConfig Config>
	void TQueryFunctionBase<ReturnType>::CallInternal(TResult<ReturnType>& Result, IContextContract& Contract, IQueryFunctionResponse& Response)
	{
		auto Specialization = [](IQueryFunctionResponse& Response, TArrayView<const void*> ConstColumns, TArrayView<void*> MutableColumns)
			{
				if constexpr (EnumHasAnyFlags(Config, EFunctionCallConfig::VerifyColumns))
				{
					bool bMissingColumn = false;
					for (const void* Column : ConstColumns)
					{
						bMissingColumn = bMissingColumn || Column == nullptr;
					}
					for (void* Column : MutableColumns)
					{
						bMissingColumn = bMissingColumn || Column == nullptr;
					}
					return !bMissingColumn;
				}
				else
				{
					return true;
				}
			};

		Function(Result, Contract, Response, Specialization);
	}

	template<typename Return, FunctionType Function>
	TQueryFunction<Return> BuildQueryFunction(Function&& Callback)
	{
		using FunctionInfo = Private::TFunctionInfo<Function>;
		using ArgumentInfo = typename FunctionInfo::ArgumentInfo;
		
		TQueryFunction<Return> Result;
		FunctionInfo::template StaticValidation<Return>();
		FunctionInfo::SetupResult(Result);

		Result.Function =
			[LocalCallback = Forward<Function>(Callback)](
				TResult<Return>& QueryResult, 
				IContextContract& Contract, 
				IQueryFunctionResponse& Response, 
				TQueryFunction<Return>::FunctionSpecializationCallback Specialization)
			{
				Private::CallBody<FunctionInfo, ArgumentInfo, Return>(QueryResult, Contract, Response, Specialization, LocalCallback);
			};
		return Result;
	}

	template<typename Return, FunctionType Function>
	TQueryFunction<void> BuildQueryFunction(TResult<Return>& Result, Function&& Callback)
	{
		using FunctionInfo = Private::TFunctionInfo<Function>;
		using ArgumentInfo = typename FunctionInfo::ArgumentInfo;

		TQueryFunction<Return> CompositedFunction;
		FunctionInfo::template StaticValidation<Return>();
		FunctionInfo::SetupResult(CompositedFunction);

		Result.Function =
			[&Result, LocalCallback = Forward<Function>(Callback)](
				TResult<Return>& QueryResult,
				IContextContract& Contract,
				IQueryFunctionResponse& Response,
				TQueryFunction<Return>::FunctionSpecializationCallback Specialization)
			{
				// Ignore QueryResult (which will always be a dummy for TQueryFunction<void>) in favor of the captured TResult.
				Private::CallBody<FunctionInfo, ArgumentInfo, Return>(Result, Contract, Response, Specialization, LocalCallback);
			};

		return CompositedFunction;
	}
} // namespace UE::Editor::DataStorage::Queries
