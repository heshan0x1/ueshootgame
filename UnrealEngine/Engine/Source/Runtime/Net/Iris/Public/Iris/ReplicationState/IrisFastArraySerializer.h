// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// IrisFastArraySerializer.h —— Iris 版的 FastArraySerializer：把传统 FFastArraySerializer 嫁接进 Iris 的脏跟踪体系
// ---------------------------------------------------------------------------------------------------------------------
// FFastArraySerializer 是 UE 旧网络框架中的"差异化数组复制"工具：上层维护 ReplicationID/ReplicationKey，
// 由 NetDriver 在 NetSerialize 时按 key 比对决定 Add/Change/Remove。Iris 不再走那条路径，但完全兼容这套
// 用户 API（MarkItemDirty / MarkArrayDirty / PostReplicatedAdd 等），因此引入本类作为"桥"：
//   - 继承自 FFastArraySerializer，所有现有用户代码无需修改即可工作；
//   - 重写 MarkItemDirty / MarkArrayDirty：在原有逻辑之外，额外把对应 item / array 的位写进 Iris 的
//     "成员 ChangeMask"，并通过 GlobalDirtyNetObjectTracker 通知 NetObject "我脏了"；
//   - 内嵌 FReplicationStateHeader（30-bit NetHandleId + 2-bit dirty flags）作为脏对象索引；
//   - 内嵌 ChangeMaskStorage[4]（128-bit）保存元素 changemask + 条件 changemask（见下方位布局）。
//
// ChangeMaskStorage[4] 的位布局：
//   ─────────────────────────────────────────────────────────────────────────────────
//   │ ChangeMaskStorage[0..1]  │ ChangeMaskStorage[2..3]                            │
//   │ Member ChangeMask (64位) │ Conditional ChangeMask (64位)                       │
//   │  bit  0     : array 总体脏 (PropertyBitIndex)
//   │  bits 1..63 : 元素 changemask（IrisFastArrayChangeMaskBits = 63 项；
//   │               实际数组长度 > 63 时用 modulo 散列共享位 — 同一位代表多个元素）
//   ─────────────────────────────────────────────────────────────────────────────────
//
//   InitChangeMask 把前一半（Member）置 0，后一半（Conditional）置 0xFFFFFFFF（默认全条件通过）。
//   GetChangeMask / GetConditionalChangeMask 通过 FNetBitArrayView 暴露给 Iris 的 ReplicationFragment 使用。
//
// 关键约束：
//   - 重要：Iris 版 FastArray 不支持"局部不复制"——若覆写 ShouldWriteFastArrayItem 并过滤掉某些元素，
//     当前实现会 ensure（最终接收端可能会收到额外元素并自行过滤，但不推荐）。
//   - 拷贝/移动构造**不复制 ReplicationStateHeader**（Header 标识"自己是哪个 NetObject"，
//     如果跟着 copy 会把脏标记错位发到别人头上），改为重新 InitChangeMask + 全脏标记。
//
// 推式编辑（Push Mode）：
//   传统 FastArray 在每帧 Poll 时仍然要扫描整个 TArray 比对 ReplicationKey。
//   Iris 推荐通过 TIrisFastArrayEditor<FastArrayType>（见 FastArrayReplicationFragmentInternal.h /
//   IrisFastArraySerializerInternal.h）直接 Add/Edit/Remove，编辑器内部会精确标 changemask，
//   彻底避免每帧 poll 扫描；这是 Iris 在大规模数组上的关键性能收益。
// =====================================================================================================================

#pragma once

#include "Net/Serialization/FastArraySerializer.h"
#include "Iris/ReplicationState/ReplicationStateFwd.h"
#include "IrisFastArraySerializer.generated.h"

// Forward  declarations
namespace UE::Net::Private
{

// PrivateAccessor —— 让 ReplicationFragment / 内部模块可以读写 changemask 与 Header 而不暴露 public API。
struct FIrisFastArraySerializerPrivateAccessor;

}

/**
 * Specialization of FFastArraySerializer in order to add state tracking support for Iris
 * Current usage is to inherit from this struct instead of FFastArraySerializer, backwards compatible with existing system as it simply forwards calls to MarkDirty/MarkItemDirty
 * This class could be named FFastArrayReplicationState, but kept the FIrisFastArraySerializer to match old naming for the time being
 *
 * NOTE: IrisFastArraySerializer, does not support having local not replicated items in the array.
 * If ShouldWriteFastArrayItem is overridden and filters out items, an ensure will be triggered. 
 * Logic should still work, but extra elements will be replicated and filtered out on receiving end.
 */
USTRUCT(Experimental)
struct FIrisFastArraySerializer : public FFastArraySerializer
{
	// At the moment as we have no way to specify this per derived type, currently we reserve a fixed range of bits used for the changemask, the first bit is used for the array itself
	enum {
		// 元素 changemask 占 63 位（不含 array bit）。数组长度超过 63 时用 modulo 共享位。
		IrisFastArrayChangeMaskBits = 63U,
		// 元素 changemask 起始偏移（bit 0 是 array bit，因此元素位从 1 开始）。
		IrisFastArrayChangeMaskBitOffset = 1U,
		// "数组总体脏"位 —— 任意修改都会把它置位，作为外部的"全脏"快速通道。
		IrisFastArrayPropertyBitIndex = 0U
	};

