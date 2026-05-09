// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// StringNetSerializerUtils.cpp —— FStringNetSerializerBase 实现
// -----------------------------------------------------------------------------
// 本文件提供：
//   • `GNetError_CorruptString` FName 错误码的实例定义；
//   • `TStringCodec<TCHAR>` 的显式实例化（避免每个 .cpp 重复生成）；
//   • `FStringNetSerializerBase` 的所有共享实现：
//       - Serialize/Deserialize：长度 + 字节流（PackedUint32）
//       - Quantize/Dequantize：判断 ANSI/UTF 走不同路径
//       - Clone/Free：通过 InternalContext 分配器管理 ElementStorage
//       - IsQuantizedEqual：memcmp 字节缓冲（必须先比 ElementCount/编码标志）
//       - Validate：长度 ≤ 65535 字节
// =============================================================================

#include "StringNetSerializerUtils.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetSerializer.h"
#include "Misc/CString.h"

namespace UE::Net
{

// 定义错误码 FName。FName 形态便于在网络日志/错误 trace 中以离散 token 显示。
const FName GNetError_CorruptString("Corrupt string");

}

namespace UE::Net::Private
{

// 显式实例化 TCHAR 版本的 codec：链接期出现在唯一编译单元中，缩短编译时间。
template class FStringNetSerializerUtils::TStringCodec<TCHAR>;

// 把量化态 FString 写入位流：bIsEncoded(1) + PackedUint32(长度) + 字节流。
// 注意 ElementCount 包含尾零字节，写出长度为 ElementCount-1（空串记 0）。
void FStringNetSerializerBase::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	Writer->WriteBits(Value.bIsEncoded, 1U);
	WritePackedUint32(Writer, (Value.ElementCount > 0 ? Value.ElementCount - 1 : 0));
	if (Value.ElementCount > 0)
	{
		// 以位流方式写出字节缓冲。0 偏移、按字节数 * 8 位计。底层会按 uint32 对齐高效搬运。
		Writer->WriteBitStream(static_cast<uint32*>(Value.ElementStorage), 0U, (Value.ElementCount - 1U) * 8U);
	}
}

// 反序列化：与 Serialize 对称。读到长度后调用 AdjustArraySize 触发分配。
void FStringNetSerializerBase::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	const bool bIsEncoded = Reader->ReadBool();
	const uint32 NewElementCount = ReadPackedUint32(Reader);
	// 防御：长度上限 65535；并禁止"编码且空串"这种不合法组合（编码格式至少包含尾零）。
	if ((NewElementCount > 65535U) | (bIsEncoded & (NewElementCount == 0)))
	{
		Context.SetError(GNetError_ArraySizeTooLarge);
		return;
	}

	Target.bIsEncoded = bIsEncoded;
	// 量化态需要尾零字节，因此实际容量 = NewElementCount + 1（仅当非空时）。
	FStringNetSerializerUtils::AdjustArraySize<QuantizedType, uint8>(Context, Target, static_cast<uint16>(NewElementCount > 0 ? NewElementCount + 1 : 0));
	if (NewElementCount > 0)
	{
		Reader->ReadBitStream(static_cast<uint32*>(Target.ElementStorage), NewElementCount*8U);
		// 注意我们已经为终止字符分配了空间
		static_cast<uint8*>(Target.ElementStorage)[NewElementCount] = 0;
		// UTF 路径需校验编码合法性
		if (bIsEncoded && !FStringNetSerializerUtils::TStringCodec<TCHAR>::IsValidEncoding(static_cast<uint8*>(Target.ElementStorage), NewElementCount))
		{
			Context.SetError(GNetError_CorruptString);
			return;
		}
	}
}

// 复制动态状态（深拷贝 ElementStorage）
void FStringNetSerializerBase::CloneDynamicState(FNetSerializationContext& Context, const FNetCloneDynamicStateArgs& Args)
{
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	FStringNetSerializerUtils::CloneDynamicState<QuantizedType, ANSICHAR>(Context, Target, Source);
}

// 释放动态状态
void FStringNetSerializerBase::FreeDynamicState(FNetSerializationContext& Context, const FNetFreeDynamicStateArgs& Args)
{
	QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Source);
	FStringNetSerializerUtils::FreeDynamicState<QuantizedType, ANSICHAR>(Context, Value);
}

