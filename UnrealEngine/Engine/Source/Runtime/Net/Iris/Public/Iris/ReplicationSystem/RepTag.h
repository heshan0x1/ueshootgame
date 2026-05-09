// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================
// RepTag.h —— Replicated 状态成员的语义 Tag
// -------------------------------------------------------------------------------------------------------------
// 模块定位：FRepTag 是一个 64-bit 字符串哈希，用来给"复制状态中的具体成员"打一个跨语言/跨平台的稳定标签。
//
// 主要用途：
//   ① WorldLocations 系统从 Protocol 中按 Tag 找到对象的位置成员（RepTag_WorldLocation），
//      避免硬编码"哪个 Property 是位置"——任何 Class 只要给位置成员打上 WorldLocation tag，就会自动被空间过滤器使用。
//   ② NetRole / NetRemoteRole：Iris 在反量化时翻转 Role / RemoteRole（旧引擎 Role/RemoteRole 黑魔法的等价实现）。
//   ③ CullDistanceSqr：FObjectReplicationBridge / NetObjectGridFilter 自动获取每对象的 cull distance。
//
// 编译期生成：UHT 通过 ReplicatedUsing= meta + 自动 RepTag 的代码生成机制为标记成员创建 UE_REPTAG_<TAGNAME>。
//
// 与 FReplicationStateMemberTagDescriptor 的关系：
//   - Descriptor 中存放 (Tag, MemberIndex, InnerTagIndex)；
//   - InnerTagIndex 用于嵌套 Struct 的 tag 查找：递归走入子 Struct 的 StateDescriptor。
// =============================================================================================================

#pragma once

#include "CoreTypes.h"

// Forward declarations
struct FNetSerializerConfig;
namespace UE::Net
{
	struct FNetSerializer;
	struct FReplicationProtocol;
	struct FReplicationStateDescriptor;
}

namespace UE::Net
{

// Types
// 64-bit Tag 哈希；由 MakeRepTag 生成，使用 CityHash64WithSeed 保证字符串 → 标签稳定。
typedef uint64 FRepTag;

// Generated code for tags will define UE_REPTAG_<TAGNAME>.
// We need some tags for internal usage so we define them ourselves, compatible with generated code of course.
// 以下几个 Tag 由于 IrisCore 自身需要使用，因此手工硬编码（与 UHT 生成机制完全兼容）。

// This tag was generated using MakeRepTag("WorldLocation")
// 标记"对象世界位置"的成员；由 WorldLocations / 空间过滤器自动读取。
#ifndef UE_REPTAG_WORLDLOCATION
#define UE_REPTAG_WORLDLOCATION
constexpr FRepTag RepTag_WorldLocation = 0x0719E9E9E02F8B16ULL;
#endif

// This tag was generated using MakeRepTag("NetRole")
// 标记"本地角色"成员（Role）；接收侧反量化时与 NetRemoteRole 对调以便正确呈现 ROLE_Authority / ROLE_AutonomousProxy 等。
#ifndef UE_REPTAG_NETROLE
#define UE_REPTAG_NETROLE
constexpr FRepTag RepTag_NetRole = 0xFFAAB417B1123942ULL;
#endif

// This tag was generated using MakeRepTag("NetRemoteRole")
// 标记"远端角色"成员（RemoteRole），与 NetRole 配合做角色反转。
#ifndef UE_REPTAG_NETREMOTEROLE
#define UE_REPTAG_NETREMOTEROLE
constexpr FRepTag RepTag_NetRemoteRole = 0xF754C2703924C7AAULL;
#endif

// This tag was generated using MakeRepTag("CullDistanceSqr")
// 标记"剔除距离平方"成员；NetObjectGridFilter / SphereNetObjectPrioritizer 自动读取。
#ifndef UE_REPTAG_CULLDISTANCESQR
#define UE_REPTAG_CULLDISTANCESQR
constexpr FRepTag RepTag_CullDistanceSqr = 0x6BB13A5C1A655157ULL;
#endif

/**
 * The invalid RepTag can be used for purposes where one wants to know if there's a valid tag or not.
 * It cannot be a constant like RepTag_Invalid as that would require us to prevent a tag from being
 * called Invalid or cause mismatching values if someone calls a tag Invalid. So it's implemented
 * as a function instead.
 *
 * 中文：无效 Tag 用 0 表示；之所以不定义 const RepTag_Invalid，是因为名字冲突会破坏 hash 唯一性。
 */
constexpr FRepTag GetInvalidRepTag() { return FRepTag(0); }

// 把字符串转换为 FRepTag（CityHash64WithSeed），同名字符串生成稳定哈希。
IRISCORE_API FRepTag MakeRepTag(const char* TagName);

// FindRepTag 返回的命中信息：
//   - StateIndex：Protocol 内的 StateDescriptor 索引（=0 表示在单个 Descriptor 内查询）。
//   - ExternalStateOffset：成员在外部状态 buffer（UObject 内属性地址空间）的相对偏移。
//   - InternalStateAbsoluteOffset：成员在 Iris 内部 quantized buffer 的"全协议绝对偏移"——
//     可直接配合 ReplicationInstanceProtocol 的内部 buffer 使用。
//   - Serializer / SerializerConfig：成员的 NetSerializer + 配置（用于直接 Read/Write）。
struct FRepTagFindInfo
{
	/** If the tag is found in a protocol this indicates which state the tag was found in, otherwise it will be zero. */
	uint32 StateIndex;
	/** This is the offset into the state indicated by the StateIndex. Absolute offsets cannot beused as each external state may have its own state buffer. */
	uint32 ExternalStateOffset;
	/** The absolute offset into the internal state. One can ignore the StateIndex as the internal state is a single linear allocation for all states. */
	uint32 InternalStateAbsoluteOffset;
	const FNetSerializer* Serializer;
	const FNetSerializerConfig* SerializerConfig;
};

/** Returns true if the RepTag exists */
// 仅判断 Tag 是否存在（不返回偏移），比 FindRepTag 更便宜。
IRISCORE_API bool HasRepTag(const FReplicationProtocol* Protocol, FRepTag RepTag);

/**
  * Finds the first RepTag in the protocol that matches the supplied RepTag.
  * The offsets written to OutRepTagStateInfo is from the start of a full object state buffer.
  * Returns true if the tag was found, false if not. OutRepTagStateInfo is only valid if the tag is found.
  */
// Protocol 视角：在多个 StateDescriptor 中按顺序查找；命中时把 InternalStateAbsoluteOffset 调整到全协议偏移。
IRISCORE_API bool FindRepTag(const FReplicationProtocol* Protocol, FRepTag RepTag, FRepTagFindInfo& OutRepTagFindInfo);

/**
  * Finds the first RepTag in the replication state that matches the supplied RepTag.
  * The offsets written to OutRepTagStateInfo is from the start of a replication state buffer.
  * Returns true if the tag was found, false if not. OutRepTagStateInfo is only valid if the tag is found.
  */
// StateDescriptor 视角：在单个状态 buffer 内查找；如遇嵌套 Struct，递归进入子 StateDescriptor 累加 InnerTagIndex。
IRISCORE_API bool FindRepTag(const FReplicationStateDescriptor* Descriptor, FRepTag RepTag, FRepTagFindInfo& OutRepTagFindInfo);

}
