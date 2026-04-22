// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DynamicMesh/DynamicMesh3.h"
#include "Renderers/Text3DRendererBase.h"
#include "UObject/ObjectPtr.h"
#include "Text3DDynamicMeshRenderer.generated.h"

class UDynamicMesh;
class UDynamicMeshComponent;

/**
 * Renderer that uses a single dynamic mesh component to render all characters
 * Text3DComponent
 * - Text3DDynamicMesh
 */
UCLASS(DisplayName="DynamicMeshRenderer", ClassGroup="Text3D", HideCategories=(Renderer))
class UText3DDynamicMeshRenderer : public UText3DRendererBase
{
	GENERATED_BODY()

protected:
	/** Used to store data about character glyph mesh */
	struct FGlyphMeshData
	{
		bool bVisible = true;
		uint16 GlyphIndex = 0;
		TArray<int32> MaterialSlots;
		FTransform CurrentTransform = FTransform::Identity;
		FBox Bounds = FBox(ForceInitToZero);
	};

	/** Mesh update type */
	enum class EMeshEditorUpdateType : uint8
	{
		None,
		Fast,
		Full
	};

	/** Used to push render changes when destroyed */
	struct FScopedMeshEditor
	{
		explicit FScopedMeshEditor(UDynamicMeshComponent* InComponent)
			: MeshComponent(InComponent)
		{}

		~FScopedMeshEditor();
		void EditMesh(const TFunctionRef<void(UE::Geometry::FDynamicMesh3&)>& InFunctor, EMeshEditorUpdateType InType);
		void ProcessMesh(const TFunctionRef<void(const UE::Geometry::FDynamicMesh3&)>& InFunctor) const;

	private:
		UDynamicMeshComponent* MeshComponent = nullptr;
		EMeshEditorUpdateType UpdateType = EMeshEditorUpdateType::None;
	};

	//~ Begin UText3DRendererBase
	virtual void OnCreate() override;
	virtual void OnUpdate(const UE::Text3D::Renderer::FUpdateParameters& InParameters) override;
	virtual void OnClear() override;
	virtual void OnDestroy() override;
	virtual FName GetFriendlyName() const override;
	virtual FBox OnCalculateBounds() const override;
#if WITH_EDITOR
	virtual void OnDebugModeEnabled() override;
	virtual void OnDebugModeDisabled() override;
#endif
	//~ End UText3DRendererBase

#if WITH_EDITOR
	void SetDebugMode(bool bInEnabled);
#endif

	void AllocateGlyphMeshData(FScopedMeshEditor& InMeshEditor, int32 InCount);
	void AppendGlyphMesh(FScopedMeshEditor& InMeshEditor, uint16 InIndex, uint32 InGlyphIndex, UDynamicMesh* InMesh);
	void ClearMesh();
	void ClearGlyphMesh(FScopedMeshEditor& InMeshEditor, uint16 InIndex);
	void SetGlyphMeshTransform(FScopedMeshEditor& InMeshEditor, uint16 InIndex, const FTransform& InTransform, bool bInVisible);
	
	void SaveMesh(const FScopedMeshEditor& InMeshEditor);
	void RestoreMesh(FScopedMeshEditor& InMeshEditor);

	UPROPERTY(VisibleAnywhere, Instanced, Category="Renderer", meta=(EditCondition="false", EditConditionHides))
	TObjectPtr<UDynamicMeshComponent> DynamicMeshComponent;

	TArray<FGlyphMeshData> GlyphMeshData;
	TOptional<FScopedMeshEditor> ScopedMeshEditor;
	TOptional<UE::Geometry::FDynamicMesh3> CachedMesh;
};
