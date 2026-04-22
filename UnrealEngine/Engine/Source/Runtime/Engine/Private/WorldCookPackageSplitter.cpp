// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldCookPackageSplitter.h"

#if WITH_EDITOR
#include "Engine/World.h"
#include "Editor.h"

REGISTER_COOKPACKAGE_SPLITTER(FWorldCookPackageSplitter, UWorld);

TMap<UClass*, FWorldCookPackageSplitter::FSubSplitterFactory> FWorldCookPackageSplitter::RegisteredCookPackageSubSplitterFactories;

FWorldCookPackageSplitter::~FWorldCookPackageSplitter()
{
	check(!ReferencedWorld);
}

void FWorldCookPackageSplitter::RegisterCookPackageSubSplitterFactory(UClass* Class, FWorldCookPackageSplitter::FSubSplitterFactory Factory)
{
	check(!RegisteredCookPackageSubSplitterFactories.Contains(Class));
	RegisteredCookPackageSubSplitterFactories.Emplace(Class, MoveTemp(Factory));
}

void FWorldCookPackageSplitter::UnregisterCookPackageSubSplitterFactory(UClass* Class)
{
	check(RegisteredCookPackageSubSplitterFactories.Contains(Class));
	RegisteredCookPackageSubSplitterFactories.Remove(Class);
}

void FWorldCookPackageSplitter::OnWorldCleanup(UWorld* InWorld, bool bSessionEnded, bool bCleanupResources)
{
	checkf(InWorld != ReferencedWorld, TEXT("[Cook] %s is being cleaned up while still referenced by a package splitter."), *GetFullNameSafe(InWorld));
}

void FWorldCookPackageSplitter::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(ReferencedWorld);
}

FString FWorldCookPackageSplitter::GetReferencerName() const
{
	return TEXT("FWorldCookPackageSplitter");
}

bool FWorldCookPackageSplitter::ShouldSplit(UObject* SplitData)
{
	bool bResult = false;
	if (UWorld* World = Cast<UWorld>(SplitData))
	{
		ForEachRegisteredCookPackageSubSplitterFactories(
			[&bResult, World](const FWorldCookPackageSplitter::FSubSplitterFactory& WorldCookPackageSplitterFactory)
			{
				bResult = WorldCookPackageSplitterFactory.ShouldSplit(World);
				return !bResult;
			});
	}
	return bResult;
}

bool FWorldCookPackageSplitter::UseInternalReferenceToAvoidGarbageCollect()
{
	return true;
}

ICookPackageSplitter::EGeneratedRequiresGenerator FWorldCookPackageSplitter::DoesGeneratedRequireGenerator()
{
	return EGeneratedRequiresGenerator::Populate;
}

bool FWorldCookPackageSplitter::RequiresGeneratorPackageDestructBeforeResplit()
{
	return true;
}

