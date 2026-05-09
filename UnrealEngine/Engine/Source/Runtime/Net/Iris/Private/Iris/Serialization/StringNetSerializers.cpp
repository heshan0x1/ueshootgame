// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// StringNetSerializers.cpp —— FName / FString 网络序列化实现
// -----------------------------------------------------------------------------
// 关键概念：
//   • Quantize / Dequantize：把源态（FString/FName）转换为 Iris 标准的 POD 量化态
//     （FQuantizedType），后者是 ReplicationState 中真正持久化的形态。量化态可
//     直接做 memcpy / memcmp，便于 Pre/Post 状态比较与 Delta 压缩。
//   • Serialize / Deserialize：在量化态与 NetBitStream 之间做位流读写。所有路径
//     都可能写出"长度 + 编码"对，长度走 PackedUint32 可变长压缩。
//   • CloneDynamicState / FreeDynamicState：FName/FString 的量化态含有动态分配
//     的字节缓冲（ElementStorage）；克隆/释放走 InternalNetSerializationContext
//     的 Alloc/Free 分配器，与 ReplicationSystem 的内存池对齐（4 字节）。
//   • bHasDynamicState = true：声明该序列化器持有动态状态，使 Iris 在状态拷贝
//     和销毁路径上调用 Clone/Free 钩子。
//
// 错误传播：
//   • Context.SetError(GNetError_*) 用于把字节流错误（数组超长、UTF 解码失败）
//     抛给上层，反序列化将立即终止。错误码是 FName，避免日志包含敏感数据。
//
// Token Store 协作（仅 FNameAsNetTokenNetSerializer）：
//   • 量化阶段调用 FNameTokenStore::GetOrCreateToken(FName) → 得到 FNetToken；
//   • 序列化阶段调用 FNetTokenStore::WriteNetTokenWithKnownType<FNameTokenStore>，
//     在 NetTokenStore 内部按 token 做差分导出（pending exports / append exports）；
//   • 反序列化阶段 ReadNetTokenWithKnownType 取回 token；
//   • 反量化阶段 ResolveToken 通过远端 NetTokenStoreState 把 token 解析回 FName。
//
// 注意 FQuantizedType 必须是 POD（trivially copyable），用 TIsPODType 特化告知容器。
// =============================================================================

#include "Iris/Serialization/StringNetSerializers.h"
#include "Iris/Serialization/StringNetSerializerUtils.h"
#include "Iris/Serialization/InternalNetSerializer.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Core/BitTwiddling.h"
#include "Containers/StringConv.h"
#include "GenericPlatform/GenericPlatformMemory.h"
#include "Iris/ReplicationSystem/NameTokenStore.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetExportContext.h"
#include "Net/Core/Trace/NetTrace.h"

// 由 USTRUCT 反射生成的 .gen.cpp（INLINE 形式，编译期被并入本编译单元），
// 提供 FNameNetSerializerConfig / FStringNetSerializerConfig 等 USTRUCT 元信息。
#include UE_INLINE_GENERATED_CPP_BY_NAME(StringNetSerializers)

// 将 ANSICHAR 视为单字节，简化 ANSI 字符串与 uint8 缓冲的强转。
static_assert(sizeof(ANSICHAR) == sizeof(uint8), "ANSICHAR is expected to be one byte.");

namespace UE::Net
{

// -----------------------------------------------------------------------------
// FNameNetSerializer —— FName 直接序列化器
// -----------------------------------------------------------------------------
// 序列化协议（位流自顶向下）：
//   bIsString : 1
//     ┣ 1（字符串路径）
//     │   bEncodeNumberFromIntMax : 1     —— 反向编码 Number 以减少位数
//     │   PackedInt32(Number)             —— FName 的数字后缀
//     │   bIsEncoded : 1                  —— 是 ANSI 还是 UTF 编码
//     │   PackedUint32(ElementCount-1)    —— 字节数
//     │   <ElementCount-1 字节>           —— ANSI 直存 / UTF 走自定义 codec
//     ┗ 0（硬编码 EName 短路径）
//         BitsNeededForEName              —— 仅写 EName 编号位（log2(MAX_NETWORKED_HARDCODED_NAME)）
//
// 设计动机：常见名字（如 NAME_None / NAME_Pawn 等）通过 EName 短路径只占几位；
// 一般名字按字符串走可压缩长度 + 紧凑编码。
struct FNameNetSerializer
{
	// Serializer 协议版本号。任何对协议位流格式的破坏性更改都需递增此值，
	// Iris 会在握手阶段检查 Serializer 版本以拒绝不兼容的对端。
	static const uint32 Version = 0;

