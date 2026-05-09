// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// PropertyNetSerializerInfoRegistry.h —— Property → NetSerializer 映射的注册表（Iris 反射桥的关键）
// ---------------------------------------------------------------------------------------------------------------------
// 这个文件是 Iris ReplicationState 模块"反射烘焙"环节最核心的对外扩展点之一。
// 当 FReplicationStateDescriptorBuilder 对 UClass / UScriptStruct / UFunction 反射遍历每个 FProperty 时，
// 它需要回答一个问题："这个 FProperty 应该用哪个 FNetSerializer 来量化/反量化？"
// 该问题的答案就由本文件提供的两套机制给出：
//
//   1) FPropertyNetSerializerInfo（虚接口）：每个支持类型实现一个子类，描述
//        - 接受哪个 FProperty FieldClass（FIntProperty / FStructProperty / ...）
//        - 是否支持给定 Property 实例（IsSupported——用于同一 FieldClass 下的细分，如 EnumAsByte vs uint8）
//        - 选用哪个 FNetSerializer
//        - 是否需要构造定制的 FNetSerializerConfig（如 FBitfieldNetSerializerConfig 的位偏）
//        - 可选：自定义 FReplicationFragment 创建函数（极少数 struct 才需要）
//
//   2) FPropertyNetSerializerInfoRegistry（静态注册表）：以 (FFieldClass*, FPropertyNetSerializerInfo*) 元组数组保存所有 Info；
//        - 启动期 Register / 卸载期 Unregister；
//        - Freeze() 按 FieldClass 指针排序（提升后续 Find 局部性，并使同一 FieldClass 的多 Info 连续聚集）；
//        - FindSerializerInfo(FProperty*) 顺序扫描"第一条 IsA 命中的 FieldClass 段"，再逐条 IsSupported 命中即返回；
//          找不到时按 FStructProperty → FDefaultStructPropertyNetSerializerInfo、
//                       FArrayProperty  → FDefaultArrayPropertyNetSerializerInfo、
//                       其他            → FLastResortPropertyNetSerializerInfo（兜底）
//          的优先级回退；
//        - FindStructSerializerInfo(FName) 则对 USTRUCT 名字检索（如 "Vector"、"Quat"、"Guid"）；
//        - GetNopNetSerializerInfo() 提供"绑定但不复制"的 Nop 形式（meta data only 场景）。
//
// 文件还提供了一组宏，用于把"声明、实现、注册、卸载"四步样板代码压成单行：
//   UE_NET_DECLARE_NETSERIALIZER_INFO            —— 头文件中导出 GetPropertyNetSerializerInfo_X 单例访问器
//   UE_NET_IMPLEMENT_NETSERIALIZER_INFO          —— cpp 中实现单例（Meyers' Singleton）
//   UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO   —— 一行实现"PropertyType ↔ Serializer"的直接绑定
//   UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO[_WITH_CUSTOM_FRAGMENT]
//                                                —— 按 USTRUCT 名注册（Vector/Quat/...）
//   UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_INFO[_WITH_SIZE_OVERRIDE]
//                                                —— 兜底版本，把整个 struct 走 LastResortNetSerializer
//   UE_NET_REGISTER_NETSERIALIZER_INFO           —— Registry::Register(...)
//   UE_NET_UNREGISTER_NETSERIALIZER_INFO         —— Registry::Unregister(...)（monolithic 构建是 no-op）
//   UE_NET_IMPLEMENT_NETSERIALIZER_REGISTRY_DELEGATES
//                                                —— 自动派生 FNetSerializerRegistryDelegates，把 Register/Unregister
//                                                  挂到 PreFreeze/析构两个时机上
//   UE_NET_IMPLEMENT_FORWARDING_NETSERIALIZER_AND_REGISTRY_DELEGATES
//                                                —— 上面三件套一气呵成（命名 struct → Serializer + 自动注册）
//   UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_AND_REGISTRY_DELEGATES[_WITH_SIZE_OVERRIDE]
//                                                —— 同上，但走 LastResort
//
// 生命周期协议（与 Serialization 模块的 FNetSerializerRegistryDelegates 协作）：
//   FIrisCoreModule::StartupModule()
//     → RegisterPropertyNetSerializerSelectorTypes()
//        → 广播 PreFreeze（各模块的 Delegates 在此时调用 UE_NET_REGISTER_NETSERIALIZER_INFO）
//        → RegisterDefaultPropertyNetSerializerInfos()（注册引擎内置类型）
//        → FPropertyNetSerializerInfoRegistry::Freeze()（排序）
//        → 广播 PostFreeze
//   模块卸载（仅非 monolithic）：Delegates 析构 → UE_NET_UNREGISTER_NETSERIALIZER_INFO。
//
// 重要约束：Freeze() 之后注册表本质应被视为不可变；运行时再 Register 会重新触发 bRegistryIsDirty
// 并在下次 Find 时重新排序。Iris 不支持热插拔 Serializer。
// =====================================================================================================================

