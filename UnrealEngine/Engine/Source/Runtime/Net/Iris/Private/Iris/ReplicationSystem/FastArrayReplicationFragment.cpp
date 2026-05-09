// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// FastArrayReplicationFragment.cpp —— FastArray Fragment 基类实现
// ---------------------------------------------------------------------------------------------------------------------
// 实现 FastArrayReplicationFragmentInternal.h 中两个抽象基类（兼容路径 + 原生路径）的具体方法：
//   - FFastArrayReplicationFragmentBase     → 兼容 FFastArraySerializer 的 fragment 基类。
//   - FNativeFastArrayReplicationFragmentBase → 原生 IrisFastArraySerializer 的 fragment 基类。
//
// 关键工具：
//   - GetFastArrayPropertyStructDescriptor —— 取 FastArray 这层 USTRUCT 的 Descriptor。
//   - GetArrayElementDescriptor             —— 取 TArray<Item>::Item 这层的 Descriptor。
//   - WrappedArrayOffsetRelativeFastArraySerializerProperty —— FastArray USTRUCT 内 TArray 字段相对 FastArraySerializer 起点的偏移。
//   - InternalDequantizeFastArray            —— 全量反量化 TArray 段（不含 FastArray 自身 extra 属性）。
//   - InternalPartialDequantizeFastArray     —— 按 ChangeMask 选择性反量化（接收 delta 用）。
//   - InternalDequantizeExtraProperties      —— 反量化 FastArray USTRUCT 的"额外"UPROPERTY 字段。
//   - InternalCopyArrayElement / InternalCompareArrayElement —— 仅作用在"复制成员"，保留客户态字段。
//
// 构造时计算的两类元数据（缓存到成员上，O(1) 取用）：
//   1) Descriptor 校验：Iris 强制 IsFastArrayReplicationState；NativeFastArray 必须同时含两个 trait。
//   2) 在 USTRUCT 描述符中找 ItemArray 成员索引（GetFastArrayStructItemArrayMemberIndex），并据此算 WrappedArrayOffset。
//
// PushModel 联动（仅兼容路径）：
//   - 若 Descriptor 含 HasPushBasedDirtiness，构造时调用 FFastArraySerializer::CachePushModelState 把 Owner+RepIndex
//     存到 FastArraySerializer，让后续 MarkItemDirty / MarkArrayDirty 能找到 FGlobalDirtyNetObjectTracker。
// =====================================================================================================================

#include "Iris/ReplicationSystem/FastArrayReplicationFragment.h"
#include "CoreTypes.h"
#include "Iris/Core/IrisLog.h"
#include "Net/Core/NetBitArray.h"
#include "Iris/ReplicationState/InternalPropertyReplicationState.h"
#include "Iris/ReplicationState/PropertyReplicationState.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"
#include "Iris/ReplicationState/ReplicationStateUtil.h"
#include "Iris/Serialization/InternalNetSerializers.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/InternalNetSerializerUtils.h"
#include "Iris/Core/IrisProfiler.h"

