// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// NetBlob.h —— Iris NetBlob 子模块的最底层抽象基类。
//
// 概览：
//   FNetBlob 是 Iris 中"通用网络消息载荷"的统一抽象。所有需要伴随对象复制流之外、
//   或独立成包发送的数据，都被建模为某个 FNetBlob 派生类的实例：
//     - RPC                ：UNetRPCHandler 创建的 FNetRPC（FNetObjectAttachment 派生）
//     - 对象附件 (Attachment)：FNetObjectAttachment（带 NetObjectReference 指向目标）
//     - 巨型对象状态 (HugeObject)：FNetObjectBlob（在 NetObjectBlobHandler 中处理）
//     - 大 blob 分片        ：FPartialNetBlob（超过 MTU 时切分，由 Assembler 重组）
//     - 原始字节            ：FRawDataNetBlob（已序列化的字节流，直接复制）
//
//   每个 blob 都有一个 typeId（FNetBlobType，由 FNetBlobHandlerManager 在
//   注册时按 ini 配置顺序分配），以及一组 ENetBlobFlags（Reliable/Ordered/HasExports/
//   RawDataNetBlob）。typeId 在线上以最多 8 bits 序列化（实际 1+7 紧凑编码），
//   因此 NetBlobHandlerDefinitions 中最多只能登记 < 128 个 handler。
//
// 引用计数：
//   FNetBlob 自身实现侵入式 RefCount（AddRef/Release），可与 TRefCountPtr<FNetBlob>
//   配合。新建 blob 引用计数为 0，调用方必须在第一次持有时显式 AddRef，或者
//   通过 TRefCountPtr 包装时由其 ctor 自动 AddRef。
//
// 序列化：
//   - 派生类常用两种方式之一：
//     a) 提供 FReplicationStateDescriptor + FQuantizedBlobState：基类自动用
//        FReplicationStateOperations::Serialize/Deserialize 序列化（推荐）。
//     b) 重写 Serialize/Deserialize 或 SerializeWithObject/DeserializeWithObject。
//   - SerializeCreationInfo / DeserializeCreationInfo 仅写入 typeId + Reliable 标志，
//     接收端据此调用 FNetBlobHandlerManager::CreateNetBlob 创建对应派生类型的实例。
//
// 与文档对应关系：
//   ReplicationSystem.md §6.5 NetBlob 中的 "FNetBlob / FNetObjectAttachment：抽象基类"。
// =====================================================================================

#pragma once
#include "CoreTypes.h"
#include "Iris/Core/NetObjectReference.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "HAL/UnrealMemory.h"
#include "Containers/ArrayView.h"
#include "Templates/RefCounting.h"
#include "Templates/UniquePtr.h"

namespace UE::Net
{
	class FNetBlob;
	class FNetSerializationContext;
	struct FReplicationStateDescriptor;
	class FNetReferenceCollector;
	namespace Private
	{
		// FNetBlobManager 是 FNetObjectAttachment 的 friend，需要在创建 RPC/附件后
		// 直接写入 NetObjectReference / TargetObjectReference。
		class FNetBlobManager;
	}
}

namespace UE::Net
{

/**
 * 创建 blob 时可设置的位标志。
 *
 * Flags that can be set at blob creation time.
 *
 * 说明：
 *   - 这些 flags 会影响 blob 在发送队列中的排序与是否需要重传。
 *   - SerializeCreationInfo 仅会写出 Reliable 这一位；其余 flag 对接收端不可见，
 *     除非派生类自行序列化。
 */
enum class ENetBlobFlags : uint32
{
	None = 0,

	/** The blob should be delivered reliably in order with respect to other reliable blobs. Implies Ordered. */
	// 可靠送达：与其他 Reliable blob 之间保证有序。隐含 Ordered 语义。
	// 在 ReliableNetBlobQueue 中按序号排队，丢失会重发直到 ACK。
	Reliable = 1U << 0U,

