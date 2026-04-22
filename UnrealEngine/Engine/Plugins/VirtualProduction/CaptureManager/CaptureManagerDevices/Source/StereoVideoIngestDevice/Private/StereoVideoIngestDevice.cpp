// Copyright Epic Games, Inc. All Rights Reserved.

#include "StereoVideoIngestDevice.h"

#include "HAL/FileManager.h"
#include "Settings/CaptureManagerSettings.h"
#include "ImageUtils.h"
#include "CaptureManagerMediaRWModule.h"
#include "Asset/CaptureAssetSanitization.h"
#include "Misc/FileHelper.h"
#include "Engine/Texture2D.h"

#include "Misc/MessageDialog.h"
#include "Utils/TakeDiscoveryExpressionParser.h"
#include "Utils/VideoDeviceThumbnailExtractor.h"
#include "Utils/CaptureExtractTimecode.h"
#include "Utils/ParseTakeUtils.h"

#include "VideoLiveLinkDeviceLog.h"

DEFINE_LOG_CATEGORY(LogVideoLiveLinkDevice);
#define LOCTEXT_NAMESPACE "StereoVideoLLDevice"

namespace UE::StereoVideoLiveLinkDevice::Private
{
	static const TArray<FString::ElementType> Delimiters =
	{
		'-',
		'_',
		'.',
		'/'
	};

	static const TArray<FStringView> SupportedVideoExtensions =
	{
		TEXTVIEW("mp4"),
		TEXTVIEW("mov")
	};

	static const TArray<FStringView> SupportedAudioExtensions =
	{
		TEXTVIEW("wav")
	};

	static const TArray<FStringView> SupportedImageSequenceExtensions =
	{
		TEXTVIEW("jpg"),
		TEXTVIEW("jpeg"),
		TEXTVIEW("png")
	};

	FMediaRWManager& GetMediaRWManager()
	{
		return FModuleManager::LoadModuleChecked<FCaptureManagerMediaRWModule>("CaptureManagerMediaRW").Get();
	}

	static constexpr int32 MaximumVideoFilesCountPerTake = 2;
	static constexpr int32 MaximumAudioFilesCountPerTake = 1;

	static constexpr int32 DirectoriesCountToIterateForTakesSearch = 2000;
	static constexpr int32 DirectoriesCountToIterateForVideoAudioFilesSearch = 500;
}

const UStereoVideoIngestDeviceSettings* UStereoVideoIngestDevice::GetSettings() const
{
	return GetDeviceSettings<UStereoVideoIngestDeviceSettings>();
}

TSubclassOf<ULiveLinkDeviceSettings> UStereoVideoIngestDevice::GetSettingsClass() const
{
	return UStereoVideoIngestDeviceSettings::StaticClass();
}

FText UStereoVideoIngestDevice::GetDisplayName() const
{
	return FText::FromString(GetSettings()->DisplayName);
}

EDeviceHealth UStereoVideoIngestDevice::GetDeviceHealth() const
{
	return EDeviceHealth::Nominal;
}

FText UStereoVideoIngestDevice::GetHealthText() const
{
	return FText::FromString("Nominal");
}

void UStereoVideoIngestDevice::OnSettingChanged(const FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::OnSettingChanged(InPropertyChangedEvent);

	const FName& PropertyName = InPropertyChangedEvent.GetMemberPropertyName();

	if (GET_MEMBER_NAME_CHECKED(UStereoVideoIngestDeviceSettings, TakeDirectory) == PropertyName
		|| GET_MEMBER_NAME_CHECKED(UStereoVideoIngestDeviceSettings, VideoDiscoveryExpression) == PropertyName
		|| GET_MEMBER_NAME_CHECKED(UStereoVideoIngestDeviceSettings, AudioDiscoveryExpression) == PropertyName)
	{
		SetConnectionStatus(ELiveLinkDeviceConnectionStatus::Disconnected);

		FString Path = GetSettings()->TakeDirectory.Path;
		if (!Path.IsEmpty() && FPaths::DirectoryExists(Path))
		{
			SetConnectionStatus(ELiveLinkDeviceConnectionStatus::Connected);
		}
		else
		{
			SetConnectionStatus(ELiveLinkDeviceConnectionStatus::Disconnected);
		}
	}
}

