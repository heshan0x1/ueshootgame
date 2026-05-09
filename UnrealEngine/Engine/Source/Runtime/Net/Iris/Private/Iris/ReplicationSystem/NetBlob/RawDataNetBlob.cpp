// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// RawDataNetBlob.cpp —— 原始字节流 NetBlob 实现
// -----------------------------------------------------------------------------
// 设计要点：
//   * 构造时强制设置 ENetBlobFlags::RawDataNetBlob 标志位 —— 上游分片器/Assembler
//     可据此跳过普通 blob 的反序列化-再序列化流程，转走快路径直接拷贝 bit stream。
//   * 序列化格式：[PackedUint32 BitCount][BitCount 位裸数据]。
//   * 四个 Serialize 入口（SerializeWithObject / Serialize × 收发）都直接转发到
//     InternalSerialize / InternalDeserialize —— 与 NetRefHandle 无关。
//   * 不做二次量化（no double-quantize）：内容已是位流形式，再走 Quantize 是浪费。
// =============================================================================

#include "Iris/ReplicationSystem/NetBlob/RawDataNetBlob.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Net/Core/Trace/NetTrace.h"

namespace UE::Net
{

// 构造：在传入 Flags 上 OR 一个 RawDataNetBlob 标识位，让框架的快速分片/重组路径
// 能够通过 GetCreationInfo().Flags 识别本类型。
FRawDataNetBlob::FRawDataNetBlob(const FNetBlobCreationInfo& InCreationInfo)
: FNetBlob(FNetBlobCreationInfo{InCreationInfo.Type, InCreationInfo.Flags | ENetBlobFlags::RawDataNetBlob})
{
}

// 移动语义版本：避免拷贝大块位流。
void FRawDataNetBlob::SetRawData(TArray<uint32>&& InRawData, uint32 InRawDataBitCount)
{
	RawData = MoveTemp(InRawData);
	RawDataBitCount = InRawDataBitCount;
}

// 拷贝语义版本：用于调用方仍持有原数据的场景。
void FRawDataNetBlob::SetRawData(const TArrayView<const uint32> InRawData, uint32 InRawDataBitCount)
{
	RawData = InRawData;
	RawDataBitCount = InRawDataBitCount;
}

// 写：先变长写出有效 bit 数，再按位拷贝 RawData[0..bitcount)。
void FRawDataNetBlob::InternalSerialize(FNetSerializationContext& Context) const
{
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	UE_NET_TRACE_SCOPE(RawDataNetBlob, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
	WritePackedUint32(Writer, RawDataBitCount);
	Writer->WriteBitStream(RawData.GetData(), 0, RawDataBitCount);
}

// 读：变长读位数 → 按 (n+31)/32 大小分配 uint32 缓冲 → 按位读回。
void FRawDataNetBlob::InternalDeserialize(FNetSerializationContext& Context)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	UE_NET_TRACE_SCOPE(RawDataNetBlob, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
	RawDataBitCount = ReadPackedUint32(Reader);
	RawData.SetNumUninitialized((RawDataBitCount + 31U)/32U);
	Reader->ReadBitStream(RawData.GetData(), RawDataBitCount);
}

// 以下四个虚函数无差别地走内部实现 —— RawData 不依赖目标对象上下文。

void FRawDataNetBlob::SerializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) const
{
	InternalSerialize(Context);
}

void FRawDataNetBlob::DeserializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle)
{
	InternalDeserialize(Context);
}


void FRawDataNetBlob::Serialize(FNetSerializationContext& Context) const
{
	InternalSerialize(Context);
}

void FRawDataNetBlob::Deserialize(FNetSerializationContext& Context)
{
	InternalDeserialize(Context);
}

}
