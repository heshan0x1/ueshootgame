// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Templates/SubclassOf.h"

class UPCGGraph;
class UPCGNode;
class UPCGPin;
class UPCGSettings;
class UObject;

class FPCGSubgraphHelpers
{
public:
	static PCG_API UPCGGraph* CollapseIntoSubgraphWithReason(UPCGGraph* InOriginalGraph, const TArray<UPCGNode*>& InNodesToCollapse, const TArray<UObject*>& InExtraNodesToCollapse, FText& OutFailReason, UPCGGraph* OptionalPreAllocatedGraph = nullptr);
	static PCG_API UPCGGraph* CollapseIntoSubgraph(UPCGGraph* InOriginalGraph, const TArray<UPCGNode*>& InNodesToCollapse, const TArray<UObject*>& InExtraNodesToCollapse, UPCGGraph* OptionalPreAllocatedGraph = nullptr);

	/** Generic function to spawn a node and connect it to the default in and out pins. */
	static PCG_API UPCGNode* SpawnNodeAndConnect(UPCGGraph* InGraph, UPCGPin* InUpstreamPin, UPCGPin* InDownstreamPin, TSubclassOf<UPCGSettings> InSettingsClass, const TFunction<bool(UPCGSettings*)>& PostSettingsCreation = {});
};