// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialIRModule.h"
#include "Materials/MaterialIR.h"
#include "Materials/MaterialIRTypes.h"
#include "Materials/MaterialIRInternal.h"
#include "Materials/Material.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "ParameterCollection.h"

#if WITH_EDITOR

FMaterialIRModule::FMaterialIRModule()
{
}

FMaterialIRModule::~FMaterialIRModule()
{
	Empty();
}

void FMaterialIRModule::Empty()
{
	Errors.Empty();
	ShadingModelsFromCompilation = {};
	FunctionHLSLs.Empty();
	ParameterCollections.Empty();
	EnvironmentDefines.Empty();
	UserStrings.Empty();
	ParameterIdToData.Empty();
	ParameterInfoToId.Empty();
	Statistics = {};
	MIR::ZeroArray(TArrayView<MIR::FValue*>{ PropertyValues, MP_MAX });
	EntryPoints.Empty();
	Values.Empty();
	CompilationOutput = {};
	Allocator.Flush();

	// Initialize statistics used external input usage mask.
	for (int i = 0; i < MIR::NumStages; ++i)
	{
		Statistics.ExternalInputUsedMask[i].Init(false, (int)MIR::EExternalInput::Count);
	}
}

int32 FMaterialIRModule::AddEntryPoint(FStringView Name, MIR::EStage Stage, int32 NumOutputs)
{
	uint32 Index = EntryPoints.Num();

	EntryPoints.Push({
		.Name = InternString(Name),
		.Stage = Stage,
		.RootBlock = {},
		.Outputs = AllocateArray<MIR::FValue*>(NumOutputs),
	});

	MIR::ZeroArray(EntryPoints.Last().Outputs);

	return Index;
}

FStringView FMaterialIRModule::InternString(FStringView InString)
{
	// Allocate the buffer to contain the string
	FStringView::ElementType* String = (FStringView::ElementType*)Allocator.PushBytes(InString.NumBytes() + sizeof(FStringView::ElementType), alignof(FStringView::ElementType));

	// Copy the string over to the interned location
	FMemory::Memcpy(String, InString.GetData(), InString.NumBytes());

	// Mark the null-character at the end
	String[InString.Len()] = 0;

	return { String, InString.Len() };
}

void FMaterialIRModule::AddError(UMaterialExpression* Expression, FString Message)
{
	Errors.Push({ Expression, MoveTemp(Message) });
}

int32 FMaterialIRModule::FindOrAddParameterCollection(UMaterialParameterCollection* ParameterCollection)
{
	int32 CollectionIndex = ParameterCollections.Find(ParameterCollection);

	if (CollectionIndex == INDEX_NONE)
	{
		if (ParameterCollections.Num() >= MaxNumParameterCollectionsPerMaterial)
		{
			return INDEX_NONE;
		}
		else
		{
			ParameterCollections.Add(ParameterCollection);
			CollectionIndex = ParameterCollections.Num() - 1;
		}
	}

	return CollectionIndex;
}

void FMaterialIRModule::AddShadingModel(EMaterialShadingModel InShadingModel)
{
	ShadingModelsFromCompilation.AddShadingModel(InShadingModel);
}

bool FMaterialIRModule::IsMaterialPropertyUsed(EMaterialProperty InProperty) const
{
	return PropertyValues[InProperty] && !PropertyValues[InProperty]->EqualsConstant(FMaterialAttributeDefinitionMap::GetDefaultValue(InProperty));
}

#endif // #if WITH_EDITOR
