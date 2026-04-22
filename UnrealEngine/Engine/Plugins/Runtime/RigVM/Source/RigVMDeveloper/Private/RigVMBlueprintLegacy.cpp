// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMBlueprintLegacy.h"

#include "RigVMBlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "EdGraph/RigVMEdGraph.h"
#include "EdGraph/RigVMEdGraphNode.h"
#include "EdGraph/RigVMEdGraphSchema.h"
#include "UObject/AssetRegistryTagsContext.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UObjectGlobals.h"
#include "RigVMObjectVersion.h"
#include "BlueprintCompilationManager.h"
#include "IMessageLogListing.h"
#include "RigVMCompiler/RigVMCompiler.h"
#include "RigVMCore/RigVMRegistry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "RigVMPythonUtils.h"
#include "RigVMTypeUtils.h"
#include "Algo/Count.h"
#include "Misc/StringOutputDevice.h"
#include "RigVMModel/RigVMControllerActions.h"
#include "RigVMModel/Nodes/RigVMAggregateNode.h"
#include "RigVMModel/Nodes/RigVMDispatchNode.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "Stats/StatsHierarchical.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RigVMBlueprintLegacy)

#if WITH_EDITOR
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/WatchedPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "RigVMBlueprintUtils.h"
#include "Editor/UnrealEdEngine.h"
#include "Editor/Transactor.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "RigVMEditorModule.h"
#include "ScopedTransaction.h"
#if !WITH_RIGVMLEGACYEDITOR
#include "RigVMEditor/Private/Editor/Kismet/RigVMBlueprintCompilationManager.h"
#endif
#endif//WITH_EDITOR

#define LOCTEXT_NAMESPACE "RigVMBlueprint"

FEdGraphPinType FRigVMOldPublicFunctionArg::GetPinType() const
{
	FRigVMExternalVariable Variable;
	Variable.Name = Name;
	Variable.bIsArray = bIsArray;
	Variable.TypeName = CPPType;
	
	if(CPPTypeObjectPath.IsValid())
	{
		Variable.TypeObject = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath(CPPTypeObjectPath.ToString());
	}

	return RigVMTypeUtils::PinTypeFromExternalVariable(Variable);
}

FRigVMOldPublicFunctionData::~FRigVMOldPublicFunctionData() = default;

bool FRigVMOldPublicFunctionData::IsMutable() const
{
	for(const FRigVMOldPublicFunctionArg& Arg : Arguments)
	{
		if(!Arg.CPPTypeObjectPath.IsNone())
		{
			if(UScriptStruct* Struct = Cast<UScriptStruct>(
				RigVMTypeUtils::FindObjectFromCPPTypeObjectPath(Arg.CPPTypeObjectPath.ToString())))
			{
				if(Struct->IsChildOf(FRigVMExecuteContext::StaticStruct()))
				{
					return true;
				}
			}
		}
	}
	return false;
}

void URigVMBlueprint::BeginDestroy()
{
	Super::BeginDestroy();
	Clear();
}

void URigVMBlueprint::UpdateSupportedEventNames()
{
	SupportedEventNames.Reset();
	if (URigVMHost* CDO = Cast<URigVMHost>(GetDefaultsObject()))
	{
		SupportedEventNames = CDO->GetSupportedEvents();
	}
}

UObject* URigVMBlueprint::GetDefaultsObject()
{
	if (GeneratedClass)
	{
		return GeneratedClass->GetDefaultObject();
	}
	return nullptr;
}

void URigVMBlueprint::PostEditChangeBlueprintActors()
{
	FBlueprintEditorUtils::PostEditChangeBlueprintActors(this);
}

FCompilerResultsLog URigVMBlueprint::CompileBlueprint()
{
	FCompilerResultsLog LogResults;
	LogResults.SetSourcePath(GetPathName());
	LogResults.BeginEvent(TEXT("Compile"));
		
	// TODO: sara-s remove once blueprint backend is replaced
	{
		EBlueprintCompileOptions CompileOptions = EBlueprintCompileOptions::None;

		// If compilation is enabled during PIE/simulation, references to the CDO might be held by a script variable.
		// Thus, we set the flag to direct the compiler to allow those references to be replaced during reinstancing.
		if (GEditor->PlayWorld != nullptr)
		{
			CompileOptions |= EBlueprintCompileOptions::IncludeCDOInReferenceReplacement;
		}
		
		FKismetEditorUtilities::CompileBlueprint(this, CompileOptions, &LogResults);
	}

	LogResults.EndEvent();

	// CachedNumWarnings = LogResults.NumWarnings;
	// CachedNumErrors = LogResults.NumErrors;

	if (UpgradeNotesLog.IsValid())
	{
		for (TSharedRef<FTokenizedMessage> Message :UpgradeNotesLog->Messages)
		{
			LogResults.AddTokenizedMessage(Message);
		}
	}

	return LogResults;
}

URigVMBlueprint::URigVMBlueprint()
	: IRigVMAssetInterface()
{
}

URigVMBlueprint::URigVMBlueprint(const FObjectInitializer& ObjectInitializer)
	: IRigVMAssetInterface(ObjectInitializer)
{

#if WITH_EDITORONLY_DATA
	ReferencedObjectPathsStored = false;
#endif

	bRecompileOnLoad = 0;
	SupportedEventNames.Reset();
	VMCompileSettings.ASTSettings.ReportDelegate.BindUObject(this, &IRigVMAssetInterface::HandleReportFromCompiler);

	bUpdatingExternalVariables = false;
	
#if WITH_EDITOR
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		CompileLog.bSilentMode = true;
	}
	CompileLog.SetSourcePath(GetPathName());
