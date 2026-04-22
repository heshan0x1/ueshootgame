// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuR/Operations.h"
#include "MuR/ParametersPrivate.h"
#include "MuR/Platform.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuR/System.h"
#include "MuR/Material.h"
#include "MuT/AST.h"
#include "MuT/ASTOpConditional.h"
#include "MuT/ASTOpConstantResource.h"
#include "MuT/ASTOpImagePixelFormat.h"
#include "MuT/ASTOpImageResize.h"
#include "MuT/ASTOpMaterialBreak.h"
#include "MuT/ASTOpSwitch.h"
#include "MuT/CodeGenerator.h"
#include "MuT/CodeGenerator_FirstPass.h"
#include "MuT/CompilerPrivate.h"
#include "MuT/ErrorLog.h"
#include "MuT/NodeScalar.h"
#include "MuT/NodeMaterial.h"
#include "MuT/NodeMaterialConstant.h"
#include "MuT/NodeMaterialSwitch.h"
#include "MuT/NodeMaterialVariation.h"
#include "MuT/NodeMaterialTable.h"
#include "MuT/NodeMaterialParameter.h"
#include "MuT/Table.h"
#include "MuT/TablePrivate.h"


namespace UE::Mutable::Private
{
	class Node;

	void CodeGenerator::GenerateMaterial(const FMaterialGenerationOptions& InOptions, FMaterialGenerationResult& Result , const Ptr<const NodeMaterial>& InUntypedNode)
	{
		// Early out
		if (!InUntypedNode)
		{
			Result = FMaterialGenerationResult();
			return;
		}

		// See if it was already generated
		FGeneratedCacheKey Key;
		Key.Node = InUntypedNode;
		Key.Options = InOptions;
		{
			UE::TUniqueLock Lock(GeneratedMaterials.Mutex);
			FGeneratedMaterialsMap::ValueType* Found = GeneratedMaterials.Map.Find(Key);
			if (Found)
			{
				Result = *Found;
				return;
			}
		}

		// Generate for each different type of node
		const NodeMaterial* Node = InUntypedNode.get();
		switch (Node->GetType()->Type)
		{
		case Node::EType::MaterialConstant:
			GenerateMaterial_Constant(InOptions, Result, static_cast<const NodeMaterialConstant*>(Node)); 
			break;
		
		case Node::EType::MaterialSwitch:
			GenerateMaterial_Switch(InOptions, Result, static_cast<const NodeMaterialSwitch*>(Node));
			break;

		case Node::EType::MaterialVariation:
			GenerateMaterial_Variation(InOptions, Result, static_cast<const NodeMaterialVariation*>(Node));
			break;

		case Node::EType::MaterialTable:
			GenerateMaterial_Table(InOptions, Result, static_cast<const NodeMaterialTable*>(Node));
			break;

		case Node::EType::MaterialParameter:
			GenerateMaterial_Parameter(InOptions, Result, static_cast<const NodeMaterialParameter*>(Node));
			break;

		default: 
			check(false);
		}
	}


	void CodeGenerator::GenerateMaterial_Constant(const FMaterialGenerationOptions& Options, FMaterialGenerationResult& Result, const NodeMaterialConstant* Node)
	{
		// Constant Material Resource
		TSharedPtr<FMaterial> MaterialResource = MakeShared<FMaterial>();
		MaterialResource->ReferenceID = Node->MaterialId;
		MaterialResource->ColorParameters = Node->ColorValues;
		MaterialResource->ScalarParameters = Node->ScalarValues;

		// Constant Material Operation
		Ptr<ASTOpConstantResource> ConstantMaterial = new ASTOpConstantResource();
		ConstantMaterial->Type = EOpType::MI_CONSTANT;

		//TODO: each image should have its own generation options
		FImageGenerationOptions ImageOptions(Options.ComponentId, Options.LODIndex);
		ImageOptions.ImageLayoutStrategy = Options.ImageLayoutStrategy;
		ImageOptions.RectSize = Options.RectSize;
		ImageOptions.LayoutBlockId = Options.LayoutBlockId;
		ImageOptions.LayoutToApply = Options.LayoutToApply;

		const TArray<TPair<FName, Ptr<NodeImage>>>& ImageValuesArray = Node->ImageValues.Array();

		//Store the images into the operation so that we can link them later
		for (int32 ImageIndex = 0; ImageIndex < ImageValuesArray.Num(); ++ImageIndex)
		{
			FName ImageParameterName = ImageValuesArray[ImageIndex].Key;
			Ptr<NodeImage> ImageNode = ImageValuesArray[ImageIndex].Value;
			FImageGenerationResult ImageResult;

			GenerateImage(ImageOptions, ImageResult, ImageNode);

			// Store lazy branches
			ConstantMaterial->ImageOperations.Add(ImageParameterName, ASTChild(ConstantMaterial, ImageResult.op));
		}

		// Store the constant material
		ConstantMaterial->SetValue(MaterialResource, CompilerOptions->OptimisationOptions.DiskCacheContext);

		Result.op = ConstantMaterial;
	}


