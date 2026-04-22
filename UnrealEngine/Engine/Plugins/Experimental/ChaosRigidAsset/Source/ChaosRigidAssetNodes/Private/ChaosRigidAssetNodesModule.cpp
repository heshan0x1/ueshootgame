// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosRigidAssetNodesModule.h"
#include "ShapeElemNodes.h"
#include "PhysicsAssetDataflowNodes.h"

IMPLEMENT_MODULE(FChaosRigidAssetNodesModule, ChaosRigidAssetNodes);

 void FChaosRigidAssetNodesModule::StartupModule()
{
	UE::Dataflow::RegisterPhysicsAssetTerminalNode();
	UE::Dataflow::RegisterPhysicsAssetNodes();
	UE::Dataflow::RegisterShapeNodes();
}

 void FChaosRigidAssetNodesModule::ShutdownModule()
{

}
