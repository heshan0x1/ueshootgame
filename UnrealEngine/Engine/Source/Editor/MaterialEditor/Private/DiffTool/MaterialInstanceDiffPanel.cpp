// Copyright Epic Games, Inc. All Rights Reserved.

#include "DiffTool/MaterialInstanceDiffPanel.h"

#include "HAL/PlatformApplicationMisc.h"
#include "MaterialDomain.h"
#include "MaterialEditor/PreviewMaterial.h"
#include "MaterialEditorActions.h"
#include "MaterialShared.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionBreakMaterialAttributes.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialInstance.h"
#include "SMaterialEditorViewport.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "MaterialInstanceDiffPanel"

void FMaterialInstanceDiffPanel::SetViewportToDisplay()
{
	if (!MaterialInstance)
	{
		return;
	}

	if (MaterialInstance && MaterialInstance->IsUIMaterial())
	{
		SAssignNew(Preview2DViewport, SMaterialEditorUIPreviewViewport, MaterialInstance);
	}

	SAssignNew(Preview3DViewport, SMaterialEditor3DPreviewViewport)
		.PreviewMaterial(MaterialInstance);
}

TSharedRef<SWidget> FMaterialInstanceDiffPanel::GetViewportToDisplay() const
{
	if (Preview2DViewport)
	{
		return Preview2DViewport.ToSharedRef();
	}

	return Preview3DViewport ? Preview3DViewport.ToSharedRef() : TSharedRef<SWidget>(SNew(SBorder));
}

void FMaterialInstanceDiffPanel::SetPreviewExpression(UMaterialExpression* NewPreviewExpression)
{
	UMaterialExpressionFunctionOutput* FunctionOutput = Cast<UMaterialExpressionFunctionOutput>(NewPreviewExpression);

	check(MaterialInstance);

	if (!NewPreviewExpression || PreviewExpression == NewPreviewExpression)
	{
		if (FunctionOutput)
		{
			FunctionOutput->bLastPreviewed = false;
		}
		// If we are already previewing the selected expression toggle previewing off
		PreviewExpression = nullptr;
		if(ExpressionPreviewMaterial)
		{
			ExpressionPreviewMaterial->GetExpressionCollection().Empty();
		}
		SetPreviewMaterial(MaterialInstance);
		// Recompile the preview material to get changes that might have been made during previewing
		UpdatePreviewMaterial();
	}
	else
	{
		if (ExpressionPreviewMaterial == nullptr)
		{
			// Create the expression preview material if it hasnt already been created
			ExpressionPreviewMaterial = NewObject<UPreviewMaterial>(GetTransientPackage(), NAME_None, RF_Public);
			ExpressionPreviewMaterial->bIsPreviewMaterial = true;
			ExpressionPreviewMaterial->bEnableNewHLSLGenerator = MaterialInstance->IsUsingNewHLSLGenerator();
			if (MaterialInstance->IsUIMaterial())
			{
				ExpressionPreviewMaterial->MaterialDomain = MD_UI;
			}
			else if (MaterialInstance->IsPostProcessMaterial())
			{
				ExpressionPreviewMaterial->MaterialDomain = MD_PostProcess;
			}
		}

		if (FunctionOutput)
		{
			FunctionOutput->bLastPreviewed = true;
		}
		else
		{
			// Hooking up the output of the break expression doesn't make much sense, preview the expression feeding it instead.
			UMaterialExpressionBreakMaterialAttributes* BreakExpr = Cast<UMaterialExpressionBreakMaterialAttributes>(NewPreviewExpression);
			if (BreakExpr && BreakExpr->GetInput(0) && BreakExpr->GetInput(0)->Expression)
			{
				NewPreviewExpression = BreakExpr->GetInput(0)->Expression;
			}
		}

		// The expression preview material's expressions array must stay up to date before recompiling
		// So that RebuildMaterialFunctionInfo will see all the nested material functions that may need to be updated
		const UMaterial* Material = MaterialInstance->GetMaterial();
		const FMaterialExpressionCollection& ExpressionCollection = Material ? Material->GetExpressionCollection() : FMaterialExpressionCollection{};
		ExpressionPreviewMaterial->AssignExpressionCollection(ExpressionCollection);

		// The preview window should now show the expression preview material
		SetPreviewMaterial(ExpressionPreviewMaterial);

		// Set the preview expression
		PreviewExpression = NewPreviewExpression;

		// Recompile the preview material
		UpdatePreviewMaterial();
	}
}


void FMaterialInstanceDiffPanel::SetPreviewMaterial(UMaterialInterface* InMaterialInterface) const
{
	if (Preview2DViewport)
	{
		Preview2DViewport->SetPreviewMaterial(InMaterialInterface);
	}

	if(Preview3DViewport)
	{
		Preview3DViewport->SetPreviewMaterial(InMaterialInterface);
	}
}

void FMaterialInstanceDiffPanel::UpdatePreviewMaterial() const
{
	if (PreviewExpression && ExpressionPreviewMaterial)
	{
		ExpressionPreviewMaterial->UpdateCachedExpressionData();
		PreviewExpression->ConnectToPreviewMaterial(ExpressionPreviewMaterial, 0);
	}

	if (PreviewExpression)
	{
		check(ExpressionPreviewMaterial);

		// The preview material's expressions array must stay up to date before recompiling
		// So that RebuildMaterialFunctionInfo will see all the nested material functions that may need to be updated
		const UMaterial* Material = MaterialInstance->GetMaterial();
		const FMaterialExpressionCollection& ExpressionCollection = Material ? Material->GetExpressionCollection() : FMaterialExpressionCollection{};
		ExpressionPreviewMaterial->AssignExpressionCollection(ExpressionCollection);
		ExpressionPreviewMaterial->bEnableNewHLSLGenerator = MaterialInstance->IsUsingNewHLSLGenerator();

		FMaterialUpdateContext UpdateContext(FMaterialUpdateContext::EOptions::SyncWithRenderingThread);
		UpdateContext.AddMaterial(ExpressionPreviewMaterial);

		// If we are previewing an expression, update the expression preview material
		ExpressionPreviewMaterial->PreEditChange(nullptr);
		ExpressionPreviewMaterial->PostEditChange();
	}

	// Reregister all components that use the preview material, since UMaterial::PEC does not reregister components using a bIsPreviewMaterial=true material
	if(Preview3DViewport)
	{
		Preview3DViewport->RefreshViewport();
	}
}

void FMaterialInstanceDiffPanel::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ExpressionPreviewMaterial);
	Collector.AddReferencedObject(PreviewExpression);
	Collector.AddReferencedObject(MaterialInstance);
}

FString FMaterialInstanceDiffPanel::GetReferencerName() const
{
	return TEXT("FMaterialInstanceDiffPanel");
}


#undef LOCTEXT_NAMESPACE
