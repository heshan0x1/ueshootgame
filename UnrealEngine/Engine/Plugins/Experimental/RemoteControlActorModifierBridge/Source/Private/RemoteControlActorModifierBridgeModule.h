// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleInterface.h"

#include "Containers/ContainersFwd.h"
#include "Templates/SharedPointerFwd.h"

class FPropertyPath;
class UObject;
struct FRemoteControlProperty;

class FRemoteControlActorModifierBridgeModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

private:
	bool ResolverActorModifierProperty(const TSharedRef<FRemoteControlProperty>& InProperty, TArray<UObject*>& InOutBoundObjects, TSharedPtr<FPropertyPath>& OutPropertyPath);
};
