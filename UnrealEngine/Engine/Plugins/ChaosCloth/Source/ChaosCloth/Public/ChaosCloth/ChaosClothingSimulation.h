// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClothingSimulation.h"
#include "ClothingAsset.h"
#include "ClothCollisionData.h"
#include "ChaosCloth/ChaosClothConfig.h"
#include "ChaosCloth/ChaosClothVisualization.h"
#include "Templates/Atomic.h"
#include "Templates/UniquePtr.h"

class USkeletalMeshComponent;
class FClothingSimulationContextCommon;

namespace Chaos
{
	class FTriangleMesh;
	class FClothingSimulationSolver;
	class FClothingSimulationMesh;
	class FClothingSimulationCloth;
	class FClothingSimulationCollider;
	class FClothingSimulationConfig;
	class FSkeletalMeshCacheAdapter;

	typedef FClothingSimulationContextCommon FClothingSimulationContext;

	class UE_DEPRECATED(5.7, "Use IClothingSimulationInterface instead.") FClothingSimulation  // TODO: 5.9 switch to IClothingSimulationInterface
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		: public FClothingSimulationCommon
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
	public:
		FClothingSimulation();
		virtual ~FClothingSimulation() override;

		friend FSkeletalMeshCacheAdapter;

	protected:
		//~ Begin IClothingSimulationInterface Interface
		virtual void Initialize() override;
		virtual void Shutdown() override;

		virtual IClothingSimulationContext* CreateContext() override;
		virtual void DestroyContext(IClothingSimulationContext* InContext) override { delete InContext; }

		virtual void CreateActor(USkeletalMeshComponent* InOwnerComponent, const UClothingAssetBase* InAsset, int32 SimDataIndex) override;
		virtual void EndCreateActor() override;
		virtual void DestroyActors() override;

		virtual void FillContextAndPrepareTick(const USkeletalMeshComponent* InComponent, float InDeltaTime, IClothingSimulationContext* InOutContext, bool bIsInitialization, bool bForceTeleportResetOnly) override;
		virtual bool ShouldSimulateLOD(int32 OwnerLODIndex) const override;
		virtual void Simulate_AnyThread(const IClothingSimulationContext* InContext) override;
		virtual void ForceClothNextUpdateTeleportAndReset_AnyThread() override;
		virtual void HardResetSimulation(const IClothingSimulationContext* InContext) override
		{
			RefreshClothConfig(InContext);
		}
		virtual void AppendSimulationData(TMap<int32, FClothSimulData>& OutData, const USkeletalMeshComponent* InOwnerComponent, const USkinnedMeshComponent* InOverrideComponent) const override;

		// Return bounds in local space (or in world space if InOwnerComponent is null).
		virtual FBoxSphereBounds GetBounds(const USkeletalMeshComponent* InOwnerComponent) const override;

		virtual void AddExternalCollisions(const FClothCollisionData& InData) override;
		virtual void ClearExternalCollisions() override;
		virtual void GetCollisions(FClothCollisionData& OutCollisions, bool bIncludeExternal = true) const override;
		//~ End IClothingSimulationInterface Interface

	public:
		void SetGravityOverride(const FVector& InGravityOverride);
		void DisableGravityOverride();

		// Function to be called if any of the assets' configuration parameters have changed
		void RefreshClothConfig(const IClothingSimulationContext* InContext);
		// Function to be called if any of the assets' physics assets changes (colliders)
		// This seems to only happen when UPhysicsAsset::RefreshPhysicsAssetChange is called with
		// bFullClothRefresh set to false during changes created using the viewport manipulators.
		void RefreshPhysicsAsset();

		//~ Begin IClothingSimulationInterface Interface
		virtual int32 GetNumCloths() const override { return NumCloths; }
		virtual int32 GetNumKinematicParticles() const override { return NumKinematicParticles; }
		virtual int32 GetNumDynamicParticles() const override { return NumDynamicParticles; }
		virtual int32 GetNumIterations() const override { return NumIterations; }
		virtual int32 GetNumSubsteps() const override { return NumSubsteps; }
		virtual float GetSimulationTime() const override { return SimulationTime; }
		virtual bool IsTeleported() const override { return bIsTeleported; }
		//~ End IClothingSimulationInterface Interface

		FClothingSimulationCloth* GetCloth(int32 ClothId);

