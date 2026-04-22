// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGNaniteAssemblyStaticMeshBuilder.h"

#include "PCGAssetExporterUtils.h"
#include "PCGContext.h"
#include "PCGParamData.h"
#include "MeshSelectors/PCGMeshSelectorByAttribute.h"
#include "Metadata/PCGMetadataPartitionCommon.h"
#include "Utils/PCGLogErrors.h"

#include "NaniteAssemblyStaticMeshBuilder.h"
#include "NaniteDefinitions.h"
#include "Materials/MaterialInterface.h"

#define LOCTEXT_NAMESPACE "PCGNaniteAssemblyStaticMeshBuilderElement"

#if WITH_EDITOR
FName UPCGNaniteAssemblyStaticMeshBuilderSettings::GetDefaultNodeName() const
{
	return FName(TEXT("NaniteAssemblyStaticMeshBuilder"));
}

FText UPCGNaniteAssemblyStaticMeshBuilderSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("NodeTitle", "Nanite Assembly Static Mesh Builder");
}

FText UPCGNaniteAssemblyStaticMeshBuilderSettings::GetNodeTooltipText() const
{
	return LOCTEXT("NodeTooltip", "[EXPERIMENTAL] Create a Static Mesh using Nanite assemblies from the input point data. Points are expected to be in local coordinates.");
}
#endif // WITH_EDITOR

FPCGElementPtr UPCGNaniteAssemblyStaticMeshBuilderSettings::CreateElement() const
{
	return MakeShared<FPCGNaniteAssemblyStaticMeshBuilderElement>();
}

TArray<FPCGPinProperties> UPCGNaniteAssemblyStaticMeshBuilderSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Properties;
	Properties.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point, /*bInAllowMultipleConnections=*/false, /*bInAllowMultipleData=*/false).SetRequiredPin();
	return Properties;
}

TArray<FPCGPinProperties> UPCGNaniteAssemblyStaticMeshBuilderSettings::OutputPinProperties() const
{ 
	TArray<FPCGPinProperties> Properties;
	Properties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Param);	
	return Properties;
}

bool FPCGNaniteAssemblyStaticMeshBuilderElement::PrepareDataInternal(FPCGContext* InContext) const
{
	FPCGNaniteAssemblyStaticMeshBuilderContext* Context = static_cast<FPCGNaniteAssemblyStaticMeshBuilderContext*>(InContext);
	
	check(Context);
#if WITH_EDITOR
	const UPCGNaniteAssemblyStaticMeshBuilderSettings* Settings = InContext->GetInputSettings<UPCGNaniteAssemblyStaticMeshBuilderSettings>();
	check(Settings);

	TArray<FSoftObjectPath> ObjectsToLoad;

	if (!Context->bPartitionDone)
	{
		const TArray<FPCGTaggedData> Inputs = InContext->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
		if (Inputs.IsEmpty())
		{
			return true;
		}
	
		if (Inputs.Num() > 1)
		{
			PCGLog::InputOutput::LogFirstInputOnlyWarning(PCGPinConstants::DefaultInputLabel, InContext);	
		}

		const UPCGBasePointData* PointData = Cast<const UPCGBasePointData>(Inputs[0].Data);
		if (!PointData)
		{
			PCGLog::InputOutput::LogTypedDataNotFoundWarning(EPCGDataType::Point, PCGPinConstants::DefaultInputLabel, InContext);
			return true;
		}

		const int32 NumPoints = PointData->GetNumPoints();

		if (NumPoints == 0)
		{
			// Nothing to do
			return true;
		}

		// @todo_pcg: If a better API is available to query that number, we should use it
		constexpr int32 MaxNumInstances = NANITE_HIERARCHY_MAX_ASSEMBLY_TRANSFORMS;

		if (NumPoints > MaxNumInstances)
		{
			PCGLog::LogErrorOnGraph(FText::Format(LOCTEXT("TooManyTransforms", "Too many input points ({0}) ; Nanite assemblies have a limit of {1} instances. No assembly generated."), NumPoints, MaxNumInstances), InContext);
			return true;
		}

		TArray<const FPCGAttributePropertySelector> MeshAndMaterialOverrides;
		MeshAndMaterialOverrides.Reserve(Settings->MaterialOverrides.Num() + 1);
		MeshAndMaterialOverrides.Emplace(Settings->MeshAttribute.CopyAndFixLast(PointData));
		
		for (const FPCGAttributePropertyInputSelector& MaterialOverride : Settings->MaterialOverrides)
		{
			MeshAndMaterialOverrides.Emplace(MaterialOverride.CopyAndFixLast(PointData));
		}
		
		TArray<TArray<int32>> Partitions = PCGMetadataPartitionCommon::AttributeGenericPartition(PointData, MeshAndMaterialOverrides, Context);

		if (Partitions.IsEmpty())
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("PartitionFailed", "When partitioning attributes for each mesh and material, it fails. Make sure the attributes exist and are of the right type."), InContext);
			return true;
		}

		TArray<const FPCGMetadataAttribute<FSoftObjectPath>*> Attributes;
		Attributes.Reserve(MeshAndMaterialOverrides.Num());
		for (const FPCGAttributePropertySelector& Selector : MeshAndMaterialOverrides)
		{
			const FPCGMetadataDomain* MetadataDomain = PointData->ConstMetadata()->GetConstMetadataDomainFromSelector(Selector);
			if (!MetadataDomain)
			{
				PCGLog::Metadata::LogInvalidMetadataDomain(Selector, InContext);
				return true;
			}

			const FPCGMetadataAttribute<FSoftObjectPath>* Attribute = MetadataDomain->GetConstTypedAttribute<FSoftObjectPath>(Selector.GetAttributeName());
			if (!Attribute)
			{
				PCGLog::Metadata::LogFailToGetAttributeError(Selector, InContext);
				return true;
			}
			
			Attributes.Emplace(Attribute);
		}
		
		TConstPCGValueRange<int64> MetadataEntries = PointData->GetConstMetadataEntryValueRange();

		for (TArray<int32>& Indices : Partitions)
		{
			if (!ensure(!Indices.IsEmpty()))
			{
				continue;
			}

			const PCGMetadataEntryKey EntryKey = MetadataEntries[Indices[0]];

			auto& NewEntry = Context->Mapping.Emplace_GetRef();

			// 1. Set the Mesh
			FSoftObjectPath Mesh = Attributes[0]->GetValueFromItemKey(EntryKey);
			if (!Mesh.IsNull())
			{
				ObjectsToLoad.AddUnique(Mesh);
			}
			
			NewEntry.Get<0>() = MoveTemp(Mesh);

			// 2. Set the Material Overrides
			NewEntry.Get<1>().Reserve(Attributes.Num() - 1);
			for (int32 i = 1; i < Attributes.Num(); ++i)
			{
				FSoftObjectPath Material = Attributes[i]->GetValueFromItemKey(EntryKey);

				if (!Material.IsNull())
				{
					ObjectsToLoad.AddUnique(Material);
				}
				
				NewEntry.Get<1>().Emplace(MoveTemp(Material));
			}

			// 3. Set the point indices
			NewEntry.Get<2>() = MoveTemp(Indices);
		}

		Context->bPartitionDone = true;
	}

	if (!Context->WasLoadRequested() && !ObjectsToLoad.IsEmpty())
	{
		if (!Context->RequestResourceLoad(Context, MoveTemp(ObjectsToLoad), /*bAsynchronous=*/!Settings->bSynchronousLoad))
		{
			return false;
		}
	}

	Context->bPrepareValid = true;
#else
	PCGLog::LogWarningOnGraph(LOCTEXT("CannotExportInNonEditor", "Can't export an asset in non editor build."), InContext);
#endif // WITH_EDITOR
	
	return true;
}

