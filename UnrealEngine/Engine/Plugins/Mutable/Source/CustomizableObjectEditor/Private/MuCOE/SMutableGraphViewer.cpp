// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/SMutableGraphViewer.h"

#include "DesktopPlatformModule.h"
#include "EditorDirectories.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Views/TableViewMetadata.h"
#include "IDesktopPlatform.h"
#include "Misc/Paths.h"
#include "MuCOE/CustomizableObjectCompileRunnable.h"
#include "MuCOE/CustomizableObjectEditorStyle.h"
#include "MuCOE/SMutableCodeViewer.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Input/SNumericDropDown.h"
#include "Widgets/Views/STreeView.h"
#include "ScopedTransaction.h"
#include "MuT/NodeColourConstant.h"
#include "MuT/NodeColourFromScalars.h"
#include "MuT/NodeColourParameter.h"
#include "MuT/NodeColourSampleImage.h"
#include "MuT/NodeColourSwitch.h"
#include "MuT/NodeComponentEdit.h"
#include "MuT/NodeComponentSwitch.h"
#include "MuT/NodeImageFormat.h"
#include "MuT/NodeImageInterpolate.h"
#include "MuT/NodeImageInvert.h"
#include "MuT/NodeImageLayer.h"
#include "MuT/NodeImageLayerColour.h"
#include "MuT/NodeImageMipmap.h"
#include "MuT/NodeImageMultiLayer.h"
#include "MuT/NodeImagePlainColour.h"
#include "MuT/NodeImageProject.h"
#include "MuT/NodeImageResize.h"
#include "MuT/NodeImageSwitch.h"
#include "MuT/NodeImageSwizzle.h"
#include "MuT/NodeImageTable.h"
#include "MuT/NodeObjectGroup.h"
#include "MuT/NodeObjectNew.h"
#include "MuT/NodeSurfaceNew.h"
#include "MuT/NodeSurfaceSwitch.h"
#include "MuT/NodeSurfaceVariation.h"
#include "MuT/NodeLOD.h"
#include "MuT/NodeMeshConstant.h"
#include "MuT/NodeMeshFormat.h"
#include "MuT/NodeMeshFragment.h"
#include "MuT/NodeMeshMakeMorph.h"
#include "MuT/NodeMeshMorph.h"
#include "MuT/NodeMeshTable.h"
#include "MuT/NodeModifierMeshClipDeform.h"
#include "MuT/NodeModifierMeshClipMorphPlane.h"
#include "MuT/NodeModifierMeshClipWithUVMask.h"
#include "MuT/NodeModifierMeshClipMorphPlane.h"
#include "MuT/NodeModifierSurfaceEdit.h"
#include "MuT/NodeScalarConstant.h"
#include "MuT/NodeScalarCurve.h"
#include "MuT/NodeScalarSwitch.h"
#include "MuT/NodeScalarTable.h"
#include "MuT/NodeObjectNew.h"
#include "MuT/NodeModifierMeshTransformInMesh.h"
#include "MuT/NodeModifierMeshTransformWithBone.h"
#include "Widgets/MutableExpanderArrow.h"

class FExtender;
class FReferenceCollector;
class FUICommandList;
class ITableRow;
class SWidget;
struct FGeometry;
struct FSlateBrush;

#define LOCTEXT_NAMESPACE "SMutableDebugger"

// \todo: multi-column tree
namespace MutableGraphTreeViewColumns
{
	static const FName Name("Name");
};

static const char* MutableNodeNames[] =
{
	"None",

	"Node",

	"Mesh",
	"MeshConstant",
	"MeshTable",
	"MeshFormat",
	"MeshTangents",
	"MeshMorph",
	"MeshMakeMorph",
	"MeshSwitch",
	"MeshFragment",
	"MeshTransform",
	"MeshClipMorphPlane",
	"MeshClipWithMesh",
	"MeshApplyPose",
	"MeshVariation",
	"MeshReshape",
	"MeshClipDeform",
	"MeshParameter",

	"Image",
	"ImageConstant",
	"ImageInterpolate",
	"ImageSaturate",
	"ImageTable",
	"ImageSwizzle",
	"ImageColorMap",
	"ImageGradient",
	"ImageBinarise",
	"ImageLuminance",
	"ImageLayer",
	"ImageLayerColour",
	"ImageResize",
	"ImagePlainColour",
	"ImageProject",
	"ImageMipmap",
	"ImageSwitch",
	"ImageConditional",
	"ImageFormat",
	"ImageParameter",
	"ImageMultiLayer",
	"ImageInvert",
	"ImageVariation",
	"ImageNormalComposite",
	"ImageTransform",
	"ImageFromMaterialParameter",

	"Bool",
	"BoolConstant",
	"BoolParameter",
	"BoolNot",
	"BoolAnd",

	"Color",
	"ColorConstant",
	"ColorParameter",
	"ColorSampleImage",
	"ColorTable",
	"ColorImageSize",
	"ColorFromScalars",
	"ColorArithmeticOperation",
	"ColorSwitch",
	"ColorVariation",
	"ColorToSRGB",

	"Scalar",
	"ScalarConstant",
	"ScalarParameter",
	"ScalarEnumParameter",
	"ScalarCurve",
	"ScalarSwitch",
	"ScalarArithmeticOperation",
	"ScalarVariation",
	"ScalarTable",

	"String",
	"StringConstant",
	"StringParameter",

	"Projector",
	"ProjectorConstant",
	"ProjectorParameter",

	"Range",
	"RangeFromScalar",

	"Layout",

	"PatchImage",
	"PatchMesh",

	"Surface",
	"SurfaceNew",
	"SurfaceSwitch",
	"SurfaceVariation",

	"LOD",

	"Component",
	"ComponentNew",
	"ComponentEdit",
	"ComponentSwitch",
	"ComponentVariation",

	"Object",
	"ObjectNew",
	"ObjectGroup",

	"Modifier",
	"ModifierMeshClipMorphPlane",
	"ModifierMeshClipWithMesh",
	"ModifierMeshClipDeform",
	"ModifierMeshClipWithUVMask",
	"ModifierSurfaceEdit",
	"ModifierTransformInMesh",
	"ModifierTransformWithBone",

	"ExtensionData",
	"ExtensionDataConstant",
	"ExtensionDataSwitch",
	"ExtensionDataVariation",

	"Matrix",
	"MatrixConstant",
	"MatrixParameter",

	"Material",
	"MaterialConstant",
	"MaterialTable",
	"MaterialSwitch",
	"MaterialVariation",
	"MaterialParameter",

	"ImageMaterialBreak",
	"ScalarMaterialBreak",
	"ColorMaterialBreak",
};

