// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVEditor.h"

#include "EngineAnalytics.h"
#include "IMessageLogListing.h"
#include "MessageLogModule.h"
#include "PackagesDialog.h"
#include "PCGDefaultExecutionSource.h"
#include "PCGEditorCommands.h"
#include "ProceduralVegetation.h"
#include "ProceduralVegetationEditorModule.h"
#include "PVEditorCommands.h"
#include "PVEditorSchema.h"
#include "PVEditorSettings.h"
#include "SPVEditorViewport.h"
#include "ToolMenus.h"
#include "ToolMenuSection.h"

#include "Dataflow/DataflowCollectionSpreadSheetWidget.h"

#include "DataTypes/PVData.h"
#include "DataTypes/PVFoliageMeshData.h"

#include "Facades/PVAttributesNames.h"

#include "GeometryCollection/GeometryCollection.h"

#include "Helpers/PVAnalyticsHelper.h"
#include "Helpers/PVExportHelper.h"
#include "Helpers/PVUtilities.h"

#include "Misc/MessageDialog.h"
#include "Misc/UObjectToken.h"

#include "Nodes/PCGEditorGraphNodeBase.h"
#include "Nodes/PVOutputSettings.h"

#include "PCGEditor/Private/PCGEditorGraph.h"

#include "Subsystems/PCGEngineSubsystem.h"

#include "Widgets/SPVExportSelectionDialog.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "PVEditor"

const FName FPVEditor::CollectionSpreadSheetTabId (TEXT("PVEditor_CollectionSpreadSheet"));

FPVEditor::FPVEditor()
{
}

void FPVEditor::Initialize(const EToolkitMode::Type InMode, const TSharedPtr<class IToolkitHost>& InToolkitHost, UProceduralVegetation* InProceduralVegetation)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	bForceRefreshAttributeEvenIfClosed = true;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	
	check(InProceduralVegetation);

	UE_LOG(LogProceduralVegetationEditor, Log, TEXT("PVEditor initialized for %s"), *InProceduralVegetation->GetFName().ToString());
	ProceduralVegetationBeingEdited = InProceduralVegetation;
	
	UProceduralVegetationGraph* ProceduralVegetationGraph = InProceduralVegetation->GetGraph();

	check(ProceduralVegetationGraph);

	// Initialize widgets before calling base

	if (PV::Utilities::DebugModeEnabled())
	{
		CollectionSpreadSheetWidget = CreateCollectionSpreadSheetWidget();
	}
	
	FPCGEditor::Initialize(InMode, InToolkitHost, ProceduralVegetationGraph, InProceduralVegetation);

	check(GEngine);
	
	UPCGEngineSubsystem* Subsystem = GEngine->GetEngineSubsystem<UPCGEngineSubsystem>();

	check(Subsystem);
	
	const FPCGDefaultExecutionSourceParams Params
	{
		.GraphInterface = ProceduralVegetationGraph
	};
	
	ExecutionSource = Subsystem->CreateExecutionSource(Params);

	// Set the stack
	FPCGStack Stack;
	Stack.PushFrame(ExecutionSource.Get());
	Stack.PushFrame(ProceduralVegetationGraph);
	SetStackBeingInspected(Stack);

	// Ask for generation
	ExecutionSource->Generate();

	// Select the first available output
	for (auto EdNode : GetPCGEditorGraph()->Nodes)
	{
		if (const UPCGEditorGraphNodeBase* PCGEdNode = Cast<UPCGEditorGraphNodeBase>(EdNode))
		{
			if (const UPCGNode* PCGNode = PCGEdNode->GetPCGNode())
			{
				if(PCGNode->GetSettings()->IsA<UPVOutputSettings>())
				{
					GetPCGEditorGraph()->SelectNodeSet({PCGEdNode}, true);
					break;
				}
			}
		}
	}

	SessionStartTime = FDateTime::Now();

	if (FEngineAnalytics::IsAvailable())
	{
		PV::Analytics::SendSessionStartedEvent();
	}
}

TSubclassOf<UPCGEditorGraphSchema> FPVEditor::GetSchemaClass() const
{
	return UPVEditorSchema::StaticClass();
}

void FPVEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	FPCGEditor::AddReferencedObjects(Collector);

	Collector.AddReferencedObject(ExecutionSource);
}

FText FPVEditor::GetToolkitName() const
{
	return GetLabelForObject(ProceduralVegetationBeingEdited);
}

