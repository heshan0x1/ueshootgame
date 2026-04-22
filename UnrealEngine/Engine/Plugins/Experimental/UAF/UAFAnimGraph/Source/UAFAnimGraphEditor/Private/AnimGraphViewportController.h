// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorkspaceViewportController.h"

namespace UE::UAF::Editor
{
	class FAnimGraphViewportController : public Workspace::IWorkspaceViewportController
	{
		virtual void OnEnter(const FViewportContext& InViewportContext) override;
		virtual void OnExit() override;

		TArray<AActor*> PreviewActors;
	};
}