// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include <stdlib.h>
#include "Misc/ByteSwap.h"

namespace UE::Net::BitStreamUtils
{

/**
 * 从 Src 中"任意起始比特 SrcBit"读取 BitCount 位并以 uint32 返回（低位存结果）。
 *  - 仅用于 NetBitStreamReader.cpp / NetBitStreamWriter.cpp 内部支持跨 word 的零散比特提取，属 Private API。
 *  - BitCount 必须在 [1, 31] 范围内（不能 0，也不能 32，32 位读取调用方自己处理）。
 *  - 使用无分支技巧处理"是否跨 word":
 *      Word1Mask = WordOffset0 - WordOffset1 产生 0（同一个 word）或 0xFFFFFFFF（跨 word，补码减法下溢）。
 *    从而避免条件分支，CPU 流水线更友好。
 *  - 假设 Src 是 4 字节对齐、INTEL_ORDER32 小端存储。
 *
 * @param Src      原始 uint32 数组。
 * @param SrcBit   起始绝对比特位置。
 * @param BitCount 要读取的位数（1~31）。
 * @return         读出的低 BitCount 位的值，高位为 0。
 */
// BitCount is in range [1, 31]
inline uint32 GetBits(const uint32* Src, uint32 SrcBit, uint32 BitCount)
{
	const uint32 ShiftAmount0 = SrcBit & 31U;
	 // Only masking with 31 to avoid undefined behavior. Otherwise 32U - ShiftAmount0 could have been used because Word1 would be masked away anyway.
	// 掩码 31：避免 SrcBit 恰好对齐时 (32 - 0) 造成的左移 32 未定义行为。
	const uint32 ShiftAmount1 = (32U - SrcBit) & 31U;

	const uint32 WordOffset0 = SrcBit >> 5U;                            // 起始比特所在 word 下标
	const uint32 WordOffset1 = (SrcBit + BitCount - 1) >> 5U;           // 结束比特所在 word 下标

	const uint32 Word0 = INTEL_ORDER32(Src[WordOffset0]) >> ShiftAmount0;
	const uint32 Word1 = INTEL_ORDER32(Src[WordOffset1]) << ShiftAmount1;

	// WordOffset1 is either WordOffset0 or WordOffset0+1. By subtracting WordOffset0 from WordOffset1
	// the desired mask 0xFFFFFFFF is produced if the offsets differ and 0 if they are the same.
	// WordOffset1 只能是 WordOffset0 或 WordOffset0+1；相减后用于驱动"无分支选择":
	//  - 同一个 word → 0 → Word1 被屏蔽；
	//  - 跨 word    → 0-1 = 0xFFFFFFFF → 保留 Word1。
	const uint32 Word1Mask = WordOffset0 - WordOffset1;
	const uint32 WordMask = (1U << BitCount) - 1U;

	const uint32 Word = ((Word1 & Word1Mask) | Word0) & WordMask;
	return Word;
}

}
