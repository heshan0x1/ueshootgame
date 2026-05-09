// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   定义 UNavArea —— "导航区域（Area）" 的抽象基类。每个 Area 描述了一段
//   NavMesh 多边形"是什么"：寻路代价、进入代价、颜色、允许的 Agent、Flag。
//   系统为每个 UNavArea 派生类的 CDO 分配一个 8-bit 的 AreaId（Recast 限制），
//   然后在 Tile 生成时把该 Id 打印到 PolyMesh 对应多边形上。
//
// 派生类（见同目录）：
//   - UNavArea_Default   —— 默认可行走区（Cost=1，Flag=1）。
//   - UNavArea_Null      —— 完全不可通行（Cost=FLT_MAX，Flag=0）。
//   - UNavArea_Obstacle  —— 高代价障碍区（Cost=1,000,000）。
//   - UNavArea_LowHeight —— 顶部空间不足不可通行（Cost=BIG_NUMBER，Flag=0）。
//   - UNavAreaMeta / UNavAreaMeta_SwitchByAgent —— Meta 区域，运行时按 Agent
//     选择最终具体 Area 类。
//
// 注册流程：
//   - CDO 构造后 PostInitProperties → RegisterArea → 通过 UNavigationSystemV1::
//     RequestAreaRegistering 统一登记；蓝图派生类会在 PostLoad 再次尝试注册。
//   - 析构（主要是 Engine 关闭）时 FinishDestroy → RequestAreaUnregistering。
//
// 与其它文件的关系：
//   - 被 NavigationQueryFilter 大量引用（Area 作为 Cost 覆写的键）。
//   - RecastNavMeshGenerator 在生成 PolyMesh 时把 Modifier 的 Area 类翻译为 AreaId。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "AI/Navigation/NavAreaBase.h"
#include "AI/Navigation/NavAgentSelector.h"
#include "NavArea.generated.h"

/** Class containing definition of a navigation area */
// 导航区域抽象基类；所有可在编辑器里选择/配置的 Area 都应派生自本类。
UCLASS(DefaultToInstanced, abstract, Config=Engine, Blueprintable, MinimalAPI)
class UNavArea : public UNavAreaBase
{
	GENERATED_BODY()

public: 
	/** travel cost multiplier for path distance */
	// 通行代价倍率：A* 走一步实际距离 * DefaultCost。值越大越不愿意穿越。
	UPROPERTY(EditAnywhere, Category=NavArea, config, meta=(ClampMin = "0.0"))
	float DefaultCost;

protected:
	/** entering cost */
	// 进入代价：首次踏入该 Area 时一次性附加的固定代价。
	UPROPERTY(EditAnywhere, Category=NavArea, config, meta=(ClampMin = "0.0"))
	float FixedAreaEnteringCost;

public:
	/** area color in navigation view */
	// 在 NavMesh 可视化里的染色；CDO 里设置，运行时读取。
	UPROPERTY(EditAnywhere, Category=NavArea, config)
	FColor DrawColor;

	/** restrict area only to specified agents */
	// 限制该 Area 仅对部分 Agent 生效（16 位位集）。
	UPROPERTY(EditAnywhere, Category=NavArea, config)
	FNavAgentSelector SupportedAgents;

