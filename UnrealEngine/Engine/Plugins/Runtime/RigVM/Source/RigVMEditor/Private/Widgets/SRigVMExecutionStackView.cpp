// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SRigVMExecutionStackView.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Editor/RigVMNewEditor.h"
#include "Editor/RigVMExecutionStackCommands.h"
#include "RigVMHost.h"
#include "RigVMBlueprintGeneratedClass.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "DetailLayoutBuilder.h"
#include "RigVMModel/Nodes/RigVMAggregateNode.h"
#include "Widgets/Input/SSearchBox.h"
#include "Dialog/SCustomDialog.h"
#include "Async/TaskGraphInterfaces.h"

#define LOCTEXT_NAMESPACE "SRigVMExecutionStackView"

TAutoConsoleVariable<bool> CVarRigVMExecutionStackDetailedLabels(TEXT("RigVM.StackDetailedLabels"), false, TEXT("Set to true to turn on detailed labels for the execution stack widget"));

//////////////////////////////////////////////////////////////
/// FRigStackEntry
///////////////////////////////////////////////////////////
FRigStackEntry::FRigStackEntry(int32 InEntryIndex, ERigStackEntry::Type InEntryType, int32 InInstructionIndex, ERigVMOpCode InOpCode, const FString& InLabel, const FRigVMASTProxy& InProxy)
	: EntryIndex(InEntryIndex)
	, EntryType(InEntryType)
	, InstructionIndex(InInstructionIndex)
	, CallPath(InProxy.GetCallstack().GetCallPath())
	, Callstack(InProxy.GetCallstack())
	, OpCode(InOpCode)
	, Label(InLabel)
	, MicroSeconds(0.0)
{

}

TSharedRef<ITableRow> FRigStackEntry::MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FRigStackEntry> InEntry,
	TSharedRef<FUICommandList> InCommandList, TSharedPtr<SRigVMExecutionStackView> InStackView, TWeakObjectPtr<UObject> InBlueprint)
{
	return SNew(SRigStackItem, InOwnerTable, InEntry, InCommandList, InBlueprint.Get());
}

TSharedRef<ITableRow> FRigStackEntry::MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FRigStackEntry> InEntry, TSharedRef<FUICommandList> InCommandList, TSharedPtr<SRigVMExecutionStackView> InStackView, TWeakInterfacePtr<IRigVMAssetInterface> InBlueprint)
{
	return SNew(SRigStackItem, InOwnerTable, InEntry, InCommandList, InBlueprint);
}

//////////////////////////////////////////////////////////////
/// SRigStackItem
///////////////////////////////////////////////////////////
void SRigStackItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, TSharedRef<FRigStackEntry> InStackEntry, TSharedRef<FUICommandList> InCommandList, TWeakInterfacePtr<IRigVMAssetInterface> InBlueprint)
{
	WeakStackEntry = InStackEntry;
	WeakBlueprint = InBlueprint;
	WeakCommandList = InCommandList;

	TSharedPtr< STextBlock > NumberWidget;
	TSharedPtr< STextBlock > TextWidget;

	const FSlateBrush* Icon = nullptr;
	switch (InStackEntry->EntryType)
	{
		case ERigStackEntry::Operator:
		{
			Icon = FSlateIcon(TEXT("RigVMEditor"), "RigVM.Unit").GetIcon();
			break;
		}
		case ERigStackEntry::Info:
		{
			Icon = FAppStyle::GetBrush("Icons.Info");
			break;
		}
		case ERigStackEntry::Warning:
		{
			Icon = FAppStyle::GetBrush("Icons.Warning");
			break;
		}
		case ERigStackEntry::Error:
		{
			Icon = FAppStyle::GetBrush("Icons.Error");
			break;
		}
		default:
		{
			break;
		}
	}

	STableRow<TSharedPtr<FRigStackEntry>>::Construct(
		STableRow<TSharedPtr<FRigStackEntry>>::FArguments()
		.Content()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.MaxWidth(35.f)
				.AutoWidth()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				[
					SAssignNew(NumberWidget, STextBlock)
					.Text(this, &SRigStackItem::GetIndexText)
					.ColorAndOpacity(this, &SRigStackItem::GetLabelColorAndOpacity)
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				+ SHorizontalBox::Slot()
				.MaxWidth(22.f)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				[
					SNew(SImage)
					.Image(Icon)
				]
				
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				.AutoWidth()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Fill)
				[
					SAssignNew(TextWidget, STextBlock)
					.Text(this, &SRigStackItem::GetLabelText)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(this, &SRigStackItem::GetLabelColorAndOpacity)
					.ToolTipText(this, &SRigStackItem::GetTooltip)
					.Justification(ETextJustify::Left)
				]
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				SNew(SSpacer)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
	            .HAlign(HAlign_Left)
	            [
	                SNew(STextBlock)
	                .Text(this, &SRigStackItem::GetVisitedCountText)
					.ColorAndOpacity(this, &SRigStackItem::GetLabelColorAndOpacity)
	                .Font(IDetailLayoutBuilder::GetDetailFont())
	            ]
	            
	            + SHorizontalBox::Slot()
	            .Padding(20, 0, 0, 0)
				.AutoWidth()
	            .VAlign(VAlign_Center)
	            .HAlign(HAlign_Left)
	            [
	                SNew(STextBlock)
	                .Text(this, &SRigStackItem::GetDurationText)
					.ColorAndOpacity(this, &SRigStackItem::GetLabelColorAndOpacity)
	                .Font(IDetailLayoutBuilder::GetDetailFont())
	            ]
	        ]
        ], OwnerTable);
}

FText SRigStackItem::GetIndexText() const
{
	const FString IndexStr = FString::FromInt(WeakStackEntry.Pin()->EntryIndex) + TEXT(".");
	return FText::FromString(IndexStr);
}

FText SRigStackItem::GetLabelText() const
{
	return (FText::FromString(WeakStackEntry.Pin()->Label));
}

FSlateColor SRigStackItem::GetLabelColorAndOpacity() const
{
	if (TSharedPtr<FRigStackEntry> StackEntry = WeakStackEntry.Pin())
	{
		if(StackEntry->ShowAsFadedOut.Get(false))
		{
			return FSlateColor::UseSubduedForeground();
		}
	}
	return FSlateColor::UseForeground();
}

