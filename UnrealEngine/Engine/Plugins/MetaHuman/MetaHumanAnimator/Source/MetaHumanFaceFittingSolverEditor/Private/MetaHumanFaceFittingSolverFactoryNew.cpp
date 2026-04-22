// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanFaceFittingSolverFactoryNew.h"
#include "MetaHumanFaceFittingSolver.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanFaceFittingSolverFactoryNew)

//////////////////////////////////////////////////////////////////////////
// UMetaHumanFaceFittingSolverFactoryNew

#define LOCTEXT_NAMESPACE "MetaHumanFaceFittingSolverFactory"

UMetaHumanFaceFittingSolverFactoryNew::UMetaHumanFaceFittingSolverFactoryNew()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UMetaHumanFaceFittingSolver::StaticClass();
}

UObject* UMetaHumanFaceFittingSolverFactoryNew::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, UObject* InContext, FFeedbackContext* InWarn)
{
	return NewObject<UMetaHumanFaceFittingSolver>(InParent, InClass, InName, InFlags);
}

FText UMetaHumanFaceFittingSolverFactoryNew::GetToolTip() const
{
	return LOCTEXT("MetaHumanFaceFittingSolverFactory_ToolTip",
		"MetaHuman Face Fitting Solver\n"
		"\nHolds configuration info used by the solver.");
}

#undef LOCTEXT_NAMESPACE