#pragma once

#include "Containers/Array.h"
#include "Iris/Serialization/NetSerializer.h"
#include "Iris/Serialization/NetSerializerDelegates.h"
#include "Templates/Casts.h"
#include "Templates/Tuple.h"
#include "UObject/NameTypes.h"
#include "UObject/UnrealType.h"

namespace UE::Net
{

// 前置声明：FReplicationFragment 是 ReplicationSystem 模块定义的"片段（一段可复制状态 + 行为）"接口；
// CreateAndRegisterReplicationFragmentFunc 用于让 USTRUCT 自定义 Fragment 创建（高级且不鼓励的用法）。
class FFragmentRegistrationContext;
class FReplicationFragment;
struct FReplicationStateDescriptor;
enum class EReplicationStateTraits : uint32;
typedef FReplicationFragment* (*CreateAndRegisterReplicationFragmentFunc)(UObject*, const FReplicationStateDescriptor*, FFragmentRegistrationContext&);

}

namespace UE::Net
{

/**
 * This function should be called in the following way to take advantage of SFINAE and perform compile-time
 * error checking for declared types:
 *     static_assert(IsDeclaredType((struct MyStruct*)Ptr), "Error message..");
 *
 * If MyStruct is not declared, the cast will pass a void* pointer to IsDeclaredType(). Otherwise, if MyStruct
 * is declared then the cast will pass a MyStruct* pointer to IsDeclaredType().
 */
// 编译期检测：StructName 是否为已声明的 UE 类型。
// 通过模板 + SFINAE：若 StructName 已完整声明，sizeof(StructName) 合法 → 走第一个重载返回 true；
// 否则把 nullptr cast 成 void* 命中下面的重载返回 false。
// 主要用途见 UE_NET_IS_DECLARED_TYPE 宏：在写死类型名的注册场景下，把"打错或缺前缀"提前到编译期。
template<typename StructName>
constexpr auto IsDeclaredType(StructName*) -> decltype(sizeof(StructName))
{
	return true;
}

// 兜底重载——任何未声明类型 cast 都会衰减到 void*，落到此处返回 false。
constexpr auto IsDeclaredType(void*)
{
	return false;
}

/**
 * Currently we require each supported type to register FPropertyNetSerializerInfo 
 * It provides information on what NetSerializer to use for which property and how to build the required NetSerializer config which is used when we build the dynamic descriptor
 * It is possible to register multiple FPropertyNetSerializerInfo for the same PropertyType-class as long as the IsSupportedFunction only matches a single Property, i.e. bool/nativebool enums of different sizes
 */
// FPropertyNetSerializerInfo —— 描述"某类 FProperty 应该如何映射到 NetSerializer + Config"的虚接口。
// 实现思路：
//   - 同一 FFieldClass（例如 FBoolProperty）允许注册多个 Info：一个用于 native bool、一个用于 bitfield，
//     在 IsSupported() 中分流；
//   - 同一 FStructProperty 也允许注册多个 Info：通过 StructName 字段做名字区分（FNamedStructPropertyNetSerializerInfo）；
//   - 没有 IsSupported 命中时由 FindSerializerInfo 走 Default/LastResort 兜底。
struct FPropertyNetSerializerInfo
{
	// 返回此 Info 想接管的 FProperty 子类型（如 FIntProperty::StaticClass()）。
	// Registry 用此值做 IsA 命中检测；nullptr 视为兜底（仅 NopNetSerializerInfo 会用到）。
	virtual const FFieldClass* GetPropertyTypeClass() const { return nullptr; }

	// 在 GetPropertyTypeClass() IsA 命中后做精筛：例如同样是 FByteProperty，
	// "EnumAsByte" 与 "原始 uint8" 走两个不同 Info；返回 false 时 Registry 继续往下找同类 Info。
	virtual bool IsSupported(const FProperty* Property) const { return true; }

