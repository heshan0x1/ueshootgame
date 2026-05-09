// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：UNetObjectCountLimiter —— 「数量限额」打分器（非空间）。
// 用途：对一组对象限制每帧每连接最多考虑 N 个，避免低带宽连接被大量小对象淹没。
// 两种模式：
//   - RoundRobin：每帧顺序取下 N 个内部对象（不论谁拥有）；其它对象本帧忽略。
//                  如果这 N 个对象本帧没脏，则本连接本帧不发送任何被限额的对象。
//   - Fill：    每帧选 N 个"最久未被考虑过的"脏对象（per-connection，按 LastConsiderFrame 排序）。
//                  保证带宽充足时旧对象会被替换出去；脏得多 / 没别的可发时同对象会被反复发送。
//
// Owner Fast-Lane（bEnableOwnedObjectsFastLane）：
//   - true：本连接拥有的对象不计入 N 限额（总是考虑）；
//   - false：包括 owned 在内一律占用 N 名额。
//
// 注意：
//   - 本 prioritizer 不参与"最近距离 / 视锥"——纯粹做名额控制；
//   - 与空间 prioritizer 互斥（一个对象只能绑定一个 prioritizer，详见 ReplicationPrioritization.cpp）；
//   - 无法处理"超大对象数 + Fill 模式"（一次 Prioritize 调用看不到全部脏对象时会触发 ensure，见 cpp）。
// =============================================================================================================================

#pragma once

#include "Iris/ReplicationSystem/Prioritization/NetObjectPrioritizer.h"
#include "Net/Core/NetBitArray.h"
#include "UObject/StrongObjectPtr.h"
#include "NetObjectCountLimiter.generated.h"

UENUM()
enum class ENetObjectCountLimiterMode : uint32
{
	/**
	 * Each net update the next N, as configured, objects will be allowed to be replicated if they have modified properties.
	 * This means that even if there are many objects that have modified properties none will be sent if the N objects that
	 * are considered this frame haven't been modified.
	 */
	// 顺序轮询：所有连接共享同一全局指针 NextIndexToConsider。
	// 优点：调度成本极低（O(1) 分配）；
	// 缺点：N 个轮询对象本帧若都不脏，则该连接本帧无任何被限额对象被发送。
	RoundRobin,
	/**
	 * Each net update the N least recently replicated objects with modified properties will be allowed to be replicated.
	 * This can cause an object to be replicated very often if it's modified a lot and nothing else is.
	 */
	// 填充模式：per-connection 维护 LastConsiderFrame[]，每帧排序选最久没考虑过的 N 个。
	// 优点：带宽利用率高、不浪费名额；
	// 缺点：排序成本（n log n），且不能跨多个 batch 执行（必须一次看到全部脏对象）。
	Fill,
};

//TODO $IRIS: Document class usage
UCLASS(transient, config=Engine, MinimalAPI)
class UNetObjectCountLimiterConfig : public UNetObjectPrioritizerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY()
	ENetObjectCountLimiterMode Mode = ENetObjectCountLimiterMode::RoundRobin;

	/**
	 * How many objects to be considered for replication each frame.
	 * With 2 at least 1 object that isn't owned by the connection will be considered.
	 * If the prioritizer won't deal with objects that are owned by a specific connection
	 * it's safe to set to 1.
	 */
	// 每帧每连接最多考虑 N 个（不计 owned，如开启 Fast-Lane）。
	UPROPERTY(Config)
	uint32 MaxObjectCount = 2;

	/**
	  * Which priority to set for objects considered for replication.
	  * Priority is accumulated for an object until it's replicated.
	  * 1.0f is the threshold at which the object may be replicated.
	  */
	// 通用对象的赋值优先级（注意：不是叠加，是直接覆盖；非"考虑"对象保留其原值）。
	UPROPERTY(Config)
	float Priority = 1.0f;

	/**
	 * The priority to set for a considered object if it's owned by the connection being prioritized for.
	 */
	// owned 对象的优先级（通常 ≥ Priority 以确保自己拥有的对象先发）。
	UPROPERTY(Config)
	float OwningConnectionPriority = 1.0f;

	/**
	 * Whether objects owned by the connection should always be considered for replication.
	 * If so, such objects won't count against the MaxObjectCount.
	 */
	// owned 对象是否走快速通道（不占 N 名额）。
	UPROPERTY(Config)
	bool bEnableOwnedObjectsFastLane = true;
};

