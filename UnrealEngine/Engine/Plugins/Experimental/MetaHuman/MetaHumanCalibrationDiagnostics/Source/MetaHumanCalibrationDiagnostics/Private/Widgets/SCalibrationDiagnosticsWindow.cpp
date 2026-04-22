// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCalibrationDiagnosticsWindow.h"

#include "SlateOptMacros.h"
#include "Engine/Texture2D.h"
#include "Widgets/Images/SImage.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"

#include "ImgMediaSource.h"

#include "ParseTakeUtils.h"
#include "ImageSequenceUtils.h"

#include "Widgets/Docking/SDockTab.h"
#include "Framework/Docking/LayoutService.h"

#include "MetaHumanCalibrationDiagnosticsCommands.h"

#define LOCTEXT_NAMESPACE "SCalibrationDiagnosticsWindow"

namespace UE::MetaHuman::Private
{
static const FName ImageViewerTabName = TEXT("ImageViewerTab");
static const FName OptionsTabName = TEXT("OptionsTab");
}

void SCalibrationDiagnosticsWindow::Construct(const FArguments& InArgs, const TSharedPtr<SWindow>& InOwningWindow, const TSharedRef<SDockTab>& InOwningTab)
{
	FeatureMatcher.Reset(NewObject<UMetaHumanRobustFeatureMatcher>());
	check(FeatureMatcher.Get());

	CaptureData.Reset(InArgs._FootageCaptureData);
	check(CaptureData);

	Options.Reset(NewObject<UMetaHumanCalibrationDiagnosticsOptions>());
	
	if (CaptureData->CameraCalibrations.IsValidIndex(0))
	{
		Options->CameraCalibration = CaptureData->CameraCalibrations[0];
	}

	TabManager = FGlobalTabmanager::Get()->NewTabManager(InOwningTab);

	const TSharedRef<FWorkspaceItem> TargetSetsWorkspaceMenuCategory =
		TabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("CalibrationDiagnosticsWorkspaceMenuCategory", "Calibration Diagnostics"));
	RegisterImageViewerTabSpawner(TabManager.ToSharedRef(), TargetSetsWorkspaceMenuCategory);
	RegisterOptionsTabSpawner(TabManager.ToSharedRef(), TargetSetsWorkspaceMenuCategory);

	bIsFeatureDetectorInitialized = FeatureMatcher->Init(CaptureData.Get(), Options.Get());

	const auto& PersistLayout = [](const TSharedRef<FTabManager::FLayout>& LayoutToSave)
		{
			FLayoutSaveRestore::SaveToConfig(GEditorLayoutIni, LayoutToSave);
		};

	TabManager->SetOnPersistLayout(FTabManager::FOnPersistLayout::CreateLambda(PersistLayout));

	TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("CalibrationDiagnostics")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetHideTabWell(false)
				->SetSizeCoefficient(0.75)
				->AddTab(UE::MetaHuman::Private::ImageViewerTabName, ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.25)
				->SetHideTabWell(false)
				->AddTab(UE::MetaHuman::Private::OptionsTabName, ETabState::OpenedTab)
			)	
		);

	ChildSlot
		[
			TabManager->RestoreFrom(Layout, InOwningWindow).ToSharedRef()
		];

	SetImages(0);
}

void SCalibrationDiagnosticsWindow::SetImages(int32 InFrameId)
{
	ImageViewer->SetImages(InFrameId);
}

void SCalibrationDiagnosticsWindow::OnClose()
{
	ImageViewer->OnClose();
}

void SCalibrationDiagnosticsWindow::OnFinishedChangingProperties(const FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (InPropertyChangedEvent.Property == nullptr)
	{
		return;
	}

	if (InPropertyChangedEvent.MemberProperty != nullptr)
	{
		// The MemberProperty is set to AreaOfInterest while the Property can be X, Y
		const FName MemberPropertyName(InPropertyChangedEvent.MemberProperty->GetFName());

		if (MemberPropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCalibrationDiagnosticsOptions, AreaOfInterestsForCameras))
		{
			ImageViewer->UpdateState();
			return;
		}
	}

	const FName PropertyName(InPropertyChangedEvent.Property->GetFName());
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCalibrationDiagnosticsOptions, FeatureMatchErrorThreshold) || 
		PropertyName == GET_MEMBER_NAME_CHECKED(UMetaHumanCalibrationDiagnosticsOptions, CameraCalibration))
	{
		if (FeatureMatcher)
		{
			FeatureMatcher->MarkAsGarbage();
		}

		FeatureMatcher.Reset(NewObject<UMetaHumanRobustFeatureMatcher>());

		bIsFeatureDetectorInitialized = FeatureMatcher->Init(CaptureData.Get(), Options.Get());

		ImageViewer->ResetState();
	}
}