namespace UE::Net::Private
{

const FReplicationStateDescriptor* FFastArrayReplicationFragmentBase::GetFastArrayPropertyStructDescriptor() const
{
	const FReplicationStateMemberSerializerDescriptor& FastArrayMemberSerializerDescriptor = ReplicationStateDescriptor->MemberSerializerDescriptors[0];
	const FStructNetSerializerConfig* FastArrayStructSerializerConfig = static_cast<const FStructNetSerializerConfig*>(FastArrayMemberSerializerDescriptor.SerializerConfig);

	return FastArrayStructSerializerConfig->StateDescriptor;
}

FFastArrayReplicationFragmentBase::FFastArrayReplicationFragmentBase(EReplicationFragmentTraits InTraits, UObject* InOwner, const FReplicationStateDescriptor* InDescriptor, bool bValidateDescriptor)
: FReplicationFragment(InTraits)
, ReplicationStateDescriptor(InDescriptor)
, Owner(InOwner)
{
	// Verify that class descriptor is of expected type
	// 兼容路径要求 Descriptor 必须是 IsFastArrayReplicationState 但不是 IsNativeFastArrayReplicationState。
	{
		check(EnumHasAnyFlags(InDescriptor->Traits, EReplicationStateTraits::IsFastArrayReplicationState));
		check(!EnumHasAnyFlags(InDescriptor->Traits, EReplicationStateTraits::IsNativeFastArrayReplicationState));
	}

	// We do not create temporary states
	// FastArray Apply 走 RawStateBuffer + ChangeMask 路径，不让 Iris 帮忙构造临时 ExternalState（avoid 重复反量化）。
	Traits |= EReplicationFragmentTraits::HasPersistentTargetStateBuffer;

	if (EnumHasAnyFlags(InTraits, EReplicationFragmentTraits::CanReplicate))
	{
		// 服务端：分配 FPropertyReplicationState 用作"上一帧 FastArray 镜像"，poll 阶段比较用。
		ReplicationState = MakeUnique<FPropertyReplicationState>(InDescriptor);
	}
	
	if (EnumHasAnyFlags(InTraits, EReplicationFragmentTraits::CanReceive))
	{
		if (EnumHasAnyFlags(InDescriptor->Traits, EReplicationStateTraits::HasRepNotifies))
		{
			if (EnumHasAnyFlags(InDescriptor->Traits, EReplicationStateTraits::KeepPreviousState))
			{
				// FastArray 不支持"OnRep 旧值"语义（Native FastArray RepNotify 始终无参数）。
				check(false);
			}

			Traits |= EReplicationFragmentTraits::HasRepNotifies;
		}

		// For now we always expect pre/post operations for legacy states, we might make this optional
		Traits |= EReplicationFragmentTraits::NeedsLegacyCallbacks;
	}
	
	if (EnumHasAnyFlags(InTraits, EReplicationFragmentTraits::CanReplicate))
	{
		// For PropertyReplicationStates we need to poll properties from our owner in order to detect state changes.
		// FastArray 兼容路径必然 NeedsPoll：除非用户走原生 + Editor 推模式（那条走 NativeFastArray 路径）。
		Traits |= EReplicationFragmentTraits::NeedsPoll;
	}

	// Propagate object reference.
	if (EnumHasAnyFlags(InDescriptor->Traits, EReplicationStateTraits::HasObjectReference))
	{
		Traits |= EReplicationFragmentTraits::HasObjectReference;
	}

	Traits |= EReplicationFragmentTraits::HasPropertyReplicationState;
	Traits |= EReplicationFragmentTraits::SupportsPartialDequantizedState;

	// Look up the descriptor of the struct
	// 取 FastArray 那层 USTRUCT 的 Descriptor —— Iris 把外层"包装属性"和内层"FastArray struct"分开烘焙。
	const FReplicationStateDescriptor* FastArrayStructDescriptor = GetFastArrayPropertyStructDescriptor();

	// And we are looking for the offset of ItemArray which is the relative offset from the FastArray struct
	// 找到 TArray<Item> 成员在 USTRUCT 中的下标，缓存其 ExternalMemberOffset。
	// 后续 GetFastArraySerializerFromOwner 等只需 (Owner + Property->Offset_ForGC) + WrappedArrayOffsetRelativeFastArraySerializerProperty。
	const uint32 ItemArrayMemberIndex = FFastArrayReplicationFragmentHelper::GetFastArrayStructItemArrayMemberIndex(FastArrayStructDescriptor);
	WrappedArrayOffsetRelativeFastArraySerializerProperty = FastArrayStructDescriptor->MemberDescriptors[ItemArrayMemberIndex].ExternalMemberOffset;

	// Validate the expected member count
	// 标准 FastArray 应只有一个成员（TArray<Item>）。允许额外属性时由调用方关闭 bValidateDescriptor。
	ensureMsgf(!bValidateDescriptor || FastArrayStructDescriptor->MemberCount == 1U, TEXT("A FastArray using the default FastArrayReplicationFragment is expected to have a single replicated array property derived from FastArraySerializerItem"));

#if WITH_PUSH_MODEL
	if (EnumHasAnyFlags(InDescriptor->Traits, EReplicationStateTraits::HasPushBasedDirtiness))
	{
		// PushModel 注册：把 Owner + RepIndex 存进 FastArraySerializer，
		// 让 MarkItemDirty / MarkArrayDirty 在游戏代码中调用时能直接通知 FGlobalDirtyNetObjectTracker。
		FFastArraySerializer* FastArraySerializer = reinterpret_cast<FFastArraySerializer*>(reinterpret_cast<uint8*>(InOwner) + InDescriptor->MemberProperties[0]->GetOffset_ForGC());
		FastArraySerializer->CachePushModelState(InOwner, InDescriptor->MemberProperties[0]->RepIndex);
		Traits |= EReplicationFragmentTraits::HasPushBasedDirtiness;
		if (EnumHasAnyFlags(InDescriptor->Traits, EReplicationStateTraits::HasFullPushBasedDirtiness))
		{
			Traits |= EReplicationFragmentTraits::HasFullPushBasedDirtiness;
		}
	}
#endif
}

FFastArrayReplicationFragmentBase::~FFastArrayReplicationFragmentBase() = default;

const FReplicationStateDescriptor* FFastArrayReplicationFragmentBase::GetArrayElementDescriptor() const
{
	const FReplicationStateDescriptor* FastArrayDescriptor = ReplicationStateDescriptor.GetReference();
	const FReplicationStateDescriptor* FastArrayStructDescriptor = static_cast<const FStructNetSerializerConfig*>(FastArrayDescriptor->MemberSerializerDescriptors[0].SerializerConfig)->StateDescriptor;

	const uint32 ItemArrayMemberIndex = FFastArrayReplicationFragmentHelper::GetFastArrayStructItemArrayMemberIndex(FastArrayStructDescriptor);
	const FReplicationStateDescriptor* ElementStateDescriptor = static_cast<const FArrayPropertyNetSerializerConfig*>(FastArrayStructDescriptor->MemberSerializerDescriptors[ItemArrayMemberIndex].SerializerConfig)->StateDescriptor;

	const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];
	
