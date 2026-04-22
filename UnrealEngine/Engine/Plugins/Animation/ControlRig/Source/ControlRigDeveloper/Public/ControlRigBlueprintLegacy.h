// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "UObject/ObjectMacros.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture2D.h"
#include "ControlRigDefines.h"
#include "Rigs/RigHierarchyContainer.h"
#include "Rigs/RigHierarchy.h"
#include "Interfaces/Interface_PreviewMeshProvider.h"
#include "ControlRigGizmoLibrary.h"
#include "ControlRigSchema.h"
#include "RigVMCore/RigVMStatistics.h"
#include "RigVMModel/RigVMClient.h"
#include "ControlRigValidationPass.h"
#include "RigVMBlueprintLegacy.h"
#include "Rigs/RigModuleDefines.h"
#include "ModularRigModel.h"
#include "ControlRigAsset.h"

#if WITH_EDITOR
#include "Kismet2/CompilerResultsLog.h"
#include "Overrides/SOverrideListWidget.h"
#endif

#include "ControlRigBlueprintLegacy.generated.h"

class URigVMBlueprintGeneratedClass;

UCLASS(MinimalAPI, BlueprintType, meta=(IgnoreClassThumbnail))
class UControlRigBlueprint : public URigVMBlueprint, public IControlRigAssetInterface, public IInterface_PreviewMeshProvider, public IRigHierarchyProvider
{
	GENERATED_UCLASS_BODY()

public:
	CONTROLRIGDEVELOPER_API UControlRigBlueprint();

	// --- IControlRigAssetInterface interface ---
	virtual FRigVMAssetInterfacePtr GetRigVMAssetInterface() override { return FRigVMAssetInterfacePtr(this); }
	virtual FModularRigModel& GetModularRigModel() override { return ModularRigModel; }
	virtual URigHierarchy* GetHierarchy() const override { return Hierarchy; }
	virtual URigHierarchy* GetHierarchy() override { return Hierarchy; }
	virtual TSoftObjectPtr<USkeletalMesh>& GetPreviewSkeletalMesh() override { return PreviewSkeletalMesh; }
	virtual bool& GetAllowMultipleInstances() override { return bAllowMultipleInstances; }
	virtual TMap<FRigElementKey, FRigElementKeyCollection>& GetArrayConnectionMap() override { return ArrayConnectionMap; }
	virtual EControlRigType& GetControlRigType() override { return ControlRigType; }
	virtual FString& GetCustomThumbnail() override { return CustomThumbnail; }
	virtual float& GetDebugBoneRadius() override { return DebugBoneRadius; }
	virtual FRigVMDrawContainer& GetDrawContainer() override { return DrawContainer; }
	virtual bool& GetExposesAnimatableControls() override { return bExposesAnimatableControls; }
	virtual FRigHierarchySettings& GetHierarchySettings() override { return HierarchySettings; }
	virtual FRigInfluenceMapPerEvent& GetInfluences() override { return Influences; }
	virtual FName& GetItemTypeDisplayName() override { return ItemTypeDisplayName; }
	virtual FModularRigSettings& GetModularRigSettings() override { return ModularRigSettings; }
	virtual FRigModuleSettings& GetRigModuleSettings() override { return RigModuleSettings; }
	virtual TArray<FModuleReferenceData>& GetModuleReferenceData() override { return ModuleReferenceData; }
	virtual TArray<TSoftObjectPtr<UControlRigShapeLibrary>>& GetShapeLibraries() override { return ShapeLibraries; }
	virtual TSoftObjectPtr<UObject>& GetSourceCurveImport() override { return SourceCurveImport; }
	virtual TSoftObjectPtr<UObject>& GetSourceHierarchyImport() override { return SourceHierarchyImport; }
	virtual bool& GetSupportsControls() override { return bSupportsControls; }
	virtual bool& GetSupportsInversion() override { return bSupportsInversion; }
	virtual TObjectPtr<UControlRigValidator>& GetValidator() override { return Validator; }

