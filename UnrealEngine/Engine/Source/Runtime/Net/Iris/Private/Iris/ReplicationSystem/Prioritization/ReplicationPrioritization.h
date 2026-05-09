// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：FReplicationPrioritization —— Iris Prioritization 子模块的「核心调度器」。
// 在 ReplicationSystem 帧循环中位于：UpdatePrioritization 阶段（参见 Iris_Architecture.md §4 与 ReplicationSystem.md §6.2）。
//
// 职责：
//   1) InitPrioritizers：读取 ini，逐个 NewObject + Init 各 prioritizer，分配 PrioritizerInfos[]。
//   2) 维护映射：ObjectIndex → 唯一 prioritizer 索引（uint8，0xFF 表示静态默认优先级）。
//   3) 维护数据：
//        - DefaultPriorities：每对象的「静态默认优先级」（SetStaticPriority 设置）；
//        - NetObjectPrioritizationInfos：每对象 16 字节的 prioritizer 私有缓存；
//        - ConnectionInfos[ConnId].Priorities：per-connection 优先级累加值；
//        - ObjectsWithNewStaticPriority：标记本帧需要把新静态优先级广播到所有连接的对象。
//   4) 每帧 Prioritize(ConnectionsToSend, DirtyObjectsThisFrame)：
//        a. UpdatePrioritiesForNewAndDeletedObjects：处理本帧新增/删除对象（增量同步到所有连接）；
//        b. NotifyPrioritizersOfDirtyObjects：把脏对象按 prioritizer 分桶批量调用 UpdateObjects；
//        c. 对每个有视图的连接：
//           - PrePrioritize 一次；
//           - 用 FPrioritizerBatchHelper 把该连接的 ObjectsRequiringPriorityUpdate 按 prioritizer 切片（每批 1024）；
//           - 调用 Prioritize；
//           - 把结果数组指针交给 ReplicationWriter::UpdatePriorities（决定 Write 顺序）；
//           - 可选：把 Controller / ViewTarget 的优先级强制设为 ViewTargetHighPriority=1e7。
//        d. PostPrioritize 一次。
//
// 与 ReplicationWriter 的协作：每对象在 Writer 中有一个 PerObjectInfo，accumulator 按帧累加优先级；
//   被实际发送后清零；Writer 据此挑选下一帧要写的对象。本调度器只负责"刷新数值"，不直接决定何时发送。
// =============================================================================================================================

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Iris/ReplicationSystem/Prioritization/NetObjectPrioritizer.h"
#include "Net/Core/NetBitArray.h"
#include "UObject/StrongObjectPtr.h"

class UNetObjectPrioritizerDefinitions;
class UReplicationSystem;
namespace UE::Net
{
	class FNetBitArrayView;
	namespace Private
	{
		class FNetRefHandleManager;
		class FReplicationConnections;

		typedef uint32 FInternalNetRefIndex;
	}

	// For testing
	class FTestNetObjectPrioritizerFixture;
}

namespace UE::Net::Private
{

// FReplicationPrioritization 的初始化参数。从 FReplicationSystemImpl::Init 中传入。
struct FReplicationPrioritizationInitParams
{
	TObjectPtr<const UReplicationSystem> ReplicationSystem;
	const FNetRefHandleManager* NetRefHandleManager = nullptr;       // 中央对象登记表（提供 Scoped/Prev/State 等位图）
	FReplicationConnections* Connections = nullptr;                  // 所有连接（提供 ReplicationView / ReplicationWriter）
	FInternalNetRefIndex MaxInternalNetRefIndex = 0;                 // 当前对象索引上限（动态增长）
};

// =============================================================================================================================
// FReplicationPrioritization：Prioritization 调度器。
// =============================================================================================================================
class FReplicationPrioritization
{
public:
	FReplicationPrioritization();

	void Init(FReplicationPrioritizationInitParams& Params);
	void Deinit();

