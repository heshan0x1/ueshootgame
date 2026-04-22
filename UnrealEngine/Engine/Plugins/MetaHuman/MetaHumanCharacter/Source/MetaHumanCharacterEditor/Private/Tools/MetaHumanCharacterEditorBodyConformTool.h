// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanCharacterEditorSubTools.h"
#include "MetaHumanCharacterEditorSubsystem.h"

#include "MetaHumanCharacterEditorBodyConformTool.generated.h"

UCLASS()
class UMetaHumanCharacterEditorBodyConformToolBuilder : public UMetaHumanCharacterEditorToolWithToolTargetsBuilder
{
	GENERATED_BODY()

public:

	//~Begin UInteractiveToolWithToolTargetsBuilder interface
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& InSceneState) const override;
	//~End UInteractiveToolWithToolTargetsBuilder interface

protected:

	//~Begin UInteractiveToolWithToolTargetsBuilder interface
	virtual const FToolTargetTypeRequirements& GetTargetRequirements() const override;
	//~End UInteractiveToolWithToolTargetsBuilder interface
};

UCLASS(Abstract)
class UMetaHumanCharacterImportBodySubToolBase : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	virtual bool CanConform() const PURE_VIRTUAL(UMetaHumanCharacterImportBodySubToolBase::CanConform(), { return false; });
	virtual void Conform() PURE_VIRTUAL(UMetaHumanCharacterImportBodySubToolBase::Conform(), {});

	virtual bool CanImportMesh() const PURE_VIRTUAL(UMetaHumanCharacterImportBodySubToolBase::CanImportMesh(), { return false; });
	virtual void ImportMesh() PURE_VIRTUAL(UMetaHumanCharacterImportBodySubToolBase::ImportMesh(), {});

	virtual bool CanImportJoints() const PURE_VIRTUAL(UMetaHumanCharacterImportBodySubToolBase::CanImportJoints(), { return false; });
	virtual void ImportJoints() PURE_VIRTUAL(UMetaHumanCharacterImportBodySubToolBase::ImportJoints(), {});

	/* Display a conform error message */
	virtual void DisplayConformError(const FText& ErrorMessageText) const;
};

UCLASS()
class UMetaHumanCharacterImportBodyDNAProperties : public UMetaHumanCharacterImportBodySubToolBase
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "File", meta = (FilePathFilter = "DNA file (*.dna)|*.dna"))
	FFilePath BodyDNAFile;

	UPROPERTY(EditAnywhere, Category = "File", meta = (FilePathFilter = "DNA file (*.dna)|*.dna"))
	FFilePath HeadDNAFile;

	UPROPERTY(EditAnywhere, Category = "Import DNA Options", meta = (ShowOnlyInnerProperties))
	FConformBodyParams ImportOptions;

public:

	virtual bool CanConform() const override;
	virtual void Conform() override;

	virtual bool CanImportMesh() const override;
	virtual void ImportMesh() override;

	virtual bool CanImportJoints() const override;
	virtual void ImportJoints() override;

	bool CanImportWholeRig() const;
	void ImportWholeRig();

private:
	bool GetErrorMessageText(EImportErrorCode InErrorCode, FText& OutErrorMessage) const;
};

UCLASS()
class UMetaHumanCharacterImportBodyTemplateProperties : public UMetaHumanCharacterImportBodySubToolBase
{
	GENERATED_BODY()

public:

	// Static/skeletal mesh used as source for mesh/skeleton. Must be body only, using MetaHuman topology.
	UPROPERTY(EditAnywhere, Category = "Asset", meta = (AllowedClasses = "/Script/Engine.StaticMesh, /Script/Engine.SkeletalMesh"))
	TSoftObjectPtr<UObject> BodyMesh;

	UPROPERTY(EditAnywhere, Category = "Asset", meta = (AllowedClasses = "/Script/Engine.StaticMesh, /Script/Engine.SkeletalMesh"))
	TSoftObjectPtr<UObject> HeadMesh;

	// Get corresponding vertices from template mesh by matching the template mesh's UVs to MH standard
	UPROPERTY(EditAnywhere, Category = "Asset")
	bool bMatchVerticesByUVs;

	UPROPERTY(EditAnywhere, Category= "Options", meta = (ShowOnlyInnerProperties))
	FConformBodyParams ConformBodyParams;

public:

	virtual bool CanConform() const override;
	virtual void Conform() override;

	virtual bool CanImportMesh() const override;
	virtual void ImportMesh() override;

	virtual bool CanImportJoints() const override;
	virtual void ImportJoints() override;

private:
	bool GetErrorMessageText(EImportErrorCode InErrorCode, FText& OutErrorMessage) const;
};

UCLASS()
class UMetaHumanCharacterEditorBodyConformTool : public UMetaHumanCharacterEditorToolWithSubTools
{
	GENERATED_BODY()

public:

	//~Begin UMetaHumanCharacterEditorToolWithSubTools interface
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType InShutdownType) override;
	//~End UMetaHumanCharacterEditorToolWithSubTools interface

	TSharedRef<const FMetaHumanCharacterBodyIdentity::FState> GetOriginalState() const;
	const TArray<uint8>& GetOriginalDNABuffer() const;
	void UpdateOriginalState();
	void UpdateOriginalDNABuffer();

private:

	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterImportBodyDNAProperties> ImportDNAProperties;

	UPROPERTY()
	TObjectPtr<UMetaHumanCharacterImportBodyTemplateProperties> ImportTemplateProperties;

	// Hold the original state of the character, used to undo changes on cancel
	TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> OriginalState;

	// Hold the original DNA buffer of the character, used to undo changes on cancel
	TArray<uint8> OriginalDNABuffer;
};