	//  --- IRigVMClientHost interface ---
	virtual UClass* GetRigVMSchemaClass() const override { return IControlRigAssetInterface::GetRigVMSchemaClass(); }
	virtual UScriptStruct* GetRigVMExecuteContextStruct() const override { return IControlRigAssetInterface::GetRigVMExecuteContextStruct(); }
	CONTROLRIGDEVELOPER_API virtual UClass* GetRigVMEdGraphClass() const override { return IControlRigAssetInterface::GetRigVMEdGraphClass(); }
	CONTROLRIGDEVELOPER_API virtual UClass* GetRigVMEdGraphNodeClass() const override { return IControlRigAssetInterface::GetRigVMEdGraphNodeClass(); }
	CONTROLRIGDEVELOPER_API virtual UClass* GetRigVMEdGraphSchemaClass() const override { return IControlRigAssetInterface::GetRigVMEdGraphSchemaClass(); }
	CONTROLRIGDEVELOPER_API virtual UClass* GetRigVMEditorSettingsClass() const override { return IControlRigAssetInterface::GetRigVMEditorSettingsClass(); }

	// URigVMBlueprint interface
	UE_DEPRECATED(5.7, "Please use GetRigVMGeneratedClassPrototype")
	virtual UClass* GetRigVMBlueprintGeneratedClassPrototype() const { return UControlRigBlueprintGeneratedClass::StaticClass(); }
	virtual UClass* GetRigVMGeneratedClassPrototype() const override { return UControlRigBlueprintGeneratedClass::StaticClass(); }
	
	CONTROLRIGDEVELOPER_API virtual TArray<FString> GeneratePythonCommands(const FString InNewBlueprintName) override { return IControlRigAssetInterface::GeneratePythonCommands(InNewBlueprintName); }
	CONTROLRIGDEVELOPER_API virtual void GetPreloadDependencies(TArray<UObject*>& OutDeps) override;
#if WITH_EDITOR
	CONTROLRIGDEVELOPER_API virtual const FLazyName& GetPanelPinFactoryName() const override { return IControlRigAssetInterface::GetPanelPinFactoryName(); }
	CONTROLRIGDEVELOPER_API virtual IRigVMEditorModule* GetEditorModule() const override { return IControlRigAssetInterface::GetEditorModule(); }
#endif

	CONTROLRIGDEVELOPER_API virtual void Serialize(FArchive& Ar) override;
	virtual void SerializeSuper(FArchive& Ar) override { return URigVMBlueprint::SerializeSuper(Ar); }

#if WITH_EDITOR

	// UBlueprint interface
	CONTROLRIGDEVELOPER_API virtual UClass* GetBlueprintClass() const override { return IControlRigAssetInterface::GetBlueprintClass(); }
	CONTROLRIGDEVELOPER_API virtual UClass* RegenerateClass(UClass* ClassToRegenerate, UObject* PreviousCDO) override;
	virtual bool RequiresForceLoadMembersSuper(UObject* InObject) const override { return URigVMBlueprint::RequiresForceLoadMembersSuper(InObject); }
	virtual bool SupportedByDefaultBlueprintFactory() const override { return IControlRigAssetInterface::SupportedByDefaultBlueprintFactory(); }
	virtual bool IsValidForBytecodeOnlyRecompile() const override { return IControlRigAssetInterface::IsValidForBytecodeOnlyRecompile(); }
	CONTROLRIGDEVELOPER_API virtual void GetTypeActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override { return IControlRigAssetInterface::GetTypeActions(ActionRegistrar); }
	CONTROLRIGDEVELOPER_API virtual void GetInstanceActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override { return IControlRigAssetInterface::GetInstanceActions(ActionRegistrar); }
	CONTROLRIGDEVELOPER_API virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;
	CONTROLRIGDEVELOPER_API virtual void PostLoad() override;
	CONTROLRIGDEVELOPER_API virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;
	CONTROLRIGDEVELOPER_API virtual void PostDuplicate(bool bDuplicateForPIE) override;
	CONTROLRIGDEVELOPER_API virtual void PostRename(UObject* OldOuter, const FName OldName) override;
	CONTROLRIGDEVELOPER_API virtual bool RequiresForceLoadMembers(UObject* InObject) const override { return IControlRigAssetInterface::RequiresForceLoadMembers(InObject); }

