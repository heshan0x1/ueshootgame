// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR
#include "MaterialXStandardSurfaceShader.h"
#include "Engine/EngineTypes.h"

namespace mx = MaterialX;

FMaterialXStandardSurfaceShader::FMaterialXStandardSurfaceShader(UInterchangeBaseNodeContainer& BaseNodeContainer)
	: FMaterialXSurfaceShaderAbstract(BaseNodeContainer)
{
	NodeDefinition = mx::NodeDefinition::StandardSurface;
}

TSharedRef<FMaterialXBase> FMaterialXStandardSurfaceShader::MakeInstance(UInterchangeBaseNodeContainer& BaseNodeContainer)
{
	TSharedRef<FMaterialXStandardSurfaceShader> Result= MakeShared<FMaterialXStandardSurfaceShader>(BaseNodeContainer);
	Result->RegisterConnectNodeOutputToInputDelegates();
	return Result;
}

UInterchangeBaseNode* FMaterialXStandardSurfaceShader::Translate(mx::NodePtr StandardSurfaceNode)
{
	this->SurfaceShaderNode = StandardSurfaceNode;
	
	using namespace UE::Interchange::Materials;
	using namespace UE::Interchange::MaterialX;
	using namespace UE::Interchange::Materials::Standard::Nodes;

	UInterchangeShaderNode* StandardSurfaceShaderNode = FMaterialXSurfaceShaderAbstract::Translate(EInterchangeMaterialXShaders::StandardSurface);

	//Two sided
	if(MaterialX::InputPtr Input = GetInput(SurfaceShaderNode, mx::StandardSurface::Input::ThinWalled);
	   Input->hasValue() && mx::fromValueString<bool>(Input->getValueString()) == true)
	{
		// weird that we also have to enable that to have a two sided material (seems to only have meaning for Translucent material)
		ShaderGraphNode->SetCustomTwoSidedTransmission(true);
		ShaderGraphNode->SetCustomTwoSided(true);
	}

	if(UInterchangeShaderPortsAPI::HasInput(StandardSurfaceShaderNode, StandardSurface::Parameters::Transmission))
	{
		StandardSurfaceShaderNode->AddInt32Attribute(Attributes::EnumType, IndexSurfaceShaders);
		StandardSurfaceShaderNode->AddInt32Attribute(Attributes::EnumValue, int32(EInterchangeMaterialXShaders::StandardSurfaceTransmission));
	}

	// Outputs
	if(bIsSubstrateEnabled)
	{
		if (!bIsSubstrateAdaptiveGBufferEnabled)
		{
			MTLX_LOG("MaterialXStandardSurfaceShader", "Standard Surface material rendering might be wrong. Please select Substrate Adaptive GBuffer format in the Project settings.");
		}

		auto CreateShaderNodeInternal = [this]<class T>(mx::NodePtr StandardSurfaceNode, const FString& PathOrShaderType, const FString& DisplayLabel)
		{
			T* Node;

			const FString NodeUID = UInterchangeShaderNode::MakeNodeUid(GetUniqueName(StandardSurfaceNode) + DisplayLabel, FStringView{});

			if (Node = const_cast<T*>(Cast<T>(NodeContainer.GetNode(NodeUID))); !Node)
			{
				Node = NewObject<T>(&NodeContainer);
				NodeContainer.SetupNode(Node, NodeUID, DisplayLabel, EInterchangeNodeContainerType::TranslatedAsset);
				if constexpr (std::is_same_v<T, UInterchangeFunctionCallShaderNode>)
				{
					Node->SetCustomMaterialFunction(PathOrShaderType);
				}
				else
				{
					Node->SetCustomShaderType(PathOrShaderType);
				}
			}

			return Node;
		};

		if (UInterchangeShaderPortsAPI::HasInput(StandardSurfaceShaderNode, StandardSurface::Parameters::Transmission) &&
			UInterchangeShaderPortsAPI::HasInput(StandardSurfaceShaderNode, StandardSurface::Parameters::Subsurface) &&
			UInterchangeShaderPortsAPI::HasInput(StandardSurfaceShaderNode, StandardSurface::Parameters::BaseColor))
		{
			// 1. Set the Material as Opaque with Mask Mode
			StandardSurfaceShaderNode->AddInt32Attribute(Attributes::EnumType, IndexSurfaceShaders);
			StandardSurfaceShaderNode->AddInt32Attribute(Attributes::EnumValue, int32(EInterchangeMaterialXShaders::StandardSurface));
			ShaderGraphNode->SetCustomBlendMode(EBlendMode::BLEND_Masked);

			//2. Disconnect the transmission inputs
			FString TransmissionNodeUid, TransmissionOutputName;
			UInterchangeShaderPortsAPI::DisconnectInputFromOutputNode(StandardSurfaceShaderNode, StandardSurface::Parameters::Transmission.ToString(), TransmissionNodeUid, TransmissionOutputName);
			UInterchangeShaderPortsAPI::DisconnectInput(StandardSurfaceShaderNode, StandardSurface::Parameters::TransmissionColor.ToString());
			UInterchangeShaderPortsAPI::DisconnectInput(StandardSurfaceShaderNode, StandardSurface::Parameters::TransmissionDepth.ToString());
			UInterchangeShaderPortsAPI::DisconnectInput(StandardSurfaceShaderNode, StandardSurface::Parameters::TransmissionDispersion.ToString());
			UInterchangeShaderPortsAPI::DisconnectInput(StandardSurfaceShaderNode, StandardSurface::Parameters::TransmissionExtraRoughness.ToString());
			UInterchangeShaderPortsAPI::DisconnectInput(StandardSurfaceShaderNode, StandardSurface::Parameters::TransmissionScatter.ToString());
			UInterchangeShaderPortsAPI::DisconnectInput(StandardSurfaceShaderNode, StandardSurface::Parameters::TransmissionScatterAnisotropy.ToString());

			//3. Create DitherMask node
			UInterchangeShaderNode* DitherMaskNode = CreateShaderNodeInternal.operator()<UInterchangeFunctionCallShaderNode>(StandardSurfaceNode, UE::MaterialFunctions::Path::DitherMask, TEXT("DitherMaskNode"));

			//4. Connect the surface shader and the transmission to the MF_DitherMask
			if (const UInterchangeShaderNode* TransmissionNode = Cast<UInterchangeShaderNode>(NodeContainer.GetNode(TransmissionNodeUid)))
			{
				UInterchangeShaderPortsAPI::ConnectOuputToInputByName(DitherMaskNode, TEXT("BSDF"), StandardSurfaceShaderNode->GetUniqueID(), StandardSurface::SubstrateMaterial::Outputs::Opaque.ToString());
				UInterchangeShaderPortsAPI::ConnectOuputToInputByName(DitherMaskNode, TEXT("transmission"), TransmissionNode->GetUniqueID(), TransmissionOutputName);
			}

			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, SubstrateMaterial::Parameters::FrontMaterial.ToString(), DitherMaskNode->GetUniqueID(), TEXT("Dither"));
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, SubstrateMaterial::Parameters::OpacityMask.ToString(), DitherMaskNode->GetUniqueID(), SubstrateMaterial::Parameters::OpacityMask.ToString());
		}
		else if (UInterchangeShaderPortsAPI::HasInput(StandardSurfaceShaderNode, StandardSurface::Parameters::Transmission) &&
				 UInterchangeShaderPortsAPI::HasInput(StandardSurfaceShaderNode, StandardSurface::Parameters::Subsurface))
		{
			ShaderGraphNode->SetCustomBlendMode(EBlendMode::BLEND_TranslucentColoredTransmittance);
			auto ReplaceInputConnection = [this, StandardSurfaceShaderNode](const FString& InputToRemove, const FString& InputName)
			{
				UInterchangeShaderPortsAPI::DisconnectInput(StandardSurfaceShaderNode, InputToRemove);

				FString NodeUid, NodeOutputName;
				UInterchangeShaderPortsAPI::GetInputConnection(StandardSurfaceShaderNode, InputName, NodeUid, NodeOutputName);
				if (const UInterchangeShaderNode* ShaderNode = Cast<UInterchangeShaderNode>(NodeContainer.GetNode(NodeUid)))
				{
					UInterchangeShaderPortsAPI::ConnectOuputToInputByName(StandardSurfaceShaderNode, InputToRemove, ShaderNode->GetUniqueID(), NodeOutputName);
				}
			};

			//1. subsurface_color input now replaces the base_color
			if(UInterchangeShaderPortsAPI::HasInput(StandardSurfaceShaderNode, StandardSurface::Parameters::SubsurfaceColor))
			{
				ReplaceInputConnection(StandardSurface::Parameters::BaseColor.ToString(), StandardSurface::Parameters::SubsurfaceColor.ToString());
			}

			//2. replace base by subsurface
			ReplaceInputConnection(StandardSurface::Parameters::Base.ToString(), StandardSurface::Parameters::Subsurface.ToString());

			//3. scale the subsurface_radius by 600 (empirical value that gives better results, need further testing though)
			FString SubsurfaceRadiusNodeUid, SubsurfaceRadiusNodeOutputName;
			if (UInterchangeShaderPortsAPI::GetInputConnection(StandardSurfaceShaderNode, StandardSurface::Parameters::SubsurfaceRadius.ToString(), SubsurfaceRadiusNodeUid, SubsurfaceRadiusNodeOutputName))
			{
				UInterchangeShaderNode* ScaleSubsurfaceRadiusNode = CreateShaderNodeInternal.operator()<UInterchangeShaderNode>(StandardSurfaceNode, Multiply::Name.ToString(), TEXT("ScaleSubsurfaceRadius"));
				ScaleSubsurfaceRadiusNode->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Multiply::Inputs::B.ToString()), 600);
				UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ScaleSubsurfaceRadiusNode, Multiply::Inputs::A.ToString(), SubsurfaceRadiusNodeUid, SubsurfaceRadiusNodeOutputName);

				// 4. Multiply the scaled SubsurfaceRadius by the TransmissionColor if any
				FString TransmissionColorNodeUid, TransmissionColorNodeOutputName;
				if(UInterchangeShaderPortsAPI::DisconnectInputFromOutputNode(StandardSurfaceShaderNode, StandardSurface::Parameters::TransmissionColor.ToString(), TransmissionColorNodeUid, TransmissionColorNodeOutputName))
				{
					UInterchangeShaderNode* SubsurfaceTransmissionColorNode = CreateShaderNodeInternal.operator()<UInterchangeShaderNode>(StandardSurfaceNode, Multiply::Name.ToString(), TEXT("SubsurfaceRadiusTransmissionColorMult"));
					UInterchangeShaderPortsAPI::ConnectOuputToInputByName(SubsurfaceTransmissionColorNode, Multiply::Inputs::A.ToString(), TransmissionColorNodeUid, TransmissionColorNodeOutputName);
					UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(SubsurfaceTransmissionColorNode, Multiply::Inputs::B.ToString(), ScaleSubsurfaceRadiusNode->GetUniqueID());
					UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(StandardSurfaceShaderNode, StandardSurface::Parameters::TransmissionColor.ToString(), SubsurfaceTransmissionColorNode->GetUniqueID());
				}
				else
				{
					UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(StandardSurfaceShaderNode, StandardSurface::Parameters::TransmissionColor.ToString(), ScaleSubsurfaceRadiusNode->GetUniqueID());
				}
			}

			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, SubstrateMaterial::Parameters::FrontMaterial.ToString(), StandardSurfaceShaderNode->GetUniqueID(), StandardSurface::SubstrateMaterial::Outputs::Translucent.ToString());
		}
		else if(UInterchangeShaderPortsAPI::HasInput(StandardSurfaceShaderNode, StandardSurface::Parameters::Transmission))
		{
			ShaderGraphNode->SetCustomBlendMode(EBlendMode::BLEND_TranslucentColoredTransmittance);
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, SubstrateMaterial::Parameters::FrontMaterial.ToString(), StandardSurfaceShaderNode->GetUniqueID(), StandardSurface::SubstrateMaterial::Outputs::Translucent.ToString());
		}
		else
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, SubstrateMaterial::Parameters::FrontMaterial.ToString(), StandardSurfaceShaderNode->GetUniqueID(), StandardSurface::SubstrateMaterial::Outputs::Opaque.ToString());
			if(UInterchangeShaderPortsAPI::HasInput(StandardSurfaceShaderNode, StandardSurface::Parameters::Opacity))
			{
				UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, SubstrateMaterial::Parameters::OpacityMask.ToString(), StandardSurfaceShaderNode->GetUniqueID(), StandardSurface::SubstrateMaterial::Outputs::Opacity.ToString());
				ShaderGraphNode->SetCustomBlendMode(EBlendMode::BLEND_Masked);
			}
		}
	}
	else
	{
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::BaseColor.ToString(), StandardSurfaceShaderNode->GetUniqueID(), TEXT("Base Color"));
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::Metallic.ToString(), StandardSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::Metallic.ToString());
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::Specular.ToString(), StandardSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::Specular.ToString());
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::Roughness.ToString(), StandardSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::Roughness.ToString());
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::EmissiveColor.ToString(), StandardSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::EmissiveColor.ToString());
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::Anisotropy.ToString(), StandardSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::Anisotropy.ToString());
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::Normal.ToString(), StandardSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::Normal.ToString());
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::Tangent.ToString(), StandardSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::Tangent.ToString());

		// We can't have all shading models at once, so we have to make a choice here
		if(UInterchangeShaderPortsAPI::HasInput(StandardSurfaceShaderNode, StandardSurface::Parameters::Transmission))
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::Opacity.ToString(), StandardSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::Opacity.ToString());
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, ThinTranslucent::Parameters::TransmissionColor.ToString(), StandardSurfaceShaderNode->GetUniqueID(), ThinTranslucent::Parameters::TransmissionColor.ToString());
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, Common::Parameters::Refraction.ToString(), StandardSurfaceShaderNode->GetUniqueID(), Common::Parameters::Refraction.ToString());

			// If we have have Transmission and Opacity let's use the Surface Coverage instead
			if(UInterchangeShaderPortsAPI::HasInput(StandardSurfaceShaderNode, StandardSurface::Parameters::Opacity))
			{
				UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, ThinTranslucent::Parameters::SurfaceCoverage.ToString(), StandardSurfaceShaderNode->GetUniqueID(), ThinTranslucent::Parameters::SurfaceCoverage.ToString());
			}
		}
		else if(UInterchangeShaderPortsAPI::HasInput(StandardSurfaceShaderNode, StandardSurface::Parameters::Sheen))
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, Sheen::Parameters::SheenColor.ToString(), StandardSurfaceShaderNode->GetUniqueID(), Sheen::Parameters::SheenColor.ToString());
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, Sheen::Parameters::SheenRoughness.ToString(), StandardSurfaceShaderNode->GetUniqueID(), Sheen::Parameters::SheenRoughness.ToString());
		}
		else if(UInterchangeShaderPortsAPI::HasInput(StandardSurfaceShaderNode, StandardSurface::Parameters::Coat))
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, ClearCoat::Parameters::ClearCoat.ToString(), StandardSurfaceShaderNode->GetUniqueID(), ClearCoat::Parameters::ClearCoat.ToString());
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, ClearCoat::Parameters::ClearCoatRoughness.ToString(), StandardSurfaceShaderNode->GetUniqueID(), ClearCoat::Parameters::ClearCoatRoughness.ToString());
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, ClearCoat::Parameters::ClearCoatNormal.ToString(), StandardSurfaceShaderNode->GetUniqueID(), ClearCoat::Parameters::ClearCoatNormal.ToString());
		}
		else if(UInterchangeShaderPortsAPI::HasInput(StandardSurfaceShaderNode, StandardSurface::Parameters::Subsurface))
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::Opacity.ToString(), StandardSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::Opacity.ToString());
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, Subsurface::Parameters::SubsurfaceColor.ToString(), StandardSurfaceShaderNode->GetUniqueID(), Subsurface::Parameters::SubsurfaceColor.ToString());
		}
		else if(UInterchangeShaderPortsAPI::HasInput(StandardSurfaceShaderNode, StandardSurface::Parameters::Opacity))
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, ThinTranslucent::Parameters::SurfaceCoverage.ToString(), StandardSurfaceShaderNode->GetUniqueID(), ThinTranslucent::Parameters::SurfaceCoverage.ToString());
		}
	}

	return StandardSurfaceShaderNode;
}

mx::InputPtr FMaterialXStandardSurfaceShader::GetInputNormal(mx::NodePtr StandardSurfaceNode, const char*& InputNormal) const
{
	InputNormal = mx::StandardSurface::Input::Normal;
	
	mx::InputPtr Input = StandardSurfaceNode->getActiveInput(InputNormal);
		
	if (!Input)
	{
		Input = StandardSurfaceNode->getNodeDef(mx::EMPTY_STRING, true)->getActiveInput(InputNormal);
	}

	return Input;
}

#endif
