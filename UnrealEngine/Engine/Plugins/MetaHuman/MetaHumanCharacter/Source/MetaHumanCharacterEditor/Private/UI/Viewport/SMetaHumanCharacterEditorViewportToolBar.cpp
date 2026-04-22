// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMetaHumanCharacterEditorViewportToolBar.h"
#include "UI/Widgets/SMetaHumanCharacterEditorTileView.h"

#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "PropertyCustomizationHelpers.h"

#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorLog.h"
#include "MetaHumanCharacterEditorActor.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "MetaHumanCharacterEditorStyle.h"
#include "SMetaHumanCharacterEditorViewport.h"
#include "MetaHumanCharacterEditorViewportClient.h"

#include "ViewportToolbar/UnrealEdViewportToolbar.h"
#include "ViewportToolbar/UnrealEdViewportToolbarContext.h"
#include "ToolMenus.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "ContentBrowserDelegates.h"
#include "AssetRegistry/AssetRegistryModule.h"

#define LOCTEXT_NAMESPACE "SMetaHumanCharacterEditorViewportToolBar"


TSharedRef<SWidget> CreateCustomEnvironmentSelector(TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport)
{
	FAssetData InitialSelection;
	FAssetPickerConfig LevelPickerConfig;
	LevelPickerConfig.Filter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());
	LevelPickerConfig.Filter.bRecursiveClasses = true;
	LevelPickerConfig.SelectionMode = ESelectionMode::Single;
	LevelPickerConfig.bAllowNullSelection = false;

	if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
	{
		if(UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get())
		{
			FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			InitialSelection = AssetRegistryModule.Get().GetAssetByObjectPath(MetaHumanCharacter->ViewportSettings.CustomEnvironment);
		}
	}
	LevelPickerConfig.InitialAssetSelection = InitialSelection;

	LevelPickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda([WeakMetaHumanCharacterEditorViewport](const FAssetData& SelectedAsset) {
		if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
		{
			if(UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get())
			{
				if (SelectedAsset.IsValid())
				{
					MetaHumanCharacter->ViewportSettings.CustomEnvironment = SelectedAsset.ToSoftObjectPath();
					UMetaHumanCharacterEditorSubsystem::Get()->NotifyLightingEnvironmentChanged(MetaHumanCharacter);
					MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->Invalidate();
				}
			}
			MetaHumanCharacterEditorViewport->CustomEnvironmentSelectionBox->SetIsOpen(false);
		}
	});

	const FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	return SNew(SBox)
		.HeightOverride(500.f)
		.WidthOverride(400.f)
		[
			ContentBrowserModule.Get().CreateAssetPicker(LevelPickerConfig)
		];
}

