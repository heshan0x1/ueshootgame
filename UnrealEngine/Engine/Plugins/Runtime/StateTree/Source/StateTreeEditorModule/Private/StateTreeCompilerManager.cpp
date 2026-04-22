// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeCompilerManager.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeDelegates.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorModule.h"
#include "StateTreeEditorPropertyBindings.h"

#include "Editor.h"
#include "StructUtilsDelegates.h"
#include "StructUtils/UserDefinedStruct.h"
#include "UObject/ObjectKey.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

namespace UE::StateTree::Compiler::Private
{
FAutoConsoleVariable CVarLogStateTreeDependencies(
	TEXT("StateTree.Compiler.LogDependenciesOnCompilation"),
	false,
	TEXT("After a StateTree compiles, log the dependencies that will be required for the asset to recompile.")
);

bool bUseDependenciesToTriggerCompilation = true;
FAutoConsoleVariableRef CVarLogStateTreeUseDependenciesToTriggerCompilation(
	TEXT("StateTree.Compiler.UseDependenciesToTriggerCompilation"),
	bUseDependenciesToTriggerCompilation,
	TEXT("Use the build dependencies to detect when a state tree needs to be linked or compiled.")
);

struct FStateTreeDependencies
{
	enum EDependencyType
	{
		DT_None = 0,
		DT_Link = 1 << 0,
		DT_Internal = 1<< 1,
		DT_Public = 1 << 2,
	};
	struct FItem
	{
		FObjectKey Key;
		EDependencyType Type = EDependencyType::DT_None;
	};
	TArray<FItem> Dependencies;
};
ENUM_CLASS_FLAGS(FStateTreeDependencies::EDependencyType);

/** Find the references that are needed by the asset. */
class FArchiveReferencingProperties : public FArchiveUObject
{
public:
	FArchiveReferencingProperties(TNotNull<const UObject*> InReferencingObject)
		: ReferencingObjectPackage(InReferencingObject->GetPackage())
		, StateTreeModulePackage(UStateTree::StaticClass()->GetOutermost())
		, StateTreeEditorModulePackage(UStateTreeEditorData::StaticClass()->GetOutermost())
		, CoreUObjectModulePackage(UObject::StaticClass()->GetOutermost())
	{
		ArIsObjectReferenceCollector = true;
		ArIgnoreOuterRef = true;
		ArIgnoreArchetypeRef = true;
		ArIgnoreClassGeneratedByRef = true;
		ArIgnoreClassRef = true;

		SetShouldSkipCompilingAssets(false);
	}

	bool IsSupportedObject(TNotNull<const UStruct*> Struct)
	{
		/** As an optimization, do not include basic structures like FVector and state tree internal types. */
		return !Struct->IsInPackage(StateTreeModulePackage)
			&& !Struct->IsInPackage(StateTreeEditorModulePackage)
			&& !Struct->IsInPackage(CoreUObjectModulePackage);
	}

	virtual FArchive& operator<<(UObject*& InSerializedObject) override
	{
		if (InSerializedObject)
		{
			if (const UStruct* AsStruct = Cast<const UStruct>(InSerializedObject))
			{
				if (IsSupportedObject(AsStruct))
				{
					Dependencies.Add(AsStruct);
				}
			}
			else
			{
				if (IsSupportedObject(InSerializedObject->GetClass()))
				{
					Dependencies.Add(InSerializedObject->GetClass());
				}
			}

			// Traversing the asset inner dependencies (instanced type).
			if (InSerializedObject->IsInPackage(ReferencingObjectPackage))
			{
				bool bAlreadyExists;
				SerializedObjects.Add(InSerializedObject, &bAlreadyExists);

				if (!bAlreadyExists)
				{
					InSerializedObject->Serialize(*this);
				}
			}
		}

		return *this;
	}

	TSet<TNotNull<const UStruct*>> Dependencies;

private:
	/** Tracks the objects which have been serialized by this archive, to prevent recursion */
	TSet<UObject*> SerializedObjects;

