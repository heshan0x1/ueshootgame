// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/EditorDomainSaveCommandlet.h"

#include "EditorDomainSave.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EditorDomainSaveCommandlet)

UEditorDomainSaveCommandlet::UEditorDomainSaveCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UEditorDomainSaveCommandlet::Main(const FString& CmdLineParams)
{
	FEditorDomainSaveServer Server;
	return Server.Run();
}
