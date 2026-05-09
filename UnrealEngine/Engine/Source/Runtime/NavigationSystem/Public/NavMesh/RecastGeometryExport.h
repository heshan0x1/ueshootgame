// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

// ============================================================================
// 文件概览：RecastGeometryExport.h
// ----------------------------------------------------------------------------
// FRecastGeometryExport：把 UE 里的"可导航几何"（静态/动态 Mesh、高度场、凸包、
// 自定义顶点汤等）按 Recast 体素化所期望的"三角形顶点+索引缓冲"格式扁平输出。
//
// 被谁使用：
//   - Tile 生成阶段：FRecastTileGenerator 收集 FNavigationRelevantData，
//     其中的 CollisionData 就是 Export 生成的字节流（后续再由 ReadCollisionCache
//     还原为 Verts+Indices 供 rcRasterizeTriangles* 使用）。
//   - FNavigationElement / INavRelevantInterface 在 GatherGeometry 时可调用
//     ExportElementGeometry / ExportVertexSoupGeometry 把自身几何送进 NavOctree。
//
// 坐标空间：导出阶段 Verts 保持 UE 坐标；调用 ConvertVertexBufferToRecast
//   才会做 Unreal2Recast (x,y,z)→(-x,z,-y) 的翻转，之后才能被 Recast 光栅化。
// ============================================================================

#include "AI/NavigationSystemHelpers.h"
#include "Engine/EngineTypes.h"

struct FNavigationRelevantData;
struct FNavigationElement;

#if WITH_RECAST

/**
 * Class that handles geometry exporting for Recast navmesh generation.
 */
// 该结构体通过继承 FNavigableGeometryExport 实现"引擎把自身几何推给导航系统"的标准钩子；
// 内部把所有来源的三角形统一堆到 VertexBuffer / IndexBuffer，再 StoreCollisionCache 序列化进 Data。
struct FRecastGeometryExport : public FNavigableGeometryExport
{
	// 构造时必须给定将要写回的 FNavigationRelevantData；所有 Export 结果最终会经 StoreCollisionCache 存入其 CollisionData 字节流。
	NAVIGATIONSYSTEM_API FRecastGeometryExport(FNavigationRelevantData& InData);

	FNavigationRelevantData* Data;                 // 被写入的宿主（FRecastTileGenerator 会读取它的 CollisionData）
	TNavStatArray<FVector::FReal> VertexBuffer;    // 所有导出几何的顶点（UE 坐标，需后续翻转）
	TNavStatArray<int32> IndexBuffer;              // 三角形索引，3 个一组指向 VertexBuffer
	FWalkableSlopeOverride SlopeOverride;          // 某些几何（如 Landscape）允许覆盖斜率过滤阈值

	/** Export the collision of a Chaos triangle mesh into the Vertex and Index buffer. */
	// Chaos 三角网 → 顶点 + 索引（World Space，应用 LocalToWorld）
	NAVIGATIONSYSTEM_API virtual void ExportChaosTriMesh(const Chaos::FTriangleMeshImplicitObject* const TriMesh, const FTransform& LocalToWorld) override;
	/** Export the collision of a Chaos convex mesh into the Vertex and Index buffer. */
	// Chaos 凸包 → 三角形化后进缓冲
	NAVIGATIONSYSTEM_API virtual void ExportChaosConvexMesh(const FKConvexElem* const Convex, const FTransform& LocalToWorld) override;
	/** Export the collision of a Chaos height field into the Vertex and Index buffer. */
	// 整块 Heightfield → 三角面（Landscape 最常见）
	NAVIGATIONSYSTEM_API virtual void ExportChaosHeightField(const Chaos::FHeightField* const Heightfield, const FTransform& LocalToWorld) override;
	/** Export a slice of the collision of a Chaos height field into the Vertex and Index buffer.
	 * @param SliceBox Box that defines the slice we want to extract from the height field */
	// 只取 Heightfield 被 SliceBox 覆盖的那部分，用于 Tile 级"按需导出"大 Landscape
	NAVIGATIONSYSTEM_API virtual void ExportChaosHeightFieldSlice(const FNavHeightfieldSamples& PrefetchedHeightfieldSamples, const int32 NumRows, const int32 NumCols, const FTransform& LocalToWorld, const FBox& SliceBox) override;
	/** Export a custom mesh into the Vertex and Index buffer.*/
	// 把外部顶点三角汤直接注入，适用于 ProceduralMesh / BSP 之类的特殊几何
	NAVIGATIONSYSTEM_API virtual void ExportCustomMesh(const FVector* InVertices, int32 NumVerts, const int32* InIndices, int32 NumIndices, const FTransform& LocalToWorld) override;
	/** Export a rigid body into the Vertex and Index buffer.*/
	// 从 UBodySetup 导出所有碰撞（Box/Sphere/Capsule/Convex/TriMesh），内部会三角化
	NAVIGATIONSYSTEM_API virtual void ExportRigidBodySetup(UBodySetup& BodySetup, const FTransform& LocalToWorld) override;
	/** Add Nav Modifiers to the owned NavigationRelevantData. */
	// 除了几何，某些来源还要向 NavData 附加"区域修饰"（例如标这一块是水域）
	NAVIGATIONSYSTEM_API virtual void AddNavModifiers(const FCompositeNavModifier& Modifiers) override;
	/** Optional delegate for geometry per instance transforms. */
	// 给 HISM / FoliageInstances 之类"一个 BodySetup + N 个 Transform"的几何注入每实例变换委托
	NAVIGATIONSYSTEM_API virtual void SetNavDataPerInstanceTransformDelegate(const FNavDataPerInstanceTransformDelegate& InDelegate) override;

