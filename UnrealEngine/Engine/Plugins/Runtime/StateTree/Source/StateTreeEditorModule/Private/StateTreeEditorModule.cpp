// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditorModule.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Blueprint/StateTreeConsiderationBlueprintBase.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Customizations/StateTreeAnyEnumDetails.h"
#include "Customizations/StateTreeBindingExtension.h"
#include "Customizations/StateTreeBlueprintPropertyRefDetails.h"
#include "Customizations/StateTreeEditorColorDetails.h"
#include "Customizations/StateTreeEditorDataDetails.h"
#include "Customizations/StateTreeEditorNodeDetails.h"
#include "Customizations/StateTreeEnumValueScorePairsDetails.h"
#include "Customizations/StateTreeEventDescDetails.h"
#include "Customizations/StateTreeReferenceDetails.h"
#include "Customizations/StateTreeReferenceOverridesDetails.h"
#include "Customizations/StateTreeStateDetails.h"
#include "Customizations/StateTreeStateLinkDetails.h"
#include "Customizations/StateTreeStateParametersDetails.h"
#include "Customizations/StateTreeTransitionDetails.h"
#include "Debugger/StateTreeDebuggerCommands.h"
#include "Debugger/StateTreeRewindDebuggerExtensions.h"
#include "Debugger/StateTreeRewindDebuggerTrack.h"
#include "IRewindDebuggerExtension.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeCompilerManager.h"
#include "StateTreeDelegates.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditor.h"
#include "StateTreeEditorCommands.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorSchema.h"
#include "StateTreeEditorStyle.h"
#include "StateTreeNodeClassCache.h"
#include "StateTreePropertyFunctionBase.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

DEFINE_LOG_CATEGORY(LogStateTreeEditor);

IMPLEMENT_MODULE(FStateTreeEditorModule, StateTreeEditorModule)

namespace UE::StateTree::Editor
{
	// @todo Could we make this a IModularFeature?
	static bool CompileStateTree(UStateTree& StateTree)
	{
		FStateTreeCompilerLog Log;
		return UStateTreeEditingSubsystem::CompileStateTree(&StateTree, Log);
	}

	static TSharedRef<FStateTreeNodeClassCache> InitNodeClassCache()
	{
		TSharedRef<FStateTreeNodeClassCache> NodeClassCache = MakeShareable(new FStateTreeNodeClassCache());
		NodeClassCache->AddRootScriptStruct(FStateTreeEvaluatorBase::StaticStruct());
		NodeClassCache->AddRootScriptStruct(FStateTreeTaskBase::StaticStruct());
		NodeClassCache->AddRootScriptStruct(FStateTreeConditionBase::StaticStruct());
		NodeClassCache->AddRootScriptStruct(FStateTreeConsiderationBase::StaticStruct());
		NodeClassCache->AddRootScriptStruct(FStateTreePropertyFunctionBase::StaticStruct());
		NodeClassCache->AddRootClass(UStateTreeEvaluatorBlueprintBase::StaticClass());
		NodeClassCache->AddRootClass(UStateTreeTaskBlueprintBase::StaticClass());
		NodeClassCache->AddRootClass(UStateTreeConditionBlueprintBase::StaticClass());
		NodeClassCache->AddRootClass(UStateTreeConsiderationBlueprintBase::StaticClass());
		NodeClassCache->AddRootClass(UStateTreeSchema::StaticClass());
		return NodeClassCache;
	}

}; // UE::StateTree::Editor

FStateTreeEditorModule& FStateTreeEditorModule::GetModule()
{
	return FModuleManager::LoadModuleChecked<FStateTreeEditorModule>("StateTreeEditorModule");
}

FStateTreeEditorModule* FStateTreeEditorModule::GetModulePtr()
{
	return FModuleManager::GetModulePtr<FStateTreeEditorModule>("StateTreeEditorModule");
}

void FStateTreeEditorModule::StartupModule()
{
	UE::StateTree::Delegates::OnRequestEditorHash.BindLambda([](const UStateTree& InStateTree) -> uint32 { return UStateTreeEditingSubsystem::CalculateStateTreeHash(&InStateTree); });
	UE::StateTree::Compiler::FCompilerManager::Startup();

	OnPostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddLambda([this]()
		{
			NodeClassCache = UE::StateTree::Editor::InitNodeClassCache();
		});

#if WITH_STATETREE_TRACE_DEBUGGER
	FStateTreeDebuggerCommands::Register();

	RewindDebuggerPlaybackExtension = MakePimpl<UE::StateTreeDebugger::FRewindDebuggerPlaybackExtension>();
	IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerExtension::ModularFeatureName, RewindDebuggerPlaybackExtension.Get());

	RewindDebuggerTrackCreator = MakePimpl<UE::StateTreeDebugger::FRewindDebuggerTrackCreator>();
	IModularFeatures::Get().RegisterModularFeature(RewindDebugger::IRewindDebuggerTrackCreator::ModularFeatureName, RewindDebuggerTrackCreator.Get());
