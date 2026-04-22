// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGCommon.h"
#include "PCGComponent.h"
#include "PCGSubsystem.h"
#include "PCGWorldActor.h"

#include "EngineDefines.h" // For UE_ENABLE_DEBUG_DRAWING

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGCommon)

namespace PCGLog
{
	FString GetComponentOwnerName(const UPCGComponent* InComponent, bool bUseLabel, FString InDefaultName)
	{
		if (InComponent && InComponent->GetOwner())
		{
			if (IsRunningCommandlet())
			{
				return InComponent->GetOwner()->GetPathName();
			}
			else if(bUseLabel)
			{
				return InComponent->GetOwner()->GetActorNameOrLabel();
			}
			else
			{
				return InComponent->GetOwner()->GetName();
			}
		}

		return InDefaultName;
	}

	FString GetExecutionSourceName(const IPCGGraphExecutionSource* InExecutionSource, bool bUseLabel, FString InDefaultSource)
	{
		const UPCGComponent* PCGComponent = Cast<UPCGComponent>(InExecutionSource);
		if (PCGComponent)
		{
			return GetComponentOwnerName(PCGComponent, bUseLabel, InDefaultSource);
		}
		else if (InExecutionSource)
		{
			return InExecutionSource->GetExecutionState().GetDebugName();
		}
		else
		{
			return InDefaultSource;
		}
	}
}

namespace PCGFeatureSwitches
{
	TAutoConsoleVariable<bool> CVarCheckSamplerMemory{
		TEXT("pcg.CheckSamplerMemory"),
		true,
		TEXT("Checks expected memory size consumption prior to performing sampling operations")
	};

	TAutoConsoleVariable<float> CVarSamplerMemoryThreshold{
		TEXT("pcg.SamplerMemoryThreshold"),
		0.8f,
		TEXT("Normalized threshold of remaining physical memory required to abort sampling operation."),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
		{
			if (InVariable->GetFloat() < 0.f || InVariable->GetFloat() > 1.0)
			{
				InVariable->SetWithCurrentPriority(FMath::Clamp(InVariable->GetFloat(), 0.f, 1.f));
			}
		})
	};

	namespace Helpers
	{
		uint64 GetAvailableMemoryForSamplers()
		{
			const FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
			// Also uses AvailableVirtual because the system might have plenty of physical memory but still be limited by virtual memory available in some cases.
			// (i.e. per-process quota, paging file size lower than actual memory available, etc.).
			return CVarSamplerMemoryThreshold.GetValueOnAnyThread() * FMath::Min(MemoryStats.AvailablePhysical, MemoryStats.AvailableVirtual);
		}
	}
}

namespace PCGSystemSwitches
{
#if WITH_EDITOR
	TAutoConsoleVariable<bool> CVarPausePCGExecution(
		TEXT("pcg.PauseExecution"),
		false,
		TEXT("Pauses all execution of PCG but does not cancel tasks."));

	TAutoConsoleVariable<bool> CVarGlobalDisableRefresh(
		TEXT("pcg.GlobalDisableRefresh"),
		false,
		TEXT("Disable refresh for all PCG Components."));

	TAutoConsoleVariable<bool> CVarDirtyLoadAsPreviewOnLoad(
		TEXT("pcg.DirtyLoadAsPreviewOnLoad"),
		false,
		TEXT("Enables dirtying on load for load as preview components.\nTurning off this option will require to force generate or apply a change before this component is regenerated."));

	TAutoConsoleVariable<bool> CVarForceDynamicGraphDispatch(
		TEXT("pcg.GraphCompilation.ForceDynamicDispatch"),
		false,
		TEXT("Forces all subgraph executions to be dynamic. Performance warning, also requires to flush all compiled graphs."));
#endif

