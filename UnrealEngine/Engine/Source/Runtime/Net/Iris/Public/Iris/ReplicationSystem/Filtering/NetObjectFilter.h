// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// NetObjectFilter.h —— Iris ReplicationSystem ‖ Filtering 子模块的"过滤器抽象基类"声明
// ---------------------------------------------------------------------------------------------------------------------
// 角色定位（在 Iris 中）：
//   Filtering 是 Iris 三大可插拔过滤通路之一（Exclusion Group → Dynamic UNetObjectFilter → Inclusion Group）的
//   "Dynamic"中段。本文件提供 UNetObjectFilter 抽象基类与所有外围 POD 参数结构，所有"动态过滤器"（如内置
//   AlwaysRelevant / FilterOut / Connection / Grid 与游戏侧自定义实现）均通过继承本类参与每帧 Filter() 调度。
//
// 核心契约（每帧由 FReplicationFiltering 调度）：
//   1) Init / OnInit             —— RS 启动期，给定 MaxNetObjectCount / MaxConnectionCount / 可选 Config 实例；
//   2) AddObject / RemoveObject  —— 对象选择本 Filter 时的注册/反注册；FilteringInfo 已清零，子类填私有数据；
//   3) AddConnection / RemoveConnection —— 连接生灭时分配/释放 per-connection 数据；
//   4) UpdateObjects             —— 仅当 Filter 声明 ENetFilterTraits::NeedsUpdate 才会被调用；脏对象批量通知；
//   5) PreFilter / Filter / PostFilter —— 每帧 Filter 阶段；PreFilter+PostFilter 各执行 1 次（若有连接将被过滤）；
//                                    Filter 可能针对同一 Connection 被多次调用（分批）；
//   6) MaxInternalNetRefIndexIncreased —— 当对象索引上限因运行时增长而扩容时，重新分配位图。
//
// 关键数据：
//   - FNetObjectFilterHandle (uint32) ——
//       特殊保留值：InvalidNetObjectFilterHandle(0)、ToOwnerFilterHandle(1)、ConnectionFilterHandle(2)；
//       其它值用 MSB(最高位) 区分 Static/Dynamic（详见 ReplicationFiltering.cpp::FNetObjectFilterHandleUtil）。
//   - ENetFilterStatus —— Allow / Disallow，二值结果；
//   - FNetObjectFilteringInfo —— 4×uint16=8B 的 per-object 自定义存储槽（offsets / 索引等），由各 Filter 自由编码。
//   - ENetFilterTraits —— Spatial（依赖 WorldLocation 的空间过滤）/ NeedsUpdate（需要 UpdateObjects 回调）。
// =====================================================================================================================

#pragma once

#include "Net/Core/NetBitArray.h"
#include "Iris/Core/NetChunkedArray.h"
#include "Iris/ReplicationSystem/ReplicationView.h"
#include "UObject/ObjectMacros.h"
#include "NetObjectFilter.generated.h"

struct FNetObjectFilteringInfo;
class UReplicationSystem;
namespace UE::Net
{
	// FNetObjectFilterHandle —— 32 位过滤器句柄。其编码语义见上方文件头说明。
	typedef uint32 FNetObjectFilterHandle;
	struct FReplicationInstanceProtocol;
	struct FReplicationProtocol;
	class FNetRefHandle;

	namespace Private
	{
		typedef uint32 FInternalNetRefIndex;
		class FNetRefHandleManager;
	}
}

