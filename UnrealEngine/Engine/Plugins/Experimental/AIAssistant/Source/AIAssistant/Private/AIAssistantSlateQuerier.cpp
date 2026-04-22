// Copyright Epic Games, Inc. All Rights Reserved.
	

#include "AIAssistantSlateQuerier.h"

#include "EditorModes.h"
#include "Containers/Set.h"
#include "Containers/ArrayView.h"
#include "Internationalization/Regex.h"
#include "LevelEditorSubsystem.h"
#include "LevelEditorViewport.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "EditorModeManager.h"
#include "EngineUtils.h"
#include "IDetailsView.h"
#include "Interfaces/IMainFrameModule.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/Culture.h"
#include "Serialization/JsonSerializable.h"
#include "Serialization/JsonSerializerMacros.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Toolkits/BaseToolkit.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Docking/SDockTab.h"
#include "Filters/SFilterSearchBox.h"
#include "SGraphNode.h"
#include "Framework/Application/SlateApplication.h"

#include "AIAssistant.h"
#include "AIAssistantLog.h"
#include "AIAssistantSubsystem.h"
#include "AIAssistantWebBrowser.h"


#define LOCTEXT_NAMESPACE "FAIAssistantSlateQuerier"

struct FAIAssistantSlateQueryStructuredContext : public FJsonSerializable
{
	FString TopWindowName;
	FString UnrealEditorVersion;
	FString UnrealEditorLanguage;
	FString UnrealEditorLocale;
	TOptional<FString> WindowName;
	TOptional<FString> EditorName;
	TOptional<FString> EditorMode;
	TOptional<FString> ActiveToolName;
	TOptional<FString> TabName;
	TOptional<FString> ToolTipText;
	TOptional<FString> TextUnderCursor;
	TOptional<FString> WidgetType;
	TOptional<FString> ActorClass;
	TOptional<FString> ItemName;
	TOptional<FString> ItemDescriptor;
	TOptional<FString> CategoryName;
	TOptional<TArray<FString>> WidgetPath;

	BEGIN_JSON_SERIALIZER
		JSON_SERIALIZE("toplevel_window_name", TopWindowName);
		JSON_SERIALIZE("unreal_editor_version", UnrealEditorVersion);
		JSON_SERIALIZE("unreal_editor_language", UnrealEditorLanguage);
		JSON_SERIALIZE("unreal_editor_locale", UnrealEditorLocale);
		JSON_SERIALIZE_OPTIONAL("window_name", WindowName);
		JSON_SERIALIZE_OPTIONAL("editor_name", EditorName);
		JSON_SERIALIZE_OPTIONAL("editor_mode", EditorMode);
		JSON_SERIALIZE_OPTIONAL("active_tool", ActiveToolName);
		JSON_SERIALIZE_OPTIONAL("tab_name", TabName);
		JSON_SERIALIZE_OPTIONAL("tooltip_text", ToolTipText);
		JSON_SERIALIZE_OPTIONAL("text_under_cursor", TextUnderCursor);
		JSON_SERIALIZE_OPTIONAL("widget_type", WidgetType);
		JSON_SERIALIZE_OPTIONAL("actor_class", ActorClass);
		JSON_SERIALIZE_OPTIONAL("item_name", ItemName);
		JSON_SERIALIZE_OPTIONAL("item_descriptor", ItemDescriptor);
		JSON_SERIALIZE_OPTIONAL("category_name", CategoryName);
		JSON_SERIALIZE_OPTIONAL_ARRAY("widget_path", WidgetPath);
	END_JSON_SERIALIZER
};


//
// FAIAssistantSlateQueryContext
//
// Used for accumulating UI context information, and ultimately building strings for the query.
//


struct FAIAssistantSlateQueryContext
{
	TSharedPtr<SWidget> LastPickedWidget;
	FText CurrentToolTipText = FText();
	FText FormattedWindowName = FText();
	FText FormattedTabName = FText();
	FText TextUnderCursor = FText();
	FText ItemName = FText();
	FText ItemDescriptor = FText();
	FString SelectedActorName = FString();
	FString SelectedActorClass = FString();
	bool bIsUIWidget = false;
	bool bIsObject = false;
	bool bIsInAssistantPanel = false;

	FText GeneratedQuery = FText();
	FText GeneratedQueryInstructions = FText();
	FText LanguageInstructions = FText();

	FAIAssistantSlateQueryStructuredContext StructuredContext;
};


//
// Statics
//

// Search backwards from end of WidgetPathToTest and return first widget that matches any type in WidgetTypes.
// Return null ptr if not found.
static TSharedPtr<SWidget> FindClosestWidgetPtrOfTypes(const FWidgetPath& WidgetPathToTest, const TSet<FName> WidgetTypes)
{
	TSharedPtr<SWidget> ClosestWidget;
	for (int32 WidgetIndex = WidgetPathToTest.Widgets.Num() - 1; WidgetIndex >= 0; --WidgetIndex)
	{
		const FArrangedWidget* ArrangedWidget = &WidgetPathToTest.Widgets[WidgetIndex];
		const TSharedRef<SWidget>& ThisWidget = ArrangedWidget->Widget;
		if (WidgetTypes.Contains(ThisWidget->GetType()))
		{
			ClosestWidget = ThisWidget.ToSharedPtr();
			break;
		}
	}
	return ClosestWidget;
}

// Search backwards from end of WidgetPathToTest and return first widget that matches WidgetType.  Return null
// ptr if not found.
static TSharedPtr<SWidget> FindClosestWidgetPtrOfType(const FWidgetPath& WidgetPathToTest, const FName& WidgetType)
{
	const TSet<FName> WidgetTypes = TSet<FName>({WidgetType});
	return FindClosestWidgetPtrOfTypes(WidgetPathToTest, WidgetTypes);
}

// Search backwards from end of WidgetPathToTest and return first widget that matches a type in WidgetTypes,
// testing each type in order.  Return null ptr if not found.
static TSharedPtr<SWidget> FindClosestWidgetPtrOfSortedTypes(
	const FWidgetPath& WidgetPathToTest, const TConstArrayView<FName>& WidgetTypes)
{
	for (FName WidgetType : WidgetTypes)
	{
		TSharedPtr<SWidget> ClosestWidget = FindClosestWidgetPtrOfType(WidgetPathToTest, WidgetType);
		if (ClosestWidget.IsValid())
		{
			return ClosestWidget;
		}
	}

	return nullptr;
}

// Search forward from start of WidgetPathToTest and return first widget that matches WidgetType.  Return null
// ptr if not found.
static TSharedPtr<SWidget> FindFirstWidgetPtrOfType(const FWidgetPath& WidgetPathToTest, const FName& WidgetType)
{
	TSharedPtr<SWidget> FirstWidget;
	for (int32 WidgetIndex = 0; WidgetIndex < WidgetPathToTest.Widgets.Num(); WidgetIndex++)
	{
		const FArrangedWidget* ArrangedWidget = &WidgetPathToTest.Widgets[WidgetIndex];
		const TSharedRef<SWidget>& ThisWidget = ArrangedWidget->Widget;
		if (ThisWidget->GetType() == WidgetType)
		{
			FirstWidget = ThisWidget.ToSharedPtr();
			break;
		}
	}
	return FirstWidget;
}

// Search children of WidgetToTest for any widgets that match WidgetType; return all found widgets in OutWidgets.
static void FindChildWidgetsOfType(TArray<TSharedRef<SWidget>>& OutWidgets, TSharedPtr<SWidget> WidgetToTest, const FName& WidgetType)
{
	if (WidgetToTest.IsValid())
	{
		for (int32 ChildIdx = 0; ChildIdx < WidgetToTest->GetChildren()->Num(); ChildIdx++)
		{
			TSharedRef<SWidget> ThisWidget = WidgetToTest->GetChildren()->GetChildAt(ChildIdx);
			if (ThisWidget->GetType() == WidgetType)
			{
				OutWidgets.Add(ThisWidget);
			}
			FindChildWidgetsOfType(OutWidgets, ThisWidget.ToSharedPtr(), WidgetType);
		}
	}
}

// Search children of WidgetToTest and return first widget that matches WidgetType.  Return null ptr if not found.
static TSharedPtr<SWidget> FindChildWidgetOfType(const TSharedPtr<SWidget> WidgetToTest, const FName& WidgetType)
{
	if (WidgetToTest.IsValid())
	{
		for (int32 ChildIdx = 0; ChildIdx < WidgetToTest->GetChildren()->Num(); ChildIdx++)
		{
			TSharedRef<SWidget> ThisWidget = WidgetToTest->GetChildren()->GetChildAt(ChildIdx);
			if (ThisWidget->GetType() == WidgetType)
			{
				return ThisWidget.ToSharedPtr();
			}
			else
			{
				TSharedPtr<SWidget> ChildWidget = FindChildWidgetOfType(ThisWidget.ToSharedPtr(), WidgetType);
				if (ChildWidget.IsValid() && ChildWidget->GetType() == WidgetType)
				{
					return ChildWidget;
				}
			}
		}
	}
	return TSharedPtr<SWidget>();
}

// Search children of WidgetToTest and return text from first one found with alpha text.  Return empty FText
// if not found.
static FText FindChildWidgetWithText(const TSharedPtr<SWidget> WidgetToTest)
{
	// get all children and see if any are text widgets.
	// refuse any that only contain numbers (they're probably just values)
	const FRegexPattern AlphaPattern(TEXT("[A-Za-z]+"));
	if (WidgetToTest.IsValid())
	{
		if (WidgetToTest->GetType() == "STextBlock")
		{
			TSharedRef<STextBlock> TextBlock = StaticCastSharedRef<STextBlock>(WidgetToTest.ToSharedRef());
			const FText WidgetText = TextBlock->GetText();
			if (!WidgetText.IsEmpty())
			{
				FRegexMatcher AlphaMatcher(AlphaPattern, WidgetText.ToString());
				if (AlphaMatcher.FindNext())
				{
					return WidgetText;
				}
			}
		}
		for (int32 ChildIdx = 0; ChildIdx < WidgetToTest->GetChildren()->Num(); ChildIdx++)
		{
			TSharedRef<SWidget> ThisWidget = WidgetToTest->GetChildren()->GetChildAt(ChildIdx);
			const FText ChildText = FindChildWidgetWithText(ThisWidget.ToSharedPtr());
			if (!ChildText.IsEmpty())
			{
				return ChildText;
			}
		}
	}
	
	return FText();
}

// Return first active tooltip text found.  Return empty FText if not found.
static FText FindCurrentToolTipText()
{
	for (const TArray<TSharedRef<SWindow>> AllWindows = FSlateApplication::Get().GetTopLevelWindows();
		const TSharedRef<SWindow>& ThisWindow : AllWindows)
	{
		if (ThisWindow->GetType() == EWindowType::ToolTip && ThisWindow->IsVisible())
		{
			return FindChildWidgetWithText(ThisWindow);
		}
	}
	
	return FText();
}

// Search backwards from end of WidgetPath and return first SMenuEntryBlock or SWidgetBlock widget found.
// Return null ptr if not found.
static TSharedPtr<SWidget> FindClosestMenuItem(const FWidgetPath& WidgetPath)
{
	TSharedPtr<SWidget> MenuItemBlock;
	if (WidgetPath.IsValid())
	{
		// is it a menu item? Look up for an SMenuEntryBlock or SWidgetBlock.
		static const TStaticArray<FName, 2> MenuTypes = {"SMenuEntryBlock", "SWidgetBlock"};
		MenuItemBlock = FindClosestWidgetPtrOfSortedTypes(WidgetPath, MenuTypes);

		// disabled menu items *contain* the menu entry block
		if (!MenuItemBlock.IsValid())
		{
			if (WidgetPath.GetLastWidget()->GetChildren()->Num() > 0)
			{
				for (int32 ChildIdx = 0; ChildIdx < WidgetPath.GetLastWidget()->GetChildren()->Num(); ChildIdx++)
				{
					TSharedRef<SWidget> ThisWidget = WidgetPath.GetLastWidget()->GetChildren()->GetChildAt(ChildIdx);
					for (FName MenuType : MenuTypes)
					{
						if (ThisWidget->GetType() == MenuType)
						{
							MenuItemBlock = ThisWidget.ToSharedPtr();
							break;
						}
					}
				}
			}
		}
	}

	return MenuItemBlock;
}

// For menu items, we want to generate the WidgetPath context based on the root widget that spawned the menu,
// not on the actual menu item itself.
static FWidgetPath FindContextWidgetPath(const FWidgetPath& WidgetPath)
{
	const TSharedPtr<SWidget> MenuItem = FindClosestMenuItem(WidgetPath);
	if (MenuItem.IsValid())
	{
		TSharedPtr<SWidget> RootWidget = FSlateApplication::Get().GetMenuHostWidget();
		if (RootWidget.IsValid())
		{
			FWidgetPath OutWidgetPath;
			FSlateApplication::Get().GeneratePathToWidgetChecked(RootWidget.ToSharedRef(), OutWidgetPath);
			return OutWidgetPath;
		}
	}
	return WidgetPath;
}

// Return true and fill in SlateQueryContext if queried item is in viewport.
static bool ItemIsInViewport(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	TSharedPtr<SWidget> Viewport = FindClosestWidgetPtrOfType(WidgetPath, "SViewport");
	if (Viewport.IsValid())
	{
		// Web browser widgets also have Viewports!
		// Ignore web browser views except for the special case of AI Assistant window or panel.
		TSharedPtr<SWidget> WebBrowser = Viewport->GetParentWidget();
		if (WebBrowser.IsValid() && WebBrowser->GetType() == "SWebBrowserView")
		{
			TSharedPtr<SWidget> AssistantPane = FindClosestWidgetPtrOfType(WidgetPath, "SAIAssistantWebBrowser");
			if (AssistantPane.IsValid())
			{
				SlateQueryContext.bIsInAssistantPanel = true;
				SlateQueryContext.LastPickedWidget = AssistantPane;
				return true;
			}
		}
		else
		{
			// Selected actors are filled in by the IsInEditor function.
			if (!SlateQueryContext.SelectedActorName.IsEmpty())
			{
				SlateQueryContext.bIsObject = true;
				SlateQueryContext.LastPickedWidget = Viewport;
				SlateQueryContext.ItemName = FText::FromString(SlateQueryContext.SelectedActorName);
				SlateQueryContext.ItemDescriptor = LOCTEXT("ItemDescriptor_SelectedActor", "actor");
				SlateQueryContext.StructuredContext.ActorClass = SlateQueryContext.SelectedActorClass;
				return true;
			}
		}
	}
	return false;
}

