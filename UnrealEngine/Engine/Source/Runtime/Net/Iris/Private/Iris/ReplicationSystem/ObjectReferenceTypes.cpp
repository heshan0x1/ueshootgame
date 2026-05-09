// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================
// 文件作用 (中文说明)
//   实现 ObjectReferenceTypes.h 中声明的 ConvertAsyncLoadingPriority(),
//   通过 3 个 CVar 把 EIrisAsyncLoadingPriority 枚举映射为具体整型优先级,
//   供 LoadPackageAsync() / FObjectReferenceCache::StartAsyncLoadingPackage() 使用。
//
//   默认值参考 (可在 .ini / 控制台中改写):
//     net.iris.AsyncLoading.DefaultPriority   = 200
//     net.iris.AsyncLoading.HighPriority      = 225
//     net.iris.AsyncLoading.VeryHighPriority  = 250
// =====================================================================================================

#include "Iris/ReplicationSystem/ObjectReferenceTypes.h"

#include "HAL/IConsoleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ObjectReferenceTypes)

namespace UE::Net::Private
{
	// 默认档(Default)对应的整型优先级值, 经验值 200。
	static int32 IrisDefaultAsyncLoadingPriority = 200;
	static FAutoConsoleVariableRef CVarIrisDefaultAsyncLoadingPriority(
		TEXT("net.iris.AsyncLoading.DefaultPriority"),
		IrisDefaultAsyncLoadingPriority,
		TEXT("The default priority to use when async loading packages referenced by replicated properties.")
		);

	// 高优先级(High)档的整型优先级值, 默认 225。
	static int32 IrisHighAsyncLoadingPriority = 225;
	static FAutoConsoleVariableRef CVarIrisHighAsyncLoadingPriority(
		TEXT("net.iris.AsyncLoading.HighPriority"),
		IrisHighAsyncLoadingPriority,
		TEXT("The high priority value to use when async loading packages referenced by replicated properties.")
		);

	// 极高优先级(VeryHigh)档的整型优先级值, 默认 250。
	static int32 IrisVeryHighAsyncLoadingPriority = 250;
	static FAutoConsoleVariableRef CVarIrisVeryHighAsyncLoadingPriority(
		TEXT("net.iris.AsyncLoading.VeryHighPriority"),
		IrisVeryHighAsyncLoadingPriority,
		TEXT("The very high priority value to use when async loading packages referenced by replicated properties.")
		);

}

/**
 * 将 EIrisAsyncLoadingPriority 转换为引擎可识别的 TAsyncLoadPriority(int32)。
 * - High / VeryHigh -> 对应 CVar 数值
 * - Invalid        -> 触发 ensure 并退化为 Default (说明上层未赋值, 属逻辑 BUG)
 * - 其它(Default)   -> Default 值
 */
TAsyncLoadPriority ConvertAsyncLoadingPriority(EIrisAsyncLoadingPriority IrisPriority)
{
	switch(IrisPriority)
	{
		case EIrisAsyncLoadingPriority::High:
		{
			return UE::Net::Private::IrisHighAsyncLoadingPriority;
		} break;

		case EIrisAsyncLoadingPriority::VeryHigh:
		{
			return UE::Net::Private::IrisVeryHighAsyncLoadingPriority;
		} break;

		case EIrisAsyncLoadingPriority::Invalid:
		{
			ensureMsgf(false, TEXT("Invalid config used, should not happen."));
			return UE::Net::Private::IrisDefaultAsyncLoadingPriority;
		} break;

		default:
		{
			return UE::Net::Private::IrisDefaultAsyncLoadingPriority;
		} break;
	}
}
