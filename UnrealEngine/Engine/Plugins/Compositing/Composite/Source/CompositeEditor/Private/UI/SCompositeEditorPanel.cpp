// Copyright Epic Games, Inc. All Rights Reserved.

#include "SCompositeEditorPanel.h"

#include "CompositeActor.h"
#include "CompositeEditorStyle.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailCustomization.h"
#include "LevelEditor.h"
#include "SCompositePanelLayerTree.h"
#include "ScopedTransaction.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"

#define LOCTEXT_NAMESPACE "SCompositeEditorPanel"

namespace CompositingEditorPanel
{
	static const FName CompositeEditorTabName = "CompositeEditorTab";

	TSharedRef<SDockTab> CreateTab(const FSpawnTabArgs& InArgs)
	{
		return SNew(SDockTab)
			.Label(LOCTEXT("PanelTabLabel", "Composure"))
			.TabRole(ETabRole::PanelTab)
			[
				SNew(SCompositeEditorPanel)
			];
	}
}

class FResolutionTypeCustomization : public IPropertyTypeCustomization
{
private:
	struct FNamedResolution
	{
		FText Name = FText::GetEmpty();
		FIntPoint Resolution = FIntPoint::ZeroValue;
		FText ToolTip = FText::GetEmpty();
			
		FNamedResolution(const FText& InName, const FIntPoint& InResolution, const FText& InToolTip)
			: Name(InName)
			, Resolution(InResolution)
			, ToolTip(InToolTip)
		{ }
	};
	using FNamedResolutionPtr = TSharedPtr<FNamedResolution>;
	
	static TArray<FNamedResolutionPtr> NamedResolutions;
	
public:
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InCustomizationUtils) override
	{
		PropertyHandle = InPropertyHandle;

		XPropertyHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FIntPoint, X));
		YPropertyHandle = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FIntPoint, Y));

		FIntPoint InitialResolution = FIntPoint::ZeroValue;
		FPropertyAccess::Result XResult = XPropertyHandle->GetValue(InitialResolution.X);
		FPropertyAccess::Result YResult = YPropertyHandle->GetValue(InitialResolution.Y);

		int32 InitialResolutionIndex = NamedResolutions.IndexOfByPredicate([&InitialResolution](const FNamedResolutionPtr& NamedResolution)
		{
			return NamedResolution->Resolution == InitialResolution;
		});

		if (InitialResolutionIndex == INDEX_NONE)
		{
			InitialResolutionIndex = NamedResolutions.Num() - 1;
		}
		
		InHeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SAssignNew(ResolutionComboBox, SComboBox<FNamedResolutionPtr>)
			.OptionsSource(&NamedResolutions)
			.OnGenerateWidget_Lambda([](FNamedResolutionPtr InItem)
			{
				return SNew(STextBlock)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text(InItem->Name)
					.ToolTipText(InItem->ToolTip);
			})
			.OnSelectionChanged(this, &FResolutionTypeCustomization::SetResolution)
			.InitiallySelectedItem(NamedResolutions[InitialResolutionIndex])
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(this, &FResolutionTypeCustomization::GetResolutionText)
			]
		];

		InHeaderRow.OverrideResetToDefault(FResetToDefaultOverride::Create(
			FIsResetToDefaultVisible::CreateLambda([this](TSharedPtr<IPropertyHandle>)
			{
				return ResolutionComboBox->GetSelectedItem() != NamedResolutions[1];
			}), FResetToDefaultHandler::CreateLambda([this](TSharedPtr<IPropertyHandle>)
			{
				ResolutionComboBox->SetSelectedItem(NamedResolutions[1]);
			})));
	}

	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& InCustomizationUtils) override
	{
		InChildBuilder.AddCustomRow(InPropertyHandle->GetPropertyDisplayName())
		.NameContent()
		[
			XPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			XPropertyHandle->CreatePropertyValueWidget()
		]
		.ExtensionContent()
		[
			SNew(SButton)
			.OnClicked_Lambda([this]()
			{
				if (LockedAspectRatio.IsSet())
				{
					LockedAspectRatio.Reset();
				}
				else
				{
					double AspectRatio;
					if (GetAspectRatio(AspectRatio))
					{
						LockedAspectRatio = AspectRatio;
					}
				}
				
				return FReply::Handled();
			})
			.ContentPadding(FMargin(0, 0, 4, 0))
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.Image_Lambda([this]
				{
					return LockedAspectRatio.IsSet() ? FAppStyle::GetBrush(TEXT("Icons.Link")) : FAppStyle::GetBrush(TEXT("Icons.Unlink"));
				})
				.ToolTipText_Lambda([this]
				{
					return LockedAspectRatio.IsSet() ?
					FText::Format(LOCTEXT("LockedAspectRatioTooltipFormat", "Click to unlock Aspect Ratio ({0})"), FText::AsNumber(LockedAspectRatio.GetValue())) :
					LOCTEXT("UnlockedAspectRatioTooltip", "Click to lock the Aspect Ratio");
				})
			]
		];
		
		InChildBuilder.AddCustomRow(InPropertyHandle->GetPropertyDisplayName())
		.NameContent()
		[
			YPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			YPropertyHandle->CreatePropertyValueWidget()
		];

		XPropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([this]
		{
			if (!bSettingNamedResolution)
			{
				ResolutionComboBox->SetSelectedItem(NamedResolutions.Last());
			}

			if (LockedAspectRatio.IsSet())
			{
				SetDimensionFromAspectRatio(XPropertyHandle, YPropertyHandle, 1.0 / LockedAspectRatio.GetValue());
			}
		}));
		
		YPropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([this]
		{
			if (!bSettingNamedResolution)
			{
				ResolutionComboBox->SetSelectedItem(NamedResolutions.Last());
			}

			if (LockedAspectRatio.IsSet())
			{
				SetDimensionFromAspectRatio(YPropertyHandle, XPropertyHandle, LockedAspectRatio.GetValue());
			}
		}));
	}
	
