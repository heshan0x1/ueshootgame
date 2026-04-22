// Copyright Epic Games, Inc. All Rights Reserved.

#include "Module/AnimNextSkeletalMeshComponentReferenceComponent.h"

#include "Components/SkeletalMeshComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextSkeletalMeshComponentReferenceComponent)

FAnimNextSkeletalMeshComponentReferenceComponent::FAnimNextSkeletalMeshComponentReferenceComponent()
{
	ComponentType = USkeletalMeshComponent::StaticClass();
	OnInitializeHelper(StaticStruct());
}

USkeletalMeshComponent* FAnimNextSkeletalMeshComponentReferenceComponent::GetComponent() const
{
	return Cast<USkeletalMeshComponent>(Component);
}