namespace UE::Net
{

/** 无效过滤器句柄（"未设置/未指定"）。 */
constexpr FNetObjectFilterHandle InvalidNetObjectFilterHandle = FNetObjectFilterHandle(0);
/** 静态保留：将对象仅向其 OwningConnection 发送（不需要走 UNetObjectFilter，FReplicationFiltering 内置实现）。 */
constexpr FNetObjectFilterHandle ToOwnerFilterHandle = FNetObjectFilterHandle(1);
/** ConnectionFilterHandle is for internal use only. */
/** 静态保留：基于"连接位图"的过滤（白/黑名单），同样由 FReplicationFiltering 内置实现，用户不可显式 SetFilter。 */
constexpr FNetObjectFilterHandle ConnectionFilterHandle = FNetObjectFilterHandle(2);

/** Used to control whether an object is allowed to be replicated or not. */
/** 过滤器结果二值枚举：是否允许对象向某连接复制。 */
enum class ENetFilterStatus : uint32
{
	/** Do not allow replication. */
	/** 不允许（被过滤掉）。 */
	Disallow,
	/** Allow replication. */
	/** 允许（保留在该连接的 RelevantObjectsInScope 中）。 */
	Allow,
};

IRISCORE_API const TCHAR* LexToString(ENetFilterStatus Status);

} // end namespace UE::Net

/**
 * Parameters passed to UNetObjectFilter::Filter.
 */
/**
 * 传给 UNetObjectFilter::Filter() 的参数包。
 * 每"批"（同一连接可能多批）传入一组待过滤对象的索引集（FilteredObjects 位图），Filter 实现负责
 * 将允许复制的对象的对应位置位写入 OutAllowedObjects。返回时 OutAllowedObjects 由调度器进一步聚合
 * 到 per-connection 的 ObjectsInScope 中（最终再被三阶段汇总）。
 */
struct FNetObjectFilteringParams
{
	/**
	 * The contents of OutAllowedObjects is undefined when passed to Filter(). The filter is responsible
	 * for setting and clearing bits for objects that have this filter set, which is provided in the
	 * FilteredObjects member. It's safe to set or clear all bits in the bitarray as the callee will
	 * only care about bits which the filter is responsible for.
	 */
	/**
	 * 输出位图（"允许复制的对象"）。入口处内容未定义；Filter 仅需正确填写本 Filter 所拥有的对象位（其它位
	 * 调度器会忽略），可以放心 SetAllBits/ClearAllBits 简化实现（如 AlwaysRelevant / FilterOut）。
	 */
	UE::Net::FNetBitArrayView OutAllowedObjects;

	/** FilteringInfos for all objects. Index using the set bit indices in FilteredObjects. */
	/** 全局 FilteringInfo 数组视图；按 ObjectIndex（即 InternalNetRefIndex）寻址；存放各对象 8B 的私有槽位。 */
	TArrayView<const FNetObjectFilteringInfo> FilteringInfos;

	/** State buffers for all objects. Index using the set bit indices in FilteredObjects. */
	/** 全局对象状态缓冲（量化形态）数组指针，按 ObjectIndex 索引；可结合 ReplicationProtocol dequantize 取数据。 */
	const UE::Net::TNetChunkedArray<uint8*>* StateBuffers = nullptr;

	/** ID of the connection that the filtering applies to. */
	/** 当前批次面向的连接 ID（1..MaxConnections）。0 通常意为"无连接"。 */
	uint32 ConnectionId = 0;

	/** The view associated with the connection and its sub-connections that objects are filtered for. */
	/** 该连接（含子连接/拆屏）的 FReplicationView：相机/视点位置等，用于空间过滤判定。 */
	UE::Net::FReplicationView View;

	/** List of objects that have been filtered out by groups for the ConnectionId */
	/** 已被 Exclusion Group 阶段过滤掉的对象（位图）。Dynamic Filter 可参考此信息避免冗余计算。 */
	const UE::Net::FNetBitArrayView GroupFilteredOutObjects;
};

/**
 * Parameters passed to UNetObjectFilter::PreFilter.
 */
/**
 * 传给 UNetObjectFilter::PreFilter() 的参数包。
 * PreFilter 在所有 Filter() 调用之前恰被调用一次（每帧、每个 Filter 实例）；用于一次性预处理（如
 * 更新对象位置、采样 WorldLocations、重置统计计数等）。仅当至少有一个有效连接时才会被调用。
 */
struct FNetObjectPreFilteringParams
{
	// The IDs of all valid connections.
	/** 当前所有有效连接的 ID 位图（位 i 置位表示 ConnectionId=i 有效）。 */
	UE::Net::FNetBitArrayView ValidConnections;

