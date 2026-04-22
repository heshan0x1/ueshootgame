// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "WorkflowOrientedApp/WorkflowTabFactory.h"
#include "RigDependencyGraph/SRigDependencyGraph.h"

struct FRigDependencyGraphSummoner : public FWorkflowTabFactory
{
public:
	FRigDependencyGraphSummoner(const TSharedRef<IControlRigBaseEditor>& InControlRigEditor);

	/** FWorkflowTabFactory interface */
	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;
	virtual TSharedPtr<SToolTip> CreateTabToolTipWidget(const FWorkflowTabSpawnInfo& Info) const override;
	virtual TSharedRef<SDockTab> SpawnTab(const FWorkflowTabSpawnInfo& Info) const override;

protected:
	TWeakPtr<IControlRigBaseEditor> ControlRigEditor;
};
