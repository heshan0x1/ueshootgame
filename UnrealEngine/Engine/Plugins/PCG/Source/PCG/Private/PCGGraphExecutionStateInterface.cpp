// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGGraphExecutionStateInterface.h"

#include "PCGModule.h"
#include "Editor/IPCGEditorModule.h"
#include "Subsystems/PCGSubsystem.h"
#include "Subsystems/PCGEngineSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGGraphExecutionStateInterface)

void IPCGGraphExecutionState::AddToManagedResources(UPCGManagedResource* InResource)
{
	UE_LOG(LogPCG, Error, TEXT("This execution source (%s) does not support managed resources."), *GetDebugName());
}

IPCGBaseSubsystem* IPCGGraphExecutionState::GetSubsystem() const
{
	IPCGBaseSubsystem* Subsystem = nullptr;
	
	if (UWorld* World = GetWorld())
	{
		Subsystem = UPCGSubsystem::GetInstance(World);
	}
	else
	{
		Subsystem = UPCGEngineSubsystem::Get();
	}

	return Subsystem;
}

FPCGGridDescriptor IPCGGraphExecutionState::GetGridDescriptor(uint32 InGridSize) const
{
	return FPCGGridDescriptor()
		.SetGridSize(InGridSize)
		.SetIs2DGrid(Use2DGrid())
		.SetIsRuntime(IsManagedByRuntimeGenSystem());
}

#if WITH_EDITOR

FPCGDynamicTrackingPriority IPCGGraphExecutionState::GetDynamicTrackingPriority() const
{
	ensureMsgf(false, TEXT("Please implement IPCGGraphExecutionState::GetDynamicTrackingPriority"));
	return FPCGDynamicTrackingPriority();
}

#endif

//////////////////////////////////////////////////////////////////////

UPCGGraphExecutionSource::UPCGGraphExecutionSource(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

//////////////////////////////////////////////////////////////////////

FPCGSoftGraphExecutionSource::FPCGSoftGraphExecutionSource(const TSoftObjectPtr<UObject>& InSoftObjectPtr)
	: SoftObjectPtr(InSoftObjectPtr)
{
}

FPCGSoftGraphExecutionSource::FPCGSoftGraphExecutionSource(IPCGGraphExecutionSource* InSource)
	: SoftObjectPtr(Cast<UObject>(InSource))
{
}

FPCGSoftGraphExecutionSource::FPCGSoftGraphExecutionSource(const IPCGGraphExecutionSource* InSource)
	: FPCGSoftGraphExecutionSource(const_cast<IPCGGraphExecutionSource*>(InSource))
{
}

FPCGSoftGraphExecutionSource::FPCGSoftGraphExecutionSource(const FPCGSoftGraphExecutionSource& Other)
{
	SoftObjectPtr = Other.SoftObjectPtr;
}

FPCGSoftGraphExecutionSource::FPCGSoftGraphExecutionSource(FPCGSoftGraphExecutionSource&& Other)
{
	SoftObjectPtr = std::move(Other.SoftObjectPtr);
}

FPCGSoftGraphExecutionSource& FPCGSoftGraphExecutionSource::operator=(const FPCGSoftGraphExecutionSource& Other)
{
	Reset();

	SoftObjectPtr = Other.SoftObjectPtr;
	return *this;
}

FPCGSoftGraphExecutionSource& FPCGSoftGraphExecutionSource::operator=(FPCGSoftGraphExecutionSource&& Other)
{
	Reset();

	SoftObjectPtr = std::move(Other.SoftObjectPtr);
	return *this;
}

FPCGSoftGraphExecutionSource& FPCGSoftGraphExecutionSource::operator=(const IPCGGraphExecutionSource* InSource)
{
	Reset();

	*this = FPCGSoftGraphExecutionSource(InSource);
	return *this;
}

IPCGGraphExecutionSource* FPCGSoftGraphExecutionSource::Get() const
{
	if (!CachedWeakPtr.IsValid())
	{
		FGCScopeGuard ScopeGuard;
		
		UObject* Object = SoftObjectPtr.Get();
		if (!Object || !Object->Implements<UPCGGraphExecutionSource>())
		{
			return nullptr;
		}

		IPCGGraphExecutionSource* InterfacePtr = CastChecked<IPCGGraphExecutionSource>(Object);
		
		{
			UE::TScopeLock Lock(SpinLock);
			if (!CachedWeakPtr.IsValid())
			{
				CachedWeakPtr = CastChecked<IPCGGraphExecutionSource>(Object);
			}
		}
	}

	return CachedWeakPtr.Get();
}

UObject* FPCGSoftGraphExecutionSource::GetObject() const
{
	return SoftObjectPtr.Get();
}

void FPCGSoftGraphExecutionSource::Reset()
{
	SoftObjectPtr = nullptr;
	if (CachedWeakPtr.IsValid())
	{
		UE::TScopeLock Lock(SpinLock);
		CachedWeakPtr.Reset();
	}
}

bool FPCGSoftGraphExecutionSource::operator==(const FPCGSoftGraphExecutionSource& Other) const
{
	return Other.SoftObjectPtr == SoftObjectPtr;
}

int32 GetTypeHash(const FPCGSoftGraphExecutionSource& This)
{
	return GetTypeHash(This.SoftObjectPtr);
}
