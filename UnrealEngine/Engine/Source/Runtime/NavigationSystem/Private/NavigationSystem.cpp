// Copyright Epic Games, Inc. All Rights Reserved.

// =============================================================================
// NavigationSystem.cpp  ── 模块最大的 cpp（~6500 行）
// -----------------------------------------------------------------------------
// 本文件是 UNavigationSystemV1 的实现主体。注释范围覆盖：
//   - 文件头 + 关键全局状态（bNavigationAutoUpdateEnabled 等）
//   - UNavigationSystemV1 生命周期：Ctor / PostInitProperties / FinishDestroy /
//                                   ConditionalPopulateNavOctree / ConstructNavOctree
//   - 主 Tick：Tick/RebuildDirtyAreas/PerformAsyncQueries/UpdateInvokers
//   - 寻路：FindPathSync / FindPathAsync / TestPathSync / ProjectPointToNavigation / GetRandomPoint
//   - NavData 注册链：RequestRegistrationDeferred / ProcessRegistrationCandidates /
//                    RegisterNavData / CreateNavigationDataInstanceInLevel
//   - Octree 链：OnComponentRegistered / UpdateComponentInNavOctree /
//               UpdateActorAndComponentsInNavOctree / AddElementToNavOctree
//   - 脏区域：AddDirtyArea / AddDirtyAreas
//   - Invoker：RegisterInvoker / UpdateInvokers / UpdateNavDataActiveTiles / DirtyTilesInBuildBounds
//   - Custom Link：RegisterCustomLink / UpdateCustomLink / RequestCustomLink*
// 架构参考：Runtime/NavigationSystem/DevDocs/NavigationSystem_Architecture_CN.md
// =============================================================================

#include "NavigationSystem.h"
#include "AbstractNavData.h"
#include "AI/NavDataGenerator.h"
#include "AI/Navigation/NavAgentInterface.h"
#include "AI/Navigation/NavigationDataChunk.h"
#include "AI/Navigation/NavigationDirtyArea.h"
#include "AI/Navigation/NavigationDirtyElement.h"
#include "AI/Navigation/NavigationInvokerInterface.h"
#include "AI/Navigation/NavigationInvokerPriority.h"
#include "AI/Navigation/NavigationElement.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "AI/NavigationModifier.h"
#include "Components/PrimitiveComponent.h"
#include "CrowdManagerBase.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Logging/MessageLog.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "NavAreas/NavArea.h"
#include "NavAreas/NavArea_Default.h"
#include "NavAreas/NavArea_Obstacle.h"
#include "NavAreas/NavAreaMeta_SwitchByAgent.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavigationDataHandler.h"
#include "NavigationInvokerComponent.h"
#include "NavigationObjectRepository.h"
#include "NavigationOctree.h"
#include "NavigationPath.h"
#include "NavLinkCustomInterface.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Stats/StatsMisc.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectThreadContext.h"
#include "VisualLogger/VisualLogger.h"

#if WITH_RECAST
#include "NavMesh/RecastGeometryExport.h"
#include "NavMesh/RecastHelpers.h"
#include "NavMesh/RecastNavMesh.h"
#endif // WITH_RECAST

#if WITH_EDITOR
#include "EditorModeManager.h"
#include "EditorModes.h"
#include "LevelEditor.h"
#include "Misc/MessageDialog.h"
#include "WorldPartition/WorldPartition.h"
#endif // WITH_EDITOR


#include UE_INLINE_GENERATED_CPP_BY_NAME(NavigationSystem)


static const uint32 INITIAL_ASYNC_QUERIES_SIZE = 32;
static const uint32 REGISTRATION_QUEUE_SIZE = 16;	// and we'll not reallocate

#define LOCTEXT_NAMESPACE "Navigation"

DECLARE_CYCLE_STAT(TEXT("Nav Tick: mark dirty"), STAT_Navigation_TickMarkDirty, STATGROUP_Navigation);
DECLARE_CYCLE_STAT(TEXT("Nav Tick: async build"), STAT_Navigation_TickAsyncBuild, STATGROUP_Navigation);
DECLARE_CYCLE_STAT(TEXT("Nav Tick: dispatch async pathfinding results"), STAT_Navigation_DispatchAsyncPathfindingResults, STATGROUP_Navigation);
DECLARE_CYCLE_STAT(TEXT("Nav Tick: async pathfinding"), STAT_Navigation_TickAsyncPathfinding, STATGROUP_Navigation);
DECLARE_CYCLE_STAT_WITH_FLAGS(TEXT("NavOctree bookkeeping"), STAT_NavOctreeBookkeeping, STATGROUP_Navigation, EStatFlags::Verbose);

//----------------------------------------------------------------------//
// Stats
//----------------------------------------------------------------------//

DEFINE_STAT(STAT_Navigation_QueriesTimeSync);
DEFINE_STAT(STAT_Navigation_RequestingAsyncPathfinding);
DEFINE_STAT(STAT_Navigation_PathfindingSync);
DEFINE_STAT(STAT_Navigation_PathfindingAsync);
DEFINE_STAT(STAT_Navigation_TileNavAreaSorting);
DEFINE_STAT(STAT_Navigation_TileGeometryExportToObjAsync);
DEFINE_STAT(STAT_Navigation_TileVoxelFilteringAsync);
DEFINE_STAT(STAT_Navigation_TileBuildAsync);
DEFINE_STAT(STAT_Navigation_TileBuildPreparationSync);
DEFINE_STAT(STAT_Navigation_BSPExportSync);
DEFINE_STAT(STAT_Navigation_GatheringNavigationModifiersSync);
DEFINE_STAT(STAT_Navigation_ActorsGeometryExportSync);
DEFINE_STAT(STAT_Navigation_ProcessingActorsForNavMeshBuilding);
DEFINE_STAT(STAT_Navigation_AdjustingNavLinks);
DEFINE_STAT(STAT_Navigation_RegisterNavOctreeElement);
DEFINE_STAT(STAT_Navigation_UnregisterNavOctreeElement);
DEFINE_STAT(STAT_Navigation_AddingActorsToNavOctree);
DEFINE_STAT(STAT_Navigation_RecastAddGeneratedTiles);
DEFINE_STAT(STAT_Navigation_RecastAddGeneratedTileLayer);
DEFINE_STAT(STAT_Navigation_RecastTick);
DEFINE_STAT(STAT_Navigation_RecastPathfinding);
DEFINE_STAT(STAT_Navigation_RecastTestPath);
DEFINE_STAT(STAT_Navigation_StoringCompressedLayers);
DEFINE_STAT(STAT_Navigation_CreateTileGenerator);
DEFINE_STAT(STAT_Navigation_DoWork);
DEFINE_STAT(STAT_Navigation_RemoveLayers);
DEFINE_STAT(STAT_Navigation_RecastBuildCompressedLayers);
DEFINE_STAT(STAT_Navigation_RecastCreateHeightField);
DEFINE_STAT(STAT_Navigation_RecastComputeRasterizationMasks);
DEFINE_STAT(STAT_Navigation_RecastRasterizeTriangles);
DEFINE_STAT(STAT_Navigation_RecastVoxelFilter);
DEFINE_STAT(STAT_Navigation_RecastFilter);
DEFINE_STAT(STAT_Navigation_FilterLedgeSpans);
DEFINE_STAT(STAT_Navigation_RecastBuildCompactHeightField);
DEFINE_STAT(STAT_Navigation_RecastErodeWalkable);
DEFINE_STAT(STAT_Navigation_RecastBuildLayers);
DEFINE_STAT(STAT_Navigation_RecastBuildTileCache);
DEFINE_STAT(STAT_Navigation_RecastBuildPolyMesh);
DEFINE_STAT(STAT_Navigation_RecastBuildPolyDetail);
DEFINE_STAT(STAT_Navigation_RecastGatherOffMeshData);
DEFINE_STAT(STAT_Navigation_RecastCreateNavMeshData);
DEFINE_STAT(STAT_Navigation_RecastMarkAreas);
DEFINE_STAT(STAT_Navigation_RecastBuildContours);
DEFINE_STAT(STAT_Navigation_RecastBuildNavigation);
DEFINE_STAT(STAT_Navigation_GenerateNavigationDataLayer);
DEFINE_STAT(STAT_Navigation_RecastBuildLinks);
DEFINE_STAT(STAT_Navigation_RecastBuildLinks_FindEdges);
DEFINE_STAT(STAT_Navigation_RecastBuildLinks_Sample);
DEFINE_STAT(STAT_Navigation_RecastBuildRegions);
DEFINE_STAT(STAT_Navigation_UpdateNavOctree);
DEFINE_STAT(STAT_Navigation_CollisionTreeMemory);
DEFINE_STAT(STAT_Navigation_NavDataMemory);
DEFINE_STAT(STAT_Navigation_TileCacheMemory);
DEFINE_STAT(STAT_Navigation_OutOfNodesPath);
DEFINE_STAT(STAT_Navigation_PartialPath);
DEFINE_STAT(STAT_Navigation_CumulativeBuildTime);
DEFINE_STAT(STAT_Navigation_BuildTime);
DEFINE_STAT(STAT_Navigation_OffsetFromCorners);
DEFINE_STAT(STAT_Navigation_PathVisibilityOptimisation);
DEFINE_STAT(STAT_Navigation_ObservedPathsCount);
DEFINE_STAT(STAT_Navigation_RecastMemory);

DEFINE_STAT(STAT_Navigation_DetourTEMP);
DEFINE_STAT(STAT_Navigation_DetourPERM);
DEFINE_STAT(STAT_Navigation_DetourPERM_AVOIDANCE);
DEFINE_STAT(STAT_Navigation_DetourPERM_CROWD);
DEFINE_STAT(STAT_Navigation_DetourPERM_LOOKUP);
DEFINE_STAT(STAT_Navigation_DetourPERM_NAVQUERY);
DEFINE_STAT(STAT_Navigation_DetourPERM_NAVMESH);
DEFINE_STAT(STAT_Navigation_DetourPERM_NODE_POOL);
DEFINE_STAT(STAT_Navigation_DetourPERM_PATH_CORRIDOR);
DEFINE_STAT(STAT_Navigation_DetourPERM_PATH_QUEUE);
DEFINE_STAT(STAT_Navigation_DetourPERM_PROXY_GRID);
DEFINE_STAT(STAT_Navigation_DetourPERM_TILE_DATA);
DEFINE_STAT(STAT_Navigation_DetourPERM_TILE_DYNLINK_OFFMESH);
DEFINE_STAT(STAT_Navigation_DetourPERM_TILE_DYNLINK_CLUSTER);
DEFINE_STAT(STAT_Navigation_DetourPERM_TILES);
DEFINE_STAT(STAT_Navigation_DetourPERM_TILE_LINK_BUILDER);

DEFINE_STAT(STAT_DetourTileMemory);
DEFINE_STAT(STAT_DetourTileMeshHeaderMemory);
DEFINE_STAT(STAT_DetourTileNavVertsMemory);
DEFINE_STAT(STAT_DetourTileNavPolysMemory);
DEFINE_STAT(STAT_DetourTileLinksMemory);
DEFINE_STAT(STAT_DetourTileDetailMeshesMemory);
DEFINE_STAT(STAT_DetourTileDetailVertsMemory);
DEFINE_STAT(STAT_DetourTileDetailTrisMemory);
DEFINE_STAT(STAT_DetourTileBVTreeMemory);
DEFINE_STAT(STAT_DetourTileOffMeshConsMemory);
DEFINE_STAT(STAT_DetourTileOffMeshSegsMemory);
DEFINE_STAT(STAT_DetourTileClustersMemory);
DEFINE_STAT(STAT_DetourTilePolyClustersMemory);

CSV_DEFINE_CATEGORY(NavigationSystem, false);
CSV_DEFINE_CATEGORY(NavigationBuildDetailed, true);
CSV_DEFINE_CATEGORY(NavTasksDelays, true);
CSV_DEFINE_CATEGORY(NavTasks, true);
CSV_DEFINE_CATEGORY(NavInvokers, true);

namespace UE::Navigation::Private
{

static FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdNavDirtyAreaAroundPlayer(
	TEXT("ai.debug.nav.DirtyAreaAroundPlayer"),
	TEXT("Dirty all tiles in a square area around the local player using provided value as extent (in cm), using 10 meters if not specified."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, const UWorld* World, FOutputDevice& OutputDevice)
		{
			if (const ULocalPlayer* LocalPlayer = World->GetFirstLocalPlayerFromController<ULocalPlayer>())
			{
				const FVector Center = LocalPlayer->LastViewLocation;

				FVector::FReal Extent = 1000;
				if (Args.Num() > 0)
				{
					if (FCString::IsNumeric(*Args[0]))
					{
						Extent = FCString::Atod(*Args[0]);
					}
					else
					{
						OutputDevice.Log(ELogVerbosity::Error, TEXT("Command failed since first parameter is not a valid numerical value"));
						return;
					}
				}

				UNavigationSystemV1::NavigationDirtyEvent.Broadcast(FBox(Center - FVector(Extent), Center + FVector(Extent)));
			}
			else
			{
				OutputDevice.Log(ELogVerbosity::Error, TEXT("Command failed since it was unable to find a local player"));
			}
		}
	));


static FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdDumpOctreeElements(
	TEXT("ai.debug.nav.DumpOctreeElements"),
	TEXT("Iterates through all nodes of the navigation octree and log details about each element to the output device."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, const UWorld* World, FOutputDevice& OutputDevice)
		{
			if (const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
			{
				if (const FNavigationOctree* Octree = NavSys->GetNavOctree())
				{
					int32 NumElements = 0;
					Octree->FindNodesWithPredicate(
						[&NumElements](FNavigationOctree::FNodeIndex /*ParentNodeIndex*/, FNavigationOctree::FNodeIndex /*NodeIndex*/, const FBoxCenterAndExtent&) { return true; },
						[&NumElements, &OutputDevice, &Octree](FNavigationOctree::FNodeIndex /*ParentNodeIndex*/, const FNavigationOctree::FNodeIndex NodeIndex, const FBoxCenterAndExtent&)
						{
							for (const FNavigationOctreeElement& OctreeElement : Octree->GetElementsForNode(NodeIndex))
							{
								NumElements++;
								OutputDevice.Logf(ELogVerbosity::Type::Log, TEXT("%s bounds: [%s] parent:'%s'"),
									*OctreeElement.GetSourceElement()->GetPathName(),
									*OctreeElement.Bounds.ToString(),
									*GetNameSafe(OctreeElement.GetSourceElement().Get().GetNavigationParent().Get()));
							};
						});

					OutputDevice.Logf(ELogVerbosity::Type::Log, TEXT("Total: %d elements"), NumElements);
				}
				else
				{
					OutputDevice.Log(ELogVerbosity::Error, TEXT("Octree not used in the current configuration"));
				}
			}
			else
			{
				OutputDevice.Log(ELogVerbosity::Error, TEXT("Command failed since it was unable to find the navigation system"));
			}
		}
	));

static FAutoConsoleCommandWithWorldArgsAndOutputDevice CmdLogInvokers(
	TEXT("ai.debug.nav.LogInvokers"),
	TEXT("Iterate through all the navigation invokers and log details about each of them to the output device."),
	FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, const UWorld* World, FOutputDevice& OutputDevice)
		{
#if !UE_BUILD_SHIPPING
			if (const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
			{
				NavSys->DebugLogInvokers(OutputDevice);
			}
#endif // !UE_BUILD_SHIPPING
		}
	));

const FNavDataConfig& GetFallbackNavDataConfig()
{
	static FNavDataConfig FallbackNavDataConfig(FNavigationSystem::FallbackAgentRadius, FNavigationSystem::FallbackAgentHeight);
	return FallbackNavDataConfig;
}

FORCEINLINE bool IsValidExtent(const FVector& Extent)
{
	return Extent != INVALID_NAVEXTENT;
}

static bool bComponentShouldWaitForActorToRegister = true;
static FAutoConsoleVariableRef CVarRollbackNavigationComponentShouldWaitForActorToRegister(
	TEXT("UE.Rollback.Navigation.ComponentShouldWaitForActorToRegister"), bComponentShouldWaitForActorToRegister,
	TEXT("Components registration to navigation octree will be postponed until owning actor is registered to the octree."
		"\nCategory: [Navigation]"),
	ECVF_Default);

bool ShouldComponentWaitForActorToRegister(const UActorComponent* Comp)
{
	if (bComponentShouldWaitForActorToRegister)
	{
		// Ignore operations on components until the actor has registered all its components to the scene.
		// Then, Actor registration to the navigation octree will also registers its components to the octree.
		if (const AActor* Owner = Comp->GetOwner())
		{
			if (!Owner->HasActorRegisteredAllComponents())
			{
				return true;
			}
		}
	}

	return false;
}

} // UE::Navigation::Private

//----------------------------------------------------------------------//
// FNavigationSystem
//----------------------------------------------------------------------//
namespace FNavigationSystem
{
FCustomLinkOwnerInfo::FCustomLinkOwnerInfo(INavLinkCustomInterface* Link)
{
	LinkInterface = Link;
	LinkOwner = Link->GetLinkOwner();
}

bool ShouldLoadNavigationOnClient(ANavigationData& NavData)
{
	const UWorld* World = NavData.GetWorld();

	if (World && World->GetNavigationSystem())
	{
		const UNavigationSystemV1* NavSys = Cast<UNavigationSystemV1>(World->GetNavigationSystem());
		return NavSys && NavSys->ShouldLoadNavigationOnClient(&NavData);
	}
	else if (GEngine->NavigationSystemClass && GEngine->NavigationSystemClass->IsChildOf<UNavigationSystemV1>())
	{
		const UNavigationSystemV1* NavSysCDO = GEngine->NavigationSystemClass->GetDefaultObject<const UNavigationSystemV1>();
		return NavSysCDO && NavSysCDO->ShouldLoadNavigationOnClient(&NavData);
	}
	return false;
}

void MakeAllComponentsNeverAffectNav(AActor& Actor)
{
	const TSet<UActorComponent*> Components = Actor.GetComponents();
	for (UActorComponent* ActorComp : Components)
	{
		ActorComp->SetCanEverAffectNavigation(false);
	}
}

} // namespace FNavigationSystem

//----------------------------------------------------------------------//
// NavigationDebugDrawing
//----------------------------------------------------------------------//
namespace NavigationDebugDrawing
{
	const float PathLineThickness = 3.f;
	const FVector PathOffset(0,0,15);
	const FVector PathNodeBoxExtent(16.f);
}

//----------------------------------------------------------------------//
// FNavigationInvokerRaw
//----------------------------------------------------------------------//
FNavigationInvokerRaw::FNavigationInvokerRaw(const FVector& InLocation, float Min, float Max, const FNavAgentSelector& InSupportedAgents, ENavigationInvokerPriority InPriority)
: Location(InLocation)
, RadiusMin(Min)
, RadiusMax(Max)
, SupportedAgents(InSupportedAgents)
, Priority(InPriority)
{
}

//----------------------------------------------------------------------//
// FNavigationInvoker
//----------------------------------------------------------------------//
FNavigationInvoker::FNavigationInvoker()
: Actor(nullptr)
, Object(nullptr)
, GenerationRadius(0)
, RemovalRadius(0)
, Priority(ENavigationInvokerPriority::Default)
{
	SupportedAgents.MarkInitialized();
}

FNavigationInvoker::FNavigationInvoker(AActor& InActor, float InGenerationRadius, float InRemovalRadius, const FNavAgentSelector& InSupportedAgents, ENavigationInvokerPriority InPriority)
: Actor(&InActor)
, Object(nullptr)
, GenerationRadius(InGenerationRadius)
, RemovalRadius(InRemovalRadius)
, SupportedAgents(InSupportedAgents)
, Priority(InPriority)
{
	SupportedAgents.MarkInitialized();
}

FNavigationInvoker::FNavigationInvoker(INavigationInvokerInterface& InObject, float InGenerationRadius, float InRemovalRadius, const FNavAgentSelector& InSupportedAgents, ENavigationInvokerPriority InPriority)
: Actor(nullptr)
, Object(&InObject)
, GenerationRadius(InGenerationRadius)
, RemovalRadius(InRemovalRadius)
, SupportedAgents(InSupportedAgents)
, Priority(InPriority)
{
}

FString FNavigationInvoker::GetName() const
{
	/** We are using IsExplicitlyNull to know which one of the Actor or the Object was set at construction */
	if (!Actor.IsExplicitlyNull())
	{
		return GetNameSafe(Actor.Get());
	}
	else
	{
		return GetNameSafe(Object.GetObject());
	}
}

bool FNavigationInvoker::GetLocation(FVector& OutLocation) const
{
	/** We are using IsExplicitlyNull to know which one of the Actor or the Object was set at construction */
	if (!Actor.IsExplicitlyNull())
	{
		if (const AActor* ActorPtr = Actor.Get())
		{
			OutLocation = ActorPtr->GetActorLocation();
			return true;
		}
	}
	else
	{
		if (const INavigationInvokerInterface* InvokerInterface = Object.Get())
		{
			OutLocation = InvokerInterface->GetNavigationInvokerLocation();
			return true;
		}
	}

	return false;
}

//----------------------------------------------------------------------//
// helpers
//----------------------------------------------------------------------//
namespace
{
#if ENABLE_VISUAL_LOG
	void NavigationDataDump(const UObject* Object, const FName& CategoryName, const ELogVerbosity::Type Verbosity, const FBox& Box, const UWorld& World, FVisualLogEntry& CurrentEntry)
	{
		const ANavigationData* MainNavData = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World)->GetDefaultNavDataInstance();
		const FNavDataGenerator* Generator = MainNavData ? MainNavData->GetGenerator() : nullptr;
		if (Generator)
		{
			Generator->GrabDebugSnapshot(&CurrentEntry, (!Box.IsValid || FMath::IsNearlyZero(Box.GetVolume())) ? MainNavData->GetBounds().ExpandBy(FVector(20, 20, 20)) : Box, CategoryName, Verbosity);
		}
	}
#endif // ENABLE_VISUAL_LOG
}

void FNavRegenTimeSlicer::SetupTimeSlice(double SliceDuration)
{
	RemainingDuration = OriginalDuration = SliceDuration; 
	StartTime = TimeLastTested = 0.;
	bTimeSliceFinishedCached = false; 
}

void FNavRegenTimeSlicer::StartTimeSlice()
{
	ensureMsgf(!bTimeSliceFinishedCached, TEXT("Starting a time slice that has already been tested as finished! Call SetupTimeSlice() before calling StartTimeSlice() again!"));
	ensureMsgf(RemainingDuration > 0., TEXT("Attempting to start a time slice that has zero duration!"));

	TimeLastTested = StartTime = FPlatformTime::Seconds();
}
 
void FNavRegenTimeSlicer::EndTimeSliceAndAdjustDuration()
{
	RemainingDuration = FMath::Max(RemainingDuration - (TimeLastTested - StartTime), 0.);
}

#if ALLOW_TIME_SLICE_DEBUG
void FNavRegenTimeSlicer::DebugSetLongTimeSliceData(TFunction<void(FName, double)> LongTimeSliceFunction, double LongTimeSliceDuration) const
{
	DebugLongTimeSliceFunction = LongTimeSliceFunction;
	DebugLongTimeSliceDuration = LongTimeSliceDuration;
}

void FNavRegenTimeSlicer::DebugResetLongTimeSliceFunction() const
{
	DebugLongTimeSliceFunction.Reset();
}

#endif // ALLOW_TIME_SLICE_DEBUG

bool FNavRegenTimeSlicer::TestTimeSliceFinished() const
{
	ensureMsgf(!bTimeSliceFinishedCached, TEXT("Testing time slice is finished when we have already confirmed that!"));

	const double Time = FPlatformTime::Seconds();

#if ALLOW_TIME_SLICE_DEBUG
	const double TimeSinceLastTested = Time - TimeLastTested;
	if (TimeSinceLastTested >= DebugLongTimeSliceDuration)
	{
		if (ensureMsgf(DebugLongTimeSliceFunction, TEXT("DebugLongTimeSliceFunction should be setup! Call DebugSetLongTimeSliceData() prior to TestTimeSliceFinished()!")))
		{
			DebugLongTimeSliceFunction(DebugSectionName, TimeSinceLastTested);
		}
	}

	// Reset SectionDebugName
	DebugSectionName = FNavigationSystem::DebugTimeSliceDefaultSectionName;
#endif // ALLOW_TIME_SLICE_DEBUG

	TimeLastTested = Time;

	bTimeSliceFinishedCached = (TimeLastTested - StartTime) >= RemainingDuration;
	return bTimeSliceFinishedCached;
}

void FNavRegenTimeSliceManager::ResetTileWaitTimeArrays(const TArray<TObjectPtr<ANavigationData>>& NavDataSet)
{
	TileWaitTimes.SetNum(NavDataSet.Num());
	for (TArray<double>& Array : TileWaitTimes)
	{
		Array.Empty();
	}
}

void FNavRegenTimeSliceManager::PushTileWaitTime(const int32 NavDataIndex, const double NewTime)
{
	if (TileWaitTimes.IsValidIndex(NavDataIndex))
	{
		TileWaitTimes[NavDataIndex].Add(NewTime);	
	}
}

#if !UE_BUILD_SHIPPING
void FNavRegenTimeSliceManager::ResetTileHistoryData(const TArray<TObjectPtr<ANavigationData>>& NavDataSet)
{
	TileHistoryData.SetNum(NavDataSet.Num());
	for (TArray<FTileHistoryData>& HistoryData : TileHistoryData)
	{
		HistoryData.Empty();
	}
	TileHistoryStartTime = FPlatformTime::Seconds();
}

void FNavRegenTimeSliceManager::PushTileHistoryData(const int32 NavDataIndex, const FTileHistoryData& TileData)
{
	if (TileHistoryData.IsValidIndex(NavDataIndex))
	{
		TileHistoryData[NavDataIndex].Add(TileData);	
	}
}
#endif // UE_BUILD_SHIPPING

double FNavRegenTimeSliceManager::GetAverageTileWaitTime(const int32 NavDataIndex) const
{
	if (!TileWaitTimes.IsValidIndex(NavDataIndex))
	{
		return 0.;
	}
	
	double Total = 0.;
	const TArray<double>& TimeArray = TileWaitTimes[NavDataIndex];
	if (TimeArray.Num() == 0)
	{
		return 0.;			
	}
		
	for (const double Time : TimeArray)
	{
		Total += Time;
	}
	return Total / TimeArray.Num();
}

void FNavRegenTimeSliceManager::ResetTileWaitTime(const int32 NavDataIndex)
{
	if (TileWaitTimes.IsValidIndex(NavDataIndex))
	{
		TileWaitTimes[NavDataIndex].Reset();			
	}
}

FNavRegenTimeSliceManager::FNavRegenTimeSliceManager()
	: MinTimeSliceDuration(0.00075)
	, MaxTimeSliceDuration(0.004)
	, FrameNumOld(TNumericLimits<int64>::Max() - 1)
	, MaxDesiredTileRegenDuration(0.7f)
	, TimeLastCall(-1.f)
	, NavDataIdx(0)
#if WITH_RECAST && TIME_SLICE_NAV_REGEN
	, bDoTimeSlicedUpdate(true)
#else
	, bDoTimeSlicedUpdate(false)
#endif
{
}

void FNavRegenTimeSliceManager::CalcAverageDeltaTime(uint64 FrameNum)
{
	const double CurTime = FPlatformTime::Seconds();

	if ((FrameNumOld + 1) == FrameNum)
	{
		const double DeltaTime = CurTime - TimeLastCall;
		MovingWindowDeltaTime.PushValue(DeltaTime);
	}
	TimeLastCall = CurTime;
	FrameNumOld = FrameNum;
}

