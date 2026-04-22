// Copyright Epic Games, Inc. All Rights Reserved.

#include "Injection/InjectionUtils.h"
#include "GraphInterfaces/AnimNextNativeDataInterface_AnimSequencePlayer.h"
#include "Animation/AnimSequence.h"
#include "Component/AnimNextComponent.h"
#include "Logging/StructuredLog.h"

namespace UE::UAF
{
	FInjectionRequestPtr FInjectionUtils::Inject(UAnimNextComponent* InComponent, FInjectionRequestArgs&& InArgs, FInjectionLifetimeEvents&& InLifetimeEvents)
	{
		return Inject(InComponent, InComponent->GetModuleHandle(), MoveTemp(InArgs), MoveTemp(InLifetimeEvents));
	}

	FInjectionRequestPtr FInjectionUtils::Inject(UObject* InHost, FModuleHandle InHandle, FInjectionRequestArgs&& InArgs, FInjectionLifetimeEvents&& InLifetimeEvents)
	{
		check(IsInGameThread());

		FInjectionRequestPtr Request = MakeInjectionRequest();
		if(Request->Inject(MoveTemp(InArgs), MoveTemp(InLifetimeEvents), InHost, InHandle))
		{
			return Request;
		}
		return nullptr;
	}

	void FInjectionUtils::Uninject(FInjectionRequestPtr InInjectionRequest)
	{
		if(!InInjectionRequest.IsValid())
		{
			return;
		}
		InInjectionRequest->Uninject();
	}

	FInjectionRequestPtr FInjectionUtils::PlayAnim(
		UAnimNextComponent* InComponent,
		const FInjectionSite& InSite,
		UAnimSequence* InAnimSequence,
		float InPlayRate,
		float InStartPosition,
		const FInjectionBlendSettings& InBlendInSettings,
		const FInjectionBlendSettings& InBlendOutSettings,
		FInjectionLifetimeEvents&& InLifetimeEvents)
	{
		return PlayAnimHandle(
			InComponent,
			InComponent->GetModuleHandle(),
			InSite,
			InAnimSequence,
			{
				.PlayRate = InPlayRate,
				.StartPosition = InStartPosition
			},
			InBlendInSettings,
			InBlendOutSettings,
			MoveTemp(InLifetimeEvents));
	}

	FInjectionRequestPtr FInjectionUtils::PlayAnim(
		UAnimNextComponent* InComponent,
		const FInjectionSite& InSite,
		UAnimSequence* InAnimSequence,
		FPlayAnimArgs&& InArgs,
		const FInjectionBlendSettings& InBlendInSettings,
		const FInjectionBlendSettings& InBlendOutSettings,
		FInjectionLifetimeEvents&& InLifetimeEvents)
	{
		return PlayAnimHandle(
			InComponent,
			InComponent->GetModuleHandle(),
			InSite,
			InAnimSequence,
			MoveTemp(InArgs),
			InBlendInSettings,
			InBlendOutSettings,
			MoveTemp(InLifetimeEvents));
	}

	FInjectionRequestPtr FInjectionUtils::PlayAnimHandle(
		UObject* InHost,
		FModuleHandle InModuleHandle,
		const FInjectionSite& InSite,
		UAnimSequence* InAnimSequence,
		FPlayAnimArgs&& InArgs,
		const FInjectionBlendSettings& InBlendInSettings,
		const FInjectionBlendSettings& InBlendOutSettings,
		FInjectionLifetimeEvents&& InLifetimeEvents)
	{
		check(IsInGameThread());

		FInjectionRequestArgs RequestArgs;
		RequestArgs.Site = InSite;
		RequestArgs.Object = InAnimSequence;
		RequestArgs.Type = EAnimNextInjectionType::InjectObject;
		RequestArgs.BlendInSettings = InBlendInSettings;
		RequestArgs.BlendOutSettings = InBlendOutSettings;
		RequestArgs.LifetimeType = InArgs.LifetimeType;

		if (!InArgs.FactoryParams.IsValid())
		{
			RequestArgs.FactoryParams =
				FAnimNextFactoryParams()
				.PushPublicTrait(FSequencePlayerData(
					{
						.AnimSequence = InAnimSequence,
						.PlayRate = InArgs.PlayRate,
						.StartPosition = InArgs.StartPosition,
						.bLoop = [&InArgs, &InAnimSequence]()
						{
							switch (InArgs.LoopMode)
							{
							case ELoopMode::Auto:
								return InAnimSequence->bLoop;
							case ELoopMode::ForceLoop:
								return true;
							case ELoopMode::ForceNonLoop:
								return false;
							}
							return false;
						}()
					}));
		}
		else
		{
			RequestArgs.FactoryParams = MoveTemp(InArgs.FactoryParams);
		}

		return Inject(InHost, InModuleHandle, MoveTemp(RequestArgs), MoveTemp(InLifetimeEvents));
	}

	FInjectionRequestPtr FInjectionUtils::InjectAsset(
		UAnimNextComponent* InComponent,
		const FInjectionSite& InSite,
		UObject* InAsset,
		FAnimNextFactoryParams&& InFactoryParams,
		const FInjectionBlendSettings& InBlendInSettings,
		const FInjectionBlendSettings& InBlendOutSettings,
		FInjectionLifetimeEvents&& InLifetimeEvents)
	{
		return InjectAsset(
			InComponent,
			InComponent->GetModuleHandle(),
			InSite,
			InAsset,
			MoveTemp(InFactoryParams),
			InBlendInSettings,
			InBlendOutSettings,
			MoveTemp(InLifetimeEvents));
	}

	FInjectionRequestPtr FInjectionUtils::InjectAsset(
		UObject* InHost,
		FModuleHandle InModuleHandle,
		const FInjectionSite& InSite,
		UObject* InAsset,
		FAnimNextFactoryParams&& InFactoryParams,
		const FInjectionBlendSettings& InBlendInSettings,
		const FInjectionBlendSettings& InBlendOutSettings,
		FInjectionLifetimeEvents&& InLifetimeEvents)
	{
		check(IsInGameThread());

		FInjectionRequestArgs RequestArgs;
		RequestArgs.Site = InSite;
		RequestArgs.Object = InAsset;
		RequestArgs.Type = EAnimNextInjectionType::InjectObject;
		RequestArgs.BlendInSettings = InBlendInSettings;
		RequestArgs.BlendOutSettings = InBlendOutSettings;
		RequestArgs.FactoryParams = MoveTemp(InFactoryParams);

		return Inject(InHost, InModuleHandle, MoveTemp(RequestArgs), MoveTemp(InLifetimeEvents));
	}

	FInjectionRequestPtr FInjectionUtils::InjectEvaluationModifier(
		UAnimNextComponent* InComponent,
		const TSharedRef<IEvaluationModifier>& InEvaluationModifier,
		const FInjectionSite& InSite)
	{
		return InjectEvaluationModifier(
			InComponent,
			InComponent->GetModuleHandle(),
			InEvaluationModifier,
			InSite);
	}

	FInjectionRequestPtr FInjectionUtils::InjectEvaluationModifier(
		UObject* InHost,
		FModuleHandle InModuleHandle,
		const TSharedRef<IEvaluationModifier>& InEvaluationModifier,
		const FInjectionSite& InSite)
	{
		check(IsInGameThread());

		FInjectionRequestArgs RequestArgs;
		RequestArgs.Site = InSite;
		RequestArgs.Type = EAnimNextInjectionType::EvaluationModifier;
		RequestArgs.EvaluationModifier = InEvaluationModifier;

		return Inject(InHost, InModuleHandle, MoveTemp(RequestArgs));
	}
}
