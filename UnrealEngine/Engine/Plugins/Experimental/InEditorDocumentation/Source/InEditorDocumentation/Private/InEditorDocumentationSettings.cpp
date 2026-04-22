// Copyright Epic Games, Inc. All Rights Reserved.


#include "InEditorDocumentationSettings.h"

#if WITH_EDITOR
void UInEditorDocumentationSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SaveConfig();
}
#endif // WITH_EDITOR