	/** Convert the vertices in VertexBuffer from Unreal to Recast coordinates. */
	// UE(x,y,z) → Recast(-x,z,-y)；StoreCollisionCache 之前必须先翻转
	NAVIGATIONSYSTEM_API void ConvertVertexBufferToRecast();
	/** Store Vertex and Index buffer data in associated FNavigationRelevantData. */
	// 将 VertexBuffer/IndexBuffer 压缩打包进 Data->CollisionData（字节流）
	NAVIGATIONSYSTEM_API void StoreCollisionCache();

	/** Collects the collision information from a navigation element and stores it into the FNavigationRelevantData's CollisionData. */
	// 便捷静态方法：一行完成 "NavigationElement → 所有几何 → CollisionData" 的端到端流程
	static NAVIGATIONSYSTEM_API void ExportElementGeometry(const FNavigationElement& InElement, FNavigationRelevantData& OutData);

	/** Convert a list of vertices into the navigation format and store it into the FNavigationRelevantData's CollisionData.
	 * @param InVerts Array of triangles vertices position. Each triangle will be created from 3 consecutive vertices in the array. Its size must be a multiple a 3.*/
	// 顶点汤（3 个为一组一个三角面）→ 三角索引 + 坐标翻转 + 写入 CollisionData
	static NAVIGATIONSYSTEM_API void ExportVertexSoupGeometry(const TArray<FVector>& InVerts, FNavigationRelevantData& OutData);

	/** Collect the collision information of a BodySetup as a triangle mesh. */
	// 将 BodySetup 统一当"三角网"导出（所有形状被三角化并合并）
	static NAVIGATIONSYSTEM_API void ExportRigidBodyGeometry(UBodySetup& InOutBodySetup,
		TNavStatArray<FVector>& OutVertexBuffer,
		TNavStatArray<int32>& OutIndexBuffer,
		FBox& OutBounds,
		const FTransform& LocalToWorld = FTransform::Identity);

	/** Collect the collision information of a BodySetup as a triangle mesh and a series of convex shapes. */
	// 分离导出：TriMesh 走一份缓冲、Convex 走另一份；ShapeBuffer 用于记录每个 Convex 的顶点起止，便于后续按形状施加 Modifier
	static NAVIGATIONSYSTEM_API void ExportRigidBodyGeometry(
		UBodySetup& InOutBodySetup,
		TNavStatArray<FVector>& OutTriMeshVertexBuffer,
		TNavStatArray<int32>& OutTriMeshIndexBuffer,
		TNavStatArray<FVector>& OutConvexVertexBuffer,
		TNavStatArray<int32>& OutConvexIndexBuffer,
		TNavStatArray<int32>& OutShapeBuffer,
		FBox& OutBounds,
		const FTransform& LocalToWorld = FTransform::Identity);

	/** Collect the collision information of an AggregateGeometry as a series of convex shapes. */
	// 只处理 AggregateGeom（Box/Sphere/Capsule/Convex），全部输出为凸包序列
	static NAVIGATIONSYSTEM_API void ExportAggregatedGeometry(
		const FKAggregateGeom& AggGeom,
		TNavStatArray<FVector>& OutConvexVertexBuffer,
		TNavStatArray<int32>& OutConvexIndexBuffer,
		TNavStatArray<int32>& OutShapeBuffer,
		FBox& OutBounds,
		const FTransform& LocalToWorld = FTransform::Identity);

	/** Transform a list of vertex triplets from Unreal to Recast coordinate and generate an Index buffer. */
	// 把 UE 坐标的三角顶点汤翻转为 Recast 坐标并生成顺序索引
	static NAVIGATIONSYSTEM_API void TransformVertexSoupToRecast(const TArray<FVector>& VertexSoup, TNavStatArray<FVector>& Verts, TNavStatArray<int32>& Faces);

private:
	// 日志用：返回 Data 对应 NavigationElement 的可读名字，便于出错时定位
	FString GetDataOwnerName() const;
};

#endif // WITH_RECAST
