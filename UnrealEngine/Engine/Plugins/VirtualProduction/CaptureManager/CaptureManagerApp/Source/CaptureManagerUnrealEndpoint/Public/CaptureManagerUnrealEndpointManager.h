// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CaptureManagerUnrealEndpoint.h"

#include "Templates/PimplPtr.h"

#include "CoreMinimal.h"

namespace UE::CaptureManager
{

/**
* @brief Detects and manages ingest endpoints (UE/UEFN instances) for the Capture Manager.
*/
UE_INTERNAL class CAPTUREMANAGERUNREALENDPOINT_API FUnrealEndpointManager
{
public:
	DECLARE_TS_MULTICAST_DELEGATE(FEndpointsChanged);

	FUnrealEndpointManager();
	~FUnrealEndpointManager();

	/**
	* @brief Start discovering endpoints.
	*/
	void Start();

	/**
	* @brief Stop discovering endpoints.
	*/
	void Stop();

	/**
	* @brief Blocks until an endpoint matching the given predicate is discovered or the timeout (in milliseconds) is reached.
	*
	* @returns An optional containing a weak pointer to the endpoint if it was discovered else the optional will be empty.
	*/
	TOptional<TWeakPtr<FUnrealEndpoint>> WaitForEndpoint(TFunction<bool(const FUnrealEndpoint&)> InPredicate, int32 InTimeoutMS);

	/**
	* @brief Finds a discovered endpoint matching the given predicate.
	*
	* @returns An optional containing a weak pointer to the endpoint if it is found else the optional will be empty.
	*/
	TOptional<TWeakPtr<FUnrealEndpoint>> FindEndpointByPredicate(TFunction<bool(const FUnrealEndpoint&)> InPredicate);

	/**
	* @returns A list of weak pointers to the discovered endpoints which match the predicate.
	*/
	TArray<TWeakPtr<FUnrealEndpoint>> FindEndpointsByPredicate(TFunction<bool(const FUnrealEndpoint&)> InPredicate);

	/**
	* @returns A list of weak pointers to the discovered endpoints.
	*/
	TArray<TWeakPtr<FUnrealEndpoint>> GetEndpoints();

	/**
	* @returns The number of discovered endpoints
	*/
	int32 GetNumEndpoints() const;

	/**
	* @returns A delegate which fires whenever the discovered endpoints have changed.
	*/
	FEndpointsChanged& EndpointsChanged();

private:
	struct FImpl;
	TPimplPtr<FImpl> Impl;
};

}
