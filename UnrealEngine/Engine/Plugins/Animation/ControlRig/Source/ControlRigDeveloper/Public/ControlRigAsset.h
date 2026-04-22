// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "ControlRigDefines.h"
#include "Rigs/RigHierarchyContainer.h"
#include "Rigs/RigHierarchy.h"
#include "Interfaces/Interface_PreviewMeshProvider.h"
#include "ControlRigGizmoLibrary.h"
#include "ControlRigSchema.h"
#include "RigVMCore/RigVMStatistics.h"
#include "RigVMModel/RigVMClient.h"
#include "ControlRigValidationPass.h"
#include "RigVMAsset.h"
#include "Rigs/RigModuleDefines.h"
#include "ModularRigModel.h"

#if WITH_EDITOR
#include "Overrides/SOverrideListWidget.h"
#endif

#include "ControlRigAsset.generated.h"

#define UE_API CONTROLRIGDEVELOPER_API

class FRigVMNameValidator;
struct FRigVMOldPublicFunctionData;
class IControlRigAssetInterface;
typedef TScriptInterface<IControlRigAssetInterface> FControlRigAssetInterfacePtr;

class USkeletalMesh;
class UControlRigGraph;
struct FEndLoadPackageContext;


UINTERFACE(BlueprintType)
class UE_API UControlRigAssetInterface : public UInterface
{
	GENERATED_BODY()
};

class IControlRigAssetInterface 
{
	GENERATED_BODY()

public:
	UE_API static FControlRigAssetInterfacePtr GetInterfaceOuter(const UObject* InObject);
	static TArray<UClass*> FindAllImplementingClasses();
	
	virtual FRigVMAssetInterfacePtr GetRigVMAssetInterface() = 0;
	virtual const FRigVMAssetInterfacePtr GetRigVMAssetInterface() const { return const_cast<IControlRigAssetInterface*>(this)->GetRigVMAssetInterface(); }
	virtual URigHierarchy* GetHierarchy() = 0;
	virtual URigHierarchy* GetHierarchy() const { return const_cast<IControlRigAssetInterface*>(this)->GetHierarchy(); }
	UE_API virtual FRigVMClient* GetRigVMClient();

protected:

	virtual bool RequiresForceLoadMembersSuper(UObject* InObject) const = 0;
	virtual void SerializeSuper(FArchive& Ar) = 0;
	virtual void HandlePackageDoneSuper() = 0;
	virtual void SetHierarchy(TObjectPtr<URigHierarchy> InHierarchy) = 0;

	UObject* GetObject() { return GetRigVMAssetInterface()->GetObject(); }
	const UObject* GetObject() const { return const_cast<IControlRigAssetInterface*>(this)->GetObject(); }
	FControlRigAssetInterfacePtr GetControlRigAssetInterface() { return FControlRigAssetInterfacePtr(GetObject()); }

public:
	void Modify() { GetObject()->Modify(); }
	UObject* GetObjectBeingDebugged() { return GetRigVMAssetInterface()->GetObjectBeingDebugged(); }
	const UObject* GetObjectBeingDebugged() const { return GetRigVMAssetInterface()->GetObjectBeingDebugged(); }

	static void CommonInitialization(const FObjectInitializer& ObjectInitializer);
	IControlRigAssetInterface();

	//  --- IRigVMClientHost interface ---
	virtual UClass* GetRigVMSchemaClass() const { return UControlRigSchema::StaticClass(); }
	virtual UScriptStruct* GetRigVMExecuteContextStruct() const { return FControlRigExecuteContext::StaticStruct(); }
	virtual UClass* GetRigVMEdGraphClass() const;
	virtual UClass* GetRigVMEdGraphNodeClass() const;
	virtual UClass* GetRigVMEdGraphSchemaClass() const;
	virtual UClass* GetRigVMEditorSettingsClass() const;

	// URigVMBlueprint interface
	//virtual UClass* GetRigVMGeneratedClassPrototype() const override { return UControlRigBlueprintGeneratedClass::StaticClass(); }
	virtual TArray<FString> GeneratePythonCommands(const FString InNewBlueprintName);
	virtual void GetPreloadDependencies(TArray<UObject*>& OutDeps);
#if WITH_EDITOR
	virtual const FLazyName& GetPanelPinFactoryName() const;
	static inline const FLazyName ControlRigPanelNodeFactoryName = FLazyName(TEXT("FControlRigGraphPanelPinFactory"));
	virtual IRigVMEditorModule* GetEditorModule() const;
#endif

	virtual void Serialize(FArchive& Ar);

#if WITH_EDITOR

