// Copyright Epic Games, Inc. All Rights Reserved.
#include "SMorphTargetManager.h"

#include "SkeletalMeshModelingToolsCommands.h"
#include "SPositiveActionButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Editor/EditorEngine.h"
#include "Internationalization/Regex.h"
#include "ScopedTransaction.h"
#include "Framework/Commands/GenericCommands.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

extern UNREALED_API UEditorEngine* GEditor;


#define LOCTEXT_NAMESPACE "SMorphTargetList"

namespace MorphTargetManagerLocal
{
	struct FMorphTargetInfo
	{
		FName Name = NAME_None;
		float Weight = 1.0f;
		TSharedPtr<SInlineEditableTextBlock> EditableText;
	};
	static const FName ColumnId_MorphTargetNameLabel( "MorphTargetName" );
	static const FName ColumnID_MorphTargetWeightLabel( "Weight" );
}

class SMorphTargetManagerListRow : public SMultiColumnTableRow< FMorphTargetInfoPtr >
{
public:

	SLATE_BEGIN_ARGS( SMorphTargetManagerListRow ) {}

	/** The item for this row **/
	SLATE_ARGUMENT( FMorphTargetInfoPtr, Item )

		/* The SMorphTargetViewer that we push the morph target weights into */
		SLATE_ARGUMENT( SMorphTargetManager* , Manager)

		/* Widget used to display the list of morph targets */
		SLATE_ARGUMENT( SMorphTargetManagerListType* , ListView )

	SLATE_END_ARGS()

	void Construct( const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView )
	{
		Item = InArgs._Item;
		Manager = InArgs._Manager;
		ListView = InArgs._ListView;
		
		SMultiColumnTableRow< FMorphTargetInfoPtr >::Construct( FSuperRowType::FArguments(), OwnerTableView );
	}

