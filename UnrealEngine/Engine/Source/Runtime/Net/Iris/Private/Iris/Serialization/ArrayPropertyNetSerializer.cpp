// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// ArrayPropertyNetSerializer.cpp —— 反射 TArray<T> 属性的通用 Serializer
// -----------------------------------------------------------------------------
// FArrayPropertyNetSerializer 处理一切 UE 反射可见的 TArray 属性，
// 通过元素 ReplicationStateDescriptor + Property 路径完成：
//   * Quantize：用 FScriptArrayHelper 取真实数组每个元素 → 调元素 Serializer.Quantize；
//   * Dequantize：反向，用 ScriptArrayHelper.Resize 后回写；
//   * Serialize：先写 1 bit 是否为空，否则写 Count（位宽 = ElementCountBitCount）
//                 再逐元素 Serialize；
//   * Delta：先写"长度是否相等"1 bit，相等不写长度；前 PrevCount 个元素走 SerializeDelta，
//            余下走标准 Serialize。元素级长 baseline 已是简化版，没有 LCS 智能 diff。
//
// QuantizedType 设计：
//   struct FQuantizedType {
//     uint16 ElementCapacityCount;  // 当前 ElementStorage 容量
//     uint16 ElementCount;          // 实际元素数
//     void*  ElementStorage;        // 元素量化态连续块（每元素 InternalSize 字节）
//   };
// 即"动态增长的元素量化数组"——按元素 Descriptor->InternalSize 紧凑存储。
//
// ChangeMask 元素级位映射：
//   * 元素数 > 可用 ChangeMask 位时使用"模运算映射"（element_idx % bitCount），
//     多个元素共享同 1 个 dirty bit；
//   * 第 0 位被外层 property 占用（"is dirty"宏标记），所以可用位为 BitCount - 1，
//     起始偏移 BitOffset + 1。
//
// 容量管理：AdjustArraySize() 包含 Free / Shrink（Free 元素 dynamic state） /
//          Grow / Memzero（保持新元素初始态确定）四种路径。
//
// 内部 trait：bIsForwardingSerializer + bHasDynamicState（量化态自己有动态分配）。
// =============================================================================

#include "InternalNetSerializers.h"
#include "Iris/Core/IrisLog.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetReferenceCollector.h"
#include "Iris/ReplicationSystem/ReplicationOperationsInternal.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Logging/StructuredLog.h"
#include "Net/Core/NetBitArray.h"
#include "Net/Core/Trace/NetTrace.h"

namespace UE::Net
{

struct FArrayPropertyNetSerializer
{
	// Version
	static const uint32 Version = 0;

	// Traits
	// bIsForwardingSerializer：本 serializer 自己仅做 dispatch，没有底层数据存放。
	static constexpr bool bIsForwardingSerializer = true;
	// bHasDynamicState：QuantizedType 内含堆分配（ElementStorage），需 Clone/Free。
	static constexpr bool bHasDynamicState = true;

	// Types
	// 量化态：紧凑保存 capacity/count/storage 三元组，
	// ElementStorage 内每元素占 ElementStateDescriptor->InternalSize 字节。
	struct FQuantizedType
	{
		// How many elements the current allocation can hold.
		// 当前 ElementStorage 已分配可容纳的元素数；用于 Grow 决策。
		uint16 ElementCapacityCount;
		// How many elements are valid
		// 当前实际元素数（<= ElementCapacityCount）。
		uint16 ElementCount;
		// 元素量化态首地址。
		void* ElementStorage;
	};

	// SourceType=FScriptArray：UE 反射数组的底层结构，配合 FScriptArrayHelper 使用。
	typedef FScriptArray SourceType;
	typedef FQuantizedType QuantizedType;
	typedef FArrayPropertyNetSerializerConfig ConfigType;

	//
	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);

	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs& Args);
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs& Args);

	static void CloneDynamicState(FNetSerializationContext&, const FNetCloneDynamicStateArgs&);
	static void FreeDynamicState(FNetSerializationContext&, const FNetFreeDynamicStateArgs&);

	static void CollectNetReferences(FNetSerializationContext&, const FNetCollectReferencesArgs&);

private:
	// 容量管理 helper：
	// - FreeDynamicStateInternal: 元素全释放 + Storage 归零
	// - GrowDynamicStateInternal:  容量增长（重新分配 + memcpy 旧元素）
	// - ShrinkDynamicStateInternal: 容量缩减（释放尾端元素 dynamic state，但保留 storage）
	// - AdjustArraySize:           上述三者的总入口（按 NewElementCount 分支）
	static void FreeDynamicStateInternal(FNetSerializationContext&, QuantizedType& Array, const ConfigType* Config);
	static void GrowDynamicStateInternal(FNetSerializationContext&, QuantizedType& Array, const ConfigType* Config, uint16 NewElementCount);
	static void ShrinkDynamicStateInternal(FNetSerializationContext&, QuantizedType& Array, const ConfigType* Config, uint16 NewElementCount);
	static void AdjustArraySize(FNetSerializationContext&, QuantizedType& Array, const ConfigType* Config, uint16 NewElementCount);
};
UE_NET_IMPLEMENT_SERIALIZER_INTERNAL(FArrayPropertyNetSerializer);

// 工具：从量化态 array 取元素裸数据指针 + 元素数。被 FastArray 等需要遍历
// 数组元素的高层组件使用。返回 0 表示空数组。
 uint32 GetNetArrayPropertyData(NetSerializerValuePointer QuantizedArray, NetSerializerValuePointer& OutArrayData)
{
	FArrayPropertyNetSerializer::QuantizedType& Array = *reinterpret_cast<FArrayPropertyNetSerializer::QuantizedType*>(QuantizedArray);

	if (Array.ElementCount > 0U && Array.ElementStorage)
	{
		OutArrayData = NetSerializerValuePointer(Array.ElementStorage);

		return Array.ElementCount;
	}

	return 0U;
}

