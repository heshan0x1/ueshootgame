// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// IrisFastArraySerializerInternal.h —— FastArray 的「内部访问 + 推式编辑器」
// -----------------------------------------------------------------------------
// 路径放在 Public/.../Private/ 是 UE 的命名约定：对 IrisCore 内部 / 同一 Public
// 头集合中的内部桥接代码可见，但不属于「面向游戏代码」的稳定 API。
//
// 本文件提供两件东西：
//
//   (A) FIrisFastArraySerializerPrivateAccessor：
//       FIrisFastArraySerializer 把若干「dirty 控制」字段（changemask、
//       conditional changemask、replication state header）声明为 protected/
//       private；本访问器以静态友元的形式开洞，给 IrisCore 内部代码 / 模板
//       编辑器使用。这样既保持了「外部 mut 必须经由编辑器」的约束，又能让
//       IrisCore 内部高效操作位图。
//
//       关键 API：
//         * GetChangeMask / GetConditionalChangeMask
//             —— 取出位图视图，IrisCore 在序列化路径直接读位、清位；
//         * GetReplicationStateHeader
//             —— 取出 header，用于判断该 FastArray 是否「已 Bind 到某 RS」
//             （Header.IsBound() 控制下面 dirty 通知是否真的触达 RS 端）。
//         * MarkArrayDirty
//             —— 把整个 array changemask 的「数组本身改动位」置 1 + 通知
//             DirtyNetObjectTracker（仅首次需要），用于 add/remove 等结构变更；
//         * MarkAllArrayItemsDirty(StartingIndex)
//             —— 从某个起点开始把所有 item 的 changemask bit 全置 1，
//             典型用法：RemoveAt（非 swap）后从 [Idx..End] 整体被左移，必须
//             把所有「可能被左移」的元素全部标 dirty；
//         * MarkArrayItemDirty(Index)
//             —— 仅把 Index 对应的 changemask bit 置 1（注意：因 changemask
//             位数有限，多个 item 可能映射到同一 bit，按 hash 折叠）。
//
//   (B) TIrisFastArrayEditor<FastArrayType> 模板：
//       「推式（push-model）」FastArray 编辑器，是 Iris 推荐的外部 mut 路径。
//       与传统 FastArraySerializer 的「Poll 全表对比」不同，本编辑器的每次
//       Add/Edit/Remove/Empty 都会：
//         1) 直接调用旧 FastArray::MarkItemDirty 维护 ReplicationID / ID 池；
//         2) 同步直接更新 changemask 对应的 bit；
//         3) 通知 DirtyNetObjectTracker 该对象 dirty；
//       从而让 ReplicationSystem 不再需要每帧 poll 整个 TArray 比对，CPU 成本
//       从 O(N) 降为 O(变更数)。
//
//       使用模式（推荐）：
//         FMyFastArray& Arr = ...;
//         TIrisFastArrayEditor Editor(Arr);   // C++17 类模板参数推导
//         Editor.Add(NewEntry);
//         Editor.Edit(3).Value = 42;
//         Editor.RemoveAtSwap(1);
//
//       「Local*」系列方法不打 dirty，专门给客户端反序列化路径使用——
//       客户端从网络收到的 Add/Edit 只更新本地 Item，不应再二次走 dirty 通路。
// =============================================================================

#pragma once

#include "Iris/ReplicationState/IrisFastArraySerializer.h"

namespace UE::Net 
{
	class FNetBitArray;
	class FNetBitArrayView;
}

namespace UE::Net	
{

namespace Private 
{

/*
 * Internal access of FastArraySerializer, for internal use only
 */
// FastArray 的私有字段访问器：在 IrisCore 内部以「静态外挂」形式打开 protected/
// private 字段访问通道（FIrisFastArraySerializer 通过 friend 授权该 struct）。
// 所有方法均为 static —— 调用方传引用进来，避免给本结构添加状态。
struct FIrisFastArraySerializerPrivateAccessor
{
	// 取出该 FastArray 的主 changemask（每 bit 对应一个 item 槽位的 hash）。
	// 调用方常用：ReplicationWriter 序列化时按 bit 读出哪些 item 需写入；
	//             MarkArrayItemDirty 通过它修改某位。
	IRISCORE_API static FNetBitArrayView GetChangeMask(FIrisFastArraySerializer& Array);