	virtual bool SupportsGlobalVariables() const override { return IControlRigAssetInterface::SupportsGlobalVariables(); }
	virtual bool SupportsLocalVariables() const override { return IControlRigAssetInterface::SupportsLocalVariables(); }
	virtual bool SupportsFunctions() const override { return IControlRigAssetInterface::SupportsFunctions(); }
	virtual bool SupportsEventGraphs() const override { return IControlRigAssetInterface::SupportsEventGraphs(); }


	// UObject interface
	CONTROLRIGDEVELOPER_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	CONTROLRIGDEVELOPER_API virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;

#endif	// #if WITH_EDITOR

	UFUNCTION(BlueprintCallable, Category = "VM")
	CONTROLRIGDEVELOPER_API virtual UClass* GetControlRigClass() const { return IControlRigAssetInterface::GetControlRigClass(); }

	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	UControlRig* CreateControlRig() { return IControlRigAssetInterface::CreateControlRig(); }

	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	UControlRig* GetDebuggedControlRig() { return IControlRigAssetInterface::GetDebuggedControlRig(); } 

	/** IInterface_PreviewMeshProvider interface */
	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	CONTROLRIGDEVELOPER_API virtual void SetPreviewMesh(USkeletalMesh* PreviewMesh, bool bMarkAsDirty = true) override { return IControlRigAssetInterface::SetPreviewMesh(PreviewMesh, bMarkAsDirty); }
	
	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	CONTROLRIGDEVELOPER_API virtual USkeletalMesh* GetPreviewMesh() const override { return IControlRigAssetInterface::GetPreviewMesh(); }

	UFUNCTION(BlueprintPure, Category = "Control Rig Blueprint")
	CONTROLRIGDEVELOPER_API virtual bool IsControlRigModule() const { return IControlRigAssetInterface::IsControlRigModule(); }

#if WITH_EDITORONLY_DATA
	
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "TurnIntoControlRigModule", ScriptName = "TurnIntoControlRigModule"), Category = "Control Rig Blueprint")
	bool TurnIntoControlRigModule_Blueprint() { return IControlRigAssetInterface::TurnIntoControlRigModule(); }

	UFUNCTION(BlueprintPure, meta = (DisplayName = "CanTurnIntoStandaloneRig", ScriptName = "CanTurnIntoStandaloneRig"), Category = "Control Rig Blueprint")
	bool CanTurnIntoStandaloneRig_Blueprint() const { return IControlRigAssetInterface::CanTurnIntoStandaloneRig(); }

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "TurnIntoStandaloneRig", ScriptName = "TurnIntoStandaloneRig"), Category = "Control Rig Blueprint")
	bool TurnIntoStandaloneRig_Blueprint() { return IControlRigAssetInterface::TurnIntoStandaloneRig(); }

	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	CONTROLRIGDEVELOPER_API TArray<URigVMNode*> ConvertHierarchyElementsToSpawnerNodes(URigHierarchy* InHierarchy, TArray<FRigElementKey> InKeys, bool bRemoveElements = true) { return IControlRigAssetInterface::ConvertHierarchyElementsToSpawnerNodes(InHierarchy, InKeys, bRemoveElements); }

