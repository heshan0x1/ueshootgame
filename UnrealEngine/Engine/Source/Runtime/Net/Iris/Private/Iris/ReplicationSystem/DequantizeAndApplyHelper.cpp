// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：DequantizeAndApplyHelper.cpp
// 模块：Iris / ReplicationSystem
// 功能：FDequantizeAndApplyHelper 实现——客户端把"内部量化态 → 外部状态"
//       并 Apply 到 UObject 的统一入口。
//
// 关键步骤摘要（详见各函数注释）：
//   * Initialize：解 ChangeMask、Dequantize 每个脏 state 到临时 ExternalBuffer、
//                 收集需要 PreNetReceive/PostNetReceive/RepNotify 的 owner。
//   * ApplyAndCallLegacyPreApplyFunction：PreNetReceive + Fragment.ApplyReplicatedState。
//   * CallLegacyPostApplyFunctions：PostNetReceive + CallRepNotifies + PostRepNotifies。
//   * Deinitialize：析构临时 ExternalBuffer。
//
// 错误处理：所有错误经 FNetSerializationContext::SetError 上报，由 Reader
// 决策是否丢 batch / 关连接。Validate 失败发生在 Serializer 层面，本 helper
// 只是被动接收已 Deserialize 的 SrcStateBuffer。
// =====================================================================================

#include "DequantizeAndApplyHelper.h"
#include "Containers/ArrayView.h"
#include "HAL/IConsoleManager.h"
#include "HAL/LowLevelMemStats.h"
#include "Iris/Core/IrisProfiler.h"
#include "Net/Core/NetBitArray.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Iris/ReplicationState/ReplicationStateUtil.h"
#include "Iris/ReplicationSystem/ReplicationFragment.h"
#include "Iris/ReplicationSystem/ReplicationOperations.h"
#include "Iris/ReplicationSystem/ReplicationProtocol.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetSerializer.h"
#include "Misc/MemStack.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/AssetMetadataTrace.h"
#include "UObject/Package.h"

namespace UE::Net::Private
{

// 中文：调试 cvar——开启后强制走"全量 Dequantize"（忽略 PartialDequantize trait）。
// 用于排查"PartialDequantize 是否漏处理某些成员"之类的 bug。
static bool bCVarForceFullDequantizeAndApply = false;
static FAutoConsoleVariableRef CVarForceFullDequantizeAndApply(
	TEXT("net.iris.ForceFullDequantizeAndApply"),
	bCVarForceFullDequantizeAndApply,
	TEXT("When enabled a full dequantize of dirty states will be used when applying received statedata regardless of traits set in the fragments."));

/**
 * FContext —— Initialize 输出的"已脏 state 缓存清单"。
 * 由 FMemStackBase 在 Initialize 中分配，FMemMark 退出时整段回收。
 */
struct FDequantizeAndApplyHelper::FContext
{
	struct FStateData
	{
		// 中文：传给 Fragment::ApplyReplicatedState 的 buffer 信息：
		//   非持久化：ExternalStateBuffer = 本 helper 临时分配并 Dequantize 后的副本。
		//   持久化：    RawStateBuffer = 内部 Buffer 直接指针 + ChangeMaskData。
		FReplicationStateApplyContext::FStateBufferData StateBufferData;
		const FReplicationStateDescriptor* Descriptor;
		FReplicationFragment* Fragment;
		EReplicationFragmentTraits FragmentTraits;
		// 中文：本 state 是否包含未解析引用（用于在 Apply 时给 fragment 透传"延迟 Resolve"信息）。
		bool bHasUnresolvableReferences;
		// 中文：仅 IsInit 时有效——init-only 引用是否可能未解析。
		bool bMightHaveUnresolvableInitReferences;
	};

