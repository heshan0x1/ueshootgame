// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCalibrationGenerator.h"

#include "ImageSequenceUtils.h"
#include "ImgMediaSource.h"

#include "Async/Async.h"
#include "Async/Monitor.h"
#include "Async/ParallelFor.h"

#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "FileHelpers.h"

#include "Utils/MetaHumanCalibrationNotificationManager.h"
#include "Utils/MetaHumanCalibrationUtils.h"
#include "Utils/MetaHumanCalibrationFrameResolver.h"

#include "MetaHumanCalibrationPatternDetector.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanCalibrationGenerator)

#define LOCTEXT_NAMESPACE "MetaHumanCalibrationGenerator"

DEFINE_LOG_CATEGORY_STATIC(LogMetaHumanCalibrationGenerator, Log, All);

namespace UE::MetaHuman::Private
{

static UCameraCalibration* CreateCameraCalibrationAsset(const FString& InTargetPackagePath, const FString& InDesiredAssetName)
{
	IAssetRegistry& AssetRegistry = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();

	FString AssetName = InDesiredAssetName;
	FString ObjectPathToCheck = InTargetPackagePath / (AssetName + FString(TEXT(".")) + AssetName);
	FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ObjectPathToCheck));

	int32 Counter = 1;
	while (AssetData.IsValid())
	{
		AssetName = InDesiredAssetName + TEXT("_") + FString::FromInt(Counter++);
		ObjectPathToCheck = InTargetPackagePath / (AssetName + FString(TEXT(".")) + AssetName);
		AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ObjectPathToCheck));
	}

	return Cast<UCameraCalibration>(AssetTools.CreateAsset(AssetName, InTargetPackagePath, UCameraCalibration::StaticClass(), nullptr));
}

static void SaveCalibrationProcessCreatedAssets(const FString& InAssetPath)
{
	IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	IAssetRegistry& AssetRegistry = FModuleManager::GetModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> AssetsData;
	AssetRegistry.GetAssetsByPath(FName{ *InAssetPath }, AssetsData, true, false);

	if (AssetsData.IsEmpty())
	{
		return;
	}

	TArray<UPackage*> Packages;
	for (const FAssetData& AssetData : AssetsData)
	{
		UPackage* Package = AssetData.GetAsset()->GetPackage();
		if (!Packages.Contains(Package))
		{
			Packages.Add(Package);
		}
	}

	UEditorLoadingAndSavingUtils::SavePackages(Packages, true);
}

static TArray<FCameraCalibration> MatchImageSequenceWithCalibration(const TArray<UImgMediaSource*> InImageSequences,
																	const TArray<FCameraCalibration>& InCameraCalibrations)
{
	TArray<FCameraCalibration> MatchedCameraCalibrations;

	for (UImgMediaSource* ImgMediaSource : InImageSequences)
	{
		FString CameraName = ImgMediaSource->GetName();

		const FCameraCalibration* Match = InCameraCalibrations.FindByPredicate([CameraName](const FCameraCalibration& InCameraCalibration)
		{
			return CameraName == InCameraCalibration.CameraId;
		});

		check(Match);

		if (Match)
		{
			MatchedCameraCalibrations.Add(*Match);
		}
	}

	return MatchedCameraCalibrations;
}

static void CreateCalibrationAsset_GameThread(TStrongObjectPtr<const UMetaHumanCalibrationGenerator> InOwner,
											  TStrongObjectPtr<const UMetaHumanCalibrationGeneratorOptions> InOptions,
											  TArray<FCameraCalibration> InCameraCalibrations,
											  TSharedPtr<FMetaHumanCalibrationNotificationManager> InNotificationManager,
											  TStrongObjectPtr<UFootageCaptureData> OutCaptureData)
{
	TArray<FCameraCalibration> MatchedCalibrations =
		MatchImageSequenceWithCalibration(OutCaptureData->ImageSequences, InCameraCalibrations);

	TObjectPtr<UCameraCalibration> CalibrationAsset =
		UE::MetaHuman::Private::CreateCameraCalibrationAsset(InOptions->PackagePath.Path, InOptions->AssetName);

	CalibrationAsset->CameraCalibrations.Reset();
	CalibrationAsset->StereoPairs.Reset();
	CalibrationAsset->ConvertFromTrackerNodeCameraModels(MatchedCalibrations, false);

	OutCaptureData->CameraCalibrations.Add(MoveTemp(CalibrationAsset));

	OutCaptureData->MarkPackageDirty();

	if (InOptions->bAutoSaveAssets)
	{
		SaveCalibrationProcessCreatedAssets(InOptions->PackagePath.Path);
	}
}

