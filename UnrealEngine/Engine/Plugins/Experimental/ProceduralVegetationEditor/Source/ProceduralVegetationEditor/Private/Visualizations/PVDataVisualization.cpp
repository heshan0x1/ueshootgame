// Copyright Epic Games, Inc. All Rights Reserved.

#include "PVDataVisualization.h"

#include "AdvancedPreviewScene.h"
#include "EditorViewportClient.h"
#include "GeometryCollectionToDynamicMesh.h"
#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGDataVisualization.h"
#include "PCGEditorSettings.h"
#include "PlanarCut.h"
#include "PVBoneComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/TextRenderComponent.h"
#include "DataTypes/PVData.h"
#include "DebugVisualization/PVDebugVisualizer.h"
#include "Facades/PVFoliageFacade.h"
#include "GeometryCollection/GeometryCollectionObject.h"
#include "Helpers/PVUtilities.h"
#include "Nodes/PVBaseSettings.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Visualizations/PVSkeletonVisualizerComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/Material.h"
#include "ProceduralVegetation/Private/Implementations/PVFoliage.h"

#define LOCTEXT_NAMESPACE "PCGManagedArrayCollectionDataVisualization"

class UPVBoneComponent;

DEFINE_LOG_CATEGORY(LogProceduralVegetationDataVisualization);

FPVDataVisualization::FPVDataVisualization()
{
	RegisterVisualizations();
}

void FPVDataVisualization::RegisterVisualizations()
{
	RenderMap.Add(EPVRenderType::PointData, FPVRenderCallback::CreateRaw(this, &FPVDataVisualization::SkeletonRenderer));
	RenderMap.Add(EPVRenderType::Mesh, FPVRenderCallback::CreateRaw(this, &FPVDataVisualization::MeshRenderer));
	RenderMap.Add(EPVRenderType::Foliage, FPVRenderCallback::CreateRaw(this, &FPVDataVisualization::FoliageRenderer));
	RenderMap.Add(EPVRenderType::Bones, FPVRenderCallback::CreateRaw(this, &FPVDataVisualization::BonesRenderer));
	RenderMap.Add(EPVRenderType::FoliageGrid, FPVRenderCallback::CreateRaw(this, &FPVDataVisualization::FoliageGridRenderer));
}

void FPVDataVisualization::ExecuteDebugDisplay(FPCGContext* Context, const UPCGSettingsInterface* SettingsInterface, const UPCGData* Data,
                                                         AActor* TargetActor) const
{
	return;
}

FPCGSetupSceneFunc FPVDataVisualization::GetViewportSetupFunc(const UPCGSettingsInterface* SettingsInterface, const UPCGData* Data) const
{
	return [this, WeakData=TWeakObjectPtr<const UPVData>(Cast<UPVData>(Data)), WeakSettings = TWeakObjectPtr<const UPVBaseSettings>(Cast<UPVBaseSettings>(SettingsInterface))](
		FPCGSceneSetupParams& InOutParams)
	{
		check(InOutParams.Scene);

		if (!WeakData.IsValid())
		{
			UE_LOG(LogProceduralVegetationDataVisualization, Error, TEXT("Failed to setup data viewport, the data was lost or invalid."));
			return;
		}

		InOutParams.Scene->SetFloorVisibility(true);
		InOutParams.Scene->SetFloorOffset(1.0f);

		// Reapply view mode settings as for some reason SetFloorVisibility changes EngineFlags
		InOutParams.EditorViewportClient->SetViewMode(InOutParams.EditorViewportClient->GetViewMode());
			
		InOutParams.EditorViewportClient->SetRealtime(true);
		if (!InOutParams.Scene->IsGridEnabled())
		{
			InOutParams.Scene->HandleToggleGrid();
		}
		if (InOutParams.Scene->IsEnvironmentEnabled())
		{
			InOutParams.Scene->HandleToggleEnvironment();
		}
		if (!InOutParams.Scene->IsPostProcessingEnabled())
		{
			InOutParams.Scene->HandleTogglePostProcessing();
		}

		const FManagedArrayCollection& Collection = WeakData->GetCollection();

		FBoxSphereBounds Bounds(ForceInit);

		for (const EPVRenderType& RenderType : WeakSettings->GetCurrentRenderTypes())
		{
			if (RenderMap.Contains(RenderType))
			{
				InOutParams.Scene->GetLineBatcher()->Flush();
				RenderMap[RenderType].Execute(Collection, InOutParams, Bounds);
			}
		}

		if (PV::Utilities::DebugModeEnabled())
		{
			if (WeakSettings.IsValid() && WeakSettings->bDebug)
			{
				FPVDebugVisualizer::Draw(WeakData.Get(), InOutParams);
			}
		}

		InOutParams.FocusBounds = FBoxSphereBounds(Bounds.Origin, Bounds.BoxExtent, Bounds.SphereRadius);
	};
}