// Return true and fill in SlateQueryContext if queried item is a graph node.
static bool ItemIsGraphNode(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	// Look up for an SGraphEditor and then down for an SGraphPanel. Whatever is the child of that SGraphPanel is our SGraphNode.
	// Note that this rests on the assumption that SGraphPanel only contains children that are SGraphNodes. This assumption is also
	// made in the code of SGraphPanel itself.
	TSharedPtr<SGraphEditor> GraphEditor;
	for (int32 WidgetIndex = WidgetPath.Widgets.Num() - 1; WidgetIndex >= 0; --WidgetIndex)
	{
		const FArrangedWidget* ArrangedWidget = &WidgetPath.Widgets[WidgetIndex];
		const TSharedRef<SWidget>& ThisWidget = ArrangedWidget->Widget;
		if (ThisWidget->GetType() == "SGraphEditor")
		{
			for (int32 EditorDescendantIndex = WidgetIndex; EditorDescendantIndex < WidgetPath.Widgets.Num(); EditorDescendantIndex++)
			{
				const FArrangedWidget* DescendantArrangedWidget = &WidgetPath.Widgets[EditorDescendantIndex];
				const TSharedRef<SWidget>& ThisDescendantWidget = DescendantArrangedWidget->Widget;
				if (ThisDescendantWidget->GetType() == "SGraphPanel" && (EditorDescendantIndex + 1 < WidgetPath.Widgets.Num()))
				{
					const FArrangedWidget* NodeArrangedWidget = &WidgetPath.Widgets[EditorDescendantIndex+1];
					const TSharedRef<SWidget>& ThisNodeWidget = NodeArrangedWidget->Widget;
					const TSharedPtr<SGraphNode> AsGraphNode = StaticCastSharedPtr<SGraphNode>(ThisNodeWidget.ToSharedPtr());
					SlateQueryContext.LastPickedWidget = ThisNodeWidget.ToSharedPtr();
					SlateQueryContext.ItemName = AsGraphNode->GetNodeObj()->GetNodeTitle(ENodeTitleType::MenuTitle);
					SlateQueryContext.ItemDescriptor = LOCTEXT("ItemDescriptor_GraphNode", "graph node");
					return true;
				}
			}
		}
	}
	return false;
}

// Fill query context structure for buttons.
static bool FillButtonQueryContext(const TSharedPtr<SWidget> Button,
	FAIAssistantSlateQueryContext& SlateQueryContext, FText ItemDescriptor)
{
	FText ChildText = FindChildWidgetWithText(Button);
	if (!ChildText.IsEmpty() || !SlateQueryContext.CurrentToolTipText.IsEmpty())
	{
		if (!ChildText.IsEmpty())
		{
			SlateQueryContext.ItemName = ChildText;
		}
		else
		{
			// The tool tip can be used in this case.
			SlateQueryContext.ItemName = SlateQueryContext.CurrentToolTipText;
		}
		SlateQueryContext.ItemDescriptor = ItemDescriptor;
		SlateQueryContext.LastPickedWidget = Button;
		SlateQueryContext.bIsUIWidget = true;
		return true;
	}
	return false;
}

// Return true and fill in SlateQueryContext if queried item is a button type as specified in
// ButtonWidgetTypes.
static bool ItemIsButtonType(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext,
	const TSet<FName> ButtonWidgetTypes, FText ItemDescriptor)
{
	TSharedPtr<SWidget> Button = FindClosestWidgetPtrOfTypes(WidgetPath, ButtonWidgetTypes);

	// Disabled buttons *contain* the menu entry block.
	if (!Button.IsValid() && WidgetPath.GetLastWidget()->GetChildren()->Num() > 0)
	{
		for (int32 ChildIdx = 0; ChildIdx < WidgetPath.GetLastWidget()->GetChildren()->Num(); ChildIdx++)
		{
			TSharedRef<SWidget> ThisWidget = WidgetPath.GetLastWidget()->GetChildren()->GetChildAt(ChildIdx);
			if (ButtonWidgetTypes.Contains(ThisWidget->GetType()))
			{
				Button = ThisWidget.ToSharedPtr();
			}
		}
	}

	return Button.IsValid() && FillButtonQueryContext(Button, SlateQueryContext, ItemDescriptor);
}

// Return true and fill in SlateQueryContext if queried item is a button.
static bool ItemIsButton(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	static const TSet<FName> ToolbarButtonTypes = TSet<FName>({
		"SToolBarButtonBlock", "SToolBarComboButtonBlock"
		});

	static const TSet<FName> SimpleButtonTypes = TSet<FName>({
			"SButton",
			"SPrimaryButton",
			"SCheckBox"
		});

	return (ItemIsButtonType(WidgetPath, SlateQueryContext, ToolbarButtonTypes,
				LOCTEXT("ItemDescriptor_ToolbarButton", "toolbar button")) ||
			ItemIsButtonType(WidgetPath, SlateQueryContext, SimpleButtonTypes,
				LOCTEXT("ItemDescriptor_Button", "button")));
}

// Return true and fill in SlateQueryContext if queried item is a search box.
static bool ItemIsSearchBox(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	static const TSet<FName> SearchBoxTypes = TSet<FName>({"SSearchBox", "SFilterSearchBox"});
	TSharedPtr<SWidget> SearchBox = FindClosestWidgetPtrOfTypes(WidgetPath, SearchBoxTypes);

	if (SearchBox.IsValid())
	{
		FText HintText;
		if (SearchBox->GetType() == "SSearchBox")
		{
			TSharedPtr<SSearchBox> ThisSearchBoxCast = StaticCastSharedPtr<SSearchBox>(SearchBox);
			HintText = ThisSearchBoxCast->GetHintText();
		}
		else
		{
			TSharedPtr<SWidget> EditableText = FindChildWidgetOfType(SearchBox, "SEditableText");
			if (EditableText)
			{
				TSharedPtr<SEditableText> ThisEditableTextCast = StaticCastSharedPtr<SEditableText>(EditableText);
				HintText = ThisEditableTextCast->GetHintText();
			}
		}
		SlateQueryContext.ItemName = FText::Format(LOCTEXT("SearchBoxFormat", "{0}"),
			(HintText.IsEmpty()) ? LOCTEXT("SearchBoxDefaultName", "default") : HintText);
		SlateQueryContext.ItemDescriptor = LOCTEXT("ItemDescriptor_SearchBox", "search box");
		SlateQueryContext.LastPickedWidget = SearchBox;
		SlateQueryContext.bIsUIWidget = true;
		return true;
	}

	return false;
}

// Return true and fill in SlateQueryContext if queried item is a console input box.
static bool ItemIsConsoleInputBox(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	TSharedPtr<SWidget> ConsoleInputBox = FindClosestWidgetPtrOfType(WidgetPath, "SConsoleInputBox");
	if (ConsoleInputBox.IsValid())
	{
		FText HintText;
		TSharedPtr<SWidget> EditableText = FindChildWidgetOfType(ConsoleInputBox, "SMultiLineEditableTextBox");
		if (EditableText.IsValid())
		{
			TSharedPtr<SMultiLineEditableTextBox> ThisMultiLineEditableTextBoxCast = StaticCastSharedPtr<SMultiLineEditableTextBox>(EditableText);
			HintText = ThisMultiLineEditableTextBoxCast->GetHintText();
		}
		SlateQueryContext.ItemName = FText::Format(LOCTEXT("ConsoleInputBoxFormat", "{0}"),
			(HintText.IsEmpty() ? LOCTEXT("ConsoleInputBoxDefaultName","Console") : HintText));
		SlateQueryContext.ItemDescriptor = LOCTEXT("ItemDescriptor_ConsoleInputBox", "input box");
		SlateQueryContext.LastPickedWidget = ConsoleInputBox;
		SlateQueryContext.bIsUIWidget = true;
		return true;
	}
	return false;
}

// Return true and fill in SlateQueryContext if queried item is a menu item.
static bool ItemIsMenuItem(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	// Look up for an SMenuEntryBlock or SWidgetBlock and then down for the text.
	TSharedPtr<SWidget> MenuItemBlock = FindClosestMenuItem(WidgetPath);
	if (MenuItemBlock.IsValid())
	{
		FText ChildText = FindChildWidgetWithText(MenuItemBlock);
		if (!ChildText.IsEmpty())
		{
			SlateQueryContext.ItemName = ChildText;
			SlateQueryContext.ItemDescriptor = LOCTEXT("ItemDescriptor_Menu", "menu item");
			SlateQueryContext.LastPickedWidget = MenuItemBlock;
			SlateQueryContext.bIsUIWidget = true;
			return true;
		}
	}
	return false;
}

