// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// QuantizedObjectReference.cpp
// ---------------------------------------------------------------------------------------------
// FQuantizedObjectReference 的非内联实现：相等比较、合法性检查、远程引用释放、字符串化。
// 详细职责说明见同名头文件。
// =============================================================================================

#include "Iris/Serialization/QuantizedObjectReference.h"

#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetSerializer.h"
#include "Iris/Serialization/QuantizedRemoteObjectReference.h"
#include "Iris/Serialization/RemoteObjectReferenceNetSerializer.h"

namespace UE::Net
{

// ------------------------------------------------------------------------------------------
// operator==
// ------------------------------------------------------------------------------------------
// 同态比较：
//   - 两侧都是本地态  ➜ 比较 NetReference；
//   - 两侧都是远程态  ➜ 比较 *RemoteReferencePtr；
//   - 一本地一远程    ➜ 视为不等（即使指向"同一个对象"也不会自动跨态匹配，
//                     因为这两种引用语义和解析路径都不一样）。
// ------------------------------------------------------------------------------------------
bool FQuantizedObjectReference::operator==(const FQuantizedObjectReference& RHS) const
{
	if (!IsRemoteReference() && !RHS.IsRemoteReference())
	{
		return NetReference == RHS.NetReference;
	}
	else if (IsRemoteReference() && RHS.IsRemoteReference())
	{
		return *RemoteReferencePtr == *RHS.RemoteReferencePtr;
	}

	return false;
}

// ------------------------------------------------------------------------------------------
// IsValid
// ------------------------------------------------------------------------------------------
// 远程态：检查 RemoteReferencePtr 指向的对象（ObjectId/ServerId 不为 0）；
// 本地态：检查 NetReference.IsValid()。
// ------------------------------------------------------------------------------------------
bool FQuantizedObjectReference::IsValid() const
{
	if (IsRemoteReference())
	{
		return RemoteReferencePtr->IsValid();
	}
	else
	{
		return NetReference.IsValid();
	}
}

// ------------------------------------------------------------------------------------------
// FreeRemoteReference
// ------------------------------------------------------------------------------------------
// 释放远程引用占用的动态内存。两步走：
//   1) 调用 FRemoteObjectReferenceNetSerializer 的 FreeDynamicState：让 NetSerializer 自己
//      去清理它在远程态结构体里挂的内嵌动态状态（如 QuantizedPathName 内部的可变长字符串）；
//   2) 调用 InternalContext->Free 归还 RemoteReferencePtr 自身这块内存。
//
// 在本地态（RemoteReferencePtr 为空）调用是 no-op，这一点对调用方很重要——
// SetNetReference 在覆写前会无条件调用本函数。
// ------------------------------------------------------------------------------------------
void FQuantizedObjectReference::FreeRemoteReference(FNetSerializationContext& Context, const FNetSerializerBaseArgs& Args)
{
	if (RemoteReferencePtr)
	{
		// 步骤 1：让 RemoteObjectReferenceNetSerializer 释放其内部动态状态。
		FNetFreeDynamicStateArgs InternalArgs;
		InternalArgs.ChangeMaskInfo = Args.ChangeMaskInfo;
		InternalArgs.Version = Args.Version;
		InternalArgs.NetSerializerConfig = UE_NET_GET_SERIALIZER_DEFAULT_CONFIG(FRemoteObjectReferenceNetSerializer);
		InternalArgs.Source = NetSerializerValuePointer(RemoteReferencePtr);
		UE_NET_GET_SERIALIZER(FRemoteObjectReferenceNetSerializer).FreeDynamicState(Context, InternalArgs);

		// 步骤 2：归还远程引用结构体本身的内存，并把指针清空回到本地态。
		Context.GetInternalContext()->Free(RemoteReferencePtr);
		RemoteReferencePtr = nullptr;
	}
}

// ------------------------------------------------------------------------------------------
// ToString
// ------------------------------------------------------------------------------------------
// 调试用：
//   - 本地态：直接 NetReference.ToString()；
//   - 远程态：构造一个临时 FRemoteObjectId 并加 "Remote:" 前缀，方便日志区分。
// ------------------------------------------------------------------------------------------
FString FQuantizedObjectReference::ToString() const
{
	if (IsNetReference())
	{
		return NetReference.ToString();
	}
	else
	{
		const FRemoteObjectId TempId = FRemoteObjectId::CreateFromInt(RemoteReferencePtr->ObjectId);
		return FString::Printf(TEXT("Remote: %s"), *TempId.ToString());
	}
}

}
