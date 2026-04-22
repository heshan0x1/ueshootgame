// Copyright Epic Games, Inc. All Rights Reserved.

#include "AudioMotorSimDebug.h"

#include "AudioMotorModelSlateIMDebugger.h"
#include "Features/IModularFeatures.h"
#include "IAudioMotorModelDebugger.h"

#define LOCTEXT_NAMESPACE "FAudioMotorSimDebugModule"

void FAudioMotorSimDebugModule::StartupModule()
{
	SlateIMDebugger = MakeUnique<FAudioMotorModelSlateIMDebugger>();
	IModularFeatures::Get().RegisterModularFeature(AudioMotorModelDebugger::DebuggerModularFeatureName, SlateIMDebugger.Get());
}

void FAudioMotorSimDebugModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(AudioMotorModelDebugger::DebuggerModularFeatureName, SlateIMDebugger.Get());
	SlateIMDebugger.Reset();
}

#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FAudioMotorSimDebugModule, AudioMotorSimDebug)