static_assert(UE_ARRAY_COUNT(MutableNodeNames) == SIZE_T(UE::Mutable::Private::Node::EType::Count));


class SMutableGraphTreeRow : public STableRow<TSharedPtr<FMutableGraphTreeElement>>
{
public:

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<FMutableGraphTreeElement>& InRowItem)
	{
		RowItem = InRowItem;

		FText MainLabel = FText::GetEmpty();
		if (RowItem->MutableNode)
		{
			UE::Mutable::Private::Node::EType MutableType = RowItem->MutableNode->GetType()->Type;
			
			FString NodeName = MutableNodeNames[int32(MutableType)];

			const FString LabelString = RowItem->Prefix.IsEmpty() 
				? FString::Printf( TEXT("%s"), *NodeName)
				: FString::Printf( TEXT("%s : %s"), *RowItem->Prefix, *NodeName);

			MainLabel = FText::FromString(LabelString);
			if (RowItem->DuplicatedOf)
			{
				MainLabel = FText::FromString( FString::Printf(TEXT("%s (Duplicated)"), *NodeName));
			}
		}
		else
		{
			MainLabel = FText::FromString( *RowItem->Prefix);
		}


		this->ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SMutableExpanderArrow, SharedThis(this))
			]

			+ SHorizontalBox::Slot()
			[
				SNew(STextBlock)
				.Text(MainLabel)
			]
		];

		STableRow< TSharedPtr<FMutableGraphTreeElement> >::ConstructInternal(
			STableRow::FArguments()
			//.Style(FAppStyle::Get(), "DetailsView.TreeView.TableRow")
			.ShowSelection(true)
			, InOwnerTableView
		);

	}


private:

	TSharedPtr<FMutableGraphTreeElement> RowItem;
};


void SMutableGraphViewer::AddReferencedObjects(FReferenceCollector& Collector)
{
	// Add UObjects here if we own any at some point
	//Collector.AddReferencedObject(CustomizableObject);
}


FString SMutableGraphViewer::GetReferencerName() const
{
	return TEXT("SMutableGraphViewer");
}


void SMutableGraphViewer::Construct(const FArguments& InArgs, const UE::Mutable::Private::NodePtr& InRootNode)
{
	ReferencedRuntimeTextures = InArgs._ReferencedRuntimeTextures;
	ReferencedCompileTextures = InArgs._ReferencedCompileTextures;
	RootNode = InRootNode;
	  
	ChildSlot
	[
		SNew(SSplitter)
		.Orientation(EOrientation::Orient_Horizontal)
		+ SSplitter::Slot()
		.Value(0.25f)
		[
			SNew(SBorder)
			.BorderImage(UE_MUTABLE_GET_BRUSH("ToolPanel.GroupBorder"))
			.Padding(FMargin(4.0f, 4.0f))
			[
				SAssignNew(TreeView, STreeView<TSharedPtr<FMutableGraphTreeElement>>)
				.TreeItemsSource(&RootNodes)
				.OnGenerateRow(this,&SMutableGraphViewer::GenerateRowForNodeTree)
				.OnGetChildren(this, &SMutableGraphViewer::GetChildrenForInfo)
				.OnSetExpansionRecursive(this, &SMutableGraphViewer::TreeExpandRecursive)
				.OnContextMenuOpening(this, &SMutableGraphViewer::OnTreeContextMenuOpening)
				.SelectionMode(ESelectionMode::Single)
				.HeaderRow
				(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(MutableGraphTreeViewColumns::Name)
					.FillWidth(25.f)
					.DefaultLabel(LOCTEXT("Node Name", "Node Name"))
				)
			]
		]
		+ SSplitter::Slot()
		.Value(0.75f)
		[
			SNew(SBorder)
			.BorderImage(UE_MUTABLE_GET_BRUSH("ToolPanel.GroupBorder"))
			.Padding(FMargin(4.0f, 4.0f))
		]
	];
	
	RebuildTree();
}


