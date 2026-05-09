// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace UE::Net
{

/**
 * FNetBitStreamReader —— Iris 序列化层的按位读取器（位流 Reader）。
 *
 * 职责：
 *  - 在一段 4 字节对齐、以 uint32 小端（INTEL_ORDER32）方式存储的连续缓冲区上，
 *    提供"按任意位宽读取"的原语（ReadBits / ReadBool / ReadBitStream）。
 *  - 维护"溢出（overflow）"语义：一旦想读取的位数超过剩余容量，后续所有读操作都会变成 no-op，
 *    并返回零值，直到调用 Seek 回到合法位置为止。
 *  - 支持 substream（子流）机制：从当前位置切出一段独立读取窗口，窗口内的读位置与溢出状态
 *    不会污染父流；commit 时把子流消耗的比特数"吃掉"，discard 时丢弃。
 *
 * 与 FNetBitStreamWriter 对偶；两者合起来构成整个 Iris 序列化的比特传输层。
 *
 * 线程安全：**非线程安全**。典型使用方式是每条连接/每次反序列化调用独占一个实例。
 *
 * 约束：
 *  - Buffer 必须 4 字节对齐（由 InitBits 内部 check 保证）。
 *  - 不能跨越已分配 buffer Seek（会直接进入 overflow 状态）。
 *  - 父流存在活跃 substream 期间禁止自身读取（UE_NETBITSTREAMREADER_CHECK 保护）。
 */
class FNetBitStreamReader
{
public:
	/** 构造一个未初始化的 Reader。必须调用 InitBits 后才能读取。 */
	IRISCORE_API FNetBitStreamReader();

	/** 析构。若仍持有活跃 substream（bHasSubstream 置位）会触发 checkf 报错——子流必须先 commit/discard。 */
	IRISCORE_API ~FNetBitStreamReader();

	/**
	 * InitBits must be called before reading from the stream.
	 * @param Buffer The buffer must be at least 4-byte aligned.
	 * @param BitCount The number of bits that is allowed to be read from the buffer.
	 *
	 * 初始化 Reader：
	 *  - Buffer 必须 4 字节对齐（函数内部 check）。
	 *  - BitCount 是"逻辑上"允许读取的位数，可以不是 8 的倍数（例如子包末尾可能有非 8 对齐的剩余）。
	 *  - 首次 InitBits 会将第一个 word（4 字节）加载到 PendingWord，减少后续热路径的内存访问。
	 */
	IRISCORE_API void InitBits(const void* Buffer, uint32 BitCount);

	/**
	 * Reads BitCount bits that are stored in the least significant bits in the return value. Other bits
	 * will be set to zero. If the BitCount exceeds the remaining space the function will return zero 
	 * and the stream will be marked as overflown.
	 *
	 * 读取 BitCount（范围 [0, 32]）个比特，结果放在返回值的低 BitCount 位，高位清零。
	 * 边界条件：
	 *  - 已 overflow → 直接返回 0，不推进位置；
	 *  - 剩余容量不足 → 标记溢出并返回 0；
	 *  - 读取跨越 word 边界时会自动加载下一个 word 到 PendingWord。
	 */
	IRISCORE_API uint32 ReadBits(uint32 BitCount);

	/**
	 * Reads a bool from the stream and returns the value,
	 * A failed read will always return false and stream will be marked as overflown
	 *
	 * 读取 1 位并解释为 bool。失败/溢出统一返回 false。
	 */
	bool ReadBool() { return ReadBits(1) & 1U; }

	/**
	 * Reads BitCount bits and stores them in Dst, starting from bit offset 0. The bits will be stored
	 * as they are stored internally in this class, i.e. bits will be written from lower to higher
	 * memory addresses.
	 * If the BitCount exceeds the remaining space no bits will be written to Dst and the stream will be
	 * marked as overflown. It's up to the user to check for overflow.
	 *
	 * 批量读取任意位数到目标 uint32 数组 Dst，按"低地址 → 高地址、低位 → 高位"的内部布局写入。
	 * 通常用于大块拷贝（如附件 payload 读取、SparseBitArray 内部字节拷贝）。
	 */
	IRISCORE_API void ReadBitStream(uint32* Dst, uint32 BitCount);

	/**
	 * Seek to a specific position from the start of the stream or substream. If the stream is overflown and you seek back to a position
	 * where you can still read bits the stream will no longer be considered overflown.
	 *
	 * 跳转到相对于"流或子流起始（BufferBitStartOffset）"的 BitPosition。
	 * 若原先 overflow 的流被 Seek 回到合法位置，会清除 OverflowBitCount（即"重新变为未溢出"）。
	 * 约束：不能跨越已分配 buffer 上限；内部用 uint32 溢出保护（BitPosition + StartOffset < BitPosition 的 wrap-around 也视为溢出）。
	 */
	IRISCORE_API void Seek(uint32 BitPosition);

	/** Returns the the current byte position. */
	/** 返回相对于流/子流起点的"字节位置"（向上取整）。 */
	inline uint32 GetPosBytes() const { return (BufferBitPosition - BufferBitStartOffset + 7) >> 3U; }

	/** Returns the current bit position */
	/** 返回相对于流/子流起点的比特位置。子流从 0 开始计数。 */
	inline uint32 GetPosBits() const { return BufferBitPosition - BufferBitStartOffset; }

	/** Returns the absolute bit position */
	/** 返回"绝对"比特位置（相对于原始 buffer[0] 的 bit 0），用于 NetTrace 以便收发端对齐比较。 */
	inline uint32 GetAbsolutePosBits() const { return BufferBitPosition; }

	/** Returns the number of bits that can be read before overflowing. */
	/** 返回剩余可读位数；溢出状态下总是返回 0。 */
	inline uint32 GetBitsLeft() const { return (OverflowBitCount ? 0U : (BufferBitCapacity - BufferBitPosition)); }

	/** Force an overflow. */
	/** 强制把流置为溢出状态（用于错误路径立即截断后续读取）。 */
	IRISCORE_API void DoOverflow();
	
	/** Returns whether the stream is overflown or not. */
	/** 查询当前是否已溢出（OverflowBitCount != 0）。 */
	inline bool IsOverflown() const { return OverflowBitCount != 0; }

	/** 
	 * Creates a substream at the current bit position. The substream must be committed or discarded. Only one active substream at a time is allowed,
	 * but a substream can have an active substream as well. Once the substream has been commited or discarded a new substream may be created. No
	 * reads may be performed on this stream until the substream has been committed or discarded.  
	 *
	 * The returned FNetBitStreamReader will have similar behavior to a newly constructed regular FNetBitStreamWriter. 
	 * 
	 * @param MaxBitCount The maximum allowed bits that may be read. The value will be clamped to the number of bits left in this stream/substream. If it's a requirement a specific size is supported you can verify it with GetBitsLeft().
	 * @return A FNetBitStreamReader.
	 *
	 * 在当前位置创建一个子流。子流与父流共享底层 buffer，但拥有独立的 BufferBitStartOffset/BitPosition/OverflowBitCount。
	 * 典型场景：按 batch 读取某个对象的状态时，先切出定长子流，避免 deserialize 读过头影响后续对象。
	 * 约束：
	 *  - 一次只能有一个活跃子流（bHasSubstream 标志）；子流自身可再切子子流（嵌套）。
	 *  - 子流未 commit/discard 前，父流禁止自身读取。
	 *  - 若父流已 overflow，返回的子流 BitCapacity == StartOffset，从而立即视为已满，保证写读一致性。
	 */
	IRISCORE_API FNetBitStreamReader CreateSubstream(uint32 MaxBitCount = ~0U);

	/**
	 * Commits a substream to this stream. Substreams that are overflown or do not belong to this stream will be ignored. 
	 * If the substream is valid then this stream's bit position will be updated.
	 *
	 * 将子流"吃回"父流：把父流的 BufferBitPosition 推进到子流的结束位置，并重新加载 PendingWord。
	 * 若子流已 overflow 或非当前父流的子流，则本次提交被忽略（但子流仍会被置 bIsInvalid 防止再次使用）。
	 */
	IRISCORE_API void CommitSubstream(FNetBitStreamReader& Substream);

	/** Discards a substream of this stream. This stream's bit position will remain intact.
	 *  丢弃子流：父流读位置保持不变，子流被标记为 invalid。 */
	IRISCORE_API void DiscardSubstream(FNetBitStreamReader& Substream);

private:
	const uint32* Buffer;                // 指向底层 uint32 数组；存储时以 INTEL_ORDER32 小端布局
	// The BufferBitCapacity is an absolute bit position indicating the bit after the last valid bit position to read.
	uint32 BufferBitCapacity;            // 绝对比特容量上限（相对 Buffer[0] bit 0）
	// For substreams this indicate the absolute bit position in the buffer where it will start reading
	uint32 BufferBitStartOffset;         // 子流起点（绝对位置）；主流恒为 0
	uint32 BufferBitPosition;            // 当前绝对比特位置
	uint32 PendingWord;                  // 当前 word 的缓存（小端化后）；减少热路径上的内存访问
	uint32 OverflowBitCount;             // 溢出指示：0 = 正常；非 0 = 超出容量的位数，所有后续读操作 no-op

	uint32 bHasSubstream : 1;            // 当前流存在活跃子流，禁止自身读取
	uint32 bIsSubstream : 1;             // 当前流自身是一个子流
	uint32 bIsInvalid : 1;               // 已被父流 commit/discard 置位；禁止再使用
};

}

// Always report the actual bitstream position, even on overflow. This normally allows for better comparisons between sending and receiving side when bitstream errors occur.
/**
 * NetTrace 辅助：获取 Reader 的绝对比特位置——即使溢出也如实返回，方便收发两端在错误时对齐比较。
 * 定义在 UE::Net 命名空间之外，是 NetTrace 宏 UE_NET_TRACE_*_BEGIN 用 ADL 查找的挂钩点。
 */
inline uint32 GetBitStreamPositionForNetTrace(const UE::Net::FNetBitStreamReader& Stream) { return Stream.GetAbsolutePosBits(); }
