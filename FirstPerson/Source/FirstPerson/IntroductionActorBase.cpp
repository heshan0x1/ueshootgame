// Copyright Epic Games, Inc. All Rights Reserved.

#include "IntroductionActorBase.h"
#include "Engine/World.h"

AIntroductionActorBase::AIntroductionActorBase()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AIntroductionActorBase::BeginPlay()
{
	Super::BeginPlay();
	OriginLocation = GetActorLocation();
	UE_LOG(LogTemp, Log, TEXT("[C++] BeginPlay - Origin:%s Top=%d Mid=%d Leaf=%d"),
		*OriginLocation.ToString(), TopIter, MidIter, LeafIter);
}

// =============================================================
// 共享 busy-work: 链式 sin/cos, 禁止优化
// =============================================================
PRAGMA_DISABLE_OPTIMIZATION
float AIntroductionActorBase::CppBusyWork(int32 Iterations, float Seed)
{
	float Acc = Seed;
	for (int32 i = 0; i < Iterations; ++i)
	{
		Acc = FMath::Sin(Acc + static_cast<float>(i) * 0.0001f)
		    + FMath::Cos(Acc * 0.9999f)
		    + 0.001f;
	}
	return Acc;
}
PRAGMA_ENABLE_OPTIMIZATION

// =============================================================
// Tick: 6 层深度 C++ ↔ AS 交替调用入口
//
// C++ L1 (3个函数, 各做 busy-work + 调 1 个 L2 AS 事件)
//   → AS L2 (3个事件, 各做 busy-work + 调 2 个 L3 C++ callable)
//     → C++ L3 (2个callable, 各做 busy-work + 调 2 个 L4 AS 事件)
//       → AS L4 (2个事件, 各做 busy-work + 调 2 个 L5 C++ callable)
//         → C++ L5 (2个callable, 各做 busy-work + 调 2 个 L6 AS 事件)
//           → AS L6 (2个事件, 各做 busy-work, 叶子返回)
// =============================================================
void AIntroductionActorBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ElapsedTime += DeltaTime;

	const float Seed = FMath::Fmod(ElapsedTime * 7.13f, 100.0f);
	float Total = 0.0f;

	// ---- Layer 1: 3 个 C++ 函数, 每个做 busy-work 后调对应 L2 AS ----
	Total += L1_SimPhysics(Seed);
	Total += L1_SimAnimation(Seed + 1.0f);
	Total += L1_SimEffects(Seed + 2.0f);

	// 用累计结果驱动 Actor 变换 (防止整条计算链被优化掉)
	Accumulator = Total;
	{
		const float Angle = ElapsedTime * OrbitSpeed;
		SetActorLocation(OriginLocation + FVector(
			FMath::Sin(Angle) * OrbitRadius,
			FMath::Cos(Angle) * OrbitRadius, 0.0f));
		SetActorRotation(FRotator(0.0f, ElapsedTime * RotationSpeed, 0.0f));
		const float S = 1.0f + 0.1f * FMath::Abs(FMath::Sin(Total * 0.01f));
		SetActorScale3D(FVector(S));
	}

	// [C++→AS] 帧结束通知
	BP_OnTickComplete(Total, DeltaTime);
}

// =============================================================
// Layer 1 (C++): 3 个函数, 各做 TopIter busy-work → 调 L2 AS
// =============================================================

float AIntroductionActorBase::L1_SimPhysics(float Seed)
{
	float R = CppBusyWork(TopIter, Seed);          // C++ work
	R += BP_L2_Physics(R);                          // ★ C++→AS
	return R;
}

float AIntroductionActorBase::L1_SimAnimation(float Seed)
{
	float R = CppBusyWork(TopIter, Seed);
	R += BP_L2_Animation(R);                        // ★ C++→AS
	return R;
}

float AIntroductionActorBase::L1_SimEffects(float Seed)
{
	float R = CppBusyWork(TopIter, Seed);
	R += BP_L2_Effects(R);                          // ★ C++→AS
	return R;
}

// =============================================================
// Layer 3 (C++): 2 个 BlueprintCallable
// AS 调用进来 → C++ 做 MidIter busy-work → 调 2 个 L4 AS 事件
// =============================================================

float AIntroductionActorBase::CppL3_Dynamics(float Seed)
{
	float R = CppBusyWork(MidIter, Seed);           // C++ work
	R += BP_L4_Forces(R);                           // ★ C++→AS (第1个L4)
	R += BP_L4_Collision(R);                        // ★ C++→AS (第2个L4)
	return R;
}

float AIntroductionActorBase::CppL3_Constraints(float Seed)
{
	float R = CppBusyWork(MidIter, Seed + 0.5f);   // C++ work
	R += BP_L4_Forces(R);                           // ★ C++→AS (第1个L4)
	R += BP_L4_Collision(R);                        // ★ C++→AS (第2个L4)
	return R;
}

// =============================================================
// Layer 5 (C++): 2 个 BlueprintCallable
// AS 调用进来 → C++ 做 LeafIter busy-work → 调 2 个 L6 AS 事件
// =============================================================

float AIntroductionActorBase::CppL5_Integrate(float Seed)
{
	float R = CppBusyWork(LeafIter, Seed);          // C++ work
	R += BP_L6_Position(R);                         // ★ C++→AS (第1个L6)
	R += BP_L6_Velocity(R);                         // ★ C++→AS (第2个L6)
	return R;
}

float AIntroductionActorBase::CppL5_Dampen(float Seed)
{
	float R = CppBusyWork(LeafIter, Seed + 0.5f);  // C++ work
	R += BP_L6_Position(R);                         // ★ C++→AS (第1个L6)
	R += BP_L6_Velocity(R);                         // ★ C++→AS (第2个L6)
	return R;
}