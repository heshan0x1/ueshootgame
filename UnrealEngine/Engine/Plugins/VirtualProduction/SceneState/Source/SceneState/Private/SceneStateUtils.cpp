// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneStateUtils.h"
#include "Engine/Engine.h"
#include "Functions/SceneStateFunction.h"
#include "PropertyBindingPath.h"
#include "PropertyBindingTypes.h"
#include "SceneStateBindingCollection.h"
#include "SceneStateExecutionContext.h"
#include "SceneStateLog.h"
#include "SceneStateRange.h"
#include "StructUtils/InstancedStructContainer.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "UObject/PropertyVisitor.h"
#include "UObject/UnrealType.h"

namespace UE::SceneState
{

TArray<FStructView> GetStructViews(FInstancedStructContainer& InStructContainer, FSceneStateRange InRange)
{
	if (InRange.Count == 0)
	{
		return {};
	}

	if (!InStructContainer.IsValidIndex(InRange.Index) || !InStructContainer.IsValidIndex(InRange.GetLastIndex()))
	{
		UE_LOG(LogSceneState, Error, TEXT("GetStructViews failed. Range [%d, %d] out of bounds. Struct Container Num: %d")
			, InRange.Index
			, InRange.GetLastIndex()
			, InStructContainer.Num());
		return {};
	}

	TArray<FStructView> StructViews;
	StructViews.Reserve(InRange.Count);

	for (int32 Index = InRange.Index; Index <= InRange.GetLastIndex(); ++Index)
	{
		StructViews.Add(InStructContainer[Index]);
	}

	return StructViews;
}

TArray<FStructView> GetStructViews(FInstancedStructContainer& InStructContainer)
{
	FSceneStateRange Range;
	Range.Index = 0;
	Range.Count = InStructContainer.Num();
	return GetStructViews(InStructContainer, Range);
}

TArray<FConstStructView> GetConstStructViews(const FInstancedStructContainer& InStructContainer, FSceneStateRange InRange)
{
	FInstancedStructContainer& StructContainer = const_cast<FInstancedStructContainer&>(InStructContainer);
	return TArray<FConstStructView>(GetStructViews(StructContainer, InRange));
}

TArray<FConstStructView> GetConstStructViews(const FInstancedStructContainer& InStructContainer)
{
	FSceneStateRange Range;
	Range.Index = 0;
	Range.Count = InStructContainer.Num();
	return GetConstStructViews(InStructContainer, Range);
}

bool IsValidRange(FPropertyBindingIndex16 InBegin, FPropertyBindingIndex16 InEnd)
{
	return InBegin.IsValid() && InEnd.IsValid() && InBegin.Get() < InEnd.Get();
}

bool ApplyBatch(const FSceneStateExecutionContext& InContext, const FApplyBatchParams& InParams)
{
	const FPropertyBindingIndex16 BindingsBatch(InParams.BindingsBatch);
	if (!BindingsBatch.IsValid())
	{
		// Normal behavior if object is not a target of any binding, and so does not have a binding batch.
		return false;
	}

	const FSceneStateBindingCollection& BindingCollection = InContext.GetBindingCollection();

	bool bResult = true;

	const FPropertyBindingCopyInfoBatch& Batch = BindingCollection.GetBatch(BindingsBatch);
	check(InParams.TargetDataView.GetStruct() == Batch.TargetStruct.Get().Struct);

	// If there were valid functions found on setup, execute them now
	if (UE::SceneState::IsValidRange(Batch.PropertyFunctionsBegin, Batch.PropertyFunctionsEnd))
	{
		const FSceneStateRange FunctionRange = FSceneStateRange::MakeBeginEndRange(Batch.PropertyFunctionsBegin.Get(), Batch.PropertyFunctionsEnd.Get());

		for (uint16 FunctionIndex = FunctionRange.Index; FunctionIndex <= FunctionRange.GetLastIndex(); ++FunctionIndex)
		{
			if (const FSceneStateFunction* Function = InContext.FindFunction(FunctionIndex).GetPtr<const FSceneStateFunction>())
			{
				FStructView FunctionInstance = InContext.FindFunctionInstance(FunctionIndex);
				Function->Execute(InContext, FunctionInstance);
			}
		}
	}

	for (const FPropertyBindingCopyInfo& Copy : BindingCollection.GetBatchCopies(Batch))
	{
		const FPropertyBindingDataView SourceView = InContext.FindDataView(Copy.SourceDataHandle.Get<FSceneStateBindingDataHandle>());
		bResult &= BindingCollection.CopyProperty(Copy, SourceView, InParams.TargetDataView);
	}

	return bResult;
}

void* ResolveVisitedPath(const UScriptStruct* InRootObject, void* InRootData, const FPropertyVisitorPath& InPath)
{
	if (!InRootObject || !InRootData)
	{
		return nullptr;
	}

	const TArray<FPropertyVisitorInfo>& PathArray = InPath.GetPath();
	if (PathArray.IsEmpty())
	{
		return nullptr;
	}

	void* PropertyData = InRootObject->ResolveVisitedPathInfo(InRootData, PathArray[0]);
	for (int32 PathIndex = 1; PropertyData && PathIndex < PathArray.Num(); ++PathIndex)
	{
		const FPropertyVisitorInfo& PreviousInfo = PathArray[PathIndex - 1];
		PropertyData = PreviousInfo.Property->ResolveVisitedPathInfo(PropertyData, PathArray[PathIndex]);
	}
	return PropertyData;
}

void DiscardObject(UObject* InObjectToDiscard)
{
	if (InObjectToDiscard)
	{
		UObject* NewOuter = GetTransientPackage();
		FName UniqueName = MakeUniqueObjectName(NewOuter, InObjectToDiscard->GetClass(), *(TEXT("TRASH_") + InObjectToDiscard->GetName()));
		InObjectToDiscard->Rename(*UniqueName.ToString(), NewOuter, REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
		InObjectToDiscard->MarkAsGarbage();
	}
}

UObject* DiscardObject(UObject* InOuter, const TCHAR* InObjectName, TFunctionRef<void(UObject*)> InOnPreDiscardOldObject)
{
	if (UObject* OldObject = StaticFindObject(UObject::StaticClass(), InOuter, InObjectName))
	{
		InOnPreDiscardOldObject(OldObject);
		DiscardObject(OldObject);
	}
	return nullptr;
}

bool ReplaceObject(UObject*& InOutObject
	, UObject* InOuter
	, UClass* InClass
	, const TCHAR* InObjectName
	, const TCHAR* InContextName
	, TFunctionRef<void(UObject*)> InOnPreDiscardOldObject)
{
	if (!InOuter)
	{
		UE_LOG(LogSceneState, Error, TEXT("ReplaceObjectSafe did not take place (Context: %s). Outer is invalid."), InContextName);
		return false;
	}

	if (!InObjectName)
	{
		UE_LOG(LogSceneState, Error, TEXT("ReplaceObjectSafe did not take place (Context: %s). Object Name is invalid."), InContextName);
		return false;
	}

	if (InOutObject && InOutObject->GetName() != InObjectName)
	{
		UE_LOG(LogSceneState, Error, TEXT("ReplaceObjectSafe did not take place (Context: %s). Object Name '%s' does not match existing object name '%s'.")
			, InContextName
			, InObjectName
			, *InOutObject->GetName());
		return false;
	}

	if (InOutObject && InClass && InOutObject->GetClass() == InClass)
	{
		UE_LOG(LogSceneState, Log, TEXT("ReplaceObjectSafe did not take place (Context: %s). '%s' (%p) as is already of class %s.")
			, InContextName
			, *InOutObject->GetName()
			, InOutObject
			, *InClass->GetName());
		return false;
	}

	EObjectFlags MaskedOuterFlags = InOuter->GetMaskedFlags(RF_PropagateToSubObjects);

	UObject* OldObject = DiscardObject(InOuter, InObjectName, InOnPreDiscardOldObject);

	if (InClass)
	{
		InOutObject = NewObject<UObject>(InOuter, InClass, InObjectName, MaskedOuterFlags);

		if (OldObject && GEngine)
		{
			TMap<UObject*, UObject*> ReplacementMap;
			ReplacementMap.Add(OldObject, InOutObject);
			GEngine->NotifyToolsOfObjectReplacement(ReplacementMap);
		}
	}
	else
	{
		InOutObject = nullptr;
	}

	return true;
}

} // UE::SceneState
