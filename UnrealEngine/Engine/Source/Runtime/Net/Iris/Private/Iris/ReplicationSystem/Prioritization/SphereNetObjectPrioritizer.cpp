// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：USphereNetObjectPrioritizer 的实现：4-wide SIMD 批量距离打分。
// 关键点：
//   - 距离公式（线性衰减）：见头文件注释；
//   - "取较大者"：与外部已存储优先级 max，允许多 prioritizer 共存；
//   - 单/双/多视图三个特化路径，前两者循环展开 + SIMD 友好，多视图用内联数组（最多 8 个视图免堆分配）。
// =============================================================================================================================

#include "Iris/ReplicationSystem/Prioritization/SphereNetObjectPrioritizer.h"
#include "Iris/ReplicationSystem/RepTag.h"
#include "Iris/ReplicationSystem/ReplicationProtocol.h"
#include "Iris/Serialization/VectorNetSerializers.h"
#include "Iris/Core/IrisProfiler.h"
#include "Misc/MemStack.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(SphereNetObjectPrioritizer)

void USphereNetObjectPrioritizer::Init(FNetObjectPrioritizerInitParams& Params)
{
	checkf(Params.Config != nullptr, TEXT("Need config to operate."));
	// 类型必须匹配——CastChecked 在不匹配时会断言，避免静默错配。
	Config = TStrongObjectPtr<USphereNetObjectPrioritizerConfig>(CastChecked<USphereNetObjectPrioritizerConfig>(Params.Config));

	Super::Init(Params);
}

void USphereNetObjectPrioritizer::Deinit()
{
	Super::Deinit();

	Config = nullptr;
}

void USphereNetObjectPrioritizer::Prioritize(FNetObjectPrioritizationParams& PrioritizationParams)
{
	IRIS_PROFILER_SCOPE(USphereNetObjectPrioritizer_Prioritize);
	
	// FMemStack：线程局部的栈式分配器；MemMark 析构时一次性回收本批内的所有分配，零碎片、零 free。
	FMemStack& Mem = FMemStack::Get();
	FMemMark MemMark(Mem);

	// Trade-off memory/performance
	// 每批 1024 对象 → 4 KB priorities + 16 KB positions ≈ 20 KB，能放进 L1。
	constexpr uint32 MaxBatchObjectCount = 1024U;

	// 4 倍数对齐：确保最后一批不会有越界 SIMD 读写（不足部分会被 PrepareBatch 用 0 填充）。
	uint32 BatchObjectCount = FMath::Min((PrioritizationParams.ObjectCount + 3U) & ~3U, MaxBatchObjectCount);
	FBatchParams BatchParams;
	SetupBatchParams(BatchParams, PrioritizationParams, BatchObjectCount, Mem);

	// 流水线：Prepare（拷贝/对齐）→ Prioritize（SIMD 计算）→ Finish（写回）。
	for (uint32 ObjectIt = 0, ObjectEndIt = PrioritizationParams.ObjectCount; ObjectIt < ObjectEndIt; )
	{
		const uint32 CurrentBatchObjectCount = FMath::Min(ObjectEndIt - ObjectIt, MaxBatchObjectCount);

		BatchParams.ObjectCount = CurrentBatchObjectCount;
		PrepareBatch(BatchParams, PrioritizationParams, ObjectIt);
		PrioritizeBatch(BatchParams);
		FinishBatch(BatchParams, PrioritizationParams, ObjectIt);

		ObjectIt += CurrentBatchObjectCount;
	}
}

