// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// ReplicationStateDescriptorMacros.h —— 手写 ReplicationState 类的"声明侧"辅助宏
// -------------------------------------------------------------------------------------
// 本文件是 ReplicationStateDescriptorImplementationMacros.h 的姊妹文件：
//   - 这里定义在 .h 类内部使用的 IRIS_DECLARE_COMMON / IRIS_ACCESS_BY_VALUE /
//     IRIS_ACCESS_BY_REFERENCE 等"声明宏"；
//   - 那里定义在 .cpp 中使用的 IRIS_IMPLEMENT_* 等"实现宏"。
//
// 【典型使用】
//   class FMyState
//   {
//       float Position;
//       FVector Velocity;
//       IRIS_DECLARE_COMMON()                 // 声明 sReplicationStateDescriptor 等静态字段 + ChangeMask + IsDirty/SetDirty
//       IRIS_ACCESS_BY_VALUE(Position, float, 0)        // 生成 SetPosition / GetPosition / IsPositionDirty
//       IRIS_ACCESS_BY_REFERENCE(Velocity, FVector, 1)  // 生成 const FVector& Get... 形式
//   };
// =====================================================================================

#pragma once

#include "HAL/Platform.h"
#include "Templates/UnrealTemplate.h"
#include "Net/Core/NetBitArray.h"
#include "Iris/ReplicationState/ReplicationStateUtil.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"


//
// $IRIS: TEMPORARY TO HELP WITH FAKE GENERATING REPLICATIONSTATES
// define macros to declare and implement the fake Generated State
// This will be generate by UHT 
//

//
// Helper functions
//

namespace UE::Net::Private
{

// 【CalculateWordCountForChangeMask】根据 ChangeMaskDescriptors 数组的最后一项推算
// 总位数，再换算为 32-bit word 数（向上取整）。用于 IRIS_DECLARE_COMMON 在类内静态
// 分配 ChangeMask[] 数组的长度。
template <typename T>
static constexpr uint32 CalculateWordCountForChangeMask(const T& ChangeMaskDescriptors)
{
	const uint32 Index = UE_ARRAY_COUNT(ChangeMaskDescriptors);
	return Index == 0u ? 0u : UE::Net::FNetBitArrayView::CalculateRequiredWordCount(ChangeMaskDescriptors[Index - 1u].BitOffset + ChangeMaskDescriptors[Index - 1u].BitCount);
}

}

//
// DECLARATION MACROS
//

// 【IRIS_DECLARE_COMMON】手写状态类内部展开：
//   - 声明所有 static const 数组（MemberDescriptors / SerializerDescriptors / TraitsDescriptors / ...）；
//   - 声明 static const FReplicationStateDescriptor sReplicationStateDescriptor 单例；
//   - 内嵌运行期数据 InternalReplicationState（Header）和 ChangeMask[] 字数组；
//   - 提供 SetDirty / IsDirty 内联辅助方法 —— 直接走 MarkDirty 入口；
//   - 暴露 public 静态 GetReplicationStateDescriptor 取出本类型的 Descriptor。
//
// 注意：sReplicationStateChangeMaskDescriptors 必须由用户在调用 IRIS_DECLARE_COMMON
//       之前定义（通常放在头文件外或同 .cpp 中作 static const 数组），因为 ChangeMask
//       字数组长度需要编译期知晓。
#define IRIS_DECLARE_COMMON() \
private: \
	static const UE::Net::FReplicationStateDescriptor sReplicationStateDescriptor; \
	static const UE::Net::FReplicationStateMemberDescriptor sReplicationStateDescriptorMemberDescriptors[]; \
	static const UE::Net::FReplicationStateMemberSerializerDescriptor sReplicationStateDescriptorMemberSerializerDescriptors[]; \
	static const UE::Net::FReplicationStateMemberTraitsDescriptor sReplicationStateDescriptorMemberTraitsDescriptors[]; \
	static const UE::Net::FReplicationStateMemberFunctionDescriptor  sReplicationStateDescriptorMemberFunctionDescriptors[]; \
	static const UE::Net::FReplicationStateMemberTagDescriptor  sReplicationStateDescriptorMemberTagDescriptors[]; \
	static const UE::Net::FReplicationStateMemberReferenceDescriptor  sReplicationStateDescriptorMemberReferenceDescriptors[]; \
	static const UE::Net::FReplicationStateMemberDebugDescriptor sReplicationStateDescriptorMemberDebugDescriptors[]; \
	UE::Net::FReplicationStateHeader InternalReplicationState; \
	UE::Net::FNetBitArrayView::StorageWordType ChangeMask[UE::Net::Private::CalculateWordCountForChangeMask(sReplicationStateChangeMaskDescriptors)] = { }; \
\
	/* helpers, should do special methods for this with no safety at all since this will be generated. Single bitcase should be as fast as possible */ \
	/* SetDirty/IsDirty：标脏与判脏，直接调用 ReplicationStateUtil::MarkDirty 入口（含首次置脏的 NetObject 通知）。 */ \
	void SetDirty(const UE::Net::FReplicationStateMemberChangeMaskDescriptor& Bits) { UE::Net::FNetBitArrayView Mask(&ChangeMask[0], sReplicationStateDescriptor.ChangeMaskBitCount); UE::Net::Private::MarkDirty(InternalReplicationState, Mask, Bits);} \
	bool IsDirty(const UE::Net::FReplicationStateMemberChangeMaskDescriptor& Bits) const { const UE::Net::FNetBitArrayView Mask(const_cast<UE::Net::FNetBitArrayView::StorageWordType*>(&ChangeMask[0]), sReplicationStateDescriptor.ChangeMaskBitCount); return Mask.GetBit(Bits.BitOffset); } \
public: \
	static const UE::Net::FReplicationStateDescriptor* GetReplicationStateDescriptor() { return &sReplicationStateDescriptor; }

// 【IRIS_ACCESS_BY_VALUE】按值传递的 Setter/Getter（适合 POD：float/int/...）
//   - SetXxx(Value)：仅当值不同才置脏（避免无意义的写包）+ 写入字段；
//   - GetXxx()    ：直接返回值副本；
//   - IsXxxDirty()：查询本字段在 ChangeMask 中是否被置脏。
#define IRIS_ACCESS_BY_VALUE(MemberName, MemberType, MemberIndex) \
void Set##MemberName(MemberType Value) { if (MemberName != Value) { SetDirty(sReplicationStateChangeMaskDescriptors[MemberIndex]); } MemberName = Value; } \
MemberType Get##MemberName() const { return MemberName; } \
bool Is##MemberName##Dirty() const { return IsDirty(sReplicationStateChangeMaskDescriptors[MemberIndex]); }

// 【IRIS_ACCESS_BY_REFERENCE】按引用传递（适合大对象：FVector/FString/...）
//   - SetXxx(const &)：与 ByValue 等价但避免值拷贝；
//   - GetXxx()       ：返回 const 引用避免值拷贝；
//   - IsXxxDirty()   ：同 ByValue。
#define IRIS_ACCESS_BY_REFERENCE(MemberName, MemberType, MemberIndex) \
void Set##MemberName(const MemberType& Value) { if (MemberName != Value) { SetDirty(sReplicationStateChangeMaskDescriptors[MemberIndex]); } MemberName = Value; } \
const MemberType& Get##MemberName() const { return MemberName; } \
bool Is##MemberName##Dirty() const { return IsDirty(sReplicationStateChangeMaskDescriptors[MemberIndex]); }
