// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// ReplicationStateDescriptorRegistry.h —— Descriptor 共享缓存（按 FFieldVariant 索引）
// -------------------------------------------------------------------------------------
// 【职责】在 FReplicationStateDescriptorBuilder 烘焙过程中复用已生成的 Descriptor，
// 避免对同一 UClass / UStruct / UFunction / FProperty 重复烘焙。
//
// 【键值设计】
//   Key   : FFieldVariant —— 可同时指代 UObject*（UClass/UStruct/UFunction）或
//                            FField*（FProperty/FStructProperty/...），覆盖反射的两种世界。
//   Value : FRegisteredDescriptors {
//             - WeakPtrForPruning：弱引用一个 UObject 用作"GC 探针"
//                                  （通常是 CDO 或 Field 的 OwnerUObject）；
//             - Descriptors      ：该 Key 对应的所有 Descriptor（FastArray 类可能有多个）；
//             - OwnerKey         ：FObjectKey，二级 GC 探针；
//                                  即使 WeakPtr 还活着，但若 owner archetype 已被替换/重建，
//                                  也会触发失效（受 CVar net.Iris.PruneReplicationStateDescriptorsWithArchetype 控制）。
//           }
//
// 【生命周期】
//   - PostGarbageCollect 触发 PruneStaleDescriptors —— 把 weak/owner 已失效的条目移除，
//     并通知 ProtocolManager InvalidateDescriptor（让协议管理器丢弃对应协议）；
//   - 重新使用 Class 时若发现旧条目失效（owner 已变），先 InvalidateDescriptors 再 Add 新条目，
//     避免协议悬挂。
//
// 【与 ProtocolManager 的协作】Registry 仅缓存 Descriptor 自身；
//   ProtocolManager 持有 Descriptor 数组组成的 Protocol，用 Registry 失效通知来同步丢弃。
// =====================================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Templates/RefCounting.h"
#include "UObject/Field.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/ObjectKey.h"

namespace UE::Net
{
	struct FReplicationStateDescriptor;
	namespace Private
	{
		class FReplicationProtocolManager;
	}
}

namespace UE::Net::Private
{

// Init 时由调用方传入 ProtocolManager，用于失效通知。
struct FReplicationStateDescriptorRegistryInitParams
{
	FReplicationProtocolManager* ProtocolManager;
};

// 【FReplicationStateDescriptorRegistry】Iris 内部的 Descriptor 缓存。
// 注意它是 ReplicationSystem 级别的（每个 ReplicationSystem 实例自有一个）；
// 静态 Descriptor（手写）不会经过这里。
class FReplicationStateDescriptorRegistry
{
public:
	FReplicationStateDescriptorRegistry();

	void Init(const FReplicationStateDescriptorRegistryInitParams& Params);

	/** We allow for multiple descriptors to be created by a single class */
	// 一个 Class 可能拆出多个 Descriptor（普通 / Init / FastArray 各一组）—— 因此值是数组。
	typedef TArray<TRefCountPtr<const FReplicationStateDescriptor>> FDescriptors;

	/** Find registered Descriptors for Object */
	// 主查询：Object 是缓存键（UClass/Struct/Function/Property），ObjectForPruning 是
	// "用于 GC 探针的对象"（通常 = CDO 或 OwnerUObject）。
	// 返回 nullptr 表示未缓存或缓存已失效。
	const FDescriptors* Find(const FFieldVariant& Object, const UObject* ObjectForPruning) const;

	/** Find registered Descriptors for Object */
	// 便捷重载：ObjectForPruning 自动取 GetObjectForPruning(Object)。
	const FDescriptors* Find(const FFieldVariant& Object) const { return Find(Object, GetObjectForPruning(Object)); }

	/** Register created Descriptors for ClassObject */
	// 注册一组 Descriptor。若该 Key 已有失效条目则先调用 InvalidateDescriptors 通知 ProtocolManager 再覆盖。
	// 若已有"有效条目"则触发 checkf 报错（重复注册视为编程错误）。
	void Register(const FFieldVariant& DescriptorKey, const UObject* ObjectForPruning, const FDescriptors& Descriptors);

	/** Register created Descriptors for ClassObject */
	void Register(const FFieldVariant& DescriptorKey, const FDescriptors& Descriptors) { Register(DescriptorKey, GetObjectForPruning(DescriptorKey), Descriptors); }

	/** Register a single Descriptor for ClassObject */
	// 单 Descriptor 重载（FProperty 路径常用：单属性 = 单 Descriptor）。
	void Register(const FFieldVariant& DescriptorKey, const UObject* ObjectForPruning, const TRefCountPtr<const FReplicationStateDescriptor>& Descriptor);

	/** Register a single Descriptor for ClassObject */
	void Register(const FFieldVariant& DescriptorKey, const TRefCountPtr<const FReplicationStateDescriptor>& Descriptor) { Register(DescriptorKey, GetObjectForPruning(DescriptorKey), Descriptor); }

	/** Invoked on PostGarbageCollect to prune descriptors for stale weak ptrs */
	// PostGC 钩子：把 GC 已回收的 owner 对应的缓存条目清理掉，并通知 ProtocolManager。
	void PruneStaleDescriptors();

private:
	// 取出 FFieldVariant 对应的"GC 探针 UObject"。
	// - UObject  → 返回自身；
	// - FField   → 返回其 OwnerUObject（FProperty 的 owner UStruct）。
	static const UObject* GetObjectForPruning(const FFieldVariant& Object);

	// 通知 ProtocolManager 失效一组 Descriptor（让其丢弃缓存的协议）。
	void InvalidateDescriptors(const FDescriptors& Descriptors) const;

	// 单条缓存值。三个字段构成"双重 GC 探针"：
	//   - WeakPtrForPruning：直接弱引用 ObjectForPruning（CDO 或 owner）；
	//   - OwnerKey         ：用 FObjectKey 二次锁定，防 archetype 重用被误命中。
	struct FRegisteredDescriptors
	{
		TWeakObjectPtr<const UObject> WeakPtrForPruning;
		FDescriptors Descriptors;
		FObjectKey OwnerKey;
	};

	typedef TMap<FFieldVariant, FRegisteredDescriptors> FClassToDescriptorMap;
	FClassToDescriptorMap RegisteredDescriptorsMap;
	FReplicationProtocolManager* ProtocolManager;
};

}
