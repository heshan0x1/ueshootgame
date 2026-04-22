// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextWorkspaceEditorMode.h"

#include "AnimNextEditorModule.h"
#include "AnimNextRigVMAsset.h"
#include "AnimNextRigVMAssetEditorData.h"
#include "AnimNextScopedCompileJob.h"
#include "ContextObjectStore.h"
#include "EditorModeManager.h"
#include "IAssetCompilationHandler.h"
#include "InteractiveToolManager.h"
#include "IWorkspaceEditor.h"
#include "IWorkspaceEditorModule.h"
#include "RigVMCommands.h"
#include "RigVMCore/RigVMGraphFunctionHost.h"
#include "RigVMModel/Nodes/RigVMFunctionReferenceNode.h"
#include "UncookedOnlyUtils.h"
#include "Logging/MessageLog.h"
#include "Modules/ModuleManager.h"
#include "Toolkits/AssetEditorToolkitMenuContext.h"
#include "Toolkits/BaseToolkit.h"
#include "Tools/EdModeInteractiveToolsContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextWorkspaceEditorMode)

#define LOCTEXT_NAMESPACE "AnimNextWorkspaceEditorMode"

const FEditorModeID UAnimNextWorkspaceEditorMode::EM_AnimNextWorkspace("AnimNextWorkspace");

UAnimNextWorkspaceEditorMode::UAnimNextWorkspaceEditorMode()
{
	Info = FEditorModeInfo(UAnimNextWorkspaceEditorMode::EM_AnimNextWorkspace,
		LOCTEXT("AnimNextWorkspaceEditorModeName", "AnimNextWorkspaceEditorMode"),
		FSlateIcon(),
		false);
}

void UAnimNextWorkspaceEditorMode::Enter()
{
	Super::Enter();
	
	TSharedPtr<UE::Workspace::IWorkspaceEditor> WorkspaceEditor = GetWorkspaceEditor();
	if(!WorkspaceEditor.IsValid())
	{
		return;
	}
	
	WorkspaceEditor->OnFocusedDocumentChanged().AddUObject(this, &UAnimNextWorkspaceEditorMode::HandleFocusedDocumentChanged);

	TArray<UObject*> Assets;
	WorkspaceEditor->GetOpenedAssets<UAnimNextRigVMAsset>(Assets);
	for(UObject* Asset : Assets)
	{
		UAnimNextRigVMAsset* AnimNextRigVMAsset = static_cast<UAnimNextRigVMAsset*>(Asset);
		UAnimNextRigVMAssetEditorData* EditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(AnimNextRigVMAsset);

		EditorData->RigVMCompiledEvent.RemoveAll(this);
		EditorData->RigVMCompiledEvent.AddUObject(this, &UAnimNextWorkspaceEditorMode::HandleRigVMCompiledEvent);
		EditorData->RigVMGraphModifiedEvent.RemoveAll(this);
		EditorData->RigVMGraphModifiedEvent.AddUObject(this, &UAnimNextWorkspaceEditorMode::HandleRigVMModifiedEvent);

		WeakAssets.Add(Asset);
	}

	UpdateCompileStatus();
}