	return static_cast<const FStructNetSerializerConfig*>(ElementSerializerDescriptor.SerializerConfig)->StateDescriptor;
}

void FFastArrayReplicationFragmentBase::InternalCopyArrayElement(const FReplicationStateDescriptor* ArrayElementDescriptor, void* RESTRICT Dst, const void* RESTRICT Src)
{
	InternalCopyStructProperty(ArrayElementDescriptor, Dst, Src);
}

bool FFastArrayReplicationFragmentBase::InternalCompareArrayElement(const FReplicationStateDescriptor* ArrayElementDescriptor, void* RESTRICT Dst, const void* RESTRICT Src)
{
	return InternalCompareStructProperty(ArrayElementDescriptor, Dst, Src);
}

void FFastArrayReplicationFragmentBase::Register(FFragmentRegistrationContext& Context, EReplicationFragmentTraits InTraits)
{
	this->Traits |= InTraits;
	Context.RegisterReplicationFragment(this, ReplicationStateDescriptor.GetReference(), ReplicationState ? ReplicationState->GetStateBuffer() : nullptr);
}

void FFastArrayReplicationFragmentBase::InternalDequantizeFastArray(FNetSerializationContext& Context, uint8* RESTRICT DstExternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	const uint32 ArrayMemberIndex = FFastArrayReplicationFragmentHelper::GetFastArrayStructItemArrayMemberIndex(Descriptor);
	check(ArrayMemberIndex < MemberCount);

	const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[ArrayMemberIndex];
	const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[ArrayMemberIndex];
	
	// Dequantize full array
	FNetDequantizeArgs Args;
	Args.Version = 0;
	Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
	Args.Target = reinterpret_cast<NetSerializerValuePointer>(DstExternalBuffer + MemberDescriptor.ExternalMemberOffset);
	Args.Source = reinterpret_cast<NetSerializerValuePointer>(SrcInternalBuffer + MemberDescriptor.InternalMemberOffset);	

	MemberSerializerDescriptor.Serializer->Dequantize(Context, Args);
}

