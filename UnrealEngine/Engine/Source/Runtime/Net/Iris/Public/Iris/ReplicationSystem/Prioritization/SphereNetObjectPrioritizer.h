// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：USphereNetObjectPrioritizer —— 经典「双层球」距离衰减打分器。
// 打分公式（per-object，per-view）：
//   d = distance(ObjectPos, ViewPos)
//   d_clamped = clamp(d, InnerRadius, OuterRadius) - InnerRadius
//   priority_in_sphere = InnerPriority + (OuterPriority - InnerPriority) * (d_clamped / (OuterRadius - InnerRadius))
//   final = (d > OuterRadius) ? OutsidePriority : priority_in_sphere
//   stored = max(stored, final)   // 注意 max，允许多 prioritizer 共存 / 多视图取最近者
// 多视图（Split-screen / 多个 PlayerController）：取距离最小的视图。
// 实现要点：
//   - SIMD 4-wide：每次处理 4 个对象，避免标量循环；
//   - 单视图 / 双视图 / 多视图 三个特化函数（双视图常见于本地双玩家分屏，多视图走 fallback）；
//   - 1024 对象一批，FMemStack 临时分配 Positions / Priorities，避免污染堆。
// =============================================================================================================================

#pragma once

#include "Iris/ReplicationSystem/Prioritization/LocationBasedNetObjectPrioritizer.h"
#include "Net/Core/NetBitArray.h"
#include "Math/VectorRegister.h"
#include "UObject/StrongObjectPtr.h"
#include "SphereNetObjectPrioritizer.generated.h"

class FMemStackBase;

// ini 配置：[/Script/IrisCore.SphereNetObjectPrioritizerConfig]
UCLASS(transient, config=Engine, MinimalAPI)
class USphereNetObjectPrioritizerConfig : public UNetObjectPrioritizerConfig
{
	GENERATED_BODY()

public:
	// 内球半径：在该半径内的对象统一使用 InnerPriority。
	UPROPERTY(Config)
	float InnerRadius = 2000.0f;

	// 外球半径：超过此半径的对象使用 OutsidePriority。
	UPROPERTY(Config)
	float OuterRadius = 10000.0f;

	UPROPERTY(Config)
	/** Priority for objects inside the inner sphere */
	// 内球内的优先级（最高）。
	float InnerPriority = 1.0f;

	UPROPERTY(Config)
	/** Priority at the border of the outer sphere */
	// 外球边界的优先级（衰减终点）。
	float OuterPriority = 0.2f;

	UPROPERTY(Config)
	/** Priority outside the sphere */
	// 外球以外的优先级（最低）。
	float OutsidePriority = 0.1f;
};

UCLASS(Transient, MinimalAPI)
class USphereNetObjectPrioritizer : public ULocationBasedNetObjectPrioritizer
{
	GENERATED_BODY()

protected:
	// UNetObjectPrioritizer interface
	IRISCORE_API virtual void Init(FNetObjectPrioritizerInitParams& Params) override;
	IRISCORE_API virtual void Deinit() override;
	IRISCORE_API virtual void Prioritize(FNetObjectPrioritizationParams&) override;

protected:
	// SIMD 计算常量（4-wide 广播）。SetupCalculationConstants 从 Config 计算一次后整批复用。
	struct FPriorityCalculationConstants
	{
		VectorRegister InnerRadius;
		VectorRegister OuterRadius;
		// OuterRadius - InnerRadius
		VectorRegister RadiusDiff;
		VectorRegister InvRadiusDiff;
		VectorRegister InnerPriority;
		VectorRegister OuterPriority;
		VectorRegister OutsidePriority;
		// OuterPriority - InnerPriority 
		VectorRegister PriorityDiff;
	};

	// 一批最多 1024 对象的临时数据（分配自 FMemStack）。
	struct FBatchParams
	{
		FPriorityCalculationConstants PriorityCalculationConstants;
		UE::Net::FReplicationView View;     // 完整视图（含多视点）。
		uint32 ConnectionId;

		uint32 ObjectCount;                  // 本批实际对象数（4 倍数对齐，剩余位用 0 占位）。
		VectorRegister* Positions;           // 16B 对齐的位置数组（VectorRegister 形式）。
		/** 16-byte aligned pointer to priorities. */
		// 16B 对齐：用 VectorLoadAligned/VectorStoreAligned 直接 SIMD 读写。
		float* Priorities;
	};

protected:
	// 把 PrioritizationParams 中本批要处理的对象的优先级 / 位置拷贝到 BatchParams（紧凑布局供 SIMD 使用）。
	// 不足 4 倍数的尾部用 0 填充，避免越界与分支。
	void PrepareBatch(FBatchParams& BatchParams, const FNetObjectPrioritizationParams& PrioritizationParams, uint32 ObjectIndexStartOffset);
	// 根据视图数量分发到三种 SIMD 实现之一。
	void PrioritizeBatch(FBatchParams& BatchParams);
	// 单视图（最常见，性能最好）。
	void PrioritizeBatchForSingleView(FBatchParams& BatchParams);
	// 双视图（分屏双玩家、本地共享设备）。
	void PrioritizeBatchForDualView(FBatchParams& BatchParams);
	// 多视图（>=3，循环展开度有限，性能较低）。
	void PrioritizeBatchForMultiView(FBatchParams& BatchParams);
	// 把 BatchParams.Priorities 拷贝回 PrioritizationParams.Priorities（按 ObjectIndices 索引）。
	void FinishBatch(const FBatchParams& BatchParams, FNetObjectPrioritizationParams& PrioritizationParams, uint32 ObjectIndexStartOffset);
	// 在 FMemStack 上预分配 BatchParams 的 Positions / Priorities 缓冲，并填好 PriorityCalculationConstants。
	void SetupBatchParams(FBatchParams& OutBatchParams, const FNetObjectPrioritizationParams& PrioritizationParams, uint32 MaxBatchObjectCount, FMemStackBase& Mem);
	void SetupCalculationConstants(FPriorityCalculationConstants& OutConstants);

	// ini Config（StrongObjectPtr 保活）。Init 时由派生类用 CastChecked 写入。
	TStrongObjectPtr<USphereNetObjectPrioritizerConfig> Config;
};
