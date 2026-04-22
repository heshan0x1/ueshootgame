// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Misc/Build.h"      // For CHAOS_DEBUG_DRAW
#include "Chaos/Declares.h"  //
#include "Misc/CoreMiscDefines.h"
#include "UObject/NameTypes.h"

#if WITH_EDITOR
#include "UObject/GCObject.h"
#include "UObject/ObjectPtr.h"
#endif  // #if WITH_EDITOR

class FCanvas;
class FSceneView;
class FPrimitiveDrawInterface;
class UMaterial;

namespace Chaos
{
	class FClothingSimulationSolver;

	class FClothVisualizationNoGC
	{
	public:
		CHAOSCLOTH_API explicit FClothVisualizationNoGC(const ::Chaos::FClothingSimulationSolver* InSolver = nullptr);
		CHAOSCLOTH_API virtual ~FClothVisualizationNoGC();

#if CHAOS_DEBUG_DRAW
		// Editor & runtime functions
		CHAOSCLOTH_API void SetSolver(const ::Chaos::FClothingSimulationSolver* InSolver);

		CHAOSCLOTH_API void DrawParticleIndices(FCanvas* Canvas = nullptr, const FSceneView* SceneView = nullptr) const;
		CHAOSCLOTH_API void DrawElementIndices(FCanvas* Canvas = nullptr, const FSceneView* SceneView = nullptr) const;
		CHAOSCLOTH_API void DrawMaxDistanceValues(FCanvas* Canvas = nullptr, const FSceneView* SceneView = nullptr) const;

