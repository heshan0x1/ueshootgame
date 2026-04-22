// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextRigVMAssetEntry.h"
#include "Param/ParamType.h"
#include "AnimNextSharedVariablesEntry.generated.h"

class UAnimNextSharedVariables;
class UAnimNextRigVMAssetEditorData;
class UAssetDefinition_AnimNextSharedVariablesEntry;

namespace UE::UAF::Editor
{
	class FVariablesOutlinerHierarchy;
	struct FVariablesOutlinerStructSharedVariablesItem;
	class SVariablesOutlinerStructSharedVariablesLabel;
	class SVariablesOutlinerValue;
	class FVariableProxyCustomization;
	class SAddVariablesDialog;
	class SVariableOverride;
	class FVariablesOutlinerMode;
	class SVariablesOutliner;
	struct FVariablesOutlinerAssetItem;
}

namespace UE::UAF::UncookedOnly
{
	struct FUtils;
}


UENUM()
enum class EAnimNextSharedVariablesType : uint8
{
	Asset,
	Struct
};

UCLASS(MinimalAPI, Category = "Shared Variables", DisplayName = "Shared Variables")
class UAnimNextSharedVariablesEntry : public UAnimNextRigVMAssetEntry
{
	GENERATED_BODY()

	friend class UAnimNextRigVMAssetEditorData;
	friend class UAssetDefinition_AnimNextSharedVariablesEntry;
	friend class UE::UAF::Editor::FVariablesOutlinerHierarchy;
	friend struct UE::UAF::Editor::FVariablesOutlinerStructSharedVariablesItem;
	friend struct UE::UAF::UncookedOnly::FUtils;
	friend class UE::UAF::Editor::SVariablesOutlinerStructSharedVariablesLabel;
	friend class UE::UAF::Editor::SVariablesOutlinerValue;
	friend class UE::UAF::Editor::FVariableProxyCustomization;
	friend class UE::UAF::Editor::SAddVariablesDialog;
	friend class UE::UAF::Editor::SVariableOverride;
	friend class UE::UAF::Editor::SVariablesOutliner;
	friend struct UE::UAF::Editor::FVariablesOutlinerAssetItem;
	friend UE::UAF::Editor::FVariablesOutlinerMode;
	friend class UAnimNextAssetFindReplaceVariables;

	// UAnimNextRigVMAssetEntry interface
	virtual void Initialize(UAnimNextRigVMAssetEditorData* InEditorData) override;
	virtual FName GetEntryName() const override;
	virtual void SetEntryName(FName InName, bool bSetupUndoRedo = true) override {}
	virtual FText GetDisplayName() const override;
	virtual FText GetDisplayNameTooltip() const override;

	// Get the type of this shared variables entry
	EAnimNextSharedVariablesType GetType() const
	{
		return Type;
	}

	// Set this entry to use an asset (rather than a struct)
	UAFUNCOOKEDONLY_API void SetAsset(const UAnimNextSharedVariables* InAsset, bool bSetupUndoRedo = true);

	// Get the asset that this entry represents. If this entry uses a struct, this call will return nullptr
	UAFUNCOOKEDONLY_API const UAnimNextSharedVariables* GetAsset() const;

	// Get the path to the asset/struct path that this entry represents, if any
	UAFUNCOOKEDONLY_API FSoftObjectPath GetObjectPath() const;

	// Set this entry to use a struct (rather than an asset)
	UAFUNCOOKEDONLY_API void SetStruct(const UScriptStruct* InStruct, bool bSetupUndoRedo = true);

	// Get the struct that this entry represents. If this entry uses an asset, this call will return nullptr
	UAFUNCOOKEDONLY_API const UScriptStruct* GetStruct() const;

	// Recompiles this asset when the linked asset is modified
	void HandleAssetModified(UAnimNextRigVMAssetEditorData* InEditorData, EAnimNextEditorDataNotifType InType, UObject* InSubject);

#if WITH_LIVE_CODING
	// Recompiles this asset when live coding is run (potentially modifying the struct we reference)
	void HandlePatchComplete();
#endif

	/** The asset whose variables we share */
	UPROPERTY(VisibleAnywhere, Category = "Asset")
	TObjectPtr<const UAnimNextSharedVariables> Asset;

	/** Soft reference to the asset/struct for error reporting */
	UPROPERTY()
	FSoftObjectPath ObjectPath;

	/** The struct whose variables we use */
	UPROPERTY(VisibleAnywhere, Category = "Asset")
	TObjectPtr<const UScriptStruct> Struct;

	UPROPERTY()
	EAnimNextSharedVariablesType Type;
};