	// DEPRECATED AGENT CONFIG
	// 以下 16 个 bSupportsAgentN 是旧版的位拆分存储，与 SupportedAgents 通过 union 复用同一块内存；
	// RegisterArea() 会把这些旧字段拷贝进新版 SupportedAgents（只在未初始化时一次性迁移）。
#if CPP
	union
	{
		struct
		{
#endif
	UPROPERTY(config)
	uint32 bSupportsAgent0 : 1;
	UPROPERTY(config)
	uint32 bSupportsAgent1 : 1;
	UPROPERTY(config)
	uint32 bSupportsAgent2 : 1;
	UPROPERTY(config)
	uint32 bSupportsAgent3 : 1;
	UPROPERTY(config)
	uint32 bSupportsAgent4 : 1;
	UPROPERTY(config)
	uint32 bSupportsAgent5 : 1;
	UPROPERTY(config)
	uint32 bSupportsAgent6 : 1;
	UPROPERTY(config)
	uint32 bSupportsAgent7 : 1;
	UPROPERTY(config)
	uint32 bSupportsAgent8 : 1;
	UPROPERTY(config)
	uint32 bSupportsAgent9 : 1;
	UPROPERTY(config)
	uint32 bSupportsAgent10 : 1;
	UPROPERTY(config)
	uint32 bSupportsAgent11 : 1;
	UPROPERTY(config)
	uint32 bSupportsAgent12 : 1;
	UPROPERTY(config)
	uint32 bSupportsAgent13 : 1;
	UPROPERTY(config)
	uint32 bSupportsAgent14 : 1;
	UPROPERTY(config)
	uint32 bSupportsAgent15 : 1;
#if CPP
		};
		// union 别名：把 16 个 bit 压成一个 uint32 方便整体读写（RegisterArea 会读它）。
		uint32 SupportedAgentsBits;
	};
#endif

	NAVIGATIONSYSTEM_API UNavArea(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	// 析构：若是 CDO 则从 NavigationSystem 反注册该 Area 类。
	NAVIGATIONSYSTEM_API virtual void FinishDestroy() override;
	// 加载后：补做 Registration（蓝图派生类此时属性才齐）。
	NAVIGATIONSYSTEM_API virtual void PostLoad() override;
	// 属性初始化后：CDO 首次注册入口。
	NAVIGATIONSYSTEM_API virtual void PostInitProperties() override;
	// 序列化：在保存前标记 SupportedAgents 已初始化，避免再次 legacy 迁移。
	NAVIGATIONSYSTEM_API virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	// 编辑器属性变更：若 Cost/SupportedAgents 被改，先反注册再重新注册。
	NAVIGATIONSYSTEM_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent);
#endif // WITH_EDITOR

	// 读取当前 Area Flags（16-bit）。AreaFlags==0 特指 UNWALKABLE。
	inline uint16 GetAreaFlags() const { return AreaFlags; }
	// 判断 AreaFlags 是否包含 InFlags 中任意一位。
	inline bool HasFlags(uint16 InFlags) const { return (InFlags & AreaFlags) != 0; }

	// 判定本 Area 是否对指定 AgentIndex 开放（查询 SupportedAgents 位集）。
	inline bool IsSupportingAgent(int32 AgentIndex) const { return SupportedAgents.Contains(AgentIndex); }

	/** called before adding to navigation system */
	// 注册进 NavigationSystem 前的钩子；派生类可重写做一次性的运行时初始化。
	virtual void InitializeArea() {};

	/** Get the fixed area entering cost. */
	// 可被派生类重写以动态决定进入代价（默认返回 UPROPERTY 值）。
	virtual float GetFixedAreaEnteringCost() { return FixedAreaEnteringCost; }

	/** Retrieved color declared for AreaDefinitionClass */
	// 静态工具：根据 Area 类读取其 CDO 的 DrawColor。
	static NAVIGATIONSYSTEM_API FColor GetColor(UClass* AreaDefinitionClass);

	/** copy properties from other area */
	// 拷贝另一 Area 类的 Cost/Flag/Color（不复制 SupportedAgents），用于派生类继承行为。
	NAVIGATIONSYSTEM_API virtual void CopyFrom(TSubclassOf<UNavArea> AreaClass);

protected:

	/** these flags will be applied to navigation data along with AreaID */
	// Area Flag 位集；会和 AreaID 一起写入 Recast Poly。注意：0 == 不可通行！
	uint16 AreaFlags;
	
	// 核心注册逻辑：首次完成属性初始化后把本 Area 类上报 NavigationSystem。
	NAVIGATIONSYSTEM_API void RegisterArea();
};
