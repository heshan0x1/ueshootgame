// Copyright Epic Games, Inc. All Rights Reserved.


#include "MediaProfileEditor.h"

#include "IAssetViewport.h"
#include "LevelEditor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "LevelEditorViewport.h"
#include "MediaProfileEditorUserSettings.h"
#include "PropertyEditorModule.h"
#include "SMediaProfileDetailsPanel.h"
#include "SMediaProfileSourcesTreeView.h"
#include "SMediaProfileViewport.h"
#include "CaptureTab/SMediaFrameworkCapture.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/WorldSettings.h"
#include "Modules/ModuleManager.h"
#include "Profile/IMediaProfileManager.h"
#include "Profile/MediaProfile.h"
#include "Profile/MediaProfilePlaybackManager.h"
#include "Profile/MediaProfileSettings.h"
#include "Slate/SceneViewport.h"
#include "UI/MediaFrameworkUtilitiesEditorStyle.h"
#include "UI/SMediaFrameworkTimecodeGenlockHeader.h"
#include "UI/SMediaFrameworkTimecodeGenlockPanel.h"
#include "Widgets/SViewport.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "MediaProfileEditor"

const FName FMediaProfileEditor::AppName =  FName(TEXT("MediaProfileEditor"));
const FName FMediaProfileEditor::MediaOutputTabId =  FName(TEXT("MediaOutput"));
const FName FMediaProfileEditor::MediaTreeTabId =  FName(TEXT("MediaTree"));
const FName FMediaProfileEditor::DetailsTabId =  FName(TEXT("Details"));
const FName FMediaProfileEditor::TimecodeTabId =  FName(TEXT("Timecode"));

namespace MediaProfileEditor
{	
	/**
	 * Determines the target size from the media output if desired size is specified, otherwise,
	 * fallback to the default viewport capture resolution.
	 */
	FIntPoint GetTargetSize(UMediaOutput* InMediaOutput)
	{
		const FIntPoint TargetSize = InMediaOutput->GetRequestedSize();
		if (TargetSize != UMediaOutput::RequestCaptureSourceSize)
		{
			return TargetSize;
		}
		
		return GetMutableDefault<UMediaFrameworkMediaCaptureSettings>()->DefaultViewportCaptureSize;
	}
}

TSharedRef<FMediaProfileEditor> FMediaProfileEditor::CreateMediaProfileEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UMediaProfile* InMediaProfile)
{
	TSharedRef<FMediaProfileEditor> NewEditor = MakeShared<FMediaProfileEditor>();
	NewEditor->Initialize(Mode, InitToolkitHost, InMediaProfile);
	return NewEditor;
}

void FMediaProfileEditor::Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UMediaProfile* InMediaProfile)
{
	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseOtherEditors(InMediaProfile, this);
	MediaProfileBeingEdited = InMediaProfile;

	FEditorDelegates::MapChange.AddSP(this, &FMediaProfileEditor::OnMapChange);
	GEngine->OnLevelActorDeleted().AddSP(this, &FMediaProfileEditor::OnLevelActorsRemoved);
	FEditorDelegates::OnAssetsDeleted.AddSP(this, &FMediaProfileEditor::OnAssetsDeleted);
	FCoreUObjectDelegates::OnPreObjectPropertyChanged.AddSP(this, &FMediaProfileEditor::OnObjectPreEditChange);
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP(this, &FMediaProfileEditor::OnObjectPropertyChanged);
	
	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_MediaProfileEditor_Layout")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.8f)
					->SetHideTabWell(true)
					->AddTab(MediaOutputTabId, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewSplitter()
					->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(0.2f)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.4f)
						->AddTab(MediaTreeTabId, ETabState::OpenedTab)
					)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.6f)
						->AddTab(DetailsTabId, ETabState::OpenedTab)
						->AddTab(TimecodeTabId, ETabState::OpenedTab)
					)
				)
			)
		);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	InitAssetEditor(Mode, InitToolkitHost, AppName, StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, InMediaProfile);

	ExtendToolbar();
	RegenerateMenusAndToolbars();
	
	// TODO: Not sure this is the best place for this, but for now, when we open a media profile, automatically make it the active media profile
	// First, update the media profile settings config to match the media profile's source and output count
	GetMutableDefault<UMediaProfileSettings>()->FillDefaultMediaSourceProxies(InMediaProfile->NumMediaSources(), false);
	GetMutableDefault<UMediaProfileSettings>()->FillDefaultMediaOutputProxies(InMediaProfile->NumMediaOutputs(), false);
	IMediaProfileManager::Get().SetCurrentMediaProfile(InMediaProfile);
}

