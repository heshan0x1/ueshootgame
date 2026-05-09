// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// StringNetSerializerUtils.h —— 字符串 Serializer 共用工具
// -----------------------------------------------------------------------------
// 本文件提供两类工具：
//   1) `TStringCodec<WideCharType>`：Iris 内部专用的紧凑 Unicode 编解码器。
//      与标准 UTF-8 不同，它把 codepoint 切成 7 位/字节并通过最高位作为
//      "continuation"标志，**最多 3 字节**就能表达完整的 21 位 Unicode 范围；
//      而标准 UTF-8 在 BMP 之外需要 4 字节。该编码常用来压缩 FName/FString。
//   2) NetSerializer 帮助函数（`AdjustArraySize` / `CloneDynamicState` /
//      `FreeDynamicState` / `GrowDynamicState`）：所有持有动态字节缓冲的
//      字符串/名字 Serializer 都通过它们与 InternalNetSerializationContext
//      的 Alloc/Free 分配器交互，统一对齐到 4 字节，便于
//      `FNetBitStreamWriter::WriteBitStream` 直接以 uint32 块读写。
//
// 此外定义了 `FStringNetSerializerBase`：FString 序列化的"无源类型"基类，
// FStringNetSerializer / FSoftObject*NetSerializer 等都通过它实现公共部分
// （Serialize/Deserialize/Clone/Free），仅在 Quantize/Dequantize/IsEqual/Validate
// 中提供源类型相关的转换。
//
// 错误：UTF 解码错误统一抛 `GNetError_CorruptString`（FName，定义于 .cpp）。
// =============================================================================

#pragma once

#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"

namespace UE::Net
{

// 字符串损坏错误码（FName）。供反序列化阶段检测到非法 UTF / 非空尾零等异常时使用。
extern const FName GNetError_CorruptString;

}

namespace UE::Net::Private
{

class FStringNetSerializerUtils
{
public:
	// Note: 内存分配保证为 4 字节倍数 + 至少 4 字节对齐，方便
	// NetBitstreamWriter::WriteBitStream 以 uint32 为单位读写。

	// 比标准 UTF-8 更紧凑：每个 codepoint 至多 24 位（3 字节），UTF-8 则可能 32 位（4 字节）。
	// 编码方案：以"低 7 位 + 最高位 continuation"的方式向高位扩张：
	//   - codepoint < 0x80         → 1 字节 (0xxxxxxx)
	//   - codepoint < 0x400        → 2 字节 (1xxxxxxx 0xxxxxxx)
	//   - codepoint ≤ 0x10FFFF     → 3 字节 (1xxxxxxx 1xxxxxxx 0xxxxxxx)
	template<typename InWideCharType>
	class TStringCodec
	{
	public:
		typedef uint32 Codepoint;
		typedef uint8 EncodeType;
		typedef InWideCharType WideCharType;

		// 编码：把宽字符流编码为字节流（最多 3 字节/codepoint）。
		// 返回 false 表示编码缓冲不够或源串异常。
		static bool Encode(EncodeType* Dest, uint32 DestLen, const WideCharType* Source, uint32 SourceLen, uint32& OutDestLen)
		{
			return EncodeImpl(Dest, DestLen, Source, SourceLen, OutDestLen);
		}

		// 解码到 WideCharType 串。**调用前必须先调用 IsValidEncoding()** 做基础校验，
		// 否则 Decode 内部仅对最严重的错误做防护。
		static bool Decode(WideCharType* Dest, uint32 DestLen, const EncodeType* Source, uint32 SourceLen, uint32& OutDestLen)
		{
			return DecodeImpl(Dest, DestLen, Source, SourceLen, OutDestLen);
		}

		// 由原始（解码后）字符数给出"安全编码缓冲大小" = 3 * 字符数（最坏情况）。
		static uint32 GetSafeEncodedBufferLength(uint32 DecodedLen)
		{
			return 3*DecodedLen;
		}

		// 由编码字节数给出"安全解码缓冲大小"。若本端是 16 位 wide（UTF-16 系），
		// 一个 4 字节 codepoint 可能需要 2 个 16 位字符（surrogate pair），所以最坏情况
		// 是 4/3 倍展开；若本端是 32 位 wide（UTF-32 系），一字节最多对应 1 个 codepoint。
		static uint32 GetSafeDecodedBufferLength(uint32 EncodedLen)
		{
			if constexpr (sizeof(WideCharType) == 2)
			{
				// 若发送方使用 32 位字符，可能存在被压缩为 3 字节但本端需要 4 字节（surrogate pair）的 codepoint
				// 最坏情况是每个 codepoint 都需要 4 字节
				return (EncodedLen * 4U) / 3U;
			}
			else
			{
				return EncodedLen;
			}
		}