FText SRigStackItem::GetTooltip() const
{
	return FText::FromString(WeakStackEntry.Pin()->Callstack.GetCallPath(true));
}

FText SRigStackItem::GetVisitedCountText() const
{
	if (TSharedPtr<FRigStackEntry> StackEntry = WeakStackEntry.Pin())
	{
		return StackEntry->VisitedCountText.Get(FText());
	}
	return FText();
}

FText SRigStackItem::GetDurationText() const
{
	if (TSharedPtr<FRigStackEntry> StackEntry = WeakStackEntry.Pin())
	{
		return StackEntry->DurationText.Get(FText());
	}
	return FText();
}

//////////////////////////////////////////////////////////////
/// SRigVMExecutionStackView
///////////////////////////////////////////////////////////

SRigVMExecutionStackView::~SRigVMExecutionStackView()
{
	UnbindHostDelegates();
	UnbindAssetDelegates();
}

void SRigVMExecutionStackView::Construct( const FArguments& InArgs, TSharedRef<IRigVMEditor> InRigVMEditor)
{
	WeakEditor = InRigVMEditor;
	WeakRigVMBlueprint = InRigVMEditor->GetRigVMAssetInterface();
	CommandList = MakeShared<FUICommandList>();
	bSuspendModelNotifications = false;
	bSuspendControllerSelection = false;

	BindCommands();

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		.VAlign(VAlign_Top)
		.Padding(0.0f)
		[
			SNew(SBorder)
			.Padding(0.0f)
			.BorderImage(FAppStyle::GetBrush("DetailsView.CategoryTop"))
			.BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.0f))
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				.VAlign(VAlign_Top)
				[
					SNew(SHorizontalBox)
					//.Visibility(this, &SRigHierarchy::IsSearchbarVisible)
					+SHorizontalBox::Slot()
					.AutoWidth()
					+SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.Padding(3.0f, 1.0f)
					[
						SAssignNew(FilterBox, SSearchBox)
						.OnTextChanged(this, &SRigVMExecutionStackView::OnFilterTextChanged)
					]
				]
			]
		]
		+SVerticalBox::Slot()
		.Padding(0.0f, 0.0f)
		[
			SNew(SBorder)
			.Padding(2.0f)
			.BorderImage(FAppStyle::GetBrush("SCSEditor.TreePanel"))
			[
				SAssignNew(TreeView, STreeView<TSharedPtr<FRigStackEntry>>)
				.TreeItemsSource(&Operators)
				.SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow(this, &SRigVMExecutionStackView::MakeTableRowWidget, WeakRigVMBlueprint)
				.OnGetChildren(this, &SRigVMExecutionStackView::HandleGetChildrenForTree)
				.OnSelectionChanged(this, &SRigVMExecutionStackView::OnSelectionChanged)
				.OnContextMenuOpening(this, &SRigVMExecutionStackView::CreateContextMenu)
				.OnMouseButtonDoubleClick(this, &SRigVMExecutionStackView::HandleItemMouseDoubleClick)
			]
		]
	];

	RefreshTreeView(nullptr, nullptr);

	if (TStrongObjectPtr<UObject> StrongPtr = WeakRigVMBlueprint.GetWeakObjectPtr().Pin())
	{
		IRigVMAssetInterface* RigVMBlueprint = WeakRigVMBlueprint.Get();
		if (OnVMCompiledHandle.IsValid())
		{
			RigVMBlueprint->OnVMCompiled().Remove(OnVMCompiledHandle);
		}
		if (OnModelModified.IsValid())
		{
			RigVMBlueprint->OnModified().Remove(OnModelModified);
		}
		OnVMCompiledHandle = RigVMBlueprint->OnVMCompiled().AddSP(this, &SRigVMExecutionStackView::OnVMCompiled);
		OnObjectBeingDebuggedChangedHandle = WeakRigVMBlueprint->OnSetObjectBeingDebugged().AddSP(this, &SRigVMExecutionStackView::OnObjectBeingDebuggedChanged);

		OnModelModified = RigVMBlueprint->OnModified().AddSP(this, &SRigVMExecutionStackView::HandleModifiedEvent);

		BindHostDelegates();
		
		URigVMHost* RigVMHost = PreviouslyDebuggedHost.Get();
		OnObjectBeingDebuggedChanged(RigVMHost);
	}

	if (TSharedPtr<IRigVMEditor> Editor = WeakEditor.Pin())
	{
		OnPreviewHostUpdatedHandle = Editor->OnPreviewHostUpdated().AddSP(this, &SRigVMExecutionStackView::HandlePreviewHostUpdated);
	}
}

void SRigVMExecutionStackView::OnSelectionChanged(TSharedPtr<FRigStackEntry> Selection, ESelectInfo::Type SelectInfo)
{
	if (bSuspendModelNotifications || bSuspendControllerSelection)
	{
		return;
	}
	TGuardValue<bool> SuspendNotifs(bSuspendModelNotifications, true);

	TArray<TSharedPtr<FRigStackEntry>> SelectedItems = TreeView->GetSelectedItems();
	if (SelectedItems.Num() > 0)
	{
		if (TStrongObjectPtr<UObject> StrongPtr = WeakRigVMBlueprint.GetWeakObjectPtr().Pin())
		{
			IRigVMAssetInterface* RigVMBlueprint = WeakRigVMBlueprint.Get();
			URigVMHost* Host = PreviouslyDebuggedHost.Get();
			if (Host == nullptr || Host->GetVM() == nullptr)
			{
				return;
			}

			const FRigVMByteCode& ByteCode = Host->GetVM()->GetByteCode();

			TMap<URigVMGraph*, TArray<FName>> SelectedNodesPerGraph;
			for (TSharedPtr<FRigStackEntry>& Entry : SelectedItems)
			{
				for(int32 StackIndex = 0; StackIndex < Entry->Callstack.Num(); StackIndex++)
				{
					const UObject* Subject = Entry->Callstack[StackIndex];
					URigVMGraph* SubjectGraph = nullptr;

					FName NodeName = NAME_None;
					if (const URigVMNode* Node = Cast<URigVMNode>(Subject))
					{
						NodeName = Node->GetFName();
						SubjectGraph = Node->GetGraph();
					}
					else if (const URigVMPin* Pin = Cast<URigVMPin>(Subject))
					{
						NodeName = Pin->GetNode()->GetFName();
						SubjectGraph = Pin->GetGraph();
					}

					if (NodeName.IsNone() || SubjectGraph == nullptr)
					{
						continue;
					}

					SelectedNodesPerGraph.FindOrAdd(SubjectGraph).AddUnique(NodeName);
				}
			}

			for (const TPair< URigVMGraph*, TArray<FName> >& Pair : SelectedNodesPerGraph)
			{
				RigVMBlueprint->GetOrCreateController(Pair.Key)->SetNodeSelection(Pair.Value);
			}
		}
	}

	UpdateTargetItemHighlighting();
}

