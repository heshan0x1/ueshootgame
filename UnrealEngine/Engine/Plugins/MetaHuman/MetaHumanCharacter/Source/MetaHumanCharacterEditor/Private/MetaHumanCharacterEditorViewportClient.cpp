// Copyright Epic Games, Inc.All Rights Reserved.

#include "MetaHumanCharacterEditorViewportClient.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorLog.h"
#include "MetaHumanCharacterEditorActorInterface.h"
#include "MetaHumanCharacterEditorSettings.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterEditorModule.h"
#include "PreviewScene.h"
#include "SceneView.h"
#include "ContentStreaming.h"
#include "Components/SkeletalMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/Light.h"
#include "Components/LightComponent.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "SkeletalDebugRendering.h"
#include "MetaHumanCharacterEditorActor.h"
#include "MetaHumanCharacterEnvironmentLightRig.h"
#include "Animation/DebugSkelMeshComponent.h"

#define LOCTEXT_NAMESPACE "MetaHumanCharacterEditorViewportClient"

FMetaHumanCharacterViewportClient::FMetaHumanCharacterViewportClient(
	FEditorModeTools* InModeTools, 
	FPreviewScene* InPreviewScene,
	TWeakInterfacePtr<IMetaHumanCharacterEditorActorInterface> InEditingActor,
	TWeakObjectPtr<UMetaHumanCharacter> InCharacter)
	: FEditorViewportClient{ InModeTools, InPreviewScene }
	, WeakCharacterActor{InEditingActor}
	, WeakCharacter{InCharacter}
	, bIsViewportFramed{false}
	, AutoSelectedFrame{EMetaHumanCharacterCameraFrame::Face}
	, LastSelectedFrame{EMetaHumanCharacterCameraFrame::Auto}
{
	// The real time override is required to make sure the world ticks while the viewport is not active
	// or this requires the user to interact with the viewport to get up to date lighting and textures
	AddRealtimeOverride(true, NSLOCTEXT("FMetaHumanCharacterViewportClient", "RealTimeOverride", "MetaHumanCharacterRealTimeOverride"));
	SetRealtime(true);

	// This is done in order to enable Advanced Post Process effects that are disabled in FPreviewScene that we use 
	EngineShowFlags.EnableAdvancedFeatures();

	// TODO: Find a better way to hide icons, probably just setting some of the flags. 
	// I tried this by setting flags like collision, bounds, some lighting, icons but it wasn't successful so I'll get back to tidy this
	SetGameView(true);

	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	check(Settings);

	FEditorViewportClient::SetCameraSpeedSettings(Settings->CameraSpeedSettings);
	
	// Allow closeups of the face without clipping
	OverrideNearClipPlane(1.0f);

	EngineShowFlags.SetDepthOfField(true);

	ChangeRenderQuality(InCharacter.Get()->ViewportSettings.RenderingQuality);

	// Use the Min float value as a flag to indicate the last camera yaw is not valid
	LastCameraYaw = TNumericLimits<float>::Min();

	// Register a delegate to the Effects scalability setting so that the viewport can react
	// when it changes and enable/disable transmission if needed. The delegate is bound to the
	// editing actor so it gets unregistered when closing the asset editor
	static IConsoleVariable* EffectsQualityCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("sg.EffectsQuality"));
	check(EffectsQualityCVar);
	EffectsQualityCVar->OnChangedDelegate().AddWeakLambda(
			InEditingActor.GetObject(),
			[this](IConsoleVariable* CVar)
			{
				if (WeakCharacter.IsValid())
				{
					const bool bShouldLightTransmissionBeEnabled = WeakCharacter->ViewportSettings.RenderingQuality != EMetaHumanCharacterRenderingQuality::Medium;
					SetTransmissionForAllLights(bShouldLightTransmissionBeEnabled);
				}
			});

	// Bone drawing info initizalization
	const USkeletalMeshComponent* MetaHumanSkeletalMeshFaceComponent = WeakCharacterActor.Get()->GetFaceComponent();
	const USkeletalMeshComponent* MetaHumanSkeletalMeshBodyComponent = WeakCharacterActor.Get()->GetBodyComponent();

	FaceBonesWorldTransforms.AddUninitialized(MetaHumanSkeletalMeshFaceComponent->GetNumComponentSpaceTransforms());
	BodyBonesWorldTransforms.AddUninitialized(MetaHumanSkeletalMeshBodyComponent->GetNumComponentSpaceTransforms());
	FaceBoneColors.AddUninitialized(MetaHumanSkeletalMeshFaceComponent->GetNumComponentSpaceTransforms());
	BodyBoneColors.AddUninitialized(MetaHumanSkeletalMeshBodyComponent->GetNumComponentSpaceTransforms());
}

