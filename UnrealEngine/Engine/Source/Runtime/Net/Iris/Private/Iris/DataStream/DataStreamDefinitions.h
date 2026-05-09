// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// DataStreamDefinitions.h —— Iris 数据流的 ini 注册表（私有头）
// ---------------------------------------------------------------------------------------------
// 本文件提供 DataStream 框架的"配置层"：
//   * `FDataStreamDefinition`  ：单条流定义（USTRUCT，Engine ini 反射加载）。
//   * `UDataStreamDefinitions` ：流定义集合（UCLASS, config=Engine）。
//
// 配置示例（DefaultEngine.ini）：
//   [/Script/IrisCore.DataStreamDefinitions]
//   +DataStreams=(DataStreamName="Replication",ClassName="/Script/IrisCore.ReplicationDataStream",
//                 bAutoCreate=true,bDynamicCreate=false,DefaultSendStatus=Send)
//   +DataStreams=(DataStreamName="NetToken",ClassName="/Script/IrisCore.NetTokenDataStream",
//                 bAutoCreate=true,bDynamicCreate=false)
//   +DataStreams=(DataStreamName="ChunkedData",ClassName="/Script/IrisCore.ChunkedDataStream",
//                 bAutoCreate=false,bDynamicCreate=true)
//
// 关键流程：
//   - `UDataStreamManager::Init`（每条连接）→ `InitStreams()` → `FixupDefinitions()` 首次调用时
//     按数组顺序为每条 Definition 分配 `StreamIndex`（0..N-1，最多 32 条）并 StaticLoadClass 把 ClassName 解析为 UClass*。
//   - `GetStreamNamesToAutoCreateOrRegister` 收集 bAutoCreate || bDynamicCreate 的流名，
//     由 Manager 逐一 `CreateStream`：bAutoCreate=true 立即实例化并置 Open；bDynamicCreate=true 仅占位（StreamIndex 锁定）。
//   - 5-bit StreamIndex 决定运行期最多 32 条流（见 DataStreamManager.cpp::MaxStreamCount）。
// =============================================================================================

#pragma once

#include "Iris/DataStream/DataStream.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "DataStreamDefinitions.generated.h"

/**
 * 单条数据流定义（ini 反射加载）。
 *
 * 字段语义：
 *  - DataStreamName     ：流的唯一标识；连接级 CreateStream / GetStream 都用它做 key。
 *  - ClassName          ：UDataStream 派生类的完整路径（如 `/Script/IrisCore.ReplicationDataStream`），
 *                         在 FixupDefinitions 时通过 StaticLoadClass 延迟加载到 Class。
 *  - Class              ：上一字段加载结果；非 ini 字段，运行期填充。
 *  - DefaultSendStatus  ：流被创建后初始的发送状态（Pause / Send）；可在运行期通过 SetSendStatus 改写。
 *  - bAutoCreate        ：true → 每条连接 Init 时自动创建并立即转 Open（典型：Replication / NetToken）。
 *  - bDynamicCreate     ：true → 运行期可通过 OpenDataStream/CloseDataStream 动态开关（占位 StreamIndex，
 *                         需要双向握手；典型：ChunkedData）。
 *  - StreamIndex        ：FixupDefinitions 时按数组下标分配的 5-bit 索引（0..31）；用于位掩码序列化。
 *
 * bAutoCreate 与 bDynamicCreate 并不互斥，但典型用法只设其一：
 *   - 都为 false：必须由代码显式调用 `UDataStreamManager::CreateStream`（不常见）。
 *   - bAutoCreate=true / bDynamicCreate=false：连接建立即开通，且不允许关闭（直到连接断开）。
 *   - bAutoCreate=false / bDynamicCreate=true：占位但默认未开通，运行期按需 Open/Close 并走握手。
 */
USTRUCT()
struct FDataStreamDefinition
{
	GENERATED_BODY()

	// Data stream identifier
	// 流标识符（唯一名）。CreateStream / GetStream / CloseStream 等所有 API 的 key。
	UPROPERTY()
	FName DataStreamName;

	// UClass name used to create the UDataStream
	// 派生 UDataStream 的 UClass 完整路径名；在 FixupDefinitions 中 StaticLoadClass 解析为 Class。
	UPROPERTY()
	FName ClassName;		

	// UClass used to create the UDataStream
	// 解析后的 UClass 指针；非 ini 字段，由 FixupDefinitions 填充。
	UPROPERTY()
	TObjectPtr<UClass> Class = nullptr;

