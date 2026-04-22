// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetDefinition.h"
#include "CoreMinimal.h"
#include "PropertyPath.h"
#include "Widgets/SWidget.h"

class UMaterialInterface;
class SMaterialEditorUIPreviewViewport;
class SMaterialEditor3DPreviewViewport;
class UMaterial;
class UMaterialExpression;
class UMaterialInstance;

struct FRevisionInfo;

/** Panel used to display the MaterialInstance */
struct FMaterialInstanceDiffPanel : public FGCObject
{
	void SetViewportToDisplay();
	
	TSharedRef<SWidget> GetViewportToDisplay() const;

	void SetPreviewExpression(UMaterialExpression* NewPreviewExpression);

	void SetPreviewMaterial(UMaterialInterface* InMaterialInterface) const;

	void UpdatePreviewMaterial() const;

	TSharedPtr<SMaterialEditor3DPreviewViewport> Preview3DViewport;

	TSharedPtr<SMaterialEditorUIPreviewViewport> Preview2DViewport;

	TObjectPtr<UMaterial> ExpressionPreviewMaterial = nullptr;

	TObjectPtr<UMaterialExpression> PreviewExpression = nullptr;

	TObjectPtr<UMaterialInstance> MaterialInstance = nullptr;

	/** Revision information for this MaterialInstance */
	FRevisionInfo RevisionInfo;
	
	// Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
	// End FGCObject interface
};