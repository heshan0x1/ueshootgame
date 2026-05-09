// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// NetBlobAssembler.h —— 分片 blob 重组器。
//
// 背景：
//   当一个 blob 序列化后超过 packet/分片阈值时，FPartialNetObjectAttachmentHandler 或
//   USequentialPartialNetBlobHandler 会把它切成多个 FPartialNetBlob 顺序发送。
//   接收端按序把这些分片喂给 FNetBlobAssembler，重组完成后产出原始 blob 对象。
//
// 工作机制：
//   - 每个分片携带 (SequenceNumber, PartIndex, PartCount, OriginalCreationInfo, payload bits)。
//   - 第一片必须先到（IsFirstPart()==true），后续分片必须按序递增 SequenceNumber。
//   - 可靠序列：必须严格连续；任何错位都视为永久性破坏（bIsBrokenSequence）。
//   - 不可靠序列：允许从中途丢弃后接收新一轮的"第一片"重启。
//
// 状态机：
//   AddPartialNetBlob  ──► 收到第一片：分配 payload buffer + 写入第 0 段
//                       ─► 收到后续片：写入对应段；最后一片到达后置位 bIsReadyToAssemble
//   IsReadyToAssemble() == true 后调用 Assemble(Context) 产出最终 blob。
//
// 与 ReplicationSystem.md §6.5 中 "FNetBlobAssembler：分片重组" 对应。
// =====================================================================================

#pragma once
#include "Iris/ReplicationSystem/NetBlob/PartialNetBlob.h"
#include "Iris/Serialization/NetBitStreamWriter.h"

class USequentialPartialNetBlobHandlerConfig;

namespace UE::Net
{

// 初始化参数：必须提供 USequentialPartialNetBlobHandlerConfig（含 MaxPartCount /
// MaxPartBitCount 等校验阈值）。Assembler 用它做防御性检查，避免恶意/错误的分片
// 申请超大缓冲。
struct FNetBlobAssemblerInitParams
{
	const USequentialPartialNetBlobHandlerConfig* PartialNetBlobHandlerConfig = nullptr;
};

/**
 * Utility class to reassemble split blobs.
 * Partial blobs need to be added in order and represent the same blob until it's assembled or an error occurs.
 */
// 分片 blob 重组器。一个 Assembler 实例同一时刻只能负责一个 blob 的重组；
// 一旦 Assemble 完成或者序列被破坏，需要等接收新的"第一片"重启序列。
class FNetBlobAssembler
{
public:
	IRISCORE_API FNetBlobAssembler();

	/** Initialize the NetBlobAssembler. */
	// 注入 PartialNetBlobHandlerConfig，提供大小/数量上限做边界检查。
	IRISCORE_API void Init(const FNetBlobAssemblerInitParams& InitParams);

	/**
	 * Add the next expected part of the split blob. If it's the first part of a blob the effects of prior calls to this function are discarded.
	 * @param Context FNetSerializationContext for error reporting. Call HasError() on it afterwards to check whether something went wrong.
	 * @param NetHandle The NetHandle needs to remain constant for every call until the original blob has been assembled or the first part of a new blob is added.
	 */
	// 喂入下一个分片。
	//   - 若 IsFirstPart：丢弃此前未完成的部分，重置序列、PayloadBitCount 上限、PartCount。
	//   - 否则要求 RefHandle 与 SequenceNumber 都连续，且每片大小不超过第一片
	//     （仅最后一片可以更小）。
	//   - 检测到错位时设置 Context 错误并标记 bIsBrokenSequence；后续调用直接拒绝。
	IRISCORE_API void AddPartialNetBlob(FNetSerializationContext& Context, FNetRefHandle RefHandle, const TRefCountPtr<FPartialNetBlob>& PartialNetBlob);

	/** Returns true if all parts of the split blob have been added and is ready to be assembled. */
	bool IsReadyToAssemble() const { return bIsReadyToAssemble; }

	/** Returns true if the sequence order is broken. */
	bool IsSequenceBroken() const { return bIsBrokenSequence; }

	/**
	 * Assemble all parts of the split blob.
	 * @param Context FNetSerializationContext for error reporting. Call HasError() on it afterwards to check whether something went wrong.
	 * @return The assembled blob.
	 * @note IsReadyToAssemble() must return true before calling this function.
	 * @note After this function has been called it may not be called again until a new blob is ready to be assembled.
	 */
	// 把累积的 payload 还原为完整 blob：
	//   1. 用 OriginalCreationInfo 通过 INetBlobReceiver::CreateNetBlob 反查 handler
	//      创建对应类型的空 blob。
	//   2. 若 blob 是 RawDataNetBlob：直接 SetRawData（避免再走一遍量化）。
	//      否则在 Payload 上构造 BitReader，调用 blob->Deserialize(WithObject)。
	//   3. 校验 BitWriter / BitReader 位数一致，否则报 GNetError_BitStreamError。
	IRISCORE_API TRefCountPtr<FNetBlob> Assemble(FNetSerializationContext& Context);

private:
	// 第一片中保存的原始 blob 创建信息：决定 Assemble 时创建什么类型的 blob。
	FNetBlobCreationInfo NetBlobCreationInfo;
	// 累积所有分片 payload 的连续缓冲区（uint32 数组，按位写入）。
	TArray<uint32> Payload;
	// 写入器：定位于 Payload 末尾，按 PayloadBitCount 追加每个分片的字节流。
	FNetBitStreamWriter BitWriter;
	// 当前正在重组的 blob 对应的 NetRefHandle（owner）；后续分片必须保持一致。
	FNetRefHandle RefHandle;
	const USequentialPartialNetBlobHandlerConfig* PartialNetBlobHandlerConfig = nullptr;
	uint32 NextPartIndex = 0;
	// Initialized to an arbitrary number. If we receive the first part of a sequence it doesn't matter. If the next received blob isn't the first part it's a broken sequence.
	// 期望的下一个分片序号；初始任意值。收到 IsFirstPart 时被重置。
	uint32 NextSequenceSumber = 0U;
	// The sequence number of the last part expected to be received before being ready to assemble.
	// 最后一片的序号 = FirstSequenceNumber + PartCount - 1。
	uint32 LastPartSequenceNumber = ~0U;
	// 第一片的 PayloadBitCount，作为后续分片大小上限的校验基准（最后一片例外）。
	uint32 FirstPayloadBitCount = 0;
	// 全部分片就位、可调用 Assemble。
	bool bIsReadyToAssemble = false;
	// 序列已损坏（不可靠分片错位/可靠分片不连续/bit 长度异常等），后续 Add 将拒绝。
	bool bIsBrokenSequence = false;
	// 当前序列是否在处理可靠分片。可靠序列必须严格完整接收，途中不允许重启。
	bool bIsProcessingReliable = false;
};

}
