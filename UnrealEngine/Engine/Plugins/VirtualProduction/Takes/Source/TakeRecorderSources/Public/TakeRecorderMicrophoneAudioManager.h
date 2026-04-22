// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TakeRecorderSourceProperty.h"

#include "TakeRecorderMicrophoneAudioManager.generated.h"

#define UE_API TAKERECORDERSOURCES_API

class IAudioCaptureEditor;
class USoundWave;
struct FTakeRecorderAudioSourceSettings;


DECLARE_MULTICAST_DELEGATE_OneParam(FOnNotifySourcesOfDeviceChange, int);

/** This class exposes the audio input device list via the project settings details. It does this in 
*   conjunction with FAudioInputDevicePropertyCustomization. It also manages the IAudioCaptureEditor 
*   object which handles the low level audio device recording.
*/
UCLASS(MinimalAPI, config=EditorPerProjectUserSettings, PerObjectConfig, DisplayName = "Audio Input Device")
class UTakeRecorderMicrophoneAudioManager : public UTakeRecorderAudioInputSettings
{
public:
	GENERATED_BODY()

	UE_API UTakeRecorderMicrophoneAudioManager(const FObjectInitializer& ObjInit);

	// Begin UObject Interface
	UE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	// End UObject Interface

	/** Enumerates the audio devices present on the current machine */
	UE_API virtual void EnumerateAudioDevices(bool InForceRefresh = false) override;

	/** Returns input channel count for currently selected audio device */
	UE_API virtual int32 GetDeviceChannelCount() override;

	/** The audio device to use for this microphone source */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Source", meta = (ShowOnlyInnerProperties))
	FAudioInputDeviceProperty AudioInputDevice;

	/** 
	*  Calls StartRecording on the AudioRecorder object. This is called multiple times (once for each
	*  microphone source, however, only the first call triggers the call to AudioRecorder.
	*/
	UE_API void StartRecording(int32 InChannelCount);
	/**
	 * Calls StopRecording on the AudioRecorder object. This is called multiple times (once for each
	 * microphone source, however, only the first call triggers the call to AudioRecorder.
	 */
	UE_API void StopRecording();
	/**
	 *  Calls FinalizeRecording on the AudioRecorder object. This is called multiple times (once for each
	 *  microphone source, however, only the first call triggers the call to AudioRecorder.
	 */
	UE_API void FinalizeRecording();


	/** Fetches the USoundWave for this source after a Take has been recorded */
	UE_API TObjectPtr<class USoundWave> GetRecordedSoundWave(const FTakeRecorderAudioSourceSettings& InSourceSettings);
	/** Accessor for the OnNotifySourcesOfDeviceChange delegate list */
	FOnNotifySourcesOfDeviceChange& GetOnNotifySourcesOfDeviceChange() { return OnNotifySourcesOfDeviceChange; }

private:

	/** Multicast delegate which notifies clients when the currently selected audio device changes. */
	FOnNotifySourcesOfDeviceChange OnNotifySourcesOfDeviceChange;
	/** Calls factory to create the AudioRecorder object */
	UE_API TUniquePtr<IAudioCaptureEditor> CreateAudioRecorderObject();
	/** Builds the list of audio devices which will be used in the device menu */
	UE_API void BuildDeviceInfoArray();

	/** Returns whether audio device with given Id was found during enumeration */
	UE_API bool IsAudioDeviceAvailable(const FString& InDeviceId);

	/** The audio recorder object which manages low level recording of audio data */
	TUniquePtr<IAudioCaptureEditor> AudioRecorder;
};

#undef UE_API
