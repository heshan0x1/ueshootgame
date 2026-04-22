// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/MathFwd.h"

namespace UE::Mutable::Private
{
class FMesh;
struct FBoneName;

/**  */
extern void MeshTransformWithBoneInline(FMesh* Mesh, const FMatrix44f& Transform, const FBoneName& BoneName, const float Threshold);

}