	// 中文：脏 state 数组（容量 = Protocol->ReplicationStateCount，未必填满）。
	FStateData* CachedStateData;
	// 中文：legacy 回调时收集 owner（Object*）的辅助器；仅 fragment 需要时才分配。
	FReplicationStateOwnerCollector* OwnerCollector;
	const uint32* UnresolvedReferencesChangeMaskData;
	uint32 CachedStateCount;
	// 中文：实例是否需要 PreNetReceive/PostNetReceive/RepNotify（来自 InstanceTraits）。
	uint32 bNeedsLegacyCallbacks : 1;
	// 中文：本 batch 是否为对象的 Init batch（影响 Apply 标志位）。
	uint32 bIsInitialState : 1;
};

// 中文：对所有 owner 调用 Functor 的小工具——附带 LLM/Trace 元数据 Scope，
// 让内存与 Trace 工具能把 PreNetReceive/RepNotify 等回调归到对象 asset 上。
template<typename T>
static void CallLegacyFunctionForEachOwner(const FDequantizeAndApplyHelper::FContext& Context, T&& Functor)
{
	if (Context.OwnerCollector)
	{
		for (UObject* Object : MakeArrayView(Context.OwnerCollector->GetOwners(), Context.OwnerCollector->GetOwnerCount()))
		{
			LLM_SCOPE_BYNAME(TEXT("UObject/NetworkReceive"));
			LLM_SCOPE_DYNAMIC_STAT_OBJECTPATH(Object->GetPackage(), ELLMTagSet::Assets);
			LLM_SCOPE_DYNAMIC_STAT_OBJECTPATH(Object->GetClass(), ELLMTagSet::AssetClasses);
			UE_TRACE_METADATA_SCOPE_ASSET(Object, Object->GetClass());
			Functor(Object);
		}
	}
}

// ------------------------------------------------------------------------------------
// Initialize
// ------------------------------------------------------------------------------------
// 中文：核心初始化逻辑——
//   1. 在 Allocator（MemStack）上分配 FContext + FStateData[]；
//   2. 用 NetBitStreamReader 把扁平 ChangeMaskData 切成 per-state 子位图；
//   3. 对每个有脏的 state：
//        a. 非持久化 fragment：分配 ExternalStateBuffer → ConstructReplicationState → Dequantize；
//           IsInit 或 ForceFullDequantize 走全量；否则走 DequantizeWithMask（仅脏成员）。
//        b. 持久化 fragment：直接指向内部 Buffer（不再多拷一次）。
//   4. 计算 Unresolved 标记（含 init-only 区分）。
//   5. 若 InstanceTraits 标 NeedsLegacyCallbacks → 收集 owner 列表用于 Pre/Post/RepNotify。
// ------------------------------------------------------------------------------------
FDequantizeAndApplyHelper::FContext* FDequantizeAndApplyHelper::Initialize(FNetSerializationContext& NetSerializationContext, const FDequantizeAndApplyParameters& Parameters)
{
	IRIS_PROFILER_SCOPE(FDequantizeAndApplyHelper_Init);

	FMemStackBase& Allocator = *Parameters.Allocator;

	// Allocate context from temp allocator
	// 中文：placement new 到 MemStack；MEM_Zeroed 保证位段全 0。
	FContext* Context = new (Allocator, MEM_Zeroed) FContext;

	checkSlow(Context);

	const FReplicationInstanceProtocol* InstanceProtocol = Parameters.InstanceProtocol;
	const FReplicationProtocol* Protocol = Parameters.Protocol;
	const uint32* UnresolvedChangeMaskData = Parameters.UnresolvedReferencesChangeMaskData;
	
	// Allocate memory for our temporary cache
	// 中文：StateData 数组按协议状态数预留——多数情况下只有一部分会被填，
	// 但一次性数组可避免按需扩容。
	FContext::FStateData* CachedStateData = new (Allocator, MEM_Zeroed) FContext::FStateData[Protocol->ReplicationStateCount];
	uint32 CachedStateCount = 0U;
	
	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;
	FReplicationFragment* const * Fragments = InstanceProtocol->Fragments;

	// We need to accumulate the offsets and alignment of each internal state
	// 中文：内部 buffer 中各 state 是按"InternalAlignment 对齐 + InternalSize 跨步"
	// 紧密排布的；这里手工游标式推进。
	const uint8* CurrentInternalStateBuffer = Parameters.SrcObjectStateBuffer;

	// We use this to extract the change mask for each state in the protocol
	// 中文：把扁平 ChangeMaskData 包成 BitStreamReader，逐 state ReadBitStream 切片。
	FNetBitStreamReader ChangeMaskReader;
	ChangeMaskReader.InitBits(Parameters.ChangeMaskData, Protocol->ChangeMaskBitCount);
	uint32 CurrentChangeMaskBitOffset = 0;

	for (uint32 StateIt = 0, StateEndIt = Protocol->ReplicationStateCount; StateIt != StateEndIt; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];

		// 中文：按 Descriptor 的 InternalAlignment 对齐当前游标。
		CurrentInternalStateBuffer = Align(CurrentInternalStateBuffer, CurrentDescriptor->InternalAlignment);

		const uint32 ChangeMaskBitCount = CurrentDescriptor->ChangeMaskBitCount;

		// Unpack change mask
		// 中文：分配本 state 的 ChangeMask 字数组，从位流读出对应位段。
		// ClearPaddingBits 保证最尾字内未用位为 0，避免 IsAnyBitSet 误判。
		FNetBitArrayView::StorageWordType* ChangeMaskStorage = new (Allocator) FNetBitArrayView::StorageWordType[FNetBitArrayView::CalculateRequiredWordCount(ChangeMaskBitCount)];
		FNetBitArrayView ChangeMask(ChangeMaskStorage, ChangeMaskBitCount, FNetBitArrayView::NoResetNoValidate);
		ChangeMask.ClearPaddingBits();
		ChangeMaskReader.ReadBitStream(ChangeMaskStorage, ChangeMaskBitCount);

		// Cache all ReplicationStates with dirty changes
		// 中文：决定是否需要 Dequantize 该 state——
		//   * Init 状态：即使没脏位也要全量 Apply（首次初始化）；
		//   * 否则：仅当 ChangeMask 有任意脏位时才处理。
		const bool bIsInitState = NetSerializationContext.IsInitState() && CurrentDescriptor->IsInitState();
		const bool bShouldDequantizeState = ChangeMask.IsAnyBitSet() || bIsInitState;
		if (bShouldDequantizeState)
		{
			FContext::FStateData& StateData = CachedStateData[CachedStateCount];
			StateData.Descriptor = CurrentDescriptor;
			StateData.Fragment = Fragments[StateIt];
			StateData.FragmentTraits = StateData.Fragment->GetTraits();

			// Dequantize state data
			if (!EnumHasAnyFlags(StateData.Fragment->GetTraits(), EReplicationFragmentTraits::HasPersistentTargetStateBuffer))
			{
				// allocate buffer for temporary state and construct the external state
				// 中文：非持久化 fragment——分配临时外部 buffer 并构造（C++ 默认构造 + 内部
				// FPropertyReplicationState 的 RepNotify 标记区初始化）。
				uint8* StateBuffer = (uint8*)Allocator.Alloc(CurrentDescriptor->ExternalSize, CurrentDescriptor->ExternalAlignment);
				CurrentDescriptor->ConstructReplicationState(StateBuffer, CurrentDescriptor);

				// Inject ChangeMask
				// 中文：把 ChangeMask 写入外部 buffer 的 MemberChangeMask 区段——
				// PropertyReplicationState 的 RepNotify 触发判定要用它。
				FNetBitArrayView DestChangeMask = Private::GetMemberChangeMask(StateBuffer, CurrentDescriptor);
				DestChangeMask.Copy(ChangeMask);
	
				// Dequantize state data, if the fragment supports partial dequantize we will only dequantize dirty members
				// 中文：决定全量还是按脏位 Dequantize：
				//   * Init / cvar 强制 / fragment 不支持 partial → 全量；
				//   * 否则按 ChangeMask 跳过未脏成员（节省 CPU，特别是大型 struct）。
				const bool bUseFullDequantizeAndApply = bIsInitState || bCVarForceFullDequantizeAndApply || !EnumHasAnyFlags(StateData.Fragment->GetTraits(), EReplicationFragmentTraits::SupportsPartialDequantizedState);
				if (bUseFullDequantizeAndApply)
				{
					FReplicationStateOperations::Dequantize(NetSerializationContext, StateBuffer, (uint8*)CurrentInternalStateBuffer, CurrentDescriptor);
				}
				else
				{
					NetSerializationContext.SetChangeMask(&ChangeMask);
					FReplicationStateOperations::DequantizeWithMask(NetSerializationContext, ChangeMask, 0U, StateBuffer, (uint8*)CurrentInternalStateBuffer, CurrentDescriptor);
				}

				StateData.StateBufferData.ExternalStateBuffer = StateBuffer;
			}
			else
			{
				// 中文：持久化 fragment —— 不复制；直接传内部 buffer 指针 + ChangeMask 字。
				// fragment 自行决定怎么用（例如 FastArray 直接读内部态）。
				StateData.StateBufferData.ChangeMaskData = ChangeMaskStorage;
				StateData.StateBufferData.RawStateBuffer = CurrentInternalStateBuffer;
			}

			// Check if we have or might have unresolvable references
			// 中文：根据 Descriptor 是否含对象引用 + UnresolvedChangeMask，
			// 计算本 state 的"未解析"标记。两类：
			//   * bHasUnresolvableReferences：当前帧内有"已知尚未解析"的 ref。
			//   * bMightHaveUnresolvableInitReferences：init-only 引用尚未解析（仅 IsInit 时）。
			bool bHasUnresolvableReferences = false;
			bool bMightHaveUnresolvableInitReferences = false;
			if (CurrentDescriptor->HasObjectReference())
			{
				if (UnresolvedChangeMaskData)
				{
					const FNetBitArrayView UnresolvedChangeMask = MakeNetBitArrayView(UnresolvedChangeMaskData, Protocol->ChangeMaskBitCount);
					bHasUnresolvableReferences = UnresolvedChangeMask.IsAnyBitSet(CurrentChangeMaskBitOffset, CurrentDescriptor->ChangeMaskBitCount);
				}
				bMightHaveUnresolvableInitReferences = Parameters.bHasUnresolvedInitReferences && NetSerializationContext.IsInitState() && CurrentDescriptor->IsInitState();
			}

			StateData.bHasUnresolvableReferences = bHasUnresolvableReferences;
			StateData.bMightHaveUnresolvableInitReferences = bMightHaveUnresolvableInitReferences;

			++CachedStateCount;
		}

		// 中文：游标推进——按 InternalSize 跨步，进入下一个 state（注意这是非脏 state 也走的路径）。
		CurrentInternalStateBuffer += CurrentDescriptor->InternalSize;
		// 中文：ChangeMask 偏移按 Descriptor 的 ChangeMaskBitCount 推进——保证下一 state
		// 计算 Unresolved 时位偏移正确。
		CurrentChangeMaskBitOffset += CurrentDescriptor->ChangeMaskBitCount;
	}

