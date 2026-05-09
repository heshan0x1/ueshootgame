// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/Serialization/NetBitStreamUtil.h"

#include "Iris/Core/BitTwiddling.h"
#include "Iris/Core/IrisLog.h"

#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/StringNetSerializerUtils.h"

#include "Math/Vector.h"

#include "Net/Core/NetBitArray.h"

namespace UE::Net
{

/**
 * WritePackedUint64：前缀 3 位表示字节数(1~8)，之后以 ByteCountNeeded*8 位写入数值。
 *  - GetBitsNeeded(Value | 1U) 强制把 0 也算作需要 1 位，避免 ByteCountNeeded 出现 0。
 *  - 数值超过 32 位时分两次 WriteBits（WriteBits 每次最多 32 位）。
 */
void WritePackedUint64(FNetBitStreamWriter* Writer, uint64 Value)
{
	// As we represent the number of bytes to write with three bits we want bits needed to be >= 1 such that the number of bytes ends up in the range [1, 8].
	const uint32 BitCountNeeded = GetBitsNeeded(Value | 1U);
	const uint32 ByteCountNeeded = (BitCountNeeded + 7U) / 8U;
	const uint32 BitCountToWrite = ByteCountNeeded * 8U;

	Writer->WriteBits(ByteCountNeeded - 1U, 3U);
	if (BitCountToWrite <= 32U)
	{
		Writer->WriteBits(Value & 0xFFFFFFFFU, BitCountToWrite);
	}
	else
	{
		Writer->WriteBits(Value & 0xFFFFFFFFU, 32U);
		Writer->WriteBits(static_cast<uint32>(Value >> 32U), BitCountToWrite - 32U);
	}
}

/** ReadPackedUint64：读 3 位前缀 + ByteCount*8 位数据，重建 uint64。 */
uint64 ReadPackedUint64(FNetBitStreamReader* Reader)
{
	const uint32 ByteCountToRead = Reader->ReadBits(3U) + 1U;
	const uint32 BitCountToRead = ByteCountToRead * 8U;

	if (BitCountToRead <= 32)
	{
		const uint64 Value = Reader->ReadBits(BitCountToRead);
		return Value;
	}
	else
	{
		uint64 Value = Reader->ReadBits(32U);
		Value |= (static_cast<uint64>(Reader->ReadBits(BitCountToRead - 32U)) << 32U);
		return Value;
	}
}

/**
 * WritePackedInt64：与 Uint 版本相同的前缀编码，但值区间含负数——
 * GetBitsNeeded(Value) 对有符号数会返回"最高位为符号位"所需总位数，故无须再 |1U。
 * 注意 32 位边界时先 static_cast 到 uint64 避免符号扩展覆盖高位。
 */
void WritePackedInt64(FNetBitStreamWriter* Writer, int64 Value)
{
	const uint32 BitCountNeeded = GetBitsNeeded(Value);
	const uint32 ByteCountNeeded = (BitCountNeeded + 7U) / 8U;
	const uint32 BitCountToWrite = ByteCountNeeded * 8U;

	Writer->WriteBits(ByteCountNeeded - 1U, 3U);
	if (BitCountToWrite <= 32U)
	{
		Writer->WriteBits(static_cast<uint64>(Value) & 0xFFFFFFFFU, BitCountToWrite);
	}
	else
	{
		const uint64 UnsignedValue = static_cast<uint64>(Value);
		Writer->WriteBits(UnsignedValue & 0xFFFFFFFFU, 32U);
		Writer->WriteBits(static_cast<uint32>(UnsignedValue >> 32U), BitCountToWrite - 32U);
	}
}

/**
 * ReadPackedInt64：
 *  - 先读前缀和无符号值；
 *  - 末尾用经典的"异或-减去高位 mask"做符号扩展：(U ^ SignMask) - SignMask。
 *    SignMask = 1 << (BitCountToRead - 1)，当第 BitCountToRead-1 位为 1 时相当于扣除 2^BitCount。
 */
int64 ReadPackedInt64(FNetBitStreamReader* Reader)
{
	const uint32 ByteCountToRead = Reader->ReadBits(3U) + 1U;
	const uint32 BitCountToRead = ByteCountToRead * 8U;

	uint64 UnsignedValue;
	if (BitCountToRead <= 32U)
	{
		UnsignedValue = Reader->ReadBits(BitCountToRead);
	}
	else
	{
		UnsignedValue = Reader->ReadBits(32U);
		UnsignedValue |= (static_cast<uint64>(Reader->ReadBits(BitCountToRead - 32U)) << 32U);
	}

	// Sign-extend the value
	const uint64 Mask = 1ULL << (BitCountToRead - 1U);
	UnsignedValue = (UnsignedValue ^ Mask) - Mask;
	const int64 Value = static_cast<int64>(UnsignedValue);
	return Value;
}

/** WritePackedUint32：2 位前缀 + 1~4 字节值。思路与 Uint64 版一致。 */
void WritePackedUint32(FNetBitStreamWriter* Writer, uint32 Value)
{
	// As we represent the number of bytes to write with two bits we want bits needed to be >= 1
	const uint32 BitCountNeeded = GetBitsNeeded(Value | 1U);
	const uint32 ByteCountNeeded = (BitCountNeeded + 7U) / 8U;
	const uint32 BitCountToWrite = ByteCountNeeded * 8U;

	Writer->WriteBits(ByteCountNeeded - 1U, 2U);
	Writer->WriteBits(Value, BitCountToWrite);
}

/** ReadPackedUint32：2 位前缀 + 1~4 字节值。 */
uint32 ReadPackedUint32(FNetBitStreamReader* Reader)
{
	const uint32 ByteCountToRead = Reader->ReadBits(2U) + 1U;
	const uint32 BitCountToRead = ByteCountToRead * 8U;

	const uint32 Value = Reader->ReadBits(BitCountToRead);
	return Value;
}

/** WritePackedInt32：与 Uint32 同，但无须 |1U（有符号 GetBitsNeeded 已考虑符号位）。 */
void WritePackedInt32(FNetBitStreamWriter* Writer, int32 Value)
{
	const uint32 BitCountNeeded = GetBitsNeeded(Value);
	const uint32 ByteCountNeeded = (BitCountNeeded + 7U)/8U;
	const uint32 BitCountToWrite = ByteCountNeeded*8U;
	
	Writer->WriteBits(ByteCountNeeded - 1U, 2U);
	Writer->WriteBits(Value, BitCountToWrite);
}

/** ReadPackedInt32：读前缀 + 无符号值，末尾用 (X ^ SignMask) - SignMask 做 32 位符号扩展。 */
int32 ReadPackedInt32(FNetBitStreamReader* Reader)
{
	const uint32 ByteCountToRead = Reader->ReadBits(2U) + 1U;
	const uint32 BitCountToRead = ByteCountToRead*8U;
	
	uint32 UnsignedValue = Reader->ReadBits(BitCountToRead);
	// Sign-extend the value
	const uint32 Mask = 1U << (BitCountToRead - 1U);
	UnsignedValue = (UnsignedValue ^ Mask) - Mask;
	const int32 Value = static_cast<int32>(UnsignedValue);
	return Value;
}

/** WritePackedUint16：1 位前缀（0=1 字节，1=2 字节）+ 数值。 */
void WritePackedUint16(FNetBitStreamWriter* Writer, uint16 Value)
{
	// As we represent the number of bytes to write with one bit we want bits needed to be >= 1
	const uint32 BitCountNeeded = GetBitsNeeded(Value | 1U);
	const uint32 ByteCountNeeded = (BitCountNeeded + 7U) / 8U;
	const uint32 BitCountToWrite = ByteCountNeeded * 8U;

	Writer->WriteBits(ByteCountNeeded - 1U, 1U);
	Writer->WriteBits(Value, BitCountToWrite);
}

/** ReadPackedUint16：1 位前缀 + 1/2 字节值。 */
uint16 ReadPackedUint16(FNetBitStreamReader* Reader)
{
	const uint32 ByteCountToRead = Reader->ReadBits(1U) + 1U;
	const uint32 BitCountToRead = ByteCountToRead * 8U;

	const uint16 Value = static_cast<uint16>(Reader->ReadBits(BitCountToRead));
	return Value;
}

/**
 * WriteString：字符串序列化。
 * 编码格式：
 *   bit 0（bIsEncoded）：
 *     1 → 内容含非 ANSI 字符，使用 UTF-8 风格编码；
 *     0 → 纯 ANSI，直接按 8 位/字符写。
 *   bits 1..16（Length）：编码后字节数或 ANSI 字符数，最多 65535。
 *   之后：实际字节流（通过 WriteBitStream 32 位对齐拷贝）。
 *
 * 长度超限直接写入 false + 0 长度并记录 Error；调用方需要检查 Writer->IsOverflown。
 */
void WriteString(FNetBitStreamWriter* Writer, FStringView StringView)
{
	using Codec = Private::FStringNetSerializerUtils::TStringCodec<TCHAR>;
	constexpr uint32 SizeAlignment = 4;                          // 编码缓冲大小按 4 字节对齐，便于按 word 拷贝
	
	const uint32 StringViewLength = StringView.Len();
	if (StringViewLength > 65535)
	{
		UE_LOG(LogSerialization, Error, TEXT("String to write is too long."));
		Writer->WriteBool(false);
		Writer->WriteBits(0, 16);
		return;
	}

	constexpr uint32 StackAllocatedBufferSize = 1024;
	TArray<uint8, TInlineAllocator<StackAllocatedBufferSize>> EncodingBuffer;
	const bool bIsEncoded = !FCString::IsPureAnsi(StringView.GetData(), StringView.Len());
	if (Writer->WriteBool(bIsEncoded))
	{
		if (Writer->IsOverflown())
		{
			return;
		}

		const uint32 SafeEncodedLength = Codec::GetSafeEncodedBufferLength(StringViewLength);
		EncodingBuffer.SetNumUninitialized(Align(SafeEncodedLength, SizeAlignment));

		uint32 OutEncodedLength = 0;
		const bool bEncodingSuccess = Codec::Encode(EncodingBuffer.GetData(), EncodingBuffer.Num(), StringView.GetData(), StringView.Len(), OutEncodedLength);

		if (!bEncodingSuccess || OutEncodedLength > 65535U)
		{
			UE_LOG(LogSerialization, Error, TEXT("String to write is invalid or too long."));
			Writer->WriteBits(0, 16);
			return;
		}

		Writer->WriteBits(OutEncodedLength, 16);
		Writer->WriteBitStream(reinterpret_cast<uint32*>(EncodingBuffer.GetData()), 0U, OutEncodedLength*8U);
	}
	else
	{
		// 纯 ANSI 快路径：逐字符压为 uint8（TCHAR & 0xFF）后走 WriteBitStream。
		Writer->WriteBits(StringViewLength, 16);
		if (Writer->IsOverflown())
		{
			return;
		}

		if (StringViewLength > 0)
		{
			EncodingBuffer.SetNumUninitialized(Align(StringViewLength, SizeAlignment));
			uint8* EncodingBufferData = EncodingBuffer.GetData();
			{
				uint32 CharIndex = 0;
				for (TCHAR Char : StringView)
				{
					EncodingBuffer[CharIndex++] = Char & 0xFF;
				}
			}

			Writer->WriteBitStream(reinterpret_cast<uint32*>(EncodingBufferData), 0, StringViewLength*8U);
		}
	}
}

/** FString 重载：转发到 FStringView 版本。 */
void WriteString(FNetBitStreamWriter* Writer, const FString& String)
{
	return WriteString(Writer, FStringView(String));
}

/**
 * ReadString：与 WriteString 对偶。
 *  - 先读 bIsEncoded 和 Length；
 *  - 编码路径：读字节流 → 验证 UTF-8 合法性 → Codec::Decode 到 FString；非法编码 → DoOverflow 标记错误；
 *  - ANSI 路径：读字节流 → 直接用 FString::ConstructFromPtrSize 构造。
 *  - 所有错误路径（解码失败/过长）都会 log Error 并可能触发 DoOverflow。
 */
void ReadString(FNetBitStreamReader* Reader, FString& OutString)
{
	using Codec = Private::FStringNetSerializerUtils::TStringCodec<TCHAR>;
	constexpr uint32 SizeAlignment = 4;
	constexpr uint32 StackAllocatedBufferSize = 1024;

	const bool bIsEncoded = Reader->ReadBool();
	const uint32 Length = Reader->ReadBits(16);

	if (Reader->IsOverflown())
	{
		return;
	}

	if (bIsEncoded)
	{
		TArray<uint8, TInlineAllocator<StackAllocatedBufferSize>> EncodedBuffer;
		EncodedBuffer.SetNumUninitialized(Align(Length + 1, SizeAlignment));

		// Read data and null-terminate encoded string.
		Reader->ReadBitStream(reinterpret_cast<uint32*>(EncodedBuffer.GetData()), Length*8U);
		EncodedBuffer[Length] = 0;

		if (Reader->IsOverflown())
		{
			return;
		}

		if (!Codec::IsValidEncoding(EncodedBuffer.GetData(), Length))
		{
			UE_LOG(LogSerialization, Error, TEXT("Received invalid encoded string."));
			Reader->DoOverflow();
			return;
		}

		const uint32 SafeDestLength = Codec::GetSafeDecodedBufferLength(Length + 1);
		OutString.GetCharArray().Reserve(SafeDestLength);

		uint32 ConvertedLength = 0;
		if (!Codec::Decode(OutString.GetCharArray().GetData(), SafeDestLength, EncodedBuffer.GetData(), Length + 1, ConvertedLength))
		{
			UE_LOG(LogSerialization, Error, TEXT("Received invalid encoded string."));
			Reader->DoOverflow();
			return;
		}

		OutString.GetCharArray().SetNumUninitialized(ConvertedLength, EAllowShrinking::No);
	}
	else
	{
		TArray<char, TInlineAllocator<StackAllocatedBufferSize>> CharBuffer;
		CharBuffer.SetNumUninitialized(Align(Length + 1, SizeAlignment));

		// Read ANSI string.
		Reader->ReadBitStream(reinterpret_cast<uint32*>(CharBuffer.GetData()), Length*8U);
		// Null-terminate string for easier debugging.
		CharBuffer[Length] = '\0';
		if (Reader->IsOverflown())
		{
			UE_LOG(LogSerialization, Error, TEXT("Received invalid ANSI string."));
			return;
		}

		// There are two FString constructors with similar signature. The one with known length of the string passes the count first.
		OutString = FString::ConstructFromPtrSize(CharBuffer.GetData(), static_cast<int32>(Length));
	}
}

/**
 * WriteVector：借助已注册的 FVectorNetSerializer 来序列化。
 * 流程：栈上分配一小段 QuantizedState（≥ Serializer.QuantizedTypeSize）→ 调 Quantize → 调 Serialize。
 * 避免自行处理双精度拆分、有效数字、量化的细节；也使未来修改 FVectorNetSerializer 可以自动生效。
 */
void WriteVector(FNetBitStreamWriter* Writer, const FVector& Vector)
{
	FNetSerializationContext Context(Writer);

	const FNetSerializer& Serializer = UE_NET_GET_SERIALIZER(FVectorNetSerializer);

	alignas(16) uint8 QuantizedState[32] = {};
	checkSlow(sizeof(QuantizedState) >= Serializer.QuantizedTypeSize);

	FNetQuantizeArgs QuantizeArgs;
	QuantizeArgs.Version = Serializer.Version;
	QuantizeArgs.Source = NetSerializerValuePointer(&Vector);
	QuantizeArgs.Target = NetSerializerValuePointer(&QuantizedState[0]);
	QuantizeArgs.NetSerializerConfig = NetSerializerConfigParam(Serializer.DefaultConfig);
	Serializer.Quantize(Context, QuantizeArgs);

	FNetSerializeArgs SerializeArgs;
	SerializeArgs.Version = Serializer.Version;
	SerializeArgs.Source = QuantizeArgs.Target;
	SerializeArgs.NetSerializerConfig = NetSerializerConfigParam(Serializer.DefaultConfig);
	Serializer.Serialize(Context, SerializeArgs);
}

/** ReadVector：对称地 Deserialize + Dequantize 到外部 FVector。 */
void ReadVector(FNetBitStreamReader* Reader, FVector& Vector)
{
	FNetSerializationContext Context(Reader);

	const FNetSerializer& Serializer = UE_NET_GET_SERIALIZER(FVectorNetSerializer);

	alignas(16) uint8 QuantizedState[32] = {};
	checkSlow(sizeof(QuantizedState) >= Serializer.QuantizedTypeSize);

	FNetDeserializeArgs DeserializeArgs;
	DeserializeArgs.Version = Serializer.Version;
	DeserializeArgs.Target = NetSerializerValuePointer(&QuantizedState[0]);
	DeserializeArgs.NetSerializerConfig = NetSerializerConfigParam(Serializer.DefaultConfig);
	Serializer.Deserialize(Context, DeserializeArgs);

	FNetDequantizeArgs DequantizeArgs;
	DequantizeArgs.Version = Serializer.Version;
	DequantizeArgs.Source = DeserializeArgs.Target;
	DequantizeArgs.Target = NetSerializerValuePointer(&Vector);
	DequantizeArgs.NetSerializerConfig = NetSerializerConfigParam(Serializer.DefaultConfig);
	Serializer.Dequantize(Context, DequantizeArgs);
}

/** WriteVector（带默认值压缩）：接近默认值时只写 1 位；否则写 1 位 + 完整 Vector。 */
void WriteVector(FNetBitStreamWriter* Writer, const FVector& Vector, const FVector& DefaultValue, float Epsilon)
{
	FNetSerializationContext Context(Writer);

	if (Writer->WriteBool(!Vector.Equals(DefaultValue, Epsilon)))
	{
		// Write vector
		WriteVector(Writer, Vector);
	}
}

/** ReadVector（带默认值压缩）：读 1 位，为 1 则读完整 Vector，否则直接置 DefaultValue。 */
void ReadVector(FNetBitStreamReader* Reader, FVector& OutVector, const FVector& DefaultValue)
{
	if (Reader->ReadBool())
	{
		ReadVector(Reader, OutVector);
	}
	else
	{
		OutVector = DefaultValue;
	}
}

/** WriteRotator：与 WriteVector 同思路，改用 FRotatorNetSerializer。 */
void WriteRotator(FNetBitStreamWriter* Writer, const FRotator& Rotator)
{
	FNetSerializationContext Context(Writer);

	const FNetSerializer& Serializer = UE_NET_GET_SERIALIZER(FRotatorNetSerializer);

	alignas(16) uint8 QuantizedState[16] = {};
	checkSlow(sizeof(QuantizedState) >= Serializer.QuantizedTypeSize);

	FNetQuantizeArgs QuantizeArgs;
	QuantizeArgs.Version = Serializer.Version;
	QuantizeArgs.Source = NetSerializerValuePointer(&Rotator);
	QuantizeArgs.Target = NetSerializerValuePointer(&QuantizedState[0]);
	QuantizeArgs.NetSerializerConfig = NetSerializerConfigParam(Serializer.DefaultConfig);
	Serializer.Quantize(Context, QuantizeArgs);

	FNetSerializeArgs SerializeArgs;
	SerializeArgs.Version = Serializer.Version;
	SerializeArgs.Source = QuantizeArgs.Target;
	SerializeArgs.NetSerializerConfig = NetSerializerConfigParam(Serializer.DefaultConfig);
	Serializer.Serialize(Context, SerializeArgs);
}

/** ReadRotator：对称 Deserialize + Dequantize。 */
void ReadRotator(FNetBitStreamReader* Reader, FRotator& Rotator)
{
	FNetSerializationContext Context(Reader);

	const FNetSerializer& Serializer = UE_NET_GET_SERIALIZER(FRotatorNetSerializer);

	alignas(16) uint8 QuantizedState[16] = {};
	checkSlow(sizeof(QuantizedState) >= Serializer.QuantizedTypeSize);

	FNetDeserializeArgs DeserializeArgs;
	DeserializeArgs.Version = Serializer.Version;
	DeserializeArgs.Target = NetSerializerValuePointer(&QuantizedState[0]);
	DeserializeArgs.NetSerializerConfig = NetSerializerConfigParam(Serializer.DefaultConfig);
	Serializer.Deserialize(Context, DeserializeArgs);

	FNetDequantizeArgs DequantizeArgs;
	DequantizeArgs.Version = Serializer.Version;
	DequantizeArgs.Source = DeserializeArgs.Target;
	DequantizeArgs.Target = NetSerializerValuePointer(&Rotator);
	DequantizeArgs.NetSerializerConfig = NetSerializerConfigParam(Serializer.DefaultConfig);
	Serializer.Dequantize(Context, DequantizeArgs);
}

/** WriteRotator（带默认值压缩）：与 WriteVector 同策略。 */
void WriteRotator(FNetBitStreamWriter* Writer, const FRotator& Rotator, const FRotator& DefaultValue, float Epsilon)
{
	FNetSerializationContext Context(Writer);

	if (Writer->WriteBool(!Rotator.Equals(DefaultValue, Epsilon)))
	{
		// Write vector
		WriteRotator(Writer, Rotator);
	}
}

/** ReadRotator（带默认值压缩）。 */
void ReadRotator(FNetBitStreamReader* Reader, FRotator& OutRotator, const FRotator& DefaultValue)
{
	if (Reader->ReadBool())
	{
		ReadRotator(Reader, OutRotator);
	}
	else
	{
		OutRotator = DefaultValue;
	}
}

namespace Private
{

/** 一次 SparseBitArray 编码分段上限：1024 位，对应 32 个 word。超出则按段分批处理。 */
constexpr uint32 SerializeSparseArrayMaxBitCount = 1024U;
constexpr uint32 SerializeSparseArrayMaxWordCount = SerializeSparseArrayMaxBitCount / 32U;

/** UsingIndices 编码路径的"置位数量"前缀上限——最多用索引表达 3 个置位，多于此则回退到 ByteMask。 */
const uint32 SparseUint32UsingIndices_MaxEncodedIndexBits = 3U;
/** 用于编码"置位数量"的前缀位宽（GetBitsNeeded(3) = 2）。 */
const uint32 SparseUint32UsingIndices_EncodedIndexBitsHeaderSize = GetBitsNeeded(SparseUint32UsingIndices_MaxEncodedIndexBits);

// Note: This functions expects any stray bits to be filtered out by the caller
// if it is exposed directly, masking of stray bits needs to be added. 
// i.e. something like
//if (BitCount < 32U)
//{
//	Value &= ~uint32(0) >> (uint32(-int32(BitCount)) & (31U));
//}
/**
 * WriteSparseUint32UsingByteMask：字节级稀疏编码。
 * 格式：
 *   - NumMaskBits 位字节掩码：每位代表对应字节是否非零；
 *   - 之后顺序写入所有非零字节（每个 8 位，末字节可能不满 8 位）。
 * 适合有若干非零字节但不密集到需要全 32 位的中等稀疏情况。
 */
static void WriteSparseUint32UsingByteMask(FNetBitStreamWriter* Writer, uint32 Value, uint32 BitCount = 32U)
{
	checkSlow((Value & ~(~uint32(0) >> (uint32(-int32(BitCount)) & (31U)))) == 0U);

	uint32 Mask = 0U;
	uint32 ValueToWrite = 0U;
	uint32 ValueBitsToWrite = 0U;
	int32 RemainingBits = BitCount;
	uint32 CurrentMaskBit = 1U;
	
	while (RemainingBits > 0)
	{
		if (const uint32 CurrentByte = Value & 0xffU)
		{
			Mask |= CurrentMaskBit;
			ValueToWrite |= CurrentByte << ValueBitsToWrite;
			ValueBitsToWrite += FMath::Min(8U, (uint32)RemainingBits);
		}
		RemainingBits -= 8;
		Value >>= 8U;
		CurrentMaskBit += CurrentMaskBit;
	}

	// Write mask
	const uint32 NumMaskBits = (BitCount + 7U) / 8U;
	Writer->WriteBits(Mask, NumMaskBits);
	// Write data
	Writer->WriteBits(ValueToWrite, ValueBitsToWrite);
}

/** ReadSparseUint32UsingByteMask：与上面的 Write 对偶，复原字节级稀疏 uint32。 */
static uint32 ReadSparseUint32UsingByteMask(FNetBitStreamReader* Reader, uint32 BitCount = 32U)
{
	const uint32 NumMaskBits = (BitCount + 7U) / 8U;
	const uint32 HighestBitMask = 1U << (NumMaskBits - 1U);

	// Read Mask bits
	const uint32 Mask = Reader->ReadBits(NumMaskBits);
	
	// Calculate bitcount for value based on bits set in mask, if the highest bit is set we need to subtract bits if the last byte is not a full byte
	// 掩码置位字节数 * 8，若最高位字节不是完整 8 位（BitCount % 8 != 0），需扣除相应差值。
	const uint32 ValueBitsToRead = FMath::CountBits(Mask) * 8U - ((Mask & HighestBitMask) ? 8U - BitCount & 7U : 0U);

	// Read Value bits
	uint32 ValueBits = Reader->ReadBits(ValueBitsToRead);

	uint32 Value = 0U;
	uint32 CurrentMaskBit = 1U;
	uint32 CurrentByteOffset = 0U;
	uint32 CurrentValueByteMask = 0xffU;
	
	for (uint32 It = 0U; It < NumMaskBits; ++It)
	{
		if (Mask & CurrentMaskBit)
		{
			Value |= (ValueBits & 0xffU) << CurrentByteOffset;
			ValueBits >>= 8U;			
		}
		
		CurrentByteOffset += 8U;
		CurrentMaskBit += CurrentMaskBit;
	}

	return Value;
}

// Note: This functions expects any stray bits to be filtered out by the caller
// if it is exposed directly, masking of stray bits needs to be added. 
// i.e. something like
//if (BitCount < 32U)
//{
//	Value &= ~uint32(0) >> (uint32(-int32(BitCount)) & (31U));
//}
/**
 * WriteSparseUint32UsingIndices：位索引稀疏编码（极稀疏情况最优）。
 * 策略：
 *  - 若置位数量 ∈ [1, 3]：写"数量"（2 位）+ 每个置位的"delta 索引"（差分 + 动态位宽）；
 *  - 否则写"数量 = 0"作为 fallback 标记，回退到 WriteSparseUint32UsingByteMask。
 * Delta 索引编码：
 *  - 第 k 个 set bit 与前一个 set bit 的距离，位宽为 GetBitsNeeded(MaxIndexDeltaBits - LastWrittenBitIndex)；
 *  - 这样既充分利用"前面已经占过的范围越大、剩余范围越小、所需位数越少"的特点。
 */
static void WriteSparseUint32UsingIndices(FNetBitStreamWriter* Writer, uint32 Value, uint32 BitCount = 32U)
{
	// Note: We expect at least 1 bit to be set so zero bits will take an unoptimal path
	checkSlow(BitCount >= 1U);
	checkSlow((Value & ~(~uint32(0) >> (uint32(-int32(BitCount)) & (31U)))) == 0U);

	const uint32 MaxIndexDeltaBits = BitCount - 1U;
	const uint32 NumBitsSet = FMath::CountBits(Value);

	// If the number of dirty bits is lower SparseUint32UsingIndices_MaxEncodedIndexBits we encode the word using indices for the set bits
	if (NumBitsSet > 0U && NumBitsSet <= SparseUint32UsingIndices_MaxEncodedIndexBits)
	{
		Writer->WriteBits(NumBitsSet, SparseUint32UsingIndices_EncodedIndexBitsHeaderSize);
		uint32 LastWrittenBitIndex = 0U;
		while ((Value != 0))
		{
			const uint32 LeastSignificantBit = Value & uint32(-int32(Value));
			Value ^= LeastSignificantBit;

			// We can deltacompress the index against the previous one to save some bits
			const uint32 RequiredBitCount = GetBitsNeeded(MaxIndexDeltaBits - LastWrittenBitIndex);
			const uint32 BitIndexDelta = FPlatformMath::CountTrailingZeros(LeastSignificantBit) - LastWrittenBitIndex;
			LastWrittenBitIndex += BitIndexDelta;

			Writer->WriteBits(BitIndexDelta, RequiredBitCount);
		}		
	}
	else
	{
		// Fallback on masked approach if too many bits are set
		Writer->WriteBits(0U, SparseUint32UsingIndices_EncodedIndexBitsHeaderSize);
		WriteSparseUint32UsingByteMask(Writer, Value, BitCount);
	}
}

/** ReadSparseUint32UsingIndices：对偶读取。数量 == 0 时回退到 ByteMask 路径。 */
static uint32 ReadSparseUint32UsingIndices(FNetBitStreamReader* Reader, uint32 BitCount = 32U)
{
	const uint32 EncodedBitCount = Reader->ReadBits(SparseUint32UsingIndices_EncodedIndexBitsHeaderSize);
	if (EncodedBitCount > 0U)
	{
		const uint32 MaxDeltaBitCount = BitCount - 1U;

		uint32 Value = 0U;
		uint32 LastReadBitIndex = 0U;
		uint32 CurrentBitMask = 1U;

		for (uint32 It = 0U; It < EncodedBitCount; ++It)
		{
			const uint32 RequiredBitCount = GetBitsNeeded(MaxDeltaBitCount - LastReadBitIndex);
			const uint32 BitIndexDelta = Reader->ReadBits(RequiredBitCount);
			LastReadBitIndex += BitIndexDelta;
			CurrentBitMask <<= BitIndexDelta;

			Value |= CurrentBitMask;
		}

		return Value;
	}
	else
	{
		// Fallback on masked approach if too many bits are set
		return ReadSparseUint32UsingByteMask(Reader, BitCount);
	}
}

/**
 * 模板化的 SparseBitArray 写入器：
 *  - GetDataFunction：对每个 word 应用的变换函数（None hint → 恒等；ContainsMostlyOnes → ~Value，把"多 1"翻转成"多 0"再编码）。
 *  - WriteSparseUint32Function：单 word 的编码实现（通常是 WriteSparseUint32UsingIndices）。
 * 
 * 编码结构：
 *   1) WordCount 位 NonZeroWordMask：哪些 word 既非全 0 也非全 1；
 *   2) 1 位 HasInvertedWords：是否存在全 1 的 word；
 *   3) 若有 → WordCount 位 InvertedWordMask：哪些 word 是全 1（读取时直接填 ~0）；
 *   4) 依次编码 NonZeroWordMask 中置位的 word 内容。
 *
 * 这种三级表达（全 0 / 全 1 / 部分非零）能最大化地压缩大量 0 或多个连续 1 的 ChangeMask。
 */
template<typename GetDataFunc, typename WriteSparseUint32Func>
void WriteSparseBitArray(FNetBitStreamWriter* Writer, const uint32* Data, uint32 BitCount, GetDataFunc&& GetDataFunction, WriteSparseUint32Func&& WriteSparseUint32Function)
{
	ensure(BitCount <= SerializeSparseArrayMaxBitCount);

	using StorageWordType = FNetBitArrayBase::StorageWordType;
	const uint32 WordBitCount = FNetBitArrayBase::WordBitCount;
	const uint32 WordCount = FNetBitArrayView::CalculateRequiredWordCount(BitCount);
	const StorageWordType LastWordMask = ~StorageWordType(0) >> (uint32(-int32(BitCount)) & (WordBitCount - 1));
	const uint32 LastWordIt = WordCount - 1;

	// Build mask over words that are all zero or all ones.
	uint32 NonZeroWordMask = uint32(0);
	// Mask over words that are all ones.
	uint32 InvertedWordMask = uint32(0);
	{
		for (uint32 WordIt = 0, CurrentBitMask = 1U; WordIt < WordCount; ++WordIt, CurrentBitMask <<= 1U)
		{
			const StorageWordType CurrentWord = Data[WordIt] & ((WordIt == LastWordIt) ? LastWordMask : ~0U);
			// Set bits outside the valid range as ones when inverting the word to have the possibility of the zero word optimization.
			// 对最后一个 word 的无效位补 1，再翻转——若翻转后为 0 则说明有效位全 1，可进全 1 优化路径。
			const StorageWordType InvertedWord = ~(CurrentWord | (WordIt == LastWordIt ? ~LastWordMask : 0U));

			NonZeroWordMask |= (CurrentWord == 0) or (InvertedWord == 0) ? 0U : CurrentBitMask;
			InvertedWordMask |=  InvertedWord == 0 ? CurrentBitMask : 0U;
		}
	}

	// Write Mask
	Writer->WriteBits(NonZeroWordMask, WordCount);
	// Write all ones word mask.
	if (Writer->WriteBool(InvertedWordMask != 0))
	{
		Writer->WriteBits(InvertedWordMask, WordCount);
	}

	// Encode dirty words
	{
		// Full words
		uint32 CurrentMaskBit = 1U;
		uint32 WordIt = 0U;
		uint32 RemainingBits = BitCount;

		while (RemainingBits >= 32U)
		{
			if (NonZeroWordMask & CurrentMaskBit)
			{
				WriteSparseUint32Function(Writer, GetDataFunction(Data[WordIt]), 32U);
			}
			++WordIt;
			CurrentMaskBit <<= 1;
			RemainingBits -= 32U;
		}
		// Last word
		if (RemainingBits && (NonZeroWordMask & CurrentMaskBit))
		{
			const StorageWordType CurrentWord = GetDataFunction(Data[WordIt]) & LastWordMask;
			WriteSparseUint32Function(Writer, CurrentWord, RemainingBits);
		}
	}
}

/**
 * 模板化的 SparseBitArray 读取器（对偶 WriteSparseBitArray）：
 * 读 NonZeroWordMask + 可选 InvertedWordMask，再按标记选择：
 *  - Non-zero：调 ReadSparseUint32Function 解码；
 *  - Inverted：直接填 ~0（或 LastWordMask）；
 *  - 否则 0。
 */
template<typename GetDataFunc, typename ReadSparseUint32Func>
void ReadSparseBitArray(FNetBitStreamReader* Reader, uint32* OutData, uint32 BitCount, GetDataFunc&& GetDataFunction, ReadSparseUint32Func&& ReadSparseUint32Function)
{
	checkSlow(BitCount <= SerializeSparseArrayMaxBitCount);

	using StorageWordType = FNetBitArrayBase::StorageWordType;

	const uint32 WordBitCount = FNetBitArrayBase::WordBitCount;
	const uint32 WordCount = FNetBitArrayView::CalculateRequiredWordCount(BitCount);
	const StorageWordType LastWordMask = ~StorageWordType(0) >> (uint32(-int32(BitCount)) & (WordBitCount - 1U));
	const uint32 LastWordIt = WordCount - 1U;

	// Read mask
	uint32 NonZeroWordMask = Reader->ReadBits(WordCount);
	uint32 InvertedWordMask = uint32(0);
	if (Reader->ReadBool())
	{
		InvertedWordMask = Reader->ReadBits(WordCount);
	}

	// Read and decode dirty words
	uint32 CurrentMaskBit = 1U;
	uint32 WordIt = 0U;
	uint32 RemainingBits = BitCount;

	while (RemainingBits >= 32U)
	{
		StorageWordType ReadValue = 0U;
		if (NonZeroWordMask & CurrentMaskBit)
		{
			ReadValue = ReadSparseUint32Function(Reader, 32U);
			ReadValue = GetDataFunction(ReadValue);
		}
		else if (InvertedWordMask & CurrentMaskBit)
		{
			ReadValue = ~0U;
		}
		OutData[WordIt] = ReadValue;

		CurrentMaskBit += CurrentMaskBit;
		RemainingBits -= 32U;
		++WordIt;
	}

	// Last word, make sure we do not overwrite existing data
	// 最后一个 word：保留 OutData 里"无效位"区域，仅按 LastWordMask 覆盖有效位。
	if (RemainingBits > 0U)
	{
		StorageWordType ReadValue = 0U;
		if (NonZeroWordMask & CurrentMaskBit)
		{
			ReadValue = ReadSparseUint32Function(Reader, RemainingBits);
			ReadValue = GetDataFunction(ReadValue) & LastWordMask;
		}
		else if (InvertedWordMask & CurrentMaskBit)
		{
			ReadValue = LastWordMask;
		}
		OutData[WordIt] = (OutData[WordIt] & ~LastWordMask) | ReadValue;
	}
}

/**
 * WriteSparseBitArrayDelta：Delta 版本。
 * 思路：把 Data ^ OldData 当作稀疏比特数组 DeltaData 编码（仅"有变动"的位置上为 1）。
 * 不需要 InvertedWordMask——Delta 全 1 意味着整 word 翻转，通常不会发生。
 */
template<typename WriteSparseUint32Func>
void WriteSparseBitArrayDelta(FNetBitStreamWriter* Writer, const uint32* Data, const uint32* OldData, uint32 BitCount, WriteSparseUint32Func&& WriteSparseUint32Function)
{
	checkSlow(BitCount <= SerializeSparseArrayMaxBitCount);

	using StorageWordType = FNetBitArrayBase::StorageWordType;
	const uint32 WordBitCount = FNetBitArrayBase::WordBitCount;
	const uint32 WordCount = FNetBitArrayView::CalculateRequiredWordCount(BitCount);
	const StorageWordType LastWordMask = ~StorageWordType(0) >> (uint32(-int32(BitCount)) & (WordBitCount - 1));
	const uint32 LastWordIt = WordCount - 1;

	StorageWordType DeltaData[Private::SerializeSparseArrayMaxWordCount];

	// Build mask
	uint32 DirtyWordMask = uint32(0);
	{
		for (uint32 WordIt = 0, CurrentBitMask = 1U; WordIt < WordCount; ++WordIt, CurrentBitMask <<= 1)
		{
			const StorageWordType CurrentWord = (Data[WordIt] ^ OldData[WordIt]) & ((WordIt == LastWordIt) ? LastWordMask : ~0U);
			DirtyWordMask |= CurrentWord ? CurrentBitMask : 0U;
			DeltaData[WordIt] = CurrentWord;
		}
	}

	// Write Mask
	Writer->WriteBits(DirtyWordMask, WordCount);

	// Encode dirty words
	{
		// Full words
		uint32 CurrentMaskBit = 1U;
		uint32 WordIt = 0U;
		uint32 RemainingBits = BitCount;

		while (RemainingBits >= 32U)
		{
			if (DirtyWordMask & CurrentMaskBit)
			{
				WriteSparseUint32Function(Writer, DeltaData[WordIt], 32U);
			}
			++WordIt;
			CurrentMaskBit <<= 1;
			RemainingBits -= 32U;
		}
		// Last word
		if (RemainingBits && (DirtyWordMask & CurrentMaskBit))
		{
			const StorageWordType CurrentWord = DeltaData[WordIt] & LastWordMask;
			WriteSparseUint32Function(Writer, CurrentWord, RemainingBits);
		}
	}
}

/**
 * ReadSparseBitArrayDelta：读 DirtyWordMask + 对每个 dirty word 解码出 DeltaWord，
 * OutData[i] = OldData[i] ^ DeltaWord，重建新值。
 * 末 word 同样仅覆盖 LastWordMask 范围，保留无效位。
 */
template<typename ReadSparseUint32Func>
void ReadSparseBitArrayDelta(FNetBitStreamReader* Reader, uint32* OutData, const uint32* OldData, uint32 BitCount, ReadSparseUint32Func&& ReadSparseUint32Function)
{
	checkSlow(BitCount <= SerializeSparseArrayMaxBitCount);

	using StorageWordType = FNetBitArrayBase::StorageWordType;

	const uint32 WordBitCount = FNetBitArrayBase::WordBitCount;
	const uint32 WordCount = FNetBitArrayView::CalculateRequiredWordCount(BitCount);
	const StorageWordType LastWordMask = ~StorageWordType(0) >> (uint32(-int32(BitCount)) & (WordBitCount - 1U));
	const uint32 LastWordIt = WordCount - 1U;

	// Read mask
	uint32 DirtyWordMask = Reader->ReadBits(WordCount);

	// Read and decode dirty words
	uint32 CurrentMaskBit = 1U;
	uint32 WordIt = 0U;
	uint32 RemainingBits = BitCount;

	while (RemainingBits >= 32U)
	{
		const StorageWordType ReadValue = DirtyWordMask & CurrentMaskBit ? ReadSparseUint32Function(Reader, 32U) : 0U;
		OutData[WordIt] = OldData[WordIt] ^ ReadValue;

		CurrentMaskBit += CurrentMaskBit;
		RemainingBits -= 32U;
		++WordIt;
	}

	// Last word, make sure we do not overwrite existing data
	if (RemainingBits > 0U)
	{
		const StorageWordType ReadValue = DirtyWordMask & CurrentMaskBit ? ReadSparseUint32Function(Reader, RemainingBits) : 0U;
		OutData[WordIt] = (OutData[WordIt] & ~LastWordMask) | ((OldData[WordIt] ^ ReadValue) & LastWordMask);
	}
}

}

/**
 * 对外的 WriteSparseBitArray：按 1024 位分段循环调用内部模板实现。
 * Hint == ContainsMostlyOnes 时使用 lambda [](v) → ~v 对每个 word 取反（多 1 变多 0 再编码）。
 */
void WriteSparseBitArray(FNetBitStreamWriter* Writer, const uint32* Data, uint32 BitCount, ESparseBitArraySerializationHint Hint)
{
	if (Hint == ESparseBitArraySerializationHint::None)
	{
		auto GetDataFunc = [](const uint32 Value) { return Value; };
		// Support large bit arrays
		uint32 RemainingBits = BitCount;
		while (RemainingBits >= Private::SerializeSparseArrayMaxBitCount)
		{
			Private::WriteSparseBitArray(Writer, Data, Private::SerializeSparseArrayMaxBitCount, GetDataFunc, Private::WriteSparseUint32UsingIndices);
			RemainingBits -= Private::SerializeSparseArrayMaxBitCount;
			Data += Private::SerializeSparseArrayMaxWordCount;
		}
		if (RemainingBits)
		{
			Private::WriteSparseBitArray(Writer, Data, RemainingBits, GetDataFunc, Private::WriteSparseUint32UsingIndices);
		}
	}
	else
	{
		auto GetDataFunc = [](const uint32 Value) { return ~Value; };
		// Support large bit arrays
		uint32 RemainingBits = BitCount;
		while (RemainingBits >= Private::SerializeSparseArrayMaxBitCount)
		{
			Private::WriteSparseBitArray(Writer, Data, Private::SerializeSparseArrayMaxBitCount, GetDataFunc, Private::WriteSparseUint32UsingIndices);
			RemainingBits -= Private::SerializeSparseArrayMaxBitCount;
			Data += Private::SerializeSparseArrayMaxWordCount;
		}
		if (RemainingBits)
		{
			Private::WriteSparseBitArray(Writer, Data, RemainingBits, GetDataFunc, Private::WriteSparseUint32UsingIndices);
		}
	}
}

/** ReadSparseBitArray：与 WriteSparseBitArray 对偶，对 ContainsMostlyOnes 做逆向取反。 */
void ReadSparseBitArray(FNetBitStreamReader* Reader, uint32* OutData, uint32 BitCount, ESparseBitArraySerializationHint Hint)
{
	if (Hint == ESparseBitArraySerializationHint::None)
	{
		auto SetDataFunc = [](const uint32 Value) { return Value; };
		// Support large bit arrays
		uint32 RemainingBits = BitCount;
		while (RemainingBits >= Private::SerializeSparseArrayMaxBitCount)
		{
			Private::ReadSparseBitArray(Reader, OutData, Private::SerializeSparseArrayMaxBitCount, SetDataFunc, Private::ReadSparseUint32UsingIndices);
			RemainingBits -= Private::SerializeSparseArrayMaxBitCount;
			OutData += Private::SerializeSparseArrayMaxWordCount;
		}
		if (RemainingBits)
		{
			Private::ReadSparseBitArray(Reader, OutData, RemainingBits, SetDataFunc, Private::ReadSparseUint32UsingIndices);
		}
	}
	else
	{
		auto SetDataFunc = [](const uint32 Value) { return ~Value; };
		// Support large bit arrays
		uint32 RemainingBits = BitCount;
		while (RemainingBits >= Private::SerializeSparseArrayMaxBitCount)
		{
			Private::ReadSparseBitArray(Reader, OutData, Private::SerializeSparseArrayMaxBitCount, SetDataFunc, Private::ReadSparseUint32UsingIndices);
			RemainingBits -= Private::SerializeSparseArrayMaxBitCount;
			OutData += Private::SerializeSparseArrayMaxWordCount;
		}
		if (RemainingBits)
		{
			Private::ReadSparseBitArray(Reader, OutData, RemainingBits, SetDataFunc, Private::ReadSparseUint32UsingIndices);
		}
	}
}

/** WriteSparseBitArrayDelta：按 1024 位分段循环调用模板实现。 */
void WriteSparseBitArrayDelta(FNetBitStreamWriter* Writer, const uint32* Data, const uint32* OldData, uint32 BitCount)
{
	// Support large bit arrays
	uint32 RemainingBits = BitCount;
	while (RemainingBits >= Private::SerializeSparseArrayMaxBitCount)
	{
		Private::WriteSparseBitArrayDelta(Writer, Data, OldData, Private::SerializeSparseArrayMaxBitCount, Private::WriteSparseUint32UsingIndices);
		RemainingBits -= Private::SerializeSparseArrayMaxBitCount;
		Data += Private::SerializeSparseArrayMaxWordCount;
		OldData += Private::SerializeSparseArrayMaxWordCount;
	}
	if (RemainingBits)
	{
		Private::WriteSparseBitArrayDelta(Writer, Data, OldData, RemainingBits, Private::WriteSparseUint32UsingIndices);
	}
}

/** ReadSparseBitArrayDelta：对偶读取。 */
void ReadSparseBitArrayDelta(FNetBitStreamReader* Reader, uint32* OutData, const uint32* OldData, uint32 BitCount)
{
	// Support large bit arrays
	uint32 RemainingBits = BitCount;
	while (RemainingBits >= Private::SerializeSparseArrayMaxBitCount)
	{
		Private::ReadSparseBitArrayDelta(Reader, OutData, OldData, Private::SerializeSparseArrayMaxBitCount, Private::ReadSparseUint32UsingIndices);
		RemainingBits -= Private::SerializeSparseArrayMaxBitCount;
		OutData += Private::SerializeSparseArrayMaxWordCount;
		OldData += Private::SerializeSparseArrayMaxWordCount;
	}
	if (RemainingBits)
	{
		Private::ReadSparseBitArrayDelta(Reader, OutData, OldData, RemainingBits, Private::ReadSparseUint32UsingIndices);
	}
}

/** 哨兵值常量。选择"不好看的"0xBAADDEAD 便于在 hex dump 中一眼识别，且非零字节密度高，错位容易检测出。 */
const uint32 NetBitStreamSentinelValue = 0xBAADDEADU;

/** 写入哨兵值的低 BitCount 位。 */
void WriteSentinelBits(FNetBitStreamWriter* Writer, uint32 BitCount)
{
	Writer->WriteBits(NetBitStreamSentinelValue, BitCount);
}

/**
 * 读取并校验哨兵：对 NetBitStreamSentinelValue 按 BitCount 截取后比较。
 *  - 溢出 / 值不符 → ensureAlwaysMsgf 报错（ensureAlways 不可被屏蔽）。
 *  - ErrorString 作为日志标签，便于定位哪一处哨兵失败。
 */
bool ReadAndVerifySentinelBits(FNetBitStreamReader* Reader, const TCHAR* ErrorString, uint32 BitCount)
{
	const uint32 ReadValue = Reader->ReadBits(BitCount);
	const uint32 CompareMask = ~0U >> (32U - BitCount);
	const uint32 ExpectedValue = (NetBitStreamSentinelValue & CompareMask);
	return ensureAlwaysMsgf(!Reader->IsOverflown() &&  ExpectedValue == ReadValue, TEXT("ReadAndVerifySentinelBits %s failed OverFlow %u Got 0x%u != 0x%u"), ErrorString, Reader->IsOverflown() ? 1U : 0U, ReadValue, ExpectedValue);
}

/**
 * ReadBytes：按字节读取（源指针可能非对齐的场景）。
 * 算法：
 *  1. 修正目标 Dst 对齐（每次 8 位补到 4 字节边界）；
 *  2. 对齐后批量调用 ReadBitStream（32 位为单位）；
 *  3. 尾部残余字节再用 ReadBits(8) 逐一补齐。
 */
void ReadBytes(FNetBitStreamReader* Reader, uint8* Destination, uint32 BytesToRead)
{
	if (BytesToRead == 0)
	{
		return;
	}

	uint8* DstData = Destination;
	// Fix up dst alignment
	while (((uintptr_t)(DstData) & 3) && BytesToRead)
	{
		*(DstData++) = static_cast<uint8>(Reader->ReadBits(8U));
		--BytesToRead;
	}
	// Write full words
	const uint32 FullWordsToRead = BytesToRead / 4U;
	if (FullWordsToRead)
	{
		Reader->ReadBitStream(reinterpret_cast<uint32*>(DstData), FullWordsToRead * 32U);
		DstData += FullWordsToRead * 4U;
		BytesToRead -= FullWordsToRead * 4U;
	}
	while (BytesToRead)
	{
		*(DstData++) = static_cast<uint8>(Reader->ReadBits(8U));
		--BytesToRead;
	}
}

/** WriteBytes：与 ReadBytes 对偶，先修正源对齐，再按 word 批量写，尾部逐字节写。 */
void WriteBytes(FNetBitStreamWriter* Writer, const uint8* Src, uint32 BytesToWrite)
{
	if (BytesToWrite == 0)
	{
		return;
	}

	const uint8* SrcData = Src;
	// Fix up src alignment
	while (((uintptr_t)(SrcData) & 3) && BytesToWrite)
	{
		Writer->WriteBits(*(SrcData++), 8U);
		--BytesToWrite;
	}
	// Write full words
	const uint32 FullWordsToWrite = BytesToWrite / 4U;
	if (FullWordsToWrite)
	{
		Writer->WriteBitStream(reinterpret_cast<const uint32*>(SrcData), 0U, FullWordsToWrite * 32);
		SrcData += FullWordsToWrite * 4U;
		BytesToWrite -= FullWordsToWrite * 4U;
	}
	while (BytesToWrite)
	{
		Writer->WriteBits(*(SrcData++), 8U);
		--BytesToWrite;
	}
}

/**
 * WriteConditionallyQuantizedVector：条件量化 Vector 序列化（常见用法：游戏对象位置的廉价同步）。
 * 编码：
 *   bit 0 (IsNotDefault)：若接近 DefaultValue 只写此位；
 *   bit 1 (bQuantize)：决定用 FVectorNetQuantize10NetSerializer（每单位 0.1 精度）还是 FVectorNetSerializer（全精度）；
 *   之后：对应 Serializer 的输出。
 *
 * 量化模式的 Epsilon 选 0.01f（对应 0.1 精度留出冗余），全精度模式用 UE_KINDA_SMALL_NUMBER。
 */
void WriteConditionallyQuantizedVector(UE::Net::FNetBitStreamWriter* Writer, const FVector& Vector, const FVector& DefaultValue, bool bQuantize)
{
	using namespace UE::Net::Private;

	// We use 0.01f for comparing when using quantization, because we will only send a single point of precision anyway.
	// We could probably get away with 0.1f, but that may introduce edge cases for rounding.
	static constexpr float Epsilon_Quantized = 0.01f;
				
	// We use KINDA_SMALL_NUMBER for comparing when not using quantization, because that's the default for FVector::Equals.
	const float Epsilon = bQuantize ? Epsilon_Quantized : UE_KINDA_SMALL_NUMBER;
	
	if (Writer->WriteBool(!Vector.Equals(DefaultValue, Epsilon)))
	{
		Writer->WriteBool(bQuantize);

		const FNetSerializer& Serializer = bQuantize ? UE_NET_GET_SERIALIZER(FVectorNetQuantize10NetSerializer) : UE_NET_GET_SERIALIZER(FVectorNetSerializer);

		FNetSerializationContext Context(Writer);

		alignas(16) uint8 QuantizedState[32] = {};
		checkSlow(sizeof(QuantizedState) >= Serializer.QuantizedTypeSize);

		FNetQuantizeArgs QuantizeArgs;
		QuantizeArgs.Version = Serializer.Version;
		QuantizeArgs.Source = NetSerializerValuePointer(&Vector);
		QuantizeArgs.Target = NetSerializerValuePointer(&QuantizedState[0]);
		QuantizeArgs.NetSerializerConfig = NetSerializerConfigParam(Serializer.DefaultConfig);
		Serializer.Quantize(Context, QuantizeArgs);

		FNetSerializeArgs SerializeArgs;
		SerializeArgs.Version = Serializer.Version;
		SerializeArgs.Source = QuantizeArgs.Target;
		SerializeArgs.NetSerializerConfig = NetSerializerConfigParam(Serializer.DefaultConfig);
		Serializer.Serialize(Context, SerializeArgs);
	}
}

/** ReadConditionallyQuantizedVector：对偶读取。接收端从 bQuantize 位自行选择对应 Serializer。 */
void ReadConditionallyQuantizedVector(UE::Net::FNetBitStreamReader* Reader, FVector& OutVector, const FVector& DefaultValue)
{
	using namespace UE::Net::Private;

	if (Reader->ReadBool())
	{
		const bool bIsQuantized = Reader->ReadBool();

		const FNetSerializer& Serializer = bIsQuantized ? UE_NET_GET_SERIALIZER(FVectorNetQuantize10NetSerializer) : UE_NET_GET_SERIALIZER(FVectorNetSerializer);

		FNetSerializationContext Context(Reader);

		alignas(16) uint8 QuantizedState[32] = {};
		checkSlow(sizeof(QuantizedState) >= Serializer.QuantizedTypeSize);

		FNetDeserializeArgs DeserializeArgs;
		DeserializeArgs.Version = Serializer.Version;
		DeserializeArgs.Target = NetSerializerValuePointer(&QuantizedState[0]);
		DeserializeArgs.NetSerializerConfig = NetSerializerConfigParam(Serializer.DefaultConfig);
		Serializer.Deserialize(Context, DeserializeArgs);

		FNetDequantizeArgs DequantizeArgs;
		DequantizeArgs.Version = Serializer.Version;
		DequantizeArgs.Source = DeserializeArgs.Target;
		DequantizeArgs.Target = NetSerializerValuePointer(&OutVector);
		DequantizeArgs.NetSerializerConfig = NetSerializerConfigParam(Serializer.DefaultConfig);
		Serializer.Dequantize(Context, DequantizeArgs);
	}
	else
	{
		OutVector = DefaultValue;
	}
}

} // end namespace UE::Net
