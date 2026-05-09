// Copyright Epic Games, Inc. All Rights Reserved.
// =============================================================================
// 中文说明：
//   FNetSerializerRegistryDelegates 是 Iris 序列化层的"扩展点基类"。任何想为
//   自定义 UStruct/UScriptStruct 注册 FNetSerializer、或者在 Iris 序列化注册表
//   生命周期关键节点执行初始化/清理的模块，都应该继承本类，并按需重写以下三个
//   虚函数：
//     - OnPreFreezeNetSerializerRegistry()  在注册表"冻结"前调用，最常见用途
//       是为某个 struct 名字调用 UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO
//       或 FPropertyNetSerializerInfoRegistry::Register 注册自家 Serializer。
//     - OnPostFreezeNetSerializerRegistry() 在注册表冻结后调用，此时可以拿到
//       某个 struct 的 FReplicationStateDescriptor（PreFreeze 时还不能）。
//     - OnLoadedModulesUpdated() 模块加载/卸载后回调；只有在 ctor 显式传入
//       EFlags::ShouldBindLoadedModulesUpdatedDelegate 才会被绑定。
//
//   使用模式：
//     class FMyNetSerializerRegistry final : public UE::Net::FNetSerializerRegistryDelegates {
//         virtual void OnPostFreezeNetSerializerRegistry() override { ... }
//     };
//     // 在模块作用域定义一个静态实例：ctor 自动订阅，dtor 自动反订阅。
//     static FMyNetSerializerRegistry GMyRegistry;
//
//   生命周期细节由 Private/Iris/Serialization/InternalNetSerializerDelegates 持有
//   的 FSimpleMulticastDelegate 触发；ctor 会向其 AddRaw(this, &PreFreeze...)，
//   dtor 会 Remove。PreFreeze/PostFreeze 是一次性事件，触发后 InternalDelegates
//   会 Clear() 所有订阅者；LoadedModulesUpdated 不清空，会反复触发。
// =============================================================================

#pragma once

#include "Delegates/Delegate.h"

namespace UE::Net
{

/**
 * Helper class for registering NetSerializers.
 * Override OnPreFreezeNetSerializerRegistry() to register your serializer
 * and OnPostFreezeNetSerializerRegistry() to perform any additional fixup
 * needed, such as creating a descriptor for a struct that you forward your
 * NetSerializer's calls to.
 * The virtual functions OnPreFreezeNetSerializerRegistry and OnPostFreezeNetSerializerRegistry will be called zero or one times each.
 *
 * 中文：用于"注册自定义 NetSerializer"的辅助基类，封装了 Iris 序列化注册表
 *      生命周期事件的订阅与反订阅。OnPreFreezeNetSerializerRegistry 与
 *      OnPostFreezeNetSerializerRegistry 这两个虚函数最多只会被各调用一次
 *      （事件本身就是一次性的）。
 */
class FNetSerializerRegistryDelegates
{
public:

	/**
	 * 构造时的可选行为开关位（按位 OR 组合）。
	 */
	enum EFlags : uint32
	{
		/** 默认：不订阅 LoadedModulesUpdated。 */
		None = 0U,
		/**
		 * 订阅 LoadedModulesUpdated 事件（每次模块加载/卸载触发）。
		 * 仅在 Serializer 需要根据其它模块加载状态刷新缓存时才设置；
		 * 大多数自定义 Serializer 不需要此开关。
		 */
		ShouldBindLoadedModulesUpdatedDelegate = 1U,
	};

	/** 默认构造：等价于使用 EFlags::None；仅订阅 PreFreeze/PostFreeze。 */
	IRISCORE_API FNetSerializerRegistryDelegates();

	/**
	 * 显式指定订阅开关位的构造函数。
	 * - 始终订阅 PreFreeze 与 PostFreeze 单播-多播事件；
	 * - 如果 Flags 含 ShouldBindLoadedModulesUpdatedDelegate，
	 *   再额外订阅 LoadedModulesUpdated。
	 */
	explicit IRISCORE_API FNetSerializerRegistryDelegates(EFlags Flags);

