// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件: ChunkedDataStreamCommon.h
// 模块: Iris / ReplicationSystem / ChunkedDataStream（分块大数据流，子模块共享头）
//
// 概述：
//   本文件是 ChunkedDataStream 子模块（UChunkedDataStream + FChunkedDataWriter
//   + FChunkedDataReader）共用的"协议常量 + 日志宏"集中定义点。
//   ChunkedDataStream 用于把"无法塞进单个网络包"的大块业务 payload（如
//   levelstreaming、save game upload、自定义大块业务数据）切分成固定大小的
//   小 chunk，按可靠 + 有序的方式分多个 packet 发送，对端再重组为完整 payload。
//
//   本头里只定义两类东西：
//     1) 日志类别 LogIrisChunkedDataStream + 连接级日志宏
//        UE_LOG_CHUNKEDDATASTREAM_CONN（自动追加 ReplicationSystemId / ConnectionId）；
//     2) 协议常量结构 FChunkedDataStreamParameters：
//        - 序列号位宽（11 位 → 0..2047 循环）；
//        - 最大未确认分片数（= 1 << 11 = 2048，决定环形发送缓冲容量）；
//        - 单个 chunk 的 payload 字节上限 ChunkSize（192 B）；
//        - 导出（exports）段长度字段的位宽（32 位）。
//
// 与文档对应：
//   - Iris_Architecture.md §3.8 ChunkedDataStream 总览；
//   - ReplicationSystem.md §6.7 ChunkedDataStream 子模块；
//   - DataStream.md（基类 UDataStream 写读管线）。
// =============================================================================

#pragma once

#include "Iris/Core/IrisLog.h"
#include "Net/Core/Trace/NetTrace.h"

// -----------------------------------------------------------------------------
// 日志详细等级编译开关
// -----------------------------------------------------------------------------
// Shipping 构建剔除 Verbose/VeryVerbose 等高频日志，避免影响发布版性能；
// 非 Shipping（Development / Test / Editor）保留全部 verbosity 以便调试。
#ifndef UE_NET_CHUNKEDDATASTREAM_LOG_COMPILE_VERBOSITY
// Don't compile verbose logs in Shipping builds	
#if UE_BUILD_SHIPPING
#	define UE_NET_CHUNKEDDATASTREAM_LOG_COMPILE_VERBOSITY Log
#else
#	define UE_NET_CHUNKEDDATASTREAM_LOG_COMPILE_VERBOSITY All
#endif
#endif

// 声明本子模块专用日志类别 LogIrisChunkedDataStream（对应 .cpp 中的 DEFINE_LOG_CATEGORY）。
DECLARE_LOG_CATEGORY_EXTERN(LogIrisChunkedDataStream, Log, UE_NET_CHUNKEDDATASTREAM_LOG_COMPILE_VERBOSITY);

// 连接级日志宏：自动在格式串前追加 "ChunkedDataStream: R:<RepSysId> :C<ConnId> "，
// 便于在多 ReplicationSystem（多 PIE）/多连接环境下定位单条流的事件。
// 调用方上下文必须存在名为 InitParams 的 UDataStream::FInitParameters 成员/局部变量
// （Reader/Writer 两个类都缓存了它）。
#define UE_LOG_CHUNKEDDATASTREAM_CONN(Verbosity, Format, ...)  UE_LOG(LogIrisChunkedDataStream, Verbosity, TEXT("ChunkedDataStream: R:%u :C%u ") Format, InitParams.ReplicationSystemId, InitParams.ConnectionId, ##__VA_ARGS__)

namespace UE::Net::Private
{

// -----------------------------------------------------------------------------
// FChunkedDataStreamParameters：分块流协议常量
// -----------------------------------------------------------------------------
// 这些常量同时被 Writer / Reader 使用，必须保持收发两端一致——任何修改都
// 等同于一次"协议版本升级"，需要客户端/服务端同步重打包。
//
// 协议布局（粗略，详见 FDataChunk::Serialize）：
//   每条 packet 的 ChunkedDataStream 段：
//     while (ReadBool()) /* 1 bit 续帧标记 */
//     {
//        sequence : 1 bit (bIsInSequence) [+ 11 bit 显式序号]
//        chunk-header :
//            bIsFirstChunk : 1 bit
//            if (bIsFirstChunk):
//                bIsExportChunk : 1 bit
//                PartCount      : packed uint32（总分片数）
//            bIsFullChunk : 1 bit
//            if (!bIsFullChunk):
//                PartByteCount : packed uint16（本分片实际字节数）
//        payload : <PartByteCount> bytes（首片若 PartCount>1 则恒为 ChunkSize）
//     }
// -----------------------------------------------------------------------------
struct FChunkedDataStreamParameters
{	
	// 序列号位宽：11 bit，序号空间 [0, 2048)，环形回绕。
	static constexpr uint32 SequenceBitCount = 11U;

	// 最大同时在途/未 ACK 的分片数 = 2048。
	// 同时也是 Sent[] / Acked[] 位图的大小，以及 SequenceToIndex 的模数。
	// 当 DataChunksPendingAck.Num() 达到此值时 Writer 暂停发送新分片，等 ACK。
	static constexpr uint32 MaxUnackedDataChunkCount = 1 << SequenceBitCount;

	// 序号掩码 0x7FF，用于把任意 uint16 折回 11 位序号空间。
	static constexpr uint32 SequenceBitMask = (1U << SequenceBitCount) - 1U;

	// 单个分片承载的 payload 字节上限。
	// 选择 192 B：足够小，可在多种 MTU 下与其他 DataStream 共存于同一 packet；
	// 又足够大，使得切分开销（每片头）相对于 payload 不至于失衡。
	static constexpr uint32 ChunkSize = 192U;

	// 导出段（exports payload）头部"该段总位数"字段的位宽。
	// 32 位足以表达单批次 exports 的最大可能大小（最大为 ExportsBufferMaxSize=512KB）。
	static constexpr uint32 NumBitsForExportOffset = 32U;
};

} // End of namespace(s)
