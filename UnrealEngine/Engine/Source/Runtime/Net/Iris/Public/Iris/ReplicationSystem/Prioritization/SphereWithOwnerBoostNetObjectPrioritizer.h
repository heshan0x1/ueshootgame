// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：USphereWithOwnerBoostNetObjectPrioritizer —— 在 USphereNetObjectPrioritizer 基础上增加 Owner 加成。
// 加成规则：
//   if (Connection.Id == Object.OwningConnection) priority += OwnerPriorityBoost
// 用途：让玩家自己拥有的对象（如自己的角色 / 武器 / Pawn）获得更高优先级，确保第一时间收到。
// 实现要点：
//   - OwningConnection 来自 FReplicationFiltering::GetOwningConnection（由 UReplicationSystem::SetOwningNetConnection 维护）；
//   - 复用基类 16 字节槽的 LocationIndex（用同一槽位寻址 OwningConnections 数组），所以基类 AllocLocation 与本类
//     AllocOwningConnection 产出的 index 必须一致（见 AddObject 中的 checkSlow）；
//   - 在 PrepareBatch 阶段把"本连接拥有的对象"的本地索引收集到 OwnedObjectsLocalIndices；
//   - 调用基类 PrioritizeBatch 完成球体打分；
//   - 末尾 BoostOwningConnectionPriorities 把这些索引上的优先级直接 += OwnerPriorityBoost。
// =============================================================================================================================

#pragma once

#include "Iris/ReplicationSystem/Prioritization/SphereNetObjectPrioritizer.h"
#include "UObject/ObjectPtr.h"
#include "SphereWithOwnerBoostNetObjectPrioritizer.generated.h"

// ini 配置：继承 SphereConfig，仅多一个 OwnerPriorityBoost 字段。
UCLASS(Transient, Config=Engine, MinimalAPI)
class USphereWithOwnerBoostNetObjectPrioritizerConfig : public USphereNetObjectPrioritizerConfig
{
	GENERATED_BODY()

public:
	UPROPERTY(Config)
	/** Priority boost for the owning connection. Added to the priority calculated by the sphere prioritizer. */
	// Owner 加成值（直接相加，不是相乘）。注意是"在球体公式之上叠加"，所以可以让原本 OutsidePriority(0.1) 的 owned
	// 对象瞬间跳到 2.1，从而每帧都会被 ReplicationWriter 优先发送。
	float OwnerPriorityBoost = 2.0f;
};

UCLASS(Transient, MinimalAPI)
class USphereWithOwnerBoostNetObjectPrioritizer : public USphereNetObjectPrioritizer
{
	GENERATED_BODY()

protected:
	// UNetObjectPrioritizer interface
	IRISCORE_API virtual void Init(FNetObjectPrioritizerInitParams& Params) override;
	IRISCORE_API virtual void Deinit() override;
	IRISCORE_API virtual bool AddObject(uint32 ObjectIndex, FNetObjectPrioritizerAddObjectParams& Params) override;
	IRISCORE_API virtual void RemoveObject(uint32 ObjectIndex, const FNetObjectPrioritizationInfo& Info) override;
	IRISCORE_API virtual void UpdateObjects(FNetObjectPrioritizerUpdateParams&) override;
	IRISCORE_API virtual void Prioritize(FNetObjectPrioritizationParams&) override;

protected:
	using ConnectionId = uint16;
	enum : unsigned
	{
		/** We're not expecting a huge number of objects being added to this prioritizer. A connection ID is two bytes. */
		// OwningConnections 按 chunk 增长，每 chunk 2KB / 2B = 1024 个 ConnectionId。
		OwningConnectionsChunkSize = 2U*1024U,
		// 0 = 无效连接 ID（OwningConnections[i] = 0 表示该对象没有 owner）。
		InvalidConnectionID = 0U,
	};

	// 本类批参数：在基类 FBatchParams 之上增加 owned 对象列表。
	struct FOwnerBoostBatchParams : public Super::FBatchParams
	{
		/** Batch indices for objects owned by the connection being prioritized for. Access other arrays in the batch via  OtherArray[OwnedObjectsLocalIndices[0..OwnedObjectCount-1]]. */
		// 本批中"被该连接拥有"的对象在 LocalPriorities/Positions 中的下标（不是全局 ObjectIndex）。
		uint32* OwnedObjectsLocalIndices;
		/** The number of owned objects in this batch. Local indices for them can be found in OwnedObjectsLocalIndices. */
		uint32 OwnedObjectCount;
		/** How much to add to the priority for owned objects. */
		float OwnerPriorityBoost;
	};

	// 重写流水线（与基类同形式，但带 owner 信息）。
	void PrepareBatch(FOwnerBoostBatchParams& BatchParams, const FNetObjectPrioritizationParams& PrioritizationParams, uint32 ObjectIndexStartOffset);
	void PrioritizeBatch(FOwnerBoostBatchParams& BatchParams);
	void FinishBatch(const FOwnerBoostBatchParams& BatchParams, FNetObjectPrioritizationParams& PrioritizationParams, uint32 ObjectIndexStartOffset);
	// 末尾对 OwnedObjectsLocalIndices 中的本地索引应用 += OwnerPriorityBoost。
	void BoostOwningConnectionPriorities(FOwnerBoostBatchParams& BatchParams) const;
	void SetupBatchParams(FOwnerBoostBatchParams& OutBatchParams, const FNetObjectPrioritizationParams& PrioritizationParams, uint32 MaxBatchObjectCount, FMemStackBase& Mem);

	// 与基类 AllocLocation 配套——必须保证两者返回同一 index（见 AddObject 中的断言）。
	uint32 AllocOwningConnection();
	void FreeOwningConnection(uint32 Index);

	// 通过 LocationIndex 间接读取 OwningConnections（与位置共享同一索引）。
	uint32 GetOwningConnection(const FObjectLocationInfo& Info) const;

	/** The IDs of the objects' owning connection. */
	// 每个对象的 owning connection ID（uint16）。0 表示无 owner。
	TChunkedArray<ConnectionId, OwningConnectionsChunkSize> OwningConnections;
	/** Which indices in OwningConnections are in use. */
	UE::Net::FNetBitArray AssignedOwningConnectionIndices;

private:
	// UpdateObjects 中查询 ReplicationFiltering 用，缓存以避免每次穿透到 ReplicationSystem。
	TObjectPtr<const UReplicationSystem> ReplicationSystem = nullptr;
};

inline uint32 USphereWithOwnerBoostNetObjectPrioritizer::GetOwningConnection(const FObjectLocationInfo& Info) const
{
	// 复用 LocationIndex 作为 OwningConnections 的下标——前提：基类 AllocLocation 与 AllocOwningConnection 返回相同 index。
	return OwningConnections[Info.GetLocationIndex()];
}
