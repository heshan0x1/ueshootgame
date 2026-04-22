// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"

enum ELifetimeCondition : int;

namespace UE::Net::Private
{

class FPropertyConditionDelegates
{
public:
	using FOnPropertyCustomConditionChanged = TMulticastDelegate<void(const UObject* OwningObject, const uint16 RepIndex, const bool bActive)>;
	using FOnPropertyDynamicConditionChanged = TMulticastDelegate<void(const UObject* OwningObject, const uint16 RepIndex, const ELifetimeCondition Condition)>;
	
	static FOnPropertyCustomConditionChanged& GetOnPropertyCustomConditionChangedDelegate() { return OnPropertyCustomConditionChangedDelegate; }
	static FOnPropertyDynamicConditionChanged& GetOnPropertyDynamicConditionChangedDelegate() { return OnPropertyDynamicConditionChangedDelegate; }

private:
	static NETCORE_API FOnPropertyCustomConditionChanged OnPropertyCustomConditionChangedDelegate;
	static NETCORE_API FOnPropertyDynamicConditionChanged OnPropertyDynamicConditionChangedDelegate;
};

} // UE::Net::Private