void FFastArrayReplicationFragmentBase::InternalPartialDequantizeFastArray(FReplicationStateApplyContext& ApplyContext, uint8* RESTRICT DstExternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	const uint32 ArrayMemberIndex = FFastArrayReplicationFragmentHelper::GetFastArrayStructItemArrayMemberIndex(Descriptor);
	check(ArrayMemberIndex < MemberCount);

	const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[ArrayMemberIndex];
	const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[ArrayMemberIndex];
	const FReplicationStateMemberChangeMaskDescriptor& MemberChangeMaskDescriptor = ApplyContext.Descriptor->MemberChangeMaskDescriptors[0];

	// Setup changemask
	FNetBitArrayView MemberChangeMask = MakeNetBitArrayView(ApplyContext.StateBufferData.ChangeMaskData, ApplyContext.Descriptor->ChangeMaskBitCount);
	ApplyContext.NetSerializationContext->SetChangeMask(&MemberChangeMask);

	FNetDequantizeArgs Args;
	Args.Version = 0;
	Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
	Args.Target = reinterpret_cast<NetSerializerValuePointer>(DstExternalBuffer + MemberDescriptor.ExternalMemberOffset);
	Args.Source = reinterpret_cast<NetSerializerValuePointer>(SrcInternalBuffer + MemberDescriptor.InternalMemberOffset);

	Args.ChangeMaskInfo.BitCount = MemberChangeMaskDescriptor.BitCount;
	Args.ChangeMaskInfo.BitOffset = MemberChangeMaskDescriptor.BitOffset;

	MemberSerializerDescriptor.Serializer->Dequantize(*ApplyContext.NetSerializationContext, Args);
}

void FFastArrayReplicationFragmentBase::InternalDequantizeExtraProperties(FNetSerializationContext& Context, uint8* RESTRICT DstExternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	const uint32 ArrayMemberIndex = FFastArrayReplicationFragmentHelper::GetFastArrayStructItemArrayMemberIndex(Descriptor);

	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	check(ArrayMemberIndex < MemberCount);

	for (uint32 MemberIndex = 0; MemberIndex < MemberCount; ++MemberIndex)
	{
		if (MemberIndex == ArrayMemberIndex)
		{
			continue;
		}

		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIndex];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIndex];

		FNetDequantizeArgs Args;
		Args.Version = 0;
		Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		Args.Target = reinterpret_cast<NetSerializerValuePointer>(DstExternalBuffer + MemberDescriptor.ExternalMemberOffset);
		Args.Source = reinterpret_cast<NetSerializerValuePointer>(SrcInternalBuffer + MemberDescriptor.InternalMemberOffset);	

		MemberSerializerDescriptor.Serializer->Dequantize(Context, Args);
	}
}

void FFastArrayReplicationFragmentBase::CollectOwner(FReplicationStateOwnerCollector* Owners) const
{
	Owners->AddOwner(Owner);
}

void FFastArrayReplicationFragmentBase::ToString(FStringBuilderBase& StringBuilder, const uint8* StateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	const FProperty** MemberProperties = Descriptor->MemberProperties;
	const uint32 MemberCount = Descriptor->MemberCount;

	StringBuilder.Appendf(TEXT("FFastArrayReplicationState %s\n"), Descriptor->DebugName->Name);

	FString TempString;
	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FProperty* Property = MemberProperties[MemberIt];

		Property->ExportTextItem_Direct(TempString, StateBuffer + Descriptor->MemberDescriptors[MemberIt].ExternalMemberOffset, nullptr, nullptr, PPF_SimpleObjectText);
		StringBuilder.Appendf(TEXT("%u - %s : %s\n"), MemberIt, *Property->GetName(), ToCStr(TempString));
		TempString.Reset();
	}
}

