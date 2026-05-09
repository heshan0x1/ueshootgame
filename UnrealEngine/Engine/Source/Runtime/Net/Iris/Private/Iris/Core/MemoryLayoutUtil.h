// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// ============================================================================
// MemoryLayoutUtil.h —— 手工拼接"多段分配共用一大块内存"的 offset/alignment 工具
// ----------------------------------------------------------------------------
// 背景：Iris 大量数据结构（FReplicationProtocol / FReplicationInstanceProtocol /
// FReplicationStateDescriptor / FastArray 状态存储 …）为了减少 malloc 次数、
// 提升缓存命中并支持一次 Free，会把多段异构子数组（例如"成员描述数组 + 成员
// Offset 数组 + Serializer 配置数组 + DebugName 等"）一次性分配在同一块连续
// 内存里，再按各段 offset 切出子视图。
//
// 但这类"一次分配多段"的代码很容易写错对齐和大小计算。FMemoryLayoutUtil 就
// 是该计算模板，提供"累加式 Layout"——按顺序 AddToLayout 声明每一段的
// 类型/数量，工具会为你算出每段的 aligned Offset 和 Size，最后再调
// GetTotalSizeIncludingAlignment 得到整块应申请的字节数。
//
// 典型调用路径：
//   FReplicationStateDescriptorBuilder::Build(...) 过程中：
//     1) 先依次 AddToLayout 各段（MemberDescriptors、MemberChangeMaskDescriptors、
//        MemberProperties、MemberPropertyDescriptors、DefaultStateBuffer …）；
//     2) 读取 FLayout::GetTotalSizeIncludingAlignment 申请一次大内存；
//     3) 用得到的 FOffsetAndSize 从基址偏移出各子数组的指针。
//   FReplicationProtocol / FReplicationInstanceProtocol 构造也使用同样套路。
//
// 本头文件位于 Private 目录，不对外暴露；仅 IrisCore 内部使用。
//
// 对齐算法说明（Align 宏基于位运算的 "向上取整到 Alignment 的倍数"）：
//   Align(Offset, A) = (Offset + (A-1)) & ~(A-1)    （当 A 是 2 的幂）
// 这个算法要求 A 是 2 的整数次幂（C++ 语义下 alignof 永远满足）；它把 Offset
// 从最近的 A 的倍数向上对齐。每 AddToLayout 时：
//   1) 先把 CurrentOffset 上对齐到 ItemAlignment，得到 CurrentAlignedOffset；
//   2) 再往后塞 ItemSize * ItemCount 字节；
//   3) 更新 CurrentOffset = CurrentAlignedOffset + TotalItemSize；
//   4) 记录全块的 MaxAlignment = max(已记录的 MaxAlignment, ItemAlignment)。
// 最终整块分配时再按 MaxAlignment 再一次向上取整，保证"整体首地址对齐后，
// 所有段 offset 也必然对齐"——这是最严格的对齐保守策略。
// ============================================================================

#include "Math/UnrealMathUtility.h"
#include "Templates/AlignmentTemplates.h"

/** Helper to deal with manually setting up a block of memory containing multiple allocations. */
/**
 * 辅助工具：增量式地计算"多段分配合成一大块连续内存"时的偏移和对齐。
 *
 * 设计思路：调用方持有一个 FLayout 状态，依次对每段子数据调用 AddToLayout，
 * 工具把当前已累计的偏移上对齐到段要求的对齐值、记录该段的 Offset 和 Size，
 * 再累加到下一轮。最后 GetTotalSizeIncludingAlignment 返回整体需要申请的
 * 字节数（还会额外按整体最大对齐再向上取整）。
 *
 * 使用姿势：
 *   FMemoryLayoutUtil::FLayout Layout;
 *   FMemoryLayoutUtil::FOffsetAndSize DescOS, PropertyOS;
 *   FMemoryLayoutUtil::AddToLayout<FDescriptor>(Layout, DescOS, N);
 *   FMemoryLayoutUtil::AddToLayout<const FProperty*>(Layout, PropertyOS, M);
 *   uint32 Total = FMemoryLayoutUtil::GetTotalSizeIncludingAlignment(Layout);
 *   uint8* Buffer = (uint8*)FMemory::Malloc(Total, Layout.MaxAlignment);
 *   Descriptors = reinterpret_cast<FDescriptor*>(Buffer + DescOS.Offset);
 *   Properties  = reinterpret_cast<const FProperty**>(Buffer + PropertyOS.Offset);
 */
struct FMemoryLayoutUtil
{
	/**
	 * 累计状态：在多次 AddToLayout 调用之间传递的 Layout 游标。
	 */
	struct FLayout
	{
		/** 下一段将落在的起始字节偏移（未对齐），下一次 AddToLayout 会先把它上对齐。 */
		SIZE_T CurrentOffset = 0;
		/** 至今为止所有段中最大的对齐值，用于最终整块内存分配时的"整体对齐"。 */
		SIZE_T MaxAlignment = 1;
	};

