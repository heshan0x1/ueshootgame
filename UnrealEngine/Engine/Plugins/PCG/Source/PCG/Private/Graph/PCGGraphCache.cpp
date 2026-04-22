// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/PCGGraphCache.h"

#include "PCGComponent.h"
#include "PCGModule.h"

#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeRWLock.h"

static TAutoConsoleVariable<bool> CVarCacheEnabledEditor(
	TEXT("pcg.Cache.Editor.Enabled"),
	true,
	TEXT("Enables the cache system in editor worlds."));

static TAutoConsoleVariable<bool> CVarCacheEnabledRuntime(
	TEXT("pcg.Cache.Runtime.Enabled"),
	false,
	TEXT("Enables the cache system in runtime game worlds."));

static TAutoConsoleVariable<bool> CVarCacheDebugging(
	TEXT("pcg.Cache.EnableDebugging"),
	false,
	TEXT("Enable various features for debugging the graph cache system."));

static TAutoConsoleVariable<int32> CVarCacheMemoryBudgetEditorMB(
	TEXT("pcg.Cache.Editor.MemoryBudgetMB"),
	6144,
	TEXT("Memory budget for data in cache in editor worlds (MB)."));

static TAutoConsoleVariable<int32> CVarCacheMemoryBudgetRuntimeMB(
	TEXT("pcg.Cache.Runtime.MemoryBudgetMB"),
	128,
	TEXT("Memory budget for data in cache in game worlds (MB)."));

static TAutoConsoleVariable<float> CVarCacheMemoryCleanupRatio(
	TEXT("pcg.Cache.MemoryCleanupRatio"),
	0.5f,
	TEXT("Target cache size ratio after triggering a cleanup (between 0 and 1.)."));

static TAutoConsoleVariable<bool> CVarCacheMemoryBudgetEnabled(
	TEXT("pcg.Cache.EnableMemoryBudget"),
	true,
	TEXT("Whether memory budget is enforced (items purged from cache to respect pcg.Cache.MemoryBudgetMB."));

static TAutoConsoleVariable<bool> CVarValidateElementToCacheEntryKeys(
	TEXT("pcg.Cache.Debug.ValidateElementToCacheEntryKeys"),
	false,
	TEXT("Validate ElementToCacheEntryKeys acceleration table (debug)."));

// Aliases for old deprecated settings.
static TAutoConsoleVariable<bool> CVarCacheEnabled_DEPRECATED(
	TEXT("pcg.Cache.Enabled"),
	true,
	TEXT("DEPRECATED (5.7): use pcg.Cache.Editor.Enabled and pcg.Cache.Runtime.Enabled instead."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		// Called when user sets cvar from console, or when this cvar is set when launching standalone build. Pushes value to new cvars.
		UE_LOG(LogPCG, Warning, TEXT("pcg.Cache.Enabled is deprecated in 5.7. use pcg.Cache.Editor.Enabled and pcg.Cache.Runtime.Enabled instead."));
		check(InVariable);
		bool bNewValue = true;
		InVariable->GetValue(bNewValue);
		CVarCacheEnabledRuntime->Set(bNewValue, ECVF_SetByCode);
		CVarCacheEnabledEditor->Set(bNewValue, ECVF_SetByCode);
	}));
static TAutoConsoleVariable<int32> CVarCacheMemoryBudgetMB_DEPRECATED(
	TEXT("pcg.Cache.MemoryBudgetMB"),
	6144,
	TEXT("DEPRECATED (5.7): use pcg.Cache.Editor.MemoryBudgetMB and pcg.Cache.Runtime.MemoryBudgetMB instead."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* InVariable)
	{
		// Called when user sets cvar from console, or when this cvar is set when launching standalone build. Pushes value to new cvars.
		UE_LOG(LogPCG, Warning, TEXT("pcg.Cache.MemoryBudgetMB is deprecated in 5.7. use pcg.Cache.Editor.MemoryBudgetMB and pcg.Cache.Runtime.MemoryBudgetMB instead."));
		check(InVariable);
		int32 NewValue = 6144;
		InVariable->GetValue(NewValue);
		CVarCacheMemoryBudgetEditorMB->Set(NewValue, ECVF_SetByCode);
		CVarCacheMemoryBudgetRuntimeMB->Set(NewValue, ECVF_SetByCode);
	}));