FString UStereoVideoIngestDevice::GetFullTakePath(UE::CaptureManager::FTakeId InTakeId) const
{
	if (const FString* TakePath = FullTakePaths.Find(InTakeId))
	{
		return *TakePath;
	}

	return FString();
}

void UStereoVideoIngestDevice::RunConvertAndUploadTake(const UIngestCapability_ProcessHandle* InProcessHandle, const UIngestCapability_Options* InIngestOptions)
{
	AsyncTask(ENamedThreads::AnyThread, [this,
		ProcessHandle = TStrongObjectPtr<const UIngestCapability_ProcessHandle>(InProcessHandle),
		IngestOptions = TStrongObjectPtr<const UIngestCapability_Options>(InIngestOptions)]()
		{
			static constexpr uint32 NumberOfTasks = 2;

			using namespace UE::CaptureManager;
			TSharedPtr<FTaskProgress> TaskProgress
				= MakeShared<FTaskProgress>(NumberOfTasks, FTaskProgress::FProgressReporter::CreateLambda([this, ProcessHandle](double InProgress)
					{
						ExecuteProcessProgressReporter(ProcessHandle.Get(), InProgress);
					}));

			Super::IngestTake(ProcessHandle.Get(), IngestOptions.Get(), MoveTemp(TaskProgress));
		});
}

int32 UStereoVideoIngestDevice::FTakeWithComponents::CountComponents(UStereoVideoIngestDevice::ETakeComponentType Type)
{
	int32 Count = 0;
	for (const UStereoVideoIngestDevice::FTakeWithComponents::Component& Component : Components)
	{
		if (Component.Type == Type)
		{
			++Count;
		}
	}
	return Count;
}

void UStereoVideoIngestDevice::UpdateTakeList_Implementation(UIngestCapability_UpdateTakeListCallback* InCallback)
{
	AsyncTask(ENamedThreads::AnyThread, [this, Callback = TStrongObjectPtr<UIngestCapability_UpdateTakeListCallback>(InCallback)]()
	{
		RemoveAllTakes();

		FString StoragePath = GetSettings()->TakeDirectory.Path;

		TMap<FString, UStereoVideoIngestDevice::FTakeWithComponents> TakesWithComponentsCandidates = DiscoverTakes(StoragePath);

		TArray<FString> SlateNames;
		TakesWithComponentsCandidates.GetKeys(SlateNames);

		for (const FString& SlateName : SlateNames)
		{
			FTakeWithComponents& TakeWithComponents = TakesWithComponentsCandidates[SlateName];

			bool TakeAsExpected = ((TakeWithComponents.CountComponents(ETakeComponentType::VIDEO) == UE::StereoVideoLiveLinkDevice::Private::MaximumVideoFilesCountPerTake && TakeWithComponents.CountComponents(ETakeComponentType::IMAGE_SEQUENCE) == 0) ||
				(TakeWithComponents.CountComponents(ETakeComponentType::VIDEO) == 0 && TakeWithComponents.CountComponents(ETakeComponentType::IMAGE_SEQUENCE) == UE::StereoVideoLiveLinkDevice::Private::MaximumVideoFilesCountPerTake)) &&
				TakeWithComponents.CountComponents(ETakeComponentType::AUDIO) <= UE::StereoVideoLiveLinkDevice::Private::MaximumAudioFilesCountPerTake;

			if (!TakeAsExpected)
			{
				UE_LOG(LogVideoLiveLinkDevice, Warning, TEXT("Invalid take at '%s'. Take should have exactly two video files or two image sequences. Optionally, one audio file."), *TakeWithComponents.TakePath);
				continue;
			}

			TOptional<FTakeMetadata> TakeInfoResult = CreateTakeMetadata(TakeWithComponents);

			if (TakeInfoResult.IsSet())
			{
				FTakeMetadata TakeMetadata = TakeInfoResult.GetValue();

				int32 TakeId = AddTake(MoveTemp(TakeMetadata));
				FullTakePaths.Add(TakeId, TakeWithComponents.TakePath);

				PublishEvent<FIngestCapability_TakeAddedEvent>(TakeId);
			}
		}

		ExecuteUpdateTakeListCallback(Callback.Get(), Execute_GetTakeIdentifiers(this));
	});
}

