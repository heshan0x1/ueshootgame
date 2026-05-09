// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NetRPC.cpp —— Iris RPC 数据载体的完整实现
// -----------------------------------------------------------------------------
// 阅读路线：
//   * 序列化: Serialize / SerializeWithObject —— 写 Header(占位) → ObjectRef
//             → FunctionLocator → Payload(已 Quantize 的参数) → 回填 Header。
//   * 反序列化: Deserialize / DeserializeWithObject —— 读 Header(取得总位数)
//             → ObjectRef → FunctionLocator → ResolveFunctionAndObject
//             → Payload；若 Resolve 失败则 Seek 到 PostNetRPCPos 跳过整段，
//             保证后续 blob 不被错位。
//   * Create —— 发送侧把 UFunction* 与原始参数 buffer 组装成 FNetRPC 实例：
//       1) 走 SuperFunction 链找到顶层（蓝图派生函数共享 locator）；
//       2) NetRPC_GetFunctionLocator 在 ReplicationProtocol 状态描述符 + 成员函
//          数描述符表中线性查找匹配的 UFunction，得到 (Descriptor,Function) 索引；
//       3) Quantize 参数到 QuantizedBlobState（有 server 端 stomp 检测可选 Protect）；
//       4) FNetReferenceCollector 收集 payload 内可导出的对象引用。
//   * CallFunction —— 接收侧执行：
//       a) 解析 root/sub 对象，校验 UFunction、是否 Net 函数、方向（Server/Client）；
//       b) 服务器还要校验"target 是不是该 connection 拥有"（IsServerAllowedToExecuteRPC）；
//       c) 分配栈上 FunctionParameters → Dequantize → ForwardNetRPCCallDelegate
//          多播（外部 Hook）→ Object->ProcessEvent；
//       d) 销毁参数。
// 注意点：
//   * BP 派生函数：FunctionLocator 指向 SuperFunction，但实际调用要 Object
//     ->FindFunction(Function->GetFName()) 拿到子类覆写版本（UE-220400）。
//   * 反序列化结束后还会校验 PostNetRPCPos 与实际位置是否一致 —— 不一致说明
//     Payload 解析与 Header 声明的位长不匹配，将整段跳过并标错。
// =============================================================================

#include "Iris/ReplicationSystem/NetBlob/NetRPC.h"

#include "HAL/IConsoleManager.h"
#include "Iris/Core/BitTwiddling.h"
#include "Net/Core/Trace/NetDebugName.h"
#include "Net/Core/Trace/NetTrace.h"
#include "Net/Core/Misc/NetContext.h"
#include "Iris/ReplicationSystem/NetBlob/NetRPCHandler.h"
#include "Iris/ReplicationSystem/ObjectReferenceCache.h"
#include "Iris/ReplicationSystem/ObjectReplicationBridge.h"
#include "Iris/Serialization/NetReferenceCollector.h"
#include "Iris/ReplicationSystem/ReplicationOperations.h"
#include "Iris/ReplicationSystem/ReplicationOperationsInternal.h"
#include "Iris/ReplicationSystem/ReplicationProtocol.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"
#include "Iris/ReplicationSystem/ReplicationSystemInternal.h"
#include "Iris/Serialization/InternalNetSerializationContext.h"
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Serialization/ObjectNetSerializer.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Core/IrisLog.h"
#include "Iris/Core/IrisProfiler.h"
#include "Containers/ArrayView.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "UObject/CoreNetContext.h"

DEFINE_LOG_CATEGORY_STATIC(LogIrisRpc, Log, All);

