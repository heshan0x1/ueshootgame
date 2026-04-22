// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>
#include "Containers/ContainersFwd.h"
#include "DataStorage/Handles.h"
#include "Misc/EnumClassFlags.h"
#include "UObject/NameTypes.h"

class UScriptStruct;

namespace UE::Editor::DataStorage::Queries
{
	enum class EContextCapabilityFlags
	{
		SupportsSingle = 1 << 0,
		SupportsBatch = 1 << 1
	};
	ENUM_CLASS_FLAGS(EContextCapabilityFlags)

	/** 
	 * Class fragments used to composite a context.
	 * A query context is created by combining various context capabilities together. Users can request a query context
	 * being created with one or more of these capabilities and based on the requested capabilities they get connected
	 * with an implementation. Each capability represents a subset of functionality of TEDS such as access to column data,
	 * creating new rows or access to subqueries.
	 */
	struct IContextCapability
	{
		virtual ~IContextCapability() = default;
	};

	
#define ArgTypeName(Type, Name) Type Name

#define Function0(Capability, Return, Function) virtual Return Function() = 0;
#define Function1(Capability, Return, Function, Arg1) virtual Return Function(ArgTypeName Arg1 ) = 0;
#define Function2(Capability, Return, Function, Arg1, Arg2) virtual Return Function(ArgTypeName Arg1 , ArgTypeName Arg2 ) = 0;
#define Function3(Capability, Return, Function, Arg1, Arg2, Arg3) virtual Return Function(ArgTypeName Arg1 , ArgTypeName Arg2 , ArgTypeName Arg3 ) = 0;
#define Function4(Capability, Return, Function, Arg1, Arg2, Arg3, Arg4) virtual Return Function(ArgTypeName Arg1 , ArgTypeName Arg2 , ArgTypeName Arg3 , ArgTypeName Arg4 ) = 0;

#define ConstFunction0(Capability, Return, Function) virtual Return Function() const = 0;
#define ConstFunction1(Capability, Return, Function, Arg1) virtual Return Function(ArgTypeName Arg1 ) const = 0;
#define ConstFunction2(Capability, Return, Function, Arg1, Arg2) virtual Return Function(ArgTypeName Arg1 , ArgTypeName Arg2 ) const = 0;
#define ConstFunction3(Capability, Return, Function, Arg1, Arg2, Arg3) virtual Return Function(ArgTypeName Arg1 , ArgTypeName Arg2 , ArgTypeName Arg3 ) const = 0;
#define ConstFunction4(Capability, Return, Function, Arg1, Arg2, Arg3, Arg4) virtual Return Function(ArgTypeName Arg1 , ArgTypeName Arg2 , ArgTypeName Arg3 , ArgTypeName Arg4 ) const = 0;

#define CapabilityStart(InCapability, InFlags) \
	template<typename Base> \
	struct I##InCapability : Base \
	{ \
		inline static const FName Name = #InCapability ; \
		static constexpr EContextCapabilityFlags Flags = InFlags; \
		virtual ~I##InCapability () override = default;

#define CapabilityEnd(Capability) \
	}; \
	using Capability = I##Capability <IContextCapability>;

#include "Elements/Framework/TypedElementQueryCapabilities.inl"

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
} // namespace UE::Editor::DataStorage::Queries