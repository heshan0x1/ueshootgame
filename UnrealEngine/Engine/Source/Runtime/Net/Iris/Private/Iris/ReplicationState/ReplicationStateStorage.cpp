// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/ReplicationState/ReplicationStateStorage.h"
#include "Iris/Core/IrisMemoryTracker.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"
#include "Iris/ReplicationSystem/ReplicationOperations.h"
#include "Iris/ReplicationSystem/ReplicationOperationsInternal.h"
#include "Iris/ReplicationSystem/ReplicationProtocol.h"
#include "HAL/PlatformMath.h"
#include "HAL/UnrealMemory.h"
#include <limits>

// =============================================================================================
// 中文说明：FReplicationStateStorage 实现
// =============================================================================================
// Baseline 完整生命周期典型时序（DeltaCompressionBaselineManager 视角）：
//
//   ┌──────────────────────────────────────────────────────────────────────────┐
//   │  Frame N（写包阶段）                                                      │
//   │    1) ReserveBaseline(Object, CurrentSendState)                           │
//   │       → 获得 ReservedStorage（未初始化）+ BaselineBaseStorage（指向 Send buffer）│
//   │    2) ReplicationWriter 把"相对 BaselineBaseStorage 的 delta"写入 packet  │
//   │    3) 若写入成功：CommitBaselineReservation(Object, Storage, CurrentSendState)│
//   │       → 此时才执行 CloneState，从 SendState 真正拷贝一份内容到 Reserved    │
//   │       存储中，作为后续 delta 的参考；                                       │
//   │       若写入失败 / 包丢弃：CancelBaselineReservation → 直接 Free。          │
//   └──────────────────────────────────────────────────────────────────────────┘
//
//   后续帧：基于该 baseline 计算 delta；ACK 收到/超时后调用 FreeBaseline 释放。
//
// 之所以采用"先 Reserve 后 Commit"的两阶段：避免每条连接都先克隆完整 state 再决定要不要用——
// 当 delta 包因为优先级被丢弃时可以零开销 Cancel。Commit 时才真正 CloneState 是为了减少
// 不必要的 dynamic state 深拷贝。
// =============================================================================================