TSharedRef<SWidget> CreateEnvironmentWidget(TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport)
{
	check(MetaHumanCharacterEditorViewport);

	UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();

	UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
	check(MetaHumanCharacter);

	FProperty* LightRotationProperty = FMetaHumanCharacterViewportSettings::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterViewportSettings, LightRotation));
	const float MinValue = LightRotationProperty->GetFloatMetaData(TEXT("ClampMin"));
	const float MaxValue = LightRotationProperty->GetFloatMetaData(TEXT("ClampMax"));

	FProperty* BackgroundColorProperty = FMetaHumanCharacterViewportSettings::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FMetaHumanCharacterViewportSettings, BackgroundColor));
	check(BackgroundColorProperty);

	TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = MetaHumanCharacterEditorViewport;

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(5.0f, 5.0f, 5.f, 5.0f)
		[
			SNew(SBox)
			.WidthOverride(310.f)
			.Visibility_Lambda([WeakMetaHumanCharacterEditorViewport]()
			{
				if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
				{
					if(UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get())
					{
						return MetaHumanCharacter->ViewportSettings.bUseCustomEnvironment ? EVisibility::Collapsed : EVisibility::Visible;
					}
				}
				return EVisibility::Visible;
			})
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(5.0f, 5.0f, 5.f, 5.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.MinWidth(300)
					[
						SNew(SMetaHumanCharacterEditorTileView<EMetaHumanCharacterEnvironment>)
						.InitiallySelectedItem_Lambda([WeakMetaHumanCharacterEditorViewport]
						{
							if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
									UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
									check(MetaHumanCharacter);
									return MetaHumanCharacter->ViewportSettings.CharacterEnvironment;
								}
								return EMetaHumanCharacterEnvironment::Studio;
						})
						.OnGetSlateBrush_Lambda([](uint8 InItem) -> const FSlateBrush*
						{
							const FString EnvironmentName = StaticEnum<EMetaHumanCharacterEnvironment>()->GetAuthoredNameStringByValue(InItem);
							const FString EnvironmentBrushName = FString::Format(TEXT("Viewport.LightScenarios.{0}"), { EnvironmentName });
							return FMetaHumanCharacterEditorStyle::Get().GetBrush(*EnvironmentBrushName);
						})
						.OnSelectionChanged_Lambda([WeakMetaHumanCharacterEditorViewport](uint8 InItem)
						{
							if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
							{
								UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
								UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
								check(MetaHumanCharacter);
								MetaHumanCharacter->ViewportSettings.CharacterEnvironment = EMetaHumanCharacterEnvironment(InItem);
								GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>()->NotifyLightingEnvironmentChanged(MetaHumanCharacter);
								MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->Invalidate();
							}
						})
						]
					]
					+ SVerticalBox::Slot()
					.Padding(5.0f, 5.0f)
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Left)
						.VAlign(VAlign_Center)
						.Padding(5.0f, 5.0f)
						[
							SNew(STextBlock)
							.Text(BackgroundColorProperty->GetDisplayNameText())
						]
						+ SHorizontalBox::Slot()
						[
							SNew(SColorBlock)
							.Color_Lambda([WeakMetaHumanCharacterEditorViewport]
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
									return MetaHumanCharacter->ViewportSettings.BackgroundColor;
								}
								return FLinearColor::White;
							})
							.OnMouseButtonDown_Lambda([WeakMetaHumanCharacterEditorViewport](const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									TNotNull<UMetaHumanCharacter*> MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();

									if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
									{
										return FReply::Unhandled();
									}

									FColorPickerArgs Args;
									Args.bIsModal = false;
									Args.bOnlyRefreshOnMouseUp = false;
									Args.bOnlyRefreshOnOk = false;
									Args.bUseAlpha = false;
									Args.bOpenAsMenu = false;
									Args.bClampValue = true;
									Args.ParentWidget = MetaHumanCharacterEditorViewport;
									Args.InitialColor = MetaHumanCharacter->ViewportSettings.BackgroundColor;
									Args.OnColorCommitted = FOnLinearColorValueChanged::CreateWeakLambda(MetaHumanCharacter,
									[MetaHumanCharacter, WeakMetaHumanCharacterEditorViewport](const FLinearColor& NewColor)
									{
										if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
										{
											UMetaHumanCharacterEditorSubsystem::Get()->UpdateBackgroundColor(MetaHumanCharacter, NewColor);
											MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->Invalidate();
										}
									});
										OpenColorPicker(Args);
									}

								return FReply::Handled();
							})
							.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
							.AlphaBackgroundBrush(FAppStyle::Get().GetBrush("ColorPicker.RoundedAlphaBackground"))
							.ShowBackgroundForAlpha(true)
							.CornerRadius(FVector4(2.f, 2.f, 2.f, 2.f))
						]
					]
					+ SVerticalBox::Slot()
					.Padding(5.0f, 5.0f)
					.AutoHeight()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.Padding(0.0f, 5.0f)
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("LightRigRotationLabel", "Light Rig Rotation"))
						]
							+ SVerticalBox::Slot()
							.Padding(0.0f, 5.0f)
							.AutoHeight()
							[
								SNew(SSlider)
								.MinValue(MinValue)
								.MaxValue(MaxValue)
								.Value_Lambda([WeakMetaHumanCharacterEditorViewport]
								{
									float Value = 0.0f;
									if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
									{
										if(UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get())
										{
											return MetaHumanCharacter->ViewportSettings.LightRotation;
										}
									}
									return Value;
								})
								.OnValueChanged_Lambda([WeakMetaHumanCharacterEditorViewport, LightRotationProperty](float NewValue)
								{
									if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
									{
										UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
										check(MetaHumanCharacter);
										UMetaHumanCharacterEditorSubsystem::Get()->UpdateLightRotation(MetaHumanCharacter, NewValue);
										MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->Invalidate();
									}
								})
							]
					]
					+ SVerticalBox::Slot()
					.Padding(5.0f, 5.0f)
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.Padding(0.0f, 5.0f)
						.AutoWidth()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("TonemapperLabel", "Tonemapper"))
						]
						+ SHorizontalBox::Slot()
						.Padding(0.0f, 5.0f)
						.AutoWidth()
						[
							SNew(SCheckBox)
							.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
							{
								bool Value = false;
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
									return MetaHumanCharacter->ViewportSettings.bTonemapperEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
								}
								return ECheckBoxState::Unchecked;
							})
							.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
									UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
									check(MetaHumanCharacter);
									MetaHumanCharacter->ViewportSettings.bTonemapperEnabled = NewState == ECheckBoxState::Checked? true : false;
									GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>()->NotifyLightingEnvironmentChanged(MetaHumanCharacter);
									MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->Invalidate();
								}
							})
						]
					]
				]
			];
}

