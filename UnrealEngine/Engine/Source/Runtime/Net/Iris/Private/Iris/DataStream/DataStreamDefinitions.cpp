// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================
// DataStreamDefinitions.cpp —— ini 定义的延迟加载、查询与索引分配实现
// =============================================================================================

#include "Iris/DataStream/DataStreamDefinitions.h"
#include "Iris/DataStream/DataStream.h"
#include "Iris/Core/IrisLog.h"
#include "UObject/UObjectGlobals.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataStreamDefinitions)

// 默认构造：bFixupComplete = false。DataStreamDefinitions 数组由 UObject ini 反射机制在 CDO 加载时填充。
UDataStreamDefinitions::UDataStreamDefinitions()
: bFixupComplete(false)
{
}

/**
 * 延迟初始化：把 ini 中字符串形态的 ClassName 解析成 UClass*，并按数组下标分配 StreamIndex。
 *
 * 调用时机：第一条连接的 `UDataStreamManager::FImpl::InitStreams()` 中调用一次（对 CDO）；
 * 之后每条新连接复用同一份 fixup 过的 CDO（bFixupComplete 短路保证只跑一次）。
 *
 * 失败处理：StaticLoadClass 失败 → 仅打印 Error，Class 保持为 nullptr。后续 CreateStreamFromDefinition
 * 会因 Class==nullptr 返回 `Error_InvalidDefinition`。
 *
 * 索引分配规则：按 DataStreamDefinitions 数组下标（即 ini 中的出现顺序）逐个递增。
 * 这意味着：**ini 中流的位置就是其在 packet 位掩码中的位序**，新增流时应**追加**而非插入，
 * 否则会破坏二进制兼容性（对端如果是旧版本，位序对不上）。
 */
void UDataStreamDefinitions::FixupDefinitions()
{
	if (bFixupComplete)
	{
		return;
	}

	int32 CurrentStreamIndex = 0;
	for (FDataStreamDefinition& Definition : DataStreamDefinitions)
	{
		// 校验 1：同名重复检测。Lambda 通过指针比较排除自身。
		UE_CLOG(DataStreamDefinitions.ContainsByPredicate([Name = Definition.DataStreamName, &Definition](const FDataStreamDefinition& ExistingDefinition) { return Name == ExistingDefinition.DataStreamName && &Definition != &ExistingDefinition; }), LogIris, Error, TEXT("DataStream name is defined multiple times: %s."), *Definition.DataStreamName.GetPlainNameString());
		// 校验 2：DefaultSendStatus 必须是 EDataStreamSendStatus 的合法枚举值。
		UE_CLOG(!StaticEnum<EDataStreamSendStatus>()->IsValidEnumValue(int8(Definition.DefaultSendStatus)), LogIris, Error, TEXT("Invalid DataStreamSendStatus %u for DataStream %s."), unsigned(Definition.DefaultSendStatus), *Definition.DataStreamName.GetPlainNameString());

		// 加载 UClass：LOAD_Quiet 表示找不到时不抛 ScriptError。后续 CreateStream 会因 Class==nullptr 直接返回 Error_InvalidDefinition。
		Definition.Class = StaticLoadClass(UDataStream::StaticClass(), nullptr, *Definition.ClassName.ToString(), nullptr, LOAD_Quiet);

		UE_CLOG(Definition.Class == nullptr, LogIris, Error, TEXT("DataStream class could not be loaded: %s"), *Definition.ClassName.GetPlainNameString());

		// 分配 StreamIndex：按 ini 出现顺序自增；用作 5-bit 位掩码索引（最多 32 条流）。
		Definition.StreamIndex = CurrentStreamIndex++;
	}

	bFixupComplete = true;
}

// 静态 helper：暴露 Definition 的 private StreamIndex（FDataStreamDefinition 把 UDataStreamDefinitions 设为友元）。
int32 UDataStreamDefinitions::GetStreamIndex(const FDataStreamDefinition& Definition)
{
	return Definition.StreamIndex;
}

// 按名查找：FindByPredicate 线性扫描；流数量极少（典型 < 8）所以 O(N) 可接受。
const FDataStreamDefinition* UDataStreamDefinitions::FindDefinition(const FName Name) const
{
	return DataStreamDefinitions.FindByPredicate([Name](const FDataStreamDefinition& Definition) { return Name == Definition.DataStreamName; });
}

// 按 StreamIndex 查找：在 ReadData 反序列化阶段，对端送来的 StreamIndex 用此反映射回本端 Definition。
const FDataStreamDefinition* UDataStreamDefinitions::FindDefinition(int32 StreamIndex) const
{
	return DataStreamDefinitions.FindByPredicate([StreamIndex](const FDataStreamDefinition& Definition) { return StreamIndex == Definition.StreamIndex; });
}

/**
 * 收集所有"需要在连接 Init 时被 CreateStream 处理"的流名。
 *
 * 包括：
 *  - bAutoCreate=true     ：要立即实例化并置 Open。
 *  - bDynamicCreate=true  ：仅占位 StreamIndex，不实例化（等待运行期 OpenDataStream）。
 *
 * Manager 在 InitStreams 中遍历这份名单，对每个名字调用 `CreateStream(name, RegisterIfStreamIsDynamic)`，
 * 由 Flag 区分两类的处理路径。
 */
void UDataStreamDefinitions::GetStreamNamesToAutoCreateOrRegister(TArray<FName>& OutStreamNames) const
{
	for (const FDataStreamDefinition& Definition : DataStreamDefinitions)
	{
		if (Definition.bAutoCreate || Definition.bDynamicCreate)
		{
			OutStreamNames.Add(Definition.DataStreamName);
		}
	}
}
