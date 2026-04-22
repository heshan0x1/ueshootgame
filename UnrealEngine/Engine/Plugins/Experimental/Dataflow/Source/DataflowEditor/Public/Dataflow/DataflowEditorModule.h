// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseCharacterFXEditorModule.h"

#define UE_API DATAFLOWEDITOR_API


class FDataflowSNodeFactory;

/**
 * The public interface to this module
 */
class FDataflowEditorModule : public FBaseCharacterFXEditorModule
{
public:

	static UE_API const FColor SurfaceColor;

	/** IModuleInterface implementation */
	UE_API virtual void StartupModule();
	UE_API virtual void ShutdownModule();

private:

	TSharedPtr<FDataflowSNodeFactory> DataflowSNodeFactory;

};

#undef UE_API