void FMetaHumanCharacterViewportClient::Tick(float InDeltaSeconds)
{
	FEditorViewportClient::Tick(InDeltaSeconds);

	if (!GIntraFrameDebuggingGameThread && GetPreviewScene() != nullptr)
	{
		GetPreviewScene()->GetWorld()->Tick(LEVELTICK_ViewportsOnly, InDeltaSeconds);
	}

	if (!bIsViewportFramed && Viewport->GetSizeXY().X > 0 && Viewport->GetSizeXY().Y > 0)
	{
		if (UMetaHumanCharacter* Character = WeakCharacter.Get())
		{
			// initial focus on the camera frame as stored in the character
			FocusOnSelectedFrame(Character->ViewportSettings.CameraFrame, /*bInRotate*/ true);
		}
		bIsViewportFramed = true;
	}

	// Rotate the light rig with the camera if in orbit mode
	if (bUsingOrbitCamera)
	{
		if (LastCameraYaw == TNumericLimits<float>::Min())
		{
			// Get the value of the camera yaw the first time the camera changes to orbit mode
			LastCameraYaw = GetViewTransform().GetRotation().Yaw;
		}

		if (UWorld* World = GetPreviewScene()->GetWorld())
		{
			const float CurrentCameraYaw = GetViewTransform().GetRotation().Yaw;
			const float DeltaCameraYaw = LastCameraYaw - CurrentCameraYaw;

			if (DeltaCameraYaw != 0.0f)
			{
				for (FActorIterator It(PreviewScene->GetWorld()); It; ++It)
				{
					AActor* Actor = *It;

					if (Actor->GetClass()->ImplementsInterface(UMetaHumanCharacterEnvironmentLightRig::StaticClass()))
					{
						if (AActor* ParentActor = Actor->GetAttachParentActor())
						{
							// Rotate the parent of the light rig which makes it follow the camera rotation
							// The light rig rotates it self when the user changes the slider in the Environment dropdown menu
							FRotator ParentRotation = ParentActor->GetActorRotation();
							ParentRotation.Yaw += DeltaCameraYaw;
							ParentActor->SetActorRotation(ParentRotation);
							break;
						}
					}
				}
			}

			LastCameraYaw = CurrentCameraYaw;
		}
	}
}

bool FMetaHumanCharacterViewportClient::InputAxis(const FInputKeyEventArgs& Args)
{
	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	check(Settings);

	const float AdjustedMoveDelta = Args.AmountDepressed * Settings->MouseSensitivityModifier;
	FInputKeyEventArgs ModifiedArgs = Args;
	ModifiedArgs.AmountDepressed = AdjustedMoveDelta;
	
	return FEditorViewportClient::InputAxis(ModifiedArgs);
}

bool FMetaHumanCharacterViewportClient::InputKey(const FInputKeyEventArgs& InEventArgs)
{
	if (InEventArgs.Key == EKeys::F && InEventArgs.Event == IE_Pressed)
	{
		if (UMetaHumanCharacter* Character = WeakCharacter.Get())
		{
			FocusOnSelectedFrame(Character->ViewportSettings.CameraFrame, /*bInRotate*/ Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift));
			return true;
		}
	}
	TUniquePtr<FViewportCameraTransform> PreViewTransform;
	if (InEventArgs.Key == EKeys::MouseScrollUp)
	{
		// make sure orbit camera is used
		ToggleOrbitCamera(true);
		PreViewTransform = MakeUnique<FViewportCameraTransform>(GetViewTransform());
	}
	const bool Success = FEditorViewportClient::InputKey(InEventArgs);
	if (PreViewTransform && bUsingOrbitCamera)
	{
		// ensure mouse wheel scrolling stops at minimum distance
		float MinDist = 35.0f;
		float PreDist = (PreViewTransform->GetLookAt() - PreViewTransform->GetLocation()).Length();
		float PostDist = (GetViewTransform().GetLookAt() - GetViewTransform().GetLocation()).Length();
		if (PostDist > PreDist || PostDist < MinDist)
		{
			GetViewTransform() = *PreViewTransform;
			FVector Offset = GetViewTransform().GetLocation() - GetViewTransform().GetLookAt();
			FVector OffsetNormalized = Offset.GetSafeNormal();
			GetViewTransform().SetLocation(GetViewTransform().GetLookAt() + OffsetNormalized * MinDist);
		}

	}
	return Success;
}

bool FMetaHumanCharacterViewportClient::ShouldOrbitCamera() const
{
	return true;
}

void FMetaHumanCharacterViewportClient::OverridePostProcessSettings(FSceneView& View)
{
	View.OverridePostProcessSettings(PostProcessSettings, /* Blending Weight */ 1.0f);
	Invalidate();
}

void FMetaHumanCharacterViewportClient::SetupViewForRendering(FSceneViewFamily& InViewFamily, FSceneView& InOutView)
{
	FEditorViewportClient::SetupViewForRendering(InViewFamily, InOutView);

	// Set the streaming boost based on the character editor project settings to allow streaming of textures even with low FoV values
	const int32 StreamingBoost = GetDefault<UMetaHumanCharacterEditorSettings>()->TextureStreamingBoost;
	const float SizeX = InOutView.UnscaledViewRect.Width();
	const float FOVScreenSize = SizeX / FMath::Tan(FMath::DegreesToRadians(ViewFOV * 0.5f));
	IStreamingManager::Get().AddViewInformation(InOutView.ViewMatrices.GetViewOrigin(), SizeX, FOVScreenSize, StreamingBoost);
}

void FMetaHumanCharacterViewportClient::SetAutoFocusToSelectedFrame(EMetaHumanCharacterCameraFrame SelectedFrame, bool bInRotate)
{
	if (SelectedFrame != EMetaHumanCharacterCameraFrame::Auto)
	{
		AutoSelectedFrame = SelectedFrame;

		if (AutoSelectedFrame != LastSelectedFrame)
		{
			if (UMetaHumanCharacter* Character = WeakCharacter.Get())
			{
				if (Character->ViewportSettings.CameraFrame == EMetaHumanCharacterCameraFrame::Auto)
				{
					FocusOnSelectedFrame(Character->ViewportSettings.CameraFrame, bInRotate);
				}
			}
		}
	}
}

void FMetaHumanCharacterViewportClient::RescheduleFocus()
{
	bIsViewportFramed = false;
}

void FMetaHumanCharacterViewportClient::FocusOnSelectedFrame(EMetaHumanCharacterCameraFrame SelectedFrame, bool bInRotate)
{
	if (UMetaHumanCharacter* Character = WeakCharacter.Get())
	{
		if (Character->ViewportSettings.CameraFrame != SelectedFrame)
		{
			Character->ViewportSettings.CameraFrame = SelectedFrame;
			Character->MarkPackageDirty();
		}
	}

	// make sure the viewport is in orbit camera mode
	ToggleOrbitCamera(true);

	if (SelectedFrame == EMetaHumanCharacterCameraFrame::Auto)
	{
		SelectedFrame = AutoSelectedFrame;
	}
	LastSelectedFrame = SelectedFrame;

	switch (SelectedFrame)
	{
	case EMetaHumanCharacterCameraFrame::Face:
		if (bInRotate)
		{
			SetViewRotation(FRotator{ 0, 180, 0 });
		}
		FocusOnFace(0.75f, FVector{ 0, 0, 0.4 }, /*bInInstant*/false);
		break;
	case EMetaHumanCharacterCameraFrame::Body:
		if (bInRotate)
		{
			SetViewRotation(FRotator{ 0, 180, 0 });
		}
		FocusOnBody(0.9f, FVector{ 0, 0, 0 }, /*bInInstant*/false);
		break;
	case EMetaHumanCharacterCameraFrame::Far:
		if (bInRotate)
		{
			SetViewRotation(FRotator{ 0, 180, 0 });
		}
		FocusOnBody(1.25f, FVector{ 0, 0, 0 }, /*bInInstant*/false);
		break;
	default:
		break;
	}
}

void FMetaHumanCharacterViewportClient::ChangeRenderQuality(EMetaHumanCharacterRenderingQuality InRenderQuality)
{
	const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
	check(Settings);

	switch (InRenderQuality)
	{
		//TODO : Discuss about these options with someone from tech art and what should be applied, this is more like a skeleton
		case(EMetaHumanCharacterRenderingQuality::Medium):

			PostProcessSettings = Settings->DefaultRenderingQualities[EMetaHumanCharacterRenderingQuality::Medium];
			SetTransmissionForAllLights(false);

			EngineShowFlags.SetDynamicShadows(false);
			EngineShowFlags.SetSubsurfaceScattering(false);
			EngineShowFlags.SetGlobalIllumination(false);
			EngineShowFlags.SetLumenGlobalIllumination(false);
			EngineShowFlags.SetLumenReflections(false);

			SetPreviewingScreenPercentage(true);
			SetPreviewScreenPercentage(50);
			
			break;

		case(EMetaHumanCharacterRenderingQuality::High):

			PostProcessSettings = Settings->DefaultRenderingQualities[EMetaHumanCharacterRenderingQuality::High];

			SetTransmissionForAllLights(true);

			EngineShowFlags.SetDynamicShadows(true);
			EngineShowFlags.SetSubsurfaceScattering(true);
			EngineShowFlags.SetGlobalIllumination(false);
			EngineShowFlags.SetLumenGlobalIllumination(false);
			EngineShowFlags.SetLumenReflections(false);

			SetPreviewingScreenPercentage(true);
			SetPreviewScreenPercentage(70);

			break;

		case(EMetaHumanCharacterRenderingQuality::Epic):

			PostProcessSettings = Settings->DefaultRenderingQualities[EMetaHumanCharacterRenderingQuality::Epic];

			SetTransmissionForAllLights(true);

			EngineShowFlags.SetDynamicShadows(true);
			EngineShowFlags.SetSubsurfaceScattering(true);
			EngineShowFlags.SetGlobalIllumination(true);
			EngineShowFlags.SetLumenGlobalIllumination(true);
			EngineShowFlags.SetLumenReflections(true);

			SetPreviewingScreenPercentage(true);
			SetPreviewScreenPercentage(100);

			break;

		default:

			break;
	}
}