	TAutoConsoleVariable<bool> CVarReleaseTransientResourcesEarly(
		TEXT("pcg.ReleaseTransientResourcesEarly"),
		true,
		TEXT("Aggressively release transient (esp. GPU) data when possible."));

#if UE_ENABLE_DEBUG_DRAWING
	TAutoConsoleVariable<bool> CVarPCGDebugDrawGeneratedCells(
		TEXT("pcg.GraphExecution.DebugDrawGeneratedCells"),
		false,
		TEXT("Draws debug boxes around any cell that is executing, colored by grid level, and in PIE/standalone labels grid size and coords."));
#endif // UE_ENABLE_DEBUG_DRAWING

#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<bool> CVarFuzzGPUMemory(
		TEXT("pcg.GPU.FuzzMemory"),
		false,
		TEXT("Initializes GPU buffers with random numbers which helps to reproduce undefined behaviour from unitialized memory."));
#endif //!UE_BUILD_SHIPPING

	TAutoConsoleVariable<bool> CVarPassGPUDataThroughGridLinks(
		TEXT("pcg.Graph.GPU.PassGPUDataThroughGridLinks"),
		true,
		TEXT("Whether proxies for GPU data are cached in per pin output data and passed through grid links. If false data is read back to CPU."));
}

namespace PCGHiGenGrid
{
	bool IsValidGridSize(uint32 InGridSize)
	{
		// Must be a power of 2 (in m) and within the valid range
		// TODO: support other units
		if (FMath::IsPowerOfTwo(InGridSize / 100)
			&& InGridSize >= GridToGridSize(EPCGHiGenGrid::GridMin)
			&& InGridSize <= GridToGridSize(EPCGHiGenGrid::GridMax))
		{
			return true;
		}

		UPCGSubsystem* PCGSubsystem = UPCGSubsystem::GetSubsystemForCurrentWorld();
		APCGWorldActor* PCGWorldActor = PCGSubsystem ? PCGSubsystem->GetPCGWorldActor() : nullptr;

		return !PCGWorldActor || PCGWorldActor->PartitionGridSize == InGridSize;
	}

	bool IsValidGrid(EPCGHiGenGrid InGrid)
	{
		// Check the bitmask value is within range
		return InGrid >= EPCGHiGenGrid::GridMin && static_cast<uint32>(InGrid) < 2 * static_cast<uint32>(EPCGHiGenGrid::GridMax);
	}

	bool IsValidGridOrUninitialized(EPCGHiGenGrid InGrid)
	{
		return IsValidGrid(InGrid) || InGrid == EPCGHiGenGrid::Uninitialized;
	}

	uint32 GridToGridSize(EPCGHiGenGrid InGrid)
	{
		const uint32 GridAsUint = static_cast<uint32>(InGrid);
		ensure(FMath::IsPowerOfTwo(GridAsUint));
		// TODO: support other units
		return IsValidGrid(InGrid) ? (GridAsUint * 100) : UninitializedGridSize();
	}

	EPCGHiGenGrid GridSizeToGrid(uint32 InGridSize)
	{
		if (InGridSize == UnboundedGridSize())
		{
			return EPCGHiGenGrid::Unbounded;
		}
		else if (InGridSize <= GridToGridSize(EPCGHiGenGrid::GridMin))
		{
			return EPCGHiGenGrid::GridMin;
		}
		else if (InGridSize >= GridToGridSize(EPCGHiGenGrid::GridMax))
		{
			return EPCGHiGenGrid::GridMax;
		}

		// Grid size is in cm, but EPCGHiGenGrid is in meters so we convert the units before rounding.
		const uint32 RoundedGridSizeMeters = FMath::RoundUpToPowerOfTwo(InGridSize / 100);
		return static_cast<EPCGHiGenGrid>(RoundedGridSizeMeters);
	}

	uint32 UninitializedGridSize()
	{
		return static_cast<uint32>(EPCGHiGenGrid::Uninitialized);
	}

	uint32 UnboundedGridSize()
	{
		return static_cast<uint32>(EPCGHiGenGrid::Unbounded);
	}
}

