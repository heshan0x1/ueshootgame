// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：NetObjectFactory.h
// 角色：Iris 的"对象创建/重建抽象工厂"。每种被复制的对象家族对应一个 UNetObjectFactory
//       派生类（如 Actor / Component / 自定义 GameplayObject 等）。
//
// 责任：
//   * 序列化端：CreateAndFillHeader -> WriteHeader 把"足够让对端实例化对象的最小信息"
//     塞进位流（含 ProtocolId、FactoryId、对象路径/Archetype/初始数据等）；
//   * 反序列化端：ReadHeader -> InstantiateReplicatedObjectFromHeader 把对象在远端拉起
//     （或绑定到已有实例 / 池），随后由 ReplicationReader 调用 PostInstantiation/PostInit；
//   * 销毁端：DetachedFromReplication 处理"对端已不需要"——可销毁 / 回池 / 仅解绑。
//
// 关键模式：
//   * 抽象工厂（Factory Method）：FNetObjectCreationHeader 是抽象产品，派生 Factory
//     生成具体派生 Header 子类。
//   * 双阶段构造：Instantiate -> ApplyState -> PostInit，保证 RepNotify 在最终态后触发。
//
// 关键 ID：
//   * FactoryId       : 8-bit，序列化时排在最前——接收端先读 FactoryId 找出 Factory，
//                       再调用其 ReadHeader 反序列化 ProtocolId 与具体 Header 内容。
//   * ProtocolId      : 32-bit，对应 FReplicationProtocol 的形态指纹（一致才能 Apply 状态）。
//
// 调试开关：
//   * IRIS_CREATIONHEADER_BITGUARD：在 Header 前后多写 32-bit 长度，
//     接收端比对位数差异以快速定位 Read/Write 不对称的 Bug。
// =====================================================================================

#pragma once

#include "UObject/ObjectMacros.h"

#include "Iris/ReplicationSystem/NetObjectFactoryRegistry.h"
#include "Iris/ReplicationSystem/ReplicationBridgeTypes.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"

#include "Misc/EnumClassFlags.h"
#include "Misc/Optional.h"

#include "NetObjectFactory.generated.h"

class UObjectReplicationBridge;
class UNetObjectFactory;

namespace UE::Net
{
	class FNetSerializationContext;

	struct FNetObjectResolveContext;

	typedef uint32 FReplicationProtocolIdentifier;
}

namespace UE::Net
{

/** Contextual information passed to an header so it can serialize/deserialize itself  */
/**
 * Header 序列化/反序列化时需要的上下文打包：
 *   * Handle        : 当前 Header 描述的对象句柄
 *   * Bridge        : 拥有该 Factory 的 Bridge（提供 Resolve / 注册等服务）
 *   * Factory       : 当前 Factory（可访问 FactoryId 等）
 *   * Serialization : 位流读写器（Reader 或 Writer）
 */
struct FCreationHeaderContext
{
	/** The handle of the replicated object represented by the header */
	FNetRefHandle Handle;
	/** The bridge responsible for the replicated object */
	UObjectReplicationBridge* Bridge;
	/** The factory that allocated the header */
	UNetObjectFactory* Factory;
	/** Access to the bitstream reader or writer */
	FNetSerializationContext& Serialization;

	FCreationHeaderContext(FNetRefHandle InHandle, UObjectReplicationBridge* InBridge, UNetObjectFactory* InFactory, FNetSerializationContext& InSerialization) : Handle(InHandle), Bridge(InBridge), Factory(InFactory), Serialization(InSerialization) {}
};

/*
 * Class holding the raw information allowing any client from retrieving or allocating a replicated UObject instance.
 * Can also implement the serialization of it's data into a bitstream
 */ 
/**
 * 抽象创建 Header：在写包时由 Factory CreateAndFillHeader 生成，序列化到对端供 ReadHeader/Instantiate 使用。
 *
 * 基类只承载 Factory/Protocol Id 两个公共字段，具体内容（路径 / Archetype / 初始 transform 等）
 * 由派生类自行实现 SerializeHeader / DeserializeHeader。
 */ 
class FNetObjectCreationHeader
{
public:

	virtual ~FNetObjectCreationHeader() {};

	void SetProtocolId(uint32 InId) { ProtocolIdentifier = InId; }
	void SetFactoryId(FNetObjectFactoryId InId) { FactoryId = InId; }

	FReplicationProtocolIdentifier GetProtocolId() const { return ProtocolIdentifier; }
	FNetObjectFactoryId GetNetFactoryId() const { return FactoryId; }

	/** Transform the header information into a readable format */
	/** 调试输出：派生类应重载并打印 Path / Class / Archetype 等关键字段。 */
	virtual FString ToString() const { return TEXT("NotImplemented"); }

private:

	FReplicationProtocolIdentifier ProtocolIdentifier = 0;     // 32-bit 协议指纹
	FNetObjectFactoryId  FactoryId = InvalidNetObjectFactoryId;// 8-bit Factory ID
};

} // end namespace UE::Net

/**
 * The class is responsible for creating the header representing specific replicated object types.
 * Also responsible for instantiating the UObject from a replicated header.
 */
/**
 * Iris 网络对象工厂的抽象基类。每个 Factory 类负责一类对象（Actor / Component / 自定义对象）。
 *
 * 派生类必须实现的纯虚：
 *   * CreateAndFillHeader              : 生成并填充派生 Header（写包侧）
 *   * SerializeHeader / CreateAndDeserializeHeader : Header 字段位流读写
 *   * InstantiateReplicatedObjectFromHeader        : 在远端实例化或查找/绑定对象
 *   * DetachedFromReplication                      : 对端不再需要时的销毁/解绑
 *   * GetWorldInfo                                 : 提供位置 / Cull 距离（仅 RootObject）
 *   * GetPollFrequency                             : 提供该对象的轮询频率（Hz）
 *
 * 可选重写：PostInstantiation / PostInit / SubObjectCreatedFromReplication / SubObjectDetachedFromReplication
 */
UCLASS(MinimalAPI, transient, abstract)
class UNetObjectFactory : public UObject
{
	GENERATED_BODY()

public:

	/** 由 Bridge 在 InitNetObjectFactories 时调用，绑定 Id 与所属 Bridge。 */
	void Init(UE::Net::FNetObjectFactoryId InId, UObjectReplicationBridge* InBridge);
	/** 反向解绑（Bridge 反初始化时调用）。 */
	void Deinit();
	/** 接收一帧数据完成后的回调（清理 per-frame 状态）。 */
	void PostReceiveUpdate();

	struct FInstantiateResult;
	struct FInstantiateContext;
	struct FPostInstantiationContext;
	struct FPostInitContext;
	struct FDestroyedContext;
	struct FWorldInfoContext;
	struct FWorldInfoData;

	/**
	 * Creates the header containing all information required to instantiate a remote version of the object represented by the handle.
	 * @param Handle The handle of the object represented by the header
	 * @param ProtocolId The protocol id to add to the header
	 * @return Return a valid header filled with the information that will be sent to remote connections
	 */
	/** 写包侧入口：创建并填充 Header（内部调 CreateAndFillHeader 并塞入 ProtocolId / FactoryId）。 */
	TUniquePtr<UE::Net::FNetObjectCreationHeader> CreateHeader(UE::Net::FNetRefHandle Handle, UE::Net::FReplicationProtocolIdentifier ProtocolId);

	/** 
	 * Serializes a valid header so it can be replicated to remote connections 
	 * @param Handle The handle of the object represented by the header
	 * @param Header The filled header that will be serialized
	 * @return Returns true if the serialization was a success
	 */
	/**
	 * 把 Header 序列化到位流：
	 *   1) 先写 FactoryId（接收端首先读取以路由到正确 Factory）
	 *   2) 再写 ProtocolId（32-bit）
	 *   3) [可选] BitGuard 占位 32-bit
	 *   4) 调派生 SerializeHeader 写具体字段
	 *   5) [可选] 回填实际 bit 数到 BitGuard
	 */
	bool WriteHeader(UE::Net::FNetRefHandle Handle, UE::Net::FNetSerializationContext& Serialization, const UE::Net::FNetObjectCreationHeader* Header);

