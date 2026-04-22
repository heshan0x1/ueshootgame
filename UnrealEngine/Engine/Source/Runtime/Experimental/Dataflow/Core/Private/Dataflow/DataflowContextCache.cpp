// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowContextCache.h"
#include "UObject/StructOnScope.h"

namespace UE::Dataflow
{
	//////////////////////////////////////////////////////////////////////////////////////////////////////

	const void* FContextCacheElementReference::GetUntypedData(const IContextCacheStore& Context, const FProperty* InProperty) const
	{
		if (const TUniquePtr<FContextCacheElementBase>* Cache = Context.FindCacheElement(DataKey))
		{
			return (*Cache)->GetUntypedData(Context, InProperty);
		}
		return nullptr;
	}

	bool FContextCacheElementReference::IsArray(const IContextCacheStore& Context) const
	{
		if (const TUniquePtr<FContextCacheElementBase>* Cache = Context.FindCacheElement(DataKey))
		{
			return (*Cache)->IsArray(Context);
		}
		return false;
	}

	int32 FContextCacheElementReference::GetNumArrayElements(const IContextCacheStore& Context) const
	{
		if (const TUniquePtr<FContextCacheElementBase>* Cache = Context.FindCacheElement(DataKey))
		{
			return (*Cache)->GetNumArrayElements(Context);
		}
		return 0;
	}

	TUniquePtr<FContextCacheElementBase> FContextCacheElementReference::CreateFromArrayElement(const IContextCacheStore& Context, int32 Index, const FProperty* InProperty, const FGuid& InNodeGuid, uint32 InNodeHash, const FTimestamp& InTimestamp) const
	{
		if (const TUniquePtr<FContextCacheElementBase>* Cache = Context.FindCacheElement(DataKey))
		{
			return (*Cache)->CreateFromArrayElement(Context, Index, InProperty, InNodeGuid, InNodeHash, InTimestamp);
		}
		return {};
	}

	TUniquePtr<FContextCacheElementBase> FContextCacheElementReference::CreateArrayFromElement(const IContextCacheStore& Context, const FProperty* InProperty, const FGuid& InNodeGuid, uint32 InNodeHash, const FTimestamp& InTimestamp) const
	{
		if (const TUniquePtr<FContextCacheElementBase>* Cache = Context.FindCacheElement(DataKey))
		{
			return (*Cache)->CreateArrayFromElement(Context, InProperty, InNodeGuid, InNodeHash, InTimestamp);
		}
		return {};
	}

