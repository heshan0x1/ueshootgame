// Copyright Epic Games, Inc. All Rights Reserved.


#include "Profile/MediaProfile.h"

#include "IAnalyticsProviderET.h"
#include "MediaFrameworkUtilitiesModule.h"

#include "Engine/Engine.h"
#include "Engine/EngineCustomTimeStep.h"
#include "Engine/TimecodeProvider.h"
#include "MediaAssets/ProxyMediaOutput.h"
#include "MediaAssets/ProxyMediaSource.h"
#include "Profile/IMediaProfileManager.h"
#include "Profile/MediaProfilePlaybackManager.h"

#if WITH_EDITOR
#include "EngineAnalytics.h"
#endif

#if WITH_EDITOR
namespace MediaProfileAnalytics
{
	template <typename ObjectType>
	auto JoinObjectNames = [](const TArray<ObjectType*>& InObjects) -> FString
	{
		TArray<FString> SourceNames;
		Algo::TransformIf(
			InObjects,
			SourceNames,
			[](ObjectType* Object)
			{
				return !!Object;
			},
			[](ObjectType* Object)
			{
				return Object->GetName();
			});

		TStringBuilder<64> StringBuilder;
		StringBuilder.Join(SourceNames, TEXT(","));
		return FString(StringBuilder);
	};
}
#endif

UMediaProfile::UMediaProfile(const FObjectInitializer& ObjectInitializer)
{
	PlaybackManager = CreateDefaultSubobject<UMediaProfilePlaybackManager>(TEXT("PlaybackManager"), true);
}

UMediaSource* UMediaProfile::GetMediaSource(int32 Index) const
{
	if (MediaSources.IsValidIndex(Index))
	{
		return MediaSources[Index];
	}
	return nullptr;
}

void UMediaProfile::SetMediaSource(int32 Index, UMediaSource* InMediaSource)
{
	if (MediaSources.IsValidIndex(Index))
	{
		FString CurrentMediaSourceName = MediaSources[Index] ? MediaSources[Index]->GetName() : TEXT("");
		MediaSources[Index] = InMediaSource;
		
#if WITH_EDITOR
		FPropertyChangedEvent PropertyChangedEvent(StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMediaProfile, MediaSources)), EPropertyChangeType::ValueSet);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(this, PropertyChangedEvent);
		
		// If the original label for the media source was the source's object name (indicating the user has not changed it)
		// update the label to the new source's object name
		if (!MediaSourceLabels.IsValidIndex(Index) || GetLabelForMediaSource(Index) == CurrentMediaSourceName)
		{
			SetLabelForMediaSource(Index, InMediaSource->GetName());
		}
#endif
	}
}

int32 UMediaProfile::NumMediaSources() const
{
	return MediaSources.Num();
}

int32 UMediaProfile::FindMediaSourceIndex(UMediaSource* MediaSource) const
{
	return MediaSources.Find(MediaSource);
}

void UMediaProfile::AddMediaSource(UMediaSource* MediaSource)
{
	MediaSources.Add(MediaSource);

#if WITH_EDITOR
	if (MediaSourceLabels.Num() < MediaSources.Num())
	{
		for (int32 Index = MediaSourceLabels.Num(); Index < MediaSources.Num(); ++Index)
		{
			FString Name = MediaSources[Index] ? MediaSources[Index]->GetName() : TEXT("");
			MediaSourceLabels.Add(Name);
		}
	}
	else
	{
		FString Name = MediaSource ? MediaSource->GetName() : TEXT("");
		const int32 Index = MediaSources.Num() - 1;
		if (MediaSourceLabels.IsValidIndex(Index))
		{
			MediaSourceLabels[Index] = Name;
		}
		else
		{
			MediaSourceLabels.Add(Name);
		}
	}
#endif
}

bool UMediaProfile::RemoveMediaSource(int32 Index)
{
	if (!MediaSources.IsValidIndex(Index))
	{
		return false;
	}
	
	PlaybackManager->ChangeMediaSourceIndex(Index, INDEX_NONE);
	MediaSources.RemoveAt(Index, EAllowShrinking::No);

#if WITH_EDITOR
	if (MediaSourceLabels.IsValidIndex(Index))
	{
		MediaSourceLabels.RemoveAt(Index, EAllowShrinking::No);
	}
#endif
	
	return true;
}