#endif

	if(GetClass() == URigVMBlueprint::StaticClass())
	{
		CommonInitialization(ObjectInitializer);
	}

	UBlueprint::OnChanged().AddUObject(this, &URigVMBlueprint::OnBlueprintChanged);
	UBlueprint::OnSetObjectBeingDebugged().AddUObject(this, &URigVMBlueprint::OnSetObjectBeingDebuggedReceived);
}

URigVMBlueprintGeneratedClass* URigVMBlueprint::GetRigVMBlueprintGeneratedClass() const
{
	URigVMBlueprintGeneratedClass* Result = Cast<URigVMBlueprintGeneratedClass>(*GeneratedClass);
	return Result;
}

void URigVMBlueprint::PostSerialize(FArchive& Ar)
{
	if(Ar.IsLoading())
	{
		if(Model_DEPRECATED || FunctionLibrary_DEPRECATED)
		{
			TGuardValue<bool> DisableClientNotifs(GetRigVMClient()->bSuspendNotifications, true);
			GetRigVMClient()->SetFromDeprecatedData(Model_DEPRECATED, FunctionLibrary_DEPRECATED);
		}
	}
}

UClass* URigVMBlueprint::GetBlueprintClass() const
{
	return URigVMBlueprintGeneratedClass::StaticClass();
}

UClass* URigVMBlueprint::RegenerateClass(UClass* ClassToRegenerate, UObject* PreviousCDO)
{
	UClass* Result;
	{
		TGuardValue<bool> NotificationGuard(bSuspendAllNotifications, true);
		Result = Super::RegenerateClass(ClassToRegenerate, PreviousCDO);
		if (URigVMBlueprintGeneratedClass* Generated = Cast<URigVMBlueprintGeneratedClass>(Result))
		{
			Generated->SupportedEventNames = SupportedEventNames;
			Generated->AssetVariant = AssetVariant;
		}
	}
	return Result;
}

TArray<FRigVMExternalVariable> URigVMBlueprint::GetExternalVariables(bool bFallbackToBlueprint) const
{
	TArray<FRigVMExternalVariable> ExternalVariables;
	if (URigVMBlueprintGeneratedClass* RigClass = GetRigVMBlueprintGeneratedClass())
	{
		if (URigVMHost* CDO = Cast<URigVMHost>(RigClass->GetDefaultObject(true /* create if needed */)))
		{
			return CDO->GetExternalVariablesImpl(bFallbackToBlueprint /* rely on variables within blueprint */);
		}
	}
	return ExternalVariables;
}

URigVM* URigVMBlueprint::GetVM(bool bCreateIfNeeded) const
{
	if (URigVMBlueprintGeneratedClass* RigClass = GetRigVMBlueprintGeneratedClass())
	{
		if (URigVMHost* CDO = Cast<URigVMHost>(RigClass->GetDefaultObject(bCreateIfNeeded)))
		{
			return CDO->GetVM();
		}
	}
	return nullptr;
}

FRigVMExtendedExecuteContext* URigVMBlueprint::GetRigVMExtendedExecuteContext()
{
	if (URigVMBlueprintGeneratedClass* RigClass = GetRigVMBlueprintGeneratedClass())
	{
		if (URigVMHost* CDO = Cast<URigVMHost>(RigClass->GetDefaultObject(false)))
		{
			return &CDO->GetRigVMExtendedExecuteContext();
		}
	}
	return nullptr;
}

void URigVMBlueprint::SetAssetStatus(const ERigVMAssetStatus& InStatus)
{
	switch (InStatus)
	{
		case RVMA_Dirty: Status = BS_Dirty; break;
		case RVMA_Error: Status = BS_Error; break;
		case RVMA_UpToDate: Status = BS_UpToDate; break;
		case RVMA_BeingCreated: Status = BS_BeingCreated; break;
		case RVMA_UpToDateWithWarnings: Status = BS_UpToDateWithWarnings; break;
		default: Status = BS_Unknown; break;
	}
}

ERigVMAssetStatus URigVMBlueprint::GetAssetStatus() const
{
	switch (Status)
	{
		case BS_Dirty: return RVMA_Dirty; break;
		case BS_Error: return RVMA_Error; break;
		case BS_UpToDate: return RVMA_UpToDate; break;
		case BS_BeingCreated: return RVMA_BeingCreated; break;
		case BS_UpToDateWithWarnings: return RVMA_UpToDateWithWarnings; break;
		default: return RVMA_Unknown; break;
	}
}

TArray<UObject*> URigVMBlueprint::GetArchetypeInstances(bool bIncludeCDO, bool bIncludeDerivedClasses) const
{
	URigVMBlueprintGeneratedClass* RigClass = GetRigVMBlueprintGeneratedClass();
	TArray<UObject*> ArchetypeInstances;
	if (bIncludeCDO)
	{
		URigVMHost* CDO = Cast<URigVMHost>(RigClass->GetDefaultObject(false /* create if needed */));
		ArchetypeInstances.Add(CDO);
	}
	//CDO->GetArchetypeInstances(ArchetypeInstances);
	GetObjectsOfClass(RigClass, ArchetypeInstances, bIncludeDerivedClasses);
	return ArchetypeInstances;
}

FRigVMDebugInfo& URigVMBlueprint::GetDebugInfo()
{
	URigVMBlueprintGeneratedClass* RigClass = GetRigVMBlueprintGeneratedClass();
	URigVMHost* CDO = Cast<URigVMHost>(RigClass->GetDefaultObject(false));
	return CDO->GetDebugInfo();
}

URigVMHost* URigVMBlueprint::CreateRigVMHostSuper(UObject* InOuter)
{
	return NewObject<URigVMHost>(InOuter, GetRigVMHostClass());
}

