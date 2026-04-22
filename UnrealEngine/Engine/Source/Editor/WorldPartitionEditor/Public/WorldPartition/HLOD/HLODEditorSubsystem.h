// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "WorldPartition/IWorldPartitionEditorModule.h"
#include "WorldPartition/WorldPartitionActorLoaderInterface.h"
#include "WorldPartition/WorldPartitionHandle.h"
#include "Templates/PimplPtr.h"
#include "UObject/ObjectKey.h"

#include "HLODEditorSubsystem.generated.h"

#define UE_API WORLDPARTITIONEDITOR_API


class AActor;
class AWorldPartitionHLOD;
class UPrimitiveComponent;
class UWorldPartition;
class UWorldPartitionEditorSettings;
struct FWorldPartitionHLODEditorData;

// Visibility level for HLOD settings
// By default, settings are classified in the "AllSettings" category
enum class EHLODSettingsVisibility : uint8
{
	BasicSettings,
	AllSettings
};


/**
 * UWorldPartitionHLODEditorSubsystem
 */
UCLASS(MinimalAPI)
class UWorldPartitionHLODEditorSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UE_API UWorldPartitionHLODEditorSubsystem();
	UE_API virtual ~UWorldPartitionHLODEditorSubsystem();

	//~ Begin USubsystem Interface.
	UE_API virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	UE_API virtual void Deinitialize() override;
	//~ End USubsystem Interface.

	//~ Begin UWorldSubsystem Interface.
	UE_API virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UWorldSubsystem Interface.

	//~ Begin FTickableGameObject Interface
	virtual bool IsTickable() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }
	UE_API virtual void Tick(float DeltaTime) override;
	UE_API virtual TStatId GetStatId() const override;
	//~ End FTickableGameObject Interface

	UE_API virtual bool WriteHLODStats(const IWorldPartitionEditorModule::FWriteHLODStatsParams& Params) const;

	static UE_API void AddHLODSettingsFilter(EHLODSettingsVisibility InSettingsVisibility, TSoftObjectPtr<UStruct> InStruct, FName InPropertyName);
	
private:
	UE_API bool IsHLODInEditorEnabled();
	UE_API void SetHLODInEditorEnabled(bool bInEnable);

	UE_API void OnWorldPartitionInitialized(UWorldPartition* InWorldPartition);
	UE_API void OnWorldPartitionUninitialized(UWorldPartition* InWorldPartition);

	UE_API void OnLoaderAdapterStateChanged(const IWorldPartitionActorLoaderInterface::ILoaderAdapter* LoaderAdapter);

	UE_API void ForceHLODStateUpdate();

	UE_API bool WriteHLODStats(const FString& InFilename) const;
	UE_API bool WriteHLODInputStats(const FString& InFilename) const;
	
	UE_API void OnWorldPartitionEditorSettingsChanged(const FName& PropertyName, const UWorldPartitionEditorSettings& WorldPartitionEditorSettings);
	UE_API void ApplyHLODSettingsFiltering();

	UE_API void OnColorHandlerPropertyChangedEvent(UObject* InObject, FPropertyChangedEvent& InPropertyChangedEvent);

private:
	FVector CachedCameraLocation;
	double CachedHLODMinDrawDistance;
	double CachedHLODMaxDrawDistance;
	bool bCachedShowHLODsOverLoadedRegions;
	bool bForceHLODStateUpdate;

	TMap<TObjectKey<UWorldPartition>, TPimplPtr<FWorldPartitionHLODEditorData>> WorldPartitionsHLODEditorData;

	typedef TMap<TSoftObjectPtr<UStruct>, TSet<FName>> FStructsPropertiesMap;
	static UE_API TMap<EHLODSettingsVisibility, FStructsPropertiesMap> StructsPropertiesVisibility;
};


// Macros to simplify registration of HLOD settings filtering
#define HLOD_ADD_CLASS_SETTING_FILTER_NAME(SettingsLevel, TypeIdentifier, PropertyName) \
	UWorldPartitionHLODEditorSubsystem::AddHLODSettingsFilter(EHLODSettingsVisibility::SettingsLevel, TypeIdentifier::StaticClass(), (PropertyName))

#define HLOD_ADD_STRUCT_SETTING_FILTER_NAME(SettingsLevel, TypeIdentifier, PropertyName) \
	UWorldPartitionHLODEditorSubsystem::AddHLODSettingsFilter(EHLODSettingsVisibility::SettingsLevel, TypeIdentifier::StaticStruct(), (PropertyName))

#define HLOD_ADD_CLASS_SETTING_FILTER(SettingsLevel, TypeIdentifier, PropertyIdentifier) \
	HLOD_ADD_CLASS_SETTING_FILTER_NAME(SettingsLevel, TypeIdentifier, GET_MEMBER_NAME_CHECKED(TypeIdentifier, PropertyIdentifier))

#define HLOD_ADD_STRUCT_SETTING_FILTER(SettingsLevel, TypeIdentifier, PropertyIdentifier) \
	HLOD_ADD_STRUCT_SETTING_FILTER_NAME(SettingsLevel, TypeIdentifier, GET_MEMBER_NAME_CHECKED(TypeIdentifier, PropertyIdentifier))

#undef UE_API