void FPVDataVisualization::MeshRenderer(const FManagedArrayCollection& InCollection, FPCGSceneSetupParams& InOutParams,
                                                  FBoxSphereBounds& OutBounds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPVDataVisualization::MeshRenderer);

	TArray<UMaterialInterface*> Materials;
	
	if (InCollection.HasAttribute("MaterialPath", FGeometryCollection::MaterialGroup))
	{
		const TManagedArray<FString>& MaterialPaths = InCollection.GetAttribute<FString>("MaterialPath", FGeometryCollection::MaterialGroup);
		for (const FString& MaterialPath : MaterialPaths.GetConstArray())
		{
			Materials.Add(LoadObject<UMaterialInterface>(nullptr, MaterialPath));
		}
	}

	UE::Geometry::FGeometryCollectionToDynamicMeshes Converter;
	Converter.Init(InCollection, {});
	for (const auto& MeshInfo : Converter.Meshes)
	{
		FDynamicMesh3* DynamicMesh = MeshInfo.Mesh.Get();
		if (DynamicMesh->VertexCount())
		{
			TObjectPtr<UDynamicMeshComponent> DynamicMeshComponent = NewObject<UDynamicMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
			DynamicMeshComponent->SetMesh(MoveTemp(*DynamicMesh));
			DynamicMeshComponent->SetTangentsType(EDynamicMeshComponentTangentsMode::AutoCalculated);
			DynamicMeshComponent->ConfigureMaterialSet(Materials);
			DynamicMeshComponent->UpdateBounds();

			InOutParams.ManagedResources.Add(DynamicMeshComponent);
			InOutParams.Scene->AddComponent(DynamicMeshComponent, FTransform::Identity);

			OutBounds = OutBounds + DynamicMeshComponent->CalcLocalBounds();
		}
	}
}

void FPVDataVisualization::SkeletonRenderer(const FManagedArrayCollection& InCollection, FPCGSceneSetupParams& InOutParams,
                                                      FBoxSphereBounds& OutBounds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPVDataVisualization::SkeletonRenderer);

	const TObjectPtr<UPVSkeletonVisualizerComponent> VisualizerComponent = NewObject<UPVSkeletonVisualizerComponent>(
		GetTransientPackage(), NAME_None, RF_Transient);
	VisualizerComponent->SetCollection(&InCollection);
	VisualizerComponent->UpdateBounds();

	InOutParams.ManagedResources.Add(VisualizerComponent);
	InOutParams.Scene->AddComponent(VisualizerComponent, FTransform::Identity);

	OutBounds = OutBounds + VisualizerComponent->CalcLocalBounds();
}

void FPVDataVisualization::FoliageRenderer(const FManagedArrayCollection& InCollection, FPCGSceneSetupParams& InOutParams,
                                                     FBoxSphereBounds& OutBounds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPVDataVisualization::FoliageRenderer);

	PV::Facades::FFoliageFacade FoliageFacade(InCollection);

	auto OnComponentCreated = [&InOutParams](UMeshComponent* InInstancedComponent)
	{
		InOutParams.ManagedResources.Add(InInstancedComponent);
		InInstancedComponent->RegisterComponentWithWorld(InOutParams.Scene->GetWorld());
		InOutParams.Scene->AddComponent(InInstancedComponent, FTransform::Identity);
	};

	TMap<FString, TObjectPtr<UMeshComponent>> OutInstancedComponentMap;

	PV::Utilities::AddFoliageInstances(FoliageFacade, nullptr,
	                                           PV::Utilities::FFoliageComponentCreatedCallback::CreateLambda(OnComponentCreated),
	                                           OutInstancedComponentMap);

	for (const auto& [FoliageName , Component] : OutInstancedComponentMap)
	{
		OutBounds = OutBounds + Component->CalcLocalBounds();
	}
}