	TUniquePtr<FContextCacheElementBase> FContextCacheElementReference::Clone(const IContextCacheStore& Context) const
	{
		if (const TUniquePtr<FContextCacheElementBase>* ReferencedCacheEntry = Context.FindCacheElement(DataKey))
		{
			if (ReferencedCacheEntry && ReferencedCacheEntry->IsValid())
			{
				return (*ReferencedCacheEntry)->Clone(Context);
			}
		}
		return MakeUnique<FContextCacheElementNull>(GetNodeGuid(), GetProperty(), GetNodeHash(), GetTimestamp());
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////

	const void* FContextCacheElementNull::GetUntypedData(const IContextCacheStore& Context, const FProperty* PropertyIn) const
	{
		return nullptr;
	}

	bool FContextCacheElementNull::IsArray(const IContextCacheStore& Context) const
	{
		return false; // we have no way to tell at this point , null entry means that we don't have a value and we let the requester of teh value to get a default value 
	}

	int32 FContextCacheElementNull::GetNumArrayElements(const IContextCacheStore& Context) const
	{
		return 0;
	}

	TUniquePtr<FContextCacheElementBase> FContextCacheElementNull::CreateFromArrayElement(const IContextCacheStore& Context, int32 Index, const FProperty* InProperty, const FGuid& InNodeGuid, uint32 InNodeHash, const FTimestamp& InTimestamp) const
	{
		return {};
	}

	TUniquePtr<FContextCacheElementBase> FContextCacheElementNull::CreateArrayFromElement(const IContextCacheStore& Context, const FProperty* InProperty, const FGuid& InNodeGuid, uint32 InNodeHash, const FTimestamp& InTimestamp) const
	{
		return {};
	}

	TUniquePtr<FContextCacheElementBase> FContextCacheElementNull::Clone(const IContextCacheStore& Context) const
	{
		return MakeUnique<FContextCacheElementNull>(GetNodeGuid(), GetProperty(), GetNodeHash(), GetTimestamp());
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////

	FContextCacheElementUStruct::FContextCacheElementUStruct(FGuid InNodeGuid, const FProperty* InProperty, const FConstStructView& StructView, uint32 InNodeHash, FTimestamp Timestamp)
		: FContextCacheElementBase(EType::CacheElementUStruct, InNodeGuid, InProperty, InNodeHash, Timestamp)
		, InstancedStruct(StructView)
	{}

	FContextCacheElementUStruct::FContextCacheElementUStruct(const FContextCacheElementUStruct& Other)
		: FContextCacheElementBase(EType::CacheElementUStruct, Other.GetNodeGuid(), Other.GetProperty(), Other.GetNodeHash(), Other.GetTimestamp())
		, InstancedStruct(Other.InstancedStruct)
	{}

	const void* FContextCacheElementUStruct::GetUntypedData(const IContextCacheStore& /*Context*/, const FProperty* /*PropertyIn*/) const
	{
		return InstancedStruct.GetMemory();
	}

	bool FContextCacheElementUStruct::IsArray(const IContextCacheStore& Context) const
	{
		return false;
	}

	int32 FContextCacheElementUStruct::GetNumArrayElements(const IContextCacheStore& Context) const
	{
		return 0;
	}

	TUniquePtr<FContextCacheElementBase> FContextCacheElementUStruct::CreateFromArrayElement(const IContextCacheStore& Context, int32 Index, const FProperty* InProperty, const FGuid& InNodeGuid, uint32 InNodeHash, const FTimestamp& InTimestamp) const
	{
		return {};
	}

	TUniquePtr<FContextCacheElementBase> FContextCacheElementUStruct::CreateArrayFromElement(const IContextCacheStore& Context, const FProperty* InProperty, const FGuid& InNodeGuid, uint32 InNodeHash, const FTimestamp& InTimestamp) const
	{
		return MakeUnique<FContextCacheElementUStructArray>(InNodeGuid, InProperty, FConstStructView(InstancedStruct), InNodeHash, InTimestamp);
	}

	TUniquePtr<FContextCacheElementBase> FContextCacheElementUStruct::Clone(const IContextCacheStore& Context) const
	{
		return MakeUnique<FContextCacheElementUStruct>(*this);
	}

	void FContextCacheElementUStruct::AddReferencedObjects(FReferenceCollector& Collector)
	{
		InstancedStruct.AddStructReferencedObjects(Collector);
	}

	FString FContextCacheElementUStruct::GetReferencerName() const
	{
		return TEXT("FContextCacheElementUStruct");
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////

	const void* FContextCacheElementUStructArray::GetUntypedData(const IContextCacheStore& /*Context*/, const FProperty* /*PropertyIn*/) const
	{
		return InstancedStructArray.GetMemory();
	}

	bool FContextCacheElementUStructArray::IsArray(const IContextCacheStore& Context) const
	{
		return true;
	}

	int32 FContextCacheElementUStructArray::GetNumArrayElements(const IContextCacheStore& Context) const
	{
		return InstancedStructArray.Num();
	}

	TUniquePtr<FContextCacheElementBase> FContextCacheElementUStructArray::CreateFromArrayElement(const IContextCacheStore& Context, int32 Index, const FProperty* InProperty, const FGuid& InNodeGuid, uint32 InNodeHash, const FTimestamp& InTimestamp) const
	{
		if (InstancedStructArray.IsValidIndex(Index))
		{
			const FConstStructView ElementStructView = InstancedStructArray.GetScriptViewAt(Index);
			return MakeUnique<FContextCacheElementUStruct>(GetNodeGuid(), GetProperty(), ElementStructView, GetNodeHash(), GetTimestamp());
		}
		return {};
	}

	TUniquePtr<FContextCacheElementBase> FContextCacheElementUStructArray::CreateArrayFromElement(const IContextCacheStore& Context, const FProperty* InProperty, const FGuid& InNodeGuid, uint32 InNodeHash, const FTimestamp& InTimestamp) const
	{
		return {}; // can't make arrays of arrays
	}

	TUniquePtr<FContextCacheElementBase> FContextCacheElementUStructArray::Clone(const IContextCacheStore& Context) const
	{
		return MakeUnique<FContextCacheElementUStructArray>(*this);
	}

	void FContextCacheElementUStructArray::AddReferencedObjects(FReferenceCollector& Collector)
	{
		InstancedStructArray.AddReferencedObjects(Collector);
	}

	FString FContextCacheElementUStructArray::GetReferencerName() const
	{
		return TEXT("FContextCacheElementUStructArray");
	}

	FConstStructArrayView FContextCacheElementUStructArray::GetStructArrayView() const
	{
		return InstancedStructArray.GetScriptStruct() ?
			FConstStructArrayView(*InstancedStructArray.GetScriptStruct(), InstancedStructArray.GetData(), InstancedStructArray.Num()) :
			FConstStructArrayView();
	}

	FContextCacheElementUStructArray::FInstancedStructArray::FInstancedStructArray(const UScriptStruct* const InScriptStruct)
		: ScriptStruct(InScriptStruct)
	{}

	FContextCacheElementUStructArray::FInstancedStructArray::FInstancedStructArray(const FConstStructView& StructView)
		: ScriptStruct(StructView.GetScriptStruct())
	{
		InitFromRawData(StructView.GetMemory(), 1);
	}

	FContextCacheElementUStructArray::FInstancedStructArray::FInstancedStructArray(const FConstStructArrayView& StructArrayView)
		: ScriptStruct(StructArrayView.GetScriptStruct())
	{
		InitFromRawData(StructArrayView.GetData(), StructArrayView.Num());
	}
	
	FContextCacheElementUStructArray::FInstancedStructArray::~FInstancedStructArray()
	{
		if (ScriptStruct)
		{
			ScriptStruct->DestroyStruct(GetData(), Num());
			GetAllocatorInstance().ResizeAllocation(ArrayMax, 0, GetStructureSize(), ScriptStruct->GetMinAlignment());
		}
	}

	const int32 FContextCacheElementUStructArray::FInstancedStructArray::GetStructureSize() const
	{
		return FMath::Max(1, ScriptStruct? ScriptStruct->GetStructureSize(): 0);
	}

	const UScriptStruct* FContextCacheElementUStructArray::FInstancedStructArray::GetScriptStruct() const
	{
		return ScriptStruct;
	}

	FConstStructView FContextCacheElementUStructArray::FInstancedStructArray::GetScriptViewAt(int32 Index) const
	{
		if (IsValidIndex(Index))
		{
			return FConstStructView(ScriptStruct, GetData() + (GetStructureSize() * Index));
		}
		return FConstStructView();
	}

	const void* FContextCacheElementUStructArray::FInstancedStructArray::GetMemory() const
	{
		return GetData();
	}

	void FContextCacheElementUStructArray::FInstancedStructArray::InitFromRawData(const void* Data, const int32 Num)
	{
		if (ScriptStruct)  // Null if the array has 0 element
		{
			ArrayNum = Num;
			// Note: SetNumUnsafeInternal cannot expand the array, but we use it here to make sure e.g. slack tracking is called (if enabled)
			SetNumUnsafeInternal(Num);
			ArrayMax = Num;
			GetAllocatorInstance().ResizeAllocation(0, Num, GetStructureSize(), ScriptStruct->GetMinAlignment());
			ScriptStruct->InitializeStruct(GetData(), Num);
			ScriptStruct->CopyScriptStruct(GetData(), Data, Num);
		}
	}

	void FContextCacheElementUStructArray::FInstancedStructArray::AddReferencedObjects(FReferenceCollector& Collector)
	{
		for(int32 Index = 0; IsValidIndex(Index); ++Index)
		{
			FStructOnScope ScopeStruct(ScriptStruct, GetData() + (GetStructureSize() * Index));
			ScopeStruct.AddReferencedObjects(Collector);
		}
	}

	//////////////////////////////////////////////////////////////////////////////////////////////////////

	FContextValue::FContextValue(IContextCacheStore& InContext, FContextCacheKey InCacheKey)
		: Context(InContext)
		, CacheKey(InCacheKey)
	{}

	FContextValue::FContextValue(IContextCacheStore& InContext, TUniquePtr<FContextCacheElementBase>&& InCacheElement)
		: Context(InContext)
		, CacheKey(0)
		, CacheElement(MoveTemp(InCacheElement))
	{}

	int32 FContextValue::Num() const
	{
		if (const FContextCacheElementBase* CacheEntry = GetCacheEntry())
		{
			return CacheEntry->GetNumArrayElements(Context);
		}
		return 0;
	}

	FContextValue FContextValue::GetAt(int32 Index) const
	{
		if (const FContextCacheElementBase* CacheEntry = GetCacheEntry())
		{
			TUniquePtr<FContextCacheElementBase> NewCacheElement = CacheEntry->CreateFromArrayElement(Context, Index, CacheEntry->GetProperty(), CacheEntry->GetNodeGuid(), CacheEntry->GetNodeHash(), CacheEntry->GetTimestamp());
			return FContextValue(Context, MoveTemp(NewCacheElement));
		}
		return FContextValue(Context, 0);
	}

	FContextValue FContextValue::ToArray() const
	{
		if (const FContextCacheElementBase* CacheEntry = GetCacheEntry())
		{
			// arrays return themselves
			if (CacheEntry->IsArray(Context))
			{
				if (CacheElement)
				{
					return FContextValue(Context, CacheEntry->Clone(Context));
				}
				// simply return the reference to the original cached array 
				return FContextValue(Context, CacheKey);
			}

			// actually convert to an array 
			TUniquePtr<FContextCacheElementBase> NewCacheElement = CacheEntry->CreateArrayFromElement(Context, CacheEntry->GetProperty(), CacheEntry->GetNodeGuid(), CacheEntry->GetNodeHash(), CacheEntry->GetTimestamp());
			return FContextValue(Context, MoveTemp(NewCacheElement));
		}
		return FContextValue(Context, 0);
	}

	const FContextCacheElementBase* FContextValue::GetCacheEntry() const
	{
		if (CacheElement)
		{
			return CacheElement.Get();
		}
		if (const TUniquePtr<FContextCacheElementBase>* CacheEntry = Context.FindCacheElement(CacheKey))
		{
			return CacheEntry->Get();
		}
		return nullptr;
	}
};

FArchive& operator<<(FArchive& Ar, UE::Dataflow::FTimestamp& ValueIn)
{
	Ar << ValueIn.Value;
	Ar << ValueIn.Invalid;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, UE::Dataflow::FContextCache& ValueIn)
{
	ValueIn.Serialize(Ar);
	return Ar;
}




