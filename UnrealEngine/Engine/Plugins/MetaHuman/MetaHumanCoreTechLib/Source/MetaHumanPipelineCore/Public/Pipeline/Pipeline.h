// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Pipeline/Connection.h"
#include "Pipeline/DataTreeTypes.h"
#include "HAL/IConsoleManager.h"

#define UE_API METAHUMANPIPELINECORE_API

class UMetaHumanPipelineProcess;

namespace UE::MetaHuman::Pipeline
{

METAHUMANPIPELINECORE_API extern TAutoConsoleVariable<bool> CVarBalancedGPUSelection;

class FNode;
class FPipelineData;

DECLARE_MULTICAST_DELEGATE_OneParam(FFrameComplete, TSharedPtr<FPipelineData> InPipelineData);
DECLARE_MULTICAST_DELEGATE_OneParam(FProcessComplete, TSharedPtr<FPipelineData> InPipelineData);

enum class EPipelineMode
{
	PushSync = 0,
	PushAsync,
	PushSyncNodes,
	PushAsyncNodes,
	Pull
};

class FPipelineRunParameters
{
public:

	FPipelineRunParameters() = default;

	UE_API void SetMode(EPipelineMode InMode);
	UE_API EPipelineMode GetMode() const;

	UE_API void SetOnFrameComplete(const FFrameComplete& InOnFrameComplete);
	UE_API const FFrameComplete& GetOnFrameComplete() const;

	UE_API void SetOnProcessComplete(const FProcessComplete& InOnProcessComplete);
	UE_API const FProcessComplete& GetOnProcessComplete() const;

	UE_API void SetStartFrame(int32 InStartFrame);
	UE_API int32 GetStartFrame() const;

	UE_API void SetEndFrame(int32 InEndFrame);
	UE_API int32 GetEndFrame() const;

	UE_API void SetRestrictStartingToGameThread(bool bInRestrictStartingToGameThread);
	UE_API bool GetRestrictStartingToGameThread() const;

	UE_API void SetProcessNodesInRandomOrder(bool bInProcessNodesInRandomOrder);
	UE_API bool GetProcessNodesInRandomOrder() const;

	UE_API void SetCheckThreadLimit(bool bInCheckThreadLimit);
	UE_API bool GetCheckThreadLimit() const;

	UE_API void SetCheckProcessingSpeed(bool bInCheckProcessingSpeed);
	UE_API bool GetCheckProcessingSpeed() const;

	UE_API void SetVerbosity(ELogVerbosity::Type InVerbosity);
	UE_API ELogVerbosity::Type GetVerbosity() const;

	UE_API void SetGpuToUse(const FString& InUseGPU);
	UE_API void UnsetGpuToUse();
	UE_API TOptional<FString> GetGpuToUse() const;

private:

	EPipelineMode Mode = EPipelineMode::PushAsync;

	FFrameComplete OnFrameComplete;
	FProcessComplete OnProcessComplete;

	int32 StartFrame = 0;
	int32 EndFrame = -1;

	bool bRestrictStartingToGameThread = true;
	bool bProcessNodesInRandomOrder = true;
	bool bCheckThreadLimit = true;
	bool bCheckProcessingSpeed = true;

	ELogVerbosity::Type Verbosity = ELogVerbosity::Display;

	TOptional<FString> UseGPU;

	// potentially other termination conditions here like timeouts
};

class FPipeline
{
public:

	UE_API FPipeline();
	UE_API ~FPipeline();

	// Make class non-copyable - copying is not supported due to how the Process member is protected from garbage collection
	FPipeline(const FPipeline& InOther) = delete;
	FPipeline(FPipeline&& InOther) = delete;
	FPipeline& operator=(const FPipeline& InOther) = delete;

	UE_API void Reset();

	template<typename T, typename... InArgTypes>
	TSharedPtr<T> MakeNode(InArgTypes&&... InArgs)
	{
		TSharedPtr<T> Node = MakeShared<T>(Forward<InArgTypes>(InArgs)...);
		Nodes.Add(Node);
		return Node;
	}

	UE_API void AddNode(const TSharedPtr<FNode>& InNode);

	UE_API void MakeConnection(const TSharedPtr<FNode>& InFrom, const TSharedPtr<FNode>& InTo, int32 InFromGroup = 0, int32 InToGroup = 0);

	UE_API void Run(EPipelineMode InPipelineMode, const FFrameComplete& InOnFrameComplete, const FProcessComplete& InOnProcessComplete);
	UE_API void Run(const FPipelineRunParameters& InPipelineRunParameters);
	UE_API bool IsRunning() const;
	UE_API void Cancel();
	UE_API int32 GetNodeCount() const;

	UE_API FString ToString() const;

	static UE_API bool GetPhysicalDeviceLUIDs(FString& OutUEPhysicalDeviceLUID, TArray<FString>& OutAllPhysicalDeviceLUIDs);
	static UE_API FString PickPhysicalDevice();

private:

	TArray<TSharedPtr<FNode>> Nodes;
	TArray<FConnection> Connections;

	TWeakObjectPtr<UMetaHumanPipelineProcess> Process = nullptr;

	UE_API void StopProcess(bool bInClearMessageQueue = false);
};

}

#undef UE_API