// -----------------------------------------------------------------------------
// Serialize —— 写出量化态数组到比特流
// -----------------------------------------------------------------------------
// 报文格式：
//   [1 bit isEmpty] ([N bit ElementCount] [元素 0] [元素 1] ... [元素 N-1])?
// 其中 N = Config->ElementCountBitCount。元素位置可由 ChangeMask 跳过（仅当
// ChangeMaskInfo.BitCount > 1 时启用元素级 mask）。
void FArrayPropertyNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const QuantizedType& Array = *reinterpret_cast<const QuantizedType*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	if (Array.ElementCount == 0)
	{
		Writer->WriteBits(1U, 1U);
		return;
	}

	Writer->WriteBits(0U, 1U);
	Writer->WriteBits(Array.ElementCount, Config->ElementCountBitCount);

	const FReplicationStateDescriptor* ElementStateDescriptor = Config->StateDescriptor;
	const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];

	FNetSerializeArgs ElementArgs;
	ElementArgs.Version = 0;
	ElementArgs.NetSerializerConfig = ElementSerializerDescriptor.SerializerConfig;
	ElementArgs.Source = NetSerializerValuePointer(Array.ElementStorage);

	const FNetSerializer* ElementSerializer = ElementSerializerDescriptor.Serializer;
	const uint32 ElementSize = ElementStateDescriptor->InternalSize;

	// Check if we have additional changemask bits
	const FNetBitArrayView* ChangeMask = Args.ChangeMaskInfo.BitCount > 1U ? Context.GetChangeMask() : nullptr;
	if (!ChangeMask)
	{
		for (uint32 ElementIt = 0, ElementEndIt = Array.ElementCount; ElementIt < ElementEndIt; ++ElementIt)
		{
			UE_NET_TRACE_SCOPE(Element, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
			ElementSerializer->Serialize(Context, ElementArgs);
			ElementArgs.Source += ElementSize;
		}
	}
	else
	{
		// We currently use a simple modulo scheme for bits in the changemask, if we have more elements in the array then is covered by the changemask
		// several entries in the array will be considered dirty and be serialized
		// As the first bit is used by the owning property we need to offset by one and deduct one from the usable bits
		const uint32 ChangeMaskBitOffset = Args.ChangeMaskInfo.BitOffset + 1U;
		const uint32 ChangeMaskBitCount = Args.ChangeMaskInfo.BitCount - 1U;

		for (uint32 ElementIt = 0, ElementEndIt = Array.ElementCount; ElementIt < ElementEndIt; ++ElementIt)
		{
			if (ChangeMask->GetBit(ChangeMaskBitOffset + (ElementIt % ChangeMaskBitCount)))
			{
				UE_NET_TRACE_SCOPE(Element, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
				ElementSerializer->Serialize(Context, ElementArgs);
			}
			ElementArgs.Source += ElementSize;
		}
	}
}

// -----------------------------------------------------------------------------
// CollectNetReferences —— 收集数组中的对象引用
// -----------------------------------------------------------------------------
// 与 Serialize 类似的元素级遍历，但调用 FReplicationStateOperationsInternal::
// CollectReferences。如有 ChangeMask，仅遍历 dirty 的元素并构造单元素 mask 信息，
// 以便上层做 per-element 引用记录。
void FArrayPropertyNetSerializer::CollectNetReferences(FNetSerializationContext& Context, const FNetCollectReferencesArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const QuantizedType& Array = *reinterpret_cast<const QuantizedType*>(Args.Source);
	FNetReferenceCollector& Collector = *reinterpret_cast<FNetReferenceCollector*>(Args.Collector);

	if (Array.ElementCount == 0)
	{
		return;
	}

	const FReplicationStateDescriptor* ElementStateDescriptor = Config->StateDescriptor;
	const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];

	FNetSerializeArgs ElementArgs;
	ElementArgs.Version = 0;
	ElementArgs.NetSerializerConfig = ElementSerializerDescriptor.SerializerConfig;
	ElementArgs.Source = NetSerializerValuePointer(Array.ElementStorage);

	const FNetSerializer* ElementSerializer = ElementSerializerDescriptor.Serializer;
	const uint32 ElementSize = ElementStateDescriptor->InternalSize;

	// Check if we have additional changemask bits, if we have we only collect references for elements corresponding to dirty bits
	const FNetBitArrayView* ChangeMask = Args.ChangeMaskInfo.BitCount > 1U ? Context.GetChangeMask() : nullptr;
	if (!ChangeMask)
	{
		for (uint32 ElementIt = 0, ElementEndIt = Array.ElementCount; ElementIt < ElementEndIt; ++ElementIt)
		{
			Private::FReplicationStateOperationsInternal::CollectReferences(Context, Collector, Args.ChangeMaskInfo, reinterpret_cast<uint8*>(ElementArgs.Source), ElementStateDescriptor);
			ElementArgs.Source += ElementSize;
		}
	}
	else
	{
		// We currently use a simple modulo scheme for bits in the changemask, if we have more elements in the array then is covered by the changemask
		// several entries in the array will be considered dirty and be serialized
		// As the first bit is used by the owning property we need to offset by one and deduct one from the usable bits
		const uint32 ChangeMaskBitOffset = Args.ChangeMaskInfo.BitOffset + 1U;
		const uint32 ChangeMaskBitCount = Args.ChangeMaskInfo.BitCount - 1U;

		// This is the info that will be stored in the entries
		for (uint32 ElementIt = 0, ElementEndIt = Array.ElementCount; ElementIt < ElementEndIt; ++ElementIt)
		{
			const uint32 ElementBitOffset = ChangeMaskBitOffset + (ElementIt % ChangeMaskBitCount);
			if (ChangeMask->GetBit(ElementBitOffset))
			{
				FNetSerializerChangeMaskParam LocalChangeMaskInfo;
				LocalChangeMaskInfo.BitCount = 1U;
				LocalChangeMaskInfo.BitOffset = ElementBitOffset;

				// Collect references
				Private::FReplicationStateOperationsInternal::CollectReferences(Context, Collector, LocalChangeMaskInfo, reinterpret_cast<uint8*>(ElementArgs.Source), ElementStateDescriptor);
			}
			ElementArgs.Source += ElementSize;
		}
	}
}

