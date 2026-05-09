// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：Iris ReplicationSystem 的「优先级 Prioritization」子模块对外抽象基类。
//   - 定义 UNetObjectPrioritizer 抽象基类（生命周期：Init/Deinit、AddObject/RemoveObject/UpdateObjects、Pre/Prioritize/Post）。
//   - 定义参数族：FNetObjectPrioritizerInitParams / AddObjectParams / UpdateParams / PrioritizationParams /
//     Pre|PostPrioritizationParams，以及每对象的 16 字节缓存 FNetObjectPrioritizationInfo。
//   - 定义 FNetObjectPrioritizerHandle（按名字索引到具体 prioritizer 的不透明句柄）和默认空间 Prioritizer 句柄常量。
// 与文档一致：见 ReplicationSystem.md §6.2 Prioritization。
// =============================================================================================================================

#pragma once

#include "Iris/Core/NetChunkedArray.h"
#include "Iris/ReplicationSystem/ReplicationView.h"
#include "UObject/ObjectMacros.h"
#include "NetObjectPrioritizer.generated.h"

struct FNetObjectPrioritizationInfo;
class UReplicationSystem;
namespace UE::Net
{
	// FNetObjectPrioritizerHandle：32 位整数句柄。0 表示默认空间 prioritizer，~0 表示无效。
	// 由 UNetObjectPrioritizerDefinitions ini 注册顺序决定。
	typedef uint32 FNetObjectPrioritizerHandle;
	struct FReplicationInstanceProtocol;     // 每实例协议：含 FragmentData[]，指向真实 UObject 属性源缓冲。
	struct FReplicationProtocol;             // 共享形态协议：含 ReplicationStateDescriptors[]、ChangeMaskBits 等。
	struct FReplicationView;                 // 连接视图（含多个 FView：Pos/Dir/Controller/ViewTarget）。
}

namespace UE::Net
{

/** Used to represent an invalid handle to a prioritizer. */
// 无效 prioritizer 句柄常量（全 1）。SetPrioritizer 不接受此值。
constexpr FNetObjectPrioritizerHandle InvalidNetObjectPrioritizerHandle = ~FNetObjectPrioritizerHandle(0);

/**
 * The handle of the default prioritizer.
 * The first valid prioritizer definition will assume the role as default spatial prioritizer. All objects with a RepTag_WorldLocation tag 
 * will be added to the default prioritizer. To override the behavior a prioritizer must be set via calls to the ReplicationSystem.
 * @see UNetObjectPrioritizerDefinitions
 */
// 默认空间 prioritizer 句柄（句柄值固定为 0）：
//   - ini 中 `UNetObjectPrioritizerDefinitions::NetObjectPrioritizerDefinitions` 数组的第一个有效条目会被视为默认空间 prioritizer。
//   - 任何带有 RepTag_WorldLocation 的对象会被自动加入这个默认 prioritizer。
//   - 如需自定义可调用 UReplicationSystem::SetPrioritizer。
constexpr FNetObjectPrioritizerHandle DefaultSpatialNetObjectPrioritizerHandle = FNetObjectPrioritizerHandle(0);

}

/**
  * Parameters passed to UNetObjectPrioritizer::Prioritize.
  *
  * The prioritizer should honor the existing priority set for an object and only update the
  * priority if the calculated value is higher than what is already stored.
  * Prioritize() is only allowed to modify Priorities.
  */
// Prioritize() 的入参：批量打分一组对象。
// 重要约定：
//   - prioritizer 应"取较大者"——即只有当自己计算出的优先级 > 已存优先级时才覆盖（允许多个 prioritizer 叠加）；
//   - Prioritize() 仅允许写 Priorities，其它字段（ObjectIndices/PrioritizationInfos/...）只读；
//   - 同一连接可能多次调用 Prioritize（按 prioritizer 分桶 + 按批次切片，每批最多 1024 对象，见
//     FReplicationPrioritization::FPrioritizerBatchHelper::MaxObjectCountPerBatch）。
struct FNetObjectPrioritizationParams
{
	/** 
	 * The indices for the objects that are being prioritized. Will only contain objects
	 * which have been added to the prioritizer and have dirty properties or are in need of resending.
	 */
	// 本批要打分的对象的 InternalNetRefIndex 列表（仅来自当前 prioritizer 名下的脏对象 / 需重发对象）。
	const uint32* ObjectIndices;

	/** The number of objects to prioritize. */
	// ObjectIndices 的长度。
	uint32 ObjectCount;

