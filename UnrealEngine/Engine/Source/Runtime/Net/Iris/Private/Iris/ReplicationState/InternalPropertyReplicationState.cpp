// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/ReplicationState/InternalPropertyReplicationState.h"
#include "Iris/ReplicationState/PropertyReplicationState.h"
#include "Iris/ReplicationState/ReplicationStateUtil.h"
#include "Iris/ReplicationState/InternalReplicationStateDescriptorUtils.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Net/Core/NetBitArray.h"
#include "UObject/UnrealType.h"

// =============================================================================================
// 中文说明：本文件实现了 InternalPropertyReplicationState.h 中声明的所有低层操作。
// 设计理念：
//   - 整个 Iris 的"外部状态缓冲（external state buffer）"在内存上是 [Header][Members][ChangeMask]
//     [ConditionalChangeMask?][PollMask] 的连续布局，由 Descriptor 给出每段偏移；
//   - 这里所有以 StateBuffer 为操作对象的函数都依赖 ReplicationStateUtil.h 提供的访问器
//     （GetReplicationStateHeader / GetMemberChangeMask / GetMemberConditionalChangeMask 等）；
//   - 处理 Struct / Array 时遵循"只看 replicated 成员"的不变量——即便 native struct 中存在
//     non-replicated 字段，也不能整体 memcpy / Identical，必须按 Descriptor 列表逐成员处理。
// =============================================================================================

namespace UE::Net::Private
{

// 中文：StateBuffer 头部初始化——construct 出新的 FReplicationStateHeader、
//       清零 MemberChangeMask、把 ConditionalChangeMask 全部置 1（默认所有条件位启用）。
//       这是 ConstructPropertyReplicationState 的第一步。
void InitReplicationStateInternals(uint8* StateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	// Init internal data
	new (StateBuffer) FReplicationStateHeader();

	// init dirty state tracking
	FNetBitArrayView DirtyStates = GetMemberChangeMask(StateBuffer, Descriptor);
	DirtyStates.ClearAllBits();

	// Init optional conditionals
	if (EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::HasLifetimeConditionals))
	{
		FNetBitArrayView ConditionalChangeMask = GetMemberConditionalChangeMask(StateBuffer, Descriptor);
		ConditionalChangeMask.SetAllBits();
	}
}

// 中文：把 Src 上的内部跟踪位（MemberChangeMask + 可选 ConditionalChangeMask）拷给 Dst。
//   - bOverwriteChangeMask=true：覆盖式拷贝（CopyPropertyReplicationState 使用）；
//   - bOverwriteChangeMask=false：用 OR 合并（CopyDirtyMembers 使用，保留 Dst 已有 dirty 位，
//     用于"接力"地把 Src 上的新增 dirty 位合并到一份累计的 Dst 上）。
void CopyPropertyReplicationStateInternals(uint8* RESTRICT DstStateBuffer, uint8* RESTRICT SrcStateBuffer, const FReplicationStateDescriptor* Descriptor, bool bOverwriteChangeMask = true)
{
	FNetBitArrayView DstChangeMask = GetMemberChangeMask(DstStateBuffer, Descriptor);
	FNetBitArrayView SrcChangeMask = GetMemberChangeMask(SrcStateBuffer, Descriptor);

	if (bOverwriteChangeMask)
	{
		DstChangeMask.Copy(SrcChangeMask);
	}
	else
	{
		DstChangeMask.Combine(SrcChangeMask, FNetBitArrayView::OrOp);
	}

	// Copy optional conditional changemask
	if (EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::HasLifetimeConditionals))
	{
		FNetBitArrayView DstConditionalChangeMask = GetMemberConditionalChangeMask(DstStateBuffer, Descriptor);
		FNetBitArrayView SrcConditionalChangeMask = GetMemberConditionalChangeMask(SrcStateBuffer, Descriptor);

		if (bOverwriteChangeMask)
		{
			DstConditionalChangeMask.Copy(SrcConditionalChangeMask);
		}
		else
		{
			DstConditionalChangeMask.Combine(SrcConditionalChangeMask, FNetBitArrayView::OrOp);
		}
	}
}

