// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionBuilderHelpers.h"

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Logging/LogMacros.h"
#include "WorldPartition/WorldPartitionBuilder.h"
#include "Misc/FileHelper.h"
#include "Algo/ForEach.h"
#include "UObject/SavePackage.h"

#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"


void FBuilderModifiedFiles::Add(EFileOperation FileOp, const FString& File)
{
	Files[FileOp].Add(File);
}

const TSet<FString>& FBuilderModifiedFiles::Get(EFileOperation FileOp) const
{
	return Files[FileOp];
}

void FBuilderModifiedFiles::Append(EFileOperation FileOp, const TArray<FString>& InFiles)
{
	Files[FileOp].Append(InFiles);
}

void FBuilderModifiedFiles::Append(const FBuilderModifiedFiles& Other)
{
	Files[EFileOperation::FileAdded].Append(Other.Files[EFileOperation::FileAdded]);
	Files[EFileOperation::FileEdited].Append(Other.Files[EFileOperation::FileEdited]);
	Files[EFileOperation::FileDeleted].Append(Other.Files[EFileOperation::FileDeleted]);
}

void FBuilderModifiedFiles::Empty()
{
	Files[EFileOperation::FileAdded].Empty();
	Files[EFileOperation::FileEdited].Empty();
	Files[EFileOperation::FileDeleted].Empty();
}

TArray<FString> FBuilderModifiedFiles::GetAllFiles() const
{
	TArray<FString> AllFiles;
	AllFiles.Append(Files[EFileOperation::FileAdded].Array());
	AllFiles.Append(Files[EFileOperation::FileEdited].Array());
	AllFiles.Append(Files[EFileOperation::FileDeleted].Array());
	return AllFiles;
}


FSourceControlHelper::FSourceControlHelper(FPackageSourceControlHelper& InPackageHelper, FBuilderModifiedFiles& InModifiedFiles)
	: PackageHelper(InPackageHelper)
	, ModifiedFiles(InModifiedFiles)
{}

FSourceControlHelper::~FSourceControlHelper()
{}

FString FSourceControlHelper::GetFilename(const FString& PackageName) const 
{
	return SourceControlHelpers::PackageFilename(PackageName);
}

FString FSourceControlHelper::GetFilename(UPackage* Package) const 
{
	return SourceControlHelpers::PackageFilename(Package);
}

bool FSourceControlHelper::Checkout(UPackage* Package) const 
{
	bool bCheckedOut = PackageHelper.Checkout(Package);
	if (bCheckedOut)
	{
		const FString Filename = GetFilename(Package);
		const bool bAdded = ModifiedFiles.Get(FBuilderModifiedFiles::EFileOperation::FileAdded).Contains(Filename);
		if (!bAdded)
		{
			ModifiedFiles.Add(FBuilderModifiedFiles::EFileOperation::FileEdited, Filename);
		}		
	}
	return bCheckedOut;
}

bool FSourceControlHelper::Checkout(const FString& Filename) const
{
	bool bCheckedOut = PackageHelper.Checkout({ Filename });
	if (bCheckedOut)
	{
		const bool bAdded = ModifiedFiles.Get(FBuilderModifiedFiles::EFileOperation::FileAdded).Contains(Filename);
		if (!bAdded)
		{
			ModifiedFiles.Add(FBuilderModifiedFiles::EFileOperation::FileEdited, Filename);
		}
	}
	return bCheckedOut;
}

bool FSourceControlHelper::Add(UPackage* Package) const 
{
	bool bAdded = PackageHelper.AddToSourceControl(Package);
	if (bAdded)
	{
		ModifiedFiles.Add(FBuilderModifiedFiles::EFileOperation::FileAdded, GetFilename(Package));
	}
	return bAdded;
}

bool FSourceControlHelper::Add(const FString& Filename) const
{
	bool bAdded = PackageHelper.AddToSourceControl({ Filename });
	if (bAdded)
	{
		ModifiedFiles.Add(FBuilderModifiedFiles::EFileOperation::FileAdded, Filename);
	}
	return bAdded;
}

bool FSourceControlHelper::Delete(const FString& PackageName) const 
{
	bool bDeleted = PackageHelper.Delete(PackageName);
	if (bDeleted)
	{
		ModifiedFiles.Add(FBuilderModifiedFiles::EFileOperation::FileDeleted, PackageName);
	}
	return bDeleted;
}

bool FSourceControlHelper::Delete(UPackage* Package) const 
{
	FString PackageName = GetFilename(Package);
	bool bDeleted = PackageHelper.Delete(Package);
	if (bDeleted)
	{
		ModifiedFiles.Add(FBuilderModifiedFiles::EFileOperation::FileDeleted, PackageName);
	}
	return bDeleted;
}

bool FSourceControlHelper::Save(UPackage* Package) const 
{
	bool bFileExists = IPlatformFile::GetPlatformPhysical().FileExists(*GetFilename(Package));

	// Checkout package
	Package->MarkAsFullyLoaded();

	if (bFileExists)
	{
		if (!Checkout(Package))
		{
			UE_LOG(LogWorldPartitionBuilderSourceControlHelper, Error, TEXT("Error checking out package %s."), *Package->GetName());
			return false;
		}
	}

	// Save package
	FString PackageFileName = GetFilename(Package);
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Standalone;
	SaveArgs.SaveFlags = PackageHelper.UseSourceControl() ? ESaveFlags::SAVE_None : ESaveFlags::SAVE_Async;
	if (!UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs))
	{
		UE_LOG(LogWorldPartitionBuilderSourceControlHelper, Error, TEXT("Error saving package %s."), *Package->GetName());
		return false;
	}

	// Add new package to source control
	if (!bFileExists)
	{
		if (!Add(Package))
		{
			UE_LOG(LogWorldPartitionBuilderSourceControlHelper, Error, TEXT("Error adding package %s to revision control."), *Package->GetName());
			return false;
		}
	}

	return true;
}

bool FSourceControlHelper::Copy(const FString& SrcFilePath, const FString& DstFilePath) const
{
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();

	// Check if the source file exists
	if (!PlatformFile.FileExists(*SrcFilePath))
	{
		UE_LOG(LogWorldPartitionBuilderSourceControlHelper, Error, TEXT("Source file does not exist: %s"), *SrcFilePath);
		return false;
	}

	// Check if destination file already exists
	const bool bDstExists = PlatformFile.FileExists(*DstFilePath);

	// If the destination exists, check it out from source control
	if (bDstExists)
	{
		if (!Checkout(DstFilePath))
		{
			UE_LOG(LogWorldPartitionBuilderSourceControlHelper, Error, TEXT("Error checking out destination file %s."), *DstFilePath);
			return false;
		}
	}

	// Attempt to copy the file
	if (!PlatformFile.CopyFile(*DstFilePath, *SrcFilePath))
	{
		UE_LOG(LogWorldPartitionBuilderSourceControlHelper, Error, TEXT("Error copying file from %s to %s."), *SrcFilePath, *DstFilePath);
		return false;
	}

	// If the destination file didn’t exist before, add it to source control
	if (!bDstExists)
	{
		if (!Add(DstFilePath))
		{
			UE_LOG(LogWorldPartitionBuilderSourceControlHelper, Error, TEXT("Error adding file %s to revision control."), *DstFilePath);
			return false;
		}
	}

	return true;
}