	// 实际选用的 FNetSerializer。子类通常引用全局单例（UE_NET_GET_SERIALIZER(...)）。
	virtual const FNetSerializer* GetNetSerializer(const FProperty* Property) const { return nullptr; }

	// 是否使用 NetSerializer 默认 Config（无需自定义）。
	// 返回 false 时 Builder 会调用 BuildNetSerializerConfig() 让 Info 在指定缓冲上 placement-new 出 Config。
	virtual bool CanUseDefaultConfig(const FProperty* Property) const { return true; }

	// 在外部提供的内存上 placement-new 出 NetSerializerConfig（Builder 会预分配，无需此处 alloc）。
	// 用于把 FProperty 反射出来的元数据（PropertyClass / Enum / 位偏 / 边界）回写到 Config 中。
	virtual const FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const { return nullptr; }

	/** Custom replication fragments are currently only supported by structs with a custom NetSerializer. See UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_WITH_CUSTOM_FRAGMENT_INFO. */
	// 自定义 Fragment 工厂函数（极少数 struct 才需要，例如 FastArray）。
	// 默认返回 nullptr 表示走标准 Fragment 流程；非 nullptr 时 Builder 会绑定到 Descriptor::CreateAndRegisterReplicationFragmentFunction。
	virtual CreateAndRegisterReplicationFragmentFunc GetCreateAndRegisterReplicationFragmentFunction() const { return nullptr; }

	// 为按"USTRUCT 名字"注册的 NamedStruct 系列 Info 提供匹配辅助。
	bool IsSupportedStruct(FName InStructName) const
	{
		return StructName == InStructName;
	}

protected:
	// Used by named struct serializers.
	// 仅 FNamedStructPropertyNetSerializerInfo / FNamedStructLastResortPropertyNetSerializerInfo 使用。
	FName StructName;
};

/**
 *  This is a simple static registry used by the ReplicationStateDescriptorBuiider when building descriptors using properties
 */
// FPropertyNetSerializerInfoRegistry —— 进程级单实例的 (FFieldClass, FPropertyNetSerializerInfo*) 数组。
// 它不是真正的 map：因为 FProperty IsA 是顺序匹配（基类亦命中），所以使用排序后的数组 + 顺序扫描已经足够快；
// 同一 FieldClass 的多个 Info 在排序后会聚集在一起，IndexOfByPredicate 只需一次定位再线性细筛。
class FPropertyNetSerializerInfoRegistry
{
public:
	/** Register FPropertyNetSerializerInfo in the registry */
	// 把一个 Info 加入注册表（去重）。Register 后会把 bRegistryIsDirty 置 true，下一次 Find 时再 Sort。
	IRISCORE_API static void Register(const FPropertyNetSerializerInfo* Info);
	// 移除指定 Info（按指针匹配）。仅在非 monolithic 构建中由模块卸载流程调用。
	IRISCORE_API static void Unregister(const FPropertyNetSerializerInfo* Info);

	/** Reset the registry */
	// 清空所有 Info（测试或全局重启使用）。
	IRISCORE_API static void Reset();

	/** Sort entries in registry on property type */
	// 按 FFieldClass* 指针值排序。这样可以把同一 FieldClass 的多 Info 聚拢，使 Find 在命中后线性细筛。
	// 注意：这是 quasi-Freeze——Register 之后 dirty 为 true，下次 Find 会自动再 Freeze 一次，但 Iris 设计上
	// 期望仅在启动期 Register、Freeze 后视为不可变。
	IRISCORE_API static void Freeze();

	/** Find the FPropertyNetSerializerInfo for the provided property */
	// 查找 FProperty 对应的 Info：
	//   1) 在排序后的数组里 IndexOfByPredicate(Property->IsA(Element.Class))；
	//   2) 命中后线性扫描相邻同 FieldClass 段，调用 IsSupported() 找首个真正接受的 Info；
	//   3) 全部不命中时按 Struct → DefaultStruct、Array → DefaultArray、其他 → LastResort 回退。
	IRISCORE_API static const FPropertyNetSerializerInfo* FindSerializerInfo(const FProperty* Property);

