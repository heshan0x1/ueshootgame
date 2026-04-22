// Copyright Epic Games, Inc. All Rights Reserved.

#include "MusicEnvironmentEditorModule.h"
#include "Modules/ModuleInterface.h"
#include "PropertyEditorModule.h"
#include "Customization/FrameBasedTimeSignatureCustomization.h"
#include "Modules/ModuleManager.h"

class FMusicEnvironmentEditorModule : public IModuleInterface
{
public:
 
	virtual void StartupModule() override
	{
		// register detail customizations
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditorModule.RegisterCustomPropertyTypeLayout("FrameBasedTimeSignature", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FFrameBasedTimeSignatureCustomization::MakeInstance));
	}
 
	virtual void ShutdownModule() override
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditorModule.UnregisterCustomPropertyTypeLayout("FrameBasedTimeSignature");
	}
};

IMPLEMENT_MODULE(FMusicEnvironmentEditorModule, MusicEnvironmentEditor);