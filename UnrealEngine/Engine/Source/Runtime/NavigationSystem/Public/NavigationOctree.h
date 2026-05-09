// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavigationOctree.h —— 全局导航 Octree 的类型定义
// -----------------------------------------------------------------------------
// 文件职责：
//   - 定义 FNavigationOctreeElement：Octree 里存储的一个单元（Bounds + 指向
//     FNavigationRelevantData 的共享指针 + 源 FNavigationElement 的 SharedRef）；
//   - 定义 FNavigationOctreeSemantics：TOctree2 的特征（叶子最大元素数、深度等）；
//   - 定义 FNavigationOctree：派生自 TOctree2 并维护 ElementHandle ↔ OctreeId
//     反查表，以及"几何导出/脏位标记/惰性采集"的高层接口。
//
// 关键执行流程：参见架构文档 4.2 节。
//   - AddNode：把一个元素放入 Octree（可选同步 gather 几何与 modifier）
//   - AppendToNode：把子元素附着到父节点的 AABB 合并后重新插入
//   - UpdateNode：仅更新 Bounds（Remove + Add 的等价操作）
//   - RemoveNode：移除节点并维护 NodesMemory 统计
//
// 线程约束：
//   Octree 本身通过 TSharedPtr 共享；Recast 生成线程只读 snapshot，
//   写入全部在 GameThread 上进行（进入 PendingUpdates → ProcessPendingOctreeUpdates）。
// =============================================================================

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_4
#include "CoreMinimal.h"
#endif

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5
#include "Stats/Stats.h"
#include "AI/Navigation/NavigationTypes.h"
#include "AI/NavigationModifier.h"
#include "AI/Navigation/NavRelevantInterface.h"
#endif // UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_5

#include "Math/GenericOctreePublic.h"
#include "NavigationSystemTypes.h"
#include "EngineStats.h"
#include "AI/Navigation/NavigationElement.h"
#include "AI/Navigation/NavigationRelevantData.h"
#include "Math/GenericOctree.h"
#include "HAL/LowLevelMemTracker.h"

class INavRelevantInterface;
class FNavigationOctree;
class UActorComponent;
enum class ENavDataGatheringMode : uint8;
struct FNavigationRelevantDataFilter;
typedef FNavigationRelevantDataFilter FNavigationOctreeFilter;

LLM_DECLARE_TAG(NavigationOctree);

// 单个 Octree 元素：Bounds 用于空间索引，Data 承载对导航生成有意义的所有内容
// （几何三角形、Modifier、Link、Area 覆写等）。Data 采用 shared 指针以便异步读取。
struct FNavigationOctreeElement
{
	// Octree 空间索引键：元素的 AABB 外包球。
	FBoxSphereBounds Bounds;
	// 指向实际负载（几何/Modifier 等）的线程安全 shared 指针。
	TSharedRef<FNavigationRelevantData, ESPMode::ThreadSafe> Data;

#if WITH_EDITORONLY_DATA
	UE_DEPRECATED(5.5, "ID no longer used.")
	uint32 OwnerUniqueId = INDEX_NONE;
#endif // WITH_EDITORONLY_DATA

	UE_DEPRECATED(5.5, "Use the constructor taking a FNavigationElement instead.")
	explicit FNavigationOctreeElement(UObject& SourceObject)
		: Data(MakeShared<FNavigationRelevantData>(MakeShared<const FNavigationElement>(SourceObject)))
#if WITH_EDITORONLY_DATA
		, OwnerUniqueId(SourceObject.GetUniqueID())
#endif // WITH_EDITORONLY_DATA
	{
	}

	explicit FNavigationOctreeElement(const TSharedRef<const FNavigationElement>& SourceObject)
		: Bounds(SourceObject.Get().GetBounds())
		, Data(MakeShared<FNavigationRelevantData>(SourceObject))
	{
	}

	FNavigationOctreeElement(const FNavigationOctreeElement& Other)
		: Bounds(Other.Bounds)
		, Data(Other.Data)
	{}

	FNavigationOctreeElement(FNavigationOctreeElement&& Other) noexcept
		: Bounds(MoveTemp(Other.Bounds))
		, Data(MoveTemp(Other.Data))
	{
	}

	FNavigationOctreeElement& operator=(FNavigationOctreeElement&& Other)
	{
		if (this != &Other)
		{
			this->~FNavigationOctreeElement();
			new(this) FNavigationOctreeElement(Forward<FNavigationOctreeElement>(Other));
		}
		return *this;
	}
	
	FNavigationOctreeElement& operator=(const FNavigationOctreeElement& Other)
	{
		if (this != &Other)
		{
			this->~FNavigationOctreeElement();
			new(this) FNavigationOctreeElement(Other);
		}
		return *this;
	}

