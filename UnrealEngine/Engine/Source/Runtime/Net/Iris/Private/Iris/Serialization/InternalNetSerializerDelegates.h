// Copyright Epic Games, Inc. All Rights Reserved.
// =============================================================================
// 中文说明：
//   本头文件定义 Iris Serialization 模块内部使用的"NetSerializer 注册生命周期
//   多播委托持有者" FInternalNetSerializerDelegates。
//
//   它在 Iris 启动期间被 FIrisCoreModule::StartupModule()（具体为
//   RegisterPropertyNetSerializerSelectorTypes()）按以下顺序触发：
//     1) BroadcastPreFreezeNetSerializerRegistry()  —— 各模块自定义 Serializer
//        的 PreFreeze 钩子；此时还没有任何 Serializer 已注册到全局注册表，
//        适合做"为某 struct 注册自定义 NetSerializer"。
//     2) RegisterDefaultPropertyNetSerializerInfos() —— 注册 Iris 默认提供的
//        Property → NetSerializer 绑定。
//     3) FreezeNetSerializerRegistry() —— 冻结注册表，禁止再添加。
//     4) BroadcastPostFreezeNetSerializerRegistry() —— PostFreeze 钩子；
//        此时已可通过名字拿到 FNetSerializer / 构建 ReplicationStateDescriptor。
//     5) （可选）BroadcastLoadedModulesUpdated() —— 当模块加载/卸载时调用，
//        允许监听者刷新缓存（仅订阅了 ShouldBindLoadedModulesUpdatedDelegate
//        的监听者会被调用）。
//
//   PreFreeze/PostFreeze 在 Broadcast 后会立即 Clear()，确保只触发一次；
//   LoadedModulesUpdated 不 Clear，会在每次模块变化时反复触发。
//
//   外部模块通过 Public 头 NetSerializerDelegates.h 中的
//   FNetSerializerRegistryDelegates（其 ctor 自动 AddRaw 到这里的多播）来订阅。
// =============================================================================

#pragma once

#include "Delegates/Delegate.h"

namespace UE::Net::Private
{

/**
 * Iris 内部 NetSerializer 注册生命周期多播委托的"全局持有者"。
 *
 * 设计要点：
 *  - 三个静态 FSimpleMulticastDelegate 的实例存放在三个 Get* 函数内的静态局部
 *    变量中（Meyers Singleton），保证首次访问时被构造，跨翻译单元下也保持
 *    单一实例。
 *  - Broadcast* 系列由 IrisCoreModule 调用；Clear() 用于 PreFreeze/PostFreeze
 *    这两个"一次性"事件，防止重复触发。
 *  - LoadedModulesUpdated 不 Clear，因为它会随 FModuleManager::OnModulesChanged
 *    多次触发。
 */
class FInternalNetSerializerDelegates
{
public:
	/** 触发 PreFreeze 阶段：广播给所有监听者，然后清空委托列表（仅触发一次）。 */
	static void BroadcastPreFreezeNetSerializerRegistry();
	/** 触发 PostFreeze 阶段：广播给所有监听者，然后清空委托列表（仅触发一次）。 */
	static void BroadcastPostFreezeNetSerializerRegistry();
	/** 模块加载/卸载时触发；不清空，可被多次调用。 */
	static void BroadcastLoadedModulesUpdated();

	/** 取得 PreFreeze 多播委托引用，供监听者 AddRaw / Remove。 */
	static FSimpleMulticastDelegate& GetPreFreezeNetSerializerRegistryDelegate();
	/** 取得 PostFreeze 多播委托引用，供监听者 AddRaw / Remove。 */
	static FSimpleMulticastDelegate& GetPostFreezeNetSerializerRegistryDelegate();
	/** 取得 LoadedModulesUpdated 多播委托引用（仅 ShouldBindLoadedModulesUpdatedDelegate 订阅者会用到）。 */
	static FSimpleMulticastDelegate& GetLoadedModulesUpdatedDelegate();
};

}
