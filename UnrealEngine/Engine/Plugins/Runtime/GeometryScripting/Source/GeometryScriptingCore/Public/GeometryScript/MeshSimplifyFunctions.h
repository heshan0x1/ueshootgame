// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "MeshSimplifyFunctions.generated.h"

#define UE_API GEOMETRYSCRIPTINGCORE_API

class UDynamicMesh;

USTRUCT(BlueprintType)
struct FGeometryScriptPlanarSimplifyOptions
{
	GENERATED_BODY()
public:
	/** */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	float AngleThreshold = 0.001;

	/** If enabled, the simplified mesh is automatically compacted to remove gaps in the index space. This is expensive and can be disabled by advanced users. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bAutoCompact = true;
};

USTRUCT(BlueprintType)
struct FGeometryScriptPolygroupSimplifyOptions
{
	GENERATED_BODY()
public:
	/** */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	float AngleThreshold = 0.001;

	/** If enabled, the simplified mesh is automatically compacted to remove gaps in the index space. This is expensive and can be disabled by advanced users. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bAutoCompact = true;
};


UENUM(BlueprintType)
enum class EGeometryScriptRemoveMeshSimplificationType : uint8
{
	StandardQEM = 0,
	VolumePreserving = 1,
	AttributeAware = 2
};


USTRUCT(Blueprintable)
struct FGeometryScriptSimplifyMeshOptions
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	EGeometryScriptRemoveMeshSimplificationType Method = EGeometryScriptRemoveMeshSimplificationType::AttributeAware;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bAllowSeamCollapse = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bAllowSeamSmoothing = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bAllowSeamSplits = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bPreserveVertexPositions = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bRetainQuadricMemory = false;

	/** If enabled, the simplified mesh is automatically compacted to remove gaps in the index space. This is expensive and can be disabled by advanced users. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bAutoCompact = true;
};

UENUM(BlueprintType)
enum class EGeometryScriptClusterSimplifyConstraintLevel : uint8
{
	// Edge will be kept as-is in final output
	Fixed,
	// Attempt to preserve 'paths' of constrained edges, but may simplify along the path
	// Vertices where paths converge will be kept in final output
	Constrained,
	// Edge can be simplified without constraint
	Free
};

USTRUCT(BlueprintType)
struct FGeometryScriptClusterSimplifyEdgeConstraintOptions
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ConstraintOptions)
	EGeometryScriptClusterSimplifyConstraintLevel MeshBoundary = EGeometryScriptClusterSimplifyConstraintLevel::Constrained;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ConstraintOptions)
	EGeometryScriptClusterSimplifyConstraintLevel GroupBoundary = EGeometryScriptClusterSimplifyConstraintLevel::Constrained;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ConstraintOptions)
	EGeometryScriptClusterSimplifyConstraintLevel MaterialBoundary = EGeometryScriptClusterSimplifyConstraintLevel::Constrained;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ConstraintOptions)
	EGeometryScriptClusterSimplifyConstraintLevel UVSeam = EGeometryScriptClusterSimplifyConstraintLevel::Constrained;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ConstraintOptions)
	EGeometryScriptClusterSimplifyConstraintLevel ColorSeam = EGeometryScriptClusterSimplifyConstraintLevel::Constrained;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = ConstraintOptions)
	EGeometryScriptClusterSimplifyConstraintLevel NormalSeam = EGeometryScriptClusterSimplifyConstraintLevel::Constrained;
};

USTRUCT(BlueprintType)
struct FGeometryScriptClusterSimplifyMeshOptions
{
	GENERATED_BODY()
public:

	// Whether to discard attributes (materials, UV/normal/tangent/color attributes)
	// Note that attribute seam edges will not be constrained if attributes are discarded
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	bool bDiscardAttributes = false;

	// If > 0, boundary vertices w/ incident boundary edge angle greater than this (in degrees) will be kept in the output
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	double FixBoundaryAngleTolerance = 45;

	// Options to constrain simplification of different mesh edge types, to optionally help preserve mesh boundaries and seams
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Options)
	FGeometryScriptClusterSimplifyEdgeConstraintOptions EdgeConstraints;
};



UCLASS(MinimalAPI, meta = (ScriptName = "GeometryScript_MeshSimplification"))
class UGeometryScriptLibrary_MeshSimplifyFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	/**
	* Simplifies planar areas of the mesh that have more triangles than necessary. Note that it does not change the 3D shape of the mesh.
	* Planar regions are identified by comparison of face normals using a Angle Threshold in the Options.
	*/
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Simplification", meta=(ScriptMethod, HidePin = "Debug"))
	static UE_API UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplySimplifyToPlanar(  
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPlanarSimplifyOptions Options,
		UGeometryScriptDebug* Debug = nullptr);
	