bool UMediaProfile::MoveMediaSource(int32 CurrentIndex, int32 DestIndex)
{
	if (!MediaSources.IsValidIndex(CurrentIndex) || !MediaSources.IsValidIndex(DestIndex))
	{
		return false;
	}

	PlaybackManager->ChangeMediaSourceIndex(CurrentIndex, DestIndex);
	TObjectPtr<UMediaSource> MediaSource = MediaSources[CurrentIndex];
	MediaSources.RemoveAt(CurrentIndex, EAllowShrinking::No);
	MediaSources.Insert(MediaSource, DestIndex);

#if WITH_EDITOR
	if (MediaSourceLabels.IsValidIndex(CurrentIndex))
	{
		FString Label = MediaSourceLabels[CurrentIndex];
		MediaSourceLabels.RemoveAt(CurrentIndex, EAllowShrinking::No);
		MediaSourceLabels.Insert(Label, DestIndex);
	}
#endif
	
	return true;
}

#if WITH_EDITOR
FString UMediaProfile::GetLabelForMediaSource(int32 Index)
{
	if (!MediaSourceLabels.IsValidIndex(Index))
	{
		if (MediaSources.IsValidIndex(Index))
		{
			if (MediaSources[Index])
			{
				return MediaSources[Index]->GetName();
			}
			else
			{
				return TEXT("");
			}
		}
		
		return TEXT("");
	}
	
	return MediaSourceLabels[Index];
}

void UMediaProfile::SetLabelForMediaSource(int32 Index, const FString& NewLabel)
{
	if (!MediaSourceLabels.IsValidIndex(Index))
	{
		if (!MediaSources.IsValidIndex(Index))
		{
			return;
		}
		
		// In some cases, we may not have labels for all media sources yet (such as an older media profile being edited)
		// In that case, add all the missing labels now
		for (int32 MissingIdx = MediaSourceLabels.Num(); MissingIdx < MediaSources.Num(); ++MissingIdx)
		{
			if (MediaSources[MissingIdx])
			{
				MediaSourceLabels.Add(MediaSources[MissingIdx]->GetName());
			}
			else
			{
				MediaSourceLabels.Add(TEXT(""));
			}
		}
	}
	
	MediaSourceLabels[Index] = NewLabel;
	
	FPropertyChangedEvent PropertyChangedEvent(StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMediaProfile, MediaSourceLabels)), EPropertyChangeType::ValueSet);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(this, PropertyChangedEvent);
}
#endif //WITH_EDITOR

UMediaOutput* UMediaProfile::GetMediaOutput(int32 Index) const
{
	if (MediaOutputs.IsValidIndex(Index))
	{
		return MediaOutputs[Index];
	}
	return nullptr;
}

void UMediaProfile::SetMediaOutput(int32 Index, UMediaOutput* InMediaOutput)
{
	if (MediaOutputs.IsValidIndex(Index))
	{
		FString CurrentMediaOutputName = MediaOutputs[Index] ? MediaOutputs[Index]->GetName() : TEXT("");
		MediaOutputs[Index] = InMediaOutput;
		
#if WITH_EDITOR
		FPropertyChangedEvent PropertyChangedEvent(StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMediaProfile, MediaOutputs)), EPropertyChangeType::ValueSet);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(this, PropertyChangedEvent);
		
		// If the original label for the media output was the output's object name (indicating the user has not changed it)
		// update the label to the new output's object name
		if (!MediaOutputLabels.IsValidIndex(Index) ||GetLabelForMediaOutput(Index) == CurrentMediaOutputName)
		{
			SetLabelForMediaOutput(Index, InMediaOutput->GetName());
		}
#endif
	}
}

int32 UMediaProfile::NumMediaOutputs() const
{
	return MediaOutputs.Num();
}

int32 UMediaProfile::FindMediaOutputIndex(UMediaOutput* MediaOutput) const
{
	return MediaOutputs.Find(MediaOutput);
}

void UMediaProfile::AddMediaOutput(UMediaOutput* MediaOutput)
{
	MediaOutputs.Add(MediaOutput);

#if WITH_EDITOR
	if (MediaOutputLabels.Num() < MediaOutputs.Num())
	{
		for (int32 Index = MediaOutputLabels.Num(); Index < MediaOutputs.Num(); ++Index)
		{
			FString Name = MediaOutputs[Index] ? MediaOutputs[Index]->GetName() : TEXT("");
			MediaOutputLabels.Add(Name);
		}
	}
	else
	{
		FString Name = MediaOutput ? MediaOutput->GetName() : TEXT("");
		const int32 Index = MediaOutputs.Num() - 1;
		if (MediaOutputLabels.IsValidIndex(Index))
		{
			MediaOutputLabels[Index] = Name;
		}
		else
		{
			MediaOutputLabels.Add(Name);
		}
	}
#endif
}

bool UMediaProfile::RemoveMediaOutput(int32 Index)
{
	if (!MediaOutputs.IsValidIndex(Index))
	{
		return false;
	}

	PlaybackManager->ChangeMediaOutputIndex(Index, INDEX_NONE);
	MediaOutputs.RemoveAt(Index, EAllowShrinking::No);

#if WITH_EDITOR
	if (MediaOutputLabels.IsValidIndex(Index))
	{
		MediaOutputLabels.RemoveAt(Index, EAllowShrinking::No);
	}
#endif
	
	return true;
}

bool UMediaProfile::MoveMediaOutput(int32 CurrentIndex, int32 DestIndex)
{
	if (!MediaOutputs.IsValidIndex(CurrentIndex) || !MediaOutputs.IsValidIndex(DestIndex))
	{
		return false;
	}

	PlaybackManager->ChangeMediaOutputIndex(CurrentIndex, DestIndex);
	TObjectPtr<UMediaOutput> MediaOutput = MediaOutputs[CurrentIndex];
	MediaOutputs.RemoveAt(CurrentIndex, EAllowShrinking::No);
	MediaOutputs.Insert(MediaOutput, DestIndex);

#if WITH_EDITOR
	if (MediaOutputLabels.IsValidIndex(CurrentIndex))
	{
		FString OutputLabel = MediaOutputLabels[CurrentIndex];
		MediaOutputLabels.RemoveAt(CurrentIndex, EAllowShrinking::No);
		MediaOutputLabels.Insert(OutputLabel, DestIndex);
	}
#endif
	
	return true;
}

#if WITH_EDITOR
FString UMediaProfile::GetLabelForMediaOutput(int32 Index)
{
	if (!MediaOutputLabels.IsValidIndex(Index))
	{
		if (MediaOutputs.IsValidIndex(Index))
		{
			if (MediaOutputs[Index])
			{
				return MediaOutputs[Index]->GetName();
			}
			else
			{
				return TEXT("");
			}
		}
		
		return TEXT("");
	}
	
	return MediaOutputLabels[Index];
}

void UMediaProfile::SetLabelForMediaOutput(int32 Index, const FString& NewLabel)
{
	if (!MediaOutputLabels.IsValidIndex(Index))
	{
		if (!MediaOutputs.IsValidIndex(Index))
		{
			return;
		}
		
		// In some cases, we may not have labels for all media outputs yet (such as an older media profile being edited)
		// In that case, add all the missing labels now
		for (int32 MissingIdx = MediaOutputLabels.Num(); MissingIdx < MediaOutputs.Num(); ++MissingIdx)
		{
			if (MediaOutputs[MissingIdx])
			{
				MediaOutputLabels.Add(MediaOutputs[MissingIdx]->GetName());
			}
			else
			{
				MediaOutputLabels.Add(TEXT(""));
			}
		}
	}
	
	MediaOutputLabels[Index] = NewLabel;

	FPropertyChangedEvent PropertyChangedEvent(StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMediaProfile, MediaOutputLabels)), EPropertyChangeType::ValueSet);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(this, PropertyChangedEvent);
}
#endif //WITH_EDITOR

UTimecodeProvider* UMediaProfile::GetTimecodeProvider() const
{
	return bOverrideTimecodeProvider ? TimecodeProvider : nullptr;
}


UEngineCustomTimeStep* UMediaProfile::GetCustomTimeStep() const
{
	return bOverrideCustomTimeStep ? CustomTimeStep : nullptr;
}

