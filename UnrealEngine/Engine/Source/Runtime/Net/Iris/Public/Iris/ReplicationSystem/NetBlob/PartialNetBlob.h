// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// PartialNetBlob.h —— "分片 NetBlob"（FPartialNetBlob）
// ---------------------------------------------------------------------------------------------
// 角色：当一个 FNetBlob（例如带大数组参数的 RPC、或大型 NetObjectAttachment）单帧/单包序列化
//       后超过 MTU 限制时，需要将其拆成多个小块 = 多个 FPartialNetBlob 分片，跨多帧/多包发出，
//       接收端再由 `FNetBlobAssembler` 按 SequenceNumber 重组回原始 blob。
//
// 协议字段（线协议）：
//   - SequenceNumber  ：全局递增序号（PackedUint32），用于检测乱序与重组。同一原始 blob 的
//                       所有分片预先连续保留一段序号。
//   - IsFirstPart     ：bool，仅首片为 true。首片额外携带 PartCount + OriginalCreationInfo。
//   - PartCount       ：仅首片有效，分片总数（PackedUint16, 实际写入 PartCount-1）。
//   - PayloadBitCount ：当前分片的 payload 比特数（PackedUint16）。
//   - Payload         ：原 blob 序列化结果按字节切分后的本片数据；为方便位流操作以 uint32 数组存储。
//
// 关键约束：
//   - MaxPartBitCount 必须 32 对齐（>>32 整数倍），保证可 memcpy；MaxPartBitCount<65536。
//   - MaxPartCount<65536；首片携带 OriginalCreationInfo 用于重组时还原原始 blob 类型 + Flags。
//   - 仅首片携带 NetObjectReference / NetToken 导出（HasExports 标志），保证导出 token 早于 payload。
// =============================================================================================

#pragma once
#include "Iris/ReplicationSystem/NetBlob/NetBlob.h"
#include "Iris/ReplicationSystem/NetBlob/RawDataNetBlob.h"
#include "Iris/Core/NetObjectReference.h"
#include "Net/Core/Trace/NetDebugName.h"

namespace UE::Net
{

// FPartialNetBlob：分片 blob 子类（final）。每个实例仅承载原始 blob 的一段连续 payload。
// 注意：发送端持有 OriginalBlob 引用以便首片在 split 时复制其 ObjectReference 导出列表；
//       接收端的 FPartialNetBlob 仅承载 Payload 比特流，由 FNetBlobAssembler 重组。
class FPartialNetBlob final : public FNetBlob
{
public:
	// 切分参数：配置一次切分行为的硬约束（来自 USequentialPartialNetBlobHandlerConfig）。
	struct FSplitParams
	{
		uint32 MaxPartBitCount;             // 每个分片 payload 最大比特数（必为 32 对齐）。
		uint32 MaxPartCount;                // 一个原始 blob 最多被切成多少片。
		FNetObjectReference NetObjectReference; // 当 bSerializeWithObject=true 时附带的目标对象引用。
		FNetDebugName DebugName;            // 仅 trace 用的可读名（无副作用）。
		bool bSerializeWithObject;          // true 走 SerializeWithObject 路径（典型 RPC/Attachment）。
	};

	// 将任意 FNetBlob 切分为多个 FPartialNetBlob。
	// 即使原 blob 序列化结果未超过 MaxPartBitCount，也会"无条件"切分（至少产出 1 片）。
	// 实现：先把原 blob 序列化到一段临时 buffer（按需 grow），再按 PartBitCount 切片。
	IRISCORE_API static bool SplitNetBlob(FNetSerializationContext& Context, const FNetBlobCreationInfo& CreationInfo, const FSplitParams& SplitParams, const TRefCountPtr<FNetBlob>& Blob, TArray<TRefCountPtr<FNetBlob>>& OutPartialBlobs);

	// FRawDataNetBlob 专用快速路径：raw 已是序列化好的字节流，直接按位偏移切片，避免二次序列化。
	IRISCORE_API static bool SplitNetBlob(const FNetBlobCreationInfo& CreationInfo, const FSplitParams& SplitParams, const TRefCountPtr<FRawDataNetBlob>& Blob, TArray<TRefCountPtr<FNetBlob>>& OutPartialBlobs);

public:
	// 序列状态位标记（写入 1 bit）。
	enum class ESequenceFlags : uint32
	{
		None = 0,
		IsFirstPart = 1U, // 该分片是否为首片：首片才包含 PartCount + OriginalCreationInfo。
	};
	FRIEND_ENUM_CLASS_FLAGS(ESequenceFlags);

	IRISCORE_API FPartialNetBlob(const FNetBlobCreationInfo& CreationInfo);