	/** Used for FRawDataNetBlob derived classes to avoid duplicate serialization when splitting large blob. */
	// 标记当前 blob 是 FRawDataNetBlob 派生：内容已经序列化为 raw bits。
	// 当 NetBlobAssembler 重组完成时走快速路径直接 SetRawData，避免再次走
	// Deserialize → 反量化 → 再序列化的流程。
	RawDataNetBlob = Reliable << 1U,

	/** Used to indicate that this blob have ObjectReferences or NetTokens that might have to be exported. */
	// 该 blob 内含 NetObjectReference 或 NetToken，可能需要在导出表中追加导出。
	// FReplicationWriter 会在写入 blob 之前先把这些引用导出到 NetExports，
	// 接收端才能解析。详见 GetNetObjectReferenceExports / GetNetTokenExports。
	HasExports = RawDataNetBlob << 1U,

	/** The blob should respect delivery order with respect to other Ordered blobs, including Reliable ones. Unreliable ordered blobs will only be sent once. */
	// 仅保证有序但不保证可靠：与其他 Ordered（含 Reliable）blob 一起按序送达；
	// 不可靠的 Ordered blob 只发送一次，丢了就丢了不补发。
	Ordered = HasExports << 1U,
};
ENUM_CLASS_FLAGS(ENetBlobFlags);

// blob 类型 id。注册顺序决定 typeId 数值，运行期一旦分配就不可变。
typedef uint32 FNetBlobType;

/** Denotes an invalid NetBlob type. */
// 表示尚未注册或非法的 blob 类型。
constexpr FNetBlobType InvalidNetBlobType = ~FNetBlobType(0);

/**
 * Information typically passed to a FNetBlob at creation time.
 * Flags will not be serialized unless the FNetBlob derived class chooses to do so.
 */
// 创建 blob 时所需的最小信息。
//   - Type ：blob 的运行期 typeId（来自 FNetBlobHandlerManager 注册结果）。
//   - Flags：通常仅 Reliable 会被基类自动序列化；其它 flag 仅运行期使用。
struct FNetBlobCreationInfo
{
	FNetBlobType Type = InvalidNetBlobType;
	ENetBlobFlags Flags = ENetBlobFlags::None;
};

// FNetBlob 是一切 NetBlob 的抽象基类。它不具备多态创建机制，依赖
// UNetBlobHandler 来知道如何"按 typeId 创建对应派生类"。
class FNetBlob
{
public:
	// 量化后的 blob 状态（Internal 表示）。一段裸字节缓冲区，由 FReplicationStateDescriptor
	// 描述其字段布局。多用于复用 ReplicationState 的序列化路径。
	struct FQuantizedBlobState
	{
		enum class EMemoryAllocationFlags : uint32
		{
			None = 0,
			// If the state is constructed as protectable one can call Protect() to detect memory stomps.
			// 申请整页对齐内存以便 Protect()/Unprotect() 切换页保护，调试 memory stomp。
			Protectable = 1,
		};

		FQuantizedBlobState() = default;
		FQuantizedBlobState(const FQuantizedBlobState&) = delete;
		FQuantizedBlobState(FQuantizedBlobState&&);
		// 按 Size/Alignment 申请置零的状态缓冲区；若 MemoryAllocationFlags 含 Protectable
		// 则按整页对齐与大小申请。
		IRISCORE_API FQuantizedBlobState(uint32 Size, uint32 Alignment, EMemoryAllocationFlags MemoryAllocationFlags = EMemoryAllocationFlags::None);
		IRISCORE_API ~FQuantizedBlobState();

		FQuantizedBlobState& operator=(const FQuantizedBlobState&) = delete;
		FQuantizedBlobState& operator=(FQuantizedBlobState&&);

		uint8* GetStateBuffer() { return StateBuffer; }
		const uint8* GetStateBuffer() const { return StateBuffer; }

		/** Protects the state buffer such that a page fault will occur if something tries to modify its contents. The state must have been constructed with EMemoryAllocationFlags::Protectable. */
		// 设置内存页为只读：任何写入将触发 page fault，便于检查"完成量化后是否被异常修改"。
		IRISCORE_API void Protect();