	// UBlueprint interface
	virtual UClass* GetBlueprintClass() const;
	void OnRegeneratedClass(UClass* ClassToRegenerate, UObject* PreviousCDO);
	virtual bool SupportedByDefaultBlueprintFactory() const { return false; }
	virtual bool IsValidForBytecodeOnlyRecompile() const { return false; }
	virtual void GetTypeActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const;
	virtual void GetInstanceActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const;
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext);
	virtual void PostLoad();
	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent);
	virtual void PostDuplicate(bool bDuplicateForPIE);
	virtual void PostRename(UObject* OldOuter, const FName OldName);
	virtual bool RequiresForceLoadMembers(UObject* InObject) const;

	virtual bool SupportsGlobalVariables() const { return true; }
	virtual bool SupportsLocalVariables() const { return !IsModularRig(); }
	virtual bool SupportsFunctions() const { return !IsModularRig(); }
	virtual bool SupportsEventGraphs() const { return !IsModularRig(); }


	// UObject interface
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent);

#endif	// #if WITH_EDITOR

	UE_API virtual UClass* GetControlRigClass() const;

	UE_API bool IsModularRig() const;

	UE_API virtual UControlRig* CreateControlRig();

	UE_API virtual UControlRig* GetDebuggedControlRig();

	/** IInterface_PreviewMeshProvider interface */
	virtual void SetPreviewMesh(USkeletalMesh* PreviewMesh, bool bMarkAsDirty = true);
	
	virtual USkeletalMesh* GetPreviewMesh() const;

	UE_API virtual bool IsControlRigModule() const;

#if WITH_EDITORONLY_DATA
	
	UE_API bool CanTurnIntoControlRigModule(bool InAutoConvertHierarchy, FString* OutErrorMessage = nullptr) const;

	UE_API bool TurnIntoControlRigModule(bool InAutoConvertHierarchy = false, FString* OutErrorMessage = nullptr);

	UE_API bool CanTurnIntoStandaloneRig(FString* OutErrorMessage = nullptr) const;

	UE_API bool TurnIntoStandaloneRig(FString* OutErrorMessage = nullptr);

	TArray<URigVMNode*> ConvertHierarchyElementsToSpawnerNodes(URigHierarchy* InHierarchy, TArray<FRigElementKey> InKeys, bool bRemoveElements = true);

#endif // WITH_EDITORONLY_DATA

	UE_API UTexture2D* GetRigModuleIcon() const;

	DECLARE_EVENT_OneParam(IControlRigAssetInterface, FOnRigTypeChanged, FControlRigAssetInterfacePtr);

	FOnRigTypeChanged& OnRigTypeChanged() { return OnRigTypeChangedDelegate; }

	virtual FModularRigSettings& GetModularRigSettings() = 0;
	const FModularRigSettings& GetModularRigSettings() const { return const_cast<IControlRigAssetInterface*>(this)->GetModularRigSettings(); }

	virtual FRigHierarchySettings& GetHierarchySettings() = 0;
	const FRigHierarchySettings& GetHierarchySettings() const { return const_cast<IControlRigAssetInterface*>(this)->GetHierarchySettings(); }

	virtual FRigModuleSettings& GetRigModuleSettings() = 0;
	virtual const FRigModuleSettings& GetRigModuleSettings() const { return const_cast<IControlRigAssetInterface*>(this)->GetRigModuleSettings(); }

	// This relates to FAssetThumbnailPool::CustomThumbnailTagName and allows
	// the thumbnail pool to show the thumbnail of the icon rather than the
	// rig itself to avoid deploying the 3D renderer.
	virtual FString& GetCustomThumbnail() = 0;

	/** Asset searchable information module references in this rig */
	virtual TArray<FModuleReferenceData>& GetModuleReferenceData() = 0;

	virtual TMap<FRigElementKey, FRigElementKeyCollection>& GetArrayConnectionMap() = 0;
	const TMap<FRigElementKey, FRigElementKeyCollection>& GetArrayConnectionMap() const { return const_cast<IControlRigAssetInterface*>(this)->GetArrayConnectionMap(); }

	TArray<FModuleReferenceData> FindReferencesToModule() const;

	UE_API static EControlRigType GetRigType(const FAssetData& InAsset);
	UE_API static TArray<FSoftObjectPath> GetReferencesToRigModule(const FAssetData& InModuleAsset);

	UE_API virtual void UpdateExposedModuleConnectors() const;

#if WITH_EDITOR
	UE_API TArray<FOverrideStatusSubject> GetOverrideSubjects() const;
	UE_API uint32 GetOverrideSubjectsHash() const;
#endif
	
