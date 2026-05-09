// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// FastArrayReplicationFragmentInternal.h —— FastArray ReplicationFragment 的"内部辅助层"
// ---------------------------------------------------------------------------------------------------------------------
// 文件位置：Public/Iris/ReplicationSystem/Private/  ← 注意：虽然在 Public 目录下，但 Private 子目录是约定俗成的
//                                                     "对其他模块隐藏"标记，仅 Iris 自身或派生类用。
//
// 本文件提供：
//   - FastArrayPollingPolicies（命名空间）：
//       * FPollingState：保存上次 ArrayReplicationKey + 每 Item 的 ReplicationKey/ID 镜像，用于差异化 poll；
//       * FNeedPollingPolicy：要 poll（典型 FFastArraySerializer 派生用户）；
//       * FNoPollingPolicy：不 poll（Native Iris FastArray 走推模式 TIrisFastArrayEditor）。
//
//   - Private::FFastArrayReplicationFragmentHelper（静态工具类）：
//       * ConditionalRebuildItemMap —— 在 ID 不一致或强制时重建 ItemMap（ReplicationID → 索引）；
//       * ApplyReplicatedState —— 客户端收到 FastArray 状态后，按 changemask 与 ItemMap 做 Add/Remove/Modify 三类回调
//                                  （PreReplicatedRemove/PostReplicatedAdd/PostReplicatedChange）；
//       * InternalApplyArrayElement / InternalCopyArrayElement / InternalCompareArrayElement —— 仅按 Descriptor
//         触达"被复制"的字段（避免覆盖 ReplicationID 等元数据/本地字段）；
//       * GetFastArrayStructItemArrayMemberIndex —— 在 Struct 描述符中找到内嵌 TArray 的成员索引，支持
//         FastArrayNetSerializer 携带额外属性的场景；
//       * CallPostReplicatedReceiveOrNot —— SFINAE 探测目标 FastArray 是否定义 PostReplicatedReceive，没有就空实现，
//         避免在用户未实现时强制构造参数。
//
//   - FFastArrayReplicationFragmentBase / FNativeFastArrayReplicationFragmentBase：
//       两条 FastArray 路径的 Fragment 基类。
//       * 普通 FastArrayReplicationFragment（继承自 FReplicationFragment）：
//           - 持有 ReplicationStateDescriptor + 用户态 FPropertyReplicationState；
//           - 仍走 Poll/Copy 路径（与传统 FastArraySerializer 兼容）；
//           - 提供 InternalDequantize / InternalPartialDequantize / InternalDequantizeExtraProperties 路径供 Apply 使用。
//       * NativeFastArrayReplicationFragmentBase：
//           - 用于 IrisFastArraySerializer 的"native"快速路径（不需要 FPropertyReplicationState 缓冲），
//           - 通常配合 TIrisFastArrayEditor 推模式使用，poll 阶段被精简或省略。
//
// ApplyReplicatedState 的算法（客户端接收侧，重要）：
//   1) 解析 Context.StateBufferData.ChangeMaskData —— 获得 array bit + 元素 changemask；
//   2) 若 array bit 为 1 → 强制重建 SrcArraySerializer 的 ItemMap；
//   3) 在 Dst（本地）的 ItemMap 中找哪些 ReplicationID 已不存在于 Src → 收集到 RemovedIndices；
//   4) 遍历 Src 中每个元素：根据 (ItemIdx % ChangeMaskBitCount) + offset 取对应 changemask 位
//      （注意：bit 数少于元素数时多个元素共享一位 → 必须 Compare 才能确定真脏）：
//      a) 若该 bit 为 1 且 ID 已存在于 Dst：Compare → 不同 → InternalApplyArrayElement 修改 + ModifiedIndices；
//      b) 若该 bit 为 1 且 ID 不在 Dst：新增（Add 到 Dst → 同步 ReplicationID → AddedIndices）；
//      c) 更新 Dst 元素的 MostRecentArrayReplicationKey 与 ReplicationKey++ 以便客户端 replay 重序列化使用；
//   5) 增加 DstArraySerializer.ArrayReplicationKey；
//   6) 调用 PreReplicatedRemove → PostReplicatedAdd → PostReplicatedChange（Item 级 + ArraySerializer 级）；
//   7) 实际从 Dst 中 RemoveAtSwap 删除元素 + Empty ItemMap（强制下次重建）；
//   8) 若派生类定义了 PostReplicatedReceive 则调用（SFINAE 自动选择）。
//
// 注意：本文件只是一个辅助 header，TIrisFastArrayEditor 推式编辑器**不在此处**——
// 它定义在 Public/Iris/ReplicationState/Private/IrisFastArraySerializerInternal.h，与本文件互补。
// =====================================================================================================================