FName FPVEditor::GetToolkitFName() const
{
	return TEXT("ProceduralVegetationEditor");
}

FText FPVEditor::GetBaseToolkitName() const
{
	return LOCTEXT("PVEditorToolkitName", "Procedural Vegetation Editor");
}

FText FPVEditor::GetToolkitToolTipText() const
{
	return GetToolTipTextForObject(ProceduralVegetationBeingEdited);
}

FLinearColor FPVEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor::Blue;
}

FString FPVEditor::GetWorldCentricTabPrefix() const
{
	return TEXT("Procedural Vegetation Editor ");
}

void FPVEditor::OnClose()
{
	UE_LOG(LogProceduralVegetationEditor, Log, TEXT("PVEditor Closed"));
	
	double TotalSeconds = (FDateTime::Now() - SessionStartTime).GetTotalSeconds();
	PV::Analytics::SendSessionEndedEvent(TotalSeconds);
	
	FPCGEditor::OnClose();

	if (ExecutionSource)
	{
		ExecutionSource->SetGraphInterface(nullptr);
	}
}

void FPVEditor::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FPCGEditor::RegisterTabSpawners(InTabManager);

	if (CollectionSpreadSheetWidget.IsValid())
	{
		InTabManager->RegisterTabSpawner(CollectionSpreadSheetTabId, FOnSpawnTab::CreateSP(this, &FPVEditor::SpawnTab_CollectionSpreadSheet))
			.SetDisplayName(LOCTEXT("CollectionSpreadsheetTab", "Collection Spreadsheet"))
			.SetGroup(WorkspaceMenuCategory.ToSharedRef());
	}
}

void FPVEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FPCGEditor::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(CollectionSpreadSheetTabId);
}

IPCGBaseSubsystem* FPVEditor::GetSubsystem() const
{
	return UPCGEngineSubsystem::Get();
}

TAttribute<FGraphAppearanceInfo> FPVEditor::GetAppearanceInfo() const
{
	FGraphAppearanceInfo AppearanceInfo;
	AppearanceInfo.CornerText = LOCTEXT("PVEditorCornerText", "Procedural Vegetation");

	return AppearanceInfo;
}

TSharedRef<FTabManager::FLayout> FPVEditor::GetDefaultLayout() const
{
	return FTabManager::NewLayout("Standalone_PVGraphEditor_DefaultLayout_v1.0.5")
	->AddArea // Main PCG Graph Editor Area
	(
		FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
		->Split // Top Section - Graph, Data Viewport, HLSL Source Editor, and Details View
		(
			FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)->SetSizeCoefficient(0.9f)
			->SetSizeCoefficient(0.40f)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.1f)
				->Split // Viewport and AVL
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.95f)
					->AddTab(GetPanelID(EPCGEditorPanel::Viewport1), ETabState::OpenedTab)
					->SetForegroundTab(GetPanelID(EPCGEditorPanel::Viewport1))
					->SetHideTabWell(true)
				)
			)
			->Split // Graph and details panel
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.25f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.6f)
					->AddTab(GetPanelID(EPCGEditorPanel::GraphEditor), ETabState::OpenedTab)
					->SetHideTabWell(true)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.4f)
					->AddTab(GetPanelID(EPCGEditorPanel::PropertyDetails1), ETabState::OpenedTab)
				)
			)
		)
	);
}

bool FPVEditor::IsPanelAvailable(const FName PanelID) const
{
	static TSet<FName> SupportedIDs = {
		GetPanelID(EPCGEditorPanel::Find),
		GetPanelID(EPCGEditorPanel::GraphEditor),
		GetPanelID(EPCGEditorPanel::Log),
		GetPanelID(EPCGEditorPanel::PropertyDetails1),
		GetPanelID(EPCGEditorPanel::UserParams),
		GetPanelID(EPCGEditorPanel::Viewport1)
	};
	return SupportedIDs.Contains(PanelID);
}

