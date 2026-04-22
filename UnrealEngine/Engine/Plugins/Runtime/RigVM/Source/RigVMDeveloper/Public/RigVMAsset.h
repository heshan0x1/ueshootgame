// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigVMCore/RigVM.h"
#include "RigVMHost.h"
#include "RigVMModel/RigVMClient.h"
#include "RigVMModel/RigVMExternalDependency.h"
#include "RigVMCompiler/RigVMCompiler.h"
#include "RigVMCore/RigVMGraphFunctionDefinition.h"
#include "RigVMSettings.h"
#include "Blueprint/BlueprintExtension.h"
#if WITH_EDITOR
#include "HAL/CriticalSection.h"
#include "Kismet2/CompilerResultsLog.h"
#endif

#include "RigVMAsset.generated.h"

#define UE_API RIGVMDEVELOPER_API

#if WITH_EDITOR
class URigVMEdGraph;
class IRigVMEditorModule;
namespace UE::RigVM::Editor::Tools
{
	class FFilterByAssetTag;
}
#endif
struct FEndLoadPackageContext;
struct FRigVMMemoryStorageStruct;
struct FGuardSkipDirtyBlueprintStatus;

class IRigVMAssetInterface;
typedef TScriptInterface<IRigVMAssetInterface> FRigVMAssetInterfacePtr;

DECLARE_EVENT_ThreeParams(IRigVMAssetInterface, FOnRigVMCompiledEvent, UObject*, URigVM*, FRigVMExtendedExecuteContext&);
DECLARE_EVENT_OneParam(IRigVMAssetInterface, FOnRigVMRefreshEditorEvent, FRigVMAssetInterfacePtr);
DECLARE_EVENT_FourParams(IRigVMAssetInterface, FOnRigVMVariableDroppedEvent, UObject*, FProperty*, const FVector2D&, const FVector2D&);
DECLARE_EVENT_OneParam(IRigVMAssetInterface, FOnRigVMExternalVariablesChanged, const TArray<FRigVMExternalVariable>&);
DECLARE_EVENT_TwoParams(IRigVMAssetInterface, FOnRigVMNodeDoubleClicked, FRigVMAssetInterfacePtr, URigVMNode*);
DECLARE_EVENT_OneParam(IRigVMAssetInterface, FOnRigVMGraphImported, UEdGraph*);
DECLARE_EVENT_OneParam(IRigVMAssetInterface, FOnRigVMPostEditChangeChainProperty, FPropertyChangedChainEvent&);
DECLARE_EVENT_FourParams(IRigVMAssetInterface, FOnRigVMLocalizeFunctionDialogRequested, FRigVMGraphFunctionIdentifier&, URigVMController*, IRigVMGraphFunctionHost*, bool);
DECLARE_EVENT_ThreeParams(IRigVMAssetInterface, FOnRigVMReportCompilerMessage, EMessageSeverity::Type, UObject*, const FString&);
DECLARE_DELEGATE_RetVal_FourParams(FRigVMController_BulkEditResult, FRigVMOnBulkEditDialogRequestedDelegate, FRigVMAssetInterfacePtr, URigVMController*, URigVMLibraryNode*, ERigVMControllerBulkEditType);
DECLARE_DELEGATE_RetVal_OneParam(bool, FRigVMOnBreakLinksDialogRequestedDelegate, TArray<URigVMLink*>);
DECLARE_DELEGATE_RetVal_OneParam(TRigVMTypeIndex, FRigVMOnPinTypeSelectionRequestedDelegate, const TArray<TRigVMTypeIndex>&);
DECLARE_EVENT(IRigVMAssetInterface, FOnRigVMBreakpointAdded);
DECLARE_EVENT_OneParam(IRigVMAssetInterface, FOnRigVMRequestInspectObject, const TArray<UObject*>& );
DECLARE_EVENT_OneParam(IRigVMAssetInterface, FOnRigVMRequestInspectMemoryStorage, const TArray<FRigVMMemoryStorageStruct*>&);
DECLARE_EVENT_OneParam( IRigVMAssetInterface, FOnRigVMAssetChangedEvent, class UObject * );
DECLARE_EVENT_OneParam(IRigVMAssetInterface, FOnRigVMSetObjectBeingDebugged, UObject* /*InDebugObj*/);

USTRUCT()
struct UE_API FRigVMPythonSettings
{
	GENERATED_BODY();

	FRigVMPythonSettings()
	{
	}
};

UENUM(BlueprintType)
enum class ERigVMTagDisplayMode : uint8
{
	None = 0,
	All = 0x001,
	DeprecationOnly = 0x002,
	Last = DeprecationOnly UMETA(Hidden), 
};

USTRUCT()
struct UE_API FRigVMEdGraphDisplaySettings
{
	GENERATED_BODY();

	FRigVMEdGraphDisplaySettings();
	~FRigVMEdGraphDisplaySettings();

	// When enabled shows the first node instruction index
	// matching the execution stack window.
	UPROPERTY(EditAnywhere, Category = "Graph Display Settings")
	bool bShowNodeInstructionIndex;

	// When enabled shows the node counts both in the graph view as
	// we as in the execution stack window.
	// The number on each node represents how often the node has been run.
	// Keep in mind when looking at nodes in a function the count
	// represents the sum of all counts for each node based on all
	// references of the function currently running.
	UPROPERTY(EditAnywhere, Category = "Graph Display Settings")
	bool bShowNodeRunCounts;