FMediaProfileEditor::~FMediaProfileEditor()
{
	GEditor->OnLevelViewportClientListChanged().RemoveAll(this);
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
	FCoreUObjectDelegates::OnPreObjectPropertyChanged.RemoveAll(this);
	FEditorDelegates::OnAssetsDeleted.RemoveAll(this);
	GEngine->OnLevelActorDeleted().RemoveAll(this);
	FEditorDelegates::MapChange.RemoveAll(this);
	FEditorDelegates::PrePIEEnded.RemoveAll(this);
	FEditorDelegates::PostPIEStarted.RemoveAll(this);

	CloseAllMediaSources();
}

void FMediaProfileEditor::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenuCategory", "Media Profile Editor"));
	
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(MediaOutputTabId, FOnSpawnTab::CreateSP(this, &FMediaProfileEditor::SpawnTab_MediaOutput))
		.SetDisplayName(LOCTEXT("MediaOutputTabDisplayName", "Media Output"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(MediaTreeTabId, FOnSpawnTab::CreateSP(this, &FMediaProfileEditor::SpawnTab_MediaTree))
		.SetDisplayName(LOCTEXT("MediaTreeTabDisplayName", "Media"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FMediaProfileEditor::SpawnTab_Details))
		.SetDisplayName(LOCTEXT("DetailsTabDisplayName", "Details"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(TimecodeTabId, FOnSpawnTab::CreateSP(this, &FMediaProfileEditor::SpawnTab_Timecode))
		.SetDisplayName(LOCTEXT("TimecodeTabDisplayName", "Timecode/Genlock"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FMediaFrameworkUtilitiesEditorStyle::GetStyleSetName(), TEXT("ToolbarIcon.Timecode")));
}

void FMediaProfileEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(MediaOutputTabId);
	InTabManager->UnregisterTabSpawner(MediaTreeTabId);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
	InTabManager->UnregisterTabSpawner(TimecodeTabId);
}

FName FMediaProfileEditor::GetToolkitFName() const
{
	return FName("MediaProfileEditor");
}

FText FMediaProfileEditor::GetBaseToolkitName() const
{
	return LOCTEXT("MediaProfileEditorLabel", "Media Profile Editor");
}

FText FMediaProfileEditor::GetToolkitName() const
{
	return FText::FromString(MediaProfileBeingEdited->GetName());
}

FText FMediaProfileEditor::GetToolkitToolTipText() const
{
	return FAssetEditorToolkit::GetToolTipTextForObject(MediaProfileBeingEdited);
}

FString FMediaProfileEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("MediaProfileEditor");
}

FLinearColor FMediaProfileEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor::White;
}

void FMediaProfileEditor::CloseAllMediaSources()
{
	if (!IsValid(MediaProfileBeingEdited))
	{
		return;
	}
	
	for (int32 Index = 0; Index < MediaProfileBeingEdited->NumMediaSources(); ++Index)
	{
		UMediaProfilePlaybackManager::FCloseSourceArgs Args;
		Args.Consumer = this;
		
		MediaProfileBeingEdited->GetPlaybackManager()->CloseSourceFromIndex(Index, Args);
	}
}

void FMediaProfileEditor::CloseAllMediaOutputs()
{
	if (!IsValid(MediaProfileBeingEdited))
	{
		return;
	}
	
	for (int32 MediaOutputIndex = 0; MediaOutputIndex < MediaProfileBeingEdited->NumMediaOutputs(); ++MediaOutputIndex)
	{
		MediaProfileBeingEdited->GetPlaybackManager()->CloseOutputFromIndex(MediaOutputIndex);
	}
}

