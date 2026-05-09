// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

// =============================================================================================
// IrisPackageMapExportUtil.h
// ---------------------------------------------------------------------------------------------
// FIrisPackageMapExportsUtil：把 FIrisPackageMapExports（PackageMap 劫持时捕获到的对象/Name/Token）
// 包装成一个"标准 NetSerializer 量化态"，以便复用 Iris 主流的 Serialize/Quantize/Dequantize
// 等接口直接发送。
//
// 使用场景：
//   - 调用某个老式 NetSerialize 时通过 UIrisObjectReferencePackageMap 捕获了若干引用/名字/Token，
//     这些"附带物"需要随主结构体的量化态一起在网络上发出去。
//   - 本工具把这些副产物压成 FIrisPackageMapExportsQuantizedType（POD 布局），
//     然后用三个独立 NetSerializer（Object / Name / NetToken）分别处理三种数据。
//
// FIrisPackageMapExportsQuantizedType 内存布局（POD）：
//   - ObjectReferenceStorage：对象引用的量化数组（ObjectNetSerializer 的 quantized 单元），
//                             TInlinedElementAllocationPolicy<MaxInlinedObjectRefs=4>。
//   - NameStorage           ：FName 的量化数组（NameAsNetTokenNetSerializer 的 quantized
//                             单元），同样 inline 4 个。
//   - NetTokenStorage       ：FNetToken 数组，inline 4 个；不是 quantized——NetToken 已经
//                             是发送态，我们只是临时存一份 pending export 列表。
//
// 注意三类数据的处理方式不同：
//   - 对象/Name：会真正 Quantize → Serialize（带二级 NetSerializer 调用）；
//   - NetToken：Quantize 阶段只是缓存到 NetTokenStorage；Serialize 阶段不写位流，
//               而是把它们 AddPendingExport 到 ExportContext，让外层导出框架统一发。
//
// 与文档对照：Docs/Modules/Serialization.md §1.4「引用/PackageMap」"PackageMapExportUtil"。
// =============================================================================================

// Note: This util is intended for internal use and should not be included or used outside of NetSerializers having to deal with calling into existing NetSerialzie functions.
 
#include "NetSerializer.h"
#include "UObject/ObjectMacros.h"
#include "Iris/Core/NetObjectReference.h"
#include "Net/Core/NetToken/NetToken.h"
#include "Iris/Serialization/NetSerializerArrayStorage.h"
#include "Iris/Serialization/ObjectNetSerializer.h"
#include "Iris/Serialization/StringNetSerializers.h"

namespace UE::Net
{
struct FIrisPackageMapExports;
class FNetReferenceCollector;

/**
 * Quantized 状态：包含三段独立的 ArrayStorage（对象、Name、Token）。
 * 标记为 POD，可被外层包含进更大的 quantized 结构里按位拷贝（CloneDynamicState 仍需走专用接口）。
 */
struct FIrisPackageMapExportsQuantizedType
{
	/** 内联存储多少条对象引用——超过则走堆分配。4 是经验典型值。 */
	static constexpr uint32 MaxInlinedObjectRefs = 4;
	/** 对象引用 quantized 单元的存储类型：FObjectNetSerializerQuantizedReferenceStorage 数组。 */
	typedef FNetSerializerArrayStorage<FObjectNetSerializerQuantizedReferenceStorage, AllocationPolicies::TInlinedElementAllocationPolicy<MaxInlinedObjectRefs>> FObjectReferenceStorage;

	/**
	 * Name 的 quantized 单元包装。把 NameNetSerializer 要求的"安全 quantized 字节数"用
	 * 8 字节对齐的 buffer 包出来，便于放进数组存储。
	 */
	struct FQuantizedName
	{
		alignas(8) uint8 Name[GetNameNetSerializerSafeQuantizedSize()];
	};

	/** 内联存储多少个 Name。 */
	static constexpr uint32 MaxInlinedNames = 4;
	/** Name quantized 数组类型。 */
	typedef FNetSerializerArrayStorage<FQuantizedName, AllocationPolicies::TInlinedElementAllocationPolicy<MaxInlinedNames>> FNamesStorage;

	/** 内联存储多少个 NetToken。 */
	static constexpr uint32 MaxInlinedNetTokens = 4;
	/** NetToken 数组类型——注意：NetToken 不需要"二次量化"，直接存原值即可。 */
	typedef FNetSerializerArrayStorage<FNetToken, AllocationPolicies::TInlinedElementAllocationPolicy<MaxInlinedNetTokens>> FNetTokenStorage;

