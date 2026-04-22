// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Facades/PVPointFacade.h"
#include "Facades/PVBranchFacade.h"
#include "Facades/PVFoliageFacade.h"
#include "GeometryCollection/ManagedArrayCollection.h"

#include "PVCarve.generated.h"

UENUM(BlueprintType)
enum class ECarveBasis : uint8
{
	LengthFromRoot UMETA(DisplayName = "Length from root"),
	FromBottom UMETA(DisplayName = "From Bottom"),
	ZPosition UMETA(DisplayName = "Z Position"),
	Radius UMETA(DisplayName = "Radius")
};

USTRUCT()
struct FPVCarveParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Carve", meta=(Tooltip="Sets the reference used to trim the plant.\n\nChooses the basis for trimming the plant structure (e.g., length from root, bottom height, world Z, or radial distance). \nLengthFromRoot – distance measured from the plant’s root/trunk base.\n\nFrom Bottom – relative height starting from the plant’s lowest point.\n\nZPosition – absolute world-space Z axis position.\n\nRadius – Radius/Thickness of branch"))
	ECarveBasis CarveBasis = ECarveBasis::LengthFromRoot;

	UPROPERTY(EditAnywhere, Category="Carve", meta=(ClampMin=0.0f, ClampMax=1.0f, UIMin=0.0f, UIMax=1.0f, Tooltip="Amount of trimming based on the selected basis.\n\nControls how much of the structure is removed according to the chosen Carve Basis. Lower values keep more geometry; higher values trim more. Use to reveal or mask regions procedurally."))
	float Carve = 0.0f;
};

struct FPVCarve
{
	static void ApplyCarve(FManagedArrayCollection& OutCollection, const FManagedArrayCollection& SourceCollection, const ECarveBasis CarveBasis,
	                       const float Carve);

private:
	static void UpdatePointScales(PV::Facades::FPointFacade& PointFacadeOut, const PV::Facades::FPointFacade& PointFacadeSource,
	                              const TArray<int>& BranchPoints, const float LastPointScale, const int32 LastPointIndex, const float CarveRatio,
	                              const int32 EndIndex, const float FirstPointTargetPScale);

	static void RecomputeAttributes(FManagedArrayCollection& OutCollection, const FManagedArrayCollection& SourceCollection,
	                                const TMap<int32, int32>& BranchesNewIDsToOldIDs);

	static void CarveFromTop(FManagedArrayCollection& OutCollection, const FManagedArrayCollection& SourceCollection, const float Carve,
	                         const ECarveBasis CarveBasis);

	static void CarveFromBottom(FManagedArrayCollection& OutCollection, const FManagedArrayCollection& SourceCollection, const float Carve);

	static void ComputeMetadata(TMap<int32, int32>& OutBranchNumbersToBranchIDs, TMap<int32, float>& OutBranchNumbersToLengthFromRoots,
	                            const PV::Facades::FBranchFacade& BranchFacadeSource, const PV::Facades::FPointFacade& PointFacadeSource);

	static void RemoveEntriesAndRecomputeAttributes(FManagedArrayCollection& OutCollection, const FManagedArrayCollection& SourceCollection,
	                                                TArray<bool>& PointsToRemove, TArray<bool>& BranchesToRemove,
	                                                TArray<bool>& FoliageInstancesToRemove);
};
