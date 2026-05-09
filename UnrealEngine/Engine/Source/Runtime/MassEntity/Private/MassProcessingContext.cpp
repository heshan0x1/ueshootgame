// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// MassProcessingContext.cpp —— FProcessingContext 析构函数实现
// -----------------------------------------------------------------------------
// 这里只放析构函数（其他都是 .h 中的内联实现），因为析构里要 #include 全的
// FMassEntityManager / FMassCommandBuffer 实现来调用 FlushCommands / AppendCommands。
// =============================================================================

#include "MassProcessingContext.h"

namespace UE::Mass
{
	// @todo remove the depracation disabling once CommandBuffer and EntityManager are moved to `protected` and un-deprecated (around UE5.8)
	// 中文：访问被废弃直访的 CommandBuffer / EntityManager / bFlushCommandBuffer，需要包 PRAGMA。
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	/**
	 * 析构函数 —— 把累积的延迟命令应用回 EntityManager。
	 *
	 * 行为：
	 *   * 若 ExecutionContextPtr 为空（未 lazy 创建过 ExecutionContext，或被 rvalue GetExecutionContext 移走），
	 *     什么都不做。
	 *   * 否则：
	 *       1) 一致性检查：ExecutionContext 内的命令缓冲必须与 FProcessingContext 自己的 CommandBuffer 是同一个；
	 *          这是 SetCommandBuffer 的 checkf 保障的契约（不能在 ExecutionContext 创建后再换 buffer）。
	 *       2) 命令缓冲不能正处于 flushing 状态（避免重入）。
	 *       3) 按 bFlushCommandBuffer：
	 *            true  → FlushCommands：立刻在 EntityManager 上执行所有 deferred 命令（结构变更落地）
	 *            false → AppendCommands：仅把命令并入 EntityManager 的主 deferred 缓冲，由调用方统一 flush
	 *       4) 显式调用 ~FMassExecutionContext —— 因为它是 placement-new 在 ExecutionContextBuffer 上的。
	 *
	 * 调用线程：与 FProcessingContext 的所在作用域一致。Phase 路径下通常是 GameThread 或 task 内。
	 */
	FProcessingContext::~FProcessingContext()
	{	
		if (ExecutionContextPtr)
		{
			checkf(ExecutionContextPtr->GetSharedDeferredCommandBuffer(), TEXT("A valid execution context without a valid command buffer is unexpected"));
			checkf(ExecutionContextPtr->GetSharedDeferredCommandBuffer() == CommandBuffer
			   , TEXT("Hosted Execution Context's command buffer is not the same as FProcessingContext's command buffer. "
				 "This is not supposed to happen. Make sure FProcessingContext.CommandBuffer never gets assigned after FMassExecutionContext's creation"));

			ensure(!CommandBuffer->IsFlushing());

			if (bFlushCommandBuffer)
			{
				// 立即应用：触发 EntityManager 真正执行 add/remove/destroy 等结构变更命令
				EntityManager->FlushCommands(CommandBuffer);
			}
			else
			{
				// 推迟应用：仅把命令并入 EntityManager 主缓冲，留给上层统一 flush
				// （例如多 processor 串行 + Phase 末尾一次性 flush 的场景）
				EntityManager->AppendCommands(CommandBuffer);
			}

			// 显式析构 placement-new 出来的 FMassExecutionContext
			ExecutionContextPtr->~FMassExecutionContext();
		}
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
} // namespace UE::Mass