	/** Called when the maximum InternalNetRefIndex increased and we need to realloc our lists */
	// 系统对象索引上限增长：扩容内部所有 per-object 数组（含每连接的 Priorities），并通知所有 prioritizer。
	void OnMaxInternalNetRefIndexIncreased(FInternalNetRefIndex NewMaxInternalIndex);

	// 帧驱动入口：见文件头算法。
	// ConnectionsToSend：需要发送的连接位图（由 Filtering 阶段决定）。
	// DirtyObjectsThisFrame：本帧脏对象位图（来自 DirtyNetObjectTracker）。
	void Prioritize(const FNetBitArrayView& ConnectionsToSend, const FNetBitArrayView& DirtyObjectsThisFrame);

	// 设置「静态默认优先级」（不绑定任何 prioritizer，每帧使用此固定值）。Prio 必须 ≥ 0。
	void SetStaticPriority(uint32 ObjectIndex, float Prio);
	// 把对象绑定到指定 prioritizer。返回 false 表示该 prioritizer 拒绝该对象（缺 RepTag_WorldLocation 等）。
	// 内部会先 RemoveObject 旧 prioritizer 再 AddObject 新 prioritizer。
	bool SetPrioritizer(uint32 ObjectIndex, FNetObjectPrioritizerHandle Prioritizer);
	// 按名字查找句柄。"DefaultPrioritizer" 名映射为 DefaultSpatialNetObjectPrioritizerHandle(=0)。
	FNetObjectPrioritizerHandle GetPrioritizerHandle(const FName PrioritizerName) const;
	UNetObjectPrioritizer* GetPrioritizer(const FName PrioritizerName) const;

	// 连接生命周期：把连接加入 / 移出每个 prioritizer 的 per-connection 状态。
	void AddConnection(uint32 ConnectionId);
	void RemoveConnection(uint32 ConnectionId);

	// 调试/测试：返回单个连接对单个对象的当前优先级累加值。
	float GetObjectPriorityForConnection(uint32 ConnectionId, FInternalNetRefIndex InternalIndex) const
	{
		return GetPrioritiesForConnection(ConnectionId)[InternalIndex];
	}

private:
	// 内部辅助类：把对象按 prioritizer 分桶切片（每批 ≤ 1024，对应 FNetObjectPrioritizationParams 一次调用）。
	class FPrioritizerBatchHelper;
	// 内部辅助类：把脏对象按 prioritizer 分桶（用于 NotifyPrioritizersOfDirtyObjects → UpdateObjects；每批 ≤ 512）。
	class FUpdateDirtyObjectsBatchHelper;

	// 设置 per-object 数组容量（NetObjectPrioritizationInfos / ObjectIndexToPrioritizer / DefaultPriorities / ObjectsWithNewStaticPriority）。
	void SetNetObjectListsSize(FInternalNetRefIndex MaxInternalIndex);
	// 扩容某个 priorities 数组，并把新元素填为 DefaultPriority(=1.0f)。
	void ResizePrioritiesList(TArray<float>& OutPriorities, FInternalNetRefIndex MaxInternalIndex);

	// 处理本帧新增 / 删除对象。
	// - 删除：解绑 prioritizer，恢复 DefaultPriority；
	// - 新增（仅当存在连接）：把 DefaultPriorities 拷贝到所有连接的 Priorities 数组。
	// 同时合并 ObjectsWithNewStaticPriority（SetStaticPriority 标记的需要广播的对象）。
	void UpdatePrioritiesForNewAndDeletedObjects();

	// 单连接打分主流程：见 cpp 注释。
	void PrioritizeForConnection(uint32 ConnId, FPrioritizerBatchHelper& BatchHelper, FNetBitArrayView Objects);

	// 把视图目标（Controller / ViewTarget）的优先级强制设为 ViewTargetHighPriority(=1e7)。
	// 受 CVar net.Iris.ForceConnectionViewerPriority 控制。
	void SetHighPriorityOnViewTargets(const TArrayView<float>& Priorities, const FReplicationView& View);

