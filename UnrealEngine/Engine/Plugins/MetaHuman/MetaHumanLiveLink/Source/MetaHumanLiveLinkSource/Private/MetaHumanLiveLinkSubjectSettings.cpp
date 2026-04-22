// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanLiveLinkSubjectSettings.h"

#include "UObject/Package.h"
#include "Async/Async.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "PropertyEditorModule.h"
#endif



UMetaHumanLiveLinkSubjectSettings::UMetaHumanLiveLinkSubjectSettings()
{
	// Calibration
	Properties = FMetaHumanRealtimeCalibration::GetDefaultProperties();

	// Smoothing
	static constexpr const TCHAR* SmoothingPath = TEXT("/MetaHumanCoreTech/RealtimeMono/DefaultSmoothing.DefaultSmoothing");
	Parameters = LoadObject<UMetaHumanRealtimeSmoothingParams>(GetTransientPackage(), SmoothingPath);
}

void UMetaHumanLiveLinkSubjectSettings::PostLoad()
{
	Super::PostLoad();

	if (NeutralHeadTranslation != FVector::ZeroVector) // back-compatibility with presets before neutral head orientation support
	{
		NeutralHeadPoseInverse = FTransform(NeutralHeadOrientation, NeutralHeadTranslation).Inverse();
	}
}

#if WITH_EDITOR
void UMetaHumanLiveLinkSubjectSettings::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(InPropertyChangedEvent);

	const FProperty* Property = InPropertyChangedEvent.Property;

	// Calibration
	if (Calibration)
	{
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, Properties))
		{
			Calibration->SetProperties(Properties);
		}
		else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, Alpha))
		{
			Calibration->SetAlpha(Alpha);
		}
		else if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, NeutralFrame))
		{
			Calibration->SetNeutralFrame(NeutralFrame);
		}
	}

	// Smoothing
	if (Smoothing)
	{
		if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(ThisClass, Parameters))
		{
			Smoothing.Reset();
		}
	}

	// Neutral head pose
	if (Property->GetFName() == "NeutralHeadTranslation" || Property->GetFName() == "NeutralHeadOrientation" || 
		Property->GetFName() == "X" || Property->GetFName() == "Y" || Property->GetFName() == "Z" ||
		Property->GetFName() == "Roll" || Property->GetFName() == "Pitch" || Property->GetFName() == "Yaw")
	{
		NeutralHeadPoseInverse = FTransform(NeutralHeadOrientation, NeutralHeadTranslation).Inverse();
	}
}
#endif

