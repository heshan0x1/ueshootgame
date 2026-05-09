// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// EnumPropertyNetSerializerInfo.cpp —— 三个 Enum 类 PropertyNetSerializerInfo 的实现
// ---------------------------------------------------------------------------------------------------------------------
// UE 反射对 enum 有两种属性表达：
//   1) FByteProperty（uint8 后端）+ Enum 字段不为空 —— 历史上的 TEnumAsByte<EFoo> / UENUM(BlueprintType=uint8)
//   2) FEnumProperty —— Native enum class（C++11+），underlying 可以是 int8/uint8/int16/uint16/int32/uint32/int64/uint64
//
// 此外有一个特例：
//   3) Role/RemoteRole（FByteProperty + UEnum::ENetRole + 命名 NAME_Role/NAME_RemoteRole 且属于 UClass）
//      需要走 FNetRoleNetSerializer：服务端 Role/RemoteRole 在发送时要交换，
//      客户端收到后再交换回来——这是 UE 旧网络框架就有的特殊行为。
//
// 三个 Info 通过 IsSupported 互斥分流：
//   - FEnumAsBytePropertyNetSerializerInfo: ByteProperty.IsEnum() && !ShouldUseNetRoleSerializer
//   - FNetRoleNetSerializerInfo:            ShouldUseNetRoleSerializer (即必须是 ENetRole + 在 UClass 内 + 名 Role/RemoteRole)
//   - FEnumPropertyNetSerializerInfo:       FEnumProperty（独立的 FieldClass，不与 ByteProperty 冲突）
// =====================================================================================================================

#include "Iris/ReplicationState/EnumPropertyNetSerializerInfo.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/InternalNetSerializers.h"
#include "Iris/Serialization/InternalEnumNetSerializers.h"

namespace UE::Net::Private
{

// 静态 FName 常量：UEnum 名 "ENetRole"，用于识别 FByteProperty 是否为 NetRole 后端。
static const FName PropertyNetSerializerRegistry_NAME_NetRole(TEXT("ENetRole"));

// 判断一个 FByteProperty 是否要走 FNetRoleNetSerializer。
// 三个条件全部满足才会返回 true：
//   1) 属性名必须是 NAME_Role 或 NAME_RemoteRole（防止其他自定义 ENetRole 字段误命中）；
//   2) 后端 UEnum 必须真是 ENetRole；
//   3) 必须挂在 UClass 上（Actor 等）——结构体里的 ENetRole 不需要 role swapping。
static bool PropertyNetSerializerInfo_ShouldUseNetRoleSerializer(const FByteProperty* Property)
{
	// Only ENetRole named Role and RemoteRole are supported
	const FName PropertyName = Property->GetFName();
	if (PropertyName != NAME_Role && PropertyName != NAME_RemoteRole)
	{
		return false;
	}

	// Verify this is the right enum.
	const UEnum* Enum = Property->Enum;
	if (Enum == nullptr)
	{
		return false;
	}

	if (Enum->GetFName() != PropertyNetSerializerRegistry_NAME_NetRole)
	{
		return false;
	}

	// Only NetRoles in classes is expected to need the role swapping
	if (const UClass* Class = Cast<UClass>(Property->GetOwner<UObject>()))
	{
		return true;
	}

	return false;
}

/**
 * ByteProperty when backed by EnumAsByte
 */
// 标准的 EnumAsByte 路径：FByteProperty + 非空 Enum，且不是 Role/RemoteRole。
// 一律使用 FEnumUint8NetSerializer + Config 由 InitEnumNetSerializerConfig 填充（内含 Enum 边界与无效值校验）。
struct FEnumAsBytePropertyNetSerializerInfo : public FPropertyNetSerializerInfo
{
	virtual const FFieldClass* GetPropertyTypeClass() const override { return FByteProperty::StaticClass(); }
	
	virtual bool IsSupported(const FProperty* Property) const override
	{ 
		const FByteProperty* ByteProperty = CastFieldChecked<FByteProperty>(Property);
		// 排除 Role/RemoteRole（让 FNetRoleNetSerializerInfo 接管）
		if (PropertyNetSerializerInfo_ShouldUseNetRoleSerializer(ByteProperty))
		{
			return false;
		}

		// 必须是 enum 后端的 ByteProperty；纯 uint8（FByteProperty.Enum == nullptr）由 FUint8PropertyNetSerializerInfo 接管。
		return ByteProperty->IsEnum(); 
	}

	virtual const FNetSerializer* GetNetSerializer(const FProperty* Property) const override { return &UE_NET_GET_SERIALIZER(FEnumUint8NetSerializer); }

	virtual bool CanUseDefaultConfig(const FProperty* Property) const override { return false; }

