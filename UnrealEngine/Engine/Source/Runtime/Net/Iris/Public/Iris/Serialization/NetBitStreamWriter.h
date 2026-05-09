// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace UE::Net
{

/**
 * FNetBitStreamWriter —— Iris 序列化层的按位写入器（位流 Writer），与 FNetBitStreamReader 对偶。
 *
 * 职责：
 *  - 在一段 4 字节对齐、以 uint32 小端（INTEL_ORDER32）布局的连续缓冲区上，提供任意位宽写入（WriteBits / WriteBool / WriteBitStream）。
 *  - 通过 PendingWord 缓存当前未满的 word，延迟真实 store——只有在 word 填满、Seek 跨越 word、commit 时才会真正 Commit 到 Buffer。
 *  - overflow 语义：一旦 WriteBits 请求位数超过剩余容量，后续所有写入都成为 no-op（直到 Seek 回到合法位置清除溢出）。
 *  - substream 机制：切出独立写入窗口；commit 时把子流产生的比特数合并回父流，discard 时放弃（但 buffer 内容可能已被修改，这是设计上的折衷）。
 *
 * 为什么需要 CommitWrites？
 *   WriteBits 热路径只更新 PendingWord（寄存器常驻），不立即写回 Buffer。析构、Seek、CommitSubstream 等会隐式 CommitWrites 一次。
 *   若在外部需要查看已写入的 Buffer，必须手动调用 CommitWrites，否则最后半 word 尚未刷新。
 *
 * 线程安全：**非线程安全**。一条连接一个实例。
 *
 * 约束：
 *  - Buffer 必须 4 字节对齐，容量（ByteCount）必须是 4 的倍数（InitBytes 内部 check）。
 *  - 与 Reader 相同的 substream 嵌套/互斥规则。
 */
class FNetBitStreamWriter
{
public:
	/** 构造未初始化的 Writer，必须调用 InitBytes 后才能写入。 */
	IRISCORE_API FNetBitStreamWriter();

	/** 析构。会隐式 CommitWrites 一次把 PendingWord 刷回 Buffer。持有活跃 substream 时会触发 checkf 报错。 */
	IRISCORE_API ~FNetBitStreamWriter();

	/**
	 * InitBytes must be called before writing to the stream. 
	 * @param Buffer The buffer must be at least 4-byte aligned.
	 * @param ByteCount The number of bytes that is allowed to be written. Must be a multiple of 4.
	 *
	 * 初始化 Writer：
	 *  - Buffer 必须 4 字节对齐；ByteCount 必须 4 的倍数（便于 word 化处理）。
	 *  - 容量换算为 BitCapacity = ByteCount*8。
	 *  - 首次会把 Buffer[0] 预取到 PendingWord（保证覆盖写入时不破坏 buffer 中已有未写区）。
	 */
	IRISCORE_API void InitBytes(void* Buffer, uint32 ByteCount);

	/**
	 * Writes the BitCount least significant bits from Value. There's no validation of the Value, meaning
	 * it is allowed to contain garbage in the bits that are not going to be written to the buffer.
	 * If the BitCount exceeds the remaining space no bits will be written and the stream will be 
	 * marked as overflown.
	 *
	 * 写入 Value 的低 BitCount 位（范围 [0, 32]）。
	 * 调用者无需清零高位；内部通过 mask 过滤。
	 * 溢出判定：若剩余容量不足则标脏 OverflowBitCount，本次及后续写入全部 no-op。
	 * 热路径优化：若当前 word 尚有足够剩余位数，仅修改 PendingWord 不触及内存；跨 word 时一次 store + 加载下个 word。
	 */
	IRISCORE_API void WriteBits(uint32 Value, uint32 BitCount);

	/**
	 * Writes a bool to the stream and returns the value of the bool
	 *
	 * 写入单个 bool（1 位）。实现上将 Value 装入 volatile int8 以避免编译器对"非 0/1 的 bool"做非预期优化，
	 * 最终只保留最低位。
	 */
	inline bool WriteBool(bool Value);

	/**
	 * Writes BitCount bits from Src starting at SrcBitOffset. Assumes that Src was written to via 
	 * this class or that bits were written in order from lowest to highest memory address. 
	 * If the BitCount exceeds the remaining space no bits will be written and the stream will be
	 * marked as overflown.
	 *
	 * 批量写入任意位数，Src 也需遵循"低地址→高地址、低位→高位"的内部布局。
	 * 用于大块数据拷贝（附件 payload、SparseBitArray、WriteBytes 等）。
	 */
	IRISCORE_API void WriteBitStream(const uint32* Src, uint32 SrcBitOffset, uint32 BitCount);

	/**
	 * Commits pending writes to the buffer. Before this call, or the destruction of this instance, the buffer 
	 * may not be up to date. Typically called after all WriteBits() calls have been made.
	 *
	 * 显式把 PendingWord 刷回 Buffer。热路径上的 WriteBits 只改 PendingWord，外部若要查看 Buffer 字节必须先 CommitWrites。
	 * Seek/~FNetBitStreamWriter/CommitSubstream 会隐式调用此函数。
	 */
	IRISCORE_API void CommitWrites();

	/** 
	 * Seek to a specific BitPosition. If the stream is overflown and you seek back to a position
	 * where you can still write bits the stream will no longer be considered overflown.
	 *
	 * 跳转到相对于流/子流起点的 BitPosition。
	 * 若 Seek 前处于溢出状态，且新位置合法则清除溢出。
	 * 实现细节：跳转前会先把旧 PendingWord 刷回 Buffer，再从新位置重新加载 PendingWord（保证不丢失未提交数据）。
	 * 约束：不能跨越已分配 buffer；uint32 wrap-around 也视为越界。
	 */
	IRISCORE_API void Seek(uint32 BitPosition);

	/** Returns the the current byte position. */
	/** 返回相对流/子流起点的字节位置（向上取整）。 */
	inline uint32 GetPosBytes() const { return (BufferBitPosition - BufferBitStartOffset + 7) >> 3U; }

	/** Returns the current bit position */
	/** 返回相对流/子流起点的比特位置。 */
	inline uint32 GetPosBits() const { return BufferBitPosition - BufferBitStartOffset; }

	/** Returns the absolute bit position */
	/** 返回绝对比特位置（相对原始 Buffer[0] bit 0）。 */
	inline uint32 GetAbsolutePosBits() const { return BufferBitPosition; }

	/** Returns the number of bits that can be written before overflowing. */
	/** 返回剩余可写位数；溢出状态下总是返回 0。 */
	inline uint32 GetBitsLeft() const { return (OverflowBitCount ? 0U : (BufferBitCapacity - BufferBitPosition)); }

	/** Force an overflow. */
	/** 强制置为溢出——错误路径立即截断后续写入。 */
	IRISCORE_API void DoOverflow();

	/** Returns whether the stream is overflown or not. */
	/** 查询当前是否已溢出。 */
	inline bool IsOverflown() const { return OverflowBitCount != 0; }

	/** 
	 * Creates a substream at the current bit position. The substream must be committed or discarded. Only one active substream at a time is allowed,
	 * but a substream can have an active substream as well. Once the substream has been commited or discarded a new substream may be created. No
	 * writes may be performed to this stream until the substream has been committed or discarded. Any write to a substream that occurs before overflow
	 * can modify the stream buffer contents regardless of whether the stream is committed or discarded. 
	 *
	 * The returned BitStreamWriter will have similar behavior to a newly constructed regular FNetBitStreamWriter. 
	 * 
	 * @param MaxBitCount The maximum allowed bits that may be written. The value will be clamped to the number of bits left in this stream/substream.
	 * @return A BitStreamWriter that can be written to just like a regular BitStreamWriter.
	 *
	 * 在当前位置切出子流。子流与父流共享底层 Buffer，独立维护位置/溢出状态。
	 * 注意：子流向 Buffer 的真实字节写入不会被 discard 撤销（buffer 已脏）——只有父流的位置不会推进。上层通常配合 FNetBitStreamRollbackScope 在
	 * 检测到错误时重置父流位置，从而让后续写入覆盖掉子流的脏数据。
	 */
	IRISCORE_API FNetBitStreamWriter CreateSubstream(uint32 MaxBitCount = ~0U);

	/**
	 * Commits a substream to this stream. Substreams that are overflown or do not belong to this stream will be ignored. 
	 * If the substream is valid then this stream's bit position will be updated. CommitWrites() is not called by this method.
	 *
	 * 提交子流：父流 BufferBitPosition 推进到子流结束位置，并调用子流的 CommitWrites 把其 PendingWord 刷出；
	 * 随后父流从新位置重新加载 PendingWord。
	 * 若子流已 overflow 则整体 commit 失败，父流位置保持不变（子流仍被置为 invalid）。
	 */
	IRISCORE_API void CommitSubstream(FNetBitStreamWriter& Substream);

	/** Discards a substream of this stream. This stream's bit position will remain intact. The buffer may be modified anyway as mentioned in CreateSubStream().
	 *  丢弃子流：父流位置保持不变，但 Buffer 中已由子流写入的字节不会被自动回滚——需要上层配合位置回退覆盖。 */
	IRISCORE_API void DiscardSubstream(FNetBitStreamWriter& Substream);

private:
	uint32* Buffer;                      // 底层 uint32 数组（4 字节对齐），小端存储
	uint32 BufferBitCapacity;            // 绝对比特容量上限（InitBytes 里算出的 ByteCount*8；子流则为 StartOffset + 允许写入位数）
	// For substreams this indicate the bit position in the buffer where it may start writing
	uint32 BufferBitStartOffset;         // 子流起点（绝对位置）；主流恒为 0
	uint32 BufferBitPosition;            // 当前绝对写入位置
	uint32 PendingWord;                  // 当前 word 的缓存；未 commit 前不刷回 Buffer，用于减少热路径 store
	uint32 OverflowBitCount;             // 溢出指示：0 = 正常；非 0 = 超出容量的位数

	uint32 bHasSubstream : 1;            // 当前流存在活跃子流，禁止自身写入
	uint32 bIsSubstream : 1;             // 当前流是一个子流
	uint32 bIsInvalid : 1;               // 已被父流 commit/discard，不再可用
};

/**
 * WriteBool 的内联实现。使用 volatile int8 间接层是为了防止编译器假定 bool 只能是 0/1——
 * 在 UE 早期版本中外部代码偶尔会把非 0/1 的字节"伪装"为 bool（虽然这是未定义行为），这个写法保证兼容。
 * 实际写入位数恒为 1。
 */
bool FNetBitStreamWriter::WriteBool(bool Value)
{
	// This is to support a Value other than 0 or 1.
	volatile int8 ValueAsInt8 = Value;
	WriteBits(ValueAsInt8 ? 1U : 0U, 1U);
	return ValueAsInt8 ? true : false;
}

}

/**
 * NetTrace 辅助：获取 Writer 的绝对比特位置——溢出时返回 0（而非实际位置），
 * 以表达"这个 trace 点对应的数据并未真正写入"，与 Reader 语义不同。
 * 实现使用无分支技巧：(IsOverflown ? 0 : Pos) = (uint32(IsOverflown)-1) & Pos。
 */
inline uint32 GetBitStreamPositionForNetTrace(const UE::Net::FNetBitStreamWriter& Stream)
{
	return (uint32(Stream.IsOverflown()) - 1U) & Stream.GetAbsolutePosBits();
}