	// 标记该 Serializer 持有动态分配的状态（ElementStorage），需要 Clone/Free 钩子。
	static constexpr bool bHasDynamicState = true;

	// Types

	// 量化态（POD）。精心设计为"全零内存即 FName(NAME_None)"，
	// 这样 ReplicationState 默认初始化无需额外构造逻辑。
	struct FQuantizedType
	{
		// 是否走字符串路径（否则为硬编码 EName 路径）
		uint32 bIsString : 1U;
		// 是否使用 (MAX_int32 - Number) 反向编码 Number 以缩短位长
		uint32 bEncodeNumberFromIntMax : 1U;
		// 字符串路径下，存储的字节是 UTF 编码（true）还是直接 ANSI（false）
		uint32 bIsEncoded : 1U;

		// 字符串路径：FName 的 Number 后缀；非字符串路径：EName 编号
		int32 ENameOrNumber;

		// 当前分配可容纳的元素数（含尾零字节）
		uint16 ElementCapacityCount;
		// 当前有效元素数（含尾零字节）。0 表示无字符串数据（EName 路径）。
		uint16 ElementCount;
		// 动态分配的字节缓冲，由 InternalNetSerializationContext 的分配器管理
		void* ElementStorage;
	};

	// 编译期校验量化态尺寸不超过 Public 头中声明的安全上限。
	static_assert(GetNameNetSerializerSafeQuantizedSize() >= sizeof(FQuantizedType));

	typedef FName SourceType;
	typedef FQuantizedType QuantizedType;
	typedef FNameNetSerializerConfig ConfigType;

	// Iris 要求每个 Serializer 暴露默认配置实例供 GetDefaultConfig() 使用。
	static const ConfigType DefaultConfig;

	//
	// 序列化六大钩子。具体语义见类定义上方注释。
	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);

	static void CloneDynamicState(FNetSerializationContext&, const FNetCloneDynamicStateArgs&);
	static void FreeDynamicState(FNetSerializationContext&, const FNetFreeDynamicStateArgs&);

private:
	// 表示一个 EName 编号需要的位数（即 ⌈log2(MAX_NETWORKED_HARDCODED_NAME+1)⌉）。
	// 仅可复制少量"网络级硬编码"名字。
	static const uint32 BitCountNeededForEName;
};
// 通过 UE_NET_IMPLEMENT_SERIALIZER_INTERNAL 宏完成 Serializer 的"接口表"实例化，
// 把上面 6+2 个静态函数装进 FNetSerializer vtable 风格的全局结构体。
UE_NET_IMPLEMENT_SERIALIZER_INTERNAL(FNameNetSerializer);
const FNameNetSerializer::ConfigType FNameNetSerializer::DefaultConfig;
const uint32 FNameNetSerializer::BitCountNeededForEName = UE::Net::GetBitsNeeded(MAX_NETWORKED_HARDCODED_NAME);

// -----------------------------------------------------------------------------
// FStringNetSerializer —— FString 序列化器
// -----------------------------------------------------------------------------
// 继承 FStringNetSerializerBase，复用其 Serialize/Deserialize/Clone/Free，本类
// 只重写需要源类型相关逻辑的接口（Quantize/Dequantize/IsEqual/Validate）。
struct FStringNetSerializer : public Private::FStringNetSerializerBase
{
	// Version
	static const uint32 Version = 0;

	typedef FString SourceType;
	typedef FStringNetSerializerConfig ConfigType;

	static const ConfigType DefaultConfig;

	//
	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);
};
UE_NET_IMPLEMENT_SERIALIZER_INTERNAL(FStringNetSerializer);
const FStringNetSerializer::ConfigType FStringNetSerializer::DefaultConfig;