bool UMetaHumanLiveLinkSubjectSettings::PreProcess(const FLiveLinkBaseStaticData& InStaticData, FLiveLinkBaseFrameData& InOutFrameData)
{
	bool bOK = true;

	TArray<float>& FrameData = InOutFrameData.PropertyValues;

	const double Now = FPlatformTime::Seconds();
	const double DeltaTime = Now - LastTime;
	LastTime = Now;

	// Calibration
	if (!Calibration)
	{
		Calibration = MakeShared<FMetaHumanRealtimeCalibration>(Properties, NeutralFrame, Alpha);
	}

	if (Calibration && CaptureNeutralFrameCountdown == -1) // Dont calibrate while capturing calibration neutral
	{
		bOK &= Calibration->ProcessFrame(InStaticData.PropertyNames, FrameData);
	}

	// Smoothing
	if (!Smoothing && Parameters)
	{
		Smoothing = MakeShared<FMetaHumanRealtimeSmoothing>(Parameters->Parameters);
	}

	if (Smoothing)
	{
		bOK &= Smoothing->ProcessFrame(InStaticData.PropertyNames, FrameData, DeltaTime);
	}

	// Set calibration neutral
	if (Calibration && CaptureNeutralFrameCountdown == 0)
	{
		NeutralFrame = FrameData;

		Calibration->SetNeutralFrame(NeutralFrame);
	}

	if (CaptureNeutralFrameCountdown != -1)
	{
		CaptureNeutralFrameCountdown--;
	}

	// Head translation and orientation
	const int32 HeadXIndex = InStaticData.PropertyNames.Find("HeadTranslationX");
	const int32 HeadYIndex = InStaticData.PropertyNames.Find("HeadTranslationY");
	const int32 HeadZIndex = InStaticData.PropertyNames.Find("HeadTranslationZ");
	const int32 HeadRollIndex = InStaticData.PropertyNames.Find("HeadRoll");
	const int32 HeadPitchIndex = InStaticData.PropertyNames.Find("HeadPitch");
	const int32 HeadYawIndex = InStaticData.PropertyNames.Find("HeadYaw");

	if (!ensureMsgf(HeadXIndex != INDEX_NONE, TEXT("Can not find HeadTranslationX property")))
	{
		return false;
	}

	if (!ensureMsgf(HeadYIndex != INDEX_NONE, TEXT("Can not find HeadTranslationY property")))
	{
		return false;
	}

	if (!ensureMsgf(HeadZIndex != INDEX_NONE, TEXT("Can not find HeadTranslationZ property")))
	{
		return false;
	}

	if (!ensureMsgf(HeadRollIndex != INDEX_NONE, TEXT("Can not find HeadRoll property")))
	{
		return false;
	}

	if (!ensureMsgf(HeadPitchIndex != INDEX_NONE, TEXT("Can not find HeadPitch property")))
	{
		return false;
	}

	if (!ensureMsgf(HeadYawIndex != INDEX_NONE, TEXT("Can not find HeadYaw property")))
	{
		return false;
	}

	FVector HeadTranslation = FVector(FrameData[HeadXIndex], FrameData[HeadYIndex], FrameData[HeadZIndex]);
	FRotator HeadOrientation = FRotator(FrameData[HeadPitchIndex], FrameData[HeadYawIndex], FrameData[HeadRollIndex]);

PRAGMA_DISABLE_DEPRECATION_WARNINGS

	if (CaptureNeutralHeadPoseCountdown == 0 || CaptureNeutralHeadTranslationCountdown == 0)
	{
		NeutralHeadTranslation = HeadTranslation;
		NeutralHeadOrientation = HeadOrientation;

		NeutralHeadPoseInverse = FTransform(NeutralHeadOrientation, NeutralHeadTranslation).Inverse();

#if WITH_EDITOR
		// Refresh properties page
		AsyncTask(ENamedThreads::GameThread, []() {
			FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			PropertyEditorModule.NotifyCustomizationModuleChanged();
		});
#endif
	}

	if (CaptureNeutralHeadPoseCountdown != -1)
	{
		CaptureNeutralHeadPoseCountdown--;
	}

	if (CaptureNeutralHeadTranslationCountdown != -1)
	{
		CaptureNeutralHeadTranslationCountdown--;
	}

	if (CaptureNeutralHeadPoseCountdown == -1 && CaptureNeutralHeadTranslationCountdown == -1)
	{
		const FTransform CalibratedHeadPose = FTransform(HeadOrientation, HeadTranslation) * NeutralHeadPoseInverse;

		HeadTranslation = CalibratedHeadPose.GetTranslation();
		HeadOrientation = CalibratedHeadPose.Rotator();
	}

	if (NeutralHeadTranslation == FVector::ZeroVector)
	{
		HeadTranslation = FVector::ZeroVector; // Without a neutral head position, head translation is not useable
	}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

	EMetaHumanLiveLinkHeadPoseMode HeadPoseMode = EMetaHumanLiveLinkHeadPoseMode::None;

	if (InOutFrameData.MetaData.StringMetaData.Contains("HeadPoseMode"))
	{
		HeadPoseMode = (EMetaHumanLiveLinkHeadPoseMode) FCString::Atoi(*InOutFrameData.MetaData.StringMetaData["HeadPoseMode"]);
	}

	if ((HeadPoseMode & EMetaHumanLiveLinkHeadPoseMode::CameraRelativeTranslation) != EMetaHumanLiveLinkHeadPoseMode::None)
	{
		FrameData[HeadXIndex] = HeadTranslation.X;
		FrameData[HeadYIndex] = HeadTranslation.Y;
		FrameData[HeadZIndex] = HeadTranslation.Z;
	}
	else
	{
		FrameData[HeadXIndex] = 0;
		FrameData[HeadYIndex] = 0;
		FrameData[HeadZIndex] = 0;
	}

	if ((HeadPoseMode & EMetaHumanLiveLinkHeadPoseMode::Orientation) != EMetaHumanLiveLinkHeadPoseMode::None)
	{
		FrameData[HeadRollIndex] = HeadOrientation.Roll;
		FrameData[HeadPitchIndex] = HeadOrientation.Pitch;
		FrameData[HeadYawIndex] = HeadOrientation.Yaw;
	}
	else
	{
		FrameData[HeadRollIndex] = 0;
		FrameData[HeadPitchIndex] = 0;
		FrameData[HeadYawIndex] = 0;
	}

	return bOK;
}

void UMetaHumanLiveLinkSubjectSettings::CaptureNeutrals()
{
	CaptureNeutralFrame();
	CaptureNeutralHeadPose();
}

void UMetaHumanLiveLinkSubjectSettings::CaptureNeutralFrame()
{
	// Somewhat arbitrary number of frames to wait before capturing the calibration
	// neutral values. The calibration neutrals needs to be captured after smoothing
	// but without any previous calibration applied. Turning off the previous calibration
	// in order to capture a new one causes a jump in animation values and that needs
	// time to be smoothed out. Ideally here we would switch off the usual smoothing while
	// capturing a neutral and instead apply a known size rolling average since the head
	// should be steady while capturing a neutral and we are only interesting in removing
	// the noise introduced by the solve and not trying to smooth out any head motion.

	CaptureNeutralFrameCountdown = 5;
}

void UMetaHumanLiveLinkSubjectSettings::CaptureNeutralHeadPose()
{
	// See comment above. Larger number used here to first capture neutral, then wait until
	// that is smoothed before capturing head pose.

	CaptureNeutralHeadPoseCountdown = 10;
}

void UMetaHumanLiveLinkSubjectSettings::SetCalibrationProperties(const TArray<FName>& InProperties)
{
	Properties = InProperties;

	if (Calibration)
	{
		Calibration->SetProperties(Properties);
	}
}

void UMetaHumanLiveLinkSubjectSettings::GetCalibrationProperties(TArray<FName>& OutProperties) const
{
	OutProperties = Properties;
}

void UMetaHumanLiveLinkSubjectSettings::SetCalibrationAlpha(float InAlpha)
{
	Alpha = InAlpha;

	if (Calibration)
	{
		Calibration->SetAlpha(Alpha);
	}
}

void UMetaHumanLiveLinkSubjectSettings::GetCalibrationAlpha(float& OutAlpha) const
{
	OutAlpha = Alpha;
}

void UMetaHumanLiveLinkSubjectSettings::SetCalibrationNeutralFrame(const TArray<float>& InNeutralFrame)
{
	NeutralFrame = InNeutralFrame;

	if (Calibration)
	{
		Calibration->SetNeutralFrame(NeutralFrame);
	}
}

void UMetaHumanLiveLinkSubjectSettings::GetCalibrationNeutralFrame(TArray<float>& OutNeutralFrame) const
{
	OutNeutralFrame = NeutralFrame;
}

void UMetaHumanLiveLinkSubjectSettings::SetSmoothing(UMetaHumanRealtimeSmoothingParams* InSmoothing)
{
	Parameters = InSmoothing;

	Smoothing.Reset();
}

void UMetaHumanLiveLinkSubjectSettings::GetSmoothing(UMetaHumanRealtimeSmoothingParams*& OutSmoothing) const
{
	OutSmoothing = Parameters;
}

void UMetaHumanLiveLinkSubjectSettings::SetNeutralHeadTranslation(const FVector& InNeutralHeadTranslation)
{
	NeutralHeadTranslation = InNeutralHeadTranslation;

	NeutralHeadPoseInverse = FTransform(NeutralHeadOrientation, NeutralHeadTranslation).Inverse();
}

void UMetaHumanLiveLinkSubjectSettings::GetNeutralHeadTranslation(FVector& OutNeutralHeadTranslation) const
{
	OutNeutralHeadTranslation = NeutralHeadTranslation;
}

void UMetaHumanLiveLinkSubjectSettings::SetNeutralHeadOrientation(const FRotator& InNeutralHeadOrientation)
{
	NeutralHeadOrientation = InNeutralHeadOrientation;

	NeutralHeadPoseInverse = FTransform(NeutralHeadOrientation, NeutralHeadTranslation).Inverse();
}

void UMetaHumanLiveLinkSubjectSettings::GetNeutralHeadOrientation(FRotator& OutNeutralHeadOrientation) const
{
	OutNeutralHeadOrientation = NeutralHeadOrientation;
}
