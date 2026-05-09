// Copyright Epic Games, Inc. All Rights Reserved.

#include "InternalNetSerializers.h"
#include "Iris/Serialization/EnumNetSerializers.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/IntRangeNetSerializerUtils.h"
#include "Iris/Serialization/InternalEnumNetSerializers.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"

namespace UE::Net
{

/**
 * FNetRoleNetSerializer：专门用于 AActor 的 Role/RemoteRole（ENetRole）序列化。
 *
 * 背景：
 *  - AActor 上同时存在 Role（本端视角的角色）和 RemoteRole（对端视角的角色），两者写时需要 swap：
 *    即服务器端的 Role=Authority / RemoteRole=SimulatedProxy 发送给客户端后，
 *    客户端应看到 Role=SimulatedProxy / RemoteRole=Authority——这就是 Dequantize 里的"角色互换"逻辑。
 *  - 若某连接的目标 Actor 不属于该玩家（bDowngradeAutonomousProxyRole=true），对 AutonomousProxy
 *    的 Role 应当被下调为 SimulatedProxy 再发送——避免非 Owner 客户端错以为自己能操控该 Actor。
 *
 * 设计要点：
 *  - 底层序列化仍然复用 FIntRangeNetSerializerBase：ENetRole 本质是一个带范围的小整数枚举；
 *  - Serialize 里如果命中"下调条件"，把发出去的值替换成 SimulatedProxyValue；读回时客户端什么都看不出；
 *  - Dequantize 则给 Target 加上 RelativeExternalOffsetToOtherRole 的偏移，把值写入"另一侧"字段实现 swap；
 *  - bUseDefaultDelta=false：下调会破坏"前后值相等"的判断，不能用默认 Delta 路径。
 *
 * 注册方式：UE_NET_IMPLEMENT_SERIALIZER_INTERNAL——属于内部 Serializer，不导出。
 */
struct FNetRoleNetSerializer
{
	static const uint32 Version = 0;
	// Can't use same value optimization due to role downgrading.
	// 下调逻辑使"对端实际收到的值"与本地当前值不一致，因此不能走 delta 的"同值优化"。
	static constexpr bool bUseDefaultDelta = false;

	typedef uint8 SourceType;       // ENetRole 底层是 uint8。
	typedef uint8 QuantizedType;
	typedef FNetRoleNetSerializerConfig ConfigType;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);

	// 首次构建好后缓存下来，避免每次引用都去查 UEnum。通过 PartialInitNetRoleSerializerConfig 填充。
	static FNetRoleNetSerializerConfig CachedConfig;
};

FNetRoleNetSerializerConfig FNetRoleNetSerializer::CachedConfig;

UE_NET_IMPLEMENT_SERIALIZER_INTERNAL(FNetRoleNetSerializer);


/**
 * Serialize：若命中"下调 AutonomousProxy"条件，则把 Source 指针换成 SimulatedProxyValue 再走通用序列化。
 *
 *  - Context.GetInternalContext()->bDowngradeAutonomousProxyRole 是 per-connection 的标志；
 *    对于 Actor 的 Owner 连接一般为 false，对其他连接为 true。
 *  - 这里使用位与（&）而非逻辑与（&&）以确保两端表达式都求值（编译器友好，分支预测无差异）。
 */
void FNetRoleNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const SourceType Value = *reinterpret_cast<const SourceType*>(Args.Source);
	if (Context.GetInternalContext()->bDowngradeAutonomousProxyRole & (Value == Config->AutonomousProxyValue))
	{
		// 下调：把即将发送的 Source 改指向 SimulatedProxyValue。
		FNetSerializeArgs DowngradedRoleArgs = Args;
		DowngradedRoleArgs.Source = NetSerializerValuePointer(&Config->SimulatedProxyValue);
		Private::FIntRangeNetSerializerBase<SourceType, ConfigType>::Serialize(Context, DowngradedRoleArgs);
	}
	else
	{
		// 正常路径：走基础区间序列化（按枚举 BitCount 写入）。
		Private::FIntRangeNetSerializerBase<SourceType, ConfigType>::Serialize(Context, Args);
	}
}

/** Deserialize：接收端不需要任何特殊处理；直接复用基础实现读出 ENetRole 值。 */
void FNetRoleNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	Private::FIntRangeNetSerializerBase<SourceType, ConfigType>::Deserialize(Context, Args);
}

/** Quantize：沿用基础整数区间的 Quantize（rebase 到 [0, UpperBound-LowerBound]）。 */
void FNetRoleNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	Private::FIntRangeNetSerializerBase<SourceType, ConfigType>::Quantize(Context, Args);
}

