// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementQueryCapabilities.h"

namespace UE::Editor::DataStorage::Queries
{
	namespace Private
	{
		template<template<typename> typename Base, template<typename> typename... Bases>
		struct IContextContractCombinerImpl : Base<IContextContractCombinerImpl<Bases>...>
		{
			virtual ~IContextContractCombinerImpl() override = default;
		};

		template<template<typename> typename Base>
		struct IContextContractCombinerImpl<Base> : Base<IContextCapability>
		{
			virtual ~IContextContractCombinerImpl() override = default;
		};

		template<template<typename> typename... Bases>
		struct IContextContractCombiner : IContextContractCombinerImpl<Bases>...
		{
			virtual ~IContextContractCombiner() override = default;

		private:
			// Work around for MSVC struggling to correctly expand fold expressions when providing the implementation for a template with 
			// templated types.
			template<typename Implementation, template<typename> typename Base>
			constexpr static bool ImplementsCapability()
			{
				return std::is_base_of_v<Base<IContextCapability>, Implementation>;
			}

			template<typename Implementation, template<typename> typename Base>
			static bool ImplementsCapability(const FName& Capability)
			{
				return ImplementsCapability<Implementation, Base>() && Base<IContextCapability>::Name == Capability;
			}

		public:
			template<typename Implementation>
			static bool SupportsCapability(const FName& Capability)
			{
				return (ImplementsCapability<Implementation, Bases>(Capability) || ...);
			}

			template<typename Implementation>
			constexpr static int32 CountSupportedCapabilities()
			{
				return 0 + ((ImplementsCapability<Implementation, Bases>() ? 1 : 0) + ...);
			}

			template<typename Implementation>
			static TConstArrayView<FName> SupportedCapabilitiesList()
			{
				static TConstArrayView<FName> Result = []()
					{
						constexpr int32 CapabilityCount = CountSupportedCapabilities<Implementation>();
						static FName Capabilities[CapabilityCount] = {};

						int32 Index = 0;
						(
							[&]
							{
								if constexpr (ImplementsCapability<Implementation, Bases>())
								{
									Capabilities[Index++] = Bases<IContextCapability>::Name;
								}
							}(), ...);

						return TConstArrayView<FName>(Capabilities, CapabilityCount);
					}();
				return Result;
			}
		};

		//Used to remove the first template argument as that's a placeholder to allow the X-macros to work with the template.
		template<typename, template<typename> typename... Bases>
		struct IPreContextContractCombiner : IContextContractCombiner<Bases...>
		{
			virtual ~IPreContextContractCombiner() override = default;
		};
} // namespace Private

#define Function0(Capability, Return, Function)
#define Function1(Capability, Return, Function, Arg1)
#define Function2(Capability, Return, Function, Arg1, Arg2)
#define Function3(Capability, Return, Function, Arg1, Arg2, Arg3)
#define Function4(Capability, Return, Function, Arg1, Arg2, Arg3, Arg4)
#define ConstFunction0(Capability, Return, Function)
#define ConstFunction1(Capability, Return, Function, Arg1)
#define ConstFunction2(Capability, Return, Function, Arg1, Arg2)
#define ConstFunction3(Capability, Return, Function, Arg1, Arg2, Arg3)
#define ConstFunction4(Capability, Return, Function, Arg1, Arg2, Arg3, Arg4)

#define CapabilityStart(Capability, Flags) , I##Capability
#define CapabilityEnd(Capability)

	/** 
	 * Contract between context and implementation to be able to communicate.
	 * The contract contains all functions for all capabilities that are available. Based on the supported capabilities
	 * an implementation may opt to only partially implement the contract, with the remaining functions asserting. On 
	 * the opposite side a context may restrict what funtions on the contract can be called based on the requested
	 * capabilities. Through the query function the capabilities on both sides are kept aligned, resulting in no
	 * function on the contract being callable if they're not implemented.
	 */
	struct IContextContract : Private::IPreContextContractCombiner<int
#include "Elements/Framework/TypedElementQueryCapabilities.inl"
	>
	{
		virtual ~IContextContract() override = default;
	};

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
} // namespace UE::Editor::DataStorage::Queries