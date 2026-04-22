// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditingSubsystem.h"

#include "SStateTreeView.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeCompilerManager.h"
#include "StateTreeDelegates.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorModule.h"
#include "StateTreeEditorSchema.h"
#include "StateTreeObjectHash.h"
#include "StateTreeTaskBase.h"
#include "UObject/UObjectGlobals.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StateTreeEditingSubsystem)


UStateTreeEditingSubsystem::UStateTreeEditingSubsystem()
{
	PostGarbageCollectHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddUObject(this, &UStateTreeEditingSubsystem::HandlePostGarbageCollect);
	PostCompileHandle = UE::StateTree::Delegates::OnPostCompile.AddUObject(this, &UStateTreeEditingSubsystem::HandlePostCompile);
}

void UStateTreeEditingSubsystem::BeginDestroy()
{
	UE::StateTree::Delegates::OnPostCompile.Remove(PostCompileHandle);
	FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostGarbageCollectHandle);
	Super::BeginDestroy();
}

bool UStateTreeEditingSubsystem::CompileStateTree(TNotNull<UStateTree*> InStateTree, FStateTreeCompilerLog& InOutLog)
{
	return UE::StateTree::Compiler::FCompilerManager::CompileSynchronously(InStateTree, InOutLog);
}

TSharedPtr<FStateTreeViewModel> UStateTreeEditingSubsystem::FindViewModel(TNotNull<const UStateTree*> InStateTree) const
{
	const FObjectKey StateTreeKey = InStateTree;
	TSharedPtr<FStateTreeViewModel> ViewModelPtr = StateTreeViewModels.FindRef(StateTreeKey);
	if (ViewModelPtr)
	{
		// The StateTree could be re-instantiated. Can occur when the object is destroyed and recreated in a pool or when reloaded in editor.
		//The object might have the same pointer value or the same path but it's a new object and all weakptr are now invalid.
		if (ViewModelPtr->GetStateTree() == InStateTree)
		{
			return ViewModelPtr.ToSharedRef();
		}
	}
	return nullptr;
}

TSharedRef<FStateTreeViewModel> UStateTreeEditingSubsystem::FindOrAddViewModel(TNotNull<UStateTree*> InStateTree)
{
	const FObjectKey StateTreeKey = InStateTree;
	TSharedPtr<FStateTreeViewModel> ViewModelPtr = StateTreeViewModels.FindRef(StateTreeKey);
	if (ViewModelPtr)
	{
		// The StateTree could be re-instantiated. Can occur when the object is destroyed and recreated in a pool or when reloaded in editor.
		//The object might have the same pointer value or the same path but it's a new object and all weakptr are now invalid.
		if (ViewModelPtr->GetStateTree() == InStateTree)
		{
			return ViewModelPtr.ToSharedRef();
		}
		else
		{
			StateTreeViewModels.Remove(StateTreeKey);
			ViewModelPtr = nullptr;
		}
	}

	ValidateStateTree(InStateTree);

	TSharedRef<FStateTreeViewModel> SharedModel = StateTreeViewModels.Add(StateTreeKey, MakeShared<FStateTreeViewModel>()).ToSharedRef();
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(InStateTree->EditorData);
	SharedModel->Init(EditorData);

	return SharedModel;
}

TSharedRef<SWidget> UStateTreeEditingSubsystem::GetStateTreeView(TSharedRef<FStateTreeViewModel> InViewModel, const TSharedRef<FUICommandList>& TreeViewCommandList)
{
	return SNew(SStateTreeView, InViewModel, TreeViewCommandList);
}

