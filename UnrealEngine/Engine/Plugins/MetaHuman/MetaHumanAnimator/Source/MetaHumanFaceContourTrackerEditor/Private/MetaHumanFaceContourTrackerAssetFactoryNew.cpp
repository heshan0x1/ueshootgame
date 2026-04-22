// Copyright Epic Games, Inc. All Rights Reserved.


#include "MetaHumanFaceContourTrackerAssetFactoryNew.h"
#include "MetaHumanFaceContourTrackerAsset.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanFaceContourTrackerAssetFactoryNew)

#define LOCTEXT_NAMESPACE "MetaHumanFaceContourTrackerAssetFactory"

/* UMetaHumanFaceContourTrackerAssetFactoryNew structors
 *****************************************************************************/

UMetaHumanFaceContourTrackerAssetFactoryNew::UMetaHumanFaceContourTrackerAssetFactoryNew()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UMetaHumanFaceContourTrackerAsset::StaticClass();
}

/* UFactory overrides
 *****************************************************************************/

UObject* UMetaHumanFaceContourTrackerAssetFactoryNew::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, FFeedbackContext* InWarn)
{
	return NewObject<UMetaHumanFaceContourTrackerAsset>(InParent, InClass, InName, InFlags);
};

FText UMetaHumanFaceContourTrackerAssetFactoryNew::GetToolTip() const
{
	return LOCTEXT("MetaHumanFaceContourTrackerAssetFactory_ToolTip",
		"MetaHuman Face Contour Tracker Asset\n"
		"\nContains trackers for different facial features\n"
		"Used in MetaHuman Identity and Performance assets.");
}

#undef LOCTEXT_NAMESPACE
