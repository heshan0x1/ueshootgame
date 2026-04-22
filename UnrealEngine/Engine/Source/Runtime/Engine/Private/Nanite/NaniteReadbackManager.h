// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RHIGPUReadback.h"

class FRDGBuilder;
class FRDGBuffer;
class FRDGPooledBuffer;

namespace Nanite
{

class FReadbackManager
{
public:
	FReadbackManager(uint32 InNumBuffers);

	uint32 PrepareRequestsBuffer(FRDGBuilder& GraphBuilder);
	
	struct FGPUStreamingRequest* LockLatest(uint32& OutNumStreamingRequestsClamped, uint32& OutNumStreamingRequests);
	void Unlock();
	
	void QueueReadback(FRDGBuilder& GraphBuilder);

	FRDGBuffer* GetStreamingRequestsBuffer(FRDGBuilder& GraphBuilder) const;
	uint32 GetBufferVersion() const;
	
private:
	struct FReadbackBuffer
	{
		TUniquePtr<FRHIGPUBufferReadback>	Buffer;
		uint32								NumElements = 0u;
	};

	class FBufferSizeManager
	{
	public:
		FBufferSizeManager();

		void Update(uint32 NumRequests);
		uint32 GetSize();

	private:
		float CurrentSize;
		uint32 OverBudgetCounter = 0;
		uint32 UnderBudgetCounter = 0;
	};

	TRefCountPtr<FRDGPooledBuffer>	RequestsBuffer;
	TArray<FReadbackBuffer>			ReadbackBuffers;

	FReadbackBuffer*				LatestBuffer = nullptr;
	uint32							NumBuffers = 0;
	uint32							NumPendingBuffers = 0;
	uint32							NextReadBufferIndex = 0;
	uint32							BufferVersion = 0;

	FBufferSizeManager				BufferSizeManager;
};


} // namespace Nanite