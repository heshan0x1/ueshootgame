// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// =============================================================================================
// InternalNetSerializationContext.h
// ---------------------------------------------------------------------------------------------
// FInternalNetSerializationContext：FNetSerializationContext 的"私有内部数据"。
//
// 设计动机：
//   - 公共上下文 FNetSerializationContext 暴露在 IRISCORE_API 头文件里（含 NetSerializer
//     接口），但有些字段（ReplicationSystem*、ObjectReferenceCache*、UIrisObjectReferencePackageMap*、
//     动态状态分配/回收函数等）只服务 Iris 内部，不希望客户代码看到，因此把它们隔离到
//     这个 Private 头文件中，由 FNetSerializationContext::InternalContext 间接访问。
//   - 同时承担"动态状态内存策略"职责：Alloc/Realloc/Free 三个虚化点未来可被替换成
//     PerConnection/PerBatch 的内存池。当前实现只是直转 FMemory。
//
// 三个序列化标志位：
//   - bDowngradeAutonomousProxyRole：仅供 NetRole 序列化使用——服务器在向"非主控"连接发送时
//     把 ROLE_AutonomousProxy 降级为 ROLE_SimulatedProxy，避免泄漏。
//   - bInlineObjectReferenceExports：在写入对象引用时强制"行内导出"——把引用所需的
//     full-state 一并写入当前数据流，而不是延迟到下一次 ExportPacket。
//   - bSerializeObjectReferencesAsRemoteIds：当 UE_WITH_REMOTE_OBJECT_HANDLE=1 且
//     ReplicationSystem 启用了远程对象时，把对象引用量化/序列化为 FRemoteObjectId 而非
//     FNetObjectReference（远程对象多 server 网格场景）。
//
// FForceInlineExportScope：典型 RAII 包装，进入作用域时把 bInlineObjectReferenceExports
// 拨为 1，离开时恢复旧值；用在某些必须立即发引用的小段代码中（详见 Object/Array 系列序列化器）。
//
// 与文档对照：Docs/Modules/Serialization.md §1.3 末段「内部上下文/标志位」。
// =============================================================================================

#include "HAL/Platform.h"
#include "Iris/IrisConstants.h"
#include "Iris/ReplicationSystem/ObjectReferenceCacheFwd.h"

class UReplicationSystem;
class UObject;
class UIrisObjectReferencePackageMap;

namespace UE::Net::Private
{
	class FObjectReferenceCache;
}

namespace UE::Net::Private
{

/**
 * Iris 序列化层的"内部上下文"。生命周期通常与一次 Update/Receive 调用绑定，挂在外层
 * FNetSerializationContext 的 InternalContext 字段上，对外只通过 GetInternalContext()
 * 获取，因而不出现在公共头文件里。
 */
class FInternalNetSerializationContext final
{
public:
	/**
	 * Init 入参。把"由谁来初始化我"显式列出，避免 Init 函数签名继续膨胀。
	 */
	struct FInitParameters
	{
		/** 关联的复制系统。一台 World 上可能有多个（例如 IRIS + 旧系统并存测试期）。 */
		UReplicationSystem* ReplicationSystem = nullptr;
		/** 对端解析上下文：包含远端 NetTokenStoreState、连接 ID 等"读取时"必须的信息。 */
		FNetObjectResolveContext ObjectResolveContext;
		/** 用于劫持 FArchive 的 PackageMap，详见 IrisObjectReferencePackageMap.h。 */
		UIrisObjectReferencePackageMap* PackageMap = nullptr;			
	};

	/** 默认构造：所有字段 nullptr/0；之后必须调用 Init 才能用。 */
	FInternalNetSerializationContext();
	/** 便利构造：直接从 ReplicationSystem 抓取 ObjectReferenceCache、PackageMap，
	 *  并依据系统能力位决定是否启用 RemoteObjectId 序列化。 */
	explicit FInternalNetSerializationContext(UReplicationSystem* InReplicationSystem);

	/** 用 Init 参数集初始化所有内部字段；可重复调用以"复用上下文对象"。 */
	void Init(const FInitParameters& Params);

	// ----------------------------------------------------------------------------------
	// 动态状态内存策略：当 NetSerializer 需要在 Quantized 状态外额外分配 buffer
	// （如远程引用、字符串、变长 Array 等）时统一走这三个入口；未来可替换为内存池。
	// ----------------------------------------------------------------------------------
	/** 分配 Size 字节、Alignment 对齐的内存块。 */
	void* Alloc(SIZE_T Size, SIZE_T Alignment);
	/** 释放 Alloc/Realloc 返回的指针；nullptr 安全。 */
	void Free(void* Ptr);
	/** 重新分配（语义同 FMemory::Realloc）：旧块可能被搬移，调用方需更新指针。 */
	void* Realloc(void* PrevAddress, SIZE_T NewSize, uint32 Alignment);

