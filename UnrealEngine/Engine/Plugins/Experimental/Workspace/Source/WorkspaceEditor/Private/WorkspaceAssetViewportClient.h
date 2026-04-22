// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorViewportClient.h"

namespace UE::Workspace
{
	class FWorkspaceAssetViewportClient : public FEditorViewportClient
	{
	public:
		FWorkspaceAssetViewportClient(FEditorModeTools* InModeTools, FPreviewScene* InPreviewScene)
			: FEditorViewportClient(InModeTools, InPreviewScene)
		{
		}
		
		// FEditorViewportClient interface
		virtual void Tick(float DeltaTime) override;
	};
}