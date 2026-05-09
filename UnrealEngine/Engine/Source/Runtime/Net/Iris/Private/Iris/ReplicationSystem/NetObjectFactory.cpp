// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// 文件：NetObjectFactory.cpp
// 角色：UNetObjectFactory 基类的非纯虚方法实现：
//          * Init/Deinit/PostReceiveUpdate（生命周期）
//          * CreateHeader（写包前生成 Header 并塞入 ProtocolId / FactoryId）
//          * WriteHeader / ReadHeader（与对端协议）
//
// 序列化协议（位流顺序）：
//      WriteHeader: [FactoryId : GetMaxBits()] [ProtocolId : 32] [BitGuard : 32]? [HeaderPayload]
//      ReadHeader : [ProtocolId : 32] [BitGuard : 32]? [HeaderPayload]   ← FactoryId 已被 Bridge 在外层消费
//
// IRIS_CREATIONHEADER_BITGUARD：
//   定义后 WriteHeader 会先占位 32-bit，再写 Payload，最后回填实际写入位数；
//   ReadHeader 比对实际读取位数 vs 期望，若不一致触发 Overflow + 详细日志，
//   非常便于定位 Header Read/Write 不对称的 Bug，建议开发期开启。
// =====================================================================================

#include "Iris/ReplicationSystem/NetObjectFactory.h"

#include "Iris/Core/IrisLog.h"

#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"

#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetSerializationContext.h"

// Define this on both client and server to add extra header information and quickly detect errors in the header serialization/deserialization code

#include UE_INLINE_GENERATED_CPP_BY_NAME(NetObjectFactory)
#ifndef IRIS_CREATIONHEADER_BITGUARD
	#define IRIS_CREATIONHEADER_BITGUARD 0
#endif


//------------------------------------------------------------------------
// UNetObjectFactory
//------------------------------------------------------------------------

// 由 Bridge 在 InitNetObjectFactories 中调用：注入 FactoryId + Bridge 指针，并触发派生 OnInit。
void UNetObjectFactory::Init(UE::Net::FNetObjectFactoryId InId, UObjectReplicationBridge* InBridge)
{
	FactoryId = InId;
	Bridge = InBridge;

	OnInit();
}

// 反向：先派生 OnDeinit 再断开 Bridge 引用，避免派生在清理中访问 dangling Bridge。
void UNetObjectFactory::Deinit()
{
	OnDeinit();

	Bridge = nullptr;
}

void UNetObjectFactory::PostReceiveUpdate()
{
	OnPostReceiveUpdate();
}

// 写包侧：生成完整 Header（含 FactoryId + ProtocolId + 派生字段）。
// 失败原因通常是：handle 非法 / 非本 RS 复制 / 派生 CreateAndFillHeader 返回空。
TUniquePtr<UE::Net::FNetObjectCreationHeader> UNetObjectFactory::CreateHeader(UE::Net::FNetRefHandle Handle, UE::Net::FReplicationProtocolIdentifier ProtocolId)
{
	using namespace UE::Net;

	const bool bIsValid = Bridge->IsReplicatedHandle(Handle);
	if (UNLIKELY(!bIsValid))
	{
		ensureMsgf(bIsValid, TEXT("%s::CreateLocalHeader received invalid or non-replicated handle: %s"), *GetNameSafe(GetClass()), *Handle.ToString());
		return nullptr;
	}

	// Ask the derived class to allocate and fill the header
	// 让派生类完成派生 Header 的实例化与字段填充。
	TUniquePtr<FNetObjectCreationHeader> Header = CreateAndFillHeader(Handle);

	if (LIKELY(Header.IsValid()))
	{
		Header->SetProtocolId(ProtocolId);
		Header->SetFactoryId(FactoryId);
		return Header;
	}

	return nullptr;
}

// 写 Header 到位流：FactoryId -> ProtocolId -> [BitGuard] -> 派生 SerializeHeader
bool UNetObjectFactory::WriteHeader(UE::Net::FNetRefHandle Handle, UE::Net::FNetSerializationContext& Serialization, const UE::Net::FNetObjectCreationHeader* Header)
{
	using namespace UE::Net;

	FNetBitStreamWriter* Writer = Serialization.GetBitStreamWriter();
	check(Writer);

	check(Header->GetNetFactoryId() == FactoryId);
	check(FactoryId != InvalidNetObjectFactoryId);

	// FactoryID is always serialized first since the bridge will read it first to find the right factory 
	// FactoryId 必须最先写——接收端 Bridge 会先读它路由到对应 Factory，再调 ReadHeader。
	Writer->WriteBits(Header->GetNetFactoryId(), FNetObjectFactoryRegistry::GetMaxBits());
	Writer->WriteBits(Header->GetProtocolId(), 32);

#if IRIS_CREATIONHEADER_BITGUARD
	// Preserialize bits that will hold the header bit size
	// 预占 32-bit 用于稍后回填实际 Payload 位数。
	const uint32 StartPos = Writer->GetPosBits();
	Writer->WriteBits(0, 32);
#endif

	
	// 派生类写入具体字段。
	const bool bSuccess = SerializeHeader(FCreationHeaderContext(Handle, Bridge, this, Serialization), Header);

#if IRIS_CREATIONHEADER_BITGUARD
	if (bSuccess && !Writer->IsOverflown())
	{
		// Go back and write the final header bit size
		// 用 ScopedSeek 回写 Payload 实际位数。
		const uint32 BitsWritten = Writer->GetPosBits() - StartPos;
		FNetBitStreamWriteScope WriteScope(*Writer, StartPos);
		Writer->WriteBits(BitsWritten, 32);
	}
#endif

	return bSuccess && !Writer->IsOverflown();
}

// 读 Header：ProtocolId -> [BitGuard] -> 派生 CreateAndDeserializeHeader
// 注意：FactoryId 已由 Bridge 在外层读取（用于反查 Factory），不再重复读取。
TUniquePtr<UE::Net::FNetObjectCreationHeader> UNetObjectFactory::ReadHeader(UE::Net::FNetRefHandle Handle, UE::Net::FNetSerializationContext& Serialization)
{
	using namespace UE::Net;

	FNetBitStreamReader* Reader = Serialization.GetBitStreamReader();
	check(Reader);

	// FactoryId was already read by the Bridge
	const FReplicationProtocolIdentifier ProtocolId = Reader->ReadBits(32);

#if IRIS_CREATIONHEADER_BITGUARD
	// Find the amount of bits we are expected to read
	// BitGuard：取出对端写入的 Payload 位数，待 Payload 解析完毕做对账。
	const uint32 StartPos = Reader->GetPosBits();
	const uint32 ExpectedReadBits = Reader->ReadBits(32);
#endif

	// 派生类构造并填充 Header。
	TUniquePtr<FNetObjectCreationHeader> Header = CreateAndDeserializeHeader(FCreationHeaderContext(Handle, Bridge, this, Serialization));	

#if IRIS_CREATIONHEADER_BITGUARD
	const uint32 ActualReadBits = Reader->GetPosBits() - StartPos;
	if (ActualReadBits != ExpectedReadBits)
	{
		// 位数不匹配：通常说明 Read/Write 不对称（可能新增/漏写字段）。强制 Overflow 防止后续乱序解析。
		Reader->DoOverflow();
		UE_LOG(LogIris, Error, TEXT("Found deserialization error in %s for %s. Header: %s. Source wrote %u bits but we read %u bits (delta: %d)"), *GetName(), *Handle.ToString(), Header.IsValid() ? *Header->ToString() : TEXT("invalid"), ExpectedReadBits, ActualReadBits, (ActualReadBits-ExpectedReadBits));
		ensureMsgf(ActualReadBits == ExpectedReadBits, TEXT("Found deserialization error in %s for %s"), *GetName(), *Handle.ToString());
		return nullptr;
	}
#endif

	if (LIKELY(Header.IsValid() && !Reader->IsOverflown()))
	{
		Header->SetFactoryId(FactoryId);
		Header->SetProtocolId(ProtocolId);
		return Header;
	}

	return nullptr;
}