private:
	FText GetResolutionText() const
	{
		FIntPoint Resolution;
		FPropertyAccess::Result XResult = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FIntPoint, X))->GetValue(Resolution.X);
		FPropertyAccess::Result YResult = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FIntPoint, Y))->GetValue(Resolution.Y);

		if (XResult == FPropertyAccess::Fail || YResult == FPropertyAccess::Fail)
		{
			return FText::GetEmpty();
		}

		if (XResult == FPropertyAccess::MultipleValues || YResult == FPropertyAccess::MultipleValues)
		{
			return LOCTEXT("MultipleValues", "Multiple Values");
		}

		FNamedResolutionPtr SelectedResolution = ResolutionComboBox->GetSelectedItem();
		if (SelectedResolution->Resolution != FIntPoint::ZeroValue)
		{
			return SelectedResolution->Name;
		}

		FNumberFormattingOptions FormattingOptions;
		FormattingOptions.UseGrouping = false;
		return FText::Format(LOCTEXT("CustomResolutionTextFormat", "Custom - {0}x{1}"),
			FText::AsNumber(Resolution.X, &FormattingOptions),
			FText::AsNumber(Resolution.Y, &FormattingOptions));
	}

	void SetResolution(FNamedResolutionPtr InNamedResolution, ESelectInfo::Type SelectInfo)
	{
		if (InNamedResolution->Resolution != FIntPoint::ZeroValue)
		{
			TGuardValue<bool> SettingNamedResolution(bSettingNamedResolution, true);
			PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FIntPoint, X))->SetValue(InNamedResolution->Resolution.X);
			PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FIntPoint, Y))->SetValue(InNamedResolution->Resolution.Y);
		}
	}

	bool GetAspectRatio(double& OutAspectRatio) const
	{
		int32 X = 0;
		int32 Y = 0;

		if (XPropertyHandle->GetValue(X) != FPropertyAccess::Success)
		{
			return false;
		}

		if (YPropertyHandle->GetValue(Y) != FPropertyAccess::Success)
		{
			return false;
		}

		if (Y == 0)
		{
			return false;
		}

		OutAspectRatio = (double)X / (double)Y;
		return true;
	}

	void SetDimensionFromAspectRatio(const TSharedPtr<IPropertyHandle>& SrcDimHandle, const TSharedPtr<IPropertyHandle>& DestDimHandle, double AspectRatio)
	{
		if (bAspectRatioRecursionGuard)
		{
			return;
		}

		TGuardValue<bool> RecursionGuard(bAspectRatioRecursionGuard, true);
		
		int32 SrcDim;
		if (SrcDimHandle->GetValue(SrcDim) != FPropertyAccess::Success)
		{
			return;
		}

		const double ScaledSrcDim = SrcDim * AspectRatio;

		// Round to the nearest even number
		int32 DestDim = FMath::FloorToInt(ScaledSrcDim);
		if (DestDim % 2 != 0)
		{
			DestDim = FMath::CeilToInt(ScaledSrcDim);
		}

		DestDimHandle->SetValue(DestDim);
	}
	