		/** Allows the state buffer to be modified again if it was protected. */
		// 恢复内存页可写。
		IRISCORE_API void Unprotect();

	private:
		uint8* StateBuffer = nullptr;
		EMemoryAllocationFlags MemoryAllocationFlags = EMemoryAllocationFlags::None;
		uint32 AllocationSize = 0;
	};

	/** Construct a NetBlob with reference count zero. */
	// 构造时引用计数为 0；调用方需在第一次持有时显式 AddRef，
	// 或者直接由 TRefCountPtr 接管。
	IRISCORE_API FNetBlob(const FNetBlobCreationInfo&);

	/** Set the blob state. Use when there's a descriptor to avoid having to override serialization functions. */
	// 把 (Descriptor, QuantizedState) 一对设进 blob：基类 SerializeBlob/DeserializeBlob
	// 会自动按 descriptor 走标准流程，派生类无需重写 Serialize/Deserialize。
	IRISCORE_API void SetState(const TRefCountPtr<const FReplicationStateDescriptor>& BlobDescriptor, FQuantizedBlobState&& QuantizedBlobState);

	/** Returns the FNetBlobCreationInfo. */
	const FNetBlobCreationInfo& GetCreationInfo() const { return CreationInfo; }

	/* Whether the blob is reliably sent or not. */
	// 是否带 Reliable 位（决定加入 ReliableNetBlobQueue 与发送策略）。
	bool IsReliable() const { return EnumHasAnyFlags(GetCreationInfo().Flags, ENetBlobFlags::Reliable); }

	/** Returns the FReplicationStateDescriptor if there is one. It's recommended to use a descriptor instead of overriding the serialization methods. */
	const FReplicationStateDescriptor* GetReplicationStateDescriptor() const { return BlobDescriptor.GetReference(); }

	/** Serializes the necessary parts of the creation info so that the blob can be recreated on the receiving side. */
	// 序列化 typeId + Reliable 位：
	//   bit 0      ： typeId == 0 ? 0 : 1
	//   bits 1..7 ： 当 typeId != 0 时再写 7 位 typeId（紧凑：0 占 1 bit，其它占 8 bits）
	//   bit 8      ： Reliable 位
	// → 因此 typeId 范围是 [0,127]，匹配 NetBlobHandlerDefinitions.Num() < 128 的限制。
	IRISCORE_API static void SerializeCreationInfo(FNetSerializationContext& Context, const FNetBlobCreationInfo& CreationInfo);

	/** Deserializes the necessary parts of the creation info so that the correct blob can be created. */
	// 与 SerializeCreationInfo 对称：恢复 typeId / Reliable，供
	// FNetBlobHandlerManager::CreateNetBlob(typeId) 反查 handler 创建实例。
	IRISCORE_API static void DeserializeCreationInfo(FNetSerializationContext& Context, FNetBlobCreationInfo& OutCreationInfo);

	/** Serialize the blob together with/targeting a specific object knowing the NetHandle has already been serialized. */
	// 与一个已序列化好 NetRefHandle 的对象一起发送时调用：派生类可省略对象引用的写入。
	IRISCORE_API virtual void SerializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) const;