#endif // WITH_STATETREE_TRACE_DEBUGGER

	MenuExtensibilityManager = MakeShareable(new FExtensibilityManager);
	ToolBarExtensibilityManager = MakeShareable(new FExtensibilityManager);

	FStateTreeEditorStyle::Register();
	FStateTreeEditorCommands::Register();

	// Register the details customizer
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeTransition", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeTransitionDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeEventDesc", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeEventDescDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeStateLink", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeStateLinkDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeEditorNode", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeEditorNodeDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeStateParameters", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeStateParametersDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeAnyEnum", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeAnyEnumDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeReference", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeReferenceDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeReferenceOverrides", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeReferenceOverridesDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeEditorColorRef", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeEditorColorRefDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeEditorColor", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeEditorColorDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeBlueprintPropertyRef", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeBlueprintPropertyRefDetails::MakeInstance));
	PropertyModule.RegisterCustomPropertyTypeLayout("StateTreeEnumValueScorePairs", FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FStateTreeEnumValueScorePairsDetails::MakeInstance));
	PropertyModule.RegisterCustomClassLayout("StateTreeState", FOnGetDetailCustomizationInstance::CreateStatic(&FStateTreeStateDetails::MakeInstance));
	PropertyModule.RegisterCustomClassLayout("StateTreeEditorData", FOnGetDetailCustomizationInstance::CreateStatic(&FStateTreeEditorDataDetails::MakeInstance));

	PropertyModule.NotifyCustomizationModuleChanged();
}

void FStateTreeEditorModule::ShutdownModule()
{
	// Unregister the details customization
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeTransition");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeEventDesc");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeStateLink");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeEditorNode");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeStateParameters");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeAnyEnum");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeReference");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeReferenceOverrides");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeEditorColorRef");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeEditorColor");
		PropertyModule.UnregisterCustomPropertyTypeLayout("StateTreeBlueprintPropertyRef");
		PropertyModule.UnregisterCustomClassLayout("StateTreeState");
		PropertyModule.UnregisterCustomClassLayout("StateTreeEditorData");
		PropertyModule.NotifyCustomizationModuleChanged();
	}

	FStateTreeEditorStyle::Unregister();
	FStateTreeEditorCommands::Unregister();

	MenuExtensibilityManager.Reset();
	ToolBarExtensibilityManager.Reset();

#if WITH_STATETREE_TRACE_DEBUGGER
	IModularFeatures::Get().UnregisterModularFeature(RewindDebugger::IRewindDebuggerTrackCreator::ModularFeatureName, RewindDebuggerTrackCreator.Get());
	IModularFeatures::Get().UnregisterModularFeature(IRewindDebuggerExtension::ModularFeatureName, RewindDebuggerPlaybackExtension.Get());
	FStateTreeDebuggerCommands::Unregister();
#endif // WITH_STATETREE_TRACE_DEBUGGER

	FCoreDelegates::OnPostEngineInit.Remove(OnPostEngineInitHandle);
	NodeClassCache.Reset();

	UE::StateTree::Compiler::FCompilerManager::Shutdown();
	UE::StateTree::Delegates::OnRequestEditorHash.Unbind();
}

TSharedRef<IStateTreeEditor> FStateTreeEditorModule::CreateStateTreeEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UStateTree* StateTree)
{
	TSharedRef<FStateTreeEditor> NewEditor(new FStateTreeEditor());
	NewEditor->InitEditor(Mode, InitToolkitHost, StateTree);
	return NewEditor;
}

void FStateTreeEditorModule::SetDetailPropertyHandlers(IDetailsView& DetailsView)
{
	DetailsView.SetExtensionHandler(MakeShared<FStateTreeBindingExtension>());
	DetailsView.SetChildrenCustomizationHandler(MakeShared<FStateTreeBindingsChildrenCustomization>());
}

TSharedPtr<FStateTreeNodeClassCache> FStateTreeEditorModule::GetNodeClassCache()
{
	check(NodeClassCache.IsValid());
	return NodeClassCache;
}

bool FStateTreeEditorModule::FEditorTypes::HasData() const
{
	return EditorData.IsValid() || EditorSchema.IsValid();
}

namespace UE::StateTreeEditor::Private
{
	TOptional<int32> GetDepth(TNotNull<const UStruct*> Struct, TNotNull<const UStruct*> MatchingParent)
	{
		int32 Depth = 0;
		if (Struct == MatchingParent)
		{
			return Depth;
		}

		for (const UStruct* TempStruct : Struct->GetSuperStructIterator())
		{
			++Depth;
			if (TempStruct == MatchingParent)
			{
				return Depth;
			}
		}

		return {};
	}
}

