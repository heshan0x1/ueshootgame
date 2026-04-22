// Copyright Epic Games, Inc. All Rights Reserved.

#include "Renderers/StaticMeshes/Text3DStaticMeshesRenderer.h"

#include "Characters/Text3DCharacterBase.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Extensions/Text3DGeometryExtensionBase.h"
#include "Extensions/Text3DLayoutExtensionBase.h"
#include "Extensions/Text3DMaterialExtensionBase.h"
#include "Extensions/Text3DRenderingExtensionBase.h"
#include "Logs/Text3DLogs.h"
#include "Text3DComponent.h"
#include "Text3DInternalTypes.h"
#include "Utilities/Text3DUtilities.h"

#if WITH_EDITOR
#include "Misc/ScopedSlowTask.h"
#endif

#define LOCTEXT_NAMESPACE "Text3DStaticMeshesRenderer"

void UText3DStaticMeshesRenderer::OnCreate()
{
	UText3DComponent* TextComponent = GetText3DComponent();
	AActor* Owner = TextComponent->GetOwner();
	const FAttachmentTransformRules AttachRule = FAttachmentTransformRules::SnapToTargetIncludingScale;
	
	if (!TextRoot)
	{
		TextRoot = NewObject<USceneComponent>(this, TEXT("TextRoot"));
		TextRoot->RegisterComponent();
		Owner->AddOwnedComponent(TextRoot);
		TextRoot->AttachToComponent(TextComponent, AttachRule);
	}
	else
	{
		FDetachmentTransformRules DetachRule = FDetachmentTransformRules::KeepRelativeTransform;
		DetachRule.bCallModify = false;
		TextRoot->DetachFromComponent(DetachRule);
		TextRoot->AttachToComponent(TextComponent, AttachRule);

		if (TextRoot->GetOuter() != this)
		{
			TextRoot->Rename(nullptr, this, REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
		}
	}

	AllocateComponents(0);
}

void UText3DStaticMeshesRenderer::OnUpdate(const UE::Text3D::Renderer::FUpdateParameters& InParameters)
{
	const UText3DComponent* TextComponent = GetText3DComponent();

	if (!TextRoot)
	{
		UE_LOG(LogText3D, Warning, TEXT("Invalid root component for %s renderer, cannot proceed!"), *GetFriendlyName().ToString())
		return;
	}

	if (InParameters.CurrentFlag == EText3DRendererFlags::Geometry)
	{
		AllocateComponents(TextComponent->GetCharacterCount());

		TextComponent->ForEachCharacter([this](UText3DCharacterBase* InCharacter, uint16 InIndex, uint16 InCount)
		{
			if (CharacterMeshes.IsValidIndex(InIndex))
			{
				if (UStaticMeshComponent* CharacterMeshComponent = CharacterMeshes[InIndex])
				{
					if (const FText3DCachedMesh* CachedMesh = InCharacter->GetGlyphMesh())
					{
						CharacterMeshComponent->SetStaticMesh(CachedMesh->StaticMesh);
					}
					else
					{
						CharacterMeshComponent->SetStaticMesh(nullptr);
					}
				}
				else
				{
					UE_LOG(LogText3D, Warning, TEXT("Invalid component object for character %i"), InIndex)
				}
			}
			else
			{
				UE_LOG(LogText3D, Warning, TEXT("Invalid component index for character %i"), InIndex)
			}
		});

		RefreshBounds();
	}
	else if (InParameters.CurrentFlag == EText3DRendererFlags::Layout)
	{
		const UText3DLayoutExtensionBase* LayoutExtension = TextComponent->GetLayoutExtension();

		TextRoot->SetRelativeScale3D(LayoutExtension->GetTextScale());

		TextComponent->ForEachCharacter([this](UText3DCharacterBase* InCharacter, uint16 InIndex, uint16 InCount)
		{
			constexpr bool bReset = false;
			const FTransform& CharacterTransform = InCharacter->GetTransform(bReset);

			if (CharacterMeshes.IsValidIndex(InIndex))
			{
				if (UStaticMeshComponent* CharacterMeshComponent = CharacterMeshes[InIndex])
				{
					CharacterMeshComponent->SetRelativeTransform(CharacterTransform);
				}
				else
				{
					UE_LOG(LogText3D, Warning, TEXT("Invalid component object for character %i"), InIndex)
				}
			}
			else
			{
				UE_LOG(LogText3D, Warning, TEXT("Invalid component index for character %i"), InIndex)
			}
		});

		RefreshBounds();
	}
	else if (InParameters.CurrentFlag == EText3DRendererFlags::Material)
	{
		using namespace UE::Text3D::Material;

		const UText3DMaterialExtensionBase* MaterialExtension = TextComponent->GetMaterialExtension();

		TextComponent->ForEachCharacter([this, MaterialExtension](UText3DCharacterBase* InCharacter, uint16 InIndex, uint16 InCount)
		{
			if (CharacterMeshes.IsValidIndex(InIndex))
			{
				if (UStaticMeshComponent* CharacterMeshComponent = CharacterMeshes[InIndex])
				{
					for (int32 GroupIndex = 0; GroupIndex < static_cast<int32>(EText3DGroupType::TypeCount); GroupIndex++)
					{
						const int32 MaterialIndex = CharacterMeshComponent->GetMaterialIndex(SlotNames[GroupIndex]);

						if (MaterialIndex == INDEX_NONE)
						{
							continue;
						}

						const EText3DGroupType GroupType = static_cast<EText3DGroupType>(GroupIndex);
						const FName StyleTag = InCharacter->GetStyleTag();

						FMaterialParameters Parameters;
						Parameters.Group = GroupType;
						Parameters.Tag = StyleTag;

						UMaterialInterface* Material = MaterialExtension->GetMaterial(Parameters);

						if (Material != CharacterMeshComponent->GetMaterial(MaterialIndex))
						{
							CharacterMeshComponent->SetMaterial(MaterialIndex, Material);
						}
					}
				}
				else
				{
					UE_LOG(LogText3D, Warning, TEXT("Invalid component object for character %i"), InIndex)
				}
			}
			else
			{
				UE_LOG(LogText3D, Warning, TEXT("Invalid component index for character %i"), InIndex)
			}
		});
	}
	else if (InParameters.CurrentFlag == EText3DRendererFlags::Visibility)
	{
		const UText3DRenderingExtensionBase* RenderingExtension = TextComponent->GetRenderingExtension();

		TextComponent->ForEachCharacter([this, TextComponent, RenderingExtension](UText3DCharacterBase* InCharacter, uint16 InIndex, uint16 InCount)
		{
			if (CharacterMeshes.IsValidIndex(InIndex))
			{
				if (UStaticMeshComponent* CharacterMeshComponent = CharacterMeshes[InIndex])
				{
					CharacterMeshComponent->SetHiddenInGame(TextComponent->bHiddenInGame);
					CharacterMeshComponent->SetVisibility(TextComponent->GetVisibleFlag() && InCharacter->GetVisibility());
					CharacterMeshComponent->SetCastShadow(RenderingExtension->GetTextCastShadow());
					CharacterMeshComponent->SetCastHiddenShadow(RenderingExtension->GetTextCastHiddenShadow());
					CharacterMeshComponent->SetAffectDynamicIndirectLighting(RenderingExtension->GetTextAffectDynamicIndirectLighting());
					CharacterMeshComponent->SetAffectIndirectLightingWhileHidden(RenderingExtension->GetTextAffectIndirectLightingWhileHidden());
					CharacterMeshComponent->SetHoldout(RenderingExtension->GetTextHoldout());
				}
				else
				{
					UE_LOG(LogText3D, Warning, TEXT("Invalid component object for character %i"), InIndex)
				}
			}
			else
			{
				UE_LOG(LogText3D, Warning, TEXT("Invalid component index for character %i"), InIndex)
			}
		});
	}
}

void UText3DStaticMeshesRenderer::OnClear()
{
	AllocateComponents(0);
}

void UText3DStaticMeshesRenderer::OnDestroy()
{
	AllocateComponents(0);
	CharacterMeshes.Reset();

	for (UStaticMeshComponent* MeshComponent : CharacterMeshesPool)
	{
		if (MeshComponent)
		{
			MeshComponent->DestroyComponent();
		}
	}
	CharacterMeshesPool.Reset();

	if (TextRoot)
	{
		TextRoot->DestroyComponent();
		TextRoot = nullptr;
	}
}

FName UText3DStaticMeshesRenderer::GetFriendlyName() const
{
	static const FName Name(TEXT("StaticMeshesRenderer"));
	return Name;
}

FBox UText3DStaticMeshesRenderer::OnCalculateBounds() const
{
	FBox Box(ForceInit);

	for (const UStaticMeshComponent* StaticMeshComponent : CharacterMeshes)
	{
		Box += StaticMeshComponent->Bounds.GetBox();
	}

	return Box;
}

void UText3DStaticMeshesRenderer::AllocateComponents(int32 InCount)
{
	CharacterMeshes.RemoveAll([](const UStaticMeshComponent* InComponent)
		{
			return !InComponent;
		}
	);

	if (CharacterMeshes.Num() == InCount)
	{
		return;
	}

	CharacterMeshes.Reserve(InCount);

	const int32 RemainingCharacterCount = CharacterMeshes.Num() - InCount;

	const UText3DComponent* TextComponent = GetText3DComponent();
	AActor* Actor = TextComponent->GetOwner();

	if (RemainingCharacterCount > 0)
	{
		FDetachmentTransformRules DetachmentRule = FDetachmentTransformRules::KeepRelativeTransform;
		DetachmentRule.bCallModify = false;

		for (int32 CharacterIndex = InCount; CharacterIndex < CharacterMeshes.Num(); CharacterIndex++)
		{
			if (UStaticMeshComponent* MeshComponent = CharacterMeshes[CharacterIndex])
			{
				MeshComponent->DetachFromComponent(DetachmentRule);
				if (MeshComponent->IsRegistered())
				{
					MeshComponent->UnregisterComponent();
				}
				Actor->RemoveInstanceComponent(MeshComponent);
				Actor->RemoveOwnedComponent(MeshComponent);

				if (MeshComponent->GetOwner() == Actor)
				{
					CharacterMeshesPool.Add(MeshComponent);
				}
			}
		}
	}
	else if (RemainingCharacterCount < 0)
	{
		for (int32 CharacterIndex = 0; CharacterIndex < FMath::Abs(RemainingCharacterCount); ++CharacterIndex)
		{
			UStaticMeshComponent* MeshComponent = nullptr;
			while (!CharacterMeshesPool.IsEmpty() && !MeshComponent)
			{
				MeshComponent = CharacterMeshesPool.Pop();
			}

			if (!MeshComponent)
			{
				const FName StaticMeshComponentName = MakeUniqueObjectName(this, UStaticMeshComponent::StaticClass(), FName(TEXT("CharacterMesh")));
				MeshComponent = NewObject<UStaticMeshComponent>(this, StaticMeshComponentName);
			}

			if (MeshComponent)
			{
				CharacterMeshes.Add(MeshComponent);
				Actor->AddOwnedComponent(MeshComponent);
				MeshComponent->RegisterComponent();
				MeshComponent->AttachToComponent(TextRoot, FAttachmentTransformRules::SnapToTargetIncludingScale);
			}
		}
	}

	CharacterMeshes.SetNum(InCount);
}

int32 UText3DStaticMeshesRenderer::GetGlyphCount()
{
	if (!TextRoot)
	{
		return 0;
	}

	return TextRoot->GetNumChildrenComponents();
}

UStaticMeshComponent* UText3DStaticMeshesRenderer::GetGlyphMeshComponent(int32 Index)
{
	if (!CharacterMeshes.IsValidIndex(Index))
	{
		return nullptr;
	}

	return CharacterMeshes[Index];
}

const TArray<UStaticMeshComponent*>& UText3DStaticMeshesRenderer::GetGlyphMeshComponents()
{
	return CharacterMeshes;
}

#if WITH_EDITOR
void UText3DStaticMeshesRenderer::ConvertToStaticMesh()
{
	if (!IsValid(this))
	{
		return;
	}

	const UText3DComponent* Text3DComponent = GetText3DComponent();
	if (!IsValid(Text3DComponent) || !IsValid(Text3DComponent->GetOwner()))
	{
		return;
	}

	const AActor* Owner = Text3DComponent->GetOwner();
	FScopedSlowTask SlowTask(0.0f, LOCTEXT("ConvertToStaticMesh", "Converting Text3D to static mesh"));
	SlowTask.MakeDialog();

	UE_LOG(LogText3D, Log, TEXT("%s : Request ConvertToStaticMesh..."), *Owner->GetActorNameOrLabel())

	if (UE::Text3D::Utilities::Conversion::ConvertToStaticMesh(Text3DComponent))
	{
		UE_LOG(LogText3D, Log, TEXT("%s : ConvertToStaticMesh Completed"), *Owner->GetActorNameOrLabel())
	}
	else
	{
		UE_LOG(LogText3D, Warning, TEXT("%s : ConvertToStaticMesh Failed"), *Owner->GetActorNameOrLabel())
	}
}

void UText3DStaticMeshesRenderer::OnDebugModeEnabled()
{
	Super::OnDebugModeEnabled();
	SetDebugMode(true);
}

void UText3DStaticMeshesRenderer::OnDebugModeDisabled()
{
	Super::OnDebugModeDisabled();
	SetDebugMode(false);
}

void UText3DStaticMeshesRenderer::SetDebugMode(bool bEnabled)
{
	// Since we are dealing with class FProperty, no need to run this for each instance, do it once
	static bool bDebugModeEnabled = true;

	if (bDebugModeEnabled != bEnabled)
	{
		bDebugModeEnabled = bEnabled;

		FProperty* TextRootProperty = FindFProperty<FProperty>(UText3DStaticMeshesRenderer::StaticClass(), GET_MEMBER_NAME_CHECKED(UText3DStaticMeshesRenderer, TextRoot));
		FArrayProperty* CharacterMeshesProperty = FindFProperty<FArrayProperty>(UText3DStaticMeshesRenderer::StaticClass(), GET_MEMBER_NAME_CHECKED(UText3DStaticMeshesRenderer, CharacterMeshes));

		// Here we toggle the CPF_Edit flag to hide/show property in the details panel component editor tree / outliner
		// @see FComponentEditorUtils::GetPropertyForEditableNativeComponent
		// todo : implement a custom debug view widget for text or add a property editor metadata to control the component visibility in the component editor tree / outliner
		if (bEnabled)
		{
			TextRootProperty->SetPropertyFlags(CPF_Edit);
			CharacterMeshesProperty->SetPropertyFlags(CPF_Edit);
		}
		else
		{
			TextRootProperty->ClearPropertyFlags(CPF_Edit);
			CharacterMeshesProperty->ClearPropertyFlags(CPF_Edit);
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE 