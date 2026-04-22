// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "Dataflow/DataflowEngine.h"
#include "Dataflow/DataflowSelection.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMeshProcessor.h"

#include "MeshBooleanNodes.generated.h"

class UDynamicMesh;
class UMaterialInterface;


UENUM(BlueprintType)
enum class EMeshBooleanOperationEnum : uint8
{
	// A union of A + B includes everything inside either A or B
	Dataflow_MeshBoolean_Union UMETA(DisplayName = "Union"),
	// An intersection of A & B includes only the points inside both A and B, i.e. trimming A by B (and vice versa)
	Dataflow_MeshBoolean_Intersect UMETA(DisplayName = "Intersect"),
	// A difference of A - B includes only the points inside A that are outside of B, i.e. subtracting B from A
	Dataflow_MeshBoolean_Difference UMETA(DisplayName = "Difference"),
	//~~~
	//256th entry
	Dataflow_Max                UMETA(Hidden)
};

/**
 *
 * Compute a Mesh Boolean between Mesh1 and Mesh2
 * 
 * Supported Boolean Operations:
 *  Union (Mesh1 + Mesh2)
 *  Difference (Mesh1 - Mesh2; removing what's inside of Mesh2 from Mesh1)
 *  Intersection (Mesh1 & Mesh2; removing what's outside of Mesh2 from Mesh1)
 *
 */
USTRUCT()
struct FMeshBooleanDataflowNode : public FDataflowNode
{
	GENERATED_USTRUCT_BODY()
	DATAFLOW_NODE_DEFINE_INTERNAL(FMeshBooleanDataflowNode, "MeshBoolean", "Mesh|Utilities", "")
	DATAFLOW_NODE_RENDER_TYPE("SurfaceRender",FName("FDynamicMesh3"), "Mesh")

public:

	FMeshBooleanDataflowNode(const UE::Dataflow::FNodeParameters& InParam, FGuid InGuid = FGuid::NewGuid());

private:
	/** Boolean operation */
	UPROPERTY(EditAnywhere, Category = "Boolean");
	EMeshBooleanOperationEnum Operation = EMeshBooleanOperationEnum::Dataflow_MeshBoolean_Intersect;

	/** Mesh input */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TObjectPtr<UDynamicMesh> Mesh1;

	/** Mesh input */
	UPROPERTY(meta = (DataflowInput, DataflowIntrinsic))
	TObjectPtr<UDynamicMesh> Mesh2;

	/** Output mesh */
	UPROPERTY(meta = (DataflowOutput))
	TObjectPtr<UDynamicMesh> Mesh;

	virtual void Evaluate(UE::Dataflow::FContext& Context, const FDataflowOutput* Out) const override;

};



namespace UE::Dataflow
{
	void MeshBooleanNodes();
}