	/** FilteringInfos for all objects. Index using the set bit indices in FilteredObjects. */
	/** 全局 FilteringInfo 视图（与 Filter() 中含义相同）。 */
	TArrayView<const FNetObjectFilteringInfo> FilteringInfos;
};

/**
 * Parameters passed to UNetObjectFilter::PostFilter.
 */
/**
 * 传给 UNetObjectFilter::PostFilter() 的参数包（目前为空，预留以便未来扩展）。
 * PostFilter 在所有 Filter() 调用后被调用一次；用于汇总统计、批写日志等收尾。
 */
struct FNetObjectPostFilteringParams
{
};

/**
 * Filter specific data stored per object, such as offsets to tags.
 * The data is initialized to zero by default.
 */
/**
 * 每对象 Filter 私有数据（8 字节，按 8 字节对齐）。
 * 仅 4×uint16 槽位，由各 Filter 自行解释（典型用法：状态偏移、本地索引、Cell 索引等）。
 * AddObject 之前由调度器清零；RemoveObject 时按值传回供清理外部资源。
 */
struct alignas(8) FNetObjectFilteringInfo
{
	uint16 Data[4];
};

/** Filter 行为特征位（按位或组合）。由具体 Filter 在 OnInit 中通过 AddFilterTraits 声明。 */
enum class ENetFilterTraits : uint8
{
	None = 0x00,
	/** Set this trait for NetFilters that filter according to the WorldLocation of it's objects. */
	/** 空间型 Filter（依据 WorldLocation 进行可见性判定，如 NetObjectGridFilter）。 */
	Spatial = 0x01,
	/** Set this trait so that UpdateObjects will be called on your NetFilter. Default is to not call the virtual */
	/** 需要 UpdateObjects 回调（脏对象批量通知）；不声明则 UpdateObjects 不会被调度。 */
	NeedsUpdate = 0x02,
};
ENUM_CLASS_FLAGS(ENetFilterTraits);

/**
 * Base class for filter specific configuration.
 * @see FNetObjectFilterDefinition
 */
/**
 * Filter 配置基类。
 * 各 Filter 通过 FNetObjectFilterDefinition::ConfigClassName 在 ini 中指定其具体派生类（取 ClassDefault），
 * 在 Filter 的 Init() 中通过 FNetObjectFilterInitParams::Config 注入。
 */
UCLASS(Transient, MinimalAPI, config=Engine)
class UNetObjectFilterConfig : public UObject
{
	GENERATED_BODY()

public:
};

/** Parameters passed to the filter's Init() call. */
/** Filter::Init() 的参数包。 */
struct FNetObjectFilterInitParams
{
public:
	/** 所属 ReplicationSystem。 */
	TObjectPtr<UReplicationSystem> ReplicationSystem = nullptr;
	/** Optional config as set in the FNetObjectFilterDefinition. */
	/** 可选 Config 实例（取自 ConfigClassName 的 CDO）。Filter 可用 CastChecked 强转到具体派生类。 */
	UNetObjectFilterConfig* Config = nullptr;
	/** The maximum number of replicated objects in the system. */
	/** 系统理论最大对象数（编译期/启动期确定的硬上限）。 */
	uint32 AbsoluteMaxNetObjectCount = 0;
	/** The current maximum replicated objects referenced by an index (may grow at runtime). */
	/** 当前对象索引上限（可能随运行时扩容；扩容时会回调 OnMaxInternalNetRefIndexIncreased）。 */
	uint32 CurrentMaxInternalIndex = 0;
	/** The maximum number of connections in the system. */
	/** 系统最大连接数（用于预分配 PerConnectionInfo 数组）。 */
	uint32 MaxConnectionCount = 0;
};

