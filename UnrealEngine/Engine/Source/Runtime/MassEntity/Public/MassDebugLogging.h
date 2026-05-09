// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityElementTypes.h"
#include "MassProcessingTypes.h"
// 下面这组 include 只在开启 Mass 调试时才需要，因为它们都属于 debug-only 路径：
//   * MassExecutionContext —— Processor 执行上下文，从中取 fragment view 和 entity list
//   * MassEntityHandle     —— 实体句柄，VLOG 时要把它显示出来
//   * MassDebugger         —— 运行时调试器，决定"当前哪个实体正在被调试观察"
//   * VisualLogger         —— UE 的可视化日志录制系统
// 非调试构建里这些全部被裁掉，FLoggingContext 走另一条"空实现"分支，零开销。
#if WITH_MASSENTITY_DEBUG
#include "MassExecutionContext.h"
#include "MassEntityHandle.h"
#include "MassDebugger.h"
#include "VisualLogger/VisualLogger.h"
#endif // WITH_MASSENTITY_DEBUG
#include "MassDebugLogging.generated.h"


// 前置声明：避免在头文件里直接引入 ExecutionContext / UObject 的完整定义
struct FMassExecutionContext;
class UObject;

/**
 * FMassDebugLogFragment —— 可选挂在实体上的"调试所有者"Fragment。
 *
 * 当某个实体由某个 UObject（如 AActor）驱动/代表时，把该 UObject 挂到这个 fragment
 * 上。VisualLogger 会以这个 UObject 为日志根（Owner）来组织轨迹/图形，使得实体的
 * 运行状态能在 VLog Viewer 里按其"概念所有者"归类，而不是堆在一起。
 *
 * 注意：LogOwner 使用 TWeakObjectPtr，允许 UObject 先于 Mass 实体被 GC 掉，不会
 * 造成悬垂指针。
 */
USTRUCT()
struct FMassDebugLogFragment : public FMassFragment
{
	GENERATED_BODY()

	/** 与此实体关联的"日志所有者"UObject（通常是 actor）。可为 null。 */
	UPROPERTY()
	TWeakObjectPtr<const UObject> LogOwner = nullptr;
};

// =============================================================================
// UE::Mass::Debug::FLoggingContext
// -----------------------------------------------------------------------------
// 在 Processor 内部做 per-entity 日志/可视化时的"帮助器"。它封装了三件事：
//   1) 拿到当前 chunk 里所有 FMassDebugLogFragment 的视图
//   2) 拿到当前 chunk 里对应的 FMassEntityHandle 列表
//   3) 在"要不要记录这个实体"的判断里综合 VisualLogger 的录制状态和 MassDebugger 的选中状态
//
// 通过同一套 API 同时支持 debug 和 release：release 下所有方法都是 no-op 返回 false，
// 调用方可以无脑写 `if (LogCtx.ShouldLogEntity(i)) UE_VLOG(...)` 而无需手写 #if。
// =============================================================================
namespace UE::Mass::Debug
{
	struct FLoggingContext
	{
#if WITH_MASSENTITY_DEBUG
		/**
		 * 调试构建下的构造函数：从 Processor 的执行上下文提取视图。
		 * @param InContext                     当前 Processor 的执行上下文
		 * @param bInLogEverythingWhenRecording 录像时是否记录"所有挂了 LogOwner 的实体"。
		 *                                       true=录像期间全量记录（用于事后审查）；
		 *                                       false=只记录被 MassDebugger 明确选中的实体。
		 */
		explicit FLoggingContext(const FMassExecutionContext& InContext, bool bInLogEverythingWhenRecording = true)
			: DebugFragmentsView(InContext.GetFragmentView<FMassDebugLogFragment>())
			, EntityListView(InContext.GetEntities())
			, bLogEverythingWhenRecording(bInLogEverythingWhenRecording)
		{	
		}

