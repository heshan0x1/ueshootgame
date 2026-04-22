// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVFoliagePaletteSettings.h"

#include "ProceduralVegetationModule.h"
#include "DataTypes/PVData.h"
#include "DataTypes/PVMeshData.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGStaticMeshResourceData.h"
#include "Facades/PVFoliageFacade.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Helpers/PCGHelpers.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Helpers/PVAnalyticsHelper.h"
#include "Implementations/PVFoliage.h"

#define LOCTEXT_NAMESPACE "PVFoliagePaletteSettings"

#if WITH_EDITOR
FText UPVFoliagePaletteSettings::GetDefaultNodeTitle() const 
{ 
	return LOCTEXT("NodeTitle", "Foliage Palette");
}

FText UPVFoliagePaletteSettings::GetNodeTooltipText() const 
{ 
	return LOCTEXT("NodeTooltip", 
		"Allows the user to visualize and optionally change the foliage meshes. "
		"Foliage mesh data is loaded from the Procedrual Vegetation preset. "
		"The user can replace or remove foliage meshes. The placement of folliage meshes is driven by the procedural vegetaiton preset and cannot be modifed"
		"\n\nPress Ctrl + L to lock/unlock node output"
	);
}

void UPVFoliagePaletteSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property
		&& PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPVFoliagePaletteSettings, bOverrideFoliage))
	{
		DirtyCache();
	}
}

void UPVFoliagePaletteSettings::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	FProperty* Property = PropertyChangedEvent.Property;
	FProperty* MemberProperty = nullptr;

	if (PropertyChangedEvent.PropertyChain.GetActiveMemberNode())
	{
		MemberProperty = PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue();
	}

	if (MemberProperty && Property)
	{
		if (MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UPVFoliagePaletteSettings, FoliageInfos))
		{
			FName Name = Property->GetFName();
			
			int Index = PropertyChangedEvent.GetArrayIndex(Name.ToString());

			if (Index >= 0 && FoliageInfos.Num() > Index)
			{
				if (!FoliageInfos[Index].Mesh.IsNull())
				{
					FString MeshPath = FoliageInfos[Index].Mesh.ToString();

					PV::Analytics::SendFoliageMeshChangeEvent(MeshPath);	
				}
			}

			// For backwards compatibility 
			FoliageMeshes.Empty();

			for (int i = 0; i < FoliageInfos.Num(); i++)
			{
				FoliageMeshes.Add(FoliageInfos[i].Mesh);
			}
		}
	}
}

#endif

FPCGDataTypeIdentifier UPVFoliagePaletteSettings::GetInputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier{ FPVDataTypeInfoMesh::AsId() };
}

FPCGDataTypeIdentifier UPVFoliagePaletteSettings::GetOutputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier{ FPVDataTypeInfoMesh::AsId() };
}

FPCGElementPtr UPVFoliagePaletteSettings::CreateElement() const
{
	return MakeShared<FPVFoliageElement>();
}

void UPVFoliagePaletteSettings::PostLoad()
{
	Super::PostLoad();

	if (bOverrideFoliage)
	{
		if (FoliageInfos.Num() == 0)
		{
			for (int32 i = 0; i < FoliageMeshes.Num(); i++)
			{
				FoliageInfos.Emplace(FoliageMeshes[i]);
			}
		}
	}
}

bool FPVFoliageElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPVFoliageElement::Execute);

	check(InContext);

	const UPVFoliagePaletteSettings* Settings = InContext->GetInputSettings<UPVFoliagePaletteSettings>();
	check(Settings);

	UPVFoliagePaletteSettings* const MutableSettings = const_cast<UPVFoliagePaletteSettings*>(Settings);
	check(MutableSettings);

	const TArray<FPCGTaggedData>& Inputs = InContext->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
	if (!Inputs.IsEmpty())
	{
		if (const UPVMeshData* InputData = Cast<UPVMeshData>(Inputs[0].Data))
		{
			FManagedArrayCollection Collection = InputData->GetCollection();

			UPVMeshData* OutManagedArrayCollectionData = FPCGContext::NewObject_AnyThread<UPVMeshData>(InContext);

			PV::Facades::FFoliageFacade Facade(Collection);

			if (!Settings->bOverrideFoliage)
			{
				MutableSettings->FoliageInfos.Empty();
				MutableSettings->bOverrideFoliage = false;
				for (int32 FoliageIndex = 0; FoliageIndex < Facade.NumFoliageInfo(); ++FoliageIndex)
				{
					FPVFoliageInfo FoliageInfo = Facade.GetFoliageInfo(FoliageIndex);
					MutableSettings->FoliageInfos.Emplace(FoliageInfo);
				}
#if WITH_EDITOR
				MutableSettings->PostEditChange();
#endif
			}

			const int32 IterationLimit = FMath::Min(Settings->FoliageInfos.Num(), Facade.NumFoliageInfo());
			TArray<FPVFoliageInfo> FoliageInfos;
			
			for (int32 i = 0; i < IterationLimit; ++i)
			{
				FoliageInfos.Emplace(Settings->FoliageInfos[i]);
			}

			if (Settings->FoliageInfos.Num() > Facade.NumFoliageInfo())
			{
				UE_LOG(LogProceduralVegetation, Warning,
				       TEXT("Num of Foliage meshes specified is greater than the number of available entries, ignoring the last %d entries"),
				       Settings->FoliageInfos.Num() - Facade.NumFoliageInfo());
			}
			else if (Settings->FoliageInfos.Num() < Facade.NumFoliageInfo())
			{
				if (Settings->FoliageInfos.Num() > 0)
				{
					UE_LOG(LogProceduralVegetation, Warning,
					       TEXT("Num of Foliage meshes specified is less than the number of available entries, repeating the last entry"));

					for (int32 i = Settings->FoliageInfos.Num(); i < Facade.NumFoliageInfo(); ++i)
					{
						FoliageInfos.Emplace(Settings->FoliageInfos.Last());
					}
				}
			}

			Facade.SetFoliageInfos(FoliageInfos);

			OutManagedArrayCollectionData->Initialize(MoveTemp(Collection));
			InContext->OutputData.TaggedData.Emplace(OutManagedArrayCollectionData);
		}
		else
		{
			PCGLog::InputOutput::LogInvalidInputDataError(InContext);
			return true;
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE