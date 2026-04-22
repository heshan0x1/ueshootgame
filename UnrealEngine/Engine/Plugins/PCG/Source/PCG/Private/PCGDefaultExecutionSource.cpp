// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGDefaultExecutionSource.h"

#include "PCGSubgraph.h"
#include "Subsystems/IPCGBaseSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGDefaultExecutionSource)

UPCGData* FPCGDefaultExecutionState::GetSelfData() const
{
	return nullptr;
}

int32 FPCGDefaultExecutionState::GetSeed() const
{
	check(Source);
	return Source->Seed;
}

FString FPCGDefaultExecutionState::GetDebugName() const
{
	return TEXT("PCGDefaultExecutionSource");
}

FTransform FPCGDefaultExecutionState::GetTransform() const
{
	return FTransform::Identity;
}

UWorld* FPCGDefaultExecutionState::GetWorld() const
{
	return nullptr;
}

bool FPCGDefaultExecutionState::HasAuthority() const
{
	return false;
}

FBox FPCGDefaultExecutionState::GetBounds() const
{
	return FBox(-FVector::One(), FVector::One());
}

UPCGGraph* FPCGDefaultExecutionState::GetGraph() const
{
	check(Source);
	return Source->GetGraph();
}

UPCGGraphInstance* FPCGDefaultExecutionState::GetGraphInstance() const
{
	check(Source);
	return Source->GetGraphInstance();
}

void FPCGDefaultExecutionState::OnGraphExecutionAborted(bool bQuiet, bool bCleanupUnusedResources)
{
	Source->CurrentGenerationTask = InvalidPCGTaskId;
#if WITH_EDITOR
	if (IPCGBaseSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->OnPCGSourceGenerationDone(Source, EPCGGenerationStatus::Aborted);
	}
#endif
}

void FPCGDefaultExecutionState::Cancel()
{
	if (Source->CurrentGenerationTask == InvalidPCGTaskId)
	{
		return;
	}

	if (IPCGBaseSubsystem* Subsystem = GetSubsystem())
	{
		Subsystem->CancelGeneration(Source);
	}
}

bool FPCGDefaultExecutionState::IsGenerating() const
{
	check(Source);
	return Source->CurrentGenerationTask != InvalidPCGTaskId;
}

IPCGGraphExecutionSource* FPCGDefaultExecutionState::GetOriginalSource() const
{
	// Partitioned generation not supported.
	return Source;
}

#if WITH_EDITOR
const PCGUtils::FExtraCapture& FPCGDefaultExecutionState::GetExtraCapture() const
{
	check(Source);
	return Source->ExtraCapture;
}

PCGUtils::FExtraCapture& FPCGDefaultExecutionState::GetExtraCapture()
{
	check(Source);
	return Source->ExtraCapture;
}

const FPCGGraphExecutionInspection& FPCGDefaultExecutionState::GetInspection() const
{
	check(Source);
	return Source->Inspection;
}

FPCGGraphExecutionInspection& FPCGDefaultExecutionState::GetInspection()
{
	check(Source);
	return Source->Inspection;
}
#endif // WITH_EDITOR

void UPCGDefaultExecutionSource::SetGraphInterface(UPCGGraphInterface* InGraphInterface)
{
	if (InGraphInterface == GraphInterface)
	{
		return;
	}

#if WITH_EDITOR
	if (GraphInterface)
	{
		GraphInterface->OnGraphChangedDelegate.RemoveAll(this);
	}
#endif // WITH_EDITOR
	
	GraphInterface = InGraphInterface;

#if WITH_EDITOR
	if (GraphInterface)
	{
		GraphInterface->OnGraphChangedDelegate.AddUObject(this, &UPCGDefaultExecutionSource::OnGraphChanged);
	}
#endif // WITH_EDITOR
}

UPCGGraphInstance* UPCGDefaultExecutionSource::GetGraphInstance()
{
	return Cast<UPCGGraphInstance>(GraphInterface);
}

