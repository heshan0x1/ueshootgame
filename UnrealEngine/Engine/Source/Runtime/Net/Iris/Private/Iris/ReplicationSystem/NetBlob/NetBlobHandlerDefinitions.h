// Copyright Epic Games, Inc. All Rights Reserved.

// =====================================================================================
// NetBlobHandlerDefinitions.h —— ini 配置驱动的 NetBlobHandler 注册表。
//
// 配置位置：
//   [/Script/IrisCore.NetBlobHandlerDefinitions]
//   +NetBlobHandlerDefinitions=(ClassName="...UNetRPCHandler...")
//   +NetBlobHandlerDefinitions=(ClassName="...UPartialNetObjectAttachmentHandler...")
//   ...
//
// typeId 分配规则：
//   - FNetBlobHandlerManager::Init 时按 NetBlobHandlerDefinitions 数组下标分配 typeId。
//   - RegisterHandler 时按 ClassName 反查下标，写入 handler->NetBlobType。
//   - 因此 ini 顺序就是 typeId，发送端与接收端必须保持顺序一致；新增项必须追加到末尾。
//   - 数量上限 < 128（参见 FNetBlob::SerializeCreationInfo 的 1+7 bits 编码）。
// =====================================================================================

#pragma once
#include "CoreTypes.h"
#include "Iris/ReplicationSystem/NetRefHandle.h"
#include "NetBlobHandlerDefinitions.generated.h"

namespace UE::Net::Private
{
	class FNetBlobHandlerManager;
}

// 单条 ini 配置：仅含 ClassName。Manager 按 ClassName 字符串匹配 UClass。
USTRUCT()
struct FNetBlobHandlerDefinition
{
	GENERATED_BODY()

	/**
	 * UClass name of the UNetObjectHandler derived class.
	 * In order for a handler to be successfully registered via UReplicationSystem::RegisterNetBlobHandler
	 * the handler class must match one of the definitions.
	 */
	// 派生自 UNetBlobHandler 的 UClass 名称（不含包名）。注册时按此名匹配。
	UPROPERTY()
	FName ClassName;
};

/** Configurable net blob handler definitions. */
// 全局可配置 handler 列表，承载在 CDO 上（Engine.ini 加载）。
UCLASS(MinimalAPI, transient, config=Engine)
class UNetBlobHandlerDefinitions : public UObject
{
	GENERATED_BODY()

protected:
	UNetBlobHandlerDefinitions();

private:
	// FNetBlobHandlerManager 直接读取此数组以分配 typeId。
	friend UE::Net::Private::FNetBlobHandlerManager;

	// 全部 handler 定义；下标即运行期 typeId。
	UPROPERTY(Config)
	TArray<FNetBlobHandlerDefinition> NetBlobHandlerDefinitions;

#if WITH_AUTOMATION_WORKER
public:
	// 仅自动化测试可读写：测试代码通过此入口动态注入临时 handler 配置。
	TArray<FNetBlobHandlerDefinition>& ReadWriteHandlerDefinitions() { return NetBlobHandlerDefinitions; }
#endif
};
