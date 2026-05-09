// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Iris/Core/NetObjectReference.h"
#include "Iris/Serialization/NetSerializer.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"

namespace UE::Net
{

struct FQuantizedObjectReference;

/**
 * ENetReferenceCollectorTraits —— 引用收集器的行为位图。
 * - None：默认，只收集有效且可被导出的引用；
 * - IncludeInvalidReferences：把无效引用也纳入（用于调试或特殊场景，一般不启用）；
 * - OnlyCollectReferencesThatCanBeExported：跳过不可导出的引用（例如纯内存本地对象，远端
 *   无法解析）。收集阶段用此位避免把永远解析不了的引用写进"待导出列表"造成无穷挂起。
 */
enum class ENetReferenceCollectorTraits : uint32
{
	None								= 0U,
	IncludeInvalidReferences			= 1U,
	OnlyCollectReferencesThatCanBeExported = IncludeInvalidReferences << 1U,
};
ENUM_CLASS_FLAGS(ENetReferenceCollectorTraits);

/**
 * FNetReferenceCollector —— Iris 的"对象引用收集器"。
 *
 * 工作流程：
 *   1. `FReplicationWriter` 在写一个对象前，构造一个 FNetReferenceCollector；
 *   2. 遍历该对象的 `FReplicationStateDescriptor`，对每个引用类型成员调用其 Serializer 的
 *      `CollectNetReferences`，Serializer 内部把 `FQuantizedObjectReference` 解出，拼装成
 *      `FReferenceInfo` 调用 `Add`；
 *   3. ReplicationWriter 根据收集到的 ReferenceInfos 决定：
 *        - 哪些需要导出（加到 `FNetExportContext::PendingExport`）；
 *        - 哪些是解析依赖（保证 target connection 已 ACK 对应的 export）；
 *        - 如何把引用本身编码进 BitStream（内联 or 索引化）。
 *
 * `FReferenceInfo` 与 `FQuantizedObjectReference` 的关系：
 *   - `FQuantizedObjectReference` 是对象引用的"量化形态"（可能是本地 FNetObjectReference 或
 *     远端 FQuantizedRemoteObjectReference，二选一）；
 *   - `FReferenceInfo` 是一个"收集条目"：把 Descriptor 级别的 ResolveType（ResolveOnServer /
 *     ResolveOnClient）+ 真正的 `FNetObjectReference`（从 Quantized 中剥离得到）+ ChangeMaskInfo
 *     三者打包，供 Writer 决策。
 */
class FNetReferenceCollector
{
public:
	/** 单条引用收集记录。ReplicationWriter 把这个数组消费后决定导出/内联。 */
	struct FReferenceInfo
	{
		FNetReferenceInfo Info;                        // ResolveType（服务/客户端解析） + 其它描述级属性
		FNetObjectReference Reference;                 // 真正的对象引用（从 Quantized 剥离出来）
		FNetSerializerChangeMaskParam ChangeMaskInfo;  // 该引用所属成员的 ChangeMask 位偏移 / 位数
	};

	/** 内联 32 元素——典型对象引用数量很少，避免堆分配。超出后退化到堆分配。 */
	typedef TArray<FReferenceInfo, TInlineAllocator<32>> FReferenceInfoArray;

public:
	FNetReferenceCollector() : Traits(ENetReferenceCollectorTraits::None) {}
	explicit FNetReferenceCollector(ENetReferenceCollectorTraits InTraits) : Traits(InTraits) {}
	
	/**
	 * 追加一条引用记录。
	 * 行为（按 Traits 过滤）：
	 *   - 若 Reference 是"远程引用"（`RemoteReferencePtr != nullptr`），直接忽略（远程引用走
	 *     另一个独立的导出通路 FRemoteObjectReferenceNetSerializer）；
	 *   - 若引用 invalid 且 Traits 未设 IncludeInvalidReferences，跳过；
	 *   - 若引用 !CanBeExported 且 Traits 设置了 OnlyCollectReferencesThatCanBeExported，跳过；
	 *   - 否则把 { Info, NetReference, ChangeMaskInfo } 追加进 ReferenceInfos。
	 */
	void Add(const FNetReferenceInfo& ReferenceInfo, const FQuantizedObjectReference& Reference, const FNetSerializerChangeMaskParam& ChangeMaskInfo);

	/** 获取已收集的全部引用记录（只读视图）。由 ReplicationWriter 消费。 */
	const FReferenceInfoArray& GetCollectedReferences() const { return ReferenceInfos; }

	/** 清空列表以便复用同一个收集器处理下一个对象。 */
	void Reset() { ReferenceInfos.Reset(); }

private:
	FReferenceInfoArray ReferenceInfos;       // 收集到的全部条目
	const ENetReferenceCollectorTraits Traits;// 过滤位图，构造后不可变
};

}
