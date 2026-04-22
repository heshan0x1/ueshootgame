// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/GenerateMutableSource/GenerateMutableSourceMaterial.h"

#include "Engine/TextureLODSettings.h"
#include "Interfaces/ITargetPlatform.h"
#include "MuCOE/CustomizableObjectCompiler.h"
#include "MuCOE/CustomizableObjectGraph.h"
#include "MuCOE/EdGraphSchema_CustomizableObject.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSourceFloat.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSourceImage.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSourceMacro.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSourceTable.h"
#include "MuCOE/GraphTraversal.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMaterial.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMaterialParameter.h"
#include "MuCOE/Nodes/CustomizableObjectNodeMacroInstance.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTable.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTunnel.h"
#include "MuCOE/Nodes/CONodeMaterialBreak.h"
#include "MuCOE/Nodes/CONodeMaterialConstant.h"
#include "MuCOE/Nodes/CONodeMaterialSwitch.h"
#include "MuCOE/Nodes/CONodeMaterialVariation.h"
#include "MuCO/LoadUtils.h"
#include "MuT/NodeImageFormat.h"
#include "MuT/NodeImageFromMaterialParameter.h"
#include "MuT/NodeImageResize.h"
#include "MuT/NodeMaterialConstant.h"
#include "MuT/NodeMaterialSwitch.h"
#include "MuT/NodeMaterialTable.h"
#include "MuT/NodeMaterialVariation.h"
#include "MuT/NodeMaterialParameter.h"

#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"