	/** Overridden from SMultiColumnTableRow.  Generates a widget for this column of the tree row. */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn( const FName& ColumnName ) override
	{
		using namespace MorphTargetManagerLocal;
		
		if ( ColumnName == ColumnId_MorphTargetNameLabel )
		{
			FText MorphNameText = FText::FromName(Item->Name);
			return
				SNew( SHorizontalBox )
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding( 4.0f , 4.0f, 0.0f, 4.0f )
				.VAlign( VAlign_Center )
				[
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("MorphTargetEditToggleToolTip", "Activate Morph Target for editing"))
					.Style(FAppStyle::Get(), "RadioButton")
					.OnCheckStateChanged(this, &SMorphTargetManagerListRow::OnSetEditingMorphTarget)
					.IsChecked(this, &SMorphTargetManagerListRow::IsEditingMorphTarget)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding( 0.0f, 4.0f )
				.VAlign( VAlign_Center )
				[
					SAssignNew(Item->EditableText, SInlineEditableTextBlock)
					.Text_Lambda([this]()
					{
						return FText::FromName(Item->Name);
					})
					.HighlightText(Manager,  &SMorphTargetManager::GetHighlightText, MorphNameText)
					.OnTextCommitted(this, &SMorphTargetManagerListRow::OnRenameMorphTarget)
					.IsSelected(this, &SMorphTargetManagerListRow::IsSelected)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding( 4.0f, 4.0f)
				.VAlign( VAlign_Center )
				[
					SNew(STextBlock)
					.Text( LOCTEXT("EditingMorphTargetMarker", "(Editing)") )
					.Visibility_Lambda([this]()
					{
						return IsEditingMorphTarget() == ECheckBoxState::Checked ? EVisibility::Visible : EVisibility::Collapsed;
					})
				];
		}
		else if ( ColumnName == ColumnID_MorphTargetWeightLabel )
		{
			// Encase the SSpinbox in an SVertical box so we can apply padding. Setting ItemHeight on the containing SListView has no effect :-(
			return
				SNew( SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding( 0.0f, 4.0f )
				.VAlign( VAlign_Center )
				[
					SNew( SSpinBox<float> )
					.MinSliderValue(-1.f)
					.MaxSliderValue(1.f)
					.Value( this, &SMorphTargetManagerListRow::GetWeight )
					.OnBeginSliderMovement(this, &SMorphTargetManagerListRow::OnBeginSlideMorphTargetWeight)
					.OnEndSliderMovement(this, &SMorphTargetManagerListRow::OnEndSlideMorphTargetWeight)
					.OnValueChanged( this, &SMorphTargetManagerListRow::OnMorphTargetWeightChanged )
					.OnValueCommitted( this, &SMorphTargetManagerListRow::OnMorphTargetWeightValueCommitted )
					.IsEnabled(this, &SMorphTargetManagerListRow::IsMorphTargetWeightSliderEnabled)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding( 0.0f, 4.0f )
				.VAlign( VAlign_Center )
				[
					SNew(SCheckBox)
					.ToolTipText(LOCTEXT("MorphTargetAutoFillToolTip", "When checked, animation system takes control of morph target weights"))
					.OnCheckStateChanged(this, &SMorphTargetManagerListRow::OnMorphTargetAutoFillChecked)
					.IsChecked(this, &SMorphTargetManagerListRow::IsMorphTargetAutoFillChangedChecked)
				];
		}
		

		return SNullWidget::NullWidget;
	}

	

private:

	/**
	* Called when the user begins/ends dragging the slider on the SSpinBox
	*/
	void OnBeginSlideMorphTargetWeight()
	{
		GEditor->BeginTransaction(LOCTEXT("OverrideMorphTargetWeight", "Override Morph Target Weight"));
	};
	void OnEndSlideMorphTargetWeight(float Value)
	{
		GEditor->EndTransaction();
	};
	
	/**
	* Called when the user changes the value of the SSpinBox
	*
	* @param NewWeight - The new number the SSpinBox is set to
	*
	*/
	void OnMorphTargetWeightChanged( float NewWeight )
	{
		Item->Weight = NewWeight;
		Manager->SetMorphTargetWeight(Item->Name, NewWeight);
	};
	
	/**
	* Called when the user types the value and enters
	*
	* @param NewWeight - The new number the SSpinBox is set to
	*
	*/
	void OnMorphTargetWeightValueCommitted( float NewWeight, ETextCommit::Type CommitType)
	{
		OnMorphTargetWeightChanged( NewWeight );
	};
	
	/**
	* Called to know if we enable or disable the weight sliders
	*/
	bool IsMorphTargetWeightSliderEnabled() const
	{
		return true;
	};
	
	/** Auto fill check call back functions */
	void OnMorphTargetAutoFillChecked(ECheckBoxState InState)
	{
		Manager->SetMorphTargetAutoFill(Item->Name, InState == ECheckBoxState::Checked, Item->Weight);
	};
	ECheckBoxState IsMorphTargetAutoFillChangedChecked() const
	{
		return Manager->GetMorphTargetAutoFill(Item->Name) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	};

	/** Auto fill check call back functions */
	void OnSetEditingMorphTarget(ECheckBoxState InState)
	{
		FName EditingMorphTarget = InState == ECheckBoxState::Checked ? Item->Name : NAME_None;
		Manager->SetEditingMorphTarget(EditingMorphTarget);
	};
	ECheckBoxState IsEditingMorphTarget() const
	{
		return Manager->IsEditingMorphTarget(Item->Name) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
	};
	
	/**
	* Returns the weight of this morph target
	*
	* @return SearchText - The new number the SSpinBox is set to
	*
	*/
	float GetWeight() const
	{
		return Manager->GetMorphTargetWeight(Item->Name);
	};

	void OnRenameMorphTarget(const FText& InNewName, ETextCommit::Type)
	{
		Item->Name = Manager->RenameMorphTarget( Item->Name, *InNewName.ToString());
	}

	/* The SMorphTargetViewer that we push the morph target weights into */
	SMorphTargetManager* Manager = nullptr;

	/** Widget used to display the list of morph targets */
	SMorphTargetManagerListType* ListView = nullptr;

	/** The name and weight of the morph target */
	FMorphTargetInfoPtr Item;
};



void SMorphTargetManager::Construct(const FArguments& InArgs)
{
	using namespace MorphTargetManagerLocal;

	Delegates = InArgs._Delegates;

	BindCommands();
	
	ChildSlot
	[
		SNew( SVerticalBox )
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(0.f, 2.f))
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(6.f, 0.0))
			[
				SNew(SPositiveActionButton)
				.OnGetMenuContent( this, &SMorphTargetManager::CreateNewMenuWidget )
				.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
			]
			// Filter entry
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SAssignNew( NameFilterBox, SSearchBox )
				.SelectAllTextWhenFocused( true )
				.OnTextChanged( this, &SMorphTargetManager::OnFilterTextChanged )
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(FMargin(6.f, 0.0))
			[
				// A dummy widget to inform user the morph target viewer was replaced with morph target manager
				// and how they can access the default viewer
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.IsChecked_Lambda([](){return ECheckBoxState::Checked;})
				[
					SNew(STextBlock)
					.ToolTipText(LOCTEXT("UsingMorphTargetManagerToolTip", "Deactivate Skeletal Mesh Editing Tools to access default Morph Target Viewer"))
					.Text(LOCTEXT("UsingMorphTargetManagerNote", "Editing Tools Enabled"))
				]
			]
		]

		+ SVerticalBox::Slot()
		.Padding( FMargin( 0.0f, 2.0f, 0.0f, 0.0f ) )
		.FillHeight( 1.0f )		// This is required to make the scrollbar work, as content overflows Slate containers by default
		[
			SAssignNew( ListView , SMorphTargetManagerListType )
			.ListItemsSource( &List)
			.OnGenerateRow(this, &SMorphTargetManager::GenerateMorphTargetRow )
			.OnContextMenuOpening( this, &SMorphTargetManager::OnGetContextMenuContent )
			.HeaderRow
			(
				SNew( SHeaderRow )
				+ SHeaderRow::Column( ColumnId_MorphTargetNameLabel )
				.DefaultLabel( LOCTEXT( "MorphTargetNameLabel", "Morph Target Name" ) )

				+ SHeaderRow::Column( ColumnID_MorphTargetWeightLabel )
				.DefaultLabel( LOCTEXT( "MorphTargetWeightLabel", "Weight" ) )
			)
		]	
	];

