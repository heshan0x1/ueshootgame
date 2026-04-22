// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigAsset.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "Engine/SkeletalMesh.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "ControlRig.h"
#include "ControlRigBlueprintGeneratedClass.h"
#include "Graph/ControlRigGraph.h"
#include "Graph/ControlRigGraphSchema.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UObjectGlobals.h"
#include "ControlRigObjectVersion.h"
#include "ModularRig.h"
#include "ModularRigController.h"
#include "RigVMCompiler/RigVMCompiler.h"
#include "RigVMCore/RigVMRegistry.h"
#include "Units/Execution/RigUnit_BeginExecution.h"
#include "Units/Hierarchy/RigUnit_SetBoneTransform.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "RigVMPythonUtils.h"
#include "RigVMTypeUtils.h"
#include "UObject/UObjectIterator.h"
#include "MVVM/ViewModels/ViewDensity.h"
#include "RigVMModel/Nodes/RigVMAggregateNode.h"
#include "Rigs/RigControlHierarchy.h"
#include "Settings/ControlRigSettings.h"
#include "Units/ControlRigNodeWorkflow.h"
#include "Units/Execution/RigUnit_PrepareForExecution.h"
#include "Units/Execution/RigUnit_DynamicHierarchy.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ControlRigAsset)

#if WITH_EDITOR
#include "IControlRigEditorModule.h"
#include "Kismet2/WatchedPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Editor/UnrealEdEngine.h"
#include "Editor/Transactor.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "ScopedTransaction.h"
#include "Algo/Count.h"
#endif//WITH_EDITOR

#define LOCTEXT_NAMESPACE "ControlRigBlueprint"

TArray<FControlRigAssetInterfacePtr> IControlRigAssetInterface::sCurrentlyOpenedRigBlueprints;

FControlRigAssetInterfacePtr IControlRigAssetInterface::GetInterfaceOuter(const UObject* InObject)
{
	if (FRigVMAssetInterfacePtr RigVMAsset = IRigVMAssetInterface::GetInterfaceOuter(InObject))
	{
		if (RigVMAsset.GetObject()->Implements<UControlRigAssetInterface>())
		{
			return RigVMAsset.GetObject();
		}
	}
	return nullptr;
}

TArray<UClass*> IControlRigAssetInterface::FindAllImplementingClasses()
{
	TArray<UClass*> Result;
	for (TObjectIterator<UClass> ClassIterator; ClassIterator; ++ClassIterator)
	{
		if (ClassIterator->ImplementsInterface(UControlRigAssetInterface::StaticClass()))
		{
			Result.AddUnique(*ClassIterator);
		}
	}
	return Result;
}

FRigVMClient* IControlRigAssetInterface::GetRigVMClient()
{
	return GetRigVMAssetInterface()->GetRigVMClient();
}

void IControlRigAssetInterface::CommonInitialization(const FObjectInitializer& ObjectInitializer)
{
	FControlRigAssetInterfacePtr Asset = ObjectInitializer.GetObj();
#if WITH_EDITORONLY_DATA
	Asset->GetShapeLibraries().Add(UControlRigSettings::Get()->DefaultShapeLibrary);
#endif

	Asset->GetValidator() = ObjectInitializer.CreateDefaultSubobject<UControlRigValidator>(Asset.GetObject(), TEXT("ControlRigValidator"));
	Asset->GetDebugBoneRadius() = 1.f;

	Asset->GetExposesAnimatableControls() = false;

	Asset->SetHierarchy(ObjectInitializer.CreateDefaultSubobject<URigHierarchy>(Asset.GetObject(), TEXT("Hierarchy")));
	URigHierarchyController* Controller = Asset->GetHierarchy()->GetController(true);
	// give BP a chance to propagate hierarchy changes to available control rig instances
	Controller->OnModified().AddRaw(Asset.GetInterface(), &IControlRigAssetInterface::HandleHierarchyModified);

	Asset->GetRigVMAssetInterface()->IRigVMAssetInterface::CommonInitialization(ObjectInitializer);
	
	Asset->GetModularRigModel().SetOuterClientHost(Asset.GetObject());
	UModularRigController* ModularController = Asset->GetModularRigModel().GetController();
	ModularController->OnModified().AddRaw(Asset.GetInterface(), &IControlRigAssetInterface::HandleRigModulesModified);
}

IControlRigAssetInterface::IControlRigAssetInterface()
{
	ModulesRecompilationBracket = 0;
}

void IControlRigAssetInterface::OnRegeneratedClass(UClass* ClassToRegenerate, UObject* PreviousCDO)
{
	GetHierarchy()->CleanupInvalidCaches();
	PropagateHierarchyFromBPToInstances();
}

bool IControlRigAssetInterface::RequiresForceLoadMembers(UObject* InObject) const
{
	// old assets don't support preload filtering
	if (GetObject()->GetLinkerCustomVersion(FControlRigObjectVersion::GUID) < FControlRigObjectVersion::RemoveParameters)
	{
		return RequiresForceLoadMembersSuper(InObject);
	}
	
	return GetRigVMAssetInterface()->IRigVMAssetInterface::RequiresForceLoadMembers(InObject);
}

void IControlRigAssetInterface::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// if this is any of our external variables we need to request construction so that the rig rebuilds itself
	TArray<FRigVMGraphVariableDescription> Variables = GetRigVMAssetInterface()->GetAssetVariables();
	if(Variables.ContainsByPredicate([&PropertyChangedEvent](const FRigVMGraphVariableDescription& Variable)
	{
		return Variable.Name == PropertyChangedEvent.GetMemberPropertyName();
	}))
	{
		if(UControlRig* DebuggedControlRig = Cast<UControlRig>(GetObjectBeingDebugged()))
		{
			if(const FProperty* PropertyOnRig = DebuggedControlRig->GetClass()->FindPropertyByName(PropertyChangedEvent.MemberProperty->GetFName()))
			{
				if(PropertyOnRig->SameType(PropertyChangedEvent.MemberProperty))
				{
					UControlRig* CDO = DebuggedControlRig->GetClass()->GetDefaultObject<UControlRig>();
					const uint8* SourceMemory = PropertyOnRig->ContainerPtrToValuePtr<uint8>(CDO);
					uint8* TargetMemory = PropertyOnRig->ContainerPtrToValuePtr<uint8>(DebuggedControlRig);
					PropertyOnRig->CopyCompleteValue(TargetMemory, SourceMemory);
				}
			}
			DebuggedControlRig->RequestConstruction();
		}
	}
}

void IControlRigAssetInterface::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	// Propagate shape libraries
	if (PropertyChangedEvent.Property && PropertyChangedEvent.Property->GetFName() == TEXT("ShapeLibraries"))
	{
		UClass* RigClass = GetRigVMAssetInterface()->GetRigVMGeneratedClass();
		UControlRig* CDO = Cast<UControlRig>(RigClass->GetDefaultObject(false /* create if needed */));

		TArray<UObject*> ArchetypeInstances;
		CDO->GetArchetypeInstances(ArchetypeInstances);
		ArchetypeInstances.Add(CDO);

		// Propagate libraries to archetypes
		for (UObject* Instance : ArchetypeInstances)
		{
			if (UControlRig* InstanceRig = Cast<UControlRig>(Instance))
			{
				InstanceRig->ShapeLibraries = GetShapeLibraries();
			}
		}
	}
}

UClass* IControlRigAssetInterface::GetControlRigClass() const
{
	return GetRigVMAssetInterface()->GetRigVMGeneratedClass();
}

bool IControlRigAssetInterface::IsModularRig() const
{
	if(const UClass* Class = GetControlRigClass())
	{
		return Class->IsChildOf(UModularRig::StaticClass());
	}
	return false;
}

UControlRig* IControlRigAssetInterface::CreateControlRig()
{
	return Cast<UControlRig>( GetRigVMAssetInterface()->CreateRigVMHostImpl());
}

USkeletalMesh* IControlRigAssetInterface::GetPreviewMesh() const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

#if WITH_EDITORONLY_DATA
	if (!GetPreviewSkeletalMesh().IsValid())
	{
		(void)GetPreviewSkeletalMesh().LoadSynchronous();
	}

	return GetPreviewSkeletalMesh().Get();
#else
	return nullptr;
#endif
}

bool IControlRigAssetInterface::IsControlRigModule() const
{
	return GetRigModuleSettings().Identifier.IsValid();
}

#if WITH_EDITORONLY_DATA

bool IControlRigAssetInterface::CanTurnIntoControlRigModule(bool InAutoConvertHierarchy, FString* OutErrorMessage) const
{
	if(IsControlRigModule())
	{
		if(OutErrorMessage)
		{
			static const FString Message = TEXT("This asset is already a Control Rig Module.");
			*OutErrorMessage = Message;
		}
		return false;
	}

	if (GetRigVMAssetInterface()->GetRigVMGeneratedClass()->IsChildOf<UModularRig>())
	{
		if(OutErrorMessage)
		{
			static const FString Message = TEXT("This asset is a Modular Rig.");
			*OutErrorMessage = Message;
		}
		return false;
	}

	if(GetHierarchy() == nullptr)
	{
		if(OutErrorMessage)
		{
			static const FString Message = TEXT("This asset contains no hierarchy.");
			*OutErrorMessage = Message;
		}
		return false;
	}

	const TArray<FRigElementKey> Keys = GetHierarchy()->GetAllKeys(true);
	for(const FRigElementKey& Key : Keys)
	{
		if(!InAutoConvertHierarchy)
		{
			if(Key.Type != ERigElementType::Bone &&
				Key.Type != ERigElementType::Curve &&
				Key.Type != ERigElementType::Connector)
			{
				if(OutErrorMessage)
				{
					static constexpr TCHAR Format[] = TEXT("The hierarchy contains elements other than bones (for example '%s'). Modules only allow imported bones and user authored connectors.");
					*OutErrorMessage = FString::Printf(Format, *Key.ToString());
				}
				return false;
			}

			if(Key.Type == ERigElementType::Bone)
			{
				if(GetHierarchy()->FindChecked<FRigBoneElement>(Key)->BoneType != ERigBoneType::Imported)
				{
					if(OutErrorMessage)
					{
						static constexpr TCHAR Format[] = TEXT("The hierarchy contains a user defined bone ('%s') - only imported bones are allowed.");
						*OutErrorMessage = FString::Printf(Format, *Key.ToString());
					}
					return false;
				}
			}
		}
	}
	
	return true;
}

bool IControlRigAssetInterface::TurnIntoControlRigModule(bool InAutoConvertHierarchy, FString* OutErrorMessage)
{
	if(!CanTurnIntoControlRigModule(InAutoConvertHierarchy, OutErrorMessage))
	{
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("TurnIntoControlRigModule", "Turn Rig into Module"));

	GetRigVMAssetInterface()->GetObject()->Modify();
	GetRigModuleSettings().Identifier = FRigModuleIdentifier();
	GetRigModuleSettings().Identifier.Name = GetRigVMAssetInterface()->GetObject()->GetName();

	if(GetHierarchy())
	{
		GetHierarchy()->Modify();
		URigHierarchyController* Controller = GetHierarchy()->GetController(true);

		// create a copy of this hierarchy
		URigHierarchy* CopyOfHierarchy = NewObject<URigHierarchy>(GetTransientPackage());
		CopyOfHierarchy->CopyHierarchy(GetHierarchy());

		// also create a hierarchy based on the preview mesh
		URigHierarchy* PreviewMeshHierarchy = NewObject<URigHierarchy>(GetTransientPackage());
		if(GetPreviewSkeletalMesh())
		{
			PreviewMeshHierarchy->GetController(true)->ImportBones(GetPreviewSkeletalMesh()->GetSkeleton());
			PreviewMeshHierarchy->GetController(true)->ImportSocketsFromSkeletalMesh(GetPreviewSkeletalMesh().Get(), NAME_None, false, false, false, false, false);;
		}

		// disable compilation
		{
			FRigVMBlueprintCompileScope CompileScope(GetRigVMAssetInterface());

			// remove everything from the hierarchy
			GetHierarchy()->Reset();

			const TArray<FRigElementKey> AllKeys = CopyOfHierarchy->GetAllKeys(true);
			TArray<FRigElementKey> KeysToSpawn;

			for(const FRigElementKey& Key : AllKeys)
			{
				if(Key.Type == ERigElementType::Curve)
				{
					continue;
				}
				if(Key.Type == ERigElementType::Bone)
				{
					if(PreviewMeshHierarchy->Contains(Key))
					{
						continue;
					}
				}
				if(Key.Type == ERigElementType::Null)
				{
					// if this is a mesh socket based null
					if(PreviewMeshHierarchy->Contains(Key))
					{
						continue;
					}
				}
				KeysToSpawn.Add(Key);
			}

			(void)ConvertHierarchyElementsToSpawnerNodes(CopyOfHierarchy, KeysToSpawn, false);

			if(GetHierarchy()->Num(ERigElementType::Connector) == 0)
			{
				static const FName RootName = TEXT("Root");
				static const FString RootDescription = TEXT("This is the default temporary socket used for the root connection.");
				const FRigElementKey ConnectorKey = Controller->AddConnector(RootName);
				const FRigElementKey SocketKey = Controller->AddSocket(RootName, FRigElementKey(), FTransform::Identity, false, FRigSocketElement::SocketDefaultColor, RootDescription, false);
				(void)ResolveConnector(ConnectorKey, SocketKey);
			}
		}
	}

	OnRigTypeChangedDelegate.Broadcast(GetControlRigAssetInterface().GetObject());
	return true;
}

bool IControlRigAssetInterface::CanTurnIntoStandaloneRig(FString* OutErrorMessage) const
{
	return IsControlRigModule();
}

bool IControlRigAssetInterface::TurnIntoStandaloneRig(FString* OutErrorMessage)
{
	if(!CanTurnIntoStandaloneRig(OutErrorMessage))
	{
		return false;
	}

	FScopedTransaction Transaction(LOCTEXT("TurnIntoStandaloneRig", "Turn Module into Rig"));

	GetObject()->Modify();
	GetRigModuleSettings() = FRigModuleSettings();

	if(GetHierarchy())
	{
		GetHierarchy()->Modify();
		GetHierarchy()->Reset();
		if(GetPreviewSkeletalMesh())
		{
			GetHierarchy()->GetController(true)->ImportBones(GetPreviewSkeletalMesh()->GetSkeleton());
			GetHierarchy()->GetController(true)->ImportSocketsFromSkeletalMesh(GetPreviewSkeletalMesh().Get(), NAME_None, false, false, false, false, false);
		}
	}

	OnRigTypeChangedDelegate.Broadcast(GetControlRigAssetInterface()->GetObject());
	return true;
}