#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeMaterial> GenerateMutableSourceMaterial(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext)
{
	check(Pin)
	RETURN_ON_CYCLE(*Pin, GenerationContext)

	CheckNumOutputs(*Pin, GenerationContext);
	
	const UEdGraphSchema_CustomizableObject* Schema = GetDefault<UEdGraphSchema_CustomizableObject>();

	UCustomizableObjectNode* Node = CastChecked<UCustomizableObjectNode>(Pin->GetOwningNode());

	const FGeneratedKey Key(reinterpret_cast<void*>(&GenerateMutableSourceMaterial), *Pin, *Node, GenerationContext);
	if (const FGeneratedData* Generated = GenerationContext.Generated.Find(Key))
	{
		return static_cast<UE::Mutable::Private::NodeMaterial*>(Generated->Node.get());
	}

	if (Node->IsNodeOutDatedAndNeedsRefresh())
	{
		Node->SetRefreshNodeWarning();
	}

	// Bool that determines if a node can be added to the cache of nodes.
	// Most nodes need to be added to the cache but there are some that don't. For exampel, MacroInstanceNodes
	bool bCacheNode = true;

	UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeMaterial> Result;
	
	if (const UCONodeMaterialConstant* TypedNodeMaterialConstant = Cast<UCONodeMaterialConstant>(Node))
	{
		UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeMaterialConstant> MaterialNode;
		bCacheNode = false; //custom node cache

		if (UMaterialInterface* Material = GenerationContext.LoadObject(TypedNodeMaterialConstant->Material))
		{
			if (GenerationContext.MaterialConstantNodesCache.Contains(TypedNodeMaterialConstant))
			{
				MaterialNode = GenerationContext.MaterialConstantNodesCache[TypedNodeMaterialConstant];
			}
			else
			{
				MaterialNode = new UE::Mutable::Private::NodeMaterialConstant();

				GenerationContext.CurrentReferencedMaterialIndex = GenerationContext.ReferencedMaterials.AddUnique(Material);
				MaterialNode->MaterialId = GenerationContext.CurrentReferencedMaterialIndex;

				GenerationContext.MaterialConstantNodesCache.Add(TypedNodeMaterialConstant, MaterialNode);
			}

			// Store the parameter that needs to be processed
			if (GenerationContext.CurrentMaterialBreakParameter.ParameterType != EMaterialParameterType::None)
			{
				if (!TypedNodeMaterialConstant->Material)
				{
					GenerationContext.Log(LOCTEXT("NoRefernceMaterialToBreak", "Could not find a Reference Material to break."), TypedNodeMaterialConstant);
				}
				else
				{
					switch (GenerationContext.CurrentMaterialBreakParameter.ParameterType)
					{
					case EMaterialParameterType::Texture:
					{
						UTexture* Texture = nullptr;
						bool bParameterFound = Material->GetTextureParameterValue(GenerationContext.CurrentMaterialBreakParameter.ParameterName, Texture);
						UTexture2D* BaseTexture = Cast<UTexture2D>(Texture);

						if (bParameterFound && BaseTexture)
						{
							TSharedPtr<UE::Mutable::Private::FImage> ImageConstant = GenerateImageConstant(BaseTexture, GenerationContext, false);
							const uint32 MipsToSkip = ComputeLODBiasForTexture(GenerationContext, *BaseTexture);

							UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeImageConstant> ConstantImageNode = new UE::Mutable::Private::NodeImageConstant;
							ConstantImageNode->SetValue(ImageConstant);

							const UTextureLODSettings& TextureLODSettings = GenerationContext.CompilationContext->Options.TargetPlatform->GetTextureLODSettings();
							const FTextureLODGroup& LODGroupInfo = TextureLODSettings.GetTextureLODGroup(BaseTexture->LODGroup);
							const FCompilationOptions& CompilationOptions = GenerationContext.CompilationContext->Options;

							ConstantImageNode->SourceDataDescriptor.OptionalMaxLODSize = LODGroupInfo.OptionalMaxLODSize;
							ConstantImageNode->SourceDataDescriptor.OptionalLODBias = LODGroupInfo.OptionalLODBias;
							ConstantImageNode->SourceDataDescriptor.NumNonOptionalLODs = CompilationOptions.MinDiskMips;

							const FString TextureName = GetNameSafe(BaseTexture).ToLower();
							ConstantImageNode->SourceDataDescriptor.SourceId = CityHash32(reinterpret_cast<const char*>(*TextureName), TextureName.Len() * sizeof(FString::ElementType));

							UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeImage> FinalNodeImag = ResizeTextureByNumMips(ConstantImageNode, MipsToSkip);

							if (FinalNodeImag)
							{
								MaterialNode->ImageValues.Add(GenerationContext.CurrentMaterialBreakParameter.ParameterName, FinalNodeImag);
							}
						}

						break;
					}
					case EMaterialParameterType::Vector:
					{
						FLinearColor Color;
						if (Material->GetVectorParameterValue(GenerationContext.CurrentMaterialBreakParameter.ParameterName, Color))
						{
							MaterialNode->ColorValues.Add(GenerationContext.CurrentMaterialBreakParameter.ParameterName, Color);
						}

						break;
					}
					case EMaterialParameterType::Scalar:
					{
						float Scalar;
						if (Material->GetScalarParameterValue(GenerationContext.CurrentMaterialBreakParameter.ParameterName, Scalar))
						{
							MaterialNode->ScalarValues.Add(GenerationContext.CurrentMaterialBreakParameter.ParameterName, Scalar);
						}

						break;
					}
					default:
						break;
					}
				}
			}
		}

		Result = MaterialNode;
	}

	else if (const UCONodeMaterialSwitch* TypedNodeMaterialSwitch = Cast<UCONodeMaterialSwitch>(Node))
	{
		Result = [&]()
		{
			const UEdGraphPin* SwitchParameter = TypedNodeMaterialSwitch->SwitchParameter();

			// Check Switch Parameter arity preconditions.
			if (const UEdGraphPin* ConnectedPin = FollowInputPin(*SwitchParameter))
			{
				UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeScalar> SwitchParam = GenerateMutableSourceFloat(ConnectedPin, GenerationContext);

				// Switch Param not generated
				if (!SwitchParam)
				{
					const FText Message = LOCTEXT("FailedToGenerateSwitchParam", "Could not generate switch enum parameter. Please refesh the switch node and connect an enum.");
					GenerationContext.Log(Message, Node);

					return Result;
				}

				if (SwitchParam->GetType() != UE::Mutable::Private::NodeScalarEnumParameter::GetStaticType())
				{
					const FText Message = LOCTEXT("WrongSwitchParamType", "Switch parameter of incorrect type.");
					GenerationContext.Log(Message, Node);

					return Result;
				}

				const int32 NumSwitchOptions = TypedNodeMaterialSwitch->GetNumElements();

				UE::Mutable::Private::NodeScalarEnumParameter* EnumParameter = static_cast<UE::Mutable::Private::NodeScalarEnumParameter*>(SwitchParam.get());
				if (NumSwitchOptions != EnumParameter->Options.Num())
				{
					const FText Message = LOCTEXT("MismatchedSwitch", "Switch enum and switch node have different number of options. Please refresh the switch node to make sure the outcomes are labeled properly.");
					GenerationContext.Log(Message, Node);
				}

				UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeMaterialSwitch> SwitchNode = new UE::Mutable::Private::NodeMaterialSwitch;
				SwitchNode->Parameter = SwitchParam;
				SwitchNode->Options.SetNum(NumSwitchOptions);

				for (int SelectorIndex = 0; SelectorIndex < NumSwitchOptions; ++SelectorIndex)
				{
					if (const UEdGraphPin* MaterialPin = FollowInputPin(*TypedNodeMaterialSwitch->GetElementPin(SelectorIndex)))
					{
						SwitchNode->Options[SelectorIndex] = GenerateMutableSourceMaterial(MaterialPin, GenerationContext);
					}
					else
					{
						const FText Message = LOCTEXT("MissingMaterial", "Unable to generate material switch node. Required connection not found.");
						GenerationContext.Log(Message, Node);
						return Result;
					}
				}

				Result = SwitchNode;
				return Result;
			}
			else
			{
				GenerationContext.Log(LOCTEXT("NoEnumParamInSwitch", "Switch nodes must have an enum switch parameter. Please connect an enum and refesh the switch node."), Node);
				return Result;
			}
		}(); // invoke lambda.
	}

	else if (const UCONodeMaterialVariation* TypedNodeMaterialVariation = Cast<UCONodeMaterialVariation>(Node))
	{
		UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeMaterialVariation> MaterialNode = new UE::Mutable::Private::NodeMaterialVariation();
		Result = MaterialNode;

		if (const UEdGraphPin* ConnectedPin = FollowInputPin(*TypedNodeMaterialVariation->DefaultPin()))
		{
			UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeMaterial> ChildNode = GenerateMutableSourceMaterial(ConnectedPin, GenerationContext);
			if (ChildNode)
			{
				MaterialNode->DefaultMaterial = ChildNode;
			}
			else
			{
				GenerationContext.Log(LOCTEXT("MaterialVariationFailed", "Material variation generation failed."), Node);
			}
		}

		const int32 NumVariations = TypedNodeMaterialVariation->GetNumVariations();
		MaterialNode->Variations.SetNum(NumVariations);
		for (int VariationIndex = 0; VariationIndex < NumVariations; ++VariationIndex)
		{
			if (const UEdGraphPin* VariationPin = TypedNodeMaterialVariation->VariationPin(VariationIndex))
			{
				MaterialNode->Variations[VariationIndex].Tag = TypedNodeMaterialVariation->GetVariationTag(VariationIndex, &GenerationContext.MacroNodesStack);

				if (const UEdGraphPin* ConnectedPin = FollowInputPin(*VariationPin))
				{
					UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeMaterial> ChildNode = GenerateMutableSourceMaterial(ConnectedPin, GenerationContext);
					MaterialNode->Variations[VariationIndex].Material = ChildNode;
				}
			}
		}
	}

	else if (const UCustomizableObjectNodeMaterialParameter* MaterialParameterNode = Cast<UCustomizableObjectNodeMaterialParameter>(Node))
	{
		bCacheNode = false;
		UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeMaterialParameter> MaterialNode;

		if (GenerationContext.MaterialParameterNodesCache.Contains(MaterialParameterNode))
		{
			MaterialNode = GenerationContext.MaterialParameterNodesCache[MaterialParameterNode];
		}
		else
		{
			MaterialNode = new UE::Mutable::Private::NodeMaterialParameter();
			MaterialNode->Name = MaterialParameterNode->GetParameterName(&GenerationContext.MacroNodesStack);
			MaterialNode->UID = GenerationContext.GetNodeIdUnique(Node).ToString();

			if (MaterialParameterNode->DefaultValue)
			{
				GenerationContext.MaterialParameterDefaultValues.Add(FName(MaterialParameterNode->GetParameterName()), UE::Mutable::Private::LoadObject(MaterialParameterNode->DefaultValue));
			}

			GenerationContext.ParameterUIDataMap.Add(MaterialParameterNode->GetParameterName(&GenerationContext.MacroNodesStack), FMutableParameterData(
				MaterialParameterNode->ParamUIMetadata,
				EMutableParameterType::Material));

			GenerationContext.MaterialParameterNodesCache.Add(MaterialParameterNode, MaterialNode);
		}

		// Store the parameter that needs to be processed
		if (GenerationContext.CurrentMaterialBreakParameter.ParameterType != EMaterialParameterType::None)
		{
			if (!MaterialParameterNode->ReferenceValue)
			{
				GenerationContext.Log(LOCTEXT("NoRefernceMaterialToBreak", "Could not find a Reference Material to break."), MaterialParameterNode);
			}
			else
			{
				switch (GenerationContext.CurrentMaterialBreakParameter.ParameterType)
				{
				case EMaterialParameterType::Texture:
				{
					//TODO(Max): Move this to the generatemutablesourceimage:
					UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeImageFromMaterialParameter> TextureNode = new UE::Mutable::Private::NodeImageFromMaterialParameter();
					TextureNode->ImageParameterName = GenerationContext.CurrentMaterialBreakParameter.ParameterName;

					// Force the same format that the default texture if any.
					UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeImageFormat> FormatNode = new UE::Mutable::Private::NodeImageFormat();

					// Force an "easy format" on the texture.
					FormatNode->Format = UE::Mutable::Private::EImageFormat::RGBA_UByte;
					FormatNode->Source = TextureNode;

					// Resize image node.
					UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeImageResize> ResizeNode = new UE::Mutable::Private::NodeImageResize();
					ResizeNode->Base = FormatNode;
					ResizeNode->bRelative = false;

					UE::Mutable::Private::FImageSize TextureSize(1);

					//Find reference Texture
					UTexture* Texture = nullptr;
					bool bParameterFound = MaterialParameterNode->ReferenceValue->GetTextureParameterValue(GenerationContext.CurrentMaterialBreakParameter.ParameterName, Texture);
					UTexture2D* ReferenceTexture = Cast<UTexture2D>(Texture);

					if (bParameterFound && ReferenceTexture)
					{
						const uint32 LODBias = ComputeLODBiasForTexture(GenerationContext, *ReferenceTexture);
						TextureSize.X = FMath::Max(ReferenceTexture->Source.GetSizeX() >> LODBias, 1);
						TextureSize.Y = FMath::Max(ReferenceTexture->Source.GetSizeY() >> LODBias, 1);
					}

					ResizeNode->SizeX = TextureSize.X;
					ResizeNode->SizeY = TextureSize.Y;

					MaterialNode->ImageParameterNodes.Add(GenerationContext.CurrentMaterialBreakParameter.ParameterName.ToString(), ResizeNode);

					break;
				}
				case EMaterialParameterType::Vector:
				{
					MaterialNode->ColorParameterNames.Add(GenerationContext.CurrentMaterialBreakParameter.ParameterName.ToString());
					break;
				}
				case EMaterialParameterType::Scalar:
				{
					MaterialNode->ScalarParameterNames.Add(GenerationContext.CurrentMaterialBreakParameter.ParameterName.ToString());
					break;
				}
				default:
					break;
				}
			}
		}

		Result = MaterialNode;
	}

	else if (const UCustomizableObjectNodeTable* TypedNodeTable = Cast<UCustomizableObjectNodeTable>(Node))
	{
		//This node will add a default value in case of error
		UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeMaterialConstant> ConstantValue = new UE::Mutable::Private::NodeMaterialConstant();
		ConstantValue->MaterialId = GenerationContext.CurrentReferencedMaterialIndex;

		Result = ConstantValue;

		if (Pin->PinType.PinCategory == Schema->PC_Material)
		{
			// Material pins have to skip the cache of nodes or they will return always the same column node
			bCacheNode = false;
		}

		bool bSuccess = true;
		UDataTable* DataTable = GetDataTable(TypedNodeTable, GenerationContext);

		if (DataTable)
		{
			FString ColumnName = TypedNodeTable->GetPinColumnName(Pin);
			FProperty* Property = TypedNodeTable->FindPinProperty(*Pin);

			if (!Property)
			{
				FString Msg = FString::Printf(TEXT("Couldn't find the column [%s] in the data table's struct."), *ColumnName);
				GenerationContext.Log(FText::FromString(Msg), Node);

				bSuccess = false;
			}

			if (bSuccess)
			{
				if (UMaterialInstance* TableMaterial = TypedNodeTable->GetColumnDefaultAssetByType<UMaterialInstance>(Pin))
				{
					check(GenerationContext.ReferencedMaterials.IsValidIndex(GenerationContext.CurrentReferencedMaterialIndex));
					UMaterialInterface* BaseMaterial = GenerationContext.ReferencedMaterials[GenerationContext.CurrentReferencedMaterialIndex];

					const bool bTableMaterialCheckDisabled = GenerationContext.CompilationContext->Object->GetPrivate()->IsTableMaterialsParentCheckDisabled();
					const bool bMaterialParentMismatch = !bTableMaterialCheckDisabled && BaseMaterial
						&& TableMaterial->GetMaterial() != BaseMaterial->GetMaterial();

					// Checking if the reference material of the Table Node has the same parent as the material of the Material Node 
					if (bMaterialParentMismatch)
					{
						GenerationContext.Log(LOCTEXT("DifferentParentMaterial", "The Default Material of the Data Table and the Mesh Section must have the same Parent Material."), Node);
						bSuccess = false;
					}
				}
				else
				{
					FString TableColumnName = TypedNodeTable->GetPinColumnName(Pin);
					FText Msg = FText::Format(LOCTEXT("DefaultValueNotFound", "Couldn't find a default value in the data table's struct for the column {0}. The default value is null or not a Material Instance."), FText::FromString(TableColumnName));
					GenerationContext.Log(Msg, Node);
					bSuccess = false;
				}
			}

			if (bSuccess)
			{
				// Generating a new data table if not exists
				UE::Mutable::Private::Ptr<UE::Mutable::Private::FTable> Table;
				Table = GenerateMutableSourceTable(DataTable, TypedNodeTable, GenerationContext);

				if (Table)
				{
					UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeMaterialTable> MaterialTableNode = new UE::Mutable::Private::NodeMaterialTable();

					// Materials use the parameter id as column names
					ColumnName = GenerationContext.CurrentMaterialParameterId;

					// Generating a new material column if it does not exist
					if (Table->FindColumn(ColumnName) == INDEX_NONE)
					{
						GenerationContext.CurrentTableColumnType = UE::Mutable::Private::ETableColumnType::Material;
						bSuccess = GenerateTableColumn(TypedNodeTable, Pin, Table, ColumnName, Property, FMutableSourceMeshData(), GenerationContext);

						if (!bSuccess)
						{
							FString Msg = FString::Printf(TEXT("Failed to generate the mutable table column [%s]"), *ColumnName);
							GenerationContext.Log(FText::FromString(Msg), Node);
						}
					}

					if (bSuccess)
					{
						Result = MaterialTableNode;

						MaterialTableNode->Table = Table;
						MaterialTableNode->ColumnName = ColumnName;
						MaterialTableNode->ParameterName = TypedNodeTable->ParameterName;
						MaterialTableNode->bNoneOption = TypedNodeTable->bAddNoneOption;
						MaterialTableNode->DefaultRowName = TypedNodeTable->DefaultRowName.ToString();
					}
				}
				else
				{
					FString Msg = FString::Printf(TEXT("Couldn't generate a mutable table."), *ColumnName);
					GenerationContext.Log(FText::FromString(Msg), Node);
				}
			}
		}
		else
		{
			GenerationContext.Log(LOCTEXT("MaterialTableError", "Couldn't find the data table of the node."), Node);
		}
	}

	else if (const UCustomizableObjectNodeMacroInstance* TypedNodeMacro = Cast<UCustomizableObjectNodeMacroInstance>(Node))
	{
		bCacheNode = false;
		Result = GenerateMutableSourceMacro<UE::Mutable::Private::NodeMaterial>(*Pin, GenerationContext, GenerateMutableSourceMaterial);
	}

	else if (const UCustomizableObjectNodeTunnel* TypedNodeTunnel = Cast<UCustomizableObjectNodeTunnel>(Node))
	{
		bCacheNode = false;
		Result = GenerateMutableSourceMacro<UE::Mutable::Private::NodeMaterial>(*Pin, GenerationContext, GenerateMutableSourceMaterial);
	}

	else
	{
		GenerationContext.Log(LOCTEXT("UnimplementedNode", "Node type not implemented yet."), Node);
	}

	if (bCacheNode)
	{
		GenerationContext.Generated.Add(Key, FGeneratedData(Node, Result));
		GenerationContext.GeneratedNodes.Add(Node);
	}

	if (Result)
	{
		Result->SetMessageContext(Node);
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE

