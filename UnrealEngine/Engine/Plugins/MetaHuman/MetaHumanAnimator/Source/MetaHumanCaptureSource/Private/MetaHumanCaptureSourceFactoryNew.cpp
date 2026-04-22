// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCaptureSourceFactoryNew.h"
#include "MetaHumanCaptureSource.h"
#include "MetaHumanCaptureSourceSync.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanCaptureSourceFactoryNew)

#define LOCTEXT_NAMESPACE "MetaHumanCaptureSourceFactory"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

UMetaHumanCaptureSourceFactoryNew::UMetaHumanCaptureSourceFactoryNew()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UMetaHumanCaptureSource::StaticClass();
}

UObject* UMetaHumanCaptureSourceFactoryNew::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, FFeedbackContext* InWarn)
{
	return NewObject<UMetaHumanCaptureSource>(InParent, InClass, InName, InFlags);
}

FText UMetaHumanCaptureSourceFactoryNew::GetToolTip() const
{
	return LOCTEXT("MetaHumanCaptureSourceFactory_ToolTip",
		"MetaHuman Capture Source Asset\n"
		"\nAn asset representing a physical device or an archive\n"
		"that can be used to import the footage data into Unreal Editor.\n"
		"\nA footage of live performance, in combination with a Skeletal Mesh\n"
		"obtained through MetaHuman Identity asset toolkit.Used in MetaHuman\n"
		"Performance asset to generate an Animation Sequence by automatically\n"
		"tracking facial features of the actor in the performance.");
}

UMetaHumanCaptureSourceSyncFactoryNew::UMetaHumanCaptureSourceSyncFactoryNew()
{
	bCreateNew = false;
	bEditAfterNew = true;
	SupportedClass = UMetaHumanCaptureSourceSync::StaticClass();
}

UObject* UMetaHumanCaptureSourceSyncFactoryNew::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, FFeedbackContext* InWarn)
{
	return NewObject<UMetaHumanCaptureSourceSync>(InParent, InClass, InName, InFlags);
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

#undef LOCTEXT_NAMESPACE
