// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// NetObjectConnectionFilter.h —— "按连接 ID 过滤"的动态过滤器。
// ---------------------------------------------------------------------------------------------------------------------
// 与 UReplicationSystem::SetConnectionFilter 内置静态通道（ConnectionFilterHandle 流程）的差异：
//   - 静态通道：由 FReplicationFiltering::UpdateOwnerFiltering 一次性生效，但被 dependent object 关系覆盖时不可控；
//   - 本 Filter（动态）：作为 UNetObjectFilter 派生，纳入"动态过滤"阶段，dependent object 同样可以覆盖它（仅限
//     dynamic 阶段才支持），更适合"对象既可能是某连接的依赖、又需 per-connection 黑白名单"的复杂用例。
// 实现思路：
//   - per-object 在 FFilteringInfo.Data[0] 存"本地小索引"（uint16），LocalToNetRefIndex 反查；
//   - per-connection 维护一份 FNetBitArray ReplicationEnabledObjects，按 LocalIndex 取位，决定本连接是否复制；
//   - PreFilter 处理被移除对象的索引回收。
// .cpp 实现位于 Private/Iris/ReplicationSystem/Prioritization/NetObjectConnectionFilter.cpp（与命名空间分布略有
// 历史遗留差异，并未影响功能）。
// =====================================================================================================================

#pragma once

#include "Containers/ContainersFwd.h"
#include "Iris/ReplicationSystem/Filtering/NetObjectFilter.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "Net/Core/NetBitArray.h"
#include "UObject/StrongObjectPtr.h"
#include "NetObjectConnectionFilter.generated.h"

namespace UE::Net::Private
{
	class FNetRefHandleManager;
}

/** ConnectionFilter 配置：包含本地索引上限。 */
UCLASS(transient, MinimalAPI)
class UNetObjectConnectionFilterConfig : public UNetObjectFilterConfig
{
	GENERATED_BODY()

public:
	/** The maximum amount of objects that may be added to the filter. It's not designed to handle massive amounts- static connection filtering via the ReplicationSystem API is preferred. */
	/**
	 * 最大可加入对象数。本 Filter 不为大规模设计——若需百万对象级别 per-connection 过滤，请优先使用 RS 的静态
	 * SetConnectionFilter API。本上限决定 LocalToNetRefIndex 与每个 PerConnectionInfo 位图大小。
	 */
	UPROPERTY(Config)
	uint16 MaxObjectCount = 4096;
};

/**
 * The NetObjectConnectionFilter is a dynamic pre-poll filter that supports per connection filtering. It should be seen as an alternative to the ReplicationSystem SetConnectionFilter API for use cases where
 * for example the object in question can be a dependent object. Dependent objects will override dynamic filtering and only dynamic filtering.
 */
/**
 * UNetObjectConnectionFilter —— 动态 per-connection 过滤器。"动态"意味着它的判定结果可被 dependent object 关系覆盖，
 * 这是相对于 RS 静态 SetConnectionFilter 的关键差异。
 */
UCLASS(transient, MinimalAPI) 
class UNetObjectConnectionFilter : public UNetObjectFilter
{
	GENERATED_BODY()

public:
	/**
	 * 主公开 API：设置某 NetObject 对某连接的 Allow/Disallow。
	 * 内部把 NetRefHandle 解为 InternalNetRefIndex → LocalIndex，再写入对应连接的 ReplicationEnabledObjects 位图。
	 */
	IRISCORE_API void SetReplicateToConnection(UE::Net::FNetRefHandle RefHandle, uint32 ConnectionId, UE::Net::ENetFilterStatus FilterStatus);

protected:
	/**
	 * 私有 FilteringInfo 结构：复用 FNetObjectFilteringInfo 的 4×uint16 槽位。
	 * 此 Filter 只用 Data[0] 存"本地小索引"（占满 16-bit 空间，即 65535 上限；超过 MaxObjectCount 时由调度器处理）。
	 */
	struct FFilteringInfo : public FNetObjectFilteringInfo
	{
		void SetLocalObjectIndex(uint16 Index) { Data[0] = Index; }
		uint16 GetLocalObjectIndex() const { return Data[0]; }
	};

	/** per-connection 数据：一个 FNetBitArray 表示该连接对哪些 LocalIndex 是 Allow 的。 */
	struct FPerConnectionInfo
	{
		UE::Net::FNetBitArray ReplicationEnabledObjects;
	};

	// UNetObjectFilter interface
	IRISCORE_API virtual void OnInit(const FNetObjectFilterInitParams&) override;
	IRISCORE_API virtual void OnDeinit() override;
	virtual void OnMaxInternalNetRefIndexIncreased(uint32 NewMaxInternalIndex) override {}
	IRISCORE_API virtual void AddConnection(uint32 ConnectionId) override;
	IRISCORE_API virtual void RemoveConnection(uint32 ConnectionId) override;
	IRISCORE_API virtual bool AddObject(uint32 ObjectIndex, FNetObjectFilterAddObjectParams&) override;
	IRISCORE_API virtual void RemoveObject(uint32 ObjectIndex, const FNetObjectFilteringInfo&) override;
	IRISCORE_API virtual void PreFilter(FNetObjectPreFilteringParams&) override;
	IRISCORE_API virtual void Filter(FNetObjectFilteringParams&) override;

	/** 强引用持有 Config（跨 GC）。 */
	TStrongObjectPtr<UNetObjectConnectionFilterConfig> Config;
	/** LocalIndex → InternalNetRefIndex 映射（紧凑数组，UsedLocalInfoIndices 表示哪些槽位有效）。 */
	TArray<uint32> LocalToNetRefIndex;
	/** 每个连接的允许位图。 */
	TArray<FPerConnectionInfo> PerConnectionInfos;
	/** LocalIndex 槽位占用位图（用于查找下一个空闲槽）。 */
	UE::Net::FNetBitArray UsedLocalInfoIndices;

	/** 缓存当前对象索引上限（OnMaxInternalNetRefIndexIncreased 时同步更新）。 */
	uint32 MaxInternalIndex = 0;
	/** 上一帧是否有对象被移除。用于 PreFilter 中决定是否要做整理。 */
	bool bObjectRemoved = false;
};
