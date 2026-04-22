// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"
#include "Toolkits/AssetEditorToolkit.h"

enum class EMediaCaptureState : uint8;
class SMediaFrameworkTimecodeGenlockHeader;
class SMediaProfileViewport;
class FMediaFrameworkCaptureLevelEditorViewportClient;
class UMediaFrameworkWorldSettingsAssetUserData;
class UMediaOutput;
class UMediaCapture;
class UMediaProfile;
class UMediaProfileEditorCaptureSettings;
class SMediaProfileDetailsPanel;

/**
 * Media profile editor 2.0, which replaces the old, simple editor with a more robust one
 */
class FMediaProfileEditor : public FAssetEditorToolkit
{
private:
	static const FName AppName;
	static const FName MediaOutputTabId;
	static const FName MediaTreeTabId;
	static const FName DetailsTabId;
	static const FName TimecodeTabId;

public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnCaptureMethodChanged, UMediaOutput*)

public:
	static TSharedRef<FMediaProfileEditor> CreateMediaProfileEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, UMediaProfile* InMediaProfile);

	void Initialize(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, UMediaProfile* InMediaProfile);

	virtual ~FMediaProfileEditor() override;
	
	// IToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
	// End of IToolkit interface

	// FAssetEditorToolkit
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	// End of FAssetEditorToolkit

	/** Gets the media profile being edited */
	UMediaProfile* GetMediaProfile() const { return MediaProfileBeingEdited; }

	/** Closes all open media sources in the media profile */
	void CloseAllMediaSources();

	/** Closes all open media outputs in the media profile */
	void CloseAllMediaOutputs();
	
	/** Determines if the specified media output is properly configured for capturing */
	bool CanMediaOutputCapture(UMediaOutput* InMediaOutput) const;

	/** Gets the media capture settings config object */
	static UMediaProfileEditorCaptureSettings* GetMediaFrameworkCaptureSettings();

	/** Gets the delegate that is raised when the capture method is changed through the details panel for a media output */
	FOnCaptureMethodChanged& GetOnCaptureMethodChanged() { return OnCaptureMethodChanged; }
	
private:
	TSharedRef<SDockTab> SpawnTab_MediaOutput(const FSpawnTabArgs& InArgs);
	TSharedRef<SDockTab> SpawnTab_MediaTree(const FSpawnTabArgs& InArgs);
	TSharedRef<SDockTab> SpawnTab_Details(const FSpawnTabArgs& InArgs);
	TSharedRef<SDockTab> SpawnTab_Timecode(const FSpawnTabArgs& InArgs);

	void ExtendToolbar();

	/** Refreshes the selected media items, reopening media sources and outputs */
	void RefreshSelectedMediaItems(const TArray<int32>& InMediaSources,const TArray<int32>& InMediaOutputs);
	
	void OnMediaItemDeleted(UClass* InMediaType, int32 InMediaItemIndex);
	void OnSelectedMediaItemsChanged(const TArray<int32>& SelectedMediaSources, const TArray<int32>& SelectedMediaOutputs);
	
	void OnMapChange(uint32 InMapFlags);
	void OnLevelActorsRemoved(AActor* InActor);
	void OnAssetsDeleted(const TArray<UClass*>& DeletedAssetClasses);
	void OnObjectPreEditChange(UObject* Object, const FEditPropertyChain& PropertyChain);
	void OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent);
	
private:
	/** The media profile being edited by this editor */
	TObjectPtr<UMediaProfile> MediaProfileBeingEdited;

	TSharedPtr<SMediaProfileDetailsPanel> DetailsPanel;
	TSharedPtr<SMediaProfileViewport> ViewportPanel;
	
	/** Timecode/Genlock header widget that is displayed in the editor's primary toolbar */
	TSharedPtr<SMediaFrameworkTimecodeGenlockHeader> TimecodeToolbarEntry;

	/** List of media outputs whose capture needs to be restarted on the next engine tick */
	TSet<TWeakObjectPtr<UMediaOutput>> OutputsToRestartCapture;

	/** Raised when the capture method is changed on a media output through the details panel */
	FOnCaptureMethodChanged OnCaptureMethodChanged;
	
	/** Handle for callback to restart any modified captures on the next engine tick */
	FTimerHandle RestartCapturesTimerHandle;
};
