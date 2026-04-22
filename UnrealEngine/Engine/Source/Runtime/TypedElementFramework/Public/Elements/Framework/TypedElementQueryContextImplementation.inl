// Copyright Epic Games, Inc. All Rights Reserved.

#include <type_traits>

namespace UE::Editor::DataStorage::Queries
{
	template<typename QueryContext, ContextCapability... SupportedCapabilities>
	template<typename... TArgs>
	TQueryContextImpl<QueryContext, SupportedCapabilities...>::TQueryContextImpl(TArgs&&... Args)
		: Implementation(Forward<TArgs>(Args)...)
	{
	}

	template<typename QueryContext, ContextCapability... SupportedCapabilities>
	template<ContextCapability RequestedCapability>
	constexpr bool TQueryContextImpl<QueryContext, SupportedCapabilities...>::SupportsCapability()
	{
		if constexpr (sizeof...(SupportedCapabilities) > 0)
		{
			return std::is_base_of_v<RequestedCapability, QueryContext> && (std::is_same_v<RequestedCapability, SupportedCapabilities> || ...);
		}
		else
		{
			return std::is_base_of_v<RequestedCapability, QueryContext>;
		}
	}

	template<typename QueryContext, ContextCapability... SupportedCapabilities>
	bool TQueryContextImpl<QueryContext, SupportedCapabilities...>::SupportsCapability(const FName& Capability)
	{
		if constexpr (sizeof...(SupportedCapabilities) > 0)
		{
			return IContextContract::SupportsCapability<QueryContext>(Capability) && ((SupportedCapabilities::Name == Capability) || ...);
		}
		else
		{
			return IContextContract::SupportsCapability<QueryContext>(Capability);
		}
	}

	template<typename QueryContext, ContextCapability... SupportedCapabilities>
	bool TQueryContextImpl<QueryContext, SupportedCapabilities...>::SupportsCapabilities(TConstArrayView<FName> Capabilities)
	{
		for (const FName& Capability : Capabilities)
		{
			if (!SupportsCapability(Capability))
			{
				return false;
			}
		}
		return true;
	}

	// Use a macro for this since it's impossible to do with a template since the function pointer that would be needed might not exist.
	// The macro also has the added benefit that it can print the function name for better reporting.
#define CallFunction(Capability, ReturnValue, FunctionName, ...) \
	if constexpr (SupportsCapability< Capability >()) \
	{ \
		return Implementation.FunctionName( __VA_ARGS__ ); \
	} \
	else \
	{ \
		checkf(false, TEXT("Function '" #FunctionName "' in capability '" #Capability"' is not supported by the current query context implementation.")); \
		return ReturnValue {} ; \
	}

#define ArgTypeName(Type, Name) Type Name
#define ArgName(Type, Name) Name

#define FunctionCommon(ReturnType) \
	template<typename QueryContext, ContextCapability... SupportedCapabilities> \
	ReturnType TQueryContextImpl<QueryContext, SupportedCapabilities...>::

#define Function0(Capability, Return, Function) \
	FunctionCommon(Return) Function() \
	{ \
		CallFunction(Capability, Return, Function ) \
	}
#define Function1(Capability, Return, Function, Arg1) \
	FunctionCommon(Return) Function(ArgTypeName Arg1) \
	{ \
		CallFunction(Capability, Return, Function, ArgName Arg1 ) \
	}
#define Function2(Capability, Return, Function, Arg1, Arg2) \
	FunctionCommon(Return) Function(ArgTypeName Arg1, ArgTypeName Arg2) \
	{ \
		CallFunction(Capability, Return, Function, ArgName Arg1, ArgName Arg2 ) \
	}
#define Function3(Capability, Return, Function, Arg1, Arg2, Arg3) \
	FunctionCommon(Return) Function(ArgTypeName Arg1, ArgTypeName Arg2, ArgTypeName Arg3) \
	{ \
		CallFunction(Capability, Return, Function, ArgName Arg1, ArgName Arg2, ArgName Arg3 ) \
	}
#define Function4(Capability, Return, Function, Arg1, Arg2, Arg3, Arg4) \
	FunctionCommon(Return) Function(ArgTypeName Arg1, ArgTypeName Arg2, ArgTypeName Arg3, ArgTypeName Arg4) \
	{ \
		CallFunction(Capability, Return, Function, ArgName Arg1, ArgName Arg2, ArgName Arg3, ArgName Arg4 ) \
	}

#define ConstFunction0(Capability, Return, Function) \
	FunctionCommon(Return) Function() const \
	{ \
		CallFunction(Capability, Return, Function ) \
	}
#define ConstFunction1(Capability, Return, Function, Arg1) \
	FunctionCommon(Return) Function(ArgTypeName Arg1) const \
	{ \
		CallFunction(Capability, Return, Function, ArgName Arg1 ) \
	}
#define ConstFunction2(Capability, Return, Function, Arg1, Arg2) \
	FunctionCommon(Return) Function(ArgTypeName Arg1, ArgTypeName Arg2) const \
	{ \
		CallFunction(Capability, Return, Function, ArgName Arg1, ArgName Arg2 ) \
	}
#define ConstFunction3(Capability, Return, Function, Arg1, Arg2, Arg3) \
	FunctionCommon(Return) Function(ArgTypeName Arg1, ArgTypeName Arg2, ArgTypeName Arg3) const \
	{ \
		CallFunction(Capability, Return, Function, ArgName Arg1, ArgName Arg2, ArgName Arg3 ) \
	}
#define ConstFunction4(Capability, Return, Function, Arg1, Arg2, Arg3, Arg4) \
	FunctionCommon(Return) Function(ArgTypeName Arg1, ArgTypeName Arg2, ArgTypeName Arg3, ArgTypeName Arg4) const \
	{ \
		CallFunction(Capability, Return, Function, ArgName Arg1, ArgName Arg2, ArgName Arg3, ArgName Arg4 ) \
	}

#define CapabilityStart(Capability, Flags)
#define CapabilityEnd(Capability)

#include "Elements/Framework/TypedElementQueryCapabilities.inl"

#undef ArgTypeName
#undef ArgName
#undef FunctionCommon
#undef Function0
#undef Function1
#undef Function2
#undef Function3
#undef Function4
#undef ConstFunction0
#undef ConstFunction1
#undef ConstFunction2
#undef ConstFunction3
#undef ConstFunction4
#undef CapabilityStart
#undef CapabilityEnd
#undef CallFunction

	//
	// Generic access
	// 

	template<typename QueryContext, ContextCapability... SupportedCapabilities>
	template<typename ReturnType>
	bool TQueryContextImpl<QueryContext, SupportedCapabilities...>::CheckCompatiblity(const TQueryFunction<ReturnType>& Function) const
	{
		bool Result = true;
		for (const FName& Capability : Function.Capabilities)
		{
			if (!SupportsCapability(Capability))
			{
				Result = false;
				checkf(false, TEXT("Requested query context '%s' is not supported by query context implementation."), 
					*Capability.ToString());
			}
		}
		return Result;
	}

	template<typename QueryContext, ContextCapability... SupportedCapabilities>
	const QueryContext& TQueryContextImpl<QueryContext, SupportedCapabilities...>::GetContextImplementation() const
	{
		return Implementation;
	}

	template<typename QueryContext, ContextCapability... SupportedCapabilities>
	QueryContext& TQueryContextImpl<QueryContext, SupportedCapabilities...>::GetContextImplementation()
	{
		return Implementation;
	}
}
// namespace UE::Editor::DataStorage::Queries
