// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Chaos/ChaosVDRemoteSessionsManager.h"

namespace Chaos::VD
{
	class ITraceDataRelayTransport;
}

#if WITH_CHAOS_VISUAL_DEBUGGER

#include "Chaos/ParticleHandleFwd.h"
#include "DataWrappers/ChaosVDParticleDataWrapper.h"
#include "Delegates/IDelegateInstance.h"
#include "HAL/Platform.h"

class IPhysicsProxyBase;
class UBodySetup;
class UChaosVDTraceDataChannel;
class UGameInstance;

namespace Chaos
{
	namespace VisualDebugger
	{
		struct FChaosVDOptionalDataChannel;
	}
}

#ifndef WITH_CHAOS_VISUAL_DEBUGGER_EXTERNAL_MESSAGING
	#define WITH_CHAOS_VISUAL_DEBUGGER_EXTERNAL_MESSAGING 0
#endif

/** Object that bridges the gap between the ChaosVD runtime module and the Engine & CVD editor.
 * As the ChaosVDRuntime module does not have access to the engine module, this object reacts to events and performs necessary operation the runtime module cannot do directly
 */
class FChaosVDEngineEditorBridge
{
public:
	
	FChaosVDEngineEditorBridge() : RemoteSessionsManager(MakeShared<FChaosVDRemoteSessionsManager>())
	{
	}
	
	~FChaosVDEngineEditorBridge(){}

	CHAOSSOLVERENGINE_API static FChaosVDEngineEditorBridge& Get();

	void Initialize();
	void TearDown();

	TSharedPtr<FChaosVDRemoteSessionsManager> GetRemoteSessionsManager()
	{
		return RemoteSessionsManager;
	}

	using FChaosVDDataDataChannel = Chaos::VisualDebugger::FChaosVDOptionalDataChannel;

	TSharedPtr<Chaos::VD::ITraceDataRelayTransport> GetRelayTransportInstance()
	{
		return RelayTraceDataTransportInstance;
	}

	bool IsInitialized() const
	{
		return bIsInitialized;
	}

	FSimpleMulticastDelegate& OnEngineEditorBridgeInitialized()
	{
		return EngineEditorBridgeInitializedDelegate;
	}

	CHAOSSOLVERENGINE_API void SetExternalTraceRelayInstance(const TSharedPtr<Chaos::VD::ITraceDataRelayTransport>& InExternalTraceRelayInstance);

private:
	void HandleTraceConnectionDetailsUpdated();
	bool AddOnScreenRecordingMessage(float DummyDeltaTime = 0.1f);
	void RemoveOnScreenRecordingMessage();
	void HandleCVDRecordingStarted();
	void HandleCVDPostRecordingStarted();
	void HandleCVDRecordingStopped();
	void HandleCVDRecordingStartFailed(const FText& InFailureReason) const;
	void HandlePIEStarted(UGameInstance* GameInstance);

	void HandleDataChannelChanged(TWeakPtr<FChaosVDDataDataChannel> ChannelWeakPtr);

	void SerializeCollisionChannelsNames();
	
	bool BroadcastSessionStatus(float DeltaTime);

	static FChaosVDParticleMetadata GenerateParticleMetadata(const IPhysicsProxyBase* ParticleProxy, const Chaos::FGeometryParticleHandle* ParticleHandle);

	FDelegateHandle RecordingStartedHandle;
	FDelegateHandle PostRecordingStartedHandle;
	FDelegateHandle RecordingStoppedHandle;
	FDelegateHandle RecordingStartFailedHandle;
	uint64 CVDRecordingMessageKey = 0;

#if WITH_EDITOR
	FDelegateHandle PIEStartedHandle;
#endif

	TSharedPtr<Chaos::VD::ITraceDataRelayTransport> RelayTraceDataTransportInstance;

	TSharedRef<FChaosVDRemoteSessionsManager> RemoteSessionsManager;
	
	FTSTicker::FDelegateHandle RecordingStatusUpdateHandle;
	FTSTicker::FDelegateHandle DeferredShowMessageOnScreenHandle;

	FSimpleMulticastDelegate EngineEditorBridgeInitializedDelegate;

	bool bIsInitialized = false;
};
#else

class FChaosVDEngineEditorBridge
{
public:
	CHAOSSOLVERENGINE_API static FChaosVDEngineEditorBridge& Get();
	
	TSharedPtr<FChaosVDRemoteSessionsManager> GetRemoteSessionsManager()
	{
		return nullptr;
	}

	TSharedPtr<Chaos::VD::ITraceDataRelayTransport> GetRelayTransportInstance()
	{
		return nullptr;
	}
};

#endif
