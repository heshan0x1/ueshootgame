// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMBlueprintGeneratedClass.h"
#include "Engine/Blueprint.h"
#include "RigVMCore/RigVM.h"
#include "RigVMHost.h"
#include "RigVMModel/RigVMClient.h"
#include "RigVMCompiler/RigVMCompiler.h"
#include "RigVMCore/RigVMGraphFunctionDefinition.h"
#include "EdGraph/RigVMEdGraph.h"
#include "EdGraph/RigVMEdGraphSchema.h"
#if WITH_EDITOR
#include "HAL/CriticalSection.h"
#endif

#include "RigVMAsset.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/ObjectSaveContext.h"

#include "RigVMBlueprintLegacy.generated.h"

#define UE_API RIGVMDEVELOPER_API


class URigVMBlueprintGeneratedClass;

USTRUCT(meta = (Deprecated = "5.2"))
struct RIGVMDEVELOPER_API FRigVMOldPublicFunctionArg
{
	GENERATED_BODY();
	
	FRigVMOldPublicFunctionArg()
	: Name(NAME_None)
	, CPPType(NAME_None)
	, CPPTypeObjectPath(NAME_None)
	, bIsArray(false)
	, Direction(ERigVMPinDirection::Input)
	{}

	UPROPERTY()
	FName Name;

	UPROPERTY()
	FName CPPType;

	UPROPERTY()
	FName CPPTypeObjectPath;

	UPROPERTY()
	bool bIsArray;

	UPROPERTY()
	ERigVMPinDirection Direction;

	FEdGraphPinType GetPinType() const;
};

USTRUCT(meta = (Deprecated = "5.2"))
struct RIGVMDEVELOPER_API FRigVMOldPublicFunctionData
{
	GENERATED_BODY();

	FRigVMOldPublicFunctionData()
		:Name(NAME_None)
	{}

	virtual ~FRigVMOldPublicFunctionData();

	UPROPERTY()
	FName Name;

	UPROPERTY()
	FString DisplayName;

	UPROPERTY()
	FString Category;

	UPROPERTY()
	FString Keywords;

	UPROPERTY()
	FRigVMOldPublicFunctionArg ReturnValue;

	UPROPERTY()
	TArray<FRigVMOldPublicFunctionArg> Arguments;

	bool IsMutable() const;
};