namespace UE::Net::Private
{

// 中文：调试 CVar —— 在 dedicated server 上检测 RPC 状态在 Quantize 之后、Serialize
// 之前是否被外部代码"踩坏"。开启后 QuantizedBlobState 用 Protectable 内存分配；
// Quantize 完成后立即 Protect()，任何写入都会立即触发 access violation 便于定位。
static bool bEnableRPCServerStateStompDetection = false;
static FAutoConsoleVariableRef CVarEnableRPCStateStompDetection(
	TEXT("net.Iris.RPC.ServerStompDetectionEnabled"),
	bEnableRPCServerStateStompDetection,
	TEXT("If enabled we can detect if RPC state is stomped between quantization and serialization on a dedicated server.")
);

// ---- 内部辅助函数前置声明（实现在文件末尾） --------------------------------
// 中文：在 ReplicationProtocol 中线性查找 UFunction → FFunctionLocator + 描述符。
//       O(N) 即可，因为单个对象的描述符与函数数量都很小。

static bool NetRPC_GetFunctionLocator(const UReplicationSystem* ReplicationSystem, const FNetObjectReference& ObjectReference, const UFunction* Function, FNetRPC::FFunctionLocator& OutFunctionLocator, const FReplicationStateMemberFunctionDescriptor*& OutFunctionDescriptor);

// 中文：FNetObjectReference → UObject*（统一入口；带 SubObject 优先解 SubObject）。
static UObject* NetRPC_GetObject(FNetSerializationContext& Context, const FNetObjectReference& ObjectReference);
static UObject* NetRPC_GetObject(FNetSerializationContext& Context, const FNetObjectReference& RootObjectReference, const FNetObjectReference& SubObjectReference);
// 中文：拿 SubObject 的根对象（用于 ForwardNetRPCCallDelegate 区分 root/sub）。
static UObject* NetRPC_GetRootObject(FNetSerializationContext& Context, const FNetObjectReference& ObjectReference);

// 中文：接收侧"对象 + 函数描述符"一站式解析（含错误处理）。
static bool NetRPC_GetFunctionAndObject(FNetSerializationContext& Context, const FNetObjectReference& RootObjectReference, const FNetObjectReference& SubObjectReferece, const FNetRPC::FFunctionLocator& FunctionLocator, const FReplicationStateMemberFunctionDescriptor*& OutFunctionDescriptor, TWeakObjectPtr<UObject>& OutObject);

// 中文：调试用——把 (Descriptor, Function) 索引映射回函数名 / 引用名。
static FString NetRPC_GetDebugFunctionName(const UReplicationSystem* ReplicationSystem, const FNetObjectReference& ObjectReference, const FNetRPC::FFunctionLocator& FunctionLocator);
static FString NetRPC_GetDebugObjectRefName(FNetSerializationContext& Context, const FNetObjectReference& ObjectReference, UObject* ObjectPtr = nullptr);

// 中文：错误名常量（NetTrace / SetError 时使用，便于上层定位）。
static const FName NetError_InvalidNetObjectReference("Invalid NetObjectRefererence");
static const FName NetError_UnknownFunction("Unknown RPC");
static const FName NetError_FunctionCallNotAllowed("RPC call not allowed");

FNetRPC::FNetRPC(const FNetBlobCreationInfo& CreationInfo)
: FNetObjectAttachment(CreationInfo)
, FunctionLocator({})
, Function(nullptr)
{
}

FNetRPC::~FNetRPC()
{
}

// 中文：返回 Create() 时收集到的可导出对象引用列表（参数中含的 ref）。
TArrayView<const FNetObjectReference> FNetRPC::GetNetObjectReferenceExports() const
{
	return ReferencesToExport.IsValid() ? MakeArrayView(*ReferencesToExport) : MakeArrayView<const FNetObjectReference>(nullptr, 0);
}

// 中文：SerializeWithObject —— 上层连接已知 root RefHandle，仅写 SubObject 引用 +
// Locator + Payload。Header 占位，全部写完再回填准确位长。任何错误立刻返回，
// 由上层回滚整段。
void FNetRPC::SerializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle) const
{
	UE_NET_TRACE_DYNAMIC_NAME_SCOPE(BlobDescriptor->DebugName, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Verbose);

	FNetBitStreamWriter& Writer = *Context.GetBitStreamWriter();

	const uint32 HeaderPos = Writer.GetPosBits();
	InternalSerializeHeader(Context);
	if (Context.HasError())
	{
		return;
	}

	SerializeFunctionLocator(Context);
	if (Context.HasError())
	{
		return;
	}

	InternalSerializeSubObjectReference(Context, RefHandle);
	if (Context.HasError())
	{
		return;
	}

	InternalSerializeBlob(Context);
	if (Context.HasError())
	{
		return;
	}

	if (!Writer.IsOverflown())
	{
		// Re-serialize the final size value in the header
		// 中文：未溢出才回填实际大小；溢出时上层会回滚整段写入。
		const uint32 RPCSize = Writer.GetPosBits() - HeaderPos;
		FNetBitStreamWriteScope WriteScope(Writer, HeaderPos);
		InternalSerializeHeader(Context, RPCSize);
	}
}