void SRigVMExecutionStackView::BindCommands()
{
	// create new command
	const FRigVMExecutionStackCommands& Commands = FRigVMExecutionStackCommands::Get();
	CommandList->MapAction(Commands.FocusOnSelection, FExecuteAction::CreateSP(this, &SRigVMExecutionStackView::HandleFocusOnSelectedGraphNode));
	CommandList->MapAction(Commands.GoToInstruction, FExecuteAction::CreateSP(this, &SRigVMExecutionStackView::HandleGoToInstruction));
	CommandList->MapAction(Commands.SelectTargetInstructions, FExecuteAction::CreateSP(this, &SRigVMExecutionStackView::HandleSelectTargetInstructions));
}

TSharedRef<ITableRow> SRigVMExecutionStackView::MakeTableRowWidget(TSharedPtr<FRigStackEntry> InItem, const TSharedRef<STableViewBase>& OwnerTable, TWeakInterfacePtr<IRigVMAssetInterface> InBlueprint)
{
	return InItem->MakeTreeRowWidget(OwnerTable, InItem.ToSharedRef(), CommandList.ToSharedRef(), SharedThis(this), InBlueprint);
}

void SRigVMExecutionStackView::HandleGetChildrenForTree(TSharedPtr<FRigStackEntry> InItem, TArray<TSharedPtr<FRigStackEntry>>& OutChildren)
{
	OutChildren = InItem->Children;
}