// FNameNetSerializer
// 把量化态写入位流。详见类定义顶部的协议表。
void FNameNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	// 默认状态初始化阶段（用于哈希默认值），不参与位流写入。
	if (Context.IsInitializingDefaultState())
	{
		return;
	}

	const QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Source);
	
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	// WriteBool 同时写位并返回写入值（true 走字符串分支）。
	if (Writer->WriteBool(Value.bIsString))
	{
		// 写 Number：先写 1bit 反向编码标记，再用 PackedInt32 写实际值。
		// 反向编码（MAX_int32 - Number）能在 Number 较大（接近 MAX_int32）时
		// 让其变为接近 0 的小数，从而被 PackedInt32 用更少的字节表示。
		Writer->WriteBits(Value.bEncodeNumberFromIntMax, 1U);
		const int32 Number = Value.bEncodeNumberFromIntMax ? (MAX_int32 - Value.ENameOrNumber) : Value.ENameOrNumber;
		WritePackedInt32(Writer, Number);

		// 写字符串体：bIsEncoded(1) + PackedUint32(长度-1) + 字节流。
		// 注意 ElementCount 含尾零字节，写出的真实长度为 ElementCount-1。
		Writer->WriteBits(Value.bIsEncoded, 1U);
		WritePackedUint32(Writer, Value.ElementCount - 1U);
		Writer->WriteBitStream(static_cast<uint32*>(Value.ElementStorage), 0U, (Value.ElementCount - 1U)*8U);
	}
	else
	{
		// 硬编码 EName 短路径：仅写 BitCountNeededForEName 位。
		Writer->WriteBits(static_cast<uint32>(Value.ENameOrNumber), BitCountNeededForEName);
	}
}

// 读取并组装到 Target 量化态。会调用 AdjustArraySize 触发动态分配/释放。
void FNameNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	using namespace Private;

	// 与 Serialize 一致：默认状态初始化阶段不读取任何位。
	if (Context.IsInitializingDefaultState())
	{
		return;
	}

	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
	const uint32 CurrentElementCount = Target.ElementCount;

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	if (const bool bIsString = Reader->ReadBool())
	{
		// 还原 Number 与字符串元数据
		const bool bEncodeNumberFromIntMax = Reader->ReadBool();
		int32 Number = ReadPackedInt32(Reader);
		Number = bEncodeNumberFromIntMax ? (MAX_int32 - Number) : Number;
		
		const bool bIsEncoded = Reader->ReadBool();
		const uint32 NewElementCount = ReadPackedUint32(Reader) + 1U;
		// 防御：长度上限 (NAME_SIZE+1)*3，对应 UTF codec 最坏情况 3 字节/字符。
		if (NewElementCount == 0 || NewElementCount > (NAME_SIZE + 1)*3U)
		{
			Context.SetError(GNetError_ArraySizeTooLarge);
			return;
		}

		Target.bIsString = 1;
		Target.bEncodeNumberFromIntMax = bEncodeNumberFromIntMax;
		Target.bIsEncoded = bIsEncoded;
		Target.ENameOrNumber = Number;
		// 调整 ElementStorage 大小（必要时重新分配，否则原地复用）。
		FStringNetSerializerUtils::AdjustArraySize<QuantizedType, uint8>(Context, Target, static_cast<uint16>(NewElementCount));
		Reader->ReadBitStream(static_cast<uint32*>(Target.ElementStorage), (NewElementCount - 1U)*8U);
		// 末尾补 \0，便于调试观察 / 安全字符串构造。
		static_cast<uint8*>(Target.ElementStorage)[NewElementCount - 1] = 0;
		// 编码字符串还要做基础合法性校验（避免越界 / 非法 UTF 字节模式）。
		if (bIsEncoded && !FStringNetSerializerUtils::TStringCodec<WIDECHAR>::IsValidEncoding(static_cast<uint8*>(Target.ElementStorage), NewElementCount - 1U))
		{
			Context.SetError(GNetError_CorruptString);
			return;
		}
	}
	else
	{
		// 硬编码 EName 短路径：固定位数读取，复检该 EName 是否真的允许整数复制。
		const uint32 ENameNumber = Reader->ReadBits(BitCountNeededForEName);
		if (!ShouldReplicateAsInteger(EName(ENameNumber), FName(EName(ENameNumber))))
		{
			Context.SetError(GNetError_BitStreamError);
			return;
		}

		// EName 路径下不需要任何字节存储；缩到 0 释放此前可能持有的缓冲。
		constexpr uint32 NewElementCount = 0U;
		FStringNetSerializerUtils::AdjustArraySize<QuantizedType, uint8>(Context, Target, NewElementCount);

		Target.bIsString = 0U;
		Target.bEncodeNumberFromIntMax = 0;
		Target.bIsEncoded = 0;
		Target.ENameOrNumber = ENameNumber;
	}
}

// 把源 FName 转为量化态（按需分配字节缓冲、做 ANSI 直存或 UTF 编码）。
void FNameNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	using namespace Private;

	const SourceType SourceName = *reinterpret_cast<const SourceType*>(Args.Source);
	QuantizedType& TargetName = *reinterpret_cast<QuantizedType*>(Args.Target);

	// 仅当 Number 为 NAME_NO_NUMBER_INTERNAL 且映射到合法 EName 时，才有可能走整数短路径。
	const EName* AsEName = (SourceName.GetNumber() == NAME_NO_NUMBER_INTERNAL ? SourceName.ToEName() : nullptr);
	const bool bIsString = (AsEName == nullptr || !ShouldReplicateAsInteger(*AsEName, SourceName));
	if (bIsString)
	{
		const FNameEntry* DisplayNameEntry = SourceName.GetDisplayNameEntry();
		TargetName.bIsString = 1;
		// FNameEntry 标记是否宽字符。决定走 UTF codec 还是 ANSI 直存。
		TargetName.bIsEncoded = DisplayNameEntry->IsWide();
		TargetName.ENameOrNumber = SourceName.GetNumber();
		// 选择 Number / (MAX_int32 - Number) 中位数更少的那种编码方式
		TargetName.bEncodeNumberFromIntMax = GetBitsNeeded(MAX_int32 - TargetName.ENameOrNumber) < GetBitsNeeded(TargetName.ENameOrNumber);
		// Encode the string if needed
		if (TargetName.bIsEncoded)
		{
			// 取出 wide 字符串到栈缓冲。NAME_SIZE 是 FName 的最大长度。
			WIDECHAR TempWideBuffer[NAME_SIZE];
			SourceName.GetPlainWIDEString(TempWideBuffer);

			// 自定义 codec 至多 3 字节/codepoint，因此目的缓冲长度 = 3 * 字符数。
			const uint32 NameLength = DisplayNameEntry->GetNameLength() + 1U;
			constexpr uint32 MaxArrayCount = 65536U/3U;
			if (NameLength > MaxArrayCount)
			{
				Context.SetError(GNetError_ArraySizeTooLarge);
				return;
			}
			FStringNetSerializerUtils::AdjustArraySize<QuantizedType, uint8>(Context, TargetName, static_cast<uint16>(3U*NameLength));

			uint32 OutDestLen = 0;
			uint8* EncodingBuffer = static_cast<uint8*>(TargetName.ElementStorage);
			const bool bEncodingSuccess = FStringNetSerializerUtils::TStringCodec<WIDECHAR>::Encode(EncodingBuffer, 3U*NameLength, TempWideBuffer, NameLength, OutDestLen);
			if (bEncodingSuccess)
			{
				// 编码后的真实字节数（≤ 3*NameLength），后续序列化按此长度写出。
				TargetName.ElementCount = static_cast<uint16>(OutDestLen);
			}
			else
			{
				// 极少数情况编码失败：以空字符串替代避免下游崩溃。
				ensureMsgf(bEncodingSuccess, TEXT("Failed to encode string '%s'"), TempWideBuffer);
				TargetName.ElementCount = 1;
				EncodingBuffer[0] = 0;
			}
		}
		else
		{
			// ANSI 路径：包含尾零，直接拷贝。这里多做一次栈临时拷贝是为了
			// 适配 GetPlainANSIString 接口（无法直接写到 TargetName.ElementStorage）。
			const uint32 NewElementCount = DisplayNameEntry->GetNameLength() + 1U;
			constexpr uint32 MaxArrayCount = 65536U;
			if (NewElementCount > MaxArrayCount)
			{
				Context.SetError(GNetError_ArraySizeTooLarge);
				return;
			}
			FStringNetSerializerUtils::AdjustArraySize<QuantizedType, uint8>(Context, TargetName, static_cast<uint16>(NewElementCount));

			// 暂时无法避免双拷贝，除非使用过大的固定栈空间
			ANSICHAR TempAnsiBuffer[NAME_SIZE];
			SourceName.GetPlainANSIString(TempAnsiBuffer);

			FMemory::Memcpy(TargetName.ElementStorage, TempAnsiBuffer, NewElementCount*sizeof(ANSICHAR));
		}
	}
	else
	{
		// 硬编码 EName 路径：清空字节缓冲，仅记录 EName 编号。
		constexpr uint16 NewElementCount = 0U;
		FStringNetSerializerUtils::AdjustArraySize<QuantizedType, uint8>(Context, TargetName, NewElementCount);

		TargetName.bIsString = 0;
		// 这里 bIsEncoded 实际上没意义，硬编码 EName 一律按 ANSI 处理
		TargetName.bIsEncoded = 0;
		// 同样无意义：硬编码 Number 一律为 0
		TargetName.bEncodeNumberFromIntMax = 0;
		// 关键字段：写入 EName 编号
		TargetName.ENameOrNumber = static_cast<int32>(static_cast<uint32>(*AsEName));
	}
}

