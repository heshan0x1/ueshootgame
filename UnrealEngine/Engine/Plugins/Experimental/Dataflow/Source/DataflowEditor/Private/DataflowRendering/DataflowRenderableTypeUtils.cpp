// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataflowRendering/DataflowRenderableTypeUtils.h"
#include "DataflowRendering/DataflowRenderableComponents.h"

#include "Components/DynamicMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"

#include "GeometryCollection/Facades/CollectionMeshFacade.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/ManagedArrayCollection.h"

#include "DynamicMeshToMeshDescription.h"
#include "MeshDescriptionUVsToDynamicMesh.h"
#include "StaticMeshAttributes.h"

#include "Dataflow/DataflowEditorStyle.h"

namespace UE::Dataflow::Rendering
{
	namespace Private
	{
		constexpr float UvScaleFactor = 100.f;

		FVector3d ConvertUvToPosition(const FVector2f& Uv)
		{
			return FVector3d((1 - Uv.Y) * UvScaleFactor, (Uv.X * UvScaleFactor), 0);
		}
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	UMaterial* GetVertexMaterial()
	{
		return FDataflowEditorStyle::Get().VertexMaterial;
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	TSharedPtr<UE::Geometry::FDynamicMesh3> GenerateUvMesh(const FMeshDescription& MeshDescription, int32 UvChannel)
	{
		UE::Geometry::FMeshDescriptionUVsToDynamicMesh Converter;
		Converter.UVLayerIndex = UvChannel;
		Converter.ScaleFactor = Private::UvScaleFactor;
		return Converter.GetUVMesh(&MeshDescription);
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	TSharedPtr<UE::Geometry::FDynamicMesh3> GenerateUvMesh(const UStaticMesh& StaticMesh, int32 UvChannel)
	{
		if (const FMeshDescription* MeshDescription = StaticMesh.GetMeshDescription(0))
		{
			return GenerateUvMesh(*MeshDescription, UvChannel);
		}
		return {};
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	TSharedPtr<UE::Geometry::FDynamicMesh3> GenerateUvMesh(const USkeletalMesh& SkeletalMesh, int32 UvChannel)
	{
		if (const FMeshDescription* MeshDescription = SkeletalMesh.GetMeshDescription(0))
		{
			return GenerateUvMesh(*MeshDescription, UvChannel);
		}
		return {};
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	TSharedPtr<UE::Geometry::FDynamicMesh3> GenerateUvMesh(const UE::Geometry::FDynamicMesh3& DynamicMesh, int32 UvChannel)
	{
		// first create  mesh description for this dynamic mesh
		FMeshDescription MeshDescription;
		{
			FStaticMeshAttributes Attributes(MeshDescription);
			Attributes.Register();
			FConversionToMeshDescriptionOptions ConverterOptions;
			FDynamicMeshToMeshDescription Converter(ConverterOptions);
			Converter.Convert(&DynamicMesh, MeshDescription);
		}

		// generate the UV mesh from it 
		return GenerateUvMesh(MeshDescription, UvChannel);
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	TSharedPtr<UE::Geometry::FDynamicMesh3> GenerateUvMesh(const FManagedArrayCollection& Collection, int32 UvChannel)
	{
		const GeometryCollection::Facades::FCollectionUVFacade UVFacade(Collection);
		const GeometryCollection::Facades::FCollectionMeshFacade MeshFacade(Collection);
		if (!UVFacade.IsValid() || !MeshFacade.IndicesAttribute.IsValid())
		{
			return {};
		}

		if (!UVFacade.FindUVLayer(UvChannel))
		{
			return {};
		}

		TSharedPtr<FDynamicMesh3> MeshOut = MakeShared<FDynamicMesh3>();

		// create vertices from Uvs
		MeshOut->BeginUnsafeVerticesInsert();
		const TManagedArray<FVector2f>& UVs = UVFacade.GetUVLayer(UvChannel);
		for (int32 UvIndex = 0; UvIndex < UVs.Num(); ++UvIndex)
		{
			MeshOut->InsertVertex(UvIndex, Private::ConvertUvToPosition(UVs[UvIndex]), true);
		}
		MeshOut->EndUnsafeVerticesInsert();

		// connect them using the regular triangles
		MeshOut->BeginUnsafeTrianglesInsert();
		for (int32 TriIndex = 0; TriIndex < MeshFacade.IndicesAttribute.Num(); ++TriIndex)
		{
			const bool bIsVisible = (MeshFacade.VisibleAttribute.IsValid()) ? MeshFacade.VisibleAttribute.Get()[TriIndex] : true;
			if (bIsVisible)
			{
				const FIntVector& Triangle = MeshFacade.IndicesAttribute[TriIndex];
				MeshOut->InsertTriangle(TriIndex, { Triangle.X, Triangle.Y, Triangle.Z }, 0, true);
			}
		}
		MeshOut->EndUnsafeTrianglesInsert();

		return MeshOut;
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void SetUvMaterial(UDynamicMeshComponent& Component, int32 MaterialIndex)
	{
		UMaterialInterface* UvMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/UVEditor/Materials/UVEditor_UnwrapMaterial"));
		Component.SetMaterial(MaterialIndex, UvMaterial);
		Component.SetTwoSided(true); // need to be two sided for the Uv display
	}

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////
	UDynamicMeshComponent* AddDynamicMeshComponent(UE::Geometry::FDynamicMesh3&& DynamicMesh, FRenderableComponents& OutComponents)
	{
		if (UDynamicMeshComponent* Component = OutComponents.AddNewComponent<UDynamicMeshComponent>())
		{
			Component->SetMesh(MoveTemp(DynamicMesh));
			return Component;
		}
		return nullptr;
	}
}