	FORCEINLINE bool IsEmpty() const
	{
		const FBox BBox = Bounds.GetBox();
		return Data->IsEmpty() && (BBox.IsValid == 0 || BBox.GetSize().IsNearlyZero());
	}

	FORCEINLINE bool IsMatchingFilter(const FNavigationOctreeFilter& Filter) const
	{
		return Data->IsMatchingFilter(Filter);
	}

	/** 
	 *	retrieves Modifier, if it doesn't contain any "Meta Navigation Areas". 
	 *	If it does then retrieves a copy with meta areas substituted with
	 *	appropriate non-meta areas, depending on NavAgent
	 */
	FORCEINLINE FCompositeNavModifier GetModifierForAgent(const FNavAgentProperties* NavAgent = nullptr) const
	{ 
		return Data->GetModifierForAgent(NavAgent);
	}

	FORCEINLINE bool ShouldUseGeometry(const FNavDataConfig& NavConfig) const
	{ 
		return !Data->ShouldUseGeometryDelegate.IsBound() || Data->ShouldUseGeometryDelegate.Execute(&NavConfig);
	}

	FORCEINLINE int32 GetAllocatedSize() const
	{
		return (int32)Data->GetAllocatedSize();
	}

	FORCEINLINE void Shrink()
	{
		Data->Shrink();
	}

	FORCEINLINE void ValidateAndShrink()
	{
		Data->ValidateAndShrink();
	}

	UE_DEPRECATED(5.5, "Use GetSourceElement instead.")
	FORCEINLINE UObject* GetOwner(bool bEvenIfPendingKill = false) const
	{
		return const_cast<UObject*>(Data.Get().SourceElement.Get().GetWeakUObject().Get());
	}

	const TSharedRef<const FNavigationElement>& GetSourceElement() const
	{
		return Data->SourceElement;
	}
};

// Octree 的静态语义描述：给 TOctree2 模板当策略类使用。
struct FNavigationOctreeSemantics
{
	typedef TOctree2<FNavigationOctreeElement, FNavigationOctreeSemantics> FOctree;
	// 每个叶子最多 16 个元素；超出则分裂。
	enum { MaxElementsPerLeaf = 16 };
	// 节点包含的元素低于该值时合并回父节点。
	enum { MinInclusiveElementsPerNode = 7 };
	// 最大深度，避免过度细分导致节点爆炸。
	enum { MaxNodeDepth = 12 };

	typedef TInlineAllocator<MaxElementsPerLeaf> ElementAllocator;

	// TOctree2 调用此钩子取元素 Bounds
	FORCEINLINE static const FBoxSphereBounds& GetBoundingBox(const FNavigationOctreeElement& NavData)
	{
		return NavData.Bounds;
	}

	// 判等以源 FNavigationElement 的 Handle 为准（相同 Handle 视作同一元素）。
	FORCEINLINE static bool AreElementsEqual(const FNavigationOctreeElement& A, const FNavigationOctreeElement& B)
	{
		return A.Data.Get().SourceElement.Get().GetHandle() == B.Data.Get().SourceElement.Get().GetHandle();
	}

	FORCEINLINE static void ApplyOffset(FNavigationOctreeElement& Element, const FVector& InOffset)
	{
		ensureMsgf(false, TEXT("Not implemented yet"));
	}

#if NAVSYS_DEBUG
	FORCENOINLINE 
#endif // NAVSYS_DEBUG
	// TOctree2 插入元素后会回调此函数，告诉我们该元素被分到哪个 ElementId；
	// 我们在此同步更新 ElementToOctreeId 反查表。
	static void SetElementId(FOctree& OctreeOwner, const FNavigationOctreeElement& Element, FOctreeElementId2 Id);
};

// 全局导航 Octree：派生自 TOctree2，额外维护反查表、几何/Modifier 导出委托、
// 统计内存、惰性采集策略等。由 FNavigationOctreeController 持有。
class FNavigationOctree : public TOctree2<FNavigationOctreeElement, FNavigationOctreeSemantics>, public TSharedFromThis<FNavigationOctree, ESPMode::ThreadSafe>
{
public:
	UE_DEPRECATED(5.5, "Use FGeometryExportDelegate.")
	DECLARE_DELEGATE_TwoParams(FNavRelevantGeometryExportDelegate, INavRelevantInterface&, FNavigationRelevantData&);
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FNavRelevantGeometryExportDelegate NavRelevantGeometryExportDelegate;
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	// 几何导出委托：由 UNavigationSystemV1 绑定；用 FNavigationElement 的
	// GeometryExportDelegate/ ExportGeometryDelegate 做实际导出。
	DECLARE_DELEGATE_TwoParams(FGeometryExportDelegate, const FNavigationElement&, FNavigationRelevantData&);
    FGeometryExportDelegate GeometryExportDelegate;

