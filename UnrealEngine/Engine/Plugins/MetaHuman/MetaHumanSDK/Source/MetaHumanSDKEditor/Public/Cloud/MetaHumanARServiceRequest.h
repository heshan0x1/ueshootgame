// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Cloud/MetaHumanServiceRequest.h"

#define UE_API METAHUMANSDKEDITOR_API

class IDNAReader;
namespace UE::MetaHuman
{
	enum class ERigType
	{
		JointsOnly,
		JointsAndBlendshapes,
	};

	enum class ERigRefinementLevel
	{
		None,
		Medium
	};

	enum class EExportLayers
	{
		Default, // can be anything, down to the current service implementation. Only use if you really don't care
		None,
		Rbf,
	};

	struct FTargetSolveParameters
	{
		FTargetSolveParameters() = default;

		TArray<FVector> ConformedFaceVertices;
		TArray<FVector> ConformedLeftEyeVertices;
		TArray<FVector> ConformedRightEyeVertices;
		TArray<FVector> ConformedTeethVertices;
		TArray<FVector> ConformedCartilageVertices;
		TArray<FVector> ConformedEyeEdgeVertices;
		TArray<FVector> ConformedEyeShellVertices;
		TArray<FVector> ConformedEyeLashesVertices;
		TArray<FVector> ConformedSalivaVertices;
		TArray<float> BindPose;
		TArray<float> Coefficients;
		int32 HighFrequency = 0;
		ERigType RigType = ERigType::JointsOnly;
		ERigRefinementLevel RigRefinementLevel = ERigRefinementLevel::None;
		EExportLayers ExportLayers = EExportLayers::Rbf;
		float Scale = 1.0f;
		FString ModelIdentifier;
	};

	class FAutoRigServiceRequest;
	class FAutorigResponse
	{
		friend class FAutoRigServiceRequest;
		FAutorigResponse(const TArray<uint8>& Content)
			: Payload(Content)
		{
			bValid = ReadDna();
		}
	public:
		bool IsValid() const 
		{
			return bValid;
		}

		// DNA that can be applied to skeletal mesh.
		TSharedPtr<::IDNAReader> Dna;

	private:
		//NOTE: this payload data's lifetime is limited by the duration of the delegate recieving the response data
		const TArray<uint8>& Payload;
		bool bValid;
		UE_API bool ReadDna();
	};

	/*
	* Request to autorig service
	* Usage
	*	* Bind to the required Delegates (in this class and in FMetaHumanServiceRequestBase as needed)
	*	* Create an instance of a request using CreateRequest
	*	* Run the request
	* 
	* The request will return a raw DNA file (in memory)
	*/
	class FAutoRigServiceRequest final
		: public FMetaHumanServiceRequestBase
	{
	public:
		FAutoRigServiceRequest() = default;

		DECLARE_DELEGATE_OneParam(FAutorigRequestCompleteDelegate, const FAutorigResponse& Response);
		FAutorigRequestCompleteDelegate AutorigRequestCompleteDelegate;

		// create a new autorig service request for a mesh
		static UE_API TSharedRef<FAutoRigServiceRequest> CreateRequest(const FTargetSolveParameters& InSolveParams);
		// execute the solve request asynchronously
		UE_API void RequestSolveAsync();

	protected:
		UE_API virtual bool DoBuildRequest(TSharedRef<IHttpRequest> HttpRequest, FRequestContextBasePtr Context) override;
		UE_API virtual void OnRequestCompleted(const TArray<uint8>& Content, FRequestContextBasePtr Context) override;
	private:
		FTargetSolveParameters SolveParameters;
	};
}

#undef UE_API
