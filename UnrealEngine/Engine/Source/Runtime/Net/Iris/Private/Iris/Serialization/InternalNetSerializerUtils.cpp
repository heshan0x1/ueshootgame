// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// InternalNetSerializerUtils.cpp —— "对象引用 Serializer 判别"实现
// -----------------------------------------------------------------------------
// IsStructNetSerializer / IsArrayPropertyNetSerializer 已在头文件 inline 实现。
// 这里只剩 IsObjectReferenceNetSerializer，因为它需要短路 OR 比较 4 个不同的
// singleton，体量略大，所以放 .cpp 而非 .h。
// =============================================================================

#include "Iris/Serialization/InternalNetSerializerUtils.h"

namespace UE::Net
{

// 判断给定 NetSerializer 是否为 4 种对象引用 Serializer 之一。
// 对应规则：在 ReplicationStateDescriptor 的引用收集、PIE 重映射、
// FObjectReferenceCache 解析路径上，这些 serializer 均会写出 NetGUID/Handle，
// 上层会按"对象引用语义"特殊处理（例如等待对象到达再 dequantize）。
//
// 注意：SoftObject* / RemoteObject* 系列虽然也涉及对象，但它们的解析、
// 异步加载、跨服远端语义不同，统一不算入此谓词。
bool IsObjectReferenceNetSerializer(const FNetSerializer* NetSerializer)
{
	bool bIsObjectReferenceNetSerializer = false;
	// 标准 UObject* 属性
	bIsObjectReferenceNetSerializer = bIsObjectReferenceNetSerializer || (NetSerializer == &UE_NET_GET_SERIALIZER(FObjectNetSerializer));
	// TObjectPtr<T>
	bIsObjectReferenceNetSerializer = bIsObjectReferenceNetSerializer || (NetSerializer == &UE_NET_GET_SERIALIZER(FObjectPtrNetSerializer));
	// TWeakObjectPtr<T>
	bIsObjectReferenceNetSerializer = bIsObjectReferenceNetSerializer || (NetSerializer == &UE_NET_GET_SERIALIZER(FWeakObjectNetSerializer));
	// TScriptInterface<I>
	bIsObjectReferenceNetSerializer = bIsObjectReferenceNetSerializer || (NetSerializer == &UE_NET_GET_SERIALIZER(FScriptInterfaceNetSerializer));
	return bIsObjectReferenceNetSerializer;
}

}