void UStateTreeEditingSubsystem::ValidateStateTree(TNotNull<UStateTree*> InStateTree)
{
	auto FixChangedStateLinkName = [](FStateTreeStateLink& StateLink, const TMap<FGuid, FName>& IDToName) -> bool
		{
			if (StateLink.ID.IsValid())
			{
				const FName* Name = IDToName.Find(StateLink.ID);
				if (Name == nullptr)
				{
					// Missing link, we'll show these in the UI
					return false;
				}
				if (StateLink.Name != *Name)
				{
					// Name changed, fix!
					StateLink.Name = *Name;
					return true;
				}
			}
			return false;
		};

	auto ValidateLinkedStates = [&FixChangedStateLinkName](TNotNull<UStateTreeEditorData*> TreeData)
		{
			// Make sure all state links are valid and update the names if needed.

			// Create ID to state name map.
			TMap<FGuid, FName> IDToName;

			TreeData->VisitHierarchy([&IDToName](const UStateTreeState& State, UStateTreeState* /*ParentState*/)
			{
				IDToName.Add(State.ID, State.Name);
				return EStateTreeVisitor::Continue;
			});
		
			// Fix changed names.
			TreeData->VisitHierarchy([&IDToName, FixChangedStateLinkName](UStateTreeState& State, UStateTreeState* /*ParentState*/)
			{
				constexpr bool bMarkDirty = false;
				State.Modify(bMarkDirty);
				if (State.Type == EStateTreeStateType::Linked)
				{
					FixChangedStateLinkName(State.LinkedSubtree, IDToName);
				}
					
				for (FStateTreeTransition& Transition : State.Transitions)
				{
					FixChangedStateLinkName(Transition.State, IDToName);
				}

				return EStateTreeVisitor::Continue;
			});
		};

	auto FixEditorData = [](TNotNull<UStateTree*> StateTree)
	{
		UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
		// The schema is defined in the EditorData. If we can't find the editor data (probably because the class doesn't exist anymore), then try the compiled schema in the state tree asset.
		TSubclassOf<const UStateTreeSchema> SchemaClass;
		if (EditorData && EditorData->Schema)
		{
			SchemaClass = EditorData->Schema->GetClass();
		}
		else if (StateTree->GetSchema())
		{
			SchemaClass = StateTree->GetSchema()->GetClass();
		}

		if (SchemaClass.Get() == nullptr)
		{
			UE_LOG(LogStateTreeEditor, Error, TEXT("The state tree '%s' does not have a schema."), *StateTree->GetPathName());
			return;
		}

		TNonNullSubclassOf<UStateTreeEditorData> EditorDataClass = FStateTreeEditorModule::GetModule().GetEditorDataClass(SchemaClass.Get());
		if (EditorData == nullptr)
		{
			EditorData = NewObject<UStateTreeEditorData>(StateTree, EditorDataClass.Get(), FName(), RF_Transactional);
			EditorData->AddRootState();
			EditorData->Schema = NewObject<UStateTreeSchema>(EditorData, SchemaClass.Get());
			EditorData->EditorBindings.SetBindingsOwner(EditorData);

			constexpr bool bMarkDirty = false;
			StateTree->Modify(bMarkDirty);
			StateTree->EditorData = EditorData;
		}
		else if (!EditorData->IsA(EditorDataClass.Get()))
		{
			// The current EditorData is not of the correct type. The data needs to be patched by the schema desired editor data subclass.
			UStateTreeEditorData* PreviousEditorData = EditorData;
			EditorData = CastChecked<UStateTreeEditorData>(StaticDuplicateObject(EditorData, StateTree, FName(), RF_Transactional, EditorDataClass.Get()));
			if (EditorData->SubTrees.Num() == 0)
			{
				EditorData->AddRootState();
			}
			if (EditorData->Schema == nullptr || !EditorData->Schema->IsA(SchemaClass.Get()))
			{
				EditorData->Schema = NewObject<UStateTreeSchema>(EditorData, SchemaClass.Get());
			}
			EditorData->EditorBindings.SetBindingsOwner(EditorData);

			constexpr bool bMarkDirty = false;
			StateTree->Modify(bMarkDirty);
			StateTree->EditorData = EditorData;

			// Trash the previous EditorData
			const FName TrashStateTreeName = MakeUniqueObjectName(GetTransientPackage(), UStateTree::StaticClass(), *FString::Printf(TEXT("TRASH_%s"), *UStateTree::StaticClass()->GetName()));
			UStateTree* TransientOuter = NewObject<UStateTree>(GetTransientPackage(), TrashStateTreeName, RF_Transient);
			const FName TrashSchemaName = *FString::Printf(TEXT("TRASH_%s"), *PreviousEditorData->GetName());
			constexpr ERenameFlags RenameFlags = REN_DoNotDirty | REN_DontCreateRedirectors;
			PreviousEditorData->Rename(*TrashSchemaName.ToString(), TransientOuter, RenameFlags);
			PreviousEditorData->SetFlags(RF_Transient);
		}
	};

	auto FixEditorSchema = [](TNotNull<UStateTreeEditorData*> EditorData)
		{
			TSubclassOf<const UStateTreeSchema> SchemaClass = EditorData->Schema ? EditorData->Schema->GetClass() : nullptr;
			if (SchemaClass.Get() == nullptr)
			{
				return;
			}

			TNonNullSubclassOf<UStateTreeEditorSchema> EditorSchemaClass = FStateTreeEditorModule::GetModule().GetEditorSchemaClass(SchemaClass.Get());
			if (EditorData->EditorSchema == nullptr)
			{
				EditorData->EditorSchema = NewObject<UStateTreeEditorSchema>(EditorData, EditorSchemaClass.Get(), FName(), RF_Transactional);
			}
			else if (!EditorData->EditorSchema->IsA(EditorSchemaClass.Get()))
			{
				// The current EditorSchema is not of the correct type. The data needs to be patched by the schema desired editor data subclass.
				UStateTreeEditorSchema* PreviousEditorSchema = EditorData->EditorSchema;
				EditorData->EditorSchema = CastChecked<UStateTreeEditorSchema>(StaticDuplicateObject(PreviousEditorSchema, EditorData, FName(), RF_Transactional, EditorSchemaClass.Get()));

				// Trash the previous EditorData
				const FName TrashName = MakeUniqueObjectName(GetTransientPackage(), UStateTreeEditorSchema::StaticClass(), *FString::Printf(TEXT("TRASH_%s"), *PreviousEditorSchema->GetName()));
				constexpr ERenameFlags RenameFlags = REN_DoNotDirty | REN_DontCreateRedirectors;
				PreviousEditorSchema->Rename(*TrashName.ToString(), PreviousEditorSchema, RenameFlags);
				PreviousEditorSchema->SetFlags(RF_Transient);
			}
		};

	auto RemoveUnusedBindings = [](TNotNull<UStateTreeEditorData*> EditorData)
		{
			TMap<FGuid, const FPropertyBindingDataView> AllStructValues;
			EditorData->GetAllStructValues(AllStructValues);
			EditorData->GetPropertyEditorBindings()->RemoveInvalidBindings(AllStructValues);
		};

	auto UpdateLinkedStateParameters = [](TNotNull<UStateTreeEditorData*> TreeData)
		{
			const EStateTreeVisitor Result = TreeData->VisitHierarchy([](UStateTreeState& State, UStateTreeState* /*ParentState*/)
			{
				if (State.Type == EStateTreeStateType::Linked
					|| State.Type == EStateTreeStateType::LinkedAsset)
				{
					constexpr bool bMarkDirty = false;
					State.Modify(bMarkDirty);
					State.UpdateParametersFromLinkedSubtree();
				}
				return EStateTreeVisitor::Continue;
			});
		};

	auto UpdateTransactionalFlags = [](TNotNull<UStateTreeEditorData*> EditorData)
		{
			for (UStateTreeState* SubTree : EditorData->SubTrees)
			{
				TArray<UStateTreeState*> Stack;

				Stack.Add(SubTree);
				while (!Stack.IsEmpty())
				{
					if (UStateTreeState* State = Stack.Pop())
					{
						State->SetFlags(RF_Transactional);

						for (UStateTreeState* ChildState : State->Children)
						{
							Stack.Add(ChildState);
						}
					}
				}
			}
		};

	FixEditorData(InStateTree);
	if (InStateTree->EditorData)
	{
		constexpr bool bMarkDirty = false;
		InStateTree->EditorData->Modify(bMarkDirty);

		UStateTreeEditorData* EditorData = CastChecked<UStateTreeEditorData>(InStateTree->EditorData);
		FixEditorSchema(EditorData);

		EditorData->ReparentStates();
		if (EditorData->EditorSchema)
		{
			EditorData->EditorSchema->Validate(InStateTree);
		}

		RemoveUnusedBindings(EditorData);
		ValidateLinkedStates(EditorData);
		UpdateLinkedStateParameters(EditorData);
		UpdateTransactionalFlags(EditorData);
	}
}

