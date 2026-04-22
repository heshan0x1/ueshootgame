// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Facades/PVRenderingFacade.h"

class UInstancedStaticMeshComponent;
class UMeshComponent;
class FFoliageFacade;
class AActor;
struct FAssetData;
struct FPVExportParams;

namespace PV::Utilities
{
	bool PROCEDURALVEGETATION_API DebugModeEnabled();

	DECLARE_DELEGATE_OneParam(FFoliageComponentCreatedCallback, UMeshComponent*);
	
	void PROCEDURALVEGETATION_API AddFoliageInstancesToActor(const Facades::FFoliageFacade& InFoliageFacade, AActor* InActor, TMap<FString, TObjectPtr<UMeshComponent>>& OutInstancedComponentMap);
	void PROCEDURALVEGETATION_API AddFoliageInstances(const Facades::FFoliageFacade& InFoliageFacade, const UObject* InParent, FFoliageComponentCreatedCallback InCallback, TMap<FString, TObjectPtr<UMeshComponent>>& OutInstancedComponentMap);

	bool PROCEDURALVEGETATION_API ValidateAssetPathAndName(const FString& MeshName, const FString& Path, UClass* InClass, FString& OutError);

	bool IsFileNameValid(FName FileName, FText& Reason);
	bool PROCEDURALVEGETATION_API DoesConflictingPackageExist(const FString& PackageName, UClass* InClass);
	bool PackageExists(const FString& LongPackageName, UClass* AssetClass);
	int32 PROCEDURALVEGETATION_API GetMeshTriangles(const FString InMeshPath);

	bool IsValidPVData(const FManagedArrayCollection& Collection);
}
