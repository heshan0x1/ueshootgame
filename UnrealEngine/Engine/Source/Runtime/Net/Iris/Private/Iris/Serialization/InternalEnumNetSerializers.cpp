// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/InternalEnumNetSerializers.h"
#include "Iris/Core/BitTwiddling.h"
#include <limits>
#include <type_traits>

namespace UE::Net::Private
{

/**
 * 通用 EnumNetSerializerConfig 初始化模板。
 *
 * 核心任务：从 UEnum 中扫描所有已定义值，得到真实的最小/最大枚举值，
 * 进而算出 BitCount = GetBitsNeededForRange(Min, Max)。
 *
 * 难点：
 *  - NumEnums() 包含自动生成的 _MAX 哨兵（UHT 为部分 UENUM 追加），其值通常远大于真实枚举最大值；
 *    如果不剔除，BitCount 会被撑大，导致不必要的位数浪费。
 *  - uint64 类型的枚举值可能超过 int64 的正半区间，直接用 int64 会溢出；
 *    因此用 LargeIntegerType 按签名性分别选择 int64/uint64。
 *  - UEnum::GetValueByIndex 返回 int64，对 uint64 枚举的大值读取需要做 LargeIntegerType 转换后再判。
 *
 *  @tparam SourceType  源 C++ 整型（匹配对应 EnumConfig 的 LowerBound/UpperBound 类型）。
 *  @tparam ConfigType  目标 EnumConfig 类型。
 */
template<typename SourceType, typename ConfigType>
class TInitEnumNetSerializerConfig
{
public:
	/**
	 * 遍历 Enum 中所有成员，写入 OutConfig 的 LowerBound/UpperBound/BitCount/Enum 字段。
	 *
	 * 边界处理：
	 *  - EnumValueCount <= 0：理论上不可能（UHT 对空 UENUM 会报错），ensureAlways 断言并给出安全默认值。
	 *  - 自动生成的 _MAX 哨兵：用 FMath::Min(Value, MaxSupportedValue) 裁剪，不让其"偷偷"扩展值域。
	 *
	 *  @return 永远返回 true（与 IRISCORE_API 导出 API 约定一致）。
	 */
	static bool Init(ConfigType& OutConfig, const UEnum* Enum)
	{
		// NumEnums() include the potentially auto-generated _MAX enum value.
		const int32 EnumValueCount = Enum->NumEnums();

		// 根据签名性选择 int64/uint64 做中间计算——兼顾有符号枚举负值与 uint64 超大正值。
		using LargeIntegerType = std::conditional_t<std::is_signed_v<SourceType>, int64, uint64>;
		constexpr LargeIntegerType MaxSupportedValue = static_cast<LargeIntegerType>(std::numeric_limits<SourceType>::max());


		// Find smallest and largest values.
		if (EnumValueCount <= 0)
		{
			// At the time of this writing this case should not happen due to errors when a UENUM does not contain values.
			// 理论上 UHT 会拒绝空 UENUM；此处走兜底以保证 Config 仍然是可用的零范围。
			ensureAlways(EnumValueCount > 0);
			OutConfig.LowerBound = 0;
			OutConfig.UpperBound = 0;
			OutConfig.BitCount = static_cast<uint8>(GetBitsNeededForRange(OutConfig.LowerBound, OutConfig.UpperBound));
			OutConfig.Enum = Enum;
		}
		else
		{
			// Cannot use UEnum methods here due to issues with the generated _MAX value as well as uint64 values outside of the positive int64 range.
			// 不能用 UEnum 的 GetMaxEnumValue 等方法——它们对 _MAX 和超 int64 范围的 uint64 值处理不一致。
			LargeIntegerType SmallestValue = std::numeric_limits<LargeIntegerType>::max();
			LargeIntegerType LargestValue = std::numeric_limits<LargeIntegerType>::min();
			for (int32 EnumIt = 0, EnumEndIt = EnumValueCount; EnumIt != EnumEndIt; ++EnumIt)
			{
				const LargeIntegerType Value = static_cast<LargeIntegerType>(Enum->GetValueByIndex(EnumIt));
				SmallestValue = FMath::Min(SmallestValue, Value);
				// Prevent out of bounds auto-generated _MAX values from sneaking in.
				// 把 _MAX 等哨兵夹断到 SourceType::max()，避免让 BitCount 被其无谓撑大。
				LargestValue = FMath::Max(LargestValue, FMath::Min(Value, MaxSupportedValue));
			}

			OutConfig.LowerBound = static_cast<SourceType>(SmallestValue);
			OutConfig.UpperBound = static_cast<SourceType>(LargestValue);
			// 用值域宽度（UpperBound-LowerBound）计算最少位数——这正是"枚举动态位宽"的核心优化。
			OutConfig.BitCount = static_cast<uint8>(GetBitsNeededForRange(OutConfig.LowerBound, OutConfig.UpperBound));
			OutConfig.Enum = Enum;
		}

		return true;
	}
};

// 下列 8 个具体实现把调用转发到模板，和 EnumNetSerializers 中的 8 个 Config 一一对应。

bool InitEnumNetSerializerConfig(FEnumInt8NetSerializerConfig& OutConfig, const UEnum* Enum)
{
	return TInitEnumNetSerializerConfig<int8, decltype(OutConfig)>::Init(OutConfig, Enum);
}

bool InitEnumNetSerializerConfig(FEnumInt16NetSerializerConfig& OutConfig, const UEnum* Enum)
{
	return TInitEnumNetSerializerConfig<int16, decltype(OutConfig)>::Init(OutConfig, Enum);
}

bool InitEnumNetSerializerConfig(FEnumInt32NetSerializerConfig& OutConfig, const UEnum* Enum)
{
	return TInitEnumNetSerializerConfig<int32, decltype(OutConfig)>::Init(OutConfig, Enum);
}

bool InitEnumNetSerializerConfig(FEnumInt64NetSerializerConfig& OutConfig, const UEnum* Enum)
{
	return TInitEnumNetSerializerConfig<int64, decltype(OutConfig)>::Init(OutConfig, Enum);
}

bool InitEnumNetSerializerConfig(FEnumUint8NetSerializerConfig& OutConfig, const UEnum* Enum)
{
	return TInitEnumNetSerializerConfig<uint8, decltype(OutConfig)>::Init(OutConfig, Enum);
}

bool InitEnumNetSerializerConfig(FEnumUint16NetSerializerConfig& OutConfig, const UEnum* Enum)
{
	return TInitEnumNetSerializerConfig<uint16, decltype(OutConfig)>::Init(OutConfig, Enum);
}

bool InitEnumNetSerializerConfig(FEnumUint32NetSerializerConfig& OutConfig, const UEnum* Enum)
{
	return TInitEnumNetSerializerConfig<uint32, decltype(OutConfig)>::Init(OutConfig, Enum);
}

bool InitEnumNetSerializerConfig(FEnumUint64NetSerializerConfig& OutConfig, const UEnum* Enum)
{
	return TInitEnumNetSerializerConfig<uint64, decltype(OutConfig)>::Init(OutConfig, Enum);
}

}
