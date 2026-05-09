// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavLinkCustomComponent.cpp  ── SmartLink 实现
// -----------------------------------------------------------------------------
// 详细注释见 NavLinkCustomComponent.h 顶端。本文件实现以下关键点：
//   - ID 管理：构造时生成 AuxiliaryCustomLinkId（稳定 Hash 基），OnRegister 时
//              合 Owner Instance GUID 得 CustomLinkId；5.3 前的 NavLinkUserId 会
//              在 Serialize/PostLoad 阶段自动迁移。
//   - Enable/Disable：SetEnabled 只调 NavSys->UpdateCustomLink（切 AreaClass），
//              不会触发整块 Tile 重建 —— 见架构文档 4.5。
//   - 广播：CollectNearbyAgents 做 SphereOverlap（端点距离 < 0.5*BR 时合并一次），
//            再经 OnBroadcastFilter 过滤，可周期触发 (BroadcastInterval)。
//   - 障碍：bCreateBoxObstacle=true 时额外塞一条 Box FAreaNavModifier。
// =============================================================================

#include "NavLinkCustomComponent.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "CollisionQueryParams.h"
#include "WorldCollision.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "NavigationSystem.h"
#include "NavAreas/NavArea_Null.h"
#include "NavAreas/NavArea_Default.h"
#include "AI/NavigationModifier.h"
#include "NavigationOctree.h"
#include "AI/NavigationSystemHelpers.h"
#include "AI/Navigation/PathFollowingAgentInterface.h"
#include "Engine/OverlapResult.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NavLinkCustomComponent)

#if WITH_EDITOR
namespace UE::Navigation::LinkCustomComponent::Private
{
	// 编辑器：AreaClass / ObstacleAreaClass 相关的类注册变化 → 刷新组件
	void OnNavAreaRegistrationChanged(UNavLinkCustomComponent& CustomComponent, const UWorld& World, const UClass* NavAreaClass)
	{
		if (NavAreaClass && (NavAreaClass == CustomComponent.GetLinkAreaClass() || NavAreaClass == CustomComponent.GetObstacleAreaClass()) && &World == CustomComponent.GetWorld())
		{
			CustomComponent.RefreshNavigationModifiers();
		}
	}
} // UE::Navigation::LinkCustomComponent::Private
#endif // WITH_EDITOR

// 构造：默认链接 ±70 沿 X 轴、启用 Default Walkable、禁用 Null。
// AuxiliaryCustomLinkId 用 PathName 做哈希种子，保证跨世界复位仍稳定。
UNavLinkCustomComponent::UNavLinkCustomComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	NavLinkUserId = 0;  // 5.3 前遗留字段，保持默认 0
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	LinkRelativeStart = FVector(70, 0, 0);
	LinkRelativeEnd = FVector(-70, 0, 0);
	LinkDirection = ENavLinkDirection::BothWays;
	EnabledAreaClass = UNavArea_Default::StaticClass();
	DisabledAreaClass = UNavArea_Null::StaticClass();
	ObstacleAreaClass = UNavArea_Null::StaticClass();
	ObstacleExtent = FVector(50, 50, 50);
	bLinkEnabled = true;
	bNotifyWhenEnabled = false;
	bNotifyWhenDisabled = false;
	bCreateBoxObstacle = false;
	BroadcastRadius = 0.0f;
	BroadcastChannel = ECC_Pawn;
	BroadcastInterval = 0.0f;

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		// 用 PathName 生成稳定的辅助 ID（同一蓝图/Actor 在同层级稳定）
		const FString PathName = GetPathName();
		AuxiliaryCustomLinkId = FNavLinkAuxiliaryId::GenerateUniqueAuxiliaryId(*PathName);
	}
}

// Serialize：处理 UE5.2→5.3 ID 升级。旧存档 NavLinkUserId 是 uint32，
// 升级时被包成 FNavLinkId 存入 CustomLinkId。
void UNavLinkCustomComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);

	if (Ar.IsLoading() && Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::NavigationLinkID32To64)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		CustomLinkId = FNavLinkId(NavLinkUserId);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}