// 把量化态还原为源 FName。若是编码字符串，需走 codec 解码并校验末尾 \0。
void FNameNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	using namespace Private;

	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	if (Source.bIsString)
	{
		if (Source.bIsEncoded)
		{
			const uint32 SourceLen = Source.ElementCount;

			// 内联 4KB 栈缓冲（足够典型 FName）；超长时退化为堆。
			TArray<WIDECHAR, TInlineAllocator<4096U/sizeof(WIDECHAR)>> TempString;
			TempString.AddUninitialized(FStringNetSerializerUtils::TStringCodec<WIDECHAR>::GetSafeDecodedBufferLength(SourceLen));

			uint32 OutLength = 0;
			if (!FStringNetSerializerUtils::TStringCodec<WIDECHAR>::Decode(TempString.GetData(), TempString.Num(), static_cast<uint8*>(Source.ElementStorage), SourceLen, OutLength))
			{
				// 已在 Deserialize 中执行过 IsValidEncoding（仅做粗检），
				// 这里如果发现非法 codepoint，则降级为空 FName + 设置错误。
				Target = FName();
				Context.SetError(GNetError_CorruptString);
				return;
			}

			if (OutLength == 0 || TempString.GetData()[OutLength - 1] != 0)
			{
				// 空串应当走 EName 短路径；解码后必有尾零（即使错误路径也会强制写）。
				Context.SetError(GNetError_CorruptString);
				return;
			}
			// 用 (长度, ptr, Number) 构造 FName，避免再次 strlen。
			Target = FName(int32(OutLength - 1), TempString.GetData(), Source.ENameOrNumber);
		}
		else
		{
			// ANSI 直存路径：ElementCount 含尾零，长度 = ElementCount - 1。
			Target = FName(int32(Source.ElementCount - 1), static_cast<ANSICHAR*>(Source.ElementStorage), Source.ENameOrNumber);
		}
	}
	else
	{
		// EName 路径：直接构造为对应硬编码名字。
		Target = EName(static_cast<uint32>(Source.ENameOrNumber));
	}
}

// 量化态等值比较。先做位运算"差异为 0"的快速判定，再按需逐字符比较。
bool FNameNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		const QuantizedType& Value0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& Value1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);

		// 利用 XOR：对应字段相等则结果为 0；用 | 合并避免短路分支。
		const bool bIsEqual = ((Value0.bIsString ^ Value1.bIsString) | (Value0.ENameOrNumber ^ Value1.ENameOrNumber) | (Value0.ElementCount ^ Value1.ElementCount)) == 0;
		if (!bIsEqual)
		{
			return false;
		}

		/**
		 * 理想情况下我们希望有进程无关的快速 FName 比较方法。当前没有这种机制。
		 * 也可以在量化态中存储一个哈希，但目前未实现。本函数被调用次数有限，
		 * 上面的字段位检查已能过滤掉大多数差异。
		 */
		if (Value0.bIsString)
		{
			if (Value0.bIsEncoded)
			{
				// 编码字符串：解码后用 FName == 比较（处理大小写无关、Number 等）。
				SourceType SourceValue0;
				SourceType SourceValue1;

				FNetDequantizeArgs DequantizeArgs = {};
				DequantizeArgs.NetSerializerConfig = Args.NetSerializerConfig;

				DequantizeArgs.Source = Args.Source0;
				DequantizeArgs.Target = NetSerializerValuePointer(&SourceValue0);
				Dequantize(Context, DequantizeArgs);

				DequantizeArgs.Source = Args.Source1;
				DequantizeArgs.Target = NetSerializerValuePointer(&SourceValue1);
				Dequantize(Context, DequantizeArgs);

				return SourceValue0 == SourceValue1;
			}
			else
			{
				// ANSI 路径：直接做大小写不敏感的字节比较（FName 语义）。
				return FCStringAnsi::Strnicmp(static_cast<const ANSICHAR*>(Value0.ElementStorage), static_cast<const ANSICHAR*>(Value1.ElementStorage), Value0.ElementCount) == 0;;
			}
		}

		return true;
	}
	else
	{
		// 源态比较：直接交由 FName::operator== 处理（带 Number 的同名比较）。
		const SourceType& Value0 = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& Value1 = *reinterpret_cast<const SourceType*>(Args.Source1);
		const bool bIsEqual = (Value0 == Value1);
		return bIsEqual;
	}
}