void FNavRegenTimeSliceManager::CalcTimeSliceDuration(const TArray<TObjectPtr<ANavigationData>>& NavDataSet, int32 NumTilesToRegen, const TArray<double>& CurrentTileRegenDurations)
{
	const float RawDeltaTimesAverage = FloatCastChecked<float>(MovingWindowDeltaTime.GetAverage(), UE::LWC::DefaultFloatPrecision);
	const float DeltaTimesAverage = (RawDeltaTimesAverage > 0.f) ? RawDeltaTimesAverage : (1.f / 30.f); //use default 33 ms

	const double TileRegenTimesAverage = (MovingWindowTileRegenTime.GetAverage() > 0.) ? MovingWindowTileRegenTime.GetAverage() : 0.0025; //use default of 2.5 milli secs to regen a full tile

	//calculate the max desired frames to regen all the tiles in PendingDirtyTiles
	const float MaxDesiredFramesToRegen = FMath::FloorToFloat(MaxDesiredTileRegenDuration / DeltaTimesAverage);

	//tiles to add to PendingDirtyTiles if the current tiles are taking longer than average to regen
	//we add 1 tile for however many times longer the current tile is taking compared with the moving window average
	int32 TilesToAddForLongCurrentTileRegen = 0;
	for (const double RegenDuration : CurrentTileRegenDurations)
	{
		TilesToAddForLongCurrentTileRegen += (RegenDuration > 0.) ? (static_cast<int32>(RegenDuration / TileRegenTimesAverage)) : 0;
	}

	//calculate the total processing time to regen all the tiles based on the moving window average
	const double TotalRegenTime = TileRegenTimesAverage * static_cast<double>(NumTilesToRegen + TilesToAddForLongCurrentTileRegen);

	//calculate the time slice per frame required to regen all the tiles clamped between MinTimeSliceDuration and MaxTimeSliceDuration
	const double NextRegenTimeSliceTime = FMath::Clamp(TotalRegenTime / static_cast<double>(MaxDesiredFramesToRegen), MinTimeSliceDuration, MaxTimeSliceDuration);

	TimeSlicer.SetupTimeSlice(NextRegenTimeSliceTime);

#if !UE_BUILD_SHIPPING
	CSV_CUSTOM_STAT(NavigationSystem, NavTileRegenTimeSliceTimeMs, static_cast<float>(NextRegenTimeSliceTime * 1000.), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(NavigationSystem, NavTileNumTilesToRegen, NumTilesToRegen, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(NavigationSystem, NavTilesToAddForLongCurrentTileRegen, TilesToAddForLongCurrentTileRegen, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(NavigationSystem, NavTileAvRegenTimeMs, static_cast<float>(MovingWindowTileRegenTime.GetAverage() * 1000.), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(NavigationSystem, NavTileAvRegenDeltaTimeMs, static_cast<float>(MovingWindowDeltaTime.GetAverage() * 1000.), ECsvCustomStatOp::Set);

	for (int32 NavDataIndex = 0; NavDataIndex < NavDataSet.Num(); ++NavDataIndex)
	{
		if (TileWaitTimes.IsValidIndex(NavDataIndex))
		{
#if CSV_PROFILER_STATS			
			const float WaitTime = static_cast<float>(GetAverageTileWaitTime(NavDataIndex) * 1000.);

			const FString StatName = FString::Printf(TEXT("NavTileAvTileWaitTimeMs_%s"), *GetNameSafe(NavDataSet[NavDataIndex])); 
			FCsvProfiler::RecordCustomStat(*StatName, CSV_CATEGORY_INDEX(NavTasksDelays), WaitTime, ECsvCustomStatOp::Set);
#endif // CSV_PROFILER_STATS

			ResetTileWaitTime(NavDataIndex);
		}
	}
#endif // !UE_BUILD_SHIPPING
}

void FNavRegenTimeSliceManager::SetMinTimeSliceDuration(double NewMinTimeSliceDuration)
{
	MinTimeSliceDuration = NewMinTimeSliceDuration;

	UE_LOG(LogNavigationDataBuild, Verbose, TEXT("Navigation System: MinTimeSliceDuration = %f"), MinTimeSliceDuration);
}

void FNavRegenTimeSliceManager::SetMaxTimeSliceDuration(double NewMaxTimeSliceDuration)
{
	MaxTimeSliceDuration = NewMaxTimeSliceDuration;

	UE_LOG(LogNavigationDataBuild, Verbose, TEXT("Navigation System: MaxTimeSliceDuration = %f"), MaxTimeSliceDuration);
}

void FNavRegenTimeSliceManager::SetMaxDesiredTileRegenDuration(float NewMaxDesiredTileRegenDuration)
{
	MaxDesiredTileRegenDuration = NewMaxDesiredTileRegenDuration;

	UE_LOG(LogNavigationDataBuild, Verbose, TEXT("Navigation System: MaxDesiredTileRegenDuration = %f"), MaxDesiredTileRegenDuration);
}

#if !UE_BUILD_SHIPPING
void FNavRegenTimeSliceManager::LogTileStatistics(const TArray<TObjectPtr<ANavigationData>>& NavDataSet) const
{
	UE_SUPPRESS(LogNavigationHistory, Log,
	{
		// Log median tile processing time every 60 frames.
		const bool bLog = GFrameCounter % 60 == 0;
		const double HistoryDuration = FPlatformTime::Seconds() - TileHistoryStartTime;
		for (int32 NavDataIndex = 0; bLog && NavDataIndex < NavDataSet.Num(); ++NavDataIndex)
		{
			if (TileHistoryData.IsValidIndex(NavDataIndex))
			{
				TArray<FTileHistoryData> HistoryData = TileHistoryData[NavDataIndex];
				if (HistoryData.Num() > 0)
				{
					const int32 MedianIndex = HistoryData.Num()/2;
					const int32 HighIndex = int(HistoryData.Num()*0.9);

					HistoryData.Sort([](const FTileHistoryData& A, const FTileHistoryData& B){ return A.TileRegenTime < B.TileRegenTime; });
					const double MedianRegenTimeMs = HistoryData[MedianIndex].TileRegenTime * 1000.f;
					const double HighRegenTimeMs = HistoryData[HighIndex].TileRegenTime * 1000.f;
					const int64 MedianRegenFrames = HistoryData[MedianIndex].EndRegenFrame - HistoryData[MedianIndex].StartRegenFrame;

					HistoryData.Sort([](const FTileHistoryData& A, const FTileHistoryData& B){ return A.TileWaitTime < B.TileWaitTime; });
					const double MedianWaitTimeMs = HistoryData[MedianIndex].TileWaitTime * 1000.f;
					const double HighWaitTimeMs = HistoryData[HighIndex].TileWaitTime * 1000.f;
					
					UE_LOG(LogNavigationHistory, Log, TEXT("%-35s Median tile stats: regen time: %2.2f ms, regen frames %lld, wait time: %4.f ms (high regen time: %2.2f ms, high wait time: %4.f ms) regen count: %i, regen/s: %0.2f"),
						*GetNameSafe(NavDataSet[NavDataIndex]), MedianRegenTimeMs, MedianRegenFrames, MedianWaitTimeMs, HighRegenTimeMs, HighWaitTimeMs,
						HistoryData.Num(), HistoryData.Num()/HistoryDuration);
				}
			}
		}
	});
}
#endif // !UE_BUILD_SHIPPING

//----------------------------------------------------------------------//
// UNavigationSystemV1
//----------------------------------------------------------------------//
bool UNavigationSystemV1::bNavigationAutoUpdateEnabled = true;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
FNavigationSystemExec UNavigationSystemV1::ExecHandler;
#endif // !UE_BUILD_SHIPPING

/** called after navigation influencing event takes place*/
UNavigationSystemV1::FOnNavigationDirty UNavigationSystemV1::NavigationDirtyEvent;

bool UNavigationSystemV1::bUpdateNavOctreeOnComponentChange = true;
bool UNavigationSystemV1::bStaticRuntimeNavigation = false;
bool UNavigationSystemV1::bIsPIEActive = false;
//----------------------------------------------------------------------//
// life cycle stuff
//----------------------------------------------------------------------//

UNavigationSystemV1::UNavigationSystemV1(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bTickWhilePaused(false)
	, bWholeWorldNavigable(false)
	, bSkipAgentHeightCheckWhenPickingNavData(false)
	, DirtyAreaWarningSizeThreshold(-1.0f)
	, GatheringNavModifiersWarningLimitTime(-1.0f)
	, BuildBounds(EForceInit::ForceInit)
	, OperationMode(FNavigationSystemRunMode::InvalidMode)
	, bAbortAsyncQueriesRequested(false)
	, NavBuildingLockFlags(0)
	, InitialNavBuildingLockFlags(0)
	, bInitialSetupHasBeenPerformed(false)
	, bInitialLevelsAdded(false)
	, bWorldInitDone(false)
	, bCleanUpDone(false)
	, CurrentlyDrawnNavDataIndex(0)
{
#if WITH_EDITOR
	NavUpdateLockFlags = 0;
#endif
	struct FDelegatesInitializer
	{
		FDelegatesInitializer()
		{
			UNavigationSystemBase::GetSupportsDynamicChangesDelegate().BindStatic(&UNavigationSystemV1::SupportsDynamicChanges);
			UNavigationSystemBase::GetAddNavigationElementDelegate().BindStatic(&UNavigationSystemV1::AddNavigationElement);
			UNavigationSystemBase::GetRemoveNavigationElementDelegate().BindStatic(&UNavigationSystemV1::RemoveNavigationElement);
			UNavigationSystemBase::GetUpdateNavigationElementDelegate().BindStatic(&UNavigationSystemV1::OnNavigationElementUpdated);

			UNavigationSystemBase::GetUpdateNavigationElementBoundsDelegate().BindLambda(
				[](UWorld* World, FNavigationElementHandle Handle, const FBox& NewBounds, const TConstArrayView<FBox> DirtyAreas)
				{
					if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
					{
						NavSys->UpdateNavOctreeElementBounds(Handle, NewBounds, DirtyAreas);
					}
				});

			UNavigationSystemBase::RegisterNavRelevantObjectDelegate().BindLambda([](UObject& Object) { UNavigationSystemV1::OnNavRelevantObjectRegistered(Object); });
			UNavigationSystemBase::UpdateNavRelevantObjectDelegate().BindStatic(&UNavigationSystemV1::UpdateNavRelevantObjectInNavOctree);
			UNavigationSystemBase::UnregisterNavRelevantObjectDelegate().BindLambda([](UObject& Object) { UNavigationSystemV1::OnNavRelevantObjectUnregistered(Object); });
			UNavigationSystemBase::OnObjectBoundsChangedDelegate().BindLambda([](UObject& Object, const FBox& NewBounds, const TConstArrayView<FBox> DirtyAreas)
				{
					if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Object.GetWorld()))
					{
						NavSys->UpdateNavOctreeElementBounds(FNavigationElementHandle(&Object), NewBounds, DirtyAreas);
					}
				});

			UNavigationSystemBase::UpdateActorDataDelegate().BindStatic(&UNavigationSystemV1::UpdateActorInNavOctree);
			UNavigationSystemBase::UpdateComponentDataDelegate().BindStatic(&UNavigationSystemV1::UpdateComponentInNavOctree);
			UNavigationSystemBase::UpdateComponentDataAfterMoveDelegate().BindLambda([](USceneComponent& Comp) { UNavigationSystemV1::UpdateNavOctreeAfterMove(&Comp); });
			UNavigationSystemBase::OnActorBoundsChangedDelegate().BindLambda([](AActor& Actor) { UNavigationSystemV1::UpdateNavOctreeBounds(&Actor); });
			UNavigationSystemBase::OnPostEditActorMoveDelegate().BindLambda([](AActor& Actor) {
				// update actor and all its components in navigation system after finishing move
				// USceneComponent::UpdateNavigationData works only in game world
				UNavigationSystemV1::UpdateNavOctreeBounds(&Actor);

				TArray<AActor*> ParentedActors;
				Actor.GetAttachedActors(ParentedActors);
				for (int32 Idx = 0; Idx < ParentedActors.Num(); Idx++)
				{
					UNavigationSystemV1::UpdateNavOctreeBounds(ParentedActors[Idx]);
				}

				// We need to check this actor has registered all their components post spawn / load
				// before attempting to update the components in the nav octree.
				// Without this check we were getting an issue with UNavRelevantComponent::GetNavigationParent().
				if (Actor.HasActorRegisteredAllComponents())
				{
					// not doing manual update of all attached actors since UpdateActorAndComponentsInNavOctree should take care of it
					UNavigationSystemV1::UpdateActorAndComponentsInNavOctree(Actor);
				}
			});
			UNavigationSystemBase::OnComponentTransformChangedDelegate().BindLambda([](USceneComponent& Comp)
			{
				if (ShouldUpdateNavOctreeOnComponentChange())
				{
					UpdateNavOctreeAfterMove(&Comp);
				}
			});
			UNavigationSystemBase::OnActorRegisteredDelegate().BindLambda([](AActor& Actor) { UNavigationSystemV1::OnActorRegistered(&Actor); });
			UNavigationSystemBase::OnActorUnregisteredDelegate().BindLambda([](AActor& Actor) { UNavigationSystemV1::OnActorUnregistered(&Actor); });
			UNavigationSystemBase::OnComponentRegisteredDelegate().BindLambda([](UActorComponent& Comp) { UNavigationSystemV1::OnComponentRegistered(&Comp); });
			UNavigationSystemBase::OnComponentUnregisteredDelegate().BindLambda([](UActorComponent& Comp) { UNavigationSystemV1::OnComponentUnregistered(&Comp); });
			UNavigationSystemBase::RegisterComponentDelegate().BindLambda([](UActorComponent& Comp) { UNavigationSystemV1::RegisterComponent(&Comp); });
			UNavigationSystemBase::UnregisterComponentDelegate().BindLambda([](UActorComponent& Comp) { UNavigationSystemV1::UnregisterComponent(&Comp); });
			UNavigationSystemBase::RemoveActorDataDelegate().BindLambda([](AActor& Actor) { UNavigationSystemV1::ClearNavOctreeAll(&Actor); });
			UNavigationSystemBase::HasComponentDataDelegate().BindLambda([](UActorComponent& Comp)
			{
				UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Comp.GetWorld());
				const FNavigationElementHandle Element(&Comp);
				return (NavSys && (NavSys->GetNavOctreeIdForElement(Element) || NavSys->HasPendingUpdateForElement(Element)));
			});
			UNavigationSystemBase::GetDefaultSupportedAgentDelegate().BindStatic(&UNavigationSystemV1::GetDefaultSupportedAgent);
			UNavigationSystemBase::GetBiggestSupportedAgentDelegate().BindStatic(&UNavigationSystemV1::GetBiggestSupportedAgent);
			UNavigationSystemBase::UpdateActorAndComponentDataDelegate().BindStatic(&UNavigationSystemV1::UpdateActorAndComponentsInNavOctree);
			UNavigationSystemBase::OnComponentBoundsChangedDelegate().BindLambda([](UActorComponent& Comp, const FBox& NewBounds, const FBox& DirtyArea)
			{
				UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Comp.GetWorld());
				if (NavSys)
				{
					NavSys->UpdateNavOctreeElementBounds(FNavigationElementHandle(&Comp), NewBounds, {DirtyArea});
				}
			});
			//UNavigationSystemBase::GetNavDataForPropsDelegate();
			UNavigationSystemBase::GetNavDataForActorDelegate().BindStatic(&UNavigationSystemV1::GetNavDataForActor);

#if WITH_RECAST
			UNavigationSystemBase::GetDefaultNavDataClassDelegate().BindLambda([]() { return ARecastNavMesh::StaticClass(); });
#endif // WITH_RECAST
			UNavigationSystemBase::VerifyNavigationRenderingComponentsDelegate().BindLambda([](UWorld& World, const bool bShow) {
				UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
				if (NavSys)
				{
					NavSys->VerifyNavigationRenderingComponents(bShow);
				}
			});
			UNavigationSystemBase::BuildDelegate().BindLambda([](UWorld& World) {
				UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
				if (NavSys)
				{
					NavSys->Build();
				}
			});
#if WITH_EDITOR
			UNavigationSystemBase::OnPIEStartDelegate().BindLambda([](UWorld& World) {
				UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
				if (NavSys)
				{
					NavSys->OnPIEStart();
				}
			});
			UNavigationSystemBase::OnPIEEndDelegate().BindLambda([](UWorld& World) {
				UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
				if (NavSys)
				{
					NavSys->OnPIEEnd();
				}
			});
			UNavigationSystemBase::UpdateLevelCollisionDelegate().BindLambda([](ULevel& Level) {
				UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&Level);
				if (NavSys)
				{
					NavSys->UpdateLevelCollision(&Level);
				}
			});
			UNavigationSystemBase::SetNavigationAutoUpdateEnableDelegate().BindStatic(&UNavigationSystemV1::SetNavigationAutoUpdateEnabled);
				/*.BindLambda([](const bool bNewEnable, UNavigationSystemBase* InNavigationSystem) {

			})*/
			UNavigationSystemBase::AddNavigationUpdateLockDelegate().BindLambda([](UWorld& World, uint8 Flags) {
				UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
				if (NavSys)
				{
					NavSys->AddNavigationUpdateLock(Flags);
				}
			});
			UNavigationSystemBase::RemoveNavigationUpdateLockDelegate().BindLambda([](UWorld& World, uint8 Flags) {
				UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
				if (NavSys)
				{
					NavSys->RemoveNavigationUpdateLock(Flags);
				}
			});
			UNavigationSystemBase::GetWorldPartitionNavigationDataBuilderOverlapDelegate().BindStatic(&UNavigationSystemV1::GetWorldPartitionNavigationDataBuilderOverlap);
#endif // WITH_EDITOR

#if ENABLE_VISUAL_LOG
			FVisualLogger::NavigationDataDumpDelegate.AddStatic(&NavigationDataDump);
#endif // ENABLE_VISUAL_LOG
		}
	};
	static FDelegatesInitializer DelegatesInitializer;

	// NOP line to silence code analysis warning: "Local variable 'DelegatesInitializer' is never used"
	(void)DelegatesInitializer;
	
	// Set to the ai module's crowd manager, this module may not exist at spawn time but then it will just fail to load
	CrowdManagerClass = FSoftObjectPath(TEXT("/Script/AIModule.CrowdManager"));

	// active tiles
	NextInvokersUpdateTime = 0.;
	ActiveTilesUpdateInterval = 1.f;
	bGenerateNavigationOnlyAroundNavigationInvokers = false;
	DataGatheringMode = ENavDataGatheringModeConfig::Instant;
	bShouldDiscardSubLevelNavData = true;

	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		// reserve some arbitrary size
		AsyncPathFindingQueries.Reserve( INITIAL_ASYNC_QUERIES_SIZE );
		NavDataRegistrationQueue.Reserve( REGISTRATION_QUEUE_SIZE );
	
		FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &UNavigationSystemV1::OnWorldPostActorTick);
		FWorldDelegates::LevelAddedToWorld.AddUObject(this, &UNavigationSystemV1::OnLevelAddedToWorld);
		FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &UNavigationSystemV1::OnLevelRemovedFromWorld);
		FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &UNavigationSystemV1::OnBeginTearingDown);
#if !UE_BUILD_SHIPPING
		FCoreDelegates::OnGetOnScreenMessages.AddUObject(this, &UNavigationSystemV1::GetOnScreenMessages);
#endif // !UE_BUILD_SHIPPING

		if (const UWorld* World = UNavigationSystemV1::GetWorld())
		{
			Repository = World->GetSubsystem<UNavigationObjectRepository>();
		}

		if (Repository == nullptr)
		{
			UE_LOG(LogNavigation, Warning, TEXT("UNavigationObjectRepository is required for navigation system operations."));
		}
	}
	else if (GetClass() == UNavigationSystemV1::StaticClass())
	{
		SetDefaultWalkableArea(UNavArea_Default::StaticClass());
		SetDefaultObstacleArea(UNavArea_Obstacle::StaticClass());
		
#if WITH_RECAST
		const FTransform RecastToUnrealTransform(Recast2UnrealMatrix());
		SetCoordTransform(ENavigationCoordSystem::Navigation, ENavigationCoordSystem::Unreal, RecastToUnrealTransform);
#endif // WITH_RECAST
	}
}

// UObject 销毁尾声：保险 CleanUp（CleanupUnsafe 模式不依赖 World 仍然有效）。
// 通常此时 CleanUp 已被 OnBeginTearingDown 调用过，bCleanUpDone=true 直接返回。
void UNavigationSystemV1::FinishDestroy()
{
	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		CleanUp(FNavigationSystem::ECleanupMode::CleanupUnsafe);
	}
	Super::FinishDestroy();
}

// 给 OnScreen Debug HUD（按 Apostrophe）补充导航统计：Repository / NavData / Octree 节点数 / 待办瓦片任务等。
void UNavigationSystemV1::GatherDebugLabels(TArray<FString>& InOutDebugLabels) const
{
	if (Repository)
	{
		InOutDebugLabels.Add(FString::Printf(TEXT("Repository Navigation Elements count: %i"), Repository->GetNumRegisteredElements()));
		InOutDebugLabels.Add(FString::Printf(TEXT("Repository UObjects count: %i"), Repository->GetNumRegisteredUObjects()));
		InOutDebugLabels.Add(FString::Printf(TEXT("Repository Custom NavLinks count: %i"), Repository->GetNumRegisteredCustomLinks()));
	}

	InOutDebugLabels.Add(FString::Printf(TEXT("NavData count: %i"), NavDataSet.Num()));
	InOutDebugLabels.Add(FString::Printf(TEXT("MainNavData: %s"), MainNavData ? *MainNavData->GetName() : TEXT("none")));
	InOutDebugLabels.Add(FString::Printf(TEXT("Custom NavLinks count: %i"), GetNumCustomLinks()));

	if (GetNavOctree())
	{
		int32 NumNodes = 0;
		int32 NumElements = 0;

		GetNavOctree()->FindNodesWithPredicate(
			[](FNavigationOctree::FNodeIndex /*ParentNodeIndex*/, FNavigationOctree::FNodeIndex /*NodeIndex*/, const FBoxCenterAndExtent&) { return true; },
			[&, Octree = GetNavOctree()](FNavigationOctree::FNodeIndex /*ParentNodeIndex*/, const FNavigationOctree::FNodeIndex NodeIndex, const FBoxCenterAndExtent&)
			{
				NumNodes++;
				NumElements += Octree->GetElementsForNode(NodeIndex).Num();
			});

		InOutDebugLabels.Add(FString::Printf(TEXT("Octree node count: %i"), NumNodes));
		InOutDebugLabels.Add(FString::Printf(TEXT("Octree element count: %i"), NumElements));
	}

#if WITH_NAVMESH_CLUSTER_LINKS
	InOutDebugLabels.Add(FString::Printf(TEXT("Using cluster links")));
#endif // WITH_NAVMESH_CLUSTER_LINKS

	if (IsActiveTilesGenerationEnabled()) // Checks bGenerateNavigationOnlyAroundNavigationInvokers
	{
		InOutDebugLabels.Add(FString::Printf(TEXT("Invoker Locations: %i"), GetInvokerLocations().Num()));
	}

	const int32 Running = GetNumRunningBuildTasks();
	const int32 Remaining = GetNumRemainingBuildTasks();
	if (Running || Remaining)
	{
		InOutDebugLabels.Add(FString::Printf(TEXT("Tile jobs running/remaining: %6d / %6d"), Running, Remaining));
	}

	InOutDebugLabels.Add(TEXT("")); // empty line
}

// 把本 NavSystem 切到/退出"静态模式"：bStaticRuntimeNavigation 决定，
// 同时关掉 Component 变化通知（静态模式下不需要监听）。
// 由 UNavigationSystemModuleConfig::CreateAndConfigureNavigationSystem 调用。
void UNavigationSystemV1::ConfigureAsStatic(bool bEnableStatic)
{
	bStaticRuntimeNavigation = bEnableStatic;
	SetWantsComponentChangeNotifies(!bEnableStatic);
}

// 控制 Component 属性变化时是否自动同步到 NavOctree。关闭后开发者需自己手动调 Update。
void UNavigationSystemV1::SetUpdateNavOctreeOnComponentChange(bool bNewUpdateOnComponentChange)
{
	bUpdateNavOctreeOnComponentChange = bNewUpdateOnComponentChange;
}

// 一次性初始化：UpdateAbstractNavData + CreateCrowdManager + 订阅 Repository 事件。
// 由 OnInitializeActors / OnWorldInitDone 调用，幂等（bInitialSetupHasBeenPerformed 守护）。
void UNavigationSystemV1::DoInitialSetup()
{
	if (bInitialSetupHasBeenPerformed)
	{
		return;
	}
	
	UpdateAbstractNavData();
	CreateCrowdManager();

	RegisterToRepositoryDelegates();

	bInitialSetupHasBeenPerformed = true;
}

// 确保 World 中存在唯一的 AAbstractNavData 实例（用于直线/直接路径不依赖具体 NavMesh）。
// 没有则在 PersistentLevel 上 Spawn 一份并标 RF_Transient。
void UNavigationSystemV1::UpdateAbstractNavData()
{
	if (IsValid(AbstractNavData))
	{
		return;
	}

	// spawn abstract nav data separately
	// it's responsible for direct paths and shouldn't be picked for any agent type as default one
	UWorld* NavWorld = GetWorld();
	for (TActorIterator<AAbstractNavData> It(NavWorld); It; ++It)
	{
		AAbstractNavData* Nav = (*It);
		if (IsValid(Nav))
		{
			AbstractNavData = Nav;
			break;
		}
	}

	if (AbstractNavData == NULL)
	{
		FNavDataConfig DummyConfig;
		DummyConfig.SetNavDataClass(AAbstractNavData::StaticClass());
		AbstractNavData = CreateNavigationDataInstanceInLevel(DummyConfig, nullptr);
		if (AbstractNavData)
		{
			AbstractNavData->SetFlags(RF_Transient);
		}
	}
}

// 给指定 SupportedAgent 设置 NavData 子类（同时同步 PreferredNavData）。
// 编辑器下若不是 CDO 调用，会同步把变更写到 CDO，让 Project Settings 里也能正确显示。
void UNavigationSystemV1::SetSupportedAgentsNavigationClass(int32 AgentIndex, TSubclassOf<ANavigationData> NavigationDataClass)
{
	const bool bCDOInEditor =
#if WITH_EDITOR
		// the CDO will have 0 supported agents if none are defined which is fine in the editor
		(GIsEditor && HasAnyFlags(RF_ClassDefaultObject))
#else
		false
#endif // WITH_EDITOR
		;

	check(SupportedAgents.IsValidIndex(AgentIndex) || bCDOInEditor);

	if (SupportedAgents.IsValidIndex(AgentIndex))
	{
	SupportedAgents[AgentIndex].SetNavDataClass(NavigationDataClass);

	// keep preferred navigation data class in sync with actual class
	// this will be passed to navigation data actor and will be required
	// for comparisons done in DoesSupportAgent calls
	//
	// "Any" navigation data preference is valid only for instanced agents
	SupportedAgents[AgentIndex].SetPreferredNavData(NavigationDataClass);
	}

#if WITH_EDITOR
	if (GIsEditor)
	{
		if (HasAnyFlags(RF_ClassDefaultObject) == false)
		{
			// set it at CDO to properly show up in project settings
			// @hack the reason for doing it this way is that engine doesn't handle default TSubclassOf properties
			//	set to game-specific classes;
			UNavigationSystemV1* NavigationSystemCDO = GetMutableDefault<UNavigationSystemV1>(GetClass());
			NavigationSystemCDO->SetSupportedAgentsNavigationClass(AgentIndex, NavigationDataClass);
		}
	}
#endif // WITH_EDITOR
}

// UObject 初始化末尾钩子：此时所有 UPROPERTY 已反序列化完毕。
// 本方法：
//   - 收集场景中已加载的 UNavArea 派生类并注册
//   - 应用 SupportedAgentsMask 过滤 SupportedAgents
//   - 给每个 Agent 设置 NavigationDataClass
//   - 配置 Dirty Area 警告阈值
//   - 应用 InitialNavBuildingLockFlags（可能包括 InitialLock/NoUpdateInEditor 等）
//   - 订阅 ActorMoved / PostLoadMapWithWorld / NavigationDirtyEvent / ReloadComplete
void UNavigationSystemV1::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject) == false)
	{
		// Populate our NavAreaClasses list with all known nav area classes.
		// If more are loaded after this they will be registered as they come
		TArray<UClass*> CurrentNavAreaClasses;
		GetDerivedClasses(UNavArea::StaticClass(), CurrentNavAreaClasses);
		for (UClass* NavAreaClass : CurrentNavAreaClasses)
		{
			RegisterNavAreaClass(NavAreaClass);
		}

		ApplySupportedAgentsFilter();
		for (int32 AgentIndex = 0; AgentIndex < SupportedAgents.Num(); ++AgentIndex)
		{
			FNavDataConfig& SupportedAgentConfig = SupportedAgents[AgentIndex];
			SetSupportedAgentsNavigationClass(AgentIndex, SupportedAgentConfig.GetNavDataClass<ANavigationData>());
		}

		DefaultDirtyAreasController.SetDirtyAreaWarningSizeThreshold(DirtyAreaWarningSizeThreshold);
	
		if (bInitialBuildingLocked)
		{
			InitialNavBuildingLockFlags |= ENavigationBuildLock::InitialLock;
		}

		uint8 UseLockFlags = InitialNavBuildingLockFlags;

		AddNavigationBuildLock(UseLockFlags);

		// register for any actor move change
#if WITH_EDITOR
		if ( GIsEditor )
		{
			GEngine->OnActorMoved().AddUObject(this, &UNavigationSystemV1::OnActorMoved);
		}
#endif
		FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UNavigationSystemV1::OnPostLoadMap);
		UNavigationSystemV1::NavigationDirtyEvent.AddUObject(this, &UNavigationSystemV1::OnNavigationDirtied);

		ReloadCompleteDelegateHandle = FCoreUObjectDelegates::ReloadCompleteDelegate.AddUObject(this, &UNavigationSystemV1::OnReloadComplete);
	}
}

// 真正 new FNavigationOctree 实例：中心 = 可导航世界 AABB 的中心，半径 = AABB 最大半径，
// 默认 64000（UE 单位，约 640m）。数据收集模式 + Modifier 警告时间阈值也在这里配给 Octree。
void UNavigationSystemV1::ConstructNavOctree()
{
	// Default values to keep previous behavior.
	FVector NavOctreeCenter = FVector::ZeroVector;
	double NavOctreeRadius = 64000;

	const FBox Bounds = GetNavigableWorldBounds();
	if(Bounds.IsValid)
	{
		NavOctreeCenter = Bounds.GetCenter();
		NavOctreeRadius = Bounds.GetExtent().GetAbsMax();
	}

	FNavigationDataHandler NavHandler(DefaultOctreeController, DefaultDirtyAreasController);
	NavHandler.ConstructNavOctree(NavOctreeCenter, NavOctreeRadius, DataGatheringMode, GatheringNavModifiersWarningLimitTime);
}

// 条件性构造 NavOctree：
//   1) 查 RequiresNavOctree() ——若所有 NavData 都 Static 则不创建（节省内存）。
//   2) 构造 NavOctree 并根据运行时生成类型决定是否存储几何。
//   3) 若未锁 Octree，则注册当前 World 的所有 Level 碰撞 + Repository 里已有元素。
// 期间产生的 DirtyAreas 会被丢弃（TGuardValue），否则会引发一次无意义的全量重建。
bool UNavigationSystemV1::ConditionalPopulateNavOctree()
{
	// Discard all navigation updates caused by octree construction
	UE_LOG(LogNavigationDirtyArea, VeryVerbose, TEXT("%hs: Reseting Dirty Areas added during octree construction. DirtyAreas.Num = [%d]."), __FUNCTION__, DefaultDirtyAreasController.DirtyAreas.Num());
	TGuardValue<TArray<FNavigationDirtyArea>> DirtyGuard(DefaultDirtyAreasController.DirtyAreas, TArray<FNavigationDirtyArea>());

	// See if any of registered navigation data need navoctree
	bSupportRebuilding = RequiresNavOctree();

	if (bSupportRebuilding)
	{
		ConstructNavOctree();
		if (DefaultOctreeController.IsValid())
		{
			const ERuntimeGenerationType RuntimeGenerationType = GetRuntimeGenerationType();
			const bool bStoreNavGeometry = (RuntimeGenerationType == ERuntimeGenerationType::Dynamic);
			DefaultOctreeController.SetNavigableGeometryStoringMode(bStoreNavGeometry ? FNavigationOctree::StoreNavGeometry : FNavigationOctree::SkipNavGeometry);
			if (bStoreNavGeometry)
			{
#if WITH_RECAST
				DefaultOctreeController.NavOctree->GeometryExportDelegate =
					FNavigationOctree::FGeometryExportDelegate::CreateStatic(&FRecastGeometryExport::ExportElementGeometry);
#endif // WITH_RECAST
			}

			if (!DefaultOctreeController.IsNavigationOctreeLocked())
			{
				UWorld* World = GetWorld();
				check(World);

				// Register level collisions
				for (int32 LevelIndex = 0; LevelIndex < World->GetNumLevels(); ++LevelIndex)
				{
					ULevel* Level = World->GetLevel(LevelIndex);
					if (ensure(Level) && Level->bIsVisible)
					{
						AddLevelToOctree(*Level);
					}
				}

				if (Repository != nullptr)
				{
					// Register all elements registered in the repository world subsystem.
					Repository->ForEachNavigationElement([this](const TSharedRef<const FNavigationElement>& Element)
						{
							RegisterNavigationElementWithNavOctree(Element, FNavigationOctreeController::OctreeUpdate_Default);
						});
				}
			}
		}
	}
	else
	{
		// Discard current octree along with pending updates
		DestroyNavOctree();
	}

	// Add all found elements to octree, this will not add new dirty areas to navigation
	FNavigationDataHandler NavHandler(DefaultOctreeController, DefaultDirtyAreasController);
	NavHandler.ProcessPendingOctreeUpdates();

	return bSupportRebuilding;
}

#if WITH_EDITOR
// 编辑器：属性链改动回调（监听到 SupportedAgents[i].NavDataClass / bAllowClientSideNavigation）。
//   - NavDataClass 改：调 SetSupportedAgentsNavigationClass + SaveConfig 写回 ini
//   - AllowClientSideNavigation 改（CDO 上）：同步给所有 ModuleConfig 实例
void UNavigationSystemV1::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	static const FName NAME_NavDataClass = FNavDataConfig::GetNavigationDataClassPropertyName();
	static const FName NAME_SupportedAgents = GET_MEMBER_NAME_CHECKED(UNavigationSystemV1, SupportedAgents);
	static const FName NAME_AllowClientSideNavigation = GET_MEMBER_NAME_CHECKED(UNavigationSystemV1, bAllowClientSideNavigation);

	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		FName PropName = PropertyChangedEvent.Property->GetFName();
		if (PropName == NAME_NavDataClass)
		{
			int32 SupportedAgentIndex = PropertyChangedEvent.GetArrayIndex(NAME_SupportedAgents.ToString());
			if (SupportedAgents.IsValidIndex(SupportedAgentIndex))
			{
				// reflect the change to SupportedAgent's 
				TSubclassOf<ANavigationData> NavClass = SupportedAgents[SupportedAgentIndex].GetNavDataClass<ANavigationData>();
				SetSupportedAgentsNavigationClass(SupportedAgentIndex, NavClass);
				SaveConfig();
			}
		}
		else if (PropName == NAME_AllowClientSideNavigation && HasAnyFlags(RF_ClassDefaultObject))
		{
			for (FThreadSafeObjectIterator It(UNavigationSystemModuleConfig::StaticClass()); It; ++It)
			{
				((UNavigationSystemModuleConfig*)*It)->UpdateWithNavSysCDO(*this);
			}
		}
	}
}

// 编辑器：单属性变更回调。
//   - bGenerateNavigationOnlyAroundNavigationInvokers 切换 → 通知所有 NavData
//   - AgentRadius 改 → 在 World Partition 地图弹提示，需要 ResaveActorsBuilder 让分区生效
void UNavigationSystemV1::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName NAME_GenerateNavigationOnlyAroundNavigationInvokers = GET_MEMBER_NAME_CHECKED(UNavigationSystemV1, bGenerateNavigationOnlyAroundNavigationInvokers);

	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		FName PropName = PropertyChangedEvent.Property->GetFName();
		if (PropName == NAME_GenerateNavigationOnlyAroundNavigationInvokers)
		{
			OnGenerateNavigationOnlyAroundNavigationInvokersChanged();
		}
		else if (PropName == GET_MEMBER_NAME_CHECKED(FNavDataConfig, AgentRadius))
		{
			const bool bIsCDO = HasAnyFlags(RF_ClassDefaultObject);
			if (!bIsCDO)
			{
				const UWorld* World = GetWorld();
				if (World && World->IsPartitionedWorld())
				{
					FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NeedToRunPartitionResaveActorsBuilder",
						"In a world partitioned map, changing this property changes the partitioning of actors.\n"
						"For the change to take effect on partitioning, actors needs to be resaved.\n"
						"Run the WorldPartitionResaveActorsBuilder to update the whole map."));	
				}
			}
		}
	}
}
#endif // WITH_EDITOR

// World 初始化阶段（关卡 Actor 已加载/初始化）的钩子，本类无逻辑——保留为子类扩展点。
void UNavigationSystemV1::OnInitializeActors()
{
	
}

// FWorldDelegates::OnWorldBeginTearDown 回调：World 即将销毁时调 CleanUp（CleanupWithWorld 模式）。
// 必须在 World 仍可访问时执行，因为 CleanUp 内会 ResetUniqueId 等需要 World 信息的操作。
void UNavigationSystemV1::OnBeginTearingDown(UWorld* World)
{
	// If the world being torn down is my world context
	if (World == GetWorld())
	{
		CleanUp(FNavigationSystem::ECleanupMode::CleanupWithWorld);
	}
}

// World 初始化完成时调用：标记 bWorldInitDone、广播 OnNavigationInitDone，
// 根据 RunMode 决定是否立即 RebuildAll / 释放 InitialLock。
void UNavigationSystemV1::OnWorldInitDone(FNavigationSystemRunMode Mode)
{
	UNavigationSystemBase::OnNavigationInitStartStaticDelegate().Broadcast(*this);

	OperationMode = Mode;
	DoInitialSetup();
	
	UWorld* World = GetWorld();
	check(World);

	// process all registered link from the repository subsystem
	// (since it's possible navigation system was not ready by the time
	// those links were serialized-in or spawned)
	if (!bWorldInitDone)
	{
		ProcessCustomLinkPendingRegistration();
	}

	if (IsThereAnywhereToBuildNavigation() == false)
	{
		// remove all navigation data instances
		for (TActorIterator<ANavigationData> It(World); It; ++It)
		{
			ANavigationData* Nav = (*It);
			if (IsValid(Nav) && Nav != GetAbstractNavData())
			{
				UnregisterNavData(Nav);
				Nav->CleanUpAndMarkPendingKill();
				bNavDataRemovedDueToMissingNavBounds = true;
			}
		}

		if (FNavigationSystem::IsEditorRunMode(OperationMode))
		{
			RemoveNavigationBuildLock(InitialNavBuildingLockFlags, ELockRemovalRebuildAction::NoRebuild);
		}
	}
	else
	{
		// Discard all bounds updates that was submitted during world initialization, 
		// to avoid navigation rebuild right after map is loaded
		PendingNavBoundsUpdates.Empty();
		
		// gather navigable bounds
		GatherNavigationBounds();

		// gather all navigation data instances and register all not-yet-registered
		// (since it's quite possible navigation system was not ready by the time
		// those instances were serialized-in or spawned)
		RegisterNavigationDataInstances();

		if (bAutoCreateNavigationData == true)
		{
			SpawnMissingNavigationData();
			// in case anything spawned has registered
			ProcessRegistrationCandidates();
		}
		else
		{
			const bool bIsBuildLocked = IsNavigationBuildingLocked();
			const bool bCanRebuild = !bIsBuildLocked && GetIsAutoUpdateEnabled();

			if (GetDefaultNavDataInstance(FNavigationSystem::DontCreate) != nullptr)
			{
				// trigger navmesh update
				for (TActorIterator<ANavigationData> It(World); It; ++It)
				{
					ANavigationData* NavData = (*It);
					if (NavData != nullptr)
					{
						const ERegistrationResult Result = RegisterNavData(NavData);
						LogNavDataRegistrationResult(Result);

						if (Result == RegistrationSuccessful)
						{
							// allowing full rebuild of the entire navmesh only for the fully dynamic generation modes
							// other modes partly rely on the serialized data and full rebuild would wipe it out
							if (bCanRebuild && IsAllowedToRebuild())
							{
								NavData->RebuildAll();
							}
						}
						else if (Result != RegistrationFailed_DataPendingKill
							&& Result != RegistrationFailed_AgentNotValid
							)
						{
							NavData->CleanUpAndMarkPendingKill();
						}
					}
				}
			}
		}

		if (FNavigationSystem::IsEditorRunMode(OperationMode))
		{
			// don't lock navigation building in editor
			RemoveNavigationBuildLock(InitialNavBuildingLockFlags, ELockRemovalRebuildAction::NoRebuild);
		}

		// See if any of registered navigation data needs NavOctree
		ConditionalPopulateNavOctree();

		// All navigation actors are registered
		// Add NavMesh parts from all sub-levels that were streamed in prior NavMesh registration
		const auto& Levels = World->GetLevels();
		for (ULevel* Level : Levels)
		{
			if (!Level->IsPersistentLevel() && Level->bIsVisible)
			{
				for (ANavigationData* NavData : NavDataSet)
				{
					if (NavData)
					{
						NavData->OnStreamingLevelAdded(Level, World);
					}
				}
			}
		}
	}

#if	WITH_EDITOR
	if (FNavigationSystem::IsEditorRunMode(Mode))
	{
		// make sure this static get applied to this instance
		bNavigationAutoUpdateEnabled = !bNavigationAutoUpdateEnabled; 
		SetNavigationAutoUpdateEnabled(!bNavigationAutoUpdateEnabled, this);
		
		// update navigation invokers
		if (bGenerateNavigationOnlyAroundNavigationInvokers)
		{
			for (TObjectIterator<UNavigationInvokerComponent> It; It; ++It)
			{
				if (World == It->GetWorld())
				{
					It->RegisterWithNavigationSystem(*this);
				}
			}
		}

		// update navdata after loading world
		if (GetIsAutoUpdateEnabled())
		{
			const bool bIsLoadTime = true;
			RebuildAll(bIsLoadTime);
		}
	}
#endif

	if (!DefaultDirtyAreasController.bCanAccumulateDirtyAreas)
	{
		DefaultDirtyAreasController.DirtyAreas.Empty();
	}

	// Dirty area controller reports oversized dirty areas only in game mode and if we are not using active tile generation.
	// When using active tile generation, this is reported only if tiles are actually marked dirty (ex: see MarkDirtyTiles).
	DefaultDirtyAreasController.SetCanReportOversizedDirtyArea(Mode == FNavigationSystemRunMode::GameMode && !IsActiveTilesGenerationEnabled());

	for (const ANavigationData* NavData : NavDataSet)
	{
		if (NavData)
		{
#if WITH_RECAST
			const ARecastNavMesh* RecastNavMesh = Cast<ARecastNavMesh>(NavData);
			if (RecastNavMesh && RecastNavMesh->bIsWorldPartitioned && NavData->GetRuntimeGenerationMode() > ERuntimeGenerationType::Static)
			{
				DefaultDirtyAreasController.SetUseWorldPartitionedDynamicMode(true);
				break;
			}
#endif // WITH_RECAST
		}
	}

	bWorldInitDone = true;
	OnNavigationInitDone.Broadcast();
	UNavigationSystemBase::OnNavigationInitDoneStaticDelegate().Broadcast(*this);
}

// 扫描 World 里已存在的 ANavigationData Actor（包括子 Level 里的）并推入注册队列。
// 在 OnWorldInitDone 里调；后续 Tick 的 ProcessRegistrationCandidates 真正完成注册。
void UNavigationSystemV1::RegisterNavigationDataInstances()
{
	UWorld* World = GetWorld();

	bool bProcessRegistration = false;
	for (TActorIterator<ANavigationData> It(World); It; ++It)
	{
		ANavigationData* Nav = (*It);
		if (IsValid(Nav) && Nav->IsRegistered() == false)
		{
			RequestRegistrationDeferred(*Nav);
			bProcessRegistration = true;
		}
	}
	if (bProcessRegistration == true)
	{
		ProcessRegistrationCandidates();
	}
}