void UAnimNextWorkspaceEditorMode::Exit()
{
	TSharedPtr<UE::Workspace::IWorkspaceEditor> WorkspaceEditor = GetWorkspaceEditor();
	if(WorkspaceEditor.IsValid())
	{
		const TSharedRef<FUICommandList>& CommandList = WorkspaceEditor->GetToolkitCommands();
		const UE::UAF::FRigVMCommands& RigVMCommands = UE::UAF::FRigVMCommands::Get();
		CommandList->UnmapAction(RigVMCommands.Compile);
		CommandList->UnmapAction(RigVMCommands.AutoCompile);
		CommandList->UnmapAction(RigVMCommands.CompileWholeWorkspace);
		CommandList->UnmapAction(RigVMCommands.CompileDirtyFiles);
	}

	Super::Exit();

	for (TPair<FObjectKey, TSharedRef<UE::UAF::Editor::IAssetCompilationHandler>>& AssetCompiler : AssetCompilers)
	{
		TSharedRef<UE::UAF::Editor::IAssetCompilationHandler> AssetCompilerRef = AssetCompiler.Value;
		AssetCompilerRef->OnCompileStatusChanged().Unbind();
	}

	AssetCompilers.Reset();

	for(TObjectKey<UObject> Asset : WeakAssets)
	{
		UAnimNextRigVMAsset* AnimNextRigVMAsset = Cast<UAnimNextRigVMAsset>(Asset.ResolveObjectPtr());
		if(AnimNextRigVMAsset == nullptr)
		{
			continue;;
		}
		
		UAnimNextRigVMAssetEditorData* EditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(AnimNextRigVMAsset);

		EditorData->RigVMCompiledEvent.RemoveAll(this);
		EditorData->RigVMGraphModifiedEvent.RemoveAll(this);
	}

	WeakAssets.Reset();
}

void UAnimNextWorkspaceEditorMode::BindCommands()
{
	Super::BindCommands();

	TSharedPtr<UE::Workspace::IWorkspaceEditor> WorkspaceEditor = GetWorkspaceEditor();
	if(!WorkspaceEditor.IsValid())
	{
		return;
	}

	TSharedRef<FUICommandList> ToolkitCommands = WorkspaceEditor->GetToolkitCommands();

	const UE::UAF::FRigVMCommands& RigVMCommands = UE::UAF::FRigVMCommands::Get();
	ToolkitCommands->MapAction(RigVMCommands.Compile,
		FExecuteAction::CreateUObject(this, &UAnimNextWorkspaceEditorMode::HandleCompile),
		FCanExecuteAction::CreateUObject(this, &UAnimNextWorkspaceEditorMode::CanCompile));

	ToolkitCommands->MapAction(RigVMCommands.AutoCompile,
		FExecuteAction::CreateUObject(this, &UAnimNextWorkspaceEditorMode::HandleAutoCompile),
		FCanExecuteAction::CreateUObject(this, &UAnimNextWorkspaceEditorMode::CanCompile),
		FIsActionChecked::CreateUObject(this, &UAnimNextWorkspaceEditorMode::IsAutoCompileChecked));

	ToolkitCommands->MapAction(RigVMCommands.CompileWholeWorkspace,
		FExecuteAction::CreateUObject(this, &UAnimNextWorkspaceEditorMode::HandleCompileWholeWorkspace),
		FCanExecuteAction::CreateUObject(this, &UAnimNextWorkspaceEditorMode::CanCompile),
		FIsActionChecked::CreateUObject(this, &UAnimNextWorkspaceEditorMode::IsCompileWholeWorkspaceChecked));

	ToolkitCommands->MapAction(RigVMCommands.CompileDirtyFiles,
		FExecuteAction::CreateUObject(this, &UAnimNextWorkspaceEditorMode::HandleCompileDirtyFiles),
		FCanExecuteAction::CreateUObject(this, &UAnimNextWorkspaceEditorMode::CanCompile),
		FIsActionChecked::CreateUObject(this, &UAnimNextWorkspaceEditorMode::IsCompileDirtyFilesChecked));
}

