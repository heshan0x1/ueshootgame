// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// PolymorphicNetSerializerImpl.h —— TPolymorphic{Struct,ArrayStruct}NetSerializerImpl 模板
// -----------------------------------------------------------------------------
// 这是"多态 struct 序列化"的 SFINAE 模板实现。两类模板：
//
//   * TPolymorphicStructNetSerializerImpl<Source, Item, GetItem>
//     用于"持有单个 TSharedPtr<Item> 的 Source 类型"，例如 FGameplayEffectContextHandle。
//     模板参数：
//       - Source : 外层 struct/类（如 FGameplayEffectContextHandle）
//       - Item   : 多态基类（如 FGameplayEffectContext）
//       - GetItem: 函数指针，从 Source& 中拿到 TSharedPtr<Item>& 引用
//
//   * TPolymorphicArrayStructNetSerializerImpl<Source, ArrayItem, GetArray, SetArrayNum>
//     用于"持有 TArray<TSharedPtr<ArrayItem>> 的 Source"，例如 FGameplayAbilityTargetDataHandle。
//     模板参数：
//       - GetArray   : 取数组（TArrayView<TSharedPtr<ArrayItem>>）
//       - SetArrayNum: 设置数组长度（Resize）
//     报文额外约束：ArrayItemBits = 8，最多 255 个元素。
//
// 报文协议（单值）：
//   [1 bit isValidType] ([5 bit TypeIndex] [按对应 Descriptor 序列化的内容])?
//
// 报文协议（数组）：
//   [8 bit NumItems] (元素 0) (元素 1) ... 每元素同上单值格式
//
// Quantize/Dequantize 流程（单值）：
//   Quantize  : Item.GetScriptStruct → Cache.GetTypeIndex → 找 Descriptor
//                → InternalContext.Alloc(Descriptor->InternalSize) → FReplicationStateOperations::Quantize
//   Dequantize: GetTypeInfo(TypeIndex) → 用全局 Malloc + InitializeStruct 创建外部 SourceItemType
//                → FReplicationStateOperations::Dequantize → MakeShareable 包成 TSharedPtr
//
// 关键 trait：
//   * bHasDynamicState           = true（QuantizedType 含 StructData 堆分配）
//   * bIsForwardingSerializer    = true（自身只 dispatch，无 raw 数据）
//   * bHasCustomNetReference     = true（不知道 inner type 是否含引用，需自定义收集）
//
// 重要约束（注释中 BIG DISCLAIMER 部分）：
//   * 模板原本为 RPC 设计（非 replicated property），用 TSharedPtr 持有；
//   * 若用于 replicated property，SourceType 必须：
//     1. 重载 operator= 实现深拷贝（IsEqual 才正确）
//     2. 重载 operator== 比较实例数据（避免拿到 TSharedRef 的"指针相等"误判）
//     3. 在 TStructOpsTypeTraits 标记 WithCopy + WithIdenticalViaEquality
// =============================================================================
#pragma once

#include "NetSerializer.h"
#include "PolymorphicNetSerializer.h"

#include "Iris/Core/IrisLog.h"
#include "Iris/Core/NetObjectReference.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/ReplicationState/ReplicationStateDescriptor.h"
#include "Iris/ReplicationSystem/ReplicationOperations.h"
#include "Net/Core/Trace/NetTrace.h"

// NOTE: This file should not be included in a header
// 因为含模板实现 + 强依赖 ReplicationOperations 内部 API，避免被传染。

namespace UE::Net
{

class FNetReferenceCollector;
extern IRISCORE_API const FName NetError_PolymorphicStructNetSerializer_InvalidStructType;

}

namespace UE::Net::Private
{

// Wrapper to allow access to the internal context
// 让模板 Impl 通过 protected 继承访问 Iris 私有的 InternalContext 分配器与
// ReplicationStateOperationsInternal API（避免模板直接 #include 私有头）。
struct FPolymorphicStructNetSerializerInternal
{
protected:
	// 调用 Context.GetInternalContext()->Alloc/Free —— 走 Iris 的 per-frame allocator。
	IRISCORE_API static void* Alloc(FNetSerializationContext& Context, SIZE_T Size, SIZE_T Alignment);
	IRISCORE_API static void Free(FNetSerializationContext& Context, void* Ptr);
	// 转发到 FReplicationStateOperationsInternal::CollectReferences/CloneQuantizedState。
	IRISCORE_API static void CollectReferences(FNetSerializationContext& Context, UE::Net::FNetReferenceCollector& Collector, const FNetSerializerChangeMaskParam& OuterChangeMaskInfo, const uint8* RESTRICT SrcInternalBuffer,  const FReplicationStateDescriptor* Descriptor);
	IRISCORE_API static void CloneQuantizedState(FNetSerializationContext& Context, uint8* RESTRICT DstInternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor);
};

}

namespace UE::Net
{

/**
  * TPolymorphicStructNetSerializerImpl
  *
  * Helper to implement serializers that requires dynamic polymorphism.
  * It can either be used to declare a typed serializer or be used as an internal helper.
  * ExternalSourceType is the class/struct that has the TSharedPtr<ExternalSourceItemType> data.
  * ExternalSourceItemType is the polymorphic struct type
  * GetItem is a function that will return a reference to the TSharedPtr<ExternalSourceItemType>
  *
  * !BIG DISCLAIMER:!
  *
  * This serializer was written to mimic the behavior seen in FGameplayAbilityTargetDataHandle and FGameplayEffectContextHandle 
  * which both are written with the intent of being used for RPCs and not being used for replicated properties and uses a TSharedPointer to hold the polymorphic struct
  *
  * That said, IF the serializer is used for replicated properties it has very specific requirements on the implementation of the SourceType to work correctly.
  *
  * 1. The sourcetype MUST provide a custom assignment operator performing a deep-copy/clone
  * 2. The sourcetype MUST define a comparison operator that compares the instance data of the stored ExternalSourceItemType
  * 3. TStructOpsTypeTraits::WithCopy and TStructOpsTypeTraits::WithIdenticalViaEquality must be specified
  *
  */
template <typename ExternalSourceType, typename ExternalSourceItemType, TSharedPtr<ExternalSourceItemType>&(*GetItem)(ExternalSourceType&)>
struct TPolymorphicStructNetSerializerImpl : protected Private::FPolymorphicStructNetSerializerInternal
{
	// 量化态：单一 polymorphic struct 的存储指针 + 类型索引（[1, 31] 或 0=Invalid）。
	struct FQuantizedData
	{
		void* StructData;
		uint32 TypeIndex;
	};

	// Traits
	static constexpr bool bHasDynamicState = true;
	static constexpr bool bIsForwardingSerializer = true; // Triggers asserts if a function is missing
	static constexpr bool bHasCustomNetReference = true; // We must support this as we do not know the type

	typedef ExternalSourceType SourceType;
	typedef FQuantizedData QuantizedType;
	typedef FPolymorphicStructNetSerializerConfig ConfigType;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs&);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs&);

	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs&);
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs&);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs&);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs&);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs&);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs&);

	static void CloneDynamicState(FNetSerializationContext&, const FNetCloneDynamicStateArgs&);
	static void FreeDynamicState(FNetSerializationContext&, const FNetFreeDynamicStateArgs&);

	static void CollectNetReferences(FNetSerializationContext&, const FNetCollectReferencesArgs&);

