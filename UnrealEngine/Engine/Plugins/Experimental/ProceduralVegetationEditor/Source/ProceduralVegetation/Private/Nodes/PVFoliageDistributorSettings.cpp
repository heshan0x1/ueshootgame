// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVFoliageDistributorSettings.h"
#include "DataTypes/PVData.h"
#include "DataTypes/PVMeshData.h"
#include "DataTypes/PVFoliageMeshData.h"
#include "Data/PCGBasePointData.h"
#include "Facades/PVFoliageConditionFacade.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Helpers/PCGHelpers.h"
#include "Helpers/PCGSettingsHelpers.h"

#define LOCTEXT_NAMESPACE "PVFoliageDistributorSettings"

#if WITH_EDITOR
FText UPVFoliageDistributorSettings::GetDefaultNodeTitle() const 
{ 
	return LOCTEXT("NodeTitle", "Foliage Distributor"); 
}

FText UPVFoliageDistributorSettings::GetNodeTooltipText() const 
{ 
	return LOCTEXT("NodeTooltip", 
		"Allows the user to customize the distribution of the foliage. \n"
		"Ethylene plays a key role in plant aging and leaf abscission (falling).\n"
		"EthyleneThreshold: Defines how sensitive the procedural system is to \"ethylene levels.\"\n"
		"\tHigher Threshold = more buds/branches retained, making the plant denser and bushier.\n"
		"\tLower Threshold = fewer buds/branches retained, resulting in a sparser, pruned structure."
		"\n\nPress Ctrl + L to lock/unlock node output"
	); 
}

void UPVFoliageDistributorSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property
		&& PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPVFoliageDistributorSettings, OverrideDistribution))
	{
		if (OverrideDistribution)
		{
			Conditions = CustomConditions;
		}
		else
		{
			Conditions = DefaultConditions;
		}	
	}
}
#endif

UPVFoliageDistributorSettings::UPVFoliageDistributorSettings()
{
	if (PCGHelpers::IsNewObjectAndNotDefault(this))
	{
		FRichCurve* RichCurve = InstanceSpacingRamp.GetRichCurve();
		RichCurve->Reset();
		const FKeyHandle Handle0 = RichCurve->AddKey(0.0f, 0.0f);
		const FKeyHandle Handle1 = RichCurve->AddKey(1.0f, 1.0f);
		RichCurve->SetKeyInterpMode(Handle0, RCIM_Linear);
		RichCurve->SetKeyInterpMode(Handle1, RCIM_Linear);

		RichCurve = ScaleRamp.GetRichCurve();
		RichCurve->Reset();
		const FKeyHandle Handle2 = RichCurve->AddKey(0.0f, 0.0f);
		const FKeyHandle Handle3 = RichCurve->AddKey(1.0f, 1.0f);
		RichCurve->SetKeyInterpMode(Handle2, RCIM_Linear);
		RichCurve->SetKeyInterpMode(Handle3, RCIM_Linear);

		RichCurve = AxilAngleRamp.GetRichCurve();
		RichCurve->Reset();
		const FKeyHandle Handle4 = RichCurve->AddKey(0.0f, 1.0f);
		const FKeyHandle Handle5 = RichCurve->AddKey(1.0f, 0.5f);
		RichCurve->SetKeyInterpMode(Handle4, RCIM_Linear);
		RichCurve->SetKeyInterpMode(Handle5, RCIM_Linear);
	}
}

FPCGDataTypeIdentifier UPVFoliageDistributorSettings::GetInputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier{ FPVDataTypeInfoMesh::AsId() };
}

FPCGDataTypeIdentifier UPVFoliageDistributorSettings::GetOutputPinTypeIdentifier() const
{
	return FPCGDataTypeIdentifier{ FPVDataTypeInfoFoliageMesh::AsId() };
}

FPCGElementPtr UPVFoliageDistributorSettings::CreateElement() const
{
	return MakeShared<FPVFoliageDistributorElement>();
}