// 校验源 FName 是否合法。仅做最弱校验：只要 IsValid() 即可（NAME_None 也算合法）。
bool FNameNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const SourceType& Value = *reinterpret_cast<const SourceType*>(Args.Source);
	const bool bIsValid = Value.IsValid();
	return bIsValid;
}

// 复制动态状态：复用 FStringNetSerializerUtils 模板，对 ANSICHAR 元素深拷贝缓冲。
void FNameNetSerializer::CloneDynamicState(FNetSerializationContext& Context, const FNetCloneDynamicStateArgs& Args)
{
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	return Private::FStringNetSerializerUtils::CloneDynamicState<QuantizedType, ANSICHAR>(Context, Target, Source);
}

// 释放动态状态：把 ElementStorage 交还给 InternalContext 分配器，并清零量化态。
void FNameNetSerializer::FreeDynamicState(FNetSerializationContext& Context, const FNetFreeDynamicStateArgs& Args)
{
	QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Source);
	return Private::FStringNetSerializerUtils::FreeDynamicState<QuantizedType, ANSICHAR>(Context, Value);
}

// FStringNetSerializer
// 量化：把 FString 委托到基类做"判 ANSI -> 拷贝/编码"逻辑。
void FStringNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	return FStringNetSerializerBase::Quantize(Context, Args, Source);
}

// 反量化：基类按 bIsEncoded 选 ANSI 直构造或 codec 解码后构造 FString。
void FStringNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);
	return FStringNetSerializerBase::Dequantize(Context, Args, Target);
}

// 判等：量化态 memcmp，源态用大小写敏感的字符串比较（不做 lower 转换以省去量化转换）。
bool FStringNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		return IsQuantizedEqual(Context, Args);
	}
	else
	{
		const SourceType& Value0 = *reinterpret_cast<SourceType*>(Args.Source0);
		const SourceType& Value1 = *reinterpret_cast<SourceType*>(Args.Source1);
		return Value0.Equals(Value1, ESearchCase::CaseSensitive);
	}
}

// 校验：限制最大编码后字节 ≤ 65535（PackedUint32 + 实际位流大小风险）。
bool FStringNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	return FStringNetSerializerBase::Validate(Context, Args, Source);
}

// -----------------------------------------------------------------------------
// FNameAsNetTokenNetSerializer —— FName 通过 NetToken 复用的序列化器
// -----------------------------------------------------------------------------
// 朴素实现：忽略 Number，等价于把"基础名字部分"当 token，不同 Number 会
// 重新导出名字部分。但好处是它对所有 FName 配置都通用（含 wide、特殊字符）。
//
// 量化态布局比 FName 直接序列化器小（只需 token + flags + EName），8 字节 NetToken
// 加上 4 字节标志位即可。
struct FNameAsNetTokenNetSerializerQuantizedType
{
	// FNetToken：NetTokenStore 中给某个 FName 分配的稳定整数 ID
	FNetToken NetToken;

	// 字符串路径下保留 Number 字段（实际未参与 token 协议，只是占位/兼容）；
	// 非字符串路径（EName 短路径）下存放 EName 编号
	int32 ENameOrNumber;

	// 是否走字符串（即 token）路径
	uint32 bIsString : 1U;
	// 与 FNameNetSerializer 一致：反向编码 Number 标志（当前实现下未使用）
	uint32 bEncodeNumberFromIntMax : 1U;
};

// 共用的安全量化态尺寸校验
static_assert(GetNameNetSerializerSafeQuantizedSize() >= sizeof(FNameAsNetTokenNetSerializerQuantizedType));

} // End of namespace UE::Net

// 声明该结构体为 POD，使其可被 TArray / 容器按位拷贝（Iris 状态克隆要求）。
template<> struct TIsPODType<UE::Net::FNameAsNetTokenNetSerializerQuantizedType> { enum { Value = true }; };