void SRigVMExecutionStackView::PopulateStackView(URigVM* InVM, FRigVMExtendedExecuteContext* InVMContext)
{
	if (InVM && InVMContext)
	{
		const FRigVMInstructionArray Instructions = InVM->GetInstructions();
		const FRigVMByteCode& ByteCode = InVM->GetByteCode();

		TArray<URigVMGraph*> RootGraphs;
		RootGraphs.AddZeroed(Instructions.Num());

		const bool bUseSimpleLabels = !CVarRigVMExecutionStackDetailedLabels.GetValueOnAnyThread();
		if(bUseSimpleLabels)
		{
			for (int32 InstructionIndex = 0; InstructionIndex < Instructions.Num(); InstructionIndex++)
			{
				URigVMNode* Node = Cast<URigVMNode>(ByteCode.GetSubjectForInstruction(InstructionIndex));

				FString DisplayName;

				if(Node)
				{
					DisplayName = Node->GetName();
					RootGraphs[InstructionIndex] = Node->GetRootGraph();
					
					// only unit nodes among all nodes has StaticExecute() that generates actual instructions
					if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(Node))
					{
						DisplayName = UnitNode->GetNodeTitle();
	#if WITH_EDITOR
						UScriptStruct* Struct = UnitNode->GetScriptStruct();
						FString MenuDescSuffixMetadata;
						if (Struct)
						{
							Struct->GetStringMetaDataHierarchical(FRigVMStruct::MenuDescSuffixMetaName, &MenuDescSuffixMetadata);
						}
						if (!MenuDescSuffixMetadata.IsEmpty())
						{
							DisplayName = FString::Printf(TEXT("%s %s"), *UnitNode->GetNodeTitle(), *MenuDescSuffixMetadata);
						}

						if(UnitNode->IsEvent())
						{
							DisplayName = Node->GetEventName().ToString();
							if(!DisplayName.EndsWith(TEXT("Event")))
							{
								DisplayName += TEXT(" Event");
							}
						}
	#endif
					}
				}

				FString Label;

				switch (Instructions[InstructionIndex].OpCode)
				{
					case ERigVMOpCode::Execute:
					{
						const FRigVMExecuteOp& Op = ByteCode.GetOpAt<FRigVMExecuteOp>(Instructions[InstructionIndex]);

						if(Node)
						{
							Label = DisplayName;
						}
						else
						{
							Label = InVM->GetRigVMFunctionName(Op.FunctionIndex);
						}
						break;
					}
					case ERigVMOpCode::Copy:
					case ERigVMOpCode::Zero:
					case ERigVMOpCode::BoolFalse:
					case ERigVMOpCode::BoolTrue:
					case ERigVMOpCode::Increment:
					case ERigVMOpCode::Decrement:
					case ERigVMOpCode::Equals:
					case ERigVMOpCode::NotEquals:
					case ERigVMOpCode::JumpAbsolute:
					case ERigVMOpCode::JumpForward:
					case ERigVMOpCode::JumpBackward:
					case ERigVMOpCode::JumpAbsoluteIf:
					case ERigVMOpCode::JumpForwardIf:
					case ERigVMOpCode::JumpBackwardIf:
					case ERigVMOpCode::BeginBlock:
					case ERigVMOpCode::EndBlock:
					case ERigVMOpCode::ChangeType:
					case ERigVMOpCode::ArrayReset:
					case ERigVMOpCode::ArrayGetNum: 
					case ERigVMOpCode::ArraySetNum:
					case ERigVMOpCode::ArrayGetAtIndex:  
					case ERigVMOpCode::ArraySetAtIndex:
					case ERigVMOpCode::ArrayAdd:
					case ERigVMOpCode::ArrayInsert:
					case ERigVMOpCode::ArrayRemove:
					case ERigVMOpCode::ArrayFind:
					case ERigVMOpCode::ArrayAppend:
					case ERigVMOpCode::ArrayClone:
					case ERigVMOpCode::ArrayIterator:
					case ERigVMOpCode::ArrayUnion:
					case ERigVMOpCode::ArrayDifference:
					case ERigVMOpCode::ArrayIntersection:
					case ERigVMOpCode::ArrayReverse:
					{
						const FText OpCodeText = StaticEnum<ERigVMOpCode>()->GetDisplayNameTextByValue((int32)Instructions[InstructionIndex].OpCode);
						if(Node)
						{
							Label = DisplayName + TEXT(" - ") + OpCodeText.ToString();
						}
						else
						{
							Label = OpCodeText.ToString();
						}
						break;
					}
					case ERigVMOpCode::InvokeEntry:
					{
						const FRigVMInvokeEntryOp& Op = ByteCode.GetOpAt<FRigVMInvokeEntryOp>(Instructions[InstructionIndex]);
						Label = FString::Printf(TEXT("Run %s Event"), *Op.EntryName.ToString());
						break;
					}
					case ERigVMOpCode::JumpToBranch:
					{
						const FRigVMJumpToBranchOp& Op = ByteCode.GetOpAt<FRigVMJumpToBranchOp>(Instructions[InstructionIndex]);
						Label = TEXT("Jump To Branch");
						break;
					}
					case ERigVMOpCode::RunInstructions:
					{
						const FRigVMRunInstructionsOp& Op = ByteCode.GetOpAt<FRigVMRunInstructionsOp>(Instructions[InstructionIndex]);
						Label = FString::Printf(TEXT("Run Instructions %d-%d"), Op.StartInstruction, Op.EndInstruction);
						break;
					}
					case ERigVMOpCode::SetupTraits:
					{
						Label = TEXT("Setup Traits");
						break;
					}
					case ERigVMOpCode::Exit:
					{
						Label = TEXT("Exit");
						break;
					}
					default:
					{
						ensure(false);
						break;
					}
				}
				
				// add the entry with the new label to the stack view
				const FRigVMInstruction& Instruction = Instructions[InstructionIndex];
				if (FilterText.IsEmpty() || Label.Contains(FilterText.ToString()))
				{
					FRigVMASTProxy Proxy;
					if(const TArray<TWeakObjectPtr<UObject>>* Callstack = ByteCode.GetCallstackForInstruction(InstructionIndex))
					{
						Proxy = FRigVMASTProxy::MakeFromCallstack(Callstack);
					}
					TSharedPtr<FRigStackEntry> NewEntry = MakeShared<FRigStackEntry>(InstructionIndex, ERigStackEntry::Operator, InstructionIndex, Instruction.OpCode, Label, Proxy);
					Operators.Add(NewEntry);
				}
			}
			return;
		}

		// 1. cache information about instructions/nodes, which will be used later 
		TMap<FString, FString> OperandFormatMap;
		for (int32 InstructionIndex = 0; InstructionIndex < Instructions.Num(); InstructionIndex++)
		{
			const FRigVMASTProxy Proxy = FRigVMASTProxy::MakeFromCallPath(ByteCode.GetCallPathForInstruction(InstructionIndex), RootGraphs[InstructionIndex]);
			if(URigVMNode* Node = Proxy.GetSubject<URigVMNode>())
			{
				FString DisplayName = Node->GetName();

				// only unit nodes among all nodes has StaticExecute() that generates actual instructions
				if (URigVMUnitNode* UnitNode = Cast<URigVMUnitNode>(Node))
				{
					DisplayName = UnitNode->GetNodeTitle();
#if WITH_EDITOR
					UScriptStruct* Struct = UnitNode->GetScriptStruct();
					FString MenuDescSuffixMetadata;
					if (Struct)
					{
						Struct->GetStringMetaDataHierarchical(FRigVMStruct::MenuDescSuffixMetaName, &MenuDescSuffixMetadata);
					}
					if (!MenuDescSuffixMetadata.IsEmpty())
					{
						DisplayName = FString::Printf(TEXT("%s %s"), *UnitNode->GetNodeTitle(), *MenuDescSuffixMetadata);
					}
#endif
				}

				// this is needed for name replacement later
				OperandFormatMap.Add(Node->GetName(), DisplayName);
			}
		}

		// 2. replace raw operand names with NodeTitle.PinName/PropertyName.OffsetName
		TArray<FString> Labels = InVM->DumpByteCodeAsTextArray(*InVMContext, TArray<int32>(), false, [OperandFormatMap](const FString& RegisterName, const FString& RegisterOffsetName)
		{
			FString NewRegisterName = RegisterName;
			FString NodeName;
			FString PinName;
			if (RegisterName.Split(TEXT("."), &NodeName, &PinName))
			{
				const FString* NodeTitle = OperandFormatMap.Find(NodeName);
				NewRegisterName = FString::Printf(TEXT("%s.%s"), NodeTitle ? **NodeTitle : *NodeName, *PinName);
			}
			FString OperandLabel;
			OperandLabel = NewRegisterName;
			if (!RegisterOffsetName.IsEmpty())
			{
				OperandLabel = FString::Printf(TEXT("%s.%s"), *OperandLabel, *RegisterOffsetName);
			}
			return OperandLabel;
		});

		if (!ensure(Labels.Num() == Instructions.Num()))
		{
			return;
		}
		
		// 3. replace instruction names with node titles
		for (int32 InstructionIndex = 0; InstructionIndex < Labels.Num(); InstructionIndex++)
		{
			FString Label = Labels[InstructionIndex];
			const FRigVMASTProxy Proxy = FRigVMASTProxy::MakeFromCallPath(ByteCode.GetCallPathForInstruction(InstructionIndex), RootGraphs[InstructionIndex]);

			if(URigVMNode* Node = Proxy.GetSubject<URigVMNode>())
			{
				FString Suffix;
				switch(Instructions[InstructionIndex].OpCode)
				{
					case ERigVMOpCode::Copy:
					case ERigVMOpCode::Zero:
	                case ERigVMOpCode::BoolFalse:
	                case ERigVMOpCode::BoolTrue:
	                case ERigVMOpCode::Increment:
	                case ERigVMOpCode::Decrement:
					case ERigVMOpCode::Equals:
                	case ERigVMOpCode::NotEquals:
					case ERigVMOpCode::JumpAbsolute:
                	case ERigVMOpCode::JumpForward:
                	case ERigVMOpCode::JumpBackward:
					case ERigVMOpCode::JumpAbsoluteIf:
                	case ERigVMOpCode::JumpForwardIf:
                	case ERigVMOpCode::JumpBackwardIf:
					case ERigVMOpCode::BeginBlock:
					case ERigVMOpCode::EndBlock:
					{
						const FText OpCodeText = StaticEnum<ERigVMOpCode>()->GetDisplayNameTextByValue((int32)Instructions[InstructionIndex].OpCode);
						Suffix = FString::Printf(TEXT(" - %s"), *OpCodeText.ToString());
						break;
					}
					default:
					{
						break;
					}
				}
				
				Label =  OperandFormatMap[Node->GetName()] + Suffix;
				// Label = Proxy.GetCallstack().GetCallPath() + Suffix;
			}

			// add the entry with the new label to the stack view
			const FRigVMInstruction& Instruction = Instructions[InstructionIndex];
			if (FilterText.IsEmpty() || Label.Contains(FilterText.ToString()))
			{
				TSharedPtr<FRigStackEntry> NewEntry = MakeShared<FRigStackEntry>(InstructionIndex, ERigStackEntry::Operator, InstructionIndex, Instruction.OpCode, Label, Proxy);
				Operators.Add(NewEntry);
			}
		}
	}
}

