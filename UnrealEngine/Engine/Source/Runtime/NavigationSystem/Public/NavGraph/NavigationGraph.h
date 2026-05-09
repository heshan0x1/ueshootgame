// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavigationGraph.h
// -----------------------------------------------------------------------------
// 概述：
//   定义了与 NavMesh 平行的 "图式导航数据" 基础类型。包含：
//     - FNavGraphEdge：图的一条边（起点、终点、标志位、启用开关）
//     - FNavGraphNode：图的一个节点（归属对象 Owner + 出边数组）
//     - ANavigationGraph：ANavigationData 的派生 Actor（当前仍为 abstract）
//
// 与架构文档的关系：
//   详见 NavigationSystem_Architecture_CN.md 的 "Layer 4'：NavGraph（不完整的替代实现）"
//   目前仅作为占位存在，FNavGraphGenerator 也只有空方法，没有真正的生成算法。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "NavigationData.h"
#include "NavigationGraph.generated.h"


// 图中的一条有向边：从 Start 节点连到 End 节点。
// Flags 用 7 bit 位域描述通行性/代价等级等；bEnabled 允许运行时开关某条边。
USTRUCT()
struct FNavGraphEdge
{
	GENERATED_USTRUCT_BODY()

	enum {
		// 边标志位所占 bit 数（等于 int32 的高 25 位对应的 Flags 可用范围）
		EgdeFlagsCount = 7
	};

	// 起始节点裸指针（图内部互联，不由 UObject 系统管理）
	struct FNavGraphNode* Start;

	// 终止节点裸指针
	struct FNavGraphNode* End;

	// 边的业务标志位（具体含义由使用者自定义），7 bit 位域
	int32 Flags : EgdeFlagsCount;
	// 运行时开关：是否将该边纳入寻路（例如门锁闭 = false）
	uint32 bEnabled : 1;

	FNavGraphEdge()
		: Start(NULL)
		, End(NULL)
		, Flags(0)
		, bEnabled(false)
	{
	}
};


// 图中的一个节点，对应世界里的一个 Owner（常见是 Actor 或 Component）。
USTRUCT()
struct FNavGraphNode
{
	GENERATED_USTRUCT_BODY()

	/** Who's this node referring to? This will most commonly point to an actor or a component */
	// 节点所绑定的业务对象（Actor/Component），可空。用于从节点反查到原始来源。
	UPROPERTY()
	TObjectPtr<UObject> Owner;

	enum {
		// 构造时预分配的出边容量（避免前几条边反复扩容）
		InitialEdgesCount = 4
	};

	// 本节点的出边集合
	TArray<FNavGraphEdge> Edges;
	// Location to be added here
	// Radius might be needed as well
	// TODO：坐标/半径/代价等字段尚未添加；当前节点不含空间信息，属于半成品。

	FNavGraphNode();
};

/** currently abstract since it's not full implemented */
// 图式导航数据 Actor 基类。继承自 ANavigationData，按理应提供图的构建 + 查询能力，
// 但目前仅在构造函数里挂一个空的 FNavGraphGenerator，因此标记为 abstract。
UCLASS(config=Engine, MinimalAPI, abstract)
class ANavigationGraph : public ANavigationData
{
	GENERATED_UCLASS_BODY()

public:
};