void URigVMBlueprint::MarkAssetAsStructurallyModified(bool bSkipDirtyAssetStatus)
{
	const TEnumAsByte<EBlueprintStatus> OldStatus = Status;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(this);
	if (bSkipDirtyAssetStatus)
	{
		Status = OldStatus;
	}
}

void URigVMBlueprint::MarkAssetAsModified(FPropertyChangedEvent PropertyChangedEvent)
{
	FBlueprintEditorUtils::MarkBlueprintAsModified(this, PropertyChangedEvent);
}

void URigVMBlueprint::AddUbergraphPage(URigVMEdGraph* RigVMEdGraph)
{
	FBlueprintEditorUtils::AddUbergraphPage(this, RigVMEdGraph);
}

void URigVMBlueprint::AddLastEditedDocument(URigVMEdGraph* RigVMEdGraph)
{
	LastEditedDocuments.AddUnique(RigVMEdGraph);
}

void URigVMBlueprint::Compile()
{
#if WITH_RIGVMLEGACYEDITOR
	FBPCompileRequest Request(this, EBlueprintCompileOptions::None, nullptr);
	FBlueprintCompilationManager::CompileSynchronously(Request);
#else
	// FRigVMBPCompileRequest Request(this, EBlueprintCompileOptions::None, nullptr);
	// FRigVMBlueprintCompilationManager::CompileSynchronously(Request);
#endif
}

void URigVMBlueprint::PatchVariableNodesOnLoad()
{
	IRigVMAssetInterface::PatchVariableNodesOnLoad();
#if WITH_EDITOR
	LastNewVariables = NewVariables;
#endif
}

void URigVMBlueprint::AddPinWatch(UEdGraphPin* InPin)
{
	if (!FKismetDebugUtilities::IsPinBeingWatched(this, InPin))
	{
		FKismetDebugUtilities::AddPinWatch(this, FBlueprintWatchedPin(InPin));
	}
}

void URigVMBlueprint::RemovePinWatch(UEdGraphPin* InPin)
{
	FKismetDebugUtilities::RemovePinWatch(this, InPin);
}

void URigVMBlueprint::ClearPinWatches()
{
	FKismetDebugUtilities::ClearPinWatches(this);
}

bool URigVMBlueprint::IsPinBeingWatched(const UEdGraphPin* InPin) const
{
	return FKismetDebugUtilities::IsPinBeingWatched(this, InPin);
}

void URigVMBlueprint::ForeachPinWatch(TFunctionRef<void(UEdGraphPin*)> Task)
{
	FKismetDebugUtilities::ForeachPinWatch(this, Task);
}

TMap<FString, FSoftObjectPath>& URigVMBlueprint::GetUserDefinedStructGuidToPathName(bool bFromCDO)
{
	if (bFromCDO)
	{
		URigVMBlueprintGeneratedClass* RigClass = GetRigVMBlueprintGeneratedClass();
		URigVMHost* CDO = Cast<URigVMHost>(RigClass->GetDefaultObject(true /* create if needed */));
		if (CDO)
		{
			return CDO->UserDefinedStructGuidToPathName;
		}
	}
	return UserDefinedStructGuidToPathName;
}

TMap<FString, FSoftObjectPath>& URigVMBlueprint::GetUserDefinedEnumToPathName(bool bFromCDO)
{
	if (bFromCDO)
	{
		URigVMBlueprintGeneratedClass* RigClass = GetRigVMBlueprintGeneratedClass();
		URigVMHost* CDO = Cast<URigVMHost>(RigClass->GetDefaultObject(true /* create if needed */));
		if (CDO)
		{
			return CDO->UserDefinedEnumToPathName;
		}
	}
	return UserDefinedEnumToPathName;
}

TSet<TObjectPtr<UObject>>& URigVMBlueprint::GetUserDefinedTypesInUse(bool bFromCDO)
{
	if (bFromCDO)
	{
		URigVMBlueprintGeneratedClass* RigClass = GetRigVMBlueprintGeneratedClass();
		URigVMHost* CDO = Cast<URigVMHost>(RigClass->GetDefaultObject(true /* create if needed */));
		if (CDO)
		{
			return CDO->UserDefinedTypesInUse;
		}
	}
	return UserDefinedTypesInUse;
}

void URigVMBlueprint::PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)
{
	UBlueprint::PostEditChangeChainProperty(PropertyChangedEvent);
	IRigVMAssetInterface::PostEditChangeChainProperty(PropertyChangedEvent);
}

void URigVMBlueprint::PostRename(UObject* OldOuter, const FName OldName)
{
	UBlueprint::PostRename(OldOuter, OldName);
	IRigVMAssetInterface::PostRename(OldOuter, OldName);
}

void URigVMBlueprint::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	UBlueprint::GetPreloadDependencies(OutDeps);
	IRigVMAssetInterface::GetPreloadDependencies(OutDeps);
}

void URigVMBlueprint::PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext)
{
	Super::PostSaveRoot(ObjectSaveContext);

	// Make sure all the tags are accounted for in the TypeActions after we save
	if (FBlueprintActionDatabase* ActionDatabase = FBlueprintActionDatabase::TryGet())
	{
		ActionDatabase->ClearAssetActions(GetClass());
		ActionDatabase->RefreshClassActions(GetClass());
	}
}

void URigVMBlueprint::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	UBlueprint::PreSave(ObjectSaveContext);
	IRigVMAssetInterface::PreSave(ObjectSaveContext);
	if (URigVMBlueprintGeneratedClass* RigClass = GetRigVMBlueprintGeneratedClass())
	{
		if (const URigVMHost* CDO = Cast<URigVMHost>(RigClass->GetDefaultObject(true /* create if needed */)))
		{
			RigClass->SupportedEventNames = SupportedEventNames;
		}
		RigClass->AssetVariant = AssetVariant;
	}
}