// -----------------------------------------------------------------------------
// Deserialize —— 读出数组并按 NewElementCount 调整 storage
// -----------------------------------------------------------------------------
// 报文格式与 Serialize 对称。NewElementCount > Config->MaxElementCount 时
// 视为非法报文 → SetError(GNetError_ArraySizeTooLarge)。
//
// 关于元素 dynamic alloc：
// /*
//  * If array shrinks we may need to destruct elements (if element HasDynamicAllocation)
//  * If array grows we may need to construct elements (if element HasDynamicAllocation)
//  *
//  * If element has dynamic allocation we may not want to early out and just set element count to zero
//  * as that may hold an unnecessarily large amount of memory.
//  */
/*
 * If array shrinks we may need to destruct elements (if element HasDynamicAllocation)
 * If array grows we may need to construct elements (if element HasDynamicAllocation)
 * 
 * If element has dynamic allocation we may not want to early out and just set element count to zero
 * as that may hold an unnecessarily large amount of memory.
 */

void FArrayPropertyNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	QuantizedType& Array = *reinterpret_cast<QuantizedType*>(Args.Target);
	const uint32 CurrentElementCount = Array.ElementCount;

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	const uint32 bIsEmpty = Reader->ReadBits(1U);
	const uint32 NewElementCount = (bIsEmpty ? 0U : Reader->ReadBits(Config->ElementCountBitCount));

	if (NewElementCount > Config->MaxElementCount)
	{
		UE_LOGFMT(LogIris, Error, "FArrayPropertyNetSerializer::Deserialize Element count exceeds Config->MaxElementCount. {NewElementCount} > {MaxElementCount}. Config->ElementCountBitCount: {ElementCountBitCount}", NewElementCount, Config->MaxElementCount, Config->ElementCountBitCount);
		Context.SetError(GNetError_ArraySizeTooLarge);
		return;
	}

	const FReplicationStateDescriptor* ElementStateDescriptor = Config->StateDescriptor;
	const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];

	// Memory management
	AdjustArraySize(Context, Array, Config, static_cast<uint16>(NewElementCount));

	// Deserialize
	FNetDeserializeArgs ElementArgs;
	ElementArgs.Version = 0;
	ElementArgs.NetSerializerConfig = ElementSerializerDescriptor.SerializerConfig;
	ElementArgs.Target = NetSerializerValuePointer(Array.ElementStorage);

	const FNetSerializer* ElementSerializer = ElementSerializerDescriptor.Serializer;
	const uint32 ElementSize = ElementStateDescriptor->InternalSize;

	// Check if we have additional changemask bits
	const FNetBitArrayView* ChangeMask = Args.ChangeMaskInfo.BitCount > 1U ? Context.GetChangeMask() : nullptr;
	if (!ChangeMask)
	{
		for (uint32 ElementIt = 0, ElementEndIt = Array.ElementCount; ElementIt < ElementEndIt; ++ElementIt)
		{
			UE_NET_TRACE_SCOPE(Element, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
			ElementSerializer->Deserialize(Context, ElementArgs);
			ElementArgs.Target += ElementSize;
		}
	}
	else
	{
		// We currently use a simple modulo scheme for bits in the changemask, if we have more elements in the array then is covered by the changemask
		// several entries in the array will be considered dirty and be serialized
		// As the first bit is used by the owning property we need to offset by one and deduct one from the usable bits
		const uint32 ChangeMaskBitOffset = Args.ChangeMaskInfo.BitOffset + 1U;
		const uint32 ChangeMaskBitCount = Args.ChangeMaskInfo.BitCount - 1U;

		for (uint32 ElementIt = 0, ElementEndIt = Array.ElementCount; ElementIt < ElementEndIt; ++ElementIt)
		{
			if (ChangeMask->GetBit(ChangeMaskBitOffset + (ElementIt % ChangeMaskBitCount)))
			{
				UE_NET_TRACE_SCOPE(Element, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
				ElementSerializer->Deserialize(Context, ElementArgs);
			}
			ElementArgs.Target += ElementSize;
		}
	}
}

/** The below implementation assumes we wouldn't get the call unless the array had changed in the first place. That is
  * why we don't have an expensive IsEqual check as we expect the arrays to never or rarely be equal. The actual delta
  * compression just relies on the per element delta compressions. While this is naive approach this serializer is only
  * used in backwards compatibility mode.
  * We might want to support something like directed delta based on longest common subsequence. It can be costly to
  * calculate the LCS since it needs to be done in the SerializeDelta call. So LCS should probably not be enabled
  * by default. In a perfect world the serialization can be shared across many connections.
  */
// -----------------------------------------------------------------------------
// SerializeDelta —— 数组级 + 元素级双层增量
// -----------------------------------------------------------------------------
// 报文格式：
//   [1 bit bSameSizeArray]
//   [bSameSizeArray==0 时：N bit ElementCount]
//   [前 PrevCount 个元素：调元素 SerializeDelta（含 baseline）]
//   [溢出的新元素：直接 Serialize（无 baseline）]
//
// 注意：当前未做 LCS 这种"基于最长公共子序列"的智能 diff（成本太高），
// 仅做简单的"按位置对齐"增量，因此插入/删除中间元素后压缩效率会降低。
void FArrayPropertyNetSerializer::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const QuantizedType& Array = *reinterpret_cast<const QuantizedType*>(Args.Source);
	const QuantizedType& PrevArray = *reinterpret_cast<const QuantizedType*>(Args.Prev);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	const uint32 ElementCount = Array.ElementCount;
	const uint32 PrevElementCount = PrevArray.ElementCount;
	const bool bSameSizeArray = (ElementCount == PrevElementCount);
	Writer->WriteBits(bSameSizeArray, 1U);
	if (!bSameSizeArray)
	{
		Writer->WriteBits(Array.ElementCount, Config->ElementCountBitCount);
	}

	const FReplicationStateDescriptor* ElementStateDescriptor = Config->StateDescriptor;
	const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];
	const FNetSerializer* ElementSerializer = ElementSerializerDescriptor.Serializer;
	const uint32 ElementSize = ElementStateDescriptor->InternalSize;
	const FNetBitArrayView* ChangeMask = Args.ChangeMaskInfo.BitCount > 1U ? Context.GetChangeMask() : nullptr;

	// We currently use a simple modulo scheme for bits in the changemask, if we have more elements in the array then is covered by the changemask
	// several entries in the array will be considered dirty and be serialized
	// As the first bit is used by the owning property we need to offset by one and deduct one from the usable bits
	const uint32 ChangeMaskBitOffset = ChangeMask ? Args.ChangeMaskInfo.BitOffset + 1U : 0U;
	const uint32 ChangeMaskBitCount = ChangeMask ? Args.ChangeMaskInfo.BitCount - 1U : 0U;

	// Elements in the current array up to the previous size can use delta serialization.
	if (PrevElementCount)
	{
		FNetSerializeDeltaArgs ElementArgs;
		ElementArgs.Version = 0;
		ElementArgs.NetSerializerConfig = ElementSerializerDescriptor.SerializerConfig;
		ElementArgs.Source = NetSerializerValuePointer(Array.ElementStorage);
		ElementArgs.Prev = NetSerializerValuePointer(PrevArray.ElementStorage);

		if (!ChangeMaskBitCount)
		{
			for (uint32 ElementIt = 0, ElementEndIt = FMath::Min(ElementCount, PrevElementCount); ElementIt < ElementEndIt; ++ElementIt)
			{
				UE_NET_TRACE_SCOPE(Element, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
				ElementSerializer->SerializeDelta(Context, ElementArgs);
				ElementArgs.Source += ElementSize;
				ElementArgs.Prev += ElementSize;
			}
		}
		else
		{
			for (uint32 ElementIt = 0, ElementEndIt = FMath::Min(ElementCount, PrevElementCount); ElementIt < ElementEndIt; ++ElementIt)
			{
				if (ChangeMask->GetBit(ChangeMaskBitOffset + (ElementIt % ChangeMaskBitCount)))
				{
					UE_NET_TRACE_SCOPE(Element, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
					ElementSerializer->SerializeDelta(Context, ElementArgs);
				}
				ElementArgs.Source += ElementSize;
				ElementArgs.Prev += ElementSize;
			}
		}
	}

	// For elements that are beyond the previous size we rely on the element serializer having minimal bandwidth behavior in the standard serialization.
	if (ElementCount > PrevElementCount)
	{
		FNetSerializeArgs ElementArgs;
		ElementArgs.Version = 0;
		ElementArgs.NetSerializerConfig = ElementSerializerDescriptor.SerializerConfig;
		ElementArgs.Source = NetSerializerValuePointer(Array.ElementStorage) + PrevElementCount*ElementSize;

		if (!ChangeMaskBitCount)
		{
			for (uint32 ElementIt = PrevElementCount, ElementEndIt = ElementCount; ElementIt < ElementEndIt; ++ElementIt)
			{
				UE_NET_TRACE_SCOPE(Element, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
				ElementSerializer->Serialize(Context, ElementArgs);
				ElementArgs.Source += ElementSize;
			}
		}
		else
		{
			for (uint32 ElementIt = PrevElementCount, ElementEndIt = ElementCount; ElementIt < ElementEndIt; ++ElementIt)
			{
				if (ChangeMask->GetBit(ChangeMaskBitOffset + (ElementIt % ChangeMaskBitCount)))
				{
					UE_NET_TRACE_SCOPE(Element, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
					ElementSerializer->Serialize(Context, ElementArgs);
				}
				ElementArgs.Source += ElementSize;
			}
		}
	}
}

// -----------------------------------------------------------------------------
// DeserializeDelta —— SerializeDelta 反向
// -----------------------------------------------------------------------------
void FArrayPropertyNetSerializer::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	QuantizedType& Array = *reinterpret_cast<QuantizedType*>(Args.Target);
	const QuantizedType& PrevArray = *reinterpret_cast<const QuantizedType*>(Args.Prev);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	const uint32 PrevElementCount = PrevArray.ElementCount;
	const bool bSameSizeArray = !!Reader->ReadBits(1U);
	const uint32 ElementCount = (bSameSizeArray ? PrevElementCount : Reader->ReadBits(Config->ElementCountBitCount));

	if (ElementCount > Config->MaxElementCount)
	{
		UE_LOGFMT(LogIris, Error, "FArrayPropertyNetSerializer::DeserializeDelta Element count exceeds Config->MaxElementCount. {ElementCount} > {MaxElementCount}. Config->ElementCountBitCount: {ElementCountBitCount} bSameSizeArray: {bSameSizeArray}", ElementCount, Config->MaxElementCount, Config->ElementCountBitCount, bSameSizeArray);
		Context.SetError(GNetError_ArraySizeTooLarge);
		return;
	}

	const FReplicationStateDescriptor* ElementStateDescriptor = Config->StateDescriptor;
	const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];
	const FNetBitArrayView* ChangeMask = Args.ChangeMaskInfo.BitCount > 1U ? Context.GetChangeMask() : nullptr;

	// We currently use a simple modulo scheme for bits in the changemask, if we have more elements in the array then is covered by the changemask
	// several entries in the array will be considered dirty and be serialized
	// As the first bit is used by the owning property we need to offset by one and deduct one from the usable bits
	const uint32 ChangeMaskBitOffset = ChangeMask ? Args.ChangeMaskInfo.BitOffset + 1U : 0U;
	const uint32 ChangeMaskBitCount = ChangeMask ? Args.ChangeMaskInfo.BitCount - 1U : 0U;

	const FNetSerializer* ElementSerializer = ElementSerializerDescriptor.Serializer;
	const uint32 ElementSize = ElementStateDescriptor->InternalSize;

	AdjustArraySize(Context, Array, Config, static_cast<uint16>(ElementCount));

	// Elements in the current array up to the previous size can use delta serialization.
	if (PrevElementCount)
	{
		FNetDeserializeDeltaArgs ElementArgs;
		ElementArgs.Version = 0;
		ElementArgs.NetSerializerConfig = ElementSerializerDescriptor.SerializerConfig;
		ElementArgs.Target = NetSerializerValuePointer(Array.ElementStorage);
		ElementArgs.Prev = NetSerializerValuePointer(PrevArray.ElementStorage);

		if (!ChangeMaskBitCount)
		{
			for (uint32 ElementIt = 0, ElementEndIt = FMath::Min(ElementCount, PrevElementCount); ElementIt < ElementEndIt; ++ElementIt)
			{
				UE_NET_TRACE_SCOPE(Element, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
				ElementSerializer->DeserializeDelta(Context, ElementArgs);
				ElementArgs.Target += ElementSize;
				ElementArgs.Prev += ElementSize;
			}
		}
		else
		{
			for (uint32 ElementIt = 0, ElementEndIt = FMath::Min(ElementCount, PrevElementCount); ElementIt < ElementEndIt; ++ElementIt)
			{
				if (ChangeMask->GetBit(ChangeMaskBitOffset + (ElementIt % ChangeMaskBitCount)))
				{
					UE_NET_TRACE_SCOPE(Element, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
					ElementSerializer->DeserializeDelta(Context, ElementArgs);
				}
				ElementArgs.Target += ElementSize;
				ElementArgs.Prev += ElementSize;
			}
		}
	}

	// For elements that are beyond the previous size we rely on the element serializer having minimal bandwidth behavior in the standard serialization.
	if (ElementCount > PrevElementCount)
	{
		FNetDeserializeArgs ElementArgs;
		ElementArgs.Version = 0;
		ElementArgs.NetSerializerConfig = ElementSerializerDescriptor.SerializerConfig;
		ElementArgs.Target = NetSerializerValuePointer(Array.ElementStorage) + PrevElementCount*ElementSize;

		if (!ChangeMaskBitCount)
		{
			for (uint32 ElementIt = PrevElementCount, ElementEndIt = ElementCount; ElementIt < ElementEndIt; ++ElementIt)
			{
				UE_NET_TRACE_SCOPE(Element, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
				ElementSerializer->Deserialize(Context, ElementArgs);
				ElementArgs.Target += ElementSize;
			}
		}
		else
		{
			for (uint32 ElementIt = PrevElementCount, ElementEndIt = ElementCount; ElementIt < ElementEndIt; ++ElementIt)
			{
				if (ChangeMask->GetBit(ChangeMaskBitOffset + (ElementIt % ChangeMaskBitCount)))
				{
					UE_NET_TRACE_SCOPE(Element, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
					ElementSerializer->Deserialize(Context, ElementArgs);
				}
				ElementArgs.Target += ElementSize;
			}
		}
	}
}

// -----------------------------------------------------------------------------
// Quantize —— 真实 SourceType（FScriptArray，含 FProperty 描述的元素 layout）
//             → 量化态 FQuantizedType
// -----------------------------------------------------------------------------
// 调用 FScriptArrayHelper.GetRawPtr(i) 取每个真实元素地址，喂给元素 Serializer 的 Quantize。
void FArrayPropertyNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);

	FScriptArrayHelper ScriptArrayHelper(Config->Property.Get(), reinterpret_cast<const void*>(Args.Source));
	const uint32 ElementCount = static_cast<uint32>(ScriptArrayHelper.Num());

	if (ElementCount > Config->MaxElementCount)
	{
		UE_LOGFMT(LogIris, Error, "FArrayPropertyNetSerializer::Quantize Element count exceeds Config->MaxElementCount. {ElementCount} > {MaxElementCount}.", ElementCount, Config->MaxElementCount);
		Context.SetError(GNetError_ArraySizeTooLarge);
		return;
	}

	QuantizedType& TargetArray = *reinterpret_cast<QuantizedType*>(Args.Target);
	AdjustArraySize(Context, TargetArray, Config, static_cast<uint16>(ElementCount));

	const FReplicationStateDescriptor* ElementStateDescriptor = Config->StateDescriptor;
	const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];

	FNetQuantizeArgs ElementArgs;
	ElementArgs.Version = 0;
	ElementArgs.NetSerializerConfig = ElementSerializerDescriptor.SerializerConfig;
	ElementArgs.Source = 0;
	ElementArgs.Target = NetSerializerValuePointer(TargetArray.ElementStorage);

	const FNetBitArrayView* ChangeMask = Args.ChangeMaskInfo.BitCount > 1U ? Context.GetChangeMask() : nullptr;

	const FNetSerializer* ElementSerializer = ElementSerializerDescriptor.Serializer;
	const uint32 ElementSize = ElementStateDescriptor->InternalSize;

	if (!ChangeMask)
	{
		for (uint32 ElementIt = 0, ElementEndIt = ElementCount; ElementIt < ElementEndIt; ++ElementIt)
		{
			ElementArgs.Source = NetSerializerValuePointer(static_cast<const void*>(ScriptArrayHelper.GetRawPtr(ElementIt)));
			ElementSerializer->Quantize(Context, ElementArgs);
			ElementArgs.Target += ElementSize;
		}
	}
	else
	{
		// We currently use a simple modulo scheme for bits in the changemask, if we have more elements in the array then is covered by the changemask
		// several entries in the array will be considered dirty and be serialized
		// As the first bit is used by the owning property we need to offset by one and deduct one from the usable bits
		const uint32 ChangeMaskBitOffset = ChangeMask ? Args.ChangeMaskInfo.BitOffset + 1U : 0U;
		const uint32 ChangeMaskBitCount = ChangeMask ? Args.ChangeMaskInfo.BitCount - 1U : 0U;

		for (uint32 ElementIt = 0, ElementEndIt = ElementCount; ElementIt < ElementEndIt; ++ElementIt)
		{
			if (ChangeMask->GetBit(ChangeMaskBitOffset + (ElementIt % ChangeMaskBitCount)))
			{
				ElementArgs.Source = NetSerializerValuePointer(static_cast<const void*>(ScriptArrayHelper.GetRawPtr(ElementIt)));
				ElementSerializer->Quantize(Context, ElementArgs);
			}
			ElementArgs.Target += ElementSize;
		}
	}
}

// -----------------------------------------------------------------------------
// Dequantize —— 量化态 → 真实 SourceType
// -----------------------------------------------------------------------------
// 先用 ScriptArrayHelper.Resize(N) 调整真实数组容量（会析构/构造元素），
// 再逐个调用元素 Serializer.Dequantize 把量化态写回。
// 注意：ChangeMask 模式下当前不会做"局部反量化"——因为 FastArrayReplicationFragment
// 与 PropertyReplicationState::ApplyState 里仍有一些与"全量化"耦合的复杂状态。
void FArrayPropertyNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);

	FScriptArrayHelper ScriptArrayHelper(Config->Property.Get(), reinterpret_cast<void*>(Args.Target));
	const QuantizedType& SourceArray = *reinterpret_cast<QuantizedType*>(Args.Source);
	const uint32 ElementCount = SourceArray.ElementCount;
	ScriptArrayHelper.Resize(ElementCount);

	const FReplicationStateDescriptor* ElementStateDescriptor = Config->StateDescriptor;
	const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];

	FNetDequantizeArgs ElementArgs;
	ElementArgs.Version = 0;
	ElementArgs.NetSerializerConfig = ElementSerializerDescriptor.SerializerConfig;
	ElementArgs.Source = NetSerializerValuePointer(SourceArray.ElementStorage);
	ElementArgs.Target = 0;

	const FNetBitArrayView* ChangeMask = Args.ChangeMaskInfo.BitCount > 1U ? Context.GetChangeMask() : nullptr;

	const FNetSerializer* ElementSerializer = ElementSerializerDescriptor.Serializer;
	const uint32 ElementSize = ElementStateDescriptor->InternalSize;

	// Currently we do not support partial dequantize using changemask for array properties due to complexities elsewhere (FastArrayReplicationFragment and PropertyReplicationState::ApplyState)
	if (!ChangeMask)
	{
		for (uint32 ElementIt = 0, ElementEndIt = ElementCount; ElementIt < ElementEndIt; ++ElementIt)
		{
			ElementArgs.Target = NetSerializerValuePointer(static_cast<const void*>(ScriptArrayHelper.GetRawPtr(ElementIt)));
			ElementSerializer->Dequantize(Context, ElementArgs);
			ElementArgs.Source += ElementSize;
		}
	}
	else
	{
		// We currently use a simple modulo scheme for bits in the changemask, if we have more elements in the array then is covered by the changemask
		// several entries in the array will be considered dirty and be serialized
		// As the first bit is used by the owning property we need to offset by one and deduct one from the usable bits
		const uint32 ChangeMaskBitOffset = ChangeMask ? Args.ChangeMaskInfo.BitOffset + 1U : 0U;
		const uint32 ChangeMaskBitCount = ChangeMask ? Args.ChangeMaskInfo.BitCount - 1U : 0U;

		for (uint32 ElementIt = 0, ElementEndIt = ElementCount; ElementIt < ElementEndIt; ++ElementIt)
		{
			if (ChangeMask->GetBit(ChangeMaskBitOffset + (ElementIt % ChangeMaskBitCount)))
			{
				ElementArgs.Target = NetSerializerValuePointer(static_cast<const void*>(ScriptArrayHelper.GetRawPtr(ElementIt)));
				ElementSerializer->Dequantize(Context, ElementArgs);
			}
			ElementArgs.Source += ElementSize;
		}
	}
}

