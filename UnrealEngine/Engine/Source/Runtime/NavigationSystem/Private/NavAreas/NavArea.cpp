// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// 文件概览：
//   UNavArea 基类的实现：构造默认值、向 NavigationSystem 注册/反注册自身、
//   以及属性变更时的回调处理。
//
// 关键实现要点：
//   - 仅 CDO（Class Default Object）真正参与注册；普通实例不会被 NavigationSystem 使用。
//   - RegisterArea 做两件事：
//       1) 迁移 Legacy 的 bSupportsAgentN 位到 SupportedAgents（只迁移一次）。
//       2) 上报到 UNavigationSystemV1::RequestAreaRegistering。
//   - AreaFlags==0 == UNWALKABLE（Recast 规则），所以基类默认设为 1 保证可通行。
// =============================================================================

#include "NavAreas/NavArea.h"
#include "NavigationSystem.h"
#include "Modules/ModuleManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavArea)

// 默认构造：给出基础可通行 Area 的数值——Cost=1，无进入代价，粉色，全 Agent 支持。
UNavArea::UNavArea(const FObjectInitializer& ObjectInitializer) 
	: Super(ObjectInitializer)
{
	DefaultCost = 1.f;
	FixedAreaEnteringCost = 0.f;
	DrawColor = FColor::Magenta;
	// 旧字段 union 别名：一次性把 16 个 Agent 位全置 1（默认全支持）。
	SupportedAgentsBits = 0xffffffff;
	// NOTE! AreaFlags == 0 means UNWALKABLE!
	// 注意：AreaFlags==0 代表"不可通行"；基类默认写 1 表示可走。
	AreaFlags = 1;
}

// 析构：若本对象是 CDO（引擎里只有一份），要主动告知 NavigationSystem 反注册这个 Area 类。
// 过滤条件：IsReloadActive 表示热重载过程中不要触发反注册（等 Reload 完再注册一次）。
void UNavArea::FinishDestroy()
{
	if (HasAnyFlags(RF_ClassDefaultObject) && !IsReloadActive())
	{
		UNavigationSystemV1::RequestAreaUnregistering(GetClass());
	}

	Super::FinishDestroy();
}

// 加载阶段完成后再次尝试注册——给从磁盘反序列化的蓝图派生类一个机会。
void UNavArea::PostLoad()
{
	Super::PostLoad();
	RegisterArea();
}

// 构造期属性就绪后首次注册（C++ 静态类的 CDO 走这条路径）。
void UNavArea::PostInitProperties()
{
	Super::PostInitProperties();
	RegisterArea();
}

// 注册核心：
//   1) 把 Legacy 的 bSupportsAgentN 位迁移到 SupportedAgents（仅未初始化时）。
//   2) 若本对象是 CDO 且属性已初始化完毕，向 NavigationSystem 登记。
void UNavArea::RegisterArea()
{
	// 迁移 Legacy Agent 位（UE4 老版本用这种方式存储）。
	if (!SupportedAgents.IsInitialized())
	{
		SupportedAgents.bSupportsAgent0 = bSupportsAgent0;
		SupportedAgents.bSupportsAgent1 = bSupportsAgent1;
		SupportedAgents.bSupportsAgent2 = bSupportsAgent2;
		SupportedAgents.bSupportsAgent3 = bSupportsAgent3;
		SupportedAgents.bSupportsAgent4 = bSupportsAgent4;
		SupportedAgents.bSupportsAgent5 = bSupportsAgent5;
		SupportedAgents.bSupportsAgent6 = bSupportsAgent6;
		SupportedAgents.bSupportsAgent7 = bSupportsAgent7;
		SupportedAgents.bSupportsAgent8 = bSupportsAgent8;
		SupportedAgents.bSupportsAgent9 = bSupportsAgent9;
		SupportedAgents.bSupportsAgent10 = bSupportsAgent10;
		SupportedAgents.bSupportsAgent11 = bSupportsAgent11;
		SupportedAgents.bSupportsAgent12 = bSupportsAgent12;
		SupportedAgents.bSupportsAgent13 = bSupportsAgent13;
		SupportedAgents.bSupportsAgent14 = bSupportsAgent14;
		SupportedAgents.bSupportsAgent15 = bSupportsAgent15;
		SupportedAgents.MarkInitialized();
	}

	// 分支：仅 CDO 注册；且蓝图派生类属性未加载完时不要注册（PostLoad 会再调用一次）。
	if (HasAnyFlags(RF_ClassDefaultObject) && 
		!HasAnyFlags(RF_NeedInitialization)  // Don't register BP Area that has still not finished loaded their properties, it was also try again to register later via UNavArea::PostLoad()
		&& !IsReloadActive()
		)
	{
		UNavigationSystemV1::RequestAreaRegistering(GetClass());
	}
}

// 序列化钩子：保存前确保 SupportedAgents 已"MarkInitialized"，避免磁盘里留下未初始化状态，
// 下次加载时误入 Legacy 迁移路径覆盖已有值。
void UNavArea::Serialize(FArchive& Ar)
{
	if (Ar.IsSaving() && !SupportedAgents.IsInitialized())
	{
		SupportedAgents.MarkInitialized();
	}
		
	Super::Serialize(Ar);
}

// 静态工具：从一个 Area 类快速取颜色（NavMesh 调试绘制用）。类为空返回黑色。
FColor UNavArea::GetColor(UClass* AreaDefinitionClass)
{
	return AreaDefinitionClass ? AreaDefinitionClass->GetDefaultObject<UNavArea>()->DrawColor : FColor::Black;
}

// 从另一 Area 类拷贝数值型属性——Cost / Flag / Color，但刻意不复制 SupportedAgents
// （后者由各 Area 类独立控制，避免互相覆盖）。
void UNavArea::CopyFrom(TSubclassOf<UNavArea> AreaClass)
{
	if (AreaClass)
	{
		UNavArea* DefArea = (UNavArea*)AreaClass->GetDefaultObject();

		DefaultCost = DefArea->DefaultCost;
		FixedAreaEnteringCost = DefArea->GetFixedAreaEnteringCost();
		AreaFlags = DefArea->GetAreaFlags();
		DrawColor = DefArea->DrawColor;

		// don't copy supported agents bits
	}
}

#if WITH_EDITOR
// 编辑器属性修改：当 CDO 的 Cost/SupportedAgents 被改后，需要先反注册再重新注册，
// 让 NavigationSystem 重新分配 AreaId 并广播给下游生成器。
void UNavArea::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName NAME_DefaultCost = GET_MEMBER_NAME_CHECKED(UNavArea, DefaultCost);
	static const FName NAME_FixedAreaEnteringCost = GET_MEMBER_NAME_CHECKED(UNavArea, FixedAreaEnteringCost);
	static const FName NAME_SupportedAgents = GET_MEMBER_NAME_CHECKED(UNavArea, SupportedAgents);

	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (HasAnyFlags(RF_ClassDefaultObject) && !IsReloadActive())
	{
		const FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;
		const FName MemberPropertyName = (PropertyChangedEvent.MemberProperty != nullptr) ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

		// 分支：仅对业务意义上会影响寻路的属性触发重注册，避免颜色/绘制属性变更也引发重建。
		if (PropertyName == NAME_DefaultCost
			|| PropertyName == NAME_FixedAreaEnteringCost
			|| MemberPropertyName == NAME_SupportedAgents)
		{
			UNavigationSystemV1::RequestAreaUnregistering(GetClass());
			RegisterArea();
		}
	}
}
#endif // WITH_EDITOR