protected:
	typedef TPolymorphicStructNetSerializerImpl<ExternalSourceType, ExternalSourceItemType, GetItem> ThisType;

	typedef ExternalSourceItemType SourceItemType;
	typedef FPolymorphicNetSerializerScriptStructCache::FTypeInfo FTypeInfo;

	// TSharedPtr 的自定义 deleter：对 polymorphic struct 必须用 ScriptStruct->DestroyStruct
	// 调用真正的析构（因为 Item 类型在 TSharedPtr 模板参数中只是基类，不能用基类析构，
	// 否则派生成员不会被销毁）。
	struct FSourceItemTypeDeleter
	{
		void operator()(SourceItemType* Object) const
		{
			check(Object);
			UScriptStruct* ScriptStruct = Object->GetScriptStruct();
			check(ScriptStruct);
			ScriptStruct->DestroyStruct(Object);
			FMemory::Free(Object);
		}
	};

	// 注册时 hook：让具体派生 NetSerializer（如 FGameplayEffectContextHandleNetSerializer）
	// 一行代码完成"以 BaseScriptStruct 为根扫描注册派生类"。
	// 通过 const_cast 写入 DefaultConfig.RegisteredTypes —— DefaultConfig 通常是 const，
	// 但 RegisteredTypes 必须运行时填，所以这里有意 cast 掉 const。
	template <typename SerializerType>
	static void InitTypeCache()
	{
		FPolymorphicNetSerializerScriptStructCache* Cache = const_cast<FPolymorphicNetSerializerScriptStructCache*>(&SerializerType::DefaultConfig.RegisteredTypes);
		Cache->InitForType(SerializerType::SourceItemType::StaticStruct());
	}

private:
	// 释放当前 Item 量化态（递归 Free dynamic state + Free StructData + 清 TypeIndex）。
	static void InternalFreeItem(FNetSerializationContext& Context, const ConfigType& Config, QuantizedType& Value);
};


/**
  * TPolymorphicArrayStructNetSerializerImpl
  *
  * Helper to implement array serializers that requires dynamic polymorphism.
  * It can either be used to declare a typed serializer or be used as an internal helper.
  *
  * @See: TPolymorphicStructNetSerializerImpl for requirements on external data
  */
template <typename ExternalSourceType, typename ExternalSourceArrayItemType, TArrayView<TSharedPtr<ExternalSourceArrayItemType>>(*GetArray)(ExternalSourceType& Source), void(*SetArrayNum)(ExternalSourceType& Source, SIZE_T Num)>
struct TPolymorphicArrayStructNetSerializerImpl : protected Private::FPolymorphicStructNetSerializerInternal
{
	// Our quantized type
	// 单元素：与 TPolymorphicStructNetSerializerImpl::FQuantizedData 同结构。
	struct FQuantizedItem
	{
		void* StructData;
		uint32 TypeIndex;
	};

	// 数组级量化态：连续 Items 数组 + 元素数。
	struct FQuantizedArray
	{
		FQuantizedItem* Items;
		uint32 NumItems;
	};

	// Traits
	static constexpr bool bHasDynamicState = true;
	static constexpr bool bIsForwardingSerializer = true; // Triggers asserts if a function is missing
	static constexpr bool bHasCustomNetReference = true; // We must support this as we do not know the type

	typedef ExternalSourceType SourceType;
	typedef FQuantizedArray QuantizedType;
	typedef ExternalSourceArrayItemType SourceArrayItemType;
	typedef FPolymorphicArrayStructNetSerializerConfig ConfigType;

	typedef FPolymorphicNetSerializerScriptStructCache::FTypeInfo FTypeInfo;

	// 数组元素数序列化的位宽。8 bit → 最多 255 个元素，>= 此值 Validate/Quantize 报错。
	static const uint32 ArrayItemBits = 8U;
	static const uint32 MaxArrayItems = (1U << ArrayItemBits) - 1U;

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

	// 同样的自定义 deleter，用于 TSharedPtr<SourceArrayItemType>。
	struct FSourceArrayItemTypeDeleter
	{
		inline void operator()(SourceArrayItemType* Object) const
		{
			check(Object);
			UScriptStruct* ScriptStruct = Object->GetScriptStruct();
			check(ScriptStruct);
			ScriptStruct->DestroyStruct(Object);
			FMemory::Free(Object);
		}
	};

	// 与单值版同名工具：根据 SourceArrayItemType::StaticStruct() 注册派生类型。
	template <typename SerializerType>
	static void InitTypeCache()
	{
		FPolymorphicNetSerializerScriptStructCache* Cache = const_cast<FPolymorphicNetSerializerScriptStructCache*>(&SerializerType::DefaultConfig.RegisteredTypes);
		Cache->InitForType(SerializerType::SourceArrayItemType::StaticStruct());
	}

private:
	// 复用单值版做"单元素 SerializeDelta/DeserializeDelta"——把数组里第 i 个元素当作
	// 单值多态 struct 调用相应函数。注意第三模板参数 GetItem 用 nullptr —— 这个
	// 调用路径不会走到 GetItem，因此可以传 nullptr 满足模板要求。
	using FItemNetSerializer = TPolymorphicStructNetSerializerImpl<ExternalSourceType, ExternalSourceArrayItemType, nullptr>;

	// Allocate storage for the item array
	// 分配 NumItems * sizeof(FQuantizedItem) 的连续 buffer 给 Items（zero-init）。
	static void InternalAllocateItemArray(FNetSerializationContext& Context, QuantizedType& Value, uint32 NumItems);

	// Free allocated storage for the item array, including allocated struct data
	// 逐元素释放 StructData → Free Items 数组 → 清 NumItems。
	static void InternalFreeItemArray(FNetSerializationContext& Context, QuantizedType& Value, const FPolymorphicArrayStructNetSerializerConfig& Config);
};

/** TPolymorphicStructNetSerializerImpl */
// -----------------------------------------------------------------------------
// 单值多态 Serialize ——
//   报文：[1 bit isValidType] ([5 bit TypeIndex] [Descriptor 序列化的 struct 内容])?
// -----------------------------------------------------------------------------
template <typename ExternalSourceType, typename ExternalSourceItemType, TSharedPtr<ExternalSourceItemType>&(*GetItem)(ExternalSourceType&)>
void TPolymorphicStructNetSerializerImpl<ExternalSourceType, ExternalSourceItemType, GetItem>::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	FNetBitStreamWriter& Writer = *Context.GetBitStreamWriter();
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);

	const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(Value.TypeIndex);
	if (Writer.WriteBool(TypeInfo != nullptr))
	{
		CA_ASSUME(TypeInfo != nullptr);
		Writer.WriteBits(Value.TypeIndex, FPolymorphicNetSerializerScriptStructCache::RegisteredTypeBits);
		UE::Net::FReplicationStateOperations::Serialize(Context, static_cast<const uint8*>(Value.StructData), TypeInfo->Descriptor);
	}
}

