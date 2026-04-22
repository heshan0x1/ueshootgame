// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositeLayerPlateCustomization.h"

#include "CompositeActor.h"
#include "CompositeEditorStyle.h"
#include "CompositeMeshActor.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "IDetailGroup.h"
#include "IPropertyUtilities.h"
#include "LevelEditorSubsystem.h"
#include "MediaSource.h"
#include "MediaTexture.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "UnrealEdGlobals.h"
#include "Camera/CameraComponent.h"
#include "Editor/UnrealEdEngine.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Layers/CompositeLayerPlate.h"
#include "Profile/IMediaProfileManager.h"
#include "Profile/MediaProfile.h"
#include "Profile/MediaProfilePlaybackManager.h"
#include "Styling/SlateIconFinder.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UI/SCompositeActorPickerTable.h"
#include "UI/SCompositePlatePassPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "FCompositeLayerPlateCustomization"

namespace CompositeLayerPlateCustomization
{
	DECLARE_DELEGATE_TwoParams(FOnCustomizePropertyRow, const TSharedPtr<IPropertyHandle>&, IDetailPropertyRow&);
}

TSharedRef<IDetailCustomization> FCompositeLayerPlateCustomization::MakeInstance()
{
	return MakeShared<FCompositeLayerPlateCustomization>();
}

void FCompositeLayerPlateCustomization::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	CachedDetailBuilder = DetailBuilder;
	IDetailCustomization::CustomizeDetails(DetailBuilder);
}

void FCompositeLayerPlateCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	TArray<TWeakObjectPtr<UCompositeLayerPlate>> Objects = DetailLayout.GetObjectsOfTypeBeingCustomized<UCompositeLayerPlate>();

	DetailLayout.HideProperty(GET_MEMBER_NAME_CHECKED(UCompositeLayerPlate, CompositeMeshes));
	DetailLayout.HideProperty(GET_MEMBER_NAME_CHECKED(UCompositeLayerPlate, MediaPasses));
	DetailLayout.HideProperty(GET_MEMBER_NAME_CHECKED(UCompositeLayerPlate, LayerPasses));
	
	IDetailCategoryBuilder& PlateCategory = DetailLayout.EditCategoryAllowNone(NAME_None);

	TMap<FName, CompositeLayerPlateCustomization::FOnCustomizePropertyRow> PropertyCustomizations =
	{
		{
			GET_MEMBER_NAME_CHECKED(UCompositeLayerPlate, Texture),
			CompositeLayerPlateCustomization::FOnCustomizePropertyRow::CreateSP(this, &FCompositeLayerPlateCustomization::CustomizeTexturePropertyRow)
		}
	};
	
	// Add all default simple properties ahead of the custom group
	TArray<TSharedRef<IPropertyHandle>> PlateProperties;
	PlateCategory.GetDefaultProperties(PlateProperties, /* bSimpleProperties */ true, /* bAdvancedProperties */ false);
	for (const TSharedRef<IPropertyHandle>& Property : PlateProperties)
	{
		if (!Property->IsCustomized())
		{
			const FName PropertyName = Property->GetProperty()->GetFName();
			IDetailPropertyRow& PropertyRow = PlateCategory.AddProperty(Property);
			if (PropertyCustomizations.Contains(PropertyName))
			{
				PropertyCustomizations[PropertyName].ExecuteIfBound(Property, PropertyRow);
			}
		}
	}
	
	if (Objects.Num() == 1 && Objects[0].IsValid())
	{
		UCompositeLayerPlate* Plate = Cast<UCompositeLayerPlate>(Objects[0].Get());

		IDetailGroup& ActorListGroup = PlateCategory.AddGroup("CompositeMeshContent", LOCTEXT("CompositeMeshGroupName", "Composite Mesh Content"), false, true);
		FCompositeActorPickerListRef ActorListRef(Plate, GET_MEMBER_NAME_CHECKED(UCompositeLayerPlate, CompositeMeshes), &Plate->CompositeMeshes);
		
		ActorListGroup.AddWidgetRow()
		.WholeRowContent()
		[
			SNew(SCompositeActorPickerTable, ActorListRef)
			.OnExtendAddMenu(this, &FCompositeLayerPlateCustomization::OnExtendCompositeMeshAddMenu)
			.OnLayoutSizeChanged(this, &FCompositeLayerPlateCustomization::OnLayoutSizeChanged)
			.ShowApplyMaterialSection(true)
		];
		
		IDetailGroup& PassesGroup = PlateCategory.AddGroup("Passes", LOCTEXT("PassesGroupName", "Passes"), false, true);

		PassesGroup.AddWidgetRow()
		.WholeRowContent()
		[
			SNew(SCompositePlatePassPanel, Plate)
			.OnLayoutSizeChanged(this, &FCompositeLayerPlateCustomization::OnLayoutSizeChanged)
		];
	}  
	else
	{
		// Can't display plate pass panel if multiple plates are selected, so simply put a "Multiple Values" entry in the property list
		PlateCategory.AddCustomRow(LOCTEXT("PassesGroupName", "Passes"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PassesGroupName", "Passes"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("MultipleValues", "Multiple Values"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];
	}
}

void FCompositeLayerPlateCustomization::CustomizeTexturePropertyRow(const TSharedPtr<IPropertyHandle>& InPropertyHandle, IDetailPropertyRow& InPropertyRow)
{
	TexturePropertyHandle = InPropertyHandle;

	InPropertyRow.CustomWidget()
	.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		SNew(SObjectPropertyEntryBox)
		.PropertyHandle(InPropertyHandle)
		.AllowedClass(UTexture::StaticClass())
		.AllowCreate(true)
		.ThumbnailPool(CachedDetailBuilder.IsValid() ? CachedDetailBuilder.Pin()->GetThumbnailPool() : nullptr)
		.OnShouldFilterAsset_Lambda([](FAssetData const& AssetData)
		{
			if (UClass* AssetClass = AssetData.GetClass())
			{
				return !(AssetClass->IsChildOf(UTexture2D::StaticClass()) || AssetClass->IsChildOf(UMediaTexture::StaticClass()));
			}

			return true;
		})
		.CustomContentSlot()
		[
			SNew(SHorizontalBox)

			+SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SComboButton)
				.HasDownArrow(true)
				.ComboButtonStyle(FAppStyle::Get(), "SimpleComboButton")
				.ToolTipText(LOCTEXT("MediaProfileSourceSelectorToolTip", "Select media texture from a media source in the active Media Profile"))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnGetMenuContent(this, &FCompositeLayerPlateCustomization::GetMediaProfileSourceSelectorMenu)
				.IsEnabled(this, &FCompositeLayerPlateCustomization::HasActiveMediaProfile)
				.ButtonContent()
				[
					SNew(SImage)
					.Image(FCompositeEditorStyle::Get().GetBrush("CompositeEditor.MediaProfile"))
				]
			]
		]
	];
}

bool FCompositeLayerPlateCustomization::HasActiveMediaProfile() const
{
	UMediaProfile* ActiveMediaProfile = IMediaProfileManager::Get().GetCurrentMediaProfile();
	return IsValid(ActiveMediaProfile);
}

