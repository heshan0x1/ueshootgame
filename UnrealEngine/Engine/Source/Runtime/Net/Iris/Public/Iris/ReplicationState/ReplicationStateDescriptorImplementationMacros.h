// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// ReplicationStateDescriptorImplementationMacros.h —— 手写"伪反射"Descriptor 实现宏
// -------------------------------------------------------------------------------------
// 【用途】在没有 UHT 真正生成代码之前，Iris 提供一组宏来"手写"一个 ReplicationState
// 的 Descriptor 数组（MemberDescriptor / Serializer / Traits / Tag / Function /
// Reference / Debug 等），并把它们组装成一个完整的 FReplicationStateDescriptor 单例。
//
// 【典型使用流程】（伪代码）
//   class FMyState
//   {
//       float Position;       int32 Health;
//       IRIS_DECLARE_COMMON()  // 声明所有 static 数组 + sReplicationStateDescriptor
//       IRIS_ACCESS_BY_VALUE(Position, float, 0)
//       IRIS_ACCESS_BY_VALUE(Health, int32, 1)
//   };
//
//   // 在 .cpp 中：
//   IRIS_BEGIN_INTERNAL_TYPE_INFO(FMyState)
//       IRIS_INTERNAL_TYPE_INFO(FFloatNetSerializer)
//       IRIS_INTERNAL_TYPE_INFO(FInt32NetSerializer)
//   IRIS_END_INTERNAL_TYPE_INFO()
//
//   IRIS_BEGIN_SERIALIZER_DESCRIPTOR(FMyState)
//       IRIS_SERIALIZER_DESCRIPTOR(FFloatNetSerializer, nullptr)
//       IRIS_SERIALIZER_DESCRIPTOR(FInt32NetSerializer, nullptr)
//   IRIS_END_SERIALIZER_DESCRIPTOR()
//
//   IRIS_BEGIN_MEMBER_DESCRIPTOR(FMyState)
//       IRIS_MEMBER_DESCRIPTOR(FMyState, Position, 0)
//       IRIS_MEMBER_DESCRIPTOR(FMyState, Health, 1)
//   IRIS_END_MEMBER_DESCRIPTOR()
//
//   // 同理 IRIS_BEGIN/END_TRAITS/TAG/FUNCTION/REFERENCE/MEMBER_DEBUG_DESCRIPTOR
//   IRIS_IMPLEMENT_CONSTRUCT_AND_DESTRUCT(FMyState)
//   IRIS_IMPLEMENT_REPLICATIONSTATEDESCRIPTOR(FMyState)
//
// 【对比】FReplicationStateDescriptorBuilder 是从反射动态烘焙；本宏组是"静态烘焙"，
// 适用于 Iris 自身的内置状态（NetRoles 等），不需要也不应当用反射开销。
//
// 【为什么使用 "Count - 1U"】
//   Tag/Function/Reference 数组允许为空，但 C++ 规定零长度数组非法，所以 IRIS_END_*
//   宏会追加一个 dummy 末尾哨兵元素 { ... }，最终统计 Count 时再减 1 还原真实数量。
// =====================================================================================

#pragma once

#include "CoreTypes.h"
#include "Templates/UnrealTemplate.h"
#include "Net/Core/NetBitArray.h"
#include "Net/Core/Trace/NetDebugName.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Iris/Serialization/NetSerializers.h"

//
// $IRIS: TEMPORARY TO HELP WITH FAKE GENERATING REPLICATIONSTATES
// define macros to declare and implement the fake Generated State
// The idea is to generate this automatically for native iris replication
//

