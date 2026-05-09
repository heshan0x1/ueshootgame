// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：Prioritizer 的 ini 注册表（与 UNetObjectFilterDefinitions/UNetBlobHandlerDefinitions 同形式）。
// 配置节：DefaultEngine.ini → [/Script/IrisCore.NetObjectPrioritizerDefinitions]
//   +NetObjectPrioritizerDefinitions=(PrioritizerName="DefaultSpatial",
//                                     ClassName="/Script/IrisCore.SphereWithOwnerBoostNetObjectPrioritizer",
//                                     ConfigClassName="/Script/IrisCore.SphereWithOwnerBoostNetObjectPrioritizerConfig")
// 启动加载流程（FReplicationPrioritization::InitPrioritizers）：
//   1) NewObject<UNetObjectPrioritizerDefinitions>() 触发 PostInitProperties → LoadDefinitions → StaticLoadClass；
//   2) GetValidDefinitions 过滤掉 Class 加载失败 / ConfigClass 失败的条目；
//   3) 顺序遍历，逐个 NewObject(prioritizer 类) + Init(InitParams)；句柄即 PrioritizerInfos 中的索引。
// 注意：数组中第一个有效条目自动成为「默认空间 prioritizer」，所有带 RepTag_WorldLocation 的对象会被自动加入。
// =============================================================================================================================

#pragma once

#include "UObject/ObjectMacros.h"
#include "NetObjectPrioritizerDefinitions.generated.h"

/** Prioritizer definition. Configurable via UNetObjectPrioritizerDefinitions. */
// Prioritizer 注册项：定义一个具体 prioritizer 实例。
// 同一个 Class 可被注册多次（PrioritizerName 必须唯一）；不同实例可使用不同的 Config 子类。
USTRUCT()
struct FNetObjectPrioritizerDefinition
{
	GENERATED_BODY()

	/** Prioritizer identifier. Used to uniquely identify a prioritizer in various APIs. */
	// 唯一标识符（FName）。用户通过名字调用 UReplicationSystem::SetPrioritizer / GetPrioritizerHandle。
	UPROPERTY()
	FName PrioritizerName;

	/**
	 * UClass name, specified by its fully qualified path, used to create the UNetObjectPrioritizer. You can have multiple instances of the same prioritizer as long as 
	 * their PrioritizerNames are unique.
	 */
	// UClass 全路径名（形如 "/Script/IrisCore.SphereNetObjectPrioritizer"）。LoadDefinitions 时通过 StaticLoadClass 解析。
	UPROPERTY()
	FName ClassName;		

	/** UClass used to create the UNetObjectPrioritizer. Filled in automatically when reading the config. */
	// 加载完成后填充的实际 UClass*。GetValidDefinitions 仅返回此字段非空的条目。
	UPROPERTY()
	TObjectPtr<UClass> Class = nullptr;

	/**
	 * Optional UClass, specified by its fully qualified path, used to create the UNetObjectPrioritizerConfig. The class default instance will be passed at prioritizer initialization.
	 * If you want multiple instances of the same prioritizer then use subclassing to create unique prioritizer configs.
	 */
	// 可选 ConfigClass 全路径名。Init 时调度器 NewObject(ConfigClass) 后传入 prioritizer。
	// 注意：Iris 实际用 NewObject 而非 CDO（见 InitPrioritizers），所以 ini 直接用同一 ConfigClass 也可。
	UPROPERTY()
	FName ConfigClassName;		

	/** UClass used to create the UNetObjectPrioritizerConfig. Filled in automatically when reading the config. */
	// 加载完成后填充的实际 ConfigClass*。
	UPROPERTY()
	TObjectPtr<UClass> ConfigClass = nullptr;
};

/** Configurable prioritizer definitions. Valid prioritizer definitions are auto-created by the prioritization system. */
// ini 配置容器（config=Engine）。Iris 启动时由 FReplicationPrioritization::InitPrioritizers 内部 NewObject 触发加载。
UCLASS(transient, config=Engine)
class UNetObjectPrioritizerDefinitions final : public UObject
{
	GENERATED_BODY()

public:
	/** Retrieve the valid prioritizer definitions- those that should be able to create valid prioritizers. */
	// 返回 Class 已加载、且 ConfigClass 要么为空要么也已加载 的有效条目。失败条目仅在日志中报错并被忽略。
	void GetValidDefinitions(TArray<FNetObjectPrioritizerDefinition>& OutDefinitions) const;

private:
	// UObject
	virtual void PostInitProperties() override;
	virtual void PostReloadConfig(FProperty* PropertyToLoad) override;

	// 内部加载：遍历 NetObjectPrioritizerDefinitions，对每条目用 StaticLoadClass 解析 Class 与 ConfigClass。
	void LoadDefinitions();

private:
	/**
	 * Prioritizer definitions.
	 * The first valid definition will assume the role as default spatial prioritizer. All objects with a RepTag_WorldLocation tag will 
	 * be added to the default prioritizer. To override the behavior a prioritizer must be set via calls to the ReplicationSystem.
	 */
	// ini 数组。第一个有效条目自动成为「默认空间 prioritizer」（句柄 = DefaultSpatialNetObjectPrioritizerHandle = 0）。
	// 含 RepTag_WorldLocation 的对象会在 NetRefHandleManager 注册时自动加入这个默认 prioritizer。
	UPROPERTY(Config)
	TArray<FNetObjectPrioritizerDefinition> NetObjectPrioritizerDefinitions;
};
