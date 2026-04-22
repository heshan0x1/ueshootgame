// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataflowRendering/DataflowGeometryCollectionProximityRenderableType.h"

#include "Drawing/LineSetComponent.h"
#include "Drawing/PointSetComponent.h"

#include "Dataflow/DataflowRenderingViewMode.h"

#include "DataflowRendering/DataflowRenderableComponents.h"
#include "DataflowRendering/DataflowRenderableType.h"
#include "DataflowRendering/DataflowRenderableTypeInstance.h"
#include "DataflowRendering/DataflowRenderableTypeRegistry.h"
#include "DataflowRendering/DataflowRenderableTypeUtils.h"

#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionAlgo.h"
#include "GeometryCollection/ManagedArrayCollection.h"

#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"

#include "UObject/ObjectPtr.h"

namespace UE::Dataflow::Private
{
	////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	class FGeometryCollectionProximityRenderableType : public IRenderableType
	{
		UE_DATAFLOW_IRENDERABLE_PRIMARY_TYPE(FManagedArrayCollection, Collection);
		UE_DATAFLOW_IRENDERABLE_RENDER_GROUP(Proximity);
		UE_DATAFLOW_IRENDERABLE_VIEW_MODE(FDataflowConstruction3DViewMode);

	public:
		FGeometryCollectionProximityRenderableType()
		{
			constexpr bool bDepthTested = false;
			Material = (bDepthTested) 
				? LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolsetExp/Materials/PointSetComponentMaterial"))
				: LoadObject<UMaterial>(nullptr, TEXT("/MeshModelingToolsetExp/Materials/PointSetOverlaidComponentMaterial"));
		}

	private:
		virtual bool CanRender(const FRenderableTypeInstance& Instance) const
		{
			const FManagedArrayCollection& Collection = GetCollection(Instance);
			return Collection.HasAttribute(ProximityAttributeName, FGeometryCollection::GeometryGroup)
				&& Collection.HasAttribute(BoundingBoxAttributeName, FGeometryCollection::GeometryGroup);
		}

		virtual void GetPrimitiveComponents(const FRenderableTypeInstance& Instance, FRenderableComponents& OutComponents) const override
		{
			const FManagedArrayCollection& Collection = GetCollection(Instance);

			if (Collection.HasAttribute(ProximityAttributeName, FGeometryCollection::GeometryGroup) &&
				Collection.HasAttribute(BoundingBoxAttributeName, FGeometryCollection::GeometryGroup))
			{
				static const FName PointComponentName = TEXT("Connectivity_Points");
				UPointSetComponent* PointComponent = OutComponents.AddNewComponent<UPointSetComponent>(PointComponentName);
				static const FName LineComponentName = TEXT("Connectivity_Lines");
				ULineSetComponent* LineComponent = OutComponents.AddNewComponent<ULineSetComponent>(LineComponentName);

				if (PointComponent && LineComponent)
				{
					const TManagedArray<TSet<int32>>& Proximity = Collection.GetAttribute<TSet<int32>>(ProximityAttributeName, FGeometryCollection::GeometryGroup);
					const TManagedArray<FBox>& BoundingBox = Collection.GetAttribute<FBox>(BoundingBoxAttributeName, FGeometryCollection::GeometryGroup);
					const TManagedArray<FTransform3f>* Transform = Collection.FindAttributeTyped<FTransform3f>(FGeometryCollection::TransformAttribute, FGeometryCollection::TransformGroup);
					const TManagedArray<int32>* Parent = Collection.FindAttributeTyped<int32>(FGeometryCollection::ParentAttribute, FGeometryCollection::TransformGroup);
					const TManagedArray<int32>* GeometryToTransformIndex = Collection.FindAttributeTyped<int32>(FGeometryCollection::TransformIndexAttribute, FGeometryCollection::GeometryGroup);

					// compute transforms in collection space 
					TArray<FTransform> GlobalTransformArray;
					if (Transform && Parent)
					{
						GeometryCollectionAlgo::GlobalMatrices(*Transform, *Parent, GlobalTransformArray);
					}

					auto GetGeometryTransform =
						[&GeometryToTransformIndex, &GlobalTransformArray](int32 GeoIndex) -> FTransform
						{
							if (GeometryToTransformIndex && GeometryToTransformIndex->IsValidIndex(GeoIndex))
							{
								const int32 TransformIndex = (*GeometryToTransformIndex)[GeoIndex];
								if (GlobalTransformArray.IsValidIndex(TransformIndex))
								{
									return GlobalTransformArray[TransformIndex];
								}
							}
							return FTransform::Identity;
						};

					const int32 NumGeometry = Collection.NumElements(FGeometryCollection::GeometryGroup);
					TArray<FVector> Centers;
					for (int32 GeoIndex = 0; GeoIndex < NumGeometry; ++GeoIndex)
					{
						if (Proximity[GeoIndex].Num() > 0)
						{
							const FTransform GeometryTransform = GetGeometryTransform(GeoIndex);
							const FVector Center = GeometryTransform.TransformPosition(BoundingBox[GeoIndex].GetCenter());

							PointComponent->AddPoint(FRenderablePoint(Center, FColor::Blue, 15.0f));

							for (const int32 OtherGeoIndex : Proximity[GeoIndex])
							{
								if (OtherGeoIndex > GeoIndex)
								{
									const FTransform OtherGeometryTransform = GetGeometryTransform(OtherGeoIndex);
									const FVector OtherCenter = OtherGeometryTransform.TransformPosition(BoundingBox[OtherGeoIndex].GetCenter());

									LineComponent->AddLine(FRenderableLine(Center, OtherCenter, FColor::Yellow, 2.0f));
								}
							}
						}
					}

					PointComponent->SetPointMaterial(Material);
					LineComponent->SetLineMaterial(Material);
				}
			}
		}

		static inline const FName ProximityAttributeName = TEXT("Proximity");
		static inline const FName BoundingBoxAttributeName = TEXT("BoundingBox");

		UMaterialInterface* Material = nullptr;
	};


	///////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void RegisterGeometryCollectionProximityRenderableTypes()
	{
		UE_DATAFLOW_REGISTER_RENDERABLE_TYPE(FGeometryCollectionProximityRenderableType);
	}
}