	/** Find StructSerializerInfo by name for non property based serialization */
	// 按 USTRUCT 名字（FName）查 NamedStruct 系列 Info；用于不持有 FProperty 的场景（例如 TPolymorphicStructNetSerializer
	// 在运行时根据 RegisteredTypes 动态构造 sub-Descriptor）。
	IRISCORE_API static const FPropertyNetSerializerInfo* FindStructSerializerInfo(const FName Name);

	/**
	 * Get NopNetSerializerInfo. For when you want to use the NopNetSerializer. An example
	 * could be that you want to give a system access to some meta data that should not be
	 * replicated.
	 * 
	 * @see FNopNetSerializerConfig
	 */
	// 获取 Nop Info（绑定 FNopNetSerializer）。用于"成员存在于 Descriptor 但不实际复制"的占位场景。
	IRISCORE_API static const FPropertyNetSerializerInfo* GetNopNetSerializerInfo();

private:
	// 注册表条目：FieldClass 指针（用于 IsA 匹配 + 排序键）+ Info 实例指针（Meyers' Singleton，跨调用稳定）。
	typedef TTuple<const FFieldClass*, const FPropertyNetSerializerInfo*> FRegistryEntry;
	typedef TArray<FRegistryEntry> FNetSerializerInfoRegistry;

	// "需要重新排序"标记。Register/Unregister 置 true，Freeze 置 false。
	static bool bRegistryIsDirty;
	// 全局唯一注册表（进程级、跨 ReplicationSystem）。
	static FNetSerializerInfoRegistry Registry; 
};

/** 
 * Some helpers to register default infos for properties
*/

// Helper to implement simple default PropertyNetSerializerInfo for primitive types
// TSimplePropertyNetSerializerInfo —— "一对一直绑"模板：
//   - T 为 FProperty 子类（如 FIntProperty）
//   - ConfigType 为对应 FNetSerializerConfig（默认基类，意味着零字段）
// 适用于"无需读取 Property 元数据"的简单类型。
// 若 NetSerializer 需要 Config 的字段（比如位偏、PropertyClass），改用自定义子类（见 .cpp 中的 FObjectPropertyNetSerializerInfo 等）。
template <typename T, typename ConfigType = FNetSerializerConfig>
struct TSimplePropertyNetSerializerInfo : public FPropertyNetSerializerInfo
{
	const FNetSerializer& Serializer; // 静态绑定到具体 Serializer 全局实例

	TSimplePropertyNetSerializerInfo(const FNetSerializer& InSerializer) : Serializer(InSerializer) {}
	virtual const FFieldClass* GetPropertyTypeClass() const override { return T::StaticClass(); }
	virtual const FNetSerializer* GetNetSerializer(const FProperty* Property) const override { return &Serializer; }
	// 在 Builder 提供的 Config buffer 上 placement-new 出一个默认构造的 ConfigType。
	virtual FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const override { return new (NetSerializerConfigBuffer) ConfigType; }
};

// Helper to implement simple default PropertyNetSerializerInfo for structs
// FNamedStructPropertyNetSerializerInfo —— "按 USTRUCT 名字"绑定的 Info（FStructProperty 专用）。
// 在 IsSupported 中检查 StructProperty->Struct->GetFName() == StructName，以区分不同 USTRUCT。
// 同时支持设置自定义 Fragment 创建函数（FastArray 等少数场景）。
struct FNamedStructPropertyNetSerializerInfo : public TSimplePropertyNetSerializerInfo<FStructProperty>
{
	typedef TSimplePropertyNetSerializerInfo<FStructProperty> Super;

	FNamedStructPropertyNetSerializerInfo(const FName InPropertyFName, const FNetSerializer& InSerializer)
	: Super(InSerializer)
	{
		StructName = InPropertyFName; // 记下要匹配的 USTRUCT 名字
	}

	// 覆盖 IsSupported：除了 FieldClass 是 FStructProperty，还要求 struct 名字精确匹配。
	virtual bool IsSupported(const FProperty* Property) const override
	{	
		const FStructProperty* StructProp = CastFieldChecked<const FStructProperty>(Property);
		return IsSupportedStruct(StructProp->Struct->GetFName());
	};

	// 仅当外部通过 SetCreateAndRegisterReplicationFragmentFunction 设置后才返回非空。
	virtual CreateAndRegisterReplicationFragmentFunc GetCreateAndRegisterReplicationFragmentFunction() const
	{
		return CreateAndRegisterReplicationFragmentFunction;
	}

