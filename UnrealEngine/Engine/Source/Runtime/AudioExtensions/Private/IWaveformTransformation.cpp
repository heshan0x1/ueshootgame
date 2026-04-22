// Copyright Epic Games, Inc. All Rights Reserved.


#include "IWaveformTransformation.h"
#include "Templates/SharedPointer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(IWaveformTransformation)

TArray<Audio::FTransformationPtr> UWaveformTransformationChain::CreateTransformations() const
{
	TArray<Audio::FTransformationPtr> TransformationPtrs;

	for(UWaveformTransformationBase* Transformation : Transformations)
	{
		if(Transformation)
		{
			TransformationPtrs.Add(Transformation->CreateTransformation());
		}
	}
	
	return TransformationPtrs;
}

#if WITH_EDITOR
void UWaveformTransformationBase::PostEditUndo()
{
	Super::PostEditUndo();

	constexpr bool bMarkFileDirty = true;
	OnTransformationChanged.ExecuteIfBound(bMarkFileDirty);
}

void UWaveformTransformationBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	constexpr bool bMarkFileDirty = true;
	OnTransformationChanged.ExecuteIfBound(bMarkFileDirty);
}

void UWaveformTransformationBase::NotifyPropertyChange(FProperty* Property)
{
	if (Property)
	{
		FPropertyChangedEvent PropertyChangedEvent(Property);
		PostEditChangeProperty(PropertyChangedEvent);
	}
}
#endif //WITH_EDITOR
