// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGGraphPerExecutionCache.h"

UPCGData* FPCGPerExecutionCache::GetExecutionCachePCGData(FPCGTaskId InGraphExecutionTaskId, EPCGPerExecutionCacheDataType InExecutionCacheDataType, bool& bOutFound)
{
	bOutFound = false;
	if (InGraphExecutionTaskId != InvalidPCGTaskId)
	{
		UE::TScopeLock ScopeLock(Lock);
		if (FPCGPerExecutionCacheEntry* PerExecutionCacheEntry = Entries.Find(InGraphExecutionTaskId))
		{
			TInstancedStruct<FPCGPerExecutionCacheData>& StructData = PerExecutionCacheEntry->Data[(uint8)InExecutionCacheDataType];
			if (StructData.IsValid())
			{
				bOutFound = true;
				FPCGPerExecutionCachePCGData& PCGData = StructData.GetMutable<FPCGPerExecutionCachePCGData>();
				return PCGData.Data;
			}
		}
	}

	return nullptr;
}

void FPCGPerExecutionCache::SetExecutionCachePCGData(FPCGTaskId InGraphExecutionTaskId, EPCGPerExecutionCacheDataType InExecutionCacheDataType, UPCGData* InData)
{
	if (InGraphExecutionTaskId != InvalidPCGTaskId)
	{
		UE::TScopeLock ScopeLock(Lock);
		FPCGPerExecutionCacheEntry& PerExecutionCacheEntry = Entries.FindOrAdd(InGraphExecutionTaskId);
		check(!PerExecutionCacheEntry.Data[(uint8)InExecutionCacheDataType].IsValid());
		PerExecutionCacheEntry.Data[(uint8)InExecutionCacheDataType].InitializeAs<FPCGPerExecutionCachePCGData>(InData);
	}
}

TOptional<FBox> FPCGPerExecutionCache::GetExecutionCacheBounds(FPCGTaskId InGraphExecutionTaskId, EPCGPerExecutionCacheDataType InExecutionCacheDataType)
{
	if (InGraphExecutionTaskId != InvalidPCGTaskId)
	{
		UE::TScopeLock ScopeLock(Lock);
		if (FPCGPerExecutionCacheEntry* PerExecutionCacheEntry = Entries.Find(InGraphExecutionTaskId))
		{
			TInstancedStruct<FPCGPerExecutionCacheData>& StructData = PerExecutionCacheEntry->Data[(uint8)InExecutionCacheDataType];
			if (StructData.IsValid())
			{
				FPCGPerExecutionCacheBounds& BoundsData = StructData.GetMutable<FPCGPerExecutionCacheBounds>();
				return BoundsData.Bounds;
			}
		}
	}

	return TOptional<FBox>();
}

void FPCGPerExecutionCache::SetExecutionCacheBounds(FPCGTaskId InGraphExecutionTaskId, EPCGPerExecutionCacheDataType InExecutionCacheDataType, const FBox& InBounds)
{
	if (InGraphExecutionTaskId != InvalidPCGTaskId)
	{
		UE::TScopeLock ScopeLock(Lock);
		FPCGPerExecutionCacheEntry& PerExecutionCacheEntry = Entries.FindOrAdd(InGraphExecutionTaskId);
		check(!PerExecutionCacheEntry.Data[(uint8)InExecutionCacheDataType].IsValid());
		PerExecutionCacheEntry.Data[(uint8)InExecutionCacheDataType].InitializeAs<FPCGPerExecutionCacheBounds>(InBounds);
	}
}

void FPCGPerExecutionCache::Clear()
{
	UE::TScopeLock ScopeLock(Lock);
	Entries.Empty();
}

void FPCGPerExecutionCache::AddStructReferencedObjects(FReferenceCollector& Collector)
{
	UE::TScopeLock ScopeLock(Lock);
	for (TMap<FPCGTaskId, FPCGPerExecutionCacheEntry>::TIterator It = Entries.CreateIterator(); It; ++It)
	{
		It.Value().AddStructReferencedObjects(Collector);
	}
}

void FPCGPerExecutionCacheEntry::AddStructReferencedObjects(FReferenceCollector& Collector)
{
	for (uint8 DataIndex = 0; DataIndex < (uint8)EPCGPerExecutionCacheDataType::Count; ++DataIndex)
	{
		if (Data[DataIndex].IsValid())
		{
			Data[DataIndex].GetMutable().AddStructReferencedObjects(Collector);
		}
	}
}

void FPCGPerExecutionCachePCGData::AddStructReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Data);
}

