// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAttributesList.h"

#include "AttributeDragDrop.h"
#include "Animation/BuiltInAttributeTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "PersonaModule.h"
#include "SPositiveActionButton.h"
#include "ScopedTransaction.h"
#include "SetDragDrop.h"
#include "UAF/AbstractSkeleton/AbstractSkeletonSetBinding.h"
#include "ToolMenus.h"
#include "Widgets/Input/SSearchBox.h"

#define LOCTEXT_NAMESPACE "UE::UAF::Sets::SAttributesList"

using namespace UE::UAF::Sets;

FName SAttributesList::FColumns::SetId = FName("Set");
FName SAttributesList::FColumns::AttributeId = FName("Attribute");

FName SAttributesList::FMenus::AddAttributeId = FName("SAttributesList.AddAttribute");

void SAttributesList::SetSetBinding(TWeakObjectPtr<UAbstractSkeletonSetBinding> InSetBinding)
{
	SetBinding = InSetBinding;
	RepopulateListData();
}

void SAttributesList::Construct(const FArguments& InArgs)
{
	SetBinding = InArgs._SetBinding;
	OnListRefreshed = InArgs._OnListRefreshed;

	RegisterMenus();

	ChildSlot
	[
		SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(1.0f, 1.0f))
			[
				SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(FMargin(1.0f, 1.0f))
					[
						SNew(SPositiveActionButton)
							.Icon(FAppStyle::Get().GetBrush("Icons.Plus"))
							.Text(LOCTEXT("AddAttributeButton_Label", "Add"))
							.OnGetMenuContent( this, &SAttributesList::CreateAddAttributeWidget)
							.IsEnabled_Lambda([this]()
							{
								return SetBinding.IsValid() && SetBinding->GetSetCollection() && SetBinding->GetSkeleton();
							})
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.Padding(FMargin(1.0f, 1.0f))
					[
						SNew(SSearchBox)
							.SelectAllTextWhenFocused(true)
							.OnTextChanged_Lambda([](const FText& InText) { /* TODO: Implement */ })
							.HintText(LOCTEXT("SearchBox_Hint", "Search Sets..."))
					]
			]
			+ SVerticalBox::Slot()
			[
				SAssignNew(ListView, SListView<FListItemPtr>)
					.ListItemsSource(&ListItems)
					.OnGenerateRow(this, &SAttributesList::ListView_OnGenerateRow)
					.HeaderRow(
						SNew(SHeaderRow)
						+ SHeaderRow::Column(FColumns::SetId)
						.DefaultLabel(LOCTEXT("SetColumnLabel", "Set"))
						.FillWidth(0.3f)
						+ SHeaderRow::Column(FColumns::AttributeId)
						.DefaultLabel(LOCTEXT("AttributeColumnLabel", "Attribute"))
						.FillWidth(0.7f)
					)
			]
	];

	RepopulateListData();
}

void SAttributesList::RepopulateListData()
{
	if (!bRepopulating)
	{
		TGuardValue<bool> RecursionGuard(bRepopulating, true);

		ListItems.Empty();

		if (SetBinding.IsValid())
		{
			for (const FAbstractSkeleton_AttributeBinding& Binding : SetBinding->GetAttributeBindings())
			{
				FListItemPtr BindingItem = MakeShared<FListItem>(Binding.SetName, Binding.Attribute);
				ListItems.Add(BindingItem);
			}
		}

		ListView->RequestListRefresh();

		OnListRefreshed.ExecuteIfBound();
	}
}