// 创建 Crowd Manager 实例：按配置的 CrowdManagerClass 实例化并 SetCrowdManager。
// CrowdManager 负责群体避让/局部避障；类未配置时整个步骤跳过。
void UNavigationSystemV1::CreateCrowdManager()
{
	UClass* CrowdManagerClassInstance = CrowdManagerClass.Get();
	if (CrowdManagerClassInstance)
	{
		UCrowdManagerBase* ManagerInstance = NewObject<UCrowdManagerBase>(this, CrowdManagerClassInstance);
		// creating an instance when we have a valid class should never fail
		check(ManagerInstance);
		SetCrowdManager(ManagerInstance);
	}
}

// 替换 CrowdManager：旧实例先 RemoveAgent 全部 Agent 后释放，再赋新实例。
// 传入 nullptr 表示彻底关闭群体管理（CleanUp 时使用）。
void UNavigationSystemV1::SetCrowdManager(UCrowdManagerBase* NewCrowdManager)
{
	if (NewCrowdManager == CrowdManager.Get())
	{
		return;
	}

	if (CrowdManager.IsValid())
	{
		CrowdManager->RemoveFromRoot();
	}
	CrowdManager = NewCrowdManager;
	if (NewCrowdManager != nullptr)
	{
		CrowdManager->AddToRoot();
	}
}

// 收集所有 NavData Generator 的"时间切片"信息：每个 NavData 是否启用切片、剩余瓦片任务数、当前已用切片时长。
// 用于 NavRegenTimeSliceManager 在多个 NavData 间动态分配下一帧可用预算。
void UNavigationSystemV1::CalcTimeSlicedUpdateData(TArray<double>& OutCurrentTimeSlicedBuildTaskDurations, TArray<bool>& OutIsTimeSlicingArray, bool& bOutAnyNonTimeSlicedGenerators, TArray<int32, TInlineAllocator<8>>& OutNumTimeSlicedRemainingBuildTasksArray)
{
	OutNumTimeSlicedRemainingBuildTasksArray.SetNumZeroed(NavDataSet.Num());
	OutIsTimeSlicingArray.SetNumZeroed(NavDataSet.Num());
	bOutAnyNonTimeSlicedGenerators = false;
	OutCurrentTimeSlicedBuildTaskDurations.Reset(NavDataSet.Num());

	for (int32 NavDataIdx = 0; NavDataIdx < NavDataSet.Num(); ++NavDataIdx)
	{
		const ANavigationData* NavData = NavDataSet[NavDataIdx];
		const FNavDataGenerator* Generator = NavData ? NavData->GetGenerator() : nullptr;
		if (Generator)
		{
			double TimeSlicedBuildTaskDuration = 0.;
			int32 NumRemainingBuildTasksTemp = 0;

			if (Generator->GetTimeSliceData(NumRemainingBuildTasksTemp, TimeSlicedBuildTaskDuration))
			{
				OutIsTimeSlicingArray[NavDataIdx] = true;
				OutNumTimeSlicedRemainingBuildTasksArray[NavDataIdx] += NumRemainingBuildTasksTemp;
				if (TimeSlicedBuildTaskDuration > 0.)
				{
					OutCurrentTimeSlicedBuildTaskDurations.Push(TimeSlicedBuildTaskDuration);
				}
			}
			else
			{
				bOutAnyNonTimeSlicedGenerators = true;
			}
		}
	}
}

// NavigationSystem 的主 Tick。每帧：
//   1) CalcAverageDeltaTime 维持时间切片的移动平均
//   2) 消费 PendingNavBoundsUpdates → PerformNavigationBoundsUpdate
//   3) 按 DirtyAreasUpdateFreq 周期把 Dirty Areas 推给各 NavData（RebuildDirtyAreas）
//   4) 派发上一帧异步寻路结果（DispatchAsyncQueriesResults），发起新一轮（TriggerAsyncQueries）
//   5) 按 ActiveTilesUpdateInterval 更新 Invokers + UpdateNavDataActiveTiles
//   6) 处理 NavDataRegistrationQueue、PendingOctreeUpdates
// 线程：GameThread。
void UNavigationSystemV1::Tick(float DeltaSeconds)
{
	SET_DWORD_STAT(STAT_Navigation_ObservedPathsCount, 0);

	UWorld* World = GetWorld();

	if (World == nullptr 
		|| (bTickWhilePaused == false && World->IsPaused())
#if WITH_EDITOR
		|| (bIsPIEActive && !World->IsGameWorld())
#endif // WITH_EDITOR
		)
	{
		return;
	}

	if (PendingNavBoundsUpdates.Num() > 0)
	{
		PerformNavigationBoundsUpdate(PendingNavBoundsUpdates);
		PendingNavBoundsUpdates.Reset();
	}

	if (NavDataRegistrationQueue.Num() > 0)
	{
		CSV_SCOPED_TIMING_STAT(NavigationBuildDetailed, Navigation_ProcessRegistrationCandidates);
		ProcessRegistrationCandidates();
	}

	if (DefaultOctreeController.PendingUpdates.Num() > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_Navigation_AddingActorsToNavOctree);
		CSV_SCOPED_TIMING_STAT(NavigationBuildDetailed, Navigation_ProcessPendingOctreeUpdates);

		SCOPE_CYCLE_COUNTER(STAT_Navigation_BuildTime)
		STAT(double ThisTime = 0);
		{
			SCOPE_SECONDS_COUNTER(ThisTime);
			FNavigationDataHandler NavHandler(DefaultOctreeController, DefaultDirtyAreasController);
			NavHandler.ProcessPendingOctreeUpdates();
		}
		INC_FLOAT_STAT_BY(STAT_Navigation_CumulativeBuildTime,(float)ThisTime*1000);
	}
		
	if (IsNavigationBuildingLocked() == false)
	{
		if (bGenerateNavigationOnlyAroundNavigationInvokers)
		{
			CSV_SCOPED_TIMING_STAT(NavigationBuildDetailed, Navigation_UpdateInvokers);
			UpdateInvokers();
		}

		{
			CSV_SCOPED_TIMING_STAT(NavigationBuildDetailed, Navigation_RebuildDirtyAreas);
			RebuildDirtyAreas(DeltaSeconds);
		}

		// Tick navigation mesh async builders
		if (bAsyncBuildPaused == false)
		{
			CSV_SCOPED_TIMING_STAT(NavigationBuildDetailed, Navigation_TickAsyncBuild);
			SCOPE_CYCLE_COUNTER(STAT_Navigation_TickAsyncBuild);

			bool bDoStandardTickAsync = true;

			if (NavRegenTimeSliceManager.DoTimeSlicedUpdate())
			{
				TArray<int32, TInlineAllocator<8>> NumTimeSlicedRemainingBuildTasksArray;
				NumTimeSlicedRemainingBuildTasksArray.SetNumZeroed(NavDataSet.Num());
				
				TArray<double> CurrentTimeSlicedBuildTaskDurations;
				TArray<bool> IsTimeSlicingArray;
				bool bAnyNonTimeSlicedGenerators = false;

				NavRegenTimeSliceManager.CalcAverageDeltaTime(GFrameCounter);

				CalcTimeSlicedUpdateData(CurrentTimeSlicedBuildTaskDurations, IsTimeSlicingArray, bAnyNonTimeSlicedGenerators, NumTimeSlicedRemainingBuildTasksArray);

				int32 NumTimeSlicedRemainingBuildTasks = 0;
				for (const int32 NumTasks : NumTimeSlicedRemainingBuildTasksArray)
				{
					NumTimeSlicedRemainingBuildTasks += NumTasks;
				}

#if !UE_BUILD_SHIPPING
				NavRegenTimeSliceManager.LogTileStatistics(NavDataSet);
#endif // !UE_BUILD_SHIPPING
				
				if (NumTimeSlicedRemainingBuildTasks > 0)
				{
					NavRegenTimeSliceManager.CalcTimeSliceDuration(NavDataSet, NumTimeSlicedRemainingBuildTasks, CurrentTimeSlicedBuildTaskDurations);

					//The general idea here is to tick any non time sliced generators once per frame. Time sliced generators we aim to tick one per frame and move to the next, next frame. In the
					//case where one time sliced generator doesn't use the whole time slice we move to the next time sliced generator. That generator will only be considered to have a full frames
					//processing if either it runs out of work or uses a large % of the time slice. Depending we either tick it again next frame or go to the next time sliced generator (next frame).
					bool bNavDataIdxSet = false;
					int32 NavDataIdxTemp = NavRegenTimeSliceManager.GetNavDataIdx();
					constexpr double RemainingFractionConsideredWholeTick = 0.8;
					const int32 FirstNavDataIdx = NavDataIdxTemp = NavDataIdxTemp % NavDataSet.Num();

					for (int32 NavDataIter = 0; NavDataIter < NavDataSet.Num(); ++NavDataIter)
					{
						if (ANavigationData* NavData = NavDataSet[NavDataIdxTemp])
						{
							if (IsTimeSlicingArray[NavDataIdxTemp])
							{
								if (NavRegenTimeSliceManager.GetTimeSlicer().IsTimeSliceFinishedCached())
								{
									//if we haven't set the NavDataIdx then this is the TimeSliced Generator to process next frame
									if (!bNavDataIdxSet)
									{
										NavRegenTimeSliceManager.SetNavDataIdx(NavDataIdxTemp);
										bNavDataIdxSet = true;
									}

									//if the time slice is finished and we have no non time sliced generators then stop TickAsyncBuild, otherwise continue
									if (!bAnyNonTimeSlicedGenerators)
									{
										break;
									}
									continue;
								}
								else if (NavRegenTimeSliceManager.GetTimeSlicer().GetRemainingDurationFraction() < RemainingFractionConsideredWholeTick)
								{
									//don't check bNavDataIdxSet here, either this time sliced generator won't get enough time this frame to be considered
									//a whole tick or it will complete and there is some time sliced left - in the later case next frame we'll process the 
									//next time sliced generator we process this frame or the first Idx we processed this frame
									NavRegenTimeSliceManager.SetNavDataIdx(NavDataIdxTemp);
									bNavDataIdxSet = true;
								}
							}
							NavData->TickAsyncBuild(DeltaSeconds);
						}
						//Increment and mod NavDataIdxTemp
						++NavDataIdxTemp;
						NavDataIdxTemp %= NavDataSet.Num();
					}

					//if we processed all the time sliced generators and there is still some time slice left
					//OR if we haven't SetNavDataIdx() we should start next frame where we started this frame
					if (!NavRegenTimeSliceManager.GetTimeSlicer().IsTimeSliceFinishedCached() || !bNavDataIdxSet)
					{
						NavRegenTimeSliceManager.SetNavDataIdx(FirstNavDataIdx);
						bNavDataIdxSet = true;
					}
					//don't do the standard TickASyncBuild as we have already processed the regen appropriately 
					bDoStandardTickAsync = false;
				}
			}

			//if we aren't time sliced rebuilding and / or if there aren't any time sliced nav data's with work to do just tick all nav data
			if (bDoStandardTickAsync)
			{
				for (ANavigationData* NavData : NavDataSet)
				{
					if (NavData)
					{
						NavData->TickAsyncBuild(DeltaSeconds);
					}
				}
			}
		}
	}

#if !UE_BUILD_SHIPPING && CSV_PROFILER_STATS
	for (const TObjectPtr<ANavigationData>& NavigationData : NavDataSet)
	{
		if (NavigationData)
		{
			if (const FNavDataGenerator* Generator = NavigationData->GetGenerator())
			{
				const int32 BuildTaskNum = Generator->GetNumRemaningBuildTasks();
				const FString StatName = FString::Printf(TEXT("NumRemainingTasks_%s"), *GetNameSafe(NavigationData)); 
				FCsvProfiler::RecordCustomStat(*StatName, CSV_CATEGORY_INDEX(NavTasks), BuildTaskNum, ECsvCustomStatOp::Set);
			}
		}
	}
	
	CSV_CUSTOM_STAT(NavigationSystem, NumRunningTasks, GetNumRunningBuildTasks(), ECsvCustomStatOp::Set);
#endif // !UE_BUILD_SHIPPING && CSV_PROFILER_STATS

	// In multithreaded configuration we can process async pathfinding queries
	// in dedicated task while dispatching completed queries results on the main thread.
	// The created task can start and append new result right away so we transfer
	// completed queries before to keep the list safe.
	TArray<FAsyncPathFindingQuery> AsyncPathFindingCompletedQueriesToDispatch;
	Swap(AsyncPathFindingCompletedQueriesToDispatch, AsyncPathFindingCompletedQueries);

	// Trigger the async pathfinding queries (new ones and those that may have been postponed from last frame)
	if (AsyncPathFindingQueries.Num() > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_Navigation_TickAsyncPathfinding);
		TriggerAsyncQueries(AsyncPathFindingQueries);
		AsyncPathFindingQueries.Reset();
	}

	// Dispatch async pathfinding queries results from last frame
	DispatchAsyncQueriesResults(AsyncPathFindingCompletedQueriesToDispatch);

	if (CrowdManager.IsValid())
	{
		CSV_SCOPED_TIMING_STAT(NavigationBuildDetailed, Navigation_CrowdManager);
		CrowdManager->Tick(DeltaSeconds);
	}
}

// GC 引用收集：把 CrowdManager 和（运行时模式下）NavAreaClasses 加入引用集，避免被回收。
// 编辑器非 PIE 模式下不锁 NavAreaClasses，方便热重载/CDO 替换。
void UNavigationSystemV1::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	UNavigationSystemV1* This = CastChecked<UNavigationSystemV1>(InThis);
	Collector.AddReferencedObject(This->CrowdManager, InThis);

	// don't reference NavAreaClasses in editor (unless PIE is active)
	if (!FNavigationSystem::IsEditorRunMode(This->OperationMode))
	{
		Collector.AddReferencedObjects(This->NavAreaClasses, InThis);
	}
}

#if WITH_EDITOR
void UNavigationSystemV1::SetNavigationAutoUpdateEnabled(bool bNewEnable, UNavigationSystemBase* InNavigationSystemBase)
{
	if (bNewEnable != bNavigationAutoUpdateEnabled)
	{
		bNavigationAutoUpdateEnabled = bNewEnable;

		UNavigationSystemV1* NavSystem = Cast<UNavigationSystemV1>(InNavigationSystemBase);
		if (NavSystem)
		{
			const bool bCurrentIsEnabled = NavSystem->GetIsAutoUpdateEnabled();
			NavSystem->DefaultDirtyAreasController.bCanAccumulateDirtyAreas = bCurrentIsEnabled
				|| (!FNavigationSystem::IsEditorRunMode(NavSystem->OperationMode) && NavSystem->OperationMode != FNavigationSystemRunMode::InvalidMode);

			if (bCurrentIsEnabled)
			{
				NavSystem->RemoveNavigationBuildLock(ENavigationBuildLock::NoUpdateInEditor);
			}
			else
			{
#if !UE_BUILD_SHIPPING
				NavSystem->DefaultDirtyAreasController.bDirtyAreasReportedWhileAccumulationLocked = false;
#endif // !UE_BUILD_SHIPPING
				NavSystem->AddNavigationBuildLock(ENavigationBuildLock::NoUpdateInEditor);
			}
		}
	}
}
#endif // WITH_EDITOR

//----------------------------------------------------------------------//
// Public querying interface
//----------------------------------------------------------------------//
// 同步寻路主入口（带 Agent 版）：按 AgentProperties 选 NavData → 调同步版本。
FPathFindingResult UNavigationSystemV1::FindPathSync(const FNavAgentProperties& AgentProperties, FPathFindingQuery Query, EPathFindingMode::Type Mode)
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_PathfindingSync);
	CSV_SCOPED_TIMING_STAT(NavigationSystem, PathfindingSync);

	if (Query.NavData.IsValid() == false)
	{
		Query.NavData = GetNavDataForProps(AgentProperties, Query.StartLocation);
	}

	FPathFindingResult Result(ENavigationQueryResult::Error);
	if (Query.NavData.IsValid())
	{
		if (Mode == EPathFindingMode::Hierarchical)
		{
			Result = Query.NavData->FindHierarchicalPath(AgentProperties, Query);
		}
		else
		{
			Result = Query.NavData->FindPath(AgentProperties, Query);
		}
	}

	return Result;
}

// 同步寻路：Query.NavData 已指定。最终落到 ANavigationData::FindPath（函数指针），
// ARecastNavMesh 的函数指针指向自己的静态方法 ARecastNavMesh::FindPath（RecastNavMesh.cpp:~3509）→ FPImplRecastNavMesh::FindPath。
// 阻塞 GameThread；高频路径查询请使用 FindPathAsync。
FPathFindingResult UNavigationSystemV1::FindPathSync(FPathFindingQuery Query, EPathFindingMode::Type Mode)
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_PathfindingSync);
	CSV_SCOPED_TIMING_STAT(NavigationSystem, PathfindingSync);

	if (Query.NavData.IsValid() == false)
	{
		Query.NavData = GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
	}
	
	FPathFindingResult Result(ENavigationQueryResult::Error);
	if (Query.NavData.IsValid())
	{
		if (Mode == EPathFindingMode::Regular)
		{
			Result = Query.NavData->FindPath(Query.NavAgentProperties, Query);
		}
		else // EPathFindingMode::Hierarchical
		{
			Result = Query.NavData->FindHierarchicalPath(Query.NavAgentProperties, Query);
		}
	}

	return Result;
}

// 同步路径存在性测试：与 FindPathSync 区别在于不构造完整路径，只判断是否可达，
// 可选返回访问节点数。开销显著低于 FindPathSync，适合 AI 决策中的"能否到达"判定。
bool UNavigationSystemV1::TestPathSync(FPathFindingQuery Query, EPathFindingMode::Type Mode, int32* NumVisitedNodes) const
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_PathfindingSync);
	CSV_SCOPED_TIMING_STAT(NavigationSystem, PathfindingSync);

	if (Query.NavData.IsValid() == false)
	{
		Query.NavData = GetDefaultNavDataInstance();
	}
	
	bool bExists = false;
	if (Query.NavData.IsValid())
	{
		if (Mode == EPathFindingMode::Hierarchical)
		{
			bExists = Query.NavData->TestHierarchicalPath(Query.NavAgentProperties, Query, NumVisitedNodes);
		}
		else
		{
			bExists = Query.NavData->TestPath(Query.NavAgentProperties, Query, NumVisitedNodes);
		}
	}

	return bExists;
}

// 把异步寻路 Query 加入待处理队列；下一帧 Tick 的 TriggerAsyncQueries 会批量派发到后台线程。
// 必须在 GameThread 调用。
void UNavigationSystemV1::AddAsyncQuery(const FAsyncPathFindingQuery& Query)
{
	check(IsInGameThread());
	AsyncPathFindingQueries.Add(Query);
}

// 异步寻路入口：把 Query+ResultDelegate 塞进 AsyncPathFindingQueries，返回递增 RequestID。
// 下一帧 Tick 里 TriggerAsyncQueries 会 snapshot 并启一个后台 Graph 任务处理。
uint32 UNavigationSystemV1::FindPathAsync(const FNavAgentProperties& AgentProperties, FPathFindingQuery Query, const FNavPathQueryDelegate& ResultDelegate, EPathFindingMode::Type Mode)
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_RequestingAsyncPathfinding);

	if (Query.NavData.IsValid() == false)
	{
		Query.NavData = GetNavDataForProps(AgentProperties, Query.StartLocation);
	}

	if (Query.NavData.IsValid())
	{
		FAsyncPathFindingQuery AsyncQuery(Query, ResultDelegate, Mode);

		if (AsyncQuery.QueryID != INVALID_NAVQUERYID)
		{
			AddAsyncQuery(AsyncQuery);
		}

		return AsyncQuery.QueryID;
	}

	return INVALID_NAVQUERYID;
}

// 通过 RequestID 取消尚未派发的异步寻路请求；若该 ID 已经进入后台执行则无法取消。
// 必须在 GameThread 调用。
void UNavigationSystemV1::AbortAsyncFindPathRequest(uint32 AsynPathQueryID)
{
	check(IsInGameThread());
	FAsyncPathFindingQuery* Query = AsyncPathFindingQueries.GetData();
	for (int32 Index = 0; Index < AsyncPathFindingQueries.Num(); ++Index, ++Query)
	{
		if (Query->QueryID == AsynPathQueryID)
		{
			AsyncPathFindingQueries.RemoveAtSwap(Index);
			break;
		}
	}
}

FAutoConsoleTaskPriority CPrio_TriggerAsyncQueries(
	TEXT("TaskGraph.TaskPriorities.NavTriggerAsyncQueries"),
	TEXT("Task and thread priority for UNavigationSystemV1::PerformAsyncQueries."),
	ENamedThreads::BackgroundThreadPriority, // if we have background priority task threads, then use them...
	ENamedThreads::NormalTaskPriority, // .. at normal task priority
	ENamedThreads::NormalTaskPriority // if we don't have background threads, then use normal priority threads at normal task priority instead
	);


// 启动后台寻路 Graph 任务：拷贝 PathFindingQueries 给 PerformAsyncQueries（在 AnyHiPriThreadNormalTask 线程池跑）
void UNavigationSystemV1::TriggerAsyncQueries(TArray<FAsyncPathFindingQuery>& PathFindingQueries)
{
	DECLARE_CYCLE_STAT(TEXT("FSimpleDelegateGraphTask.NavigationSystem batched async queries"),
		STAT_FSimpleDelegateGraphTask_NavigationSystemBatchedAsyncQueries,
		STATGROUP_TaskGraphTasks);

	AsyncPathFindingTask = FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
		FSimpleDelegateGraphTask::FDelegate::CreateUObject(this, &UNavigationSystemV1::PerformAsyncQueries, PathFindingQueries),
		GET_STATID(STAT_FSimpleDelegateGraphTask_NavigationSystemBatchedAsyncQueries), nullptr, CPrio_TriggerAsyncQueries.Get());
}

// 当后台异步寻路任务正在跑时，置位中止标志并阻塞等待其完成（剩余 Query 顺延到下一帧）。
// 用于在 NavData 重建/关卡切换等场景下避免后台任务访问到失效数据。
void UNavigationSystemV1::PostponeAsyncQueries()
{
	if (AsyncPathFindingTask.GetReference() && !AsyncPathFindingTask->IsComplete())
	{
		bAbortAsyncQueriesRequested = true;
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(AsyncPathFindingTask, ENamedThreads::GameThread);
		bAbortAsyncQueriesRequested = false;
	}
}

// 在 GameThread 上派发已完成的异步寻路结果：逐条触发用户绑定的 OnDoneDelegate。
// 由 Tick 在合并完成队列后调用，保证回调在主线程执行。
void UNavigationSystemV1::DispatchAsyncQueriesResults(const TArray<FAsyncPathFindingQuery>& PathFindingQueries) const
{
	if (PathFindingQueries.Num() > 0)
	{
		SCOPE_CYCLE_COUNTER(STAT_Navigation_DispatchAsyncPathfindingResults);
		CSV_SCOPED_TIMING_STAT(NavigationSystem, AsyncNavQueryFinished);

		for (const FAsyncPathFindingQuery& Query : PathFindingQueries)
		{
			Query.OnDoneDelegate.ExecuteIfBound(Query.QueryID, Query.Result.Result, Query.Result.Path);
		}
	}
}

// 真正在后台线程执行寻路：对每条 Query 调 NavData->FindPath，结果回塞给 CompletedQueries。
// 可被 bAbortAsyncQueriesRequested 中途取消；剩余 Query 会顺延到下一帧。
// 线程：非 GameThread。
void UNavigationSystemV1::PerformAsyncQueries(TArray<FAsyncPathFindingQuery> PathFindingQueries)
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_PathfindingAsync);
	CSV_SCOPED_TIMING_STAT(NavigationSystem, PathfindingAsync);

	if (PathFindingQueries.Num() == 0)
	{
		return;
	}

	int32 NumProcessed = 0;
	for (FAsyncPathFindingQuery& Query : PathFindingQueries)
	{
		// perform query
		if (const TStrongObjectPtr<const ANavigationData> NavData = Query.NavData.Pin())
		{
			if (Query.Mode == EPathFindingMode::Hierarchical)
			{
				Query.Result = NavData->FindHierarchicalPath(Query.NavAgentProperties, Query);
			}
			else
			{
				Query.Result = NavData->FindPath(Query.NavAgentProperties, Query);
			}
		}
		else
		{
			Query.Result = ENavigationQueryResult::Error;
		}
		++NumProcessed;

		// Check for abort request from the main tread
		if (bAbortAsyncQueriesRequested)
		{
			break;
		}
	}

	const int32 NumQueries = PathFindingQueries.Num();
	const int32 NumPostponed = NumQueries - NumProcessed;

	// Queue remaining queries for next frame
	if (bAbortAsyncQueriesRequested)
	{
		AsyncPathFindingQueries.Append(PathFindingQueries.GetData() + NumProcessed, NumPostponed);
	}
	
	// Append to list of completed queries to dispatch results in main thread
	AsyncPathFindingCompletedQueries.Append(PathFindingQueries.GetData(), NumProcessed);

	UE_LOG(LogNavigation, Log, TEXT("Async pathfinding queries: %d completed, %d postponed to next frame"), NumProcessed, NumPostponed);
}

// 在 NavData 上随机取一个可达点（任一可达多边形随机点）。NavData 为 null 时回退到 MainNavData。
// 同步阻塞调用；常用于 AI 巡逻/初始位置选择。
bool UNavigationSystemV1::GetRandomPoint(FNavLocation& ResultLocation, ANavigationData* NavData, FSharedConstNavQueryFilter QueryFilter)
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_QueriesTimeSync);

	if (NavData == nullptr)
	{
		NavData = MainNavData;
	}

	if (NavData != nullptr)
	{
		ResultLocation = NavData->GetRandomPoint(QueryFilter);
		return true;
	}

	return false;
}

// 以 Origin 为中心、Radius 半径内随机选一可达点（要求从 Origin 真的能走到）。
// 比 GetRandomPointInNavigableRadius 更"准"但更贵——内部含一次寻路可达性检测。
bool UNavigationSystemV1::GetRandomReachablePointInRadius(const FVector& Origin, float Radius, FNavLocation& ResultLocation, ANavigationData* NavData, FSharedConstNavQueryFilter QueryFilter) const
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_QueriesTimeSync);

	if (NavData == nullptr)
	{
		NavData = MainNavData;
	}

	return NavData != nullptr && NavData->GetRandomReachablePointInRadius(Origin, Radius, ResultLocation, QueryFilter);
}

// 以 Origin 为中心、Radius 半径内随机选一可导航点（不要求与 Origin 连通）。
// 仅做几何相交筛选，比 GetRandomReachablePointInRadius 便宜。
bool UNavigationSystemV1::GetRandomPointInNavigableRadius(const FVector& Origin, float Radius, FNavLocation& ResultLocation, ANavigationData* NavData, FSharedConstNavQueryFilter QueryFilter) const
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_QueriesTimeSync);

	if (NavData == nullptr)
	{
		NavData = MainNavData;
	}

	return NavData != nullptr && NavData->GetRandomPointInNavigableRadius(Origin, Radius, ResultLocation, QueryFilter);
}

ENavigationQueryResult::Type UNavigationSystemV1::GetPathCost(const FVector& PathStart, const FVector& PathEnd, FVector::FReal& OutPathCost, const ANavigationData* NavData, FSharedConstNavQueryFilter QueryFilter) const
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_QueriesTimeSync);

	if (NavData == nullptr)
	{
		NavData = GetDefaultNavDataInstance();
	}

	return NavData != nullptr ? NavData->CalcPathCost(PathStart, PathEnd, OutPathCost, QueryFilter) : ENavigationQueryResult::Error;
}

ENavigationQueryResult::Type UNavigationSystemV1::GetPathLength(const FVector& PathStart, const FVector& PathEnd, FVector::FReal& OutPathLength, const ANavigationData* NavData, FSharedConstNavQueryFilter QueryFilter) const
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_QueriesTimeSync);

	if (NavData == nullptr)
	{
		NavData = GetDefaultNavDataInstance();
	}

	return NavData != nullptr ? NavData->CalcPathLength(PathStart, PathEnd, OutPathLength, QueryFilter) : ENavigationQueryResult::Error;
}

ENavigationQueryResult::Type UNavigationSystemV1::GetPathLengthAndCost(const FVector& PathStart, const FVector& PathEnd, FVector::FReal& OutPathLength, FVector::FReal& OutPathCost, const ANavigationData* NavData, FSharedConstNavQueryFilter QueryFilter) const
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_QueriesTimeSync);

	if (NavData == nullptr)
	{
		NavData = GetDefaultNavDataInstance();
	}

	return NavData != nullptr ? NavData->CalcPathLengthAndCost(PathStart, PathEnd, OutPathLength, OutPathCost, QueryFilter) : ENavigationQueryResult::Error;
}

// 把任意 Point 投影到导航网格上（双重作用：① 修正/吸附位置为可走点；② 顺带挑出对应 NavData）。
// Extent 为搜索盒；为零时使用 NavData 的 DefaultQueryExtent。命中失败返回 false。
bool UNavigationSystemV1::ProjectPointToNavigation(const FVector& Point, FNavLocation& OutLocation, const FVector& Extent, const ANavigationData* NavData, FSharedConstNavQueryFilter QueryFilter) const
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_QueriesTimeSync);

	if (NavData == nullptr)
	{
		NavData = GetDefaultNavDataInstance();
	}

	return NavData != nullptr && NavData->ProjectPoint(Point, OutLocation
											, UE::Navigation::Private::IsValidExtent(Extent) ? Extent : NavData->GetConfig().DefaultQueryExtent
											, QueryFilter);
}

UNavigationPath* UNavigationSystemV1::FindPathToActorSynchronously(UObject* WorldContextObject, const FVector& PathStart, AActor* GoalActor, float TetherDistance, AActor* PathfindingContext, TSubclassOf<UNavigationQueryFilter> FilterClass)
{
	if (GoalActor == nullptr)
	{
		return nullptr; 
	}

	INavAgentInterface* NavAgent = Cast<INavAgentInterface>(GoalActor);
	UNavigationPath* GeneratedPath = FindPathToLocationSynchronously(WorldContextObject, PathStart, NavAgent ? NavAgent->GetNavAgentLocation() : GoalActor->GetActorLocation(), PathfindingContext, FilterClass);
	if (GeneratedPath != nullptr && GeneratedPath->GetPath().IsValid() == true)
	{
		GeneratedPath->GetPath()->SetGoalActorObservation(*GoalActor, TetherDistance);
	}

	return GeneratedPath;
}

UNavigationPath* UNavigationSystemV1::FindPathToLocationSynchronously(UObject* WorldContextObject, const FVector& PathStart, const FVector& PathEnd, AActor* PathfindingContext, TSubclassOf<UNavigationQueryFilter> FilterClass)
{
	UWorld* World = nullptr;

	if (WorldContextObject != nullptr)
	{
		World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	}
	if (World == nullptr && PathfindingContext != nullptr)
	{
		World = GEngine->GetWorldFromContextObject(PathfindingContext, EGetWorldErrorMode::LogAndReturnNull);
	}

	UNavigationPath* ResultPath = nullptr;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	if (NavSys != nullptr && NavSys->GetDefaultNavDataInstance() != nullptr)
	{
		ResultPath = NewObject<UNavigationPath>(NavSys);
		bool bValidPathContext = false;
		const ANavigationData* NavigationData = nullptr;

		if (PathfindingContext != nullptr)
		{
			INavAgentInterface* NavAgent = Cast<INavAgentInterface>(PathfindingContext);
			
			if (NavAgent != nullptr)
			{
				const FNavAgentProperties& AgentProps = NavAgent->GetNavAgentPropertiesRef();
				NavigationData = NavSys->GetNavDataForProps(AgentProps, PathStart);
				bValidPathContext = true;
			}
			else if (Cast<ANavigationData>(PathfindingContext))
			{
				NavigationData = (ANavigationData*)PathfindingContext;
				bValidPathContext = true;
			}
		}
		if (bValidPathContext == false)
		{
			// just use default
			NavigationData = NavSys->GetDefaultNavDataInstance();
		}

		check(NavigationData);

		const FPathFindingQuery Query(PathfindingContext, *NavigationData, PathStart, PathEnd, UNavigationQueryFilter::GetQueryFilter(*NavigationData, PathfindingContext, FilterClass));
		const FPathFindingResult Result = NavSys->FindPathSync(Query, EPathFindingMode::Regular);
		if (Result.IsSuccessful())
		{
			ResultPath->SetPath(Result.Path);
		}
	}

	return ResultPath;
}

// 沿导航网格做"射线"测试（沿表面行走是否被阻挡）：被阻挡返回 true，HitLocation 写入碰撞点。
// 转发到带 AdditionalResults 的重载。
bool UNavigationSystemV1::NavigationRaycast(UObject* WorldContextObject, const FVector& RayStart, const FVector& RayEnd, FVector& HitLocation, TSubclassOf<UNavigationQueryFilter> FilterClass, AController* Querier)
{
	return NavigationRaycastWithAdditionalResults(WorldContextObject, RayStart, RayEnd, HitLocation, nullptr, FilterClass, Querier);
}

// 导航射线检测的全功能版：根据 Querier 选 NavData → 调用 NavData->Raycast。
// AdditionalResults 可拿到"终点是否落在走廊内"等附加信息，用于平滑路径/可见性判断。
bool UNavigationSystemV1::NavigationRaycastWithAdditionalResults(UObject* WorldContextObject, const FVector& RayStart, const FVector& RayEnd, FVector& HitLocation, FNavigationRaycastAdditionalResults* AdditionalResults, TSubclassOf<UNavigationQueryFilter> FilterClass, AController* Querier)
{
	UWorld* World = nullptr;

	if (WorldContextObject != nullptr)
	{
		World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	}
	if (World == nullptr && Querier != nullptr)
	{
		World = GEngine->GetWorldFromContextObject(Querier, EGetWorldErrorMode::LogAndReturnNull);
	}

	// blocked, i.e. not traversable, by default
	bool bRaycastBlocked = true;
	HitLocation = RayStart;
	if (AdditionalResults)
	{
		AdditionalResults->bIsRayEndInCorridor = false;
	}

	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	if (NavSys)
	{
		// figure out which navigation data to use
		const ANavigationData* NavData = nullptr;
		INavAgentInterface* MyNavAgent = Cast<INavAgentInterface>(Querier);
		if (MyNavAgent)
		{
			const FNavAgentProperties& AgentProps = MyNavAgent->GetNavAgentPropertiesRef();
			NavData = NavSys->GetNavDataForProps(AgentProps, RayStart);
		}
		if (NavData == nullptr)
		{
			NavData = NavSys->GetDefaultNavDataInstance();
		}

		if (NavData != nullptr)
		{
			bRaycastBlocked = NavData->Raycast(RayStart, RayEnd, HitLocation, AdditionalResults, UNavigationQueryFilter::GetQueryFilter(*NavData, Querier, FilterClass));
		}
	}

	return bRaycastBlocked;
}

void UNavigationSystemV1::GetNavAgentPropertiesArray(TArray<FNavAgentProperties>& OutNavAgentProperties) const
{
	AgentToNavDataMap.GetKeys(OutNavAgentProperties);
}

// 按 Agent 匹配 NavData（const 版，可选位置 + Extent）。
// 基本策略：
//   1) 先查 AgentToNavDataMap 缓存
//   2) 遍历 NavDataSet，比对 Agent 半径/高度等（受 bSkipAgentHeightCheckWhenPickingNavData 影响）
//   3) 若配置了 Preferred NavData Class 优先匹配
ANavigationData* UNavigationSystemV1::GetNavDataForProps(const FNavAgentProperties& AgentProperties, const FVector& AgentLocation, const FVector& Extent) const
{
	return const_cast<ANavigationData*>(GetNavDataForProps(AgentProperties));
}

ANavigationData* UNavigationSystemV1::GetNavDataForProps(const FNavAgentProperties& AgentProperties)
{
	return const_cast<ANavigationData*>(AsConst(*this).GetNavDataForProps(AgentProperties));
}

