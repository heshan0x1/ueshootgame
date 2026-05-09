// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// ReplicationStateDescriptorConfig.h —— Iris Descriptor 烘焙的 ini 配置
// -------------------------------------------------------------------------------------
// 【UReplicationStateDescriptorConfig】config=Engine 的全局配置：
//   1) SupportsStructNetSerializerList：白名单/黑名单——
//      允许"看起来像有自定义 NetSerialize 但实际上仍可用 StructNetSerializer 处理"
//      的 struct 不触发 Iris 的警告。例如 FVector、FRotator 等数学类型在旧网络栈里
//      实现了 NetSerialize（量化），Iris 这边有更优的 SerializerInfo 版本。
//   2) EnsureFullyPushModelClassNames：强制 push-model 检查的指定类名。
//      Builder 在烘焙这些类的 Descriptor 时会 ensure！HasPushBasedDirtiness。
//   3) bEnsureAllClassesAreFullyPushModel：全局开关，对所有类启用 push-model 检查。
//
// 通过 DefaultEngine.ini 的 [/Script/IrisCore.ReplicationStateDescriptorConfig] 段配置。
// =====================================================================================

#pragma once

#include "Containers/ContainersFwd.h"
#include "UObject/ObjectMacros.h"
#include "ReplicationStateDescriptorConfig.generated.h"

// 【FReplicationStateDescriptorClassPushModelConfig】单个"必须是 push-model"的类名条目。
// 仅记录 short class name（如 "PlayerController"），不带模块/包前缀。
USTRUCT()
struct FReplicationStateDescriptorClassPushModelConfig
{
	GENERATED_BODY()
	
	/** Short class name, e.g. PlayerController. */
	UPROPERTY()
	FName ClassName;
};

// 【FSupportsStructNetSerializerConfig】白名单条目：声明某 struct 即使有
// NetSerialize/NetDeltaSerialize 方法，仍可使用默认的 StructNetSerializer 处理。
//   - bCanUseStructNetSerializer = true：允许（Iris 不必另行处理）；
//   - bCanUseStructNetSerializer = false：明确禁止用默认 NetSerializer（罕见场景）。
USTRUCT()
struct FSupportsStructNetSerializerConfig
{
	GENERATED_BODY()

	/** Struct name. */
	UPROPERTY()
	FName StructName;

	/** If the named struct works with the default Iris StructNetSerializer. */
	UPROPERTY()
	bool bCanUseStructNetSerializer = true;
};

// 【UReplicationStateDescriptorConfig】配置 UObject 单例。GetDefault<...> 取默认实例。
// transient + config=Engine：从 DefaultEngine.ini 读取，运行期不持久化。
UCLASS(transient, config=Engine)
class UReplicationStateDescriptorConfig : public UObject
{
	GENERATED_BODY()

public:
	// 取出"允许走 StructNetSerializer"白名单。Builder 在 CanStructUseStructNetSerializer 时查询。
	IRISCORE_API TConstArrayView<FSupportsStructNetSerializerConfig> GetSupportsStructNetSerializerList() const;
	
	// 取出"必须 push-model"类名列表。Builder 在 ShouldValidateIsFullyPushModel 时查询。
	IRISCORE_API TConstArrayView<FReplicationStateDescriptorClassPushModelConfig> GetEnsureFullyPushModelClassNames() const;

	// 全局开关：对所有类启用 push-model 检查。
	inline bool EnsureAllClassesAreFullyPushModel() const;

protected:
	UReplicationStateDescriptorConfig();

private:
	/**
	 * Structs that works using the default struct NetSerializer when running iris replication even though they implement a custom NetSerialize or NetDeltaSerialize method.
	 *
	 * 默认情况下若 struct 有 NetSerialize/NetDeltaSerialize 但没注册自定义 Iris NetSerializer，
	 * Builder 会发警告。把这些 struct 加入本白名单后将不再警告。
	 */
	UPROPERTY(Config)
	TArray<FSupportsStructNetSerializerConfig> SupportsStructNetSerializerList;

	/** Which classes should ensure they are fully push model. */
	UPROPERTY(Config)
	TArray<FReplicationStateDescriptorClassPushModelConfig> EnsureFullyPushModelClassNames;

	/** If you want to be alerted of all classes not being fully push model. */
	UPROPERTY(Config)
	bool bEnsureAllClassesAreFullyPushModel = false;
};

bool UReplicationStateDescriptorConfig::EnsureAllClassesAreFullyPushModel() const
{
	return bEnsureAllClassesAreFullyPushModel;
}