// Return true and fill in SlateQueryContext if queried item is a Details panel property.
static bool ItemIsDetailsProperty(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	// Look up for an SDetailSingleItemRow [could be others?] then down through the SPropertyNameWidget.
	TSharedPtr<SWidget> PropertyRow = FindClosestWidgetPtrOfType(WidgetPath, "SDetailSingleItemRow");
	if (PropertyRow.IsValid())
	{
		FText ChildText;

		SlateQueryContext.LastPickedWidget = PropertyRow;

		TSharedPtr<SWidget> PropertyNameWidget = FindChildWidgetOfType(PropertyRow, "SPropertyNameWidget");
		if (PropertyNameWidget.IsValid())
		{
			ChildText = FindChildWidgetWithText(PropertyNameWidget);
			if (!ChildText.IsEmpty())
			{
				SlateQueryContext.ItemName = ChildText;
				SlateQueryContext.ItemDescriptor = LOCTEXT("ItemDescriptor_Property", "setting");
			}
		}
		else
		{
			// Some rows e.g. Transform have no SPropertyNameWidget. Best we can do is look for a text label.
			ChildText = FindChildWidgetWithText(PropertyRow);
			if (!ChildText.IsEmpty())
			{
				SlateQueryContext.ItemName = ChildText;
				SlateQueryContext.ItemDescriptor = LOCTEXT("ItemDescriptor_Property", "setting");
			}
		}

		// Find section name from sibling SDetailCategoryTableRow.
		if (PropertyRow->IsParentValid())
		{
			TSharedPtr<SWidget> ListPanel = PropertyRow->GetParentWidget();
			TSharedRef<SWidget> PreviousDetailCategory = SNullWidget::NullWidget;
			for (int32 ChildIdx = 0; ChildIdx < ListPanel->GetChildren()->Num(); ChildIdx++)
			{
				TSharedRef<SWidget> ThisWidget = ListPanel->GetChildren()->GetChildAt(ChildIdx);
				if (ThisWidget == PropertyRow)
				{
					break;
				}
				if (ThisWidget->GetType() == "SDetailCategoryTableRow")
				{
					PreviousDetailCategory = ThisWidget;
				}
			}
			if (PreviousDetailCategory->GetType() != "SNullWidgetContent")
			{
				ChildText = FindChildWidgetWithText(PreviousDetailCategory);
				if (!ChildText.IsEmpty())
				{
					SlateQueryContext.StructuredContext.CategoryName = ChildText.ToString();
					SlateQueryContext.ItemDescriptor = FText::Format(
						LOCTEXT("ItemDescriptor_PropertyWithCategory", "setting in the {0} category"), ChildText);
				}
			}
		}
		return true;
	}
	return false;
}

// Return true and fill in SlateQueryContext if queried item is a plugin in Plugins browser.
static bool ItemIsPlugin(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	// Look up for containing SPluginBrowser.
	TSharedPtr<SWidget> PluginBrowser = FindClosestWidgetPtrOfType(WidgetPath, "SPluginBrowser");
	if (PluginBrowser.IsValid())
	{
		FText ChildText;
		TSharedPtr<SWidget> PluginCategoryTree = FindClosestWidgetPtrOfType(WidgetPath, "SPluginCategoryTree");
		TSharedPtr<SWidget> PluginTileList = FindClosestWidgetPtrOfType(WidgetPath, "SPluginTileList");
		if (PluginCategoryTree.IsValid())
		{
			SlateQueryContext.LastPickedWidget = PluginCategoryTree;
			TSharedPtr<SWidget> PluginCategory = FindClosestWidgetPtrOfType(WidgetPath, "SPluginCategory");
			if (PluginCategory.IsValid())
			{
				SlateQueryContext.LastPickedWidget = PluginCategory;
				ChildText = FindChildWidgetWithText(PluginCategory);
				if (!ChildText.IsEmpty())
				{
					SlateQueryContext.ItemName = ChildText;
					SlateQueryContext.ItemDescriptor = LOCTEXT("ItemDescriptor_PluginCategory", "plugin category");
					SlateQueryContext.bIsObject = true;
					return true;
				}
			}
		}
		else if (PluginTileList.IsValid())
		{
			SlateQueryContext.LastPickedWidget = PluginTileList;
			TSharedPtr<SWidget> PluginTile = FindClosestWidgetPtrOfType(WidgetPath, "SPluginTile");
			if (PluginTile.IsValid())
			{
				SlateQueryContext.LastPickedWidget = PluginTile;
				ChildText = FindChildWidgetWithText(PluginTile);
				if (!ChildText.IsEmpty())
				{
					SlateQueryContext.ItemName = ChildText;
					SlateQueryContext.ItemDescriptor = LOCTEXT("ItemDescriptor_PluginTile", "plugin");
					SlateQueryContext.bIsObject = true;
					return true;
				}
			}
		}
	}
	return false;
}

// Return true and fill in SlateQueryContext if queried item is a navigation breadcrumb button.
static bool ItemIsBreadcrumbTrailButton(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	TSharedPtr<SWidget> BreadcrumbTrail = FindClosestWidgetPtrOfType(WidgetPath, "SBreadcrumbTrail<FNavigationCrumb>");
	TSharedPtr<SWidget> BreadcrumbButton = BreadcrumbTrail ? FindClosestWidgetPtrOfType(WidgetPath, "SButton") : nullptr;
	FText ChildText = FindChildWidgetWithText(BreadcrumbButton);
	if (!ChildText.IsEmpty())
	{
		SlateQueryContext.ItemName = ChildText;
		SlateQueryContext.ItemDescriptor = LOCTEXT("ItemDescriptor_Breadcrumb", "navigation breadcrumb");
		SlateQueryContext.LastPickedWidget = BreadcrumbButton;
		SlateQueryContext.bIsUIWidget = true;
		return true;
	}
	return false;
}

// Return true and fill in SlateQueryContext if queried item is an asset item.
static bool ItemIsAsset(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	FText ChildText;

	// Is it an asset tile item?
	TSharedPtr<SWidget> AssetTileItem = FindClosestWidgetPtrOfType(WidgetPath, "SAssetTileItem");
	if (AssetTileItem.IsValid())
	{
		TSharedPtr<SWidget> AssetThumbnail = FindChildWidgetOfType(AssetTileItem, "SAssetThumbnail");
		if (AssetThumbnail.IsValid())
		{
			ChildText = FindChildWidgetWithText(AssetThumbnail); // This returns TYPE of asset instead of name
			if (!ChildText.IsEmpty())
			{
				FFormatNamedArguments Args;
				Args.Add(TEXT("AssetType"), ChildText);
				SlateQueryContext.ItemDescriptor = FText::Format(LOCTEXT("ItemDescriptor_Asset", "{AssetType} asset"), Args);
			}

			if (!SlateQueryContext.CurrentToolTipText.IsEmpty())
			{
				// We can use the tooltip for the asset's name.
				SlateQueryContext.ItemName = SlateQueryContext.CurrentToolTipText;
			}
			SlateQueryContext.LastPickedWidget = AssetThumbnail;
			SlateQueryContext.bIsObject = true;
		}
		else
		{
			ChildText = FindChildWidgetWithText(AssetTileItem);
			if (!ChildText.IsEmpty())
			{
				SlateQueryContext.ItemName = ChildText;
				SlateQueryContext.ItemDescriptor = LOCTEXT("ItemDescriptor_Folder", "asset folder");
				SlateQueryContext.LastPickedWidget = AssetTileItem;
				SlateQueryContext.bIsObject = true;
			}
		}
		return true;
	}

	// Is it an asset tree item?
	TSharedPtr<SWidget> AssetTreeItem = FindClosestWidgetPtrOfType(WidgetPath, "SAssetTreeItem");
	if (AssetTreeItem.IsValid())
	{
		ChildText = FindChildWidgetWithText(AssetTreeItem);
		if (!ChildText.IsEmpty())
		{
			SlateQueryContext.ItemName = ChildText;
			SlateQueryContext.ItemDescriptor = LOCTEXT("ItemDescriptor_Folder", "asset folder");
			SlateQueryContext.LastPickedWidget = AssetTreeItem;
			SlateQueryContext.bIsObject = true;
			return true;
		}
	}
	return false;
}