// 中文：在已分配（按 Descriptor->ExternalAlignment 对齐）的 StateBuffer 上构造完整的外部状态：
//   1) 初始化 Header / ChangeMask / ConditionalChangeMask；
//   2) 对每个 Member 调用 Property->InitializeValue 以构造其原生表示
//      （C 数组只在 ArrayIndex==0 时调用一次——InitializeValue 会一次性初始化整个数组）。
//   注意：StateBuffer 进入函数前已被 Memzero（见 FPropertyReplicationState::ConstructStateInternal），
//        某些 InitializeValue 实现会依赖该前置条件。
void ConstructPropertyReplicationState(uint8* StateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	check(IsAligned(StateBuffer, Descriptor->ExternalAlignment));
	
	InitReplicationStateInternals(StateBuffer, Descriptor);

	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FProperty** MemberProperties = Descriptor->MemberProperties;
	const FReplicationStateMemberPropertyDescriptor* MemberPropertyDescriptors = Descriptor->MemberPropertyDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberPropertyDescriptor& MemberPropertyDescriptor = MemberPropertyDescriptors[MemberIt];
		
		// InitializeValue operates on the entire static array so make sure not to call it other than for the first element.
		// 中文：UProperty::InitializeValue 是对整个 C 数组调用的，所以仅在 ArrayIndex==0 时执行，避免重复初始化。
		if (MemberPropertyDescriptor.ArrayIndex == 0)
		{
			const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
			const FProperty* Property = MemberProperties[MemberIt];
			Property->InitializeValue(StateBuffer + MemberDescriptor.ExternalMemberOffset);
		}
	}
}

// 中文：与 Construct 配对的析构。若 Descriptor 标记 IsSourceTriviallyDestructible 则直接跳过
//       （所有成员均为 POD，无需 DestroyValue）；否则逐成员调用 Property->DestroyValue
//       （同样仅在 C 数组首元素调用一次）。
void DestructPropertyReplicationState(uint8* StateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	check(IsAligned(StateBuffer, Descriptor->ExternalAlignment));
	if (EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::IsSourceTriviallyDestructible))
	{
		return;
	}

	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FProperty** MemberProperties = Descriptor->MemberProperties;
	const FReplicationStateMemberPropertyDescriptor* MemberPropertyDescriptors = Descriptor->MemberPropertyDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberPropertyDescriptor& MemberPropertyDescriptor = MemberPropertyDescriptors[MemberIt];
		
		// DestroyValue operates on the entire static array so make sure not to call it other than for the first element.
		// 中文：DestroyValue 同样对整个 C 数组操作，避免重复释放。
		if (MemberPropertyDescriptor.ArrayIndex == 0)
		{
			const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
			const FProperty* Property = MemberProperties[MemberIt];
			Property->DestroyValue(StateBuffer + MemberDescriptor.ExternalMemberOffset);
		}
	}
}

// 中文：整体覆盖拷贝——ChangeMask & 全部成员都从 Src 覆盖到 Dst。
//       对每个成员调用 Property->CopyCompleteValue（处理 C 数组 + 含动态内存的 FString/TArray 等）。
void CopyPropertyReplicationState(uint8* RESTRICT DstStateBuffer, uint8* RESTRICT SrcStateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	check(IsAligned(DstStateBuffer, Descriptor->ExternalAlignment) && IsAligned(SrcStateBuffer, Descriptor->ExternalAlignment));

	// Copy changemasks
	CopyPropertyReplicationStateInternals(DstStateBuffer, SrcStateBuffer, Descriptor);

	// copy statedata
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FProperty** MemberProperties = Descriptor->MemberProperties;
	const FReplicationStateMemberPropertyDescriptor* MemberPropertyDescriptors = Descriptor->MemberPropertyDescriptors;
	const uint32 MemberCount = Descriptor->MemberCount;

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberPropertyDescriptor& MemberPropertyDescriptor = MemberPropertyDescriptors[MemberIt];
		if (MemberPropertyDescriptor.ArrayIndex == 0)
		{
			const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
			const FProperty* Property = MemberProperties[MemberIt];
			Property->CopyCompleteValue(DstStateBuffer + MemberDescriptor.ExternalMemberOffset, SrcStateBuffer + MemberDescriptor.ExternalMemberOffset);
		}
	}
}

