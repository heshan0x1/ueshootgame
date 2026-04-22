// Copyright Epic Games, Inc. All Rights Reserved.


#include "AIAssistantSubsystem.h"

#include "Containers/UnrealString.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"

#include "AIAssistant.h"
#include "AIAssistantPythonExecutor.h"
#include "AIAssistantWebBrowser.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AIAssistantSubsystem)


using namespace UE::AIAssistant;
using namespace UE::AIAssistant::PythonExecutor;

//
// Statics.
//

FAIAssistantModule& UAIAssistantSubsystem::GetAIAssistantModule()
{
	return FModuleManager::LoadModuleChecked<FAIAssistantModule>(UE_PLUGIN_NAME);
}

// Get the assistant web browser.
TSharedPtr<SAIAssistantWebBrowser> UAIAssistantSubsystem::GetAIAssistantWebBrowserWidget()
{
	auto AIAssistantWebBrowserWidget = GetAIAssistantModule().GetAIAssistantWebBrowserWidget();
	check(AIAssistantWebBrowserWidget.IsValid());
	return AIAssistantWebBrowserWidget;
}

//
// UAIAssistantSubsystem
//

void UAIAssistantSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAIAssistantSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

/*no:static*/ FString UAIAssistantSubsystem::ExecutePythonScriptViaJavaScript(const FString& Code)
{
	FString CodeOutput;
	ExecutePythonScript(Code, &CodeOutput);

	if (CodeOutput.IsEmpty())
	{
		CodeOutput = TEXT("Code executed successfully.");
	}
	
	return CodeOutput;
}


/*no:static*/ void UAIAssistantSubsystem::ShowContextMenuViaJavaScript(const FString& SelectedString, const int32 ClientX, const int32 ClientY) const
{
	GetAIAssistantModule().ShowContextMenu(SelectedString, FVector2f(ClientX, ClientY));
}

