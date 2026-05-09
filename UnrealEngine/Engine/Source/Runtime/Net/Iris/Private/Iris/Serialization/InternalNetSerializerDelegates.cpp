// Copyright Epic Games, Inc. All Rights Reserved.
// =============================================================================
// 中文说明：
//   FInternalNetSerializerDelegates 的实现。三个多播委托的载体为三个静态局部
//   变量（Meyers Singleton），由 IrisCoreModule.StartupModule 在
//   RegisterPropertyNetSerializerSelectorTypes() 内部按以下序列触发：
//
//        BroadcastPreFreezeNetSerializerRegistry()
//          → 所有监听者执行 OnPreFreezeNetSerializerRegistry
//          → 清空 PreFreeze 委托列表（一次性）
//        RegisterDefaultPropertyNetSerializerInfos()  // Iris 默认 Property→Serializer 绑定
//        FreezeNetSerializerRegistry()                // 冻结注册表
//        BroadcastPostFreezeNetSerializerRegistry()
//          → 所有监听者执行 OnPostFreezeNetSerializerRegistry
//          → 清空 PostFreeze 委托列表（一次性）
//
//   而 LoadedModulesUpdated 由 FModuleManager::OnModulesChanged 间接触发，
//   不会 Clear，可被多次广播。
// =============================================================================

#include "Iris/Serialization/InternalNetSerializerDelegates.h"

namespace UE::Net::Private
{

void FInternalNetSerializerDelegates::BroadcastPreFreezeNetSerializerRegistry()
{
	// 取得 PreFreeze 多播委托。
	FSimpleMulticastDelegate& PreFreezeNetSerializerRegistryDelegate = GetPreFreezeNetSerializerRegistryDelegate();
	// 通知所有订阅者（FNetSerializerRegistryDelegates::PreFreezeNetSerializerRegistry）。
	PreFreezeNetSerializerRegistryDelegate.Broadcast();
	// 清空委托列表：PreFreeze 是"一次性"事件，避免后续重复触发或内存泄漏。
	PreFreezeNetSerializerRegistryDelegate.Clear();
}

void FInternalNetSerializerDelegates::BroadcastPostFreezeNetSerializerRegistry()
{
	// 取得 PostFreeze 多播委托。
	FSimpleMulticastDelegate& PostFreezeNetSerializerRegistryDelegate = GetPostFreezeNetSerializerRegistryDelegate();
	// 通知所有订阅者（此时全局 NetSerializer 注册表已 Freeze，可安全查询）。
	PostFreezeNetSerializerRegistryDelegate.Broadcast();
	// 同样清空，确保 PostFreeze 是一次性事件。
	PostFreezeNetSerializerRegistryDelegate.Clear();
}

void FInternalNetSerializerDelegates::BroadcastLoadedModulesUpdated()
{
	// 注意：此处不 Clear()——LoadedModulesUpdated 是多次触发事件
	// （每次 FModuleManager::OnModulesChanged 都会调用），监听者保持订阅。
	FSimpleMulticastDelegate& LoadedModulesUpdatedDelegate = GetLoadedModulesUpdatedDelegate();
	LoadedModulesUpdatedDelegate.Broadcast();
}

FSimpleMulticastDelegate& FInternalNetSerializerDelegates::GetPreFreezeNetSerializerRegistryDelegate()
{
	// Meyers Singleton：首次调用时构造，跨翻译单元唯一。
	static FSimpleMulticastDelegate PreFreezeNetSerializerRegistryDelegate;
	return PreFreezeNetSerializerRegistryDelegate;
}

FSimpleMulticastDelegate& FInternalNetSerializerDelegates::GetPostFreezeNetSerializerRegistryDelegate()
{
	// Meyers Singleton。
	static FSimpleMulticastDelegate PostFreezeNetSerializerRegistryDelegate;
	return PostFreezeNetSerializerRegistryDelegate;
}

FSimpleMulticastDelegate& FInternalNetSerializerDelegates::GetLoadedModulesUpdatedDelegate()
{
	// Meyers Singleton。
	static FSimpleMulticastDelegate LoadedModulesUpdatedDelegate;
	return LoadedModulesUpdatedDelegate;
}

}
