// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/Guid.h"
#include "Templates/SubclassOf.h"
#include "Templates/UniquePtr.h"
#include "Widgets/SWidget.h"
#include "ISequencer.h"
#include "MovieSceneTrack.h"
#include "ISequencerSection.h"
#include "MovieSceneTrackEditor.h"
#include "SequencerCoreFwd.h"

#define UE_API MOVIESCENETOOLS_API

struct FAssetData;
struct FMovieSceneTimeWarpChannel;
struct FMovieSceneSequenceTransform;

class FMenuBuilder;
class FSequencerSectionPainter;
class UAnimSeqExportOption;
class UAnimSequenceBase;
class UMovieSceneCommonAnimationTrack;
class UMovieSceneSkeletalAnimationSection;
class USkeletalMeshComponent;
class USkeleton;

namespace UE::Sequencer
{

class ITrackExtension;

/**
 * Tools for animation tracks
 */
class FCommonAnimationTrackEditor : public FMovieSceneTrackEditor, public FGCObject
{
public:
	/**
	 * Constructor
	 *
	 * @param InSequencer The sequencer instance to be used by this tool
	 */
	UE_API FCommonAnimationTrackEditor( TSharedRef<ISequencer> InSequencer );

	/** Virtual destructor. */
	virtual ~FCommonAnimationTrackEditor() { }

	//~ FGCObject
	UE_API virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FCommonAnimationTrackEditor");
	}

	/**
	* Keeps track of how many skeletal animation track editors we have*
	*/
	static UE_API int32 NumberActive;

	static UE_API USkeletalMeshComponent* AcquireSkeletalMeshFromObjectGuid(const FGuid& Guid, TSharedPtr<ISequencer> SequencerPtr);
	static UE_API USkeleton* AcquireSkeletonFromObjectGuid(const FGuid& Guid, TSharedPtr<ISequencer> SequencerPtr);

public:

	// ISequencerTrackEditor interface

	UE_API virtual FText GetDisplayName() const override;
	UE_API virtual void BuildObjectBindingContextMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass) override;
	UE_API virtual void BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass) override;
	UE_API virtual bool HandleAssetAdded(UObject* Asset, const FGuid& TargetObjectGuid) override;
	UE_API virtual TSharedRef<ISequencerSection> MakeSectionInterface( UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding ) override;
	UE_API virtual TSharedPtr<SWidget> BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params) override;
	UE_API virtual bool OnAllowDrop(const FDragDropEvent& DragDropEvent, FSequencerDragDropParams& DragDropParams) override;
	UE_API virtual FReply OnDrop(const FDragDropEvent& DragDropEvent, const FSequencerDragDropParams& DragDropParams) override;
	UE_API virtual void OnInitialize() override;
	UE_API virtual void OnRelease() override;

protected:

	virtual TSubclassOf<UMovieSceneCommonAnimationTrack> GetTrackClass() const = 0;

	/** Animation sub menu */
	UE_API TSharedRef<SWidget> BuildAddAnimationSubMenu(FGuid ObjectBinding, USkeleton* Skeleton, UE::Sequencer::TWeakViewModelPtr<UE::Sequencer::ITrackExtension> TrackModel);
	UE_API TSharedRef<SWidget> BuildAnimationSubMenu(FGuid ObjectBinding, USkeleton* Skeleton, UMovieSceneTrack* Track);
	UE_API void AddAnimationSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings, USkeleton* Skeleton, UMovieSceneTrack* Track);

	/** Filter only compatible skeletons */
	UE_API bool FilterAnimSequences(const FAssetData& AssetData, USkeleton* Skeleton);

	/** Animation sub menu filter function */
	UE_API bool ShouldFilterAsset(const FAssetData& AssetData);

	/** Animation asset selected */
	UE_API void OnAnimationAssetSelected(const FAssetData& AssetData, TArray<FGuid> ObjectBindings, UMovieSceneTrack* Track);

	/** Animation asset enter pressed */
	UE_API void OnAnimationAssetEnterPressed(const TArray<FAssetData>& AssetData, TArray<FGuid> ObjectBindings, UMovieSceneTrack* Track);

	/** Delegate for AnimatablePropertyChanged in AddKey */
	UE_API FKeyPropertyResult AddKeyInternal(FFrameNumber KeyTime, UObject* Object, UAnimSequenceBase* AnimSequence, UMovieSceneTrack* Track, int32 RowIndex);
	
	/** Construct the binding menu*/
	UE_API void ConstructObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings);

	/** Callback to Create the Animation Asset, pop open the dialog */
	UE_API void HandleCreateAnimationSequence(USkeletalMeshComponent* SkelMeshComp, USkeleton* Skeleton, FGuid Binding, bool bCeateSoftLink);

	/** Callback to Creae the Animation Asset after getting the name*/
	UE_API bool CreateAnimationSequence(const TArray<UObject*> NewAssets,USkeletalMeshComponent* SkelMeshComp, FGuid Binding, bool bCreateSoftLink);

	/** Open the linked Anim Sequence*/
	UE_API void OpenLinkedAnimSequence(FGuid Binding);

	/** Can Open the linked Anim Sequence*/
	UE_API bool CanOpenLinkedAnimSequence(FGuid Binding);

	friend class FMovieSceneSkeletalAnimationParamsDetailCustomization;

