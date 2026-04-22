// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"

#include "NNERuntimeIREESettings.generated.h"

/** Specifies the thread affinity group type */
UENUM()
enum class ENNERuntimeIREEThreadingAffinityGroupSpecifierType : uint8
{
	Index 		UMETA(DisplayName = "Specify group by index"),
	Current		UMETA(DisplayName = "Use same group as calling thread"),
	All			UMETA(DisplayName = "Use all groups"),
	Any			UMETA(DisplayName = "Use any group")
};

/** Specifies the processor affinity for a particular thread given by processor group the thread should be assigned to (aka NUMA node, cluster etc., depending on the platform) and the processor it should be scheduled on */
USTRUCT()
struct FNNERuntimeIREEThreadingAffinity
{
	GENERATED_BODY()

	/** How is the group specified? */
	UPROPERTY(Config, EditAnywhere, Category = "IREE Runtime", meta = (
		DisplayName = "Group specifier",
		ToolTip = "Specify the group (type) the thread should be assigned to."))
	ENNERuntimeIREEThreadingAffinityGroupSpecifierType GroupSpecifierType = ENNERuntimeIREEThreadingAffinityGroupSpecifierType::Any;

	/** Group index, only used if node specifier type is 'Index' */
	UPROPERTY(Config, EditAnywhere, Category = "IREE Runtime", meta = (
		DisplayName = "Group index",
		ToolTip = "Specify with an index the group the thread should be assigned to. Only used if group specifier type is 'Index'."))
	int32 GroupIndex = -1;

	/** Logical core index */
	UPROPERTY(Config, EditAnywhere, Category = "IREE Runtime", meta = (
		DisplayName = "Logical core index",
		ToolTip = "Specify with an index the logical core the thread should be scheduled on. Set to -1 to let the thread be scheduled on any core."))
	int32 CoreIndex = -1;
};

/** Specifies the task topology used for multi-threading */
USTRUCT()
struct FNNERuntimeIREETaskTopology
{
	GENERATED_BODY()

	/** Task topology groups */
	UPROPERTY(Config, EditAnywhere, Category = "IREE Runtime", meta = (
		DisplayName = "Task topology groups",
		ToolTip = "Specify the task system topology with zero or more groups. If empty, the runtime will try to use all physical cores."))
	TArray<FNNERuntimeIREEThreadingAffinity> TaskTopologyGroups;
};

/** Threading options to configure single/multi-threading, including task topology for multi-threading case */
USTRUCT()
struct FNNERuntimeIREEThreadingOptions
{
	GENERATED_BODY()

	/** Run single threaded? */
	UPROPERTY(Config, EditAnywhere, Category = "IREE Runtime", meta = (
		DisplayName = "Is single threaded",
		ToolTip = "Should the runtime run single threaded (might result in better performance with very small models)."))
	bool bIsSingleThreaded = true;

	/** Task topology used for multi-threading */
	UPROPERTY(Config, EditAnywhere, Category = "IREE Runtime", meta = (
		DisplayName = "Task topology",
		ToolTip = "For multi-threading you can specify the task system topology."))
	FNNERuntimeIREETaskTopology TaskTopology;
};

/** Settings used to configure NNERuntimeIREE */
UCLASS(config = Engine, defaultconfig, meta = (DisplayName = "NNERuntimeIREE"))
class UNNERuntimeIREESettings : public UDeveloperSettings
{
	GENERATED_BODY()

	UNNERuntimeIREESettings(const FObjectInitializer& ObjectInitlaizer);

public:

	/** Threading options in Editor targets */
	UPROPERTY(Config, EditAnywhere, Category = "IREE Runtime", meta = (DisplayName = "Editor threading options", ToolTip = "Threading options in Editor targets", ConfigRestartRequired = true))
	FNNERuntimeIREEThreadingOptions EditThreadingOptions{};

	/** Threading options in Non-Editor (Game, Program, ...) targets */
	UPROPERTY(Config, EditAnywhere, Category = "IREE Runtime", meta = (DisplayName = "Game threading options", ToolTip = "Threading options in Non-Editor (Game, Program, ...) targets", ConfigRestartRequired = true))
	FNNERuntimeIREEThreadingOptions GameThreadingOptions{};

	// Begin UDeveloperSettings Interface
	NNERUNTIMEIREE_API virtual FName GetCategoryName() const override;
#if WITH_EDITOR
	NNERUNTIMEIREE_API virtual FText GetSectionText() const override;
	// END UDeveloperSettings Interface
#endif

};
