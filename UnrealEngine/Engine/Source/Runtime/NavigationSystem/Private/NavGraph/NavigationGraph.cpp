// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavigationGraph.cpp
// -----------------------------------------------------------------------------
// 与 NavigationGraph.h / NavigationGraphNode.h / NavigationGraphNodeComponent.h
// 配套的实现文件。包含四个类型的最小构造逻辑：
//   - FNavGraphNode：预分配边数组容量
//   - UNavigationGraphNodeComponent：销毁时自动从双向链表脱链
//   - ANavigationGraphNode：空
//   - ANavigationGraph：在非 CDO 上挂 FNavGraphGenerator（当前是空生成器）
// 注意：整套 NavGraph 仍是半成品，没有实现真正的图构建/寻路。
// =============================================================================

#include "NavGraph/NavigationGraph.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavGraph/NavGraphGenerator.h"
#include "NavNodeInterface.h"
#include "NavGraph/NavigationGraphNodeComponent.h"
#include "NavGraph/NavigationGraphNode.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavigationGraph)

//----------------------------------------------------------------------//
// FNavGraphNode
//----------------------------------------------------------------------//
// 节点默认构造：Owner 置空，预留 InitialEdgesCount(=4) 条边的容量，
// 避免前几次 AddEdge 连续扩容。
FNavGraphNode::FNavGraphNode() 
	: Owner(nullptr)
{
	Edges.Reserve(InitialEdgesCount);
}

//----------------------------------------------------------------------//
// UNavigationGraphNodeComponent
//----------------------------------------------------------------------//
UNavigationGraphNodeComponent::UNavigationGraphNodeComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// BeginDestroy：节点从双向链表中"优雅脱链"。
// 把前后邻居相互连接，避免悬空指针；最后清掉自身前后指针防止 UObject GC 时误引用。
// 调用者：UObject 销毁路径（GC 或 MarkPendingKill 后）。
void UNavigationGraphNodeComponent::BeginDestroy()
{
	Super::BeginDestroy();
	
	// 让前驱指向我的后继
	if (PrevNodeComponent != NULL)
	{
		PrevNodeComponent->NextNodeComponent = NextNodeComponent;
	}

	// 让后继指向我的前驱
	if (NextNodeComponent != NULL)
	{
		NextNodeComponent->PrevNodeComponent = PrevNodeComponent;
	}

	// 彻底清空自身链接，避免被 GC 期间再次访问
	NextNodeComponent = NULL;
	PrevNodeComponent = NULL;
}

//----------------------------------------------------------------------//
// ANavigationGraphNode
//----------------------------------------------------------------------//
ANavigationGraphNode::ANavigationGraphNode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

//----------------------------------------------------------------------//
// ANavigationGraph
//----------------------------------------------------------------------//

// 构造函数：仅在非 CDO（真正放进世界的实例）上创建 FNavGraphGenerator。
// 避免 Class Default Object 也跑生成器逻辑。当前 FNavGraphGenerator 是空实现。
ANavigationGraph::ANavigationGraph(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		NavDataGenerator = MakeShareable(new FNavGraphGenerator(this));
	}
}

