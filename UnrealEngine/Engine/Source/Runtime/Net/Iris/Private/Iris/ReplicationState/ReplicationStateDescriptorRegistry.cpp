// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// ReplicationStateDescriptorRegistry.cpp —— Descriptor 缓存的 Find/Register/Prune 实现
// -------------------------------------------------------------------------------------
// 关键策略：
//   - "双重 GC 探针" —— WeakPtrForPruning + OwnerKey。
//     仅 WeakPtr 失效不一定立即清理（保守策略，方便 PIE 频繁加载/卸载）；
//     当 CVar net.Iris.PruneReplicationStateDescriptorsWithArchetype=1 时，OwnerKey
//     ResolveObjectPtr 失败也触发清理（更激进，避免 archetype 替换后还命中旧缓存）。
//   - 重复注册防御 —— 若 Key 已有"对同一 ObjectForPruning + 同一 OwnerKey"的有效条目，
//     视为编程错误（checkf 报错）；若条目已失效，则先 InvalidateDescriptors 通知
//     ProtocolManager 再覆盖。
// =====================================================================================

#include "Iris/ReplicationState/ReplicationStateDescriptorRegistry.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Iris/ReplicationSystem/ReplicationProtocolManager.h"
#include "Iris/Core/IrisProfiler.h"
#include "Iris/Core/IrisLog.h"
#include "HAL/ConsoleManager.h"

