// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// IrisFastArraySerializer.cpp —— FIrisFastArraySerializer 与 PrivateAccessor 的实现
// ---------------------------------------------------------------------------------------------------------------------
// 本文件提供两块实现：
//   1) FIrisFastArraySerializer 自身：构造/拷贝/移动 + 4 个 Internal 辅助（InitChangeMask / InternalMarkItem*等）
//   2) Private::FIrisFastArraySerializerPrivateAccessor 的若干静态方法：
//      - GetChangeMask / GetConditionalChangeMask 提供 FNetBitArrayView 形式的访问；
//      - MarkAllArrayItemsDirty / MarkArrayDirty / MarkArrayItemDirty 三种粒度的脏标记，
//        统一处理 array bit 与 GlobalDirtyNetObjectTracker 通知，避免重复触发。
//
// 关键不变式：
//   - 构造函数中的 static_assert 校验 ChangeMaskStorage 紧邻 ReplicationStateHeader 之后（仅相隔
//     sizeof(Header) 字节）。这是 PrivateAccessor::GetReplicationStateHeader 由 ChangeMaskStorage 反推 Header
//     地址所依赖的内存布局——任何字段顺序变动都会破坏它。
//   - changemask 元素位采用 modulo 散列：array 长度 > 63 时多个元素共享同一位，
//     接收侧 ApplyReplicatedState 通过逐元素 InternalCompareArrayElement 二次确认真假脏。
//   - 当 ReplicationStateHeader.IsBound() == false（即此 FIrisFastArraySerializer 还未与某个 NetObject 绑定）
//     时，所有"标脏"调用都是 no-op（在 .h 中由 if (IsBound()) 守卫）。
// =====================================================================================================================

#include "Iris/ReplicationState/IrisFastArraySerializer.h"
#include "Iris/ReplicationState/Private/IrisFastArraySerializerInternal.h"
#include "Iris/ReplicationState/ReplicationStateUtil.h"
#include "Net/Core/NetBitArray.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IrisFastArraySerializer)

FIrisFastArraySerializer::FIrisFastArraySerializer()
: FFastArraySerializer()
{
	// 内存布局不变式校验：ReplicationStateHeader 必须紧邻 ChangeMaskStorage 之前。
	// PrivateAccessor::GetReplicationStateHeader 通过 ChangeMaskStorage 起点 - sizeof(Header) 反推 Header 地址；
	// 一旦字段顺序变化（例如有人在中间加了字段）此条件将失败，立即编译期报错。
	static_assert(offsetof(FIrisFastArraySerializer, ChangeMaskStorage) - sizeof(UE::Net::FReplicationStateHeader) == offsetof(FIrisFastArraySerializer, ReplicationStateHeader), "GetReplicationStateHeader is no longer compatible with FIrisFastArraySerializer");
	InitChangeMask();
}

FIrisFastArraySerializer::FIrisFastArraySerializer(const FIrisFastArraySerializer& Other)
: FFastArraySerializer(Other)
{
	// 不接管 Other 的 ReplicationStateHeader（默认零初始化，即未绑定到任何 NetHandle）
	// 也不复制 Other 的 changemask——重新干净起步。
	InitChangeMask();
}

FIrisFastArraySerializer::FIrisFastArraySerializer(const FIrisFastArraySerializer&& Other)
: FFastArraySerializer(Other)
{
	// 注意签名是 const &&，无法 std::move 真正窃取，等价于一次拷贝；
	// 与拷贝构造同样不接管 Header / 不继承 changemask。
	InitChangeMask();
}

FIrisFastArraySerializer& FIrisFastArraySerializer::operator=(const FIrisFastArraySerializer& Other)
{
	if (this != &Other)
	{
		// 父类（FFastArraySerializer）负责复制 ReplicationID/Key 等运行时字段
		Super::operator=(Other);
		// Currently we are pessimistic and marks everything as dirty
		// 悲观策略：因为我们无法在这里廉价地比对哪些 item 真的变化（且 Other 的元素与本对象元素可能完全不同），
		// 一律标记全脏，让接收端按完整 changemask 重新跑一遍 Apply。
		if (ReplicationStateHeader.IsBound())
		{
			InternalMarkAllItemsChanged();
		}
	}

	return *this;
}

