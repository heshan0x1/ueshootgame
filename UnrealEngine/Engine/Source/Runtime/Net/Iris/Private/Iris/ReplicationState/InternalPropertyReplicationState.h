// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"

namespace UE::Net
{
	class FNetBitArrayView;
}

// =============================================================================================
// 中文说明：InternalPropertyReplicationState
// =============================================================================================
// 这是 FPropertyReplicationState 的"内部辅助层"，封装所有不暴露给外界、但被 PropertyReplicationState
// 与各种 Fragment / 复制管线内部反复调用的低层操作：
//   - StateBuffer 的构造 / 析构（递归处理 Property->InitializeValue / DestroyValue + ChangeMask 初始化）；
//   - 复制：CopyPropertyReplicationState（整体）/ CopyDirtyMembers（按 ChangeMask）；
//   - 单成员"Apply"：把外部 source 应用到 target——分支处理 Serializer.HasApply / Struct / Array；
//   - 单成员"Copy"：纯 memberwise 拷贝（不调用 Serializer Apply）；
//   - 嵌套 Struct 处理：InternalCopy/CompareStructProperty 跳过 non-replicated 成员；
//   - TArray 元素级比较 + 拷贝：InternalCompareAndCopyArrayWithElementChangeMask（同时维护 ChangeMask）。
//
// 这一层之所以被独立：因为 Iris 的"复制状态"语义和 UProperty 的"内存拷贝"语义存在差异——
//   1) 一个 UStruct 中可能存在 non-replicated 字段，必须按 Descriptor 列表逐成员处理；
//   2) 自定义 NetSerializer 的 struct（如 FVector / FRotator）需要走 Serializer.Apply 而非 memcpy；
//   3) TArray 的元素 dirty 跟踪需要在比较时同时更新 ChangeMask。
// =============================================================================================