	Context->CachedStateData = CachedStateData;
	Context->CachedStateCount = CachedStateCount;
	Context->UnresolvedReferencesChangeMaskData = Parameters.UnresolvedReferencesChangeMaskData;
	Context->bNeedsLegacyCallbacks = EnumHasAnyFlags(InstanceProtocol->InstanceTraits, EReplicationInstanceProtocolTraits::NeedsLegacyCallbacks);
	Context->bIsInitialState = NetSerializationContext.IsInitState();

	// Collect all states requiring legacy callbacks
	// 中文：收集"需要 PreNetReceive / PostNetReceive / RepNotify 的 owner"。
	// fragment 自身知道自己代表哪些 owner（通常是 1 个 UObject，但 sub-state 可能多个）。
	if (Context->bNeedsLegacyCallbacks)
	{
		// Extract owners for all fragments requiring legacy callbacks
		UObject** Owners = (UObject**)Allocator.Alloc(InstanceProtocol->FragmentCount * sizeof(UObject*), alignof(UObject*));
		Context->OwnerCollector = new (Allocator) FReplicationStateOwnerCollector(Owners, InstanceProtocol->FragmentCount);

		for (const FContext::FStateData& StateData : MakeArrayView(CachedStateData, CachedStateCount))
		{
			if (EnumHasAnyFlags(StateData.Fragment->GetTraits(), EReplicationFragmentTraits::NeedsLegacyCallbacks))
			{
				StateData.Fragment->CollectOwner(Context->OwnerCollector);
			}
		}
	}