class SAttributesListRow : public SMultiColumnTableRow<SAttributesList::FListItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SAttributesListRow) {}
		SLATE_ARGUMENT(SAttributesList::FListItemPtr, Item)
		SLATE_EVENT(FOnCanAcceptDrop, OnCanAcceptDrop)
		SLATE_EVENT(FOnAcceptDrop, OnAcceptDrop)
		SLATE_EVENT(FOnDragDetected, OnDragDetected)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView)
	{
		Item = InArgs._Item;

		const FSuperRowType::FArguments Args = FSuperRowType::FArguments()
			.OnDragDetected(InArgs._OnDragDetected)
			.OnCanAcceptDrop(InArgs._OnCanAcceptDrop)
			.OnAcceptDrop(InArgs._OnAcceptDrop)
			.Style(FAppStyle::Get(), "TableView.AlternatingRow");

		SMultiColumnTableRow<SAttributesList::FListItemPtr>::Construct(Args, OwnerTableView);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == SAttributesList::FColumns::SetId)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6, 0)
				[
					SNew(SBox)
						.WidthOverride(8.0f)
						.HeightOverride(8.0f)
						[
							SNew(SImage)
								.Image(FAppStyle::Get().GetBrush("Icons.FilledCircle"))
								.ColorAndOpacity((Item->SetName != NAME_None) ? FLinearColor(FColor(31, 228, 75)) : FLinearColor(FColor(239, 53, 53)))
						]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2, 2)
				[
					SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("ClassIcon.GroupActor"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2, 2)
				[
					SNew(STextBlock)
						.Text(FText::FromName(Item->SetName))
				];
		}
		else if (ColumnName == SAttributesList::FColumns::AttributeId)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2, 2)
				[
					SNew(SImage)
						.Image(FAppStyle::Get().GetBrush("AnimGraph.Attribute.Attributes.Icon"))
						.ColorAndOpacity((Item->SetName != NAME_None) ? FLinearColor(FColor(31, 228, 75)) : FSlateColor::UseForeground())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2, 2)
				[
					SNew(STextBlock)
						.Text(FText::FromName(Item->Attribute.GetName()))
				];
		}
		else 
		{
			return SNullWidget::NullWidget;
		}
	}

private:
	SAttributesList::FListItemPtr Item;
};

SAttributesList::FListItem::FListItem(const FName InSetName, const FAnimationAttributeIdentifier& InAttribute)
	: Attribute(InAttribute)
	, SetName(InSetName)
{
}

TSharedRef<ITableRow> SAttributesList::ListView_OnGenerateRow(FListItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SAttributesListRow, OwnerTable)
		.Item(Item)
		.OnCanAcceptDrop_Lambda([](const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, FListItemPtr TargetTreeItem) -> TOptional<EItemDropZone>
			{
				TOptional<EItemDropZone> ReturnedDropZone;

				const TSharedPtr<FSetDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FSetDragDropOp>();
				if (DragDropOp.IsValid())
				{
					ReturnedDropZone = EItemDropZone::OntoItem;
				}

				return ReturnedDropZone;
			})
		.OnAcceptDrop_Lambda([this](const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, FListItemPtr TargetTreeItem) -> FReply
			{
				const TSharedPtr<FSetDragDropOp> DragDropOp = DragDropEvent.GetOperationAs<FSetDragDropOp>();

				if (DragDropOp.IsValid())
				{
					const FScopedTransaction Transaction(LOCTEXT("AddAttributeToSet", "Add Attribute to Set"));
					SetBinding->Modify();

					SetBinding->AddAttributeToSet(TargetTreeItem->Attribute, DragDropOp->Item->Set.SetName);
					
					RepopulateListData();

					return FReply::Handled();
				}

				return FReply::Unhandled();
			})
		.OnDragDetected_Lambda([this](const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
			{
				if (MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton))
				{
					TArray<FAnimationAttributeIdentifier> SelectedAttributes;
					SelectedAttributes.Reserve(ListView->GetNumItemsSelected());
					
					for (const FListItemPtr& Selected : ListView->GetSelectedItems())
					{
						SelectedAttributes.Add(Selected->Attribute);
					}
					
					const TSharedRef<FAttributeDragDropOp> DragDropOp = FAttributeDragDropOp::New(MoveTemp(SelectedAttributes));
					return FReply::Handled().BeginDragDrop(DragDropOp);
				}

				return FReply::Unhandled();
			});
}