void SMutableGraphViewer::RebuildTree()
{
	RootNodes.Reset();
	ItemCache.Reset();
	MainItemPerNode.Reset();

	RootNodes.Add(MakeShareable(new FMutableGraphTreeElement(RootNode)));
	TreeView->RequestTreeRefresh();
	TreeExpandUnique();
}


TSharedRef<ITableRow> SMutableGraphViewer::GenerateRowForNodeTree(TSharedPtr<FMutableGraphTreeElement> InTreeNode, const TSharedRef<STableViewBase>& InOwnerTable)
{
	TSharedRef<SMutableGraphTreeRow> Row = SNew(SMutableGraphTreeRow, InOwnerTable, InTreeNode);
	return Row;
}

void SMutableGraphViewer::GetChildrenForInfo(TSharedPtr<FMutableGraphTreeElement> InInfo, TArray<TSharedPtr<FMutableGraphTreeElement>>& OutChildren)
{
// This is necessary because of problems with rtti information in other platforms. In any case, this part of the debugger is only useful in the standard editor.
#if PLATFORM_WINDOWS
	if (!InInfo->MutableNode)
	{
		return;
	}

	// If this is a duplicated of another row, don't provide its children.
	if (InInfo->DuplicatedOf)
	{
		return;
	}

	UE::Mutable::Private::Node* ParentNode = InInfo->MutableNode.get();
	uint32 InputIndex = 0;

	auto AddChildFunc = [this, ParentNode, &InputIndex, &OutChildren](UE::Mutable::Private::Node* ChildNode, const FString& Prefix)
	{
		if (ChildNode)
		{
			FItemCacheKey Key = { ParentNode, ChildNode, InputIndex };
			TSharedPtr<FMutableGraphTreeElement>* CachedItem = ItemCache.Find(Key);

			if (CachedItem)
			{
				OutChildren.Add(*CachedItem);
			}
			else
			{
				TSharedPtr<FMutableGraphTreeElement>* MainItemPtr = MainItemPerNode.Find(ChildNode);
				TSharedPtr<FMutableGraphTreeElement> Item = MakeShareable(new FMutableGraphTreeElement(ChildNode, MainItemPtr, Prefix));
				OutChildren.Add(Item);
				ItemCache.Add(Key, Item);

				if (!MainItemPtr)
				{
					MainItemPerNode.Add(ChildNode, Item);
				}
			}
		}
		else
		{
			// No mutable node has been provided so create a dummy tree element
			TSharedPtr<FMutableGraphTreeElement> Item = MakeShareable(new FMutableGraphTreeElement(nullptr, nullptr , Prefix));
			OutChildren.Add(Item);
		}
		++InputIndex;
	};

	if (ParentNode->GetType() == UE::Mutable::Private::NodeObjectNew::GetStaticType())
	{
		UE::Mutable::Private::NodeObjectNew* ObjectNew = StaticCast<UE::Mutable::Private::NodeObjectNew*>(ParentNode);
		for (int32 l = 0; l < ObjectNew->Components.Num(); ++l)
		{
			AddChildFunc(ObjectNew->Components[l].get(), TEXT("COMP") );
		}

		for (int32 Modifier = 0; Modifier < ObjectNew->Modifiers.Num(); Modifier++)
		{
			AddChildFunc(ObjectNew->Modifiers[Modifier].get(), FString::Printf(TEXT("MOD [%d]"), Modifier));
		}

		for (int32 l = 0; l < ObjectNew->Children.Num(); ++l)
		{
			AddChildFunc(ObjectNew->Children[l].get(), TEXT("CHILD"));
		}
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeObjectGroup::GetStaticType())
	{
		UE::Mutable::Private::NodeObjectGroup* ObjectGroup = StaticCast<UE::Mutable::Private::NodeObjectGroup*>(ParentNode);
		for (int32 l = 0; l < ObjectGroup->Children.Num(); ++l)
		{
			AddChildFunc(ObjectGroup->Children[l].get(), TEXT("CHILD"));
		}
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeSurfaceNew::GetStaticType())
	{
		UE::Mutable::Private::NodeSurfaceNew* SurfaceNew = StaticCast<UE::Mutable::Private::NodeSurfaceNew*>(ParentNode);
		AddChildFunc(SurfaceNew->Mesh.get(), TEXT("MESH"));

		for (int32 l = 0; l < SurfaceNew->Images.Num(); ++l)
		{
			AddChildFunc(SurfaceNew->Images[l].Image.get(), FString::Printf(TEXT("IMAGE [%s]"), *SurfaceNew->Images[l].Name));
		}

		for (int32 l = 0; l < SurfaceNew->Vectors.Num(); ++l)
		{
			AddChildFunc(SurfaceNew->Vectors[l].Vector.get(), FString::Printf(TEXT("VECTOR [%s]"), *SurfaceNew->Vectors[l].Name));
		}

		for (int32 l = 0; l < SurfaceNew->Scalars.Num(); ++l)
		{
			AddChildFunc(SurfaceNew->Scalars[l].Scalar.get(), FString::Printf(TEXT("SCALAR [%s]"), *SurfaceNew->Scalars[l].Name));
		}

		for (int32 l = 0; l < SurfaceNew->Strings.Num(); ++l)
		{
			AddChildFunc(SurfaceNew->Strings[l].String.get(), FString::Printf(TEXT("STRING [%s]"), *SurfaceNew->Strings[l].Name));
		}
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeModifierSurfaceEdit::GetStaticType())
	{
		UE::Mutable::Private::NodeModifierSurfaceEdit* SurfaceEdit = StaticCast<UE::Mutable::Private::NodeModifierSurfaceEdit*>(ParentNode);

		AddChildFunc(SurfaceEdit->MorphFactor.get(), FString::Printf(TEXT("MORPH_FACTOR [%s]"), *SurfaceEdit->MeshMorph));

		for (int32 LODIndex = 0; LODIndex < SurfaceEdit->LODs.Num(); ++LODIndex)
		{
			AddChildFunc(SurfaceEdit->LODs[LODIndex].MeshAdd.get(), FString::Printf(TEXT("LOD%d MESH_ADD"), LODIndex));
			AddChildFunc(SurfaceEdit->LODs[LODIndex].MeshRemove.get(), FString::Printf(TEXT("LOD%d MESH_REMOVE"), LODIndex));

			for (int32 l = 0; l < SurfaceEdit->LODs[LODIndex].Textures.Num(); ++l)
			{
				AddChildFunc(SurfaceEdit->LODs[LODIndex].Textures[l].Extend.get(), FString::Printf(TEXT("LOD%d EXTEND [%d]"), LODIndex, l));
				AddChildFunc(SurfaceEdit->LODs[LODIndex].Textures[l].PatchImage.get(), FString::Printf(TEXT("LOD%d PATCH IMAGE [%d]"), LODIndex, l));
				AddChildFunc(SurfaceEdit->LODs[LODIndex].Textures[l].PatchMask.get(), FString::Printf(TEXT("LOD%d PATCH MASK [%d]"), LODIndex, l));

			}
		}
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeSurfaceSwitch::GetStaticType())
	{
		UE::Mutable::Private::NodeSurfaceSwitch* SurfaceSwitch = StaticCast<UE::Mutable::Private::NodeSurfaceSwitch*>(ParentNode);
		AddChildFunc(SurfaceSwitch->Parameter.get(), TEXT("PARAM"));
		for (int32 l = 0; l < SurfaceSwitch->Options.Num(); ++l)
		{
			AddChildFunc(SurfaceSwitch->Options[l].get(), FString::Printf(TEXT("OPTION [%d]"), l));
		}
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeSurfaceVariation::GetStaticType())
	{
		UE::Mutable::Private::NodeSurfaceVariation* SurfaceVar = StaticCast<UE::Mutable::Private::NodeSurfaceVariation*>(ParentNode);
		for (int32 l = 0; l < SurfaceVar->DefaultSurfaces.Num(); ++l)
		{
			AddChildFunc(SurfaceVar->DefaultSurfaces[l].get(), FString::Printf(TEXT("DEF SURF [%d]"), l));
		}
		for (int32 l = 0; l < SurfaceVar->DefaultModifiers.Num(); ++l)
		{
			AddChildFunc(SurfaceVar->DefaultModifiers[l].get(), FString::Printf(TEXT("DEF MOD [%d]"), l));
		}

		for (int32 v = 0; v < SurfaceVar->Variations.Num(); ++v)
		{
			const UE::Mutable::Private::NodeSurfaceVariation::FVariation Var = SurfaceVar->Variations[v];
			for (int32 l = 0; l < Var.Surfaces.Num(); ++l)
			{
				AddChildFunc(Var.Surfaces[l].get(), FString::Printf(TEXT("VAR [%s] SURF [%d]"), *Var.Tag, l));
			}
			for (int32 l = 0; l < Var.Modifiers.Num(); ++l)
			{
				AddChildFunc(Var.Modifiers[l].get(), FString::Printf(TEXT("VAR [%s] MOD [%d]"), *Var.Tag, l));
			}
		}
	}
	
	else if (ParentNode->GetType() == UE::Mutable::Private::NodeLOD::GetStaticType())
	{
		UE::Mutable::Private::NodeLOD* LodVar = StaticCast<UE::Mutable::Private::NodeLOD*>(ParentNode);

		for (int32 SurfaceIndex = 0; SurfaceIndex < LodVar->Surfaces.Num(); SurfaceIndex++)
		{
			AddChildFunc(LodVar->Surfaces[SurfaceIndex].get(), FString::Printf(TEXT("SURFACE [%d]"), SurfaceIndex));
		}
	}
	
	else if (ParentNode->GetType() == UE::Mutable::Private::NodeComponentNew::GetStaticType())
	{
		UE::Mutable::Private::NodeComponentNew* ComponentVar = StaticCast<UE::Mutable::Private::NodeComponentNew*>(ParentNode);
		for (int32 LODIndex = 0; LODIndex < ComponentVar->LODs.Num(); LODIndex++)
		{
			AddChildFunc(ComponentVar->LODs[LODIndex].get(), FString::Printf(TEXT("LOD [%d]"), LODIndex));
		}

		AddChildFunc(ComponentVar->OverlayMaterial.get(), TEXT("OVERLAY MATERIAL"));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeComponentEdit::GetStaticType())
	{
		UE::Mutable::Private::NodeComponentEdit* ComponentVar = StaticCast<UE::Mutable::Private::NodeComponentEdit*>(ParentNode);
		for (int32 LODIndex = 0; LODIndex < ComponentVar->LODs.Num(); LODIndex++)
		{
			AddChildFunc(ComponentVar->LODs[LODIndex].get(), FString::Printf(TEXT("LOD [%d]"), LODIndex));
		}
		}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeComponentSwitch::GetStaticType())
	{
		UE::Mutable::Private::NodeComponentSwitch* ComponentSwitch = StaticCast<UE::Mutable::Private::NodeComponentSwitch*>(ParentNode);
		AddChildFunc(ComponentSwitch->Parameter.get(), TEXT("PARAM"));
		for (int32 OptionIndex = 0; OptionIndex < ComponentSwitch->Options.Num(); OptionIndex++)
		{
			AddChildFunc(ComponentSwitch->Options[OptionIndex].get(), FString::Printf(TEXT("OPTION [%d]"), OptionIndex));
		}
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeMeshConstant::GetStaticType())
	{
		UE::Mutable::Private::NodeMeshConstant* MeshConstantVar = StaticCast<UE::Mutable::Private::NodeMeshConstant*>(ParentNode);
		for (int32 LayoutIndex = 0; LayoutIndex < MeshConstantVar->Layouts.Num(); LayoutIndex++)
		{
			AddChildFunc(MeshConstantVar->Layouts[LayoutIndex].get(), FString::Printf(TEXT("LAYOUT [%d]"), LayoutIndex));
		}
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeImageFormat::GetStaticType())
	{
		UE::Mutable::Private::NodeImageFormat* ImageFormatVar = StaticCast<UE::Mutable::Private::NodeImageFormat*>(ParentNode);
		AddChildFunc(ImageFormatVar->Source.get(), FString::Printf(TEXT("SOURCE IMAGE")));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeMeshFormat::GetStaticType())
	{
		UE::Mutable::Private::NodeMeshFormat* MeshFormatVar = StaticCast<UE::Mutable::Private::NodeMeshFormat*>(ParentNode);
		AddChildFunc(MeshFormatVar->Source.get(), FString::Printf(TEXT("SOURCE MESH")));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeModifierMeshClipMorphPlane::GetStaticType())
	{
		// Nothing to show
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeModifierMeshClipWithMesh::GetStaticType())
	{
		UE::Mutable::Private::NodeModifierMeshClipWithMesh* ModifierMeshClipWithMeshVar = StaticCast<UE::Mutable::Private::NodeModifierMeshClipWithMesh*>(ParentNode);
		AddChildFunc(ModifierMeshClipWithMeshVar->ClipMesh.get(), FString::Printf(TEXT("CLIP MESH")));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeModifierMeshClipDeform::GetStaticType())
	{
		UE::Mutable::Private::NodeModifierMeshClipDeform* ModifierMeshClipDeformVar = StaticCast<UE::Mutable::Private::NodeModifierMeshClipDeform*>(ParentNode);
		AddChildFunc(ModifierMeshClipDeformVar->ClipMesh.get(), FString::Printf(TEXT("CLIP MESH")));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeModifierMeshClipWithUVMask::GetStaticType())
	{
		UE::Mutable::Private::NodeModifierMeshClipWithUVMask* ModifierMeshClipWithUVMaskVar = StaticCast<UE::Mutable::Private::NodeModifierMeshClipWithUVMask*>(ParentNode);
		AddChildFunc(ModifierMeshClipWithUVMaskVar->ClipMask.get(), FString::Printf(TEXT("CLIP MASK")));
		AddChildFunc(ModifierMeshClipWithUVMaskVar->ClipLayout.get(), FString::Printf(TEXT("CLIP LAYOUT")));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeModifierMeshTransformInMesh::GetStaticType())
	{
		UE::Mutable::Private::NodeModifierMeshTransformInMesh* ModifierMeshTransformInMeshVar = StaticCast<UE::Mutable::Private::NodeModifierMeshTransformInMesh*>(ParentNode);
		AddChildFunc(ModifierMeshTransformInMeshVar->BoundingMesh.get(), FString::Printf(TEXT("BOUNDING MESH")));
		AddChildFunc(ModifierMeshTransformInMeshVar->MatrixNode.get(), FString::Printf(TEXT("MESH TRANSFORM")));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeModifierMeshTransformWithBone::GetStaticType())
	{
		UE::Mutable::Private::NodeModifierMeshTransformWithBone* ModifierMeshTransformWithBoneVar = StaticCast<UE::Mutable::Private::NodeModifierMeshTransformWithBone*>(ParentNode);
		AddChildFunc(ModifierMeshTransformWithBoneVar->MatrixNode.get(), FString::Printf(TEXT("MESH TRANSFORM")));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeImageSwitch::GetStaticType())
	{
		UE::Mutable::Private::NodeImageSwitch* ImageSwitchVar = StaticCast<UE::Mutable::Private::NodeImageSwitch*>(ParentNode);
		AddChildFunc(ImageSwitchVar->Parameter.get(), FString::Printf(TEXT("PARAM")));
		for (int32 OptionIndex = 0; OptionIndex < ImageSwitchVar->Options.Num(); OptionIndex++)
		{
			AddChildFunc(ImageSwitchVar->Options[OptionIndex].get(), FString::Printf(TEXT("OPTION [%d]"), OptionIndex));
		}
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeImageMipmap::GetStaticType())
	{
		UE::Mutable::Private::NodeImageMipmap* ImageMipMapVar = StaticCast<UE::Mutable::Private::NodeImageMipmap*>(ParentNode);
		AddChildFunc(ImageMipMapVar->Source.get(), FString::Printf(TEXT("SOURCE")));
		AddChildFunc(ImageMipMapVar->Factor.get(), FString::Printf(TEXT("FACTOR")));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeImageLayer::GetStaticType())
	{
		UE::Mutable::Private::NodeImageLayer* ImageLayerVar = StaticCast<UE::Mutable::Private::NodeImageLayer*>(ParentNode);
		AddChildFunc(ImageLayerVar->Base.get(), FString::Printf(TEXT("BASE")));
		AddChildFunc(ImageLayerVar->Mask.get(), FString::Printf(TEXT("MASK")));
		AddChildFunc(ImageLayerVar->Blended.get(), FString::Printf(TEXT("BLEND")));
	}
	
	else if (ParentNode->GetType() == UE::Mutable::Private::NodeImageLayerColour::GetStaticType())
	{
		UE::Mutable::Private::NodeImageLayerColour* ImageLayerColourVar = StaticCast<UE::Mutable::Private::NodeImageLayerColour*>(ParentNode);
		AddChildFunc(ImageLayerColourVar->Base.get(), FString::Printf(TEXT("BASE")));
		AddChildFunc(ImageLayerColourVar->Mask.get(), FString::Printf(TEXT("MASK")));
		AddChildFunc(ImageLayerColourVar->Colour.get(), FString::Printf(TEXT("COLOR")));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeImageResize::GetStaticType())
	{
		UE::Mutable::Private::NodeImageResize* ImageResizeVar = StaticCast<UE::Mutable::Private::NodeImageResize*>(ParentNode);
		AddChildFunc(ImageResizeVar->Base.get(), FString::Printf(TEXT("BASE")));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeMeshMorph::GetStaticType())
	{
		UE::Mutable::Private::NodeMeshMorph* MeshMorphVar = StaticCast<UE::Mutable::Private::NodeMeshMorph*>(ParentNode);
		AddChildFunc(MeshMorphVar->Base.get(), FString::Printf(TEXT("BASE")));
		AddChildFunc(MeshMorphVar->Morph.get(), FString::Printf(TEXT("MORPH")));
		AddChildFunc(MeshMorphVar->Factor.get(), FString::Printf(TEXT("FACTOR")));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeImageProject::GetStaticType())
	{
		UE::Mutable::Private::NodeImageProject* ImageProjectVar = StaticCast<UE::Mutable::Private::NodeImageProject*>(ParentNode);
		AddChildFunc(ImageProjectVar->Projector.get(), FString::Printf(TEXT("PROJECTOR")));
		AddChildFunc(ImageProjectVar->Mesh.get(), FString::Printf(TEXT("MESH")));
		AddChildFunc(ImageProjectVar->Image.get(), FString::Printf(TEXT("IMAGE")));
		AddChildFunc(ImageProjectVar->Mask.get(), FString::Printf(TEXT("MASK")));
		AddChildFunc(ImageProjectVar->AngleFadeStart.get(), FString::Printf(TEXT("FADE START ANGLE")));
		AddChildFunc(ImageProjectVar->AngleFadeEnd.get(), FString::Printf(TEXT("FADE END ANGLE")));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeImagePlainColour::GetStaticType())
	{
		UE::Mutable::Private::NodeImagePlainColour* ImagePlainColourVar = StaticCast<UE::Mutable::Private::NodeImagePlainColour*>(ParentNode);
		AddChildFunc(ImagePlainColourVar->Colour.get(), FString::Printf(TEXT("COLOR")));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeLayout::GetStaticType())
	{
		// Nothing to show
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeScalarEnumParameter::GetStaticType())
	{
		UE::Mutable::Private::NodeScalarEnumParameter* ScalarEnumParameterVar = StaticCast<UE::Mutable::Private::NodeScalarEnumParameter*>(ParentNode);
		for (int32 RangeIndex = 0; RangeIndex < ScalarEnumParameterVar->Ranges.Num(); RangeIndex++)
		{
			AddChildFunc(ScalarEnumParameterVar->Ranges[RangeIndex].get(), FString::Printf(TEXT("RANGE [%d]"), RangeIndex));
		}
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeMeshFragment::GetStaticType())
	{
		UE::Mutable::Private::NodeMeshFragment* MeshFragmentVar = StaticCast<UE::Mutable::Private::NodeMeshFragment*>(ParentNode);
		AddChildFunc(MeshFragmentVar->SourceMesh.get(), FString::Printf(TEXT("MESH")));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeColourSampleImage::GetStaticType())
	{
		UE::Mutable::Private::NodeColourSampleImage* ColorSampleImageVar = StaticCast<UE::Mutable::Private::NodeColourSampleImage*>(ParentNode);
		AddChildFunc(ColorSampleImageVar->Image.get(), FString::Printf(TEXT("IMAGE")));
		AddChildFunc(ColorSampleImageVar->X.get(), FString::Printf(TEXT("X")));
		AddChildFunc(ColorSampleImageVar->Y.get(), FString::Printf(TEXT("Y")));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeImageInterpolate::GetStaticType())
	{
		UE::Mutable::Private::NodeImageInterpolate* ImageInterpolateVar = StaticCast<UE::Mutable::Private::NodeImageInterpolate*>(ParentNode);
		AddChildFunc(ImageInterpolateVar->Factor.get(), FString::Printf(TEXT("FACTOR")));
		for (int32 TargetIndex = 0; TargetIndex < ImageInterpolateVar->Targets.Num(); TargetIndex++)
		{
			AddChildFunc(ImageInterpolateVar->Targets[TargetIndex].get(), FString::Printf(TEXT("TARGET [%d]"), TargetIndex));
		}
	}
	
	else if (ParentNode->GetType() == UE::Mutable::Private::NodeScalarConstant::GetStaticType())
	{
		// Nothing to show
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeScalarParameter::GetStaticType())
	{
		UE::Mutable::Private::NodeScalarParameter* ScalarParameterVar = StaticCast<UE::Mutable::Private::NodeScalarParameter*>(ParentNode);
		for (int32 RangeIndex = 0; RangeIndex < ScalarParameterVar->Ranges.Num(); RangeIndex++)
		{
			AddChildFunc(ScalarParameterVar->Ranges[RangeIndex].get(), FString::Printf(TEXT("RANGE [%d]"), RangeIndex));
		}
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeColourParameter::GetStaticType())
	{
		UE::Mutable::Private::NodeColourParameter* ColorParameterVar = StaticCast<UE::Mutable::Private::NodeColourParameter*>(ParentNode);
		for (int32 RangeIndex = 0; RangeIndex < ColorParameterVar->Ranges.Num(); RangeIndex++)
		{
			AddChildFunc(ColorParameterVar->Ranges[RangeIndex].get(), FString::Printf(TEXT("RANGE [%d]"), RangeIndex));
		}
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeColourConstant::GetStaticType())
	{
		// Nothing to show
	}


	else if (ParentNode->GetType() == UE::Mutable::Private::NodeImageConstant::GetStaticType())
	{
		// Nothing to show
	}

	
	else if (ParentNode->GetType() == UE::Mutable::Private::NodeScalarCurve::GetStaticType())
	{
		UE::Mutable::Private::NodeScalarCurve* ScalarCurveVar = StaticCast<UE::Mutable::Private::NodeScalarCurve*>(ParentNode);
		AddChildFunc(ScalarCurveVar->CurveSampleValue.get(), FString::Printf(TEXT("INPUT")));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeMeshMakeMorph::GetStaticType())
	{
		UE::Mutable::Private::NodeMeshMakeMorph* MeshMakeMorphVar = StaticCast<UE::Mutable::Private::NodeMeshMakeMorph*>(ParentNode);
		AddChildFunc(MeshMakeMorphVar->Base.get(), FString::Printf(TEXT("BASE")));
		AddChildFunc(MeshMakeMorphVar->Target.get(), FString::Printf(TEXT("TARGET")));
	}
	
	else if (ParentNode->GetType() == UE::Mutable::Private::NodeProjectorParameter::GetStaticType())
	{
		UE::Mutable::Private::NodeProjectorParameter* ProjectorParameterVar = StaticCast<UE::Mutable::Private::NodeProjectorParameter*>(ParentNode);
		for (int32 RangeIndex = 0; RangeIndex < ProjectorParameterVar->Ranges.Num(); RangeIndex++)
		{
			AddChildFunc(ProjectorParameterVar->Ranges[RangeIndex].get(), FString::Printf(TEXT("RANGE [%d]"), RangeIndex));
		}
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeProjectorConstant::GetStaticType())
	{
		// Nothing to show
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeColourSwitch::GetStaticType())
	{
		UE::Mutable::Private::NodeColourSwitch* ColorSwitchVar = StaticCast<UE::Mutable::Private::NodeColourSwitch*>(ParentNode);
		AddChildFunc(ColorSwitchVar->Parameter.get(), TEXT("PARAM"));
		for (int32 OptionIndex = 0; OptionIndex < ColorSwitchVar->Options.Num(); ++OptionIndex)
		{
			AddChildFunc(ColorSwitchVar->Options[OptionIndex].get(), FString::Printf(TEXT("OPTION [%d]"), OptionIndex));
		}
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeImageSwizzle::GetStaticType())
	{
		UE::Mutable::Private::NodeImageSwizzle* ImageSwizzleVar = StaticCast<UE::Mutable::Private::NodeImageSwizzle*>(ParentNode);
		for (int32 SourceIndex = 0; SourceIndex < ImageSwizzleVar->Sources.Num(); ++SourceIndex)
		{
			AddChildFunc(ImageSwizzleVar->Sources[SourceIndex].get(), FString::Printf(TEXT("SOURCE [%d]"), SourceIndex));
		}
	}
	
	else if (ParentNode->GetType() == UE::Mutable::Private::NodeImageInvert::GetStaticType())
	{
		UE::Mutable::Private::NodeImageInvert* ImageInvertVar = StaticCast<UE::Mutable::Private::NodeImageInvert*>(ParentNode);
		AddChildFunc(ImageInvertVar->Base.get(), TEXT("BASE"));
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeImageMultiLayer::GetStaticType())
	{
		UE::Mutable::Private::NodeImageMultiLayer* ImageMultilayerVar = StaticCast<UE::Mutable::Private::NodeImageMultiLayer*>(ParentNode);
		AddChildFunc(ImageMultilayerVar->Base.get(), FString::Printf(TEXT("BASE")));
		AddChildFunc(ImageMultilayerVar->Mask.get(), FString::Printf(TEXT("MASK")));
		AddChildFunc(ImageMultilayerVar->Blended.get(), FString::Printf(TEXT("BLEND")));
		AddChildFunc(ImageMultilayerVar->Range.get(), FString::Printf(TEXT("RANGE")));
	}
	
	else if (ParentNode->GetType() == UE::Mutable::Private::NodeImageTable::GetStaticType())
	{
		// No nodes to show
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeMeshTable::GetStaticType())
	{
		UE::Mutable::Private::NodeMeshTable* MeshTableVar = StaticCast<UE::Mutable::Private::NodeMeshTable*>(ParentNode);
		for (int32 LayoutIndex = 0; LayoutIndex < MeshTableVar->Layouts.Num(); ++LayoutIndex)
		{
			AddChildFunc(MeshTableVar->Layouts[LayoutIndex].get(), FString::Printf(TEXT("LAYOUT [%d]"), LayoutIndex));
		}
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeScalarTable::GetStaticType())
	{
		// Nothing to show
	}

	else if (ParentNode->GetType() == UE::Mutable::Private::NodeScalarSwitch::GetStaticType())
	{
		UE::Mutable::Private::NodeScalarSwitch* ScalarSwitchVar = StaticCast<UE::Mutable::Private::NodeScalarSwitch*>(ParentNode);
		AddChildFunc(ScalarSwitchVar->Parameter.get(), TEXT("PARAM"));
		for (int32 OptionIndex = 0; OptionIndex < ScalarSwitchVar->Options.Num(); ++OptionIndex)
		{
			AddChildFunc(ScalarSwitchVar->Options[OptionIndex].get(), FString::Printf(TEXT("OPTION [%d]"), OptionIndex));
		}
	}
	
	else if (ParentNode->GetType() == UE::Mutable::Private::NodeColourFromScalars::GetStaticType())
	{
		UE::Mutable::Private::NodeColourFromScalars* ScalarTableVar = StaticCast<UE::Mutable::Private::NodeColourFromScalars*>(ParentNode);
		AddChildFunc(ScalarTableVar->X.get(), TEXT("X"));
		AddChildFunc(ScalarTableVar->Y.get(), TEXT("Y"));
		AddChildFunc(ScalarTableVar->Z.get(), TEXT("Z"));
		AddChildFunc(ScalarTableVar->W.get(), TEXT("W"));
	}
	
	else
	{
		UE_LOG(LogMutable,Error,TEXT("The node of type %d has not been implemented, so its children won't be added to the tree."), int32(ParentNode->GetType()->Type));

		// Add a placeholder to the tree
		const FString Prefix =  FString::Printf(TEXT("[%d] NODE TYPE NOT IMPLEMENTED"), int32(ParentNode->GetType()->Type));
		AddChildFunc(nullptr, Prefix);
	}
