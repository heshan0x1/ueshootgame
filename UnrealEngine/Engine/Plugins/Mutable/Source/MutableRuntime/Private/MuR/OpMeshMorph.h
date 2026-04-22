// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class FName;

namespace UE::Mutable::Private
{
	class FMesh;
}


namespace UE::Mutable::Private
{
	/** Morph Parameter Meshes. Mesh contains the deltas in morph buffer. Deltas are compressed using the engine format. */
	void MeshMorph(FMesh* Mesh, const FName& MorphName, float Factor);

	
	/** Morph Compiled Meshes. MaxMesh contains de deltas in the position buffer. */
	void MeshMorph(FMesh* BaseMesh, const FMesh* MaxMesh, float Factor); // TODO Use the engine compressed format. This means changing how be compile morphs.
}