void FPVEditor::RegisterToolbarInternal(FToolMenuSection& PCGSection) const
{
	RegisterToolbarButton(PCGSection, EPCGToolbarButtons::Find);
	RegisterToolbarButton(PCGSection, EPCGToolbarButtons::ForceRegen);
	
	const FPVEditorCommands& Commands = FPVEditorCommands::Get();
	PCGSection.AddEntry(FToolMenuEntry::InitToolBarButton(
			Commands.Export,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Toolbar.Export")));
}

void FPVEditor::BindCommands()
{
	FPCGEditor::BindCommands();

	const FPVEditorCommands& EditorCommands = FPVEditorCommands::Get();

	ToolkitCommands->MapAction(
		EditorCommands.Export,
		FExecuteAction::CreateSP(this, &FPVEditor::OnExport_Clicked));

	ToolkitCommands->MapAction(
		EditorCommands.LockNodeInspection,
		FExecuteAction::CreateSP(this, &FPVEditor::OnLockNodeSelection));
}

UPVBaseSettings* GetNodeSettings(UPCGEditorGraphNodeBase* InNode)
{
	if (InNode && InNode->GetPCGNode())
	{
		return Cast<UPVBaseSettings>(InNode->GetPCGNode()->GetSettings());
	}

	return nullptr;
}

void FPVEditor::ChangeNodeInspection(UPCGEditorGraphNodeBase* InNode)
{
	if (UPVBaseSettings* Settings = GetNodeSettings(InNode))
	{
		if (ViewportWidgets.Num() == 0)
		{
			return;
		}

		ViewportWidgets[0]->OnNodeInspectionChanged(Settings);
		
		NodeBeingInspected = InNode;
		CollectionBeingInspected = SelectedCollection;
		
		UpdateCollectionSpreadSheet(InNode->GetPCGNode());
		
		SetNodeInspected(InNode, true);	
	}
}

void FPVEditor::OnSelectedNodesChanged(const TSet<UObject*>& InNewSelection)
{
	FPCGEditor::OnSelectedNodesChanged(InNewSelection);

	UPCGEditorGraphNodeBase* EdNode = nullptr;

	SelectedNodes.Empty();

	for(UObject* Selection: InNewSelection)
	{
		EdNode = Cast<UPCGEditorGraphNodeBase>(Selection);

		if (EdNode && EdNode->GetPCGNode())
		{
			SelectedNodes.Add(EdNode);
		}
	}

	if (UPCGNode* SelectedNode = GetSelectedNode() ? GetSelectedNode()->GetPCGNode() : nullptr)
	{
		SelectedCollection = MakeShared<FManagedArrayCollection>();
		FName OutputName;
		GetCollectionFromNode(SelectedNode, OutputName, *SelectedCollection);
		
		if (UPVBaseSettings* Settings = Cast<UPVBaseSettings>(SelectedNode->GetSettings()))
		{
			const UPVBaseSettings* InspectionSettings = GetNodeSettings(NodeBeingInspected);
			if (!InspectionSettings || !InspectionSettings->bLockInspection)
			{
				ChangeNodeInspection(EdNode);

				TArray<FText> OverlayStats;
				GatherStats(OverlayStats);
				ViewportWidgets[0]->PopulateStatsOverlayText(OverlayStats);
			}
		}	
	}
}

void FPVEditor::OnExport_Clicked()
{
	ExportInternal();
}

FReply FPVEditor::ExportInternal()
{
	const UProceduralVegetationGraph* ProceduralVegetationGraph = Cast<UProceduralVegetationGraph>(GetPCGGraph());
	
	TSharedPtr<SPVExportSelectionDialog> ExportSelectionWidget =
		SNew(SPVExportSelectionDialog)
		.Title(LOCTEXT("Export Selection", "Choose outputs to export"))
		.Graph(ProceduralVegetationGraph)
		.SelectedNodes(SelectedNodes);

	if (ExportSelectionWidget->ShowModal() != EAppReturnType::Ok)
	{
		return FReply::Handled();
	}

	UE_LOG(LogProceduralVegetationEditor, Log, TEXT("Export Started"));
	struct FScopedMessages
	{
		TArray<TSharedRef<FTokenizedMessage>> Messages;

		TSharedRef<FTokenizedMessage> Error(const FText& ErrorMsg)
		{
			TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Error, ErrorMsg);
			Messages.Add(Message);
			return Message;
		}

		TSharedRef<FTokenizedMessage> Warning(const FText& ErrorMsg)
		{
			TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Warning, ErrorMsg);
			Messages.Add(Message);
			return Message;
		}

		TSharedRef<FTokenizedMessage> Info(const FText& ErrorMsg)
		{
			TSharedRef<FTokenizedMessage> Message = FTokenizedMessage::Create(EMessageSeverity::Info, ErrorMsg);
			Messages.Add(Message);
			return Message;
		}
		
		bool ContainsErrors() const
		{
			for (TSharedRef<FTokenizedMessage> Message : Messages)
			{
				if (Message->GetSeverity() == EMessageSeverity::Error)
				{
					return true;
				}
			}

			return false;
		}

		~FScopedMessages()
		{
			if (Messages.Num() > 0)
			{
				FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
				TSharedRef<IMessageLogListing> PVEditorMessageLogListing = MessageLogModule.GetLogListing(PVEditor::MessageLogName);

				PVEditorMessageLogListing->ClearMessages();
				PVEditorMessageLogListing->AddMessages(Messages);
				PVEditorMessageLogListing->NotifyIfAnyMessages(
					ContainsErrors() ? LOCTEXT("PVEMeshExportFailed", "Mesh Export Failed") : LOCTEXT("PVEMeshExportComplete", "Mesh Export Completed"),
					EMessageSeverity::Info,
					true
				);
			}
		}
	};

	FScopedMessages Messages;

	const static auto ValidateExportSettings = [](const TArray<UPCGNode*>& NodesToExport, FScopedMessages& OutMessages)->bool
	{
		// Check that the paths are valid
		bool bHasInvalidPath = false;
		for (const UPCGNode* Node : NodesToExport)
		{
			const UPVOutputSettings* OutputSettings = CastChecked<const UPVOutputSettings>(Node->GetSettings());

			FString PathError;
			if (!OutputSettings->ExportSettings.Validate(PathError))
			{
				TSharedRef<FTokenizedMessage> Error = OutMessages.Error(
					FText::Format(
						LOCTEXT("ExportFailed_InvalidPath", "Invalid export path for node \"{0}\": \"{1}\""),
						Node->GetDefaultTitle(),
						FText::FromString(PathError)
					)
				);
				Error->AddToken(FUObjectToken::Create(OutputSettings));
				bHasInvalidPath = true;
			}
		}

		return !bHasInvalidPath;
	};

	const static auto ShouldOverwriteAssets = [](const TArray<UPCGNode*>& NodesToExport)->bool 
	{
		TArray<FString> ExportPaths;
		ExportPaths.Reserve(NodesToExport.Num() * 2);

		for (const UPCGNode* Node : NodesToExport)
		{
			const UPVOutputSettings* OutputSettings = CastChecked<const UPVOutputSettings>(Node->GetSettings());
			const FPVExportParams& ExportParams = OutputSettings->ExportSettings;
			if (ExportParams.ReplacePolicy != EPVAssetReplacePolicy::Replace)
			{
				continue;
			}
			ExportPaths.Add(ExportParams.GetOutputMeshPackagePath());
			if (ExportParams.ExportMeshType == EPVExportMeshType::SkeletalMesh)
			{
				ExportPaths.Add(ExportParams.GetOutputSkeletonPackagePath());

				if (ExportParams.IsCollisionEnable())
				{
					ExportPaths.Add(ExportParams.GetOutputPhysicsAssetPackagePath());
				}
			}
		}

		TArray<UPackage*> ExistingPackages;
		ExistingPackages.Reserve(ExportPaths.Num());
		for (const FString& AssetPath : ExportPaths)
		{
			UPackage* ExistingPackage = LoadPackage(nullptr, *AssetPath, LOAD_None);
			if (ExistingPackage)
			{
				ExistingPackages.Add(ExistingPackage);
			}
		}

		if (ExistingPackages.Num() == 0)
		{
			return true;
		}

		FPackagesDialogModule& PackagesDialogModule = FModuleManager::LoadModuleChecked<FPackagesDialogModule>(TEXT("PackagesDialog"));
		PackagesDialogModule.CreatePackagesDialog(
			LOCTEXT("PackagesDialog_OverwriteFiles_Title", "WARNING: Assets will be overwritten"), 
			LOCTEXT("PackagesDialog_OverwriteFiles_Message", "The following assets will be overwritten. Would you like to continue?."),
			/*InReadOnly=*/true
		);
		PackagesDialogModule.AddButton(
			DRT_Save, 
			LOCTEXT("PackagesDialog_OverwriteFiles_ContinueButton", "Continue"), 
			LOCTEXT("PackagesDialog_OverwriteFiles_ButtonTip", "Continues the export, overwrites any existing assets")
		);
		PackagesDialogModule.AddButton(
			DRT_Cancel, 
			LOCTEXT("PackagesDialog_OverwriteFiles_CancelButton", "Cancel"), 
			LOCTEXT("CancelDeleteButtonTip", "Cancel export")
		);

		for (UPackage* Package : ExistingPackages)
		{
			PackagesDialogModule.AddPackageItem(Package, ECheckBoxState::Checked);
		}

		const EDialogReturnType UserResponse = PackagesDialogModule.ShowPackagesDialog();

		return UserResponse == DRT_Save;
	};

	const auto GetOutputNodeCollections = [this](TArray<FManagedArrayCollection>& OutCollections, const TArray<UPCGNode*>& NodesToExport, FScopedMessages& OutMessages)->bool
	{
		OutCollections.Reset(NodesToExport.Num());
		for (UPCGNode* Node : NodesToExport)
		{
			FManagedArrayCollection& OutCollection = OutCollections.AddDefaulted_GetRef();
			FName PinName = NAME_None;
			if (!GetCollectionFromNode(Node, PinName, OutCollection))
			{
				TSharedRef<FTokenizedMessage> Error = OutMessages.Error(
					FText::Format(
						LOCTEXT("ExportFailed_NodeErrors", "Export failed for output node \"{0}\", Invalid output please fix the errors on the node."),
						Node->GetDefaultTitle()
					)
				);
				Error->AddToken(FUObjectToken::Create(Node));
				return false;
			}
		}
		
		return true;
	};

	const auto FoliageDistributorValidation = [this](const TArray<UPCGNode*>& NodesToExport)->bool
	{
		TArray<const UPCGNode*> NodesWithoutFoliage;
		for (UPCGNode* Node : NodesToExport)
		{
			FName PinName = NAME_None;
			const UPVData* PVData = GetPVDataFromNode(Node, PinName);
			if (PVData && !Cast<UPVFoliageMeshData>(PVData))
			{
				NodesWithoutFoliage.Add(Node);
			}
		}

		if (NodesWithoutFoliage.Num() > 0)
		{
			FString NodeNames = "";
			for (int32 i = 0; i < NodesWithoutFoliage.Num(); i++)
			{
				const UPCGNode* Node = NodesWithoutFoliage[i];
				NodeNames += FString::Format(TEXT("\" {0} \""), {Node->GetAuthoredTitleName().ToString()});
				
				if (NodesWithoutFoliage.Num() > 1 && i == NodesWithoutFoliage.Num() - 2)
				{
					NodeNames += " and ";
				}

				if (i != NodesWithoutFoliage.Num() - 2)
				{
					NodeNames += ", ";
				}
			}
			
			FString Message = FString::Format(TEXT("{0} does not use input from foliage distributor node, Do you want to export with default foliage distribution."),{NodeNames});
			const EAppReturnType::Type ret = FMessageDialog::Open(EAppMsgType::YesNoCancel, FText::Format(LOCTEXT("Export_Foliage_Warning", "{0}"), FText::FromString(Message)));
			if (ret != EAppReturnType::Cancel)
			{
				const bool bShouldExportFoliage = ret == EAppReturnType::Yes;
				for (const UPCGNode* Node : NodesWithoutFoliage)
				{
					UPVOutputSettings* OutputSettings = Cast<UPVOutputSettings>(Node->GetSettings());
					OutputSettings->ExportSettings.bShouldExportFoliage = bShouldExportFoliage;
				}
			}
			else
			{
				return false;
			}
		}

		return true;
	};

	TArray<UPCGNode*> NodesToExport;

	const UPVEditorSettings* EditorSettings = GetDefault<UPVEditorSettings>();
	
	if (EditorSettings->ExportType == EPVExportType::Selection)
	{
		UE_LOG(LogProceduralVegetationEditor, Log, TEXT("Exporting the selected nodes"));
		
		for (int i = 0; i < SelectedNodes.Num(); i++)
		{
			if (UPCGNode* Node = SelectedNodes[i] ?  SelectedNodes[i]->GetPCGNode() : nullptr)
			{
				if (const UPVOutputSettings* OutputSettings = Cast<UPVOutputSettings>(Node->GetSettings()))
				{
					NodesToExport.Add(Node);
				}	
			}
		}
	}
	else
	{
		UE_LOG(LogProceduralVegetationEditor, Log, TEXT("Batch Exporting nodes"));
		NodesToExport = ProceduralVegetationGraph->GetNodes().FilterByPredicate([](UPCGNode* InNode)->bool {
			const UPVOutputSettings* OutputSettings = Cast<const UPVOutputSettings>(InNode->GetSettings());
			return OutputSettings && OutputSettings->ExportSettings.bShouldExport;
		});
	}

	if (!ValidateExportSettings(NodesToExport, Messages))
	{
		return FReply::Handled();
	}

	if (!ShouldOverwriteAssets(NodesToExport))
	{
		return FReply::Handled();
	}
	
	TArray<FManagedArrayCollection> CollectionsToExport;
	if (!GetOutputNodeCollections(CollectionsToExport, NodesToExport, Messages))
	{
		return FReply::Handled();
	}

	if (!FoliageDistributorValidation(NodesToExport))
	{
		return FReply::Handled();
	}

	FScopedSlowTask ProgressSlowTask(NodesToExport.Num(), LOCTEXT("ExportAsset_SlowTask", "Exporting Asset(s) ..."));
	ProgressSlowTask.MakeDialog(false);

	for (int32 i = 0; i < NodesToExport.Num(); ++i)
	{
		if (ProgressSlowTask.ShouldCancel())
		{
			break;
		}

		const UPCGNode* Node = NodesToExport[i];
		const UPVOutputSettings* OutputSettings = CastChecked<const UPVOutputSettings>(Node->GetSettings());
		check(OutputSettings != nullptr);

		const FText AssetCountText = NodesToExport.Num() > 1
			? FText::FromString(FString::Printf(TEXT(" [%d/%d]"), i + 1, NodesToExport.Num()))
			: FText::FromString("");

		ProgressSlowTask.EnterProgressFrame(1.f, FText::Format(LOCTEXT("ExportingAsset", "Exporting \"{0}\"{1}"), FText::FromName(OutputSettings->ExportSettings.MeshName), AssetCountText));

		FScopedSlowTask ExportCollectionSlowTask(1.f, LOCTEXT("GeneratingMesh", "Generating Mesh ..."));
		float PrevProgress = 0.f;
		const auto OnStatusUpdated = [&](const FText& Stage, float Progress)->bool
		{
			const float WorkThisFrame = Progress - PrevProgress;
			PrevProgress = Progress;

			ExportCollectionSlowTask.FrameMessage = Stage;

			ExportCollectionSlowTask.CompletedWork += ExportCollectionSlowTask.CurrentFrameScope;

			const float WorkRemaining = ExportCollectionSlowTask.TotalAmountOfWork - ExportCollectionSlowTask.CompletedWork;
			ExportCollectionSlowTask.CurrentFrameScope = FMath::Min(WorkRemaining, WorkThisFrame);
			ExportCollectionSlowTask.ForceRefresh();

			return !ProgressSlowTask.ShouldCancel();
		};

		TArray<FString> CreatedAssets;
		const PV::Export::EExportResult ExportResult = PV::Export::ExportCollectionAsMesh(
			ProceduralVegetationBeingEdited, 
			CollectionsToExport[i], 
			OutputSettings->ExportSettings,
			CreatedAssets,
			OnStatusUpdated
		);

		if (ExportResult == PV::Export::EExportResult::Fail)
		{
			Messages.Error(FText::Format(
				LOCTEXT("ExportFailed_UnknownError", "Failed to export mesh \"{0}\" due to unknown error, export aborted."),
				FText::FromName(OutputSettings->ExportSettings.MeshName)
			));
			
			break;
		}
		else if (ExportResult == PV::Export::EExportResult::Canceled)
		{
			FString MeshNames = "{ ";
			for (int32 j = i; j < NodesToExport.Num(); j++)
			{
				MeshNames += CastChecked<const UPVOutputSettings>(NodesToExport[j]->GetSettings())->ExportSettings.MeshName.ToString();
				if (j != (NodesToExport.Num() - 1))
				{
					MeshNames += ", ";
				}
			}
			MeshNames += " }";

			Messages.Warning(
				FText::Format(
					LOCTEXT("ExportFailed_ExportCanceled", "Failed to export meshes \"{0}\", operation canceled by user."),
					FText::FromString(MeshNames)
				));

			break;
		}
		else if (ExportResult == PV::Export::EExportResult::Skipped)
		{
			Messages.Info(FText::Format(
				LOCTEXT("ExportSkipped", "Export of mesh \"{0}\" skipped due to conflicting asset at output location"),
				FText::FromName(OutputSettings->ExportSettings.MeshName)
			));
		}
		else
		{
			auto Message = Messages.Info(FText::Format(
				LOCTEXT("ExportComplete", "Mesh exported successfully \"{0}\""),
				FText::FromName(OutputSettings->ExportSettings.MeshName)
			));

			for (const FString& AssetPath : CreatedAssets)
			{
				Message->AddToken(FAssetNameToken::Create(AssetPath));
			}
		}
	}
	
	return FReply::Handled();
}

