// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// IrisPackageMapExportUtil.cpp
// ---------------------------------------------------------------------------------------------
// FIrisPackageMapExportsUtil 的实现。把 FIrisPackageMapExports（来自 PackageMap 劫持）
// 变成"标准 NetSerializer 量化态" FIrisPackageMapExportsQuantizedType，复用主流接口收发。
//
// 数据流概览：
//   写侧：
//     UIrisObjectReferencePackageMap  (劫持老 NetSerialize)
//        ↓ 捕获到 References / Names / Tokens
//     FIrisPackageMapExports
//        ↓ Quantize
//     FIrisPackageMapExportsQuantizedType (POD, 三段 ArrayStorage)
//        ↓ Serialize（位流上写 References/Names；Tokens 转交 ExportContext）
//     BitStream
//
//   读侧：与上述完全对偶。
//
// 三段数据的差异（再次强调）：
//   - References / Names：真正参与位流读写、Quantize/Dequantize；
//   - NetTokens          ：只在 Quantize 阶段拷贝一份；Serialize 阶段不写流，
//                          而是 AddPendingExport 让外层导出框架统一发送。
// =============================================================================================

#include "Iris/Serialization/IrisPackageMapExportUtil.h"

#include "Iris/Core/IrisLog.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetSerializers.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetReferenceCollector.h"
#include "Iris/Serialization/NetSerializerArrayStorage.h"
#include "Iris/Serialization/IrisObjectReferencePackageMap.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Net/Core/NetToken/NetToken.h"
#include "Net/Core/Trace/NetTrace.h"
#include "NetExportContext.h"