void FMetaHumanCharacterViewportClient::SetTransmissionForAllLights(bool bTransmissionEnabled)
{
	if (UWorld* World = GetPreviewScene()->GetWorld())
	{
		// Only enable transmission if Effects is set to Epic or Cinematic
		static const TConsoleVariableData<int32>* EffectsQualityCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("sg.EffectsQuality"));
		check(EffectsQualityCVar);
		const bool bIsEffectEpicOrHigher = EffectsQualityCVar->GetValueOnAnyThread() >= 3;

		for(TActorIterator<ALight> LightIt(World); LightIt; ++LightIt)
		{
			ALight* Light = *LightIt;
			ULightComponent* LightComp = Light->GetLightComponent();

			if(LightComp)
			{
				LightComp->bTransmission = bTransmissionEnabled && bIsEffectEpicOrHigher;
				LightComp->MarkRenderStateDirty();
			}
		}
	}
}

void FMetaHumanCharacterViewportClient::FocusOnFace(float InDistanceScale, const FVector& InOffset, bool bInInstant)
{
	if (IMetaHumanCharacterEditorActorInterface* CharacterActor = WeakCharacterActor.Get())
	{
		FBoxSphereBounds FaceBounds = CharacterActor->GetFaceComponent()->Bounds;
		FaceBounds.Origin += FaceBounds.BoxExtent * InOffset;
		FaceBounds = FaceBounds.ExpandBy((InDistanceScale - 1.0f) * FaceBounds.SphereRadius);
		FocusViewportOnBox(FaceBounds.GetBox(), bInInstant);
	}
}

void FMetaHumanCharacterViewportClient::FocusOnBody(float InDistanceScale, const FVector& InOffset, bool bInInstant)
{
	if (IMetaHumanCharacterEditorActorInterface* CharacterActor = WeakCharacterActor.Get())
	{
		FBoxSphereBounds FaceBounds = CharacterActor->GetFaceComponent()->Bounds;
		FBoxSphereBounds BodyBounds = CharacterActor->GetBodyComponent()->Bounds;
		FBox Bounds = BodyBounds.GetBox() + FaceBounds.GetBox();
		Bounds = Bounds.ShiftBy(Bounds.GetExtent() * InOffset);
		Bounds = Bounds.ExpandBy((InDistanceScale - 1.0f) * BodyBounds.SphereRadius);
		FocusViewportOnBox(Bounds, bInInstant);
	}
}

void FMetaHumanCharacterViewportClient::SetViewportWidget(const TWeakPtr<SEditorViewport>& InEditorViewportWidget)
{
	EditorViewportWidget = InEditorViewportWidget;
}

