// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

// =============================================================================================
// QuantizedRemoteObjectReference.h
// ---------------------------------------------------------------------------------------------
// FQuantizedRemoteObjectReference：远程对象引用的 Quantized/POD 表示。
//
// 用途：
//   - 仅在 UE_WITH_REMOTE_OBJECT_HANDLE=1 的多服务器/Mesh 部署中使用。
//   - 对应公开层的 FRemoteObjectReference：用 (ObjectId, ServerId, PathName) 三元组
//     在跨服务器边界上定位对象——本地 NetRefHandle 体系无法跨服务器解析。
//   - 由 FRemoteObjectReferenceNetSerializer 进行 Quantize / Dequantize / Serialize；
//     在 FQuantizedObjectReference 中作为"远程态"通过指针动态分配挂载（详见
//     QuantizedObjectReference.h 的二态合一说明）。
//
// 字段布局（POD，按位拷贝安全）：
//   - ObjectId (uint64) ：对象在源服务器的全局 ID。0 表示无效。
//   - ServerId (uint16) ：对象所在源服务器 ID。0 表示无效。
//   - QuantizedPathNameStruct[32]：对象路径名（FName 链）的 Quantized 形式占位 buffer，
//                                  alignas(8) 保证 NameNetSerializer 可直接 placement-write。
//                                  实际内容由 NameNetSerializer 解释，外部不应直接读写。
//
// 与文档对照：Docs/Modules/Serialization.md §1.4「引用/PackageMap」"远程引用"。
// =============================================================================================

#include "HAL/Platform.h"

namespace UE::Net
{

// Quantized/POD state for FRemoteObjectReference
/**
 * 远程对象引用的量化态。整体 POD：48 字节，可按位复制（但其内嵌的 PathName Quantized 数据
 * 由 NameNetSerializer 拥有，clone 时仍需走相应 NetSerializer 的 CloneDynamicState）。
 */
struct FQuantizedRemoteObjectReference
{
	/** 对象在源服务器上的全局 ID。0 视为无效。 */
	uint64 ObjectId;
	/** 对象所属源服务器的 ID。0 视为无效。 */
	uint16 ServerId;
	/** 路径名 Quantized 表示占位 buffer。32 字节 + 8 字节对齐，对应 NameNetSerializer 的
	 *  GetNameNetSerializerSafeQuantizedSize()；不要直接读写，必须经过 NameNetSerializer。 */
	alignas(8) uint8 QuantizedPathNameStruct[32];

	/** 同时要求 ObjectId 和 ServerId 都非零才视为有效；任一为 0 都视作"未设置"。 */
	bool IsValid() const
	{
		return ObjectId != 0 && ServerId != 0;
	}

	/** 仅用 ObjectId 判等：因为同一对象在不同时刻可能持有不同的 ServerId/PathName 缓存，
	 *  但 ObjectId 是稳定的全局 ID。 */
	bool operator==(const FQuantizedRemoteObjectReference& Other) const
	{
		// Equal only if the object itself is the same
		return ObjectId == Other.ObjectId;
	}
};

}