#pragma once

#include "Iris/ReplicationSystem/ReplicationFragment.h"
#include "Iris/Core/IrisProfiler.h"
#include "Iris/ReplicationState/IrisFastArraySerializer.h"
#include "Iris/ReplicationState/Private/IrisFastArraySerializerInternal.h"
#include "Iris/ReplicationState/PropertyReplicationState.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Net/Core/Trace/NetDebugName.h"
#include "Net/Core/NetBitArray.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DECLARE_CATEGORY_MODULE_EXTERN(NETCORE_API, Networking);

namespace UE::Net
{

namespace FastArrayPollingPolicies
{

// FPollingState —— 用于在两次 poll 之间记录 FastArray 的"上次状态镜像"。
// 比对算法基于 ArrayReplicationKey（数组级别一次 inc）+ 每元素 ReplicationKey（item 修改即 ++）：
// 任意 key 不同视作脏。FNeedPollingPolicy 会持有此状态。
struct FPollingState
{
	int32 ArrayReplicationKey = -1;
	struct FEntry
	{
		int32 ReplicationKey = -1;
		int32 ReplicationID = -1;
	};
	TArray<FEntry>  ItemPollData;
};

/*
 * FNeedPollingPolicy, used for Native Iris FastArrays that does not need polling
 */
// 需要 Poll 的策略 —— 用于"普通 FastArray"（用户调用 MarkItemDirty 但未走推式 Editor）。
// PollingState 会被 ReplicationFragment 用来比对找出脏 item。
class FNeedPollingPolicy
{
public:
	FPollingState* GetPollingState() { return &PollingState; }	
private:
	FPollingState PollingState;
};

/*
 * FNeedPollingPolicy, used for Native Iris FastArrays that do not require polling
 */
// 不需要 Poll —— Native Iris FastArray + TIrisFastArrayEditor 推式路径。
// 推模式下每次 Add/Edit/Remove 都直接写 changemask，poll 阶段无事可做，GetPollingState() 返回 nullptr。
class FNoPollingPolicy
{
public:
	FPollingState* GetPollingState() { return nullptr; }
};

} // End of namespace FastArrayPollingPolicies

namespace Private
{

/** Utility methods to behave similar to FastArraySerializer */
// 一组静态工具函数，集中实现"FastArray 应用收包"的标准行为。
// 模板化在使用方的派生类型上展开（FastArrayType / ItemArrayType），便于针对不同 USTRUCT 内嵌数组复用。
struct FFastArrayReplicationFragmentHelper
{
	/** Rebuild IndexMap for FastArrraySerializer */
	// 在 ItemMap 数与实际数组数不一致或强制时，按 Items[i].ReplicationID 重建 ItemMap。
	// ItemMap 是 ReplicationID → 数组下标的快查表，让 Apply 阶段以 O(1) 找到现有元素。
	template <typename FastArrayType, typename ItemArrayType>
	static void ConditionalRebuildItemMap(FastArrayType& ArraySerializer, const ItemArrayType& Items, bool bForceRebuild);

	/** Apply received state and try to behave like current FastArrays */
	// 接收侧应用：见文件头部"ApplyReplicatedState 的算法"详解。
	template <typename FastArrayType, typename ItemArrayType>
	static void ApplyReplicatedState(FastArrayType* DstFastArray, ItemArrayType* DstWrappedArray, FastArrayType* SrcFastArray, const ItemArrayType* SrcWrappedArray, const FReplicationStateDescriptor* ArrayElementDescriptor, FReplicationStateApplyContext& Context);

	/** Apply array element, only replicated items will be applied, using the serializers' Apply function if present  */
	// 单元素 Apply：根据元素 Descriptor 仅写入"复制成员"——保留本地 ReplicationID/客户态字段。
	IRISCORE_API static void InternalApplyArrayElement(const FReplicationStateDescriptor* ArrayElementDescriptor, void* RESTRICT Dst, const void* RESTRICT Src);

