// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "USDGeomXformableTranslator.h"

#include "UsdWrappers/SdfPath.h"

#define UE_API USDSCHEMAS_API

#if USE_USD_SDK

class FUsdGeomPrimitiveTranslator : public FUsdGeomXformableTranslator
{
public:
	using Super = FUsdGeomXformableTranslator;
	using FUsdGeomXformableTranslator::FUsdGeomXformableTranslator;

	FUsdGeomPrimitiveTranslator(const FUsdGeomPrimitiveTranslator& Other) = delete;
	FUsdGeomPrimitiveTranslator& operator=(const FUsdGeomPrimitiveTranslator& Other) = delete;

	UE_API virtual void CreateAssets() override;
	UE_API virtual USceneComponent* CreateComponents() override;
	UE_API virtual void UpdateComponents(USceneComponent* SceneComponent) override;

	UE_API virtual bool CollapsesChildren(ECollapsingType CollapsingType) const override;
	UE_API virtual bool CanBeCollapsed(ECollapsingType CollapsingType) const override;

	UE_API virtual TSet<UE::FSdfPath> CollectAuxiliaryPrims() const override;
};

#endif	  // #if USE_USD_SDK

#undef UE_API
