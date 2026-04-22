// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMotorModelSlateIMDebugger.h"

FAudioMotorModelSlateIMDebugger::FAudioMotorModelSlateIMDebugger()
	: SlateIMWindow(TEXT("Fort.VehicleAudio.ToggleMotorModelDebuggerWindow"), TEXT(""))
#if WITH_ENGINE    
	, SlateIMViewportWidget(TEXT("Fort.VehicleAudio.ToggleMotorModelDebuggerViewport"), TEXT(""))
#endif
{
}

void FAudioMotorModelSlateIMDebugger::RegisterComponentWithDebugger(UAudioMotorModelComponent* Component)
{
	SlateIMWindow.DebugWidget.RegisterMotorModelComponent(Component);
	SlateIMViewportWidget.DebugWidget.RegisterMotorModelComponent(Component);
}

void FAudioMotorModelSlateIMDebugger::SendAdditionalDebugData(UObject* Object, const FInstancedStruct& InData)
{
	SlateIMWindow.DebugWidget.SendAdditionalDebugData(Object, InData);
	SlateIMViewportWidget.DebugWidget.SendAdditionalDebugData(Object, InData);
}