// @todo could optimize this by having "SupportedAgentIndex" in FNavAgentProperties
const ANavigationData* UNavigationSystemV1::GetNavDataForProps(const FNavAgentProperties& AgentProperties) const
{
	if (SupportedAgents.Num() <= 1)
	{
		return MainNavData;
	}

	// Because an invalid AgentProperties uses -1 values the code below is able to match the PreferredNavData.
	UE_CLOG(!(AgentProperties.IsValid() || AgentProperties.PreferredNavData.IsValid()), LogNavigation, Warning, TEXT("Looking for NavData using invalid FNavAgentProperties."));
	
	const TWeakObjectPtr<ANavigationData>* NavDataForAgent = AgentToNavDataMap.Find(AgentProperties);
	const ANavigationData* NavDataInstance = NavDataForAgent ? NavDataForAgent->Get() : nullptr;

	if (NavDataInstance == nullptr)
	{
		TArray<FNavAgentProperties> AgentPropertiesList;
		AgentToNavDataMap.GenerateKeyArray(AgentPropertiesList);
		
		FNavAgentProperties BestFitNavAgent;
		float BestExcessHeight = -FLT_MAX;
		float BestExcessRadius = -FLT_MAX;
		float ExcessRadius = -FLT_MAX;
		float ExcessHeight = -FLT_MAX;
		const float AgentHeight = bSkipAgentHeightCheckWhenPickingNavData ? 0.f : AgentProperties.AgentHeight;

		for (TArray<FNavAgentProperties>::TConstIterator It(AgentPropertiesList); It; ++It)
		{
			const FNavAgentProperties& NavIt = *It;
			const bool bNavClassMatch = NavIt.IsNavDataMatching(AgentProperties);
			if (!bNavClassMatch)
			{
				continue;
			}

			ExcessRadius = NavIt.AgentRadius - AgentProperties.AgentRadius;
			ExcessHeight = bSkipAgentHeightCheckWhenPickingNavData ? 0.f : (NavIt.AgentHeight - AgentHeight);

			const bool bExcessRadiusIsBetter = ((ExcessRadius == 0) && (BestExcessRadius != 0)) 
				|| ((ExcessRadius > 0) && (BestExcessRadius < 0))
				|| ((ExcessRadius > 0) && (BestExcessRadius > 0) && (ExcessRadius < BestExcessRadius))
				|| ((ExcessRadius < 0) && (BestExcessRadius < 0) && (ExcessRadius > BestExcessRadius));
			const bool bExcessHeightIsBetter = ((ExcessHeight == 0) && (BestExcessHeight != 0))
				|| ((ExcessHeight > 0) && (BestExcessHeight < 0))
				|| ((ExcessHeight > 0) && (BestExcessHeight > 0) && (ExcessHeight < BestExcessHeight))
				|| ((ExcessHeight < 0) && (BestExcessHeight < 0) && (ExcessHeight > BestExcessHeight));
			const bool bBestIsValid = (BestExcessRadius >= 0) && (BestExcessHeight >= 0);
			const bool bRadiusEquals = (ExcessRadius == BestExcessRadius);
			const bool bHeightEquals = (ExcessHeight == BestExcessHeight);

			bool bValuesAreBest = ((bExcessRadiusIsBetter || bRadiusEquals) && (bExcessHeightIsBetter || bHeightEquals));
			if (!bValuesAreBest && !bBestIsValid)
			{
				bValuesAreBest = bExcessRadiusIsBetter || (bRadiusEquals && bExcessHeightIsBetter);
			}

			if (bValuesAreBest)
			{
				BestFitNavAgent = NavIt;
				BestExcessHeight = ExcessHeight;
				BestExcessRadius = ExcessRadius;
			}
		}

		if (BestFitNavAgent.IsValid())
		{
			NavDataForAgent = AgentToNavDataMap.Find(BestFitNavAgent);
			NavDataInstance = NavDataForAgent ? NavDataForAgent->Get() : nullptr;
		}
	}

	return NavDataInstance ? NavDataInstance : MainNavData;
}

ANavigationData* UNavigationSystemV1::GetNavDataForAgentName(const FName AgentName) const
{
	ANavigationData* Result = nullptr;

	for (ANavigationData* NavData : NavDataSet)
	{
		if (IsValid(NavData) && NavData->GetConfig().Name == AgentName)
		{
			Result = NavData;
			break;
		}
	}

	return Result;
}

// 取整个世界中可导航区域的总包围盒。bWholeWorldNavigable 时遍历所有 nav-relevant Actor，
// 否则只汇总 RegisteredNavBounds（NavMeshBoundsVolume / 注册的导航边界）。
FBox UNavigationSystemV1::GetNavigableWorldBounds() const
{
	return GetWorldBounds();
}

void UNavigationSystemV1::SetBuildBounds(const FBox& Bounds)
{
	BuildBounds = Bounds;
}

bool UNavigationSystemV1::ContainsNavData(const FBox& Bounds) const
{
	for (const ANavigationData* NavData : NavDataSet)
	{
		if (NavData && Bounds.Intersect(NavData->GetBounds()))
		{
			return true;
		}
	}
	return false;
}

FBox UNavigationSystemV1::ComputeNavDataBounds() const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UNavigationSystemV1::ComputeNavDataBounds);
	
	FBox Bounds(ForceInit);
	for (const ANavigationData* NavData : NavDataSet)
	{
		if (NavData)
		{
			Bounds += NavData->GetBounds();
		}
	}
	return Bounds;
}

// World Partition 流式：把一个 NavigationDataChunkActor 携带的瓦片广播到所有 NavData，
// 由 NavData 自行 attach/恢复对应的瓦片数据。
void UNavigationSystemV1::AddNavigationDataChunk(ANavigationDataChunkActor& DataChunkActor)
{
	for (ANavigationData* NavData : NavDataSet)
	{
		if (NavData)
		{
			NavData->OnStreamingNavDataAdded(DataChunkActor);
		}
	}
}

// 与 AddNavigationDataChunk 配对：通知所有 NavData 卸载该 ChunkActor 携带的瓦片数据。
void UNavigationSystemV1::RemoveNavigationDataChunk(ANavigationDataChunkActor& DataChunkActor)
{
	for (ANavigationData* NavData : NavDataSet)
	{
		if (NavData)
		{
			NavData->OnStreamingNavDataRemoved(DataChunkActor);
		}
	}
}

// World Partition 烘焙阶段：把 QueryBounds 范围内的 NavData 瓦片打包写入 DataChunkActor，
// 并返回实际包含的瓦片包围盒（OutTilesBounds），用于序列化到 chunk。
void UNavigationSystemV1::FillNavigationDataChunkActor(const FBox& QueryBounds, ANavigationDataChunkActor& DataChunkActor, FBox& OutTilesBounds)
{
	for (const ANavigationData* NavData : NavDataSet)
	{
		if (NavData)
		{
			NavData->FillNavigationDataChunkActor(QueryBounds, DataChunkActor, OutTilesBounds);
		}
	}
}

// 返回 MainNavData（缺省路径）。若没有且允许创建，则 Spawn 一份 AbstractNavData 兜底。
// NavAgentProperties 默认寻路走这条路径。
ANavigationData* UNavigationSystemV1::GetDefaultNavDataInstance(FNavigationSystem::ECreateIfMissing CreateNewIfNoneFound)
{
	checkSlow(IsInGameThread() == true);

	if (!IsValid(MainNavData))
	{
		MainNavData = nullptr;

		// @TODO this should be done a differently. There should be specified a "default agent"
		for (int32 NavDataIndex = 0; NavDataIndex < NavDataSet.Num(); ++NavDataIndex)
		{
			ANavigationData* NavData = NavDataSet[NavDataIndex];
			if (IsValid(NavData) && NavData->CanBeMainNavData()
				&& (DefaultAgentName == NAME_None || NavData->GetConfig().Name == DefaultAgentName))
			{
				MainNavData = NavData;
				break;
			}
		}

#if WITH_RECAST
		if (/*GIsEditor && */(MainNavData == nullptr) && CreateNewIfNoneFound == FNavigationSystem::Create)
		{
			// Spawn a new one if we're in the editor.  In-game, either we loaded one or we don't get one.
			MainNavData = GetWorld()->SpawnActor<ANavigationData>(ARecastNavMesh::StaticClass());
		}
#endif // WITH_RECAST
		// either way make sure it's registered. Registration stores unique
		// navmeshes, so we have nothing to lose
		if (MainNavData != nullptr)
		{
			const ERegistrationResult Result = RegisterNavData(MainNavData);
			LogNavDataRegistrationResult(Result);
		}
	}

	return MainNavData;
}

// 拷贝 MainNavData 的默认查询过滤器：用户拿到副本后可改 AreaCost 等而不影响原配置。
FSharedNavQueryFilter UNavigationSystemV1::CreateDefaultQueryFilterCopy() const 
{ 
	return MainNavData ? MainNavData->GetDefaultQueryFilter()->GetCopy() : nullptr; 
}

bool UNavigationSystemV1::IsNavigationBuilt(const AWorldSettings* Settings) const
{
	if (Settings == nullptr || Settings->IsNavigationSystemEnabled() == false || IsThereAnywhereToBuildNavigation() == false)
	{
		return true;
	}

	bool bIsBuilt = true;

	for (int32 NavDataIndex = 0; NavDataIndex < NavDataSet.Num(); ++NavDataIndex)
	{
		ANavigationData* NavData = NavDataSet[NavDataIndex];
		if (NavData != nullptr && NavData->GetWorldSettings() == Settings)
		{
			FNavDataGenerator* Generator = NavData->GetGenerator();
			if ((NavData->GetRuntimeGenerationMode() != ERuntimeGenerationType::Static
#if WITH_EDITOR
				|| GEditor != nullptr
#endif // WITH_EDITOR
				) && (Generator == nullptr || Generator->IsBuildInProgressCheckDirty() == true))
			{
				bIsBuilt = false;
				break;
			}
		}
	}

	return bIsBuilt;
}

bool UNavigationSystemV1::IsThereAnywhereToBuildNavigation() const
{
	// not check if there are any volumes or other structures requiring/supporting navigation building
	if (bWholeWorldNavigable == true)
	{
		return true;
	}

	for (const FNavigationBounds& Bounds : RegisteredNavBounds)
	{
		if (Bounds.AreaBox.IsValid)
		{
			return true;
		}
	}

	// @TODO this should be made more flexible to be able to trigger this from game-specific 
	// code (like Navigation System's subclass maybe)
	bool bCreateNavigation = false;

	for (TActorIterator<ANavMeshBoundsVolume> It(GetWorld()); It; ++It)
	{
		ANavMeshBoundsVolume const* const V = (*It);
		if (IsValid(V))
		{
			bCreateNavigation = true;
			break;
		}
	}

	return bCreateNavigation;
}

// 判断 Actor 是否对导航有"贡献"：Actor 自身或其任一组件实现 INavRelevantInterface 且为 relevant。
// 用于决定是否将该 Actor 加入 NavOctree、是否参与导航重算。
bool UNavigationSystemV1::IsNavigationRelevant(const AActor* TestActor) const
{
	const INavRelevantInterface* NavInterface = Cast<const INavRelevantInterface>(TestActor);
	if (NavInterface && NavInterface->IsNavigationRelevant())
	{
		return true;
	}

	if (TestActor)
	{
		TInlineComponentArray<UActorComponent*> Components;
		TestActor->GetComponents(Components);
		for (int32 Idx = 0; Idx < Components.Num(); Idx++)
		{
			NavInterface = Cast<const INavRelevantInterface>(Components[Idx]);
			if (NavInterface && NavInterface->IsNavigationRelevant())
			{
				return true;
			}
		}
	}

	return false;
}

FBox UNavigationSystemV1::GetWorldBounds() const
{
	checkSlow(IsInGameThread() == true);

	NavigableWorldBounds = FBox(ForceInit);

	if (GetWorld() != nullptr)
	{
		if (bWholeWorldNavigable == false)
		{
			for (const FNavigationBounds& Bounds : RegisteredNavBounds)
			{
				NavigableWorldBounds += Bounds.AreaBox;
			}
		}
		else
		{
			// @TODO - super slow! Need to ask where I can get this from
			for (FActorIterator It(GetWorld()); It; ++It)
			{
				if (IsNavigationRelevant(*It))
				{
					NavigableWorldBounds += (*It)->GetComponentsBoundingBox();
				}
			}
		}
	}

	return NavigableWorldBounds;
}

FBox UNavigationSystemV1::GetLevelBounds(ULevel* InLevel) const
{
	FBox NavigableLevelBounds(ForceInit);

	if (InLevel)
	{
		auto Actor = InLevel->Actors.CreateConstIterator();
		const int32 ActorCount = InLevel->Actors.Num();
		for (int32 ActorIndex = 0; ActorIndex < ActorCount; ++ActorIndex, ++Actor)
		{
			if (IsNavigationRelevant(*Actor))
			{
				NavigableLevelBounds += (*Actor)->GetComponentsBoundingBox();
			}
		}
	}

	return NavigableLevelBounds;
}

const TSet<FNavigationBounds>& UNavigationSystemV1::GetNavigationBounds() const
{
	return RegisteredNavBounds;
}

// World Origin Rebasing：当世界原点平移时同步移动注册的导航边界，并按需重建 NavMesh。
// 动态生成时整体重建；静态时通知所有 NavData->ApplyWorldOffset 做就地偏移。
void UNavigationSystemV1::ApplyWorldOffset(const FVector& InOffset, bool bWorldShift)
{
	// Move the navmesh bounds by the offset
	for (FNavigationBounds& Bounds : RegisteredNavBounds)
	{
		Bounds.AreaBox = Bounds.AreaBox.ShiftBy(InOffset);
	}

	// Attempt at generation of new nav mesh after the shift
	// dynamic navmesh, we regenerate completely
	if (GetRuntimeGenerationType() == ERuntimeGenerationType::Dynamic)
	{
		//stop generators from building navmesh
		CancelBuild();

		ConditionalPopulateNavOctree();
		Build();

		for (ANavigationData* NavData : NavDataSet)
		{
			if (NavData)
			{
				NavData->ConditionalConstructGenerator();
#if WITH_RECAST
				ARecastNavMesh* RecastNavMesh = Cast<ARecastNavMesh>(NavData);
				if (RecastNavMesh)
				{
					RecastNavMesh->RequestDrawingUpdate();
				}
#endif // WITH_RECAST
			}
		}
	}
	else // static navmesh
	{
		//not sure what happens when we shift farther than the extents of the NavOctree are
		for (ANavigationData* NavData : NavDataSet)
		{
			if (NavData)
			{
				NavData->ApplyWorldOffset(InOffset, bWorldShift);
			}
		}
	}
}

//----------------------------------------------------------------------//
// Bookkeeping 
//----------------------------------------------------------------------//
// 推入待注册队列：典型调用者是 ANavigationData::PostRegisterAllComponents（Actor 刚从关卡加载）。
// 真正注册发生在下一次 ProcessRegistrationCandidates（Tick 里）。
void UNavigationSystemV1::RequestRegistrationDeferred(ANavigationData& NavData)
{
	FScopeLock RegistrationLock(&NavDataRegistrationSection);

	if (NavDataRegistrationQueue.Num() < REGISTRATION_QUEUE_SIZE)
	{
		NavDataRegistrationQueue.AddUnique(&NavData);
	}
	else
	{
		UE_LOG(LogNavigation, Warning, TEXT("Navigation System: registration queue full! System:%s NavData:%s"), *GetPathNameSafe(this), *GetPathNameSafe(&NavData));
	}
}

// 处理注册候选队列：逐个调 RegisterNavData；若成功会从队列移除。
// 失败 (Agent 重复/不支持等) 会打 Log 并可能销毁 Actor。Tick 中驱动。
void UNavigationSystemV1::ProcessRegistrationCandidates()
{
	FScopeLock RegistrationLock(&NavDataRegistrationSection);

	if (NavDataRegistrationQueue.Num() == 0)
	{
		return;
	}
	
	const int CandidatesCount = NavDataRegistrationQueue.Num();
	int32 NumNavDataProcessed = 0;
	for (int32 CandidateIndex = CandidatesCount - 1; CandidateIndex >= 0; --CandidateIndex)
	{
		ANavigationData* NavDataPtr = NavDataRegistrationQueue[CandidateIndex];
		ULevel* OwningLevel = NavDataPtr != nullptr ? NavDataPtr->GetLevel() : nullptr;
		if (OwningLevel && OwningLevel->bIsVisible)
		{
			const ERegistrationResult Result = RegisterNavData(NavDataPtr);
			LogNavDataRegistrationResult(Result);

			if (Result != RegistrationSuccessful && Result != RegistrationFailed_DataPendingKill)
			{
				NavDataPtr->Destroy();
				if (NavDataPtr == MainNavData)
				{
					MainNavData = nullptr;
				}
			}

			NumNavDataProcessed++;
			NavDataRegistrationQueue.RemoveAtSwap(CandidateIndex);
		}
	}	
	
	if (NumNavDataProcessed)
	{
		MainNavData = GetDefaultNavDataInstance(FNavigationSystem::DontCreate);

		// See if any of registered navigation data now needs NavOctree
		if (DefaultOctreeController.IsValid() == false && RequiresNavOctree() == true)
		{
			ConditionalPopulateNavOctree();
		}
	}
}

// 把延迟到 NavigationObjectRepository 上的 CustomLink 一次性 flush 到本 NavSystem。
// 在 NavSystem 初始化完毕后调用，以处理"Repository 早于 NavSystem 收集"的链接。
void UNavigationSystemV1::ProcessCustomLinkPendingRegistration()
{
	if (Repository == nullptr)
	{
		return;
	}

	for (TWeakInterfacePtr<INavLinkCustomInterface> It : Repository->GetCustomLinks())
	{
		if (INavLinkCustomInterface* Interface = It.Get())
		{
			RegisterCustomLink(*Interface);
		}
	}
}

// NavData 注册核心逻辑。步骤：
//   1) 防呆：PendingKill / Agent 不匹配 SupportedAgents → 返回相应失败码
//   2) 查是否已有同 Agent 的注册实例 → Agent 重复时按 bForceRebuildOnLoad 规则决定替换
//   3) 分配 DataIDs，设置 NavDataConfig，把 NavData 放进 NavDataSet[SupportedAgentIndex]
//   4) 更新 MainNavData（若当前 DefaultAgentName 匹配）
//   5) 广播 OnNavDataRegisteredEvent
UNavigationSystemV1::ERegistrationResult UNavigationSystemV1::RegisterNavData(ANavigationData* NavData)
{
	UE_LOG(LogNavigation, Verbose, TEXT("%hs %s"), __FUNCTION__, *GetFullNameSafe(NavData));
	
	if (NavData == nullptr)
	{
		return RegistrationError;
	}
	else if (!IsValid(NavData))
	{
		return RegistrationFailed_DataPendingKill;
	}
	// still to be seen if this is really true, but feels right
	else if (NavData->IsRegistered() == true)
	{
		return RegistrationSuccessful;
	}

	FScopeLock Lock(&NavDataRegistration);

	UNavigationSystemV1::ERegistrationResult Result = RegistrationError;

	// find out which, if any, navigation agents are supported by this nav data
	// if none then fail the registration
	FNavDataConfig NavConfig = NavData->GetConfig();

	// not discarding navmesh when there's only one Supported Agent
	if (NavConfig.IsValid() == false && SupportedAgents.Num() == 1)
	{
		// fill in AgentProps with whatever is the instance's setup
		NavConfig = SupportedAgents[0];
		NavData->SetConfig(SupportedAgents[0]);
		NavData->SetSupportsDefaultAgent(true);	
		NavData->ProcessNavAreas(ObjectPtrDecay(NavAreaClasses), 0);
	}

	if (NavConfig.IsValid() == true)
	{
		if (NavData->IsA(AAbstractNavData::StaticClass()))
		{
			if (AbstractNavData == nullptr || AbstractNavData == NavData)
			{
				// fake registration since it's a special navigation data type 
				// and it would get discarded for not implementing any particular
				// navigation agent
				// Node that we don't add abstract navigation data to NavDataSet
				NavData->OnRegistered();

				Result = RegistrationSuccessful;
			}
			else
			{
				// otherwise specified agent type already has its navmesh implemented, fail redundant instance
				Result = RegistrationFailed_AgentAlreadySupported;
			}
		}
		else
		{
			// check if this kind of agent has already its navigation implemented
			TWeakObjectPtr<ANavigationData>* NavDataForAgent = AgentToNavDataMap.Find(NavConfig);
			ANavigationData* NavDataInstanceForAgent = NavDataForAgent ? NavDataForAgent->Get() : nullptr;

			if (NavDataInstanceForAgent == nullptr)
			{
				// ok, so this navigation agent doesn't have its navmesh registered yet, but do we want to support it?
				bool bAgentSupported = false;

				for (int32 AgentIndex = 0; AgentIndex < SupportedAgents.Num(); ++AgentIndex)
				{
					if (NavData->GetClass() == SupportedAgents[AgentIndex].GetNavDataClass<ANavigationData>()
						&& SupportedAgents[AgentIndex].IsEquivalent(NavConfig) == true)
					{
						// it's supported, then just in case it's not a precise match (IsEquivalent succeeds with some precision) 
						// update NavData with supported Agent
						bAgentSupported = true;

						NavData->SetConfig(SupportedAgents[AgentIndex]);
						AgentToNavDataMap.Add(SupportedAgents[AgentIndex], NavData);
						NavData->SetSupportsDefaultAgent(SupportedAgents[AgentIndex].Name == DefaultAgentName);
						NavData->ProcessNavAreas(ObjectPtrDecay(NavAreaClasses), AgentIndex);

						OnNavDataRegisteredEvent.Broadcast(NavData);

						NavDataSet.AddUnique(NavData);
						NavData->OnRegistered();

						break;
					}
				}
				Result = bAgentSupported == true ? RegistrationSuccessful : RegistrationFailed_AgentNotValid;
			}
			else if (NavDataInstanceForAgent == NavData)
			{
				ensure(NavDataSet.Find(NavData) != INDEX_NONE);
				// let's treat double registration of the same nav data with the same agent as a success
				Result = RegistrationSuccessful;
			}
			else
			{
				// otherwise specified agent type already has its navmesh implemented, fail redundant instance
				Result = RegistrationFailed_AgentAlreadySupported;
			}
		}
	}
	else
	{
		Result = RegistrationFailed_AgentNotValid;
	}

	NavRegenTimeSliceManager.ResetTileWaitTimeArrays(NavDataSet);

#if !UE_BUILD_SHIPPING
	NavRegenTimeSliceManager.ResetTileHistoryData(NavDataSet);
#endif // UE_BUILD_SHIPPING

	// @todo else might consider modifying this NavData to implement navigation for one of the supported agents
	// care needs to be taken to not make it implement navigation for agent who's real implementation has 
	// not been loaded yet.

	if (Result == RegistrationSuccessful && CrowdManager != nullptr)
	{
		CrowdManager->OnNavDataRegistered(*NavData);
	}

	return Result;
}

// 反注册 NavData：从 NavDataSet/AgentToNavDataMap/RegistrationQueue 移除，调用 OnUnregistered，
// 并通知 CrowdManager。重置 NavRegenTimeSliceManager 的瓦片等待数组。
void UNavigationSystemV1::UnregisterNavData(ANavigationData* NavData)
{
	UE_LOG(LogNavigation, Verbose, TEXT("%hs %s"), __FUNCTION__, *GetFullNameSafe(NavData));
	
	NavDataSet.RemoveSingle(NavData);

	if (NavData == nullptr)
	{
		return;
	}

	AgentToNavDataMap.Remove(NavData->GetNavAgentProperties());

	FScopeLock Lock(&NavDataRegistration);
	NavDataRegistrationQueue.Remove(NavData);
	NavData->OnUnregistered();

	NavRegenTimeSliceManager.ResetTileWaitTimeArrays(NavDataSet);

#if !UE_BUILD_SHIPPING
	NavRegenTimeSliceManager.ResetTileHistoryData(NavDataSet);
#endif // UE_BUILD_SHIPPING
	
	if (CrowdManager != nullptr)
	{
		CrowdManager->OnNavDataUnregistered(*NavData);
	}
}

// SmartLink / 其它 Custom Link 注册。
// 步骤：
//   1) 生成/校验 FNavLinkId（若已存在冲突则 UpdateLinkId 重分配）
//   2) 写入 CustomNavLinksMap
//   3) 通知所有 NavData —— ARecastNavMesh 会登记 OffMeshConnection 触发 Tile 重建
void UNavigationSystemV1::RegisterCustomLink(INavLinkCustomInterface& CustomLink)
{
	ensureMsgf(CustomLink.GetLinkOwner() == nullptr || GetWorld() == CustomLink.GetLinkOwner()->GetWorld(),
		TEXT("Registering a link from a world different than the navigation system world should not happen."));

	const FNavLinkId OldId = CustomLink.GetId();
	FNavLinkId NewId = OldId;
	bool bGenerateNewId = false;

	// Test for Id clash
	if (CustomNavLinksMap.Contains(OldId))
	{
		if (OldId.IsLegacyId() == false)
		{
			UWorld* World = GetWorld();
			check(World);

			// During PIE or game we just generate a new Id, this is most likely to be from a runtime (non editor placed) prefab like a level instance but could be from 
			// a legitimate but extremely unlikely Id clash after loading.
			// If this occurs in EWorldType::Editor world it's a legitimate ID clash, currently we do not handle this edge case here as it should be incredibly unlikely to occur
			// and we do not save changes when cooking or building paths running a commandlet etc.
			bGenerateNewId = World->WorldType == EWorldType::PIE || World->WorldType == EWorldType::Game;
			if (ensureMsgf(bGenerateNewId, TEXT("Id clash in non Game and non PIE world. This should be incredibly rare!")))
			{
				// Pass in NewGuid() here as EWorldType::Game does not have access to the ActorInstanceGuid in any case and any random Unique Guid is acceptable here 
				// if we are not in EWorldType::Editor. Editor is different as we need the cook to be deterministic but for level instances individual actors are not 
				// serialized (but they are when cooked).
				NewId = FNavLinkId::GenerateUniqueId(CustomLink.GetAuxiliaryId(), FGuid::NewGuid());
			}
			
			// This should be very unlikely to occur, if its causing issues we should add code to handle this being careful to account for the editor world being run as a commandlet to cook and build paths on seperate runs.
			UE_CLOG(!bGenerateNewId, LogNavLink, Warning, TEXT("%hs navlink ID %llu is clashing with existing ID (Owner: %s). "
				"This will not be regenerated automatically in editor although for dynamic navmesh this will be handled at run time in game. "
				"For static mesh in the editor world the INavLinkCustomInterface implementor should regenerate the ID, "
				"deleting the owning actor and or component and placing again should fix this."), __FUNCTION__, CustomLink.GetId().GetId(), *GetFullNameSafe(CustomLink.GetLinkOwner()));
		}
		else
		{
			bGenerateNewId = true;
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			NewId = FNavLinkId(INavLinkCustomInterface::GetUniqueId());
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}

		// If the Id has changed mark the area dirty, this will fix the clash in the editor world and also in game for dynamic Navmesh, but not in game for static Navmesh.
		if (NewId != OldId)
		{
			CustomLink.UpdateLinkId(NewId);
			UE_LOG(LogNavLink, VeryVerbose, TEXT("%hs new navlink ID %llu."), __FUNCTION__, CustomLink.GetId().GetId());

			const FBox LinkBounds = ComputeCustomLinkBounds(CustomLink);
			if (LinkBounds.IsValid)
			{
				AddDirtyArea(LinkBounds, ENavigationDirtyFlag::DynamicModifier);
			}
		}
	}

	ensureMsgf(CustomLink.GetId().IsValid(), TEXT("%hs, registering a CustomLink with an invalid id."), __FUNCTION__);
	
	UE_CLOG(bGenerateNewId && CustomNavLinksMap.Contains(CustomLink.GetId()), LogNavLink, Warning, TEXT("%hs New navlink ID %llu is clashing with existing ID (Owner: %s)."),
		__FUNCTION__, CustomLink.GetId().GetId(), *GetFullNameSafe(CustomLink.GetLinkOwner()));
	CustomNavLinksMap.Add(CustomLink.GetId(), FNavigationSystem::FCustomLinkOwnerInfo(&CustomLink));
}

// CustomLink 反注册：仅从 CustomNavLinksMap 移除条目。Tile 重建由调用方按需追加 dirty area。
void UNavigationSystemV1::UnregisterCustomLink(INavLinkCustomInterface& CustomLink)
{
	CustomNavLinksMap.Remove(CustomLink.GetId());
}

INavLinkCustomInterface* UNavigationSystemV1::GetCustomLink(FNavLinkId UniqueLinkId) const
{
	const FNavigationSystem::FCustomLinkOwnerInfo* LinkInfo = CustomNavLinksMap.Find(UniqueLinkId);
	return (LinkInfo && LinkInfo->IsValid()) ? LinkInfo->LinkInterface : nullptr;
}

// 通知所有 NavData 更新链接状态（例如 SmartLink 的 Area Class 切换）。
// ARecastNavMesh 实现里只改对应 OffMeshConnection 的 areaId / flags，不重建 Tile —— 因此切 Enable/Disable 很便宜。
void UNavigationSystemV1::UpdateCustomLink(const INavLinkCustomInterface* CustomLink)
{
	for (TMap<FNavAgentProperties, TWeakObjectPtr<ANavigationData> >::TIterator It(AgentToNavDataMap); It; ++It)
	{
		ANavigationData* NavData = It.Value().Get();
		if (NavData)
		{
			NavData->UpdateCustomLink(CustomLink);
		}
	}
}

// 把 CustomLink 注册请求转交给 NavigationObjectRepository（World 级单例）。
// Repository 在 NavSystem 真正可用时再调用 RegisterCustomLink，解耦初始化顺序。
void UNavigationSystemV1::RequestCustomLinkRegistering(INavLinkCustomInterface& CustomLink, UObject* Owner)
{
	SCOPE_CYCLE_COUNTER(STAT_NavOctreeBookkeeping);

	if (Owner != nullptr)
	{
		if (UNavigationObjectRepository* Repository = UWorld::GetSubsystem<UNavigationObjectRepository>(Owner->GetWorld()))
		{
			UE_LOG(LogNavLink, Log, TEXT("%hs 0x%p"), __FUNCTION__, &CustomLink);
			Repository->RegisterCustomNavLinkObject(CustomLink);
		}
	}
}

// 反注册请求：同样走 NavigationObjectRepository，避免 Owner 销毁顺序与 NavSystem 不一致带来的悬挂注册。
void UNavigationSystemV1::RequestCustomLinkUnregistering(INavLinkCustomInterface& CustomLink, UObject* Owner)
{
	SCOPE_CYCLE_COUNTER(STAT_NavOctreeBookkeeping);

	if (Owner != nullptr)
	{
		if (UNavigationObjectRepository* Repository = UWorld::GetSubsystem<UNavigationObjectRepository>(Owner->GetWorld()))
		{
			UE_LOG(LogNavLink, Log, TEXT("%hs 0x%p"), __FUNCTION__, &CustomLink);
			Repository->UnregisterCustomNavLinkObject(CustomLink);
		}
	}
}

// 用 LinkOwner 的世界变换把链接两端点 A/B 投到世界空间，包成 FBox。
// 用于在 RegisterCustomLink 检测到 ID 冲突时把链接覆盖区域标脏。
FBox UNavigationSystemV1::ComputeCustomLinkBounds(const INavLinkCustomInterface& CustomLink)
{
	const UObject* CustomLinkOb = CustomLink.GetLinkOwner();
	const UActorComponent* OwnerComp = Cast<UActorComponent>(CustomLinkOb);
	const AActor* OwnerActor = OwnerComp ? OwnerComp->GetOwner() : Cast<AActor>(CustomLinkOb);

	FBox LinkBounds(ForceInitToZero);
	if (OwnerActor)
	{
		ENavLinkDirection::Type DummyDir = ENavLinkDirection::BothWays;
		FVector RelativePtA, RelativePtB;
		CustomLink.GetLinkData(RelativePtA, RelativePtB, DummyDir);

		const FTransform OwnerActorTM = OwnerActor->GetTransform();
		const FVector WorldPtA = OwnerActorTM.TransformPosition(RelativePtA);
		const FVector WorldPtB = OwnerActorTM.TransformPosition(RelativePtB);

		LinkBounds += WorldPtA;
		LinkBounds += WorldPtB;
	}
	return LinkBounds;
}

// 全局静态入口：广播到所有 NavSystem 实例（多 World 场景）调用 UnregisterNavAreaClass。
// 由 UNavArea CDO 析构 / 蓝图重编译触发。
void UNavigationSystemV1::RequestAreaUnregistering(UClass* NavAreaClass)
{
	for (TObjectIterator<UNavigationSystemV1> NavSysIt; NavSysIt; ++NavSysIt)
	{
		NavSysIt->UnregisterNavAreaClass(NavAreaClass);
	}
}

// 将一个 NavArea 子类从 NavAreaClasses 中移除并触发 ENavAreaEvent::Unregistered。
// 所有 NavData 会重排 AreaID/移除该 area 的 cost 配置；并广播全局 OnNavAreaUnregisteredDelegate。
void UNavigationSystemV1::UnregisterNavAreaClass(UClass* NavAreaClass)
{
	// remove from known areas
	if (NavAreaClasses.Remove(NavAreaClass) > 0)
	{
		// notify navigation data
		// notify existing nav data
		OnNavigationAreaEvent(NavAreaClass, ENavAreaEvent::Unregistered);

		const UWorld* const World = GetWorld();
		if (ensure(World))
		{
			UNavigationSystemBase::OnNavAreaUnregisteredDelegate().Broadcast(*World, NavAreaClass);
		}
	}
}

// 全局静态入口：把 NavArea CDO 注册广播到所有 NavSystem 实例。
// 通常在 UNavArea CDO 构造完成 / 蓝图编译完成时触发。
void UNavigationSystemV1::RequestAreaRegistering(UClass* NavAreaClass)
{
	for (TObjectIterator<UNavigationSystemV1> NavSysIt; NavSysIt; ++NavSysIt)
	{
		NavSysIt->RegisterNavAreaClass(NavAreaClass);
	}
}

