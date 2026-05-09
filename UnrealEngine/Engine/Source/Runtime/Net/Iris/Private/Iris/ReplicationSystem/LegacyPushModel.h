// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：LegacyPushModel.h
// 模块：Iris / ReplicationSystem（辅助 & 周边）
// 功能：把 UE 旧 PushModel 体系（MARK_PROPERTY_DIRTY 系列宏）桥接到 Iris 的脏跟踪。
//
// 背景：
//   * 旧 Replication 用 FPushModel：每个支持 push 的 UObject 持有一个
//     uint64 NetPushID（FNetIrisPushObjectId 在 Iris 模式下复用此字段），
//     宏展开时调用 UE_NET_xx_DELEGATE → 委托回这里。
//   * Iris 不再使用 PushModel 内部表，但为了兼容海量已有代码继续用
//     MARK_PROPERTY_DIRTY，必须把回调转发到 Iris 自己的 FDirtyNetObjectTracker。
//
// 桥接路径：
//   MARK_PROPERTY_DIRTY(Owner, Property)
//     → FPushModelHelpers (NetCore)
//     → FIrisMarkPropertyDirty 委托（本文件 SetDelegate）
//     → FNetHandleLegacyPushModelHelper::MarkPropertyOwnerDirty(...)
//     → MarkNetObjectStateDirty(NetHandle, RepIndex, RepIndex)（在 GlobalDirtyNetObjectTracker）
//     → 由各 RS 的 FDirtyNetObjectTracker::ApplyGlobalDirtyObjectList 在
//        StartPreSendUpdate 拉取。
//
// 模式（cvar `net.Iris.PushModelMode`）：
//   * 0 = 关闭（但运行时可切回）；
//   * 1 = 启用且不可切（直接绑无判断的 Mark*OwnerDirty）；
//   * 2 = 启用但运行时可切（绑 OptionallyMark*，每次回调都查 IsIrisPushModelEnabled）。
//
// 模块入口：
//   * UE_NET_IRIS_INIT_LEGACY_PUSH_MODEL()      —— FIrisCoreModule::StartupModule 调用。
//   * UE_NET_IRIS_SHUTDOWN_LEGACY_PUSH_MODEL()  —— FIrisCoreModule::ShutdownModule 调用。
//   * UE_NET_IRIS_SET_PUSH_ID(Object, Handle)   —— 为对象写入 NetPushID。
//   * UE_NET_IRIS_CLEAR_PUSH_ID(Object)         —— 解绑时清掉 NetPushID。
//
// 当 WITH_PUSH_MODEL == 0 时，所有宏退化为空。
// =====================================================================================

#pragma once

#if WITH_PUSH_MODEL

#include "CoreTypes.h"
#include "Net/Core/NetHandle/NetHandle.h"
#include "Net/Core/PushModel/PushModel.h"

namespace UE::Net
{
	struct FReplicationProtocol;
	struct FReplicationInstanceProtocol;
}

namespace UE::Net::Private
{

// 中文：cvar 同名变量——bIsIrisPushModelForceEnabled 是模式 1 的强制开关，
// IrisPushModelMode 是 cvar 当前值（运行时可切）。两者共同决定 IsIrisPushModelEnabled。
extern IRISCORE_API bool bIsIrisPushModelForceEnabled;
extern IRISCORE_API int IrisPushModelMode;
// 中文：被 MARK_PROPERTY_DIRTY 宏内部检查——三态合取：
//   (Force 强开) | (cvar > 0) ＆ (运行时 Push 开启)。
inline bool IsIrisPushModelEnabled(bool bIsPushModelEnabled = IS_PUSH_MODEL_ENABLED()) { return (bIsIrisPushModelForceEnabled | (IrisPushModelMode > 0)) & bIsPushModelEnabled; }

/**
 * FNetPushObjectHandle
 * 中文：把 FNetHandle / FNetIrisPushObjectId 互转的薄包装。
 *   * 上层（PushModel 宏）只看到 uint64 PushObjectId。
 *   * 下层（Iris）需要 FNetHandle 来查 InternalIndex / RS。
 *   * 两者实际共用同一个 64-bit 表示，本类只做类型双向 reinterpret。
 */
class FNetPushObjectHandle
{
public:
	// 中文：从 FNetHandle 构造（外部调用入口）。
	FNetPushObjectHandle(FNetHandle InNetHandle);

	bool IsValid() const;

private:
	friend struct FNetHandleLegacyPushModelHelper;

	// 中文：私有构造——从 PushObjectId 反向构造 FNetHandle（仅 Helper 使用）。
	explicit FNetPushObjectHandle(UEPushModelPrivate::FNetIrisPushObjectId PushId);
	FNetHandle GetNetHandle() const;
	UEPushModelPrivate::FNetIrisPushObjectId GetPushObjectId() const;