	// A lower limit for counts for nodes used for debugging.
	// Any node lower than this count won't show the run count.
	UPROPERTY(EditAnywhere, Category = "Graph Display Settings")
	int32 NodeRunLowerBound;

	// A upper limit for counts for nodes used for debugging.
	// If a node reaches this count a warning will be issued for the
	// node and displayed both in the execution stack as well as in the
	// graph. Setting this to <= 1 disables the warning.
	// Note: The count limit doesn't apply to functions / collapse nodes.
	UPROPERTY(EditAnywhere, Category = "Graph Display Settings")
	int32 NodeRunLimit;

	// The duration in microseconds of the fastest instruction / node
	UPROPERTY(EditAnywhere, Category = "Graph Display Settings", transient, meta = (EditCondition = "!bAutoDetermineRange"))
	double MinMicroSeconds;

	// The duration in microseconds of the slowest instruction / node
	UPROPERTY(EditAnywhere, Category = "Graph Display Settings", transient, meta = (EditCondition = "!bAutoDetermineRange"))
	double MaxMicroSeconds;

	// The total duration of the last execution of the rig
	UPROPERTY(VisibleAnywhere, Category = "Graph Display Settings", transient)
	double TotalMicroSeconds;

	// If you set this to more than 1 the results will be averaged across multiple frames
	UPROPERTY(EditAnywhere, Category = "Graph Display Settings", meta = (UIMin=1, UIMax=256))
	int32 AverageFrames;

	TArray<double> MinMicroSecondsFrames;
	TArray<double> MaxMicroSecondsFrames;
	TArray<double> TotalMicroSecondsFrames;

	UPROPERTY(EditAnywhere, Category = "Graph Display Settings")
	bool bAutoDetermineRange;

	UPROPERTY(transient)
	double LastMinMicroSeconds;

	UPROPERTY(transient)
	double LastMaxMicroSeconds;

	// The color of the fastest instruction / node
	UPROPERTY(EditAnywhere, Category = "Graph Display Settings")
	FLinearColor MinDurationColor;

	// The color of the slowest instruction / node
	UPROPERTY(EditAnywhere, Category = "Graph Display Settings")
	FLinearColor MaxDurationColor;

	// The color of the slowest instruction / node
	UPROPERTY(EditAnywhere, Category = "Graph Display Settings")
	ERigVMTagDisplayMode TagDisplayMode;

	void SetTotalMicroSeconds(double InTotalMicroSeconds);
	void SetLastMinMicroSeconds(double InMinMicroSeconds);
	void SetLastMaxMicroSeconds(double InMaxMicroSeconds);
	double AggregateAverage(TArray<double>& InFrames, double InPrevious, double InNext) const;
};

/**
 * Enumerates states a RigVMAsset can be in.
 * Copied from EBlueprintStatus.
 */
UENUM()
enum ERigVMAssetStatus : int
{
	/** Asset is in an unknown state. */
	RVMA_Unknown,
	/** Asset has been modified but not recompiled. */
	RVMA_Dirty,
	/** Asset tried but failed to be compiled. */
	RVMA_Error,
	/** Asset has been compiled since it was last modified. */
	RVMA_UpToDate,
	/** Asset is in the process of being created for the first time. */
	RVMA_BeingCreated,
	/** Asset has been compiled since it was last modified. There are warnings. */
	RVMA_UpToDateWithWarnings,
	RVMA_MAX,
};

UINTERFACE(BlueprintType)
class UE_API URigVMAssetInterface : public UInterface
{
	GENERATED_BODY()
};

class UE_API IRigVMAssetInterface 
{
	GENERATED_BODY()

#if WITH_EDITOR
public:
	static FRigVMAssetInterfacePtr GetInterfaceOuter(const UObject* InObject);

	virtual UObject* GetObject() = 0;
	const UObject* GetObject() const { return const_cast<IRigVMAssetInterface*>(this)->GetObject(); }
	virtual void Clear();

	TScriptInterface<IRigVMClientHost> GetRigVMClientHost();
	const TScriptInterface<IRigVMClientHost> GetRigVMClientHost() const { return const_cast<IRigVMAssetInterface*>(this)->GetRigVMClientHost(); }

	FRigVMClient* GetRigVMClient() { return GetRigVMClientHost()->GetRigVMClient(); }
	const FRigVMClient* GetRigVMClient() const { return GetRigVMClientHost()->GetRigVMClient(); }
	void RequestClientHostRigVMInit();
	
	static inline const FLazyName RigVMPanelNodeFactoryName = FLazyName(TEXT("FRigVMEdGraphPanelNodeFactory"));
	static inline const FLazyName RigVMPanelPinFactoryName = FLazyName(TEXT("FRigVMEdGraphPanelPinFactory"));