// 把 AreaClass CDO 加入 NavAreaClasses 集合并通知所有 NavData 重排 AreaID/同步 area cost。
// 过滤：抽象类、蓝图骨架类、Developers 目录下的蓝图、已注册的类全部 skip。
// 副作用：调用 InitializeArea 初始化 flags；广播 OnNavAreaRegisteredDelegate。
void UNavigationSystemV1::RegisterNavAreaClass(UClass* AreaClass)
{
	// can't be null
	if (AreaClass == nullptr)
	{
		return;
	}

	// can't be abstract
	if (AreaClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return;
	}

	// special handling of blueprint based areas
	if (AreaClass->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
	{
		// can't be skeleton of blueprint class
		if (AreaClass->GetName().Contains(TEXT("SKEL_")))
		{
			return;
		}

		// can't be class from Developers folder (won't be saved properly anyway)
		const UPackage* Package = AreaClass->GetOutermost();
		if (Package && Package->GetName().Contains(TEXT("/Developers/")))
		{
			return;
		}
	}

	if (NavAreaClasses.Contains(AreaClass))
	{
		// Already added
		return;
	}

	UNavArea* AreaClassCDO = GetMutableDefault<UNavArea>(AreaClass);
	check(AreaClassCDO);

	// initialize flags
	AreaClassCDO->InitializeArea();

	// add to know areas
	NavAreaClasses.Add(AreaClass);

	// notify existing nav data
	OnNavigationAreaEvent(AreaClass, ENavAreaEvent::Registered);

#if WITH_EDITOR
	UNavAreaMeta_SwitchByAgent* SwitchByAgentCDO = Cast<UNavAreaMeta_SwitchByAgent>(AreaClassCDO);
	// update area properties
	if (SwitchByAgentCDO)
	{
		SwitchByAgentCDO->UpdateAgentConfig();
	}
#endif

	const UWorld* const World = GetWorld();
	if (ensure(World))
	{
		UNavigationSystemBase::OnNavAreaRegisteredDelegate().Broadcast(*World, AreaClass);
	}
}

// 把 NavArea 注册/反注册事件转发给所有有效的 NavData（让其重排内部 AreaID 表）。
void UNavigationSystemV1::OnNavigationAreaEvent(UClass* AreaClass, ENavAreaEvent::Type Event)
{
	// notify existing nav data
	for (auto NavigationData : NavDataSet)
	{
		if (NavigationData != NULL && NavigationData->IsPendingKillPending() == false)
		{
			NavigationData->OnNavAreaEvent(AreaClass, Event);
		}
	}
}

int32 UNavigationSystemV1::GetSupportedAgentIndex(const ANavigationData* NavData) const
{
	if (SupportedAgents.Num() == 1)
	{
		return 0;
	}

	const FNavDataConfig& TestConfig = NavData->GetConfig();
	for (int32 AgentIndex = 0; AgentIndex < SupportedAgents.Num(); AgentIndex++)
	{
		if (SupportedAgents[AgentIndex].IsValid() && SupportedAgents[AgentIndex].IsEquivalent(TestConfig))
		{
			return AgentIndex;
		}
	}
	
	return INDEX_NONE;
}

int32 UNavigationSystemV1::GetSupportedAgentIndex(const FNavAgentProperties& NavAgent) const
{
	if (SupportedAgents.Num() == 1)
	{
		return 0;
	}

	for (int32 AgentIndex = 0; AgentIndex < SupportedAgents.Num(); AgentIndex++)
	{
		if (SupportedAgents[AgentIndex].IsValid() && SupportedAgents[AgentIndex].IsEquivalent(NavAgent))
		{
			return AgentIndex;
		}
	}

	return INDEX_NONE;
}

// 编辑器辅助：用 UEnum 的显示名填充 IncludeFlags/ExcludeFlags 16 个 bool 的 DisplayName 元数据。
// 让 UNavigationQueryFilter 编辑器面板上的 NavFlag0..15 显示成游戏自定义名字。
void UNavigationSystemV1::DescribeFilterFlags(UEnum* FlagsEnum) const
{
#if WITH_EDITOR
	TArray<FString> FlagDesc;
	FString EmptyStr;
	FlagDesc.Init(EmptyStr, 16);

	const int32 NumEnums = FMath::Min(16, FlagsEnum->NumEnums() - 1);	// skip _MAX
	for (int32 FlagIndex = 0; FlagIndex < NumEnums; FlagIndex++)
	{
		FlagDesc[FlagIndex] = FlagsEnum->GetDisplayNameTextByIndex(FlagIndex).ToString();
	}

	DescribeFilterFlags(FlagDesc);
#endif
}

// DescribeFilterFlags 数组版：直接给定 16 个名字。空名字位置会清掉 CPF_Edit（在面板隐藏）。
// 自动把"Navigation link"专用位（ARecastNavMesh::GetNavLinkFlag()）保留为系统名。
void UNavigationSystemV1::DescribeFilterFlags(const TArray<FString>& FlagsDesc) const
{
#if WITH_EDITOR
	const int32 MaxFlags = 16;
	TArray<FString> UseDesc = FlagsDesc;

	FString EmptyStr;
	while (UseDesc.Num() < MaxFlags)
	{
		UseDesc.Add(EmptyStr);
	}

	// get special value from recast's navmesh
#if WITH_RECAST
	uint16 NavLinkFlag = ARecastNavMesh::GetNavLinkFlag();
	for (int32 FlagIndex = 0; FlagIndex < MaxFlags; FlagIndex++)
	{
		if ((NavLinkFlag >> FlagIndex) & 1)
		{
			UseDesc[FlagIndex] = TEXT("Navigation link");
			break;
		}
	}
#endif

	// setup properties
	FStructProperty* StructProp1 = FindFProperty<FStructProperty>(UNavigationQueryFilter::StaticClass(), TEXT("IncludeFlags"));
	FStructProperty* StructProp2 = FindFProperty<FStructProperty>(UNavigationQueryFilter::StaticClass(), TEXT("ExcludeFlags"));
	check(StructProp1);
	check(StructProp2);

	UStruct* Structs[] = { StructProp1->Struct, StructProp2->Struct };
	const FString CustomNameMeta = TEXT("DisplayName");

	for (int32 StructIndex = 0; StructIndex < UE_ARRAY_COUNT(Structs); StructIndex++)
	{
		for (int32 FlagIndex = 0; FlagIndex < MaxFlags; FlagIndex++)
		{
			FString PropName = FString::Printf(TEXT("bNavFlag%d"), FlagIndex);
			FProperty* Prop = FindFProperty<FProperty>(Structs[StructIndex], *PropName);
			check(Prop);

			if (UseDesc[FlagIndex].Len())
			{
				Prop->SetPropertyFlags(CPF_Edit);
				Prop->SetMetaData(*CustomNameMeta, *UseDesc[FlagIndex]);
			}
			else
			{
				Prop->ClearPropertyFlags(CPF_Edit);
			}
		}
	}

#endif
}

// 让所有 NavData 丢弃名为 FilterClass 的查询过滤器缓存。
// 通常在 FilterClass CDO 属性变更后调用，强制下次查询重新构造。
void UNavigationSystemV1::ResetCachedFilter(TSubclassOf<UNavigationQueryFilter> FilterClass)
{
	for (int32 NavDataIndex = 0; NavDataIndex < NavDataSet.Num(); NavDataIndex++)
	{
		if (NavDataSet[NavDataIndex])
		{
			NavDataSet[NavDataIndex]->RemoveQueryFilter(FilterClass);
		}
	}
}

// CDO 静态查询：判断给定 World 是否需要创建本类型的 NavSystem 实例。
// 默认规则：World 有效 + (允许客户端导航 || 非客户端模式)。
bool UNavigationSystemV1::ShouldCreateNavigationSystemInstance(const UWorld* World) const
{
	ensureMsgf(IsTemplate(), TEXT("This method is expected to only be called on Template objects"
								" to determine if an instance of this type should be created."));

	return (World != nullptr)
		&& (ShouldAllowClientSideNavigation() || (World->GetNetMode() != NM_Client));
}

// Deprecated
UNavigationSystemV1* UNavigationSystemV1::CreateNavigationSystem(UWorld* WorldOwner)
{
	UNavigationSystemV1* NavSys = nullptr;

	// create navigation system for editor and server targets, but remove it from game clients
	if (GetDefault<UNavigationSystemV1>()->ShouldCreateNavigationSystemInstance(WorldOwner))
	{
		AWorldSettings* WorldSettings = WorldOwner->GetWorldSettings();
		if (WorldSettings == nullptr || WorldSettings->IsNavigationSystemEnabled())
		{
			NavSys = NewObject<UNavigationSystemV1>(WorldOwner, GEngine->NavigationSystemClass);
			WorldOwner->SetNavigationSystem(NavSys);
		}
	}

	return NavSys;
}

// 接管 World 初始化（FWorldDelegates::OnPostWorldInitialization 等）：
// 标记运行模式（Editor/PIE/Game/Inactive），完成 Octree 创建、AbstractNavData 生成、订阅事件。
// 实质转发到 OnWorldInitDone，这里仅是桥接 NavigationSystemBase 接口。
void UNavigationSystemV1::InitializeForWorld(UWorld& World, FNavigationSystemRunMode Mode)
{
	OnWorldInitDone(Mode);
}

UNavigationSystemV1* UNavigationSystemV1::GetCurrent(UWorld* World)
{
	return FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
}

UNavigationSystemV1* UNavigationSystemV1::GetCurrent(UObject* WorldContextObject)
{
	return FNavigationSystem::GetCurrent<UNavigationSystemV1>(WorldContextObject);
}

ANavigationData* UNavigationSystemV1::GetNavDataWithID(const uint16 NavDataID) const
{
	for (int32 NavDataIndex = 0; NavDataIndex < NavDataSet.Num(); ++NavDataIndex)
	{
		const ANavigationData* NavData = NavDataSet[NavDataIndex];
		if (NavData != nullptr && NavData->GetNavDataUniqueID() == NavDataID)
		{
			return const_cast<ANavigationData*>(NavData);
		}
	}

	return nullptr;
}

FNavigationElementHandle UNavigationSystemV1::GetNavigationElementHandleForUObject(const UObject* Object) const
{
	checkf(Repository, TEXT("%hs is expected to be called after the repository gets cached."), __FUNCTION__);
	return Repository->GetNavigationElementHandleForUObject(Object);
}

TSharedPtr<const FNavigationElement> UNavigationSystemV1::GetNavigationElementForUObject(const UObject* Object) const
{
	checkf(Repository, TEXT("%hs is expected to be called after the repository gets cached."), __FUNCTION__);
	return Repository->GetNavigationElementForUObject(Object);
}

// UObject 形式的 NavRelevant 对象（非 Component/Actor）注册入口。
// 通常来自 NavigationObjectRepository 的通知。
void UNavigationSystemV1::OnNavRelevantObjectRegistered(UObject& Object)
{
	if (IsNavigationSystemStatic())
	{
		return;
	}

	if (const INavRelevantInterface* NavInterface = Cast<INavRelevantInterface>(&Object))
	{
		RegisterNavRelevantObjectStatic(*NavInterface, Object);
	}
}

// 把 ActorComponent 注册进 NavOctree。前置条件：
//   - NavSystem 非 Static
//   - 不需要等 Owner Actor 自身先注册（否则跳过；后续 Actor 注册会带它进来）
//   - Owner 视该 Component 为导航相关（IsComponentRelevantForNavigation）
// 命中后转发到 RegisterNavRelevantObjectStatic，最终走 NavigationDataHandler 加进 Octree。
void UNavigationSystemV1::RegisterComponentToNavOctree(UActorComponent* Comp)
{
	SCOPE_CYCLE_COUNTER(STAT_NavOctreeBookkeeping);

	if ((Comp == nullptr) || IsNavigationSystemStatic())
	{
		return;
	}

	if (UE::Navigation::Private::ShouldComponentWaitForActorToRegister(Comp))
	{
		UE_LOG(LogNavigation, VeryVerbose
			, TEXT("%hs: %s registration skipped (waiting for actor to register)"), __FUNCTION__, *GetNameSafe(Comp));
		return;
	}

	if (INavRelevantInterface* NavInterface = Cast<INavRelevantInterface>(Comp))
	{
		const AActor* OwnerActor = Comp->GetOwner();
		if (OwnerActor && OwnerActor->IsComponentRelevantForNavigation(Comp))
		{
			RegisterNavRelevantObjectStatic(*NavInterface, *Comp);
		}
	}
}

// 给定 World 是否支持运行时动态修改导航（要求 NavSystem 非 Static 且需要 NavOctree）。
// 静态烘焙模式下返回 false，调用方据此跳过运行时增量更新逻辑。
bool UNavigationSystemV1::SupportsDynamicChanges(UWorld* World)
{
	if (IsNavigationSystemStatic())
	{
		return false;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	return NavSys && NavSys->RequiresNavOctree();
}

// 程序化构造 FNavigationElement 加入导航：通过 World 的 NavigationObjectRepository 落库，
// 返回稳定句柄供 Remove/Update 使用。Static NavSystem 直接返回 Invalid。
FNavigationElementHandle UNavigationSystemV1::AddNavigationElement(UWorld* World, FNavigationElement&& Element)
{
	SCOPE_CYCLE_COUNTER(STAT_NavOctreeBookkeeping);

	if (IsNavigationSystemStatic())
	{
		return FNavigationElementHandle::Invalid;
	}

	if (UNavigationObjectRepository* Repository = UWorld::GetSubsystem<UNavigationObjectRepository>(World))
	{
		if (const TSharedPtr<const FNavigationElement> SharedElement = Repository->AddNavigationElement(MoveTemp(Element)))
		{
			return SharedElement->GetHandle();
		}
	}

	return FNavigationElementHandle::Invalid;
}

// 与 AddNavigationElement 配对：从 NavigationObjectRepository 移除句柄对应元素，
// Repository 内部会通知 NavOctree 反注册并标脏对应区域。
void UNavigationSystemV1::RemoveNavigationElement(UWorld* World, const FNavigationElementHandle ElementHandle)
{
	SCOPE_CYCLE_COUNTER(STAT_NavOctreeBookkeeping);

	if (IsNavigationSystemStatic())
	{
		return;
	}

	if (UNavigationObjectRepository* Repository = UWorld::GetSubsystem<UNavigationObjectRepository>(World))
	{
		Repository->RemoveNavigationElement(ElementHandle);
	}
}

// NavRelevantObject 反注册回调（来自 NavigationObjectRepository）：转 UnregisterNavRelevantObjectStatic，
// 把对象从所有活跃 NavSystem 的 Octree 中移除。
void UNavigationSystemV1::OnNavRelevantObjectUnregistered(UObject& Object)
{
	UnregisterNavRelevantObjectStatic(Object);
}

// Component 反注册：跳过 IsComponentRelevantForNavigation 检查（删除路径不需要），
// 直接走 UnregisterNavRelevantObjectStatic 把元素从 Octree 移除。
void UNavigationSystemV1::UnregisterComponentToNavOctree(UActorComponent* Comp)
{
	// skip IsComponentRelevantForNavigation check, it's only for adding new stuff
	if (Comp)
	{
		if (UE::Navigation::Private::ShouldComponentWaitForActorToRegister(Comp))
		{
			return;
		}

		UnregisterNavRelevantObjectStatic(*Comp);
	}
}

// Deprecated
void UNavigationSystemV1::AddDirtyArea(const FBox& NewArea, int32 Flags, const FName& DebugReason)
{
	DefaultDirtyAreasController.AddArea(NewArea, static_cast<ENavigationDirtyFlag>(Flags), /*ElementProvideFunc*/ nullptr, /*DirtyElement*/nullptr, DebugReason);
}

// Deprecated
void UNavigationSystemV1::AddDirtyArea(const FBox& NewArea, int32 Flags, const TFunction<UObject*()>&, const FName& DebugReason /*= NAME_None*/)
{
	DefaultDirtyAreasController.AddArea(NewArea, static_cast<ENavigationDirtyFlag>(Flags), /*ElementProvideFunc*/ nullptr, /*DirtyElement*/nullptr, DebugReason);
}

// Deprecated
void UNavigationSystemV1::AddDirtyAreas(const TArray<FBox>& NewAreas, int32 Flags, const FName& DebugReason /*= NAME_None*/)
{ 
	AddDirtyAreas(NewAreas, static_cast<ENavigationDirtyFlag>(Flags), DebugReason);
}

// AddDirtyArea 实际实现（ENavigationDirtyFlag 版）：委托 DefaultDirtyAreasController。
// 触发时机：Octree 元素添加/更新/删除；Brush/Modifier 改动；关卡加载等。
// Flag 决定下游 NavData 应该做哪种类型的重建（Geometry / Modifier / All）。
// 把"包围盒+脏标志"打入 DirtyAreasController 的累积队列。
// 该队列在 Tick 由 ProcessDirtyAreas 派发到 NavData 触发瓦片重建。
void UNavigationSystemV1::AddDirtyArea(const FBox& NewArea, const ENavigationDirtyFlag Flags, const FName& DebugReason /*= NAME_None*/)
{
	DefaultDirtyAreasController.AddArea(NewArea, Flags, nullptr, nullptr, DebugReason);
}

// AddDirtyArea 的延迟元素提供版：传入 lambda，仅在真正消费 dirty area 时取得 FNavigationElement，
// 用于元素生命周期与 dirty area 提交时机不一致的场景，避免悬挂引用。
void UNavigationSystemV1::AddDirtyArea(const FBox& NewArea, const ENavigationDirtyFlag Flags, const TFunction<const TSharedPtr<const FNavigationElement>()>& ElementProviderFunc, const FName& DebugReason /*= NAME_None*/)
{
	DefaultDirtyAreasController.AddArea(NewArea, Flags, ElementProviderFunc, /*DirtyElement*/ nullptr, DebugReason);
}

// 批量打入 dirty areas：逐个转 AddDirtyArea。Flags 为 None 时直接返回（保护用）。
void UNavigationSystemV1::AddDirtyAreas(const TArray<FBox>& NewAreas, const ENavigationDirtyFlag Flags, const FName& DebugReason /*= NAME_None*/)
{
	if (Flags == ENavigationDirtyFlag::None)
	{
		return;
	}

	for (int32 NewAreaIndex = 0; NewAreaIndex < NewAreas.Num(); NewAreaIndex++)
	{
		AddDirtyArea(NewAreas[NewAreaIndex], Flags, DebugReason);
	}
}

int32 UNavigationSystemV1::GetNumDirtyAreas() const
{
	return DefaultDirtyAreasController.GetNumDirtyAreas();
}

bool UNavigationSystemV1::HasDirtyAreasQueued() const
{
	return DefaultDirtyAreasController.IsDirty();
}

// 把 FNavigationElement 注册进 NavOctree（新版接口，基于共享指针元素）。
// 由 NavigationDataHandler 桥接，最终 AddElementToNavOctree + 标脏。
FSetElementId UNavigationSystemV1::RegisterNavigationElementWithNavOctree(const TSharedRef<const FNavigationElement>& Element, const int32 UpdateFlags)
{
	return FNavigationDataHandler(DefaultOctreeController, DefaultDirtyAreasController).RegisterElementWithNavOctree(Element, UpdateFlags);
}

// Deprecated
// 已废弃（基于 UObject + INavRelevantInterface 的旧接口）：
// 内部包装为 FNavigationElement 后转发到新版 RegisterElementWithNavOctree。
FSetElementId UNavigationSystemV1::RegisterNavOctreeElement(UObject* ElementOwner, INavRelevantInterface* ElementInterface, int32 UpdateFlags)
{
	return (ElementOwner && ElementInterface) 
		? FNavigationDataHandler(DefaultOctreeController, DefaultDirtyAreasController).RegisterElementWithNavOctree(FNavigationElement::CreateFromNavRelevantInterface(*ElementInterface), UpdateFlags)
		: FSetElementId();
}

// 真正把元素插入 Octree。由 ProcessPendingOctreeUpdates 逐个消费 PendingUpdates 调用。
// 内部会：
//   1) 把 Element 加入 FNavigationOctree（TOctree2 插入）
//   2) 计算 DirtyBounds + Flags 并 AddDirtyArea（除非 bSkipDirtyAreaOnAddOrRemove）
void UNavigationSystemV1::AddElementToNavOctree(const FNavigationDirtyElement& DirtyElement)
{
	FNavigationDataHandler(DefaultOctreeController, DefaultDirtyAreasController).AddElementToNavOctree(DirtyElement);
}

// 取 Element 在 Octree 中已缓存的 DirtyFlags 与 DirtyBounds（如果存在）。
// 用于复用脏区计算，避免上层重复推算包围盒。
bool UNavigationSystemV1::GetNavOctreeElementData(const FNavigationElementHandle Element, ENavigationDirtyFlag& OutDirtyFlags, FBox& OutDirtyBounds)
{
	return DefaultOctreeController.GetNavOctreeElementData(Element, OutDirtyFlags, OutDirtyBounds);
}

// Deprecated
bool UNavigationSystemV1::GetNavOctreeElementData(const UObject& NodeOwner, int32& DirtyFlags, FBox& DirtyBounds)
{
	ENavigationDirtyFlag TmpDirtyFlags = ENavigationDirtyFlag::None;
	const bool bSuccess = GetNavOctreeElementData(FNavigationElementHandle(&NodeOwner), TmpDirtyFlags, DirtyBounds);
	DirtyFlags = static_cast<int32>(TmpDirtyFlags);
	return bSuccess;
}

// Deprecated
// 旧接口：通过 (Owner+Interface) 反注册 NavOctree 元素，最终走 UnregisterNavRelevantObjectInternal。
void UNavigationSystemV1::UnregisterNavOctreeElement(UObject* ElementOwner, INavRelevantInterface* ElementInterface, int32 UpdateFlags)
{
	if (ElementOwner && ElementInterface)
	{
		UnregisterNavRelevantObjectInternal(*ElementOwner);
	}
}

// 新接口：把 FNavigationElement 从 NavOctree 反注册（含标脏其覆盖区）。
void UNavigationSystemV1::UnregisterNavigationElementWithOctree(const TSharedRef<const FNavigationElement>& Element, const int32 UpdateFlags)
{
	FNavigationDataHandler(DefaultOctreeController, DefaultDirtyAreasController).UnregisterElementWithNavOctree(Element, UpdateFlags);
}

// Deprecated
void UNavigationSystemV1::RemoveObjectsNavOctreeId(const UObject& Object)
{
	// doing nothing since we don't want external calls to remove a mapping without properly update the nodes
}

// Deprecated
void UNavigationSystemV1::RemoveNavOctreeElementId(const FOctreeElementId2& ElementId, const int32 UpdateFlags)
{
	RemoveFromNavOctree(ElementId, UpdateFlags);
}

// 按 Octree ElementId 删除元素并按需标脏（内部接口，已知精确节点位置时使用）。
void UNavigationSystemV1::RemoveFromNavOctree(const FOctreeElementId2& ElementId, const int32 UpdateFlags)
{
	FNavigationDataHandler(DefaultOctreeController, DefaultDirtyAreasController).RemoveFromNavOctree(ElementId, UpdateFlags);
}

// 触发 Lazy 收集：在真正需要数据时，调用元素的 GatherNavigationData 把 Modifier/几何信息填入 ElementData。
// 配合 FNavigationOctree::bGatherGeometry / bGatherDataLazily 使用，节省内存。
void UNavigationSystemV1::DemandLazyDataGathering(FNavigationRelevantData& ElementData)
{
	FNavigationDataHandler(DefaultOctreeController, DefaultDirtyAreasController).DemandLazyDataGathering(ElementData);
}

// Deprecated
const FNavigationRelevantData* UNavigationSystemV1::GetDataForObject(const UObject& Object) const
{
	return GetDataForElement(FNavigationElementHandle(&Object));
}

// Deprecated
FNavigationRelevantData* UNavigationSystemV1::GetMutableDataForObject(const UObject& Object)
{
	return GetMutableDataForElement(FNavigationElementHandle(&Object));
}

const FNavigationRelevantData* UNavigationSystemV1::GetDataForElement(const FNavigationElementHandle Element) const
{
	return DefaultOctreeController.GetDataForElement(Element);
}

FNavigationRelevantData* UNavigationSystemV1::GetMutableDataForElement(const FNavigationElementHandle Element)
{
	return DefaultOctreeController.GetMutableDataForElement(Element);
}

// 全局静态注册：从 Object 的 World 找到 NavigationObjectRepository，把对象登记进去。
// Repository 会按需通知本 World 的 NavSystem，把元素加入 NavOctree。
void UNavigationSystemV1::RegisterNavRelevantObjectStatic(const INavRelevantInterface& NavRelevantObject, const UObject& Object)
{
	SCOPE_CYCLE_COUNTER(STAT_NavOctreeBookkeeping);

	if (IsNavigationSystemStatic())
	{
		return;
	}

	if (UNavigationObjectRepository* Repository = UWorld::GetSubsystem<UNavigationObjectRepository>(Object.GetWorld()))
	{
		Repository->RegisterNavRelevantObject(NavRelevantObject);
	}
}

// 实例方法：直接使用本 NavSystem 缓存的 Repository 注册（避免再走 World->Subsystem 查表）。
// 仅用于内部已知 NavSystem 实例的快速路径。
void UNavigationSystemV1::RegisterNavRelevantObjectInternal(const INavRelevantInterface& NavRelevantObject, const UObject& Object)
{
	SCOPE_CYCLE_COUNTER(STAT_NavOctreeBookkeeping);

	if (IsNavigationSystemStatic())
	{
		return;
	}

	if (Repository == nullptr)
	{
		return;
	}

	Repository->RegisterNavRelevantObject(NavRelevantObject);
}

// 全局静态反注册：从 Object 的 World 找到 Repository，将其登记移除（并触发 Octree 反注册）。
void UNavigationSystemV1::UnregisterNavRelevantObjectStatic(const UObject& Object)
{
	SCOPE_CYCLE_COUNTER(STAT_NavOctreeBookkeeping);

	if (IsNavigationSystemStatic())
	{
		return;
	}

	if (UNavigationObjectRepository* Repository = UWorld::GetSubsystem<UNavigationObjectRepository>(Object.GetWorld()))
	{
		Repository->UnregisterNavRelevantObject(&Object);
	}
}

// 实例方法：通过本 NavSystem 缓存的 Repository 反注册对象（快速路径）。
void UNavigationSystemV1::UnregisterNavRelevantObjectInternal(const UObject& Object)
{
	SCOPE_CYCLE_COUNTER(STAT_NavOctreeBookkeeping);

	if (IsNavigationSystemStatic())
	{
		return;
	}

	if (Repository == nullptr)
	{
		return;
	}

	Repository->UnregisterNavRelevantObject(&Object);
}

// 通用更新入口：把对象在 Repository 中刷新成新的 FNavigationElement，再对找到的 NavSystem 调用 InCallback
// （通常是 UpdateNavOctreeElement）。NavSystem 不可用时退化为静态注册，等系统初始化后再补登。
void UNavigationSystemV1::UpdateNavRelevantObjectInNavOctreeStatic(
	const INavRelevantInterface& InNavRelevantObject,
	const UObject& InObject,
	UNavigationSystemV1* InNavigationSystem,
	TFunctionRef<void(UNavigationSystemV1&, const TSharedRef<const FNavigationElement>&)> InCallback)
{
	SCOPE_CYCLE_COUNTER(STAT_NavOctreeBookkeeping);

	if (IsNavigationSystemStatic())
	{
		return;
	}

	if (!ensureMsgf(InNavRelevantObject.IsNavigationRelevant(), TEXT("%hs: %s is not navigation relevant"), __FUNCTION__, *InObject.GetName()))
	{
		return;
	}

	if (UNavigationSystemV1* NavSys = InNavigationSystem ? InNavigationSystem : FNavigationSystem::GetCurrent<UNavigationSystemV1>(InObject.GetWorld()))
	{
		if (NavSys->Repository)
		{
			if (const TSharedPtr<const FNavigationElement> SharedElement = NavSys->Repository->UpdateNavigationElementForUObject(InNavRelevantObject, InObject))
			{
				InCallback(*NavSys, SharedElement.ToSharedRef());
			}
		}
	}
	else
	{
		// Navigation system not available so use the static registration to be stored in the repository
		// so the navigation system will gather it on initialization.
		UE_LOG(LogNavigation, VeryVerbose,
			TEXT("%hs: %s Registering to the repository (NavigationSystem not available)"), __FUNCTION__, *InObject.GetName());

		RegisterNavRelevantObjectStatic(InNavRelevantObject, InObject);
	}
}

void UNavigationSystemV1::UpdateNavRelevantObjectInNavOctree(UObject& Object)
{
	SCOPE_CYCLE_COUNTER(STAT_NavOctreeBookkeeping);

	if (IsNavigationSystemStatic())
	{
		return;
	}

	const INavRelevantInterface* NavRelevantInterface = Cast<INavRelevantInterface>(&Object);
	if (NavRelevantInterface && NavRelevantInterface->IsNavigationRelevant())
	{
		UpdateNavRelevantObjectInNavOctreeStatic(*NavRelevantInterface, Object, /*NavigationSystem*/nullptr,
			[](UNavigationSystemV1& NavSys, const TSharedRef<const FNavigationElement>& SharedElement)
			{
				NavSys.UpdateNavOctreeElement(SharedElement->GetHandle(), SharedElement, FNavigationOctreeController::OctreeUpdate_Default);
			});
	}
}

// FNavigationElement 直接更新入口（非 UObject 路径）：找到 World 上的 NavSystem 并调 UpdateNavOctreeElement，
// 用于程序化构造的导航元素（无 UObject 包装）变更通知。
void UNavigationSystemV1::OnNavigationElementUpdated(UWorld* World, const FNavigationElementHandle ElementHandle, FNavigationElement&& Element)
{
	SCOPE_CYCLE_COUNTER(STAT_NavOctreeBookkeeping);

	if (IsNavigationSystemStatic())
	{
		return;
	}

	if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		NavSys->UpdateNavOctreeElement(ElementHandle, MakeShared<FNavigationElement>(MoveTemp(Element)) , FNavigationOctreeController::OctreeUpdate_Default);
	}
}

void UNavigationSystemV1::UpdateActorInNavOctree(AActor& Actor)
{
	UpdateNavRelevantObjectInNavOctree(Actor);
}

// 更新组件在 Octree 里的登记（Bounds/数据变化）。
// 委托 FNavigationDataHandler::UpdateNavOctreeElement（走 PendingUpdates 队列）。
// 典型触发：UNavRelevantComponent::RefreshNavigationModifiers、Primitive Transform 变化。
void UNavigationSystemV1::UpdateComponentInNavOctree(UActorComponent& Comp)
{
	SCOPE_CYCLE_COUNTER(STAT_NavOctreeBookkeeping);

	if (ShouldUpdateNavOctreeOnComponentChange() == false)
	{
		return;
	}

	// Due to an issue with PostEditChangeProperty and AActor::RerunConstructionScripts()
	// we need to make sure that we are not processing an invalid component.
	// Could be converted to an ensure once UE-252220 is fixed
	if (!IsValid(&Comp))
	{
		return;
	}

	if (UE::Navigation::Private::ShouldComponentWaitForActorToRegister(&Comp))
	{
		return;
	}

	// special case for early out: use cached nav relevancy
	if (Comp.bNavigationRelevant)
	{
		if (const AActor* OwnerActor = Comp.GetOwner())
		{
			const INavRelevantInterface* NavRelevantInterface = Cast<INavRelevantInterface>(&Comp);
			if (ensureMsgf(NavRelevantInterface != nullptr, TEXT("Components reaching this point are expected to implement INavRelevantInterface.")))
			{
				if (OwnerActor->IsComponentRelevantForNavigation(&Comp) && Comp.IsNavigationRelevant())
				{

					UpdateNavRelevantObjectInNavOctreeStatic(*NavRelevantInterface, Comp, /*NavigationSystem*/nullptr,
						[](UNavigationSystemV1& NavSys, const TSharedRef<const FNavigationElement>& SharedElement)
						{
							NavSys.UpdateNavOctreeElement(SharedElement->GetHandle(), SharedElement, FNavigationOctreeController::OctreeUpdate_Default);
						});
				}
				else
				{
					if (UNavigationObjectRepository* Repository = UWorld::GetSubsystem<UNavigationObjectRepository>(Comp.GetWorld()))
					{
						Repository->UnregisterNavRelevantObject(&Comp);
					}
				}
			}
		}
	}
	else if (Comp.CanEverAffectNavigation()
#if WITH_EDITOR
		|| GIsReconstructingBlueprintInstances
		// This condition handles a crappy edge case with component registration in Editor
		// Problem occurs when a component in an instance has 'bCanEverAffectNavigation = false' and AActor::RerunConstructionScripts() is called
		// 1. Current component values are serialized to FActorComponentInstanceData
		// 2. Component gets unregistered then destroyed (nothing to do here since it is not affecting navigation)
		// 3. New component gets created and registered using default values from the template (default is affecting navigation so we register to the octree)
		// 4. FActorComponentInstanceData is applied to the component (changing `bCanEverAffectNavigation` from `true` to `false` directly in memory)
		// 5. Component will re-register itself since it was registered at Step 3
		//    Problem is that we normally don't need to do anything for components never affecting navigation
		//    so we never unregister that component from the octree!
#endif // WITH_EDITOR
		)
	{
		// could have been relevant before and now it isn't. Need to check if there's an octree element ID for it
		if (UNavigationObjectRepository* Repository = UWorld::GetSubsystem<UNavigationObjectRepository>(Comp.GetWorld()))
		{
			Repository->UnregisterNavRelevantObject(&Comp);
		}
	}
}

// 更新 Actor 整体 + 其所有相关组件在 Octree 里的登记。
// 典型触发：Actor 被移动 / 子组件关系变化 / 编辑器 Tab 切换。
// bUpdateAttachedActors=true 时会连带更新所有挂接的子 Actor（递归）。
void UNavigationSystemV1::UpdateActorAndComponentsInNavOctree(AActor& Actor, const bool bUpdateAttachedActors)
{
	SCOPE_CYCLE_COUNTER(STAT_NavOctreeBookkeeping);

	if (IsNavigationSystemStatic())
	{
		return;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Actor.GetWorld());

	// Callback to update an actor with its components
	auto UpdateActorAndComponentFunc = [NavSys](AActor& ActorToUpdate)
		{
			const INavRelevantInterface* ActorNavRelevantInterface = Cast<INavRelevantInterface>(&ActorToUpdate);
			if (ActorNavRelevantInterface && ActorNavRelevantInterface->IsNavigationRelevant())
			{
				UpdateNavRelevantObjectInNavOctreeStatic(*ActorNavRelevantInterface, ActorToUpdate, NavSys,
					[](UNavigationSystemV1& InNavSys, const TSharedRef<const FNavigationElement>& SharedElement)
					{
						InNavSys.UpdateNavOctreeElement(SharedElement->GetHandle(), SharedElement, FNavigationOctreeController::OctreeUpdate_Default);
					});
			}

			for (UActorComponent* Component : ActorToUpdate.GetComponents())
			{
				if (Component != nullptr
					&& Component->CanEverAffectNavigation()
					&& Component->IsRegistered()
					&& ActorToUpdate.IsComponentRelevantForNavigation(Component))
				{
					const INavRelevantInterface* ComponentNavRelevantInterface = Cast<INavRelevantInterface>(Component);
					if (ComponentNavRelevantInterface && ComponentNavRelevantInterface->IsNavigationRelevant())
					{
						UpdateNavRelevantObjectInNavOctreeStatic(*ComponentNavRelevantInterface, *Component, NavSys,
							[](UNavigationSystemV1& InNavSys, const TSharedRef<const FNavigationElement>& InElement)
							{
								InNavSys.UpdateNavOctreeElement(InElement->GetHandle(), InElement, FNavigationOctreeController::OctreeUpdate_Default);
							});
						continue;
					}
				}

				if (NavSys)
				{
					NavSys->UnregisterNavRelevantObjectInternal(*Component);
				}
			}
		};

	if (ShouldUpdateNavOctreeOnComponentChange())
	{
		UpdateActorAndComponentFunc(Actor);
	}
	else
	{
		const INavRelevantInterface* ActorNavRelevantInterface = Cast<INavRelevantInterface>(&Actor);
		if (ActorNavRelevantInterface && ActorNavRelevantInterface->IsNavigationRelevant())
		{
			UpdateNavRelevantObjectInNavOctreeStatic(*ActorNavRelevantInterface, Actor, NavSys,
				[](UNavigationSystemV1& InNavSys, const TSharedRef<const FNavigationElement>& SharedElement)
				{
					InNavSys.UpdateNavOctreeElement(SharedElement->GetHandle(), SharedElement, FNavigationOctreeController::OctreeUpdate_Default);
				});
		}
	}

	if (bUpdateAttachedActors)
	{
		TArray<AActor*> UniqueAttachedActors;
		if (GetAllAttachedActors(Actor, UniqueAttachedActors) > 0)
		{
			for (AActor* AttachedActor : UniqueAttachedActors)
			{
				checkf(AttachedActor, TEXT("GetAllAttachedActors should only return unique, non-null ptrs."));
				UpdateActorAndComponentFunc(*AttachedActor);
			}
		}
	}
}

// 当 SceneComponent 移动后被调用：仅在它是 Owner 的根组件时，整个 Actor + 子 Actor 全更新一遍。
// 非根组件不进入这里（其自身 OnRegister 路径会走）。
void UNavigationSystemV1::UpdateNavOctreeAfterMove(USceneComponent* Comp)
{
	AActor* OwnerActor = Comp->GetOwner();
	if (OwnerActor && OwnerActor->GetRootComponent() == Comp)
	{
		UpdateActorAndComponentsInNavOctree(*OwnerActor, true);
	}
}

int32 UNavigationSystemV1::GetAllAttachedActors(const AActor& RootActor, TArray<AActor*>& OutAttachedActors)
{
	OutAttachedActors.Reset();
	RootActor.GetAttachedActors(OutAttachedActors);

	TArray<AActor*> TempAttachedActors;
	for (int32 ActorIndex = 0; ActorIndex < OutAttachedActors.Num(); ++ActorIndex)
	{
		check(OutAttachedActors[ActorIndex]);
		// find all attached actors
		OutAttachedActors[ActorIndex]->GetAttachedActors(TempAttachedActors);

		for (int32 AttachmentIndex = 0; AttachmentIndex < TempAttachedActors.Num(); ++AttachmentIndex)
		{
			// and store the ones we don't know about yet
			OutAttachedActors.AddUnique(TempAttachedActors[AttachmentIndex]);
		}
	}

	return OutAttachedActors.Num();
}

// 收集 RootActor 下所有挂接（含递归）的子 Actor 后，逐个 UpdateActorAndComponentsInNavOctree。
// 用于父对象变换时把子层级跟着同步到 Octree。
void UNavigationSystemV1::UpdateAttachedActorsInNavOctree(AActor& RootActor)
{
	TArray<AActor*> UniqueAttachedActors;
	if (GetAllAttachedActors(RootActor, UniqueAttachedActors) > 0)
	{
		for (AActor* AttachedActor : UniqueAttachedActors)
		{
			UpdateActorAndComponentsInNavOctree(*AttachedActor, /*bUpdateAttachedActors=*/false);
		}
	}
}

// 让 Actor 内所有 NavRelevant 组件重新计算自身的导航包围盒（INavRelevantInterface::UpdateNavigationBounds）。
// 通常在组件 transform/parent 变化、bCanEverAffectNavigation 翻转后调用。
void UNavigationSystemV1::UpdateNavOctreeBounds(AActor* Actor)
{
	for (UActorComponent* Component : Actor->GetComponents())
	{
		INavRelevantInterface* NavElement = Cast<INavRelevantInterface>(Component);
		if (NavElement)
		{
			NavElement->UpdateNavigationBounds();
		}
	}
}

// 一次性把 Actor + 其所有组件从 Octree 中移除（销毁/卸载场景）。
// 内部走 OnActorUnregistered + 逐组件 OnComponentUnregistered。
void UNavigationSystemV1::ClearNavOctreeAll(AActor* Actor)
{
	if (Actor)
	{
		OnActorUnregistered(Actor);

		TInlineComponentArray<UActorComponent*> Components;
		Actor->GetComponents(Components);

		for (int32 Idx = 0; Idx < Components.Num(); Idx++)
		{
			OnComponentUnregistered(Components[Idx]);
		}
	}
}

// Deprecated
// 已废弃 UObject 重载：经 Repository 找句柄→构造 FNavigationElement→走新版 UpdateNavOctreeElement。
void UNavigationSystemV1::UpdateNavOctreeElement(UObject* ElementOwner, INavRelevantInterface* ElementInterface, int32 UpdateFlags)
{
	if (IsNavigationSystemStatic())
	{
		return;
	}

	if (ElementOwner && ElementInterface)
	{
		if (const FNavigationElementHandle Handle = GetNavigationElementHandleForUObject(ElementOwner))
		{
			FNavigationDataHandler(DefaultOctreeController, DefaultDirtyAreasController).UpdateNavOctreeElement(
				Handle,
				FNavigationElement::CreateFromNavRelevantInterface(*ElementInterface),
				UpdateFlags);
		}
	}
}

// Octree 元素更新核心：替换 Element 数据 + 计算新旧并集脏区 + 标脏。
// UpdateFlags 控制是否合并/跳过几何重收集等。委托 FNavigationDataHandler 实现。
void UNavigationSystemV1::UpdateNavOctreeElement(const FNavigationElementHandle Handle, const TSharedRef<const FNavigationElement>& Element, const int32 UpdateFlags)
{
	if (IsNavigationSystemStatic())
	{
		return;
	}

	FNavigationDataHandler(DefaultOctreeController, DefaultDirtyAreasController).UpdateNavOctreeElement(Handle, Element, UpdateFlags);
}

// Deprecated
// 已废弃：沿父链向上传播更新（用于子组件 bounds 变化需要让父 Actor 一起重算的旧路径）。
void UNavigationSystemV1::UpdateNavOctreeParentChain(UObject* ElementOwner, bool bSkipElementOwnerUpdate)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (ElementOwner)
	{
		FNavigationDataHandler(DefaultOctreeController, DefaultDirtyAreasController).UpdateNavOctreeParentChain(*ElementOwner, bSkipElementOwnerUpdate);
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

// Deprecated
// 已废弃 UObject 重载：仅更新 Octree 中元素的 Bounds（不重新收集几何/Modifier），
// 并附加 DirtyAreas 数组（额外需要标脏的区域）。从 Repository 取句柄后转新版。
bool UNavigationSystemV1::UpdateNavOctreeElementBounds(UObject& Object, const FBox& NewBounds, TConstArrayView<FBox> DirtyAreas)
{
	if (Repository == nullptr)
	{
		return false;
	}

	const FNavigationElementHandle Handle = Repository->GetNavigationElementHandleForUObject(&Object);
	if (Handle.IsValid())
	{
		return UpdateNavOctreeElementBounds(Handle, NewBounds, DirtyAreas);
	}

	return false;
}

// 仅更新 Octree 元素 Bounds 的轻量路径：先同步到 FNavigationElement::SetBounds，
// 再调 NavigationDataHandler 把新 Bounds 与额外 DirtyAreas 标脏（不重收数据）。
bool UNavigationSystemV1::UpdateNavOctreeElementBounds(const FNavigationElementHandle Handle, const FBox& NewBounds, const TConstArrayView<FBox> DirtyAreas)
{
	if (IsNavigationSystemStatic())
	{
		return false;
	}

	if (const TSharedPtr<FNavigationElement> NavigationElement = Repository ? Repository->GetMutableNavigationElementForHandle(Handle) : nullptr)
	{
		NavigationElement->SetBounds(NewBounds);
	}

	return FNavigationDataHandler(DefaultOctreeController, DefaultDirtyAreasController).UpdateNavOctreeElementBounds(Handle, NewBounds, DirtyAreas);
}

// Deprecated
// 已废弃 UObject 重载：把 Octree 中 Object 对应元素中所有 OldArea 的 Modifier 替换为 NewArea。
// bReplaceChildClasses=true 时也匹配 OldArea 的子类。从 Repository 解析句柄后转新版。
bool UNavigationSystemV1::ReplaceAreaInOctreeData(const UObject& Object, TSubclassOf<UNavArea> OldArea, TSubclassOf<UNavArea> NewArea, bool bReplaceChildClasses)
{
	if (Repository == nullptr)
	{
		return false;
	}

	if (const FNavigationElementHandle Handle = Repository->GetNavigationElementHandleForUObject(&Object))
	{
		return ReplaceAreaInOctreeData(Handle, OldArea, NewArea, bReplaceChildClasses);
	}

	return false;
}

// 同上，基于句柄的版本：在 Octree 数据里把 OldArea 替换为 NewArea；
// 用于运行时 NavArea 切换（如 SmartLink Enable/Disable）而不必重收几何。
bool UNavigationSystemV1::ReplaceAreaInOctreeData(const FNavigationElementHandle Handle, const TSubclassOf<UNavArea> OldArea, const TSubclassOf<UNavArea> NewArea, const bool bReplaceChildClasses)
{
	if (IsNavigationSystemStatic())
	{
		return false;
	}

	return FNavigationDataHandler(DefaultOctreeController, DefaultDirtyAreasController).ReplaceAreaInOctreeData(Handle, OldArea, NewArea, bReplaceChildClasses);
}

// 组件注册的静态入口（UNavRelevantComponent::OnRegister 会调到这里）。
// 从组件所在 World 找 NavigationSystem，委托走 FNavigationDataHandler::RegisterElementWithNavOctree。
void UNavigationSystemV1::OnComponentRegistered(UActorComponent* Comp)
{
	RegisterComponentToNavOctree(Comp);
}

void UNavigationSystemV1::OnComponentUnregistered(UActorComponent* Comp)
{
	UnregisterComponentToNavOctree(Comp);
}

// 同 OnComponentRegistered 的对外别名，转发到 RegisterComponentToNavOctree。
void UNavigationSystemV1::RegisterComponent(UActorComponent* Comp)
{
	RegisterComponentToNavOctree(Comp);
}

void UNavigationSystemV1::UnregisterComponent(UActorComponent* Comp)
{
	UnregisterComponentToNavOctree(Comp);
}

// Actor 级别的注册：枚举 Actor 所有 NavRelevant 组件，逐个登记到 Octree。
// 另外把 Actor 本身（若实现 INavRelevantInterface）也登记一次。
void UNavigationSystemV1::OnActorRegistered(AActor* Actor)
{
	if (IsNavigationSystemStatic())
	{
		return;
	}

	if (const INavRelevantInterface* NavInterface = Cast<INavRelevantInterface>(Actor))
	{
		RegisterNavRelevantObjectStatic(*NavInterface, *Actor);
	}

	if (UE::Navigation::Private::bComponentShouldWaitForActorToRegister)
	{
		checkf(Actor && Actor->HasActorRegisteredAllComponents()
			, TEXT("Actor is expected to be valid and all its components registered."));

		// Tell all components they need to update their navigation bounds before getting registered to the navigation octree.
		UpdateNavOctreeBounds(Actor);

		// We can now process all the components registered to the scene.
		// Note that we do so using the delegate since it is possible for derived systems to override them.
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component && Component->IsRegistered())
			{
				FNavigationSystem::OnComponentRegistered(*Component);
			}
		}
	}
}

