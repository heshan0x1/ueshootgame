// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/NetSerializers.h"

namespace UE::Net
{

/**
 * FNopNetSerializer："什么都不做"的空 Serializer。
 *
 * 典型使用场景：
 *  - 某个成员在服务器上有意义但不需要同步到客户端（例如 AI 内部状态、服务器专用统计值）；
 *  - Replication 描述符中要求每个字段都有 Serializer 的占位情形；
 *  - 作为某些特殊 Property 的"禁用同步"标记（通过手动绑定到 FNopNetSerializerInfo）。
 *
 * 全部 Serialize/Deserialize/SerializeDelta/DeserializeDelta/Quantize/Dequantize 都是空函数，
 * 不消耗比特流；IsEqual 恒返回 true（"没有数据自然永远相等"），Validate 恒返回 true（无可校验内容）。
 *
 * SourceType 被定义为 void——本 Serializer 不碰 Source/Target 内存，适配宏展开时仅用于类型签名。
 */
struct FNopNetSerializer
{
	// 协议版本号。即便是 Nop，仍定义 Version 以满足 Builder 的 SFINAE 探测契约。
	static const uint32 Version = 0;

	typedef void SourceType;                     // Nop 不操作任何实际数据。
	typedef FNopNetSerializerConfig ConfigType;  // 空 Config 结构（声明于 NetSerializers.h）。

	// 以下六个函数体均为空：这是 Nop 的全部语义。
	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs&) {}
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs&) {}

	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs&) {}
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs&) {}

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs&) {}
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs&) {}

	// 两侧既然没有数据，判定永远相等、永远合法。
	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs&) { return true; }
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs&) { return true; }

	// DefaultConfig：仅为满足 Builder 的 GetDefaultConfig 要求，实际内容为空。
	static FNopNetSerializerConfig DefaultConfig;
};

FNopNetSerializerConfig FNopNetSerializer::DefaultConfig;

// 宏展开注册 FNopNetSerializerInfo 的 FNetSerializer 静态实例。
UE_NET_IMPLEMENT_SERIALIZER(FNopNetSerializer);

}