TSharedPtr<SMetaHumanCharacterEditorViewport> GetMetaHumanCharacterEditorViewportFromContext(UUnrealEdViewportToolbarContext* const InEditorViewportContext)
{
	if (!InEditorViewportContext)
	{
		return {};
	}

	const TSharedPtr<SEditorViewport> EditorViewport = InEditorViewportContext->Viewport.Pin();
	if (!EditorViewport)
	{
		return {};
	}

	TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = StaticCastSharedPtr<SMetaHumanCharacterEditorViewport>(EditorViewport);
	return MetaHumanCharacterEditorViewport;
}

FToolMenuEntry CreateEnvironmentSubmenu()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicEnvironmentOptions",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>());

				const TAttribute<FText> Label = TAttribute<FText>::CreateLambda(
					[WeakMetaHumanCharacterEditorViewport]()
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
							if (UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get())
							{
								{
									if (!(MetaHumanCharacter->ViewportSettings.bUseCustomEnvironment && MetaHumanCharacter->ViewportSettings.CustomEnvironment.IsValid()))
									{
										return UEnum::GetDisplayValueAsText(MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter->ViewportSettings.CharacterEnvironment);
									}
									else
									{
										return LOCTEXT("CustomEnvironment", "Custom");
									}
								}
							}

						return LOCTEXT("EnvironmentLabel", "Environment");
					}
				);

				FToolMenuEntry& Entry = InDynamicSection.AddSubMenu(
					"Environment",
					Label,
					LOCTEXT("EnvironmentSubmenuTooltip", "Select environment"),
					FNewToolMenuDelegate::CreateLambda(
						[](UToolMenu* Submenu) -> void
						{
							TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(Submenu->FindContext<UUnrealEdViewportToolbarContext>());
							if (!MetaHumanCharacterEditorViewport.IsValid())
							{
								return;
							}

							Submenu->AddMenuEntry(NAME_None, FToolMenuEntry::InitWidget(NAME_None, CreateEnvironmentWidget(MetaHumanCharacterEditorViewport), FText()));
						}
					),
					false,
					FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), "Viewport.Icons.Environment")
				);
				Entry.ToolBarData.ResizeParams.ClippingPriority = 800;
			}
		)
	);
}

void PopulateCameraSelectionMenu(UToolMenu* InMenu)
{
	TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InMenu->FindContext<UUnrealEdViewportToolbarContext>());
	if (!WeakMetaHumanCharacterEditorViewport.IsValid())
	{
		return;
	}

	FToolMenuSection& CameraSelectionSection = InMenu->FindOrAddSection("MetaHumanCharacterEditorViewport_CameraSelection", LOCTEXT("CameraSwitchSubmenuLabel", "Camera Switching"));

	for (EMetaHumanCharacterCameraFrame FrameOption : TEnumRange<EMetaHumanCharacterCameraFrame>())
	{
		const bool Rotate = false;
		CameraSelectionSection.AddMenuEntry(
			NAME_None,
			UEnum::GetDisplayValueAsText(FrameOption),
			UEnum::GetDisplayValueAsText(FrameOption),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport, FrameOption]() {
					if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
					{
						check(MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.IsValid());
						MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->FocusOnSelectedFrame(FrameOption, /*bInRotate*/ Rotate);
					}}),
				FCanExecuteAction()
			),
			EUserInterfaceActionType::Button
		);

	}
}

