// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringView.h"
#include "Misc/AssertionMacros.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/IrisConfig.h"

namespace UE::Net
{

/**
 * FNetBitStreamWriteScope —— 在指定位置临时写入的 RAII scope。
 * 进入 scope 时：记录当前位置 OriginalPos，并 Seek 到 WritePos；
 * 退出 scope 时：无条件 Seek 回 OriginalPos（即使中间抛出异常或提前 return 也能恢复）。
 *
 * 典型用途：回填"包头/长度字段"——先在起始位置 skip 留下空位，写完正文后用本 scope 回到起始位置填上实际长度，
 * 析构时自动回到正文末尾继续写。
 */
/** Simple helper to a temporary write at a specific offset in the stream and return to the original position when exiting the scope. */
class FNetBitStreamWriteScope
{
public:
	/**
	 * 进入 scope：
	 *  - check：进入时 Writer 不能已溢出；
	 *  - 记录当前位置 OriginalPos；
	 *  - Seek 到目标写入位置 WritePos。
	 */
	FNetBitStreamWriteScope(FNetBitStreamWriter& InWriter, uint32 WritePos)
	: Writer(InWriter)
	, OriginalPos(InWriter.GetPosBits())
	{
#if UE_NETBITSTREAMWRITER_VALIDATE
		check(!Writer.IsOverflown());
#endif

		Writer.Seek(WritePos);
	}

	/**
	 * 退出 scope：
	 *  - check：在 Validate 模式下要求回填后的位置不能超过原位置（否则说明越界写入了原本正文数据）；
	 *  - Seek 回 OriginalPos，让外层继续正常写入。
	 */
	~FNetBitStreamWriteScope()
	{
#if UE_NETBITSTREAMWRITER_VALIDATE
		check(Writer.GetPosBits() <= OriginalPos);
#endif
		Writer.Seek(OriginalPos);
	}

private:
	FNetBitStreamWriter& Writer;    // 外部提供的 Writer 引用，生命周期由调用者保证
	const uint32 OriginalPos;       // 进入 scope 时的原始位置，析构时恢复
};


/**
 * FNetBitStreamRollbackScope —— 若 scope 结束时 Writer 处于溢出状态，自动回滚到进入时的位置。
 *
 * 用途：保证"半写的对象状态"不会污染后续数据——
 *  典型调用序列：
 *      FNetBitStreamRollbackScope Rollback(Writer);
 *      SerializeSomeObject(...);      // 中途可能溢出
 *      if (Writer.IsOverflown()) { ... 处理错误 ... }   // 析构自动 rollback
 *
 * 也支持手动 Rollback() 显式调用（非溢出情况下的业务决策回滚，如验证失败）。
 *
 * 注意：析构时只看 IsOverflown；若中途溢出后又被 Seek 清除，则不会自动回滚。
 */
/**
 * RollbackScope, if the provided BitWriter is in an invalid state when exiting the scope, the BitWriter will be restored to the state it had when entering the FNetBitStreamRollbackScope
 */
class FNetBitStreamRollbackScope
{
public:
	/** 进入 scope：记录起始位置 StartPos。 */
	explicit FNetBitStreamRollbackScope(FNetBitStreamWriter& InWriter)
	: Writer(InWriter)
	, StartPos(InWriter.GetPosBits())
	{
	}

	/** 析构：若溢出则 Seek 回 StartPos（此操作会清除 OverflowBitCount，重新"解封"流）。 */
	~FNetBitStreamRollbackScope()
	{
		if (Writer.IsOverflown())
		{
			Writer.Seek(StartPos);
		}
	}

	/** 显式回滚：无条件 Seek 回 StartPos，常用于业务层发现错误后主动放弃已写入数据。 */
	void Rollback()
	{
		Writer.Seek(StartPos);
	}

