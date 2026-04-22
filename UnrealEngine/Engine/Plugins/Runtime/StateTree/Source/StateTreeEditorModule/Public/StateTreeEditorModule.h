// Copyright Epic Games, Inc. All Rights Reserved.

#pragma  once

#include "Modules/ModuleInterface.h"
#include "Logging/LogMacros.h"
#include "Templates/NonNullSubclassOf.h"
#include "Templates/PimplPtr.h"
#include "Toolkits/AssetEditorToolkit.h"

#define UE_API STATETREEEDITORMODULE_API

class UStateTree;
class UStateTreeEditorData;
class UStateTreeEditorSchema;
class UStateTreeSchema;
class UUserDefinedStruct;
class IStateTreeEditor;
struct FStateTreeNodeClassCache;

namespace UE::StateTree::Compiler
{
	struct FPostInternalContext;
}

namespace UE::StateTreeDebugger
{
	class FRewindDebuggerPlaybackExtension;
	class FRewindDebuggerRecordingExtension;
	struct FRewindDebuggerTrackCreator;
}

STATETREEEDITORMODULE_API DECLARE_LOG_CATEGORY_EXTERN(LogStateTreeEditor, Log, All);

/**
* The public interface to this module
*/
class FStateTreeEditorModule : public IModuleInterface, public IHasMenuExtensibility, public IHasToolBarExtensibility
{
public:
	//~Begin IModuleInterface
	UE_API virtual void StartupModule() override;
	UE_API virtual void ShutdownModule() override;
	//~End IModuleInterface

	/** Gets this module, will attempt to load and should always exist. */
	static UE_API FStateTreeEditorModule& GetModule();

	/** Gets this module, will not attempt to load and may not exist. */
	static UE_API FStateTreeEditorModule* GetModulePtr();

	/** Creates an instance of StateTree editor. Only virtual so that it can be called across the DLL boundary. */
	UE_API virtual TSharedRef<IStateTreeEditor> CreateStateTreeEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UStateTree* StateTree);

	/** Sets the Details View with required State Tree Detail Property Handlers */
	static UE_API void SetDetailPropertyHandlers(IDetailsView& DetailsView);

	virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager() override
	{
		return MenuExtensibilityManager;
	}
	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager() override
	{
		return ToolBarExtensibilityManager;
	}

	UE_API TSharedPtr<FStateTreeNodeClassCache> GetNodeClassCache();
	
	DECLARE_EVENT_OneParam(FStateTreeEditorModule, FOnRegisterLayoutExtensions, FLayoutExtender&);
	FOnRegisterLayoutExtensions& OnRegisterLayoutExtensions()
	{
		return RegisterLayoutExtensions;
	}

	DECLARE_EVENT_OneParam(FStateTreeEditorModule, FPostInternalCompile, const UE::StateTree::Compiler::FPostInternalContext&);
	/**
	 * Handle post internal compilation for all state tree assets.
	 * The state tree asset compiled successfully.
	 * @note Use the UStateTreeEditorExtension::HandlePostInternalCompile for controlling a single asset.
	 * @note Use the UStateTreeEditorSchema::HandlePostInternalCompile for controlling a type of state tree asset.
	 */
	FPostInternalCompile& OnPostInternalCompile()
	{
		return PostInternalCompile;
	}

	/** Register the editor data type for a specific schema. */
	UE_API void RegisterEditorDataClass(TNonNullSubclassOf<const UStateTreeSchema> Schema, TNonNullSubclassOf<const UStateTreeEditorData> EditorData);
	/** Unregister the editor data type for a specific schema. */
	UE_API void UnregisterEditorDataClass(TNonNullSubclassOf<const UStateTreeSchema> Schema);
	/** Get the editor data type for a specific schema. */
	UE_API TNonNullSubclassOf<UStateTreeEditorData> GetEditorDataClass(TNonNullSubclassOf<const UStateTreeSchema> Schema) const;

	/** Register the editor schema type for a specific schema. */
	void RegisterEditorSchemaClass(TNonNullSubclassOf<const UStateTreeSchema> Schema, TNonNullSubclassOf<const UStateTreeEditorSchema> EditorSchema);
	/** Unregister the editor schema type for a specific schema. */
	void UnregisterEditorSchemaClass(TNonNullSubclassOf<const UStateTreeSchema> Schema);
	/** Get the editor data type for a specific schema. */
	TNonNullSubclassOf<UStateTreeEditorSchema> GetEditorSchemaClass(TNonNullSubclassOf<const UStateTreeSchema> Schema) const;

protected:
	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;
	TSharedPtr<FStateTreeNodeClassCache> NodeClassCache;

	struct FEditorTypes
	{
		TWeakObjectPtr<const UClass> Schema;
		TWeakObjectPtr<const UClass> EditorData;
		TWeakObjectPtr<const UClass> EditorSchema;
		bool HasData() const;
	};
	TArray<FEditorTypes> EditorTypes;

#if WITH_STATETREE_TRACE_DEBUGGER
	TPimplPtr<UE::StateTreeDebugger::FRewindDebuggerPlaybackExtension> RewindDebuggerPlaybackExtension;
	TPimplPtr<UE::StateTreeDebugger::FRewindDebuggerTrackCreator> RewindDebuggerTrackCreator;
#endif  // WITH_STATETREE_TRACE_DEBUGGER

	FDelegateHandle OnPostEngineInitHandle;
	UE_DEPRECATED(5.7, "OnUserDefinedStructReinstancedHandle is not used.")
	FDelegateHandle OnUserDefinedStructReinstancedHandle;
	FOnRegisterLayoutExtensions RegisterLayoutExtensions;
	FPostInternalCompile PostInternalCompile;
};

#undef UE_API