	/** Priorities for all objects. Index using ObjectIndices[0..ObjectCount-1]. */
	// 整个连接所有对象的优先级数组（per-connection）；用 ObjectIndices[i] 作为下标。可读可写。
	// >= 0：0 表示无需复制；1.0 是被纳入复制候选的下限阈值；累加直到对象被实际复制后清零。
	float* Priorities;

	/** PrioritizationInfos for all objects. Index using ObjectIndices[0..ObjectCount-1]. */
	// 每对象的 prioritizer 私有数据（由 AddObject 时填入），只读，用 ObjectIndices[i] 作为下标。
	const FNetObjectPrioritizationInfo* PrioritizationInfos;

	/** ID of the connection that objects are prioritized for. */
	// 当前正在为之打分的连接 ID。
	uint32 ConnectionId;

	/** The view associated with the connection and its sub-sconnections that objects are prioritized for. */
	// 该连接（含子连接）的视图：含位置 / 朝向 / Controller / ViewTarget。Sphere/FOV prioritizer 据此打分。
	UE::Net::FReplicationView View;
};

/** Parameters passed to the prioritizer's PrePrioritize call. */
// PrePrioritize 入参（目前为空，预留扩展）。在所有连接的 Prioritize 调用之前调用一次。
struct FNetObjectPrePrioritizationParams
{
};

/** Parameters passed to the prioritizer's PostPrioritize call. */
// PostPrioritize 入参（目前为空，预留扩展）。在所有连接的 Prioritize 调用之后调用一次。
struct FNetObjectPostPrioritizationParams
{
};

/**
 * Prioritizer specific data stored per object, such as offsets to tags.
 * The data is initialized to zero by default.
 */
// 每对象的 prioritizer 私有缓存（固定 8 字节对齐，4×uint16 共 8 字节）。
//   - Iris 总共为每个 NetObject 预留这固定 16 字节空间（不同 prioritizer 派生类用不同方式打包）。
//   - 各派生类常用法（参考各 cpp）：
//       Location 系列：Data[0]=ExternalStateOffset / Data[1]=StateIndex / Data[2..3]=LocationIndex（指向内部位置数组的 32-bit 索引）；
//       NetObjectCountLimiter：Data[0]=PrioritizerInternalIndex / Data[1]=OwningConnection；
//   - AddObject 调用之前由调度器清零（见 FReplicationPrioritization::SetPrioritizer 中
//     `NetObjectPrioritizationInfo = FNetObjectPrioritizationInfo{};`）。
struct alignas(8) FNetObjectPrioritizationInfo
{
	uint16 Data[4];
};

/**
 * Base class for prioritizer specific configuration.
 * @see FNetObjectPrioritizerDefinition
 */
// Prioritizer 配置基类。每个具体 prioritizer 可继承本类提供 ini 配置（如球半径、视野角度等）。
// 由 ini `[/Script/IrisCore.NetObjectPrioritizerDefinitions]` 中 ConfigClassName 引用，
// 在 InitPrioritizers() 时通过 NewObject 实例化并传给 UNetObjectPrioritizer::Init。
UCLASS(Transient, MinimalAPI)
class UNetObjectPrioritizerConfig : public UObject
{
	GENERATED_BODY()
};

/** Parameters passed to the prioritizer's Init() call. */
// Init 入参：每个 prioritizer 在 ReplicationSystem 启动阶段初始化一次。
struct FNetObjectPrioritizerInitParams
{
	/** The ReplicationSystem that owns the prioritizer. */
	// 拥有该 prioritizer 的 ReplicationSystem（只读）。
	TObjectPtr<const UReplicationSystem> ReplicationSystem;
	/** Optional config as set in the FNetObjectPrioritizerDefinition. */
	// 由 ini 定义的 ConfigClass 实例（可能为空，由派生类自行 CastChecked）。
	UNetObjectPrioritizerConfig* Config = nullptr;
	/** The maximum number of replicated objects in the system. */
	// 系统支持的可复制对象绝对上限（用于一次性预分配最大容量数组）。
	uint32 AbsoluteMaxNetObjectCount = 0;
	/** The current maximum replicated objects referenced by an index (may grow at runtime). */
	// 当前实际使用到的最大 InternalNetRefIndex（运行期会增长，触发 OnMaxInternalNetRefIndexIncreased）。
	uint32 CurrentMaxInternalIndex = 0;
	/** The maximum number of connections in the system. */
	// 系统支持的最大连接数。
	uint32 MaxConnectionCount = 0;
};