	// 取出 conditional changemask —— 用于 COND_* 条件复制（如 OwnerOnly）。
	// 与主 changemask 同尺寸，序列化时按连接条件做位与，决定是否真的发送。
	IRISCORE_API static FNetBitArrayView GetConditionalChangeMask(FIrisFastArraySerializer& Array);

	// 直接返回 ReplicationStateHeader 引用：里面记录了该 FastArray 是否已 Bind
	// 到某 ReplicationSystem。Bind 之后才有 DirtyNetObjectTracker 可通知；
	// 未 Bind 时所有 dirty 操作降级为只更新旧 FastArraySerializer 状态。
	static FReplicationStateHeader& GetReplicationStateHeader(FIrisFastArraySerializer& Array) { return Array.ReplicationStateHeader; }

	/*
	 * Mark array as dirty and notify DirtyObjectTracker that the object is dirty if it has not been done before
	 */
	// 标记「数组本身」结构改动（add/remove/empty）。内部仅置一个保留 bit；
	// 同时若是该对象首次 dirty，通知 DirtyNetObjectTracker，让 RS 把该对象拉
	// 进本帧 dirty 集合。重复调用是幂等的（Tracker 内部去重）。
	IRISCORE_API static void MarkArrayDirty(FIrisFastArraySerializer& Array);

	/*
	 * Mark array and all bits in changemask as dirty and notify DirtyObjectTracker that the object is dirty if it has not been done before
	 */
	// 把 [StartingIndex, End] 的所有 item bit 全部置 1。
	// 用于 RemoveAt（非 swap）这种「会引起后续元素左移」的操作：被左移的所有
	// 元素其内容相对原索引位置已变，必须全部当作 dirty 重新发送。
	IRISCORE_API static void MarkAllArrayItemsDirty(FIrisFastArraySerializer& Array, uint32 StartingIndex = 0U);

	/*
	 * Mark array dirty and mark changemask bit used for array Item as dirty and notify DirtyObjectTracker that the object is dirty if it has not been done before
     * Currently the same changemask bit might be used to indicate dirtiness for multiple array items
	 */
	// 标记单个 item dirty。注意：changemask 位数有限（典型 16/32 位），多个
	// item 可能映射到同一 bit（按 Index 取模 / hash），因此「只 dirty 一个 item」
	// 在序列化端可能让该 bit 上的多个 item 都被重新发送 —— 这是空间换时间
	// 的折中，但对正确性无影响。
	IRISCORE_API static void MarkArrayItemDirty(FIrisFastArraySerializer& Array, int32 Index);
};

} // end namespace Private

/**
 * Experimental support for more explicit interface to edit FastArrays which can be used to avoid polling
 * The idea is that the interface would implement a subset of the Array interface and be used instead of directly modifying the array
 */
// TIrisFastArrayEditor —— FastArray 的「推式编辑器」，提供类似 TArray 子集的接口
// （Add/Edit/Remove/RemoveAtSwap/Empty/operator[]），但每次操作都同步推送
// dirty 信息到 changemask + DirtyNetObjectTracker，避免 RS 每帧 poll 整个数组。
//
// 模板参数 FastArrayType 必须满足：
//   * 提供 typedef ItemArrayType（通常是 TArray<FMyFastArrayItem>）；
//   * 继承自 FIrisFastArraySerializer 或其等价物，含 GetItemArray()、
//     MarkArrayDirty()、MarkItemDirty()、ShouldWriteFastArrayItem<>()。
template <typename FastArrayType>
class TIrisFastArrayEditor
{
public:
	// 元素 TArray 类型（来自 FastArrayType 的内嵌 typedef）。
	typedef typename FastArrayType::ItemArrayType FastArrayItemArrayType;
	// 单个 item 的 element 类型，比如 FExampleItemEntry。
	typedef typename FastArrayItemArrayType::ElementType FastArrayItemType;

