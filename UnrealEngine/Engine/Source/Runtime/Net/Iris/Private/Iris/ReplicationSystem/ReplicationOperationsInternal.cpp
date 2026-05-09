// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// ReplicationOperationsInternal.cpp —— 内部 Operation 的实现
// ---------------------------------------------------------------------------------------------------------------------
// 实现 ReplicationOperationsInternal.h 中三套结构：
//   1) FReplicationInstanceOperationsInternal —— 与 NetHandle / 实例紧耦合：BindInstanceProtocol、QuantizeObjectStateData、
//        ResetObjectStateDirtiness。后者是 FReplicationSystemImpl::QuantizeDirtyStateData 的核心被调函数。
//   2) FReplicationStateOperationsInternal    —— 单个 state 的"动态状态 + 引用收集"。
//   3) FReplicationProtocolOperationsInternal —— 对象级别的"动态状态 + 引用收集 + 量化态比较"。
//
// 重要技术点：
//   - QuantizeObjectStateData 是 Iris 帧管线"PreSendUpdate → QuantizeDirtyStateData"阶段的执行体。
//     它会决定每对象是 FullCopyAndQuantize 还是 QuantizeIfDirty（CVar / 对象 flag 决定），
//     还会处理"父对象因 SubObject 改变而被一并标脏"的传播。
//   - CollectReferences 系列遍历 Descriptor 的 ObjectReferenceCount 个引用条目；如果是直接引用，按 offset 取
//     FQuantizedObjectReference；如果是嵌套结构 / 动态数组等"复合引用"，则递归到内层 Descriptor 或调用 Serializer
//     的 CollectNetReferences 自定义钩子。
// =====================================================================================================================

#include "ReplicationOperationsInternal.h"

#include "Iris/Core/IrisLog.h"
#include "Iris/Core/IrisProfiler.h"

#include "Net/Core/Trace/NetTrace.h"
#include "Net/Core/Trace/NetDebugName.h"

#include "Iris/ReplicationSystem/ChangeMaskCache.h"
#include "Iris/ReplicationSystem/ChangeMaskUtil.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"
#include "Iris/ReplicationSystem/ObjectReferenceCache.h"
#include "Iris/ReplicationSystem/ReplicationFragment.h"
#include "Iris/ReplicationSystem/ReplicationProtocol.h"
#include "Iris/ReplicationState/InternalReplicationStateDescriptorUtils.h"
#include "Iris/ReplicationState/ReplicationStateUtil.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetSerializer.h"
#include "Iris/ReplicationSystem/ReplicationOperations.h"
#include "Iris/Serialization/NetReferenceCollector.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/InternalNetSerializers.h"
#include "Iris/Serialization/QuantizedObjectReference.h"
#include "Math/NumericLimits.h"
#include "HAL/IConsoleManager.h"
#include "Iris/Core/IrisProfiler.h"
#include "Net/Core/Trace/NetDebugName.h"
#include "Iris/Stats/NetStatsContext.h"

