// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/NameTypes.h"

namespace UE::SceneState::Metadata
{
#if WITH_EDITOR
	const FLazyName IsRefToArray(TEXT("IsRefToArray"));
	const FLazyName CanRefToArray(TEXT("CanRefToArray"));
	const FLazyName RefType(TEXT("RefType"));
#endif
}