		FClothingSimulationSolver* GetSolver() { return Solver.Get(); }

#if WITH_EDITOR
		// Editor only debug draw function
		void DebugDrawPhysMeshShaded(FPrimitiveDrawInterface* PDI) const { Visualization.DrawPhysMeshShaded(PDI); }
#endif  // #if WITH_EDITOR

#if CHAOS_DEBUG_DRAW
		// Editor & runtime debug draw functions
		void DebugDrawParticleIndices(FCanvas* Canvas = nullptr, const FSceneView* SceneView = nullptr) const { Visualization.DrawParticleIndices(Canvas, SceneView); }
		void DebugDrawElementIndices(FCanvas* Canvas = nullptr, const FSceneView* SceneView = nullptr) const { Visualization.DrawElementIndices(Canvas, SceneView); }
		void DebugDrawMaxDistanceValues(FCanvas* Canvas = nullptr, const FSceneView* SceneView = nullptr) const { Visualization.DrawMaxDistanceValues(Canvas, SceneView); }
		void DebugDrawPhysMeshWired(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawPhysMeshWired(PDI); }
		void DebugDrawAnimMeshWired(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawAnimMeshWired(PDI); }
		void DebugDrawAnimNormals(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawAnimNormals(PDI); }
		void DebugDrawAnimVelocities(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawAnimVelocities(PDI); }
		void DebugDrawPointNormals(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawPointNormals(PDI); }
		void DebugDrawPointVelocities(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawPointVelocities(PDI); }
		void DebugDrawCollision(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawCollision(PDI); }
		void DebugDrawBackstops(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawBackstops(PDI); }
		void DebugDrawBackstopDistances(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawBackstopDistances(PDI); }
		void DebugDrawMaxDistances(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawMaxDistances(PDI); }
		void DebugDrawAnimDrive(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawAnimDrive(PDI); }
		void DebugDrawEdgeConstraint(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawEdgeConstraint(PDI); }
		void DebugDrawBendingConstraint(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawBendingConstraint(PDI); }
		void DebugDrawLongRangeConstraint(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawLongRangeConstraint(PDI); }
		void DebugDrawWindAndPressureForces(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawWindAndPressureForces(PDI); }
		void DebugDrawWindVelocity(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawWindVelocity(PDI); }
		void DebugDrawLocalSpace(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawLocalSpace(PDI); }
		void DebugDrawSelfCollision(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawSelfCollision(PDI); }
		void DebugDrawSelfIntersection(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawSelfIntersection(PDI); }
		void DebugDrawBounds(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawBounds(PDI); }
		void DebugDrawGravity(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawGravity(PDI); }
		void DebugDrawTeleportReset(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawTeleportReset(PDI); }
		void DebugDrawExtremlyDeformedEdges(FPrimitiveDrawInterface* PDI = nullptr) const { Visualization.DrawExtremlyDeformedEdges(PDI); }
#endif  // #if CHAOS_DEBUG_DRAW

		/** Return the visualization object for this simulation. */
		const FClothVisualizationNoGC* GetClothVisualization() const
		{
			return &Visualization;
		}

	private:
		void ResetStats();
		void UpdateStats(const FClothingSimulationCloth* Cloth);

		void UpdateSimulationFromSharedSimConfig();

		const FClothingSimulationContext* GetClothingSimulationContext(const USkeletalMeshComponent* SkeletalMeshComponent) const;

	private:
		// Visualization object
		FClothVisualizationNoGC Visualization;

		// Simulation objects
		TUniquePtr<FClothingSimulationSolver> Solver;  // Default solver
		TArray<TUniquePtr<FClothingSimulationMesh>> Meshes;
		TArray<TUniquePtr<FClothingSimulationCloth>> Cloths;
		TArray<TUniquePtr<FClothingSimulationCollider>> Colliders;
		TArray<TUniquePtr<FClothingSimulationConfig>> Configs;

		// External collision Data
		FClothCollisionData ExternalCollisionData;

		// Shared cloth config
		const UChaosClothSharedSimConfig* ClothSharedSimConfig;

		TBitArray<> LODHasAnyRenderClothMappingData; // Used by ShouldSimulateLOD

		// Properties that must be readable from all threads
		TAtomic<int32> NumCloths;
		TAtomic<int32> NumKinematicParticles;
		TAtomic<int32> NumDynamicParticles;
		TAtomic<int32> NumIterations;
		TAtomic<int32> NumSubsteps;
		TAtomic<float> SimulationTime;
		TAtomic<bool> bIsTeleported;

		// Overrides
		bool bUseLocalSpaceSimulation;
		bool bUseGravityOverride;
		FVector GravityOverride;
		FReal MaxDistancesMultipliers;

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		int32 StepCount;
		int32 ResetCount;
#endif
		mutable bool bHasInvalidReferenceBoneTransforms;
	};
} // namespace Chaos

#if !defined(CHAOS_GET_SIM_DATA_ISPC_ENABLED_DEFAULT)
#define CHAOS_GET_SIM_DATA_ISPC_ENABLED_DEFAULT 1
#endif

#if !defined(USE_ISPC_KERNEL_CONSOLE_VARIABLES_IN_SHIPPING)
#define USE_ISPC_KERNEL_CONSOLE_VARIABLES_IN_SHIPPING 0
#endif

// Support run-time toggling on supported platforms in non-shipping configurations
#if !INTEL_ISPC || (UE_BUILD_SHIPPING && !USE_ISPC_KERNEL_CONSOLE_VARIABLES_IN_SHIPPING)
static constexpr bool bChaos_GetSimData_ISPC_Enabled = INTEL_ISPC && CHAOS_GET_SIM_DATA_ISPC_ENABLED_DEFAULT;
#else
extern bool bChaos_GetSimData_ISPC_Enabled;
#endif
