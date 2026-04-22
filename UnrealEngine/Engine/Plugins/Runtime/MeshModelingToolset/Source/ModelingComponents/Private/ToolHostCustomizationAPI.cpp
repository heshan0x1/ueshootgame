// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToolHostCustomizationAPI.h"

#include "ContextObjectStore.h"
#include "InteractiveToolManager.h"
#include "UObject/Object.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ToolHostCustomizationAPI)

TScriptInterface<IToolHostCustomizationAPI> IToolHostCustomizationAPI::Find(UInteractiveToolManager* ToolManager)
{
	if (!ensure(ToolManager))
	{
		return nullptr;
	}

	UContextObjectStore* ContextObjectStore = ToolManager->GetContextObjectStore();
	if (!ensure(ContextObjectStore))
	{
		return nullptr;
	}

	return ContextObjectStore->FindContextByClass(UToolHostCustomizationAPI::StaticClass());
}
