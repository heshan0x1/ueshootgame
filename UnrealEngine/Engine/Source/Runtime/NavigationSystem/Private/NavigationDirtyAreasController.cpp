// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// NavigationDirtyAreasController.cpp —— FNavigationDirtyAreasController 实现。
// 关键要点：
//   - Tick 按 DirtyAreasUpdateFreq 节流，达到阈值才把 DirtyAreas 整批下发；
//   - 若 NavSys 启用了 Active Tiles Generation（Invokers），先把每个 DirtyArea
//     与 Invoker 的 seed bounds 取 Overlap，生成 SubAreaArray 再下发——避免
//     在玩家看不到的远处浪费 tile 生成；
//   - AddAreas 会校验 Bounds 合法性、Oversized 告警、WP Dynamic 模式下
//     跳过"只是可见性切换"的脏；
//   - SourceElement 只有在至少一个 DirtyArea 被加入后才有保留意义，
//     所以即使最终全部被跳过也不会写入 DirtyAreas。
// ============================================================================

#include "NavigationDirtyAreasController.h"
#include "AI/Navigation/NavigationDirtyArea.h"
#include "AI/Navigation/NavigationDirtyElement.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "VisualLogger/VisualLogger.h"

DEFINE_LOG_CATEGORY(LogNavigationDirtyArea);

//----------------------------------------------------------------------//
// FNavigationDirtyAreasController
//----------------------------------------------------------------------//
FNavigationDirtyAreasController::FNavigationDirtyAreasController()
	: bCanAccumulateDirtyAreas(true)
	, bUseWorldPartitionedDynamicMode(false)
#if !UE_BUILD_SHIPPING
	, bDirtyAreasReportedWhileAccumulationLocked(false)
	, bCanReportOversizedDirtyArea(false)
	, bNavigationBuildLocked(false)
#endif // !UE_BUILD_SHIPPING
{

}

// 把累积计时拨到 >=一个周期，下一次 Tick 立即触发下发。
// 典型使用：UnlockBuild 后需要尽快让所有积压 DirtyArea 生效。
void FNavigationDirtyAreasController::ForceRebuildOnNextTick()
{
	float MinTimeForUpdate = (DirtyAreasUpdateFreq != 0.f ? (1.0f / DirtyAreasUpdateFreq) : 0.f);
	DirtyAreasUpdateTime = FMath::Max(DirtyAreasUpdateTime, MinTimeForUpdate);
}

namespace UE::Navigation::Private
{
	// 工具：从 NavDataSet 任一可用 NavData 回推 UNavigationSystemV1。
	// 用于判断是否开启 Active Tiles 生成（依赖 NavSys 的开关与 Invokers）。
	const UNavigationSystemV1* FindNavigationSystem(const TArray<ANavigationData*>& NavDataSet)
	{
		const UNavigationSystemV1* NavSys = nullptr;
		for (const ANavigationData* NavData : NavDataSet)
		{
			if (NavData)
			{
				NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(NavData->GetWorld());
				if (NavSys)
				{
					return NavSys;
				}
			}
		}

		return NavSys;
	}
}