	RefreshList();

	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}
}
SMorphTargetManager::~SMorphTargetManager()
{
	if (GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
}

void SMorphTargetManager::PostUndo(bool bSuccess)
{
	RefreshList();
}

void SMorphTargetManager::PostRedo(bool bSuccess)
{
	RefreshList();
}

void SMorphTargetManager::BindCommands()
{
	const FSkeletalMeshModelingToolsCommands& Commands = FSkeletalMeshModelingToolsCommands::Get();
	const FGenericCommands& GenericCommands = FGenericCommands::Get();

	CommandList = MakeShared<FUICommandList>(); 
	CommandList->MapAction(Commands.NewMorphTarget, FSimpleDelegate::CreateSP(this, &SMorphTargetManager::AddMorphTarget) );
	CommandList->MapAction(
		GenericCommands.Rename,
		FExecuteAction::CreateSP(this, &SMorphTargetManager::RenameSelectedMorphTarget), 
		FCanExecuteAction::CreateSP(this, &SMorphTargetManager::CanRename) );
	CommandList->MapAction(
		GenericCommands.Delete,
		FExecuteAction::CreateSP(this, &SMorphTargetManager::RemoveSelectedMorphTargets), 
		FCanExecuteAction::CreateSP(this, &SMorphTargetManager::CanRemove) );
	
	CommandList->MapAction(
		GenericCommands.Duplicate,
		FExecuteAction::CreateSP(this, &SMorphTargetManager::DuplicateSelectedMorphTargets), 
		FCanExecuteAction::CreateSP(this, &SMorphTargetManager::CanDuplicate));
}

void SMorphTargetManager::RefreshList()
{
	using namespace MorphTargetManagerLocal;
	
	List.Reset();

	FText FilterText = GetFilterText();
	
	const TArray<FName>& MorphTargets = Delegates.OnGetMorphTargets.Execute();
	
	for (const FName& MorphTarget : MorphTargets)
	{
		bool bAddToList = true;
		if (!FilterText.IsEmpty())
		{
			FRegexPattern Pattern(FilterText.ToString());
			FRegexMatcher Matcher(Pattern, MorphTarget.ToString());

			bAddToList = Matcher.FindNext();
		}
		
		if (bAddToList)
		{
			TSharedPtr<FMorphTargetInfo> Info = MakeShared<FMorphTargetInfo>();
			Info->Name = MorphTarget;
			Info->Weight = Delegates.OnGetMorphTargetWeight.Execute(MorphTarget);
			List.Add(Info);	
		}
	}

	ListView->RequestListRefresh();
}

void SMorphTargetManager::SelectMorphTargets(const TArray<FName>& MorphTargets)
{
	ListView->ClearSelection();
	
	FMorphTargetInfoPtr First = nullptr; 
	for (const FName& Name : MorphTargets)
	{
		FMorphTargetInfoPtr* Info = List.FindByPredicate([&](const FMorphTargetInfoPtr& InInfo)
		{
			return InInfo->Name == Name;
		});

		if (Info)
		{
			ListView->SetItemSelection(*Info, true);
			if (!First)
			{
				First = *Info;
			}
		}
	}
	
	ListView->RequestNavigateToItem(First);
}

TSharedRef<SWidget> SMorphTargetManager::CreateNewMenuWidget()
{
	const FSkeletalMeshModelingToolsCommands& Commands = FSkeletalMeshModelingToolsCommands::Get();

	static constexpr bool bCloseAfterSelection = true;
	FMenuBuilder MenuBuilder(bCloseAfterSelection, CommandList);
	
	MenuBuilder.BeginSection("NewMorphTarget", LOCTEXT("AddNewMorphTargetOperations", "Morph Targets"));
	MenuBuilder.AddMenuEntry(Commands.NewMorphTarget);
	MenuBuilder.EndSection();
		
	return MenuBuilder.MakeWidget();
}

void SMorphTargetManager::OnFilterTextChanged(const FText& Text)
{
	RefreshList();
}

FText SMorphTargetManager::GetFilterText() const
{
	return NameFilterBox->GetText(); 
}

FText SMorphTargetManager::GetHighlightText(FText InName) const
{
	FText FilterText = GetFilterText();
	if (!FilterText.IsEmpty())
	{
		FRegexPattern Pattern(FilterText.ToString());
		FString Name = InName.ToString();
		FRegexMatcher Matcher(Pattern, Name);
		if (Matcher.FindNext())
		{
			const int Begin = Matcher.GetMatchBeginning();	
			const int End = Matcher.GetMatchEnding();
			const int MatchSize = End - Begin;
			FString Highlight = Name.Mid(Begin, MatchSize);
			return FText::FromString(Highlight);
		}
	}

	return {};
}

TSharedPtr<SWidget> SMorphTargetManager::OnGetContextMenuContent()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder( bShouldCloseWindowAfterMenuSelection, CommandList);

	MenuBuilder.BeginSection("MorphTargetManagerAction", LOCTEXT( "MorphsAction", "Selected Item Actions" ) );

	MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
	MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
	MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);

	MenuBuilder.EndSection();
	return MenuBuilder.MakeWidget();
}