	/**
	* Simplifies the mesh down to the PolyGroup Topology. For example, the high-level faces of the mesh PolyGroups. 
	* Another example would be on a default Box-Sphere where simplifying to PolyGroup topology produces a box.
	*/
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Simplification", meta=(ScriptMethod, HidePin = "Debug", DisplayName = "Apply Simplify To PolyGroup Topology"))
	static UE_API UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplySimplifyToPolygroupTopology(  
		UDynamicMesh* TargetMesh, 
		FGeometryScriptPolygroupSimplifyOptions Options,
		FGeometryScriptGroupLayer GroupLayer,
		UGeometryScriptDebug* Debug = nullptr);

	/**
	* Simplifies the mesh until a target triangle count is reached. Behavior can be additionally controlled with the Options. 
	*/
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Simplification", meta=(ScriptMethod, HidePin = "Debug"))
	static UE_API UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplySimplifyToTriangleCount(  
		UDynamicMesh* TargetMesh, 
		int32 TriangleCount,
		FGeometryScriptSimplifyMeshOptions Options,
		UGeometryScriptDebug* Debug = nullptr);

	/**
	* Simplifies the mesh until a target vertex count is reached. Behavior can be additionally controlled with the Options. 
	*/
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Simplification", meta=(ScriptMethod, HidePin = "Debug"))
	static UE_API UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplySimplifyToVertexCount(  
		UDynamicMesh* TargetMesh, 
		int32 VertexCount,
		FGeometryScriptSimplifyMeshOptions Options,
		UGeometryScriptDebug* Debug = nullptr);

	/**
	* Simplifies the mesh until a target triangle count is reached, using the UE Editor's standard mesh simplifier. Editor only.
	*/
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Simplification", meta = (ScriptMethod, HidePin = "Debug"))
	static UE_API UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyEditorSimplifyToTriangleCount(  
		UDynamicMesh* TargetMesh, 
		int32 TriangleCount,
		UGeometryScriptDebug* Debug = nullptr);

	/**
	* Simplifies the mesh until a target vertex count is reached, using the UE Editor's standard mesh simplifier. Editor only.
	*/
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Simplification", meta=(ScriptMethod, HidePin = "Debug"))
	static UE_API UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplyEditorSimplifyToVertexCount(  
		UDynamicMesh* TargetMesh, 
		int32 VertexCount,
		UGeometryScriptDebug* Debug = nullptr);

	/**
	* Simplifies the mesh to a target geometric tolerance. Stops when any further simplification would result in a deviation from the input mesh larger than the tolerance.
	* Behavior can be additionally controlled with the Options. 
	*/
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Simplification", meta=(ScriptMethod, HidePin = "Debug"))
	static UE_API UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplySimplifyToTolerance(  
		UDynamicMesh* TargetMesh, 
		float Tolerance,
		FGeometryScriptSimplifyMeshOptions Options,
		UGeometryScriptDebug* Debug = nullptr);

	/**
	 * Simplifies the mesh to a target edge length, using error-based edge collapse.
	 * Note: Not intended to create uniform edge lengths. Result may have edge lengths much larger than the target value.
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Simplification", meta=(ScriptMethod, HidePin = "Debug"))
	static UE_API UPARAM(DisplayName = "Target Mesh") UDynamicMesh* 
	ApplySimplifyToEdgeLength(  
		UDynamicMesh* TargetMesh, 
		double EdgeLength,
		FGeometryScriptSimplifyMeshOptions Options,
		UGeometryScriptDebug* Debug = nullptr);

	/**
	 * Simplifies the mesh to a target edge length, using a fast cluster-based method (not error-based).
	 * Note: Lengths are approximated via 'graph distance' on the original mesh; will not exactly match the target.
	 */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|Simplification", meta = (ScriptMethod, HidePin = "Debug"))
	static UE_API UPARAM(DisplayName = "Target Mesh") UDynamicMesh*
	ApplyClusterSimplifyToEdgeLength(
		UDynamicMesh* TargetMesh,
		double EdgeLength,
		FGeometryScriptClusterSimplifyMeshOptions Options,
		UGeometryScriptDebug* Debug = nullptr);

};

#undef UE_API
