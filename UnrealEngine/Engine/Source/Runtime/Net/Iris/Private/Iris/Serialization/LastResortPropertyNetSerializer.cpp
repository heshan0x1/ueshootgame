// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// LastResortPropertyNetSerializer.cpp —— 兜底：旧式 NetSerialize 桥接
// -----------------------------------------------------------------------------
// 当一个 FProperty 在 PropertyNetSerializerInfoRegistry 中找不到任何匹配的
// Iris-原生 NetSerializer 时，烘焙器就会让它走本"最后手段"路径：
//   * Quantize  : 调 FProperty::NetSerializeItem(FNetBitWriter, PackageMap, Source)
//                 把整段写入 NetBitWriter 字节流；同时通过 UIrisObjectReferencePackageMap
//                 把内部产生的 UObject* / NetGUID / NetToken 引用捕获，转成
//                 FIrisPackageMapExportsQuantizedType 一并保存。
//   * Serialize : 写入捕获的 exports + 报文长度 (PackedUint32) + 实际位流。
//   * Dequantize: 反向，用 FNetBitReader + InitForRead 把 exports 注入回 PackageMap，
//                 再调 NetSerializeItem 还原。
//
// 触发条件（"什么样的 FProperty 会落到这里"）：
//   * 属性自己实现了 op<<(FArchive&)/NetSerialize 但未被 Iris 单独识别；
//   * struct 里没有 UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO 声明；
//   * 通过 FName 反射不到对应的 named struct serializer；
//   * 上述三者皆未命中 → 烘焙阶段 fallback 到 LastResort（详见 NetSerializerSelector）。
//
// 限制（与文件名"Last Resort"含义一致）：
//   * 不实现 SerializeDelta（无法对未知字节流做语义级 diff）；
//   * 必须分配动态内存保存 quantized 比特流（长度运行时才知道）；
//   * 性能远逊于专用 serializer —— 仅用作过渡兼容。
//
// 与 PackageMap 的桥接：
//   * 旧式 NetSerialize 会通过 FArchive::SerializeObject 等捕获 UObject 引用，
//     UIrisObjectReferencePackageMap::InitForWrite() → 把引用转 index → 收集到
//     FIrisPackageMapExports；
//   * 反序列化端 InitForRead() → 把 index 转回真实 UObject* 注入回 NetSerializeItem。
//
// 默认状态哈希支持：bExcludeFromDefaultStateHash 和 NetTokenStorage > 0 时跳过哈希
// 写入（避免每个连接因 NetToken 不同而生成不同 hash）。
// =============================================================================

#include "InternalNetSerializers.h"
#include "UObject/CoreNet.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetReferenceCollector.h"
#include "Iris/Serialization/NetSerializerArrayStorage.h"
#include "Iris/Serialization/IrisObjectReferencePackageMap.h"
#include "Iris/Serialization/IrisPackageMapExportUtil.h"
#include "Iris/Serialization/NetExportContext.h"
#include "Net/Core/NetToken/NetTokenExportContext.h"
#include "Net/Core/Trace/NetTrace.h"

namespace UE::Net
{

// 量化态：捕获的对象引用 / NetToken exports + 实际比特流 + 容量信息。
struct FFLastResortPropertyNetSerializerQuantizedType
{
	// 旧式 NetSerialize 期间被 UIrisObjectReferencePackageMap 捕获的所有 UObject*/
	// NetGUID/NetToken 引用，已经量化为 Iris 标准导出态。
	FIrisPackageMapExportsQuantizedType QuantizedExports;

	// How many bytes the current allocation can hold.
	// Storage 当前已分配字节数（按 4 字节对齐），用于增长决策。
	uint32 ByteCapacity = 0;
	// How many bits are valid
	// 比特流中有效位数（不是字节数）。
	uint32 BitCount = 0;
	// 比特流首地址（按 32-bit 字对齐分配，Memcmp 优化用）。
	void* Storage = nullptr;
};

}

// POD 标注：可以直接 memcpy（Storage 等指针本身不需要构造/析构语义）。
template <> struct TIsPODType<UE::Net::FFLastResortPropertyNetSerializerQuantizedType> { enum { Value = true }; };