	/** Copy array element, only replicated items will be copied */
	// 单元素 Copy：和 Apply 类似但仅"复制"，常用于 Poll 阶段把 Live → Snapshot。
	IRISCORE_API static void InternalCopyArrayElement(const FReplicationStateDescriptor* ArrayElementDescriptor, void* RESTRICT Dst, const void* RESTRICT Src);

	/** Compare array element, only replicated items will be compared */
	// 单元素 Compare：仅比较"复制成员"。当 changemask 对一个 bit 多个元素共享时用此判定真假脏。
	IRISCORE_API static bool InternalCompareArrayElement(const FReplicationStateDescriptor* ArrayElementDescriptor, void* RESTRICT Dst, const void* RESTRICT Src);

	/** Find the member index of the FastArrayIteArray, used to support FastArrayNetSerializers with extra properties */
	// 在 USTRUCT 描述符中定位"内嵌 TArray<Item>"成员的下标。
	// 用于 FastArray 自身又额外定义其他 UPROPERTY 字段的场景（非纯 wrapper）。
	IRISCORE_API static uint32 GetFastArrayStructItemArrayMemberIndex(const FReplicationStateDescriptor* StructDescriptor);

	/**
	 * Conditionally invoke PostReplicatedReceive method depending on if it is defined or not
	 * We only want to do this for FastArrays that define PostReplicatedReceive since it might require extra work to calculate the required parameters
	 */
	// SFINAE：若用户 FastArray 定义了 PostReplicatedReceive，则调用并构造参数（OldArraySize / bHasUnresolvedReferences）；
	// 否则匹配第二个空模板特化，避免无谓构造参数。
	// 通过 TModels_V<CPostReplicatedReceiveFuncable, FastArrayType, ...> 在编译期探测函数签名是否存在。
	template<typename FastArrayType>
	static inline typename TEnableIf<TModels_V<FFastArraySerializer::CPostReplicatedReceiveFuncable, FastArrayType, const FFastArraySerializer::FPostReplicatedReceiveParameters&>, void>::Type CallPostReplicatedReceiveOrNot(FastArrayType& ArraySerializer, int32 OldArraySize, bool bHasUnresolvedReferences)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		FFastArraySerializer::FPostReplicatedReceiveParameters PostReceivedParameters = { OldArraySize, bHasUnresolvedReferences };
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		
		ArraySerializer.PostReplicatedReceive(PostReceivedParameters);
	}

	template<typename FastArrayType>
	static inline typename TEnableIf<!TModels_V<FFastArraySerializer::CPostReplicatedReceiveFuncable, FastArrayType, const FFastArraySerializer::FPostReplicatedReceiveParameters&>, void>::Type CallPostReplicatedReceiveOrNot(FastArrayType& ArraySerializer, int32 OldArraySize, bool bHasUnresolvedReferences) {}
};

// FFastArrayReplicationFragmentBase —— 兼容路径 FastArray 的 ReplicationFragment 基类。
// 它持有：
//   - ReplicationStateDescriptor：已烘焙的 USTRUCT 描述符（FastArray 自身 + 内嵌 Item Descriptor）；
//   - ReplicationState：用户态 FPropertyReplicationState 缓冲（外部 state 与 ChangeMask 的桥）；
//   - Owner & WrappedArrayOffsetRelativeFastArraySerializerProperty：用于在 Owner 上快速找到 TArray 字段地址。
// 派生类（FastArrayReplicationFragment）会基于 PollingPolicy 完成 PollAndCopy 等逻辑。
class FFastArrayReplicationFragmentBase : public FReplicationFragment
{
public:
	IRISCORE_API void Register(FFragmentRegistrationContext& Context, EReplicationFragmentTraits InTraits);

protected:
	IRISCORE_API FFastArrayReplicationFragmentBase(EReplicationFragmentTraits InTraits, UObject* InOwner, const FReplicationStateDescriptor* InDescriptor, bool bValidateDescriptor = true);
	IRISCORE_API virtual ~FFastArrayReplicationFragmentBase();

