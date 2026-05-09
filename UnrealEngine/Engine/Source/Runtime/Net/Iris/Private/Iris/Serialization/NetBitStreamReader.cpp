// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamUtils.h"
#include "Iris/IrisConfig.h"
#include "HAL/PlatformMemory.h"
#include "Misc/AssertionMacros.h"

// 仅在 UE_NETBITSTREAMREADER_VALIDATE 宏开启时启用内部一致性 check（非 Shipping 构建默认开启）。
// 开启会有轻微性能影响，用于捕获子流未 commit/discard、对 invalid 流进行读取等错误。
#if UE_NETBITSTREAMREADER_VALIDATE
#	define UE_NETBITSTREAMREADER_CHECK(expr) check(expr)
#	define UE_NETBITSTREAMREADER_CHECKF(expr, format, ...) checkf(expr, format, ##__VA_ARGS__)
#else
#	define UE_NETBITSTREAMREADER_CHECK(...) 
#	define UE_NETBITSTREAMREADER_CHECKF(...) 
#endif

namespace UE::Net
{

/** 默认构造：所有字段清零，处于"未初始化"状态。必须调用 InitBits 后才能使用。 */
FNetBitStreamReader::FNetBitStreamReader()
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

/** 析构：仅做合法性 check——若仍持有未 commit/discard 的子流，触发 checkf。 */
FNetBitStreamReader::~FNetBitStreamReader()
{
	UE_NETBITSTREAMREADER_CHECKF(!bHasSubstream, TEXT("FNetBitStreamReader is destroyed with active substream. "));
}

/**
 * 初始化 Reader，把外部 buffer 接到本对象上。
 *  - Buffer：必须 4 字节对齐的 uint32 数组；字节序按 INTEL_ORDER32 解释。
 *  - BitCount：允许读取的比特数（逻辑上限）。
 * 预加载：若 BitCount > 0，把 Buffer[0] 读进 PendingWord，减少第一个 ReadBits 的访存。
 */
void FNetBitStreamReader::InitBits(const void* InBuffer, uint32 BitCount)
{
	check(InBuffer != nullptr);
	checkf((UPTRINT(InBuffer) & 3) == 0, TEXT("Buffer needs to be 4-byte aligned."));
	// Re-initializing a substream or while having an active substream is not supported.
	// 不支持对子流/持有子流的父流重复初始化——那会让外部关联状态与 Buffer 不一致。
	UE_NETBITSTREAMREADER_CHECK(!bHasSubstream && !bIsSubstream);

	Buffer = static_cast<const uint32*>(InBuffer);
	BufferBitCapacity = BitCount;
	BufferBitPosition = 0;
	if (BitCount > 0)
	{
		PendingWord = INTEL_ORDER32(Buffer[0]);
	}
}

/**
 * 读取 BitCount 个比特（返回值的低 BitCount 位存放结果，其余清零）。
 * 算法分两种情况：
 *  1. 当前 word 剩余 > BitCount：直接从 PendingWord 中位移 & 掩码返回，不触发访存。
 *  2. 跨 word 边界：先取 PendingWord 的高位部分，再读取下一个 word 拼接；加载新 word 为后续读取做准备。
 * 边界：溢出后直接返回 0，且容量不足时标脏 OverflowBitCount。
 */
uint32 FNetBitStreamReader::ReadBits(uint32 BitCount)
{
	// Must be valid and must not read from main stream if it has a substream. Technically the latter would work, as we're just reading, but it's weird.
	// 不允许对 invalid 的（已被 commit/discard）或持有活跃子流的父流直接读取——后者虽然理论上读操作无副作用，但语义上容易出错。
	UE_NETBITSTREAMREADER_CHECK((!bIsInvalid) & (!bHasSubstream));

	if (OverflowBitCount != 0)
	{
		return 0U;
	}

	if (BufferBitCapacity - BufferBitPosition < BitCount)
	{
		// 剩余不足 → 标脏，后续所有读操作将 no-op。
		OverflowBitCount = BitCount - (BufferBitCapacity - BufferBitPosition);
		return 0U;
	}

	const uint32 CurrentBufferBitPosition = BufferBitPosition;
	const uint32 BitCountUsedInWord = BufferBitPosition & 31;            // 当前 word 内已消耗位数
	const uint32 BitCountLeftInWord = 32 - (BufferBitPosition & 31);     // 当前 word 内剩余位数

	BufferBitPosition += BitCount;

	// If after the read we still have unused bits in the PendingWord we can skip loading a new word.
	// 情况 1：全部待读比特都在当前 word 内，无需加载新 word。
	if (BitCountLeftInWord > BitCount)
	{
		const uint32 PendingWordMask = ((1U << BitCount) - 1U);
		const uint32 Value = (PendingWord >> BitCountUsedInWord) & PendingWordMask;
		return Value;
	}
	else
	{
		// 情况 2：跨 word，先取当前 word 的高位部分。
		uint32 Value = PendingWord >> BitCountUsedInWord;
		if ((BufferBitPosition & ~31U) < BufferBitCapacity)
		{
			// BitCountToRead will be in range [0, 31] as we've already written at least one bit at this point
			// 还需从下一个 word 读取 BitCountToRead 位（范围 [0, 31]）并拼到 Value 高位。
			const uint32 BitCountToRead = BitCount - BitCountLeftInWord;
			const uint32 Word = INTEL_ORDER32(Buffer[BufferBitPosition >> 5U]);
			const uint32 WordMask = (1U << BitCountToRead) - 1U;

			Value = ((Word & WordMask) << (BitCountLeftInWord & 31)) | Value;
			PendingWord = Word;                                              // 下次 ReadBits 的基础缓存
		}

		return Value;
	}
}

/**
 * 批量读取任意位数到目标 uint32* Dst。
 *  - 先刷新 BufferBitPosition 到末尾（后续所有处理都用局部变量 CurSrcBit），
 *    并把新的 PendingWord 预取好。
 *  - 主循环分"源 buffer 按字节对齐"的快路径（FMemcpy）和通用跨字路径。
 *  - 尾部零散比特用 BitStreamUtils::GetBits 读取，保护 Dst 未涉及位。
 */
void FNetBitStreamReader::ReadBitStream(uint32* InDst, uint32 BitCount)
{
	// Must be valid and must not read from main stream if it has a substream. Technically the latter would work, as we're just reading, but it's weird.
	UE_NETBITSTREAMREADER_CHECK((!bIsInvalid) & (!bHasSubstream));

	if (OverflowBitCount != 0)
	{
		return;
	}

	if (BufferBitCapacity - BufferBitPosition < BitCount)
	{
		OverflowBitCount = BitCount - (BufferBitCapacity - BufferBitPosition);
		return;
	}

	uint32 CurSrcBit = BufferBitPosition;
	const uint32* RESTRICT Src = Buffer;
	uint32* RESTRICT Dst = InDst;
	uint32 DstWordOffset = 0;
	uint32 BitCountToCopy = BitCount;

	// We can adjust the final bit position here as we're only using the above variables from here on
	// 后续逻辑只用局部变量 CurSrcBit，可以先把 Reader 的 BufferBitPosition 直接推到末尾。
	BufferBitPosition += BitCount;
	// Make sure PendingWord is up to date unless we've reached the end of the stream
	if (BufferBitPosition < BufferBitCapacity)
	{
		PendingWord = INTEL_ORDER32(Src[BufferBitPosition >> 5U]);
	}

	// Copy full words
	if (BitCountToCopy >= 32U)
	{
		// Fast path for byte aligned source buffer.
		// 快路径：源位偏移按字节对齐（CurSrcBit & 7 == 0），直接 Memcpy 整 word。
		if ((CurSrcBit & 7) == 0)
		{
			const uint32 WordCountToCopy = BitCountToCopy >> 5U;
			FPlatformMemory::Memcpy(Dst, reinterpret_cast<const uint8*>(Src) + (CurSrcBit >> 3U), WordCountToCopy*sizeof(uint32));
			DstWordOffset += WordCountToCopy;
		}
		else
		{
			// We know that each 32 bit copy straddles two words from Src as CurSrcBit % 32 != 0, 
			// else the fast path above would be used.
			// 通用路径：每个目标 word 都要从源的两个相邻 word 拼出。
			const uint32 PrevWordShift = CurSrcBit & 31U;
			const uint32 NextWordShift = (32U - CurSrcBit) & 31U;

			// Set up initial Word so we can do a single read in each loop iteration.
			uint32 SrcWordOffset = CurSrcBit >> 5U;
			uint32 PrevWord = INTEL_ORDER32(Src[SrcWordOffset]);
			++SrcWordOffset;
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
		BitCountToCopy &= 31U;
	}

	// 尾部不足 32 位的零散比特：读出后与 Dst 原值的未涉及位合并（不覆盖尾部高位）。
	if (BitCountToCopy)
	{
		const uint32 Word = INTEL_ORDER32(Dst[DstWordOffset]);
		const uint32 SrcWord = BitStreamUtils::GetBits(Src, CurSrcBit, BitCountToCopy);
		const uint32 SrcMask = (1U << BitCountToCopy) - 1U;
		const uint32 DstWord = (Word & ~SrcMask) | (SrcWord & SrcMask);
		Dst[DstWordOffset] = INTEL_ORDER32(DstWord);
	}
}

/**
 * 跳转到"相对于流/子流起点"的 BitPosition。
 * - 新位置越界（或 uint32 wrap-around） → 置溢出；
 * - 合法则清除溢出并重新加载 PendingWord。
 */
void FNetBitStreamReader::Seek(uint32 BitPosition)
{
	UE_NETBITSTREAMREADER_CHECK((!bIsInvalid) & (!bHasSubstream));

	const uint32 AdjustedBitPosition = BitPosition + BufferBitStartOffset;
	// We handle uint32 overflow as well which makes this code a bit more complicated. The OverflowBitCount may not always end up correct, but will be at least 1.
	// 同时处理 uint32 加法回绕：若 AdjustedBitPosition 反而变小则说明 BitPosition 太大。
	if ((AdjustedBitPosition > BufferBitCapacity) | (AdjustedBitPosition < BitPosition))
	{
		OverflowBitCount = FPlatformMath::Max(AdjustedBitPosition, BufferBitCapacity + 1U) - BufferBitCapacity;
		return;
	}

	OverflowBitCount = 0;

	BufferBitPosition = AdjustedBitPosition;
	if ((BufferBitPosition & ~31U) < BufferBitCapacity)
	{
		PendingWord = INTEL_ORDER32(Buffer[BufferBitPosition >> 5U]);
	}
}

/** 强制标脏：Seek 到容量+1 的位置即会触发溢出分支。 */
void FNetBitStreamReader::DoOverflow()
{
	if (OverflowBitCount == 0)
	{
		Seek(BufferBitCapacity + 1);
	}
}

/**
 * 创建子流（详细语义见头文件注释）。
 * 实现思路：memcpy 自己一份副本，然后调整 StartOffset 和 BitCapacity。
 * 关键点：父流若已溢出，子流必须同样立即溢出——通过把 BitCapacity 设为 StartOffset 强制"0 剩余容量"。
 */
FNetBitStreamReader FNetBitStreamReader::CreateSubstream(uint32 MaxBitCount)
{
	UE_NETBITSTREAMREADER_CHECK((!bIsInvalid) & (!bHasSubstream));

	// Create a copy of this stream and overwrite the necessary members.
	FNetBitStreamReader Substream = *this;
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
		// 子流容量 = 从当前位置开始、不超过 MaxBitCount 与父流剩余量中较小者。
		Substream.BufferBitCapacity = BufferBitPosition + FPlatformMath::Min(MaxBitCount, BufferBitCapacity - BufferBitPosition);
	}

	return Substream;
}

/**
 * 提交子流：把子流读到的进度合并回父流。
 * 合法性检查（ensure，非致命）：
 *  - 本流确实持有活跃子流；
 *  - Substream 本身没有再嵌套子流；
 *  - 两端都不是 invalid；
 *  - Buffer 指针一致；
 *  - 父流位置未被挪过（仍等于子流 StartOffset）。
 * 子流溢出则本次 commit 被忽略（父流位置不变），但子流仍被置 invalid 防止再次使用。
 */
void FNetBitStreamReader::CommitSubstream(FNetBitStreamReader& Substream)
{
	// Only accept substreams iff this is the parent and the substream has not overflown and has not previously been commited or discarded.
	if (!ensure(bHasSubstream & (!Substream.bHasSubstream) & (!bIsInvalid) & (!Substream.bIsInvalid) & (Buffer == Substream.Buffer) & (BufferBitPosition == Substream.BufferBitStartOffset)))
	{
		return;
	}

	if (!Substream.IsOverflown())
	{
		BufferBitPosition = Substream.BufferBitPosition;
		if ((Substream.BufferBitPosition & ~31U) < BufferBitCapacity)
		{
			PendingWord = INTEL_ORDER32(Buffer[Substream.BufferBitPosition >> 5U]);
		}
	}

	bHasSubstream = 0;
	Substream.bIsInvalid = 1;
}

/**
 * 丢弃子流：父流位置/状态保持不变，子流被标脏为 invalid。
 * 合法性 check 与 CommitSubstream 相同。
 */
void FNetBitStreamReader::DiscardSubstream(FNetBitStreamReader& Substream)
{
	// Only accept substreams iff this is the parent and the substream has not previously been commited or discarded.
	if (!ensure(bHasSubstream & (!Substream.bHasSubstream) & (!bIsInvalid) & (!Substream.bIsInvalid) & (Buffer == Substream.Buffer) & (BufferBitPosition == Substream.BufferBitStartOffset)))
	{
		return;
	}

	bHasSubstream = 0;
	Substream.bIsInvalid = 1;
}

}

#undef UE_NETBITSTREAMREADER_CHECK
#undef UE_NETBITSTREAMREADER_CHECKF
