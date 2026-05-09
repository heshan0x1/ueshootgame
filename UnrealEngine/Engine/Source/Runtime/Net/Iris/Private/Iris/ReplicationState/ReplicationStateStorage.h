// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Iris/IrisConfigInternal.h"
#include "Net/Core/NetBitArray.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Containers/ChunkedArray.h"
#if UE_NET_VALIDATE_DC_BASELINES
#include "Containers/Map.h"
#endif

namespace UE::Net
{
	struct FReplicationProtocol;
	namespace Private
	{
		typedef uint32 FInternalNetRefIndex;
		class FNetRefHandleManager;
	};
}

// =============================================================================================
// 中文说明：FReplicationStateStorage —— per-object 的 SendState/RecvState/Baseline 缓冲管理器
// =============================================================================================
// 职责：
//   1) 通过 GetState 给上层提供"当前发送/接收 state buffer"的只读句柄（其实底层数据归属
//      FNetRefHandleManager 管理，本类只是缓存常用入口）；
//   2) 为 Delta Compression 提供"历史基线（baseline）"快照存储——
//      AllocBaseline / ReserveBaseline+Commit / FreeBaseline / Cancel；
//   3) 协作 FDeltaCompressionBaselineManager：每个 connection 在写包时会 ReserveBaseline，
//      若包成功发送则 Commit（变成持久化基线），失败则 Cancel；ACK 后过期基线由 Free 回收。
//
// 内存策略：
//   - 不为每个 NetObject 都常驻一个 FPerObjectInfo（通常只有"参与 DeltaCompression 或正在做
//     baseline 操作"的对象需要），用 ObjectIndexToObjectInfoIndex 做按需创建；
//   - 实际的 baseline storage 通过 FMemory::Malloc 分配，按 Protocol->InternalTotalSize/Alignment
//     对齐——每个 baseline 是一份 internal-quantized state 的独立副本；
//   - HasDynamicState 的 Protocol 还需要 FreeDynamicState 释放嵌套动态内存（FString/TArray 等）。
//
// 与 FNetRefHandleManager 协作：
//   - per-object 的 SendStateBuffer / ReceiveStateBuffer 由 NetRefHandleManager 持有；
//   - 本类拿这两个指针作为"克隆基线时的源数据"。
// =============================================================================================

namespace UE::Net
{

// 中文：初始化参数包：
//   - ReplicationSystem：所属 RS（用于构造 InternalNetSerializationContext）；
//   - NetRefHandleManager：从中读取 per-object 的 Protocol / SendState / RecvState 指针；
//   - MaxObjectCount / MaxInternalNetRefIndex：用于预分配 ObjectIndexToObjectInfoIndex；
//   - MaxConnectionCount / MaxDeltaCompressedObjectCount：决定可同时存在的 PerObjectInfo 上限
//     （受 ObjectInfoIndexType=uint16 限制，最多 65535 个）。
struct FReplicationStateStorageInitParams
{
	UReplicationSystem* ReplicationSystem = nullptr;
	const Private::FNetRefHandleManager* NetRefHandleManager = nullptr;
	uint32 MaxObjectCount = 0;
	uint32 MaxInternalNetRefIndex = 0;
	uint32 MaxConnectionCount = 0;
	uint32 MaxDeltaCompressedObjectCount = 0;
};

// 中文：基线初始化语义——决定 Alloc/Commit 时如何填充 baseline 缓冲区：
enum class EReplicationStateType : unsigned
{
	UninitializedState, // 中文：仅分配未初始化内存，调用者负责后续 memcpy / 序列化填充。
	ZeroedState,        // 中文：分配并清零（适用于 Quantized state 的"零值基线"）。
	DefaultState,       // 中文：用 Protocol 默认 state（FReplicationProtocolOperations::InitializeFromDefaultState）。
	CurrentSendState,   // 中文：克隆当前 SendState 作为基线（最常用：服务器写包后冻结一份基线供下次 delta）。
	CurrentRecvState,   // 中文：克隆当前 RecvState 作为基线（客户端 delta 解压参考）。
};

class FReplicationStateStorage
{
public:
	// 中文：FBaselineReservation —— ReserveBaseline 的返回值，描述"预留但未提交"的 baseline。
	//   - ReservedStorage：已分配但未初始化的内存指针（Commit 时再写入）；
	//   - BaselineBaseStorage：若以 CurrentSendState/RecvState 为基础，则保存源指针，
	//     便于上层在 Commit 之前对源做幂等访问（用于校验 / fast path 等）。
	class FBaselineReservation
	{
	public:
		bool IsValid() const { return ReservedStorage != nullptr; }

