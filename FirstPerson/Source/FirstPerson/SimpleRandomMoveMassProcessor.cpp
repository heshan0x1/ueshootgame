// Fill out your copyright notice in the Description page of Project Settings.


#include "SimpleRandomMoveMassProcessor.h"
#include "MassCommonFragments.h"
#include "FSimpleRandomMoveFragment.h"

USimpleRandomMoveMassProcessor::USimpleRandomMoveMassProcessor() : EntityQuery(*this)
{
	bAutoRegisterWithProcessingPhases = true;
	// Only run movement logic on server and standalone - clients receive replicated data
	ExecutionFlags = (int32)(EProcessorExecutionFlags::Server | EProcessorExecutionFlags::Standalone);
}

void USimpleRandomMoveMassProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FSimpleRandomMoveFragment>(EMassFragmentAccess::ReadWrite);
}

void USimpleRandomMoveMassProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	const float DeltaTime = Context.GetDeltaTimeSeconds();
	const float MoveSpeed = 200.0f;        // 移动速度 (cm/s)
	const float ArrivalThreshold = 50.0f;  // 到达目标判定距离
	const float RandomRange = 1000.0f;     // 随机目标范围

	EntityQuery.ForEachEntityChunk(Context, ([DeltaTime, MoveSpeed, ArrivalThreshold, RandomRange](FMassExecutionContext& Context)
		{
			const int32 NumEntities = Context.GetNumEntities();
			TArrayView<FTransformFragment> TransformFragments = Context.GetMutableFragmentView<FTransformFragment>();
			TArrayView<FSimpleRandomMoveFragment> MoveFragments = Context.GetMutableFragmentView<FSimpleRandomMoveFragment>();

			for (int32 i = 0; i < NumEntities; ++i)
			{
				FTransform& Transform = TransformFragments[i].GetMutableTransform();
				FVector& Target = MoveFragments[i].Target;
				FVector CurrentLocation = Transform.GetLocation();

				// 如果目标为零向量(初始状态)或已到达目标，生成新的随机目标
				if (Target.IsZero() || FVector::Dist(CurrentLocation, Target) < ArrivalThreshold)
				{
					Target = CurrentLocation + FVector(
						FMath::RandRange(-RandomRange, RandomRange),
						FMath::RandRange(-RandomRange, RandomRange),
						0.0f  // 保持在同一水平面
					);
				}

				// 朝目标方向移动
				FVector Direction = (Target - CurrentLocation).GetSafeNormal();
				FVector NewLocation = CurrentLocation + Direction * MoveSpeed * DeltaTime;
				Transform.SetLocation(NewLocation);

				// 让实体面朝移动方向
				if (!Direction.IsNearlyZero())
				{
					FRotator LookRotation = Direction.Rotation();
					Transform.SetRotation(FQuat(LookRotation));
				}
			}
		}));
}