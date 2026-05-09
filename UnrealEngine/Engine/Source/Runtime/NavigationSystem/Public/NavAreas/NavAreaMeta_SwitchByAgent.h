// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   定义 UNavAreaMeta_SwitchByAgent —— 最常见的 Meta 区域实现：
//   在 NavigationSystem 的 SupportedAgents 数组中每个索引位（0..15）
//   单独配置一个具体 UNavArea 派生类；Tile 生成时根据当前 NavData 的 Agent
//   选择对应槽位的 Area。
//
//   典型用法：同一个 Modifier 对"人"是可通行区，对"车"变成障碍区。
//
// 编辑器辅助：
//   UpdateAgentConfig() 根据 NavigationSystem 实际配置的 Agent 数量
//   动态显示/隐藏 AgentNArea 属性，避免编辑面板充斥未使用的槽位。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "Templates/SubclassOf.h"
#include "NavAreas/NavAreaMeta.h"
#include "NavAreaMeta_SwitchByAgent.generated.h"

class AActor;

/** Class containing definition of a navigation area */
// 按 Agent 索引切换具体 Area 类的 Meta Area。
UCLASS(Abstract, MinimalAPI)
class UNavAreaMeta_SwitchByAgent : public UNavAreaMeta
{
	GENERATED_UCLASS_BODY()

	// Agent 0 槽位对应的具体 Area 类（默认为 UNavArea_Default）。
	UPROPERTY(EditAnywhere, Category=AgentTypes)
	TSubclassOf<UNavArea> Agent0Area;

	// 以下同理，为 Agent 1~15 槽位分别指定 Area 类。
	UPROPERTY(EditAnywhere, Category=AgentTypes)
	TSubclassOf<UNavArea> Agent1Area;

	UPROPERTY(EditAnywhere, Category=AgentTypes)
	TSubclassOf<UNavArea> Agent2Area;

	UPROPERTY(EditAnywhere, Category=AgentTypes)
	TSubclassOf<UNavArea> Agent3Area;

	UPROPERTY(EditAnywhere, Category=AgentTypes)
	TSubclassOf<UNavArea> Agent4Area;

	UPROPERTY(EditAnywhere, Category=AgentTypes)
	TSubclassOf<UNavArea> Agent5Area;

	UPROPERTY(EditAnywhere, Category=AgentTypes)
	TSubclassOf<UNavArea> Agent6Area;

	UPROPERTY(EditAnywhere, Category=AgentTypes)
	TSubclassOf<UNavArea> Agent7Area;

	UPROPERTY(EditAnywhere, Category=AgentTypes)
	TSubclassOf<UNavArea> Agent8Area;

	UPROPERTY(EditAnywhere, Category=AgentTypes)
	TSubclassOf<UNavArea> Agent9Area;

	UPROPERTY(EditAnywhere, Category=AgentTypes)
	TSubclassOf<UNavArea> Agent10Area;

	UPROPERTY(EditAnywhere, Category=AgentTypes)
	TSubclassOf<UNavArea> Agent11Area;

	UPROPERTY(EditAnywhere, Category=AgentTypes)
	TSubclassOf<UNavArea> Agent12Area;

	UPROPERTY(EditAnywhere, Category=AgentTypes)
	TSubclassOf<UNavArea> Agent13Area;

	UPROPERTY(EditAnywhere, Category=AgentTypes)
	TSubclassOf<UNavArea> Agent14Area;

	UPROPERTY(EditAnywhere, Category=AgentTypes)
	TSubclassOf<UNavArea> Agent15Area;

	// 重写：根据 NavAgent 在系统中的 Index 返回 AgentXArea 槽位的 Area 类。
	NAVIGATIONSYSTEM_API virtual TSubclassOf<UNavAreaBase> PickAreaClassForAgent(const AActor& Actor, const FNavAgentProperties& NavAgent) const override;

#if WITH_EDITOR
	/** setup AgentXArea properties */
	// 按当前 NavigationSystem 配置的 Agent 数量动态显示 AgentNArea 属性并设置友好名。
	NAVIGATIONSYSTEM_API virtual void UpdateAgentConfig();
#endif
};