void FStateTreeEditorModule::RegisterEditorDataClass(TNonNullSubclassOf<const UStateTreeSchema> Schema, TNonNullSubclassOf<const UStateTreeEditorData> EditorData)
{
	FEditorTypes* EditorType = EditorTypes.FindByPredicate(
		[Schema](const FEditorTypes& Other)
		{
			return Other.Schema == Schema.Get();
		});
	if (EditorType)
	{
		ensureMsgf(EditorType->EditorData.IsExplicitlyNull(), TEXT("The type %s is already registered."), *Schema.Get()->GetName());
		EditorType->EditorData = EditorData.Get();
	}
	else
	{
		FEditorTypes& NewEditorType = EditorTypes.AddDefaulted_GetRef();
		NewEditorType.Schema = Schema.Get();
		NewEditorType.EditorData = EditorData.Get();
	}
}

void FStateTreeEditorModule::UnregisterEditorDataClass(TNonNullSubclassOf<const UStateTreeSchema> Schema)
{
	const int32 FoundIndex = EditorTypes.IndexOfByPredicate(
		[Schema](const FEditorTypes& Other)
		{
			return Other.Schema == Schema.Get();
		});
	if (FoundIndex != INDEX_NONE)
	{
		EditorTypes[FoundIndex].EditorData.Reset();
		if (!EditorTypes[FoundIndex].HasData())
		{
			EditorTypes.RemoveAtSwap(FoundIndex);
		}
	}
}

TNonNullSubclassOf<UStateTreeEditorData> FStateTreeEditorModule::GetEditorDataClass(TNonNullSubclassOf<const UStateTreeSchema> Schema) const
{
	int32 BestDepth = INT_MAX;
	const FEditorTypes* BestEditorType = nullptr;
	for (const FEditorTypes& EditorType : EditorTypes)
	{
		const UClass* OtherSchema = EditorType.Schema.Get();
		TOptional<int32> Depth = UE::StateTreeEditor::Private::GetDepth(OtherSchema, Schema.Get());
		if (Depth.IsSet() && Depth.GetValue() < BestDepth)
		{
			BestDepth = Depth.GetValue();
			BestEditorType = &EditorType;
		}
	}

	const UClass* Result = BestEditorType && BestEditorType->EditorData.Get() ? BestEditorType->EditorData.Get() : UStateTreeEditorData::StaticClass();
	return const_cast<UClass*>(Result); // NewObject wants none const UClass
}

void FStateTreeEditorModule::RegisterEditorSchemaClass(TNonNullSubclassOf<const UStateTreeSchema> Schema, TNonNullSubclassOf<const UStateTreeEditorSchema> EditorSchema)
{
	FEditorTypes* EditorType = EditorTypes.FindByPredicate(
		[Schema](const FEditorTypes& Other)
		{
			return Other.Schema == Schema.Get();
		});
	if (EditorType)
	{
		ensureMsgf(EditorType->EditorSchema.IsExplicitlyNull(), TEXT("The type %s is already registered."), *Schema.Get()->GetName());
		EditorType->EditorSchema = EditorSchema.Get();
	}
	else
	{
		FEditorTypes& NewEditorType = EditorTypes.AddDefaulted_GetRef();
		NewEditorType.Schema = Schema.Get();
		NewEditorType.EditorSchema = EditorSchema.Get();
	}
}

void FStateTreeEditorModule::UnregisterEditorSchemaClass(TNonNullSubclassOf<const UStateTreeSchema> Schema)
{
	const int32 FoundIndex = EditorTypes.IndexOfByPredicate(
		[Schema](const FEditorTypes& Other)
		{
			return Other.Schema == Schema.Get();
		});
	if (FoundIndex != INDEX_NONE)
	{
		EditorTypes[FoundIndex].EditorSchema.Reset();
		if (!EditorTypes[FoundIndex].HasData())
		{
			EditorTypes.RemoveAtSwap(FoundIndex);
		}
	}
}

TNonNullSubclassOf<UStateTreeEditorSchema> FStateTreeEditorModule::GetEditorSchemaClass(TNonNullSubclassOf<const UStateTreeSchema> Schema) const
{
	int32 BestDepth = INT_MAX;
	const FEditorTypes* BestEditorType = nullptr;
	for (const FEditorTypes& OtherEditorType : EditorTypes)
	{
		const UClass* OtherSchema = OtherEditorType.Schema.Get();
		TOptional<int32> Depth = UE::StateTreeEditor::Private::GetDepth(OtherSchema, Schema.Get());
		if (Depth.IsSet() && Depth.GetValue() < BestDepth)
		{
			BestDepth = Depth.GetValue();
			BestEditorType = &OtherEditorType;
		}
	}

	const UClass* Result = BestEditorType && BestEditorType->EditorSchema.Get() ? BestEditorType->EditorSchema.Get() : UStateTreeEditorSchema::StaticClass();
	return const_cast<UClass*>(Result); // NewObject wants none const UClass
}

#undef LOCTEXT_NAMESPACE