	// 在模板内对 Private accessor 起一个短别名，避免后续每处都写 namespace。
	using FIrisFastArraySerializerPrivateAccessor = Private::FIrisFastArraySerializerPrivateAccessor;

	// 构造时绑定到一个具体 FastArray 实例引用。Editor 不持有所有权，使用者
	// 必须保证 Editor 的生命周期短于 FastArray。
	TIrisFastArrayEditor(FastArrayType& InFastArray) : FastArray(InFastArray) {}

	/**
	 * Forwards MarkItemDirty call to FastArray and if the FastArray is bound it will also update DirtyState tracking
	 */
	// 将「指定 item」标记为 dirty。
	// 行为分两类（见下方实现）：
	//   * 未 Bind 到 RS（Header.IsBound()==false）：仅调用旧 MarkItemDirty
	//     维护 ReplicationID 池，等同于纯本地编辑。
	//   * 已 Bind：除维护 ID 池外，还按 item 在数组中的真实索引把对应
	//     changemask bit 置 1，必要时把所有「被新 item 插入而后移」的元素一并
	//     dirty（保证客户端能正确接收次序变化）。
	void MarkItemDirty(FastArrayItemType& Item);
	
	/**
	 * Forwards MarkArrayDirty call to FastArray and if the FastArray is bound it will also update DirtyState tracking
	 */
	// 仅把「数组本身结构」标 dirty，不动 item bit。MarkArrayDirty 内部会按需
	// 通知 DirtyNetObjectTracker（实现见 IrisFastArraySerializer.cpp）。
	void MarkArrayDirty() { FastArray.MarkArrayDirty(); };

	/**
	 * Local add which will add item to array without dirtying it
	 */
	// 本地 Add，不打 dirty。客户端反序列化路径专用：网络已经把该新增推过来，
	// 再 dirty 一次会触发反向广播。
	void AddLocal(const FastArrayItemType& ItemEntry) { FastArray.GetItemArray().Add(ItemEntry); }

	/**
	 * Local edit which will modify local item without dirtying it
	 */
	// 本地 Edit，不打 dirty。同样用于客户端反序列化或本地 UI 临时修改场景。
	FastArrayItemType& EditLocal(int32 ItemIdx) { return FastArray.GetItemArray()[ItemIdx]; }

	/**
	 * Add Item to array and call MarkItemDirty
	 */
	// 推式 Add：插入末尾后立刻 MarkItemDirty，让 RS 在下次 send 时直接知道
	// 「有一个新元素」，无需 poll 比对。
	void Add(const FastArrayItemType& ItemEntry);

	/**
	 * Edit Item in array and call MarkItemDirty
	 */
	// 推式 Edit：返回可写引用并预先标 dirty。注意该函数在调用方实际写入字段
	// **之前** 就把 dirty 推出去了，因此即使调用方拿到引用后没真改，也会被
	// 当作 dirty —— 调用方需自行判断。
	FastArrayItemType& Edit(int32 ItemIdx);

	/**
	 * Mutable access to Item, will call MarkItemDirty
	 */
	// 等价于 Edit(ItemIdx)，提供 [] 语法糖。
	FastArrayItemType& operator[](int32 ItemIdx) { return Edit(ItemIdx); }

	/**
	 * Remove item at the specified index, will forward call to MarkArrayDirty, if bound will mark all potentially moved items as dirty
	 */
	// 保序删除（TArray::RemoveAt）：会把后续元素整体左移，若 FastArray 已
	// Bind 还需把 [ItemIdx..End] 全部 item 标 dirty（用 MarkAllArrayItemsDirty）。
	// 成本相对较高；如不关心顺序请用 RemoveAtSwap。
	void Remove(int32 ItemIdx);

	/**
	 * Remove item at the specified index, will only mark the affected item dirty
	 */
	// Swap 删除（TArray::RemoveAtSwap）：把末尾元素 swap 到 ItemIdx 后弹出，
	// 只有 ItemIdx 槽内容变了；因此只需要 dirty 这一个槽（或在数组清空时
	// 退化为 MarkArrayDirty）。这是高频删除路径推荐用法。
	void RemoveAtSwap(int32 ItemIdx);

