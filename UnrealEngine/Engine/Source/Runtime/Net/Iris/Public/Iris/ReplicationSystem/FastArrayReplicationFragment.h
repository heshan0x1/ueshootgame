// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// FastArrayReplicationFragment.h —— FastArraySerializer ↔ Iris 的 Fragment 桥
// ---------------------------------------------------------------------------------------------------------------------
// 文件位置：Public/Iris/ReplicationSystem/  ← 对外 API（含模板实现）。
// 角色：把 UE 传统的 FFastArraySerializer 接入 Iris 的 Fragment 体系，提供两条路径：
//
//   1) TFastArrayReplicationFragment<FastArrayItemType, FastArrayType>（兼容路径）
//        - 适用：用户继承自 FFastArraySerializer 的常规 FastArray，无需改动 game code；
//        - 通过 FPropertyReplicationState 维护一份"上次镜像 FastArray"，Poll 阶段比较 ArrayReplicationKey
//          + 每元素 ReplicationKey 找出脏元素，并在 ChangeMask 中按 modulo 散列写位；
//        - Apply 阶段用 InternalPartialDequantizeFastArray 把内部状态展开到 AccumulatedReceivedState（持久），
//          然后调用 FFastArrayReplicationFragmentHelper::ApplyReplicatedState 触发 PreReplicatedRemove / PostReplicatedAdd /
//          PostReplicatedChange 标准回调。
//
//   2) TNativeFastArrayReplicationFragment<FastArrayItemType, FastArrayType, PollingPolicyType>（原生路径）
//        - 适用：用户继承自 FIrisFastArraySerializer（带嵌入 FReplicationStateHeader + ChangeMaskStorage[4]）；
//        - 不分配 FPropertyReplicationState 缓冲——ChangeMask 直接由 FastArraySerializer 嵌入位提供；
//        - 配合 PollingPolicyType 决定是否还需要 poll：
//             * FNoPollingPolicy（推模式 + TIrisFastArrayEditor）：ChangeMask 已由 Editor 写好，poll 是 no-op；
//             * FNeedPollingPolicy：仍按"上次镜像"比对找脏（不需要持有完整 FastArray 镜像，只需 ReplicationKey/ID）。
//
// ChangeMask 布局（与 IrisFastArraySerializer.h 协议一致）：
//   bit 0                            → "数组结构变化"位（Add/Remove）
//   bit 1 .. ChangeMaskBitCount-1    → 每元素散列位（i 元素 → bit (i % (BitCount-1)) + offset）
// 散列冲突时多个元素共享一位，Apply 阶段必须再 Compare 才能确认真假脏。
//
// 与文档对应：
//   - ReplicationState.md §2.7（FIrisFastArraySerializer / TIrisFastArrayEditor）。
//   - FastArrayReplicationFragmentInternal.h 中 FFastArrayReplicationFragmentHelper 的 ApplyReplicatedState 详细算法。
//
// CreateAndRegisterFragment（命名空间末尾）：根据 FastArrayType 是否继承 FIrisFastArraySerializer + 是否 NativeFastArrayState
// 在两种 Fragment 中自动选择，由 ReplicationStateDescriptorBuilder 写入 Descriptor->CreateAndRegisterReplicationFragmentFunction。
// =====================================================================================================================

#pragma once

#include "Iris/ReplicationSystem/ReplicationFragment.h"
#include "Iris/ReplicationSystem/Private/FastArrayReplicationFragmentInternal.h" // IWYU pragma: export
#include "Iris/ReplicationState/IrisFastArraySerializer.h"
#include "Iris/ReplicationState/Private/IrisFastArraySerializerInternal.h" // IWYU pragma: export
#include "Iris/ReplicationState/PropertyReplicationState.h"
#include "Iris/ReplicationState/ReplicationStateUtil.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"

#include "Net/Core/NetBitArray.h"

#include "Templates/UnrealTemplate.h"

namespace UE::Net
{

/**
 * TFastArrayReplicationFragment - Binds a typed FastArray to a FReplicationfragment
 * Used to support FFastArray-based serialization with no required code modifications
 * Backed by a PropertyReplicationState which means that we will have to poll source data for dirtiness,
 * in the case of FastArrays this involves comparing the replication key of the array and its items.
 */
// TFastArrayReplicationFragment ── 兼容路径：传统 FFastArraySerializer + 用户态 FPropertyReplicationState 镜像。
// Poll 阶段比较 ArrayReplicationKey + 每元素 ReplicationKey 找脏。
template <typename FastArrayItemType, typename FastArrayType>
class TFastArrayReplicationFragment : public Private::FFastArrayReplicationFragmentBase
{
public:
	typedef TArray<FastArrayItemType> ItemArrayType;
	// 构造：bValidateDescriptor 默认 true —— 强制 FastArray 只有一个 TArray 成员；额外字段路径走下面 protected 重载。
	TFastArrayReplicationFragment(EReplicationFragmentTraits InTraits, UObject* InOwner, const FReplicationStateDescriptor* InDescriptor, bool bValidateDescriptor = true);

protected:
	enum EAllowAdditionalPropertiesType { AllowAdditionalProperties };
	// 允许额外属性的构造重载：把 bValidateDescriptor 关闭。派生类继续走 ApplyReplicatedStateForExtraProperties。
	TFastArrayReplicationFragment(EReplicationFragmentTraits InTraits, UObject* InOwner, const FReplicationStateDescriptor* InDescriptor, const EAllowAdditionalPropertiesType) : TFastArrayReplicationFragment(InTraits, InOwner, InDescriptor, false) {}