	// 开关：是否把几何三角形数据也存进 Octree（StoreNavGeometry），
	// 否则仅存 Bounds/Modifier。静态网格较多时关闭可大量省内存。
	enum ENavGeometryStoringMode {
		SkipNavGeometry,
		StoreNavGeometry,
	};

	/**
	 * Adds an element to the octree.
	 * @param Element - The element to add.
	 */
	inline void AddElement(const FNavigationOctreeElement& Element)
	{
		LLM_SCOPE_BYTAG(NavigationOctree);

		DEC_MEMORY_STAT_BY(STAT_NavigationMemory, OctreeSizeBytes);
		DEC_MEMORY_STAT_BY(STAT_Navigation_CollisionTreeMemory, OctreeSizeBytes);
		TOctree2<FNavigationOctreeElement, FNavigationOctreeSemantics>::AddElement(Element);
		OctreeSizeBytes = GetSizeBytes();
		INC_MEMORY_STAT_BY(STAT_NavigationMemory, OctreeSizeBytes);
		INC_MEMORY_STAT_BY(STAT_Navigation_CollisionTreeMemory, OctreeSizeBytes);
	}

	/**
	 * Removes an element from the octree.
	 * @param ElementId - The element to remove from the octree.
	 */
	inline void RemoveElement(FOctreeElementId2 ElementId)
	{
		DEC_MEMORY_STAT_BY(STAT_NavigationMemory, OctreeSizeBytes);
		DEC_MEMORY_STAT_BY(STAT_Navigation_CollisionTreeMemory, OctreeSizeBytes);
		TOctree2<FNavigationOctreeElement, FNavigationOctreeSemantics>::RemoveElement(ElementId);
		OctreeSizeBytes = GetSizeBytes();
		INC_MEMORY_STAT_BY(STAT_NavigationMemory, OctreeSizeBytes);
		INC_MEMORY_STAT_BY(STAT_Navigation_CollisionTreeMemory, OctreeSizeBytes);
	}

	NAVIGATIONSYSTEM_API FNavigationOctree(const FVector& Origin, FVector::FReal Radius);
	NAVIGATIONSYSTEM_API virtual ~FNavigationOctree();

	/** Add new node and fill it with navigation export data */
	// 关键入口：把一个 FNavigationOctreeElement 放入 Octree。
	// 内部顺序：
	//   1) 设置 Element.Bounds；2) 按 GatherMode 决定立即 gather 或标记 Pending Lazy；
	//   3) ValidateAndShrink 回收多余内存；4) 调用 AddElement（底层 TOctree2）；
	//   5) TOctree2 通过 FNavigationOctreeSemantics::SetElementId 回调注册 ElementToOctreeId。
	// 调用者：FNavigationDataHandler::AddElementToNavOctree。
	NAVIGATIONSYSTEM_API void AddNode(const FBox& Bounds, FNavigationOctreeElement& OctreeElement);
	UE_DEPRECATED(5.5, "Use the overloaded version with only FBox and FNavigationOctreeElement instead.")
	NAVIGATIONSYSTEM_API void AddNode(UObject* ElementOb, INavRelevantInterface* NavElement, const FBox& Bounds, FNavigationOctreeElement& Data);

	/** Append new data to existing node */
	// 把子元素的数据"合并"到已有节点上（Bounds 并集 + Data 累加）。
	// 典型用途：Actor 是 Parent，组件作为 Child 追加贡献。
	// 实现是 RemoveElement + 合并 + AddElement（因为 TOctree2 不支持原地修改 Bounds）。
	NAVIGATIONSYSTEM_API void AppendToNode(const FOctreeElementId2& Id, const TSharedRef<const FNavigationElement>& ElementRef, const FBox& Bounds, FNavigationOctreeElement& Data);
	UE_DEPRECATED(5.5, "Use the overloaded version with FNavigationElement instead.")
	NAVIGATIONSYSTEM_API void AppendToNode(const FOctreeElementId2& Id, INavRelevantInterface* NavElement, const FBox& Bounds, FNavigationOctreeElement& Data);

	/** Updates element bounds remove/add operation */
	// 只改 Bounds 的等价操作：由于 TOctree2 不支持就地更新，所以先 RemoveElement 再 AddElement。
	NAVIGATIONSYSTEM_API void UpdateNode(const FOctreeElementId2& Id, const FBox& NewBounds);

