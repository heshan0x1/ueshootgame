// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// InternalNetSerializers.cpp —— 内部 Config 类型的非 inline 实现
// -----------------------------------------------------------------------------
// 大部分 Config 都是 POD-like，不需要构造/析构。这里只为
// FArrayPropertyNetSerializerConfig 提供一对显式构造/析构：
//   * 构造：标记 ConfigTraits = NeedDestruction，让 Iris 协议销毁路径
//     在不再需要 Config 时主动调用析构（释放 RefCountPtr<Descriptor>）；
//   * 析构：默认（智能指针自动释放）。
// =============================================================================

#include "Iris/Serialization/InternalNetSerializers.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"

// UHT 生成的 USTRUCT 反射代码（USTRUCT 反射元数据、序列化、构造表等）。
#include UE_INLINE_GENERATED_CPP_BY_NAME(InternalNetSerializers)

// FArrayPropertyNetSerializerConfig 必须显式声明 NeedDestruction，否则
// 协议销毁时不会调到析构，TRefCountPtr<Descriptor> 永远不释放（泄漏）。
FArrayPropertyNetSerializerConfig::FArrayPropertyNetSerializerConfig()
: FNetSerializerConfig()
{
	ConfigTraits = ENetSerializerConfigTraits::NeedDestruction;
}

// 默认析构：让 TRefCountPtr<FReplicationStateDescriptor> 走 RAII 释放。
FArrayPropertyNetSerializerConfig::~FArrayPropertyNetSerializerConfig() = default;
