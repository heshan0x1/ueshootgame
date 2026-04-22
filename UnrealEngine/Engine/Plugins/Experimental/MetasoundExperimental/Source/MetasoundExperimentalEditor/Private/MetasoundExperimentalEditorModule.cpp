// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEditorModule.h"
#include "MetasoundExampleNodeConfiguration.h"
#include "MetasoundExampleNodeDetailsCustomization.h"
#include "MetasoundMappingFunctionNode.h"
#include "MetasoundMappingFunctionDetailsCustomization.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "MetasoundExperimentalEditor"

class FMetasoundExperimentalEditorModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface API
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface API
};

void FMetasoundExperimentalEditorModule::StartupModule()
{
	Metasound::Editor::IMetasoundEditorModule& MetasoundEditorModule = FModuleManager::GetModuleChecked<Metasound::Editor::IMetasoundEditorModule>("MetasoundEditor");
	MetasoundEditorModule.RegisterCustomNodeConfigurationDetailsCustomization(
		FMetaSoundWidgetExampleNodeConfiguration::StaticStruct()->GetFName(),
		[](TSharedPtr<IPropertyHandle> InStructProperty, TWeakObjectPtr<UMetasoundEditorGraphNode> InNode)
		{
			return MakeShareable(new FExampleWidgetNodeConfigurationCustomization(InStructProperty, InNode));
		});
	MetasoundEditorModule.RegisterCustomNodeConfigurationDetailsCustomization(
		FMetaSoundMappingFunctionNodeConfiguration::StaticStruct()->GetFName(),
		[](TSharedPtr<IPropertyHandle> InStructProperty, TWeakObjectPtr<UMetasoundEditorGraphNode> InNode)
		{
			return MakeShareable(new FMappingFunctionNodeConfigurationCustomization(InStructProperty, InNode));
		});
	MetasoundEditorModule.RegisterPinType("Enum:CurveFunction", "Int32");
}

void FMetasoundExperimentalEditorModule::ShutdownModule()
{
	Metasound::Editor::IMetasoundEditorModule& MetasoundEditorModule = FModuleManager::GetModuleChecked<Metasound::Editor::IMetasoundEditorModule>("MetasoundEditor");
	MetasoundEditorModule.UnregisterCustomNodeConfigurationDetailsCustomization(FMetaSoundWidgetExampleNodeConfiguration::StaticStruct()->GetFName());
	MetasoundEditorModule.UnregisterCustomNodeConfigurationDetailsCustomization(FMetaSoundMappingFunctionNodeConfiguration::StaticStruct()->GetFName());
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMetasoundExperimentalEditorModule, MetasoundExperimentalEditor)