		// 检查最严重错误，例如某 codepoint 被编码为超过 3 字节，或位流穿越缓冲尾部。
		// 不检查 codepoint 数值合法性（留给 Decode 处理）。
		static bool IsValidEncoding(const EncodeType* Encoding, uint32 EncodeLen)
		{
			uint32 Continue = 0;
			for (uint32 EncodeIt = 0, EncodeEndIt = EncodeLen; EncodeIt != EncodeEndIt; ++EncodeIt)
			{
				uint32 EncodedCount = 0;
				do
				{
					// 单 codepoint 占用字节超过 3 个 / 越界
					if ((++EncodedCount > 3U) | (EncodeIt == EncodeEndIt))
					{
						return false;
					}

					EncodeType Code = Encoding[EncodeIt];
					Continue = (Code & 0x80) ? 1U : 0U;
					EncodeIt += Continue;
				} while (Continue);
			}

			return true;
		}

	private:

		// 检查无效 codepoint：
		//   - 超出 Unicode 上限 0x10FFFF
		//   - 0xFFFE / 0xFFFF（非字符）
		static bool IsInvalidCodepoint(uint32 Codepoint)
		{
			// 合法 codepoint 范围：[0, 0x10FFFF]，但 0xFFFE 与 0xFFFF 除外
			return (Codepoint > 0x10FFFFU) | ((Codepoint - 0xFFFEU) <= 1U);
		}

		// 是否为 UTF-16 surrogate 区间码点（[0xD800, 0xDFFF]）。
		static bool IsSurrogate(uint32 Codepoint)
		{
			return (Codepoint >= 0xD800U) & (Codepoint <= 0xDFFFU);
		}

		// 高代理（high surrogate）：[0xD800, 0xDBFF]
		static bool IsHighSurrogate(WideCharType Char)
		{
			return (Char >= WideCharType(0xD800U)) & (Char <= WideCharType(0xDBFFU));
		}

		// 低代理（low surrogate）：[0xDC00, 0xDFFF]
		static bool IsLowSurrogate(WideCharType Char)
		{
			return (Char >= WideCharType(0xDC00U)) & (Char <= WideCharType(0xDFFFU));
		}

		// 由代理对组合成 4 字节 codepoint。
		static uint32 GetCodepointFromSurrogates(WideCharType HighSurrogate, WideCharType LowSurrogate)
		{
			return (uint32(uint16(HighSurrogate) - 0xD800U) << 10U) + (uint32(uint16(LowSurrogate) - 0xDC00U) + 0x10000U);
		}

		// codepoint ≥ 0x10000 时，UTF-16 端必须用代理对表示。
		static bool IsInNeedOfSurrogates(uint32 Codepoint)
		{
			return (Codepoint >= 0x10000U) & (Codepoint <= 0x10FFFFU);
		}

		// 取得高代理位
		static WideCharType GetHighSurrogate(uint32 Codepoint)
		{
			return static_cast<WideCharType>(((Codepoint & 0xFFFFU) >> 10U) + 0xD800U);
		}

		// 取得低代理位
		static WideCharType GetLowSurrogate(uint32 Codepoint)
		{
			return static_cast<WideCharType>((Codepoint & 0x3FFU) + 0xDC00U);
		}

