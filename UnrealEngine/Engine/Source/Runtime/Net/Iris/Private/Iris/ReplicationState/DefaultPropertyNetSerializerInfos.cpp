// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// DefaultPropertyNetSerializerInfos.cpp —— 引擎内置 Property → NetSerializer 默认绑定的实现 + 注册
// ---------------------------------------------------------------------------------------------------------------------
// 内容分两部分：
//   A) 大量 file-static 的 Info 单例（用 UE_NET_IMPLEMENT_*_NETSERIALIZER_INFO 宏批量生成）
//   B) RegisterDefaultPropertyNetSerializerInfos() —— 把它们一次性 Register 进 FPropertyNetSerializerInfoRegistry
//
// 默认绑定一览：
// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
//  Property 类型               →  NetSerializer                       说明
// ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
//  FInt8Property               →  FInt8NetSerializer                  原始有符号字节
//  FInt16Property              →  FInt16NetSerializer
//  FIntProperty (int32)        →  FPackedInt32NetSerializer           VarInt 压缩
//  FInt64Property              →  FInt64NetSerializer
//  FByteProperty (无 Enum)     →  FUint8NetSerializer                 由 FUint8PropertyNetSerializerInfo 处理
//  FByteProperty (有 Enum)     →  FEnumUint8NetSerializer             由 FEnumAsBytePropertyNetSerializerInfo 处理
//  FByteProperty (NetRole)     →  FNetRoleNetSerializer               由 FNetRoleNetSerializerInfo 处理
//  FUInt16Property             →  FUint16NetSerializer
//  FUInt32Property             →  FPackedUint32NetSerializer          VarInt 压缩
//  FUInt64Property             →  FUint64NetSerializer
//  FFloatProperty              →  FFloatNetSerializer
//  FDoubleProperty             →  FDoubleNetSerializer
//  FBoolProperty (native bool) →  FBoolNetSerializer
//  FBoolProperty (bitfield)    →  FBitfieldNetSerializer + 位偏 Config
//  FEnumProperty               →  FEnum{U,Int}{8/16/32/64}NetSerializer（按 underlying 选）
//  FObjectProperty (raw)       →  FObjectNetSerializer + PropertyClass Config
//  FObjectProperty (TObjectPtr)→  FObjectPtrNetSerializer
//  FWeakObjectProperty         →  FWeakObjectNetSerializer
//  FInterfaceProperty          →  FScriptInterfaceNetSerializer
//  FSoftObjectProperty         →  FSoftObjectNetSerializer
//  FSoftClassProperty          →  FSoftObjectNetSerializer            （复用同一序列化器）
//  FFieldPathProperty          →  FFieldPathNetSerializer
//  FNameProperty               →  FNameAsNetTokenNetSerializer        （NetToken 替代 string，省带宽）
//  FStrProperty                →  FStringNetSerializer
//  FTextProperty               →  FLastResortPropertyNetSerializer    （bExcludeFromDefaultStateHash=true：本地化）
//
//  Named structs：
//   "Guid"                     →  FGuidNetSerializer
//   "Vector" / "Vector3f/d"    →  FVector{,3f,3d}NetSerializer
//   "Rotator" / "Rotator3f/d"  →  FRotator{,3f,3d}NetSerializer
//   "Quat" / "Quat4f/d"        →  FUnitQuat{,4f,4d}NetSerializer       (单位四元数，3 分量编码)
//   "Vector_NetQuantize{100/10}" → FVectorNetQuantize{100/10}NetSerializer
//   "Vector_NetQuantize"       →  FVectorNetQuantizeNetSerializer
//   "Vector_NetQuantizeNormal" →  FVectorNetQuantizeNormalNetSerializer
//   "DateTime"                 →  FDateTimeNetSerializer
//   "SoftObjectPath" / "SoftClassPath" → FSoftObjectPathNetSerializer / FSoftClassPathNetSerializer
//   "RemoteObjectReference"    →  FRemoteObjectReferenceNetSerializer  (跨节点对象引用)
//   "RemoteServerId"           →  FRemoteServerIdNetSerializer
//   "RemoteObjectId"           →  FRemoteObjectIdNetSerializer
//
// 未在表中的 USTRUCT → FStructNetSerializer（递归处理子成员）
// 未在表中的 FArrayProperty → FArrayPropertyNetSerializer（前提是 Inner 已支持）
// 全部失败 → FLastResortPropertyNetSerializer（旧 NetSerialize 兼容）
// =====================================================================================================================