// PostLoad：把 CustomLinkId 交给全局唯一 ID 注册表刷新（防止加载的 ID 与运行时新生成 ID 冲突）
void UNavLinkCustomComponent::PostLoad()
{
	Super::PostLoad();

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	INavLinkCustomInterface::UpdateUniqueId(CustomLinkId);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

#if WITH_EDITOR
// This function is only called if GIsEditor == true for non default objects components that are registered.
void UNavLinkCustomComponent::OnNavAreaRegistered(const UWorld& World, const UClass* NavAreaClass)
{
	UE::Navigation::LinkCustomComponent::Private::OnNavAreaRegistrationChanged(*this, World, NavAreaClass);
}

// This function is only called if GIsEditor == true for non default objects components that are registered.
void UNavLinkCustomComponent::OnNavAreaUnregistered(const UWorld& World, const UClass* NavAreaClass)
{
	UE::Navigation::LinkCustomComponent::Private::OnNavAreaRegistrationChanged(*this, World, NavAreaClass);
}
#endif // WITH_EDITOR

// 构造脚本重跑期间保存 ID —— 否则每次 Rerun 会重生 ID，导致 NavMesh 上的 OffMeshConnection 被标失效。
TStructOnScope<FActorComponentInstanceData> UNavLinkCustomComponent::GetComponentInstanceData() const
{
	TStructOnScope<FActorComponentInstanceData> InstanceData = MakeStructOnScope<FActorComponentInstanceData, FNavLinkCustomInstanceData>(this);
	FNavLinkCustomInstanceData* NavLinkCustomInstanceData = InstanceData.Cast<FNavLinkCustomInstanceData>();
	NavLinkCustomInstanceData->CustomLinkId = CustomLinkId;
	NavLinkCustomInstanceData->AuxiliaryCustomLinkId = AuxiliaryCustomLinkId;
	return InstanceData;
}

// 把保存的 ID 还原到组件。若 ID 发生变化且组件已注册，需要先反注册旧 ID 再注册新 ID。
void UNavLinkCustomComponent::ApplyComponentInstanceData(FNavLinkCustomInstanceData* NavLinkData)
{
	check(NavLinkData);

	if (CustomLinkId != NavLinkData->CustomLinkId
		|| AuxiliaryCustomLinkId != NavLinkData->AuxiliaryCustomLinkId)
	{
		// Registered component has its link registered in the navigation system. 
		// In such case, we need to unregister current Id and register with the Id from the instance data.
		const bool bIsLinkRegistrationUpdateRequired = IsRegistered();

		if (bIsLinkRegistrationUpdateRequired)
		{
			UNavigationSystemV1::RequestCustomLinkUnregistering(*this, this);
		}

		AuxiliaryCustomLinkId = NavLinkData->AuxiliaryCustomLinkId;
		CustomLinkId = NavLinkData->CustomLinkId;

		if (bIsLinkRegistrationUpdateRequired)
		{
			UNavigationSystemV1::RequestCustomLinkRegistering(*this, this);
		}
	}
}

#if WITH_EDITOR
// 从其它 Actor 粘贴过来时：重新生成 Auxiliary ID + 置空 CustomLinkId，交给 OnRegister 重分配
void UNavLinkCustomComponent::PostEditImport()
{
	Super::PostEditImport();

	// Generate a new AuxiliarLinkUserId and set CustomLinkId to Invalid, this is then inline with the constructor in this regard.
	// CustomLinkId is set to a valid Id later in OnRegister().
	const FString PathName = GetPathName();
	AuxiliaryCustomLinkId = FNavLinkAuxiliaryId::GenerateUniqueAuxiliaryId(*PathName);

	CustomLinkId = FNavLinkId::Invalid;
}
#endif // WITH_EDITOR

// INavLinkCustomInterface：返回 *相对* Owner 的端点（外部再做变换）
void UNavLinkCustomComponent::GetLinkData(FVector& LeftPt, FVector& RightPt, ENavLinkDirection::Type& Direction) const
{
	LeftPt = LinkRelativeStart;
	RightPt = LinkRelativeEnd;
	Direction = LinkDirection;
}

void UNavLinkCustomComponent::GetSupportedAgents(FNavAgentSelector& OutSupportedAgents) const
{
	OutSupportedAgents = SupportedAgents;
}

// 核心：根据当前启用状态返回 Enabled 或 Disabled 的 Area 类。
// 这也是 "Enable/Disable 不重建 Tile" 的关键 —— 只切 Area，不改几何。
TSubclassOf<UNavArea> UNavLinkCustomComponent::GetLinkAreaClass() const
{
	return bLinkEnabled ? EnabledAreaClass : DisabledAreaClass;
}

FNavLinkAuxiliaryId UNavLinkCustomComponent::GetAuxiliaryId() const
{
	return AuxiliaryCustomLinkId;
}

FNavLinkId UNavLinkCustomComponent::GetId() const
{
	return CustomLinkId;
}

// NavigationSystem 批量重分 ID 时调用（例如两条链接 ID 撞车需要让一条改掉）。
void UNavLinkCustomComponent::UpdateLinkId(FNavLinkId NewUniqueId)
{
	if (NewUniqueId == CustomLinkId)
	{
		return;
	}

#if WITH_EDITOR
	UE_CLOG(NewUniqueId.IsLegacyId(), LogNavLink, Verbose, TEXT("%hs Navlink using LinkUserId id %llu [%s] is being updated with old style link ID. Please move to the new system. See FNavLinkId::GenerateUniqueId()"), __FUNCTION__, CustomLinkId.GetId(), *GetFullName());
#endif

	CustomLinkId = NewUniqueId;

#if WITH_EDITOR
	// 让编辑器知道这个组件被改动了，需要保存
	Modify(true);
#endif
}

// 寻路过滤回调：默认允许所有 Querier 使用该链接。派生类可覆盖实现"仅 AI 可用/仅玩家可用"等。
bool UNavLinkCustomComponent::IsLinkPathfindingAllowed(const UObject* Querier) const
{
	return true;
}

// Agent 到达链接起点：把它加入 MovingAgents；若绑了回调则执行并告知 PathFollowing "我来接管"。
bool UNavLinkCustomComponent::OnLinkMoveStarted(UObject* PathComp, const FVector& DestPoint)
{
	MovingAgents.Add(MakeWeakObjectPtr(PathComp));

	if (OnMoveReachedLink.IsBound())
	{
		OnMoveReachedLink.Execute(this, PathComp, DestPoint);
		return true;
	}

	return false;
}

// 结束时从 MovingAgents 移除（弱引用，本身也能自动失效）
void UNavLinkCustomComponent::OnLinkMoveFinished(UObject* PathComp)
{
	MovingAgents.Remove(MakeWeakObjectPtr(PathComp));
}

// 给 Tile 生成：填入 1 条 FNavigationLink（点对点）+ 可选的 Box Obstacle 作为 Area Modifier。
void UNavLinkCustomComponent::GetNavigationData(FNavigationRelevantData& Data) const
{
	TArray<FNavigationLink> NavLinks;
	FNavigationLink LinkMod = GetLinkModifier();
	// 清掉默认的下落/投影参数：SmartLink 端点来自 Owner 变换，不走自动投影
	LinkMod.MaxFallDownLength = 0.f;
	LinkMod.LeftProjectHeight = 0.f;
	NavLinks.Add(LinkMod);
	NavigationHelper::ProcessNavLinkAndAppend(&Data.Modifiers, GetOwner(), NavLinks);

	// 可选的"门缝障碍"：在链接端点附近挖一块 Null Area，逼迫寻路只能走链接本身
	if (bCreateBoxObstacle)
	{
		Data.Modifiers.Add(FAreaNavModifier(FBox::BuildAABB(ObstacleOffset, ObstacleExtent), GetOwner()->GetTransform(), ObstacleAreaClass));
	}
}

// Bounds：两端点 + Obstacle 的并集（世界空间）
void UNavLinkCustomComponent::CalcAndCacheBounds() const
{
	Bounds = FBox(ForceInit);
	Bounds += GetStartPoint();
	Bounds += GetEndPoint();

	if (bCreateBoxObstacle)
	{
		FBox ObstacleBounds = FBox::BuildAABB(ObstacleOffset, ObstacleExtent);
		Bounds += ObstacleBounds.TransformBy(GetOwner()->GetTransform());
	}
}

// 组件注册：生成/确认 CustomLinkId，并把自己登记到 NavigationSystem。
// ID 生成策略分两路：
//   编辑器：确定性 ID（Auxiliary + ActorInstanceGuid）——保证 Level Instance 多份不冲突
//   运行时：若 ID 仍为 Invalid，使用 (Auxiliary + 随机 GUID) 生成，运行时级联 RequestCustomLinkRegistering 内部再做冲突处理
void UNavLinkCustomComponent::OnRegister()
{
	Super::OnRegister();

	// Actor::GetActorInstanceGuid() is only available when WITH_EDITOR is valid.
#if WITH_EDITOR
	// Do not convert old Ids.
	// 分支：仅当 ID 无效 或 使用新式 ID 时才生成；旧式 ID 留给 PostLoad 升级逻辑处理
	if (CustomLinkId == FNavLinkId::Invalid || CustomLinkId.IsLegacyId() == false)
	{
		// Either this is a freshly spawned component or a component loaded after AuxiliaryCustomLinkId has been saved (and is a new style Id), so we can safely use this to generate a new id.
		// 
		const AActor* Owner = GetOwner();
		checkf(Owner, TEXT("We must have an Owner here as we need it to create a unique id."));

		const FNavLinkId OldCustomLinkId = CustomLinkId;

		// We calculate the CustomLinkId deterministically every time we call OnRegister(), as Level Instances with Actors with UNavLinkCustomComponents can not serialize data for each 
		// duplicated UNavLinkCustomComponent in different Level Instances (in the editor world). 
		// For cooked data this is not a problem as the Level Instances are expanded in to actual instances of actors and components so we can cook the in game value for the CustomLinkId.
		// 关键：每次 OnRegister 都重新确定性计算——因为 Level Instance 里的组件没法单独序列化出新 ID
		CustomLinkId = FNavLinkId::GenerateUniqueId(AuxiliaryCustomLinkId, Owner->GetActorInstanceGuid());
		UE_LOG(LogNavLink, VeryVerbose, TEXT("%hs navlink id generated %llu [%s]."), __FUNCTION__, CustomLinkId.GetId(), *GetFullName());

		if (OldCustomLinkId != CustomLinkId)
		{
			Modify(true);
		}
	}
#else // !WITH_EDITOR
	// There is an edge case here for run time only level instances in that they will have a valid CustomLinkId but it will be from the level instance so Ids will get duplicated
	// in different level instances. Currently baked in nav mesh is not supported for these level instances and its anticipated nav mesh will need to be dynamically regenerated
	// when the level instance is spawned. This means the CustomLinkId does not need to be deterministic as its not relating to anything generated outside of the run time.
	// This clash gets handled via RequestCustomLinkRegistering() in UNavigationSystemV1::RegisterCustomLink(), not in the next section of code!

	// This will only be true for freshly spawned components in game that are not in level instances.
	// 运行时路径：仅在首次 spawn 且尚未分配时生成；冲突解决在 RequestCustomLinkRegistering 内部
	if (CustomLinkId == FNavLinkId::Invalid)
	{
		CustomLinkId = FNavLinkId::GenerateUniqueId(AuxiliaryCustomLinkId, FGuid::NewGuid());

		UE_LOG(LogNavLink, VeryVerbose, TEXT("%hs incremental navlink id generated %llu [%s]."), __FUNCTION__, CustomLinkId.GetId(), *GetFullName());
	}
#endif // WITH_EDITOR

	// 登记到 NavigationSystem：走 NavigationObjectRepository → UNavigationSystemV1::RegisterCustomLink
	UNavigationSystemV1::RequestCustomLinkRegistering(*this, this);

#if WITH_EDITOR
	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		OnNavAreaRegisteredDelegateHandle = UNavigationSystemBase::OnNavAreaRegisteredDelegate().AddUObject(this, &UNavLinkCustomComponent::OnNavAreaRegistered);
		OnNavAreaUnregisteredDelegateHandle = UNavigationSystemBase::OnNavAreaUnregisteredDelegate().AddUObject(this, &UNavLinkCustomComponent::OnNavAreaUnregistered);
	}
#endif // WITH_EDITOR 
}