	/** Implement a destructor to unregister your NetSerializers. */
	/**
	 * 中文：派生类应在自己的析构函数中"反注册"那些在 OnPreFreeze 阶段
	 *      注册到全局 Serializer 注册表的自定义 Serializer，避免热重载时悬挂。
	 *      本基类的析构会自动从 InternalNetSerializerDelegates 的多播委托中
	 *      Remove 掉本对象的订阅。
	 */
	IRISCORE_API virtual ~FNetSerializerRegistryDelegates();

protected:
	/**
	 * Pre freeze can be called before there are any serializers registered.
	 * At this point it's fine to register custom serializers for structs, but if you
	 * need a descriptor for a struct as a helper for your serializer you need to wait
	 * until post freeze.
	 *
	 * 中文：PreFreeze 阶段——这是"在注册表冻结前"的窗口。此时尚无任何 Iris
	 *      默认 Serializer 注册到全局表；适合调用宏 UE_NET_IMPLEMENT_NAMED_STRUCT_*
	 *      或 FPropertyNetSerializerInfoRegistry::Register 把自己的 Serializer
	 *      插入注册表。但由于其它 struct 的 ReplicationStateDescriptor 还未生成，
	 *      不能在此调用 GetReplicationStateDescriptor()——那要放到 PostFreeze。
	 */
	IRISCORE_API virtual void OnPreFreezeNetSerializerRegistry();

	/**
	 * Post freeze is called after all loaded modules, including this one, has
	 * registered their serializers. At this point you should be able to get
	 * a descriptor for a struct that contains types that your module depends on.
	 *
	 * 中文：PostFreeze 阶段——所有模块（包含本模块）的 Serializer 都已注册并
	 *      冻结。可以安全地：
	 *        - 通过 FPropertyNetSerializerInfoRegistry 查询某 struct 的 Serializer；
	 *        - 调用 FReplicationStateDescriptorBuilder 为某 UScriptStruct 构建
	 *          描述符（用作"转发型 Serializer"的内部依赖）。
	 */
	IRISCORE_API virtual void OnPostFreezeNetSerializerRegistry();

	/**
	 * OnLoadedModulesUpdated delegate will be called every time a module has been loaded or unloaded
	 * It gives the serializer an opportunity to update cached data that could be affected by loading or unloading modules.
	 * NOTE: It will only be called if the EFlags::ShouldBindLoadedModulesUpdatedDelegate is set
	 *
	 * 中文：模块加载/卸载回调（多次触发）。仅当 ctor 传入
	 *      ShouldBindLoadedModulesUpdatedDelegate 时才会被绑定与回调；用于
	 *      刷新与"已加载模块集合"相关的缓存（如多态 struct 的支持类型表）。
	 */
	IRISCORE_API virtual void OnLoadedModulesUpdated();


private:

	/** 内部桥接函数：被 InternalDelegates 多播触发，转调 OnPreFreezeNetSerializerRegistry，并反订阅自身（一次性）。 */
	void PreFreezeNetSerializerRegistry();
	/** 内部桥接函数：被 InternalDelegates 多播触发，转调 OnPostFreezeNetSerializerRegistry，并反订阅自身（一次性）。 */
	void PostFreezeNetSerializerRegistry();
	/** 内部桥接函数：转调 OnLoadedModulesUpdated（不反订阅，可被多次触发）。 */
	void LoadedModulesUpdated();

	/** ctor 中 AddRaw 拿到的 PreFreeze 订阅句柄；析构时若仍有效则 Remove。 */
	FDelegateHandle PreFreezeDelegate;
	/** ctor 中 AddRaw 拿到的 PostFreeze 订阅句柄；析构时若仍有效则 Remove。 */
	FDelegateHandle PostFreezeDelegate;
	/** ctor 中（仅当指定 ShouldBindLoadedModulesUpdatedDelegate）AddRaw 拿到的 LoadedModulesUpdated 订阅句柄。 */
	FDelegateHandle LoadedModulesUpdatedDelegate;
};

}