namespace UE::Net::Private
{

// 【FInternalTypeInfo】内部类型信息——记录每个 Member 在量化（Internal）表示下的
// size/alignment。由 IRIS_INTERNAL_TYPE_INFO 宏配合 NetSerializer 自带的 trait 填入。
// 用于 constexpr 计算 InternalMemberOffset，避免运行期偏移计算。
struct FInternalTypeInfo
{
	uint32 Size;
	uint32 Alignment;
};

// 【GetInternalMemberOffset】计算成员 i 在 Internal 缓冲中的偏移：
//   Member0 偏移恒为 0；后续成员 = Align(prev.Offset + prev.Size, current.Alignment)。
// 编译期递归，要求 MemberDescriptors[i-1].InternalMemberOffset 已正确递推。
constexpr uint32 GetInternalMemberOffset(const FReplicationStateMemberDescriptor* MemberDescriptors, const FInternalTypeInfo* InternalTypeInfo, uint32 MemberIndex)
{
	return MemberIndex == 0 ? 0u : Align(MemberDescriptors[MemberIndex - 1].InternalMemberOffset + InternalTypeInfo[MemberIndex - 1].Size, InternalTypeInfo[MemberIndex].Alignment);
}

// 【GetInternalStateSize】Internal 总大小：最后一个成员的偏移 + 大小。
// 注意：未对齐到结构体对齐边界，调用方需自己 Align 到 Alignment。
template <typename T>
constexpr uint32 GetInternalStateSize(const T& MemberDescriptors, const FInternalTypeInfo* InternalTypeInfo)
{
	const uint32 Count = UE_ARRAY_COUNT(MemberDescriptors);

	if (Count == 0)
	{
		return 0;
	}

	return MemberDescriptors[Count - 1].InternalMemberOffset + InternalTypeInfo[Count - 1].Size;
}

// 【GetInternalStateAlignment】Internal 整体对齐取所有成员对齐的最大值（保证最严格成员能正确对齐）。
template <typename T>
constexpr uint16 GetInternalStateAlignment(const T& MemberDescriptors, const FInternalTypeInfo* InternalTypeInfo)
{
	const uint32 Count = UE_ARRAY_COUNT(MemberDescriptors);

	uint16 Alignment = 1;

	for (uint32 It = 0; It < Count; ++It)
	{
		Alignment = FPlatformMath::Max<uint16>(Alignment, static_cast<uint16>(InternalTypeInfo[It].Alignment));
	}

	return Alignment;
}

// 【GetMemberChangeMaskSize】根据 ChangeMaskDescriptors 数组的最后一项推算
// 总 ChangeMask 位数（最后位偏移 + 该位 BitCount）。
template <typename T>
constexpr uint32 GetMemberChangeMaskSize(const T& MemberChangeMaskDescriptors)
{
	constexpr uint32 Count = UE_ARRAY_COUNT(MemberChangeMaskDescriptors);
	return Count > 0u ? MemberChangeMaskDescriptors[Count - 1].BitOffset + MemberChangeMaskDescriptors[Count - 1].BitCount : 0u;
}

}

//
// IMPLEMENTATION MACROS
//

// Used to declare an temporary array with serializer, config size and alignment for the internal types used by the specified serializes
// This is used by our fake generated states to make the declarations more readable.
// 【内部类型信息表】通过 NetSerializer 的 trait 宏取出每种序列化类型的 size/alignment。
// IRIS_INTERNAL_TYPE_INFO(FFloatNetSerializer) 会展开为 { 4, 4 } 之类。
#define IRIS_BEGIN_INTERNAL_TYPE_INFO(StateName) static const UE::Net::Private::FInternalTypeInfo StateName ## TypeInfoData[] = {
#define IRIS_INTERNAL_TYPE_INFO(SerializerName) { UE_NET_GET_SERIALIZER_INTERNAL_TYPE_SIZE(SerializerName), UE_NET_GET_SERIALIZER_INTERNAL_TYPE_ALIGNMENT(SerializerName) },
#define IRIS_END_INTERNAL_TYPE_INFO() };

// Used to declare the array of member serializer descriptors
// 【序列化描述数组】每个成员引用一个 FNetSerializer + Config（nullptr 表示用默认 Config）。
#define IRIS_BEGIN_SERIALIZER_DESCRIPTOR(StateName) const UE::Net::FReplicationStateMemberSerializerDescriptor StateName::sReplicationStateDescriptorMemberSerializerDescriptors[] = {
#define IRIS_SERIALIZER_DESCRIPTOR(SerializerName, ConfigPointer) { &UE_NET_GET_SERIALIZER(SerializerName), ConfigPointer ? ConfigPointer : UE_NET_GET_SERIALIZER_DEFAULT_CONFIG(SerializerName)},
#define IRIS_END_SERIALIZER_DESCRIPTOR() };