	virtual void GetAllEdGraphs(TArray<UEdGraph*>& Graphs) const = 0;
	virtual TArray<FRigVMGraphVariableDescription> GetAssetVariables() const = 0;
	virtual bool IsRegeneratingOnLoad() const = 0;
	virtual FProperty* FindGeneratedPropertyByName(const FName& InName) const = 0; 
	virtual FName AddHostMemberVariableFromExternal(FRigVMExternalVariable InVariableToCreate, FString InDefaultValue = FString()) = 0;
	virtual FName AddMemberVariable(const FName& InName, const FString& InCPPType, bool bIsPublic = false, bool bIsReadOnly = false, FString InDefaultValue = TEXT("")) = 0;
	virtual bool RemoveMemberVariable(const FName& InName) = 0;
	virtual bool BulkRemoveMemberVariables(const TArray<FName>& InNames) = 0;
	virtual bool RenameMemberVariable(const FName& InOldName, const FName& InNewName) = 0;
	virtual bool ChangeMemberVariableType(const FName& InName, const FString& InCPPType, bool bIsPublic = false, bool bIsReadOnly = false, FString InDefaultValue = TEXT("")) = 0;
	virtual bool ChangeMemberVariableType(const FName& InName, const FEdGraphPinType& InType) = 0;
	virtual FText GetVariableTooltip(const FName& InName) const = 0;
	virtual FString GetVariableCategory(const FName& InName) = 0;
	virtual FString GetVariableMetadataValue(const FName& InName, const FName& InKey) = 0;
	virtual bool SetVariableCategory(const FName& InName, const FString& InCategory) = 0;
	virtual bool SetVariableTooltip(const FName& InName, const FText& InTooltip) = 0; 
	virtual bool SetVariableExposeOnSpawn(const FName& InName, const bool bInExposeOnSpawn) = 0; 
	virtual bool SetVariableExposeToCinematics(const FName& InName, const bool bInExposeToCinematics) = 0; 
	virtual bool SetVariablePrivate(const FName& InName, const bool bInPrivate) = 0; 
	virtual bool SetVariablePublic(const FName& InName, const bool bIsPublic) = 0; 
	virtual FString OnCopyVariable(const FName& InName) const = 0;
	virtual bool OnPasteVariable(const FString& InText) = 0;
	virtual UObject* GetObjectBeingDebugged() = 0;
	virtual UObject* GetObjectBeingDebugged() const = 0;
	virtual const FString& GetObjectBeingDebuggedPath() const = 0;
	virtual TArray<UObject*> GetArchetypeInstances(bool bIncludeCDO, bool bIncludeDerivedClass) const = 0;
	virtual UWorld* GetWorldBeingDebugged() const = 0;
	virtual void SetWorldBeingDebugged(UWorld* NewWorld) = 0;
	virtual ERigVMAssetStatus GetAssetStatus() const = 0;
	virtual bool IsUpToDate() const = 0;
	virtual UClass* GetRigVMGeneratedClass() = 0;
	virtual const UClass* GetRigVMGeneratedClass() const { return const_cast<IRigVMAssetInterface*>(this)->GetRigVMGeneratedClass(); }
	virtual URigVM* GetVM(bool bCreateIfNeeded) const = 0;
	
	virtual bool ExportGraphToText(UEdGraph* InEdGraph, FString& OutText);
	virtual bool TryImportGraphFromText(const FString& InClipboardText, UEdGraph** OutGraphPtr = nullptr);
	virtual bool CanImportGraphFromText(const FString& InClipboardText);

	virtual void RefreshAllNodes() ;
	virtual void IncrementVMRecompileBracket();
	virtual void DecrementVMRecompileBracket();

	virtual void SetObjectBeingDebugged(UObject* NewObject);
	virtual FOnRigVMSetObjectBeingDebugged& OnSetObjectBeingDebugged()  { return SetObjectBeingDebuggedEvent; }

	/** Returns the editor module to be used for this blueprint */
	virtual IRigVMEditorModule* GetEditorModule() const;

	/** Returns true if a given panel node factory is compatible this blueprint */
	virtual const FLazyName& GetPanelNodeFactoryName() const;

	virtual FRigVMRuntimeSettings& GetVMRuntimeSettings() = 0;
	virtual const FRigVMRuntimeSettings& GetVMRuntimeSettings() const { return const_cast<IRigVMAssetInterface*>(this)->GetVMRuntimeSettings();};
	
	
	static void QueueCompilerMessageDelegate(const FOnRigVMReportCompilerMessage::FDelegate& InDelegate);
	static void ClearQueuedCompilerMessageDelegates();

	virtual FRigVMGraphModifiedEvent& OnModified();
	FOnRigVMCompiledEvent& OnVMCompiled();

	virtual FOnRigVMReportCompilerMessage& OnReportCompilerMessage()  { return ReportCompilerMessageEvent; }

	/** Returns true if a given panel pin factory is compatible this blueprint */
	virtual const FLazyName& GetPanelPinFactoryName() const;

	virtual void AddPinWatch(UEdGraphPin* InPin) = 0;
	virtual bool IsPinBeingWatched(const UEdGraphPin* InPin) const = 0;

	TMap<FString, FRigVMOperand> PinToOperandMap;

	/**
	 * Sets the instruction index to exit early on 
	 */
	bool SetEarlyExitInstruction(URigVMNode* InNodeToExitEarlyAfter, int32 InInstruction = INDEX_NONE, bool bRequestHyperLink = false);

	/**
	 * Resets / removes the instruction index to exit early on 
	 */
	bool ResetEarlyExitInstruction(bool bResetCallstack = true);

	/**
	 * Toggles the preview here functionality based on the selection
	 */
	void TogglePreviewHere(const URigVMGraph* InGraph);

	/**
	 * Steps forward in a preview here session
	 */
	void PreviewHereStepForward();

