// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// ReplicationFilteringConfig.h —— Filtering 子模块的全局配置（ini，节 [/Script/IrisCore.ReplicationFilteringConfig]）。
// ---------------------------------------------------------------------------------------------------------------------
// 关注点：Object Scope Hysteresis（对象 Scope 滞后机制）。
//   - 动机：当对象在 in-scope ↔ out-of-scope 边缘抖动（例如恰在 cull 距离上来回），频繁地 EndReplication →
//     再 BeginReplication 会触发昂贵的 Create/Destroy 与基线重建。Hysteresis 让"被动态过滤掉"的对象在 N 帧内
//     仍保留在 Scope 中，给空间过滤一段缓冲，避免抖动。
//   - 配置项：
//       * bEnableObjectScopeHysteresis            —— 总开关。
//       * DefaultHysteresisFrameCount             —— 默认滞后帧数（仅在没匹配 Profile 时生效）。
//       * HysteresisUpdateConnectionThrottling    —— 每帧只更新 1/N 连接的 hysteresis（限频，clamp 到 [1,128]）。
//       * HysteresisProfiles                      —— 名称到帧数的特化映射（如 PawnFilterProfile=30）。
//   - 关联：FObjectScopeHysteresisUpdater（实现）+ FReplicationFiltering::PreUpdateObjectScopeHysteresis /
//     PostUpdateObjectScopeHysteresis（调度）。Profile 名通过 ObjectReplicationBridgeConfig 的 FilterConfig 引用。
// =====================================================================================================================

#pragma once

#include "UObject/ObjectMacros.h"
#include "Math/UnrealMathUtility.h"
#include "ReplicationFilteringConfig.generated.h"

/**
 * 单条 Hysteresis Profile：将一个名称映射到具体的滞后帧数。
 * 在 ObjectReplicationBridgeConfig 的 FilterConfigs 中通过 FilterProfile 字段引用。
 */
USTRUCT()
struct FObjectScopeHysteresisProfile
{
	GENERATED_BODY()

public:
	/** 便捷比较（直接以 FName 作为 key 在 TArray 中查找）。 */
	bool operator==(FName Name) const
	{
		return FilterProfileName == Name;
	}

	/** The config name used to map to this profile */
	/** Profile 唯一标识名（与 FilterConfig 中的 FilterProfile 字段对照查找）。 */
	UPROPERTY()
	FName FilterProfileName;

	/** The number of frames to keep the object in scope after it has been filtered out by dynamic filtering. */
	/** 对象被动态过滤掉后，仍保留在 Scope 中的帧数。8 位（0-255）。 */
	UPROPERTY()
	uint8 HysteresisFrameCount = 0;
};

/**
 * Object scope hysteresis support. Keep dynamically filtered out objects around for a specified amount of frames. 
 * Configure behavior via hysteresis profiles that determine the frame timeout per class.
 * The filter config for a specific class can then mention the hysteresis profile in order to get the appropriate behavior. 
 *
 * Example:
 * [/Script/IrisCore.ReplicationFilteringConfig]
 * bEnableObjectScopeHysteresis=true
 * DefaultHysteresisFrameCount=4
 * HysteresisUpdateConnectionThrottling=4
 * !HysteresisProfiles=ClearArray
 * +FilterProfiles=(FilterProfileName=PawnFilterProfile, HysteresisFrameCount=30)
 * 
 * [/Script/ IrisCore.ObjectReplicationBridgeConfig]
 * +FilterConfigs=(ClassName=/Script/Engine.Pawn, DynamicFilterName=Spatial, FilterProfile=PawnFilterProfile)
 */
UCLASS(transient, config = Engine)
class UReplicationFilteringConfig final : public UObject
{
	GENERATED_BODY()

public:
	bool IsObjectScopeHysteresisEnabled() const;
	uint8 GetDefaultHysteresisFrameCount() const;
	uint8 GetHysteresisUpdateConnectionThrottling() const;

	const TArray<FObjectScopeHysteresisProfile>& GetHysteresisProfiles() const;

private:
	UPROPERTY(Config)
	/** If enabled a dynamically filtered out object will not be considered out of scope for a particular number of frames. */
	/** 总开关：是否启用对象 Scope 滞后。关闭则被动态过滤掉的对象立即出 scope。 */
	bool bEnableObjectScopeHysteresis = true;

	UPROPERTY(Config)
	/** How many frames a dynamically filtered out object should still be considered in scope by default. Can be overridden with HysteresisClassConfigs. */ 
	/** 默认滞后帧数（找不到 Profile 时使用）。 */
	uint8 DefaultHysteresisFrameCount = 0;

	UPROPERTY(Config)
	/**
	 * Update every Nth connection each frame. If 1 then every connection will be updated every frame, if 2 then half of the connections will be updated per frame and so on.
	 * Keep this number low. The value will be clamped to 128. Due to the nature of the throttling objects may linger for N-1 extra frames before considered out of scope.
	 */
	/**
	 * 连接更新限频步长 N：每帧仅更新 1/N 的连接的 hysteresis（其它连接顺延）。
	 * 取值越大单帧开销越小，但对象可能多停留 N-1 帧才被剔除。运行时 clamp 到 [1, 128]。
	 */
	uint8 HysteresisUpdateConnectionThrottling = 1;

	/** Specialized configuration profiles */
	/** 各特化 Profile 列表（按 FilterProfileName 查找）。 */
	UPROPERTY(Config)
	TArray<FObjectScopeHysteresisProfile> HysteresisProfiles;
};

/** 内联 getter：是否启用 Hysteresis 总开关。 */
inline bool UReplicationFilteringConfig::IsObjectScopeHysteresisEnabled() const
{
	return bEnableObjectScopeHysteresis;
}

/** 内联 getter：默认滞后帧数。 */
inline uint8 UReplicationFilteringConfig::GetDefaultHysteresisFrameCount() const
{
	return DefaultHysteresisFrameCount;
}

/** 内联 getter：连接更新限频步长（运行时 clamp 到 [1,128]）。 */
inline uint8 UReplicationFilteringConfig::GetHysteresisUpdateConnectionThrottling() const
{
	return FMath::Clamp<uint8>(HysteresisUpdateConnectionThrottling, 1U, 128U);
}

/** 内联 getter：返回特化 Profile 列表（按 FilterProfileName 名称查找）。 */
inline const TArray<FObjectScopeHysteresisProfile>& UReplicationFilteringConfig::GetHysteresisProfiles() const
{
	return HysteresisProfiles;
}
