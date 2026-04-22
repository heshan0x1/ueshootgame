// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Nodes/HyprsenseNodeBase.h"

#define UE_API METAHUMANPIPELINE_API

namespace UE::MetaHuman::Pipeline
{
	class FHyprsenseSparseNode : public FHyprsenseNodeBase
	{
	public:
		UE_API FHyprsenseSparseNode(const FString& InName);

		UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
		UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;
		
		UE_API bool SetTrackers(const TSharedPtr<UE::NNE::IModelInstanceGPU>& InFaceTracker, const TSharedPtr<UE::NNE::IModelInstanceGPU>& InFaceDetector);
	};


	// The managed node is a version of the above that take care of loading the correct NNE models
	// rather than these being specified by an externally.

	class FHyprsenseSparseManagedNode : public FHyprsenseSparseNode
	{
	public:
		UE_API FHyprsenseSparseManagedNode(const FString& InName);

	};
}

#undef UE_API