/** Parameters passed to the prioritizer's AddObject() call. */
// AddObject 入参：当对象第一次绑定到本 prioritizer 时调用（初次注册或 SetPrioritizer 切换）。
struct FNetObjectPrioritizerAddObjectParams
{
	/** The info is zeroed before the AddObject() call. Fill in with prioritizer specifics, like offsets to tags. */
	// 输出：需要由 prioritizer 写入的 16 字节私有缓存。在调用前已清零。
	FNetObjectPrioritizationInfo& OutInfo;

	/** The FReplicationInstanceProtocol which describes the source state data. */
	// 实例协议（指向真实 UObject 属性的 ExternalSrcBuffer）。
	const UE::Net::FReplicationInstanceProtocol* InstanceProtocol;

	/** The FReplicationProtocol which describes the internal state data. */
	// 共享形态协议（含 RepTag 偏移、Descriptor 数组等）；用于 FindRepTag 检索 RepTag_WorldLocation 等位置信息。
	const UE::Net::FReplicationProtocol* Protocol;

	/**
	 * One can retrieve relevant information from the object state buffer using the FReplicationProtocol.
	 * Note that this is the internal network representation of the data which is stored in quantized form.
	 * NetSerializers can dequantize the data to the original source data form.
	 */
	// 该对象的内部已量化状态缓冲（按 Protocol 描述布局）。如需读取需经 NetSerializer 反量化。
	const uint8* StateBuffer;
};

/** Parameters passed to the prioritizer's UpdateObjects() call. */
// UpdateObjects 入参：当一批对象当前帧标脏时，调度器调用此函数以让 prioritizer 刷新缓存数据
// （例如 Location prioritizer 重新读取世界位置）。每批最多 512 对象（见 FUpdateDirtyObjectsBatchHelper）。
struct FNetObjectPrioritizerUpdateParams
{
	/** Indices of the updated objects. */
	// 本批被标脏的对象 InternalNetRefIndex 列表。
	const uint32* ObjectIndices;
	/** The number of objects that have been updated. */
	uint32 ObjectCount;

	/** InstanceProtocols for updated objects. Index using 0..ObjectCount-1. */
	// 与 ObjectIndices 一一对应的 InstanceProtocol 数组（注意：这个数组用 0..ObjectCount-1 索引，不是用 ObjectIndices[i]）。
	UE::Net::FReplicationInstanceProtocol const*const* InstanceProtocols;

	/** State buffers for all objects. Index using ObjectIndices[0..ObjectCount-1]. */
	// 整个系统所有对象的 StateBuffer 数组；用 ObjectIndices[i] 作为下标取本对象的状态缓冲。
	const UE::Net::TNetChunkedArray<uint8*>* StateBuffers = nullptr;

	/** Infos for all objects. Index using ObjectIndices[0..ObjectCount-1]. */
	// 所有对象的 prioritizer 私有缓存数组；可读可写，用 ObjectIndices[i] 作为下标。
	FNetObjectPrioritizationInfo* PrioritizationInfos;
};

/**
 * NetObjectPrioritizers are responsible for determining how important it is to replicate an object. Priorities should be at least 0.0f, 
 * meaning no need to replicate. At 1.0f objects are being considered for replication. Priorities are acumulated per object and connection 
 * until it's replicated, at which point the priority is reset to zero. Bandwidth constraints and other factors may cause a highly prioritized 
 * object to still not be replicated to a particular connection a certain frame. There is no mechanism to force an object to be replicated a 
 * certain frame, but the priority is a major factor in the decision.
 */
// =============================================================================================================================
// UNetObjectPrioritizer 抽象基类
// 职责：决定一个对象对某连接当帧"有多重要"——给出一个浮点优先级分数。
// 关键语义：
//   - 优先级是 per-(object, connection) 的累加值；
//   - 0.0f = 完全无需复制；1.0f = 达到入选阈值；越大优先级越高；
//   - 一旦对象在该连接被实际复制（ReplicationWriter 成功 Write），其优先级清零；
//   - 优先级仅是"调度因子"之一，并非强制；带宽/包大小/状态机等因素也可能阻止其被发送。
// 内置派生类（见 ReplicationSystem.md §6.2）：
//   - ULocationBasedNetObjectPrioritizer   — 位置基类（提供位置缓存和距离计算骨架）；
//   - USphereNetObjectPrioritizer          — 球体衰减打分；
//   - USphereWithOwnerBoostNetObjectPrioritizer — 球体 + Owner 加成；
//   - UFieldOfViewNetObjectPrioritizer     — 视锥 + 内/外球 + 视线胶囊四何形状取最大；
//   - UNetObjectCountLimiter               — RoundRobin / Fill 模式的数量限额。
// =============================================================================================================================
UCLASS(Abstract)
class UNetObjectPrioritizer : public UObject
{
	GENERATED_BODY()

public:
	/** Called once at init time before any other calls to the prioritizer. */
	// 初始化（只调用一次）。派生类必须实现：缓存 Config / 分配位置数组 / 注册 WorldLocations 等。
	virtual void Init(FNetObjectPrioritizerInitParams& Params) PURE_VIRTUAL(Init,)

