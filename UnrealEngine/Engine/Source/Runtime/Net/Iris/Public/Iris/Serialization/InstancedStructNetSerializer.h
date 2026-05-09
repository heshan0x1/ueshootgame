// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// InstancedStructNetSerializer.h —— FInstancedStruct 容器序列化
// -----------------------------------------------------------------------------
// FInstancedStruct（来自 StructUtils 模块）是 UE 中"持有任意 UScriptStruct 实例"
// 的容器（类似 std::any over UScriptStruct）。Iris 通过 FInstancedStructNetSerializer
// 提供其网络序列化：
//   * 量化时记录"struct 类型 + 嵌套量化 buffer"两段；
//   * 反量化时根据类型 InitializeAs 实例化具体 UScriptStruct 再 Dequantize；
//   * 类型解析使用 FObjectNetSerializer（对 UScriptStruct 的 NetGUID 引用）。
//
// 与多态 PolymorphicNetSerializer 的关键区别：
//   * Polymorphic: 静态注册的"基类→派生类型"白名单（最多 31 个，5-bit TypeIndex）；
//   * Instanced  : 动态—— 任意 UScriptStruct 都允许（白名单可选），
//                  类型作为 UObject* 直接序列化（NetGUID/Path）。
//
// 关键扩展点：
//   * SupportedTypes（白名单 TArray<TSoftObjectPtr<UScriptStruct>>，空表示全允许）
//   * FInstancedStructDescriptorCache —— LRU 缓存，按 struct path FName 索引
//     ReplicationStateDescriptor，限制内存占用（默认 8 项 LRU；CVar 可调）。
// =============================================================================

#pragma once

#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Iris/Serialization/NetSerializer.h"
#include "Containers/LruCache.h"
#include "UObject/ObjectMacros.h"
#include "Misc/TransactionallySafeCriticalSection.h"

#include "InstancedStructNetSerializer.generated.h"

class UScriptStruct;

namespace UE::Net::Private
{

// FInstancedStructDescriptorCache ——
// 一个线程安全的 "struct path → ReplicationStateDescriptor" LRU/Map 缓存。
// 因为 InstancedStruct 的实际 struct 类型是动态决定的，每次量化/反量化都
// 需要拿对应 Descriptor；缓存避免重复烘焙开销（CreateDescriptorForStruct 较贵）。
//
// 双存储模式：
//   * MaxCachedDescriptorCount > 0  → 使用 LRU（限大小，超出按最近最少使用淘汰）；
//   * MaxCachedDescriptorCount <= 0 → 使用普通 Map（无限增长，适合白名单内有限种类）。
class FInstancedStructDescriptorCache
{
public:
	FInstancedStructDescriptorCache();
	~FInstancedStructDescriptorCache();

	// Name for debugging purposes
	// 设置调试名（一般是"OuterClass.PropertyName"），LRU 满日志/泄漏排查用。
	void SetDebugName(const FString& DebugName);

	// Set max cached descriptor count. The most recently used descriptors will be kept. MaxCount <= 0 means no limit which is the default.
	// 设置 LRU 容量。<=0 表示无限（用 DescriptorMap 而非 LruCache）。
	void SetMaxCachedDescriptorCount(int32 MaxCount);

	// 添加白名单（TSoftObjectPtr 形式，运行时延迟加载）。空白名单表示"接受任意 UScriptStruct"。
	void AddSupportedTypes(const TConstArrayView<TSoftObjectPtr<UScriptStruct>>& SupportedTypes);

	// 当 SupportedTypes 非空时，仅当 Struct IsChildOf 任一白名单项才返回 true。
	bool IsSupportedType(const UScriptStruct* Struct) const;

	// Find descriptor for struct with fully qualified name.
	// 按 struct fully-qualified path（FName） 查找。LRU 模式下命中会触碰更新 LRU。
	TRefCountPtr<const FReplicationStateDescriptor> FindDescriptor(FName StructPath);

