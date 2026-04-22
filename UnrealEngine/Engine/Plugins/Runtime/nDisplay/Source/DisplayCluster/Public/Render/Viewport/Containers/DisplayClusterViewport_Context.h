// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StereoRendering.h"
#include "ShowFlags.h"

#include "Render/Viewport/Containers/DisplayClusterViewport_Enums.h"

/**
 * Viewport context with cahched data and states
 */
class FDisplayClusterViewport_Context
{
public:
	FDisplayClusterViewport_Context(const uint32 InContextNum, const EStereoscopicPass InStereoscopicPass, const int32 InStereoViewIndex)
		: ContextNum(InContextNum)
		, StereoscopicPass(InStereoscopicPass)
		, StereoViewIndex(InStereoViewIndex)
	{ }

public:
	/**
	* Declare all bitfields together to avoid alignment and padding.
	*/

	// True if this ViewData have a valid data.
	uint8 bValidData : 1 = 0;

	// Is this data have been calculated.
	uint8 bCalculated : 1 = 0;

	// Enables nDisplay's native implementation of cross-GPU transfer.
	// This disables cross-GPU transfer by default for nDisplay viewports in FSceneViewFamily structure.
	uint8 bOverrideCrossGPUTransfer : 1 = 0;

	// Disable render for this viewport (Overlay)
	uint8 bDisableRender : 1 = 0;

public:
	// Index of context (eye #)
	const uint32            ContextNum;

	// References to IStereoRendering
	EStereoscopicPass StereoscopicPass;
	int32             StereoViewIndex;

	struct FCachedViewData
	{
		/**
		* Here's the order of data transformation:
		*
		* IStereoRenderingDevice
		* -> {CameraOrigin,CameraRotation}
		*    IDisplayClusterViewport::CalculateViewData()
		*      []->{EyeLocation, EyeRotation}
		* 		   IDisplayClusterProjectionPolicy::CalculateView()
		*      []-> {ViewLocation, ViewRotation}
		* <- {ViewLocation, ViewRotation}
		* IStereoRenderingDevice
		*/

		// Camera location in the world space
		// This value receives nDisplay from IStereoRenderingDevice
		FVector CameraOrigin = FVector::ZeroVector;

		// Camera rotation in the world space
		// This value receives nDisplay from IStereoRenderingDevice
		FRotator CameraRotation = FRotator::ZeroRotator;

		// The eye location  in the world space
		// This value is used as an argument to IDisplayClusterProjectionPolicy::CalculateView()
		FVector EyeOrigin = FVector::ZeroVector;

		// The eye rotation in the world space
		// This value is used as an argument to IDisplayClusterProjectionPolicy::CalculateView()
		FRotator EyeRotation = FRotator::ZeroRotator;

		// Render view location used in world space
		FVector  RenderLocation = FVector::ZeroVector;

		// Render view rotation used in world space
		FRotator RenderRotation = FRotator::ZeroRotator;
	};

	// View data for this context
	FCachedViewData ViewData;

	/** Cached projection data */
	struct FCachedProjectionData
	{
		bool bValid = false;

		// Is overscan used
		bool bUseOverscan = false;

		// Projection angles [Left, Right, Top, Bottom]
		FVector4 ProjectionAngles;

		// Projection angles for Overscan [Left, Right, Top, Bottom]
		FVector4 OverscanProjectionAngles;

		// Clipping planes
		double ZNear = 0.f;
		double ZFar = 0.f;
	};

	// Cached projection values
	// This values updated from function FDisplayClusterViewport::CalculateProjectionMatrix()
	FCachedProjectionData ProjectionData;

	// UE scale: how many UE Units in one meter
	float WorldToMeters = 100.f;

	// Geometry scale: how many Geometry Units in one meter
	float GeometryToMeters = 100.f;

	// Origin component to world transform
	// This component is defined in the projection policy
	FTransform OriginToWorld = FTransform::Identity;

	// RootActor to world transform
	FTransform RootActorToWorld = FTransform::Identity;

	// View location used in world space
	FVector  ViewLocation = FVector::ZeroVector;

	// View rotation used in world space
	FRotator ViewRotation = FRotator::ZeroRotator;

	// Projection Matrix
	FMatrix ProjectionMatrix = FMatrix::Identity;

	// Overscan Projection Matrix (internal use)
	FMatrix OverscanProjectionMatrix = FMatrix::Identity;
	
	/** Additional data for the Depth of Field (DoF). */
	struct FDepthOfFieldSettings
	{
		// Focal length of the Depth of Field effect camera in mm.
		float SensorFocalLength = 0.f;

		// This is the squeeze factor for the DOF, which emulates the properties of anamorphic lenses.
		float SqueezeFactor = 1.f;

	} DepthOfField;

	// GPU index for this context render target
	int32 GPUIndex = INDEX_NONE;

	// Location and size on a render target texture
	FIntRect RenderTargetRect;

	// Context size
	FIntPoint ContextSize;

	// Location and size on a frame target texture
	FIntRect FrameTargetRect;

	// Tile location and size in the source viewport
	FIntRect TileDestRect;

	// Buffer ratio
	float CustomBufferRatio = 1;

	// Mips number for additional MipsShader resources
	int32 NumMips = 1;

	/**
	* Viewport context data for rendering thread
	*/
	struct FRenderThreadData
	{
		FRenderThreadData()
			: EngineShowFlags(ESFIM_All0)
		{ }
		
		// GPUIndex used to render this context.
		int32 GPUIndex = INDEX_NONE;

		// Display gamma used to render this context
		float EngineDisplayGamma = 2.2f;

		// Engine flags used to render this context
		FEngineShowFlags EngineShowFlags;
	};

	// This data updated only on rendering thread
	FRenderThreadData RenderThreadData;
};