void SRigVMExecutionStackView::BindHostDelegates()
{
	UnbindHostDelegates();
	
	if (TStrongObjectPtr<UObject> Blueprint = WeakRigVMBlueprint.GetWeakObjectPtr().Pin())
	{
		BindHostDelegates(WeakRigVMBlueprint->GetObjectBeingDebugged());
	}
}

void SRigVMExecutionStackView::BindHostDelegates(UObject* InObjectBeingDebugged)
{
	UnbindHostDelegates();

	if (URigVMHost* DebuggedHost = Cast<URigVMHost>(InObjectBeingDebugged))
	{
		PreviouslyDebuggedHost = DebuggedHost;
		OnHostInitializedHandle = DebuggedHost->OnInitialized_AnyThread().AddSP(this, &SRigVMExecutionStackView::HandleHostInitializedEvent);
		OnHostExecutedHandle = DebuggedHost->OnExecuted_AnyThread().AddSP(this, &SRigVMExecutionStackView::HandleHostExecutedEvent);
	}
}

void SRigVMExecutionStackView::UnbindHostDelegates()
{
	if (URigVMHost* Host = PreviouslyDebuggedHost.Get())
	{
		if (OnHostInitializedHandle.IsValid())
		{
			Host->OnInitialized_AnyThread().Remove(OnHostInitializedHandle);
		}
		if (OnHostExecutedHandle.IsValid())
		{
			Host->OnExecuted_AnyThread().Remove(OnHostExecutedHandle);
		}
	}
	OnHostInitializedHandle.Reset();
	OnHostExecutedHandle.Reset();
	PreviouslyDebuggedHost.Reset();
}

void SRigVMExecutionStackView::UnbindAssetDelegates()
{
	if (TSharedPtr<IRigVMEditor> Editor = WeakEditor.Pin())
	{
		if (OnModelModified.IsValid() && Editor->GetRigVMAssetInterface())
		{
			Editor->GetRigVMAssetInterface()->OnModified().Remove(OnModelModified);
		}
	}
	if (TStrongObjectPtr<UObject> Blueprint = WeakRigVMBlueprint.GetWeakObjectPtr().Pin())
	{
		if (OnVMCompiledHandle.IsValid())
		{
			WeakRigVMBlueprint->OnVMCompiled().Remove(OnVMCompiledHandle);
		}
		if (OnObjectBeingDebuggedChangedHandle.IsValid())
		{
			WeakRigVMBlueprint->OnSetObjectBeingDebugged().Remove(OnObjectBeingDebuggedChangedHandle);
		}
	}
}

void SRigVMExecutionStackView::RefreshTreeView(URigVM* InVM, FRigVMExtendedExecuteContext* InVMContext)
{
	Operators.Reset();

	// populate the stack with node names/instruction names
	PopulateStackView(InVM, InVMContext);
	
	if (InVM && InVMContext)
	{
		// fill the children from the log
		if (TSharedPtr<IRigVMEditor> Editor = WeakEditor.Pin())
		{
			URigVMHost* Host = PreviouslyDebuggedHost.Get();
			if(Host && Host->GetLog())
			{
				const TArray<FRigVMLog::FLogEntry>& LogEntries = Host->GetLog()->Entries;
				for (const FRigVMLog::FLogEntry& LogEntry : LogEntries)
				{
					if (!Operators.IsValidIndex(LogEntry.InstructionIndex))
					{
						continue;
					}
					int32 ChildIndex = Operators[LogEntry.InstructionIndex]->Children.Num();
					switch (LogEntry.Severity)
					{
						case EMessageSeverity::Info:
						{
							Operators[LogEntry.InstructionIndex]->Children.Add(MakeShared<FRigStackEntry>(ChildIndex, ERigStackEntry::Info, LogEntry.InstructionIndex, ERigVMOpCode::Invalid, LogEntry.Message, FRigVMASTProxy()));
							break;
						}
						case EMessageSeverity::Warning:
						case EMessageSeverity::PerformanceWarning:
						{
							Operators[LogEntry.InstructionIndex]->Children.Add(MakeShared<FRigStackEntry>(ChildIndex, ERigStackEntry::Warning, LogEntry.InstructionIndex, ERigVMOpCode::Invalid, LogEntry.Message, FRigVMASTProxy()));
							break;
						}
						case EMessageSeverity::Error:
						{
							Operators[LogEntry.InstructionIndex]->Children.Add(MakeShared<FRigStackEntry>(ChildIndex, ERigStackEntry::Error, LogEntry.InstructionIndex, ERigVMOpCode::Invalid, LogEntry.Message, FRigVMASTProxy()));
							break;
						}
						default:
						{
							break;
						}
					}
				}
			}
		}
	}

	TreeView->RequestTreeRefresh();
}