	/** 
	 * Deserialize the header data received and return a valid header
	 * @param Handle The handle of the object represented by the header
	 * @param Serialization Gives access to the bitstream reader
	 * @return Return a valid header filled with the information that represents the remote object
	 */
	/**
	 * 收包侧：从位流恢复 Header。FactoryId 已被 Bridge 在外层读出用以路由，
	 * 这里读 ProtocolId 并调派生 CreateAndDeserializeHeader 创建并填字段。
	 */
	TUniquePtr<UE::Net::FNetObjectCreationHeader> ReadHeader(UE::Net::FNetRefHandle Handle, UE::Net::FNetSerializationContext& Serialization);

	/**
	* Create or bind a replicated object from the received creation header.
	* @param Context Gives you access to useful info on the object to instantiate
	* @param Header The filled header information to use to spawn the object
	* @return The instantiated object if successful and relevant flags for the bridge to act on
	*/
	/**
	 * 接收端的"实例化或绑定"主入口（纯虚）。
	 *   - 动态对象：通常在此 SpawnActor / NewObject；
	 *   - 静态对象：通过 ResolveContext 在关卡中查找已存在实例。
	 * 返回值的 Flags 决定 Bridge 后续动作（是否回调 SubObjectCreated、是否注册到 ObjectRefCache 等）。
	 */
	virtual FInstantiateResult InstantiateReplicatedObjectFromHeader(const FInstantiateContext& Context, const UE::Net::FNetObjectCreationHeader* Header) PURE_VIRTUAL(UNetObjectFactory::InstantiateReplicatedObjectFromHeader, return FInstantiateResult(););

	/**
	 * Optional callback triggered at the end of the instantiation process and before any replicated properties were applied.
	 * Useful to apply any additional data included with the header or to signal the new object to other systems.
	 * @param Context Gives you access to the new object and the header it was created from.
	 */
	/** 实例化完成、状态尚未 apply 时的钩子；可在此从 Header 取额外数据写入实例。 */
	virtual void PostInstantiation(const FPostInstantiationContext& Context) {}

	/**
	 * Optional callback triggered after we applied the initial replicated properties to the instantiated object.
	 * From here the remote object is ready to be used by the game engine.
	 * @param Context Gives you access to the new object.
	 */
	/** 状态 apply 之后的钩子——此时对象进入"完全可用"。常用于触发 RepNotify 收尾。 */
	virtual void PostInit(const FPostInitContext& Context) {}

	/**
	 * Callback triggered when a replicated object is no longer relevant on the client.
	 * This is where a factory would destroy dynamic objects, reset stable objects or put objects back in a pool.
	 * Note that this callback is called on remotes (clients) only.
	 * @param Context Gives access to the instance that should be destroyed along with details on what type of destruction is requested.
	 */
	/**
	 * 对端不再需要该对象时调用（仅 Client）。
	 * Reason 决定路径：DoNotDestroy（仅解绑）、TearOff（撕断）、Destroy（销毁）；
	 * Flags 决定细节（是否真的从 Remote 触发销毁动作）。
	 */
	virtual void DetachedFromReplication(const FDestroyedContext& Context) PURE_VIRTUAL(UNetObjectFactory::DetachedFromReplication, );

	/**
	 * Replaced by DetachedFromReplication since it didn't receive callbacks for objects with EReplicationBridgeDestroyInstanceReason::DoNotDestroy reasons.
	*/
	UE_DEPRECATED(5.7, "Replaced by DetachedFromReplication")
	virtual void DestroyReplicatedObject(const FDestroyedContext& Context) final {}

	/** 
	 * Optional callback triggered when a root object managed by this factory gets assigned a dynamic subobject. 
	 * This callback is called on remotes only. 
	 * At the time of the callback the RootObject will have the latest replicated properties set, but the subobject will only be default constructed and won't be assigned the replicated properties received alongside the creation request. 
	 * @param RootObject The root object that owns the subobject
	 * @param SubObjectCreated The subobject that was just instantiated
	*/