// Used to declare the array of member traits descriptors
// 【Trait 描述数组】每个成员的 EReplicationStateMemberTraits 位掩码（HasObjectReference、
// HasDynamicState 等）。运行期供 NetSerializer 选择正确分支。
#define IRIS_BEGIN_TRAITS_DESCRIPTOR(StateName) const UE::Net::FReplicationStateMemberTraitsDescriptor StateName::sReplicationStateDescriptorMemberTraitsDescriptors[] = {
#define IRIS_TRAITS_DESCRIPTOR(Traits) { Traits },
#define IRIS_END_TRAITS_DESCRIPTOR() };

// Used to declare the optional array of member tag descriptors. Since tags are optional we need to create a fake one to prevent a zero-sized array.
// 【Tag 描述数组】RepTag 可选——附加一个 dummy 哨兵 { 0, ~0, ~0 } 以避免零长数组。
// FReplicationStateDescriptor 中的 TagCount 会由 IRIS_IMPLEMENT_REPLICATIONSTATEDESCRIPTOR 设置为 (Count-1)。
#define IRIS_BEGIN_TAG_DESCRIPTOR(StateName) const UE::Net::FReplicationStateMemberTagDescriptor StateName::sReplicationStateDescriptorMemberTagDescriptors[] = {
#define IRIS_TAG_DESCRIPTOR(Tag, MemberIndex) { Tag, MemberIndex, uint16(~0) },
#define IRIS_END_TAG_DESCRIPTOR() { UE::Net::FRepTag(0) /* UE::Net::GetInvalidRepTag() */, uint16(~0), uint16(~0) } };

// Used to declare the optional array of member function descriptors. Since functions are optional we need to create a fake one to prevent a zero-sized array.
// 【Function 描述数组】记录与本 State 关联的 RPC/Notify UFunction 及其 Descriptor。
// 同样需要 dummy 末尾哨兵。
#define IRIS_BEGIN_FUNCTION_DESCRIPTOR(StateName) const UE::Net::FReplicationStateMemberFunctionDescriptor StateName::sReplicationStateDescriptorMemberFunctionDescriptors[] = {
#define IRIS_FUNCTION_DESCRIPTOR(Function, Descriptor) { Function, Descriptor },
#define IRIS_END_FUNCTION_DESCRIPTOR() { {} } };

// Used to declare the optional array of member reference descriptors. Since references are optional we need to create a fake one to prevent a zero-sized array.
// 【Reference 描述数组】记录所有指向其它 NetObject 的引用偏移信息。同样末尾哨兵。
#define IRIS_BEGIN_REFERENCE_DESCRIPTOR(StateName) const UE::Net::FReplicationStateMemberReferenceDescriptor StateName::sReplicationStateDescriptorMemberReferenceDescriptors[] = {
#define IRIS_END_REFERENCE_DESCRIPTOR() { {} } };

// Declare an entry in the ReplicationStateMemberDescriptor array, requires the temporary TypeInfoData array to be declared
// 【成员偏移描述数组】每个成员同时记录 External 偏移（offsetof）+ Internal 偏移
// （由 GetInternalMemberOffset 编译期递推）。
#define IRIS_BEGIN_MEMBER_DESCRIPTOR(StateName) const UE::Net::FReplicationStateMemberDescriptor StateName::sReplicationStateDescriptorMemberDescriptors[] = {
#define IRIS_MEMBER_DESCRIPTOR(StateName, MemberName, MemberIndex) { offsetof(StateName, MemberName), UE::Net::Private::GetInternalMemberOffset(sReplicationStateDescriptorMemberDescriptors, StateName ## TypeInfoData, MemberIndex) },
#define IRIS_END_MEMBER_DESCRIPTOR() };