FIrisFastArraySerializer& FIrisFastArraySerializer::operator=(FIrisFastArraySerializer&& Other)
{
	if (this != &Other)
	{
		Super::operator=(Other);
		// 同上：移动赋值也走全脏策略
		if (ReplicationStateHeader.IsBound())
		{
			InternalMarkAllItemsChanged();
		}
	}
	return *this;
}

void FIrisFastArraySerializer::InitChangeMask()
{
	// Init changemask and conditional changemask
	// 初始化 ChangeMaskStorage：
	//   前 64 位（成员 changemask） → 0   （无任何脏）
	//   后 64 位（条件 changemask） → 0xFFFFFFFFU   （默认全部条件通过——无 LifetimeCondition 约束）
	for (uint32& Storage : ChangeMaskStorage)
	{
		const SIZE_T Index = &Storage - &ChangeMaskStorage[0];
		if (Index < ConditionalChangeMaskStorageIndex)
		{
			Storage = 0U;
		}
		else
		{
			Storage = 0xFFFFFFFFU;
		}
	}
}

void FIrisFastArraySerializer::InternalMarkAllItemsChanged()
{
	checkSlow(ReplicationStateHeader.IsBound());
	// 委托给 PrivateAccessor，由它统一处理 array bit + 全元素位 + GlobalDirtyNetObjectTracker。
	UE::Net::Private::FIrisFastArraySerializerPrivateAccessor::MarkAllArrayItemsDirty(*this);
}

void FIrisFastArraySerializer::InternalMarkArrayAsDirty()
{
	checkSlow(ReplicationStateHeader.IsBound());
	UE::Net::Private::FIrisFastArraySerializerPrivateAccessor::MarkArrayDirty(*this);
}

void FIrisFastArraySerializer::InternalMarkItemChanged(int32 ItemIdx)
{
	checkSlow(ReplicationStateHeader.IsBound());
	UE::Net::Private::FIrisFastArraySerializerPrivateAccessor::MarkArrayItemDirty(*this, ItemIdx);
}

