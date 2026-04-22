// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Helpers/PVUtilities.h"

#include "PVDebugVisualizationBase.h"
#include "GeometryCollection/ManagedArray.h"

struct FManagedArrayCollection;

class FPVPointDebugVisualization final : public FPVDebugVisualizationBase
{
public:
	FPVPointDebugVisualization(){};
protected:
	virtual TArray<FVector3f> GetPivotPositions(const FManagedArrayCollection& InCollection) override;
	virtual void GetPivot(const FManagedArrayCollection& InCollection, const int InIndex, FVector3f& OutPos, float& OutScale) override;
};

class FPVFoliageDebugVisualization final : public FPVDebugVisualizationBase
{
public:
	FPVFoliageDebugVisualization(){};
protected:
	virtual TArray<FVector3f> GetPivotPositions(const FManagedArrayCollection& InCollection) override;
	virtual void GetPivot(const FManagedArrayCollection& InCollection, const int InIndex, FVector3f& OutPos, float& OutScale) override;
};

class FPVBranchDebugVisualization final : public FPVDebugVisualizationBase
{
public:
	FPVBranchDebugVisualization(){};
protected:
	virtual TArray<FVector3f> GetPivotPositions(const FManagedArrayCollection& InCollection) override;
	virtual void GetPivot(const FManagedArrayCollection& InCollection, const int InIndex, FVector3f& OutPos, float& OutScale) override;
};