namespace UE::Net
{

struct FLastResortPropertyNetSerializer
{
	// Version
	static const uint32 Version = 0;

	// Traits
	// QuantizedType 自带堆分配（Storage） + 捕获的 exports；需 dynamic state 管理。
	static constexpr bool bHasDynamicState = true;
	// 旧式 NetSerialize 内部可能携带 UObject 引用，需要 Iris 引用收集。
	static constexpr bool bHasCustomNetReference = true;

	// Types

	// SourceType is unknown
	// SourceType 是 void —— 真实类型在 Config.Property 反射里，调用 FProperty::NetSerializeItem 时再用。
	typedef void SourceType;
	typedef FFLastResortPropertyNetSerializerQuantizedType QuantizedType;
	typedef FLastResortPropertyNetSerializerConfig ConfigType;

	//
	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);

	static void CloneDynamicState(FNetSerializationContext&, const FNetCloneDynamicStateArgs&);
	static void FreeDynamicState(FNetSerializationContext&, const FNetFreeDynamicStateArgs&);

	static void CollectNetReferences(FNetSerializationContext&, const FNetCollectReferencesArgs&);

private:
	// Storage 分配按 4 字节对齐 —— 最后字节用 uint32* 写零，便于 Memcmp 跳过尾部不确定 bit。
	static constexpr uint32 AllocationAlignment = 4U;

	// 容量管理三件套（与 ArrayProperty 类似但更简化）：
	static void FreeDynamicStateInternal(FNetSerializationContext&, QuantizedType& Value);
	static void GrowDynamicStateInternal(FNetSerializationContext&, QuantizedType& Value, uint32 NewBitCount);
	static void ShrinkDynamicStateInternal(FNetSerializationContext&, QuantizedType& Value, uint32 NewBitCount);
	static void AdjustStorageSize(FNetSerializationContext&, QuantizedType& Value, uint32 NewBitCount);
};
UE_NET_IMPLEMENT_SERIALIZER_INTERNAL(FLastResortPropertyNetSerializer);

// -----------------------------------------------------------------------------
// Serialize —— 写"exports + 长度 + 比特流"
// -----------------------------------------------------------------------------
// 默认状态哈希阶段会有特殊跳过逻辑（bExcludeFromDefaultStateHash / NetToken>0）。
void FLastResortPropertyNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	
	if (Context.IsInitializingDefaultState())
	{
		// If the config indicates that we should not be included in the default state hash write nothing
		if (Config->bExcludeFromDefaultStateHash)
		{
			return;
		}

		// For now we ignore this in default state hash if it has exported NetTokens as they will differ
		if (Value.QuantizedExports.NetTokenStorage.Num() > 0U)
		{
			return;
		}
	}

	UE_NET_TRACE_DYNAMIC_NAME_SCOPE(Config->Property.Get()->GetName(), *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);

	// If we have any captured exports, serialize them.
	FIrisPackageMapExportsUtil::Serialize(Context, Value.QuantizedExports);

	// Write data.
	WritePackedUint32(Writer, Value.BitCount);
	if (Value.BitCount > 0)
	{
		Writer->WriteBitStream(static_cast<uint32*>(Value.Storage), 0U, Value.BitCount);
	}
}

// -----------------------------------------------------------------------------
// Deserialize —— 与 Serialize 对称：先读 exports → 长度 → 比特流
// -----------------------------------------------------------------------------
void FLastResortPropertyNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	// For consistency, we should never get here. For now we ignore this in default state hash due to complications with asymmetrically serialized state.
	if (Context.IsInitializingDefaultState())
	{
		return;
	}

	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Target);
	const uint32 CurrentBitCount = Value.BitCount;

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	UE_NET_TRACE_DYNAMIC_NAME_SCOPE(Config->Property.Get()->GetName(), *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);

	// Read exports for packagemap.
	FIrisPackageMapExportsUtil::Deserialize(Context, Value.QuantizedExports);

	// Read the data
	const uint32 NewBitCount = ReadPackedUint32(Reader);
	if (!ensureMsgf(NewBitCount <= Config->MaxQuantizedSizeBits,
		TEXT("FLastResortPropertyNetSerializer::Deserialize data size of %u bits exceeds maximum of %u bits for property %s."),
		NewBitCount, Config->MaxQuantizedSizeBits, *Config->Property.Get()->GetName()))
	{
		Context.SetError(GNetError_ArraySizeTooLarge);
		return;
	}

	AdjustStorageSize(Context, Value, NewBitCount);

	Reader->ReadBitStream(static_cast<uint32*>(Value.Storage), NewBitCount);
}