FToolMenuEntry CreateCameraSelectionSubmenu()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicCameraOptions",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>());

				const TAttribute<FText> Label = TAttribute<FText>::CreateLambda(
					[WeakMetaHumanCharacterEditorViewport]()
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							return UEnum::GetDisplayValueAsText(MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter->ViewportSettings.CameraFrame);
						}
						return LOCTEXT("CameraSelectionSubmenuLabel", "Camera Selection");
					}
				);

				FToolMenuEntry& Entry = InDynamicSection.AddSubMenu(
					"Camera",
					Label,
					LOCTEXT("CameraSelectionSubmenuTooltip", "Select camera framing"),
					FNewToolMenuDelegate::CreateLambda(
						[](UToolMenu* Submenu) -> void
						{
							PopulateCameraSelectionMenu(Submenu);
						}
					),
					false,
					FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), "Viewport.Icons.Camera")
				);
				Entry.ToolBarData.ResizeParams.ClippingPriority = 800;
			}
		)
	);
}

void PopulateLODMenu(UToolMenu* InMenu)
{
	TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InMenu->FindContext<UUnrealEdViewportToolbarContext>());
	if (!WeakMetaHumanCharacterEditorViewport.IsValid())
	{
		return;
	}

	FToolMenuSection& LODSection = InMenu->FindOrAddSection("MetaHumanCharacterEditorViewport_LOD", LOCTEXT("LODSubmenuLabel", "Level of Detail"));

	for (EMetaHumanCharacterLOD LODOption : TEnumRange<EMetaHumanCharacterLOD>())
	{
		LODSection.AddMenuEntry(
			NAME_None,
			UEnum::GetDisplayValueAsText(LODOption),
			UEnum::GetDisplayValueAsText(LODOption),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport, LODOption]()
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();

							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							check(MetaHumanCharacter);

							MetaHumanCharacterSubsystem->UpdateCharacterLOD(MetaHumanCharacter, LODOption);
						}
					}),
				FCanExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport, LODOption]() -> bool
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();

							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							check(MetaHumanCharacter);

							return LODOption == EMetaHumanCharacterLOD::LOD0 || MetaHumanCharacterSubsystem->GetRiggingState(MetaHumanCharacter) != EMetaHumanCharacterRigState::Unrigged;
						}
						return false;
					})

			),
			EUserInterfaceActionType::Button
		);
	}

	LODSection.AddSeparator(NAME_None);

	LODSection.AddMenuEntry(
		NAME_None,
		LOCTEXT("AlwaysUseCardsLabel", "Always Use Hair Cards"),
		LOCTEXT("AlwaysUseCardsSubmenuTooltip", "Toggle always use hair cards on groom components"),
		FSlateIcon(),
		FUIAction(
		FExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
		{
			if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
			{
				UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();
				UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
				check(MetaHumanCharacter);
				const bool bUseCards = !MetaHumanCharacter->ViewportSettings.bAlwaysUseHairCards;
				MetaHumanCharacter->ViewportSettings.bAlwaysUseHairCards = bUseCards;
				MetaHumanCharacterSubsystem->UpdateAlwaysUseHairCardsOption(MetaHumanCharacter, bUseCards);
				MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->Invalidate();
			}
		}),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
		{
			if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
			{
				UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();

				UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
				check(MetaHumanCharacter);

				return MetaHumanCharacter->ViewportSettings.bAlwaysUseHairCards;
			}
			return false;
		})
		),
		EUserInterfaceActionType::Check
	);
}

