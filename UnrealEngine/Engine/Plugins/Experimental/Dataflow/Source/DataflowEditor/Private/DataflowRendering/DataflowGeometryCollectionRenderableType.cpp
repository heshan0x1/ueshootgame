// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataflowRendering/DataflowGeometryCollectionRenderableType.h"

#include "Components/DynamicMeshComponent.h"

#include "Dataflow/DataflowRenderingViewMode.h"

#include "DataflowRendering/DataflowRenderableComponents.h"
#include "DataflowRendering/DataflowRenderableType.h"
#include "DataflowRendering/DataflowRenderableTypeInstance.h"
#include "DataflowRendering/DataflowRenderableTypeRegistry.h"
#include "DataflowRendering/DataflowRenderableTypeUtils.h"

#include "GeometryCollection/Facades/CollectionMeshFacade.h"
#include "GeometryCollection/GeometryCollectionUtility.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/ManagedArrayCollection.h"

#include "GeometryCollection/Facades/CollectionCurveFacade.h"

#include "UObject/ObjectPtr.h"

#include "PlanarCut.h" // for ConvertGeometryCollectionToDynamicMesh
#include "Dataflow/DataflowEditorStyle.h"

namespace UE::Dataflow::Private
{
	int32 CurveSurfaceVertexThreshold = 50000;
	FAutoConsoleVariableRef CVarCurveSurfaceVertexThreshold(TEXT("p.Dataflow.Curve.Surface.VertexThreshold"), CurveSurfaceVertexThreshold, TEXT("Vertex limit used to reduce curve based rendering in the contruction viewport for performance control"));

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class FGeometryCollectionSurfaceRenderableType : public IRenderableType
	{
		UE_DATAFLOW_IRENDERABLE_PRIMARY_TYPE(FManagedArrayCollection, Collection);
		UE_DATAFLOW_IRENDERABLE_RENDER_GROUP(Surface);
		UE_DATAFLOW_IRENDERABLE_VIEW_MODE(FDataflowConstruction3DViewMode);

		virtual bool CanRender(const FRenderableTypeInstance& Instance) const
		{
			const FManagedArrayCollection& Collection = GetCollection(Instance);
			return Collection.HasGroup(FGeometryCollection::GeometryGroup);
		}

		virtual void GetPrimitiveComponents(const FRenderableTypeInstance& Instance, FRenderableComponents& OutComponents) const override
		{
			const FManagedArrayCollection& Collection = GetCollection(Instance);

			// TODO(dataflow) : use a facade or a more generic way to convert a collection to a dynamic mesh
			const TManagedArray<FTransform3f>* TransformAttribute = Collection.FindAttribute<FTransform3f>(FTransformCollection::TransformAttribute, FTransformCollection::TransformGroup);
			const TManagedArray<int32>* TransformIndexAttribute = Collection.FindAttribute<int32>(FGeometryCollection::TransformIndexAttribute, FGeometryCollection::GeometryGroup);
			const TManagedArray<FString>* BoneNameAttribute = Collection.FindAttribute<FString>(FTransformCollection::BoneNameAttribute, FTransformCollection::TransformGroup);

			if (TransformAttribute && TransformIndexAttribute)
			{
				TArray<FDynamicMesh3> DynamicMeshes;
				if (Instance.HasUptoDateCachedValue())
				{
					const TArray<FDynamicMesh3>& CachedDynamicMeshes = Instance.GetCachedValue<TArray<FDynamicMesh3>>();
					DynamicMeshes.SetNum(CachedDynamicMeshes.Num());

					// copying dynamic meshes can be expensive let's parallelize it 
					ParallelFor(DynamicMeshes.Num(),
						[&CachedDynamicMeshes, &DynamicMeshes](int32 Index)
						{
							DynamicMeshes[Index] = CachedDynamicMeshes[Index];
						});
				}
				else
				{
					// TODO(dataflow) : need a version of ConvertGeometryCollectionToDynamicMesh that only takes a managed array collection
					if (TUniquePtr<FGeometryCollection> GeomCollection = TUniquePtr<FGeometryCollection>(Collection.NewCopy<FGeometryCollection>()))
					{
						CullCurvesIfNeeded(*GeomCollection);

						// prepare the dynamic meshes
						DynamicMeshes.SetNum(TransformAttribute->Num());

						TArrayView<const FTransform3f> BoneTransforms = MakeArrayView(TransformAttribute->GetConstArray());

						// convert to dynamic meshes in parallel
						ParallelFor(DynamicMeshes.Num(),
							[&DynamicMeshes, &BoneTransforms, &GeomCollection](int32 TransformIndex)
							{
								FDynamicMesh3& DynamicMesh = DynamicMeshes[TransformIndex];
								TArrayView<const int32> TransformIndicesToConvert(&TransformIndex, 1);

								FTransform UnusedTransform;
								constexpr bool bWeldEdges = false; // disabling this as this can take 43% of the conversion time for large meshes
								::ConvertGeometryCollectionToDynamicMesh(DynamicMesh, UnusedTransform, false, *GeomCollection, bWeldEdges, BoneTransforms, true, TransformIndicesToConvert);
							});
					}
					Instance.CacheValue(DynamicMeshes);
				}
				// only create components for the one that have vertices
				for (int32 TransformIndex = 0; TransformIndex < TransformAttribute->Num(); ++TransformIndex)
				{
					if (DynamicMeshes[TransformIndex].VertexCount() > 0)
					{
						static const FString DefaultBoneString = TEXT("Geometry_Collection_Bone");
						const FString ComponentNamePostFix = BoneNameAttribute ? (*BoneNameAttribute)[TransformIndex] : DefaultBoneString;
						const FName ComponentName = FName(FString::Printf(TEXT("[%03d]_%s"), TransformIndex, *ComponentNamePostFix));
						if (UDynamicMeshComponent* Component = OutComponents.AddNewComponent<UDynamicMeshComponent>(ComponentName))
						{
							Component->SetCastShadow(false);
							Component->SetMesh(MoveTemp(DynamicMeshes[TransformIndex]));
							Component->SetOverrideRenderMaterial(FDataflowEditorStyle::Get().VertexMaterial);
						}
					}
				}
			}
		}

		void CullCurvesIfNeeded(FGeometryCollection& GeomCollection) const
		{
			// Performance control for curve based geometry that can ghave a lot of geometry
					// if the threshold is reached, we only render a certain percentage of the curves
			const int32 NumVertices = GeomCollection.NumElements(FGeometryCollection::VerticesGroup);
			if (CurveSurfaceVertexThreshold > 0 && NumVertices > CurveSurfaceVertexThreshold)
			{
				const float CullingRatio = 1.0 - FMath::Clamp((float)CurveSurfaceVertexThreshold / (float)NumVertices, 0, 1);

				GeometryCollection::Facades::FCollectionCurveGeometryFacade CurveFacade(GeomCollection);
				const bool bHasCurves = CurveFacade.IsValid();
				if (bHasCurves)
				{
					TManagedArray<bool>& FaceVisible = GeomCollection.AddAttribute<bool>(FGeometryCollection::FaceVisibleAttribute, FGeometryCollection::FacesGroup);

					FRandomStream Random(0);

					// assumption that vertex index are Pointindex * 2
					const TArray<int32>& CurvePointOffsets = CurveFacade.GetCurvePointOffsets();
					int32 StartFace = 0;
					const int32 NumCurves = CurvePointOffsets.Num();
					for (int32 CurveIndex = 0; CurveIndex < NumCurves; ++CurveIndex)
					{
						const float CurveRand = Random.GetFraction();
						const bool bCurveVisible = (CurveRand > CullingRatio);

						const int32 CurvPointIndex = CurvePointOffsets[CurveIndex];
						const int32 PrevPointIndex = (CurveIndex > 0) ? CurvePointOffsets[CurveIndex - 1] : 0;
						const int32 CurveLength = FMath::Max(0, CurvPointIndex - PrevPointIndex);
						if (CurveLength > 0)
						{
							const int32 CurveNumFaces = (CurveLength - 1) * 2;
							for (int32 FaceIndex = 0; FaceIndex < CurveNumFaces; ++FaceIndex)
							{
								const int32 GlobalFaceIndex = StartFace + FaceIndex;
								if (FaceVisible.IsValidIndex(GlobalFaceIndex))
								{
									FaceVisible[GlobalFaceIndex] = bCurveVisible;
								}
							}
							StartFace += CurveNumFaces;
						}
					}
				}
			}
		}
	};

	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class FGeometryCollectionUvsRenderableType : public IRenderableType
	{
		UE_DATAFLOW_IRENDERABLE_PRIMARY_TYPE(FManagedArrayCollection, Collection);
		UE_DATAFLOW_IRENDERABLE_RENDER_GROUP(Uvs);
		UE_DATAFLOW_IRENDERABLE_VIEW_MODE(FDataflowConstructionUVViewMode);

		virtual bool CanRender(const FRenderableTypeInstance& Instance) const
		{
			const FManagedArrayCollection& Collection = GetCollection(Instance);
			return Collection.HasGroup(FGeometryCollection::GeometryGroup);
		}

		virtual void GetPrimitiveComponents(const FRenderableTypeInstance& Instance, FRenderableComponents& OutComponents) const override
		{
			const FManagedArrayCollection& Collection = GetCollection(Instance);

			Rendering::AddUvDynamicMeshComponent(Collection, 0, OutComponents);
		}
	};

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void RegisterGeometryCollectionRenderableTypes()
	{
		UE_DATAFLOW_REGISTER_RENDERABLE_TYPE(FGeometryCollectionSurfaceRenderableType);
		UE_DATAFLOW_REGISTER_RENDERABLE_TYPE(FGeometryCollectionUvsRenderableType);
	}
}
