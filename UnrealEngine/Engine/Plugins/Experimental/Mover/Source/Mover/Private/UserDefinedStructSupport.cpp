// Copyright Epic Games, Inc. All Rights Reserved.


#include "UserDefinedStructSupport.h"
#include "StructUtils/UserDefinedStruct.h"
#include "MoverLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UserDefinedStructSupport)

#define LOCTEXT_NAMESPACE "MoverUDSInstances"


// TODO: Consider different rules for interpolation/merging/reconciliation checks. 
// This could be accomplished via cvars / Mover settings / per-type metadata , etc.

bool FMoverUserDefinedDataStruct::ShouldReconcile(const FMoverDataStructBase& AuthorityState) const
{
	const FMoverUserDefinedDataStruct& TypedAuthority = static_cast<const FMoverUserDefinedDataStruct&>(AuthorityState);

	check(TypedAuthority.StructInstance.GetScriptStruct() == this->StructInstance.GetScriptStruct());

	return !StructInstance.Identical(&TypedAuthority.StructInstance, EPropertyPortFlags::PPF_DeepComparison);
}

void FMoverUserDefinedDataStruct::Interpolate(const FMoverDataStructBase& From, const FMoverDataStructBase& To, float LerpFactor)
{
	const FMoverUserDefinedDataStruct& PrimarySource = static_cast<const FMoverUserDefinedDataStruct&>((LerpFactor < 0.5f) ? From : To);

	// copy all properties from the heaviest-weighted source rather than interpolate
	StructInstance = PrimarySource.StructInstance;
}

void FMoverUserDefinedDataStruct::Merge(const FMoverDataStructBase& From)
{
	const FMoverUserDefinedDataStruct& TypedFrom = static_cast<const FMoverUserDefinedDataStruct&>(From);

	check(TypedFrom.StructInstance.GetScriptStruct() == this->StructInstance.GetScriptStruct());

	// Merging is typically only done for inputs. Let's make the assumption that boolean inputs should be OR'd so we never miss any digital inputs.

	if (const UScriptStruct* UdsScriptStruct = TypedFrom.StructInstance.GetScriptStruct())
	{
		uint8* ThisInstanceMemory = StructInstance.GetMutableMemory();
		const uint8* FromInstanceMemory = TypedFrom.StructInstance.GetMemory();

		for (TFieldIterator<FBoolProperty> BoolProperty(UdsScriptStruct); BoolProperty; ++BoolProperty)
		{
			bool bMergedBool = BoolProperty->GetPropertyValue(ThisInstanceMemory);

			if (!bMergedBool)
			{
				bMergedBool |= BoolProperty->GetPropertyValue(FromInstanceMemory);

				if (bMergedBool)
				{
					BoolProperty->SetPropertyValue(ThisInstanceMemory, bMergedBool);
				}
			}
		}
	}
}

FMoverDataStructBase* FMoverUserDefinedDataStruct::Clone() const
{
	FMoverUserDefinedDataStruct* CopyPtr = new FMoverUserDefinedDataStruct(*this);
	return CopyPtr;
}

bool FMoverUserDefinedDataStruct::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bool bSuperSuccess, bStructSuccess;

	Super::NetSerialize(Ar, Map, bSuperSuccess);
	StructInstance.NetSerialize(Ar, Map, bStructSuccess);

	bOutSuccess = bSuperSuccess && bStructSuccess;

	return true;
}


void FMoverUserDefinedDataStruct::ToString(FAnsiStringBuilderBase& Out) const
{
	Super::ToString(Out);

	// TODO: add property-wise concatenated string output
}

const UScriptStruct* FMoverUserDefinedDataStruct::GetDataScriptStruct() const
{
	return StructInstance.GetScriptStruct();
}


#undef LOCTEXT_NAMESPACE