FToolMenuEntry CreateLODSubmenu()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicLODOptions",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>());

				const TAttribute<FText> Label = TAttribute<FText>::CreateLambda(
					[WeakMetaHumanCharacterEditorViewport]()
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							return UEnum::GetDisplayValueAsText(MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter->ViewportSettings.LevelOfDetail);
						}
						return LOCTEXT("LODSelectionSubmenuLabel", "LOD Selection");
					}
				);

				FToolMenuEntry& Entry = InDynamicSection.AddSubMenu(
					"LOD",
					Label,
					LOCTEXT("LODSubmenuTooltip", "Select LOD"),
					FNewToolMenuDelegate::CreateLambda(
						[](UToolMenu* Submenu) -> void
						{
							PopulateLODMenu(Submenu);
							Submenu->bSearchable = false;
						}
					),
					false,
					FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), "Viewport.Icons.LOD")
				);
				Entry.ToolBarData.ResizeParams.ClippingPriority = 800;
			}
		)
	);
}

void PopulateRenderingQualityMenu(UToolMenu* InMenu)
{
	TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InMenu->FindContext<UUnrealEdViewportToolbarContext>());
	if (!WeakMetaHumanCharacterEditorViewport.IsValid())
	{
		return;
	}

	FToolMenuSection& RenderingQualitySection = InMenu->FindOrAddSection("MetaHumanCharacterEditorViewport_RenderingQuality", LOCTEXT("RenderingQualitySubmenuLabel", "Rendering Quality"));

	for (EMetaHumanCharacterRenderingQuality RenderingQualityOption : TEnumRange<EMetaHumanCharacterRenderingQuality>())
	{
		RenderingQualitySection.AddMenuEntry(
			NAME_None,
			UEnum::GetDisplayValueAsText(RenderingQualityOption),
			UEnum::GetDisplayValueAsText(RenderingQualityOption),
			FSlateIcon(),
			FUIAction
			(
				FExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport, RenderingQualityOption]()
				{
					if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
					{
						UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
						check(MetaHumanCharacter);

						MetaHumanCharacter->ViewportSettings.RenderingQuality = RenderingQualityOption;
						MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ChangeRenderQuality(RenderingQualityOption);
					}
				}),
				FCanExecuteAction()
			),
			EUserInterfaceActionType::Button
		);

	}
}

FToolMenuEntry CreateRenderingQualitySubmenu()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicRenderingQualityOptions",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>());

				const TAttribute<FText> Label = TAttribute<FText>::CreateLambda(
					[WeakMetaHumanCharacterEditorViewport]()
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							return UEnum::GetDisplayValueAsText(MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter->ViewportSettings.RenderingQuality);
						}
						return LOCTEXT("RenderingQualitySelectionSubmenuLabel", "Rendering Quality Selection");
					}
				);

				FToolMenuEntry& Entry = InDynamicSection.AddSubMenu(
					"RenderingQuality",
					Label,
					LOCTEXT("RenderingQualitySubmenuTooltip", "Select rendering quality"),
					FNewToolMenuDelegate::CreateLambda(
						[](UToolMenu* Submenu) -> void
						{
							PopulateRenderingQualityMenu(Submenu);
						}
					),
					false,
					FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), "Viewport.Icons.Quality")
				);
				Entry.ToolBarData.ResizeParams.ClippingPriority = 800;
			}
		)
	);
}