void FMetaHumanCharacterViewportClient::DrawInfos(FCanvas* InCanvas, const FText& Title, const TArray<TPair<FText, FText>>& Infos, const FDrawInfoOptions& InDrawInfoOptions) const
{
	const float DPIInvScale = InCanvas->GetDPIScale() > 0 ? (1.0f / InCanvas->GetDPIScale()) : 1.0f;

	const int32 X = InDrawInfoOptions.TopCenter.X;
	int32 Y = InDrawInfoOptions.TopCenter.Y;
	const int32 Padding = InDrawInfoOptions.Padding;

	FCanvasTextItem TextItem(FVector2D(X, Y), Title, GEngine->GetLargeFont(), FLinearColor::White);
	if (InDrawInfoOptions.bTitleLeft)
	{
		TextItem.SetColor(FLinearColor::Transparent);
		InCanvas->DrawItem(TextItem);
		TextItem.SetColor(FLinearColor::White);
		TextItem.bOutlined = true;
		TextItem.Position.X -= TextItem.DrawnSize.X * DPIInvScale;
		InCanvas->DrawItem(TextItem);
	}
	else
	{
		InCanvas->DrawItem(TextItem);
	}

	Y += TextItem.DrawnSize.Y * DPIInvScale + Padding;

	for (const TPair<FText, FText>& Info : Infos)
	{
		FText Key = FText::Format(FText::FromString(TEXT("{0}: ")), Info.Get<0>());
		FCanvasTextItem TextItemKey(FVector2D(X, Y), Key, GEngine->GetSmallFont(), FLinearColor::Transparent);
		
		InCanvas->DrawItem(TextItemKey);
		TextItemKey.Position.X -= TextItemKey.DrawnSize.X * DPIInvScale;
		TextItemKey.SetColor(FLinearColor::Gray);
		InCanvas->DrawItem(TextItemKey);

		FCanvasTextItem TextItemValue(FVector2D(X, Y), Info.Get<1>(), GEngine->GetSmallFont(), FLinearColor::Gray);
		InCanvas->DrawItem(TextItemValue);

		Y += TextItem.DrawnSize.Y * DPIInvScale + Padding;
	}
}

