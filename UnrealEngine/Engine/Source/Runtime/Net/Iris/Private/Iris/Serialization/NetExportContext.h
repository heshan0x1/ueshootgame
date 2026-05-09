// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// =============================================================================================
// NetExportContext.h
// ---------------------------------------------------------------------------------------------
// FNetExportContext：管理"本批次导出"与"已确认导出"两态的对象/Token 引用集合。
//
// 名词解释：
//   - 引用 export = 对端首次见到某个 NetRefHandle / NetToken / NetObjectReference 时，
//     需要把它对应的"完整描述"（如对象路径、字符串字面量）一起发过去；之后只需要发 ID。
//   - 是否需要 export 的判定基于"对端已经知道哪些"，因此需要双方就"什么是 acknowledged"
//     达成共识——对应 FAcknowledgedExports。
//
// 两层数据结构（核心）：
//   1. FAcknowledgedExports（已确认）：被对端 ACK 过的导出 ID 集合，跨 batch 持久。
//      - AcknowledgedExportedHandles：FNetRefHandle 集（TSet）
//      - AcknowledgedExportedNetTokens：FNetToken 集（TSet）
//   2. FBatchExports（本批次）：当前打包中正在导出/待导出的 ID 集合，写完一个 batch 即清空。
//      - HandlesExportedInCurrentBatch  ：本批次已经"决定要发"的 NetRefHandle
//      - NetTokensExportedInCurrentBatch：本批次已经"决定要发"的 NetToken
//      - ReferencesPendingExportInCurrentBatch ：本批次"还没决定怎么发"的 FNetObjectReference
//      - NetTokensPendingExportInCurrentBatch  ：本批次"还没决定怎么发"的 FNetToken
//
// 三类 Export：
//   - FNetRefHandle ：复制对象本体的 ID（最常见）
//   - FNetToken     ：字符串/Name 等小型不变量对应的 ID
//   - FNetObjectReference：尚未确定能否解析（resolve）的引用，先放到 Pending 列表
//     等条件成熟再升级为正式 export 或继续 pending。
//
// FNetExportRollbackScope（错误回滚 RAII）：
//   - 进入作用域时记下"四个数组的当前长度"快照；
//   - 析构时若上下文进入 Error/Overflow，则把数组 SetNum 回到快照——把这次写入产生的"假
//     export 记录"统统抹掉，等价于"事务回滚"。
//   - 也可手动调用 Rollback() 显式回滚。
//
// 与文档对照：Docs/Modules/Serialization.md §1.4「引用/PackageMap」"导出/确认两态"。
// =============================================================================================

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/Set.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Net/Core/NetToken/NetToken.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Core/NetObjectReference.h"

namespace UE::Net::Private
{

/**
 * 管理"已确认 / 本批次"两层导出集合的上下文对象。
 * 每个发送 batch 构造一个，配合 FNetExportRollbackScope 用于事务式写入。
 */
class FNetExportContext
{
public:
	// ----------------------------------------------------------------------------
	// 类型别名：所有数组都用 TInlineAllocator<32>，单批典型规模在 32 以内可零堆分配。
	// ----------------------------------------------------------------------------
	/** 本批次"已决定导出"的 NetRefHandle 列表类型。 */
	typedef TArray<FNetRefHandle, TInlineAllocator<32>> FExportsArray;
	/** 本批次"已决定导出"的 NetToken 列表类型。 */
	typedef TArray<FNetToken, TInlineAllocator<32>> FNetTokenExportsArray;
	/** 本批次"待决"的 FNetObjectReference 列表类型。 */
	typedef TArray<FNetObjectReference, TInlineAllocator<32>> FPendingExportArray;
	/** 本批次"待决"的 FNetToken 列表类型。 */
	typedef TArray<FNetToken, TInlineAllocator<32>> FNetTokenPendingExportArray;

	/**
	 * 已被对端 ACK 过的导出集合。跨 batch 持久存在，由更高层（连接级）维护。
	 * 写入路径上读取以判断"是否仍需 export"；ACK 路径上写入以加入。
	 */
	struct FAcknowledgedExports
	{
		TSet<FNetRefHandle> AcknowledgedExportedHandles;
		TSet<FNetToken> AcknowledgedExportedNetTokens;
	};

	/**
	 * 本批次工作区。每个新 batch 开头由调用方调用 Reset() 清空；
	 * batch 写完后这些信息会被 promote 到上层数据流的 export header 中。
	 */
	struct FBatchExports
	{
		/** 清空全部本批次状态。每开始一个新 batch 都应当调用一次。 */
		void Reset()
		{
			HandlesExportedInCurrentBatch.Empty();
			NetTokensExportedInCurrentBatch.Empty();
			ReferencesPendingExportInCurrentBatch.Empty();
			NetTokensPendingExportInCurrentBatch.Empty();
		}