void FPVEditor::OnLockNodeSelection()
{
	UPVBaseSettings* NodeBeingInspectedSettings = GetNodeSettings(NodeBeingInspected);
	UPVBaseSettings* SelectedNodeSettings = GetNodeSettings(GetSelectedNode());

	if (NodeBeingInspectedSettings)
	{
		NodeBeingInspectedSettings->bLockInspection = !NodeBeingInspectedSettings->bLockInspection;
	}

	if (NodeBeingInspected != GetSelectedNode())
	{
		if (GetSelectedNode())
		{
			SelectedNodeSettings->bLockInspection = true;
			ChangeNodeInspection(GetSelectedNode());
		}
	}
	if (ViewportWidgets.IsEmpty())
	{
		return;
	}

	NodeBeingInspectedSettings = GetNodeSettings(NodeBeingInspected);

	TArray<FText> OverlayStats;
	GatherStats(OverlayStats);
	ViewportWidgets[0]->PopulateStatsOverlayText(OverlayStats);
	
	ViewportWidgets[0]->SetOverlayText(
		NodeBeingInspectedSettings->bLockInspection
		? NodeBeingInspected->GetNodeTitle(ENodeTitleType::FullTitle)
		: FText::GetEmpty()
	);
}

TSharedRef<SPCGEditorViewport> FPVEditor::CreateViewportWidget()
{
	TSharedPtr<SPVEditorViewport> ViewportWidget;
	
	SAssignNew(ViewportWidget, SPVEditorViewport);

	ViewportWidgets.Add(ViewportWidget.ToSharedRef());

	return ViewportWidget.ToSharedRef();
	
}

