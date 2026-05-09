// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// PolymorphicNetSerializer.cpp ——
//   * FPolymorphicNetSerializerScriptStructCache::InitForType 实现：扫描派生类、
//     烘焙 Descriptor、稳定排序、校验上限；
//   * FPolymorphicStructNetSerializerInternal 工具：把 InternalContext 的 Alloc/
//     Free / CollectRefs / CloneQuantizedState 暴露给模板 Impl 使用（避免模板
//     直接依赖 Iris 私有头文件）。
// =============================================================================

#include "Iris/Serialization/PolymorphicNetSerializer.h"
#include "Iris/Serialization/PolymorphicNetSerializerImpl.h"
#include "Iris/Core/IrisProfiler.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/ReplicationState/ReplicationStateDescriptorBuilder.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/ReplicationSystem/ReplicationOperationsInternal.h"
#include "UObject/UObjectIterator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PolymorphicNetSerializer)

namespace UE::Net
{

// 多态报错码：当反序列化端遇到未知 TypeIndex（发送端注册了，本端没注册）时上报。
const FName NetError_PolymorphicStructNetSerializer_InvalidStructType(TEXT("Invalid struct type"));

// -----------------------------------------------------------------------------
// InitForType —— 扫描所有 IsChildOf 派生类并烘焙 Descriptor
// -----------------------------------------------------------------------------
// 详细流程：
//   1. GetObjectsOfClass(UScriptStruct, includeDerived=true) 把所有 UScriptStruct 拿出来；
//   2. 过滤 IsChildOf(InScriptStruct) 的；
//   3. 已经注册过的复用旧 Descriptor，否则 CreateDescriptorForStruct（昂贵）；
//   4. 按 ScriptStruct 名小写降序稳定排序 —— 保证两端只要派生集合相同，TypeIndex 也一致；
//   5. 检查注册数量是否 ≤ MaxRegisteredTypeCount (=31)，超过 LowLevelFatalError；
//   6. 把新数组 MoveTemp 进 RegisteredTypes。
//
// 性能：startup-time 操作，单次成本 O(全部 UScriptStruct + 派生类 Descriptor 烘焙)。
void FPolymorphicNetSerializerScriptStructCache::InitForType(const UScriptStruct* InScriptStruct)
{
	IRIS_PROFILER_SCOPE(FPolymorphicNetSerializerScriptStructCache_InitForType);

	TArray<FTypeInfo> UpdatedRegisteredTypes;
	UpdatedRegisteredTypes.Reserve(RegisteredTypes.Num());

	// 重置 trait 并集；下面遍历时累计。
	CommonTraits = EReplicationStateTraits::None;

	bool bFoundNewScriptStructs = false;

	// Find all script structs of this type and add them to the list and build descriptor
	// (not sure of a better way to do this but it should only happen at startup)
	TArray<UObject*> Structs;
	Structs.Reserve(EstimatedScriptStructCount);

	constexpr bool bIncludeDerivedClasses = true;
	GetObjectsOfClass(UScriptStruct::StaticClass(), Structs, bIncludeDerivedClasses, RF_ClassDefaultObject, GetObjectIteratorDefaultInternalExclusionFlags(EInternalObjectFlags::None));
	// 用本次返回数当做下次预留量，避免反复 realloc。
	EstimatedScriptStructCount = Structs.Max();
	for (const UObject* Object : Structs)
	{
		const UScriptStruct* Struct = static_cast<const UScriptStruct*>(Object);
		if (Struct->IsChildOf(InScriptStruct))
		{
			FTypeInfo Entry;
			Entry.ScriptStruct = Struct;

			// See if we already had a descriptor for the struct
			// 增量更新：若本类型在旧表已注册，复用 Descriptor（避免重复烘焙）。
			const uint32 ExistingTypeIndex = GetTypeIndex(Entry.ScriptStruct);
			if (ExistingTypeIndex != InvalidTypeIndex)
			{
				Entry.Descriptor = GetTypeInfo(ExistingTypeIndex)->Descriptor;
			}
			else
			{
				// Get or create descriptor
				FReplicationStateDescriptorBuilder::FParameters Params;
				Entry.Descriptor = FReplicationStateDescriptorBuilder::CreateDescriptorForStruct(Struct, Params);
				bFoundNewScriptStructs = true;
			}
			
			if (Entry.Descriptor.IsValid())
			{
				UpdatedRegisteredTypes.Add(Entry);

				// trait 取并集中 ObjectReference 子位 —— 用作 CollectRefs 早退判定。
				CommonTraits |= (Entry.Descriptor->Traits & EReplicationStateTraits::HasObjectReference);
			}
			else
			{
				UE_LOG(LogIris, Error, TEXT("FPolymorphicNetSerializerScriptStructCache::InitForType Failed to create descriptor for type %s when building cache for base %s"), ToCStr(Struct->GetName()), ToCStr(InScriptStruct->GetName()));
			}
		}
	}

	// 稳定排序：按 ScriptStruct 名小写降序。两端只要 UClass 集合一致，TypeIndex 必然一致。
	UpdatedRegisteredTypes.Sort([](const FTypeInfo& A, const FTypeInfo& B) { return A.ScriptStruct->GetName().ToLower() > B.ScriptStruct->GetName().ToLower(); });

	// 上限校验：5-bit TypeIndex 仅可表达 31 个。
	if ((uint32)UpdatedRegisteredTypes.Num() > MaxRegisteredTypeCount)
	{
		UE_LOG(LogIris, Error, TEXT("FPolymorphicNetSerializerScriptStructCache::InitForType Too many types (%u of %u) of base %s"), UpdatedRegisteredTypes.Num(), MaxRegisteredTypeCount,  ToCStr(InScriptStruct->GetName()));
		check((uint32)RegisteredTypes.Num() <= MaxRegisteredTypeCount);
	}

	// Log if we updated the types
	const bool bWasUpdated = (UpdatedRegisteredTypes.Num() != RegisteredTypes.Num()) || bFoundNewScriptStructs;
	UE_CLOG(bWasUpdated, LogIris, Log,TEXT("FPolymorphicNetSerializerScriptStructCache::InitForType Updated CachedTypedIds for base %s, NumCachedTypeIDs: %d"), ToCStr(InScriptStruct->GetName()), UpdatedRegisteredTypes.Num());

	RegisteredTypes = MoveTemp(UpdatedRegisteredTypes);
}

}

