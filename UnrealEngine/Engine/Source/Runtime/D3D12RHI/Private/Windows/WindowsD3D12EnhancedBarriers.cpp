// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12RHIDefinitions.h"

#if D3D12RHI_SUPPORTS_ENHANCED_BARRIERS

#include "D3D12ThirdParty.h"

D3D12_BARRIER_LAYOUT GetSkipFastClearEliminateLayoutFlags()
{
	return {};
}

#endif // D3D12RHI_SUPPORTS_ENHANCED_BARRIERS