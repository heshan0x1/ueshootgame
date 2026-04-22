// Copyright Epic Games, Inc. All Rights Reserved.

#include "UfbxMaterial.h"
#include "UfbxParser.h"

#include "UfbxConvert.h"

#include "FbxMaterial.h"

#include "InterchangeMaterialDefinitions.h"
#include "InterchangeMaterialInstanceNode.h"
#include "InterchangeTexture2DNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

#include "Fbx/InterchangeFbxMessages.h"

#include "Misc/Paths.h"

#if WITH_ENGINE
#include "Texture/InterchangeImageWrapperTranslator.h"
#endif

#define GET_VALLUE_FROM_FACTOR_MAP( FactorMap ) FactorMap.has_value ? FactorMap.value_real : 1.f;

#define LOCTEXT_NAMESPACE "InterchangeUfbxMaterial"

namespace UE::Interchange::Private
{

	namespace Inputs
	{
		///PostFixes
		namespace PostFix
		{
			const FString Color_RGB = TEXT("_RGB");
			const FString Color_A = TEXT("_A");

			const FString TexCoord = TEXT("_TexCoord");

			const FString OffsetX = TEXT("_Offset_X");
			const FString OffsetY = TEXT("_Offset_Y");
			const FString ScaleX = TEXT("_Scale_X");
			const FString ScaleY = TEXT("_Scale_Y");

			const FString OffsetScale = TEXT("_OffsetScale");

			const FString Rotation = TEXT("_Rotation");

			const FString TilingMethod = TEXT("_TilingMethod");
		}
	}

	class  FMaterialMapConverter
	{
	public:
		explicit FMaterialMapConverter(FUfbxParser& InParser, UInterchangeBaseNodeContainer& InNodeContainer, UInterchangeBaseNode& InMaterialInstance)
			: Parser(InParser)
			, NodeContainer(InNodeContainer)
			, MaterialInstance(InMaterialInstance)
		{
		}

		FString FindTextureFile(const ufbx_texture& Texture)
		{
			// #ufbx_todo: maybe just for each of "filename" always check absolute/relative? Just need to decide on the order...

			FString TextureFilepath = Convert::ToUnrealString(Texture.absolute_filename);
			FPaths::NormalizeFilename(TextureFilepath);
			if(FPaths::FileExists(TextureFilepath))
			{
				return TextureFilepath;
			}
			FString FileBasePath = FPaths::GetPath(Parser.SourceFilename);

			FString RelativeToFBXTextureFilepath = FileBasePath / Convert::ToUnrealString(Texture.relative_filename);
			FPaths::NormalizeFilename(RelativeToFBXTextureFilepath);
			if( FPaths::FileExists(RelativeToFBXTextureFilepath) )
			{
				return RelativeToFBXTextureFilepath;
			}

			FString FilenameToFBXTextureFilepath = FileBasePath / Convert::ToUnrealString(Texture.filename);
			FPaths::NormalizeFilename(FilenameToFBXTextureFilepath);
			if( FPaths::FileExists(FilenameToFBXTextureFilepath) )
			{
				return FilenameToFBXTextureFilepath;
			}

			// Some fbx files do not store the actual absolute filename as absolute and it is actually relative.  Try to get it relative to the FBX file we are importing
			FString AbsoluteAsRelativeToFBXTextureFilepath = FileBasePath / Convert::ToUnrealString(Texture.absolute_filename);
			FPaths::NormalizeFilename(AbsoluteAsRelativeToFBXTextureFilepath );
			if( FPaths::FileExists(AbsoluteAsRelativeToFBXTextureFilepath ) )
			{
				return AbsoluteAsRelativeToFBXTextureFilepath ;
			}

			if (TextureFilepath.IsEmpty())
			{
				// Make a name so it could be used as a key or in a message
				return FString::Printf(TEXT("Texture_%d"), Texture.element_id);
			}
			return TextureFilepath;
		}

