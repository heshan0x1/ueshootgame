// Copyright Epic Games, Inc. All Rights Reserved.

// ============================================================================
// 文件概览：LinkGenerationConfig.cpp
// ----------------------------------------------------------------------------
// FNavLinkGenerationJumpConfig 与其弃用前身 FNavLinkGenerationJumpDownConfig 的实现：
//   - 构造函数：默认把上下行 Area 都设为 UNavArea_Default；
//   - Serialize：在加载时把老字段 AreaClass_DEPRECATED 迁移到新字段（兼容处理）；
//   - CopyToDetourConfig：把 UE 侧参数搬到 Detour 的 dtNavLinkBuilderJumpConfig
//     结构里，交给 Recast NavLink 构建器使用。
// ============================================================================

#include "NavMesh/LinkGenerationConfig.h"
#include "BaseGeneratedNavLinksProxy.h"
#include "NavAreas/NavArea_Default.h"

#if WITH_RECAST
#include "Detour/DetourNavLinkBuilderConfig.h"
#endif //WITH_RECAST

// Deprecated FNavLinkGenerationJumpDownConfig

// Refer to header for more information

#include UE_INLINE_GENERATED_CPP_BY_NAME(LinkGenerationConfig)
PRAGMA_DISABLE_DEPRECATION_WARNINGS
// Deprecated
// 弃用结构构造：默认双向 Area 都为 Default
FNavLinkGenerationJumpDownConfig::FNavLinkGenerationJumpDownConfig()
{
	DownDirectionAreaClass = UNavArea_Default::StaticClass();
	UpDirectionAreaClass = UNavArea_Default::StaticClass();
}

// Deprecated
// 自定义序列化：优先走 UScriptStruct 的 TaggedProperties，再迁移旧字段
bool FNavLinkGenerationJumpDownConfig::Serialize(FArchive& Ar)
{
	UScriptStruct* const Struct = StaticStruct();
	Struct->SerializeTaggedProperties(Ar, (uint8*)this, Struct, nullptr);

#if WITH_EDITORONLY_DATA
	// 加载时把已弃用的 AreaClass 迁移到上下行 AreaClass，并清空旧字段
	if (Ar.IsLoading())
	{
		if (AreaClass_DEPRECATED)
		{
			DownDirectionAreaClass = AreaClass_DEPRECATED;
			UpDirectionAreaClass = AreaClass_DEPRECATED;
			AreaClass_DEPRECATED = nullptr;
		}
	}
#endif //WITH_EDITORONLY_DATA

	return true;
}

#if WITH_RECAST
// Deprecated
// 把 UE 侧参数原样拷贝给 Detour 版本（字段一一对应，无单位换算）。
// linkUserId 使用 Proxy 的稳定 ID，便于 Detour 回调定位到 UObject。
void FNavLinkGenerationJumpDownConfig::CopyToDetourConfig(dtNavLinkBuilderJumpDownConfig& OutDetourConfig) const
{
	OutDetourConfig.enabled = bEnabled;
	OutDetourConfig.jumpLength = JumpLength;
	OutDetourConfig.jumpDistanceFromEdge = JumpDistanceFromEdge;
	OutDetourConfig.jumpMaxDepth = JumpMaxDepth;
	OutDetourConfig.jumpHeight = JumpHeight;
	OutDetourConfig.jumpEndsHeightTolerance	= JumpEndsHeightTolerance;
	OutDetourConfig.samplingSeparationFactor = SamplingSeparationFactor;
	OutDetourConfig.filterDistanceThreshold = FilterDistanceThreshold;
	OutDetourConfig.linkBuilderFlags = LinkBuilderFlags;

	// 若有代理则把 64bit ID 透传给 Detour，运行时 isLinkAllowed 等回调据此找到 Proxy
	if (LinkProxy)
	{
		OutDetourConfig.linkUserId = LinkProxy->GetId().GetId();	
	}
}
#endif //WITH_RECAST
PRAGMA_ENABLE_DEPRECATION_WARNINGS


// Refer to header for more information
PRAGMA_DISABLE_DEPRECATION_WARNINGS
// 现行结构构造：默认上下行 Area 都走 Default；用户若要分离需在编辑器改
FNavLinkGenerationJumpConfig::FNavLinkGenerationJumpConfig()
{
	DownDirectionAreaClass = UNavArea_Default::StaticClass();
	UpDirectionAreaClass = UNavArea_Default::StaticClass();
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

// 序列化 + 旧字段迁移，逻辑同上
bool FNavLinkGenerationJumpConfig::Serialize(FArchive& Ar)
{
	UScriptStruct* const Struct = StaticStruct();
	Struct->SerializeTaggedProperties(Ar, (uint8*)this, Struct, nullptr);

#if WITH_EDITORONLY_DATA
	if (Ar.IsLoading())
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		// 兼容 5.6 之前"单一 AreaClass"设置的老存档
		if (AreaClass_DEPRECATED)
		{
			DownDirectionAreaClass = AreaClass_DEPRECATED;
			UpDirectionAreaClass = AreaClass_DEPRECATED;
			AreaClass_DEPRECATED = nullptr;
		}
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
#endif //WITH_EDITORONLY_DATA

	return true;
}

#if WITH_RECAST

// 拷贝到 Detour NavLink 构建器。被 FRecastTileGenerator 在构建 Tile 时调用，
// 之后 Detour 会沿边缘采样并生成 OffMeshConnection。
void FNavLinkGenerationJumpConfig::CopyToDetourConfig(dtNavLinkBuilderJumpConfig& OutDetourConfig) const
{
	OutDetourConfig.enabled = bEnabled;
	OutDetourConfig.jumpLength = JumpLength;
	OutDetourConfig.jumpDistanceFromEdge = JumpDistanceFromEdge;
	OutDetourConfig.jumpMaxDepth = JumpMaxDepth;
	OutDetourConfig.jumpHeight = JumpHeight;
	OutDetourConfig.jumpEndsHeightTolerance	= JumpEndsHeightTolerance;
	OutDetourConfig.samplingSeparationFactor = SamplingSeparationFactor;
	OutDetourConfig.filterDistanceThreshold = FilterDistanceThreshold;
	OutDetourConfig.linkBuilderFlags = LinkBuilderFlags;

	if (LinkProxy)
	{
		OutDetourConfig.linkUserId = LinkProxy->GetId().GetId();	
	}
}

#endif //WITH_RECAST
