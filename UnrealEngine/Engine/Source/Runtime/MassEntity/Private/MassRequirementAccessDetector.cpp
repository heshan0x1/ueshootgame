// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassRequirementAccessDetector.h"
#if WITH_MASSENTITY_DEBUG
#include "MassEntityQuery.h"
#include "HAL/IConsoleManager.h"
#include "MassEntityManager.h"

// =====================================================================================
// 整个 .cpp 文件由 #if WITH_MASSENTITY_DEBUG 包裹——只在调试构建编译。
// Shipping 构建下整个 detector 子系统是 0 行代码、0 字节。
// =====================================================================================

namespace UE::Mass::Private
{
	/**
	 * 运行时开关——默认 false（开销不为零，只在排查问题时打开）。
	 * 通过 cvar "mass.debug.TrackRequirementsAccess" 控制。
	 */
	bool bTrackRequirementsAccess = false;
	
	FAutoConsoleVariableRef CVarTrackRequirementsAccess(TEXT("mass.debug.TrackRequirementsAccess"), bTrackRequirementsAccess
		, TEXT("Enables Mass processing debugging mode where we monitor thread-safety of query requirements access."));
}

/**
 * 为所有已知 element 类型创建 detector。覆盖 5 个 BitSet 类别：
 *   - Fragment / ChunkFragment / SharedFragment / ConstSharedFragment
 *   - ExternalSubsystem
 *
 * 每个 FStructTrackerWrapper::StructTracker 是各类 BitSet 共享的全局类型注册表
 * （见 BitSet 体系，M3 文档）。这里直接读它即可拿到所有曾注册过的子类。
 *
 * @note must be on GameThread——AddDetectors 自身非线程安全（非原子地写 Detectors map）。
 */
void FMassRequirementAccessDetector::Initialize()
{
	check(IsInGameThread());
	AddDetectors(FMassFragmentBitSet::FStructTrackerWrapper::StructTracker);
	AddDetectors(FMassChunkFragmentBitSet::FStructTrackerWrapper::StructTracker);
	AddDetectors(FMassSharedFragmentBitSet::FStructTrackerWrapper::StructTracker);
	AddDetectors(FMassConstSharedFragmentBitSet::FStructTrackerWrapper::StructTracker);
	AddDetectors(FMassExternalSubsystemBitSet::FStructTrackerWrapper::StructTracker);
}

/**
 * 从 StructTracker 中提取所有已注册类型，每种类型创建一个 FRWAccessDetector。
 * Detector 用 TSharedRef 包装是因为后续以 reference 形式被持有（FRWAccessDetector 不可拷贝）。
 */
void FMassRequirementAccessDetector::AddDetectors(const FStructTracker& StructTracker)
{
	TConstArrayView<TWeakObjectPtr<const UStruct>> Types = StructTracker.DebugGetAllStructTypes<UStruct>();
	for (TWeakObjectPtr<const UStruct> Type : Types)
	{
		check(Type.Get());
		Detectors.Add(Type.Get(), MakeShareable(new FRWAccessDetector()));
	}
}

/**
 * 申请 query 涉及的所有 detector：
 *   - subsystem：const → AcquireRead，mutable → AcquireWrite
 *   - 4 类 fragment 数组：按 FMassFragmentRequirementDescription.AccessMode 申请
 *
 * 若任意 detector 已被冲突地占用（W vs R/W），FRWAccessDetector 内部 ensure 触发，
 * 立即抛出可定位的报错。
 *
 * cvar 关闭时整体 no-op，零开销。
 */
void FMassRequirementAccessDetector::RequireAccess(const FMassEntityQuery& Query)
{
	if (UE::Mass::Private::bTrackRequirementsAccess)
	{
		Operation(Query.RequiredConstSubsystems, &FRWAccessDetector::AcquireReadAccess);
		Operation(Query.RequiredMutableSubsystems, &FRWAccessDetector::AcquireWriteAccess);
		
		Aquire(Query.FragmentRequirements);
		Aquire(Query.ChunkFragmentRequirements);
		Aquire(Query.ConstSharedFragmentRequirements);
		Aquire(Query.SharedFragmentRequirements);
	}
}

/**
 * 与 RequireAccess 完全对称。query 结束后必须调用，否则后续 query 申请同 detector
 * 会误以为冲突而报错。RAII（FScopedRequirementAccessDetector）保证配对。
 */
void FMassRequirementAccessDetector::ReleaseAccess(const FMassEntityQuery& Query)
{
	if (UE::Mass::Private::bTrackRequirementsAccess)
	{
		Operation(Query.RequiredConstSubsystems, &FRWAccessDetector::ReleaseReadAccess);
		Operation(Query.RequiredMutableSubsystems, &FRWAccessDetector::ReleaseWriteAccess);

		Release(Query.FragmentRequirements);
		Release(Query.ChunkFragmentRequirements);
		Release(Query.ConstSharedFragmentRequirements);
		Release(Query.SharedFragmentRequirements);
	}
}

namespace UE::Mass::Debug
{
	//-----------------------------------------------------------------------------
	// FScopedRequirementAccessDetector
	// RAII 包装：构造时申请、析构时释放。
	// 持有 EntityManager 强引用以确保析构时 detector 仍然有效。
	//-----------------------------------------------------------------------------
	FScopedRequirementAccessDetector::FScopedRequirementAccessDetector(const FMassEntityQuery& InQuery)
		: EntityManager(InQuery.GetEntityManager())
		, Query(InQuery)
	{
		EntityManager->GetRequirementAccessDetector().RequireAccess(InQuery);
	}

	FScopedRequirementAccessDetector::~FScopedRequirementAccessDetector()
	{
		EntityManager->GetRequirementAccessDetector().ReleaseAccess(Query);
	}
}

#endif // WITH_MASSENTITY_DEBUG
