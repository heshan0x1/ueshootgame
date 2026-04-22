// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class UPCGComputeKernel;
struct FPCGComputeGraphContext;

namespace PCGCopyPointsKernel
{
	/** Performs data validation common to all copy points kernels. */
	bool IsKernelDataValid(const UPCGComputeKernel* InKernel, const FPCGComputeGraphContext* InContext);
}
