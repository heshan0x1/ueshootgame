// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanPaletteItemPath.h"

#include "BaseTools/SingleTargetWithSelectionTool.h"
#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h"
#include "StructUtils/PropertyBag.h"

#include "MetaHumanCharacterEditorCostumeTools.generated.h"

class UMetaHumanCharacter;
class UMetaHumanCollection;
class UMetaHumanWardrobeItem;

UENUM()
enum class EMetaHumanCharacterCostumeEditingTool : uint8
{
	Costume
};

/** Enum for displaying grooms highlights variations in the Costume Tool */
UENUM()
enum class EMetaHumanCharacterEditorHighlightsVariation : uint8
{
	Bold,
	Blended,
	Traditional,
	Thin
};

UCLASS()
class UMetaHumanCharacterEditorCostumeToolBuilder : public UInteractiveToolWithToolTargetsBuilder
{
	GENERATED_BODY()

public:

	//~Begin UInteractiveToolWithToolTargetsBuilder interface
	virtual bool CanBuildTool(const FToolBuilderState& InSceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& InSceneState) const override;
	//~End UInteractiveToolWithToolTargetsBuilder interface

	UPROPERTY()
	EMetaHumanCharacterCostumeEditingTool ToolType = EMetaHumanCharacterCostumeEditingTool::Costume;

protected:
	//~Begin UInteractiveToolWithToolTargetsBuilder interface
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
	//~End UInteractiveToolWithToolTargetsBuilder interface
};

UCLASS(Transient)
class UMetaHumanCharacterEditorCostumeItem : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FName SlotName;

	UPROPERTY()
	FMetaHumanPaletteItemPath ItemPath;

	UPROPERTY()
	TWeakObjectPtr<UMetaHumanWardrobeItem> WardrobeItem;

	UPROPERTY(EditAnywhere, Category = "Overrides", meta = (FixedLayout, ShowOnlyInnerProperties))
	FInstancedPropertyBag InstanceParameters;
};

/**
 * The Detail Customization for this class contains the costume editing UI
 */
UCLASS(Transient)
class UMetaHumanCharacterEditorCostumeToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UMetaHumanCollection> Collection;

	/** The array of costume items containing costume parameters */
	UPROPERTY(EditAnywhere, Category = "Costume", meta = (ShowOnlyInnerProperties))
	TArray<TObjectPtr<UMetaHumanCharacterEditorCostumeItem>> CostumeItems;
};

/**
 * The Costume Tool allows the user to add, remove and apply adornments, such as hair and clothing
 */
UCLASS()
class UMetaHumanCharacterEditorCostumeTool : public USingleTargetWithSelectionTool
{
	GENERATED_BODY()

public:
	/** Get the Costume Tool properties. */
	UMetaHumanCharacterEditorCostumeToolProperties* GetCostumeToolProperties() const { return PropertyObject; }

	//~Begin UInteractiveTool interface
	virtual void Setup() override;

	virtual bool HasCancel() const override { return false; }
	virtual bool HasAccept() const override { return false; }
	virtual bool CanAccept() const override { return false; }
	//~End UInteractiveTool interface

	void UpdateCostumeItems();

private:
	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterEditorCostumeToolProperties> PropertyObject;
};