bool FPCGNaniteAssemblyStaticMeshBuilderElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGNaniteAssemblyStaticMeshBuilderElement::Execute);

	FPCGNaniteAssemblyStaticMeshBuilderContext* Context = static_cast<FPCGNaniteAssemblyStaticMeshBuilderContext*>(InContext);
	
	check(Context);

	if (!Context->bPrepareValid)
	{
		return true;
	}

	// We only support writing to an asset in editor build.
#if WITH_EDITOR
	const UPCGNaniteAssemblyStaticMeshBuilderSettings* Settings = InContext->GetInputSettings<UPCGNaniteAssemblyStaticMeshBuilderSettings>();
	check(Settings);

	const TArray<FPCGTaggedData> Inputs = InContext->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	const UPCGBasePointData* PointData = CastChecked<const UPCGBasePointData>(Inputs[0].Data);
	TConstPCGValueRange<FTransform> PointTransformRange = PointData->GetConstTransformValueRange();

	UObject* OutObject = nullptr;
	
	UPackage* OutPackage = UPCGAssetExporterUtils::CreateAsset<UStaticMesh>(Settings->ExportParams, [&PointTransformRange, Context, &OutObject, Settings](const FString&, UObject* Asset)
	{
		// Export options, make sure we don't emit a transaction.

		bool bSuccess = false;
		UNaniteAssemblyStaticMeshBuilder* Builder = FPCGContext::NewObject_AnyThread<UNaniteAssemblyStaticMeshBuilder>(Context);
		UStaticMesh* OutMesh = CastChecked<UStaticMesh>(Asset);
			
		if (!Builder->BeginAssemblyBuild(OutMesh))
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("BeginAssemblyFailed", "Failed to initialize the assembly builder."));
			return false;
		}

		for (const auto& [Mesh, Materials, Indices] : Context->Mapping)
		{
			const UStaticMesh* InMesh = Mesh.Get();
			if (!InMesh)
			{
				PCGLog::LogWarningOnGraph(LOCTEXT("InvalidMesh", "Mesh asset was null or not a static mesh. Skipped."));
				continue;
			}
			
			TArray<UMaterialInterface*> MaterialOverrides;
			MaterialOverrides.Reserve(Materials.Num());
			Algo::Transform(Materials, MaterialOverrides, [&PointTransformRange](const TSoftObjectPtr<UMaterialInterface>& Material) { return Material.Get(); });
			
			TArray<FTransform, TInlineAllocator<128>> Transforms;
			Transforms.Reserve(Indices.Num());
			Algo::Transform(Indices, Transforms, [&PointTransformRange](const int32 Index) { return PointTransformRange[Index]; });

			FNaniteAssemblyMaterialMergeOptions Options{};
			Options.MaterialOverrides = MoveTemp(MaterialOverrides);

			if (!Builder->AddAssemblyParts(InMesh, Transforms, Options))
			{
				PCGLog::LogWarningOnGraph(LOCTEXT("AddAssemblyFailed", "Failed to add the parts for the assembly builder. Skipped."));
			}
		}
			
		if (!Builder->FinishAssemblyBuild(OutMesh))
		{
			PCGLog::LogErrorOnGraph(LOCTEXT("EndAssemblyFailed", "Failed to finalize the assembly builder."));
			return false;
		}
			
		OutObject = Asset;
		return true;
	}, InContext);

	if (!OutPackage || !OutObject)
	{
		return true;
	}

	UPCGParamData* OutParamData = FPCGContext::NewObject_AnyThread<UPCGParamData>(Context);
	OutParamData->Metadata->CreateAttribute<FSoftObjectPath>(TEXT("AssetPath"), FSoftObjectPath(OutObject), false, false);
	OutParamData->Metadata->AddEntry();

	InContext->OutputData.TaggedData.Emplace_GetRef(Inputs[0]).Data = OutParamData;
#endif // WITH_EDITOR
	
	return true;
}

#undef LOCTEXT_NAMESPACE