// PrepareBatch：把全局数组中"本批"的对象优先级 / 位置拷贝到紧凑的本地缓冲（SIMD 友好）。
void USphereNetObjectPrioritizer::PrepareBatch(FBatchParams& BatchParams, const FNetObjectPrioritizationParams& PrioritizationParams, uint32 ObjectIndexStartOffset)
{
	IRIS_PROFILER_SCOPE(USphereNetObjectPrioritizer_PrepareBatch);
	const float* ExternalPriorities = PrioritizationParams.Priorities;
	const uint32* ObjectIndices = PrioritizationParams.ObjectIndices;
	const FNetObjectPrioritizationInfo* PrioritizationInfos = PrioritizationParams.PrioritizationInfos;

	float* LocalPriorities = BatchParams.Priorities;
	VectorRegister* Positions = BatchParams.Positions;

	// Copy priorities.
	// 拷贝当前优先级（SIMD 计算时取 max(已存, 新算)）。
	{
		uint32 LocalObjIt = 0;
		for (uint32 ObjIt = ObjectIndexStartOffset, ObjEndIt = ObjIt + BatchParams.ObjectCount; ObjIt != ObjEndIt; ++ObjIt, ++LocalObjIt)
		{
			const uint32 ObjectIndex = ObjectIndices[ObjIt];
			LocalPriorities[LocalObjIt] = ExternalPriorities[ObjectIndex];
		}
	}

	// Copy positions. 
	// 拷贝位置（基类 GetLocation 已是 VectorRegister）。
	uint32 LocalObjIt = 0;
	{
		for (uint32 ObjIt = ObjectIndexStartOffset, ObjEndIt = ObjIt + BatchParams.ObjectCount; ObjIt != ObjEndIt; ++ObjIt, ++LocalObjIt)
		{
			const uint32 ObjectIndex = ObjectIndices[ObjIt];
			const FObjectLocationInfo& Info = static_cast<const FObjectLocationInfo&>(PrioritizationInfos[ObjectIndex]);
			Positions[LocalObjIt] = GetLocation(Info);
		}
	}

	// Make sure we have a multiple of four valid entries.
	// 4 倍数尾部填充：优先级 0、位置 0 向量。FinishBatch 不会写回这些填充槽（按 ObjectCount 截断）。
	for (uint32 ObjIt = ObjectIndexStartOffset, ObjEndIt = (ObjIt + 3U) & ~3U; ObjIt != ObjEndIt; ++ObjIt, ++LocalObjIt)
	{
		LocalPriorities[LocalObjIt] = 0.0f;
		Positions[LocalObjIt] = VectorZero();
	}
}

void USphereNetObjectPrioritizer::PrioritizeBatch(FBatchParams& BatchParams)
{
	// 按视图数分发到三个特化版本（避免在内层循环里走通用路径）。
	const int32 ViewCount = BatchParams.View.Views.Num();
	if (ViewCount == 1)
	{
		PrioritizeBatchForSingleView(BatchParams);
	}
	else if (ViewCount == 2)
	{
		PrioritizeBatchForDualView(BatchParams);
	}
	else
	{
		PrioritizeBatchForMultiView(BatchParams);
	}
}

/**
  * Priority falls off linearly with distance from the object position to the view position.
  *
  * The equation is:
  * OuterPriority + (OuterPriority - InnerPriority)*(Clamp(Distance(ObjPos, ViewPos), InnerRadius, OuterRadius)/(OuterRadius - InnerRadius))
  */
// 实际计算（与上面注释稍有差异，以代码为准）：
//   d = |ObjectPos - ViewPos|
//   d_clamped = max(d - InnerRadius, 0)               // 内球内 d_clamped == 0 → 结果 = InnerPriority
//   factor = d_clamped / (OuterRadius - InnerRadius)
//   p_in   = InnerPriority + factor * (OuterPriority - InnerPriority)
//   p      = (d > OuterRadius) ? OutsidePriority : p_in
//   stored = max(stored, p)

