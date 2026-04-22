// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/SecureHash.h"

#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

#define UE_API METAHUMANIDENTITY_API


enum class EIdentityPoseType : uint8;
enum class EIdentityInvalidationState : uint8;

enum class EIdentityEditType: uint8
{
	Add,
	Remove,
	ChangeProperty,
	Count
};

// Teeth fitting state has to be checked separately as it doesn't prevent you from further progressing
enum class EIdentityProgressState : uint8
{
	Solve, AR, PrepareForPerformance, Complete, Invalid
};

struct FIdentityHashes
{
	FSHAHash SolveStateHash;
	FSHAHash TeethStateHash;
};

class FMetaHumanIdentityStateValidator : public TSharedFromThis<FMetaHumanIdentityStateValidator>
{

public:
	UE_API FMetaHumanIdentityStateValidator();

	UE_API void PostAssetLoadHashInitialization(TWeakObjectPtr<class UMetaHumanIdentity> InIdentity);
	
	UE_API void UpdateIdentityProgress();

	UE_API void MeshConformedStateUpdate();
	UE_API void MeshAutoriggedUpdate();
	UE_API void MeshPreparedForPerformanceUpdate() const;
	UE_API void TeethFittedUpdate();

	//void PopulateIdentityValidationTooltip();

	UE_API FText GetInvalidationStateToolTip();

private:

	UE_API void CalculateIdentityHashes();
	UE_API void BindToContourDataChangeDelegates();
	UE_API void InvalidateIdentityWhenContoursChange();
	UE_API void InvalidateTeethWhenContoursChange();
	UE_API void UpdateIdentityInvalidationState();
	UE_API void UpdateCurrentProgressState();

	UE_API FSHAHash GetHashForString(const FString& InStringToHash) const;
	UE_API FSHAHash GetSolverStateHash() const;
	UE_API FSHAHash GetTeethStateHash() const;
	
	FText IdentityStateTooltip;
	FIdentityHashes IdentityHashes;
	EIdentityProgressState CurrentProgress;
	
	TWeakObjectPtr<class UMetaHumanIdentity> Identity;
	
	TPair<int32, FText> SolveText;
	TPair<int32, FText> MeshToMetahumanText;
	TPair<int32, FText> FitTeethText;
	TPair<int32, FText> PrepareForPerformanceText;

	bool bPrepareForPerformanceEnabled = false;

};

#undef UE_API