namespace UE::Net::Private
{

// 【CVar 控制】是否在 archetype 失效时清理。默认 true（推荐），更安全；
// 若怀疑有"误失效"问题可临时关闭，回退为仅 WeakPtr 失效才清理。
static bool bPruneReplicationStateDescriptorsWithArchetype = true;
static FAutoConsoleVariableRef CVarbPruneReplicationStateDescriptorsWithArchetype(TEXT("net.Iris.PruneReplicationStateDescriptorsWithArchetype"), bPruneReplicationStateDescriptorsWithArchetype, 
	TEXT("If true, we will invalidate registered descriptors if archetype is no longer resolvable, otherwise we will keep them around until CDO is no longer valid."));

FReplicationStateDescriptorRegistry::FReplicationStateDescriptorRegistry()
: ProtocolManager(nullptr)
{
}

void FReplicationStateDescriptorRegistry::Init(const FReplicationStateDescriptorRegistryInitParams& Params)
{
	ProtocolManager = Params.ProtocolManager;
}

// 【Register（多 Descriptor 版本）】注册 Class 路径产生的多个 Descriptor。
// 注意三种情形：
//   A) 不存在 Key：直接 emplace 新条目；
//   B) 存在 Key 且条目仍有效（WeakPtr 与 ObjectForPruning 一致 + OwnerKey 一致） →
//      属于"重复注册"，触发 checkf —— 这是开发期错误（不应出现）；
//   C) 存在 Key 但已无效（CDO 已变 / archetype 已变） → 先 Invalidate 通知 ProtocolManager
//      丢弃旧协议，再 Remove + Add 新条目。
void FReplicationStateDescriptorRegistry::Register(const FFieldVariant& DescriptorKey, const UObject* ObjectForPruning, const FDescriptors& Descriptors)
{
	check(ObjectForPruning != nullptr);

	// Make sure the object isn't already registered
	if (const FRegisteredDescriptors* Entry = RegisteredDescriptorsMap.Find(DescriptorKey))
	{
		// We do not want to overwrite descriptors for valid objects.
		if (Entry->WeakPtrForPruning.Get() == ObjectForPruning)
		{
			if (Entry->OwnerKey == FObjectKey(GetObjectForPruning(DescriptorKey)))
			{
				// 条件 B：完全相同的有效条目 → 编程错误。
				checkf(false, TEXT("FReplicationStateDescriptorRegistry::Trying to register descriptors for the same UObject %s"), ToCStr(ObjectForPruning->GetName()));
				return;
			}
		}

		// We found an invalid entry, invalidate it before registering new descriptors.
		// 条件 C：旧条目已失效，通知 ProtocolManager 后清理重建。
		UE_LOG(LogIris, VeryVerbose, TEXT("FReplicationStateDescriptorRegistry::Register invalidate descriptors for ptr: 0x%p"), DescriptorKey.GetRawPointer());

		// Notify protocol manager about pruned descriptors
		InvalidateDescriptors(Entry->Descriptors);
		RegisteredDescriptorsMap.Remove(DescriptorKey);
	}

	FRegisteredDescriptors NewEntry;
	NewEntry.OwnerKey = FObjectKey(GetObjectForPruning(DescriptorKey));
	NewEntry.WeakPtrForPruning = TWeakObjectPtr<const UObject>(ObjectForPruning);
	NewEntry.Descriptors = Descriptors;
	RegisteredDescriptorsMap.Add(DescriptorKey, NewEntry);
}

// 【Register（单 Descriptor 版本）】FProperty 路径常用——单属性映射单 Descriptor。
// 与多 Descriptor 版本逻辑相同，只是 Descriptors 数组只追加一项。
void FReplicationStateDescriptorRegistry::Register(const FFieldVariant& DescriptorKey, const UObject* ObjectForPruning, const TRefCountPtr<const FReplicationStateDescriptor>& Descriptor)
{
	check(ObjectForPruning != nullptr);

	// Make sure the descriptor isn't already registered
	if (const FRegisteredDescriptors* Entry = RegisteredDescriptorsMap.Find(DescriptorKey))
	{
		// We do not want to overwrite descriptors for valid objects.
		if (Entry->WeakPtrForPruning.Get() == ObjectForPruning)
		{
			if (Entry->OwnerKey == FObjectKey(GetObjectForPruning(DescriptorKey)))
			{
				checkf(false, TEXT("FReplicationStateDescriptorRegistry::Trying to register descriptor for the same UObject %s"), ToCStr(ObjectForPruning->GetName()));
				return;
			}
		}

		// We found an invalid entry, invalidate it before registering new descriptors.
		UE_LOG(LogIris, VeryVerbose, TEXT("FReplicationStateDescriptorRegistry::Register invalidate descriptor for ptr: 0x%p"), DescriptorKey.GetRawPointer());
		// Notify protocol manager about pruned descriptors
		InvalidateDescriptors(Entry->Descriptors);
		RegisteredDescriptorsMap.Remove(DescriptorKey);
	}

	FRegisteredDescriptors& NewEntry = RegisteredDescriptorsMap.Emplace(DescriptorKey);
	NewEntry.OwnerKey = FObjectKey(GetObjectForPruning(DescriptorKey));
	NewEntry.WeakPtrForPruning = TWeakObjectPtr<const UObject>(ObjectForPruning);
	NewEntry.Descriptors.Add(Descriptor);
}

// 【Find】查询缓存。命中条件：
//   1) Key 存在；
//   2) WeakPtrForPruning 仍指向调用方传入的 ObjectForPruning（防止 owner 已变）；
//   3) 若 Key 是 UObject，OwnerKey 仍可解析（archetype 未失效）。
//
// 注意：未命中条件 3 时只返回 nullptr，不立即清理（清理由 PruneStaleDescriptors 集中处理）；
// 但 Register 会在 owner 不一致时主动 Invalidate。
const FReplicationStateDescriptorRegistry::FDescriptors* FReplicationStateDescriptorRegistry::Find(const FFieldVariant& Object, const UObject* ObjectForPruning) const
{
	const FRegisteredDescriptors* Entry = RegisteredDescriptorsMap.Find(Object);

	check(ObjectForPruning != nullptr);

	if (Entry && (Entry->WeakPtrForPruning.Get() == ObjectForPruning))
	{
		// Archetype might have been reused, we will clear this up when registering
		// 如果 Key 是 UObject 但 OwnerKey 已无法解析 → 视为失效，返回 nullptr。
		// 真正的清理推迟到下一次 Register 或 PruneStaleDescriptors。
		if (Object.IsUObject() && !Entry->OwnerKey.ResolveObjectPtr())
		{
			UE_LOG(LogIris, VeryVerbose, TEXT("FReplicationStateDescriptorRegistry Found invalidated entry ptr: 0x%p"), Object.GetRawPointer());
			return nullptr;
		}

		return &Entry->Descriptors;
	}
	else
	{
		return nullptr;
	}
}

// 【PruneStaleDescriptors】PostGC 调用。两种触发清理：
//   - WeakPtr 已失效（CDO 已被 GC）；
//   - OwnerKey ResolveObjectPtr 失败（archetype 已替换/重建），仅当 CVar 启用时。
// 清理时通知 ProtocolManager 让其丢弃对应协议。
void FReplicationStateDescriptorRegistry::PruneStaleDescriptors()
{
	IRIS_PROFILER_SCOPE(FReplicationStateDescriptorRegistry_PruneStaleDescriptors);

	// Iterate over all registered descriptors and see if they have been destroyed
	for (auto It = RegisteredDescriptorsMap.CreateIterator(); It; ++It)
	{
		const FRegisteredDescriptors& RegisteredDescriptors = It.Value();
		const bool bPruneDueToWeakPtrForPruningBeingStale = !RegisteredDescriptors.WeakPtrForPruning.IsValid();
		if (bPruneDueToWeakPtrForPruningBeingStale || (bPruneReplicationStateDescriptorsWithArchetype && (RegisteredDescriptors.OwnerKey.ResolveObjectPtr() == nullptr)))
		{
			UE_LOG(LogIris, VeryVerbose, TEXT("FReplicationStateDescriptorRegistry Pruning descriptors for ptr: 0x%p due to %s"), It.Key().GetRawPointer(), (bPruneDueToWeakPtrForPruningBeingStale ? TEXT("invalidated CDO") : TEXT("invalidated Key/Archetype")));

			// Notify protocol manager about pruned descriptors
			InvalidateDescriptors(RegisteredDescriptors.Descriptors);
			It.RemoveCurrent();
		}
	}
}

// 【GetObjectForPruning】把 FFieldVariant 转换为"GC 探针 UObject"。
//   - UObject 直接当探针；
//   - FField（如 FProperty）→ 返回其 OwnerUObject（属性所属的 UStruct）。
const UObject* FReplicationStateDescriptorRegistry::GetObjectForPruning(const FFieldVariant& FieldVariant)
{
	if (FieldVariant.IsUObject())
	{
		return FieldVariant.ToUObject();
	}
	else
	{
		const FField* Field = FieldVariant.ToField();
		const UObject* Object = Field->GetOwnerUObject();
		return Object;
	}
}

// 【InvalidateDescriptors】通知 ProtocolManager 这些 Descriptor 已失效。
// 注：本函数只处理"协议层失效"——Descriptor 自身的引用计数仍由 TRefCountPtr 管理，
// 在 RegisteredDescriptorsMap.Remove 时自然释放最后一份引用。
void FReplicationStateDescriptorRegistry::InvalidateDescriptors(const FDescriptors& Descriptors) const
{
	if (ProtocolManager)
	{
		for (const TRefCountPtr<const FReplicationStateDescriptor>& Descriptor : MakeArrayView(Descriptors.GetData(), Descriptors.Num()))
		{
			ProtocolManager->InvalidateDescriptor(Descriptor.GetReference());
		}
	}
}

}
