// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/UEdMode.h"

#include "GizmoEdMode.generated.h"

#define UE_API GIZMOEDMODE_API

class IAssetEditorGizmoFactory;
template <typename InterfaceType> class TScriptInterface;

class UCombinedTransformGizmo;
class UInteractiveGizmo;
class UInteractiveGizmoManager;

UCLASS(MinimalAPI)
class UGizmoEdModeSettings : public UObject
{
	GENERATED_BODY()
};

UCLASS(MinimalAPI)
class UGizmoEdMode : public UEdMode
{
	GENERATED_BODY()
public:
	UE_API UGizmoEdMode();

	UE_API void AddFactory(TScriptInterface<IAssetEditorGizmoFactory> GizmoFactory);
	virtual bool UsesToolkits() const override
	{
		return false;
	}

private:
	UE_API void ActorSelectionChangeNotify() override;
	UE_API void Enter() override;
	UE_API void Exit() override;
	UE_API void ModeTick(float DeltaTime) override;

	bool IsCompatibleWith(FEditorModeID OtherModeID) const override { return true; }

	UE_API void RecreateGizmo();
	UE_API void DestroyGizmo();

	UPROPERTY()
	TArray<TScriptInterface<IAssetEditorGizmoFactory>> GizmoFactories;
	IAssetEditorGizmoFactory* LastFactory = nullptr;

	UPROPERTY()
	TArray<TObjectPtr<UInteractiveGizmo>> InteractiveGizmos;

	FDelegateHandle WidgetModeChangedHandle;

	bool bNeedInitialGizmos{false};
};

#undef UE_API