void SCalibrationDiagnosticsWindow::RegisterImageViewerTabSpawner(TSharedRef<FTabManager> InTabManager, const TSharedRef<FWorkspaceItem> InWorkspaceItem)
{
	const auto& ImageViewerTabSpawner = [this](const FSpawnTabArgs& SpawnTabArgs) -> TSharedRef<SDockTab>
		{
			const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
				.Label(LOCTEXT("ImageViewerTabLabel", "Image Viewer"))
				.CanEverClose(false)
				.OnCanCloseTab_Lambda([]()
				{
					return false;
				});

			ImageViewer = SNew(SCalibrationDiagnosticsImageViewer)
				.FootageCaptureData(CaptureData.Get())
				.Options(Options.Get())
				.FeatureDetector(AsSharedSubobject(this));

			DockTab->SetContent(
				ImageViewer.ToSharedRef()
			);

			return DockTab;
		};

	FTabSpawnerEntry& TabSpawnerEntry = InTabManager->RegisterTabSpawner(UE::MetaHuman::Private::ImageViewerTabName, FOnSpawnTab::CreateLambda(ImageViewerTabSpawner))
		.SetDisplayName(LOCTEXT("ImageViewerTabSpawner", "Image Viewer"));

	InWorkspaceItem->AddItem(TabSpawnerEntry.AsShared());
}

void SCalibrationDiagnosticsWindow::RegisterOptionsTabSpawner(TSharedRef<FTabManager> InTabManager, const TSharedRef<FWorkspaceItem> InWorkspaceItem)
{
	const auto& OptionsTabSpawner = [this](const FSpawnTabArgs& SpawnTabArgs) -> TSharedRef<SDockTab>
		{
			const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
				.Label(LOCTEXT("OptionsTabLabel", "Options"))
				.CanEverClose(false)
				.OnCanCloseTab_Lambda([]()
				{
					return false;
				});

			OptionsViewer = SNew(SMetaHumanCalibrationOptionsWidget)
				.Object(Options.Get())
				.OnObjectChanged(this, &SCalibrationDiagnosticsWindow::OnFinishedChangingProperties);

			DockTab->SetContent(
				OptionsViewer.ToSharedRef()
			);

			return DockTab;
		};

	FTabSpawnerEntry& TabSpawnerEntry = InTabManager->RegisterTabSpawner(UE::MetaHuman::Private::OptionsTabName, FOnSpawnTab::CreateLambda(OptionsTabSpawner))
		.SetDisplayName(LOCTEXT("OptionsTabSpawner", "Options"));

	InWorkspaceItem->AddItem(TabSpawnerEntry.AsShared());
}

FDetectedFeatures SCalibrationDiagnosticsWindow::GetDetectedFeatures(int32 InFrameId)
{
	if (!bIsFeatureDetectorInitialized)
	{
		return FDetectedFeatures();
	}

	return FeatureMatcher->GetFeatures(InFrameId);
}

FDetectedFeatures SCalibrationDiagnosticsWindow::DetectFeatures(int32 InFrameId)
{
	if (!bIsFeatureDetectorInitialized)
	{
		return FDetectedFeatures();
	}

	FDetectedFeatures DetectedFeatures = 
		FeatureMatcher->GetFeatures(InFrameId);

	if (DetectedFeatures.IsValid())
	{
		return DetectedFeatures;
	}

	if (!FeatureMatcher->DetectFeatures(InFrameId))
	{
		return FDetectedFeatures();
	}

	return FeatureMatcher->GetFeatures(InFrameId);
}

#undef LOCTEXT_NAMESPACE