void UMediaProfile::ApplyTimecodeProvider()
{
	ResetTimecodeProvider();
	if (bOverrideTimecodeProvider)
	{
		bTimecodeProvideWasApplied = true;
		AppliedTimecodeProvider = TimecodeProvider;
		PreviousTimecodeProvider = GEngine->GetTimecodeProvider();

		if (PreviousTimecodeProvider && AppliedTimecodeProvider && PreviousTimecodeProvider->GetClass() == AppliedTimecodeProvider->GetClass())
		{
			GEngine->SetTimecodeProvider(nullptr);
			// Sleep to let the timecode provider complete its shutdown on its hardware.
			FPlatformProcess::Sleep(0.2);
		}

		bool bResult = GEngine->SetTimecodeProvider(TimecodeProvider);
		if (!bResult && TimecodeProvider)
		{
			UE_LOG(LogMediaFrameworkUtilities, Warning, TEXT("The Timecode Provider '%s' could not be initialized."), *TimecodeProvider->GetName());
		}
	}
}

void UMediaProfile::ApplyCustomTimeStep()
{
	ResetCustomTimeStep();
	if (bOverrideCustomTimeStep)
	{
		bCustomTimeStepWasApplied = true;
		AppliedCustomTimeStep = CustomTimeStep;
		PreviousCustomTimeStep = GEngine->GetCustomTimeStep();
		bool bResult = GEngine->SetCustomTimeStep(CustomTimeStep);
		if (!bResult && CustomTimeStep)
		{
			UE_LOG(LogMediaFrameworkUtilities, Warning, TEXT("The Custom Time Step '%s' could not be initialized."), *CustomTimeStep->GetName());
		}
	}
}

void UMediaProfile::Apply()
{
#if WITH_EDITORONLY_DATA
	bNeedToBeReapplied = false;
#endif

	if (GEngine == nullptr)
	{
		UE_LOG(LogMediaFrameworkUtilities, Error, TEXT("The MediaProfile '%s' could not be applied. The Engine is not initialized."), *GetName());
		return;
	}

	// Make sure we have the same amount of sources and outputs as the number of proxies.
	FixNumSourcesAndOutputs();

	{
		TArray<UProxyMediaSource*> SourceProxies = IMediaProfileManager::Get().GetAllMediaSourceProxy();
		check(SourceProxies.Num() == MediaSources.Num());
		for (int32 Index = 0; Index < MediaSources.Num(); ++Index)
		{
			UProxyMediaSource* Proxy = SourceProxies[Index];
			if (Proxy)
			{
				Proxy->SetDynamicMediaSource(MediaSources[Index]);
			}
		}
	}

	{
		TArray<UProxyMediaOutput*> OutputProxies = IMediaProfileManager::Get().GetAllMediaOutputProxy();
		check(OutputProxies.Num() == MediaOutputs.Num());
		for (int32 Index = 0; Index < MediaOutputs.Num(); ++Index)
		{
			UProxyMediaOutput* Proxy = OutputProxies[Index];
			if (Proxy)
			{
				Proxy->SetDynamicMediaOutput(MediaOutputs[Index]);
			}
		}
	}

	ApplyTimecodeProvider();
	ApplyCustomTimeStep();

	SendAnalytics();
}


void UMediaProfile::Reset()
{
	if (GEngine == nullptr)
	{
		UE_LOG(LogMediaFrameworkUtilities, Error, TEXT("The MediaProfile '%s' could not be reset. The Engine is not initialized."), *GetName());
		return;
	}

	{
		// Reset the source proxies
		TArray<UProxyMediaSource*> SourceProxies = IMediaProfileManager::Get().GetAllMediaSourceProxy();
		for (UProxyMediaSource* Proxy : SourceProxies)
		{
			if (Proxy)
			{
				Proxy->SetDynamicMediaSource(nullptr);
			}
		}
	}

	{
		// Reset the output proxies
		TArray<UProxyMediaOutput*> OutputProxies = IMediaProfileManager::Get().GetAllMediaOutputProxy();
		for (UProxyMediaOutput* Proxy : OutputProxies)
		{
			if (Proxy)
			{
				Proxy->SetDynamicMediaOutput(nullptr);
			}
		}
	}

	// Reset the timecode provider
	ResetTimecodeProvider();

	// Reset the engine custom time step
	ResetCustomTimeStep();
}