// -----------------------------------------------------------------------------
// IsEqual —— 量化/真实两态分支
// -----------------------------------------------------------------------------
// 量化态：先比 ElementCount，再逐元素调用元素 Serializer.IsEqual（量化）。
// 真实态：从 FScriptArrayHelper 取真实元素地址做逐元素比较。
bool FArrayPropertyNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);

		const QuantizedType& Array0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& Array1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);

		const uint32 ElementCount0 = Array0.ElementCount;
		const uint32 ElementCount1 = Array1.ElementCount;
		if (ElementCount0 != ElementCount1)
		{
			return false;
		}

		const FReplicationStateDescriptor* ElementStateDescriptor = Config->StateDescriptor;
		const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];

		FNetIsEqualArgs ElementArgs;
		ElementArgs.Version = 0;
		ElementArgs.NetSerializerConfig = ElementSerializerDescriptor.SerializerConfig;
		ElementArgs.Source0 = NetSerializerValuePointer(Array0.ElementStorage);
		ElementArgs.Source1 = NetSerializerValuePointer(Array1.ElementStorage);
		ElementArgs.bStateIsQuantized = true;
		const FNetSerializer* ElementSerializer = ElementSerializerDescriptor.Serializer;
		const uint32 ElementSize = ElementStateDescriptor->InternalSize;
		for (uint32 ElementIt = 0, ElementEndIt = ElementCount0; ElementIt < ElementEndIt; ++ElementIt)
		{
			if (!ElementSerializer->IsEqual(Context, ElementArgs))
			{
				return false;
			}

			ElementArgs.Source0 += ElementSize;
			ElementArgs.Source1 += ElementSize;
		}
	}
	else
	{
		const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);

		FScriptArrayHelper ScriptArrayHelper0(Config->Property.Get(), reinterpret_cast<const void*>(Args.Source0));
		FScriptArrayHelper ScriptArrayHelper1(Config->Property.Get(), reinterpret_cast<const void*>(Args.Source1));
		const uint32 ElementCount0 = static_cast<uint32>(ScriptArrayHelper0.Num());
		const uint32 ElementCount1 = static_cast<uint32>(ScriptArrayHelper1.Num());
		if (ElementCount0 != ElementCount1)
		{
			return false;
		}

		const FReplicationStateDescriptor* ElementStateDescriptor = Config->StateDescriptor;
		const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];

		FNetIsEqualArgs ElementArgs;
		ElementArgs.Version = 0;
		ElementArgs.NetSerializerConfig = ElementSerializerDescriptor.SerializerConfig;
		ElementArgs.bStateIsQuantized = false;
		const FNetSerializer* ElementSerializer = ElementSerializerDescriptor.Serializer;
		for (uint32 ElementIt = 0, ElementEndIt = ElementCount0; ElementIt < ElementEndIt; ++ElementIt)
		{
			ElementArgs.Source0 = NetSerializerValuePointer(static_cast<const void*>(ScriptArrayHelper0.GetRawPtr(ElementIt)));
			ElementArgs.Source1 = NetSerializerValuePointer(static_cast<const void*>(ScriptArrayHelper1.GetRawPtr(ElementIt)));
			if (!ElementSerializer->IsEqual(Context, ElementArgs))
			{
				return false;
			}
		}
	}

	return true;
}

