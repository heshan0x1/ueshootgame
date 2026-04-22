// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AudioMotorModelComponent.h"
#include "AudioMotorModelSlateIMWindow.h"
#include "IAudioMotorModelDebugger.h"

class FAudioMotorModelSlateIMDebugger : public IAudioMotorModelDebugger
{
public:
	FAudioMotorModelSlateIMDebugger();

	virtual void RegisterComponentWithDebugger(UAudioMotorModelComponent* MotorModelComponent) override;
	virtual void SendAdditionalDebugData(UObject* Object, const FInstancedStruct& InData) override;

private:
	FAudioMotorModelSlateIMWindow SlateIMWindow;
	
#if WITH_ENGINE                                                                      
	FAudioMotorModelSlateIMViewportWidget SlateIMViewportWidget;
#endif
};