// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneTrack.h"
#include "MVVM/ViewModelPtr.h"

#define UE_API SEQUENCER_API

class ISequencer;

namespace UE::Sequencer
{
	class IObjectBindingExtension;
	class IOutlinerExtension;
	class ITrackExtension;
}

using FSequencerTrackFilterType = UE::Sequencer::FViewModelPtr;

/** Represents a cache between nodes for a filter operation. */
struct FSequencerFilterData
{
	UE_API FSequencerFilterData(const FString& InRawFilterText);

	UE_API bool operator==(const FSequencerFilterData& InRhs) const;
	UE_API bool operator!=(const FSequencerFilterData& InRhs) const;

	UE_API void Reset();

	UE_API FString GetRawFilterText() const;

	UE_API uint32 GetDisplayNodeCount() const;
	UE_API uint32 GetTotalNodeCount() const;

	UE_API uint32 GetFilterInCount() const;
	UE_API uint32 GetFilterOutCount() const;

	UE_API void IncrementTotalNodeCount();

	UE_API void FilterInNode(UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension> InNodeWeak);
	UE_API void FilterOutNode(UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension> InNodeWeak);

	UE_API void FilterInParentChildNodes(const UE::Sequencer::TViewModelPtr<UE::Sequencer::IOutlinerExtension>& InNode
		, const bool bInIncludeSelf, const bool bInIncludeParents, const bool bInIncludeChildren = false);

	UE_API void FilterInNodeWithAncestors(const UE::Sequencer::TViewModelPtr<UE::Sequencer::IOutlinerExtension>& InNode);

	UE_API bool ContainsFilterInNodes(const FSequencerFilterData& InOtherData) const;

	UE_API bool IsFilteredOut(const UE::Sequencer::TViewModelPtr<UE::Sequencer::IOutlinerExtension>& InNode) const;

	UE_API UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::ITrackExtension> ResolveTrack(FSequencerTrackFilterType InNode);
	UE_API UMovieSceneTrack* ResolveMovieSceneTrackObject(FSequencerTrackFilterType InNode);
	UE_API UObject* ResolveTrackBoundObject(ISequencer& InSequencer, FSequencerTrackFilterType InNode);

	TMap<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>, UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::ITrackExtension>> ResolvedTracks;
	TMap<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>, TWeakObjectPtr<UMovieSceneTrack>> ResolvedTrackObjects;
	TMap<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>, TWeakObjectPtr<>> ResolvedObjects;

protected:
	FString RawFilterText;

	uint32 TotalNodeCount = 0;

	/** Nodes to be displayed in the UI */
	TSet<UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::IOutlinerExtension>> FilterInNodes;
};

#undef UE_API