UCLASS()
class UNetObjectCountLimiter : public UNetObjectPrioritizer
{
	GENERATED_BODY()

protected:
	// UNetObjectPrioritizer interface
	IRISCORE_API virtual void Init(FNetObjectPrioritizerInitParams&) override;
	IRISCORE_API virtual void Deinit() override;
	// 不需要按 InternalNetRefIndex 扩容——本类的 InternalObjectIndices 是按"绑定到本 prioritizer 的对象数"增长的。
	virtual void OnMaxInternalNetRefIndexIncreased(uint32 NewMaxInternalIndex) override {}
	IRISCORE_API virtual void AddConnection(uint32 ConnectionId) override;
	IRISCORE_API virtual void RemoveConnection(uint32 ConnectionId) override;
	IRISCORE_API virtual bool AddObject(uint32 ObjectIndex, FNetObjectPrioritizerAddObjectParams&) override;
	IRISCORE_API virtual void RemoveObject(uint32 ObjectIndex, const FNetObjectPrioritizationInfo&) override;
	IRISCORE_API virtual void UpdateObjects(FNetObjectPrioritizerUpdateParams&) override;
	IRISCORE_API virtual void PrePrioritize(FNetObjectPrePrioritizationParams&) override;
	IRISCORE_API virtual void Prioritize(FNetObjectPrioritizationParams&) override;

protected:
	// 16 字节 prioritizer 私有缓存的视图：
	//   Data[0] = PrioritizerInternalIndex（在 InternalObjectIndices 位图 / LastConsiderFrames 数组中的下标，最多 65535）；
	//   Data[1] = OwningConnection（uint16 截断的 ConnectionId）。
	struct FObjectInfo : public FNetObjectPrioritizationInfo
	{
		void SetPrioritizerInternalIndex(uint16 Index) { Data[0] = Index; }
		uint16 GetPrioritizerInternalIndex() const { return Data[0]; }

		void SetOwningConnection(uint32 ConnectionId) { Data[1] = static_cast<uint16>(ConnectionId); }
		uint32 GetOwningConnection() const { return static_cast<uint32>(Data[1]); }
	};

	// 单连接信息（仅 Fill 模式使用）。
	struct FPerConnectionInfo
	{
		// Which frame was this object last considered for replication for this connection
		// 下标 = PrioritizerInternalIndex；值 = 上次该对象被本连接考虑的 PrioFrame。
		// 排序时用 (PrioFrame - LastConsiderFrame) 作为"久未考虑"度。
		TArray<uint32> LastConsiderFrames;
	};

	struct FBatchParams
	{
		uint32 ConnectionId;
		uint32 ObjectCount;
		float* Priorities;
	};

protected:
	IRISCORE_API UNetObjectCountLimiter();

	// 测试 / 调试用：读写 LastConsiderFrame。
	IRISCORE_API uint32 GetLastConsiderFrame(uint32 ConnectionId, uint32 ObjectIndex) const;
	IRISCORE_API void SetLastConsiderFrame(uint32 ConnectionId, uint32 ObjectIndex, uint32 FrameNumber);

private:
	enum : unsigned
	{
		// Keep the ObjectGrowCount fairly low as allocation size = ObjectGrowCount * max number of connections * size for per object info
		// 按 64 一组扩容 InternalObjectIndices 与 LastConsiderFrames（每连接也按此对齐）。
		ObjectGrowCount = 64U,
	};

	// RoundRobin 模式的全局状态：
	//   InternalObjectIndices：本帧轮到的 N 个对象的位图；
	//   NextIndexToConsider：下一帧从哪个 internal index 开始（环形）。
	struct FRoundRobinState
	{
		UE::Net::FNetBitArray InternalObjectIndices;
		uint16 NextIndexToConsider = 0;
	};

	// Fill 模式的"重入检测"：当一帧内同连接被调用多次时（说明对象太多被切成多批），ensure 警告。
	struct FFillState
	{
		uint32 LastPrioFrame = 0;
		uint32 LastConnectionId = 0;
	};

	// AllocInternalIndex：为新对象分配一个紧凑 internal index（FindFirstZero + 必要时按 ObjectGrowCount 扩容）。
	uint16 AllocInternalIndex();
	void FreeInternalIndex(uint16 Index);
	void PrePrioritizeForRoundRobin();
	void PrioritizeForRoundRobin(FNetObjectPrioritizationParams&) const;
	void PrioritizeForFill(FNetObjectPrioritizationParams&);

private:
	TObjectPtr<const UReplicationSystem> ReplicationSystem;
	TStrongObjectPtr<UNetObjectCountLimiterConfig> Config;
	TArray<FPerConnectionInfo> PerConnectionInfos;
	// 所有绑定到本 prioritizer 的对象的 internal index 位图（哪个 index 在用）。
	UE::Net::FNetBitArray InternalObjectIndices;
	FRoundRobinState RoundRobinState;
	FFillState FillState;
	// 帧计数器（PrePrioritize 自增）。Fill 模式的"久未考虑"度依赖此值。
	uint32 PrioFrame;
	uint32 ReplicationSystemId;
};