TArray<URigVMNode*> IControlRigAssetInterface::ConvertHierarchyElementsToSpawnerNodes(URigHierarchy* InHierarchy, TArray<FRigElementKey> InKeys, bool bRemoveElements)
{
	TArray<URigVMNode*> SpawnerNodes;

	// find the construction event 
	const URigVMNode* EventNode = nullptr;
	for(const URigVMGraph* Graph : GetRigVMClient()->GetAllModels(false, false))
	{
		for(const URigVMNode* Node : Graph->GetNodes())
		{
			if(Node->IsEvent() && Node->GetEventName() == FRigUnit_PrepareForExecution::EventName)
			{
				EventNode = Node;
				break;
			}
		}
		if(EventNode)
		{
			break;
		}
	}

	FVector2D NodePosition = FVector2D::ZeroVector;
	const FVector2D NodePositionIncrement = FVector2D(400, 0);
	
	// if we didn't find the construction event yet, create it
	if(EventNode == nullptr)
	{
		const URigVMGraph* ConstructionGraph = GetRigVMClient()->AddModel(TEXT("ConstructionGraph"), true);
		URigVMController* GraphController = GetRigVMClient()->GetOrCreateController(ConstructionGraph);
		EventNode = GraphController->AddUnitNode(FRigUnit_PrepareForExecution::StaticStruct(), FRigUnit::GetMethodName(), NodePosition);
		NodePosition += NodePositionIncrement;
	}

	const URigVMPin* LastPin = EventNode->FindExecutePin();
	if(LastPin)
	{
		// follow the node's execution links to find the last one
		bool bCarryOn = true;
		while(bCarryOn)
		{
			static const TArray<FString> ExecutePinPaths = {
				FRigVMStruct::ControlFlowCompletedName.ToString(),
				FRigVMStruct::ExecuteContextName.ToString()
			};

			for(const FString& ExecutePinPath : ExecutePinPaths)
			{
				if(const URigVMPin* ExecutePin = LastPin->GetNode()->FindPin(ExecutePinPath))
				{
					const TArray<URigVMPin*> TargetPins = ExecutePin->GetLinkedTargetPins();
					if(TargetPins.IsEmpty())
					{
						bCarryOn = false;
						break;
					}
					LastPin = TargetPins[0];
					NodePosition = LastPin->GetNode()->GetPosition() + NodePositionIncrement;
				}
			}
		}
	}

	const URigVMGraph* ConstructionGraph = EventNode->GetGraph();
	URigVMController* GraphController = GetRigVMClient()->GetOrCreateController(ConstructionGraph);

	auto GetParentAndTransformDefaults = [InHierarchy](const FRigElementKey& InKey, FString& OutParentDefault, FString& OutTransformDefault)
	{
		const FRigElementKey Parent = InHierarchy->GetFirstParent(InKey);
		OutParentDefault.Reset();
		FRigElementKey::StaticStruct()->ExportText(OutParentDefault, &Parent, nullptr, nullptr, PPF_None, nullptr);

		const FTransform Transform = InHierarchy->GetInitialLocalTransform(InKey);
		OutTransformDefault.Reset();
		TBaseStructure<FTransform>::Get()->ExportText(OutTransformDefault, &Transform, nullptr, nullptr, PPF_None, nullptr);
	};

	TMap<FRigElementKey, const URigVMPin*> ParentItemPinMap;
	auto AddParentItemLink = [GraphController, InHierarchy, &SpawnerNodes, &ParentItemPinMap]
		(const FRigElementKey& Key, URigVMNode* Node)
		{
			SpawnerNodes.Add(Node);
			ParentItemPinMap.Add(Key, Node->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddElement, Item)));

			if(const URigVMPin** SourcePin = ParentItemPinMap.Find(InHierarchy->GetFirstParent(Key)))
			{
				if(const URigVMPin* TargetPin = Node->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddElement, Parent)))
				{
					GraphController->AddLink((*SourcePin)->GetPinPath(), TargetPin->GetPinPath(), true);
				}
			}
		};
	
	for(const FRigElementKey& Key : InKeys)
	{
		if(Key.Type == ERigElementType::Bone)
		{
			FString ParentDefault, TransformDefault;
			GetParentAndTransformDefaults(Key, ParentDefault, TransformDefault);

			URigVMNode* AddBoneNode = GraphController->AddUnitNode(FRigUnit_HierarchyAddBone::StaticStruct(), FRigUnit::GetMethodName(), NodePosition);
			NodePosition += NodePositionIncrement;
			AddParentItemLink(Key, AddBoneNode);

			if(LastPin)
			{
				if(const URigVMPin* NextPin = AddBoneNode->FindExecutePin())
				{
					GraphController->AddLink(LastPin->GetPinPath(), NextPin->GetPinPath(), true);
					LastPin = NextPin;
				}
			}

			GraphController->SetPinDefaultValue(AddBoneNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddElement, Name))->GetPinPath(), Key.Name.ToString(), true, true);
			GraphController->SetPinDefaultValue(AddBoneNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddElement, Parent))->GetPinPath(), ParentDefault, true, true);
			GraphController->SetPinDefaultValue(AddBoneNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddBone, Space))->GetPinPath(), TEXT("LocalSpace"), true, true);
			GraphController->SetPinDefaultValue(AddBoneNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddBone, Transform))->GetPinPath(), TransformDefault, true, true);
		}
		else if(Key.Type == ERigElementType::Null)
		{
			FString ParentDefault, TransformDefault;
			GetParentAndTransformDefaults(Key, ParentDefault, TransformDefault);

			URigVMNode* AddNullNode = GraphController->AddUnitNode(FRigUnit_HierarchyAddNull::StaticStruct(), FRigUnit::GetMethodName(), NodePosition);
			NodePosition += NodePositionIncrement;
			AddParentItemLink(Key, AddNullNode);
			SpawnerNodes.Add(AddNullNode);

			if(LastPin)
			{
				if(const URigVMPin* NextPin = AddNullNode->FindExecutePin())
				{
					GraphController->AddLink(LastPin->GetPinPath(), NextPin->GetPinPath(), true);
					LastPin = NextPin;
				}
			}

			GraphController->SetPinDefaultValue(AddNullNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddElement, Name))->GetPinPath(), Key.Name.ToString(), true, true);
			GraphController->SetPinDefaultValue(AddNullNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddElement, Parent))->GetPinPath(), ParentDefault, true, true);
			GraphController->SetPinDefaultValue(AddNullNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddNull, Space))->GetPinPath(), TEXT("LocalSpace"), true, true);
			GraphController->SetPinDefaultValue(AddNullNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddNull, Transform))->GetPinPath(), TransformDefault, true, true);
		}
		else if(Key.Type == ERigElementType::Control)
		{
			FRigControlElement* ControlElement = InHierarchy->FindChecked<FRigControlElement>(Key);
			
			FString ParentDefault, TransformDefault;
			GetParentAndTransformDefaults(Key, ParentDefault, TransformDefault);

			const FTransform OffsetTransform = InHierarchy->GetControlOffsetTransform(ControlElement, ERigTransformType::InitialLocal);
			FString OffsetDefault;
			TBaseStructure<FTransform>::Get()->ExportText(OffsetDefault, &OffsetTransform, nullptr, nullptr, PPF_None, nullptr);
			
			if(ControlElement->Settings.AnimationType == ERigControlAnimationType::AnimationChannel)
			{
				UScriptStruct* UnitNodeStruct = nullptr;
				TRigVMTypeIndex TypeIndex = INDEX_NONE;
				FString InitialValue, MinimumValue, MaximumValue, SettingsValue;
				switch(ControlElement->Settings.ControlType)
				{
					case ERigControlType::Bool:
					{
						UnitNodeStruct = FRigUnit_HierarchyAddAnimationChannelBool::StaticStruct();
						TypeIndex = RigVMTypeUtils::TypeIndex::Bool;
						InitialValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Initial).ToString<float>();
						MinimumValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Minimum).ToString<float>();
						MaximumValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Maximum).ToString<float>();
						break;
					}
					case ERigControlType::Float:
					{
						UnitNodeStruct = FRigUnit_HierarchyAddAnimationChannelFloat::StaticStruct();
						TypeIndex = RigVMTypeUtils::TypeIndex::Float;
						InitialValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Initial).ToString<float>();
						MinimumValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Minimum).ToString<float>();
						MaximumValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Maximum).ToString<float>();

						if(ControlElement->Settings.LimitEnabled.Num() == 1)
						{
							FRigUnit_HierarchyAddAnimationChannelSingleLimitSettings Settings;
							Settings.Enabled = ControlElement->Settings.LimitEnabled[0];
							FRigUnit_HierarchyAddAnimationChannelSingleLimitSettings::StaticStruct()->ExportText(SettingsValue, &Settings, &Settings, nullptr, PPF_None, nullptr);
						}
						break;
					}
					case ERigControlType::ScaleFloat:
					{
						UnitNodeStruct = FRigUnit_HierarchyAddAnimationChannelScaleFloat::StaticStruct();
						TypeIndex = RigVMTypeUtils::TypeIndex::Float;
						InitialValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Initial).ToString<float>();
						MinimumValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Minimum).ToString<float>();
						MaximumValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Maximum).ToString<float>();

						if(ControlElement->Settings.LimitEnabled.Num() == 1)
						{
							FRigUnit_HierarchyAddAnimationChannelSingleLimitSettings Settings;
							Settings.Enabled = ControlElement->Settings.LimitEnabled[0];
							FRigUnit_HierarchyAddAnimationChannelSingleLimitSettings::StaticStruct()->ExportText(SettingsValue, &Settings, &Settings, nullptr, PPF_None, nullptr);
						}
						break;
					}
					case ERigControlType::Integer:
					{
						UnitNodeStruct = FRigUnit_HierarchyAddAnimationChannelInteger::StaticStruct();
						TypeIndex = RigVMTypeUtils::TypeIndex::Int32; 
						InitialValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Initial).ToString<int32>();
						MinimumValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Minimum).ToString<int32>();
						MaximumValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Maximum).ToString<int32>();

						if(ControlElement->Settings.LimitEnabled.Num() == 1)
						{
							FRigUnit_HierarchyAddAnimationChannelSingleLimitSettings Settings;
							Settings.Enabled = ControlElement->Settings.LimitEnabled[0];
							FRigUnit_HierarchyAddAnimationChannelSingleLimitSettings::StaticStruct()->ExportText(SettingsValue, &Settings, &Settings, nullptr, PPF_None, nullptr);
						}
						break;
					}
					case ERigControlType::Vector2D:
					{
						UnitNodeStruct = FRigUnit_HierarchyAddAnimationChannelVector2D::StaticStruct();
						TypeIndex = FRigVMRegistry::Get().GetTypeIndex<FVector2D>(); 
						InitialValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Initial).ToString<FVector2D>();
						MinimumValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Minimum).ToString<FVector2D>();
						MaximumValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Maximum).ToString<FVector2D>();

						if(ControlElement->Settings.LimitEnabled.Num() == 2)
						{
							FRigUnit_HierarchyAddAnimationChannel2DLimitSettings Settings;
							Settings.X = ControlElement->Settings.LimitEnabled[0];
							Settings.Y = ControlElement->Settings.LimitEnabled[1];
							FRigUnit_HierarchyAddAnimationChannel2DLimitSettings::StaticStruct()->ExportText(SettingsValue, &Settings, &Settings, nullptr, PPF_None, nullptr);
						}
						break;
					}
					case ERigControlType::Position:
					{
						UnitNodeStruct = FRigUnit_HierarchyAddAnimationChannelVector::StaticStruct();
						TypeIndex = FRigVMRegistry::Get().GetTypeIndex<FVector>(); 
						InitialValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Initial).ToString<FVector>();
						MinimumValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Minimum).ToString<FVector>();
						MaximumValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Maximum).ToString<FVector>();

						if(ControlElement->Settings.LimitEnabled.Num() == 3)
						{
							FRigUnit_HierarchyAddAnimationChannelVectorLimitSettings Settings;
							Settings.X = ControlElement->Settings.LimitEnabled[0];
							Settings.Y = ControlElement->Settings.LimitEnabled[1];
							Settings.Z = ControlElement->Settings.LimitEnabled[2];
							FRigUnit_HierarchyAddAnimationChannelVectorLimitSettings::StaticStruct()->ExportText(SettingsValue, &Settings, &Settings, nullptr, PPF_None, nullptr);
						}
						break;
					}
					case ERigControlType::Scale:
					{
						UnitNodeStruct = FRigUnit_HierarchyAddAnimationChannelScaleVector::StaticStruct();
						TypeIndex = FRigVMRegistry::Get().GetTypeIndex<FVector>(); 
						InitialValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Initial).ToString<FVector>();
						MinimumValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Minimum).ToString<FVector>();
						MaximumValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Maximum).ToString<FVector>();

						if(ControlElement->Settings.LimitEnabled.Num() == 3)
						{
							FRigUnit_HierarchyAddAnimationChannelVectorLimitSettings Settings;
							Settings.X = ControlElement->Settings.LimitEnabled[0];
							Settings.Y = ControlElement->Settings.LimitEnabled[1];
							Settings.Z = ControlElement->Settings.LimitEnabled[2];
							FRigUnit_HierarchyAddAnimationChannelVectorLimitSettings::StaticStruct()->ExportText(SettingsValue, &Settings, &Settings, nullptr, PPF_None, nullptr);
						}
						break;
					}
					case ERigControlType::Rotator:
					{
						UnitNodeStruct = FRigUnit_HierarchyAddAnimationChannelRotator::StaticStruct();
						TypeIndex = FRigVMRegistry::Get().GetTypeIndex<FRotator>(); 
						InitialValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Initial).ToString<FRotator>();
						MinimumValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Minimum).ToString<FRotator>();
						MaximumValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Maximum).ToString<FRotator>();

						if(ControlElement->Settings.LimitEnabled.Num() == 3)
						{
							FRigUnit_HierarchyAddAnimationChannelRotatorLimitSettings Settings;
							Settings.Pitch = ControlElement->Settings.LimitEnabled[0];
							Settings.Yaw = ControlElement->Settings.LimitEnabled[1];
							Settings.Roll = ControlElement->Settings.LimitEnabled[2];
							FRigUnit_HierarchyAddAnimationChannelRotatorLimitSettings::StaticStruct()->ExportText(SettingsValue, &Settings, &Settings, nullptr, PPF_None, nullptr);
						}
						break;
					}
					default:
					{
						break;
					}
				}

				if(UnitNodeStruct == nullptr)
				{
					continue;
				}

				URigVMNode* AddControlNode = GraphController->AddUnitNode(UnitNodeStruct, FRigUnit::GetMethodName(), NodePosition);
				NodePosition += NodePositionIncrement;
				AddParentItemLink(Key, AddControlNode);

				if(LastPin)
				{
					if(const URigVMPin* NextPin = AddControlNode->FindExecutePin())
					{
						GraphController->AddLink(LastPin->GetPinPath(), NextPin->GetPinPath(), true);
						LastPin = NextPin;
					}
				}

				GraphController->ResolveWildCardPin(AddControlNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddAnimationChannelFloat, InitialValue))->GetPinPath(), TypeIndex, true);
				GraphController->SetPinDefaultValue(AddControlNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddAnimationChannelFloat, Name))->GetPinPath(), Key.Name.ToString(), true, true);
				GraphController->SetPinDefaultValue(AddControlNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddAnimationChannelFloat, Parent))->GetPinPath(), ParentDefault, true, true);
				GraphController->SetPinDefaultValue(AddControlNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddAnimationChannelFloat, InitialValue))->GetPinPath(), InitialValue, true, true);
				GraphController->SetPinDefaultValue(AddControlNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddAnimationChannelFloat, MinimumValue))->GetPinPath(), MinimumValue, true, true);
				GraphController->SetPinDefaultValue(AddControlNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddAnimationChannelFloat, MaximumValue))->GetPinPath(), MaximumValue, true, true);

				if(!SettingsValue.IsEmpty())
				{
					GraphController->SetPinDefaultValue(AddControlNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddAnimationChannelFloat, LimitsEnabled))->GetPinPath(), SettingsValue, true, true);
				}
			}
			else
			{
				UScriptStruct* UnitNodeStruct = nullptr;
				TRigVMTypeIndex TypeIndex = INDEX_NONE;
				FString InitialValue;
				switch(ControlElement->Settings.ControlType)
				{
					case ERigControlType::Float:
					case ERigControlType::ScaleFloat:
					{
						UnitNodeStruct = FRigUnit_HierarchyAddControlFloat::StaticStruct();
						TypeIndex = RigVMTypeUtils::TypeIndex::Float;
						InitialValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Initial).ToString<float>();
						break;
					}
					case ERigControlType::Integer:
					{
						UnitNodeStruct = FRigUnit_HierarchyAddControlInteger::StaticStruct();
						TypeIndex = RigVMTypeUtils::TypeIndex::Int32; 
						InitialValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Initial).ToString<int32>();
						break;
					}
					case ERigControlType::Vector2D:
					{
						UnitNodeStruct = FRigUnit_HierarchyAddControlVector2D::StaticStruct();
						TypeIndex = FRigVMRegistry::Get().GetTypeIndex<FVector2D>(); 
						InitialValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Initial).ToString<FVector2D>();
						break;
					}
					case ERigControlType::Position:
					case ERigControlType::Scale:
					{
						UnitNodeStruct = FRigUnit_HierarchyAddControlVector::StaticStruct();
						TypeIndex = FRigVMRegistry::Get().GetTypeIndex<FVector>(); 
						InitialValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Initial).ToString<FVector>();
						break;
					}
					case ERigControlType::Rotator:
					{
						UnitNodeStruct = FRigUnit_HierarchyAddControlRotator::StaticStruct();
						TypeIndex = FRigVMRegistry::Get().GetTypeIndex<FRotator>(); 
						InitialValue = InHierarchy->GetControlValue(Key, ERigControlValueType::Initial).ToString<FRotator>();
						break;
					}
					case ERigControlType::Transform:
					case ERigControlType::TransformNoScale:
					case ERigControlType::EulerTransform:
					{
						UnitNodeStruct = FRigUnit_HierarchyAddControlTransform::StaticStruct();
						TypeIndex = FRigVMRegistry::Get().GetTypeIndex<FTransform>(); 
						const FTransform InitialTransform = InHierarchy->GetInitialLocalTransform(Key);
						TBaseStructure<FTransform>::Get()->ExportText(InitialValue, &InitialTransform, nullptr, nullptr, PPF_None, nullptr);
						break;
					}
					default:
					{
						break;
					}
				}

				if(UnitNodeStruct == nullptr)
				{
					continue;
				}

				URigVMNode* AddControlNode = GraphController->AddUnitNode(UnitNodeStruct, FRigUnit::GetMethodName(), NodePosition);
				NodePosition += NodePositionIncrement;
				AddParentItemLink(Key, AddControlNode);

				if(LastPin)
				{
					if(const URigVMPin* NextPin = AddControlNode->FindExecutePin())
					{
						GraphController->AddLink(LastPin->GetPinPath(), NextPin->GetPinPath(), true);
						LastPin = NextPin;
					}
				}

				GraphController->ResolveWildCardPin(AddControlNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddControlInteger, InitialValue))->GetPinPath(), TypeIndex, true);
				GraphController->SetPinDefaultValue(AddControlNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddElement, Name))->GetPinPath(), Key.Name.ToString(), true, true);
				GraphController->SetPinDefaultValue(AddControlNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddElement, Parent))->GetPinPath(), ParentDefault, true, true);
				GraphController->SetPinDefaultValue(AddControlNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddControlElement, OffsetSpace))->GetPinPath(), TEXT("LocalSpace"), true, true);
				GraphController->SetPinDefaultValue(AddControlNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddControlElement, OffsetTransform))->GetPinPath(), OffsetDefault, true, true);
				GraphController->SetPinDefaultValue(AddControlNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddControlInteger, InitialValue))->GetPinPath(), InitialValue, true, true);

				if(const FStructProperty* SettingsProperty = CastField<FStructProperty>(UnitNodeStruct->FindPropertyByName(TEXT("Settings"))))
				{
					UScriptStruct* SettingsStruct = CastChecked<UScriptStruct>(SettingsProperty->Struct);
					FStructOnScope SettingsScope(SettingsStruct);
					FRigUnit_HierarchyAddControl_Settings* Settings = (FRigUnit_HierarchyAddControl_Settings*)SettingsScope.GetStructMemory();
					Settings->ConfigureFrom(ControlElement, ControlElement->Settings);
					FString SettingsDefault;
					SettingsStruct->ExportText(SettingsDefault, Settings, nullptr, nullptr, PPF_None, nullptr);

					GraphController->SetPinDefaultValue(AddControlNode->FindPin(SettingsProperty->GetName())->GetPinPath(), SettingsDefault, true, true);
				}
			}
		}
		else if(Key.Type == ERigElementType::Socket)
		{
			FString ParentDefault, TransformDefault;
			GetParentAndTransformDefaults(Key, ParentDefault, TransformDefault);

			URigVMNode* AddSocketNode = GraphController->AddUnitNode(FRigUnit_HierarchyAddSocket::StaticStruct(), FRigUnit::GetMethodName(), NodePosition);
			NodePosition += NodePositionIncrement;
			AddParentItemLink(Key, AddSocketNode);
			SpawnerNodes.Add(AddSocketNode);

			if(LastPin)
			{
				if(const URigVMPin* NextPin = AddSocketNode->FindExecutePin())
				{
					GraphController->AddLink(LastPin->GetPinPath(), NextPin->GetPinPath(), true);
					LastPin = NextPin;
				}
			}

			GraphController->SetPinDefaultValue(AddSocketNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddElement, Name))->GetPinPath(), Key.Name.ToString(), true, true);
			GraphController->SetPinDefaultValue(AddSocketNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddElement, Parent))->GetPinPath(), ParentDefault, true, true);
			GraphController->SetPinDefaultValue(AddSocketNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddNull, Space))->GetPinPath(), TEXT("LocalSpace"), true, true);
			GraphController->SetPinDefaultValue(AddSocketNode->FindPin(GET_MEMBER_NAME_STRING_CHECKED(FRigUnit_HierarchyAddNull, Transform))->GetPinPath(), TransformDefault, true, true);
		}
	}

	if(bRemoveElements && InHierarchy)
	{
		InHierarchy->Modify();
		for(const FRigElementKey& Key : InKeys)
		{
			InHierarchy->GetController(true)->RemoveElement(Key, true);
		}
	}

	return SpawnerNodes;
}

