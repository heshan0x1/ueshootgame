// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/STaggedAssetBrowserCreateAssetWindow.h"

#include "ContentBrowserModule.h"
#include "EditorDirectories.h"
#include "Widgets/Layout/SSpacer.h"

#define LOCTEXT_NAMESPACE "TaggedAssetBrowser"

void STaggedAssetBrowserCreateAssetWindow::Construct(const FArguments& InArgs, const UTaggedAssetBrowserConfiguration& Configuration, UClass& InCreatedClass)
{
	CreatedClass = &InCreatedClass;
	AdditionalFactorySettingsClass = InArgs._AdditionalFactorySettingsClass;
	bAllowEmptyAssetCreation = InArgs._bAllowEmptyAssetCreation;

	FArguments Args = InArgs;

	TSharedRef<SWidget> CreateAssetControls = SNew(SBox)
		.Padding(16.f, 16.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(5.f, 1.f)
			.HAlign(HAlign_Left)
			[
				SNew(SButton)
				.OnClicked(this, &STaggedAssetBrowserCreateAssetWindow::Proceed)
				.Visibility(bAllowEmptyAssetCreation ? EVisibility::Visible : EVisibility::Hidden)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2.f)
					[
						SNew(SImage)
						.Image(FSlateIconFinder::FindIconForClass(CreatedClass.Get()).GetIcon())
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2.f)
					[
						SNew(STextBlock).Text(FText::FormatOrdered(LOCTEXT("CreateEmptyAssetButtonLabel", "Create Empty {0}"), CreatedClass->GetDisplayNameText()))
					]
				]
			]
			+ SHorizontalBox::Slot()
			[
				SNew(SSpacer)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(5.f, 1.f)
			[
				SNew(SButton)
				.ContentPadding(FMargin(50.f, 4.f, 50.f, 1.f))
				.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
				.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("PrimaryButtonText"))
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("CreatePrimaryButtonLabel", "Create"))
				.OnClicked(this, &STaggedAssetBrowserCreateAssetWindow::Proceed)
				.IsEnabled(this, &STaggedAssetBrowserCreateAssetWindow::HasSelectedAssets)
				.ToolTipText(this, &STaggedAssetBrowserCreateAssetWindow::GetCreateButtonTooltip)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(5.f, 1.f)
			[
				SNew(SButton)
				.ContentPadding(FMargin(50.f, 4.f, 50.f, 1.f))
				.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("PrimaryButtonText"))
				.HAlign(HAlign_Center)
				.Text(LOCTEXT("CancelButtonLabel", "Cancel"))
				.OnClicked(this, &STaggedAssetBrowserCreateAssetWindow::Cancel)
			]
		];

	if(AdditionalFactorySettingsClass.IsValid())
	{
		FTaggedAssetBrowserCustomTabInfo FactoryTabInfo;
		FactoryTabInfo.Title = LOCTEXT("FactoryTabTitle", "Settings");
		FactoryTabInfo.Icon = FAppStyle::GetBrush("Icons.Settings");
		FactoryTabInfo.OnGetTabContent = FOnGetContent::CreateSP(this, &STaggedAssetBrowserCreateAssetWindow::CreateFactorySettingsTab);

		Args._AssetBrowserWindowArgs._AssetBrowserArgs._CustomTabInfos = { FactoryTabInfo };
	}
	
	Args._AssetBrowserWindowArgs._AssetBrowserArgs._AdditionalBottomWidget = CreateAssetControls;
	Args._AssetBrowserWindowArgs._AssetBrowserArgs._OnAssetsActivated = FOnAssetsActivated::CreateSP(this, &STaggedAssetBrowserCreateAssetWindow::OnAssetsActivatedInternal);

	// If we haven't specified any referencing assets, we assume the context is the last "save asset" editor path, which should be set before this is called
	if(Args._AssetBrowserWindowArgs._AssetBrowserArgs._AdditionalReferencingAssets.Num() == 0)
	{
		Args._AssetBrowserWindowArgs._AssetBrowserArgs._AdditionalReferencingAssets = { FAssetData("TmpAsset", FName(FEditorDirectories::Get().GetLastDirectory(ELastDirectory::NEW_ASSET)), "TmpAsset", UObject::StaticClass()->GetClassPathName()) };  
	}
	
	STaggedAssetBrowserWindow::Construct(Args._AssetBrowserWindowArgs, Configuration);
}

STaggedAssetBrowserCreateAssetWindow::STaggedAssetBrowserCreateAssetWindow()
{
}

STaggedAssetBrowserCreateAssetWindow::~STaggedAssetBrowserCreateAssetWindow()
{
}

void STaggedAssetBrowserCreateAssetWindow::OnAssetsActivatedInternal(const TArray<FAssetData>& AssetData, EAssetTypeActivationMethod::Type)
{
	bProceedWithAction = true;
}

FReply STaggedAssetBrowserCreateAssetWindow::Proceed()
{
	// The assets don't matter here as the factories making use of this window will retrieve the selected assets afterwards
	OnAssetsActivated({}, EAssetTypeActivationMethod::Opened);
	return FReply::Handled();
}

FReply STaggedAssetBrowserCreateAssetWindow::Cancel()
{
	bProceedWithAction = false;
	RequestDestroyWindow();
	return FReply::Handled();
}

FText STaggedAssetBrowserCreateAssetWindow::GetCreateButtonTooltip() const
{
	return HasSelectedAssets()
	? FText::FormatOrdered(LOCTEXT("CreateAssetButtonTooltip_Enabled", "Create a new {0} with selected asset {1}"), CreatedClass->GetDisplayNameText(), FText::FromName(GetSelectedAssets()[0].AssetName))
	: FText::FormatOrdered(LOCTEXT("CreateAssetButtonTooltip_Disabled", "Please select an asset as a base for your new effect.{0}"), bAllowEmptyAssetCreation ? LOCTEXT("CreateAssetButtonTooltip_Disabled_AllowsEmptyCreation", " Alternatively, create an empty asset.") : FText::GetEmpty());
}

TSharedRef<SWidget> STaggedAssetBrowserCreateAssetWindow::CreateFactorySettingsTab()
{
	if(AdditionalFactorySettingsClass.IsValid())
	{
		FactorySettingsObject = NewObject<UObject>(GetTransientPackage(), AdditionalFactorySettingsClass.Get(), NAME_None, RF_Transient);
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

		FDetailsViewArgs Args;
		Args.bShowObjectLabel = false;
		Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;

		FactorySettingsWidget = PropertyEditorModule.CreateDetailView(Args);
		FactorySettingsWidget->SetObject(FactorySettingsObject);
		
		return FactorySettingsWidget.ToSharedRef();
	}

	return SNullWidget::NullWidget;
}

FString STaggedAssetBrowserCreateAssetWindow::GetReferencerName() const
{
	return TEXT("STaggedAssetBrowserCreateAsset");
}

void STaggedAssetBrowserCreateAssetWindow::AddReferencedObjects(FReferenceCollector& Collector)
{
	FSlateInvalidationRoot::AddReferencedObjects(Collector);
	
	if(FactorySettingsObject)
	{
		Collector.AddReferencedObject(FactorySettingsObject);
	}
}

#undef LOCTEXT_NAMESPACE