	/** Deserialize a blob that was serialized with SerializeWithObject. */
	IRISCORE_API virtual void DeserializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle);

	/** Serialize the blob. */
	// 独立序列化（不附着到对象）。派生类必须保证与 Deserialize 对称。
	IRISCORE_API virtual void Serialize(FNetSerializationContext& Context) const;

	/** Deserialize a blob that was serialized with Serialize. */
	IRISCORE_API virtual void Deserialize(FNetSerializationContext& Context);

	/** Collect object references from quantized data */
	// 从量化态扫描出全部 NetObjectReference，加入 Collector，供 NetExports 导出。
	IRISCORE_API void CollectObjectReferences(FNetSerializationContext& Context, FNetReferenceCollector& Collector) const;

	/** Adds a reference. A blob is created with reference count zero. */
	void AddRef() const { ++RefCount; }

	/** Removes a reference. When the reference count reaches zero the blob will be deleted. A blob is created with reference count zero. */
	// 减少一次引用；归零后 delete this。注意 blob 创建时计数为 0。
	IRISCORE_API void Release() const;

	/** Returns the reference count.  A blob is created with reference count zero. */
	int32 GetRefCount() const { return RefCount; }

	/* Returns true if the blob has additional exports to add. */
	// 是否需要在写入 blob 前导出额外的 NetObjectReference / NetToken。
	bool HasExports() const { return EnumHasAnyFlags(GetCreationInfo().Flags, ENetBlobFlags::HasExports); }

	/** Retrieve the object references that need to be exported. */
	// 转发到虚函数 GetNetObjectReferenceExports，供 FReplicationWriter 取出导出列表。
	TArrayView<const FNetObjectReference> CallGetNetObjectReferenceExports() const { return GetNetObjectReferenceExports(); };

	/** Retrieve NetToken that needs to be exported. */
	TArrayView<const FNetToken> CallGetNetTokenExports() const { return GetNetTokenExports(); };

protected:
	FNetBlob(const FNetBlob&) = delete;
	FNetBlob& operator=(const FNetBlob&) = delete;

	/** The destructor will free dynamic state, if present, in the state buffer and remove a reference to the descriptor. */
	// 析构会清理量化态中带 Dynamic 标志的子状态（字符串/数组等动态分配字段）。
	IRISCORE_API virtual ~FNetBlob();

	/** Override to return the object references that need to be exported. */
	// 派生类可重写返回需要导出的对象引用（默认空数组）。
	virtual TArrayView<const FNetObjectReference> GetNetObjectReferenceExports() const;

	/** Override to return NetTokenExports that need to be exported, mostly relevant for pre-serialized blobs. */
	// 派生类可重写返回需要导出的 NetToken。Pre-serialized blob（如 RawData）尤需此机制。
	virtual TArrayView<const FNetToken> GetNetTokenExports() const;

	/** Serializes the state if there's a valid descriptor and state buffer. */
	// 标准实现：若设置过 (descriptor, state) 就调用 ReplicationStateOperations::Serialize。
	IRISCORE_API void SerializeBlob(FNetSerializationContext& Context) const;

	/** Deserializes the state if there's a valid descriptor and state buffer. */
	IRISCORE_API void DeserializeBlob(FNetSerializationContext& Context);

protected:
	/** The CreationInfo that was passed to the constructor. */
	// 构造时传入的 CreationInfo。typeId 在此（被 handler 反查时使用）。
	FNetBlobCreationInfo CreationInfo;

	/** The state descriptor. */
	// 状态描述符：决定 SerializeBlob/DeserializeBlob 的字段布局与序列化器。
	TRefCountPtr<const FReplicationStateDescriptor> BlobDescriptor;

	/** The state buffer that holds the data described by the descriptor. */
	// 量化态缓冲区：与 BlobDescriptor 配合使用。
	FQuantizedBlobState QuantizedBlobState;

private:
	// 侵入式引用计数；mutable 以便 const blob 也能 AddRef/Release。
	mutable int32 RefCount;
};

/**
 * FNetObjectAttachment serves as a base class for NetBlobs targeting a specific object.
 * @see UReplicationSystem::QueueNetObjectAttachment
 */
// 对象附件 blob 基类：携带"持有者引用 + 目标子对象引用"，使 blob 与具体复制对象关联。
//   - NetObjectReference     ：负责承载该 blob 的对象（一定是已复制的 root/subobject）。
//   - TargetObjectReference  ：blob 真正应用到的目标（可能是非复制的子对象，则与 NetObjectReference 不同）。
// 典型派生：FNetRPC（来自 UNetRPCHandler::CreateRPC），以及外部游戏逻辑通过
//   UReplicationSystem::QueueNetObjectAttachment 入队的自定义附件。
class FNetObjectAttachment : public FNetBlob
{
public:
	IRISCORE_API FNetObjectAttachment(const FNetBlobCreationInfo&);
	const FNetObjectReference& GetNetObjectReference() const { return NetObjectReference; }
	const FNetObjectReference& GetTargetObjectReference() const { return TargetObjectReference; }

protected:
	virtual ~FNetObjectAttachment();

