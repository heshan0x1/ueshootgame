// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorMode/PCGEdModeCommands.h"

#include "PCGEditorModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorMode/PCGEdModeSettings.h"
#include "EditorMode/PCGEdModeStyle.h"
#include "EditorMode/Tools/Helpers/PCGEdModeEditorUtilities.h"
#include "EditorMode/Tools/Line/PCGDrawSplineTool.h"
#include "EditorMode/Tools/Paint/PCGPaintTool.h"
#include "EditorMode/Tools/Volume/PCGVolumeTool.h"
#include "Logging/StructuredLog.h"
#include "Misc/OutputDeviceNull.h"

#define LOCTEXT_NAMESPACE "PCGEditorMode"

struct FSlateIcon;

FPCGEditorModeCommands::FPCGEditorModeCommands()
	: TCommands(/*InContextName=*/TEXT("PCGEditorModeCommands")
							  , /*InContextDesc=*/LOCTEXT("PCGEditorModeCommands", "PCG Editor Mode")
							  , /*InContextParent=*/NAME_None
							  , /*InStyleSetName=*/FPCGEditorModeStyle::Get().GetStyleSetName()) {}

void FPCGEditorModeCommands::RegisterCommands() {}

FPCGEditorModePaletteCommands::FPCGEditorModePaletteCommands()
	: TCommands(TEXT("PCGEditorModePaletteCommands")
			, LOCTEXT("PCGEditorModePaletteCommands", "PCG Editor Mode - Palette")
			, /*Parent=*/NAME_None
			, /*IconStyleSet=*/FPCGEditorModeStyle::Get().GetStyleSetName()) {}

/**
 * @param CommandInfoSP The shared pointer for the FUICommandInfo
 * @param Name The name of the tool
 * @param IconStyleName The style name for the icon to be used for the button
 */
#define PCG_CONTEXT_COMMAND_INFO(CommandInfoSP, Name, IconStyleName)\
FUICommandInfo::MakeCommandInfo(AsShared()\
	, CommandInfoSP\
	, TEXT("Enter" Name "Context")\
	, LOCTEXT(Name "ContextLabel", Name)\
	, LOCTEXT(Name "ContextToolTip", "Enter '" Name "' context and open corresponding toolkit")\
	, FSlateIcon(FPCGEditorModeStyle::Get().GetStyleSetName(), IconStyleName)\
	, EUserInterfaceActionType::ToggleButton\
	, FInputChord())

void FPCGEditorModePaletteCommands::RegisterCommands()
{
	/*** PCG EDITOR CONTEXTS **/
	PCG_CONTEXT_COMMAND_INFO(LoadSplineContextPalette, "Spline", "PCGEditorMode.Context.DrawSpline");
	PCG_CONTEXT_COMMAND_INFO(LoadPaintContextPalette, "Paint", "PCGEditorMode.Context.Paint");
	PCG_CONTEXT_COMMAND_INFO(LoadVolumeContextPalette, "Volume", "PCGEditorMode.Context.Volume");
}

#undef PCG_CONTEXT_COMMAND_INFO

FPCGEditorModeToolCommands::FPCGEditorModeToolCommands()
	: TInteractiveToolCommands(TEXT("PCGEditorModeToolCommands")
						   , LOCTEXT("PCGEditorModeToolCommands", "PCG Editor Mode - Tool Commands")
						   , /*Parent=*/NAME_None
						   , /*IconStyleSet=*/FPCGEditorModeStyle::Get().GetStyleSetName()) {}

/**
  * @param CommandInfoSP The shared pointer for the FUICommandInfo
 * @param Name The name of the tool
 * @param Label The user-forward label for the tool
 * @param ToolTip The tooltip for the tool: Format can include {ToolTag}.
 * @param ToolTag The tag for the tool: can be used in the tooltip.
 * @param IconStyleName The style name for the icon to be used for the button
 */
#define PCG_TOOL_COMMAND_INFO(CommandInfoSP, Name, Label, ToolTip, ToolTag, IconStyleName)\
FUICommandInfo::MakeCommandInfo(AsShared()\
	, CommandInfoSP\
	, TEXT(Name"Tool")\
	, LOCTEXT(Name"ToolLabel", Label)\
	, FText::Format(LOCTEXT(Name"ToolTip", ToolTip), FFormatNamedArguments({{TEXT("ToolTag"), FText::FromName(ToolTag)}}))\
	, FSlateIcon(FPCGEditorModeStyle::Get().GetStyleSetName(), IconStyleName)\
	, EUserInterfaceActionType::ToggleButton\
	, FInputChord())

