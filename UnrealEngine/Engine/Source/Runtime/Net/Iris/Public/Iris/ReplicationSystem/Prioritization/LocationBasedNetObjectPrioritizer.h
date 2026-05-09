// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：ULocationBasedNetObjectPrioritizer —— 所有"基于空间位置"打分器的共同基类。
// 职责：
//   - 在 AddObject 时定位对象的世界位置来源：
//       a) 若 FWorldLocations 已知（CullDistance/Tag-less 路径），用 WorldLocations 提供；
//       b) 否则在协议中查找 RepTag_WorldLocation（要求对象状态里有此 Tag），记录 (StateIndex, ExternalStateOffset)。
//   - 维护一个紧凑的 TChunkedArray<VectorRegister> Locations，每对象一个 LocationIndex（位 16 槽）。
//   - 每帧 UpdateObjects（来自 NotifyPrioritizersOfDirtyObjects）会刷新所有脏对象的位置到本地数组。
//   - 派生类（Sphere / SphereWithOwnerBoost / FieldOfView）只用 GetLocation(Info) 读这个 4-float 向量做距离/方向运算。
// 注意：
//   - FObjectLocationInfo 必须与 FNetObjectPrioritizationInfo 同尺寸（8 字节，4×uint16）；占用全部 4 个 Data 槽：
//       Data[0] = LocationStateOffset（外部源缓冲偏移）
//       Data[1] = LocationStateIndex（fragment 索引；65535 表示走 WorldLocations 路径）
//       Data[2..3] = LocationIndex（指向 Locations 数组的 32-bit 索引）
// =============================================================================================================================

#pragma once

#include "Iris/ReplicationSystem/Prioritization/NetObjectPrioritizer.h"
#include "Net/Core/NetBitArray.h"
#include "Math/VectorRegister.h"
#include "UObject/StrongObjectPtr.h"
#include "LocationBasedNetObjectPrioritizer.generated.h"

namespace UE::Net
{
	class FWorldLocations;
}

// 抽象基类：必须由具体 prioritizer（Sphere/FOV/...）继承，单独不可实例化。
UCLASS(Transient, MinimalAPI, Abstract)
class ULocationBasedNetObjectPrioritizer : public UNetObjectPrioritizer
{
	GENERATED_BODY()

protected:
	// UNetObjectPrioritizer interface
	IRISCORE_API virtual void Init(FNetObjectPrioritizerInitParams& Params) override;
	IRISCORE_API virtual void Deinit() override;
	IRISCORE_API virtual void OnMaxInternalNetRefIndexIncreased(uint32 NewMaxInternalIndex) override;
	IRISCORE_API virtual bool AddObject(uint32 ObjectIndex, FNetObjectPrioritizerAddObjectParams& Params) override;
	IRISCORE_API virtual void RemoveObject(uint32 ObjectIndex, const FNetObjectPrioritizationInfo& Info) override;
	IRISCORE_API virtual void UpdateObjects(FNetObjectPrioritizerUpdateParams&) override;

protected:
	IRISCORE_API ULocationBasedNetObjectPrioritizer();

	// FNetObjectPrioritizationInfo 的位置信息视图：把基类提供的 16 字节槽拆解为 (StateOffset, StateIndex, LocationIndex)。
	// 通过 static_cast<FObjectLocationInfo&>(BaseInfo) 直接复用同一块内存（断言尺寸一致）。
	struct FObjectLocationInfo : public FNetObjectPrioritizationInfo
	{
		// 是否走 WorldLocations 路径（StateIndex == InvalidStateIndex 时为真）。
		bool IsUsingWorldLocations() const { return GetLocationStateIndex() == InvalidStateIndex; }
		// 是否走"在状态缓冲中按 Tag 偏移读取"路径。
		bool IsUsingLocationInState() const { return GetLocationStateIndex() != InvalidStateIndex; }

		// External buffer 偏移：ExternalSrcBuffer + Offset 是 FVector 的地址。
		void SetLocationStateOffset(uint16 Offset) { Data[0] = Offset; }
		uint16 GetLocationStateOffset() const { return Data[0]; }

		// 在 InstanceProtocol->FragmentData[] 中的 fragment 索引；65535 表示走 WorldLocations 路径。
		void SetLocationStateIndex(uint16 Index) { Data[1] = Index; }
		uint16 GetLocationStateIndex() const { return Data[1]; }

		// 指向 Locations 数组的 32-bit 索引（拆成两个 uint16 存放）。
		void SetLocationIndex(uint32 Index) { Data[2] = Index & 65535U; Data[3] = Index >> 16U; }
		uint32 GetLocationIndex() const { return (uint32(Data[3]) << 16U) | uint32(Data[2]); }
	};

	// 读取已缓存的位置（每帧 UpdateObjects 末刷新）。
	IRISCORE_API VectorRegister GetLocation(const FObjectLocationInfo& Info) const;
	// 内部使用：写入 Locations[Info.GetLocationIndex()]。
	IRISCORE_API void SetLocation(const FObjectLocationInfo& Info, VectorRegister Location);
	// UpdateObjects 内部使用：根据 Info 走 WorldLocations 或读 Tag 偏移，然后 SetLocation。
	IRISCORE_API void UpdateLocation(const uint32 ObjectIndex, const FObjectLocationInfo& Info, const UE::Net::FReplicationInstanceProtocol* InstanceProtocol);

private:
	enum : unsigned
	{
		// 每个 chunk 64KB（按 4096 个 VectorRegister 分配）。Locations 是 TChunkedArray，按 chunk 增长避免反复 realloc。
		LocationsChunkSize = 64*1024,
		// 哨兵：表示 fragment 索引无效（走 WorldLocations）。
		InvalidStateIndex = 65535U,
		// 哨兵：表示偏移无效。
		InvalidStateOffset = 65535U,
	};

	// 在 Locations 中分配一个空闲槽位（FindFirstZero + chunk 自动扩容）。
	uint32 AllocLocation();
	// 释放一个槽位（仅清位图，数组本身不收缩）。
	void FreeLocation(uint32 Index);

	// 紧凑位置数组：所有绑定到本 prioritizer 的对象共享一份。
	TChunkedArray<VectorRegister, LocationsChunkSize> Locations;
	// AssignedLocationIndices：哪些 Locations 槽位在用（位图 + FindFirstZero 实现 O(words) 的分配）。
	UE::Net::FNetBitArray AssignedLocationIndices;
	// 引用 ReplicationSystem 共享的 WorldLocations（提供 HasInfoForObject / GetWorldLocation）。
	const UE::Net::FWorldLocations* WorldLocations = nullptr;
};
