// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// NetBlob.cpp —— FNetBlob / FNetObjectAttachment / FQuantizedBlobState 实现。
// 关键点：CreationInfo 的紧凑序列化、量化态生命周期、对象引用收集、引用计数 delete this。
// =====================================================================================

#include "Iris/ReplicationSystem/NetBlob/NetBlob.h"
#include "Iris/ReplicationSystem/ObjectReferenceCache.h"
#include "Iris/ReplicationSystem/ReplicationOperations.h"
#include "Iris/ReplicationSystem/ReplicationOperationsInternal.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/ObjectNetSerializer.h"
#include "HAL/PlatformMemory.h"
#include "Templates/AlignmentTemplates.h"
#include "AutoRTFM.h"

namespace UE::Net
{

// NetBlob
// 构造函数仅记录 CreationInfo（含 typeId 与 flags），引用计数 0。
FNetBlob::FNetBlob(const FNetBlobCreationInfo& InCreationInfo)
: CreationInfo(InCreationInfo)
, RefCount(0)
{
}

// 析构：若量化态中包含动态分配字段（HasDynamicState：字符串、可变数组等），
// 必须先解除页保护再调用 FreeDynamicState 释放，避免泄漏。
FNetBlob::~FNetBlob()
{
	if (const FReplicationStateDescriptor* Descriptor = BlobDescriptor.GetReference())
	{
		if (EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::HasDynamicState))
		{
			if (uint8* StateBuffer = QuantizedBlobState.GetStateBuffer())
			{
				QuantizedBlobState.Unprotect();
				Private::FReplicationStateOperationsInternal::FreeDynamicState(StateBuffer, Descriptor);
			}
		}
	}
}

// 把外部已量化好的状态搬入 blob：派生类常用，避免重写 Serialize/Deserialize。
void FNetBlob::SetState(const TRefCountPtr<const FReplicationStateDescriptor>& InBlobDescriptor, FQuantizedBlobState&& InQuantizedBlobState)
{
	BlobDescriptor = InBlobDescriptor;
	QuantizedBlobState = MoveTemp(InQuantizedBlobState);
}

// 把 typeId + Reliable 标志写入 bitstream，紧凑编码：
//   - typeId == 0：仅写 1 bit '0'。
//   - typeId != 0：先写 1 bit '1'，再写 7 bits typeId。
//   → 实际可表达的 typeId 范围 [0,127]，与 FNetBlobHandlerManager::Init 中
//     "<128 个 NetBlobHandler" 的断言一致。
// 末尾再写 1 bit Reliable，让接收端在创建 blob 时立即知道是否走可靠路径。
void FNetBlob::SerializeCreationInfo(FNetSerializationContext& Context, const FNetBlobCreationInfo& CreationInfo)
{
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	Writer->WriteBits((CreationInfo.Type == 0 ? 0U : 1U), 1U);
	if (CreationInfo.Type != 0)
	{
		Writer->WriteBits(CreationInfo.Type, 7U);
	}

	// Retain Reliable flag
	// 仅保留 Reliable 标志参与序列化；其它运行期 flag（HasExports/RawDataNetBlob 等）
	// 在接收端会在 blob 真正创建后由具体 handler 重新计算。
	Writer->WriteBits(EnumHasAnyFlags(CreationInfo.Flags, ENetBlobFlags::Reliable) ? 1U : 0U, 1U);
}

// 与 SerializeCreationInfo 对称。
void FNetBlob::DeserializeCreationInfo(FNetSerializationContext& Context, FNetBlobCreationInfo& OutCreationInfo)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	FNetBlobType Type = Reader->ReadBits(1U);
	if (Type != 0)
	{
		Type = Reader->ReadBits(7U);
	}

	const uint32 Reliable = Reader->ReadBits(1);
	ENetBlobFlags Flags = (Reliable ? ENetBlobFlags::Reliable : ENetBlobFlags::None);

	OutCreationInfo.Type = Type;
	OutCreationInfo.Flags = Flags;
}

// 与对象一起发送：基类默认只写 blob 状态，对象引用由外层 batch 负责。
void FNetBlob::SerializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) const
{
	SerializeBlob(Context);
}

void FNetBlob::DeserializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle)
{
	check(BlobDescriptor.IsValid());
	DeserializeBlob(Context);
}

// 独立发送：基类同样只写状态。需要写引用的派生类（如 FNetObjectAttachment）会重写。
void FNetBlob::Serialize(FNetSerializationContext& Context) const
{
	SerializeBlob(Context);
}

void FNetBlob::Deserialize(FNetSerializationContext& Context)
{
	DeserializeBlob(Context);
}

// 扫描量化态中的对象引用并加入 Collector。仅在带 descriptor + state 的情况下生效。
// FReplicationWriter 调用此方法以便在写 blob 之前把引用导出到 NetExports。
void FNetBlob::CollectObjectReferences(FNetSerializationContext& Context, FNetReferenceCollector& Collector) const
{
	if (BlobDescriptor.IsValid() && QuantizedBlobState.GetStateBuffer())
	{
		const FNetSerializerChangeMaskParam InitStateChangeMaskInfo = { 0 };
		Private::FReplicationStateOperationsInternal::CollectReferences(Context, Collector, InitStateChangeMaskInfo, QuantizedBlobState.GetStateBuffer(), BlobDescriptor);
	}
}

// 标准序列化：委托给通用 ReplicationStateOperations，按 descriptor 字段顺序量化写入。
void FNetBlob::SerializeBlob(FNetSerializationContext& Context) const
{
	if (BlobDescriptor.IsValid() && QuantizedBlobState.GetStateBuffer())
	{
		FReplicationStateOperations::Serialize(Context, QuantizedBlobState.GetStateBuffer(), BlobDescriptor);
	}
}