	TNotNull<const UPackage*> ReferencingObjectPackage;
	TNotNull<const UPackage*> StateTreeModulePackage;
	TNotNull<const UPackage*> StateTreeEditorModulePackage;
	TNotNull<const UPackage*> CoreUObjectModulePackage;
};

class FCompilerManagerImpl
{
public:
	FCompilerManagerImpl();
	~FCompilerManagerImpl();
	FCompilerManagerImpl(const FCompilerManagerImpl&) = delete;
	FCompilerManagerImpl& operator=(const FCompilerManagerImpl&) = delete;

	bool CompileInternalSynchronously(TNotNull<UStateTree*> InStateTree, FStateTreeCompilerLog& InOutLog);

private:
	bool HandleCompileStateTree(UStateTree& StateTree);
	void UpdateBindingsInstanceStructsIfNeeded(TSet<const UStruct*>& Structs, TNotNull<UStateTreeEditorData*> EditorData);
	void GatherDependencies(TNotNull<UStateTree*> StateTree);
	void LogDependencies(TNotNull<UStateTree*> StateTree) const;

	using FReplacementObjectMap = TMap<UObject*, UObject*>;
	void HandleObjectsReinstanced(const FReplacementObjectMap& ObjectMap);
	void HandlePreBeginPIE(bool bIsSimulating);
	void HandleUserDefinedStructReinstanced(const UUserDefinedStruct& UserDefinedStruct);

private:
	FDelegateHandle ObjectsReinstancedHandle;
	FDelegateHandle UserDefinedStructReinstancedHandle;
	FDelegateHandle PreBeginPIEHandle;