namespace UE::Net
{

// 二级 NetSerializer 静态指针：在第一次使用前由静态初始化器一次性绑定。
const FNetSerializer* FIrisPackageMapExportsUtil::ObjectNetSerializer = &UE_NET_GET_SERIALIZER(FObjectNetSerializer);
const FNetSerializer* FIrisPackageMapExportsUtil::NameNetSerializer = &UE_NET_GET_SERIALIZER(FNameAsNetTokenNetSerializer);
//const FNetSerializer* FIrisPackageMapExportsUtil::NameNetSerializer = &UE_NET_GET_SERIALIZER(FNameNetSerializer);

// ------------------------------------------------------------------------------------------
// Serialize —— 写侧
// ------------------------------------------------------------------------------------------
// 三段处理：
//   1. ObjectReferences：先写 1 bit 表示是否有，再写数量，再逐个调用 ObjectNetSerializer.Serialize。
//   2. Names           ：与对象引用结构完全相同，差别只是二级 Serializer。
//   3. NetTokens       ：不写位流；遍历后 AddPendingExport 到 ExportContext，由外层统一处理。
// 使用 UE_NET_TRACE_SCOPE 在网络追踪里把这两段切出独立 sub-scope，调试时清晰可见。
// ------------------------------------------------------------------------------------------
void FIrisPackageMapExportsUtil::Serialize(FNetSerializationContext& Context, const QuantizedType& Value)
{
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	// If we have any references, export them!
	{
		UE_NET_TRACE_SCOPE(ObjectReferences, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
		const uint32 NumReferences = Value.ObjectReferenceStorage.Num();
		// 用 1 bit 表示"是否非空"，零开销跳过空段。
		if (Writer->WriteBool(NumReferences != 0))
		{
			UE::Net::WritePackedUint32(Writer, NumReferences);		
			FObjectNetSerializerConfig ObjectSerializerConfig;
			for (const FObjectNetSerializerQuantizedReferenceStorage& Ref : MakeArrayView(Value.ObjectReferenceStorage.GetData(), NumReferences))
			{
				FNetSerializeArgs ObjectArgs;
				ObjectArgs.NetSerializerConfig = &ObjectSerializerConfig;
				ObjectArgs.Source = NetSerializerValuePointer(&Ref);

				ObjectNetSerializer->Serialize(Context, ObjectArgs);
			}
		}
	}

	// If we have any names, export them!
	{
		UE_NET_TRACE_SCOPE(Names, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
		const uint32 NumNames = Value.NameStorage.Num();
		if (Writer->WriteBool(NumNames != 0))
		{
			UE::Net::WritePackedUint32(Writer, NumNames);
			FNetSerializerConfig NameSerializerConfig;
			for (const QuantizedType::FQuantizedName& QuantizedName : MakeArrayView(Value.NameStorage.GetData(), NumNames))
			{
				FNetSerializeArgs NameArgs;
				NameArgs.NetSerializerConfig = &NameSerializerConfig;
				NameArgs.Source = NetSerializerValuePointer(&QuantizedName);

				NameNetSerializer->Serialize(Context, NameArgs);
			}
		}
	}

	// We now serialize NetTokens directly in the data but we still need to append exports.
	// NetToken 已经在主流序列化中直接出现在位流里，但仍需把它们登记到 pending export，
	// 以便对端首次见到该 Token 时能拿到对应的字面量数据。
	if (UE::Net::Private::FNetExportContext* ExportContext = Context.GetExportContext())
	{
		for (const FNetToken& NetToken : MakeArrayView(Value.NetTokenStorage.GetData(), Value.NetTokenStorage.Num()))
		{
			ExportContext->AddPendingExport(NetToken);
		}
	}
}

// ------------------------------------------------------------------------------------------
// Deserialize —— 读侧
// ------------------------------------------------------------------------------------------
// 与 Serialize 对偶：读 bool 判空，读数量，越界保护，AdjustSize 调整存储再逐元素 Deserialize。
// 收尾处把 NetTokenStorage 显式 Free——读侧不发送 pending Token，因此它必须为空。
// ------------------------------------------------------------------------------------------
void FIrisPackageMapExportsUtil::Deserialize(FNetSerializationContext& Context, QuantizedType& Value)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	// Read any object references
	{
		UE_NET_TRACE_SCOPE(ObjectReferences, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
		const bool bHasObjectReferences = Reader->ReadBool();
		if (bHasObjectReferences)
		{
			const uint32 NumReferences = UE::Net::ReadPackedUint32(Reader);

			// 防御：恶意/损坏对端发巨大数组。直接打 Error 标志位短路后续解码。
			if (NumReferences > MaxExports)
			{
				UE_LOG(LogIris, Error, TEXT("FIrisPackageMapExportsUtil::Received too many object reference exports %u > max:%u"), NumReferences, MaxExports);
				Context.SetError(GNetError_ArraySizeTooLarge);
				return;
			}

			Value.ObjectReferenceStorage.AdjustSize(Context, NumReferences);

			FObjectNetSerializerConfig ObjectSerializerConfig;
			for (FObjectNetSerializerQuantizedReferenceStorage& Ref : MakeArrayView(Value.ObjectReferenceStorage.GetData(), Value.ObjectReferenceStorage.Num()))
			{
				FNetDeserializeArgs ObjectArgs;
				ObjectArgs.NetSerializerConfig = &ObjectSerializerConfig;
				ObjectArgs.Target = NetSerializerValuePointer(&Ref);

				ObjectNetSerializer->Deserialize(Context, ObjectArgs);
			}
		}
		else
		{
			// 空段：把可能残留的存储释放掉（Quantized 状态可能被复用）。
			Value.ObjectReferenceStorage.Free(Context);
		}
	}

	// Read any exported names
	{
		UE_NET_TRACE_SCOPE(Names, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
		const bool bHasNames = Reader->ReadBool();
		if (bHasNames)
		{
			const uint32 NumNames = UE::Net::ReadPackedUint32(Reader);

			if (NumNames > MaxExports)
			{
				UE_LOG(LogIris, Error, TEXT("FIrisPackageMapExportsUtil::Received too many name exports %u > max:%u"), NumNames, MaxExports);
				Context.SetError(GNetError_ArraySizeTooLarge);
				return;
			}

			Value.NameStorage.AdjustSize(Context, NumNames);

			FNetSerializerConfig NameSerializerConfig;
			for (QuantizedType::FQuantizedName& QuantizedName : MakeArrayView(Value.NameStorage.GetData(), Value.NameStorage.Num()))
			{
				FNetDeserializeArgs NameArgs;
				NameArgs.NetSerializerConfig = &NameSerializerConfig;
				NameArgs.Target = NetSerializerValuePointer(&QuantizedName);

				NameNetSerializer->Deserialize(Context, NameArgs);
			}
		}
		else
		{
			Value.NameStorage.Free(Context);
		}
	}

	// 读侧不携带 pending NetToken 列表——发送侧已经把对应字面量交给外层 Export 框架了。
	Value.NetTokenStorage.Free(Context);
}

// ------------------------------------------------------------------------------------------
// Quantize
// ------------------------------------------------------------------------------------------
// 把 (FIrisPackageMapExports + pending Token 列表) 量化到 QuantizedType。
//   - References / Names：通过二级 NetSerializer 的 Quantize 逐个转换；
//   - NetTokens         ：直接拷贝（Token 本身已经是发送态）。
// ------------------------------------------------------------------------------------------
void FIrisPackageMapExportsUtil::Quantize(FNetSerializationContext& Context, const UE::Net::FIrisPackageMapExports& PackageMapExports, TArrayView<const UE::Net::FNetToken> NetTokensPendingExport, QuantizedType& Value)
{
	// Quantize captured references
	{
		const FIrisPackageMapExports::FObjectReferenceArray& ObjectReferences = PackageMapExports.References;
		const uint32 NumObjectReferences = ObjectReferences.Num();
		Value.ObjectReferenceStorage.AdjustSize(Context, NumObjectReferences);
		if (NumObjectReferences > 0)
		{
			FObjectNetSerializerConfig ObjectNetSerializerConfig;
			const TObjectPtr<UObject>* SourceReferences = ObjectReferences.GetData();
			FObjectNetSerializerQuantizedReferenceStorage* TargetReferences = Value.ObjectReferenceStorage.GetData();
			for (uint32 ReferenceIndex = 0; ReferenceIndex < NumObjectReferences; ++ReferenceIndex)
			{
				FNetQuantizeArgs ObjectArgs;
				ObjectArgs.NetSerializerConfig = &ObjectNetSerializerConfig;
				ObjectArgs.Source = NetSerializerValuePointer(SourceReferences + ReferenceIndex);
				ObjectArgs.Target = NetSerializerValuePointer(TargetReferences + ReferenceIndex);

				ObjectNetSerializer->Quantize(Context, ObjectArgs);
			}
		}
	}

	// Quantize captured names
	{
		const FIrisPackageMapExports::FNameArray& Names = PackageMapExports.Names;
		const uint32 NumNames = Names.Num();
		Value.NameStorage.AdjustSize(Context, NumNames);
		if (NumNames > 0)
		{
			FNetSerializerConfig NameNetSerializerConfig;
			const FName* SourceNames = Names.GetData();
			QuantizedType::FQuantizedName* TargetNames = Value.NameStorage.GetData();
			for (uint32 ReferenceIndex = 0; ReferenceIndex < NumNames; ++ReferenceIndex)
			{
				FNetQuantizeArgs NameArgs;
				NameArgs.NetSerializerConfig = &NameNetSerializerConfig;
				NameArgs.Source = NetSerializerValuePointer(SourceNames + ReferenceIndex);
				NameArgs.Target = NetSerializerValuePointer(TargetNames + ReferenceIndex);

				NameNetSerializer->Quantize(Context, NameArgs);
			}
		}
	}

	// Just store captured NetTokenExports they will be added as pending exports during serialization.
	// NetToken 直接拷贝即可——它已经是发送态。等 Serialize 阶段再推到 ExportContext。
	{
		const uint32 NumNetTokens = NetTokensPendingExport.Num();
		Value.NetTokenStorage.AdjustSize(Context, NumNetTokens);
		FNetToken* TargetTokens = Value.NetTokenStorage.GetData();
		for (uint32 Index = 0; Index < NumNetTokens; ++Index)
		{
			TargetTokens[Index] = NetTokensPendingExport[Index];
		}
	}
}

// ------------------------------------------------------------------------------------------
// FreeDynamicState (无 Context 版本)
// ------------------------------------------------------------------------------------------
// 当调用方手头没有有效 Context（如析构链晚期）时使用。构造一个 stub Context+InternalContext，
// 让 ArrayStorage 退化使用 FMemory::Free 完成释放。
// ------------------------------------------------------------------------------------------
void FIrisPackageMapExportsUtil::FreeDynamicState(QuantizedType& Value)
{
	FNetSerializationContext Context;
	Private::FInternalNetSerializationContext InternalContext;
	Context.SetInternalContext(&InternalContext);

	// Clear all info
	Value.ObjectReferenceStorage.Free(Context);
	Value.NameStorage.Free(Context);
	Value.NetTokenStorage.Free(Context);
}

// ------------------------------------------------------------------------------------------
// Dequantize
// ------------------------------------------------------------------------------------------
// 把 QuantizedType 还原回 FIrisPackageMapExports：
//   - 注意 NetTokenStorage 不参与 Dequantize（它只在写侧需要传播到 ExportContext，
//     读侧从主流位流直接拿到 Token，已经能用）。
// ------------------------------------------------------------------------------------------
void FIrisPackageMapExportsUtil::Dequantize(FNetSerializationContext& Context, const QuantizedType& Source, UE::Net::FIrisPackageMapExports& PackageMapExports)
{
	// References
	{
		UE::Net::FIrisPackageMapExports::FObjectReferenceArray& ObjectReferences = PackageMapExports.References;
		const uint32 NumObjectReferences = Source.ObjectReferenceStorage.Num();
		if (NumObjectReferences > 0U)
		{		
			ObjectReferences.SetNumUninitialized(NumObjectReferences);

			FObjectNetSerializerConfig ObjectNetSerializerConfig;
			const FObjectNetSerializerQuantizedReferenceStorage* SourceReferences = Source.ObjectReferenceStorage.GetData();
			TObjectPtr<UObject>* TargetReferences = ObjectReferences.GetData();
			for (uint32 ReferenceIndex = 0; ReferenceIndex < NumObjectReferences; ++ReferenceIndex)
			{
				FNetDequantizeArgs ObjectArgs;
				ObjectArgs.NetSerializerConfig = &ObjectNetSerializerConfig;
				ObjectArgs.Source = NetSerializerValuePointer(SourceReferences + ReferenceIndex);
				ObjectArgs.Target = NetSerializerValuePointer(TargetReferences + ReferenceIndex);

				ObjectNetSerializer->Dequantize(Context, ObjectArgs);
			}
		}
	}

	// Names
	{
		UE::Net::FIrisPackageMapExports::FNameArray& Names = PackageMapExports.Names;

		const uint32 NumNames = Source.NameStorage.Num();
		if (NumNames > 0U)
		{		
			Names.SetNumUninitialized(NumNames);

			FNetSerializerConfig NameNetSerializerConfig;
			const QuantizedType::FQuantizedName* SourceNames = Source.NameStorage.GetData();
			FName* TargetNames = Names.GetData();
			for (uint32 ReferenceIndex = 0; ReferenceIndex < NumNames; ++ReferenceIndex)
			{
				FNetDequantizeArgs NameArgs;
				NameArgs.NetSerializerConfig = &NameNetSerializerConfig;
				NameArgs.Source = NetSerializerValuePointer(SourceNames + ReferenceIndex);
				NameArgs.Target = NetSerializerValuePointer(TargetNames + ReferenceIndex);

				NameNetSerializer->Dequantize(Context, NameArgs);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------
// IsEqual
// ------------------------------------------------------------------------------------------
// 长度三段全部相等，且 References / Names 用 memcmp 比较内存（POD quantized）。
// 注意：此处用 sizeof(FNetObjectReference) 计算 References 比较字节数——这是早期实现的
// 简化，前提是 FObjectNetSerializerQuantizedReferenceStorage 的存储布局与之等价。
// 改动相关结构时需要同步检查。
// ------------------------------------------------------------------------------------------
bool FIrisPackageMapExportsUtil::IsEqual(FNetSerializationContext& Context, const QuantizedType& Value0, const QuantizedType& Value1)
{
	if ((Value0.ObjectReferenceStorage.Num() != Value1.ObjectReferenceStorage.Num()) || (Value0.NameStorage.Num() != Value1.NameStorage.Num()) || (Value0.NetTokenStorage.Num() != Value1.NetTokenStorage.Num()))
	{
		return false;
	}

	if (Value0.ObjectReferenceStorage.Num() > 0 && FMemory::Memcmp(Value0.ObjectReferenceStorage.GetData(), Value1.ObjectReferenceStorage.GetData(), sizeof(FNetObjectReference) * Value0.ObjectReferenceStorage.Num()) != 0)
	{
		return false;
	}

	if (Value0.NameStorage.Num() > 0 && FMemory::Memcmp(Value0.NameStorage.GetData(), Value1.NameStorage.GetData(), sizeof(FName) * Value0.NameStorage.Num()) != 0)
	{
		return false;
	}

	return true;
}

// ------------------------------------------------------------------------------------------
// CloneDynamicState
// ------------------------------------------------------------------------------------------
// 深拷贝三段 ArrayStorage。Storage 的 Clone 内部会处理重分配 + 元素拷贝。
// ------------------------------------------------------------------------------------------
void FIrisPackageMapExportsUtil::CloneDynamicState(FNetSerializationContext& Context, QuantizedType& Target, const QuantizedType& Source)
{
	Target.ObjectReferenceStorage.Clone(Context, Source.ObjectReferenceStorage);
	Target.NameStorage.Clone(Context, Source.NameStorage);
	Target.NetTokenStorage.Clone(Context, Source.NetTokenStorage);
}

// ------------------------------------------------------------------------------------------
// FreeDynamicState (有 Context 版本)
// ------------------------------------------------------------------------------------------
// 用 Context 中的 InternalContext::Free 进行回收，可走自定义内存池。
// ------------------------------------------------------------------------------------------
void FIrisPackageMapExportsUtil::FreeDynamicState(FNetSerializationContext& Context, QuantizedType& Value)
{
	// Clear all info
	Value.ObjectReferenceStorage.Free(Context);
	Value.NameStorage.Free(Context);
	Value.NetTokenStorage.Free(Context);
}

// ------------------------------------------------------------------------------------------
// CollectNetReferences
// ------------------------------------------------------------------------------------------
// 把 ObjectReferenceStorage 中的每个引用上报给外层 Collector。
// 关键 reinterpret_cast：把 Object NetSerializer 的 quantized 内层结构当作
// FQuantizedObjectReference 使用——这要求 Storage 的内存布局头部即 FQuantizedObjectReference，
// 是 ObjectNetSerializer 与本 util 之间的"内存契约"。改动 ObjectNetSerializer quantized
// 结构时务必同步检查这里。
// ------------------------------------------------------------------------------------------
void FIrisPackageMapExportsUtil::CollectNetReferences(FNetSerializationContext& Context, const QuantizedType& Value, const FNetSerializerChangeMaskParam& ChangeMaskInfo, FNetReferenceCollector& Collector)
{
	const FNetReferenceInfo ReferenceInfo(FNetReferenceInfo::EResolveType::ResolveOnClient);	
	for (const FObjectNetSerializerQuantizedReferenceStorage& Ref : MakeArrayView(Value.ObjectReferenceStorage.GetData(), Value.ObjectReferenceStorage.Num()))
	{
		// 从 Storage 头部 reinterpret 取 FQuantizedObjectReference——和 ObjectNetSerializer
		// 内部布局形成约定，必须与之保持同步。
		const FQuantizedObjectReference& InternalReference = *reinterpret_cast<const FQuantizedObjectReference*>(&Ref.Storage);

		Collector.Add(ReferenceInfo, InternalReference, ChangeMaskInfo);
	}
}

// ------------------------------------------------------------------------------------------
// Validate
// ------------------------------------------------------------------------------------------
// 数组长度上限保护：>MaxExports 视为非法。
// 注意：NetTokenStorage 不在 Validate 检查范围——它在 Quantize 阶段由调用方控制。
// ------------------------------------------------------------------------------------------
bool FIrisPackageMapExportsUtil::Validate(FNetSerializationContext& Context, const QuantizedType& SourceValue)
{
	if ((SourceValue.ObjectReferenceStorage.Num() > MaxExports) || (SourceValue.NameStorage.Num() > MaxExports))
	{
		return false;
	}
	return true;
}

}