void PopulateDebugSubmenu(UToolMenu* InMenu)
{
	TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InMenu->FindContext<UUnrealEdViewportToolbarContext>());
	if (!WeakMetaHumanCharacterEditorViewport.IsValid())
	{
		return;
	}

	FToolMenuSection& FaceDebugOptionsSection = InMenu->FindOrAddSection("MetaHumanCharacterEditorViewport_FaceDebug", LOCTEXT("FaceDebugSubmenuLabel", "Face"));
	FToolMenuSection& BodyDebugOptionsSection = InMenu->FindOrAddSection("MetaHumanCharacterEditorViewport_BodyDebug", LOCTEXT("BodyDebugSubmenuLabel", "Body"));

	FaceDebugOptionsSection.AddEntry(FToolMenuEntry::InitWidget(
		NAME_None,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 1.0f)
		[
			SNew(SCheckBox)
			.Padding(5.0f)
			.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
				{
					bool Value = false;
					if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
					{
						UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
						return MetaHumanCharacter->ViewportSettings.bShowFaceBones ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}
				
					return ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
			{
				if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
				{
					UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
					MetaHumanCharacter->ViewportSettings.bShowFaceBones = NewState == ECheckBoxState::Checked ? true : false;
					MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowFaceBonesOnCharacter(NewState == ECheckBoxState::Checked ? true : false);
				}
			})
			.Content()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1.f)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Persona.AssetClass.Skeleton"))
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1.f)
				[
					SNew(STextBlock).Text(LOCTEXT("DebugSubmenuShowBonesOnFace", "Bones"))
				]
				
			]
		]
		,
		FText(),
		true
	));

	BodyDebugOptionsSection.AddEntry(FToolMenuEntry::InitWidget(
		NAME_None,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f,1.0)
		[
			SNew(SCheckBox)
			.Padding(5.0f)
			.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
			{
				bool Value = false;
				if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
				{
					UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
					return MetaHumanCharacter->ViewportSettings.bShowBodyBones ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}

				return ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
			{
				if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
				{
					UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
					MetaHumanCharacter->ViewportSettings.bShowBodyBones = NewState == ECheckBoxState::Checked ? true : false;
					MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowBodyBonesOnCharacter(NewState == ECheckBoxState::Checked ? true : false);
				}
			})
			
			.Content()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1.f)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Persona.AssetClass.Skeleton"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(1.f)
				[
					SNew(STextBlock).Text(LOCTEXT("DebugSubmenuShowBonesOnBody", "Bones"))
				]
			]
		]
		,
		FText(),
		true
	));

	FaceDebugOptionsSection.AddEntry(FToolMenuEntry::InitWidget(
		NAME_None,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 1.0f)
		[
			SNew(SCheckBox)
				.Padding(5.0f)
				.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
					{
						bool Value = false;
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							return MetaHumanCharacter->ViewportSettings.bShowFaceNormals ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}

						return ECheckBoxState::Unchecked;
					})
				.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							MetaHumanCharacter->ViewportSettings.bShowFaceNormals = NewState == ECheckBoxState::Checked ? true : false;
							MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowFaceNormalsOnCharacter(NewState == ECheckBoxState::Checked ? true : false);
						}
					})
				.Content()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("AnimViewportMenu.SetShowNormals.Small"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.f)
					[
						SNew(STextBlock).Text(LOCTEXT("DebugSubmenuShowNormalsOnFace", "Normals"))
					]
				]
		]
	,
		FText(),
		true
	));

	BodyDebugOptionsSection.AddEntry(FToolMenuEntry::InitWidget(
		NAME_None,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 1.0f)
		[
			SNew(SCheckBox)
				.Padding(5.0f)
				.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
					{
						bool Value = false;
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							return MetaHumanCharacter->ViewportSettings.bShowBodyNormals ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}

						return ECheckBoxState::Unchecked;
					})
				.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							MetaHumanCharacter->ViewportSettings.bShowBodyNormals = NewState == ECheckBoxState::Checked ? true : false;
							MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowBodyNormalsOnCharacter(NewState == ECheckBoxState::Checked ? true : false);
						}
					})
				.Content()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("AnimViewportMenu.SetShowNormals.Small"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.f)
					[
						SNew(STextBlock).Text(LOCTEXT("DebugSubmenuShowNormalsOnBody", "Normals"))
					]
				]
		]
	,
		FText(),
		true
	));

	FaceDebugOptionsSection.AddEntry(FToolMenuEntry::InitWidget(
		NAME_None,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 1.0f)
		[
			SNew(SCheckBox)
				.Padding(5.0f)
				.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
					{
						bool Value = false;
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							return MetaHumanCharacter->ViewportSettings.bShowFaceTangents ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}

						return ECheckBoxState::Unchecked;
					})
				.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							MetaHumanCharacter->ViewportSettings.bShowFaceTangents = NewState == ECheckBoxState::Checked ? true : false;
							MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowFaceTangentsOnCharacter(NewState == ECheckBoxState::Checked ? true : false);
						}
					})
				.Content()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("AnimViewportMenu.SetShowTangents.Small"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.f)
					[
						SNew(STextBlock).Text(LOCTEXT("DebugSubmenuShowTangentsOnFace", "Tangents"))
					]
				]
		]
	,
		FText(),
		true
	));

	BodyDebugOptionsSection.AddEntry(FToolMenuEntry::InitWidget(
		NAME_None,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 1.0f)
		[
			SNew(SCheckBox)
				.Padding(5.0f)
				.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
					{
						bool Value = false;
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							return MetaHumanCharacter->ViewportSettings.bShowBodyTangents ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}

						return ECheckBoxState::Unchecked;
					})
				.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							MetaHumanCharacter->ViewportSettings.bShowBodyTangents = NewState == ECheckBoxState::Checked ? true : false;
							MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowBodyTangentsOnCharacter(NewState == ECheckBoxState::Checked ? true : false);
						}
					})
				.Content()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("AnimViewportMenu.SetShowTangents.Small"))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(1.f)
					[
						SNew(STextBlock).Text(LOCTEXT("DebugSubmenuShowTangentsOnBody", "Tangents"))
					]
				]
		]
	,
		FText(),
		true
	));

	FaceDebugOptionsSection.AddEntry(FToolMenuEntry::InitWidget(
		NAME_None,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 1.0f)
		[
			SNew(SCheckBox)
				.Padding(5.0f)
				.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
					{
						bool Value = false;
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							return MetaHumanCharacter->ViewportSettings.bShowFaceBinormals ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}

						return ECheckBoxState::Unchecked;
					})
				.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							MetaHumanCharacter->ViewportSettings.bShowFaceBinormals = NewState == ECheckBoxState::Checked ? true : false;
							MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowFaceBinormalsOnCharacter(NewState == ECheckBoxState::Checked ? true : false);
						}
					})
				.Content()
				[
					SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(1.f)
						[
							SNew(SImage)
								.Image(FAppStyle::GetBrush("AnimViewportMenu.SetShowBinormals.Small"))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(1.f)
						[
							SNew(STextBlock).Text(LOCTEXT("DebugSubmenuShowBinormalsOnFace", "Binormals"))
						]
				]
		]
	,
		FText(),
		true
	));

	BodyDebugOptionsSection.AddEntry(FToolMenuEntry::InitWidget(
		NAME_None,
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.Padding(5.0f, 1.0f)
		[
			SNew(SCheckBox)
				.Padding(5.0f)
				.IsChecked_Lambda([WeakMetaHumanCharacterEditorViewport]()
					{
						bool Value = false;
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							return MetaHumanCharacter->ViewportSettings.bShowBodyBinormals ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}

						return ECheckBoxState::Unchecked;
					})
				.OnCheckStateChanged_Lambda([WeakMetaHumanCharacterEditorViewport](ECheckBoxState NewState)
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							MetaHumanCharacter->ViewportSettings.bShowBodyBinormals = NewState == ECheckBoxState::Checked ? true : false;
							MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->ShowBodyBinormalsOnCharacter(NewState == ECheckBoxState::Checked ? true : false);
						}
					})
				.Content()
				[
					SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(1.f)
						[
							SNew(SImage)
								.Image(FAppStyle::GetBrush("AnimViewportMenu.SetShowBinormals.Small"))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(1.f)
						[
							SNew(STextBlock).Text(LOCTEXT("DebugSubmenuShowBinormalsOnBody", "Binormals"))
						]
				]
		]
	,
		FText(),
		true
	));

}