// Actor 反注册：把 Actor 自身从 Repository 中移除（其组件由 OnComponentUnregistered 各自处理）。
void UNavigationSystemV1::OnActorUnregistered(AActor* Actor)
{
	if (IsNavigationSystemStatic())
	{
		return;
	}

	if (Actor)
	{
		UnregisterNavRelevantObjectStatic(*Actor);
	}
}

// 在 NavOctree 中按 QueryBox + Filter 查找元素，结果填入 Elements。
// 给 NavData 生成器（如 RecastTileGenerator）拿瓦片相关元素用。
void UNavigationSystemV1::FindElementsInNavOctree(const FBox& QueryBox, const FNavigationOctreeFilter& Filter, TArray<FNavigationOctreeElement>& Elements)
{	
	FNavigationDataHandler(DefaultOctreeController, DefaultDirtyAreasController).FindElementsInNavOctree(QueryBox, Filter, Elements);
}

// 解除"初始构建锁"：World 初始化期间为避免重复重建会上 InitialLock，加载完毕后调本函数解锁。
// 解锁后累积的 dirty areas 才会真正派发到 NavData 生成器。
void UNavigationSystemV1::ReleaseInitialBuildingLock()
{
	RemoveNavigationBuildLock(ENavigationBuildLock::InitialLock);
}

// World 初始化时把已可见 Level 上的世界静态碰撞导入 Octree。仅执行一次（bInitialLevelsAdded）。
// 后续 Level 流式加载由 OnLevelAddedToWorld 处理。
void UNavigationSystemV1::InitializeLevelCollisions()
{
	if (IsNavigationSystemStatic())
	{
		bInitialLevelsAdded = true;
		return;
	}

	UWorld* World = GetWorld();
	if (!bInitialLevelsAdded && FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) == this)
	{
		// Process all visible levels
		const auto& Levels = World->GetLevels();
		for (ULevel* Level : Levels)
		{
			if (Level->bIsVisible)
			{
				AddLevelCollisionToOctree(Level);
			}
		}

		bInitialLevelsAdded = true;
	}
}

#if WITH_EDITOR
// 编辑器：Level 几何变了重新拉一遍——先 OnLevelRemovedFromWorld 再 OnLevelAddedToWorld。
// 简单暴力但确保所有 nav-relevant 元素被刷新。
void UNavigationSystemV1::UpdateLevelCollision(ULevel* InLevel)
{
	if (InLevel != nullptr)
	{
		UWorld* World = GetWorld();
		OnLevelRemovedFromWorld(InLevel, World);
		OnLevelAddedToWorld(InLevel, World);
	}
}
#endif

// NavMeshBoundsVolume 几何变化（缩放/移动）后回调：根据有无有效 AreaBox 决定 Updated/Removed，
// 入队 PendingNavBoundsUpdates 等下一帧 PerformNavigationBoundsUpdate 处理。
void UNavigationSystemV1::OnNavigationBoundsUpdated(ANavMeshBoundsVolume* NavVolume)
{
	if (NavVolume == nullptr || IsNavigationSystemStatic())
	{
		return;
	}

	FNavigationBoundsUpdateRequest UpdateRequest;
	UpdateRequest.NavBounds.UniqueID = NavVolume->GetUniqueID();
	UpdateRequest.NavBounds.AreaBox = NavVolume->GetComponentsBoundingBox(true);
	UpdateRequest.NavBounds.Level = NavVolume->GetLevel();
	UpdateRequest.NavBounds.SupportedAgents = NavVolume->SupportedAgents;
	
	if (UpdateRequest.NavBounds.AreaBox.IsValid)
	{
		UpdateRequest.UpdateRequest = FNavigationBoundsUpdateRequest::Updated;
	}
	else
	{
		// Make a removal request if the bounds are invalid.
		UpdateRequest.UpdateRequest = FNavigationBoundsUpdateRequest::Removed;
	}

	CheckToLimitNavigationBoundsToLoadedRegions(UpdateRequest.NavBounds);
	AddNavigationBoundsUpdateRequest(UpdateRequest);
}

// NavMeshBoundsVolume 注册时回调：构造 Added 类型 UpdateRequest 入队。
// 副作用：可能在 World Partition 模式下被 CheckToLimitNavigationBoundsToLoadedRegions 截短。
void UNavigationSystemV1::OnNavigationBoundsAdded(ANavMeshBoundsVolume* NavVolume)
{
	if (NavVolume == nullptr || IsNavigationSystemStatic())
	{
		return;
	}

	FNavigationBoundsUpdateRequest UpdateRequest;
	UpdateRequest.NavBounds.UniqueID = NavVolume->GetUniqueID();
	UpdateRequest.NavBounds.AreaBox = NavVolume->GetComponentsBoundingBox(true);
	UpdateRequest.NavBounds.Level = NavVolume->GetLevel();
	UpdateRequest.NavBounds.SupportedAgents = NavVolume->SupportedAgents;

	UpdateRequest.UpdateRequest = FNavigationBoundsUpdateRequest::Added;

	CheckToLimitNavigationBoundsToLoadedRegions(UpdateRequest.NavBounds);
	AddNavigationBoundsUpdateRequest(UpdateRequest);
}

// NavMeshBoundsVolume 销毁时回调：构造 Removed UpdateRequest 入队。
void UNavigationSystemV1::OnNavigationBoundsRemoved(ANavMeshBoundsVolume* NavVolume)
{
	if (NavVolume == nullptr || IsNavigationSystemStatic())
	{
		return;
	}
	
	FNavigationBoundsUpdateRequest UpdateRequest;
	UpdateRequest.NavBounds.UniqueID = NavVolume->GetUniqueID();
	UpdateRequest.NavBounds.AreaBox = NavVolume->GetComponentsBoundingBox(true);
	UpdateRequest.NavBounds.Level = NavVolume->GetLevel();
	UpdateRequest.NavBounds.SupportedAgents = NavVolume->SupportedAgents;

	UpdateRequest.UpdateRequest = FNavigationBoundsUpdateRequest::Removed;

	CheckToLimitNavigationBoundsToLoadedRegions(UpdateRequest.NavBounds);
	AddNavigationBoundsUpdateRequest(UpdateRequest);
}

// World Partition + 编辑器场景：把 NavBounds 裁剪到当前用户加载的 Editor 区域内，
// 避免给未加载区域生成空导航瓦片。仅在存在 World-Partitioned NavMesh 且 EditorWorld 时生效。
void UNavigationSystemV1::CheckToLimitNavigationBoundsToLoadedRegions(FNavigationBounds& OutBounds) const
{
#if WITH_EDITOR && WITH_RECAST
	// Find out if at least one of the nav meshes is world partitioned
	bool bAnyWorldPartitionedNavMeshes = false;
	for (const TObjectPtr<ANavigationData>& NavData : NavDataSet)
	{
		const ARecastNavMesh* RecastNavMesh = Cast<ARecastNavMesh>(NavData);
		if (RecastNavMesh && RecastNavMesh->bIsWorldPartitioned)
		{
			bAnyWorldPartitionedNavMeshes = true;
			break;
		}
	}

	// Don't limit nav bounds if none of the nav meshes are world partitioned
	if (!bAnyWorldPartitionedNavMeshes)
	{
		return;
	}

	// Don't limit nav bounds at runtime
	const UWorld* World = MainNavData->GetWorld();
	if (!World || World->WorldType != EWorldType::Editor)
	{
		return;
	}
	
	// Don't limit nav bounds if not in a world partitioned world
	const UWorldPartition* WorldPartition = World->GetWorldPartition();
	if (!WorldPartition)
	{
		return;
	}
	
	// Get all loaded regions from the world partition
	const TArray<FBox> LoadedWorldPartitionRegions = WorldPartition->GetUserLoadedEditorRegions();

	// Store all overlaps between loaded world partition regions and UpdateRequest's nav bounds
	TArray<FBox> OverlapRegions;
	for (const FBox& LoadedWorldPartitionRegion : LoadedWorldPartitionRegions)
	{
		if (OutBounds.AreaBox.Intersect(LoadedWorldPartitionRegion))
		{
			OverlapRegions.Add(OutBounds.AreaBox.Overlap(LoadedWorldPartitionRegion));
		}
	}

	// Merge all regions which overlap UpdateRequest's nav bounds
	if (!OverlapRegions.IsEmpty())
	{
		OutBounds.AreaBox = FBox(ForceInitToZero);
		for (const FBox& OverlapRegion : OverlapRegions)
		{
			OutBounds.AreaBox += OverlapRegion;
		}
	}
#endif // WITH_EDITOR && WITH_RECAST
}

// 把 NavBounds 更新请求加入 PendingNavBoundsUpdates。规则：
//   - 同 ID 已有请求：覆盖；特殊地，Removed 紧跟 Added 且 bounds 未变 → 取消（一次都不更新）
//   - 否则追加到队列尾部
void UNavigationSystemV1::AddNavigationBoundsUpdateRequest(const FNavigationBoundsUpdateRequest& UpdateRequest)
{
	const int32 ExistingIdx = PendingNavBoundsUpdates.IndexOfByPredicate([&](const FNavigationBoundsUpdateRequest& Element) {
		return UpdateRequest.NavBounds.UniqueID == Element.NavBounds.UniqueID;
	});

	if (ExistingIdx != INDEX_NONE)
	{
		// catch the case where the bounds was removed and immediately re-added with the same bounds as before
		// in that case, we can cancel any update at all
		bool bCanCancelUpdate = false;
		if (PendingNavBoundsUpdates[ExistingIdx].UpdateRequest == FNavigationBoundsUpdateRequest::Removed && UpdateRequest.UpdateRequest == FNavigationBoundsUpdateRequest::Added)
		{
			for (TSet<FNavigationBounds>::TConstIterator It(RegisteredNavBounds); It; ++It)
			{
				if (*It == UpdateRequest.NavBounds)
				{
					bCanCancelUpdate = true;
					break;
				}
			}
		}
		if (bCanCancelUpdate)
		{
			PendingNavBoundsUpdates.RemoveAt(ExistingIdx);
		}
		else
		{
			// Overwrite any previous updates
			PendingNavBoundsUpdates[ExistingIdx] = UpdateRequest;
		}
	}
	else
	{
		PendingNavBoundsUpdates.Add(UpdateRequest);
	}
}

// 处理 NavBounds 更新请求队列：增 / 删 / 改尺寸。
// 每个请求会：
//   1) 同步 RegisteredNavBounds
//   2) 通知所有 NavData（如 ARecastNavMesh 会据此决定生成范围）
//   3) 触发 DirtyAll（全量重建）当有 Add/Remove 时
void UNavigationSystemV1::PerformNavigationBoundsUpdate(const TArray<FNavigationBoundsUpdateRequest>& UpdateRequests)
{
	// NOTE: we used to create missing nav data first, before updating nav bounds, 
	// but some nav data classes (like RecastNavMesh) may depend on the nav bounds
	// being already known at the moment of creation or serialization, so it makes more 
	// sense to update bounds first 

	// Create list of areas that needs to be updated
	TArray<FBox> UpdatedAreas;
	for (const FNavigationBoundsUpdateRequest& Request : UpdateRequests)
	{
		FSetElementId ExistingElementId = RegisteredNavBounds.FindId(Request.NavBounds);

		switch (Request.UpdateRequest)
		{
		case FNavigationBoundsUpdateRequest::Removed:
			{
				if (ExistingElementId.IsValidId())
				{
					UpdatedAreas.Add(RegisteredNavBounds[ExistingElementId].AreaBox);
					RegisteredNavBounds.Remove(ExistingElementId);
				}
			}
			break;

		case FNavigationBoundsUpdateRequest::Added:
		case FNavigationBoundsUpdateRequest::Updated:
			{
				if (ExistingElementId.IsValidId())
				{
					const FBox ExistingBox = RegisteredNavBounds[ExistingElementId].AreaBox;
					const bool bSameArea = (Request.NavBounds.AreaBox == ExistingBox);
					if (!bSameArea)
					{
						UpdatedAreas.Add(ExistingBox);
					}

					// always assign new bounds data, it may have different properties (like supported agents)
					RegisteredNavBounds[ExistingElementId] = Request.NavBounds;
				}
				else
				{
					AddNavigationBounds(Request.NavBounds);
				}

				UpdatedAreas.Add(Request.NavBounds.AreaBox);
			}

			break;
		}
	}

	if (UpdatedAreas.Num())
	{
		for (ANavigationData* NavData : NavDataSet)
		{
			if (NavData)
			{
				NavData->OnNavigationBoundsChanged();	
			}
		}
	}

	if (!IsNavigationBuildingLocked())
	{
		// Propagate to generators areas that needs to be updated
		AddDirtyAreas(UpdatedAreas, ENavigationDirtyFlag::All | ENavigationDirtyFlag::NavigationBounds, "Navigation bounds update");
	}

	// I'm not sure why we even do the following as part of this function
	// @TODO investigate if we can extract it into a separate function and
	// call it directly
	if (NavDataSet.Num() == 0)
	{
		//TODO: will hitch when user places first navigation volume in the world 

		if (NavDataRegistrationQueue.Num() > 0)
		{
			ProcessRegistrationCandidates();
		}

		if (NavDataSet.Num() == 0 && bAutoCreateNavigationData == true)
		{
			SpawnMissingNavigationData();
			ProcessRegistrationCandidates();
		}

		ConditionalPopulateNavOctree();
	}
}

void UNavigationSystemV1::AddNavigationBounds(const FNavigationBounds& NewBounds)
{
	RegisteredNavBounds.Add(NewBounds);
}

// 全量扫描 World 里的 ANavMeshBoundsVolume，重建 RegisteredNavBounds 集合。
// 通常在 NavSystem 初始化阶段使用；运行时增删走 OnNavigationBoundsAdded/Removed 增量路径。
void UNavigationSystemV1::GatherNavigationBounds()
{
	// Gather all available navigation bounds
	RegisteredNavBounds.Empty();
	for (TActorIterator<ANavMeshBoundsVolume> It(GetWorld()); It; ++It)
	{
		// Iterator can access actors with unregistered components which can result in invalid bounding boxes.
		// In this case we skip these actors and wait calls to OnNavigationBoundsAdded.
		const ANavMeshBoundsVolume* V = (*It);
		if (IsValid(V) && V->HasActorRegisteredAllComponents())
		{
			FNavigationBounds NavBounds;
			NavBounds.UniqueID = V->GetUniqueID();
			NavBounds.AreaBox = V->GetComponentsBoundingBox(true);
			NavBounds.Level = V->GetLevel();
			NavBounds.SupportedAgents = V->SupportedAgents;

			AddNavigationBounds(NavBounds);
		}
	}
}

// 收集所有 PlayerController 的 Pawn/Camera 位置作为"Invoker 种子点"。
// NavigationInvoker 模式下用于初始化时圈出哪些瓦片需要立即烘焙。
void UNavigationSystemV1::GetInvokerSeedLocations(const UWorld& InWorld, TArray<FVector, TInlineAllocator<32>>& OutSeedLocations)
{
	for (FConstPlayerControllerIterator PlayerIt = InWorld.GetPlayerControllerIterator(); PlayerIt; ++PlayerIt)
	{
		const APlayerController* PlayerController = PlayerIt->Get();
		if (PlayerController)
		{
			if (PlayerController->GetPawn())
			{
				OutSeedLocations.Add(PlayerController->GetPawn()->GetActorLocation());
			}
			else if (PlayerController->PlayerCameraManager)
			{
				OutSeedLocations.Add(PlayerController->PlayerCameraManager->GetCameraLocation());
			}
		}
	}
}

// 一次性全量构建：丢弃缓存的 NavigationDataChunks → SpawnMissingNavigationData →
// ProcessRegistrationCandidates → UpdateInvokers/DirtyTilesInBuildBounds → RebuildAll →
// 阻塞等待每个 NavData->EnsureBuildCompletion 完成。常用于编辑器 "Build Paths"、烘焙 commandlet。
void UNavigationSystemV1::Build()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UNavigationSystemV1::Build);
	
	UE_LOG(LogNavigationDataBuild, Display, TEXT("UNavigationSystemV1::Build started..."));

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogNavigation, Error, TEXT("Unable to build navigation due to missing World pointer"));
		return;
	}

	FNavigationSystem::DiscardNavigationDataChunks(*World);

	const bool bHasWork = IsThereAnywhereToBuildNavigation();
	const bool bLockedIgnoreEditor = (NavBuildingLockFlags & ~ENavigationBuildLock::NoUpdateInEditor) != 0;
	if (!bHasWork || bLockedIgnoreEditor)
	{
		return;
	}

	const double BuildStartTime = FPlatformTime::Seconds();

	if (bAutoCreateNavigationData == true
#if WITH_EDITOR
		|| FNavigationSystem::IsEditorRunMode(OperationMode)
#endif // WITH_EDITOR
		)
	{
		SpawnMissingNavigationData();
	}

	// make sure freshly created navigation instances are registered before we try to build them
	ProcessRegistrationCandidates();
	
	// update invokers in case we're not updating navmesh automatically, in which case
	// navigation generators wouldn't have up-to-date info.
	if (bGenerateNavigationOnlyAroundNavigationInvokers)
	{
		UpdateInvokers();
	}

	if (BuildBounds.IsValid)
	{
		// Prepare to build tiles overlapping the bounds
		DirtyTilesInBuildBounds();
	}

	// and now iterate through all registered and just start building them
	RebuildAll();

	// Block until build is finished
	for (ANavigationData* NavData : NavDataSet)
	{
		if (NavData)
		{
			NavData->EnsureBuildCompletion();
		}
	}

#if !UE_BUILD_SHIPPING
	// no longer report that navmesh needs to be rebuild
	DefaultDirtyAreasController.bDirtyAreasReportedWhileAccumulationLocked = false;
#endif // !UE_BUILD_SHIPPING

	UE_LOG(LogNavigationDataBuild, Display, TEXT("UNavigationSystemV1::Build total execution time: %.2fs"), float(FPlatformTime::Seconds() - BuildStartTime));
	UE_LOG(LogNavigation, Display, TEXT("UNavigationSystemV1::Build total execution time: %.5fs"), float(FPlatformTime::Seconds() - BuildStartTime));
}

// 取消所有 NavData 的当前构建任务（调用各自 Generator->CancelBuild）。
// 用于 World shift / NavSystem 重置 / 用户主动取消。
void UNavigationSystemV1::CancelBuild()
{
	for (ANavigationData* NavData : NavDataSet)
	{
		if (NavData)
		{
			if (NavData->GetGenerator())
			{
				NavData->GetGenerator()->CancelBuild();
			}
		}
	}
}

// 为每个 SupportedAgent 检查是否已有 NavData；没有则 CreateNavigationDataInstanceInLevel。
// bAutoCreateNavigationData + bSpawnNavDataInNavBoundsLevel 共同决定行为。
void UNavigationSystemV1::SpawnMissingNavigationData()
{
	const int32 AllSupportedAgentsCount = SupportedAgents.Num();
	check(AllSupportedAgentsCount >= 0);
	int32 ValidSupportedAgentsCount = 0;
	for (int32 AgentIndex = 0; AgentIndex < AllSupportedAgentsCount; ++AgentIndex)
	{
		if (SupportedAgentsMask.Contains(AgentIndex))
		{
			++ValidSupportedAgentsCount;
		}
	}
	
	// Bit array might be a bit of an overkill here, but this function will be called very rarely
	TBitArray<> AlreadyInstantiated;
	uint8 NumberFound = 0;

	// 1. check whether any of required navigation data has already been instantiated
	NumberFound = FillInstantiatedDataMask(AlreadyInstantiated);

	// 2. for any not already instantiated navigation data call creator functions
	if (NumberFound < ValidSupportedAgentsCount)
	{
		SpawnMissingNavigationDataInLevel(AlreadyInstantiated);
	}

	if (MainNavData == nullptr || MainNavData->IsPendingKillPending())
	{
		MainNavData = GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
	}
}

uint8 UNavigationSystemV1::FillInstantiatedDataMask(TBitArray<>& OutInstantiatedMask, ULevel* InLevel /*= nullptr*/)
{
	int32 AllSupportedAgentsCount = SupportedAgents.Num();
	OutInstantiatedMask.Init(false, AllSupportedAgentsCount);
	uint8 NumberFound = 0;

	auto SetMatchingAgentIndexFunc = [&](ANavigationData* Nav) {
		for (int32 AgentIndex = 0; AgentIndex < AllSupportedAgentsCount; ++AgentIndex)
		{
			if (OutInstantiatedMask[AgentIndex] == false
				&& Nav->GetClass() == SupportedAgents[AgentIndex].GetNavDataClass<ANavigationData>()
				&& Nav->DoesSupportAgent(SupportedAgents[AgentIndex]) == true)
			{
				OutInstantiatedMask[AgentIndex] = true;
				++NumberFound;
				break;
			}
		}
	};

	if (InLevel != nullptr)
	{
		for (AActor* Actor: InLevel->Actors)
		{
			if (ANavigationData* NavData = Cast<ANavigationData>(Actor))
			{
				SetMatchingAgentIndexFunc(NavData);
				if (NumberFound >= AllSupportedAgentsCount)
				{
					break;
				}
			}
		}
	} 
	else
	{
		UWorld* NavWorld = GetWorld();	
		for (TActorIterator<ANavigationData> It(NavWorld); It && NumberFound < AllSupportedAgentsCount; ++It)
		{
			ANavigationData* Nav = (*It);
			if (IsValid(Nav)
				// mz@todo the 'is level in' condition is temporary
				&& (Nav->GetTypedOuter<UWorld>() == NavWorld || NavWorld->GetLevels().Contains(Nav->GetLevel())))
			{
				// find out which one it is
				SetMatchingAgentIndexFunc(Nav);
			}
		}
	}

	return NumberFound;
}

// 在指定 Level 中为缺失的 SupportedAgent 生成 NavData。
// 跳过：CDO 不允许 spawn / 非 Editor 但 NavData 是 Static 模式 / Agent 不在 SupportedAgentsMask。
// 创建后调用 RequestRegistrationDeferred 入注册队列。
void UNavigationSystemV1::SpawnMissingNavigationDataInLevel(const TBitArray<>& InInstantiatedMask, ULevel* InLevel/*=nullptr*/)
{
	UWorld* NavWorld = GetWorld();

	ensure(SupportedAgents.Num() == InInstantiatedMask.Num());
	int32 AllSupportedAgentsCount = InInstantiatedMask.Num();

	for (int32 AgentIndex = 0; AgentIndex < AllSupportedAgentsCount; ++AgentIndex)
	{
		const FNavDataConfig& NavConfig = SupportedAgents[AgentIndex];
		if (InInstantiatedMask[AgentIndex] == false
			&& SupportedAgentsMask.Contains(AgentIndex)
			&& NavConfig.GetNavDataClass<ANavigationData>() != nullptr)
		{
			const ANavigationData* NavDataCDO = NavConfig.GetNavDataClass<ANavigationData>()->GetDefaultObject<ANavigationData>();
			if (NavDataCDO == nullptr || !NavDataCDO->CanSpawnOnRebuild())
			{
				continue;
			}

			if (NavWorld->WorldType != EWorldType::Editor && NavDataCDO->GetRuntimeGenerationMode() == ERuntimeGenerationType::Static)
			{
				// if we're not in the editor, and specified navigation class is configured 
				// to be static, then we don't want to create an instance					
				UE_LOG(LogNavigation, Log, TEXT("Not spawning navigation data for %s since indicated NavigationData type is not configured for dynamic generation")
					, *NavConfig.Name.ToString());
				continue;
			}

			ANavigationData* Instance = CreateNavigationDataInstanceInLevel(NavConfig, InLevel);
			if (Instance)
			{
				RequestRegistrationDeferred(*Instance);
			}
			else
			{
				UE_LOG(LogNavigation, Warning, TEXT("Was not able to create navigation data for SupportedAgent[%d]: %s"), AgentIndex, *NavConfig.Name.ToString());
			}
		}
	}
}

// 在指定关卡里 Spawn 一份 NavData（默认通常是 ARecastNavMesh）。
// SpawnLevel==null 且 bSpawnNavDataInNavBoundsLevel==true 时：
//   从 RegisteredNavBounds 里挑第一个 Volume 所在的 Level 作为生成层级。
// 用途：bAutoCreateNavigationData=true 且没有任何匹配 NavData 时自动补一份。
ANavigationData* UNavigationSystemV1::CreateNavigationDataInstanceInLevel(const FNavDataConfig& NavConfig, ULevel* SpawnLevel)
{
	UWorld* World = GetWorld();
	check(World);

	const int32 NavSupportedAgents = GetSupportedAgentIndex(NavConfig);

	// not creating new NavData instance if the agent it's representing is not supported
	// with the exception of AbstractNavData
	if (NavSupportedAgents == INDEX_NONE
		&& NavConfig.GetNavDataClass<AAbstractNavData>() == nullptr)
	{
		UE_LOG(LogNavigation, Warning, TEXT("Unable to create NavigationData instance for config \'%s\' as this agent is not supported by current NavigationSystem instance")
			, *NavConfig.GetDescription());
		return nullptr;
	}

	FActorSpawnParameters SpawnInfo;
	SpawnInfo.OverrideLevel = SpawnLevel;
	if (bSpawnNavDataInNavBoundsLevel && SpawnLevel == nullptr && RegisteredNavBounds.Num() > 0)
	{
		// pick the first valid level that supports these agents
		for (const FNavigationBounds& Bounds : RegisteredNavBounds)
		{
			if (Bounds.SupportedAgents.Contains(NavSupportedAgents) && Bounds.Level.IsValid())
			{
				SpawnInfo.OverrideLevel = Bounds.Level.Get();
				break;
			}
		}
	}
	if (SpawnInfo.OverrideLevel == nullptr)
	{
		SpawnInfo.OverrideLevel = World->PersistentLevel;
	}

	ANavigationData* Instance = World->SpawnActor<ANavigationData>(*NavConfig.GetNavDataClass<ANavigationData>(), SpawnInfo);

	if (Instance != nullptr)
	{
		Instance->SetConfig(NavConfig);
		if (NavConfig.Name != NAME_None)
		{
			FString StrName = FString::Printf(TEXT("%s-%s"), *(Instance->GetFName().GetPlainNameString()), *(NavConfig.Name.ToString()));
			// temporary solution to make sure we don't try to change name while there's already
			// an object with this name
			UObject* ExistingObject = StaticFindObject(/*Class=*/ nullptr, Instance->GetOuter(), *StrName, EFindObjectFlags::ExactClass);
			while (ExistingObject != nullptr)
			{
				ANavigationData* ExistingNavigationData = Cast<ANavigationData>(ExistingObject);
				if (ExistingNavigationData)
				{
					UnregisterNavData(ExistingNavigationData);
				}

				// Reset the existing object's name
				ExistingObject->Rename(nullptr, nullptr, REN_DontCreateRedirectors | REN_ForceGlobalUnique | REN_DoNotDirty | REN_NonTransactional);
				// see if there's another one, it does happen when undo/redoing 
				// nav instance deletion in the editor
				ExistingObject = StaticFindObject(/*Class=*/ nullptr, Instance->GetOuter(), *StrName, EFindObjectFlags::ExactClass);
			}

			// Set descriptive name
			Instance->Rename(*StrName, nullptr, REN_DoNotDirty);
#if WITH_EDITOR
			if (World->WorldType == EWorldType::Editor)
			{
				FString ActorLabel = StrName;
				if (Instance->IsPackageExternal())
				{
					// When using external package, don't rely on actor's name to generate a label as it contains a unique actor identifier which obfuscates the label
					ActorLabel = FString::Printf(TEXT("%s-%s"), *(Instance->GetClass()->GetFName().GetPlainNameString()), *(NavConfig.Name.ToString()));
				}
				
				constexpr bool bMarkDirty = false;
				Instance->SetActorLabel(ActorLabel, bMarkDirty);
			}
#endif // WITH_EDITOR
		}
	}

	return Instance;
}

