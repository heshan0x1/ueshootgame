// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "AnimGraphNode_Base.h"
#include "AnimNodes/AnimNode_RetargetPoseFromMesh.h"
#include "AnimGraphNode_RetargetPoseFromMesh.generated.h"

#define UE_API IKRIGDEVELOPER_API

class FPrimitiveDrawInterface;
class USkeletalMeshComponent;

// Facilitates runtime retargeting of an input pose using a user-specified IK Retargeter asset
// Supports input poses from the animation graph, or from other skeletal mesh components
UCLASS(MinimalAPI)
class UAnimGraphNode_RetargetPoseFromMesh : public UAnimGraphNode_Base
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_RetargetPoseFromMesh Node;

public:
	// UEdGraphNode interface
	UE_API virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	UE_API virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	UE_API virtual void ValidateAnimNodeDuringCompilation(USkeleton* ForSkeleton, FCompilerResultsLog& MessageLog);
	// End of UEdGraphNode interface

	// UAnimGraphNode_Base interface
	UE_API virtual void PreloadRequiredAssets() override;
	UE_API virtual void CopyNodeDataToPreviewNode(FAnimNode_Base* AnimNode) override;
	UE_API virtual FEditorModeID GetEditorMode() const override;
	UE_API virtual void Draw(FPrimitiveDrawInterface* PDI, USkeletalMeshComponent* PreviewSkelMeshComp) const override;
	UE_API virtual void CustomizePinData(UEdGraphPin* Pin, FName SourcePropertyName, int32 ArrayIndex) const override;
	virtual bool UsingCopyPoseFromMesh() const override { return true; };
	// End of UAnimGraphNode_Base interface

	// UK2Node interface
	UE_API virtual UObject* GetJumpTargetForDoubleClick() const override;
	// End of UK2Node interface

	static UE_API const FName AnimModeName;
};

#undef UE_API