		/**
		 * 4 字节 wide（UTF-32 风格）编码实现：每个 codepoint 至多输出 3 字节。
		 * 检查无效 codepoint（>0x10FFFF 视作非法），用替换字符 0xFFFD 取代。
		 */
		template<typename T = WideCharType, typename TEnableIf<sizeof(T) == 4, char>::Type CharSize = 4>
		static bool EncodeImpl(EncodeType* Dest, uint32 DestLen, const WideCharType* Source, uint32 SourceLen, uint32& OutDestLen)
		{
			constexpr WideCharType LastCodePoint = 0x10FFFF;
			constexpr WideCharType ReplacementCharacter = 0xFFFD;

			// 必须保证目的缓冲足够（按最坏情况 3*Source）。否则返回 false，避免内层做越界检查。
			if (DestLen < 3U*SourceLen)
			{
				return false;
			}

			uint32 DestIt = 0;
			uint32 SourceIt = 0;
			for (const uint32 SourceEndIt = SourceLen; SourceIt < SourceEndIt; ++SourceIt)
			{
				WideCharType Char = Source[SourceIt];
				Char = (Char > LastCodePoint ? ReplacementCharacter : Char);

				if (Char < WideCharType(0x80))
				{
					// 1 字节
					Dest[DestIt++] = static_cast<EncodeType>(Char);
				}
				else if (Char < WideCharType(0x400))
				{
					// 2 字节：第一字节最高位置 1（continuation）
					Dest[DestIt + 0] = EncodeType(0x80) | static_cast<EncodeType>(Char >> 7U);
					Dest[DestIt + 1] = (Char & 0x7F);
					DestIt += 2;
				}
				else
				{
					// 3 字节
					Dest[DestIt + 0] = EncodeType(0x80) | static_cast<EncodeType>(Char >> 14U);
					Dest[DestIt + 1] = EncodeType(0x80) | static_cast<EncodeType>((Char >> 7U) & 0x7F);
					Dest[DestIt + 2] = (Char & 0x7F);
					DestIt += 3;
				}
			}

			OutDestLen = DestIt;
			return SourceIt == SourceLen;
		}

		/**
		 * 2 字节 wide（UTF-16 风格）编码：检测 surrogate 对并合并为单一 4 字节 codepoint，
		 * 这样一对代理只需 3 字节（而非两个独立字符各 3 字节 = 6 字节）。
		 * 单独出现的 surrogate 留给解码侧处理（视作普通字符）。
		 */
		template<typename T = WideCharType, typename TEnableIf<sizeof(T) == 2, char>::Type CharSize = 2>
		static bool EncodeImpl(EncodeType* Dest, uint32 DestLen, const WideCharType* Source, uint32 SourceLen, uint32& OutDestLen)
		{
			// 同上，必须保证目的缓冲足够
			if (DestLen < 3U*SourceLen)
			{
				return false;
			}

			uint32 DestIt = 0;
			uint32 SourceIt = 0;
			for (const uint32 SourceEndIt = SourceLen; SourceIt < SourceEndIt; ++SourceIt)
			{
				const WideCharType Char = Source[SourceIt];
				const WideCharType NextChar = Source[FMath::Min(SourceIt + 1U, SourceEndIt - 1U)];
				/**
				 * 一对代理（surrogate pair）若按 wide 单字符编码会得到 6 字节；这里合并为 4 字节 codepoint 后只需 3 字节
				 * 解码端负责处理无效代理序列（顺序错误、单代理）
				 */
				if (IsHighSurrogate(Char) && IsLowSurrogate(NextChar))
				{
					const uint32 Codepoint = GetCodepointFromSurrogates(Char, NextChar);
					Dest[DestIt + 0] = EncodeType(0x80) | static_cast<EncodeType>(Codepoint >> 14U);
					Dest[DestIt + 1] = EncodeType(0x80) | static_cast<EncodeType>((Codepoint >> 7U) & 0x7F);
					Dest[DestIt + 2] = (Codepoint & 0x7F);
					DestIt += 3;
					++SourceIt;
				}
				else if (Char < WideCharType(0x80))
				{
					Dest[DestIt++] = static_cast<EncodeType>(Char);
				}
				else if (Char < WideCharType(0x400))
				{
					Dest[DestIt + 0] = EncodeType(0x80) | static_cast<EncodeType>(Char >> 7U);
					Dest[DestIt + 1] = (Char & 0x7F);
					DestIt += 2;
				}
				else
				{
					Dest[DestIt + 0] = EncodeType(0x80) | static_cast<EncodeType>(Char >> 14U);
					Dest[DestIt + 1] = EncodeType(0x80) | static_cast<EncodeType>((Char >> 7U) & 0x7F);
					Dest[DestIt + 2] = (Char & 0x7F);
					DestIt += 3;
				}
			}

			OutDestLen = DestIt;
			return SourceIt == SourceLen;
		}

