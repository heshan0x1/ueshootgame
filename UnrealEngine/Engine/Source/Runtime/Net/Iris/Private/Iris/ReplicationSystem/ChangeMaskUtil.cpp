// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：ChangeMaskUtil.cpp
// 模块：Iris / ReplicationSystem
// 功能：ChangeMask 两类分配器（全局 Heap / MemStack 线性栈）的非内联实现。
// =====================================================================================

#include "Iris/ReplicationSystem/ChangeMaskUtil.h"
#include "HAL/UnrealMemory.h"
#include "Misc/MemStack.h"

namespace UE::Net::Private
{

// ------------------- FGlobalChangeMaskAllocator -------------------
// 中文：跨帧持久化的 ChangeMask 字数组分配器（如每对象每连接累积位图）。
// 直接走 UE 全局堆，配对 Free 必须显式调用。

void* FGlobalChangeMaskAllocator::Alloc(uint32 Size, uint32 Alignment)
{
	return FMemory::Malloc(Size, Alignment);
}

void FGlobalChangeMaskAllocator::Free(void* Pointer)
{
	return FMemory::Free(Pointer);
}

// ------------------- FMemStackChangeMaskAllocator -------------------
// 中文：帧内一次性 ChangeMask 分配（DequantizeAndApplyHelper / Quantize 临时位图）。
// MemStack 由调用方在 Scope 退出时整段回收，所以 Free() 是 no-op。

FMemStackChangeMaskAllocator::FMemStackChangeMaskAllocator(FMemStackBase* InMemStack)
: MemStack(InMemStack)
{
}

void* FMemStackChangeMaskAllocator::Alloc(uint32 Size, uint32 Alignment)
{ 
	return MemStack->Alloc(Size, Alignment);
}

void FMemStackChangeMaskAllocator::Free(void* Pointer)
{
	// 中文：MemStack 不支持单块释放，整段在 FMemMark 退出时回收，这里留空。
}

}