void USphereNetObjectPrioritizer::PrioritizeBatchForSingleView(FBatchParams& BatchParams)
{
	IRIS_PROFILER_SCOPE(USphereNetObjectPrioritizer_PrioritizeBatchForSingleView);
	// ViewPos 加载为 W=0 的向量（与对象向量相减时不会污染 dot 结果）。
	const FVector& ViewPosVector = BatchParams.View.Views[0].Pos;
	const VectorRegister ViewPos = VectorLoadFloat3_W0(&ViewPosVector);

	const VectorRegister* Positions = BatchParams.Positions;
	float* Priorities = BatchParams.Priorities;
	// 4-wide 主循环：每次处理 4 个对象。
	for (uint32 ObjIt = 0, ObjEndIt = BatchParams.ObjectCount; ObjIt < ObjEndIt; ObjIt += 4)
	{
		// 读 4 个对象位置和已有 4 个优先级（16B 对齐 → VectorLoadAligned）。
		const VectorRegister Pos0 = Positions[ObjIt + 0];
		const VectorRegister Pos1 = Positions[ObjIt + 1];
		const VectorRegister Pos2 = Positions[ObjIt + 2];
		const VectorRegister Pos3 = Positions[ObjIt + 3];

		const VectorRegister OriginalPriorities0123 = VectorLoadAligned(Priorities + ObjIt);

		// Distance from point to view center
		// 距离向量（4 个对象 vs 同一视点）。
		const VectorRegister Dist0 = VectorSubtract(Pos0, ViewPos);
		const VectorRegister Dist1 = VectorSubtract(Pos1, ViewPos);
		const VectorRegister Dist2 = VectorSubtract(Pos2, ViewPos);
		const VectorRegister Dist3 = VectorSubtract(Pos3, ViewPos);

		// dot4 = x²+y²+z²（W=0 不影响）。结果广播在 4 分量上。
		const VectorRegister ScalarDistSqr0 = VectorDot4(Dist0, Dist0);
		const VectorRegister ScalarDistSqr1 = VectorDot4(Dist1, Dist1);
		const VectorRegister ScalarDistSqr2 = VectorDot4(Dist2, Dist2);
		const VectorRegister ScalarDistSqr3 = VectorDot4(Dist3, Dist3);

		// Assemble all distances into a single vector
		// $IRIS TODO: This can be optimized with SSE 4.1 using _mm_blend_ps. VectorDot4 or similar would only have to store the result in the X component too.
		// 把 4 个标量 dot 结果拼接成 (d0², d1², d2², d3²) 一个 SIMD 向量，以便后续 sqrt/cmp/select 也走 SIMD。
		const VectorRegister ScalarDistSqr0101 = VectorSwizzle(VectorCombineHigh(ScalarDistSqr0, ScalarDistSqr1), 0, 2, 1, 3);
		const VectorRegister ScalarDistSqr2323 = VectorSwizzle(VectorCombineHigh(ScalarDistSqr2, ScalarDistSqr3), 0, 2, 1, 3);
		const VectorRegister ScalarDistSqr0123 = VectorCombineHigh(ScalarDistSqr0101, ScalarDistSqr2323);

		// 开方得到真实距离，clamp 到 [InnerRadius, ...] 后减去 InnerRadius；max(...,0) 保证内球内为 0。
		const VectorRegister ScalarDist0123 = VectorSqrt(ScalarDistSqr0123);
		const VectorRegister ClampedScalarDist0123 = VectorMax(VectorSubtract(ScalarDist0123, BatchParams.PriorityCalculationConstants.InnerRadius), VectorZeroVectorRegister());

		// Calculate priority assuming the object is inside the sphere
		// 球内（含内球）的线性插值：InnerPriority + factor*(OuterPriority - InnerPriority)。
		const VectorRegister RadiusFactor = VectorMultiply(ClampedScalarDist0123, BatchParams.PriorityCalculationConstants.InvRadiusDiff);
		VectorRegister Priorities0123 = VectorMultiplyAdd(RadiusFactor, BatchParams.PriorityCalculationConstants.PriorityDiff, BatchParams.PriorityCalculationConstants.InnerPriority);

		// If object is outside the sphere we use the OutsidePriority
		// 球外（d_clamped > RadiusDiff，即 d > OuterRadius）：用 OutsidePriority 替换。
		const VectorRegister OutsideSphereMask = VectorCompareGT(ClampedScalarDist0123, BatchParams.PriorityCalculationConstants.RadiusDiff);
		Priorities0123 = VectorSelect(OutsideSphereMask, BatchParams.PriorityCalculationConstants.OutsidePriority, Priorities0123);

		// Store the max of our calculated priority and the provided priorities
		// 与已有优先级取 max（多 prioritizer 共存的关键约定）。
		Priorities0123 = VectorMax(Priorities0123, OriginalPriorities0123);
		VectorStoreAligned(Priorities0123, Priorities + ObjIt);
	}
}