void URigVMBlueprint::PostLoad()
{
	UBlueprint::PostLoad();
	IRigVMAssetInterface::PostLoad();

	FCoreUObjectDelegates::OnObjectModified.RemoveAll(this);
	FCoreUObjectDelegates::OnObjectModified.AddUObject(this, &URigVMBlueprint::OnPreVariableChange);
	
	UBlueprint::OnChanged().RemoveAll(this);
	UBlueprint::OnChanged().AddUObject(this, &URigVMBlueprint::OnPostVariableChange);
}

#if WITH_EDITORONLY_DATA

void URigVMBlueprint::DeclareConstructClasses(TArray<FTopLevelAssetPath>& OutConstructClasses, const UClass* SpecificSubclass)
{
	UBlueprint::DeclareConstructClasses(OutConstructClasses, SpecificSubclass);
	IRigVMAssetInterface::DeclareConstructClasses(OutConstructClasses, SpecificSubclass);
}
#endif


void URigVMBlueprint::OnBlueprintChanged(UBlueprint* InBlueprint)
{
	IRigVMAssetInterface::OnChanged().Broadcast(InBlueprint);
}

void URigVMBlueprint::OnSetObjectBeingDebuggedReceived(UObject* InObject)
{
	IRigVMAssetInterface::OnSetObjectBeingDebugged().Broadcast(InObject);
}

void URigVMBlueprint::PreCompile()
{
	if (URigVMBlueprintGeneratedClass* RigClass = GetRigVMBlueprintGeneratedClass())
	{
		URigVMHost* CDO = Cast<URigVMHost>(RigClass->GetDefaultObject(true /* create if needed */));
		{
			SetupDefaultObjectDuringCompilation(CDO);
			if (!this->HasAnyFlags(RF_Transient | RF_Transactional))
			{
				CDO->Modify(false);
			}
		}
	}
}

FProperty* URigVMBlueprint::FindGeneratedPropertyByName(const FName& InName) const
{
	return SkeletonGeneratedClass->FindPropertyByName(InName);
}

bool URigVMBlueprint::SetVariableTooltip(const FName& InName, const FText& InTooltip)
{
	FBlueprintEditorUtils::SetBlueprintVariableMetaData(this, InName, nullptr, FBlueprintMetadata::MD_Tooltip, InTooltip.ToString());
	return true;
}

FText URigVMBlueprint::GetVariableTooltip(const FName& InName) const
{
	FString Result;
	FBlueprintEditorUtils::GetBlueprintVariableMetaData(this, InName, nullptr, FBlueprintMetadata::MD_Tooltip, Result);
	return FText::FromString(Result);
}

bool URigVMBlueprint::SetVariableCategory(const FName& InName, const FString& InCategory)
{
	FBlueprintEditorUtils::SetBlueprintVariableCategory(this, InName, nullptr, FText::FromString(InCategory), true);
	return true;
}

FString URigVMBlueprint::GetVariableCategory(const FName& InName) 
{
	return FBlueprintEditorUtils::GetBlueprintVariableCategory(this, InName, nullptr).ToString();
}

FString URigVMBlueprint::GetVariableMetadataValue(const FName& InName, const FName& InKey)
{
	FString Result;
	FBlueprintEditorUtils::GetBlueprintVariableMetaData(this, InName, nullptr, InKey, /*out*/ Result);
	return Result;
}

bool URigVMBlueprint::SetVariableExposeOnSpawn(const FName& InName, const bool bInExposeOnSpawn)
{
	if(bInExposeOnSpawn)
	{
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(this, InName, nullptr, FBlueprintMetadata::MD_ExposeOnSpawn, TEXT("true"));
	}
	else
	{
		FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(this, InName, nullptr, FBlueprintMetadata::MD_ExposeOnSpawn);
	} 
	return true;
}

bool URigVMBlueprint::SetVariableExposeToCinematics(const FName& InName, const bool bInExposeToCinematics)
{
	FBlueprintEditorUtils::SetInterpFlag(this, InName, bInExposeToCinematics);
	return true;
}

bool URigVMBlueprint::SetVariablePrivate(const FName& InName, const bool bInPrivate)
{
	if(bInPrivate)
	{
		FBlueprintEditorUtils::SetBlueprintVariableMetaData(this, InName, nullptr, FBlueprintMetadata::MD_Private, TEXT("true"));
	}
	else
	{
		FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(this, InName, nullptr, FBlueprintMetadata::MD_Private);
	} 
	return true;
}

bool URigVMBlueprint::SetVariablePublic(const FName& InName, const bool bIsPublic)
{
	FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(this, InName, bIsPublic);
	return true;
}

FString URigVMBlueprint::OnCopyVariable(const FName& InName) const
{
	FString OutputString;
	int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(this, InName);
	if (VarIndex != INDEX_NONE)
	{
		// make a copy of the Variable description so we can set the default value
		FBPVariableDescription Description = NewVariables[VarIndex];

		//Grab property of blueprint's current CDO
		UObject* GeneratedCDO = GeneratedClass->GetDefaultObject();

		if (FProperty* TargetProperty = FindFProperty<FProperty>(GeneratedClass, Description.VarName))
		{
			// Grab the address of where the property is actually stored (UObject* base, plus the offset defined in the property)
			if (void* OldPropertyAddr = TargetProperty->ContainerPtrToValuePtr<void>(GeneratedCDO))
			{
				TargetProperty->ExportTextItem_Direct(Description.DefaultValue, OldPropertyAddr, OldPropertyAddr, nullptr, PPF_SerializedAsImportText);
			}
		}

		FBPVariableDescription::StaticStruct()->ExportText(OutputString, &Description, &Description, nullptr, 0, nullptr, false);
		OutputString = TEXT("BPVar") + OutputString;
	}

	return OutputString;
}