	/** 获取 scope 起始位置，便于上层计算已写入位数等。 */
	uint32 GetStartPos() const { return StartPos; }

private:
	FNetBitStreamWriter& Writer;   // 外部提供的 Writer 引用
	const uint32 StartPos;         // 进入 scope 时的位置
};

/**
 * 写入完整 uint64（64 位，固定宽度）。拆成低 32 + 高 32 两次 WriteBits。
 * 与 WritePackedUint64 区别：此函数不做压缩，便于对齐字段 / 已知大范围数值（如 hash）。
 */
/**
 * Write a full uint64 to the provided FNetBitStreamWriter
 */
inline void WriteUint64(FNetBitStreamWriter* Writer, uint64 Value)
{
	Writer->WriteBits((uint32)Value, 32);
	Writer->WriteBits((uint32)(Value >> 32), 32);
}

/** 读取完整 uint64（64 位，固定宽度）。与 WriteUint64 配对。 */
/**
 * Read a full uint64 to the provided FNetBitStreamReader
 */
inline uint64 ReadUint64(FNetBitStreamReader* Reader)
{
	uint64 Value = Reader->ReadBits(32);
	uint64 HighValue = Reader->ReadBits(32);
	Value |= HighValue << 32;

	return Value;
}

/** 写入完整 int64（当作 uint64 转发）。符号位在读取时自然保留。 */
/**
 * Write a full uint64 to the provided FNetBitStreamWriter
 */
inline void WriteInt64(FNetBitStreamWriter* Writer, int64 Value)
{
	WriteUint64(Writer, static_cast<uint64>(Value));
}

/** 读取完整 int64。 */
/**
 * Read a full uint64 to the provided FNetBitStreamReader
 */
inline int64 ReadInt64(FNetBitStreamReader* Reader)
{
	uint64 Value = ReadUint64(Reader);
	return static_cast<int64>(Value);
}

/**
 * Packed 系列整数编解码：根据实际值大小选择"最少字节数"后以固定宽度写入。
 *
 * 编码策略（以 WritePackedUint32 为例）：
 *   1. 计算表达 Value 需要的最少比特数 BitCountNeeded（用 BitTwiddling::GetBitsNeeded）；
 *   2. 向上取整到字节数 ByteCountNeeded（1..4）；
 *   3. 以 2 位（Uint32/Int32）/ 1 位（Uint16）/ 3 位（Uint64/Int64）前缀写入 "ByteCountNeeded - 1"；
 *   4. 以 ByteCountNeeded*8 位写入数值本身。
 * 解码时按前缀读出字节数再读固定位数的值；有符号版本通过异或/减法做符号扩展（sign-extend）。
 *
 * 带宽收益：当数值通常较小（如枚举值、小计数、小索引）时显著节省比特。
 *
 * 前缀位数由数据类型决定：
 *   - Uint16：1 位（2 种长度：1~2 字节）
 *   - Uint32/Int32：2 位（4 种长度：1~4 字节）
 *   - Uint64/Int64：3 位（8 种长度：1~8 字节）
 */
/** Write a uint64 using as few bytes as possible */
IRISCORE_API void WritePackedUint64(FNetBitStreamWriter* Writer, uint64 Value);

/** Read a uint64 that was written using WritePackedUint64 */
IRISCORE_API uint64 ReadPackedUint64(FNetBitStreamReader* Reader);

/** Write an int64 using as few bytes as possible */
IRISCORE_API void WritePackedInt64(FNetBitStreamWriter* Writer, int64 Value);

/** Read an int64 that was written using WritePackedInt64 */
IRISCORE_API int64 ReadPackedInt64(FNetBitStreamReader* Reader);

/** Write a uint32 using as few bytes as possible */
IRISCORE_API void WritePackedUint32(FNetBitStreamWriter* Writer, uint32 Value);

/** Read a uint32 that was written using WritePackedUint32 */
IRISCORE_API uint32 ReadPackedUint32(FNetBitStreamReader* Reader);

/** Write an int32 using as few bytes as possible */
IRISCORE_API void WritePackedInt32(FNetBitStreamWriter* Writer, int32 Value);

/** Read an int32 that was written using WritePackedInt32 */
IRISCORE_API int32 ReadPackedInt32(FNetBitStreamReader* Reader);

/** Write a uint16 using as few bits as possible */
IRISCORE_API void WritePackedUint16(FNetBitStreamWriter* Writer, uint16 Value);

/** Read a uint16 that was written using WritePackedUint16 */
IRISCORE_API uint16 ReadPackedUint16(FNetBitStreamReader* Reader);

/**
 * 字符串编解码（UTF-8 风格）。
 * 格式：
 *   bit 0：bIsEncoded（1 = 多字节 UTF-8 编码；0 = 纯 ANSI，每 char 只写 8 位）
 *   bits 1..16：Length（最多 65535，超出报错）
 *   之后：Length 字节的 UTF-8 字节流 或 Length 字节的 ANSI 字节流
 *
 * 内部使用 FStringNetSerializerUtils::TStringCodec<TCHAR>：
 *   - 非纯 ANSI 走编码路径，避免每个 TCHAR 写 16/32 位；
 *   - 纯 ANSI 走快路径，每 char 8 位，节省带宽。
 */
/** Read an UTF8-like encoded string into a FString. It must've been written by WriteString(). */
IRISCORE_API void ReadString(FNetBitStreamReader* Reader, FString& OutString);

/** Write an FString as a UTF8-like encoded stream. */
IRISCORE_API void WriteString(FNetBitStreamWriter* Writer, const FString& String);

/** Write an FStringView as a UTF8-like encoded stream. It can be read by ReadString. */
IRISCORE_API void WriteString(FNetBitStreamWriter* Writer, FStringView String);

/**
 * Vector / Rotator 高层便利函数：
 *  - 无默认值版本：走 FVectorNetSerializer / FRotatorNetSerializer 的 Quantize + Serialize（双精度 FVector）。
 *  - 带 DefaultValue + Epsilon 版本：若 Value 与 DefaultValue 相差小于 Epsilon，只写 1 位表示"使用默认值"。
 *
 * 实现上借用已注册的 Serializer（函数表），拷贝一小段量化状态到栈上，避免自行处理量化细节。
 */
/** Write full Vector */
IRISCORE_API void WriteVector(FNetBitStreamWriter* Writer, const FVector& Vector);

/** Read full Vector */
IRISCORE_API void ReadVector(FNetBitStreamReader* Reader, FVector& Vector);

/** Write vector using default value compression */
IRISCORE_API void WriteVector(FNetBitStreamWriter* Writer, const FVector& Vector, const FVector& DefaultValue, float Epsilon);

/** Read default value compressed vector */
IRISCORE_API void ReadVector(FNetBitStreamReader* Reader, FVector& OutVector, const FVector& DefaultValue);

/** Write full Rotator */
IRISCORE_API void WriteRotator(FNetBitStreamWriter* Writer, const FRotator& Vector);

/** Read full Rotator */
IRISCORE_API void ReadRotator(FNetBitStreamReader* Reader, FRotator& Vector);

/** 
 * Write rotator using default value compression
 * i.e. Only write the Vector if it differs from the provided DefaultValue, if the the diff is within the provided epsilon a single bit will be written
 * Note: The provided DefaultValue must match be the same on Server and Client;
*/
IRISCORE_API void WriteRotator(FNetBitStreamWriter* Writer, const FRotator& Rotator, const FRotator& DefaultValue, float Epsilon);

/** Read default value compressed rotator, See WriteRotator.*/
IRISCORE_API void ReadRotator(FNetBitStreamReader* Reader, FRotator& OutRotator, const FRotator& DefaultValue);

/**
 * SparseBitArray 序列化的"数据密度提示"：
 *  - None：默认，数组中多数位为 0；
 *  - ContainsMostlyOnes：数组中多数位为 1，序列化前先翻转。
 * 两端必须使用相同的 Hint。BitCount 和 Hint 本身**不**被写入，由调用方确保对齐。
 */
enum class ESparseBitArraySerializationHint : uint8
{
	None,
	ContainsMostlyOnes,
};
/** 
 * Write sparse BitArray which is expected to contain mostly 0`s, if the array contains mostly 1`s passing the hint ESparseBitArraySerializationHint::ContainsMostlyOnes can be provided to flip the data before writing 
 * Note: BitCount and hint is not Written so the write must be matched by a corresponding Read
 *
 * 编码算法概要（详见 NetBitStreamUtil.cpp::WriteSparseBitArray）：
 *  1. 按 32 位 word 切分，构建 "非零 word 掩码"（NonZeroWordMask）和 "全 1 word 掩码"（InvertedWordMask）；
 *  2. 先写掩码；全 1 的 word 不需要再写数据；
 *  3. 对每个非零且非全 1 的 word，用 WriteSparseUint32UsingIndices 编码：
 *      - 若置位数 ≤ 3，写"置位数量 + 每个位的 delta 索引"；
 *      - 否则回退到字节级 mask 方式（WriteSparseUint32UsingByteMask）。
 *  4. 超过 1024 位的大数组按 1024 位一段分批编码。
 */
IRISCORE_API void WriteSparseBitArray(FNetBitStreamWriter* Writer, const uint32* Data, uint32 BitCount, ESparseBitArraySerializationHint Hint = ESparseBitArraySerializationHint::None);

/** 
 * Read sparse BitArray which is expected to contain mostly 0`s, if the array contains mostly 1`s passing the hint ESparseBitArraySerializationHint::ContainsMostlyOnes can be provided to flip the read data
 */
IRISCORE_API void ReadSparseBitArray(FNetBitStreamReader* Reader,  uint32* OutData, uint32 BitCount, ESparseBitArraySerializationHint Hint = ESparseBitArraySerializationHint::None);

/** 
 * Write sparse BitArray which is expected to contain mostly 0`s, if the array contains mostly 1`s passing the hint ESparseBitArraySerializationHint::ContainsMostlyOnes can be provided to flip the data before writing 
 * Note: BitCount and hint is not Written so the write must be matched by a corresponding Read
 *
 * Delta 变种：将 (Data ^ OldData) 当作稀疏比特数组编码，只传输变化位。
 * 常用于 ChangeMask / DirtyMask 的增量同步。
 */
IRISCORE_API void WriteSparseBitArrayDelta(FNetBitStreamWriter* Writer, const uint32* Data, const uint32* OldData, uint32 BitCount);

/** 
 * Read sparse BitArray which is expected to contain mostly 0`s, if the array contains mostly 1`s passing the hint ESparseBitArraySerializationHint::ContainsMostlyOnes can be provided to flip the read data
 */
IRISCORE_API void ReadSparseBitArrayDelta(FNetBitStreamReader* Reader,  uint32* OutData, const uint32* OldData, uint32 BitCount);

/**
 * 写入 Sentinel（哨兵值 0xBAADDEADU 的低 BitCount 位）。
 * 用于调试/校验：在关键位置前后写入固定的"魔数"，读取端若不匹配则说明收发位流错位，触发 ensure。
 */
/**
 * Write sentinelBits from predefined sentinel value
 */
IRISCORE_API void WriteSentinelBits(FNetBitStreamWriter* Writer, uint32 BitCount = 32U);

/**
 * 读取并校验 Sentinel：若溢出或与预期值不符，返回 false 并 ensureAlwaysMsgf 报错。
 * @param ErrorString 失败时日志描述（用于定位是哪一处 Sentinel）。
 */
/**
 * Read and verify sentinelBits against predefined sentinel value, will return false if we have a bitstream overflow or the read sentinel bits mismatch
 */

IRISCORE_API bool ReadAndVerifySentinelBits(FNetBitStreamReader* Reader, const TCHAR* ErrorString = TEXT("Sentinel"), uint32 BitCount = 32U);

/** 
* Write bytes
* Note: The purpose of this method is for cases when we cannot guarantee that the buffer is word aligned. If buffer is known to be word aligned, it is faster to use NetBitStreamReader::WriteBitStream directly.
*       Does not write the size.
*
* 按字节写入：当源指针不一定 4 字节对齐时的安全通路。
* 实现上先手动一字节一字节写直到对齐、再走 WriteBitStream 快路径、最后尾部零散再一字节一字节写。
* 不写入长度，需要调用方自行约定。
*/
IRISCORE_API void WriteBytes(FNetBitStreamWriter* Writer, const uint8* Src, uint32 BytesToWrite);

/** 
 * Read bytes into preallocated buffer
 * Note: The purpose of this method is for cases when we cannot guarantee that the buffer is word aligned. If buffer is known to be word aligned, it is faster to use NetBitStreamReader::ReadBitStream directly.
 *
 * 按字节读取：与 WriteBytes 对偶。目标 Destination 由调用方预分配；不读长度。
 */
IRISCORE_API void ReadBytes(FNetBitStreamReader* Reader, uint8* Destination, uint32 BytesToRead);

/**
 * 条件量化 Vector：
 *  - 先写 1 位 IsNotDefault：若接近 DefaultValue 只写这 1 位；
 *  - 否则再写 1 位 bQuantize：决定使用 FVectorNetQuantize10NetSerializer（量化）还是 FVectorNetSerializer（全精度）；
 *  - 然后写对应 Serializer 的数据。
 * 两端 DefaultValue 必须一致；bQuantize 是发送端策略，接收端读取时自然得知。
 */
/** Send 1 bit if the vector equals the DefaultValue, otherwise write a compressed vector using Quantize10 */
IRISCORE_API void WriteConditionallyQuantizedVector(FNetBitStreamWriter* Writer, const FVector& Vector, const FVector& DefaultValue, bool bQuantize);

/** Read a vector serialized using WriteConditionallyQuantizedVector. DefaultValue must be identical in both Read and Write calls. */
IRISCORE_API void ReadConditionallyQuantizedVector(FNetBitStreamReader* Reader, FVector& OutVector, const FVector& DefaultValue);

} // end namespace UE::Net
