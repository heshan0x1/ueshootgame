// Copyright Epic Games, Inc. All Rights Reserved.
// =============================================================================
// 中文说明（本文件是 Iris 序列化层的"心脏"之一，请仔细阅读）：
//
//   NetSerializerBuilder.inl 用 SFINAE 元编程实现"按需检测 Impl 成员、
//   按需填充 FNetSerializer 函数指针"的编译期工厂。
//
//   核心思想：用户编写一个 NetSerializerImpl struct，可以"只实现需要的成员"，
//   缺失的成员由 TNetSerializerBuilder 提供合理默认实现：
//
//     必填：SourceType、ConfigType、Serialize、Deserialize
//     可选：QuantizedType（缺失则等同于 SourceType）
//          Quantize/Dequantize（缺失时仅当 SourceType 是 POD，使用按值复制）
//          IsEqual（缺失时使用 SourceType 的 operator==）
//          Validate（缺失时返回 true）
//          SerializeDelta/DeserializeDelta（缺失时按"先写一个 IsEqual bit、
//            等则写 1+空、不等则写 1bit + 完整 Serialize"的方式生成）
//          CloneDynamicState/FreeDynamicState（仅 bHasDynamicState 才需要）
//          CollectNetReferences（仅 bHasCustomNetReference 才需要）
//          Apply（可选，独立 trait）
//
//   trait（static constexpr bool bXxx）：
//     bIsForwardingSerializer / bHasConnectionSpecificSerialization /
//     bHasCustomNetReference / bHasDynamicState / bUseDefaultDelta /
//     bUseSerializerIsEqual
//   缺失则视为 false（除 bUseDefaultDelta 默认 true）。
//
//   运作机制（SFINAE 三件套）：
//     1) FSignatureCheck<U, U>：用于"按精确签名探测成员函数"。例如
//          template<typename U> static ETrueType TestHasSerialize(
//              FSignatureCheck<NetSerializeFunction, &U::Serialize>*);
//        如果 U::Serialize 存在且类型完全匹配 NetSerializeFunction，
//        重载解析选 ETrueType 版本；否则回落到 (...) 的 EFalseType。
//     2) FTypeCheck<T>：用于"按嵌套类型/decltype 表达式合法性探测"。例如
//          template<typename U> static ETrueType TestHasSourceType(
//              FTypeCheck<typename U::SourceType>*);
//        若 U 内没有 SourceType 嵌套类型，模板替换失败 → SFINAE 走 (...)。
//     3) TEnableIf<...> 在 GetXxx 函数上做"按条件二选一重载"——
//        每对 GetXxx 都有两个同名实现，且仅有一个能通过 TEnableIf；
//        编译期非歧义地解析出唯一定义。
//
//   读法建议：
//     - 先看下面 4 个 NetXxxDefault<...> 默认实现，理解"缺成员时回落到什么"。
//     - 再看 TNetSerializerBuilder<NetSerializerImpl> 的 private 区：
//         · ETrueType / EFalseType 哨兵；
//         · FVersion / FTraits 参考默认值结构；
//         · FSignatureCheck / FTypeCheck SFINAE 工具；
//         · 一系列 TestHasXxx 探测器；
//         · ETraits 把所有探测结果汇成 unsigned 位标志，方便 TEnableIf。
//     - 然后看 public 区的 GetXxxFunction()/GetXxxX()：
//         每对函数对应"用户实现了 vs 没实现"两种重载。
//     - 最后看 GetTraits()（汇总到 ENetSerializerTraits）和 Validate()
//         （编译期 static_assert 严格性检查）。
// =============================================================================

#pragma once

#include "Templates/EnableIf.h"
#include "Templates/IsPODType.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"