#include "Iris/ReplicationState/DefaultPropertyNetSerializerInfos.h"
#include "Iris/ReplicationState/EnumPropertyNetSerializerInfo.h"
#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/InternalNetSerializers.h"
#include "Iris/Serialization/DateTimeNetSerializer.h"
#include "Iris/Serialization/RemoteObjectReferenceNetSerializer.h"
#include "Iris/Serialization/RemoteServerIdNetSerializer.h"
#include "Iris/Serialization/RemoteObjectIdNetSerializer.h"
#include "UObject/TextProperty.h"

namespace UE::Net::Private
{

/**
 *	Primitive types defaults, remember to bind them as well in RegisterDefaultPropertyNetSerializerInfos
 */
// ---- 整型 ----
// 注：FIntProperty / FUInt32Property 走 Packed 变体（VarInt）以便对小值省带宽；
//     FInt8/16/64、FUInt16/64 直接走定长，因为 8/16-bit 通常已经够紧凑或 Packed 收益较小。
UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FInt8Property, FInt8NetSerializer);
UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FInt16Property, FInt16NetSerializer);
UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FIntProperty, FPackedInt32NetSerializer);
//UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(IntProperty, FInt32NetSerializer);
UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FInt64Property, FInt64NetSerializer);
// ByteProperty is special as it also handles EnumAsByte
// FByteProperty 不能简单一行绑定——需要根据是否 IsEnum() / 是否 Role 分流，见下方 FUint8PropertyNetSerializerInfo
//UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FByteProperty, FUint8NetSerializer);
UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FUInt16Property, FUint16NetSerializer);
UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FUInt32Property, FPackedUint32NetSerializer);
//UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FUInt32Property, FUint32NetSerializer);
UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FUInt64Property, FUint64NetSerializer);
UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FFloatProperty, FFloatNetSerializer);
UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FDoubleProperty, FDoubleNetSerializer);

// Objects and fields
// ---- 各类对象/路径 USTRUCT 名字常量 ----
// 用于 UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO 宏的"按名注册"。
static const FName PropertyNetSerializerRegistry_NAME_SoftObjectPath("SoftObjectPath");
static const FName PropertyNetSerializerRegistry_NAME_SoftClassPath("SoftClassPath");
static const FName PropertyNetSerializerRegistry_NAME_RemoteObjectReference("RemoteObjectReference");
static const FName PropertyNetSerializerRegistry_NAME_RemoteServerId("RemoteServerId");
static const FName PropertyNetSerializerRegistry_NAME_RemoteObjectId("RemoteObjectId");

// Info struct for raw object pointers, can be used in RPC parameters
// 处理"裸 UObject*"（非 TObjectPtr）。常见于 RPC 参数。
// 关键：通过 IsSupported 检查 !CPF_TObjectPtr 把 TObjectPtr 路径分流给 FObjectPtrPropertyNetSerializerInfo。
struct FObjectPropertyNetSerializerInfo : public FPropertyNetSerializerInfo
{
	virtual const FFieldClass* GetPropertyTypeClass() const override
	{
		return FObjectProperty::StaticClass();
	}

	virtual bool IsSupported(const FProperty* Property) const
	{
		// Only use this info for raw UObject pointers.
		return !Property->HasAnyPropertyFlags(CPF_TObjectPtr);
	}

	virtual const FNetSerializer* GetNetSerializer(const FProperty* Property) const override
	{
		return &UE_NET_GET_SERIALIZER(FObjectNetSerializer);
	}

	virtual bool CanUseDefaultConfig(const FProperty*) const override { return false; }

	virtual FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const override
	{
		// 把声明的 PropertyClass（如 AActor / UPrimitiveComponent）写入 Config，
		// NetSerializer 在反序列化时可据此做 IsA 校验/类型剪裁。
		FObjectNetSerializerConfig* Config = new (NetSerializerConfigBuffer) FObjectNetSerializerConfig();
		const FObjectPropertyBase* ObjectPtrPropertyBase = static_cast<const FObjectPropertyBase*>(Property);
		Config->PropertyClass = ObjectPtrPropertyBase->PropertyClass;
		return Config;
	}
};
UE_NET_IMPLEMENT_NETSERIALIZER_INFO(FObjectPropertyNetSerializerInfo);

