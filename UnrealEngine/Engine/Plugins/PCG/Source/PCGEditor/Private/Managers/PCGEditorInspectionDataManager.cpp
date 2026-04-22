// Copyright Epic Games, Inc. All Rights Reserved.

#include "Managers/PCGEditorInspectionDataManager.h"

#include "PCGEditorGraph.h"
#include "PCGGraphExecutionInspection.h"
#include "PCGGraphExecutionStateInterface.h"
#include "Nodes/PCGEditorGraphNodeBase.h"

bool FPCGEditorInspectionDataEntry::IsValid() const
{
	return !InspectionData.TaggedData.IsEmpty();
}

void FPCGEditorInspectionDataEntry::Clear()
{
	InspectionData = FPCGDataCollection{};
	Node.Reset();
	PinName = NAME_None;
	bIsOutputPin = true;
	Stack = FPCGStack{};
}

void FPCGEditorInspectionDataManager::Cleanup()
{
	if (PCGSourceBeingInspected.IsValid())
	{
		if (PCGSourceBeingInspected->GetExecutionState().GetInspection().IsInspecting())
		{
			PCGSourceBeingInspected->GetExecutionState().GetInspection().DisableInspection();
		}
	}

	if (LastValidPCGSourceBeingInspected.IsValid())
	{
		if (LastValidPCGSourceBeingInspected->GetExecutionState().GetInspection().IsInspecting())
		{
			LastValidPCGSourceBeingInspected->GetExecutionState().GetInspection().DisableInspection();
		}
	}

	OnInspectedStackChangedDelegate.Clear();
}

void FPCGEditorInspectionDataManager::SetStackBeingInspected(const FPCGStack& FullStack)
{
	if (FullStack == StackBeingInspected)
	{
		// No-op if we're already inspecting this stack.
		return;
	}

	IPCGGraphExecutionSource* LastSource = LastValidPCGSourceBeingInspected.Get();
	IPCGGraphExecutionSource* NewSource = const_cast<IPCGGraphExecutionSource*>(FullStack.GetRootSource());

	if (NewSource && NewSource != LastSource)
	{
		if (LastSource && LastSource->GetExecutionState().GetInspection().IsInspecting())
		{
			LastSource->GetExecutionState().GetInspection().DisableInspection();
		}

		LastValidPCGSourceBeingInspected = NewSource;
	}

	PCGSourceBeingInspected = NewSource;

	StackBeingInspected = FullStack;
	OnInspectedStackChangedDelegate.Broadcast(StackBeingInspected);
}

void FPCGEditorInspectionDataManager::OnSourceGenerated(IPCGGraphExecutionSource* InSource) const
{
	if (InSource == GetPCGSourceBeingInspected())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGEditor::SetStackBeingInspected::BroadcastStackBeingInspected);
		OnInspectedStackChangedDelegate.Broadcast(StackBeingInspected);
	}
}

const FPCGDataCollection* FPCGEditorInspectionDataManager::GetInspectionData(const FPCGEditorInspectionDataEntrySetupParams& InParams) const
{
	const FPCGDataCollection* DataCollection = nullptr;
	FPCGStack Stack{};
	const UPCGPin* Pin = nullptr;

	return GetInspectionData_Internal(InParams, DataCollection, Stack, Pin) ? DataCollection : nullptr;
}

void FPCGEditorInspectionDataManager::SetupInspectionEntry(const FPCGEditorInspectionDataEntrySetupParams& InParams)
{
	InspectionEntries[InParams.EntryIndex] = ConstructEntry(InParams);
	OnInspectionEntryChangedDelegate.Broadcast(InspectionEntries[InParams.EntryIndex], InParams.EntryIndex);
}

const FPCGEditorInspectionDataEntry& FPCGEditorInspectionDataManager::GetInspectionEntry(int32 EntryIndex)
{
	check(EntryIndex >= 0 && EntryIndex < NumberOfEntries);
	return InspectionEntries[EntryIndex].IsValid() ? InspectionEntries[EntryIndex] : InspectionEntries[0];
}

void FPCGEditorInspectionDataManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (FPCGEditorInspectionDataEntry& Entry : InspectionEntries)
	{
		Entry.InspectionData.AddReferences(Collector);
	}
}

FPCGEditorInspectionDataEntry FPCGEditorInspectionDataManager::ConstructEntry(const FPCGEditorInspectionDataEntrySetupParams& InParams) const
{
	FPCGEditorInspectionDataEntry Result{};

	if (InParams.EntryIndex < 0
		|| InParams.EntryIndex >= InspectionEntries.Num()
		|| !InParams.Node
		|| !PCGSourceBeingInspected.IsValid()
		|| StackBeingInspected.GetStackFrames().IsEmpty())
	{
		return Result;
	}

	const FPCGDataCollection* DataCollection = nullptr;
	FPCGStack Stack{};
	const UPCGPin* Pin = nullptr;
	
	if (!GetInspectionData_Internal(InParams, DataCollection, Stack, Pin))
	{
		return Result;
	}

	check(DataCollection && Pin);

	Result.InspectionData = *DataCollection;
	Result.Node = InParams.Node;
	Result.PinName = Pin->Properties.Label;
	Result.bIsOutputPin = InParams.bIsOutputPin;
	Result.Stack = MoveTemp(Stack);

	return Result;
}

bool FPCGEditorInspectionDataManager::GetInspectionData_Internal(const FPCGEditorInspectionDataEntrySetupParams& InParams, FPCGDataCollection const*& OutData, FPCGStack& OutStack, UPCGPin const*& OutPin) const
{
	const UPCGNode* PCGNode = InParams.Node ? InParams.Node->GetPCGNode() : nullptr;
	if (!PCGNode)
	{
		return false;
	}

	const TArray<TObjectPtr<UPCGPin>>& Pins = InParams.bIsOutputPin ? PCGNode->GetOutputPins() : PCGNode->GetInputPins();
	int32 PinIndex = InParams.PinIndex;
	if (PinIndex == INDEX_NONE)
	{
		PinIndex = InParams.PinName == NAME_None ? 0 : Pins.IndexOfByPredicate([PinName = InParams.PinName](const UPCGPin* InPin) { return InPin && InPin->Properties.Label == PinName; });
	}
	
	if (!Pins.IsValidIndex(PinIndex))
	{
		return false;
	}

	const UPCGPin* Pin = Pins[PinIndex];

	PCGEditorGraphUtils::GetInspectablePin(PCGNode, Pin, PCGNode, OutPin);

	if (!PCGNode || !OutPin)
	{
		return false;
	}

	// Create a temporary stack with Node+Pin to query the exact DataCollection we are inspecting
	OutStack = StackBeingInspected;
	TArray<FPCGStackFrame>& StackFrames = OutStack.GetStackFramesMutable();
	StackFrames.Reserve(StackFrames.Num() + 2);
	StackFrames.Emplace(PCGNode);
	StackFrames.Emplace(OutPin);

	if (IPCGGraphExecutionSource* Source = PCGSourceBeingInspected.Get())
	{
		OutData = Source->GetExecutionState().GetInspection().GetInspectionData(OutStack);
	}
	
	return OutData != nullptr;
}
