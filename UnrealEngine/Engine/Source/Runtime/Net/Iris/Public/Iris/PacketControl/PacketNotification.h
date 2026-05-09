// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// ==========================================================================================
// PacketNotification.h —— Iris PacketControl 模块的唯一公共头文件
// ------------------------------------------------------------------------------------------
// 模块定位：L0 基础契约层（单头文件 + 单枚举，零依赖）。
// 设计目的：定义 DataStream ↔ Iris 传输层之间用于"包投递状态"通知的统一
//           契约枚举 EPacketDeliveryStatus。
// 唯一用途：作为 UDataStream::ProcessPacketDeliveryStatus(...) /
//           UDataStreamManager::ProcessPacketDeliveryStatus(...) 的参数类型。
//
// 典型调用链（从 NetConnection 层向下传递 ACK / Lost / Discard 通知）：
//   NetConnection ACK → UNetDriver / DataStreamChannel
//     → UDataStreamManager::ProcessPacketDeliveryStatus(status, record)
//       → 内部按 bitmask 分发到每条 UDataStream
//         → UReplicationDataStream::ProcessPacketDeliveryStatus
//           → FReplicationWriter::ProcessPacketDeliveryStatus
//         → 其它 Stream（例如 ChunkedDataStream、NetRPCHandler）按需处理
//
// 扩展代价：添加新的投递状态需要同步修改所有 ProcessPacketDeliveryStatus
//          实现，因此本枚举长期保持稳定，属于"跨模块契约"。
// ==========================================================================================

#include "CoreTypes.h"  // 仅为了拿到 uint8 —— 本文件的唯一外部依赖

namespace UE::Net
{

/**
 * Enum used to report packet notifications to DataStreams.
 *
 * 中文说明：
 *   EPacketDeliveryStatus 是 NetConnection 层向 DataStream 层通报"某个
 *   已发出包的最终命运"的三值状态枚举。每个下发到 DataStream 的包都会
 *   得到恰好一次以下三种结果之一的回调。
 *
 *   三个值语义差异（非常重要，不要混淆）：
 *     - Delivered：对端 ACK 已回到本机。DataStream 可以释放与这个包相关
 *                  的所有"等待 ACK"的临时状态，并把承载的数据标记为
 *                  "已同步到对端"（例如 FReplicationWriter 会把对象的
 *                  确认窗口向前推进）。
 *     - Lost     ：已判定该包不会被送达 —— 常见情形是 NetConnection 的
 *                  序列号窗口推进后仍未收到 ACK、或对端返回了"乱序丢弃"
 *                  的隐式信号。DataStream 需要把这个包里承载的状态"回滚"
 *                  或"标记为待重传"，但"FDataStreamRecord 结构本身仍需
 *                  保留"，因为它承载的信息将在下次发送时被重用。
 *     - Discard  ：不是"投递成功 / 失败"的结论，而是"请彻底释放这条
 *                  FDataStreamRecord 所占用的资源"的指令。典型触发场景：
 *                  连接被关闭、DataStream 被销毁、或 Iris 在清理未完成的
 *                  Record 队列。DataStream 必须释放本条 Record 所挂的所有
 *                  子资源，但**不应**把数据当作"送达"或"丢失"处理 ——
 *                  下一次重连会从头开始重建状态。
 *
 *   底层类型固定为 uint8：
 *     - 与 DataStream bitmask 分发的位宽匹配；
 *     - 便于作为 FDataStreamRecord 内短字段紧凑存放。
 */
enum class EPacketDeliveryStatus : uint8
{
	/** The packet was delivered. */
	// 已投递：对端已 ACK，DataStream 应推进"已确认"状态并释放等待队列里的对应条目。
	Delivered,

	/** The packet was lost or ignored by the recipient due to out of order delivery for example. */
	// 已丢失：NetConnection 层判定此包不会到达（超时未 ACK / 对端乱序丢弃 等）。
	// DataStream 需要回滚状态或将未确认数据标记为"待重传"，但 Record 结构本身通常仍会被后续流程复用。
	Lost,

	/** Free any resource related to this packet, such as a DataStreamRecord. Typically used when closing connections and similar scenarios. */
	// 丢弃：上层指示直接释放本条记录绑定的资源（如 FDataStreamRecord 里的动态对象句柄），
	// 常见于连接关闭、DataStream 销毁或资源回收场景。语义上"既非成功也非失败"，
	// 仅代表"不再关心这个包的最终结局，请立即清理"。
	Discard,
};

}

// 文件结束 —— 本头文件只定义契约枚举，无其它符号、无内联实现。