// PIE 启动钩子（编辑器实例）：把 EditorWorld 标脏积累暂停 + 加 NoUpdateInPIE 锁，
// 避免 PIE 期间编辑器 World 持续重建浪费算力。
void UNavigationSystemV1::OnPIEStart()
{
	bIsPIEActive = true;
	// no updates for editor world while PIE is active
	const UWorld* MyWorld = GetWorld();
	if (MyWorld && !MyWorld->IsGameWorld())
	{
		bAsyncBuildPaused = true;
		AddNavigationBuildLock(ENavigationBuildLock::NoUpdateInPIE);
	}
}

// PIE 结束钩子：解 NoUpdateInPIE 锁；为避免编辑器 World 触发不必要的重建，
// 用 ELockRemovalRebuildAction::NoRebuild 跳过解锁后的 RebuildAll。
void UNavigationSystemV1::OnPIEEnd()
{
	bIsPIEActive = false;
	const UWorld* MyWorld = GetWorld();
	if (MyWorld && !MyWorld->IsGameWorld())
	{
		bAsyncBuildPaused = false;

		// There's no need to request navigation rebuilding when PIE ended.
		RemoveNavigationBuildLock(ENavigationBuildLock::NoUpdateInPIE, ELockRemovalRebuildAction::NoRebuild);
	}
}

// 给 NavBuildingLockFlags 上对应位（按 ENavigationBuildLock 标志）。
// 从 unlocked → locked 的边沿会通知 DirtyAreasController 进入累积模式。
void UNavigationSystemV1::AddNavigationBuildLock(uint8 Flags)
{
	const bool bWasLocked = IsNavigationBuildingLocked();

	NavBuildingLockFlags |= Flags;

	const bool bIsLocked = IsNavigationBuildingLocked();
	UE_LOG(LogNavigation, Verbose, TEXT("UNavigationSystemV1::AddNavigationBuildLock WasLocked=%s IsLocked=%s"), *LexToString(bWasLocked), *LexToString(bIsLocked));
	if (!bWasLocked && bIsLocked)
	{
		DefaultDirtyAreasController.OnNavigationBuildLocked();
	}
}

// 解某些构建锁；从 locked → unlocked 边沿时按 RebuildAction 决定是否立即 RebuildAll。
// 默认 Rebuild；NoRebuild 用于 OnPIEEnd 这种不需重建的场景。
void UNavigationSystemV1::RemoveNavigationBuildLock(uint8 Flags, const ELockRemovalRebuildAction RebuildAction /*= ELockRemovalRebuildAction::Rebuild*/)
{
	const bool bWasLocked = IsNavigationBuildingLocked();

	NavBuildingLockFlags &= ~Flags;

	const bool bIsLocked = IsNavigationBuildingLocked();
	UE_LOG(LogNavigation, Verbose, TEXT("UNavigationSystemV1::RemoveNavigationBuildLock WasLocked=%s IsLocked=%s"), *LexToString(bWasLocked), *LexToString(bIsLocked));
	if (bWasLocked && !bIsLocked)
	{
		DefaultDirtyAreasController.OnNavigationBuildUnlocked();

		const bool bRebuild = 
			(RebuildAction == ELockRemovalRebuildAction::RebuildIfNotInEditor && !FNavigationSystem::IsEditorRunMode(OperationMode)) || 
			(RebuildAction == ELockRemovalRebuildAction::Rebuild);
		
		if (bRebuild)
		{
			RebuildAll();
		}
	}
}

// Octree 锁：bLock=true 时拒绝任何 Add/Update/Remove 请求（用于关键阶段防并发改动）。
// 由 DefaultOctreeController 内部维护标志位。
void UNavigationSystemV1::SetNavigationOctreeLock(bool bLock)
{
	UE_LOG(LogNavigation, Verbose, TEXT("UNavigationSystemV1::SetNavigationOctreeLock IsLocked=%s"), *LexToString(bLock));

	DefaultOctreeController.SetNavigationOctreeLock(bLock);
}


// 全量重建所有 NavData。
// 典型调用：OnWorldInitDone 后第一次初始化、编辑器 Build 按钮、NavBounds 大规模变动。
// 内部：对每份 NavData 调 RebuildAll（真正走 FNavDataGenerator::RebuildAll）。
void UNavigationSystemV1::RebuildAll(bool bIsLoadTime)
{
	UE_LOG(LogNavigation, Verbose, TEXT("UNavigationSystemV1::RebuildAll"));

	const bool bIsInGame = GetWorld()->IsGameWorld();
	
	GatherNavigationBounds();

	// make sure that octree is up to date
	FNavigationDataHandler NavHandler(DefaultOctreeController, DefaultDirtyAreasController);
	NavHandler.ProcessPendingOctreeUpdates();
	
	PendingNavBoundsUpdates.Reset();

	DefaultDirtyAreasController.Reset();

	for (int32 NavDataIndex = 0; NavDataIndex < NavDataSet.Num(); ++NavDataIndex)
	{
		ANavigationData* NavData = NavDataSet[NavDataIndex];
				
		if (NavData && (!bIsLoadTime || NavData->NeedsRebuildOnLoad()) && (!bIsInGame || NavData->SupportsRuntimeGeneration()) && (BuildBounds.IsValid == 0))
		{
			UE_LOG(LogNavigationDataBuild, Display, TEXT("   RebuildAll building NavData:  %s."), *NavData->GetConfig().GetDescription());
			UE_LOG(LogNavigationDataBuild, Verbose, TEXT("   RebuildAll bIsLoadTime=%s, NavData->NeedsRebuildOnLoad()=%s, bIsInGame=%s, NavData->SupportsRuntimeGeneration()=%s, BuildBounds.IsValid=%s"),
				*LexToString(bIsLoadTime), *LexToString(NavData->NeedsRebuildOnLoad()), *LexToString(bIsInGame), *LexToString(NavData->SupportsRuntimeGeneration()), *LexToString(BuildBounds.IsValid));

#if	WITH_EDITOR
			NavData->SetIsBuildingOnLoad(bIsLoadTime);
#endif
			NavData->RebuildAll();
		}
	}
}

// 每帧把 DefaultDirtyAreasController 里的脏区域推给各 NavData 做增量重建。
// 频率由 DirtyAreasUpdateFreq 决定（通常每帧跑）。
// NavData::RebuildDirtyAreas 会把 Dirty 区域转为 Tile 坐标 → 加入 PendingDirtyTiles。
void UNavigationSystemV1::RebuildDirtyAreas(float DeltaSeconds)
{
	SCOPE_CYCLE_COUNTER(STAT_Navigation_TickMarkDirty);
	UWorld* World = GetWorld();
	const bool bForceRebuilding = (World != nullptr) && (World->IsGameWorld() == false);
	DefaultDirtyAreasController.Tick(DeltaSeconds, NavDataSet, bForceRebuilding);
}

bool UNavigationSystemV1::IsNavigationBuildInProgress()
{
	bool bRet = false;

	if (NavDataSet.Num() == 0)
	{
		// @todo this is wrong! Should not need to create a navigation data instance in a "getter" like function
		// update nav data. If none found this is the place to create one
		GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
	}
	
	for (int32 NavDataIndex = 0; NavDataIndex < NavDataSet.Num(); ++NavDataIndex)
	{
		ANavigationData* NavData = NavDataSet[NavDataIndex];
		if (NavData != nullptr && NavData->GetGenerator() != nullptr && NavData->GetGenerator()->IsBuildInProgressCheckDirty() == true)
		{
			bRet = true;
			break;
		}
	}

	return bRet;
}

// 单个 NavData 烘焙完成的回调。广播 OnNavigationGenerationFinishedDelegate；
// 编辑器下重置 bIsBuildingOnLoad 标记。供调试 / UI 显示进度用。
void UNavigationSystemV1::OnNavigationGenerationFinished(ANavigationData& NavData)
{
	OnNavigationGenerationFinishedDelegate.Broadcast(&NavData);

#if WITH_EDITOR
	if (GetWorld()->IsGameWorld() == false)
	{
		UE_LOG(LogNavigationDataBuild, Verbose, TEXT("Navigation data generation finished for %s (%s)."), *NavData.GetActorLabel(), *NavData.GetFullName());
	}

	// Reset bIsBuildingOnLoad
	NavData.SetIsBuildingOnLoad(false);
#endif //WITH_EDITOR
}

// 累计所有 NavData 待办瓦片任务数（含尚未派发的）。常用于 HUD/CSV stat。
int32 UNavigationSystemV1::GetNumRemainingBuildTasks() const
{
	int32 NumTasks = 0;
	
	for (ANavigationData* NavData : NavDataSet)
	{
		if (NavData && NavData->GetGenerator())
		{
			NumTasks+= NavData->GetGenerator()->GetNumRemaningBuildTasks();
		}
	}
	
	return NumTasks;
}

// 累计所有 NavData 当前正在跑的瓦片任务数（已派发到线程池）。
int32 UNavigationSystemV1::GetNumRunningBuildTasks() const 
{
	int32 NumTasks = 0;
	
	for (ANavigationData* NavData : NavDataSet)
	{
		if (NavData && NavData->GetGenerator())
		{
			NumTasks+= NavData->GetGenerator()->GetNumRunningBuildTasks();
		}
	}
	
	return NumTasks;
}

// 流式关卡加载完成回调：
//   1) AddLevelCollisionToOctree —— 把 Level 静态碰撞导入 Octree
//   2) 通知所有 NavData->OnStreamingLevelAdded（Recast 会按需拉取该 Level 内的 NavDataChunkActor）
//   3) 编辑器模式下补登记其内的 ANavigationData 实例
void UNavigationSystemV1::OnLevelAddedToWorld(ULevel* InLevel, UWorld* InWorld)
{
	if ((InWorld != GetWorld()) || (InLevel == nullptr))
	{
		return;
	}

	if ((IsNavigationSystemStatic() == false))
	{
		AddLevelCollisionToOctree(InLevel);
	}

	if (!InLevel->IsPersistentLevel())
	{
		for (ANavigationData* NavData : NavDataSet)
		{
			if (NavData)
			{
				NavData->OnStreamingLevelAdded(InLevel, InWorld);
			}
		}
	}

#if WITH_EDITOR
	if (FNavigationSystem::IsEditorRunMode(OperationMode))
	{
		// see if there are any unregistered yet valid nav data instances
		// In general we register navdata on its PostLoad, but in some cases
		// levels get removed from world and readded and in that case we might
		// miss registering them
		for (AActor* Actor : InLevel->Actors)
		{
			ANavigationData* NavData = Cast<ANavigationData>(Actor);
			if (NavData != nullptr && NavData->IsRegistered() == false)
			{
				RequestRegistrationDeferred(*NavData);
			}
		}
	}
	else
#endif // WITH_EDITOR
	if (OperationMode == FNavigationSystemRunMode::InvalidMode)
	{
		// While streaming multiple levels it is possible that NavigationData and NavMeshBoundsVolume from different levels gets
		// loaded in different order so we need to wait navigation system initialization to make sure everything is registered properly.
		// Otherwise the register may fail and discard the navigation data since navbounds are not registered.
		UE_LOG(LogNavigation, Log, TEXT("%hs won't process navigation data registration candidates until OperationMode is set. Waiting for OnWorldInitDone."), __FUNCTION__);
	}
	else if (NavDataRegistrationQueue.Num() > 0)
	{
		ProcessRegistrationCandidates();
	}
}

// 流式关卡卸载回调：
//   1) RemoveLevelCollisionFromOctree —— 从 Octree 移除该 Level 静态碰撞
//   2) 通知所有 NavData->OnStreamingLevelRemoved；其中 NavData 自己所属的 Level 被卸载时直接 UnregisterNavData
void UNavigationSystemV1::OnLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld)
{
	if ((InWorld == GetWorld()) && (InLevel != nullptr))
	{
		if (IsNavigationSystemStatic() == false)
		{
			RemoveLevelCollisionFromOctree(InLevel);
		}

		if (InLevel && !InLevel->IsPersistentLevel())
		{
			for (int32 DataIndex = NavDataSet.Num() - 1; DataIndex >= 0; --DataIndex)
			{		
				ANavigationData* NavData = NavDataSet[DataIndex];
				if (NavData)
				{
					if (NavData->GetLevel() != InLevel)
					{
						NavData->OnStreamingLevelRemoved(InLevel, InWorld);
					}
					else
					{
						// removing manually first so that UnregisterNavData won't mess with NavDataSet
						NavDataSet.RemoveAt(DataIndex, EAllowShrinking::No);
						UnregisterNavData(NavData);
					}
				}
			}
		}
	}
}

void UNavigationSystemV1::AddLevelToOctree(ULevel& Level)
{
	// We only need to add level collision (BSP)
	// Actors and components are handled by the navigation element repository.
	AddLevelCollisionToOctree(&Level);
}

// 把 Level 的 Model（BSP）几何登记进 Octree。Actor/Component 的登记由 Repository 路径完成。
void UNavigationSystemV1::AddLevelCollisionToOctree(ULevel* Level)
{
	if (Level)
	{
		FNavigationDataHandler(DefaultOctreeController, DefaultDirtyAreasController).AddLevelCollisionToOctree(*Level);
	}
}

// 与 AddLevelCollisionToOctree 配对：把 Level 的 Model 几何从 Octree 中移除。
void UNavigationSystemV1::RemoveLevelCollisionFromOctree(ULevel* Level)
{
	if (Level)
	{
		FNavigationDataHandler(DefaultOctreeController, DefaultDirtyAreasController).RemoveLevelCollisionFromOctree(*Level);
	}
}

// FCoreUObjectDelegates::PostLoadMapWithWorld 回调：地图 Load 完成后做兜底。
// 若没有可用 NavData 但 bAutoCreateNavigationData 且非 Static 模式，则创建一份默认 NavData。
void UNavigationSystemV1::OnPostLoadMap(UWorld* LoadedWorld)
{
	if (LoadedWorld != GetWorld())
	{
		return;
	}

	UE_LOG(LogNavigation, Verbose, TEXT("%hs (Package: %s)"), __FUNCTION__, *GetNameSafe(LoadedWorld->GetOuter()));

	// If map has been loaded and there are some navigation bounds volumes 
	// then create appropriate navigation structure.
	ANavigationData* NavData = GetDefaultNavDataInstance(FNavigationSystem::DontCreate);

	// Do this if there's currently no navigation
	if ( NavData == nullptr &&
		 bAutoCreateNavigationData &&
		 IsThereAnywhereToBuildNavigation() &&
		 (GetRuntimeGenerationType() != ERuntimeGenerationType::Static) )	// Prevent creating a static default nav instance out of the editor (GetRuntimeGenerationType() is always dynamic in editor).
	{
		NavData = GetDefaultNavDataInstance(FNavigationSystem::Create);
		UE_LOG(LogNavigation, Verbose, TEXT("%hs Created DefaultNavDataInstance %s"), __FUNCTION__, *GetNameSafe(NavData));
	}
}

#if WITH_EDITOR
// 编辑器：Actor 在视口中被拖动后回调。NavMeshBoundsVolume 走 OnNavigationBoundsUpdated；
// 其他 Actor 在已注册全部组件后整体 Refresh 进 Octree（含挂接子 Actor）。
void UNavigationSystemV1::OnActorMoved(AActor* Actor)
{
	if (Cast<ANavMeshBoundsVolume>(Actor))
	{
		OnNavigationBoundsUpdated((ANavMeshBoundsVolume*)Actor);
	}
	// We need to check this actor has registered all their components post spawn / load
	// before attempting to update the components in the nav octree.
	// Without this check we were getting an issue with UNavRelevantComponent::GetNavigationParent().
	else if (Actor && Actor->HasActorRegisteredAllComponents())
	{
		UpdateActorAndComponentsInNavOctree(*Actor, /*bUpdateAttachedActors=*/true);
	}
}
#endif // WITH_EDITOR

// NavigationDirtyEvent 全局事件回调：把外部传入的 Bounds 当作"全脏"区域加入累积队列。
// 触发方：游戏代码通过 UNavigationSystemV1::NavigationDirtyEvent.Broadcast 主动通知重建。
void UNavigationSystemV1::OnNavigationDirtied(const FBox& Bounds)
{
	AddDirtyArea(Bounds, ENavigationDirtyFlag::All, "OnNavigationDirtied");
}

void UNavigationSystemV1::OnReloadComplete(EReloadCompleteReason Reason)
{
	if (RequiresNavOctree() && DefaultOctreeController.NavOctree.IsValid() == false)
	{
		ConditionalPopulateNavOctree();

		if (bInitialBuildingLocked)
		{
			RemoveNavigationBuildLock(ENavigationBuildLock::InitialLock, ELockRemovalRebuildAction::RebuildIfNotInEditor);
		}
	}
}

// World 关闭/换图时的清理；在 OnBeginTearingDown / WorldDestroy 路径调用。顺序很关键：
//   1) 解绑所有引擎/编辑器 delegate
//   2) 反注册并 CleanUp 每个 NavData（让其完成正在跑的异步任务后释放 Octree 引用）
//   3) DestroyNavOctree 销毁 NavOctree 数据
//   4) SetCrowdManager(nullptr) 释放 CrowdManager
//   5) 清空 AgentToNavDataMap / MainNavData，必要时 Reset NavLink Unique Id
void UNavigationSystemV1::CleanUp(FNavigationSystem::ECleanupMode Mode)
{
	if (bCleanUpDone)
	{
		return;
	}

	UE_LOG(LogNavigation, Log, TEXT("UNavigationSystemV1::CleanUp"));

#if WITH_EDITOR
	if (GIsEditor && GEngine)
	{
		GEngine->OnActorMoved().RemoveAll(this);
	}
#endif // WITH_EDITOR

	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
	UNavigationSystemV1::NavigationDirtyEvent.RemoveAll(this);
	FWorldDelegates::LevelAddedToWorld.RemoveAll(this);
	FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
	FWorldDelegates::OnWorldBeginTearDown.RemoveAll(this);
#if !UE_BUILD_SHIPPING
	FCoreDelegates::OnGetOnScreenMessages.RemoveAll(this);
#endif // !UE_BUILD_SHIPPING

	FCoreUObjectDelegates::ReloadCompleteDelegate.Remove(ReloadCompleteDelegateHandle);

	// Unregister and cleanup navigation data before destroying their dependencies.
	// The order of operations here mirrors ANavigationData::UnregisterAndCleanUp(), 
	// minus it having to resolve this NavigationSystem.
	for (int32 Idx = NavDataSet.Num() - 1; Idx >= 0; --Idx)
	{
		ANavigationData* NavData = NavDataSet[Idx];
		if (NavData)
		{
			// Unregister the nav data
			if (NavData->IsRegistered())
			{
				UnregisterNavData(NavData);
			}

			// Clean up nav data before the cleaning up the rest of the system. This may block while the NavData waits on async 
			// tasks that it started, but this is safer than cleaning up navigation systems while those tasks are running, since
			// those tasks may access state we're about to destroy such as the NavOctree.
			NavData->CleanUp();
		}
	}

	DestroyNavOctree();

	SetCrowdManager(nullptr);

	if (NavDataSet.Num())
	{
		UE_LOG(LogNavigation, Error, TEXT("UNavigationSystemV1::CleanUp still has data in NavDataSet after unregister them all"));
		NavDataSet.Reset();
	}

	if (AgentToNavDataMap.Num())
	{
		UE_LOG(LogNavigation, Error, TEXT("UNavigationSystemV1::CleanUp still has agents mapped to navigation data after clean up"));
		AgentToNavDataMap.Reset();
	}
	
	MainNavData = nullptr;

	const UWorld* MyWorld = (Mode == FNavigationSystem::ECleanupMode::CleanupWithWorld) ? GetWorld() : nullptr;
	if (MyWorld)
	{
		if (bInitialSetupHasBeenPerformed)
		{
			UnregisterFromRepositoryDelegates();
		}

		// reset unique link Id for new map
		if (MyWorld->WorldType == EWorldType::Game || MyWorld->WorldType == EWorldType::Editor)
		{
			UE_LOG(LogNavLink, VeryVerbose, TEXT("Reset navlink id on cleanup."));
			PRAGMA_DISABLE_DEPRECATION_WARNINGS
			INavLinkCustomInterface::ResetUniqueId();
			PRAGMA_ENABLE_DEPRECATION_WARNINGS
		}
	}

	bCleanUpDone = true;
}

// 释放 NavOctree（DefaultOctreeController.Reset）。
// 注意必须在所有 NavData CleanUp 完成后调用，避免后台瓦片任务访问已释放的 Octree。
void UNavigationSystemV1::DestroyNavOctree()
{
	DefaultOctreeController.Reset();
}

bool UNavigationSystemV1::RequiresNavOctree() const
{
	UWorld* World = GetWorld();
	check(World);
	
	// We always require navoctree in editor worlds
	if (!World->IsGameWorld())
	{
		return true;
	}
		
	for (ANavigationData* NavData : NavDataSet)
	{
		if (NavData && NavData->SupportsRuntimeGeneration())
		{
			return true;
		}
	}
	
	return false;
}

ERuntimeGenerationType UNavigationSystemV1::GetRuntimeGenerationType() const
{
	UWorld* World = GetWorld();
	check(World);
	
	// We always use ERuntimeGenerationType::Dynamic in editor worlds
	if (!World->IsGameWorld())
	{
		return ERuntimeGenerationType::Dynamic;
	}
	
	ERuntimeGenerationType RuntimeGenerationType = ERuntimeGenerationType::Static;

	for (ANavigationData* NavData : NavDataSet)
	{
		if (NavData && NavData->GetRuntimeGenerationMode() > RuntimeGenerationType)
		{
			RuntimeGenerationType = NavData->GetRuntimeGenerationMode();
		}
	}
	
	return RuntimeGenerationType;
}

void UNavigationSystemV1::LogNavDataRegistrationResult(ERegistrationResult InResult)
{
	switch (InResult)
	{
	case UNavigationSystemV1::RegistrationError:
		UE_VLOG_UELOG(this, LogNavigation, Warning, TEXT("NavData RegistrationError, could not be registered."));
		break;
	case UNavigationSystemV1::RegistrationFailed_DataPendingKill:
		UE_VLOG_UELOG(this, LogNavigation, Warning, TEXT("NavData RegistrationFailed_DataPendingKill."));
		break;
	case UNavigationSystemV1::RegistrationFailed_AgentAlreadySupported:
		UE_VLOG_UELOG(this, LogNavigation, Warning, TEXT("NavData RegistrationFailed_AgentAlreadySupported, specified agent type already has its navmesh implemented."));
		break;
	case UNavigationSystemV1::RegistrationFailed_AgentNotValid:
		UE_VLOG_UELOG(this, LogNavigation, Warning, TEXT("NavData RegistrationFailed_AgentNotValid, NavData instance contains navmesh that doesn't support any of expected agent types."));
		break;
	case UNavigationSystemV1::RegistrationFailed_NotSuitable:
		UE_VLOG_UELOG(this, LogNavigation, Warning, TEXT("NavData RegistrationFailed_NotSuitable."));
		break;
	case UNavigationSystemV1::RegistrationSuccessful:
		UE_VLOG_UELOG(this, LogNavigation, Verbose, TEXT("NavData RegistrationSuccessful."));
		break;
	default:
		UE_VLOG_UELOG(this, LogNavigation, Warning, TEXT("Registration not successful default warning."));
		break;
	}
}

bool UNavigationSystemV1::IsAllowedToRebuild() const
{
	const UWorld* World = GetWorld();
	
	return World && (!World->IsGameWorld() || GetRuntimeGenerationType() == ERuntimeGenerationType::Dynamic);
}

// 当 bGenerateNavigationOnlyAroundNavigationInvokers 切换时回调：
//   - 同步 NavOctree 的 DataGatheringMode（懒加载/即时）
//   - 让所有 NavData 切到/退出 ActiveTiles 模式（仅生成 invoker 周围瓦片）
void UNavigationSystemV1::OnGenerateNavigationOnlyAroundNavigationInvokersChanged()
{
	if (DefaultOctreeController.NavOctree.IsValid())
	{
		DefaultOctreeController.NavOctree->SetDataGatheringMode(DataGatheringMode);
	}

	for (auto NavData : NavDataSet)
	{
		if (NavData)
		{
			NavData->RestrictBuildingToActiveTiles(bGenerateNavigationOnlyAroundNavigationInvokers);
		}
	}
}

//----------------------------------------------------------------------//
// Blueprint functions
//----------------------------------------------------------------------//
UNavigationSystemV1* UNavigationSystemV1::GetNavigationSystem(UObject* WorldContextObject)
{
	return GetCurrent(WorldContextObject);
}

bool UNavigationSystemV1::K2_ProjectPointToNavigation(UObject* WorldContextObject, const FVector& Point, FVector& ProjectedLocation, ANavigationData* NavData, TSubclassOf<UNavigationQueryFilter> FilterClass, const FVector QueryExtent)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	ProjectedLocation = Point;
	bool bResult = false;

	if (NavSys)
	{
		FNavLocation OutNavLocation;
		ANavigationData* UseNavData = NavData ? NavData : NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
		if (UseNavData)
		{
			bResult = NavSys->ProjectPointToNavigation(Point, OutNavLocation, QueryExtent, NavData
				, UNavigationQueryFilter::GetQueryFilter(*UseNavData, WorldContextObject, FilterClass));
			ProjectedLocation = OutNavLocation.Location;
		}
	}

	return bResult;
}

bool UNavigationSystemV1::K2_GetRandomReachablePointInRadius(UObject* WorldContextObject, const FVector& Origin, FVector& RandomLocation, float Radius, ANavigationData* NavData, TSubclassOf<UNavigationQueryFilter> FilterClass)
{
	FNavLocation RandomPoint(Origin);
	bool bResult = false;

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (NavSys)
	{
		ANavigationData* UseNavData = NavData ? NavData : NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
		if (UseNavData)
		{
			bResult = NavSys->GetRandomReachablePointInRadius(Origin, Radius, RandomPoint, UseNavData, UNavigationQueryFilter::GetQueryFilter(*UseNavData, WorldContextObject, FilterClass));
			RandomLocation = RandomPoint.Location;
		}
	}

	return bResult;
}

bool UNavigationSystemV1::K2_GetRandomLocationInNavigableRadius(UObject* WorldContextObject, const FVector& Origin, FVector& RandomLocation, float Radius, ANavigationData* NavData, TSubclassOf<UNavigationQueryFilter> FilterClass)
{
	FNavLocation RandomPoint(Origin);
	bool bResult = false;
	RandomLocation = Origin;

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (NavSys)
	{
		ANavigationData* UseNavData = NavData ? NavData : NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
		if (UseNavData)
		{
			if (NavSys->GetRandomPointInNavigableRadius(Origin, Radius, RandomPoint, UseNavData, UNavigationQueryFilter::GetQueryFilter(*UseNavData, WorldContextObject, FilterClass)))
			{
				bResult = true;
				RandomLocation = RandomPoint.Location;
			}
		}
	}

	return bResult;
}

ENavigationQueryResult::Type UNavigationSystemV1::GetPathCost(UObject* WorldContextObject, const FVector& PathStart, const FVector& PathEnd, double& OutPathCost, ANavigationData* NavData, TSubclassOf<UNavigationQueryFilter> FilterClass)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (NavSys)
	{
		ANavigationData* UseNavData = NavData ? NavData : NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
		if (UseNavData)
		{
			return NavSys->GetPathCost(PathStart, PathEnd, OutPathCost, UseNavData, UNavigationQueryFilter::GetQueryFilter(*UseNavData, WorldContextObject, FilterClass));
		}
	}

	return ENavigationQueryResult::Error;
}

ENavigationQueryResult::Type UNavigationSystemV1::GetPathLength(UObject* WorldContextObject, const FVector& PathStart, const FVector& PathEnd, double& OutPathLength, ANavigationData* NavData, TSubclassOf<UNavigationQueryFilter> FilterClass)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (NavSys)
	{
		ANavigationData* UseNavData = NavData ? NavData : NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
		if (UseNavData)
		{
			return NavSys->GetPathLength(PathStart, PathEnd, OutPathLength, UseNavData, UNavigationQueryFilter::GetQueryFilter(*UseNavData, WorldContextObject, FilterClass));
		}
	}

	return ENavigationQueryResult::Error;
}

bool UNavigationSystemV1::IsNavigationBeingBuilt(UObject* WorldContextObject)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	
	if (NavSys && !NavSys->IsNavigationBuildingPermanentlyLocked())
	{
		return NavSys->HasDirtyAreasQueued() || NavSys->IsNavigationBuildInProgress();
	}

	return false;
}

bool UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(UObject* WorldContextObject)
{
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	if (NavSys)
	{
		return NavSys->IsNavigationBuildingLocked() || NavSys->HasDirtyAreasQueued() || NavSys->IsNavigationBuildInProgress();
	}

	return false;
}

bool UNavigationSystemV1::K2_ReplaceAreaInOctreeData(const UObject* Object, TSubclassOf<UNavArea> OldArea, TSubclassOf<UNavArea> NewArea)
{
	SCOPE_CYCLE_COUNTER(STAT_NavOctreeBookkeeping);

	if (Repository == nullptr)
	{
		return false;
	}

	if (const FNavigationElementHandle Handle = Repository->GetNavigationElementHandleForUObject(Object))
	{
		return ReplaceAreaInOctreeData(Handle, OldArea, NewArea);
	}
	return false;
}

//----------------------------------------------------------------------//
// HACKS!!!
//----------------------------------------------------------------------//
bool UNavigationSystemV1::ShouldGeneratorRun(const FNavDataGenerator* Generator) const
{
	if (Generator != nullptr && (IsNavigationSystemStatic() == false))
	{
		for (int32 NavDataIndex = 0; NavDataIndex < NavDataSet.Num(); ++NavDataIndex)
		{
			ANavigationData* NavData = NavDataSet[NavDataIndex];
			if (NavData != nullptr && NavData->GetGenerator() == Generator)
			{
				return true;
			}
		}
	}

	return false;
}

bool UNavigationSystemV1::HandleCycleNavDrawnCommand( const TCHAR* Cmd, FOutputDevice& Ar )
{
	CycleNavigationDataDrawn();

	return true;
}

bool UNavigationSystemV1::HandleCountNavMemCommand()
{
	UE_LOG(LogNavigation, Warning, TEXT("Logging NavigationSystem memory usage:"));

	if (DefaultOctreeController.NavOctree.IsValid())
	{
		UE_LOG(LogNavigation, Warning, TEXT("NavOctree memory: %llu"), DefaultOctreeController.NavOctree->GetSizeBytes());
	}

	for (int32 NavDataIndex = 0; NavDataIndex < NavDataSet.Num(); ++NavDataIndex)
	{
		ANavigationData* NavData = NavDataSet[NavDataIndex];
		if (NavData != nullptr)
		{
			NavData->LogMemUsed();
		}
	}
	return true;
}

//----------------------------------------------------------------------//
// Commands
//----------------------------------------------------------------------//
bool FNavigationSystemExec::Exec_Runtime(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	UNavigationSystemV1*  NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(InWorld);

	if (NavSys && NavSys->NavDataSet.Num() > 0)
	{
		if (FParse::Command(&Cmd, TEXT("CYCLENAVDRAWN")))
		{
			NavSys->HandleCycleNavDrawnCommand( Cmd, Ar );
			// not returning true to enable all navigation systems to cycle their own data
			return false;
		}
		else if (FParse::Command(&Cmd, TEXT("CountNavMem")))
		{
			NavSys->HandleCountNavMemCommand();
			return false;
		}
		/** Builds the navigation mesh (or rebuilds it). **/
		else if (FParse::Command(&Cmd, TEXT("RebuildNavigation")))
		{
			NavSys->Build();
		}
		else if (FParse::Command(&Cmd, TEXT("RedrawNav")) || FParse::Command(&Cmd, TEXT("RedrawNavigation")))
		{
			for (ANavigationData* NavData : NavSys->NavDataSet)
			{
				if (NavData)
				{
					NavData->MarkComponentsRenderStateDirty();
				}
			}
		}
	}

	return false;
}

void UNavigationSystemV1::CycleNavigationDataDrawn()
{
	++CurrentlyDrawnNavDataIndex;
	if (CurrentlyDrawnNavDataIndex >= NavDataSet.Num())
	{
		CurrentlyDrawnNavDataIndex = INDEX_NONE;
	}

	for (int32 NavDataIndex = 0; NavDataIndex < NavDataSet.Num(); ++NavDataIndex)
	{
		ANavigationData* NavData = NavDataSet[NavDataIndex];
		if (NavData != nullptr)
		{
			const bool bNewEnabledDrawing = (CurrentlyDrawnNavDataIndex == INDEX_NONE) || (NavDataIndex == CurrentlyDrawnNavDataIndex);
			NavData->SetNavRenderingEnabled(bNewEnabledDrawing);
		}
	}
}

bool UNavigationSystemV1::IsNavigationDirty() const
{
	if (!IsThereAnywhereToBuildNavigation())
	{
		// Nowhere to build navigation so it can't be dirty.
		return false;
	}
	
#if !UE_BUILD_SHIPPING
	if (DefaultDirtyAreasController.HadDirtyAreasReportedWhileAccumulationLocked())
	{
		return true;
	}
#endif // !UE_BUILD_SHIPPING

	for (int32 NavDataIndex=0; NavDataIndex < NavDataSet.Num(); ++NavDataIndex)
	{
		if (NavDataSet[NavDataIndex] && NavDataSet[NavDataIndex]->NeedsRebuild())
		{
			return true;
		}
	}

	return false;
}

bool UNavigationSystemV1::CanRebuildDirtyNavigation() const
{
	const bool bIsInGame = GetWorld()->IsGameWorld();

	for (const ANavigationData* NavData : NavDataSet)
	{
		if (NavData)
		{
			const bool bIsDirty = NavData->NeedsRebuild();
			const bool bCanRebuild = !bIsInGame || NavData->SupportsRuntimeGeneration();

			if (bIsDirty && !bCanRebuild)
			{
				return false;
			}
		}
	}

	return true;
}

bool UNavigationSystemV1::DoesPathIntersectBox(const FNavigationPath* Path, const FBox& Box, uint32 StartingIndex, FVector* AgentExtent)
{
	return Path != nullptr && Path->DoesIntersectBox(Box, StartingIndex, nullptr, AgentExtent);
}

bool UNavigationSystemV1::DoesPathIntersectBox(const FNavigationPath* Path, const FBox& Box, const FVector& AgentLocation, uint32 StartingIndex, FVector* AgentExtent)
{
	return Path != nullptr && Path->DoesIntersectBox(Box, AgentLocation, StartingIndex, nullptr, AgentExtent);
}

// 把所有 Recast NavMesh 的 MaxSimultaneousTileGenerationJobsCount 改为传入值。
// 用于运行时动态限流（如低端机降配，或大批量加载时降低瞬时压力）。
void UNavigationSystemV1::SetMaxSimultaneousTileGenerationJobsCount(int32 MaxNumberOfJobs)
{
#if WITH_RECAST
	for (auto NavigationData : NavDataSet)
	{
		ARecastNavMesh* RecastNavMesh = Cast<ARecastNavMesh>(NavigationData);
		if (RecastNavMesh)
		{
			RecastNavMesh->SetMaxSimultaneousTileGenerationJobsCount(MaxNumberOfJobs);
		}
	}
#endif
}

