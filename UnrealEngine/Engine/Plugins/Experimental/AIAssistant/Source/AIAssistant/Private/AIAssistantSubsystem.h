// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "EditorSubsystem.h"
#include "Templates/SharedPointer.h"

#include "AIAssistantSubsystem.generated.h"


class FAIAssistantModule;
class SAIAssistantWebBrowser;

//
// UAIAssistantSubsystem
//

UCLASS(BlueprintType)
class AIASSISTANT_API UAIAssistantSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	// UEditorSubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	//
	// JavaScript exposed functions.
	//

	// See NOTE_JAVASCRIPT_CPP_FUNCTIONS in C++ code for how to call this from JavaScript.
	UFUNCTION(BlueprintCallable, Category="JavaScript")
	FString ExecutePythonScriptViaJavaScript(const FString& Code);
	
	// See NOTE_JAVASCRIPT_CPP_FUNCTIONS in C++ code for how to call this from JavaScript.
	UFUNCTION(BlueprintCallable, Category="JavaScript")
	void ShowContextMenuViaJavaScript(const FString& SelectedString, const int32 ClientX, const int32 ClientY) const;

public:
	// Get the assistant module.
	static FAIAssistantModule& GetAIAssistantModule();
	// Get the assistant web browser.
	static TSharedPtr<SAIAssistantWebBrowser> GetAIAssistantWebBrowserWidget();
};
