// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanIdentityFactoryNew.h"
#include "MetaHumanIdentity.h"
#include "UObject/Package.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanIdentityFactoryNew)

#define LOCTEXT_NAMESPACE "MetaHumanIdentityFactory"

UMetaHumanIdentityFactoryNew::UMetaHumanIdentityFactoryNew()
{
	// Creating Identity assets on non-Windows platforms is currently disabled
#if PLATFORM_WINDOWS
	bCreateNew = true;
#else
	bCreateNew = false;
#endif
	bEditAfterNew = true;
	SupportedClass = UMetaHumanIdentity::StaticClass();
}

UObject* UMetaHumanIdentityFactoryNew::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* Context, FFeedbackContext* Warn)
{
	UObject* NewIdentity = NewObject<UMetaHumanIdentity>(InParent, InClass, InName, InFlags | RF_Transactional);
	
	// Disable exporting for the identity asset until we implement a custom exporter
	// JIRA: MH-7716
	check(NewIdentity);
	check(NewIdentity->GetPackage());
	NewIdentity->GetPackage()->SetPackageFlags(PKG_DisallowExport);

	return NewIdentity;
}

FText UMetaHumanIdentityFactoryNew::GetToolTip() const
{
	return LOCTEXT("MetaHumanIdentityFactory_ToolTip",
		"MetaHuman Identity Asset\n"
		"\nProvides the tools to auto-generate a fully rigged Skeletal Mesh\n"
		"of a human face from Capture Data(Mesh or Footage) by tracking\n"
		"the facial features, fitting a Template Mesh having MetaHuman\n"
		"topology to the tracked curves, and sending the resulting mesh\n"
		"to MetaHuman Service, which returns an auto - rigged SkeletalMesh\n"
		"resembling the person from the Capture Data.\n"
		"\nThe obtained Skeletal Mesh can be used by MetaHuman Performance\n"
		"asset to generate an Animation Sequence from video footage.\n"
		"\nMetaHuman Identity Asset Toolkit can also create a full MetaHuman in MetaHuman\n"
		"Creator, downloadable through Quixel Bridge.");
}

#undef LOCTEXT_NAMESPACE