#endif // WITH_EDITORONLY_DATA

UTexture2D* IControlRigAssetInterface::GetRigModuleIcon() const
{
	if(IsControlRigModule())
	{
		if(UTexture2D* Icon = Cast<UTexture2D>(GetRigModuleSettings().Icon.TryLoad()))
		{
			return Icon;
		}
	}
	return nullptr;
}

UControlRig* IControlRigAssetInterface::GetDebuggedControlRig()
{
	return Cast<UControlRig>(GetRigVMAssetInterface()->GetDebuggedRigVMHost());
}

void IControlRigAssetInterface::SetPreviewMesh(USkeletalMesh* PreviewMesh, bool bMarkAsDirty/*=true*/)
{
#if WITH_EDITORONLY_DATA
	if(bMarkAsDirty)
	{
		Modify();
	}

	GetPreviewSkeletalMesh() = PreviewMesh;

	if(IsControlRigModule())
	{
		GetSourceHierarchyImport().Reset();
		GetSourceCurveImport().Reset();
	}
#endif
}

void IControlRigAssetInterface::Serialize(FArchive& Ar)
{
	UE_RIGVM_ARCHIVETRACE_SCOPE(Ar, FString::Printf(TEXT("IControlRigAssetInterface(%s)"), *GetName()));

	if(IsValidChecked(GetObject()))
	{
		GetRigVMClient()->SetOuterClientHost(GetObject(), TEXT("RigVMClient"));
		GetModularRigModel().SetOuterClientHost(GetObject());
	}
	
	GetRigVMAssetInterface()->IRigVMAssetInterface::SerializeImpl(Ar);
	UE_RIGVM_ARCHIVETRACE_ENTRY(Ar, TEXT("Super::Serialize"));

	if(Ar.IsObjectReferenceCollector())
	{
		Ar.UsingCustomVersion(FControlRigObjectVersion::GUID);


#if WITH_EDITORONLY_DATA
		if (Ar.IsCooking() && GetRigVMAssetInterface()->IsReferencedObjectPathsStored())
		{
			for (FSoftObjectPath ObjectPath : GetRigVMAssetInterface()->GetReferencedObjectPaths())
			{
				ObjectPath.Serialize(Ar);
			}
		}
		else
#endif
		{
			TArray<IRigVMGraphFunctionHost*> ReferencedFunctionHosts = GetRigVMAssetInterface()->GetReferencedFunctionHosts(false);

			for(IRigVMGraphFunctionHost* ReferencedFunctionHost : ReferencedFunctionHosts)
			{
				if (URigVMBlueprintGeneratedClass* BPGeneratedClass = Cast<URigVMBlueprintGeneratedClass>(ReferencedFunctionHost))
				{
					Ar << BPGeneratedClass;
				}
			}

			for(const TSoftObjectPtr<UControlRigShapeLibrary>& ShapeLibraryPtr : GetShapeLibraries())
			{
				if(ShapeLibraryPtr.IsValid())
				{
					UControlRigShapeLibrary* ShapeLibrary = ShapeLibraryPtr.Get();
					Ar << ShapeLibrary;
				}
			}
		}
	}

	if(Ar.IsLoading())
	{
		GetModularRigModel().UpdateCachedChildren();
		GetModularRigModel().Connections.UpdateFromConnectionList();
	}
}

UClass* IControlRigAssetInterface::GetBlueprintClass() const
{
	return GetRigVMAssetInterface()->GetRigVMGeneratedClassPrototype();
}

void IControlRigAssetInterface::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	// make sure to save the VM with high performance settings
	// so that during cooking we reach small footprints.
	// these settings may have changed during the user session.
	GetRigVMAssetInterface()->GetVMCompileSettings().ASTSettings.bFoldAssignments = true;
	GetRigVMAssetInterface()->GetVMCompileSettings().ASTSettings.bFoldLiterals = true;

	GetExposesAnimatableControls() = false;
	GetHierarchy()->ForEach<FRigControlElement>([this](FRigControlElement* ControlElement) -> bool
    {
		if (GetHierarchy()->IsAnimatable(ControlElement))
		{
			GetExposesAnimatableControls() = true;
			return false;
		}
		return true;
	});

	if(IsControlRigModule())
	{
		URigHierarchy* DebuggedHierarchy = GetHierarchy();
		if(UControlRig* DebuggedRig = Cast<UControlRig>(GetObjectBeingDebugged()))
		{
			DebuggedHierarchy = DebuggedRig->GetHierarchy();
		}

		TGuardValue<bool> SuspendNotifGuard(GetHierarchy()->GetSuspendNotificationsFlag(), true);
		TGuardValue<bool> SuspendNotifGuardOnDebuggedHierarchy(DebuggedHierarchy->GetSuspendNotificationsFlag(), true);

		UpdateExposedModuleConnectors();

		GetSourceHierarchyImport().Reset();
		GetSourceCurveImport().Reset();
	}

	if (IsControlRigModule())
	{
		GetControlRigType() = EControlRigType::RigModule;
		GetItemTypeDisplayName() = TEXT("Rig Module");
		GetCustomThumbnail() = GetRigModuleSettings().Icon.ToString();
	}
	else if (GetControlRigClass()->IsChildOf(UModularRig::StaticClass()))
	{
		GetControlRigType() = EControlRigType::ModularRig;
		GetItemTypeDisplayName() = TEXT("Modular Rig");
	}
	else
	{
		GetControlRigType() = EControlRigType::IndependentRig;
		GetItemTypeDisplayName() = TEXT("Control Rig");
	}

	if (IsModularRig())
	{
		GetModuleReferenceData() = GetModuleReferenceDataImpl();
		IAssetRegistry::GetChecked().AssetTagsFinalized(*GetObject());
	}
}

TArray<FModuleReferenceData> IControlRigAssetInterface::FindReferencesToModule() const
{
	TArray<FModuleReferenceData> Result;
	if (!IsControlRigModule())
	{
		return Result;
	}

	const UClass* RigModuleClass = GetControlRigClass();
	if (!RigModuleClass)
	{
		return Result;
	}

	// Load the asset registry module
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	// Collect a full list of assets with the control rig class
	TArray<FAssetData> AssetDataList;
	AssetRegistryModule.Get().GetAssetsByClass(UControlRigAssetInterface::StaticClass()->GetClassPathName(), AssetDataList, true);

	static const FLazyName ModuleReferenceDataName(TEXT("ModuleReferenceData"));

	for(const FAssetData& AssetData : AssetDataList)
	{
		UClass* AssetClass = FindObject<UClass>(AssetData.AssetClassPath);
		if (!AssetClass)
		{
			continue;
		}

		FArrayProperty* ModuleReferenceDataProperty = CastField<FArrayProperty>(AssetClass->StaticClass()->FindPropertyByName(ModuleReferenceDataName));
		if (!ModuleReferenceDataProperty)
		{
			continue;
		}
		
		// Check only modular rigs
		if (IControlRigAssetInterface::GetRigType(AssetData) != EControlRigType::ModularRig)
		{
			continue;
		}
		
		const FString ModularRigDataString = AssetData.GetTagValueRef<FString>(ModuleReferenceDataName);
		if (ModularRigDataString.IsEmpty())
		{
			continue;
		}

		TArray<FModuleReferenceData> Modules;
		ModuleReferenceDataProperty->ImportText_Direct(*ModularRigDataString, &Modules, nullptr, EPropertyPortFlags::PPF_None);

		for (FModuleReferenceData& Module : Modules)
		{
			if (Module.ReferencedModule == RigModuleClass)
			{
				Result.Add(Module);
			}
		}
	}

	return Result;
}

EControlRigType IControlRigAssetInterface::GetRigType(const FAssetData& InAsset)
{
	EControlRigType Result = EControlRigType::MAX;
	static const FLazyName ControlRigTypeName( TEXT("ControlRigType"));

	UClass* AssetClass = FindObject<UClass>(InAsset.AssetClassPath);
	if (!AssetClass)
	{
		return Result;
	}

	FProperty* ControlRigTypeProperty = CastField<FProperty>(AssetClass->FindPropertyByName(ControlRigTypeName));
	if (!ControlRigTypeProperty)
	{
		return Result;
	}
	
	const FString ControlRigTypeString = InAsset.GetTagValueRef<FString>(ControlRigTypeName);
	if (ControlRigTypeString.IsEmpty())
	{
		return Result;
	}

	EControlRigType RigType;
	ControlRigTypeProperty->ImportText_Direct(*ControlRigTypeString, &RigType, nullptr, EPropertyPortFlags::PPF_None);
	return RigType;
}