	FNetHandle NetHandle;
};

/**
 * FNetHandleLegacyPushModelHelper
 * 中文：所有桥接静态函数的容器。InitPushModel/ShutdownPushModel 由
 * IrisCoreModule 在生命周期边界调用；SetNetPushID/ClearNetPushID 由
 * Bridge 在对象注册/注销时调用。
 */
struct FNetHandleLegacyPushModelHelper
{
	// 中文：根据 cvar 模式注册/解注 IrisMarkPropertyDirty 系列委托。
	static void InitPushModel();
	static void ShutdownPushModel();

	// 中文：把 FNetPushObjectHandle 写入对象的 NetPushIdDynamic 字段——
	// 之后 MARK_PROPERTY_DIRTY 拿到的 PushId 即可反推 FNetHandle。
	IRISCORE_API static void SetNetPushID(UObject* Object, FNetPushObjectHandle PushHandle);
	IRISCORE_API static void ClearNetPushID(UObject* Object);

private:
	// 中文：模式 1 的回调——直接转发到 Iris MarkNetObjectStateDirty（带 RepIndex 范围）。
	static void MarkPropertyOwnerDirty(const UObject* Object, UEPushModelPrivate::FNetIrisPushObjectId PushId, const int32 RepIndex);
	static void MarkPropertiesOwnerDirty(const UObject* Object, UEPushModelPrivate::FNetIrisPushObjectId PushId, const int32 StartRepIndex, const int32 EndRepIndex);

	// 中文：模式 2 的回调——回调时再做一次 IsIrisPushModelEnabled 判断，
	// 适合"想保留运行时切换 push/全量 poll 调试能力"的场景。
	static void OptionallyMarkPropertyOwnerDirty(const UObject* Object, UEPushModelPrivate::FNetIrisPushObjectId PushId, const int32 RepIndex);
	static void OptionallyMarkPropertiesOwnerDirty(const UObject* Object, UEPushModelPrivate::FNetIrisPushObjectId PushId, const int32 StartRepIndex, const int32 EndRepIndex);
};


inline FNetPushObjectHandle::FNetPushObjectHandle(FNetHandle InNetHandle)
: NetHandle(InNetHandle)
{
}

inline FNetPushObjectHandle::FNetPushObjectHandle(UEPushModelPrivate::FNetIrisPushObjectId PushId)
: NetHandle(static_cast<uint64>(PushId))
{
}

inline bool FNetPushObjectHandle::IsValid() const
{
	return NetHandle.IsValid();
}

inline FNetHandle FNetPushObjectHandle::GetNetHandle() const
{
	return NetHandle;
}

inline UEPushModelPrivate::FNetIrisPushObjectId FNetPushObjectHandle::GetPushObjectId() const
{
	return NetHandle.GetInternalValue();
}

}

// 中文：对外宏（不含 namespace 限定，方便 Bridge / Module 处使用）。
#define UE_NET_IRIS_SET_PUSH_ID(...) UE::Net::Private::FNetHandleLegacyPushModelHelper::SetNetPushID(__VA_ARGS__)
#define UE_NET_IRIS_CLEAR_PUSH_ID(...) UE::Net::Private::FNetHandleLegacyPushModelHelper::ClearNetPushID(__VA_ARGS__)

// 中文：模块级初始化/反初始化宏；具体在 FIrisCoreModule::StartupModule/ShutdownModule 调用。
#define UE_NET_IRIS_INIT_LEGACY_PUSH_MODEL() UE::Net::Private::FNetHandleLegacyPushModelHelper::InitPushModel()
#define UE_NET_IRIS_SHUTDOWN_LEGACY_PUSH_MODEL()  UE::Net::Private::FNetHandleLegacyPushModelHelper::ShutdownPushModel()

#else  // !WITH_PUSH_MODEL

namespace UE::Net::Private
{

// 中文：编译关闭 Push 模型时的退化函数——始终返回 false。
inline constexpr bool IsIrisPushModelEnabled(bool /* bIsPushModelEnabled */ = false) { return false; }

}

// 中文：所有桥接宏退化为空——上层调用代码无需 #if WITH_PUSH_MODEL 包裹。
#define UE_NET_IRIS_SET_PUSH_ID(...)
#define UE_NET_IRIS_CLEAR_PUSH_ID(...)

#define UE_NET_IRIS_INIT_LEGACY_PUSH_MODEL() 
#define UE_NET_IRIS_SHUTDOWN_LEGACY_PUSH_MODEL() 

#endif