// -----------------------------------------------------------------------------
// 单值多态 Deserialize ——
//   读 1 bit valid → 读 5 bit TypeIndex → 找 Descriptor → Alloc + Memzero +
//   FReplicationStateOperations::Deserialize 反序列化到新分配的 buffer。
// -----------------------------------------------------------------------------
// 注意：
//   * 必须先 InternalFreeItem 把旧 Target 释放，否则会泄漏；
//   * 未知 TypeIndex → SetError(NetError_PolymorphicStructNetSerializer_InvalidStructType)，
//     fall through 让 Target 保持空（已被 InternalFreeItem 清空）。
template <typename ExternalSourceType, typename ExternalSourceItemType, TSharedPtr<ExternalSourceItemType>&(*GetItem)(ExternalSourceType&)>
void TPolymorphicStructNetSerializerImpl<ExternalSourceType, ExternalSourceItemType, GetItem>::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader& Reader = *Context.GetBitStreamReader();
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);

	InternalFreeItem(Context, Config, Target);

	QuantizedType TempValue = {};
	if (const bool bIsValidType = Reader.ReadBool())
	{
		const uint32 TypeIndex = Reader.ReadBits(FPolymorphicNetSerializerScriptStructCache::RegisteredTypeBits);
		if (const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(TypeIndex))
		{
			const FReplicationStateDescriptor* Descriptor = TypeInfo->Descriptor;

			// Allocate storage and read struct data
			TempValue.StructData = FPolymorphicStructNetSerializerInternal::Alloc(Context, Descriptor->InternalSize, Descriptor->InternalAlignment);
			TempValue.TypeIndex = TypeIndex;

			FMemory::Memzero(TempValue.StructData, Descriptor->InternalSize);
			FReplicationStateOperations::Deserialize(Context, static_cast<uint8*>(TempValue.StructData), Descriptor);
		}
		else
		{
			Context.SetError(NetError_PolymorphicStructNetSerializer_InvalidStructType);
			// Fall through to clear the target
		}
	}

	Target = TempValue;
}

// -----------------------------------------------------------------------------
// 单值多态 SerializeDelta ——
//   报文："是否同类型" 1 bit
//          - 同类型：直接调元素 SerializeDelta；
//          - 不同类型：写 [isValid + TypeIndex + 数据]（与 Serialize 后两段相同）。
// -----------------------------------------------------------------------------
template <typename ExternalSourceType, typename ExternalSourceItemType, TSharedPtr<ExternalSourceItemType>&(*GetItem)(ExternalSourceType&)>
void TPolymorphicStructNetSerializerImpl<ExternalSourceType, ExternalSourceItemType, GetItem>::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	FNetBitStreamWriter& Writer = *Context.GetBitStreamWriter();
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);
	const QuantizedType& PrevValue = *reinterpret_cast<const QuantizedType*>(Args.Prev);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);

	// If prev has the same type we can delta-compress
	if (Writer.WriteBool(Value.TypeIndex == PrevValue.TypeIndex))
	{
		const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(Value.TypeIndex);
		if (TypeInfo != nullptr)
		{
			UE::Net::FReplicationStateOperations::SerializeDelta(Context, static_cast<const uint8*>(Value.StructData), static_cast<const uint8*>(PrevValue.StructData), TypeInfo->Descriptor);
		}
	}
	else
	{
		const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(Value.TypeIndex);
		if (Writer.WriteBool(TypeInfo != nullptr))
		{
			CA_ASSUME(TypeInfo != nullptr);
			Writer.WriteBits(Value.TypeIndex, FPolymorphicNetSerializerScriptStructCache::RegisteredTypeBits);
			UE::Net::FReplicationStateOperations::Serialize(Context, static_cast<const uint8*>(Value.StructData), TypeInfo->Descriptor);
		}
	}
}

template <typename ExternalSourceType, typename ExternalSourceItemType, TSharedPtr<ExternalSourceItemType>&(*GetItem)(ExternalSourceType&)>
void TPolymorphicStructNetSerializerImpl<ExternalSourceType, ExternalSourceItemType, GetItem>::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	FNetBitStreamReader& Reader = *Context.GetBitStreamReader();
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);
	const QuantizedType& PrevValue = *reinterpret_cast<const QuantizedType*>(Args.Prev);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);

	InternalFreeItem(Context, Config, Target);

	QuantizedType TempValue = {};

	// If prev has the same type we can delta-compress
	if (const bool bIsSameType = Reader.ReadBool())
	{
		const uint32 TypeIndex = PrevValue.TypeIndex;
		if (const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(TypeIndex))
		{
			const FReplicationStateDescriptor* Descriptor = TypeInfo->Descriptor;

			// Allocate storage and read struct data
			TempValue.StructData = FPolymorphicStructNetSerializerInternal::Alloc(Context, Descriptor->InternalSize, Descriptor->InternalAlignment);
			TempValue.TypeIndex = TypeIndex;

			FMemory::Memzero(TempValue.StructData, Descriptor->InternalSize);
			FReplicationStateOperations::DeserializeDelta(Context, static_cast<uint8*>(TempValue.StructData), static_cast<uint8*>(PrevValue.StructData), Descriptor);
		}
	}
	else
	{
		if (const bool bIsValidType = Reader.ReadBool())
		{
			const uint32 TypeIndex = Reader.ReadBits(FPolymorphicNetSerializerScriptStructCache::RegisteredTypeBits);
			if (const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(TypeIndex))
			{
				const FReplicationStateDescriptor* Descriptor = TypeInfo->Descriptor;

				// Allocate storage and read struct data
				TempValue.StructData = FPolymorphicStructNetSerializerInternal::Alloc(Context, Descriptor->InternalSize, Descriptor->InternalAlignment);
				TempValue.TypeIndex = TypeIndex;

				FMemory::Memzero(TempValue.StructData, Descriptor->InternalSize);
				FReplicationStateOperations::Deserialize(Context, static_cast<uint8*>(TempValue.StructData), Descriptor);
			}
			else
			{
				Context.SetError(NetError_PolymorphicStructNetSerializer_InvalidStructType);
				// Fall through to clear the target
			}
		}
	}

	Target = TempValue;
}

// -----------------------------------------------------------------------------
// 单值多态 Quantize ——
//   1) GetItem(SourceValue) → TSharedPtr<Item>
//   2) Item->GetScriptStruct() → Cache.GetTypeIndex
//   3) 找 Descriptor，Alloc(InternalSize) → FReplicationStateOperations::Quantize
//   4) Item 为空或类型未注册：清 TypeIndex / 报错
// -----------------------------------------------------------------------------
template <typename ExternalSourceType, typename ExternalSourceItemType, TSharedPtr<ExternalSourceItemType>&(*GetItem)(ExternalSourceType&)>
void TPolymorphicStructNetSerializerImpl<ExternalSourceType, ExternalSourceItemType, GetItem>::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	SourceType& SourceValue = *reinterpret_cast<SourceType*>(Args.Source);
	QuantizedType& TargetValue = *reinterpret_cast<QuantizedType*>(Args.Target);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);

	FPolymorphicStructNetSerializerInternal::Free(Context, TargetValue.StructData);

	const TSharedPtr<SourceItemType>& Item = GetItem(SourceValue);
	const UScriptStruct* ScriptStruct = Item.IsValid() ? Item->GetScriptStruct() : nullptr;
	const uint32 TypeIndex = Config.RegisteredTypes.GetTypeIndex(ScriptStruct);

	// Quantize polymorphic data
	QuantizedType TempValue = {};
	if (TypeIndex != FPolymorphicNetSerializerScriptStructCache::InvalidTypeIndex)
	{
		const FPolymorphicNetSerializerScriptStructCache::FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(TypeIndex);
		const FReplicationStateDescriptor* Descriptor = TypeInfo->Descriptor;

		TempValue.TypeIndex = TypeIndex;
		TempValue.StructData = FPolymorphicStructNetSerializerInternal::Alloc(Context, Descriptor->InternalSize, Descriptor->InternalAlignment);
		FMemory::Memzero(TempValue.StructData, Descriptor->InternalSize);

		FReplicationStateOperations::Quantize(Context, static_cast<uint8*>(TempValue.StructData), reinterpret_cast<const uint8*>(Item.Get()), Descriptor);
	}
	else
	{
		if (ScriptStruct)
		{
			Context.SetError(NetError_PolymorphicStructNetSerializer_InvalidStructType);
			UE_LOG(LogIris, Warning, TEXT("TPolymorphicStructNetSerializerImpl::Quantize Trying to quantize unregistered ScriptStruct type %s."), ToCStr(ScriptStruct->GetName()));
		}
	}

	TargetValue = TempValue;
}