TArray<FSoftObjectPath> IControlRigAssetInterface::GetReferencesToRigModule(const FAssetData& InModuleAsset)
{
	TArray<FSoftObjectPath> Result;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.GetRegistry();
	
	TArray<FName> PackageDependencies;
	AssetRegistry.GetReferencers(InModuleAsset.PackageName, PackageDependencies);

	for (FName& DependencyPath : PackageDependencies)
	{
		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByPackageName(DependencyPath, Assets);

		for (const FAssetData& DependencyData : Assets)
		{
			if (DependencyData.IsAssetLoaded())
			{
				if (IControlRigAssetInterface* Blueprint = Cast<IControlRigAssetInterface>(DependencyData.GetAsset()))
				{
					if (Blueprint->IsModularRig())
					{
						TArray<const FRigModuleReference*> Modules = Blueprint->GetModularRigModel().FindModuleInstancesOfClass(InModuleAsset);
						for (const FRigModuleReference* Module : Modules)
						{
							FSoftObjectPath ModulePath = DependencyData.GetSoftObjectPath();
							ModulePath.SetSubPathString(Module->GetModulePath().GetPath());
							Result.Add(ModulePath);
						}
					}
				}
				else if (UControlRigBlueprintGeneratedClass* Generated = Cast<UControlRigBlueprintGeneratedClass>(DependencyData.GetAsset()))
				{
					if (Generated->ControlRigType == EControlRigType::ModularRig)
					{
						for (FModuleReferenceData& Module : Generated->ModuleReferenceData)
						{
							if (Module.ReferencedModule == InModuleAsset.GetClass())
							{
								FSoftObjectPath ModulePath = DependencyData.GetSoftObjectPath();
								ModulePath.SetSubPathString(Module.ModulePath);
								Result.Add(ModulePath);
							}
						}
					}
				}
			}
			else
			{
				// Check only modular rigs
				if (IControlRigAssetInterface::GetRigType(DependencyData) != EControlRigType::ModularRig)
				{
					continue;
				}

				UClass* AssetClass = FindObject<UClass>(DependencyData.AssetClassPath);
				if (!AssetClass)
				{
					continue;
				}

				static const FLazyName ModuleReferenceDataName(TEXT("ModuleReferenceData"));
				FArrayProperty* ModuleReferenceDataProperty = CastField<FArrayProperty>(AssetClass->StaticClass()->FindPropertyByName(ModuleReferenceDataName));
				if (!ModuleReferenceDataProperty)
				{
					continue;
				}
				const FString ModularRigDataString = DependencyData.GetTagValueRef<FString>(ModuleReferenceDataName);
				if (ModularRigDataString.IsEmpty())
				{
					continue;
				}

				

				TArray<FModuleReferenceData> Modules;
				ModuleReferenceDataProperty->ImportText_Direct(*ModularRigDataString, &Modules, nullptr, EPropertyPortFlags::PPF_None);

				for (const FModuleReferenceData& Module : Modules)
				{
					FTopLevelAssetPath ModulePath = Module.ReferencedModule.GetAssetPath();
					FString AssetName = ModulePath.GetAssetName().ToString();
					AssetName.RemoveFromEnd(TEXT("_C"));
					ModulePath = FTopLevelAssetPath(ModulePath.GetPackageName(), *AssetName);
					if (ModulePath == InModuleAsset.GetSoftObjectPath().GetAssetPath())
					{
						FSoftObjectPath ResultModulePath = DependencyData.GetSoftObjectPath();
						ResultModulePath.SetSubPathString(Module.ModulePath);
						Result.Add(ResultModulePath);
					}
				}
			}
		}
	}
	
	return Result;
}

TArray<FModuleReferenceData> IControlRigAssetInterface::GetModuleReferenceDataImpl() const
{
	TArray<FModuleReferenceData> Result;
	Result.Reserve(GetModularRigModel().Modules.Num());
	GetModularRigModel().ForEachModule([&Result](const FRigModuleReference* Module) -> bool
	{
		Result.Add(Module);
		return true;
	});
	return Result;
}

void IControlRigAssetInterface::UpdateExposedModuleConnectors() const
{
	IControlRigAssetInterface* MutableThis = ((IControlRigAssetInterface*)this);
	MutableThis->GetRigModuleSettings().ExposedConnectors.Reset();
	GetHierarchy()->ForEach<FRigConnectorElement>([MutableThis, this](const FRigConnectorElement* ConnectorElement) -> bool
	{
		const FRigElementKey ConnectorKey = ConnectorElement->GetKey().ConvertToModuleNameFormat(&GetModularRigModel().PreviousModulePaths);
		
		FRigModuleConnector ExposedConnector;
		ExposedConnector.Name = ConnectorKey.Name.ToString();
		ExposedConnector.Settings = ConnectorElement->Settings;
		MutableThis->GetRigModuleSettings().ExposedConnectors.Add(ExposedConnector);
		return true;
	});
	PropagateHierarchyFromBPToInstances();
}

#if WITH_EDITOR

TArray<FOverrideStatusSubject> IControlRigAssetInterface::GetOverrideSubjects() const
{
	TArray<FOverrideStatusSubject> Subjects;

	if(const UModularRig* DebuggedRig = Cast<UModularRig>(GetObjectBeingDebugged()))
	{
		GetModularRigModel().ForEachModule([&Subjects, DebuggedRig](const FRigModuleReference* ModuleReference) -> bool
		{
			if(const FRigModuleInstance* ModuleInstance = DebuggedRig->FindModule(ModuleReference->Name))
			{
				if(const UControlRig* ModuleRig = ModuleInstance->GetRig())
				{
					for(const FControlRigOverrideValue& Override : ModuleReference->ConfigOverrides)
					{
						Subjects.Add({ModuleRig, Override.ToPropertyPath()});
					}
				}
			}
			return true;
		});
	}

	return Subjects;
}

uint32 IControlRigAssetInterface::GetOverrideSubjectsHash() const
{
	uint32 Hash = 0;
	
	GetModularRigModel().ForEachModule([&Hash](const FRigModuleReference* ModuleReference) -> bool
	{
		Hash = HashCombine(Hash, GetTypeHash(ModuleReference->Name));
		Hash = HashCombine(Hash, GetTypeHash(ModuleReference->ConfigOverrides));
		return true;
	});

	return Hash;
}

FName IControlRigAssetInterface::FindHostMemberVariableUniqueName(TSharedPtr<FRigVMNameValidator> InNameValidator, const FString& InBaseName)
{
	FString BaseName = InBaseName;
	if (InNameValidator->IsValid(BaseName, false) == EValidatorResult::ContainsInvalidCharacters)
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
	while (InNameValidator->IsValid(KismetName, false) != EValidatorResult::Ok)
	{
		KismetName = FString::Printf(TEXT("%s_%d"), *BaseName, Suffix);
		Suffix++;
	}


	return *KismetName;
}

#endif

bool IControlRigAssetInterface::ResolveConnector(const FRigElementKey& DraggedKey, const FRigElementKey& TargetKey, bool bSetupUndoRedo)
{
	return ResolveConnectorToArray(DraggedKey, {TargetKey}, bSetupUndoRedo);
}

bool IControlRigAssetInterface::ResolveConnectorToArray(const FRigElementKey& DraggedKey, const TArray<FRigElementKey>& TargetKeys, bool bSetupUndoRedo)
{
	FScopedTransaction Transaction(LOCTEXT("ResolveConnector", "Resolve connector"));

	if(bSetupUndoRedo)
	{
		Modify();
	}

	TArray<FRigElementKey> FilteredKeys = TargetKeys;
	FilteredKeys.RemoveAll([](const FRigElementKey& Key)
	{
		return !Key.IsValid();
	});
	
	if(!FilteredKeys.IsEmpty())
	{
		FRigElementKeyCollection& ExistingTargetKeys = GetArrayConnectionMap().FindOrAdd(DraggedKey);

		if(ExistingTargetKeys.Num() == FilteredKeys.Num())
		{
			bool bCompleteMatch = true;
			for(int32 Index = 0; Index < ExistingTargetKeys.Num(); Index++)
			{
				if(ExistingTargetKeys[Index] != FilteredKeys[Index])
				{
					bCompleteMatch = false;
					break;
				}
			}
			if(bCompleteMatch)
			{
				return false;
			}
		}
		ExistingTargetKeys.Keys = FilteredKeys;

		if (IsModularRig())
		{
			// Add connection to the model
			if (UModularRigController* Controller = GetModularRigController())
			{
				Controller->ConnectConnectorToElements(DraggedKey, FilteredKeys, bSetupUndoRedo, GetModularRigSettings().bAutoResolve);
			}
		}
		else
		{
			GetArrayConnectionMap().FindOrAdd(DraggedKey) = ExistingTargetKeys;
		}
	}
	else
	{
		if (IsModularRig())
		{
			// Add connection to the model
			if (UModularRigController* Controller = GetModularRigController())
			{
				Controller->DisconnectConnector(DraggedKey, false, bSetupUndoRedo);
			}
		}
		else
		{
			GetArrayConnectionMap().Remove(DraggedKey);
		}
	}

	RecompileModularRig();

	PropagateHierarchyFromBPToInstances();

	if(UControlRig* ControlRig = Cast<UControlRig>(GetObjectBeingDebugged()))
	{
		for (UEdGraph* Graph : GetRigVMAssetInterface()->GetUberGraphs())
		{
			UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph);
			if (RigGraph == nullptr)
			{
				continue;
			}
			RigGraph->CacheNameLists(ControlRig->GetHierarchy(), &GetDrawContainer(), GetShapeLibraries());
		}
	}

	return true;
}

void IControlRigAssetInterface::UpdateConnectionMapFromModel()
{
	if (IsModularRig())
	{
		GetArrayConnectionMap().Reset();

		for (const FModularRigSingleConnection& Connection : GetModularRigModel().Connections)
		{
			GetArrayConnectionMap().Add(Connection.Connector, {Connection.Targets});
		}
	}
}

void IControlRigAssetInterface::PostLoad()
{
	GetModularRigModel().PatchModelsOnLoad();
	
#if WITH_EDITOR
	if(IsControlRigModule() && GetHierarchy())
	{
		// backwards compat - makes sure to only ever allow one primary connector
		TArray<FRigConnectorElement*> Connectors = GetHierarchy()->GetConnectors();
		const int32 NumPrimaryConnectors = Algo::CountIf(Connectors, [](const FRigConnectorElement* InConnector) -> bool
		{
			return InConnector->IsPrimary();
		});
		if(NumPrimaryConnectors > 1)
		{
			bool bHasSeenPrimary = false;
			for(FRigConnectorElement* Connector : Connectors)
			{
				if(bHasSeenPrimary)
				{
					Connector->Settings.Type = EConnectorType::Secondary;
				}
				else
				{
					bHasSeenPrimary = Connector->IsPrimary();
				}
			}
			UpdateExposedModuleConnectors();
		}
	}
#endif

	// patch from previously used module paths to unique module names
	TMap<FRigElementKey, FRigElementKeyCollection> PreviousArrayConnectionMap;
	Swap(PreviousArrayConnectionMap, GetArrayConnectionMap());

	GetArrayConnectionMap().Reset();
	for(TPair<FRigElementKey, FRigElementKeyCollection>& Connection : PreviousArrayConnectionMap)
	{
		FRigElementKey Key = Connection.Key.ConvertToModuleNameFormat(&GetModularRigModel().PreviousModulePaths);
		FRigElementKeyCollection& Targets = Connection.Value;
		for(FRigElementKey& TargetKey : Targets.Keys)
		{
			TargetKey.ConvertToModuleNameFormatInline(&GetModularRigModel().PreviousModulePaths);
		}
		GetArrayConnectionMap().Add(Key, Targets);
	}

	UpdateModularDependencyDelegates();

	if(GetHierarchy())
	{
		GetHierarchy()->PatchElementMetadata(GetModularRigModel().PreviousModulePaths);
		GetHierarchy()->PatchModularRigComponentKeys(GetModularRigModel().PreviousModulePaths);
	}
}

#if WITH_EDITOR

void IControlRigAssetInterface::HandlePackageDone()
{
	if (ShapeLibrariesToLoadOnPackageLoaded.Num() > 0)
	{
		for(const FString& ShapeLibraryToLoadOnPackageLoaded : ShapeLibrariesToLoadOnPackageLoaded)
		{
			GetShapeLibraries().Add(LoadObject<UControlRigShapeLibrary>(nullptr, *ShapeLibraryToLoadOnPackageLoaded));
		}

		TArray<UObject*> ArchetypeInstances = GetRigVMAssetInterface()->GetArchetypeInstances(true, true);
		for (UObject* Instance : ArchetypeInstances)
		{
			if (UControlRig* InstanceRig = Cast<UControlRig>(Instance))
			{
				InstanceRig->ShapeLibraries = GetShapeLibraries();
			}
		}

		ShapeLibrariesToLoadOnPackageLoaded.Reset();
	}

	PropagateHierarchyFromBPToInstances();

	HandlePackageDoneSuper();
	
	if(IsModularRig())
	{
		// force load all dependencies
		GetModularRigModel().ForEachModule([](const FRigModuleReference* Element) -> bool
		{
			(void)Element->Class.LoadSynchronous();
			const_cast<FRigModuleReference*>(Element)->PatchModelsOnLoad();
			return true;
		});

		RecompileModularRig();
	}
}

void IControlRigAssetInterface::HandleConfigureRigVMController(const FRigVMClient* InClient, URigVMController* InControllerToConfigure)
{
	GetRigVMAssetInterface()->IRigVMAssetInterface::HandleConfigureRigVMController(InClient, InControllerToConfigure);

	TWeakObjectPtr<UObject> WeakThis(GetObject());
	InControllerToConfigure->ConfigureWorkflowOptionsDelegate.BindLambda([WeakThis](URigVMUserWorkflowOptions* Options)
	{
		if(UControlRigWorkflowOptions* ControlRigNodeWorkflowOptions = Cast<UControlRigWorkflowOptions>(Options))
		{
			ControlRigNodeWorkflowOptions->Hierarchy = nullptr;
			ControlRigNodeWorkflowOptions->Selection.Reset();

			if (UObject* StrongThis = WeakThis.Get())
			{
				if (StrongThis->Implements<URigVMAssetInterface>())
				{
					FRigVMAssetInterfacePtr Asset = StrongThis;
					if(UControlRig* ControlRig = Cast<UControlRig>(Asset->GetObjectBeingDebugged()))
					{
						ControlRigNodeWorkflowOptions->Hierarchy = ControlRig->GetHierarchy();
						ControlRigNodeWorkflowOptions->Selection = ControlRig->GetHierarchy()->GetSelectedKeys();
					}
				}
			}
		}
	});
}

#endif

void IControlRigAssetInterface::UpdateConnectionMapAfterRename(const FString& InOldModuleName)
{
	const FString OldModuleName = InOldModuleName + FRigHierarchyModulePath::ModuleNameSuffix;
	const FString NewModuleName = GetRigModuleSettings().Identifier.Name + FRigHierarchyModulePath::ModuleNameSuffix;
	
	TMap<FRigElementKey, FRigElementKeyCollection> FixedConnectionMap;
	for(const TPair<FRigElementKey, FRigElementKeyCollection>& Pair : GetArrayConnectionMap())
	{
		auto FixUpConnectionMap = [OldModuleName, NewModuleName](const FRigElementKey& InKey) -> FRigElementKey
		{
			const FString NameString = InKey.Name.ToString();
			if(NameString.StartsWith(OldModuleName, ESearchCase::CaseSensitive))
			{
				return FRigElementKey(*(NewModuleName + NameString.Mid(OldModuleName.Len())), InKey.Type);
			}
			return InKey;
		};

		const FRigElementKey Key = FixUpConnectionMap(Pair.Key);
		FRigElementKeyCollection Values;
		for(const FRigElementKey& OldValue : Pair.Value)
		{
			Values.Keys.Add(FixUpConnectionMap(OldValue));
		}
		FixedConnectionMap.FindOrAdd(Key) = Values;
	}

	Swap(GetArrayConnectionMap(), FixedConnectionMap);
}

UClass* IControlRigAssetInterface::GetRigVMEdGraphNodeClass() const
{
	return UControlRigGraphNode::StaticClass();
}