	// Default send status when created.
	// 创建时默认 SendStatus（Pause / Send），可在运行期通过 UDataStreamManager::SetSendStatus 修改。
	UPROPERTY()
	EDataStreamSendStatus DefaultSendStatus = EDataStreamSendStatus::Send;

	// Whether the DataStream should be auto created for each connection. If not then CreateStream need be called manually.
	// true：每条连接 Init 时自动 CreateStream 并置 Open；false：必须代码显式调用 CreateStream。
	UPROPERTY()
	bool bAutoCreate = false;

	// If bDynamicCreate is set to true we will reserve a slot for the stream allowing it to be openened and closed on demand
	// true：占位 StreamIndex 但不立即创建；运行期可通过 RequestClose / 双向握手开/关。
	UPROPERTY()
	bool bDynamicCreate = false;

	// Get the assigned stream index
	// 返回 FixupDefinitions 时分配的 StreamIndex（用于位掩码）；尚未 fixup 时为 -1。
	int32 GetStreamIndex() const;

private:
	friend class UDataStreamDefinitions;
	// FixupDefinitions 中按 ini 数组下标顺序分配的 5-bit 流索引（0..MaxStreamCount-1，即 0..31）。
	// -1 表示尚未 Fixup。
	int32 StreamIndex = -1;
};

/**
 * UDataStreamDefinitions —— 全引擎共享的 DataStream 定义集合（CDO 单例）。
 *
 * 通过 `GetDefault<UDataStreamDefinitions>()` 取 CDO 即可读取所有 ini 定义，
 * 通过 `GetMutableDefault<UDataStreamDefinitions>()` 可在首次访问时触发 FixupDefinitions（延迟加载 UClass）。
 *
 * config=Engine：定义来自 DefaultEngine.ini 的 `[/Script/IrisCore.DataStreamDefinitions]` 段。
 * transient：不参与磁盘序列化（仅持有 ini 反射快照）。
 *
 * 友元 UDataStreamManager —— 仅 Manager 需要查询/触发 fixup；外部使用者不应直接访问。
 */
UCLASS(transient, config=Engine)
class UDataStreamDefinitions : public UObject
{
	GENERATED_BODY()

protected:
	// 默认构造：bFixupComplete = false，DataStreamDefinitions 由 ini 反射填充。
	UDataStreamDefinitions();

protected:
	friend class UDataStreamManager;

	// 延迟初始化：首次被 Manager 调用时执行 ——
	//   1) 校验同名重复；
	//   2) 校验 DefaultSendStatus 合法（IsValidEnumValue）；
	//   3) StaticLoadClass 把 ClassName 解析为 UClass*；
	//   4) 按 ini 数组顺序分配 StreamIndex（0..N-1）。
	// 第二次起 short-circuit（bFixupComplete 已为 true）。
	void FixupDefinitions();

	// 按名查找 ini 定义；返回 nullptr 表示未注册。
	const FDataStreamDefinition* FindDefinition(const FName Name) const;
	// 按 StreamIndex 查找 ini 定义（用于 ReadData 时把对端 StreamIndex 反映射成 Definition）。
	const FDataStreamDefinition* FindDefinition(int32 StreamIndex) const;
	// 静态 helper：返回 Definition.StreamIndex（封装 private 字段）。
	static int32 GetStreamIndex(const FDataStreamDefinition& Definition);
	// 收集所有 bAutoCreate || bDynamicCreate 的流名 —— 由 Manager 在 InitStreams 中遍历它们逐个 CreateStream。
	void GetStreamNamesToAutoCreateOrRegister(TArray<FName>& OutStreamNames) const;

private:
	// ini 配置数组：来自 [/Script/IrisCore.DataStreamDefinitions] +DataStreams=(...) 行。
	UPROPERTY(Config)
	TArray<FDataStreamDefinition> DataStreamDefinitions;

	// FixupDefinitions 是否已完成 —— 防止重复 StaticLoadClass / 重新分配 StreamIndex。
	bool bFixupComplete;

// For testing purposes only
// 自动化测试钩子：仅在 WITH_AUTOMATION_WORKER 下暴露读写访问，便于注入伪定义并强制重新 fixup。
#if WITH_AUTOMATION_WORKER
public:
	inline TArray<FDataStreamDefinition>& ReadWriteDataStreamDefinitions() { return DataStreamDefinitions; }
	inline bool& ReadWriteFixupComplete() { return bFixupComplete; }
#endif
};
