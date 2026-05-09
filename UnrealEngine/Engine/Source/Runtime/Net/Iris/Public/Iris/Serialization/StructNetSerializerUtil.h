// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// StructNetSerializerUtil.h —— 一次性"读/写一个 UScriptStruct 值"的工具 API
// -----------------------------------------------------------------------------
// 用途：在自定义 NetSerializer / FastArray / 多态 / InstancedStruct 等场景下，
// 当只想一次性把"某个 struct 实例"按其 ReplicationStateDescriptor 描述写入/
// 读取比特流（一步完成 Quantize + Serialize 或 Deserialize + Dequantize），
// 不必关心中间临时量化态缓冲的对齐与生命周期。
//
// 实现要点（详见 .cpp）：
//   * 内部使用 FNetSerializerAlignedStorage 自动按 Descriptor->InternalSize/
//     InternalAlignment 临时分配 quantized buffer；
//   * 一次性调用 FStructNetSerializer 的 Quantize→Serialize（写）或
//     Deserialize→Dequantize（读）；
//   * 完成后自动 Free。
//
// 适用场景：写大型 RPC 参数 / Polymorphic struct payload / Iris Pkt 内嵌 struct。
// =============================================================================

#pragma once

#include "Iris/Serialization/NetSerializer.h"

namespace UE::Net
{
	struct FReplicationStateDescriptor;
	// Write a Struct value to the provided context using the provided Descriptor.
	// 把"InValue 处的 SourceType（真实 struct 内存）"按 Descriptor 描述
	// 量化并写入 Context.BitStreamWriter；一步完成 Quantize→Serialize。
	IRISCORE_API void WriteStruct(FNetSerializationContext& Context, NetSerializerValuePointer InValue, const FReplicationStateDescriptor* Descriptor);

	// Read a Struct value to OutValue from the provided context using the provided Descriptor.
	// 反向：从 Context.BitStreamReader 读取并反量化到 OutValue 处的 SourceType。
	// 一步完成 Deserialize→Dequantize。
	IRISCORE_API void ReadStruct(FNetSerializationContext& Context, NetSerializerValuePointer OutValue, const FReplicationStateDescriptor* Descriptor);
}
