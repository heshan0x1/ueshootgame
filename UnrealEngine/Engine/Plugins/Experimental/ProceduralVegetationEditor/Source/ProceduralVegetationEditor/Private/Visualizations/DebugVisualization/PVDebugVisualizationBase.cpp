// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVDebugVisualizationBase.h"

#include "AdvancedPreviewScene.h"
#include "PVDataAttributeValueInterface.h"
#include "PVDebugVisualizer.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"

#include "DataTypes/PVData.h"

#include "Engine/StaticMesh.h"

#include "UObject/Package.h"

#include "Visualizations/PVLineBatchComponent.h"

#define LOCTEXT_NAMESPACE "PVDebugVisualization"

DEFINE_LOG_CATEGORY(LogPVDebugVisualization);

const float FPVDebugVisualizationBase::DefaultDirectionStrength = 20;
const float FPVDebugVisualizationBase::DefaultTextScale = 2.5;
const float FPVDebugVisualizationBase::DefaultPointScale = 0.005f;

void FPVDebugVisualizationBase::Draw(FVisualizerDrawContext& InContext)
{
	DrawPivotPoints(InContext);
	DrawAttributes(InContext);
}

void FPVDebugVisualizationBase::DrawPivotPoints(const FVisualizerDrawContext& InContext)
{
	auto Collection = InContext.Collection;
	auto Settings = InContext.VisualizationSettings;

	if (Settings.bShowAnchorPoints)
	{
		const TArray<FVector3f>& Positions = GetPivotPositions(Collection);
	
		for (int32 i = 0; i < Positions.Num(); i++)
		{
			const FVector Pos(Positions[i]);
		
			AddDrawAsPoint(Pos);
		}	
	}
}

void FPVDebugVisualizationBase::DrawAttributes(FVisualizerDrawContext& InContext)
{
	auto Collection = InContext.Collection;
	
	auto VisualizationSettings = InContext.VisualizationSettings;
	const FName AttributeName = VisualizationSettings.AttributeToFilter;
	const FName GroupName = VisualizationSettings.GetDebugTypeString();

	if (!GroupName.IsNone() && !AttributeName.IsNone() && Collection.HasGroup(GroupName))
	{
		if (Collection.HasAttribute(AttributeName, GroupName))
		{
			int NumElements = Collection.NumElements(GroupName);
			const FManagedArrayCollection::EArrayType AttributeType = Collection.GetAttributeType(AttributeName, GroupName);

			FPVAttributeValuePtr AttributeValue = FPVAttributeValueInterface::Create(GroupName, AttributeName, AttributeType, Collection);

			if (AttributeValue)
			{
				if (AttributeValue->IsScalar() || VisualizationSettings.VisualizationMode == EPVDebugValueVisualizationMode::Text)
				{
					auto TextValueArray = AttributeValue->ToText();
		
					for (int32 i = 0; i < NumElements; i++)
					{
						FVector3f OutPos;
						float OutScale;
						GetPivot(Collection, i, OutPos, OutScale);
						
						check(TextValueArray.IsValidIndex(i));

						DrawAsText(InContext.SceneSetupParams, TextValueArray[i], OutPos, OutScale, VisualizationSettings.Color);
					}
				}
				else if (VisualizationSettings.VisualizationMode == EPVDebugValueVisualizationMode::Direction)
				{
					auto VectorsAttribute = AttributeValue->ToVectors();

					for (int32 i = 0; i < NumElements; i++)
					{
						FVector3f OutPos;
						float OutScale;
						GetPivot(Collection, i, OutPos, OutScale);

						FVector StartPos(OutPos);
						FVector EndPos = StartPos + VectorsAttribute[i].GetSafeNormal() * DefaultDirectionStrength;
						check(VectorsAttribute.IsValidIndex(i));

						DrawAsDirection(InContext.SceneSetupParams, StartPos, EndPos, VisualizationSettings.Color);
					}
				}
				else if (VisualizationSettings.VisualizationMode == EPVDebugValueVisualizationMode::Point)
				{
					auto Vectors = AttributeValue->ToVectors();

					for (int32 i = 0; i < NumElements; i++)
					{
						check(Vectors.IsValidIndex(i));

						AddDrawAsPoint(Vectors[i]);
					}
				}	
			}
		}
	}

	DrawAsPoint(InContext.SceneSetupParams, PointsToDraw);
}