namespace UE::Net
{

// -----------------------------------------------------------------------------
// 默认实现 #1：NetSerializeDeltaDefault（一参数版本——只有 Serialize 可用）。
// 当 Impl 没实现 SerializeDelta 且 bUseDefaultDelta=false 时使用：
//   不写 IsEqual 标志位，直接调用全量 Serialize 输出 Args.Source（忽略 Args.Prev）。
// 等价于"无 delta"——线协议上没有任何节省，但可以保证多态子类无 IsEqual 时
// 仍能编译通过。
// -----------------------------------------------------------------------------
template<NetSerializeFunction Serialize>
void
NetSerializeDeltaDefault(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	Serialize(Context, Args);
};

// -----------------------------------------------------------------------------
// 默认实现 #2：NetDeserializeDeltaDefault（一参数版本——配对上面的一参数 Delta）。
// 不读 IsEqual 标志位，直接全量 Deserialize 到 Args.Target，忽略 Args.Prev。
// -----------------------------------------------------------------------------
template<NetDeserializeFunction Deserialize>
void
NetDeserializeDeltaDefault(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	Deserialize(Context, Args);
}

// -----------------------------------------------------------------------------
// 默认实现 #3：NetSerializeDeltaDefault（双参数 Serialize+IsEqual 版本）。
// Iris delta 编码的"标准"实现：
//   1) 用 IsEqual 比较 Source vs Prev；
//   2) 把 1bit 标志 isEqual 写入 BitStream；
//   3) 若相等：直接返回（节省整个 payload）；
//   4) 否则：按全量 Serialize 写出 Source。
// 接收端的对应实现是 NetDeserializeDeltaDefault 的双/三参数版本。
// 使用此默认实现要求 IsEqual 工作在"已 Quantize 状态"上（bStateIsQuantized=true），
// 因为 SerializeDelta 的输入已经是 Quantized。
// -----------------------------------------------------------------------------
template<NetSerializeFunction Serialize, NetIsEqualFunction IsEqual>
void
NetSerializeDeltaDefault(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	// 把 Args 适配为 FNetIsEqualArgs 形式调用 IsEqual。
	FNetIsEqualArgs EqualArgs;
	EqualArgs.Version = 0;
	EqualArgs.NetSerializerConfig = Args.NetSerializerConfig;
	EqualArgs.Source0 = Args.Source;
	EqualArgs.Source1 = Args.Prev;
	// 关键：SerializeDelta 阶段操作的总是 Quantized 数据，告诉 IsEqual 走 quantized 路径
	// （对于按 memcmp 的默认实现这不影响；对自定义 IsEqual 则可能切换分支）。
	EqualArgs.bStateIsQuantized = true;

	// WriteBool 同时写 1bit 并把布尔值返回——若相等则直接 return，写出的 payload 仅 1 bit。
	if (Context.GetBitStreamWriter()->WriteBool(IsEqual(Context, EqualArgs)))
	{
		return;
	}

	// 不相等：紧随 1bit 标志后写出全量 Serialize（接收端一致解析）。
	Serialize(Context, Args);
};

// -----------------------------------------------------------------------------
// 默认实现 #4：NetDeserializeDeltaDefault（多参数版本——配对上面的双参 Serialize+IsEqual）。
// 模板参数：
//   QuantizedTypeSize   —— Quantized 状态字节大小（用于 IsEqual=true 分支的 memcpy）
//   Deserialize         —— 不等时用于解码
//   FreeDynamicState    —— 可为 nullptr；非空表示 Quantized 内含动态状态，
//                          memcpy 前需要先 Free 掉 Target 的旧动态分配
//   CloneDynamicState   —— 可为 nullptr；非空表示 memcpy 后需要 Clone Prev 的动态状态
// 流程：
//   1) ReadBool 读 1bit isEqual 标志；
//   2) 若 true：从 Prev 拷贝一份完整 Quantized → Target，配合 Free/Clone 维持
//      动态状态的所有权（详见 FNetSerializerArrayStorage 文档）；
//   3) 若 false：调用全量 Deserialize 到 Target。
// -----------------------------------------------------------------------------
template<uint32 QuantizedTypeSize, NetDeserializeFunction Deserialize, NetFreeDynamicStateFunction FreeDynamicState, NetCloneDynamicStateFunction CloneDynamicState>
void
NetDeserializeDeltaDefault(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	if (Context.GetBitStreamReader()->ReadBool())
	{
		// Clone from prev. Need to free target first.
		// 中文：相等分支——把 Prev 完整复制到 Target。
		// Step 1：若 Quantized 含动态状态，先释放 Target 旧动态分配，避免泄漏。
		if (FreeDynamicState != NetFreeDynamicStateFunction(nullptr))
		{
			FNetFreeDynamicStateArgs FreeArgs;
			FreeArgs.Version = 0;
			FreeArgs.NetSerializerConfig = Args.NetSerializerConfig;
			FreeArgs.Source = Args.Target;

			FreeDynamicState(Context, FreeArgs);
		}

		// Step 2：整块 Memcpy。这一步会让 Target 的"动态指针字段"指向 Prev 的内存
		// （悬挂副本），必须紧接 Step 3 的 Clone 来纠正。
		FMemory::Memcpy(reinterpret_cast<uint8*>(Args.Target), reinterpret_cast<uint8*>(Args.Prev), QuantizedTypeSize);

		// Step 3：若有动态状态，克隆出独立副本（Source=Prev、Target=Target）。
		// FNetSerializerArrayStorage::Clone 等会先 AllocatorInstance.Initialize() 把
		// 悬挂指针归零再分配自己的副本——参见 NetSerializerArrayStorage.h。
		if (CloneDynamicState != NetCloneDynamicStateFunction(nullptr))
		{
			FNetCloneDynamicStateArgs CloneArgs;
			CloneArgs.Version = 0;
			CloneArgs.NetSerializerConfig = Args.NetSerializerConfig;
			CloneArgs.Source = Args.Prev;
			CloneArgs.Target = Args.Target;

			CloneDynamicState(Context, CloneArgs);
		}

		return;
	}

	// 不等分支：直接全量解码——Deserialize 内部要负责 Target 旧状态的释放与新分配。
	Deserialize(Context, Args);
}

// -----------------------------------------------------------------------------
// 默认实现 #5：NetQuantizeDefault —— 按值赋值（仅适用于 SourceType 与 QuantizedType
// 相同或可隐式赋值的场景）。Source POD 时 SFINAE 选这个；非 POD 类型必须自己实现 Quantize。
// -----------------------------------------------------------------------------
template<typename T>
void
NetQuantizeDefault(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	*reinterpret_cast<T*>(Args.Target) = *reinterpret_cast<const T*>(Args.Source);
}

// -----------------------------------------------------------------------------
// 默认实现 #6：NetDequantizeDefault —— 与 NetQuantizeDefault 对称的反向赋值。
// -----------------------------------------------------------------------------
template<typename T>
void
NetDequantizeDefault(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	*reinterpret_cast<T*>(Args.Target) = *reinterpret_cast<const T*>(Args.Source);
}

// -----------------------------------------------------------------------------
// 默认实现 #7：NetIsEqualDefault —— 通过 SourceType::operator== 比较。
// 注意：FNetIsEqualArgs 的两个 Source 在调用时可能是 SourceType 也可能是
// QuantizedType（取决于 bStateIsQuantized）；默认实现按 T 解读。
// 因此 Impl 一旦定义了独立的 QuantizedType 就必须自己实现 IsEqual。
// -----------------------------------------------------------------------------
template<typename T>
bool
NetIsEqualDefault(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	return *reinterpret_cast<const T*>(Args.Source0) == *reinterpret_cast<const T*>(Args.Source1);
}

// -----------------------------------------------------------------------------
// 默认实现 #8：NetValidateDefault —— 不做任何校验，恒返回 true。
// Validate 在反序列化端被调用，用于检测对端发来的数据是否合法（如范围、ID 是否在白名单）。
// 默认 true 意味着 Iris 信任数据；安全敏感的 Serializer 应自行实现。
// -----------------------------------------------------------------------------
template<typename T = void>
bool
NetValidateDefault(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	return true;
}

/**
 * TNetSerializerBuilder —— Iris NetSerializer 的"编译期工厂"。
 *
 * 通过 SFINAE 探测用户提供的 NetSerializerImpl 包含哪些成员（成员函数、嵌套类型、
 * trait 常量），并据此：
 *   1) 为每个 FNetSerializer 函数指针字段返回"用户实现 or 库默认实现"；
 *   2) 计算 ENetSerializerTraits 位标志；
 *   3) 通过 Validate() 在编译期 static_assert 一致性约束（必填/互斥/搭配）。
 *
 * 通常不直接使用——请通过 UE_NET_DECLARE_SERIALIZER + UE_NET_IMPLEMENT_SERIALIZER
 * 宏（公共）或 UE_NET_DECLARE_SERIALIZER_INTERNAL + UE_NET_IMPLEMENT_SERIALIZER_INTERNAL
 * 宏（内部）来声明/实现。
 */
template<typename NetSerializerImpl>
class TNetSerializerBuilder
{
private:
	// -------------------------------------------------------------------------
	// SFINAE 哨兵类型：通过 decltype(...)::Value 得到 1 或 0，便于 enum 位标志聚合。
	// 用 enum class 包装而非直接 std::true_type/false_type，是为了让 ::Value
	// 直接是 unsigned 整数，可与下方 ETraits enum 中的位标志无缝混用。
	// -------------------------------------------------------------------------
	enum class ETrueType : unsigned
	{
		Value = 1
	};

	enum class EFalseType : unsigned
	{
		Value = 0
	};

	// -------------------------------------------------------------------------
	// FVersion / FTraits：参考默认值结构。
	// 它们仅用于 SFINAE 的"成员指针类型比对"——用 std::is_same_v 比较
	// "Impl::bXxx 的 decltype" 与 "FTraits::bXxx 的 decltype" 是否一致，可
	// 一并探测：
	//   (a) 成员存在 + (b) 成员是 static constexpr <expected type>。
	// 如果用户错写成 static const、非 constexpr 或函数等形式，比对失败 → 视为 IsBool=false。
	// -------------------------------------------------------------------------
	struct FVersion
	{
		static constexpr uint32 Version = 0;
	};

	struct FTraits
	{
		static constexpr bool bIsForwardingSerializer = false;
		static constexpr bool bHasConnectionSpecificSerialization = false;
		static constexpr bool bHasCustomNetReference = false;
		static constexpr bool bHasDynamicState = false;
		// 注意此处默认值与其他不同——bUseDefaultDelta 缺省视为 true（即"启用 Iris
		// 默认的 IsEqual+全量 Serialize delta 实现"）；用户仅在希望禁用时显式声明 false。
		static constexpr bool bUseDefaultDelta = true;
		static constexpr bool bUseSerializerIsEqual = false;
	};

	// -------------------------------------------------------------------------
	// SFINAE 工具模板。
	//   FSignatureCheck<U, U>：用于"成员函数指针 + 精确签名"探测。
	//     例：FSignatureCheck<NetSerializeFunction, &U::Serialize>
	//          ≡ U::Serialize 必须是签名完全等于 NetSerializeFunction 的成员/静态函数。
	//   FTypeCheck<T>：用于"嵌套类型/decltype 表达式合法性"探测。
	//     例：FTypeCheck<typename U::SourceType> 仅当 U::SourceType 存在时才能实例化。
	// 这两个模板只声明不定义，因为只用于 sizeof/decltype 而不会被实际调用。
	// -------------------------------------------------------------------------
	template<typename U, U> struct FSignatureCheck;
	template<typename> struct FTypeCheck;