double FPCGRuntimeGenerationRadii::GetGenerationRadiusFromGrid(EPCGHiGenGrid Grid) const
{
	switch (Grid)
	{
		case EPCGHiGenGrid::Grid4: return GenerationRadius400;
		case EPCGHiGenGrid::Grid8: return GenerationRadius800;
		case EPCGHiGenGrid::Grid16: return GenerationRadius1600;
		case EPCGHiGenGrid::Grid32: return GenerationRadius3200;
		case EPCGHiGenGrid::Grid64: return GenerationRadius6400;
		case EPCGHiGenGrid::Grid128: return GenerationRadius12800;
		case EPCGHiGenGrid::Grid256: return GenerationRadius25600;
		case EPCGHiGenGrid::Grid512: return GenerationRadius51200;
		case EPCGHiGenGrid::Grid1024: return GenerationRadius102400;
		case EPCGHiGenGrid::Grid2048: return GenerationRadius204800;
		case EPCGHiGenGrid::Grid4096: return GenerationRadius204800 * (1 << 1);
		case EPCGHiGenGrid::Grid8192: return GenerationRadius204800 * (1 << 2);
		case EPCGHiGenGrid::Grid16384: return GenerationRadius204800 * (1 << 3);
		case EPCGHiGenGrid::Grid32768: return GenerationRadius204800 * (1 << 4);
		case EPCGHiGenGrid::Grid65536: return GenerationRadius204800 * (1 << 5);
		case EPCGHiGenGrid::Grid131072: return GenerationRadius204800 * (1 << 6);
		case EPCGHiGenGrid::Grid262144: return GenerationRadius204800 * (1 << 7);
		case EPCGHiGenGrid::Grid524288: return GenerationRadius204800 * (1 << 8);
		case EPCGHiGenGrid::Grid1048576: return GenerationRadius204800 * (1 << 9);
		case EPCGHiGenGrid::Grid2097152: return GenerationRadius204800 * (1 << 10);
		case EPCGHiGenGrid::Grid4194304: return GenerationRadius204800 * (1 << 11);
		case EPCGHiGenGrid::Unbounded: return GenerationRadius;
	}

	ensure(false);
	return 0;
}

double FPCGRuntimeGenerationRadii::GetCleanupRadiusFromGrid(EPCGHiGenGrid Grid) const
{
	return GetGenerationRadiusFromGrid(Grid) * CleanupRadiusMultiplier;
}

bool FPCGRuntimeGenerationRadii::operator==(const FPCGRuntimeGenerationRadii& Other) const
{
	return GenerationRadius == Other.GenerationRadius
		&& GenerationRadius400 == Other.GenerationRadius400
		&& GenerationRadius800 == Other.GenerationRadius800
		&& GenerationRadius1600 == Other.GenerationRadius1600
		&& GenerationRadius3200 == Other.GenerationRadius3200
		&& GenerationRadius6400 == Other.GenerationRadius6400
		&& GenerationRadius12800 == Other.GenerationRadius12800
		&& GenerationRadius25600 == Other.GenerationRadius25600
		&& GenerationRadius51200 == Other.GenerationRadius51200
		&& GenerationRadius102400 == Other.GenerationRadius102400
		&& GenerationRadius204800 == Other.GenerationRadius204800
		&& CleanupRadiusMultiplier == Other.CleanupRadiusMultiplier;
}

uint32 GetTypeHash(const FPCGRuntimeGenerationRadii& InGenerationRadii)
{
	uint32 Hash = 0;

	Hash = HashCombine(Hash, GetTypeHash(InGenerationRadii.GenerationRadius));
	Hash = HashCombine(Hash, GetTypeHash(InGenerationRadii.GenerationRadius400));
	Hash = HashCombine(Hash, GetTypeHash(InGenerationRadii.GenerationRadius800));
	Hash = HashCombine(Hash, GetTypeHash(InGenerationRadii.GenerationRadius1600));
	Hash = HashCombine(Hash, GetTypeHash(InGenerationRadii.GenerationRadius3200));
	Hash = HashCombine(Hash, GetTypeHash(InGenerationRadii.GenerationRadius6400));
	Hash = HashCombine(Hash, GetTypeHash(InGenerationRadii.GenerationRadius12800));
	Hash = HashCombine(Hash, GetTypeHash(InGenerationRadii.GenerationRadius25600));
	Hash = HashCombine(Hash, GetTypeHash(InGenerationRadii.GenerationRadius51200));
	Hash = HashCombine(Hash, GetTypeHash(InGenerationRadii.GenerationRadius102400));
	Hash = HashCombine(Hash, GetTypeHash(InGenerationRadii.GenerationRadius204800));
	Hash = HashCombine(Hash, GetTypeHash(InGenerationRadii.CleanupRadiusMultiplier));

	return Hash;
}

