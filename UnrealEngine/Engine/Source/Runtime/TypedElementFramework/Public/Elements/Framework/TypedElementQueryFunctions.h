// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>
#include "Containers/ContainersFwd.h"
#include "Elements/Framework/TypedElementQueryCapabilities.h"
#include "Elements/Framework/TypedElementQueryFunctionArgumentTypes.h"
#include "Misc/EnumClassFlags.h"
#include "Templates/Function.h"
#include "UObject/NameTypes.h"

class UScriptStruct;

namespace UE::Editor::DataStorage::Queries
{
	struct IContextContract;

	struct IQueryFunctionResponse
	{
		virtual ~IQueryFunctionResponse() = default;
		
		virtual bool NextBatch() = 0;
		virtual bool NextRow() = 0;

		virtual void GetConstColumns(TArrayView<const void*> ColumnsData, TConstArrayView<const UScriptStruct*> ColumnTypes) = 0;
		virtual void GetMutableColumns(TArrayView<void*> ColumnsData, TConstArrayView<const UScriptStruct*> ColumnTypes) = 0;
	};

	enum class EFunctionCallConfig
	{
		None = 0,
		/** If not all columns could be retrieved, return the default value for the return type, if applicable, and don't call the function. */
		VerifyColumns = 1 << 0,
	};
	ENUM_CLASS_FLAGS(EFunctionCallConfig);

	template<typename T>
	concept FunctionType =
		requires { &std::remove_reference_t<T>::operator(); }			// non-generic lambdas & single-operator() functors
		|| std::is_function_v<std::remove_pointer_t<std::decay_t<T>>>	// functions, function pointers & function references
		|| std::is_member_function_pointer_v<std::decay_t<T>>;			// member function pointers

	/** Storage for a function that can be used as part of a query.  */
	template<typename ReturnType>
	class TQueryFunctionBase
	{
	public:
		using FunctionSpecializationCallback = 
			bool(*)(IQueryFunctionResponse& Response, TArrayView<const void*> ConstColumns, TArrayView<void*> MutableColumns);
	
		using WrapperFunctionType = TFunction<
			void(
				TResult<ReturnType>& Result, 
				IContextContract& Contract, 
				IQueryFunctionResponse& Response,
				FunctionSpecializationCallback Specialization)
		>;

		TConstArrayView<FName> Capabilities;
		TConstArrayView<const UScriptStruct*> ConstColumnTypes;
		TConstArrayView<const UScriptStruct*> MutableColumnTypes;
		WrapperFunctionType Function;
		bool bIsSingleRowProcessor;

	protected:
		template<EFunctionCallConfig Config>
		void CallInternal(TResult<ReturnType>& Result, IContextContract& Contract, IQueryFunctionResponse& Response);
	};

	template<typename ReturnType>
	class TQueryFunction final : public TQueryFunctionBase<ReturnType>
	{
	public:
		template<EFunctionCallConfig Config>
		void Call(TResult<ReturnType>& Result, IContextContract& Contract, IQueryFunctionResponse& Response)
		{
			this->template CallInternal<Config>(Result, Contract, Response);
		}

		void Call(TResult<ReturnType>& Result, IContextContract& Contract, IQueryFunctionResponse& Response)
		{
			this->template CallInternal<EFunctionCallConfig::None>(Result, Contract, Response);
		}
	};

	template<>
	class TQueryFunction<void> final : public TQueryFunctionBase<void>
	{
	public:
		template<EFunctionCallConfig Config>
		void Call(IContextContract& Contract, IQueryFunctionResponse& Response)
		{
			TResult<void> Dummy;
			this->template CallInternal<Config>(Dummy, Contract, Response);
		}

		void Call(IContextContract& Contract, IQueryFunctionResponse& Response)
		{
			this->Call<EFunctionCallConfig::None>(Contract, Response);
		}
	};

	template<typename Return, FunctionType Function>
	TQueryFunction<Return> BuildQueryFunction(Function&& Callback);

	template<typename Return, FunctionType Function>
	TQueryFunction<void> BuildQueryFunction(TResult<Return>& Result, Function&& Callback);

} // namespace UE::Editor::DataStorage::Queries

#include "Elements/Framework/TypedElementQueryFunctions.inl"