// Return true and fill in SlateQueryContext if queried item is in Outliner or Subobject Instance Editor.
static bool ItemIsOutlinerActorOrSubobject(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	FText ChildText;

	// Is it an actor in the Outliner?
	TSharedPtr<SWidget> Outliner = FindClosestWidgetPtrOfType(WidgetPath, "SSceneOutliner");
	TSharedPtr<SWidget> ActorTreeLabel = Outliner ? FindClosestWidgetPtrOfType(WidgetPath, "SActorTreeLabel") : nullptr;
	TSharedPtr<SWidget> OutlinerRow = Outliner ? FindClosestWidgetPtrOfType(WidgetPath, "SSceneOutlinerTreeRow") : nullptr;

	if (!ActorTreeLabel.IsValid() && OutlinerRow.IsValid())
	{
		ActorTreeLabel = FindChildWidgetOfType(OutlinerRow, "SActorTreeLabel");
	}

	if (ActorTreeLabel.IsValid())
	{
		ChildText = FindChildWidgetWithText(ActorTreeLabel);
		if (!ChildText.IsEmpty())
		{
			SlateQueryContext.ItemName = ChildText;
			SlateQueryContext.ItemDescriptor = LOCTEXT("ItemDescriptor_OutlinerActor", "actor");
			SlateQueryContext.LastPickedWidget = ActorTreeLabel;
			SlateQueryContext.bIsObject = true;
			return true;
		}
	}

	// Is it an object in the Subobject Instance editor?
	TSharedPtr<SWidget> SubobjectInstanceEditor = FindClosestWidgetPtrOfType(WidgetPath, "SSubobjectInstanceEditor");
	TSharedPtr<SWidget> SubObjectRow = SubobjectInstanceEditor ? FindClosestWidgetPtrOfType(WidgetPath, "SSubobject_RowWidget") : nullptr;
	TSharedPtr<SWidget> NameWidget = SubObjectRow ? FindChildWidgetOfType(SubObjectRow, "SInlineEditableTextBlock") : nullptr;

	if (NameWidget.IsValid())
	{
		ChildText = FindChildWidgetWithText(NameWidget);
		SlateQueryContext.ItemName = ChildText;
		SlateQueryContext.ItemDescriptor = LOCTEXT("ItemDescriptor_SubobjectInstance", "instance");
		SlateQueryContext.LastPickedWidget = SubObjectRow;
		SlateQueryContext.bIsObject = true;
		return true;
	}

	return false;
}

// Return true and fill in SlateQueryContext if queried item is a Details panel filter checkbox.
static bool ItemIsDetailPanelFilterCheckbox(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	TSharedPtr<SWidget> ActorDetails = FindClosestWidgetPtrOfType(WidgetPath, "SActorDetails");
	if (!ActorDetails)
	{
		return false;
	}

	TSharedPtr<SWidget> DetailsView = FindClosestWidgetPtrOfType(WidgetPath, "SDetailsView");
	TSharedPtr<SWidget> DetailsNameArea = FindClosestWidgetPtrOfType(WidgetPath, "SDetailsNameArea");
	TSharedPtr<SWidget> SubobjectInstanceEditor = FindClosestWidgetPtrOfType(WidgetPath, "SSubobjectInstanceEditor");
	TSharedPtr<SWidget> WrapBox = FindClosestWidgetPtrOfType(WidgetPath, "SWrapBox");
	TSharedPtr<SWidget> CheckBox = FindClosestWidgetPtrOfType(WidgetPath, "SCheckBox");

	if (!DetailsView && !DetailsNameArea && !SubobjectInstanceEditor && WrapBox && CheckBox)
	{
		return FillButtonQueryContext(CheckBox, SlateQueryContext,
			LOCTEXT("ItemDescriptor_PropertyFilter", "property filter"));
	}
	return false;
}

// Return true and fill in SlateQueryContext with item information if valid item information is found.
static bool FindItemName(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	SlateQueryContext.ItemDescriptor = LOCTEXT("ItemDescriptor_Generic", "control");

	if (!WidgetPath.IsValid())
	{
		return false;
	}

	if (// Is it a graph node?
		ItemIsGraphNode(WidgetPath, SlateQueryContext) ||
		// Is it a search/filter field?
		ItemIsSearchBox(WidgetPath, SlateQueryContext) ||
		// Is it a console input box?
		ItemIsConsoleInputBox(WidgetPath, SlateQueryContext) ||
		// Is it a details panel property?
		ItemIsDetailsProperty(WidgetPath, SlateQueryContext) ||
		// Is it a plugin in the plugins browser ?
		ItemIsPlugin(WidgetPath, SlateQueryContext) ||
		// Is it a breadcrumb trail button?
		ItemIsBreadcrumbTrailButton(WidgetPath, SlateQueryContext) ||
		// Is it an asset item?
		ItemIsAsset(WidgetPath, SlateQueryContext) ||
		// Is it an Actor in Outliner, or an instance or component in the SubobjectInstanceEditor?
		ItemIsOutlinerActorOrSubobject(WidgetPath, SlateQueryContext) ||
		// Does the widget path end up in a Viewport?
		ItemIsInViewport(WidgetPath, SlateQueryContext) ||
		// Is it one of the Details panel checkboxes that filter the detail properties viewed?
		ItemIsDetailPanelFilterCheckbox(WidgetPath, SlateQueryContext) ||
		// Is it a menu item?
		ItemIsMenuItem(WidgetPath, SlateQueryContext) ||
		// Is it a button?
		ItemIsButton(WidgetPath, SlateQueryContext))
	{
		return true;
	}

	return false;
}

// Return true and fill in SlateQueryContext if valid Tab or Panel information for queried item is found.
static bool FindTabName(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	bool bFoundTabName = false;

	TSharedPtr<SWidget> TabStack = FindClosestWidgetPtrOfType(WidgetPath, "SDockingTabStack");
	if (TabStack.IsValid())
	{
		TArray<TSharedRef<SWidget>> DockTabs;
		FindChildWidgetsOfType(DockTabs, TabStack, "SDockTab");
		for (auto& ThisTab : DockTabs)
		{
			TSharedRef<SDockTab> ThisTabCast = StaticCastSharedRef<SDockTab>(ThisTab);
			if (ThisTabCast->GetTabRole() == ETabRole::MajorTab || ThisTabCast->GetTabRole() == ETabRole::DocumentTab)
			{
				break;
			}
			if (ThisTabCast->IsForeground())
			{
				SlateQueryContext.LastPickedWidget = TabStack;
				SlateQueryContext.StructuredContext.TabName = ThisTabCast->GetTabLabel().ToString();
				FFormatNamedArguments Args;
				Args.Add(TEXT("PanelName"), ThisTabCast->GetTabLabel());
				SlateQueryContext.FormattedTabName = FText::Format(LOCTEXT("TabName_Format", " the {PanelName} panel"), Args);
				bFoundTabName = true;
			}
		}
	}

	return bFoundTabName;
}

// If we're in a details panel, include the class of actor or object that we're editing in SlateQueryContext.
static void GenerateDetailsViewContext(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	const TSharedPtr<SWidget> DetailsView = FindClosestWidgetPtrOfType(WidgetPath, "SDetailsView");
	if (DetailsView.IsValid())
	{
		const TSharedPtr<IDetailsView> DetailsViewCast = StaticCastSharedPtr<IDetailsView>(DetailsView);
		if (DetailsViewCast->GetSelectedActors().Num() > 0)
		{
			FString ActorName;
			DetailsViewCast->GetSelectedActors()[0]->GetClass()->GetName(ActorName);
			SlateQueryContext.StructuredContext.ActorClass = ActorName;
		}
		else if (DetailsViewCast->GetSelectedObjects().Num() > 0)
		{
			FString ObjectName;
			DetailsViewCast->GetSelectedObjects()[0]->GetClass()->GetName(ObjectName);
			SlateQueryContext.StructuredContext.ActorClass = ObjectName;
		}
	}
}

