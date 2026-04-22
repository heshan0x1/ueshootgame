// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>
#include "Elements/Framework/TypedElementQueryCapabilities.h"
#include "Elements/Framework/TypedElementQueryCapabilityForwarder.h"

namespace UE::Editor::DataStorage::Queries
{
	struct IContextContract;

	template<typename T>
	concept ContextCapability = std::is_base_of_v<IContextCapability, T> && !std::is_same_v<IContextCapability, T>;

	template<ContextCapability... Capabilities>
	struct TCapabilityStore : Capabilities... 
	{
		static constexpr bool bIsSingle = (EnumHasAnyFlags(Capabilities::Flags, EContextCapabilityFlags::SupportsSingle) && ...);
		static constexpr bool bIsBatch = (EnumHasAnyFlags(Capabilities::Flags, EContextCapabilityFlags::SupportsBatch) && ...);
	};

	/** 
	 * Template to composite a query context using context capabilities.
	 * Any query callback that requires interacting with the editor data storage requires a context to get access. Each context capability
	 * provides access to a different kind of functionality. Some of the capabilities are mutually exclusive and not all places that accept
	 * a query callback support the same capabilities. It's therefore recommended to only include the capabilities that are needed by the
	 * query callback to improve re-usability or use a predefined query context if the query callback doesn't need to be reused.
	 */
	template<ContextCapability... CapabilityTypes>
	struct TQueryContext final : Private::TForwarder<CapabilityTypes...>
	{
		using Capabilities = TCapabilityStore<CapabilityTypes...>;

		static_assert(Capabilities::bIsSingle || Capabilities::bIsBatch,
			"One or more capabilities that only support single or batch processing were mixed.");

		TQueryContext() = default;
		TQueryContext(IContextContract& Contract);
	};
	
	//
	// Implementations.
	//
	
	template<ContextCapability... CapabilityTypes>
	TQueryContext<CapabilityTypes...>::TQueryContext(IContextContract& Contract)
		: Private::TForwarder<CapabilityTypes...>(Contract)
	{
	}
} // namespace UE::Editor::DataStorage::Queries
