// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "IDetailsView.h"

#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"

#include "Widgets/Layout/SScrollBox.h"

template<class FObjectType>
class SMetaHumanCalibrationObjectWidget : public SCompoundWidget
{
public:

	DECLARE_DELEGATE_OneParam(FOnObjectChanged, const FPropertyChangedEvent&);

	SLATE_BEGIN_ARGS(SMetaHumanCalibrationObjectWidget)
		: _Object(nullptr)
		, _OnObjectChanged(nullptr)
		{
		}

		SLATE_ARGUMENT(TWeakObjectPtr<FObjectType>, Object)
		SLATE_EVENT(FOnObjectChanged, OnObjectChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		check(InArgs._Object.Get());
		OnObjectChanged = InArgs._OnObjectChanged;

		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::ENameAreaSettings::HideNameArea;
		DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
		DetailsViewArgs.bShowPropertyMatrixButton = false;

		FPropertyEditorModule& PropertyEditorModule =
			FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

		ChildSlot
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					DetailsView->AsShared()
				]
			];

		DetailsView->SetObject(InArgs._Object.Get());
		DetailsView->OnFinishedChangingProperties().AddSP(this, &SMetaHumanCalibrationObjectWidget::OnDetailsPropertyChanged);
	}

	void Reset(TWeakObjectPtr<FObjectType> InObject)
	{
		DetailsView->SetObject(InObject.Get());

		DetailsView->ForceRefresh();
	}

	void Refresh()
	{
		DetailsView->ForceRefresh();
	}

private:

	void OnDetailsPropertyChanged(const FPropertyChangedEvent& InPropertyChangedEvent)
	{
		OnObjectChanged.ExecuteIfBound(InPropertyChangedEvent);
	}

	FOnObjectChanged OnObjectChanged;
	TSharedPtr<IDetailsView> DetailsView;
};