UClass* IControlRigAssetInterface::GetRigVMEdGraphSchemaClass() const
{
	return UControlRigGraphSchema::StaticClass();
}

UClass* IControlRigAssetInterface::GetRigVMEdGraphClass() const
{
	return UControlRigGraph::StaticClass();
}

UClass* IControlRigAssetInterface::GetRigVMEditorSettingsClass() const
{
	return UControlRigEditorSettings::StaticClass();
}

void IControlRigAssetInterface::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	for (FRigModuleReference& Module : GetModularRigModel().Modules)
	{
		OutDeps.Add(Module.Class.Get());
	}
}

#if WITH_EDITOR
const FLazyName& IControlRigAssetInterface::GetPanelPinFactoryName() const
{
	return ControlRigPanelNodeFactoryName;
}

IRigVMEditorModule* IControlRigAssetInterface::GetEditorModule() const
{
	return &IControlRigEditorModule::Get();
}
#endif

TArray<FString> IControlRigAssetInterface::GeneratePythonCommands(const FString InNewBlueprintName)
{
	TArray<FString> InternalCommands;
	InternalCommands.Add(TEXT("import unreal"));
	InternalCommands.Add(TEXT("unreal.load_module('ControlRigDeveloper')"));
	InternalCommands.Add(TEXT("factory = unreal.ControlRigBlueprintFactory"));
	InternalCommands.Add(FString::Printf(TEXT("blueprint = factory.create_new_control_rig_asset(desired_package_path = '%s')"), *InNewBlueprintName));
	InternalCommands.Add(TEXT("hierarchy = blueprint.hierarchy"));
	InternalCommands.Add(TEXT("hierarchy_controller = hierarchy.get_controller()"));

	// Hierarchy
	InternalCommands.Append(GetHierarchy()->GetController(true)->GeneratePythonCommands());

#if WITH_EDITORONLY_DATA
	const FString PreviewMeshPath = GetPreviewMesh()->GetPathName();
	InternalCommands.Add(FString::Printf(TEXT("blueprint.set_preview_mesh(unreal.load_object(name='%s', outer=None))"),
		*PreviewMeshPath));
#endif

	InternalCommands.Append(GetRigVMAssetInterface()->IRigVMAssetInterface::GeneratePythonCommands(InNewBlueprintName));
	return InternalCommands;
}

void IControlRigAssetInterface::GetTypeActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	IControlRigEditorModule::Get().GetTypeActions(FRigVMAssetInterfacePtr(const_cast<UObject*>(GetObject())), ActionRegistrar);
}

void IControlRigAssetInterface::GetInstanceActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	IControlRigEditorModule::Get().GetInstanceActions(FRigVMAssetInterfacePtr(const_cast<UObject*>(GetObject())), ActionRegistrar);
}

void IControlRigAssetInterface::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	DECLARE_SCOPE_HIERARCHICAL_COUNTER_FUNC()

	if (TransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		TArray<FName> PropertiesChanged = TransactionEvent.GetChangedProperties();
		int32 TransactionIndex = GEditor->Trans->FindTransactionIndex(TransactionEvent.GetTransactionId());
		const FTransaction* Transaction = GEditor->Trans->GetTransaction(TransactionIndex);
		if (Transaction && Transaction->ContainsObject(GetHierarchy()))
		{
			if (Transaction->GetTitle().BuildSourceString() == TEXT("Transform Gizmo"))
			{
				PropagatePoseFromBPToInstances();
				return;
			}

			PropagateHierarchyFromBPToInstances();

			// make sure the bone name list is up 2 date for the editor graph
			for (UEdGraph* Graph : GetRigVMAssetInterface()->GetUberGraphs())
			{
				UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph);
				if (RigGraph == nullptr)
				{
					continue;
				}
				RigGraph->CacheNameLists(GetHierarchy(), &GetDrawContainer(), GetShapeLibraries());
			}

			GetRigVMAssetInterface()->RequestAutoVMRecompilation();
			(void) GetObject()->MarkPackageDirty();
		}

		if (PropertiesChanged.Contains(TEXT("ModularRigModel")))
		{
			if (IsModularRig())
			{
				GetModularRigModel().UpdateCachedChildren();
				GetModularRigModel().Connections.UpdateFromConnectionList();
				RecompileModularRig();
			}
		}
		
		if (PropertiesChanged.Contains(TEXT("DrawContainer")))
		{
			PropagateDrawInstructionsFromBPToInstances();
		}

		if (PropertiesChanged.Contains(TEXT("ArrayConnectionMap")))
		{
			PropagateHierarchyFromBPToInstances();
		}
	}
}

void IControlRigAssetInterface::PostDuplicate(bool bDuplicateForPIE)
{
	if (URigHierarchyController* Controller = GetHierarchy()->GetController(true))
	{
		Controller->OnModified().RemoveAll(this);
		Controller->OnModified().AddRaw(this, &IControlRigAssetInterface::HandleHierarchyModified);
	}

	if (UModularRigController* ModularController = GetModularRigModel().GetController())
	{
		ModularController->OnModified().RemoveAll(this);
		ModularController->OnModified().AddRaw(this, &IControlRigAssetInterface::HandleRigModulesModified);
	}

	// update the rig module identifier after save-as or duplicate asset
	if(IsControlRigModule())
	{
		const FString OldNameSpace = GetRigModuleSettings().Identifier.Name;
		GetRigModuleSettings().Identifier.Name = URigHierarchy::GetSanitizedName(FRigName(GetObject()->GetName())).ToString();
		UpdateConnectionMapAfterRename(OldNameSpace);
	}

	GetModularRigModel().UpdateCachedChildren();
	GetModularRigModel().Connections.UpdateFromConnectionList();
}

void IControlRigAssetInterface::PostRename(UObject* OldOuter, const FName OldName)
{
	// update the rig module identifier after renaming the asset
	if(IsControlRigModule())
	{
		const FString OldNameSpace = GetRigModuleSettings().Identifier.Name; 
		GetRigModuleSettings().Identifier.Name = URigHierarchy::GetSanitizedName(FRigName(GetObject()->GetName())).ToString();
		UpdateConnectionMapAfterRename(OldNameSpace);
	}
}

TArray<FControlRigAssetInterfacePtr> IControlRigAssetInterface::GetCurrentlyOpenRigAssets()
{
	return sCurrentlyOpenedRigBlueprints;
}

#if WITH_EDITOR

const FControlRigShapeDefinition* IControlRigAssetInterface::GetControlShapeByName(const FName& InName) const
{
	TMap<FString, FString> LibraryNameMap;
	if(const UControlRig* ControlRig = Cast<UControlRig>(GetObjectBeingDebugged()))
	{
		LibraryNameMap = ControlRig->ShapeLibraryNameMap;
	}
	return UControlRigShapeLibrary::GetShapeByName(InName, GetShapeLibraries(), LibraryNameMap);
}

FName IControlRigAssetInterface::AddTransientControl(const URigVMUnitNode* InNode, const FRigDirectManipulationTarget& InTarget)
{
	TUniquePtr<FControlValueScope> ValueScope;
	if (!UControlRigEditorSettings::Get()->bResetControlsOnPinValueInteraction) // if we need to retain the controls
	{
		ValueScope = MakeUnique<FControlValueScope>(GetObject());
	}

	// for now we only allow one pin control at the same time
	ClearTransientControls();

	FName ReturnName = NAME_None;
	TArray<UObject*> ArchetypeInstances = GetRigVMAssetInterface()->GetArchetypeInstances(false, true);
	for (UObject* ArchetypeInstance : ArchetypeInstances)
	{
		UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
		if (InstancedControlRig)
		{
			FName ControlName = InstancedControlRig->AddTransientControl(InNode, InTarget);
			if (ReturnName.IsEqual(NAME_None))
			{
				ReturnName = ControlName;
			}
		}
	}

	return ReturnName;
}

FName IControlRigAssetInterface::RemoveTransientControl(const URigVMUnitNode* InNode, const FRigDirectManipulationTarget& InTarget)
{
	TUniquePtr<FControlValueScope> ValueScope;
	if (!UControlRigEditorSettings::Get()->bResetControlsOnPinValueInteraction) // if we need to retain the controls
	{
		ValueScope = MakeUnique<FControlValueScope>(GetObject());
	}

	FName RemovedName = NAME_None;
	TArray<UObject*> ArchetypeInstances = GetRigVMAssetInterface()->GetArchetypeInstances(false, true);
	for (UObject* ArchetypeInstance : ArchetypeInstances)
	{
		UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
		if (InstancedControlRig)
		{
			FName Name = InstancedControlRig->RemoveTransientControl(InNode, InTarget);
			if (RemovedName.IsEqual(NAME_None))
	{
				RemovedName = Name;
			}
		}
	}

	return RemovedName;
}

FName IControlRigAssetInterface::AddTransientControl(const FRigElementKey& InElement)
{
	TUniquePtr<FControlValueScope> ValueScope;
	if (!UControlRigEditorSettings::Get()->bResetControlsOnPinValueInteraction) // if we need to retain the controls
	{
		ValueScope = MakeUnique<FControlValueScope>(GetObject());
	}
	FName ReturnName = NAME_None;
	TArray<UObject*> ArchetypeInstances = GetRigVMAssetInterface()->GetArchetypeInstances(false, true);
	
	// hierarchy transforms will be reset when ClearTransientControls() is called,
	// so to retain any bone transform modifications we have to save them
	TMap<UObject*, FTransform> SavedElementLocalTransforms;
	for (UObject* ArchetypeInstance : ArchetypeInstances)
	{
		UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
		if (InstancedControlRig)
		{
			if (InstancedControlRig->DynamicHierarchy)
			{ 
				SavedElementLocalTransforms.FindOrAdd(InstancedControlRig) = InstancedControlRig->DynamicHierarchy->GetLocalTransform(InElement);
			}
		}
	}

	// for now we only allow one pin control at the same time
	ClearTransientControls();
	
	for (UObject* ArchetypeInstance : ArchetypeInstances)
	{
		UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
		if (InstancedControlRig)
		{
			// restore the element transforms so that transient controls are created at the right place
			if (const FTransform* SavedTransform = SavedElementLocalTransforms.Find(InstancedControlRig))
			{
				if (InstancedControlRig->DynamicHierarchy)
				{ 
					InstancedControlRig->DynamicHierarchy->SetLocalTransform(InElement, *SavedTransform);
				}
			}
			
			FName ControlName = InstancedControlRig->AddTransientControl(InElement);
			if (ReturnName.IsEqual(NAME_None))
			{
				ReturnName = ControlName;
			}
		}
	}

	return ReturnName;

}

FName IControlRigAssetInterface::RemoveTransientControl(const FRigElementKey& InElement)
{
	TUniquePtr<FControlValueScope> ValueScope;
	if (!UControlRigEditorSettings::Get()->bResetControlsOnPinValueInteraction) // if we need to retain the controls
	{
		ValueScope = MakeUnique<FControlValueScope>(GetObject());
	}

	FName RemovedName = NAME_None;
	TArray<UObject*> ArchetypeInstances = GetRigVMAssetInterface()->GetArchetypeInstances(false, true);
	for (UObject* ArchetypeInstance : ArchetypeInstances)
	{
		UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
		if (InstancedControlRig)
		{
			FName Name = InstancedControlRig->RemoveTransientControl(InElement);
			if (RemovedName.IsEqual(NAME_None))
			{
				RemovedName = Name;
			}
		}
	}

	return RemovedName;
}

void IControlRigAssetInterface::ClearTransientControls()
{
	bool bHasAnyTransientControls = false;
	
	{
		TArray<UObject*> ArchetypeInstances = GetRigVMAssetInterface()->GetArchetypeInstances(false, true);
		for (UObject* ArchetypeInstance : ArchetypeInstances)
		{
			UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
			if (InstancedControlRig)
			{
				if (URigHierarchy* InstanceHierarchy = InstancedControlRig->GetHierarchy())
				{
					if(!InstanceHierarchy->GetTransientControls().IsEmpty())
					{
						bHasAnyTransientControls = true;
						break;
					}
				}
			}
		}
	}

	if(!bHasAnyTransientControls)
	{
		return;
	}

	TUniquePtr<FControlValueScope> ValueScope;
	if (!UControlRigEditorSettings::Get()->bResetControlsOnPinValueInteraction) // if we need to retain the controls
	{
		ValueScope = MakeUnique<FControlValueScope>(GetObject());
	}

	{
		TArray<UObject*> ArchetypeInstances = GetRigVMAssetInterface()->GetArchetypeInstances(false, true);
		for (UObject* ArchetypeInstance : ArchetypeInstances)
		{
			UControlRig* InstancedControlRig = Cast<UControlRig>(ArchetypeInstance);
			if (InstancedControlRig)
			{
				InstancedControlRig->ClearTransientControls();
			}
		}
	}
}

UModularRigController* IControlRigAssetInterface::GetModularRigController() 
{
	if (!GetControlRigClass()->IsChildOf(UModularRig::StaticClass()))
	{
		return nullptr;
	}

	return GetModularRigModel().GetController();
}

void IControlRigAssetInterface::RecompileModularRig()
{
	RefreshModuleConnectors();
	OnModularRigPreCompiled().Broadcast(GetRigVMAssetInterface());
	if (const UClass* MyControlRigClass = GetControlRigClass())
	{
		if (UModularRig* DefaultObject = Cast<UModularRig>(MyControlRigClass->GetDefaultObject(false)))
		{
			PropagateModuleHierarchyFromBPToInstances();
			RequestConstructionOnAllModules();
		}
	}
	UpdateModularDependencyDelegates();

#if WITH_EDITOR
	if(GetObjectBeingDebugged() == nullptr)
	{
		GetRigVMAssetInterface()->SetObjectBeingDebugged(CreateControlRig());
	}
#endif

	OnModularRigCompiled().Broadcast(GetRigVMAssetInterface());
}

#endif

void IControlRigAssetInterface::SetupDefaultObjectDuringCompilation(URigVMHost* InCDO)
{
	GetRigVMAssetInterface()->IRigVMAssetInterface::SetupDefaultObjectDuringCompilation(InCDO);
	CastChecked<UControlRig>(InCDO)->GetHierarchy()->CopyHierarchy(GetHierarchy());
}

void IControlRigAssetInterface::SetupPinRedirectorsForBackwardsCompatibility()
{
	for(URigVMGraph* Model : *GetRigVMClient())
	{
		for (URigVMNode* Node : Model->GetNodes())
		{
			if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(Node))
			{
				UScriptStruct* Struct = UnitNode->GetScriptStruct();
				if (Struct == FRigUnit_SetBoneTransform::StaticStruct())
				{
					URigVMPin* TransformPin = UnitNode->FindPin(TEXT("Transform"));
					URigVMPin* ResultPin = UnitNode->FindPin(TEXT("Result"));
					GetRigVMAssetInterface()->GetOrCreateController()->AddPinRedirector(false, true, TransformPin->GetPinPath(), ResultPin->GetPinPath());
				}
			}
		}
	}
}

void IControlRigAssetInterface::PathDomainSpecificContentOnLoad()
{
	PatchRigElementKeyCacheOnLoad();
	PatchPropagateToChildren();
}

