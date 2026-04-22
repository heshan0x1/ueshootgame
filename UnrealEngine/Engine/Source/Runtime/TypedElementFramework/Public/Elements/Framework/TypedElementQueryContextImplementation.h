// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementQueryCapabilities.h"
#include "Elements/Framework/TypedElementQueryContract.h"
#include "Elements/Framework/TypedElementQueryContext.h"
#include "Elements/Framework/TypedElementQueryFunctions.h"
#include "Elements/Framework/TypedElementQueryFunctionArgumentTypes.h"

#define ArgTypeName(Type, Name) Type Name

#define Function0(Capability, Return, Function) virtual Return Function() override;
#define Function1(Capability, Return, Function, Arg1) virtual Return Function(ArgTypeName Arg1 ) override;
#define Function2(Capability, Return, Function, Arg1, Arg2) virtual Return Function(ArgTypeName Arg1 , ArgTypeName Arg2 ) override;
#define Function3(Capability, Return, Function, Arg1, Arg2, Arg3) virtual Return Function(ArgTypeName Arg1 , ArgTypeName Arg2 , ArgTypeName Arg3 ) override;
#define Function4(Capability, Return, Function, Arg1, Arg2, Arg3, Arg4) virtual Return Function(ArgTypeName Arg1 , ArgTypeName Arg2 , ArgTypeName Arg3 , ArgTypeName Arg4 ) override;

#define ConstFunction0(Capability, Return, Function) virtual Return Function() const override;
#define ConstFunction1(Capability, Return, Function, Arg1) virtual Return Function(ArgTypeName Arg1 ) const override;
#define ConstFunction2(Capability, Return, Function, Arg1, Arg2) virtual Return Function(ArgTypeName Arg1 , ArgTypeName Arg2 ) const override;
#define ConstFunction3(Capability, Return, Function, Arg1, Arg2, Arg3) virtual Return Function(ArgTypeName Arg1 , ArgTypeName Arg2 , ArgTypeName Arg3 ) const override;
#define ConstFunction4(Capability, Return, Function, Arg1, Arg2, Arg3, Arg4) virtual Return Function(ArgTypeName Arg1 , ArgTypeName Arg2 , ArgTypeName Arg3 , ArgTypeName Arg4 ) const override;

#define CapabilityStart(Capability, Flags)
#define CapabilityEnd(Capability)

#define WithWrappers 1

namespace UE::Editor::DataStorage::Queries
{
	/** 
	 * Base used for context implementations. This will verify if a context is correctly implemented.
	 * Classes that implement capabilities should use this template to automatically satisfy the requirements
	 * of a query contract. Any missing functionality will be filled in by this template with a placeholder
	 * function that asserts. The supported capabilities are automatically extracted from the implementation type
	 * but can be overruled by providing a list of capabilities as extra template arguments. Note that any
	 * capabilities provided this way do need to be implemented by the implementation type.
	 */
	template<typename ImplementationType, ContextCapability... SupportedCapabilities>
	class TQueryContextImpl final : public IContextContract
	{
	public:
		template<typename... TArgs>
		explicit TQueryContextImpl(TArgs&&... Args);

		virtual ~TQueryContextImpl() override = default;

		static bool SupportsCapabilities(TConstArrayView<FName> Capabilities);

#include "Elements/Framework/TypedElementQueryCapabilities.inl"

		// Generic access
		template<typename ReturnType>
		bool CheckCompatiblity(const TQueryFunction<ReturnType>& Function) const;
		const ImplementationType& GetContextImplementation() const;
		ImplementationType& GetContextImplementation();
	
	private:
		template<ContextCapability RequestedCapability>
		static constexpr bool SupportsCapability();
		static bool SupportsCapability(const FName& Capability);

		ImplementationType Implementation;
	};
}
// namespace UE::Editor::DataStorage::Queries

#undef ArgTypeName
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
#undef WithWrappers

#include "Elements/Framework/TypedElementQueryContextImplementation.inl"