	// =========================================================================
	// 探测器（重载对：第一条 ETrueType 走 SFINAE 主路径；第二条 EFalseType 是
	// 任意可变参数兜底）。
	// 调用方式：decltype(TestHasXxx<NetSerializerImpl>(nullptr))::Value → 0/1
	// =========================================================================

	// Version check
	// 探测：U::Version 是否存在并且类型与 FVersion::Version 完全一致（即
	// `static constexpr uint32 Version`）。任何形式不符（如写成 int Version）
	// 都会让 std::is_same_v 求值为 false，TEnableIf::Type 不存在 → SFINAE 退到 (...)。
	template<typename U> static ETrueType TestHasVersion(typename TEnableIf<std::is_same_v<decltype(&FVersion::Version), decltype(&U::Version)>>::Type*);
	template<typename> static EFalseType TestHasVersion(...);

	// Traits
	// -------------------------------------------------------------------------
	// 每个"bXxx"trait 都拆成两个探测器：
	//   IsPresent —— 成员存在（不限类型，便于在 Validate 给出"声明形式错误"的提示）；
	//   IsBool    —— 成员存在且类型严格匹配 FTraits 中的 static constexpr bool。
	// Validate() 中 (!IsPresent || IsBool) 用于报警"用户写了但形式不对"。
	// -------------------------------------------------------------------------

	// bHasCustomNetReference：Quantized 内含 Iris 不感知的"自定义对象引用"，
	// 必须配对实现 CollectNetReferences。
	template<typename U> static ETrueType TestHasCustomNetReferenceIsPresent(FTypeCheck<decltype(&U::bHasCustomNetReference)>*);
	template<typename> static EFalseType TestHasCustomNetReferenceIsPresent(...);

	template<typename U> static ETrueType TestHasCustomNetReferenceIsBool(typename TEnableIf<std::is_same_v<decltype(&FTraits::bHasCustomNetReference), decltype(&U::bHasCustomNetReference)>>::Type*);
	template<typename> static EFalseType TestHasCustomNetReferenceIsBool(...);

	// bUseSerializerIsEqual：Impl 已实现 IsEqual，且希望 Iris 在所有路径上都
	// 调用该 IsEqual（默认情况某些热路径会使用按字节 memcmp）。
	template<typename U> static ETrueType TestUseSerializerIsEqualIsPresent(FTypeCheck<decltype(&U::bUseSerializerIsEqual)>*);
	template<typename> static EFalseType TestUseSerializerIsEqualIsPresent(...);

	template<typename U> static ETrueType TestUseSerializerIsEqualIsBool(typename TEnableIf<std::is_same_v<decltype(&FTraits::bUseSerializerIsEqual), decltype(&U::bUseSerializerIsEqual)>>::Type*);
	template<typename> static EFalseType TestUseSerializerIsEqualIsBool(...);

	// bIsForwardingSerializer：转发型 Serializer（如 StructNetSerializer 把
	// 调用转发给"内部成员的子 Serializer"），有更严格的实现要求
	// （见 ValidateForwardingSerializer 必填项列表）。
	template<typename U> static ETrueType TestIsForwardingSerializerIsPresent(FTypeCheck<decltype(&U::bIsForwardingSerializer)>*);
	template<typename> static EFalseType TestIsForwardingSerializerIsPresent(...);

	template<typename U> static ETrueType TestIsForwardingSerializerIsBool(typename TEnableIf<std::is_same_v<decltype(&FTraits::bIsForwardingSerializer), decltype(&U::bIsForwardingSerializer)>>::Type*);
	template<typename> static EFalseType TestIsForwardingSerializerIsBool(...);

	// bHasConnectionSpecificSerialization：序列化结果与连接相关
	// （如 NetRole 的 AutonomousProxy 降级、NetToken 按连接导出）——
	// 影响调度策略与缓存（Iris 不能跨连接复用同一份 Quantized 输出）。
	template<typename U> static ETrueType TestHasConnectionSpecificSerializationIsPresent(FTypeCheck<decltype(&U::bHasConnectionSpecificSerialization)>*);
	template<typename> static EFalseType TestHasConnectionSpecificSerializationIsPresent(...);

	template<typename U> static ETrueType TestHasConnectionSpecificSerializationIsBool(typename TEnableIf<std::is_same_v<decltype(&FTraits::bHasConnectionSpecificSerialization), decltype(&U::bHasConnectionSpecificSerialization)>>::Type*);
	template<typename> static EFalseType TestHasConnectionSpecificSerializationIsBool(...);

	// bHasDynamicState：Quantized 内含动态分配（FNetSerializerArrayStorage 等），
	// 此时必须实现 CloneDynamicState/FreeDynamicState（Validate 会强制）。
	template<typename U> static ETrueType TestHasDynamicStateIsPresent(FTypeCheck<decltype(&U::bHasDynamicState)>*);
	template<typename> static EFalseType TestHasDynamicStateIsPresent(...);

	template<typename U> static ETrueType TestHasDynamicStateIsBool(typename TEnableIf<std::is_same_v<decltype(&FTraits::bHasDynamicState), decltype(&U::bHasDynamicState)>>::Type*);
	template<typename> static EFalseType TestHasDynamicStateIsBool(...);

	// bUseDefaultDelta：缺省 true。声明为 false 则禁用"按 IsEqual+全量 Serialize"
	// 默认 Delta，回落到"无 Delta，直接转发 Serialize"（见 NetSerializeDeltaDefault 单参版）。
	// 适用于 Serialize 自身已做高效压缩、再加 IsEqual 反而更耗的场景。
	template<typename U> static ETrueType TestUseDefaultDeltaIsPresent(FTypeCheck<decltype(&U::bUseDefaultDelta)>*);
	template<typename> static EFalseType TestUseDefaultDeltaIsPresent(...);

	template<typename U> static ETrueType TestUseDefaultDeltaIsBool(typename TEnableIf<std::is_same_v<decltype(&FTraits::bUseDefaultDelta), decltype(&U::bUseDefaultDelta)>>::Type*);
	template<typename> static EFalseType TestUseDefaultDeltaIsBool(...);

	// Type checks
	// -------------------------------------------------------------------------
	// 嵌套类型探测：必填的 ConfigType / SourceType；可选的 QuantizedType。
	// 如果 typename U::Xxx 替换失败，模板被 SFINAE 排除 → 走 (...) 分支。
	// -------------------------------------------------------------------------
	template<typename U> static ETrueType TestHasConfigType(FTypeCheck<typename U::ConfigType>*);
	template<typename> static EFalseType TestHasConfigType(...);

	template<typename U> static ETrueType TestHasSourceType(FTypeCheck<typename U::SourceType>*);
	template<typename> static EFalseType TestHasSourceType(...);

	template<typename U> static ETrueType TestHasQuantizedType(FTypeCheck<typename U::QuantizedType>*);
	template<typename> static EFalseType TestHasQuantizedType(...);

	// IsXxxPod：用于在缺失 Quantize 实现时判断"是否能合法回落到默认按值赋值"。
	// 双重 SFINAE：先以 FTypeCheck<typename U::Xxx>* 确认嵌套类型存在，存在时
	// 才取 TIsPODType<...>::Value；不存在时回落到 vararg 返回 false。
	template<typename U> static constexpr bool IsSourceTypePod(FTypeCheck<typename U::SourceType>*) { return TIsPODType<typename U::SourceType>::Value; }
	template<typename> static constexpr bool IsSourceTypePod(...) { return false; }