	GENERATED_BODY()
	IRISCORE_API FIrisFastArraySerializer();

	~FIrisFastArraySerializer() = default;

	/** Will not copy replication state header */
	// 拷贝构造：按 FFastArraySerializer 父类的语义复制 ReplicationID/Key 等；
	// **不**复制 ReplicationStateHeader（不能错误绑定到原对象的 NetHandle）；
	// 重新 InitChangeMask 而非继承原 changemask（新对象绑定到新 NetHandle 时再由系统标脏）。
	IRISCORE_API FIrisFastArraySerializer(const FIrisFastArraySerializer& Other);

	/** We must make sure that we do not copy replication state header and must update dirtiness if bound */
	// 拷贝赋值：父类负责真实数据；这里若已绑定（IsBound），保守地把所有元素标脏——
	// 因为我们无法在不深比较的情况下知道哪些 item 真的变了。
	IRISCORE_API FIrisFastArraySerializer& operator=(const FIrisFastArraySerializer& Other);

	/** Will not copy replication state header */
	// 移动构造：与拷贝构造同理（不接管 Header，重置 changemask）。
	// 注意签名是 `const FIrisFastArraySerializer&&`——非典型，无法真正"窃取"，等价于一次拷贝。
	IRISCORE_API FIrisFastArraySerializer(const FIrisFastArraySerializer&& Other);

	/** We must make sure that we do not move replication state header and must update dirtiness if bound */
	IRISCORE_API FIrisFastArraySerializer& operator=(FIrisFastArraySerializer&& Other);
	
	/** Override MarkItemDirty in order to mark object as dirty in the DirtyNetObjectTracker */
	// 改写 MarkItemDirty：先调父类（维护 Item.ReplicationKey++）再额外通知 Iris 标脏。
	// 注意此处只标"数组层级"脏（InternalMarkArrayAsDirty）——具体哪个 item 脏由用户用 TIrisFastArrayEditor::Edit 才能精确传递。
	// 普通 MarkItemDirty 用法兼容旧代码，但会让 ReplicationFragment 走"全数组重比"路径。
	void MarkItemDirty(FFastArraySerializerItem & Item)
	{ 
		FFastArraySerializer::MarkItemDirty(Item);
		if (ReplicationStateHeader.IsBound())
		{
			// Mark array dirty for Iris so that is it copied
			InternalMarkArrayAsDirty();
		}
	}
	
	/** Override MarkArrayDirty in order to mark object as dirty in the DirtyNetObjectTracker */
	// 改写 MarkArrayDirty：父类保持兼容；Iris 侧把 array bit 置位 + 通知 GlobalDirtyNetObjectTracker。
	void MarkArrayDirty()
	{
		FFastArraySerializer::MarkArrayDirty();
		if (ReplicationStateHeader.IsBound())
		{
			// Mark array dirty for Iris
			InternalMarkArrayAsDirty();
		}	
	}

	// 仅 PrivateAccessor 可以触达下方的私有 changemask / Header 字段。
	friend UE::Net::Private::FIrisFastArraySerializerPrivateAccessor;

private:
	enum : unsigned
	{
		// ChangeMaskStorage 中"成员 changemask"起始 dword 索引（占 [0..1] 共 64 位）。
		MemberChangeMaskStorageIndex = 0U,
		// "条件 changemask"起始 dword 索引（占 [2..3] 共 64 位，默认全 1）。
		ConditionalChangeMaskStorageIndex = 2U,
	};

	// Mark item at index as changed, and set the array as dirty
	// 重置 changemask 到初始状态（前 64 位清零、后 64 位全 1）。构造函数与拷贝构造调用。
	IRISCORE_API void InitChangeMask();
	// 把指定下标的 item 标脏（基于 modulo 散列到 1..63 位之一，并通过 array bit 通知对象级脏）。
	IRISCORE_API void InternalMarkItemChanged(int32 ItemIdx);
	// 把所有 item 全部标脏（拷贝赋值/移动赋值时使用，因为无法精确判定哪些 item 真的变化）。
	IRISCORE_API void InternalMarkAllItemsChanged();
	// 仅设置 array bit，并通知 GlobalDirtyNetObjectTracker；用于通用"数组级"标脏（未指定 item）。
	IRISCORE_API void InternalMarkArrayAsDirty();

	// Header for dirty state tracking needs to be just before ChangeMaskStorage. See GetReplicationStateHeader for more info.
	// 重要内存布局：ReplicationStateHeader 必须紧邻 ChangeMaskStorage 之前。
	// FIrisFastArraySerializerPrivateAccessor::GetReplicationStateHeader 通过 ChangeMaskStorage 起点 - sizeof(Header) 反推 Header 地址。
	// 构造函数中有 static_assert 校验该不变式（offsetof(ChangeMaskStorage) - sizeof(Header) == offsetof(Header)）。
	UE::Net::FReplicationStateHeader ReplicationStateHeader;

	// Storage for changemask, this is currently hardcoded
	// 4 个 uint32 = 128 bit。前 64 位为成员 changemask（含 1 个 array bit + 63 个元素位），
	// 后 64 位为条件 changemask（用于 Lifetime/动态 condition 判定，默认全 1 表示通过）。
	// UPROPERTY(Transient, NotReplicated) 防止反射误以为这是要复制的数据。
	UPROPERTY(Transient, NotReplicated)
	uint32 ChangeMaskStorage[4];
};