void SMorphTargetManager::SetMorphTargetWeight(FName MorphTarget, float Weight)
{
	FScopedTransaction Transaction(LOCTEXT("OverrideMorphTargetWeight", "Override Morph Target Weight"));
	Delegates.OnSetMorphTargetWeight.Execute(MorphTarget, Weight);
}

float SMorphTargetManager::GetMorphTargetWeight(FName MorphTarget)
{
	return Delegates.OnGetMorphTargetWeight.Execute(MorphTarget);
}

void SMorphTargetManager::SetMorphTargetAutoFill(FName MorphTarget, bool bAutoFill, float PreviousOverrideWeight)
{
	FText Title = bAutoFill ? LOCTEXT("ClearOverride", "Clear Morph Target Override") : LOCTEXT("OverrideMorphTargetWeight", "Override Morph Target Weight");
	FScopedTransaction Transaction(Title);
	Delegates.OnSetMorphTargetAutoFill.Execute(MorphTarget, bAutoFill, PreviousOverrideWeight);
}

bool SMorphTargetManager::GetMorphTargetAutoFill(FName MorphTarget)
{
	return Delegates.OnGetMorphTargetAutoFill.Execute(MorphTarget);
}

void SMorphTargetManager::SetEditingMorphTarget(FName MorphTarget)
{
	FText Title = MorphTarget!=NAME_None ? LOCTEXT("EnabledMorphTargetEditing", "Enable Morph Target for editing") : LOCTEXT("DisabledMorphTargetEditing", "Disabled Morph Target For Editing");
	FScopedTransaction Transaction(Title);
	Delegates.OnSetEditingMorphTarget.Execute(MorphTarget);
}

