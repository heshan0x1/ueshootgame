// Copyright Epic Games, Inc. All Rights Reserved.

#include "ComputeFramework/ComputeGraphFromText.h"

#include "Algo/Count.h"
#include "ComputeFramework/ComputeDataInterfaceBuffer.h"
#include "ComputeFramework/ComputeDataInterfaceDispatch.h"
#include "ComputeFramework/ComputeGraphComponent.h"
#include "ComputeFramework/ComputeKernel.h"
#include "ComputeFramework/ComputeKernelSource.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ComputeGraphFromText)

#if WITH_EDITOR

void UComputeGraphFromText::SetGraphSourceText(FString const& InText)
{
	GraphSourceText = InText;
	Rebuild();
}

void UComputeGraphFromText::PostLoad()
{
	Rebuild();
	Super::PostLoad();
}

void UComputeGraphFromText::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UComputeGraphFromText, GraphSourceText))
		{
			Rebuild();
			UpdateResources();
		}
	}
}

void UComputeGraphFromText::Reset()
{
	KernelInvocations.Reset();
	DataInterfaces.Reset();
	GraphEdges.Reset();
	Bindings.Reset();
	DataInterfaceToBinding.Reset();
}

int32 UComputeGraphFromText::AddKernel(FString const& InSourceText, FString const& InEntryPoint, FIntVector const& InGroupSize)
{
	UComputeKernel* Kernel = NewObject<UComputeKernel>(this);
	const int32 KernelIndex = KernelInvocations.Add(Kernel);

	UComputeKernelSourceWithText* KernelSource = NewObject<UComputeKernelSourceWithText>(Kernel);
	Kernel->KernelSource = KernelSource;
	
	KernelSource->SourceText = InSourceText;
	KernelSource->EntryPoint = InEntryPoint;
	KernelSource->GroupSize = InGroupSize;

	return KernelIndex;
}

int32 UComputeGraphFromText::AddObjectBinding(UClass* InClass)
{
	const int32 BindingIndex = Bindings.Add(InClass);
	return BindingIndex;
}

int32 UComputeGraphFromText::AddDataInterface(UComputeDataInterface* InDataInterface, int32 InBindingIndex)
{
	const int32 DataInterfaceIndex = DataInterfaces.Add(InDataInterface);
	DataInterfaceToBinding.Add(InBindingIndex);
	return DataInterfaceIndex;
}

bool UComputeGraphFromText::AddInputGraphConnection(int32 InKernelIndex, FString const& InKernelFunctionName, int32 InDataProviderIndex, FString const& InDataProviderFunctionName)
{
	if (!KernelInvocations.IsValidIndex(InKernelIndex) || !DataInterfaces.IsValidIndex(InDataProviderIndex))
	{
		return false;
	}
	
	// Find matching function exposed by data provider.
	TArray<FShaderFunctionDefinition> Functions;
	DataInterfaces[InDataProviderIndex]->GetSupportedInputs(Functions);
	const int32 FunctionIndex = Functions.IndexOfByPredicate([InDataProviderFunctionName](FShaderFunctionDefinition const& Def) { return Def.Name == InDataProviderFunctionName; });
	if (FunctionIndex == INDEX_NONE)
	{
		return false;
	}
	
	// Store input function and rename.
	KernelInvocations[InKernelIndex]->KernelSource->ExternalInputs.Add_GetRef(Functions[FunctionIndex]).SetName(InKernelFunctionName);

	// Count current kernel binding index.
	const int32 KernelInputIndex = Algo::CountIf(GraphEdges, [InKernelIndex](FComputeGraphEdge const& Edge) { return Edge.KernelIndex == InKernelIndex && Edge.bKernelInput; });

	FComputeGraphEdge& Edge = GraphEdges.AddDefaulted_GetRef();
	Edge.KernelIndex = InKernelIndex;
	Edge.KernelBindingIndex = KernelInputIndex;
	Edge.DataInterfaceIndex = InDataProviderIndex;
	Edge.DataInterfaceBindingIndex = FunctionIndex;
	Edge.bKernelInput = true;

	return true;
}