// Return true and fill in SlateQueryContext with item information if valid editor information is found.
static bool IsInEditor(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	static const TSet<FEditorModeID> BlockedModeIDs = TSet<FEditorModeID>({
		FBuiltinEditorModes::EM_Default,
		"EditMode.SubTrackEditMode"
		});

	// are we in the level editor?
	TSharedPtr<SWidget> LevelEditor = FindFirstWidgetPtrOfType(WidgetPath, "SLevelEditor");
	if (LevelEditor)
	{
		SlateQueryContext.StructuredContext.EditorName = TEXT("LevelEditor");
		FText ModeString;
		ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
		if (FEditorModeTools* ModeManager = LevelEditorSubsystem->GetLevelEditorModeManager())
		{
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			TArray<FEditorModeInfo> ModeInfos = AssetEditorSubsystem->GetEditorModeInfoOrderedByPriority();
			for (const FEditorModeInfo& ModeInfo : ModeInfos)
			{
				if (UEdMode* EdMode = ModeManager->GetActiveScriptableMode(ModeInfo.ID))
				{
					TWeakPtr<FModeToolkit> ModeToolkit = EdMode->GetToolkit();
					if (ModeToolkit.IsValid())
					{
						FText ToolDisplayName = ModeToolkit.Pin()->GetActiveToolDisplayName();
						if (!ToolDisplayName.IsEmpty())
						{
							SlateQueryContext.StructuredContext.ActiveToolName = ToolDisplayName.ToString();
						}
					}
				}
				if (ModeString.IsEmpty() && ModeManager->IsModeActive(ModeInfo.ID) && !BlockedModeIDs.Contains(ModeInfo.ID))
				{
					FFormatNamedArguments Args;
					Args.Add(TEXT("ModeName"), ModeInfo.Name);
					ModeString = FText::Format(LOCTEXT("EditorName_ModeFormat", "with {ModeName} mode active"), Args);
					SlateQueryContext.StructuredContext.EditorMode = ModeInfo.Name.ToString();
				}
			}
		}
		SlateQueryContext.LastPickedWidget = LevelEditor;
		if (ModeString.IsEmpty())
		{
			SlateQueryContext.FormattedWindowName = LOCTEXT("EditorName_LevelEditor", " the Level Editor");
		}
		else
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("WithModeActive"), ModeString);
			SlateQueryContext.FormattedWindowName = FText::Format(LOCTEXT("EditorName_LevelEditorAndMode", " the Level Editor {WithModeActive}"), Args);
		}

		if (!GEditor->IsPlaySessionInProgress())
		{
			// Get selected actors, if any.
			bool bActorIsSelected = false;
			UEditorActorSubsystem* EditorActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
			// Get selected actors
			TArray<AActor*> SelectedActors = EditorActorSubsystem->GetSelectedLevelActors();
			for (AActor* Actor : SelectedActors)
			{
				if (Actor)
				{
					SlateQueryContext.SelectedActorName = Actor->GetName();
					SlateQueryContext.SelectedActorClass = Actor->GetClass()->GetName();
					bActorIsSelected = true;
					break;
				}
			}

			// If there is no selection and mouse is in viewport, see what actor is under mouse cursor.
			if (!bActorIsSelected && FindClosestWidgetPtrOfType(WidgetPath, "SViewport"))
			{
				FViewport* ActiveViewport = GEditor->GetActiveViewport();
				if (ActiveViewport)
				{
					FIntPoint MousePos;
					ActiveViewport->GetMousePos(MousePos);
					HHitProxy* HitProxy = ActiveViewport->GetHitProxy(MousePos.X, MousePos.Y);
					if (HitProxy && HitProxy->IsA(HActor::StaticGetType()))
					{
						HActor* TargetProxy = static_cast<HActor*>(HitProxy);
						AActor* TargetActor = TargetProxy->Actor;
						if (TargetActor)
						{
							SlateQueryContext.SelectedActorName = TargetActor->GetName();
							SlateQueryContext.SelectedActorClass = TargetActor->GetClass()->GetName();
							bActorIsSelected = true;
						}
					}
				}
			}
		}

		return true;
	}

	// if not, are we in a different asset editor?
	TSharedPtr<SWidget> StandaloneAssetEditorHost = FindFirstWidgetPtrOfType(WidgetPath, "SStandaloneAssetEditorToolkitHost");
	if (StandaloneAssetEditorHost)
	{
		TArray<TSharedRef<SWidget>> TabStacks;
		FindChildWidgetsOfType(TabStacks, StandaloneAssetEditorHost, "SDockingTabStack");
		for (auto& ThisTabStack : TabStacks)
		{
			TArray<TSharedRef<SWidget>> DockTabs;
			FindChildWidgetsOfType(DockTabs, ThisTabStack.ToSharedPtr(), "SDockTab");

			for (auto& ThisTab : DockTabs)
			{
				TSharedRef<SDockTab> ThisTabCast = StaticCastSharedRef<SDockTab>(ThisTab);
				if (ThisTabCast->IsForeground())
				{
					TSharedPtr<FTabManager> PickedWidgetTabManager = ThisTabCast->GetTabManagerPtr();

					if (PickedWidgetTabManager.IsValid())
					{
						UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
						TArray<UObject*> AllEditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
						for (auto& ThisEditedAsset : AllEditedAssets)
						{
							IAssetEditorInstance* ThisEditor = AssetEditorSubsystem->FindEditorForAsset(ThisEditedAsset, false);
							if (ThisEditor && ThisEditor->GetAssociatedTabManager() == PickedWidgetTabManager)
							{
								SlateQueryContext.StructuredContext.EditorName = ThisEditor->GetEditorName().ToString();
								SlateQueryContext.LastPickedWidget = ThisTabStack.ToSharedPtr();
								FFormatNamedArguments Args;
								Args.Add(TEXT("AssetEditorName"), FText::FromName(ThisEditor->GetEditorName()));
								SlateQueryContext.FormattedWindowName = FText::Format(
									LOCTEXT("EditorName_AssetEditorFormat", " the {AssetEditorName} editor"), Args);

								SlateQueryContext.SelectedActorName = ThisEditedAsset->GetName();
								SlateQueryContext.SelectedActorClass = ThisEditedAsset->GetClass()->GetName();

								return true;
							}
						}
					}
				}
			}
		}
	}
	return false;
}

// Return true and fill in SlateQueryContext with item information if queried item is in a drawer and valid
// window and tab information is found.
static bool IsInDrawer(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	TSharedPtr<SWidget> DrawerOverlay = FindFirstWidgetPtrOfType(WidgetPath, "SDrawerOverlay");
	if (DrawerOverlay.IsValid())
	{
		FText DrawerName;
		TSharedPtr<SWidget> DrawerWidget = FindChildWidgetOfType(DrawerOverlay, "SContentBrowser");
		if (!DrawerWidget)
		{
			DrawerWidget = FindChildWidgetOfType(DrawerOverlay, "SOutputLog");
		}

		if (DrawerWidget)
		{
			if (DrawerWidget->GetType() == "SContentBrowser")
			{
				DrawerName = LOCTEXT("DrawerName_ContentBrowser", "ContentBrowser");
			}
			else if (DrawerWidget->GetType() == "SOutputLog")
			{
				DrawerName = LOCTEXT("DrawerName_OutputLog", "OutputLog");
			}
			// Are there other items that are commonly in drawers?

			if (!DrawerName.IsEmpty())
			{
				SlateQueryContext.StructuredContext.WindowName = DrawerOverlay->GetType().ToString();
				SlateQueryContext.StructuredContext.TabName = DrawerName.ToString();
				SlateQueryContext.LastPickedWidget = DrawerWidget;
				FFormatNamedArguments Args;
				Args.Add(TEXT("DrawerName"), DrawerName);
				SlateQueryContext.FormattedWindowName = FText::Format(LOCTEXT("DrawerNameFormat", "the {DrawerName} drawer"), Args);
				return true;
			}
		}
	}
	return false;
}

