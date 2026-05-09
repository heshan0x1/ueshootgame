// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassProcessingTypes.h"
#if WITH_MASSENTITY_DEBUG
#include "Containers/ArrayView.h"
#include "MassEntityQuery.h"
#include "Misc/MTAccessDetector.h"


struct FMassEntityManager;

// =====================================================================================
// 【FMassRequirementAccessDetector / 调试期 R/W 冲突检测器】
//
// 本结构仅在 WITH_MASSENTITY_DEBUG（开发/调试构建）下启用。Shipping 构建中以
// 空 stub（FScopedRequirementAccessDetector(const FMassEntityQuery&){}）替代，零开销。
//
// 【它在解决什么问题？】
//   Mass 调度器允许多个 query 在不同线程并行执行——只要它们对同一 fragment 的访问不冲突
//   （读读相容，读写/写写互斥）。但调度逻辑本身可能有 bug，或开发者手写了
//   绕过调度器的代码（例如直接调用 Query.ForEachEntityChunk）。如果这种绕过
//   导致两个 query 同时对同一 fragment 写入，行为就是数据竞争——而数据竞争是
//   Heisenberg 级别的 bug，难以复现。
//
//   为了在开发期就**主动捕获**这种错误，每种 element 类型挂一个 FRWAccessDetector
//   （UE 引擎内置的 MT 访问检测器，本质上是带 thread-id 的读写计数器）：
//     - query 开跑前 RequireAccess：根据 fragment 需求对每个相关 detector 申请 R/W 访问
//     - query 结束后 ReleaseAccess：对应释放
//     - 若两个 query 同时申请同一 detector 的 R+W 或 W+W，detector 内部 ensure 触发，
//       立即定位到冲突点
//
// 【典型踩坑场景】
//   1. ProcessorA 声明对 FTransformFragment 写访问，ProcessorB 也声明写访问，
//      但开发者忘了在 ConfigureQueries 中正确表达——调度器以为它们无冲突，
//      并行跑 → 检测器立即报告 "AcquireWriteAccess on already-write-locked detector"。
//
//   2. 一个 processor 内部启动了 ParallelFor，每个 worker 复用同一个 query
//      → 同一 detector 在不同线程同时申请 → 触发。
//
//   3. 在 GameThread 上某代码直接拿到 EntityManager 改了 fragment，而此时另一
//      Thread 上的 processor 正在跑同 query → 触发。
//
// 【cvar 控制】
//   "mass.debug.TrackRequirementsAccess"（默认 false，因为加锁有开销）。
//   想启用检测时在控制台打开。
// =====================================================================================
struct FMassRequirementAccessDetector
{
	/**
	 * 初始化——为所有已注册的 fragment / chunk / shared / constShared / external subsystem
	 * 类型各创建一个 FRWAccessDetector。必须在 game thread 调用一次（典型在
	 * EntityManager 启动时）。
	 */
	MASSENTITY_API void Initialize();

	/**
	 * Query 开跑前调用：对 query 声明的所有 fragment/subsystem 按 R/W 申请 detector。
	 * 若 cvar mass.debug.TrackRequirementsAccess 关闭，本函数等同 no-op。
	 */
	MASSENTITY_API void RequireAccess(const FMassEntityQuery& Query);

	/** Query 结束后调用：对应释放所有申请的 detector 访问。必须与 RequireAccess 配对。 */
	MASSENTITY_API void ReleaseAccess(const FMassEntityQuery& Query);

private:
	/** 通过成员函数指针调用 detector 的 Acquire/ReleaseRead/WriteAccess。 */
	using FDetectorMethod = bool (FRWAccessDetector::*)() const;

	/**
	 * 通用模板：对 BitSet 中所有命中类型，调用 detector 上的同一个方法（Acquire/Release）。
	 * 用于 subsystem 集合（位集本身就完整描述了类型）。
	 */
	template<typename TBitSet>
	void Operation(const TBitSet& BitSet, FDetectorMethod Op)
	{
		TArray<const UStruct*> Types;
		BitSet.ExportTypes(Types);
		for (const UStruct* Type : Types)
		{
			if (TSharedRef<FRWAccessDetector>* Detector = Detectors.Find(Type))
			{
				FRWAccessDetector& DetectorRef = Detector->Get();
				(DetectorRef.*Op)();
			}
		}
	}