	/**
	 * Returns true if we can step forward in a preview here session
	 */
	bool CanPreviewHereStepForward() const;

	/**
	 * Returns the instruction index of the currently selected node
	 */
	int32 GetPreviewNodeInstructionIndexFromSelection(const URigVMGraph* InGraph) const;

	virtual FRigVMController_RequestJumpToHyperlinkDelegate& OnRequestJumpToHyperlink()  { return RequestJumpToHyperlink; };

	/** Returns the settings defaults for this blueprint */
	URigVMEditorSettings* GetRigVMEditorSettings() const;

	virtual UEdGraph* GetEdGraph(const URigVMGraph* InModel) const;
	virtual UEdGraph* GetEdGraph(const FString& InNodePath) const;
	
	void AddVariableSearchMetaDataInfo(const FName InVariableName, TArray<UBlueprintExtension::FSearchTagDataPair>& OutTaggedMetaData) const;

	virtual FRigVMEdGraphDisplaySettings& GetRigGraphDisplaySettings() = 0;
	virtual const FRigVMEdGraphDisplaySettings& GetRigGraphDisplaySettings() const { return const_cast<IRigVMAssetInterface*>(this)->GetRigGraphDisplaySettings(); };
	virtual FRigVMCompileSettings& GetVMCompileSettings() = 0;
	virtual const FRigVMCompileSettings& GetVMCompileSettings() const { return const_cast<IRigVMAssetInterface*>(this)->GetVMCompileSettings(); };
	

	virtual URigVMHost* GetDebuggedRigVMHost();

	virtual URigVMGraph* GetFocusedModel() const;
	virtual TArray<URigVMGraph*> GetAllModels() const;
	virtual URigVMFunctionLibrary* GetLocalFunctionLibrary() const;
	virtual URigVMGraph* GetModel(const UEdGraph* InEdGraph = nullptr) const;
	virtual URigVMGraph* GetModel(const FString& InNodePath) const;
	virtual URigVMController* GetControllerByName(const FString InGraphName = TEXT("")) const;
	virtual URigVMController* GetOrCreateController(URigVMGraph* InGraph = nullptr);
	virtual URigVMController* GetController(const UEdGraph* InEdGraph) const;
	virtual URigVMController* GetOrCreateController(const UEdGraph* InGraph);
	virtual URigVMController* GetController(const URigVMGraph* InGraph = nullptr) const;

	virtual FOnRigVMLocalizeFunctionDialogRequested& OnRequestLocalizeFunctionDialog()  { return RequestLocalizeFunctionDialog; }
	void BroadcastRequestLocalizeFunctionDialog(FRigVMGraphFunctionIdentifier InFunction, bool bForce = false);

	const FCompilerResultsLog& GetCompileLog() const;
	FCompilerResultsLog& GetCompileLog();
	void HandleReportFromCompiler(EMessageSeverity::Type InSeverity, UObject* InSubject, const FString& InMessage);

	// Returns a list of dependencies of this blueprint.
	// Dependencies are blueprints that contain functions used in this blueprint
	TArray<IRigVMAssetInterface*> GetDependencies(bool bRecursive = false) const;

	// Returns a list of dependents as unresolved soft object pointers.
	// A dependent is a blueprint which uses a function defined in this blueprint.
	// This function is not recursive, since it avoids opening the asset.
	// Use GetDependentBlueprints as an alternative.
	TArray<FAssetData> GetDependentAssets() const;

	// Returns a list of dependents as resolved blueprints.
	// A dependent is a blueprint which uses a function defined in this blueprint.
	// If bOnlyLoaded is false, this function loads the dependent assets and can introduce a large cost
	// depending on the size / count of assets in the project.
	TArray<IRigVMAssetInterface*> GetDependentResolvedAssets(bool bRecursive = false, bool bOnlyLoaded = false) const;

	void BroadcastRefreshEditor();
	virtual FOnRigVMRefreshEditorEvent& OnRefreshEditor()  { return RefreshEditorEvent; }

	FOnRigVMRequestInspectMemoryStorage& OnRequestInspectMemoryStorage();
	void RequestInspectMemoryStorage(const TArray<FRigVMMemoryStorageStruct*>& InMemoryStorageStructs) const;

	FOnRigVMPostEditChangeChainProperty& OnPostEditChangeChainProperty() { return PostEditChangeChainPropertyEvent; }
	void BroadcastPostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedChainEvent);
	virtual void MarkAssetAsStructurallyModified(bool bSkipDirtyAssetStatus = false) = 0;
	virtual void MarkAssetAsModified(FPropertyChangedEvent PropertyChangedEvent = FPropertyChangedEvent(nullptr)) = 0;

	virtual URigVMGraph* GetDefaultModel() const;
	virtual TArray<TObjectPtr<UEdGraph>>& GetUberGraphs() = 0;
	virtual const TArray<TObjectPtr<UEdGraph>>& GetUberGraphs() const { return const_cast<IRigVMAssetInterface*>(this)->GetUberGraphs(); };

	/** Enables or disables profiling for this asset */
	void SetProfilingEnabled(const bool bEnabled);

