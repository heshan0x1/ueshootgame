// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageSequenceReader.h"

#include "MediaRWManager.h"

#include "Misc/FileHelper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"

#include "Internationalization/Regex.h"

#define LOCTEXT_NAMESPACE "ImageSequenceReader"

namespace UE::CaptureManager::Private
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

static TArray<FString> GetImageSequenceFilesFromPath(const FString& InFullSequencePath)
{
	TArray<FString> ImageFiles;

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	IFileManager& FileManager = IFileManager::Get();

	bool bIterateResult = FileManager.IterateDirectory(*InFullSequencePath, [&ImageFiles, &ImageWrapperModule](const TCHAR* InFilenameOrDirectory, bool bInIsDirectory)
	{
		if (!bInIsDirectory)
		{
			EImageFormat Format = ImageWrapperModule.GetImageFormatFromExtension(*FPaths::GetExtension(InFilenameOrDirectory));
			if (Format != EImageFormat::Invalid)
			{
				ImageFiles.Add(InFilenameOrDirectory);
			}
		}

		return true;
	});

	return ImageFiles;
}

static bool GetImageDimensionsFromPath(const FString& InImagePath, FIntPoint& OutDimensions)
{
	TArray<uint8> RawFileData;
	if (FFileHelper::LoadFileToArray(RawFileData, *InImagePath))
	{
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(RawFileData.GetData(), RawFileData.Num());
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);

		if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num()))
		{
			OutDimensions.X = ImageWrapper->GetWidth();
			OutDimensions.Y = ImageWrapper->GetHeight();
			return true;
		}
	}

	return false;
}

static UE::CaptureManager::EMediaTexturePixelFormat ConvertFormat(ERGBFormat InFormat, int32 InBitDepth)
{
	switch (InFormat)
	{
		case ERGBFormat::Gray:
			if (InBitDepth == 8)
			{
				return UE::CaptureManager::EMediaTexturePixelFormat::U8_Mono;
			}
			else if (InBitDepth == 16)
			{
				return UE::CaptureManager::EMediaTexturePixelFormat::U16_Mono;
			}
			else
			{
				return UE::CaptureManager::EMediaTexturePixelFormat::Undefined;
			}
		case ERGBFormat::GrayF:
			return UE::CaptureManager::EMediaTexturePixelFormat::F_Mono;
		case ERGBFormat::RGBA:
			return UE::CaptureManager::EMediaTexturePixelFormat::U8_RGBA;
		case ERGBFormat::BGRA:
			return UE::CaptureManager::EMediaTexturePixelFormat::U8_BGRA;
		default:
			return UE::CaptureManager::EMediaTexturePixelFormat::Undefined;
	}
}

}

void FImageSequenceReaderHelper::RegisterReaders(FMediaRWManager& InManager)
{
	TArray<FString> SupportedFormats = { TEXT("image_sequence") };
	InManager.RegisterVideoReader(SupportedFormats, MakeUnique<FImageSequenceReaderFactory>());
}

TUniquePtr<IVideoReader> FImageSequenceReaderFactory::CreateVideoReader()
{
	return MakeUnique<FImageSequenceReader>();
}

FImageSequenceReader::FImageSequenceReader() = default;
FImageSequenceReader::~FImageSequenceReader() = default;

TOptional<FText> FImageSequenceReader::Open(const FString& InDirectoryPath)
{
	if (!FPaths::DirectoryExists(InDirectoryPath))
	{
		FText Error = FText::Format(
			LOCTEXT("ImageSequenceFolderMissing", "Failed to read image sequence directory: {0}"),
			FText::FromString(InDirectoryPath));
		return Error;
	}

	ImagePaths = UE::CaptureManager::Private::GetImageSequenceFilesFromPath(InDirectoryPath);

	if (ImagePaths.IsEmpty())
	{
		FText Error = FText::Format(
			LOCTEXT("ImageSequenceEmptyDirectory", "No images found in the directory: {0}"),
			FText::FromString(InDirectoryPath));
		return Error;
	}

	ImagePaths.Sort([](const FString& InLeft, const FString& InRight)
	{
		FString Dummy;
		FString LeftDigits, RightDigits;
		bool bSuccess = UE::CaptureManager::Private::ExtractInfoFromFileName(InLeft, Dummy, LeftDigits, Dummy);
		bSuccess = bSuccess && UE::CaptureManager::Private::ExtractInfoFromFileName(InRight, Dummy, RightDigits, Dummy);

		if (bSuccess)
		{
			return FCString::Atoi(*LeftDigits) < FCString::Atoi(*RightDigits);
		}

		return InLeft < InRight;
	});

	check(ImagePaths.IsValidIndex(0));
	UE::CaptureManager::Private::GetImageDimensionsFromPath(ImagePaths[0], Dimensions);

	return {};
}

TOptional<FText> FImageSequenceReader::Close()
{
	ImagePaths.Empty();

	return {};
}

TValueOrError<TUniquePtr<UE::CaptureManager::FMediaTextureSample>, FText> FImageSequenceReader::Next()
{
	const int32 CurrentFrame = CurrentFrameNumber;

	if (!ImagePaths.IsValidIndex(CurrentFrame))
	{
		return MakeValue(nullptr);
	}

	const FString& ImagePath = ImagePaths[CurrentFrame];

	TArray<uint8> RawFileData;
	if (!FFileHelper::LoadFileToArray(RawFileData, *ImagePath))
	{
		FText Error = FText::Format(LOCTEXT("ImageSequenceNextFrameReadFailed", "Failed to read frame {0} from {1}"),
									FText::AsNumber(CurrentFrame), FText::FromString(ImagePath));
		return MakeError(MoveTemp(Error));
	}

	TUniquePtr<UE::CaptureManager::FMediaTextureSample> Sample = 
		MakeUnique<UE::CaptureManager::FMediaTextureSample>();

	Sample->Dimensions = Dimensions;
	Sample->Stride = Dimensions.X;
	Sample->Time = FTimespan::FromSeconds(CurrentFrame);

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(RawFileData.GetData(), RawFileData.Num());
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);

	if (!ImageWrapper.IsValid() || !ImageWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num()))
	{
		FText Error = FText::Format(LOCTEXT("ImageSequenceNextDataToImageFailed", "Unsupported image format {0} for {1}"),
									FText::FromString(FPaths::GetExtension(ImagePath, false)), FText::FromString(ImagePath));

		return MakeError(MoveTemp(Error));
	}

	// Emptying the first buffer before obtaining a new one.
	RawFileData.Empty();

	Sample->CurrentFormat = UE::CaptureManager::Private::ConvertFormat(ImageWrapper->GetFormat(), ImageWrapper->GetBitDepth());

	TArray64<uint8> Buffer;
	if (!ImageWrapper->GetRaw(Buffer))
	{
		FText Error = FText::Format(LOCTEXT("ImageSequenceNextDecodeFailed", "Failed to decode the image {0}"),
									FText::FromString(ImagePath));

		return MakeError(MoveTemp(Error));
	}

	Sample->Buffer = MoveTemp(Buffer);

	++CurrentFrameNumber;

	return MakeValue(MoveTemp(Sample));
}

FTimespan FImageSequenceReader::GetDuration() const
{
	return FTimespan::FromSeconds(ImagePaths.Num());
}

FIntPoint FImageSequenceReader::GetDimensions() const
{
	return Dimensions;
}

FFrameRate FImageSequenceReader::GetFrameRate() const
{
	return FFrameRate(1, 1);
}

#undef LOCTEXT_NAMESPACE