// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrameAnimationData.h"
#include "MetaHumanCoreCustomVersion.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FrameAnimationData)


bool FFrameAnimationData::ContainsData() const
{
	return !AnimationData.IsEmpty();
}

void FFrameAnimationData::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FMetaHumanCoreCustomVersion::GUID);

	Ar << Pose;
	Ar << AnimationData;
	Ar << AnimationQuality;

	if (Ar.CustomVer(FMetaHumanCoreCustomVersion::GUID) >= FMetaHumanCoreCustomVersion::AddAudioProcessingTypeToFrameData)
	{
		Ar << AudioProcessingMode;
	}
}