// Used to declare the mandatory array of member debug descriptors
// 【调试名数组】给每个成员附调试名（在 Iris log / replay 中用于显示）。
// 通过 CreatePersistentNetDebugName 申请进程生命期常驻内存。
#define IRIS_BEGIN_MEMBER_DEBUG_DESCRIPTOR(StateName) const UE::Net::FReplicationStateMemberDebugDescriptor StateName::sReplicationStateDescriptorMemberDebugDescriptors[] = {
#define IRIS_MEMBER_DEBUG_DESCRIPTOR(StateName, MemberDebugName) { UE::Net::CreatePersistentNetDebugName(TEXT(#MemberDebugName)), },
#define IRIS_END_MEMBER_DEBUG_DESCRIPTOR() };

// Implement the required construct and destruct functions
// 【构造/析构函数对】
//   - Construct##StateName 在给定 buffer 上执行 placement new；
//   - Destruct##StateName  反向调用析构函数。
// FReplicationStateDescriptor 内部就是用这两个函数指针在 buffer 上 spawn/destroy state。
#define IRIS_IMPLEMENT_CONSTRUCT_AND_DESTRUCT(StateName) \
void Construct##StateName(uint8* StateBuffer, const UE::Net::FReplicationStateDescriptor* Descriptor) { new (StateBuffer) StateName(); } \
void Destruct##StateName(uint8* StateBuffer, const UE::Net::FReplicationStateDescriptor* Descriptor) { StateName* State = reinterpret_cast<StateName*>(StateBuffer); State->~StateName(); }

// Implement the ReplicationStateDescriptor for the faked state

