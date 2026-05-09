// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// StructNetSerializerUtil.cpp —— WriteStruct / ReadStruct 实现
// -----------------------------------------------------------------------------
// 这两个工具把"一次性写/读一整个 struct 值"封装成无状态调用。
// 流程概览：
//   WriteStruct: 真实内存 InValue → [Quantize] → 临时量化缓冲 → [Serialize] → 比特流
//   ReadStruct : 比特流 → [Deserialize] → 临时量化缓冲 → [Dequantize] → 真实内存 OutValue
//
// 注意：
//   * 仅适用于"无 dynamic state 也行"的简单 struct，在 Quantize 后没有
//     特别处理 dynamic 部分清理（因为临时缓冲 Free 走 AlignedStorage 会
//     释放整段内存，但内含的 dynamic state 不会被 FreeDynamicState 单独
//     回收）。如 struct 含 dynamic state（如 TArray 元素），临时缓冲只
//     适合短生命周期、立即写出/读完即抛的场景。
//   * 用于多态/InstancedStruct 等"先快速写个 struct，再继续写其它内容"
//     的复合协议；不用于持久存档。
// =============================================================================

#include "Iris/Serialization/StructNetSerializerUtil.h"

#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/NetSerializerArrayStorage.h"

namespace UE::Net
{
	// 一次性写：真实 struct 内存 → 比特流。
	void WriteStruct(FNetSerializationContext& Context, NetSerializerValuePointer InValue, const FReplicationStateDescriptor* Descriptor)
	{
		// Descriptor 必须有效，否则我们无法知道 layout，直接早退并 ensure。
		if (!ensureAlwaysMsgf(Descriptor, TEXT("Replication State Descriptor cannot be null")))
		{
			return;
		}
		// 取 FStructNetSerializer 的全局 singleton（递归 dispatch 容器）。
		const FNetSerializer& Serializer = UE_NET_GET_SERIALIZER(FStructNetSerializer);
		FStructNetSerializerConfig StructConfig;
		StructConfig.StateDescriptor = Descriptor;

		// 临时量化缓冲：按 Descriptor 描述的 InternalSize/InternalAlignment 申请。
		// FNetSerializerAlignedStorage 走 InternalContext 分配器，析构由 Free 显式调用。
		FNetSerializerAlignedStorage QuantizedStorage;
		QuantizedStorage.AdjustSize(Context, StructConfig.StateDescriptor->InternalSize, StructConfig.StateDescriptor->InternalAlignment);
		{
			// 第一步：Quantize —— 把真实内存转成"网络归一态"（统一字节序、压缩浮点等）。
			FNetQuantizeArgs QuantizeArgs;
			QuantizeArgs.Source = InValue;
			QuantizeArgs.Target = NetSerializerValuePointer(QuantizedStorage.GetData());
			QuantizeArgs.NetSerializerConfig = NetSerializerConfigParam(&StructConfig);
			Serializer.Quantize(Context, QuantizeArgs);
		}

		{
			// 第二步：Serialize —— 把量化态写入 Context.BitStreamWriter。
			FNetSerializeArgs SerializeArgs;
			SerializeArgs.Version = Serializer.Version;
			SerializeArgs.Source = NetSerializerValuePointer(QuantizedStorage.GetData());
			SerializeArgs.NetSerializerConfig = NetSerializerConfigParam(&StructConfig);
			Serializer.Serialize(Context, SerializeArgs);
		}
		// 释放临时量化缓冲。注意：若 struct 含 dynamic state，请改用更完整的
		// CloneDynamicState/FreeDynamicState 流程；此 Util 只处理简单情形。
		QuantizedStorage.Free(Context);
	}
	
	// 一次性读：比特流 → 真实 struct 内存。
	void ReadStruct(FNetSerializationContext& Context, NetSerializerValuePointer OutValue, const FReplicationStateDescriptor* Descriptor)
	{
		if (!ensureAlwaysMsgf(Descriptor, TEXT("Replication State Descriptor cannot be null")))
		{
			return;
		}
		const FNetSerializer& Serializer = UE_NET_GET_SERIALIZER(FStructNetSerializer);
		FStructNetSerializerConfig StructConfig;
		StructConfig.StateDescriptor = Descriptor;
		// 临时量化缓冲，与 WriteStruct 对称。
		FNetSerializerAlignedStorage QuantizedStorage;
		QuantizedStorage.AdjustSize(Context, StructConfig.StateDescriptor->InternalSize, StructConfig.StateDescriptor->InternalAlignment);
		{
			// 第一步：Deserialize —— 从 BitStreamReader 读出量化态。
			FNetDeserializeArgs SerializeArgs;
			SerializeArgs.Version = Serializer.Version;
			SerializeArgs.Target = NetSerializerValuePointer(QuantizedStorage.GetData());
			SerializeArgs.NetSerializerConfig = NetSerializerConfigParam(&StructConfig);
			Serializer.Deserialize(Context, SerializeArgs);
		}
		
		{
			// 第二步：Dequantize —— 把量化态还原为真实 struct 内存（OutValue）。
			FNetDequantizeArgs DequantizeArgs;
			DequantizeArgs.Source = NetSerializerValuePointer(QuantizedStorage.GetData());
			DequantizeArgs.Target = OutValue;
			DequantizeArgs.NetSerializerConfig = NetSerializerConfigParam(&StructConfig);
			Serializer.Dequantize(Context, DequantizeArgs);
		}
		
		QuantizedStorage.Free(Context);
	}
}
