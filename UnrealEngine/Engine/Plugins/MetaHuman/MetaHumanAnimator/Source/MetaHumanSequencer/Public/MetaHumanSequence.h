// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneSequence.h"
#include "FrameRange.h"

#include "MetaHumanSequence.generated.h"

#define UE_API METAHUMANSEQUENCER_API

/**
 * Movie scene used by the MetaHuman system
 */
UCLASS(MinimalAPI)
class UMetaHumanSceneSequence : public UMovieSceneSequence
{
	GENERATED_BODY()

public:
	UE_API UMetaHumanSceneSequence(const FObjectInitializer& ObjectInitializer);

	//~ UMovieSceneSequence interface
	UE_API virtual void BindPossessableObject(const FGuid& ObjectId, UObject& PossessedObject, UObject* Context) override;
	UE_API virtual bool CanPossessObject(UObject& Object, UObject* InPlaybackContext) const override;
	UE_API virtual void LocateBoundObjects(const FGuid& ObjectId, UObject* Context, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const override;
	UE_API virtual UMovieScene* GetMovieScene() const override;
	UE_API virtual UObject* GetParentObject(UObject* Object) const override;
	UE_API virtual void UnbindPossessableObjects(const FGuid& ObjectId) override;
	UE_API virtual void UnbindObjects(const FGuid& ObjectId, const TArray<UObject*>& InObjects, UObject* Context) override;
	UE_API virtual void UnbindInvalidObjects(const FGuid& ObjectId, UObject* Context) override;

#if WITH_EDITOR
	UE_API virtual FText GetDisplayName() const override;
	UE_API virtual ETrackSupport IsTrackSupportedImpl(TSubclassOf<class UMovieSceneTrack> InTrackClass) const override;
	UE_API void SetTickRate(class UFootageCaptureData* InFootageCaptureData);
#endif

public:
	UPROPERTY()
	TObjectPtr<UMovieScene> MovieScene;

#if WITH_EDITOR
	DECLARE_DELEGATE_FourParams(FGetExcludedFrameInfo, FFrameRate& OutSourceRate, FFrameRangeMap& OutExcludedFramesMap, int32& OutMediaStartFrame, TRange<FFrameNumber>& OutProcessingLimit);

	FGetExcludedFrameInfo GetExcludedFrameInfo;
#endif

private:
	UPROPERTY()
	TMap<FGuid, TObjectPtr<UObject>> Bindings;
};

#undef UE_API