// -----------------------------------------------------------------------------
// Quantize —— 调旧式 NetSerializeItem 写到 FNetBitWriter，然后转 quantized
// -----------------------------------------------------------------------------
// 流程：
//   1. 取 InternalContext 的 UIrisObjectReferencePackageMap，InitForWrite 准备捕获；
//   2. 用 FNetBitWriter（容量 8192 bit，会自动扩容）+ FNetTokenExportScope 兜住 Token；
//   3. 调 FProperty::NetSerializeItem(Archive, PackageMap, Source) —— 这是 UE 老路径；
//   4. 把捕获的 PackageMapExports + NetTokens 通过 FIrisPackageMapExportsUtil::Quantize
//      转成 Iris 标准导出态；
//   5. 把 BitWriter 内的字节流 memcpy 到 Storage（按 4 字节对齐）。
void FLastResortPropertyNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const FProperty* Property = Config->Property.Get();
	QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Target);

	// Setup UIrisObjectReferencePackageMap to capture exports
	Private::FInternalNetSerializationContext* InternalContext = Context.GetInternalContext();
	UIrisObjectReferencePackageMap* PackageMap = InternalContext ? InternalContext->PackageMap : nullptr;

	// Since this struct uses custom serialization path we need to explicitly capture exports in order to forward them to iris
	UE::Net::FIrisPackageMapExports PackageMapExports;
	UE::Net::FNetTokenExportContext::FNetTokenExports NetTokensPendingExport;

	if (PackageMap)
	{
		PackageMap->InitForWrite(&PackageMapExports);
	}

	// Use the Property serialization and store as binary blob.
	FNetBitWriter Archive(PackageMap, 8192);	
	FNetTokenExportScope NetTokenExportScope(Archive, Context.GetNetTokenStore(), NetTokensPendingExport);
	Property->NetSerializeItem(Archive, PackageMap, reinterpret_cast<void*>(Args.Source));

	const uint64 BitCount = Archive.GetNumBits();

	if (!ensureMsgf(BitCount <= Config->MaxQuantizedSizeBits,
		TEXT("FLastResortPropertyNetSerializer::Quantize: data size of %llu bits exceeds maximum of %u bits for property %s."),
		BitCount, Config->MaxQuantizedSizeBits, *Config->Property.Get()->GetName()))
	{
		Context.SetError(GNetError_ArraySizeTooLarge);
		return;
	}

	if (!ensureMsgf(!Archive.IsError(), TEXT("FLastResortPropertyNetSerializer::Quantize: NetBitWriter archive error in NetSerializeItem for property %s."), *Config->Property.Get()->GetName()))
	{
		Context.SetError(GNetError_ArraySizeTooLarge);
		return;
	}

	// Quantize captured exports
	FIrisPackageMapExportsUtil::Quantize(Context, PackageMapExports, NetTokensPendingExport, Value.QuantizedExports);

	// Deal with serialized data
	AdjustStorageSize(Context, Value, static_cast<uint32>(BitCount));
	if (BitCount > 0)
	{
		FMemory::Memcpy(Value.Storage, Archive.GetData(), (BitCount + 7U)/8U);
	}
}