	return Context;
}

// ------------------------------------------------------------------------------------
// Deinitialize
// ------------------------------------------------------------------------------------
// 中文：仅做 C++ 析构（非持久化 fragment 的 ExternalState）。
// 内存本身由 MemStack 在 FMemMark 退出时整段释放，本函数不调用 Free。
// 注意：使用 StateData.FragmentTraits 而非 StateData.Fragment->GetTraits()，
// 因为 PostNetReceive/RPC 等 ApplyReplicatedState 期间可能销毁 fragment。
// ------------------------------------------------------------------------------------
void FDequantizeAndApplyHelper::Deinitialize(FDequantizeAndApplyHelper::FContext* Context)
{
	if (Context)
	{
		for (const FContext::FStateData& StateData : MakeArrayView(Context->CachedStateData, Context->CachedStateCount))
		{
			// Note: We do not use the fragment directly here as there are some code paths that destroys replicated instance & associated Fragments from PostNetReceive/RPC`s
			if (!EnumHasAnyFlags(StateData.FragmentTraits, EReplicationFragmentTraits::HasPersistentTargetStateBuffer))
			{
				StateData.Descriptor->DestructReplicationState(StateData.StateBufferData.ExternalStateBuffer, StateData.Descriptor);
			}
		}
	}
}