void FMetaHumanCharacterViewportClient::Draw(FViewport* InViewport, FCanvas* InCanvas)
{
	FEditorViewportClient::Draw(InViewport, InCanvas);

	const UMetaHumanCharacter* Character = WeakCharacter.Get();
	const UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();

	if (InCanvas && Character && Subsystem && Character->ViewportSettings.bShowViewportOverlays)
	{
		{
			TArray<TPair<FText, FText>> StatusInfos;

			const FInt32Point FaceAlbedoResolution = Character->GetSynthesizedFaceTexturesResolution(EFaceTextureType::Basecolor);
			const FInt32Point FaceNormalResolution = Character->GetSynthesizedFaceTexturesResolution(EFaceTextureType::Normal);
			const FInt32Point FaceCavityResolution = Character->GetSynthesizedFaceTexturesResolution(EFaceTextureType::Cavity);
			const FInt32Point FaceAnimMapsResolutions = Character->GetSynthesizedFaceTexturesResolution(EFaceTextureType::Basecolor_Animated_CM1);

			const FInt32Point BodyAlbedoResolution = Character->GetSynthesizedBodyTexturesResolution(EBodyTextureType::Body_Basecolor);
			const FInt32Point BodyNormalResolution = Character->GetSynthesizedBodyTexturesResolution(EBodyTextureType::Body_Normal);
			const FInt32Point BodyCavityResolution = Character->GetSynthesizedBodyTexturesResolution(EBodyTextureType::Body_Cavity);
			const FInt32Point BodyMasksResolution = Character->GetSynthesizedBodyTexturesResolution(EBodyTextureType::Body_Underwear_Mask);

			bool bIsUsingTextureOverrides = false;
			
			if (const TSharedRef<FMetaHumanCharacterEditorData>* MetaHumanCharacterEditorData = Subsystem->GetMetaHumanCharacterEditorData(Character))
			{
				if (MetaHumanCharacterEditorData->Get().SkinSettings.IsSet() && MetaHumanCharacterEditorData->Get().SkinSettings.GetValue().bEnableTextureOverrides)
				{
					bIsUsingTextureOverrides = true;
				}
			}
			else if (Character->SkinSettings.bEnableTextureOverrides)
			{
				bIsUsingTextureOverrides = true;
			}

			const FText TextureSourcesInfo = LOCTEXT("TextureSourcesKey", "Texture Sources");

			if (bIsUsingTextureOverrides)
			{
				StatusInfos.Add({ TextureSourcesInfo, LOCTEXT("TextureSourcesOverriden", "Overrides") });
			}
			else
			{
				FText TextureSourceStatus = LOCTEXT("TextureSourceUpToDate", "Up to date");

				if (Character->NeedsToDownloadTextureSources())
				{
					TextureSourceStatus = LOCTEXT("TextureSourcesNeeedsDownload", "Needs download");
				}

				StatusInfos.Add({ INVTEXT("Texture Sources"), TextureSourceStatus });

				auto GetTextureResolutionText = [](const FInt32Point& Res) -> FText
				{
					const int32 ResK = Res.X / 1024;
					if (ResK <= 0)
					{
						return LOCTEXT("TextureSourcesUnavailable", "Unavailable");
					}
					return FText::Format(INVTEXT("{0}k"), ResK);
				};

				const FText FaceTexturesInfo1 = FText::Format(LOCTEXT("FaceTexturesSourcesInfo1", "Albedo: {0} Normal: {1}"),
															 GetTextureResolutionText(FaceAlbedoResolution),
															 GetTextureResolutionText(FaceNormalResolution));

				const FText FaceTexturesInfo2 = FText::Format(LOCTEXT("FaceTexturesSourceInfo2", "Cavity {0}: Anim. Maps: {1}"),
															 GetTextureResolutionText(FaceCavityResolution),
															 GetTextureResolutionText(FaceAnimMapsResolutions));

				const FText BodyTextureInfo1 = FText::Format(LOCTEXT("BodyTexturesSourcesInfo1", "Albedo: {0} Normal: {1}"),
															 GetTextureResolutionText(BodyAlbedoResolution),
															 GetTextureResolutionText(BodyNormalResolution));

				const FText BodyTextureInfo2 = FText::Format(LOCTEXT("BodyTetureSourcesInfo2", "Cavity: {0}, Masks: {1}"),
															 GetTextureResolutionText(BodyCavityResolution),
															 GetTextureResolutionText(BodyMasksResolution));

				StatusInfos.Add({ INVTEXT("Face"), FaceTexturesInfo1});
				StatusInfos.Add({ FText::GetEmpty(), FaceTexturesInfo2 });
				StatusInfos.Add({ INVTEXT("Body"), BodyTextureInfo1 });
				StatusInfos.Add({ FText::GetEmpty(), BodyTextureInfo2 });
			}

			const UMetaHumanCharacterEditorSettings* Settings = GetDefault<UMetaHumanCharacterEditorSettings>();
			if (Settings->bUseVirtualTextures)
			{
				const FText VTEnabledText = Settings->ShouldUseVirtualTextures() ? LOCTEXT("Enabled", "Enabled") : LOCTEXT("Disabled", "Disabled");
				StatusInfos.Add({ LOCTEXT("VirtualTexturesInfo", "Virtual Textures"), VTEnabledText });
			}

			FText RigStateTextValue = LOCTEXT("RigStateUnrigged", "Unrigged");
			if (Character->HasFaceDNA())
			{
				if (Character->HasFaceDNABlendshapes())
				{
					RigStateTextValue = LOCTEXT("RigStateJointsAndBlendshapes", "Joints and Blend Shapes");
				}
				else
				{
					RigStateTextValue = LOCTEXT("RigStateJointsOnly", "Joints Only");
				}
			}
			else if (Subsystem->GetRiggingState(Character) == EMetaHumanCharacterRigState::RigPending)
			{
				RigStateTextValue = LOCTEXT("RigStatePending", "Pending");
			}

			const FText BodyTypeTextValue = (Character->bFixedBodyType) ? LOCTEXT("BodyTypeValueFixed", "Fixed") : LOCTEXT("BodyTypeValueParametric", "Parametric");
			StatusInfos.Add({ LOCTEXT("BodyTypeKey", "Body Type"), BodyTypeTextValue });
			StatusInfos.Add({ LOCTEXT("RigStateKey", "Rig State"), RigStateTextValue });

			FDrawInfoOptions DrawInfoOptions;
			DrawInfoOptions.bTitleLeft = true;
			DrawInfoOptions.TopCenter.X = 140;
			DrawInfoOptions.TopCenter.Y = 20;
			DrawInfos(InCanvas, LOCTEXT("StatusTitle", "Status"), StatusInfos, DrawInfoOptions);
		}

		if (!Shortcuts.IsEmpty())
		{
			FDrawInfoOptions DrawInfoOptions;
			DrawInfoOptions.bTitleLeft = false;
			DrawInfoOptions.TopCenter.X = InViewport->GetSizeXY().X / InCanvas->GetDPIScale()  - 210;
			DrawInfoOptions.TopCenter.Y = 20;
			DrawInfos(InCanvas, LOCTEXT("ShortcutsTitle", "Shortcuts"), Shortcuts, DrawInfoOptions);
		}

		if(!FMetaHumanCharacterEditorModule::IsOptionalMetaHumanContentInstalled())
		{
			FText OptionalContentMissingText = LOCTEXT("OptionalContentMissingViewportMessage", "METAHUMAN CREATOR CORE DATA IS MISSING.");
			FText OptionalContentMissingTextContext = LOCTEXT("OptionalContentMissingViewportContext", "Some features will be unavailable until it's added to your project.");
			FLinearColor TextColor = FLinearColor::Red;
			float FontScale = 1.5f;
			UFont* Font = GEngine->GetMediumFont();
			FVector2D ScreenSize = FVector2D(InViewport->GetSizeXY());
			float X = ScreenSize.X / InCanvas->GetDPIScale() * 0.5f - 200.f;
			float Y = ScreenSize.Y / InCanvas->GetDPIScale() - 50.f;

			FCanvasTextItem OptionalContentTextItem(FVector2D(X, Y), OptionalContentMissingText, Font, TextColor);
			FCanvasTextItem OptionalContentContextTextItem(FVector2D(X - 70.f, Y + 20.f), OptionalContentMissingTextContext, Font, TextColor);

			OptionalContentTextItem.Scale = FVector2D(FontScale, FontScale);
			OptionalContentTextItem.bCentreX = false;
			OptionalContentTextItem.bCentreY = false;
			OptionalContentTextItem.EnableShadow(FLinearColor::Black);
			
			OptionalContentContextTextItem.Scale = FVector2D(FontScale, FontScale);
			OptionalContentContextTextItem.bCentreX = false;
			OptionalContentContextTextItem.bCentreY = false;
			OptionalContentContextTextItem.EnableShadow(FLinearColor::Black);

			InCanvas->DrawItem(OptionalContentTextItem);
			InCanvas->DrawItem(OptionalContentContextTextItem);
		}
	}	
}

void FMetaHumanCharacterViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);

	if (bDrawFaceBones)
	{
		if (WeakCharacterActor.Get())
		{
			DrawDebugBones(WeakCharacterActor.Get()->GetFaceComponent(), /*bIsFace*/ true, PDI);	
		}
	}

	if (bDrawBodyBones)
	{
		if (WeakCharacterActor.Get())
		{
			DrawDebugBones(WeakCharacterActor.Get()->GetBodyComponent(), /*bIsFace*/ false, PDI);
		}
	}

}

void FMetaHumanCharacterViewportClient::ClearShortcuts()
{
	Shortcuts.Reset();
}

void FMetaHumanCharacterViewportClient::SetShortcuts(const TArray<TPair<FText, FText>>& InShortcuts)
{
	Shortcuts = InShortcuts;
}

void FMetaHumanCharacterViewportClient::ShowFaceBonesOnCharacter(const bool InDrawFaceBones)
{
	bDrawFaceBones = InDrawFaceBones;
	Invalidate();
}

void FMetaHumanCharacterViewportClient::ShowBodyBonesOnCharacter(const bool InDrawBodyBones)
{
	bDrawBodyBones = InDrawBodyBones;
	Invalidate();
}

void FMetaHumanCharacterViewportClient::DrawDebugBones(const USkeletalMeshComponent* MetaHumanSkeletalMeshComponent, bool bIsFaceComponent, FPrimitiveDrawInterface* PDI)
{
	FSkelDebugDrawConfig DrawConfig;
	DrawConfig.BoneDrawMode = EBoneDrawMode::All;
	DrawConfig.BoneDrawSize = 1.0f;
	DrawConfig.bForceDraw = true;
	DrawConfig.bAddHitProxy = false;
	DrawConfig.bUseMultiColorAsDefaultColor = true;
	DrawConfig.DefaultBoneColor = FLinearColor::Green;
	DrawConfig.AffectedBoneColor = FLinearColor::Red;
	DrawConfig.SelectedBoneColor = FLinearColor::Blue;
	DrawConfig.ParentOfSelectedBoneColor = FLinearColor::Black;

	TArray<int32> SelectedBones;

	if(bIsFaceComponent)
	{
		const TArray<FTransform>& FaceSpaceTransforms = MetaHumanSkeletalMeshComponent->GetComponentSpaceTransforms();

		const TArray<FBoneIndexType>& DrawFaceBoneIndices = MetaHumanSkeletalMeshComponent->RequiredBones;
		for (int32 Index = 0; Index < DrawFaceBoneIndices.Num(); ++Index)
		{
			const int32 BoneIndex = DrawFaceBoneIndices[Index];
			FaceBonesWorldTransforms[BoneIndex] = FaceSpaceTransforms[BoneIndex] * MetaHumanSkeletalMeshComponent->GetComponentTransform();
			FaceBoneColors[BoneIndex] = SkeletalDebugRendering::GetSemiRandomColorForBone(Index);
		}

		SkeletalDebugRendering::DrawBones(
			PDI,
			MetaHumanSkeletalMeshComponent->GetComponentLocation(),
			MetaHumanSkeletalMeshComponent->RequiredBones,
			MetaHumanSkeletalMeshComponent->GetSkeletalMeshAsset()->GetRefSkeleton(),
			FaceBonesWorldTransforms,
			SelectedBones,
			FaceBoneColors,
			HitProxies,
			DrawConfig
		);
	}
	else
	{
		const TArray<FTransform>& BodySpaceTransforms = MetaHumanSkeletalMeshComponent->GetComponentSpaceTransforms();

		const TArray<FBoneIndexType>& DrawBodyBoneIndices = MetaHumanSkeletalMeshComponent->RequiredBones;
		for (int32 Index = 0; Index < DrawBodyBoneIndices.Num(); ++Index)
		{
			const int32 BoneIndex = DrawBodyBoneIndices[Index];
			BodyBonesWorldTransforms[BoneIndex] = BodySpaceTransforms[BoneIndex] * MetaHumanSkeletalMeshComponent->GetComponentTransform();
			BodyBoneColors[BoneIndex] = SkeletalDebugRendering::GetSemiRandomColorForBone(Index);
		}

		SkeletalDebugRendering::DrawBones(
			PDI,
			MetaHumanSkeletalMeshComponent->GetComponentLocation(),
			MetaHumanSkeletalMeshComponent->RequiredBones,
			MetaHumanSkeletalMeshComponent->GetSkeletalMeshAsset()->GetRefSkeleton(),
			BodyBonesWorldTransforms,
			SelectedBones,
			BodyBoneColors,
			HitProxies,
			DrawConfig
		);
	}
}