// 反注册：对称解绑 + 从 NavigationSystem 移除
void UNavLinkCustomComponent::OnUnregister()
{
	Super::OnUnregister();

#if WITH_EDITOR
	if (GIsEditor && !HasAnyFlags(RF_ClassDefaultObject))
	{
		UNavigationSystemBase::OnNavAreaRegisteredDelegate().Remove(OnNavAreaRegisteredDelegateHandle);
		UNavigationSystemBase::OnNavAreaUnregisteredDelegate().Remove(OnNavAreaUnregisteredDelegateHandle);
	}
#endif // WITH_EDITOR 

	UNavigationSystemV1::RequestCustomLinkUnregistering(*this, this);
}

// 修改端点/方向：Bounds 会变，必须重新算并刷新 Octree 项
void UNavLinkCustomComponent::SetLinkData(const FVector& RelativeStart, const FVector& RelativeEnd, ENavLinkDirection::Type Direction)
{
	LinkRelativeStart = RelativeStart;
	LinkRelativeEnd = RelativeEnd;
	LinkDirection = Direction;

	// Link start and end positions have changed, we need to update the bounds
	UpdateNavigationBounds();

	RefreshNavigationModifiers();
}

FNavigationLink UNavLinkCustomComponent::GetLinkModifier() const
{
	return INavLinkCustomInterface::GetModifier(this);
}