// 中文：仅拷贝 Src 中标 dirty 的成员，常用于把"per-frame poll 结果"合并到"per-frame send buffer"。
//   - ChangeMask 走 OR 合并（保留 Dst 已有的 dirty 位）；
//   - InitState 视为"全部都需要拷贝"（无 ChangeMask 概念）。
//   注意：这里仍用 Property->CopyCompleteValue（不区分 struct 内是否含非复制字段）——这是因为本函数
//        服务于 external→external 的拷贝，且 Iris 的 external state 中本身就只包含 replicated 字段
//        (对于含非复制字段的 struct，Descriptor 会用专用 Serializer/分派来构造)。
void CopyDirtyMembers(uint8* RESTRICT DstStateBuffer, uint8* RESTRICT SrcStateBuffer, const FReplicationStateDescriptor* Descriptor)
{
	check(IsAligned(DstStateBuffer, Descriptor->ExternalAlignment) && IsAligned(SrcStateBuffer, Descriptor->ExternalAlignment));

	// Merge changemasks
	const bool bOverwriteChangeMask = false;
	CopyPropertyReplicationStateInternals(DstStateBuffer, SrcStateBuffer, Descriptor, bOverwriteChangeMask);

	// copy dirty members
	const FReplicationStateMemberDescriptor* MemberDescriptors = Descriptor->MemberDescriptors;
	const FProperty** MemberProperties = Descriptor->MemberProperties;
	const FReplicationStateMemberPropertyDescriptor* MemberPropertyDescriptors = Descriptor->MemberPropertyDescriptors;
	FNetBitArrayView DirtyStates = GetMemberChangeMask(SrcStateBuffer, Descriptor);
	
	const uint32 MemberCount = Descriptor->MemberCount;

	const bool bIsInitState = Descriptor->IsInitState();

	for (uint32 MemberIt = 0; MemberIt < MemberCount; ++MemberIt)
	{
		const FReplicationStateMemberPropertyDescriptor& MemberPropertyDescriptor = MemberPropertyDescriptors[MemberIt];
		const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskInfo = Descriptor->MemberChangeMaskDescriptors[MemberIt];

		// 中文：InitState 全拷；其余仅在该成员的任意 ChangeMask 位被置时拷贝。
		const bool bShouldCopyProperty = bIsInitState || DirtyStates.IsAnyBitSet(ChangeMaskInfo.BitOffset, ChangeMaskInfo.BitCount);
		if (bShouldCopyProperty && MemberPropertyDescriptor.ArrayIndex == 0)
		{
			const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
			const FProperty* Property = MemberProperties[MemberIt];
			Property->CopyCompleteValue(DstStateBuffer + MemberDescriptor.ExternalMemberOffset, SrcStateBuffer + MemberDescriptor.ExternalMemberOffset);
		}
	}
}

/**
 * Structs and Arrays need to be handled carefully. Our intermediate representation need 
 * to match the actual native representation perfectly. We are careful not
 * to copy anything that isn't replicated and not consider a struct dirty
 * in case a non-replicated member was modified. Rep notifies that require
 * the previous value as parameter will only get the replicated values
 * updated- non-replicated members will be the same as the default state.
 *
 * 中文：单成员比较的核心实现。三条路径：
 *   1) Serializer 显式声明 UseSerializerIsEqual：使用 Serializer->IsEqual（最精确，例如
 *      量化浮点的容差比较）；
 *   2) Struct/Array 含非复制成员：递归走 InternalCompareStructProperty / Serializer.IsEqual，
 *      仅比较 replicated 字段（避免因 non-replicated 字段变化而误标 dirty）；
 *   3) 其他默认走 FProperty::Identical（基于 ReflectionData 的字节级比较）。
 */
bool InternalCompareMember(const FReplicationStateDescriptor* Descriptor, uint32 MemberIndex, const void* RESTRICT ValueA, const void* RESTRICT ValueB)
{
	const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = Descriptor->MemberSerializerDescriptors[MemberIndex];

	if (EnumHasAnyFlags(MemberSerializerDescriptor.Serializer->Traits, ENetSerializerTraits::UseSerializerIsEqual))
	{
		FNetSerializationContext Context;
		FNetIsEqualArgs Args;
		Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		Args.Source0 = NetSerializerValuePointer(ValueA);
		Args.Source1 = NetSerializerValuePointer(ValueB);
		Args.bStateIsQuantized = false;

		return MemberSerializerDescriptor.Serializer->IsEqual(Context, Args);
	}
	else if (IsUsingStructNetSerializer(MemberSerializerDescriptor))
	{
		// 中文：Struct 路径——若 struct 含非复制字段（!AllMembersAreReplicated）走 memberwise 比较，
		//       否则继续走默认 Property->Identical。
		const FStructNetSerializerConfig* StructConfig = static_cast<const FStructNetSerializerConfig*>(MemberSerializerDescriptor.SerializerConfig);
		const FReplicationStateDescriptor* StructDescriptor = StructConfig->StateDescriptor;
		if (!EnumHasAnyFlags(StructDescriptor->Traits, EReplicationStateTraits::AllMembersAreReplicated))
		{
			return InternalCompareStructProperty(StructDescriptor, ValueA, ValueB);
		}
	}
	else if (IsUsingArrayPropertyNetSerializer(MemberSerializerDescriptor))
	{
		// 中文：Array 路径——若元素类型为含非复制字段的 struct，则使用 ArrayPropertyNetSerializer 自身
		//       的 IsEqual（它会逐元素正确处理）；否则走默认 Property->Identical。
		const FArrayPropertyNetSerializerConfig* ArrayConfig = static_cast<const FArrayPropertyNetSerializerConfig*>(MemberSerializerDescriptor.SerializerConfig);
		const FReplicationStateDescriptor* StructDescriptor = ArrayConfig->StateDescriptor;
		if (!EnumHasAnyFlags(StructDescriptor->Traits, EReplicationStateTraits::AllMembersAreReplicated))
		{
			FNetSerializationContext Context;
			FNetIsEqualArgs Args;
			Args.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
			Args.Source0 = NetSerializerValuePointer(ValueA);
			Args.Source1 = NetSerializerValuePointer(ValueB);
			Args.bStateIsQuantized = false;

			return MemberSerializerDescriptor.Serializer->IsEqual(Context, Args);
		}
	}

	// Default handling
	const FProperty* Property = Descriptor->MemberProperties[MemberIndex];
	return Property->Identical(ValueA, ValueB);
}

// 中文：递归比较 Struct 的所有 replicated 成员——
//   - "派生自带 NetSerializer 的 struct"（IsDerivedFromStructWithCustomSerializer）的第 0 个成员
//     是 base struct 整体，这里用 Serializer->IsEqual 比较 base 部分；
//   - 之后逐成员递归 InternalCompareMember；任一不等立刻返回 false。
//   注意：成员偏移用的是 Property->GetOffset_ForGC()（即外部原生表示中的偏移）。
bool InternalCompareStructProperty(const FReplicationStateDescriptor* StructDescriptor, const void* RESTRICT ValueA, const void* RESTRICT ValueB)
{
	uint32 FirstStructMemberForCompare = 0U;

	const FReplicationStateMemberDescriptor* MemberDescriptors = StructDescriptor->MemberDescriptors;
	const FReplicationStateMemberSerializerDescriptor* MemberSerializerDescriptors = StructDescriptor->MemberSerializerDescriptors;
	const FProperty** MemberProperties = StructDescriptor->MemberProperties;
	const FReplicationStateMemberPropertyDescriptor* MemberPropertyDescriptors = StructDescriptor->MemberPropertyDescriptors;

	// For derived structs the first property is a NetSerializer for some super struct. Its property will be null so we can't use standard iteration over properties. Let's use serializer equal for it. 
	// 中文：派生 struct 的 Member[0] 是"基类 struct"且 Property 为 nullptr，无法用 FProperty::Identical，
	//       这里使用其 Serializer.IsEqual 处理，比较成功后从 Member[1] 开始走常规逻辑。
	if (EnumHasAnyFlags(StructDescriptor->Traits, EReplicationStateTraits::IsDerivedFromStructWithCustomSerializer))
	{
		FirstStructMemberForCompare = 1U;

		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[0];
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = MemberSerializerDescriptors[0];

		FNetSerializationContext Context;
		FNetIsEqualArgs IsEqualArgs;
		IsEqualArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		IsEqualArgs.Source0 = NetSerializerValuePointer(ValueA) + MemberDescriptor.ExternalMemberOffset;
		IsEqualArgs.Source1 = NetSerializerValuePointer(ValueB) + MemberDescriptor.ExternalMemberOffset;
		IsEqualArgs.bStateIsQuantized = false;
		if (!MemberSerializerDescriptor.Serializer->IsEqual(Context, IsEqualArgs))
		{
			return false;
		}
	}

	for (uint32 StructMemberIt = FirstStructMemberForCompare, StructMemberEndIt = StructDescriptor->MemberCount; StructMemberIt < StructMemberEndIt; ++StructMemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[StructMemberIt];
		const FReplicationStateMemberPropertyDescriptor& MemberPropertyDescriptor = MemberPropertyDescriptors[StructMemberIt];
		const FProperty* MemberProperty = MemberProperties[StructMemberIt];
		// 中文：注意此处使用 Native 偏移（Property->GetOffset_ForGC + ArrayIndex 间距），因为
		//       ValueA/ValueB 是外部原生表示中的 struct 起点。
		const SIZE_T MemberOffset = MemberProperty->GetOffset_ForGC() + MemberProperty->GetElementSize()*MemberPropertyDescriptor.ArrayIndex;
		if (!InternalCompareMember(StructDescriptor, StructMemberIt, static_cast<const uint8*>(ValueA) + MemberOffset, static_cast<const uint8*>(ValueB) + MemberOffset))
		{
			return false;
		}
	}

	return true;
}