TMap<FString, UStereoVideoIngestDevice::FTakeWithComponents> UStereoVideoIngestDevice::DiscoverTakes(FString StoragePath)
{
	TMap<FString, FTakeWithComponents> TakesWithComponentsCandidates;

	int32 DirectoriesInterrogatedCount = 0;

	const bool bIterationResult = IFileManager::Get().IterateDirectoryRecursively(*StoragePath, [&](const TCHAR* InPath, bool bInIsDirectory)
		{
			if (bInIsDirectory)
			{
				if (++DirectoriesInterrogatedCount > UE::StereoVideoLiveLinkDevice::Private::DirectoriesCountToIterateForTakesSearch)
				{
					return false;
				}
			}

			FString Extension = FPaths::GetExtension(InPath);
			if (UE::StereoVideoLiveLinkDevice::Private::SupportedVideoExtensions.Contains(Extension))
			{
				ExtractTakeComponents(InPath, StoragePath, ETakeComponentType::VIDEO, GetSettings()->VideoDiscoveryExpression.Value, "UnknownVideoName", TakesWithComponentsCandidates);
			}
			else if (UE::StereoVideoLiveLinkDevice::Private::SupportedImageSequenceExtensions.Contains(Extension))
			{
				ExtractTakeComponents(InPath, StoragePath, ETakeComponentType::IMAGE_SEQUENCE, GetSettings()->VideoDiscoveryExpression.Value, "UnknownImageSequenceName", TakesWithComponentsCandidates);
			}
			else if (UE::StereoVideoLiveLinkDevice::Private::SupportedAudioExtensions.Contains(Extension))
			{
				ExtractTakeComponents(InPath, StoragePath, ETakeComponentType::AUDIO, GetSettings()->AudioDiscoveryExpression.Value, "UnknownAudioName", TakesWithComponentsCandidates);
			}

			return true;
		});

	return TakesWithComponentsCandidates;
}

void UStereoVideoIngestDevice::ExtractTakeComponents(FString ComponentPath, FString StoragePath, ETakeComponentType ComponentType, FString Format, FString UnknownComponentName, TMap<FString, UStereoVideoIngestDevice::FTakeWithComponents>& OutTakesWithComponentsCandidates)
{
	TOptional<FTakeWithComponents> TakeWithComponents;

	if (Format == TEXT("<Auto>"))
	{
		TakeWithComponents = ExtractTakeComponentsFromDirectoryStructure(ComponentPath, StoragePath, ComponentType);
	}
	else
	{
		TakeWithComponents = ExtractTakeComponentsUsingTokens(ComponentPath, StoragePath, Format, ComponentType);
	}

	if (!TakeWithComponents.IsSet())
	{
		FString ComponentRootPath;
		FString FileName;
		FString Extension;
		FPaths::Split(ComponentPath, ComponentRootPath, FileName, Extension);

		TakeWithComponents = { ComponentRootPath, "Slate name not determined", -1, {{UnknownComponentName, ComponentType, ComponentPath}} };
	}

	GroupFoundComponents(OutTakesWithComponentsCandidates, TakeWithComponents.GetValue());
};

TOptional<UStereoVideoIngestDevice::FTakeWithComponents> UStereoVideoIngestDevice::ExtractTakeComponentsUsingTokens(FString ComponentPath, FString StoragePath, FString Format, ETakeComponentType ComponentType)
{
	FStringView InPathStringView(ComponentPath);
	FString RelativePath = FString(InPathStringView.SubStr(StoragePath.Len(), InPathStringView.Len() - StoragePath.Len()));
	FString Extension = FPaths::GetExtension(RelativePath);
	FString RelativePathNoExtension = RelativePath.Mid(0, RelativePath.Len() - Extension.Len() - 1);

	FTakeDiscoveryExpressionParser TokenParser(Format, RelativePathNoExtension, UE::StereoVideoLiveLinkDevice::Private::Delimiters);

	FString StorageLeafWithRelativePath = FPaths::GetPathLeaf(StoragePath) + RelativePathNoExtension;
	FTakeDiscoveryExpressionParser WithLeafTokenParser(Format, StorageLeafWithRelativePath, UE::StereoVideoLiveLinkDevice::Private::Delimiters);

	FString TakePath;
	FTakeDiscoveryExpressionParser* SuccessfulParser = nullptr;
	if (TokenParser.Parse())
	{
		SuccessfulParser = &TokenParser;
		TArray<FString> RelativePathParts;
		FPaths::NormalizeDirectoryName(RelativePath);
		RelativePath.ParseIntoArray(RelativePathParts, TEXT("/"));
		TakePath = StoragePath + TEXT("/") + RelativePathParts[0];
	}
	else if (WithLeafTokenParser.Parse())
	{
		SuccessfulParser = &WithLeafTokenParser;
		TakePath = StoragePath;
	}

	if (SuccessfulParser != nullptr)
	{
		return FTakeWithComponents{ TakePath, SuccessfulParser->GetSlateName(), SuccessfulParser->GetTakeNumber(), { { SuccessfulParser->GetName(), ComponentType, ComponentPath } } };
	}

	return {};
};