		// 4 字节字符目的版本：解码到 UTF-32 风格。
		template<typename T = WideCharType, typename TEnableIf<sizeof(T) == 4, char>::Type CharSize = 4>
		static bool DecodeImpl(WideCharType* Dest, uint32 DestLen, const EncodeType* Source, uint32 SourceLen, uint32& OutDestLen)
		{
			if (SourceLen == 0)
			{
				OutDestLen = 0;
				return true;
			}

			if (DestLen < GetSafeDecodedBufferLength(SourceLen))
			{
				return false;
			}

			constexpr WideCharType ReplacementCharacter = 0xFFFD;

			uint32 SurrogateCount = 0;
			uint32 DestIt = 0;
			bool bIsErrorDetected = false;
			for (uint32 SourceIt = 0, SourceEndIt = SourceLen, DestEndIt = DestLen; (SourceIt < SourceEndIt) & (DestIt < DestEndIt) & (!bIsErrorDetected); ++SourceIt)
			{
				EncodeType Byte;
				uint32 Codepoint;
				uint8 Mask;

				// 解码第一字节
				Byte = Source[SourceIt];
				Codepoint = Byte & 0x7FU;

				// 可选地解码第二字节。仅当首字节最高位为 1 时才前进 SourceIt（用 Mask 实现无分支）
				Mask = int8(Byte) >> 7U;
				SourceIt += 1U & Mask;
				if (SourceIt >= SourceEndIt)
				{
					bIsErrorDetected = true;
					break;
				}

				Byte = Source[SourceIt];
				Codepoint = (Codepoint << (7U & Mask)) | (Byte & 0x7FU);

				// 可选地解码第三字节。同上的无分支技巧。
				Mask = int8(Byte) >> 7U;
				SourceIt += 1U & Mask;
				if (SourceIt >= SourceEndIt)
				{
					bIsErrorDetected = true;
					break;
				}
				Byte = Source[SourceIt];
				Codepoint = (Codepoint << (7U & Mask)) | (Byte & 0x7FU);

				const bool bIsInvalidCodepoint = IsInvalidCodepoint(Codepoint);
				bIsErrorDetected = bIsErrorDetected | bIsInvalidCodepoint;
				SurrogateCount += IsSurrogate(Codepoint);

				// 非法 codepoint 替换为 0xFFFD
				Dest[DestIt++] = bIsInvalidCodepoint ? ReplacementCharacter : static_cast<WideCharType>(Codepoint);
			}

			// 确保解码末尾为 \0；缺失则强制写入并标记错误
			if (Dest[DestIt - 1] != 0)
			{
				bIsErrorDetected = true;
				// 这一步可能覆盖最后一个字符
				uint32 LastIndex = FMath::Min(DestIt, DestLen - 1U);
				Dest[LastIndex] = 0;
				DestIt = LastIndex + 1U;
			}

			uint32 CurrentLength = DestIt;
			if (SurrogateCount > 0)
			{
				// 如果检测到代理，则按 UTF-32 端的需要"原地合并"成一个完整 codepoint
				bIsErrorDetected = bIsErrorDetected || !CombineSurrogatesInPlace(SurrogateCount, Dest, DestLen, CurrentLength);
			}

			OutDestLen = CurrentLength;
			return !bIsErrorDetected;
		}

		// 2 字节字符目的版本：解码到 UTF-16 风格，必要时拆为代理对。
		template<typename T = WideCharType, typename TEnableIf<sizeof(T) == 2, char>::Type CharSize = 2>
		static bool DecodeImpl(WideCharType* Dest, uint32 DestLen, const EncodeType* Source, uint32 SourceLen, uint32& OutDestLen)
		{
			if (SourceLen == 0)
			{
				OutDestLen = 0;
				return true;
			}

			if (DestLen < GetSafeDecodedBufferLength(SourceLen))
			{
				return false;
			}

			constexpr WideCharType ReplacementCharacter = 0xFFFD;

			uint32 NeedSurrogatesCount = 0;
			uint32 DestIt = 0;
			bool bIsErrorDetected = false;
			for (uint32 SourceIt = 0, SourceEndIt = SourceLen, DestEndIt = DestLen; (SourceIt < SourceEndIt) & (DestIt < DestEndIt) & (!bIsErrorDetected); ++SourceIt)
			{
				EncodeType Byte;
				uint32 Codepoint;
				uint8 Mask;

				// 解码第一字节
				Byte = Source[SourceIt];
				Codepoint = Byte & 0x7FU;

				// 同 UTF-32 版本：根据高位条件性前进
				Mask = int8(Byte) >> 7U;
				SourceIt += 1U & Mask;
				if (SourceIt >= SourceEndIt)
				{
					bIsErrorDetected = true;
					break;
				}
				Byte = Source[SourceIt];
				Codepoint = (Codepoint << (7U & Mask)) | (Byte & 0x7FU);

				// 解码第三字节（同样无分支）
				Mask = int8(Byte) >> 7U;
				SourceIt += 1U & Mask;
				if (SourceIt >= SourceEndIt)
				{
					bIsErrorDetected = true;
					break;
				}
				Byte = Source[SourceIt];
				Codepoint = (Codepoint << (7U & Mask)) | (Byte & 0x7FU);

				const bool bIsInvalidCodePoint = IsInvalidCodepoint(Codepoint);
				const bool bIsInNeedOfSurrogates = IsInNeedOfSurrogates(Codepoint);
				bIsErrorDetected = bIsErrorDetected | bIsInvalidCodePoint;

				// 非法字符替换为 0xFFFD
				Codepoint = bIsInvalidCodePoint ? ReplacementCharacter : Codepoint;

				// 注意：对单独 surrogate 不做错误检查，直接当作普通字符（不会触发 IsInNeedOfSurrogates）
				if (bIsInNeedOfSurrogates)
				{
					// 循环条件保证至少能写一个；写两个时需额外校验
					if (DestIt + 1U >= DestLen)
					{
						bIsErrorDetected = true;
						break;
					}
					Dest[DestIt + 0] = GetHighSurrogate(Codepoint);
					Dest[DestIt + 1] = GetLowSurrogate(Codepoint);
					DestIt += 2U;
				}
				else
				{
					Dest[DestIt++] = bIsInvalidCodePoint ? ReplacementCharacter : static_cast<WideCharType>(Codepoint);
				}
			}

			OutDestLen = DestIt;
			return !bIsErrorDetected;
		}