UCLASS(BlueprintType, meta=(IgnoreClassThumbnail))
class UE_API URigVMBlueprint : public UBlueprint, public IRigVMAssetInterface, public IRigVMClientHost, public IRigVMExternalDependencyManager
{
	GENERATED_UCLASS_BODY()

public:
	virtual void BeginDestroy() override;
	virtual UObject* GetObject() override { return this; }
	virtual TArray<FRigVMGraphFunctionHeader>& GetPublicGraphFunctions() override { return PublicGraphFunctions; }
	virtual FRigVMClient* GetRigVMClient() override { return &RigVMClient; }
	virtual const FRigVMClient* GetRigVMClient() const override { return &RigVMClient; }
	virtual FRigVMVariant& GetAssetVariant() override { return AssetVariant; }
	virtual const FRigVMVariant& GetAssetVariant() const override { return AssetVariant; }
	virtual FRigVMCompileSettings& GetVMCompileSettings() override { return VMCompileSettings; }
	virtual FRigVMRuntimeSettings& GetVMRuntimeSettings() override { return VMRuntimeSettings; }
	virtual bool IsRegeneratingOnLoad() const override { return bIsRegeneratingOnLoad;}
	virtual TArray<FRigVMGraphVariableDescription> GetAssetVariables() const override;
	virtual TArray<struct FEditedDocumentInfo>& GetLastEditedDocuments() override { return LastEditedDocuments;}

protected:
	virtual UClass* GetRigVMGeneratedClass() override { return GeneratedClass; }
	virtual TArray<FRigVMExternalVariable> GetExternalVariables(bool bFallbackToBlueprint) const override;
	virtual FString GetVariableDefaultValue(const FName& InName, bool bFromDebuggedObject) const override;
	virtual URigVM* GetVM(bool bCreateIfNeeded = true) const override;
	virtual FRigVMExtendedExecuteContext* GetRigVMExtendedExecuteContext() override;
	virtual TScriptInterface<IRigVMGraphFunctionHost> GetRigVMGraphFunctionHost() override { return GetRigVMBlueprintGeneratedClass(); }
	virtual TScriptInterface<const IRigVMGraphFunctionHost> GetRigVMGraphFunctionHost() const override { return GetRigVMBlueprintGeneratedClass(); }
	virtual FCompilerResultsLog* GetCurrentMessageLog() const override { return CurrentMessageLog; }
	virtual void SetAssetStatus(const ERigVMAssetStatus& InStatus) override;
	virtual ERigVMAssetStatus GetAssetStatus() const override;
	virtual bool IsUpToDate() const override { return UBlueprint::IsUpToDate(); }
	virtual TArray<UObject*> GetArchetypeInstances(bool bIncludeCDO, bool bIncludeDerivedClasses) const override;
	virtual const FString& GetObjectBeingDebuggedPath() const override { return UBlueprint::GetObjectPathToDebug(); }
	virtual UWorld* GetWorldBeingDebugged() const override { return UBlueprint::GetWorldBeingDebugged(); }
	virtual void SetWorldBeingDebugged(UWorld* NewWorld) override { return UBlueprint::SetWorldBeingDebugged(NewWorld); }
	virtual FRigVMDebugInfo& GetDebugInfo() override;
	virtual TObjectPtr<URigVMEdGraph>& GetFunctionLibraryEdGraph() override { return FunctionLibraryEdGraph; }
	virtual URigVMHost* CreateRigVMHostSuper(UObject* InOuter) override;
	virtual void MarkAssetAsStructurallyModified(bool bSkipDirtyAssetStatus = false) override;
	virtual void MarkAssetAsModified(FPropertyChangedEvent PropertyChangedEvent = FPropertyChangedEvent(nullptr)) override;
	virtual void AddUbergraphPage(URigVMEdGraph* RigVMEdGraph) override;
	virtual void AddLastEditedDocument(URigVMEdGraph* RigVMEdGraph) override;
	virtual void Compile() override;
	virtual FCompilerResultsLog CompileBlueprint() override;
	virtual void PatchVariableNodesOnLoad() override;
	virtual void AddPinWatch(UEdGraphPin* InPin) override;
	virtual void RemovePinWatch(UEdGraphPin* InPin) override;
	virtual void ClearPinWatches() override;
	virtual bool IsPinBeingWatched(const UEdGraphPin* InPin) const override;
	virtual void ForeachPinWatch(TFunctionRef<void(UEdGraphPin*)> Task) override;
	virtual TMap<FString, FSoftObjectPath>& GetUserDefinedStructGuidToPathName(bool bFromCDO) override;
	virtual TMap<FString, FSoftObjectPath>& GetUserDefinedEnumToPathName(bool bFromCDO) override;
	virtual TSet<TObjectPtr<UObject>>& GetUserDefinedTypesInUse(bool bFromCDO) override;
	virtual FRigVMEdGraphDisplaySettings& GetRigGraphDisplaySettings() override { return RigGraphDisplaySettings; }
	virtual TArray<TObjectPtr<UEdGraph>>& GetFunctionGraphs() override { return FunctionGraphs; }
	virtual bool& IsReferencedObjectPathsStored() override { return ReferencedObjectPathsStored; }
	virtual TArray<FSoftObjectPath>& GetReferencedObjectPaths() override { return ReferencedObjectPaths; }
	virtual TArray<FName> GetSupportedEventNames() override { return SupportedEventNames; }
	virtual void UpdateSupportedEventNames() override;
	virtual TArray<FRigVMReferenceNodeData>& GetFunctionReferenceNodeData() override { return FunctionReferenceNodeData; }
	virtual void NotifyGraphRenamedSuper(class UEdGraph* Graph, FName OldName, FName NewName) override { UBlueprint::NotifyGraphRenamed(Graph, OldName, NewName); }
	virtual UObject* GetDefaultsObject() override;
	virtual void PostEditChangeBlueprintActors() override;

public:
	URigVMBlueprint();

	/** Get the (full) generated class for this rigvm blueprint */
	URigVMBlueprintGeneratedClass* GetRigVMBlueprintGeneratedClass() const;
	/** Returns the class used as the super class for all generated classes */
	virtual UClass* GetRigVMGeneratedClassPrototype() const { return URigVMBlueprintGeneratedClass::StaticClass(); }