// Tick 的核心作用：
//   1) 累计 DeltaSeconds，当累计时间 >= 一个周期（或强制）时触发下发；
//   2) 若启用 Active Tiles，先把 DirtyArea 与 InvokerSeedBounds 取交集得到 SubAreaArray；
//   3) 遍历 NavDataSet，调用 ANavigationData::RebuildDirtyAreas（见架构文档 4.2 步骤 5）；
//   4) 清空本帧 DirtyAreas。
// 线程：GameThread（但 NavData::RebuildDirtyAreas 内部会把任务派给 Generator 的异步队列）。
void FNavigationDirtyAreasController::Tick(const float DeltaSeconds, const TArray<ANavigationData*>& NavDataSet, bool bForceRebuilding /*= false*/)
{
	DirtyAreasUpdateTime += DeltaSeconds;
	const bool bCanRebuildNow = bForceRebuilding || (DirtyAreasUpdateFreq != 0.f && DirtyAreasUpdateTime >= (1.0f / DirtyAreasUpdateFreq));

	if (DirtyAreas.Num() > 0 && bCanRebuildNow)
	{
		bool bIsUsingActiveTileGeneration = false;
		TArray<FNavigationDirtyArea> SubAreaArray;
		SubAreaArray.Reserve(DirtyAreas.Num());
		
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_RecastNavMeshGenerator_MakingSubAreas);

			// Find the relevant navigation system
			const UNavigationSystemV1* NavSys = UE::Navigation::Private::FindNavigationSystem(NavDataSet);
			
			const TArray<FBox>* SeedsBoundsArrayPtr = nullptr;
			bIsUsingActiveTileGeneration = NavSys && NavSys->IsActiveTilesGenerationEnabled(); 
			if (bIsUsingActiveTileGeneration)
			{
				// Invoker seed bounds：围绕玩家（或其他 Invoker）的活跃生成区域
				SeedsBoundsArrayPtr = &NavSys->GetInvokersSeedBounds();
			}

			// 循环不变量：遍历本帧所有累积的 DirtyArea，按需裁剪到 Invoker 区域
			for (const FNavigationDirtyArea& DirtyArea : DirtyAreas)
			{
				const FBox& AreaBound = DirtyArea.Bounds;
				if (!ensureMsgf(AreaBound.IsValid, TEXT("%hs Attempting to use DirtyArea.Bounds which are not valid. SourceObject: %s"),
					__FUNCTION__, *DirtyArea.GetSourceDescription()))
				{
					continue;
				}

				if (SeedsBoundsArrayPtr != nullptr && SeedsBoundsArrayPtr->Num() > 0)
				{
					// 对每个 invoker 的 seed bounds 计算 Overlap，得到多个子区域
					for (const FBox& SeedBounds : *SeedsBoundsArrayPtr)
					{
						// Compute sub area bound
						const FBox OverlapBox = AreaBound.Overlap(SeedBounds);
						if (OverlapBox.IsValid)
						{
							SubAreaArray.Emplace(OverlapBox, DirtyArea.Flags, DirtyArea.OptionalSourceElement);
						}
					}
				}
				else
				{
					// 无 Active Tiles 或无 invoker：整块透传
					SubAreaArray.Emplace(DirtyArea);
				}
			}
		}
		
		// 将最终区域列表下发给每个 NavData 让它们去做增量重建
		for (ANavigationData* NavData : NavDataSet)
		{
			if (NavData)
			{
				NavData->RebuildDirtyAreas(bIsUsingActiveTileGeneration ? SubAreaArray : DirtyAreas);
			}
		}

		DirtyAreasUpdateTime = 0.f;
		DirtyAreas.Reset();
	}
}

// Deprecated
void FNavigationDirtyAreasController::AddArea(const FBox& NewArea, const int32 Flags, const TFunction<UObject*()>& ObjectProviderFunc /*= nullptr*/,
	const FNavigationDirtyElement* DirtyElement /*= nullptr*/, const FName& DebugReason /*= NAME_None*/)
{
	AddAreas({NewArea}, static_cast<ENavigationDirtyFlag>(Flags), /*ElementProviderFunc*/nullptr, DirtyElement, DebugReason);
}

// Deprecated
void FNavigationDirtyAreasController::AddAreas(const TConstArrayView<FBox> NewAreas, const int32 Flags, const TFunction<UObject*()>& ObjectProviderFunc, const FNavigationDirtyElement* DirtyElement, const FName& DebugReason)
{
	AddAreas(NewAreas, static_cast<ENavigationDirtyFlag>(Flags), /*ElementProviderFunc*/nullptr, DirtyElement, DebugReason);
}

// 单 AABB 的便捷封装（转发到 AddAreas）
void FNavigationDirtyAreasController::AddArea(const FBox& NewArea, const ENavigationDirtyFlag Flags, const TFunction<const TSharedPtr<const FNavigationElement>()>& ElementProviderFunc /*= nullptr*/,
	const FNavigationDirtyElement* DirtyElement /*= nullptr*/, const FName& DebugReason /*= NAME_None*/)
{
	AddAreas({NewArea}, Flags, ElementProviderFunc, DirtyElement, DebugReason);
}

