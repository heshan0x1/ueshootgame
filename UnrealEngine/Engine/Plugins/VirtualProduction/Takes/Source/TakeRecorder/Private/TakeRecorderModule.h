// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/IDelegateInstance.h"
#include "ITakeRecorderModule.h"
#include "Templates/SharedPointer.h"
#include "Timecode/HitchlessProtectionRootLogic.h"
#include "Timecode/Visualization/Sequencer/SequencerHitchVisualizer.h"
#include "UObject/GCObject.h"
#include "Timecode/HitchlessProtectionRootLogic.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FExtender;
class FTakePresetActions;
class UTakePreset;
class FSerializedRecorder;
class UTakeRecorderSources;
class USequencerSettings;

class FTakeRecorderModule : public ITakeRecorderModule, public FGCObject
{
public:
	FTakeRecorderModule();

	void PopulateSourcesMenu(TSharedRef<FExtender> InExtender, UTakeRecorderSources* InSources) const;

	//~ Begin IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface Interface
	
	//~ Begin ITakeRecorderModule Interface
	virtual UTakePreset* GetPendingTake() const override;
	virtual FOnGenerateWidgetExtensions& GetToolbarExtensionGenerators() override { return ToolbarExtensionGenerators; }
	virtual FOnGenerateWidgetExtensions& GetRecordButtonExtensionGenerators() override { return ButtonExtensionGenerators; }
	virtual FOnExternalObjectAddRemoveEvent& GetExternalObjectAddRemoveEventDelegate() override { return ExternalObjectAddRemoveEvent; }
	virtual FOnRecordErrorCheck&  GetRecordErrorCheckGenerator() override { return RecordErrorCheck; }
	virtual TArray<TWeakObjectPtr<>>& GetExternalObjects() override { return ExternalObjects; }
	virtual FLastRecordedLevelSequenceProvider& GetLastLevelSequenceProvider() override { return LastLevelSequenceProvider; }
	virtual FCanReviewLastRecordedLevelSequence& GetCanReviewLastRecordedLevelSequenceDelegate() override { return CanReviewLastRecordedSequence; };
	virtual void RegisterExternalObject(UObject* InExternalObject) override;
	virtual void UnregisterExternalObject(UObject* InExternalObject) override;
	virtual FOnForceSaveAsPreset& OnForceSaveAsPreset() override { return ForceSaveAsPresetEvent; }
	virtual FDelegateHandle RegisterSourcesMenuExtension(const FOnExtendSourcesMenu& InExtension) override;
	virtual void RegisterSourcesExtension(const FSourceExtensionData& InData) override;
	virtual void UnregisterSourcesExtension() override;
	virtual void UnregisterSourcesMenuExtension(FDelegateHandle Handle) override;
	virtual void RegisterSettingsObject(UObject* InSettingsObject) override;
	//~ Begin ITakeRecorderModule Interface
	
	FSourceExtensionData& GetSourcesExtensionData() { return SourceExtensionData; }
	
private:
	
	//~ Begin FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return "FTakeRecorderModule"; }
	//~ End FGCObject Interface

	void RegisterDetailCustomizations();
	void UnregisterDetailCustomizations();

	void RegisterLevelEditorExtensions();
	void UnregisterLevelEditorExtensions() const;

	void RegisterSettings();
	void UnregisterSettings();

	void RegisterSerializedRecorder();
	void UnregisterSerializedRecorder() const;

	void RegisterMenus();

	void OnEditorClose();

private:

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnExtendSourcesMenuEvent, TSharedRef<FExtender>, UTakeRecorderSources*);

	FOnExtendSourcesMenuEvent SourcesMenuExtenderEvent;
	FOnGenerateWidgetExtensions ToolbarExtensionGenerators;
	FOnGenerateWidgetExtensions ButtonExtensionGenerators;
	FLastRecordedLevelSequenceProvider LastLevelSequenceProvider;
	FCanReviewLastRecordedLevelSequence CanReviewLastRecordedSequence;

	FOnExternalObjectAddRemoveEvent ExternalObjectAddRemoveEvent;
	FOnForceSaveAsPreset ForceSaveAsPresetEvent;
	FOnRecordErrorCheck RecordErrorCheck;

	FDelegateHandle LevelEditorLayoutExtensionHandle;
	FDelegateHandle LevelEditorTabManagerChangedHandle;
	FDelegateHandle ModulesChangedHandle;

	TSharedPtr<FSerializedRecorder> SerializedRecorder;

	/**
	 * Sets up all sub-systems related to managing timecode in Take Recorder
	 * 
	 * For future maintainers: this could totally live in a separate module / plugin / whatever.
	 * There's no technical reason to have this live in TakeRecorder. The reason it's here is that we probably always want hitching to be handled
	 * gracefully.
	 */
	TUniquePtr<UE::TakeRecorder::FHitchlessProtectionRootLogic> TimecodeManagement;
#if WITH_EDITOR
	TUniquePtr<UE::TakeRecorder::FSequencerHitchVisualizer> SequencerHitchVisualizer;
#endif

	/** Extra class default objects that should appear in the Take Recorder panel. */
	TArray<TWeakObjectPtr<>> ExternalObjects;

	/** The settings object registers with the Settings module. */
	TObjectPtr<USequencerSettings> SequencerSettings;

	/**
	 * Registered extensions for the source module. We only use a single struct rather than container
	 * as only the source module should be utilizing this.
	 */
	FSourceExtensionData SourceExtensionData;
};