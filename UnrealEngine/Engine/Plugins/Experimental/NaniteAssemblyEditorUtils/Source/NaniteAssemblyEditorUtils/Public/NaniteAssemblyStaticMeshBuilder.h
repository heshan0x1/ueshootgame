// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NaniteAssemblyBuilder.h"

#include "NaniteAssemblyStaticMeshBuilder.generated.h"

class USceneComponent;
class UStaticMeshComponent;

UCLASS(BlueprintType)
class NANITEASSEMBLYEDITORUTILS_API UNaniteAssemblyStaticMeshBuilder : public UNaniteAssemblyBuilder
{
	GENERATED_BODY()

public:	
	UNaniteAssemblyStaticMeshBuilder(const FObjectInitializer& Initializer);

	virtual UObject* GetTargetMeshObject() const override { return TargetMesh; }
	
	/**
	 * Creates a Nanite Assembly Static Mesh builder to generate a new Static Mesh asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "Static Mesh")
	static UNaniteAssemblyStaticMeshBuilder* BeginNewStaticMeshAssemblyBuild(const FNaniteAssemblyCreateNewParameters& Parameters);
	
	/**
	 * Creates a Nanite Assembly Static Mesh builder to add assembly parts to the specified Static Mesh, or overwrite its
	 * assembly parts if it is an existing Nanite Assembly mesh.
	 */
	UFUNCTION(BlueprintCallable, Category = "Static Mesh")
	static UNaniteAssemblyStaticMeshBuilder* BeginEditStaticMeshAssemblyBuild(UStaticMesh* BaseMesh);
	
	bool BeginAssemblyBuild(UStaticMesh* InTargetMesh);

	/** Finalizes the assembly build and returns the finished assembly mesh. */
	UFUNCTION(BlueprintCallable, Category = "Static Mesh", meta = ( ReturnDisplayName = "Success" ))
	bool FinishAssemblyBuild(UStaticMesh*& OutStaticMesh);

	bool AddAssemblyParts(
		const UStaticMesh* PartMesh,
		TArrayView<const FTransform> LocalTransforms,
		const FNaniteAssemblyMaterialMergeOptions& MaterialMergeOptions = FNaniteAssemblyMaterialMergeOptions()
	);

	/** Adds instances of the specified mesh to the assembly with the specified local transforms */
	UFUNCTION(BlueprintCallable, Category = "Assembly Parts", meta = ( ReturnDisplayName = "Success", AutoCreateRefTerm = "MaterialMergeOptions" ))
	bool AddAssemblyParts(
		const UStaticMesh* PartMesh,
		const TArray<FTransform>& LocalTransforms,
		const FNaniteAssemblyMaterialMergeOptions& MaterialMergeOptions = FNaniteAssemblyMaterialMergeOptions()
	)
	{
		return AddAssemblyParts(PartMesh, TArrayView<const FTransform>(LocalTransforms), MaterialMergeOptions);
	}

	/** Adds an instance of the specified mesh to the assembly with the specified local transform */
	UFUNCTION(BlueprintCallable, Category = "Assembly Parts", meta = ( ReturnDisplayName = "Success", AutoCreateRefTerm = "MaterialMergeOptions" ))
	bool AddAssemblyPart(
		const UStaticMesh* PartMesh,
		const FTransform& LocalTransform,
		const FNaniteAssemblyMaterialMergeOptions& MaterialMergeOptions = FNaniteAssemblyMaterialMergeOptions()
	)
	{
		return AddAssemblyParts(PartMesh, MakeConstArrayView(&LocalTransform, 1), MaterialMergeOptions);
	}

	/**
	 * Adds the meshes, materials, and instances of the specified component to the assembly.
	 * If an origin object is specified, all instance transforms of the component will be made relative to that object's
	 * world transform, and OriginTransform will be considered relative to the origin object's world-space transform.
	 */
	UFUNCTION(BlueprintCallable, Category = "Assembly Parts", meta = ( ReturnDisplayName = "Success", AutoCreateRefTerm = "OriginTransform, MaterialMergeOptions" ))
	bool AddAssemblyPartsFromComponent(
		const UStaticMeshComponent* PartComponent,
		const FTransform& OriginTransform = FTransform(),
		const USceneComponent* OriginObject = nullptr,
		const FNaniteAssemblyMaterialMergeOptions& MaterialMergeOptions = FNaniteAssemblyMaterialMergeOptions()
	);

private:
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> TargetMesh;
};