// -----------------------------------------------------------------------------
// Validate —— 长度与元素逐个 Validate；元素 Descriptor 必须仅 1 个 member
// -----------------------------------------------------------------------------
// ArrayProperty 烘焙后，元素描述符应仅含一个 member（即元素本身）；如不满足
// 则 Descriptor 错误，直接 false。
bool FArrayPropertyNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);

	FScriptArrayHelper ScriptArrayHelper(Config->Property.Get(), reinterpret_cast<const void*>(Args.Source));
	const uint32 ElementCount = static_cast<uint32>(ScriptArrayHelper.Num());
	if (ElementCount > Config->MaxElementCount)
	{
		return false;
	}

	// We expect the inner element ReplicationStateDescriptor to contain exactly one element.
	const FReplicationStateDescriptor* ElementStateDescriptor = Config->StateDescriptor;
	if (ElementStateDescriptor->MemberCount != 1U)
	{
		return false;
	}

	const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];

	FNetValidateArgs ElementArgs;
	ElementArgs.Version = 0;
	ElementArgs.NetSerializerConfig = ElementSerializerDescriptor.SerializerConfig;
	const FNetSerializer* ElementSerializer = ElementSerializerDescriptor.Serializer;
	for (uint32 ElementIt = 0, ElementEndIt = ElementCount; ElementIt < ElementEndIt; ++ElementIt)
	{
		ElementArgs.Source = NetSerializerValuePointer(static_cast<const void*>(ScriptArrayHelper.GetRawPtr(ElementIt)));
		if (!ElementSerializer->Validate(Context, ElementArgs))
		{
			return false;
		}
	}

	return true;
}