void UMediaProfile::ResetTimecodeProvider()
{
	if (bTimecodeProvideWasApplied)
	{
		if (AppliedTimecodeProvider == GEngine->GetTimecodeProvider())
		{
			bool bResult = GEngine->SetTimecodeProvider(PreviousTimecodeProvider);
			if (!bResult && PreviousTimecodeProvider)
			{
				UE_LOG(LogMediaFrameworkUtilities, Warning, TEXT("The TimecodeProvider '%s' could not be initialized."), *PreviousTimecodeProvider->GetName());
			}
		}
		else
		{
			if (PreviousTimecodeProvider)
			{
				UE_LOG(LogMediaFrameworkUtilities, Warning, TEXT("Could not set the previous TimecodeProvider '%s'."), *PreviousTimecodeProvider->GetName());
			}
			else
			{
				UE_LOG(LogMediaFrameworkUtilities, Warning, TEXT("Could not set the previous TimecodeProvider."));
			}
		}
		PreviousTimecodeProvider = nullptr;
		AppliedTimecodeProvider = nullptr;
		bTimecodeProvideWasApplied = false;
	}
}

void UMediaProfile::ResetCustomTimeStep()
{
	if (bCustomTimeStepWasApplied)
	{
		if (AppliedCustomTimeStep == GEngine->GetCustomTimeStep())
		{
			bool bResult = GEngine->SetCustomTimeStep(PreviousCustomTimeStep);
			if (!bResult && PreviousCustomTimeStep)
			{
				UE_LOG(LogMediaFrameworkUtilities, Warning, TEXT("The Custom Time Step '%s' could not be initialized."), *PreviousCustomTimeStep->GetName());
			}
		}
		else
		{
			if (PreviousCustomTimeStep)
			{
				UE_LOG(LogMediaFrameworkUtilities, Warning, TEXT("Could not set the previous Custom Time Step '%s'."), *PreviousCustomTimeStep->GetName());
			}
			else
			{
				UE_LOG(LogMediaFrameworkUtilities, Warning, TEXT("Could not set the previous Custom Time Step."));
			}
		}
		PreviousCustomTimeStep = nullptr;
		AppliedCustomTimeStep = nullptr;
		bCustomTimeStepWasApplied = false;
	}
}

/**
 * @EventName MediaFramework.ApplyMediaProfile
 * @Trigger Triggered when a media profile is applied.
 * @Type Client
 * @Owner MediaIO Team
 */
void UMediaProfile::SendAnalytics() const
{
#if WITH_EDITOR
	if (FEngineAnalytics::IsAvailable())
	{
		const FString TimecodeProviderName = GetTimecodeProvider() ? GetTimecodeProvider()->GetName() : TEXT("None");
		const FString CustomTimestepName = GetCustomTimeStep() ? GetCustomTimeStep()->GetName() : TEXT("None");
		
		TArray<FAnalyticsEventAttribute> EventAttributes;
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("Inputs"), MediaProfileAnalytics::JoinObjectNames<UMediaSource>(MediaSources)));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("Outputs"), MediaProfileAnalytics::JoinObjectNames<UMediaOutput>(MediaOutputs)));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("TimecodeProvider"), TimecodeProviderName));
		EventAttributes.Add(FAnalyticsEventAttribute(TEXT("CustomTimeStep"), CustomTimestepName));
		
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("MediaFramework.ApplyMediaProfile"), EventAttributes);
	}
#endif
}

void UMediaProfile::FixNumSourcesAndOutputs()
{
	const int32 NumSourceProxies = IMediaProfileManager::Get().GetAllMediaSourceProxy().Num();
	const int32 NumOutputProxies = IMediaProfileManager::Get().GetAllMediaOutputProxy().Num();

	const bool bResizeMediaSources = MediaSources.Num() != NumSourceProxies;
	const bool bResizeMediaOutputs = MediaOutputs.Num() != NumOutputProxies;

	if (bResizeMediaSources || bResizeMediaOutputs)
	{
		Modify();
	}
	
	if (bResizeMediaSources)
	{
		MediaSources.SetNumZeroed(NumSourceProxies);

#if WITH_EDITOR
		FPropertyChangedEvent PropertyChangedEvent(StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMediaProfile, MediaSources)), EPropertyChangeType::ArrayAdd);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(this, PropertyChangedEvent);
#endif // WITH_EDITOR
	}

	if (bResizeMediaOutputs)
	{
		MediaOutputs.SetNumZeroed(NumOutputProxies);

#if WITH_EDITOR
		FPropertyChangedEvent PropertyChangedEvent(StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UMediaProfile, MediaOutputs)), EPropertyChangeType::ArrayAdd);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(this, PropertyChangedEvent);
#endif // WITH_EDITOR
	}
}
