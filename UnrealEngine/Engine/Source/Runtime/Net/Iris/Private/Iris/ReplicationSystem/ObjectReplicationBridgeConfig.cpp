// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：ObjectReplicationBridgeConfig.cpp
// 角色：UObjectReplicationBridgeConfig 的 GetConfig + 各 GetXxxConfigs 的薄实现。
//       配置内容由 UE 的反射 + ini 自动加载（config=Engine），运行期只读。
//
// 单例：GetDefault<UObjectReplicationBridgeConfig>() —— 永远复用 CDO，避免反复构造。
// =====================================================================================

#include "Iris/ReplicationSystem/ObjectReplicationBridgeConfig.h"
#include "Containers/ArrayView.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ObjectReplicationBridgeConfig)

UObjectReplicationBridgeConfig::UObjectReplicationBridgeConfig()
: Super()
{
}

// 取 CDO 作为单例：UE 的 ini 加载机制会把 [/Script/IrisCore.ObjectReplicationBridgeConfig] 写入 CDO 字段。
const UObjectReplicationBridgeConfig* UObjectReplicationBridgeConfig::GetConfig()
{
	return GetDefault<UObjectReplicationBridgeConfig>();
}

// 以下 7 个 Get 函数仅返回内部 TArray 的只读视图，调用方按类查找匹配项。
TConstArrayView<FObjectReplicationBridgePollConfig> UObjectReplicationBridgeConfig::GetPollConfigs() const
{
	return MakeArrayView(PollConfigs);
}

TConstArrayView<FObjectReplicationBridgeFilterConfig> UObjectReplicationBridgeConfig::GetFilterConfigs() const
{
	return MakeArrayView(FilterConfigs);
}

TConstArrayView<FObjectReplicationBridgePrioritizerConfig> UObjectReplicationBridgeConfig::GetPrioritizerConfigs() const
{
	return MakeArrayView(PrioritizerConfigs);
}

TConstArrayView<FObjectReplicationBridgeDeltaCompressionConfig> UObjectReplicationBridgeConfig::GetDeltaCompressionConfigs() const
{
	return MakeArrayView(DeltaCompressionConfigs);
}

TConstArrayView<FObjectReplicatedBridgeCriticalClassConfig> UObjectReplicationBridgeConfig::GetCriticalClassConfigs() const
{
	return MakeArrayView(CriticalClassConfigs);
}

TConstArrayView<FObjectReplicationBridgeTypeStatsConfig> UObjectReplicationBridgeConfig::GetTypeStatsConfigs() const
{
	return MakeArrayView(TypeStatsConfigs);
}

TConstArrayView<FObjectReplicationBridgeAsyncLoadingClassConfig> UObjectReplicationBridgeConfig::GetAsyncLoadingClassConfigs() const
{
	return MakeArrayView(AsyncLoadingClassConfigs);
}