	virtual void Serialize(FArchive& Ar) override { IRigVMAssetInterface ::SerializeImpl(Ar); }
	virtual void SerializeSuper(FArchive& Ar) override { UBlueprint ::Serialize(Ar); }
	virtual void PostSerialize(FArchive& Ar) override;

#if WITH_EDITOR

	// UBlueprint interface
	virtual UClass* GetBlueprintClass() const override;
	virtual UClass* RegenerateClass(UClass* ClassToRegenerate, UObject* PreviousCDO) override;
	virtual bool SupportedByDefaultBlueprintFactory() const override { return false; }
	virtual bool IsValidForBytecodeOnlyRecompile() const override { return false; }
	virtual void LoadModulesRequiredForCompilation() override {}
	virtual void GetTypeActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void GetInstanceActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void SetObjectBeingDebugged(UObject* NewObject) override { IRigVMAssetInterface::SetObjectBeingDebugged(NewObject); }
	virtual void SetObjectBeingDebuggedSuper(UObject* NewObject) override { UBlueprint::SetObjectBeingDebugged(NewObject); }
	virtual UObject* GetObjectBeingDebugged() override { return UBlueprint::GetObjectBeingDebugged(); }
	virtual UObject* GetObjectBeingDebugged() const override { return UBlueprint::GetObjectBeingDebugged(); }
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;  
	virtual void PostLoad() override;
#if WITH_EDITORONLY_DATA
	static void DeclareConstructClasses(TArray<FTopLevelAssetPath>& OutConstructClasses, const UClass* SpecificSubclass);
#endif
	virtual bool IsPostLoadThreadSafe() const override { return IRigVMAssetInterface::IsPostLoadThreadSafe(); }
	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override { IRigVMAssetInterface::PostTransacted(TransactionEvent); }
	virtual void PostTransactedSuper(const FTransactionObjectEvent& TransactionEvent) override { UBlueprint::PostTransacted(TransactionEvent); };
	virtual void ReplaceDeprecatedNodes() override { IRigVMAssetInterface::ReplaceDeprecatedNodes(); }
	virtual void ReplaceDeprecatedNodesSuper() override { UBlueprint::ReplaceDeprecatedNodes(); };
	virtual void PreDuplicate(FObjectDuplicationParameters& DupParams) override { IRigVMAssetInterface::PreDuplicate(DupParams); }
	virtual void PreDuplicateSuper(FObjectDuplicationParameters& DupParams) override { UBlueprint::PreDuplicate(DupParams); }
	virtual void PostDuplicate(bool bDuplicateForPIE) override { IRigVMAssetInterface::PostDuplicate(bDuplicateForPIE); }
	virtual void PostDuplicateSuper(bool bDuplicateForPIE) override { UBlueprint::PostDuplicate(bDuplicateForPIE); };
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override { IRigVMAssetInterface::GetAssetRegistryTags(Context); }
	virtual void GetAssetRegistryTagsSuper(FAssetRegistryTagsContext Context) const override { UBlueprint::GetAssetRegistryTags(Context); }
	UE_DEPRECATED(5.4, "Implement the version that takes FAssetRegistryTagsContext instead.")
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;

	virtual bool SupportsGlobalVariables() const override { return true; }
	virtual bool SupportsLocalVariables() const override { return true; }
	virtual bool SupportsFunctions() const override { return true; }
	virtual bool SupportsMacros() const override { return false; }
	virtual bool SupportsDelegates() const override { return false; }
	virtual bool SupportsEventGraphs() const override { return true; }
	virtual bool SupportsAnimLayers() const override { return false; }
	virtual bool ExportGraphToText(UEdGraph* InEdGraph, FString& OutText) override { IRigVMAssetInterface::ExportGraphToText(InEdGraph, OutText); return true; }
	virtual bool TryImportGraphFromText(const FString& InClipboardText, UEdGraph** OutGraphPtr = nullptr) override { return IRigVMAssetInterface::TryImportGraphFromText(InClipboardText, OutGraphPtr); }
	virtual bool CanImportGraphFromText(const FString& InClipboardText) override { return IRigVMAssetInterface::CanImportGraphFromText(InClipboardText); }
	virtual bool RequiresForceLoadMembers(UObject* InObject) const override { return IRigVMAssetInterface::RequiresForceLoadMembers(InObject); }
	virtual bool RequiresForceLoadMembersSuper(UObject* InObject) const override { return UBlueprint::RequiresForceLoadMembers(InObject); }
	