bool URigVMBlueprint::OnPasteVariable(const FString& InText)
{
	if (!ensure(InText.StartsWith(TEXT("BPVar"), ESearchCase::CaseSensitive)))
	{
		return false;
	}

	FBPVariableDescription Description;
	FStringOutputDevice Errors;
	const TCHAR* Import = InText.GetCharArray().GetData() + FCString::Strlen(TEXT("BPVar"));
	FBPVariableDescription::StaticStruct()->ImportText(Import, &Description, nullptr, PPF_None, &Errors, FBPVariableDescription::StaticStruct()->GetName());
	if (Errors.IsEmpty())
	{
		FBPVariableDescription NewVar = FBlueprintEditorUtils::DuplicateVariableDescription(this, Description);
		if (NewVar.VarGuid.IsValid())
		{
			FScopedTransaction Transaction(FText::Format(LOCTEXT("PasteVariable", "Paste Variable: {0}"), FText::FromName(NewVar.VarName)));
			Modify();
			NewVariables.Add(NewVar);

			// Potentially adjust variable names for any child blueprints
			FBlueprintEditorUtils::ValidateBlueprintChildVariables(this, NewVar.VarName);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(this);
			return true;
		}
	}
	return false;
}

void URigVMBlueprint::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS;
	UBlueprint::GetAssetRegistryTags(OutTags);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS;
}

TArray<FRigVMGraphVariableDescription> URigVMBlueprint::GetAssetVariables() const
{
	TArray<FRigVMGraphVariableDescription> Variables;
	for (const FBPVariableDescription& BPVariable : NewVariables)
	{
		FRigVMGraphVariableDescription NewVariable;
		NewVariable.Name = BPVariable.VarName;
		NewVariable.DefaultValue = BPVariable.DefaultValue;
		NewVariable.Category = BPVariable.Category;
		FString CPPType;
		UObject* CPPTypeObject;
		RigVMTypeUtils::CPPTypeFromPinType(BPVariable.VarType, CPPType, &CPPTypeObject);
		NewVariable.CPPType = CPPType;
		NewVariable.CPPTypeObject = CPPTypeObject;
		if (NewVariable.CPPTypeObject)
		{
			NewVariable.CPPTypeObjectPath = *NewVariable.CPPTypeObject->GetPathName();
		}
		if(BPVariable.HasMetaData(FBlueprintMetadata::MD_Tooltip))
		{
			NewVariable.Tooltip = FText::FromString(BPVariable.GetMetaData(FBlueprintMetadata::MD_Tooltip));
		}
		if(BPVariable.HasMetaData(FBlueprintMetadata::MD_ExposeOnSpawn))
		{
			NewVariable.bExposedOnSpawn = BPVariable.GetMetaData(FBlueprintMetadata::MD_ExposeOnSpawn) == TEXT("true");
		}
		if (BPVariable.PropertyFlags & CPF_Interp)
		{
			NewVariable.bExposeToCinematics = true;
		}
		if (!(BPVariable.PropertyFlags & CPF_DisableEditOnInstance))
		{
			NewVariable.bPublic = true;
		}
		if(BPVariable.HasMetaData(FBlueprintMetadata::MD_Private))
		{
			NewVariable.bPrivate = BPVariable.GetMetaData(FBlueprintMetadata::MD_Private) == TEXT("true");
		}
		Variables.Add(NewVariable);
	}

	return Variables;
}

#if WITH_EDITOR