namespace UE::Net::Private
{

/** Construct the external state described by the descriptor in the given Buffer */
// 中文：在 StateBuffer 起点构造一份外部表示——
//   1) 在头部 placement-new 一个 FReplicationStateHeader（30bit NetHandleId + 2bit dirty flag）；
//   2) 清零 ChangeMask；
//   3) 若有 Lifetime/Custom Conditionals，把 ConditionalChangeMask 全部置 1（默认全部启用）；
//   4) 对每个 Member（同一 C 数组只在 ArrayIndex==0 调用一次）调用 Property->InitializeValue。
IRISCORE_API void ConstructPropertyReplicationState(uint8* StateBuffer, const FReplicationStateDescriptor* Descriptor);

/** Destruct the external state described by the descriptor in the given Buffer */
// 中文：与 Construct 配对——若状态非 IsSourceTriviallyDestructible，就对每个成员调用
//       Property->DestroyValue（同样只在 C 数组首元素调用），释放内嵌的动态内存（FString/TArray 等）。
IRISCORE_API void DestructPropertyReplicationState(uint8* StateBuffer, const FReplicationStateDescriptor* Descriptor);

/** Copy external state in SrcStateBuffer to external state DstStateBuffer including changemask */
// 中文：把 Src 整个外部状态（含 ChangeMask 与 ConditionalChangeMask）覆盖式拷贝到 Dst。
//       逐成员调用 Property->CopyCompleteValue。
IRISCORE_API void CopyPropertyReplicationState(uint8* RESTRICT DstStateBuffer, uint8* RESTRICT SrcStateBuffer, const FReplicationStateDescriptor* Descriptor);

/** Copy only properties from external state in SrcStateBuffer to external state DstStateBufferif dirty, destination changemask will also be updated */
// 中文：仅拷贝在 Src ChangeMask 中标 dirty 的成员（InitState 例外，仍走全量拷贝）。
//       目标 ChangeMask 用 OR 方式与 Src 合并（保留 Dst 已有 dirty 位）。
IRISCORE_API void CopyDirtyMembers(uint8* RESTRICT DstStateBuffer, uint8* RESTRICT SrcStateBuffer, const FReplicationStateDescriptor* Descriptor);

/** Copy property value to target. If the property is fully replicated we use the properties copy function, otherwise we do a per member copy of all replicated data. Members with NetSerializers with an Apply() function will call that. */
// 中文：单成员 Apply（外部 → 目标）。分支：
//   1) Serializer 自带 Apply（HasApply trait） → 直接调用 Serializer->Apply；
//   2) Struct + 含非复制成员 → 走 InternalApplyStructProperty（递归 + memberwise）；
//   3) Array 且元素是上述两种情形 → 调整目标长度后逐元素 Apply；
//   4) 其他默认情形 → Property->CopySingleValue（memcpy 等价语义）。
IRISCORE_API void InternalApplyPropertyValue(const FReplicationStateDescriptor* Descriptor, uint32 MemberIndex, void* RESTRICT Dst, const void* RESTRICT Src);

/** Copy struct members to target using InternalApplyPropertyValue */
// 中文：按 StructDescriptor 递归 Apply 每个成员。对"派生自 NetSerializer 基类的 struct"
//       (IsDerivedFromStructWithCustomSerializer / IsStructWithCustomSerializer) 先处理 base
//       struct 的 Apply（或 BaseStruct->CopyScriptStruct），再 Apply 派生新增的成员。
IRISCORE_API void InternalApplyStructProperty(const FReplicationStateDescriptor* StructDescriptor, void* RESTRICT Dst, const void* RESTRICT Src);

/** Copy property value, if the property is fully replicated we use the properties copy function, otherwise we do a per member copy of all replicated data */
// 中文：与 InternalApplyPropertyValue 同样语义但不走 Serializer.Apply。仅在内部 ReplicationState
//       之间拷贝时使用（不需要 Apply 的 side-effects）。结构与 Array 同样按 Descriptor 递归处理。
IRISCORE_API void InternalCopyPropertyValue(const FReplicationStateDescriptor* Descriptor, uint32 MemberIndex, void* RESTRICT Dst, const void* RESTRICT Src);

/** Copy struct members using InternalCopyPropertyValue */
// 中文：递归把 StructDescriptor 中的所有成员从 Src 拷到 Dst（派生 struct 先 base copy 一次）。
IRISCORE_API void InternalCopyStructProperty(const FReplicationStateDescriptor* StructDescriptor, void* RESTRICT Dst, const void* RESTRICT Src);

/** Compare struct members using InternalCompareMember, this function will only compare replicated members of the struct */
// 中文：比较两个 Struct 的所有 replicated 成员（跳过 base struct serializer-equal、再逐成员 InternalCompareMember），
//       任一不等返回 false。注意：non-replicated 成员不参与比较。
IRISCORE_API bool InternalCompareStructProperty(const FReplicationStateDescriptor* StructDescriptor, const void* RESTRICT ValueA, const void* RESTRICT ValueB);

/** CompareMembers, if data is not fully replicated we will use per member compare for structs and arrays */
// 中文：单成员比较——优先用 Serializer.IsEqual；若是 struct/array 含非复制字段则递归走
//       InternalCompareStructProperty；其余默认调 Property->Identical。
IRISCORE_API bool InternalCompareMember(const FReplicationStateDescriptor* Descriptor, uint32 MemberIndex, const void* RESTRICT ValueA, const void* RESTRICT ValueB);

/**
 * Compare array elements and mark dirty indices in the changemask. The resulting DstArray will be identical to the SrcArray after calling this function.
 * @param ChangeMask needs to be big enough for the entire descriptor. The member's change mask descriptor will be modified for dirty array indices. The bit which indicates the array is dirty is left unmodified.
 * @return false if the DstArray was modified in anyway, including size changes, and true if the arrays were already identical.
 */
// 中文：TArray 专用——同时做"元素级比较 + 选择性拷贝 + element changemask 更新"。
//   - 检测 size 变化：缩小则清掉超出范围的 element bit；增长则跳过比较直接置位（视为新元素）；
//   - 逐元素 InternalCompareStructProperty；不等则置位 element bit + InternalCopyStructProperty；
//   - "数组已 dirty" 的总位（TArrayPropertyChangeMaskBitIndex=0）由调用方在外部 MarkArrayDirty 设置；
//   - 调用结束后 DstArray 与 SrcArray 保持一致；返回 true 表示原本就完全相等（未做任何修改）。
IRISCORE_API bool InternalCompareAndCopyArrayWithElementChangeMask(const FReplicationStateDescriptor* Descriptor, uint32 MemberIndex, const void* RESTRICT DstArray, const void* RESTRICT SrcArray, UE::Net::FNetBitArrayView& ChangeMask);

}
