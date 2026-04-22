// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigMapperOp.h"

#include "Animation/AnimCurveUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimNodeBase.h"
#include "RigMapperDefinition.h"
#include "Retargeter/IKRetargetProcessor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigMapperOp)

const UClass* FIKRetargetRigMapperOpSettings::GetControllerType() const
{
	return UIKRetargetRigMapperOpController::StaticClass();
}

void FIKRetargetRigMapperOpSettings::CopySettingsAtRuntime(const FIKRetargetOpSettingsBase* InSettingsToCopyFrom)
{
	// copies all properties
	const TArray<FName> PropertiesToIgnore = {};
	FIKRetargetOpBase::CopyStructProperties(
		FIKRetargetRigMapperOpSettings::StaticStruct(),
		InSettingsToCopyFrom,
		this,
		PropertiesToIgnore);
}

void FIKRetargetRigMapperOp::AnimGraphPreUpdateMainThread(
	USkeletalMeshComponent& SourceMeshComponent,
    USkeletalMeshComponent& TargetMeshComponent)
{
	if (!IsEnabled())
	{
		return;
	}
	
	SourceCurves.Empty();
	
	// get the source curves out of the source anim instance
	const UAnimInstance* SourceAnimInstance = SourceMeshComponent.GetAnimInstance();
	if (!SourceAnimInstance)
	{
		return;
	}
	
	// Potential optimization/tradeoff: If we stored the curve results on the mesh component in non-editor scenarios, this would be
	// much faster (but take more memory). As it is, we need to translate the map stored on the anim instance.
	const TMap<FName, float>& AnimCurveList = SourceAnimInstance->GetAnimationCurveList(EAnimCurveType::AttributeCurve);
	UE::Anim::FCurveUtils::BuildUnsorted(SourceCurves, AnimCurveList);


	// Need reinit if the definitions in use have changed.
	bool bReInit = false;

	// If definitions were loaded from the SKM asset user data, they take priority and are the ones we will check against
	TArray<TObjectPtr<URigMapperDefinition>>* DefinitionsToCheck = &Settings.Definitions;
	if (LoadedUserData)
	{
		DefinitionsToCheck = &LoadedUserData->Definitions;
	}

	check(DefinitionsToCheck);

	// Otherwise, reinit if the definitions are not the same
	if (DefinitionsToCheck->Num() != LoadedDefinitions.Num())
	{
		bReInit = true;
	}
	else
	{
		for (int32 DefinitionIndex = 0; DefinitionIndex < DefinitionsToCheck->Num(); DefinitionIndex++)
		{
			const bool bDefinitionEdittedAndNotValidated = (*DefinitionsToCheck)[DefinitionIndex] != nullptr && !(*DefinitionsToCheck)[DefinitionIndex]->WasDefinitionValidated();
			const bool bDefinitionHasChanged = (*DefinitionsToCheck)[DefinitionIndex] != LoadedDefinitions[DefinitionIndex];
			if (bDefinitionHasChanged || bDefinitionEdittedAndNotValidated)
			{
				bReInit = true;
				break;
			}
		}
	}
	if (bReInit)
	{
		USkeletalMesh* TargetMesh = TargetMeshComponent.GetSkeletalMeshAsset();
		bIsInitialized = InitializeRigMapping(TargetMesh);
	}

}

void FIKRetargetRigMapperOp::AnimGraphEvaluateAnyThread(FPoseContext& Output)
{
	if (!IsEnabled())
	{
		return;
	}

	FBlendedCurve& OutputCurves = Output.Curve;

	FBlendedCurve InputCurves;
	if (GetTakeInputCurvesFromSourceAnimInstance())
	{
		InputCurves.CopyFrom(SourceCurves);
		if (Settings.bCopyAllSourceCurves)
		{
			OutputCurves.CopyFrom(SourceCurves);
		}
	}
	else
	{
		InputCurves.CopyFrom(OutputCurves);
		// clear the outputs if needed
		if (!Settings.bCopyAllSourceCurves)
		{
			OutputCurves.ForEachElement([&OutputCurves](const UE::Anim::FCurveElement& InCurveElement)
				{
					OutputCurves.Set(InCurveElement.Name, 0.0f);
				});
		}

	}

	EvaluateRigMapping(InputCurves, OutputCurves);

}