	// FReplicationFragment Implementation
	IRISCORE_API virtual void CollectOwner(FReplicationStateOwnerCollector* Owners) const override;

protected:
	// Get the ReplicationStateDescriptor for the FastArraySerializer Struct
	// 返回外层 FastArraySerializer 这个 USTRUCT 的描述符
	IRISCORE_API const FReplicationStateDescriptor* GetFastArrayPropertyStructDescriptor() const;

	// Get the ReplicationStateDescriptor for the Array Element
	// 返回 TArray 元素 USTRUCT 的描述符（用于 InternalCopy/Compare/Apply）
	IRISCORE_API const FReplicationStateDescriptor* GetArrayElementDescriptor() const;

	// Copy array element using the descriptor to esure that we only copy replicated data
	IRISCORE_API static void InternalCopyArrayElement(const FReplicationStateDescriptor* ArrayElementDescriptor, void* RESTRICT Dst, const void* RESTRICT Src);

	// Compare an array element using the descriptor to ensure that we only compare replicated data
	IRISCORE_API static bool InternalCompareArrayElement(const FReplicationStateDescriptor* ArrayElementDescriptor, void* RESTRICT Dst, const void* RESTRICT Src);

	// Dequantize state into DstExternalBuffer, Note: it is expected to be initialized
	// 全量反量化：按 Descriptor 把 SrcInternalBuffer（Iris 内部紧凑表示）展开到 DstExternalBuffer（用户态布局）。
	IRISCORE_API static void InternalDequantizeFastArray(FNetSerializationContext& Context, uint8* RESTRICT DstExternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* FastArrayPropertyDescriptor);

	// Partial dequantize state based on changemask into DstExternalBuffer, Note: it is expected to be initialized
	// 部分反量化（按 changemask）。client 收到 delta 时常用：仅展开真正变化的元素。
	IRISCORE_API static void InternalPartialDequantizeFastArray(FReplicationStateApplyContext& Context, uint8* RESTRICT DstExternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* FastArrayPropertyDescriptor);

	// Dequantize additional properties to  DstExternalBuffer, Note: it is expected to be initialized
	// 反量化"FastArray struct 上的其他 UPROPERTY 字段"（不是 Item 数组本身）。
	IRISCORE_API static void InternalDequantizeExtraProperties(FNetSerializationContext& Context, uint8* RESTRICT DstExternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor);

	// Dequantize and output state to string
	// 调试输出当前 ExternalStateBuffer 的可读形式，用于 ReplicationDebugger / NetTrace。
	IRISCORE_API static void ToString(FStringBuilderBase& StringBuilder, const uint8* ExternalStateBuffer, const FReplicationStateDescriptor* FastArrayPropertyDescriptor);

protected:
	// Replication descriptor built for the specific property
	TRefCountPtr<const FReplicationStateDescriptor> ReplicationStateDescriptor;

	// This is the source state from which we source our state data
	// "外部状态缓冲壳"——对接用户内存与 Iris ChangeMask
	TUniquePtr<FPropertyReplicationState> ReplicationState;

	// Owner
	UObject* Owner;

	// This allows us to quickly find the wrapped array relative to the owner
	// 内嵌 TArray 字段相对于 FastArraySerializer USTRUCT 起点的偏移；
	// Owner 中 FastArraySerializer 所在 UPROPERTY 偏移 + 此 = TArray<Item> 实际地址。
	SIZE_T WrappedArrayOffsetRelativeFastArraySerializerProperty;
};

// Native FastArray
// FNativeFastArrayReplicationFragmentBase —— Iris 原生路径，使用 IrisFastArraySerializer 内嵌的 ChangeMask，
// 不需要 FPropertyReplicationState 用户态缓冲（推式编辑下 changemask 直接由 Editor 写）。
// poll/Copy 阶段被简化（推模式下基本是 no-op）。
class FNativeFastArrayReplicationFragmentBase : public FReplicationFragment
{
protected:
	IRISCORE_API FNativeFastArrayReplicationFragmentBase(EReplicationFragmentTraits InTraits, UObject* InOwner, const FReplicationStateDescriptor* InDescriptor);
	IRISCORE_API const FReplicationStateDescriptor* GetFastArrayPropertyStructDescriptor() const;
	IRISCORE_API const FReplicationStateDescriptor* GetArrayElementDescriptor() const;
	IRISCORE_API virtual void CollectOwner(FReplicationStateOwnerCollector* Owners) const override;

