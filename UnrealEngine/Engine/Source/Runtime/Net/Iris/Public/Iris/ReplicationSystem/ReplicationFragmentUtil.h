// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// ReplicationFragmentUtil.h —— Fragment 自动构建工具门面
// ---------------------------------------------------------------------------------------------------------------------
// 文件位置：Public/Iris/ReplicationSystem/  ← 对外 API。
// 角色：把"给一个 UObject 自动创建并注册 ReplicationFragment 集合"的过程封装为静态门面：
//   - 通过反射触发 FReplicationStateDescriptorBuilder 烘焙 Class 级别的 Descriptor 列表；
//   - 对每个 Descriptor 选择正确的 Fragment 实现：
//        * 若 Descriptor 提供了 CreateAndRegisterReplicationFragmentFunction（用户自定义）→ 用之；
//        * 否则 → 默认 FPropertyReplicationFragment（FastArray 的特化由 FastArrayReplicationFragment 通过
//                 CreateAndRegisterReplicationFragmentFunction 钩子提供）；
//   - 把每个新创建的 Fragment 注册进 FFragmentRegistrationContext，便于上层取出。
//
// 与文档对应：
//   - ReplicationSystem.md §2.6（序列化/状态拷贝路径的入口）。
//   - Iris_Architecture.md §3.7（Descriptor + Fragment 协作）。
//
// 调用关系：
//   UObjectReplicationBridge::BeginReplication
//     └─> 各 Owner 的 RegisterReplicationFragments
//             └─> FReplicationFragmentUtil::CreateAndRegisterFragmentsForObject
//                     ├─> FReplicationStateDescriptorBuilder::CreateDescriptorsForClass（反射烘焙）
//                     └─> 对每个 Desc：调用 Desc->CreateAndRegisterReplicationFragmentFunction 或
//                                       FPropertyReplicationFragment::CreateAndRegisterFragment
// =====================================================================================================================

#pragma once
#include "CoreTypes.h"
#include "Containers/ContainersFwd.h"

#include "Iris/ReplicationSystem/ReplicationFragment.h"

class UObject;

namespace UE::Net
{

/** FPropertyReplicationFragment - used to bind PropertyReplicationStates to their owner */
// 静态门面类。所有方法均为 static —— 不需要实例。
class FReplicationFragmentUtil
{
public:

	/** Simple version of CreateAndRegisterFragmentsForObject */
	// 简版重载：直接传入 UObject 与 Flags。等价于把参数包成 FCreateFragmentParams 调用全功能版。
	IRISCORE_API static uint32 CreateAndRegisterFragmentsForObject(UObject* Object, FFragmentRegistrationContext& Context, EFragmentRegistrationFlags RegistrationFlags, TArray<FReplicationFragment*>* OutCreatedFragments = nullptr);


	// CreateAndRegisterFragmentsForObject 的入参集合。
	struct FCreateFragmentParams
	{
		/** The replicated object to create fragments for */
		// 待构建 fragment 的复制对象（非空）。
		UObject* ObjectInstance = nullptr;
		/** Flags to control how the fragments should be created */
		// 控制构建策略：仅 RPC、用 CDO 而非 archetype、允许 FastArray 携带额外属性等。
		EFragmentRegistrationFlags RegistrationFlags = EFragmentRegistrationFlags::None;
	};

	/**
	* Create and register all property replication Fragments for the provided object, Descriptors will be created based on the Class of the object
	* Lifetime of created fragments will be managed by the ReplicationSystem
	* If the OutCreatedFragments are provided pointers to the created fragments will be added to the provided array
	* Returns the number of created fragments
	*/
	// 全功能版：
	//   - 内部用反射 +（缓存好的）FReplicationStateDescriptorRegistry 烘焙出 Descriptor 列表；
	//   - 创建对应 Fragment 并注册到 Context；
	//   - 创建出的 Fragment 通过 EReplicationFragmentTraits::DeleteWithInstanceProtocol 由 InstanceProtocol 接管生命周期；
	//   - 可选 OutCreatedFragments 收集"本调用新建"的 fragment 数组（已存在 / 复用的不收）；
	//   - 返回新建 fragment 数量（仅计数 OutCreatedFragments 写入的项）。
	IRISCORE_API static uint32 CreateAndRegisterFragmentsForObject(const FCreateFragmentParams& Params, FFragmentRegistrationContext& Context, TArray<FReplicationFragment*>* OutCreatedFragments = nullptr);
};

}