namespace UE::Net::Private
{

// 暴露成员 changemask 视图（前 64 位），实际可访问位数 = IrisFastArrayChangeMaskBits + 1（含 array bit）。
// 用 FNetBitArrayView 包装，便于 ReplicationFragment 用 SetBit/GetBit/SetAllBits 等高层语义。
FNetBitArrayView FIrisFastArraySerializerPrivateAccessor::GetChangeMask(FIrisFastArraySerializer& Array)
{
	return MakeNetBitArrayView(&Array.ChangeMaskStorage[FIrisFastArraySerializer::MemberChangeMaskStorageIndex], FIrisFastArraySerializer::IrisFastArrayChangeMaskBits + 1U);
}

// 暴露条件 changemask 视图（后 64 位）。Lifetime/动态 Condition 系统会读这部分位决定本帧某 item 对哪些连接生效。
FNetBitArrayView FIrisFastArraySerializerPrivateAccessor::GetConditionalChangeMask(FIrisFastArraySerializer& Array)
{
	return MakeNetBitArrayView(&Array.ChangeMaskStorage[FIrisFastArraySerializer::ConditionalChangeMaskStorageIndex], FIrisFastArraySerializer::IrisFastArrayChangeMaskBits + 1U);
}

// 把所有元素标脏（可指定起点 StartingIndex 仅标后段，但默认 0 标全部）。
// 关键：仅当 array bit 当前未置位时才需要 MarkNetObjectStateHeaderDirty，
// 这样可以在大量频繁标脏调用下避免对 GlobalDirtyNetObjectTracker 的重复登记。
void FIrisFastArraySerializerPrivateAccessor::MarkAllArrayItemsDirty(FIrisFastArraySerializer& Array, uint32 StartingIndex)
{
	checkSlow(Array.ReplicationStateHeader.IsBound());

	FNetBitArrayView MemberChangeMask = UE::Net::Private::FIrisFastArraySerializerPrivateAccessor::GetChangeMask(Array);
	if (!MemberChangeMask.GetBit(FIrisFastArraySerializer::IrisFastArrayPropertyBitIndex))
	{
		// 第一次标脏 → 通知对象级 dirty tracker，让 ReplicationSystem 在下一帧 Poll/Quantize 中包含此对象
		MarkNetObjectStateHeaderDirty(Array.ReplicationStateHeader);
	}
	if (StartingIndex == 0)
	{
		// 全部位（含 array bit + 元素位）都置 1：最暴力的"全脏"
		MemberChangeMask.SetAllBits();
	}
	else
	{
		// 仅从 StartingIndex 起标脏（用于 RemoveAt 后让后段元素全部进入"待重比"状态）
		MemberChangeMask.SetBit(FIrisFastArraySerializer::IrisFastArrayPropertyBitIndex);
		MemberChangeMask.SetBits(FIrisFastArraySerializer::IrisFastArrayChangeMaskBitOffset + StartingIndex, FIrisFastArraySerializer::IrisFastArrayChangeMaskBits - StartingIndex);
	}
}

// 仅标"数组级"脏：用于 array bit 通道（结构变化但未指定 item）。
// 同样通过 array bit 当前值快速判断是否需要再次通知 tracker。
void FIrisFastArraySerializerPrivateAccessor::MarkArrayDirty(FIrisFastArraySerializer& Array)
{
	checkSlow(Array.ReplicationStateHeader.IsBound());

	FNetBitArrayView MemberChangeMask = UE::Net::Private::FIrisFastArraySerializerPrivateAccessor::GetChangeMask(Array);

	// Dirty object unless already dirty, we only use the array bit for this purpose
	// 用 array bit 充当"是否已通知 tracker"的去重标志：
	//   - 未置位 → 这是本帧第一次标脏，需要 MarkNetObjectStateHeaderDirty 通知 GlobalDirtyNetObjectTracker
	//   - 已置位 → 已通知过，跳过即可（避免高频改动时重复登记）
	if (!MemberChangeMask.GetBit(FIrisFastArraySerializer::IrisFastArrayPropertyBitIndex))
	{
		MemberChangeMask.SetBit(FIrisFastArraySerializer::IrisFastArrayPropertyBitIndex);
		MarkNetObjectStateHeaderDirty(Array.ReplicationStateHeader);
	}
}

// 标记单个 item 脏：通过 modulo 散列定位元素位（数组超过 63 项时多个元素共享同一位）。
// 同时设置 array bit + 通知 tracker（同样以 array bit 为去重标志）。
void FIrisFastArraySerializerPrivateAccessor::MarkArrayItemDirty(FIrisFastArraySerializer& Array, int32 ItemIdx)
{
	checkSlow(Array.ReplicationStateHeader.IsBound());

	// Mark changemask dirty for this item
	// We are using a modulo scheme for dirtiness
	// (ItemIdx % 63) + 1 → 实际元素位下标。位 0 是 array bit，所以 +1 偏移。
	FNetBitArrayView MemberChangeMask = UE::Net::Private::FIrisFastArraySerializerPrivateAccessor::GetChangeMask(Array);
	MemberChangeMask.SetBit((ItemIdx % FIrisFastArraySerializer::IrisFastArrayChangeMaskBits) + FIrisFastArraySerializer::IrisFastArrayChangeMaskBitOffset);

	// Dirty object unless already dirty, we only use the array bit for this purpose
	if (!MemberChangeMask.GetBit(FIrisFastArraySerializer::IrisFastArrayPropertyBitIndex))
	{
		MemberChangeMask.SetBit(FIrisFastArraySerializer::IrisFastArrayPropertyBitIndex);
		MarkNetObjectStateHeaderDirty(Array.ReplicationStateHeader);
	}
}

}
