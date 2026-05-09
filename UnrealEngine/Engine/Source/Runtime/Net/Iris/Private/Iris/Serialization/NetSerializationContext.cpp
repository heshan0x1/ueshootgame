// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// NetSerializationContext.cpp
// ---------------------------------------------------------------------------------------------
// FNetSerializationContext：贯穿整个 Iris 序列化调用链的"会话级上下文"实现。
//
// 角色：
//   - 所有 NetSerializer 的 Serialize/Deserialize/Quantize/Dequantize 等函数第一参数都是它，
//     避免向下层透传一长串散装指针；
//   - 持有当前 BitStream 读写器、错误上下文（FNetErrorContext）、读侧诊断日志（FNetJournal）、
//     私有的 InternalContext（用于隐藏内部依赖如 ReplicationSystem / ObjectReferenceCache 等）。
//   - 大多数字段已在头文件 inline；本 cpp 实现的是需要访问其它子系统（位流/复制系统/Token Store）
//     的薄包装函数。
//
// 与文档对照：Docs/Modules/Serialization.md §1.3「上下文/错误/日志」。
// =============================================================================================

#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"

namespace UE::Net
{

// ------------------------------------------------------------------------------------------
// IsBitStreamOverflown
// ------------------------------------------------------------------------------------------
// 判断当前会话中"读 OR 写"任一侧位流是否越界。
// 注意：读、写两个流可能同时存在（罕见），也可能只挂其中一个，nullptr 视为未越界。
// ------------------------------------------------------------------------------------------
bool FNetSerializationContext::IsBitStreamOverflown() const
{
	if (BitStreamReader != nullptr && BitStreamReader->IsOverflown())
	{
		return true;
	}

	if (BitStreamWriter != nullptr && BitStreamWriter->IsOverflown())
	{
		return true;
	}

	return false;
}

// ------------------------------------------------------------------------------------------
// AddReadJournalEntry —— 字面量字符串版本
// ------------------------------------------------------------------------------------------
// 在反序列化时打一条诊断点。三个细节：
//   1. 仅当上下文当前未处于 Error/Overflow 时才追加，避免错误后被无意义条目刷掉根因；
//   2. 记录 BitStreamReader 当前的位偏移，可与对端的写入轨迹对照对齐到出错字段；
//   3. 取 ErrorContext.GetObjectHandle() 把"当前在解哪个对象"一并落到条目里，
//      Print() 时可按对象分组。
// ------------------------------------------------------------------------------------------
void FNetSerializationContext::AddReadJournalEntry(const TCHAR* Name)
{
	if (!HasErrorOrOverflow())
	{
		ReadJournal.AddEntry(Name, BitStreamReader->GetPosBits(), ErrorContext.GetObjectHandle());
	}
}

// ------------------------------------------------------------------------------------------
// AddReadJournalEntry —— FNetDebugName 版本
// ------------------------------------------------------------------------------------------
// 与上面字面量版本等价，差别只是入参类型。FNetDebugName 是 Iris 自己的"轻量调试名"
// 容器（在 Shipping 下可能编译为占位空字符串以省内存）。
// ------------------------------------------------------------------------------------------
void FNetSerializationContext::AddReadJournalEntry(const FNetDebugName* DebugName)
{
	if (!HasErrorOrOverflow())
	{
		ReadJournal.AddEntry(DebugName->Name, BitStreamReader->GetPosBits(), ErrorContext.GetObjectHandle());
	}
}

// ------------------------------------------------------------------------------------------
// PrintReadJournal
// ------------------------------------------------------------------------------------------
// 把读侧诊断日志格式化为人类可读字符串。如果 InternalContext 不存在或没有
// ReplicationSystem，则只打 Handle ID 而不查对象名。
// ------------------------------------------------------------------------------------------
FString FNetSerializationContext::PrintReadJournal()
{
	return ReadJournal.Print(InternalContext ? InternalContext->ReplicationSystem : nullptr);
}

// ------------------------------------------------------------------------------------------
// SetBitStreamOverflow
// ------------------------------------------------------------------------------------------
// 主动把位流标记为"溢出"。常见用法：上层逻辑校验失败后想立即让位流回到无效态，
// 阻断后续解码。如果两侧都挂了 reader/writer，会同时打标。
// ------------------------------------------------------------------------------------------
void FNetSerializationContext::SetBitStreamOverflow()
{
	if (BitStreamReader != nullptr && !BitStreamReader->IsOverflown())
	{
		BitStreamReader->DoOverflow();
	}

	if (BitStreamWriter != nullptr && !BitStreamWriter->IsOverflown())
	{
		BitStreamWriter->DoOverflow();
	}
}

// ------------------------------------------------------------------------------------------
// GetLocalConnectionUserData
// ------------------------------------------------------------------------------------------
// 在某些自定义 NetSerializer 里需要拿到"目标连接绑定的用户数据 UObject*"，
// 一般是 PlayerState/Controller 或游戏自定义的 ConnectionDriver。
//
// 防御式编程：任一前置条件不满足即返回 nullptr——
//   * InternalContext 未设置；
//   * ReplicationSystem 不存在；
//   * 传入的 ConnectionId 非法。
// ------------------------------------------------------------------------------------------
UObject* FNetSerializationContext::GetLocalConnectionUserData(uint32 ConnectionId)
{
	if (InternalContext == nullptr)
	{
		return nullptr;
	}

	const UReplicationSystem* ReplicationSystem = InternalContext->ReplicationSystem;
	if (ReplicationSystem == nullptr)
	{
		return nullptr;
	}

	if (ConnectionId == UE::Net::InvalidConnectionId)
	{
		return nullptr;
	}

	UObject* UserData = ReplicationSystem->GetConnectionUserData(ConnectionId);
	return UserData;
}

// ------------------------------------------------------------------------------------------
// GetNetTokenStore (const) —— 本端 NetToken 存储
// ------------------------------------------------------------------------------------------
// NetToken 用于把 FName / 字符串等"重复出现的小数据"压成 ID。本端 Store 既负责生成
// （写入侧）也负责按 ID 反查（持久层）。
// ------------------------------------------------------------------------------------------
const FNetTokenStore* FNetSerializationContext:: GetNetTokenStore() const
{
	if (InternalContext == nullptr)
	{
		return nullptr;
	}

	const UReplicationSystem* ReplicationSystem = InternalContext->ReplicationSystem;

	return ReplicationSystem ? ReplicationSystem->GetNetTokenStore() : nullptr;
}

// ------------------------------------------------------------------------------------------
// GetNetTokenStore (non-const)
// ------------------------------------------------------------------------------------------
// 与 const 版本对偶；非常量入口主要用于发送端为新数据创建 Token。
// ------------------------------------------------------------------------------------------
FNetTokenStore* FNetSerializationContext::GetNetTokenStore()
{
	if (InternalContext == nullptr)
	{
		return nullptr;
	}

	UReplicationSystem* ReplicationSystem = InternalContext->ReplicationSystem;

	return ReplicationSystem ? ReplicationSystem->GetNetTokenStore() : nullptr;
}

// ------------------------------------------------------------------------------------------
// GetRemoteNetTokenStoreState
// ------------------------------------------------------------------------------------------
// 拿到"对端"已知的 NetToken 状态——这是用来判定某个 Token 是否需要随包导出的关键依据。
// 由 InternalContext.ResolveContext 持有；为空时返回 nullptr。
// ------------------------------------------------------------------------------------------
const UE::Net::FNetTokenStoreState* FNetSerializationContext::GetRemoteNetTokenStoreState() const
{
	return InternalContext ? InternalContext->ResolveContext.RemoteNetTokenStoreState : nullptr;
}


}