// ------------------------------------------------------------------------------------
// ApplyAndCallLegacyPreApplyFunction
// ------------------------------------------------------------------------------------
// 中文：第一段 Apply——
//   1. 对每个 owner 调 PreNetReceive；
//   2. 对每个脏 state 调 Fragment::ApplyReplicatedState（写入 UObject 真实属性）。
// 不在此处发 RepNotify——延后到 CallLegacyPostApplyFunctions，让调用方
// 有机会先做"等引用解析"等额外处理。
// ------------------------------------------------------------------------------------
void FDequantizeAndApplyHelper::ApplyAndCallLegacyPreApplyFunction(FContext* Context, FNetSerializationContext& NetSerializationContext)
{
	IRIS_PROFILER_SCOPE(FDequantizeAndApplyHelper_Apply);

	checkSlow(Context);

	// Call PreNetReceive (if needed)
	CallLegacyFunctionForEachOwner(*Context, [](UObject* Object) { Object->PreNetReceive(); });

	// Apply the state
	for (const FContext::FStateData& StateData : MakeArrayView(Context->CachedStateData, Context->CachedStateCount))
	{
		FReplicationStateApplyContext SetStateContext;
		SetStateContext.NetSerializationContext = &NetSerializationContext;
		SetStateContext.Descriptor = StateData.Descriptor;
		SetStateContext.StateBufferData = StateData.StateBufferData;
		SetStateContext.bIsInit = Context->bIsInitialState;
		SetStateContext.bHasUnresolvableReferences = StateData.bHasUnresolvableReferences;
		SetStateContext.bMightHaveUnresolvableInitReferences = StateData.bMightHaveUnresolvableInitReferences;

		// 中文：fragment 自己负责把 ExternalState/RawState 应用到目标 UObject——
		// PropertyReplicationFragment 用反射 Copy；FastArrayReplicationFragment
		// 走 FFastArraySerializer::PostReplicatedReceive。
		StateData.Fragment->ApplyReplicatedState(SetStateContext);
	}
}

// ------------------------------------------------------------------------------------
// ApplyAndCallLegacyFunctions
// ------------------------------------------------------------------------------------
// 中文：单步完成 PreApply + PostApply 两段。等价于：
//   ApplyAndCallLegacyPreApplyFunction(...) + CallLegacyPostApplyFunctions(...)
// 是最常用的接收路径（不涉及 PendingBatchData 延迟）。
// ------------------------------------------------------------------------------------
void FDequantizeAndApplyHelper::ApplyAndCallLegacyFunctions(FContext* Context, FNetSerializationContext& NetSerializationContext)
{
	IRIS_PROFILER_SCOPE(FDequantizeAndApplyHelper_ApplyAndCallLegacyFunctions);

	ApplyAndCallLegacyPreApplyFunction(Context, NetSerializationContext);
	CallLegacyPostApplyFunctions(Context, NetSerializationContext);
}

// ------------------------------------------------------------------------------------
// CallLegacyPostApplyFunctions
// ------------------------------------------------------------------------------------
// 中文：第二段——
//   1. 对每个 owner 调 PostNetReceive；
//   2. 对带 HasRepNotifies trait 的 fragment 调 CallRepNotifies（实际触发 OnRep_xxx）；
//   3. 对每个 owner 调 PostRepNotifies。
//
// CSV 计时项 RepNotifies 用于评估 RepNotify 阶段开销（含玩家自己的 OnRep）。
// ------------------------------------------------------------------------------------
void FDequantizeAndApplyHelper::CallLegacyPostApplyFunctions(FContext* Context, FNetSerializationContext& NetSerializationContext)
{
	IRIS_PROFILER_SCOPE(FDequantizeAndApplyHelper_CallLegacyPostApplyFunctions);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(RepNotifies);

	checkSlow(Context);

	// Call PostNetReceive (if needed)
	CallLegacyFunctionForEachOwner(*Context, [](UObject* Object) { Object->PostNetReceive(); });

	// CallRepNotifies
	if (Context->bNeedsLegacyCallbacks)
	{
		// We only call rep notifies for states that have received any data
		// 中文：只对脏 state 发 RepNotify——内部由 Fragment 比对 MemberChangeMask 决定哪些
		// OnRep_xxx 真正触发，避免误触发未变更属性的回调。
		for (const FContext::FStateData& StateData : MakeArrayView(Context->CachedStateData, Context->CachedStateCount))
		{
			if (EnumHasAnyFlags(StateData.Fragment->GetTraits(), EReplicationFragmentTraits::HasRepNotifies))
			{
				FReplicationStateApplyContext SetStateContext;
				SetStateContext.NetSerializationContext = &NetSerializationContext;
				SetStateContext.Descriptor = StateData.Descriptor;
				SetStateContext.StateBufferData = StateData.StateBufferData;
				SetStateContext.bIsInit = Context->bIsInitialState;

				StateData.Fragment->CallRepNotifies(SetStateContext);
			}
		}
	}

	// Call PostRepNotifies
	CallLegacyFunctionForEachOwner(*Context, [](UObject* Object) { Object->PostRepNotifies(); });
}

}
