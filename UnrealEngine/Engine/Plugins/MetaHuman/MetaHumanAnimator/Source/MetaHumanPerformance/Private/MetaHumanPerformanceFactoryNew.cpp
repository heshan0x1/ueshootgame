// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanPerformanceFactoryNew.h"
#include "MetaHumanPerformance.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanPerformanceFactoryNew)

//////////////////////////////////////////////////////////////////////////
// UMetaHumanPerformanceFactoryNew

#define LOCTEXT_NAMESPACE "MetaHumanPerformanceFactory"

UMetaHumanPerformanceFactoryNew::UMetaHumanPerformanceFactoryNew()
{
	// Creating Performance assets on non-Windows platforms is currently disabled
#if PLATFORM_WINDOWS
	bCreateNew = true;
#else
	bCreateNew = false;
#endif
	bEditAfterNew = true;
	SupportedClass = UMetaHumanPerformance::StaticClass();
}

UObject* UMetaHumanPerformanceFactoryNew::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, FFeedbackContext* InWarn)
{
	return NewObject<UMetaHumanPerformance>(InParent, InClass, InName, InFlags);
}

FText UMetaHumanPerformanceFactoryNew::GetToolTip() const
{
	return LOCTEXT("MetaHumanPerformanceFactory_ToolTip",
		"MetaHuman Performance Asset\n"
		"\nProduces an Animation Sequence for MetaHuman Control Rig by tracking\n"
		"facial expressions in video - footage from a Capture Source, imported\n"
		"through Capture Manager, using a SkeletalMesh obtained through\n"
		"MetaHuman Identity asset toolkit.");
}

#undef LOCTEXT_NAMESPACE