void SAttributesList::RegisterMenus()
{
	if (UToolMenus::Get()->IsMenuRegistered(FMenus::AddAttributeId))
	{
		return;
	}

	FToolMenuOwnerScoped OwnerScoped(this);
	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu(FMenus::AddAttributeId);

	UToolMenu* AddCurveMenu = Menu->AddSubMenu(FToolMenuOwner(), "AddCurve", "AddCurve", LOCTEXT("AddCurve", "Add Curve"));
	UToolMenu* AddAttributeMenu = Menu->AddSubMenu(FToolMenuOwner(), "AddAttribute", "AddAttribute", LOCTEXT("AddAttribute", "Add Attribute"));

	{
		FToolMenuSection& AttributeWidgetSection = AddAttributeMenu->AddSection("WidgetSection");

		AttributeWidgetSection.AddDynamicEntry(
			"AttributePicker",
			FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& Section)
			{
				if (const UAttributesListMenuContext* Context = Section.FindContext<UAttributesListMenuContext>())
				{
					FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");

					TSharedPtr<SAttributesList> SetBindingWidget = Context->SetBindingWidget.Pin();
					ensure(SetBindingWidget);
					
					const TWeakObjectPtr<UAbstractSkeletonSetBinding> SetBinding = SetBindingWidget->SetBinding;
					
					if (SetBinding.IsValid())
					{	
						FToolMenuEntry WidgetEntry = FToolMenuEntry::InitWidget(
							"AttributePicker",
							PersonaModule.CreateAttributePicker(
								SetBinding->GetSkeleton(),
								FOnAttributesPicked::CreateLambda([SetBinding, WeakSetBindingWidget = Context->SetBindingWidget](const TConstArrayView<FAnimationAttributeIdentifier> InSelectedAttributes)
									{
										const FScopedTransaction Transaction(LOCTEXT("AddAttributeToSet", "Add Attribute to Set"));
										SetBinding->Modify();

										for (const FAnimationAttributeIdentifier& Attribute : InSelectedAttributes)
										{
											SetBinding->AddAttributeToSet(Attribute, NAME_None);
										}

										FSlateApplication::Get().DismissAllMenus();
										WeakSetBindingWidget.Pin()->RepopulateListData();
									}),
								true,
								false),
							FText(),
							true,
							false,
							true
						);

						Section.AddEntry(WidgetEntry);
					}
				}
			}));
	}

	{
		FToolMenuSection& CurveWidgetSection = AddCurveMenu->AddSection("WidgetSection");

		CurveWidgetSection.AddDynamicEntry(
			"CurvePicker",
			FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& Section)
			{
				if (const UAttributesListMenuContext* Context = Section.FindContext<UAttributesListMenuContext>())
				{
					FPersonaModule& PersonaModule = FModuleManager::LoadModuleChecked<FPersonaModule>("Persona");

					TSharedPtr<SAttributesList> SetBindingWidget = Context->SetBindingWidget.Pin();
					ensure(SetBindingWidget);
					
					const TWeakObjectPtr<UAbstractSkeletonSetBinding> SetBinding = SetBindingWidget->SetBinding;

					if (SetBinding.IsValid())
					{
						FToolMenuEntry WidgetEntry = FToolMenuEntry::InitWidget(
							"CurvePicker",
							PersonaModule.CreateCurvePicker(
								SetBinding->GetSkeleton(),
								FOnCurvePicked::CreateLambda([SetBinding, SetBindingWidget = Context->SetBindingWidget](const FName& InCurve)
									{
										const FScopedTransaction Transaction(LOCTEXT("AddCurvesToSet", "Add Curve to Set"));
										SetBinding->Modify();

										FAnimationAttributeIdentifier CurveAttrId(InCurve, INDEX_NONE, NAME_None, FFloatAnimationAttribute::StaticStruct());
										SetBinding->AddAttributeToSet(CurveAttrId, NAME_None);

										FSlateApplication::Get().DismissAllMenus();
										SetBindingWidget.Pin()->RepopulateListData();
									})),
							FText(),
							true,
							false,
							true
						);

						Section.AddEntry(WidgetEntry);
					}
				}
			}));
	}
}

TSharedRef<SWidget> SAttributesList::CreateAddAttributeWidget()
{
	UAttributesListMenuContext* ListContext = NewObject<UAttributesListMenuContext>();
	ListContext->SetBindingWidget = SharedThis(this);
	
	FToolMenuContext MenuContext;
	MenuContext.AddObject(ListContext);

	return UToolMenus::Get()->GenerateWidget(FMenus::AddAttributeId, MenuContext);
}

#undef LOCTEXT_NAMESPACE