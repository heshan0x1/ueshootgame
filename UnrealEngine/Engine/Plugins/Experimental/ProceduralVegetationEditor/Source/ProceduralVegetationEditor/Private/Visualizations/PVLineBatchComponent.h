// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "SceneView.h"
#include "Components/PrimitiveComponent.h"
#include "PVLineBatchComponent.generated.h"

struct FManagedArrayCollection;
class UPVLineBatchComponent;

class FPVLineSceneProxy final : public FPrimitiveSceneProxy
{
public:
	FPVLineSceneProxy(const UPVLineBatchComponent* InComponent);

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override;
	virtual void GetDynamicMeshElements(
		const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily,
		uint32 VisibilityMap,
		FMeshElementCollector& Collector
	) const override;

	virtual SIZE_T GetTypeHash() const override;
	virtual uint32 GetMemoryFootprint(void) const override;

	virtual bool CanBeOccluded() const override;

private:
	const TObjectPtr<const UPVLineBatchComponent> LineComponent;
};

UENUM()
enum class EPointDrawSettings : uint8
{
	None,
	Start,
	End,
	Both
};

USTRUCT()
struct FPVLineInfo
{
	GENERATED_BODY()

	FVector StartPos;
	FVector EndPos;
	FLinearColor Color;
	ESceneDepthPriorityGroup DepthPriorityGroup = SDPG_World;
	EPointDrawSettings PointDrawSettings = EPointDrawSettings::None;
};

UCLASS(Blueprintable, BlueprintType, meta=(BlueprintSpawnableComponent), MinimalAPI)
class UPVLineBatchComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UPVLineBatchComponent();

	void InitBounds();
	void AddLine(const FVector& InStartPos, const FVector& InEndPos, const FLinearColor& InColor = FLinearColor::White,
				const ESceneDepthPriorityGroup InDepthPriorityGroup = ESceneDepthPriorityGroup::SDPG_World,
				const EPointDrawSettings InPointDrawSettings = EPointDrawSettings::None);

	void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) const;

	void Flush();
	
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

protected:
	FBox BBox;
	TArray<FPVLineInfo> Lines;

private:
	const float PointSize = 7.5f;
};
