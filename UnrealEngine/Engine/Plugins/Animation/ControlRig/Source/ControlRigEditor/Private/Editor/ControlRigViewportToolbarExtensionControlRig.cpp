// Copyright Epic Games, Inc. All Rights Reserved.

#include "ControlRigViewportToolbarExtensionControlRig.h"

#include "AnimationEditorViewportClient.h"
#include "BlueprintEditor.h"
#include "ControlRigContextMenuContext.h"
#include "ControlRigEditorStyle.h"
#include "EditMode/ControlRigEditModeSettings.h"
#include "Editor/ControlRigEditor.h"
#include "Editor/ControlRigEditorCommands.h"
#include "Editor/ControlRigNewEditor.h"
#include "SEditorViewport.h"
#include "Templates/SharedPointer.h"
#include "ToolMenu.h"
#include "ToolMenuEntry.h"
#include "ToolMenus.h"
#include "ToolMenuSection.h"
#include "UObject/ScriptInterface.h"
#include "UObject/WeakInterfacePtr.h"
#include "ViewportToolbar/UnrealEdViewportToolbarContext.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include <limits>

#define LOCTEXT_NAMESPACE "ControlRigViewportToolbarExtensionControlRig"

namespace UE::ControlRigEditor
{
	/** Creates the conrol rig entry */
	namespace Private
	{
		/** Creates a widget to edit the axis scale in editor */
		TSharedRef<SWidget> MakeAxisScaleWidget()
		{
			const FText Tooltip = LOCTEXT("ControlRigAxesScaleToolTip", "Scale of axes drawn for selected rig elements");

			return 
				SNew(SBox)
				.HAlign(HAlign_Right)
				[
					SNew(SBox)
					.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
					.WidthOverride(100.0f)
					[
						SNew(SNumericEntryBox<float>)
						.Font(FAppStyle::GetFontStyle(TEXT("MenuItem.Font")))
						.AllowSpin(true)
						.MinSliderValue(0.0f)
						.MaxSliderValue(100.0f)
						.Value_Lambda([]()
							{										
								return GetDefault<UControlRigEditModeSettings>()->AxisScale;
							})
						.OnValueChanged_Lambda([](float Value)
						{
							UControlRigEditModeSettings* Settings = GetMutableDefault<UControlRigEditModeSettings>();
							Settings->AxisScale = FMath::Max(0.f, Value);
							Settings->SaveConfig();
						})
						.ToolTipText(Tooltip)
					]
				];
		}

		/** Adds the control rig sub menu to the toolbar */
		FToolMenuEntry CreateControlRigEntry(const IControlRigAssetInterface& Asset)
		{
			const TWeakInterfacePtr<const IControlRigAssetInterface> WeakAsset = &Asset;

			FToolMenuEntry Entry = FToolMenuEntry::InitSubMenu(
				"ControlRig",
				LOCTEXT("ControlRigSubMenuLabel", "Control Rig"),
				LOCTEXT("ControlRigSubMenuTooltip", "Control Rig related settings"),
				FNewToolMenuDelegate::CreateLambda(
					[WeakAsset](UToolMenu* SubMenu) -> void
					{
						FToolMenuSection& ControlRigSection = 
							SubMenu->FindOrAddSection(
								"ControlRig", 
								LOCTEXT("ControlRigLabel", "Control Rig"));

						const FControlRigEditorCommands& Commands = FControlRigEditorCommands::Get();

						ControlRigSection.AddMenuEntry(Commands.ToggleControlVisibility);
						ControlRigSection.AddMenuEntry(Commands.ToggleControlsAsOverlay);
						ControlRigSection.AddMenuEntry(Commands.ToggleDrawNulls);
						ControlRigSection.AddMenuEntry(Commands.ToggleDrawSockets);
						ControlRigSection.AddMenuEntry(Commands.ToggleDrawAxesOnSelection);

						ControlRigSection.AddSeparator("SliderSection");

						ControlRigSection.AddEntry(FToolMenuEntry::InitWidget(
							"AxesScale",
							MakeAxisScaleWidget(),
							LOCTEXT("ControlRigAxesScale", "Axes Scale")));

						// Entries for Modular Rigs
						if (WeakAsset.IsValid() && WeakAsset.Get()->IsModularRig())
						{
							FToolMenuSection& ModularRigSection = SubMenu->FindOrAddSection(
								"ModularRig",
								LOCTEXT("ModularRig_Label", "Modular Rig"));

							FToolMenuSection& MRSection = SubMenu->FindOrAddSection(
								"ControlRig",
								LOCTEXT("ControlRigLabel", "Control Rig"));

							ModularRigSection.AddMenuEntry(Commands.ToggleSchematicViewportVisibility);
						}
					})
				);

			Entry.InsertPosition = FToolMenuInsert("Show", EToolMenuInsertType::After);
			Entry.Icon = FSlateIcon(FControlRigEditorStyle::Get().GetStyleSetName(), "ControlRig.Editor.TabIcon");
			Entry.ToolBarData.LabelOverride = FText::GetEmpty();

			// Set resize prio to highest as other entries in other sections dominate otherwise
			Entry.ToolBarData.ResizeParams.ClippingPriority = std::numeric_limits<int32>::max(); 

			return Entry;
		}
	}
	//~ namespace Private

	void PopulateControlRigViewportToolbarControlRigSubmenu(const FName& MenuName)
	{
		TWeakObjectPtr<UToolMenu> Toolbar = UToolMenus::Get()->ExtendMenu(MenuName);
		FToolMenuSection* RightSection = Toolbar.IsValid() ? Toolbar->FindSection("Right") : nullptr;
		if (!Toolbar.IsValid() ||
			!RightSection)
		{
			return;
		}

		RightSection->AddDynamicEntry(
			"ControlRig",
			FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& Section)
				{		
					// Only for control rig assets
					const UControlRigContextMenuContext* Context = Section.FindContext<UControlRigContextMenuContext>();
					const TScriptInterface<IControlRigAssetInterface> ControlRigAsset = Context ? Context->GetControlRigAssetInterface() : nullptr;
					if (Context &&
						ControlRigAsset)
					{
						Section.AddEntry(Private::CreateControlRigEntry(*ControlRigAsset));
					}
				})
		);	
	}
}

#undef LOCTEXT_NAMESPACE
