// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// InternalReplicationStateDescriptorUtils.h —— ReplicationState 模块内部便捷判断工具
// ---------------------------------------------------------------------------------------------------------------------
// 文件位置：Private/Iris/ReplicationState/
// 用途：Iris 内部需要频繁判断"某个成员到底用的是 FStructNetSerializer 还是 FArrayPropertyNetSerializer"
//       因为这两类 Serializer 在 Builder/Quantize/Apply 等代码路径中需要走特殊分支
//       （例如 StructNetSerializer 内部递归含子 Descriptor、ArrayPropertyNetSerializer 维护元素 changemask）。
//
// 这里只是把 Serialization 层 InternalNetSerializerUtils.h 提供的 IsStructNetSerializer / IsArrayPropertyNetSerializer
// 用 ReplicationState 一侧熟悉的"成员描述符（FReplicationStateMemberSerializerDescriptor）"参数做了一层薄包装，
// 让调用者不必再去触摸 .Serializer 成员，统一在 ReplicationState 命名空间内使用。
//
// 注意：此文件位于 Private/，仅供 IrisCore 模块自身的 .cpp 引用，不属于对外 API。
// =====================================================================================================================

#pragma once

#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Iris/Serialization/InternalNetSerializerUtils.h"

namespace UE::Net
{

// 判断指定成员是否使用 FStructNetSerializer（结构体序列化器）。
// 当成员是嵌套 USTRUCT 且没有自定义 NetSerializer 时，Builder 会为其分配 FStructNetSerializer，
// 此时 Quantize/Dequantize/Apply 需要按 sub-Descriptor 递归处理。
bool IsUsingStructNetSerializer(const FReplicationStateMemberSerializerDescriptor& MemberDescriptor);

// 判断指定成员是否使用 FArrayPropertyNetSerializer（动态数组序列化器，对应 TArray<T>）。
// 该 Serializer 内部会管理 element changemask，CompareAndCopy 路径需要单独处理。
bool IsUsingArrayPropertyNetSerializer(const FReplicationStateMemberSerializerDescriptor& MemberDescriptor);

}

namespace UE::Net
{

// 内联实现：直接转发到 Serialization 层的 IsStructNetSerializer。
// 该函数底层通常是把传入的 FNetSerializer 指针与 GStructNetSerializer 全局实例比较。
inline bool IsUsingStructNetSerializer(const FReplicationStateMemberSerializerDescriptor& MemberDescriptor)
{
	return IsStructNetSerializer(MemberDescriptor.Serializer);
}

// 内联实现：直接转发到 Serialization 层的 IsArrayPropertyNetSerializer。
// 与上面同理，对比的是 GArrayPropertyNetSerializer 全局实例。
inline bool IsUsingArrayPropertyNetSerializer(const FReplicationStateMemberSerializerDescriptor& MemberDescriptor)
{
	return IsArrayPropertyNetSerializer(MemberDescriptor.Serializer);
}

}