	/** Serializes the owner and subobject reference. */
	// 把 owner + target 两个引用都写入 bitstream（独立发送时使用）。
	IRISCORE_API void SerializeObjectReference(FNetSerializationContext& Context) const;

	/** Serializes the owner and subobject reference. */
	IRISCORE_API void DeserializeObjectReference(FNetSerializationContext& Context);

	/** Serializes only the subobject reference and assumes the passed NetHandle is the owner. */
	// 仅写入 target 引用：当 blob 与 owner 对象一起发送时用，省一段引用。
	IRISCORE_API void SerializeSubObjectReference(FNetSerializationContext& Context, FNetRefHandle RefHandle) const;

	/** Deserializes a subobject reference that was serialized using SerializeSubObjectReference with the same NetHandle. */
	// 与 SerializeSubObjectReference 对称；接收端用传入的 RefHandle 充当 owner 引用。
	IRISCORE_API void DeserializeSubObjectReference(FNetSerializationContext& Context, FNetRefHandle RefHandle);

	/** Set the owner and subobject references. */
	// 由 FNetBlobManager 在入队前调用，写入 owner/target；二者相同时仅设置 owner。
	void SetNetObjectReference(const FNetObjectReference& InQueueOwnerReference, const FNetObjectReference& InTargetObjectReference);

protected:
	// FNetBlobManager 需要在 QueueNetObjectAttachment / SendRPC 流程里直接写
	// NetObjectReference / TargetObjectReference，故声明为 friend。
	friend class Private::FNetBlobManager;

	/** The owner reference. */
	// 承载该 attachment 的复制对象引用（root 或 subobject 之一）。
	FNetObjectReference NetObjectReference;

	/** The subobject reference. */
	// 真正目标对象引用；若与 owner 相同则保持默认值。
	FNetObjectReference TargetObjectReference;
};

// FQuantizedBlobState 移动构造：直接接管缓冲区指针，源对象置零防止 double-free。
inline FNetBlob::FQuantizedBlobState::FQuantizedBlobState(FNetBlob::FQuantizedBlobState&& Other)
{
	StateBuffer = Other.StateBuffer;
	MemoryAllocationFlags = Other.MemoryAllocationFlags;
	AllocationSize = Other.AllocationSize;

	Other.StateBuffer = nullptr;
	Other.MemoryAllocationFlags = EMemoryAllocationFlags::None;
	Other.AllocationSize = 0;
}

// 移动赋值：先释放本对象现有缓冲（解除页保护并 Free），再接管 Other 的资源。
inline FNetBlob::FQuantizedBlobState& FNetBlob::FQuantizedBlobState::operator=(FNetBlob::FQuantizedBlobState&& Other)
{
	if (StateBuffer)
	{
		Unprotect();
		FMemory::Free(StateBuffer);
	}

	StateBuffer = Other.StateBuffer;
	MemoryAllocationFlags = Other.MemoryAllocationFlags;
	AllocationSize = Other.AllocationSize;

	Other.StateBuffer = nullptr;
	Other.MemoryAllocationFlags = EMemoryAllocationFlags::None;
	Other.AllocationSize = 0;
	return *this;
}

// 设置 owner/target：当二者相同（典型 root object RPC）时，TargetObjectReference 保持
// 默认（无效）；序列化时可省略。
inline void FNetObjectAttachment::SetNetObjectReference(const FNetObjectReference& InQueueOwnerReference, const FNetObjectReference& InTargetObjectReference)
{
	NetObjectReference = InQueueOwnerReference;
	if (InQueueOwnerReference != InTargetObjectReference)
	{
		TargetObjectReference = InTargetObjectReference;
	}
}

}