	/** Called when the replication system is shutting down. Use this to remove references to other systems */
	// 反初始化：释放对其它系统的引用（WorldLocations、ReplicationSystem 等）。
	virtual void Deinit() PURE_VIRTUAL(Deinit)

	/** Called when the maximum InternalNetRefIndex increased and we need to realloc our lists */
	// 最大 InternalNetRefIndex 增长时回调，派生类需扩容自己的 per-object 数据结构。
	virtual void OnMaxInternalNetRefIndexIncreased(uint32 NewMaxInternalIndex) PURE_VIRTUAL(OnMaxInternalNetRefIndexIncreased);

	/** A new connection has been added. An opportunity for the prioritizer to allocate per connection info. */
	// 新连接接入：派生类可在此分配 per-connection 状态（如 NetObjectCountLimiter 的 LastConsiderFrames）。
	IRISCORE_API virtual void AddConnection(uint32 ConnectionId);

	/** A new connection has been added. An opportunity for the prioritizer to deallocate per connection info. */
	// 连接断开：释放 per-connection 状态。
	IRISCORE_API virtual void RemoveConnection(uint32 ConnectionId);

	/** A new object want to use this prioritizer. Opportunity to cache some information for it. The info struct passed has been zeroed. */
	// 对象绑定到本 prioritizer：返回 false 表示本 prioritizer 不能处理该对象（调度器会回退到静态默认优先级）。
	// 输入的 OutInfo 已清零。
	virtual bool AddObject(uint32 ObjectIndex, FNetObjectPrioritizerAddObjectParams& Params) PURE_VIRTUAL(AddObject, return false;)

	/** An object do no longer want to use this prioritizer. */
	// 对象解绑：释放与该对象相关的内部资源（位置槽位、Owner 槽位等）。
	virtual void RemoveObject(uint32 ObjectIndex, const FNetObjectPrioritizationInfo& Info) PURE_VIRTUAL(RemoveObject,)

	/** A set of objects used by this prioritizer have been updated. An opportunity for the prioritizer to update cached data. */
	// 对象当前帧脏化时，调度器集中传给 prioritizer 让其刷新缓存（例如重新读取世界位置）。
	virtual void UpdateObjects(FNetObjectPrioritizerUpdateParams&) PURE_VIRTUAL(UpdateObjects,)

	/**
	 * If there are any connections being replicated and there's a chance Prioritize() will be called then PrePrioritize()
	 * will be called exactly once before all calls to Prioritize().
	 */
	// 在所有连接的 Prioritize 调用前唯一一次回调。仅当至少有一个连接需要复制时才会调用。
	// 用于 prioritizer 准备共享数据（例如 NetObjectCountLimiter 的 RoundRobin 全局轮询索引）。
	IRISCORE_API virtual void PrePrioritize(FNetObjectPrePrioritizationParams&);

	/**
	 * Prioritize a batch of objects. There may be multiple calls to this function even for the same connection. 
	 * Stored priorities are expected to use the maximum of the already stored priority and the prioritizer calculated one.
	 * That allows multiple prioritizers to be used on the same object.
	 */
	// 批量打分。同一连接可能被多次调用（按 prioritizer 分桶 + 1024/批切片）。
	// 派生类应当：Priorities[ObjectIndex] = max(Priorities[ObjectIndex], 计算值)，以便多 prioritizer 共存。
	virtual void Prioritize(FNetObjectPrioritizationParams&) PURE_VIRTUAL(Prioritize,);

	/** If PrePrioritize() was called then PostPrioritize() will be called exactly once after all Prioritize() calls. */
	// 与 PrePrioritize 对称，所有 Prioritize 调用之后唯一一次回调（仅当 PrePrioritize 被调用过）。
	IRISCORE_API virtual void PostPrioritize(FNetObjectPostPrioritizationParams&);

protected:
	IRISCORE_API UNetObjectPrioritizer();
};
