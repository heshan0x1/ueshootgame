// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// InternalNetSerializerUtils.h —— Iris 内部 NetSerializer 判别工具
// -----------------------------------------------------------------------------
// 提供三个"指针相等"判别函数，用于在通用算法（StructNetSerializer / Array /
// 引用收集）中识别某个 FNetSerializer* 是否属于以下"特殊处理类":
//   1. IsStructNetSerializer     —— 是否为复合 struct（容器型，递归 dispatch）
//   2. IsArrayPropertyNetSerializer —— 是否为 TArray 属性 serializer
//   3. IsObjectReferenceNetSerializer —— 是否为四种对象引用 serializer 之一
//
// 实现要点：
//   * 由于每种 NetSerializer 通过 UE_NET_DECLARE/IMPLEMENT_SERIALIZER 宏在全局
//     有且仅有唯一一份 FNetSerializer 实例（singleton 静态变量），因此可以直接
//     通过指针比较来判定类型，开销 O(1) 且无 RTTI 依赖。
//   * 仅 IsObjectReferenceNetSerializer 需要 IRISCORE_API 导出，因为对象引用
//     的特殊处理跨模块均可能用到（例如 PIE 重映射、引用收集 collector）。
// =============================================================================

#pragma once

#include "Iris/Serialization/InternalNetSerializers.h"
#include "Iris/Serialization/NetSerializers.h"

namespace UE::Net
{

// 判断给定的 NetSerializer 是否就是 FStructNetSerializer。
// StructNetSerializer 在 Iris 中对应"嵌套结构体"的递归序列化入口，
// 在 ArrayProperty/InstancedStruct/Polymorphic/StructNetSerializer 自身的
// SerializeDelta 等处都需要用此判别来"特殊提前 IsEqual 短路 + 写一位 bSame"。
bool IsStructNetSerializer(const FNetSerializer* Serializer);

// 判断给定的 NetSerializer 是否就是 FArrayPropertyNetSerializer。
// 在 ChangeMask 计算、Apply/RepNotify、引用收集等处可能需要识别 TArray，
// 以便走数组特定的迭代逻辑（元素 ChangeMask、动态扩缩容）。
bool IsArrayPropertyNetSerializer(const FNetSerializer* Serializer);

// 判断给定的 NetSerializer 是否为四种"对象引用"序列化器之一：
//   FObjectNetSerializer / FObjectPtrNetSerializer /
//   FWeakObjectNetSerializer / FScriptInterfaceNetSerializer
// 由于 SoftObject* / RemoteObject* 走另外的解析路径，这里不算入对象引用集合。
// 该函数在 .cpp 中实现并通过 IRISCORE_API 跨模块导出。
IRISCORE_API bool IsObjectReferenceNetSerializer(const FNetSerializer* Serializer);

}

namespace UE::Net
{

// IsStructNetSerializer 内联实现 —— 直接和 FStructNetSerializer 的全局
// singleton（由 UE_NET_GET_SERIALIZER 宏展开为静态实例引用）做指针相等比较。
inline bool IsStructNetSerializer(const FNetSerializer* Serializer)
{
	return Serializer == &UE_NET_GET_SERIALIZER(FStructNetSerializer);
}

// IsArrayPropertyNetSerializer 内联实现 —— 同样的指针比较，但比较的是
// FArrayPropertyNetSerializer 的全局 singleton。
inline bool IsArrayPropertyNetSerializer(const FNetSerializer* Serializer)
{
	return Serializer == &UE_NET_GET_SERIALIZER(FArrayPropertyNetSerializer);
}

}