// Initial max number of entries graph cache
static const int32 GPCGGraphCacheInitialCapacity = 65536;

FPCGGraphCache::FPCGGraphCache(bool bGameWorld)
	: bIsGameWorld(bGameWorld)
{
	const int32 InitialMaxCapacity = IsEnabled() ? GPCGGraphCacheInitialCapacity : 0;
	CacheData.Empty(InitialMaxCapacity);
}

FPCGGraphCache::~FPCGGraphCache()
{
	ClearCache();
}

bool FPCGGraphCache::IsEnabled() const
{
	return bIsGameWorld ? CVarCacheEnabledRuntime.GetValueOnAnyThread() : CVarCacheEnabledEditor.GetValueOnAnyThread();
}

int32 FPCGGraphCache::GetMemoryBudgetMB() const
{
	return bIsGameWorld ? CVarCacheMemoryBudgetRuntimeMB.GetValueOnAnyThread() : CVarCacheMemoryBudgetEditorMB.GetValueOnAnyThread();
}

bool FPCGGraphCache::GetFromCache(const FPCGGetFromCacheParams& Params, FPCGDataCollection& OutOutput) const
{
	if (!IsEnabled())
	{
		return false;
	}

	const UPCGNode* InNode = Params.Node;
	const IPCGElement* InElement = Params.Element;
	const IPCGGraphExecutionSource* InExecutionSource = Params.ExecutionSource;
	const FPCGCrc& InDependenciesCrc = Params.Crc;

	if(!InDependenciesCrc.IsValid())
	{
		UE_LOG(LogPCG, Warning, TEXT("Invalid dependencies passed to FPCGGraphCache::GetFromCache(), lookup aborted."));
		return false;
	}

	const bool bDebuggingEnabled = IsDebuggingEnabled() && InExecutionSource && InNode;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGraphCache::GetFromCache);
		UE::TScopeLock ScopedLock(CacheLock);

		FPCGCacheEntryKey CacheKey(InElement, InDependenciesCrc);
		if (const FPCGDataCollection* Value = const_cast<FPCGGraphCache*>(this)->CacheData.FindAndTouch(CacheKey))
		{
			if (bDebuggingEnabled)
			{
				// Leading spaces to align log content with warnings below - helps readability a lot.
				UE_LOG(LogPCG, Log, TEXT("         [%s] %s\t\tCACHE HIT %u"), *InExecutionSource->GetExecutionState().GetDebugName(), *InNode->GetNodeTitle(EPCGNodeTitleType::ListView).ToString(), InDependenciesCrc.GetValue());
			}

			OutOutput = *Value;

			return true;
		}
		else
		{
			if (bDebuggingEnabled)
			{
				UE_LOG(LogPCG, Warning, TEXT("[%s] %s\t\tCACHE MISS %u"), *InExecutionSource->GetExecutionState().GetDebugName(), *InNode->GetNodeTitle(EPCGNodeTitleType::ListView).ToString(), InDependenciesCrc.GetValue());
			}

			return false;
		}
	}
}

void FPCGGraphCache::StoreInCache(const FPCGStoreInCacheParams& Params, const FPCGDataCollection& InOutput)
{
	if (!IsEnabled())
	{
		return;
	}

	const IPCGElement* InElement = Params.Element;
	const FPCGCrc& InDependenciesCrc = Params.Crc;

	if (!ensure(InDependenciesCrc.IsValid()))
	{
		return;
	}

	// Proxies should never go into the graph cache. These can hold onto large chunks of video memory.
	for (const FPCGTaggedData& Data : InOutput.TaggedData)
	{
		ensure(!Data.Data || Data.Data->IsCacheable());
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGraphCache::StoreInCache);
		UE::TScopeLock ScopedLock(CacheLock);

		if (CacheData.Num() == CacheData.Max())
		{
			GrowCache_Unsafe();
		}

		const FPCGCacheEntryKey CacheKey(InElement, InDependenciesCrc);
		AddToCacheInternal(CacheKey, InOutput, /*bAddToMemory=*/true);
	}
}