void FNetRPC::DeserializeWithObject(FNetSerializationContext& Context, FNetRefHandle RefHandle)
{
	// We don't know the function name until much later.
	// 中文：解析 Locator → 拿到 Function 的 DebugName 后再回填 trace scope 名字。
	UE_NET_TRACE_NAMED_SCOPE(TraceScope, RPC, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

	// Store the next valid position after the NetRPC data.
	// 中文：HeaderPos + Header 中读出的位长 = 整个 RPC 的尾部位置。失败时用于 Seek 跳过整段。
	const uint32 HeaderPos = Context.GetBitStreamReader()->GetPosBits();
	const uint32 PostNetRPCPos = HeaderPos + InternalDeserializeHeader(Context);
	if (Context.HasErrorOrOverflow())
	{
		return;
	}

	DeserializeFunctionLocator(Context);
	if (Context.HasErrorOrOverflow())
	{
		return;
	}

	InternalDeserializeSubObjectReference(Context, RefHandle);
	if (Context.HasErrorOrOverflow())
	{
		return;
	}

	bool bResolveSucceeded = ResolveFunctionAndObject(Context);
	if (Context.HasErrorOrOverflow())
	{
		return;
	}
	
	if (!bResolveSucceeded)
	{
		UE_LOG(LogIrisRpc, Error, TEXT("DeserializeWithObject::Skipping RPC due missing object or function."));

		// Stop deserializing and seek past the entire payload if the resolve failed
		// 中文：跳过整个 RPC payload，避免错位影响后续 blob。
		Context.GetBitStreamReader()->Seek(PostNetRPCPos);
		return;
	}

	UE_NET_TRACE_SET_SCOPE_NAME(TraceScope, BlobDescriptor->DebugName);
	InternalDeserializeBlob(Context);
	
	if (!Context.HasErrorOrOverflow())
	{
		// Just because the serialization didn't detect an error doesn't mean everything is ok. Validate stream position.
		// 中文：双重保险——Payload 实际读出长度必须等于 Header 声明的长度，否则
		// 协议结构错位（很可能 server/client 协议版本不一致），整段跳过并标错。
		if (PostNetRPCPos != Context.GetBitStreamReader()->GetPosBits())
		{
			UE_LOG(LogIrisRpc, Error, TEXT("Bitstream mismatch while deserializing function %s. Actual stream position: %u Expected stream position: %u"), ToCStr(BlobDescriptor->DebugName), Context.GetBitStreamReader()->GetPosBits(), PostNetRPCPos);
			ensureMsgf(PostNetRPCPos == Context.GetBitStreamReader()->GetPosBits(), TEXT("Bitstream mismatch while deserializing function %s. Actual stream position: %u Expected stream position: %u"), ToCStr(BlobDescriptor->DebugName), Context.GetBitStreamReader()->GetPosBits(), PostNetRPCPos);
			Context.GetBitStreamReader()->Seek(PostNetRPCPos);
			Context.SetError(GNetError_BitStreamError);
			// Make sure the RPC won't be exeuted regardless of how errors are handled.
			// 中文：清掉 Function 防止 CallFunction 仍意外执行已无效的内容。
			Function = nullptr;
		}
	}
}

void FNetRPC::Serialize(FNetSerializationContext& Context) const
{
	// 中文：Serialize 全量版本——上层不知道 RefHandle，需要写完整 ObjectRef。
	UE_NET_TRACE_DYNAMIC_NAME_SCOPE(BlobDescriptor->DebugName, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

	FNetBitStreamWriter& Writer = *Context.GetBitStreamWriter();
	
	const uint32 HeaderPos = Writer.GetPosBits();
	InternalSerializeHeader(Context);
	if (Context.HasError())
	{
		return;
	}

	InternalSerializeObjectReference(Context);
	if (Context.HasError())
	{
		return;
	}

	SerializeFunctionLocator(Context);
	if (Context.HasError())
	{
		return;
	}

	InternalSerializeBlob(Context);
	if (Context.HasError())
	{
		return;
	}

	if (!Writer.IsOverflown())
	{
		// Re-serialize the final size value in the header
		const uint32 RPCSize = Writer.GetPosBits() - HeaderPos;
		FNetBitStreamWriteScope WriteScope(Writer, HeaderPos);
		InternalSerializeHeader(Context, RPCSize);
	}
}

void FNetRPC::Deserialize(FNetSerializationContext& Context)
{
	// 中文：Deserialize 全量版本——读完整 ObjectRef → Locator → 解析 → Payload。
	// 任何阶段出错或对象/函数找不到都把读指针 Seek 到 PostNetRPCPos 跳过整段。
	UE_NET_TRACE_NAMED_SCOPE(TraceScope, RPC, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

	// Store the next valid bit position after the NetRPC data.
	const uint32 HeaderPos = Context.GetBitStreamReader()->GetPosBits();
	const uint32 PostNetRPCPos = HeaderPos + InternalDeserializeHeader(Context);
	if (Context.HasErrorOrOverflow())
	{
		return;
	}

	InternalDeserializeObjectReference(Context);
	if (Context.HasErrorOrOverflow())
	{
		return;
	}

	DeserializeFunctionLocator(Context);
	if (Context.HasErrorOrOverflow())
	{
		return;
	}
	
	bool bResolveSucceeded = ResolveFunctionAndObject(Context);
	if (Context.HasErrorOrOverflow())
	{
		return;
	}

	if (!bResolveSucceeded)
	{
		UE_LOG(LogIrisRpc, Error, TEXT("DeserializeWithObject::Skipping RPC due missing object or function."));
		// Stop deserializing and seek past the entire payload when the Resolve fails
		Context.GetBitStreamReader()->Seek(PostNetRPCPos);
		return;
	}
	
	UE_NET_TRACE_SET_SCOPE_NAME(TraceScope, BlobDescriptor->DebugName);

	InternalDeserializeBlob(Context);
	
	if (!Context.HasErrorOrOverflow())
	{
		if (PostNetRPCPos != Context.GetBitStreamReader()->GetPosBits())
		{
			UE_LOG(LogIrisRpc, Error, TEXT("DeserializeWithObject::RPC %s did not read expected number of bits ErrorContext: %s"), ToCStr(BlobDescriptor->DebugName), *Context.PrintReadJournal())
			ensure(PostNetRPCPos == Context.GetBitStreamReader()->GetPosBits());
		}
	}
}

void FNetRPC::InternalSerializeHeader(FNetSerializationContext& Context, int32 PayloadSize /*= -1*/) const
{
	// 中文：Header 写入——分两阶段使用：
	//   * PayloadSize=-1：占位写 0（保留 24bit 槽位）；
	//   * 真实大小：用 FNetBitStreamWriteScope 切回原 Header 位置回填。
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	// Pre-serialize the header before we know the final RPC payload size
	if (PayloadSize == -1)
	{
		UE_NET_TRACE_SCOPE(RPCSize, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
		Writer->WriteBits(0u, HeaderSizeBitCount);
	}
	// Write the final size once available
	else
	{
		checkf(PayloadSize > 0 && PayloadSize <= MaxRpcSizeInBits, TEXT("FNetRPC can only support a payload size of %u bits (RPC %s cost %u bits)"), MaxRpcSizeInBits, ToCStr(Function->GetName()), PayloadSize);
		Writer->WriteBits(PayloadSize, HeaderSizeBitCount);
	}
}

uint32 FNetRPC::InternalDeserializeHeader(FNetSerializationContext& Context)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	UE_NET_TRACE_SCOPE(RPCSize, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

	return Reader->ReadBits(HeaderSizeBitCount);
}

void FNetRPC::SerializeFunctionLocator(FNetSerializationContext& Context) const
{
	// 中文：FunctionLocator 变长写——按两索引中的最大值算 nibble 数（4-bit 一组）：
	//   先写 2bit 表示 (nibble 数 - 1)，随后两索引各占 nibble*4 位。
	//   绝大多数对象 Locator ≤ 15，nibble=1 即可（总 2+4+4=10bit）。
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	UE_NET_TRACE_SCOPE(FunctionLocator, *Writer, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

	const uint16 MaxValue = FMath::Max(FunctionLocator.DescriptorIndex, FunctionLocator.FunctionIndex);
	const uint32 NibbleCount = (GetBitsNeeded(MaxValue | uint16(1)) + 3) >> 2U;

	Writer->WriteBits(NibbleCount - 1U, 2U);
	Writer->WriteBits(FunctionLocator.DescriptorIndex, NibbleCount*4U);
	Writer->WriteBits(FunctionLocator.FunctionIndex, NibbleCount*4U);
}

void FNetRPC::DeserializeFunctionLocator(FNetSerializationContext& Context)
{
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	UE_NET_TRACE_SCOPE(FunctionLocator, *Reader, Context.GetTraceCollector(), ENetTraceVerbosity::Trace);

	const uint32 NibbleCount = Reader->ReadBits(2U) + 1U;
	FunctionLocator.DescriptorIndex = static_cast<uint16>(Reader->ReadBits(NibbleCount*4U));
	FunctionLocator.FunctionIndex = static_cast<uint16>(Reader->ReadBits(NibbleCount*4U));
}

void FNetRPC::InternalSerializeObjectReference(FNetSerializationContext& Context) const
{
	UE_NET_TRACE_SCOPE(TargetObject, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
	SerializeObjectReference(Context);
}

void FNetRPC::InternalDeserializeObjectReference(FNetSerializationContext& Context)
{
	UE_NET_TRACE_SCOPE(TargetObject, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
	DeserializeObjectReference(Context);
}

void FNetRPC::InternalSerializeSubObjectReference(FNetSerializationContext& Context, FNetRefHandle RefHandle) const
{
	UE_NET_TRACE_SCOPE(TargetObject, *(Context.GetBitStreamWriter()), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
	SerializeSubObjectReference(Context, RefHandle);
}

void FNetRPC::InternalDeserializeSubObjectReference(FNetSerializationContext& Context, FNetRefHandle RefHandle)
{
	UE_NET_TRACE_SCOPE(TargetObject, *(Context.GetBitStreamReader()), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
	DeserializeSubObjectReference(Context, RefHandle);
}

void FNetRPC::InternalSerializeBlob(FNetSerializationContext& Context) const
{
	UE_NET_TRACE_SCOPE(FunctionParams, *Context.GetBitStreamWriter(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
	SerializeBlob(Context);
}

void FNetRPC::InternalDeserializeBlob(FNetSerializationContext& Context)
{
	UE_NET_TRACE_SCOPE(FunctionParams, *Context.GetBitStreamReader(), Context.GetTraceCollector(), ENetTraceVerbosity::Trace);
	DeserializeBlob(Context);
}

bool FNetRPC::ResolveFunctionAndObject(FNetSerializationContext& Context)
{
	// 中文：接收侧综合解析——成功返回 true 后会设置 Function、ObjectPtr、
	// BlobDescriptor、QuantizedBlobState；同时根据 UFunction 标志补 ENetBlobFlags。
	// 失败返回 false，调用方会跳过整段 RPC。
	// At this point we need a valid handle and FunctionLocator
	if (!NetObjectReference.GetRefHandle().IsValid())
	{
		// This can occur if sending side had queued up rpcs to object being invalidated
		// 中文：发送侧入队后该对象失效（Actor/组件销毁），ref 已被置无效。
		return false;
	}

	const FReplicationStateMemberFunctionDescriptor* FunctionDescriptor = nullptr;
	if (!NetRPC_GetFunctionAndObject(Context, NetObjectReference, TargetObjectReference, FunctionLocator, FunctionDescriptor, ObjectPtr))
	{
		return false;
	}

	Function = FunctionDescriptor->Function;

	// Patch up NetBlobFlags based on function flags.
	// 中文：根据 UFunction 标志位补 ENetBlobFlags，让接收端逻辑保持与发送端一致。
	if (Function)
	{
		if ((Function->FunctionFlags & FUNC_NetReliable) != 0)
		{
			CreationInfo.Flags |= ENetBlobFlags::Reliable;
		}

		// The sending side will set Ordered on unicast/reliable RPCs so we're restoring that flag. Unicast RPCs are ordered with respect to other reliable and unicast RPCs whereas multicast RPCs are not.
		// 中文：unicast RPC 与同对象其他 reliable/unicast RPC 严格保序；multicast 不要求。
		if ((Function->FunctionFlags & FUNC_NetMulticast) == 0)
		{
			CreationInfo.Flags |= UE::Net::ENetBlobFlags::Ordered;
		}
	}

	// Set the BlobDescriptor even if it has zero size so that we can trace with a meaningful name.
	// 中文：即使无参数（InternalSize=0），也设置 Descriptor 用于 trace 命名。
	BlobDescriptor = FunctionDescriptor->Descriptor;

	// 中文：参数描述符 InternalSize=0 → 无参，跳过 Quantize 缓冲分配。
	if (FunctionDescriptor->Descriptor->InternalSize)
	{
		QuantizedBlobState = FQuantizedBlobState(FunctionDescriptor->Descriptor->InternalSize, FunctionDescriptor->Descriptor->InternalAlignment);
	}

	return true;
}

FNetRPC* FNetRPC::Create(UReplicationSystem* ReplicationSystem, const FNetBlobCreationInfo& CreationInfo, const FNetObjectReference& ObjectReference, const UFunction* Function, const void* FunctionParameters)
{
	// 中文：发送侧工厂——把 UFunction* + 参数 buffer 组装成 FNetRPC。
	//   1) Walk to topmost SuperFunction —— 蓝图/原生派生 RPC 共享顶层签名，
	//      发送时使用顶层 UFunction 才能在不同实例上 locator 索引一致。
	//   2) NetRPC_GetFunctionLocator 在对象 ReplicationProtocol 中线性查找。
	//   3) Quantize 函数参数（写入 QuantizedBlobState 缓冲）。
	//   4) FNetReferenceCollector 收集 payload 中"可导出"的对象引用。
	while (const UFunction* SuperFunction = Function->GetSuperFunction())
	{
		Function = SuperFunction;
	};

	FFunctionLocator FunctionLocator = {};
	const FReplicationStateMemberFunctionDescriptor* FunctionDescriptor = nullptr;
	if (!NetRPC_GetFunctionLocator(ReplicationSystem, ObjectReference, Function, FunctionLocator, FunctionDescriptor))
	{
		return nullptr;
	}

	const FReplicationStateDescriptor* BlobDescriptor = FunctionDescriptor->Descriptor;
	FQuantizedBlobState QuantizedBlobState;

	// Don't spend CPU cycles on quantizing zero parameters
	if (BlobDescriptor != nullptr && BlobDescriptor->InternalSize)
	{
#if WITH_SERVER_CODE
		const FQuantizedBlobState::EMemoryAllocationFlags MemoryAllocationFlags = bEnableRPCServerStateStompDetection ? FQuantizedBlobState::EMemoryAllocationFlags::Protectable : FQuantizedBlobState::EMemoryAllocationFlags::None;
#else
		const FQuantizedBlobState::EMemoryAllocationFlags MemoryAllocationFlags = FQuantizedBlobState::EMemoryAllocationFlags::None;
#endif
		QuantizedBlobState = FQuantizedBlobState(FunctionDescriptor->Descriptor->InternalSize, FunctionDescriptor->Descriptor->InternalAlignment, MemoryAllocationFlags);

		// Setup Context
		FNetSerializationContext Context;
		FInternalNetSerializationContext InternalContext(ReplicationSystem);
		
		Context.SetInternalContext(&InternalContext);

		// Quantize the function parameters
		// 中文：将外部表示（External，UFunction 参数）→ 内部表示（Internal，紧凑 quantized）。
		FReplicationStateOperations::Quantize(Context, QuantizedBlobState.GetStateBuffer(), static_cast<const uint8*>(FunctionParameters), BlobDescriptor);

#if WITH_SERVER_CODE
		if (MemoryAllocationFlags == FQuantizedBlobState::EMemoryAllocationFlags::Protectable)
		{
			// 中文：保护内存，Quantize 之后任何写入都将立即触发 access violation 便于排错。
			QuantizedBlobState.Protect();
		}
#endif
	}

	FNetRPC* NetRPC = new FNetRPC(CreationInfo);
	NetRPC->SetFunctionLocator(FunctionLocator);
	NetRPC->Function = Function;
	if (BlobDescriptor != nullptr)
	{
		if (BlobDescriptor->HasObjectReference())
		{
			// Collect all references and add them to potential exports
			// 中文：参数中可能含需"导出"的对象引用，发送前先收集。仅收集"可导出"
			// 引用——内联引用、不可导出的引用不进表。
			using namespace UE::Net::Private;
			{
				FNetSerializationContext LocalContext;
				FNetReferenceCollector Collector(ENetReferenceCollectorTraits::OnlyCollectReferencesThatCanBeExported);
				const FNetSerializerChangeMaskParam InitStateChangeMaskInfo = { 0 };
				FReplicationStateOperationsInternal::CollectReferences(LocalContext, Collector, InitStateChangeMaskInfo, QuantizedBlobState.GetStateBuffer(), BlobDescriptor);

				if (Collector.GetCollectedReferences().Num())
				{
					NetRPC->ReferencesToExport = MakeUnique<FNetRPCExportsArray>();
					for (const FNetReferenceCollector::FReferenceInfo& Info : MakeArrayView(Collector.GetCollectedReferences()))
					{
						NetRPC->ReferencesToExport->AddUnique(Info.Reference);
					}

					NetRPC->CreationInfo.Flags |= ENetBlobFlags::HasExports;
				}
			}
		}

		NetRPC->SetState(BlobDescriptor, MoveTemp(QuantizedBlobState));
	}

	return NetRPC;
}

void FNetRPC::CallFunction(FNetRPCCallContext& CallContext)
{
	// 中文：接收侧执行 RPC 的核心流程。
	//   1) 解析 Object（弱引用→ObjectReferenceCache 二次解析）；
	//   2) 校验：函数有效、对象仍在复制（HasInstanceProtocol）、是 FUNC_Net 函数、
	//      Server/Client 方向匹配；服务器还要校验 connection ownership；
	//   3) 在栈上分配 FunctionParameters → InitializeValue 复杂参数 → Dequantize；
	//   4) BP override：用 Object->FindFunction 拿派生版 UFunction；
	//   5) ForwardNetRPCCallDelegate 多播 → ProcessEvent；
	//   6) 销毁参数。
#if UE_NET_IRIS_CSV_STATS
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(HandleRPC);
#endif

	FNetSerializationContext& Context = CallContext.GetNetSerializationContext();

	const UReplicationSystem* ReplicationSystem = Context.GetInternalContext()->ReplicationSystem;

	// Check whether we are ok with calling the function
	UObject* Object = ObjectPtr.Get();

	if (Object == nullptr)
	{
		Object = NetRPC_GetObject(Context, NetObjectReference, TargetObjectReference);
	}

	if (Object == nullptr || Function == nullptr)
	{
		if (!TargetObjectReference.IsValid())
		{
			UE_LOG(LogIrisRpc, Error, TEXT("Rejected RPC (%u:%u) %s due to missing object or function for object: %s."),
				FunctionLocator.DescriptorIndex, FunctionLocator.FunctionIndex,
				(Function ? ToCStr(Function->GetName()) : TEXT("\'unknown\'")),
				*NetRPC_GetDebugObjectRefName(Context, NetObjectReference, Object));
		}
		else
		{
			UE_LOG(LogIrisRpc, Error, TEXT("Rejected RPC (%u:%u) %s due to missing object or function for subobject: %s of rootobject: %s"),
				FunctionLocator.DescriptorIndex, FunctionLocator.FunctionIndex,
				(Function ? ToCStr(Function->GetName()) : TEXT("\'unknown\'")),
				*NetRPC_GetDebugObjectRefName(Context, TargetObjectReference, Object), *NetRPC_GetDebugObjectRefName(Context, NetObjectReference));
		}
		return;
	}

	// Make sure the root object has an instance protocol
	// 中文：root object 必须仍处于复制状态（HasInstanceProtocol）。否则 RPC 是"残
	// 留消息"——对象在本端已停止复制，丢弃避免触发死实例上的代码。
	{
		const FNetRefHandleManager& NetRefHandleManager = ReplicationSystem->GetReplicationSystemInternal()->GetNetRefHandleManager();
		const FNetRefHandle RootObjectNetRefHandle = NetObjectReference.GetRefHandle();
		const FInternalNetRefIndex InternalIndex = NetRefHandleManager.GetInternalIndex(RootObjectNetRefHandle);

		ensureMsgf(InternalIndex != FNetRefHandleManager::InvalidInternalIndex, TEXT("Processing NetRPC for an object before it's internal index was assigned. NetRefHandle: %s | Instance: %s"),
			*RootObjectNetRefHandle.ToString(), *GetNameSafe(Object));

		// If the object stopped replicating locally, discard all received RPCs so they don't get executed.
		if (!NetRefHandleManager.HasInstanceProtocol(InternalIndex))
		{
			UE_LOG(LogIrisRpc, Warning, TEXT("Rejected RPC: %s due to object: %s having stopped replication"),
				*(Function ? Function->GetName() : NetRPC_GetDebugFunctionName(ReplicationSystem, NetObjectReference, FunctionLocator)),
				*NetRPC_GetDebugObjectRefName(Context, NetObjectReference, Object));

			return;
		}
	}

	if ((Function->FunctionFlags & FUNC_Net) == 0)
	{
		// 中文：必须是 Net 函数（FUNC_Net），否则视为协议非法，立刻 SetError。
		UE_LOG(LogIrisRpc, Error, TEXT("Rejected %s function %s due to it not being a Net function. %s : %s"), (Function->FunctionFlags & FUNC_NetReliable ? TEXT("reliable") : TEXT("unreliable")), ToCStr(Function->GetName()), *NetObjectReference.ToString(), *Object->GetFullName());
		Context.SetError(NetError_FunctionCallNotAllowed);
		return;
	}

	const bool bIsServer = ReplicationSystem->IsServer();
	if (bIsServer)
	{
		if ((Function->FunctionFlags & (FUNC_NetClient | FUNC_NetMulticast)) != 0)
		{
			// 中文：服务器不应该收到 client/multicast 类型 RPC（multicast 由服务器发起）。
			UE_LOG(LogIrisRpc, Error, TEXT("Rejected %s client RPC function %s due to this being the server. %s : %s"), (Function->FunctionFlags & FUNC_NetReliable ? TEXT("reliable") : TEXT("unreliable")), ToCStr(Function->GetName()), *NetObjectReference.ToString(), *Object->GetFullName());
			Context.SetError(NetError_FunctionCallNotAllowed);
			return;
		}

		if (!IsServerAllowedToExecuteRPC(Context))
		{
			// 中文：服务器侧 ownership 校验——只有"持有该对象的连接"才能在该对象
			// 上发起 ServerRPC。否则视为试图代他人发 RPC，直接丢弃。
			UE_LOG(LogIrisRpc, Verbose, TEXT("Rejected %s RPC function %s due to target not being owned by the connection. %s : %s"), (Function->FunctionFlags & FUNC_NetReliable ? TEXT("reliable") : TEXT("unreliable")), ToCStr(Function->GetName()), *NetObjectReference.ToString(), *Object->GetFullName()); 
			return;
		}

	}
	else
	{
		if ((Function->FunctionFlags & FUNC_NetServer) != 0)
		{
			// 中文：客户端不应该收到 server-only RPC。
			UE_LOG(LogIrisRpc, Error, TEXT("Rejected %s server RPC function %s due to this being the client. %s : %s"), (Function->FunctionFlags & FUNC_NetReliable ? TEXT("reliable") : TEXT("unreliable")), ToCStr(Function->GetName()), *NetObjectReference.ToString(), *Object->GetFullName());
			return;
		}
	}

	if (UE_LOG_ACTIVE(LogIrisRpc, Verbose))
	{
		bool bLogRpc = true;

		// Suppress spammy engine RPCs. This could be made a configurable list in the future.
		const FString& FunctionName = Function->GetName();
		if (   FunctionName.Contains(TEXT("ServerUpdateCamera"))
			|| FunctionName.Contains(TEXT("ClientAckGoodMove"))
			|| FunctionName.Contains(TEXT("ServerMove")))
		{
			bLogRpc = false;
		}
		
		UE_CLOG(bLogRpc, LogIrisRpc, Verbose, TEXT("Calling %hs RPC function %s for %s : %s"), (Function->FunctionFlags & FUNC_NetReliable ? "reliable" : "unreliable"), ToCStr(Function->GetName()), *NetObjectReference.ToString(), *Object->GetFullName());
	}

	// Setup function parameters
	// 中文：在栈上分配参数 buffer（FMemory_Alloca），按 BlobDescriptor 描述把
	// quantized → external，然后 ProcessEvent 即可。空 ParmsSize 直接跳过。
	uint8* FunctionParameters = nullptr;
	if (Function->ParmsSize > 0)
	{
		check(BlobDescriptor.IsValid());

		FunctionParameters = static_cast<uint8*>(FMemory_Alloca(Function->ParmsSize));
		FMemory::Memzero(FunctionParameters, Function->ParmsSize);
		if (!EnumHasAnyFlags(BlobDescriptor->Traits, EReplicationStateTraits::IsSourceTriviallyConstructible))
		{
			const FReplicationStateMemberDescriptor* MemberDescriptors = BlobDescriptor->MemberDescriptors;
			const FReplicationStateMemberPropertyDescriptor* MemberPropertyDescriptors = BlobDescriptor->MemberPropertyDescriptors;
			const FProperty** MemberProperties = BlobDescriptor->MemberProperties;
			for (uint32 MemberIt = 0, MemberCount = BlobDescriptor->MemberCount; MemberIt < MemberCount; ++MemberIt)
			{
				const FReplicationStateMemberPropertyDescriptor& MemberPropertyDescriptor = MemberPropertyDescriptors[MemberIt];
		
				// InitializeValue operates on the entire static array so make sure not to call it other than for the first element.
				if (MemberPropertyDescriptor.ArrayIndex == 0)
				{
					const FProperty* Property = MemberProperties[MemberIt];
					// We will dequantize all parameters to this buffer so we don't care if zero is the wrong value as it will be overwritten anyway.
					// So we only need to initialize the value if it's complex, such as having virtual functions.
					if (!Property->HasAnyPropertyFlags(CPF_ZeroConstructor | CPF_IsPlainOldData))
					{
						const FReplicationStateMemberDescriptor& MemberDescriptor = MemberDescriptors[MemberIt];
						Property->InitializeValue(FunctionParameters + MemberDescriptor.ExternalMemberOffset);
					}
				}
			}
		}
		
		FReplicationStateOperations::Dequantize(Context, FunctionParameters, QuantizedBlobState.GetStateBuffer(), BlobDescriptor);
	}

	// Since the replicated FunctionLocator references the SuperFunction, we must lookup the actual function to call from the target object to properly support BP derived functions.
	// See: JIRA: UE-220400
	// 中文：locator 指向 SuperFunction，但 BP 派生类可能 override；必须从目标对象
	// 拿派生版本的 UFunction* 才能正确派发 ProcessEvent。
	const UFunction* ActualFunction = Object->FindFunction(Function->GetFName());
	if (ActualFunction == nullptr)
	{
		ActualFunction = Function;
	}

	// Forward function
	// 中文：ForwardNetRPCCallDelegate Hook —— ProcessEvent 之前广播给所有订阅方
	// （回放、anti-cheat、调试等），可记录或转发。
	if (const FForwardNetRPCCallMulticastDelegate& Delegate = CallContext.GetForwardNetRPCCallDelegate(); Delegate.IsBound())
	{
		UObject* RootObject = NetRPC_GetRootObject(Context, NetObjectReference);
		UObject* SubObject = (Object != RootObject ? Object : static_cast<UObject*>(nullptr));
		Delegate.Broadcast(RootObject, SubObject, const_cast<UFunction*>(ActualFunction), FunctionParameters);
	}

	// Call function
	{
		// 中文：FScopedNetContextRPC / FScopedRemoteRPCMode 通知 UFunction 系统
		// "正在处理一个远端 RPC"——某些函数实现里据此判断来源（如 ServerRPC 合法性）。
#if IRIS_CLIENT_PROFILER_ENABLE
		UE::Net::FClientProfiler::RecordRPC(ActualFunction->GetFName());
#endif

		UE::Net::FScopedNetContextRPC CallingRPC;
		UE::Net::Private::FScopedRemoteRPCMode ReceivingRemoteRPC(ActualFunction, UE::Net::Private::ERemoteFunctionMode::Receiving);
		Object->ProcessEvent(const_cast<UFunction*>(ActualFunction), FunctionParameters);
	}

	// Deinitialize function parameters
	if (FunctionParameters != nullptr)
	{
		// 中文：与 InitializeValue 对偶——非 trivially destructible 的参数（含 FString /
		// 智能指针等）需要逐个 DestroyValue 释放资源。
		if (!EnumHasAnyFlags(BlobDescriptor->Traits, EReplicationStateTraits::IsSourceTriviallyDestructible))
		{
			for (TFieldIterator<FProperty> ParamIt(Function); ParamIt && (ParamIt->PropertyFlags & (CPF_Parm | CPF_ReturnParm)) == CPF_Parm; ++ParamIt)
			{
				ParamIt->DestroyValue_InContainer(FunctionParameters);
			}
		}
	}
}

bool FNetRPC::IsServerAllowedToExecuteRPC(FNetSerializationContext& Context) const
{
	// 中文：服务器端 ownership 校验。OwningConnectionId 在 SetOwningNetConnection
	// 时被记录；只有该 connection 才能在该对象上发起 ServerRPC。否则视为试图代他
	// 人发 RPC，丢弃。
	const FNetRefHandle Handle = NetObjectReference.GetRefHandle();
	const UReplicationSystem* ReplicationSystem = Context.GetInternalContext()->ReplicationSystem;

	const uint32 OwningConnectionId = ReplicationSystem->GetOwningNetConnection(Handle);
	const uint32 ExecutingConnectionId = Context.GetLocalConnectionId();
	const bool bTargetIsOwnedByConnection = OwningConnectionId == ExecutingConnectionId;
	return bTargetIsOwnedByConnection;
}

static bool NetRPC_GetFunctionLocator(const UReplicationSystem* ReplicationSystem, const FNetObjectReference& ObjectReference, const UFunction* Function, FNetRPC::FFunctionLocator& OutFunctionLocator, const FReplicationStateMemberFunctionDescriptor*& OutFunctionDescriptor)
{
	// 中文：在对象的 ReplicationProtocol 中线性搜索匹配的 UFunction。
	// 一个对象的描述符与函数数都很少，O(N*M) 完全够用。
	const UObjectReplicationBridge* Bridge = Cast<UObjectReplicationBridge>(ReplicationSystem->GetReplicationBridge());
	if (!ensure(Bridge != nullptr))
	{
		return false;
	}

	const FReplicationProtocol* Protocol = ReplicationSystem->GetReplicationProtocol(ObjectReference.GetRefHandle());
	if (!ensure(Protocol != nullptr))
	{
		return false;
	}

	for (const FReplicationStateDescriptor*& Descriptor : MakeArrayView(Protocol->ReplicationStateDescriptors, Protocol->ReplicationStateCount))
	{
		for (const FReplicationStateMemberFunctionDescriptor& FunctionDescriptor : MakeArrayView(Descriptor->MemberFunctionDescriptors, Descriptor->FunctionCount))
		{
			if (FunctionDescriptor.Function == Function)
			{
				OutFunctionLocator.DescriptorIndex = static_cast<uint16>(&Descriptor - Protocol->ReplicationStateDescriptors);
				OutFunctionLocator.FunctionIndex = static_cast<uint16>(&FunctionDescriptor - Descriptor->MemberFunctionDescriptors);
				OutFunctionDescriptor = &FunctionDescriptor;
				return true;
			}
		}
	}

	return false;
}

static UObject* NetRPC_GetObject(FNetSerializationContext& Context, const FNetObjectReference& RootObjectReference, const FNetObjectReference& SubObjectReference)
{
	return NetRPC_GetObject(Context, SubObjectReference.IsValid() ? SubObjectReference : RootObjectReference);
}

static UObject* NetRPC_GetObject(FNetSerializationContext& Context, const FNetObjectReference& ObjectReference)
{
	FInternalNetSerializationContext* InternalContext = Context.GetInternalContext();
	return InternalContext->ObjectReferenceCache->ResolveObjectReference(ObjectReference, InternalContext->ResolveContext);
}

static UObject* NetRPC_GetRootObject(FNetSerializationContext& Context, const FNetObjectReference& ObjectReference)
{
	// 中文：得到目标对象所属的"根对象"（沿 SubObjectRootIndex 反查），用于 Forward
	// 委托区分 root/sub 参数。
	FInternalNetSerializationContext* InternalContext = Context.GetInternalContext();
	const FNetRefHandleManager& NetRefHandleManager = InternalContext->ReplicationSystem->GetReplicationSystemInternal()->GetNetRefHandleManager();
	
	FNetRefHandle OwnerRefHandle = ObjectReference.GetRefHandle();
	const FInternalNetRefIndex InternalIndex = NetRefHandleManager.GetInternalIndex(OwnerRefHandle);
	if (!ensureMsgf(InternalIndex != FNetRefHandleManager::InvalidInternalIndex, TEXT("Unable to find InternalIndex for object reference %s"), ToCStr(ObjectReference.ToString())))
	{
		return nullptr;
	}

	const FNetRefHandleManager::FReplicatedObjectData& ObjectData = NetRefHandleManager.GetReplicatedObjectDataNoCheck(InternalIndex);
	if (ObjectData.SubObjectRootIndex != FNetRefHandleManager::InvalidInternalIndex)
	{
		const FNetRefHandleManager::FReplicatedObjectData& RootObjectData = NetRefHandleManager.GetReplicatedObjectDataNoCheck(ObjectData.SubObjectRootIndex);
		OwnerRefHandle = RootObjectData.RefHandle;
	}

	UObject* RootObject = InternalContext->ObjectReferenceCache->ResolveObjectReferenceHandle(OwnerRefHandle, InternalContext->ResolveContext);
	return RootObject;
}

static bool NetRPC_GetFunctionAndObject(FNetSerializationContext& Context, const FNetObjectReference& ObjectReference, const FNetObjectReference& SubObjectReference, const FNetRPC::FFunctionLocator& FunctionLocator, const FReplicationStateMemberFunctionDescriptor*& OutFunctionDescriptor, TWeakObjectPtr<UObject>& OutObject)
{
	// 中文：接收侧综合解析——返回 UFunction 描述符 + 目标 UObject。
	//   * 找不到对象（cache 解析失败）→ 返回 false 但不 SetError，允许 RPC 静默
	//     丢弃（仍在 streaming-in / 已销毁等情况）；
	//   * 协议不存在 → 同上，对象已停止复制；
	//   * Locator 越界 → SetError(NetError_UnknownFunction)，视为协议错误。
	const UReplicationSystem* ReplicationSystem = Context.GetInternalContext()->ReplicationSystem;
	const UObjectReplicationBridge* Bridge = Cast<UObjectReplicationBridge>(ReplicationSystem->GetReplicationBridge());
	if (!ensure(Bridge != nullptr))
	{
		Context.SetError(NetError_InvalidNetObjectReference);
		return false;
	}

	UObject* RefObject = NetRPC_GetObject(Context, ObjectReference, SubObjectReference);
	if (!RefObject)
	{
		// Ignore this RPC and continue processing the rest of the data
        return false;
	}

	const FReplicationProtocol* Protocol = ReplicationSystem->GetReplicationProtocol(ObjectReference.GetRefHandle());
	if (!Protocol)
	{
		// Ignore this RPC and continue processing the rest of the data, Note: this might for example occur if we have incoming RPC data from client to an object that has been destroyed on server.
  		UE_LOG(LogIrisRpc, Verbose, TEXT("ReplicationProtocol doesn't exist for %s (Connection %u). Ignoring RPC (%u|%u), this is most likely due to object %s no longer being replicated."), *GetNameSafe(RefObject), Context.GetLocalConnectionId(), FunctionLocator.DescriptorIndex, FunctionLocator.FunctionIndex, *ObjectReference.ToString());
		return false;
	}

	if (!ensure(FunctionLocator.DescriptorIndex < Protocol->ReplicationStateCount))
	{
		Context.SetError(NetError_UnknownFunction);
		return false;
	}

	const FReplicationStateDescriptor* Descriptor = Protocol->ReplicationStateDescriptors[FunctionLocator.DescriptorIndex];
	if (!ensure(FunctionLocator.FunctionIndex < Descriptor->FunctionCount))
	{
		Context.SetError(NetError_UnknownFunction);
		return false;
	}

	OutFunctionDescriptor = &Descriptor->MemberFunctionDescriptors[FunctionLocator.FunctionIndex];

	OutObject = RefObject;

	return true;
}

static FString NetRPC_GetDebugFunctionName(const UReplicationSystem* ReplicationSystem, const FNetObjectReference& ObjectReference, const FNetRPC::FFunctionLocator& FunctionLocator)
{
	const FReplicationProtocol* Protocol = ReplicationSystem->GetReplicationProtocol(ObjectReference.GetRefHandle());
	if (!Protocol)
	{
		return TEXT("ProtocolNotFound");
	}

	if (FunctionLocator.DescriptorIndex >= Protocol->ReplicationStateCount)
	{
		return TEXT("InvalidDescriptorIndex");
	}

	const FReplicationStateDescriptor* Descriptor = Protocol->ReplicationStateDescriptors[FunctionLocator.DescriptorIndex];
	if (FunctionLocator.FunctionIndex >= Descriptor->FunctionCount)
	{
		return TEXT("InvalidFunctionIndex");
	}

	return GetNameSafe(Descriptor->MemberFunctionDescriptors[FunctionLocator.FunctionIndex].Function);
}

static FString NetRPC_GetDebugObjectRefName(FNetSerializationContext& Context, const FNetObjectReference& ObjectReference, UObject* ObjectPtr)
{
	if (!ObjectPtr)
	{
		ObjectPtr = NetRPC_GetObject(Context, ObjectReference);
	}
	return FString::Printf(TEXT("%s (%s)"), *GetNameSafe(ObjectPtr), *ObjectReference.ToString());
}

}
