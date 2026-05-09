// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================================================================
// 文件作用：UNetObjectPrioritizerDefinitions 的实现。负责从 ini 加载 prioritizer 注册表。
// 启动加载时机：FReplicationPrioritization::InitPrioritizers 中 NewObject<UNetObjectPrioritizerDefinitions>()，
// PostInitProperties 自动触发 LoadDefinitions（StaticLoadClass 解析 ClassName 与 ConfigClassName）。
// =============================================================================================================================

#include "Iris/ReplicationSystem/Prioritization/NetObjectPrioritizerDefinitions.h"
#include "Iris/ReplicationSystem/Prioritization/NetObjectPrioritizer.h"
#include "Iris/Core/IrisLog.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NetObjectPrioritizerDefinitions)

// 过滤有效条目：Class 加载成功；ConfigClassName 要么留空、要么 ConfigClass 也已加载。
// 加载失败的条目仅在日志中报错（见 LoadDefinitions），此处直接跳过。
void UNetObjectPrioritizerDefinitions::GetValidDefinitions(TArray<FNetObjectPrioritizerDefinition>& OutDefinitions) const
{
	OutDefinitions.Reserve(NetObjectPrioritizerDefinitions.Num());
	for (const FNetObjectPrioritizerDefinition& Definition : NetObjectPrioritizerDefinitions)
	{
		if (Definition.Class != nullptr && (Definition.ConfigClassName.IsNone() || Definition.ConfigClass != nullptr))
		{
			OutDefinitions.Push(Definition);
		}
	}
}

// CDO 不加载（CDO 没有有效的 config 数据）；普通实例首次创建时由 PostInitProperties 自动加载。
void UNetObjectPrioritizerDefinitions::PostInitProperties()
{
	Super::PostInitProperties();
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		LoadDefinitions();
	}
}

// 热重载 ini（Net.Iris 当前不主动支持 prioritizer 热替换；此处保证基础数据被刷新）。
void UNetObjectPrioritizerDefinitions::PostReloadConfig(FProperty* PropertyToLoad)
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		LoadDefinitions();
	}
}

// 内部加载：把 ClassName / ConfigClassName 转成 UClass*。LOAD_Quiet 表示失败不抛出异常，仅返回 nullptr。
// 若加载失败则在 LogIris 输出 Error，并保留 Class=nullptr —— GetValidDefinitions 会跳过这种条目。
void UNetObjectPrioritizerDefinitions::LoadDefinitions()
{
	for (FNetObjectPrioritizerDefinition& Definition : NetObjectPrioritizerDefinitions)
	{
		// 解析主类（必填）。
		Definition.Class = StaticLoadClass(UNetObjectPrioritizer::StaticClass(), nullptr, *Definition.ClassName.ToString(), nullptr, LOAD_Quiet);
		UE_CLOG(Definition.Class == nullptr, LogIris, Error, TEXT("NetObjectPrioritizer class could not be loaded: %s"), *Definition.ClassName.GetPlainNameString());

		// 解析可选 Config 类。
		if (!Definition.ConfigClassName.IsNone())
		{
			Definition.ConfigClass = StaticLoadClass(UNetObjectPrioritizerConfig::StaticClass(), nullptr, *Definition.ConfigClassName.ToString(), nullptr, LOAD_Quiet);
			UE_CLOG(Definition.ConfigClass == nullptr, LogIris, Error, TEXT("NetObjectPrioritizerConfig class could not be loaded: %s"), *Definition.ConfigClassName.GetPlainNameString());
		}
	}
}