		// 仅 UTF-32 端使用：原地把代理对合并为单一 4 字节 codepoint。
		// 内部委托 StringConv::InlineCombineSurrogates_Buffer 完成。
		// 期望返回长度 = 原长度 - SurrogateCount/2，且 SurrogateCount 为偶数。
		template<typename T = WideCharType, typename TEnableIf<sizeof(T) == 4, char>::Type CharSize = 4>
		static bool CombineSurrogatesInPlace(uint32 SurrogateCount, WideCharType* Buffer, uint32 BufferCapacity, uint32& InOutBufferLen)
		{
			const int32 InBufferLen = static_cast<int32>(InOutBufferLen);
			const int32 NewLength = StringConv::InlineCombineSurrogates_Buffer<WideCharType>(Buffer, InBufferLen);

			const int32 ExpectedNewLength = InBufferLen - static_cast<int32>(SurrogateCount/2U);
			const bool bSuccess = (NewLength == ExpectedNewLength) & ((SurrogateCount & 1) == 0);

			InOutBufferLen = static_cast<uint32>(NewLength);
			return bSuccess;
		}
	};

	// ---- NetSerializer 帮助函数 ----
	// 复制动态状态：把源量化态的 ElementStorage 深拷贝到目标。其它字段直接按值拷贝。
	// 调用方：FNameNetSerializer::CloneDynamicState / FStringNetSerializerBase::CloneDynamicState 等。
	template<typename QuantizedType, typename ElementType>
	static void CloneDynamicState(FNetSerializationContext& Context, QuantizedType& Target, const QuantizedType& Source)
	{
		// 先按位拷贝（含 ElementStorage 指针）；下面会替换为新分配的副本
		Target = Source;

		constexpr SIZE_T ElementSize = sizeof(ElementType);
		constexpr SIZE_T ElementAlignment = Align(alignof(ElementType), 4U);

		void* ElementStorage = nullptr;
		if (Source.ElementCount > 0)
		{
			// 通过 InternalContext 分配；4 字节倍数大小 + 至少 4 字节对齐
			ElementStorage = Context.GetInternalContext()->Alloc(Align(ElementSize*Source.ElementCount, 4U), ElementAlignment);
			FMemory::Memcpy(ElementStorage, Source.ElementStorage, ElementSize*Source.ElementCount);
		}
		Target.ElementCapacityCount = Source.ElementCount;
		Target.ElementCount = Source.ElementCount;
		Target.ElementStorage = ElementStorage;
	}

	// 释放量化态持有的动态分配内存，并把状态清零（QuantizedType 默认构造）。
	template<typename QuantizedType, typename ElementType>
	static void FreeDynamicState(FNetSerializationContext& Context, QuantizedType& Array)
	{
		Context.GetInternalContext()->Free(Array.ElementStorage);

		// 清空所有字段（避免悬挂指针等）
		Array = QuantizedType();
	}