void UAnimNextWorkspaceEditorMode::HandleCompile()
{
	TSharedPtr<UE::Workspace::IWorkspaceEditor> WorkspaceEditor = GetWorkspaceEditor();
	if(!WorkspaceEditor.IsValid())
	{
		return;
	}

	TArray<UObject*> Assets;

	UObject* Asset = nullptr;
	FText JobName = LOCTEXT("DefaultJobName", "Job");
	if(IsCompileWholeWorkspaceChecked())
	{
		WorkspaceEditor->GetOpenedAssets<UObject>(Assets);
		if(Assets.Num() == 1)
		{
			Asset = Assets[0];
			JobName = FText::FromName(Asset->GetFName());
		}
		else
		{
			Asset = WorkspaceEditor->GetWorkspaceAsset();
			JobName = FText::FromName(Asset->GetFName());
		}
	}
	else if(UObject* FocusedDocument = WorkspaceEditor->GetFocusedWorkspaceDocument().GetObject())
	{
		// Find the outer asset for this document
		Asset = FocusedDocument;
		
		while(Asset && (!Asset->IsAsset() || Asset->IsPackageExternal()))
		{
			Asset = Asset->GetOuter();
		}

		if(Asset)
		{
			JobName = FText::FromName(Asset->GetFName());
			Assets.Add(Asset);
		}
	}

	if(IsCompileDirtyFilesChecked())
	{
		for(int32 AssetIndex = 0; AssetIndex < Assets.Num(); ++AssetIndex)
		{
			UObject* AssetToCheck = Assets[AssetIndex];
			if(TSharedPtr<UE::UAF::Editor::IAssetCompilationHandler> FoundCompiler = GetAssetCompiler(AssetToCheck))
			{
				UE::UAF::Editor::ECompileStatus AssetCompileStatus = FoundCompiler->GetCompileStatus(WorkspaceEditor.ToSharedRef(), AssetToCheck);
				if( AssetCompileStatus != UE::UAF::Editor::ECompileStatus::Dirty &&
					AssetCompileStatus != UE::UAF::Editor::ECompileStatus::Error)
				{
					Assets.RemoveAtSwap(AssetIndex);
					AssetIndex--;
				}
			}
		}
	}

	if(Assets.Num() == 0)
	{
		return;
	}

	// Start a batch compile scope
	UE::UAF::UncookedOnly::FScopedCompileJob CompileJob(JobName, Assets);

	UE::UAF::Editor::FAnimNextEditorModule& AnimNextEditorModule = FModuleManager::LoadModuleChecked<UE::UAF::Editor::FAnimNextEditorModule>("UAFEditor");
	for(UObject* AssetToCompile : Assets)
	{
		if(TSharedPtr<UE::UAF::Editor::IAssetCompilationHandler> FoundCompiler = GetAssetCompiler(AssetToCompile))
		{
			FoundCompiler->Compile(WorkspaceEditor.ToSharedRef(), AssetToCompile);
		}
	}
}

bool UAnimNextWorkspaceEditorMode::CanCompile() const
{
	TSharedPtr<UE::Workspace::IWorkspaceEditor> WorkspaceEditor = GetWorkspaceEditor();
	return WorkspaceEditor.IsValid();
}

void UAnimNextWorkspaceEditorMode::HandleAutoCompile()
{
	State.bAutoCompile = !State.bAutoCompile;

	TSharedPtr<UE::Workspace::IWorkspaceEditor> WorkspaceEditor = GetWorkspaceEditor();
	if(WorkspaceEditor.IsValid())
	{
		PropagateAutoCompile(WorkspaceEditor.ToSharedRef(), State.bAutoCompile);
	}
}

void UAnimNextWorkspaceEditorMode::PropagateAutoCompile(TSharedRef<UE::Workspace::IWorkspaceEditor> InWorkspaceEditor, bool bInAutoCompile)
{
	TArray<UObject*> Assets;
	InWorkspaceEditor->GetOpenedAssets(Assets);
	UE::UAF::Editor::FAnimNextEditorModule& AnimNextEditorModule = FModuleManager::LoadModuleChecked<UE::UAF::Editor::FAnimNextEditorModule>("UAFEditor");
	for(UObject* Asset : Assets)
	{
		if(TSharedPtr<UE::UAF::Editor::IAssetCompilationHandler> FoundCompiler = GetAssetCompiler(Asset))
		{
			FoundCompiler->SetAutoCompile(InWorkspaceEditor, Asset, bInAutoCompile);
		}
	}
}

bool UAnimNextWorkspaceEditorMode::IsAutoCompileChecked() const
{
	return State.bAutoCompile;
}