namespace UE::Net
{

// FNameAsNetTokenNetSerializer 实现体。与 FNameNetSerializer 接口对齐，
// 但量化阶段把 FName 转成 NetToken，省去字符串字节传输。
struct FNameAsNetTokenNetSerializer
{
	// Version
	static const uint32 Version = 0;

	// Types

	// 全零内存代表 FName(NAME_None)（NetToken 默认无效，bIsString=0 => EName 短路径）
	typedef FNameAsNetTokenNetSerializerQuantizedType FQuantizedType;
	typedef FName SourceType;
	typedef FQuantizedType QuantizedType;
	typedef FNameAsNetTokenNetSerializerConfig ConfigType;

	static const ConfigType DefaultConfig;

	//
	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);

private:

	// 工具方法。后续随 NetTokenStore 改造可与其他实现统一封装。
	// 通过远端 NetTokenStoreState 把 token 解析回 FName。
	static FName ResolveNetToken(FNetSerializationContext&, FNetToken NetToken);

	static const uint32 BitCountNeededForEName;
};

UE_NET_IMPLEMENT_SERIALIZER_INTERNAL(FNameAsNetTokenNetSerializer);
const FNameAsNetTokenNetSerializer::ConfigType FNameAsNetTokenNetSerializer::DefaultConfig;
const uint32 FNameAsNetTokenNetSerializer::BitCountNeededForEName = UE::Net::GetBitsNeeded(MAX_NETWORKED_HARDCODED_NAME);

// 序列化协议：bIsString:1，1 → 写 NetToken；0 → 写 EName 编号位。
// 注意：写 token 后会调用 FNetTokenStore::AppendExport 把该 token 加入待导出集，
// 后续在 NetExportContext 中真正把 token → FName 映射数据发到对端。
void FNameAsNetTokenNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	using namespace UE::Net::Private;

	// 默认状态哈希阶段不参与位流写入。
	// 如未来需要严格默认状态校验，可输出 lowercase 哈希
	if (Context.IsInitializingDefaultState())
	{
		return;
	}

	const QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Source);
	
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	if (Writer->WriteBool(Value.bIsString))
	{
		// 在已知 token 类型（FNameTokenStore）上下文中写出 token。
		// NetTokenStore 内部会按对端是否已知该 token 自动选择"长形式（含定义）"
		// 或"短形式（仅引用）"。
		Context.GetNetTokenStore()->WriteNetTokenWithKnownType<FNameTokenStore>(Context, Value.NetToken);

		// 把 token 加入导出队列：要么立即内联导出，要么挂起到 NetExportContext
		// 在合适时机批量导出（依据 InternalContext 的 bInlineObjectReferenceExports）。
		FNetTokenStore::AppendExport(Context, Value.NetToken);
	}
	else
	{
		// EName 短路径：直接写编号位，无需 token。
		Writer->WriteBits(static_cast<uint32>(Value.ENameOrNumber), BitCountNeededForEName);
	}
}

void FNameAsNetTokenNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	using namespace Private;

	// 与 Serialize 一致：默认状态阶段不读
	if (Context.IsInitializingDefaultState())
	{
		return;
	}

	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	if (const bool bIsString = Reader->ReadBool())
	{
		Target.bIsString = 1;

		// 始终读取 token；若位流溢出则视为读失败并提前返回（外层会处理错误）。
		FNetToken NetToken = Context.GetNetTokenStore()->ReadNetTokenWithKnownType<FNameTokenStore>(Context);

		if (Reader->IsOverflown())
		{
			return;
		}

		Target.NetToken = NetToken;
	}
	else
	{
		// EName 短路径校验
		const uint32 ENameNumber = Reader->ReadBits(BitCountNeededForEName);
		if (!ShouldReplicateAsInteger(EName(ENameNumber), FName(EName(ENameNumber))))
		{
			Context.SetError(GNetError_BitStreamError);
			return;
		}

		Target.bIsString = 0U;
		Target.bEncodeNumberFromIntMax = 0;
		Target.ENameOrNumber = ENameNumber;
	
		// 清空 token，避免误用先前残留值
		Target.NetToken = FNetToken();
	}
}

