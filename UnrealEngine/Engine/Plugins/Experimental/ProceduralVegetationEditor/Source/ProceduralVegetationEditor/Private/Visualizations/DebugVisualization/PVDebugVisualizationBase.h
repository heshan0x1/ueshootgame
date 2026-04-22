// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Helpers/PVUtilities.h"

#include "GeometryCollection/ManagedArray.h"
#include "Templates/SharedPointer.h"

class UPVLineBatchComponent;
struct FVisualizerDrawContext;
class UPVSkeletonVisualizerComponent;
enum class EManagedArrayType : uint8;
class FPVAttributeValueInterface;
struct FPCGSceneSetupParams;
class UPVData;

class FPVDebugVisualizationBase
{
public:
	virtual ~FPVDebugVisualizationBase() = default;
	FPVDebugVisualizationBase(){};
	virtual void Draw(FVisualizerDrawContext& InContext);
protected:
	void DrawPivotPoints(const FVisualizerDrawContext& InContext);
	void DrawAttributes(FVisualizerDrawContext& InContext);
	void AddDrawAsPoint(const FVector& InPos);
	virtual TArray<FVector3f> GetPivotPositions(const FManagedArrayCollection& InCollection) = 0;
	virtual void GetPivot(const FManagedArrayCollection& InCollection, const int InIndex, FVector3f& OutPos, float& OutScale) = 0;
	
	static void DrawAsText(FPCGSceneSetupParams& InOutParams, const FText& InTextToDraw, const FVector3f& InPos, const float& InScale, const FLinearColor& InColor = FLinearColor::White);
	static void DrawAsDirection( FPCGSceneSetupParams& InOutParams, const FVector& InStartPos, const FVector& InEndPos, const FLinearColor& InColor = FLinearColor::White);
	static void DrawAsPoint(FPCGSceneSetupParams& InOutParams,const TArray<FTransform>& InTransforms, const FLinearColor& InColor = FLinearColor::White);
	
	static UPVLineBatchComponent* GetOrCreateLineComponent(FPCGSceneSetupParams& InOutParams);
	static UInstancedStaticMeshComponent* GetOrCreatePointComponent(FPCGSceneSetupParams& InOutParams);

	static void AddToScene(FPCGSceneSetupParams& InOutParams, UPrimitiveComponent* InComponent);
	
	TArray<FTransform> PointsToDraw;

	static const float DefaultDirectionStrength;
	static const float DefaultTextScale;
	static const float DefaultPointScale;
};
typedef TSharedPtr<FPVDebugVisualizationBase> FPVDebugVisualizationPtr;

DECLARE_LOG_CATEGORY_EXTERN(LogPVDebugVisualization, Log, All);