// Info struct for TObjectPtr properties
// TObjectPtr 路径：UE5 引入的"软追踪指针"，序列化层有不同的 Resolve/Snapshot 语义。
struct FObjectPtrPropertyNetSerializerInfo : public FPropertyNetSerializerInfo
{
	virtual const FFieldClass* GetPropertyTypeClass() const override
	{
		return FObjectProperty::StaticClass();
}

	virtual bool IsSupported(const FProperty* Property) const
	{
		// Only use this info for TObjectPtr properties.
		return Property->HasAnyPropertyFlags(CPF_TObjectPtr);
	}

	virtual const FNetSerializer* GetNetSerializer(const FProperty* Property) const override
	{
		return &UE_NET_GET_SERIALIZER(FObjectPtrNetSerializer);
	}

	virtual bool CanUseDefaultConfig(const FProperty*) const override { return false; }

	virtual FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const override
	{
		FObjectPtrNetSerializerConfig* Config = new (NetSerializerConfigBuffer) FObjectPtrNetSerializerConfig();
		const FObjectPropertyBase* ObjectPtrPropertyBase = static_cast<const FObjectPropertyBase*>(Property);
		Config->PropertyClass = ObjectPtrPropertyBase->PropertyClass;
		return Config;
	}
};
UE_NET_IMPLEMENT_NETSERIALIZER_INFO(FObjectPtrPropertyNetSerializerInfo);

// 弱引用属性。FWeakObjectProperty 的 FieldClass 与 FObjectProperty 不同，因此不会跟上面冲突。
struct FWeakObjectPtrPropertyNetSerializerInfo : public FPropertyNetSerializerInfo
{
	virtual const FFieldClass* GetPropertyTypeClass() const override
	{
		return FWeakObjectProperty::StaticClass();
	}

	virtual bool IsSupported(const FProperty* Property) const
	{
		return true;
	}

	virtual const FNetSerializer* GetNetSerializer(const FProperty* Property) const override
	{
		return &UE_NET_GET_SERIALIZER(FWeakObjectNetSerializer);
	}

	virtual bool CanUseDefaultConfig(const FProperty*) const override { return false; }

	virtual FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const override
	{
		FWeakObjectNetSerializerConfig* Config = new (NetSerializerConfigBuffer) FWeakObjectNetSerializerConfig();
		const FObjectPropertyBase* ObjectPtrPropertyBase = static_cast<const FObjectPropertyBase*>(Property);
		Config->PropertyClass = ObjectPtrPropertyBase->PropertyClass;
		return Config;
	}
};
UE_NET_IMPLEMENT_NETSERIALIZER_INFO(FWeakObjectPtrPropertyNetSerializerInfo);


// ---- Soft 对象 / FieldPath 直绑 ----
// SoftObjectProperty 与 SoftClassProperty 共享同一序列化器（FSoftObjectNetSerializer）。
UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FSoftObjectProperty, FSoftObjectNetSerializer);
UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FSoftClassProperty, FSoftObjectNetSerializer);
UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FFieldPathProperty, FFieldPathNetSerializer);

// 按 USTRUCT 名注册的对象路径/远端引用类
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_SoftObjectPath, FSoftObjectPathNetSerializer);
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_SoftClassPath, FSoftClassPathNetSerializer);
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_RemoteObjectReference, FRemoteObjectReferenceNetSerializer);
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_RemoteServerId, FRemoteServerIdNetSerializer);
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_RemoteObjectId, FRemoteObjectIdNetSerializer);

// Strings
// 字符串：
//   - FStrProperty 直接走 FStringNetSerializer（UTF-16 + 长度）；
//   - FNameProperty 默认走 FNameAsNetTokenNetSerializer：
//     用 NetToken（FNameTokenStore 维护的 ID 表）替代每次发送字符串字节，大幅省带宽，
//     代价是首次出现需要导出（Export）一次。这是 Iris 相对旧网络的一项重要优化。
UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FStrProperty, FStringNetSerializer);
//UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FNameProperty, FNameNetSerializer);
// Use NetTokens instead of strings when serializing FNames
UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(FNameProperty, FNameAsNetTokenNetSerializer);