	// Dequantize state into DstExternalBuffer, note it is expected to be initialized
	IRISCORE_API static void InternalDequantizeFastArray(FNetSerializationContext& Context, uint8* RESTRICT DstExternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* FastArrayPropertyDescriptor);

	IRISCORE_API static void ToString(FStringBuilderBase& StringBuilder, const uint8* ExternalStateBuffer, const FReplicationStateDescriptor* FastArrayPropertyDescriptor);

protected:
	// Replication descriptor built for the specific property
	TRefCountPtr<const FReplicationStateDescriptor> ReplicationStateDescriptor;

	// Owner
	UObject* Owner;

	// This allows us to quickly find the wrapped array relative to the owner
	SIZE_T WrappedArrayOffsetRelativeFastArraySerializerProperty;
};

// ConditionalRebuildItemMap 实现：仅当强制或长度不一致时才重建。
// ItemMap 不持久维护 ReplicationID == INDEX_NONE 的元素（这些是 Add 后还未分配 ID 的临时项）。
template <typename FastArrayType, typename ItemArrayType>
void FFastArrayReplicationFragmentHelper::ConditionalRebuildItemMap(FastArrayType& ArraySerializer, const ItemArrayType& Items, bool bForceRebuild)
{
	typedef typename ItemArrayType::ElementType ItemType;

	if (bForceRebuild || ArraySerializer.ItemMap.Num() != Items.Num())
	{
		UE_LOG(LogNetFastTArray, Verbose, TEXT("FastArrayDeltaSerialize: Recreating Items map. Items.Num: %d Map.Num: %d"), Items.Num(), ArraySerializer.ItemMap.Num());

		ArraySerializer.ItemMap.Reset();
			
		const ItemType* SrcItems = Items.GetData();
		for (int32 It = 0, EndIt = Items.Num(); It != EndIt; ++It)
		{
			const ItemType& Item = SrcItems[It];
			if (Item.ReplicationID == INDEX_NONE)
			{
				continue;
			}

			ArraySerializer.ItemMap.Add(Item.ReplicationID, It);
		}
	}
}

// ApplyReplicatedState 完整实现：客户端收包后把 Src（已 dequantize 的接收态）应用到 Dst（本地态），
// 触发标准 Pre/PostReplicatedRemove/Add/Change 回调。注意逻辑细节已在文件头部展开说明。
template <typename FastArrayType, typename ItemArrayType>
void FFastArrayReplicationFragmentHelper::ApplyReplicatedState(FastArrayType* DstArraySerializer, ItemArrayType* DstWrappedArray, FastArrayType* SrcArraySerializer, const ItemArrayType* SrcWrappedArray, const FReplicationStateDescriptor* ArrayElementDescriptor, FReplicationStateApplyContext& Context)
{
	typedef typename ItemArrayType::ElementType ItemType;

	CSV_SCOPED_TIMING_STAT(Networking, FastArray_Apply);

	UE_LOG(LogNetFastTArray, Log, TEXT("FFastArrayReplicationFragmentHelper::ApplyReplicatedState for %s"), Context.Descriptor->DebugName->Name);

	// 取出本帧 changemask（包含 array bit + 元素 changemask）
	const uint32* ChangeMaskData = Context.StateBufferData.ChangeMaskData;
	FNetBitArrayView MemberChangeMask = MakeNetBitArrayView(ChangeMaskData, Context.Descriptor->ChangeMaskBitCount);

	// We currently use a simple modulo scheme for bits in the changemask
	// A single bit might represent several entries in the array which all will be considered dirty, it is up to the serializer to handle this
	// The first bit is used by the owning property we need to offset by one and deduct one from the usable bits
	// 元素 changemask 段：起点 + 长度（去掉 array bit 的 1 位）
	const FReplicationStateMemberChangeMaskDescriptor& MemberChangeMaskDescriptor = Context.Descriptor->MemberChangeMaskDescriptors[0];
	const uint32 ChangeMaskBitOffset = MemberChangeMaskDescriptor.BitOffset + FIrisFastArraySerializer::IrisFastArrayChangeMaskBitOffset;
	const uint32 ChangeMaskBitCount = MemberChangeMaskDescriptor.BitCount - FIrisFastArraySerializer::IrisFastArrayChangeMaskBitOffset;

	// Force rebuild if the array has been modified
	// array bit（bit 0）置 1 表示数组结构有变（增删），需要重建 SrcArraySerializer 的 ItemMap
	const bool bForceRebuildItemMap = MemberChangeMask.GetBit(0);

	// We need to rebuild our maps for both target array and incoming data
	// Can optimize this later
	ConditionalRebuildItemMap(*DstArraySerializer, *DstWrappedArray, false);
	ConditionalRebuildItemMap(*SrcArraySerializer, *SrcWrappedArray, bForceRebuildItemMap);

	// We need this for callback
	const int32 OriginalSize = DstWrappedArray->Num();

	// ── 1) 找出移除项：本地存在但远端 ItemMap 中不存在 ──
	// Find removed elements in received data, that is elements that exist in old map but not in new map
	TArray<int32> RemovedIndices;
	{
		RemovedIndices.Reserve(DstWrappedArray->Num());
		ItemType* DstItems = DstWrappedArray->GetData();
		for (int32 It=0, EndIt=DstWrappedArray->Num(); It != EndIt; ++It)
		{
			const int32 ReplicationID = DstItems[It].ReplicationID;
			if (ReplicationID != -1 && !SrcArraySerializer->ItemMap.Contains(ReplicationID))
			{
				UE_LOG(LogNetFastTArray, Log, TEXT("   Removed ID: %d local Idx: %d"), ReplicationID, It);
				RemovedIndices.Add(It);
			}
		}
	}
	
	// ── 2) 找出新增/修改项：通过 changemask 判定，再用 InternalCompareArrayElement 二次确认 ──
	// Find new and modified elements in received data, That is elements that do not exist in old map
	TArray<int32> AddedIndices;
	TArray<int32> ModifiedIndices;
	{
		AddedIndices.Reserve(SrcWrappedArray->Num());
		ModifiedIndices.Reserve(SrcWrappedArray->Num());
		const ItemType* SrcItems = SrcWrappedArray->GetData();
		for (int32 It=0, EndIt=SrcWrappedArray->Num(); It != EndIt; ++It)
		{
			// changemask 位通过 modulo 散列共享：bit i 对应所有 (k * ChangeMaskBitCount + i) 元素
			// 没有 bit（ChangeMaskBitCount==0）时退化为"全部视为脏"
			const bool bIsDirty = ChangeMaskBitCount == 0U || MemberChangeMask.GetBit((It % ChangeMaskBitCount) + ChangeMaskBitOffset);
			if (!bIsDirty)
			{
				continue;
			}

			ItemType* DstItem = nullptr;
			if (int32* ExistingIndex = DstArraySerializer->ItemMap.Find(SrcItems[It].ReplicationID))
			{
				// Only compare if the changemask indicate that this might be a dirty entry, the compare is required since we do share entries in the changemask.
				// 因为 bit 共享，必须 Compare 才能确认这是真脏还是假脏
				if (!InternalCompareArrayElement(ArrayElementDescriptor, &(*DstWrappedArray)[*ExistingIndex], &SrcItems[It]))
				{
					UE_LOG(LogNetFastTArray, Log, TEXT("   Changed. ID: %d -> Idx: %d"), SrcItems[It].ReplicationID, *ExistingIndex);

					ModifiedIndices.Add(*ExistingIndex);

					// We use per element apply since we do not want to overwrite data that is not replicated
					// 仅写"复制成员"，保留客户态字段（例如本地缓存的指针/状态）
					DstItem = &(*DstWrappedArray)[*ExistingIndex];
					InternalApplyArrayElement(ArrayElementDescriptor, DstItem, &SrcItems[It]);
				}
			}
			else
			{
				// Since we zero initialize our replicated properties we can end up with ReplicationID == 0 when receiving partial changes which should be ignored.
				// ReplicationID==0 / INDEX_NONE 都不是有效 ID（前者源自零初始化、后者未分配），跳过。
				if (!(SrcItems[It].ReplicationID == INDEX_NONE || SrcItems[It].ReplicationID == 0))
				{
					int32 AddedIndex = DstWrappedArray->Add(SrcItems[It]);

					UE_LOG(LogNetFastTArray, Log, TEXT("   New. ID: %d. New Element! local Idx: %d"), SrcItems[It].ReplicationID, AddedIndex);

					// We need to propagate the ReplicationID in order to find our object
					// 同步 ReplicationID 以便本地后续按 ID 找到此元素
					DstItem = &(*DstWrappedArray)[AddedIndex];
					DstItem->ReplicationID = SrcItems[It].ReplicationID;

					// should we store ids or indices?
					AddedIndices.Add(AddedIndex);
				}
			}

			if (DstItem != nullptr)
			{
				// Update the item's most recent array replication key
				DstItem->MostRecentArrayReplicationKey = DstArraySerializer->ArrayReplicationKey;

				// Update the item's replication key so that a client can re-serialize the array for client replay recording
				DstItem->ReplicationKey++;
			}
		}
	}

	// Increment keys so that a client can re-serialize the array if needed, such as for client replay recording.
	// 数组级 key 自增。客户端 replay 录制需要把当前数组完整再序列化，靠该 key 触发。
	DstArraySerializer->IncrementArrayReplicationKey();

	// Added and changed callbacks to FastArraySerializer
	const int32 PreRemoveSize = DstWrappedArray->Num();
	const int32 FinalSize = PreRemoveSize - RemovedIndices.Num();

	// ── 3) 标准回调序列：Pre/Post（先 Pre 再做实际删除/插入，最后 Post）──
	// Remove callback
	for (int32 RemovedIndex : RemovedIndices)
	{
		(*DstWrappedArray)[RemovedIndex].PreReplicatedRemove(*DstArraySerializer);
	}

	// Remove callback to FastArraySerializer - done after adding new elements
	DstArraySerializer->PreReplicatedRemove(MakeArrayView(RemovedIndices), FinalSize);

	if (PreRemoveSize != DstWrappedArray->Num())
	{
		UE_LOG(LogNetFastTArray, Error, TEXT("Item size changed after PreReplicatedRemove! PremoveSize: %d  Item.Num: %d"),
			PreRemoveSize, DstWrappedArray->Num());
	}

	// Add callbacks
	for (int32 AddedIndex : AddedIndices)
	{
		(*DstWrappedArray)[AddedIndex].PostReplicatedAdd(*DstArraySerializer);
	}
	DstArraySerializer->PostReplicatedAdd(MakeArrayView(AddedIndices), FinalSize);

	// Change callbacks
	for (int32 ExistingIndex : ModifiedIndices)
	{
		(*DstWrappedArray)[ExistingIndex].PostReplicatedChange(*DstArraySerializer);
	}
	DstArraySerializer->PostReplicatedChange(MakeArrayView(ModifiedIndices), FinalSize);

	if (PreRemoveSize != DstWrappedArray->Num())
	{
		UE_LOG(LogNetFastTArray, Error, TEXT("Item size changed after PostReplicatedAdd/PostReplicatedChange! PreRemoveSize: %d  Item.Num: %d"),
			PreRemoveSize, DstWrappedArray->Num());
	}

	// Remove indices
	// 真正移除元素：从大到小 RemoveAtSwap，避免索引错位；ItemMap 因 swap 失效需 Empty 等待下次重建。
	if (RemovedIndices.Num() > 0)
	{
		RemovedIndices.Sort();
		for (int32 i = RemovedIndices.Num() - 1; i >= 0; --i)
		{
			int32 DeleteIndex = RemovedIndices[i];
			if (DstWrappedArray->IsValidIndex(DeleteIndex))
			{
				DstWrappedArray->RemoveAtSwap(DeleteIndex, EAllowShrinking::No);
			}
		}

		// Clear the map now that the indices are all shifted around. This kind of sucks, we could use slightly better data structures here I think.
		// This will force the ItemMap to be rebuilt for the current Items array
		DstArraySerializer->ItemMap.Empty();
	}

	// Invoke PostReplicatedReceive if is defined by the serializer
	// 如果用户实现了 PostReplicatedReceive，最后调用一次。
	CallPostReplicatedReceiveOrNot(*DstArraySerializer, OriginalSize, Context.bHasUnresolvableReferences);
}

}} // End of namespaces