bool FPVFoliageDistributorElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPVFoliageDistributorElement::Execute);

	check(InContext);

	const UPVFoliageDistributorSettings* Settings = InContext->GetInputSettings<UPVFoliageDistributorSettings>();
	check(Settings);

	UPVFoliageDistributorSettings* MutableSettings = const_cast<UPVFoliageDistributorSettings*>(Settings);
	check(MutableSettings);

	for (const FPCGTaggedData& Input : InContext->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
	{
		if(const UPVMeshData* InputData = Cast<UPVMeshData>(Input.Data))
		{
			FManagedArrayCollection Collection = InputData->GetCollection();
			
			UPVFoliageMeshData* OutManagedArrayCollectionData = FPCGContext::NewObject_AnyThread<UPVFoliageMeshData>(InContext);

			const FDistributionSettings DistributionSettings = {
				.EthyleneThreshold = Settings->EthyleneThreshold,
				.OverrideDistribution = Settings->OverrideDistribution,
				.InstanceSpacing = Settings->InstanceSpacing,
				.InstanceSpacingRamp = &Settings->InstanceSpacingRamp,
				.InstanceSpacingRampEffect = Settings->InstanceSpacingRampEffect,
				.MaxPerBranch = Settings->MaxPerBranch
			};

			const FScaleSettings ScaleSettings = {
				.BaseScale = Settings->BaseScale,
				.BranchScaleImpact = Settings->BranchScaleImpact,
				.MinScale = Settings->MinScale,
				.MaxScale = Settings->MaxScale,
				.RandomScaleMin = Settings->RandomScaleMin,
				.RandomScaleMax = Settings->RandomScaleMax,
				.ScaleRamp = &Settings->ScaleRamp
			};

			const FVectorSettings VectorSettings = {
				.OverrideAxilAngle = Settings->OverrideAxilAngle,
				.AxilAngle = Settings->AxilAngle,
				.AxilAngleRamp = &Settings->AxilAngleRamp,
				.AxilAngleRampUpperValue = Settings->AxilAngleRampUpperValue,
				.AxilAngleRampEffect = Settings->AxilAngleRampEffect
			};

			const FPhyllotaxySettings PhyllotaxySettings = {
				.OverridePhyllotaxy = Settings->OverridePhyllotaxy,
				.PhyllotaxyType = Settings->PhyllotaxyType,
				.PhyllotaxyFormation = Settings->PhyllotaxyFormation,
				.MinimumNodeBuds = Settings->MinimumNodeBuds,
				.MaximumNodeBuds = Settings->MaximumNodeBuds,
				.PhyllotaxyAdditionalAngle = Settings->PhyllotaxyAdditionalAngle
			};

			const FMiscSettings MiscSettings = {
				.RandomSeed = Settings->RandomSeed
			};

			if (!Settings->OverrideDistribution)
			{
				PV::Facades::FFoliageConditionFacade FoliageConditionFacade(Collection);

				auto DefaultConditionsData = FoliageConditionFacade.GetData();

				for (auto ConditonInfo : DefaultConditionsData)
				{
					auto ConditionType = ConditonInfo.GetType();

					if (ConditionType != EFoliageDistributionCondition::None)
					{
						
						if (auto ConditionSettings = MutableSettings->DefaultConditions.GetSettings(ConditionType))
						{
							ConditionSettings->Weight = ConditonInfo.Weight;
							ConditionSettings->Offset = ConditonInfo.Offset;

							MutableSettings->DefaultConditions.SetConditionState(ConditionType, true);	
						}
					}
				}

				if (!MutableSettings->bCustomConditionSet)
				{
					MutableSettings->CustomConditions = MutableSettings->DefaultConditions;
					MutableSettings->bCustomConditionSet = true;
				}
			}

			if (!MutableSettings->OverrideDistribution)
			{
				MutableSettings->Conditions = MutableSettings->DefaultConditions;
			}
			else
			{
				MutableSettings->CustomConditions = MutableSettings->Conditions;
			}	

			FPVFoliage::DistributeFoliage(Collection, Collection, DistributionSettings, ScaleSettings, VectorSettings,
												 PhyllotaxySettings, MiscSettings, Settings->Conditions);

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