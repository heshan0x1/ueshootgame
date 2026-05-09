// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// NetReferenceCollector.cpp
// ---------------------------------------------------------------------------------------------
// FNetReferenceCollector：在量化（Quantize）阶段沿协议树向下"收割"所有 UObject 引用。
//
// 应用场景：
//   - 复制系统在打 baseline / 增量 / Filtering / Prioritization 时，需要知道某个量化态
//     里携带了哪些对象引用，以便：
//       * 确保被引用对象先于本对象被复制（依赖排序）；
//       * 决定哪些对象需要"挂起"等待对端 Ack 后再发引用；
//       * 在 Garbage Collection / Tear-Off 时维持引用图的正确。
//   - 各 NetSerializer（Object/Soft/Array/Struct/InstancedStruct/PackageMapExports 等）在自己
//     的 CollectNetReferences 实现里调用本类的 Add()，把内部存放的 FQuantizedObjectReference
//     上报到外层 Collector。
//
// 过滤规则（参见 Add 中的几个 early-return）：
//   - 远程引用（FQuantizedRemoteObjectReference）不参与本机引用图，直接丢弃；
//   - 无效引用：除非 Traits 里显式打开 IncludeInvalidReferences，否则不收集；
//   - 不可导出引用（CanBeExported() == false）：当 Traits 设了
//     OnlyCollectReferencesThatCanBeExported 时也丢弃。
//
// 与文档对照：Docs/Modules/Serialization.md §1.4「引用/PackageMap」"引用收集"。
// =============================================================================================

#include "Iris/Serialization/NetReferenceCollector.h"

#include "Iris/Serialization/ObjectNetSerializer.h"
#include "Iris/Serialization/QuantizedObjectReference.h"

namespace UE::Net
{

// ------------------------------------------------------------------------------------------
// FNetReferenceCollector::Add
// ------------------------------------------------------------------------------------------
// 把一条来自 NetSerializer 的引用添加到收集器内部数组。三类过滤逻辑见上方头注。
// 入参：
//   - ReferenceInfo：对端解析方式（ResolveOnClient/ResolveServerOnly 等）等元信息；
//   - Reference    ：量化态的 FQuantizedObjectReference（本地引用 vs 远程引用二态合一）；
//   - ChangeMaskInfo：所属 ChangeMask 的 (BitOffset, BitCount)，便于上层把"引用变化"
//     映射到"哪些字段脏了"。
// ------------------------------------------------------------------------------------------
void FNetReferenceCollector::Add(const FNetReferenceInfo& ReferenceInfo, const FQuantizedObjectReference& Reference, const FNetSerializerChangeMaskParam& ChangeMaskInfo)
{
	// 远程对象引用走另一条路径（FRemoteObjectId 流），本地引用图不需要追踪。
	if (Reference.IsRemoteReference())
	{
		return;
	}

	const FNetObjectReference& NetReference = Reference.NetReference;

	// 默认丢弃无效引用（如未注册的 UObject*），除非 Traits 显式声明要保留。
	// 保留无效引用的典型场景：调试期希望看到所有占位项，或某些诊断/统计路径。
	if (!NetReference.IsValid() && !EnumHasAnyFlags(Traits, ENetReferenceCollectorTraits::IncludeInvalidReferences))
	{
		return;
	}

	// 当调用方只关心"将来会被导出到对端的引用"时（OnlyCollectReferencesThatCanBeExported），
	// 跳过那些注定无法导出的引用（例如纯 Editor / Transient 对象）。
	if (!NetReference.CanBeExported() && EnumHasAnyFlags(Traits, ENetReferenceCollectorTraits::OnlyCollectReferencesThatCanBeExported))
	{
		return;
	}

	// 通过 FReferenceInfo 把 (元信息 / 引用 / ChangeMask 位置) 三元组打包后追加。
	FReferenceInfo RefInfo;
	RefInfo.Info = ReferenceInfo;
	RefInfo.Reference = NetReference;
	RefInfo.ChangeMaskInfo = ChangeMaskInfo;

	// MoveTemp 避免再做一次深拷贝（FReferenceInfo 内部含字符串/数组等可移构件）。
	ReferenceInfos.Add(MoveTemp(RefInfo));
}

}
