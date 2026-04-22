// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR
#include "MaterialXOpenPBRSurfaceShader.h"

#include "Engine/EngineTypes.h"

namespace mx = MaterialX;

FMaterialXOpenPBRSurfaceShader::FMaterialXOpenPBRSurfaceShader(UInterchangeBaseNodeContainer& BaseNodeContainer)
	: FMaterialXSurfaceShaderAbstract(BaseNodeContainer)
{
	NodeDefinition = mx::NodeDefinition::OpenPBRSurface;
}

TSharedRef<FMaterialXBase> FMaterialXOpenPBRSurfaceShader::MakeInstance(UInterchangeBaseNodeContainer& BaseNodeContainer)
{
	TSharedRef<FMaterialXOpenPBRSurfaceShader> Result = MakeShared<FMaterialXOpenPBRSurfaceShader>(BaseNodeContainer);
	Result->RegisterConnectNodeOutputToInputDelegates();
	return Result;
}

UInterchangeBaseNode* FMaterialXOpenPBRSurfaceShader::Translate(MaterialX::NodePtr OpenPBRSurfaceNode)
{
	using namespace UE::Interchange::Materials;
	using namespace UE::Interchange::MaterialX;
	using namespace UE::Interchange::Materials::Standard::Nodes;

	this->SurfaceShaderNode = OpenPBRSurfaceNode;

	UInterchangeShaderNode* OpenPBRSurfaceShaderNode = FMaterialXSurfaceShaderAbstract::Translate(EInterchangeMaterialXShaders::OpenPBRSurface);

	//Two sided
	if(MaterialX::InputPtr Input = GetInput(SurfaceShaderNode, mx::OpenPBRSurface::Input::GeometryThinWalled);
	   Input->hasValue() && mx::fromValueString<bool>(Input->getValueString()) == true)
	{
		// weird that we also have to enable that to have a two sided material (seems to only have meaning for Translucent material)
		ShaderGraphNode->SetCustomTwoSidedTransmission(true);
		ShaderGraphNode->SetCustomTwoSided(true);
	}

	if(UInterchangeShaderPortsAPI::HasInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::TransmissionWeight))
	{
		OpenPBRSurfaceShaderNode->AddInt32Attribute(Attributes::EnumType, IndexSurfaceShaders);
		OpenPBRSurfaceShaderNode->AddInt32Attribute(Attributes::EnumValue, int32(EInterchangeMaterialXShaders::OpenPBRSurfaceTransmission));
		ShaderGraphNode->SetCustomBlendMode(EBlendMode::BLEND_TranslucentColoredTransmittance);
	}

	if(bIsSubstrateEnabled)
	{
		if (!bIsSubstrateAdaptiveGBufferEnabled)
		{
			MTLX_LOG("MaterialXOpenPBRSurfaceShader", "OpenPBR material rendering might be wrong. Please select Substrate Adaptive GBuffer format in the Project settings.");
		}

		auto CreateShaderNodeInternal = [this]<class T>(mx::NodePtr OpenPBRSurfaceNode, const FString & PathOrShaderType, const FString & DisplayLabel)
		{
			T* Node;

			const FString NodeUID = UInterchangeShaderNode::MakeNodeUid(GetUniqueName(OpenPBRSurfaceNode) + DisplayLabel, FStringView{});

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

		if (UInterchangeShaderPortsAPI::HasInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::TransmissionWeight) &&
			UInterchangeShaderPortsAPI::HasInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::SubsurfaceWeight) &&
			UInterchangeShaderPortsAPI::HasInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::BaseColor))
		{
			// 1. Set the Material as Opaque with Mask Mode
			OpenPBRSurfaceShaderNode->AddInt32Attribute(Attributes::EnumType, IndexSurfaceShaders);
			OpenPBRSurfaceShaderNode->AddInt32Attribute(Attributes::EnumValue, int32(EInterchangeMaterialXShaders::OpenPBRSurface));
			ShaderGraphNode->SetCustomBlendMode(EBlendMode::BLEND_Masked);

			//2. Disconnect the transmission inputs
			FString TransmissionNodeUid, TransmissionOutputName;
			UInterchangeShaderPortsAPI::DisconnectInputFromOutputNode(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::TransmissionWeight.ToString(), TransmissionNodeUid, TransmissionOutputName);
			UInterchangeShaderPortsAPI::DisconnectInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::TransmissionColor.ToString());
			UInterchangeShaderPortsAPI::DisconnectInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::TransmissionDepth.ToString());
			UInterchangeShaderPortsAPI::DisconnectInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::TransmissionDispersionAbbeNumber.ToString());
			UInterchangeShaderPortsAPI::DisconnectInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::TransmissionDispersionScale.ToString());
			UInterchangeShaderPortsAPI::DisconnectInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::TransmissionScatterAnisotropy.ToString());
			UInterchangeShaderPortsAPI::DisconnectInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::TransmissionScatter.ToString());

			//3. Create DitherMask node
			UInterchangeShaderNode* DitherMaskNode = CreateShaderNodeInternal.operator()<UInterchangeFunctionCallShaderNode>(OpenPBRSurfaceNode, UE::MaterialFunctions::Path::DitherMask, TEXT("DitherMaskNode"));

			//4. Connect the surface shader and the transmission to the MF_DitherMask
			if (const UInterchangeShaderNode* TransmissionNode = Cast<UInterchangeShaderNode>(NodeContainer.GetNode(TransmissionNodeUid)))
			{
				UInterchangeShaderPortsAPI::ConnectOuputToInputByName(DitherMaskNode, TEXT("BSDF"), OpenPBRSurfaceShaderNode->GetUniqueID(), OpenPBRSurface::SubstrateMaterial::Outputs::FrontMaterial.ToString());
				UInterchangeShaderPortsAPI::ConnectOuputToInputByName(DitherMaskNode, TEXT("transmission"), TransmissionNode->GetUniqueID(), TransmissionOutputName);
			}

			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, SubstrateMaterial::Parameters::FrontMaterial.ToString(), DitherMaskNode->GetUniqueID(), TEXT("Dither"));
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, SubstrateMaterial::Parameters::OpacityMask.ToString(), DitherMaskNode->GetUniqueID(), SubstrateMaterial::Parameters::OpacityMask.ToString());
		}
		else if (UInterchangeShaderPortsAPI::HasInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::TransmissionWeight) &&
				 UInterchangeShaderPortsAPI::HasInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::SubsurfaceWeight))
		{
			ShaderGraphNode->SetCustomBlendMode(EBlendMode::BLEND_TranslucentColoredTransmittance);
			auto ReplaceInputConnection = [this, OpenPBRSurfaceShaderNode](const FString& InputToRemove, const FString& InputName)
				{
					UInterchangeShaderPortsAPI::DisconnectInput(OpenPBRSurfaceShaderNode, InputToRemove);

					FString NodeUid, NodeOutputName;
					UInterchangeShaderPortsAPI::GetInputConnection(OpenPBRSurfaceShaderNode, InputName, NodeUid, NodeOutputName);
					if (const UInterchangeShaderNode* ShaderNode = Cast<UInterchangeShaderNode>(NodeContainer.GetNode(NodeUid)))
					{
						UInterchangeShaderPortsAPI::ConnectOuputToInputByName(OpenPBRSurfaceShaderNode, InputToRemove, ShaderNode->GetUniqueID(), NodeOutputName);
					}
				};

			//1. subsurface_color input now replaces the base_color
			if (UInterchangeShaderPortsAPI::HasInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::SubsurfaceColor))
			{
				ReplaceInputConnection(OpenPBRSurface::Parameters::BaseColor.ToString(), OpenPBRSurface::Parameters::SubsurfaceColor.ToString());
			}

			//2. replace base by subsurface
			ReplaceInputConnection(OpenPBRSurface::Parameters::BaseWeight.ToString(), OpenPBRSurface::Parameters::SubsurfaceWeight.ToString());

			//3. scale the subsurface_radius by 600 (empirical value that gives better results, need further testing though)
			FString SubsurfaceRadiusNodeUid, SubsurfaceRadiusNodeOutputName;
			if (UInterchangeShaderPortsAPI::GetInputConnection(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::SubsurfaceRadius.ToString(), SubsurfaceRadiusNodeUid, SubsurfaceRadiusNodeOutputName))
			{
				UInterchangeShaderNode* ScaleSubsurfaceRadiusNode = CreateShaderNodeInternal.operator()<UInterchangeShaderNode>(OpenPBRSurfaceNode, Multiply::Name.ToString(), TEXT("ScaleSubsurfaceRadius"));
				ScaleSubsurfaceRadiusNode->AddFloatAttribute(UInterchangeShaderPortsAPI::MakeInputValueKey(Multiply::Inputs::B.ToString()), 600);
				UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ScaleSubsurfaceRadiusNode, Multiply::Inputs::A.ToString(), SubsurfaceRadiusNodeUid, SubsurfaceRadiusNodeOutputName);

				// 4. Multiply the scaled SubsurfaceRadius by the TransmissionColor if any
				FString TransmissionColorNodeUid, TransmissionColorNodeOutputName;
				if (UInterchangeShaderPortsAPI::DisconnectInputFromOutputNode(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::TransmissionColor.ToString(), TransmissionColorNodeUid, TransmissionColorNodeOutputName))
				{
					UInterchangeShaderNode* SubsurfaceTransmissionColorNode = CreateShaderNodeInternal.operator()<UInterchangeShaderNode>(OpenPBRSurfaceNode, Multiply::Name.ToString(), TEXT("SubsurfaceRadiusTransmissionColorMult"));
					UInterchangeShaderPortsAPI::ConnectOuputToInputByName(SubsurfaceTransmissionColorNode, Multiply::Inputs::A.ToString(), TransmissionColorNodeUid, TransmissionColorNodeOutputName);
					UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(SubsurfaceTransmissionColorNode, Multiply::Inputs::B.ToString(), ScaleSubsurfaceRadiusNode->GetUniqueID());
					UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::TransmissionColor.ToString(), SubsurfaceTransmissionColorNode->GetUniqueID());
				}
				else
				{
					UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::TransmissionColor.ToString(), ScaleSubsurfaceRadiusNode->GetUniqueID());
				}
			}

			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, SubstrateMaterial::Parameters::FrontMaterial.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), OpenPBRSurface::SubstrateMaterial::Outputs::FrontMaterial.ToString());
		}
		else
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, SubstrateMaterial::Parameters::FrontMaterial.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), OpenPBRSurface::SubstrateMaterial::Outputs::FrontMaterial.ToString());

			if (UInterchangeShaderPortsAPI::HasInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::GeometryOpacity))
			{
				UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, SubstrateMaterial::Parameters::OpacityMask.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), OpenPBRSurface::SubstrateMaterial::Outputs::OpacityMask.ToString());
				ShaderGraphNode->SetCustomBlendMode(EBlendMode::BLEND_Masked);
			}
		}
	}
	else
	{	// Outputs
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::BaseColor.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::BaseColor.ToString());
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::Metallic.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::Metallic.ToString());
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::Specular.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::Specular.ToString());
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::Roughness.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::Roughness.ToString());
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::EmissiveColor.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::EmissiveColor.ToString());
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::Anisotropy.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::Anisotropy.ToString());
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::Normal.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::Normal.ToString());
		UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::Tangent.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::Tangent.ToString());

		// We can't have all shading models at once, so we have to make a choice here
		if(UInterchangeShaderPortsAPI::HasInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::TransmissionWeight))
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::Opacity.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::Opacity.ToString());
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, ThinTranslucent::Parameters::TransmissionColor.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), ThinTranslucent::Parameters::TransmissionColor.ToString());
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, Common::Parameters::Refraction.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), Common::Parameters::Refraction.ToString());
		}
		else if(UInterchangeShaderPortsAPI::HasInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::FuzzWeight))
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, Sheen::Parameters::SheenColor.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), Sheen::Parameters::SheenColor.ToString());
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, Sheen::Parameters::SheenRoughness.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), Sheen::Parameters::SheenRoughness.ToString());
		}
		else if(UInterchangeShaderPortsAPI::HasInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::CoatWeight))
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, ClearCoat::Parameters::ClearCoat.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), ClearCoat::Parameters::ClearCoat.ToString());
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, ClearCoat::Parameters::ClearCoatRoughness.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), ClearCoat::Parameters::ClearCoatRoughness.ToString());
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, ClearCoat::Parameters::ClearCoatNormal.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), ClearCoat::Parameters::ClearCoatNormal.ToString());
		}
		else if(UInterchangeShaderPortsAPI::HasInput(OpenPBRSurfaceShaderNode, OpenPBRSurface::Parameters::SubsurfaceWeight))
		{
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, PBRMR::Parameters::Opacity.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), PBRMR::Parameters::Opacity.ToString());
			UInterchangeShaderPortsAPI::ConnectOuputToInputByName(ShaderGraphNode, Subsurface::Parameters::SubsurfaceColor.ToString(), OpenPBRSurfaceShaderNode->GetUniqueID(), Subsurface::Parameters::SubsurfaceColor.ToString());
		}
	}

	return OpenPBRSurfaceShaderNode;
}

mx::InputPtr FMaterialXOpenPBRSurfaceShader::GetInputNormal(mx::NodePtr OpenPbrSurfaceShaderNode, const char*& InputNormal) const
{
	InputNormal = mx::OpenPBRSurface::Input::GeometryNormal;
	mx::InputPtr Input = OpenPbrSurfaceShaderNode->getActiveInput(InputNormal);

	// if no input is connected take the one from the nodedef
	if (!Input)
	{
		Input = OpenPbrSurfaceShaderNode->getNodeDef(mx::EMPTY_STRING, true)->getActiveInput(InputNormal);
	}

	return Input;
}

#endif //WITH_EDITOR
