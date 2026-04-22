// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"

#define UE_API METAHUMANSEQUENCER_API

class FMetaHumanSequencerPlaybackContext
	: public TSharedFromThis<FMetaHumanSequencerPlaybackContext>
{
public:
	UE_API UObject* GetPlaybackContext() const;

private:
	UE_API class UWorld* ComputePlaybackContext() const;

	mutable TWeakObjectPtr<class UWorld> WeakCurrentContext;
};

#undef UE_API