void FIKRetargetRigMapperOp::EvaluateRigMapping(const FBlendedCurve& InCurve, FBlendedCurve& OutCurve)
{
	if (!RigMapperProcessor.IsValid())
	{
		return;
	}
	
	// Retrieve inputs
	CachedInputValues.Reset(InputCurveMappings.Num());
	CachedInputValues.AddDefaulted(InputCurveMappings.Num());
	UE::Anim::FCurveUtils::BulkGet(InCurve, InputCurveMappings,
		[&Inputs = CachedInputValues](const FRigMapperCurveMapping& InBulkElement, const float InValue)
		{
			Inputs[InBulkElement.CurveIndex] = InValue;
		}
	);

	{
		// Evaluate frame
		RigMapperProcessor.EvaluateFrame(RigMapperProcessor.GetInputNames(), CachedInputValues, CachedOutputValues);
	}

	{
		// Set all output curves from the given output values.
		UE::Anim::FCurveUtils::BulkSet(OutCurve, OutputCurveMappings,
			[&Inputs = CachedInputValues, &Outputs = CachedOutputValues](const FRigMapperOutputCurveMapping& InBulkElement)
			{
				TOptional<float> Value = Outputs[InBulkElement.CurveIndex];
	
				// If the value was not set by the rig mapper default to 0
				return Value.Get(0.f);
			}
		);
	}
}


bool FIKRetargetRigMapperOp::InitializeRigMapping(const USkeletalMesh* InTargetMesh)
{
	TArray<URigMapperDefinition*> DefinitionsToLoad;

	// Retrieve the definitions to load and use (either the ones set in the op, or the ones overriden by the SKM asset user data)
	if (InTargetMesh)
	{
		// Note that we cast away constness here as we need to get at the Asset User Data. This is not changed anywhere so is a reasonable pragmatic solution
		LoadedUserData = Cast<URigMapperDefinitionUserData>(const_cast<USkeletalMesh*>(InTargetMesh)->GetAssetUserDataOfClass(URigMapperDefinitionUserData::StaticClass()));
		if (LoadedUserData)
		{
			DefinitionsToLoad = LoadedUserData->Definitions;
		}
	}
	if (DefinitionsToLoad.IsEmpty())
	{
		DefinitionsToLoad = Settings.Definitions;
	}

	RigMapperProcessor = FRigMapperProcessor(DefinitionsToLoad);
	LoadedDefinitions = MoveTemp(DefinitionsToLoad);

	if (!RigMapperProcessor.IsValid())
	{
		return false;
	}

	// Cache a map of curve indices to bulk get current curve values for the input
	const TArray<FName>& InputNames = RigMapperProcessor.GetInputNames();
	InputCurveMappings.Empty();
	InputCurveMappings.Reserve(InputNames.Num());
	for (int32 InputIndex = 0; InputIndex < InputNames.Num(); InputIndex++)
	{
		InputCurveMappings.Add(InputNames[InputIndex], InputIndex);
	}

	// Cache a map of curve indices to bulk set the new curve values for the output
	const TArray<FName>& OutputNames = RigMapperProcessor.GetOutputNames();
	OutputCurveMappings.Empty();
	OutputCurveMappings.Reserve(OutputNames.Num());
	for (int32 OutputIndex = 0; OutputIndex < OutputNames.Num(); OutputIndex++)
	{
		const FName& CurveName = OutputNames[OutputIndex];

		int32 InputIndex = InputNames.Find(CurveName);
		OutputCurveMappings.Add(OutputNames[OutputIndex], OutputIndex, InputIndex);
	}

	return true;
}

static FLinearColor MakeCurveColor(const FName& InCurveName)
{
	// Create a color based on the hash of the name
	FRandomStream Stream(GetTypeHash(InCurveName));
	const uint8 Hue = (uint8)(Stream.FRand() * 255.0f);
	return FLinearColor::MakeFromHSV8(Hue, 196, 196);
}