TSharedPtr< SWidget > SRigVMExecutionStackView::CreateContextMenu()
{
	TArray<TSharedPtr<FRigStackEntry>> SelectedItems = TreeView->GetSelectedItems();
	if(SelectedItems.Num() == 0)
	{
		return nullptr;
	}

	const FRigVMExecutionStackCommands& Actions = FRigVMExecutionStackCommands::Get();

	const bool CloseAfterSelection = true;
	FMenuBuilder MenuBuilder(CloseAfterSelection, CommandList);
	{
		MenuBuilder.BeginSection("RigStackToolsAction", LOCTEXT("ToolsAction", "Tools"));
		MenuBuilder.AddMenuEntry(Actions.FocusOnSelection);
		MenuBuilder.AddMenuEntry(Actions.GoToInstruction);

		if(SelectedItems.ContainsByPredicate([](const TSharedPtr<FRigStackEntry>& InEntry) -> bool
		{
			return InEntry->OpCode == ERigVMOpCode::JumpAbsolute ||
				InEntry->OpCode == ERigVMOpCode::JumpBackward || 
				InEntry->OpCode == ERigVMOpCode::JumpForward ||
				InEntry->OpCode == ERigVMOpCode::JumpAbsoluteIf || 
				InEntry->OpCode == ERigVMOpCode::JumpBackwardIf || 
				InEntry->OpCode == ERigVMOpCode::JumpForwardIf ||
				InEntry->OpCode == ERigVMOpCode::RunInstructions;
		}))
		{
			MenuBuilder.AddMenuEntry(Actions.SelectTargetInstructions);
		}
		
		MenuBuilder.EndSection();
	}

	return MenuBuilder.MakeWidget();
}

void SRigVMExecutionStackView::HandleFocusOnSelectedGraphNode()
{
	OnSelectionChanged(TSharedPtr<FRigStackEntry>(), ESelectInfo::Direct);

	TArray<TSharedPtr<FRigStackEntry>> SelectedItems = TreeView->GetSelectedItems();
	if (SelectedItems.Num() > 0)
	{
		if (TSharedPtr<IRigVMEditor> Editor = WeakEditor.Pin())
		{
			if (TStrongObjectPtr<UObject> StrongPtr = WeakRigVMBlueprint.GetWeakObjectPtr().Pin())
			{
				IRigVMAssetInterface* RigVMBlueprint = WeakRigVMBlueprint.Get();
				URigVMHost* Host = PreviouslyDebuggedHost.Get();
				if (Host == nullptr || Host->GetVM() == nullptr)
				{
					return;
				}

				const FRigVMByteCode& ByteCode = Host->GetVM()->GetByteCode();
				UObject* Subject = ByteCode.GetSubjectForInstruction(SelectedItems[0]->InstructionIndex);
				if (URigVMNode* SelectedNode = Cast<URigVMNode>(Subject))
				{
					URigVMGraph* GraphToFocus = SelectedNode->GetGraph();
					if (GraphToFocus && GraphToFocus->GetTypedOuter<URigVMAggregateNode>())
					{
						if(URigVMGraph* ParentGraph = GraphToFocus->GetParentGraph())
						{
							GraphToFocus = ParentGraph;
						} 
					}
				
					if (UEdGraph* EdGraph = RigVMBlueprint->GetEdGraph(GraphToFocus))
					{
						Editor->OpenGraphAndBringToFront(EdGraph, true);
						Editor->ZoomToSelection_Clicked();
						Editor->HandleModifiedEvent(ERigVMGraphNotifType::NodeSelected, GraphToFocus, SelectedNode);
					}
				}
			}
		}
	}
}

void SRigVMExecutionStackView::HandleGoToInstruction()
{
	// figure out the current instruction's index
	int32 Index = 0;
	TArray<TSharedPtr<FRigStackEntry>> SelectedItems = TreeView->GetSelectedItems();
	if (SelectedItems.Num() > 0)
	{
		Index = SelectedItems[0]->InstructionIndex;
	}
	
	const FVector2D MouseCursorLocation = FSlateApplication::Get().GetCursorPos();
	
	SWindow::FArguments WindowArguments;
	WindowArguments.ScreenPosition(MouseCursorLocation);
	WindowArguments.AutoCenter(EAutoCenter::None);
	WindowArguments.FocusWhenFirstShown(true);

	TSharedPtr<SNumericEntryBox<int32>> NumericBox;
	const TSharedRef<SCustomDialog> OptionsDialog = SNew(SCustomDialog)
	.Title(FText(LOCTEXT("GoToInstructionDialog", "Go to...")))
	.WindowArguments(WindowArguments)
	.Content()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1)
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		[
			SAssignNew(NumericBox, SNumericEntryBox<int32>)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Value(Index)
			.MinDesiredValueWidth(250)
			.MinValue(0)
			.MaxValue(Operators.Num() - 1)
			.OnValueChanged_Lambda([&Index](const int32& InIndex)
			{
				Index = InIndex;
			})
			.IsEnabled(true)
		]
	]
	.Buttons({
		SCustomDialog::FButton(LOCTEXT("OK", "OK")),
		SCustomDialog::FButton(LOCTEXT("Cancel", "Cancel"))
	});

	if(OptionsDialog->ShowModal() == 0)
	{
		TreeView->SetSelection(Operators[Index]);
		TreeView->SetScrollOffset(static_cast<float>(FMath::Max(Index-5, 0)));
	}
}

void SRigVMExecutionStackView::HandleSelectTargetInstructions()
{
	const TArray<TSharedPtr<FRigStackEntry>> TargetItems = GetTargetItems(TreeView->GetSelectedItems());
	if(!TargetItems.IsEmpty())
	{
		TreeView->ClearSelection();
		for(const TSharedPtr<FRigStackEntry>& TargetItem : TargetItems)
		{
			TreeView->SetItemSelection(TargetItem, true, ESelectInfo::Direct);
		}
		TreeView->RequestScrollIntoView(TargetItems[0]);
	}
}