// 量化：检测纯 ANSI 串（IsPureAnsi）走快速路径，否则走 UTF codec 编码。
//   - ANSI 路径：直接 byte-cast 拷贝（每字符 & 0xFF），含尾零；
//   - UTF 路径：缓冲按最坏 3*Length 分配，调用 codec Encode。
void FStringNetSerializerBase::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args, const FString& Source)
{
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

	const bool bIsEncoded = !FCString::IsPureAnsi(*Source, Source.Len());
	if (bIsEncoded)
	{
		const uint32 StringLength = Source.Len() + 1U;
		// codec 最坏 3 字节/字符，因此输入字符数上限 = 65535/3
		constexpr uint32 MaxStringLength = 65535U/3U;
		if (StringLength > MaxStringLength)
		{
			Context.SetError(GNetError_ArraySizeTooLarge);
			return;
		}
		FStringNetSerializerUtils::AdjustArraySize<QuantizedType, uint8>(Context, Target, static_cast<uint16>(3U*StringLength));
		Target.bIsEncoded = true;

		uint32 OutDestLen = 0;
		uint8* EncodingBuffer = static_cast<uint8*>(Target.ElementStorage);
		const bool bEncodingSuccess = FStringNetSerializerUtils::TStringCodec<FString::ElementType>::Encode(EncodingBuffer, 3U*StringLength, GetData(Source), StringLength, OutDestLen);
		// 注意：编码失败时 ElementCount 仍设为 OutDestLen（部分写入），上层 IsValidEncoding 在反序列化时会检测
		Target.ElementCount = static_cast<uint16>(OutDestLen);
	}
	else
	{
		const uint32 StringLength = Source.Len();
		// ANSI 路径长度上限 65535（包含尾零）
		if (StringLength + 1U > 65535U)
		{
			Context.SetError(GNetError_ArraySizeTooLarge);
			return;
		}
		FStringNetSerializerUtils::AdjustArraySize<QuantizedType, uint8>(Context, Target, static_cast<uint16>(StringLength > 0 ? StringLength + 1 : 0));
		Target.bIsEncoded = false;

		if (StringLength > 0)
		{
			// 由于 FString 内部为 TCHAR，把高字节截掉得到 ANSI 字节（已知是纯 ANSI）
			const TCHAR* SourceString = GetData(Source);
			uint8* TargetString = static_cast<uint8*>(Target.ElementStorage);
			for (uint32 It = 0, EndIt = StringLength + 1; It != EndIt; ++It)
			{
				TargetString[It] = SourceString[It] & 0xFF;
			}
		}
	}
}

// 反量化：根据 bIsEncoded 走 codec 解码或 ANSI 直构造。
void FStringNetSerializerBase::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args, FString& Target)
{
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	if (Source.bIsEncoded)
	{
		const uint32 SourceLen = Source.ElementCount;
		// 4KB 内联栈缓冲，超长时退化到堆
		TArray<TCHAR, TInlineAllocator<4096U/sizeof(TCHAR)>> TempString;
		TempString.AddUninitialized(FStringNetSerializerUtils::TStringCodec<WIDECHAR>::GetSafeDecodedBufferLength(SourceLen));

		uint32 OutLength = 0;
		if (!FStringNetSerializerUtils::TStringCodec<WIDECHAR>::Decode(TempString.GetData(), TempString.Num(), static_cast<uint8*>(Source.ElementStorage), SourceLen, OutLength))
		{
			// 解码失败：清空目标并标记错误
			Target.Empty();
			Context.SetError(GNetError_CorruptString);
			return;
		}

		// 兼容容错：若解码结果末尾不是 \0，向后多算 1 字节避免越界
		if (OutLength == 0 || TempString.GetData()[OutLength - 1] != 0)
		{
			OutLength += 1U;
		}

		// 用 (ptr, size) 构造 FString，省掉再次 strlen
		Target = FString::ConstructFromPtrSize(TempString.GetData(), int32(OutLength - 1U));
	}
	else
	{
		// ANSI 直存：直接以字节缓冲 + 长度构造（FString 内部会做 ANSI→TCHAR 提升）
		Target = FString::ConstructFromPtrSize(static_cast<ANSICHAR*>(Source.ElementStorage), (Source.ElementCount > 0U ? Source.ElementCount - 1U : 0U));
	}
}

// 量化态判等：先比对编码标志和长度，再 memcmp 字节缓冲。
// 注意：仅在 bIsEncoded、ElementCount 完全一致时才比内容；不同编码方式即使可表达
// 同一字符串也判定为不等（量化态严格按字节比对，避免每次 dequantize 解码）。
bool FStringNetSerializerBase::IsQuantizedEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args)
{
	const QuantizedType& Value0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
	const QuantizedType& Value1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);
	if ((Value0.bIsEncoded != Value1.bIsEncoded) | (Value0.ElementCount != Value1.ElementCount))
	{
		return false;
	}

	const bool bIsEqual = FMemory::Memcmp(Value0.ElementStorage, Value1.ElementStorage, Value0.ElementCount) == 0;
	return bIsEqual;
}

// 校验：编码后字节数不得超过 65535（PackedUint32 + 协议位流尺寸约束）。
bool FStringNetSerializerBase::Validate(FNetSerializationContext&, const FNetValidateArgs&, const FString& Source)
{
	// 校验字符串长度。当前限制为编码后 65535 字节
	if (FStringNetSerializerUtils::TStringCodec<TCHAR>::GetSafeEncodedBufferLength(Source.Len() + 1U) > 65535U)
	{
		return false;
	}

	return true;
}

}