namespace UE::Net::Private
{

static bool bCVarForceFullCopyAndQuantize = false;
static FAutoConsoleVariableRef CVarForceFullCopyAndQuantize(
	TEXT("net.iris.ForceFullCopyAndQuantize"),
	bCVarForceFullCopyAndQuantize,
	TEXT("When enabled a full copy and quantize will be used, if disabled we will only copy and quantize dirty state data."));

void FReplicationInstanceOperationsInternal::BindInstanceProtocol(FNetHandle NetHandle, FReplicationInstanceProtocol* InstanceProtocol, const FReplicationProtocol* Protocol)
{
	// 把 FNetHandle 的 Id 写入每个 ExternalSrcBuffer 的 FReplicationStateHeader（30-bit NetHandleId）。
	// 这是 PushModel 标脏的关键信息——只持有 UObject 指针的游戏代码无法直接定位 Iris 内部索引；
	// 把 Id 写入"用户态状态缓冲"头部后，标脏代码（LegacyPushModel / FIrisFastArraySerializer 等）即可读出来上报。
	FReplicationInstanceProtocol::FFragmentData* FragmentData = InstanceProtocol->FragmentData;
	const FReplicationStateDescriptor** Descriptors = Protocol->ReplicationStateDescriptors;

	const uint32 FragmentCount = InstanceProtocol->FragmentCount;
	for (uint32 It = 0; It < FragmentCount; ++It)
	{
		if (FragmentData[It].ExternalSrcBuffer)
		{
			UE::Net::FReplicationStateHeader& ReplicationStateHeader = UE::Net::Private::GetReplicationStateHeader(FragmentData[It].ExternalSrcBuffer, Descriptors[It]);

			// Can't overwrite a bound header with a valid NetHandle.
			// 不允许"已经被绑定"的 header 再次绑定一个新有效 handle——只能先 Unbind 才能换 handle。
			check(!ReplicationStateHeader.IsBound() || !NetHandle.IsValid());

			FReplicationStateHeaderAccessor::SetNetHandleId(ReplicationStateHeader, NetHandle);
		}
	}

	// 同步更新 InstanceProtocol 的 IsBound trait —— Bridge / Filtering 等基于此 trait 决定是否参与某些路径。
	InstanceProtocol->InstanceTraits = NetHandle.IsValid() ? InstanceProtocol->InstanceTraits | EReplicationInstanceProtocolTraits::IsBound : InstanceProtocol->InstanceTraits &= ~EReplicationInstanceProtocolTraits::IsBound;
}

void FReplicationInstanceOperationsInternal::UnbindInstanceProtocol(FReplicationInstanceProtocol* InstanceProtocol, const FReplicationProtocol* Protocol)
{ 
	// 解绑：等价于绑定一个空 handle（FNetHandle()）。
	if (EnumHasAnyFlags(InstanceProtocol->InstanceTraits, EReplicationInstanceProtocolTraits::IsBound))
	{
		BindInstanceProtocol(FNetHandle(), InstanceProtocol, Protocol);
	}
}

uint32 FReplicationInstanceOperationsInternal::QuantizeObjectStateData(FNetBitStreamWriter& ChangeMaskWriter, FChangeMaskCache& Cache, FNetRefHandleManager& NetRefHandleManager, FNetSerializationContext& SerializationContext, uint32 InternalIndex)
{
	// 单对象 Quantize 的执行函数（FReplicationSystemImpl::QuantizeDirtyStateData 中的并行任务体）。
	// 返回 1 表示成功 quantize 一份对象，0 表示跳过。
	if (NetRefHandleManager.IsScopableIndex(InternalIndex))
	{
		FNetRefHandleManager::FReplicatedObjectData& Object = NetRefHandleManager.GetReplicatedObjectDataNoCheck(InternalIndex);

		// We cannot quantize state data for zero sized objects or objects that no longer has an instance protocol.
		// 已无 InstanceProtocol（被 Detach / Destroy）或 Protocol 内部状态空，无需 quantize。
		if (Object.InstanceProtocol && Object.Protocol->InternalTotalSize > 0U)
		{
			IRIS_PROFILER_PROTOCOL_NAME(Object.Protocol->DebugName->Name);
			UE_NET_IRIS_STATS_TIMER(Timer, SerializationContext.GetNetStatsContext());
			UE_NET_TRACE_QUANTIZE_OBJECT_SCOPE(Object.RefHandle, Timer);

			// if the object was scopable prev frame we can do partial copy
			// 上一帧已在 scope 内 → 允许"部分 copy/quantize"；新进 scope 的对象（上帧不在）必须做 FullCopyAndQuantize 以得到完整 baseline。
			bool bShouldPropagateChangedStates = Object.bShouldPropagateChangedStates;

			// We do a full CopyAndQuantize if the cvar bCVarForceFullCopyAndQuantize is set or that the object is marked as needing a FullCopyAndQuantize
			// 是否做全量 quantize：cvar 开关 OR 对象自身被标记。标记完成后在本调用清除，避免下一帧再次全量。
			const bool bUseFullCopyAndQuantize = bCVarForceFullCopyAndQuantize || Object.bNeedsFullCopyAndQuantize;
			Object.bNeedsFullCopyAndQuantize = 0U;

			// Quantize dirty state
			{
				// Add entry to cache
				// 在 ChangeMaskCache 中分配一段位图槽位用于存放本帧对象级 ChangeMask。
				FChangeMaskCache::FCachedInfo& Info = Cache.AddChangeMaskForObject(InternalIndex, Object.Protocol->ChangeMaskBitCount);
				const uint32 ChangeMaskByteCount = FNetBitArrayView::CalculateRequiredWordCount(Object.Protocol->ChangeMaskBitCount) * 4;
				ChangeMaskStorageType* ChangeMaskData = Cache.GetChangeMaskStorage(Info);
				ChangeMaskWriter.InitBytes(ChangeMaskData, ChangeMaskByteCount);

				//UE_LOG(LogIris, Log, TEXT("Copying state data for ( InternalIndex: %u ) with NetRefHandle (Id=%u)"), InternalIndex, Object.RefHandle.GetId());
				if (bUseFullCopyAndQuantize)
				{
					FReplicationInstanceOperations::Quantize(SerializationContext, NetRefHandleManager.GetReplicatedObjectStateBufferNoCheck(InternalIndex), &ChangeMaskWriter, Object.InstanceProtocol, Object.Protocol);					
				}
				else
				{
					FReplicationInstanceOperations::QuantizeIfDirty(SerializationContext, NetRefHandleManager.GetReplicatedObjectStateBufferNoCheck(InternalIndex), &ChangeMaskWriter, Object.InstanceProtocol, Object.Protocol);
				}

				// 记录"本对象本帧是否真有脏位"——后续 Writer 调度优先级时使用。
				Info.bHasDirtyChangeMask = MakeNetBitArrayView(ChangeMaskData, ChangeMaskByteCount * 8U).FindFirstOne() != FNetBitArrayView::InvalidIndex;
			}

			// Mark subobject owner as dirty if this is a subobject
			// SubObject 脏要传播到父对象（保证父子原子可见性 / Filtering scope 一致）。
			if (const uint32 SubObjectOwnerIndex = Object.SubObjectRootIndex)
			{
				const bool bIsOwnerScopable = NetRefHandleManager.IsScopableIndex(SubObjectOwnerIndex);
				// Dependent objects should not ensure if the owner isn't scopable. Subobjects pending tear off is ok too.
				ensureMsgf(bIsOwnerScopable || Object.IsDependentObject() || Object.bTearOff, TEXT("SubObject ( InternaIndex: %u ) with NetRefHandle (Id=%" UINT64_FMT ") is trying to dirty parent ( InternalIndex: %u ) not in scope."), InternalIndex, Object.RefHandle.GetId(), SubObjectOwnerIndex);
				if (bIsOwnerScopable)
				{
					// Do we want to control this separately for subobjects? Or should they respect the setting on the owner?
					// For now, we do and will not mark owner as dirty if owner should not propagate statechanges
					// 子对象脏只在父对象也允许 propagate 时才向上传播；否则两者均"沉默"。
					bShouldPropagateChangedStates = bShouldPropagateChangedStates && NetRefHandleManager.GetReplicatedObjectDataNoCheck(SubObjectOwnerIndex).bShouldPropagateChangedStates;
					if (bShouldPropagateChangedStates)
					{
						//UE_LOG(LogIris, Log, TEXT("Marking SubObjectOwner( InternalIndex: %u ) as dirty for ( InternalIndex: %u ) with NetRefHandle (Id=%u)"), SubObjectOwnerIndex, InternalIndex, Object.RefHandle.GetId());
						Cache.AddSubObjectOwnerDirty(SubObjectOwnerIndex);
					}
				}
			}

			// If changed states should not be propagated, we must pop the last entry added to the cache
			// dormant 等不传播的对象：刚 quantize 的结果必须撤回，避免被 Writer 取走。
			if (!bShouldPropagateChangedStates)
			{
				Cache.PopLastEntry();
			}

			UE_NET_IRIS_STATS_ADD_TIME_AND_COUNT_FOR_OBJECT(Timer, Quantize, InternalIndex);

			return 1U;
		}
		else if (const uint32 SubObjectOwnerIndex = Object.SubObjectRootIndex)
		{
			// 自身没有内部状态、但是 SubObject —— 仍需把父对象标脏（例如纯结构层级/事件型子对象）。
			if (Object.bShouldPropagateChangedStates && ensure(NetRefHandleManager.IsScopableIndex(SubObjectOwnerIndex)))
			{	
				// Do we want to control this separately for subobjects? Or should they respect the setting on the owner?
				// For now, we do and will not mark owner as dirty if owner should not propagate statechanges
				if (NetRefHandleManager.GetReplicatedObjectDataNoCheck(SubObjectOwnerIndex).bShouldPropagateChangedStates)
				{
					//UE_LOG(LogIris, Log, TEXT("Marking SubObjectOwner( InternalIndex: %u ) as dirty for ( InternalIndex: %u ) with NetRefHandle (Id=%u)"), SubObjectOwnerIndex, InternalIndex, Object.Handle.GetIndex());
					Cache.AddSubObjectOwnerDirty(SubObjectOwnerIndex);
				}
			}			
		}
	}
	else
	{
		// Deal with error, object not found
		// Tracker / Bridge 与 NetRefHandleManager 不一致 —— 多见于销毁 / scope 边缘竞态，留警告便于排查。
		UE_LOG(LogIris, Warning, TEXT("CopyObjectStateData called on object ( InternalIndex: %u ) not in scope"), InternalIndex);
	}

	return 0U;
}

void FReplicationInstanceOperationsInternal::ResetObjectStateDirtiness(FNetRefHandleManager& NetRefHandleManager, uint32 InternalIndex)
{
	if (NetRefHandleManager.IsScopableIndex(InternalIndex))
	{
		const FNetRefHandleManager::FReplicatedObjectData& Object = NetRefHandleManager.GetReplicatedObjectDataNoCheck(InternalIndex);
		// Only instance protocols with state date can have dirtiness.
		if (Object.InstanceProtocol && Object.Protocol->InternalTotalSize > 0U)
		{
			FReplicationInstanceOperations::ResetDirtiness(Object.InstanceProtocol, Object.Protocol);
		}
	}
}

void FReplicationStateOperationsInternal::CloneDynamicState(FNetSerializationContext& Context, uint8* RESTRICT DstInternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberTraitsDescriptor* MemberTraitsDescriptors = Descriptor->MemberTraitsDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberTraitsDescriptor& MemberTraitsDescriptor = MemberTraitsDescriptors[MemberIt];
		if (!EnumHasAnyFlags(MemberTraitsDescriptor.Traits, EReplicationStateMemberTraits::HasDynamicState))
		{
			continue;
		}

		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FNetSerializer* Serializer = MemberSerializerDescriptor.Serializer;
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];