private:
	TSharedPtr<IPropertyHandle> PropertyHandle;
	TSharedPtr<IPropertyHandle> XPropertyHandle;
	TSharedPtr<IPropertyHandle> YPropertyHandle;

	TSharedPtr<SComboBox<FNamedResolutionPtr>> ResolutionComboBox;
	
	TOptional<double> LockedAspectRatio = TOptional<double>();

	bool bAspectRatioRecursionGuard = false;
	bool bSettingNamedResolution = false;
};

TArray<TSharedPtr<FResolutionTypeCustomization::FNamedResolution>> FResolutionTypeCustomization::NamedResolutions =
{
	MakeShared<FNamedResolution>(LOCTEXT("720pResolutionName", "720p (HD) - 1280x720"), FIntPoint(1280, 720), LOCTEXT("Resolution720pToolTip", "High Definition")),
	MakeShared<FNamedResolution>(LOCTEXT("1080pResolutionName", "1080p (FHD) - 1920x1080"), FIntPoint(1920, 1080), LOCTEXT("Resolution1080pToolTip", "Full high definition")),
	MakeShared<FNamedResolution>(LOCTEXT("1440pResolutionName", "1440p (QHD) - 2560x1440"), FIntPoint(2560, 1440), LOCTEXT("Resolution1440pToolTip", "Quad high definition")),
	MakeShared<FNamedResolution>(LOCTEXT("2160pResolutionName", "2160p (4K UHD) - 3840x2160"), FIntPoint(3840, 2160), LOCTEXT("Resolution2160pToolTip", "Ultra high definition (4k)")),
	MakeShared<FNamedResolution>(LOCTEXT("CustomResolutionName", "Custom"), FIntPoint::ZeroValue, FText::GetEmpty())
};

class FCompositeActorPanelDetailCustomization : public IDetailCustomization
{
private:
	class FResolutionPropertyIdentifier : public IPropertyTypeIdentifier
	{
		virtual bool IsPropertyTypeCustomized(const IPropertyHandle& PropertyHandle) const override
		{
			return PropertyHandle.GetProperty()->GetNameCPP() == GET_MEMBER_NAME_STRING_CHECKED(ACompositeActor, RenderResolution);
		}
	};
	
public:
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
	{
		DetailBuilder.RegisterInstancedCustomPropertyTypeLayout(NAME_IntPoint, FOnGetPropertyTypeCustomizationInstance::CreateLambda([]
			{
				return MakeShared<FResolutionTypeCustomization>();
			}),
			MakeShared<FResolutionPropertyIdentifier>());
		
		DetailBuilder.GetObjectsBeingCustomized(ObjectsBeingCustomized);
		
		// Hide all categories besides the Composite category
		static const FName CompositeCategoryName = "Composite";
		
		TArray<FName> CategoryNames;
		DetailBuilder.GetCategoryNames(CategoryNames);

		for (const FName& CategoryName : CategoryNames)
		{
			if (CategoryName == CompositeCategoryName)
			{
				continue;
			}
			
			DetailBuilder.HideCategory(CategoryName);
		}

		// TransformCommon is a custom category that doesn't get returned by GetCategoryNames that also needs to be hidden
		DetailBuilder.HideCategory(TEXT("TransformCommon"));

		IDetailCategoryBuilder& CompositeCategory = DetailBuilder.EditCategory(CompositeCategoryName, LOCTEXT("CompositeActorCategoryName", "Composite Actor"));

		CompositeCategory.AddCustomRow(LOCTEXT("CompositeActorNameFilterText", "Name"))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("CompositeActorNameProperty", "Name"))
				.Font(FAppStyle::Get().GetFontStyle("PropertyWindow.NormalFont"))
			]
			.ValueContent()
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]
				{
					if (ObjectsBeingCustomized.Num() == 1 && ObjectsBeingCustomized[0].IsValid() && ObjectsBeingCustomized[0]->IsA<AActor>())
					{
						return FText::FromString(Cast<AActor>(ObjectsBeingCustomized[0])->GetActorLabel());
					}
					if (ObjectsBeingCustomized.Num() > 1)
					{
						return LOCTEXT("MultipleValuesLabel", "Multiple Values");
					}

					return FText::GetEmpty();
				})
				.OnTextCommitted_Lambda([this](const FText& InText, ETextCommit::Type InCommitType)
				{
					if (ObjectsBeingCustomized.Num() == 1 && ObjectsBeingCustomized[0].IsValid() && ObjectsBeingCustomized[0]->IsA<AActor>())
					{
						FScopedTransaction RenameActorTransaction(LOCTEXT("RenameActorTransaction", "Rename Composite Actor"));
						FActorLabelUtilities::RenameExistingActor(Cast<AActor>(ObjectsBeingCustomized[0]), InText.ToString(), true);
					}
				})
				.IsEnabled_Lambda([this]
				{
					return ObjectsBeingCustomized.Num() == 1;
				})
			];
		
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ACompositeActor, bIsActive));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ACompositeActor, CompositeLayers));
	}

