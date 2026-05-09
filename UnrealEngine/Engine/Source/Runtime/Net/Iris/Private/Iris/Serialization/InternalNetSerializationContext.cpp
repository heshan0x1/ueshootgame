// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// InternalNetSerializationContext.cpp
// ---------------------------------------------------------------------------------------------
// FInternalNetSerializationContext 的实现：内部上下文的便利构造、Init、内存策略三个 helper。
// 详细职责说明见同名头文件。
// =============================================================================================

#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "HAL/UnrealMemory.h"

namespace UE::Net::Private
{

// ------------------------------------------------------------------------------------------
// 便利构造：直接从 ReplicationSystem 抓取所有内部依赖
// ------------------------------------------------------------------------------------------
//   - ObjectReferenceCache 通过 InternalRep API 拿到（和 PackageMap 一样在 Internal 层）；
//   - bSerializeObjectReferencesAsRemoteIds 取决于该 ReplicationSystem 的能力位
//     （UReplicationSystem::IsUsingRemoteObjectReferences()），运行时一次性确定不会再变。
//   - bDowngradeAutonomousProxyRole / bInlineObjectReferenceExports 默认关闭，
//     由具体调用点（角色序列化器 / FForceInlineExportScope）按需打开。
// ------------------------------------------------------------------------------------------
FInternalNetSerializationContext::FInternalNetSerializationContext(UReplicationSystem* InReplicationSystem)
: ReplicationSystem(InReplicationSystem)
, ObjectReferenceCache(&InReplicationSystem->GetReplicationSystemInternal()->GetObjectReferenceCache())
, PackageMap(InReplicationSystem->GetReplicationSystemInternal()->GetIrisObjectReferencePackageMap())
, bDowngradeAutonomousProxyRole(0)
, bInlineObjectReferenceExports(0)
, bSerializeObjectReferencesAsRemoteIds(InReplicationSystem->IsUsingRemoteObjectReferences() ? 1 : 0)
{
}

// ------------------------------------------------------------------------------------------
// 动态状态分配：当前实现就是 FMemory 直转。
// 之所以不直接用 FMemory，而要走这一层包装，是为了将来可以替换为：
//   - PerConnection 内存池（避免与游戏线程争锁）；
//   - PerBatch 线性分配器（一帧之内只前进、批量回收）。
// 对外接口签名提前固化下来，可以无侵入式替换。
// ------------------------------------------------------------------------------------------
void* FInternalNetSerializationContext::Alloc(SIZE_T Size, SIZE_T Alignment)
{
	return FMemory::Malloc(Size, static_cast<uint32>(Alignment));
}

void FInternalNetSerializationContext::Free(void* Ptr)
{
	return FMemory::Free(Ptr);
}

void* FInternalNetSerializationContext::Realloc(void* PrevAddress, SIZE_T NewSize, uint32 Alignment)
{
	return FMemory::Realloc(PrevAddress, NewSize, Alignment);
}

// ------------------------------------------------------------------------------------------
// Init：用 FInitParameters 重置内部字段
// ------------------------------------------------------------------------------------------
// 与便利构造的区别：
//   - 接受调用方显式传入的 PackageMap（可能是临时/特例 PackageMap，非系统默认那个）；
//   - 接受调用方组装好的 ObjectResolveContext（特别是远端 NetTokenStoreState）；
//   - 不会更新 bSerializeObjectReferencesAsRemoteIds 等标志位——这些标志在对象生命周期内
//     不应被 Init 改写。
// ------------------------------------------------------------------------------------------
void FInternalNetSerializationContext::Init(const FInitParameters& InitParams)
{
	ReplicationSystem = InitParams.ReplicationSystem;
	ObjectReferenceCache = &InitParams.ReplicationSystem->GetReplicationSystemInternal()->GetObjectReferenceCache();
	PackageMap = InitParams.PackageMap;
	ResolveContext = InitParams.ObjectResolveContext;
}

}