// -----------------------------------------------------------------------------
// Dequantize —— 反向：注入 exports 到 PackageMap → 调 NetSerializeItem 还原
// -----------------------------------------------------------------------------
// 注意 ResolveContext：把 RemoteNetTokenStoreState + NetTokenStore 注入到 PackageMap，
// 让 NetSerializeItem 内部再访问 token 时能解析。
void FLastResortPropertyNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const FProperty* Property = Config->Property.Get();

	QuantizedType& Source = *reinterpret_cast<QuantizedType*>(Args.Source);

	// Dequantize and inject exports
	Private::FInternalNetSerializationContext* InternalContext = Context.GetInternalContext();
	UIrisObjectReferencePackageMap* PackageMap = InternalContext ? InternalContext->PackageMap : nullptr;
	UE::Net::FIrisPackageMapExports PackageMapExports;

	FIrisPackageMapExportsUtil::Dequantize(Context, Source.QuantizedExports, PackageMapExports);	

	// Setup resolve context for call into NetSerialize
	UE::Net::FNetTokenResolveContext ResolveContext;
	ResolveContext.RemoteNetTokenStoreState = Context.GetRemoteNetTokenStoreState();
	ResolveContext.NetTokenStore = Context.GetNetTokenStore();

	PackageMap->InitForRead(&PackageMapExports, ResolveContext);

	// Read data
	if (Source.BitCount)
	{
		FNetBitReader Archive(PackageMap, static_cast<uint8*>(Source.Storage), Source.BitCount);
		Property->NetSerializeItem(Archive, PackageMap, reinterpret_cast<void*>(Args.Target));
	}
	else
	{
		Property->ClearValue(reinterpret_cast<void*>(Args.Target));
	}
}

// -----------------------------------------------------------------------------
// IsEqual —— 量化态：长度 + exports + memcmp 字节流；真实态：FProperty::Identical
// -----------------------------------------------------------------------------
// memcmp 之所以能用 Align(BitCount/8, 4) 字节，是因为 Grow/Adjust 阶段最后字
// 已被清零（见 GrowDynamicStateInternal/AdjustStorageSize 的 LastWordIndex 注释），
// 这样不需要做 bit-level 比较。
bool FLastResortPropertyNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		const QuantizedType& Value0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& Value1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);
		if ((Value0.BitCount != Value1.BitCount))
		{
			return false;
		}

		if (!FIrisPackageMapExportsUtil::IsEqual(Context, Value0.QuantizedExports, Value1.QuantizedExports))
		{
			return false;
		}

		const bool bIsEqual = (Value0.BitCount == 0U) || FMemory::Memcmp(Value0.Storage, Value1.Storage, Align((Value0.BitCount + 7U)/8U, AllocationAlignment)) == 0;
		return bIsEqual;
	}
	else
	{
		const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
		const FProperty* Property = Config->Property.Get();

		const void* Value0 = reinterpret_cast<const void*>(Args.Source0);
		const void* Value1 = reinterpret_cast<const void*>(Args.Source1);
		const bool bIsEqual = Property->Identical(Value0, Value1);
		return bIsEqual;
	}
}

// -----------------------------------------------------------------------------
// CloneDynamicState —— 深克隆：exports + Storage 内容
// -----------------------------------------------------------------------------
void FLastResortPropertyNetSerializer::CloneDynamicState(FNetSerializationContext& Context, const FNetCloneDynamicStateArgs& Args)
{
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);

	// Clone captured exports
	FIrisPackageMapExportsUtil::CloneDynamicState(Context, Target.QuantizedExports, Source.QuantizedExports);

	const uint16 ByteCount = static_cast<uint16>(Align((Source.BitCount + 7U)/8U, AllocationAlignment));

	void* Storage = nullptr;
	if (ByteCount > 0)
	{
		Storage = Context.GetInternalContext()->Alloc(ByteCount, AllocationAlignment);
		FMemory::Memcpy(Storage, Source.Storage, ByteCount);
	}
	Target.ByteCapacity = ByteCount;
	Target.BitCount = Source.BitCount;
	Target.Storage = Storage;

}

// -----------------------------------------------------------------------------
// FreeDynamicState / FreeDynamicStateInternal —— 释放 exports + Storage
// -----------------------------------------------------------------------------
void FLastResortPropertyNetSerializer::FreeDynamicState(FNetSerializationContext& Context, const FNetFreeDynamicStateArgs& Args)
{
	return FreeDynamicStateInternal(Context, *reinterpret_cast<QuantizedType*>(Args.Source));
}

