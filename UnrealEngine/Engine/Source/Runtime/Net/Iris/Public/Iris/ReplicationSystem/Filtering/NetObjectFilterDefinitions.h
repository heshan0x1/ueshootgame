// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// NetObjectFilterDefinitions.h —— 通过 ini 注册"动态过滤器"的清单（类似 NetObjectPrioritizerDefinitions）。
// ---------------------------------------------------------------------------------------------------------------------
// 工作方式：
//   - 配置节：[/Script/IrisCore.NetObjectFilterDefinitions]，例如：
//       +NetObjectFilterDefinitions=(FilterName="Spatial",
//                                    ClassName="/Script/IrisCore.NetObjectGridWorldLocFilter",
//                                    ConfigClassName="/Script/IrisCore.NetObjectGridFilterConfig")
//   - FReplicationFiltering::InitFilters() 在启动期遍历全部定义，按 ClassName 反射创建 UNetObjectFilter 实例，
//     按 ConfigClassName 取 CDO 实例传入 FNetObjectFilterInitParams::Config，分配 FNetObjectFilterHandle，
//     建立 (FilterName → Handle) 映射；之后游戏侧通过 UReplicationSystem::SetFilter 按名称把对象绑定到 Filter。
//   - 同一 ClassName 可对应多个 FilterName（用于参数差异），FilterName 必须唯一。
// =====================================================================================================================

#pragma once

#include "Containers/ContainersFwd.h"
#include "UObject/ObjectMacros.h"
#include "NetObjectFilterDefinitions.generated.h"

/**
 * 单条 Filter 定义记录：表征"用 ConfigClassName 配置 ClassName 的实例，并以 FilterName 注册"。
 */
USTRUCT()
struct FNetObjectFilterDefinition
{
	GENERATED_BODY()

	/** Filter identifier. Used to uniquely identify a filter. */
	/** Filter 唯一标识名（也是 SetFilter / GetFilterHandle 的 key）。 */
	UPROPERTY()
	FName FilterName;

	/**
	 * UClass name, specified by its fully qualified path, used to create the UNetObjectFilter. You can have multiple instances
	 * of the same filter as long as their FilterNames are unique.
	 */
	/**
	 * UNetObjectFilter 派生类全路径名（如 "/Script/IrisCore.NetObjectGridWorldLocFilter"）。
	 * 可以为同一个类创建多份（例如不同空间分辨率的 Grid），只要 FilterName 不重复即可。
	 */
	UPROPERTY()
	FName ClassName;		

	/**
	 * Optional UClass name, specified by its fully qualified path, used to create the UNetObjectFilterConfig. The class default instance
	 * will be passed at filter initialization time. If you want multiple instances of the same
	 * filter then use subclassing to create unique filter configs.
	 */
	/**
	 * 可选：UNetObjectFilterConfig 派生类全路径名。Filter 初始化时会拿其 CDO 作为 Config 传入；
	 * 若要为"同一 Filter 类"做差异化配置，请通过派生子类（每子类一份 ConfigClassName）实现。
	 */
	UPROPERTY()
	FName ConfigClassName;		
};

/** Configurable filter definitions. Valid filter definitions are auto-created by the filter system. */
/**
 * UNetObjectFilterDefinitions —— 由 ini 驱动的全局 Filter 清单容器（CDO 单例）。
 * FReplicationFiltering 在 Init 阶段读取本类型 CDO 的 NetObjectFilterDefinitions，逐项实例化与注册。
 * 非法定义（ClassName 不继承自 UNetObjectFilter 等）会被 InitFilters 跳过并打日志。
 */
UCLASS(transient, config=Engine)
class UNetObjectFilterDefinitions final : public UObject
{
	GENERATED_BODY()

public:
	/** Returns the filter definitions exactly as configured. May contain invalid definitions, such as ClassNames not inheriting from UNetObjectFilter. */
	/** 原样返回 ini 中的全部 Filter 定义；调用方负责自行校验（如 ClassName 是否合法）。 */
	IRISCORE_API TConstArrayView<FNetObjectFilterDefinition> GetFilterDefinitions() const;

private:
	/** ini 中读取得到的 Filter 定义数组（Engine.ini → [/Script/IrisCore.NetObjectFilterDefinitions]）。 */
	UPROPERTY(Config)
	TArray<FNetObjectFilterDefinition> NetObjectFilterDefinitions;
};
