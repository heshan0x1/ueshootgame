// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：NetObjectFactoryRegistry.h
// 角色：UNetObjectFactory 类（"客户端如何重建一个对象"的策略类）的全局静态注册表。
//
// 设计：
//   * 注册一个 (FName name, UClass* class) 对，分配 8-bit FNetObjectFactoryId 作为索引；
//   * 所有 NetObjectFactory 必须在任意 ReplicationSystem 创建之前完成注册——
//       Engine 会在 RS 创建后调用 SetFactoryRegistrationAllowed(false) 锁住注册；
//   * Id 通过 FNetObjectCreationHeader.FactoryId 序列化到对端，使用 GetMaxBits() 位；
//   * 容量上限 UE_IRIS_MAX_NETOBJECT_FACTORIES（默认 16，可在工程 Target.cs 提升）。
//
// 使用方式：
//   FNetObjectFactoryRegistry::RegisterFactory(UMyNetObjectFactory::StaticClass(), TEXT("MyFactory"));
//   ...... ReplicationSystem 创建后即不能再 Register/Unregister。
// =====================================================================================

#pragma once

#include "Containers/ArrayView.h"
#include "UObject/Class.h"
#include "UObject/NameTypes.h"
#include "UObject/WeakObjectPtrTemplates.h"

// Define this to increase the maximum amount of NetObjectFactories that can exist
// 工程可以在 Target.cs 中重定义，以提升每进程允许注册的 NetObjectFactory 数量上限。
#ifndef UE_IRIS_MAX_NETOBJECT_FACTORIES
	#define UE_IRIS_MAX_NETOBJECT_FACTORIES 16
#endif

namespace UE::Net
{

/** Factory 的 ID 类型，序列化时占用 GetMaxBits() 位。 */
typedef uint8 FNetObjectFactoryId;
enum { InvalidNetObjectFactoryId = MAX_uint8 };  // 0xFF 表示无效


/**
 * Keeps track of Iris NetObjectFactory templates
 * NetObjectFactories must all be registered before any Iris ReplicationSystem is created.
 */
/**
 * Iris NetObjectFactory 类的全局注册表（static-only，禁止实例化）。
 * 所有注册必须在第一个 ReplicationSystem 创建前完成。
 */
class FNetObjectFactoryRegistry
{
public:

	/** Register a UNetObjectFactory class and associate with a specific name. */
	/**
	 * 将 FactoryClass 与名字绑定，分配新 FactoryId。要求：
	 *   1) FactoryClass 必须派生自 UNetObjectFactory；
	 *   2) FactoryName 不为 NAME_None 且未被注册过；
	 *   3) 总数不超过 MaxFactories；
	 *   4) bIsFactoryRegistrationAllowed == true（即尚未创建 RS）。
	 */
	IRISCORE_API static void RegisterFactory(UClass* FactoryClass, FName FactoryName);

	/** Unregister the UNetObjectFactory class associated with the name */
	/**
	 * 撤销注册：只清空 slot 内容（保留 Id->index 映射，避免错位）。
	 * 同样要求 bIsFactoryRegistrationAllowed == true。
	 */
	IRISCORE_API static void UnregisterFactory(FName FactoryName);

	/** Find the FNetFactoryID that was assigned to name on registration */
	/** 通过名字反查 FactoryId；找不到返回 InvalidNetObjectFactoryId。 */
	IRISCORE_API static FNetObjectFactoryId GetFactoryIdFromName(FName FactoryName);

	/** Id 是否为合法且当前可用的 FactoryId（slot 仍有名字）。 */
	IRISCORE_API static bool IsValidFactoryId(FNetObjectFactoryId Id);

	/** The engine sets this false after it created an Iris replication system since its now illegal to register new factories */
	/**
	 * 引擎在 RS 创建之后会调用 SetFactoryRegistrationAllowed(false)，
	 * 之后任何 Register/Unregister 都会触发 check()。
	 */
	IRISCORE_API static void SetFactoryRegistrationAllowed(bool bAllowed);

	/** Limit how many factories can be registered */
	static constexpr uint32 MaxFactories = UE_IRIS_MAX_NETOBJECT_FACTORIES;

	/** The amount of bits to serialize FNetFactoryIds with  */
	/** 序列化 FactoryId 所需的位数（恰好覆盖 [0, MaxFactories-1]）。 */
	static constexpr uint32 GetMaxBits() { return GetNumBits(MaxFactories-1); }

	/** 单条注册记录。Id 在注册时分配，与 NetFactories 数组下标一致。 */
	struct FFactoryData
	{
		/** Name associated with this factory class */
		FName Name;
		/** FactoryId assigned to this factory */
		FNetObjectFactoryId Id = InvalidNetObjectFactoryId;
		/** Class representing a concrete UNetObjectFactory */
		TWeakObjectPtr<UClass> NetFactoryClass;
	};

	/** The registered factories ready to be instantiated by the replication bridge */
	/** 给 Bridge 实例化 Factory 时使用——返回 const view，不允许外部修改容器。 */
	static const TConstArrayView<FFactoryData> GetRegisteredFactories() { return MakeConstArrayView(NetFactories.GetData(), NetFactories.Num()); }

private:
	
	FNetObjectFactoryRegistry() = delete;
	~FNetObjectFactoryRegistry() = delete;

	// 编译期递归求 Number 所需的最小 bit 数。
	static constexpr uint32 GetNumBits(uint32 Number)
	{
		return Number==0 ? 0 : 1 + GetNumBits(Number >> 1);
	}

private:

	// 全局静态数组，固定容量 = MaxFactories（避免运行期再分配）。
	static TArray<FFactoryData, TFixedAllocator<MaxFactories>> NetFactories;
};

} // end namespace UE::Net