	// For the select few cases where we allow additional properties, this is a helper to deal with applying them directly from quantized state
	// FastArray 上还有非"item 数组"的 UPROPERTY 字段时调用：直接 Dequantize 它们到 owner 内存。
	void ApplyReplicatedStateForExtraProperties(FReplicationStateApplyContext& Context) const;

	// FReplicationFragment
	virtual void ApplyReplicatedState(FReplicationStateApplyContext& Context) const override;
	virtual void CallRepNotifies(FReplicationStateApplyContext& Context) override;
	virtual bool PollReplicatedState(EReplicationFragmentPollFlags PollOption) override;
	virtual void ReplicatedStateToString(FStringBuilderBase& StringBuilder, FReplicationStateApplyContext& Context, EReplicationStateToStringFlags Flags) const override;

protected:
	// Poll entire FastFrray
	// 完整比较 FastArray：bForceFullCompare=true 时每个元素都走 InternalCompareArrayElement 而不是仅看 ReplicationKey。
	bool PollAllState(bool bForceFullCompare = false);

	// Returns true if the FastArray is dirty
	// 状态 header 的 dirty 位（由 SetBit / 标脏路径写入）。
	bool IsDirty() const;

	// Returns true if the FastArray should be polled
	// dirty 或 PollMask 有任意位（即"待比对 / 待重新 poll"）。
	bool IsDirtyForPolling() const;

	// Mark FastArray dirty
	// 显式标脏：设置 ChangeMask 的 array 位 + state header 的 dirty 位。
	void MarkDirty();

	// Get FastArraySerializer from owner
	// 三个 Getter：分别从不同源头读 FastArraySerializer 指针。
	// FromOwner：通过 Descriptor->MemberProperties[0]->GetOffset_ForGC() 在 Owner 内偏移定位。
	inline FastArrayType* GetFastArraySerializerFromOwner() const;

	// Get FastArraySerializer from our cached ReplicationState
	// FromReplicationState：从镜像 SrcState 中按 ExternalMemberOffset 取（服务端 poll 镜像）。
	inline FastArrayType* GetFastArraySerializerFromReplicationState() const;

	// Get FastArraySerialzier from received state
	// FromApplyContext：在 Apply 时从 ExternalStateBuffer 取（接收态）。
	inline FastArrayType* GetFastArraySerializerFromApplyContext(FReplicationStateApplyContext& Context) const;

	// TODO: Can be removed when we have implemented explicit code to traverse quantized data directly
	// 客户端持有"累积接收态"FastArray —— InternalPartialDequantizeFastArray 把 ChangeMask 标记的元素展开到这里，
	// 再以此为 Src 与 Dst 做 ApplyReplicatedState（避免每帧重建）。
	TUniquePtr<FastArrayType> AccumulatedReceivedState;
};

/**
 * TNativeFastArrayReplicationFragment - Binds a typed FastArray to a FReplicationfragments
 * Used to support FFastArray-based serialization with some minor code modifications
 * The FastArray must be changed to inherit from IrisFastArraySerializer instead of FFastArraySerializer which will inject a
 * ReplicationStateHeader and a fixed size changemask, in it most basic form this allows us to not keep a full copy the fast array to detect dirtiness 
 * but instead only store the ReplicationID and ReplicationKeys. We can also provide an alternative interface for editing the FastArrays which allows us 
 * update dirtiness directly and skip the poll step completely.
 */
// TNativeFastArrayReplicationFragment ── 原生路径：要求 FastArrayType 继承 FIrisFastArraySerializer。
// 因 ChangeMask 已嵌在 IrisFastArraySerializer 内部，无需 FPropertyReplicationState 镜像；
// 配合 TIrisFastArrayEditor 推模式可完全跳过 poll。
template <typename FastArrayItemType, typename FastArrayType, typename PollingPolicyType = FastArrayPollingPolicies::FNoPollingPolicy>
class TNativeFastArrayReplicationFragment final : public Private::FNativeFastArrayReplicationFragmentBase
{
public:
	typedef TArray<FastArrayItemType> ItemArrayType;

	TNativeFastArrayReplicationFragment(EReplicationFragmentTraits InTraits, UObject* InOwner, const FReplicationStateDescriptor* InDescriptor);
	void Register(FFragmentRegistrationContext& Fragments, EReplicationFragmentTraits Traits = EReplicationFragmentTraits::None);

protected:
	// FReplicationFragment implementation
	virtual void ApplyReplicatedState(FReplicationStateApplyContext& Context) const override;
	virtual void CallRepNotifies(FReplicationStateApplyContext& Context) override;
	virtual bool PollReplicatedState(EReplicationFragmentPollFlags PollOption) override;
	virtual void ReplicatedStateToString(FStringBuilderBase& StringBuilder, FReplicationStateApplyContext& Context, EReplicationStateToStringFlags Flags) const override;

protected:
	// 走 PollingPolicy.GetPollingState 决定是否真的 poll；推模式下为 nullptr，直接早退。
	bool PollAllState();

	bool IsDirty() const;