TOptional<UStereoVideoIngestDevice::FTakeWithComponents> UStereoVideoIngestDevice::ExtractTakeComponentsFromDirectoryStructure(FString ComponentPath, FString StoragePath, ETakeComponentType ComponentType)
{
	FStringView InPathStringView(ComponentPath);
	FString RelativePath = FString(InPathStringView.SubStr(StoragePath.Len(), InPathStringView.Len() - StoragePath.Len()));

	FString TakePath;
	FString TakeName;

	if (ComponentType == ETakeComponentType::VIDEO || ComponentType == ETakeComponentType::AUDIO)
	{
		// As a default, assume that user user navigates StoragePath to TakeFolder.
		// Then use specified storage path leaf folder as TakeName
		TakeName = FPaths::GetPathLeaf(StoragePath);
		TakePath = StoragePath;


		// Run a quick count of video and audio files in the selected StoragePath
		bool bMoreAudioOrVideoFilesThanExpectedPerTake = false;
		{
			int32 DirectoriesInterrogatedCount = 0;
			int32 VideoFilesFound = 0;
			int32 AudioFilesFound = 0;

			const bool bIterationResult = IFileManager::Get().IterateDirectoryRecursively(*StoragePath, [&](const TCHAR* InPath, bool bInIsDirectory)
				{
					if (bInIsDirectory)
					{
						if (++DirectoriesInterrogatedCount > UE::StereoVideoLiveLinkDevice::Private::DirectoriesCountToIterateForVideoAudioFilesSearch)
						{
							return false;
						}
					}

					FString Extension = FPaths::GetExtension(InPath);
					if (UE::StereoVideoLiveLinkDevice::Private::SupportedVideoExtensions.Contains(Extension))
					{
						VideoFilesFound++;
					}
					else if (UE::StereoVideoLiveLinkDevice::Private::SupportedAudioExtensions.Contains(Extension))
					{
						AudioFilesFound++;
					}

					if (VideoFilesFound > UE::StereoVideoLiveLinkDevice::Private::MaximumVideoFilesCountPerTake 
						|| AudioFilesFound > UE::StereoVideoLiveLinkDevice::Private::MaximumAudioFilesCountPerTake)
					{
						// Once we found more video or audio files than expected for one take, stop iteration over folders
						bMoreAudioOrVideoFilesThanExpectedPerTake = true;
						return false;
					}

					return true;
				});
		}

		// If specified StoragePath contains more than maximum video files count or more than maximum audio files count,
		// further assumption is that user selected a folder that is a container to take folders. 
		if (bMoreAudioOrVideoFilesThanExpectedPerTake)
		{
			// If sub folder exist, use it's name as TakeName
			TArray<FString> Parts;
			FPaths::NormalizeDirectoryName(RelativePath);
			RelativePath.ParseIntoArray(Parts, TEXT("/"));
			if (Parts.Num() > 1)
			{
				TakeName = Parts[0];
				TakePath = StoragePath / TakeName;
			}
			else
			{
				// Invalid take folder
				return {};
			}
		}
	}
	else if (ComponentType == ETakeComponentType::IMAGE_SEQUENCE)
	{
		// Assumption is that frames are stored in separate folders
		TArray<FString> Parts;
		FPaths::NormalizeDirectoryName(RelativePath);
		RelativePath.ParseIntoArray(Parts, TEXT("/"));
		if (Parts.Num() > 2)
		{
			TakeName = Parts[0];
			TakePath = StoragePath + "/" + TakeName;
		}
		else
		{
			TakePath = StoragePath;
			TakeName = FPaths::GetPathLeaf(StoragePath);
		}

		{
			FString Filename, Extension;
			FPaths::Split(ComponentPath, ComponentPath, Filename, Extension);
		}
	}

	if (TakeName.IsEmpty())
	{
		return {}; // Failed to match TakeName
	}

	FString SlateName = TakeName;
	int32 TakeNumber = -1;
	FString Name;

	return FTakeWithComponents{ TakePath, SlateName, TakeNumber, { { Name, ComponentType, ComponentPath } } };
};