void FPCGGraphCache::ClearCache()
{
	UE::TScopeLock ScopedLock(CacheLock);

	// Remove all entries
	ClearCacheInternal(CacheData.Max(), /*bClearMemory=*/true);
}

bool FPCGGraphCache::EnforceMemoryBudget()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGraphCache::FPCGGraphCache::EnforceMemoryBudget);
	if (!IsEnabled())
	{
		return false;
	}

	if (!CVarCacheMemoryBudgetEnabled.GetValueOnAnyThread())
	{
		return false;
	}

	const uint64 MemoryBudget = static_cast<uint64>(GetMemoryBudgetMB()) * 1024 * 1024;
	if (TotalMemoryUsed <= MemoryBudget)
	{
		return false;
	}

	{
		UE::TScopeLock ScopedLock(CacheLock);
		const float MemoryCleanupRatio = FMath::Clamp(CVarCacheMemoryCleanupRatio.GetValueOnAnyThread(), 0.0f, 1.0f);
		const uint64 TargetCacheMemoryUsage = static_cast<uint64>(MemoryCleanupRatio * MemoryBudget);

		while (TotalMemoryUsed > TargetCacheMemoryUsage && CacheData.Num() > 0)
		{
			RemoveFromCacheInternal(CacheData.GetLeastRecentKey());
		}
		ValidateElementToCacheEntryKeys();
	}

	return true;
}

#if WITH_EDITOR
void FPCGGraphCache::CleanFromCache(const IPCGElement* InElement, const UPCGSettings* InSettings/*= nullptr*/)
{
	if (!InElement)
	{
		return;
	}

	if (IsDebuggingEnabled())
	{
		UE_LOG(LogPCG, Warning, TEXT("[] \t\tCACHE: PURGED [%s]"), InSettings ? *InSettings->GetDefaultNodeTitle().ToString() : TEXT("AnonymousElement"));
	}

	{
		UE::TScopeLock ScopedLock(CacheLock);

		ValidateElementToCacheEntryKeys();

		TSet<FPCGCacheEntryKey> ElementCacheEntryKeys;
		ElementToCacheEntryKeys.RemoveAndCopyValue(InElement, ElementCacheEntryKeys);

		for (const FPCGCacheEntryKey& Key : ElementCacheEntryKeys)
		{
			RemoveFromCacheInternal(Key);
		}

		ValidateElementToCacheEntryKeys();
	}
}

uint32 FPCGGraphCache::GetGraphCacheEntryCount(IPCGElement* InElement) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGraphCache::GetFromCache);
	UE::TScopeLock ScopedLock(CacheLock);

	if (const TSet<FPCGCacheEntryKey>* ElementCacheEntryKeys = ElementToCacheEntryKeys.Find(InElement))
	{
		return ElementCacheEntryKeys->Num();
	}

	return 0;
}
#endif // WITH_EDITOR

void FPCGGraphCache::AddReferencedObjects(FReferenceCollector& Collector)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGraphCache::AddReferencedObjects);
	UE::TScopeLock ScopedLock(CacheLock);

	for (FPCGDataCollection& CacheEntry : CacheData)
	{
		CacheEntry.AddReferences(Collector);
	}
}

void FPCGGraphCache::ValidateElementToCacheEntryKeys() const
{
	if (CVarValidateElementToCacheEntryKeys.GetValueOnAnyThread())
	{
		int32 CacheKeyCount = 0;
		for (const auto& Kvp : ElementToCacheEntryKeys)
		{
			CacheKeyCount += Kvp.Value.Num();
		}

		check(CacheKeyCount == CacheData.Num());
	}
}

void FPCGGraphCache::ClearCacheInternal(int32 InMaxEntries, bool bClearMemory)
{
	if (bClearMemory)
	{
		MemoryRecords.Empty();
		TotalMemoryUsed = 0;
	}

	CacheData.Empty(InMaxEntries);
	ElementToCacheEntryKeys.Empty();
}