		const UInterchangeTexture2DNode* CreateTexture2DNode(const ufbx_texture& Texture)
		{
			const FString TextureFilename = FindTextureFile(Texture);
			if (TextureFilename.IsEmpty() || !FPaths::FileExists(TextureFilename))
			{
				if (!GIsAutomationTesting)
				{
					UInterchangeResultTextureDisplay_TextureFileDoNotExist* Message = Parser.AddMessage<UInterchangeResultTextureDisplay_TextureFileDoNotExist>();
					Message->TextureName = TextureFilename;
					Message->MaterialName = MaterialInstance.GetDisplayLabel();
				}

				return nullptr;
			}
			return CreateTexture2DNode(TextureFilename);
		}

		// Expected normalized full path for existing file textures
		const UInterchangeTexture2DNode* CreateTexture2DNode(const FString& TextureFilePath)
		{
			const FString TextureName = FPaths::GetBaseFilename(TextureFilePath);
			const FString TextureNodeID = UInterchangeTextureNode::MakeNodeUid(TextureName);

			if (const UInterchangeTexture2DNode* TextureNode = Cast<const UInterchangeTexture2DNode>(NodeContainer.GetNode(TextureNodeID)))
			{
				return TextureNode;
			}

			UInterchangeTexture2DNode* NewTextureNode = UInterchangeTexture2DNode::Create(&NodeContainer, TextureNodeID);
			NewTextureNode->SetDisplayLabel(TextureName);

			//All texture translator expect a file as the payload key
			NewTextureNode->SetPayLoadKey(TextureFilePath);

			return NewTextureNode;
		}

		FUfbxParser& Parser;
		UInterchangeBaseNodeContainer& NodeContainer;
		UInterchangeBaseNode& MaterialInstance;
	};

	UInterchangeMaterialInstanceNode* CreateMaterialInstanceNode(UInterchangeBaseNodeContainer& NodeContainer, const FString& NodeUID, const FString& NodeLabel)
	{
		UInterchangeMaterialInstanceNode* MaterialInstanceNode = NewObject<UInterchangeMaterialInstanceNode>(&NodeContainer);
		MaterialInstanceNode->SetCustomParent(TEXT("/InterchangeAssets/Materials/FBXLegacyPhongSurfaceMaterial.FBXLegacyPhongSurfaceMaterial"));
		NodeContainer.SetupNode(MaterialInstanceNode, NodeUID, NodeLabel, EInterchangeNodeContainerType::TranslatedAsset);

		return MaterialInstanceNode;
	}