		// The uninitalized memory that was reserved.
		uint8* ReservedStorage = nullptr;
		/*
		 * The state presumed to be cloned if the reservation is committed. N.B.
		 * Can only be valid if CurrentSendState or CurrentRecvState was the base.
		 */
		const uint8* BaselineBaseStorage = nullptr;
	};

public:
	FReplicationStateStorage();
	~FReplicationStateStorage();

	// 中文：初始化——分配 UsedPerObjectInfos 位图、ObjectIndexToObjectInfoIndex 表、
	//       构造 Internal/External SerializationContext。
	void Init(FReplicationStateStorageInitParams& InitParams);
	// 中文：反初始化——验证所有 baseline 已被 Free（DC_VALIDATE_BASELINES 开启时）。
	void Deinit();

	/*
	 * Returns a buffer to a state of the requested type. Only supports CurrentSendState and CurrentRecvState. May return null
	 * for example if requesting CurrentSendState on the receiving end. Returned pointer may not be written to.
	 */
	// 中文：只读访问"当前发送/接收 state buffer"。优先走快路径（PerObjectInfo 缓存），
	//       没有缓存则慢路径直接问 NetRefHandleManager。仅支持 Current* 两种语义。
	const uint8* GetState(uint32 ObjectIndex, EReplicationStateType Base) const;

	/*
	 * Allocates an internal state buffer large enough to accommodate the entire internal state, including init state,
	 * for an object and initializes it to the desired state, using cloning if needed.
	 * Returns a non-null pointer if there's memory to fulfill the request.
	 * If the requested state type is "Uninitialized" the memory must be properly initialized, zeroed or copied from other state,
	 * before calling FreeBaseline!
	 */
	// 中文：分配并初始化一份 baseline 缓冲（按 Base 决定填充语义，见 EReplicationStateType 注释）。
	//       Uninitialized 路径下调用方必须在 FreeBaseline 之前自行写入合法内容（Free 内会触发
	//       FreeDynamicState，未初始化的动态指针会导致崩溃）。
	uint8* AllocBaseline(uint32 ObjectIndex, EReplicationStateType Base);
	/* Frees a baseline allocated with AllocBaseline(). The Storage must have been properly initialized as any dynamic state will be freed as well. */
	// 中文：释放 AllocBaseline / Commit 出来的基线——若 Protocol 含 HasDynamicState 还会先递归
	//       释放嵌套动态内存。AllocationCount 归零时回收 PerObjectInfo 槽位。
	void FreeBaseline(uint32 ObjectIndex, uint8* Storage);

	/*
	 * Allocates memory for a baseline but does not initialize it.
	 * The memory should either be committed or canceled. A committed baseline
	 * must later be freed.
	 */
	// 中文：两阶段分配——先 Reserve（拿到一块未初始化内存），等真正需要时再 Commit。
	//       场景：DeltaCompression 在写包前 Reserve，写包成功后 Commit、失败则 Cancel——
	//       避免每条连接都常驻一份 baseline 副本，节省内存。
	FBaselineReservation ReserveBaseline(uint32 ObjectIndex, EReplicationStateType Base);
	/* Cancels a baseline reservation. The pointer is invalid to use after this call. */
	// 中文：取消未提交的 Reservation——直接 Free 内存，无需 FreeDynamicState（未初始化 = 无动态）。
	void CancelBaselineReservation(uint32 ObjectIndex, uint8* Storage);
	/*
	 * Initializes a buffer previously returned by ReserveBaseline. The Base must identical to that in the ReserveBaseline call.
	 * The baseline should later be freed by FreeBaseline.
	 */
	// 中文：把 Reserve 出来的内存按 Base 填充内容并提交为正式 baseline。Base 必须与 ReserveBaseline
	//       传入的一致。提交后 baseline 与 AllocBaseline 等价，需经 FreeBaseline 释放。
	void CommitBaselineReservation(uint32 ObjectIndex, uint8* Storage, EReplicationStateType Base);

	/** Called when the maximum InternalNetRefIndex increased and we need to realloc our lists */
	// 中文：当 Iris 内部最大 NetRefIndex 增加（NetObject 数量动态扩张）时回调，扩容索引表。
	void OnMaxInternalNetRefIndexIncreased(UE::Net::Private::FInternalNetRefIndex NewMaxInternalIndex);

private:
	using ObjectInfoIndexType = uint16;

