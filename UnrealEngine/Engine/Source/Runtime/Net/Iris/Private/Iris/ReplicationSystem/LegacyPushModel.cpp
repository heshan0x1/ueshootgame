// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：LegacyPushModel.cpp
// 模块：Iris / ReplicationSystem
// 功能：FNetHandleLegacyPushModelHelper 实现——把旧版 PushModel 宏的回调转接
//       到 NetCore 的 GlobalDirtyNetObjectTracker（再由 RS 的 DirtyTracker 拉取）。
//
// 调用链回顾：
//   MARK_PROPERTY_DIRTY_FROM_NAME(...) (在游戏代码)
//       │
//       ▼
//   Push Model 宏内 → 通过 IrisMarkPropertyDirty 委托
//       │
//       ▼
//   FNetHandleLegacyPushModelHelper::Mark[Property|Properties]OwnerDirty
//       │（把 PushId 反推为 FNetHandle，调 NetCore::MarkNetObjectStateDirty）
//       ▼
//   FGlobalDirtyNetObjectTracker::MarkNetObjectDirty(Handle, RepStart, RepEnd)
//       │
//       ▼
//   每个 RS 的 FDirtyNetObjectTracker::ApplyGlobalDirtyObjectList()
// =====================================================================================

#include "LegacyPushModel.h"
#include "Iris/IrisConfigInternal.h"
#include "Iris/ReplicationState/ReplicationStateUtil.h"
#include "Iris/ReplicationState/PropertyReplicationState.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/ReplicationSystem/NetRefHandleManager.h"
#include "Net/Core/DirtyNetObjectTracker/GlobalDirtyNetObjectTracker.h"
#include "Containers/ArrayView.h"

#if WITH_PUSH_MODEL
#include "HAL/IConsoleManager.h"
#include "UObject/Object.h"