	// 增长容量到 NewElementCount。**不保留旧内容**（字符串 Serializer 不支持
	// delta 压缩，因此每次增长后内容会被反序列化或拷贝重写）。
	template<typename QuantizedType, typename ElementType>
	static void GrowDynamicState(FNetSerializationContext& Context, QuantizedType& Array, uint16 NewElementCount)
	{
		checkSlow(NewElementCount > Array.ElementCapacityCount);

		constexpr SIZE_T ElementSize = sizeof(ElementType);
		constexpr SIZE_T ElementAlignment = Align(alignof(ElementType), 4U);

		// 我们不为字符串支持 delta 压缩，因此无需复制旧内容
		Context.GetInternalContext()->Free(Array.ElementStorage);

		void* NewElementStorage = Context.GetInternalContext()->Alloc(Align(ElementSize*NewElementCount, 4U), ElementAlignment);
		// 不需要清零内存
		//FMemory::Memzero(NewElementStorage, ElementSize*NewElementCount);

		Array.ElementCapacityCount = NewElementCount;
		Array.ElementCount = NewElementCount;
		Array.ElementStorage = NewElementStorage;
	}

	// 调整数组大小到 NewElementCount。
	//   - 0：完全释放（FreeDynamicState）
	//   - > 容量：重新分配（GrowDynamicState）
	//   - 其它：复用现有缓冲，只更新 ElementCount
	template<typename QuantizedType, typename ElementType>
	static void AdjustArraySize(FNetSerializationContext& Context, QuantizedType& Array, uint16 NewElementCount)
	{
		if (NewElementCount == 0)
		{
			// 释放
			FreeDynamicState<QuantizedType, ElementType>(Context, Array);
		}
		else if (NewElementCount > Array.ElementCapacityCount)
		{
			GrowDynamicState<QuantizedType, ElementType>(Context, Array, NewElementCount);
		}
		// 容量足够时只更新 ElementCount，避免重新分配
		else
		{
			Array.ElementCount = NewElementCount;
		}
	}
};

// 显式实例化声明：TCHAR 版本的 codec（在 .cpp 显式实例化定义）。避免每个 TU 重复生成。
extern template class FStringNetSerializerUtils::TStringCodec<TCHAR>;

}

namespace UE::Net
{

// 前向声明，避免本头引入额外重头文件
struct FNetSerializeArgs;
struct FNetDeserializeArgs;
struct FNetCloneDynamicStateArgs;
struct FNetFreeDynamicStateArgs;
struct FNetQuantizeArgs;
struct FNetDequantizeArgs;
struct FNetIsEqualArgs;
struct FNetValidateArgs;

}

namespace UE::Net::Private
{

/**
 * FStringNetSerializerBase —— FString 序列化的"无源类型"基类。
 *
 * 设计：把"位流读写、动态分配、量化态判等"等与源类型无关的逻辑放在基类，
 * 派生类（FStringNetSerializer / FSoftObjectPathNetSerializer / FSoftClassPathNetSerializer
 * / FSoftObjectNetSerializer）按需调用 protected Quantize/Dequantize/Validate
 * 完成 source ↔ FString 的转换桥接。
 *
 * 量化态 FQuantizedType 比 FName 版本简化（无 EName 短路径，因此没有
 * bIsString / ENameOrNumber 字段）：
 *   bIsEncoded : 1   —— 是 UTF 编码还是 ANSI 直存
 *   ElementCapacityCount / ElementCount —— 字节缓冲容量与有效长度（含尾零）
 *   ElementStorage   —— 动态分配的字节缓冲
 */
struct FStringNetSerializerBase
{
	// 持有动态状态
	static constexpr bool bHasDynamicState = true;

	// Types
	struct FQuantizedType
	{
		// 是否使用 UTF 编码（true：codec 编码字节；false：ANSI 直存字节）
		uint32 bIsEncoded : 1U;

		// 当前分配可容纳的元素数（含尾零）
		uint16 ElementCapacityCount;
		// 当前有效元素数（含尾零）。0 表示空字符串（无分配）
		uint16 ElementCount;
		// 动态分配的字节缓冲
		void* ElementStorage;
	};

	typedef FQuantizedType QuantizedType;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void CloneDynamicState(FNetSerializationContext&, const FNetCloneDynamicStateArgs&);
	static void FreeDynamicState(FNetSerializationContext&, const FNetFreeDynamicStateArgs&);

protected:
	// 由派生类提供源类型 → FString 转换后调用这两个函数
	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs&, const FString& Source);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs&, FString& Target);

	// 量化态判等：直接 memcmp 字节缓冲。
	static bool IsQuantizedEqual(FNetSerializationContext&, const FNetIsEqualArgs&);
	// 校验源 FString 长度是否在协议允许范围内
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs&, const FString& Source);
};

}