// FName URigVMBlueprint::AddAssetVariable(const FName& InName, const FString& InCPPType, bool bIsPublic, bool bIsReadOnly, FString InDefaultValue)
// {
// 	FBPVariableDescription NewVar;
// 	const FRigVMExternalVariable Variable = RigVMTypeUtils::ExternalVariableFromCPPTypePath(InName, InCPPType, bIsPublic, bIsReadOnly);
// 	if (!Variable.IsValid(true))
// 	{
// 		return NAME_None;
// 	}
//
// 	return AddAssetVariableFromExternal(Variable, InDefaultValue);
// }
//
// FName URigVMBlueprint::AddAssetVariableFromExternal(const FRigVMExternalVariable& Variable, const FString& InDefaultValue)
// {
// 	FBPVariableDescription NewVar;
// 	TSharedPtr<FKismetNameValidator> NameValidator = MakeShareable(new FKismetNameValidator(this, NAME_None, nullptr));
// 	const FName VarName = FindHostMemberVariableUniqueName(NameValidator, Variable.Name.ToString());
// 	NewVar.VarName = VarName;
// 	NewVar.VarGuid = FGuid::NewGuid();
// 	
// 	NewVar.VarType = RigVMTypeUtils::PinTypeFromCPPType(Variable.TypeName, Variable.TypeObject);
// 	if (!NewVar.VarType.PinCategory.IsValid())
// 	{
// 		return NAME_None;
// 	}
// 	NewVar.FriendlyName = FName::NameToDisplayString(Variable.Name.ToString(), (NewVar.VarType.PinCategory == UEdGraphSchema_K2::PC_Boolean) ? true : false);
//
// 	NewVar.PropertyFlags |= (CPF_Edit | CPF_BlueprintVisible | CPF_DisableEditOnInstance);
//
// 	if (Variable.bIsPublic)
// 	{
// 		NewVar.PropertyFlags &= ~CPF_DisableEditOnInstance;
// 	}
//
// 	if (Variable.bIsReadOnly)
// 	{
// 		NewVar.PropertyFlags |= CPF_BlueprintReadOnly;
// 	}
//
// 	NewVar.ReplicationCondition = COND_None;
//
// 	NewVar.Category = UEdGraphSchema_K2::VR_DefaultCategory;
//
// 	// user created variables should be none of these things
// 	NewVar.VarType.bIsConst = false;
// 	NewVar.VarType.bIsWeakPointer = false;
// 	NewVar.VarType.bIsReference = false;
//
// 	// Text variables, etc. should default to multiline
// 	NewVar.SetMetaData(TEXT("MultiLine"), TEXT("true"));
//
// 	NewVar.DefaultValue = InDefaultValue;
//
// 	Modify();
// 	NewVariables.Add(NewVar);
// 	MarkAssetAsModified();
// 	
// #if WITH_RIGVMLEGACYEDITOR
// 	FBPCompileRequest Request(this, EBlueprintCompileOptions::None, nullptr);
// 	FBlueprintCompilationManager::CompileSynchronously(Request);
// #endif
// 	
// 	return NewVar.VarName;
// }
//
// bool URigVMBlueprint::RemoveAssetVariable(const FName& InName)
// {
// 	const int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(this, InName);
// 	if (VarIndex == INDEX_NONE)
// 	{
// 		return false;
// 	}
// 	
// 	FBlueprintEditorUtils::RemoveMemberVariable(this, InName);
// 	return true;
// }
//
// bool URigVMBlueprint::RenameAssetVariable(const FName& InOldName, const FName& InNewName)
// {
// 	int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(this, InOldName);
// 	if (VarIndex == INDEX_NONE)
// 	{
// 		return false;
// 	}
//
// 	VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(this, InNewName);
// 	if (VarIndex != INDEX_NONE)
// 	{
// 		return false;
// 	}
// 	
// 	FBlueprintEditorUtils::RenameMemberVariable(this, InOldName, InNewName);
// 	return true;
// }
//
// bool URigVMBlueprint::ChangeAssetVariableType(const FName& InName, const FString& InCPPType, bool bIsPublic, bool bIsReadOnly, FString InDefaultValue)
// {
// 	int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(this, InName);
// 	if (VarIndex == INDEX_NONE)
// 	{
// 		return false;
// 	}
//
// 	FRigVMExternalVariable Variable;
// 	Variable.Name = InName;
// 	Variable.bIsPublic = bIsPublic;
// 	Variable.bIsReadOnly = bIsReadOnly;
//
// 	FString CPPType = InCPPType;
// 	if (CPPType.StartsWith(TEXT("TMap<")))
// 	{
// 		UE_LOG(LogRigVMDeveloper, Warning, TEXT("TMap Variables are not supported."));
// 		return false;
// 	}
//
// 	Variable.bIsArray = RigVMTypeUtils::IsArrayType(CPPType);
// 	if (Variable.bIsArray)
// 	{
// 		CPPType = RigVMTypeUtils::BaseTypeFromArrayType(CPPType);
// 	}
//
// 	if (CPPType == TEXT("bool"))
// 	{
// 		Variable.TypeName = *CPPType;
// 		Variable.Size = sizeof(bool);
// 	}
// 	else if (CPPType == TEXT("float"))
// 	{
// 		Variable.TypeName = *CPPType;
// 		Variable.Size = sizeof(float);
// 	}
// 	else if (CPPType == TEXT("double"))
// 	{
// 		Variable.TypeName = *CPPType;
// 		Variable.Size = sizeof(double);
// 	}
// 	else if (CPPType == TEXT("int32"))
// 	{
// 		Variable.TypeName = *CPPType;
// 		Variable.Size = sizeof(int32);
// 	}
// 	else if (CPPType == TEXT("FString"))
// 	{
// 		Variable.TypeName = *CPPType;
// 		Variable.Size = sizeof(FString);
// 	}
// 	else if (CPPType == TEXT("FName"))
// 	{
// 		Variable.TypeName = *CPPType;
// 		Variable.Size = sizeof(FName);
// 	}
// 	else if(UScriptStruct* ScriptStruct = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UScriptStruct>(CPPType))
// 	{
// 		Variable.TypeName = *RigVMTypeUtils::GetUniqueStructTypeName(ScriptStruct);
// 		Variable.TypeObject = ScriptStruct;
// 		Variable.Size = ScriptStruct->GetStructureSize();
// 	}
// 	else if (UEnum* Enum= RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UEnum>(CPPType))
// 	{
// 		Variable.TypeName = *RigVMTypeUtils::CPPTypeFromEnum(Enum);
// 		Variable.TypeObject = Enum;
// 		Variable.Size = static_cast<int32>(Enum->GetResourceSizeBytes(EResourceSizeMode::EstimatedTotal));
// 	}
//
// 	FEdGraphPinType PinType = RigVMTypeUtils::PinTypeFromExternalVariable(Variable);
// 	if (!PinType.PinCategory.IsValid())
// 	{
// 		return false;
// 	}
//
// 	ChangeAssetVariableType(InName, PinType);
//
// 	return true;
// }

FString URigVMBlueprint::GetVariableDefaultValue(const FName& InName, bool bFromDebuggedObject) const
{
	FString DefaultValue;
	UObject* ObjectContainer = bFromDebuggedObject ? GetObjectBeingDebugged() : GeneratedClass->GetDefaultObject();
	if(ObjectContainer)
	{
		FProperty* TargetProperty = FindFProperty<FProperty>(GeneratedClass, InName);

		if (TargetProperty)
		{
			const uint8* Container = (const uint8*)ObjectContainer;
			FBlueprintEditorUtils::PropertyValueToString(TargetProperty, Container, DefaultValue, nullptr);
			return DefaultValue;
		}
	}
	return DefaultValue;
}

// bool URigVMBlueprint::ChangeAssetVariableType(const FName& InName, const FEdGraphPinType& InType)
// {
// 	FBlueprintEditorUtils::ChangeMemberVariableType(this, InName, InType);
// 	return true;
// }

