// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#define UE_API METAHUMANCORETECH_API



// Functions to convert a transformation that is applied to a 
// standalone head skel mesh into a transformation to apply to the
// head bone of a full MetaHuman (that uses that head mesh) such
// that the head remains in a constant pose.

class FMetaHumanHeadTransform
{
public:

	static UE_API FTransform MeshToBone(const FTransform& InTransform);
	static UE_API FTransform BoneToMesh(const FTransform& InTransform);

	static UE_API FTransform HeadToRoot(const FTransform& InTransform);
	static UE_API FTransform RootToHead(const FTransform& InTransform);
};

#undef UE_API