void FPCGEditorModeToolCommands::RegisterCommands()
{
	TInteractiveToolCommands::RegisterCommands();

	/*** PCG EDITOR MODE TOOLS **/

	PCG_TOOL_COMMAND_INFO(EnableDrawSplineTool, "DrawSpline", "Draw Spline", "Use the '{ToolTag}' tool to build a spline for PCG.", UPCGInteractiveToolSettings_Spline::StaticGetToolTag(), "PCGEditorMode.Tools.DrawSpline");
	PCG_TOOL_COMMAND_INFO(EnableDrawSurfaceTool, "DrawSurface", "Draw Spline Surface", "Use the '{ToolTag}' tool to build a closed spline representing an area for PCG.", UPCGInteractiveToolSettings_SplineSurface::StaticGetToolTag(), "PCGEditorMode.Tools.DrawSurface");
	PCG_TOOL_COMMAND_INFO(EnablePaintTool, "Paint", "Paint", "Use the '{ToolTag}' tool to apply PCG points with a brush.", UPCGInteractiveToolSettings_PaintTool::StaticGetToolTag(), "PCGEditorMode.Context.Paint");
	PCG_TOOL_COMMAND_INFO(EnableVolumeTool, "Volume", "Volume", "Create or updates a volume in the scene using the '{ToolTag}' tool.", UPCGInteractiveToolSettings_Volume::StaticGetToolTag(), "PCGEditorMode.Tools.Volume");
}

void FPCGEditorModeToolCommands::RegisterAllToolCommands()
{
	FPCGDrawSplineCommands::Register();
	FPCGPaintCommands::Register();
	FPCGVolumeCommands::Register();
}

void FPCGEditorModeToolCommands::UnregisterAllToolCommands()
{
	FPCGVolumeCommands::Unregister();
	FPCGPaintCommands::Unregister();
	FPCGDrawSplineCommands::Unregister();
}

void FPCGEditorModeToolCommands::GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs)
{
	// @todo_pcg: To be populated with tool commands for bindings.
}

TArray<TSharedPtr<FUICommandInfo>> FPCGEditorModeToolCommands::GetLineContextToolCommands()
{
	return {EnableDrawSplineTool, EnableDrawSurfaceTool};
}

TArray<TSharedPtr<FUICommandInfo>> FPCGEditorModeToolCommands::GetPaintContextToolCommands()
{
	return {EnablePaintTool};
}

TArray<TSharedPtr<FUICommandInfo>> FPCGEditorModeToolCommands::GetVolumeContextToolCommands()
{
	return {EnableVolumeTool};
}

/**
 * Macro for defining a constructor and a helper function to retrieve 
 * @param ToolCommandClassName The tool's class name
 * @param ToolNameLabel A name string for the command context
 * @param ToolClassName The class name of the tool
 */
#define PCG_DEFINE_TOOL_COMMANDS(ToolCommandClassName, Label, Name, ToolClassName )\
ToolCommandClassName::ToolCommandClassName()\
	: TInteractiveToolCommands<ToolCommandClassName>(\
		Name,\
		NSLOCTEXT("PCGEditorModeTools", "PCG" Label "ToolCommands", "PCG Editor Tools - '" Name "' Tool"),\
		NAME_None,\
		FPCGEditorModeStyle::Get().GetStyleSetName()) {}\
void ToolCommandClassName::GetToolDefaultObjectList(TArray<UInteractiveTool*>& ToolCDOs)\
{\
ToolCDOs.Add(GetMutableDefault<ToolClassName>());\
}
PCG_DEFINE_TOOL_COMMANDS(FPCGDrawSplineCommands, "DrawSpline", "Draw Spline", UPCGDrawSplineToolBase);
PCG_DEFINE_TOOL_COMMANDS(FPCGDrawSurfaceCommands, "DrawSurface", "Draw Surface", UPCGDrawSplineToolBase);
PCG_DEFINE_TOOL_COMMANDS(FPCGPaintCommands, "Paint", "Paint", UPCGPaintTool);
PCG_DEFINE_TOOL_COMMANDS(FPCGVolumeCommands, "Volume", "Volume", UPCGVolumeTool);

#undef PCG_DEFINE_TOOL_COMMANDS

#undef PCG_TOOL_COMMAND_INFO

#undef LOCTEXT_NAMESPACE