	// UObject interface
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void PostRename(UObject* OldOuter, const FName OldName) override;
	/** Called during cooking. Must return all objects that will be Preload()ed when this is serialized at load time. */
	virtual void GetPreloadDependencies(TArray<UObject*>& OutDeps) override;

	virtual bool MarkPackageDirty() override { return UBlueprint::MarkPackageDirty(); }
	virtual UPackage* GetPackage() override { return UBlueprint::GetPackage(); }
	virtual TArray<TObjectPtr<UEdGraph>>& GetUberGraphs() override { return UbergraphPages; }
	virtual void GetAllEdGraphs(TArray<UEdGraph*>& Graphs) const override { UBlueprint::GetAllGraphs(Graphs); }
	virtual void SetUberGraphs(const TArray<TObjectPtr<UEdGraph>>& InGraphs) override { UbergraphPages = InGraphs; }
	virtual void PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext) override;

	//  --- IRigVMClientHost interface Start---
	virtual FString GetAssetName() const override { return GetName(); }
	virtual UClass* GetRigVMSchemaClass() const override { return URigVMSchema::StaticClass(); }
	virtual UScriptStruct* GetRigVMExecuteContextStruct() const override { return FRigVMExecuteContext::StaticStruct(); }
	virtual UClass* GetRigVMEdGraphClass() const override { return URigVMEdGraph::StaticClass(); }
	virtual UClass* GetRigVMEdGraphNodeClass() const override { return URigVMEdGraphNode::StaticClass(); }
	virtual UClass* GetRigVMEdGraphSchemaClass() const override { return URigVMEdGraphSchema::StaticClass(); }
	virtual UClass* GetRigVMEditorSettingsClass() const override { return URigVMEditorSettings::StaticClass(); }
	virtual UObject* GetEditorObjectForRigVMGraph(const URigVMGraph* InVMGraph) const override { return IRigVMAssetInterface::GetEditorObjectForRigVMGraph(InVMGraph); }
	virtual URigVMGraph* GetRigVMGraphForEditorObject(UObject* InObject) const override { return IRigVMAssetInterface::GetRigVMGraphForEditorObject(InObject); }
	virtual void HandleRigVMGraphAdded(const FRigVMClient* InClient, const FString& InNodePath) override { return IRigVMAssetInterface::HandleRigVMGraphAdded(InClient, InNodePath); }
	virtual void HandleRigVMGraphRemoved(const FRigVMClient* InClient, const FString& InNodePath) override { return IRigVMAssetInterface::HandleRigVMGraphRemoved(InClient, InNodePath); }
	virtual void HandleRigVMGraphRenamed(const FRigVMClient* InClient, const FString& InOldNodePath, const FString& InNewNodePath) override { return IRigVMAssetInterface::HandleRigVMGraphRenamed(InClient, InOldNodePath, InNewNodePath); }
	virtual void HandleConfigureRigVMController(const FRigVMClient* InClient, URigVMController* InControllerToConfigure) override { return IRigVMAssetInterface::HandleConfigureRigVMController(InClient, InControllerToConfigure); }
	virtual void IncrementVMRecompileBracket() override { return IRigVMAssetInterface::IncrementVMRecompileBracket(); }
	virtual void DecrementVMRecompileBracket() override { return IRigVMAssetInterface::DecrementVMRecompileBracket(); }
	virtual void RefreshAllModels(ERigVMLoadType InLoadType = ERigVMLoadType::PostLoad) override { return IRigVMAssetInterface::RefreshAllModels(InLoadType); }
	virtual void OnRigVMRegistryChanged() override { return IRigVMAssetInterface::OnRigVMRegistryChanged(); }
	virtual URigVMGraph* GetModel(const FString& InNodePath) const override { return IRigVMAssetInterface::GetModel(InNodePath); }
	virtual FRigVMGetFocusedGraph& OnGetFocusedGraph() override { return IRigVMAssetInterface::OnGetFocusedGraph(); }
	virtual const FRigVMGetFocusedGraph& OnGetFocusedGraph() const override { return IRigVMAssetInterface::OnGetFocusedGraph(); }
	virtual void SetupPinRedirectorsForBackwardsCompatibility() override { return IRigVMAssetInterface::SetupPinRedirectorsForBackwardsCompatibility(); }
	virtual FRigVMGraphModifiedEvent& OnModified() override { return IRigVMAssetInterface::OnModified(); }
	virtual bool IsFunctionPublic(const FName& InFunctionName) const override { return IRigVMAssetInterface::IsFunctionPublic(InFunctionName); }
	virtual void MarkFunctionPublic(const FName& InFunctionName, bool bIsPublic = true) override { return IRigVMAssetInterface::MarkFunctionPublic(InFunctionName, bIsPublic); }
	virtual void RenameGraph(const FString& InNodePath, const FName& InNewName) override { return IRigVMAssetInterface::RenameGraph(InNodePath, InNewName); }
	//  --- IRigVMClientHost interface End---
	
	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual void RecompileVM() override { IRigVMAssetInterface::RecompileVM(); }
	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual void RecompileVMIfRequired() override { return IRigVMAssetInterface::RecompileVMIfRequired(); }
	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual void RequestAutoVMRecompilation() override { return IRigVMAssetInterface::RequestAutoVMRecompilation(); }
	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual void SetAutoVMRecompile(bool bAutoRecompile) override { return IRigVMAssetInterface::SetAutoVMRecompile(bAutoRecompile); }
	UFUNCTION(BlueprintPure, Category = "RigVM Blueprint")
	virtual bool GetAutoVMRecompile() const override { return IRigVMAssetInterface::GetAutoVMRecompile(); }

	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual void RequestRigVMInit() override { IRigVMAssetInterface::RequestRigVMInit(); }

	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual URigVMGraph* GetModel(const UEdGraph* InEdGraph = nullptr) const override { return IRigVMAssetInterface::GetModel(InEdGraph); }

	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual URigVMGraph* GetDefaultModel() const override { return IRigVMAssetInterface::GetDefaultModel(); }

	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual TArray<URigVMGraph*> GetAllModels() const override { return IRigVMAssetInterface::GetAllModels(); }

	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual URigVMFunctionLibrary* GetLocalFunctionLibrary() const override { return IRigVMAssetInterface::GetLocalFunctionLibrary(); }

	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual URigVMFunctionLibrary* GetOrCreateLocalFunctionLibrary(bool bSetupUndoRedo = true) override { return IRigVMAssetInterface::GetOrCreateLocalFunctionLibrary(bSetupUndoRedo); }

	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual URigVMGraph* AddModel(FString InName = TEXT("Rig Graph"), bool bSetupUndoRedo = true, bool bPrintPythonCommand = true) override { return IRigVMAssetInterface::AddModel(InName, bSetupUndoRedo, bPrintPythonCommand); }

	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual bool RemoveModel(FString InName = TEXT("Rig Graph"), bool bSetupUndoRedo = true, bool bPrintPythonCommand = true) override { return IRigVMAssetInterface::RemoveModel(InName, bSetupUndoRedo, bPrintPythonCommand); }


	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual URigVMGraph* GetFocusedModel() const override { return IRigVMAssetInterface::GetFocusedModel(); }

	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual URigVMController* GetController(const URigVMGraph* InGraph = nullptr) const override { return IRigVMAssetInterface::GetController(InGraph); }
	virtual URigVMController* GetController(const UEdGraph* InGraph) const override { return IRigVMAssetInterface::GetController(InGraph); }

	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual URigVMController* GetControllerByName(const FString InGraphName = TEXT("")) const override { return IRigVMAssetInterface::GetControllerByName(InGraphName); }

	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual URigVMController* GetOrCreateController(URigVMGraph* InGraph = nullptr) override { return IRigVMAssetInterface::GetOrCreateController(InGraph); }
	virtual URigVMController* GetOrCreateController(const UEdGraph* InGraph) override { return IRigVMAssetInterface::GetOrCreateController(InGraph); }

	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual TArray<FString> GeneratePythonCommands(const FString InNewBlueprintName) override { return IRigVMAssetInterface::GeneratePythonCommands(InNewBlueprintName); }
