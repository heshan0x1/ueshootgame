// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectCompilerTypes.h"
#include "MuCO/CustomizableObjectInstance.h"


/**
 * Schedules the async compilation of a given Customizable object and, if a Customizable Object Instance is provided, tries to compile only the data required
 * for that given COI. Use the delegate to know when the operation has completed.
 * @param InTargetInstance The Customizable Object Instance generated from the provided COI that we want to use to limit the scope of the CO compilation.
 * @param InCustomizableObjectCompilationDelegate Delegate invoked once the compilation has completed.
 * @param bPerformPartialCompilation Instead of compiling all the data from the InTargetInstance Co just compile the data required for the provided instance.
 * todo: UE-315780 Make this option true by default when the compilation warnings caused by it are resolved
 */
CUSTOMIZABLEOBJECTEDITOR_API void ScheduleCOCompilationForBaking(UCustomizableObjectInstance& InTargetInstance, const FCompileNativeDelegate& InCustomizableObjectCompilationDelegate, const bool bPerformPartialCompilation = false);


/**
 * Schedules the async update of the target instance. It will clear previous update data.
 * This method will also take care of setting and later resetting the state of the Customizable Object System.
 * @param InInstance The instance we want to update so we can later bake it's resources.
 * @param InInstanceUpdateDelegate Delegate to be called after the instance gets updated.
 */
CUSTOMIZABLEOBJECTEDITOR_API void ScheduleInstanceUpdateForBaking(UCustomizableObjectInstance& InInstance, FInstanceUpdateNativeDelegate& InInstanceUpdateDelegate);


/**
 * Serializes onto disk the resources used by the targeted Customizable Object Instance. The operation can be configured in the FInstanceBakingSettings settings object.
 * @param InInstance The mutable COI instance whose resources we want to bake onto disk
 * @param Configuration The baking configuration object with all the settings to be used for this baking operation.
 * @param bIsUnattendedExecution Determines if we want to run this method and show promps for user interaction or if we want to automatically chose the more sensible option (without the need of user interaction)
 * @param OutSavedPackages A collection with the packages marked for save.
 * @return True if the baking operation could be completed without issues and false otherwise
 */
CUSTOMIZABLEOBJECTEDITOR_API bool BakeCustomizableObjectInstance(
	UCustomizableObjectInstance& InInstance,
	const FBakingConfiguration& Configuration,
	bool bIsUnattendedExecution,
	TMap<UPackage*,EPackageSaveResolutionType>& OutSavedPackages);