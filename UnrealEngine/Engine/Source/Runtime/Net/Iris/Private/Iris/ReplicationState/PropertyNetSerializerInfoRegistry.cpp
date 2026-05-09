// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// PropertyNetSerializerInfoRegistry.cpp —— 注册表实现 + Default/LastResort/Nop 回退 Info 的实现
// ---------------------------------------------------------------------------------------------------------------------
// 本文件实现头中声明的：
//   - FPropertyNetSerializerInfoRegistry::Register/Unregister/Reset/Freeze/Find*
//   - FNopNetSerializerInfo（GetNopNetSerializerInfo 返回的单例）
//   - FDefaultStructPropertyNetSerializerInfo（FStructProperty 兜底，走 FStructNetSerializer）
//   - FDefaultArrayPropertyNetSerializerInfo（FArrayProperty 兜底，走 FArrayPropertyNetSerializer）
//   - FLastResortPropertyNetSerializerInfo / FNamedStructLastResortPropertyNetSerializerInfo 各成员
//   - ValidateForwardingNetSerializerTraits（trait 一致性校验）
//
// 关键运行机制说明：
//   1) Registry 是一张排序数组 + bRegistryIsDirty 标记，惰性 sort：Register 时仅置 dirty，下次 Find 第一次调用
//      自动 Freeze 一次。这样支持启动期"先注册一堆再用"，也避免每次 Find 都 sort。
//   2) FindSerializerInfo 的两层匹配：
//        a) IndexOfByPredicate(IsA) —— 因为已按 FieldClass 指针排序，所以同 FieldClass 的多个 Info 是连续的；
//           IsA 匹配也会对父类生效（FObjectProperty IsA FProperty 等），但 Iris 习惯把 Info 按"具体类"注册，
//           所以扫描到第一段 IsA 命中后只在该段内细筛即可。
//        b) 段内逐项 IsSupported() —— 用 Property 的实例性质（IsEnum / IsNativeBool / TObjectPtr 等）做精筛。
//   3) 三级兜底：no-match → FStructProperty → DefaultStruct，FArrayProperty → DefaultArray，其他 → LastResort。
//      Default 系列 Info 自身**不在 Registry 中**——它们由 IMPLEMENT_NETSERIALIZER_INFO 暴露访问器，
//      Find 路径直接 GetPropertyNetSerializerInfo_FDefault* 取单例返回，避免污染主匹配段。
//   4) ValidateForwardingNetSerializerTraits：当一个 Struct 的"自定义 NetSerializer"被 Iris 转发使用时，
//      要求 NetSerializer 必须正确声明 traits（HasDynamicState 等），否则 Apply/Quantize 会跳过 dyn-state 释放等关键步骤。
//      漏标即 LowLevelFatalError 中止——这是开发期防呆，不可被代码忽略。
// =====================================================================================================================

#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#include "Iris/ReplicationState/EnumPropertyNetSerializerInfo.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Templates/Sorting.h"
#include "UObject/UnrealType.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/InternalNetSerializers.h"

namespace UE::Net::Private
{
	// FNopNetSerializerInfo —— 用于"挂在 Descriptor 上但不实际复制"的占位 Info。
	// 例：某些 metadata-only 的成员需要进入 Descriptor 以让上层系统拿到引用，但不应消耗带宽。
	// 注意它不在 Registry 中——通过 FPropertyNetSerializerInfoRegistry::GetNopNetSerializerInfo() 直接返回。
	struct FNopNetSerializerInfo : public FPropertyNetSerializerInfo
	{
		virtual const FFieldClass* GetPropertyTypeClass() const { return nullptr; }
		virtual bool IsSupported(const FProperty* Property) const { return true; }
		virtual const FNetSerializer* GetNetSerializer(const FProperty* Property) const { return &UE_NET_GET_SERIALIZER(FNopNetSerializer); }
		virtual bool CanUseDefaultConfig(const FProperty* Property) const { return true; }
		virtual const FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const { return nullptr; }
	} static const NopNetSerializerInfo;

}