TSharedRef<SWidget> FCompositeLayerPlateCustomization::GetMediaProfileSourceSelectorMenu()
{
	UMediaProfile* ActiveMediaProfile = IMediaProfileManager::Get().GetCurrentMediaProfile();
	if (!ActiveMediaProfile)
	{
		return SNullWidget::NullWidget;
	}
	
	const bool bCloseAfterSelection = true;
	FMenuBuilder MenuBuilder(bCloseAfterSelection, nullptr);

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("MediaProfileSourcesSectionLabel", "Texture from Media Profile"));
	{
		bool bNoMediaSources = true;
		for (int32 Index = 0; Index < ActiveMediaProfile->NumMediaSources(); ++Index)
		{
			if (UMediaSource* MediaSource = ActiveMediaProfile->GetMediaSource(Index))
			{
				bNoMediaSources = false;

				MenuBuilder.AddMenuEntry(
					FText::FromString(ActiveMediaProfile->GetLabelForMediaSource(Index)),
					FText::GetEmpty(),
					FSlateIconFinder::FindIconForClass(MediaSource->GetClass()),
					FUIAction(FExecuteAction::CreateSP(this, &FCompositeLayerPlateCustomization::SetTextureToMediaProfileSource, Index))
				);
			}
		}

		if (bNoMediaSources)
		{
			TSharedRef<SWidget> TextBlock = SNew(STextBlock)
				.Text(LOCTEXT("NoMediaSources", "No Media Sources"))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 10));
			
			MenuBuilder.AddWidget(TextBlock,FText::GetEmpty());
		}
	}
	MenuBuilder.EndSection();

	MenuBuilder.AddSeparator();
	MenuBuilder.BeginSection(NAME_None);
	{
		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("OpenActiveMediaProfileLabelFormat", "Open {0}"), FText::FromString(ActiveMediaProfile->GetName())),
			FText::GetEmpty(),
			FSlateIconFinder::FindIconForClass(ActiveMediaProfile->StaticClass()),
			FUIAction(FExecuteAction::CreateSP(this, &FCompositeLayerPlateCustomization::OpenMediaProfile))
		);
	}
	MenuBuilder.EndSection();
	
	return MenuBuilder.MakeWidget();
}

void FCompositeLayerPlateCustomization::SetTextureToMediaProfileSource(int32 InMediaSourceIndex)
{
	UMediaProfile* ActiveMediaProfile = IMediaProfileManager::Get().GetCurrentMediaProfile();
	if (!ActiveMediaProfile)
	{
		return;
	}

	if (!IsValid(ActiveMediaProfile->GetMediaSource(InMediaSourceIndex)))
	{
		return;
	}

	UMediaTexture* MediaTexture = ActiveMediaProfile->GetPlaybackManager()->GetSourceMediaTextureFromIndex(InMediaSourceIndex);
	TexturePropertyHandle->SetValue(MediaTexture);
}

void FCompositeLayerPlateCustomization::OpenMediaProfile()
{
	UMediaProfile* ActiveMediaProfile = IMediaProfileManager::Get().GetCurrentMediaProfile();
	if (!ActiveMediaProfile)
	{
		return;
	}
	
	GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(ActiveMediaProfile);
}

void FCompositeLayerPlateCustomization::OnExtendCompositeMeshAddMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("PlaceCompositeMeshActorEntryLabel", "Place Composite Mesh Actor"),
		LOCTEXT("PlaceCompositeMeshActorEntryToolTip", "Place a composite mesh actor at the appropriate position in the level"),
		FSlateIcon(FCompositeEditorStyle::Get().GetStyleSetName(), "CompositeEditor.Composure"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FCompositeLayerPlateCustomization::CreateCompositeMeshActor),
			FCanExecuteAction::CreateSP(this, &FCompositeLayerPlateCustomization::CanCreateCompositeMeshActor))
	);
}