// SetEnabledArea / SetDisabledArea：仅在对应状态被激活时才去 NavSys 更新（性能考量）。
// 这里只改 Area、不重建 Tile —— NavSys.UpdateCustomLink 最终只改 Polygon 的 Area flag。
void UNavLinkCustomComponent::SetEnabledArea(TSubclassOf<UNavArea> AreaClass)
{
	EnabledAreaClass = AreaClass;
	if (IsNavigationRelevant() && bLinkEnabled)
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (NavSys)
		{
			NavSys->UpdateCustomLink(this);
		}
	}
}

void UNavLinkCustomComponent::SetDisabledArea(TSubclassOf<UNavArea> AreaClass)
{
	DisabledAreaClass = AreaClass;
	if (IsNavigationRelevant() && !bLinkEnabled)
	{
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (NavSys)
		{
			NavSys->UpdateCustomLink(this);
		}
	}
}

// 开启 Box 障碍：这里会把相关 Bounds 改变，所以需要 Refresh（触发 Octree 重更新）
void UNavLinkCustomComponent::AddNavigationObstacle(TSubclassOf<UNavArea> AreaClass, const FVector& BoxExtent, const FVector& BoxOffset)
{
	ObstacleOffset = BoxOffset;
	ObstacleExtent = BoxExtent;
	ObstacleAreaClass = AreaClass;
	bCreateBoxObstacle = true;

	RefreshNavigationModifiers();
}