void IControlRigAssetInterface::PatchRigElementKeyCacheOnLoad()
{
	if (GetObject()->GetLinkerCustomVersion(FControlRigObjectVersion::GUID) < FControlRigObjectVersion::RigElementKeyCache)
	{
		for (URigVMGraph* Graph : GetRigVMAssetInterface()->GetAllModels())
		{
			URigVMController* Controller = GetRigVMAssetInterface()->GetOrCreateController(Graph);
			TGuardValue<bool> DisablePinDefaultValueValidation(Controller->bValidatePinDefaults, false);
			FRigVMControllerNotifGuard NotifGuard(Controller, true);
			for (URigVMNode* Node : Graph->GetNodes())
			{
				if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(Node))
				{
					UScriptStruct* ScriptStruct = UnitNode->GetScriptStruct();
					FString FunctionName = FString::Printf(TEXT("%s::%s"), *ScriptStruct->GetStructCPPName(), *UnitNode->GetMethodName().ToString());
					const FRigVMFunction* Function = FRigVMRegistry::Get().FindFunction(*FunctionName);
					check(Function);
					for (TFieldIterator<FProperty> It(Function->Struct); It; ++It)
					{
						if (It->GetCPPType() == TEXT("FCachedRigElement"))
						{
							if (URigVMPin* Pin = Node->FindPin(It->GetName()))
							{
								int32 BoneIndex = FCString::Atoi(*Pin->GetDefaultValue());
								FRigElementKey Key = GetHierarchy()->GetKey(BoneIndex);
								FCachedRigElement DefaultValueElement(Key, GetHierarchy());
								FString Result;
								TBaseStructure<FCachedRigElement>::Get()->ExportText(Result, &DefaultValueElement, nullptr, nullptr, PPF_None, nullptr);								
								FRigVMDefaultValueTypeGuard _(Controller, ERigVMPinDefaultValueType::Override);
								Controller->SetPinDefaultValue(Pin->GetPinPath(), Result, true, false, false);
								GetRigVMAssetInterface()->MarkDirtyDuringLoad();
							}
						}
					}
				}
			}
		}
	}
}

// change the default value form False to True for transform nodes
void IControlRigAssetInterface::PatchPropagateToChildren()
{
	// no need to update default value past this version
	if (GetObject()->GetLinkerCustomVersion(FControlRigObjectVersion::GUID) >= FControlRigObjectVersion::RenameGizmoToShape)
	{
		return;
	}
	
	auto IsNullOrControl = [](const URigVMPin* InPin)
	{
		const bool bHasItem = InPin->GetCPPTypeObject() == FRigElementKey::StaticStruct() && InPin->GetName() == "Item";
		if (!bHasItem)
		{
			return false;
		}

		if (const URigVMPin* TypePin = InPin->FindSubPin(TEXT("Type")))
		{
			const FString& TypeValue = TypePin->GetDefaultValue();
			return TypeValue == TEXT("Null") || TypeValue == TEXT("Space") || TypeValue == TEXT("Control");
		}
		
		return false;
	};

	auto IsPropagateChildren = [](const URigVMPin* InPin)
	{
		return InPin->GetCPPType() == TEXT("bool") && InPin->GetName() == TEXT("bPropagateToChildren");
	};

	auto FindPropagatePin = [IsNullOrControl, IsPropagateChildren](const URigVMNode* InNode)-> URigVMPin*
	{
		URigVMPin* PropagatePin = nullptr;
		URigVMPin* ItemPin = nullptr;  
		for (URigVMPin* Pin: InNode->GetPins())
		{
			// look for Item pin
			if (!ItemPin && IsNullOrControl(Pin))
			{
				ItemPin = Pin;
			}

			// look for bPropagateToChildren pin
			if (!PropagatePin && IsPropagateChildren(Pin))
			{
				PropagatePin = Pin;
			}

			// return propagation pin if both found
			if (ItemPin && PropagatePin)
			{
				return PropagatePin;
			}
		}
		return nullptr;
	};

	for (URigVMGraph* Graph : GetRigVMAssetInterface()->GetAllModels())
	{
		TArray< const URigVMPin* > PinsToUpdate;
		for (const URigVMNode* Node : Graph->GetNodes())
		{
			if (const URigVMPin* PropagatePin = FindPropagatePin(Node))
			{
				PinsToUpdate.Add(PropagatePin);
			}
		}
		
		if (URigVMController* Controller = GetRigVMAssetInterface()->GetOrCreateController(Graph))
		{
			FRigVMControllerNotifGuard NotifGuard(Controller, true);
			for (const URigVMPin* Pin: PinsToUpdate)
			{
				Controller->SetPinDefaultValue(Pin->GetPinPath(), TEXT("True"), false, false, false);
			}
		}
	}
}

void IControlRigAssetInterface::CreateMemberVariablesOnLoad()
{
#if WITH_EDITOR

	const int32 LinkerVersion = GetObject()->GetLinkerCustomVersion(FControlRigObjectVersion::GUID);
	if (LinkerVersion < FControlRigObjectVersion::SwitchedToRigVM)
	{
		// ignore errors during the first potential compile of the VM
		// since that this point variable nodes may still be ill-formed.
		TGuardValue<FRigVMReportDelegate> SuspendReportDelegate(GetRigVMAssetInterface()->GetVMCompileSettings().ASTSettings.ReportDelegate,
			FRigVMReportDelegate::CreateLambda([](EMessageSeverity::Type,  UObject*, const FString&)
			{
				// do nothing
			})
		);
		GetRigVMAssetInterface()->InitializeModelIfRequired();
	}

	GetRigVMAssetInterface()->AddedMemberVariableMap.Reset();

	for (int32 VariableIndex = 0; VariableIndex < GetRigVMAssetInterface()->GetAssetVariables().Num(); VariableIndex++)
	{
		GetRigVMAssetInterface()->AddedMemberVariableMap.Add(GetRigVMAssetInterface()->GetAssetVariables()[VariableIndex].Name, VariableIndex);
	}

	if (GetRigVMClient()->Num() == 0)
	{
		return;
	}

	// setup variables on the blueprint based on the previous "parameters"
	if (GetObject()->GetLinkerCustomVersion(FControlRigObjectVersion::GUID) < FControlRigObjectVersion::BlueprintVariableSupport)
	{
		
		TSharedPtr<FRigVMNameValidator> NameValidator = MakeShareable(new FRigVMNameValidator(GetRigVMAssetInterface(), nullptr));

		auto CreateVariable = [this, NameValidator](const URigVMVariableNode* InVariableNode)
		{
			if (!InVariableNode)
			{
				return;
			}
			
			static const FString VariableString = TEXT("Variable");
			if (URigVMPin* VariablePin = InVariableNode->FindPin(VariableString))
			{
				if (VariablePin->GetDirection() != ERigVMPinDirection::Visible)
				{
					return;
				}
			}

			const FRigVMGraphVariableDescription Description = InVariableNode->GetVariableDescription();
			if (GetRigVMAssetInterface()->AddedMemberVariableMap.Contains(Description.Name))
			{
				return;
			}

			const FEdGraphPinType PinType = RigVMTypeUtils::PinTypeFromExternalVariable(Description.ToExternalVariable());
			if (!PinType.PinCategory.IsValid())
			{
				return;
			}

			const FName VarName = FindHostMemberVariableUniqueName(NameValidator, Description.Name.ToString());
			const FName ResultName = GetRigVMAssetInterface()->AddAssetVariableFromPinType(VarName, PinType, false, false, FString());
			if (ResultName != NAME_None)
			{
				TArray<FRigVMGraphVariableDescription> Variables = GetRigVMAssetInterface()->GetAssetVariables();
				for (int32 VariableIndex = 0; VariableIndex < Variables.Num(); VariableIndex++)
				{
					if (Variables[VariableIndex].Name == ResultName)
					{
						GetRigVMAssetInterface()->AddedMemberVariableMap.Add(Description.Name, VariableIndex);
						GetRigVMAssetInterface()->MarkDirtyDuringLoad();
						break;
					}
				}
			}
		};

		auto CreateParameter = [this, NameValidator](const URigVMParameterNode* InParameterNode)
		{
			if (!InParameterNode)
			{
				return;
			}

			static const FString ParameterString = TEXT("Parameter");
			if (const URigVMPin* ParameterPin = InParameterNode->FindPin(ParameterString))
			{
				if (ParameterPin->GetDirection() != ERigVMPinDirection::Visible)
				{
					return;
				}
			}

			const FRigVMGraphParameterDescription Description = InParameterNode->GetParameterDescription();
			if (GetRigVMAssetInterface()->AddedMemberVariableMap.Contains(Description.Name))
			{
				return;
			}

			const FName VarName = FindHostMemberVariableUniqueName(NameValidator, Description.Name.ToString());
			FRigVMExternalVariable ExternalVariable = Description.ToExternalVariable();
			ExternalVariable.Name = VarName;
			ExternalVariable.bIsPublic = true; // parameters get this set by default
			const FName ResultName = GetRigVMAssetInterface()->AddHostMemberVariableFromExternal(ExternalVariable, FString());
			if (!ResultName.IsNone())
			{
				TArray<FRigVMGraphVariableDescription> Variables = GetRigVMAssetInterface()->GetAssetVariables();
				for (int32 VariableIndex = 0; VariableIndex < Variables.Num(); VariableIndex++)
				{
					if (Variables[VariableIndex].Name == ResultName)
					{
						GetRigVMAssetInterface()->AddedMemberVariableMap.Add(Description.Name, VariableIndex);
						GetRigVMAssetInterface()->MarkDirtyDuringLoad();
						break;
					}
				}
			}
		};
		
		for (const URigVMGraph* Model : *GetRigVMClient())
		{
			const TArray<URigVMNode*>& Nodes = Model->GetNodes();
			for (const URigVMNode* Node : Nodes)
			{
				if (const URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
				{
					CreateVariable(VariableNode);
				}

				// Leaving this for backwards compatibility, even though we don't support parameters anymore
				// When a parameter node is found, we will create a variable
				else if (const URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Node))
				{
					CreateParameter(ParameterNode);
				}
			}
		}
	}

#endif
}

void IControlRigAssetInterface::PatchVariableNodesOnLoad()
{
#if WITH_EDITOR

	// setup variables on the blueprint based on the previous "parameters"
	if (GetObject()->GetLinkerCustomVersion(FControlRigObjectVersion::GUID) < FControlRigObjectVersion::BlueprintVariableSupport)
	{
		TGuardValue<bool> GuardNotifsSelf(GetRigVMAssetInterface()->bSuspendModelNotificationsForSelf, true);
		
		check(GetRigVMClient()->GetDefaultModel());

		auto PatchVariableNode = [this](const URigVMVariableNode* InVariableNode)
		{
			if (!InVariableNode)
			{
				return;
			}

			const FRigVMGraphVariableDescription Description = InVariableNode->GetVariableDescription();
			if (!GetRigVMAssetInterface()->AddedMemberVariableMap.Contains(Description.Name))
			{
				return;
			}
			
			const int32 VariableIndex = GetRigVMAssetInterface()->AddedMemberVariableMap.FindChecked(Description.Name);
			const FName VarName = GetRigVMAssetInterface()->GetAssetVariables()[VariableIndex].Name;
			
			GetRigVMAssetInterface()->GetOrCreateController()->RefreshVariableNode(
				InVariableNode->GetFName(), VarName, Description.CPPType, Description.CPPTypeObject, false);
			
			GetRigVMAssetInterface()->MarkDirtyDuringLoad();
		};

		auto PatchParameterNode = [this](const URigVMParameterNode* InParameterNode)
		{
			if (!InParameterNode)
			{
				return;
			}
			
			const FRigVMGraphParameterDescription Description = InParameterNode->GetParameterDescription();
			if (!GetRigVMAssetInterface()->AddedMemberVariableMap.Contains(Description.Name))
			{
				return;
			}

			const int32 VariableIndex = GetRigVMAssetInterface()->AddedMemberVariableMap.FindChecked(Description.Name);
			const FName VarName = GetRigVMAssetInterface()->GetAssetVariables()[VariableIndex].Name;
			
			GetRigVMAssetInterface()->GetOrCreateController()->ReplaceParameterNodeWithVariable(
				InParameterNode->GetFName(), VarName, Description.CPPType, Description.CPPTypeObject, false);

			GetRigVMAssetInterface()->MarkDirtyDuringLoad();	
		};
		
		for(const URigVMGraph* Model : *GetRigVMClient())
		{
			TArray<URigVMNode*> Nodes = Model->GetNodes();
			for (URigVMNode* Node : Nodes)
			{
				if (const URigVMVariableNode* VariableNode = Cast<URigVMVariableNode>(Node))
				{
					PatchVariableNode(VariableNode);
				}
				else if (const URigVMParameterNode* ParameterNode = Cast<URigVMParameterNode>(Node))
				{
					PatchParameterNode(ParameterNode);
				}
			}
		}
	}

#endif
}

void IControlRigAssetInterface::UpdateElementKeyRedirector(UControlRig* InControlRig) const
{
	InControlRig->HierarchySettings = GetHierarchySettings();
	InControlRig->RigModuleSettings = GetRigModuleSettings();
	if (URigHierarchy* RigHierarchy = InControlRig->GetHierarchy())
	{
		const FRigHierarchyNameSpaceWarningBracket SuspendNameSpaceWarnings(RigHierarchy); 
		InControlRig->ElementKeyRedirector = FRigElementKeyRedirector(GetArrayConnectionMap(), RigHierarchy);
	}
}

void IControlRigAssetInterface::PropagatePoseFromInstanceToBP(UControlRig* InControlRig) const
{
	check(InControlRig);
	// current transforms in BP and CDO are meaningless, no need to copy them
	// we use BP hierarchy to initialize CDO and instances' hierarchy, 
	// so it should always be in the initial state.
	GetHierarchy()->CopyPose(InControlRig->GetHierarchy(), false, true, false, true);
}

void IControlRigAssetInterface::PropagatePoseFromBPToInstances() const
{
	if (UClass* MyControlRigClass = GetRigVMAssetInterface()->GetRigVMGeneratedClass())
	{
		if (UControlRig* DefaultObject = Cast<UControlRig>(MyControlRigClass->GetDefaultObject(false)))
		{
			DefaultObject->PostInitInstanceIfRequired();
			DefaultObject->GetHierarchy()->CopyPose(GetHierarchy(), true, true, true);

			TArray<UObject*> ArchetypeInstances;
			DefaultObject->GetArchetypeInstances(ArchetypeInstances);
			for (UObject* ArchetypeInstance : ArchetypeInstances)
			{
				if (UControlRig* InstanceRig = Cast<UControlRig>(ArchetypeInstance))
				{
					InstanceRig->PostInitInstanceIfRequired();
					if(!InstanceRig->IsRigModuleInstance())
					{
						InstanceRig->GetHierarchy()->CopyPose(GetHierarchy(), true, true, true);
					}
				}
			}
		}
	}
}

void IControlRigAssetInterface::PropagateHierarchyFromBPToInstances() const
{
	if (UClass* MyControlRigClass = GetRigVMAssetInterface()->GetRigVMGeneratedClass())
	{
		if (UControlRig* DefaultObject = Cast<UControlRig>(MyControlRigClass->GetDefaultObject(false)))
		{
			DefaultObject->PostInitInstanceIfRequired();
			DefaultObject->GetHierarchy()->CopyHierarchy(GetHierarchy());

			UpdateElementKeyRedirector(DefaultObject);

			if (!DefaultObject->HasAnyFlags(RF_NeedPostLoad)) // If CDO is loading, skip Init, it will be done later
			{
				DefaultObject->Initialize(true);
			}

			TArray<UObject*> ArchetypeInstances;
			DefaultObject->GetArchetypeInstances(ArchetypeInstances);
			for (UObject* ArchetypeInstance : ArchetypeInstances)
			{
				if (UControlRig* InstanceRig = Cast<UControlRig>(ArchetypeInstance))
				{
					if (InstanceRig->IsRigModuleInstance())
					{
						if (UModularRig* ModularRig = Cast<UModularRig>(InstanceRig->GetOuter()))
						{
							ModularRig->RequestInit();
						}
					}
					else
					{
						InstanceRig->PostInitInstanceIfRequired();
						InstanceRig->GetHierarchy()->CopyHierarchy(GetHierarchy());
						InstanceRig->HierarchySettings = GetHierarchySettings();
						UpdateElementKeyRedirector(InstanceRig);
						InstanceRig->Initialize(true);
					}
				}
			}
		}
	}
}