/** Filter::AddObject() 的参数包。 */
struct FNetObjectFilterAddObjectParams
{
	/** The info is zeroed before the AddObject() call. Fill in with filter specifics, like offsets to tags. */
	/** 已清零的 8B 私有槽（输出）。Filter 实现把"本地索引/Cell 索引/RepTag offset"等 stash 进去，
	 *  供后续 Filter() / RemoveObject() 复用。 */
	FNetObjectFilteringInfo& OutInfo;

	/** Name of a specialized configuration profile. When none, the default settings are expected. */
	/** 可选的特化配置 Profile 名（来自 ObjectReplicationBridgeConfig 的 FilterConfigs.FilterProfile）。 */
	FName ProfileName;

	/** The FReplicationInstanceProtocol which describes the source state data. */
	/** 实例协议（描述源状态数据的字段表）。 */
	const UE::Net::FReplicationInstanceProtocol* InstanceProtocol;

	/** The FReplicationProtocol which describes the internal state data. */
	/** 内部协议（描述量化形态的状态数据）。Filter 可借此用 FRepTag 解析出 WorldLocation 等。 */
	const UE::Net::FReplicationProtocol* Protocol;

	/**
	 * One can retrieve relevant information from the object state buffer using the FReplicationProtocol.
	 * Note that this is the internal network representation of the data which is stored in quantized form.
	 * NetSerializers can dequantize the data to the original source data form.
	 */
	/**
	 * 对象当前的（量化）状态缓冲指针。可配合 Protocol 通过 NetSerializer 反量化得到源域数据。
	 */
	const uint8* StateBuffer;
};

/** Parameters passed to the filter's UpdateObjects() call. */
/** Filter::UpdateObjects() 的参数包：仅声明 ENetFilterTraits::NeedsUpdate 的 Filter 才会被调用。 */
struct FNetObjectFilterUpdateParams
{
	/** Indices of the updated objects. */
	/** 本批"脏对象"索引数组的起始指针。 */
	const uint32* ObjectIndices = nullptr;
	/** The number of objects that have been updated. */
	/** 本批脏对象个数。 */
	uint32 ObjectCount = 0;

	/** Infos for all objects. Index using ObjectIndices[0..ObjectCount-1]. */
	/** 全局 FilteringInfo 视图，按 ObjectIndex 索引。 */
	TArrayView<FNetObjectFilteringInfo> FilteringInfos;
};

/**
 * UNetObjectFilter —— 所有动态过滤器的 UCLASS 抽象基类。
 *
 * 生命周期与调度（由 FReplicationFiltering 全程编排）：
 *   FReplicationFiltering::InitFilters()
 *      └─ NewObject<具体子类>() → Init(Params) → OnInit(Params)            // 启动期
 *   每帧 Filter():
 *      ├─ AddConnection / RemoveConnection (按需)                           // 连接生灭
 *      ├─ AddObject / RemoveObject (按需)                                   // 对象注册
 *      ├─ UpdateObjects（若声明 NeedsUpdate）                                // 脏对象通知
 *      ├─ PreFilter（仅当至少一个连接需要过滤时）                            // 一次性预处理
 *      ├─ 多次 Filter（每连接可能分多批）                                    // 主过滤
 *      └─ PostFilter                                                         // 一次性收尾
 *   FReplicationFiltering::Deinit()
 *      └─ Deinit() → OnDeinit()                                             // 关停
 */
UCLASS(Abstract, MinimalAPI)
class UNetObjectFilter : public UObject
{
	GENERATED_BODY()

public:
	/** 由调度器调用，分配 FilteredObjects 位图、抓取全局 FilteringInfos 视图与 NetRefHandleManager，再回调 OnInit。 */
	IRISCORE_API void Init(const FNetObjectFilterInitParams& Params);
	/** 关停：先 OnDeinit，再清空内部引用。 */
	IRISCORE_API void Deinit();

	/** 索引上限扩容回调：扩位图 + 转交新视图，最后回调 OnMaxInternalNetRefIndexIncreased。 */
	IRISCORE_API void MaxInternalNetRefIndexIncreased(UE::Net::Private::FInternalNetRefIndex MaxInternalIndex, TArrayView<FNetObjectFilteringInfo> NewFilterInfoView);
	