protected:
	IRigVMAssetInterface() {}
	IRigVMAssetInterface(const FObjectInitializer& ObjectInitializer);

	virtual FRigVMVariant& GetAssetVariant() = 0;
	virtual const FRigVMVariant& GetAssetVariant() const { return const_cast<IRigVMAssetInterface*>(this)->GetAssetVariant(); }
	virtual FRigVMVariantRef GetAssetVariantRef() const = 0;
	virtual TArray<FRigVMGraphFunctionHeader>& GetPublicGraphFunctions() = 0;
	
	virtual void RemovePinWatch(UEdGraphPin* InPin) = 0;
	virtual void ClearPinWatches() = 0;
	virtual void ForeachPinWatch(TFunctionRef<void(UEdGraphPin*)> Task) = 0;
	
	virtual void SetAssetStatus(const ERigVMAssetStatus& InStatus) = 0;	
	
	virtual UClass* GetRigVMGeneratedClassPrototype() const = 0;
		virtual TArray<struct FEditedDocumentInfo>& GetLastEditedDocuments() = 0;
	virtual UObject* GetDefaultsObject() = 0;
	virtual void SetObjectBeingDebuggedSuper(UObject* NewObject) = 0;
	virtual void SetUberGraphs(const TArray<TObjectPtr<UEdGraph>>& InGraphs) = 0;
	virtual bool MarkPackageDirty() = 0;
	virtual UPackage* GetPackage() = 0;
	virtual void PostTransactedSuper(const FTransactionObjectEvent& TransactionEvent) = 0;
	virtual void ReplaceDeprecatedNodesSuper() = 0;
	virtual void PreDuplicateSuper(FObjectDuplicationParameters& DupParams) = 0;
	virtual void PostDuplicateSuper(bool bDuplicateForPIE) = 0;
	virtual void GetAssetRegistryTagsSuper(FAssetRegistryTagsContext Context) const = 0;
	virtual bool RequiresForceLoadMembersSuper(UObject* InObject) const = 0;
	virtual TArray<FRigVMExternalVariable> GetExternalVariables(bool bFallbackToBlueprint) const = 0;
	virtual FString GetVariableDefaultValue(const FName& InName, bool bFromDebuggedObject) const = 0;
	virtual FRigVMExtendedExecuteContext* GetRigVMExtendedExecuteContext() = 0;
	virtual void PreCompile() = 0;
	virtual TMap<FString, FSoftObjectPath>& GetUserDefinedStructGuidToPathName(bool bFromCDO) = 0;
	virtual const TMap<FString, FSoftObjectPath>& GetUserDefinedStructGuidToPathName(bool bFromCDO) const {return const_cast<IRigVMAssetInterface*>(this)->GetUserDefinedStructGuidToPathName(bFromCDO);}
	virtual TMap<FString, FSoftObjectPath>& GetUserDefinedEnumToPathName(bool bFromCDO) = 0;
	virtual const TMap<FString, FSoftObjectPath>& GetUserDefinedEnumToPathName(bool bFromCDO) const {return const_cast<IRigVMAssetInterface*>(this)->GetUserDefinedEnumToPathName(bFromCDO);}
	virtual TSet<TObjectPtr<UObject>>& GetUserDefinedTypesInUse(bool bFromCDO) = 0;
	virtual FCompilerResultsLog* GetCurrentMessageLog() const = 0;
	virtual FRigVMDebugInfo& GetDebugInfo() = 0;
	virtual TObjectPtr<URigVMEdGraph>& GetFunctionLibraryEdGraph() = 0;
	virtual const TObjectPtr<URigVMEdGraph>& GetFunctionLibraryEdGraph() const { return const_cast<IRigVMAssetInterface*>(this)->GetFunctionLibraryEdGraph(); }
	virtual TArray<TObjectPtr<UEdGraph>>& GetFunctionGraphs() = 0;
	virtual const TArray<TObjectPtr<UEdGraph>>& GetFunctionGraphs() const { return const_cast<IRigVMAssetInterface*>(this)->GetFunctionGraphs();};
	virtual bool& IsReferencedObjectPathsStored() = 0;
	virtual void SetReferencedObjectPathsStored(bool bValue) { IsReferencedObjectPathsStored() = bValue; }
	virtual TArray<FSoftObjectPath>& GetReferencedObjectPaths() = 0;
	virtual TArray<FName> GetSupportedEventNames() = 0;
	virtual void UpdateSupportedEventNames() = 0;
	virtual TArray<FRigVMReferenceNodeData>& GetFunctionReferenceNodeData() = 0;
	virtual void NotifyGraphRenamedSuper(class UEdGraph* Graph, FName OldName, FName NewName) = 0;
	virtual URigVMHost* CreateRigVMHostSuper(UObject* InOuter) = 0;
	virtual void AddUbergraphPage(URigVMEdGraph* RigVMEdGraph) = 0;
	virtual void AddLastEditedDocument(URigVMEdGraph* RigVMEdGraph) = 0;
	virtual void Compile() = 0;
	virtual FCompilerResultsLog CompileBlueprint() = 0;
	virtual void PostEditChangeBlueprintActors() = 0;
	virtual bool SplitAssetVariant() = 0;
	virtual bool JoinAssetVariant(const FGuid& InGuid) = 0;
	virtual TArray<FRigVMVariantRef> GetMatchingVariants() const = 0;
	virtual void SerializeSuper(FArchive& Ar) = 0;
	virtual void PostSerialize(FArchive& Ar) = 0;


	// Interface to URigVMExternalDependencyManager
	void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const FRigVMClient* InClient) const;
	void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const FRigVMGraphFunctionStore* InFunctionStore) const;
	void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const FRigVMGraphFunctionData* InFunction) const;
	void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const FRigVMGraphFunctionHeader* InHeader) const;
	void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const FRigVMFunctionCompilationData* InCompilationData) const;
	void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const URigVMGraph* InGraph) const;
	void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const URigVMNode* InNode) const;
	void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const URigVMPin* InPin) const;
	void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const UStruct* InStruct) const;
	void CollectExternalDependencies(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const UEnum* InEnum) const;
	void CollectExternalDependenciesForCPPTypeObject(TArray<FRigVMExternalDependency>& OutDependencies, const FName& InCategory, const UObject* InObject) const;
	// end of Interface to URigVMExternalDependencyManager

	void CommonInitialization(const FObjectInitializer& ObjectInitializer);
	
	void InitializeModelIfRequired(bool bRecompileVM = true);


	void SerializeImpl(FArchive& Ar);


	
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext);
	virtual void PostLoad();
	static void DeclareConstructClasses(TArray<FTopLevelAssetPath>& OutConstructClasses, const UClass* SpecificSubclass);
	virtual bool IsPostLoadThreadSafe() const { return false; }
	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent);
	virtual void ReplaceDeprecatedNodes();
	virtual void PreDuplicate(FObjectDuplicationParameters& DupParams);
	virtual void PostDuplicate(bool bDuplicateForPIE);
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const;
	
	
	virtual bool RequiresForceLoadMembers(UObject* InObject) const;

	// UObject interface
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent);
	virtual void PostRename(UObject* OldOuter, const FName OldName);
	/** Called during cooking. Must return all objects that will be Preload()ed when this is serialized at load time. */
	virtual void GetPreloadDependencies(TArray<UObject*>& OutDeps);

	//  --- IRigVMClientHost interface Start---
	virtual UObject* GetEditorObjectForRigVMGraph(const URigVMGraph* InVMGraph) const;
	virtual URigVMGraph* GetRigVMGraphForEditorObject(UObject* InObject) const;
	virtual void HandleRigVMGraphAdded(const FRigVMClient* InClient, const FString& InNodePath);
	virtual void HandleRigVMGraphRemoved(const FRigVMClient* InClient, const FString& InNodePath);
	virtual void HandleRigVMGraphRenamed(const FRigVMClient* InClient, const FString& InOldNodePath, const FString& InNewNodePath);
	virtual void HandleConfigureRigVMController(const FRigVMClient* InClient, URigVMController* InControllerToConfigure);
	virtual UObject* ResolveUserDefinedTypeById(const FString& InTypeName) const;


	void GenerateUserDefinedDependenciesData(FRigVMExtendedExecuteContext& Context);
	virtual void RecompileVM() ;
	virtual void RecompileVMIfRequired() ;
	virtual void RequestAutoVMRecompilation() ;
	virtual void SetAutoVMRecompile(bool bAutoRecompile)  { bAutoRecompileVM = bAutoRecompile; }
	virtual bool GetAutoVMRecompile() const  { return bAutoRecompileVM; }

	

	// this is needed since even after load
	// model data can change while the RigVM BP is not opened
	// for example, if a user defined struct changed after BP load,
	// any pin that references the struct needs to be regenerated
	virtual void RefreshAllModels(ERigVMLoadType InLoadType = ERigVMLoadType::PostLoad);

	// RigVMRegistry changes can be triggered when new user defined types(structs/enums) are added/removed
	// in which case we have to refresh the model
	virtual void OnRigVMRegistryChanged();

	void RequestRigVMInit() const;
	

	virtual URigVMFunctionLibrary* GetOrCreateLocalFunctionLibrary(bool bSetupUndoRedo = true);

	virtual URigVMGraph* AddModel(FString InName = TEXT("Rig Graph"), bool bSetupUndoRedo = true, bool bPrintPythonCommand = true);

	virtual bool RemoveModel(FString InName = TEXT("Rig Graph"), bool bSetupUndoRedo = true, bool bPrintPythonCommand = true);

	virtual FRigVMGetFocusedGraph& OnGetFocusedGraph();
	virtual const FRigVMGetFocusedGraph& OnGetFocusedGraph() const;


	virtual TArray<FString> GeneratePythonCommands(const FString InNewBlueprintName);

	virtual void SetupPinRedirectorsForBackwardsCompatibility() {};

	

	virtual bool IsFunctionPublic(const FName& InFunctionName) const;
	virtual void MarkFunctionPublic(const FName& InFunctionName, bool bIsPublic = true);

	virtual void RenameGraph(const FString& InNodePath, const FName& InNewName);

	//  --- IRigVMClientHost interface End ---

	//  --- IRigVMExternalDependencyManager interface Start ---

	virtual TArray<FRigVMExternalDependency> GetExternalDependenciesForCategory(const FName& InCategory) const;
	
	//  --- IRigVMExternalDependencyManager interface End ---


	virtual FOnRigVMRequestInspectObject& OnRequestInspectObject() { return OnRequestInspectObjectEvent; }
	void RequestInspectObject(const TArray<UObject*>& InObjects) { OnRequestInspectObjectEvent.Broadcast(InObjects); }

	

	URigVMGraph* GetTemplateModel(bool bIsFunctionLibrary = false);
	URigVMController* GetTemplateController(bool bIsFunctionLibrary = false);


	

	

	TObjectPtr<URigVMGraph> TemplateModel;
	TObjectPtr<URigVMController> TemplateController;
	mutable TArray<UObject::FAssetRegistryTag> CachedAssetTags;
	TArray<TPair<TWeakObjectPtr<URigVMNode>,int32>> LastPreviewHereNodes;
	

	bool bSuspendModelNotificationsForSelf;
	bool bSuspendAllNotifications;

	void RebuildGraphFromModel();

	URigVMHost* CreateRigVMHostImpl();
	

	virtual TArray<UStruct*> GetAvailableRigVMStructs() const;

	FRigVMVariantRef GetAssetVariantRefImpl() const;

	/** Resets the asset's guid to a new one and splits it from the former variant set */
	bool SplitAssetVariantImpl();

	/** Merges the asset's guid with a provided one to join the variant set */
	bool JoinAssetVariantImpl(const FGuid& InGuid);

	TArray<FRigVMVariantRef> GetMatchingVariantsImpl() const;

	

	bool bAutoRecompileVM;

	bool bVMRecompilationRequired;

	bool bIsCompiling;

	int32 VMRecompilationBracket;
	
	bool bSkipDirtyBlueprintStatus;

	FRigVMGraphModifiedEvent ModifiedEvent;
	void Notify(ERigVMGraphNotifType InNotifType, UObject* InSubject);
	void HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject);

	FOnRigVMAssetChangedEvent ChangedEvent;
	virtual FOnRigVMAssetChangedEvent& OnChanged()  { return ChangedEvent; }

	FOnRigVMSetObjectBeingDebugged SetObjectBeingDebuggedEvent;
	
	
	void ReplaceFunctionIdentifiers(const FString& OldAssetPath, const FString& NewAssetPath);

	FOnRigVMRefreshEditorEvent RefreshEditorEvent;
	FOnRigVMVariableDroppedEvent VariableDroppedEvent;

	void SuspendNotifications(bool bSuspendNotifs);
	
	virtual FOnRigVMVariableDroppedEvent& OnVariableDropped()  { return VariableDroppedEvent; }


	FOnRigVMCompiledEvent VMCompiledEvent;

	virtual void PathDomainSpecificContentOnLoad() {}
	virtual void PatchBoundVariables();
	virtual void PatchVariableNodesWithIncorrectType();
	virtual void PatchParameterNodesOnLoad() {}
	virtual void PatchLinksWithCast();
	virtual void GetBackwardsCompatibilityPublicFunctions(TArray<FName> &BackwardsCompatiblePublicFunctions, TMap<URigVMLibraryNode*, FRigVMGraphFunctionHeader>& OldHeaders);

	virtual void CreateMemberVariablesOnLoad();
	virtual void PatchVariableNodesOnLoad();
	TMap<FName, int32> AddedMemberVariableMap;

	void PropagateRuntimeSettingsFromBPToInstances();
	void InitializeArchetypeInstances();

	void HandlePackageDone(const FEndLoadPackageContext& Context);

	virtual void HandlePackageDone();

	/** Our currently running rig vm instance */
	virtual TObjectPtr<URigVMHost>& GetEditorHost() = 0;

	// RigVMBP, once end-loaded, will inform other RigVM-Dependent systems that Host instances are ready.
	void BroadcastRigVMPackageDone();

	// Previously some memory classes were parented to the asset object
	// however it is no longer supported since classes are now identified 
	// with only package name + class name, see FTopLevelAssetPath
	// this function removes those deprecated class.
	// new classes should be created by RecompileVM and parented to the Package
	// during PostLoad
	void RemoveDeprecatedVMMemoryClass();

	// During load, we do not want the GC to destroy the generator classes until all URigVMMemoryStorage objects
	// are loaded, so we need to keep a pointer to the classes. These pointers will be removed on PreSave so that the
	// GC can do its work.
	TArray<TObjectPtr<URigVMMemoryStorageGeneratorClass>> OldMemoryStorageGeneratorClasses;

	FOnRigVMExternalVariablesChanged& OnExternalVariablesChanged() { return ExternalVariablesChangedEvent; }

	virtual void OnVariableAdded(const FName& InVarName);
	virtual void OnVariableRemoved(const FName& InVarName);
	virtual void OnVariableRenamed(const FName& InOldVarName, const FName& InNewVarName);
	virtual void OnVariableTypeChanged(const FName& InVarName, FEdGraphPinType InOldPinType, FEdGraphPinType InNewPinType);

	FName AddAssetVariableFromPinType(const FName& InName, const FEdGraphPinType& InType, bool bIsPublic = false, bool bIsReadOnly = false, FString InDefaultValue = FString());
	
	virtual FOnRigVMNodeDoubleClicked& OnNodeDoubleClicked()  { return NodeDoubleClickedEvent; }
	void BroadcastNodeDoubleClicked(URigVMNode* InNode);

	virtual FOnRigVMGraphImported& OnGraphImported()  { return GraphImportedEvent; }
	void BroadcastGraphImported(UEdGraph* InGraph);

	

	
	virtual FRigVMOnBulkEditDialogRequestedDelegate& OnRequestBulkEditDialog()  { return RequestBulkEditDialog; }

	virtual FRigVMOnBreakLinksDialogRequestedDelegate& OnRequestBreakLinksDialog()  { return RequestBreakLinksDialog; }

	virtual FRigVMOnPinTypeSelectionRequestedDelegate& OnRequestPinTypeSelectionDialog()  { return RequestPinTypeSelectionDialog; }

	

	
	void BroadCastReportCompilerMessage(EMessageSeverity::Type InSeverity, UObject* InSubject, const FString& InMessage);

	
	FOnRigVMExternalVariablesChanged ExternalVariablesChangedEvent;
	void BroadcastExternalVariablesChangedEvent();

	FOnRigVMNodeDoubleClicked NodeDoubleClickedEvent;
	FOnRigVMGraphImported GraphImportedEvent;
	FOnRigVMPostEditChangeChainProperty PostEditChangeChainPropertyEvent;
	FOnRigVMLocalizeFunctionDialogRequested RequestLocalizeFunctionDialog;
	FOnRigVMReportCompilerMessage ReportCompilerMessageEvent;
	FRigVMOnBulkEditDialogRequestedDelegate RequestBulkEditDialog;
	FRigVMOnBreakLinksDialogRequestedDelegate RequestBreakLinksDialog;
	FRigVMOnPinTypeSelectionRequestedDelegate RequestPinTypeSelectionDialog;
	FRigVMController_RequestJumpToHyperlinkDelegate RequestJumpToHyperlink;

	UEdGraph* CreateEdGraph(URigVMGraph* InModel, bool bForce = false);
	bool RemoveEdGraph(URigVMGraph* InModel);
	void DestroyObject(UObject* InObject);
	void CreateEdGraphForCollapseNodeIfNeeded(URigVMCollapseNode* InNode, bool bForce = false);
	bool RemoveEdGraphForCollapseNode(URigVMCollapseNode* InNode, bool bNotify = false);

	FCompilerResultsLog CompileLog;
	
	TArray<IRigVMGraphFunctionHost*> GetReferencedFunctionHosts(bool bForceLoad) const;

	FOnRigVMRequestInspectObject OnRequestInspectObjectEvent;
	FOnRigVMRequestInspectMemoryStorage OnRequestInspectMemoryStorageEvent;

	TArray<FRigVMReferenceNodeData> GetReferenceNodeData() const;
	virtual void SetupDefaultObjectDuringCompilation(URigVMHost* InCDO);


	static FSoftObjectPath PreDuplicateAssetPath;
	static FSoftObjectPath PreDuplicateHostPath;
	static TArray<FRigVMAssetInterfacePtr> sCurrentlyOpenedRigVMBlueprints;

	void MarkDirtyDuringLoad();

	bool IsMarkedDirtyDuringLoad() const;

	bool bDirtyDuringLoad;
	bool bErrorsDuringCompilation;
	bool bSuspendPythonMessagesForRigVMClient;
	bool bMarkBlueprintAsStructurallyModifiedPending;

	

	static FCriticalSection QueuedCompilerMessageDelegatesMutex;
	static TArray<FOnRigVMReportCompilerMessage::FDelegate> QueuedCompilerMessageDelegates;