void IControlRigAssetInterface::PropagateDrawInstructionsFromBPToInstances() const
{
	if (UClass* MyControlRigClass = GetRigVMAssetInterface()->GetRigVMGeneratedClass())
	{
		if (UControlRig* DefaultObject = Cast<UControlRig>(MyControlRigClass->GetDefaultObject(false)))
	{
			DefaultObject->DrawContainer = GetDrawContainer();

			TArray<UObject*> ArchetypeInstances;
			DefaultObject->GetArchetypeInstances(ArchetypeInstances);

			for (UObject* ArchetypeInstance : ArchetypeInstances)
			{
				if (UControlRig* InstanceRig = Cast<UControlRig>(ArchetypeInstance))
	{
					InstanceRig->DrawContainer = GetDrawContainer();
				}
			}
		}
	}


	// make sure the bone name list is up 2 date for the editor graph
	for (UEdGraph* Graph : GetRigVMAssetInterface()->GetUberGraphs())
	{
		UControlRigGraph* RigGraph = Cast<UControlRigGraph>(Graph);
		if (RigGraph == nullptr)
		{
			continue;
		}
		RigGraph->CacheNameLists(GetHierarchy(), &GetDrawContainer(), GetShapeLibraries());
	}
}

void IControlRigAssetInterface::PropagatePropertyFromBPToInstances(FRigElementKey InRigElement, const FProperty* InProperty) const
{
	int32 ElementIndex = GetHierarchy()->GetIndex(InRigElement);
	ensure(ElementIndex != INDEX_NONE);
	check(InProperty);

	if (UClass* MyControlRigClass = GetRigVMAssetInterface()->GetRigVMGeneratedClass())
	{
		if (UControlRig* DefaultObject = Cast<UControlRig>(MyControlRigClass->GetDefaultObject(false)))
		{
			TArray<UObject*> ArchetypeInstances;
			DefaultObject->GetArchetypeInstances(ArchetypeInstances);

			const int32 PropertyOffset = InProperty->GetOffset_ReplaceWith_ContainerPtrToValuePtr();
			const int32 PropertySize = InProperty->GetSize();

			uint8* Source = ((uint8*)GetHierarchy()->Get(ElementIndex)) + PropertyOffset;
			for (UObject* ArchetypeInstance : ArchetypeInstances)
			{
				if (UControlRig* InstanceRig = Cast<UControlRig>(ArchetypeInstance))
				{
					InstanceRig->PostInitInstanceIfRequired();
					uint8* Dest = ((uint8*)InstanceRig->GetHierarchy()->Get(ElementIndex)) + PropertyOffset;
					FMemory::Memcpy(Dest, Source, PropertySize);
				}
			}
		}
	}
}

void IControlRigAssetInterface::PropagatePropertyFromInstanceToBP(FRigElementKey InRigElement, const FProperty* InProperty, UControlRig* InInstance) const
{
	const int32 ElementIndex = GetHierarchy()->GetIndex(InRigElement);
	ensure(ElementIndex != INDEX_NONE);
	check(InProperty);

	const int32 PropertyOffset = InProperty->GetOffset_ReplaceWith_ContainerPtrToValuePtr();
	const int32 PropertySize = InProperty->GetSize();
	uint8* Source = ((uint8*)InInstance->GetHierarchy()->Get(ElementIndex)) + PropertyOffset;
	uint8* Dest = ((uint8*)GetHierarchy()->Get(ElementIndex)) + PropertyOffset;
	FMemory::Memcpy(Dest, Source, PropertySize);
}

void IControlRigAssetInterface::PropagateModuleHierarchyFromBPToInstances() const
{
	if (const UClass* MyControlRigClass = GetRigVMAssetInterface()->GetRigVMGeneratedClass())
	{
		if (UModularRig* DefaultObject = Cast<UModularRig>(MyControlRigClass->GetDefaultObject(false)))
		{
			// We need to first transfer the model from the blueprint to the CDO
			// We then ask instances to initialize which will provoke a call UModularRig::UpdateModuleHierarchyFromCDO
			
			DefaultObject->ResetModules();

			// copy the model over to the CDO.
			// non-CDO instances are going to instantiate the model into a
			// UObject module instance tree. CDO's are data only to avoid bugs / 
			// behaviors in the blueprint re-instancer - which is disregarding any
			// object under a CDO.
			DefaultObject->ModularRigModel = GetModularRigModel();
			DefaultObject->ModularRigModel.SetOuterClientHost(DefaultObject);
			DefaultObject->ModularRigSettings = GetModularRigSettings();
			
			TArray<UObject*> ArchetypeInstances;
			DefaultObject->GetArchetypeInstances(ArchetypeInstances);
			for (UObject* ArchetypeInstance : ArchetypeInstances)
			{
				if (UControlRig* InstanceRig = Cast<UControlRig>(ArchetypeInstance))
				{
					// this will provoke a call to InitializeFromCDO
					InstanceRig->Initialize(true);
				}
			}
		}
	}
}

void IControlRigAssetInterface::UpdateModularDependencyDelegates()
{
	TArray<FControlRigAssetInterfacePtr> VisitList;
	GetModularRigModel().ForEachModule([&VisitList, this](const FRigModuleReference* Element) -> bool
	{
		if(const UClass* Class = Element->Class.Get())
		{
			if(FControlRigAssetInterfacePtr ModuleBlueprint = Class->ClassGeneratedBy)
			{
				if(!VisitList.Contains(ModuleBlueprint))
				{
					ModuleBlueprint->GetRigVMAssetInterface()->OnVMCompiled().RemoveAll(GetObject());
					ModuleBlueprint->OnModularRigCompiled().RemoveAll(GetObject());
					ModuleBlueprint->GetRigVMAssetInterface()->OnVMCompiled().AddWeakLambda(GetObject(), [this](UObject* InModuleBlueprint, URigVM* InVM, FRigVMExtendedExecuteContext& InExecuteContext)
						{
							OnModularDependencyVMCompiled(InModuleBlueprint, InVM, InExecuteContext);
						});
					ModuleBlueprint->OnModularRigCompiled().AddWeakLambda(GetObject(), [this](FRigVMAssetInterfacePtr InModuleBlueprint)
						{
							OnModularDependencyChanged(InModuleBlueprint);
						});
					VisitList.Add(ModuleBlueprint);
				}
			}
		}
		return true;
	});
}

void IControlRigAssetInterface::OnModularDependencyVMCompiled(UObject* InModuleBlueprint, URigVM* InVM, FRigVMExtendedExecuteContext& InExecuteContext)
{
	if(URigVMBlueprint* RigVMBlueprint = Cast<URigVMBlueprint>(InModuleBlueprint))
	{
		OnModularDependencyChanged(FRigVMAssetInterfacePtr(RigVMBlueprint));
	}
}

void IControlRigAssetInterface::OnModularDependencyChanged(FRigVMAssetInterfacePtr InModuleBlueprint)
{
	RefreshModuleVariables();
	RefreshModuleConnectors();
	RecompileModularRig();
}

void IControlRigAssetInterface::RequestConstructionOnAllModules()
{
	// the rig will perform initialize itself - but we should request construction
	check(IsModularRig());

	TArray<UObject*> ArchetypeInstances = GetRigVMAssetInterface()->GetArchetypeInstances(false, true);

	// visit all or our instances and request construction
	for (UObject* Instance : ArchetypeInstances)
	{
		if (UModularRig* InstanceRig = Cast<UModularRig>(Instance))
		{
			InstanceRig->RequestConstruction();
		}
	}

}

void IControlRigAssetInterface::RefreshModuleVariables()
{
	if(!IsModularRig())
	{
		return;
	}

	if (UModularRigController* Controller = GetModularRigController())
	{
		Controller->RefreshModuleVariables(false);
	}
}

void IControlRigAssetInterface::RefreshModuleConnectors()
{
	if(!IsModularRig())
	{
		return;
	}

	if (UModularRigController* Controller = GetModularRigController())
	{
		TGuardValue<bool> NotificationsGuard(Controller->bSuspendNotifications, true);
		GetModularRigModel().ForEachModule([this](const FRigModuleReference* Element) -> bool
		{
			RefreshModuleConnectors(Element, false);
			return true;
		});
	}

	PropagateHierarchyFromBPToInstances();
}

void IControlRigAssetInterface::RefreshModuleConnectors(const FRigModuleReference* InModule, bool bPropagateHierarchy)
{
	if(!IsModularRig())
	{
		return;
	}

	// avoid dead class pointers
	if(InModule->Class.Get() == nullptr)
	{
		return;
	}
	
	const bool bRemoveAllConnectors = !GetModularRigModel().FindModule(InModule->Name);
	
	if (URigHierarchyController* Controller = GetHierarchyController())
	{
		if (UControlRig* CDO = GetControlRigClass()->GetDefaultObject<UControlRig>())
		{
			const TArray<FRigElementKey> AllConnectors = GetHierarchy()->GetKeysOfType<FRigConnectorElement>();
			TArray<FRigElementKey> ExistingConnectors = AllConnectors.FilterByPredicate([InModule, this](const FRigElementKey& ConnectorKey) -> bool
			{
				const FRigElementKey PatchedKey = ConnectorKey.ConvertToModuleNameFormat(&GetModularRigModel().PreviousModulePaths);
				const FRigHierarchyModulePath ConnectorModulePath(PatchedKey.Name);
				return ConnectorModulePath.HasModuleName(InModule->Name);
			});

			// setup the module information. this is needed so that newly added
			// connectors result in the right namespace metadata etc
			FRigVMExtendedExecuteContext& Context = CDO->GetRigVMExtendedExecuteContext();
			FControlRigExecuteContext& PublicContext = Context.GetPublicDataSafe<FControlRigExecuteContext>();
			const UControlRig* ModuleCDO = InModule->Class->GetDefaultObject<UControlRig>();
			const TArray<FRigModuleConnector>& ExpectedConnectors = ModuleCDO->GetRigModuleSettings().ExposedConnectors;

			// rename the connectors since their keys have been patched
			for(FRigElementKey& ConnectorKey : ExistingConnectors)
			{
				const FRigElementKey PatchedKey = ConnectorKey.ConvertToModuleNameFormat(&GetModularRigModel().PreviousModulePaths);
				if(ConnectorKey != PatchedKey)
				{
					ConnectorKey = Controller->RenameElement(ConnectorKey, PatchedKey.Name);
				}
			}

			// remove the obsolete connectors
			for(const FRigElementKey& ConnectorKey : ExistingConnectors)
			{
				const FRigHierarchyModulePath ConnectorModulePath(ConnectorKey.Name);
				const bool bConnectorExpected = ExpectedConnectors.ContainsByPredicate(
					[&ConnectorModulePath](const FRigModuleConnector& ExpectedConnector) -> bool
					{
						return ConnectorModulePath.HasElementName(ExpectedConnector.Name);
					}
				);

				if(bRemoveAllConnectors || !bConnectorExpected)
				{
					GetHierarchy()->Modify();
					(void)Controller->RemoveElement(ConnectorKey);
					GetArrayConnectionMap().Remove(ConnectorKey);
				}
			}

			// add the missing expected connectors
			if(!bRemoveAllConnectors)
			{
				for (const FRigModuleConnector& Connector : ExpectedConnectors)
				{
					const FName ConnectorName = *Connector.Name;
					const FRigHierarchyModulePath ConnectorModulePath(InModule->Name.ToString(), Connector.Name);
					const FRigElementKey CombinedConnectorKey(ConnectorModulePath.GetPathFName(), ERigElementType::Connector);
					if(!GetHierarchy()->Contains(CombinedConnectorKey))
					{
						const FString ModulePrefix = InModule->GetElementPrefix();
						const FString ParentModulePrefix = InModule->GetParentModule() ? InModule->GetParentModule()->GetElementPrefix() : ModulePrefix;
						const FString RootModulePrefix = InModule->GetRootModule() ? InModule->GetRootModule()->GetElementPrefix() : ModulePrefix;
						FRigHierarchyExecuteContextBracket HierarchyContextGuard(GetHierarchy(), &Context);
						FControlRigExecuteContextRigModuleGuard RigModuleGuard(PublicContext, ModulePrefix, ParentModulePrefix, RootModulePrefix);
						GetHierarchy()->Modify();
						(void)Controller->AddConnector(ConnectorName, Connector.Settings);
					}
					else
					{
						// copy the connector settings
						FRigConnectorElement* ExistingConnector = GetHierarchy()->FindChecked<FRigConnectorElement>(CombinedConnectorKey);
						ExistingConnector->Settings = Connector.Settings;
					}
				}
			}

			if (bPropagateHierarchy)
			{
				PropagateHierarchyFromBPToInstances();
			}
		}
	}
}

void IControlRigAssetInterface::HandleHierarchyModified(ERigHierarchyNotification InNotification, URigHierarchy* InHierarchy, const FRigNotificationSubject& InSubject)
{
#if WITH_EDITOR

	if(GetRigVMAssetInterface()->bSuspendAllNotifications)
	{
		return;
	}

	const FRigBaseElement* InElement = InSubject.Element;
	const FRigBaseComponent* InComponent = InSubject.Component;

	switch(InNotification)
	{
		case ERigHierarchyNotification::ElementRemoved:
		{
			Modify();
			GetInfluences().OnKeyRemoved(InElement->GetKey());
			PropagateHierarchyFromBPToInstances();
			break;
		}
		case ERigHierarchyNotification::ElementRenamed:
		{
			Modify();
			if(InElement)
			{
				const FName PreviousName = InHierarchy->GetPreviousHierarchyName(InElement->GetKey());
				const FRigElementKey OldKey(PreviousName, InElement->GetType());
				HandleHierarchyElementKeyChanged(OldKey, InElement->GetKey());
			}
			break;
		}
		case ERigHierarchyNotification::ElementAdded:
		case ERigHierarchyNotification::ParentChanged:
		case ERigHierarchyNotification::ElementReordered:
		case ERigHierarchyNotification::HierarchyReset:
		case ERigHierarchyNotification::ComponentAdded:
		case ERigHierarchyNotification::ComponentRemoved:
		case ERigHierarchyNotification::ComponentContentChanged:
		{
			Modify();
			PropagateHierarchyFromBPToInstances();
			break;
		}
		case ERigHierarchyNotification::ComponentRenamed:
		{
			Modify();
			if(InComponent)
			{
				const FName PreviousName = InHierarchy->GetPreviousHierarchyName(InComponent->GetKey());
				const FRigComponentKey OldKey(InComponent->GetElementKey(), PreviousName);
				HandleHierarchyComponentKeyChanged(OldKey, InComponent->GetKey());
			}
			break;
		}
		case ERigHierarchyNotification::ComponentReparented:
		{
			Modify();
			if(InComponent)
			{
				const FRigHierarchyKey PreviousParent = InHierarchy->GetPreviousHierarchyParent(InComponent->GetKey());
				if(PreviousParent.IsElement())
				{
					const FRigComponentKey OldKey(PreviousParent.GetElement(), InComponent->GetFName());
					HandleHierarchyComponentKeyChanged(OldKey, InComponent->GetKey());
				}
			}
			break;
		}
		case ERigHierarchyNotification::ElementSelected:
		{
			bool bClearTransientControls = true;
			if (const FRigControlElement* ControlElement = Cast<FRigControlElement>(InElement))
			{
				if (ControlElement->Settings.bIsTransientControl)
				{
					bClearTransientControls = false;
				}
			}

			if(bClearTransientControls)
			{
				if(UControlRig* RigBeingDebugged = Cast<UControlRig>(GetObjectBeingDebugged()))
				{
					const FName TransientControlName = UControlRig::GetNameForTransientControl(InElement->GetKey());
					const FRigElementKey TransientControlKey(TransientControlName, ERigElementType::Control);
					if (const FRigControlElement* ControlElement = RigBeingDebugged->GetHierarchy()->Find<FRigControlElement>(TransientControlKey))
					{
						if (ControlElement->Settings.bIsTransientControl)
						{
							bClearTransientControls = false;
						}
					}
				}
			}

			if(bClearTransientControls)
			{
				ClearTransientControls();
			}
			break;
		}
		case ERigHierarchyNotification::ElementDeselected:
		{
			if (const FRigControlElement* ControlElement = Cast<FRigControlElement>(InElement))
			{
				if (ControlElement->Settings.bIsTransientControl)
				{
					ClearTransientControls();
				}
			}
			break;
		}
		default:
		{
			break;
		}
	}

	HierarchyModifiedEvent.Broadcast(InNotification, InHierarchy, InElement);
	
#endif
}

