// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanFaceAnimationSolverFactoryNew.h"
#include "MetaHumanFaceAnimationSolver.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanFaceAnimationSolverFactoryNew)

//////////////////////////////////////////////////////////////////////////
// UMetaHumanFaceAnimationSolverFactoryNew

#define LOCTEXT_NAMESPACE "MetaHumanFaceAnimationSolverFactory"

UMetaHumanFaceAnimationSolverFactoryNew::UMetaHumanFaceAnimationSolverFactoryNew()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UMetaHumanFaceAnimationSolver::StaticClass();
}

UObject* UMetaHumanFaceAnimationSolverFactoryNew::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, FFeedbackContext* InWarn)
{
	return NewObject<UMetaHumanFaceAnimationSolver>(InParent, InClass, InName, InFlags);
}

FText UMetaHumanFaceAnimationSolverFactoryNew::GetToolTip() const
{
	return LOCTEXT("MetaHumanFaceAnimationSolverFactory_ToolTip",
		"MetaHuman Face Animation Solver\n"
		"\nHolds configuration info used by the solver.");
}

#undef LOCTEXT_NAMESPACE