	// 把全帧脏对象按 prioritizer 分桶 + 切片调用 UpdateObjects（让 prioritizer 刷新缓存，例如 Location）。
	void NotifyPrioritizersOfDirtyObjects(const FNetBitArrayView& DirtyObjectsThisFrame);
	void BatchNotifyPrioritizersOfDirtyObjects(FUpdateDirtyObjectsBatchHelper& BatchHelper, uint32* ObjectIndices, uint32 ObjectCount);

	// 读取 ini → 创建所有 prioritizer 实例并 Init。仅在 Init() 中调用一次。
	void InitPrioritizers();

private:
	friend UE::Net::FTestNetObjectPrioritizerFixture;

	// For testing
	TConstArrayView<float> GetPrioritiesForConnection(uint32 ConnectionId) const;

	// 单个 prioritizer 的运行时信息。
	struct FPrioritizerInfo
	{
		TStrongObjectPtr<UNetObjectPrioritizer> Prioritizer;  // ini 创建的实例（StrongObjectPtr 保活）
		FName Name;                                           // ini 中的 PrioritizerName
		uint32 ObjectCount;                                   // 当前绑定到此 prioritizer 的对象数
	};

	// 单个连接的 Prioritization 状态。
	struct FPerConnectionInfo
	{
		FPerConnectionInfo() : NextObjectIndexToProcess(0), IsValid(0) {}

		// per-object 优先级累加数组。下标 = InternalNetRefIndex。每帧由 prioritizer 写入；被 ReplicationWriter 读后清零。
		TArray<float> Priorities;
		// 节流时的下次起点（保留字段，目前未启用全帧节流）。
		uint32 NextObjectIndexToProcess;
		// 标志该连接是否有效（AddConnection=1, RemoveConnection=0）。
		uint32 IsValid : 1;
	};

	// 静态默认优先级（无 prioritizer 时使用）。每帧不变。
	static constexpr float DefaultPriority = 1.0f;
	// 视图目标专用强制高优先级（CVar 开启时使用）。1e7 远大于一切 prioritizer 输出。
	static constexpr float ViewTargetHighPriority = 1.0E7f;

	TObjectPtr<const UReplicationSystem> ReplicationSystem = nullptr;
	FReplicationConnections* Connections = nullptr;
	const FNetRefHandleManager* NetRefHandleManager = nullptr;

	// ini 注册表（StrongObjectPtr 保活，Deinit 时释放）。
	TStrongObjectPtr<UNetObjectPrioritizerDefinitions> PrioritizerDefinitions;

	// 每对象的 16 字节 prioritizer 私有缓存（被多个 prioritizer 复用同一槽位）。
	TArray<FNetObjectPrioritizationInfo> NetObjectPrioritizationInfos;
	// 每对象 → prioritizer 索引（PrioritizerInfos 中的下标）。0xFF = 使用静态默认优先级。
	TArray<uint8> ObjectIndexToPrioritizer;
	// 所有 prioritizer 实例。
	TArray<FPrioritizerInfo> PrioritizerInfos;
	// 每连接的状态。下标 = ConnectionId。
	TArray<FPerConnectionInfo> ConnectionInfos;
	// 每对象的静态默认优先级（SetStaticPriority 写入；Prioritize 内每批用作 reset 值）。
	TArray<float> DefaultPriorities;
	// 标记本帧静态优先级被改过的对象——会在 UpdatePrioritiesForNewAndDeletedObjects 阶段广播到所有连接。
	FNetBitArray ObjectsWithNewStaticPriority;

	FInternalNetRefIndex MaxInternalNetRefIndex = 0;

	uint32 ConnectionCount = 0;
	uint32 HasNewObjectsWithStaticPriority : 1;     // 优化标志：避免每帧扫描 ObjectsWithNewStaticPriority。
};

inline TConstArrayView<float> FReplicationPrioritization::GetPrioritiesForConnection(uint32 ConnectionId) const
{
	return MakeArrayView(ConnectionInfos[ConnectionId].Priorities);
}

}