// 中文：把整个 Struct（含派生）从 Src Apply 到 Dst。处理顺序：
//   1) 自带 custom serializer 或派生：先 Apply Member[0]（base struct 部分）——
//      若 Serializer 有 Apply trait 用 Serializer->Apply；否则 BaseStruct->CopyScriptStruct 整体拷贝；
//   2) 然后从 Member[1] 起递归 InternalApplyPropertyValue（仅 replicated 字段）；
//   普通（非派生）struct 则从 Member[0] 起常规处理。
void InternalApplyStructProperty(const FReplicationStateDescriptor* StructDescriptor, void* RESTRICT Dst, const void* RESTRICT Src)
{
	uint32 FirstStructMemberForApply = 0U;
	if (EnumHasAnyFlags(StructDescriptor->Traits, EReplicationStateTraits::IsStructWithCustomSerializer | EReplicationStateTraits::IsDerivedFromStructWithCustomSerializer))
	{
		// The first member in a derived struct descriptor is the base struct. We can skip after we've applied the value.
		FirstStructMemberForApply = 1U;

		// If the custom serializer has an Apply method we need to use it.
		const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = StructDescriptor->MemberSerializerDescriptors[0];
		if (EnumHasAnyFlags(MemberSerializerDescriptor.Serializer->Traits, ENetSerializerTraits::HasApply))
		{
			FNetSerializationContext Context;
			FNetApplyArgs ApplyArgs;
			ApplyArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
			ApplyArgs.Source = NetSerializerValuePointer(Src);
			ApplyArgs.Target = NetSerializerValuePointer(Dst);
			MemberSerializerDescriptor.Serializer->Apply(Context, ApplyArgs);
		}
		else
		{
			// For derived structs the base struct is the closest parent with a custom serializer. For root structs it's the actual struct.
			// 中文：BaseStruct 是"最接近的带 custom serializer 的祖先 struct"——按它的 ScriptStruct 整体拷贝。
			const UScriptStruct* BaseStruct = StructDescriptor->BaseStruct;
			BaseStruct->CopyScriptStruct(Dst, Src, 1);
		}
	}

	const FReplicationStateMemberDescriptor* MemberDescriptors = StructDescriptor->MemberDescriptors;
	const FProperty** MemberProperties = StructDescriptor->MemberProperties;
	const FReplicationStateMemberPropertyDescriptor* MemberPropertyDescriptors = StructDescriptor->MemberPropertyDescriptors;
	for (uint32 StructMemberIt = FirstStructMemberForApply, StructMemberEndIt = StructDescriptor->MemberCount; StructMemberIt < StructMemberEndIt; ++StructMemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[StructMemberIt];
		const FReplicationStateMemberPropertyDescriptor& MemberPropertyDescriptor = MemberPropertyDescriptors[StructMemberIt];
		const FProperty* MemberProperty = MemberProperties[StructMemberIt];
		const SIZE_T MemberOffset = MemberProperty->GetOffset_ForGC() + MemberProperty->GetElementSize()*MemberPropertyDescriptor.ArrayIndex;
		InternalApplyPropertyValue(StructDescriptor, StructMemberIt, static_cast<uint8*>(Dst) + MemberOffset, static_cast<const uint8*>(Src) + MemberOffset);
	}
}