#endif
}


TSharedPtr<SWidget> SMutableGraphViewer::OnTreeContextMenuOpening()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("Graph_Expand_Instance", "Expand Instance-Level Operations"),
		LOCTEXT("Graph_Expand_Instance_Tooltip", "Expands all the operations in the tree that are instance operations (not images, meshes, booleans, etc.)."),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &SMutableGraphViewer::TreeExpandUnique)
			//, FCanExecuteAction::CreateSP(this, &SMutableCodeViewer::HasAnyItemInPalette)
		)
	);

	return MenuBuilder.MakeWidget();
}


void SMutableGraphViewer::TreeExpandRecursive(TSharedPtr<FMutableGraphTreeElement> InInfo, bool bExpand)
{
	if (bExpand)
	{
		TreeExpandUnique();
	}
}


void SMutableGraphViewer::TreeExpandUnique()
{
	TArray<TSharedPtr<FMutableGraphTreeElement>> Pending = RootNodes;

	TSet<TSharedPtr<FMutableGraphTreeElement>> Processed;

	TArray<TSharedPtr<FMutableGraphTreeElement>> Children;

	while (!Pending.IsEmpty())
	{
		TSharedPtr<FMutableGraphTreeElement> Item = Pending.Pop();
		TreeView->SetItemExpansion(Item, true);

		Children.SetNum(0);
		GetChildrenForInfo(Item, Children);
		Pending.Append(Children);
	}
}


#undef LOCTEXT_NAMESPACE 