void FPVDebugVisualizationBase::AddDrawAsPoint(const FVector& InPos)
{
	auto Transform = FTransform(InPos);
	Transform.SetScale3D(FVector(DefaultPointScale, DefaultPointScale, DefaultPointScale));

	PointsToDraw.Add(Transform);
}

void FPVDebugVisualizationBase::DrawAsText(FPCGSceneSetupParams& InOutParams, const FText& InTextToDraw, const FVector3f& InPos, const float& InScale, const FLinearColor& InColor)
{
	const FVector3f& Pos = InPos + (FVector3f::RightVector * -1 * InScale);

	TObjectPtr<UTextRenderComponent> Component = NewObject<UTextRenderComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	Component->SetText(InTextToDraw);
	Component->SetTextRenderColor(InColor.ToFColor(false));
	Component->SetWorldSize(DefaultTextScale);
	Component->SetGenerateOverlapEvents(false);
						
	InOutParams.ManagedResources.Add(Component);
	InOutParams.Scene->AddComponent(Component, FTransform(FVector(Pos.X, Pos.Y, Pos.Z)));
}

void FPVDebugVisualizationBase::DrawAsDirection(FPCGSceneSetupParams& InOutParams, const FVector& InStartPos, const FVector& InEndPos,
	const FLinearColor& InColor)
{
	UPVLineBatchComponent* LineComponent = GetOrCreateLineComponent(InOutParams);

	check(LineComponent)
	
	if (LineComponent)
	{
		LineComponent->AddLine(InStartPos, InEndPos, InColor);
	}
}

void FPVDebugVisualizationBase::DrawAsPoint(FPCGSceneSetupParams& InOutParams, const TArray<FTransform>& InTransforms, const FLinearColor& InColor)
{
	if (InTransforms.Num() == 0)
		return;
	
	UInstancedStaticMeshComponent* Component = GetOrCreatePointComponent(InOutParams);

	check(Component)
	
	if (Component)
	{
		Component->AddInstances(InTransforms, false);
	}	
}

UPVLineBatchComponent* FPVDebugVisualizationBase::GetOrCreateLineComponent(FPCGSceneSetupParams& InOutParams)
{
	UPVLineBatchComponent* LineComponent = nullptr;
	InOutParams.ManagedResources.FindItemByClass<UPVLineBatchComponent>(&LineComponent);

	if (!LineComponent)
	{
		LineComponent = NewObject<UPVLineBatchComponent>(GetTransientPackage(), NAME_None, RF_Transient);
		LineComponent->InitBounds();

		AddToScene(InOutParams, LineComponent);
	}

	return LineComponent;
}

UInstancedStaticMeshComponent* FPVDebugVisualizationBase::GetOrCreatePointComponent(FPCGSceneSetupParams& InOutParams)
{
	UInstancedStaticMeshComponent* InstancedStaticMeshComponent = nullptr;
	InOutParams.ManagedResources.FindItemByClass<UInstancedStaticMeshComponent>(&InstancedStaticMeshComponent);

	if (!InstancedStaticMeshComponent)
	{
		UStaticMesh* PointSphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/EditorMeshes/EditorSphere"));
		
		InstancedStaticMeshComponent = NewObject<UInstancedStaticMeshComponent>(GetTransientPackage(), NAME_None);
		InstancedStaticMeshComponent->SetStaticMesh(PointSphere);

		AddToScene(InOutParams, InstancedStaticMeshComponent);
	}

	return InstancedStaticMeshComponent;
}

void FPVDebugVisualizationBase::AddToScene(FPCGSceneSetupParams& InOutParams, UPrimitiveComponent* InComponent)
{
	InOutParams.ManagedResources.Add(InComponent);
	InOutParams.Scene->AddComponent(InComponent, FTransform::Identity);
}

#undef LOCTEXT_NAMESPACE