	/** 当本 Factory 拥有的 RootObject 收到一个动态 SubObject 时回调（仅 Client）。RootObject 此时已最新，SubObject 尚未 apply 状态。 */
	virtual void SubObjectCreatedFromReplication(UE::Net::FNetRefHandle RootObject, UE::Net::FNetRefHandle SubObjectCreated) {}

	/**
	 * Optional callback triggered when a subobject will be detached from the replication system and potentially destroyed
	 * by it's factory in DestroyReplicatedObject. Both static and dynamic subobjects will be passed to this function.
	 * Note that this callback is called on remotes (clients) only.
	 * @param Context Gives access to the subobject about to be destroyed and the root object that owns him.
	 */
	/** SubObject 将被解绑/销毁时回调（仅 Client）。静态、动态 SubObject 均会回调。 */
	virtual void SubObjectDetachedFromReplication(const FDestroyedContext& Context) {}

	/**
	 * Fetch world information about a replicated object so it can be updated in the network engine.
	 * Only called for root objects.
	 * @param Context Gives access to the replicated object and which specific world information needs to be updated.
	 * @return The object's world info, or NullOpt if there is none.
	 */
	/**
	 * 提供对象的世界信息（仅 RootObject）：当前位置、Cull 距离。
	 * 由 FWorldLocations 在每帧 UpdateInstancesWorldLocation 阶段拉取。
	 */
	virtual TOptional<FWorldInfoData> GetWorldInfo(const FWorldInfoContext& Context) const PURE_VIRTUAL(UNetObjectFactory::GetWorldInfo, return NullOpt;);

	/** Return the poll frequency of a root object managed by this factory. */
	/** 返回 RootObject 期望的轮询频率（Hz）。决定该对象多久被 Poll 一次（影响脏字段检测频率）。 */
	virtual float GetPollFrequency(UE::Net::FNetRefHandle RootObjectHandle, UObject* RootObjectInstance) PURE_VIRTUAL(UNetObjectFactory::GetPollFrequency, return 100.f;);

public:

	/** Result of the instantiate request */
	/** Instantiate 的返回组合：实例 + 模板 + 后续 Bridge 标志。 */
	struct FInstantiateResult
	{
		/** The instantiated object represented by the header */
		UObject* Instance = nullptr;
		/** The template used to instantiate the object with. Only set this when the template is different from the object's archetype. */
		UObject* Template = nullptr;
		/** Flags to pass back to the bridge */
		EReplicationBridgeCreateNetRefHandleResultFlags Flags = EReplicationBridgeCreateNetRefHandleResultFlags::None;
	};

	/** Contextual information to use during instantiation */
	/** Instantiate 调用时的上下文：句柄 / Resolve 上下文 / 父 RootObject（若是 SubObject）。 */
	struct FInstantiateContext
	{
		/** The handle tied to the replicated object to instantiate */
		UE::Net::FNetRefHandle Handle;

		const UE::Net::FNetObjectResolveContext& ResolveContext;

		/** The handle of the object's root object (only if instantiating a subobject) */
		UE::Net::FNetRefHandle RootObjectOfSubObject;

		FInstantiateContext(UE::Net::FNetRefHandle InHandle, const UE::Net::FNetObjectResolveContext& InResolveContext, UE::Net::FNetRefHandle InRootObjectHandle) : Handle(InHandle), ResolveContext(InResolveContext), RootObjectOfSubObject(InRootObjectHandle) {}

		/** Tells if we instantiating a root object or a subobject. */
		bool IsRootObject() const { return !RootObjectOfSubObject.IsValid(); }
		bool IsSubObject() const { return !IsRootObject(); }
	};

	/** Contextual information to use in the PostInstantiation callback */
	struct FPostInstantiationContext
	{
		/** The object instantiated */
		UObject* Instance = nullptr;
		/** The header representing the replicated object */
		const UE::Net::FNetObjectCreationHeader* Header = nullptr;
		/** The connection that owns the replicated object */
		uint32 ConnectionId = 0;
	};