uint32 FFastArrayReplicationFragmentHelper::GetFastArrayStructItemArrayMemberIndex(const FReplicationStateDescriptor* StructDescriptor)
{
	// We know that one of the members we are looking for is the item array and there will only be one of them.
	// FastArray USTRUCT 中至少有一个 TArray 成员（多见且唯一为内嵌 Item 数组）；按 IsArrayPropertyNetSerializer 找到第一个即可。
	uint32 MemberIndex = 0U;
	for (;;)
	{
		if (IsArrayPropertyNetSerializer(StructDescriptor->MemberSerializerDescriptors[MemberIndex].Serializer))
		{
			return MemberIndex;
		}
		++MemberIndex;
	}
}

const FReplicationStateDescriptor* FNativeFastArrayReplicationFragmentBase::GetFastArrayPropertyStructDescriptor() const
{
	const FReplicationStateMemberSerializerDescriptor& FastArrayMemberSerializerDescriptor = ReplicationStateDescriptor->MemberSerializerDescriptors[0];
	const FStructNetSerializerConfig* FastArrayStructSerializerConfig = static_cast<const FStructNetSerializerConfig*>(FastArrayMemberSerializerDescriptor.SerializerConfig);

	return FastArrayStructSerializerConfig->StateDescriptor;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// NativeFastArrayReplicationFragment
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FNativeFastArrayReplicationFragmentBase::FNativeFastArrayReplicationFragmentBase(EReplicationFragmentTraits InTraits, UObject* InOwner, const FReplicationStateDescriptor* InDescriptor)
: FReplicationFragment(InTraits)
, ReplicationStateDescriptor(InDescriptor)
, Owner(InOwner)
{
	// Verify that class descriptor is of expected type
	// 原生路径要求 IsFastArrayReplicationState + IsNativeFastArrayReplicationState 两个 trait 同时存在。
	{
		check(EnumHasAllFlags(InDescriptor->Traits, EReplicationStateTraits::IsFastArrayReplicationState | EReplicationStateTraits::IsNativeFastArrayReplicationState));
	}

	// We do not create temporary states
	// 与兼容路径相同：Apply 走 RawStateBuffer + ChangeMask；不创建临时 ExternalState。
	Traits |= EReplicationFragmentTraits::HasPersistentTargetStateBuffer;
	
	if (EnumHasAnyFlags(InTraits, EReplicationFragmentTraits::CanReceive))
	{
		if (EnumHasAnyFlags(InDescriptor->Traits, EReplicationStateTraits::HasRepNotifies))
		{
			if (EnumHasAnyFlags(InDescriptor->Traits, EReplicationStateTraits::KeepPreviousState))
			{
				// 同样不支持"OnRep 旧值"。
				check(false);
			}

			Traits |= EReplicationFragmentTraits::HasRepNotifies;
		}

		// For now we always expect pre/post operations for legacy states
		Traits |= EReplicationFragmentTraits::NeedsLegacyCallbacks;
	}
	
	// Propagate object reference.
	if (EnumHasAnyFlags(InDescriptor->Traits, EReplicationStateTraits::HasObjectReference))
	{
		Traits |= EReplicationFragmentTraits::HasObjectReference;
	}

	// Look up the descriptor of the struct
	const FReplicationStateDescriptor* FastArrayStructDescriptor = GetFastArrayPropertyStructDescriptor();

	// And we are looking for the offset of ItemArray which is the relative offset from the FastArray struct
	// 与兼容路径同：缓存 ItemArray 在 USTRUCT 内偏移。
	const uint32 ItemArrayMemberIndex = FFastArrayReplicationFragmentHelper::GetFastArrayStructItemArrayMemberIndex(FastArrayStructDescriptor);
	WrappedArrayOffsetRelativeFastArraySerializerProperty = FastArrayStructDescriptor->MemberDescriptors[ItemArrayMemberIndex].ExternalMemberOffset;
}

const FReplicationStateDescriptor* FNativeFastArrayReplicationFragmentBase::GetArrayElementDescriptor() const
{
	const FReplicationStateDescriptor* FastArrayDescriptor = ReplicationStateDescriptor.GetReference();
	const FReplicationStateDescriptor* FastArrayStructDescriptor = static_cast<const FStructNetSerializerConfig*>(FastArrayDescriptor->MemberSerializerDescriptors[0].SerializerConfig)->StateDescriptor;

	const uint32 ItemArrayMemberIndex = FFastArrayReplicationFragmentHelper::GetFastArrayStructItemArrayMemberIndex(FastArrayStructDescriptor);
	const FReplicationStateDescriptor* ElementStateDescriptor = static_cast<const FArrayPropertyNetSerializerConfig*>(FastArrayStructDescriptor->MemberSerializerDescriptors[ItemArrayMemberIndex].SerializerConfig)->StateDescriptor;

	const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];
	
	return static_cast<const FStructNetSerializerConfig*>(ElementSerializerDescriptor.SerializerConfig)->StateDescriptor;
}