		FNetCloneDynamicStateArgs MemberArgs;
		MemberArgs.Version = 0;
		MemberArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		MemberArgs.Source = NetSerializerValuePointer(SrcInternalBuffer) + MemberDescriptor.InternalMemberOffset;
		MemberArgs.Target = NetSerializerValuePointer(DstInternalBuffer) + MemberDescriptor.InternalMemberOffset;

		Serializer->CloneDynamicState(Context, MemberArgs);
	}
}

void FReplicationStateOperationsInternal::CloneQuantizedState(FNetSerializationContext& Context, uint8* RESTRICT DstInternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	// Memcopy state storage
	FMemory::Memcpy(DstInternalBuffer, SrcInternalBuffer, Descriptor->InternalSize);

	// If no member has dynamic state then there's nothing for us to do
	if (!EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::HasDynamicState))
	{
		return;
	}

	// Clone dynamic state
	CloneDynamicState(Context, DstInternalBuffer, SrcInternalBuffer, Descriptor);
}

void FReplicationStateOperationsInternal::FreeDynamicState(FNetSerializationContext& Context, uint8* ObjectStateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const FReplicationStateMemberTraitsDescriptor* MemberTraitsDescriptors = Descriptor->MemberTraitsDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberTraitsDescriptor& MemberTraitsDescriptor = MemberTraitsDescriptors[MemberIt];
		if (!EnumHasAnyFlags(MemberTraitsDescriptor.Traits, EReplicationStateMemberTraits::HasDynamicState))
		{
			continue;
		}

		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIt];
		const FNetSerializer* Serializer = MemberSerializerDescriptor.Serializer;
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];

		FNetFreeDynamicStateArgs MemberArgs;
		MemberArgs.Version = 0;
		MemberArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		MemberArgs.Source = NetSerializerValuePointer(ObjectStateBuffer) + MemberDescriptor.InternalMemberOffset;

		Serializer->FreeDynamicState(Context, MemberArgs);
	}
}

