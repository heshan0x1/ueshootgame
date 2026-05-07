// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "MassEntitySubsystem.h"
#include "MassExecutionContext.h"


#include "SimpleRandomMoveMassProcessor.generated.h"

/**
 * 
 */
UCLASS()
class FIRSTPERSON_API USimpleRandomMoveMassProcessor : public UMassProcessor
{
	GENERATED_BODY()
	
public:
	USimpleRandomMoveMassProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
	
	
};