void UStereoVideoIngestDevice::GroupFoundComponents(TMap<FString, UStereoVideoIngestDevice::FTakeWithComponents>& TakesWithComponentsCandidates, FTakeWithComponents TakeWithComponents)
{
	FString TakeIdentifier = TakeWithComponents.SlateName + FString::FromInt(TakeWithComponents.TakeNumber);

	FTakeWithComponents* TakeWithComponentsPtr = TakesWithComponentsCandidates.Find(TakeIdentifier);
	if (TakeWithComponentsPtr == nullptr)
	{
		TakeWithComponentsPtr = &TakesWithComponentsCandidates.Add(TakeIdentifier, { TakeWithComponents.TakePath, TakeWithComponents.SlateName, TakeWithComponents.TakeNumber, {} });
	}

	for (FTakeWithComponents::Component& Component : TakeWithComponents.Components)
	{
		FTakeWithComponents::Component* TakeComponentPtr = TakeWithComponentsPtr->Components.FindByPredicate(
			[&Component](const FTakeWithComponents::Component& Other)
			{
				return Other.Path == Component.Path;
			});

		if (TakeComponentPtr == nullptr)
		{
			TakeWithComponentsPtr->Components.Add(MoveTemp(Component));
		}
	}
};

ELiveLinkDeviceConnectionStatus UStereoVideoIngestDevice::GetConnectionStatus_Implementation() const
{
	const UStereoVideoIngestDeviceSettings* DeviceSettings = GetSettings();
	FString Path = DeviceSettings->TakeDirectory.Path;

	if (!Path.IsEmpty() && FPaths::DirectoryExists(Path))
	{
		return ELiveLinkDeviceConnectionStatus::Connected;
	}

	return ELiveLinkDeviceConnectionStatus::Disconnected;
}

FString UStereoVideoIngestDevice::GetHardwareId_Implementation() const
{
	return FPlatformMisc::GetDeviceId();
}

bool UStereoVideoIngestDevice::SetHardwareId_Implementation(const FString& HardwareID)
{
	return false;
}

bool UStereoVideoIngestDevice::Connect_Implementation()
{
	const UStereoVideoIngestDeviceSettings* DeviceSettings = GetSettings();
	FString Path = DeviceSettings->TakeDirectory.Path;

	if (!Path.IsEmpty() && FPaths::DirectoryExists(Path))
	{
		SetConnectionStatus(ELiveLinkDeviceConnectionStatus::Connected);
		return true;
	}

	return false;
}

bool UStereoVideoIngestDevice::Disconnect_Implementation()
{
	SetConnectionStatus(ELiveLinkDeviceConnectionStatus::Disconnected);
	return true;
}

bool UStereoVideoIngestDevice::IsVideoFile(const FString& InFileNameWithExtension)
{
	FString Extension = FPaths::GetExtension(InFileNameWithExtension);

	return UE::StereoVideoLiveLinkDevice::Private::SupportedVideoExtensions.Contains(Extension);
}

bool UStereoVideoIngestDevice::IsFrameInSequenceFile(const FString& InFileNameWithExtension)
{
	FString Extension = FPaths::GetExtension(InFileNameWithExtension);

	return UE::StereoVideoLiveLinkDevice::Private::SupportedImageSequenceExtensions.Contains(Extension);
}

