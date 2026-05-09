// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：NetRefHandle.cpp
// 角色：FNetRefHandle 的字符串化与 FArchive 序列化实现。
//
// 关键约定：
//   * ToString / ToCompactString 在 IsCompleteHandle 时同时输出 Id + RepSystemId，
//     否则输出 "?" 表示当前句柄尚未绑定到具体 RS（多见于尚未路由完成的中间状态）。
//   * 序列化重载（FArchive）仅持久化 1 bit IsValid + 64-bit Packed Id，
//     ReplicationSystemId 不上线、由接收端通过 FNetRefHandleManager 重建：
//        MakeNetRefHandleFromId(NetId) -> 返回完整 handle（RS Id 由当前进程上下文补全）。
//   * 这套实现使 NetRefHandle 可在跨连接时保持紧凑（仅 Id 上线），
//     且在 PIE 多 RS 进程内正确路由至对应实例。
// =====================================================================================

#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"
#include "Containers/UnrealString.h"
#include "Misc/StringBuilder.h"

namespace UE::Net
{

// 详细字串：完整句柄输出 Id 和 RepSystemId；不完整时 RepSystemId 显示为 "?"。
FString FNetRefHandle::ToString() const
{
	if (IsCompleteHandle())
	{
		const uint32 ReplicationSystemIdToDisplay = GetReplicationSystemId();
		return FString::Printf(TEXT("NetRefHandle (Id=%" UINT64_FMT "):(RepSystemId=%u)"), GetId(), ReplicationSystemIdToDisplay);
	}
	else
	{
		return FString::Printf(TEXT("NetRefHandle (Id=%" UINT64_FMT "):(RepSystemId=?)"), GetId());
	}
}

// 紧凑字串：日志中以 "Id:RepSystemId" 形式呈现，节省空间。
FString FNetRefHandle::ToCompactString() const
{
	if (IsCompleteHandle())
	{
		const uint32 ReplicationSystemIdToDisplay = GetReplicationSystemId();
		return FString::Printf(TEXT("%" UINT64_FMT ":%u"), GetId(), ReplicationSystemIdToDisplay);
	}
	else
	{
		return FString::Printf(TEXT("%" UINT64_FMT ":?"), GetId());
	}
}

}

// FStringBuilderBase 重载：与 ToString 等价，但避免一次堆分配（高频日志友好）。
FStringBuilderBase& operator<<(FStringBuilderBase& Builder, const UE::Net::FNetRefHandle& NetRefHandle) 
{ 	
	if (NetRefHandle.IsCompleteHandle())
	{
		const uint32 ReplicationSystemIdToDisplay = NetRefHandle.GetReplicationSystemId();
		Builder.Appendf(TEXT("NetRefHandle (Id=%" UINT64_FMT "):(RepSystemId=%u)"), NetRefHandle.GetId(), ReplicationSystemIdToDisplay);
	}
	else
	{
		Builder.Appendf(TEXT("NetRefHandle (Id=%" UINT64_FMT "):(RepSystemId=?)"), NetRefHandle.GetId());
	}

	return Builder;
}

// AnsiStringBuilder 版本：用于不需要 TCHAR 的纯 ASCII 输出场合（NetTrace / 性能日志）。
FAnsiStringBuilderBase& operator<<(FAnsiStringBuilderBase& Builder, const UE::Net::FNetRefHandle& NetRefHandle)
{
	if (NetRefHandle.IsCompleteHandle())
	{
		const uint32 ReplicationSystemIdToDisplay = NetRefHandle.GetReplicationSystemId();
		Builder.Appendf("NetRefHandle (Id=%" UINT64_FMT "):(RepSystemId=%u)", NetRefHandle.GetId(), ReplicationSystemIdToDisplay);
	}
	else
	{
		Builder.Appendf("NetRefHandle (Id=%" UINT64_FMT "):(RepSystemId=?)", NetRefHandle.GetId());
	}

	return Builder;
}

// FArchive 序列化：保存端 -> 写 1 bit IsValid + Packed64 Id；
// 加载端 -> 读出后通过 NetRefHandleManager 重建出本地完整 handle（补 RepSystemId）。
// 注意：Save 路径不写入 ReplicationSystemId，因为对端的 RS Id 与本端无意义；
//       Load 路径必须由当前 RS 上下文补全，从而保证 PIE 多 RS 不会路由错乱。
FArchive& operator<<(FArchive& Ar, UE::Net::FNetRefHandle& RefHandle)
{
	using namespace UE::Net;

	if (Ar.IsSaving())
	{
		bool bIsValid = RefHandle.IsValid();
		Ar.SerializeBits(&bIsValid, 1U);
		if (bIsValid)
		{
			uint64 IdBits = RefHandle.GetId();
			Ar.SerializeIntPacked64(IdBits);
		}
	}
	else if (Ar.IsLoading())
	{
		FNetRefHandle Handle;

		bool bIsValid = false;
		Ar.SerializeBits(&bIsValid, 1U);
		if (bIsValid)
		{
			uint64 NetId = 0U;
			Ar.SerializeIntPacked64(NetId);		
			if (!Ar.IsError())
			{
				// 由 Manager 决定如何把外部上线的 Id 映射回本地完整 handle；
				// 通常仅填充 Id 字段（RS Id 维持 0，由后续 Resolve 阶段补全）。
				Handle = Private::FNetRefHandleManager::MakeNetRefHandleFromId(NetId);
			}
		}

		RefHandle = Handle;
	}
	return Ar;
}