// 中文：单成员 Apply 的核心实现，调度顺序：
//   1) Serializer.HasApply：使用其 Apply（典型场景：FObjectPropertyBase 的 NetSerializer 在 Apply
//      中负责调用 ObjectReferenceCache 解引用 + 必要时设置 nullptr 等 side-effects）；
//   2) Struct 含非复制字段：走 InternalApplyStructProperty（不能 memcpy 整个 struct，否则会覆盖
//      non-replicated 字段）；
//   3) Array 元素是 (Serializer.HasApply 或 含非复制字段的 struct)：先 Resize 目标数组到与源相同
//      长度，再逐元素 Apply 或 InternalApplyStructProperty；
//   4) 其余默认走 Property->CopySingleValue。
void InternalApplyPropertyValue(const FReplicationStateDescriptor* Descriptor, uint32 MemberIndex, void* RESTRICT Dst, const void* RESTRICT Src)
{
	const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = Descriptor->MemberSerializerDescriptors[MemberIndex];
	
	// If member has serializer with Apply then use it.
	if (EnumHasAnyFlags(MemberSerializerDescriptor.Serializer->Traits, ENetSerializerTraits::HasApply))
	{
		FNetSerializationContext Context;
		FNetApplyArgs ApplyArgs;
		ApplyArgs.NetSerializerConfig = MemberSerializerDescriptor.SerializerConfig;
		ApplyArgs.Source = NetSerializerValuePointer(Src);
		ApplyArgs.Target = NetSerializerValuePointer(Dst);
		MemberSerializerDescriptor.Serializer->Apply(Context, ApplyArgs);
		return;
	}

	if (IsUsingStructNetSerializer(MemberSerializerDescriptor))
	{
		const FStructNetSerializerConfig* StructConfig = static_cast<const FStructNetSerializerConfig*>(MemberSerializerDescriptor.SerializerConfig);
		const FReplicationStateDescriptor* StructDescriptor = StructConfig->StateDescriptor;
		if (!EnumHasAnyFlags(StructDescriptor->Traits, EReplicationStateTraits::AllMembersAreReplicated))
		{
			InternalApplyStructProperty(StructDescriptor, Dst, Src);
			return;
		}
	}
	else if (IsUsingArrayPropertyNetSerializer(MemberSerializerDescriptor))
	{
		const FArrayPropertyNetSerializerConfig* ArrayConfig = static_cast<const FArrayPropertyNetSerializerConfig*>(MemberSerializerDescriptor.SerializerConfig);
		const FReplicationStateDescriptor* ElementStateDescriptor = ArrayConfig->StateDescriptor;
		const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];

		const bool bSerializerHasApply = EnumHasAnyFlags(ElementSerializerDescriptor.Serializer->Traits, ENetSerializerTraits::HasApply);
		const bool bIsStructWithNotReplicatedProps = IsUsingStructNetSerializer(ElementSerializerDescriptor) && !EnumHasAnyFlags(ElementStateDescriptor->Traits, EReplicationStateTraits::AllMembersAreReplicated);
		if (bSerializerHasApply || bIsStructWithNotReplicatedProps)
		{
			const FReplicationStateDescriptor* StructDescriptor = static_cast<const FStructNetSerializerConfig*>(ElementSerializerDescriptor.SerializerConfig)->StateDescriptor;
	
			// Need to explicitly iterate over array members to be able to only copy data that we should copy
			// 中文：通过 ScriptArrayHelper 操作两端 TArray，先把 Dst 调整到与 Src 相同长度，再逐元素处理。
			FScriptArrayHelper ScriptArrayHelperSrc(ArrayConfig->Property.Get(), reinterpret_cast<const void*>(Src));
			FScriptArrayHelper ScriptArrayHelperDst(ArrayConfig->Property.Get(), reinterpret_cast<void*>(Dst));

			// First we must resize target to match size of source data
			const uint32 ElementCount = ScriptArrayHelperSrc.Num();
			ScriptArrayHelperDst.Resize(ElementCount);
		
			// Iterate over array entries and copy the values
			if (bSerializerHasApply)
			{
				for (uint32 ElementIt = 0, ElementEndIt = ElementCount; ElementIt < ElementEndIt; ++ElementIt)
				{
					const uint8* ArraySrc = ScriptArrayHelperSrc.GetRawPtr(ElementIt);
					uint8* ArrayDst = ScriptArrayHelperDst.GetRawPtr(ElementIt);

					FNetSerializationContext Context;
					FNetApplyArgs ApplyArgs;
					ApplyArgs.NetSerializerConfig = ElementSerializerDescriptor.SerializerConfig;
					ApplyArgs.Source = NetSerializerValuePointer(ArraySrc);
					ApplyArgs.Target = NetSerializerValuePointer(ArrayDst);
					ElementSerializerDescriptor.Serializer->Apply(Context, ApplyArgs);
				}
			}
			else
			{
				for (uint32 ElementIt = 0, ElementEndIt = ElementCount; ElementIt < ElementEndIt; ++ElementIt)
				{
					const uint8* ArraySrc = ScriptArrayHelperSrc.GetRawPtr(ElementIt);
					uint8* ArrayDst = ScriptArrayHelperDst.GetRawPtr(ElementIt);

					InternalApplyStructProperty(StructDescriptor, ArrayDst, ArraySrc);
				}
			}

			return;
		}
	}

	// Default handling for all properties except for structs and arrays that have some non-replicated members or a serializer with custom Apply.
	// 中文：默认情形——直接用 UProperty::CopySingleValue（对单元素，C 数组中的一项），等价 memcpy + 必要的内嵌动态内存深拷贝。
	const FProperty* Property = Descriptor->MemberProperties[MemberIndex];
	Property->CopySingleValue(Dst, Src);
}