void FCompositeLayerPlateCustomization::CreateCompositeMeshActor()
{
	TSharedPtr<IDetailLayoutBuilder> PinnedDetailBuilder = CachedDetailBuilder.Pin();
	if (!PinnedDetailBuilder.IsValid())
	{
		return;
	}
	
	TArray<TWeakObjectPtr<UCompositeLayerPlate>> Objects = PinnedDetailBuilder->GetObjectsOfTypeBeingCustomized<UCompositeLayerPlate>();
	if (Objects.Num() != 1)
	{
		return;
	}

	TStrongObjectPtr<UCompositeLayerPlate> Plate = Objects[0].Pin();
	if (!Plate.IsValid())
	{
		return;
	}

	ACompositeActor* CompositeActor = Plate->GetTypedOuter<ACompositeActor>();
	if (!CompositeActor)
	{
		return;
	}

	FTransform SpawnTransform = FTransform::Identity;
	if (UCameraComponent* CameraComponent = Cast<UCameraComponent>(CompositeActor->GetCamera().GetComponent(nullptr)))
	{
		// Size of one wall of the composite mesh
		constexpr float MeshSize = 1000.0f;
		
		const float FieldOfView = FMath::DegreesToRadians(CameraComponent->FieldOfView);

		// The distance from the camera a wall of MeshSize needs to be to fill the camera's horizontal field of view
		const float Distance = 0.5f * MeshSize / FMath::Tan(0.5f * FieldOfView);
		
		const FVector Forward = CameraComponent->GetForwardVector();
		const FVector Right = CameraComponent->GetRightVector();
		const FVector Up = CameraComponent->GetUpVector();
		
		const FVector CameraLoc = CameraComponent->GetComponentLocation();

		// Mesh location is set it is horizontally centered with the camera
		const FVector MeshLoc = CameraLoc - 0.333 * MeshSize * Up + (Distance - MeshSize) * Forward;
		
		SpawnTransform.SetLocation(MeshLoc);
		
		FRotator Rotation = CameraComponent->GetComponentRotation();
		// Only preserve the yaw to keep the mesh horizontally level
		Rotation.Pitch = 0.0f;
		Rotation.Roll = 0.0f;
		Rotation.Yaw -= 45.0f; // Additional rotation so that the mesh corner is aligned with the camera direction
		SpawnTransform.SetRotation(Rotation.Quaternion());

		// Increase default mesh scale
		SpawnTransform.SetScale3D(2.0 * FVector::One());
	}

	FScopedTransaction AddCompositeMeshTransaction(LOCTEXT("AddCompositeMeshTransaction", "Add Composite Mesh"));
	ACompositeMeshActor* AddedMeshActor = Cast<ACompositeMeshActor>(CompositeActor->GetWorld()->SpawnActor(ACompositeMeshActor::StaticClass(), &SpawnTransform));
	if (AddedMeshActor)
	{
		Plate->Modify();

		FProperty* CompositeMeshesProperty = FindFProperty<FProperty>(UCompositeLayerPlate::StaticClass(), GET_MEMBER_NAME_CHECKED(UCompositeLayerPlate, CompositeMeshes));
		Plate->PreEditChange(CompositeMeshesProperty);

		Plate->CompositeMeshes.Add(AddedMeshActor);

		FPropertyChangedEvent ChangedEvent(CompositeMeshesProperty, EPropertyChangeType::ArrayAdd);
		Plate->PostEditChangeProperty(ChangedEvent);

		if (GEditor)
		{
			GEditor->SelectNone(true, true);
			GEditor->SelectActor(AddedMeshActor, true, true);

			bool bAlignViewport = true;
			if (ULevelEditorSubsystem* LevelEditorSubsystem = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>())
			{
				// Don't align the viewport if there is an active pilot actor, as that will cause the actor to be moved when the viewport
				// camera is moved
				bAlignViewport = !IsValid(LevelEditorSubsystem->GetPilotLevelActor());
			}

			if (bAlignViewport)
			{
				GUnrealEd->Exec(CompositeActor->GetWorld(), TEXT("CAMERA ALIGN ACTIVEVIEWPORTONLY"));
			}
		}
	}
}

bool FCompositeLayerPlateCustomization::CanCreateCompositeMeshActor() const
{
	TSharedPtr<IDetailLayoutBuilder> PinnedDetailBuilder = CachedDetailBuilder.Pin();
	if (!PinnedDetailBuilder.IsValid())
	{
		return false;
	}
	
	TArray<TWeakObjectPtr<UCompositeLayerPlate>> Objects = PinnedDetailBuilder->GetObjectsOfTypeBeingCustomized<UCompositeLayerPlate>();
	if (Objects.Num() != 1)
	{
		return false;
	}

	ACompositeActor* CompositeActor = Objects[0].IsValid() ? Objects[0]->GetTypedOuter<ACompositeActor>() : nullptr;
	if (!CompositeActor)
	{
		return false;
	}
	
	return true;
}

void FCompositeLayerPlateCustomization::OnLayoutSizeChanged()
{
	TSharedPtr<IDetailLayoutBuilder> PinnedDetailBuilder = CachedDetailBuilder.Pin();
	if (PinnedDetailBuilder.IsValid())
	{
		PinnedDetailBuilder->GetPropertyUtilities()->RequestRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