void UAnimNextWorkspaceEditorMode::HandleCompileWholeWorkspace()
{
	State.bCompileWholeWorkspace = !State.bCompileWholeWorkspace;
}

bool UAnimNextWorkspaceEditorMode::IsCompileWholeWorkspaceChecked() const
{
	return State.bCompileWholeWorkspace;
}

void UAnimNextWorkspaceEditorMode::HandleCompileDirtyFiles()
{
	State.bCompileDirtyFiles = !State.bCompileDirtyFiles;
}

bool UAnimNextWorkspaceEditorMode::IsCompileDirtyFilesChecked() const
{
	return State.bCompileDirtyFiles;
}

void UAnimNextWorkspaceEditorMode::HandleFocusedDocumentChanged(const UE::Workspace::FWorkspaceDocument& InDocument)
{
	UObject* InObject = InDocument.GetObject();
	
	if(InObject == nullptr)
	{
		return;
	}

	UAnimNextRigVMAsset* AnimNextRigVMAsset = Cast<UAnimNextRigVMAsset>(InObject);
	if(AnimNextRigVMAsset == nullptr)
	{
		AnimNextRigVMAsset = InObject->GetTypedOuter<UAnimNextRigVMAsset>();
	}
	
	if(AnimNextRigVMAsset == nullptr)
	{
		return;
	}

	// Ensure auto-compilation is propagated for newly opened asset 
	UAnimNextRigVMAssetEditorData* EditorData = UE::UAF::UncookedOnly::FUtils::GetEditorData<UAnimNextRigVMAssetEditorData>(AnimNextRigVMAsset);
	EditorData->SetAutoVMRecompile(State.bAutoCompile);

	// Subscribe to asset compilation/modification
	EditorData->RigVMCompiledEvent.RemoveAll(this);
	EditorData->RigVMCompiledEvent.AddUObject(this, &UAnimNextWorkspaceEditorMode::HandleRigVMCompiledEvent);
	EditorData->RigVMGraphModifiedEvent.RemoveAll(this);
	EditorData->RigVMGraphModifiedEvent.AddUObject(this, &UAnimNextWorkspaceEditorMode::HandleRigVMModifiedEvent);

	WeakAssets.Add(AnimNextRigVMAsset);
}

void UAnimNextWorkspaceEditorMode::UpdateCompileStatus()
{
	bUpdateCompileStatus = false;

	TSharedPtr<UE::Workspace::IWorkspaceEditor> WorkspaceEditor = GetWorkspaceEditor();
	if(!WorkspaceEditor.IsValid())
	{
		return;
	}

	int32 NumAssetsWithWarnings = 0;
	int32 NumAssetsWithErrors = 0;
	int32 NumAssetsDirty = 0;

	TArray<UObject*> Assets;
	WorkspaceEditor->GetOpenedAssets<UAnimNextRigVMAsset>(Assets);

	for(UObject* Asset : Assets)
	{
		if(TSharedPtr<UE::UAF::Editor::IAssetCompilationHandler> FoundCompiler = GetAssetCompiler(Asset))
		{
			UE::UAF::Editor::ECompileStatus AssetCompileStatus = FoundCompiler->GetCompileStatus(WorkspaceEditor.ToSharedRef(), Asset);
			if(AssetCompileStatus == UE::UAF::Editor::ECompileStatus::Dirty)
			{
				NumAssetsDirty++;
			}

			if(AssetCompileStatus == UE::UAF::Editor::ECompileStatus::Error)
			{
				NumAssetsWithErrors++;
			}

			if(AssetCompileStatus == UE::UAF::Editor::ECompileStatus::Warning)
			{
				NumAssetsWithWarnings++;
			}
		}
	}

	if(NumAssetsWithErrors > 0)
	{
		CompileStatus = UE::UAF::Editor::ECompileStatus::Error;
	}
	else if(NumAssetsWithWarnings > 0)
	{
		CompileStatus = UE::UAF::Editor::ECompileStatus::Warning;
	}
	else if(NumAssetsDirty > 0)
	{
		CompileStatus = UE::UAF::Editor::ECompileStatus::Dirty;
	}
	else
	{
		CompileStatus = UE::UAF::Editor::ECompileStatus::UpToDate;
	}
}

