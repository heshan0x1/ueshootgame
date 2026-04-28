// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "IntroductionActorBase.generated.h"

/**
 * C++ 基类: 演示 6 层深度的 C++ ↔ AS 交替调用。
 *
 * 每帧 Tick 的完整调用树 (★ = 跨语言边界):
 *
 *  C++ Tick
 *  ├── L1_SimPhysics()    [C++ work] ──★──→ BP_L2_Physics()    [AS work]
 *  │                                         ├──★──→ CppL3_Dynamics()   [C++ work]
 *  │                                         │        ├──★──→ BP_L4_Forces()    [AS work]
 *  │                                         │        │        ├──★──→ CppL5_Integrate()  [C++ work]
 *  │                                         │        │        │        ├──★──→ BP_L6_Position() [AS work]
 *  │                                         │        │        │        └──★──→ BP_L6_Velocity() [AS work]
 *  │                                         │        │        └──★──→ CppL5_Dampen()     [C++ work]
 *  │                                         │        │                 ├──★──→ BP_L6_Position() [AS work]
 *  │                                         │        │                 └──★──→ BP_L6_Velocity() [AS work]
 *  │                                         │        └──★──→ BP_L4_Collision()  [AS work]
 *  │                                         │                 ├──★──→ CppL5_Integrate()
 *  │                                         │                 │        └── (same subtree)
 *  │                                         │                 └──★──→ CppL5_Dampen()
 *  │                                         │                          └── (same subtree)
 *  │                                         └──★──→ CppL3_Constraints() [C++ work]
 *  │                                                  ├──★──→ BP_L4_Forces()
 *  │                                                  │        └── (same subtree)
 *  │                                                  └──★──→ BP_L4_Collision()
 *  │                                                           └── (same subtree)
 *  ├── L1_SimAnimation()  [C++ work] ──★──→ BP_L2_Animation()  [AS work]
 *  │                                         ├──★──→ CppL3_Dynamics() ...
 *  │                                         └──★──→ CppL3_Constraints() ...
 *  └── L1_SimEffects()    [C++ work] ──★──→ BP_L2_Effects()    [AS work]
 *                                             ├──★──→ CppL3_Dynamics() ...
 *                                             └──★──→ CppL3_Constraints() ...
 *
 *  每帧执行统计:
 *    L1 C++ work :  3 次
 *    L2 AS work  :  3 次
 *    L3 C++ work :  6 次  (3 L2 × 2 calls)
 *    L4 AS work  : 12 次  (6 L3 × 2 calls)
 *    L5 C++ work : 24 次  (12 L4 × 2 calls)
 *    L6 AS work  : 48 次  (24 L5 × 2 calls)
 *    ───────────────────────
 *    C++ total   : 33 busy-work calls
 *    AS  total   : 63 busy-work calls
 *
 *  通过 TopIter / MidIter / LeafIter 调节各层迭代量，
 *  使总耗时 ≈30ms（C++ ≈15ms, AS ≈15ms）。
 */
UCLASS(Abstract)
class FIRSTPERSON_API AIntroductionActorBase : public AActor
{
	GENERATED_BODY()

public:
	AIntroductionActorBase();

protected:
	// ===== 分层迭代量 (编辑器中调节以校准到 ~30ms) =====

	/** L1/L2 (顶层) 每次 busy-work 迭代量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perf")
	int32 TopIter = 50000;

	/** L3/L4 (中层) 每次 busy-work 迭代量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perf")
	int32 MidIter = 10000;

	/** L5/L6 (底层) 每次 busy-work 迭代量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Perf")
	int32 LeafIter = 2000;

	// ===== 功能参数 =====

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Orbit")
	float OrbitRadius = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Orbit")
	float OrbitSpeed = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Orbit")
	float RotationSpeed = 90.0f;

	// ===== 内部状态 =====

	UPROPERTY(BlueprintReadOnly, Category="State")
	FVector OriginLocation;

	UPROPERTY(BlueprintReadOnly, Category="State")
	float ElapsedTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category="State")
	float Accumulator = 0.0f;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// ===== 共享 busy-work (BlueprintCallable 供 AS 回调) =====

	/** C++ 端链式 sin/cos 重迭代，不可被编译器优化 */
	UFUNCTION(BlueprintCallable, Category="BusyWork")
	float CppBusyWork(int32 Iterations, float Seed);

	// ===== 分层函数 =====
	
	float L1_SimPhysics(float Seed);
	float L1_SimAnimation(float Seed);
	float L1_SimEffects(float Seed);

	// ===================
	// Layer 2 (AS): 3 个 BlueprintImplementableEvent
	// C++ 调用 → AS 实现, AS 内部做 busy-work + 调 2 个 L3
	// ===================

	UFUNCTION(BlueprintImplementableEvent, Category="L2")
	float BP_L2_Physics(float Seed);

	UFUNCTION(BlueprintImplementableEvent, Category="L2")
	float BP_L2_Animation(float Seed);

	UFUNCTION(BlueprintImplementableEvent, Category="L2")
	float BP_L2_Effects(float Seed);

	// ===================
	// Layer 3 (C++): 2 个 BlueprintCallable
	// AS 调用 → C++ 实现, C++ 内部做 busy-work + 调 2 个 L4
	// ===================

	UFUNCTION(BlueprintCallable, Category="L3")
	float CppL3_Dynamics(float Seed);

	UFUNCTION(BlueprintCallable, Category="L3")
	float CppL3_Constraints(float Seed);

	// ===================
	// Layer 4 (AS): 2 个 BlueprintImplementableEvent
	// C++ 调用 → AS 实现, AS 内部做 busy-work + 调 2 个 L5
	// ===================

	UFUNCTION(BlueprintImplementableEvent, Category="L4")
	float BP_L4_Forces(float Seed);

	UFUNCTION(BlueprintImplementableEvent, Category="L4")
	float BP_L4_Collision(float Seed);

	// ===================
	// Layer 5 (C++): 2 个 BlueprintCallable
	// AS 调用 → C++ 实现, C++ 内部做 busy-work + 调 2 个 L6
	// ===================

	UFUNCTION(BlueprintCallable, Category="L5")
	float CppL5_Integrate(float Seed);

	UFUNCTION(BlueprintCallable, Category="L5")
	float CppL5_Dampen(float Seed);

	// ===================
	// Layer 6 (AS): 2 个 BlueprintImplementableEvent
	// C++ 调用 → AS 实现, AS 内部仅做 busy-work (叶子节点)
	// ===================

	UFUNCTION(BlueprintImplementableEvent, Category="L6")
	float BP_L6_Position(float Seed);

	UFUNCTION(BlueprintImplementableEvent, Category="L6")
	float BP_L6_Velocity(float Seed);

	// ===================
	// 帧结束通知
	// ===================

	UFUNCTION(BlueprintImplementableEvent, Category="Report")
	void BP_OnTickComplete(float TotalResult, float DeltaTime);
};