#endif // WITH_EDITORONLY_DATA

	UFUNCTION(BlueprintPure, Category = "Control Rig Blueprint")
	CONTROLRIGDEVELOPER_API UTexture2D* GetRigModuleIcon() const { return IControlRigAssetInterface::GetRigModuleIcon(); }

	UPROPERTY(EditAnywhere, Category = "Modular Rig")
	FModularRigSettings ModularRigSettings;

	UPROPERTY(EditAnywhere, Category = "Hierarchy")
	FRigHierarchySettings HierarchySettings;

	UPROPERTY(EditAnywhere, Category = "Hierarchy", AssetRegistrySearchable)
	FRigModuleSettings RigModuleSettings;

	// This relates to FAssetThumbnailPool::CustomThumbnailTagName and allows
	// the thumbnail pool to show the thumbnail of the icon rather than the
	// rig itself to avoid deploying the 3D renderer.
	UPROPERTY(EditAnywhere, Category = "Hierarchy", AssetRegistrySearchable)
	FString CustomThumbnail;

	/** Asset searchable information module references in this rig */
	UPROPERTY(AssetRegistrySearchable)
	TArray<FModuleReferenceData> ModuleReferenceData;

	UPROPERTY()
	TMap<FRigElementKey, FRigElementKey> ConnectionMap_DEPRECATED;

	UPROPERTY()
	TMap<FRigElementKey, FRigElementKeyCollection> ArrayConnectionMap;

	UFUNCTION(BlueprintPure, Category = "Control Rig Blueprint")
	CONTROLRIGDEVELOPER_API TArray<FModuleReferenceData> FindReferencesToModule() const { return IControlRigAssetInterface::FindReferencesToModule(); }

	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	CONTROLRIGDEVELOPER_API virtual void UpdateExposedModuleConnectors() const { return IControlRigAssetInterface::UpdateExposedModuleConnectors(); }
	
protected:


	/** Asset searchable information about exposed public functions on this rig */
	UPROPERTY(AssetRegistrySearchable)
	TArray<FRigVMOldPublicFunctionData> PublicFunctions_DEPRECATED;

	CONTROLRIGDEVELOPER_API virtual void SetupDefaultObjectDuringCompilation(URigVMHost* InCDO) override { return IControlRigAssetInterface::SetupDefaultObjectDuringCompilation(InCDO); }

public:

	CONTROLRIGDEVELOPER_API virtual void SetupPinRedirectorsForBackwardsCompatibility() override { return IControlRigAssetInterface::SetupPinRedirectorsForBackwardsCompatibility(); }

	UFUNCTION(BlueprintCallable, Category = "VM")
	static CONTROLRIGDEVELOPER_API TArray<UControlRigBlueprint*> GetCurrentlyOpenRigBlueprints();

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TSoftObjectPtr<UControlRigShapeLibrary> GizmoLibrary_DEPRECATED;

	UPROPERTY(EditAnywhere, Category = Shapes)
	TArray<TSoftObjectPtr<UControlRigShapeLibrary>> ShapeLibraries;

	UPROPERTY(transient, DuplicateTransient, meta = (DisplayName = "VM Statistics", DisplayAfter = "VMCompileSettings"))
	FRigVMStatistics Statistics_DEPRECATED;
#endif

	UPROPERTY(EditAnywhere, Category = "Drawing")
	FRigVMDrawContainer DrawContainer;

	UPROPERTY(EditAnywhere, Category = "Influence Map")
	FRigInfluenceMapPerEvent Influences;

public:

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FRigHierarchyContainer HierarchyContainer_DEPRECATED;
#endif

	UPROPERTY(BlueprintReadOnly, Category = "Hierarchy")
	TObjectPtr<URigHierarchy> Hierarchy;

	UFUNCTION(BlueprintCallable, Category = "Hierarchy")
	URigHierarchyController* GetHierarchyController() { return IControlRigAssetInterface::GetHierarchyController(); }

	UPROPERTY(BlueprintReadOnly, Category = "Modules")
	FModularRigModel ModularRigModel;

	UFUNCTION(BlueprintCallable, Category = "Modules")
	CONTROLRIGDEVELOPER_API virtual UModularRigController* GetModularRigController() { return IControlRigAssetInterface::GetModularRigController(); }

	UFUNCTION(BlueprintCallable, Category = "Control Rig Blueprint")
	CONTROLRIGDEVELOPER_API virtual void RecompileModularRig() { return IControlRigAssetInterface::RecompileModularRig(); }

	UPROPERTY(AssetRegistrySearchable)
	EControlRigType ControlRigType;

	UPROPERTY(AssetRegistrySearchable)
	FName ItemTypeDisplayName = TEXT("Control Rig");