private:
	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;

	TSharedPtr<IPropertyHandle> RenderResolutionHandle;
};

void SCompositeEditorPanel::RegisterTabSpawner()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	if (TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager())
	{
		LevelEditorTabManager->RegisterTabSpawner(CompositingEditorPanel::CompositeEditorTabName, FOnSpawnTab::CreateStatic(&CompositingEditorPanel::CreateTab))
			.SetDisplayName(LOCTEXT("PanelDisplayName", "Composure"))
			.SetTooltipText(LOCTEXT("PanelTooltipText", "Panel for viewing and editing compositing actors in the current level"))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorVirtualProductionCategory())
			.SetIcon(FSlateIcon(FCompositeEditorStyle::Get().GetStyleSetName(), "CompositeEditor.Composure"));
	}
}

void SCompositeEditorPanel::UnregisterTabSpawner()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	if (TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager())
	{
		LevelEditorTabManager->UnregisterTabSpawner(CompositingEditorPanel::CompositeEditorTabName);
	}
}

void SCompositeEditorPanel::Construct(const FArguments& InArgs)
{
	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bAllowMultipleTopLevelObjects = true;
	DetailsViewArgs.bShowPropertyMatrixButton = false;
	
	DetailsView = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(DetailsViewArgs);
	DetailsView->RegisterInstancedCustomPropertyLayout(ACompositeActor::StaticClass(), FOnGetDetailCustomizationInstance::CreateLambda([]
	{
		return MakeShared<FCompositeActorPanelDetailCustomization>();
	}));
	
	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(EOrientation::Orient_Vertical)

		+SSplitter::Slot()
		.Value(0.25f)
		.MinSize(this, &SCompositeEditorPanel::GetLayerTreeMinHeight)
		[
			SAssignNew(LayerTree, SCompositePanelLayerTree)
			.OnSelectionChanged(this, &SCompositeEditorPanel::OnLayerSelectionChanged)
		]

		+SSplitter::Slot()
		.Value(0.75f)
		[
			DetailsView.ToSharedRef()
		]
	];
}

void SCompositeEditorPanel::SelectCompositeActors(const TArray<TWeakObjectPtr<ACompositeActor>>& InCompositeActors)
{
	if (LayerTree.IsValid())
	{
		LayerTree->SelectCompositeActors(InCompositeActors);
	}
}

void SCompositeEditorPanel::OnLayerSelectionChanged(const TArray<UObject*>& SelectedLayers)
{
	if (DetailsView.IsValid())
	{
		DetailsView->SetObjects(SelectedLayers);
	}
}

float SCompositeEditorPanel::GetLayerTreeMinHeight() const
{
	if (LayerTree.IsValid())
	{
		const float PanelHeight = GetCachedGeometry().GetAbsoluteSize().Y;
		const float LayerTreeHeight = LayerTree->GetMinimumHeight();

		// Splitter panels get confused if a slot has a minimum size larger than the splitter, so only
		// return a non-zero minimum when whole panel is larger than the minimum
		if (PanelHeight > LayerTreeHeight)
		{
			return LayerTreeHeight;
		}
	}

	return 0.0f;
}

#undef LOCTEXT_NAMESPACE