#endif

	virtual TArray<FRigVMExternalDependency> GetExternalDependenciesForCategory(const FName& InCategory) const override { return IRigVMAssetInterface::GetExternalDependenciesForCategory(InCategory); }


	virtual bool ShouldBeMarkedDirtyUponTransaction() const override { return false; }

	
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<URigVMEdGraph> FunctionLibraryEdGraph;
#endif


	UPROPERTY(EditAnywhere, Category = "User Interface")
	FRigVMEdGraphDisplaySettings RigGraphDisplaySettings;

	UPROPERTY(EditAnywhere, Category = "VM")
	FRigVMRuntimeSettings VMRuntimeSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VM")
	FRigVMCompileSettings VMCompileSettings;

	UPROPERTY(EditAnywhere, Category = "Python Log Settings")
	FRigVMPythonSettings PythonLogSettings;

	UPROPERTY()
	TMap<FString, FSoftObjectPath> UserDefinedStructGuidToPathName;

	UPROPERTY()
	TMap<FString, FSoftObjectPath> UserDefinedEnumToPathName;

	UPROPERTY(transient)
	TSet<TObjectPtr<UObject>> UserDefinedTypesInUse;

protected:

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<URigVMGraph> Model_DEPRECATED;

	UPROPERTY()
	TObjectPtr<URigVMFunctionLibrary> FunctionLibrary_DEPRECATED;
