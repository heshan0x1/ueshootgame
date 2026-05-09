// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// DateTimeNetSerializer.cpp
// ---------------------------------------------------------------------------------------------
// FDateTimeNetSerializer 实现。
//
// 简单直观：
//   - FDateTime 内部是一个 int64 ticks（1 tick = 100ns，自 0001-01-01 00:00:00 起算）。
//   - Quantize：直接取 Ticks → int64（无损）。
//   - Serialize：WriteInt64 写入流（NetBitStreamUtil 内部一般会做 ZigZag/打包式写入，由
//     Util 决定，不在本 Serializer 关心范围内）。
//   - IsEqual：两态对比 ticks。Quantized 与 SourceType 比较行为完全等价。
//
// 边界 / 精度：
//   - 不限制 ticks 范围；INT64_MIN..INT64_MAX 全合法。FDateTime 的合法范围是
//     [0, 3155378975999999999]（即 0001..9999），但 Iris 不主动校验。
//   - 不做 NaN/Inf（FDateTime 是整数，无此概念）。
//   - 也无 ChangeMask 抖动顾虑（int64 比较即位精确）。
// =============================================================================================

#include "Iris/Serialization/DateTimeNetSerializer.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Misc/DateTime.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DateTimeNetSerializer)

namespace UE::Net
{

/**
 * FDateTimeNetSerializer ：FDateTime 的标准 Serializer。
 *
 * - SourceType = FDateTime（USTRUCT，存于 FProperty）。
 * - QuantizedType = int64（即 FDateTime 的内部 ticks 表示）。
 * - 量化等价 == 取 Ticks，反量化 == 用 ticks 构造 FDateTime；无损往返。
 */
struct FDateTimeNetSerializer
{
	static const uint32 Version = 0;

	typedef FDateTime SourceType;
	typedef int64 QuantizedType;
	
	typedef FDateTimeNetSerializerConfig ConfigType;

	static const ConfigType DefaultConfig;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	// 反斜杠是历史遗留（疑似从宏复制粘贴），不影响语义。
	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs&); \
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs&); \

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
};

const FDateTimeNetSerializer::ConfigType FDateTimeNetSerializer::DefaultConfig;

UE_NET_IMPLEMENT_SERIALIZER(FDateTimeNetSerializer);

/** 写：把 ticks（int64）写入位流。委托给 NetBitStreamUtil::WriteInt64。 */
void FDateTimeNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	WriteInt64(Writer, *reinterpret_cast<const QuantizedType*>(Args.Source));
}

/** 读：与 Serialize 对称，64-bit ticks 原位回放。 */
void FDateTimeNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	*reinterpret_cast<QuantizedType*>(Args.Target) = ReadInt64(Reader);
}

/**
 * 量化：FDateTime → int64 ticks（FDateTime::GetTicks）。
 * Ticks 是 FDateTime 的唯一存储字段，因此该转换无损。
 */
void FDateTimeNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	const SourceType& SourceValue = *reinterpret_cast<const SourceType*>(Args.Source);
	QuantizedType& TargetValue = *reinterpret_cast<QuantizedType*>(Args.Target);

	TargetValue = SourceValue.GetTicks();
}

/**
 * 反量化：ticks → FDateTime（用单参 FDateTime(int64) 构造）。
 */
void FDateTimeNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	Target = SourceType(Source);
}

/**
 * IsEqual：分两路。
 *   - 已量化态：直接 int64 比较（位精确，最快）。
 *   - 源态：FDateTime::operator== 比较 ticks（语义等价）。
 *
 * 注意 bStateIsQuantized 的两种调用场景：
 *   - true ：脏帧检测时比较 ChangeMask 内的量化缓存；
 *   - false：CallRepNotifies 时比较源对象与 baseline 的源态。
 */
bool FDateTimeNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		const QuantizedType& Value0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& Value1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);
		return Value0 == Value1;
	}
	else
	{
		const SourceType& Value0 = *reinterpret_cast<SourceType*>(Args.Source0);
		const SourceType& Value1 = *reinterpret_cast<SourceType*>(Args.Source1);
		return Value0 == Value1;
	}
}

}
