// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "Curves/RichCurve.h"
#include "Curves/CurveOwnerInterface.h"

#include "DataflowColorRamp.generated.h"

struct FDataflowColorCurveOwner : public FCurveOwnerInterface
{
public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnColorCurveChanged, TArray<FRichCurve*> /* ChangedCurves */);

public:
	DATAFLOWENGINE_API FDataflowColorCurveOwner();
	DATAFLOWENGINE_API FDataflowColorCurveOwner(const FDataflowColorCurveOwner& Other);
	DATAFLOWENGINE_API FDataflowColorCurveOwner& operator=(const FDataflowColorCurveOwner& Other);

	FDataflowColorCurveOwner(FDataflowColorCurveOwner&& Other) = delete;
	FDataflowColorCurveOwner& operator=(FDataflowColorCurveOwner&& Other) = delete;

	DATAFLOWENGINE_API void SetColorAtTime(float Time, const FLinearColor& Color, bool bOnlyRBG);

	DATAFLOWENGINE_API bool IsEmpty() const;

	/** implemenmt all the necessary virtual function from FCurveOwnerInterface */
	DATAFLOWENGINE_API virtual TArray<FRichCurveEditInfoConst> GetCurves() const override;
	DATAFLOWENGINE_API virtual void GetCurves(TAdderReserverRef<FRichCurveEditInfoConst> Curves) const override;
	DATAFLOWENGINE_API virtual TArray<FRichCurveEditInfo> GetCurves() override;
	DATAFLOWENGINE_API virtual void ModifyOwner() override;
	DATAFLOWENGINE_API virtual TArray<const UObject*> GetOwners() const override;
	DATAFLOWENGINE_API virtual void MakeTransactional() override;
	DATAFLOWENGINE_API virtual void OnCurveChanged(const TArray<FRichCurveEditInfo>& ChangedCurveEditInfos) override;
	DATAFLOWENGINE_API virtual bool IsLinearColorCurve() const override;
	DATAFLOWENGINE_API virtual FLinearColor GetLinearColorValue(float InTime) const override;
	DATAFLOWENGINE_API virtual bool HasAnyAlphaKeys() const override;
	DATAFLOWENGINE_API virtual bool IsValidCurve(FRichCurveEditInfo CurveInfo) override;
	DATAFLOWENGINE_API virtual FLinearColor GetCurveColor(FRichCurveEditInfo CurveInfo) const override;

	FOnColorCurveChanged OnColorCurveChangedDelegate;

private:
	TArray<FRichCurveEditInfoConst> ConstCurves;
	TArray<FRichCurveEditInfo> Curves;
	TArray<const UObject*> Owners;

	TArray<FRichCurve> RichCurves;
};

USTRUCT()
struct FDataflowColorRamp
{
	GENERATED_USTRUCT_BODY()

	FDataflowColorCurveOwner ColorRamp;
};