void FIKRetargetRigMapperOp::ProcessAnimSequenceCurves(FIKRetargetOpBase::FCurveData InCurveMetaData, FIKRetargetOpBase::FFrameValues InCurveFrameValues,
	FIKRetargetOpBase::FCurveData& OutCurveMetaData, FIKRetargetOpBase::FFrameValues& OutCurveFrameValues) const
{
	// We need to copy the RigMapperProcessor to keep this method const. Revisit this for speed at some point.
	FRigMapperProcessor RigMapperProcessorTemp = RigMapperProcessor;
	TSet<FName> OutputNames;

	if (RigMapperProcessorTemp.IsValid())
	{
		RigMapperProcessorTemp.EvaluateFrames(InCurveMetaData.Names, InCurveFrameValues, OutCurveMetaData.Names, OutCurveFrameValues);
		OutCurveMetaData.Flags.Init(4, OutCurveMetaData.Names.Num()); // a value of 4 means Editable curve
		OutCurveMetaData.Colors.SetNum(OutCurveMetaData.Names.Num());
		for (int32 Curve = 0; Curve < OutCurveMetaData.Names.Num(); ++Curve)
		{
			OutCurveMetaData.Colors[Curve] = MakeCurveColor(OutCurveMetaData.Names[Curve]);
			OutputNames.Add(OutCurveMetaData.Names[Curve]);
		}

		// 'pass-through' any input curves which have not been modified, if bCopyAllSourceCurves is true
		if (Settings.bCopyAllSourceCurves)
		{
			TArray<int32> InputCurveIndicesToAdd;
			InputCurveIndicesToAdd.Reserve(InCurveMetaData.Names.Num());
			for (int32 InputCurve = 0; InputCurve < InCurveMetaData.Names.Num(); ++InputCurve)
			{
				if (!OutputNames.Contains(InCurveMetaData.Names[InputCurve]))
				{
					InputCurveIndicesToAdd.Add(InputCurve);
				}
			}

			// now add these curves
			OutCurveMetaData.Names.Reserve(OutCurveMetaData.Names.Num() + InputCurveIndicesToAdd.Num());
			OutCurveMetaData.Flags.Reserve(OutCurveMetaData.Flags.Num() + InputCurveIndicesToAdd.Num());
			OutCurveMetaData.Colors.Reserve(OutCurveMetaData.Colors.Num() + InputCurveIndicesToAdd.Num());
			int32 Frame = 0;
			for (TArray<TOptional<float>>& OutputFrame : OutCurveFrameValues)
			{
				OutputFrame.Reserve(OutputFrame.Num() + InputCurveIndicesToAdd.Num());
				for (int32 CurveIndex : InputCurveIndicesToAdd)
				{
					OutputFrame.Add(InCurveFrameValues[Frame][CurveIndex]);
				}

				Frame++;
			}

			for (int32 CurveIndex : InputCurveIndicesToAdd)
			{
				OutCurveMetaData.Names.Add(InCurveMetaData.Names[CurveIndex]);
				OutCurveMetaData.Flags.Add(InCurveMetaData.Flags[CurveIndex]);
				OutCurveMetaData.Colors.Add(InCurveMetaData.Colors[CurveIndex]);
			}
		}
	}
}

bool FIKRetargetRigMapperOp::Initialize(
	const FIKRetargetProcessor& InProcessor,
	const FRetargetSkeleton& /*InSourceSkeleton*/,
	const FTargetSkeleton& /*InTargetSkeleton*/,
	const FIKRetargetOpBase* /*InParentOp*/,
	FIKRigLogger& /*Log*/)
{
	const FRetargetSkeleton& TargetSkeleton = InProcessor.GetSkeleton(ERetargetSourceOrTarget::Target);
	bIsInitialized = InitializeRigMapping(TargetSkeleton.SkeletalMesh);

	return bIsInitialized;
};



FIKRetargetRigMapperOpSettings UIKRetargetRigMapperOpController::GetSettings()
{
	return *reinterpret_cast<FIKRetargetRigMapperOpSettings*>(OpSettingsToControl);
}

void UIKRetargetRigMapperOpController::SetSettings(FIKRetargetRigMapperOpSettings InSettings)
{
	OpSettingsToControl->CopySettingsAtRuntime(&InSettings);
}
