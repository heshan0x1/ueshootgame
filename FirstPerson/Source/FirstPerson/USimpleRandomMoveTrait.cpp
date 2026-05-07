// Fill out your copyright notice in the Description page of Project Settings.

#include "USimpleRandomMoveTrait.h"
#include "FSimpleRandomMoveFragment.h"
#include "MassEntityTemplateRegistry.h"

void UUSimpleRandomMoveTrait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	BuildContext.AddFragment<FSimpleRandomMoveFragment>();
}