protected:
	/* Was part of the the section but should be at the track level since it takes the final blended result at the current time, not the section instance value*/
	UE_API bool CreatePoseAsset(const TArray<UObject*> NewAssets, FGuid InObjectBinding);
	UE_API void HandleCreatePoseAsset(FGuid InObjectBinding);
	UE_API bool CanCreatePoseAsset(FGuid InObjectBinding) const;

protected:
	/* For Anim Sequence UI Option with be gc'd*/
	TObjectPtr<UAnimSeqExportOption> AnimSeqExportOption;


protected:
	/* Delegate to handle sequencer changes for auto baking of anim sequences*/
	FDelegateHandle SequencerSavedHandle;
	UE_API void OnSequencerSaved(ISequencer& InSequence);
	FDelegateHandle SequencerChangedHandle;
	UE_API void OnSequencerDataChanged(EMovieSceneDataChangeType DataChangeType);
};


/** Class for animation sections */
class FCommonAnimationSection
	: public ISequencerSection
	, public TSharedFromThis<FCommonAnimationSection>
{
public:

	/** Constructor. */
	UE_API FCommonAnimationSection( UMovieSceneSection& InSection, TWeakPtr<ISequencer> InSequencer);

	/** Virtual destructor. */
	UE_API virtual ~FCommonAnimationSection();

public:

	// ISequencerSection interface

	UE_API virtual UMovieSceneSection* GetSectionObject() override;
	UE_API virtual FText GetSectionTitle() const override;
	UE_API virtual FText GetSectionToolTip() const override;
	UE_API virtual TOptional<FFrameTime> GetSectionTime(FSequencerSectionPainter& InPainter) const override;
	UE_API virtual float GetSectionHeight(const UE::Sequencer::FViewDensityInfo& ViewDensity) const override;
	UE_API virtual FMargin GetContentPadding() const override;
	UE_API virtual int32 OnPaintSection( FSequencerSectionPainter& Painter ) const override;
	UE_API virtual void BeginResizeSection() override;
	UE_API virtual void ResizeSection(ESequencerSectionResizeMode ResizeMode, FFrameNumber ResizeTime) override;
	UE_API virtual void BeginSlipSection() override;
	UE_API virtual void SlipSection(FFrameNumber SlipTime) override;
	UE_API virtual void CustomizePropertiesDetailsView(TSharedRef<IDetailsView> DetailsView, const FSequencerSectionPropertyDetailsViewCustomizationParams& InParams) const override;
	UE_API virtual void BeginDilateSection() override;
	UE_API virtual void DilateSection(const TRange<FFrameNumber>& NewRange, float DilationFactor) override;
	UE_API virtual bool RequestDeleteKeyArea(const TArray<FName>& KeyAreaNamePath) override;
	UE_API virtual void BuildSectionContextMenu(FMenuBuilder& MenuBuilder, const FGuid& InObjectBinding) override;


protected:
	UE_API void FindBestBlendSection(FGuid InObjectBinding);
protected:

	/** The section we are visualizing */
	TWeakObjectPtr<UMovieSceneSkeletalAnimationSection> WeakSection;

	/** Used to draw animation frame, need selection state and local time*/
	TWeakPtr<ISequencer> Sequencer;

	TUniquePtr<FMovieSceneSequenceTransform> InitialDragTransform;
	TUniquePtr<FMovieSceneTimeWarpChannel> PreDilateChannel;
	double PreDilatePlayRate;
};

} // namespace UE::Sequencer

#undef UE_API