// -----------------------------------------------------------------------------
// 单值多态 Dequantize ——
//   1) GetTypeInfo(SourceValue.TypeIndex) → ScriptStruct + Descriptor
//   2) 用全局 FMemory::Malloc + ScriptStruct->InitializeStruct 创建外部 SourceItem
//   3) FReplicationStateOperations::Dequantize 把量化态写回外部 struct
//   4) GetItem(TargetValue) = MakeShareable(NewData, FSourceItemTypeDeleter())
//   注：每次都重新 Malloc 是为了模仿 GameplayEffectContextHandle 的语义（外部
//        预期"每次 Dequantize 都得新对象，不要复用旧 buffer 改写"）。这并不
//        最优，应该作为 policy 暴露给上层。
// -----------------------------------------------------------------------------
template <typename ExternalSourceType, typename ExternalSourceItemType, TSharedPtr<ExternalSourceItemType>&(*GetItem)(ExternalSourceType&)>
void TPolymorphicStructNetSerializerImpl<ExternalSourceType, ExternalSourceItemType, GetItem>::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	const QuantizedType& SourceValue = *reinterpret_cast<const QuantizedType*>(Args.Source);
	SourceType& TargetValue = *reinterpret_cast<SourceType*>(Args.Target);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);

	// Dequantize polymorphic data
	if (const FPolymorphicNetSerializerScriptStructCache::FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(SourceValue.TypeIndex))
	{
		const FReplicationStateDescriptor* Descriptor = TypeInfo->Descriptor;
		const UScriptStruct* ScriptStruct = TypeInfo->ScriptStruct;

		// NOTE: We always allocate new memory in order to behave like the code we are trying to mimic expects, see GameplayEffectContextHandle
		// this should really be a policy of this class as it is far from optimal.

		// Allocate external struct, owned by external state therefore using global allocator
		SourceItemType* NewData = static_cast<SourceItemType*>(FMemory::Malloc(ScriptStruct->GetStructureSize(), ScriptStruct->GetMinAlignment()));
		ScriptStruct->InitializeStruct(NewData);

		FReplicationStateOperations::Dequantize(Context, reinterpret_cast<uint8*>(NewData), static_cast<const uint8*>(SourceValue.StructData), Descriptor);

		GetItem(TargetValue) = MakeShareable(NewData, FSourceItemTypeDeleter());
	}
	else
	{
		GetItem(TargetValue).Reset();
	}
}

// -----------------------------------------------------------------------------
// 单值多态 IsEqual ——
//   量化态：先比 TypeIndex，再调 FReplicationStateOperations::IsEqualQuantizedState；
//   真实态：直接 SourceType::operator==（前提：用户实现了基于"实例数据"的比较，
//          不是 TSharedPtr 的"指针相等"——见 BIG DISCLAIMER）。
// -----------------------------------------------------------------------------
template <typename ExternalSourceType, typename ExternalSourceItemType, TSharedPtr<ExternalSourceItemType>&(*GetItem)(ExternalSourceType&)>
bool TPolymorphicStructNetSerializerImpl<ExternalSourceType, ExternalSourceItemType, GetItem>::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);
		const QuantizedType& ValueA = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& ValueB = *reinterpret_cast<const QuantizedType*>(Args.Source1);

		if (ValueA.TypeIndex != ValueB.TypeIndex)
		{
			return false;
		}

		const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(ValueA.TypeIndex);
		checkSlow(TypeInfo != nullptr);
		if (TypeInfo != nullptr && !UE::Net::FReplicationStateOperations::IsEqualQuantizedState(Context, static_cast<const uint8*>(ValueA.StructData), static_cast<const uint8*>(ValueB.StructData), TypeInfo->Descriptor))
		{
			return false;
		}
	}
	else
	{
		const SourceType& ValueA = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& ValueB = *reinterpret_cast<const SourceType*>(Args.Source1);

		// Assuming there's a custom operator== because if there's not we would be hitting TSharedRef== which checks if the reference is identical, not the instance data.
		return ValueA == ValueB;
	}

	return true;
}

template <typename ExternalSourceType, typename ExternalSourceItemType, TSharedPtr<ExternalSourceItemType>&(*GetItem)(ExternalSourceType&)>
bool TPolymorphicStructNetSerializerImpl<ExternalSourceType, ExternalSourceItemType, GetItem>::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	SourceType& SourceValue = *reinterpret_cast<SourceType*>(Args.Source);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);

	const TSharedPtr<SourceItemType>& Item = GetItem(SourceValue);
	const UScriptStruct* ScriptStruct = Item.IsValid() ? Item->GetScriptStruct() : nullptr;
	const uint32 TypeIndex = Config.RegisteredTypes.GetTypeIndex(ScriptStruct);

	if (ScriptStruct != nullptr && (TypeIndex == FPolymorphicNetSerializerScriptStructCache::InvalidTypeIndex))
	{
		return false;
	}

	const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(TypeIndex);
	if (TypeInfo != nullptr && !UE::Net::FReplicationStateOperations::Validate(Context, reinterpret_cast<const uint8*>(Item.Get()), TypeInfo->Descriptor))
	{
		return false;
	}

	return true;
}

template <typename ExternalSourceType, typename ExternalSourceItemType, TSharedPtr<ExternalSourceItemType>&(*GetItem)(ExternalSourceType&)>
void TPolymorphicStructNetSerializerImpl<ExternalSourceType, ExternalSourceItemType, GetItem>::CloneDynamicState(FNetSerializationContext& Context, const FNetCloneDynamicStateArgs& Args)
{
	const QuantizedType& SourceValue = *reinterpret_cast<const QuantizedType*>(Args.Source);
	QuantizedType& TargetValue = *reinterpret_cast<QuantizedType*>(Args.Target);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);

	// Clone polymorphic data
	const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(SourceValue.TypeIndex);
	TargetValue.TypeIndex = SourceValue.TypeIndex;

	if (TypeInfo)
	{
		const FReplicationStateDescriptor* Descriptor = TypeInfo->Descriptor;

		// We need some memory to store the state for the polymorphic struct
		TargetValue.StructData = FPolymorphicStructNetSerializerInternal::Alloc(Context, Descriptor->InternalSize, Descriptor->InternalAlignment);
		FPolymorphicStructNetSerializerInternal::CloneQuantizedState(Context, static_cast<uint8*>(TargetValue.StructData), static_cast<const uint8*>(SourceValue.StructData), Descriptor);
	}
	else
	{
		TargetValue.StructData = nullptr;
	}
}

template <typename ExternalSourceType, typename ExternalSourceItemType, TSharedPtr<ExternalSourceItemType>&(*GetItem)(ExternalSourceType&)>
void TPolymorphicStructNetSerializerImpl<ExternalSourceType, ExternalSourceItemType, GetItem>::FreeDynamicState(FNetSerializationContext& Context, const FNetFreeDynamicStateArgs& Args)
{
	QuantizedType& SourceValue = *reinterpret_cast<QuantizedType*>(Args.Source);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);

	InternalFreeItem(Context, Config, SourceValue);
}

