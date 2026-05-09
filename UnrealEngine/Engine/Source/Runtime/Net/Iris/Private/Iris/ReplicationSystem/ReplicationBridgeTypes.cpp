// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：ReplicationBridgeTypes.cpp
// 角色：ReplicationBridgeTypes.h 中枚举的 LexToString 实现（仅日志/诊断用）。
// =====================================================================================

#include "Iris/ReplicationSystem/ReplicationBridgeTypes.h"


// 将多个位标志按出现顺序拼接成 "Flag1,Flag2,..."；None 单独返回 "None"。
FString LexToString(EEndReplicationFlags EndReplicationFlags)
{
	
	if (EndReplicationFlags == EEndReplicationFlags::None)
	{
		return TEXT("None");
	}

	FString Flags;

	if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::Destroy))
	{
		Flags += TEXT("Destroy");
	}
	if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::TearOff))
	{
		if (!Flags.IsEmpty()) { Flags += TEXT(','); }
		Flags += TEXT("TearOff");
	}
	if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::Flush))
	{
		if (!Flags.IsEmpty()) { Flags += TEXT(','); }
		Flags += TEXT("Flush");
	}
	if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::DestroyNetHandle))
	{
		if (!Flags.IsEmpty()) { Flags += TEXT(','); };
		Flags += TEXT("DestroyNetHandle");
	}
	if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::ClearNetPushId))
	{
		if (!Flags.IsEmpty()) { Flags += TEXT(','); };
		Flags += TEXT("ClearNetPushId");
	}
	if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::SkipPendingEndReplicationValidation))
	{
		if (!Flags.IsEmpty()) { Flags += TEXT(','); };
		Flags += TEXT("SkipPendingEndReplicationValidation");
	}
	
	return Flags;
}

// 销毁原因 -> 文本（DoNotDestroy / TearOff / Destroy）。
const TCHAR* LexToString(EReplicationBridgeDestroyInstanceReason Reason)
{
	switch (Reason)
	{
	case EReplicationBridgeDestroyInstanceReason::DoNotDestroy:
	{
		return TEXT("DoNotDestroy");
	}
	case EReplicationBridgeDestroyInstanceReason::TearOff:
	{
		return TEXT("TearOff");
	}
	case EReplicationBridgeDestroyInstanceReason::Destroy:
	{
		return TEXT("Destroy");
	}
	default:
	{
		return TEXT("[Invalid]");
	}
	}
}

// 销毁标志 -> 文本（仅 None / AllowDestroyInstanceFromRemote）。
const TCHAR* LexToString(EReplicationBridgeDestroyInstanceFlags DestroyFlags)
{
	switch (DestroyFlags)
	{
	case EReplicationBridgeDestroyInstanceFlags::None:
	{
		return TEXT("None");
	}
	case EReplicationBridgeDestroyInstanceFlags::AllowDestroyInstanceFromRemote:
	{
		return TEXT("AllowDestroyInstanceFromRemote");
	}
	default:
	{
		return TEXT("[Invalid]");
	}
	}
}