	/** Remove node */
	// 移除节点：同时减去该节点 GetAllocatedSize 的内存统计。
	// 反查表 ElementToOctreeId 的清理由 FNavigationOctreeController::RemoveNode 兜底。
	NAVIGATIONSYSTEM_API void RemoveNode(const FOctreeElementId2& Id);

	NAVIGATIONSYSTEM_API void SetNavigableGeometryStoringMode(ENavGeometryStoringMode NavGeometryMode);

	NAVIGATIONSYSTEM_API const FNavigationRelevantData* GetDataForID(const FOctreeElementId2& Id) const;

	NAVIGATIONSYSTEM_API FNavigationRelevantData* GetMutableDataForID(const FOctreeElementId2& Id);

	ENavGeometryStoringMode GetNavGeometryStoringMode() const
	{
		return bGatherGeometry ? StoreNavGeometry : SkipNavGeometry;
	}

	NAVIGATIONSYSTEM_API void SetDataGatheringMode(ENavDataGatheringModeConfig Mode);

	// Lazy data gathering methods
	// 查询该元素是否采用惰性 gather（首次真正需要时才 gather）
	NAVIGATIONSYSTEM_API bool IsLazyGathering(const FNavigationElement& NavigationElement) const;
	UE_DEPRECATED(5.5, "Use the overloaded version with FNavigationElement instead.")
	NAVIGATIONSYSTEM_API bool IsLazyGathering(const INavRelevantInterface& ChildNavInterface) const;

	// 立即补采集：若 ElementData 上标记了 bPendingLazyGeometryGathering / bPendingLazyModifiersGathering，
	// 现场调用委托补全；由 Recast tile 生成时遇到懒数据会触发。
	NAVIGATIONSYSTEM_API void DemandLazyDataGathering(FNavigationRelevantData& ElementData);

	NAVIGATIONSYSTEM_API void DemandChildLazyDataGathering(FNavigationRelevantData& ElementData, const FNavigationElement& ChildElement) const;
	UE_DEPRECATED(5.5, "Use the overloaded version with FNavigationElement instead.")
	NAVIGATIONSYSTEM_API void DemandChildLazyDataGathering(FNavigationRelevantData& ElementData, INavRelevantInterface& ChildNavInterface);

	UE_DEPRECATED(5.5, "This method is no longer used by the navigation system.")
	FORCEINLINE static uint32 HashObject(const UObject& Object)
	{
		return Object.GetUniqueID();
	}

#if !UE_BUILD_SHIPPING
	NAVIGATIONSYSTEM_API void SetGatheringNavModifiersTimeLimitWarning(const float Threshold);
#endif // !UE_BUILD_SHIPPING
protected:
	friend struct FNavigationOctreeController;
	friend struct FNavigationOctreeSemantics;

	UE_DEPRECATED(5.5, "Use the version taking a FNavigationElementHandle instead.")
	NAVIGATIONSYSTEM_API void SetElementIdImpl(const uint32 OwnerUniqueId, FOctreeElementId2 Id);
	// 由 FNavigationOctreeSemantics::SetElementId 回调：注册 Handle→Id 的反查关系。
	NAVIGATIONSYSTEM_API void SetElementIdImpl(FNavigationElementHandle ElementHandle, FOctreeElementId2 Id);

#if WITH_EDITORONLY_DATA
	UE_DEPRECATED(5.5, "Use ElementToOctreeId instead.")
	TMap<uint32, FOctreeElementId2> ObjectToOctreeId;
#endif // WITH_EDITORONLY_DATA
	// Handle → OctreeElementId 反查表。由 SetElementIdImpl 写入，RemoveNode 由 Controller 清理。
	// 这张表的"一致性"是 AddNode / RemoveNode / UpdateNode 操作顺序的核心不变量。
	TMap<FNavigationElementHandle, FOctreeElementId2> ElementToOctreeId;
	// 全局默认的几何 gather 模式（当元素本身未指定时使用）
	ENavDataGatheringMode DefaultGeometryGatheringMode;
	// 是否把几何顶点数据存进 Octree（见 ENavGeometryStoringMode）
	uint32 bGatherGeometry : 1;
	// 所有节点当前占用的附加内存（几何/Modifier 的 GetAllocatedSize 累加）
	uint32 NodesMemory;
#if !UE_BUILD_SHIPPING
	// Modifier gather 单次耗时超过此阈值就打 warning（秒），-1 禁用
	float GatheringNavModifiersTimeLimitWarning;
#endif // !UE_BUILD_SHIPPING
private:
	// 缓存 GetSizeBytes()，用于 STAT 计量加减不走完整树遍历
	SIZE_T OctreeSizeBytes = 0;
};