template <typename ExternalSourceType, typename ExternalSourceItemType, TSharedPtr<ExternalSourceItemType>&(*GetItem)(ExternalSourceType&)>
void TPolymorphicStructNetSerializerImpl<ExternalSourceType, ExternalSourceItemType, GetItem>::CollectNetReferences(FNetSerializationContext& Context, const FNetCollectReferencesArgs& Args)
{
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);
	FNetReferenceCollector& Collector = *reinterpret_cast<UE::Net::FNetReferenceCollector*>(Args.Collector);

	// No references nothing to do
	if (Value.TypeIndex == FPolymorphicNetSerializerScriptStructCache::InvalidTypeIndex || !Config.RegisteredTypes.CanHaveNetReferences())
	{
		return;
	}

	const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(Value.TypeIndex);
	checkSlow(TypeInfo != nullptr);
	const FReplicationStateDescriptor* Descriptor = TypeInfo ? TypeInfo->Descriptor.GetReference() : nullptr;
	if (Descriptor != nullptr && EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::HasObjectReference))
	{
		FPolymorphicStructNetSerializerInternal::CollectReferences(Context, Collector, Args.ChangeMaskInfo, static_cast<const uint8*>(Value.StructData), Descriptor);
	}
}

// -----------------------------------------------------------------------------
// 单值多态 InternalFreeItem —— Free Item 量化态：递归释放 dynamic state + Free StructData
// -----------------------------------------------------------------------------
template <typename ExternalSourceType, typename ExternalSourceItemType, TSharedPtr<ExternalSourceItemType>&(*GetItem)(ExternalSourceType&)>
void TPolymorphicStructNetSerializerImpl<ExternalSourceType, ExternalSourceItemType, GetItem>::InternalFreeItem(FNetSerializationContext& Context, const ConfigType& Config, QuantizedType& Value)
{
	const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(Value.TypeIndex);
	const FReplicationStateDescriptor* Descriptor = TypeInfo ? TypeInfo->Descriptor.GetReference() : nullptr;

	if (Value.StructData != nullptr)
	{
		checkSlow(Descriptor != nullptr);
		if (Descriptor && EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::HasDynamicState))
		{
			FReplicationStateOperations::FreeDynamicState(Context, static_cast<uint8*>(Value.StructData), Descriptor);
		}
		FPolymorphicStructNetSerializerInternal::Free(Context, Value.StructData);
		Value.StructData = nullptr;
		Value.TypeIndex = FPolymorphicNetSerializerScriptStructCache::InvalidTypeIndex;
	}
}

/** TPolymorphicArrayStructNetSerializerImpl */
// -----------------------------------------------------------------------------
// 数组多态 Serialize —— [8 bit NumItems] (每元素同单值版的 valid/type/data)
// -----------------------------------------------------------------------------
template <typename ExternalSourceType, typename ExternalSourceArrayItemType, TArrayView<TSharedPtr<ExternalSourceArrayItemType>>(*GetArray)(ExternalSourceType& Source), void(*SetArrayNum)(ExternalSourceType& Source, SIZE_T Num)>
void TPolymorphicArrayStructNetSerializerImpl<ExternalSourceType, ExternalSourceArrayItemType, GetArray, SetArrayNum>::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	FNetBitStreamWriter& Writer = *Context.GetBitStreamWriter();
	const QuantizedType& SourceValue = *reinterpret_cast<const QuantizedType*>(Args.Source);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);

	// Serialize quantized data
	Writer.WriteBits(SourceValue.NumItems, ArrayItemBits);	
	for (uint32 It = 0, EndIt = SourceValue.NumItems; It != EndIt; ++It)
	{
		UE_NET_TRACE_SCOPE(Element, Writer, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);

		const FQuantizedItem& Item = SourceValue.Items[It];
		const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(Item.TypeIndex);
		if (Writer.WriteBool(TypeInfo != nullptr))
		{			
			CA_ASSUME(TypeInfo != nullptr);
			Writer.WriteBits(Item.TypeIndex, FPolymorphicNetSerializerScriptStructCache::RegisteredTypeBits);
			FReplicationStateOperations::Serialize(Context, static_cast<const uint8*>(Item.StructData), TypeInfo->Descriptor);
		}
	}
}

// -----------------------------------------------------------------------------
// 数组多态 Deserialize ——
//   先读 NumItems（8 bit），过大或 overflow 立即报错；
//   InternalFreeItemArray + InternalAllocateItemArray 重置 storage；
//   逐元素读 valid/type/data。
// -----------------------------------------------------------------------------
template <typename ExternalSourceType, typename ExternalSourceArrayItemType, TArrayView<TSharedPtr<ExternalSourceArrayItemType>>(*GetArray)(ExternalSourceType& Source), void(*SetArrayNum)(ExternalSourceType& Source, SIZE_T Num)>
void TPolymorphicArrayStructNetSerializerImpl<ExternalSourceType, ExternalSourceArrayItemType, GetArray, SetArrayNum>::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	FNetBitStreamReader& Reader = *Context.GetBitStreamReader();
	QuantizedType& TargetValue = *reinterpret_cast<QuantizedType*>(Args.Target);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);

	// Deserialize quantized data
	const uint32 NumItems = Reader.ReadBits(ArrayItemBits); 

	if (Reader.IsOverflown())
	{
		Context.SetError(GNetError_BitStreamOverflow);
		return;
	}

	// Currently we always free all memory even though we in theory could keep it if all types are matching
	InternalFreeItemArray(Context, TargetValue, Config);

	// Allocate space for the ItemArray
	InternalAllocateItemArray(Context, TargetValue, NumItems);

	// Read polymorphic state data
	for (uint32 It = 0, EndIt = TargetValue.NumItems; It != EndIt; ++It)
	{
		UE_NET_TRACE_SCOPE(Element, Reader, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);

		FQuantizedItem& Item = TargetValue.Items[It];
		if (Reader.ReadBool())
		{
			const uint32 TypeIndex = Reader.ReadBits(FPolymorphicNetSerializerScriptStructCache::RegisteredTypeBits);
			if (const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(TypeIndex))
			{				
				const FReplicationStateDescriptor* Descriptor = TypeInfo->Descriptor;

				// Allocate storage and read struct data
				Item.StructData = FPolymorphicStructNetSerializerInternal::Alloc(Context, Descriptor->InternalSize, Descriptor->InternalAlignment);
				Item.TypeIndex = TypeIndex;

				FMemory::Memzero(Item.StructData, Descriptor->InternalSize);
				UE::Net::FReplicationStateOperations::Deserialize(Context, static_cast<uint8*>(Item.StructData), Descriptor);
			}
			else
			{
				InternalFreeItemArray(Context, TargetValue, Config);
				Context.SetError(NetError_PolymorphicStructNetSerializer_InvalidStructType);
				return;
			}
		}
	}
}