void FReplicationStateOperationsInternal::FreeDynamicState(uint8* ObjectStateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	FNetSerializationContext Context;
	FInternalNetSerializationContext InternalContext;
	Context.SetInternalContext(&InternalContext);

	FReplicationStateOperationsInternal::FreeDynamicState(Context, ObjectStateBuffer, Descriptor);
}

void FReplicationStateOperationsInternal::CollectReferences(FNetSerializationContext& Context, FNetReferenceCollector& Collector, const FNetSerializerChangeMaskParam& OuterChangeMaskInfo, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	const FReplicationStateMemberReferenceDescriptor* ReferenceDescriptors = Descriptor->MemberReferenceDescriptors;
	
	for (const FReplicationStateMemberReferenceDescriptor& MemberReferenceDescriptor : MakeArrayView(ReferenceDescriptors, Descriptor->ObjectReferenceCount))
	{
		// Direct reference located in the buffer, we just need the offset to the NetObjectReference
		if (MemberReferenceDescriptor.Info.ResolveType != FNetReferenceInfo::EResolveType::Invalid)
		{
			const FQuantizedObjectReference& QuantizedReference = *reinterpret_cast<const FQuantizedObjectReference*>(SrcInternalBuffer + MemberReferenceDescriptor.Offset);
			
			// Only collect FNetObjectReferences, if it's a remote reference it doesn't need to be collected
			Collector.Add(MemberReferenceDescriptor.Info, QuantizedReference, OuterChangeMaskInfo);
		}
		else
		{
			// For dynamic arrays and serializers with custom references we need to find the actual MemberDescriptor so that we can invoke the CollectNetRefrences function
			const FReplicationStateMemberReferenceDescriptor* CurrentReferenceDescriptor = &MemberReferenceDescriptor;
			const FReplicationStateDescriptor* CurrentDescriptor = Descriptor;
			const uint8* RESTRICT CurrentInternalBuffer = SrcInternalBuffer;

			for (;;)
			{
				const uint32 MemberIndex = CurrentReferenceDescriptor->MemberIndex;
				const FReplicationStateMemberSerializerDescriptor* SerializerInfo = &CurrentDescriptor->MemberSerializerDescriptors[MemberIndex];
				const FReplicationStateMemberDescriptor& MemberDescriptor = CurrentDescriptor->MemberDescriptors[MemberIndex];	
				const uint16 InnerReferenceIndex = CurrentReferenceDescriptor->InnerReferenceIndex;

				const bool bIsMemberWithCustomReferences = InnerReferenceIndex == MAX_uint16;
				if (bIsMemberWithCustomReferences)
				{
					// Verify that this is a dynamic array, or a serializer with custom references otherwise something is seriously broken
					checkSlow(IsUsingArrayPropertyNetSerializer(*SerializerInfo) || EnumHasAnyFlags(SerializerInfo->Serializer->Traits, ENetSerializerTraits::HasCustomNetReference));

					FNetCollectReferencesArgs Args = {};
					Args.Source = reinterpret_cast<NetSerializerValuePointer>(CurrentInternalBuffer + MemberDescriptor.InternalMemberOffset);
					Args.NetSerializerConfig = SerializerInfo->SerializerConfig;
					Args.ChangeMaskInfo = OuterChangeMaskInfo;
					Args.Collector = reinterpret_cast<NetSerializerValuePointer>(&Collector);
					Args.Version = 0U;
					
					SerializerInfo->Serializer->CollectNetReferences(Context, Args);
					break;
				}

				// The reference was to a nested inner reference
				const FStructNetSerializerConfig* StructConfig = static_cast<const FStructNetSerializerConfig*>(SerializerInfo->SerializerConfig);
				CurrentDescriptor = StructConfig->StateDescriptor;
				CurrentReferenceDescriptor = &StructConfig->StateDescriptor->MemberReferenceDescriptors[InnerReferenceIndex];
				CurrentInternalBuffer = CurrentInternalBuffer + MemberDescriptor.InternalMemberOffset;
			}
		}
	}
}