// -----------------------------------------------------------------------------
// CloneDynamicState —— 深克隆量化态（基线快照、连接副本等）
// -----------------------------------------------------------------------------
// 1. 重新分配 ElementStorage 并 Memcpy 原数据；
// 2. 若元素 Descriptor 含 dynamic state，再递归 Clone 每个元素的 dynamic 部分。
void FArrayPropertyNetSerializer::CloneDynamicState(FNetSerializationContext& Context, const FNetCloneDynamicStateArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	QuantizedType& TargetArray = *reinterpret_cast<QuantizedType*>(Args.Target);
	const QuantizedType& SourceArray = *reinterpret_cast<const QuantizedType*>(Args.Source);

	const FReplicationStateDescriptor* ElementStateDescriptor = Config->StateDescriptor;

	const SIZE_T ElementSize = ElementStateDescriptor->InternalSize;
	const SIZE_T ElementAlignment = ElementStateDescriptor->InternalAlignment;

	// Clone storage
	void* ElementStorage = nullptr;
	if (SourceArray.ElementCount > 0)
	{
		ElementStorage = Context.GetInternalContext()->Alloc(ElementSize*SourceArray.ElementCount, ElementAlignment);
		FMemory::Memcpy(ElementStorage, SourceArray.ElementStorage, ElementSize*SourceArray.ElementCount);
	}
	TargetArray.ElementCapacityCount = SourceArray.ElementCount;
	TargetArray.ElementCount = SourceArray.ElementCount;
	TargetArray.ElementStorage = ElementStorage;

	// If no member has dynamic state then there's nothing more to do.
	if (!EnumHasAnyFlags(ElementStateDescriptor->Traits, EReplicationStateTraits::HasDynamicState) || SourceArray.ElementCount == 0)
	{
		return;
	}

	const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];

	FNetCloneDynamicStateArgs ElementArgs;
	ElementArgs.Version = 0;
	ElementArgs.NetSerializerConfig = ElementSerializerDescriptor.SerializerConfig;
	ElementArgs.Source = NetSerializerValuePointer(SourceArray.ElementStorage);
	ElementArgs.Target = NetSerializerValuePointer(TargetArray.ElementStorage);

	const FNetSerializer* ElementSerializer = ElementSerializerDescriptor.Serializer;
	for (uint32 ElementIt = 0, ElementEndIt = SourceArray.ElementCount; ElementIt < ElementEndIt; ++ElementIt)
	{
		ElementSerializer->CloneDynamicState(Context, ElementArgs);
		ElementArgs.Source += ElementSize;
		ElementArgs.Target += ElementSize;
	}
}