void FPVDataVisualization::BonesRenderer(const FManagedArrayCollection& InCollection, FPCGSceneSetupParams& InOutParams,
	FBoxSphereBounds& OutBounds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPVDataVisualization::BonesRenderer);

	const TObjectPtr<UPVBoneComponent> BoneComponent = NewObject<UPVBoneComponent>(
		GetTransientPackage(), NAME_None, RF_Transient);
	BoneComponent->SetCollection(&InCollection);
	BoneComponent->UpdateBounds();

	InOutParams.ManagedResources.Add(BoneComponent);
	InOutParams.Scene->AddComponent(BoneComponent, FTransform::Identity);

	FString InTextToDraw = FString::Format(TEXT("{0} Bones"),{BoneComponent->GetBoneCount()});
	const FVector3f& Pos = FVector3f::ForwardVector * 100;

	DrawDebugString(BoneComponent->GetWorld(), FVector::Zero(), InTextToDraw);
	
	TObjectPtr<UTextRenderComponent> Component = NewObject<UTextRenderComponent>(GetTransientPackage(), NAME_None, RF_Transient);
	Component->SetText(FText::FromString(InTextToDraw));
	Component->SetTextRenderColor(FColor::Blue);
	Component->SetWorldSize(40);
	Component->SetGenerateOverlapEvents(false);
						
	InOutParams.ManagedResources.Add(Component);
	FTransform TextTransform = FTransform(FVector(Pos.X, Pos.Y, Pos.Z));
	FRotator TextRotator = FRotator(0,90,0);
	TextTransform.SetRotation(TextRotator.Quaternion());
	InOutParams.Scene->AddComponent(Component, TextTransform);

	OutBounds = OutBounds + BoneComponent->CalcLocalBounds();
}

TPair<int32, int32> GetMeshTrisAndVerts(const UObject* FoliageObject)
{
	int32 NumTriangles = 0;
	int32 NumVertices = 0;
	if (const UStaticMesh* StaticFoliageMesh = Cast<UStaticMesh>(FoliageObject))
	{
		NumTriangles = StaticFoliageMesh->GetNumTriangles(0);
		NumVertices = StaticFoliageMesh->GetNumVertices(0);
	}
	else if (const USkeletalMesh* SkeletalFoliageMesh = Cast<USkeletalMesh>(FoliageObject))
	{
		FSkeletalMeshRenderData* SkelMeshRenderData = SkeletalFoliageMesh->GetResourceForRendering();
		if (SkelMeshRenderData && SkelMeshRenderData->LODRenderData.Num() > 0)
		{
			NumTriangles = SkelMeshRenderData->LODRenderData[0].GetTotalFaces();
			NumVertices = SkelMeshRenderData->LODRenderData[0].GetNumVertices();
		}
	}
	return {NumTriangles, NumVertices};
}

