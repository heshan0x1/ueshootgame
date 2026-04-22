// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

struct FManagedArrayCollection;
struct FMeshDescription;

class UDynamicMeshComponent;
class USkeletalMesh;
class UStaticMesh;
class UMaterial;

#define UE_API DATAFLOWEDITOR_API

namespace UE::Geometry
{
	class FDynamicMesh3;
}

namespace UE::Dataflow
{
	struct FRenderableComponents;

	namespace Rendering
	{
		/** Get the default vertex material for dynamic meshes */
		UE_API UMaterial* GetVertexMaterial();
		
		/** Generate a dynamic mesh from a mesh description UV data */
		TSharedPtr<UE::Geometry::FDynamicMesh3> GenerateUvMesh(const FMeshDescription& MeshDescription, int32 UvChannel);

		/** Generate a dynamic mesh from a static mesh UV data */
		TSharedPtr<UE::Geometry::FDynamicMesh3> GenerateUvMesh(const UStaticMesh& StaticMesh, int32 UvChannel);

		/** Generate a dynamic mesh from a skeletal mesh UV data */
		TSharedPtr<UE::Geometry::FDynamicMesh3> GenerateUvMesh(const USkeletalMesh& SkeletalMesh, int32 UvChannel);

		/** Generate a dynamic mesh from a dynamic mesh UV data */
		TSharedPtr<UE::Geometry::FDynamicMesh3> GenerateUvMesh(const UE::Geometry::FDynamicMesh3& DynamicMesh, int32 UvChannel);

		/** Generate a dynamic mesh from a managed array collection UV data */
		TSharedPtr<UE::Geometry::FDynamicMesh3> GenerateUvMesh(const FManagedArrayCollection& Collection, int32 UvChannel);

		/** Make a dynamic mesh component and add it to the OutComponents */
		UDynamicMeshComponent* AddDynamicMeshComponent(UE::Geometry::FDynamicMesh3&& DynamicMesh, FRenderableComponents& OutComponents);

		/** set UV material on a primitive component */
		void SetUvMaterial(UDynamicMeshComponent& Component, int32 MaterialIndex);

		/** Generate a Uv mesh from a templated source and make a dynamic mesh component out of it and add it to the OutComponents */
		template<typename T>
		void AddUvDynamicMeshComponent(const T& Source, int32 UvIndex, FRenderableComponents& OutComponents)
		{
			if (TSharedPtr<UE::Geometry::FDynamicMesh3> UvMesh = GenerateUvMesh(Source, UvIndex))
			{
				if (UDynamicMeshComponent* Component = AddDynamicMeshComponent(MoveTemp(*UvMesh), OutComponents))
				{
					SetUvMaterial(*Component, 0);
				}
			}
		}
	}
}

#undef UE_API
