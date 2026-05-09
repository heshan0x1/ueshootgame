// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// NetBlobHandlerManager.cpp —— 全局 typeId 注册与派发实现。
// 关键逻辑：
//   - Init        ：固定 Handlers 数组长度等于 ini 条目数；断言 < 128（typeId 8 bits）。
//   - Register    ：按 ClassName 反查 ini 下标 → 把下标写回 handler->NetBlobType。
//   - CreateBlob  ：接收侧按 typeId 直接 Handlers[typeId]->CreateNetBlob(info)。
//   - OnReceived  ：派发到 Handlers[typeId]->OnNetBlobReceived。
// =====================================================================================

#include "Iris/ReplicationSystem/NetBlob/NetBlobHandlerManager.h"
#include "Iris/ReplicationSystem/NetBlob/NetBlobHandlerDefinitions.h"
#include "Iris/Serialization/NetSerializationContext.h"
#include "Iris/Core/IrisLog.h"
#include "Containers/ArrayView.h"

namespace UE::Net::Private
{

FNetBlobHandlerManager::FNetBlobHandlerManager()
{
}

void FNetBlobHandlerManager::Init()
{
	const UNetBlobHandlerDefinitions* BlobHandlerDefinitions = GetDefault<UNetBlobHandlerDefinitions>();
	// Check if FNetBlob::SerializeCreationInfo needs to use more bits for blob type.
	// typeId 在 SerializeCreationInfo 中按 1+7 bits 编码，因此最多支持 128 个 handler。
	// 超出限制说明 ini 配置过多，必须调整序列化协议（增加 typeId 位宽）。
	checkf(BlobHandlerDefinitions->NetBlobHandlerDefinitions.Num() < 128, TEXT("Excessive amount of NetBlobHandlers: %d. This breaks net serialization."), BlobHandlerDefinitions->NetBlobHandlerDefinitions.Num());
	// 预分配空槽：尚未注册的下标对应 nullptr/Invalid。
	Handlers.SetNum(BlobHandlerDefinitions->NetBlobHandlerDefinitions.Num());
}

bool FNetBlobHandlerManager::RegisterHandler(UNetBlobHandler* Handler)
{
	if (Handler == nullptr)
	{
		return false;
	}

	// 防重复注册：每个 handler 只能拿到一次 typeId。
	if (!ensureMsgf(Handler->NetBlobType == InvalidNetBlobType, TEXT("NetBlobHandler of class %s has already been registered."), ToCStr(Handler->GetClass()->GetName())))
	{
		return false;
	}

	// 按 ClassName 字符串匹配 ini 中的 ClassName：命中即拿到下标作为 typeId。
	const FString& ClassName = Handler->GetClass()->GetName();
	const UNetBlobHandlerDefinitions* BlobHandlerDefinitions = GetDefault<UNetBlobHandlerDefinitions>();
	for (const FNetBlobHandlerDefinition& Definition : MakeArrayView(BlobHandlerDefinitions->NetBlobHandlerDefinitions))
	{
		if (Definition.ClassName.ToString() == ClassName)
		{
			// 用指针差求下标 = ini 中的位置 = 运行期 typeId。
			const uint32 Index = static_cast<uint32>(&Definition - BlobHandlerDefinitions->NetBlobHandlerDefinitions.GetData());
			Handler->NetBlobType = Index;
			Handlers[Index] = Handler;
			return true;
		}
	}

	// 未在 ini 中登记 → 注册失败（典型原因：发送端/接收端 ini 不一致）。
	UE_LOG(LogIris, Warning, TEXT("Handler of class %s was not found in the NetBlobHandlerDefinitions"), ToCStr(Handler->GetClass()->GetName()));
	return false;
}

// 接收端按 typeId 创建空 blob。
// 流程：FReplicationReader 调 DeserializeCreationInfo 拿到 typeId →
//       Manager 反查 handler → handler 创建对应派生 blob 实例 → 反序列化。
TRefCountPtr<FNetBlob> FNetBlobHandlerManager::CreateNetBlob(const FNetBlobCreationInfo& CreationInfo) const
{
	// typeId 越界保护：防御异常数据导致 OOB 访问。
	if (!ensureMsgf(CreationInfo.Type < uint32(Handlers.Num()), TEXT("Unknown NetBlob type %u"), CreationInfo.Type))
	{
		return nullptr;
	}

	UNetBlobHandler* Handler = Handlers[CreationInfo.Type].Get();
	if (Handler == nullptr)
	{
		// 槽位被预留但未注册（例如服务端注册了，客户端忘了注册）。
		UE_LOG(LogIris, Warning, TEXT("No handler registered for NetBlob type %u"), CreationInfo.Type);
		return nullptr;
	}

	const TRefCountPtr<FNetBlob>& Blob = Handler->CreateNetBlob(CreationInfo);
#if !UE_BUILD_SHIPPING
	// 一致性检查：handler 创建出的 blob typeId 必须等于请求 typeId。
	ensure(!Blob.IsValid() || (Blob->GetCreationInfo().Type == CreationInfo.Type));
#endif

	return Blob;
}

// blob 反序列化完成后由 ReplicationReader 调用，根据 typeId 派发到对应 handler。
void FNetBlobHandlerManager::OnNetBlobReceived(FNetSerializationContext& Context, const TRefCountPtr<FNetBlob>& Blob)
{
	if (!Blob.IsValid())
	{
		return;
	}

	const FNetBlobCreationInfo& CreationInfo = Blob->GetCreationInfo();
	if (!ensure(CreationInfo.Type < uint32(Handlers.Num())))
	{
		// 超界视为非法 typeId，标记 UnsupportedNetBlob。
		Context.SetError(GNetError_UnsupportedNetBlob);
		return;
	}

	UNetBlobHandler* Handler = Handlers[CreationInfo.Type].Get();
	if (Handler == nullptr)
	{
		// 双方 ini 不匹配会到这里；通常仅警告，让其他 blob 继续处理。
		UE_LOG(LogIris, Warning, TEXT("No handler registered for NetBlob type %u"), CreationInfo.Type);
		return; 
	}

	return Handler->OnNetBlobReceived(Context, Blob);
}

// 广播新连接事件到所有已注册 handler。空 weak 槽（未注册或 GC 掉）跳过。
void FNetBlobHandlerManager::AddConnection(uint32 ConnectionId) const
{
	for (const TWeakObjectPtr<UNetBlobHandler>& Handler : Handlers)
	{
		if (!Handler.IsValid())
		{
			continue;
		}

		Handler->AddConnection(ConnectionId);
	}
}

// 当前实现刻意为空：handler 多用 TWeakObjectPtr，连接断开由调用方在更高层处理。
// 若未来需要逐 handler 通知断开，可在此遍历 Handlers 调用 RemoveConnection。
void FNetBlobHandlerManager::RemoveConnection(uint32 ConnectionId)
{
}

}
