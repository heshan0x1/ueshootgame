// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#define UE_API METAHUMANPIPELINECORE_API

namespace UE::MetaHuman::Pipeline
{

class FNode;

class FConnection
{
public:

	UE_API FConnection(const TSharedPtr<FNode>& InFrom, const TSharedPtr<FNode>& InTo, int32 InFromGroup = 0, int32 InToGroup = 0);

	TSharedPtr<FNode> From;
	TSharedPtr<FNode> To;
	int32 FromGroup;
	int32 ToGroup;

	UE_API FString ToString() const;
};

}

#undef UE_API