#endif

	UPROPERTY()
	FRigVMClient RigVMClient;

#if WITH_EDITORONLY_DATA

	UPROPERTY()
	bool ReferencedObjectPathsStored;

	UPROPERTY()
	TArray<FSoftObjectPath> ReferencedObjectPaths;

#endif

	/** Asset searchable information about exposed public functions on this rig */
	UPROPERTY(AssetRegistrySearchable)
	TArray<FRigVMGraphFunctionHeader> PublicGraphFunctions;

	/** Asset searchable information function references in this rig */
	UPROPERTY(AssetRegistrySearchable)
	TArray<FRigVMReferenceNodeData> FunctionReferenceNodeData;

#if WITH_EDITORONLY_DATA

	/** Variant information about this asset */
	UPROPERTY(EditAnywhere, AssetRegistrySearchable, Category = "Variant")
	FRigVMVariant AssetVariant;

#endif

public:

	UFUNCTION(BlueprintCallable, Category = "VM")
	virtual UClass* GetRigVMHostClass() const { return GeneratedClass; }

	UFUNCTION(BlueprintCallable, Category = "VM")
	virtual URigVMHost* CreateRigVMHost() { return IRigVMAssetInterface::CreateRigVMHostImpl(); }

	UFUNCTION(BlueprintCallable, Category = "VM")
	virtual URigVMHost* GetDebuggedRigVMHost() { return IRigVMAssetInterface::GetDebuggedRigVMHost(); }

	UFUNCTION(BlueprintCallable, Category = "VM")
	virtual TArray<UStruct*> GetAvailableRigVMStructs() const override { return IRigVMAssetInterface::GetAvailableRigVMStructs(); }

#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, Category = "Variables")
	virtual TArray<FRigVMGraphVariableDescription> GetMemberVariables() const { return GetAssetVariables(); }
	
	UFUNCTION(BlueprintCallable, Category = "Variables")
	virtual FName AddMemberVariable(const FName& InName, const FString& InCPPType, bool bIsPublic = false, bool bIsReadOnly = false, FString InDefaultValue = TEXT("")) override;

	UFUNCTION(BlueprintCallable, Category = "Variables")
	virtual bool RemoveMemberVariable(const FName& InName) override;

	UFUNCTION(BlueprintCallable, Category = "Variables")
	virtual bool BulkRemoveMemberVariables(const TArray<FName>& InNames) override;

	UFUNCTION(BlueprintCallable, Category = "Variables")
	virtual bool RenameMemberVariable(const FName& InOldName, const FName& InNewName) override;

	UFUNCTION(BlueprintCallable, Category = "Variables")
	virtual bool ChangeMemberVariableType(const FName& InName, const FString& InCPPType, bool bIsPublic = false, bool bIsReadOnly = false, FString InDefaultValue = TEXT("")) override;
	virtual bool ChangeMemberVariableType(const FName& InName, const FEdGraphPinType& InType) override;

	UFUNCTION(BlueprintPure, Category = "Variants", meta = (DisplayName = "GetAssetVariant", ScriptName = "GetAssetVariant"))
	virtual FRigVMVariant GetAssetVariantBP() const { return AssetVariant; }

	UFUNCTION(BlueprintPure, Category = "Variants")
	virtual FRigVMVariantRef GetAssetVariantRef() const override { return IRigVMAssetInterface::GetAssetVariantRefImpl(); }

	/** Resets the asset's guid to a new one and splits it from the former variant set */
	UFUNCTION(BlueprintCallable, Category = "Variants")
	virtual bool SplitAssetVariant() override { return IRigVMAssetInterface::SplitAssetVariantImpl(); }

	/** Merges the asset's guid with a provided one to join the variant set */
	UFUNCTION(BlueprintCallable, Category = "Variants")
	virtual bool JoinAssetVariant(const FGuid& InGuid) override { return IRigVMAssetInterface::JoinAssetVariantImpl(InGuid); }

	UFUNCTION(BlueprintPure, Category = "Variants")
	virtual TArray<FRigVMVariantRef> GetMatchingVariants() const override { return IRigVMAssetInterface::GetMatchingVariantsImpl(); }
