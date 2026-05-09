// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：ULocationBasedNetObjectPrioritizer 实现。提供位置缓存的"分配 / 刷新 / 读取"骨架；
// 派生类（Sphere/SphereWithOwnerBoost/FOV）只需在 Prioritize 中调用 GetLocation 做距离/方向运算即可。
//
// 关键流程：
//   AddObject     ：决定走 WorldLocations / Tag 偏移哪条路径，分配 LocationIndex，立即 UpdateLocation 一次。
//   RemoveObject  ：释放 LocationIndex。
//   UpdateObjects ：调度器集中传入"本帧脏对象+实例协议"，逐个调 UpdateLocation 把最新位置写入本地缓存。
// =============================================================================================================================

#include "Iris/ReplicationSystem/Prioritization/LocationBasedNetObjectPrioritizer.h"
#include "Iris/ReplicationSystem/RepTag.h"
#include "Iris/ReplicationSystem/ReplicationProtocol.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/WorldLocations.h"
#include "Iris/Serialization/VectorNetSerializers.h"
#include "Iris/Core/IrisProfiler.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LocationBasedNetObjectPrioritizer)

ULocationBasedNetObjectPrioritizer::ULocationBasedNetObjectPrioritizer()
{
	// 编译期断言：FObjectLocationInfo 必须能精确放在基类提供的 8 字节 Data[4] 槽里。
	// 任何派生类禁止扩成员——若需更多元数据，请使用外挂数组 + LocationIndex 间接寻址。
	static_assert(sizeof(FObjectLocationInfo) == sizeof(FNetObjectPrioritizationInfo), "Can't add members to FNetObjectPrioritizationInfo.");
}

void ULocationBasedNetObjectPrioritizer::Init(FNetObjectPrioritizerInitParams& Params)
{
	// 位图按当前最大对象索引分配；OnMaxInternalNetRefIndexIncreased 会扩容。
	AssignedLocationIndices.Init(Params.CurrentMaxInternalIndex);
	// 共享 ReplicationSystem 持有的 WorldLocations（FWorldLocations 是中央位置/CullDistance 存储）。
	WorldLocations = &Params.ReplicationSystem->GetWorldLocations();
}

void ULocationBasedNetObjectPrioritizer::Deinit()
{
	WorldLocations = nullptr;
}

void ULocationBasedNetObjectPrioritizer::OnMaxInternalNetRefIndexIncreased(uint32 NewMaxInternalIndex)
{
	AssignedLocationIndices.SetNumBits(NewMaxInternalIndex);
}

bool ULocationBasedNetObjectPrioritizer::AddObject(uint32 ObjectIndex, FNetObjectPrioritizerAddObjectParams& Params)
{
	// 我们支持两种位置数据来源：
	// 1) 对象状态里有 RepTag_WorldLocation 标记的 FVector 字段；
	// 2) 通过 FWorldLocations 注册了独立的世界位置（典型场景：UObjectReplicationBridge::SetCullDistance 等）。
	UE::Net::FRepTagFindInfo TagInfo;
	bool bHasWorldLocation = false;
	if (WorldLocations->HasInfoForObject(ObjectIndex))
	{
		bHasWorldLocation = true;
		// 用哨兵值标记走 WorldLocations 路径（IsUsingWorldLocations 据此判断）。
		TagInfo.StateIndex = InvalidStateIndex;
		TagInfo.ExternalStateOffset = InvalidStateOffset;
	}
	else if (!UE::Net::FindRepTag(Params.Protocol, UE::Net::RepTag_WorldLocation, TagInfo))
	{
		// 既无 WorldLocations 又无 RepTag_WorldLocation —— 本 prioritizer 无法处理该对象。
		// 调度器会回退到静态默认优先级。
		return false;
	}

	// 状态索引或偏移过大时（>= 65535）我们的 16 位编码无法存储 —— 拒绝。
	if (!bHasWorldLocation && ((TagInfo.ExternalStateOffset >= MAX_uint16) || (TagInfo.StateIndex >= MAX_uint16)))
	{
		return false;
	}

	// 写入 16 字节槽：Offset / StateIndex / LocationIndex。
	FObjectLocationInfo& ObjectInfo = static_cast<FObjectLocationInfo&>(Params.OutInfo);
	ObjectInfo.SetLocationStateOffset(static_cast<uint16>(TagInfo.ExternalStateOffset));
	ObjectInfo.SetLocationStateIndex(static_cast<uint16>(TagInfo.StateIndex));
	const uint32 LocationIndex = AllocLocation();
	ObjectInfo.SetLocationIndex(LocationIndex);

	// 立即写入一次位置，避免 prioritizer 在 UpdateObjects 之前就被 Prioritize 调用读到 0 向量。
	UpdateLocation(ObjectIndex, ObjectInfo, Params.InstanceProtocol);

	return true;
}

