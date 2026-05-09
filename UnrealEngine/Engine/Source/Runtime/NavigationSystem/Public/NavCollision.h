// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavCollision.h —— StaticMesh 的"导航专用碰撞"（简化几何）
// -----------------------------------------------------------------------------
// 文件职责：
//   - 声明 UNavCollision：UStaticMesh 资源可以挂一份"导航碰撞"，用于在
//     Recast tile 生成时取代真实的三角形几何，大幅加速体素化；
//   - 支持三种简化几何形态：Cylinder（柱）、Box（盒）、Convex（凸包）；
//   - Convex 支持 Cooked（DDC 缓存），通过 FFormatContainer/FByteBulkData 存储；
//   - 作为"动态障碍物"时还可配置 Area Class（决定 Recast 上的代价/禁行）。
// 核心类：
//   UNavCollision —— 派生自 UNavCollisionBase，被 UStaticMesh 在 PostLoad 时
//   初始化（Setup），把自身几何注册给 FNavigationRelevantData。
// 与其它文件关系：
//   - NavCollisionBase（Engine 模块的接口基类）
//   - NavRelevantInterface（UStaticMeshComponent 导出几何时会调用到）。
//   - DDC 通过 NAVCOLLISION_DERIVEDDATA_VER/FNavCollisionDataReader 访问。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/Guid.h"
#include "Templates/SubclassOf.h"
#include "Serialization/BulkData.h"
#include "AI/Navigation/NavigationTypes.h"
#include "AI/Navigation/NavCollisionBase.h"
#include "NavCollision.generated.h"

class FPrimitiveDrawInterface;
struct FCompositeNavModifier;
struct FNavigableGeometryExport;

// 简化柱体：圆柱形碰撞原语，用于 NavCollision 的快速几何描述。
USTRUCT()
struct FNavCollisionCylinder
{
	GENERATED_USTRUCT_BODY()

	// 相对 StaticMesh 局部原点的偏移（柱体底面中心）
	UPROPERTY(EditAnywhere, Category=Cylinder)
	FVector Offset = FVector::ZeroVector;

	// 柱体半径（uu）
	UPROPERTY(EditAnywhere, Category=Cylinder)
	float Radius = 0.f;

	// 柱体高度（uu），沿 Z 轴向上
	UPROPERTY(EditAnywhere, Category=Cylinder)
	float Height = 0.f;
};

// 简化盒体：轴对齐立方体原语。
USTRUCT()
struct FNavCollisionBox
{
	GENERATED_USTRUCT_BODY()

	// 相对原点的中心偏移
	UPROPERTY(EditAnywhere, Category=Box)
	FVector Offset = FVector::ZeroVector;

	// 半尺寸（中心到面的距离，各轴独立）
	UPROPERTY(EditAnywhere, Category=Box)
	FVector Extent = FVector::ZeroVector;
};

// UNavCollision：StaticMesh 资源的"导航简化碰撞"。
// - CylinderCollision / BoxCollision：用户可编辑的原语列表；
// - ConvexShapeIndices + CookedFormatData：Convex 凸包数据（可从 DDC 取，可运行时 gather）；
// - bIsDynamicObstacle：UNavCollisionBase 里的开关，若为 true 则把自身作为 Modifier 投入 tile 生成。
UCLASS(config=Engine, MinimalAPI)
class UNavCollision : public UNavCollisionBase
{
	GENERATED_UCLASS_BODY()

	// Convex 数据解析后每个 shape 的起始顶点下标（长度 = shape 数量 + 1 的方式存储）
	TNavStatArray<int32> ConvexShapeIndices;

	// 全部几何聚合后的 AABB，供 GetBounds() 使用
	FBox Bounds;

	/** list of nav collision cylinders */
	// 用户可编辑的柱体原语列表
	UPROPERTY(EditAnywhere, Category=Navigation)
	TArray<FNavCollisionCylinder> CylinderCollision;

	/** list of nav collision boxes */
	// 用户可编辑的盒体原语列表
	UPROPERTY(EditAnywhere, Category=Navigation)
	TArray<FNavCollisionBox> BoxCollision;

	/** navigation area type that will be use when this static mesh is used as 
	 *	a navigation obstacle. See bIsDynamicObstacle.
	 *	Empty AreaClass means the default obstacle class will be used */
	// 作为动态障碍物时使用的 NavArea 类（代价/禁行语义）。
	UPROPERTY(EditAnywhere, Category = Navigation, meta = (EditCondition = "bIsDynamicObstacle"))
	TSubclassOf<class UNavArea> AreaClass;

	/** If set, convex collisions will be exported offline for faster runtime navmesh building (increases memory usage) */
	// 是否把 Convex 几何在 Cook 阶段烘焙出来存进 DDC，减少运行时 gather 成本（代价：内存）
	UPROPERTY(EditAnywhere, Category=Navigation, config)
	uint32 bGatherConvexGeometry : 1;

