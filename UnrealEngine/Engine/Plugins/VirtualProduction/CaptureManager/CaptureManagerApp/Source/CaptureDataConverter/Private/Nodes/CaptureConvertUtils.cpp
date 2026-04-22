// Copyright Epic Games, Inc. All Rights Reserved.

#include "CaptureConvertUtils.h"

#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"
#include "IImageWrapperModule.h"

#include "Misc/Paths.h"
#include "Internationalization/Regex.h"

namespace UE::CaptureManager
{

bool ExtractInfoFromFileName(const FString& InFileName, FString& OutPrefix, FString& OutDigits, FString& OutExtension)
{
	FRegexPattern Pattern(TEXT("^(.*?)(\\d+)\\.(\\w+)$"));
	FRegexMatcher Matcher(Pattern, InFileName);

	if (Matcher.FindNext())
	{
		OutPrefix = Matcher.GetCaptureGroup(1);
		OutDigits = Matcher.GetCaptureGroup(2);
		OutExtension = Matcher.GetCaptureGroup(3);

		return true;
	}

	return false;
}

FString GetFileFormat(const FString& InFileName)
{
	FString Prefix;
	FString Digits;
	FString Extension;
	if (ExtractInfoFromFileName(InFileName, Prefix, Digits, Extension))
	{
		return FString::Format(TEXT("{0}%0{1}d.{2}"), { Prefix, FString::FromInt(Digits.Len()), Extension });
	}

	return FString();
}

FString GetFileNameFormat(const FString& InDirectory)
{
	IFileManager& FileManager = IFileManager::Get();
	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::Get().LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	FString FileName;
	FileManager.IterateDirectory(*InDirectory, [&FileName, &ImageWrapperModule](const TCHAR* InFilenameOrDirectory, bool bInIsDirectory)
	{
		if (bInIsDirectory)
		{
			return true;
		}

		FString Extension = FPaths::GetExtension(InFilenameOrDirectory, false);
		if (ImageWrapperModule.GetImageFormatFromExtension(*Extension) != EImageFormat::Invalid)
		{
			// Found the correct extension and return false to exit the iteration
			FileName = FPaths::GetCleanFilename(InFilenameOrDirectory);

			return false;
		}

		return true;
	});

	return GetFileFormat(FileName);
}

}