UPCGGraph* UPCGDefaultExecutionSource::GetGraph()
{
	return GraphInterface ? GraphInterface->GetMutablePCGGraph() : nullptr;
}

#if WITH_EDITOR
void UPCGDefaultExecutionSource::OnGraphChanged(UPCGGraphInterface* InGraphInterface, EPCGChangeType ChangeType)
{
	if (InGraphInterface != GraphInterface)
	{
		return;
	}

	if (ChangeType == EPCGChangeType::Cosmetic ||
		ChangeType == EPCGChangeType::GraphCustomization ||
		ChangeType == EPCGChangeType::None)
	{
		// If it is a cosmetic change (or no change), nothing to do
		return;
	}

	if (!InGraphInterface || !InGraphInterface->GetGraph())
	{
		return;
	}

	Generate();
}
#endif // WITH_EDITOR

void UPCGDefaultExecutionSource::Generate()
{
	if (CurrentGenerationTask != InvalidPCGTaskId)
	{
		return;
	}

	if (IPCGBaseSubsystem* Subsystem = State.GetSubsystem())
	{
		FPCGScheduleGraphParams GraphParams{
			/*InGraph=*/ GetGraph(),
			/*InExecutionSource=*/ this,
			/*InPreGraphElement=*/ MakeShared<FPCGInputForwardingElement>(FPCGDataCollection{}),
			/*InInputElement=*/ MakeShared<FPCGInputForwardingElement>(FPCGDataCollection{}),
			/*InExternalDependencies*/ {},
			/*InFromStack*/ nullptr,
			/*bInAllowHierarchicalGeneration*/ false};
		
		const FPCGTaskId GenerationTaskId = Subsystem->ScheduleGraph(GraphParams);
		if (GenerationTaskId != InvalidPCGTaskId)
		{
			FPCGScheduleGenericParams Params{
				/*InOperation=*/ [this, WeakSubsystem = TWeakInterfacePtr(Subsystem)](FPCGContext*) -> bool
				{
					CurrentGenerationTask = InvalidPCGTaskId;
					if (WeakSubsystem.IsValid())
					{
#if WITH_EDITOR
						WeakSubsystem->OnPCGSourceGenerationDone(this, EPCGGenerationStatus::Completed);
#endif // WITH_EDITOR
					}
					
					return true;
				},
				/*InExecutionSource=*/this,
				/*InExecutionDependencies=*/{GenerationTaskId},
				/*InDataDependencies=*/{},
				/*bSupportBasePointDataInput=*/true
			};
			
			CurrentGenerationTask = Subsystem->ScheduleGeneric(Params);
		}
#if WITH_EDITOR
		else
		{
			Subsystem->OnPCGSourceGenerationDone(this, EPCGGenerationStatus::Aborted);
		}
#endif
	}
}

void UPCGDefaultExecutionSource::Sunset()
{
#if WITH_EDITOR
	if (IsValid(GraphInterface))
	{
		check(GraphInterface);
		GraphInterface->OnGraphChangedDelegate.RemoveAll(this);
	}
#endif // WITH_EDITOR
}

UPCGDefaultExecutionSource::UPCGDefaultExecutionSource()
	: State(this)
{
}

UPCGDefaultExecutionSource::~UPCGDefaultExecutionSource()
{
	Sunset();
}

void UPCGDefaultExecutionSource::Initialize(const FPCGDefaultExecutionSourceParams& InParams)
{
	SetGraphInterface(InParams.GraphInterface);
	SetSeed(InParams.Seed);
}

void UPCGDefaultExecutionSource::SetSeed(int32 InSeed)
{
	Seed = InSeed;
}

void UPCGDefaultExecutionSource::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UPCGDefaultExecutionSource* This = CastChecked<UPCGDefaultExecutionSource>(InThis);
	
#if WITH_EDITOR
	This->Inspection.AddReferencedObjects(Collector);
#endif // WITH_EDITOR

	Super::AddReferencedObjects(This, Collector);
}
