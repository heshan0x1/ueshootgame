// Copyright Epic Games, Inc. All Rights Reserved.
// =============================================================================
// 中文说明：
//   FNetSerializerRegistryDelegates 的实现。本类作为"扩展点基类"，将派生类
//   挂接到 Private 层的 FInternalNetSerializerDelegates 多播委托上，自动管理
//   订阅/反订阅生命周期。
//
//   订阅时机：构造函数。每个 FNetSerializerRegistryDelegates 实例在被创建时
//   就立即把自己注册到 Pre/PostFreeze 多播委托；这意味着它必须在
//   "BroadcastPreFreezeNetSerializerRegistry()"被调用之前完成构造，才能收到
//   PreFreeze 通知——典型用法是模块加载阶段就把它定义为静态全局对象。
//
//   反订阅时机：析构函数。即便 PreFreeze/PostFreeze 在 Broadcast 时已经
//   Clear() 了订阅者列表，析构里仍要保护性地 Remove（如果 Handle 还有效）。
// =============================================================================

#include "Iris/Serialization/NetSerializerDelegates.h"
#include "Iris/Serialization/InternalNetSerializerDelegates.h"

namespace UE::Net
{

using namespace UE::Net::Private;

FNetSerializerRegistryDelegates::FNetSerializerRegistryDelegates(EFlags Flags)
// 构造时立即将本对象的成员函数 AddRaw 到内部多播委托，并保存 FDelegateHandle 以便析构时 Remove。
// 注：此处用 AddRaw 而非 AddUObject——本类不是 UObject，由派生方负责生命周期。
: PreFreezeDelegate(FInternalNetSerializerDelegates::GetPreFreezeNetSerializerRegistryDelegate().AddRaw(this, &FNetSerializerRegistryDelegates::PreFreezeNetSerializerRegistry))
, PostFreezeDelegate(FInternalNetSerializerDelegates::GetPostFreezeNetSerializerRegistryDelegate().AddRaw(this, &FNetSerializerRegistryDelegates::PostFreezeNetSerializerRegistry))
{
	// 仅当显式指定订阅模块加载/卸载事件时，才再额外绑定 LoadedModulesUpdated。
	// 大多数自定义 Serializer 不需要此回调；只有依赖"已加载模块集合"做缓存的
	// Serializer（如多态 struct 类型表）才打开此开关。
	if ((Flags & EFlags::ShouldBindLoadedModulesUpdatedDelegate) != 0U)
	{
		LoadedModulesUpdatedDelegate = FInternalNetSerializerDelegates::GetLoadedModulesUpdatedDelegate().AddRaw(this, &FNetSerializerRegistryDelegates::LoadedModulesUpdated);
	}
}

FNetSerializerRegistryDelegates::FNetSerializerRegistryDelegates()
// 默认构造委托给带 Flags 的构造，标志位为 None。
: FNetSerializerRegistryDelegates(EFlags::None)
{
}

FNetSerializerRegistryDelegates::~FNetSerializerRegistryDelegates()
{
	// 析构时按需反订阅。每个 Handle 在 PreFreeze/PostFreeze 已被广播一次后会被
	// PreFreezeNetSerializerRegistry/PostFreezeNetSerializerRegistry 内部 Reset()——
	// 此时 IsValid() 为 false，跳过 Remove。
	// 但若派生对象在 PreFreeze 之前就被销毁（例如插件被卸载），仍需主动 Remove
	// 以免悬挂指针被广播。
	if (PreFreezeDelegate.IsValid())
	{
		FInternalNetSerializerDelegates::GetPreFreezeNetSerializerRegistryDelegate().Remove(PreFreezeDelegate);
	}
	if (PostFreezeDelegate.IsValid())
	{
		FInternalNetSerializerDelegates::GetPostFreezeNetSerializerRegistryDelegate().Remove(PostFreezeDelegate);
	}
	if (LoadedModulesUpdatedDelegate.IsValid())
	{
		FInternalNetSerializerDelegates::GetLoadedModulesUpdatedDelegate().Remove(LoadedModulesUpdatedDelegate);
	}
}

// ----- 三个虚函数的"什么都不做"基类实现：派生类按需重写。 -----

void FNetSerializerRegistryDelegates::OnPreFreezeNetSerializerRegistry()
{
	// 默认空实现：派生类一般会在此调用 UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO
	// 等宏来注册自家 NetSerializer。
}

void FNetSerializerRegistryDelegates::OnPostFreezeNetSerializerRegistry()
{
	// 默认空实现：派生类一般会在此查询 FReplicationStateDescriptor 或缓存
	// 转发型 Serializer 所需的内部 Serializer 引用。
}

void FNetSerializerRegistryDelegates::OnLoadedModulesUpdated()
{
	// 默认空实现：派生类按需在此刷新与已加载模块集合相关的缓存。
}

// ----- 三个内部桥接函数：由 InternalDelegates 广播触发，转调虚函数。 -----

void FNetSerializerRegistryDelegates::PreFreezeNetSerializerRegistry()
{
	// 1. 转调虚函数，让派生类执行其 PreFreeze 逻辑。
	OnPreFreezeNetSerializerRegistry();
	// 2. 保险起见从多播委托主动 Remove（防止 Clear 顺序与析构竞争）。
	FInternalNetSerializerDelegates::GetPreFreezeNetSerializerRegistryDelegate().Remove(PreFreezeDelegate);
	// 3. Reset Handle，使 dtor 跳过重复 Remove。
	PreFreezeDelegate.Reset();
}

void FNetSerializerRegistryDelegates::PostFreezeNetSerializerRegistry()
{
	// 同 PreFreeze 流程：先转调虚函数，再清理订阅句柄，确保仅一次性触发。
	OnPostFreezeNetSerializerRegistry();
	FInternalNetSerializerDelegates::GetPostFreezeNetSerializerRegistryDelegate().Remove(PostFreezeDelegate);
	PostFreezeDelegate.Reset();
}

void FNetSerializerRegistryDelegates::LoadedModulesUpdated()
{
	// LoadedModulesUpdated 是多次触发事件，不需要 Remove/Reset Handle。
	OnLoadedModulesUpdated();
}

}
