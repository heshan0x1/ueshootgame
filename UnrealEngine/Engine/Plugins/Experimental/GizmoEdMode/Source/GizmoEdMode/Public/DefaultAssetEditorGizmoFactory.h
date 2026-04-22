// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetEditorGizmoFactory.h"

#include "DefaultAssetEditorGizmoFactory.generated.h"

#define UE_API GIZMOEDMODE_API

UCLASS(MinimalAPI)
class UDefaultAssetEditorGizmoFactory : public UObject, public IAssetEditorGizmoFactory
{
	GENERATED_BODY()
public:
	//IAssetEditorGizmoFactory interface
	UE_API virtual bool CanBuildGizmoForSelection(FEditorModeTools* ModeTools) const override;
	UE_API virtual TArray<UInteractiveGizmo*> BuildGizmoForSelection(FEditorModeTools* ModeTools, UInteractiveGizmoManager* GizmoManager) const override;
	virtual EAssetEditorGizmoFactoryPriority GetPriority() const override { return EAssetEditorGizmoFactoryPriority::Default; }
	UE_API virtual void ConfigureGridSnapping(bool bGridEnabled, bool bRotGridEnabled, const TArray<UInteractiveGizmo*>& Gizmos) const override;
};

#undef UE_API