/**
 * Dequantize：写入"另一侧" Role——即 Source 是 Role 时写到 RemoteRole，反之亦然。
 *
 *  - RelativeExternalOffsetToOtherRole 是 AActor 中 Role 与 RemoteRole 两个 uint8 成员的字节偏移量（±1）。
 *  - 这里加到 Target 指针上，再交给基础 Dequantize 完成 rebased→原值的反转换并写入目标字段。
 *  - 效果：接收端最终看到的 Role/RemoteRole 自动完成"视角互换"。
 */
void FNetRoleNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);

	// Dequantize to the other role. This will result in the desired role swap.
	FNetDequantizeArgs RoleSwappingArgs = Args;
	RoleSwappingArgs.Target += Config->RelativeExternalOffsetToOtherRole;
	Private::FIntRangeNetSerializerBase<SourceType, ConfigType>::Dequantize(Context, RoleSwappingArgs);
}

/** 相等比较：沿用基础整数区间实现。 */
bool FNetRoleNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	return Private::FIntRangeNetSerializerBase<SourceType, ConfigType>::IsEqual(Context, Args);
}

/**
 * 校验：值域夹断 + UEnum::IsValidEnumValue 双保险，确保收到的 Role 值是 ENetRole 中已定义的枚举项
 * （None/SimulatedProxy/AutonomousProxy/Authority 之一）。
 */
bool FNetRoleNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);

	const SourceType Value = *reinterpret_cast<const SourceType*>(Args.Source);

	// Detect values outside of the valid range. This check needs to be performed before the enum check due to the generated _MAX value.
	// 先做范围夹断（防止 ROLE_MAX 哨兵混入），再走精确成员检查。
	const SourceType ClampedValue = FMath::Clamp(Value, Config->LowerBound, Config->UpperBound);
	if (Value != ClampedValue)
	{
		return false;
	}

	return Config->Enum->IsValidEnumValue(Value);
}

/**
 * 初始化 FNetRoleNetSerializerConfig 的部分字段（LowerBound/UpperBound/BitCount/Autonomous/Simulated 值 + Enum 指针）。
 *
 * "Partial" 含义：RelativeExternalOffsetToOtherRole 字段由调用方（上层 PropertyNetSerializerInfo）
 * 自行填充，因为它取决于具体 AActor 派生类中 Role 与 RemoteRole 的实际偏移。
 *
 * 此函数使用全局 CachedConfig 做首次构建/复用缓存：ENetRole 的枚举定义对整个进程唯一，
 * 没必要重复扫描 UEnum。后续调用直接从 Cached 拷贝前部字段即可。
 */
bool PartialInitNetRoleSerializerConfig(FNetRoleNetSerializerConfig& OutConfig, const UEnum* Enum)
{
	if (FNetRoleNetSerializer::CachedConfig.BitCount == 0)
	{
		// 借用 FEnumUint8NetSerializerConfig 的构建逻辑拿到 LowerBound/UpperBound/BitCount。
		FEnumUint8NetSerializerConfig EnumConfig;
		if (!Private::InitEnumNetSerializerConfig(EnumConfig, Enum))
		{
			check(false);
			return false;
		}

		// Because of role downgrading we currently assume that quantize is a no-op so we don't have to requantize.
		// 下调替换是直接替换 Source 指针而非走 Quantize 再序列化，因此 LowerBound 必须是 0
		// （这样 Quantize 与原值一一对应，替换 SimulatedProxyValue 指针写出的 bit 才有意义）。
		check(EnumConfig.LowerBound == 0);

		// 解析 ROLE_AutonomousProxy / ROLE_SimulatedProxy 的整数值——序列化时判断 & 替换要用。
		const int64 AutonomousProxyValue = Enum->GetValueByNameString(TEXT("ROLE_AutonomousProxy"), EGetByNameFlags::CaseSensitive);
		check(AutonomousProxyValue > 0 && AutonomousProxyValue < 256);
		const int64 SimulatedProxyValue = Enum->GetValueByNameString(TEXT("ROLE_SimulatedProxy"), EGetByNameFlags::CaseSensitive);
		check(SimulatedProxyValue > 0 && SimulatedProxyValue < 256);
		
		FNetRoleNetSerializer::CachedConfig.LowerBound = EnumConfig.LowerBound;
		FNetRoleNetSerializer::CachedConfig.UpperBound = EnumConfig.UpperBound;
		FNetRoleNetSerializer::CachedConfig.BitCount = EnumConfig.BitCount;
		FNetRoleNetSerializer::CachedConfig.AutonomousProxyValue = static_cast<uint8>(AutonomousProxyValue);
		FNetRoleNetSerializer::CachedConfig.SimulatedProxyValue = static_cast<uint8>(SimulatedProxyValue);
		FNetRoleNetSerializer::CachedConfig.Enum = Enum;
	}

	// 注意：RelativeExternalOffsetToOtherRole 未在此设置，由调用者后续赋值。
	OutConfig = FNetRoleNetSerializer::CachedConfig;
	return true;
}

}