		CHAOSCLOTH_API void DrawPhysMeshWired(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawAnimMeshWired(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawAnimVelocities(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawAnimNormals(FPrimitiveDrawInterface* PDI, const FReal Length) const;
		CHAOSCLOTH_API void DrawOpenEdges(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawPointNormals(FPrimitiveDrawInterface* PDI, const FReal Length) const;
		CHAOSCLOTH_API void DrawPointVelocities(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawCollision(FPrimitiveDrawInterface* PDI, bool bWireframe) const;
		CHAOSCLOTH_API void DrawBackstops(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawBackstopDistances(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawMaxDistances(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawAnimDrive(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawEdgeConstraint(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawBendingConstraint(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawLongRangeConstraint(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawWindAndPressureForces(FPrimitiveDrawInterface* PDI, const FReal ForceLengthScale) const;
		CHAOSCLOTH_API void DrawWindVelocity(FPrimitiveDrawInterface* PDI, const FReal LengthScale) const;
		CHAOSCLOTH_API void DrawLocalSpace(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawSelfCollision(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawSelfIntersection(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawSelfCollisionThickness(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawKinematicColliderWired(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawBounds(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawGravity(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawFictitiousAngularForces(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawMultiResConstraint(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawClothClothConstraints(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawTeleportReset(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawExtremlyDeformedEdges(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawAccessoryMesh(FPrimitiveDrawInterface* PDI = nullptr, const FName& AccessoryMeshName = NAME_None) const;
		CHAOSCLOTH_API void DrawAccessoryMeshNormals(FPrimitiveDrawInterface* PDI = nullptr, const FName& AccessoryMeshName = NAME_None, const FReal Length = 20.) const;
	
#else  // #if CHAOS_DEBUG_DRAW
		void SetSolver(const ::Chaos::FClothingSimulationSolver* /*InSolver*/) {}

		void DrawParticleIndices(FCanvas* /*Canvas*/, const FSceneView* /*SceneView*/) const {}
		void DrawElementIndices(FCanvas* /*Canvas*/, const FSceneView* /*SceneView*/) const {}
		void DrawMaxDistanceValues(FCanvas* /*Canvas*/, const FSceneView* /*SceneView*/) const {}
		void DrawPhysMeshWired(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawAnimMeshWired(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawAnimNormals(FPrimitiveDrawInterface* /*PDI*/, const FReal /*Length*/) const {}
		void DrawOpenEdges(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawPointNormals(FPrimitiveDrawInterface* /*PDI*/, const FReal /*Length*/) const {}
		void DrawPointVelocities(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawCollision(FPrimitiveDrawInterface* /*PDI*/, bool /*bWireframe*/) const {}
		void DrawBackstops(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawBackstopDistances(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawMaxDistances(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawAnimDrive(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawEdgeConstraint(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawBendingConstraint(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawLongRangeConstraint(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawWindAndPressureForces(FPrimitiveDrawInterface* /*PDI*/, const FReal /*ForceLengthScale*/) const {}
		void DrawWindVelocity(FPrimitiveDrawInterface* /*PDI*/, const FReal /*LengthScale*/) const {}
		void DrawLocalSpace(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawSelfCollision(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawSelfIntersection(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawSelfCollisionThickness(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawKinematicColliderWired(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawBounds(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawGravity(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawFictitiousAngularForces(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawMultiResConstraint(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawClothClothConstraints(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawExtremlyDeformedEdges(FPrimitiveDrawInterface* /*PDI*/ = nullptr) const {}
		void DrawAccessoryMesh(FPrimitiveDrawInterface* /*PDI*/ = nullptr, const FName& AccessoryMeshName = NAME_None) const {}
		void DrawAccessoryMeshNormals(FPrimitiveDrawInterface* /*PDI*/ = nullptr, const FName& AccessoryMeshName = NAME_None, const FReal Length = 20.) const;
#endif  // #if CHAOS_DEBUG_DRAW

		void DrawAnimNormals(FPrimitiveDrawInterface* PDI = nullptr) const
		{
			constexpr FReal DefaultLengtth = 20.;
			DrawAnimNormals(PDI, DefaultLengtth);
		}
		void DrawPointNormals(FPrimitiveDrawInterface* PDI = nullptr) const
		{
			constexpr FReal DefaultLengtth = 20.;
			DrawPointNormals(PDI, DefaultLengtth);
		}
		void DrawCollision(FPrimitiveDrawInterface* PDI = nullptr) const
		{
			constexpr bool bDefaultWireframe = false;
			DrawCollision(PDI, bDefaultWireframe);
		}
		void DrawWindAndPressureForces(FPrimitiveDrawInterface* PDI = nullptr) const
		{
			constexpr FReal DefaultForceLengthScale = 10.;
			DrawWindAndPressureForces(PDI, DefaultForceLengthScale);
		}
		void DrawWindVelocity(FPrimitiveDrawInterface* PDI = nullptr) const
		{
			constexpr FReal DefaultLengthScale = .1;
			DrawWindVelocity(PDI, DefaultLengthScale);
		}

#if WITH_EDITOR && CHAOS_DEBUG_DRAW
		// Editor only functions
		CHAOSCLOTH_API void DrawPhysMeshShaded(FPrimitiveDrawInterface* PDI) const;
		CHAOSCLOTH_API void DrawWeightMap(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawSelfCollisionLayers(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawInpaintWeightsMatched(FPrimitiveDrawInterface* PDI = nullptr) const;
		CHAOSCLOTH_API void DrawKinematicColliderShaded(FPrimitiveDrawInterface* PDI) const;
		CHAOSCLOTH_API void DrawWeightMapWithName(FPrimitiveDrawInterface* PDI, const FString& Name) const;
		/** Will draw current morph target (if any) if no name or an invalid name is specified */
		CHAOSCLOTH_API void DrawSimMorphTarget(FPrimitiveDrawInterface* PDI = nullptr, const FString& Name = "") const;
		CHAOSCLOTH_API TArray<FString> GetAllWeightMapNames() const;
		CHAOSCLOTH_API TArray<FString> GetAllMorphTargetNames() const;
		CHAOSCLOTH_API TArray<FName> GetAllAccessoryMeshNames() const;
#else  // #if WITH_EDITOR && CHAOS_DEBUG_DRAW
		void DrawPhysMeshShaded(FPrimitiveDrawInterface* /*PDI*/) const {}
		void DrawKinematicColliderShaded(FPrimitiveDrawInterface* /*PDI*/) const {}
#endif  // #if WITH_EDITOR && CHAOS_DEBUG_DRAW

#if CHAOS_DEBUG_DRAW
	private:
		// Simulation objects
		const ::Chaos::FClothingSimulationSolver* Solver;
#endif  // #if CHAOS_DEBUG_DRAW

		class FMaterials;
	};
} // End namespace Chaos