void FPCGGraphCache::AddToCacheInternal(const FPCGCacheEntryKey& InKey, const FPCGDataCollection& InCollection, bool bAddToMemory)
{
	// We currently grow the cache before calling add so this shouldn't be needed but if 
	// the rules change we need to make sure we keep ElementToCacheEntryKeys in sync
	if (CacheData.Num() == CacheData.Max())
	{
		RemoveFromCacheInternal(CacheData.GetLeastRecentKey());
	}

	CacheData.Add(InKey, InCollection);
	ElementToCacheEntryKeys.FindOrAdd(InKey.GetElement()).Add(InKey);

	if (bAddToMemory)
	{
		AddDataToAccountedMemory(InCollection);
	}

	ValidateElementToCacheEntryKeys();
}

void FPCGGraphCache::RemoveFromCacheInternal(const FPCGCacheEntryKey& InKey)
{
	if (TSet<FPCGCacheEntryKey>* ElementCacheEntryKeys = ElementToCacheEntryKeys.Find(InKey.GetElement()))
	{
		ElementCacheEntryKeys->Remove(InKey);
		if (ElementCacheEntryKeys->IsEmpty())
		{
			ElementToCacheEntryKeys.Remove(InKey.GetElement());
		}
	}

	if (const FPCGDataCollection* RemovedData = CacheData.Find(InKey))
	{
		RemoveFromMemoryTotal(*RemovedData);
		CacheData.Remove(InKey);
	}
}

void FPCGGraphCache::GrowCache_Unsafe()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGGraphCache::GrowCache_Unsafe);

	TLruCache<FPCGCacheEntryKey, FPCGDataCollection> CacheDataCopy(CacheData.Max());

	// Iteration begins from most recent, so this reverses the temporal order.
	for (TLruCache<FPCGCacheEntryKey, FPCGDataCollection>::TIterator It(CacheData); It; ++It)
	{
		CacheDataCopy.Add(It.Key(), It.Value());
	}

	// Resize and flush
	const int32 NewSize = (CacheData.Num() > 0) ? (2 * CacheData.Num()) : GPCGGraphCacheInitialCapacity;
	ClearCacheInternal(NewSize, /*bClearMemory=*/false);

	UE_LOG(LogPCG, Log, TEXT("Graph cache capacity increased to %d entries."), CacheData.Max());

	// Copy back. Restore temporal order.
	for (TLruCache<FPCGCacheEntryKey, FPCGDataCollection>::TIterator It(CacheDataCopy); It; ++It)
	{
		AddToCacheInternal(It.Key(), It.Value(), /*bAddToMemory=*/false);
	}
}

void FPCGGraphCache::AddDataToAccountedMemory(const FPCGDataCollection& InCollection)
{
	for (const FPCGTaggedData& Data : InCollection.TaggedData)
	{
		if (Data.Data)
		{
			Data.Data->VisitDataNetwork([this](const UPCGData* Data)
			{
				if (Data)
				{
					// Find or add record
					if (FCachedMemoryRecord* ExistingRecord = MemoryRecords.Find(Data->UID))
					{
						ExistingRecord->InstanceCount++;
					}
					else
					{
						FResourceSizeEx ResSize = FResourceSizeEx(EResourceSizeMode::Exclusive);
						// Calculate data size. Function is non-const but is const-like, especially when
						// resource mode is Exclusive. The other mode calls a function to find all outer'd
						// objects which is non-const.
						const_cast<UPCGData*>(Data)->GetResourceSizeEx(ResSize);
						const SIZE_T DataSize = ResSize.GetDedicatedSystemMemoryBytes();

						FCachedMemoryRecord& NewRecord = MemoryRecords.Add(Data->UID);
						NewRecord.MemoryPerInstance = DataSize;
						NewRecord.InstanceCount = 1;
						TotalMemoryUsed += DataSize;
					}
				}
			});
		}
	}
}