// 量化：把 FName → NetToken。GetOrCreateToken 会根据 ReplicationSystem 配置
// 决定是分配 Authority 端 token 还是 Local 端临时 token。
void FNameAsNetTokenNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	using namespace Private;

	const SourceType SourceName = *reinterpret_cast<const SourceType*>(Args.Source);
	QuantizedType& TargetName = *reinterpret_cast<QuantizedType*>(Args.Target);

	const EName* AsEName = (SourceName.GetNumber() == NAME_NO_NUMBER_INTERNAL ? SourceName.ToEName() : nullptr);
	const bool bIsString = (AsEName == nullptr || !ShouldReplicateAsInteger(*AsEName, SourceName));
	if (bIsString)
	{
		TargetName.bIsString = 1;
		TargetName.ENameOrNumber = 0;
		TargetName.bEncodeNumberFromIntMax = 0;

		// 关键：通过 ReplicationSystem 私有的 FNameTokenStore 获取或创建该 FName 的 token。
		// 同名后续分配将命中缓存，仅在第一次发送时导出"token <-> FName"映射。
		FNameTokenStore* NameTokenStore = Context.GetNetTokenStore()->GetDataStore<FNameTokenStore>();
		TargetName.NetToken = NameTokenStore->GetOrCreateToken(SourceName);
	}
	else
	{
		TargetName.bIsString = 0;
		TargetName.bEncodeNumberFromIntMax = 0;
		// EName 路径：直接保存编号即可
		TargetName.ENameOrNumber = static_cast<int32>(static_cast<uint32>(*AsEName));

		TargetName.NetToken = FNetToken();
	}
}

// 通过远端 NetTokenStoreState 把 token 解析回 FName。
// RemoteNetTokenStoreState 由握手 / 导出报文逐步建立，实质是从对端镜像过来的"token→Name"表。
FName FNameAsNetTokenNetSerializer::ResolveNetToken(FNetSerializationContext& Context, FNetToken NetToken)
{
	using namespace UE::Net::Private;

	FInternalNetSerializationContext* InternalContext = Context.GetInternalContext();
	FNameTokenStore* NameTokenStore = Context.GetNetTokenStore()->GetDataStore<FNameTokenStore>();
	return NameTokenStore->ResolveToken(NetToken, InternalContext->ResolveContext.RemoteNetTokenStoreState);
}

// 反量化：根据 token 路径或 EName 路径直接还原 FName。
void FNameAsNetTokenNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	using namespace Private;

	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	if (Source.bIsString)
	{
		// 通过远端 token 表解析。若 token 尚未导出（遗失/乱序），返回 NAME_None。
		Target = FNameAsNetTokenNetSerializer::ResolveNetToken(Context, Source.NetToken);
	}
	else
	{
		Target = EName(static_cast<uint32>(Source.ENameOrNumber));
	}
}

// 量化态等值比较。token 来自不同端（authority 与 non-authority）时无法
// 直接比较 token 数值，必须解析回 FName 再比；同源 token 走数值比较快速路径。
bool FNameAsNetTokenNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	using namespace Private;

	if (Args.bStateIsQuantized)
	{
		const QuantizedType& Value0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& Value1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);

		// 字段相等时 XOR 结果为 0
		const bool bIsEqual = ((Value0.bIsString ^ Value1.bIsString) | (Value0.ENameOrNumber ^ Value1.ENameOrNumber)) == 0;
		if (!bIsEqual)
		{
			return false;
		}

		// 还需对比真实 FName 才能正确处理 authority/non-authority token 混合的情况
		if (Value0.bIsString)
		{
			if (Value0.NetToken.IsAssignedByAuthority() != Value1.NetToken.IsAssignedByAuthority())
			{
				const FName Name0 = FNameAsNetTokenNetSerializer::ResolveNetToken(Context, Value0.NetToken);
				const FName Name1 = FNameAsNetTokenNetSerializer::ResolveNetToken(Context, Value1.NetToken);
				
				if (Name0 != Name1)
				{
					return false;
				}
			}
			else if (Value0.NetToken != Value1.NetToken)
			{
				return false;
			}
		}

		return true;
	}
	else
	{
		const SourceType& Value0 = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& Value1 = *reinterpret_cast<const SourceType*>(Args.Source1);
		const bool bIsEqual = (Value0 == Value1);
		return bIsEqual;
	}
}

// 简单合法性检查
bool FNameAsNetTokenNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const SourceType& Value = *reinterpret_cast<const SourceType*>(Args.Source);
	const bool bIsValid = Value.IsValid();
	return bIsValid;
}

}