FName URigVMBlueprint::AddMemberVariable(const FName& InName, const FString& InCPPType, bool bIsPublic, bool bIsReadOnly, FString InDefaultValue)
{
	FRigVMExternalVariable Variable = RigVMTypeUtils::ExternalVariableFromCPPTypePath(InName, InCPPType, bIsPublic, bIsReadOnly);
	FName Result = AddHostMemberVariableFromExternal(Variable, InDefaultValue);
	if (!Result.IsNone())
	{
#if WITH_RIGVMLEGACYEDITOR
		FBPCompileRequest Request(this, EBlueprintCompileOptions::None, nullptr);
		FBlueprintCompilationManager::CompileSynchronously(Request);
#else
		// FRigVMBPCompileRequest Request(this, EBlueprintCompileOptions::None, nullptr);
		// FRigVMBlueprintCompilationManager::CompileSynchronously(Request);
#endif
	}
	return Result;
}

bool URigVMBlueprint::RemoveMemberVariable(const FName& InName)
{
	const int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(this, InName);
	if (VarIndex == INDEX_NONE)
	{
		return false;
	}
	
	FBlueprintEditorUtils::RemoveMemberVariable(this, InName);
	return true;
}

bool URigVMBlueprint::BulkRemoveMemberVariables(const TArray<FName>& InNames)
{
	for (const FName& Name : InNames)
	{
		const int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(this, Name);
		if (VarIndex == INDEX_NONE)
		{
			return false;
		}
	}

	FBlueprintEditorUtils::BulkRemoveMemberVariables(this, InNames);
	return true;
}

bool URigVMBlueprint::RenameMemberVariable(const FName& InOldName, const FName& InNewName)
{
	int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(this, InOldName);
	if (VarIndex == INDEX_NONE)
	{
		return false;
	}

	VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(this, InNewName);
	if (VarIndex != INDEX_NONE)
	{
		return false;
	}
	
	FBlueprintEditorUtils::RenameMemberVariable(this, InOldName, InNewName);
	return true;
}

bool URigVMBlueprint::ChangeMemberVariableType(const FName& InName, const FString& InCPPType, bool bIsPublic,
	bool bIsReadOnly, FString InDefaultValue)
{
	int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(this, InName);
	if (VarIndex == INDEX_NONE)
	{
		return false;
	}

	FRigVMExternalVariable Variable;
	Variable.Name = InName;
	Variable.bIsPublic = bIsPublic;
	Variable.bIsReadOnly = bIsReadOnly;

	FString CPPType = InCPPType;
	if (CPPType.StartsWith(TEXT("TMap<")))
	{
		UE_LOG(LogRigVMDeveloper, Warning, TEXT("TMap Variables are not supported."));
		return false;
	}

	Variable.bIsArray = RigVMTypeUtils::IsArrayType(CPPType);
	if (Variable.bIsArray)
	{
		CPPType = RigVMTypeUtils::BaseTypeFromArrayType(CPPType);
	}

	if (CPPType == TEXT("bool"))
	{
		Variable.TypeName = *CPPType;
		Variable.Size = sizeof(bool);
	}
	else if (CPPType == TEXT("float"))
	{
		Variable.TypeName = *CPPType;
		Variable.Size = sizeof(float);
	}
	else if (CPPType == TEXT("double"))
	{
		Variable.TypeName = *CPPType;
		Variable.Size = sizeof(double);
	}
	else if (CPPType == TEXT("int32"))
	{
		Variable.TypeName = *CPPType;
		Variable.Size = sizeof(int32);
	}
	else if (CPPType == TEXT("FString"))
	{
		Variable.TypeName = *CPPType;
		Variable.Size = sizeof(FString);
	}
	else if (CPPType == TEXT("FName"))
	{
		Variable.TypeName = *CPPType;
		Variable.Size = sizeof(FName);
	}
	else if(UScriptStruct* ScriptStruct = RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UScriptStruct>(CPPType))
	{
		Variable.TypeName = *RigVMTypeUtils::GetUniqueStructTypeName(ScriptStruct);
		Variable.TypeObject = ScriptStruct;
		Variable.Size = ScriptStruct->GetStructureSize();
	}
	else if (UEnum* Enum= RigVMTypeUtils::FindObjectFromCPPTypeObjectPath<UEnum>(CPPType))
	{
		Variable.TypeName = *RigVMTypeUtils::CPPTypeFromEnum(Enum);
		Variable.TypeObject = Enum;
		Variable.Size = static_cast<int32>(Enum->GetResourceSizeBytes(EResourceSizeMode::EstimatedTotal));
	}

	FEdGraphPinType PinType = RigVMTypeUtils::PinTypeFromExternalVariable(Variable);
	if (!PinType.PinCategory.IsValid())
	{
		return false;
	}

	FBlueprintEditorUtils::ChangeMemberVariableType(this, InName, PinType);

	return true;
}

bool URigVMBlueprint::ChangeMemberVariableType(const FName& InName, const FEdGraphPinType& InType)
{
	int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(this, InName);
	if (VarIndex == INDEX_NONE)
	{
		return false;
	}
	
	FBlueprintEditorUtils::ChangeMemberVariableType(this, InName, InType);
	return true;
}

FName URigVMBlueprint::FindHostMemberVariableUniqueName(TSharedPtr<FKismetNameValidator> InNameValidator, const FString& InBaseName)
{
	FString BaseName = InBaseName;
	if (InNameValidator->IsValid(BaseName) == EValidatorResult::ContainsInvalidCharacters)
	{
		for (TCHAR& TestChar : BaseName)
		{
			for (TCHAR BadChar : UE_BLUEPRINT_INVALID_NAME_CHARACTERS)
			{
				if (TestChar == BadChar)
				{
					TestChar = TEXT('_');
					break;
				}
			}
		}
	}

	FString KismetName = BaseName;

	int32 Suffix = 0;
	while (InNameValidator->IsValid(KismetName) != EValidatorResult::Ok)
	{
		KismetName = FString::Printf(TEXT("%s_%d"), *BaseName, Suffix);
		Suffix++;
	}


	return *KismetName;
}

