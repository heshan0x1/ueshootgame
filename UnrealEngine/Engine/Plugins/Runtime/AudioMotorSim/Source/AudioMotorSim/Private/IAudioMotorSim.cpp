// Copyright Epic Games, Inc. All Rights Reserved.

#include "IAudioMotorSim.h"

#include "AudioMotorSimConfigData.h"
#include "Features/IModularFeatures.h"
#include "HAL/IConsoleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IAudioMotorSim)

namespace AudioMotorSim_CVars
{
	bool bEnableDebugDataCollection = false;
	FAutoConsoleVariableRef CVarEnableDebugDataCollection(
		TEXT("Fort.VehicleAudio.MotorSimDebugDataCollection"),
		bEnableDebugDataCollection,
		TEXT("When true, signals to collect data for debuggers"),
		ECVF_Default);
}

UAudioMotorSim::UAudioMotorSim(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UAudioMotorSimComponent::UAudioMotorSimComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UAudioMotorSimComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UAudioMotorSimComponent::Init()
{
	static const FName UpdateFunctionName(TEXT("BP_Update"));
	if (!GetClass()->IsFunctionImplementedInScript(UpdateFunctionName))
	{
		bUpdateImplemented = false;
	}
	
#if !UE_BUILD_SHIPPING
	MotorModelDebuggers = IModularFeatures::Get().GetModularFeatureImplementations<IAudioMotorModelDebugger>(AudioMotorModelDebugger::DebuggerModularFeatureName);
	CachedDebugData.InitializeAs<FAudioMotorSimDebugDataBase>();
	ensure(CachedDebugData.IsValid());
#endif
}

void UAudioMotorSimComponent::Update(FAudioMotorSimInputContext& Input, FAudioMotorSimRuntimeContext& RuntimeInfo)
{
	QUICK_SCOPE_CYCLE_COUNTER(UAudioMotorSimComponent_Update);

	if (bEnabled && bUpdateImplemented)
	{
		BP_Update(Input, RuntimeInfo);
	}

#if WITH_EDITORONLY_DATA
	CachedInput = Input;
	CachedRuntimeInfo = RuntimeInfo;
#endif

#if !UE_BUILD_SHIPPING
	bCollectDebugData = AudioMotorSim_CVars::bEnableDebugDataCollection;
	if(bCollectDebugData)
	{
		SendCachedDebugData();
	}
#endif
}

void UAudioMotorSimComponent::Reset()
{
	BP_Reset();

#if WITH_EDITORONLY_DATA
	CachedInput = FAudioMotorSimInputContext();
	CachedRuntimeInfo = FAudioMotorSimRuntimeContext();
#endif
}

void UAudioMotorSimComponent::SetEnabled(bool bNewEnabled)
{
	bEnabled = bNewEnabled;
}

void UAudioMotorSimComponent::ConfigMotorSim(const FInstancedStruct& InConfigData)
{
	IAudioMotorSim::ConfigMotorSim(InConfigData);

	ensureMsgf(InConfigData.GetPtr<FAudioMotorSimConfigData>(), TEXT("Expected instance struct of being a FAudioMotorSimConfigData type"));
}

#if WITH_EDITORONLY_DATA
void UAudioMotorSimComponent::GetCachedData(FAudioMotorSimInputContext& OutInput, FAudioMotorSimRuntimeContext& OutRuntimeInfo)
{
	OutInput = CachedInput;
	OutRuntimeInfo = CachedRuntimeInfo;
}
#endif

#if !UE_BUILD_SHIPPING
bool UAudioMotorSimComponent::ShouldCollectDebugData() const
{
	return bCollectDebugData;
}

void UAudioMotorSimComponent::SendCachedDebugData()
{ 
	if(!CachedDebugData.IsValid())
	{
		return;
	}
	
	for(IAudioMotorModelDebugger* Debugger : MotorModelDebuggers)
	{
		if(Debugger)
		{		
			Debugger->SendAdditionalDebugData(this, CachedDebugData);
		}
	}
}
#endif