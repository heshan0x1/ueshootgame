// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// NetJournal.cpp
// ---------------------------------------------------------------------------------------------
// "读侧诊断日志"实现：Iris 反序列化路径上沿途打点的环形缓冲，出错时把最近的 N 条 Trace
// 打印出来，帮助定位"位流到底是在解哪个字段时崩的"。
//
// 关键事实：
//   - 容量固定为 32（详见头文件 NetJournal.h 中 `JournalSize` 常量），这是经验值——
//     大部分错误根因都在最近的几次解码内；过大会让缓冲在每次 deserialize 中占用过多 cache。
//   - 每条记录三元组：(字段名 / BitStream 当前位偏移 / 当前正在解的 NetRefHandle)；
//     其中 NetRefHandle 用于把多个连续条目"按对象分组"打印。
//   - 写入入口由宏 `UE_ADD_READ_JOURNAL_ENTRY(Ctx, Name)` 提供（仅在开关启用时展开），
//     这样可以在 Shipping 等极致裁剪构建中整体编译期屏蔽，零开销。
//   - 只有当上下文未处于 Error/Overflow 时才会写入，避免错误后再追加无意义的污染条目。
//
// 与文档对照：Docs/Modules/Serialization.md §1.3 末尾"NetJournal 排错日志"。
// =============================================================================================

#include "Iris/Serialization/NetJournal.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"

namespace UE::Net
{

// ------------------------------------------------------------------------------------------
// FNetJournal::Print
// ------------------------------------------------------------------------------------------
// 将环形缓冲中的最近条目"按时间顺序、按对象分组"格式化为可读文本。
//
// 实现细节：
//   - StoredCount = min(NumEntries, JournalSize)：写入次数可能远大于容量，但只能取出最近一圈；
//   - StartIndex = NumEntries - StoredCount：从最早未被覆盖的那条开始打印；
//   - 通过 (StartIndex + i) & JournalMask 进行回卷索引（前提：JournalSize 是 2 的幂）；
//   - 当连续若干条属于同一个 NetRefHandle 时，对象名只打一次，下面用缩进格式列字段——
//     这样输出对人更友好，定位"是哪个对象哪个字段"一目了然。
//
// 入参 ReplicationSystem 可以为 nullptr：在没有完整的 ReplicationSystem 上下文时会退化为
// 直接打印 NetRefHandle 自身的 ToString，不再查找对象名。
// ------------------------------------------------------------------------------------------
FString FNetJournal::Print(const UReplicationSystem* ReplicationSystem) const
{
	using namespace UE::Net::Private;

	// 通过 ReplicationSystem 拿到对象表（可空），用于把 NetRefHandle 翻译成更友好的对象名。
	const FNetRefHandleManager* NetRefHandleManager = ReplicationSystem ? &ReplicationSystem->GetReplicationSystemInternal()->GetNetRefHandleManager() : nullptr;

	FString Result;

	// 计算实际可读取的条目数量（NumEntries 可能远超容量，环形缓冲已覆盖旧的）。
	const uint32 StoredCount = FMath::Min(NumEntries, JournalSize);
	// 写入是 NumEntries 单调递增 & Mask；读出最早一条的位置 = NumEntries - StoredCount。
	const uint32 StartIndex = (NumEntries - StoredCount);

	Result.Appendf(TEXT("ErrorContext:\n"));

	// LastNetRefHandle 用于"对象切换检测"：相邻条目同对象时，对象名只打印一次。
	FNetRefHandle LastNetRefHandle;
	for (uint32 EntryIt = 0U; EntryIt < StoredCount; ++EntryIt)
	{
		// 通过位与 Mask 实现环形回卷。
		const FJournalEntry& Entry = Entries[(StartIndex + EntryIt) & JournalMask];

		// 检测到对象切换：先输出对象标题，再继续输出该对象下的字段条目。
		if (LastNetRefHandle != Entry.NetRefHandle)
		{
			Result.Appendf(TEXT("%s\n"), NetRefHandleManager ? *NetRefHandleManager->PrintObjectFromNetRefHandle(Entry.NetRefHandle) : *Entry.NetRefHandle.ToString());
			LastNetRefHandle = Entry.NetRefHandle;
		}

		// 单条输出：序号 + BitStream 位偏移 + 字段名。
		Result.Appendf(TEXT("%u: - BitOffset: %u:%s\n"), EntryIt, Entry.BitOffset, Entry.Name);		
	}
	return Result;
}

}
