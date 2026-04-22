// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraNaniteEditorModule.h"
#include "NiagaraEditorModule.h"
#include "NiagaraEditorSettings.h"

#include "Customizations/NiagaraParameterComponentBindingCustomization.h"
#include "Renderer/NiagaraNaniteRendererProperties.h"
#include "NiagaraParameterComponentBinding.h"

#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "FNiagaraNaniteEditorModule"

void FNiagaraNaniteEditorModule::StartupModule()
{
	FNiagaraEditorModule& NiagaraEditorModule = FNiagaraEditorModule::Get();
	NiagaraEditorModule.RegisterRendererCreationInfo(
		FNiagaraRendererCreationInfo(
			UNiagaraNaniteRendererProperties::StaticClass()->GetDisplayNameText(),
			FText::FromString(UNiagaraNaniteRendererProperties::StaticClass()->GetDescription()),
			UNiagaraNaniteRendererProperties::StaticClass()->GetClassPathName(),
			FNiagaraRendererCreationInfo::FRendererFactory::CreateLambda(
				[](UObject* OuterEmitter)
				{
					UNiagaraNaniteRendererProperties* NewRenderer = NewObject<UNiagaraNaniteRendererProperties>(OuterEmitter, NAME_None, RF_Transactional);
					// we have an empty entry in the constructor. Due to CDO default value propagation being unwanted, we have to keep it in there
					if (ensure(NewRenderer->Meshes.Num() == 0))
					{
						const UNiagaraEditorSettings* NiagaraEditorSettings = GetDefault<UNiagaraEditorSettings>();
						NewRenderer->Meshes.AddDefaulted();
						NewRenderer->Meshes[0].Mesh = Cast<UStaticMesh>(NiagaraEditorSettings->DefaultMeshRendererMesh.TryLoad());
					}
					return NewRenderer;
				}
			)
		)
	);


	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout(
		FNiagaraFloatParameterComponentBinding::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FNiagaraFloatParameterComponentBindingCustomization::MakeInstance)
	);
}

void FNiagaraNaniteEditorModule::ShutdownModule()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.UnregisterCustomPropertyTypeLayout(FNiagaraFloatParameterComponentBinding::StaticStruct()->GetFName());
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FNiagaraNaniteEditorModule, NiagaraNaniteEditor)