// 双视图：每个对象 vs 两个视点，取 min 的距离平方（对应"最近的玩家视点"）。其它步骤同单视图。
void USphereNetObjectPrioritizer::PrioritizeBatchForDualView(FBatchParams& BatchParams)
{
	IRIS_PROFILER_SCOPE(USphereNetObjectPrioritizer_PrioritizeBatchForDualView);
	const FVector& ViewPos0Vector = BatchParams.View.Views[0].Pos;
	const VectorRegister ViewPos0 = VectorLoadFloat3_W0(&ViewPos0Vector);

	const FVector& ViewPos1Vector = BatchParams.View.Views[1].Pos;
	const VectorRegister ViewPos1 = VectorLoadFloat3_W0(&ViewPos1Vector);

	const VectorRegister* Positions = BatchParams.Positions;
	float* Priorities = BatchParams.Priorities;
	for (uint32 ObjIt = 0, ObjEndIt = BatchParams.ObjectCount; ObjIt < ObjEndIt; ObjIt += 4)
	{
		const VectorRegister Pos0 = Positions[ObjIt + 0];
		const VectorRegister Pos1 = Positions[ObjIt + 1];
		const VectorRegister Pos2 = Positions[ObjIt + 2];
		const VectorRegister Pos3 = Positions[ObjIt + 3];

		const VectorRegister OriginalPriorities0123 = VectorLoadAligned(Priorities + ObjIt);

		// Distance from point to view centers
		// 对视点 0 的距离平方
		const VectorRegister Dist0_0 = VectorSubtract(Pos0, ViewPos0);
		const VectorRegister Dist1_0 = VectorSubtract(Pos1, ViewPos0);
		const VectorRegister Dist2_0 = VectorSubtract(Pos2, ViewPos0);
		const VectorRegister Dist3_0 = VectorSubtract(Pos3, ViewPos0);

		VectorRegister ScalarDistSqr0 = VectorDot4(Dist0_0, Dist0_0);
		VectorRegister ScalarDistSqr1 = VectorDot4(Dist1_0, Dist1_0);
		VectorRegister ScalarDistSqr2 = VectorDot4(Dist2_0, Dist2_0);
		VectorRegister ScalarDistSqr3 = VectorDot4(Dist3_0, Dist3_0);

		// 对视点 1 的距离平方
		const VectorRegister Dist0_1 = VectorSubtract(Pos0, ViewPos1);
		const VectorRegister Dist1_1 = VectorSubtract(Pos1, ViewPos1);
		const VectorRegister Dist2_1 = VectorSubtract(Pos2, ViewPos1);
		const VectorRegister Dist3_1 = VectorSubtract(Pos3, ViewPos1);

		const VectorRegister ScalarDistSqr0_1 = VectorDot4(Dist0_1, Dist0_1);
		const VectorRegister ScalarDistSqr1_1 = VectorDot4(Dist1_1, Dist1_1);
		const VectorRegister ScalarDistSqr2_1 = VectorDot4(Dist2_1, Dist2_1);
		const VectorRegister ScalarDistSqr3_1 = VectorDot4(Dist3_1, Dist3_1);

		// Pick closest points
		// 取最近视点的距离平方（注意：sqrt 单调，对距离平方取 min 等价于对距离取 min）。
		ScalarDistSqr0 = VectorMin(ScalarDistSqr0, ScalarDistSqr0_1);
		ScalarDistSqr1 = VectorMin(ScalarDistSqr1, ScalarDistSqr1_1);
		ScalarDistSqr2 = VectorMin(ScalarDistSqr2, ScalarDistSqr2_1);
		ScalarDistSqr3 = VectorMin(ScalarDistSqr3, ScalarDistSqr3_1);

		// Assemble all distances into a single vector
		const VectorRegister ScalarDistSqr0101 = VectorSwizzle(VectorCombineHigh(ScalarDistSqr0, ScalarDistSqr1), 0, 2, 1, 3);
		const VectorRegister ScalarDistSqr2323 = VectorSwizzle(VectorCombineHigh(ScalarDistSqr2, ScalarDistSqr3), 0, 2, 1, 3);
		const VectorRegister ScalarDistSqr0123 = VectorCombineHigh(ScalarDistSqr0101, ScalarDistSqr2323);

		const VectorRegister ScalarDist0123 = VectorSqrt(ScalarDistSqr0123);
		const VectorRegister ClampedScalarDist0123 = VectorMax(VectorSubtract(ScalarDist0123, BatchParams.PriorityCalculationConstants.InnerRadius), VectorZeroVectorRegister());

		// Calculate priority assuming the object is inside the sphere
		const VectorRegister RadiusFactor = VectorMultiply(ClampedScalarDist0123, BatchParams.PriorityCalculationConstants.InvRadiusDiff);
		VectorRegister Priorities0123 = VectorMultiplyAdd(RadiusFactor, BatchParams.PriorityCalculationConstants.PriorityDiff, BatchParams.PriorityCalculationConstants.InnerPriority);

		// If object is outside the sphere we use the OutsidePriority
		const VectorRegister OutsideSphereMask = VectorCompareGT(ClampedScalarDist0123, BatchParams.PriorityCalculationConstants.RadiusDiff);
		Priorities0123 = VectorSelect(OutsideSphereMask, BatchParams.PriorityCalculationConstants.OutsidePriority, Priorities0123);

		// Store the max of our calculated priority and the provided priorities
		Priorities0123 = VectorMax(Priorities0123, OriginalPriorities0123);
		VectorStoreAligned(Priorities0123, Priorities + ObjIt);
	}
}