void FMetaHumanCharacterViewportClient::ShowFaceNormalsOnCharacter(const bool InShowFaceNormals)
{
	WeakCharacterActor.Get()->SetShowNormalsOnFace(InShowFaceNormals);
	Invalidate();
}

void FMetaHumanCharacterViewportClient::ShowBodyNormalsOnCharacter(const bool InShowBodyNormals)
{
	WeakCharacterActor.Get()->SetShowNormalsOnBody(InShowBodyNormals);
	Invalidate();
}

void FMetaHumanCharacterViewportClient::ShowFaceTangentsOnCharacter(const bool InShowFaceTangents)
{
	WeakCharacterActor.Get()->SetShowTangentsOnFace(InShowFaceTangents);
	Invalidate();
}

void FMetaHumanCharacterViewportClient::ShowBodyTangentsOnCharacter(const bool InShowBodyTangents)
{
	WeakCharacterActor.Get()->SetShowTangentsOnBody(InShowBodyTangents);
	Invalidate();
}

void FMetaHumanCharacterViewportClient::ShowFaceBinormalsOnCharacter(const bool InShowFaceBinormals)
{
	WeakCharacterActor.Get()->SetShowBinormalsOnFace(InShowFaceBinormals);
	Invalidate();
}

void FMetaHumanCharacterViewportClient::ShowBodyBinormalsOnCharacter(const bool InShowBodyBinormals)
{
	WeakCharacterActor.Get()->SetShowBinormalsOnBody(InShowBodyBinormals);
	Invalidate();
}

void FMetaHumanCharacterViewportClient::MouseMove(FViewport* InViewport, int32 X, int32 Y)
{
	FEditorViewportClient::MouseMove(InViewport, X, Y);
	PreviousMousePosition.Reset();
	NextMousePosition.Reset();
}

void FMetaHumanCharacterViewportClient::ProcessAccumulatedPointerInput(FViewport* InViewport)
{
	if (PreviousMousePosition.IsSet() && NextMousePosition.IsSet() && InViewport->KeyState(EKeys::LeftMouseButton) && InViewport->KeyState(EKeys::L) && InViewport->GetSizeXY().X > 0)
	{
		UMetaHumanCharacter* Character = WeakCharacter.Get();
		const UMetaHumanCharacterEditorSubsystem* Subsystem = UMetaHumanCharacterEditorSubsystem::Get();
		if (Character && Subsystem)
		{
			float LightRotation = Character->ViewportSettings.LightRotation;
			int32 Delta = NextMousePosition.GetValue().X - PreviousMousePosition.GetValue().X;
			if (Delta != 0)
			{
				LightRotation += float(Delta) / float(InViewport->GetSizeXY().X) * 360.0f;
				if (LightRotation > 180.0f) LightRotation -= 360.0f;
				Subsystem->UpdateLightRotation(Character, LightRotation);
				Invalidate();
			}
		}
	}
	PreviousMousePosition = NextMousePosition;

	FEditorViewportClient::ProcessAccumulatedPointerInput(InViewport);
}
void FMetaHumanCharacterViewportClient::CapturedMouseMove(FViewport* InViewport, int32 X, int32 Y)
{
	if (InViewport->KeyState(EKeys::LeftMouseButton) && InViewport->KeyState(EKeys::L) && InViewport->GetSizeXY().X > 0)
	{
		if (!PreviousMousePosition.IsSet()) PreviousMousePosition = FInt32Point(X, Y);
		NextMousePosition = FInt32Point(X, Y);
	}
	else
	{
		PreviousMousePosition.Reset();
		NextMousePosition.Reset();
		FEditorViewportClient::CapturedMouseMove(InViewport, X, Y);
	}
}

#undef LOCTEXT_NAMESPACE