void FNativeFastArrayReplicationFragmentBase::InternalDequantizeFastArray(FNetSerializationContext& Context, uint8* RESTRICT DstExternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	const uint32 MemberIndex = FFastArrayReplicationFragmentHelper::GetFastArrayStructItemArrayMemberIndex(Descriptor);

	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = Descriptor->MemberSerializerDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	check(MemberIndex < MemberCount);

	const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIndex];
	const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[MemberIndex];

	FNetDequantizeArgs Args;
	Args.Version = 0;
	Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
	Args.Target = reinterpret_cast<NetSerializerValuePointer>(DstExternalBuffer + MemberDescriptor.ExternalMemberOffset);
	Args.Source = reinterpret_cast<NetSerializerValuePointer>(SrcInternalBuffer + MemberDescriptor.InternalMemberOffset);	

	MemberSerializerDescriptor.Serializer->Dequantize(Context, Args);
}

void FNativeFastArrayReplicationFragmentBase::ToString(FStringBuilderBase& StringBuilder, const uint8* StateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	const FProperty** MemberProperties = Descriptor->MemberProperties;
	const uint32 MemberCount = Descriptor->MemberCount;

	StringBuilder.Appendf(TEXT("FNativeFastArrayReplicationState %s\n"), Descriptor->DebugName->Name);

	FString TempString;
	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FProperty* Property = MemberProperties[MemberIt];

		Property->ExportTextItem_Direct(TempString, StateBuffer + Descriptor->MemberDescriptors[MemberIt].ExternalMemberOffset, nullptr, nullptr, PPF_SimpleObjectText);
		StringBuilder.Appendf(TEXT("%u - %s : %s\n"), MemberIt, *Property->GetName(), ToCStr(TempString));
		TempString.Reset();
	}
}

void FNativeFastArrayReplicationFragmentBase::CollectOwner(FReplicationStateOwnerCollector* Owners) const
{
	Owners->AddOwner(Owner);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FFastArrayReplicationFragmentHelper
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void FFastArrayReplicationFragmentHelper::InternalApplyArrayElement(const FReplicationStateDescriptor* ArrayElementDescriptor, void* RESTRICT Dst, const void* RESTRICT Src)
{
	InternalApplyStructProperty(ArrayElementDescriptor, Dst, Src);
}

void FFastArrayReplicationFragmentHelper::InternalCopyArrayElement(const FReplicationStateDescriptor* ArrayElementDescriptor, void* RESTRICT Dst, const void* RESTRICT Src)
{
	InternalCopyStructProperty(ArrayElementDescriptor, Dst, Src);
}

bool FFastArrayReplicationFragmentHelper::InternalCompareArrayElement(const FReplicationStateDescriptor* ArrayElementDescriptor, void* RESTRICT Dst, const void* RESTRICT Src)
{
	return InternalCompareStructProperty(ArrayElementDescriptor, Dst, Src);
}

}
