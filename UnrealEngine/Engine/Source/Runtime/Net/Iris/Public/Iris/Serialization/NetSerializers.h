// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// =============================================================================================
// NetSerializers.h
// ---------------------------------------------------------------------------------------------
// "聚合头"：批量 include Iris 自带的各类基本数据 NetSerializer 的对外头，并集中声明三个
// "通用"序列化器（Bool / Struct / Nop）的 Config。
//
// 角色：
//   - 业务代码只需要 #include "Iris/Serialization/NetSerializers.h" 就能拿到所有官方
//     NetSerializer 的对外类型/Config，避免逐个 include 散落的头。
//   - 同时通过 USTRUCT 把 FBoolNetSerializerConfig / FStructNetSerializerConfig /
//     FNopNetSerializerConfig 暴露给 UHT，使其可以作为 ReplicationStateDescriptor 中的
//     成员被反射访问。
//
// 注意点：
//   - FStructNetSerializerConfig 持有 TRefCountPtr<const FReplicationStateDescriptor>，
//     生命周期跟随 Config；显式删了拷贝/移动构造与赋值，并通过 TStructOpsTypeTraits 关闭
//     WithCopy（避免 UHT/UObject 系统误用按值复制）。析构函数定义在 cpp 中，避免头文件
//     强依赖 ReplicationStateDescriptor 的完整定义。
//   - FNopNetSerializerConfig 的 Serializer 为"全 no-op"，用于把成员加入复制状态描述符
//     却不消耗带宽——给 Filtering / Prioritization 等系统提供"按 RepTag 访问源字段"的能力。
//
// 与文档对照：Docs/Iris_Architecture.md §3.5 末段；Docs/Modules/Serialization.md §1.1
//   "NetSerializer 体系总览"。
// =============================================================================================

#include "NetSerializer.h"
#include "EnumNetSerializers.h"
#include "FloatNetSerializers.h"
#include "GuidNetSerializer.h"
#include "InstancedStructNetSerializer.h"
#include "IntNetSerializers.h"
#include "IntRangeNetSerializers.h"
#include "IntNetSerializers.h"
#include "ObjectNetSerializer.h"
#include "PackedIntNetSerializers.h"
#include "PackedVectorNetSerializers.h"
#include "QuatNetSerializers.h"
#include "RotatorNetSerializers.h"
#include "SoftObjectNetSerializers.h"
#include "StringNetSerializers.h"
#include "UintNetSerializers.h"
#include "UintRangeNetSerializers.h"
#include "VectorNetSerializers.h"
#include "NetSerializers.generated.h"

namespace UE::Net
{
	struct FReplicationStateDescriptor;
}

/**
 * Bool NetSerializer 的 Config——空结构体即可，反射存在的目的是被 ReplicationStateDescriptor
 * 引用。Bool 序列化只需要 1 bit，无任何可调参数。
 */
USTRUCT()
struct FBoolNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

/**
 * Struct NetSerializer 的 Config——持有一个 ReplicationStateDescriptor 引用，
 * 描述目标 USTRUCT 的字段布局、各字段对应的子 NetSerializer 等。
 *
 * 显式禁用拷贝/移动：因为 StateDescriptor 是 RefCounted 的"重资源"，意外按值复制会带来
 * 难以追踪的引用计数错误；上层应当通过 NetSerializer 注册流程构造唯一实例并按引用使用。
 */
USTRUCT()
struct FStructNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

	/** 默认构造：把 ConfigTraits 设为 NeedDestruction，确保框架析构链会调用我们的析构释放 RefCount。 */
	IRISCORE_API FStructNetSerializerConfig();
	/** 默认析构：实现在 cpp，避免头文件强依赖 ReplicationStateDescriptor。 */
	IRISCORE_API ~FStructNetSerializerConfig();

	FStructNetSerializerConfig(const FStructNetSerializerConfig&) = delete;
	FStructNetSerializerConfig(FStructNetSerializerConfig&&) = delete;

	FStructNetSerializerConfig& operator=(const FStructNetSerializerConfig&) = delete;
	FStructNetSerializerConfig& operator=(FStructNetSerializerConfig&&) = delete;

	/** 关联的复制状态描述符——决定 Struct 各字段如何复制。RefCount 由 TRefCountPtr 管理。 */
	TRefCountPtr<const UE::Net::FReplicationStateDescriptor> StateDescriptor;
};

/**
 * 关闭 UHT 默认拷贝行为：对应上面 = delete 的拷贝构造/赋值。
 * 不写这一段会导致 UStruct CopyTaggedProperties 等路径尝试按值复制并破坏 RefCount。
 */
template<>
struct TStructOpsTypeTraits<FStructNetSerializerConfig> : public TStructOpsTypeTraitsBase2<FStructNetSerializerConfig>
{
	enum
	{
		WithCopy = false
	};
};

/**
 * The NopNetSerializer have all the serializer functions implemented
 * as no-ops. The main purpose of this serializer is to allow adding
 * a non-replicated member as part of a ReplicationStateDescriptor
 * without incurring a bandwidth cost. This allows systems, such as 
 * prioritization and filtering, to access the source data given an
 * instance protocol and information regarding the member, for example
 * by looking for a particular RepTag.
 *
 * NopNetSerializer：所有序列化函数都是空操作。其主要价值在于：把"非复制成员"也写进
 * ReplicationStateDescriptor 的字段表里却不消耗任何带宽。这样像优先级（Prioritization）
 * 和过滤（Filtering）等系统就能借助 instance protocol 与字段元信息（如 RepTag）直接访问
 * 该成员的源数据，而不必把它真的发送到对端。
 */
USTRUCT()
struct FNopNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{

// 三个通用 NetSerializer 的对外声明。配合上面的 USTRUCT Config 和 cpp 中的 IMPLEMENT 完成注册。
UE_NET_DECLARE_SERIALIZER(FBoolNetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FStructNetSerializer, IRISCORE_API);
UE_NET_DECLARE_SERIALIZER(FNopNetSerializer, IRISCORE_API);

}
