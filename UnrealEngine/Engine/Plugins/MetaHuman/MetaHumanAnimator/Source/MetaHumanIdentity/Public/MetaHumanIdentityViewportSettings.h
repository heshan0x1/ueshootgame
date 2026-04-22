// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MetaHumanViewportSettings.h"

#include "Misc/FrameTime.h"

#include "MetaHumanIdentityViewportSettings.generated.h"

#define UE_API METAHUMANIDENTITY_API

enum class EIdentityPoseType : uint8;

/////////////////////////////////////////////////////
// UMetaHumanIdentityViewportSettings

USTRUCT()
struct FMetaHumanIdentityABViewportState
{
	GENERATED_BODY()

	FMetaHumanIdentityABViewportState()
		: bShowCurrentPose(true)
		, bShowTemplateMesh(false)
	{}

	UPROPERTY(EditAnywhere, Category = "Viewport Settings")
	uint8 bShowCurrentPose : 1;

	UPROPERTY(EditAnywhere, Category = "Viewport Settings")
	uint8 bShowTemplateMesh : 1;
};

USTRUCT()
struct FMetaHumanIdentityPoseState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Viewport Settings")
	int32 SelectedFrame = INDEX_NONE;

	UPROPERTY(EditAnywhere, Category = "Viewport Settings")
	FFrameTime CurrentFrameTime;
};

UENUM()
enum class EIdentityTreeNodeIdentifier
{
	None,
	IdentityRoot,
	TemplateMesh,
	SkeletalMesh,
	FaceNode,
	BodyNode,
	FacePoseList,
	FaceNeutralPose,
	FaceTeethPose
};

UCLASS(MinimalAPI)
class UMetaHumanIdentityViewportSettings
	: public UMetaHumanViewportSettings
{
	GENERATED_BODY()

public:

	UE_API UMetaHumanIdentityViewportSettings();

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Viewport Settings")
	UE_API void ToggleCurrentPoseVisibility(EABImageViewMode InView);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Viewport Settings")
	UE_API bool IsCurrentPoseVisible(EABImageViewMode InView) const;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Viewport Settings")
	UE_API void ToggleTemplateMeshVisibility(EABImageViewMode InView);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Viewport Settings")
	UE_API bool IsTemplateMeshVisible(EABImageViewMode InView) const;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Viewport Settings")
	UE_API void SetSelectedPromotedFrame(EIdentityPoseType InPoseType, int32 InPromotedFrameIndex);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Viewport Settings")
	UE_API int32 GetSelectedPromotedFrame(EIdentityPoseType InPoseType) const;

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Viewport Settings")
	UE_API void SetFrameTimeForPose(EIdentityPoseType InPoseType, const FFrameTime& InFrameTime);

	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Viewport Settings")
	UE_API FFrameTime GetFrameTimeForPose(EIdentityPoseType InPoseType) const;

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Viewport Settings")
	EIdentityTreeNodeIdentifier SelectedTreeNode;

private:

	UPROPERTY(VisibleAnywhere, Category = "Viewport Settings")
	TMap<EIdentityPoseType, FMetaHumanIdentityPoseState> IdentityPosesState;

	UPROPERTY(VisibleAnywhere, Category = "Viewport Settings")
	TMap<EABImageViewMode, FMetaHumanIdentityABViewportState> IdentityViewportState;
};

#undef UE_API