TArray<TSharedPtr<FRigStackEntry>> SRigVMExecutionStackView::GetTargetItems(const TArray<TSharedPtr<FRigStackEntry>>& InItems) const
{
	TArray<TSharedPtr<FRigStackEntry>> TargetItems;

	URigVMHost* Host = PreviouslyDebuggedHost.Get();
	if (Host == nullptr || Host->GetVM() == nullptr)
	{
		return TargetItems;
	}

	const FRigVMByteCode& ByteCode = Host->GetVM()->GetByteCode();
	const FRigVMInstructionArray Instructions = ByteCode.GetInstructions();

	TArray<int32> TargetInstructionIndices;
	const TArray<TSharedPtr<FRigStackEntry>> SelectedItems = TreeView->GetSelectedItems();
	for(const TSharedPtr<FRigStackEntry>& SelectedItem : SelectedItems)
	{
		if(!Instructions.IsValidIndex(SelectedItem->InstructionIndex))
		{
			continue;
		}
		
		const FRigVMInstruction& Instruction = Instructions[SelectedItem->InstructionIndex];
		switch(SelectedItem->OpCode)
		{
			case ERigVMOpCode::JumpAbsolute:
			{
				const FRigVMJumpOp& Op = ByteCode.GetOpAt<FRigVMJumpOp>(Instruction);
				TargetInstructionIndices.AddUnique(Op.InstructionIndex);
				break;
			}
			case ERigVMOpCode::JumpAbsoluteIf:
			{
				const FRigVMJumpIfOp& Op = ByteCode.GetOpAt<FRigVMJumpIfOp>(Instruction);
				TargetInstructionIndices.AddUnique(Op.InstructionIndex);
				break;
			}
			case ERigVMOpCode::JumpForward:
			{
				const FRigVMJumpOp& Op = ByteCode.GetOpAt<FRigVMJumpOp>(Instruction);
				TargetInstructionIndices.AddUnique(SelectedItem->InstructionIndex + Op.InstructionIndex);
				break;
			}
			case ERigVMOpCode::JumpForwardIf:
			{
				const FRigVMJumpIfOp& Op = ByteCode.GetOpAt<FRigVMJumpIfOp>(Instruction);
				TargetInstructionIndices.AddUnique(SelectedItem->InstructionIndex + Op.InstructionIndex);
				break;
			}
			case ERigVMOpCode::JumpBackward:
			{
				const FRigVMJumpOp& Op = ByteCode.GetOpAt<FRigVMJumpOp>(Instruction);
				TargetInstructionIndices.AddUnique(SelectedItem->InstructionIndex - Op.InstructionIndex);
				break;
			}
			case ERigVMOpCode::JumpBackwardIf:
			{
				const FRigVMJumpIfOp& Op = ByteCode.GetOpAt<FRigVMJumpIfOp>(Instruction);
				TargetInstructionIndices.AddUnique(SelectedItem->InstructionIndex - Op.InstructionIndex);
				break;
			}
			case ERigVMOpCode::RunInstructions:
			{
				const FRigVMRunInstructionsOp& Op = ByteCode.GetOpAt<FRigVMRunInstructionsOp>(Instruction);
				for(int32 Index = Op.StartInstruction; Index <= Op.EndInstruction; Index++)
				{
					TargetInstructionIndices.AddUnique(Index);
				}
				break;
			}
			default:
			{
				break;
			}
		}
	}

	TMap<int32, TSharedPtr<FRigStackEntry>> InstructionToEntry;
	for(const TSharedPtr<FRigStackEntry>& Entry : Operators)
	{
		InstructionToEntry.Add(Entry->InstructionIndex, Entry);
	}

	for(const int32 TargetInstructionIndex : TargetInstructionIndices)
	{
		if(const TSharedPtr<FRigStackEntry>* EntryPtr = InstructionToEntry.Find(TargetInstructionIndex))
		{
			TargetItems.Add(*EntryPtr);
		}
	}

	return TargetItems;
}

void SRigVMExecutionStackView::UpdateTargetItemHighlighting()
{
	TreeView->ClearHighlightedItems();
	
	const TArray<TSharedPtr<FRigStackEntry>> TargetItems = GetTargetItems(TreeView->GetSelectedItems());
	for(const TSharedPtr<FRigStackEntry>& TargetItem : TargetItems)
	{
		if(!TreeView->IsItemSelected(TargetItem))
		{
			TreeView->SetItemHighlighted(TargetItem, true);
		}
	}
}

void SRigVMExecutionStackView::OnVMCompiled(UObject* InCompiledObject, URigVM* InCompiledVM, FRigVMExtendedExecuteContext& InVMContext)
{
	BindHostDelegates();
	RefreshTreeView(InCompiledVM, &InVMContext);
}

void SRigVMExecutionStackView::OnObjectBeingDebuggedChanged(UObject* InObjectBeingDebugged)
{
	BindHostDelegates(InObjectBeingDebugged);
	if (URigVMHost* DebuggedHost = Cast<URigVMHost>(InObjectBeingDebugged))
	{
		RefreshTreeView(DebuggedHost->GetVM(), &DebuggedHost->GetRigVMExtendedExecuteContext());
	}
}

void SRigVMExecutionStackView::OnFilterTextChanged(const FText& SearchText)
{
	FilterText = SearchText;
	if (URigVMHost* RigVMHost = PreviouslyDebuggedHost.Get())
	{
		RefreshTreeView(RigVMHost->GetVM(), &RigVMHost->GetRigVMExtendedExecuteContext());
	}
}

void SRigVMExecutionStackView::HandleModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject)
{
	if (bSuspendModelNotifications)
	{
		return;
	}

	URigVMHost* Host = PreviouslyDebuggedHost.Get();
	if (Host == nullptr || Host->GetVM() == nullptr)
	{
		return;
	}

	const FRigVMByteCode& ByteCode = Host->GetVM()->GetByteCode();

	switch (InNotifType)
	{
		case ERigVMGraphNotifType::NodeSelectionChanged:
		{
			const TGuardValue<bool> SuspendNotifs(bSuspendModelNotifications, true);
			TreeView->ClearSelection();
			TArray<const URigVMNode*> SelectedNodes;
			const TArray<FName>& SelectedNames = InGraph->GetSelectNodes();
			for (const FName& Selected : SelectedNames)
			{
				if (const URigVMNode* Node = InGraph->FindNodeByName(Selected))
				{
					SelectedNodes.Add(Node);
				}
			}		

			TArray<TSharedPtr<FRigStackEntry>> SelectedItems;
			for (TSharedPtr<FRigStackEntry>& Operator : Operators)
			{
				for (const URigVMNode* Node : SelectedNodes)
				{
					if(Operator->Callstack.Contains(Node))
					{
						SelectedItems.Add(Operator);
						break;
					}
				}
			}

			if(!SelectedItems.IsEmpty())
			{
				TreeView->SetItemSelection(SelectedItems, true, ESelectInfo::Direct);
				TreeView->RequestScrollIntoView(SelectedItems[0]);
			}

			UpdateTargetItemHighlighting();
			break;
		}
		default:
		{
			break;
		}
	}
}

