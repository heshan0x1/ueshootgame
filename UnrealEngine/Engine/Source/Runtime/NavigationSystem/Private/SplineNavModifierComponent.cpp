// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// SplineNavModifierComponent.cpp
// -----------------------------------------------------------------------------
// USplineNavModifierComponent 实现。要点：
//   - 通过 SubdivideSpline 把 Hermite Spline 转 Bezier + Tessellate，得到顺滑折线。
//   - 每段折线扫出一个矩形截面的凸体（8 顶点 tube），交给 FAreaNavModifier。
//   - 编辑器 Tick 里检测 Spline 版本号 + Transform 变化，按 bUpdateNavDataOnSplineChange 决定是否刷 NavData。
// =============================================================================

#include "SplineNavModifierComponent.h"

#include "AI/NavigationSystemBase.h"
#include "AI/Navigation/NavigationRelevantData.h"
#include "Components/SplineComponent.h"
#include "Curves/BezierUtilities.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SplineNavModifierComponent)

namespace
{
	// Subdivide the spline into linear segments, adapting to its curvature (more curvy means more linear segments)
	// 把 Spline 切成若干直线段。曲率大的区段切得更细，SubdivisionThreshold 越小越细。
	// 实现细节：UE 的 Spline 是 Hermite，Tessellate 需要 Bezier 控制点——转换因子 = 3.0。
	void SubdivideSpline(TArray<FVector>& OutSubdivisions, const USplineComponent& Spline, const float SubdivisionThreshold)
	{
		// Sample at least 2 points
		const int32 NumSplinePoints = FMath::Max(Spline.GetNumberOfSplinePoints(), 2);

		// The USplineComponent's Hermite spline tangents are 3 times larger than Bezier tangents and we need to convert before tessellation
		constexpr double HermiteToBezierFactor = 3.0;

		// Tessellate the spline segments
		// 闭合曲线：从最后一个点开始首尾闭合；非闭合从 INDEX_NONE 起跳过第一段
		int32 PrevIndex = Spline.IsClosedLoop() ? (NumSplinePoints - 1) : INDEX_NONE;
		for (int32 SplinePointIndex = 0; SplinePointIndex < NumSplinePoints; SplinePointIndex++)
		{
			if (PrevIndex >= 0)
			{
				const FSplinePoint PrevSplinePoint = Spline.GetSplinePointAt(PrevIndex, ESplineCoordinateSpace::World);
				const FSplinePoint CurrSplinePoint = Spline.GetSplinePointAt(SplinePointIndex, ESplineCoordinateSpace::World);

				// The first point of the segment is appended before tessellation since UE::CubicBezier::Tessellate does not add it
				// Tessellate 不会包含段起点，所以先手动 push 一个
				OutSubdivisions.Add(PrevSplinePoint.Position);

				// Convert this segment of the spline from Hermite to Bezier and subdivide it 
				// Hermite 的 Leave/Arrive Tangent 除以 3 得到 Bezier 的中间控制点
				UE::CubicBezier::Tessellate(OutSubdivisions,
					PrevSplinePoint.Position,
					PrevSplinePoint.Position + PrevSplinePoint.LeaveTangent / HermiteToBezierFactor,
					CurrSplinePoint.Position - CurrSplinePoint.ArriveTangent / HermiteToBezierFactor,
					CurrSplinePoint.Position,
					SubdivisionThreshold);
			}

			PrevIndex = SplinePointIndex;
		}
	}
}

// 计算 Bounds：Spline 自己的 AABB 按 StrokeWidth/Height 的一半外扩（取较大者）
void USplineNavModifierComponent::CalculateBounds() const
{
	Bounds = FBox(ForceInit);

	if (const USplineComponent* Spline = Cast<USplineComponent>(AttachedSpline.GetComponent(GetOwner())))
	{
		// The largest stroke length is used to expand the bounds
		// 用 Width/Height 一半的较大者扩展，确保 tube 一定落在 Bounds 内
		const double Buffer = FMath::Max(StrokeWidth / 2.0, StrokeHeight / 2.0);
		Bounds = Spline->CalcBounds(SplineTransform).GetBox().ExpandBy(Buffer);
	}
}

