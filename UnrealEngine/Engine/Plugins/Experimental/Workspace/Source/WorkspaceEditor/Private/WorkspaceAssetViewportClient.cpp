// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorkspaceAssetViewportClient.h"

#include "PreviewScene.h"

namespace UE::Workspace
{
	void FWorkspaceAssetViewportClient::Tick(float DeltaTime)
	{
		if (PreviewScene)
		{
			PreviewScene->GetWorld()->Tick(LEVELTICK_All, DeltaTime);
		}

		FEditorViewportClient::Tick(DeltaTime);
	}
}