protected:

	FName FindHostMemberVariableUniqueName(TSharedPtr<FRigVMNameValidator> InNameValidator, const FString& InBaseName);

	TArray<FModuleReferenceData> GetModuleReferenceDataImpl() const;

	FOnRigTypeChanged OnRigTypeChangedDelegate;
	
	UE_API bool ResolveConnector(const FRigElementKey& DraggedKey, const FRigElementKey& TargetKey, bool bSetupUndoRedo = true);
	UE_API bool ResolveConnectorToArray(const FRigElementKey& DraggedKey, const TArray<FRigElementKey>& TargetKeys, bool bSetupUndoRedo = true);

	void UpdateConnectionMapFromModel();

	virtual void SetupDefaultObjectDuringCompilation(URigVMHost* InCDO);

public:

	virtual void SetupPinRedirectorsForBackwardsCompatibility();

	static TArray<FControlRigAssetInterfacePtr> GetCurrentlyOpenRigAssets();

#if WITH_EDITORONLY_DATA
	virtual TArray<TSoftObjectPtr<UControlRigShapeLibrary>>& GetShapeLibraries() = 0;
	const TArray<TSoftObjectPtr<UControlRigShapeLibrary>>& GetShapeLibraries() const { return const_cast<IControlRigAssetInterface*>(this)->GetShapeLibraries(); }

	UE_API const FControlRigShapeDefinition* GetControlShapeByName(const FName& InName) const;

#endif

	virtual FRigVMDrawContainer& GetDrawContainer() = 0;
	const FRigVMDrawContainer& GetDrawContainer() const { return const_cast<IControlRigAssetInterface*>(this)->GetDrawContainer(); }

#if WITH_EDITOR
	/** Remove a transient / temporary control used to interact with a pin */
	UE_API FName AddTransientControl(const URigVMUnitNode* InNode, const FRigDirectManipulationTarget& InTarget);

	/** Remove a transient / temporary control used to interact with a pin */
	UE_API FName RemoveTransientControl(const URigVMUnitNode* InNode, const FRigDirectManipulationTarget& InTarget);

	/** Remove a transient / temporary control used to interact with a bone */
	UE_API FName AddTransientControl(const FRigElementKey& InElement);

	/** Remove a transient / temporary control used to interact with a bone */
	UE_API FName RemoveTransientControl(const FRigElementKey& InElement);

	/** Removes all  transient / temporary control used to interact with pins */
	UE_API void ClearTransientControls();

#endif

	virtual FRigInfluenceMapPerEvent& GetInfluences() = 0;

public:

	UE_API virtual URigHierarchyController* GetHierarchyController() { return GetHierarchy()->GetController(true); }

	virtual FModularRigModel& GetModularRigModel() = 0;
	const FModularRigModel& GetModularRigModel() const { return const_cast<IControlRigAssetInterface*>(this)->GetModularRigModel(); }

	UE_API virtual UModularRigController* GetModularRigController();

	UE_API virtual void RecompileModularRig();

	virtual EControlRigType& GetControlRigType() = 0;

	virtual FName& GetItemTypeDisplayName() = 0;

protected:

	/** Whether or not this rig has an Inversion Event */
	virtual bool& GetSupportsInversion() = 0;

	/** Whether or not this rig has Controls on It */
	virtual bool& GetSupportsControls() = 0;

	/** The default skeletal mesh to use when previewing this asset */
#if WITH_EDITORONLY_DATA
	virtual TSoftObjectPtr<USkeletalMesh>& GetPreviewSkeletalMesh() = 0;
	virtual const TSoftObjectPtr<USkeletalMesh>& GetPreviewSkeletalMesh() const { return const_cast<IControlRigAssetInterface*>(this)->GetPreviewSkeletalMesh(); }
#endif

	/** The skeleton from import into a hierarchy */
	virtual TSoftObjectPtr<UObject>& GetSourceHierarchyImport() = 0;

	/** The skeleton from import into a curve */
	virtual TSoftObjectPtr<UObject>& GetSourceCurveImport() = 0;

	/** If set to true, this control rig has animatable controls */
	virtual bool& GetExposesAnimatableControls() = 0;
public:

	/** If set to true, multiple control rig tracks can be created for the same rig in sequencer*/
	virtual bool& GetAllowMultipleInstances() = 0;

protected:

	UE_API static TArray<FControlRigAssetInterfacePtr> sCurrentlyOpenedRigBlueprints;

	virtual void PathDomainSpecificContentOnLoad();
	virtual void GetBackwardsCompatibilityPublicFunctions(TArray<FName>& BackwardsCompatiblePublicFunctions, TMap<URigVMLibraryNode*, FRigVMGraphFunctionHeader>& OldHeaders) = 0;
	void PatchRigElementKeyCacheOnLoad();
	void PatchPropagateToChildren();

protected:
	virtual void CreateMemberVariablesOnLoad();
	virtual void PatchVariableNodesOnLoad();