// 真正把 AABB 塞入 DirtyAreas。包含多层过滤：
//   1) 累积被锁且 Flags 有值 → 记一下 bDirtyAreasReportedWhileAccumulationLocked，用于诊断；
//   2) ShouldSkipObjectPredicate 是用户自定义的丢弃规则（比如 HLOD 代理）；
//   3) WP Dynamic：如果是"仅可见性切换"且已经在 Base Navmesh 中，忽略脏；
//   4) 每个 AABB 校验 IsValid / 非零体积；
//   5) Oversized 阈值命中时打 warning。
// 真正入队条件：Flags 非空 + 累积允许。
void FNavigationDirtyAreasController::AddAreas(const TConstArrayView<FBox> NewAreas, const ENavigationDirtyFlag Flags, const TFunction<const TSharedPtr<const FNavigationElement>()>& ElementProviderFunc, const FNavigationDirtyElement* DirtyElement, const FName& DebugReason)
{
#if !UE_BUILD_SHIPPING
	// always keep track of reported areas even when filtered out by invalid area as long as flags are valid
	bDirtyAreasReportedWhileAccumulationLocked |= (Flags != ENavigationDirtyFlag::None) && !bCanAccumulateDirtyAreas;

	checkf(NewAreas.Num() > 0, TEXT("All callers of this method are expected to provide at least one area."));
#endif // !UE_BUILD_SHIPPING

	const TSharedPtr<const FNavigationElement> SourceElement = ElementProviderFunc ? ElementProviderFunc() : nullptr;
	if (bUseWorldPartitionedDynamicMode)
	{
		// Both conditions must be true to ignore dirtiness.
		//  If it's only a visibility change and it's not in the base navmesh, it's the case created by loading a data layer (dirtiness must be applied)
		//  If there is no visibility change, the change is not from loading/unloading a cell (dirtiness must be applied)
		
		// ElementProviderFunc() is not always providing a valid element.
		if (const bool bIsFromVisibilityChange = (DirtyElement && DirtyElement->bIsFromVisibilityChange) || (SourceElement && SourceElement->IsFromLevelVisibilityChange()))
		{
			// If the area is from the addition or removal of elements caused by level loading/unloading and it's already in the base navmesh ignore the dirtiness.
			if (const bool bIsIncludedInBaseNavmesh = (DirtyElement && DirtyElement->bIsInBaseNavmesh) || (SourceElement && SourceElement->IsInBaseNavigationData()))
			{
				UE_LOG(LogNavigationDirtyArea, VeryVerbose, TEXT("Ignoring dirtyness (visibility changed and in base navmesh). (element: %s from: %s)"),
					*GetFullNameSafe(SourceElement.Get()), *DebugReason.ToString());
				return;
			}
		}
	}

	// 用户注入的"跳过某类 Object 的脏"谓词
	if (ShouldSkipObjectPredicate.IsBound())
	{
		const UObject* SourceObject = SourceElement ? SourceElement->GetWeakUObject().Get() : nullptr;
		if (SourceObject && ShouldSkipObjectPredicate.Execute(*SourceObject))
		{
			return;
		}
	}

	int32 NumInvalidBounds = 0;
	int32 NumEmptyBounds = 0;
	// 循环不变量：NumInvalidBounds/NumEmptyBounds 统计跳过数量；通过校验的 AABB 入队
	for (const FBox& NewArea : NewAreas)
	{
		if (!NewArea.IsValid)
		{
			NumInvalidBounds++;
			continue;
		}

		const FVector2D BoundsSize(NewArea.GetSize());
		if (BoundsSize.IsNearlyZero())
		{
			NumEmptyBounds++;
			continue;
		}

#if !UE_BUILD_SHIPPING
		// 懒惰构造调试信息 lambda：仅在需要打日志时执行
		auto DumpExtraInfo = [SourceElement, DebugReason, BoundsSize, NewArea]()
			{
				const UObject* SourceObject = SourceElement ? SourceElement->GetWeakUObject().Get() : nullptr;
				FString ObjectInfo;
				if (const UObject* ObjectOwner = (SourceObject != nullptr ? SourceObject->GetOuter() : nullptr))
				{
					UE_VLOG_BOX(ObjectOwner, LogNavigationDirtyArea, Log, NewArea, FColor::Red, TEXT(""));
					ObjectInfo = FString::Printf(TEXT(" | Element's owner: %s"), *GetFullNameSafe(ObjectOwner));
				}

				// attempt to find the actor which is/contains SourceObject
				const AActor* Actor = Cast<AActor>(SourceObject);
				{
					const UObject* CurrentObject = SourceObject;
					while (!Actor && CurrentObject)
					{
						CurrentObject = CurrentObject->GetOuter();
						Actor = Cast<AActor>(CurrentObject);
					}
				}
				FString ActorInfo;
				if (Actor)
				{
					// useful to have actor label for those placed in editor
					ActorInfo = FString::Printf(TEXT("| Actor: %s | Actor label: %s"), *GetFullNameSafe(Actor), *Actor->GetActorNameOrLabel());
				}

				return FString::Printf(TEXT("From: %s | Object: %s %s | Bounds: %s %s"),
					*DebugReason.ToString(),
					*GetFullNameSafe(SourceObject),
					*ObjectInfo,
					*BoundsSize.ToString(),
					*ActorInfo);
			};

		if (ShouldReportOversizedDirtyArea() && BoundsSize.GetMax() > DirtyAreaWarningSizeThreshold)
		{
			UE_LOG(LogNavigationDirtyArea, Warning, TEXT("Adding an oversized dirty area: %s | Threshold: %.2f"), *DumpExtraInfo(), DirtyAreaWarningSizeThreshold);
		}
		else
		{
			UE_LOG(LogNavigationDirtyArea, VeryVerbose, TEXT("Adding dirty area object: %s"), *DumpExtraInfo());
		}
#endif // !UE_BUILD_SHIPPING

		// 真正入队：Flags 非空且允许累积才写入
		if (Flags != ENavigationDirtyFlag::None && bCanAccumulateDirtyAreas)
		{
			DirtyAreas.Add(FNavigationDirtyArea(NewArea, Flags, SourceElement));
		}
	}
	
	UE_CLOG(NumInvalidBounds > 0 || NumEmptyBounds > 0, LogNavigationDirtyArea, Warning,
		TEXT("Skipped some dirty area creation due to: %d invalid bounds, %d empty bounds (element: %s, from: %s)"),
		NumInvalidBounds, NumEmptyBounds, *GetFullNameSafe(SourceElement.Get()), *DebugReason.ToString());
}