// -----------------------------------------------------------------------------
// FreeDynamicState / FreeDynamicStateInternal —— 释放整段量化态内存
// -----------------------------------------------------------------------------
// 1. 若元素含 dynamic state，逐元素 FreeDynamicState；
// 2. Free 整段 ElementStorage；
// 3. 清零 capacity/count/storage 三字段。
void FArrayPropertyNetSerializer::FreeDynamicState(FNetSerializationContext& Context, const FNetFreeDynamicStateArgs& Args)
{
	return FreeDynamicStateInternal(Context, *reinterpret_cast<QuantizedType*>(Args.Source), static_cast<const ConfigType*>(Args.NetSerializerConfig));
}

void FArrayPropertyNetSerializer::FreeDynamicStateInternal(FNetSerializationContext& Context, QuantizedType& Array, const ConfigType* Config)
{
	const FReplicationStateDescriptor* ElementStateDescriptor = Config->StateDescriptor;

	if (EnumHasAnyFlags(ElementStateDescriptor->Traits, EReplicationStateTraits::HasDynamicState))
	{
		const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];
		const FNetSerializer* ElementSerializer = ElementSerializerDescriptor.Serializer;
		const uint32 ElementSize = ElementStateDescriptor->InternalSize;

		FNetFreeDynamicStateArgs ElementArgs;
		ElementArgs.Version = 0;
		ElementArgs.NetSerializerConfig = ElementSerializerDescriptor.SerializerConfig;
		ElementArgs.Source = NetSerializerValuePointer(Array.ElementStorage);

		for (uint32 ElementIt = 0, ElementEndIt = Array.ElementCount; ElementIt < ElementEndIt; ++ElementIt)
		{
			ElementSerializer->FreeDynamicState(Context, ElementArgs);
			ElementArgs.Source += ElementSize;
		}
	}

	Context.GetInternalContext()->Free((void*)(Array.ElementStorage));

	// Clear allocation info
	Array.ElementCapacityCount = 0;
	Array.ElementCount = 0;
	Array.ElementStorage = 0;
}