		/** 是否还有"未决定如何处理"的 pending export，需要继续解决。 */
		bool HasPendingExports() const { return (ReferencesPendingExportInCurrentBatch.Num() > 0) || (NetTokensPendingExportInCurrentBatch.Num() > 0); }

		// Exports in the current batch
		/** 本批次已确定 export 的 NetRefHandle。 */
		FExportsArray HandlesExportedInCurrentBatch;
		/** 本批次已确定 export 的 NetToken。 */
		FNetTokenExportsArray NetTokensExportedInCurrentBatch;
		/** 本批次"想 export 但暂未决定"的对象引用。 */
		FPendingExportArray ReferencesPendingExportInCurrentBatch;
		/** 本批次"想 export 但暂未决定"的 NetToken。 */
		FNetTokenPendingExportArray NetTokensPendingExportInCurrentBatch;
	};

public:

	/**
	 * 构造：把已确认表和本批次工作区"借用"进来。
	 * 注意 AcknowledgedExports 是 const&（只读），BatchExports 是 &（可写）。
	 */
	FNetExportContext(const FAcknowledgedExports& InAcknowledgedExports, FBatchExports& BatchExports);

	// Returns true if the Handle is acknowledged as delivered or if it is exported in the current batch
	/** 该 Handle 在本会话中是否已经"对端可见"——已 ACK 或本批次正在导出。 */
	bool IsExported(FNetRefHandle Handle) const;

	// Returns true if the Handle is acknowledged as delivered or if it is exported in the current batch
	/** 该 Token 是否对端可见——已 ACK 或本批次正在导出。 */
	bool IsExported(FNetToken Token) const;

	// Add a Handle to the current export batch
	/** 把一个 NetRefHandle 加入本批次"已 export"集合。 */
	void AddExported(FNetRefHandle Handle);

	// Add a Handle to the current export batch
	/** 把一个 NetToken 加入本批次"已 export"集合。 */
	void AddExported(FNetToken Token);

	// Add a reference to the current pending exports arr.
	/** 把一个对象引用加入本批次"pending"集合，等之后再决定如何 export。 */
	void AddPendingExport(const FNetObjectReference& Ref);

	// Add a NetToken to the current pending export array
	/** 把一个 NetToken 加入本批次"pending"集合。 */
	void AddPendingExport(FNetToken Token);

	// Add NetTokens to the current pending export array
	/** 批量把若干 NetToken 加入本批次"pending"集合。 */
	void AddPendingExports(TArrayView<const FNetToken> NetTokens);

	// Returns true if the Reference is in PendingExports array
	/** 引用是否仍处于 pending 状态。 */
	bool IsPendingExport(const FNetObjectReference& Ref) const;

	// Clear the list of pending exports
	/** 清空所有 pending（引用 + Token）。一般在本批次决议完成后调用。 */
	void ClearPendingExports()
	{ 
		BatchExports.ReferencesPendingExportInCurrentBatch.Empty(); 
		BatchExports.NetTokensPendingExportInCurrentBatch.Empty();
	}

	// Get current batch exports
	/** 暴露本批次工作区给上层数据流，用于把它写入 header。 */
	const FNetExportContext::FBatchExports& GetBatchExports() const { return BatchExports; }

private:
	// Rollback Scope 需要直接读写 BatchExports 来实现长度回退。
	friend class FNetExportRollbackScope;

	// Acknowledged exports
	/** 已确认导出（跨 batch 共享，只读）。 */
	const FAcknowledgedExports& AcknowledgedExports;

	// Exports for the current batch which we can treat as exported within the batch
	/** 本批次工作区（外部持有，引用进来）。 */
	FBatchExports& BatchExports;
};

// ------------------------------------------------------------------------------------------
// FNetExportRollbackScope —— "事务式"回滚
// ------------------------------------------------------------------------------------------
// 典型用法：
//     FNetExportRollbackScope Scope(Context);
//     // ... 写一段可能失败的数据；过程中可能往 BatchExports 追加若干条记录 ...
//     // 离开作用域：若 Context 进入 Error/Overflow，自动 Rollback；
//     //            否则保留所写记录。
//
// 也可显式 Scope.Rollback() 立即回滚。
// 实质：构造时记录 4 个数组的旧长度，析构/Rollback 时 SetNum 回到旧长度。
// ------------------------------------------------------------------------------------------
// Rollback scope to be able to rollback exports with bitstream
class FNetExportRollbackScope
{
public:
	/** 构造：记录 4 个数组的"快照长度"。 */
	explicit FNetExportRollbackScope(FNetSerializationContext& InContext);
	/** 析构：若 Context 处于错误态则自动调用 Rollback。 */
	~FNetExportRollbackScope();

