// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaFrameworkWorldSettingsAssetUserData.h"
#include "GameFramework/Actor.h"
#include "Serialization/CustomVersion.h"
#include "UObject/EnterpriseObjectVersion.h"
#include "UObject/UE5ReleaseStreamObjectVersion.h"

FMediaFrameworkCaptureCurrentViewportOutputInfo::FMediaFrameworkCaptureCurrentViewportOutputInfo()
	: MediaOutput(nullptr)
	, ViewMode(VMI_Unknown)
{
}


FMediaFrameworkCaptureCameraViewportCameraOutputInfo::FMediaFrameworkCaptureCameraViewportCameraOutputInfo()
	: MediaOutput(nullptr)
	, ViewMode(VMI_Unknown)
{
}

bool FMediaFrameworkCaptureCameraViewportCameraOutputInfo::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FUE5ReleaseStreamObjectVersion::GUID);
	return false;
}

#if WITH_EDITORONLY_DATA
PRAGMA_DISABLE_DEPRECATION_WARNINGS
void FMediaFrameworkCaptureCameraViewportCameraOutputInfo::PostSerialize(const FArchive& Ar)
{
	const int32 CustomVersion = Ar.CustomVer(FUE5ReleaseStreamObjectVersion::GUID);
	if (Ar.IsLoading() && CustomVersion < FUE5ReleaseStreamObjectVersion::MediaProfilePluginCaptureCameraSoftPtr)
	{

		for (const TLazyObjectPtr<AActor>& LazyActor : LockedActors_DEPRECATED)
		{
			Cameras.Add(LazyActor.Get());
		}

		LockedActors_DEPRECATED.Empty();
	}
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif

FMediaFrameworkCaptureRenderTargetCameraOutputInfo::FMediaFrameworkCaptureRenderTargetCameraOutputInfo()
	: RenderTarget(nullptr)
	, MediaOutput(nullptr)
{
}


UMediaFrameworkWorldSettingsAssetUserData::UMediaFrameworkWorldSettingsAssetUserData()
{
	CurrentViewportMediaOutput.CaptureOptions.ResizeMethod = EMediaCaptureResizeMethod::ResizeSource;
}


PRAGMA_DISABLE_DEPRECATION_WARNINGS
void UMediaFrameworkWorldSettingsAssetUserData::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FEnterpriseObjectVersion::GUID);
	if (Ar.IsLoading() && FEnterpriseObjectVersion::MediaFrameworkUserDataLazyObject > Ar.CustomVer(FEnterpriseObjectVersion::GUID))
	{
		for (FMediaFrameworkCaptureCameraViewportCameraOutputInfo& OutputInfo : ViewportCaptures)
		{
			if (OutputInfo.LockedCameraActors_DEPRECATED.Num() > 0)
			{
				for (AActor* Actor : OutputInfo.LockedCameraActors_DEPRECATED)
				{
					if (Actor)
					{
						OutputInfo.Cameras.Add(Actor);
					}
				}
				OutputInfo.LockedCameraActors_DEPRECATED.Empty();
			}
		}
	}
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