namespace UE::Net::Private
{

// =============================================================================
// FPolymorphicStructNetSerializerInternal —— Alloc/Free/CollectRefs/Clone helper
// -----------------------------------------------------------------------------
// 模板 Impl（PolymorphicNetSerializerImpl.h）需要访问 Iris InternalContext 的私有
// 分配器和 ReplicationStateOperations 内部接口；为避免模板直接 #include 这些
// 私有头，这里提供 4 个静态函数把它们桥接出来。模板通过 protected 继承访问。
// =============================================================================

void* FPolymorphicStructNetSerializerInternal::Alloc(FNetSerializationContext& Context, SIZE_T Size, SIZE_T Alignment)
{
	return Context.GetInternalContext()->Alloc(Size, Alignment);
}

void FPolymorphicStructNetSerializerInternal::Free(FNetSerializationContext& Context, void* Ptr)
{
	return Context.GetInternalContext()->Free(Ptr);
}

void FPolymorphicStructNetSerializerInternal::CollectReferences(FNetSerializationContext& Context, FNetReferenceCollector& Collector, const FNetSerializerChangeMaskParam& OuterChangeMaskInfo, const uint8* RESTRICT SrcInternalBuffer,  const FReplicationStateDescriptor* Descriptor)
{
	return FReplicationStateOperationsInternal::CollectReferences(Context, Collector, OuterChangeMaskInfo, SrcInternalBuffer, Descriptor);
}

void FPolymorphicStructNetSerializerInternal::CloneQuantizedState(FNetSerializationContext& Context, uint8* RESTRICT DstInternalBuffer, const uint8* RESTRICT SrcInternalBuffer, const FReplicationStateDescriptor* Descriptor)
{
	return FReplicationStateOperationsInternal::CloneQuantizedState(Context, DstInternalBuffer, SrcInternalBuffer, Descriptor);
}

}