static void CreateCalibrationAssetOnGameThread(TStrongObjectPtr<const UMetaHumanCalibrationGenerator> InOwner,
											   TStrongObjectPtr<const UMetaHumanCalibrationGeneratorOptions> InOptions,
											   TArray<FCameraCalibration> InCameraCalibrations,
											   TSharedPtr<FMetaHumanCalibrationNotificationManager> InNotificationManager,
											   TStrongObjectPtr<UFootageCaptureData> OutCaptureData)
{
	if (IsInGameThread())
	{
		CreateCalibrationAsset_GameThread(
			MoveTemp(InOwner),
			MoveTemp(InOptions),
			MoveTemp(InCameraCalibrations),
			MoveTemp(InNotificationManager),
			MoveTemp(OutCaptureData));
	}
	else
	{
		// UMetaHumanCalibrationGenerated, UFootageCaptureData and FCalibrationNotificationManager are captured here to protect their lifecycle. 
		ExecuteOnGameThread(TEXT("CalibrationAssetCreation"),
							[Owner = MoveTemp(InOwner),
							Options = MoveTemp(InOptions),
							CaptureData = MoveTemp(OutCaptureData),
							CameraCalibrations = MoveTemp(InCameraCalibrations),
							NotificationManager = MoveTemp(InNotificationManager)]() mutable
							{
								CreateCalibrationAsset_GameThread(
									MoveTemp(Owner),
									MoveTemp(Options),
									MoveTemp(CameraCalibrations),
									MoveTemp(NotificationManager),
									MoveTemp(CaptureData));
							});
	}
}

}

UMetaHumanCalibrationGenerator::UMetaHumanCalibrationGenerator()
	: StereoCalibrator(MakeShared<UE::Wrappers::FMetaHumanStereoCalibrator>())
{
}

bool UMetaHumanCalibrationGenerator::Init(const UMetaHumanCalibrationGeneratorConfig* InConfig)
{
	TValueOrError<void, FString> ConfigValidity = InConfig->CheckConfigValidity();

	if (ConfigValidity.HasError())
	{
		UE_LOG(LogMetaHumanCalibrationGenerator, Error, TEXT("Invalid config for stereo calibration process: %s"), *ConfigValidity.GetError());
		return false;
	}

	bInitialized = StereoCalibrator->Init(InConfig->BoardPatternWidth - 1, InConfig->BoardPatternHeight - 1, InConfig->BoardSquareSize);
	return bInitialized;
}

bool UMetaHumanCalibrationGenerator::ConfigureCameras(const UFootageCaptureData* InCaptureData)
{
	bCamerasConfigured = false;

	if (InCaptureData->ImageSequences.Num() < 2)
	{
		UE_LOG(LogMetaHumanCalibrationGenerator, Error, TEXT("Stereo calibration process expects 2 cameras, but found %d"), InCaptureData->ImageSequences.Num());
		return false;
	}

	const UImgMediaSource* FirstCameraImageSource = InCaptureData->ImageSequences[0];
	const UImgMediaSource* SecondCameraImageSource = InCaptureData->ImageSequences[1];

	if (!IsValid(FirstCameraImageSource) || !IsValid(SecondCameraImageSource))
	{
		UE_LOG(LogMetaHumanCalibrationGenerator, Error, TEXT("No valid cameras found"));
		return false;
	}

	FString FirstCameraName = FirstCameraImageSource->GetName();
	FString SecondCameraName = SecondCameraImageSource->GetName();

	FIntVector2 ImageDimensions;
	int32 FirstNumberOfImages = 0;
	FImageSequenceUtils::GetImageSequenceInfoFromAsset(FirstCameraImageSource, ImageDimensions, FirstNumberOfImages);

	int32 SecondNumberOfImages = 0;
	FImageSequenceUtils::GetImageSequenceInfoFromAsset(SecondCameraImageSource, ImageDimensions, SecondNumberOfImages);

	UE_LOG(LogMetaHumanCalibrationGenerator, Display, TEXT("Adding %s camera with image size %dx%d"), *FirstCameraName, ImageDimensions.X, ImageDimensions.Y);
	StereoCalibrator->AddCamera(FirstCameraName, ImageDimensions.X, ImageDimensions.Y);

	UE_LOG(LogMetaHumanCalibrationGenerator, Display, TEXT("Adding %s camera with image size %dx%d"), *SecondCameraName, ImageDimensions.X, ImageDimensions.Y);
	StereoCalibrator->AddCamera(SecondCameraName, ImageDimensions.X, ImageDimensions.Y);

	bCamerasConfigured = true;
	return bCamerasConfigured;
}