// Named structs with specific serializers
// ---- 数学/工具类常用 USTRUCT 的具名绑定 ----
static const FName PropertyNetSerializerRegistry_NAME_Guid("Guid");
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_Guid, FGuidNetSerializer);
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(NAME_Vector, FVectorNetSerializer);     // 默认 FVector（LWC 决定 float/double）
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(NAME_Vector3f, FVector3fNetSerializer); // 显式 float3
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(NAME_Vector3d, FVector3dNetSerializer); // 显式 double3
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(NAME_Rotator, FRotatorNetSerializer);
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(NAME_Rotator3f, FRotator3fNetSerializer);
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(NAME_Rotator3d, FRotator3dNetSerializer);
// 单位四元数：仅传 3 分量 + 1-bit 符号位重建 w，省 25% 带宽
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(NAME_Quat, FUnitQuatNetSerializer);
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(NAME_Quat4f, FUnitQuat4fNetSerializer);
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(NAME_Quat4d, FUnitQuat4dNetSerializer);

// Vector types. These are structs with custom serialization.
// FVector_NetQuantize{,10,100,Normal} —— 引擎传统的固定量化位深 Vector，常用于 hit/位置数据：
//   100  : 厘米精度
//   10   : 分米精度
//   ()   : 米级量化
//   Normal: [-1,1] 单位向量量化
static const FName PropertyNetSerializerRegistry_NAME_Vector_NetQuantize100("Vector_NetQuantize100");
static const FName PropertyNetSerializerRegistry_NAME_Vector_NetQuantize10("Vector_NetQuantize10");
static const FName PropertyNetSerializerRegistry_NAME_Vector_NetQuantize("Vector_NetQuantize");
static const FName PropertyNetSerializerRegistry_NAME_Vector_NetQuantizeNormal("Vector_NetQuantizeNormal");

static const FName PropertyNetSerializerRegistry_NAME_DateTime("DateTime");

UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_Vector_NetQuantize100, FVectorNetQuantize100NetSerializer);
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_Vector_NetQuantize10, FVectorNetQuantize10NetSerializer);
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_Vector_NetQuantize, FVectorNetQuantizeNetSerializer);
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_Vector_NetQuantizeNormal, FVectorNetQuantizeNormalNetSerializer);
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_DateTime, FDateTimeNetSerializer);

/**
 * ByteProperty when backed by a uint8
 */
// 与 FEnumAsBytePropertyNetSerializerInfo 互斥：ByteProperty 但 Enum == nullptr 才命中。
struct FUint8PropertyNetSerializerInfo : public FPropertyNetSerializerInfo
{
	virtual const FFieldClass* GetPropertyTypeClass() const override { return FByteProperty::StaticClass(); }
	virtual bool IsSupported(const FProperty* Property) const override
	{ 
		const FByteProperty* ByteProperty = CastFieldChecked<FByteProperty>(Property); 
		return !ByteProperty->IsEnum();
	}
	virtual const FNetSerializer* GetNetSerializer(const FProperty* Property) const override { return &UE_NET_GET_SERIALIZER(FUint8NetSerializer); }
	// uint8 走默认 Config 即可，不需要额外字段；这里 BuildNetSerializerConfig 不应被调用——用 checkf 兜底。
	virtual FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const override { checkf(false, TEXT("%s"), TEXT("Internal error. uint8s should use default NetSerializerConfig")); return nullptr; }
};
UE_NET_IMPLEMENT_NETSERIALIZER_INFO(FUint8PropertyNetSerializerInfo);

/**
 * Native bool
 */
// 区分两类 FBoolProperty：
//   IsNativeBool()=true  → 这里（占整字节，FBoolNetSerializer）
//   IsNativeBool()=false → 下面 FBitFieldPropertyNetSerializerInfo（位域）
struct FNativeBoolPropertyNetSerializerInfo : public FPropertyNetSerializerInfo
{
	virtual const FFieldClass* GetPropertyTypeClass() const override { return FBoolProperty::StaticClass(); }
	virtual bool IsSupported(const FProperty* Property) const override { const FBoolProperty* BoolProperty = CastFieldChecked<const FBoolProperty>(Property); return BoolProperty->IsNativeBool(); }
	virtual const FNetSerializer* GetNetSerializer(const FProperty* Property) const override { return &UE_NET_GET_SERIALIZER(FBoolNetSerializer); }
	// native bool 也走默认 Config
	virtual FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const override { checkf(false, TEXT("%s"), TEXT("Internal error. Bools should use default NetSerializerConfig")); return nullptr; }
};
UE_NET_IMPLEMENT_NETSERIALIZER_INFO(FNativeBoolPropertyNetSerializerInfo);