	// ----------------------------------------------------------------------------------
	// 内部子系统指针。一般在 Init 时一次性写入，序列化期间只读。
	// ----------------------------------------------------------------------------------
	/** 关联的复制系统主入口。 */
	UReplicationSystem* ReplicationSystem;
	/** 对象引用缓存：FNetObjectReference <-> NetRefHandle <-> UObject* 三态映射。 */
	FObjectReferenceCache* ObjectReferenceCache;
	/** 对端解析上下文（远端 Token 状态、连接 ID 等）。 */
	FNetObjectResolveContext ResolveContext;

	/** 我们对 UPackageMap 做了一个特殊实现，用来在调用 LastResortNetSerializer 等
	 *  老式 NetSerialize 路径时"劫持"FArchive 的对象/Name 序列化，把它们转化为索引
	 *  并真正在 Iris 自己的引用导出流里去发。 */
	// We have a special implementation of UPackageMap to capture references when using LastResortNetSerializer
	UIrisObjectReferencePackageMap* PackageMap = nullptr;

	/** 仅供角色（Role）序列化使用：服务器向非主控连接发送时把 AutonomousProxy 降级为
	 *  SimulatedProxy，避免暴露主控权。
	 *  $IRIS TODO 角色其实不该作为属性来复制，亟需一个真正的权威系统来托管。 */
	// $IRIS TODO Roles really shouldn't be replicated as properties. In dire need of a proper authority system.
	// This is ONLY to be used by role serialization.
	uint32 bDowngradeAutonomousProxyRole : 1;

	/** 强制内联导出对象引用：把引用所需的 full state 立刻塞进当前数据流，
	 *  而不是延迟到下一次 ExportPacket。一般通过 FForceInlineExportScope 临时打开。 */
	// Allow References to be inlined in serialized state
	uint32 bInlineObjectReferenceExports : 1;

	/** 若为 true，对象引用会被量化/序列化为 FRemoteObjectId，而不是 FNetObjectReference。
	 *  需要 UE_WITH_REMOTE_OBJECT_HANDLE=1。多服务器 Mesh 场景使用。 */
	// If true, object references will be quantized & serialized as FRemoteObjectIds instead of FNetObjectReferences.
	// Requires UE_WITH_REMOTE_OBJECT_HANDLE.
	uint32 bSerializeObjectReferencesAsRemoteIds : 1;
};

// ------------------------------------------------------------------------------------------
// 内联默认构造：所有指针置空、所有标志位清 0。
// ------------------------------------------------------------------------------------------
inline FInternalNetSerializationContext::FInternalNetSerializationContext()
: ReplicationSystem(nullptr)
, ObjectReferenceCache(nullptr)
, PackageMap(nullptr)
, bDowngradeAutonomousProxyRole(0)
, bInlineObjectReferenceExports(0)
, bSerializeObjectReferencesAsRemoteIds(0)
{}

// ------------------------------------------------------------------------------------------
// FForceInlineExportScope —— RAII：临时打开"内联导出"标志位
// ------------------------------------------------------------------------------------------
// 用法：
//   {
//       FForceInlineExportScope Scope(InternalContext);
//       // 这里写入对象引用时，引用会被立即内联导出，而非延迟。
//   }
//   // 离开作用域，自动恢复旧标志位。
//
// 当传入 nullptr 时退化为空操作（方便上层在 InternalContext 未就绪时安全使用）。
// ------------------------------------------------------------------------------------------
// Scope used when we actually want to export references.
class FForceInlineExportScope
{
public:
	/** 进入作用域：保存旧值并把 bInlineObjectReferenceExports 拨到 1。 */
	explicit FForceInlineExportScope(FInternalNetSerializationContext* InContext);
	/** 离开作用域：把标志位恢复成进入前的值。 */
	~FForceInlineExportScope();

private:
	/** 关联的内部上下文；为 nullptr 时构造/析构都成空操作。 */
	FInternalNetSerializationContext* InternalContext;
	/** 进入前的旧值，用于在析构时还原（嵌套作用域下尤其重要）。 */
	uint32 bOldInlineObjectReferences = 0U;
};

inline FForceInlineExportScope::FForceInlineExportScope(FInternalNetSerializationContext* InContext)
: InternalContext(InContext)
{
	if (InternalContext)
	{
		// 保存旧值，确保嵌套使用时仍能正确还原。
		bOldInlineObjectReferences = InternalContext->bInlineObjectReferenceExports;
		InternalContext->bInlineObjectReferenceExports = 1U;
	}
}

inline FForceInlineExportScope::~FForceInlineExportScope()
{
	if (InternalContext)
	{
		// Restore state
		// 还原进入作用域前的值；嵌套作用域下保证最外层才回到初始状态。
		InternalContext->bInlineObjectReferenceExports = bOldInlineObjectReferences;
	}
}

}
