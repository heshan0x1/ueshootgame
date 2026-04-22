// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowColorRamp.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DataflowColorRamp)

FDataflowColorCurveOwner::FDataflowColorCurveOwner()
{
	ConstCurves.SetNum(4);
	Curves.SetNum(4);

	RichCurves.SetNum(4);

	for (int32 Idx = 0; Idx < 4; ++Idx)
	{
		Curves[Idx].CurveToEdit = &RichCurves[Idx];
		ConstCurves[Idx].CurveToEdit = &RichCurves[Idx];
	}
}

bool FDataflowColorCurveOwner::IsEmpty() const
{
	return (RichCurves.Num() == 0 || RichCurves[0].IsEmpty());
}

FDataflowColorCurveOwner::FDataflowColorCurveOwner(const FDataflowColorCurveOwner& Other)
{
	this->operator=(Other);
}

FDataflowColorCurveOwner& FDataflowColorCurveOwner::operator=(const FDataflowColorCurveOwner& Other)
{
	ConstCurves.SetNum(4);
	Curves.SetNum(4);

	RichCurves.SetNum(4);

	for (int32 Idx = 0; Idx < 4; ++Idx)
	{
		if (Other.RichCurves.IsValidIndex(Idx))
		{
			RichCurves[Idx] = Other.RichCurves[Idx];
		}
		Curves[Idx].CurveToEdit = &RichCurves[Idx];
		ConstCurves[Idx].CurveToEdit = &RichCurves[Idx];
	}
	return *this;
}

void FDataflowColorCurveOwner::SetColorAtTime(float Time, const FLinearColor& Color, bool bOnlyRBG)
{
	if (ensure(Curves.Num() == 4))
	{
		Curves[0].CurveToEdit->AddKey(Time, Color.R);
		Curves[1].CurveToEdit->AddKey(Time, Color.G);
		Curves[2].CurveToEdit->AddKey(Time, Color.B);
		if (!bOnlyRBG)
		{
			Curves[3].CurveToEdit->AddKey(Time, Color.A);
		}
	}
}

TArray<FRichCurveEditInfoConst> FDataflowColorCurveOwner::GetCurves() const
{
	return ConstCurves;
}

void FDataflowColorCurveOwner::GetCurves(TAdderReserverRef<FRichCurveEditInfoConst> OutCurves) const
{
	check(OutCurves.Num() == 0);
	OutCurves.Reserve(ConstCurves.Num());
	for (const FRichCurveEditInfoConst& ConstCurve : ConstCurves)
	{
		OutCurves.Add(ConstCurve);
	}
}

TArray<FRichCurveEditInfo> FDataflowColorCurveOwner::GetCurves()
{
	return Curves;
}

void FDataflowColorCurveOwner::ModifyOwner()
{}

TArray<const UObject*> FDataflowColorCurveOwner::GetOwners() const
{
	return Owners;
}

void FDataflowColorCurveOwner::MakeTransactional()
{}

void FDataflowColorCurveOwner::OnCurveChanged(const TArray<FRichCurveEditInfo>& ChangedCurveEditInfos)
{
	TArray<FRichCurve*> ChangedCurves;
	for (const FRichCurveEditInfo& ChangedCurveEditInfo : ChangedCurveEditInfos)
	{
		ChangedCurves.Add((FRichCurve*)ChangedCurveEditInfo.CurveToEdit);
	}
	if (ChangedCurves.Num() > 0)
	{
		OnColorCurveChangedDelegate.Broadcast(ChangedCurves);
	}
}

bool FDataflowColorCurveOwner::IsLinearColorCurve() const
{
	return true;
}

FLinearColor FDataflowColorCurveOwner::GetLinearColorValue(float InTime) const
{
	return FLinearColor(
		Curves[0].CurveToEdit->Eval(InTime),
		Curves[1].CurveToEdit->Eval(InTime),
		Curves[2].CurveToEdit->Eval(InTime),
		Curves[3].CurveToEdit->Eval(InTime));
}

bool FDataflowColorCurveOwner::HasAnyAlphaKeys() const
{
	return Curves[3].CurveToEdit->GetNumKeys() != 0;
}

bool FDataflowColorCurveOwner::IsValidCurve(FRichCurveEditInfo CurveInfo)
{
	return Curves.Contains(CurveInfo);
}

FLinearColor FDataflowColorCurveOwner::GetCurveColor(FRichCurveEditInfo CurveInfo) const
{
	return FLinearColor::White;
}