	/** A new connection has been added. An opportunity for the filter to allocate per connection info. */
	/** 新连接加入回调（默认空实现，需要 per-connection 状态的 Filter 重写）。 */
	IRISCORE_API virtual void AddConnection(uint32 ConnectionId);

	/** A new connection has been removed. An opportunity for the filter to deallocate per connection info. */
	/** 连接移除回调（默认空实现）。 */
	IRISCORE_API virtual void RemoveConnection(uint32 ConnectionId);

	/** A new object want to use this filter. Opportunity to cache some information for it. The info struct passed has been zeroed. Must be overriden. */
	/** 对象选择本 Filter 时被调用：填充 FilteringInfo 槽位、注册到内部容器；返回 false 视为"拒绝接管"。必须重写。 */
	virtual bool AddObject(uint32 ObjectIndex, FNetObjectFilterAddObjectParams&) PURE_VIRTUAL(AddObject, return false;)

	/** An object no longer wants to use this filter. */
	/** 对象不再使用本 Filter（更换 filter / 销毁）。必须重写以正确清理私有数据。 */
	virtual void RemoveObject(uint32 ObjectIndex, const FNetObjectFilteringInfo&) PURE_VIRTUAL(RemoveObject,)

	/** A set of objects using this filter are dirty since the last update. An opportunity for the filter to update cached data. */
	/** 脏对象批量通知回调（仅声明 NeedsUpdate 才会被调用）。默认空实现。 */
	IRISCORE_API virtual void UpdateObjects(FNetObjectFilterUpdateParams&);

	/**
	 * If there are any connections being replicated and there's a chance Filter() will be called then PreFilter()
	 * will be called exactly once before all calls to Filter().
	 */
	/** 仅当本帧将要发生 Filter() 调用时，PreFilter() 恰被调用一次。默认空实现。 */
	IRISCORE_API virtual void PreFilter(FNetObjectPreFilteringParams&);

	/** Filter a batch of objects. There may be multiple calls to this function even for the same connection. Must be overriden. */
	/** 主过滤入口。同一连接可能被多次调用（分批）。基类默认空实现，所有具体 Filter 必须重写。 */
	IRISCORE_API virtual void Filter(FNetObjectFilteringParams&);

	/**
	 * If PreFilter() was called then PostFilter() will be called exactly once after all Filter() calls.
	 */
	/** 与 PreFilter 配对：当 PreFilter 被调用过则 PostFilter 必被调用一次。默认空实现。 */
	IRISCORE_API virtual void PostFilter(FNetObjectPostFilteringParams&);

	/** Returns all the filter's traits. */
	/** 返回 Filter 在 OnInit 中累积声明的所有 traits。 */
	ENetFilterTraits GetFilterTraits() const { return FilterTraits; }

	/** Tells if the filter was assigned a specific trait */
	/** 检查是否拥有指定 trait（位与）。 */
	bool HasFilterTrait(ENetFilterTraits FilterTrait) const { return EnumHasAnyFlags(FilterTraits, FilterTrait); }

	/** The list of objects that are filtered by this filter */
	/** 返回当前归属本 Filter 的对象集合（位图视图）。 */
	UE::Net::FNetBitArrayView GetFilteredObjects() { return MakeNetBitArrayView(FilteredObjects); }


	/** 调试辅助：PrintDebugInfoForObject 的入参；含 Filter 名、当前 Connection 与 View。 */
	struct FDebugInfoParams
	{
		FName FilterName;

		TArrayView<const FNetObjectFilteringInfo> FilteringInfos;

		/** ID of the connection that the filtering applies to. */
		uint32 ConnectionId = 0;

		/** The view associated with the connection and its sub-connections that objects are filtered for. */
		UE::Net::FReplicationView View;
	};
	/** 默认仅返回 FilterName；具体 Filter 可重写以输出更详细的 per-object 调试信息（如 GridFilter 的距离）。 */
	virtual FString PrintDebugInfoForObject(const FDebugInfoParams& Params, uint32 ObjectIndex) const { return Params.FilterName.ToString(); };

protected:
	IRISCORE_API UNetObjectFilter();