// 多视图（>=3）：循环外预先把所有视点拷到 inline array，然后内循环按视点累计 min 距离平方。
void USphereNetObjectPrioritizer::PrioritizeBatchForMultiView(FBatchParams& BatchParams)
{
	IRIS_PROFILER_SCOPE(USphereNetObjectPrioritizer_PrioritizeBatchForMultiView);
	// 内联数组容量 8（典型场景：分屏 4 玩家 + 副视图）；超过 8 会回退到堆分配并触发 ensure（性能警告）。
	TArray<VectorRegister, TInlineAllocator<8>> ViewPositions;
	for (const UE::Net::FReplicationView::FView& View : BatchParams.View.Views)
	{
		const FVector& ViewPosVector = View.Pos;
		ViewPositions.Add(VectorLoadFloat3_W0(&ViewPosVector));
	}
	ensureMsgf(ViewPositions.Num() <= 8, TEXT("Performance warning: Global allocation was needed to accommodate %d views."), ViewPositions.Num());

	// FLT_MAX 作为 min 累加器初始值。
	const VectorRegister MaxFloatVector = VectorSetFloat1(MAX_flt);

	const VectorRegister* Positions = BatchParams.Positions;
	float* Priorities = BatchParams.Priorities;
	for (uint32 ObjIt = 0, ObjEndIt = BatchParams.ObjectCount; ObjIt < ObjEndIt; ObjIt += 4)
	{
		const VectorRegister Pos0 = Positions[ObjIt + 0];
		const VectorRegister Pos1 = Positions[ObjIt + 1];
		const VectorRegister Pos2 = Positions[ObjIt + 2];
		const VectorRegister Pos3 = Positions[ObjIt + 3];

		const VectorRegister OriginalPriorities0123 = VectorLoadAligned(Priorities + ObjIt);

		// Distance from point to view centers
		// 累加 min(d²)：对每个视点更新 4 个对象的最小距离平方。
		VectorRegister ScalarDistSqr0 = MaxFloatVector;
		VectorRegister ScalarDistSqr1 = MaxFloatVector;
		VectorRegister ScalarDistSqr2 = MaxFloatVector;
		VectorRegister ScalarDistSqr3 = MaxFloatVector;

		for (VectorRegister ViewPos : ViewPositions)
		{
			const VectorRegister Dist0 = VectorSubtract(Pos0, ViewPos);
			const VectorRegister Dist1 = VectorSubtract(Pos1, ViewPos);
			const VectorRegister Dist2 = VectorSubtract(Pos2, ViewPos);
			const VectorRegister Dist3 = VectorSubtract(Pos3, ViewPos);

 			ScalarDistSqr0 = VectorMin(ScalarDistSqr0, VectorDot4(Dist0, Dist0));
			ScalarDistSqr1 = VectorMin(ScalarDistSqr1, VectorDot4(Dist1, Dist1));
			ScalarDistSqr2 = VectorMin(ScalarDistSqr2, VectorDot4(Dist2, Dist2));
			ScalarDistSqr3 = VectorMin(ScalarDistSqr3, VectorDot4(Dist3, Dist3));
		}

		// Assemble all distances into a single vector
		const VectorRegister ScalarDistSqr0101 = VectorSwizzle(VectorCombineHigh(ScalarDistSqr0, ScalarDistSqr1), 0, 2, 1, 3);
		const VectorRegister ScalarDistSqr2323 = VectorSwizzle(VectorCombineHigh(ScalarDistSqr2, ScalarDistSqr3), 0, 2, 1, 3);
		const VectorRegister ScalarDistSqr0123 = VectorCombineHigh(ScalarDistSqr0101, ScalarDistSqr2323);

		const VectorRegister ScalarDist0123 = VectorSqrt(ScalarDistSqr0123);
		const VectorRegister ClampedScalarDist0123 = VectorMax(VectorSubtract(ScalarDist0123, BatchParams.PriorityCalculationConstants.InnerRadius), VectorZeroVectorRegister());

		// Calculate priority assuming the object is inside the sphere
		const VectorRegister RadiusFactor = VectorMultiply(ClampedScalarDist0123, BatchParams.PriorityCalculationConstants.InvRadiusDiff);
		VectorRegister Priorities0123 = VectorMultiplyAdd(RadiusFactor, BatchParams.PriorityCalculationConstants.PriorityDiff, BatchParams.PriorityCalculationConstants.InnerPriority);

		// If object is outside the sphere we use the OutsidePriority
		const VectorRegister OutsideSphereMask = VectorCompareGT(ClampedScalarDist0123, BatchParams.PriorityCalculationConstants.RadiusDiff);
		Priorities0123 = VectorSelect(OutsideSphereMask, BatchParams.PriorityCalculationConstants.OutsidePriority, Priorities0123);

		// Store the max of our calculated priority and the provided priorities
		Priorities0123 = VectorMax(Priorities0123, OriginalPriorities0123);
		VectorStoreAligned(Priorities0123, Priorities + ObjIt);
	}
}