// 生成导航 Modifier：沿 Spline 每段扫出 tube 凸体，作为 FAreaNavModifier 塞入。
void USplineNavModifierComponent::GetNavigationData(FNavigationRelevantData& Data) const
{
	const USplineComponent* Spline = Cast<USplineComponent>(AttachedSpline.GetComponent(GetOwner()));
	if (!Spline)
	{
		return;
	}

	// Build a rectangle in the YZ plane used to sample the spline at each cross section
	// YZ 平面的矩形截面：X 为"前进方向"，每段 tube 用这个截面扫出
	constexpr int32 NumCrossSectionVertices = 4;
	const double StrokeHalfWidth = StrokeWidth / 2.0;
	const double StrokeHalfHeight = StrokeHeight / 2.0;
	TStaticArray<FVector, NumCrossSectionVertices> CrossSectionRect;
	CrossSectionRect[0] = FVector(0.0, -StrokeHalfWidth, -StrokeHalfHeight);
	CrossSectionRect[1] = FVector(0.0,  StrokeHalfWidth, -StrokeHalfHeight);
	CrossSectionRect[2] = FVector(0.0,  StrokeHalfWidth,  StrokeHalfHeight);
	CrossSectionRect[3] = FVector(0.0, -StrokeHalfWidth,  StrokeHalfHeight);

	// Vertices (in an arbitrary order) of a prism which will enclose each segment of the spline
	// 一个段的 8 个凸包顶点：4 前截面 + 4 后截面
	TStaticArray<FVector, NumCrossSectionVertices * 2> Tube;

	// Subdivide the spline so that high curvature sections get smaller and more linear segments than straighter sections
	TArray<FVector> Subdivisions;
	SubdivideSpline(Subdivisions, *Spline, GetSubdivisionThreshold());
	const int32 NumSubdivisions = Subdivisions.Num();

	// Create volumes from the spline subdivisions and use them to mark the nav mesh with the given are
	// 迭代目标：把相邻两个采样点之间做成一个棱柱凸包
	const FTransform ComponentTransform = Spline->GetComponentTransform();
	int32 PrevIndex = 0;
	for (int32 SubdivisionIndex = 1; SubdivisionIndex < NumSubdivisions; SubdivisionIndex++)
	{
		// Compute the rotation of this tube segment
		// 朝向 = 段方向向量的 HeadingAngle（绕 Z 轴），暂不处理高度抬升/俯仰
		const double TubeAngle = (Subdivisions[SubdivisionIndex] - Subdivisions[PrevIndex]).HeadingAngle();
		const FQuat TubeRotation(FVector::UnitZ(), TubeAngle);

		// Compute the vertices of this tube segment
		// 8 顶点 = 前截面 4 个（旋转后 + 平移到 prev 点） + 后截面 4 个（平移到 curr 点）
		for (int i = 0; i < NumCrossSectionVertices; i++)
		{
			// For each vertex of the tube segment, first rotate about the positive Z axis, then translate to the subdivision point
			Tube[i] = (TubeRotation * CrossSectionRect[i]) + Subdivisions[PrevIndex];
			Tube[i + NumCrossSectionVertices] = (TubeRotation * CrossSectionRect[i]) + Subdivisions[SubdivisionIndex];
		}

		// From the tube construct a convex hull whose volume will be used to mark the nav mesh with the selected AreaClass
		FAreaNavModifier NavModifier(Tube, ENavigationCoordSystem::Type::Unreal, ComponentTransform, AreaClass);
		if (AreaClassToReplace)
		{
			NavModifier.SetAreaClassToReplace(AreaClassToReplace);
		}
		Data.Modifiers.Add(NavModifier);

		PrevIndex = SubdivisionIndex;
	}
}

