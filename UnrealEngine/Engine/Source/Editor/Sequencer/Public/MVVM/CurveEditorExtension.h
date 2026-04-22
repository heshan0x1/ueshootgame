// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MVVM/ViewModels/ViewModel.h"
#include "MVVM/ViewModelPtr.h"
#include "MVVM/Extensions/DynamicExtensionContainer.h"
#include "MVVM/Extensions/ICurveEditorTreeItemExtension.h"
#include "CurveDataAbstraction.h"
#include "CurveEditorTypes.h"
#include "Tree/ICurveEditorTreeItem.h"
#include "STemporarilyFocusedSpinBox.h"

#define UE_API SEQUENCER_API

class ISequencer;
class IPropertyTypeCustomization;
class SCurveEditorPanel;
class SCurveEditorTree;
class SCurveEditorTreeFilterStatusBar;
struct FTimeSliderArgs;

namespace UE
{
namespace Sequencer
{

class FSequencerEditorViewModel;

class FCurveEditorExtension : public IDynamicExtension
{
public:

	UE_SEQUENCER_DECLARE_VIEW_MODEL_TYPE_ID_API(UE_API, FCurveEditorExtension)

	UE_API FCurveEditorExtension();

	UE_API virtual void OnCreated(TSharedRef<FViewModel> InWeakOwner) override;

	/** Creates the curve editor view-model and widget */
	UE_API void CreateCurveEditor(const FTimeSliderArgs& TimeSliderArgs);

	/** Gets the curve editor view-model */
	TSharedPtr<FCurveEditor> GetCurveEditor() const { return CurveEditorModel; }

	/** Opens the curve editor */
	UE_API void OpenCurveEditor();
	/** Returns whether the curve editor is open */
	UE_API bool IsCurveEditorOpen() const;
	/** Closes the curve editor */
	UE_API void CloseCurveEditor();

	/** Curve editor tree widget */
	TSharedPtr<SCurveEditorTree> GetCurveEditorTreeView() const { return CurveEditorTreeView; }

	/**
	 * Synchronize curve editor selection with sequencer outliner selection on the next update.
	 */
	UE_API void RequestSyncSelection();


public:

	static UE_API const FName CurveEditorTabName;

private:

	/** Get the default key attributes to apply to newly created keys on the curve editor */
	UE_API FKeyAttributes GetDefaultKeyAttributes() const;

	/** Syncs the selection of the curve editor with that of the sequencer */
	UE_API void SyncSelection();

	/** Executed when the filter class has changed */
	UE_API void FilterClassChanged();

private:

	/** The sequencer editor we are extending with a curve editor */
	TWeakPtr<FSequencerEditorViewModel> WeakOwnerModel;

	/** Curve editor */
	TSharedPtr<FCurveEditor> CurveEditorModel;
	/** Curve editor tree widget */
	TSharedPtr<SCurveEditorTree> CurveEditorTreeView;
	/** The search widget for filtering curves in the Curve Editor tree. */
	TSharedPtr<SWidget> CurveEditorSearchBox;
	/** The curve editor widget containing the curve editor panel */
	TSharedPtr<SWidget> CurveEditorWidget;
	/** The curve editor panel. This is created and updated even if it is not currently visible. */
	TSharedPtr<SCurveEditorPanel> CurveEditorPanel;
	/** Filter Status Bar */
	TSharedPtr<SCurveEditorTreeFilterStatusBar> CurveEditorTreeFilterStatusBar;
	/** The current playback time display. */
	TSharedPtr<STemporarilyFocusedSpinBox<double>> PlayTimeDisplay;

	friend class FCurveEditorIntegrationExtension;
};

} // namespace Sequencer
} // namespace UE

#undef UE_API