FToolMenuEntry CreateDebugSubmenu()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicDebugOptions",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				const TAttribute<FText> Label = TAttribute<FText>(FText::GetEmpty());
				
				FToolMenuEntry& Entry = InDynamicSection.AddSubMenu(
					"DebugSkeletalMesh",
					Label,
					LOCTEXT("DebugSkeletalMeshSubmenuTooltip", "Rendering debug options for SkeletalMeshes"),
					FNewToolMenuDelegate::CreateLambda(
						[](UToolMenu* Submenu)->void
						{
							PopulateDebugSubmenu(Submenu);
						}
					),
					false,
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visibility", "Icons.Visibility.Small")
				);
				Entry.ToolBarData.ResizeParams.ClippingPriority = 800;
			}
		)
	);
}

void PopulatePreviewMaterialMenu(UToolMenu* InMenu)
{
	TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InMenu->FindContext<UUnrealEdViewportToolbarContext>());
	if (!WeakMetaHumanCharacterEditorViewport.IsValid())
	{
		return;
	}

	FToolMenuSection& PreviewMaterialSection = InMenu->FindOrAddSection("MetaHumanCharacterEditorViewport_PreviewMaterial", LOCTEXT("PreviewMaterialSubmenuLabel", "Preview Material"));

	for (EMetaHumanCharacterSkinPreviewMaterial PreviewMaterialOption : TEnumRange<EMetaHumanCharacterSkinPreviewMaterial>())
	{
		PreviewMaterialSection.AddMenuEntry(
			NAME_None,
			UEnum::GetDisplayValueAsText(PreviewMaterialOption),
			UEnum::GetDisplayValueAsText(PreviewMaterialOption),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport, PreviewMaterialOption]()
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							UMetaHumanCharacterEditorSubsystem* MetaHumanCharacterSubsystem = UMetaHumanCharacterEditorSubsystem::Get();

							UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
							check(MetaHumanCharacter);

							MetaHumanCharacterSubsystem->UpdateCharacterPreviewMaterial(MetaHumanCharacter, PreviewMaterialOption);
						}
					}),
				FCanExecuteAction()
			),
			EUserInterfaceActionType::Button
		);
	}
}