bool UMetaHumanCalibrationGenerator::Process(UFootageCaptureData* InCaptureData, const UMetaHumanCalibrationGeneratorOptions* InOptions)
{
	using namespace UE::MetaHuman::Private;
	using namespace UE::MetaHuman::Image;
	using namespace UE::CaptureManager;

	if (!bInitialized)
	{
		// Backwards compatibility with old Options.
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		UMetaHumanCalibrationGeneratorConfig* Config = NewObject<UMetaHumanCalibrationGeneratorConfig>();
		Config->BoardPatternHeight = InOptions->BoardPatternHeight_DEPRECATED + 1;
		Config->BoardPatternWidth = InOptions->BoardPatternWidth_DEPRECATED + 1;
		Config->BoardSquareSize = InOptions->BoardSquareSize_DEPRECATED;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS

		Init(Config);
	}

	TValueOrError<void, FString> OptionsValidity = InOptions->CheckOptionsValidity();
	if (OptionsValidity.HasError())
	{
		UE_LOG(LogMetaHumanCalibrationGenerator, Error, TEXT("Invalid options for stereo calibration process: %s"), *OptionsValidity.GetError());
		return false;
	}

	if (!bCamerasConfigured)
	{
		ConfigureCameras(InCaptureData);
	}

	TOptional<FMetaHumanCalibrationFrameResolver> ResolverOpt = FMetaHumanCalibrationFrameResolver::CreateFromCaptureData(InCaptureData);
	if (!ResolverOpt.IsSet())
	{
		UE_LOG(LogMetaHumanCalibrationGenerator, Error, TEXT("Frame Resolver is NOT valid."));
		return false;
	}

	FMetaHumanCalibrationFrameResolver Resolver = MoveTemp(ResolverOpt.GetValue());

	if (!Resolver.HasFrames())
	{
		UE_LOG(LogMetaHumanCalibrationGenerator, Error, TEXT("No matching frames found."));
		return false;
	}

	FString FirstCameraName = InCaptureData->ImageSequences[0]->GetName();
	FString SecondCameraName = InCaptureData->ImageSequences[1]->GetName();

	using FFrameCameraPaths = TPair<TArray<FString>, TArray<FString>>;

	FFrameCameraPaths AllCameraPaths;
	Resolver.GetFramePathsForCameraIndex(0, AllCameraPaths.Key);
	Resolver.GetFramePathsForCameraIndex(1, AllCameraPaths.Value);

	TSharedPtr<FMetaHumanCalibrationNotificationManager> NotificationManager = 
		MakeShared<FMetaHumanCalibrationNotificationManager>();
	NotificationManager->NotificationOnBegin(LOCTEXT("CalibrationDetectionInProgress", "MetaHumanCalibrationGenerator: Waiting for chessboard pattern detection..."));

	FFrameCameraPaths SelectedFramePaths;
	if (!InOptions->SelectedFrames.IsEmpty())
	{
		SelectedFramePaths = FilterFramePaths(AllCameraPaths, [&](int32 InFrameIndex) -> bool
		{
			return InOptions->SelectedFrames.Contains(InFrameIndex);
		});
	}
	else
	{
		SelectedFramePaths = FilterFramePaths(AllCameraPaths, [&](int32 InFrameIndex) -> bool
		{
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			bool bShouldIncludeFrame = (InFrameIndex % InOptions->SampleRate_DEPRECATED) == 0;
			PRAGMA_ENABLE_DEPRECATION_WARNINGS

			return bShouldIncludeFrame;
		});
	}

	TUniquePtr<FMetaHumanCalibrationPatternDetector> PatternDetector =
		FMetaHumanCalibrationPatternDetector::CreateFromExistingCalibrator(StereoCalibrator);

	TMonitor<FFrameCameraPaths> ProtectedPaths(MoveTemp(AllCameraPaths));
	TMonitor<FFrameCameraPaths> ProtectedSelectedPaths(MoveTemp(SelectedFramePaths));

	auto OnFailureLambda = [&ProtectedPaths, &ProtectedSelectedPaths](const FMetaHumanCalibrationPatternDetector::FFramePaths& InFailedPaths, int32 InTry)
		-> TOptional<FMetaHumanCalibrationPatternDetector::FFramePaths>
		{
			FString NewFirstCameraPath;
			FString NewSecondCameraPath;
			{
				TMonitor<FFrameCameraPaths>::FHelper ScopeLock = ProtectedPaths.Lock();
				int32 FirstCameraPathIndex = ScopeLock->Key.IndexOfByKey(InFailedPaths.Key);
				int32 SecondCameraPathIndex = ScopeLock->Value.IndexOfByKey(InFailedPaths.Value);

				if (FirstCameraPathIndex == INDEX_NONE ||
					SecondCameraPathIndex == INDEX_NONE)
				{
					return {};
				}

				const int32 FirstPathIndex = FirstCameraPathIndex + InTry;

				if (ScopeLock->Key.IsValidIndex(FirstPathIndex))
				{
					NewFirstCameraPath = ScopeLock->Key[FirstPathIndex];
				}

				const int32 SecondPathIndex = SecondCameraPathIndex + InTry;

				if (ScopeLock->Value.IsValidIndex(SecondPathIndex))
				{
					NewSecondCameraPath = ScopeLock->Value[SecondPathIndex];
				}
			}

			{
				TMonitor<FFrameCameraPaths>::FHelper ScopeSelectedLock = ProtectedSelectedPaths.Lock();

				if (NewFirstCameraPath.IsEmpty() || NewSecondCameraPath.IsEmpty())
				{
					return {};
				}

				if (ScopeSelectedLock->Key.Contains(NewFirstCameraPath) ||
					ScopeSelectedLock->Value.Contains(NewSecondCameraPath))
				{
					return {};
				}
			}

			FMetaHumanCalibrationPatternDetector::FFramePaths NewFrame = { MoveTemp(NewFirstCameraPath), MoveTemp(NewSecondCameraPath) };
			return NewFrame;
		};

	FMetaHumanCalibrationPatternDetector::FOnFailureFrameProvider OnFailureFrameProvider =
		FMetaHumanCalibrationPatternDetector::FOnFailureFrameProvider::CreateLambda(MoveTemp(OnFailureLambda));

	FMetaHumanCalibrationPatternDetector::FDetectedFrames DetectedFrames =
		PatternDetector->DetectPatterns({ FirstCameraName, SecondCameraName }, ProtectedSelectedPaths.GetUnsafe(), InOptions->SharpnessThreshold, MoveTemp(OnFailureFrameProvider));

	PatternDetector = nullptr;

	static constexpr int32 MinimumRequiredFrames = 3;
	bool bDetectionSuccess = !DetectedFrames.IsEmpty() && (DetectedFrames.Num() >= MinimumRequiredFrames);
	
	NotificationManager->NotificationOnEnd(bDetectionSuccess);

	if (!bDetectionSuccess)
	{
		UE_LOG(LogMetaHumanCalibrationGenerator, Error, TEXT("Not enough valid frames detected to run calibration (Minimum is %d)"), MinimumRequiredFrames);
		return false;
	}

	NotificationManager->NotificationOnBegin(LOCTEXT("CalibrationInProgress", "MetaHumanCalibrationGenerator: Waiting for calibration..."));
	TArray<FCameraCalibration> CameraCalibrations;
	double OutReprojectionError = 0.0f;
	bool bResult = StereoCalibrator->Calibrate(DetectedFrames, CameraCalibrations, OutReprojectionError);

	TOptional<FText> NewMessage = bResult ? FText::Format(LOCTEXT("CalibrationSuccess", "Calibrated Successfully {0}"), OutReprojectionError) : TOptional<FText>();
	NotificationManager->NotificationOnEnd(bResult, MoveTemp(NewMessage));

	if (!bResult)
	{
		UE_LOG(LogMetaHumanCalibrationGenerator, Error, TEXT("Failed to calibrate the footage"));
		return false;
	}

	UE_LOG(LogMetaHumanCalibrationGenerator, Display, TEXT("Successfully calibrated with reprojection error of %lf"), OutReprojectionError);

	LastRMSError = OutReprojectionError;

	CreateCalibrationAssetOnGameThread(TStrongObjectPtr<const UMetaHumanCalibrationGenerator>(this),
									   TStrongObjectPtr<const UMetaHumanCalibrationGeneratorOptions>(InOptions),
									   MoveTemp(CameraCalibrations),
									   MoveTemp(NotificationManager), 
									   TStrongObjectPtr<UFootageCaptureData>(InCaptureData));

	return true;
}

double UMetaHumanCalibrationGenerator::GetLastRMSError() const
{
	return LastRMSError;
}

bool UMetaHumanCalibrationGenerator::Reset(const UMetaHumanCalibrationGeneratorConfig* InConfig, const UFootageCaptureData* InCaptureData)
{
	StereoCalibrator = MakeShared<UE::Wrappers::FMetaHumanStereoCalibrator>();

	bool bSuccess = Init(InConfig);

	if (!bSuccess)
	{
		return false;
	}

	bSuccess = ConfigureCameras(InCaptureData);

	return bSuccess;
}

#undef LOCTEXT_NAMESPACE
