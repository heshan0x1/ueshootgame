// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   UNavAreaMeta_SwitchByAgent 的实现——按 Agent 索引选择具体 Area 类。
//
// 关键函数：
//   - 构造：把 16 个 AgentNArea 槽位全部默认指向 UNavArea_Default。
//   - PickAreaClassForAgent：生成阶段被调用，根据传入的 NavAgent 在
//     NavigationSystem.SupportedAgents 中的索引选择对应槽位；找不到槽位
//     时回退到 FNavigationSystem::GetDefaultWalkableArea()。
//   - UpdateAgentConfig（编辑器）：根据当前配置 Agent 数量动态显示/隐藏属性。
// =============================================================================

#include "NavAreas/NavAreaMeta_SwitchByAgent.h"
#include "UObject/UnrealType.h"
#include "NavigationSystem.h"
#include "NavAreas/NavArea_Default.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavAreaMeta_SwitchByAgent)

// 构造：16 个槽位默认都指向 Default Area，即"没配置就当默认可行走区"。
UNavAreaMeta_SwitchByAgent::UNavAreaMeta_SwitchByAgent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	Agent0Area = UNavArea_Default::StaticClass();
	Agent1Area = UNavArea_Default::StaticClass();
	Agent2Area = UNavArea_Default::StaticClass();
	Agent3Area = UNavArea_Default::StaticClass();
	Agent4Area = UNavArea_Default::StaticClass();
	Agent5Area = UNavArea_Default::StaticClass();
	Agent6Area = UNavArea_Default::StaticClass();
	Agent7Area = UNavArea_Default::StaticClass();
	Agent8Area = UNavArea_Default::StaticClass();
	Agent9Area = UNavArea_Default::StaticClass();
	Agent10Area = UNavArea_Default::StaticClass();
	Agent11Area = UNavArea_Default::StaticClass();
	Agent12Area = UNavArea_Default::StaticClass();
	Agent13Area = UNavArea_Default::StaticClass();
	Agent14Area = UNavArea_Default::StaticClass();
	Agent15Area = UNavArea_Default::StaticClass();
}

// Tile 生成阶段调用：找到该 NavAgent 在系统内的 Index，再按 Index 查相应槽位的 Area 类。
// 若 Index 无效或槽位为空，回退到全局默认可行走区（避免产生"不可通行多边形"）。
TSubclassOf<UNavAreaBase> UNavAreaMeta_SwitchByAgent::PickAreaClassForAgent(const AActor& Actor, const FNavAgentProperties& NavAgent) const
{
	const UNavigationSystemV1* DefNavSys = GetDefault<UNavigationSystemV1>();
	const int32 AgentIndex = DefNavSys ? DefNavSys->GetSupportedAgentIndex(NavAgent) : INDEX_NONE;
	TSubclassOf<UNavArea> UseAreaClass = NULL;
	
	// 分支：按 AgentIndex 挑具体槽位。使用 switch 而不是数组是因为各槽位都是独立的 UPROPERTY。
	switch (AgentIndex)
	{
	case 0: UseAreaClass = Agent0Area; break;
	case 1: UseAreaClass = Agent1Area; break;
	case 2: UseAreaClass = Agent2Area; break;
	case 3: UseAreaClass = Agent3Area; break;
	case 4: UseAreaClass = Agent4Area; break;
	case 5: UseAreaClass = Agent5Area; break;
	case 6: UseAreaClass = Agent6Area; break;
	case 7: UseAreaClass = Agent7Area; break;
	case 8: UseAreaClass = Agent8Area; break;
	case 9: UseAreaClass = Agent9Area; break;
	case 10: UseAreaClass = Agent10Area; break;
	case 11: UseAreaClass = Agent11Area; break;
	case 12: UseAreaClass = Agent12Area; break;
	case 13: UseAreaClass = Agent13Area; break;
	case 14: UseAreaClass = Agent14Area; break;
	case 15: UseAreaClass = Agent15Area; break;
	default: break;
	}

	// 空槽位兜底：返回全局默认可行走 Area，保证生成阶段不会得到 nullptr。
	return UseAreaClass ? UseAreaClass : FNavigationSystem::GetDefaultWalkableArea();
}

#if WITH_EDITOR
// 编辑器辅助：根据 NavigationSystem 实际配置的 Agent 数量，动态显示对应数量的 AgentNArea 属性。
// 副作用：设置属性的 CPF_Edit 标记与 DisplayName Meta；不参与运行时寻路。
void UNavAreaMeta_SwitchByAgent::UpdateAgentConfig()
{
	const UNavigationSystemV1* DefNavSys = (UNavigationSystemV1*)(UNavigationSystemV1::StaticClass()->GetDefaultObject());
	check(DefNavSys);

	// 16 槽是历史上限（与 FNavAgentSelector 的位数对齐）。
	const int32 MaxAllowedAgents = 16;
	const int32 NumAgents = FMath::Min(DefNavSys->GetSupportedAgents().Num(), MaxAllowedAgents);
	// 当项目配置的 Agent 超过 16 个时，多余的无法显示——提醒用户。
	if (DefNavSys->GetSupportedAgents().Num() > MaxAllowedAgents)
	{
		UE_LOG(LogNavigation, Error, TEXT("Navigation system supports %d agents, but only %d can be shown in %s properties!"),
			DefNavSys->GetSupportedAgents().Num(), MaxAllowedAgents, *GetClass()->GetName());
	}

	const FString CustomNameMeta = TEXT("DisplayName");
	// 迭代目标：遍历全部 16 个 AgentNArea 属性，按当前 Agent 数量决定可见性。
	for (int32 i = 0; i < MaxAllowedAgents; i++)
	{
		const FString PropName = FString::Printf(TEXT("Agent%dArea"), i);
		FProperty* Prop = FindFProperty<FProperty>(UNavAreaMeta_SwitchByAgent::StaticClass(), *PropName);
		check(Prop);

		// 分支：仅配置了多个 Agent（NumAgents>1）且 i 在范围内时才显示，否则隐藏。
		// 单 Agent 时用 Meta 切换无意义，直接全隐藏。
		if (i < NumAgents && NumAgents > 1)
		{
			Prop->SetPropertyFlags(CPF_Edit);
			Prop->SetMetaData(*CustomNameMeta, *FString::Printf(TEXT("Area Class for: %s"), *DefNavSys->GetSupportedAgents()[i].Name.ToString()));
		}
		else
		{
			Prop->ClearPropertyFlags(CPF_Edit);
		}
	}
}
#endif