// -----------------------------------------------------------------------------
// 数组多态 SerializeDelta —— 元素级增量
// -----------------------------------------------------------------------------
// 报文：[1 bit bSameSizeArray] [bSameSizeArray==0 → 8 bit NumItems]
//        [前 PrevNumItems 个元素：调 FItemNetSerializer::SerializeDelta 走单值版增量]
//        [溢出元素：标准 valid/type/data]
// 与 ArrayProperty 类似，只是元素是单值多态而不是普通元素。
template <typename ExternalSourceType, typename ExternalSourceArrayItemType, TArrayView<TSharedPtr<ExternalSourceArrayItemType>>(*GetArray)(ExternalSourceType& Source), void(*SetArrayNum)(ExternalSourceType& Source, SIZE_T Num)>
void TPolymorphicArrayStructNetSerializerImpl<ExternalSourceType, ExternalSourceArrayItemType, GetArray, SetArrayNum>::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	FNetBitStreamWriter& Writer = *Context.GetBitStreamWriter();
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const QuantizedType& Array = *reinterpret_cast<const QuantizedType*>(Args.Source);
	const QuantizedType& PrevArray = *reinterpret_cast<const QuantizedType*>(Args.Prev);

	const uint32 NumItems = Array.NumItems;
	const uint32 PrevNumItems = PrevArray.NumItems;
	const bool bSameSizeArray = (NumItems == PrevNumItems);
	Writer.WriteBits(bSameSizeArray, 1U);
	if (!bSameSizeArray)
	{
		Writer.WriteBits(NumItems, ArrayItemBits);
	}

	// Use delta serialization for elements available in both the previous and current state.
	if (PrevNumItems)
	{
		FNetSerializeDeltaArgs ElementArgs = Args;

		for (uint32 ElementIt = 0, ElementEndIt = FPlatformMath::Min(NumItems, PrevNumItems); ElementIt != ElementEndIt; ++ElementIt)
		{
			UE_NET_TRACE_SCOPE(Element, Writer, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);

			ElementArgs.Source = NetSerializerValuePointer(&Array.Items[ElementIt]);
			ElementArgs.Prev = NetSerializerValuePointer(&PrevArray.Items[ElementIt]);

			FItemNetSerializer::SerializeDelta(Context, ElementArgs);
		}
	}

	// Serialize additional items with standard serialization.
	if (NumItems > PrevNumItems)
	{
		for (uint32 ElementIt = PrevNumItems, ElementEndIt = NumItems; ElementIt < ElementEndIt; ++ElementIt)
		{
			UE_NET_TRACE_SCOPE(Element, Writer, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
			const FQuantizedItem& Item = Array.Items[ElementIt];
			const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(Item.TypeIndex);
			if (Writer.WriteBool(TypeInfo != nullptr))
			{
				CA_ASSUME(TypeInfo != nullptr);
				Writer.WriteBits(Item.TypeIndex, FPolymorphicNetSerializerScriptStructCache::RegisteredTypeBits);
				FReplicationStateOperations::Serialize(Context, static_cast<const uint8*>(Item.StructData), TypeInfo->Descriptor);
			}
		}
	}
}

template <typename ExternalSourceType, typename ExternalSourceArrayItemType, TArrayView<TSharedPtr<ExternalSourceArrayItemType>>(*GetArray)(ExternalSourceType& Source), void(*SetArrayNum)(ExternalSourceType& Source, SIZE_T Num)>
void TPolymorphicArrayStructNetSerializerImpl<ExternalSourceType, ExternalSourceArrayItemType, GetArray, SetArrayNum>::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	FNetBitStreamReader& Reader = *Context.GetBitStreamReader();
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);
	QuantizedType& Array = *reinterpret_cast<QuantizedType*>(Args.Target);
	const QuantizedType& PrevArray = *reinterpret_cast<const QuantizedType*>(Args.Prev);

	const uint32 PrevNumItems = PrevArray.NumItems;
	const bool bSameSizeArray = !!Reader.ReadBits(1U);
	const uint32 NumItems = (bSameSizeArray ? PrevNumItems : Reader.ReadBits(ArrayItemBits));

	if (Reader.IsOverflown())
	{
		Context.SetError(GNetError_BitStreamOverflow);
		return;
	}

	// Currently we always free all memory even though we in theory could keep it if all types are matching
	InternalFreeItemArray(Context, Array, Config);

	// Allocate space for the ItemArray
	InternalAllocateItemArray(Context, Array, NumItems);

	// Elements in the current array up to the previous size can use delta serialization.
	if (PrevNumItems)
	{
		FNetDeserializeDeltaArgs ElementArgs = Args;

		for (uint32 ElementIt = 0, ElementEndIt = FPlatformMath::Min(NumItems, PrevNumItems); ElementIt != ElementEndIt; ++ElementIt)
		{
			UE_NET_TRACE_SCOPE(Element, Reader, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);

			ElementArgs.Target = NetSerializerValuePointer(&Array.Items[ElementIt]);
			ElementArgs.Prev = NetSerializerValuePointer(&PrevArray.Items[ElementIt]);

			FItemNetSerializer::DeserializeDelta(Context, ElementArgs);
		}
	}

	if (Context.HasError())
	{
		InternalFreeItemArray(Context, Array, Config);
		return;
	}

	if (Reader.IsOverflown())
	{
		Context.SetError(GNetError_BitStreamOverflow);
		InternalFreeItemArray(Context, Array, Config);
		return;
	}

	// Deserialize additional items with standard deserialization.
	if (NumItems > PrevNumItems)
	{
		for (uint32 ElementIt = PrevNumItems, ElementEndIt = NumItems; ElementIt < ElementEndIt; ++ElementIt)
		{
			UE_NET_TRACE_SCOPE(Element, Reader, Context.GetTraceCollector(), ENetTraceVerbosity::VeryVerbose);
			FQuantizedItem& Item = Array.Items[ElementIt];
			if (Reader.ReadBool())
			{
				const uint32 TypeIndex = Reader.ReadBits(FPolymorphicNetSerializerScriptStructCache::RegisteredTypeBits);
				if (const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(TypeIndex))
				{
					const FReplicationStateDescriptor* Descriptor = TypeInfo->Descriptor;

					// Allocate storage and read struct data
					Item.StructData = FPolymorphicStructNetSerializerInternal::Alloc(Context, Descriptor->InternalSize, Descriptor->InternalAlignment);
					Item.TypeIndex = TypeIndex;

					FMemory::Memzero(Item.StructData, Descriptor->InternalSize);
					UE::Net::FReplicationStateOperations::Deserialize(Context, static_cast<uint8*>(Item.StructData), Descriptor);
				}
				else
				{
					InternalFreeItemArray(Context, Array, Config);
					Context.SetError(NetError_PolymorphicStructNetSerializer_InvalidStructType);
					return;
				}
			}
		}
	}

	if (Reader.IsOverflown())
	{
		Context.SetError(GNetError_BitStreamOverflow);
		InternalFreeItemArray(Context, Array, Config);
		return;
	}
}