// 构造：编辑器下开 Tick，用于监测 Spline 变化；如果 Spline 已挂好则缓存 version/transform
USplineNavModifierComponent::USplineNavModifierComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	// Should tick in the editor in order to track whether the spline has updated
	bTickInEditor = true;
	PrimaryComponentTick.bCanEverTick = true;

	// If a spline is already attached, store its update-checking data
	if (const USplineComponent* Spline = Cast<USplineComponent>(AttachedSpline.GetComponent(GetOwner())))
	{
		SplineVersion = Spline->GetVersion();
		SplineTransform = Spline->GetComponentTransform();
	}
#endif // WITH_EDITORONLY_DATA
}

// 编辑器按钮入口：手动更新。非编辑器构建为空。
void USplineNavModifierComponent::UpdateNavigationWithComponentData()
{
#if WITH_EDITORONLY_DATA
	CalculateBounds();
	FNavigationSystem::UpdateComponentData(*this);
#endif // WITH_EDITORONLY_DATA
}

#if WITH_EDITORONLY_DATA
// Tick 仅在非 Game World 下启用——Runtime 我们不跟踪 Spline 变化
bool USplineNavModifierComponent::IsComponentTickEnabled() const
{
	const UWorld* World = GetWorld();
	return World && !World->IsGameWorld();
}

// 编辑器 Tick：检测 Spline 版本号或 Transform 变化，必要时刷新 NavData。
void USplineNavModifierComponent::TickComponent(const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (const USplineComponent* Spline = Cast<USplineComponent>(AttachedSpline.GetComponent(GetOwner())))
	{
		// Update spline data, and if anything changed then update nav data 
		if (SplineVersion != INVALID_SPLINE_VERSION)
		{
			bool bRequiresNavigationUpdate = false;
	
			// 版本号变化：Spline 点被增删/Tangent 改动
			const uint32 NextVersion = Spline->GetVersion();
			if (SplineVersion != NextVersion)
			{
				SplineVersion = NextVersion;
				bRequiresNavigationUpdate = true;
			}

			// Transform 变化：Actor/组件被移动
			const FTransform& NextTransform = Spline->GetComponentTransform();
			if (!SplineTransform.Equals(NextTransform))
			{
				SplineTransform = NextTransform;
				bRequiresNavigationUpdate = true;
			}

			// This can be expensive (i.e. updating every tick as the user drags a spline point), so only update nav data if the editor flag is set
			// 拖动过程中每帧更新代价很大——只有 bUpdateNavDataOnSplineChange 打开才会触发
			if (bRequiresNavigationUpdate && bUpdateNavDataOnSplineChange)
			{
				UpdateNavigationWithComponentData();
			}
		}
		else
		{
			// The spline just became valid; store its data and use it to update nav data
			// Spline 刚被赋值（之前为空）——记录初始 version/transform 并更新一次
			SplineVersion = Spline->GetVersion();
			SplineTransform = Spline->GetComponentTransform();

			UpdateNavigationWithComponentData();
		}
	}
	else if (SplineVersion != INVALID_SPLINE_VERSION)
	{
		// The spline just became invalid; reset the version and recompute nav data without the spline
		// Spline 被清空——重置 version，让 NavData 去掉我们这条
		SplineVersion = INVALID_SPLINE_VERSION;
		UpdateNavigationWithComponentData();
	}
}

#endif // WITH_EDITORONLY_DATA

// LOD → 曲率阈值的映射。Threshold 是 CubicBezier::Tessellate 的近似误差容忍。
float USplineNavModifierComponent::GetSubdivisionThreshold() const
{
	switch (SubdivisionLOD)
	{
	case ESubdivisionLOD::Ultra:
		return 10.0f;
	case ESubdivisionLOD::High:
		return 100.0f;
	case ESubdivisionLOD::Medium:
		return 250.0f;
	case ESubdivisionLOD::Low:
	default: // Fallthrough
		return 500.0f;
	}
}