// Copyright Epic Games, Inc. All Rights Reserved.
// =============================================================================
// 中文说明：
//   TInternalNetSerializerBuilder 是 TNetSerializerBuilder 的"私有版"补丁。
//
//   背景：Public 版的 TNetSerializerBuilder（NetSerializerBuilder.inl）通过
//   SFINAE 探测一组"通用 trait"（bIsForwardingSerializer / bHasDynamicState /
//   bHasCustomNetReference / bHasConnectionSpecificSerialization / 
//   bUseSerializerIsEqual / bUseDefaultDelta），并据此填充 FNetSerializer 函数
//   指针表与 ENetSerializerTraits 标志位。
//
//   但 Iris 内部还有一些"特权 trait"（例如某些 Serializer 仅供内部使用，
//   会拥有跟外部 Serializer 不同的内部 trait 探测点）。这种内部专用 trait
//   不应通过 Public API 暴露给外部模块，因此被放在这个 Private 类中。
//
//   具体使用：BitfieldNetSerializer / NetRoleNetSerializer / 
//   ArrayPropertyNetSerializer / StructNetSerializer 等内部 Serializer 通过
//   UE_NET_DECLARE_SERIALIZER_INTERNAL + UE_NET_IMPLEMENT_SERIALIZER_INTERNAL
//   宏（见 InternalNetSerializer.h）来声明，宏内部会调用 TInternalNetSerializer
//   ::ConstructNetSerializer，从而把 TInternalNetSerializerBuilder::GetTraits()
//   合并到最终生成的 FNetSerializer 上。
//
//   当前实现：本类目前是一个"占位"——FTraits/ETraits 都为空，GetTraits() 返回
//   None，Validate() 不做检查。这是一个故意保留的扩展点：当未来需要为内部
//   Serializer 增加新的 trait（例如"仅服务器使用"标志）时，只需在本类内
//   增加对应的 Test* SFINAE 探测器与 ETraits 位，无需改动 Public 头。
// =============================================================================

#pragma once

#include "Iris/Serialization/NetSerializer.h"

namespace UE::Net::Private
{

// This implementation only deals with specifics that only internal NetSerializer implementations are allowed to use.
//
// 中文：本类只处理"内部 NetSerializer"独有的特殊点；外部（Public）NetSerializer
//      不允许声明这些 trait。当前为空骨架，等待按需扩展。
template<typename NetSerializerImpl>
class TInternalNetSerializerBuilder final
{
private:
	/** SFINAE "true 哨兵"——同 Public Builder 的写法，便于 decltype(...)::Value 取 1。 */
	enum class ETrueType : unsigned
	{
		Value = 1
	};

	/** SFINAE "false 哨兵"——便于 decltype(...)::Value 取 0。 */
	enum class EFalseType : unsigned
	{
		Value = 0
	};

	/**
	 * 内部 trait 的"参考默认值"集合。
	 * 当前为空（无内部专用 trait）。如果未来增加，例如：
	 *   static constexpr bool bIsServerOnlySerializer = false;
	 * Validate() 中可用 std::is_same_v<decltype(&FTraits::bXxx), decltype(&U::bXxx)>
	 * 探测派生 Impl 是否正确以 static constexpr bool 形式声明该成员。
	 */
	struct FTraits
	{
	};

	/** 用于按"成员函数指针签名"做 SFINAE 探测；当前未使用。 */
	template<typename U, U> struct FSignatureCheck;
	/** 用于按"嵌套类型/decltype 表达式合法性"做 SFINAE 探测；当前未使用。 */
	template<typename> struct FTypeCheck;

	/**
	 * 探测结果聚合枚举（位组合）。当前为空，未来扩展时每加一个内部 trait 在此
	 * 增加一个比特位（HasXxxIsPresent / HasXxxIsBool）即可。
	 */
	enum ETraits : unsigned
	{
	};

public:
	/**
	 * 返回从 Impl 推导出的"内部专用 trait"位集，最终会被 OR 到外层
	 * FNetSerializer.Traits（详见 TInternalNetSerializer::ConstructNetSerializer）。
	 * 当前实现：始终返回 None。
	 */
	static constexpr ENetSerializerTraits GetTraits() { return ENetSerializerTraits::None; }

	/**
	 * 编译期 static_assert 校验：当前没有需要校验的内部 trait，因此为空。
	 * 当未来增加内部 trait 时，会在此放 static_assert 检查 Impl 的成员声明形式。
	 */
	static void Validate()
	{
	}
};

}