// FinishBatch：把本地紧凑数组的优先级按 ObjectIndices 写回到全局连接的 Priorities 数组。
void USphereNetObjectPrioritizer::FinishBatch(const FBatchParams& BatchParams, FNetObjectPrioritizationParams& PrioritizationParams, uint32 ObjectIndexStartOffset)
{
	IRIS_PROFILER_SCOPE(USphereNetObjectPrioritizer_FinishBatch);
	float* ExternalPriorities = PrioritizationParams.Priorities;
	const uint32* ObjectIndices = PrioritizationParams.ObjectIndices;

	const float* LocalPriorities = BatchParams.Priorities;

	// Update the object priority array
	uint32 LocalObjIt = 0;
	for (uint32 ObjIt = ObjectIndexStartOffset, ObjEndIt = ObjIt + BatchParams.ObjectCount; ObjIt != ObjEndIt; ++ObjIt, ++LocalObjIt)
	{
		const uint32 ObjectIndex = ObjectIndices[ObjIt];
		ExternalPriorities[ObjectIndex] = LocalPriorities[LocalObjIt];
	}
}

void USphereNetObjectPrioritizer::SetupBatchParams(FBatchParams& OutBatchParams, const FNetObjectPrioritizationParams& PrioritizationParams, uint32 MaxBatchObjectCount, FMemStackBase& Mem)
{
	OutBatchParams.View = PrioritizationParams.View;
	OutBatchParams.ConnectionId = PrioritizationParams.ConnectionId;
	// Positions: 16B 对齐（VectorRegister 对齐要求）。
	OutBatchParams.Positions = static_cast<VectorRegister*>(Mem.Alloc(MaxBatchObjectCount*sizeof(VectorRegister), alignof(VectorRegister)));
	// Priorities: 强制 16B 对齐以便 VectorLoadAligned/VectorStoreAligned。
	OutBatchParams.Priorities = static_cast<float*>(Mem.Alloc(MaxBatchObjectCount*sizeof(float), 16));

	SetupCalculationConstants(OutBatchParams.PriorityCalculationConstants);

	// Positions 必须先清零：尾部 4 倍数填充使用 VectorZero，但 memset 整批更稳妥（避免 SIMD 读到脏值）。
	FMemory::Memzero(OutBatchParams.Positions, MaxBatchObjectCount*sizeof(VectorRegister));
}

void USphereNetObjectPrioritizer::SetupCalculationConstants(FPriorityCalculationConstants& OutConstants)
{
	// 把 Config 中的标量"广播"成 4-wide 向量（VectorSetFloat1 = (x,x,x,x)），整批复用。
	const VectorRegister InnerRadius = VectorSetFloat1(Config->InnerRadius);
	const VectorRegister OuterRadius = VectorSetFloat1(Config->OuterRadius);
	const VectorRegister InnerPriority = VectorSetFloat1(Config->InnerPriority);
	const VectorRegister OuterPriority = VectorSetFloat1(Config->OuterPriority);
	const VectorRegister OutsidePriority = VectorSetFloat1(Config->OutsidePriority);

	const VectorRegister RadiusDiff = VectorSubtract(OuterRadius, InnerRadius);
	const VectorRegister PriorityDiff = VectorSubtract(OuterPriority, InnerPriority);

	OutConstants.InnerRadius = InnerRadius;
	OutConstants.OuterRadius = OuterRadius;
	OutConstants.RadiusDiff = RadiusDiff;
	// VectorReciprocalAccurate：高精度 1/x（避免线性插值在边界出现可见跳变）。
	OutConstants.InvRadiusDiff = VectorReciprocalAccurate(RadiusDiff);
	OutConstants.InnerPriority = InnerPriority;
	OutConstants.OuterPriority = OuterPriority;
	OutConstants.OutsidePriority = OutsidePriority;
	OutConstants.PriorityDiff = PriorityDiff;
}
