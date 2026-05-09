// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Iris/Serialization/IntNetSerializerBase.h"

namespace UE::Net::Private
{

// Similar to IntNetSerializerBase when it comes to serialization.
/**
 * 整数区间 NetSerializer 的 CRTP 基座（模板辅助）。
 *
 * 与 FIntNetSerializerBase 相比，核心区别是"值域裁剪 + 基于下界 rebase"：
 *   - Config 中除了 BitCount 还带 LowerBound / UpperBound；
 *   - Quantize 会把 SourceType 值 Clamp 到 [LowerBound, UpperBound] 区间，再减去 LowerBound
 *     形成 [0, UpperBound-LowerBound] 范围的无符号值存入 QuantizedType；
 *   - 序列化阶段复用 FIntNetSerializerBase（按 BitCount 位数写 rebased 值）；
 *   - 所需的 BitCount 通常由 `GetBitsNeededForRange(LowerBound, UpperBound)` 计算——
 *     当值域较窄时比原生位宽更省带宽。
 *
 * 越界处理策略：
 *   - Quantize：静默 Clamp（生产环境不 assert，保证收发继续进行）；
 *   - Dequantize：如果 rebased 值加回下界后仍越界（说明收到了非法编码），设置 GNetError_InvalidValue
 *     错误、不写入 Target（保持 Target 原有值以免把错误数据带给业务层）。
 */
template<typename InSourceType, typename IntRangeNetSerializerConfig>
struct FIntRangeNetSerializerBase
{
	// 网络协议版本号，改变序列化格式时递增。
	static const uint32 Version = 0;

	typedef InSourceType SourceType;
	// 量化状态使用同字节宽度的无符号类型（便于位操作与 rebase）。
	typedef typename TUnsignedIntType<sizeof(InSourceType)>::Type QuantizedType;
	typedef IntRangeNetSerializerConfig ConfigType;

	// 下列五个"透传"方法直接调用 FIntNetSerializerBase<QuantizedType, ConfigType>：
	// 因为 Quantize 已把 SourceType 映射到 [0, UpperBound-LowerBound] 的无符号整数，
	// 之后的 Serialize/Deserialize/SerializeDelta/DeserializeDelta 跟普通无符号整数一致。

	static void Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
	{
		return FIntNetSerializerBase<QuantizedType, ConfigType>::Serialize(Context, Args);
	}

	static void Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
	{
		return FIntNetSerializerBase<QuantizedType, ConfigType>::Deserialize(Context, Args);
	}

	static void SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
	{
		return FIntNetSerializerBase<QuantizedType, ConfigType>::SerializeDelta(Context, Args);
	}

	static void DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
	{
		return FIntNetSerializerBase<QuantizedType, ConfigType>::DeserializeDelta(Context, Args);
	}

	/**
	 * 量化：先 Clamp 到配置区间，再减去 LowerBound 得到 [0, UpperBound-LowerBound] 的无符号 rebased 值。
	 *
	 * rebased 表示可以显著节省位数：例如 LowerBound=1000, UpperBound=1007 只需 3 bit 即可编码。
	 */
	static void Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
	{
		const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
		const SourceType Value = *reinterpret_cast<const SourceType*>(Args.Source);
		const SourceType ClampedValue = FMath::Clamp(Value, Config->LowerBound, Config->UpperBound);
		const QuantizedType RebasedValue = static_cast<QuantizedType>(static_cast<QuantizedType>(ClampedValue) - static_cast<QuantizedType>(Config->LowerBound));

		*reinterpret_cast<QuantizedType*>(Args.Target) = RebasedValue;
	}

	/**
	 * 反量化：rebased 值 + LowerBound 得到原 SourceType；若加回后仍越界表示收到非法数据，上报错误。
	 *
	 * 错误路径不会写入 Target——避免把错值传给业务。调用方需检查 Context 错误状态。
	 */
	static void Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
	{
		const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
		const QuantizedType RebasedValue = *reinterpret_cast<const QuantizedType*>(Args.Source);
		const SourceType Value = static_cast<SourceType>(static_cast<QuantizedType>(RebasedValue + static_cast<QuantizedType>(Config->LowerBound)));
		const SourceType ClampedValue = FMath::Clamp(Value, Config->LowerBound, Config->UpperBound);
		if (ClampedValue != Value)
		{
			Context.SetError(GNetError_InvalidValue);
			return; // Do not store any value in target!
		}
	
		*reinterpret_cast<SourceType*>(Args.Target) = ClampedValue;
	}

	/**
	 * 相等比较。
	 *  - bStateIsQuantized=true：比较已 rebase 后的无符号值；
	 *  - bStateIsQuantized=false：两端都先 Clamp 到合法区间再比较，这样"被夹断后相同"的值被视作相等。
	 */
	static bool IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
	{
		const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
		if (Args.bStateIsQuantized)
		{
			const QuantizedType Value0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
			const QuantizedType Value1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);

			return Value0 == Value1;
		}
		else
		{
			const SourceType Value0 = *reinterpret_cast<const SourceType*>(Args.Source0);
			const SourceType ClampedValue0 = FMath::Clamp(Value0, Config->LowerBound, Config->UpperBound);

			const SourceType Value1 = *reinterpret_cast<const SourceType*>(Args.Source1);
			const SourceType ClampedValue1 = FMath::Clamp(Value1, Config->LowerBound, Config->UpperBound);

			return ClampedValue0 == ClampedValue1;
		}
	}

	/**
	 * 校验：BitCount 不超过 SourceType 位宽；源值必须已经落在 [LowerBound, UpperBound] 区间（不做自动 Clamp）。
	 * Validate 返回 false 会被上层标记为可能的污染数据，不一定立即丢弃，但会进入诊断路径。
	 */
	static bool Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
	{
		const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);

		// Detect invalid bit count
		const uint32 BitCount = Config->BitCount;
		if (BitCount > sizeof(SourceType)*8U)
		{
			return false;
		}

		// Detect values outside of the valid range
		const SourceType Value = *reinterpret_cast<const SourceType*>(Args.Source);
		const SourceType ClampedValue = FMath::Clamp(Value, Config->LowerBound, Config->UpperBound);

		return Value == ClampedValue;
	}
};

}
