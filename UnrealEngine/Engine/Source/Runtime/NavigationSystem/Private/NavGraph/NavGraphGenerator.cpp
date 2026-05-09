// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavGraphGenerator.cpp （Private）
// -----------------------------------------------------------------------------
// FNavGraphGenerator 的空实现——全部方法都只是占位。
// 说明当前 NavGraph 是未完成特性（见架构文档 Layer 4'）。
// =============================================================================

#include "NavGraph/NavGraphGenerator.h"

//----------------------------------------------------------------------//
// FNavGraphGenerator
//----------------------------------------------------------------------//
// 构造函数：目前不持有 InDestNavGraph 的引用，也不做初始化。
FNavGraphGenerator::FNavGraphGenerator(ANavigationGraph* InDestNavGraph)
{

}

FNavGraphGenerator::~FNavGraphGenerator()
{

}

// 预留：应在这里初始化 InclusionVolumes、生成参数等。
void FNavGraphGenerator::Init()
{

}

// 预留：清理中间数据。
void FNavGraphGenerator::CleanUpIntermediateData()
{

}

// 预留：每轮增量构建/重建时的主逻辑。
void FNavGraphGenerator::UpdateBuilding()
{

}