void UNavLinkCustomComponent::ClearNavigationObstacle()
{
	ObstacleAreaClass = NULL;
	bCreateBoxObstacle = false;

	RefreshNavigationModifiers();
}

// 核心开关：
//   - 切 bLinkEnabled
//   - UpdateCustomLink 让 NavMesh 上对应 poly 改 Area（不重建 Tile）
//   - 若需要广播，按 bNotifyWhen* 决定触发 BroadcastStateChange
//   - RefreshNavigationModifiers 确保 Obstacle 之类的几何也被同步
void UNavLinkCustomComponent::SetEnabled(bool bNewEnabled)
{
	if (bLinkEnabled != bNewEnabled)
	{
		bLinkEnabled = bNewEnabled;

		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (NavSys)
		{
			NavSys->UpdateCustomLink(this);
		}

		if (GetWorld())
		{
			// 先清掉正在跑的周期性广播定时器
			GetWorld()->GetTimerManager().ClearTimer(TimerHandle_BroadcastStateChange);

			// 按"是否启用 + 是否要通知"两维度决定是否广播
			if ((bLinkEnabled && bNotifyWhenEnabled) || (!bLinkEnabled && bNotifyWhenDisabled))
			{
				BroadcastStateChange();
			}
		}

		RefreshNavigationModifiers();
	}
}

void UNavLinkCustomComponent::SetMoveReachedLink(FOnMoveReachedLink const& InDelegate)
{
	OnMoveReachedLink = InDelegate;
}

// 是否还有弱引用有效的 Agent 正在穿越本链接
bool UNavLinkCustomComponent::HasMovingAgents() const
{
	for (int32 i = 0; i < MovingAgents.Num(); i++)
	{
		if (MovingAgents[i].IsValid())
		{
			return true;
		}
	}

	return false;
}

void UNavLinkCustomComponent::SetBroadcastData(float Radius, ECollisionChannel TraceChannel, float Interval)
{
	BroadcastRadius = Radius;
	BroadcastChannel = TraceChannel;
	BroadcastInterval = Interval;
}

void UNavLinkCustomComponent::SendBroadcastWhenEnabled(bool bEnabled)
{
	bNotifyWhenEnabled = bEnabled;
}

void UNavLinkCustomComponent::SendBroadcastWhenDisabled(bool bEnabled)
{
	bNotifyWhenDisabled = bEnabled;
}

