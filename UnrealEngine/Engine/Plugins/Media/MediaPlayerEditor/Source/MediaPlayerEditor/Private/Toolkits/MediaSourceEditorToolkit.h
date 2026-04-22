// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorUndoClient.h"
#include "Framework/Docking/TabManager.h"
#include "Styling/ISlateStyle.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/GCObject.h"

#define UE_API MEDIAPLAYEREDITOR_API

class UMediaPlayer;
class UMediaSource;
class UMediaTexture;

/**
 * Implements an Editor toolkit for media sources.
 */
class FMediaSourceEditorToolkit
	: public FAssetEditorToolkit
	, public FEditorUndoClient
	, public FGCObject
{
public:

	/**
	 * Creates and initializes a new instance.
	 *
	 * @param InStyle The style set to use.
	 */
	UE_API FMediaSourceEditorToolkit(const TSharedRef<ISlateStyle>& InStyle);

	/** Virtual destructor. */
	UE_API virtual ~FMediaSourceEditorToolkit();

public:

	/**
	 * Initializes the editor tool kit.
	 *
	 * @param InMediaSource The UMediaSource asset to edit.
	 * @param InMode The mode to create the toolkit in.
	 * @param InToolkitHost The toolkit host.
	 */
	UE_API void Initialize(UMediaSource* InMediaSource, const EToolkitMode::Type InMode, const TSharedPtr<IToolkitHost>& InToolkitHost);

public:

	//~ FAssetEditorToolkit interface

	UE_API virtual FString GetDocumentationLink() const override;
	UE_API virtual void OnClose() override;
	UE_API virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	UE_API virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

public:

	//~ IToolkit interface

	UE_API virtual FText GetBaseToolkitName() const override;
	UE_API virtual FName GetToolkitFName() const override;
	UE_API virtual FLinearColor GetWorldCentricTabColorScale() const override;
	UE_API virtual FString GetWorldCentricTabPrefix() const override;

public:

	//~ FGCObject interface

	UE_API virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FMediaSourceEditorToolkit");
	}

protected:

	//~ FEditorUndoClient interface

	UE_API virtual void PostUndo(bool bSuccess) override;
	UE_API virtual void PostRedo(bool bSuccess) override;

protected:
	/**
	 * Validates that the given player can open the media source with the selected desired player.
	 * If not, the desired player is reset to fallback to the media source selection.
	 */
	static UE_API void ValidateDesiredPlayer(UMediaPlayer* InMediaPlayer, UMediaSource* InMediaSource);

	/** Binds the UI commands to delegates. */
	UE_API void BindCommands();

	/** Builds the toolbar widget for the media player editor. */
	UE_API void ExtendToolBar();

	/**
	 * Gets the playback rate for fast forward.
	 *
	 * @return Forward playback rate.
	 */
	UE_API float GetForwardRate() const;

	/**
	 * Gets the playback rate for reverse.
	 *
	 * @return Reverse playback rate.
	 */
	UE_API float GetReverseRate() const;

	/**
	 * Enqueues rendering commands to generate a thumbnail.
	 */
	UE_API void GenerateThumbnail();

private:

	/** Callback for spawning tabs. */
	UE_API TSharedRef<SDockTab> HandleTabManagerSpawnTab(const FSpawnTabArgs& Args, FName TabIdentifier);

private:

	/** The media player to play the media with. */
	TObjectPtr<UMediaPlayer> MediaPlayer;

	/** The media source asset being edited. */
	TObjectPtr<UMediaSource> MediaSource;

	/** The media texture to output the media to. */
	TObjectPtr<UMediaTexture> MediaTexture;

	/** Pointer to the style set to use for toolkits. */
	TSharedRef<ISlateStyle> Style;
};

#undef UE_API