/**
 * Bool as bitfield
 */
// 位域 bool（uint8 BoolValue:1; 风格的 UPROPERTY）：使用 FBitfieldNetSerializer，
// Config 中保存 ByteOffset 与 BitMask 等位偏信息以便把 1 位单独编入流。
struct FBitFieldPropertyNetSerializerInfo : public FPropertyNetSerializerInfo
{
	virtual const FFieldClass* GetPropertyTypeClass() const override { return FBoolProperty::StaticClass(); }
	virtual bool IsSupported(const FProperty* Property) const override { const FBoolProperty* BoolProperty = CastFieldChecked<const FBoolProperty>(Property); return !BoolProperty->IsNativeBool(); }
	virtual const FNetSerializer* GetNetSerializer(const FProperty* Property) const override {return &UE_NET_GET_SERIALIZER(FBitfieldNetSerializer); }
	virtual bool CanUseDefaultConfig(const FProperty* Property) const override { return false; }
	virtual FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const override
	{
		FBitfieldNetSerializerConfig* Config = new (NetSerializerConfigBuffer) FBitfieldNetSerializerConfig();

		// 从 FBoolProperty 中取 ByteOffset/FieldMask 等填到 Config 里。
		InitBitfieldNetSerializerConfigFromProperty(*Config, CastFieldChecked<FBoolProperty>(Property));
	
		return Config;
	}
};
UE_NET_IMPLEMENT_NETSERIALIZER_INFO(FBitFieldPropertyNetSerializerInfo);

/**
 * ScriptInterface NetSerializerInfo
 */
// TScriptInterface<I> 属性。Config 中 PropertyClass 实际存的是 InterfaceClass，
// FScriptInterfaceNetSerializer 反序列化时据此把 UObject* 解析+wrap 成 FScriptInterface。
struct FScriptInterfacePropertyNetSerializerInfo : public FPropertyNetSerializerInfo
{
	virtual const FFieldClass* GetPropertyTypeClass() const override { return FInterfaceProperty::StaticClass(); }
	virtual const FNetSerializer* GetNetSerializer(const FProperty*) const override { return &UE_NET_GET_SERIALIZER(FScriptInterfaceNetSerializer); }
	virtual bool CanUseDefaultConfig(const FProperty*) const override { return false; }
	virtual FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const override
	{
		FScriptInterfaceNetSerializerConfig* Config = new (NetSerializerConfigBuffer) FScriptInterfaceNetSerializerConfig();
		const FInterfaceProperty* InterfaceProperty = static_cast<const FInterfaceProperty*>(Property);
		Config->PropertyClass = InterfaceProperty->InterfaceClass;
		return Config;
	}
};
UE_NET_IMPLEMENT_NETSERIALIZER_INFO(FScriptInterfacePropertyNetSerializerInfo);

/**
 * FText NetSerializerInfo, special forwarding to LastResortNetSerializer excluding default state hash.
 */
// FText 是 Iris 的"边界条件"：内容高度依赖本地化（LocService、参数化等），无法用通用 NetSerializer 量化，
// 也不能让其参与 DefaultStateHash 计算（否则不同 locale 上 Descriptor::Identifier 会漂移）。
// 所以走 FLastResortPropertyNetSerializer 兼容路径 + bExcludeFromDefaultStateHash=true 排除哈希。
struct FTextPropertyNetSerializerInfo : public FLastResortPropertyNetSerializerInfo
{
	FTextPropertyNetSerializerInfo() : FLastResortPropertyNetSerializerInfo()
	{
		bExcludeFromDefaultStateHash = true;
	}

	virtual const FFieldClass* GetPropertyTypeClass() const override
	{
		return FTextProperty::StaticClass();
	}
};
UE_NET_IMPLEMENT_NETSERIALIZER_INFO(FTextPropertyNetSerializerInfo);