	// 用于 UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_WITH_CUSTOM_FRAGMENT_INFO 宏注入 Fragment 工厂。
	void SetCreateAndRegisterReplicationFragmentFunction(CreateAndRegisterReplicationFragmentFunc InCreateAndRegisterReplicationFragmentFunction)
	{
		CreateAndRegisterReplicationFragmentFunction = InCreateAndRegisterReplicationFragmentFunction;
	}

private:
	CreateAndRegisterReplicationFragmentFunc CreateAndRegisterReplicationFragmentFunction = nullptr;
};

// FLastResortPropertyNetSerializerInfo —— "兜底"Info：当没有任何具名映射命中时使用。
// 它把整个属性走 FLastResortPropertyNetSerializer——即字节流 NetSerialize（兼容旧 FProperty::NetSerialize），
// 通过 PackageMap 桥接（UIrisObjectReferencePackageMap）让导出的 ObjectRef 仍能进入 Iris 统一引用系统。
//
// 缺点：不能 delta、不能 changemask 细分、占位预算固定 MaxQuantizedSizeBits。
// 仅在没有更好选项时使用（典型例子：FText、第三方 USTRUCT 老 NetSerialize）。
struct FLastResortPropertyNetSerializerInfo : public FPropertyNetSerializerInfo
{
	IRISCORE_API FLastResortPropertyNetSerializerInfo();
	// 允许覆盖最大量化大小（位）。预算超过该值时序列化将报错。
	IRISCORE_API explicit FLastResortPropertyNetSerializerInfo(const uint32 InMaxQuantizedSizeBits);

	IRISCORE_API virtual const FFieldClass* GetPropertyTypeClass() const override;
	IRISCORE_API virtual bool IsSupported(const FProperty* Property) const override;
	IRISCORE_API virtual const FNetSerializer* GetNetSerializer(const FProperty* Property) const override final;
	IRISCORE_API virtual bool CanUseDefaultConfig(const FProperty* Property) const override final;
	IRISCORE_API virtual FNetSerializerConfig* BuildNetSerializerConfig(void* NetSerializerConfigBuffer, const FProperty* Property) const override final;

protected:
	// Maximum size of quantized state in bits
	// 该 Property 量化后允许的最大位数；超出则视为错误。
	uint32 MaxQuantizedSizeBits;

	// If set this class should not be included in the default state hash.
	// 是否把此属性排除出默认状态 Hash 计算（FText 这类"内容随本地化变"的属性必须排除，否则 Identifier 不稳定）。
	bool bExcludeFromDefaultStateHash = false;
};

// FNamedStructLastResortPropertyNetSerializerInfo —— LastResort 的"具名 struct"特化：
// 把指定 USTRUCT 强制走 LastResort（旧 NetSerialize 兼容路径），用宏
// UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_INFO 一行就能注册一个。
struct FNamedStructLastResortPropertyNetSerializerInfo final : public FLastResortPropertyNetSerializerInfo
{
	FNamedStructLastResortPropertyNetSerializerInfo(const FName InPropertyFName)
	: FLastResortPropertyNetSerializerInfo()
	{
		StructName = InPropertyFName;
	}

	// 带 MaxBits 上限的版本：用于显式估算上限的 USTRUCT。
	FNamedStructLastResortPropertyNetSerializerInfo(const FName InPropertyFName, const uint32 InMaxBits)
	: FLastResortPropertyNetSerializerInfo(InMaxBits)
	{
		StructName = InPropertyFName;
	}

	IRISCORE_API virtual const FFieldClass* GetPropertyTypeClass() const override;
	IRISCORE_API virtual bool IsSupported(const FProperty* Property) const override;
};

// Issue fatal error if matching trait found in UsedReplicationStateTraits is not set for the Serializer.
// 校验"被转发到的 NetSerializer"必须声明了与 Replication State 一致的 traits（HasDynamicState /
// HasObjectReference / HasConnectionSpecificSerialization）。漏标会导致 Quantize/Apply 时
// Iris 不会调用 FreeDynamicState/CollectReferences 等正确分支，是一类致命缺陷，因此用 LowLevelFatalError 中止。
void IRISCORE_API ValidateForwardingNetSerializerTraits(const FNetSerializer* Serializer, EReplicationStateTraits UsedReplicationStateTraits);

}