bool FMediaProfileEditor::CanMediaOutputCapture(UMediaOutput* InMediaOutput) const
{
	if (!IsValid(MediaProfileBeingEdited))
	{
		return false;
	}
	
	UMediaProfileEditorCaptureSettings* CaptureSettings = GetMediaFrameworkCaptureSettings();
	if (!CaptureSettings)
	{
		return false;
	}

	// Return true if there is at least one valid capture configuration for the specified media output
	bool bHasValidCaptureSettings = false;
	if (CaptureSettings->CurrentViewportMediaOutput.MediaOutput == InMediaOutput)
	{
		//Check to see if there is a valid editor viewport that can be captured from
		TSharedPtr<FSceneViewport> Viewport = MediaProfileBeingEdited->GetPlaybackManager()->GetActiveViewport(InMediaOutput);
		bHasValidCaptureSettings = Viewport.IsValid();
	}
	
	for (const FMediaFrameworkCaptureCameraViewportCameraOutputInfo& ViewportOutputInfo : CaptureSettings->ViewportCaptures)
	{
		if (ViewportOutputInfo.MediaOutput != InMediaOutput)
		{
			continue;
		}

		const bool bHasValidCamera = ViewportOutputInfo.Cameras.ContainsByPredicate([](const TSoftObjectPtr<AActor>& ActorRef)
		{
			return ActorRef.IsValid();
		});
		
		if (bHasValidCamera)
		{
			bHasValidCaptureSettings = true;
			break;
		}
	}
	
	for (const FMediaFrameworkCaptureRenderTargetCameraOutputInfo& RenderTargetOutputInfo : CaptureSettings->RenderTargetCaptures)
	{
		if (RenderTargetOutputInfo.MediaOutput != InMediaOutput)
		{
			continue;
		}

		if (RenderTargetOutputInfo.RenderTarget)
		{
			bHasValidCaptureSettings = true;
			break;
		}
	}

	return bHasValidCaptureSettings;
}

UMediaProfileEditorCaptureSettings* FMediaProfileEditor::GetMediaFrameworkCaptureSettings()
{
	return GetMutableDefault<UMediaProfileEditorCaptureSettings>();
}

TSharedRef<SDockTab> FMediaProfileEditor::SpawnTab_MediaOutput(const FSpawnTabArgs& InArgs)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("MediaOutputTabLabel", "Media Output"))
		[
			SAssignNew(ViewportPanel, SMediaProfileViewport, SharedThis(this))
		];
}

TSharedRef<SDockTab> FMediaProfileEditor::SpawnTab_MediaTree(const FSpawnTabArgs& InArgs)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("MediaTreeTabLabel", "Media"))
		[
			SNew(SMediaProfileSourcesTreeView, MediaProfileBeingEdited)
			.OnMediaItemDeleted(this, &FMediaProfileEditor::OnMediaItemDeleted)
			.OnSelectedMediaItemsChanged(this, &FMediaProfileEditor::OnSelectedMediaItemsChanged)
		];
}

TSharedRef<SDockTab> FMediaProfileEditor::SpawnTab_Details(const FSpawnTabArgs& InArgs)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("DetailsTabLabel", "Details"))
		[
			SAssignNew(DetailsPanel, SMediaProfileDetailsPanel, SharedThis(this), MediaProfileBeingEdited)
			.OnRefresh(this, &FMediaProfileEditor::RefreshSelectedMediaItems)
		];
}

TSharedRef<SDockTab> FMediaProfileEditor::SpawnTab_Timecode(const FSpawnTabArgs& InArgs)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("TimecodeTabLabel", "TC/Genlock"))
		.ToolTipText(LOCTEXT("TimecodeTabTooltip", "Timecode and Genlock settings"))
		[
			SNew(SMediaFrameworkTimecodeGenlockPanel)
				.MediaProfile(MediaProfileBeingEdited)
		];
}

void FMediaProfileEditor::ExtendToolbar()
{
	TSharedPtr<FExtender> ToolbarExtender = MakeShared<FExtender>();

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateLambda([this](FToolBarBuilder& ToolbarBuilder)
		{
			ToolbarBuilder.AddWidget(SAssignNew(TimecodeToolbarEntry, SMediaFrameworkTimecodeGenlockHeader));
		})
	);

	AddToolbarExtender(ToolbarExtender);
}