// -----------------------------------------------------------------------------
// 数组多态 Quantize ——
//   1) GetArray(SourceValue) 拿 TArrayView<TSharedPtr<Item>>；
//   2) NumItems > MaxArrayItems(=255) 立即报错；
//   3) InternalFreeItemArray 释放旧分配；
//   4) InternalAllocateItemArray 分配新连续 Items 数组；
//   5) 逐元素：GetTypeIndex → Alloc + Memzero + Quantize。
// -----------------------------------------------------------------------------
template <typename ExternalSourceType, typename ExternalSourceArrayItemType, TArrayView<TSharedPtr<ExternalSourceArrayItemType>>(*GetArray)(ExternalSourceType& Source), void(*SetArrayNum)(ExternalSourceType& Source, SIZE_T Num)>
void TPolymorphicArrayStructNetSerializerImpl<ExternalSourceType, ExternalSourceArrayItemType, GetArray, SetArrayNum>::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	SourceType& SourceValue = *reinterpret_cast<SourceType*>(Args.Source);
	QuantizedType& TargetValue = *reinterpret_cast<QuantizedType*>(Args.Target);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);

	// Make sure we can properly serialize the array.
	TArrayView<const TSharedPtr<SourceArrayItemType>> ItemArray = GetArray(SourceValue);
	const uint32 NumItems = static_cast<uint32>(ItemArray.Num());
	if (NumItems > MaxArrayItems)
	{
		Context.SetError(GNetError_ArraySizeTooLarge);
		return;
	}

	// First we need to free previously allocated data
	InternalFreeItemArray(Context, TargetValue, Config);

	// Allocate new array
	InternalAllocateItemArray(Context, TargetValue, NumItems);

	// Copy polymorphic struct data
	const TSharedPtr<SourceArrayItemType>* ItemArrayData = ItemArray.GetData();
	for (const TSharedPtr<SourceArrayItemType>& SourceItem : ItemArray)
	{
		const SIZE_T ArrayIndex = &SourceItem - ItemArrayData;
		const UScriptStruct* ScriptStruct = SourceItem.IsValid() ? SourceItem->GetScriptStruct() : nullptr;
		FQuantizedItem& TargetItem = TargetValue.Items[ArrayIndex];
		const uint32 TypeIndex = Config.RegisteredTypes.GetTypeIndex(ScriptStruct);

		if (TypeIndex != FPolymorphicNetSerializerScriptStructCache::InvalidTypeIndex)
		{
			const FPolymorphicNetSerializerScriptStructCache::FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(TypeIndex);
			const FReplicationStateDescriptor* Descriptor = TypeInfo->Descriptor;

			TargetItem.TypeIndex = TypeIndex;
			TargetItem.StructData = FPolymorphicStructNetSerializerInternal::Alloc(Context, Descriptor->InternalSize, Descriptor->InternalAlignment);
			FMemory::Memzero(TargetItem.StructData, Descriptor->InternalSize);

			// Quantize data
			FReplicationStateOperations::Quantize(Context, static_cast<uint8*>(TargetItem.StructData), reinterpret_cast<const uint8*>(SourceItem.Get()), Descriptor);
		}
		else
		{
			if (ScriptStruct)
			{
				Context.SetError(NetError_PolymorphicStructNetSerializer_InvalidStructType);
				UE_LOG(LogIris, Warning,  TEXT("TPolymorphicArrayStructNetSerializerImpl::Quantize Trying to quantize unregistered ScriptStruct type %s."), ToCStr(ScriptStruct->GetName()));
			}
		}
	}
}

// -----------------------------------------------------------------------------
// 数组多态 Dequantize ——
//   SetArrayNum 调整外部 TArray 长度；
//   逐元素：每次都 Malloc + InitializeStruct + Dequantize + MakeShareable
//          （与单值版相同语义：每次都新建实例）。
// -----------------------------------------------------------------------------
template <typename ExternalSourceType, typename ExternalSourceArrayItemType, TArrayView<TSharedPtr<ExternalSourceArrayItemType>>(*GetArray)(ExternalSourceType& Source), void(*SetArrayNum)(ExternalSourceType& Source, SIZE_T Num)>
void TPolymorphicArrayStructNetSerializerImpl<ExternalSourceType, ExternalSourceArrayItemType, GetArray, SetArrayNum>::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	SourceType& TargetValue = *reinterpret_cast<SourceType*>(Args.Target);
	const QuantizedType& SourceValue = *reinterpret_cast<const QuantizedType*>(Args.Source);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);

	SetArrayNum(TargetValue, SourceValue.NumItems);
	TArrayView<TSharedPtr<SourceArrayItemType>> TargetArray = GetArray(TargetValue);
	// Dequantize polymorphic data
	for (uint32 It = 0, EndIt = SourceValue.NumItems; It != EndIt; ++It)
	{
		const FQuantizedItem& Item = SourceValue.Items[It];

		if (const FPolymorphicNetSerializerScriptStructCache::FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(Item.TypeIndex))
		{
			const FReplicationStateDescriptor* Descriptor = TypeInfo->Descriptor;
			const UScriptStruct* ScriptStruct = TypeInfo->ScriptStruct;

			// NOTE: We always allocate new memory in order to behave like the code we are trying to mimic expects, see GameplayEffectContextHandle
			// this should really be a policy of this class as it is far from optimal.

			// Allocate external struct, owned by external state therefore using global allocator
			SourceArrayItemType* NewData = static_cast<SourceArrayItemType*>(FMemory::Malloc(ScriptStruct->GetStructureSize(), ScriptStruct->GetMinAlignment()));
			ScriptStruct->InitializeStruct(NewData);

			// Dequantize data
			FReplicationStateOperations::Dequantize(Context, reinterpret_cast<uint8*>(NewData), static_cast<const uint8*>(Item.StructData), Descriptor);

			TargetArray[It] = MakeShareable(NewData, FSourceArrayItemTypeDeleter());
		}
		else
		{
			TargetArray[It].Reset();
		}
	}
}

template <typename ExternalSourceType, typename ExternalSourceArrayItemType, TArrayView<TSharedPtr<ExternalSourceArrayItemType>>(*GetArray)(ExternalSourceType& Source), void(*SetArrayNum)(ExternalSourceType& Source, SIZE_T Num)>
bool TPolymorphicArrayStructNetSerializerImpl<ExternalSourceType, ExternalSourceArrayItemType, GetArray, SetArrayNum>::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);
		const QuantizedType& ValueA = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& ValueB = *reinterpret_cast<const QuantizedType*>(Args.Source1);

		if (ValueA.NumItems != ValueB.NumItems)
		{
			return false;
		}

		for (uint32 It = 0, EndIt = ValueA.NumItems; It != EndIt; ++It)
		{
			const FQuantizedItem& ItemA = ValueA.Items[It];
			const FQuantizedItem& ItemB = ValueB.Items[It];

			if (ItemA.TypeIndex != ItemB.TypeIndex)
			{
				return false;
			}

			const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(ItemA.TypeIndex);
			if (TypeInfo && !UE::Net::FReplicationStateOperations::IsEqualQuantizedState(Context, static_cast<const uint8*>(ItemA.StructData), static_cast<const uint8*>(ItemB.StructData), TypeInfo->Descriptor))
			{
				return false;
			}
		}
	}
	else
	{
		const SourceType& ValueA = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& ValueB = *reinterpret_cast<const SourceType*>(Args.Source1);

		return ValueA == ValueB;
	}

	return true;
}

template <typename ExternalSourceType, typename ExternalSourceArrayItemType, TArrayView<TSharedPtr<ExternalSourceArrayItemType>>(*GetArray)(ExternalSourceType& Source), void(*SetArrayNum)(ExternalSourceType& Source, SIZE_T Num)>
bool TPolymorphicArrayStructNetSerializerImpl<ExternalSourceType, ExternalSourceArrayItemType, GetArray, SetArrayNum>::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	SourceType& SourceValue = *reinterpret_cast<SourceType*>(Args.Source);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);

	TArrayView<const TSharedPtr<SourceArrayItemType>> ItemArray = GetArray(SourceValue);
	const uint32 NumItems = (uint32)ItemArray.Num();
	if (NumItems > MaxArrayItems)
	{
		return false;
	}

	for (const TSharedPtr<SourceArrayItemType>& Item : ItemArray)
	{
		const UScriptStruct* ScriptStruct = Item.IsValid() ? Item->GetScriptStruct() : nullptr;
		const uint32 TypeIndex = Config.RegisteredTypes.GetTypeIndex(ScriptStruct);

		if (ScriptStruct && (TypeIndex == FPolymorphicNetSerializerScriptStructCache::InvalidTypeIndex))
		{
			return false;
		}

		const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(TypeIndex);
		if (TypeInfo && !FReplicationStateOperations::Validate(Context, reinterpret_cast<const uint8*>(Item.Get()), TypeInfo->Descriptor))
		{
			return false;
		}
	}

	return true;
}