	template<typename U> static constexpr bool IsQuantizedTypePod(FTypeCheck<typename U::QuantizedType>*) { return TIsPODType<typename U::QuantizedType>::Value; }
	template<typename> static constexpr bool IsQuantizedTypePod(...) { return false; }

	// Default config check
	// DefaultConfig 是 Impl 层可选的 static constexpr ConfigType 实例——
	// 若提供，FNetSerializer.DefaultConfig 会指向它，节省外部为每个属性单独
	// 构造 Config 的开销（典型如 FFloatNetSerializer 的 DefaultConfig）。
	template<typename U> static constexpr bool TestHasDefaultConfig(FTypeCheck<decltype(&U::DefaultConfig)>*) { return true; }
	template<typename> static constexpr bool TestHasDefaultConfig(...) { return false; }

	// Function checks
	// -------------------------------------------------------------------------
	// 用 FSignatureCheck<FuncType, &U::FuncName> 同时验证：(a) 函数存在 +
	// (b) 函数类型与 FuncType 完全一致（包括参数列表/调用约定）。
	// 任一不匹配都 SFINAE 退到 (...)。
	// -------------------------------------------------------------------------
	template<typename U> static ETrueType TestHasSerialize(FSignatureCheck<NetSerializeFunction, &U::Serialize>*);
	template<typename> static EFalseType TestHasSerialize(...);

	template<typename U> static ETrueType TestHasDeserialize(FSignatureCheck<NetDeserializeFunction, &U::Deserialize>*);
	template<typename> static EFalseType TestHasDeserialize(...);

	template<typename U> static ETrueType TestHasSerializeDelta(FSignatureCheck<NetSerializeDeltaFunction, &U::SerializeDelta>*);
	template<typename> static EFalseType TestHasSerializeDelta(...);

	template<typename U> static ETrueType TestHasDeserializeDelta(FSignatureCheck<NetDeserializeDeltaFunction, &U::DeserializeDelta>*);
	template<typename> static EFalseType TestHasDeserializeDelta(...);

	template<typename U> static ETrueType TestHasQuantize(FSignatureCheck<NetQuantizeFunction, &U::Quantize>*);
	template<typename> static EFalseType TestHasQuantize(...);

	template<typename U> static ETrueType TestHasDequantize(FSignatureCheck<NetDequantizeFunction, &U::Dequantize>*);
	template<typename> static EFalseType TestHasDequantize(...);

	template<typename U> static ETrueType TestHasIsEqual(FSignatureCheck<NetIsEqualFunction, &U::IsEqual>*);
	template<typename> static EFalseType TestHasIsEqual(...);

	template<typename U> static ETrueType TestHasValidate(FSignatureCheck<NetValidateFunction, &U::Validate>*);
	template<typename> static EFalseType TestHasValidate(...);

	template<typename U> static ETrueType TestHasFreeDynamicState(FSignatureCheck<NetFreeDynamicStateFunction, &U::FreeDynamicState>*);
	template<typename> static EFalseType TestHasFreeDynamicState(...);

	template<typename U> static ETrueType TestHasCloneDynamicState(FSignatureCheck<NetCloneDynamicStateFunction, &U::CloneDynamicState>*);
	template<typename> static EFalseType TestHasCloneDynamicState(...);

	template<typename U> static ETrueType TestHasCollectNetReferences(FSignatureCheck<NetCollectNetReferencesFunction, &U::CollectNetReferences>*);
	template<typename> static EFalseType TestHasCollectNetReferences(...);

	template<typename U> static ETrueType TestHasApply(FSignatureCheck<NetApplyFunction, &U::Apply>*);
	template<typename> static EFalseType TestHasApply(...);

	// -------------------------------------------------------------------------
	// 把所有 SFINAE 探测结果汇集到一个无类型 enum 中——便于在 public Get/Has
	// 静态函数里通过 TEnableIf<HasXxx, T>::Type 来做"分支选择"。
	// 这样写也减少模板实例化时同名 decltype 的重复求值次数。
	// -------------------------------------------------------------------------
	enum ETraits : unsigned
	{
		HasVersion = unsigned(decltype(TestHasVersion<NetSerializerImpl>(nullptr))::Value),

		HasCustomNetReferenceIsPresent = unsigned(decltype(TestHasCustomNetReferenceIsPresent<NetSerializerImpl>(nullptr))::Value),
		HasCustomNetReferenceIsBool = unsigned(decltype(TestHasCustomNetReferenceIsBool<NetSerializerImpl>(nullptr))::Value),

		UseSerializerIsEqualIsPresent = unsigned(decltype(TestUseSerializerIsEqualIsPresent<NetSerializerImpl>(nullptr))::Value),
		UseSerializerIsEqualIsBool = unsigned(decltype(TestUseSerializerIsEqualIsBool<NetSerializerImpl>(nullptr))::Value),

		IsForwardingSerializerIsPresent = unsigned(decltype(TestIsForwardingSerializerIsPresent<NetSerializerImpl>(nullptr))::Value),
		IsForwardingSerializerIsBool = unsigned(decltype(TestIsForwardingSerializerIsBool<NetSerializerImpl>(nullptr))::Value),
		HasConnectionSpecificSerializationIsPresent = unsigned(decltype(TestHasConnectionSpecificSerializationIsPresent<NetSerializerImpl>(nullptr))::Value),
		HasConnectionSpecificSerializationIsBool = unsigned(decltype(TestHasConnectionSpecificSerializationIsBool<NetSerializerImpl>(nullptr))::Value),

		HasDynamicStateIsPresent = unsigned(decltype(TestHasDynamicStateIsPresent<NetSerializerImpl>(nullptr))::Value),
		HasDynamicStateIsBool = unsigned(decltype(TestHasDynamicStateIsBool<NetSerializerImpl>(nullptr))::Value),

		UseDefaultDeltaIsPresent = unsigned(decltype(TestUseDefaultDeltaIsPresent<NetSerializerImpl>(nullptr))::Value),
		UseDefaultDeltaIsBool = unsigned(decltype(TestUseDefaultDeltaIsBool<NetSerializerImpl>(nullptr))::Value),

		HasConfigType = unsigned(decltype(TestHasConfigType<NetSerializerImpl>(nullptr))::Value),
		HasSourceType = unsigned(decltype(TestHasSourceType<NetSerializerImpl>(nullptr))::Value),
		HasQuantizedType = unsigned(decltype(TestHasQuantizedType<NetSerializerImpl>(nullptr))::Value),
		SourceTypeIsPod = IsSourceTypePod<NetSerializerImpl>(nullptr),
		QuantizedTypeIsPod = IsQuantizedTypePod<NetSerializerImpl>(nullptr),

		HasDefaultConfig = TestHasDefaultConfig<NetSerializerImpl>(nullptr),

