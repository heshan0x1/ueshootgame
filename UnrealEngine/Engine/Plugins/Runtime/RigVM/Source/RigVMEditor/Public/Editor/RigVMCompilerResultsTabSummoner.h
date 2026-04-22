// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"

#define UE_API RIGVMEDITOR_API

#define LOCTEXT_NAMESPACE "RigVMEditor"

//////////////////////////////////////////////////////////////////////////
// FCompilerResultsSummoner

struct FRigVMCompilerResultsTabSummoner : public FWorkflowTabFactory
{

public:

	static const FName TabID() { static FName ID = TEXT("RigVM Compiler Results"); return ID; }
	
	UE_API FRigVMCompilerResultsTabSummoner(TSharedPtr<class FAssetEditorToolkit> InHostingApp);

	UE_API virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;

	virtual FText GetTabToolTipText(const FWorkflowTabSpawnInfo& Info) const override
	{
		return LOCTEXT("CompilerResultsTooltip", "The compiler results tab shows any errors or warnings generated when compiling this Blueprint.");
	}
};

#undef LOCTEXT_NAMESPACE

#undef UE_API