bool SMorphTargetManager::IsEditingMorphTarget(FName MorphTarget)
{
	return Delegates.OnGetEditingMorphTarget.Execute() == MorphTarget;
}

void SMorphTargetManager::AddMorphTarget()
{
	FScopedTransaction Transaction(LOCTEXT("AddMorphTarget", "Add Morph Target"));
	FName NewMorphTarget = TEXT("NewMorphTarget");
	NewMorphTarget = Delegates.OnAddNewMorphTarget.Execute(NewMorphTarget);

	RefreshList();
	SelectMorphTargets({NewMorphTarget});
}

FName SMorphTargetManager::RenameMorphTarget(FName InOldName, FName InNewName)
{
	FScopedTransaction Transaction(LOCTEXT("RenameMorphTarget", "Rename Morph Target"));
	return Delegates.OnRenameMorphTarget.Execute(InOldName, InNewName);
}

void SMorphTargetManager::RenameSelectedMorphTarget()
{
	TArray<FMorphTargetInfoPtr> Items = ListView->GetSelectedItems();
	Items[0]->EditableText->EnterEditingMode();
}

bool SMorphTargetManager::CanRename()
{
	return ListView->GetSelectedItems().Num() == 1;
}

void SMorphTargetManager::RemoveSelectedMorphTargets()
{
	TArray<FMorphTargetInfoPtr> Items = ListView->GetSelectedItems();

	TArray<FName> Names;
	for (const FMorphTargetInfoPtr& Item : Items)
	{
		Names.Add(Item->Name);
	}

	FText Title = Names.Num() > 1 ? LOCTEXT("RemoveMorph", "Remove Morph Targets") : LOCTEXT("RemoveMorphs", "Remove Morph Targets");
	FScopedTransaction Transaction(Title);
	Delegates.OnRemoveMorphTargets.Execute(Names);
	RefreshList();
}

bool SMorphTargetManager::CanRemove()
{
	return ListView->GetSelectedItems().Num() > 0;
}

void SMorphTargetManager::DuplicateSelectedMorphTargets()
{
	TArray<FMorphTargetInfoPtr> Items = ListView->GetSelectedItems();

	TArray<FName> Names;
	for (const FMorphTargetInfoPtr& Item : Items)
	{
		Names.Add(Item->Name);
	}

	FText Title = Names.Num() > 1 ? LOCTEXT("RemoveMorph", "Remove Morph Targets") : LOCTEXT("RemoveMorphs", "Remove Morph Targets");
	FScopedTransaction Transaction(Title);
	TArray<FName> Duplicated = Delegates.OnDuplicateMorphTargets.Execute(Names);
	RefreshList();
	SelectMorphTargets(Duplicated);
}

bool SMorphTargetManager::CanDuplicate()
{
	return ListView->GetSelectedItems().Num() > 0;
}


TSharedRef<class ITableRow> SMorphTargetManager::GenerateMorphTargetRow(
	FMorphTargetInfoPtr MorphTargetItem,
	const TSharedRef<STableViewBase>& TableViewBase)
{
	TSharedPtr<SMorphTargetManagerListRow> Row =
		SNew(SMorphTargetManagerListRow, TableViewBase)
		.Item( MorphTargetItem )
		.Manager(this)
		.ListView(ListView.Get());
	
	return Row.ToSharedRef();
}

#undef LOCTEXT_NAMESPACE