		HasSerialize = unsigned(decltype(TestHasSerialize<NetSerializerImpl>(nullptr))::Value),
		HasDeserialize = unsigned(decltype(TestHasDeserialize<NetSerializerImpl>(nullptr))::Value),
		HasSerializeDelta = unsigned(decltype(TestHasSerializeDelta<NetSerializerImpl>(nullptr))::Value),
		HasDeserializeDelta = unsigned(decltype(TestHasDeserializeDelta<NetSerializerImpl>(nullptr))::Value),
		HasQuantize = unsigned(decltype(TestHasQuantize<NetSerializerImpl>(nullptr))::Value),
		HasDequantize = unsigned(decltype(TestHasDequantize<NetSerializerImpl>(nullptr))::Value),
		HasIsEqual = unsigned(decltype(TestHasIsEqual<NetSerializerImpl>(nullptr))::Value),
		HasValidate = unsigned(decltype(TestHasValidate<NetSerializerImpl>(nullptr))::Value),
		HasFreeDynamicState = unsigned(decltype(TestHasFreeDynamicState<NetSerializerImpl>(nullptr))::Value),
		HasCloneDynamicState = unsigned(decltype(TestHasCloneDynamicState<NetSerializerImpl>(nullptr))::Value),
		HasCollectNetReferences = unsigned(decltype(TestHasCollectNetReferences<NetSerializerImpl>(nullptr))::Value),
		HasApply = unsigned(decltype(TestHasApply<NetSerializerImpl>(nullptr))::Value),
	};

public:
	// =========================================================================
	// 公开 Get/Has 静态访问器。每个"按需特征"都拆成两个同名重载：
	//   - 用户实现版本：通过 TEnableIf<HasXxx, T>::Type 限定（仅 HasXxx==true 编译）；
	//   - 默认/兜底版本：通过 TEnableIf<!HasXxx, T>::Type 限定。
	// 第三个模板参数 (bool V=true / char V=0) 让两个重载的模板形参列表
	// 在签名上有所区别，避免被认为是重复定义。
	// =========================================================================

	// ----- Version：必填。HasVersion=true 用 Impl::Version；否则返回 ~0U（不可能值）；
	//       实际上 Validate() 中 static_assert(HasVersion) 让"否则"分支永不被触达。
	template<typename T = void, typename U = typename TEnableIf<HasVersion, T>::Type, bool V = true>
	static constexpr uint32 GetVersion() { return NetSerializerImpl::Version; }

	template<typename T = void, typename U = typename TEnableIf<!HasVersion, T>::Type, char V = 0>
	static constexpr uint32 GetVersion() { return ~0U; }

	// ----- HasCustomNetReference：缺省 false（即 Iris 不会调 CollectNetReferences）。
	template<typename T = void, typename U = typename TEnableIf<HasCustomNetReferenceIsBool, T>::Type, bool V = true>
	static constexpr bool HasCustomNetReference() { return NetSerializerImpl::bHasCustomNetReference; }

	template<typename T = void, typename U = typename TEnableIf<!HasCustomNetReferenceIsBool, T>::Type, char V = 0>
	static constexpr bool HasCustomNetReference() { return false; }

	// ----- UseSerializerIsEqual：仅在"声明了该 trait 且实现了 IsEqual"时返回 Impl 的值；
	//       否则一律 false（让 Iris 选择默认比较路径，可能在某些热点用 memcmp）。
	template<typename T = void, typename U = typename TEnableIf<UseSerializerIsEqualIsBool && HasIsEqual, T>::Type, bool V = true>
	static constexpr bool UseSerializerIsEqual() { return NetSerializerImpl::bUseSerializerIsEqual; }

	template<typename T = void, typename U = typename TEnableIf<!(UseSerializerIsEqualIsBool && HasIsEqual), T>::Type, char V = 0>
	static constexpr bool UseSerializerIsEqual() { return false; }

	// ----- IsForwardingSerializer：缺省 false。true 时下方 ValidateForwardingSerializer 强制额外必填项。
	template<typename T = void, typename U = typename TEnableIf<IsForwardingSerializerIsBool, T>::Type, bool V = true>
	static constexpr bool IsForwardingSerializer() { return NetSerializerImpl::bIsForwardingSerializer; }

	template<typename T = void, typename U = typename TEnableIf<!IsForwardingSerializerIsBool, T>::Type, char V = 0>
	static constexpr bool IsForwardingSerializer() { return false; }

	// ----- HasConnectionSpecificSerialization：缺省 false。true 时 Iris 不会跨连接缓存写入结果。
	template<typename T = void, typename U = typename TEnableIf<HasConnectionSpecificSerializationIsBool, T>::Type, bool V = true>
	static constexpr bool HasConnectionSpecificSerialization() { return NetSerializerImpl::bHasConnectionSpecificSerialization; }

	template<typename T = void, typename U = typename TEnableIf<!HasConnectionSpecificSerializationIsBool, T>::Type, char V = 0>
	static constexpr bool HasConnectionSpecificSerialization() { return false; }

	// ----- HasDynamicState：缺省 false。true 时必须实现 Free+Clone DynamicState（Validate 强制）。
	template<typename T = void, typename U = typename TEnableIf<HasDynamicStateIsBool, T>::Type, bool V = true>
	static constexpr bool HasDynamicState() { return NetSerializerImpl::bHasDynamicState; }

	template<typename T = void, typename U = typename TEnableIf<!HasDynamicStateIsBool, T>::Type, char V = 0>
	static constexpr bool HasDynamicState() { return false; }

	// ----- ShouldUseDefaultDelta：声明了则用 Impl 值；未声明则按"缺省 true"处理。
	template<typename T = void, typename U = typename TEnableIf<UseDefaultDeltaIsBool, T>::Type, bool V = true>
	static constexpr bool ShouldUseDefaultDelta() { return NetSerializerImpl::bUseDefaultDelta; }

	template<typename T = void, typename U = typename TEnableIf<!UseDefaultDeltaIsBool, T>::Type, char V = 0>
	static constexpr bool ShouldUseDefaultDelta() { return true; }

	// ----- Serialize/Deserialize：必填。Validate 中的 static_assert(HasSerialize/HasDeserialize)
	//       会让"否则"分支不会被实例化，所以 fallback 返回 nullptr 是哑存根。
	template<typename T = void, typename U = typename TEnableIf<HasSerialize, T>::Type, bool V = true>
	static NetSerializeFunction GetSerializeFunction() { return NetSerializerImpl::Serialize; }

	template<typename T = void, typename U = typename TEnableIf<!HasSerialize, T>::Type, char V = 0>
	static NetSerializeFunction GetSerializeFunction() { return NetSerializeFunction(0); }

	template<typename T = void, typename U = typename TEnableIf<HasDeserialize, T>::Type, bool V = true>
	static NetDeserializeFunction GetDeserializeFunction() { return NetSerializerImpl::Deserialize; }

	template<typename T = void, typename U = typename TEnableIf<!HasDeserialize, T>::Type, char V = 0>
	static NetDeserializeFunction GetDeserializeFunction() { return NetDeserializeFunction(0); }

	// Provide a default SerializeDelta implementation if needed. The default will compare the value with previous value and write an extra bit and forward to Serialize if the value differs
	// It is possible to opt out by adding static constexpr bool bUseDefaultDelta = false; in the serializer declaration
	// -------------------------------------------------------------------------
	// 中文：SerializeDelta 的四种重载——靠 TEnableIf 互斥组合，编译期最多只有
	// 一个被实例化：
	//   ① Impl 自己实现了 SerializeDelta              → 用 Impl::SerializeDelta；
	//   ② 没实现 SerializeDelta 且禁用了 DefaultDelta → 一参数 NetSerializeDeltaDefault<Serialize>（无 delta，节省判等开销）；
	//   ③ 没实现且使用 DefaultDelta 且 Impl 有 IsEqual → 用 Impl::IsEqual 做判等的双参数 NetSerializeDeltaDefault；
	//   ④ 没实现且使用 DefaultDelta 且 Impl 无 IsEqual → 用 NetIsEqualDefault<SourceType>（按值 == 比较）做判等。
	// -------------------------------------------------------------------------
	template<typename T = void, typename U = typename TEnableIf<HasSerializeDelta, T>::Type, bool V = true>
	static NetSerializeDeltaFunction GetSerializeDeltaFunction() { return NetSerializerImpl::SerializeDelta; }

