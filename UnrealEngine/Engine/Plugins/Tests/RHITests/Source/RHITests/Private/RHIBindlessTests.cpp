// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHIBindlessTests.h"
#include "RHIResourceUtils.h"
#include "RHI.h"

bool RHIBindlessTests::Test_ResourceCollection(FRHICommandListImmediate& RHICmdList)
{
	if (!GRHIGlobals.bSupportsBindless || !IsBindlessEnabledForAnyGraphics(RHIGetRuntimeBindlessConfiguration(GMaxRHIShaderPlatform)))
	{
		return true;
	}

	const uint32 BufferContents[] = { 1, 2, 3, 4 };
	const uint32 TextureContents[] = { 1 };

	const FRHIBufferCreateDesc BufferDesc =
		FRHIBufferCreateDesc::CreateByteAddress(TEXT("ResourceCollection_Buffer"), sizeof(BufferContents), sizeof(BufferContents[0]))
		.SetInitialState(ERHIAccess::SRVMask)
		.SetInitActionInitializer();
	TRHIBufferInitializer<uint32> BufferInitializer = RHICmdList.CreateBufferInitializer(BufferDesc);
	{
		BufferInitializer.WriteArray(MakeConstArrayView(BufferContents));
	}
	FBufferRHIRef TestBuffer = BufferInitializer.Finalize();

	FRHIViewDesc::FBufferSRV::FInitializer BufferSRVDesc = FRHIViewDesc::CreateBufferSRV().SetType(FRHIViewDesc::EBufferType::Raw);
	FShaderResourceViewRHIRef BufferSRV = RHICmdList.CreateShaderResourceView(TestBuffer, BufferSRVDesc);

	const FRHITextureCreateDesc TextureDesc =
		FRHITextureCreateDesc::Create2D(TEXT("ResourceCollection_Texture"), 1, 1, PF_R32_UINT)
		.SetInitialState(ERHIAccess::SRVMask)
		.SetInitActionInitializer();
	FRHITextureInitializer TextureInitializer = RHICmdList.CreateTextureInitializer(TextureDesc);
	{
		TextureInitializer.GetTexture2DSubresource(0).WriteData(&TextureContents[0], sizeof(TextureContents));
	}
	FTextureRHIRef TestTexture = TextureInitializer.Finalize();

	// Empty
	{
		FRHIResourceCollectionRef EmptyCollection = RHICmdList.CreateResourceCollection({});
	}

	// Normal Creation
	{
		TArray<FRHIResourceCollectionMember> Members;
		Members.Emplace(BufferSRV.GetReference());
		Members.Emplace(TestTexture.GetReference());

		FRHIResourceCollectionRef ResourceCollection = RHICmdList.CreateResourceCollection(Members);
	}

	// Updates
	{
		TArray<FRHIResourceCollectionMember> Members;
		Members.Emplace(BufferSRV.GetReference());
		Members.Emplace(TestTexture.GetReference());

		FRHIResourceCollectionRef ResourceCollection = RHICmdList.CreateResourceCollection(Members);

		RHICmdList.UpdateResourceCollection(ResourceCollection, 0, Members);
	}

	return true;
}
