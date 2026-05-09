// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavLinkComponent.cpp
// -----------------------------------------------------------------------------
// 简单 NavLink 组件的实现。要点：
//   - Mobility=Stationary，不产生碰撞/Overlap，仅作为导航信息载体。
//   - Links 数组默认挂一条 Default Area 的链接。
//   - GetNavigationData 把 Links 借 NavigationHelper::ProcessNavLinkAndAppend
//     转成 Modifiers，塞入 FNavigationRelevantData —— Tile 生成时会转成 OffMeshConnection。
//   - 编辑器下属性/撤销/导入后都会重新初始化 Links 的 AreaClass。
// 此文件末尾还内联了 UNavLinkTrivial 的最小构造（仅为给例子用）。
// =============================================================================

#include "NavLinkComponent.h"
#include "NavLinkRenderingProxy.h"
#include "AI/NavigationSystemHelpers.h"
#include "AI/Navigation/NavigationRelevantData.h"
#include "NavigationSystemTypes.h"
#include "NavAreas/NavArea_Default.h"
#include "Engine/CollisionProfile.h"
#include "NavLinkTrivial.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavLinkComponent)


// 构造：禁用碰撞/Overlap，默认放一条 Default Area 的空链接做占位。
UNavLinkComponent::UNavLinkComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	Mobility = EComponentMobility::Stationary;
	BodyInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);

	// 即便没有碰撞，也要把自己视为"有导航几何贡献"——否则 Octree 不会调 GetNavigationData。
	bHasCustomNavigableGeometry = EHasCustomNavigableGeometry::EvenIfNotCollidable;
	bCanEverAffectNavigation = true;
	bNavigationRelevant = true;

	// 默认添加一条 Default Area 的链接，端点留给用户去调
	FNavigationLink DefLink;
	DefLink.SetAreaClass(UNavArea_Default::StaticClass());

	Links.Add(DefLink);
}

// 组件包围盒：取所有链接端点的 AABB 并变换到世界空间。
FBoxSphereBounds UNavLinkComponent::CalcBounds(const FTransform &LocalToWorld) const
{
	FBox LocalBounds(ForceInit);
	// 迭代目标：聚合每条链接的 Left/Right 端点，得到组件局部 AABB
	for (int32 Idx = 0; Idx < Links.Num(); Idx++)
	{
		LocalBounds += Links[Idx].Left;
		LocalBounds += Links[Idx].Right;
	}

	const FBox WorldBounds = LocalBounds.TransformBy(LocalToWorld);
	return FBoxSphereBounds(WorldBounds);
}

// Tile 生成时被 NavOctree 调用：把 Links 转成 Modifier（OffMeshLink）。
void UNavLinkComponent::GetNavigationData(FNavigationRelevantData& Data) const
{
	NavigationHelper::ProcessNavLinkAndAppend(&Data.Modifiers, NavigationHelper::FNavLinkOwnerData(*this), Links);
}

// 至少存在一条链接才算与导航相关
bool UNavLinkComponent::IsNavigationRelevant() const
{
	return Links.Num() > 0;
}

// INavLinkHostInterface：只返回点链接，段链接留空
bool UNavLinkComponent::GetNavigationLinksArray(TArray<FNavigationLink>& OutLink, TArray<FNavigationSegmentLink>& OutSegments) const
{
	OutLink.Append(Links);
	return OutLink.Num() > 0;
}

// 构造调试 SceneProxy，用于编辑器绘制箭头
FPrimitiveSceneProxy* UNavLinkComponent::CreateSceneProxy()
{
	return new FNavLinkRenderingProxy(this);
}

#if WITH_EDITOR

// 编辑器改 Links 属性后重新初始化 AreaClass（把 None 替换为 Default）
void UNavLinkComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// 只有当变更的是 Links 成员时才处理，避免重复工作
	if (PropertyChangedEvent.MemberProperty && PropertyChangedEvent.MemberProperty->GetFName() == GET_MEMBER_NAME_CHECKED(UNavLinkComponent, Links))
	{
		InitializeLinksAreaClasses();
	}
}

// Undo/Redo 后可能把 AreaClass 恢复成 None，这里重新合法化
void UNavLinkComponent::PostEditUndo()
{
	Super::PostEditUndo();

	InitializeLinksAreaClasses();
}

// 拷贝粘贴/从文本导入后也要初始化 AreaClass
void UNavLinkComponent::PostEditImport()
{
	Super::PostEditImport();

	InitializeLinksAreaClasses();
}

#endif // WITH_EDITOR

// 组件注册：基类调用后初始化 Links.AreaClass 以免出现空引用。
void UNavLinkComponent::OnRegister()
{
	Super::OnRegister();

	InitializeLinksAreaClasses();
}

// 把每条 Link 里尚未指定的 AreaClass 替换为默认可行走 Area。
void UNavLinkComponent::InitializeLinksAreaClasses()
{
	for (FNavigationLink& Link : Links)
	{
		Link.InitializeAreaClass();
	}
}

//----------------------------------------------------------------------//
// UNavLinkTrivial
//----------------------------------------------------------------------//
// 最简单的 NavLink 示例：构造时塞一条 Y 轴上的跳跃链接（-100 → +100）。
// 主要用作开发期样板/测试。
UNavLinkTrivial::UNavLinkTrivial(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FNavigationLink& Link = Links[Links.Add(FNavigationLink(FVector(0, 100, 0), FVector(0, -100, 0)))];
}

