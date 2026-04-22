// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetEditorGizmoFactory.h"

#include "PointLightGizmoFactory.generated.h"

#define UE_API LIGHTGIZMOS_API

UCLASS(MinimalAPI)
class UPointLightGizmoFactory : public UObject, public IAssetEditorGizmoFactory
{
	GENERATED_BODY()
public:
	//IAssetEditorGizmoFactory interface
	UE_API virtual bool CanBuildGizmoForSelection(FEditorModeTools* ModeTools) const override;
	UE_API virtual TArray<UInteractiveGizmo*> BuildGizmoForSelection(FEditorModeTools* ModeTools, UInteractiveGizmoManager* GizmoManager) const override;
	virtual EAssetEditorGizmoFactoryPriority GetPriority() const override { return EAssetEditorGizmoFactoryPriority::Normal; }
	UE_API virtual void ConfigureGridSnapping(bool bGridEnabled, bool bRotGridEnabled, const TArray<UInteractiveGizmo*>& Gizmo) const override;
};

#undef UE_API
