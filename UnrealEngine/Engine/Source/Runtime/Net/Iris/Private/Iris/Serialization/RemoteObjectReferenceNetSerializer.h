// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// RemoteObjectReferenceNetSerializer.h —— FRemoteObjectReference 序列化器
// -----------------------------------------------------------------------------
// `FRemoteObjectReference` 是分布式对象系统中的"完整远端对象引用"：包含
//   • RemoteId（FRemoteObjectId） —— 64 位全局对象 ID
//   • SharingServerId（FRemoteServerId） —— 哪台 server 当前持有/共享该对象
//   • PathName（FRemoteObjectPathName） —— 路径名（用于在缺失对象时按路径查找
//     /创建本地代理）
//
// 与 `FNetObjectReference` 的关系：
//   • `FNetObjectReference` 是 Iris 内部"轻量句柄"形态（FNetRefHandle 居多），
//     依赖 `FObjectReferenceCache` + PackageMap 解析；
//   • `FRemoteObjectReference` 是 distributed authority 模式下的"重型引用"，
//     可独立穿越多台 server。当
//     `InternalNetSerializationContext::bSerializeObjectReferencesAsRemoteIds`
//     置位（由 ReplicationSystem::IsUsingRemoteObjectReferences 决定）时，
//     普通 TObjectPtr / TWeakObjectPtr 属性也会按本类型路径序列化。
//
// 仅在 UE_WITH_REMOTE_OBJECT_HANDLE 编译条件下有意义；在常规单 server 项目中
// 不会被激活，但代码本身始终编译进 IrisCore（行为受运行期标志控制）。
// =============================================================================

#pragma once

#include "Iris/Serialization/InternalNetSerializer.h"
#include "RemoteObjectReferenceNetSerializer.generated.h"

USTRUCT()
struct FRemoteObjectReferenceNetSerializerConfig : public FNetSerializerConfig
{
	GENERATED_BODY()
};

namespace UE::Net
{
	// 仅 Iris 内部使用的 Serializer 声明
	UE_NET_DECLARE_SERIALIZER_INTERNAL(FRemoteObjectReferenceNetSerializer, IRISCORE_API);
}