	virtual FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const override
	{ 
		FEnumUint8NetSerializerConfig* Config = new (NetSerializerConfigBuffer) FEnumUint8NetSerializerConfig();

		// 把 UEnum 的有效值范围写入 Config，方便 NetSerializer 在反序列化时检查/裁剪。
		Private::InitEnumNetSerializerConfig(*Config, CastFieldChecked<FByteProperty>(Property)->Enum);

		return Config;
	}
};
UE_NET_IMPLEMENT_NETSERIALIZER_INFO(FEnumAsBytePropertyNetSerializerInfo);

/**
 * ByteProperty when used on ENetRole
 */
// Role/RemoteRole 专用 Info：使用 FNetRoleNetSerializer，处理服务端/客户端 Role 互换的特殊行为。
// PartialInitNetRoleSerializerConfig 仅初始化部分字段——剩余字段（如 LocalRole）由运行时根据连接方向决定。
struct FNetRoleNetSerializerInfo : public FPropertyNetSerializerInfo
{
	virtual const FFieldClass* GetPropertyTypeClass() const override { return FByteProperty::StaticClass(); }
	
	virtual bool IsSupported(const FProperty* Property) const override
	{ 
		const FByteProperty* ByteProperty = CastFieldChecked<FByteProperty>(Property);
		return PropertyNetSerializerInfo_ShouldUseNetRoleSerializer(ByteProperty);
	}

	virtual const FNetSerializer* GetNetSerializer(const FProperty* Property) const override { return &UE_NET_GET_SERIALIZER(FNetRoleNetSerializer); }

	virtual bool CanUseDefaultConfig(const FProperty* Property) const override { return false; }

	virtual FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const override
	{ 
		FNetRoleNetSerializerConfig* Config = new (NetSerializerConfigBuffer) FNetRoleNetSerializerConfig();

		// "Partial" 含义：仅写入 enum 边界与位宽信息；本机 Role/远端 Role 的具体角色映射由 NetSerializer 运行时填充。
		PartialInitNetRoleSerializerConfig(*Config, CastFieldChecked<FByteProperty>(Property)->Enum);

		return Config;
	}
};
UE_NET_IMPLEMENT_NETSERIALIZER_INFO(FNetRoleNetSerializerInfo);

/**
 * This is supposed to support all enum serializers, both signed and unsigned.
 */
// FEnumPropertyNetSerializerInfo —— 处理 Native enum class（FEnumProperty）。
// FEnumProperty 持有 underlying 整数类型的 sub-Property（GetUnderlyingProperty），
// 该 underlying 决定了 enum 的位宽与 signed/unsigned，从而映射到 8 个具体 NetSerializer 中的一个。
//
// PropertyToSerializerIndex 的映射表：
//   FByteProperty   → 0 (Uint8)         FInt8Property  → 4 (Int8)
//   FUInt16Property → 1 (Uint16)        FInt16Property → 5 (Int16)
//   FUInt32Property → 2 (Uint32)        FIntProperty   → 6 (Int32)
//   FUInt64Property → 3 (Uint64)        FInt64Property → 7 (Int64)
class FEnumPropertyNetSerializerInfo : public FPropertyNetSerializerInfo
{
public:
	FEnumPropertyNetSerializerInfo() : FPropertyNetSerializerInfo()
	{
		// 8 个具体 NetSerializer 的预绑定，索引顺序与 PropertyToSerializerIndex 对齐。
		Serializers[0] = &UE_NET_GET_SERIALIZER(FEnumUint8NetSerializer);
		Serializers[1] = &UE_NET_GET_SERIALIZER(FEnumUint16NetSerializer);
		Serializers[2] = &UE_NET_GET_SERIALIZER(FEnumUint32NetSerializer);
		Serializers[3] = &UE_NET_GET_SERIALIZER(FEnumUint64NetSerializer);
		Serializers[4] = &UE_NET_GET_SERIALIZER(FEnumInt8NetSerializer);
		Serializers[5] = &UE_NET_GET_SERIALIZER(FEnumInt16NetSerializer);
		Serializers[6] = &UE_NET_GET_SERIALIZER(FEnumInt32NetSerializer);
		Serializers[7] = &UE_NET_GET_SERIALIZER(FEnumInt64NetSerializer);
	}

private:
	virtual const FFieldClass* GetPropertyTypeClass() const override { return FEnumProperty::StaticClass(); }
	
	virtual bool IsSupported(const FProperty* Property) const override
	{ 
		const FEnumProperty* EnumProperty = CastFieldChecked<FEnumProperty>(Property);
		const FProperty* UnderlyingType = EnumProperty->GetUnderlyingProperty();
		const int32 SerializerIndex = PropertyToSerializerIndex(UnderlyingType);
		// 仅当 underlying 类型在表中（即 8 种整型之一）才支持
		return SerializerIndex >= 0;
	}