uint32 UStateTreeEditingSubsystem::CalculateStateTreeHash(TNotNull<const UStateTree*> InStateTree)
{
	uint32 EditorDataHash = 0;
	if (InStateTree->EditorData != nullptr)
	{
		FStateTreeObjectCRC32 Archive;
		EditorDataHash = Archive.Crc32(InStateTree->EditorData, 0);
	}

	return EditorDataHash;
}

void UStateTreeEditingSubsystem::HandlePostGarbageCollect()
{
	// Remove the stale viewmodels
	for (TMap<FObjectKey, TSharedPtr<FStateTreeViewModel>>::TIterator It(StateTreeViewModels); It; ++It)
	{
		if (!It.Key().ResolveObjectPtr())
		{
			It.RemoveCurrent();
		}
		else if (!It.Value() || !It.Value()->GetStateTree())
		{
			It.RemoveCurrent();
		}
	}
}

void UStateTreeEditingSubsystem::HandlePostCompile(const UStateTree& InStateTree)
{
	// Notify the UI that something changed. Make sure to not request a new viewmodel. That way, we are not creating new viewmodel when cooking/PIE.
	if (TSharedPtr<FStateTreeViewModel> ViewModel = FindViewModel(&InStateTree))
	{
		ViewModel->NotifyAssetChangedExternally();
	}
}