void FMediaProfileEditor::RefreshSelectedMediaItems(const TArray<int32>& InMediaSources,const TArray<int32>& InMediaOutputs)
{
	if (!IsValid(MediaProfileBeingEdited))
	{
		return;
	}

	UMediaProfilePlaybackManager* PlaybackManager = MediaProfileBeingEdited->GetPlaybackManager();
	
	for (int32 Index = 0; Index < InMediaSources.Num(); ++Index)
	{
		const int32 SrcIndex = InMediaSources[Index];

		if (PlaybackManager->IsSourceOpenFromIndex(SrcIndex))
		{
			UMediaProfilePlaybackManager::FCloseSourceArgs Args;
			Args.Consumer = this;
			Args.bForceClose = true;
		
			PlaybackManager->CloseSourceFromIndex(SrcIndex, Args);
			PlaybackManager->OpenSourceFromIndex(SrcIndex, this);
		}
	}
	
	for (int32 Index = 0; Index < InMediaOutputs.Num(); ++Index)
	{
		const int32 OutputIndex = InMediaOutputs[Index];
		if (UMediaOutput* MediaOutput = MediaProfileBeingEdited->GetMediaOutput(OutputIndex))
		{
			if (PlaybackManager->IsOutputCapturing(MediaOutput))
			{
				if (UMediaProfileEditorCaptureSettings* CaptureSettings = GetMediaFrameworkCaptureSettings())
				{
					if (CaptureSettings->CurrentViewportMediaOutput.MediaOutput == MediaOutput)
					{
						PlaybackManager->RestartActiveViewportOutput(MediaOutput, CaptureSettings->CurrentViewportMediaOutput.CaptureOptions, CaptureSettings->bAutoRestartCaptureOnChange);
					}

					for (const FMediaFrameworkCaptureCameraViewportCameraOutputInfo& ViewportCapture : CaptureSettings->ViewportCaptures)
					{
						if (ViewportCapture.MediaOutput == MediaOutput)
						{
							PlaybackManager->RestartManagedViewportOutput(MediaOutput, ViewportCapture.CaptureOptions, CaptureSettings->bAutoRestartCaptureOnChange);
						}
					}

					for (const FMediaFrameworkCaptureRenderTargetCameraOutputInfo& RenderTargetCapture : CaptureSettings->RenderTargetCaptures)
					{
						if (RenderTargetCapture.MediaOutput == MediaOutput)
						{
							PlaybackManager->RestartRenderTargetOutput(MediaOutput, RenderTargetCapture.RenderTarget, RenderTargetCapture.CaptureOptions, CaptureSettings->bAutoRestartCaptureOnChange);
						}
					}
				}
			}
		}
	}
}

void FMediaProfileEditor::OnMediaItemDeleted(UClass* InMediaType, int32 InMediaItemIndex)
{
	if (InMediaType->IsChildOf<UMediaSource>())
	{
		UMediaProfilePlaybackManager::FCloseSourceArgs Args;
		Args.Consumer = this;
		Args.bDestroyMediaPlayer = true;
		Args.bForceClose = true;
		
		MediaProfileBeingEdited->GetPlaybackManager()->CloseSourceFromIndex(InMediaItemIndex, Args);
	}
	else if (InMediaType->IsChildOf<UMediaOutput>())
	{
		MediaProfileBeingEdited->GetPlaybackManager()->CloseOutputFromIndex(InMediaItemIndex);
	}

	if (ViewportPanel.IsValid())
	{
		ViewportPanel->ForceClearMediaItem(InMediaType, InMediaItemIndex);
	}
}

void FMediaProfileEditor::OnSelectedMediaItemsChanged(const TArray<int32>& SelectedMediaSources, const TArray<int32>& SelectedMediaOutputs)
{
	if (!DetailsPanel.IsValid())
	{
		return;
	}

	DetailsPanel->SetSelectedMediaItems(SelectedMediaSources, SelectedMediaOutputs);
	ViewportPanel->SetSelectedMediaItems(SelectedMediaSources, SelectedMediaOutputs);
}

void FMediaProfileEditor::OnMapChange(uint32 InMapFlags)
{
	// New map might have different capture settings, so stop all captures and notify of settings changes
	if (!IsValid(MediaProfileBeingEdited))
	{
		return;
	}

	CloseAllMediaOutputs();
}

void FMediaProfileEditor::OnLevelActorsRemoved(AActor* InActor)
{
	if (!IsValid(MediaProfileBeingEdited))
	{
		return;
	}
	
	UMediaProfileEditorCaptureSettings* CaptureSettings = GetMediaFrameworkCaptureSettings();
	if (!CaptureSettings)
	{
		return;
	}
	
	// If the removed actor is referenced by one of the live captures (e.g. a camera in a viewport capture) we need to stop the capture
	for (const FMediaFrameworkCaptureCameraViewportCameraOutputInfo& ViewportOutputInfo : CaptureSettings->ViewportCaptures)
	{
		if (ViewportOutputInfo.Cameras.Contains(InActor))
		{
			MediaProfileBeingEdited->GetPlaybackManager()->CloseOutput(ViewportOutputInfo.MediaOutput);
		}
	}
}