	virtual const FNetSerializer* GetNetSerializer(const FProperty* Property) const override
	{
		const FEnumProperty* EnumProperty = CastFieldChecked<FEnumProperty>(Property);
		const FProperty* UnderlyingType = EnumProperty->GetUnderlyingProperty();
		const int32 SerializerIndex = PropertyToSerializerIndex(UnderlyingType);
		return Serializers[SerializerIndex];
	}

	virtual bool CanUseDefaultConfig(const FProperty* Property) const override { return false; }

	virtual FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const override
	{
		using namespace UE::Net::Private;

		const FEnumProperty* EnumProperty = CastFieldChecked<FEnumProperty>(Property);
		const FProperty* UnderlyingType = EnumProperty->GetUnderlyingProperty();
		const int32 SerializerIndex = PropertyToSerializerIndex(UnderlyingType);

		// 按 SerializerIndex 选择对应的 ConfigType placement-new + 写入 Enum 边界。
		// switch-case 而非数组化的原因：每个 ConfigType 是不同 C++ 类型（不同 sizeof / 字段），
		// 必须在编译期决定具体类型。
		switch (SerializerIndex)
		{
		case 0:
		{
			FEnumUint8NetSerializerConfig* Config = new (NetSerializerConfigBuffer) FEnumUint8NetSerializerConfig();
			InitEnumNetSerializerConfig(*Config, EnumProperty->GetEnum());
			return Config;
		}
		case 1:
		{
			FEnumUint16NetSerializerConfig* Config = new (NetSerializerConfigBuffer) FEnumUint16NetSerializerConfig();
			InitEnumNetSerializerConfig(*Config, EnumProperty->GetEnum());
			return Config;
		}
		case 2:
		{
			FEnumUint32NetSerializerConfig* Config = new (NetSerializerConfigBuffer) FEnumUint32NetSerializerConfig();
			InitEnumNetSerializerConfig(*Config, EnumProperty->GetEnum());
			return Config;
		}
		case 3:
		{
			FEnumUint64NetSerializerConfig* Config = new (NetSerializerConfigBuffer) FEnumUint64NetSerializerConfig();
			InitEnumNetSerializerConfig(*Config, EnumProperty->GetEnum());
			return Config;
		}
		case 4:
		{
			FEnumInt8NetSerializerConfig* Config = new (NetSerializerConfigBuffer) FEnumInt8NetSerializerConfig();
			InitEnumNetSerializerConfig(*Config, EnumProperty->GetEnum());
			return Config;
		}
		case 5:
		{
			FEnumInt16NetSerializerConfig* Config = new (NetSerializerConfigBuffer) FEnumInt16NetSerializerConfig();
			InitEnumNetSerializerConfig(*Config, EnumProperty->GetEnum());
			return Config;
		}
		case 6:
		{
			FEnumInt32NetSerializerConfig* Config = new (NetSerializerConfigBuffer) FEnumInt32NetSerializerConfig();
			InitEnumNetSerializerConfig(*Config, EnumProperty->GetEnum());
			return Config;
		}
		case 7:
		{
			FEnumInt64NetSerializerConfig* Config = new (NetSerializerConfigBuffer) FEnumInt64NetSerializerConfig();
			InitEnumNetSerializerConfig(*Config, EnumProperty->GetEnum());
			return Config;
		}

		default:
			break;
		};


		return nullptr;
	}

	// 8 项整型 → 0..7 的索引映射；不命中返回 -1（IsSupported 据此返回 false）。
	int32 PropertyToSerializerIndex(const FProperty* Property) const
	{
		if (CastField<FByteProperty>(Property) != nullptr)
		{
			return 0;
		}
		if (CastField<FUInt16Property>(Property) != nullptr)
		{
			return 1;
		}
		if (CastField<FUInt32Property>(Property) != nullptr)
		{
			return 2;
		}
		if (CastField<FUInt64Property>(Property) != nullptr)
		{
			return 3;
		}
		if (CastField<FInt8Property>(Property) != nullptr)
		{
			return 4;
		}
		if (CastField<FInt16Property>(Property) != nullptr)
		{
			return 5;
		}
		if (CastField<FIntProperty>(Property) != nullptr)
		{
			return 6;
		}
		if (CastField<FInt64Property>(Property) != nullptr)
		{
			return 7;
		}

		return -1;
	}

	// 预绑定的 NetSerializer 静态指针表
	const FNetSerializer* Serializers[8];

};
UE_NET_IMPLEMENT_NETSERIALIZER_INFO(FEnumPropertyNetSerializerInfo);

}