bool UStereoVideoIngestDevice::IsAudioFile(const FString& InFileNameWithExtension)
{
	FString Extension = FPaths::GetExtension(InFileNameWithExtension);

	return UE::StereoVideoLiveLinkDevice::Private::SupportedAudioExtensions.Contains(Extension);
}

TOptional<FTakeMetadata> UStereoVideoIngestDevice::CreateTakeMetadata(const FTakeWithComponents& InTakeWithComponents) const
{
	using namespace UE::CaptureManager;

	FMediaRWManager& MediaManager = UE::StereoVideoLiveLinkDevice::Private::GetMediaRWManager();

	FTakeMetadata TakeMetadata;

	int32 TakeNumber = InTakeWithComponents.TakeNumber;

	if (TakeNumber == INDEX_NONE)
	{
		TakeNumber = 1;
	}

	TakeMetadata.Version.Major = 4;
	TakeMetadata.Version.Minor = 1;

	TakeMetadata.Slate = InTakeWithComponents.SlateName;
	TakeMetadata.TakeNumber = TakeNumber;

	// Create new guid
	TakeMetadata.UniqueId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);

	FString InCurrentTakeFile = InTakeWithComponents.Components[0].Path;
	FFileStatData FileData = IFileManager::Get().GetStatData(*InCurrentTakeFile);
	TakeMetadata.DateTime = FileData.CreationTime;

	TakeMetadata.Device.Model = TEXT("StereoHMC");


	TArray<FTakeWithComponents::Component> VideoComponents = InTakeWithComponents.Components.FilterByPredicate([](const FTakeWithComponents::Component& Component)
	{
		return Component.Type == ETakeComponentType::VIDEO || Component.Type == ETakeComponentType::IMAGE_SEQUENCE;
	});

	check(VideoComponents.Num() == UE::StereoVideoLiveLinkDevice::Private::MaximumVideoFilesCountPerTake);

	bool bVideoFrameRateSet = false;
	FFrameRate VideoFrameRate;

	for (const FTakeWithComponents::Component& VideoComponent : VideoComponents)
	{
		FTakeMetadata::FVideo Video;

		FString Name = VideoComponent.Name;
		if (Name.IsEmpty())
		{
			Name = VideoComponent.Path.Mid(InTakeWithComponents.TakePath.Len() + 1);
			SanitizePackagePath(Name, '_');
		}

		Video.Name = MoveTemp(Name);

		if (VideoComponent.Type == ETakeComponentType::VIDEO)
		{
			Video.Format = FPaths::GetExtension(VideoComponent.Path);
			Video.PathType = FTakeMetadata::FVideo::EPathType::File;

			TValueOrError<TUniquePtr<IVideoReader>, FText> VideoReaderResult = MediaManager.CreateVideoReader(VideoComponent.Path);
			if (VideoReaderResult.HasValue())
			{
				TUniquePtr<IVideoReader> VideoReader = VideoReaderResult.StealValue();
				Video.FrameRate = static_cast<float>(VideoReader->GetFrameRate().AsDecimal());

				if (!bVideoFrameRateSet)
				{
					VideoFrameRate = UE::CaptureManager::ParseFrameRate(Video.FrameRate);
					bVideoFrameRateSet = true;
				}
			}
			else
			{
				UE_LOG(LogVideoLiveLinkDevice, Warning, TEXT("Failed to determine the frame rate for the video file %s. Consider enabling Third Party Encoder in Capture Manager settings."), *VideoComponent.Path);
			}

			if (!TakeMetadata.Thumbnail.GetRawImage().IsSet())
			{
				FVideoDeviceThumbnailExtractor ThumbnailExtractor;
				TOptional<FTakeThumbnailData::FRawImage> RawImageOpt = ThumbnailExtractor.ExtractThumbnail(VideoComponent.Path);

				if (RawImageOpt.IsSet())
				{
					TakeMetadata.Thumbnail = MoveTemp(RawImageOpt.GetValue());
				}
			}

			Video.Path = VideoComponent.Path;

			FString MediaFilePath = FPaths::ConvertRelativePathToFull(InTakeWithComponents.TakePath, Video.Path);

			FCaptureExtractVideoInfo::FResult ExtractorOpt = FCaptureExtractVideoInfo::Create(MediaFilePath);
			if (ExtractorOpt.IsValid())
			{
				FCaptureExtractVideoInfo Extractor = ExtractorOpt.StealValue();
				Video.TimecodeStart = Extractor.GetTimecode().ToString();
				if (FMath::IsNearlyZero(Video.FrameRate))
				{
					Video.FrameRate = static_cast<float>(Extractor.GetFrameRate().AsDecimal());
					VideoFrameRate = Extractor.GetFrameRate();
					bVideoFrameRateSet = true;
				}
			}
			else
			{
				UE_LOG(LogVideoLiveLinkDevice, Warning, TEXT("Failed to determine the timecode for the video file %s. Consider enabling Third Party Encoder in Capture Manager settings."), *VideoComponent.Path);
			}
		}
		else // ETakeComponentType::IMAGE_SEQUENCE
		{
			Video.PathType = FTakeMetadata::FVideo::EPathType::Folder;

			int32 FramesCount = 0;
			bool FirstFrame = true;

			bool bIterationResult = IFileManager::Get().IterateDirectoryRecursively(*VideoComponent.Path, [&](const TCHAR* InPath, bool bInIsDirectory)
				{
					if (FirstFrame)
					{
						FirstFrame = false;

						if (!TakeMetadata.Thumbnail.GetThumbnailData().IsSet())
						{
							TArray<uint8> ThumbnailData;
							if (FFileHelper::LoadFileToArray(ThumbnailData, InPath))
							{
								TakeMetadata.Thumbnail = ThumbnailData;
							}
						}

						FString Path, Filename, Extension;
						FPaths::Split(InPath, Path, Filename, Extension);
						Video.Format = Extension;

						Video.Path = VideoComponent.Path;
						{
							FImage Image;
							FImageUtils::LoadImage(InPath, Image);
							if (Image.IsImageInfoValid())
							{
								Video.FrameWidth = Image.SizeX;
								Video.FrameHeight = Image.SizeY;
							}
						}
					}

					FramesCount++;

					return true;
				});

			Video.FramesCount = FramesCount;
		}

		TakeMetadata.Video.Add(MoveTemp(Video));
	}


	TArray<FTakeWithComponents::Component> AudioComponents = InTakeWithComponents.Components.FilterByPredicate([](const FTakeWithComponents::Component& Component)
	{
		return Component.Type == ETakeComponentType::AUDIO;
	});

	int32 AudioNameCounter = 1;

	for (const FTakeWithComponents::Component& AudioComponent : AudioComponents)
	{	
		FTakeMetadata::FAudio Audio;
		FString Name = AudioComponent.Name;
		if (Name.IsEmpty())
		{
			Name = TEXT("audio") + FString::FromInt(AudioNameCounter++);
		}
		Audio.Name = Name;
		Audio.Path = AudioComponent.Path;
		Audio.Duration = 0;

		FString MediaFilePath = FPaths::ConvertRelativePathToFull(InTakeWithComponents.TakePath, Audio.Path);
		TSharedPtr<FCaptureExtractAudioTimecode> Extractor = MakeShareable(new FCaptureExtractAudioTimecode(MediaFilePath));

		// The video frame rate will be used to calculate the timecode rate if the timecode rate cannot be extracted from the audio file
		// This is why we need to iterate over the video and audio components independently
		if (!bVideoFrameRateSet)
		{
			UE_LOG(LogVideoLiveLinkDevice, Warning, TEXT("Target frame rate from the input video(s) not found. This is used to set the timecode rate for the audio. Audio timecode may be set incorrectly as a result."));
		}

		FCaptureExtractAudioTimecode::FTimecodeInfoResult TimecodeInfoResult = Extractor->Extract(VideoFrameRate);
		if (TimecodeInfoResult.IsValid())
		{
			FTimecodeInfo TimecodeInfo = TimecodeInfoResult.GetValue();

			Audio.TimecodeStart = TimecodeInfo.Timecode.ToString();
			Audio.TimecodeRate = static_cast<float>(TimecodeInfo.TimecodeRate.AsDecimal());
		}

		TakeMetadata.Audio.Add(MoveTemp(Audio));
	}

	return TakeMetadata;
}

#undef LOCTEXT_NAMESPACE