void FPCGGraphCache::RemoveFromMemoryTotal(const FPCGDataCollection& InCollection)
{
	for (const FPCGTaggedData& Data : InCollection.TaggedData)
	{
		if (Data.Data)
		{
			Data.Data->VisitDataNetwork([this](const UPCGData* Data)
			{
				FCachedMemoryRecord* Record = Data ? MemoryRecords.Find(Data->UID) : nullptr;
				if (ensure(Record))
				{
					// Update instance count
					if (ensure(Record->InstanceCount > 0))
					{
						--Record->InstanceCount;
					}

					if (Record->InstanceCount == 0)
					{
						// Last instance removed, update accordingly
						if (TotalMemoryUsed >= Record->MemoryPerInstance)
						{
							TotalMemoryUsed -= Record->MemoryPerInstance;
						}
						else
						{
							// Should not normally reach here but it seems to happen in rare cases. Clamp to 0.
							TotalMemoryUsed = 0;
						}

						MemoryRecords.Remove(Data->UID);
					}
				}
			});
		}
	}
}

bool FPCGGraphCache::IsDebuggingEnabled() const
{
	return CVarCacheDebugging.GetValueOnAnyThread();
}

#if WITH_EDITOR
// Cvar deprecation. One-shot migration: runs after ini loading / user input via a cvar sink. Only required with editor, in standalone
// it appears the pcg.Cache.Enabled change delegate fires which handles driving the new cvars.
static void MigrateCVarsIfNeeded()
{
	static bool bDone = false;
	if (bDone)
	{
		return;
	}
	bDone = true;

	auto SetFromIni = [](uint32 InFlags, uint32 InSetBy)
	{
		// Treat any of these as "user set in ini" (covers typical places users put CVars)
		return InSetBy == ECVF_SetBySystemSettingsIni || // [SystemSettings] sections in *Engine.ini
			InSetBy == ECVF_SetByConsoleVariablesIni || // ConsoleVariables.ini
			InSetBy == ECVF_SetByDeviceProfile || // DeviceProfiles.ini
			(InFlags & ECVF_CreatedFromIni) != 0; // Created by ini before registration
	};

	const uint32 CacheEnabledFlags = CVarCacheEnabled_DEPRECATED->GetFlags();
	const uint32 CacheEnabledSetBy = (CacheEnabledFlags & ECVF_SetByMask);

	if (SetFromIni(CacheEnabledFlags, CacheEnabledSetBy))
	{
		const bool bValue = CVarCacheEnabled_DEPRECATED->GetBool();

		// Copy with the same SetBy priority so behavior matches the user's source of truth.
		CVarCacheEnabledRuntime->Set(bValue, (EConsoleVariableFlags)CacheEnabledSetBy);
		CVarCacheEnabledEditor->Set(bValue, (EConsoleVariableFlags)CacheEnabledSetBy);

		UE_LOG(LogPCG, Warning, TEXT("Cvar 'pcg.Cache.Enabled' was set from ini but is deprecated in 5.7. Migrated value to pcg.Cache.Runtime.Enabled=%d, pcg.Cache.Editor.Enabled=%d (SetBy=0x%x)."), bValue, bValue, CacheEnabledSetBy);
	}

	const uint32 CacheBudgetFlags = CVarCacheMemoryBudgetMB_DEPRECATED->GetFlags();
	const uint32 CacheBudgetSetBy = (CacheBudgetFlags & ECVF_SetByMask);

	if (SetFromIni(CacheBudgetFlags, CacheBudgetSetBy))
	{
		const int32 Value = CVarCacheMemoryBudgetMB_DEPRECATED->GetInt();

		// Copy with the same SetBy priority so behavior matches the user's source of truth.
		CVarCacheMemoryBudgetRuntimeMB->Set(Value, (EConsoleVariableFlags)CacheBudgetSetBy);
		CVarCacheMemoryBudgetEditorMB->Set(Value, (EConsoleVariableFlags)CacheBudgetSetBy);

		UE_LOG(LogPCG, Warning, TEXT("Cvar 'pcg.Cache.MemoryBudgetMB' was set from ini but is deprecated in 5.7. Migrated value to pcg.Cache.Runtime.MemoryBudgetMB=%d, pcg.Cache.Editor.MemoryBudgetMB=%d (SetBy=0x%x)."), Value, Value, CacheBudgetSetBy);
	}
}

// Register a sink so this runs at the right time (after ini load / on first cvar change tick).
static FAutoConsoleVariableSink GCVarMigrationSink(FConsoleCommandDelegate::CreateStatic(&MigrateCVarsIfNeeded));
#endif // WITH_EDITOR
