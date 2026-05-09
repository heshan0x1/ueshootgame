// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavigationDirtyAreasController.h —— 脏区域（Dirty Area）累积与分发
// -----------------------------------------------------------------------------
// 文件职责：
//   - 帧内累积所有"影响导航的变更"对应的 AABB（来源：组件移动、Modifier 修改、
//     Octree 的 Pending 元素插入/删除等）；
//   - 按固定频率 DirtyAreasUpdateFreq 打包，Tick 时把整个 DirtyAreas 交给
//     NavDataSet 里每个 ANavigationData 的 RebuildDirtyAreas；
//   - 若启用 Active Tiles（Invokers），会把 DirtyArea 与 Invoker seed bounds
//     取交集再下发，避免对远离玩家的 Tile 做多余工作；
//   - WP Dynamic 模式下，会过滤掉"只是 Level Visibility 切换且已在 BaseNavmesh"
//     的脏，避免关卡流式触发不必要的重建。
// 核心类：
//   FNavigationDirtyAreasController （UNavigationSystemV1 持有一份成员）
// 与其它文件关系：
//   - 被 FNavigationDataHandler/FNavigationOctreeController 填数据；
//   - 调用 ANavigationData::RebuildDirtyAreas 下发。
// =============================================================================

#pragma once
#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
#include "AI/Navigation/NavigationTypes.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
#include "Containers/Array.h"
#include "HAL/Platform.h"
#include "Logging/LogMacros.h"
#include "Math/Box.h"
#include "Templates/Function.h"
#include "UObject/NameTypes.h"
#include "UObject/UnrealNames.h"

class UObject;
class ANavigationData;
struct FNavigationDirtyElement;
struct FNavigationElement;
struct FNavigationDirtyArea;
enum class ENavigationDirtyFlag : uint8;

NAVIGATIONSYSTEM_API DECLARE_LOG_CATEGORY_EXTERN(LogNavigationDirtyArea, Warning, All);

// 脏区域控制器：按帧累积 + 限流下发到各 NavigationData。
// 不拥有 NavData，由 UNavigationSystemV1 持有并在 Tick 时驱动。
struct FNavigationDirtyAreasController
{
	/** update frequency for dirty areas on navmesh */
	// 每秒多少次下发；0 表示只在 bForceRebuilding=true 时下发。
	float DirtyAreasUpdateFreq = 60.f;

	/** temporary cumulative time to calculate when we need to update dirty areas */
	// 自上次成功下发以来的累计时间（由 Tick 每帧加 DeltaSeconds）。
	float DirtyAreasUpdateTime = 0.f;

	/** stores areas marked as dirty throughout the frame, processes them
 *	once a frame in Tick function */
	// 本轮累积的全部脏区域；在 Tick 达到节流条件时整批下发然后 Reset()。
	TArray<FNavigationDirtyArea> DirtyAreas;

	// 是否允许累积（build lock 等场景下会暂时关闭，防止把半成品状态推下去）
	uint8 bCanAccumulateDirtyAreas : 1;
	// WorldPartition Dynamic 模式开关：影响"是否跳过可见性变更导致的脏"
	uint8 bUseWorldPartitionedDynamicMode : 1;

#if !UE_BUILD_SHIPPING
	// 累积被禁时若仍有脏被报告则记一下，用于检查潜在遗漏
	uint8 bDirtyAreasReportedWhileAccumulationLocked : 1;
private:
	uint8 bCanReportOversizedDirtyArea : 1;
	uint8 bNavigationBuildLocked : 1;

	/** -1 by default, if set to a positive value dirty area with bounds size over that threshold will be logged */
	float DirtyAreaWarningSizeThreshold = -1.f;

	NAVIGATIONSYSTEM_API bool ShouldReportOversizedDirtyArea() const;
#endif // !UE_BUILD_SHIPPING

public:
	NAVIGATIONSYSTEM_API FNavigationDirtyAreasController();

	// 清空累积（例如全量重建前）
	NAVIGATIONSYSTEM_API void Reset();
	
	/** sets cumulative time to at least one cycle so next tick will rebuild dirty areas */
	// 把累计时间拨到一个周期，保证下一次 Tick 立即下发
	NAVIGATIONSYSTEM_API void ForceRebuildOnNextTick();

	// Tick：把 DirtyAreas 下发到 NavDataSet 里每个 NavData（见 cpp 注释）
	NAVIGATIONSYSTEM_API void Tick(float DeltaSeconds, const TArray<ANavigationData*>& NavDataSet, bool bForceRebuilding = false);

	/** Add a dirty area to the queue based on the provided bounds and flags.
	 * Bounds must be valid and non-empty otherwise the request will be ignored and a warning reported.
	 * Accumulation must be allowed and flags valid otherwise the add is ignored.
	 * @param NewArea Bounding box of the affected area
	 * @param Flags Indicates the type of modification applied to the area
	 * @param ElementProviderFunc Optional function to retrieve source element that can be used for error reporting and navmesh exclusion
	 * @param DirtyElement Optional dirty element
	 * @param DebugReason Source of the new area
	 */
	// 单个 AABB 版的 AddAreas 封装
	NAVIGATIONSYSTEM_API void AddArea(const FBox& NewArea, const ENavigationDirtyFlag Flags, const TFunction<const TSharedPtr<const FNavigationElement>()>& ElementProviderFunc = nullptr,
		const FNavigationDirtyElement* DirtyElement = nullptr, const FName& DebugReason = NAME_None);
	UE_DEPRECATED(5.5, "Use the version taking ENavigationDirtyFlag and FNavigationElement instead.")
	NAVIGATIONSYSTEM_API void AddArea(const FBox& NewArea, const int32 Flags, const TFunction<UObject*()>& ObjectProviderFunc = nullptr,
		const FNavigationDirtyElement* DirtyElement = nullptr, const FName& DebugReason = NAME_None);

	/** Add non-empty list of dirty areas to the queue based on the provided bounds and flags.
	 * Bounds must be valid and non-empty otherwise the request will be ignored and a warning reported.
	 * Accumulation must be allowed and flags valid otherwise the add is ignored.
	 * A check will be triggered if an empty array is provided.
	 * @param NewAreas Array of bounding boxes of the affected areas
	 * @param Flags Indicates the type of modification applied to the area
	 * @param ElementProviderFunc Optional function to retrieve source element that can be used for error reporting and navmesh exclusion
	 * @param DirtyElement Optional dirty element
	 * @param DebugReason Source of the new area
	 */
	// 真正的入口：逐个 AABB 校验后追加到 DirtyAreas。
	// WP Dynamic 模式下会对"只是可见性变更且在 BaseNavmesh"的元素进行过滤。
	NAVIGATIONSYSTEM_API void AddAreas(const TConstArrayView<FBox> NewAreas, const ENavigationDirtyFlag Flags, const TFunction<const TSharedPtr<const FNavigationElement>()>& ElementProviderFunc = nullptr,
		const FNavigationDirtyElement* DirtyElement = nullptr, const FName& DebugReason = NAME_None);
	UE_DEPRECATED(5.5, "Use the version taking ENavigationDirtyFlag and FNavigationElement instead.")
	NAVIGATIONSYSTEM_API void AddAreas(const TConstArrayView<FBox> NewAreas, const int32 Flags, const TFunction<UObject*()>& ObjectProviderFunc = nullptr,
		const FNavigationDirtyElement* DirtyElement = nullptr, const FName& DebugReason = NAME_None);
	
	bool IsDirty() const { return GetNumDirtyAreas() > 0; }
	int32 GetNumDirtyAreas() const { return DirtyAreas.Num(); }

	// NavigationSystem::BeginLoad/EndLoad 包裹调用：锁期间不把 Oversized 警告打出来
	NAVIGATIONSYSTEM_API void OnNavigationBuildLocked();
	NAVIGATIONSYSTEM_API void OnNavigationBuildUnlocked();

	NAVIGATIONSYSTEM_API void SetUseWorldPartitionedDynamicMode(bool bIsWPDynamic);
	NAVIGATIONSYSTEM_API void SetCanReportOversizedDirtyArea(const bool bCanReport);
	NAVIGATIONSYSTEM_API void SetDirtyAreaWarningSizeThreshold(const float Threshold);

#if !UE_BUILD_SHIPPING
	bool HadDirtyAreasReportedWhileAccumulationLocked() const { return bCanAccumulateDirtyAreas == false && bDirtyAreasReportedWhileAccumulationLocked; }
#endif // UE_BUILD_SHIPPING

	// 可选谓词：若返回 true 则来自该 Object 的脏被丢弃（例如 HLOD 代理不应脏导航）
	DECLARE_DELEGATE_RetVal_OneParam(bool, FSkipObjectSignature, const UObject& /*Object*/);
	FSkipObjectSignature ShouldSkipObjectPredicate;
};
