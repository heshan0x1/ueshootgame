// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NetSerializer.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "UObject/ObjectMacros.h"

#include "ObjectNetSerializer.generated.h"

namespace UE::Net
{
	class FNetObjectReference;
}

/**
 * FObjectNetSerializerConfig —— UObject 引用系列 Serializer 的通用配置基类。
 * 成员 PropertyClass 记录 FObjectProperty/FClassProperty/FWeakObjectProperty... 反射时的声明类型，
 * 反量化（Dequantize）阶段会据此做 `UObject::IsA(PropertyClass)` 校验：
 *   - 若服务端发来的对象不是该类及其子类，视为"伪造"(Forged) 对象，置为 nullptr 并发出警告；
 *   - 防止通过网络强制注入非法类型对象。
 */
USTRUCT()
struct FObjectNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()

	// 声明 Property 时的 UClass*（FObjectPropertyBase::PropertyClass），用于接收端合法性校验。
	UPROPERTY()
	TObjectPtr<UClass> PropertyClass = nullptr;
};

/** FObjectPtrNetSerializerConfig —— 供 TObjectPtr<T> 属性使用，配置结构与基类完全一致。 */
USTRUCT()
struct FObjectPtrNetSerializerConfig : public FObjectNetSerializerConfig
{
	GENERATED_BODY()
};

/** FWeakObjectNetSerializerConfig —— 供 TWeakObjectPtr<T> 属性使用，配置结构与基类完全一致。 */
USTRUCT()
struct FWeakObjectNetSerializerConfig : public FObjectNetSerializerConfig
{
	GENERATED_BODY()
};

/**
 * FScriptInterfaceNetSerializerConfig —— 供 TScriptInterface<I> 属性使用。
 * PropertyClass 此时存放"接口类"(UClass*)，在反量化时调用 UObject::GetInterfaceAddress() 绑定 vtable 入口。
 */
USTRUCT()
struct FScriptInterfaceNetSerializerConfig : public FObjectNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{

/**
 * 通用工具函数：从 BitStream 读取一个可能无效的 FNetRefHandle（1 bit 标志 + Packed Uint64）。
 * 供 Iris 其它需要手工序列化句柄的 Serializer 调用（而非仅 UObject*）。
 */
IRISCORE_API FNetRefHandle ReadNetRefHandle(FNetSerializationContext& Context);
/**
 * 通用工具函数：把一个 FNetRefHandle 写入 BitStream。Invalid Handle 仅写 1 个 false bit，零开销。
 * 有效 Handle 使用 PackedUint64 紧凑编码 NetId（常见小 id 只需 8 bits）。
 */
IRISCORE_API void WriteNetRefHandle(FNetSerializationContext& Context, const FNetRefHandle Handle);

/**
 * 通用工具函数：读取一条"完整"的 FNetObjectReference。
 * 与普通 ReadReference 的区别是：不依赖对端已导出过该引用，内联完整路径信息，
 * 便于在 InlineExports 场景（例如 RPC Payload、Polymorphic Struct 嵌套）使用。
 */
IRISCORE_API void ReadFullNetObjectReference(FNetSerializationContext& Context, FNetObjectReference& Reference);
/** 通用工具函数：写出完整内联的 FNetObjectReference。具体由 FObjectReferenceCache::WriteFullReference 实现。 */
IRISCORE_API void WriteFullNetObjectReference(FNetSerializationContext& Context, const FNetObjectReference& Reference);

// 声明 FObjectNetSerializer —— 对应原生 UObject* 指针属性。（详见 ObjectNetSerializer.cpp）
UE_NET_DECLARE_SERIALIZER(FObjectNetSerializer, IRISCORE_API);
// 声明 FObjectPtrNetSerializer —— 对应 TObjectPtr<T> 属性，在远程对象场景支持 Remote Reference 序列化路径。
UE_NET_DECLARE_SERIALIZER(FObjectPtrNetSerializer, IRISCORE_API);
// 声明 FWeakObjectNetSerializer —— 对应 TWeakObjectPtr<T> 属性；反量化时会转为弱引用避免阻止 GC。
UE_NET_DECLARE_SERIALIZER(FWeakObjectNetSerializer, IRISCORE_API);
// 声明 FScriptInterfaceNetSerializer —— 对应 TScriptInterface<I>；额外处理接口地址绑定。
UE_NET_DECLARE_SERIALIZER(FScriptInterfaceNetSerializer, IRISCORE_API);

/**
 * FObjectNetSerializerQuantizedReferenceStorage
 *
 * 供外部 forwarding serializer（嵌套/转发）使用的公开"量化态存储类型"。
 *   - 24 字节 + 8 字节对齐，正好可容纳一份 FQuantizedObjectReference（在 .cpp 中 static_assert 校验）。
 *   - 内部字段对外隐藏；只有 UObject 系列 Serializer 才能解释其语义。
 *   - 注意：只在"需要把一个对象引用当作量化子块嵌入另一个量化态"时才应用到。
 */
struct FObjectNetSerializerQuantizedReferenceStorage
{
	alignas(8) uint8 Storage[24]; // 量化存储缓冲区：兼容 FQuantizedObjectReference 布局
};

}