const UPVData* FPVEditor::GetPVDataFromNode(const UPCGNode* InNode, FName& OutPinName) const
{
	const UPCGPin* Pin = nullptr;
		
	const TArray<TObjectPtr<UPCGPin>>& Pins = InNode->GetOutputPins();

	if (Pins.IsValidIndex(0))
	{
		Pin = Pins[0];
	}
	
	const FPCGStack* PCGStack = GetStackBeingInspected();
	check(PCGStack)
		
	// Create a temporary stack with Node + valid Pin to query the exact DataCollection
	FPCGStack Stack = *PCGStack;
	TArray<FPCGStackFrame>& StackFrames = Stack.GetStackFramesMutable();
	StackFrames.Reserve(StackFrames.Num() + 2);
	StackFrames.Emplace(InNode);
	if (Pin)
	{
		StackFrames.Emplace(Pin);
	}
		
	const FPCGDataCollection* Data = ExecutionSource->GetExecutionState().GetInspection().GetInspectionData(Stack);
		
	if(Data != nullptr)
	{
		const FPCGTaggedData* TaggedData = Data->TaggedData.GetData();
		
		if(TaggedData != nullptr)
		{
			const UPVData* ProceduralVegetationData = Cast<UPVData>(TaggedData->Data);
			if(ProceduralVegetationData != nullptr && Pin)
			{
				OutPinName = Pin->Properties.Label;
				return ProceduralVegetationData;
			}
		}
	}

	return nullptr;
}