	/** If false, will not create nav collision when connecting as a client */
	// 客户端是否需要本导航碰撞；false 时在 NeedsLoadForClient 返回 false
	UPROPERTY(EditAnywhere, Category=Navigation, config)
	uint32 bCreateOnClient : 1;

	/** if set, convex geometry will be rebuild instead of using cooked data */
	// 被 InvalidateCollision 设为 true，下次 Setup 时跳过 DDC、强制重新从 BodySetup gather
	uint32 bForceGeometryRebuild : 1;

	/** Guid of associated BodySetup */
	// 关联 UBodySetup 的 Guid：DDC Key 的一部分，BodySetup 改变时让缓存失效
	FGuid BodySetupGuid;

	/** Cooked data for each format */
	// 按 Format（通常 "NavCollision"）存放的烘焙 BulkData 容器
	FFormatContainer CookedFormatData;

	//~ Begin UObject Interface.
	NAVIGATIONSYSTEM_API virtual void PostInitProperties() override;
	// Serialize：走 BulkData 把 CookedFormatData 持久化；根据平台/IsCooking 决定读写
	NAVIGATIONSYSTEM_API virtual void Serialize(FArchive& Ar) override;
	// PostLoad：编辑器中若 BodySetup Guid 不一致，触发 InvalidateCollision
	NAVIGATIONSYSTEM_API virtual void PostLoad() override;
	NAVIGATIONSYSTEM_API virtual void GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) override;
	NAVIGATIONSYSTEM_API virtual bool NeedsLoadForTargetPlatform(const class ITargetPlatform* TargetPlatform) const override;
	virtual bool NeedsLoadForClient() const override { return bCreateOnClient; }
	//~ End UObject Interface.

	// Guid 基于 BodySetupGuid + UNavCollision class 版本号
	NAVIGATIONSYSTEM_API FGuid GetGuid() const;

	/** Tries to read data from DDC, and if that fails gathers navigation
	 *	collision data, stores it and uploads to DDC */
	// Setup 逻辑：
	//   1) 用 GetGuid() 询问 DDC；2) 命中 → 用 FNavCollisionDataReader 反序列化 Convex；
	//   3) 未命中 → GatherCollision() 从 BodySetup 的 PxConvex 提取顶点/索引 → 写回 DDC。
	// 由 UStaticMesh::PostLoad 间接调用，也会在 InvalidateCollision 后再次触发。
	NAVIGATIONSYSTEM_API virtual void Setup(class UBodySetup* BodySetup) override;

	NAVIGATIONSYSTEM_API virtual FBox GetBounds() const override;

	/** copy user settings from other nav collision data */
	// 拷贝 Box/Cylinder/AreaClass/bIsDynamicObstacle 等"用户编辑"字段，不拷贝 Cooked 数据
	NAVIGATIONSYSTEM_API void CopyUserSettings(const UNavCollision& OtherData);

	/** show cylinder and box collisions */
	// 编辑器调试绘制（只画简单几何，不画 Convex）
	NAVIGATIONSYSTEM_API virtual void DrawSimpleGeom(FPrimitiveDrawInterface* PDI, const FTransform& Transform, const FColor Color) override;

	/** Get data for dynamic obstacle */
	// bIsDynamicObstacle 为 true 时被 NavOctree 调用，把柱/盒/凸包填进 Modifier
	NAVIGATIONSYSTEM_API virtual void GetNavigationModifier(FCompositeNavModifier& Modifier, const FTransform& LocalToWorld) override;

	/** Export collision data */
	// bIsDynamicObstacle 为 false 时的导出入口，把几何喂给 FNavigableGeometryExport
	NAVIGATIONSYSTEM_API virtual bool ExportGeometry(const FTransform& LocalToWorld, FNavigableGeometryExport& GeoExport) const override;

	/** Read collisions data */
	// 从 UBodySetup 的物理数据提取 Convex 凸包；填充 ConvexShapeIndices 与 CookedFormatData
	NAVIGATIONSYSTEM_API void GatherCollision();

#if WITH_EDITOR
	// 标脏 + 清空 Cooked 数据，下次 Setup 重新 gather；编辑器属性变更/BodySetup 变更时触发
	NAVIGATIONSYSTEM_API virtual void InvalidateCollision() override;
#endif // WITH_EDITOR

protected:
	// 清理 ConvexShapeIndices/CookedFormatData（不清用户字段）
	NAVIGATIONSYSTEM_API void ClearCollision();

#if WITH_EDITOR
	NAVIGATIONSYSTEM_API void InvalidatePhysicsData();
#endif // WITH_EDITOR
	// 按 Format 名取/建 FByteBulkData 槽（写入 DDC 结果会落到这里）
	NAVIGATIONSYSTEM_API FByteBulkData* GetCookedData(FName Format);
};
