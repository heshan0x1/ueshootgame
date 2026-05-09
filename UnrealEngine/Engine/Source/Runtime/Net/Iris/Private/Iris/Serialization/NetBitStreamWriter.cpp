// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetBitStreamUtils.h"
#include "Iris/IrisConfigInternal.h"
#include "HAL/PlatformMath.h"
#include "HAL/PlatformMemory.h"
#include "Misc/AssertionMacros.h"
#include "Templates/AlignmentTemplates.h"

// 仅在 UE_NETBITSTREAMWRITER_VALIDATE 宏开启时启用内部一致性 check（默认在非 Shipping 构建下开启）。
// 主要用于捕获：字节对齐违规、子流未 commit/discard、对 invalid 流的误用等。
#if UE_NETBITSTREAMWRITER_VALIDATE
#	define UE_NETBITSTREAMWRITER_CHECK(expr) check(expr)
#	define UE_NETBITSTREAMWRITER_CHECKF(expr, format, ...) checkf(expr, format, ##__VA_ARGS__)
#else
#	define UE_NETBITSTREAMWRITER_CHECK(...) 
#	define UE_NETBITSTREAMWRITER_CHECKF(...) 
#endif

namespace UE::Net
{

/** 默认构造：所有字段清零，处于"未初始化"状态，必须调用 InitBytes 后才能使用。 */
FNetBitStreamWriter::FNetBitStreamWriter()
: Buffer(nullptr)
, BufferBitCapacity(0)
, BufferBitStartOffset(0)
, BufferBitPosition(0)
, PendingWord(0)
, OverflowBitCount(0)
, bHasSubstream(0)
, bIsSubstream(0)
, bIsInvalid(0)
{
}

/**
 * 析构：
 *  - 若仍持有活跃子流，触发 checkf（子流必须显式 commit/discard）。
 *  - 隐式调用 CommitWrites 把 PendingWord 刷回 Buffer，防止最后半 word 丢失。
 */
FNetBitStreamWriter::~FNetBitStreamWriter()
{
	UE_NETBITSTREAMWRITER_CHECKF(!bHasSubstream, TEXT("%s"), TEXT("FNetBitStreamWriter is destroyed with active substream. Substreams must be commited or discarded."));
	CommitWrites();
}

/**
 * 绑定 Buffer：
 *  - Buffer 必须 4 字节对齐；
 *  - ByteCount 必须 4 的倍数（保证按 uint32 word 处理时不越界）；
 *  - 重置位置与溢出状态；
 *  - 预取 Buffer[0] 到 PendingWord（后续 WriteBits 第一次就能读取到已有内容，避免意外擦除 buffer 中未写区）。
 */
void FNetBitStreamWriter::InitBytes(void* InBuffer, uint32 ByteCount)
{
	UE_NETBITSTREAMWRITER_CHECK(InBuffer != nullptr);
	UE_NETBITSTREAMWRITER_CHECKF((uintptr_t(InBuffer) & 3) == 0, TEXT("Buffer needs to be 4-byte aligned."));
	UE_NETBITSTREAMWRITER_CHECKF((ByteCount & 3) == 0, TEXT("Buffer capacity needs to be a multiple of 4."));
	UE_NETBITSTREAMWRITER_CHECK(!bHasSubstream && !bIsSubstream);

	Buffer = static_cast<uint32*>(InBuffer);
	BufferBitCapacity = ByteCount*8U;
	BufferBitPosition = 0U;
	OverflowBitCount = 0U;

	if (ByteCount >= 4)
	{
		PendingWord = INTEL_ORDER32(Buffer[0]);
	}
}

/**
 * 核心热路径：写入 Value 的低 BitCount 位。
 * 实现策略（与 ReadBits 对偶）：
 *  - 情况 1：当前 word 剩余位数 > BitCount → 仅更新 PendingWord，不触及 Buffer（最常见、最快）。
 *  - 情况 2：跨 word → 先把当前 word 刷回 Buffer，再从 Buffer 加载下一个 word 的已有内容到 PendingWord
 *    （因为 substream 的 Capacity 不一定是 32 的倍数，需要保留尾部未涉及位）。
 * 溢出判定发生在函数很早的地方：若空间不足立即标脏返回，避免写到 buffer 外。
 */
void FNetBitStreamWriter::WriteBits(uint32 Value, uint32 BitCount)
{
	UE_NETBITSTREAMWRITER_CHECK((!bIsInvalid) & (!bHasSubstream));

	if (OverflowBitCount != 0)
	{
		return;
	}

	if (BufferBitCapacity - BufferBitPosition < BitCount)
	{
		OverflowBitCount = BitCount - (BufferBitCapacity - BufferBitPosition);
		return;
	}

	const uint32 CurrentBufferBitPosition = BufferBitPosition;
	const uint32 BitCountUsedInWord = BufferBitPosition & 31;          // 当前 word 内已写位数
	const uint32 BitCountLeftInWord = 32 - (BufferBitPosition & 31);   // 当前 word 内剩余位数

	BufferBitPosition += BitCount;

	// If after the write we still have unused bits in the PendingWord we can skip storing and loading a new word.
	// 情况 1：整条写入都落在当前 word 内，只更新 PendingWord。
	if (BitCountLeftInWord > BitCount)
	{
		const uint32 ValueMask = ((1U << BitCount) - 1U) << BitCountUsedInWord;
		PendingWord = ((Value << BitCountUsedInWord) & ValueMask) | (PendingWord & ~ValueMask);
	}
	else
	{
		// Both BitCountLeftInWord and BitCount is in range [1, 32]
		// BitCountUsedInWord is in range [0, 31]

		// We know that we're going to fill an entire word to start with. We can use that
		// fact to avoid unnecessary masking of the input Value.
		// 情况 2：至少要填满当前 word。这里先算出首个完整 word 并写回 Buffer。
		const uint32 PendingWordMask = (1U << BitCountUsedInWord) - 1U;
		const uint32 FirstWord = (Value << BitCountUsedInWord) | (PendingWord & PendingWordMask);
		Buffer[CurrentBufferBitPosition >> 5U] = INTEL_ORDER32(FirstWord);

		// If we're at the end of the buffer we cannot load a new word as that can cause
		// a read access violation. We also know that we've written everything we should have
		// as we check for overflow very early in this function.
		// Substreams may have a BufferBitCapacity which isn't evenly divisible by 32.
		// It's ok, and necessary, for such a substream to read up to the rounded up capacity.
		// 若新位置仍在 Align(Capacity, 32) 范围内，加载下一个 word 到 PendingWord，保留其未涉及位。
		if (BufferBitPosition < Align(BufferBitCapacity, 32U))
		{
			// BitCountToWrite will be in range [0, 31] as we've already written at least one bit at this point
			const uint32 BitCountToWrite = BitCount - BitCountLeftInWord;
			const uint32 ValueMask = (1U << BitCountToWrite) - 1U; // Zero if BitCountToWrite == 0

			const uint32 SecondWord = INTEL_ORDER32(Buffer[BufferBitPosition >> 5U]);
			PendingWord = (SecondWord & ~ValueMask) | ((Value >> (BitCountLeftInWord & 31U)) & ValueMask);
		}
	}
}

/**
 * 批量写入任意位数（镜像 ReadBitStream）。
 * 步骤：
 *  1. 先把当前 PendingWord 的"起始零散部分"补满（保证后续每次都是整 word 对齐）。
 *  2. 主循环：源字节对齐走 Memcpy 快路径；非对齐走双 word 拼接通用路径。
 *  3. 尾部不足 32 位的残余进 PendingWord，保留 Buffer 里未涉及位。
 */
void FNetBitStreamWriter::WriteBitStream(const uint32* InSrc, uint32 SrcBitOffset, uint32 BitCount)
{
	UE_NETBITSTREAMWRITER_CHECK((!bIsInvalid) & (!bHasSubstream));

	if ((OverflowBitCount != 0) || (BitCount == 0))
	{
		return;
	}

	if (BufferBitCapacity - BufferBitPosition < BitCount)
	{
		OverflowBitCount = BitCount - (BufferBitCapacity - BufferBitPosition);
		return;
	}

	const uint32* RESTRICT Src = InSrc;
	uint32 CurSrcBit = SrcBitOffset;
	uint32* RESTRICT Dst = Buffer;
	uint32 CurDstBit = BufferBitPosition;
	uint32 BitCountToCopy = BitCount;

	// We can adjust the final bit position here as we're only using CurDstBit from here on
	BufferBitPosition += BitCount;

	// Fill pending word so we can store a word at a time in the main loop
	// 阶段 1：把当前 word 的零散位填满，使后续 CurDstBit 恰好落在 word 边界。
	if (const uint32 BitCountUsedInWord = (CurDstBit & 31U))
	{
		const uint32 BitCountToWrite = FPlatformMath::Min(32U - BitCountUsedInWord, BitCount);
		const uint32 SrcWord = BitStreamUtils::GetBits(Src, CurSrcBit, BitCountToWrite);

		const uint32 Mask = ((1U << BitCountToWrite) - 1U) << BitCountUsedInWord;
		const uint32 StoreWord = (SrcWord << BitCountUsedInWord) | (PendingWord & ~Mask);
		Dst[CurDstBit >> 5U] = INTEL_ORDER32(StoreWord);

		CurSrcBit += BitCountToWrite;
		CurDstBit += BitCountToWrite;
		BitCountToCopy -= BitCountToWrite;
	}

	// Copy full words
	// 阶段 2：完整 word 大块拷贝。
	if (BitCountToCopy >= 32U)
	{
		// Fast path for byte aligned source buffer.
		// 源按字节对齐 → Memcpy 快路径。
		if ((CurSrcBit & 7) == 0)
		{
			const size_t WordCountToCopy = BitCountToCopy >> 5U;
			FPlatformMemory::Memcpy(Dst + (CurDstBit >> 5U), reinterpret_cast<const uint8*>(Src) + (CurSrcBit >> 3U), WordCountToCopy*sizeof(uint32));
		}
		else
		{
			// We know that each 32 bit copy straddles two words from Src as CurSrcBit % 32 != 0, 
			// else the fast path above would be used. Also note that DstSrcBit % 32 == 0
			// which allows us to perform a single store in each loop iteration.
			// 通用路径：目标对齐、源不对齐 → 每次拼两个源 word 成一个目标 word。
			const uint32 PrevWordShift = CurSrcBit & 31U;
			const uint32 NextWordShift = (32U - CurSrcBit) & 31U;

			// Set up initial Word so we can do a single read in each loop iteration.
			uint32 SrcWordOffset = CurSrcBit >> 5U;
			uint32 PrevWord = INTEL_ORDER32(Src[SrcWordOffset]);
			++SrcWordOffset;
			uint32 DstWordOffset = (CurDstBit >> 5U);
			for (uint32 WordIt = 0, WordEndIt = (BitCountToCopy >> 5U); WordIt != WordEndIt; ++WordIt, ++SrcWordOffset, ++DstWordOffset)
			{
				const uint32 NextWord = INTEL_ORDER32(Src[SrcWordOffset]);
				const uint32 Word = (NextWord << NextWordShift) | (PrevWord >> PrevWordShift);
				Dst[DstWordOffset] = INTEL_ORDER32(Word);
				PrevWord = NextWord;
			}
		}

		const uint32 BitCountCopied = (BitCountToCopy & ~31U);
		CurSrcBit += BitCountCopied;
		CurDstBit += BitCountCopied;
		BitCountToCopy &= 31U;
	}

	// 阶段 3：尾部不足 32 位的残余 → 写入 PendingWord；否则刷新 PendingWord 为新 word 的 snapshot。
	if (BitCountToCopy)
	{
		// Bear in mind that we've already made sure that CurDstBit % 32 == 0 if we're entering this path.
		const uint32 Word = INTEL_ORDER32(Dst[CurDstBit >> 5U]);
		const uint32 SrcWord = BitStreamUtils::GetBits(Src, CurSrcBit, BitCountToCopy);
		const uint32 SrcMask = (1U << BitCountToCopy) - 1U;
		PendingWord = (Word & ~SrcMask) | (SrcWord & SrcMask);
	}
	else
	{
		if (CurDstBit < Align(BufferBitCapacity, 32U))
		{
			PendingWord = INTEL_ORDER32(Dst[CurDstBit >> 5U]);
		}
	}
}

/**
 * 显式把 PendingWord 刷回 Buffer。
 * 注意只有当 BufferBitPosition 仍在 Align(Capacity, 32) 范围内时才写——防止越界。
 */
void FNetBitStreamWriter::CommitWrites()
{
	if ((!bIsInvalid) & (BufferBitPosition < Align(BufferBitCapacity, 32)))
	{
		Buffer[BufferBitPosition >> 5U] = INTEL_ORDER32(PendingWord);
	}
}

/**
 * Seek 到相对于流/子流起点的 BitPosition。
 * 流程：
 *  1. 越界 / wrap-around 检测 → 置溢出返回；
 *  2. 先把旧 PendingWord 刷回 Buffer（相当于 CommitWrites）；
 *  3. 更新 BufferBitPosition；
 *  4. 重新从 Buffer 加载新位置所在 word 的内容到 PendingWord。
 * 清除溢出：合法 Seek 会重置 OverflowBitCount = 0。
 */
void FNetBitStreamWriter::Seek(uint32 BitPosition)
{
	UE_NETBITSTREAMWRITER_CHECK((!bIsInvalid) & (!bHasSubstream));

	const uint32 AdjustedBitPosition = BitPosition + BufferBitStartOffset;
	// We handle uint32 overflow as well which makes this code a bit more complicated. The OverflowBitCount may not always end up correct, but will be at least 1.
	if ((AdjustedBitPosition > BufferBitCapacity) | (AdjustedBitPosition < BitPosition))
	{
		OverflowBitCount = FPlatformMath::Max(AdjustedBitPosition, BufferBitCapacity + 1U) - BufferBitCapacity;
		return;
	}

	OverflowBitCount = 0;

	const uint32 AlignedBufferBitCapacity = Align(BufferBitCapacity, 32U);
	// Commit data in PendingWord
	// 旧 PendingWord 刷回 Buffer——避免 Seek 后丢失未提交数据。
	if (BufferBitPosition < AlignedBufferBitCapacity)
	{
		Buffer[BufferBitPosition >> 5U] = INTEL_ORDER32(PendingWord);
	}

	// Populate PendingWord with new data from new position
	// 从新位置重新加载 PendingWord。
	BufferBitPosition = AdjustedBitPosition;
	if (BufferBitPosition < AlignedBufferBitCapacity)
	{
		PendingWord = INTEL_ORDER32(Buffer[BufferBitPosition >> 5U]);
	}
}

/** 强制标脏：Seek 到 Capacity+1 的位置触发溢出分支。 */
void FNetBitStreamWriter::DoOverflow()
{
	if (OverflowBitCount == 0)
	{
		Seek(BufferBitCapacity + 1);
	}
}


/**
 * 创建子流（语义详见头文件）。
 * 实现：memcpy 自身状态后调整 StartOffset 与 Capacity。
 * 关键：父流已溢出时，子流强制进入溢出状态以防止任何实际写入。
 */
FNetBitStreamWriter FNetBitStreamWriter::CreateSubstream(uint32 MaxBitCount)
{
	UE_NETBITSTREAMWRITER_CHECK((!bIsInvalid) & (!bHasSubstream));

	// Create a copy of this stream and overwrite the necessary members.
	FNetBitStreamWriter Substream = *this;
	Substream.BufferBitStartOffset = BufferBitPosition;
	Substream.bHasSubstream = 0;
	Substream.bIsSubstream = 1;

	bHasSubstream = 1;

	/* If this stream is overflown make sure the substream will always be overflown as well!
	 * We must be careful to ensure that a seek to the beginning of this stream will still cause the substream to be overflown.
	 * We can ignore MaxBitCount completely because no writes will succeed anyway.
	 */
	if (OverflowBitCount)
	{
		Substream.BufferBitCapacity = Substream.BufferBitStartOffset;
		// It's not vital that the OverflowBitCount is set as the user can reset it with a Seek(0) call. In any case no modifications to the bitstream can be done.
		Substream.OverflowBitCount = OverflowBitCount;
	}
	else
	{
		Substream.BufferBitCapacity = BufferBitPosition + FPlatformMath::Min(MaxBitCount, BufferBitCapacity - BufferBitPosition);
	}

	return Substream;
}

/**
 * 提交子流：先让子流 CommitWrites 把自身 PendingWord 刷回 Buffer，再把父流位置推进到子流末尾，
 * 然后从 Buffer 重新加载父流自己的 PendingWord（注意：子流可能已经向共享 Buffer 写入，父流必须重新读）。
 * 若子流已溢出则本次 commit 整体忽略，但子流仍被置 invalid。
 */
void FNetBitStreamWriter::CommitSubstream(FNetBitStreamWriter& Substream)
{
	/* Only accept substreams iff this is the parent and the substream has not overflown
	 * and has not previously been commited or discarded.
	 */
	if (!ensure(bHasSubstream & (!Substream.bHasSubstream) & (!bIsInvalid) & (!Substream.bIsInvalid) & (Buffer == Substream.Buffer) & (BufferBitPosition == Substream.BufferBitStartOffset)))
	{
		return;
	}

	if (!Substream.IsOverflown())
	{
		Substream.CommitWrites();
		BufferBitPosition = Substream.BufferBitPosition;
		if (Substream.BufferBitPosition < Align(BufferBitCapacity, 32U))
		{
			PendingWord = INTEL_ORDER32(Buffer[Substream.BufferBitPosition >> 5U]);
		}
	}

	bHasSubstream = 0;
	Substream.bIsInvalid = 1;
}

/**
 * 丢弃子流：父流位置保持不变，子流置 invalid。
 * 警告：子流已经向 Buffer 写入的字节不会被自动清除；调用者通常配合 FNetBitStreamRollbackScope
 * 让后续写入从父流原位置继续，从而覆盖脏字节。
 */
void FNetBitStreamWriter::DiscardSubstream(FNetBitStreamWriter& Substream)
{
	/* Only accept substreams iff this is the parent and the substream has not overflown
	 * and has not previously been commited or discarded. The substream may not have active substreams.
	 */
	if (!ensure(bHasSubstream & (!Substream.bHasSubstream) & (!bIsInvalid) & (!Substream.bIsInvalid) & (Buffer == Substream.Buffer) & (BufferBitPosition == Substream.BufferBitStartOffset)))
	{
		return;
	}

	bHasSubstream = 0;
	Substream.bIsInvalid = 1;
}

}

#undef UE_NETBITSTREAMWRITER_CHECK
#undef UE_NETBITSTREAMWRITER_CHECKF