template <typename ExternalSourceType, typename ExternalSourceArrayItemType, TArrayView<TSharedPtr<ExternalSourceArrayItemType>>(*GetArray)(ExternalSourceType& Source), void(*SetArrayNum)(ExternalSourceType& Source, SIZE_T Num)>
void TPolymorphicArrayStructNetSerializerImpl<ExternalSourceType, ExternalSourceArrayItemType, GetArray, SetArrayNum>::CollectNetReferences(FNetSerializationContext& Context, const FNetCollectReferencesArgs& Args)
{
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const QuantizedType& SourceValue = *reinterpret_cast<const QuantizedType*>(Args.Source);
	FNetReferenceCollector& Collector = *reinterpret_cast<FNetReferenceCollector*>(Args.Collector);

	// No references nothing to do
	if (SourceValue.NumItems == 0U || !Config.RegisteredTypes.CanHaveNetReferences())
	{
		return;
	}

	for (uint32 It = 0, EndIt = SourceValue.NumItems; It != EndIt; ++It)
	{
		const FQuantizedItem& Item = SourceValue.Items[It];
		const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(Item.TypeIndex);
		const FReplicationStateDescriptor* Descriptor = TypeInfo ? TypeInfo->Descriptor.GetReference() : nullptr;

		if (Descriptor && EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::HasObjectReference))
		{			
			FPolymorphicStructNetSerializerInternal::CollectReferences(Context, Collector, Args.ChangeMaskInfo, static_cast<const uint8*>(Item.StructData), Descriptor);
		}
	}
}

template <typename ExternalSourceType, typename ExternalSourceArrayItemType, TArrayView<TSharedPtr<ExternalSourceArrayItemType>>(*GetArray)(ExternalSourceType& Source), void(*SetArrayNum)(ExternalSourceType& Source, SIZE_T Num)>
void TPolymorphicArrayStructNetSerializerImpl<ExternalSourceType, ExternalSourceArrayItemType, GetArray, SetArrayNum>::CloneDynamicState(FNetSerializationContext& Context, const FNetCloneDynamicStateArgs& Args)
{
	const QuantizedType& SourceValue = *reinterpret_cast<const QuantizedType*>(Args.Source);
	QuantizedType& TargetValue = *reinterpret_cast<QuantizedType*>(Args.Target);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);

	InternalAllocateItemArray(Context, TargetValue, SourceValue.NumItems);

	// Clone polymorphic data
	for (uint32 It = 0, EndIt = SourceValue.NumItems; It != EndIt; ++It)
	{
		const FQuantizedItem& SourceItem = SourceValue.Items[It];
		FQuantizedItem& TargetItem = TargetValue.Items[It];

		const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(SourceItem.TypeIndex);
		TargetItem.TypeIndex = SourceItem.TypeIndex;

		if (TypeInfo)
		{
			const FReplicationStateDescriptor* Descriptor = TypeInfo->Descriptor;

			// We need some memory to store the state for the polymorphic struct
			TargetItem.StructData = FPolymorphicStructNetSerializerInternal::Alloc(Context, Descriptor->InternalSize, Descriptor->InternalAlignment);
			FPolymorphicStructNetSerializerInternal::CloneQuantizedState(Context, static_cast<uint8*>(TargetItem.StructData), static_cast<const uint8*>(SourceItem.StructData), Descriptor);
		}
		else
		{
			TargetItem.StructData = nullptr;
		}	
	}
}

template <typename ExternalSourceType, typename ExternalSourceArrayItemType, TArrayView<TSharedPtr<ExternalSourceArrayItemType>>(*GetArray)(ExternalSourceType& Source), void(*SetArrayNum)(ExternalSourceType& Source, SIZE_T Num)>
void TPolymorphicArrayStructNetSerializerImpl<ExternalSourceType, ExternalSourceArrayItemType, GetArray, SetArrayNum>::FreeDynamicState(FNetSerializationContext& Context, const FNetFreeDynamicStateArgs& Args)
{
	QuantizedType& SourceValue = *reinterpret_cast<QuantizedType*>(Args.Source);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);

	InternalFreeItemArray(Context, SourceValue, Config);
}

template <typename ExternalSourceType, typename ExternalSourceArrayItemType, TArrayView<TSharedPtr<ExternalSourceArrayItemType>>(*GetArray)(ExternalSourceType& Source), void(*SetArrayNum)(ExternalSourceType& Source, SIZE_T Num)>
void TPolymorphicArrayStructNetSerializerImpl<ExternalSourceType, ExternalSourceArrayItemType, GetArray, SetArrayNum>::InternalAllocateItemArray(FNetSerializationContext& Context, QuantizedType& Value, uint32 NumItems)
{
	const SIZE_T ElementSize = sizeof(FQuantizedItem);
	const SIZE_T Alignment = alignof(FQuantizedItem);

	if (NumItems > 0)
	{
		Value.Items = static_cast<FQuantizedItem*>(FPolymorphicStructNetSerializerInternal::Alloc(Context, ElementSize*NumItems, Alignment));
		Value.NumItems = NumItems;
		FMemory::Memzero(Value.Items, ElementSize*NumItems);
	}
	else
	{
		Value.Items = nullptr;
		Value.NumItems = 0;
	}
}

template <typename ExternalSourceType, typename ExternalSourceArrayItemType, TArrayView<TSharedPtr<ExternalSourceArrayItemType>>(*GetArray)(ExternalSourceType& Source), void(*SetArrayNum)(ExternalSourceType& Source, SIZE_T Num)>
void TPolymorphicArrayStructNetSerializerImpl<ExternalSourceType, ExternalSourceArrayItemType, GetArray, SetArrayNum>::InternalFreeItemArray(FNetSerializationContext& Context, QuantizedType& Value, const FPolymorphicArrayStructNetSerializerConfig& Config)
{
	for (uint32 It = 0, EndIt = Value.NumItems; It != EndIt; ++It)
	{
		FQuantizedItem& Item = Value.Items[It];

		const FTypeInfo* TypeInfo = Config.RegisteredTypes.GetTypeInfo(Item.TypeIndex);
		const FReplicationStateDescriptor* Descriptor = TypeInfo ? TypeInfo->Descriptor.GetReference() : nullptr;

		if (Item.StructData)
		{
			checkSlow(Descriptor != nullptr);
			if (Descriptor && EnumHasAnyFlags(Descriptor->Traits, EReplicationStateTraits::HasDynamicState))
			{
				FReplicationStateOperations::FreeDynamicState(Context, static_cast<uint8*>(Item.StructData), Descriptor);
			}
			FPolymorphicStructNetSerializerInternal::Free(Context, Value.Items[It].StructData);
		}
	}
	FPolymorphicStructNetSerializerInternal::Free(Context, Value.Items);

	// Reset the value
	Value.Items = nullptr;
	Value.NumItems = 0U;
}

}