void FMediaProfileEditor::OnAssetsDeleted(const TArray<UClass*>& DeletedAssetClasses)
{
	if (!IsValid(MediaProfileBeingEdited))
	{
		return;
	}
	
	UMediaProfileEditorCaptureSettings* CaptureSettings = GetMediaFrameworkCaptureSettings();
	if (!CaptureSettings)
	{
		return;
	}

	bool bContainsRenderTargets = DeletedAssetClasses.ContainsByPredicate([](const UClass* Class)
	{
		return Class->IsChildOf<UTextureRenderTarget2D>();
	});
	
	if (bContainsRenderTargets)
	{
		// If the deleted asset is a render target, we need to stop all render target captures in case the deleted render target is used in one of them
		for (const FMediaFrameworkCaptureRenderTargetCameraOutputInfo& RenderTargetOutputInfo : CaptureSettings->RenderTargetCaptures)
		{
			MediaProfileBeingEdited->GetPlaybackManager()->CloseOutput(RenderTargetOutputInfo.MediaOutput);
		}
	}
}

void FMediaProfileEditor::OnObjectPreEditChange(UObject* Object, const FEditPropertyChain& PropertyChain)
{
	if (!IsValid(MediaProfileBeingEdited))
	{
		return;
	}
	
	// If the capture settings are about to be changed, stop all captures
	UMediaProfileEditorCaptureSettings* CaptureSettings = GetMediaFrameworkCaptureSettings();
	if (Object == CaptureSettings)
	{
		CloseAllMediaOutputs();
	}
}

void FMediaProfileEditor::OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent)
{
	if (!IsValid(MediaProfileBeingEdited))
	{
		return;
	}
	
	if (!InObject)
	{
		return;
	}

	if (MediaProfileBeingEdited != InObject)
	{
		UMediaProfilePlaybackManager* PlaybackManager = MediaProfileBeingEdited->GetPlaybackManager();
		
		if (UMediaSource* MediaSource = Cast<UMediaSource>(InObject))
		{
			const int32 SrcIndex = MediaProfileBeingEdited->FindMediaSourceIndex(MediaSource);
			if (SrcIndex != INDEX_NONE)
			{
				if (PlaybackManager->IsSourceOpenFromIndex(SrcIndex))
				{
					UMediaProfilePlaybackManager::FCloseSourceArgs Args;
					Args.Consumer = this;
					Args.bForceClose = true;
		
					PlaybackManager->CloseSourceFromIndex(SrcIndex, Args);
					PlaybackManager->OpenSourceFromIndex(SrcIndex, this);
				}
			}
		}

		if (UMediaOutput* MediaOutput = Cast<UMediaOutput>(InObject))
		{
			const int32 OutputIndex = MediaProfileBeingEdited->FindMediaOutputIndex(MediaOutput);
			if (OutputIndex != INDEX_NONE)
			{
				if (PlaybackManager->IsOutputCapturing(MediaOutput))
				{
					if (UMediaProfileEditorCaptureSettings* CaptureSettings = GetMediaFrameworkCaptureSettings())
					{
						if (CaptureSettings->CurrentViewportMediaOutput.MediaOutput == MediaOutput)
						{
							PlaybackManager->RestartActiveViewportOutput(MediaOutput, CaptureSettings->CurrentViewportMediaOutput.CaptureOptions, CaptureSettings->bAutoRestartCaptureOnChange);
						}

						for (const FMediaFrameworkCaptureCameraViewportCameraOutputInfo& ViewportCapture : CaptureSettings->ViewportCaptures)
						{
							if (ViewportCapture.MediaOutput == MediaOutput)
							{
								PlaybackManager->RestartManagedViewportOutput(MediaOutput, ViewportCapture.CaptureOptions, CaptureSettings->bAutoRestartCaptureOnChange);
							}
						}

						for (const FMediaFrameworkCaptureRenderTargetCameraOutputInfo& RenderTargetCapture : CaptureSettings->RenderTargetCaptures)
						{
							if (RenderTargetCapture.MediaOutput == MediaOutput)
							{
								PlaybackManager->RestartRenderTargetOutput(MediaOutput, RenderTargetCapture.RenderTarget, RenderTargetCapture.CaptureOptions, CaptureSettings->bAutoRestartCaptureOnChange);
							}
						}
					}
				}
			}
		}
	}	
}

#undef LOCTEXT_NAMESPACE
