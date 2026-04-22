// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ComputeGraph.h"
#include "ComputeGraphFromText.generated.h"

#define UE_API COMPUTEFRAMEWORK_API

/** Simple data driven compute graph implementation. */
UCLASS(MinimalAPI)
class UComputeGraphFromText : public UComputeGraph
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
	/** Source text containing the full graph description. */
	UPROPERTY(EditAnywhere, Category = "Graph", meta = (MultiLine = true))
	FString GraphSourceText;
#endif

#if WITH_EDITOR
	//~ Begin UObject Interface.
	UE_API void PostLoad() override;
	UE_API void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject Interface.

public:
	/** Set the source text that describes the graph. */
	void SetGraphSourceText(FString const& InText);

private:
	void Reset();
	int32 AddKernel(FString const& InSourceText, FString const& InEntryPoint, FIntVector const& InGroupSize);
	int32 AddObjectBinding(UClass* InClass);
	int32 AddDataInterface(UComputeDataInterface* InDataInterface, int32 InBindingIndex);
	bool AddInputGraphConnection(int32 InKernelIndex, FString const& InKernelFunctionName, int32 InDataProviderIndex, FString const& InDataProviderFunctionName);
	bool AddOutputGraphConnection(int32 InKernelIndex, FString const& InKernelFunctionName, int32 InDataProviderIndex, FString const& InDataProviderFunctionName);
	void Rebuild();
#endif
};

#undef UE_API