bool FPVEditor::CanToggleInspected() const
{
	return false;
}

UPCGEditorGraphNodeBase* FPVEditor::GetSelectedNode()
{
	if (SelectedNodes.Num())
	{
		return SelectedNodes[0];
	}

	return nullptr;
}

bool FPVEditor::GetCollectionFromNode(const UPCGNode* InNode, FName& OutPinName, FManagedArrayCollection& OutCollection) const
{
	const UPVData* PVData = GetPVDataFromNode(InNode, OutPinName);
	if(PVData != nullptr)
	{
		OutCollection = PVData->GetCollection();
		return true;
	}

	return false;
}

TSharedRef<SCollectionSpreadSheetWidget> FPVEditor::CreateCollectionSpreadSheetWidget()
{
	return SNew(SCollectionSpreadSheetWidget);
}

TSharedRef<SDockTab> FPVEditor::SpawnTab_CollectionSpreadSheet(const FSpawnTabArgs& SpawnTabArgs)
{
	check(SpawnTabArgs.GetTabId() == FPVEditor::CollectionSpreadSheetTabId);

	TSharedRef<SDockTab> DockableTab = SNew(SDockTab)
		[
			CollectionSpreadSheetWidget.ToSharedRef()
		];

	CollectionSpreadSheetTab = DockableTab;

	return DockableTab;
}

