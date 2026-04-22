// Copyright Epic Games, Inc. All Rights Reserved.

#include "Renderers/DynamicMesh/Text3DDynamicMeshRenderer.h"

#include "Characters/Text3DCharacterBase.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMeshEditor.h"
#include "Engine/CollisionProfile.h"
#include "Extensions/Text3DGeometryExtensionBase.h"
#include "Extensions/Text3DLayoutExtensionBase.h"
#include "Extensions/Text3DMaterialExtensionBase.h"
#include "Extensions/Text3DRenderingExtensionBase.h"
#include "Subsystems/Text3DEngineSubsystem.h"
#include "Text3DComponent.h"
#include "Text3DInternalTypes.h"

UText3DDynamicMeshRenderer::FScopedMeshEditor::~FScopedMeshEditor()
{
	if (UpdateType != EMeshEditorUpdateType::None && IsValid(MeshComponent))
	{
		if (UpdateType == EMeshEditorUpdateType::Fast)
		{
			MeshComponent->NotifyMeshVertexAttributesModified();
		}
		else if (UpdateType == EMeshEditorUpdateType::Full)
		{
			MeshComponent->NotifyMeshUpdated();
		}

		MeshComponent->OnMeshChanged.Broadcast();
	}
}

void UText3DDynamicMeshRenderer::FScopedMeshEditor::EditMesh(const TFunctionRef<void(UE::Geometry::FDynamicMesh3&)>& InFunctor, EMeshEditorUpdateType InType)
{
	if (MeshComponent && MeshComponent->GetDynamicMesh())
	{
		constexpr bool bDeferChanges = true;
		MeshComponent->GetDynamicMesh()->EditMesh(InFunctor, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, bDeferChanges);
		UpdateType = InType > UpdateType ? InType : UpdateType;
	}
}

void UText3DDynamicMeshRenderer::FScopedMeshEditor::ProcessMesh(const TFunctionRef<void(const UE::Geometry::FDynamicMesh3&)>& InFunctor) const
{
	if (MeshComponent && MeshComponent->GetDynamicMesh())
	{
		MeshComponent->GetDynamicMesh()->ProcessMesh(InFunctor);
	}
}

void UText3DDynamicMeshRenderer::OnCreate()
{
	UText3DComponent* TextComponent = GetText3DComponent();
	AActor* Owner = TextComponent->GetOwner();
	const FAttachmentTransformRules AttachRule = FAttachmentTransformRules::SnapToTargetIncludingScale;

	if (!DynamicMeshComponent)
	{
		DynamicMeshComponent = NewObject<UDynamicMeshComponent>(this, TEXT("Text3DDynamicMesh"));
		DynamicMeshComponent->RegisterComponent();
		Owner->AddOwnedComponent(DynamicMeshComponent);
		DynamicMeshComponent->AttachToComponent(TextComponent, AttachRule);

		// Setup collision
		DynamicMeshComponent->SetComplexAsSimpleCollisionEnabled(true);
		DynamicMeshComponent->SetDeferredCollisionUpdatesEnabled(true);
		DynamicMeshComponent->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

		ClearMesh();
	}
	else
	{
		FDetachmentTransformRules DetachRule = FDetachmentTransformRules::KeepRelativeTransform;
		DetachRule.bCallModify = false;
		DynamicMeshComponent->DetachFromComponent(DetachRule);
		DynamicMeshComponent->AttachToComponent(TextComponent, AttachRule);
	}
}

