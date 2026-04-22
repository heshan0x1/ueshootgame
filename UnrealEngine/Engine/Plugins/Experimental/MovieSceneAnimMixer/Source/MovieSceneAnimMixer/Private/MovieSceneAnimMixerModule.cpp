// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieSceneAnimMixerModule.h"
#include "MovieSceneAnimMixerSettings.h"
#include "ISettingsModule.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogMovieSceneAnimMixer);

#define LOCTEXT_NAMESPACE "FAnimMixerModule"

namespace UE::MovieScene
{
	FName GSequencerDefaultAnimNextInjectionSite = NAME_None;

	FAutoConsoleVariableRef CVarDefaultAnimNextInjectionSite(
	TEXT("Sequencer.AnimNext.DefaultInjectionSite"),
	GSequencerDefaultAnimNextInjectionSite,
	TEXT("(Default: None) Specifies the default injection site name for Sequencer Anim Next Targets that is used when none is specified on the target itself."),
	ECVF_Default
	);

void FMovieSceneAnimMixerModule::StartupModule()
{
	ISettingsModule& SettingsModule = FModuleManager::Get().LoadModuleChecked<ISettingsModule>("Settings");
	
	SettingsModule.RegisterSettings("Plugins", "Animation", "AnimMixer",
		LOCTEXT("RuntimeSettingsName", "Anim Mixer"),
		LOCTEXT("RuntimeSettingsDescription", "Configure project settings relating to the Anim Mixer Plugin"),
		GetMutableDefault<UMovieSceneAnimMixerSettings>()
	);

	CVarDefaultAnimNextInjectionSite->Set(*GetMutableDefault<UMovieSceneAnimMixerSettings>()->DefaultInjectionSite.ToString(), ECVF_SetByProjectSetting);
	
}

void FMovieSceneAnimMixerModule::ShutdownModule()
{

}

}

IMPLEMENT_MODULE(UE::MovieScene::FMovieSceneAnimMixerModule, MovieSceneAnimMixer)

#undef LOCTEXT_NAMESPACE