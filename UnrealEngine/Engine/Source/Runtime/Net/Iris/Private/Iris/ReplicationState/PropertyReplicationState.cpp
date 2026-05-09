// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/ReplicationState/PropertyReplicationState.h"
#include "Iris/Core/IrisDebugging.h"
#include "Iris/Core/IrisLog.h"
#include "Iris/Core/IrisProfiler.h"
#include "Iris/ReplicationState/InternalPropertyReplicationState.h"
#include "Iris/ReplicationState/InternalReplicationStateDescriptorUtils.h"
#include "Iris/ReplicationState/ReplicationStateUtil.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Net/Core/NetBitArray.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Net/Core/Trace/NetDebugName.h"
#include "CoreTypes.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "Containers/StringFwd.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Iris/IrisConfigInternal.h"

// =============================================================================================
// 中文说明：FPropertyReplicationState 的核心业务逻辑实现
// 主要包含三大类操作：
//   ┌─ Poll 路径（Server / Source 端）：把 UObject 原生属性值同步到内部 StateBuffer
//   │  ├─ PollPropertyReplicationState         全量 poll（性能最差，全部 UPROPERTY）
//   │  ├─ PollPushBasedPropertyReplicationState 按 PushModel pollmask 选择性 poll（主流路径）
//   │  └─ PollObjectReferences                  仅 poll ObjectReference 字段（unresolved 重试）
//   ├─ Push 路径（Client / Target 端）：把 StateBuffer 写回 UObject 原生属性
//   │  └─ PushPropertyReplicationState         可选 bPushAll，附带 MARK_PROPERTY_DIRTY_UNSAFE
//   └─ RepNotify 三阶段：
//      ① StoreCurrentPropertyReplicationStateForRepNotifies  在 push 之前快照旧值
//      ② PushPropertyReplicationState                         应用新值
//      ③ CallRepNotifies                                      触发 RepNotify UFUNCTION
// =============================================================================================

DEFINE_LOG_CATEGORY_STATIC(LogIrisRepNotify, Warning, All);