	// Get FastArraySerializer from owner
	inline FastArrayType* GetFastArraySerializerFromOwner() const;

private:
	// 策略对象：FNoPollingPolicy（推模式）/ FNeedPollingPolicy（带 FPollingState 镜像）。
	PollingPolicyType PollingPolicy;
};

/**
 * TFastArrayReplicationFragment implementation
 */

template <typename FastArrayItemType, typename FastArrayType>
TFastArrayReplicationFragment<FastArrayItemType, FastArrayType>::TFastArrayReplicationFragment(EReplicationFragmentTraits InTraits, UObject* InOwner, const FReplicationStateDescriptor* InDescriptor, bool bValidateDescriptor) : FFastArrayReplicationFragmentBase(InTraits, InOwner, InDescriptor, bValidateDescriptor)
{
	if (EnumHasAnyFlags(InTraits, EReplicationFragmentTraits::CanReceive))
	{
		AccumulatedReceivedState = MakeUnique<FastArrayType>();
	}
}

template <typename FastArrayItemType, typename FastArrayType>
void TFastArrayReplicationFragment<FastArrayItemType, FastArrayType>::ApplyReplicatedState(FReplicationStateApplyContext& Context) const
{
	// 兼容路径接收侧 Apply：
	//   1) 拿到 owner 上的 FastArraySerializer + TArray<Item>（DstArraySerializer / DstWrappedArray）；
	//   2) 把 ChangeMask 标记的元素 partial dequantize 到 AccumulatedReceivedState（持久缓冲，元素逐步累积）；
	//   3) 调用 FFastArrayReplicationFragmentHelper::ApplyReplicatedState 完成 Add/Remove/Modify 三类回调。
	// Get the wrapped FastArraySerializer and array
	FastArrayType* DstArraySerializer = GetFastArraySerializerFromOwner();
	ItemArrayType* DstWrappedArray = reinterpret_cast<ItemArrayType*>(reinterpret_cast<uint8*>(DstArraySerializer) + WrappedArrayOffsetRelativeFastArraySerializerProperty);

	// For now we maintain a dequantized representation of the array into which we accumulate new data.
	// TODO: change to only maintain the map of indices
	InternalPartialDequantizeFastArray(Context, reinterpret_cast<uint8*>(AccumulatedReceivedState.Get()), Context.StateBufferData.RawStateBuffer, GetFastArrayPropertyStructDescriptor());

	// Intentionally not const as we allow the src state to be modified
	FastArrayType* SrcArraySerializer = AccumulatedReceivedState.Get();
	const ItemArrayType* SrcWrappedArray = reinterpret_cast<const ItemArrayType*>(reinterpret_cast<uint8*>(SrcArraySerializer) + WrappedArrayOffsetRelativeFastArraySerializerProperty);	

	// Apply state to target FastArray and issue callbacks
	Private::FFastArrayReplicationFragmentHelper::ApplyReplicatedState(DstArraySerializer, DstWrappedArray, SrcArraySerializer, SrcWrappedArray, GetArrayElementDescriptor(), Context);
}

template <typename FastArrayItemType, typename FastArrayType>
void TFastArrayReplicationFragment<FastArrayItemType, FastArrayType>::ApplyReplicatedStateForExtraProperties(FReplicationStateApplyContext& Context) const
{
	// Dequantize additional properties directly to DstArraySerialzier
	InternalDequantizeExtraProperties(*Context.NetSerializationContext, reinterpret_cast<uint8*>(GetFastArraySerializerFromOwner()), Context.StateBufferData.RawStateBuffer, GetFastArrayPropertyStructDescriptor());
}

template <typename FastArrayItemType, typename FastArrayType>
FastArrayType* TFastArrayReplicationFragment<FastArrayItemType, FastArrayType>::GetFastArraySerializerFromOwner() const
{
	return reinterpret_cast<FastArrayType*>(reinterpret_cast<uint8*>(Owner) + ReplicationStateDescriptor->MemberProperties[0]->GetOffset_ForGC());
}

template <typename FastArrayItemType, typename FastArrayType>
inline FastArrayType* TFastArrayReplicationFragment<FastArrayItemType, FastArrayType>::GetFastArraySerializerFromApplyContext(FReplicationStateApplyContext& Context) const
{
	return reinterpret_cast<FastArrayType*>(Context.StateBufferData.ExternalStateBuffer + ReplicationStateDescriptor->MemberDescriptors[0].ExternalMemberOffset);	
}

template <typename FastArrayItemType, typename FastArrayType>
FastArrayType* TFastArrayReplicationFragment<FastArrayItemType, FastArrayType>::GetFastArraySerializerFromReplicationState() const
{
	checkSlow(ReplicationState);
	return ReplicationState ? reinterpret_cast<FastArrayType*>(ReplicationState->GetStateBuffer() + ReplicationStateDescriptor->MemberDescriptors[0].ExternalMemberOffset) : nullptr;
}

template <typename FastArrayItemType, typename FastArrayType>
bool TFastArrayReplicationFragment<FastArrayItemType, FastArrayType>::PollReplicatedState(EReplicationFragmentPollFlags PollOption)
{
	// If the ForceObjectReferences flag is set we cannot early out and must always refresh cached data
	if (EnumHasAnyFlags(PollOption, EReplicationFragmentPollFlags::ForceRefreshCachedObjectReferencesAfterGC))
	{
		constexpr bool bForceFullCompare = true;
		return PollAllState(bForceFullCompare);
	}

	// We can early out if we are pushbased and not dirty for polling
	const bool bPoll = EnumHasAnyFlags(PollOption, EReplicationFragmentPollFlags::PollAllState) ||
		(EnumHasAnyFlags(PollOption, EReplicationFragmentPollFlags::PollDirtyState) && (!EnumHasAnyFlags(EReplicationFragmentTraits::HasPushBasedDirtiness, Traits) || IsDirtyForPolling()));

	if (bPoll)
	{
		constexpr bool bForceFullCompare = false;
		return PollAllState(bForceFullCompare);
	}
	else
	{
		return IsDirty();
	}
}

template <typename FastArrayItemType, typename FastArrayType>
bool TFastArrayReplicationFragment<FastArrayItemType, FastArrayType>::PollAllState(bool bForceFullCompare)
{
	// 服务端 Poll：把 owner 上的 FastArray（Src）与镜像 FastArray（Dst，存放在 SrcReplicationState 内）做差异比对。
	// bForceFullCompare=true 时即使 ReplicationKey 相同也强制逐字段 Compare（GC 后引用刷新场景）。
	// 步骤：
	//   1) 取 Src/Dst FastArraySerializer 与 TArray；ReplicationKey 相同且非强制 → 直接早退；
	//   2) 调整 Dst 数组大小到 Src 大小；
	//   3) 遍历 Src：
	//      - ShouldWriteFastArrayItem 决定是否参与复制；
	//      - 没有 ID 的项调用 MarkItemDirty 分配 ID；
	//      - ReplicationKey 不同（或 bForceFullCompare 且字段不等）→ 拷到 Dst + ChangeMask 散列位置位 + bMarkArrayDirty=true；
	//   4) 截断 Dst.Num 到实际复制项数；
	//   5) 同步 ArrayReplicationKey；
	//   6) bMarkArrayDirty 时设置 ChangeMask 的 array 位 + state header 的 dirty 位。
	// Lookup source data, we need the actual FastArraySerializer and the Array it is wrapping
	FastArrayType* SrcArraySerializer = GetFastArraySerializerFromOwner();
	ItemArrayType* SrcWrappedArray = reinterpret_cast<ItemArrayType*>(reinterpret_cast<uint8*>(SrcArraySerializer) + WrappedArrayOffsetRelativeFastArraySerializerProperty);
	
	IRIS_PROFILER_PROTOCOL_NAME(ReplicationStateDescriptor->DebugName->Name);

	// Lookup destination data
	FastArrayType* DstArraySerializer = GetFastArraySerializerFromReplicationState();
	ItemArrayType* DstWrappedArray = reinterpret_cast<ItemArrayType*>(reinterpret_cast<uint8*>(DstArraySerializer) + WrappedArrayOffsetRelativeFastArraySerializerProperty);

	// Check if we can early out
	// ArrayReplicationKey 相同视为整组无变化，跳过昂贵比对（ReplicationKey 由 MarkItemDirty 与 IncrementArrayReplicationKey 维护）。
	if (!bForceFullCompare && SrcArraySerializer->ArrayReplicationKey == DstArraySerializer->ArrayReplicationKey)
	{
		return IsDirty();
	}

	// First we must resize target to match size of source data
	bool bMarkArrayDirty = false;
	const uint32 ElementCount = SrcWrappedArray->Num();
	const uint32 OldDstElementCount = DstWrappedArray->Num();
	if (DstWrappedArray->Num() != ElementCount)
	{
		DstWrappedArray->SetNum(ElementCount);
	}

	const FReplicationStateDescriptor* ArrayElementDescriptor = GetArrayElementDescriptor();
	const FReplicationStateMemberDescriptor* MemberDescriptors = ArrayElementDescriptor->MemberDescriptors;

	// We currently use a simple modulo scheme for bits in the changemask
	// A single bit might represent several entries in the array which all will be considered dirty, it is up to the serializer to handle this
	// The first bit is used by the owning property we need to offset by one and deduct one from the usable bits
	// ChangeMask 散列：bit i 对应所有满足 (k * ChangeMaskBitCount + i) 的元素索引；冲突时由 Serializer 处理（多发一个）。
	FNetBitArrayView MemberChangeMask = UE::Net::Private::GetMemberChangeMask(ReplicationState->GetStateBuffer(), ReplicationStateDescriptor);
	
	const FReplicationStateMemberChangeMaskDescriptor& MemberChangeMaskDescriptor = ReplicationStateDescriptor->MemberChangeMaskDescriptors[0];
	// 第 0 位预留给"数组结构"位（Add/Remove），元素散列从 IrisFastArrayChangeMaskBitOffset(=1) 开始。
	const uint32 ChangeMaskBitOffset = MemberChangeMaskDescriptor.BitOffset + FIrisFastArraySerializer::IrisFastArrayChangeMaskBitOffset;
	const uint32 ChangeMaskBitCount = MemberChangeMaskDescriptor.BitCount - FIrisFastArraySerializer::IrisFastArrayChangeMaskBitOffset;

	FastArrayItemType* DstItems = DstWrappedArray->GetData();
	FastArrayItemType* SrcItems = SrcWrappedArray->GetData();

	// We keep a separate count as we do not care about items that should not be replicated.
	// 不应被复制的 item（ShouldWriteFastArrayItem == false）会跳过；DstItemCount 是真正写入 Dst 的数量。
	int32 DstItemCount = 0;
	{
#if WITH_PUSH_MODEL
		// Disable push model by temporarily setting the FastArray's RepIndex to none.
		// This prevents the array from adding itself to the global dirty list via MarkItemDirty while we are polling it.
		// 临时禁用 PushModel：避免 MarkItemDirty 在我们 poll 过程中递归把对象再次标脏（导致死循环）。
		TGuardValue DisablePushModel(SrcArraySerializer->RepIndex, (int32)INDEX_NONE);
#endif

		// Iterate over array entries and copy the statedata using internal data if it has changed
		for (int32 ElementIt = 0, ElementEndIt = ElementCount; ElementIt < ElementEndIt; ++ElementIt)
		{
			const int32 DstElementIt = DstItemCount;
			FastArrayItemType& SrcItem = SrcItems[ElementIt];
			FastArrayItemType& DstItem = DstItems[DstElementIt];

			const bool bIsWritingOnClient = false;
			if (SrcArraySerializer->template ShouldWriteFastArrayItem<FastArrayItemType, FastArrayType>(SrcItem, bIsWritingOnClient))
			{
				if (SrcItem.ReplicationID == INDEX_NONE)
				{
					// 第一次见到此元素 —— 通过 MarkItemDirty 给它分配 ID（同时也把 ArrayReplicationKey 推进）。
					SrcArraySerializer->MarkItemDirty(SrcItem);
				}

				const bool bReplicationKeyChanged = SrcItem.ReplicationKey != DstItem.ReplicationKey || SrcItem.ReplicationID != DstItem.ReplicationID;
				// 比对：key 不同 → 直接确认脏；bForceFullCompare 时再加一次字段 Compare（防 GC 后浮动）。
				if (bReplicationKeyChanged || (bForceFullCompare && !InternalCompareArrayElement(ArrayElementDescriptor, &DstItem, &SrcItem)))
				{
					InternalCopyArrayElement(ArrayElementDescriptor, &DstItem, &SrcItem);
					DstItem.ReplicationKey = SrcItem.ReplicationKey;

					// Mark element as dirty and mark array as dirty as well.
					if (ChangeMaskBitCount)
					{
						MemberChangeMask.SetBit((DstElementIt % ChangeMaskBitCount) + ChangeMaskBitOffset);
					}
					bMarkArrayDirty = true;
				}
				++DstItemCount;
			}
		}
	}

	// Set actual num replicated items.
	if (DstWrappedArray->Num() != DstItemCount)
	{
		// Set actual num, but keeping the allocated size of the source array
		DstWrappedArray->SetNum(DstItemCount, EAllowShrinking::No);
	}

	// Mark dirty if the new filtered size differs from our previous state.
	// 即使每个元素未变，但"非复制项的过滤结果"导致数组整体大小变化也算脏。
	bMarkArrayDirty = bMarkArrayDirty || (OldDstElementCount != DstItemCount);

	// We update this after the poll since every call to MarkItem() dirty will Increase the ArrayReplicationKey
	// 同步 ArrayReplicationKey —— 必须在 MarkItemDirty 调用之后做，因为后者会推进 Src 的 key。
	DstArraySerializer->ArrayReplicationKey = SrcArraySerializer->ArrayReplicationKey;

	// Mark the NetObject as dirty
	// 设置 array 位（bit 0）+ NetObject Header 的 dirty 位 → Iris 后续 Quantize 会把它送入 Writer。
	if (bMarkArrayDirty && ReplicationState->IsCustomConditionEnabled(FIrisFastArraySerializer::IrisFastArrayPropertyBitIndex))
	{
		MemberChangeMask.SetBit(FIrisFastArraySerializer::IrisFastArrayPropertyBitIndex);
		MarkNetObjectStateHeaderDirty(UE::Net::Private::GetReplicationStateHeader(ReplicationState->GetStateBuffer(), ReplicationStateDescriptor));
	}

	return IsDirty();
}

template <typename FastArrayItemType, typename FastArrayType>
void TFastArrayReplicationFragment<FastArrayItemType, FastArrayType>::ReplicatedStateToString(FStringBuilderBase& StringBuilder, FReplicationStateApplyContext& Context, EReplicationStateToStringFlags Flags) const
{
	// Temporary
	FastArrayType ReceivedState;

	// Dequantize into temporary array, using partial dequantize based on changemask
	InternalDequantizeFastArray(*Context.NetSerializationContext, reinterpret_cast<uint8*>(&ReceivedState), Context.StateBufferData.RawStateBuffer, GetFastArrayPropertyStructDescriptor());

	// Output state to string
	ToString(StringBuilder, reinterpret_cast<uint8*>(&ReceivedState), GetFastArrayPropertyStructDescriptor());
}

template <typename FastArrayItemType, typename FastArrayType>
bool TFastArrayReplicationFragment<FastArrayItemType, FastArrayType>::IsDirty() const
{
	FReplicationStateHeader& ReplicationStateHeader = Private::GetReplicationStateHeader(ReplicationState->GetStateBuffer(), ReplicationStateDescriptor);
	return Private::FReplicationStateHeaderAccessor::GetIsStateDirty(ReplicationStateHeader);
}

template <typename FastArrayItemType, typename FastArrayType>
bool TFastArrayReplicationFragment<FastArrayItemType, FastArrayType>::IsDirtyForPolling() const
{
	FReplicationStateHeader& ReplicationStateHeader = Private::GetReplicationStateHeader(ReplicationState->GetStateBuffer(), ReplicationStateDescriptor);
	bool bIsStateDirty = Private::FReplicationStateHeaderAccessor::GetIsStateDirty(ReplicationStateHeader);
	return bIsStateDirty || Private::GetMemberPollMask(ReplicationState->GetStateBuffer(), ReplicationStateDescriptor).IsAnyBitSet();
}

template <typename FastArrayItemType, typename FastArrayType>
void TFastArrayReplicationFragment<FastArrayItemType, FastArrayType>::MarkDirty()
{
	// Mark the NetObject as dirty
	if (ReplicationState->IsCustomConditionEnabled(FIrisFastArraySerializer::IrisFastArrayPropertyBitIndex))
	{
		FNetBitArrayView MemberChangeMask = UE::Net::Private::GetMemberChangeMask(ReplicationState->GetStateBuffer(), ReplicationStateDescriptor);
		MemberChangeMask.SetBit(FIrisFastArraySerializer::IrisFastArrayPropertyBitIndex);
		MarkNetObjectStateHeaderDirty(UE::Net::Private::GetReplicationStateHeader(ReplicationState->GetStateBuffer(), ReplicationStateDescriptor));
	}
}

template <typename FastArrayItemType, typename FastArrayType>
void TFastArrayReplicationFragment<FastArrayItemType, FastArrayType>::CallRepNotifies(FReplicationStateApplyContext& Context)
{
	const FReplicationStateDescriptor* Descriptor = ReplicationStateDescriptor;

	// If we get here we are either the init state or dirty
	// 仅当 init state 或 ChangeMask 显示脏才会到这里。
	if (const UFunction* RepNotifyFunction = Descriptor->MemberPropertyDescriptors[0].RepNotifyFunction)
	{
		// if this is the init state, we compare against default and early out if initial state does not differ from default (empty)
		// init 状态特殊处理：与"默认空 FastArray"比较；相同则跳过 OnRep（避免空数组也触发）。
		if (Context.bIsInit)
		{
			FastArrayType ReceivedState;
			FastArrayType DefaultState;
			InternalDequantizeFastArray(*Context.NetSerializationContext, reinterpret_cast<uint8*>(&ReceivedState), Context.StateBufferData.RawStateBuffer, GetFastArrayPropertyStructDescriptor());
			InternalDequantizeExtraProperties(*Context.NetSerializationContext, reinterpret_cast<uint8*>(&ReceivedState), Context.StateBufferData.RawStateBuffer, GetFastArrayPropertyStructDescriptor());			
			if (Descriptor->MemberProperties[0]->Identical(&ReceivedState, &DefaultState))
			{
				return;
			}
		}

		// 调用 OnRep_FastArray 函数 —— 注意 FastArray 的 OnRep 通常无参数（与普通属性 OnRep 不同）。
		Owner->ProcessEvent(const_cast<UFunction*>(RepNotifyFunction), nullptr);
	}
}

/*
 * TNativeFastArrayReplicationFragment implementation
 */
template <typename FastArrayItemType, typename FastArrayType, typename PollingPolicyType>
TNativeFastArrayReplicationFragment<FastArrayItemType, FastArrayType, PollingPolicyType>::TNativeFastArrayReplicationFragment(EReplicationFragmentTraits InTraits, UObject* InOwner, const FReplicationStateDescriptor* InDescriptor)
: FNativeFastArrayReplicationFragmentBase(InTraits, InOwner, InDescriptor)
{
	if (EnumHasAnyFlags(InTraits, EReplicationFragmentTraits::CanReplicate) && (PollingPolicy.GetPollingState() != nullptr))
	{
		// For PropertyReplicationStates we need to poll properties from our owner in order to detect state changes.
		Traits |= EReplicationFragmentTraits::NeedsPoll;
	}
}

template <typename FastArrayItemType, typename FastArrayType, typename PollingPolicyType>
void TNativeFastArrayReplicationFragment<FastArrayItemType, FastArrayType, PollingPolicyType>::Register(FFragmentRegistrationContext& Context, EReplicationFragmentTraits InTraits)
{
	Traits |= InTraits;
	Context.RegisterReplicationFragment(this, ReplicationStateDescriptor.GetReference(), reinterpret_cast<uint8*>(GetFastArraySerializerFromOwner()));
}

template <typename FastArrayItemType, typename FastArrayType, typename PollingPolicyType>
void TNativeFastArrayReplicationFragment<FastArrayItemType, FastArrayType, PollingPolicyType>::CallRepNotifies(FReplicationStateApplyContext& Context)
{
	const FReplicationStateDescriptor* Descriptor = ReplicationStateDescriptor;

	// if we get here we are either the init state or dirty
	if (const UFunction* RepNotifyFunction = Descriptor->MemberPropertyDescriptors[0].RepNotifyFunction)
	{
		// if this is the init state, we compare against default and early out if initial state does not differ from default (empty)
		if (Context.bIsInit)
		{
			FastArrayType ReceivedState;
			FastArrayType DefaultState;
			InternalDequantizeFastArray(*Context.NetSerializationContext, reinterpret_cast<uint8*>(&ReceivedState), Context.StateBufferData.RawStateBuffer, GetFastArrayPropertyStructDescriptor());
			if (Descriptor->MemberProperties[0]->Identical(&ReceivedState, &DefaultState))
			{
				return;
			}
		}

		Owner->ProcessEvent(const_cast<UFunction*>(RepNotifyFunction), nullptr);
	}
}

template <typename FastArrayItemType, typename FastArrayType, typename PollingPolicyType>
bool TNativeFastArrayReplicationFragment<FastArrayItemType, FastArrayType, PollingPolicyType>::PollReplicatedState(EReplicationFragmentPollFlags PollOption)
{
	// We ignore object references polling. Since the source state will have references cleaned up they will be valid once any affected item is dirtied.
	if (EnumHasAnyFlags(PollOption, EReplicationFragmentPollFlags::PollAllState | EReplicationFragmentPollFlags::PollDirtyState))
	{
		if (!EnumHasAnyFlags(EReplicationFragmentTraits::HasPushBasedDirtiness, Traits) || IsDirty())
		{
			return PollAllState();
		}
	}

	return IsDirty();
}

template <typename FastArrayItemType, typename FastArrayType, typename PollingPolicyType>
bool TNativeFastArrayReplicationFragment<FastArrayItemType, FastArrayType, PollingPolicyType>::PollAllState()
{
	using FPollingState = FastArrayPollingPolicies::FPollingState;
	if (FPollingState* PollingState = PollingPolicy.GetPollingState())
	{
		// Get the source FastArraySerializer and array
		FastArrayType* SrcArraySerializer = GetFastArraySerializerFromOwner();
		ItemArrayType* SrcWrappedArray = reinterpret_cast<ItemArrayType*>(reinterpret_cast<uint8*>(SrcArraySerializer) + WrappedArrayOffsetRelativeFastArraySerializerProperty);

		// Check if we can early out
		if (SrcArraySerializer->ArrayReplicationKey == PollingState->ArrayReplicationKey)
		{
			return IsDirty();
		}

		// First we must resize target to match size of source data
		bool bMarkArrayDirty = false;
		const uint32 ElementCount = SrcWrappedArray->Num();
		if (PollingState->ItemPollData.Num() != ElementCount)
		{
			PollingState->ItemPollData.SetNum(ElementCount);
			bMarkArrayDirty = true;
		}

		FNetBitArrayView MemberChangeMask = UE::Net::Private::FIrisFastArraySerializerPrivateAccessor::GetChangeMask(*SrcArraySerializer);

		// We currently use a simple modulo scheme for bits in the changemask
		// A single bit might represent several entries in the array which all will be considered dirty, it is up to the serializer to handle this
		// The first bit is used by the owning property we need to offset by one and deduct one from the usable bits
		const FReplicationStateMemberChangeMaskDescriptor& MemberChangeMaskDescriptor = ReplicationStateDescriptor->MemberChangeMaskDescriptors[0];
		const uint32 ChangeMaskBitOffset = MemberChangeMaskDescriptor.BitOffset + FIrisFastArraySerializer::IrisFastArrayChangeMaskBitOffset;
		const uint32 ChangeMaskBitCount = MemberChangeMaskDescriptor.BitCount - FIrisFastArraySerializer::IrisFastArrayChangeMaskBitOffset;

		FPollingState::FEntry* DstItems = PollingState->ItemPollData.GetData();
		FastArrayItemType* SrcItems = SrcWrappedArray->GetData();

		// Iterate over array entries and copy the statedata using internal data if it has changed
		for (uint32 ElementIt = 0, ElementEndIt = ElementCount; ElementIt < ElementEndIt; ++ElementIt)
		{
			FastArrayItemType& SrcItem = SrcItems[ElementIt];
			FPollingState::FEntry& DstItem = DstItems[ElementIt];

			const bool bIsWritingOnClient = false;
			if (SrcArraySerializer->template ShouldWriteFastArrayItem<FastArrayItemType, FastArrayType>(SrcItem, bIsWritingOnClient))
			{
				if (SrcItem.ReplicationID == INDEX_NONE)
				{
					SrcArraySerializer->MarkItemDirty(SrcItem);
				}

				if (SrcItem.ReplicationKey != DstItem.ReplicationKey || SrcItem.ReplicationID != DstItem.ReplicationID)
				{
					DstItem.ReplicationKey = SrcItem.ReplicationKey;
					DstItem.ReplicationID = SrcItem.ReplicationID;

					// Mark element as dirty and mark array as dirty as well.
					if (ChangeMaskBitCount)
					{
						MemberChangeMask.SetBit((ElementIt % ChangeMaskBitCount) + ChangeMaskBitOffset);
					}
					bMarkArrayDirty = true;
				}
			}
			else
			{
				ensureMsgf(false, TEXT("Native IrisFastArraySerializer does not support local non-replicated items, use FastArraySerializer intead if this is required."));

				// Even if the native variant do not really support not replicated entries we still mark the item dirty and update the stored data to keep logic working.
				if (SrcItem.ReplicationKey != DstItem.ReplicationKey || SrcItem.ReplicationID != DstItem.ReplicationID)
				{
					DstItem.ReplicationKey = SrcItem.ReplicationKey;
					DstItem.ReplicationID = SrcItem.ReplicationID;

					// Mark element as dirty and mark array as dirty as well.
					if (ChangeMaskBitCount)
					{
						MemberChangeMask.SetBit((ElementIt % ChangeMaskBitCount) + ChangeMaskBitOffset);
					}
					bMarkArrayDirty = true;
				}
			}
		}

		// We update this after the poll since every call to MarkItem() dirty will Increase the ArrayReplicationKey
		PollingState->ArrayReplicationKey = SrcArraySerializer->ArrayReplicationKey;

		if (bMarkArrayDirty)
		{
			const bool bHasCustomConditionals = EnumHasAnyFlags(ReplicationStateDescriptor->Traits, EReplicationStateTraits::HasLifetimeConditionals);
			if (!bHasCustomConditionals || UE::Net::Private::FIrisFastArraySerializerPrivateAccessor::GetConditionalChangeMask(*SrcArraySerializer).GetBit(FIrisFastArraySerializer::IrisFastArrayPropertyBitIndex))
			{
				UE::Net::Private::FIrisFastArraySerializerPrivateAccessor::MarkArrayDirty(*SrcArraySerializer);
			}
		}
	}

	return IsDirty();
}

template <typename FastArrayItemType, typename FastArrayType, typename PollingPolicyType>
bool TNativeFastArrayReplicationFragment<FastArrayItemType, FastArrayType, PollingPolicyType>::IsDirty() const
{
	FastArrayType* SrcArraySerializer = GetFastArraySerializerFromOwner();
	const FReplicationStateHeader& ReplicationStateHeader = Private::FIrisFastArraySerializerPrivateAccessor::GetReplicationStateHeader(*SrcArraySerializer);
	return Private::FReplicationStateHeaderAccessor::GetIsStateDirty(ReplicationStateHeader);
}

template <typename FastArrayItemType, typename FastArrayType, typename PollingPolicyType>
void TNativeFastArrayReplicationFragment<FastArrayItemType, FastArrayType, PollingPolicyType>::ApplyReplicatedState(FReplicationStateApplyContext& Context) const
{
	// As we must preserve local data and generate proper callbacks we must dequantize into a temporary state
	// We could do a selective operation and keep an targetstate around and only do incremental updates of this state
	FastArrayType ReceivedState;

	// Dequantize into temporary array
	InternalDequantizeFastArray(*Context.NetSerializationContext, reinterpret_cast<uint8*>(&ReceivedState), Context.StateBufferData.RawStateBuffer, GetFastArrayPropertyStructDescriptor());

	// Get the wrapped FastArraySerializer and array
	FastArrayType* DstArraySerializer = reinterpret_cast<FastArrayType*>(reinterpret_cast<uint8*>(Owner) + ReplicationStateDescriptor->MemberProperties[0]->GetOffset_ForGC());
	ItemArrayType* DstWrappedArray = reinterpret_cast<ItemArrayType*>((uint8*)(DstArraySerializer) + WrappedArrayOffsetRelativeFastArraySerializerProperty);

	// Intentionally not const as we allow the src state to be modified
	FastArrayType* SrcArraySerializer = &ReceivedState;
	const ItemArrayType* SrcWrappedArray = reinterpret_cast<const ItemArrayType*>(reinterpret_cast<uint8*>(&ReceivedState) + WrappedArrayOffsetRelativeFastArraySerializerProperty);	

	// Apply state and issue callbacks etc
	Private::FFastArrayReplicationFragmentHelper::ApplyReplicatedState(DstArraySerializer, DstWrappedArray, SrcArraySerializer, SrcWrappedArray, GetArrayElementDescriptor(), Context);
}

template <typename FastArrayItemType, typename FastArrayType, typename PollingPolicyType>
void TNativeFastArrayReplicationFragment<FastArrayItemType, FastArrayType, PollingPolicyType>::ReplicatedStateToString(FStringBuilderBase& StringBuilder, FReplicationStateApplyContext& Context, EReplicationStateToStringFlags Flags) const
{
	// Create temporary
	FastArrayType ReceivedState;

	// Dequantize into temporary array
	InternalDequantizeFastArray(*Context.NetSerializationContext, reinterpret_cast<uint8*>(&ReceivedState), Context.StateBufferData.RawStateBuffer, GetFastArrayPropertyStructDescriptor());

	// Output state to string
	ToString(StringBuilder, reinterpret_cast<uint8*>(&ReceivedState), GetFastArrayPropertyStructDescriptor());
}

template <typename FastArrayItemType, typename FastArrayType, typename PollingPolicyType>
FastArrayType* TNativeFastArrayReplicationFragment<FastArrayItemType, FastArrayType, PollingPolicyType>::GetFastArraySerializerFromOwner() const
{
	return reinterpret_cast<FastArrayType*>(reinterpret_cast<uint8*>(Owner) + ReplicationStateDescriptor->MemberProperties[0]->GetOffset_ForGC());
}

namespace Private {

template <typename FastArrayType>
FReplicationFragment* CreateAndRegisterFragment(UObject* Owner, const FReplicationStateDescriptor* Descriptor, FFragmentRegistrationContext& Context)
{
	using namespace UE::Net;
	// 编译期校验：FastArrayType 必须有恰好一个 TArray<DerivedFromFastArraySerializerItem> 成员。
	static_assert(TFastArrayTypeHelper<FastArrayType>::HasValidFastArrayItemType(), "Invalid FastArrayItemType detected. Make sure that FastArraySerializer has a single replicated property that is a dynamic array of the expected type");

	// 路径选择：
	//   1) FastArrayType 派生自 FIrisFastArraySerializer 且 Descriptor 标记 Native → 走原生 TNativeFastArrayReplicationFragment（推模式可省 poll）；
	//   2) 否则 → 走兼容 TFastArrayReplicationFragment。
	if constexpr (TIsDerivedFrom<FastArrayType, FIrisFastArraySerializer>::IsDerived)
	{
		if (EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::IsNativeFastArrayReplicationState))
		{
			typedef TNativeFastArrayReplicationFragment<typename TFastArrayTypeHelper<FastArrayType>::FastArrayItemType, FastArrayType, FastArrayPollingPolicies::FNeedPollingPolicy> FFragmentType;
			if (FFragmentType* Fragment = new FFragmentType(Context.GetFragmentTraits(), Owner, Descriptor))
			{
				Fragment->Register(Context, EReplicationFragmentTraits::DeleteWithInstanceProtocol);
				return Fragment;
			}
			return nullptr;
		}
	}

	typedef TFastArrayReplicationFragment<typename TFastArrayTypeHelper<FastArrayType>::FastArrayItemType, FastArrayType> FFragmentType;
	if (FFragmentType* Fragment = new FFragmentType(Context.GetFragmentTraits(), Owner, Descriptor))
	{
		Fragment->Register(Context, EReplicationFragmentTraits::DeleteWithInstanceProtocol);
		return Fragment;
	}
	return nullptr;
}

}} // End of namespaces