void FReplicationStateOperationsInternal::CollectReferencesWithMask(FNetSerializationContext& Context, FNetReferenceCollector& Collector, const uint32 ChangeMaskOffset, const uint8* RESTRICT SrcInternalBuffer,  const FReplicationStateDescriptor* Descriptor)
{
	const FReplicationStateMemberReferenceDescriptor* ReferenceDescriptors = Descriptor->MemberReferenceDescriptors;
	const FReplicationStateMemberChangeMaskDescriptor* ChangeMaskDescriptors = Descriptor->MemberChangeMaskDescriptors;
	const FNetBitArrayView* ChangeMask = Context.GetChangeMask();

	for (const FReplicationStateMemberReferenceDescriptor& MemberReferenceDescriptor : MakeArrayView(ReferenceDescriptors, Descriptor->ObjectReferenceCount))
	{
		checkSlow((ChangeMaskOffset + ChangeMaskDescriptors[MemberReferenceDescriptor.MemberIndex].BitOffset) < MAX_uint16);

		const FNetSerializerChangeMaskParam LocalChangeMaskInfo = FNetSerializerChangeMaskParam( { (uint16)(ChangeMaskOffset + ChangeMaskDescriptors[MemberReferenceDescriptor.MemberIndex].BitOffset), ChangeMaskDescriptors[MemberReferenceDescriptor.MemberIndex].BitCount } );
		if (ChangeMask && !ChangeMask->IsAnyBitSet(LocalChangeMaskInfo.BitOffset, LocalChangeMaskInfo.BitCount))
		{
			continue;
		}
	
		// Direct reference located in the buffer, we just need the offset to the NetObjectReference
		if (MemberReferenceDescriptor.Info.ResolveType != FNetReferenceInfo::EResolveType::Invalid)
		{
			const FQuantizedObjectReference& QuantizedReference = *reinterpret_cast<const FQuantizedObjectReference*>(SrcInternalBuffer + MemberReferenceDescriptor.Offset);

			Collector.Add(MemberReferenceDescriptor.Info, QuantizedReference, LocalChangeMaskInfo);
		}
		else
		{
			// For dynamic arrays and serializers with custom references we need to find the actual MemberDescriptor so that we can invoke the CollectNetRefrences function
			const FReplicationStateMemberReferenceDescriptor* CurrentReferenceDescriptor = &MemberReferenceDescriptor;
			const FReplicationStateDescriptor* CurrentDescriptor = Descriptor;
			const uint8* RESTRICT CurrentInternalBuffer = SrcInternalBuffer;

			for (;;)
			{
				const uint32 MemberIndex = CurrentReferenceDescriptor->MemberIndex;
				const FReplicationStateMemberSerializerDescriptor* SerializerInfo = &CurrentDescriptor->MemberSerializerDescriptors[MemberIndex];
				const FReplicationStateMemberDescriptor& MemberDescriptor = CurrentDescriptor->MemberDescriptors[MemberIndex];	
				const uint16 InnerReferenceIndex = CurrentReferenceDescriptor->InnerReferenceIndex;

				const bool bIsMemberWithCustomReferences = InnerReferenceIndex == MAX_uint16;
				if (bIsMemberWithCustomReferences)
				{
					// Verify that this is a dynamic array, or a serializer with custom references otherwise something is seriously broken
					checkSlow(IsUsingArrayPropertyNetSerializer(*SerializerInfo) || EnumHasAnyFlags(SerializerInfo->Serializer->Traits, ENetSerializerTraits::HasCustomNetReference));

					FNetCollectReferencesArgs Args = {};
					Args.Source = reinterpret_cast<NetSerializerValuePointer>(CurrentInternalBuffer + MemberDescriptor.InternalMemberOffset);
					Args.NetSerializerConfig = SerializerInfo->SerializerConfig;
					Args.ChangeMaskInfo = LocalChangeMaskInfo;
					Args.Collector = reinterpret_cast<NetSerializerValuePointer>(&Collector);
					Args.Version = 0U;
					
					SerializerInfo->Serializer->CollectNetReferences(Context, Args);
					break;
				}

				// The reference was to a nested inner reference
				const FStructNetSerializerConfig* StructConfig = static_cast<const FStructNetSerializerConfig*>(SerializerInfo->SerializerConfig);
				CurrentDescriptor = StructConfig->StateDescriptor;
				CurrentReferenceDescriptor = &StructConfig->StateDescriptor->MemberReferenceDescriptors[InnerReferenceIndex];
				CurrentInternalBuffer = CurrentInternalBuffer + MemberDescriptor.InternalMemberOffset;
			}
		}
	}
}

