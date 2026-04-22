// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SAssetEditorViewport.h"
#include "SCommonEditorViewportToolbarBase.h"

class UWorkspaceViewportSceneDescription;

namespace UE::Workspace
{
	class SWorkspaceViewport : public SAssetEditorViewport, public ICommonEditorViewportToolbarInfoProvider, public FGCObject
	{
	public:
		SLATE_BEGIN_ARGS(SWorkspaceViewport){}
			SLATE_ARGUMENT(TWeakPtr<FAssetEditorToolkit>, AssetEditorToolkit)
			SLATE_ARGUMENT(TSharedPtr<FEditorViewportClient>, ViewportClient)
			SLATE_ARGUMENT(UWorkspaceViewportSceneDescription*, SceneDescription)
			SLATE_ATTRIBUTE(FSoftObjectPath, PreviewAssetPath)
			SLATE_ATTRIBUTE(bool, bIsPinned)
			SLATE_EVENT(FSimpleDelegate, OnPinnedClicked)
		SLATE_END_ARGS()
		
		TWeakPtr<FAssetEditorToolkit> AssetEditorToolkit = nullptr;

		void Construct(const FArguments& InArgs);
		
		// SEditorViewport interface
		virtual TSharedPtr<SWidget> BuildViewportToolbar() override;
		virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
		
		// ICommonEditorViewportToolbarInfoProvider interface
		virtual TSharedRef<SEditorViewport> GetViewportWidget() override;
		virtual TSharedPtr<FExtender> GetExtenders() const override;
		virtual void OnFloatingButtonClicked() override;

		virtual void AddReferencedObjects( FReferenceCollector& Collector ) override;
		virtual FString GetReferencerName() const override
		{
			return TEXT("UE::Workspace::SWorkspaceViewport");
		}

	private:
		TAttribute<FSoftObjectPath> PreviewAssetPath;
		TAttribute<bool> bIsPinned;
		FSimpleDelegate OnPinnedClicked;

		TObjectPtr<UWorkspaceViewportSceneDescription> SceneDescription;
	};
}