namespace UE::Net
{

// 中文：构造函数——只做编译期常量校验。真正的初始化在 Init 中完成。
FReplicationStateStorage::FReplicationStateStorage()
{
	static_assert(InvalidObjectInfoIndex == 0, "This class assumes InvalidObjectInfoIndex == 0");
}

FReplicationStateStorage::~FReplicationStateStorage()
{
}

// 中文：Init —— 计算 PerObjectInfo 的最大数量（受 uint16 索引、MaxObjectCount、
//       MaxDeltaCompressedObjectCount 三者下限），分配位图与映射表，构造 SerializationContext。
//       注意 UsedPerObjectInfos.SetBit(0) 把 0 号槽位标记为占用——0 是"无效"哨兵。
void FReplicationStateStorage::Init(FReplicationStateStorageInitParams& InitParams)
{
	const uint32 MaxDeltaCompressedObjectInfoCount = 1U + FPlatformMath::Min(uint32(std::numeric_limits<ObjectInfoIndexType>::max()), FPlatformMath::Min(InitParams.MaxObjectCount, InitParams.MaxDeltaCompressedObjectCount));

	// Make sure the MaxObjectInfoCount calculation can't overflow.
	static_assert(std::numeric_limits<decltype(MaxDeltaCompressedObjectInfoCount)>::max() > std::numeric_limits<ObjectInfoIndexType>::max(), "");

	NetRefHandleManager = InitParams.NetRefHandleManager;

	UsedPerObjectInfos.Init(MaxDeltaCompressedObjectInfoCount);
	UsedPerObjectInfos.SetBit(InvalidObjectInfoIndex);
	
	ObjectIndexToObjectInfoIndex.SetNumZeroed(InitParams.MaxInternalNetRefIndex);

	InternalSerializationContext = Private::FInternalNetSerializationContext(InitParams.ReplicationSystem);
	SerializationContext.SetInternalContext(&InternalSerializationContext);
}

// 中文：反初始化——校验所有 baseline 都已被 Free。注意此处不主动 Free（调用方必须保持平衡）。
void FReplicationStateStorage::Deinit()
{
#if UE_NET_VALIDATE_DC_BASELINES
	check(BaselineStorageValidation.Num() == 0);
#endif
}

// 中文：扩容索引表——当 NetRefHandleManager 的最大 NetRefIndex 增长时，
//       这里的 ObjectIndexToObjectInfoIndex 也要同步扩容（PerObjectInfo 池本身受 uint16 上限限制，
//       不会扩容）。
void FReplicationStateStorage::OnMaxInternalNetRefIndexIncreased(UE::Net::Private::FInternalNetRefIndex NewMaxInternalIndex)
{
	ObjectIndexToObjectInfoIndex.SetNumZeroed(NewMaxInternalIndex);
}

// 中文：GetState —— 提供"当前 SendState/RecvState"的只读视图。
//   1) 快路径：若 PerObjectInfo 已存在（说明该对象有 baseline 或正在 baseline 操作），直接读缓存；
//   2) 慢路径：否则去 NetRefHandleManager 现拿——避免为不参与 DC 的对象常驻 PerObjectInfo。
const uint8* FReplicationStateStorage::GetState(uint32 ObjectIndex, EReplicationStateType StateType) const
{
	if (StateType != EReplicationStateType::CurrentSendState && StateType != EReplicationStateType::CurrentRecvState)
	{
		return nullptr;
	}

	if (const FPerObjectInfo* ObjectInfo = GetPerObjectInfoForObject(ObjectIndex))
	{
		switch (StateType)
		{
			case EReplicationStateType::CurrentSendState:
			{
				return ObjectInfo->StateBuffers[(unsigned)EStateBufferType::SendState];
			}
			case EReplicationStateType::CurrentRecvState:
			{
				return ObjectInfo->StateBuffers[(unsigned)EStateBufferType::RecvState];
			}
			default:
			{
				checkf(false, TEXT("Unknown EReplicationStateType"));
				return nullptr;
			}
		}
	}

	// Slow path but we don't want to keep PerObjectInfo around unless we have to.
	switch (StateType)
	{
		case EReplicationStateType::CurrentSendState:
		{
			return NetRefHandleManager->GetReplicatedObjectStateBufferNoCheck(ObjectIndex);
		}
		case EReplicationStateType::CurrentRecvState:
		{
			const Private::FNetRefHandleManager::FReplicatedObjectData& ReplicatedObjectData = NetRefHandleManager->GetReplicatedObjectData(ObjectIndex);
			return ReplicatedObjectData.ReceiveStateBuffer;
		}
		default:
		{
			checkf(false, TEXT("Unknown EReplicationStateType"));
			return nullptr;
		}
	}
}

// 中文：AllocBaseline —— 一次性分配 + 初始化基线缓冲。
//   流程：
//     1) GetOrCreatePerObjectInfoForObject 拿到/创建 PerObjectInfo；若超出 PerObjectInfo 上限
//        （uint16 容量耗尽）则 ensure 报错并返回 nullptr；
//     2) Malloc 按 Protocol->InternalTotalSize/Alignment 分配；
//     3) 按 Base 决定填充：Uninitialized/Zeroed/DefaultState/CurrentSendState/CurrentRecvState；
//     4) ++AllocationCount 用于后续 FreePerObjectInfoForObject 的归零判断；
//     5) DC 校验模式下登记到 BaselineStorageValidation。
//   返回原始指针（已按需初始化），调用方用 FreeBaseline 释放。
uint8* FReplicationStateStorage::AllocBaseline(uint32 ObjectIndex, EReplicationStateType Base)
{
	LLM_SCOPE_BYTAG(IrisState);

	FPerObjectInfo* ObjectInfo = GetOrCreatePerObjectInfoForObject(ObjectIndex);
	if (!ensureMsgf(ObjectInfo != nullptr, TEXT("Out of object infos. Max number of object infos is %u."), UsedPerObjectInfos.GetNumBits()))
	{
		return nullptr;
	}

	const FReplicationProtocol* Protocol = ObjectInfo->Protocol;

	uint8* Storage = nullptr;
	switch (Base)
	{
		case EReplicationStateType::UninitializedState:
		{
			// 中文：仅分配，调用方需在 FreeBaseline 之前自行写入合法内容（含 dynamic 指针正确状态）。
			Storage = static_cast<uint8*>(FMemory::Malloc(Protocol->InternalTotalSize, Protocol->InternalTotalAlignment));
			break;
		};

		case EReplicationStateType::ZeroedState:
		{
			// 中文：MallocZeroed —— 适用于"全零基线"（Quantized 表示下"未发送过任何状态"等价于全 0）。
			Storage = static_cast<uint8*>(FMemory::MallocZeroed(Protocol->InternalTotalSize, Protocol->InternalTotalAlignment));
			break;
		};

		case EReplicationStateType::DefaultState:
		{
			// 中文：分配后用 Protocol 默认 state 初始化（处理 dynamic state 的深拷贝）。
			Storage = static_cast<uint8*>(FMemory::Malloc(Protocol->InternalTotalSize, Protocol->InternalTotalAlignment));
			CloneDefaultState(ObjectInfo->Protocol, Storage);
			break;
		};

		case EReplicationStateType::CurrentSendState:
		{
			// 中文：克隆当前 SendState（DeltaCompression 最常用：写完包后把状态冻结成基线）。
			Storage = static_cast<uint8*>(FMemory::Malloc(Protocol->InternalTotalSize, Protocol->InternalTotalAlignment));
			CloneState(Protocol, Storage, ObjectInfo->StateBuffers[(unsigned)EStateBufferType::SendState]);
			break;
		};

		case EReplicationStateType::CurrentRecvState:
		{
			// 中文：克隆当前 RecvState（客户端 delta 解压所需的"上一个解压完毕状态"）。
			Storage = static_cast<uint8*>(FMemory::Malloc(Protocol->InternalTotalSize, Protocol->InternalTotalAlignment));
			CloneState(Protocol, Storage, ObjectInfo->StateBuffers[(unsigned)EStateBufferType::RecvState]);
			break;
		}

		default:
		{
			check(false);
			return nullptr;
		}
	};

	++ObjectInfo->AllocationCount;

#if UE_NET_VALIDATE_DC_BASELINES
	{
		check(Storage != nullptr);
		BaselineStorageValidation.Add(ObjectIndex, Storage);
	}
#endif

	return static_cast<uint8*>(Storage);
}

// 中文：FreeBaseline —— 释放 AllocBaseline / Commit 出来的基线。
//   1) 调 FreeState 释放嵌套动态内存（仅 Protocol HasDynamicState 时）；
//   2) Free 顶层指针；
//   3) --AllocationCount，归零则回收 PerObjectInfo（避免占用 uint16 槽位）。
void FReplicationStateStorage::FreeBaseline(uint32 ObjectIndex, uint8* Storage)
{
	FPerObjectInfo* ObjectInfo = GetPerObjectInfoForObject(ObjectIndex);
	if (!ensureMsgf(ObjectInfo != nullptr, TEXT("Trying to free baseline storage pointer %p for object ( InternalIndex: %u ) with no PerObjectInfo."), Storage, ObjectIndex))
	{
		return;
	}

#if UE_NET_VALIDATE_DC_BASELINES
	{
		const int32 RemovedCount = BaselineStorageValidation.RemoveSingle(ObjectIndex, Storage);
		check(RemovedCount == 1);
	}
#endif

	FreeState(ObjectInfo->Protocol, Storage);
	FMemory::Free(static_cast<void*>(Storage));

	if (0 == --ObjectInfo->AllocationCount)
	{
		FreePerObjectInfoForObject(ObjectIndex);
	}
}

// 中文：ReserveBaseline —— 两阶段分配的"第一阶段"。
//   - 仅 Malloc 未初始化内存（不做 CloneState/CloneDefault）；
//   - 若 Base 是 Current(Send|Recv)State，把基础源指针记录在 BaselineBaseStorage，
//     便于 ReplicationWriter 在写入 delta 时直接使用源数据；
//   - 失败回退路径：若 Malloc 返回 nullptr 且本次创建了 PerObjectInfo，需要回收槽位
//     并返回空 Reservation。
FReplicationStateStorage::FBaselineReservation FReplicationStateStorage::ReserveBaseline(uint32 ObjectIndex, EReplicationStateType Base)
{
	LLM_SCOPE_BYTAG(IrisState);

	FBaselineReservation Reservation;

	FPerObjectInfo* ObjectInfo = GetOrCreatePerObjectInfoForObject(ObjectIndex);
	if (!ensureMsgf(ObjectInfo != nullptr, TEXT("Out of object infos. Max number of object infos is %u."), UsedPerObjectInfos.GetNumBits()))
	{
		return Reservation;
	}

	if (Base == EReplicationStateType::CurrentSendState)
	{
		Reservation.BaselineBaseStorage = ObjectInfo->StateBuffers[unsigned(EStateBufferType::SendState)];
#if UE_NET_VALIDATE_DC_BASELINES
		if (!ensureAlways(Reservation.BaselineBaseStorage != nullptr))
		{
			return Reservation;
		}
#endif
	}
	else if (Base == EReplicationStateType::CurrentRecvState)
	{
		Reservation.BaselineBaseStorage = ObjectInfo->StateBuffers[unsigned(EStateBufferType::RecvState)];
#if UE_NET_VALIDATE_DC_BASELINES
		if (!ensureAlways(Reservation.BaselineBaseStorage != nullptr))
		{
			return Reservation;
		}
#endif
	}

	const FReplicationProtocol* Protocol = ObjectInfo->Protocol;
	Reservation.ReservedStorage = static_cast<uint8*>(FMemory::Malloc(Protocol->InternalTotalSize, Protocol->InternalTotalAlignment));
	if (Reservation.ReservedStorage == nullptr)
	{
		// 中文：失败兜底——如果是本次刚创建的 PerObjectInfo（AllocationCount 还是 0），把它回收掉。
		if (ObjectInfo->AllocationCount == 0)
		{
			FreePerObjectInfoForObject(ObjectIndex);
		}
		return FBaselineReservation();
	}

	++ObjectInfo->AllocationCount;

#if UE_NET_VALIDATE_DC_BASELINES
	{
		BaselineStorageValidation.Add(ObjectIndex, Reservation.ReservedStorage);
	}
#endif

	return Reservation;
}

// 中文：CancelBaselineReservation —— 取消未提交的 Reservation。
//   注意：Reserve 出来的内存还未初始化，因此不能 FreeState（会触发未初始化指针的释放），
//   直接 FMemory::Free 即可。
void FReplicationStateStorage::CancelBaselineReservation(uint32 ObjectIndex, uint8* Storage)
{
	FPerObjectInfo* ObjectInfo = GetPerObjectInfoForObject(ObjectIndex);
	if (!ensureMsgf(ObjectInfo != nullptr, TEXT("Trying to cancel a baseline reservation with pointer %p for object ( InternalIndex: %u ) with no PerObjectInfo."), Storage, ObjectIndex))
	{
		return;
	}

#if UE_NET_VALIDATE_DC_BASELINES
	{
		const int32 RemovedCount = BaselineStorageValidation.RemoveSingle(ObjectIndex, Storage);
		check(RemovedCount == 1);
	}
#endif

	FMemory::Free(static_cast<void*>(Storage));

	if (0 == --ObjectInfo->AllocationCount)
	{
		FreePerObjectInfoForObject(ObjectIndex);
	}
}

// 中文：CommitBaselineReservation —— 把 Reserve 出来的"未初始化内存"按 Base 填充内容。
//   提交后才正式成为可用 baseline，需经 FreeBaseline 释放。
//   注意 Uninitialized 路径下 Commit 是 no-op（调用方已自行写好内容）。
void FReplicationStateStorage::CommitBaselineReservation(uint32 ObjectIndex, uint8* Storage, EReplicationStateType Base)
{
#if UE_NET_VALIDATE_DC_BASELINES
	{
		check(BaselineStorageValidation.FindPair(ObjectIndex, Storage) != nullptr);
	}
#endif

	FPerObjectInfo* ObjectInfo = GetPerObjectInfoForObject(ObjectIndex);
	if (!ensureMsgf(ObjectInfo != nullptr, TEXT("Trying to commit baseline storage pointer %p for object ( InternalIndex: %u ) with no PerObjectInfo."), Storage, ObjectIndex))
	{
		return;
	}

	const FReplicationProtocol* Protocol = ObjectInfo->Protocol;

	switch (Base)
	{
		case EReplicationStateType::UninitializedState:
		{
			// 中文：调用方已自填——什么都不做。
			break;
		};

		case EReplicationStateType::ZeroedState:
		{
			FMemory::Memzero(Storage, Protocol->InternalTotalSize);
			break;
		};

		case EReplicationStateType::DefaultState:
		{
			CloneDefaultState(ObjectInfo->Protocol, Storage);
			break;
		};

		case EReplicationStateType::CurrentSendState:
		{
			CloneState(Protocol, Storage, ObjectInfo->StateBuffers[(unsigned)EStateBufferType::SendState]);
			break;
		};

		case EReplicationStateType::CurrentRecvState:
		{
			CloneState(Protocol, Storage, ObjectInfo->StateBuffers[(unsigned)EStateBufferType::RecvState]);
			break;
		}

		default:
		{
			break;
		}
	};
}

// 中文：GetOrCreatePerObjectInfoForObject —— 按需创建。
//   首次创建时从 NetRefHandleManager 拷贝 Protocol / SendStateBuffer / ReceiveStateBuffer 指针缓存，
//   后续 GetState / Alloc / Reserve / Free 都依赖这份缓存避免重复查询。
FReplicationStateStorage::FPerObjectInfo* FReplicationStateStorage::GetOrCreatePerObjectInfoForObject(uint32 ObjectIndex)
{
	ObjectInfoIndexType& InfoIndex = ObjectIndexToObjectInfoIndex[ObjectIndex];
	if (InfoIndex == InvalidObjectInfoIndex)
	{
		const ObjectInfoIndexType NewInfoIndex = AllocPerObjectInfo();
		if (NewInfoIndex == InvalidObjectInfoIndex)
		{
			return nullptr;
		}
		InfoIndex = NewInfoIndex;

		FPerObjectInfo& ObjectInfo = ObjectInfos[NewInfoIndex];
		
		// Cache some info
		const Private::FNetRefHandleManager::FReplicatedObjectData& ReplicatedObjectData = NetRefHandleManager->GetReplicatedObjectData(ObjectIndex);
		const FReplicationProtocol* Protocol = ReplicatedObjectData.Protocol;
		ObjectInfo.Protocol = Protocol;
		ObjectInfo.StateBuffers[(unsigned)EStateBufferType::SendState] = NetRefHandleManager->GetReplicatedObjectStateBufferNoCheck(ObjectIndex);
		ObjectInfo.StateBuffers[(unsigned)EStateBufferType::RecvState] = ReplicatedObjectData.ReceiveStateBuffer;
		return &ObjectInfo;
	}

	return &ObjectInfos[InfoIndex];
}

// 中文：GetPerObjectInfoForObject —— 仅查询（不创建）。返回 nullptr 表示该对象当前未参与 baseline。
FReplicationStateStorage::FPerObjectInfo* FReplicationStateStorage::GetPerObjectInfoForObject(uint32 ObjectIndex)
{
	const ObjectInfoIndexType InfoIndex = ObjectIndexToObjectInfoIndex[ObjectIndex];
	if (InfoIndex == InvalidObjectInfoIndex)
	{
		return nullptr;
	}

	return &ObjectInfos[InfoIndex];
}

const FReplicationStateStorage::FPerObjectInfo* FReplicationStateStorage::GetPerObjectInfoForObject(uint32 ObjectIndex) const
{
	const ObjectInfoIndexType InfoIndex = ObjectIndexToObjectInfoIndex[ObjectIndex];
	if (InfoIndex == InvalidObjectInfoIndex)
	{
		return nullptr;
	}

	return &ObjectInfos[InfoIndex];
}

// 中文：FreePerObjectInfoForObject —— 释放映射并把槽位归还位图。AllocationCount 归零时调用。
void FReplicationStateStorage::FreePerObjectInfoForObject(uint32 ObjectIndex)
{
	ObjectInfoIndexType& InfoIndex = ObjectIndexToObjectInfoIndex[ObjectIndex];
	if (InfoIndex != InvalidObjectInfoIndex)
	{
		FreePerObjectInfo(InfoIndex);
		InfoIndex = InvalidObjectInfoIndex;
	}
}

// 中文：AllocPerObjectInfo —— 从 UsedPerObjectInfos 位图中 FindFirstZero 拿空槽。
//   若槽位 >= 已构造 PerObjectInfo 数量，按 ObjectInfoGrowCount=256 一次性扩 ChunkedArray；
//   命中后置位 + reset PerObjectInfo（清零字段）。返回 InvalidObjectInfoIndex(0) 表示池满。
FReplicationStateStorage::ObjectInfoIndexType FReplicationStateStorage::AllocPerObjectInfo()
{
	const uint32 InfoIndex = UsedPerObjectInfos.FindFirstZero();
	if (InfoIndex != FNetBitArray::InvalidIndex)
	{
		UsedPerObjectInfos.SetBit(InfoIndex);

		if (InfoIndex >= uint32(ObjectInfos.Num()))
		{
			ObjectInfos.Add(ObjectInfoGrowCount);
		}

		FPerObjectInfo& ObjectInfo = ObjectInfos[InfoIndex];
		ObjectInfo = FPerObjectInfo();

		return static_cast<ObjectInfoIndexType>(InfoIndex);
	}

	return InvalidObjectInfoIndex;
}

// 中文：FreePerObjectInfo —— 仅清位图位（PerObjectInfo 内容会在下次 Alloc 时被重置）。
void FReplicationStateStorage::FreePerObjectInfo(ObjectInfoIndexType Index)
{
	UsedPerObjectInfos.ClearBit(Index);
}

// 中文：CloneState —— 委托给 ProtocolOperations，按 Protocol 描述深拷贝 quantized state。
//       会处理嵌套的 dynamic state（FString / TArray 等内部存储的指针）。
void FReplicationStateStorage::CloneState(const FReplicationProtocol* Protocol, uint8* TargetState, const uint8* SourceState)
{
	Private::FReplicationProtocolOperationsInternal::CloneQuantizedState(SerializationContext, TargetState, SourceState, Protocol);
}

// 中文：CloneDefaultState —— 用 Protocol 中存储的"默认状态"初始化 TargetState。
//       N.B. 注释提示 InitializeFromDefaultState 内部已包含 memcpy 与 dynamic state 拷贝。
void FReplicationStateStorage::CloneDefaultState(const FReplicationProtocol* Protocol, uint8* TargetState)
{
	// N.B. InitializeFromDefaultState does the required memcpying internally.
	FReplicationProtocolOperations::InitializeFromDefaultState(SerializationContext, TargetState, Protocol);
	check(!SerializationContext.HasError());
}

// 中文：FreeState —— 仅释放 State 中嵌套的动态内存（如 FString/TArray 内部 buffer），
//       不释放 State 自身的顶层指针（由调用方 FMemory::Free）。
//       Protocol 不含 HasDynamicState 时是 no-op。
void FReplicationStateStorage::FreeState(const FReplicationProtocol* Protocol, uint8* State)
{
	if (EnumHasAnyFlags(Protocol->ProtocolTraits, EReplicationProtocolTraits::HasDynamicState))
	{
		Private::FReplicationProtocolOperationsInternal::FreeDynamicState(SerializationContext, State, Protocol);
		check(!SerializationContext.HasError());
	}
}

}
