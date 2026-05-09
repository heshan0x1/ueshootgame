// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：NetObjectFactoryRegistry.cpp
// 角色：FNetObjectFactoryRegistry 的实现：注册/反注册 + Id 分配 + 查询。
//
// 状态机要点：
//   * bIsFactoryRegistrationAllowed 在 IrisCore 启动时为 true；
//     引擎创建第一个 ReplicationSystem 后会调用 SetFactoryRegistrationAllowed(false)
//     锁住注册（避免对端 Id 不一致）。
//
// Id 分配策略：
//   * 优先复用之前 Unregister 留下的"空 slot"（Name == NAME_None），保持 Id 稳定；
//   * 没空 slot 时再从尾部追加。
//   * UnregisterFactory 不收缩数组，仅清空 slot，确保 Id 永远等于 slot 下标。
// =====================================================================================

#include "Iris/ReplicationSystem/NetObjectFactoryRegistry.h"

#include "Iris/ReplicationSystem/NetObjectFactory.h"
#include "Iris/Core/IrisLog.h"

namespace UE::Net::Private
{
	// 注册期标志：默认 true，RS 创建后由引擎置 false。
	bool bIsFactoryRegistrationAllowed = true;
} // end namespace UE::Net::Private

namespace UE::Net 
{

// 静态数组定义：固定容量 MaxFactories 的稀疏数组，下标即 FactoryId。
TArray<FNetObjectFactoryRegistry::FFactoryData, TFixedAllocator<FNetObjectFactoryRegistry::MaxFactories>>  FNetObjectFactoryRegistry::NetFactories;

void FNetObjectFactoryRegistry::SetFactoryRegistrationAllowed(bool bAllowed)
{
	UE::Net::Private::bIsFactoryRegistrationAllowed = bAllowed;
}

// 注册一条 Factory：4 步守卫 + 分配 Id。
void FNetObjectFactoryRegistry::RegisterFactory(UClass* FactoryClass, FName FactoryName)
{
	using namespace UE::Net::Private;

	// Factories cannot be modified while Iris NetDrivers exist
	// 守卫 1：注册期已关闭。
	if (!bIsFactoryRegistrationAllowed)
	{
		UE_LOG(LogIris, Error, TEXT("FNetObjectFactoryRegistry::RegisterFactory cannot register factory: %s name: %s because it was called while Iris replication was already started"), *GetNameSafe(FactoryClass), *FactoryName.ToString());
		check(bIsFactoryRegistrationAllowed);
		return;
	}

	check(FactoryClass);

	// 守卫 2：FactoryName 非 None。
	if (FactoryName.IsNone())
	{
		checkf(!FactoryName.IsNone(), TEXT("FNetObjectFactoryRegistry::RegisterFactory cannot register %s due to invalid name"), *GetNameSafe(FactoryClass));
		return;
	}

	// 守卫 3：未达到 MaxFactories。
	if (NetFactories.Num() >= MaxFactories)
	{
		UE_LOG(LogIris, Error, TEXT("FNetObjectFactoryRegistry::RegisterFactory already has %u factories registered. Cannot register factory: %s with name: %s"), MaxFactories, *GetNameSafe(FactoryClass), *FactoryName.ToString());
		ensure(false);
		return;
	}

	// Make sure the name is unique
	// 守卫 4：FactoryName 必须未被占用。
	for (const FFactoryData& FactoryData : NetFactories)
	{
		if (FactoryData.Name == FactoryName)
		{
			UE_LOG(LogIris, Error, TEXT("FNetObjectFactoryRegistry::RegisterFactory cannot register factory: %s with name: %s. This name is already used by factory: %s id: %u"), *GetNameSafe(FactoryClass), *FactoryName.ToString(), *GetNameSafe(FactoryData.NetFactoryClass.Get()), FactoryData.Id);
			ensure(false);
			return;
		}
	}

	// Make sure the class is of the correct type
	// 守卫 5：FactoryClass 必须派生自 UNetObjectFactory。
	if (!FactoryClass->IsChildOf<UNetObjectFactory>())
	{
		checkf(FactoryClass->IsChildOf<UNetObjectFactory>(), TEXT("FNetObjectFactoryRegistry::RegisterFactory factory: %s name: %s is not derived from UNetObjectFactory."), *GetNameSafe(FactoryClass), *FactoryName.ToString());
		return;
	}

	// Find an invalid factory entry
	// 优先复用 Unregister 留下的空 slot（Name == NAME_None）。
	int32 Index = NetFactories.IndexOfByPredicate([](const FFactoryData& rhs)
	{
		return rhs.Name == NAME_None;
	});

	if (Index != INDEX_NONE)
	{
		// 重用空 slot：Id 等于 slot 下标，保证旧 Id 仍然唯一。
		NetFactories[Index] = FFactoryData
		{
			.Name = FactoryName,
			.Id = IntCastChecked<FNetObjectFactoryId>(Index),
			.NetFactoryClass = FactoryClass,
		};
	}
	else
	{
		// 否则尾部新增；Id = 新下标。
		Index = NetFactories.Emplace(FFactoryData
		{
			.Name = FactoryName,
			.NetFactoryClass = FactoryClass,
		});
		NetFactories[Index].Id = IntCastChecked<FNetObjectFactoryId>(Index);
	}

	UE_LOG(LogIris, Verbose, TEXT("FNetObjectFactoryRegistry::RegisterFactory has registered factory: %s name: %s id: %u"), *GetNameSafe(NetFactories[Index].NetFactoryClass.Get()), *NetFactories[Index].Name.ToString(), NetFactories[Index].Id);
}

// 反注册：清空 slot 但保留位置（Id 永不再分配给同一 Name 之前是稳定的）。
void FNetObjectFactoryRegistry::UnregisterFactory(FName FactoryName)
{
	using namespace UE::Net::Private;

	// Factories cannot be modified while Iris NetDrivers exist
	if (!bIsFactoryRegistrationAllowed)
	{
		UE_LOG(LogIris, Error, TEXT("FNetObjectFactoryRegistry::UnregisterFactory cannot unregister factory name: %s because it was called while Iris replication was already started"), *FactoryName.ToString());
		check(bIsFactoryRegistrationAllowed);
		return;
	}

	for (int32 Index=0; Index < NetFactories.Num(); ++Index)
	{
		const FFactoryData& Data = NetFactories[Index];
		if (Data.Name == FactoryName)
		{
			UE_LOG(LogIris, Verbose, TEXT("FNetObjectFactoryRegistry::UnregisterFactory is unregistering factory: %s name: %s id: %u"), *GetNameSafe(Data.NetFactoryClass.Get()), *Data.Name.ToString(), Data.Id);
			
			// Reset the entry to keep the Id's mapped to their index
			// 注意：保持 Id↔Index 一致，所以仅清空字段而不删除 slot。
			NetFactories[Index] = FFactoryData{};
			return;
		}
	}

	UE_LOG(LogIris, Error, TEXT("FNetObjectFactoryRegistry::UnregisterFactory could not find any factories using name: %s"), *FactoryName.ToString());
	ensure(false);
}
	  
// 通过 Name 找 Id（线性扫描；Factory 数量很小，O(N) 可接受）。
FNetObjectFactoryId FNetObjectFactoryRegistry::GetFactoryIdFromName(FName FactoryName)
{
	for (const FFactoryData& Data : NetFactories)
	{
		if (Data.Name == FactoryName)
		{
			return Data.Id;
		}
	}

	return InvalidNetObjectFactoryId;
}

// Id 是否合法 = 下标合法且 slot 非空（Name 非 None）。
bool FNetObjectFactoryRegistry::IsValidFactoryId(FNetObjectFactoryId Id)
{
	if (NetFactories.IsValidIndex(Id))
	{
		return !NetFactories[Id].Name.IsNone();
	}

	return false;
}

} // end namespace UE::Net