void FReplicationProtocolOperationsInternal::CloneDynamicState(FNetSerializationContext& Context, uint8* RESTRICT DstObjectStateBuffer, const uint8* RESTRICT SrcObjectStateBuffer, const FReplicationProtocol* Protocol)
{
	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;
	uint8* CurrentInternalDstStateBuffer = DstObjectStateBuffer;
	const uint8* CurrentInternalSrcStateBuffer = SrcObjectStateBuffer;

	for (uint32 StateIt = 0, StateEndIt = Protocol->ReplicationStateCount; StateIt < StateEndIt; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];

		CurrentInternalDstStateBuffer = Align(CurrentInternalDstStateBuffer, CurrentDescriptor->InternalAlignment);
		CurrentInternalSrcStateBuffer = Align(CurrentInternalSrcStateBuffer, CurrentDescriptor->InternalAlignment);

		if (EnumHasAnyFlags(CurrentDescriptor->Traits, EReplicationStateTraits::HasDynamicState))
		{
			FReplicationStateOperationsInternal::CloneDynamicState(Context, CurrentInternalDstStateBuffer, CurrentInternalSrcStateBuffer, CurrentDescriptor);
		}

		CurrentInternalDstStateBuffer += CurrentDescriptor->InternalSize;
		CurrentInternalSrcStateBuffer += CurrentDescriptor->InternalSize;
	}
}