ICookPackageSplitter::FGenerationManifest FWorldCookPackageSplitter::ReportGenerationManifest(
	const UPackage* OwnerPackage, const UObject* OwnerObject)
{
	TNotNull<const UWorld*> OwnerWorld = CastChecked<const UWorld>(OwnerObject);

	ReferencedWorld = const_cast<UWorld*>(NotNullGet(OwnerWorld));

	check(!bInitializedPhysicsSceneForSave && !bForceInitializedWorld);
	bInitializedPhysicsSceneForSave = GEditor->InitializePhysicsSceneForSaveIfNecessary(ReferencedWorld, bForceInitializedWorld);

	check(!CookPackageSubSplitters.Num());
	ForEachRegisteredCookPackageSubSplitterFactories(
		[this, OwnerWorld](const FWorldCookPackageSplitter::FSubSplitterFactory& WorldCookPackageSplitterFactory)
		{
			if (WorldCookPackageSplitterFactory.ShouldSplit(OwnerWorld))
			{
				CookPackageSubSplitters.Add({ &WorldCookPackageSplitterFactory, WorldCookPackageSplitterFactory.MakeInstance(OwnerWorld) });
			}
		});
	check(CookPackageSubSplitters.Num());

	ICookPackageSplitter::FGenerationManifest Manifest;
	ForEachCookPackageSubSplitters(
		[this, &Manifest, OwnerPackage, OwnerWorld](FWorldCookPackageSplitter::ISubSplitter* WorldCookPackageSplitter)
		{
			const TArray<ICookPackageSplitter::FGeneratedPackage> GenerateList = WorldCookPackageSplitter->GetGenerateList(OwnerPackage);
			
			Manifest.GeneratedPackages.Reserve(Manifest.GeneratedPackages.Num() + GenerateList.Num());
			for (const ICookPackageSplitter::FGeneratedPackage& GeneratePackage : GenerateList)
			{
				ICookPackageSplitter::FGeneratedPackage LocalGeneratePackage(GeneratePackage);

				if (LocalGeneratePackage.GeneratedRootPath.IsEmpty())
				{
					LocalGeneratePackage.GeneratedRootPath = OwnerPackage->GetName();
				}

				Manifest.GeneratedPackages.Add(LocalGeneratePackage);

				const TPair<FName, FName> FullPackageName = { *LocalGeneratePackage.GeneratedRootPath, *LocalGeneratePackage.RelativePath };
				check(!SplittersGenerateListMap.Contains(FullPackageName));
				SplittersGenerateListMap.Add(FullPackageName, WorldCookPackageSplitter);
			}
		});

	FWorldDelegates::OnWorldCleanup.AddRaw(this, &FWorldCookPackageSplitter::OnWorldCleanup);

	return Manifest;
}

bool FWorldCookPackageSplitter::PopulateGeneratedPackage(FPopulateContext& PopulateContext)
{
	const ICookPackageSplitter::FGeneratedPackageForPopulate* TargetGeneratedPackage = PopulateContext.GetTargetGeneratedPackage();
	const TPair<FName, FName> FullPackageName = { *TargetGeneratedPackage->GeneratedRootPath, *TargetGeneratedPackage->RelativePath };
	ISubSplitter* SubSplitter = SplittersGenerateListMap.FindChecked(FullPackageName);
	return SubSplitter->PopulateGeneratedPackage(PopulateContext);
}

bool FWorldCookPackageSplitter::PopulateGeneratorPackage(FPopulateContext& PopulateContext)
{
	bool bResult = false;
	ForEachCookPackageSubSplitters(
		[&bResult, &PopulateContext](FWorldCookPackageSplitter::ISubSplitter* WorldCookPackageSplitter)
		{
			bResult = WorldCookPackageSplitter->PopulateGeneratorPackage(PopulateContext);
			return bResult;
		});
	return bResult;
}

void FWorldCookPackageSplitter::OnOwnerReloaded(UPackage* OwnerPackage, UObject* OwnerObject)
{
	check(!ReferencedWorld);
}

void FWorldCookPackageSplitter::Teardown(ICookPackageSplitter::ETeardown Status)
{
	FWorldDelegates::OnWorldCleanup.RemoveAll(this);

	ForEachCookPackageSubSplitters(
		[Status](FWorldCookPackageSplitter::ISubSplitter* WorldCookPackageSplitter)
		{
			WorldCookPackageSplitter->Teardown(Status);
		});

	SplittersGenerateListMap.Empty();

	for (const TPair<const FWorldCookPackageSplitter::FSubSplitterFactory*, ISubSplitter*>& CookPackageSubSplitterPair : CookPackageSubSplitters)
	{
		CookPackageSubSplitterPair.Key->ReleaseInstance(ReferencedWorld, CookPackageSubSplitterPair.Value);
	}
	CookPackageSubSplitters.Empty();

	if (bInitializedPhysicsSceneForSave)
	{
		GEditor->CleanupPhysicsSceneThatWasInitializedForSave(ReferencedWorld, bForceInitializedWorld);
		bInitializedPhysicsSceneForSave = false;
		bForceInitializedWorld = false;
	}

	ReferencedWorld = nullptr;
}
#endif // WITH_EDITOR