bool UComputeGraphFromText::AddOutputGraphConnection(int32 InKernelIndex, FString const& InKernelFunctionName, int32 InDataProviderIndex, FString const& InDataProviderFunctionName)
{
	if (!KernelInvocations.IsValidIndex(InKernelIndex) || !DataInterfaces.IsValidIndex(InDataProviderIndex))
	{
		return false;
	}

	// Find matching function exposed by data provider.
	TArray<FShaderFunctionDefinition> Functions;
	DataInterfaces[InDataProviderIndex]->GetSupportedOutputs(Functions);
	
	const int32 FunctionIndex = Functions.IndexOfByPredicate([InDataProviderFunctionName](FShaderFunctionDefinition const& Def) { return Def.Name == InDataProviderFunctionName; });
	if (FunctionIndex == INDEX_NONE)
	{
		return false;
	}

	// Store input function and rename.
	KernelInvocations[InKernelIndex]->KernelSource->ExternalOutputs.Add_GetRef(Functions[FunctionIndex]).SetName(InKernelFunctionName);

	// Count current kernel binding index.
	const int32 KernelOutputIndex = Algo::CountIf(GraphEdges, [InKernelIndex](FComputeGraphEdge const& Edge) { return Edge.KernelIndex == InKernelIndex && !Edge.bKernelInput; });

	FComputeGraphEdge& Edge = GraphEdges.AddDefaulted_GetRef();
	Edge.KernelIndex = InKernelIndex;
	Edge.KernelBindingIndex = KernelOutputIndex;
	Edge.DataInterfaceIndex = InDataProviderIndex;
	Edge.DataInterfaceBindingIndex = FunctionIndex;
	Edge.bKernelInput = false;

	return true;
}

void UComputeGraphFromText::Rebuild()
{
	Reset();

	// Create a simple copy buffer kernel.
	FString SourceText(TEXT("[numthreads(64, 1, 1)]\nvoid CopyCS(uint DispatchThreadId : SV_DispatchThreadID)\n{\n\tuint ThreadIndex = DispatchThreadId.x;\n\tif (ThreadIndex >= ReadNumValues())\n\t{\n\t\treturn;\n\t}\n\tWriteValue(ThreadIndex, ReadValue(ThreadIndex));\n}"));
	FString EntryPoint(TEXT("CopyCS"));
	FIntVector GroupSize(64, 1, 1);
	const int32 CopyBufferKernelIndex = AddKernel(SourceText, EntryPoint, GroupSize);

	// Set some default (but unused here) object binding type.
	const int32 DefaultBindingClassIndex = AddObjectBinding(UComputeGraphComponent::StaticClass());

	// Create dispatch data interface.
	UComputeDataInterfaceDispatch* DispatchDataInterface = NewObject<UComputeDataInterfaceDispatch>(this);
	DispatchDataInterface->ThreadCount = FUintVector3(100, 1, 1);
	const int32 DispatchDataInterfaceIndex = AddDataInterface(DispatchDataInterface, DefaultBindingClassIndex);

	// Create source buffer data interface.
	UComputeDataInterfaceBuffer* SourceBufferDataInterface = NewObject<UComputeDataInterfaceBuffer>(this);
	SourceBufferDataInterface->ValueType = FShaderValueType::Get(EShaderFundamentalType::Int, 3);
	SourceBufferDataInterface->ElementCount = 100;
	SourceBufferDataInterface->bClearBeforeUse = true;
	const int32 SourceBufferDataInterfaceIndex = AddDataInterface(SourceBufferDataInterface, DefaultBindingClassIndex);

	// Create destination buffer data interface.
	UComputeDataInterfaceBuffer* DestBufferDataInterface = NewObject<UComputeDataInterfaceBuffer>(this);
	DestBufferDataInterface->ValueType = FShaderValueType::Get(EShaderFundamentalType::Int, 3);
	DestBufferDataInterface->ElementCount = 100;
	const int32 DestBufferDataInterfaceIndex = AddDataInterface(DestBufferDataInterface, DefaultBindingClassIndex);;

	// Bind kernel functions (as used in the kernel source) to the data interface functions (as defined in the data interface).
	// Note that the DispatchDataInterface is special and we must bind one of its functions even if we don't actually use any.
	AddInputGraphConnection(CopyBufferKernelIndex, TEXT("ReadNumThreads"), DispatchDataInterfaceIndex, TEXT("ReadNumThreads"));
	AddInputGraphConnection(CopyBufferKernelIndex, TEXT("ReadNumValues"), SourceBufferDataInterfaceIndex, TEXT("ReadNumValues"));
	AddInputGraphConnection(CopyBufferKernelIndex, TEXT("ReadValue"), SourceBufferDataInterfaceIndex, TEXT("ReadValue"));
	AddOutputGraphConnection(CopyBufferKernelIndex, TEXT("WriteValue"), DestBufferDataInterfaceIndex, TEXT("WriteValue"));
}

#endif