void FReplicationProtocolOperationsInternal::FreeDynamicState(FNetSerializationContext& Context, uint8* RESTRICT ObjectStateBuffer, const FReplicationProtocol* Protocol)
{
	const FReplicationStateDescriptor** ReplicationStateDescriptors = Protocol->ReplicationStateDescriptors;
	uint8* CurrentInternalStateBuffer = ObjectStateBuffer;

	for (uint32 StateIt = 0, StateEndIt = Protocol->ReplicationStateCount; StateIt < StateEndIt; ++StateIt)
	{
		const FReplicationStateDescriptor* CurrentDescriptor = ReplicationStateDescriptors[StateIt];

		CurrentInternalStateBuffer = Align(CurrentInternalStateBuffer, CurrentDescriptor->InternalAlignment);
		
		if (EnumHasAnyFlags(CurrentDescriptor->Traits, EReplicationStateTraits::HasDynamicState))
		{
			FReplicationStateOperationsInternal::FreeDynamicState(Context, CurrentInternalStateBuffer, CurrentDescriptor);
		}
		
		CurrentInternalStateBuffer += CurrentDescriptor->InternalSize;
	}
}

void FReplicationProtocolOperationsInternal::CollectReferences(FNetSerializationContext& Context, FNetReferenceCollector& Collector, const uint8* RESTRICT SrcInternalStateBuffer, const FReplicationProtocol* Protocol)
{
	const FNetSerializerChangeMaskParam InitStateChangeMaskInfo = {};
	const bool bIncludeInitStates = Context.IsInitState();
	const uint8* CurrentInternalStateBuffer = SrcInternalStateBuffer;
	uint32 CurrentChangeMaskBitOffset = 0;

	for (const FReplicationStateDescriptor* CurrentDescriptor : MakeArrayView(Protocol->ReplicationStateDescriptors, Protocol->ReplicationStateCount))
	{
		CurrentInternalStateBuffer = Align(CurrentInternalStateBuffer, CurrentDescriptor->InternalAlignment);
		
		if (CurrentDescriptor->HasObjectReference())
		{
			if (!CurrentDescriptor->IsInitState())
			{
				FReplicationStateOperationsInternal::CollectReferencesWithMask(Context, Collector, CurrentChangeMaskBitOffset, CurrentInternalStateBuffer, CurrentDescriptor);
			}
			else if (bIncludeInitStates)
			{
				FReplicationStateOperationsInternal::CollectReferences(Context, Collector, InitStateChangeMaskInfo, CurrentInternalStateBuffer, CurrentDescriptor);
			}
		}
		
		CurrentInternalStateBuffer += CurrentDescriptor->InternalSize;
		CurrentChangeMaskBitOffset += CurrentDescriptor->ChangeMaskBitCount;
	}
}

