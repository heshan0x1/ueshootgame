// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StaticArray.h"
#include "RHIDefinitions.h"
#include "VulkanCommon.h"
#include "VulkanMemory.h"
#include "VulkanThirdParty.h"
#include "RHIDescriptorAllocator.h"

class FVulkanDevice;
class FVulkanBuffer;

// Manager for resource descriptors used in bindless rendering.
class FVulkanBindlessDescriptorManager
{
public:
	FVulkanBindlessDescriptorManager(FVulkanDevice& InDevice);
	~FVulkanBindlessDescriptorManager();

	typedef TStaticArray<TArray<VkDescriptorAddressInfoEXT>, ShaderStage::MaxNumStages> FUniformBufferDescriptorArrays;

	void Init();
	void Deinit();

	bool IsSupported()
	{
		return bIsSupported;
	}

	VkPipelineLayout GetPipelineLayout() const
	{
		return BindlessPipelineLayout;
	}

	void BindDescriptorBuffers(VkCommandBuffer CommandBuffer, VkPipelineStageFlags SupportedStages);

	FRHIDescriptorHandle AllocateDescriptor(VkDescriptorType DescriptorType);
	FRHIDescriptorHandle AllocateDescriptor(VkDescriptorType DescriptorType, VkImageView VulkanImage, bool bIsDepthStencil);
	FRHIDescriptorHandle AllocateDescriptor(VkDescriptorType DescriptorType, VkDeviceAddress BufferAddress, VkDeviceSize BufferSize);
	FRHIDescriptorHandle AllocateDescriptor(VkDescriptorType DescriptorType, FVulkanBuffer* Buffer, uint32 ExtraOffset);

	FRHIDescriptorHandle AllocateDescriptor(VkSampler VulkanSampler);

	void FreeDescriptor(FRHIDescriptorHandle DescriptorHandle);

	void UpdateSampler(const FVulkanContextArray& Contexts, FRHIDescriptorHandle DescriptorHandle, VkSampler VulkanSampler);
	void UpdateImage(const FVulkanContextArray& Contexts, FRHIDescriptorHandle DescriptorHandle, VkDescriptorType DescriptorType, VkImageView VulkanImage, bool bIsDepthStencil, bool bImmediateUpdate = true);

	void UpdateBuffer(const FVulkanContextArray& Contexts, FRHIDescriptorHandle DescriptorHandle, VkDescriptorType DescriptorType, VkBuffer VulkanBuffer, VkDeviceSize BufferOffset, VkDeviceSize BufferSize, bool bImmediateUpdate = true);
	void UpdateBuffer(const FVulkanContextArray& Contexts, FRHIDescriptorHandle DescriptorHandle, VkDescriptorType DescriptorType, VkDeviceAddress BufferAddress, VkDeviceSize BufferSize, bool bImmediateUpdate = true);

	void UpdateTexelBuffer(const FVulkanContextArray& Contexts, FRHIDescriptorHandle DescriptorHandle, VkDescriptorType DescriptorType, const VkBufferViewCreateInfo& ViewInfo, bool bImmediateUpdate = true);
	void UpdateAccelerationStructure(const FVulkanContextArray& Contexts, FRHIDescriptorHandle DescriptorHandle, VkAccelerationStructureKHR AccelerationStructure, bool bImmediateUpdate = true);

	void RegisterUniformBuffers(FVulkanCommandListContext& Context, VkPipelineBindPoint BindPoint, const FUniformBufferDescriptorArrays& StageUBs);

	void UpdateUBAllocator();

private:
	FVulkanDevice& Device;
	const bool bIsSupported;

	VkDescriptorSetLayout EmptyDescriptorSetLayout = VK_NULL_HANDLE;

	struct BindlessSetState
	{
		~BindlessSetState()
		{
			if (CPUDescriptorMemory)
			{
				FMemory::Free(CPUDescriptorMemory);
			}
		}

		TArray<VkDescriptorType, TInlineAllocator<2>> DescriptorTypes;
		FRHIHeapDescriptorAllocator* Allocator = nullptr;

		VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;

		uint32 DescriptorCapacity = 0;
		uint32 DescriptorSize = 0;

		VkBuffer BufferHandle = VK_NULL_HANDLE;
		VkDeviceMemory MemoryHandle = VK_NULL_HANDLE;
		uint8* MappedPointer = nullptr;

		uint8* CPUDescriptorMemory = nullptr;
	};
	BindlessSetState BindlessSetStates[VulkanBindless::NumBindlessSets];

	VkDescriptorSetLayout SingleUseUBDescriptorSetLayout = VK_NULL_HANDLE;
	VulkanRHI::FTempBlockAllocator* SingleUseUBAllocator = nullptr;

	VkDescriptorBufferBindingInfoEXT BufferBindingInfo[VulkanBindless::NumBindlessSets];
	uint32_t BufferIndices[VulkanBindless::MaxNumSets];

	VkPipelineLayout BindlessPipelineLayout = VK_NULL_HANDLE;

	BindlessSetState& GetBindlessState(ERHIDescriptorType DescriptorType);
	BindlessSetState& GetBindlessState(VkDescriptorType DescriptorType);

	void UpdateDescriptor(const FVulkanContextArray& Contexts, FRHIDescriptorHandle DescriptorHandle, VkDescriptorType DescriptorType, VkDescriptorDataEXT DescriptorData, bool bImmediateUpdate);
	static bool VerifySupport(FVulkanDevice& InDevice);
};
