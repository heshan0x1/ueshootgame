// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 【文件总览 - MassObserverProcessor.cpp】
// UMassObserverProcessor 的实现：
//   - 构造：禁用"自动注册到 processing phases"，因为 observer 不参与每帧 tick；
//     编辑器中也不显示在 Mass 的 settings 列表里（避免被误改）。
//   - PostInitProperties：CDO 自动注册到 ObserverRegistry 的入口；
//     兼容旧 Operation 字段：自动迁移到 ObservedOperations 位标志。
//   - Register：默认注册逻辑，子类可覆盖。
// =============================================================================

#include "MassObserverProcessor.h"
#include "MassObserverRegistry.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MassObserverProcessor)

//----------------------------------------------------------------------//
// UMassObserverProcessor
//----------------------------------------------------------------------//
// 【中文注释】构造：把 observer 与普通 processor 的两点关键区别设上：
//   1. bAutoRegisterWithProcessingPhases = false：observer 不参与每帧 phase 调度，
//      它只在事件触发时执行；
//   2. bCanShowUpInSettings = false：编辑器 Mass 设置面板不展示 observer，
//      避免与普通 processor 混淆（observer 应通过自动注册而非手动编辑）。
UMassObserverProcessor::UMassObserverProcessor()
{
	bAutoRegisterWithProcessingPhases = false;
#if WITH_EDITORONLY_DATA
	bCanShowUpInSettings = false;
#endif // WITH_EDITORONLY_DATA
}

// 【中文注释】PostInitProperties：CDO 完成属性初始化后调用。
// 流程：
//   1. 仅 CDO 且非 abstract 类才走注册流程（abstract 基类不应自注册）；
//   2. 兼容旧字段：如果 Operation（5.7 已废弃）非 MAX，把它转成对应的 flag 合并到 ObservedOperations，
//      然后清空 Operation 避免重复迁移；
//   3. 同时校验 ObservedType + ObservedOperations 都有效后才调 Register；
//   4. 否则若声明了自动注册却配置不全，记日志报错（防止默默失效）。
void UMassObserverProcessor::PostInitProperties()
{
	Super::PostInitProperties();

	UClass* MyClass = GetClass();
	CA_ASSUME(MyClass);

	if (HasAnyFlags(RF_ClassDefaultObject) && MyClass->HasAnyClassFlags(CLASS_Abstract) == false)
	{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		// 【中文注释】兼容旧 Operation 字段：迁移到新的 ObservedOperations 位标志。
		// 旧 API 只支持 Add / Remove 单选；新 API 支持多 op 位组合。
		if (Operation != EMassObservedOperation::MAX)
		{
			ObservedOperations |= (Operation == EMassObservedOperation::Add)
				? EMassObservedOperationFlags::Add
				: EMassObservedOperationFlags::Remove;
			Operation = EMassObservedOperation::MAX;
		}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		if (ObservedType != nullptr && ObservedOperations != EMassObservedOperationFlags::None)
		{
			Register();
		}
		else if (bAutoRegisterWithObserverRegistry)
		{
			// 【中文注释】配置不完整警告：要么没设 ObservedType，要么没设 ObservedOperations，
			// 但 bAutoRegisterWithObserverRegistry 是 true —— 说明子类有意自动注册却忘了设元数据。
			UE_LOG(LogMass, Error, TEXT("%hs attempting to register %s while it\'s misconfigured, Type: %s, OperationFlags: %#x")
				, __FUNCTION__, *MyClass->GetName(), *GetNameSafe(ObservedType), ObservedOperations);
		}
	}
}

// 【中文注释】默认 Register 实现：直接把当前 (ObservedType, ObservedOperations, GetClass()) 写入 Registry。
// 子类如果想监听多个 type 或做自定义注册，可以 override 此方法（例如循环调用 Registry.RegisterObserver）。
void UMassObserverProcessor::Register()
{
	if (bAutoRegisterWithObserverRegistry)
	{
		check(ObservedType);
		UMassObserverRegistry::GetMutable().RegisterObserver(ObservedType, static_cast<uint8>(ObservedOperations), GetClass());
	}
}


