// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// ReplicationStateDescriptorConfig.cpp —— 简单 ini 配置访问器实现
// -------------------------------------------------------------------------------------
// 内容仅有：默认构造 + 两个数组的视图访问器。所有"配置内容"由 UnrealHeaderTool 通过
// UE_INLINE_GENERATED_CPP_BY_NAME 自动生成的反射代码读取 DefaultEngine.ini。
// =====================================================================================

#include "Iris/ReplicationState/ReplicationStateDescriptorConfig.h"
#include "Containers/ArrayView.h"

// 引入 UHT 生成的 .gen.cpp，包含 UClass 注册与 UPROPERTY 反射注入。
#include UE_INLINE_GENERATED_CPP_BY_NAME(ReplicationStateDescriptorConfig)

UReplicationStateDescriptorConfig::UReplicationStateDescriptorConfig()
: Super()
{
}

// 返回"允许走默认 StructNetSerializer"的 struct 白名单视图（不拷贝）。
TConstArrayView<FSupportsStructNetSerializerConfig> UReplicationStateDescriptorConfig::GetSupportsStructNetSerializerList() const
{
	return MakeArrayView(SupportsStructNetSerializerList);
}

// 返回"必须 push-model"类名列表视图。
TConstArrayView<FReplicationStateDescriptorClassPushModelConfig> UReplicationStateDescriptorConfig::GetEnsureFullyPushModelClassNames() const
{
	return MakeArrayView(EnsureFullyPushModelClassNames);
}
