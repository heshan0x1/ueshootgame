// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_7
#include "CoreMinimal.h"
#include "ObjectTrace.h"
#include "StructUtils/PropertyBag.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_7

#include "CoreTypes.h"
#include "Misc/Guid.h"
#include "ObjectTraceDefines.h"
#include "Trace/Trace.h"
#include "Templates/SharedPointer.h"

#define ANIMNEXT_TRACE_ENABLED (OBJECT_TRACE_ENABLED && !(UE_BUILD_SHIPPING || UE_BUILD_TEST))

#if ANIMNEXT_TRACE_ENABLED

UE_TRACE_CHANNEL_EXTERN(UAFChannel, UAF_API);

class UObject;
struct FAnimNextModuleInstance;
struct FUAFAssetInstance;
struct FAnimNextGraphInstance;
struct FUAFInstanceVariableContainer;

namespace UE::UAF
{
struct FAnimNextTrace
{
	UAF_API static const FGuid CustomVersionGUID;
	
	UAF_API static void Reset();
	
	UAF_API static void OutputAnimNextInstance(const FUAFAssetInstance* DataInterface, const UObject* OuterObject);
	UAF_API static void OutputAnimNextVariables(const FUAFAssetInstance* DataInterface, const UObject* OuterObject);

private:
	UAF_API static bool OutputAnimNextVariableSet(const TSharedRef<FUAFInstanceVariableContainer>& VariableSet, uint64 InstanceId, const UObject* OuterObject);
};

}


#define TRACE_ANIMNEXT_INSTANCE(DataInterface, OuterObject) UE::UAF::FAnimNextTrace::OutputAnimNextInstance(DataInterface, OuterObject);
#define TRACE_ANIMNEXT_VARIABLES(DataInterface, OuterObject) UE::UAF::FAnimNextTrace::OutputAnimNextVariables(DataInterface, OuterObject);
#else
#define TRACE_ANIMNEXT_INSTANCE(DataInterface, OuterObject)
#define TRACE_ANIMNEXT_VARIABLES(DataInterface, OuterObject)
#endif