void IControlRigAssetInterface::HandleHierarchyElementKeyChanged(const FRigElementKey& InOldKey, const FRigElementKey& InNewKey)
{
	if(InOldKey == InNewKey)
	{
		return;
	}
	
	static UEnum* RigElementTypeEnum = StaticEnum<ERigElementType>();
	check(RigElementTypeEnum);

	const FString OldNameStr = InOldKey.Name.ToString();
	const FString NewNameStr = InNewKey.Name.ToString();
	const ERigElementType ElementType = InNewKey.Type; 

	// update all of the graphs with the new key
	TArray<UEdGraph*> EdGraphs;
	GetRigVMAssetInterface()->GetAllEdGraphs(EdGraphs);
	for (UEdGraph* Graph : EdGraphs)
	{
		URigVMEdGraph* RigGraph = Cast<URigVMEdGraph>(Graph);
		if (RigGraph == nullptr)
		{
			continue;
		}

		URigVMController* Controller = RigGraph->GetController();
		if(Controller == nullptr)
		{
			continue;
		}

		{
			FRigVMBlueprintCompileScope CompileScope(GetObject());
			for (UEdGraphNode* Node : RigGraph->Nodes)
			{
				if (URigVMEdGraphNode* RigNode = Cast<URigVMEdGraphNode>(Node))
				{
					if (URigVMNode* ModelNode = RigNode->GetModelNode())
					{
						TArray<URigVMPin*> ModelPins = ModelNode->GetAllPinsRecursively();
						for (URigVMPin * ModelPin : ModelPins)
						{
							if(ModelPin->GetCPPType() == RigVMTypeUtils::FNameType)
							{
								if ((ModelPin->GetCustomWidgetName() == TEXT("BoneName") && ElementType == ERigElementType::Bone) ||
									(ModelPin->GetCustomWidgetName() == TEXT("ControlName") && ElementType == ERigElementType::Control) ||
									(ModelPin->GetCustomWidgetName() == TEXT("SpaceName") && ElementType == ERigElementType::Null) ||
									(ModelPin->GetCustomWidgetName() == TEXT("CurveName") && ElementType == ERigElementType::Curve) ||
									(ModelPin->GetCustomWidgetName() == TEXT("ConnectorName") && ElementType == ERigElementType::Connector))
								{
									if (ModelPin->GetDefaultValue() == OldNameStr)
									{
										Controller->SetPinDefaultValue(ModelPin->GetPinPath(), NewNameStr, false);
									}
								}
							}
							else if (ModelPin->GetCPPTypeObject() == FRigElementKey::StaticStruct())
							{
								const FString OldDefaultValueString = ModelPin->GetDefaultValue();
								FRigElementKey OldDefaultKey;
								FRigElementKey::StaticStruct()->ImportText(*OldDefaultValueString, &OldDefaultKey, nullptr, EPropertyPortFlags::PPF_None, nullptr, FRigElementKey::StaticStruct()->GetName(), true);

								if(OldDefaultKey == InOldKey)
								{
									FString NewDefaultKeyString;
									FRigElementKey::StaticStruct()->ExportText(NewDefaultKeyString, &InNewKey, nullptr, nullptr, PPF_ExternalEditor, nullptr);
									Controller->SetPinDefaultValue(ModelPin->GetPinPath(), NewDefaultKeyString, false);
								}
							}
						}
					}
				}
			}
		}
	}

	// update all of the influences
	GetInfluences().OnKeyRenamed(InOldKey, InNewKey);
	if (IsControlRigModule() && InNewKey.Type == ERigElementType::Connector)
	{
		if (FRigElementKeyCollection* Targets = GetArrayConnectionMap().Find(InOldKey))
		{
			GetArrayConnectionMap().FindOrAdd(InNewKey, *Targets);
			GetArrayConnectionMap().Remove(InOldKey);
		}
	}
	
	PropagateHierarchyFromBPToInstances();
}

void IControlRigAssetInterface::HandleHierarchyComponentKeyChanged(const FRigComponentKey& InOldKey, const FRigComponentKey& InNewKey)
{
	if(InOldKey == InNewKey)
	{
		return;
	}
	
	// update all of the graphs with the new key
	TArray<UEdGraph*> EdGraphs;
	GetRigVMAssetInterface()->GetAllEdGraphs(EdGraphs);
	for (UEdGraph* Graph : EdGraphs)
	{
		URigVMEdGraph* RigGraph = Cast<URigVMEdGraph>(Graph);
		if (RigGraph == nullptr)
		{
			continue;
		}

		URigVMController* Controller = RigGraph->GetController();
		if(Controller == nullptr)
		{
			continue;
		}

		{
			FRigVMBlueprintCompileScope CompileScope(GetObject());
			for (UEdGraphNode* Node : RigGraph->Nodes)
			{
				if (URigVMEdGraphNode* RigNode = Cast<URigVMEdGraphNode>(Node))
				{
					if (URigVMNode* ModelNode = RigNode->GetModelNode())
					{
						TArray<URigVMPin*> ModelPins = ModelNode->GetAllPinsRecursively();
						for (URigVMPin * ModelPin : ModelPins)
						{
							if (ModelPin->GetCPPTypeObject() == FRigComponentKey::StaticStruct())
							{
								const FString OldDefaultValueString = ModelPin->GetDefaultValue();
								FRigComponentKey OldDefaultKey;
								FRigComponentKey::StaticStruct()->ImportText(*OldDefaultValueString, &OldDefaultKey, nullptr, EPropertyPortFlags::PPF_None, nullptr, FRigComponentKey::StaticStruct()->GetName(), true);

								if(OldDefaultKey == InOldKey)
								{
									FString NewDefaultKeyString;
									FRigComponentKey::StaticStruct()->ExportText(NewDefaultKeyString, &InNewKey, nullptr, nullptr, PPF_ExternalEditor, nullptr);
									Controller->SetPinDefaultValue(ModelPin->GetPinPath(), NewDefaultKeyString, false);
								}
							}
						}
					}
				}
			}
		}
	}

	PropagateHierarchyFromBPToInstances();
}

void IControlRigAssetInterface::HandleRigModulesModified(EModularRigNotification InNotification, const FRigModuleReference* InModule)
{
	bool bRecompile = true;
	switch (InNotification)
	{
		case EModularRigNotification::ModuleAdded:
		{
			if (InModule)
			{
				RefreshModuleConnectors(InModule);
				UpdateModularDependencyDelegates();
			}
			break;
		}
		case EModularRigNotification::ModuleRenamed:
		case EModularRigNotification::ModuleReparented:
		case EModularRigNotification::ModuleReordered:
		{
			if (InModule)
			{
				if (URigHierarchyController* Controller = GetHierarchyController())
				{
					if (UControlRig* CDO = GetControlRigClass()->GetDefaultObject<UControlRig>())
					{
						GetHierarchy()->Modify();
						
						struct ConnectionInfo
						{
							FString NewPath;
							FRigElementKeyCollection TargetConnections;
							FRigConnectorSettings Settings;
						};
						const FName OldModuleName = InModule->PreviousName;
						const FName NewModuleName = InModule->Name;
						
						TArray<FRigElementKey> Connectors = Controller->GetHierarchy()->GetKeysOfType<FRigConnectorElement>();
						TMap<FRigElementKey, ConnectionInfo> RenamedConnectors; // old key -> new key
						for (const FRigElementKey& Connector : Connectors)
						{
							const FRigHierarchyModulePath ConnectorModulePath(Connector.Name);
							if (ConnectorModulePath.HasModuleName(OldModuleName))
							{
								ConnectionInfo& Info = RenamedConnectors.FindOrAdd(Connector);
								Info.NewPath = ConnectorModulePath.ReplaceModuleName(NewModuleName);
								Info.Settings = CastChecked<FRigConnectorElement>(Controller->GetHierarchy()->FindChecked(Connector))->Settings;
								if (FRigElementKeyCollection* TargetKeys = GetArrayConnectionMap().Find(Connector))
								{
									Info.TargetConnections = *TargetKeys;
								}
							}
						}

						// Remove connectors
						for (TPair<FRigElementKey, ConnectionInfo>& Pair : RenamedConnectors)
						{
							Controller->RemoveElement(Pair.Key);
						}

						// Add connectors
						{
							FRigVMExtendedExecuteContext& Context = CDO->GetRigVMExtendedExecuteContext();
							FRigHierarchyExecuteContextBracket HierarchyContextGuard(Controller->GetHierarchy(), &Context);
							FControlRigExecuteContext& PublicContext = Context.GetPublicDataSafe<FControlRigExecuteContext>();
							for (TPair<FRigElementKey, ConnectionInfo>& Pair : RenamedConnectors)
							{
								const FName ConnectorName = FRigHierarchyModulePath(Pair.Value.NewPath).GetElementFName();
								const FString ModulePrefix = InModule->GetElementPrefix();
								const FString ParentModulePrefix = InModule->GetParentModule() ? InModule->GetParentModule()->GetElementPrefix() : ModulePrefix;
								const FString RootModulePrefix = InModule->GetRootModule() ? InModule->GetRootModule()->GetElementPrefix() : ModulePrefix;
								
								FControlRigExecuteContextRigModuleGuard RigModuleGuard(PublicContext, ModulePrefix, ParentModulePrefix, RootModulePrefix);
								const TGuardValue<bool> DisableErrors(Controller->bReportWarningsAndErrors, false);
								Controller->AddConnector(ConnectorName, Pair.Value.Settings);
							}
						}

						// update the target connections
						TMap<FRigElementKey, FRigElementKeyCollection> PreviousArrayConnectionMap;
						Swap(PreviousArrayConnectionMap, GetArrayConnectionMap());
						GetArrayConnectionMap().Reset();
						for(TPair<FRigElementKey, FRigElementKeyCollection>& Connection : PreviousArrayConnectionMap)
						{
							FRigElementKey ConnectorKey = Connection.Key;
							FRigElementKeyCollection& TargetKeys = Connection.Value;

							FRigHierarchyModulePath ConnectorPath(ConnectorKey.Name);
							if(ConnectorPath.ReplaceModuleNameInline(OldModuleName, NewModuleName))
							{
								ConnectorKey.Name = ConnectorPath.GetPathFName();
							}
							
							for(FRigElementKey& TargetKey : TargetKeys.Keys)
							{
								FRigHierarchyModulePath TargetPath(TargetKey.Name);
								if(TargetPath.ReplaceModuleNameInline(OldModuleName, NewModuleName))
								{
									TargetKey.Name = TargetPath.GetPathFName();
								}
							}
							GetArrayConnectionMap().Add(ConnectorKey, TargetKeys);
						}

						// update the previous module table
						for(TPair<FRigHierarchyModulePath,FName>& ModulePathToName : GetModularRigModel().PreviousModulePaths)
						{
							if(ModulePathToName.Value == OldModuleName)
							{
								Modify();
								ModulePathToName.Value = NewModuleName;
							}
						}

						UpdateConnectionMapFromModel();
						PropagateHierarchyFromBPToInstances();
					}
				}
			}
			break;
		}
		case EModularRigNotification::ModuleRemoved:
		{
			if (InModule)
			{
				RefreshModuleConnectors(InModule);
				UpdateConnectionMapFromModel();
				UpdateModularDependencyDelegates();
			}
			break;
		}
		case EModularRigNotification::ConnectionChanged:
		{
			GetHierarchy()->Modify();
			
			UpdateConnectionMapFromModel();
			HierarchyModifiedEvent.Broadcast(ERigHierarchyNotification::HierarchyReset, GetHierarchy(), {});
			break;
		}
		case EModularRigNotification::ModuleClassChanged:
		{
			if (InModule)
			{
				RefreshModuleConnectors(InModule);
				UpdateConnectionMapFromModel();
			}
			break;
		}
		case EModularRigNotification::ModuleShortNameChanged:
		{
			bRecompile = false;
			break;
		}
		case EModularRigNotification::ModuleConfigValueChanged:
		{
			bRecompile = false;
			PropagateModuleHierarchyFromBPToInstances();
			RequestConstructionOnAllModules();
			break;
		}
		case EModularRigNotification::InteractionBracketOpened:
		{
			ModulesRecompilationBracket++;
			break;
		}
		case EModularRigNotification::InteractionBracketClosed:
		case EModularRigNotification::InteractionBracketCanceled:
		{
			ModulesRecompilationBracket--;
			break;
		}
		case EModularRigNotification::ModuleSelected:
		case EModularRigNotification::ModuleDeselected:
		{
			// don't do anything during selection
			return;
		}
		default:
		{
			break;
		}
	}

	if (bRecompile && ModulesRecompilationBracket == 0)
	{
		RecompileModularRig();
	}
}

IControlRigAssetInterface::FControlValueScope::FControlValueScope(FControlRigAssetInterfacePtr InBlueprint)
: Blueprint(InBlueprint)
{
#if WITH_EDITOR
	check(Blueprint);

	if (UControlRig* CR = Cast<UControlRig>(Blueprint->GetObjectBeingDebugged()))
	{
		TArray<FRigControlElement*> Controls = CR->AvailableControls();
		for (FRigControlElement* ControlElement : Controls)
		{
			ControlValues.Add(ControlElement->GetFName(), CR->GetControlValue(ControlElement->GetFName()));
		}
	}
#endif
}

IControlRigAssetInterface::FControlValueScope::~FControlValueScope()
{
#if WITH_EDITOR
	check(Blueprint);

	if (UControlRig* CR = Cast<UControlRig>(Blueprint->GetObjectBeingDebugged()))
	{
		for (const TPair<FName, FRigControlValue>& Pair : ControlValues)
		{
			if (CR->FindControl(Pair.Key))
			{
				CR->SetControlValue(Pair.Key, Pair.Value);
			}
		}
	}
#endif
}

#undef LOCTEXT_NAMESPACE


