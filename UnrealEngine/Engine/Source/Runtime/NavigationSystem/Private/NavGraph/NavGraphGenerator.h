// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavGraphGenerator.h （Private）
// -----------------------------------------------------------------------------
// ANavigationGraph 专用的 FNavDataGenerator 实现骨架。
// 与架构文档中的 Layer 4' 一致，它当前是空壳：
//   - Init / CleanUpIntermediateData / UpdateBuilding 全为空实现
//   - InclusionVolumes 用于描述生成的包围体（预留）
//   - GraphChangingLock 用于日后多线程修改图的临界区（预留）
// 真正的图构建算法尚未落地。
// =============================================================================

#pragma once 

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "AI/NavDataGenerator.h"

class ANavigationGraph;

/**
 * Class that handles generation of the ANavigationGraph data
 */
// ANavigationGraph 的生成器占位实现。真实项目若要使用需要在此处补全。
class FNavGraphGenerator : public FNavDataGenerator
{
public:
	FNavGraphGenerator(ANavigationGraph* InDestNavGraph);
	virtual ~FNavGraphGenerator();

private:
	/** Prevent copying. */
	// 禁止拷贝：生成器一般与 NavData 一对一绑定
	FNavGraphGenerator(FNavGraphGenerator const& NoCopy) { check(0); };
	FNavGraphGenerator& operator=(FNavGraphGenerator const& NoCopy) { check(0); return *this; }

private:
	// Performs initial setup of member variables so that generator is ready to do its thing from this point on
	// 成员变量的初始化入口（当前空）
	void Init();
	// 清理一次性中间数据（当前空）
	void CleanUpIntermediateData();
	// 每次图更新的主逻辑（当前空）
	void UpdateBuilding();

private:
	/** Bounding geometry definition. */
	// 生成范围包围体集合，用于限定"在哪些 AVolume 内部建图"（当前未使用）
	TArray<AVolume const*> InclusionVolumes;

	// 多线程修改图时使用的临界区（当前未使用）
	FCriticalSection GraphChangingLock;
};