	// Find descriptor for struct.
	// 按 UScriptStruct* 查找（内部用 path 索引）。
	TRefCountPtr<const FReplicationStateDescriptor> FindDescriptor(const UScriptStruct* Struct);

	// Find or create descriptor for struct with fully qualified name.
	// 命中即返回；未命中则 StaticLoadObject 解析后调用 CreateAndCacheDescriptor。
	TRefCountPtr<const FReplicationStateDescriptor> FindOrAddDescriptor(FName StructPath);

	// Find or create descriptor for struct.
	// 命中即返回；未命中先 IsSupportedType 校验白名单，再烘焙缓存。
	TRefCountPtr<const FReplicationStateDescriptor> FindOrAddDescriptor(const UScriptStruct* Struct);

private:
	// 真正的"烘焙 + 加锁插入缓存"实现。被两类 FindOrAddDescriptor 共用。
	TRefCountPtr<const FReplicationStateDescriptor> CreateAndCacheDescriptor(const UScriptStruct* Struct, FName StructPath);

	// 多线程保护：可能在并行 Quantize 任务下被多线程访问。
	FTransactionallySafeCriticalSection Mutex;
	// LRU cache for descriptors for limited descriptor counts.
	// 仅在 MaxCachedDescriptorCount > 0 时使用。
	TLruCache<FName, TRefCountPtr<const FReplicationStateDescriptor>> DescriptorLruCache;
	// Map struct name -> FReplicationStateDescriptor for unlimited descriptor counts. 
	// 仅在 MaxCachedDescriptorCount <= 0 时使用。
	TMap<FName, TRefCountPtr<const FReplicationStateDescriptor>> DescriptorMap;
	// Supported types. An empty set indicates all UScriptStructs are supported.
	// 白名单（IsChildOf 派生检查），空集 = 全部允许。
	TSet<TSoftObjectPtr<UScriptStruct>> SupportedTypes;
	FString DebugName;
	int32 MaxCachedDescriptorCount = 0;
};

}

// FInstancedStructNetSerializerConfig ——
// 内部含 SupportedTypes（UPROPERTY，反射可见以便编辑器编辑）+ DescriptorCache。
// SupportedTypes 通常由属性级注册流程填充；运行时缓存由 DescriptorCache 持有。
USTRUCT()
struct FInstancedStructNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

	// 构造时设置 ConfigTraits = NeedDestruction，确保协议销毁时调到析构。
	FInstancedStructNetSerializerConfig();
	~FInstancedStructNetSerializerConfig();

	// 禁止拷贝（UPROPERTY 默认 ops 会浅拷 RefCountPtr，破坏生命周期）。
	FInstancedStructNetSerializerConfig(const FInstancedStructNetSerializerConfig&) = delete;
	FInstancedStructNetSerializerConfig& operator=(const FInstancedStructNetSerializerConfig&) = delete;

	// The property is for serialization support. We store the supported types differently in the descriptor cache.
	// 反射可见的白名单。注意运行时缓存内部还会复制一份到 DescriptorCache.SupportedTypes。
	UPROPERTY()
	TArray<TSoftObjectPtr<UScriptStruct>> SupportedTypes;

	// 真正使用的 LRU 缓存。非 UPROPERTY()，纯运行时。
	UE::Net::Private::FInstancedStructDescriptorCache DescriptorCache;
};

template<>
struct TStructOpsTypeTraits<FInstancedStructNetSerializerConfig> : public TStructOpsTypeTraitsBase2<FInstancedStructNetSerializerConfig>
{
	enum
	{
		WithCopy = false
	};
};

namespace UE::Net
{

// FInstancedStructNetSerializer 全局 singleton 声明（实际实现在 .cpp）。
UE_NET_DECLARE_SERIALIZER(FInstancedStructNetSerializer, IRISCORE_API);

}