	TMap<TObjectKey<UStateTree>, TSharedPtr<FStateTreeDependencies>> StateTreeToDependencies;
	TMap<FObjectKey, TArray<TObjectKey<UStateTree>>> DependenciesToStateTree;
};
static TUniquePtr<FCompilerManagerImpl> CompilerManagerImpl;

FCompilerManagerImpl::FCompilerManagerImpl()
{
	UE::StateTree::Delegates::OnRequestCompile.BindRaw(this, &FCompilerManagerImpl::HandleCompileStateTree);
	ObjectsReinstancedHandle = FCoreUObjectDelegates::OnObjectsReinstanced.AddRaw(this, &FCompilerManagerImpl::HandleObjectsReinstanced);
	UserDefinedStructReinstancedHandle = UE::StructUtils::Delegates::OnUserDefinedStructReinstanced.AddRaw(this, &FCompilerManagerImpl::HandleUserDefinedStructReinstanced);
	PreBeginPIEHandle = FEditorDelegates::PreBeginPIE.AddRaw(this, &FCompilerManagerImpl::HandlePreBeginPIE);
}

FCompilerManagerImpl::~FCompilerManagerImpl()
{
	FEditorDelegates::PreBeginPIE.Remove(PreBeginPIEHandle);
	UE::StructUtils::Delegates::OnUserDefinedStructReinstanced.Remove(UserDefinedStructReinstancedHandle);
	FCoreUObjectDelegates::OnObjectsReinstanced.Remove(ObjectsReinstancedHandle);
	UE::StateTree::Delegates::OnRequestCompile.Unbind();
}

bool FCompilerManagerImpl::CompileInternalSynchronously(TNotNull<UStateTree*> StateTree, FStateTreeCompilerLog& Log)
{
	UStateTreeEditingSubsystem::ValidateStateTree(StateTree);
	FStateTreeCompiler Compiler(Log);
	const bool bCompilationResult = Compiler.Compile(StateTree);
	if (bCompilationResult)
	{
		const uint32 EditorDataHash = UStateTreeEditingSubsystem::CalculateStateTreeHash(StateTree);

		// Success
		StateTree->LastCompiledEditorDataHash = EditorDataHash;
		UE_LOG(LogStateTreeEditor, Log, TEXT("Compile StateTree '%s' succeeded."), *StateTree->GetFullName());
	}
	else
	{
		// Make sure not to leave stale data on failed compile.
		StateTree->ResetCompiled();
		StateTree->LastCompiledEditorDataHash = 0;

		UE_LOG(LogStateTreeEditor, Error, TEXT("Failed to compile '%s', errors follow."), *StateTree->GetFullName());
		Log.DumpToLog(LogStateTreeEditor);
	}

	UE::StateTree::Delegates::OnPostCompile.Broadcast(*StateTree);

	GatherDependencies(StateTree);

	if (CVarLogStateTreeDependencies->GetBool())
	{
		LogDependencies(StateTree);
	}

	return bCompilationResult;
}

void FCompilerManagerImpl::UpdateBindingsInstanceStructsIfNeeded(TSet<const UStruct*>& Structs, TNotNull<UStateTreeEditorData*> EditorData)
{
	bool bShouldUpdate = false;
	EditorData->VisitAllNodes([&Structs, &bShouldUpdate](const UStateTreeState* State, const FStateTreeBindableStructDesc& Desc, const FStateTreeDataView Value)
		{
			if (Structs.Contains(Value.GetStruct()))
			{
				bShouldUpdate = true;
				return EStateTreeVisitor::Break;
			}
			return EStateTreeVisitor::Continue;
		});

	if (!bShouldUpdate)
	{
		bShouldUpdate = EditorData->GetEditorPropertyBindings()->ContainsAnyStruct(Structs);
	}

	if (bShouldUpdate)
	{
		EditorData->UpdateBindingsInstanceStructs();
	}
}

bool FCompilerManagerImpl::HandleCompileStateTree(UStateTree& StateTree)
{
	FStateTreeCompilerLog Log;
	return CompileInternalSynchronously(&StateTree, Log);
}

void FCompilerManagerImpl::HandlePreBeginPIE(const bool bIsSimulating)
{
	for (TObjectIterator<UStateTree> It; It; ++It)
	{
		check(!It->HasAnyFlags(RF_ClassDefaultObject));
		It->CompileIfChanged();
	}
}

void FCompilerManagerImpl::HandleObjectsReinstanced(const FReplacementObjectMap& ObjectMap)
{
	if (ObjectMap.IsEmpty())
	{
		return;
	}

	TArray<const UObject*> ObjectsToBeReplaced;
	ObjectsToBeReplaced.Reserve(ObjectMap.Num());
	for (TMap<UObject*, UObject*>::TConstIterator It(ObjectMap); It; ++It)
	{
		if (const UObject* ObjectToBeReplaced = It->Value)
		{
			ObjectsToBeReplaced.Add(ObjectToBeReplaced);
		}
	}

	TSet<const UStruct*> StructsToBeReplaced;
	StructsToBeReplaced.Reserve(ObjectsToBeReplaced.Num());
	for (const UObject* ObjectToBeReplaced : ObjectsToBeReplaced)
	{
		// It's a UClass or a UScriptStruct
		if (const UStruct* StructToBeReplaced = Cast<const UStruct>(ObjectToBeReplaced))
		{
			StructsToBeReplaced.Add(StructToBeReplaced);
		}
		else
		{
			StructsToBeReplaced.Add(ObjectToBeReplaced->GetClass());
		}
	}

	if (UE::StateTree::Compiler::Private::bUseDependenciesToTriggerCompilation)
	{
		TArray<TNotNull<UStateTree*>> StateTreeToLink;
		for (const UStruct* StructToBeReplaced : StructsToBeReplaced)
		{
			const FObjectKey StructToReplacedKey = StructToBeReplaced;
			TArray<TObjectKey<UStateTree>>* Dependencies = DependenciesToStateTree.Find(StructToReplacedKey);
			if (Dependencies)
			{
				for (const TObjectKey<UStateTree>& StateTreeKey : *Dependencies)
				{
					if (UStateTree* StateTree = StateTreeKey.ResolveObjectPtr())
					{
						StateTreeToLink.AddUnique(StateTree);
					}
				}
			}
		}

		for (UStateTree* StateTree : StateTreeToLink)
		{
			if (!StateTree->Link())
			{
				UE_LOG(LogStateTree, Error, TEXT("%s failed to link after Object reinstantiation. Take a look at the asset for any errors. Asset will not be usable at runtime."), *StateTree->GetPathName());
			}
		}
	}
	else
	{
		for (TObjectIterator<UStateTreeEditorData> It; It; ++It)
		{
			UStateTreeEditorData* StateTreeEditorData = *It;
			UpdateBindingsInstanceStructsIfNeeded(StructsToBeReplaced, StateTreeEditorData);
		}

		for (TObjectIterator<UStateTree> It; It; ++It)
		{
			UStateTree* StateTree = *It;
			check(!StateTree->HasAnyFlags(RF_ClassDefaultObject));
			bool bShouldRelink = false;

			// Relink if one of the out of date objects got reinstanced.
			if (StateTree->OutOfDateStructs.Num() > 0)
			{
				for (const FObjectKey& OutOfDateObjectKey : StateTree->OutOfDateStructs)
				{
					if (const UObject* OutOfDateObject = OutOfDateObjectKey.ResolveObjectPtr())
					{
						if (ObjectMap.Contains(OutOfDateObject))
						{
							bShouldRelink = true;
							break;
						}
					}
				}
			}

			// If the asset is not linked yet (or has failed), no need to link.
			if (!bShouldRelink && !StateTree->bIsLinked)
			{
				continue;
			}

			// Relink only if the reinstantiated object belongs to this asset,
			// or anything from the property binding refers to the classes of the reinstantiated object.
			if (!bShouldRelink)
			{
				for (const UObject* ObjectToBeReplaced : ObjectsToBeReplaced)
				{
					if (ObjectToBeReplaced->IsInOuter(StateTree))
					{
						bShouldRelink = true;
						break;
					}
				}
			}

			if (!bShouldRelink)
			{
				bShouldRelink |= StateTree->PropertyBindings.ContainsAnyStruct(StructsToBeReplaced);
			}

			if (bShouldRelink)
			{
				if (!StateTree->Link())
				{
					UE_LOG(LogStateTree, Error, TEXT("%s failed to link after Object reinstantiation. Take a look at the asset for any errors. Asset will not be usable at runtime."), *StateTree->GetPathName());
				}
			}
		}
	}
}

void FCompilerManagerImpl::HandleUserDefinedStructReinstanced(const UUserDefinedStruct& UserDefinedStruct)
{
	if (UE::StateTree::Compiler::Private::bUseDependenciesToTriggerCompilation)
	{
		TSet<TNotNull<UStateTree*>> StateTreeToLink;
		const FObjectKey StructToReplacedKey = &UserDefinedStruct;
		TArray<TObjectKey<UStateTree>>* Dependencies = DependenciesToStateTree.Find(StructToReplacedKey);
		if (Dependencies)
		{
			for (const TObjectKey<UStateTree>& StateTreeKey : *Dependencies)
			{
				if (UStateTree* StateTree = StateTreeKey.ResolveObjectPtr())
				{
					StateTreeToLink.Add(StateTree);
				}
			}
		}

		for (UStateTree* StateTree : StateTreeToLink)
		{
			if (!StateTree->Link())
			{
				UE_LOG(LogStateTree, Error, TEXT("%s failed to link after Object reinstantiation. Take a look at the asset for any errors. Asset will not be usable at runtime."), *StateTree->GetPathName());
			}
		}
	}
	else
	{
		TSet<const UStruct*> Structs;
		Structs.Add(&UserDefinedStruct);

		for (TObjectIterator<UStateTreeEditorData> It; It; ++It)
		{
			UStateTreeEditorData* StateTreeEditorData = *It;
			UpdateBindingsInstanceStructsIfNeeded(Structs, StateTreeEditorData);
		}

		for (TObjectIterator<UStateTree> It; It; ++It)
		{
			UStateTree* StateTree = *It;
			if (StateTree->PropertyBindings.ContainsAnyStruct(Structs))
			{
				if (!StateTree->Link())
				{
					UE_LOG(LogStateTree, Error, TEXT("%s failed to link after Struct reinstantiation. Take a look at the asset for any errors. Asset will not be usable at runtime."), *StateTree->GetPathName());
				}
			}
		}
	}
}

void FCompilerManagerImpl::GatherDependencies(TNotNull<UStateTree*> StateTree)
{
	// Find the tree in the StateTreeToDependencies
	const TObjectKey<UStateTree> StateTreeKey = StateTree;
	TSharedPtr<FStateTreeDependencies>& FoundDependencies = StateTreeToDependencies.FindOrAdd(StateTreeKey);
	
	// Remove all from DependenciesToStateTree
	if (FoundDependencies)
	{
		for (FStateTreeDependencies::FItem& Item : FoundDependencies->Dependencies)
		{
			TArray<TObjectKey<UStateTree>>* FoundKey = DependenciesToStateTree.Find(Item.Key);
			if (FoundKey)
			{
				FoundKey->RemoveSingleSwap(StateTreeKey);
			}
		}
		FoundDependencies->Dependencies.Reset();
	}
	else
	{
		FoundDependencies = MakeShared<FStateTreeDependencies>();
	}

	auto AddDependency = [this, StateTreeKey, FoundDependencies](TNotNull<const UStruct*> Object, FStateTreeDependencies::EDependencyType DependencyType)
		{
			const FObjectKey ObjectKey = Object;
			DependenciesToStateTree.FindOrAdd(ObjectKey).AddUnique(StateTreeKey);

			if (FStateTreeDependencies::FItem* FoundItem = FoundDependencies->Dependencies.FindByPredicate([ObjectKey](const FStateTreeDependencies::FItem& Other)
				{
					return Other.Key == ObjectKey;
				}))
			{
				FoundItem->Type |= DependencyType;
			}
			else
			{
				FoundDependencies->Dependencies.Add(FStateTreeDependencies::FItem{.Key = ObjectKey, .Type = DependencyType});
			}
		};

	// Gather new inner dependencies
	UStateTreeEditorData* EditorData = CastChecked<UStateTreeEditorData>(StateTree->EditorData);
	if (EditorData)
	{
		// Internal
		{
			FArchiveReferencingProperties DependencyArchive(StateTree);
			EditorData->Serialize(DependencyArchive);
			for (const UStruct* Dependency : DependencyArchive.Dependencies)
			{
				AddDependency(Dependency, FStateTreeDependencies::EDependencyType::DT_Internal);
			}
		}
		// Public
		{
			FArchiveReferencingProperties DependencyArchive(StateTree);
			const_cast<FInstancedPropertyBag&>(EditorData->GetRootParametersPropertyBag()).Serialize(DependencyArchive);
			for (const UStruct* Dependency : DependencyArchive.Dependencies)
			{
				AddDependency(Dependency, FStateTreeDependencies::EDependencyType::DT_Public);
			}
			if (EditorData->Schema)
			{
				AddDependency(EditorData->Schema->GetClass(), FStateTreeDependencies::EDependencyType::DT_Public);
			}
		}
		// Link
		{
			TMap<FGuid, const FPropertyBindingDataView> AllStructValues;
			EditorData->GetAllStructValues(AllStructValues);
			auto AddBindingPathDependencies = [&AddDependency , &AllStructValues](const FPropertyBindingPath& PropertyPath)
				{
					const FPropertyBindingDataView* FoundStruct = AllStructValues.Find(PropertyPath.GetStructID());
					if (FoundStruct)
					{
						FString Error;
						TArray<FPropertyBindingPathIndirection> Indirections;
						if (PropertyPath.ResolveIndirectionsWithValue(*FoundStruct, Indirections, &Error))
						{
							for (const FPropertyBindingPathIndirection& Indirection : Indirections)
							{
								if (Indirection.GetInstanceStruct())
								{
									AddDependency(Indirection.GetInstanceStruct(), FStateTreeDependencies::EDependencyType::DT_Link);
								}
								else if (Indirection.GetContainerStruct())
								{
									AddDependency(Indirection.GetContainerStruct(), FStateTreeDependencies::EDependencyType::DT_Link);
								}
							}
						}
					}
				};

			EditorData->GetEditorPropertyBindings()->VisitBindings([&AddBindingPathDependencies](const FPropertyBindingBinding& Binding)
				{
					AddBindingPathDependencies(Binding.GetSourcePath());
					AddBindingPathDependencies(Binding.GetTargetPath());
					return FPropertyBindingBindingCollection::EVisitResult::Continue;
				});
		}
	}
}

void FCompilerManagerImpl::LogDependencies(TNotNull<UStateTree*> StateTree) const
{
	FStringBuilderBase LogString;
	LogString << TEXT("StateTree Dependencies (asset: '");
	StateTree->GetFullName(LogString);
	LogString << TEXT("')\n");
	
	const TObjectKey<UStateTree> StateTreeKey = StateTree;
	const TSharedPtr<FStateTreeDependencies>* FoundDependencies = StateTreeToDependencies.Find(StateTreeKey);
	if (FoundDependencies != nullptr && FoundDependencies->IsValid())
	{
		auto PrintType = [&LogString](FStateTreeDependencies::EDependencyType Type)
			{
				bool bPrinted = false;
				auto PrintSeparator = [&bPrinted, &LogString]()
					{
						if (bPrinted)
						{
							LogString << TEXT(" | ");
						}
						bPrinted = true;
					};
				if (EnumHasAnyFlags(Type, FStateTreeDependencies::EDependencyType::DT_Public))
				{
					PrintSeparator();
					LogString << TEXT("Public");
				}
				if (EnumHasAnyFlags(Type, FStateTreeDependencies::EDependencyType::DT_Internal))
				{
					PrintSeparator();
					LogString << TEXT("Internal");
				}
				if (EnumHasAnyFlags(Type, FStateTreeDependencies::EDependencyType::DT_Link))
				{
					PrintSeparator();
					LogString << TEXT("Link");
				}
			};
		for (const FStateTreeDependencies::FItem& Item : (*FoundDependencies)->Dependencies)
		{
			LogString << TEXT("  ");
			if (const UObject* Object = Item.Key.ResolveObjectPtr())
			{
				Object->GetFullName(LogString);
			}
			else
			{
				LogString << TEXT(" [None]");
			}

			LogString << TEXT(" [");
			PrintType(Item.Type);
			LogString << TEXT("]\n");
		}
	}
	else
	{
		LogString << TEXT("  No Dependency");
	}

	UE_LOG(LogStateTreeEditor, Log, TEXT("%s"), LogString.ToString());
}


} // namespace UE::StateTree::Compiler::Private


namespace UE::StateTree::Compiler
{

void FCompilerManager::Startup()
{
	Private::CompilerManagerImpl = MakeUnique<Private::FCompilerManagerImpl>();
}

void FCompilerManager::Shutdown()
{
	Private::CompilerManagerImpl.Reset();
}

bool FCompilerManager::CompileSynchronously(TNotNull<UStateTree*> StateTree)
{
	FStateTreeCompilerLog Log;
	return CompileSynchronously(StateTree, Log);
}

bool FCompilerManager::CompileSynchronously(TNotNull<UStateTree*> StateTree, FStateTreeCompilerLog& Log)
{
	if (ensureMsgf(Private::CompilerManagerImpl.IsValid(), TEXT("Can't compile the asset when the module is not available.")))
	{
		return Private::CompilerManagerImpl->CompileInternalSynchronously(StateTree, Log);
	}
	return false;
}

} // UE::StateTree::Compiler