	/**
	 * 单段结果：一段子数据在整块内存里的起始偏移与字节长度。
	 * 调用 AddToLayout 后由函数填入，调用方据此切出指针/视图。
	 */
	struct FOffsetAndSize
	{
		SIZE_T Offset;
		SIZE_T Size;
	};

	/**
	 * 只描述"大小 + 对齐"的通用 DTO。
	 * 在某些场景下需要返回某类型元素的字节数和对齐，但不一定立即上 Layout——
	 * 例如判断总开销预算、或单独缓存一份"类型 footprint"时使用。
	 */
	struct FSizeAndAlignment
	{
		SIZE_T Size = 0;
		SIZE_T Alignment = 0;
	};

	/** Get the current total size in bytes, including alignment, of the layout. */
	/**
	 * 整块内存最终需要申请的字节数。
	 *
	 * 实现：把 CurrentOffset 再按 MaxAlignment 向上取整——这样即使分配器返回
	 * 的块起始地址是 MaxAlignment 的倍数，末尾也不会溢出"结构体数组末位"。
	 * 同时在 ArrayOfStructs 布局中，整体对齐 ≥ 每段对齐 是必要条件。
	 *
	 * 复杂度 O(1)，纯位运算；无副作用。
	 */
	static SIZE_T GetTotalSizeIncludingAlignment(const FLayout& Layout) { return Align(Layout.CurrentOffset, Layout.MaxAlignment); }

	/** Add an entry to the Layout with the specified size and alignment in bytes. */
	/**
	 * 向 Layout 追加一段数据。
	 *
	 * 参数：
	 *   - Layout          ：累计状态，入参兼出参；
	 *   - OutOffsetAndSize：本段在整块内存中的偏移与长度（函数写回）；
	 *   - ItemSize        ：单个元素字节数（通常 = sizeof(T)）；
	 *   - ItemAlignment   ：单个元素对齐（通常 = alignof(T)，必须是 2 的幂）；
	 *   - ItemCount       ：元素个数。
	 *
	 * 算法：
	 *   1) 把当前游标 CurrentOffset 上对齐到 ItemAlignment，得到段起始 offset；
	 *   2) 计算本段总字节 TotalItemSize = ItemSize * ItemCount；
	 *   3) 写回 OutOffsetAndSize = {offset, TotalItemSize}；
	 *   4) 游标推进到 段末尾；
	 *   5) 更新 MaxAlignment 以覆盖整块对齐要求。
	 *
	 * 边界条件：
	 *   - ItemCount = 0 时段大小为 0，offset 仍会前向对齐——允许"空段但预留对齐锚点"；
	 *   - ItemAlignment = 1 时 Align() 不改变游标，即紧密排布。
	 *
	 * 线程安全：无同步；Layout 不应跨线程共享。
	 */
	static void AddToLayout(FLayout& Layout, FOffsetAndSize& OutOffsetAndSize, SIZE_T ItemSize, SIZE_T ItemAlignment, SIZE_T ItemCount)
	{
		// 第 1 步：把游标推进到 ItemAlignment 的下一个倍数（若已对齐则不变）。
		const SIZE_T CurrentAlignedOffset = Align(Layout.CurrentOffset, ItemAlignment);
		// 第 2 步：计算本段字节数 = 单元大小 × 数量。
		const SIZE_T TotalItemSize = ItemSize * ItemCount;

		// 第 3 步：把 {起始 offset, 总字节数} 写回给调用者。
		OutOffsetAndSize.Offset = CurrentAlignedOffset;
		OutOffsetAndSize.Size = TotalItemSize;

		// 第 4 步：游标推进到段末尾，供下一段 AddToLayout 使用。
		Layout.CurrentOffset = CurrentAlignedOffset + TotalItemSize;
		// 第 5 步：跟踪整块所需的最大对齐；最终 GetTotalSizeIncludingAlignment
		//         会按该值再次向上取整总字节数。
		Layout.MaxAlignment = FMath::Max<SIZE_T>(ItemAlignment, Layout.MaxAlignment);
	}

	/** Add a typed entry to the Layout. */
	/**
	 * AddToLayout 的类型化便捷版本。
	 * 由编译器通过 sizeof(T) / alignof(T) 自动推出正确的大小与对齐，避免手写出错。
	 * 典型用法：AddToLayout<FDescriptor>(Layout, Out, Count);
	 */
	template<typename T>
	static void AddToLayout(FLayout& Layout, FOffsetAndSize& OutOffsetAndSize, SIZE_T Count) { AddToLayout(Layout, OutOffsetAndSize, sizeof(T), alignof(T), Count); }
};
