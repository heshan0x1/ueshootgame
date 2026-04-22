// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Rendering/NaniteStreamingManager.h"	// TODO: For FPageKey. Refactor

class FRDGBuilder;
class FRDGBuffer;

namespace Nanite
{

class FStreamingPageUploader
{
	struct FAddedPageInfo
	{
		FPageKey	GPUPageKey;
		uint32		SrcPageOffset;
		uint32		DstPageOffset;
		uint32		PageDependenciesOffset;
		uint32		NumPageDependencies;
		uint32		ClustersOffset;
		uint32		NumClusters;
		uint32		InstallPassIndex;
	};

	struct FPassInfo
	{
		uint32 NumPages;
		uint32 NumClusters;
	};
public:
	FStreamingPageUploader();

	void Init(FRDGBuilder& GraphBuilder, uint32 InMaxPages, uint32 InMaxPageBytes, uint32 InMaxStreamingPages);
	uint8* Add_GetRef(uint32 PageSize, uint32 NumClusters, uint32 DstPageOffset, const FPageKey& GPUPageKey, const TArray<uint32>& PageDependencies);
	
	void Release();

	void ResourceUploadTo(FRDGBuilder& GraphBuilder, FRDGBuffer* DstBuffer);
private:
	TRefCountPtr<FRDGPooledBuffer> ClusterInstallInfoUploadBuffer;
	TRefCountPtr<FRDGPooledBuffer> PageUploadBuffer;
	TRefCountPtr<FRDGPooledBuffer> PageDependenciesBuffer;
	uint8*					PageDataPtr;
	uint32					MaxPages;
	uint32					MaxPageBytes;
	uint32					MaxStreamingPages;
	uint32					NextPageByteOffset;
	uint32					NextClusterIndex;
	TArray<FAddedPageInfo>	AddedPageInfos;
	TMap<FPageKey, uint32>	GPUPageKeyToAddedIndex;
	TArray<uint32>			FlattenedPageDependencies;
	TArray<FPassInfo>		PassInfos;
	
	void ResetState();
};

} // namespace Nanite