		/**
		 * 判断某实体本帧是否应当记录日志/可视化。
		 * @param EntityIndex      该实体在当前 chunk 中的下标
		 * @param OutEntityColor   可选输出：若 Debugger 给该实体分配了调试颜色，会写回这里
		 * @return 是否记录
		 *
		 * 判定策略（按优先级）：
		 *   1) 若开启 bLogEverythingWhenRecording 且实体挂了 LogOwner 且 VisualLogger 正在录制
		 *      —— 直接记录；
		 *   2) 否则回退到 MassDebugger：只有当该实体被调试器选中时才记录。
		 */
		inline bool ShouldLogEntity(int32 EntityIndex, FColor* OutEntityColor = nullptr) const
		{
#if ENABLE_VISUAL_LOG
			if (bLogEverythingWhenRecording
				&& DebugFragmentsView.IsValidIndex(EntityIndex)
				&& DebugFragmentsView[EntityIndex].LogOwner != nullptr
				&& FVisualLogger::IsRecording())
			{
				return true;
			}
#endif // ENABLE_VISUAL_LOG
			// If no actor owner or the visual logger is not recording, base it on the mass debugger
			// 若没有 Owner 或 VisualLogger 未录制，则以 MassDebugger 的选中状态为准
			return EntityListView.IsValidIndex(EntityIndex)
				&& IsDebuggingEntity(EntityListView[EntityIndex], OutEntityColor);
		}

		/**
		 * 取出某实体对应的日志 Owner（通常是关联的 AActor）。
		 * @param EntityIndex   实体在 chunk 中的下标
		 * @param FallbackOwner 若实体没有 DebugLogFragment 或 LogOwner 为空时的回退值
		 * @return 实体的 LogOwner；若无则返回 FallbackOwner
		 */
		inline const UObject* GetLogOwner(int32 EntityIndex, const UObject* FallbackOwner = nullptr) const
		{
			return DebugFragmentsView.IsValidIndex(EntityIndex)
				? DebugFragmentsView[EntityIndex].LogOwner.Get()
				: FallbackOwner;
		}

	private:
		/** 当前 chunk 内所有 FMassDebugLogFragment 的只读视图（下标对齐 EntityListView）。 */
		const TConstArrayView<FMassDebugLogFragment> DebugFragmentsView;
		/** 当前 chunk 内所有实体句柄（与 DebugFragmentsView 下标对齐）。 */
		const TConstArrayView<FMassEntityHandle> EntityListView;

		/** If true, ShouldLogEntity will return true when the visual logger is recording
		 *  If false, ShouldLogEntity will rely only on the MassDebugger
		 */
		/** 录像期间是否"无差别记录所有挂了 LogOwner 的实体"。详见构造函数注释。 */
		const bool bLogEverythingWhenRecording = true;

#else

		// -------------------- Non-debug build fallback --------------------
		// 在关闭调试的构建中，FLoggingContext 变成"空壳"：
		//   * 构造函数不做任何事
		//   * ShouldLogEntity 永远返回 false，让所有 VLOG 调用被死代码消除
		//   * GetLogOwner 直接返回 FallbackOwner
		// 这样调用方代码不需要 #if 包裹，发布构建里编译器会自动把整个 logging 分支剪掉。
		explicit FLoggingContext(const FMassExecutionContext& Context, bool bInCheckVisualLoggerForRecording = true)
		{	
		}

		/** 关闭调试时：始终返回 false，让 VLOG 调用被编译器优化掉。 */
		bool ShouldLogEntity(int32 EntityIndex, FColor* OutEntityColor = nullptr) const
		{
			return false;
		}

		/** 关闭调试时：无法解析 Owner，直接返回 Fallback。 */
		inline UObject* GetLogOwner(int32 EntityIndex, UObject* FallbackOwner = nullptr) const
		{
			return FallbackOwner;
		}
#endif // WITH_MASSENTITY_DEBUG
	};
}
