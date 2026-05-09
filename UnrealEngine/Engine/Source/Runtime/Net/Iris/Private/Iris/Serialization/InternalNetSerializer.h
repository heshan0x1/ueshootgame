// Copyright Epic Games, Inc. All Rights Reserved.
// =============================================================================
// 中文说明：
//   本头文件提供"内部 NetSerializer 声明/实现宏"——
//   UE_NET_DECLARE_SERIALIZER_INTERNAL / UE_NET_IMPLEMENT_SERIALIZER_INTERNAL，
//   以及它们底层依赖的辅助类 TInternalNetSerializer<>。
//
//   内部 vs 外部 Serializer 的区别：
//     · 外部（公共）Serializer：通过 Public 头 NetSerializer.h 提供的
//       UE_NET_DECLARE_SERIALIZER / UE_NET_IMPLEMENT_SERIALIZER 宏 + 
//       TNetSerializer<Impl> 模板填充 FNetSerializer。游戏代码、其他模块都
//       可以使用。
//     · 内部 Serializer：仅 Iris Private 目录下的实现使用（如 BitfieldNetSerializer、
//       NetRoleNetSerializer、ArrayPropertyNetSerializer、StructNetSerializer 等
//       这种"和 Iris 内部状态深度耦合"的 Serializer）。它们除了具备所有公共
//       trait 之外，还允许声明额外的"内部专用 trait"——这些 trait 由
//       TInternalNetSerializerBuilder 探测和合并。
//
//   实现思路：
//     TInternalNetSerializer::ConstructNetSerializer
//       1) 先调用 TNetSerializer<Impl>::ConstructNetSerializer 拿到外部公共
//          Serializer 的标准构造结果（已填好所有函数指针 + 公共 trait）。
//       2) 实例化 TInternalNetSerializerBuilder<Impl>，调用 Validate() 做内部
//          trait 的额外编译期检查。
//       3) 把内部 Builder 推导出的 ENetSerializerTraits 通过 |= 合并进
//          Serializer.Traits。
//
//   宏的具体结构：
//     UE_NET_DECLARE_SERIALIZER_INTERNAL 直接复用 UE_NET_DECLARE_SERIALIZER
//     （因为头文件声明阶段不需要区分内部/外部，只需提供一个 NetSerializerInfo 结构）。
//     UE_NET_IMPLEMENT_SERIALIZER_INTERNAL 与 UE_NET_IMPLEMENT_SERIALIZER 的差异
//     仅在于把 TNetSerializer<...>::ConstructNetSerializer 替换为
//     TInternalNetSerializer<...>::ConstructNetSerializer，从而走"内部 Builder"
//     路径，其余（GetQuantizedTypeSize/Alignment、GetDefaultConfig）保持一致，
//     仍由公共 TNetSerializerBuilder 统一计算（这些字段无论内/外部都是同一套）。
// =============================================================================

#pragma once

#include "Iris/Serialization/NetSerializer.h"
#include "Iris/Serialization/InternalNetSerializerBuilder.h"

namespace UE::Net::Private
{

/**
 * "内部 NetSerializer"构造器。
 * 在公共 TNetSerializer<Impl> 基础上，再叠加 TInternalNetSerializerBuilder<Impl>
 * 推导出的内部 trait。
 *
 * @tparam NetSerializerImpl 用户编写的 NetSerializer 实现 struct。要求与公共
 *         Serializer 同样具备 SourceType / QuantizedType（可选）/ ConfigType /
 *         Serialize/Deserialize 等成员；可选地额外声明仅供内部识别的 trait。
 */
template<typename NetSerializerImpl>
class TInternalNetSerializer final
{
public:
	/**
	 * 在编译期构造 FNetSerializer 函数表 + Traits。
	 *
	 * @param Name Serializer 名（仅用于调试/日志/错误提示）。
	 * @return 完整填好的 FNetSerializer，包含公共 trait 与内部 trait 的并集。
	 */
	static constexpr FNetSerializer ConstructNetSerializer(const TCHAR* Name)
	{
		// Start off with the basics
		// 第 1 步：先用公共 Builder 走完整的构造流程：
		//   - 函数指针表（Serialize/Deserialize/Quantize/.../FreeDynamicState）
		//   - 类型大小/对齐（QuantizedTypeSize/Alignment、ConfigTypeSize/Alignment）
		//   - 公共 trait（IsForwardingSerializer / HasDynamicState / 
		//     HasCustomNetReference / HasConnectionSpecificSerialization / 
		//     UseSerializerIsEqual / HasApply）
		//   - DefaultConfig 指针、Name 等
		FNetSerializer Serializer = TNetSerializer<NetSerializerImpl>::ConstructNetSerializer(Name);

		// Add additional internal stuff
		// 第 2 步：实例化内部 Builder 做内部专用 trait 的探测与校验。
		TInternalNetSerializerBuilder<NetSerializerImpl> Builder;
		// 编译期 static_assert：检查 Impl 是否以正确形式声明了内部 trait。
		// 当前 InternalBuilder 内部为空，但保留此调用以便未来扩展。
		Builder.Validate();

		// 第 3 步：把内部 Builder 推导出的 trait 位 OR 进 Serializer.Traits。
		// 注意是 |=，不会覆盖公共 Builder 已经设置好的位。
		Serializer.Traits |= Builder.GetTraits();

		return Serializer;
	}
};

}

/**
 * 中文：声明"内部 Serializer"的宏。
 *      头文件层面与公共宏完全一致——都只是声明 SerializerName ## NetSerializerInfo
 *      结构（包含静态 Serializer 字段与若干 Get* 函数）。区别只体现在 IMPLEMENT 阶段。
 */
#define UE_NET_DECLARE_SERIALIZER_INTERNAL UE_NET_DECLARE_SERIALIZER

/**
 * 中文：实现"内部 Serializer"的宏。
 *      对比 UE_NET_IMPLEMENT_SERIALIZER 的差异：
 *        - Serializer 字段的初始化使用 TInternalNetSerializer<...>，从而走
 *          "公共 Builder + 内部 Builder"双重构造路径，最终 trait 是两者并集。
 *        - GetQuantizedTypeSize/Alignment/GetDefaultConfig 仍走 TNetSerializerBuilder，
 *          因为这些字段对内/外部 Serializer 是相同的语义（基于 Impl 的 SourceType/
 *          QuantizedType/ConfigType + DefaultConfig 静态成员推导）。
 */
#define UE_NET_IMPLEMENT_SERIALIZER_INTERNAL(SerializerName) const UE::Net::FNetSerializer SerializerName ## NetSerializerInfo::Serializer = UE::Net::Private::TInternalNetSerializer<SerializerName>::ConstructNetSerializer(TEXT(#SerializerName)); \
	uint32 SerializerName ## NetSerializerInfo::GetQuantizedTypeSize() { return UE::Net::TNetSerializerBuilder<SerializerName>::GetQuantizedTypeSize(); }; \
	uint32 SerializerName ## NetSerializerInfo::GetQuantizedTypeAlignment() { return UE::Net::TNetSerializerBuilder<SerializerName>::GetQuantizedTypeAlignment(); }; \
	const FNetSerializerConfig* SerializerName ## NetSerializerInfo::GetDefaultConfig() { return UE::Net::TNetSerializerBuilder<SerializerName>::GetDefaultConfig(); };