private:

	/** Whether or not this rig has an Inversion Event */
	UPROPERTY(AssetRegistrySearchable)
	bool bSupportsInversion;

	/** Whether or not this rig has Controls on It */
	UPROPERTY(AssetRegistrySearchable)
	bool bSupportsControls;

	/** The default skeletal mesh to use when previewing this asset */
#if WITH_EDITORONLY_DATA
	UPROPERTY(AssetRegistrySearchable, EditAnywhere, Category="Control Rig Blueprint")
	TSoftObjectPtr<USkeletalMesh> PreviewSkeletalMesh;
#endif

	/** The skeleton from import into a hierarchy */
	UPROPERTY(DuplicateTransient, AssetRegistrySearchable, EditAnywhere, Category="Control Rig Blueprint")
	TSoftObjectPtr<UObject> SourceHierarchyImport;

	/** The skeleton from import into a curve */
	UPROPERTY(DuplicateTransient, AssetRegistrySearchable, EditAnywhere, Category="Control Rig Blueprint")
	TSoftObjectPtr<UObject> SourceCurveImport;

	/** If set to true, this control rig has animatable controls */
	UPROPERTY(AssetRegistrySearchable)
	bool bExposesAnimatableControls;
public:

	/** If set to true, multiple control rig tracks can be created for the same rig in sequencer*/
	UPROPERTY(EditAnywhere, Category="Sequencer", AssetRegistrySearchable)
	bool bAllowMultipleInstances = false;

private:

	CONTROLRIGDEVELOPER_API virtual void PathDomainSpecificContentOnLoad() override { return IControlRigAssetInterface::PathDomainSpecificContentOnLoad(); }
	CONTROLRIGDEVELOPER_API virtual void GetBackwardsCompatibilityPublicFunctions(TArray<FName>& BackwardsCompatiblePublicFunctions, TMap<URigVMLibraryNode*, FRigVMGraphFunctionHeader>& OldHeaders) override;

protected:
	CONTROLRIGDEVELOPER_API virtual void CreateMemberVariablesOnLoad() override { return IControlRigAssetInterface::CreateMemberVariablesOnLoad(); }
	CONTROLRIGDEVELOPER_API virtual void PatchVariableNodesOnLoad() override;
	virtual void SetHierarchy(TObjectPtr<URigHierarchy> InHierarchy) override { Hierarchy = InHierarchy; }
	
private:

	UPROPERTY()
	TObjectPtr<UControlRigValidator> Validator;

	FRigHierarchyModifiedEvent	HierarchyModifiedEvent;
	FOnRigVMRefreshEditorEvent ModularRigPreCompiled;
	FOnRigVMRefreshEditorEvent ModularRigCompiled;

	UPROPERTY(transient, DuplicateTransient)
	int32 ModulesRecompilationBracket = 0;


#if WITH_EDITOR
	CONTROLRIGDEVELOPER_API virtual void HandlePackageDone() override { return IControlRigAssetInterface::HandlePackageDone(); }
	virtual void HandlePackageDoneSuper() override { return URigVMBlueprint::HandlePackageDone(); }
	CONTROLRIGDEVELOPER_API virtual void HandleConfigureRigVMController(const FRigVMClient* InClient, URigVMController* InControllerToConfigure) override { return IControlRigAssetInterface::HandleConfigureRigVMController(InClient, InControllerToConfigure); }
#endif

	UPROPERTY()
	float DebugBoneRadius;

	friend class FControlRigBlueprintActions;
};
