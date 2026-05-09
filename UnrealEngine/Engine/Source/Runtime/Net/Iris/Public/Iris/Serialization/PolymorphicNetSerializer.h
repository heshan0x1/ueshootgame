// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// PolymorphicNetSerializer.h —— 多态 struct 序列化的"类型表"
// -----------------------------------------------------------------------------
// 用于 GAS 的 FGameplayAbilityTargetDataHandle / FGameplayEffectContextHandle 等
// "持有 TSharedPtr<BaseStruct> 的 polymorphic struct"。它们运行时实际类型未知，
// 但所有派生类都派生自一个共同基类 ScriptStruct。
//
// 核心：FPolymorphicNetSerializerScriptStructCache
//   * 在 PostFreeze 阶段 InitForType(BaseScriptStruct) 扫描所有 IsChildOf 派生类，
//     生成有序数组 RegisteredTypes[]（按 ScriptStruct 名小写降序排序，保证两端一致）；
//   * 每个 RegisteredTypes[i] 含 (ScriptStruct, RefCountPtr<Descriptor>);
//   * **TypeIndex 用 5 bit 编码（RegisteredTypeBits = 5）→ 上限 = 2^5 - 1 = 31 种派生类型**
//     （0 保留为 InvalidTypeIndex，所以实际可注册 [1, 31] 共 31 个，> 31 会 LowLevelFatalError）；
//   * GetTypeIndex(): 线性查找 ScriptStruct 在数组中的位置，返回 [1..31]，未找到返回 0。
//
// 与 InstancedStruct 的对比：
//   * Polymorphic 是"基类→派生白名单"，TypeIndex 编码紧凑（5 bit），适合 RPC 频繁传输；
//   * InstancedStruct 是"任意 UScriptStruct 都能装"，类型用 NetGUID/PackagePath 字符串编码，
//     不受 31 个派生上限限制但开销略大。
// =============================================================================

#pragma once

#include "NetSerializer.h"
#include "UObject/ObjectMacros.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Containers/Array.h"

#include "PolymorphicNetSerializer.generated.h"

class UScriptStruct;
namespace UE::Net
{
	struct FReplicationStateDescriptor;
}

namespace UE::Net
{

// FPolymorphicNetSerializerScriptStructCache ——
// 多态序列化的"类型注册表"。InitForType 一次性建立 BaseStruct 下所有派生类的类型表，
// 序列化时只需写 5-bit TypeIndex + 调对应 Descriptor 序列化即可。
struct FPolymorphicNetSerializerScriptStructCache
{
	// TypeIndex 编码所占位数。直接限定多态可注册类型数量上限。
	enum : uint32 { RegisteredTypeBits = 5U };
	// 5-bit 可表达 32 个值，0 保留 → 实际可注册 31 种派生类型。
	enum : uint32 { MaxRegisteredTypeCount = (1U << RegisteredTypeBits) - 1U };
	// 0 表示"没注册到该类型"（如收到未知报文 / 未注册派生类）。
	enum : uint32 { InvalidTypeIndex = 0U };

	// 单条注册记录：派生 ScriptStruct + 其 ReplicationStateDescriptor。
	struct FTypeInfo
	{
		const UScriptStruct* ScriptStruct;
		TRefCountPtr<const FReplicationStateDescriptor> Descriptor;
	};

	// 扫描所有 IsChildOf(InScriptStruct) 的派生类，烘焙 Descriptor，按名字排序入表。
	// 必须在 PostFreezeNetSerializerRegistry（或更晚）调用，因为 Descriptor 烘焙
	// 依赖 ReplicationState 子系统已初始化。
	IRISCORE_API void InitForType(const UScriptStruct* InScriptStruct);

	// 查询：ScriptStruct → TypeIndex（O(N) 线性扫描，N 最多 31）。
	// 未注册返回 InvalidTypeIndex (0)。
	inline const uint32 GetTypeIndex(const UScriptStruct* ScriptStruct) const;
	// 查询：TypeIndex → FTypeInfo*（O(1)）。无效返回 nullptr。
	inline const FTypeInfo* GetTypeInfo(uint32 TypeIndex) const;
	// 是否有任何派生类带 ObjectReference trait —— 用于跳过整批引用收集。
	inline bool CanHaveNetReferences() const { return EnumHasAnyFlags(CommonTraits, EReplicationStateTraits::HasObjectReference); }

private:
	// 注册的所有派生类（最多 31 个）。
	TArray<FTypeInfo> RegisteredTypes;
	// 所有派生类 trait 的并集中"我们关心的 trait"（目前只有 HasObjectReference）。
	EReplicationStateTraits CommonTraits = EReplicationStateTraits::None;
	// 优化用：估计 GetObjectsOfClass(UScriptStruct) 返回数量，第二次起预分配避免 realloc。
	int32 EstimatedScriptStructCount = 4096;
};

}

// 单值多态 Config（持有 1 个 polymorphic struct，如 FGameplayEffectContextHandle）。
USTRUCT()
struct FPolymorphicStructNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

	UE::Net::FPolymorphicNetSerializerScriptStructCache RegisteredTypes;
};

// 数组多态 Config（持有 TArray<TSharedPtr<polymorphic struct>>，如 FGameplayAbilityTargetDataHandle）。
USTRUCT()
struct FPolymorphicArrayStructNetSerializerConfig : public FPolymorphicStructNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{

// inline GetTypeInfo —— 边界检查 + 1-based 索引（0 是 Invalid）。
const FPolymorphicNetSerializerScriptStructCache::FTypeInfo* FPolymorphicNetSerializerScriptStructCache::GetTypeInfo(uint32 TypeIndex) const
{
	static_assert(InvalidTypeIndex == 0U, "Expected InvalidTypeIndex to be 0");
	// TypeIndex==0 → unsigned 减法 underflow → 大数 → 直接返回 nullptr。
	if ((TypeIndex - 1U) >= static_cast<uint32>(RegisteredTypes.Num()))
	{
		return nullptr;
	}
	return &RegisteredTypes.GetData()[TypeIndex - 1U];
}

// inline GetTypeIndex —— 线性查找。最坏 O(31)。
const uint32 FPolymorphicNetSerializerScriptStructCache::GetTypeIndex(const UScriptStruct* ScriptStruct) const
{
	if (ScriptStruct)
	{
		const int32 ArrayIndex =  RegisteredTypes.IndexOfByPredicate([&ScriptStruct](const FTypeInfo& TypeInfo) { return TypeInfo.ScriptStruct == ScriptStruct; } );
		if (ArrayIndex != INDEX_NONE)
		{
			// 1-based：避免 0 与 InvalidTypeIndex 冲突。
			return static_cast<uint32>(ArrayIndex + 1);
		}
	}

	return InvalidTypeIndex;
}

}