	void FUfbxMaterial::AddMaterials(UInterchangeBaseNodeContainer& NodeContainer)
	{
		using namespace UE::Interchange::Materials;

		for (ufbx_material* Material : Parser.Scene->materials)
		{
			if (!Material)
			{
				continue;
			}

			FString MaterialLabel = Parser.GetMaterialLabel(*Material).ToString();
			
			FString MaterialUid = Parser.GetMaterialUid(*Material);
			UInterchangeMaterialInstanceNode* MaterialInstanceNode = CreateMaterialInstanceNode(NodeContainer, MaterialUid, MaterialLabel);
			if (MaterialInstanceNode == nullptr)
			{
				FFormatNamedArguments Args
				{
					{ TEXT("MaterialName"), FText::FromString(MaterialLabel) }
				};
				UInterchangeResultError_Generic* Message = Parser.AddMessage<UInterchangeResultError_Generic>();
				Message->Text = FText::Format(LOCTEXT("CannotCreateUfbxMaterial", "Cannot create uFBX material '{MaterialName}'."), Args);
				continue;
			}

			// WithUVTransform
			bool bHasUVTransform = false;

			FMaterialMapConverter MaterialMapConverter(Parser, NodeContainer, *MaterialInstanceNode);
			
			auto SetColorFromMap = [&MaterialInstanceNode](FName InputName, const ufbx_material_map& Map, float Factor, TFunctionRef<bool(const FLinearColor&)> Filter = [](const FLinearColor&){return true;}) -> bool
			{
				if (Map.value_components >=3)
				{
					const FString InputAttributeKey = InputName.ToString();
					FLinearColor PropertyValue(Convert::ConvertVec4(Map.value_vec4) * Factor);
					if (Filter(PropertyValue))
					{
						return MaterialInstanceNode->AddVectorParameterValue(InputAttributeKey, PropertyValue);
					}
				}

				return false;
			};

			auto SetScalarFromMap = [&MaterialInstanceNode](FName InputName, const ufbx_material_map& Map, float Factor) -> bool
			{
				const FString InputAttributeKey = InputName.ToString();
				float PropertyValue(Map.value_real);
				return MaterialInstanceNode->AddScalarParameterValue(InputAttributeKey, PropertyValue);
			};

			auto SetTextureFromMap = [&MaterialInstanceNode, &MaterialMapConverter, &bHasUVTransform](FName InputName, const ufbx_material_map& Map, float Factor) -> bool
			{
				const ufbx_texture* Texture = Map.texture_enabled ? Map.texture : nullptr;
				if (!Texture)
				{
					return false;	
				}

				const UInterchangeTexture2DNode* TextureNode = MaterialMapConverter.CreateTexture2DNode(*Texture);

				if (!TextureNode)
				{
					return false;
				}

				MaterialInstanceNode->AddTextureParameterValue(InputName.ToString() + TEXT("Map"), TextureNode->GetUniqueID());

				MaterialInstanceNode->AddScalarParameterValue(InputName.ToString() + TEXT("MapWeight"), Factor);

				// #ufbx_todo: Handle texture transform
				// Current material asset used as parent of the material instance does not handle UV transform yet.
				// Note that handling of UV transform should also be added to FBX SDK based parser
				// Begin of future handling of UV transform
				//if (Texture->has_uv_transform)
				//{
				//	bHasUVTransform = true;
				//	const ufbx_transform& UVTransform = Texture->uv_transform;
				//	FVector4f OffsetScale(
				//		UVTransform.translation.x, UVTransform.translation.y, 
				//		UVTransform.scale.x, UVTransform.scale.y);
				//	MaterialInstanceNode->AddVectorParameterValue(InputName.ToString() + TEXT("Map") + Inputs::PostFix::OffsetScale, OffsetScale);

				//	FString RotationParameterName = InputName.ToString() + TEXT("Map") + Inputs::PostFix::Rotation;
				//	Math::TVector<double> AttributeValue = Convert::ConvertQuat(UVTransform.rotation).Euler();
				//	MaterialInstanceNode->AddScalarParameterValue(RotationParameterName, FMath::DegreesToRadians(AttributeValue.Z));
				//}
				// End of future handling of UV transform

				return true;
			};

			bool bHasInput = false;

			// Diffuse
			if (Material->fbx.diffuse_color.has_value)
			{
				FName InputName = Phong::Parameters::DiffuseColor;
				const float Factor = GET_VALLUE_FROM_FACTOR_MAP(Material->fbx.diffuse_factor);
				const ufbx_material_map& Map = Material->fbx.diffuse_color;

				bHasInput |= SetTextureFromMap(InputName, Map, Factor) ? true : SetColorFromMap(InputName, Map, Factor);
			}

			// Ambient
			if (Material->fbx.ambient_color.has_value)
			{
				FName InputName = Phong::Parameters::AmbientColor;
				const float Factor = GET_VALLUE_FROM_FACTOR_MAP(Material->fbx.ambient_factor);
				const ufbx_material_map& Map = Material->fbx.ambient_color;

				bHasInput |= SetTextureFromMap(InputName, Map, Factor) ? true : SetColorFromMap(InputName, Map, Factor);
			}
			
			// Emissive
			if (Material->fbx.emission_color.has_value)
			{
				const FName InputName = Phong::Parameters::EmissiveColor;
				const float Factor = GET_VALLUE_FROM_FACTOR_MAP(Material->fbx.emission_factor);
				const ufbx_material_map& Map = Material->fbx.emission_color;

				bHasInput |= SetTextureFromMap(InputName, Map, Factor) ? true : SetColorFromMap(InputName, Map, Factor);
			}

			bool bIsPhong = false;

			// Specular
			if (Material->fbx.specular_color.has_value)
			{
				const FName InputName = Phong::Parameters::SpecularColor;
				const float Factor = GET_VALLUE_FROM_FACTOR_MAP(Material->fbx.specular_factor);
				const ufbx_material_map& Map = Material->fbx.specular_color;

				if (bool bHasSpecular = SetTextureFromMap(InputName, Map, Factor) ? true : SetColorFromMap(InputName, Map, Factor))
				{
					bHasInput = true;
					bIsPhong = true;
				}
			}

			// Shininess
			if (Material->fbx.specular_exponent.has_value)
			{
				const FName InputName = Phong::Parameters::Shininess;
				const float Shininess = Material->fbx.specular_exponent.value_real;
				const ufbx_material_map& Map = Material->fbx.specular_exponent;

				if (SetTextureFromMap(InputName, Map, 1.f))
				{
					bIsPhong |= MaterialInstanceNode->AddStaticSwitchParameterValue(TEXT("bHasShininessMap"), true);
				}
				else
				{
					bIsPhong |= SetScalarFromMap(InputName, Map, Shininess);
				}

				bHasInput |= bIsPhong;
			}

			// Normal
			if (Material->fbx.normal_map.has_value)
			{
				if (SetTextureFromMap(Phong::Parameters::Normal, Material->fbx.normal_map, 1.f))
				{
					bHasInput = true;
				}
				else if (SetTextureFromMap(Phong::Parameters::Normal, Material->fbx.bump, 1.f))
				{
					MaterialInstanceNode->AddStaticSwitchParameterValue(TEXT("bHasBump"), true);
					bHasInput = true;
				}
			}

			MaterialInstanceNode->AddStaticSwitchParameterValue(TEXT("bIsPhong"), bIsPhong);

			// Opacity
			// Connect only if transparency is either a texture or different from 0.f
			// Check fbx.transparency first...
			if (Material->fbx.transparency_color.has_value)
			{
				FName InputName = Phong::Parameters::Opacity;
				const ufbx_material_map& ColorMap = Material->fbx.transparency_color;
				const float Factor = GET_VALLUE_FROM_FACTOR_MAP(Material->fbx.transparency_factor);

				if (ColorMap.texture_enabled && ColorMap.texture)
				{
					FString HasOpacity{ TEXT("bHasOpacity") };
					EBlendMode BlendMode = EBlendMode::BLEND_Translucent;

					// The texture must be hooked to the OpacityMask when transparency_color.value_int is equal to 0
					if (ColorMap.value_int == 0)
					{
						InputName = Phong::Parameters::OpacityMask;
						BlendMode = EBlendMode::BLEND_Masked;
						MaterialInstanceNode->AddStaticSwitchParameterValue(TEXT("bHasOpacityMaskMap"), true);
						HasOpacity += TEXT("Mask");
					}

					HasOpacity += TEXT("Map");
					MaterialInstanceNode->AddStaticSwitchParameterValue(HasOpacity, true);
					MaterialInstanceNode->SetCustomBlendMode(BlendMode);
					
					bHasInput |= SetTextureFromMap(InputName, ColorMap, Factor);
				}
				else
				{
					const float TransparencyScalar = Factor * ColorMap.value_real;
					const float OpacityScalar = 1.f - FMath::Clamp(TransparencyScalar, 0.f, 1.f);
					if (OpacityScalar < 1.f)
					{
						bHasInput |= MaterialInstanceNode->AddScalarParameterValue(InputName.ToString(), OpacityScalar);
					}
				}
			}

			// If no valid property found, create a material anyway
			TArray<FString> InputNames;
			if (!bHasInput)
			{
				FLinearColor BaseColor;
				BaseColor.R = 0.7f;
				BaseColor.G = 0.7f;
				BaseColor.B = 0.7f;

				const FString InputValueKey = Phong::Parameters::DiffuseColor.ToString();
				MaterialInstanceNode->AddVectorParameterValue(InputValueKey, BaseColor);
			}

			// #ufbx_todo: ProcessCustomAttributes(Parser, SurfaceMaterial, MaterialInstanceNode);

			for (const ufbx_prop& Prop : Material->props.props)
			{
				if (Prop.flags & UFBX_PROP_FLAG_USER_DEFINED)
				{
					Parser.ConvertProperty(Prop, MaterialInstanceNode);
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