void UNavLinkCustomComponent::SetBroadcastFilter(FBroadcastFilter const& InDelegate)
{
	OnBroadcastFilter = InDelegate;
}

// 找出附近需要通知的 Agent：
//   - 当两个端点相距足够远（>0.5*半径）时，分别在两端做 SphereOverlap 求并；
//   - 否则（链接很短）合并为中点单次 Overlap。
//   - 对命中的 Pawn 找 Controller → PathFollowingAgent → 填 NotifyList。
void UNavLinkCustomComponent::CollectNearbyAgents(TArray<UObject*>& NotifyList)
{
	AActor* MyOwner = GetOwner();
	if (BroadcastRadius < KINDA_SMALL_NUMBER || MyOwner == NULL)
	{
		return;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(SmartLinkBroadcastTrace), false, MyOwner);
	TArray<FOverlapResult> OverlapsL, OverlapsR;

	const FVector LocationL = GetStartPoint();
	const FVector LocationR = GetEndPoint();
	const FVector::FReal LinkDistSq = (LocationL - LocationR).SizeSquared();
	// 距离阈值的平方：取 BroadcastRadius * 0.25 的平方；大于此值才两端分别 Overlap
	const FVector::FReal DistThresholdSq = FMath::Square(BroadcastRadius * 0.25);
	if (LinkDistSq > DistThresholdSq)
	{
		GetWorld()->OverlapMultiByChannel(OverlapsL, LocationL, FQuat::Identity, BroadcastChannel, FCollisionShape::MakeSphere(BroadcastRadius), Params);
		GetWorld()->OverlapMultiByChannel(OverlapsR, LocationR, FQuat::Identity, BroadcastChannel, FCollisionShape::MakeSphere(BroadcastRadius), Params);
	}
	else
	{
		// 链接很短，两端 Overlap 会大量重复——合并成一次中点 Overlap
		const FVector MidPoint = (LocationL + LocationR) * 0.5;
		GetWorld()->OverlapMultiByChannel(OverlapsL, MidPoint, FQuat::Identity, BroadcastChannel, FCollisionShape::MakeSphere(BroadcastRadius), Params);
	}

	// 迭代：把 Pawn 的 Controller 收集起来（去重发生在后面通过 PathFollowingAgent 过滤）
	TArray<AController*> ControllerList;
	for (int32 i = 0; i < OverlapsL.Num(); i++)
	{
		APawn* MovingPawn = OverlapsL[i].OverlapObjectHandle.FetchActor<APawn>();
		if (MovingPawn && MovingPawn->GetController())
		{
			ControllerList.Add(MovingPawn->GetController());
		}
	}
	for (int32 i = 0; i < OverlapsR.Num(); i++)
	{
		APawn* MovingPawn = OverlapsR[i].OverlapObjectHandle.FetchActor<APawn>();
		if (MovingPawn && MovingPawn->GetController())
		{
			ControllerList.Add(MovingPawn->GetController());
		}
	}

	// 最终只把"实现了 PathFollowingAgent"的 Controller 的 PFAgent 加入通知列表
	for (AController* Controller : ControllerList)
	{
		IPathFollowingAgentInterface* PFAgent = Controller->GetPathFollowingAgent();
		UObject* PFAgentObject = Cast<UObject>(PFAgent);
		if (PFAgentObject)
		{
			NotifyList.Add(PFAgentObject);
		}
	}
}

// 真正发射广播：收集附近 Agent → 过滤回调 → 若设置了 BroadcastInterval 则安排下一次
void UNavLinkCustomComponent::BroadcastStateChange()
{
	TArray<UObject*> NearbyAgents;

	CollectNearbyAgents(NearbyAgents);
	OnBroadcastFilter.ExecuteIfBound(this, NearbyAgents);

	// >0 表示周期性广播；=0 则仅一次
	if (BroadcastInterval > 0.0f)
	{
		GetWorld()->GetTimerManager().SetTimer(TimerHandle_BroadcastStateChange, this, &UNavLinkCustomComponent::BroadcastStateChange, BroadcastInterval);
	}
}

// 相对端点 → 世界端点：走 Owner Transform
FVector UNavLinkCustomComponent::GetStartPoint() const
{
	return GetOwner()->GetTransform().TransformPosition(LinkRelativeStart);
}

FVector UNavLinkCustomComponent::GetEndPoint() const
{
	return GetOwner()->GetTransform().TransformPosition(LinkRelativeEnd);
}