public:
	UE_API void UpdateElementKeyRedirector(UControlRig* InControlRig) const;
	UE_API void PropagatePoseFromInstanceToBP(UControlRig* InControlRig) const;
	UE_API void PropagatePoseFromBPToInstances() const;
	UE_API void PropagateHierarchyFromBPToInstances() const;
	UE_API void PropagateDrawInstructionsFromBPToInstances() const;
	void PropagatePropertyFromBPToInstances(FRigElementKey InRigElement, const FProperty* InProperty) const;
	void PropagatePropertyFromInstanceToBP(FRigElementKey InRigElement, const FProperty* InProperty, UControlRig* InInstance) const;
	UE_API void PropagateModuleHierarchyFromBPToInstances() const;
	void UpdateModularDependencyDelegates();
	void OnModularDependencyVMCompiled(UObject* InBlueprint, URigVM* InVM, FRigVMExtendedExecuteContext& InExecuteContext);
	void OnModularDependencyChanged(FRigVMAssetInterfacePtr InBlueprint);
	void RequestConstructionOnAllModules();
	UE_API void RefreshModuleVariables();
	UE_API void RefreshModuleVariables(const FRigModuleReference* InModule);
	UE_API void RefreshModuleConnectors();
	UE_API void RefreshModuleConnectors(const FRigModuleReference* InModule, bool bPropagateHierarchy = true);

	/**
	* Returns the modified event, which can be used to 
	* subscribe to topological changes happening within the hierarchy. The event is broadcast only after all hierarchy instances are up to date
	* @return The event used for subscription.
	*/
	FRigHierarchyModifiedEvent& OnHierarchyModified() { return HierarchyModifiedEvent; }

	FOnRigVMRefreshEditorEvent& OnModularRigPreCompiled() { return ModularRigPreCompiled; }
	FOnRigVMRefreshEditorEvent& OnModularRigCompiled() { return ModularRigCompiled; }

protected:

	virtual TObjectPtr<UControlRigValidator>& GetValidator() = 0;

	FRigHierarchyModifiedEvent	HierarchyModifiedEvent;
	FOnRigVMRefreshEditorEvent ModularRigPreCompiled;
	FOnRigVMRefreshEditorEvent ModularRigCompiled;

	int32 ModulesRecompilationBracket = 0;


	void HandleHierarchyModified(ERigHierarchyNotification InNotification, URigHierarchy* InHierarchy, const FRigNotificationSubject& InSubject);
	void HandleHierarchyElementKeyChanged(const FRigElementKey& InOldKey, const FRigElementKey& InNewKey);
	void HandleHierarchyComponentKeyChanged(const FRigComponentKey& InOldKey, const FRigComponentKey& InNewKey);

	void HandleRigModulesModified(EModularRigNotification InNotification, const FRigModuleReference* InModule);

#if WITH_EDITOR
	virtual void HandlePackageDone();
	virtual void HandleConfigureRigVMController(const FRigVMClient* InClient, URigVMController* InControllerToConfigure);
#endif

	void UpdateConnectionMapAfterRename(const FString& InOldModuleName);

	// Class used to temporarily cache all 
	// current control values and reapply them
	// on destruction
	class FControlValueScope
	{
	public: 
		UE_API FControlValueScope(FControlRigAssetInterfacePtr InBlueprint);
		UE_API ~FControlValueScope();

	protected:

		FControlRigAssetInterfacePtr Blueprint;
		TMap<FName, FRigControlValue> ControlValues;
	};

	virtual float& GetDebugBoneRadius() = 0;

#if WITH_EDITOR
		
public:

	/** Shape libraries to load during package load completed */ 
	TArray<FString> ShapeLibrariesToLoadOnPackageLoaded;

#endif

	friend class FControlRigBlueprintCompilerContext;
	friend class SRigHierarchy;
	friend class SRigCurveContainer;
	friend class FControlRigBaseEditor;
#if WITH_RIGVMLEGACYEDITOR
	friend class FControlRigLegacyEditor;
#endif
	friend class FControlRigEditor;
	friend class UEngineTestControlRig;
	friend class FControlRigEditMode;
	friend class FControlRigBlueprintActions;
	friend class FControlRigDrawContainerDetails;
	friend class UDefaultControlRigManipulationLayer;
	friend struct FRigValidationTabSummoner;
	friend class UAnimGraphNode_ControlRig;
	friend class UControlRigThumbnailRenderer;
	friend class FControlRigGraphDetails;
	friend class FControlRigEditorModule;
	friend class UControlRigComponent;
	friend struct FControlRigGraphSchemaAction_PromoteToVariable;
	friend class UControlRigGraphSchema;
	friend class FControlRigBlueprintDetails;
	friend class FRigConnectorElementDetails;
};

#undef UE_API