namespace UE::Net
{

// 中文：构造函数（外部所有权）——从已构造好的 InStateBuffer 注入。析构时不释放该缓冲区。
FPropertyReplicationState::FPropertyReplicationState(const FReplicationStateDescriptor* Descriptor, uint8* InStateBuffer)
: ReplicationStateDescriptor(Descriptor)
, StateBuffer(nullptr)
, bOwnState(0u)
{
	check(InStateBuffer);
	InjectState(Descriptor, InStateBuffer);
}

// 中文：构造函数（自有所有权）——按 Descriptor 自行 Malloc + 构造 StateBuffer。
FPropertyReplicationState::FPropertyReplicationState(const FReplicationStateDescriptor* Descriptor)
: ReplicationStateDescriptor(Descriptor)
, StateBuffer(nullptr)
, bOwnState(1u)
{
	ConstructStateInternal();
}

/** Copy constructor, will not copy internal data */
// 中文：拷贝构造——分配并构造一份独立的 StateBuffer，再通过 operator= 把数据复制过来。
//       不会复制"绑定关系"（Header.IsBound 状态），新 state 是 loose 的。
FPropertyReplicationState::FPropertyReplicationState(const FPropertyReplicationState& Other)
: ReplicationStateDescriptor(Other.ReplicationStateDescriptor)
, StateBuffer(nullptr)
, bOwnState(1u)
{
	ConstructStateInternal();

	// Invoke assignment operator
	*this = Other;
}

// 中文：赋值——根据当前 state 是否 bound 走两条不同路径（详见 .h 注释）。
FPropertyReplicationState& 
FPropertyReplicationState::operator=(const FPropertyReplicationState& Other)
{
	check(this != &Other && IsValid());
	check(ReplicationStateDescriptor.GetReference() == Other.ReplicationStateDescriptor.GetReference());

	if (!Private::IsReplicationStateBound(StateBuffer, ReplicationStateDescriptor.GetReference()))
	{
		// 中文：未绑定 → 直接整体覆盖（包括 ChangeMask）。
		Private::CopyPropertyReplicationState(StateBuffer, Other.StateBuffer, ReplicationStateDescriptor.GetReference());
	}
	else
	{
		// 中文：已绑定 → 走 Set，逐成员 compare + copy 以维护 dirty 跟踪。
		Set(Other);
	}

	return *this;
}

// 中文：析构——只在 bOwnState=1 时 Destruct + Free。
FPropertyReplicationState::~FPropertyReplicationState()
{
	DestructStateInternal();
}

bool FPropertyReplicationState::IsValid() const
{
	return StateBuffer && ReplicationStateDescriptor;
}

bool FPropertyReplicationState::IsInitState() const
{
	return IsValid() && ReplicationStateDescriptor->IsInitState();
}

// 中文：state 是否需要 poll——已 dirty 或 PushModel pollmask 任一位被置。
bool FPropertyReplicationState::IsDirtyForPolling() const
{
	return IsDirty() || Private::GetMemberPollMask(StateBuffer, ReplicationStateDescriptor).IsAnyBitSet();
}

// 中文：state 整体是否 dirty——读 Header 上的 StateDirty / InitStateDirty 位（注意：
//       Header 只有 2 bit dirty 标志，跟具体哪个成员 dirty 是分开的——后者由 ChangeMask 跟踪）。
bool FPropertyReplicationState::IsDirty() const
{
	if (IsValid())
	{
		const FReplicationStateHeader& Header = Private::GetReplicationStateHeader(StateBuffer, ReplicationStateDescriptor);
		return Private::FReplicationStateHeaderAccessor::GetIsStateDirty(Header) || Private::FReplicationStateHeaderAccessor::GetIsInitStateDirty(Header);
	}

	return false;
}

// 中文：Owns 路径——分配 StateBuffer 内存并调用 Construct 进行初始化。
//       Memzero 是必须的：某些 InitializeValue 实现假设输入内存为 0。
void FPropertyReplicationState::ConstructStateInternal()
{
	// allocate memory
	check(StateBuffer == nullptr);

	StateBuffer = (uint8*)FMemory::Malloc(ReplicationStateDescriptor->ExternalSize, ReplicationStateDescriptor->ExternalAlignment);
	// There can be properties in here that assume the memory is cleared and do nothing in InitializeValue
	FMemory::Memzero(StateBuffer, ReplicationStateDescriptor->ExternalSize);

	// Construct the state
	Private::ConstructPropertyReplicationState(StateBuffer, ReplicationStateDescriptor);
}

// 中文：Owns 路径配套的析构——Destruct 后 Free，再清空指针。
void FPropertyReplicationState::DestructStateInternal()
{
	// Destruct state and free memory if we own it
	if (bOwnState && StateBuffer)
	{
		Private::DestructPropertyReplicationState(StateBuffer, ReplicationStateDescriptor);
		FMemory::Free(StateBuffer);
		StateBuffer = nullptr;
	}
}

// 中文：注入式绑定——直接持有外部已构造好的 StateBuffer。
//       调用方负责该缓冲区的生命周期（典型场景：来自 NetRefHandleManager 或 ReplicationFragment）。
void FPropertyReplicationState::InjectState(const FReplicationStateDescriptor* Descriptor, uint8* InStateBuffer)
{
	// allocate memory
	check(StateBuffer == nullptr);
	check(InStateBuffer != nullptr);

	ReplicationStateDescriptor = Descriptor;
	StateBuffer = InStateBuffer;
}

// 中文：从 Other 逐成员"Set"到自身——用 SetPropertyValue（即 PollPropertyValue 路径）
//       做"比较 + 拷贝 + 标 dirty"，以保证已绑定 state 的 dirty 跟踪正确。
void FPropertyReplicationState::Set(const FPropertyReplicationState& Other)
{
	if (IsValid() && this != &Other)
	{
		const FReplicationStateDescriptor* Descriptor = ReplicationStateDescriptor;
		const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;

		const uint8* SrcStateBuffer = Other.StateBuffer;
		for (uint32 MemberIt = 0, MemberEndIt = Descriptor->MemberCount; MemberIt < MemberEndIt; ++MemberIt)
		{
			const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
			const uint8* SrcValue = SrcStateBuffer + MemberDescriptor.ExternalMemberOffset;
			SetPropertyValue(MemberIt, SrcValue);
		}
	}
}

// 中文：单成员 Poll 的核心实现——
//   1) 顶层 TArray + 启用 element changemask（ChangeMaskInfo.BitCount > 1，且非 InitState
//      且 Custom Condition 启用）：调用 InternalCompareAndCopyArrayWithElementChangeMask，
//      它会同时维护元素 changemask 位；如果数组发生变化但"数组总位"还未置，则 MarkArrayDirty；
//   2) 普通成员：先 IsDirty 短路（已 dirty 就跳过比较以省 CPU），再 InternalCompareMember；
//      不同则 MarkDirty + InternalCopyPropertyValue 拷贝（覆盖到 StateBuffer 中的外部表示）。
void FPropertyReplicationState::PollPropertyValue(uint32 Index, const void* SrcValue)
{
	const FReplicationStateDescriptor* Descriptor = ReplicationStateDescriptor;
	void* DstValue = StateBuffer + Descriptor->MemberDescriptors[Index].ExternalMemberOffset;

	// Special handling for top level arrays with changemask bits for elements
	const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = Descriptor->MemberSerializerDescriptors[Index];
	if (IsUsingArrayPropertyNetSerializer(MemberSerializerDescriptor))
	{
		const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskInfo = Descriptor->MemberChangeMaskDescriptors[Index];
		if (!IsInitState() && IsCustomConditionEnabled(Index) && ChangeMaskInfo.BitCount > 1U)
		{
			FNetBitArrayView MemberChangeMask = Private::GetMemberChangeMask(StateBuffer, Descriptor);

			const bool bArraysAreEqual = Private::InternalCompareAndCopyArrayWithElementChangeMask(Descriptor, Index, DstValue, SrcValue, MemberChangeMask);
			if (!bArraysAreEqual && !MemberChangeMask.GetBit(ChangeMaskInfo.BitOffset + TArrayPropertyChangeMaskBitIndex))
			{
				// 中文：数组确实变了但"数组本身已 dirty"位（bit 0）还没被置——补上。
				MarkArrayDirty(Index);
			}

			return;
		}
	}

	// 中文：普通成员路径——只在"未 dirty 且条件启用且新旧值不同"时才 MarkDirty。
	if (!IsDirty(Index) && IsCustomConditionEnabled(Index) && !Private::InternalCompareMember(Descriptor, Index, SrcValue, DstValue))
	{
		MarkDirty(Index);
	}

	Private::InternalCopyPropertyValue(Descriptor, Index, DstValue, SrcValue);
}

// 中文：测试代码用——逻辑上等价于 PollPropertyValue（同样会 compare + 必要时标 dirty + 拷贝）。
void FPropertyReplicationState::SetPropertyValue(uint32 Index, const void* SrcValue)
{
	// We can perform the same operation as normal polling of the state does.
	PollPropertyValue(Index, SrcValue);
}

// 中文：单成员 Push 的核心——把 StateBuffer 中的成员值 Apply 到目标 DstValue（UObject 内存）。
//       之所以用 InternalApplyPropertyValue 而非 InternalCopyPropertyValue：是因为某些
//       Serializer（如 ObjectReference）有 Apply side-effects（解引用、unresolved 处理等）。
void FPropertyReplicationState::PushPropertyValue(uint32 Index, void* DstValue) const
{
	void* SrcValue = StateBuffer + ReplicationStateDescriptor->MemberDescriptors[Index].ExternalMemberOffset;
	Private::InternalApplyPropertyValue(ReplicationStateDescriptor, Index, DstValue, SrcValue);
}

// 中文：测试用 Get——从 StateBuffer 取出某成员值写到 DstValue。
//       注意这里用 InternalCopyPropertyValue（不带 Apply），因为只是想读取数据。
void FPropertyReplicationState::GetPropertyValue(uint32 Index, void* DstValue) const
{
	void* SrcValue = StateBuffer + ReplicationStateDescriptor->MemberDescriptors[Index].ExternalMemberOffset;
	Private::InternalCopyPropertyValue(ReplicationStateDescriptor, Index, DstValue, SrcValue);
}

// 中文：MarkDirty 实现——
//   - InitState：用 Header 上的 InitStateDirty 位代替 ChangeMask（InitState 不设 ChangeMask）；
//     首次置脏时若 IsBound 还要通知 NetObjectStateHeader 为 dirty（通知 DirtyNetObjectTracker）；
//   - 非 InitState：调用 Private::MarkDirty 把成员的全部 ChangeMask 位置 1，
//     若是首次置脏（ChangeMask 该位原为 0）且 IsBound 还会触发 Header.MarkStateDirty
//     + MarkNetObjectStateHeaderDirty——把 NetObject 加入待复制列表。
void FPropertyReplicationState::MarkDirty(uint32 Index)
{
	const FReplicationStateDescriptor* Descriptor = ReplicationStateDescriptor;
	FReplicationStateHeader& Header = Private::GetReplicationStateHeader(StateBuffer, Descriptor);

	if (IsInitState())
	{
		if (!Private::FReplicationStateHeaderAccessor::GetIsInitStateDirty(Header))
		{
			Private::FReplicationStateHeaderAccessor::MarkInitStateDirty(Header);
			if (Header.IsBound())
			{
				MarkNetObjectStateHeaderDirty(Header);
			}
		}
	}
	else
	{
		FNetBitArrayView MemberChangeMask = Private::GetMemberChangeMask(StateBuffer, Descriptor);
		const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskInfo = Descriptor->MemberChangeMaskDescriptors[Index];
		Private::MarkDirty(Header, MemberChangeMask, ChangeMaskInfo);
	}
}

// 中文：单成员是否需要 poll——已 dirty 或 PushModel pollmask 该位被置（PushModel 把脏位写到
//       这个 pollmask，poll 阶段据此决定要不要拉取）。
bool FPropertyReplicationState::IsDirtyForPolling(uint32 Index) const
{
	return IsDirty(Index) || Private::GetMemberPollMask(StateBuffer, ReplicationStateDescriptor).GetBit(Index);
}

// 中文：单成员 IsDirty——InitState 看 Header.InitStateDirty；非 Init 看 ChangeMask 该成员所占位段。
bool FPropertyReplicationState::IsDirty(uint32 Index) const
{
	const FReplicationStateDescriptor* Descriptor = ReplicationStateDescriptor;
	if (IsInitState())
	{
		const FReplicationStateHeader& Header = Private::GetReplicationStateHeader(StateBuffer, Descriptor);
		return Private::FReplicationStateHeaderAccessor::GetIsInitStateDirty(Header);
	}
	else
	{
		FNetBitArrayView MemberChangeMask = Private::GetMemberChangeMask(StateBuffer, Descriptor);
		const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskInfo = Descriptor->MemberChangeMaskDescriptors[Index];

		return MemberChangeMask.IsAnyBitSet(ChangeMaskInfo.BitOffset, ChangeMaskInfo.BitCount);
	}
}

// 中文：MarkArrayDirty —— 仅对 TArray 成员的"数组本身已变"位（TArrayPropertyChangeMaskBitIndex=0）
//       置 1。BitCount=1 表示只置这一位，不影响 element bits。
void FPropertyReplicationState::MarkArrayDirty(uint32 Index)
{
	const FReplicationStateDescriptor* Descriptor = ReplicationStateDescriptor;
	FReplicationStateHeader& Header = Private::GetReplicationStateHeader(StateBuffer, Descriptor);

	if (IsInitState())
	{
		if (!Private::FReplicationStateHeaderAccessor::GetIsInitStateDirty(Header))
		{
			Private::FReplicationStateHeaderAccessor::MarkInitStateDirty(Header);
			if (Header.IsBound())
			{
				MarkNetObjectStateHeaderDirty(Header);
			}
		}
	}
	else
	{
		FNetBitArrayView MemberChangeMask = Private::GetMemberChangeMask(StateBuffer, Descriptor);
		FReplicationStateMemberChangeMaskDescriptor ChangeMaskInfo = Descriptor->MemberChangeMaskDescriptors[Index];
		// 中文：临时改写 ChangeMaskInfo —— 只置位"bit 0 即 TArrayPropertyChangeMaskBitIndex"。
		ChangeMaskInfo.BitOffset += TArrayPropertyChangeMaskBitIndex;
		ChangeMaskInfo.BitCount = 1;
		Private::MarkDirty(Header, MemberChangeMask, ChangeMaskInfo);
	}
}

// 中文：【传统 Poll 入口】遍历全部成员，逐个调用 PollPropertyValue。
//   SrcStateData 是 UObject/UStruct 实例的"原生内存基址"，每个成员的真实地址用
//   Property->GetOffset_ForGC()（GC 安全的字段偏移）+ Property->GetElementSize()*ArrayIndex
//   （处理 C 数组）来计算。
//   开销：N（成员数） * 一次 compare + 一次 copy。适合不支持 PushModel 的旧式 UCLASS。
//   返回 true 表示有任何成员被标 dirty。
bool FPropertyReplicationState::PollPropertyReplicationState(const void* RESTRICT SrcStateData)
{
	if (IsValid())
	{
		const FReplicationStateDescriptor* Descriptor = ReplicationStateDescriptor;
		const uint8* SrcBuffer = reinterpret_cast<const uint8*>(SrcStateData);

#if UE_NET_WITH_VERBOSE_POLL_PROFILING
		IRIS_PROFILER_PROTOCOL_NAME(ReplicationStateDescriptor->DebugName->Name);
#endif

		const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
		const FProperty** MemberProperties = Descriptor->MemberProperties;
		const FReplicationStateMemberPropertyDescriptor* MemberPropertyDescriptors = Descriptor->MemberPropertyDescriptors;
		const uint32 MemberCount = Descriptor->MemberCount;

		for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
		{
			const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
			const FReplicationStateMemberPropertyDescriptor& MemberPropertyDescriptor = MemberPropertyDescriptors[MemberIt];
			const FProperty* Property = MemberProperties[MemberIt];

			//$TODO: make special version to avoid unnecessary overhead.
			PollPropertyValue(MemberIt, SrcBuffer + Property->GetOffset_ForGC() + Property->GetElementSize()*MemberPropertyDescriptor.ArrayIndex);
		}
	}

	return IsDirty();
}

// 中文：【PushModel Poll 入口】仅对"必须 poll"的成员执行拷贝：
//   bForcePoll = (强制刷新对象引用 && 该成员含 ObjectReference)
//             || (该成员未启用 PushModel —— 不能依赖 PushModel 标脏，必须每帧都拉)
//   再加上 IsDirtyForPolling（PushModel pollmask 命中 或 ChangeMask 已 dirty）。
//   该路径在主流 UCLASS（开启 push-based 复制）下能跳过绝大多数成员，节省大量 CPU。
//   bEnableVerboseProfiling 用于性能分析时按 PushBased / AlwaysPolled 标签细化作用域。
bool FPropertyReplicationState::PollPushBasedPropertyReplicationState(const void* RESTRICT SrcStateData, const FPollParameters& PollParameters)
{
	if (IsValid())
	{
		const FReplicationStateDescriptor* Descriptor = ReplicationStateDescriptor;
		const uint8* SrcBuffer = reinterpret_cast<const uint8*>(SrcStateData);

#if UE_NET_WITH_VERBOSE_POLL_PROFILING
		IRIS_PROFILER_SCOPE_TEXT_CONDITIONAL(Descriptor->DebugName->Name, PollParameters.bEnableVerboseProfiling);
#endif

		const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
		const FProperty** MemberProperties = Descriptor->MemberProperties;
		const FReplicationStateMemberPropertyDescriptor* MemberPropertyDescriptors = Descriptor->MemberPropertyDescriptors;
		const uint32 MemberCount = Descriptor->MemberCount;
		const FReplicationStateMemberTraitsDescriptor* MemberTraits = Descriptor->MemberTraitsDescriptors;
		for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
		{
			// 中文：bForcePoll 决定是否绕过 push-based 优化——
			//   - 含 ObjectReference 且要求强制刷新；或
			//   - 该成员根本不是 PushBased（旧式属性必须每帧都 poll）。
			const bool bForcePoll = (PollParameters.bForceRefreshObjectReferences && EnumHasAnyFlags(MemberTraits[MemberIt].Traits, EReplicationStateMemberTraits::HasObjectReference)) || !EnumHasAnyFlags(MemberTraits[MemberIt].Traits, EReplicationStateMemberTraits::HasPushBasedDirtiness);
			if (bForcePoll || IsDirtyForPolling(MemberIt))
			{
#if UE_NET_WITH_VERBOSE_POLL_PROFILING
				IRIS_PROFILER_SCOPE_TEXT_CONDITIONAL(EnumHasAnyFlags(MemberTraits[MemberIt].Traits, EReplicationStateMemberTraits::HasPushBasedDirtiness) ? TEXT("PushBased") : TEXT("AlwaysPolled"), PollParameters.bEnableVerboseProfiling);
				IRIS_PROFILER_SCOPE_TEXT_CONDITIONAL(Descriptor->MemberDebugDescriptors[MemberIt].DebugName->Name, PollParameters.bEnableVerboseProfiling);
#endif
				const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
				const FReplicationStateMemberPropertyDescriptor& MemberPropertyDescriptor = MemberPropertyDescriptors[MemberIt];
				const FProperty* Property = MemberProperties[MemberIt];

				// $TODO: make special version to avoid unnecessary overhead related to looking up same things for every single member.
				PollPropertyValue(MemberIt, SrcBuffer + Property->GetOffset_ForGC() + Property->GetElementSize()*MemberPropertyDescriptor.ArrayIndex);
			}
		}		
	}

	return IsDirty();
}

// 中文：【RepNotify 阶段 1：旧值快照】
//   场景：客户端即将把"NewStateToBeApplied"的 dirty 成员写回到 UObject，但有些 RepNotify
//   函数（带"上一个值"参数 / KeepPreviousState 模式）需要旧值——必须在 Push 之前把 UObject
//   上的当前值快照下来。
//   策略：
//     - 仅对"有 RepNotifyFunction 且在 NewStateToBeApplied 中是 dirty"的成员拷贝旧值；
//     - InitState / NewStateToBeApplied==nullptr 退化为"全部都拷贝"（因为 InitState 没有 dirty 概念）；
//     - 拷贝目标是 self.StateBuffer，这份 self 通常是上层在 ReplicationFragment 中持有的
//       PreviousState 实例（最终传给 CallRepNotifies 作为 PreviousState 参数）。
bool FPropertyReplicationState::StoreCurrentPropertyReplicationStateForRepNotifies(const void* RESTRICT SrcStateData, const FPropertyReplicationState* NewStateToBeApplied)
{
	if (IsValid())
	{
		const FReplicationStateDescriptor* Descriptor = ReplicationStateDescriptor;
		const uint8* SrcBuffer = reinterpret_cast<const uint8*>(SrcStateData);

		IRIS_PROFILER_PROTOCOL_NAME(ReplicationStateDescriptor->DebugName->Name);

		const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
		const FProperty** MemberProperties = Descriptor->MemberProperties;
		const FReplicationStateMemberPropertyDescriptor* MemberPropertyDescriptors = Descriptor->MemberPropertyDescriptors;
		const uint32 MemberCount = Descriptor->MemberCount;

		// Copy all if this is a state with no changemask or if NewStateToBeApplied is not set
		const bool bCopyAll = IsInitState() || NewStateToBeApplied == nullptr;
		for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
		{
			const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
			const FReplicationStateMemberPropertyDescriptor& MemberPropertyDescriptor = MemberPropertyDescriptors[MemberIt];

			if (MemberPropertyDescriptor.RepNotifyFunction && (bCopyAll || NewStateToBeApplied->IsDirty(MemberIt)))
			{
				const FProperty* Property = MemberProperties[MemberIt];

				void* DstValue = StateBuffer + Descriptor->MemberDescriptors[MemberIt].ExternalMemberOffset;
				const void* SrcValue = SrcBuffer + Property->GetOffset_ForGC() + Property->GetElementSize()*MemberPropertyDescriptor.ArrayIndex;

				// 中文：用 InternalCopyPropertyValue（纯 memberwise 拷贝，不走 Serializer.Apply）
				//       从 UObject 原生内存读到 PreviousState 的 StateBuffer。
				Private::InternalCopyPropertyValue(Descriptor, MemberIt, DstValue, SrcBuffer + Property->GetOffset_ForGC() + Property->GetElementSize()*MemberPropertyDescriptor.ArrayIndex);
			}
		}
	}

	return IsDirty();
}

// 中文：【Push 路径 / RepNotify 阶段 2】把内部 StateBuffer 里的成员值反向写到 UObject DstData。
//   bInPushAll=true（或 InitState）→ 全部成员都推；否则仅推 dirty 成员。
//   推每个成员时调用 PushPropertyValue → InternalApplyPropertyValue（含 Serializer.Apply 的 side-effects）。
//   WITH_PUSH_MODEL 下额外维护一份 DirtyRepIndices 数组，结束后对 Owner 调用 MARK_PROPERTY_DIRTY_UNSAFE，
//   这是为了在客户端"应用收到的状态"后，让 PushModel 把这些字段标脏——这样下一帧若它们被 native 代码再次修改，
//   PushModel 才能正确触发 Poll；同时避免漏标导致客户端预测/纠正逻辑出错。
//   $IRIS TODO 注释提到将来要改为基于 ChangeMask 直接迭代，目前是按成员遍历查 ChangeMask。
void FPropertyReplicationState::PushPropertyReplicationState(const UObject* Owner, void* RESTRICT DstData, bool bInPushAll) const
{
	// $IRIS TODO: Rewrite this to iterate over change mask instead of iterating over all members and querying the mask
	// Note, we need to use a NetBitStreamReader and the changemask descriptor since each member might have different number of bits.
	if (IsValid())
	{
#if WITH_PUSH_MODEL
		using RepIndexType = decltype(FProperty::RepIndex);
		TArray<RepIndexType, TInlineAllocator<128>> DirtyRepIndices;
#endif

		const FReplicationStateDescriptor* Descriptor = ReplicationStateDescriptor;
		uint8* DstBuffer = static_cast<uint8*>(DstData);

#if UE_NET_WITH_VERBOSE_POLL_PROFILING
		IRIS_PROFILER_PROTOCOL_NAME(ReplicationStateDescriptor->DebugName->Name);
#endif

		const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
		const FProperty** MemberProperties = Descriptor->MemberProperties;
		const FReplicationStateMemberPropertyDescriptor* MemberPropertyDescriptors = Descriptor->MemberPropertyDescriptors;
		const uint32 MemberCount = Descriptor->MemberCount;

		const bool bPushAll = bInPushAll || IsInitState();
		for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
		{
			// Note: Currently not checking whether the condition is enabled or not. This is assuming few states would be affected by an early out here.
			// We need to mask off conditional state regardless at replication time to avoid replicating lost packets which contained state that has since been disabled.
			// 中文：注意这里不检查 conditional 是否启用——因为在复制时序列化层会做最终掩码，
			//       这里多 push 一次代价很小。
			if (bPushAll || IsDirty(MemberIt))
			{
				const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
				const FReplicationStateMemberPropertyDescriptor& MemberPropertyDescriptor = MemberPropertyDescriptors[MemberIt];
				const FProperty* Property = MemberProperties[MemberIt];

				PushPropertyValue(MemberIt, DstBuffer + Property->GetOffset_ForGC() + Property->GetElementSize()*MemberPropertyDescriptor.ArrayIndex);

#if WITH_PUSH_MODEL
				// 中文：C 数组只在 ArrayIndex==0 时记 RepIndex（同一个 RepIndex 仅一次）。
				if (MemberPropertyDescriptor.ArrayIndex == 0)
				{
					DirtyRepIndices.Add(Property->RepIndex);
				}
#endif
			}
		}

#if WITH_PUSH_MODEL
		if (Owner != nullptr)
		{
			// 中文：批量把这些 RepIndex 通过 PushModel 标记为 dirty。MARK_PROPERTY_DIRTY_UNSAFE
			//       是 unsafe 版本——直接按 RepIndex 查表，避免 reflection 开销。
			for (RepIndexType RepIndex : DirtyRepIndices)
			{
				MARK_PROPERTY_DIRTY_UNSAFE(Owner, RepIndex);
			}
		}
#endif
	}
}

// 中文：CopyDirtyProperties —— 把 Other 中标 dirty 的成员合并到自身。
//   - 自身未 bound：直接 CopyDirtyMembers（只拷 dirty + 合并 ChangeMask）；
//   - 自身已 bound：走 Set，逐成员 compare + copy（保证 dirty tracking 通过 MarkDirty 正确触发
//     NetObject 通知）。
void FPropertyReplicationState::CopyDirtyProperties(const FPropertyReplicationState& Other)
{
	check(this != &Other && IsValid());
	check(ReplicationStateDescriptor.GetReference() == Other.ReplicationStateDescriptor.GetReference());

	if (!Private::IsReplicationStateBound(StateBuffer, ReplicationStateDescriptor.GetReference()))
	{
		Private::CopyDirtyMembers(StateBuffer, Other.StateBuffer, ReplicationStateDescriptor.GetReference());
	}
	else
	{
		Set(Other);
	}
}

// 中文：【ObjectReference Poll 入口】仅扫描含 HasObjectReference trait 的成员逐个 poll。
//   场景：当某个 NetObject 引用在客户端尚未导入（unresolved）时，引用解析器会请求"周期性
//   重新 poll 所有 reference 字段"——避免做全量 poll，只针对引用字段做 compare + copy。
//   即便相应字段值未变，PollPropertyValue 内部会通过 Serializer.Apply 触发引用重解析。
bool FPropertyReplicationState::PollObjectReferences(const void* RESTRICT SrcStateData)
{
	if (IsValid())
	{
		const FReplicationStateDescriptor* Descriptor = ReplicationStateDescriptor;
		const uint8* SrcBuffer = static_cast<const uint8*>(SrcStateData);

		const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
		const FReplicationStateMemberTraitsDescriptor* MemberTraitsDescriptors = Descriptor->MemberTraitsDescriptors;
		const FProperty** MemberProperties = Descriptor->MemberProperties;
		const FReplicationStateMemberPropertyDescriptor* MemberPropertyDescriptors = Descriptor->MemberPropertyDescriptors;
		const uint32 MemberCount = Descriptor->MemberCount;

		for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
		{
			if (EnumHasAnyFlags(MemberTraitsDescriptors[MemberIt].Traits, EReplicationStateMemberTraits::HasObjectReference))
			{
				const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
				const FReplicationStateMemberPropertyDescriptor& MemberPropertyDescriptor = MemberPropertyDescriptors[MemberIt];
				const FProperty* Property = MemberProperties[MemberIt];

				PollPropertyValue(MemberIt, SrcBuffer + Property->GetOffset_ForGC() + Property->GetElementSize()*MemberPropertyDescriptor.ArrayIndex);
			}
		}
	}

	return IsDirty();
}

// 中文：【RepNotify 阶段 3：触发 RepNotify 函数】
//   遍历所有成员，对"有 RepNotifyFunction 且 dirty"的成员触发 RepNotify。
//   关键细节：
//     1) bIsInit：初始状态下，仅当新值与上一个值（或本地默认值）不同时才调 RepNotify
//        （避免初始化时就触发误报的 RepNotify）；
//     2) bOnlyCallIfDiffersFromLocal=false（默认）：信任服务器，无条件调（除非 IsInit 下相等）；
//        =true：兼容老的 RepNotify_Changed 语义，只在与本地值不同时调；同时考虑
//        EReplicationStateMemberTraits::HasRepNotifyAlways（RepNotify_Always）跳过 compare 直接调；
//     3) C 数组：RepNotify 是对整个数组（不是单元素），所以 LastPropertyWithRepNotify 用于
//        识别"上一个已经触发过的成员是同一个 Property 的下一个 ArrayIndex"，并跳过；
//     4) PrevValuePtr 通过 PreviousState->StateBuffer 取得，作为 ProcessEvent 的 OutParm 参数
//        传入（KeepPreviousState 模式下 RepNotify 函数签名带"上一个值"）。
//     5) IRIS_CLIENT_PROFILER 在调用前记录函数名（性能分析钩子）。
void FPropertyReplicationState::CallRepNotifies(void* RESTRICT DstData, const FCallRepNotifiesParameters& Params) const
{
	// $IRIS TODO: Rewrite this to iterate over change mask instead of iterating over all members and querying the mask
	// Note, we need to use a NetBitStreamReader and the changemask descriptor since each member might have different number of bits.
	if (IsValid())
	{
		const FReplicationStateDescriptor* Descriptor = ReplicationStateDescriptor;
		const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
		const FProperty** MemberProperties = Descriptor->MemberProperties;
		const FReplicationStateMemberPropertyDescriptor* MemberPropertyDescriptors = Descriptor->MemberPropertyDescriptors;
		const uint32 MemberCount = Descriptor->MemberCount;

		const FProperty* LastPropertyWithRepNotify = nullptr;
		// Note: IsInitState indicates that the state itself is only ever will be applied at Init
		const bool bIsInitState = IsInitState();
		for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
		{
			const FReplicationStateMemberPropertyDescriptor& MemberPropertyDescriptor = MemberPropertyDescriptors[MemberIt];
			const UFunction* RepNotifyFunction = MemberPropertyDescriptor.RepNotifyFunction;
			if (RepNotifyFunction && (bIsInitState || IsDirty(MemberIt)))
			{
				const FProperty* Property = MemberProperties[MemberIt];
				// If this is the same property we already processed it's yet another element in a C array.
				// 中文：C 数组——同一个 Property 的多个 ArrayIndex，只在第一次触发 RepNotify。
				if (Property == LastPropertyWithRepNotify)
				{
					checkSlow(MemberPropertyDescriptor.ArrayIndex > 0);
					continue;
				}

				// For C arrays the RepNotify is for the entire array, not an individual element.
				const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
				const uint32 ExternalMemberOffset = MemberDescriptors[MemberIt].ExternalMemberOffset;
				
				UObject* Object = reinterpret_cast<UObject*>(DstData);
				const uint8* PrevValuePtr = Params.PreviousState ? Params.PreviousState->StateBuffer + ExternalMemberOffset : nullptr;
				const uint8* ValuePtr = StateBuffer + ExternalMemberOffset;				

				bool bShouldCallRepNotify = false;
				if (Params.bOnlyCallIfDiffersFromLocal)
				{
					// We try to be backwards compatible and respect RepNotify_Always/RepNotify_Changed unless it is the initial state where we only will call the repnotify of the received value differs from the local one.
					// 中文：bOnlyCallIfDiffersFromLocal 路径——
					//   - IsInit：仅在新值 != 旧值时调（不考虑 RepNotifyAlways）；
					//   - 非 Init：HasRepNotifyAlways 强制调，否则也仅在不同时调。
					bShouldCallRepNotify = Params.bIsInit ? !Private::InternalCompareMember(Descriptor, MemberIt, ValuePtr, PrevValuePtr) : EnumHasAnyFlags(Descriptor->MemberTraitsDescriptors[MemberIt].Traits, EReplicationStateMemberTraits::HasRepNotifyAlways) || !Private::InternalCompareMember(Descriptor, MemberIt, ValuePtr, PrevValuePtr);
				}
				else
				{
					// Trust data from server and call RepNotify without doing additonal compare unless it is the initial state.
					// 中文：默认路径——除 IsInit 下做一次相等比较外，其余无条件触发（信任服务器）。
					bShouldCallRepNotify = !Params.bIsInit || !Private::InternalCompareMember(Descriptor, MemberIt, ValuePtr, PrevValuePtr);
				}

				if (bShouldCallRepNotify)
				{
#if IRIS_CLIENT_PROFILER_ENABLE
					UE::Net::FClientProfiler::RecordRepNotify(RepNotifyFunction->GetFName());
#endif

					// We only want to call RepNotify once for c-arrays
					LastPropertyWithRepNotify = Property;
					// 中文：用 ProcessEvent 调用 UFUNCTION——PrevValuePtr 作为参数指针传入，
					//       若函数签名带"OldValue"参数则会被读取。
					Object->ProcessEvent(const_cast<UFunction*>(RepNotifyFunction), const_cast<uint8*>(PrevValuePtr));
				}

#if !UE_BUILD_SHIPPING
				if (UE_LOG_ACTIVE(LogIrisRepNotify, Verbose) && IrisDebugHelper::FilterDebuggedObject(Object))
				{
					const bool bIsRepNotifyAlways = EnumHasAnyFlags(Descriptor->MemberTraitsDescriptors[MemberIt].Traits, EReplicationStateMemberTraits::HasRepNotifyAlways);
					if (bShouldCallRepNotify)
					{
						UE_LOG(LogIrisRepNotify, Verbose, TEXT("Calling RepNotify. Object: %s, Function: %s IsInit: %d IsRepAlways: %d"), *Object->GetFullName(), ToCStr(RepNotifyFunction->GetName()), Params.bIsInit, bIsRepNotifyAlways);
					}
					else
					{
						UE_LOG(LogIrisRepNotify, VeryVerbose, TEXT("Skipping RepNotify. Object: %s, Function: %s IsInit: %d IsRepAlways: %d"), *Object->GetFullName(), ToCStr(RepNotifyFunction->GetName()), Params.bIsInit, bIsRepNotifyAlways);
					}					
				}
#endif
			}
		}
	}
}

// 中文：调试输出 —— 把 state 中所有成员（或仅 dirty 成员）通过 ExportTextItem_Direct 序列化为
//       人类可读字符串，附加到 StringBuilder。InitState 强制全量输出。
const TCHAR* FPropertyReplicationState::ToString(FStringBuilderBase& StringBuilder, bool bInIncludeAll) const
{
	if (IsValid())
	{
		const FReplicationStateDescriptor* Descriptor = ReplicationStateDescriptor;

		const FProperty** MemberProperties = Descriptor->MemberProperties;
		const uint32 MemberCount = Descriptor->MemberCount;

		StringBuilder.Appendf(TEXT("FPropertyReplicationState %s\n"), Descriptor->DebugName->Name);

		const bool bIncludeAll = bInIncludeAll || Descriptor->IsInitState();
		FString TempString;
		for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
		{
			const FProperty* Property = MemberProperties[MemberIt];

			if (bIncludeAll || IsDirty(MemberIt))
			{
				void* PropertyData = StateBuffer + Descriptor->MemberDescriptors[MemberIt].ExternalMemberOffset;
				Property->ExportTextItem_Direct(TempString, PropertyData, PropertyData, nullptr, PPF_SimpleObjectText|PPF_IncludeTransient|PPF_UseDeprecatedProperties);
				StringBuilder.Appendf(TEXT("%u - %s : %s\n"), MemberIt, *Property->GetName(), ToCStr(TempString));
				TempString.Reset();
			}
		}
	}

	return StringBuilder.ToString();
}

// 中文：FString 版本调试输出（内部走 StringBuilder 实现）。
FString FPropertyReplicationState::ToString(bool bIncludeAll) const
{
	TStringBuilder<2048> Builder;
	ToString(Builder, bIncludeAll);

	return FString(Builder.ToString());
}

// 中文：自定义条件位查询。
//   - 没有 LifetimeConditionals 的 state：所有成员"始终启用"，直接返回 true；
//   - 否则查 ConditionalChangeMask 的成员对应位（BitOffset）；
//   - 若该成员 BitCount==0（无 ChangeMask 槽位），也视为"启用"。
//   该位由 ReplicationSystem::SetReplicationConditionEnabled / SetCustomCondition 控制；
//   关闭时 PollPropertyValue 不会标 dirty（即数据不会进入复制）。
bool FPropertyReplicationState::IsCustomConditionEnabled(uint32 Index) const
{
	const EReplicationStateTraits Traits = ReplicationStateDescriptor->Traits;
	if (!EnumHasAnyFlags(Traits, EReplicationStateTraits::HasLifetimeConditionals))
	{
		return true;
	}

	const FReplicationStateDescriptor* Descriptor = ReplicationStateDescriptor;
	const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskInfo = Descriptor->MemberChangeMaskDescriptors[Index];

	if (ChangeMaskInfo.BitCount > 0)
	{
		FNetBitArrayView MemberConditionalChangeMask = Private::GetMemberConditionalChangeMask(StateBuffer, Descriptor);
		return MemberConditionalChangeMask.GetBit(ChangeMaskInfo.BitOffset);
	}

	// If there's no bitmask the property cannot be disabled.
	return true;
}

}