	// 当前元素个数。
	int32 Num() const { return FastArray.GetItemArray().Num(); }

	/**
	 * Empty array and call MarkArrayDirty
	 */
	// 清空数组并标 dirty。注意没有逐 item dirty，因为「整体清空」由 array
	// 自身的 dirty bit 表达即可，序列化端按 num=0 处理。
	void Empty();

private:
	// 把 FastArrayType 设为友元，允许它在自身实现里以白名单方式调用 Editor
	// 的内部辅助（见 IrisFastArraySerializer 内部 helper 实现）。
	friend FastArrayType;
	// 引用——不拥有；调用方负责生命周期匹配。
	FastArrayType& FastArray;
};

// -----------------------------------------------------------------------------
// MarkItemDirty 实现 —— 推式编辑器最核心的方法。
// 关键流程：
//   1) 若 FastArray 尚未 Bind 到 RS：直接走旧 MarkItemDirty，仅维护
//      ReplicationID 池，不必动 changemask（反正没人监听）；
//   2) 若已 Bind：
//        a) 计算 item 在 TArray 中的真实索引（基于地址差）；
//        b) 若该 item 是「新插入的」（ReplicationID == INDEX_NONE）且不在末尾，
//           说明它的插入会让后续元素相对位置发生变化；这种场景下必须像旧
//           FastArraySerializer 一样，把从该位置起到末尾的所有可写 item
//           都标 dirty，保证客户端能感知顺序变化；
//        c) 否则（在末尾追加 / 已有 item 编辑）：调用旧 MarkItemDirty 维护
//           ID 池，并把对应 changemask bit 置 1。
// -----------------------------------------------------------------------------
template <typename FastArrayType>
void TIrisFastArrayEditor<FastArrayType>::MarkItemDirty(FastArrayItemType& Item)
{
	const FReplicationStateHeader& Header = FIrisFastArraySerializerPrivateAccessor::GetReplicationStateHeader(FastArray);
	if (!Header.IsBound())
	{
		FastArray.MarkItemDirty(Item);
		return;
	}

	const FastArrayItemArrayType& Array = FastArray.GetItemArray();
	// 通过地址差反推 item 在 TArray 中的索引；只有连续 buffer 才合法。
	const uint64 Index64 = &Item - Array.GetData();
	if (ensure((IntFitsIn<int32, uint64>(Index64))))
	{
		const int32 Index = static_cast<int32>(Index64);
		if (Array.IsValidIndex(Index))
		{
			// If this is a new element make sure it is at the end, otherwise we must dirty array
			// 新插入但不在末尾：迫使后续元素位置变化 → 必须把后续 item 全部
			// 视作 dirty（与旧 FastArraySerializer 行为对齐）。
			if ((Item.ReplicationID == INDEX_NONE) && (Index != (Array.Num() - 1)))
			{
				// Must mark all items that might have been shifted dirty, including the new one, this is to mimic behavior of current FastArraySerializer
				for (FastArrayItemType& ArrayItem : MakeArrayView(&Item, Array.Num() - Index))
				{
					const bool bIsWritingOnClient = false;
					// ShouldWriteFastArrayItem 让 FastArrayType 自己决定每个 item
					// 是否「值得发送」（如条件复制场景过滤 OwnerOnly 等）。
					if (FastArray.template ShouldWriteFastArrayItem<FastArrayItemType, FastArrayType>(ArrayItem, bIsWritingOnClient))
					{
						// 新元素先用旧 MarkItemDirty 为它分配 ReplicationID。
						if (ArrayItem.ReplicationID == INDEX_NONE)
						{
							FastArray.MarkItemDirty(ArrayItem);
						}

						const uint64 ItemIndex64 = &ArrayItem - Array.GetData();
						if (ensure((IntFitsIn<int32, uint64>(ItemIndex64))))
						{
							// Mark item dirty in changemask
							FIrisFastArraySerializerPrivateAccessor::MarkArrayItemDirty(FastArray, static_cast<int32>(ItemIndex64));
						}
					}
				}
			}
			else
			{
				// This is in order to execute old logic
				// 末尾追加 / 已有 item 编辑：常见快路径。先维护 ID，再点 bit。
				FastArray.MarkItemDirty(Item);
				// Mark item dirty in changemask
				FIrisFastArraySerializerPrivateAccessor::MarkArrayItemDirty(FastArray, Index);
			}
		}
	}
};
	

// The idea is to provide an interface that is modeled after what we can do with a TArray but with dirty tracking per member
// This way we can update changemask directly and does not need to poll for changes
// 推式 Add：尾插一个 item 后立刻调用 MarkItemDirty 走「末尾分支」路径，
// 把 changemask 上对应位置 1 + 维护 ReplicationID。
template <typename FastArrayType>
void TIrisFastArrayEditor<FastArrayType>::Add(const FastArrayItemType& ItemEntry)
{
	FastArrayItemArrayType& ItemArray = FastArray.GetItemArray();
	int32 Idx = ItemArray.Add(ItemEntry);
	MarkItemDirty(ItemArray.GetData()[Idx]);
}

// 推式 Edit：返回可写引用并预先 MarkItemDirty。
// 注意 //-V758 是 PVS-Studio 静态分析的噪声抑制（reference 直接指向元素地址，
// 与 TArray 重新分配相关警告不适用——这里没有重新分配）。
template <typename FastArrayType>
typename TIrisFastArrayEditor<FastArrayType>::FastArrayItemType& TIrisFastArrayEditor<FastArrayType>::Edit(int32 ItemIdx)
{
	FastArrayItemType& Item = FastArray.GetItemArray()[ItemIdx]; //-V758
	MarkItemDirty(Item);
	return Item;
}

// 保序删除：先 RemoveAt 弹出元素，再标记 array dirty；若不是末尾删除还需要
// 把 [ItemIdx..End] 全部 item 标 dirty（因为左移导致它们的"逻辑索引"改变）。
template <typename FastArrayType>
void TIrisFastArrayEditor<FastArrayType>::Remove(int32 ItemIdx)
{
	FastArrayItemArrayType& ItemArray = FastArray.GetItemArray();

	if (ItemIdx >= 0 && ItemArray.Num() > ItemIdx)
	{
		// If the Item is at the end we only need to mark the array as dirty
		ItemArray.RemoveAt(ItemIdx);
		MarkArrayDirty();

		if (ItemIdx < ItemArray.Num())
		{
			// 删除点不是末尾：后面所有元素都被左移，必须批量标脏。
			// 仅在已 Bind 时才需要——未 Bind 没有 changemask 可言。
			const FReplicationStateHeader& Header = FIrisFastArraySerializerPrivateAccessor::GetReplicationStateHeader(FastArray);
			if (Header.IsBound())
			{
				FIrisFastArraySerializerPrivateAccessor::MarkAllArrayItemsDirty(FastArray, ItemIdx);
			}
		}
	}
}

// Swap 删除：末尾 swap 到 ItemIdx 后弹出，只有 ItemIdx 一个槽内容变了。
// 删后数组非空 → 仅 dirty 该槽；删后数组空 → 走 MarkArrayDirty 表达「清空」。
template <typename FastArrayType>
void TIrisFastArrayEditor<FastArrayType>::RemoveAtSwap(int32 ItemIdx)
{
	FastArrayItemArrayType& ItemArray = FastArray.GetItemArray();

	if (ItemIdx >= 0 && ItemArray.Num() > ItemIdx)
	{
		ItemArray.RemoveAtSwap(ItemIdx);

		// We just need to dirty the modified item
		if (ItemArray.Num() != 0)
		{
			MarkItemDirty(ItemIdx);
		}
		else
		{
			MarkArrayDirty();
		}
	}
}

// Empty：本地清空 + 整组 dirty 标记。逐 item dirty 是不必要的——序列化端
// 看到 num=0 就知道整体清空；更高效。
template <typename FastArrayType>
void TIrisFastArrayEditor<FastArrayType>::Empty()
{
	FastArray.GetItemArray().Empty();
	MarkArrayDirty();
}

} // end namespace UE::Net