void FLastResortPropertyNetSerializer::FreeDynamicStateInternal(FNetSerializationContext& Context, QuantizedType& Value)
{
	// Clear all info

	// Free captured export data.
	FIrisPackageMapExportsUtil::FreeDynamicState(Context, Value.QuantizedExports);
	
	Context.GetInternalContext()->Free(Value.Storage);

	Value.BitCount = 0;
	Value.ByteCapacity = 0;
	Value.Storage = 0;
}

// -----------------------------------------------------------------------------
// GrowDynamicStateInternal —— 扩容（无 delta，旧内容直接丢弃）
// -----------------------------------------------------------------------------
// 由于 LastResort 不支持 delta compression，扩容时不需要保留旧数据，
// 直接 Free + Alloc 即可。最后字会被清零（Memcmp 优化）。
void FLastResortPropertyNetSerializer::GrowDynamicStateInternal(FNetSerializationContext& Context, QuantizedType& Value, uint32 NewBitCount)
{
	checkSlow(NewBitCount > Value.BitCount);

	const uint32 ByteCount = Align((NewBitCount + 7U)/8U, AllocationAlignment);

	// We don't support delta compression for the unknown contents of the bits so we don't need to copy the old data.
	Context.GetInternalContext()->Free(Value.Storage);

	void* Storage = Context.GetInternalContext()->Alloc(ByteCount, AllocationAlignment);

	// Clear the last word to support IsEqual Memcmp optimization.
	const uint32 LastWordIndex = ByteCount/4U - 1U;
	static_cast<uint32*>(Storage)[LastWordIndex] = 0U;

	Value.ByteCapacity = ByteCount;
	Value.BitCount = NewBitCount;
	Value.Storage = Storage;
}

// -----------------------------------------------------------------------------
// AdjustStorageSize —— 容量调整总入口（与 ArrayProperty 三分支类似但更简）
// -----------------------------------------------------------------------------
// 1) NewByteCapacity == 0 → 全释放
// 2) NewByteCapacity > 现有容量 → Grow
// 3) 否则只更新 BitCount + 清最后一字以便 IsEqual Memcmp 优化
void FLastResortPropertyNetSerializer::AdjustStorageSize(FNetSerializationContext& Context, QuantizedType& Value, uint32 NewBitCount)
{
	const uint32 NewByteCapacity = Align((NewBitCount + 7U)/8U, AllocationAlignment);
	if (NewByteCapacity == 0)
	{
		// Free everything
		FreeDynamicStateInternal(Context, Value);
	}
	else if (NewByteCapacity > Value.ByteCapacity)
	{
		GrowDynamicStateInternal(Context, Value, NewBitCount);
	}
	// If byte capacity is within the allocated capacity we just update the bit count and clear the last word
	else
	{
		Value.BitCount = NewBitCount;

		// Clear the last word to support IsEqual Memcmp optimization.
		const uint32 LastWordIndex = NewByteCapacity/4U - 1U;
		static_cast<uint32*>(Value.Storage)[LastWordIndex] = 0U;
	}
}

// 公共初始化辅助：把 FProperty 包装到 Config，使 LastResort 知道运行时调谁的 NetSerializeItem。
bool InitLastResortPropertyNetSerializerConfigFromProperty(FLastResortPropertyNetSerializerConfig& OutConfig, const FProperty* Property)
{
	OutConfig.Property = TFieldPath<FProperty>(const_cast<FProperty*>(Property));
	return Property != nullptr;
}

// -----------------------------------------------------------------------------
// CollectNetReferences —— 把 Quantize 时捕获的所有引用上交给 Iris 收集器
// -----------------------------------------------------------------------------
// 由于真正的引用都已经被捕获到 QuantizedExports 里，这里只需 forward。
void FLastResortPropertyNetSerializer::CollectNetReferences(FNetSerializationContext& Context, const FNetCollectReferencesArgs& Args)
{
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);
	FNetReferenceCollector& Collector = *reinterpret_cast<UE::Net::FNetReferenceCollector*>(Args.Collector);

	FIrisPackageMapExportsUtil::CollectNetReferences(Context, Value.QuantizedExports, Args.ChangeMaskInfo, Collector);
}


}