FToolMenuEntry CreatePreviewMaterialSubmenu()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicMaterialOptions",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>());

				const TAttribute<FText> Label = TAttribute<FText>::CreateLambda(
					[WeakMetaHumanCharacterEditorViewport]()
					{
						if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
						{
							return UEnum::GetDisplayValueAsText(MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter->PreviewMaterialType);
						}
						return LOCTEXT("MaterialSubmenuLabel", "Preview Material");
					}
				);

				FToolMenuEntry& Entry = InDynamicSection.AddSubMenu(
					"PreviewMaterial",
					Label,
					LOCTEXT("MaterialSubmenuLabelTooltip", "Select preview material"),
					FNewToolMenuDelegate::CreateLambda(
						[](UToolMenu* Submenu) -> void
						{
							PopulatePreviewMaterialMenu(Submenu);
						}
					),
					false,
					FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), "Viewport.Icons.Clay")
				);
				Entry.ToolBarData.ResizeParams.ClippingPriority = 800;
			}
		)
	);
}

FToolMenuEntry CreateViewportOverlayToggle()
{
	return FToolMenuEntry::InitDynamicEntry(
		"DynamicViewportOverlayToggle",
		FNewToolMenuSectionDelegate::CreateLambda(
			[](FToolMenuSection& InDynamicSection) -> void
			{
				TWeakPtr<SMetaHumanCharacterEditorViewport> WeakMetaHumanCharacterEditorViewport = GetMetaHumanCharacterEditorViewportFromContext(InDynamicSection.FindContext<UUnrealEdViewportToolbarContext>());
				InDynamicSection.AddEntry(FToolMenuEntry::InitToolBarButton(
					NAME_None,
					FToolUIActionChoice(FUIAction(
						FExecuteAction::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									if (UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get())
									{
										MetaHumanCharacter->ViewportSettings.bShowViewportOverlays = !MetaHumanCharacter->ViewportSettings.bShowViewportOverlays;
									}
								}
							}),
						FCanExecuteAction(),
						FGetActionCheckState::CreateLambda([WeakMetaHumanCharacterEditorViewport]()
							{
								if (TSharedPtr<SMetaHumanCharacterEditorViewport> MetaHumanCharacterEditorViewport = WeakMetaHumanCharacterEditorViewport.Pin())
								{
									UMetaHumanCharacter* MetaHumanCharacter = MetaHumanCharacterEditorViewport->GetMetaHumanCharacterEditorViewportClient()->WeakCharacter.Get();
									if (MetaHumanCharacter)
									{
										return MetaHumanCharacter->ViewportSettings.bShowViewportOverlays ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
									}
								}
								return ECheckBoxState::Unchecked;
							})
					)),
					TAttribute<FText>{},
					LOCTEXT("ViewportToolbarToggleViewport", "Toggle viewport overlay"),
					FSlateIcon(FMetaHumanCharacterEditorStyle::Get().GetStyleSetName(), "Viewport.Icons.Keyboard"),
					EUserInterfaceActionType::ToggleButton
				));
			}
		)
	);
}



#undef LOCTEXT_NAMESPACE