void UAnimNextWorkspaceEditorMode::HandleRigVMCompiledEvent(UObject* InAsset, URigVM* InVM, FRigVMExtendedExecuteContext& InExtendedExecuteContext)
{
	UpdateCompileStatus();
}

void UAnimNextWorkspaceEditorMode::HandleRigVMModifiedEvent(ERigVMGraphNotifType InType, URigVMGraph* InGraph, UObject* InSubject)
{
	if(InGraph == nullptr)
	{
		return;
	}

	UAnimNextRigVMAssetEditorData* EditorData = InGraph->GetTypedOuter<UAnimNextRigVMAssetEditorData>();

	// Our RigVM Graph may be a function, in this case the graph has a temp outer not the editor data. 
	// So search for the host object of the function instead.
	if (!EditorData)
	{
		if (URigVMFunctionReferenceNode* FunctionReferenceNode = Cast<URigVMFunctionReferenceNode>(InSubject))
		{
			constexpr bool bLoadIfNecessary = true;
			if (UAnimNextRigVMAssetEditorData* FunctionHost = Cast<UAnimNextRigVMAssetEditorData>(FunctionReferenceNode->GetReferencedFunctionHeader().GetFunctionHost(bLoadIfNecessary)))
			{
				EditorData = FunctionHost;
			}
		}
	}
	
	if (!ensureMsgf(EditorData, TEXT("Failed to find UAF asset for RigVM modification, asset may not update correctly. It also may fail to propagate dirty status to other assets.")))
	{
		return;
	}

	if(EditorData->IsDirtyForRecompilation())
	{
		CompileStatus = UE::UAF::Editor::ECompileStatus::Dirty;
	}
}

TSharedPtr<UE::Workspace::IWorkspaceEditor> UAnimNextWorkspaceEditorMode::GetWorkspaceEditor() const
{
	if (const UContextObjectStore* ContextObjectStore = GetModeManager()->GetInteractiveToolsContext()->ContextObjectStore)
	{
		if (UAssetEditorToolkitMenuContext* Context = ContextObjectStore->FindContext<UAssetEditorToolkitMenuContext>())
		{
			if (TSharedPtr<FAssetEditorToolkit> ToolkitShared = Context->Toolkit.Pin())
			{
				return StaticCastSharedPtr<UE::Workspace::IWorkspaceEditor>(ToolkitShared);
			}
		}
	}

	return nullptr;
}

TSharedPtr<UE::UAF::Editor::IAssetCompilationHandler> UAnimNextWorkspaceEditorMode::GetAssetCompiler(UObject* InAsset)
{
	if(TSharedRef<UE::UAF::Editor::IAssetCompilationHandler>* FoundCompiler = AssetCompilers.Find(InAsset))
	{
		return *FoundCompiler;
	}

	UE::UAF::Editor::FAnimNextEditorModule& AnimNextEditorModule = FModuleManager::LoadModuleChecked<UE::UAF::Editor::FAnimNextEditorModule>("UAFEditor");
	if(const UE::UAF::Editor::FAssetCompilationHandlerFactoryDelegate* FoundFactory = AnimNextEditorModule.FindAssetCompilationHandlerFactory(InAsset->GetClass()))
	{
		TSharedRef<UE::UAF::Editor::IAssetCompilationHandler> NewCompiler = AssetCompilers.Add(InAsset, FoundFactory->Execute(InAsset));
		NewCompiler->OnCompileStatusChanged().BindUObject(this, &UAnimNextWorkspaceEditorMode::UpdateCompileStatus);
		return NewCompiler;
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE
