// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCaptureSource.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanCaptureSource)

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void FMetaHumanCaptureVoidResult::SetResult(TResult<void, FMetaHumanCaptureError> InResult)
{
	bIsValid = InResult.IsValid();

	if (!bIsValid)
	{
		FMetaHumanCaptureError Error = InResult.ClaimError();
		Code = Error.GetCode();
		Message = Error.GetMessage();
	}
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

#if WITH_EDITOR
void UMetaHumanCaptureSource::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	MinDistance = FMath::Clamp(MinDistance, 0.0, MaxDistance);
}
#endif

void UMetaHumanCaptureSource::PostLoad()
{
	Super::PostLoad();

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (!DeviceAddress_DEPRECATED.IsEmpty())
	{
		DeviceIpAddress.IpAddress = DeviceAddress_DEPRECATED;
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