	/**
	 * 申请 fragment 类需求的访问——遍历需求三元组数组，按 AccessMode 调用对应 Acquire。
	 * 跳过 Presence==None 的（不需要 binding 即不参与冲突分析）。
	 */
	void Aquire(TConstArrayView<FMassFragmentRequirementDescription> Requirements)
	{
		for (const FMassFragmentRequirementDescription& Req : Requirements)
		{
			if (Req.Presence != EMassFragmentPresence::None)
			{
				if (Req.AccessMode == EMassFragmentAccess::ReadWrite)
				{
					Detectors.Find(Req.StructType)->Get().AcquireWriteAccess();
				}
				else if (Req.AccessMode == EMassFragmentAccess::ReadOnly)
				{
					Detectors.Find(Req.StructType)->Get().AcquireReadAccess();
				}
			}
		}
	}

	/** 释放——与 Aquire 对称。 */
	void Release(TConstArrayView<FMassFragmentRequirementDescription> Requirements)
	{
		for (const FMassFragmentRequirementDescription& Req : Requirements)
		{
			if (Req.Presence != EMassFragmentPresence::None)
			{
				if (Req.AccessMode == EMassFragmentAccess::ReadWrite)
				{
					Detectors.Find(Req.StructType)->Get().ReleaseWriteAccess();
				}
				else if (Req.AccessMode == EMassFragmentAccess::ReadOnly)
				{
					Detectors.Find(Req.StructType)->Get().ReleaseReadAccess();
				}
			}
		}
	}

	/** @Note the function is not thread-safe and meant to be only called internally on game thread (see FMassRequirementAccessDetector::Initialize) */
	/**
	 * 从一个 FStructTracker（每类 element 都有一份，记录所有曾注册过的子类）中
	 * 拿到全部类型，为每种类型创建一个 FRWAccessDetector。
	 *
	 * @note 非线程安全；仅用于 Initialize 内部，调用者保证在 game thread。
	 */
	void AddDetectors(const FStructTracker& StructTracker);

	/** 类型 → detector 的映射（key = UStruct*）。每种 element 类型一个 detector。 */
	TMap<const UStruct*, TSharedRef<FRWAccessDetector>> Detectors;
};

#else
struct FMassEntityQuery;
#endif // WITH_MASSENTITY_DEBUG

namespace UE::Mass::Debug
{
	/**
	 * FScopedRequirementAccessDetector - RAII 包装
	 *
	 * 在 query 执行作用域内自动 Require/Release。典型用法：
	 *   void DoWork(FMassEntityQuery& Q) {
	 *       UE::Mass::Debug::FScopedRequirementAccessDetector Guard(Q);
	 *       Q.ForEachEntityChunk(...);
	 *   } // 作用域结束自动 ReleaseAccess
	 *
	 * Shipping 构建中蜕化为空构造函数 + 0 字节，无任何运行时开销。
	 */
	struct FScopedRequirementAccessDetector
	{
#if WITH_MASSENTITY_DEBUG
		/** 构造时调用 EntityManager 的 detector.RequireAccess(Query)。 */
		MASSENTITY_API FScopedRequirementAccessDetector(const FMassEntityQuery& InQuery);
		/** 析构时调用 ReleaseAccess(Query)。 */
		MASSENTITY_API ~FScopedRequirementAccessDetector();

		/** 持有 EntityManager 强引用——避免析构时 EntityManager 已被销毁导致悬挂。 */
		TSharedPtr<FMassEntityManager> EntityManager;
		/** 关联的 query 引用——析构时用来匹配 Release 的位集。 */
		const FMassEntityQuery& Query;
#else
		// Shipping 构建：空实现，编译器会优化掉所有调用。
		FScopedRequirementAccessDetector(const FMassEntityQuery&)
		{
		}
#endif // WITH_MASSENTITY_DEBUG
	};
}
