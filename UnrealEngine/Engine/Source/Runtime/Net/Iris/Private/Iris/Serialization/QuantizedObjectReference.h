// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

// =============================================================================================
// QuantizedObjectReference.h
// ---------------------------------------------------------------------------------------------
// FQuantizedObjectReference：对象引用在"Quantized 状态"下的二态合一存储。
//
// 背景：
//   - Iris 的对象引用有两种语义：
//       1) 本地（FNetObjectReference）：在本进程或本服务器集群内可解析的引用，最常见；
//       2) 远程（FQuantizedRemoteObjectReference）：跨服务器/Mesh 场景，用 RemoteObjectId
//          + ServerId + 路径名 三元组定位对象。仅在 UE_WITH_REMOTE_OBJECT_HANDLE 启用时存在。
//   - 二态合一通过指针非空/为空区分：
//       * RemoteReferencePtr == nullptr  ➜  使用 NetReference（本地）
//       * RemoteReferencePtr != nullptr  ➜  使用 *RemoteReferencePtr（远程）
//   - 远程引用结构体较大（>= 48 字节），但绝大多数 NetSerializer 用例都是本地引用，
//     所以远程引用通过"动态分配 + 指针"懒占空间，避免每个引用都付远程态的内存代价。
//
// 注意（重要）：
//   - 这个文件标记为 internal use only，不应在外部包含。它出现在 Private/ 目录，所有访问
//     都通过友好的高层 NetSerializer API 进行。
//   - TIsPODType 在文件末尾被 specialize 为 true：调用方可放心 memcpy / memcmp，
//     但是务必记得：当 RemoteReferencePtr 非空时，clone/free 必须经过 FreeRemoteReference
//     再分配，不能简单 memcpy（指针拷贝会双重释放）。
//
// 与文档对照：Docs/Modules/Serialization.md §1.4「引用/PackageMap」"量化对象引用"。
// =============================================================================================

// Note: This is intended for internal use and should not be included or used outside of NetSerializers having to deal with object references.
 
#include "Iris/Core/NetObjectReference.h"
#include "Templates/IsPODType.h"

namespace UE::Net
{

class FNetSerializationContext;
struct FNetSerializerBaseArgs;

struct FQuantizedRemoteObjectReference;

// The quantized state can hold either an FNetObjectReference or a FQuantizedRemoteObjectReference.
// The FQuantizedRemoteObjectReference is stored as dynamic state in order to reduce required size
// when it's not used.
// If RemoteReferencePtr is null, this state is using the FNetObjectReference.
// If RemoteReferencePtr is non-null, this state is using the remote reference.
/**
 * 二态合一的对象引用 quantized 表示。详见文件头注。
 */
struct FQuantizedObjectReference
{
	// Assign an FNetObjectReference to this state. Called from within serializer functions.
	/**
	 * 把一个本地引用赋给本量化态。先把可能持有的 RemoteReference 释放掉，再写入 NetReference。
	 * 仅供 NetSerializer 内部调用。
	 */
	void SetNetReference(FNetSerializationContext& Context, const FNetSerializerBaseArgs& Args, const FNetObjectReference& InNetReference)
	{
		FreeRemoteReference(Context, Args);
		NetReference = InNetReference;
	}

	/** 当前是否处于"本地引用"态。等价于 !IsRemoteReference()。 */
	bool IsNetReference() const
	{
		return !IsRemoteReference();
	}

	/** 当前是否处于"远程引用"态——即 RemoteReferencePtr 非空。 */
	bool IsRemoteReference() const
	{
		return RemoteReferencePtr != nullptr;
	}

	/** 比较两个 quantized 引用是否相等：必须是同态比较，跨态视为不等。 */
	bool operator==(const FQuantizedObjectReference& RHS) const;
	/** 引用是否有效（区分本地 / 远程态走不同的合法性检查）。 */
	bool IsValid() const;
	/**
	 * 释放远程引用所占的动态内存（调用 RemoteObjectReferenceNetSerializer 的 FreeDynamicState
	 * 然后通过 InternalContext::Free 归还内存），同时把 RemoteReferencePtr 置空。
	 * 在本地态调用是安全的 no-op。
	 */
	void FreeRemoteReference(FNetSerializationContext& Context, const FNetSerializerBaseArgs& Args);
	/** 调试输出。本地态走 NetReference.ToString()；远程态打印 "Remote: <RemoteId>"。 */
	FString ToString() const;

	// ----------------------------------------------------------------------------
	// 二态字段：通过 RemoteReferencePtr 是否为空来区分当前活跃的是哪一态。
	// ----------------------------------------------------------------------------
	/** 本地态使用的引用。当处于远程态时本字段值无意义（不应读取）。 */
	FNetObjectReference NetReference;
	/** 远程态使用的引用指针；非空表示当前是远程态。所有权属于本结构体，需要通过
	 *  FreeRemoteReference 释放（不能直接 delete）。 */
	FQuantizedRemoteObjectReference* RemoteReferencePtr = nullptr;
};

}

// 标记为 POD：用于上层模板优化（如 TArray 直接 memcpy）。注意：这只是说"按位复制不会出错"
// 在 trivial 拷贝意义上成立——真正涉及所有权迁移的场景必须使用 NetSerializer 的
// CloneDynamicState / FreeDynamicState，不能简单 memcpy 出多个副本同时持有 RemoteReferencePtr。
template <> struct TIsPODType<UE::Net::FQuantizedObjectReference> { enum { Value = true }; };
