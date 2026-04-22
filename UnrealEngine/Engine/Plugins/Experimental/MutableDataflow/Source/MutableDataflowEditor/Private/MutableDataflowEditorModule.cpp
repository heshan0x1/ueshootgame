// Copyright Epic Games, Inc. All Rights Reserved.

#include "MutableDataflowEditorModule.h"

#include "Dataflow/DataflowAnyTypeRegistry.h"
#include "Dataflow/DataflowNode.h"
#include "Dataflow/DataflowCore.h"
#include "Nodes/COInstanceGeneratorNode.h"
#include "Nodes/MutableMaterialParameterNode.h"
#include "Nodes/MutableSkeletalMeshParameterNode.h"
#include "Nodes/MutableTextureParameterNode.h"
#include "Nodes/MakeMutableMaterialParametersArrayNode.h"
#include "Nodes/MakeMutableSkeletalMeshParametersArrayNode.h"
#include "Nodes/MakeMutableTextureParametersArrayNode.h"

IMPLEMENT_MODULE(FMutableDataflowEditorModule, MutableDataflowEditor);

void FMutableDataflowEditorModule::StartupModule()
{
	// Main nodes
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCOInstanceGeneratorNode);
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FCOInstanceGetComponentMesh);

	// Auto connection between an array of FMutableGeneratedResource to a single TObjectPtr<USkeletalMesh>
	UE_DATAFLOW_REGISTER_AUTOCONVERT(TArray<FMutableGeneratedResource>, TObjectPtr<USkeletalMesh>, FCOInstanceGetComponentMesh);
	
	// Parameter nodes
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMutableSkeletalMeshParameterNode);
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMutableTextureParameterNode);
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMutableMaterialParameterNode);

	// Parameter array generation nodes
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeMutableSkeletalMeshParametersArrayNode);
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeMutableMaterialParametersArrayNode);
	DATAFLOW_NODE_REGISTER_CREATION_FACTORY(FMakeMutableTextureParametersArrayNode);

	// Parameter node to array automatic conversion nodes
	UE_DATAFLOW_REGISTER_AUTOCONVERT(FMutableSkeletalMeshParameter, TArray<FMutableSkeletalMeshParameter>, FMakeMutableSkeletalMeshParametersArrayNode);
	UE_DATAFLOW_REGISTER_AUTOCONVERT(FMutableMaterialParameter, TArray<FMutableMaterialParameter>, FMakeMutableMaterialParametersArrayNode);
	UE_DATAFLOW_REGISTER_AUTOCONVERT(FMutableTextureParameter, TArray<FMutableTextureParameter>, FMakeMutableTextureParametersArrayNode);
}