namespace PCGDelegates
{
#if WITH_EDITOR
	FOnInstanceLayoutChanged OnInstancedPropertyBagLayoutChanged;
#endif
} // PCGDelegates

namespace PCGPinIdHelpers
{
	FPCGPinId NodeIdAndPinIndexToPinId(FPCGTaskId NodeId, uint64 PinIndex)
	{
		// Construct a unique ID from node index and pin index.
		ensure(PinIndex < PCGPinIdHelpers::MaxOutputPins);
		return (NodeId * PCGPinIdHelpers::PinActiveBitmaskSize) + (PinIndex % PCGPinIdHelpers::PinActiveBitmaskSize);
	}

	FPCGPinId NodeIdToPinId(FPCGTaskId NodeId)
	{
		// Use (max pins - 1) to make a special pin ID that is not associated to a specific node pin.
		return (NodeId * PCGPinIdHelpers::PinActiveBitmaskSize) + PCGPinIdHelpers::MaxOutputPins;
	}

	FPCGPinId OffsetNodeIdInPinId(FPCGPinId PinId, uint64 NodeIDOffset)
	{
		return NodeIDOffset * PCGPinIdHelpers::PinActiveBitmaskSize + PinId;
	}

	FPCGTaskId GetNodeIdFromPinId(FPCGPinId PinId)
	{
		return PinId / PCGPinIdHelpers::PinActiveBitmaskSize;
	}

	uint64 GetPinIndexFromPinId(FPCGPinId PinId)
	{
		return PinId % PCGPinIdHelpers::PinActiveBitmaskSize;
	}
}

namespace PCGPointCustomPropertyNames
{
	bool IsCustomPropertyName(FName Name)
	{
		return Name == PCGPointCustomPropertyNames::ExtentsName ||
			Name == PCGPointCustomPropertyNames::LocalCenterName ||
			Name == PCGPointCustomPropertyNames::PositionName ||
			Name == PCGPointCustomPropertyNames::RotationName ||
			Name == PCGPointCustomPropertyNames::ScaleName ||
			Name == PCGPointCustomPropertyNames::LocalSizeName ||
			Name == PCGPointCustomPropertyNames::ScaledLocalSizeName;
	}
}

namespace PCGQualityHelpers
{
	FName GetQualityPinLabel()
	{
		const int32 QualityLevel = UPCGSubsystem::GetPCGQualityLevel();
		FName SelectedPinLabel = NAME_None;

		switch (QualityLevel)
		{
			case 0: // Low Quality
				SelectedPinLabel = PinLabelLow;
				break;
			case 1: // Medium Quality
				SelectedPinLabel = PinLabelMedium;
				break;
			case 2: // High Quality
				SelectedPinLabel = PinLabelHigh;
				break;
			case 3: // Epic Quality
				SelectedPinLabel = PinLabelEpic;
				break;
			case 4: // Cinematic Quality
				SelectedPinLabel = PinLabelCinematic;
				break;
			default: // Default to Low Quality if we don't have a valid quality level
				SelectedPinLabel = PinLabelDefault;
		}

		return SelectedPinLabel;
	}
}

FPCGStackHandle::FPCGStackHandle(TSharedPtr<const FPCGStackContext> InStackContext, int32 InStackIndex)
	: StackContext(InStackContext)
	, StackIndex(InStackIndex)
{
}

bool FPCGStackHandle::IsValid() const
{
	return StackContext && StackIndex >= 0 && StackIndex < StackContext->GetNumStacks();
}

const FPCGStack* FPCGStackHandle::GetStack() const
{
	return IsValid() ? StackContext->GetStack(StackIndex) : nullptr;
}
