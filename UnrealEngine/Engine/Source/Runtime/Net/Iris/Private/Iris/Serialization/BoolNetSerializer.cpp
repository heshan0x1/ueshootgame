// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"

namespace UE::Net
{

/**
 * FBoolNetSerializer：单比特布尔 Serializer。
 *
 * 量化策略：无量化——Source 与 Quantized 都是 uint8，值语义为 "0=假，非零=真"；
 * 写出时只占 1 bit，读回后同样只有 0/1 两种状态。
 *
 * 关键设计：
 *  - SourceType 使用 uint8 而非 bool：某些编译器假设 bool 只能是 0/1，未初始化的 bool 变量
 *    值可能是 2/3 等"伪真"；用 uint8 避免 UB 并可在 Validate 中主动捕获此类错误。
 *  - bUseDefaultDelta=false：一个 bit 的"差分"毫无意义，跳过默认 Delta 路径（SFINAE Builder
 *    会因此不生成 SerializeDelta/DeserializeDelta 分支，直接走非 Delta 序列化）。
 *  - Config 为空结构体（FBoolNetSerializerConfig 在 NetSerializers.h 声明），无配置项。
 */
struct FBoolNetSerializer
{
	// 协议版本号。
	static const uint32 Version = 0;
	static constexpr bool bUseDefaultDelta = false; // No meaning to delta compress a bool

	// Use uint8 instead of bool to avoid issue with certain compilers assuming the value of a bool can only be 0 or 1.
	// Uninitialized bools are certainly not guaranteed to be 0 or 1.
	// 用 uint8 规避 bool 未初始化导致的未定义值问题。
	typedef uint8 SourceType; 
	typedef FBoolNetSerializerConfig ConfigType; 

	static const ConfigType DefaultConfig;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);
};
// 宏展开定义 FBoolNetSerializerInfo::Serializer（FNetSerializer 函数表静态实例）等。
UE_NET_IMPLEMENT_SERIALIZER(FBoolNetSerializer);
// 编译期强约束：sizeof(bool) 必须与 uint8 一致，否则上层 reinterpret_cast 会读错字节。
static_assert(sizeof(FBoolNetSerializer::SourceType) == sizeof(bool), "bool has unexpected size");

// Config 是空结构体，单实例即默认配置。
const FBoolNetSerializer::ConfigType FBoolNetSerializer::DefaultConfig;

/** 把 uint8 源值归一成 0 或 1，写入 1 bit。 */
void FBoolNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const SourceType Value = *reinterpret_cast<const SourceType*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	Writer->WriteBits((Value != 0 ? 1U : 0U), 1U);
}

/** 读 1 bit，保证输出严格为 0 或 1。 */
void FBoolNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	const SourceType Value = static_cast<SourceType>(Reader->ReadBits(1U));

	SourceType* Target = reinterpret_cast<SourceType*>(Args.Target);
	*Target = Value;
}

/**
 * 相等比较：采用"先转 bool 再比较"的写法，保证 2 == 3（都视为真）被判为相等——
 * 这与 Serialize 的语义一致（任意非零均视为真）。
 */
bool FBoolNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	const SourceType Value0 = *reinterpret_cast<const SourceType*>(Args.Source0);
	const SourceType Value1 = *reinterpret_cast<const SourceType*>(Args.Source1);

	const bool bValue0 = (Value0 != 0);
	const bool bValue1 = (Value1 != 0);

	return bValue0 == bValue1;
}

/**
 * 校验：Source 值必须 <= 1。
 * 这是帮助定位"未初始化 bool 成员"的诊断路径——实际 Serialize 用的是 Value!=0，不会拒绝 >1 的值；
 * 但 Validate 会返回 false 让上层日志标记疑似 bug。
 */
bool FBoolNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const SourceType Value = *reinterpret_cast<const SourceType*>(Args.Source);

	// By failing values other than 0 and 1 we assist in finding uninitialized bools.
	return (Value <= 1);
}

}
