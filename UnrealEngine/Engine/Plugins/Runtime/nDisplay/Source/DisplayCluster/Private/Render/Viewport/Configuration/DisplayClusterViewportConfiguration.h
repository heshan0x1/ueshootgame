// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Render/Viewport/IDisplayClusterViewportConfiguration.h"
#include "Render/Viewport/Configuration/DisplayClusterViewportConfigurationProxy.h"
#include "Render/Viewport/RenderFrame/DisplayClusterRenderFrameSettings.h"

#include "SceneTypes.h"
#include "Misc/DisplayClusterObjectRef.h"
#include "Templates/SharedPointer.h"

class FDisplayClusterViewportConfigurationProxy;
class FDisplayClusterViewportManager;
class FDisplayClusterViewportManagerProxy;
class ADisplayClusterRootActor;
class UDisplayClusterConfigurationData;
struct FDisplayClusterConfigurationRenderFrame;

/**
* Implementation of the viewport manager configuration.
*/
class FDisplayClusterViewportConfiguration
	: public IDisplayClusterViewportConfiguration
	, public TSharedFromThis<FDisplayClusterViewportConfiguration, ESPMode::ThreadSafe>
{
public:
	FDisplayClusterViewportConfiguration();
	virtual ~FDisplayClusterViewportConfiguration();

	void Initialize(FDisplayClusterViewportManager& InViewportManager);

public:
	//~ BEGIN IDisplayClusterViewportConfiguration
	virtual void SetRootActor(const EDisplayClusterRootActorType InRootActorType, const ADisplayClusterRootActor* InRootActor) override;
	
	virtual void SetPreviewSettings(const FDisplayClusterViewport_PreviewSettings& InPreviewSettings) override
	{
		RenderFrameSettings.PreviewSettings = InPreviewSettings;
	}

	virtual const FDisplayClusterViewport_PreviewSettings& GetPreviewSettings() const override
	{
		return RenderFrameSettings.PreviewSettings;
	}

	virtual bool UpdateConfigurationForClusterNode(EDisplayClusterRenderFrameMode InRenderMode, const UWorld* InWorld, const FString& InClusterNodeId) override
	{
		return ImplUpdateConfiguration(InRenderMode, InWorld, InClusterNodeId, nullptr);
	}

	virtual bool UpdateConfigurationForViewportsList(EDisplayClusterRenderFrameMode InRenderMode, const UWorld* InWorld, const TArray<FString>& InViewportNames) override
	{
		return ImplUpdateConfiguration(InRenderMode, InWorld, TEXT(""), &InViewportNames);
	}

	virtual void ReleaseConfiguration() override;

	virtual UWorld* GetCurrentWorld() const override;
	virtual float GetRootActorWorldDeltaSeconds(const EDisplayClusterRootActorType InRootActorType = EDisplayClusterRootActorType::Scene) const override;
	virtual ADisplayClusterRootActor* GetRootActor(const EDisplayClusterRootActorType InRootActorType) const override;
	virtual IDisplayClusterViewportManager* GetViewportManager() const override;
	virtual const UDisplayClusterConfigurationData* GetConfigurationData() const override;
	virtual const FDisplayClusterConfigurationICVFX_StageSettings* GetStageSettings() const override;
	virtual const FDisplayClusterConfigurationRenderFrame* GetConfigurationRenderFrameSettings() const  override;

	virtual EDisplayClusterRenderFrameMode GetRenderModeForPIE() const override;
	virtual bool IsCurrentWorldHasAnyType(const EWorldType::Type InWorldType1, const EWorldType::Type InWorldType2 = EWorldType::None, const EWorldType::Type InWorldType3 = EWorldType::None) const override;
	virtual bool IsRootActorWorldHasAnyType(const EDisplayClusterRootActorType InRootActorType, const EWorldType::Type InWorldType1, const EWorldType::Type InWorldType2 = EWorldType::None, const EWorldType::Type InWorldType3 = EWorldType::None) const override;

	virtual const IDisplayClusterViewportConfigurationProxy& GetProxy() const override
	{
		return *Proxy;
	}

	virtual bool IsSceneOpened() const override
	{
		return bCurrentSceneActive && GetCurrentWorld();
	}

	virtual bool IsClusterNodeRenderingOffscreen() const override;
	virtual bool IsMediaAvailable() const override;

	virtual const FString& GetClusterNodeId() const override
	{
		return RenderFrameSettings.CurrentNode.Id;
	}

	virtual bool IsPreviewRendering() const override
	{
		return RenderFrameSettings.IsPreviewRendering();
	}

	virtual bool IsTechvisEnabled() const override
	{
		return RenderFrameSettings.IsTechvisEnabled();
	}

	virtual bool IsPreviewInGameEnabled() const override
	{
		return RenderFrameSettings.IsPreviewInGameEnabled();
	}

	virtual bool IsExclusiveLocked() const override;

	virtual const float GetWorldToMeters() const override;
	// ~~END IDisplayClusterViewportConfiguration

public:
	/** Get a pointer to the DC ViewportManager if it still exists. */
	FDisplayClusterViewportManager* GetViewportManagerImpl() const
	{
		return ViewportManagerWeakPtr.IsValid() ? ViewportManagerWeakPtr.Pin().Get() : nullptr;
	}

	/** Gets the rendering frame settings. */
	const FDisplayClusterRenderFrameSettings& GetRenderFrameSettings() const
	{ 
		check(IsInGameThread());

		return RenderFrameSettings;
	}

	/**
	* Returns true if unused resources on offscreen nodes should be optimized.
	*/
	bool ShouldOptimizeOutOffscreenNodeResources() const;


	/** Sets the rendering frame settings. */
	void SetRenderFrameSettings(const FDisplayClusterRenderFrameSettings& InRenderFrameSettings)
	{
		check(IsInGameThread());

		// Process cluster node name changes
		SetClusterNodeId(InRenderFrameSettings.CurrentNode.Id);

		RenderFrameSettings = InRenderFrameSettings;
	}

	// Returns true if the current frame's viewport list is out of date and needs to be refreshed.
	inline bool IsCurrentRenderFrameViewportsOutOfDate() const
	{
		return bCurrentRenderFrameViewportsOutOfDate;
	}

	// Clears the stale flag, marking the current frame's viewport list as up to date.
	inline void ClearCurrentRenderFrameViewportsOutOfDate()
	{
		bCurrentRenderFrameViewportsOutOfDate = 0;
	}

	// Raises the stale flag, marking the current frame's viewport list as needing an update.
	inline void MarkCurrentRenderFrameViewportsOutOfDate()
	{
		bCurrentRenderFrameViewportsOutOfDate = 1;
	}

	/** Set the name of the current cluster node. */
	void SetClusterNodeId(const FString& InClusterNodeId)
	{
		// Tell ViewportManager to update the viewport list:
		// 1. Cluster node name changed (each node has its own viewport list)
		// 2. Empty cluster node name (use custom viewport list instead: MRQ, etc)
		if (RenderFrameSettings.CurrentNode.Id != InClusterNodeId
		|| InClusterNodeId.IsEmpty())
		{
			RenderFrameSettings.CurrentNode.Id = InClusterNodeId;
			MarkCurrentRenderFrameViewportsOutOfDate();
		}
	}

	void OnHandleStartScene();
	void OnHandleEndScene();

	/** Returns the DCRA name or an empty string. */
	FString GetRootActorName() const;

private:
	/** Update configuration implementation.
	* 
	* @param InRenderMode    - rendering mode.
	* @param InWorld         - ptr to the world to be rendered
	* @param InClusterNodeId - (opt) Configuring rendering for a cluster node.
	* @param InViewportNames - (opt) Configuring rendering for a list of viewports.
	*/
	bool ImplUpdateConfiguration(EDisplayClusterRenderFrameMode InRenderMode, const UWorld* InWorld, const FString& InClusterNodeId, const TArray<FString>* InViewportNames);

	/** Hide DCRA components for nDisplay rendering.*/
	void ImplUpdateConfigurationVisibility() const;

	/** Set current world.*/
	void SetCurrentWorldImpl(const UWorld* InWorld);

public:
	// Reference to configuration proxy object
	const TSharedRef<FDisplayClusterViewportConfigurationProxy, ESPMode::ThreadSafe> Proxy;

private:
	// Whether the list of viewports of the current frame needs to be updated
	uint8 bCurrentRenderFrameViewportsOutOfDate : 1 = 0;

	// Is current scene started
	uint8 bCurrentSceneActive : 1 = 0;

	// Current render frame settings
	FDisplayClusterRenderFrameSettings RenderFrameSettings;

	// A reference to the owning viewport manager
	TWeakPtr<FDisplayClusterViewportManager, ESPMode::ThreadSafe> ViewportManagerWeakPtr;

	// This DCRA will be used to render previews. The meshes and preview materials are created at runtime.
	FDisplayClusterActorRef PreviewRootActorRef;

	// (Optional) A reference to DCRA in the scene, used as a source for math calculations and references.
	// Locations in the scene and math data are taken from this DCRA.
	FDisplayClusterActorRef SceneRootActorRef;

	// (Optional) Reference to DCRA, used as a source of configuration data from DCRA and its components.
	FDisplayClusterActorRef ConfigurationRootActorRef;

	// Pointer to the current world to be rendered in.
	TWeakObjectPtr<UWorld> CurrentWorldRef;
};
