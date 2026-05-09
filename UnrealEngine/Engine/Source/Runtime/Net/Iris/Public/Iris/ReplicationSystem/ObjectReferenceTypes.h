// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================
// 文件作用 (中文说明)
//   定义 Iris 复制系统中 与“对象引用 / 异步加载”相关的公共类型枚举:
//     - EIrisAsyncLoadingPriority: 复制属性所引用包的异步加载优先级 (Default/High/VeryHigh)
//     - 类型别名 TAsyncLoadPriority (来自 UObjectGlobals.h, 实际就是 int32 数值)
//   Cpp 端通过 CVar (net.iris.AsyncLoading.*Priority) 把枚举映射到具体数值, 在
//   ObjectReferenceCache 解析路径(包含 path-based 静态对象)时调用 LoadPackageAsync()
//   作为异步加载优先级实参传入。
//
//   该枚举使用 UENUM(), 其位宽由 GetBitsNeeded(Max) 计算 (当前 = 2bit), 在
//   WriteMustBeMappedExports / ReadMustBeMappedExports 中按 N bit 写读, 用于在
//   reliable batch 中告知接收端 “该批所引用包的异步加载优先级”。
//
//   引用关系：
//     ObjectReferenceCache.cpp -> StartAsyncLoadingPackage(...AsyncLoadingPriority)
//                              -> LoadPackageAsync(...优先级数字)
//     UObjectReplicationBridge -> 把 EIrisAsyncLoadingPriority 写入 NetRefHandleManager
// =====================================================================================================

#pragma once

#include "Iris/Core/BitTwiddling.h"

#include "ObjectReferenceTypes.generated.h"

// 来自 UObjectGlobals.h: 引擎用来表示异步加载优先级的整型, 数字越大越优先。
// From UObjectGlobals.h
typedef int32 TAsyncLoadPriority;

/**
 * Iris 复制系统专用的“异步加载优先级”枚举, 仅有 3 档:
 *   Default(普通) / High(重要) / VeryHigh(关键)
 * 通过 ConvertAsyncLoadingPriority() 转换为具体的整型优先级 (CVar 决定)。
 *
 * 序列化注意:
 *   - Max = VeryHigh 用于计算所需位数 (GetBitsNeeded), 当前为 2bit
 *   - Invalid 仅用于代码中检测 "未赋值" 的情况, 不能写入到 .ini Config
 */
UENUM()
enum class EIrisAsyncLoadingPriority : uint8
{
	/** 默认优先级。一般情况下使用, 数值最低。 */
	/** The default loading priority used for all types. Generally corresponds to 0 */
	Default,
	/** 高优先级, 用于重要类型 (例如玩家相关 Actor)。 */
	/** A loading priority setting for important classes. */
	High,
	/** 极高优先级, 用于关键类型 (例如必须立即可用的对象)。 */
	/** A loading priority setting for critical classes. */
	VeryHigh,
	/** 计算位数用的"最大值哨兵"。注意它与 VeryHigh 同值, 仅供 GetBitsNeeded 使用。 */
	/** Maximum possible value to the calculate minimum bits needed to serialize the enum */
	Max = VeryHigh,
	/** 不要写到 Config 里, 仅供运行时判断 "是否被赋值过"。 */
	/** Don't use this in the config. Used in code to tell if a value was read and set. */
	Invalid,
};

/** 序列化 EIrisAsyncLoadingPriority 时所需的位数 (取决于 Max 的值, 当前为 2 bit)。*/
static uint32 GetIrisAsyncLoadingPriorityBits()
{
	return UE::Net::GetBitsNeeded(EIrisAsyncLoadingPriority::Max);
}

/**
 * 将 Iris 优先级枚举映射为引擎层 TAsyncLoadPriority 数值。
 * 具体数值通过 CVar 控制 (见 ObjectReferenceTypes.cpp):
 *   - net.iris.AsyncLoading.DefaultPriority   (默认 200)
 *   - net.iris.AsyncLoading.HighPriority      (默认 225)
 *   - net.iris.AsyncLoading.VeryHighPriority  (默认 250)
 */
TAsyncLoadPriority ConvertAsyncLoadingPriority(EIrisAsyncLoadingPriority IrisPriority);

/** 将枚举转换为可读字符串, 用于日志/调试输出。 */
static const TCHAR* LexToString(EIrisAsyncLoadingPriority Priority)
{
	switch(Priority)
	{
		case EIrisAsyncLoadingPriority::Default:
		{
			return TEXT("Default");
		} break;

		case EIrisAsyncLoadingPriority::High:
		{
			return TEXT("High");
		} break;

		case EIrisAsyncLoadingPriority::VeryHigh:
		{
			return TEXT("VeryHigh");
		} break;

		case EIrisAsyncLoadingPriority::Invalid:
		{
			return TEXT("Invalid");
		} break;

		default:
		{
			ensure(false);
			return TEXT("Missing");
		} break;
	}
}