namespace UE::Net
{

// 进程级静态成员定义
FPropertyNetSerializerInfoRegistry::FNetSerializerInfoRegistry FPropertyNetSerializerInfoRegistry::Registry;
bool FPropertyNetSerializerInfoRegistry::bRegistryIsDirty = false;

void FPropertyNetSerializerInfoRegistry::Register(const FPropertyNetSerializerInfo* Info)
{
	check(Info);

	const FFieldClass* PropertyClass = Info->GetPropertyTypeClass();

	check(PropertyClass);

	// AddUnique 防重复——多次 PreFreeze 广播或同一 Info 被两处宏注册都不会变成重复条目。
	Registry.AddUnique(MakeTuple<>(PropertyClass, Info));

	// 标记 dirty，下次 Find 触发的 Freeze 会重新排序。
	bRegistryIsDirty = true;
}

void FPropertyNetSerializerInfoRegistry::Unregister(const FPropertyNetSerializerInfo* Info)
{
	// 按指针匹配（同一个 Meyers' Singleton 在生命周期内地址稳定）。
	Registry.RemoveAll([Info](const FRegistryEntry& Entry) { return Entry.Get<1>() == Info; });
	bRegistryIsDirty = true;
}

void FPropertyNetSerializerInfoRegistry::Reset()
{
	Registry.Empty();
	bRegistryIsDirty = false;
}

void FPropertyNetSerializerInfoRegistry::Freeze()
{
	if (bRegistryIsDirty)
	{
		// Sort by StaticClass pointer
		// 按 FFieldClass 指针值排序，同一 FieldClass 的多个 Info 由此聚集成连续段，
		// 后续 Find 在 IsA 命中后可只在段内线性细筛。
		Registry.Sort([](const FRegistryEntry& A, const FRegistryEntry& B) { return A.Get<0>() < B.Get<0>();} );
		bRegistryIsDirty = false;
	}
}

/** Default struct Serializer info */
// 兜底 Struct Info：当一个 USTRUCT 没有任何具名 Info 命中时（即既不是 Vector/Quat/Guid/...
// 也未通过 UE_NET_IMPLEMENT_FORWARDING_NETSERIALIZER_AND_REGISTRY_DELEGATES 注册具名转发），
// 就用 FStructNetSerializer 递归处理其成员（递归构造子 Descriptor）。
// 不在 Registry 中——由 FindSerializerInfo 走 FStructProperty 分支直接取。
struct FDefaultStructPropertyNetSerializerInfo : public FPropertyNetSerializerInfo
{		
	virtual const FFieldClass* GetPropertyTypeClass() const override { return FStructProperty::StaticClass(); }
	virtual bool IsSupported(const FProperty* Property) const override { return true; }
	virtual const FNetSerializer* GetNetSerializer(const FProperty* Property) const override { return &UE_NET_GET_SERIALIZER(FStructNetSerializer); }
	virtual bool CanUseDefaultConfig(const FProperty* Property) const override { return false; }
	// FStructNetSerializer 的 Config 由 Builder 在外部填充（指向 sub-Descriptor），此处返回 nullptr 仅占位。
	virtual FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const override { return nullptr; }
};
UE_NET_IMPLEMENT_NETSERIALIZER_INFO(FDefaultStructPropertyNetSerializerInfo);

/** Default dynamic array Serializer info */
// 兜底 Array Info：当 FArrayProperty 未被任何具名 Info 接管时使用 FArrayPropertyNetSerializer。
// IsSupported 会递归查 Inner 是否能被注册表识别——若内部元素类型没有可用 Info，则 ArrayProperty 也无法支持
// （会被 FindSerializerInfo 进一步回退到 LastResort）。
struct FDefaultArrayPropertyNetSerializerInfo : public FPropertyNetSerializerInfo
{		
	virtual const FFieldClass* GetPropertyTypeClass() const override { return FArrayProperty::StaticClass(); }
	virtual bool IsSupported(const FProperty* Property) const override { return FPropertyNetSerializerInfoRegistry::FindSerializerInfo(CastFieldChecked<FArrayProperty>(Property)->Inner) != nullptr; }
	virtual const FNetSerializer* GetNetSerializer(const FProperty* Property) const override { return &UE_NET_GET_SERIALIZER(FArrayPropertyNetSerializer); }
	virtual bool CanUseDefaultConfig(const FProperty* Property) const override { return false; }
	virtual FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const override { return nullptr; }
};
UE_NET_IMPLEMENT_NETSERIALIZER_INFO(FDefaultArrayPropertyNetSerializerInfo);

/** Last resort for properties that have no specialized serializer. An example is TextProperty. */
// 默认 MaxQuantizedSizeBits 来自 FLastResortPropertyNetSerializerConfig::DefaultMaxQuantizedSizeBits。
FLastResortPropertyNetSerializerInfo::FLastResortPropertyNetSerializerInfo()
	: FPropertyNetSerializerInfo()
	, MaxQuantizedSizeBits(FLastResortPropertyNetSerializerConfig::DefaultMaxQuantizedSizeBits)
{
}

FLastResortPropertyNetSerializerInfo::FLastResortPropertyNetSerializerInfo(const uint32 InMaxQuantizedSizeBits)
	: FPropertyNetSerializerInfo()
	, MaxQuantizedSizeBits(InMaxQuantizedSizeBits)
{
}

const FFieldClass* FLastResortPropertyNetSerializerInfo::GetPropertyTypeClass() const
{
	// 注意是基类 FProperty——这意味着 IsA 匹配会"碾压"任何子类，因此 LastResort Info 不应进入主 Registry。
	// 实际上它由 FindSerializerInfo 在所有匹配失败后通过 GetPropertyNetSerializerInfo_FLastResortPropertyNetSerializerInfo 直接取。
	return FProperty::StaticClass();
}

bool FLastResortPropertyNetSerializerInfo::IsSupported(const FProperty* Property) const
{ 
	return true; 
}

const FNetSerializer* FLastResortPropertyNetSerializerInfo::GetNetSerializer(const FProperty* Property) const
{
	return &UE_NET_GET_SERIALIZER(FLastResortPropertyNetSerializer);
}

bool FLastResortPropertyNetSerializerInfo::CanUseDefaultConfig(const FProperty* Property) const
{
	return false; 
}

FNetSerializerConfig* FLastResortPropertyNetSerializerInfo::BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const
{
	// placement-new 出 Config 并按 Property 元数据填充：
	// InitLastResortPropertyNetSerializerConfigFromProperty 会保存属性的尺寸/对齐/Property 指针等
	// 以便 LastResortNetSerializer 在量化/反量化时使用 FProperty::NetSerialize 的旧路径。
	FLastResortPropertyNetSerializerConfig* Config = new (NetSerializerConfigBuffer) FLastResortPropertyNetSerializerConfig();
	InitLastResortPropertyNetSerializerConfigFromProperty(*Config, Property);
	Config->bExcludeFromDefaultStateHash = bExcludeFromDefaultStateHash;
	Config->MaxQuantizedSizeBits = MaxQuantizedSizeBits;

	return Config;
}
UE_NET_IMPLEMENT_NETSERIALIZER_INFO(FLastResortPropertyNetSerializerInfo);

// FNamedStructLastResortPropertyNetSerializerInfo 的具名 struct 检查
const FFieldClass* FNamedStructLastResortPropertyNetSerializerInfo::GetPropertyTypeClass() const
{
	return FStructProperty::StaticClass();
}

bool FNamedStructLastResortPropertyNetSerializerInfo::IsSupported(const FProperty* Property) const
{
	const FStructProperty* StructProp = CastFieldChecked<const FStructProperty>(Property);
	return IsSupportedStruct(StructProp->Struct->GetFName());
}

const FPropertyNetSerializerInfo* FPropertyNetSerializerInfoRegistry::FindSerializerInfo(const FProperty* Property)
{
	// Make sure that the registry is sorted to avoid doing IsA multiple times for the same property type
	// 惰性 sort：若上次 Register/Unregister 后未 Freeze，则在此先排一次。
	Freeze();

	// first find Index containing a info matching the type of the property
	// 顺序扫描首个 IsA 命中的条目；同 FieldClass 段从此 Index 起连续。
	int32 Index = Registry.IndexOfByPredicate([Property](const FRegistryEntry& Element) { return Property->IsA(Element.Get<0>());} );
	
	if (Index != INDEX_NONE)
	{
		// Check if we got a match
		// as we allow multiple infos for the same class type we need to check if the info supports the Property
		// 段内细筛：直到 ClassType 改变才停。
		const FFieldClass* ClassType = Registry[Index].Get<0>();
		for (; Index < Registry.Num() && Registry[Index].Get<0>() == ClassType; ++Index)
		{
			const FPropertyNetSerializerInfo* Info = Registry[Index].Get<1>();
			if (Info->IsSupported(Property))
			{
				return Info;
			}
		}
	}
		
	// We did not find a match, handle default cases.
	
	// Check if this is a struct and fall back on default.
	// USTRUCT 在主 Registry 没命中（既不是 NamedStruct 也没有自定义）→ 使用 FStructNetSerializer 递归处理。
	if (Property->IsA(FStructProperty::StaticClass()))
	{
		return &GetPropertyNetSerializerInfo_FDefaultStructPropertyNetSerializerInfo();
	}

	// Check if this is a dynamic array and the serializer supports it.
	// 动态数组 → 检查内部元素是否能被序列化，若可则使用 FArrayPropertyNetSerializer。
	if (Property->IsA(FArrayProperty::StaticClass()))
	{
		const FPropertyNetSerializerInfo& Info = GetPropertyNetSerializerInfo_FDefaultArrayPropertyNetSerializerInfo();
		if (Info.IsSupported(Property))
		{
			return &Info;
		}
	}

	// Last resort!
	// 最终兜底：FLastResortPropertyNetSerializer（旧 NetSerialize 字节流）。
	// 注意 LastResort 永远会"匹配成功"，所以 FindSerializerInfo 不会返回 nullptr——除非整个 Registry 状态异常。
	return &GetPropertyNetSerializerInfo_FLastResortPropertyNetSerializerInfo();
}

const FPropertyNetSerializerInfo* FPropertyNetSerializerInfoRegistry::FindStructSerializerInfo(const FName Name)
{
	// Make sure that the registry is sorted to avoid doing IsA multiple times for the same property type
	Freeze();

	// 直接在 FStructProperty 段内按 StructName 找。常用于：
	//   - 没有 FProperty 实例的运行时调用（例如 PolymorphicStructNetSerializer 按 RegisteredTypes 动态构造 sub-Descriptor）。
	const FFieldClass* ClassType = FStructProperty::StaticClass();

	// first find Index containing a info matching the type of the property
	int32 Index = Registry.IndexOfByPredicate([ClassType](const FRegistryEntry& Element) { return Element.Get<0>() == ClassType; } );
	
	if (Index != INDEX_NONE)
	{
		// Check if we got a match on the name
		// as we allow multiple infos for the same class type we need to check if the info supports the Property
		for (; Index < Registry.Num() && Registry[Index].Get<0>() == ClassType; ++Index)
		{
			const FPropertyNetSerializerInfo* Info = Registry[Index].Get<1>();
			if (Info->IsSupportedStruct(Name))
			{
				return Info;
			}
		}
	}
		
	// 没有具名匹配（注意此处不会回退到 DefaultStruct/LastResort——调用者需要自行决定如何处理）。
	return nullptr;
}

const FPropertyNetSerializerInfo* FPropertyNetSerializerInfoRegistry::GetNopNetSerializerInfo()
{
	return &Private::NopNetSerializerInfo;
}

// 校验 NetSerializer 的 traits 是否与 ReplicationState 一致。
// 当一个 Struct 通过转发使用一个内部 NetSerializer 时，state-level traits 由 Builder 推导，
// 但 NetSerializer 自己声明的 traits（ENetSerializerTraits）是源头。两者必须一致，否则会出现：
//   - HasDynamicState 不一致 → Quantize 后 dyn-state 资源不会释放，泄漏；
//   - HasObjectReference 不一致 → CollectReferences 漏掉，PIE 重映射失效；
//   - HasConnectionSpecificSerialization 不一致 → 客户端解码错位。
// 因此一旦发现，立即 LowLevelFatalError——这是 Iris 的硬约束。
void ValidateForwardingNetSerializerTraits(const FNetSerializer* Serializer, EReplicationStateTraits UsedReplicationStateTraits)
{
	const ENetSerializerTraits SerializerTraits = Serializer->Traits;
	if (EnumHasAnyFlags(UsedReplicationStateTraits, EReplicationStateTraits::HasDynamicState) && !EnumHasAnyFlags(SerializerTraits, ENetSerializerTraits::HasDynamicState))
	{
		LowLevelFatalError(TEXT("FNetSerializer: %s is using serializer(s) that HasDynamicState without setting trait: static constexpr bool bHasDynamicState = true; in the serializer declaration."), Serializer->Name);
	}
	if (EnumHasAnyFlags(UsedReplicationStateTraits, EReplicationStateTraits::HasObjectReference) && !EnumHasAnyFlags(SerializerTraits, ENetSerializerTraits::HasCustomNetReference))
	{
		LowLevelFatalError(TEXT("FNetSerializer: %s is using serializer(s) that has trait HasCustomNetReference without setting trait: static constexpr bool bHasCustomNetReference = true; in the serializer declaration."), Serializer->Name);
	}
	if (EnumHasAnyFlags(UsedReplicationStateTraits, EReplicationStateTraits::HasConnectionSpecificSerialization) && !EnumHasAnyFlags(SerializerTraits, ENetSerializerTraits::HasConnectionSpecificSerialization))
	{
		LowLevelFatalError(TEXT("FNetSerializer: %s is using serializer(s) that has HasConnectionSpecificSerialization without setting trait: static constexpr bool bHasConnectionSpecificSerialization = true; in the serializer declaration."), Serializer->Name);
	}
}

}