int32 URigVMBlueprint::AddHostMemberVariable(URigVMBlueprint* InBlueprint, const FName& InVarName, FEdGraphPinType InVarType, bool bIsPublic, bool bIsReadOnly, FString InDefaultValue)
{
	FBPVariableDescription NewVar;

	NewVar.VarName = InVarName;
	NewVar.VarGuid = FGuid::NewGuid();
	NewVar.FriendlyName = FName::NameToDisplayString(InVarName.ToString(), (InVarType.PinCategory == UEdGraphSchema_K2::PC_Boolean) ? true : false);
	NewVar.VarType = InVarType;

	NewVar.PropertyFlags |= (CPF_Edit | CPF_BlueprintVisible | CPF_DisableEditOnInstance);

	if (bIsPublic)
	{
		NewVar.PropertyFlags &= ~CPF_DisableEditOnInstance;
	}

	if (bIsReadOnly)
	{
		NewVar.PropertyFlags |= CPF_BlueprintReadOnly;
	}

	NewVar.ReplicationCondition = COND_None;

	NewVar.Category = UEdGraphSchema_K2::VR_DefaultCategory;

	// user created variables should be none of these things
	NewVar.VarType.bIsConst = false;
	NewVar.VarType.bIsWeakPointer = false;
	NewVar.VarType.bIsReference = false;

	// Text variables, etc. should default to multiline
	NewVar.SetMetaData(TEXT("MultiLine"), TEXT("true"));

	NewVar.DefaultValue = InDefaultValue;

	return InBlueprint->NewVariables.Add(NewVar);
}

FName URigVMBlueprint::AddHostMemberVariableFromExternal(FRigVMExternalVariable InVariableToCreate, FString InDefaultValue)
{
	FEdGraphPinType PinType = RigVMTypeUtils::PinTypeFromExternalVariable(InVariableToCreate);
	if (!PinType.PinCategory.IsValid())
	{
		return NAME_None;
	}

	Modify();

	TSharedPtr<FKismetNameValidator> NameValidator = MakeShareable(new FKismetNameValidator(this, NAME_None, nullptr));
	FName VarName = FindHostMemberVariableUniqueName(NameValidator, InVariableToCreate.Name.ToString());
	int32 VariableIndex = AddHostMemberVariable(this, VarName, PinType, InVariableToCreate.bIsPublic, InVariableToCreate.bIsReadOnly, InDefaultValue);
	if (VariableIndex != INDEX_NONE)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(this);
		return VarName;
	}

	return NAME_None;
}


void URigVMBlueprint::OnPreVariableChange(UObject* InObject)
{
	if (InObject != this)
	{
		return;
	}
	LastNewVariables = NewVariables;
}

void URigVMBlueprint::OnPostVariableChange(UBlueprint* InBlueprint)
{
	if (InBlueprint != this)
	{
		return;
	}

	if (bUpdatingExternalVariables)
	{
		return;
	}

	TGuardValue<bool> UpdatingVariablesGuard(bUpdatingExternalVariables, true);
	TArray<FBPVariableDescription> LocalLastNewVariables = LastNewVariables;

	TMap<FGuid, int32> NewVariablesByGuid;
	for (int32 VarIndex = 0; VarIndex < NewVariables.Num(); VarIndex++)
	{
		NewVariablesByGuid.Add(NewVariables[VarIndex].VarGuid, VarIndex);
	}

	TMap<FGuid, int32> OldVariablesByGuid;
	for (int32 VarIndex = 0; VarIndex < LocalLastNewVariables.Num(); VarIndex++)
	{
		OldVariablesByGuid.Add(LocalLastNewVariables[VarIndex].VarGuid, VarIndex);
	}

	for (const FBPVariableDescription& OldVariable : LocalLastNewVariables)
	{
		if (!NewVariablesByGuid.Contains(OldVariable.VarGuid))
		{
			OnVariableRemoved(OldVariable.VarName);
			continue;
		}
	}

	for (const FBPVariableDescription& NewVariable : NewVariables)
	{
		if (!OldVariablesByGuid.Contains(NewVariable.VarGuid))
		{
			OnVariableAdded(NewVariable.VarName);
			continue;
		}

		int32 OldVarIndex = OldVariablesByGuid.FindChecked(NewVariable.VarGuid);
		const FBPVariableDescription& OldVariable = LocalLastNewVariables[OldVarIndex];
		if (OldVariable.VarName != NewVariable.VarName)
		{
			OnVariableRenamed(OldVariable.VarName, NewVariable.VarName);
		}

		if (OldVariable.VarType != NewVariable.VarType)
		{
			OnVariableTypeChanged(NewVariable.VarName, OldVariable.VarType, NewVariable.VarType);
		}
	}

	LastNewVariables = NewVariables;
}

void URigVMBlueprint::GetTypeActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

#if WITH_EDITOR
	GetEditorModule()->GetTypeActions(FRigVMAssetInterfacePtr(const_cast<URigVMBlueprint*>(this)), ActionRegistrar);
#endif
}

void URigVMBlueprint::GetInstanceActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

#if WITH_EDITOR
	GetEditorModule()->GetInstanceActions(FRigVMAssetInterfacePtr(const_cast<URigVMBlueprint*>(this)), ActionRegistrar);
#endif
}


#endif

#undef LOCTEXT_NAMESPACE


