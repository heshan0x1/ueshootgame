// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Nodes/HyprsenseNodeBase.h"

#define UE_API METAHUMANPIPELINE_API

namespace UE::MetaHuman::Pipeline
{
	class FHyprsenseNode : public FHyprsenseNodeBase
	{
	public:
		UE_API FHyprsenseNode(const FString& InName);

		UE_API virtual bool Start(const TSharedPtr<FPipelineData>& InPipelineData) override;
		UE_API virtual bool Process(const TSharedPtr<FPipelineData>& InPipelineData) override;

		UE_API bool SetTrackers(const TSharedPtr<UE::NNE::IModelInstanceGPU>& InFaceTracker, const TSharedPtr<UE::NNE::IModelInstanceGPU>& InFaceDetector, const TSharedPtr<UE::NNE::IModelInstanceGPU>& InEyebrowTracker, const TSharedPtr<UE::NNE::IModelInstanceGPU>& InEyeTracker, const TSharedPtr<UE::NNE::IModelInstanceGPU>& InLipsTracker, const TSharedPtr<UE::NNE::IModelInstanceGPU>& InLipZipTracker, const TSharedPtr<UE::NNE::IModelInstanceGPU>& InNasolabialNoseTracker, const TSharedPtr<UE::NNE::IModelInstanceGPU>& InChinTracker, const TSharedPtr<UE::NNE::IModelInstanceGPU>& InTeethTracker, const TSharedPtr<UE::NNE::IModelInstanceGPU>& InTeethConfidenceTracker);

		bool bAddSparseTrackerResultsToOutput = true;

	};


	// The managed node is a version of the above that take care of loading the correct NNE models
	// rather than these being specified by an externally.

	class FHyprsenseManagedNode : public FHyprsenseNode
	{
	public:
		UE_API FHyprsenseManagedNode(const FString& InName);

	};
}

#undef UE_API