	/** Called right after constructor for enabled filters. Must be overriden. */
	/** 真正的初始化钩子（基类 Init 内部回调）。子类必须实现以读取 Config、建立 per-connection/per-object 容器等。 */
	virtual void OnInit(const FNetObjectFilterInitParams&) PURE_VIRTUAL(OnInit, );

	/** Called when the replication system is shutting down. Use this to remove references to other systems */
	/** 关停钩子：用于断开外部系统引用（WorldLocations / Config 等）。子类必须实现。 */
	virtual void OnDeinit() PURE_VIRTUAL(OnDeinit);

	/** Called when the maximum InternalNetRefIndex increased and we need to realloc our lists */
	/** 索引上限扩容钩子：子类用来扩展 per-object 的 TArray/位图。 */
	virtual void OnMaxInternalNetRefIndexIncreased(uint32 NewMaxInternalIndex) PURE_VIRTUAL(OnMaxInternalNetRefIndexIncreased);

	/* Returns the filtering info for this object if it's handled by this filter, nullptr otherwise. */
	/** 安全获取 FilteringInfo：仅当对象由本 Filter 接管时返回有效指针；否则 nullptr。 */
	IRISCORE_API FNetObjectFilteringInfo* GetFilteringInfo(uint32 ObjectIndex);

	/* Returns the object index for the given NetRefHandle */
	/** NetRefHandle → InternalNetRefIndex 的便捷转换。 */
	IRISCORE_API uint32 GetObjectIndex(UE::Net::FNetRefHandle NetRefHandle) const;

	/* Returns true if the object is assigned to be filtered by this filter.*/
	/** 是否归属本 Filter（FilteredObjects 对应位置位）。 */
	inline bool IsObjectFiltered(uint32 ObjectIndex) const;

	/** Adds traits. */
	/** 累加 traits（按位或）。一般在 OnInit 中调用。 */
	inline void AddFilterTraits(ENetFilterTraits Traits);

	/** Sets the traits specified by TraitsMask to Traits. */
	/** 用 mask 局部覆盖 traits（用于切换 NeedsUpdate 等动态状态）。 */
	inline void SetFilterTraits(ENetFilterTraits Traits, ENetFilterTraits TraitsMask);

protected:

	/** The indices of the objects that have this filter set. The indices of set bits correspond to the object indices. */
	/** 归属本 Filter 的对象集合（位图）。位 i 置位表示 ObjectIndex=i 由本 Filter 接管。 */
	UE::Net::FNetBitArray FilteredObjects;

	/** 全局 NetRefHandleManager（只读引用，用于 NetRefHandle ↔ InternalNetRefIndex 的转换与日志）。 */
	const UE::Net::Private::FNetRefHandleManager* NetRefHandleManager = nullptr;

private:
	/** Filter traits 累加位（OnInit 中由子类按需 AddFilterTraits）。 */
	ENetFilterTraits FilterTraits = ENetFilterTraits::None;
	
	/** 全局 FilteringInfos 视图（共享自 FReplicationFiltering 的 NetObjectFilteringInfos）。 */
	TArrayView<FNetObjectFilteringInfo> FilteringInfos; 
};

inline bool UNetObjectFilter::IsObjectFiltered(uint32 ObjectIndex) const
{
	return ObjectIndex < FilteredObjects.GetNumBits() && FilteredObjects.IsBitSet(ObjectIndex);
}

inline void UNetObjectFilter::AddFilterTraits(ENetFilterTraits Traits)
{
	FilterTraits |= Traits;
}

inline void UNetObjectFilter::SetFilterTraits(ENetFilterTraits Traits, ENetFilterTraits TraitsMask)
{
	const ENetFilterTraits NewFilterTraits = (FilterTraits & ~TraitsMask) | (Traits & TraitsMask);
	FilterTraits = NewFilterTraits;
}