// $TODO: Investigate automatic registration by binding all infos in static chain and auto-register when we freeze the registry
/**
 * Apart from the specialized serializer we register here we support the following:
 * Structs. Structs without specialized serializer will use the StructNetSerializer.
 * UArrayProperty will use ArrayPropertyNetSerializer.
 * As a last resort for properties we use LastResortPropertyNetSerializer. It's by no means ideal.
 */
// RegisterDefaultPropertyNetSerializerInfos —— 把上面所有 file-static Info 单例集中 Register 到 Registry。
// 由 IrisCoreModule 在 PreFreeze 广播之后立即调用；之后 Freeze 排序，注册表进入"基本不可变"阶段。
//
// 注意点：
//   - 每次 UE_NET_REGISTER_NETSERIALIZER_INFO(Name) 展开为 Registry::Register(&GetPropertyNetSerializerInfo_##Name())；
//   - 由于多个 Info 可能挂在同一 FieldClass 下（FByteProperty 有 3 个、FBoolProperty 有 2 个），
//     注册顺序在 Registry 内会被 Freeze() 重新排序；段内顺序无定义，正确性由 IsSupported 互斥保障。
void RegisterDefaultPropertyNetSerializerInfos()
{
	// Register supported types
	// Integer types
	UE_NET_REGISTER_NETSERIALIZER_INFO(FInt8Property);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FInt16Property);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FIntProperty);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FInt64Property);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FUint8PropertyNetSerializerInfo);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FUInt16Property);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FUInt32Property);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FUInt64Property);

	// Enum types
	// 三个 Enum Info 互斥分流：EnumAsByte / NetRole / FEnumProperty
	UE_NET_REGISTER_NETSERIALIZER_INFO(FEnumAsBytePropertyNetSerializerInfo);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FEnumPropertyNetSerializerInfo);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FNetRoleNetSerializerInfo);

	// Float types
	UE_NET_REGISTER_NETSERIALIZER_INFO(FFloatProperty);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FDoubleProperty);

	// Object and field types
	// 三个 ObjectProperty 子 Info（raw / TObjectPtr / WeakObject）+ 接口 / 软引用 / FieldPath
	UE_NET_REGISTER_NETSERIALIZER_INFO(FObjectPropertyNetSerializerInfo);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FObjectPtrPropertyNetSerializerInfo);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FWeakObjectPtrPropertyNetSerializerInfo);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FScriptInterfacePropertyNetSerializerInfo);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FSoftObjectProperty);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FFieldPathProperty);
	UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_SoftObjectPath);
	UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_SoftClassPath);
	UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_RemoteObjectReference);
	UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_RemoteServerId);
	UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_RemoteObjectId);

	// String types
	UE_NET_REGISTER_NETSERIALIZER_INFO(FNameProperty);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FStrProperty);

	// FTextProperty is using a special variant of LastResortNetSerializer that does not serialize defaultstatehash.
	UE_NET_REGISTER_NETSERIALIZER_INFO(FTextPropertyNetSerializerInfo);

	// Special types
	// 两个 BoolProperty 子 Info：native bool / 位域
	UE_NET_REGISTER_NETSERIALIZER_INFO(FNativeBoolPropertyNetSerializerInfo);
	UE_NET_REGISTER_NETSERIALIZER_INFO(FBitFieldPropertyNetSerializerInfo);

	// Named structs that we support
	// 数学/工具 USTRUCT 一组
	UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_Guid);
	UE_NET_REGISTER_NETSERIALIZER_INFO(NAME_Vector);
	UE_NET_REGISTER_NETSERIALIZER_INFO(NAME_Vector3f);
	UE_NET_REGISTER_NETSERIALIZER_INFO(NAME_Vector3d);
	UE_NET_REGISTER_NETSERIALIZER_INFO(NAME_Rotator);
	UE_NET_REGISTER_NETSERIALIZER_INFO(NAME_Rotator3f);
	UE_NET_REGISTER_NETSERIALIZER_INFO(NAME_Rotator3d);
	UE_NET_REGISTER_NETSERIALIZER_INFO(NAME_Quat);
	UE_NET_REGISTER_NETSERIALIZER_INFO(NAME_Quat4f);
	UE_NET_REGISTER_NETSERIALIZER_INFO(NAME_Quat4d);
	UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_Vector_NetQuantize100);
	UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_Vector_NetQuantize10);
	UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_Vector_NetQuantize);
	UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_Vector_NetQuantizeNormal);
	UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_DateTime);
}

}