	/** 显式回滚：把 4 个数组截断回构造时的快照长度。 */
	void Rollback();

private:
	/** 关联的序列化上下文，用于查询导出上下文与错误态。 */
	FNetSerializationContext& Context;
	/** Handle 导出数组在进入作用域时的长度。 */
	int32 StartNumNetHandleExports;
	/** Token 导出数组在进入作用域时的长度。 */
	int32 StartNumNetTokenExports;
	/** Pending 引用数组在进入作用域时的长度。 */
	int32 StartNumPendingExports;
	/** Pending Token 数组在进入作用域时的长度。 */
	int32 StartNumNetTokensPendingExports;
};

// ------------------------------------------------------------------------------------------
// 内联实现
// ------------------------------------------------------------------------------------------

inline FNetExportRollbackScope::FNetExportRollbackScope(FNetSerializationContext& InContext)
: Context(InContext)
{
	const FNetExportContext* ExportContext = Context.GetExportContext();

	// 没有 ExportContext 时四个长度全部记 0；后续 Rollback 也会被 ExportContext 为空保护住。
	StartNumNetHandleExports = ExportContext ? ExportContext->BatchExports.HandlesExportedInCurrentBatch.Num() : 0;
	StartNumNetTokenExports = ExportContext ? ExportContext->BatchExports.NetTokensExportedInCurrentBatch.Num() : 0;
	StartNumPendingExports = ExportContext ? ExportContext->BatchExports.ReferencesPendingExportInCurrentBatch.Num() : 0;
	StartNumNetTokensPendingExports = ExportContext ? ExportContext->BatchExports.NetTokensPendingExportInCurrentBatch.Num() : 0;
}

inline void FNetExportRollbackScope::Rollback()
{
	if (const FNetExportContext* ExportContext = Context.GetExportContext())
	{ 
		// 用 SetNum 把 4 个数组截断到快照长度，等价于"删除本作用域内追加的所有条目"。
		ExportContext->BatchExports.HandlesExportedInCurrentBatch.SetNum(StartNumNetHandleExports);
		ExportContext->BatchExports.NetTokensExportedInCurrentBatch.SetNum(StartNumNetTokenExports);
		ExportContext->BatchExports.NetTokensPendingExportInCurrentBatch.SetNum(StartNumNetTokensPendingExports);
		ExportContext->BatchExports.ReferencesPendingExportInCurrentBatch.SetNum(StartNumPendingExports);
	}
}

inline FNetExportRollbackScope::~FNetExportRollbackScope()
{
	// Trigger rollback if we have encountered an error
	// 析构时只在错误态下回滚——成功路径不应清除 export 记录，否则上层将丢失正常数据。
	if (Context.HasErrorOrOverflow())
	{
		Rollback();
	}
}

inline FNetExportContext::FNetExportContext(const FAcknowledgedExports& InAcknowledgedExports, FBatchExports& InBatchExports)
	: AcknowledgedExports(InAcknowledgedExports)
	, BatchExports(InBatchExports)
{
}

inline bool FNetExportContext::IsExported(FNetRefHandle Handle) const
{
	// 命中"已确认"或者"本批次中"任一即视为已导出。
	// 注：本批次列表使用线性 Find（典型规模 < 32，足够快）。
	return AcknowledgedExports.AcknowledgedExportedHandles.Contains(Handle) || (BatchExports.HandlesExportedInCurrentBatch.Find(Handle) != INDEX_NONE);
}

inline void FNetExportContext::AddExported(FNetRefHandle Handle)
{
	// 注意：不去重。调用方需要保证自身逻辑正确（一般在 IsExported 不命中时才 AddExported）。
	BatchExports.HandlesExportedInCurrentBatch.Add(Handle);
}

inline bool FNetExportContext::IsExported(FNetToken Token) const
{
	return AcknowledgedExports.AcknowledgedExportedNetTokens.Contains(Token) || (BatchExports.NetTokensExportedInCurrentBatch.Find(Token) != INDEX_NONE);
}

inline void FNetExportContext::AddExported(FNetToken Token)
{
	BatchExports.NetTokensExportedInCurrentBatch.Add(Token);
}

inline void FNetExportContext::AddPendingExport(const FNetObjectReference& Ref)
{
	// AddUnique：避免同一引用被重复挂到 pending 列表。
	BatchExports.ReferencesPendingExportInCurrentBatch.AddUnique(Ref);
}

inline void FNetExportContext::AddPendingExport(FNetToken NetToken)
{
	BatchExports.NetTokensPendingExportInCurrentBatch.AddUnique(NetToken);
}

inline void FNetExportContext::AddPendingExports(TArrayView<const FNetToken> NetTokens)
{
	for (const FNetToken& NetToken : NetTokens)
	{
		AddPendingExport(NetToken);
	}
}

inline bool FNetExportContext::IsPendingExport(const FNetObjectReference& Ref) const
{
	return BatchExports.ReferencesPendingExportInCurrentBatch.Contains(Ref);
}


}
