// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaveformEditorModule.h"

#include "DSP/FloatArrayMath.h"
#include "Editor.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Sound/SoundWave.h"
#include "TransformedWaveformViewFactory.h"
#include "WaveformAudioAnalysisFunctions.h"
#include "WaveformEditorCommands.h"
#include "WaveformEditorInstantiator.h"
#include "WaveformEditorLog.h"

#include "UObject/PackageReload.h"

DEFINE_LOG_CATEGORY(LogWaveformEditor);

namespace
{
	void AnalyzeSoundWaveLoudness(UObject* Object)
	{
		if (Object && Object->IsA<USoundWave>())
		{
			TObjectPtr<USoundWave> SoundWave = CastChecked<USoundWave>(Object);

			TArray<uint8> ImportedRawPCMData;
			uint32 BufferSampleRate = 0;
			uint16 ImportedNumChannels = 0;

			Audio::FAlignedFloatBuffer AudioBuffer;

			if (!SoundWave->GetImportedSoundWaveData(ImportedRawPCMData, BufferSampleRate, ImportedNumChannels) || BufferSampleRate == 0)
			{
				UE_LOG(LogWaveformEditor, Warning, TEXT("Soundwave loudness analyzation failed in Waveform Editor Module."));

				return;
			}

			const int64 NumWaveformSamples = ImportedRawPCMData.Num() * sizeof(uint8) / sizeof(int16);
			AudioBuffer.SetNumUninitialized(NumWaveformSamples);
			Audio::ArrayPcm16ToFloat(MakeArrayView((int16*)ImportedRawPCMData.GetData(), NumWaveformSamples), AudioBuffer);

			const float SamplePeakDB = WaveformAudioAnalysis::GetPeakSampleValue(AudioBuffer);
			const float LUFS = WaveformAudioAnalysis::GetLUFS(AudioBuffer, BufferSampleRate, ImportedNumChannels);

			SoundWave->SetLoudnessValues(LUFS, SamplePeakDB);
			SoundWave->PostEditChange();
		}
	}
}

void FWaveformEditorModule::StartupModule()
{
	FWaveformEditorCommands::Register();
	FTransformedWaveformViewFactory::Create();

	WaveformEditorInstantiator = MakeShared<FWaveformEditorInstantiator>();
	RegisterContentBrowserExtensions(WaveformEditorInstantiator.Get());
	WaveformEditorInstantiator->RegisterAsSoundwaveEditor();

	OnPostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddLambda([this]()
		{
			if (GEditor)
			{
				if (TObjectPtr<UImportSubsystem> ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
				{
					OnAssetPostImportHandle = ImportSubsystem->OnAssetPostImport.AddLambda([&](UFactory* Factory, UObject* Object)
						{
							AnalyzeSoundWaveLoudness(Object);
						});
				}
			}

			OnPackageReloadedHandle = FCoreUObjectDelegates::OnPackageReloaded.AddLambda([](EPackageReloadPhase Phase, FPackageReloadedEvent* ReloadedEvent)
				{
					if (Phase == EPackageReloadPhase::PostPackageFixup)
					{
						check(ReloadedEvent);

						const UPackage* ReloadedPackage = ReloadedEvent->GetNewPackage();

						if (ReloadedPackage == nullptr)
						{
							return;
						}

						TArray<UObject*> ObjectsInPackage;
						constexpr bool bIncludeNestedObjects = true;
						GetObjectsWithOuter(ReloadedPackage, ObjectsInPackage, bIncludeNestedObjects);

						for (UObject* Object : ObjectsInPackage)
						{
							AnalyzeSoundWaveLoudness(Object);
						}
					}
				});
		});
}

void FWaveformEditorModule::ShutdownModule()
{
	FWaveformEditorCommands::Unregister();

	FCoreDelegates::OnPostEngineInit.Remove(OnPostEngineInitHandle);

	if (GEditor)
	{
		if (TObjectPtr<UImportSubsystem> ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
		{
			ImportSubsystem->OnAssetPostImport.Remove(OnAssetPostImportHandle);
		}
	}

	FCoreUObjectDelegates::OnPackageReloaded.Remove(OnPackageReloadedHandle);
}

void FWaveformEditorModule::RegisterContentBrowserExtensions(IWaveformEditorInstantiator* Instantiator)
{
	WaveformEditorInstantiator->ExtendContentBrowserSelectionMenu();
}

IMPLEMENT_MODULE(FWaveformEditorModule, WaveformEditor);
