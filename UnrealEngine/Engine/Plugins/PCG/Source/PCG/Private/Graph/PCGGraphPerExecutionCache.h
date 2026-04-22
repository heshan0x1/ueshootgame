// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGCommon.h"

#include "Misc/SpinLock.h"
#include "StructUtils/InstancedStruct.h"

#include "PCGGraphPerExecutionCache.generated.h"

class UPCGData;

USTRUCT()
struct FPCGPerExecutionCacheData
{
	GENERATED_BODY()

	FPCGPerExecutionCacheData() = default;
	virtual ~FPCGPerExecutionCacheData() = default;

	virtual void AddStructReferencedObjects(FReferenceCollector& Collector) {}
};

USTRUCT()
struct FPCGPerExecutionCachePCGData : public FPCGPerExecutionCacheData
{
	GENERATED_BODY()

	FPCGPerExecutionCachePCGData() = default;
	FPCGPerExecutionCachePCGData(UPCGData* InData)
		: Data(InData)
	{
	}

	virtual void AddStructReferencedObjects(FReferenceCollector& Collector) override;

	TObjectPtr<UPCGData> Data = nullptr;
};

USTRUCT()
struct FPCGPerExecutionCacheBounds : public FPCGPerExecutionCacheData
{
	GENERATED_BODY()

	FPCGPerExecutionCacheBounds() = default;
	FPCGPerExecutionCacheBounds(const FBox& InBounds)
		: Bounds(InBounds)
	{
	}

	FBox Bounds;
};

enum class EPCGPerExecutionCacheDataType : uint8
{
	PCGData = 0,
	InputData,
	ActorData,
	LandscapeData,
	LandscapeHeightData,
	OriginalActorData,
	Bounds,
	OriginalBounds,
	LocalSpaceBounds,
	OriginalLocalSpaceBounds,
	Count
};

struct FPCGPerExecutionCacheEntry
{
	void AddStructReferencedObjects(FReferenceCollector& Collector);

	// Fixed size array for now, could become a map at some point
	TInstancedStruct<FPCGPerExecutionCacheData> Data[(uint8)EPCGPerExecutionCacheDataType::Count];
};

struct FPCGPerExecutionCache
{
	using FPCGLock = UE::FSpinLock;

	UPCGData* GetExecutionCachePCGData(FPCGTaskId InGraphExecutionTaskId, EPCGPerExecutionCacheDataType InExecutionCacheDataType, bool& bOutFound);
	void SetExecutionCachePCGData(FPCGTaskId InGraphExecutionTaskId, EPCGPerExecutionCacheDataType InExecutionCacheDataType, UPCGData* InData);

	TOptional<FBox> GetExecutionCacheBounds(FPCGTaskId InGraphExecutionTaskId, EPCGPerExecutionCacheDataType InExecutionCacheDataType);
	void SetExecutionCacheBounds(FPCGTaskId InGraphExecutionTaskId, EPCGPerExecutionCacheDataType InExecutionCacheDataType, const FBox& InBounds);

	void Clear();
	void AddStructReferencedObjects(FReferenceCollector& Collector);

	/** Per graph execution cache, gets emptied when executor has no more work to do */
	FPCGLock Lock;
	TMap<FPCGTaskId, FPCGPerExecutionCacheEntry> Entries;
};