#endif	// #if WITH_EDITOR

	
private:

	/** The event names this rigvm blueprint contains */
	UPROPERTY(AssetRegistrySearchable)
	TArray<FName> SupportedEventNames;

#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, Category = "RigVM Blueprint")
	virtual void SuspendNotifications(bool bSuspendNotifs) { return IRigVMAssetInterface::SuspendNotifications(bSuspendNotifs); }
#endif

protected:

#if WITH_EDITOR
	/** Our currently running rig vm instance */
	// Declaring these transient properties here instead of the IRigVMAssetInterface class in order to have them as UPROPERTY, to avoid having stale pointers when GC happens
	UPROPERTY(transient)
	TObjectPtr<URigVMHost> EditorHost = nullptr;
	virtual TObjectPtr<URigVMHost>& GetEditorHost() override { return EditorHost; }

	static FName FindHostMemberVariableUniqueName(TSharedPtr<FKismetNameValidator> InNameValidator, const FString& InBaseName);
	static int32 AddHostMemberVariable(URigVMBlueprint* InBlueprint, const FName& InVarName, FEdGraphPinType InVarType, bool bIsPublic, bool bIsReadOnly, FString InDefaultValue);
	virtual FName AddHostMemberVariableFromExternal(FRigVMExternalVariable InVariableToCreate, FString InDefaultValue = FString()) override;
public:

	virtual void OnPreVariableChange(UObject* InObject);
	virtual void OnPostVariableChange(UBlueprint* InBlueprint);
	bool bUpdatingExternalVariables;

	void OnBlueprintChanged(UBlueprint* InBlueprint);
	void OnSetObjectBeingDebuggedReceived(UObject* InObject);
#endif

protected:

	virtual void SetupDefaultObjectDuringCompilation(URigVMHost* InCDO) override { IRigVMAssetInterface::SetupDefaultObjectDuringCompilation(InCDO); }
	virtual void PreCompile() override;

	virtual FProperty* FindGeneratedPropertyByName(const FName& InName) const override;
	virtual bool SetVariableTooltip(const FName& InName, const FText& InTooltip) override;
	virtual FText GetVariableTooltip(const FName& InName) const override;
	virtual bool SetVariableCategory(const FName& InName, const FString& InCategory) override;
	virtual FString GetVariableCategory(const FName& InName) override;
	virtual FString GetVariableMetadataValue(const FName& InName, const FName& InKey) override;
	virtual bool SetVariableExposeOnSpawn(const FName& InName, const bool bInExposeOnSpawn) override;
	virtual bool SetVariableExposeToCinematics(const FName& InName, const bool bInExposeToCinematics) override;
	virtual bool SetVariablePrivate(const FName& InName, const bool bInPrivate) override;
	virtual bool SetVariablePublic(const FName& InName, const bool bIsPublic) override;
	//virtual bool ChangeAssetVariableType(const FName& InName, const FEdGraphPinType& InType) override;
	virtual FString OnCopyVariable(const FName& InName) const override;
	virtual bool OnPasteVariable(const FString& InText) override;
	
#if WITH_EDITOR

private:

	TArray<FBPVariableDescription> LastNewVariables;
#endif

	// friend class FRigVMTreePackageNode;
	// friend class UE::RigVM::Editor::Tools::FFilterByAssetTag;
	// friend class FRigVMEditorModule;
};

#undef UE_API
