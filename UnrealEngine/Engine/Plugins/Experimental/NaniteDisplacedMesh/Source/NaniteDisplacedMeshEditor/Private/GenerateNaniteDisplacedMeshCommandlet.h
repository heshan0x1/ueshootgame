// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetRegistry/ARFilter.h"
#include "Commandlets/Commandlet.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "NaniteDisplacedMesh.h"
#include "NaniteDisplacedMeshFactory.h"
#include "UObject/WeakObjectPtr.h"

#include "GenerateNaniteDisplacedMeshCommandlet.generated.h"

class UPackage;

struct FAssetData;
struct FNaniteDisplacedMeshLinkParameters;

/*
 * Commandlet to help keeping up to date generated nanite displacement mesh assets
 * Iterate all the levels and keep track of the linked mesh used.
 */
UCLASS()
class UGenerateNaniteDisplacedMeshCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:

	static bool IsRunning();

	//~ Begin UCommandlet Interface
	virtual int32 Main(const FString& CmdLineParams) override;
	//~ End UCommandlet Interface

private:

	UNaniteDisplacedMesh* OnLinkDisplacedMesh(UNaniteDisplacedMesh* ExistingDisplacedMesh, FValidatedNaniteDisplacedMeshParams&& InParameters, const FNaniteDisplacedMeshLinkParameters& InLinkParameters);
	void LoadLevel(const FAssetData& AssetData);
	void SavePackages();
	void FreeMemoryIfNeeded();
	bool ShouldFreeMemory() const;
	static TSet<FString> GetPackagesInFolders(const TSet<FString>& Folders, const FString& NamePrefix);

	TSet<FString> LinkedPackageNames;
	TSet<FString> LinkedPackageFolders;
	TSet<FString> AddedPackageNames;
	TSet<FString> AddedOtherPackages;
	bool bSaveModifiedActor = false;

	TSet<UPackage*> PackagesToSave;

	FARCompiledFilter PreCompiledActorsFilter;
	 
	// Soft limit for when the garbage collector should be kicked (it use the same settings as the cook and will load these if found)
	uint64 MemoryMinFreeVirtual = 8192;
	uint64 MemoryMaxUsedVirtual= 98304;
	uint64 MemoryMinFreePhysical= 8192;
	uint64 MemoryMaxUsedPhysical= 0;

	TSharedPtr<class FPathPermissionList> WritableFoldersPermission;
};
