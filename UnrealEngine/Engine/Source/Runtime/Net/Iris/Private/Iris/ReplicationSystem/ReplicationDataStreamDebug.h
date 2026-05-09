// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// ReplicationDataStreamDebug.h —— Bitstream 调试辅助
// -----------------------------------------------------------------------------
// 仅在非 Shipping 编译；用于在比特流中插入"调试探针"：
//   • BatchSizePerObject  每个对象 batch 起点写入"长度"字段，方便接收侧定位错误位置；
//   • Sentinels           在关键边界写入哨兵字（uint32 模式串），接收端校验；
// 这些位会显著增大流量，仅用于排查 bitstream 错位/protocol mismatch 等疑难。
// 通过 net.Iris.* CVar 控制；写入端写入"启用了哪些 features"位图，读取端再据此解析。
// =============================================================================

#pragma once

#include "HAL/Platform.h"
#include "Misc/EnumClassFlags.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"

// Whether said features should be compiled in or not. Individual features should be togglable by cvars.
// 编译开关：Shipping 下完全去除这些调试路径。运行时再用 CVar 控制具体功能。
#ifndef UE_NET_REPLICATIONDATASTREAM_DEBUG
	#define UE_NET_REPLICATIONDATASTREAM_DEBUG !(UE_BUILD_SHIPPING)
#endif

namespace UE::Net::Private
{

// CVar net.Iris.
// 控制每对象 batch 起点是否插入"长度字段"，方便定位单个 batch 的解码错误。
extern bool bReplicationDataStreamDebugBatchSizePerObjectEnabled;
// CVar net.Iris.
// 控制是否在关键边界写入哨兵字（4 字节模式串）。
extern bool bReplicationDataStreamDebugSentinelsEnabled;

// 调试 feature 位图：写入端先写一个 N 位字段告诉接收端"启用了哪些"，接收端据此走对应解析路径。
enum EReplicationDataStreamDebugFeatures : uint32
{
	None = 0U,
	BatchSizePerObject = 1U << 0U,
	Sentinels = 1U << 1U,
};
ENUM_CLASS_FLAGS(EReplicationDataStreamDebugFeatures);

// 当前使用 2 个 feature 位（如未来扩展，需要同步增加）
static constexpr uint32 ReplicationDataStreamDebugFeaturesBitCount = 2U;

// 写：把 Features 位图写入 Bitstream（占 ReplicationDataStreamDebugFeaturesBitCount 位）
inline void WriteReplicationDataStreamDebugFeatures(FNetBitStreamWriter* Writer, EReplicationDataStreamDebugFeatures Features)
{
	Writer->WriteBits(static_cast<uint32>(Features), ReplicationDataStreamDebugFeaturesBitCount);
}

// 读：取出 Features 位图，调用方据此判断后续是否要读 BatchSize / Sentinel
inline EReplicationDataStreamDebugFeatures ReadReplicationDataStreamDebugFeatures(FNetBitStreamReader* Reader)
{
	EReplicationDataStreamDebugFeatures Features = static_cast<EReplicationDataStreamDebugFeatures>(Reader->ReadBits(ReplicationDataStreamDebugFeaturesBitCount));
	return Features;
}

}
