// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementQueryCapabilities.h"
#include "Elements/Framework/TypedElementQueryContextImplementation.h"
#include "Elements/Framework/TypedElementQueryFunctionArgumentTypes.h"
#include "Templates/FunctionFwd.h"

namespace UE::Editor::DataStorage::Queries
{
	struct FMockChainTerminator {};

	struct FQueryContextMock final : public

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

#define CapabilityStart(Capability, Flags) Capability, 
#define CapabilityEnd(Capability)

#include "Elements/Framework/TypedElementQueryCapabilities.inl"

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

		FMockChainTerminator
	{

#define WithWrappers 1

#define ArgTypeName(Type, Name) Type Name
#define ArgName(Type, Name) Name
#define MockFunction(Capability, Function) Capability##_##Function##_Mock

#define Function0(Capability, Return, Function) \
		TFunction<Return ()> MockFunction(Capability, Function); \
		virtual Return Function() override \
		{ \
			return MockFunction(Capability, Function)(); \
		}
#define Function1(Capability, Return, Function, Arg1) \
		TFunction<Return (ArgTypeName Arg1 )> MockFunction(Capability, Function); \
		virtual Return Function(ArgTypeName Arg1) override \
		{ \
			return MockFunction(Capability, Function)(ArgName Arg1); \
		}
#define Function2(Capability, Return, Function, Arg1, Arg2) \
		TFunction<Return (ArgTypeName Arg1, ArgTypeName Arg2)> MockFunction(Capability, Function); \
		virtual Return Function(ArgTypeName Arg1, ArgTypeName Arg2) override \
		{ \
			return MockFunction(Capability, Function)(ArgName Arg1, ArgName Arg2); \
		}
#define Function3(Capability, Return, Function, Arg1, Arg2, Arg3) \
		TFunction<Return (ArgTypeName Arg1, ArgTypeName Arg2, ArgTypeName Arg3)> MockFunction(Capability, Function); \
		virtual Return Function(ArgTypeName Arg1, ArgTypeName Arg2, ArgTypeName Arg3) override \
		{ \
			return MockFunction(Capability, Function)(ArgName Arg1, ArgName Arg2, ArgName Arg3); \
		}
#define Function4(Capability, Return, Function, Arg1, Arg2, Arg3, Arg4) \
		TFunction<Return (ArgTypeName Arg1, ArgTypeName Arg2, ArgTypeName Arg3, ArgTypeName Arg4)> MockFunction(Capability, Function); \
		virtual Return Function(ArgTypeName Arg1, ArgTypeName Arg2, ArgTypeName Arg3, ArgTypeName Arg4) override \
		{ \
			return MockFunction(Capability, Function)(ArgName Arg1, ArgName Arg2, ArgName Arg3, ArgName Arg4); \
		}

#define ConstFunction0(Capability, Return, Function) \
		TFunction<Return ()> MockFunction(Capability, Function); \
		virtual Return Function() const override \
		{ \
			return MockFunction(Capability, Function)(); \
		}
#define ConstFunction1(Capability, Return, Function, Arg1) \
		TFunction<Return (ArgTypeName Arg1)> MockFunction(Capability, Function); \
		virtual Return Function(ArgTypeName Arg1) const override \
		{ \
			return MockFunction(Capability, Function)(ArgName Arg1); \
		}
#define ConstFunction2(Capability, Return, Function, Arg1, Arg2) \
		TFunction<Return (ArgTypeName Arg1, ArgTypeName Arg2)> MockFunction(Capability, Function); \
		virtual Return Function(ArgTypeName Arg1, ArgTypeName Arg2) const override \
		{ \
			return MockFunction(Capability, Function)(ArgName Arg1, ArgName Arg2); \
		}
#define ConstFunction3(Capability, Return, Function, Arg1, Arg2, Arg3) \
		TFunction<Return (ArgTypeName Arg1, ArgTypeName Arg2, ArgTypeName Arg3)> MockFunction(Capability, Function); \
		virtual Return Function(ArgTypeName Arg1, ArgTypeName Arg2, ArgTypeName Arg3) const override \
		{ \
			return MockFunction(Capability, Function)(ArgName Arg1, ArgName Arg2, ArgName Arg3); \
		}
#define ConstFunction4(Capability, Return, Function, Arg1, Arg2, Arg3, Arg4) \
		TFunction<Return (ArgTypeName Arg1, ArgTypeName Arg2, ArgTypeName Arg3, ArgTypeName Arg4)> MockFunction(Capability, Function); \
		virtual Return Function(ArgTypeName Arg1, ArgTypeName Arg2, ArgTypeName Arg3, ArgTypeName Arg4) const override \
		{ \
			return MockFunction(Capability, Function)(ArgName Arg1, ArgName Arg2, ArgName Arg3, ArgName Arg4); \
		}

#define CapabilityStart(Capability, Flags) 
#define CapabilityEnd(Capability)

#include "Elements/Framework/TypedElementQueryCapabilities.inl"

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
#undef WithWrappers

#define MockFunctionImplementation(Capability, Return, Function) \
		checkf(false, TEXT( "Context function " #Function " in capability " #Capability " was unexpectedly called." )); return Return{};
#define Function0(Capability, Return, Function) \
		MockFunction(Capability, Function) = []() { MockFunctionImplementation(Capability, Return, Function) };
#define Function1(Capability, Return, Function, Arg1) \
		MockFunction(Capability, Function) = [](ArgTypeName Arg1) { MockFunctionImplementation(Capability, Return, Function) };
#define Function2(Capability, Return, Function, Arg1, Arg2) \
		MockFunction(Capability, Function) = [](ArgTypeName Arg1, ArgTypeName Arg2) { MockFunctionImplementation(Capability, Return, Function) };
#define Function3(Capability, Return, Function, Arg1, Arg2, Arg3) \
		MockFunction(Capability, Function) = [](ArgTypeName Arg1, ArgTypeName Arg2, ArgTypeName Arg3) { MockFunctionImplementation(Capability, Return, Function) };
#define Function4(Capability, Return, Function, Arg1, Arg2, Arg3, Arg4) \
		MockFunction(Capability, Function) = [](ArgTypeName Arg1, ArgTypeName Arg2, ArgTypeName Arg3, ArgTypeName Arg4) { MockFunctionImplementation(Capability, Return, Function) };

#define ConstFunction0(Capability, Return, Function) Function0(Capability, Return, Function)
#define ConstFunction1(Capability, Return, Function, Arg1) Function1(Capability, Return, Function, Arg1)
#define ConstFunction2(Capability, Return, Function, Arg1, Arg2) Function2(Capability, Return, Function, Arg1, Arg2)
#define ConstFunction3(Capability, Return, Function, Arg1, Arg2, Arg3) Function3(Capability, Return, Function, Arg1, Arg2, Arg3)
#define ConstFunction4(Capability, Return, Function, Arg1, Arg2, Arg3, Arg4) Function4(Capability, Return, Function, Arg1, Arg2, Arg3, Arg4)

		FQueryContextMock()
		{
#include "Elements/Framework/TypedElementQueryCapabilities.inl"
		}

#undef ArgTypeName
#undef ArgName
#undef MockFunctionName
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
	};

	using QueryContextMock = TQueryContextImpl<FQueryContextMock>;
} // namespace UE::Editor::DataStorage::Queries
