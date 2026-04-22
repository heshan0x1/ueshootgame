// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDGeomXformableTranslator.h"

#define UE_API USDSCHEMAS_API

#if USE_USD_SDK

class FUsdGeomCameraTranslator : public FUsdGeomXformableTranslator
{
public:
	using FUsdGeomXformableTranslator::FUsdGeomXformableTranslator;

	UE_API virtual USceneComponent* CreateComponents() override;
	UE_API virtual void UpdateComponents(USceneComponent* SceneComponent) override;

	UE_API virtual bool CollapsesChildren(ECollapsingType CollapsingType) const override;
	UE_API virtual bool CanBeCollapsed(ECollapsingType CollapsingType) const override;
};

#endif	  // #if USE_USD_SDK

#undef UE_API