void UText3DDynamicMeshRenderer::OnUpdate(const UE::Text3D::Renderer::FUpdateParameters& InParameters)
{
	const UText3DComponent* TextComponent = GetText3DComponent();

	if (!ScopedMeshEditor.IsSet())
	{
		ScopedMeshEditor = FScopedMeshEditor(DynamicMeshComponent);

		// Restore checkpoint in case mesh was altered externally
		RestoreMesh(ScopedMeshEditor.GetValue());
	}

	FScopedMeshEditor& MeshEditor = ScopedMeshEditor.GetValue();

	if (InParameters.CurrentFlag == EText3DRendererFlags::Geometry)
	{
		AllocateGlyphMeshData(MeshEditor, TextComponent->GetCharacterCount());
		
		TextComponent->ForEachCharacter([this, &MeshEditor](UText3DCharacterBase* InCharacter, uint16 InIndex, uint16 InCount)
		{
			if (const FText3DCachedMesh* CachedGlyph = InCharacter->GetGlyphMesh())
			{
				AppendGlyphMesh(MeshEditor, InIndex, InCharacter->GetGlyphIndex(), CachedGlyph->DynamicMesh);
			}
			else
			{
				ClearGlyphMesh(MeshEditor, InIndex);
			}
		});

		RefreshBounds();
	}
	else if (InParameters.CurrentFlag == EText3DRendererFlags::Layout)
	{
		const UText3DLayoutExtensionBase* LayoutExtension = TextComponent->GetLayoutExtension();

		DynamicMeshComponent->SetRelativeScale3D(LayoutExtension->GetTextScale());

		TextComponent->ForEachCharacter([this, &MeshEditor](UText3DCharacterBase* InCharacter, uint16 InIndex, uint16 InCount)
		{
			constexpr bool bReset = false;
			const FTransform& CharacterTransform = InCharacter->GetTransform(bReset);
			SetGlyphMeshTransform(MeshEditor, InIndex, CharacterTransform, InCharacter->GetVisibility());
		});

		RefreshBounds();
	}
	else if (InParameters.CurrentFlag == EText3DRendererFlags::Material)
	{
		using namespace UE::Text3D::Material;

		const UText3DMaterialExtensionBase* MaterialExtension = TextComponent->GetMaterialExtension();

		TextComponent->ForEachCharacter([this, MaterialExtension](UText3DCharacterBase* InCharacter, uint16 InIndex, uint16 InCount)
		{
			for (const int32 MaterialSlot : GlyphMeshData[InIndex].MaterialSlots)
			{
				const FName StyleTag = InCharacter->GetStyleTag();
				const EText3DGroupType GroupType = static_cast<EText3DGroupType>(MaterialSlot - (InIndex * SlotNames.Num()));

				FMaterialParameters Parameters;
				Parameters.Group = GroupType;
				Parameters.Tag = StyleTag;

				UMaterialInterface* Material = MaterialExtension->GetMaterial(Parameters);

				if (Material != DynamicMeshComponent->GetMaterial(MaterialSlot))
				{
					DynamicMeshComponent->SetMaterial(MaterialSlot, Material);
				}
			}
		});
	}
	else if (InParameters.CurrentFlag == EText3DRendererFlags::Visibility)
	{
		const UText3DRenderingExtensionBase* RenderingExtension = TextComponent->GetRenderingExtension();

		DynamicMeshComponent->SetHiddenInGame(TextComponent->bHiddenInGame);
		DynamicMeshComponent->SetCastShadow(RenderingExtension->GetTextCastShadow());
		DynamicMeshComponent->SetCastHiddenShadow(RenderingExtension->GetTextCastHiddenShadow());
		DynamicMeshComponent->SetAffectDynamicIndirectLighting(RenderingExtension->GetTextAffectDynamicIndirectLighting());
		DynamicMeshComponent->SetAffectIndirectLightingWhileHidden(RenderingExtension->GetTextAffectIndirectLightingWhileHidden());
		DynamicMeshComponent->SetHoldout(RenderingExtension->GetTextHoldout());
		DynamicMeshComponent->SetVisibility(TextComponent->GetVisibleFlag());

		TextComponent->ForEachCharacter([this, &MeshEditor](UText3DCharacterBase* InCharacter, uint16 InIndex, uint16 InCount)
		{
			constexpr bool bReset = false;
			const FTransform& CharacterTransform = InCharacter->GetTransform(bReset);
			SetGlyphMeshTransform(MeshEditor, InIndex, CharacterTransform, InCharacter->GetVisibility());
		});
	}

	if (InParameters.bIsLastFlag)
	{
		// Save checkpoint to restore mesh on next update
		SaveMesh(MeshEditor);

		ScopedMeshEditor.Reset();
	}
}