void SRigVMExecutionStackView::HandleHostInitializedEvent(URigVMHost* InHost, const FName& InEventName)
{
	auto HandleHostInitializedEventLambda = [this, InHost, InEventName]
	{
		TGuardValue<bool> SuspendControllerSelection(bSuspendControllerSelection, true);

		RefreshTreeView(InHost->GetVM(), &InHost->GetRigVMExtendedExecuteContext());
		OnSelectionChanged(TSharedPtr<FRigStackEntry>(), ESelectInfo::Direct);

		for (TSharedPtr<FRigStackEntry>& Operator : Operators)
		{
			for (TSharedPtr<FRigStackEntry>& Child : Operator->Children)
			{
				if (Child->EntryType == ERigStackEntry::Warning || Child->EntryType == ERigStackEntry::Error)
				{
					TreeView->SetItemExpansion(Operator, true);
					break;
				}
			}
		}

		if (TSharedPtr<IRigVMEditor> Editor = WeakEditor.Pin())
		{
			InHost->OnInitialized_AnyThread().Remove(OnHostInitializedHandle);
			OnHostInitializedHandle.Reset();

			if (FRigVMAssetInterfacePtr RigBlueprint = Editor->GetRigVMAssetInterface())
			{
				TArray<URigVMGraph*> Models = RigBlueprint->GetAllModels();
				for (URigVMGraph* Model : Models)
				{
					for (URigVMNode* ModelNode : Model->GetNodes())
					{
						if (ModelNode->IsSelected())
						{
							HandleModifiedEvent(ERigVMGraphNotifType::NodeSelected, Model, ModelNode);
						}
					}
				}
			}
		}
	};
	
	if(IsInGameThread())
	{
		HandleHostInitializedEventLambda();
	}
	else
	{
		ExecuteOnGameThread(UE_SOURCE_LOCATION, HandleHostInitializedEventLambda);
	}
}

void SRigVMExecutionStackView::HandleHostExecutedEvent(URigVMHost* InHost, const FName& InEventName)
{
	auto HandleHostExecutedEventLambda = [this, InHost, InEventName]()
	{
		for(const TSharedPtr<FRigStackEntry>& Entry : Operators)
		{
			Entry->ShowAsFadedOut.Reset();
			Entry->VisitedCountText.Reset();
			Entry->DurationText.Reset();
		}

		if (WeakRigVMBlueprint.IsValid())
		{
			IRigVMAssetInterface* Blueprint = WeakRigVMBlueprint.Get();
			check(Blueprint);
			
			if(Blueprint->GetRigGraphDisplaySettings().bShowNodeRunCounts)
			{
				if(URigVM* VM = InHost->GetVM())
				{
					for(TSharedPtr<FRigStackEntry>& Entry : Operators)
					{
						if(Entry->EntryType == ERigStackEntry::Operator)
						{
							const int32 Count = VM->GetInstructionVisitedCount(InHost->GetRigVMExtendedExecuteContext(), Entry->InstructionIndex);
							if(Count > 0)
							{
								Entry->VisitedCountText = FText::FromString(FString::FromInt(Count));
							}
							else
							{
								Entry->ShowAsFadedOut = true;
							}
						}
					}
				}
			}

			if(Blueprint->GetVMRuntimeSettings().bEnableProfiling)
			{
				if(URigVM* VM = InHost->GetVM())
				{
					for(TSharedPtr<FRigStackEntry>& Entry : Operators)
					{
						if(Entry->EntryType == ERigStackEntry::Operator)
						{
							const double CurrentMicroSeconds = VM->GetInstructionMicroSeconds(InHost->GetRigVMExtendedExecuteContext(), Entry->InstructionIndex);
							Entry->MicroSeconds = Blueprint->GetRigGraphDisplaySettings().AggregateAverage(Entry->MicroSecondsFrames, Entry->MicroSeconds, CurrentMicroSeconds);
							if(Entry->MicroSeconds > 0.0)
							{
								Entry->DurationText = FText::FromString(FString::Printf(TEXT("%.02f µs"), (float)Entry->MicroSeconds));
							}
							else
							{
								Entry->ShowAsFadedOut = true;
							}
						}
					}
				}
			}
		}
	};

	if(IsInGameThread())
	{
		HandleHostExecutedEventLambda();
	}
	else
	{
		ExecuteOnGameThread(UE_SOURCE_LOCATION, HandleHostExecutedEventLambda);
	}

}

void SRigVMExecutionStackView::HandlePreviewHostUpdated(IRigVMEditor* InEditor)
{
	if (URigVMHost* RigVMHost = PreviouslyDebuggedHost.Get())
	{
		RefreshTreeView(RigVMHost->GetVM(), &RigVMHost->GetRigVMExtendedExecuteContext());
	}
}

void SRigVMExecutionStackView::HandleItemMouseDoubleClick(TSharedPtr<FRigStackEntry> InItem)
{
	if (TSharedPtr<IRigVMEditor> Editor = WeakEditor.Pin())
	{
		if (TStrongObjectPtr<UObject> StrongPtr = WeakRigVMBlueprint.GetWeakObjectPtr().Pin())
		{
			IRigVMAssetInterface* RigVMBlueprint = WeakRigVMBlueprint.Get();
			URigVMHost* Host = PreviouslyDebuggedHost.Get();
			if (!Host || !Host->GetVM())
			{
				return;
			}

			const FRigVMByteCode& ByteCode = Host->GetVM()->GetByteCode();
			if (URigVMNode* Subject = Cast<URigVMNode>(ByteCode.GetSubjectForInstruction(InItem->InstructionIndex)))
			{
				URigVMGraph* GraphToFocus = Subject->GetGraph();
				if (GraphToFocus && GraphToFocus->GetTypedOuter<URigVMAggregateNode>())
				{
					if(URigVMGraph* ParentGraph = GraphToFocus->GetParentGraph())
					{
						GraphToFocus = ParentGraph;
					} 
				}
		
				if(URigVMEdGraph* EdGraph = Cast<URigVMEdGraph>(RigVMBlueprint->GetEdGraph(GraphToFocus)))
				{
					if(const UEdGraphNode* Node = EdGraph->FindNodeForModelNodeName(Subject->GetFName()))
					{
						Editor->JumpToHyperlink(Node, false);
					}
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