	// 中文：内部常量——
	//   - InvalidObjectInfoIndex=0：保留为"无效"哨兵（依赖 static_assert 检查）；
	//   - ObjectInfoGrowCount=256：ChunkedArray 每次扩容 256 个 PerObjectInfo。
	enum : unsigned
	{
		InvalidObjectInfoIndex = 0,

		ObjectInfoGrowCount = 256,
	};

	// 中文：StateBuffers 的两个槽位 —— Send/Recv，分别对应服务器/客户端视角下的当前 state。
	enum class EStateBufferType : unsigned
	{
		SendState,
		RecvState,

		Count
	};

	// 中文：每个对象的元数据条目——
	//   - Protocol：该对象的 ReplicationProtocol（决定 baseline 总大小、是否含动态状态等）；
	//   - StateBuffers[2]：SendState / RecvState 缓冲指针（来自 NetRefHandleManager）；
	//   - AllocationCount：当前已分配的 baseline + reservation 数量，归零时回收本条目。
	struct FPerObjectInfo
	{
		const FReplicationProtocol* Protocol = nullptr;
		const uint8* StateBuffers[(unsigned)EStateBufferType::Count] = {};
		uint16 AllocationCount = 0;
	};

	// 中文：按需创建 PerObjectInfo——若该 ObjectIndex 还没分配，从 UsedPerObjectInfos 找空槽，
	//       并从 NetRefHandleManager 拷贝 Protocol/SendState/RecvState 指针缓存进去。
	FPerObjectInfo* GetOrCreatePerObjectInfoForObject(uint32 ObjectIndex);
	// 中文：按需查询（不创建）——返回 nullptr 表示尚未分配。
	FPerObjectInfo* GetPerObjectInfoForObject(uint32 ObjectIndex);
	const FPerObjectInfo* GetPerObjectInfoForObject(uint32 ObjectIndex) const;
	// 中文：清空 ObjectIndex 对应的 PerObjectInfo 槽位（在 AllocationCount 归零时调用）。
	void FreePerObjectInfoForObject(uint32 ObjectIndex);

	// 中文：在 UsedPerObjectInfos 位图中找一个空闲槽，若 ChunkedArray 不够大则扩容 256 项。
	ObjectInfoIndexType AllocPerObjectInfo();
	// 中文：归还槽位（仅清位，不缩容）。
	void FreePerObjectInfo(ObjectInfoIndexType Index);

	// 中文：克隆 quantized state——委托给 FReplicationProtocolOperationsInternal::CloneQuantizedState，
	//       它会根据 Protocol 描述深拷贝（处理 dynamic state）。
	void CloneState(const FReplicationProtocol* Protocol, uint8* TargetState, const uint8* SourceState);
	// 中文：克隆默认 state——委托给 FReplicationProtocolOperations::InitializeFromDefaultState。
	void CloneDefaultState(const FReplicationProtocol* Protocol, uint8* TargetState);
	// 中文：释放 state 中的嵌套动态内存（仅在 Protocol HasDynamicState 时调用）；不释放 State 本身。
	void FreeState(const FReplicationProtocol* Protocol, uint8* State);

private:
#if UE_NET_VALIDATE_DC_BASELINES
	// 中文：调试版本——记录每个 baseline 指针属于哪个 ObjectIndex，
	//       Free / Commit / Cancel 时校验匹配。Deinit 时校验全部已释放。
	TMultiMap<uint32, const void*> BaselineStorageValidation;
#endif

	const Private::FNetRefHandleManager* NetRefHandleManager = nullptr;

	// 中文：Internal/External SerializationContext —— 供 CloneState / FreeState 调用 Protocol
	//       Operations 时使用（这些操作内部会读取 ObjectReferenceCache 等）。
	FNetSerializationContext SerializationContext;
	Private::FInternalNetSerializationContext InternalSerializationContext;

	// 中文：UsedPerObjectInfos —— 位图，每位代表 ObjectInfoIndex 是否已被占用（含 0 号哨兵）。
	FNetBitArray UsedPerObjectInfos;
	// 中文：ObjectIndex → ObjectInfoIndex 映射；0 表示还没分配 PerObjectInfo。
	TArray<ObjectInfoIndexType> ObjectIndexToObjectInfoIndex;
	// 中文：实际的 PerObjectInfo 数组（chunked，避免大数组扩容时的拷贝；按 256 项一块分配）。
	TChunkedArray<FPerObjectInfo, ObjectInfoGrowCount*sizeof(FPerObjectInfo)> ObjectInfos;
};

}
