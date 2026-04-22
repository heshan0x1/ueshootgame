// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IdleService.h"
#include <vector>

#define UE_API TEXTUREGRAPHENGINE_API

class BlobHelperService : public IdleService
{
protected:
	std::vector<BlobPtr>			Blobs;					/// The blobs that we want to hash

public:
	UE_API explicit						BlobHelperService(const FString& InName);
	UE_API virtual							~BlobHelperService() override;

	UE_API virtual void					Stop() override;

	UE_API virtual void					Add(BlobRef BlobObj);
};

#undef UE_API
