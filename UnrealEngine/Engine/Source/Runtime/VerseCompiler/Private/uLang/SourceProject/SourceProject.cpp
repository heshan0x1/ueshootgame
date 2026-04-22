// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/SourceProject/SourceProject.h"
#include "uLang/SourceProject/SourceDataProject.h"
#include "uLang/SourceProject/SourceProjectUtils.h"
#include "uLang/Common/Text/FilePathUtils.h"
#include "uLang/Common/Misc/Archive.h"

namespace uLang
{

//====================================================================================
// CSourceModule implementation
//====================================================================================

CUTF8StringView CSourceModule::GetNameFromFile() const
{
    return GetNameFromFile(GetFilePath());
}

CUTF8StringView CSourceModule::GetNameFromFile(const CUTF8StringView& ModuleFilePath)
{
    if (ModuleFilePath.IsEmpty())
    {
        return CUTF8StringView();
    }

    CUTF8StringView DirPath, FileName;
    FilePathUtils::SplitPath(ModuleFilePath, DirPath, FileName);
    CUTF8StringView Stem, Extension;
    FilePathUtils::SplitFileName(FileName, Stem, Extension);
    return Stem;
}

TOptional<TSRef<CSourceModule>> CSourceModule::FindSubmodule(const CUTF8StringView& ModuleName) const
{
    return _Submodules.FindByKey(ModuleName);
}

void CSourceModule::AddSnippet(const TSRef<ISourceSnippet>& Snippet)
{
    ULANG_ASSERTF(!_SourceSnippets.Contains(Snippet), "Duplicate Snippet `%s`!", *Snippet->GetPath());

    _SourceSnippets.Add(Snippet);
}

bool CSourceModule::RemoveSnippet(const TSRef<ISourceSnippet>& Snippet, bool bRecursive)
{
    return !VisitAll([&Snippet, bRecursive](CSourceModule& Module)
        {
            return Module._SourceSnippets.Remove(Snippet) == 0 && bRecursive;
        });
}

//====================================================================================
// CSourcePackage implementation
//====================================================================================

int32_t CSourcePackage::GetNumSnippets() const
{
    int32_t NumSnippets = 0;
    _RootModule->VisitAll([&NumSnippets](const CSourceModule& Module)
        {
            NumSnippets += Module._SourceSnippets.Num();
            return true;
        });
    return NumSnippets;
}

void CSourcePackage::SetDependencyPackages(TArray<CUTF8String>&& PackageNames)
{
    _Settings._DependencyPackages = Move(PackageNames);
}

void CSourcePackage::AddDependencyPackage(const CUTF8StringView& PackageName)
{
    _Settings._DependencyPackages.Add(PackageName);
}

void CSourcePackage::TruncateVniDestDir()
{
    CUTF8StringView VniParentDir, VniBaseDir;
    if (_Settings._VniDestDir)
    {
        _Settings._VniDestDir = FilePathUtils::GetFileName(*_Settings._VniDestDir);
    }
}

bool CSourcePackage::RemoveSnippet(const uLang::TSRef<ISourceSnippet>& Snippet)
{
    return _RootModule->RemoveSnippet(Snippet, true);
}

void CSourcePackage::GenerateFingerprint(uLang::TSRef<uLang::ISolFingerprintGenerator>& Generator) const
{
    // Hash Package Settings
    _Settings.GenerateFingerprint(Generator);
   
    // This package might be meant to be compiled from source or is a package that was already built and we have a digest for it.
    // Generate the fingerprint appropriately so that we are only fingerprinting data that will be an input to the compilation
    // process and not any data that ends up as an output artifact.
    if (_Settings._Role == ExternalPackageRole && _Digest.IsSet())
    {
        _Digest->GenerateFingerprint(Generator);
    }
    else
    {
        // Gather all snippets recursively across all submodules, sort them, and hash
        TArray<const uLang::ISourceSnippet*> Snippets;
        _RootModule->VisitAll([&Snippets](const uLang::CSourceModule& Module)
            {
                for (const uLang::TSRef<uLang::ISourceSnippet>& SourceSnippet : Module._SourceSnippets)
                {
                    Snippets.Add(SourceSnippet.Get());
                }
                return true;
            });

        Algo::Sort(Snippets, [](const uLang::ISourceSnippet* LHS, const uLang::ISourceSnippet* RHS)
            {
                return LHS->GetPath().ToStringView() < RHS->GetPath().ToStringView();
            });
        for (const uLang::ISourceSnippet* SourceSnippet : Snippets)
        {
            uLang::TOptional<uLang::CUTF8String> Text = SourceSnippet->GetText();
            if (Text.IsSet())
            {
                Generator->Update(Text->AsCString(), Text->ByteLen(), CUTF8String("Snippet - %s", *SourceSnippet->GetPath()).AsCString());
            }
        }
    }
}

CSourceProject::CSourceProject(const CSourceProject& Other)
    : _Packages(Other._Packages)
    , _Name(Other._Name)
{
}

int32_t CSourceProject::GetNumSnippets() const
{
    int32_t NumSnippets = 0;
    for (const SPackage& Package : _Packages)
    {
        NumSnippets += Package._Package->GetNumSnippets();
    }
    return NumSnippets;
}

const CSourceProject::SPackage* CSourceProject::FindPackage(const CUTF8StringView& PackageName, const CUTF8StringView& PackageVersePath) const
{
    return _Packages.FindByPredicate([&PackageName, &PackageVersePath](const SPackage& Package)
    {
        return
            Package._Package->GetName() == PackageName &&
            Package._Package->GetSettings()._VersePath == PackageVersePath;
    });
}

const CSourceProject::SPackage& CSourceProject::FindOrAddPackage(const CUTF8StringView& PackageName, const CUTF8StringView& PackageVersePath)
{
    const SPackage* Package = FindPackage(PackageName, PackageVersePath);
    if (Package)
    {
        return *Package;
    }

    SPackage& NewPackage = _Packages[_Packages.Add({TSRef<CSourcePackage>::New(PackageName, TSRef<CSourceModule>::New("")), false})];
    NewPackage._Package->SetVersePath(PackageVersePath);
    return NewPackage;
}

void CSourceProject::AddSnippet(const uLang::TSRef<ISourceSnippet>& Snippet, const CUTF8StringView& PackageName, const CUTF8StringView& PackageVersePath)
{
    FindOrAddPackage(PackageName, PackageVersePath)._Package->_RootModule->AddSnippet(Snippet);
}

bool CSourceProject::RemoveSnippet(const TSRef<ISourceSnippet>& Snippet)
{
    for (const SPackage& Package : _Packages)
    {
        if (Package._Package->RemoveSnippet(Snippet))
        {
            return true;
        }
    }

    return false;
}

void CSourceProject::TruncateVniDestDirs()
{
    for (const SPackage& Package : _Packages)
    {
        Package._Package->TruncateVniDestDir();
    }
}

void CSourceProject::PopulateTransitiveDependencyMap(uLang::TMap<const CSourcePackage*, uLang::TArray<const CSourcePackage*>>& OutPackageToSortedDependencies)
{
    // Mapping used when walking dependencies
    uLang::TMap<CUTF8String, const CSourcePackage*> PackageNameMap;
    for (int32_t Index = 0; Index < _Packages.Num(); ++Index)
    {
        CSourcePackage* SourcePackage = _Packages[Index]._Package;
        PackageNameMap.Insert(SourcePackage->GetName(), SourcePackage);
    }

    uLang::TSet<const CSourcePackage*> Visited;
    auto PopulateDependencyMapRecursive = [&OutPackageToSortedDependencies, &PackageNameMap, &Visited](
        const CSourcePackage* Package,
        uLang::TSet<const CSourcePackage*>& TransitiveDependencies,
        auto& PopulateDependencyMapRecursiveFn) -> void 
        {
            // Don't follow possible cycles in the graph
            if (Visited.Contains(Package))
            {
                return;
            }
            Visited.Insert(Package);

            // If we already found all transitive dependencies, append them here
            if (OutPackageToSortedDependencies.Contains(Package))
            {
                const uLang::TArray<const CSourcePackage*>& SortedDeps = *OutPackageToSortedDependencies.Find(Package);
                for (const CSourcePackage* Dependency : SortedDeps)
                {
                    TransitiveDependencies.Insert(Dependency);
                }
                return;
            }

            // This is a package we haven't walked before, append its dependencies recursively
            for (const CUTF8String& DependencyName : Package->GetSettings()._DependencyPackages)
            {
                const CSourcePackage** DependentPackage = PackageNameMap.Find(DependencyName);
                ULANG_ASSERTF(DependentPackage, "Cannot find Package for Dependency %s", DependencyName.AsCString());
                TransitiveDependencies.Insert(*DependentPackage);
                PopulateDependencyMapRecursiveFn(
                    *DependentPackage, TransitiveDependencies, PopulateDependencyMapRecursiveFn);
            }

            // Now that recursive calls are complete, remove ourselves from visited in 
            // case the same dependency is shared by multiple parts of the graph.
            Visited.Remove(Package);
        };

    for (int32_t Index = 0; Index < _Packages.Num(); ++Index)
    {
        CSourcePackage* SourcePackage = _Packages[Index]._Package;

        Visited.Empty();
        TSet<const CSourcePackage*> TransitiveDependencies;
        PopulateDependencyMapRecursive(SourcePackage, TransitiveDependencies, PopulateDependencyMapRecursive);

        uLang::TArray<const CSourcePackage*> SortedDeps;
        SortedDeps.Reserve(TransitiveDependencies.Num());
        for (const CSourcePackage* Dependency : TransitiveDependencies)
        {
            SortedDeps.Add(Dependency);
        }
        SortedDeps.StableSort([](const CSourcePackage& A, const CSourcePackage& B) -> bool 
            {
                return A.GetName().ToStringView() < B.GetName().ToStringView();
            });

        // Store this package's transitive dependencies now that we have seen them all.
        // This will accelerate future lookups referring to the same node
        OutPackageToSortedDependencies.Insert(SourcePackage, uLang::MoveIfPossible(SortedDeps));
    }
}

void CSourceProject::GeneratePackageFingerprints(TSRef<ISolFingerprintGenerator>& Generator)
{
    // To ensure we generate fingerprints that will be invalidated when a dependent package changes, while also handling cycles within the dependency graph, we do the following:
    // 1) Generate a fingerprint for each package that doesn't account for its dependencies
    // 2) Find the sorted set of transitive dependencies a package has
    // 3) Generate a fingerprint for a package based on the fingerprint generated in step 1) plus all dependent fingerprints found in step 2)
    // 
    // Note, there is an optimization opportunity here we are avoiding. We are generating fingerprints using the fingerprint of the dependencies
    // directly which implies that a whitespace change in a dependent package will cause a rebuild of dependees which is unnecessary. Follow-up work should
    // look to move fingerprints of dependencies to be more careful about the actual relationship dependencies have for package invalidation.
     
    // Step 1) Generate initial fingerprint for all packages (doesn't account for dependency fingerprints)
    TMap<const CSourcePackage*, FSolFingerprint> PackageInitialFingerprints;
    for (int32_t Index = 0; Index < _Packages.Num(); ++Index)
    {
        CSourcePackage* SourcePackage = _Packages[Index]._Package;

        FSolFingerprint Fingerprint = SourcePackage->GetFingerprint();
        if (Fingerprint == FSolFingerprint::Zero)
        {
            // We haven't set the fingerprint before so do so now
            Generator->Reset();
            SourcePackage->GenerateFingerprint(Generator);
            Fingerprint = Generator->Finalize(CUTF8String("GeneratePackageFingerprints_InitialPackageFingerprint - %s", SourcePackage->GetName().AsCString()).AsCString());
        }

        PackageInitialFingerprints.Insert(SourcePackage, Fingerprint);
    }

    // Step 2) Find transitive dependencies
    uLang::TMap<const CSourcePackage*, uLang::TArray<const CSourcePackage*>> PackageToSortedDependencies;
    PopulateTransitiveDependencyMap(PackageToSortedDependencies);

    // Step 3) Accumulate initial fingerprints for the package and its transitive dependencies
    for (int32_t Index = 0; Index < _Packages.Num(); ++Index)
    {
        CSourcePackage* SourcePackage = _Packages[Index]._Package;
        
        Generator->Reset();

        const FSolFingerprint& InitialPackageFingerprint = *PackageInitialFingerprints.Find(SourcePackage);
        ULANG_ASSERT(InitialPackageFingerprint != FSolFingerprint::Zero);

        Generator->Update(&InitialPackageFingerprint, sizeof(InitialPackageFingerprint),
            CUTF8String("PackageFingerprint (%s : %s)", SourcePackage->GetName().AsCString(), InitialPackageFingerprint.ToCUTF8String().AsCString()).AsCString());

        TArray<const CSourcePackage*>& SortedTransitiveDependencies = *PackageToSortedDependencies.Find(SourcePackage);
        for (const CSourcePackage* DependencyPackage : SortedTransitiveDependencies)
        {
            const FSolFingerprint& DependencyPackageFingerprint = *PackageInitialFingerprints.Find(DependencyPackage);
            Generator->Update(&DependencyPackageFingerprint, sizeof(DependencyPackageFingerprint),
                CUTF8String("\tDependencyFingerprint (%s : %s)", DependencyPackage->GetName().AsCString(), DependencyPackageFingerprint.ToCUTF8String().AsCString()).AsCString());
        }

        const FSolFingerprint CompletePackageFingerprint = Generator->Finalize(CUTF8String("GeneratePackageFingerprints - %s", SourcePackage->GetName().AsCString()).AsCString());
        SourcePackage->SetFingerprint(CompletePackageFingerprint);
    }
}

}