	template<typename T = void, typename U = typename TEnableIf<!HasSerializeDelta && !ShouldUseDefaultDelta(), T>::Type, int V = 0>
	static NetSerializeDeltaFunction GetSerializeDeltaFunction() { return NetSerializeDeltaDefault<NetSerializerImpl::Serialize>; }

	template<typename T = void, typename U = typename TEnableIf<!HasSerializeDelta && ShouldUseDefaultDelta() && HasIsEqual, T>::Type, char V = 0>
	static NetSerializeDeltaFunction GetSerializeDeltaFunction() { return NetSerializeDeltaDefault<NetSerializerImpl::Serialize, NetSerializerImpl::IsEqual>; }

	template<typename T = void, typename U = typename TEnableIf<!HasSerializeDelta && ShouldUseDefaultDelta() && !HasIsEqual, T>::Type, unsigned char V = 0>
	static NetSerializeDeltaFunction GetSerializeDeltaFunction() { return NetSerializeDeltaDefault<NetSerializerImpl::Serialize, NetIsEqualDefault<typename NetSerializerImpl::SourceType> >; }

	// Provide a default DeserializeDelta implementation if needed. The default will call Deserialize.
	// -------------------------------------------------------------------------
	// 中文：DeserializeDelta 的四种重载（与 SerializeDelta 一一对应）：
	//   ① Impl 自己实现 DeserializeDelta；
	//   ② 否则禁用 DefaultDelta → 一参数 NetDeserializeDeltaDefault<Deserialize>（无 delta，直接转发解码）；
	//   ③ 否则使用 DefaultDelta 且 Impl 有 Clone+Free → 多参数版本，
	//      实现"读 1bit isEqual；若 true 则 Free 旧 + Memcpy + Clone Prev；否则全量 Deserialize"；
	//   ④ 否则使用 DefaultDelta 且无 Clone/Free → 多参数版本但 Free=Clone=nullptr，
	//      Equal 分支只 Memcpy（适用于无动态状态的 Quantized）。
	// -------------------------------------------------------------------------
	template<typename T = void, typename U = typename TEnableIf<HasDeserializeDelta, T>::Type, bool V = true>
	static NetDeserializeDeltaFunction GetDeserializeDeltaFunction(const T* = nullptr) { return NetSerializerImpl::DeserializeDelta; }

	template<typename T = void, typename U = typename TEnableIf<!HasDeserializeDelta && !ShouldUseDefaultDelta(), T>::Type, int V = 0>
	static NetDeserializeDeltaFunction GetDeserializeDeltaFunction(const void* = nullptr) { return NetDeserializeDeltaDefault<NetSerializerImpl::Deserialize>; }

	template<typename T = void, typename U = typename TEnableIf<!HasDeserializeDelta && ShouldUseDefaultDelta() && (HasCloneDynamicState && HasFreeDynamicState), T>::Type, char V = 0>
	static NetDeserializeDeltaFunction GetDeserializeDeltaFunction(const void* = nullptr) { return NetDeserializeDeltaDefault<GetQuantizedTypeSize(), NetSerializerImpl::Deserialize, NetSerializerImpl::FreeDynamicState, NetSerializerImpl::CloneDynamicState>; }

	template<typename T = void, typename U = typename TEnableIf<!HasDeserializeDelta && ShouldUseDefaultDelta() && !(HasCloneDynamicState && HasFreeDynamicState), T>::Type, unsigned char V = 0>
	static NetDeserializeDeltaFunction GetDeserializeDeltaFunction(const void* = nullptr) { return NetDeserializeDeltaDefault<GetQuantizedTypeSize(), NetSerializerImpl::Deserialize, NetFreeDynamicStateFunction(nullptr), NetCloneDynamicStateFunction(nullptr)>; }

	// Provide a default Quantize implementation if needed. The default will copy the value.
	// 中文：缺失 Quantize 时回落 NetQuantizeDefault<SourceType>（按值赋值）。
	// Validate 要求"非 POD SourceType 必须实现 Quantize"，所以默认路径仅用于 POD。
	template<typename T = void, typename U = typename TEnableIf<HasQuantize, T>::Type, bool V = true>
	static NetQuantizeFunction GetQuantizeFunction() { return NetSerializerImpl::Quantize; }

	template<typename T = void, typename U = typename TEnableIf<!HasQuantize, T>::Type, char V = 0>
	static NetQuantizeFunction GetQuantizeFunction() { return NetQuantizeDefault<typename NetSerializerImpl::SourceType>; }

	// Provide a default Dequantize implementation if needed. The default will copy the value.
	// 中文：与 Quantize 对称的反向赋值默认实现。
	template<typename T = void, typename U = typename TEnableIf<HasDequantize, T>::Type, bool V = true>
	static NetDequantizeFunction GetDequantizeFunction() { return NetSerializerImpl::Dequantize; }

	template<typename T = void, typename U = typename TEnableIf<!HasDequantize, T>::Type, char V = 0>
	static NetDequantizeFunction GetDequantizeFunction() { return NetDequantizeDefault<typename NetSerializerImpl::SourceType>; }

	// Provide a default IsEqual implementation if needed. The default will call the equality operator.
	// 中文：缺失 IsEqual 时回落 NetIsEqualDefault<SourceType>（用 SourceType 的 operator== 比较）。
	template<typename T = void, typename U = typename TEnableIf<HasIsEqual, T>::Type, bool V = true>
	static NetIsEqualFunction GetIsEqualFunction() { return NetSerializerImpl::IsEqual; }

	template<typename T = void, typename U = typename TEnableIf<!HasIsEqual, T>::Type, char V = 0>
	static NetIsEqualFunction GetIsEqualFunction() { return NetIsEqualDefault<typename NetSerializerImpl::SourceType>; }

	// Provide a default Validate implementation if needed. The default will not perform any validation.
	// 中文：缺失 Validate 时回落 NetValidateDefault（恒返回 true，对端数据不做校验）。
	template<typename T = void, typename U = typename TEnableIf<HasValidate, T>::Type, bool V = true>
	static NetValidateFunction GetValidateFunction() { return NetSerializerImpl::Validate; }

	template<typename T = void, typename U = typename TEnableIf<!HasValidate, T>::Type, char V = 0>
	static NetValidateFunction GetValidateFunction() { return NetValidateDefault<>; }

	// ----- CollectNetReferences：仅当 bHasCustomNetReference=true 时必须实现；
	//       否则函数指针为 nullptr，Iris 不会调用，节省一层间接。
	template<typename T = void, typename U = typename TEnableIf<HasCollectNetReferences, T>::Type, bool V = true>
	static NetCollectNetReferencesFunction GetCollectNetReferencesFunction() { return NetSerializerImpl::CollectNetReferences; }

	template<typename T = void, typename U = typename TEnableIf<!HasCollectNetReferences, T>::Type, char V = 0>
	static NetCollectNetReferencesFunction GetCollectNetReferencesFunction() { return NetCollectNetReferencesFunction(nullptr); }

	// ----- Apply：可选钩子，反量化后应用到 Source 实例的额外逻辑（如触发 RepNotify 通知）。
	template<typename T = void, typename U = typename TEnableIf<HasApply, T>::Type, bool V = true>
	static NetApplyFunction GetApplyFunction() { return NetSerializerImpl::Apply; }

	template<typename T = void, typename U = typename TEnableIf<!HasApply, T>::Type, char V = 0>
	static NetApplyFunction GetApplyFunction() { return NetApplyFunction(nullptr); }