void UNavigationSystemV1::ResetMaxSimultaneousTileGenerationJobsCount()
{
#if WITH_RECAST
	for (auto NavigationData : NavDataSet)
	{
		ARecastNavMesh* RecastNavMesh = Cast<ARecastNavMesh>(NavigationData);
		if (RecastNavMesh)
		{
			const ARecastNavMesh* CDO = RecastNavMesh->GetClass()->GetDefaultObject<ARecastNavMesh>();
			RecastNavMesh->SetMaxSimultaneousTileGenerationJobsCount(CDO->MaxSimultaneousTileGenerationJobsCount);
		}
	}
#endif
}

//----------------------------------------------------------------------//
// Active tiles
//----------------------------------------------------------------------//

// 静态版：找到 Actor 所在 World 的 NavigationSystem，转调成员版 RegisterInvoker。
// UNavigationInvokerComponent::Activate 就是走到这里。
void UNavigationSystemV1::RegisterNavigationInvoker(AActor& Invoker, float TileGenerationRadius, float TileRemovalRadius, const FNavAgentSelector& Agents, ENavigationInvokerPriority Priority)
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Invoker.GetWorld());
	if (NavSys)
	{
		NavSys->RegisterInvoker(Invoker, TileGenerationRadius, TileRemovalRadius, Agents, Priority);
	}
}

// 反注册 Actor 形式的 Invoker（静态入口，按 World 找到 NavSystem 后转发到成员 UnregisterInvoker）。
void UNavigationSystemV1::UnregisterNavigationInvoker(AActor& Invoker)
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Invoker.GetWorld());
	if (NavSys)
	{
		NavSys->UnregisterInvoker(Invoker);
	}
}

// 切换 NavOctree 几何采集模式（即时 / 懒加载）。运行时改动会同步到当前已存在的 Octree。
void UNavigationSystemV1::SetGeometryGatheringMode(ENavDataGatheringModeConfig NewMode)
{
	DataGatheringMode = NewMode;
	if (DefaultOctreeController.NavOctree.IsValid())
	{
		DefaultOctreeController.NavOctree->SetDataGatheringMode(DataGatheringMode);
	}
}

namespace UE::Navigation::Private
{
	void LogNavInvokerRegistration(const UNavigationSystemV1& NavSystem, const FNavigationInvoker& Data)
	{
		UE_SUPPRESS(LogNavInvokers, Log,
		{
			TStringBuilder<128> InvokerNavData;
			for (int32 NavDataIndex = 0; NavDataIndex < NavSystem.NavDataSet.Num(); NavDataIndex++)
			{
				const ANavigationData* NavData = NavSystem.NavDataSet[NavDataIndex].Get();
				if (NavData)
				{
					const int32 NavDataSupportedAgentIndex = NavSystem.GetSupportedAgentIndex(NavData);
					if (Data.SupportedAgents.Contains(NavDataSupportedAgentIndex))
					{
						InvokerNavData.Append(FString::Printf(TEXT("%s "), *NavData->GetName()));
					}
				}
			}

			const FString RegisterText = FString::Printf(TEXT("Register invoker r: %.0f, r area: %.0f m2, removal r: %.0f, priority: %s, (%s %s) "),
				Data.GenerationRadius, UE_PI*FMath::Square(Data.GenerationRadius/100.f), Data.RemovalRadius, *UEnum::GetDisplayValueAsText(Data.Priority).ToString(), *Data.GetName(), *InvokerNavData);
			UE_LOG(LogNavInvokers, Log, TEXT("%s"), *RegisterText);

			FVector InvokerLocation = FVector::ZeroVector;
			Data.GetLocation(InvokerLocation);
			UE_VLOG_CYLINDER(&NavSystem, LogNavInvokers, Log, InvokerLocation, InvokerLocation + FVector(0, 0, 20), Data.GenerationRadius, FColorList::LimeGreen, TEXT("%s"), *RegisterText);
			UE_VLOG_CYLINDER(&NavSystem, LogNavInvokers, Log, InvokerLocation, InvokerLocation + FVector(0, 0, 20), Data.RemovalRadius, FColorList::IndianRed, TEXT(""));
		});
	}
}

// 把 Actor 记入 Invokers Map（key=UObject，value=半径/Agents/Priority）。
// 触发条件未真正建 Tile —— 真正驱动在 UpdateInvokers + UpdateNavDataActiveTiles。
void UNavigationSystemV1::RegisterInvoker(AActor& Invoker, float TileGenerationRadius, float TileRemovalRadius, const FNavAgentSelector& Agents, ENavigationInvokerPriority InPriority)
{
	UE_CVLOG(bGenerateNavigationOnlyAroundNavigationInvokers == false, this, LogNavInvokers, Warning
		, TEXT("Trying to register %s as invoker, but NavigationSystem is not set up for invoker-centric generation. See GenerateNavigationOnlyAroundNavigationInvokers in NavigationSystem's properties")
		, *Invoker.GetName());

	TileGenerationRadius = FMath::Clamp(TileGenerationRadius, 0.f, BIG_NUMBER);
	TileRemovalRadius = FMath::Clamp(TileRemovalRadius, TileGenerationRadius, BIG_NUMBER);

	FNavigationInvoker& Data = Invokers.FindOrAdd(&Invoker);
	Data.Actor = &Invoker;
	Data.GenerationRadius = TileGenerationRadius;
	Data.RemovalRadius = TileRemovalRadius;
	Data.SupportedAgents = Agents;
	Data.SupportedAgents.MarkInitialized();
	Data.Priority = InPriority;

	UE::Navigation::Private::LogNavInvokerRegistration(*this, Data);
}

// Interface 形式的 Invoker 注册：和 Actor 版相比，UObject 自身不必是 Actor，
// 通过 INavigationInvokerInterface::GetNavigationInvokerLocation 取位置（如 Component / GameMode 自定义对象）。
void UNavigationSystemV1::RegisterInvoker(const TWeakInterfacePtr<INavigationInvokerInterface>& Invoker, float TileGenerationRadius, float TileRemovalRadius, const FNavAgentSelector& Agents, ENavigationInvokerPriority InPriority)
{
	UE_CVLOG(bGenerateNavigationOnlyAroundNavigationInvokers == false, this, LogNavInvokers, Warning
		, TEXT("Trying to register %s as invoker, but NavigationSystem is not set up for invoker-centric generation. See GenerateNavigationOnlyAroundNavigationInvokers in NavigationSystem's properties")
		, *GetNameSafe(Invoker.GetObject()));

	UObject* InvokerObject = Invoker.GetObject();
	if (ensure(InvokerObject != nullptr))
	{
		FNavigationInvoker& Data = Invokers.FindOrAdd(InvokerObject);
		Data.Object = Invoker;
		Data.GenerationRadius = TileGenerationRadius;
		Data.RemovalRadius = TileRemovalRadius;
		Data.SupportedAgents = Agents;
		Data.SupportedAgents.MarkInitialized();
		Data.Priority = InPriority;

		UE::Navigation::Private::LogNavInvokerRegistration(*this, Data);
	}
}

// Actor 版反注册：转 UnregisterInvoker_Internal。
void UNavigationSystemV1::UnregisterInvoker(AActor& Invoker)
{
	UnregisterInvoker_Internal(Invoker);
}

// Interface 版反注册：拿到 UObject* 后转 UnregisterInvoker_Internal。
void UNavigationSystemV1::UnregisterInvoker(const TWeakInterfacePtr<INavigationInvokerInterface>& Invoker)
{
	if (const UObject* InvokerObject = Invoker.GetObject())
	{
		UnregisterInvoker_Internal(*InvokerObject);
	}
}

// 真正从 Invokers Map 中移除条目。下一次 UpdateInvokers 时该位置不再贡献 Active Tiles。
void UNavigationSystemV1::UnregisterInvoker_Internal(const UObject& Invoker)
{
	UE_VLOG(this, LogNavInvokers, Log, TEXT("Removing %s from invokers list"), *Invoker.GetName());
	Invokers.Remove(&Invoker);
}

void UNavigationSystemV1::RegisterToRepositoryDelegates()
{
	if (Repository == nullptr)
	{
		return;
	}

	Repository->OnCustomNavLinkObjectRegistered.BindWeakLambda(this, [this](INavLinkCustomInterface& CustomLink)
		{
			RegisterCustomLink(CustomLink);
		});

	Repository->OnCustomNavLinkObjectUnregistered.BindWeakLambda(this, [this](INavLinkCustomInterface& CustomLink)
		{
			UnregisterCustomLink(CustomLink);
		});

	Repository->OnNavigationElementAddedDelegate.BindWeakLambda(this, [this](const TSharedRef<const FNavigationElement>& Element)
		{
			RegisterNavigationElementWithNavOctree(Element, FNavigationOctreeController::OctreeUpdate_Default);
		});

	Repository->OnNavigationElementRemovedDelegate.BindWeakLambda(this, [this](const TSharedRef<const FNavigationElement>& Element)
		{
			UnregisterNavigationElementWithOctree(Element, FNavigationOctreeController::OctreeUpdate_Default);
		});
}

void UNavigationSystemV1::UnregisterFromRepositoryDelegates() const
{
	if (Repository == nullptr)
	{
		return;
	}

	Repository->OnCustomNavLinkObjectRegistered = nullptr;
	Repository->OnCustomNavLinkObjectUnregistered = nullptr;
	Repository->OnNavigationElementAddedDelegate = nullptr;
	Repository->OnNavigationElementRemovedDelegate = nullptr;
}

// 扫描 Invokers Map 构造 InvokerLocations 快照。
// 过程：
//   1) 剔除失效 Actor/Interface（弱引用已空）
//   2) 取 Owner 位置（Actor 用 ActorLocation；Interface 用 GetNavigationInvokerLocation）
//   3) 若配置了 InvokersMaximumDistanceFromSeed + GetInvokerSeedLocations，裁掉离 Seed 太远的 Invoker
//   4) 按 Agent 位集展开，写入 InvokerLocations
// 产出结果供 UpdateNavDataActiveTiles 使用。
void UNavigationSystemV1::UpdateInvokers()
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_Navigation_UpdateInvokers);
	
	const UWorld* World = GetWorld();
	const double CurrentTime = World->GetTimeSeconds();
	if (CurrentTime >= NextInvokersUpdateTime)
	{
		InvokerLocations.Reset();
		InvokersSeedBounds.Reset();

		if (Invokers.Num() > 0)
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_NavSys_Clusterize);

			const bool bCheckMaximumDistanceFromSeeds = (InvokersMaximumDistanceFromSeed != -1) && World->IsGameWorld();
			TArray<FVector, TInlineAllocator<32>> SeedLocations;
			if (bCheckMaximumDistanceFromSeeds)
			{
				GetInvokerSeedLocations(*World, SeedLocations);

				// Fill seed bounds
				for (const FVector SeedLocation : SeedLocations)
				{
					InvokersSeedBounds.Emplace(
						FVector(SeedLocation.X-InvokersMaximumDistanceFromSeed, SeedLocation.Y-InvokersMaximumDistanceFromSeed, SeedLocation.Z-InvokersMaximumDistanceFromSeed),
						FVector(SeedLocation.X+InvokersMaximumDistanceFromSeed, SeedLocation.Y+InvokersMaximumDistanceFromSeed, SeedLocation.Z+InvokersMaximumDistanceFromSeed));
				}
			}

#if ENABLE_VISUAL_LOG
			const double StartTime = FPlatformTime::Seconds();
#endif // ENABLE_VISUAL_LOG

			InvokerLocations.Reserve(Invokers.Num());

			for (auto ItemIterator = Invokers.CreateIterator(); ItemIterator; ++ItemIterator)
			{
				FVector InvokerLocation;
				if (!ItemIterator->Value.GetLocation(InvokerLocation))
				{
					ItemIterator.RemoveCurrent();
					continue;
				}

				const float GenerationRadius = ItemIterator->Value.GenerationRadius;
				bool bKeep = !bCheckMaximumDistanceFromSeeds;

				double ClosestDistanceSq = DBL_MAX;
				if (bCheckMaximumDistanceFromSeeds)
				{
					const double CheckDistanceSq = FMath::Square(InvokersMaximumDistanceFromSeed + GenerationRadius);

					// Check if the invoker is close enough
					for (const FVector SeedLocation : SeedLocations)
					{
						const double InvokerDistanceToSeedSq = FVector::DistSquared(SeedLocation, InvokerLocation);
						if (InvokerDistanceToSeedSq <= CheckDistanceSq)
						{
							bKeep = true;
							break;
						}
						else
						{
							ClosestDistanceSq = FMath::Min(InvokerDistanceToSeedSq, ClosestDistanceSq);
						}
					}
				}

				if (bKeep)
				{
					InvokerLocations.Add(FNavigationInvokerRaw(InvokerLocation, GenerationRadius, ItemIterator->Value.RemovalRadius,
						ItemIterator->Value.SupportedAgents, ItemIterator->Value.Priority));
				}
				else
				{
					UE_LOG(LogNavInvokers, Verbose, TEXT("Invoker %s ignored because it's too far from any seed location. Closest seed at %.0f."),
						*ItemIterator->Value.GetName(), FMath::Sqrt(ClosestDistanceSq));
				}
			}

#if ENABLE_VISUAL_LOG
			const double CachingFinishTime = FPlatformTime::Seconds();
			UE_VLOG(this, LogNavInvokers, Log, TEXT("Caching time %fms"), (CachingFinishTime - StartTime) * 1000);

			for (const auto& InvokerData : InvokerLocations)
			{
				UE_VLOG_CYLINDER(this, LogNavInvokers, Log, InvokerData.Location, InvokerData.Location + FVector(0, 0, 20), InvokerData.RadiusMax, FColorList::Blue, TEXT(""));
				UE_VLOG_CYLINDER(this, LogNavInvokers, Log, InvokerData.Location, InvokerData.Location + FVector(0, 0, 20), InvokerData.RadiusMin, FColorList::CadetBlue, TEXT("Priority %u"), InvokerData.Priority);
			}
#endif // ENABLE_VISUAL_LOG
		}

		UpdateNavDataActiveTiles();

		// once per second
		NextInvokersUpdateTime = CurrentTime + ActiveTilesUpdateInterval;
	}

#if !UE_BUILD_SHIPPING
#if CSV_PROFILER_STATS
	if (FCsvProfiler::Get()->IsCapturing())
	{
		TArray<int32, TInlineAllocator<8>> InvokerCounts;
		InvokerCounts.InsertZeroed(0, NavDataSet.Num());
	
		for (int32 NavDataIndex = 0; NavDataIndex < NavDataSet.Num(); NavDataIndex++)
		{
			const ANavigationData* NavData = NavDataSet[NavDataIndex].Get();
			if (NavData)
			{
				const int32 NavDataSupportedAgentIndex = GetSupportedAgentIndex(NavData);	

				for (auto ItemIterator = InvokerLocations.CreateIterator(); ItemIterator; ++ItemIterator)
				{
					const FNavAgentSelector& InvokerSupportedAgents = ItemIterator->SupportedAgents;
					if (InvokerSupportedAgents.Contains(NavDataSupportedAgentIndex))
					{
						InvokerCounts[NavDataIndex]++;
					}
				}

				const FString StatName = FString::Printf(TEXT("InvokerCount_%s"), *NavData->GetName()); 
				FCsvProfiler::RecordCustomStat(*StatName, CSV_CATEGORY_INDEX(NavInvokers), InvokerCounts[NavDataIndex], ECsvCustomStatOp::Set);
			}

			FCsvProfiler::RecordCustomStat(TEXT("InvokersFarAway"), CSV_CATEGORY_INDEX(NavInvokers), Invokers.Num() - InvokerLocations.Num(), ECsvCustomStatOp::Set);
		}		
	}
#endif // CSV_PROFILER_STATS
#endif // !UE_BUILD_SHIPPING
}

#if !UE_BUILD_SHIPPING
void UNavigationSystemV1::DebugLogInvokers(FOutputDevice& OutputDevice) const
{
	OutputDevice.Logf(ELogVerbosity::Log, TEXT("Logging %d Invokers:"), Invokers.Num());
	for (auto It = Invokers.CreateConstIterator(); It; ++It)
	{
		const FNavigationInvoker& Invoker = It.Value();
		OutputDevice.Logf(ELogVerbosity::Log, TEXT("- %s: Radius[Generation:%s Removal:%s] Agents:%d Priority:%s"),
			*GetNameSafe(It.Key()),
			*FString::SanitizeFloat(Invoker.GenerationRadius),
			*FString::SanitizeFloat(Invoker.RemovalRadius),
			Invoker.SupportedAgents.GetAgentBits(),
			*UEnum::GetValueAsString(Invoker.Priority));
	}
}
#endif // !UE_BUILD_SHIPPING

// 把 InvokerLocations 推给每份 NavData —— ARecastNavMesh 用它算活跃 Tile 集并触发重建/丢弃。
void UNavigationSystemV1::UpdateNavDataActiveTiles()
{
#if WITH_RECAST
	const double UpdateStartTime = FPlatformTime::Seconds();
	for (TActorIterator<ARecastNavMesh> It(GetWorld()); It; ++It)
	{
		It->UpdateActiveTiles(InvokerLocations);
	}
	const double UpdateEndTime = FPlatformTime::Seconds();
	UE_VLOG(this, LogNavInvokers, Log, TEXT("Marking tiles to update %fms (%d invokers)"), (UpdateEndTime - UpdateStartTime) * 1000, InvokerLocations.Num());
#endif
}

// 当 BuildBounds 变动时：把所有在 BuildBounds 内的 Tile 标脏，强制重建。
// 配合 SetBuildBounds / Invoker 漂移 使用。
void UNavigationSystemV1::DirtyTilesInBuildBounds()
{
#if WITH_RECAST
	UE_VLOG(this, LogNavigation, Log, TEXT("SetupTilesFromBuildBounds"));
	for (TActorIterator<ARecastNavMesh> It(GetWorld()); It; ++It)
	{
		It->DirtyTilesInBounds(BuildBounds);
	}
#endif // WITH_RECAST
}

// Actor 版蓝图入口（默认 Agents/优先级），转 RegisterInvoker。
void UNavigationSystemV1::RegisterNavigationInvoker(AActor* Invoker, float TileGenerationRadius, float TileRemovalRadius)
{
	if (Invoker != nullptr)
	{
		// The FNavAgentSelector class is not yet exposed in BP so we use the default value to specify that we want to generate the navmesh for all agents
		RegisterInvoker(*Invoker, TileGenerationRadius, TileRemovalRadius, FNavAgentSelector(), ENavigationInvokerPriority::Default);
	}
}

// Actor 版蓝图入口反注册：转 UnregisterInvoker。
void UNavigationSystemV1::UnregisterNavigationInvoker(AActor* Invoker)
{
	if (Invoker != nullptr)
	{
		UnregisterInvoker(*Invoker);
	}
}

// Deprecated
bool UNavigationSystemV1::K2_GetRandomPointInNavigableRadius(UObject* WorldContextObject, const FVector& Origin, FVector& RandomLocation, float Radius, ANavigationData* NavData, TSubclassOf<UNavigationQueryFilter> FilterClass)
{
	return K2_GetRandomLocationInNavigableRadius(WorldContextObject, Origin, RandomLocation, Radius, NavData, FilterClass);
}

// 编辑器调试：确保 MainNavData 上挂着导航渲染组件，并按 bShow 控制可见性。
// "show navigation" 控制台命令会用到。
void UNavigationSystemV1::VerifyNavigationRenderingComponents(const bool bShow)
{
	// make sure nav mesh has a rendering component
	ANavigationData* const NavData = GetDefaultNavDataInstance(FNavigationSystem::DontCreate);

	if (NavData && NavData->RenderingComp == nullptr)
	{
		NavData->RenderingComp = NavData->ConstructRenderingComponent();
		if (NavData->RenderingComp)
		{
			NavData->RenderingComp->SetVisibility(bShow);
			NavData->RenderingComp->RegisterComponent();
		}
	}

	if (NavData == nullptr)
	{
		UE_LOG(LogNavigation, Warning, TEXT("No NavData found when calling UNavigationSystemV1::VerifyNavigationRenderingComponents()"));
	}
}

#if !UE_BUILD_SHIPPING
void UNavigationSystemV1::GetOnScreenMessages(TMultiMap<FCoreDelegates::EOnScreenMessageSeverity, FText>& OutMessages)
{
	// check navmesh
#if WITH_EDITOR
	const bool bIsNavigationAutoUpdateEnabled = GetIsAutoUpdateEnabled();
#else
	const bool bIsNavigationAutoUpdateEnabled = true;
#endif

	// Don't display "navmesh needs to be rebuilt" on-screen editor message in partitioned world. 
	// It's not meaningful since loading and unloading parts of the world triggers it.
	if (!UWorld::IsPartitionedWorld(GetWorld())
		&& IsNavigationDirty()
		&& ((FNavigationSystem::IsEditorRunMode(OperationMode) && !bIsNavigationAutoUpdateEnabled) || !SupportsNavigationGeneration() || !CanRebuildDirtyNavigation()))
	{
		OutMessages.Add(FCoreDelegates::EOnScreenMessageSeverity::Error
			, LOCTEXT("NAVMESHERROR", "NAVMESH NEEDS TO BE REBUILT"));
	}
}
#endif // !UE_BUILD_SHIPPING

INavigationDataInterface* UNavigationSystemV1::GetNavDataForActor(const AActor& Actor)
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Actor.GetWorld());
	ANavigationData* NavData = nullptr;
	const INavAgentInterface* AsNavAgent = CastChecked<INavAgentInterface>(&Actor);
	if (AsNavAgent)
	{
		const FNavAgentProperties& AgentProps = AsNavAgent->GetNavAgentPropertiesRef();
		NavData = NavSys->GetNavDataForProps(AgentProps, AsNavAgent->GetNavAgentLocation());
	}
	if (NavData == nullptr)
	{
		NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
	}

	return NavData;
}

// 收集所有匹配 NavData Agent 且 Level 命中的 NavBounds 的 AreaBox 到 OutBounds。
// InLevel=null 时不限制 Level。返回新增的 Bounds 数量。
int UNavigationSystemV1::GetNavigationBoundsForNavData(const ANavigationData& NavData, TArray<FBox>& OutBounds, ULevel* InLevel) const
{
	const int InitialBoundsCount = OutBounds.Num();
	OutBounds.Reserve(InitialBoundsCount + RegisteredNavBounds.Num());
	const int32 AgentIndex = GetSupportedAgentIndex(&NavData);

	if (AgentIndex != INDEX_NONE)
	{
		for (const FNavigationBounds& NavigationBounds : RegisteredNavBounds)
		{
			if ((InLevel == nullptr || NavigationBounds.Level == InLevel)
				&& NavigationBounds.SupportedAgents.Contains(AgentIndex))
			{
				OutBounds.Add(NavigationBounds.AreaBox);
			}
		}
	}

	return OutBounds.Num() - InitialBoundsCount;
}

const FNavDataConfig& UNavigationSystemV1::GetDefaultSupportedAgent()
{
	static const FNavDataConfig DefaultAgent;
	const UNavigationSystemV1* NavSysCDO = GetDefault<UNavigationSystemV1>();
	check(NavSysCDO);
	return NavSysCDO->SupportedAgents.Num() > 0
		? NavSysCDO->GetDefaultSupportedAgentConfig()
		: DefaultAgent;
}

const FNavDataConfig& UNavigationSystemV1::GetBiggestSupportedAgent(const UWorld* World) 
{
	const UNavigationSystemV1* NavSys = nullptr;
	if (World != nullptr)
	{
		NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);		
	}

	if (NavSys == nullptr)
	{
		// If no world is available, use the CDO.
		NavSys = GetDefault<UNavigationSystemV1>();
	}
	check(NavSys);

	if (NavSys->GetSupportedAgents().IsEmpty())
	{
		static const FNavDataConfig DefaultAgent;
		return DefaultAgent;
	}

	const FNavDataConfig* BiggestAgent = nullptr;
	for (const FNavDataConfig& Config : NavSys->GetSupportedAgents())
	{
		if (BiggestAgent == nullptr || Config.AgentRadius > BiggestAgent->AgentRadius)
		{
			BiggestAgent = &Config; 
		}
	}

	return *BiggestAgent;
}

#if WITH_EDITOR
// World Partition 烘焙：返回所有 NavData 中"导航数据 Builder 需要的最大 Cell 重叠距离"。
// 用于 World Partition Builder 决定数据需要在邻接 Cell 之间冗余多少范围。
double UNavigationSystemV1::GetWorldPartitionNavigationDataBuilderOverlap(const UWorld& World) 
{
	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);
	if (NavSys == nullptr)
	{
		// If no world is available, use the CDO.
		NavSys = GetDefault<UNavigationSystemV1>();
	}
	check(NavSys);

	double MaxOverlap = 0;
	for (const ANavigationData* NavData : NavSys->NavDataSet)
	{
		if (NavData)
		{
			MaxOverlap = FMath::Max(MaxOverlap, NavData->GetWorldPartitionNavigationDataBuilderOverlap());
		}
	}

	return MaxOverlap;
}
#endif //WITH_EDITOR

const FNavDataConfig& UNavigationSystemV1::GetDefaultSupportedAgentConfig() const 
{ 
	static const FNavDataConfig DefaultAgent;

	int32 FirstValidIndex = INDEX_NONE;
	for (int32 AgentIndex = 0; AgentIndex < SupportedAgents.Num(); ++AgentIndex)
	{
		if (SupportedAgentsMask.Contains(AgentIndex))
		{
			if ((DefaultAgentName == NAME_None || SupportedAgents[AgentIndex].Name == DefaultAgentName))
			{
				return SupportedAgents[AgentIndex];
			}
			FirstValidIndex = (FirstValidIndex == INDEX_NONE) ? AgentIndex : FirstValidIndex;			
		}
	}

	// if not found, get the first one allowed
	return FirstValidIndex != INDEX_NONE ? SupportedAgents[FirstValidIndex] : DefaultAgent;;
}

// 运行时替换 SupportedAgents。
// 典型使用者：ANavSystemConfigOverride.AppendConfig → NavigationSystemConfig 覆盖 Agent 列表。
// 会 UnregisterUnusedNavData + 触发必要的重建。
void UNavigationSystemV1::OverrideSupportedAgents(const TArray<FNavDataConfig>& NewSupportedAgents)
{
	UE_CLOG(bWorldInitDone, LogNavigation, Warning, TEXT("Trying to override NavigationSystem\'s SupportedAgents past the World\'s initialization"));

	SupportedAgentsMask.Empty();

	// reset the SupportedAgents 
	const UNavigationSystemV1* NavSysCDO = GetClass()->GetDefaultObject<UNavigationSystemV1>();
	SupportedAgents = NavSysCDO->SupportedAgents;

	for (const FNavDataConfig& Agent : NewSupportedAgents)
	{
		for(int32 AgentIndex = 0; AgentIndex < SupportedAgents.Num(); AgentIndex++)
		{
			if (SupportedAgents[AgentIndex].IsEquivalent(Agent))
			{
				SupportedAgentsMask.Set(AgentIndex);
				break;
			}
		}
	}

	SupportedAgentsMask.MarkInitialized();

	ApplySupportedAgentsFilter();
}

// 用 SupportedAgentsMask 过滤 SupportedAgents：被 mask 排除掉的 Agent 调用 Invalidate 清空 NavDataClass。
// 不会真正删除条目，仅让其后续匹配/创建时被 GetSupportedAgentIndex 跳过。
void UNavigationSystemV1::ApplySupportedAgentsFilter()
{
	// reset the SupportedAgents 
	const UNavigationSystemV1* NavSysCDO = GetClass()->GetDefaultObject<UNavigationSystemV1>();
	SupportedAgents = NavSysCDO->SupportedAgents;
	// make sure there's at least one supported navigation agent size
	if (SupportedAgents.Num() == 0)
	{
		SupportedAgents.Add(UE::Navigation::Private::GetFallbackNavDataConfig());
	}

	// make all SupportedAgents filtered out by SupportedAgentsMask invalid by
	// clearing out their NavDataClass
	for (int32 AgentIndex = 0; AgentIndex < SupportedAgents.Num(); AgentIndex++)
	{
		if (SupportedAgentsMask.Contains(AgentIndex) == false)
		{
			SupportedAgents[AgentIndex].Invalidate();
		}
	}
}

// 把 SupportedAgentsMask 中已被禁用的 Agent 对应的 NavData 反注册掉。
// 通常紧跟 SetSupportedAgentsMask 调用，保证不留下"无人使用"的 NavData。
void UNavigationSystemV1::UnregisterUnusedNavData()
{
	for (int32 AgentIndex = 0; AgentIndex < SupportedAgents.Num(); AgentIndex++)
	{
		if (SupportedAgentsMask.Contains(AgentIndex) == false)
		{
			// if we already have navdata for this agent we need to remove it
			ANavigationData* NavData = GetNavDataForAgentName(SupportedAgents[AgentIndex].Name);
			if (NavData)
			{
				UnregisterNavData(NavData);
			}
		}
	}
}

// 设置 Agent 选择掩码并立即 ApplySupportedAgentsFilter。
// 由 NavigationSystemConfig::Configure / NavSystemConfigOverride 调用。
void UNavigationSystemV1::SetSupportedAgentsMask(const FNavAgentSelector& InSupportedAgentsMask)
{
	SupportedAgentsMask = InSupportedAgentsMask;
	ApplySupportedAgentsFilter();
}

// 把 UNavigationSystemConfig 的 DefaultAgentName + SupportedAgentsMask 应用到本实例。
// DefaultAgentName 为空时挑第一个 valid Agent 作为缺省。World 初始化阶段调用。
void UNavigationSystemV1::Configure(const UNavigationSystemConfig& Config)
{
	if (Config.DefaultAgentName != NAME_None)
	{
		DefaultAgentName = Config.DefaultAgentName;
	}
	SetSupportedAgentsMask(Config.SupportedAgentsMask);

	if (DefaultAgentName == NAME_None)
	{
		if (SupportedAgents.Num() == 1)
		{
			DefaultAgentName = SupportedAgents[0].Name;
		}
		else // pick the first available one
		{
			for (int32 AgentIndex = 0; AgentIndex < SupportedAgents.Num(); ++AgentIndex)
			{
				if (SupportedAgents[AgentIndex].IsValid())
				{
					DefaultAgentName = SupportedAgents[AgentIndex].Name;
					break;
				}
			}
		}
	}
}

// 与 Configure 不同：仅"追加"NewConfig 中尚未启用的 Agent 位（不会移除已启用的）。
// ANavSystemConfigOverride 用此来在子关卡里增加额外 Agent 支持。
void UNavigationSystemV1::AppendConfig(const UNavigationSystemConfig& NewConfig)
{
	if (NewConfig.SupportedAgentsMask.IsSame(SupportedAgentsMask) == false)
	{
		bool bAgentsAdded = false;
		for (int AgentIndex = 0; AgentIndex < SupportedAgents.Num(); ++AgentIndex)
		{
			if (NewConfig.SupportedAgentsMask.Contains(AgentIndex) == true
				&& SupportedAgentsMask.Contains(AgentIndex) == false)
			{
				SupportedAgentsMask.Set(AgentIndex);
				bAgentsAdded = true;
			}
		}

		if (bAgentsAdded)
		{
			ApplySupportedAgentsFilter();
			// @todo consider updating the octree, it might be missing data for the new agent(s)
		}

		if (DefaultAgentName == NAME_None)
		{
			DefaultAgentName = NewConfig.DefaultAgentName;
		}
	}
}

//----------------------------------------------------------------------//
// UNavigationSystemModuleConfig
//----------------------------------------------------------------------//
UNavigationSystemModuleConfig::UNavigationSystemModuleConfig(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UNavigationSystemModuleConfig::PostInitProperties()
{
	Super::PostInitProperties();

	const UNavigationSystemV1* NavSysCDO = GetDefault<UNavigationSystemV1>();
	if (NavSysCDO)
	{
		UpdateWithNavSysCDO(*NavSysCDO);
	}
}

void UNavigationSystemModuleConfig::UpdateWithNavSysCDO(const UNavigationSystemV1& NavSysCDO)
{
	UClass* MyClass = NavigationSystemClass.ResolveClass();
	if (MyClass != nullptr && MyClass->IsChildOf(NavSysCDO.GetClass()))
	{
		// note that we're not longer copying bStrictlyStatic due to UE-91171
		// Copying NavSysCDO.bStaticRuntimeNavigation resulted in copying 'true' 
		// between unrelated maps
		bCreateOnClient = NavSysCDO.bAllowClientSideNavigation;
		bAutoSpawnMissingNavData = NavSysCDO.bAutoCreateNavigationData;
		bSpawnNavDataInNavBoundsLevel = NavSysCDO.bSpawnNavDataInNavBoundsLevel;
	}
}

// 模块 Config 的主入口：World 创建时调用此函数构造 UNavigationSystemV1 实例。
// - 若 bStrictlyStatic=true：走 ConfigureAsStatic 关掉运行时重建逻辑
// - 按 SupportedAgents / bCreateOnClient / bAutoSpawnMissingNavData / bSpawnNavDataInNavBoundsLevel 配置新实例
UNavigationSystemBase* UNavigationSystemModuleConfig::CreateAndConfigureNavigationSystem(UWorld& World) const
{
	// This should be handled by ShouldCreateNavigationSystemInstance
	// called from the base class below but this is an early out.
	if (bCreateOnClient == false && World.GetNetMode() == NM_Client)
	{
		return nullptr;
	}

	UNavigationSystemBase* NewNavSys = Super::CreateAndConfigureNavigationSystem(World);
	UNavigationSystemV1* NavSysInstance = Cast<UNavigationSystemV1>(NewNavSys);
	UE_CLOG(NavSysInstance == nullptr && NewNavSys != nullptr, LogNavigation, Error
		, TEXT("Unable to spawn navigation system instance of class %s - unable to cast to UNavigationSystemV1")
		, *NavigationSystemClass.GetAssetName()
	);
	
	if (NavSysInstance)
	{
		NavSysInstance->bAutoCreateNavigationData = bAutoSpawnMissingNavData;
		NavSysInstance->bSpawnNavDataInNavBoundsLevel = bSpawnNavDataInNavBoundsLevel;
		NavSysInstance->ConfigureAsStatic(bStrictlyStatic);
	}

	return NavSysInstance;
}

#if WITH_EDITOR
void UNavigationSystemModuleConfig::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName NAME_NavigationSystemClass = GET_MEMBER_NAME_CHECKED(UNavigationSystemConfig, NavigationSystemClass);

	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property)
	{
		FName PropName = PropertyChangedEvent.Property->GetFName();
		if (PropName == NAME_NavigationSystemClass)
		{
			if (NavigationSystemClass.IsValid() == false)
			{
				NavigationSystemClass = *GEngine->NavigationSystemClass;
			}
			else
			{
				NavigationSystemClass.TryLoad();
				TSubclassOf<UNavigationSystemBase> NavSysClass = NavigationSystemClass.ResolveClass();
				const UNavigationSystemV1* NavSysCDO = *NavSysClass
					? NavSysClass->GetDefaultObject<UNavigationSystemV1>()
					: (UNavigationSystemV1*)nullptr;
				if (NavSysCDO)
				{
					UpdateWithNavSysCDO(*NavSysCDO);
				}
			}
		}
	}
}
#endif // WITH_EDITOR

//----------------------------------------------------------------------//
// Deprecated methods
//----------------------------------------------------------------------//
PRAGMA_DISABLE_DEPRECATION_WARNINGS

// Deprecated
uint32 UNavigationSystemV1::HashObject(const UObject& Object)
{
	return FNavigationOctree::HashObject(Object);
}

// Deprecated
const FOctreeElementId2* UNavigationSystemV1::GetObjectsNavOctreeId(const UObject& Object) const
{
	return GetNavOctreeIdForElement(FNavigationElementHandle(&Object));
}

// Deprecated
bool UNavigationSystemV1::HasPendingObjectNavOctreeId(UObject* Object) const
{
	return HasPendingUpdateForElement(FNavigationElementHandle(Object));
}

PRAGMA_ENABLE_DEPRECATION_WARNINGS

#undef LOCTEXT_NAMESPACE