// 中文：递归从 Src 的 struct 拷贝到 Dst（不调用任何 Serializer.Apply，仅 memberwise）。
//       派生 struct 处理 base 部分时直接用 BaseStruct->CopyScriptStruct（整体拷贝）。
void InternalCopyStructProperty(const FReplicationStateDescriptor* StructDescriptor, void* RESTRICT Dst, const void* RESTRICT Src)
{
	uint32 FirstStructMemberForCopy = 0U;
	if (EnumHasAnyFlags(StructDescriptor->Traits, EReplicationStateTraits::IsDerivedFromStructWithCustomSerializer))
	{
		// The base struct is the closest parent with a custom serializer, for which we always copy the entire state.
		const UScriptStruct* BaseStruct = StructDescriptor->BaseStruct;
		BaseStruct->CopyScriptStruct(Dst, Src, 1);

		// The first member in a derived struct descriptor is the base struct. We can skip it now that we've copied the value.
		FirstStructMemberForCopy = 1U;
	}

	const FReplicationStateMemberDescriptor* MemberDescriptors = StructDescriptor->MemberDescriptors;
	const FProperty** MemberProperties = StructDescriptor->MemberProperties;
	const FReplicationStateMemberPropertyDescriptor* MemberPropertyDescriptors = StructDescriptor->MemberPropertyDescriptors;

	for (uint32 StructMemberIt = FirstStructMemberForCopy, StructMemberEndIt = StructDescriptor->MemberCount; StructMemberIt < StructMemberEndIt; ++StructMemberIt)
	{
		const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[StructMemberIt];
		const FReplicationStateMemberPropertyDescriptor& MemberPropertyDescriptor = MemberPropertyDescriptors[StructMemberIt];
		const FProperty* MemberProperty = MemberProperties[StructMemberIt];
		const SIZE_T MemberOffset = MemberProperty->GetOffset_ForGC() + MemberProperty->GetElementSize()*MemberPropertyDescriptor.ArrayIndex;
		InternalCopyPropertyValue(StructDescriptor, StructMemberIt, static_cast<uint8*>(Dst) + MemberOffset, static_cast<const uint8*>(Src) + MemberOffset);
	}
}

// 中文：单成员 Copy（不走 Serializer.Apply，纯数据拷贝）。仅 Struct 含非复制字段或 Array 元素是
//       这样的 struct 才走递归 memberwise 拷贝；其余调用 Property->CopySingleValue。
void InternalCopyPropertyValue(const FReplicationStateDescriptor* Descriptor, uint32 MemberIndex, void* RESTRICT Dst, const void* RESTRICT Src)
{
	const FReplicationStateMemberSerializerDescriptor& MemberSerializerDescriptor = Descriptor->MemberSerializerDescriptors[MemberIndex];
	if (IsUsingStructNetSerializer(MemberSerializerDescriptor))
	{
		const FStructNetSerializerConfig* StructConfig = static_cast<const FStructNetSerializerConfig*>(MemberSerializerDescriptor.SerializerConfig);
		const FReplicationStateDescriptor* StructDescriptor = StructConfig->StateDescriptor;
		if (!EnumHasAnyFlags(StructDescriptor->Traits, EReplicationStateTraits::AllMembersAreReplicated))
		{
			InternalCopyStructProperty(StructDescriptor, Dst, Src);
			return;
		}
	}
	else if (IsUsingArrayPropertyNetSerializer(MemberSerializerDescriptor))
	{
		const FArrayPropertyNetSerializerConfig* ArrayConfig = static_cast<const FArrayPropertyNetSerializerConfig*>(MemberSerializerDescriptor.SerializerConfig);
		const FReplicationStateDescriptor* ElementStateDescriptor = ArrayConfig->StateDescriptor;
		const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];

		if (IsUsingStructNetSerializer(ElementSerializerDescriptor) && !EnumHasAnyFlags(ElementStateDescriptor->Traits, EReplicationStateTraits::AllMembersAreReplicated))
		{
			const FReplicationStateDescriptor* StructDescriptor = static_cast<const FStructNetSerializerConfig*>(ElementSerializerDescriptor.SerializerConfig)->StateDescriptor;
	
			// Need to explicitly iterate over array members to be able to only copy data that we should copy
			FScriptArrayHelper ScriptArrayHelperSrc(ArrayConfig->Property.Get(), reinterpret_cast<const void*>(Src));
			FScriptArrayHelper ScriptArrayHelperDst(ArrayConfig->Property.Get(), reinterpret_cast<void*>(Dst));

			// First we must resize target to match size of source data
			const uint32 ElementCount = ScriptArrayHelperSrc.Num();
			ScriptArrayHelperDst.Resize(ElementCount);
		
			// Iterate over array entries and copy the statedata using internal data
			for (uint32 ElementIt = 0, ElementEndIt = ElementCount; ElementIt < ElementEndIt; ++ElementIt)
			{
				const uint8*  ArraySrc = ScriptArrayHelperSrc.GetRawPtr(ElementIt);
				uint8* ArrayDst = ScriptArrayHelperDst.GetRawPtr(ElementIt);

				InternalCopyStructProperty(StructDescriptor, ArrayDst, ArraySrc);
			}

			return;
		}
	}

	// Default handling for all properties except for structs and arrays that have some non-replicated members
	const FProperty* Property = Descriptor->MemberProperties[MemberIndex];
	Property->CopySingleValue(Dst, Src);
}

