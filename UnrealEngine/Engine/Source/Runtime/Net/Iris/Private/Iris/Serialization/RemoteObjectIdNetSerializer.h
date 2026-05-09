// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// RemoteObjectIdNetSerializer.h —— FRemoteObjectId 序列化器（远端对象 ID）
// -----------------------------------------------------------------------------
// `FRemoteObjectId` 是 UE 分布式对象系统（UE_WITH_REMOTE_OBJECT_HANDLE 开关）中
// 唯一标识一个对象的 64 位 ID（高位 = ServerId，低位 = SerialNumber）。
// 它解决跨 server 引用的同一性问题：当对象在多 server 间漂移/共享时，
// FRemoteObjectId 是稳定的全局唯一标识。
//
// 关于"本地化 / 全局化"（Localize / Globalize）：
//   • 本地服务器视角下，自己的 ServerId 字段经常被存为 ERemoteServerIdConstants::Local
//     常量以避免跨 server 时混淆；序列化时必须先 GetGlobalized() 把 Local
//     映射回真实 ServerId 编号；反序列化时 GetLocalized() 把"恰好等于本机
//     ServerId"重新替换回 Local 标记。
//   • 这样确保跨 server 时网络 wire 上携带的是绝对 ID，而进程内部仍可
//     使用 Local 标记便利访问。
//
// 与 `InternalNetSerializationContext::bSerializeObjectReferencesAsRemoteIds` 的关系：
//   • 当 ReplicationSystem 配置为使用远程对象引用 (IsUsingRemoteObjectReferences)
//     时，普通的 Object/ObjectPtr/WeakObjectPtr 属性会被 FObjectNetSerializer
//     按 FRemoteObjectReference 形式序列化（通过本 Serializer 等子组件）。
// =============================================================================

#pragma once

#include "Iris/Serialization/InternalNetSerializer.h"
#include "RemoteObjectIdNetSerializer.generated.h"

/**
 * FRemoteObjectIdNetSerializer 配置（无字段）。
 * 实际行为常量来自 RemoteObjectTypes（ServerId 位宽、序号上限等）。
 */
USTRUCT()
struct FRemoteObjectIdNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{
	// 内部 Serializer：未在 Public/ 头中导出，仅供 Iris 内部 ObjectNetSerializer / 远程引用路径调用。
	UE_NET_DECLARE_SERIALIZER_INTERNAL(FRemoteObjectIdNetSerializer, IRISCORE_API);
}