void FPVDataVisualization::FoliageGridRenderer(const FManagedArrayCollection& InCollection, FPCGSceneSetupParams& InOutParams,
	FBoxSphereBounds& OutBounds)
{
	const PV::Facades::FFoliageFacade FoliageFacade(InCollection);

	const TArray<FPVFoliageInfo> FoliageInfos = FoliageFacade.GetFoliageInfos();

	FBox MaxFoliageBounds(ForceInit);
	int ValidFoliageEntries = 0;

	static const auto GetMeshBounds = [](auto* Mesh)-> FBox
		{
			const FBox BoundingBox = Mesh
				? Mesh->GetBounds().GetBox()
				: FBox(ForceInit);
			return BoundingBox;
		};

	const auto AddComponentToScene = [&](USceneComponent* ComponentRef, const FTransform& Transform, const bool UpdateBounds = true)
		{
			InOutParams.ManagedResources.Add(ComponentRef);
			InOutParams.Scene->AddComponent(ComponentRef, Transform);

			if (UpdateBounds)
			{
				ComponentRef->UpdateBounds();
				OutBounds = OutBounds + ComponentRef->CalcBounds(Transform);
			}
		};

	for (const FPVFoliageInfo& Info : FoliageInfos)
	{
		FString FoliageName = Info.Mesh.ToString();
		
		if (UObject* FoliageMesh = LoadObject<UObject>(nullptr, *FoliageName))
		{
			FBox Bounds = GetMeshBounds(Cast<UStaticMesh>(FoliageMesh));
			if (!Bounds.IsValid)
			{
				Bounds = GetMeshBounds(Cast<USkeletalMesh>(FoliageMesh));
			}
			MaxFoliageBounds = Bounds.IsValid && Bounds.GetVolume() > MaxFoliageBounds.GetVolume() ? Bounds : MaxFoliageBounds;
			ValidFoliageEntries++;
		}
	}
	
	UStaticMesh* TextBackgroundMesh = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane"));
	UMaterial* BackgroundMat = LoadObject<UMaterial>(nullptr, TEXT("/ProceduralVegetationEditor/Materials/TextBackgroundMat.TextBackgroundMat"));
	UMaterial* TextMat = LoadObject<UMaterial>(nullptr, TEXT("/ProceduralVegetationEditor/Materials/TextMaterial.TextMaterial"));
	
	const float FoliageHorizontalPadding = MaxFoliageBounds.GetSize().GetMax() + 100.0f;
	const float FoliageVerticalPadding = MaxFoliageBounds.GetSize().GetMax() + 100.0f;

	FVector FoliagePos = FVector(FoliageHorizontalPadding * FoliageInfos.Num() / -2.0f, 0.0f, 0.0f);

	for (const FPVFoliageInfo& Info : FoliageInfos)
	{
		const FString& FoliagePath = Info.Mesh.ToString();
		
		UObject* FoliageObject = LoadObject<UObject>(nullptr, FoliagePath);
		if (UStaticMesh* StaticFoliageMesh = Cast<UStaticMesh>(FoliageObject))
		{
			UStaticMeshComponent* MeshComponent = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
			MeshComponent->SetStaticMesh(StaticFoliageMesh);
			AddComponentToScene(MeshComponent, FTransform(FoliagePos));
		}
		else if (USkeletalMesh* SkeletalFoliageMesh = Cast<USkeletalMesh>(FoliageObject))
		{
			USkeletalMeshComponent* MeshComponent = NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
			MeshComponent->SetSkeletalMesh(SkeletalFoliageMesh);
			AddComponentToScene(MeshComponent, FTransform(FoliagePos));
		}

		if (FoliageObject)
		{
			const auto [Tris, Verts] = GetMeshTrisAndVerts(FoliageObject);
			const FString InTextToDraw = FString::Printf(TEXT("Name: %s\nTris: %d\nVerts: %d"), *FoliageObject->GetName(), Tris, Verts);
			
			TObjectPtr<UTextRenderComponent> TextRenderComponent = NewObject<UTextRenderComponent>(GetTransientPackage(), NAME_None, RF_Transient);
			TextRenderComponent->SetMaterial(0, TextMat);
			TextRenderComponent->SetText(FText::FromString(InTextToDraw));
			TextRenderComponent->SetTextRenderColor(FColor::Black);
			TextRenderComponent->SetWorldSize(10);
			TextRenderComponent->SetVerticalAlignment(EVRTA_TextTop);
			TextRenderComponent->SetGenerateOverlapEvents(false);
			
			FBoxSphereBounds TextBounds = TextRenderComponent->CalcLocalBounds();
			const FVector TextPos = FoliagePos + TextBounds.BoxExtent.Y * FVector::BackwardVector + FoliageVerticalPadding * FVector::UpVector;
			AddComponentToScene(
				TextRenderComponent,
				FTransform(FRotator(0, 90, 0), TextPos),
				false
			);

			TextBounds = TextBounds.TransformBy(TextRenderComponent->GetComponentTransform());

			if (TextBackgroundMesh && BackgroundMat)
			{
				UStaticMeshComponent* TextBackgroundMeshComponent = NewObject<UStaticMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient);
				TextBackgroundMeshComponent->SetStaticMesh(TextBackgroundMesh);
				TextBackgroundMeshComponent->SetMaterial(0, BackgroundMat);


				AddComponentToScene(
					TextBackgroundMeshComponent,
					FTransform(
						FRotator(0, 0, 90),
						TextBounds.Origin + FVector::LeftVector * 0.01f,
						FVector(TextBounds.BoxExtent.X / 40.0, TextBounds.BoxExtent.Z / 40.0, 1)
					),
					false
				);
			}
			
			FoliagePos.X += FoliageHorizontalPadding;
		}
	}
}

#undef LOCTEXT_NAMESPACE