// -----------------------------------------------------------------------------
// GrowDynamicStateInternal —— 容量增长（重新分配 + memcpy 旧元素 + memzero 新位）
// -----------------------------------------------------------------------------
// 实现策略简单：直接用 NewElementCount 一比一分配（无指数扩张），随后把旧
// 元素内容复制过来，新增位置由 Memzero 保证初始零态。
void FArrayPropertyNetSerializer::GrowDynamicStateInternal(FNetSerializationContext& Context, QuantizedType& Array, const ConfigType* Config, uint16 NewElementCount)
{
	checkSlow(NewElementCount > Array.ElementCapacityCount);

	const FReplicationStateDescriptor* ElementStateDescriptor = Config->StateDescriptor;
	const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];
	const FNetSerializer* ElementSerializer = ElementSerializerDescriptor.Serializer;
	const SIZE_T ElementSize = ElementStateDescriptor->InternalSize;
	const SIZE_T ElementAlignment = ElementStateDescriptor->InternalAlignment;

	void* NewElementStorage = Context.GetInternalContext()->Alloc(ElementSize*NewElementCount, ElementAlignment);
	FMemory::Memzero(NewElementStorage, ElementSize*NewElementCount);
	// We only need to copy the contents of the used elements, not the entire capacity.
	FMemory::Memcpy(NewElementStorage, Array.ElementStorage, ElementSize*Array.ElementCount);
	Context.GetInternalContext()->Free(Array.ElementStorage);

	Array.ElementCapacityCount = NewElementCount;
	Array.ElementCount = NewElementCount;
	Array.ElementStorage = NewElementStorage;
}

// -----------------------------------------------------------------------------
// ShrinkDynamicStateInternal —— 容量缩减（仅减元素数 + 释放尾段元素 dynamic state）
// -----------------------------------------------------------------------------
// 注意此处只减 ElementCount，不缩 Storage 实际分配 —— 这是性能优化：
// 同一对象常会反复增减元素，复用已分配 storage 避免反复 alloc/free。
void FArrayPropertyNetSerializer::ShrinkDynamicStateInternal(FNetSerializationContext& Context, QuantizedType& Array, const ConfigType* Config, uint16 NewElementCount)
{
	const FReplicationStateDescriptor* ElementStateDescriptor = Config->StateDescriptor;

	// Free memory allocated per element.
	if (EnumHasAnyFlags(ElementStateDescriptor->Traits, EReplicationStateTraits::HasDynamicState))
	{
		const FReplicationStateMemberSerializerDescriptor& ElementSerializerDescriptor = ElementStateDescriptor->MemberSerializerDescriptors[0];
		const FNetSerializer* ElementSerializer = ElementSerializerDescriptor.Serializer;
		const uint32 ElementSize = ElementStateDescriptor->InternalSize;

		FNetFreeDynamicStateArgs ElementArgs;
		ElementArgs.Version = 0;
		ElementArgs.NetSerializerConfig = ElementSerializerDescriptor.SerializerConfig;
		ElementArgs.Source = NetSerializerValuePointer(Array.ElementStorage) + NewElementCount*ElementSize;

		for (uint32 ElementIt = NewElementCount, ElementEndIt = Array.ElementCount; ElementIt < ElementEndIt; ++ElementIt)
		{
			ElementSerializer->FreeDynamicState(Context, ElementArgs);
			ElementArgs.Source += ElementSize;
		}
	}

	Array.ElementCount = NewElementCount;
}

// -----------------------------------------------------------------------------
// AdjustArraySize —— 容量调整总入口
// -----------------------------------------------------------------------------
// 4 种分支：
//   1) NewCount == 0      → FreeDynamicStateInternal（彻底释放）
//   2) NewCount < OldCount → ShrinkDynamicStateInternal（只缩元素数）
//   3) NewCount > Capacity → GrowDynamicStateInternal（扩容）
//   4) Capacity 内增长     → 仅 Memzero 新位 + 修改 ElementCount，零分配
void FArrayPropertyNetSerializer::AdjustArraySize(FNetSerializationContext& Context, QuantizedType& Array, const ConfigType* Config, uint16 NewElementCount)
{
	if (NewElementCount < Array.ElementCount)
	{
		// If the array is empty we free everything
		if (NewElementCount == 0)
		{
			FreeDynamicStateInternal(Context, Array, Config);
		}
		// Otherwise we shrink it, freeing allocations made by individual elements
		else
		{
			ShrinkDynamicStateInternal(Context, Array, Config, NewElementCount);
		}
	}
	else if (NewElementCount > Array.ElementCapacityCount)
	{
		GrowDynamicStateInternal(Context, Array, Config, NewElementCount);
	}
	// If element count is within the allocated capacity we just change the number of elements
	else
	{
		// Zero out data just to be sure, to always give new elements the same initial state.
		if (NewElementCount > Array.ElementCount)
		{
			const FReplicationStateDescriptor* ElementStateDescriptor = Config->StateDescriptor;
			const SIZE_T ElementSize = ElementStateDescriptor->InternalSize;
			void* ElementsToZeroOut = (void*)(NetSerializerValuePointer(Array.ElementStorage) + (Array.ElementCount * ElementSize));

			FMemory::Memzero(ElementsToZeroOut, ElementSize * (NewElementCount - Array.ElementCount));
		}
		Array.ElementCount = NewElementCount;
	}
}

}
