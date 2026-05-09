// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// RemoteServerIdNetSerializer.h —— FRemoteServerId 序列化器（远端服务器 ID）
// -----------------------------------------------------------------------------
// `FRemoteServerId` 是分布式 server 集群中每台 server 的全局编号，是
// `FRemoteObjectId` 的高位字段。位宽常量 `REMOTE_OBJECT_SERVER_ID_BIT_SIZE`
// 定义于 UObject/RemoteObjectTypes.h，决定 server 集群规模上限。
//
// 与本地的关系：
//   • 本机 server 的 ServerId 在内存里通常表示为
//     `ERemoteServerIdConstants::Local` 哨兵，只在 wire / 跨 server 引用
//     时通过 GetGlobalized() 替换为真实数字 ID。
//   • 受 UE_WITH_REMOTE_OBJECT_HANDLE 编译开关控制（详见 Iris 文档）。
//
// 仅在 Iris 内部使用（UE_NET_DECLARE_SERIALIZER_INTERNAL），用作
// FRemoteObjectReferenceNetSerializer 的子组件。
// =============================================================================

#pragma once

#include "Iris/Serialization/InternalNetSerializer.h"
#include "RemoteServerIdNetSerializer.generated.h"

USTRUCT()
struct FRemoteServerIdNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{
	// 仅 Iris 内部使用的 Serializer 声明
	UE_NET_DECLARE_SERIALIZER_INTERNAL(FRemoteServerIdNetSerializer, IRISCORE_API);
}