	/** Contextual information to use in the PostInit callback */
	struct FPostInitContext
	{
		/** The object instantiated */
		UObject* Instance = nullptr;
		/** The handle of the object */
		UE::Net::FNetRefHandle Handle;
	};

	/** Contextual information for the destroy callbacks */
	/** 销毁回调上下文：实例 / 父 / 销毁原因 / 销毁标志。 */
	struct FDestroyedContext
	{
		/** The object about to be destroyed */
		UObject* DestroyedInstance = nullptr;
		/** Optional pointer to the root object when the destroyed object is a subobject */
		UObject* RootObject = nullptr;
		EReplicationBridgeDestroyInstanceReason DestroyReason = EReplicationBridgeDestroyInstanceReason::DoNotDestroy;
		EReplicationBridgeDestroyInstanceFlags DestroyFlags = EReplicationBridgeDestroyInstanceFlags::None;
	};

	/** Details which info needs to be updated in GetWorldInfo */
	/** GetWorldInfo 中需要更新的信息子集（位掩码）。 */
	enum class EWorldInfoRequested : uint32
	{
		None = 0x0000,
		Location = 0x0001,
		CullDistance = 0x0002,
		All = Location | CullDistance,
	};

	/** Context when asking the factory for information on a specific object */
	struct FWorldInfoContext
	{
		/** The object instance we are requesting information about */
		UObject* Instance = nullptr;

		/** The handle of the object */
		UE::Net::FNetRefHandle Handle;

		/** Specify which info is requested to be updated. */
		EWorldInfoRequested InfoRequested = EWorldInfoRequested::All;
	};

	/** The world data the factory needs to fill about a given object. */
	struct FWorldInfoData
	{
		/** The current location of the object in the world. */
		FVector WorldLocation = FVector::ZeroVector;

		/** The network cull distance of the object. */
		float CullDistance = 0.0f;
	};

protected:

	/** Called when the netfactory is created */
	virtual void OnInit() {}

	/** Called before the netfactory will be destroyed */
	virtual void OnDeinit() {}

	/** Called after we finished processing all incoming packets */
	virtual void OnPostReceiveUpdate() {}

	/**
	 * Create the correct header type for a given replicated object and fill the header with the information representing it.
	 * @param Handle The net handle of the replicated object.
	 * @return A valid creation header if successful. 
	 */
	/** 派生 Factory 的核心：分配并填充派生 Header（写包侧）。 */
	virtual TUniquePtr<UE::Net::FNetObjectCreationHeader> CreateAndFillHeader(UE::Net::FNetRefHandle Handle) PURE_VIRTUAL(UNetObjectFactory::CreateAndFillHeader, return nullptr;);

	/**
	* Serialize the header into the bitstream
	* @param Context Gives access to the bitstream writer and other useful information.
	* @param Header A valid and filled header to serialize
	* @return Return true if the header serialization was successful.
	*/
	/** 把派生 Header 字段写入位流。 */
	virtual bool SerializeHeader(const UE::Net::FCreationHeaderContext& Context, const UE::Net::FNetObjectCreationHeader* Header) PURE_VIRTUAL(UNetObjectFactory::SerializeHeader, return false;);

	/**
	* Create a new header and deserialize it's data from the incoming bitstream
	* @param Serialization Gives you access to the bit reader
	* @return Return a valid and filled header if successful.
	*/
	/** 反向：分配派生 Header 并从位流读字段。 */
	virtual TUniquePtr<UE::Net::FNetObjectCreationHeader> CreateAndDeserializeHeader(const UE::Net::FCreationHeaderContext& Context) PURE_VIRTUAL(UNetObjectFactory::CreateAndDeserializeHeader, return nullptr;);

protected:

	UObjectReplicationBridge* Bridge = nullptr;                 // 由 Init 注入的所属 Bridge
	UE::Net::FNetObjectFactoryId FactoryId = UE::Net::InvalidNetObjectFactoryId; // 由 Registry 分配并由 Init 注入

};

ENUM_CLASS_FLAGS(UNetObjectFactory::EWorldInfoRequested);