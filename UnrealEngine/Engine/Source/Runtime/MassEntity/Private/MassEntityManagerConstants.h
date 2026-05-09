// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

// =============================================================================================
// MassEntityManagerConstants.h —— FMassEntityManager 内部使用的常量集合（Private 头）
// ---------------------------------------------------------------------------------------------
// 该头文件位于 Private/，仅供 MassEntity 模块内部代码包含，不构成对外 API。
// 用 namespace UE::Mass::Private 把它们隔离开，明确"内部实现细节"的语义。
// =============================================================================================

namespace UE::Mass::Private
{
	// Index 0 is a sentinel for an Empty/Unset EntityHandle
	//
	// 中文：FMassEntityHandle 的 EntityIndex 字段以 0 作为"空句柄/未设置"哨兵值。
	//      所有合法 entity 索引从 1 开始计数。这样设计的好处：
	//        - 默认构造的 FMassEntityHandle{} 自然就是无效的（不需额外标志位）。
	//        - 与 UE 中很多 ID 系统（FName index 0 = NAME_None 等）保持一致风格。
	//        - 对比快：handle.IsValid() 等价于 EntityIndex != 0，编译器能用 jne 单指令判断。
	//
	//      代价：实际可分配的 entity 数量是 INT32_MAX - 1，损失一个 slot —— 完全可以接受。
	constexpr int32 InvalidEntityIndex = 0;
};