	// CloneDynamicState
	// 中文：仅当 (HasCloneDynamicState && (IsForwardingSerializer || HasDynamicState)) 时使用 Impl 的实现。
	// 双条件的原因：
	//   - HasDynamicState=true：Quantized 自带动态分配，必须 Clone；
	//   - IsForwardingSerializer=true：转发型 Serializer 内部成员可能有动态状态，
	//     即便自身没声明 HasDynamicState，也必须把 CloneDynamicState 转发出去；
	//   - 其它情况：把指针置 nullptr，Iris 跳过 Clone 调用。
	template<typename T = void, typename U = typename TEnableIf<HasCloneDynamicState && (IsForwardingSerializer() || HasDynamicState()), T>::Type, bool V = true>
	static NetCloneDynamicStateFunction GetCloneDynamicStateFunction() { return NetSerializerImpl::CloneDynamicState; }

	template<typename T = void, typename U = typename TEnableIf<!(HasCloneDynamicState && (IsForwardingSerializer() || HasDynamicState())), T>::Type, char V = 0>
	static NetCloneDynamicStateFunction GetCloneDynamicStateFunction() { return NetCloneDynamicStateFunction(nullptr); }

	// FreeDynamicState
	// 中文：与 CloneDynamicState 同样的双条件——见上方注释。
	template<typename T = void, typename U = typename TEnableIf<HasFreeDynamicState && (IsForwardingSerializer() || HasDynamicState()), T>::Type, bool V = true>
	static NetFreeDynamicStateFunction GetFreeDynamicStateFunction() { return NetSerializerImpl::FreeDynamicState; }

	template<typename T = void, typename U = typename TEnableIf<!(HasFreeDynamicState && (IsForwardingSerializer() || HasDynamicState())), T>::Type, char V = 0>
	static NetFreeDynamicStateFunction GetFreeDynamicStateFunction() { return NetFreeDynamicStateFunction(nullptr); }

	// DefaultConfig
	// 中文：仅当 ConfigType 与 DefaultConfig 都存在时才返回静态 DefaultConfig 实例的地址；
	// 否则返回 nullptr，Iris 在描述符层面会要求外部为每个属性单独构造 Config。
	template<typename T = void, typename U = typename TEnableIf<HasConfigType && HasDefaultConfig, T>::Type, bool V = true>
	static const FNetSerializerConfig* GetDefaultConfig() { return &NetSerializerImpl::DefaultConfig; }

	template<typename T = void, typename U = typename TEnableIf<!(HasConfigType && HasDefaultConfig), T>::Type, char V = 0>
	static constexpr FNetSerializerConfig* GetDefaultConfig() { return nullptr; }

	// Type sizes and alignments
	// 中文：FNetSerializer 字段使用这些大小/对齐做内部内存池分配
	// （Quantized 状态/Config 数组）。Validate 中会校验这些大小/对齐能装进
	// FNetSerializer 的小整型字段（uint16/uint8）。
	template<typename T = void, typename U = typename TEnableIf<HasConfigType, T>::Type, bool V = true>
	static constexpr uint32 GetConfigTypeSize() { return sizeof(typename NetSerializerImpl::ConfigType); }

	template<typename T = void, typename U = typename TEnableIf<!HasConfigType, T>::Type, char V = 0>
	static constexpr uint32 GetConfigTypeSize() { return 0; }

	template<typename T = void, typename U = typename TEnableIf<HasConfigType, T>::Type, bool V = true>
	static constexpr uint32 GetConfigTypeAlignment() { return alignof(typename NetSerializerImpl::ConfigType); }

	template<typename T = void, typename U = typename TEnableIf<!HasConfigType, T>::Type, char V = 0>
	static constexpr uint32 GetConfigTypeAlignment() { return 1; }

	// 中文：QuantizedType 的大小/对齐选择规则——三档优先级：
	//   ① 显式定义了 QuantizedType  → 用它的 sizeof/alignof；
	//   ② 否则若有 SourceType（非 void）→ 退化为 SourceType 的 sizeof/alignof
	//      （等价于"无 Quantize 阶段"，源/线协议同构）；
	//   ③ 否则（连 SourceType 都没有，比如 Nop Serializer）→ size=0, alignment=1。
	// 这里的 std::conditional_t<...,uint8,SourceType> 是为了让 sizeof(void) 不出现：
	// 当 SourceType==void 时，按 uint8 算 sizeof（仍会被外层条件判 0 覆盖）。
	template<typename T = void, typename U = typename TEnableIf<HasQuantizedType, T>::Type, bool V = true, bool W = true>
	static constexpr uint32 GetQuantizedTypeSize() { return sizeof(typename NetSerializerImpl::QuantizedType); }

	template<typename T = void, typename U = typename TEnableIf<!HasQuantizedType && HasSourceType, T>::Type, bool V = true, char W = 0>
	static constexpr uint32 GetQuantizedTypeSize() { return std::is_same_v<void, typename NetSerializerImpl::SourceType> ? uint32(0) : sizeof(std::conditional_t<std::is_same_v<void, typename NetSerializerImpl::SourceType>, uint8, typename NetSerializerImpl::SourceType>); }

	template<typename T = void, typename U = typename TEnableIf<!(HasSourceType || HasQuantizedType), T>::Type, char V = 0>
	static constexpr uint32 GetQuantizedTypeSize() { return 0; }

	template<typename T = void, typename U = typename TEnableIf<HasQuantizedType, T>::Type, bool V = true, bool W = true>
	static constexpr uint32 GetQuantizedTypeAlignment() { return alignof(typename NetSerializerImpl::QuantizedType); }

	template<typename T = void, typename U = typename TEnableIf<!HasQuantizedType && HasSourceType, T>::Type, bool V = true, char W = 0>
	static constexpr uint32 GetQuantizedTypeAlignment() { return alignof(std::conditional_t<std::is_same_v<void, typename NetSerializerImpl::SourceType>, uint8, typename NetSerializerImpl::SourceType>); }

	template<typename T = void, typename U = typename TEnableIf<!(HasSourceType || HasQuantizedType), T>::Type, char V = 0>
	static constexpr uint32 GetQuantizedTypeAlignment() { return 1; }

	/**
	 * 把 6 个 trait 汇集成 ENetSerializerTraits 位标志，写入 FNetSerializer.Traits。
	 * 这是 Iris 在 RS 调度阶段判断"该 Serializer 需要怎样的处理"的依据
	 * （是否需要 per-connection 序列化、是否需要 Clone/Free、是否调 Apply 等）。
	 * 注意 HasApply 这里直接读 ETraits::HasApply（unsigned 位），它来自 SFINAE 探测器。
	 */
	static constexpr ENetSerializerTraits GetTraits()
	{ 
		ENetSerializerTraits Traits = ENetSerializerTraits::None;
		Traits |= (IsForwardingSerializer() ? ENetSerializerTraits::IsForwardingSerializer : ENetSerializerTraits::None);
		Traits |= (HasConnectionSpecificSerialization() ? ENetSerializerTraits::HasConnectionSpecificSerialization : ENetSerializerTraits::None);
		Traits |= (HasCustomNetReference() ? ENetSerializerTraits::HasCustomNetReference : ENetSerializerTraits::None);
		Traits |= (HasDynamicState() ? ENetSerializerTraits::HasDynamicState : ENetSerializerTraits::None);
		Traits |= (UseSerializerIsEqual() ? ENetSerializerTraits::UseSerializerIsEqual : ENetSerializerTraits::None);
		Traits |= (HasApply ? ENetSerializerTraits::HasApply : ENetSerializerTraits::None);

		return Traits;
	}