void FNetBlob::DeserializeBlob(FNetSerializationContext& Context)
{
	if (BlobDescriptor.IsValid() && QuantizedBlobState.GetStateBuffer())
	{
		FReplicationStateOperations::Deserialize(Context, QuantizedBlobState.GetStateBuffer(), BlobDescriptor);
	}
}

// 引用计数 -1 归零自删；与 TRefCountPtr 配合使用。
void FNetBlob::Release() const
{
	if (--RefCount == 0)
	{
		delete this;
	}
}

// 默认无导出：派生类按需重写。
TArrayView<const FNetObjectReference> FNetBlob::GetNetObjectReferenceExports() const
{
	return MakeArrayView<const FNetObjectReference>(nullptr, 0);
};

TArrayView<const FNetToken> FNetBlob::GetNetTokenExports() const
{
	return MakeArrayView<const FNetToken>(nullptr, 0);
};

// NetObjectAttachment
// 构造与析构：仅维持基类 RefCount + Reference 字段。
FNetObjectAttachment::FNetObjectAttachment(const FNetBlobCreationInfo& CreationInfo)
: FNetBlob(CreationInfo)
{
}

FNetObjectAttachment::~FNetObjectAttachment()
{
}

// 写入 owner + target 两个完整对象引用（独立发送时使用）。
void FNetObjectAttachment::SerializeObjectReference(FNetSerializationContext& Context) const
{
	WriteFullNetObjectReference(Context, NetObjectReference);
	WriteFullNetObjectReference(Context, TargetObjectReference);
}

void FNetObjectAttachment::DeserializeObjectReference(FNetSerializationContext& Context)
{
	ReadFullNetObjectReference(Context, NetObjectReference);
	ReadFullNetObjectReference(Context, TargetObjectReference);
}

// 与对象一起发送时只需写 target；owner 由外层 batch 通过 NetRefHandle 传递。
void FNetObjectAttachment::SerializeSubObjectReference(FNetSerializationContext& Context, FNetRefHandle RefHandle) const
{
	WriteFullNetObjectReference(Context, TargetObjectReference);
}

// 接收端：用传入的 RefHandle 充当 owner reference，再读 target。
void FNetObjectAttachment::DeserializeSubObjectReference(FNetSerializationContext& Context, FNetRefHandle RefHandle)
{
	NetObjectReference = Private::FObjectReferenceCache::MakeNetObjectReference(RefHandle);
	ReadFullNetObjectReference(Context, TargetObjectReference);
}

// 量化态构造：
//   - 普通模式：MallocZeroed 申请 (Size, Alignment) 缓冲。
//   - Protectable：把 Alignment 与 AllocationSize 都按整页对齐，便于 Protect()
//     调用 PageProtect 切换为只读。
FNetBlob::FQuantizedBlobState::FQuantizedBlobState(uint32 Size, uint32 Alignment, FQuantizedBlobState::EMemoryAllocationFlags InMemoryAllocationFlags)
: MemoryAllocationFlags(InMemoryAllocationFlags)
{
	if (InMemoryAllocationFlags == EMemoryAllocationFlags::None)
	{
		StateBuffer = static_cast<uint8*>(FMemory::MallocZeroed(Size, Alignment));
		AllocationSize = Size;
	}
	else
	{
		const FPlatformMemoryConstants& MemoryConstants = FPlatformMemory::GetConstants();
		const uint32 NewAlignment = Align(Alignment, MemoryConstants.PageSize);
		AllocationSize = static_cast<uint32>(Align(Size, MemoryConstants.PageSize));
		StateBuffer = static_cast<uint8*>(FMemory::MallocZeroed(AllocationSize, NewAlignment));
	}
}


// 析构必须先 Unprotect（恢复可写），否则在被保护页上 free 会触发 page fault。
FNetBlob::FQuantizedBlobState::~FQuantizedBlobState()
{
	if (StateBuffer)
	{
		Unprotect();
		FMemory::Free(StateBuffer);
	}
}

// Protect：使用 AutoRTFM 兼容事务回滚，事务 abort 时恢复为可读写。
void FNetBlob::FQuantizedBlobState::Protect()
{
	if (MemoryAllocationFlags == EMemoryAllocationFlags::Protectable)
	{
		UE_AUTORTFM_OPEN
		{
			constexpr bool bCanRead = true;
			constexpr bool bCanWrite = false;
			FPlatformMemory::PageProtect(StateBuffer, AllocationSize, bCanRead, bCanWrite);
		};
		UE_AUTORTFM_ONPREABORT(StateBuffer = this->StateBuffer, AllocationSize = this->AllocationSize)
		{
			constexpr bool bCanRead = true;
			constexpr bool bCanWrite = true;
			FPlatformMemory::PageProtect(StateBuffer, AllocationSize, bCanRead, bCanWrite);
		};
	}
}

// 恢复读写：本身就是常态，无需事务回滚。
void FNetBlob::FQuantizedBlobState::Unprotect()
{
	if (MemoryAllocationFlags == EMemoryAllocationFlags::Protectable)
	{
		// Marking a page as read and write does not require to be rolled back. It's the normal status of our memory.
		UE_AUTORTFM_OPEN
		{
			constexpr bool bCanRead = true;
			constexpr bool bCanWrite = true;
			FPlatformMemory::PageProtect(StateBuffer, AllocationSize, bCanRead, bCanWrite);
		};
	}
}

}
