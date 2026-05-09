// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringFwd.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Net/Core/Trace/NetDebugName.h"

class UReplicationSystem;

// ---------------------------------------------------------------------------------------------
// UE_NET_ENABLE_READ_JOURNAL —— 是否启用读 Journal（环形 32 项缓冲）。
//   - 默认在所有构建（含 Shipping）都开启：Journal 占用极小、对性能影响小，但在出现 crash 或
//     Payload 解码错误时能提供"最近读取路径"以定位到具体 Serializer 与对象。
//   - 若上层希望在极致精简构建下关闭此特性，可在全局覆盖此宏为 0，`UE_ADD_READ_JOURNAL_ENTRY`
//     将被替换成空实现。
// ---------------------------------------------------------------------------------------------
#ifndef UE_NET_ENABLE_READ_JOURNAL
#if (UE_BUILD_SHIPPING)
#	define UE_NET_ENABLE_READ_JOURNAL 1
#else
#	define UE_NET_ENABLE_READ_JOURNAL 1
#endif 
#endif

#if UE_NET_ENABLE_READ_JOURNAL
	/** Add entry to the read journal. The Name must be a static/permanently allocated string as the raw pointer will be stored. */
	/**
	 * 向读 Journal 追加一条记录（记录当前 BitStream 读位置 + Name + 当前 ErrorContext 的对象句柄）。
	 * 要求：Name 必须是静态/永久存在的字符串（宏内部只保存原始指针，不做拷贝）。
	 * 典型放置点：Deserialize/DeserializeDelta 开头、复杂子流进入前。
	 */
	#define UE_ADD_READ_JOURNAL_ENTRY(SerializationContext, Name) SerializationContext.AddReadJournalEntry(Name);
	/** 清空 Journal：新 packet/batch 的开头调用，避免跨 batch 干扰。 */
	#define UE_RESET_READ_JOURNAL(SerializationContext) SerializationContext.ResetReadJournal();
#else
	#define UE_ADD_READ_JOURNAL_ENTRY(...)
#define UE_RESET_READ_JOURNAL(...)
#endif

namespace UE::Net
{
// Simple journal to track last few entries of read data
/**
 * FNetJournal —— 环形缓冲（固定 32 条），记录反序列化流程中的"最近读取轨迹"。
 *
 * 设计：
 *   - 容量 JournalSize = 32，JournalMask = 31；写入时使用 `NumEntries & JournalMask` 回卷。
 *   - NumEntries 单调递增，用于打印时推导出"最早的一条"起始索引。
 *   - Entries[i] = (Name, NetRefHandle, BitOffset)；Name 仅保存指针（要求静态生命周期）。
 *
 * 使用场景：
 *   - 客户端反序列化出现 crash / ensure / 包结构错误时，`FNetSerializationContext::PrintReadJournal`
 *     会 dump 最近 32 条读取记录（包含发生错误的 Actor 与当时的位偏移）。
 *   - 该 dump 在 ReplicationSystem 的错误日志里输出，便于找到"哪个成员的哪个 Serializer"触发。
 */
class FNetJournal
{
	static constexpr uint32 JournalSize = 32U; // 环形缓冲容量（必须是 2 的幂）
	static constexpr uint32 JournalMask = JournalSize - 1U; // 回卷用位掩码

public:
	/** 清空 Journal（NumEntries=0，Entries 内容保留但不再可见）。 */
	void Reset() { NumEntries = 0U; }
	/** Add entry to the journal. Only the last 32 entries are stored. The Name must be a static/permanently allocated string as the raw pointer will be stored. */
	/**
	 * 追加一条记录。仅保留最近 32 条。
	 * @param Name         静态字符串（只存指针）
	 * @param BitOffset    当前 BitStreamReader 位偏移
	 * @param NetRefHandle 当前触发错误/读取的对象句柄
	 */
	void AddEntry(const TCHAR* Name, uint32 BitOffset, FNetRefHandle NetRefHandle);
	/**
	 * 打印最近 N (≤32) 条记录的可读文本；若传入 ReplicationSystem，将用 NetRefHandleManager 
	 * 解析出对象的调试名称。返回多行字符串，常见形态：
	 *   ErrorContext:
	 *   Foo::PropertyBar [NetRefHandle 0x0000000000005]
	 *   0: - BitOffset: 128: MyNetSerializer
	 *   1: - BitOffset: 161: InnerStructSerializer
	 *   ...
	 */
	FString Print(const UReplicationSystem* ReplicationSystem) const;

private:
	/** 单条 Journal 记录；POD 便于直接环形填充。 */
	struct FJournalEntry
	{
		const TCHAR* Name;       // 调用点标签（静态字符串）
		FNetRefHandle NetRefHandle; // 当前对象句柄
		uint32 BitOffset;        // 写入时刻的 Reader 位偏移
	};
	FJournalEntry Entries[JournalSize]; // 环形缓冲
	uint32 NumEntries = 0U;             // 总追加数（单调递增，用于回绕索引）
};

inline void FNetJournal::AddEntry(const TCHAR* Name, uint32 BitOffset, FNetRefHandle NetRefHandle)
{ 
	// 直接覆盖环形槽位；若 NumEntries ≥ JournalSize，最旧的一条会被冲掉
	Entries[NumEntries & JournalMask] = FJournalEntry({Name, NetRefHandle, BitOffset});
	++NumEntries;
}

}
