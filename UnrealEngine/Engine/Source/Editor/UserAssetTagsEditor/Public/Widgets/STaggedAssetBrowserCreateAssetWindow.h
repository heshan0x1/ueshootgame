// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MRUFavoritesList.h"
#include "STaggedAssetBrowser.h"
#include "STaggedAssetBrowserContent.h"
#include "Editor/EditorEngine.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

#define UE_API USERASSETTAGSEDITOR_API

class STaggedAssetBrowserCreateAssetWindow : public STaggedAssetBrowserWindow
{
public:
	SLATE_BEGIN_ARGS(STaggedAssetBrowserCreateAssetWindow)
		: _AdditionalFactorySettingsClass(nullptr)
		, _bAllowEmptyAssetCreation(true)
		{
		}
		SLATE_ARGUMENT(STaggedAssetBrowserWindow::FArguments, AssetBrowserWindowArgs)
		/** An additional factory settings class can be specified for additional factory information. */
		SLATE_ARGUMENT(UClass*, AdditionalFactorySettingsClass)
		SLATE_ARGUMENT(bool, bAllowEmptyAssetCreation)
	SLATE_END_ARGS()

	UE_API void Construct(const FArguments& InArgs, const UTaggedAssetBrowserConfiguration& Configuration, UClass& CreatedClass);

	UE_API STaggedAssetBrowserCreateAssetWindow();
	UE_API virtual ~STaggedAssetBrowserCreateAssetWindow() override;
	
	bool ShouldProceedWithAction() const { return bProceedWithAction; }

	/** Retrieve the factory settings object, if available. This can be used to further customize the factory process. */
	UObject* RetrieveFactorySettings() const { return FactorySettingsObject; }
private:
	/** The function that will be called by our buttons or by the asset picker itself if double-clicking, hitting enter etc. */
	void OnAssetsActivatedInternal(const TArray<FAssetData>& AssetData, EAssetTypeActivationMethod::Type);
	
	FReply Proceed();
	FReply Cancel();

	FText GetCreateButtonTooltip() const;
	
	TSharedRef<SWidget> CreateFactorySettingsTab();

	UE_API virtual FString GetReferencerName() const override;
	UE_API virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
private:
	TWeakObjectPtr<UClass> CreatedClass;
	TWeakObjectPtr<UClass> AdditionalFactorySettingsClass;

	TObjectPtr<UObject> FactorySettingsObject;
	TSharedPtr<class IDetailsView> FactorySettingsWidget;
	
	bool bProceedWithAction = false;
	bool bAllowEmptyAssetCreation = false;
};

#undef UE_API
