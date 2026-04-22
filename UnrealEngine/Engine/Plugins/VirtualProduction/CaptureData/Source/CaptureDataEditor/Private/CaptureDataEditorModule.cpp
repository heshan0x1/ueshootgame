// Copyright Epic Games, Inc. All Rights Reserved.

#include "CaptureDataEditorModule.h"

#include "DeferredObjectDirtier.h"

#include "PropertyEditorModule.h"

#include "CaptureMetadata.h"
#include "Customizations/CaptureMetadataCustomization.h"

#include "Modules/ModuleManager.h"

void FCaptureDataEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditorModule.RegisterCustomClassLayout(UCaptureMetadata::StaticClass()->GetFName(),
												   FOnGetDetailCustomizationInstance::CreateStatic(&FCaptureMetadataCustomization::MakeInstance));
}

void FCaptureDataEditorModule::ShutdownModule()
{
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditorModule.UnregisterCustomClassLayout(UCaptureMetadata::StaticClass()->GetFName());
}

void FCaptureDataEditorModule::DeferMarkDirty(TWeakObjectPtr<UObject> InObject)
{
	using namespace UE::CaptureManager;
	
	FDeferredObjectDirtier& ObjectDirtier = FDeferredObjectDirtier::Get();
	ObjectDirtier.Enqueue(MoveTemp(InObject));
}

IMPLEMENT_MODULE(FCaptureDataEditorModule, CaptureDataEditor)