// 【主组装宏 IRIS_IMPLEMENT_REPLICATIONSTATEDESCRIPTOR_WITH_TRAITS】
//   一次性把所有上面声明的数组、构造/析构函数、ChangeMaskBitCount、CityHash64 计算的
//   DescriptorIdentifier、Traits 等组装成一个完整的 FReplicationStateDescriptor 实例。
//
//   字段对应关系（按结构体声明顺序）：
//     [0]  MemberDescriptors           ← sReplicationStateDescriptorMemberDescriptors
//     [1]  MemberChangeMaskDescriptors ← sReplicationStateChangeMaskDescriptors（由用户在 .h 提前声明）
//     [2]  MemberSerializerDescriptors ← sReplicationStateDescriptorMemberSerializerDescriptors
//     [3]  MemberTraitsDescriptors     ← sReplicationStateDescriptorMemberTraitsDescriptors
//     [4]  MemberFunctionDescriptors   ← 数组若仅有哨兵则为 nullptr，否则取首址
//     [5]  MemberTagDescriptors        ← 同上
//     [6]  MemberReferenceDescriptors  ← 同上
//     [7]  MemberProperties            ← nullptr（伪反射不挂 FProperty）
//     [8]  MemberPropertyDescriptors   ← nullptr
//     [9]  MemberLifetimeConditionDescriptors ← nullptr（手写状态默认无条件）
//     [10] MemberRepIndexToMemberIndexDescriptors ← nullptr
//     [11] BaseStruct                  ← nullptr
//     [12] DebugName                   ← persistent debug name
//     [13] MemberDebugDescriptors      ← sReplicationStateDescriptorMemberDebugDescriptors
//     [14..] sizeof/alignment/Counts/ChangeMaskBitCount/ChangeMasksExternalOffset
//     [..]  DescriptorIdentifier       ← CityHash64(StateName) lambda 立即计算
//     [..]  Construct/Destruct/CreateAndRegisterFragmentFunc/Traits/RefCount=0
#define IRIS_IMPLEMENT_REPLICATIONSTATEDESCRIPTOR_WITH_TRAITS(StateName, Traits) \
const UE::Net::FReplicationStateDescriptor StateName::sReplicationStateDescriptor = \
{ \
	&sReplicationStateDescriptorMemberDescriptors[0], \
	&sReplicationStateChangeMaskDescriptors[0], \
	&sReplicationStateDescriptorMemberSerializerDescriptors[0], \
	&sReplicationStateDescriptorMemberTraitsDescriptors[0], \
	(UE_ARRAY_COUNT(sReplicationStateDescriptorMemberFunctionDescriptors) > 1 ? &sReplicationStateDescriptorMemberFunctionDescriptors[0] : static_cast<const UE::Net::FReplicationStateMemberFunctionDescriptor*>(nullptr)), \
	(UE_ARRAY_COUNT(sReplicationStateDescriptorMemberTagDescriptors) > 1 ? &sReplicationStateDescriptorMemberTagDescriptors[0] : static_cast<const UE::Net::FReplicationStateMemberTagDescriptor*>(nullptr)), \
	(UE_ARRAY_COUNT(sReplicationStateDescriptorMemberReferenceDescriptors) > 1 ? &sReplicationStateDescriptorMemberReferenceDescriptors[0] : static_cast<const UE::Net::FReplicationStateMemberReferenceDescriptor*>(nullptr)), \
	static_cast<const FProperty**>(nullptr), /* MemberProperties */ \
	static_cast<const UE::Net::FReplicationStateMemberPropertyDescriptor*>(nullptr), \
	static_cast<const UE::Net::FReplicationStateMemberLifetimeConditionDescriptor*>(nullptr), \
	static_cast<const UE::Net::FReplicationStateMemberRepIndexToMemberIndexDescriptor*>(nullptr), \
	static_cast<const UScriptStruct*>(nullptr), \
	UE::Net::CreatePersistentNetDebugName(TEXT(#StateName)), \
	&sReplicationStateDescriptorMemberDebugDescriptors[0],\
	sizeof(StateName), \
	UE::Net::Private::GetInternalStateSize(sReplicationStateDescriptorMemberDescriptors, StateName ## TypeInfoData), \
	alignof(StateName),	\
	UE::Net::Private::GetInternalStateAlignment(sReplicationStateDescriptorMemberDescriptors, StateName ## TypeInfoData), \
	static_cast<uint16>(UE_ARRAY_COUNT(sReplicationStateDescriptorMemberDescriptors)), /* MemberCount */ \
	static_cast<uint16>(UE_ARRAY_COUNT(sReplicationStateDescriptorMemberFunctionDescriptors) - 1U), /* FunctionCount —— 减 1 排除哨兵 */ \
	static_cast<uint16>(UE_ARRAY_COUNT(sReplicationStateDescriptorMemberTagDescriptors) - 1U), /* TagCount —— 减 1 排除哨兵 */ \
	static_cast<uint16>(UE_ARRAY_COUNT(sReplicationStateDescriptorMemberReferenceDescriptors) - 1U), /* ObjectReferenceCount —— 减 1 排除哨兵 */ \
	static_cast<uint16>(0U), /* RepIndexCount —— 手写状态无 RepIndex 映射 */ \
	static_cast<uint16>(UE::Net::Private::GetMemberChangeMaskSize(sReplicationStateChangeMaskDescriptors)), /* ChangeMaskBitCount */ \
	offsetof(StateName, ChangeMask), /* ChangeMasksExternalOffset：状态结构体内部 ChangeMask 数组的起始偏移 */ \
	[](){return UE::Net::FReplicationStateIdentifier({ CityHash64(#StateName, strlen(#StateName))});}(), /* CityHash64 稳定 ID（基于状态类名字符串） */ \
	Construct##StateName, \
	Destruct##StateName, \
	static_cast<UE::Net::CreateAndRegisterReplicationFragmentFunc>(nullptr), \
	Traits, \
	{}, /* RefCount —— atomic int 默认 0 */ \
};

// 默认 Traits 为 None 的便捷版本。
#define IRIS_IMPLEMENT_REPLICATIONSTATEDESCRIPTOR(StateName) IRIS_IMPLEMENT_REPLICATIONSTATEDESCRIPTOR_WITH_TRAITS(StateName, UE::Net::EReplicationStateTraits::None)