void FReplicationProtocolOperationsInternal::CloneQuantizedState(FNetSerializationContext& Context, uint8* RESTRICT DstObjectStateBuffer, const uint8* RESTRICT SrcObjectStateBuffer, const FReplicationProtocol* Protocol)
{
	FMemory::Memcpy(DstObjectStateBuffer, SrcObjectStateBuffer, Protocol->InternalTotalSize);
	if (EnumHasAnyFlags(Protocol->ProtocolTraits, EReplicationProtocolTraits::HasDynamicState))
	{
		FReplicationProtocolOperationsInternal::CloneDynamicState(Context, DstObjectStateBuffer, SrcObjectStateBuffer, Protocol);
		check(!Context.HasError());
	}
}

bool FReplicationProtocolOperationsInternal::IsEqualQuantizedState(FNetSerializationContext& Context, const uint8* RESTRICT State0, const uint8* RESTRICT State1, const FReplicationProtocol* Protocol)
{
	const bool bIncludeInitStates = Context.IsInitState();
	const uint8* CurrentState0 = State0;
	const uint8* CurrentState1 = State1;

	for (const FReplicationStateDescriptor* CurrentDescriptor : MakeArrayView(Protocol->ReplicationStateDescriptors, Protocol->ReplicationStateCount))
	{
		CurrentState0 = Align(CurrentState0, CurrentDescriptor->InternalAlignment);
		CurrentState1 = Align(CurrentState1, CurrentDescriptor->InternalAlignment);
		
		if (!CurrentDescriptor->IsInitState() || bIncludeInitStates)
		{
			const bool bStateIsEqual = FReplicationStateOperations::IsEqualQuantizedState(Context, CurrentState0, CurrentState1, CurrentDescriptor);
			if (!bStateIsEqual)
			{
				return false;
			}
		}

		CurrentState0 += CurrentDescriptor->InternalSize;
		CurrentState1 += CurrentDescriptor->InternalSize;
	}

	return true;
}

}