#endif
public:

protected:
	

	friend class IControlRigAssetInterface;
	friend class FRigVMBlueprintCompilerContext;
	friend class FRigVMLegacyEditor;
	friend class FRigVMNewEditor;
	friend class FRigVMEditorBase;
	friend class FRigVMEditorModule;
	friend class URigVMEdGraphSchema;
	friend struct FRigVMEdGraphSchemaAction_PromoteToVariable;
	friend class URigVMBuildData;
	friend class FRigVMVariantDetailCustomization;
	friend class FRigVMTreeAssetVariantFilter;
	friend class FRigVMTreePackageNode;
	friend class SRigVMGraphNode;
	friend struct FGuardSkipDirtyBlueprintStatus;
	friend class SRigModuleAssetBrowser;
	friend class UE::RigVM::Editor::Tools::FFilterByAssetTag;
	friend class UEngineTestControlRig;
	friend class SRigCurveContainer;
	friend class SRigHierarchy;
	friend class FControlRigBaseEditor;
};

class RIGVMDEVELOPER_API FRigVMBlueprintCompileScope
{
public:
   
	FRigVMBlueprintCompileScope(FRigVMAssetInterfacePtr InBlueprint);

	~FRigVMBlueprintCompileScope();

private:

	FRigVMAssetInterfacePtr Blueprint;
};

struct FGuardSkipDirtyBlueprintStatus : private FNoncopyable
{
	[[nodiscard]] FGuardSkipDirtyBlueprintStatus(FRigVMAssetInterfacePtr InBlueprint, bool bNewValue)
	{
		if (InBlueprint)
		{
			WeakBlueprint = InBlueprint;
			bOldValue = InBlueprint->bSkipDirtyBlueprintStatus;
			InBlueprint->bSkipDirtyBlueprintStatus = bNewValue;
		}
	}
	~FGuardSkipDirtyBlueprintStatus()
	{
		if (UObject* Asset = WeakBlueprint.GetWeakObjectPtr().Get())
		{
			if (FRigVMAssetInterfacePtr Interface = Asset)
			{
				Interface->bSkipDirtyBlueprintStatus = bOldValue;
			}
		}
	}

private:
	TWeakInterfacePtr<IRigVMAssetInterface> WeakBlueprint;
	bool bOldValue = false;
};

#undef UE_API