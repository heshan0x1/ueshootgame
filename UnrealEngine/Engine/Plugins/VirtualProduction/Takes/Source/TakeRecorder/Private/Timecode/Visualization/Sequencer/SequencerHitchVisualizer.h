// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/IDelegateInstance.h"
#include "Templates/SharedPointer.h"
#include "Templates/UnrealTemplate.h"

#if WITH_EDITOR
class FExtender;
class ISequencer;

namespace UE::TakeRecorder
{
class FHitchViewModel_AnalyzedData;

/** Hooks into Sequencer and extends the UI to visualize hitches. */
class FSequencerHitchVisualizer : public FNoncopyable
{
public:
	
	FSequencerHitchVisualizer();
	~FSequencerHitchVisualizer();

private:

	/** Handles for OnSequencerCreated. */
	FDelegateHandle OnSequencerCreatedHandle;
	
	/** Extends Sequencer's view options */
	const TSharedPtr<FExtender> ViewOptionsExtender;

	/** The sequencers we have customized thus far. */
	TArray<TWeakPtr<ISequencer>> CustomizedSequencers;

	/** Invoked when a sequencer is created: extends the UI with visualization UI, if needed. */
	void OnSequencerCreated(TSharedRef<ISequencer> InSequencer);
};
}
#endif