// Return true and fill in SlateQueryContext with item information if valid editor or window information is found.
static bool FindEditorName(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& SlateQueryContext)
{
	FText OutName = FText();

	if (!WidgetPath.IsValid())
	{
		SlateQueryContext.FormattedWindowName = OutName;
		return false;
	}

	// is it in the status bar?
	TSharedPtr<SWidget> StatusBar = FindClosestWidgetPtrOfType(WidgetPath, "SStatusBar");
	if (StatusBar.IsValid())
	{
		SlateQueryContext.LastPickedWidget = StatusBar;
		SlateQueryContext.StructuredContext.TabName = TEXT("StatusBar");
		SlateQueryContext.FormattedWindowName = LOCTEXT("TabName_StatusBar", " the Status Bar");
		return true;
	}

	if (IsInEditor(WidgetPath, SlateQueryContext))
	{
		return true;
	}

	// Is it in a drawer overlay?
	if (IsInDrawer(WidgetPath, SlateQueryContext))
	{
		return true;
	}

	TSharedRef<SWindow> WidgetWindow = WidgetPath.GetWindow();
	// is it the main window that hosts the level editor?
	if (FModuleManager::Get().IsModuleLoaded("LevelEditor"))
	{
		IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
		if (WidgetWindow.ToSharedPtr() == MainFrameModule.GetParentWindow())
		{
			SlateQueryContext.StructuredContext.WindowName = TEXT("MainFrame");
			SlateQueryContext.FormattedWindowName = LOCTEXT("WindowMainFrame_Title", " the editor's main window");
			return true;
		}
	}
	
	// does the window have a title, like the Project Browser or Color Picker?
	FText WindowTitle = WidgetWindow->GetTitle();
	if (!WindowTitle.IsEmpty())
	{
		SlateQueryContext.StructuredContext.WindowName = WindowTitle.ToString();
		SlateQueryContext.LastPickedWidget = WidgetWindow.ToSharedPtr();
		FFormatNamedArguments Args;
		Args.Add(TEXT("WindowTitle"), WindowTitle);
		SlateQueryContext.FormattedWindowName = FText::Format(LOCTEXT("WindowNameFormat_Title", " the {WindowTitle} window"), Args);
		return true;
	}

	// TODO: how to find other editor contexts?
	return false;
}

// Fill in AccumulatedSlateQueryContext with information on text under cursor if found.
static void FindTextUnderCursor(const FWidgetPath& WidgetPath, FAIAssistantSlateQueryContext& AccumulatedSlateQueryContext)
{
	FText OutText;

	if (WidgetPath.IsValid())
	{
		const FRegexPattern AlphaPattern(TEXT("[A-Za-z]+"));
		TSharedRef<SWidget> WidgetToTest = WidgetPath.GetLastWidget();
		FText WidgetText;
		if (WidgetToTest->GetType() == "STextBlock")
		{
			TSharedRef<STextBlock> TextBlock = StaticCastSharedRef<STextBlock>(WidgetToTest);
			WidgetText = TextBlock->GetText();
		}
		else if (WidgetToTest->GetType() == "SRichTextBlock")
		{
			TSharedRef<SRichTextBlock> TextBlock = StaticCastSharedRef<SRichTextBlock>(WidgetToTest);
			WidgetText = TextBlock->GetText();
		}
		if (!WidgetText.IsEmpty())
		{
			FRegexMatcher AlphaMatcher(AlphaPattern, WidgetText.ToString());
			if (AlphaMatcher.FindNext())
			{
				OutText = WidgetText;
			}
		}

		if (!OutText.IsEmpty())
		{
			AccumulatedSlateQueryContext.LastPickedWidget = WidgetToTest;
			AccumulatedSlateQueryContext.TextUnderCursor = OutText;
			AccumulatedSlateQueryContext.StructuredContext.TextUnderCursor = OutText.ToString();
		}
	}
}


static FString CleanExtraWhiteSpaceFromString(const FString& InString)
{
	FString CleanString = "";
	static const FRegexPattern WhiteSpacePattern(TEXT("(\\S+)"));
	FRegexMatcher Matcher(WhiteSpacePattern, InString);
	while (Matcher.FindNext())
	{
		if (!CleanString.IsEmpty())
		{
			CleanString += " ";
		}
		
		CleanString += Matcher.GetCaptureGroup(1);
	}

	return MoveTemp(CleanString);
}

//
// UE::AIAssistant::SlateQuerier
//
FWidgetPath UE::AIAssistant::SlateQuerier::GetWidgetPathUnderCursor()
{
	const FVector2f CursorPos = FSlateApplication::Get().GetCursorPos();
	const TArray<TSharedRef<SWindow>>& Windows = FSlateApplication::Get().GetTopLevelWindows();
	return FSlateApplication::Get().LocateWindowUnderMouse(CursorPos, Windows, true);
}