	// 仅在接收端首片返回有意义的 PartCount，其它分片返回 0。
	uint32 GetPartCount() const { return (IsFirstPart() ? PartCount : 0U); }

	bool IsFirstPart() const { return EnumHasAnyFlags(SequenceFlags, ESequenceFlags::IsFirstPart); }

	// 全局序号；同一 blob 的连续分片序号连续，乱序检测/重组依赖此值。
	uint32 GetSequenceNumber() const { return SequenceNumber; }

	uint32 GetPayloadBitCount() const { return PayloadBitCount; }
	const uint32* GetPayload() const { return Payload.GetData(); }

	// 仅首片有效：承载原始 blob 的 CreationInfo（Type + Flags），Assembler 用其重建原 blob。
	const FNetBlobCreationInfo& GetOriginalCreationInfo() const { return OriginalCreationInfo; }

	void SetDebugName(const FNetDebugName& InDebugName) { DebugName = InDebugName; }

private:

	// 仅首片返回非空导出列表，保证导出 token 在重组前被对端 ObjectReferenceCache 学习到。
	virtual TArrayView<const FNetObjectReference> GetNetObjectReferenceExports() const override final;
	virtual TArrayView<const FNetToken> GetNetTokenExports() const override final;

	// 序列化入口：本类对四种入口走相同的内部实现 InternalSerialize/Deserialize（仅写 payload + 元信息，
	// 不依赖 RefHandle 上下文，因为 payload 已在 split 时 baked）。
	virtual void SerializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) const override;
	virtual void DeserializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) override;

	virtual void Serialize(FNetSerializationContext& Context) const override;
	virtual void Deserialize(FNetSerializationContext& Context) override;

	// 内部统一线协议：[SeqNum] [IsFirstPart bit] [(if first) PartCount + OriginalCreationInfo] [PayloadBitCount + Payload bits]
	void InternalSerialize(FNetSerializationContext& Context) const;
	void InternalDeserialize(FNetSerializationContext& Context);

	// 单纯写/读 payload 段（PayloadBitCount + 比特流）。
	void InternalSerializeBlob(FNetSerializationContext& Context) const;
	void InternalDeserializeBlob(FNetSerializationContext& Context);

	// SplitPayload 的内部参数包：把"已序列化好的临时大 buffer"按 PartBitCount 切片所需的全部信息。
	struct FPayloadSplitParams
	{
		FNetDebugName DebugName;

		FNetBlobCreationInfo CreationInfo;             // 分片自身的 CreationInfo（Type=PartialHandler）。
		FNetBlobCreationInfo OriginalCreationInfo;     // 原始 blob 的 CreationInfo，由首片承载用于重组。
		FNetBlob* OriginalBlob;                        // 仅供首片复制 ObjectReference 导出列表。
		TConstArrayView<FNetObjectReference> ObjectReferencesPendingExport; // 当前 batch 内尚未导出的对象引用。
		TConstArrayView<FNetToken> NetTokensPendingExport;                  // 当前 batch 内尚未导出的 NetToken。
		const uint32* Payload;                         // 整个原 blob 的序列化字节流起点。
		uint32 PayloadBitCount;                        // 整个原 blob 的有效比特数。
		uint32 PartBitCount;                           // 每片切多少比特（32 对齐）。
	};
	// 把 Payload 按 PartBitCount 切片，并把首片打上 IsFirstPart + 拷贝导出列表。
	static void SplitPayload(const FPayloadSplitParams& SplitParams, TArray<TRefCountPtr<FNetBlob>>& OutPartialBlobs);

	// 首片承载（用于重组）：原始 blob 的 CreationInfo + 一个临时持有的 OriginalBlob 引用（仅发送端、仅在
	// 首片需要复制其导出列表时使用，不会通过线协议下发）。
	FNetBlobCreationInfo OriginalCreationInfo;
	TRefCountPtr<FNetBlob> OriginalBlob;

	ESequenceFlags SequenceFlags = ESequenceFlags::None;
	uint32 SequenceNumber = 0;
	// PartCount 仅在 IsFirstPart() 时有效；非首片该字段为 0 且不下发。
	uint16 PartCount = 0;
	uint16 PayloadBitCount = 0;
	// 使用 uint32 而非 uint8：与 FNetBitStreamReader/Writer 的字宽对齐，便于位操作和 memcpy。
	TArray<uint32> Payload;
	// 仅首片可能非空：包含原 blob 的对象引用导出 + 当前 batch 内尚未导出的对象引用。
	TArray<FNetObjectReference, TInlineAllocator<2>> ObjectReferenceExportsArray;
	// 仅首片可能非空：当前 batch 内尚未导出的 NetToken。
	TArray<FNetToken, TInlineAllocator<4>> NetTokenExportsArray;

	FNetDebugName DebugName;
};

}
