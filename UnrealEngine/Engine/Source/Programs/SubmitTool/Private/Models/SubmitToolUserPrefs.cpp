// Copyright Epic Games, Inc. All Rights Reserved.


#include "SubmitToolUserPrefs.h"
#include "Misc/Paths.h"
#include "Misc/StringOutputDevice.h"
#include "HAL/FileManager.h"

FString FSubmitToolUserPrefs::FilePath = FString();
FSubmitToolUserPrefs* FSubmitToolUserPrefs::Instance = nullptr;

FSubmitToolUserPrefs::~FSubmitToolUserPrefs()
{
	if(Instance != nullptr)
	{
		FString OutputText;
		FSubmitToolUserPrefs::StaticStruct()->ExportText(OutputText, Instance, Instance, nullptr, PPF_None, nullptr);

		FArchive* File = IFileManager::Get().CreateFileWriter(*FilePath, EFileWrite::FILEWRITE_EvenIfReadOnly);
		if (File != nullptr)
		{
			*File << OutputText;
			File->Close();
			delete File;
			File = nullptr;
			UE_LOG(LogSubmitToolDebug, Verbose, TEXT("Saved User Prefs to %s:\n%s"), *FilePath, *OutputText);
		}
		else
		{
			UE_LOG(LogSubmitToolDebug, Verbose, TEXT("Failed to create a file for User Prefs with path %s:"), *FilePath);
		}
		Instance = nullptr;
	}
}

TUniquePtr<FSubmitToolUserPrefs> FSubmitToolUserPrefs::Initialize(const FString& InFilePath)
{
	FilePath = InFilePath;
	TUniquePtr<FSubmitToolUserPrefs> NewPrefs = MakeUnique<FSubmitToolUserPrefs>();
	if(FPaths::FileExists(FilePath))
	{
		FString InText;
		FArchive* File = IFileManager::Get().CreateFileReader(*FilePath, EFileRead::FILEREAD_None);
		if (File != nullptr)
		{
			*File << InText;
			File->Close();
			delete File;
			File = nullptr;
			FStringOutputDevice Errors;
			FSubmitToolUserPrefs::StaticStruct()->ImportText(*InText, NewPrefs.Get(), nullptr, 0, &Errors, FSubmitToolUserPrefs::StaticStruct()->GetName());

			if(!Errors.IsEmpty())
			{
				UE_LOG(LogSubmitTool, Error, TEXT("Error loading User prefs file %s, using defaults"), *Errors);
			}
			else
			{
				UE_LOG(LogSubmitToolDebug, Verbose, TEXT("Loaded User Prefs from %s:\n%s"), *FilePath, *InText);
			}
		}
		else
		{
			UE_LOG(LogSubmitToolDebug, Verbose, TEXT("Failed to open file for reading User Prefs %s:"), *FilePath);
		}

		if(Instance != nullptr)
		{
			UE_LOG(LogSubmitTool, Warning, TEXT("UserPrefs have been reloaded"));
		}
	}
	else
	{
		UE_LOG(LogSubmitTool, Warning, TEXT("File %s does not exist, generating one."), *FilePath);
	}

	Instance = NewPrefs.Get();

	return NewPrefs;
}