// 中文：TArray 比较 + 拷贝 + ChangeMask 维护一体化操作（用于"带 element changemask"的顶层数组）。
//   流程：
//     1) Resize Dst 到 SrcElementCount；
//     2) 若 Src 比 Dst 短：清掉超出范围对应的 element bit；
//        若 Src 比 Dst 长：增长部分被视为新元素（直接置 dirty + 拷贝，不参与比较）；
//     3) 逐元素 InternalCompareStructProperty——若不等：置 element bit + InternalCopyStructProperty
//        覆盖到 Dst；
//     4) 注意 element bit 的位置 = ChangeMaskInfo.BitOffset + 1 + (ElementIt % 63)，
//        这意味着同一 changemask 槽下不同元素可能 alias，dirty 跟踪只能"近似"——但保证
//        "数组真的有变化时至少有 1 位被置"，并由总位（bit 0）兜底；
//   返回 false 表示数组发生了任何修改（含 size 变化）。
bool InternalCompareAndCopyArrayWithElementChangeMask(const FReplicationStateDescriptor* Descriptor, uint32 MemberIndex, const void* RESTRICT DstArray, const void* RESTRICT SrcArray, UE::Net::FNetBitArrayView& ChangeMask)
{
	bool bArrayIsEqual = true;

	const FReplicationStateMemberChangeMaskDescriptor& ChangeMaskInfo = Descriptor->MemberChangeMaskDescriptors[MemberIndex];

	const FReplicationStateMemberSerializerDescriptor& ArrayDescriptor = Descriptor->MemberSerializerDescriptors[MemberIndex];
	const FArrayPropertyNetSerializerConfig* ArrayConfig = static_cast<const FArrayPropertyNetSerializerConfig*>(ArrayDescriptor.SerializerConfig);

	FScriptArrayHelper SrcScriptArray(ArrayConfig->Property.Get(), SrcArray);
	FScriptArrayHelper DstScriptArray(ArrayConfig->Property.Get(), DstArray);

	// Detect size change and adjust the destination array size as needed and clear bits that don't relate to any elements.
	// 中文：长度差异处理——
	//   - 不等则强制 bArrayIsEqual = false 并 Resize；
	//   - 缩短时：清掉超出 SrcElementCount 的 element changemask 位（防止假 dirty 残留）。
	const uint32 SrcElementCount = SrcScriptArray.Num();
	const uint32 DstElementCount = DstScriptArray.Num();
	if (SrcElementCount != DstElementCount)
	{
		bArrayIsEqual = false;

		DstScriptArray.Resize(SrcElementCount);

		// If array has shrunk then mask off bits in the changemask pertaining to elements that no longer exist. For a growing array we skip compare and set it to dirty in the element loop.
		if (SrcElementCount < DstElementCount)
		{
			if (SrcElementCount + 1U < ChangeMaskInfo.BitCount)
			{
				const uint32 BitOffsetToClear = ChangeMaskInfo.BitOffset + FPropertyReplicationState::TArrayElementChangeMaskBitOffset + SrcElementCount;
				const uint32 BitCountToClear = ChangeMaskInfo.BitCount - 1U - SrcElementCount;
				ChangeMask.ClearBits(BitOffsetToClear, BitCountToClear);
			}
		}
	}

	const FReplicationStateDescriptor* ElementDescriptor = ArrayConfig->StateDescriptor;
	const uint32 ElementChangeMaskBitOffset = ChangeMaskInfo.BitOffset + FPropertyReplicationState::TArrayElementChangeMaskBitOffset;
	for (uint32 ElementIt = 0, ElementEndIt = SrcElementCount; ElementIt < ElementEndIt; ++ElementIt)
	{
		const uint8* SrcElement = SrcScriptArray.GetRawPtr(ElementIt);
		uint8* DstElement = DstScriptArray.GetRawPtr(ElementIt);

		// Compare elements up to the previous element count, i.e. at most DstElementCount. New elements will be considered different and always copied.
		// 中文：原本就存在的元素做 InternalCompareStructProperty 比较；新增（ElementIt >= DstElementCount）
		//       直接视为不同；任一不等就置 element bit + 拷贝。
		const bool bIsNewElement = ElementIt >= DstElementCount;
		if (bIsNewElement || !InternalCompareStructProperty(ElementDescriptor, DstElement, SrcElement))
		{
			bArrayIsEqual = false;
			// 中文：% TArrayElementChangeMaskBits（63）让多个元素可能映射到同一位——这是合理折衷：
			//       changemask 用 63 bit 跟踪不定长数组，过长部分共享位，只确保 dirty 至少有信号。
			const uint32 ElementBitOffset = ElementChangeMaskBitOffset + (ElementIt % FPropertyReplicationState::TArrayElementChangeMaskBits);
			ChangeMask.SetBit(ElementBitOffset);
			InternalCopyStructProperty(ElementDescriptor, DstElement, SrcElement);
		}
	}

	return bArrayIsEqual;
}

}
