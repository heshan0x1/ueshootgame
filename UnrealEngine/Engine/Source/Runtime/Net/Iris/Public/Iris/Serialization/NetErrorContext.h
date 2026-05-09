// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/NameTypes.h"

#include "Iris/ReplicationSystem/NetRefHandle.h"

namespace UE::Net
{

// Some common errors. Licensees should add their own headers if they want to easily share errors between files.
// 下列 FName 定义在 NetErrorContext.cpp，作为 Iris Serialization 层"预置错误码"。
// 使用方式：Serializer 内任意函数调用 `Context.SetError(UE::Net::GNetError_*)`；
// Licensees 可以自行声明额外的 `extern const FName` 错误码便于跨文件共享。
// 关键点：SetError 会同时把当前位流标记 overflow（除非显式 bDoOverFlow=false），从而让
// 后续 Read/Write 短路，阻断更多错误的级联写入/读取。

/** 位流溢出：读越界 / 写越界。常见于位流容量不足或解码得到畸形长度。 */
IRISCORE_API extern const FName GNetError_BitStreamOverflow;
/** 位流内容不自洽（解码失败）。例如 PackedUint 的尾部未终止、字符串非法等。 */
IRISCORE_API extern const FName GNetError_BitStreamError;
/** 反序列化得到的数组元素数超过 Config 允许的最大值（`FArrayPropertyNetSerializerConfig::MaxElementCount`）。 */
IRISCORE_API extern const FName GNetError_ArraySizeTooLarge;
/** 反序列化得到的 NetRefHandle 无效（例如未在 ObjectReferenceCache 中注册）。 */
IRISCORE_API extern const FName GNetError_InvalidNetHandle;
/** NetRefHandle 关联的对象已损坏或被释放。 */
IRISCORE_API extern const FName GNetError_BrokenNetHandle;
/** 值落在 Config 约束的合法范围之外（如整型超出 LowerBound/UpperBound）。 */
IRISCORE_API extern const FName GNetError_InvalidValue;
/** Iris 内部错误——通常为 bug / 协议不匹配。 */
IRISCORE_API extern const FName GNetError_InternalError;

/**
 * FNetErrorContext —— 仅跟踪"首个错误"的轻量记录器。
 *
 * 使用方式：
 *   `FNetSerializationContext` 内嵌一份本对象。Serializer 通过 `Context.SetError(FName)`
 *   注入错误，同一 context 生命周期内只会保留第一次报告的错误（后续 SetError 将被忽略）。
 *
 * 关联 NetRefHandle：
 *   调用 `SetObjectHandle` 可以附加"触发错误的对象"，上层（日志/NACK）据此定位到具体 Actor。
 */
class FNetErrorContext
{
public:

	/** 当前是否已记录过错误。Error.IsNone() 即未出错。 */
	bool HasError() const;
	
	/** If an error has already been set calling this function again will be a no-op. */
	/** 记录错误；若已记录过则本调用为 no-op（保留首个错误）。传入 NAME_None 会触发 ensure。 */
	IRISCORE_API void SetError(const FName Error);

	/** 获取错误名；未出错返回 NAME_None。 */
	FName GetError() const { return Error; }

	/** 关联触发错误的对象句柄（可用于日志"哪个 Actor 出错"）。 */
	void SetObjectHandle(const FNetRefHandle& InObjectHandle) { ObjectHandle = InObjectHandle; }

	/** 读取已设置的对象句柄；未设置则为 InvalidNetRefHandle。 */
	const FNetRefHandle& GetObjectHandle() const { return ObjectHandle; }

private:

	FNetRefHandle ObjectHandle; // 触发错误的对象引用，供定位
	FName Error;                // 首个报告的错误名；NAME_None 表示未出错
};

inline bool FNetErrorContext::HasError() const
{
	return !Error.IsNone();
}

}