void UText3DDynamicMeshRenderer::OnClear()
{
	if (DynamicMeshComponent)
	{
		DynamicMeshComponent->EditMesh([](FDynamicMesh3& InEditMesh)
		{
			for (const int32 TId : InEditMesh.TriangleIndicesItr())
			{
				InEditMesh.RemoveTriangle(TId);
			}
		});
	}
}

void UText3DDynamicMeshRenderer::OnDestroy()
{
	if (DynamicMeshComponent)
	{
		DynamicMeshComponent->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
		DynamicMeshComponent->DestroyComponent();
		DynamicMeshComponent = nullptr;
	}
}

FName UText3DDynamicMeshRenderer::GetFriendlyName() const
{
	static const FName Name(TEXT("DynamicMeshRenderer"));
	return Name;
}

FBox UText3DDynamicMeshRenderer::OnCalculateBounds() const
{
	FBox Box(ForceInit);
	Box += DynamicMeshComponent->Bounds.GetBox();
	return Box;
}

#if WITH_EDITOR
void UText3DDynamicMeshRenderer::OnDebugModeEnabled()
{
	Super::OnDebugModeEnabled();
	SetDebugMode(true);
}

void UText3DDynamicMeshRenderer::OnDebugModeDisabled()
{
	Super::OnDebugModeDisabled();
	SetDebugMode(false);
}

void UText3DDynamicMeshRenderer::SetDebugMode(bool bInEnabled)
{
	// Since we are dealing with class FProperty, no need to run this for each instance, do it once
	static bool bDebugModeEnabled = true;

	if (bDebugModeEnabled != bInEnabled)
	{
		bDebugModeEnabled = bInEnabled;

		FProperty* DynamicMeshComponentProperty = FindFProperty<FProperty>(UText3DDynamicMeshRenderer::StaticClass(), GET_MEMBER_NAME_CHECKED(UText3DDynamicMeshRenderer, DynamicMeshComponent));

		// Here we toggle the CPF_Edit flag to hide/show property in the details panel component editor tree / outliner
		// @see FComponentEditorUtils::GetPropertyForEditableNativeComponent
		// todo : implement a custom debug view widget for text or add a property editor metadata to control the component visibility in the component editor tree / outliner
		if (bInEnabled)
		{
			DynamicMeshComponentProperty->SetPropertyFlags(CPF_Edit);
		}
		else
		{
			DynamicMeshComponentProperty->ClearPropertyFlags(CPF_Edit);
		}
	}
}
#endif

void UText3DDynamicMeshRenderer::AllocateGlyphMeshData(FScopedMeshEditor& InMeshEditor, int32 InCount)
{
	if (GlyphMeshData.Num() > InCount)
	{
		for (int32 Index = InCount; Index < GlyphMeshData.Num(); Index++)
		{
			ClearGlyphMesh(InMeshEditor, Index);
		}
	}

	GlyphMeshData.SetNum(InCount);
}