	void CodeGenerator::GenerateMaterial_Switch(const FMaterialGenerationOptions& Options, FMaterialGenerationResult& Result, const NodeMaterialSwitch* Node)
	{
		if (Node->Options.Num() == 0)
		{
			return;
		}

		Ptr<ASTOp> Variable;
		if (Node->Parameter)
		{
			FScalarGenerationResult ParamResult;
			GenerateScalar(ParamResult, Options, Node->Parameter.get());
			Variable = ParamResult.op;
		}
		else
		{
			// This argument is required
			Variable = GenerateMissingScalarCode(TEXT("Switch variable"), 0.0f, Node->GetMessageContext());
		}

		Ptr<ASTOpSwitch> SwitchOp = new ASTOpSwitch();
		SwitchOp->Type = EOpType::MI_SWITCH;
		SwitchOp->Variable = Variable;

		// Options
		SwitchOp->Cases.Reserve(Node->Options.Num());

		for (int32 OptionIndex = 0; OptionIndex < Node->Options.Num(); ++OptionIndex)
		{
			FMaterialGenerationResult OptionResult;
			const Ptr<NodeMaterial>& OptionMaterial = Node->Options[OptionIndex];
			if (OptionMaterial)
			{
				GenerateMaterial(Options, OptionResult, OptionMaterial);
			}
			else
			{
				// This argument is required. Empty material
				OptionResult.op = GenerateMissingMaterialCode(TEXT("Material Switch"), Node->GetMessageContext());
			}

			SwitchOp->Cases.Emplace(int16(OptionIndex), SwitchOp, OptionResult.op);
		}

		Result.op = SwitchOp;
	}


	void CodeGenerator::GenerateMaterial_Variation(const FMaterialGenerationOptions& Options, FMaterialGenerationResult& Result, const NodeMaterialVariation* Node)
	{
		Ptr<ASTOp> LastOp;

		// Default case
		if (Node->DefaultMaterial)
		{
			FMaterialGenerationResult DefaultResult;
			GenerateMaterial(Options, DefaultResult, Node->DefaultMaterial);
			LastOp = DefaultResult.op;
		}

		// Process variations in reverse order, since conditionals are built bottom-up.
		for (int32 VariationIndex = int32(Node->Variations.Num()) - 1; VariationIndex >= 0; --VariationIndex)
		{
			int32 TagIndex = INDEX_NONE;
			const FString& tag = Node->Variations[VariationIndex].Tag;
			for (int32 Index = 0; Index < FirstPass.Tags.Num(); ++Index)
			{
				if (FirstPass.Tags[Index].Tag == tag)
				{
					TagIndex = Index;
				}
			}

			if (TagIndex == INDEX_NONE)
			{
				FString Msg = FString::Printf(TEXT("Unknown tag found in material variation [%s]."), *tag);

				ErrorLog->Add(Msg, ELMT_WARNING, Node->GetMessageContext());
				continue;
			}

			Ptr<ASTOp> VariationOp;
			if (Node->Variations[VariationIndex].Material)
			{
				FMaterialGenerationResult VariationResult;
				GenerateMaterial(Options, VariationResult, Node->Variations[VariationIndex].Material);
				VariationOp = VariationResult.op;
			}
			else
			{
				// This argument is required
				VariationOp = GenerateMissingMaterialCode(TEXT("Material Variation"), Node->GetMessageContext());
			}

			Ptr<ASTOpConditional> conditional = new ASTOpConditional;
			conditional->type = EOpType::MI_CONDITIONAL;
			conditional->no = LastOp;
			conditional->yes = VariationOp;
			conditional->condition = FirstPass.Tags[TagIndex].GenericCondition;

			LastOp = conditional;
		}

		Result.op = LastOp;
	}


	void CodeGenerator::GenerateMaterial_Table(const FMaterialGenerationOptions& Options, FMaterialGenerationResult& Result, const NodeMaterialTable* Node)
	{
		Result.op = GenerateTableSwitch<NodeMaterialTable, ETableColumnType::Material, EOpType::MI_SWITCH>(*Node,
			[this](const NodeMaterialTable& Node, int32 ColumnIndex, int32 RowIndex, FErrorLog* pErrorLog)
			{
				Ptr<ASTOp> Op;

				int32 MaterialId = Node.Table->GetPrivate()->Rows[RowIndex].Values[ColumnIndex].Int;
				if (MaterialId != INDEX_NONE)
				{
					// TODO(Max): UE-314401
					// Constant Material Resource
					TSharedPtr<FMaterial> MaterialResource = MakeShared<FMaterial>();
					MaterialResource->ReferenceID = MaterialId;

					// Constant Material Operation
					Ptr<ASTOpConstantResource> ConstantMaterial = new ASTOpConstantResource();
					ConstantMaterial->Type = EOpType::MI_CONSTANT;

					// Store the constant material
					ConstantMaterial->SetValue(MaterialResource, CompilerOptions->OptimisationOptions.DiskCacheContext);

					Op = ConstantMaterial;
				}

				return Op;
			});
	}


	void CodeGenerator::GenerateMaterial_Parameter(const FMaterialGenerationOptions& InOptions, FMaterialGenerationResult& Result, const NodeMaterialParameter* InNode)
	{
		{
			UE::TUniqueLock Lock(FirstPass.ParameterNodes.Mutex);
			if (Ptr<ASTOpParameter>* Found = FirstPass.ParameterNodes.GenericParametersCache.Find(InNode))
			{
				Result.op = *Found;
				return;
			}
		}
			
		Ptr<ASTOpParameter> MaterialOp = new ASTOpParameter();
		MaterialOp->Type = EOpType::MI_PARAMETER;

		MaterialOp->Parameter.Name = InNode->Name;
		bool bParseOk = FGuid::Parse(InNode->UID, MaterialOp->Parameter.UID);
		check(bParseOk);
		MaterialOp->Parameter.Type = EParameterType::Material;
		MaterialOp->Parameter.DefaultValue.Set<FParamMaterialType>(nullptr);

		// Generate the code for the ranges
		for (const Ptr<NodeRange>& RangeNode : InNode->Ranges)
		{
			FRangeGenerationResult RangeResult;
			GenerateRange(RangeResult, InOptions, RangeNode);
			MaterialOp->Ranges.Emplace(MaterialOp.get(), RangeResult.sizeOp, RangeResult.rangeName, RangeResult.rangeUID);
		}
		
		// Copy Scalar and Color Names
		MaterialOp->ColorParameterNames = InNode->ColorParameterNames;
		MaterialOp->ScalarParameterNames = InNode->ScalarParameterNames;

		// Generate Image Nodes
		FImageGenerationResult ImageResult;
		FImageGenerationOptions ImageOptions(InOptions.ComponentId, InOptions.LODIndex);
		ImageOptions.ImageLayoutStrategy = InOptions.ImageLayoutStrategy;
		ImageOptions.RectSize = InOptions.RectSize;
		ImageOptions.LayoutBlockId = InOptions.LayoutBlockId;
		ImageOptions.LayoutToApply = InOptions.LayoutToApply;
		ImageOptions.MaterialParameter = MaterialOp->Parameter;

		for (const TPair<FString, Ptr<NodeImage>>& ImageNode : InNode->ImageParameterNodes)
		{
			GenerateImage(ImageOptions, ImageResult, ImageNode.Value);

			// Store lazy branches
			MaterialOp->ImageOperations.Add(ImageNode.Key, ASTChild(MaterialOp, ImageResult.op));
		}

		Result.op = MaterialOp;

		FirstPass.ParameterNodes.GenericParametersCache.Add(InNode, MaterialOp);
	}


	Ptr<ASTOp> CodeGenerator::GenerateMissingMaterialCode(const TCHAR* strWhere, const void* ErrorContext)
	{
		// Log an error message
		FString Msg = FString::Printf(TEXT("Required connection not found: %s"), strWhere);
		ErrorLog->Add(Msg, ELMT_ERROR, ErrorContext);

		// Constant Material Operation
		Ptr<ASTOpConstantResource> ConstantMaterial = new ASTOpConstantResource();
		ConstantMaterial->Type = EOpType::MI_CONSTANT;

		// Make a checkered debug image
		TSharedPtr<FMaterial> GenerateMaterial = MakeShared<FMaterial>();
		GenerateMaterial->ReferenceID = INDEX_NONE;

		// Store the constant material
		ConstantMaterial->SetValue(GenerateMaterial, CompilerOptions->OptimisationOptions.DiskCacheContext);

		return ConstantMaterial;
	}
}
