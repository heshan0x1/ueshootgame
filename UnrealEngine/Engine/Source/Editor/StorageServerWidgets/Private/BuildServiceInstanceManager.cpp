// Copyright Epic Games, Inc. All Rights Reserved.

#include "BuildServiceInstanceManager.h"
#include "Experimental/BuildServerInterface.h"


namespace UE::Zen::Build
{

TSharedPtr<FBuildServiceInstance> FServiceInstanceManager::GetBuildServiceInstance() const
{
	if (!CurrentInstance)
	{
		CurrentInstance = MakeShared<FBuildServiceInstance>();
	}
	return CurrentInstance;
}

} // namespace UE::StorageService::Build