void UE::AIAssistant::SlateQuerier::QueryAIAssistantAboutSlateWidget(const FWidgetPath& InWidgetPath)
{
	if (!InWidgetPath.IsValid())
	{
		UE_LOG(LogAIAssistant, Warning, TEXT("No valid widget path found to generate widget query."));
		return;
	}

	// We build this up, below.
	
	FAIAssistantSlateQueryContext SlateQueryContext;

	// Is there a current tool-tip?
	SlateQueryContext.CurrentToolTipText = FindCurrentToolTipText();

	// For menu items, we want to generate the context based on the root widget that spawned the menu,
	// not on the actual menu item itself.
	const FWidgetPath ContextWidgetPath = FindContextWidgetPath(InWidgetPath);

	// Is there an identifiable asset editor, mode, or window?
	FindEditorName(ContextWidgetPath, SlateQueryContext);

	// Is there an identifiable tab?
	// Look up the chain for an SDockingTabStack, then look down to find an SDockTab.
	FindTabName(ContextWidgetPath, SlateQueryContext);

	// Is there text under the cursor??
	FindTextUnderCursor(InWidgetPath, SlateQueryContext);

	// Is there an identifiable item?
	FindItemName(InWidgetPath, SlateQueryContext);

	// Construct query.
	{
		FText FormattedItemName = SlateQueryContext.ItemName;
		if (!FormattedItemName.IsEmpty())
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("ItemName"), SlateQueryContext.ItemName);
			Args.Add(TEXT("ItemDescriptor"), SlateQueryContext.ItemDescriptor);
			FormattedItemName = FText::Format(LOCTEXT("ItemName_Format", " the \"{ItemName}\" {ItemDescriptor}"), Args);
		}

		FText NameToQuery;
		if (SlateQueryContext.bIsInAssistantPanel)
		{
			NameToQuery = LOCTEXT("ItemName_Assistant", "the AI Assistant");
		}
		else if (FormattedItemName.IsEmpty() && !SlateQueryContext.TextUnderCursor.IsEmpty())
		{
			NameToQuery = SlateQueryContext.TextUnderCursor;
		}
		else if (!FormattedItemName.IsEmpty() || !SlateQueryContext.FormattedTabName.IsEmpty() || !SlateQueryContext.FormattedWindowName.IsEmpty())
		{
			NameToQuery = (!FormattedItemName.IsEmpty()) ? FormattedItemName : (!SlateQueryContext.FormattedTabName.IsEmpty()) ? SlateQueryContext.FormattedTabName : SlateQueryContext.FormattedWindowName;
		}

		// Localized text is non-static const so it can change when the editor language is changed.
		const FText QueryFormatWidget = LOCTEXT("QueryFormat1", "I would like to know what {NameToQuery} does.");
		const FText QueryFormatGeneral = LOCTEXT("QueryFormat2", "I would like to know what \"{NameToQuery}\" means.");
		const FText QueryFormatObject = LOCTEXT("QueryFormat3", "I would like to know about {NameToQuery}.");
		const FText QueryFormatAssistant = LOCTEXT("QueryFormat4", "Tell me about yourself.");
		// Hidden instruction text sent to assistant can be non-localized and static const.
		static const FText QueryInstructionsWidgetFormat = INVTEXT(
			"Provide an easily readable, informative, accurate answer that describes what the widget does in the Unreal Editor UI. {QueryAdditionalInstructions} {StyleInstructions}");
		static const FText QueryInstructionsObjectFormat = INVTEXT(
			"Provide an easily readable, informative, accurate answer that describes what I should know about it in the Unreal Editor UI. {QueryAdditionalInstructions} {StyleInstructions}");
		static const FText QueryInstructionsAssistantFormat = INVTEXT(
			"Provide an easily readable, informative, accurate answer that describes what I should know about yourself and what you can do for me. {StyleInstructions}");
		static const FText QueryAdditionalInstructionsWidget = INVTEXT(
			"Explain what happens if I select or click on it.");
		static const FText QueryAdditionalInstructionsObject = INVTEXT(
			"Describe what the object is or what it does.");
		static const FText QueryAdditionalInstructionsGeneral = INVTEXT(
			"If the object is clickable, like a button, then explain what happens if I click on it. If the object is something you select, like an asset or a blueprint node, describe what the object is or what it does.");
		static const FText StyleInstructions = INVTEXT(
			"Use bold text to emphasize Unreal Engine specific terms. Start with a quick overview paragraph, then add key points limited to one or two formatted sections with headers and bullet points. Don't include a summary at the end.");

		if (!NameToQuery.IsEmpty())
		{
			FFormatNamedArguments QueryArgs;
			QueryArgs.Add(TEXT("NameToQuery"), NameToQuery);
			FFormatNamedArguments InstructionArgs;

			if (SlateQueryContext.bIsInAssistantPanel)
			{
				SlateQueryContext.GeneratedQuery = FText::Format(QueryFormatAssistant, QueryArgs);
				InstructionArgs.Add(TEXT("StyleInstructions"), StyleInstructions);
				SlateQueryContext.GeneratedQueryInstructions = FText::Format(QueryInstructionsAssistantFormat, InstructionArgs);
			}
			else if (SlateQueryContext.bIsObject)
			{
				SlateQueryContext.GeneratedQuery = FText::Format(QueryFormatObject, QueryArgs);
				InstructionArgs.Add(TEXT("QueryAdditionalInstructions"), QueryAdditionalInstructionsObject);
				InstructionArgs.Add(TEXT("StyleInstructions"), StyleInstructions);
				SlateQueryContext.GeneratedQueryInstructions = FText::Format(QueryInstructionsObjectFormat, InstructionArgs);
			}
			else if (SlateQueryContext.bIsUIWidget)
			{
				SlateQueryContext.GeneratedQuery = FText::Format(QueryFormatWidget, QueryArgs);
				InstructionArgs.Add(TEXT("QueryAdditionalInstructions"), QueryAdditionalInstructionsWidget);
				InstructionArgs.Add(TEXT("StyleInstructions"), StyleInstructions);
				SlateQueryContext.GeneratedQueryInstructions = FText::Format(QueryInstructionsWidgetFormat, InstructionArgs);
			}
			else
			{
				SlateQueryContext.GeneratedQuery = FText::Format(QueryFormatWidget, QueryArgs);
				InstructionArgs.Add(TEXT("QueryAdditionalInstructions"), QueryAdditionalInstructionsGeneral);
				InstructionArgs.Add(TEXT("StyleInstructions"), StyleInstructions);
				SlateQueryContext.GeneratedQueryInstructions = FText::Format(QueryInstructionsWidgetFormat, InstructionArgs);
			}
		}

		if (SlateQueryContext.GeneratedQuery.IsEmpty())
		{
			UE_LOG(LogAIAssistant, Warning, TEXT("Could not generate query for widget."));
			return;
		}
	}

	// Construct structured context.
	{
		SlateQueryContext.StructuredContext.UnrealEditorVersion = FEngineVersion::Current().ToString();
		SlateQueryContext.StructuredContext.UnrealEditorLanguage = FInternationalization::Get().GetCurrentLanguage()->GetDisplayName();
		SlateQueryContext.StructuredContext.UnrealEditorLocale = FInternationalization::Get().GetCurrentLocale()->GetName();

		static const FText LanguageInstructionsFormat = INVTEXT(" Please respond in language \"{0}\". ");
		const FText Language = FText::FromString(SlateQueryContext.StructuredContext.UnrealEditorLanguage);
		SlateQueryContext.LanguageInstructions = FText::Format(LanguageInstructionsFormat, Language);

		SlateQueryContext.StructuredContext.ToolTipText = SlateQueryContext.CurrentToolTipText.ToString();

		GenerateDetailsViewContext(InWidgetPath, SlateQueryContext);

		if (!SlateQueryContext.ItemName.IsEmpty())
		{
			SlateQueryContext.StructuredContext.ItemName = SlateQueryContext.ItemName.ToString();
			SlateQueryContext.StructuredContext.ItemDescriptor = SlateQueryContext.ItemDescriptor.ToString();
		}

		// Get top level window title.
		TSharedRef<SWindow> WidgetWindow = InWidgetPath.GetWindow();
		FText WindowTitle = WidgetWindow->GetTitle();
		if (!WindowTitle.IsEmpty())
		{
			SlateQueryContext.StructuredContext.TopWindowName = WindowTitle.ToString();
		}

		// Get picked widget type.
		if (SlateQueryContext.LastPickedWidget.IsValid())
		{
			SlateQueryContext.StructuredContext.WidgetType = SlateQueryContext.LastPickedWidget->GetType().ToString();
		}

		if (SlateQueryContext.ItemName.IsEmpty() && !SlateQueryContext.bIsInAssistantPanel)
		{
			SlateQueryContext.StructuredContext.WidgetPath = TArray<FString>();
			// Add simplified widget path
			for (int32 WidgetIndex = 0; WidgetIndex < InWidgetPath.Widgets.Num(); WidgetIndex++)
			{
				const FArrangedWidget* ArrangedWidget = &InWidgetPath.Widgets[WidgetIndex];
				const TSharedRef<SWidget>& ThisWidget = ArrangedWidget->Widget;
				FString NextWidgetString = ThisWidget->GetType().ToString();
				// Skip uninteresting widgets
				if (NextWidgetString != TEXT("SBorder") && NextWidgetString != TEXT("SBox") &&
					NextWidgetString != TEXT("SScrollBorder") &&
					NextWidgetString != TEXT("SOverlay") && NextWidgetString != TEXT("SVerticalBox") &&
					NextWidgetString != TEXT("SHorizontalBox") && NextWidgetString != TEXT("SSplitter") &&
					NextWidgetString != TEXT("SDockingSplitter"))
				{
					SlateQueryContext.StructuredContext.WidgetPath->Add(NextWidgetString);
				}
			}
		}
	}

	// OPTIONAL - We're done using these members. We can clear them to reduce size as they're
	// not used below.
	SlateQueryContext.LastPickedWidget.Reset();

	// Send widget query to AI Assistant.
	TSharedPtr<SAIAssistantWebBrowser> WebBrowser =
		UAIAssistantSubsystem::GetAIAssistantWebBrowserWidget();
	WebBrowser->CreateConversation();

	const FString VisiblePromptString =
		CleanExtraWhiteSpaceFromString(SlateQueryContext.GeneratedQuery.ToString());

	const FString HiddenContextString = 
		CleanExtraWhiteSpaceFromString(SlateQueryContext.GeneratedQueryInstructions.ToString()) +
		SlateQueryContext.LanguageInstructions.ToString() +
		TEXT(" ContextData=") +
		SlateQueryContext.StructuredContext.ToJson(false);

	WebBrowser->AddUserMessageToConversation(VisiblePromptString, HiddenContextString);
}


#undef LOCTEXT_NAMESPACE