void UText3DDynamicMeshRenderer::AppendGlyphMesh(FScopedMeshEditor& InMeshEditor, uint16 InIndex, uint32 InGlyphIndex, UDynamicMesh* InMesh)
{
	using namespace UE::Geometry;

	check(!!InMesh);

	if (!GlyphMeshData.IsValidIndex(InIndex))
	{
		return;
	}

	FBox Bounds = FBox(ForceInitToZero);
	InMesh->ProcessMesh([&Bounds](const FDynamicMesh3& InMesh)
	{
		Bounds = static_cast<FBox>(InMesh.GetBounds(true));
	});

	if (GlyphMeshData[InIndex].GlyphIndex == InGlyphIndex && GlyphMeshData[InIndex].Bounds.Equals(Bounds))
	{
		return;
	}

	ClearGlyphMesh(InMeshEditor, InIndex);

	GlyphMeshData[InIndex].GlyphIndex = InGlyphIndex;
	GlyphMeshData[InIndex].Bounds = Bounds;
	GlyphMeshData[InIndex].CurrentTransform = FTransform::Identity;

	InMeshEditor.EditMesh(
		[this, InMesh, InIndex, InGlyphIndex](FDynamicMesh3& InEditMesh)
		{
			InMesh->ProcessMesh([this, &InEditMesh, InIndex, InGlyphIndex](const FDynamicMesh3& InProcessMesh)
			{
				FMeshIndexMappings Mappings;
				FDynamicMeshEditor Editor(&InEditMesh);
				Editor.AppendMesh(&InProcessMesh, Mappings);

				TArray<int32> NewTriangles;
				const TMap<int32, int32>& FromToMap = Mappings.GetTriangleMap().GetForwardMap();
				FDynamicMeshMaterialAttribute* MaterialAttribute = InEditMesh.Attributes()->GetMaterialID();

				TArray<int32> MaterialSlots;
				for (const TPair<int32, int32>& FromToPair : FromToMap)
				{
					InEditMesh.SetTriangleGroup(FromToPair.Value, InIndex);
					const int32 MaterialID = (InIndex * UE::Text3D::Material::SlotNames.Num()) + MaterialAttribute->GetValue(FromToPair.Value);
					MaterialAttribute->SetValue(FromToPair.Value, MaterialID);
					MaterialSlots.AddUnique(MaterialID);
				}

				MaterialSlots.StableSort([](int32 A, int32 B)
				{
					return A > B;
				});

				GlyphMeshData[InIndex].MaterialSlots = MoveTemp(MaterialSlots);
			});
		}, EMeshEditorUpdateType::Full);
}

void UText3DDynamicMeshRenderer::ClearMesh()
{
	GlyphMeshData.Reset();
	DynamicMeshComponent->EditMesh([](FDynamicMesh3& InEditMesh)
	{
		InEditMesh.Clear();
		InEditMesh.EnableTriangleGroups();
		InEditMesh.EnableAttributes();
		InEditMesh.Attributes()->EnableMaterialID();
		InEditMesh.Attributes()->EnableTangents();
		InEditMesh.Attributes()->SetNumUVLayers(1);
	});
}

void UText3DDynamicMeshRenderer::ClearGlyphMesh(FScopedMeshEditor& InMeshEditor, uint16 InIndex)
{
	using namespace UE::Geometry;

	// Remove old glyph data
	InMeshEditor.EditMesh(
		[this, InIndex](FDynamicMesh3& InEditMesh)
		{
			TArray<int32> TrianglesToDelete;
			for (const int32 Tid : InEditMesh.TriangleIndicesItr())
			{
				if (InEditMesh.GetTriangleGroup(Tid) == InIndex)
				{
					TrianglesToDelete.Add(Tid);
				}
			}

			FDynamicMeshEditor Editor(&InEditMesh);
			Editor.RemoveTriangles(TrianglesToDelete, /** RemoveIsolatedVertices*/ true);
			InEditMesh.CompactInPlace();
		}, EMeshEditorUpdateType::Full);
}

