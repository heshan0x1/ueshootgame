// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================================================
// NetObjectFilter.cpp —— UNetObjectFilter 抽象基类的非虚基础实现。
// 主要内容：
//   - LexToString(ENetFilterStatus)：日志友好枚举转字符串。
//   - Init / Deinit：分配 FilteredObjects 位图、抓取全局 FilteringInfos 与 NetRefHandleManager 的引用。
//   - MaxInternalNetRefIndexIncreased：对象索引上限扩容时同步扩位图与视图。
//   - 默认空虚函数实现：AddConnection / RemoveConnection / UpdateObjects / PreFilter / Filter / PostFilter。
//   - GetFilteringInfo / GetObjectIndex：派生类常用的安全访问/查询辅助。
// 注意：FNetObjectFilteringInfoAccessor 是 friend 通道，避免 UNetObjectFilter 公开持有/分配 NetObjectFilteringInfos
// 的责任——它的实际所有权在 FReplicationFiltering（见同模块 ReplicationFiltering.h）。
// =====================================================================================================================

#include "Iris/ReplicationSystem/Filtering/NetObjectFilter.h"
#include "Iris/ReplicationSystem/Filtering/ReplicationFiltering.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NetObjectFilter)

namespace UE::Net
{
	// 将 ENetFilterStatus 枚举值转为可读字符串，主要用于 IrisFiltering 日志/调试输出。
	const TCHAR* LexToString(ENetFilterStatus Status)
	{
		switch(Status)
		{
			case ENetFilterStatus::Disallow:
			{
				return TEXT("Disallow");
			} break;
		
			case ENetFilterStatus::Allow:
			{
				return TEXT("Allow");
			} break;

			default:
			{
				ensure(false);
				return TEXT("Missing");
			} break;
		}
	}
}

// 基类构造：纯空（traits 等成员通过默认初始化器置为 None）。具体派生类会在 NewObject 后由调度器调用 Init()。
UNetObjectFilter::UNetObjectFilter()
{
}

// 由 FReplicationFiltering 在初始化"已启用 Filter"时调用：
//   1) 按当前对象索引上限分配 FilteredObjects 位图；
//   2) 通过 friend Accessor 取得 ReplicationSystem 中全局共享的 NetObjectFilteringInfos 视图；
//   3) 缓存 NetRefHandleManager 引用；
//   4) 转交派生类的 OnInit 钩子做个性化初始化（读 Config、建容器等）。
void UNetObjectFilter::Init(const FNetObjectFilterInitParams& Params)
{
	FilteredObjects.Init(Params.CurrentMaxInternalIndex);

	{
		UE::Net::Private::FNetObjectFilteringInfoAccessor FilteringInfoAccessor;
		FilteringInfos = FilteringInfoAccessor.GetNetObjectFilteringInfos(Params.ReplicationSystem);
	}
	NetRefHandleManager = &Params.ReplicationSystem->GetReplicationSystemInternal()->GetNetRefHandleManager();
	OnInit(Params);
}

// 关停：先回调派生类的 OnDeinit 释放外部引用；再清空缓存的视图与指针，避免悬挂引用。
void UNetObjectFilter::Deinit()
{
	OnDeinit();

	FilteringInfos = TArrayView<FNetObjectFilteringInfo>();
	NetRefHandleManager = nullptr;
}

// 当对象索引上限因运行时增长（NetRefHandleManager 扩容）而上调时：
//   1) 同步扩展 FilteredObjects 位图；
//   2) 接收新的全局 FilteringInfos 视图（旧视图可能已经失效）；
//   3) 转交派生类钩子 OnMaxInternalNetRefIndexIncreased。
void UNetObjectFilter::MaxInternalNetRefIndexIncreased(UE::Net::Private::FInternalNetRefIndex MaxInternalIndex, TArrayView<FNetObjectFilteringInfo> NewFilterInfoView)
{
	FilteredObjects.SetNumBits(MaxInternalIndex);
	
	//$IRIS TODO: Move the FilteringInfo somewhere else or pass it via function param only ?
	//      We shouldn't be holding views on arrays we don't own exactly for this reason...
	// $IRIS TODO：FilteringInfo 应改为按调用参数传入，避免基类长期持有外部数组的视图。
	FilteringInfos = NewFilterInfoView;

	OnMaxInternalNetRefIndexIncreased(MaxInternalIndex);
}

// 默认空实现：仅持有少量全局态的简单 Filter（如 AlwaysRelevant / FilterOut）无需重写。
void UNetObjectFilter::AddConnection(uint32 ConnectionId)
{
}

// 默认空实现：见 AddConnection 注释。
void UNetObjectFilter::RemoveConnection(uint32 ConnectionId)
{
}

// 默认空实现：仅当 Filter 声明 ENetFilterTraits::NeedsUpdate 时调度器才会调用此函数；不声明则永不进入。
void UNetObjectFilter::UpdateObjects(FNetObjectFilterUpdateParams&)
{
}

// 默认空实现：纯虚约束放宽——不所有 Filter 都需要预处理。
void UNetObjectFilter::PreFilter(FNetObjectPreFilteringParams&)
{
}

// 基类 Filter 占位（不应在生产路径触发）。具体 Filter 必须重写。
void UNetObjectFilter::Filter(FNetObjectFilteringParams&)
{
}

// 默认空实现：与 PreFilter 配对的收尾钩子。
void UNetObjectFilter::PostFilter(FNetObjectPostFilteringParams&)
{
}

// 安全读取：仅返回"由本 Filter 接管"对象的 FilteringInfo，否则返回 nullptr。
// 派生类应在 RemoveObject / Filter / UpdateObjects 等回调中通过此接口访问私有数据。
FNetObjectFilteringInfo* UNetObjectFilter::GetFilteringInfo(uint32 ObjectIndex)
{
	// Only allow retrieving infos for objects handled by this instance.
	if (!IsObjectFiltered(ObjectIndex))
	{
		return nullptr;
	}

	return &FilteringInfos[ObjectIndex];
}

// NetRefHandle → InternalNetRefIndex 的轻量封装；调用方需保证 NetRefHandleManager 已就绪。
uint32 UNetObjectFilter::GetObjectIndex(UE::Net::FNetRefHandle NetRefHandle) const
{
	return NetRefHandleManager->GetInternalIndex(NetRefHandle);
}
