// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "Delegates/IDelegateInstance.h"
#include "Delegates/DelegateCombinations.h"
#include "Misc/Attribute.h"
#include "Templates/SharedPointer.h"

#define UE_API LIVELINKEDITOR_API

class IDetailsView;
class FLiveLinkClient;
struct FLiveLinkSourceUIEntry;
struct FLiveLinkSubjectKey;
struct FLiveLinkSubjectUIEntry;
class FLiveLinkSourcesView;
class SLiveLinkSourceListView;
class FLiveLinkSubjectsView;
class FUICommandList;
class SLiveLinkDataView;

namespace ESelectInfo { enum Type : int; }
typedef TSharedPtr<FLiveLinkSourceUIEntry> FLiveLinkSourceUIEntryPtr;
typedef TSharedPtr<FLiveLinkSubjectUIEntry> FLiveLinkSubjectUIEntryPtr;


/** Handles callback connections between the sources, subjects and details views. */
class FLiveLinkPanelController : public TSharedFromThis<FLiveLinkPanelController>
{
public:
	UE_API FLiveLinkPanelController(TAttribute<bool> bInReadOnly = false);
	UE_API ~FLiveLinkPanelController();

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSubjectSelectionChanged, const FLiveLinkSubjectKey&);
	/** Subject Selection changed callback. */
	FOnSubjectSelectionChanged& OnSubjectSelectionChanged()
	{
		return SubjectSelectionChangedDelegate;
	}

private:
	// Bind live link commands 
	UE_API void BindCommands();
	// Handles the source collection changing.
	UE_API void OnSourcesChangedHandler();
	// Handles the subject collection changing.
	UE_API void OnSubjectsChangedHandler();
	// Remove all sources command handlers
	UE_API bool HasSource() const;
	// Handles the remove source command.
	UE_API void HandleRemoveSource();
	// Handles the remove all sources command.
	UE_API void HandleRemoveAllSources();
	// Returns whether a source could be removed.
	UE_API bool CanRemoveSource() const;
	// Returns whether a subject can be removed.
	UE_API bool CanRemoveSubject() const;
	// Returns whethera subject can be paused
	UE_API bool CanPauseSubject() const;
	// Pauses/Unpauses a subject.
	UE_API void HandlePauseSubject();
	// Handles the remove subject command.
	UE_API void HandleRemoveSubject();
	// Recreates the source list data behind the list view.
	UE_API void RebuildSourceList();
	// Recreates the subject list data behind the tree view.
	UE_API void RebuildSubjectList();
	// Handles source selection changing.
	UE_API void OnSourceSelectionChangedHandler(FLiveLinkSourceUIEntryPtr Entry, ESelectInfo::Type SelectionType) const;
	// Hadnles subject selection changing.
	UE_API void OnSubjectSelectionChangedHandler(FLiveLinkSubjectUIEntryPtr SubjectEntry, ESelectInfo::Type SelectInfo);

	// Sources and subjects live in different tabs in LiveLink Hub.
	bool bSeparateSourcesSubjects = false;

public:
	// Sources view
	TSharedPtr<FLiveLinkSourcesView> SourcesView;
	// Subjects view
	TSharedPtr<FLiveLinkSubjectsView> SubjectsView;
	// Reference to connection settings struct details panel
	TSharedPtr<IDetailsView> SourcesDetailsView;
	// Reference to the data value struct details panel
	TSharedPtr<SLiveLinkDataView> SubjectsDetailsView;
	// LiveLink Client
	FLiveLinkClient* Client;
	// Handle to delegate when client sources list has changed
	FDelegateHandle OnSourcesChangedHandle;
	// Handle to delegate when a client subjects list has changed
	FDelegateHandle OnSubjectsChangedHandle;
	// Guard from reentrant selection
	mutable bool bSelectionChangedGuard = false;
	// Command list
	TSharedPtr<FUICommandList> CommandList;
	// Delegate called when the subject selection changes
	FOnSubjectSelectionChanged SubjectSelectionChangedDelegate;
};

#undef UE_API