	/**
	 * 编译期严格性检查。所有 static_assert 在 ConstructNetSerializer 实例化时被触发，
	 * 写错 NetSerializerImpl 的人会立刻得到清晰错误信息。
	 *
	 * 主要类别：
	 *   · 必填项：Version / ConfigType / SourceType / Serialize / Deserialize；
	 *   · 命名形式：bXxx 必须是 static constexpr bool（IsPresent && !IsBool 时报"形式错误"）；
	 *   · 容量限制：ConfigTypeSize/Alignment 与 QuantizedTypeSize/Alignment 必须能装进
	 *     FNetSerializer 中对应的小整型字段（uint16/uint8）；
	 *   · 一致性：Quantize ↔ Dequantize 必须同时实现或同时缺失；
	 *     SerializeDelta ↔ DeserializeDelta 同上；
	 *     有 QuantizedType → 必须实现 Quantize+Dequantize；
	 *     非 POD SourceType → 必须实现 Quantize+Dequantize；
	 *     有 Quantize → 必须有 IsEqual（默认 IsEqual 走 SourceType operator== 无法处理量化结构）；
	 *     bHasCustomNetReference=true → 必须实现 CollectNetReferences；
	 *     bHasDynamicState=true → 必须同时实现 Free + Clone DynamicState；
	 *   · 转发型 Serializer 的额外约束在 ValidateForwardingSerializer 中。
	 */
	static void Validate()
	{
		static_assert(HasVersion, "FNetSerializer must have a 'static constexpr uint32 Version' member.");

		// IsPresent && !IsBool → 用户写了成员但类型/形式不对；通过 (!IsPresent || IsBool) 给出清晰提示。
		static_assert(!IsForwardingSerializerIsPresent || IsForwardingSerializerIsBool, "FNetSerializer bIsForwardingSerializer member should be declared as 'static constexpr bool bIsForwardingSerializer'.");
		static_assert(!HasConnectionSpecificSerializationIsPresent || HasConnectionSpecificSerializationIsBool, "FNetSerializer bHasConnectionSpecificSerialization member should be declared as 'static constexpr bool bHasConnectionSpecificSerialization'.");
		static_assert(!HasCustomNetReferenceIsPresent || HasCustomNetReferenceIsBool, "FNetSerializer bHasCustomNetReference member should be declared as 'static constexpr bool bHasCustomNetReference'.");
		static_assert(!HasDynamicStateIsPresent || HasDynamicStateIsBool, "FNetSerializer bHasDynamicState member should be declared as 'static constexpr bool bHasDynamicState'.");

		static_assert(HasConfigType, "FNetSerializer must have a ConfigType.");
		// 容量限制：FNetSerializer 用窄整型字段存 size/alignment（节省函数表行宽）。
		static_assert(GetConfigTypeSize() <= TNumericLimits<decltype(FNetSerializer::ConfigTypeSize)>::Max() , "FNetSerializer NetSerializerConfig type is too large.");
		static_assert(GetConfigTypeAlignment() <= TNumericLimits<decltype(FNetSerializer::ConfigTypeAlignment)>::Max() , "FNetSerializer NetSerializerConfig type has too large alignment requirements.");

		static_assert(HasSourceType, "FNetSerializer must have a SourceType.");
		// QuantizedType 必须 POD：因为 Iris 会按字节 memcpy/memzero（见 NetDeserializeDeltaDefault）。
		static_assert(!HasQuantizedType || QuantizedTypeIsPod, "QuantizedType in FNetSerializer must be POD.");
		static_assert(GetQuantizedTypeSize() <= TNumericLimits<decltype(FNetSerializer::QuantizedTypeSize)>::Max() , "FNetSerializer quantized type is too large.");
		static_assert(GetQuantizedTypeAlignment() <= TNumericLimits<decltype(FNetSerializer::QuantizedTypeAlignment)>::Max() , "FNetSerializer quantized type has too large alignment requirements.");

		static_assert(HasSerialize, "FNetSerializer must implement Serialize.");
		static_assert(HasDeserialize, "FNetSerializer must implement Deserialize.");

		// SerializeDelta ↔ DeserializeDelta 必须配对：否则 sender/receiver 协议不一致会 corrupt 数据。
		static_assert(HasSerializeDelta == HasDeserializeDelta, "FNetSerializer should implement both SerializeDelta and DeserializeDelta or none of them.");

		// 非 POD SourceType 不能用默认按值赋值的 Quantize（会调对象的拷贝构造，可能抛/分配）。
		static_assert(HasQuantize || SourceTypeIsPod, "FNetSerializer must implement Quantize and Dequantize when SourceType isn't POD.");
		static_assert(!HasQuantizedType || (HasQuantize && HasDequantize), "FNetSerializer must implement Quantize and Dequantize when it has a QuantizedType.");
		static_assert(HasQuantize == HasDequantize, "FNetSerializer must implement both Quantize and Dequantize or none of them.");
		// 有 Quantize（即 SourceType≠QuantizedType）后默认 IsEqual 用 SourceType 的 ==，
		// 但 IsEqual 实际可能在 Quantized 数据上调用——必须自定义 IsEqual 才能正确比较 Quantized。
		static_assert(!HasQuantize || HasIsEqual, "FNetSerializer must implement IsEqual when it has Quantize.");

		static_assert(!HasCustomNetReference() || (HasCustomNetReference() && HasCollectNetReferences), "FNetSerializer with bHasCustomNetReference = true must implement CollectNetReferences method.");

		static_assert(!HasDynamicStateIsBool || (HasFreeDynamicState && HasCloneDynamicState), "FNetSerializer must implement CloneDynamicState and FreeDynamicState when it has dynamic state.");

		static_assert(!UseDefaultDeltaIsPresent || UseDefaultDeltaIsBool, "FNetSerializer bUseDefaultDelta member should be declared as 'static constexpr bool bUseDefaultDelta'.");

		// 转发型 Serializer 的额外要求——拆成单独函数走 SFINAE 分支（仅 IsForwardingSerializer=true 时才触发）。
		ValidateForwardingSerializer();
	}

private:
	/**
	 * IsForwardingSerializer=true 时的额外约束：
	 * 必须把所有"可能用到的 hook"全部实现（不能用默认填充），因为内部成员的状态
	 * 转发不能简单地复用按值/按字节默认实现——例如 StructNetSerializer 必须把
	 * 调用精准地分发给每个 member 的子 Serializer。
	 */
	template<typename T = void, typename U = typename TEnableIf<IsForwardingSerializer(), T>::Type, bool V = true>
	static void ValidateForwardingSerializer()
	{
		static_assert(HasSerialize, "Forwarding FNetSerializer must implement Serialize.");
		static_assert(HasDeserialize, "Forwarding FNetSerializer must implement Deserialize.");
		static_assert(HasSerializeDelta, "Forwarding FNetSerializer must implement SerializeDelta.");
		static_assert(HasDeserializeDelta, "Forwarding FNetSerializer must implement DeserializeDelta.");
		static_assert(HasQuantize, "Forwarding FNetSerializer must implement Quantize.");
		static_assert(HasDequantize, "Forwarding FNetSerializer must implement Dequantize.");
		static_assert(HasIsEqual, "Forwarding FNetSerializer must implement IsEqual.");
		static_assert(HasValidate, "Forwarding FNetSerializer must implement Validate.");
		static_assert(HasCloneDynamicState, "Forwarding FNetSerializer must implement CloneDynamicState.");
		static_assert(HasFreeDynamicState, "Forwarding FNetSerializer must implement FreeDynamicState.");
		static_assert(HasCollectNetReferences, "Forwarding FNetSerializer must implement CollectNetReferences.");
	}

	/** 非转发型 Serializer——不做额外检查（只做 Validate() 主体的通用检查）。 */
	template<typename T = void, typename U = typename TEnableIf<!IsForwardingSerializer(), T>::Type, char V = 0>
	static void ValidateForwardingSerializer()
	{
	}
};

}