void ULocationBasedNetObjectPrioritizer::RemoveObject(uint32 ObjectIndex, const FNetObjectPrioritizationInfo& Info)
{
	const FObjectLocationInfo& ObjectInfo = static_cast<const FObjectLocationInfo&>(Info);
	FreeLocation(ObjectInfo.GetLocationIndex());
}

void ULocationBasedNetObjectPrioritizer::UpdateObjects(FNetObjectPrioritizerUpdateParams& Params)
{
	// 注意：InstanceProtocols 与 ObjectIndices 长度一致但索引方式不同（0..N-1 / 用 ObjectIndices[i] 取信息）。
	for (SIZE_T ObjectIt = 0, ObjectEndIt = Params.ObjectCount; ObjectIt != ObjectEndIt; ++ObjectIt)
	{
		const uint32 ObjectIndex = Params.ObjectIndices[ObjectIt];

		const FObjectLocationInfo& ObjectInfo = static_cast<const FObjectLocationInfo&>(Params.PrioritizationInfos[ObjectIndex]);
		const UE::Net::FReplicationInstanceProtocol* InstanceProtocol = Params.InstanceProtocols[ObjectIt];
		UpdateLocation(ObjectIndex, ObjectInfo, InstanceProtocol);
	}
}

uint32 ULocationBasedNetObjectPrioritizer::AllocLocation()
{
	// 位图扫描找第一个 0 位；如果索引超过当前数组容量则按 chunk 扩容（每 chunk = 64KB / 16B = 4096 个 VectorRegister）。
	uint32 Index = AssignedLocationIndices.FindFirstZero();
	if (Index >= uint32(Locations.Num()))
	{
		constexpr int32 NumElementsPerChunk = LocationsChunkSize / sizeof(VectorRegister);
		Locations.Add(NumElementsPerChunk);
	}

	AssignedLocationIndices.SetBit(Index);
	return Index;
}

void ULocationBasedNetObjectPrioritizer::FreeLocation(uint32 Index)
{
	// 仅清位图。Locations 数组本身保持原大小（避免频繁缩容）。
	AssignedLocationIndices.ClearBit(Index);
}

VectorRegister ULocationBasedNetObjectPrioritizer::GetLocation(const FObjectLocationInfo& Info) const
{
	return Locations[Info.GetLocationIndex()];
}

void ULocationBasedNetObjectPrioritizer::SetLocation(const FObjectLocationInfo& Info, VectorRegister Location)
{
	Locations[Info.GetLocationIndex()] = Location;
}

void ULocationBasedNetObjectPrioritizer::UpdateLocation(const uint32 ObjectIndex, const FObjectLocationInfo& Info, const UE::Net::FReplicationInstanceProtocol* InstanceProtocol)
{
	if (Info.IsUsingWorldLocations())
	{
		// 路径 A：从 WorldLocations 读取（典型用例：CullDistance 系统）。
		const FVector WorldLocation = WorldLocations->GetWorldLocation(ObjectIndex);
		// VectorLoadFloat3_W0：读 3 个 float 到 VectorRegister，第 4 分量补 0（避免脏数据干扰 dot/sub 运算）。
		SetLocation(Info, VectorLoadFloat3_W0(&WorldLocation));
	}
	else
	{
		// 路径 B：从 RepTag_WorldLocation 标记的属性读取——直接从对象的 ExternalSrcBuffer + Offset 读 FVector。
		// 注意：ExternalSrcBuffer 是真实 UObject 内属性的指针（非量化形式），所以可以直接读 FVector。
		TArrayView<const UE::Net::FReplicationInstanceProtocol::FFragmentData> FragmentDatas = MakeArrayView(InstanceProtocol->FragmentData, InstanceProtocol->FragmentCount);
		const UE::Net::FReplicationInstanceProtocol::FFragmentData& FragmentData = FragmentDatas[Info.GetLocationStateIndex()];
		const uint8* LocationOffset = FragmentData.ExternalSrcBuffer + Info.GetLocationStateOffset();
		SetLocation(Info, VectorLoadFloat3_W0(reinterpret_cast<const FVector*>(LocationOffset)));
	}
}
