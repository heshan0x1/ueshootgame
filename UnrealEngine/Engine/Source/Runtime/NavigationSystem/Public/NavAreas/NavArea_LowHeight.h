// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   UNavArea_LowHeight —— 低顶棚/空间不足区域。
//   Recast 在 Heightfield Ledge Filter 阶段判定"空间高度 < AgentHeight"的
//   范围会标为此 Area；不能被任何 Agent 穿越（AreaFlags=0，DefaultCost=BIG_NUMBER）。
//
// 识别：IsLowArea() 返回 true，是 Recast 过滤器识别"低区"的标志。
// =============================================================================

#pragma once

#include "AI/Navigation/NavigationTypes.h"
#include "CoreMinimal.h"
#include "NavAreas/NavArea.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "NavArea_LowHeight.generated.h"

class UObject;

/** Special area that can be generated in spaces with insufficient free height above. Cannot be traversed by anyone. */
// 低空区：可见但不可通行；识别标记靠 IsLowArea()==true。
UCLASS(Config = Engine, MinimalAPI)
class UNavArea_LowHeight : public UNavArea
{
	GENERATED_UCLASS_BODY()
public:
	// 覆盖基类：告诉 Recast/Filter 这是"低区"，启用对应的屏蔽逻辑。
	virtual bool IsLowArea() const override { return true; }
};
