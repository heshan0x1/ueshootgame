// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifdef __cplusplus
#define UINT32_TYPE uint32
#define UINT64_TYPE uint64
#else
#define UINT32_TYPE uint
#define UINT64_TYPE uint64_t
#endif

// --- Cluster Operations API Structures -----------------------------------------------------------------------------

typedef UINT64_TYPE GPU_VIRTUAL_ADDRESS;

struct GPU_VIRTUAL_ADDRESS_AND_STRIDE
{
	GPU_VIRTUAL_ADDRESS StartAddress;
	UINT64_TYPE         StrideInBytes;
}; // 16 bytes

struct RAYTRACING_CLUSTER_OPS_MOVE_DESC // NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_MOVE_ARGS
{
	GPU_VIRTUAL_ADDRESS SrcAccelerationStructure; // GPUVA of the object to move
}; // 8 bytes

struct RAYTRACING_CLUSTER_OPS_BUILD_CLAS_DESC // NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_CLUSTER_ARGS
{
	UINT32_TYPE ClusterId;
	UINT32_TYPE ClusterFlags;

	UINT32_TYPE TriangleCount : 9;
	UINT32_TYPE VertexCount: 9;
	UINT32_TYPE PositionTruncateBitCount : 6;
	UINT32_TYPE IndexFormat : 4;
	UINT32_TYPE OpacityMicromapIndexFormat : 4;

	UINT32_TYPE BaseGeometryIndexAndFlags;

	UINT32_TYPE IndexBufferStride : 16;
	UINT32_TYPE VertexBufferStride : 16;

	UINT32_TYPE GeometryIndexAndFlagsBufferStride : 16;
	UINT32_TYPE OpacityMicromapIndexBufferStride : 16;

	GPU_VIRTUAL_ADDRESS IndexBuffer;
	GPU_VIRTUAL_ADDRESS VertexBuffer;
	GPU_VIRTUAL_ADDRESS GeometryIndexAndFlagsBuffer;
	GPU_VIRTUAL_ADDRESS OpacityMicromapArray;
	GPU_VIRTUAL_ADDRESS OpacityMicromapIndexBuffer;
}; // 64 bytes

struct RAYTRACING_CLUSTER_OPS_BUILD_CLAS_TEMPLATE_DESC // NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_TRIANGLE_TEMPLATE_ARGS
{
	UINT32_TYPE ClusterId;
	UINT32_TYPE ClusterFlags;

	UINT32_TYPE TriangleCount : 9;
	UINT32_TYPE VertexCount : 9;
	UINT32_TYPE PositionTruncateBitCount : 6;
	UINT32_TYPE IndexFormat : 4;
	UINT32_TYPE OpacityMicromapIndexFormat : 4;

	UINT32_TYPE BaseGeometryIndexAndFlags;

	UINT32_TYPE IndexBufferStride : 16;
	UINT32_TYPE VertexBufferStride : 16;

	UINT32_TYPE GeometryIndexAndFlagsBufferStride  : 16;
	UINT32_TYPE OpacityMicromapIndexBufferStride : 16;

	GPU_VIRTUAL_ADDRESS IndexBuffer;
	GPU_VIRTUAL_ADDRESS VertexBuffer;
	GPU_VIRTUAL_ADDRESS GeometryIndexAndFlagsBuffer;
	GPU_VIRTUAL_ADDRESS OpacityMicromapArray;
	GPU_VIRTUAL_ADDRESS OpacityMicromapIndexBuffer;
	GPU_VIRTUAL_ADDRESS InstantiationBoundingBoxLimit;
}; // 72 bytes

struct RAYTRACING_CLUSTER_OPS_INSTANTIATE_CLAS_TEMPLATE_DESC // NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_INSTANTIATE_TEMPLATE_ARGS
{
	UINT32_TYPE ClusterIdOffset;
	UINT32_TYPE GeometryIndexOffset;

	GPU_VIRTUAL_ADDRESS ClusterTemplate;
	GPU_VIRTUAL_ADDRESS_AND_STRIDE VertexBuffer;
}; // 32 bytes

struct RAYTRACING_CLUSTER_OPS_BUILD_BLAS_DESC // NVAPI_D3D12_RAYTRACING_ACCELERATION_STRUCTURE_MULTI_INDIRECT_CLUSTER_ARGS
{
	UINT32_TYPE ClusterCount;
	UINT32_TYPE Reserved;
	GPU_VIRTUAL_ADDRESS ClusterVAs; // Array of CLAS addresses
}; // 16 bytes
