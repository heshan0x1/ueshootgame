// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   本文件为 NavigationSystem 模块调试可视化层提供最基础的类型别名。
//   仅在启用调试绘制（非 Shipping 构建）时，将场景代理的 Mesh 结构
//   暴露为 NavMesh/NavLink 调试绘制组件可以复用的类型 FNavDebugMeshData。
//
// 核心类型：
//   FNavDebugMeshData —— FDebugRenderSceneProxy::FMesh 的类型别名，
//   供导航相关 Rendering Component（例如 NavMesh/NavLink 渲染代理）
//   构造调试三角面片。
//
// 与其它文件的关系：
//   - 被 NavMeshRenderingComponent / NavLinkRenderingProxy 等调试绘制代码引用。
//   - 不含任何业务逻辑，只是将 Engine 层调试绘制类型引入到 NavSys 命名空间。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "EngineDefines.h"


// 仅在开启调试绘制时才提供类型别名；Shipping 构建直接剔除以避免打包调试代码。
#if UE_ENABLE_DEBUG_DRAWING
#include "DebugRenderSceneProxy.h"

// 导航调试 Mesh 数据：直接复用 Engine 调试渲染代理的 FMesh（顶点/索引/颜色）。
typedef FDebugRenderSceneProxy::FMesh FNavDebugMeshData;
#endif //!UE_BUILD_SHIPPING