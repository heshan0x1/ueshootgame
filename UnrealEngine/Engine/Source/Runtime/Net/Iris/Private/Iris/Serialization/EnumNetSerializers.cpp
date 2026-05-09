// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/EnumNetSerializers.h"
#include "Iris/Serialization/IntRangeNetSerializerUtils.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EnumNetSerializers)

namespace UE::Net::Private
{

/**
 * 枚举 NetSerializer 的 CRTP 基座。
 *
 * 复用 FIntRangeNetSerializerBase 的所有序列化/量化逻辑（因为枚举本质就是带值域约束的整数），
 * 仅重写 Validate：除了 BitCount 与值域检查，还额外走 UEnum::IsValidEnumValue 严格验证
 * 收到的值确实是 UENUM 中已定义的有效枚举项——防御非法整数冒充枚举造成业务 switch-case 踩穿。
 */
template<typename InSourceType, typename EnumNetSerializerConfig>
struct FEnumNetSerializerBase : public FIntRangeNetSerializerBase<InSourceType, EnumNetSerializerConfig>
{
	/**
	 * 校验流程：
	 *  1) BitCount <= 原生位宽；
	 *  2) Source Value 在 [LowerBound, UpperBound]（先夹断，因为 UENUM 可能自动生成 _MAX 越界哨兵）；
	 *  3) 若 Config->Enum 非空，再调用 IsValidEnumValue 做精确成员检查（O(N) 扫一遍定义）。
	 *
	 *  注意：_MAX 这样自动生成的哨兵通常超过 UpperBound，会在第 2 步就被判为无效——这是有意的。
	 */
	static bool Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
	{
		const EnumNetSerializerConfig* Config = static_cast<const EnumNetSerializerConfig*>(Args.NetSerializerConfig);
		const uint32 BitCount = Config->BitCount;

		// Detect invalid bit count
		if (BitCount > sizeof(InSourceType)*8U)
		{
			return false;
		}

		const InSourceType Value = *reinterpret_cast<const InSourceType*>(Args.Source);

		// Detect values outside of the valid range. This check needs to be performed before the enum check due to the generated _MAX value.
		// 值域检查必须先于 UEnum::IsValidEnumValue：自动生成的 _MAX 会被 IsValidEnumValue 视为有效，
		// 但不应出现在网络传输中。
		const InSourceType ClampedValue = FMath::Clamp(Value, Config->LowerBound, Config->UpperBound);
		if (Value != ClampedValue)
		{
			return false;
		}

		// This is slow as it will go through all values in the enum.
		// 严格成员检查：保证 Value 是 UENUM 明确声明的枚举项之一，拒绝随意的整数冒充。
		if (const UEnum* Enum = Config->Enum)
		{
			return Enum->IsValidEnumValue(Value);
		}

		return true;
	}
};

}

namespace UE::Net
{

// 下列 8 个 Serializer 均只派生自模板基座，无需额外方法。
// 区别仅在 SourceType 与 Config 绑定：反射/注册表根据 UEnum 底层类型选用合适的一个。

/** FEnumInt8NetSerializer：底层为 int8 的 UENUM。 */
struct FEnumInt8NetSerializer : public Private::FEnumNetSerializerBase<int8, FEnumInt8NetSerializerConfig>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FEnumInt8NetSerializer);

/** FEnumInt16NetSerializer：底层为 int16 的 UENUM。 */
struct FEnumInt16NetSerializer : public Private::FEnumNetSerializerBase<int16, FEnumInt16NetSerializerConfig>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FEnumInt16NetSerializer);

/** FEnumInt32NetSerializer：底层为 int32 的 UENUM（传统 UENUM 最常见类型之一）。 */
struct FEnumInt32NetSerializer : public Private::FEnumNetSerializerBase<int32, FEnumInt32NetSerializerConfig>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FEnumInt32NetSerializer);

/** FEnumInt64NetSerializer：底层为 int64 的 UENUM。 */
struct FEnumInt64NetSerializer : public Private::FEnumNetSerializerBase<int64, FEnumInt64NetSerializerConfig>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FEnumInt64NetSerializer);

/** FEnumUint8NetSerializer：底层为 uint8（enum class : uint8），Blueprint/蓝图枚举的默认类型。 */
struct FEnumUint8NetSerializer : public Private::FEnumNetSerializerBase<uint8, FEnumUint8NetSerializerConfig>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FEnumUint8NetSerializer);

/** FEnumUint16NetSerializer：底层为 uint16 的 UENUM。 */
struct FEnumUint16NetSerializer : public Private::FEnumNetSerializerBase<uint16, FEnumUint16NetSerializerConfig>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FEnumUint16NetSerializer);

/** FEnumUint32NetSerializer：底层为 uint32 的 UENUM。 */
struct FEnumUint32NetSerializer : public Private::FEnumNetSerializerBase<uint32, FEnumUint32NetSerializerConfig>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FEnumUint32NetSerializer);

/** FEnumUint64NetSerializer：底层为 uint64 的 UENUM。 */
struct FEnumUint64NetSerializer : public Private::FEnumNetSerializerBase<uint64, FEnumUint64NetSerializerConfig>
{
};
UE_NET_IMPLEMENT_SERIALIZER(FEnumUint64NetSerializer);

}