// Produce a compiler error if the name does not correspond to a declared UE type.
// 编译期校验：参数 Name 必须能在当前翻译单元中找到 F##Name 或 U##Name 的完整声明。
// 用途：在按名字注册 NetSerializer 时（UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_AND_REGISTRY_DELEGATES 等），
//      防止打错或忘记 #include 类型头导致运行时悄悄走兜底。
// 展开后：依赖前面 IsDeclaredType 的 SFINAE，若两个尝试都失败则 static_assert 触发"无法找到 UE 类型"错误。
#define UE_NET_IS_DECLARED_TYPE(Name) \
	static_assert( \
	UE::Net::IsDeclaredType((struct F##Name*)nullptr) || \
	UE::Net::IsDeclaredType((struct U##Name*)nullptr) \
	, "The UE type name '" #Name "' cannot be found. Make sure you have removed the 'F' or 'U' prefix from the type name.");

// Only needed if we want to export PropertyNetSerializerInfo, this goes in the header if we need to export it
// 在头文件中前向声明一个名为 GetPropertyNetSerializerInfo_##NetSerializerInfo 的访问器，用于跨模块共享 Info 单例。
// 多数 Info 是 file-static 的，无需此宏；只有需要被其他翻译单元引用（典型例子：DefaultPropertyNetSerializerInfos.cpp
// 中给 Enum*PropertyNetSerializerInfo 用 UE_NET_DECLARE_NETSERIALIZER_INFO 跨文件 register）才需要 declare。
#define UE_NET_DECLARE_NETSERIALIZER_INFO(NetSerializerInfo) \
const UE::Net::FPropertyNetSerializerInfo& GetPropertyNetSerializerInfo_##NetSerializerInfo();

// Implement FPropertyNetSerializerInfo from struct, this goes in the cpp file
// 在 cpp 中实现上面声明的访问器：Meyers' 单例模式（function-local static），首次调用时构造，线程安全（C++11 起）。
// 展开示例：UE_NET_IMPLEMENT_NETSERIALIZER_INFO(FFooInfo) →
//          const FPropertyNetSerializerInfo& GetPropertyNetSerializerInfo_FFooInfo() {
//              static FFooInfo StaticInstance; return StaticInstance; }
#define UE_NET_IMPLEMENT_NETSERIALIZER_INFO(NetSerializerInfo) \
const UE::Net::FPropertyNetSerializerInfo& GetPropertyNetSerializerInfo_##NetSerializerInfo() { static NetSerializerInfo StaticInstance; return StaticInstance; };

// Implement simple FPropertyNetSerializerInfo binding a SerializerType to the property
// "一行式" 直接绑定：把 PropertyType（如 FIntProperty）绑到 SerializerName（如 FPackedInt32NetSerializer）。
// 展开后等价于 IMPLEMENT_NETSERIALIZER_INFO(TSimplePropertyNetSerializerInfo<PropertyType>(GSerializer))；
// 仅当不需要自定义 Config 时使用（默认 FNetSerializerConfig 即可）。
#define UE_NET_IMPLEMENT_SIMPLE_NETSERIALIZER_INFO(PropertyType, SerializerName) \
const UE::Net::FPropertyNetSerializerInfo& GetPropertyNetSerializerInfo_##PropertyType() { static UE::Net::TSimplePropertyNetSerializerInfo<PropertyType> StaticInstance(UE_NET_GET_SERIALIZER(SerializerName)); return StaticInstance; };

// Implement simple FPropertyNetSerializerInfo for struct types with custom serializers
// 把名为 Name（FName 实例）的 USTRUCT 绑定到 SerializerName。
// 该宏要求 Name 是一个 const FName 变量（不是字面字符串），所以通常配合 PropertyNetSerializerRegistry_NAME_X 静态 FName 使用。
// 例：UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(NAME_Vector, FVectorNetSerializer);
#define UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(Name, SerializerName) \
const UE::Net::FPropertyNetSerializerInfo& GetPropertyNetSerializerInfo_##Name() { static UE::Net::FNamedStructPropertyNetSerializerInfo StaticInstance(Name, UE_NET_GET_SERIALIZER(SerializerName)); return StaticInstance; };

/**
 * Force a struct to use a custom serializer and custom fragment. Use of custom fragments is highly discouraged as serialization order is modified, ending up after normal class properties.
 * It probably requires additional memory allocations to create the fragment as well. Only use a last resort when all other options have been exhausted.
 * It should never be required to use custom fragments except for very rare backward compatibility purposes. Ask the experts first to try to find a better solution.
 */
// 与上面相同，但额外注入一个 Fragment 工厂函数到 Info：Builder 在为该 struct 烘 Descriptor 时
// 会写入 CreateAndRegisterReplicationFragmentFunction，使最终 Fragment 由该函数创建。
//
// !!! 警告 !!!
//   - 自定义 Fragment 会破坏属性的"按 RepIndex 顺序"序列化布局——这些 Fragment 总会被排到普通 Property 之后；
//   - 通常需要额外堆分配（每个对象一份 Fragment 对象）；
//   - 仅在 FastArray 这类有强烈结构性需求的少数 USTRUCT 中使用。
//   - 除非有兼容性需求并已咨询架构师，否则**不要使用**这个宏。
#define UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_WITH_CUSTOM_FRAGMENT_INFO(Name, SerializerName, CreateAndRegisterReplicationFragmentFunction) \
const UE::Net::FPropertyNetSerializerInfo& GetPropertyNetSerializerInfo_##Name() \
{ \
	static UE::Net::FNamedStructPropertyNetSerializerInfo StaticInstance(Name, UE_NET_GET_SERIALIZER(SerializerName)); \
	StaticInstance.SetCreateAndRegisterReplicationFragmentFunction(CreateAndRegisterReplicationFragmentFunction); \
	return StaticInstance; \
};

// Implement FPropertyNetSerializerInfo for cases where LastResortPropertyNetSerializer is needed for struct with custom serialization.
// 把名为 StructName 的 USTRUCT 强制走 FLastResortPropertyNetSerializer（旧 NetSerialize 字节流兼容路径）。
// 展开产物：file-static 的 FNamedStructLastResortPropertyNetSerializerInfo 单例 + 对应访问器。
#define UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_INFO(StructName) \
const UE::Net::FPropertyNetSerializerInfo& GetPropertyNetSerializerInfo_##StructName() { static UE::Net::FNamedStructLastResortPropertyNetSerializerInfo StaticInstance(StructName); return StaticInstance; };

// Implement FPropertyNetSerializerInfo for cases where LastResortPropertyNetSerializer is needed for struct with custom serialization, with quantized state size override.
// 同上，但显式指定量化状态最大位数（默认是 FLastResortPropertyNetSerializerConfig::DefaultMaxQuantizedSizeBits）。
// 用于显式控制超大 USTRUCT 的网络预算上限。
#define UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_INFO_WITH_SIZE_OVERRIDE(StructName, MaxBits) \
const UE::Net::FPropertyNetSerializerInfo& GetPropertyNetSerializerInfo_##StructName() { static UE::Net::FNamedStructLastResortPropertyNetSerializerInfo StaticInstance(StructName, MaxBits); return StaticInstance; };

// Register 
// 在 Registry 中登记一个 Info（参数是宏 IMPLEMENT 系列产生的访问器名后缀）。
// 通常被放进一个由 UE_NET_IMPLEMENT_NETSERIALIZER_REGISTRY_DELEGATES 自动派生的 OnPreFreezeNetSerializerRegistry 回调中。
#define UE_NET_REGISTER_NETSERIALIZER_INFO(Name) \
UE::Net::FPropertyNetSerializerInfoRegistry::Register(&GetPropertyNetSerializerInfo_##Name());

// Unregister
// 反注册。仅在非 monolithic（DLL）构建中有用——动态卸载模块时把 Info 从 Registry 拿掉。
// monolithic 构建（一次性链接进可执行）中模块永不卸载，因此此宏被定义为 no-op。
#if !IS_MONOLITHIC
#define UE_NET_UNREGISTER_NETSERIALIZER_INFO(Name) \
UE::Net::FPropertyNetSerializerInfoRegistry::Unregister(&GetPropertyNetSerializerInfo_##Name());
#else
#define UE_NET_UNREGISTER_NETSERIALIZER_INFO(...) 
#endif

// Implement minimal required delegates for a NetSerializer
// 自动派生一个继承自 FNetSerializerRegistryDelegates 的 file-static 实例：
//   - 析构函数 → UE_NET_UNREGISTER_NETSERIALIZER_INFO（模块卸载时反注册）；
//   - OnPreFreezeNetSerializerRegistry() → UE_NET_REGISTER_NETSERIALIZER_INFO（FIrisCoreModule::StartupModule
//                                          调用 RegisterPropertyNetSerializerSelectorTypes 时广播 PreFreeze）。
//
// 展开示例：UE_NET_IMPLEMENT_NETSERIALIZER_REGISTRY_DELEGATES(MyType) →
//   struct FMyTypeNetSerializerRegistryDelegates : protected FNetSerializerRegistryDelegates {
//       ~FMyTypeNetSerializerRegistryDelegates() { Unregister(&GetPropertyNetSerializerInfo_PropertyNetSerializerRegistry_NAME_MyType()); }
//       virtual void OnPreFreezeNetSerializerRegistry() override { Register(&GetPropertyNetSerializerInfo_PropertyNetSerializerRegistry_NAME_MyType()); }
//   };
//   static FMyTypeNetSerializerRegistryDelegates MyTypeNetSerializerRegistryDelegates;  // 进入 file-static 注册链
//
// 注意：这里使用 protected 继承，使外部无法直接当 FNetSerializerRegistryDelegates 接口操作；
// 该 file-static 实例会在 RegisterPropertyNetSerializerSelectorTypes 内部由 BroadcastPreFreezeNetSerializerRegistry
// 通过遍历静态链来调用 OnPreFreeze。
#define UE_NET_IMPLEMENT_NETSERIALIZER_REGISTRY_DELEGATES(Name) \
struct F##Name##NetSerializerRegistryDelegates : protected UE::Net::FNetSerializerRegistryDelegates \
{ \
	~F##Name##NetSerializerRegistryDelegates() { UE_NET_UNREGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_##Name); } \
	virtual void OnPreFreezeNetSerializerRegistry() override { UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_##Name); } \
}; \
static F##Name##NetSerializerRegistryDelegates Name##NetSerializerRegistryDelegates;

// Utility that can be used to forward serialization of a Struct to a specific NetSerializer
// "三件套合一"：声明 FName + 实现 NamedStruct Info + 自动 Register/Unregister Delegates。
// 这是 USTRUCT → 自定义 NetSerializer 注册的"标准入口"，通常在该 Serializer 自己的 cpp 文件末尾调用。
//
// 展开后产生：
//   1) static const FName PropertyNetSerializerRegistry_NAME_<Name>("Name");
//   2) GetPropertyNetSerializerInfo_PropertyNetSerializerRegistry_NAME_<Name>() 单例访问器；
//   3) F<Name>NetSerializerRegistryDelegates file-static 实例（PreFreeze 时 Register）。
#define UE_NET_IMPLEMENT_FORWARDING_NETSERIALIZER_AND_REGISTRY_DELEGATES(Name, SerializerName) \
	static const FName PropertyNetSerializerRegistry_NAME_##Name( PREPROCESSOR_TO_STRING(Name) ); \
	UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_##Name, SerializerName); \
	UE_NET_IMPLEMENT_NETSERIALIZER_REGISTRY_DELEGATES(Name)


// Utility that can be used to forward serialization of a Struct to a last resort net serializer
// 与上面同理，但目标 Serializer 固定为 LastResortPropertyNetSerializer（兼容旧 NetSerialize）。
// 额外用 UE_NET_IS_DECLARED_TYPE 在编译期校验类型存在，防止把类型名打错。
#define UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_AND_REGISTRY_DELEGATES(Name) \
	UE_NET_IS_DECLARED_TYPE(Name); \
	static const FName PropertyNetSerializerRegistry_NAME_##Name( PREPROCESSOR_TO_STRING(Name) ); \
	UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_##Name); \
	UE_NET_IMPLEMENT_NETSERIALIZER_REGISTRY_DELEGATES(Name)

// Utility that can be used to forward serialization of a Struct to a last resort net serializer with a maximum quantized size
// 同上，附加可指定的最大量化位数（用于显式声明上限以避免溢出）。
#define UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_AND_REGISTRY_DELEGATES_WITH_SIZE_OVERRIDE(Name, MaxBits) \
	UE_NET_IS_DECLARED_TYPE(Name); \
	static const FName PropertyNetSerializerRegistry_NAME_##Name( PREPROCESSOR_TO_STRING(Name) ); \
	UE_NET_IMPLEMENT_NAMED_STRUCT_LASTRESORT_NETSERIALIZER_INFO_WITH_SIZE_OVERRIDE(PropertyNetSerializerRegistry_NAME_##Name, MaxBits); \
	UE_NET_IMPLEMENT_NETSERIALIZER_REGISTRY_DELEGATES(Name)