namespace UE::Net::Private
{

// 中文：cvar 定义的两个全局态——
//   * bIsIrisPushModelForceEnabled：模式 1 = 强制启用，运行时不可切。
//   * IrisPushModelMode：0/1/2 三态切换（详见头文件注释）。
bool bIsIrisPushModelForceEnabled = false;
int IrisPushModelMode = 2;
static FAutoConsoleVariableRef CVarIrisPushModelMode(
		TEXT("net.Iris.PushModelMode"),
		IrisPushModelMode,
		TEXT("0 = disabled but runtime togglable, 1 = enabled and not togglable, 2 = enabled but runtime togglable. Requires Net.IsPushModelEnabled is true and WITH_PUSH_MODEL > 0 to use push based dirtiness in the backwards compatibility mode."
		));

// ------------------------------------------------------------------------------------
// InitPushModel / ShutdownPushModel
// ------------------------------------------------------------------------------------
// 中文：FIrisCoreModule::StartupModule 调用 Init；ShutdownModule 调用 Shutdown。
// Init 时根据 cvar 决定绑哪一组 Mark 委托：
//   * 模式 1（且全局 PushModel 开）→ 直通 MarkPropertyOwnerDirty / MarkPropertiesOwnerDirty。
//   * 其它情况  → OptionallyMark*（每次回调里再判一次开关）。
// ------------------------------------------------------------------------------------
void FNetHandleLegacyPushModelHelper::InitPushModel()
{
	bIsIrisPushModelForceEnabled = IrisPushModelMode == 1;
	if (bIsIrisPushModelForceEnabled && ensureMsgf(IS_PUSH_MODEL_ENABLED(), TEXT("Trying to force enable Iris push model support when push model is disabled, falling back to optional path. Set Net.IsPushModelEnabled true")))
	{
		// Push model is assumed to be enabled at all times. The cvar will not be checked.
		// 中文：模式 1 直通——Hot path，无分支判断。
		UE_NET_SET_IRIS_MARK_PROPERTY_DIRTY_DELEGATE(UEPushModelPrivate::FIrisMarkPropertyDirty::CreateStatic(&FNetHandleLegacyPushModelHelper::MarkPropertyOwnerDirty));
		UE_NET_SET_IRIS_MARK_PROPERTIES_DIRTY_DELEGATE(UEPushModelPrivate::FIrisMarkPropertiesDirty::CreateStatic(&FNetHandleLegacyPushModelHelper::MarkPropertiesOwnerDirty));
	}
	else
	{
		// Initialize our delegate so that push model can be toggled at runtime.
		// 中文：模式 0/2 走"可切"路径——每次 Mark 都查 IsIrisPushModelEnabled，
		// 关闭时跳过（让 Iris 退化为纯 Poll 模式，便于调试 / 性能对比）。
		UE_NET_SET_IRIS_MARK_PROPERTY_DIRTY_DELEGATE(UEPushModelPrivate::FIrisMarkPropertyDirty::CreateStatic(&FNetHandleLegacyPushModelHelper::OptionallyMarkPropertyOwnerDirty));
		UE_NET_SET_IRIS_MARK_PROPERTIES_DIRTY_DELEGATE(UEPushModelPrivate::FIrisMarkPropertiesDirty::CreateStatic(&FNetHandleLegacyPushModelHelper::OptionallyMarkPropertiesOwnerDirty));
	}
}

void FNetHandleLegacyPushModelHelper::ShutdownPushModel()
{
	// 中文：清空委托，避免悬挂指针在模块卸载后被调到。
	UE_NET_SET_IRIS_MARK_PROPERTY_DIRTY_DELEGATE({});
	UE_NET_SET_IRIS_MARK_PROPERTIES_DIRTY_DELEGATE({});
}

// ------------------------------------------------------------------------------------
// SetNetPushID / ClearNetPushID
// ------------------------------------------------------------------------------------
// 中文：写入/清除 UObject 的 NetPushIdDynamic 字段——这个字段由旧 PushModel
// 引擎宏在 MARK_PROPERTY_DIRTY 时读取并回传给委托。Iris 复用同一个字段存
// FNetHandle 的 64-bit 值，零开销互通。
// ------------------------------------------------------------------------------------
void FNetHandleLegacyPushModelHelper::SetNetPushID(UObject* Object, FNetPushObjectHandle Handle)
{
	FObjectNetPushIdHelper::SetNetPushIdDynamic(Object, UEPushModelPrivate::FNetPushObjectId(Handle.GetPushObjectId()).GetValue());
}

void FNetHandleLegacyPushModelHelper::ClearNetPushID(UObject* Object)
{
	FObjectNetPushIdHelper::SetNetPushIdDynamic(Object, UEPushModelPrivate::FNetPushObjectId().GetValue());
}

// ------------------------------------------------------------------------------------
// 模式 1（直通）回调：把 PushId 反推为 FNetHandle，再调 NetCore 全局标脏函数
// （含 RepIndex 范围；Global 端再分发给所有 RS 的 DirtyTracker）。
// ------------------------------------------------------------------------------------
void FNetHandleLegacyPushModelHelper::MarkPropertyOwnerDirty(const UObject* Object, UEPushModelPrivate::FNetIrisPushObjectId PushId, const int32 RepIndex)
{
	const FNetPushObjectHandle Handle(PushId);
	// 中文：单属性形态，[RepIndex, RepIndex] 闭区间。
	MarkNetObjectStateDirty(Handle.GetNetHandle(), RepIndex, RepIndex);
}

void FNetHandleLegacyPushModelHelper::MarkPropertiesOwnerDirty(const UObject* Object, UEPushModelPrivate::FNetIrisPushObjectId PushId, const int32 StartRepIndex, const int32 EndRepIndex)
{
	const FNetPushObjectHandle Handle(PushId);
	// 中文：连续 RepIndex 区间形态——例如数组成员、嵌套 struct 整体重置。
	MarkNetObjectStateDirty(Handle.GetNetHandle(), StartRepIndex, EndRepIndex);
}

// ------------------------------------------------------------------------------------
// 模式 0/2（可切）回调：每次回调时再判 IsIrisPushModelEnabled。
// 关闭时直接 return —— Iris 不会收到任何脏标记，对象走全量 Poll。
// ------------------------------------------------------------------------------------
void FNetHandleLegacyPushModelHelper::OptionallyMarkPropertyOwnerDirty(const UObject* Object, UEPushModelPrivate::FNetIrisPushObjectId PushId, const int32 RepIndex)
{
	if (IsIrisPushModelEnabled(true))
	{
		const FNetPushObjectHandle Handle(PushId);
		MarkNetObjectStateDirty(Handle.GetNetHandle(), RepIndex, RepIndex);
	}
}

void FNetHandleLegacyPushModelHelper::OptionallyMarkPropertiesOwnerDirty(const UObject* Object, UEPushModelPrivate::FNetIrisPushObjectId PushId, const int32 StartRepIndex, const int32 EndRepIndex)
{
	if (IsIrisPushModelEnabled(true))
	{
		const FNetPushObjectHandle Handle(PushId);
		MarkNetObjectStateDirty(Handle.GetNetHandle(), StartRepIndex, EndRepIndex);
	}
}	

}

#endif