void UText3DDynamicMeshRenderer::SetGlyphMeshTransform(FScopedMeshEditor& InMeshEditor, uint16 InIndex, const FTransform& InTransform, bool bInVisible)
{
	using namespace UE::Geometry;

	if (!GlyphMeshData.IsValidIndex(InIndex))
	{
		return;
	}

	FGlyphMeshData& GlyphData = GlyphMeshData[InIndex];

	if (!GlyphData.bVisible && GlyphData.bVisible == bInVisible)
	{
		return;
	}

	GlyphData.bVisible = bInVisible;

	const FVector TargetScale = GlyphData.bVisible ? InTransform.GetScale3D().ComponentMax(FVector(UE_KINDA_SMALL_NUMBER)) : FVector(UE_KINDA_SMALL_NUMBER);
	const FTransform TargetTransform(InTransform.GetRotation(), InTransform.GetTranslation(), TargetScale);

	if (GlyphData.CurrentTransform.Equals(TargetTransform))
	{
		return;
	}

	InMeshEditor.EditMesh([InIndex, &GlyphData, &TargetTransform](FDynamicMesh3& InEditMesh)
	{
		FDynamicMeshNormalOverlay* Normals = InEditMesh.Attributes()->PrimaryNormals();
		FDynamicMeshNormalOverlay* Tangents = InEditMesh.Attributes()->PrimaryTangents();

		TSet<int32> VerticesToTransform;
		TSet<int32> NormalsToTransform;
		TSet<int32> TangentsToTransform;
		for (const int32 TId : InEditMesh.TriangleIndicesItr())
		{
			if (InEditMesh.GetTriangleGroup(TId) == InIndex)
			{
				FIndex3i Tri = InEditMesh.GetTriangle(TId);

				VerticesToTransform.Add(Tri.A);
				VerticesToTransform.Add(Tri.B);
				VerticesToTransform.Add(Tri.C);

				NormalsToTransform.Add(Normals->GetElementIDAtVertex(TId, Tri.A));
				NormalsToTransform.Add(Normals->GetElementIDAtVertex(TId, Tri.B));
				NormalsToTransform.Add(Normals->GetElementIDAtVertex(TId, Tri.C));

				TangentsToTransform.Add(Tangents->GetElementIDAtVertex(TId, Tri.A));
				TangentsToTransform.Add(Tangents->GetElementIDAtVertex(TId, Tri.B));
				TangentsToTransform.Add(Tangents->GetElementIDAtVertex(TId, Tri.C));
			}
		}

		const FTransform3d Inverse = GlyphData.CurrentTransform.Inverse();
		for (const int32 VId : VerticesToTransform)
		{
			FVector3d LocalPos = Inverse.TransformPosition(InEditMesh.GetVertex(VId));
			FVector3d NewWorldPos = TargetTransform.TransformPosition(LocalPos);
			InEditMesh.SetVertex(VId, NewWorldPos);
		}

		const FQuat DeltaRotation = TargetTransform.GetRotation() * GlyphData.CurrentTransform.GetRotation().Inverse();
		for (const int32 NId : NormalsToTransform)
		{
			if (NId != FDynamicMesh3::InvalidID)
			{
				FVector3f Normal = Normals->GetElement(NId);
				FVector3d NewNormal = DeltaRotation.RotateVector((FVector3d)Normal);
				Normals->SetElement(NId, (FVector3f)NewNormal.GetSafeNormal());
			}
		}

		for (const int32 TId : TangentsToTransform)
		{
			if (TId != FDynamicMesh3::InvalidID)
			{
				FVector3f Tangent = Tangents->GetElement(TId);
				FVector3d NewTangent = DeltaRotation.RotateVector((FVector3d)Tangent);
				Tangents->SetElement(TId, (FVector3f)NewTangent.GetSafeNormal());	
			}
		}
	}, EMeshEditorUpdateType::Fast);

	GlyphData.CurrentTransform = TargetTransform;
}

void UText3DDynamicMeshRenderer::SaveMesh(const FScopedMeshEditor& InMeshEditor)
{
	InMeshEditor.ProcessMesh([this](const FDynamicMesh3& EditMesh)
	{
		CachedMesh = EditMesh;
	});
}

void UText3DDynamicMeshRenderer::RestoreMesh(FScopedMeshEditor& InMeshEditor)
{
	if (CachedMesh.IsSet())
	{
		InMeshEditor.EditMesh([this](FDynamicMesh3& EditMesh)
		{
			EditMesh = MoveTemp(CachedMesh.GetValue());
			CachedMesh.Reset();
		}, EMeshEditorUpdateType::Fast);
	}
}
