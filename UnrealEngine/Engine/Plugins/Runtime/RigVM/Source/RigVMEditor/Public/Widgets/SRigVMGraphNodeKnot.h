// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SGraphNodeKnot.h"

#define UE_API RIGVMEDITOR_API

class SRigVMGraphNodeKnot : public SGraphNodeKnot
{
	SLATE_BEGIN_ARGS(SRigVMGraphNodeKnot) {} 
	SLATE_END_ARGS()

	UE_API void Construct(const FArguments& InArgs, UEdGraphNode* InKnot);

	// SGraphNode interface
	UE_API virtual void EndUserInteraction() const override;
	UE_API virtual void MoveTo(const FVector2f& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override;

	UE_API void HandleNodeBeginRemoval();
	UE_API void UpdateGraphNode() override;
};

#undef UE_API