void FPVEditor::UpdateCollectionSpreadSheet(const UPCGNode* InSelectedNode)
{
	const bool bIsCollectionSpreadSheetVisible = CollectionSpreadSheetTab.IsValid() &&
		CollectionSpreadSheetTab.Pin()->GetVisibility().IsVisible() &&
		CollectionSpreadSheetWidget;

	if (InSelectedNode)
	{
		if (bIsCollectionSpreadSheetVisible)
		{
			CollectionSpreadSheetWidget->GetCollectionTable()->GetCollectionInfoMap().Empty();
			CollectionSpreadSheetWidget->GetCollectionTable()->GetCollectionInfoMap().Add("Out", {CollectionBeingInspected});
			CollectionSpreadSheetWidget->SetData(InSelectedNode->GetNodeTitle(EPCGNodeTitleType::FullTitle).ToString());
		}
	}
	else if (bIsCollectionSpreadSheetVisible)
	{
		CollectionSpreadSheetWidget->SetData(FString());
	}
	
	if (bIsCollectionSpreadSheetVisible)
	{
		CollectionSpreadSheetWidget->RefreshWidget();
	}
}

void FPVEditor::GatherStats(TArray<FText>& OverlayStats)
{
	if (CollectionBeingInspected->HasGroup(PV::GroupNames::PointGroup))
	{
		const int32 NumPoints = CollectionBeingInspected->NumElements(PV::GroupNames::PointGroup);
		OverlayStats.Add(FText::FromString("Points: " + FString::FromInt(NumPoints)));
	}
	if (CollectionBeingInspected->HasGroup(PV::GroupNames::BranchGroup))
	{
		const int32 NumBranches = CollectionBeingInspected->NumElements(PV::GroupNames::BranchGroup);
		OverlayStats.Add(FText::FromString("Branches: " + FString::FromInt(NumBranches)));
	}
	if (CollectionBeingInspected->HasGroup(FGeometryCollection::VerticesGroup))
	{
		const int32 NumVertices = CollectionBeingInspected->NumElements(FGeometryCollection::VerticesGroup);
		OverlayStats.Add(FText::FromString("Vertices: " + FString::FromInt(NumVertices)));
	}
	if (CollectionBeingInspected->HasGroup(FGeometryCollection::FacesGroup))
	{
		const int32 NumTriangles = CollectionBeingInspected->NumElements(FGeometryCollection::FacesGroup);
		OverlayStats.Add(FText::FromString("Triangles: " + FString::FromInt(NumTriangles)));
	}
	if (CollectionBeingInspected->HasGroup(PV::GroupNames::BonesGroup))
	{
		const int32 NumBones = CollectionBeingInspected->NumElements(PV::GroupNames::BonesGroup);
		OverlayStats.Add(FText::FromString("Bones: " + FString::FromInt(NumBones)));
	}
	if (CollectionBeingInspected->HasGroup(PV::GroupNames::FoliageNamesGroup))
	{
		const int32 NumUniqueFoliage = CollectionBeingInspected->NumElements(PV::GroupNames::FoliageNamesGroup);
		OverlayStats.Add(FText::FromString("Unique Foliage: " + FString::FromInt(NumUniqueFoliage)));
	}
	if (CollectionBeingInspected->HasGroup(PV::GroupNames::FoliageGroup))
	{
		const int32 NumFoliageInstances = CollectionBeingInspected->NumElements(PV::GroupNames::FoliageGroup);
		OverlayStats.Add(FText::FromString("Foliage Instances: " + FString::FromInt(NumFoliageInstances)));
	}
}

#undef LOCTEXT_NAMESPACE
