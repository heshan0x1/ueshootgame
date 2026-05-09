// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavigationPath.h —— 路径数据（FNavigationPath）的 UObject/蓝图包装层
// -----------------------------------------------------------------------------
// 文件职责：
//   - 声明 UNavigationPath：把 C++ 内部表示的 FNavigationPath（见
//     NavigationData.h 中的内嵌结构）封装成一个蓝图可见的 UObject；
//   - 提供蓝图节点：长度、代价、是否 Partial、是否 String-Pulled、调试绘制等；
//   - 通过 PathObserver 观察底层 FNavigationPath 的事件（更新、失效、重算等）
//     并广播到蓝图层的 FOnNavigationPathUpdated 多播委托。
// 核心类：
//   UNavigationPath —— 对 FNavPathSharedPtr（FNavigationPath 的共享指针）的
//   UObject 外壳，用于 BP API 与编辑器调试可视化。
// 与其它文件关系：
//   - NavigationData.h：定义底层 FNavigationPath / ENavPathEvent；
//   - NavigationSystem.cpp：蓝图 FindPathToLocationSynchronously 创建本类实例。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "NavigationData.h"
#include "NavigationPath.generated.h"

class APlayerController;
class UCanvas;
class UNavigationPath;

// 蓝图多播：当此 UNavigationPath 所包装的底层路径触发 ENavPathEvent 事件时广播。
// 参数：(受影响的 UNavigationPath*, 事件类型 ENavPathEvent::Type)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNavigationPathUpdated, UNavigationPath*, AffectedPath, TEnumAsByte<ENavPathEvent::Type>, PathEvent);

/**
 *	UObject wrapper for FNavigationPath
 */
// 本类是 FNavigationPath 的 UObject 外壳：它本身不持有路径数据，
// 而是持有一个 FNavPathSharedPtr（SharedPath），并通过 PathObserver
// 订阅 SharedPath 的事件，再把事件转发到蓝图端 PathUpdatedNotifier。
UCLASS(BlueprintType, MinimalAPI)
class UNavigationPath : public UObject
{
	GENERATED_UCLASS_BODY()

	// 蓝图事件：路径被重算/失效/更新时触发。由 OnPathEvent 转发。
	UPROPERTY(BlueprintAssignable)
	FOnNavigationPathUpdated PathUpdatedNotifier;

	// 缓存给蓝图读的路径点（世界空间）。每次 SetPath/OnPathEvent 后
	// 由 SetPathPointsFromPath 从 SharedPath 的 FNavPathPoint 提取。
	UPROPERTY(BlueprintReadOnly, Category = Navigation)
	TArray<FVector> PathPoints;

	// 是否在路径失效（底层 NavData 变更）时自动请求重算。
	// 由 EnableRecalculationOnInvalidation 修改；与 FNavigationPath::bDoAutoUpdateOnInvalidation 相关。
	UPROPERTY(BlueprintReadOnly, Category = Navigation)
	ENavigationOptionFlag RecalculateOnInvalidation = ENavigationOptionFlag::Default;

private:	
	// 是否已通过 SetPath 关联了一条可用路径（与 SharedPath 的 IsValid 保持一致）。
	uint32 bIsValid : 1;
	// 是否启用了持续的 Canvas 调试绘制（配合 DrawDebugDelegateHandle）。
	uint32 bDebugDrawingEnabled : 1;
	// 调试绘制颜色（EnableDebugDrawing 中记录）。
	FColor DebugDrawingColor;

	// 绑定到 UDebugDrawService 的句柄，在 BeginDestroy/关闭绘制时用于反注册。
	FDelegateHandle DrawDebugDelegateHandle;

protected:
	// 真正持有底层路径的共享指针，由 NavigationData/寻路实现填充。
	FNavPathSharedPtr SharedPath;

	// 注册到 SharedPath 的观察者委托：当底层触发 ENavPathEvent 时回调 OnPathEvent。
	FNavigationPath::FPathObserverDelegate::FDelegate PathObserver;
	// PathObserver 在 SharedPath 上的注册句柄，用于 BeginDestroy 取消订阅。
	FDelegateHandle PathObserverDelegateHandle;

public:

	// UObject begin
	// 取消调试绘制/观察者注册，防止野指针回调。
	NAVIGATIONSYSTEM_API virtual void BeginDestroy() override;
	// UObject end

	// 返回可读字符串：路径点数、类型、长度、代价等，调试用。
	UFUNCTION(BlueprintCallable, Category = "AI|Debug")
	NAVIGATIONSYSTEM_API FString GetDebugString() const;

	// 开/关持续的 Canvas 调试线（通过 UDebugDrawService 注册）。
	UFUNCTION(BlueprintCallable, Category = "AI|Debug")
	NAVIGATIONSYSTEM_API void EnableDebugDrawing(bool bShouldDrawDebugData, FLinearColor PathColor = FLinearColor::White);

	/** if enabled path will request recalculation if it gets invalidated due to a change to underlying navigation */
	// 打开后，当底层导航 tile 改变使 SharedPath 失效时，会自动发起 Repath 请求。
	UFUNCTION(BlueprintCallable, Category = "AI|Navigation")
	NAVIGATIONSYSTEM_API void EnableRecalculationOnInvalidation(ENavigationOptionFlag DoRecalculation);

	// 路径总几何长度（路径点间距累加），单位 UE uu。
	UFUNCTION(BlueprintCallable, Category = "AI|Navigation")
	NAVIGATIONSYSTEM_API double GetPathLength() const;

	// 路径总寻路代价（基于 NavArea Cost，不等同于几何长度）。
	UFUNCTION(BlueprintCallable, Category = "AI|Navigation")
	NAVIGATIONSYSTEM_API double GetPathCost() const;

	// Partial：未能到达终点，只到了"可达的最近点"。
	UFUNCTION(BlueprintCallable, Category = "AI|Navigation")
	NAVIGATIONSYSTEM_API bool IsPartial() const;

	// 底层 SharedPath 仍然合法（未被 Invalidate，且有足够点）。
	UFUNCTION(BlueprintCallable, Category = "AI|Navigation")
	NAVIGATIONSYSTEM_API bool IsValid() const;

	// 是否已经做过 String Pulling（把多边形走廊拉成拐角点序列）。
	UFUNCTION(BlueprintCallable, Category = "AI|Navigation")
	NAVIGATIONSYSTEM_API bool IsStringPulled() const;
		
	// 赋值底层路径。内部会：解绑旧 PathObserver → 绑定新 PathObserver
	// → 刷新 PathPoints → 标记 bIsValid。
	NAVIGATIONSYSTEM_API void SetPath(FNavPathSharedPtr NewSharedPath);
	FNavPathSharedPtr GetPath() { return SharedPath; }

protected:
	// UDebugDrawService 的回调；每帧基于当前 PathPoints 画线。
	NAVIGATIONSYSTEM_API void DrawDebug(UCanvas* Canvas, APlayerController*);
	// SharedPath 的事件回调：刷新 PathPoints、bIsValid，随后广播 PathUpdatedNotifier。
	NAVIGATIONSYSTEM_API void OnPathEvent(FNavigationPath* Path, ENavPathEvent::Type PathEvent);

	// 将 FNavigationPath 的 FNavPathPoint 数组转成 BP 友好的 FVector 数组。
	NAVIGATIONSYSTEM_API void SetPathPointsFromPath(FNavigationPath& NativePath);
};