// 进入构建锁期间（例如 UNavigationSystemV1::BeginLoad）时关闭 Oversized 告警。
void FNavigationDirtyAreasController::OnNavigationBuildLocked()
{
#if !UE_BUILD_SHIPPING
	bNavigationBuildLocked = true;
#endif // !UE_BUILD_SHIPPING
}

void FNavigationDirtyAreasController::OnNavigationBuildUnlocked()
{
#if !UE_BUILD_SHIPPING
	bNavigationBuildLocked = false;
#endif // !UE_BUILD_SHIPPING
}

void FNavigationDirtyAreasController::SetDirtyAreaWarningSizeThreshold(const float Threshold)
{
#if !UE_BUILD_SHIPPING
	DirtyAreaWarningSizeThreshold = Threshold;
#endif // !UE_BUILD_SHIPPING
}

void FNavigationDirtyAreasController::SetUseWorldPartitionedDynamicMode(bool bIsWPDynamic)
{
	bUseWorldPartitionedDynamicMode = bIsWPDynamic;
}

void FNavigationDirtyAreasController::SetCanReportOversizedDirtyArea(bool bCanReport)
{
#if !UE_BUILD_SHIPPING
	bCanReportOversizedDirtyArea = bCanReport;
#endif // !UE_BUILD_SHIPPING
}

#if !UE_BUILD_SHIPPING
// 只有：Build 未锁定 + 允许报告 + 阈值 >= 0 时才打报告
bool FNavigationDirtyAreasController::ShouldReportOversizedDirtyArea() const
{ 
	return bNavigationBuildLocked == false && bCanReportOversizedDirtyArea && DirtyAreaWarningSizeThreshold >= 0.0f;
}
#endif // !UE_BUILD_SHIPPING


// 全量重置（比如切换 World）。
void FNavigationDirtyAreasController::Reset()
{
	// discard all pending dirty areas, we are going to rebuild navmesh anyway 
	UE_LOG(LogNavigationDirtyArea, VeryVerbose, TEXT("%hs: Reseting All Dirty Areas. DirtyAreas.Num = [%d]"),__FUNCTION__, DirtyAreas.Num());

	DirtyAreas.Reset();
#if !UE_BUILD_SHIPPING
	bDirtyAreasReportedWhileAccumulationLocked = false;
#endif // !UE_BUILD_SHIPPING
}