	/** 对象引用 quantized 数据。 */
	FObjectReferenceStorage ObjectReferenceStorage;
	/** FName quantized 数据。 */
	FNamesStorage NameStorage;
	/** Pending export 的 NetToken 列表（仅在量化阶段存放，序列化阶段会被推到 ExportContext）。 */
	FNetTokenStorage NetTokenStorage;
};

}

// 标记为 POD：满足 TArray、按位复制等优化前提；具体的所有权迁移仍须走 NetSerializer 接口。
template <> struct TIsPODType<UE::Net::FIrisPackageMapExportsQuantizedType> { enum { Value = true }; };

namespace UE::Net
{

// Util to facilitate capture and export of supported types when calling into old NetSerialize() methods.
/**
 * 把 FIrisPackageMapExports 适配成"标准 NetSerializer"语义的工具集。
 * 各静态函数与 NetSerializer 的成员函数一一对应，便于在使用本 util 的 Serializer 中
 * 直接转发调用。
 */
struct FIrisPackageMapExportsUtil
{
	typedef FIrisPackageMapExportsQuantizedType QuantizedType;

	// We need some sort of limit here.
	/** 单批 export 的硬上限。防止恶意/损坏的对端发来巨大的数组导致 OOM。 */
	static constexpr uint32 MaxExports = 65536U;

	// Matches NetSerializer functions
	/** 序列化：把 quantized 内的对象/Name 写入位流；NetToken 不写流而是推到 ExportContext。 */
	IRISCORE_API static void Serialize(FNetSerializationContext& Context, const QuantizedType& Value);
	/** 反序列化：从位流读出对象/Name 写回 quantized；NetToken 不在此读取。 */
	IRISCORE_API static void Deserialize(FNetSerializationContext& Context, QuantizedType& Value);
	/** 量化：把高层 FIrisPackageMapExports + pending NetToken 列表压成 quantized。 */
	IRISCORE_API static void Quantize(FNetSerializationContext& Context, const UE::Net::FIrisPackageMapExports& PackageMapExports, TArrayView<const UE::Net::FNetToken> NetTokensPendingExport, QuantizedType& Target);
	/** 反量化：从 quantized 还原回 FIrisPackageMapExports。 */
	IRISCORE_API static void Dequantize(FNetSerializationContext& Context, const QuantizedType& Source, UE::Net::FIrisPackageMapExports& PackageMapExports);
	/** 比较：用于变更检测。 */
	IRISCORE_API static bool IsEqual(FNetSerializationContext& Context, const QuantizedType& Value0, const QuantizedType& Value1);
	/** 深拷贝动态状态（数组重分配 + 元素 Clone）。 */
	IRISCORE_API static void CloneDynamicState(FNetSerializationContext& Context, QuantizedType& Target, const QuantizedType& Source);
	/** 释放动态状态（带 Context 版本，使用 InternalContext::Free）。 */
	IRISCORE_API static void FreeDynamicState(FNetSerializationContext& Context, QuantizedType& Value);
	/** 释放动态状态（不带 Context 的 fallback；构造一个临时 Context 并退化为 FMemory::Free）。 */
	IRISCORE_API static void FreeDynamicState(QuantizedType& Value);
	/** 收集对象引用：把 ObjectReferenceStorage 中的引用上报给 NetReferenceCollector。 */
	IRISCORE_API static void CollectNetReferences(FNetSerializationContext& Context, const QuantizedType& Value, const FNetSerializerChangeMaskParam& ChangeMaskInfo, FNetReferenceCollector& Collector);
	/** 校验：上限保护，防止过大的数组通过反序列化"复活"到正常路径上。 */
	IRISCORE_API static bool Validate(FNetSerializationContext&, const QuantizedType& Value);

private:

	/** 对象引用使用的二级 NetSerializer。一次性常量初始化。 */
	static const FNetSerializer* ObjectNetSerializer;
	/** Name 使用的二级 NetSerializer：FNameAsNetTokenNetSerializer（即 Name 也走 Token 化）。
	 *  cpp 中可见已注释掉的备选实现 FNameNetSerializer。 */
	static const FNetSerializer* NameNetSerializer;
};

}
