// Copyright Epic Games, Inc. All Rights Reserved.

#include "UfbxParser.h"

#include "UfbxAnimation.h"
#include "UfbxConvert.h"
#include "UfbxMesh.h"
#include "UfbxMaterial.h"
#include "UfbxScene.h"

#include "Nodes/InterchangeSourceNode.h"

#if WITH_ENGINE
#include "Mesh/InterchangeMeshPayload.h"
#endif

#include "Fbx/InterchangeFbxMessages.h"
#include "InterchangeHelper.h"
#include "InterchangeCommonAnimationPayload.h"

#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryWriter.h"

#include "ufbx.c"
#include "Nodes/InterchangeUserDefinedAttribute.h"


#define LOCTEXT_NAMESPACE "InterchangeFbxParser"

namespace UE::Interchange::Private
{

FUfbxParser::FUfbxParser(TWeakObjectPtr<UInterchangeResultsContainer> InResultsContainer)
	: ResultsContainer(InResultsContainer)
{
}

FUfbxParser::~FUfbxParser()
{
	Reset();
}

FString FUfbxParser::GetElementNameRaw(const ufbx_element& Element) const
{
	return Convert::ToUnrealString(Element.name);
}

FString FUfbxParser::GetElementNameDeduplicated(const ufbx_element& Element, bool bIsJoint) const
{
	if (FString* Found = ElementToName.Find(&Element))
	{
		return *Found;
	}

	const FString BaseName = GetElementName(Element, bIsJoint);
	FString Name = BaseName;
	for (int32 Count = 0;;++Count)
	{
		if (!NameToElement.Contains(Name))
		{
			break;
		}
		Name = BaseName + TEXT("_ncl_") + FString::FromInt(Count+1);
	}
	NameToElement.Add(Name, &Element);
	ElementToName.Add(&Element, Name);
	return Name;
}

// inspired by UE::Interchange::Private::FFbxHelper::GetFbxObjectName
FString FUfbxParser::GetElementName(const ufbx_element& Element, bool bIsJoint) const
{
	FString ElementName = FUfbxParser::GetElementNameRaw(Element);
	UE::Interchange::SanitizeName(ElementName, bIsJoint);
	return ElementName;
}

// #ufbx_todo: see FFbxHelper::GetMeshName
FString FUfbxParser::GetMeshLabel(const ufbx_element& Mesh) const
{
	return UTF8_TO_TCHAR(Mesh.name.data);
}

FString FUfbxParser::GetNodeUid(const ufbx_node& Node) const
{
	// #ufbx_todo: check that element_id is enough
	// reference FBX parser's uses UE::Interchange::Private::FFbxHelper::GetFbxNodeHierarchyName to create NodeUid

	FString NodeUid = GetElementNameDeduplicated(Node.element);
	
	if (Node.parent)
	{
		const UInterchangeSceneNode*const* ParentSceneNodeFound = ElementIdToSceneNode.Find(Node.parent->element_id);
		// 
		if (ensureMsgf(ParentSceneNodeFound, TEXT("Expected to have child node Uid generated after parent was processed first")))
		{
			const UInterchangeSceneNode* ParentSceneNode = *ParentSceneNodeFound;
	
			return ParentSceneNode->GetUniqueID()+TEXT(".")+NodeUid;
		}
	}
	return NodeUid;
}

FString FUfbxParser::GetBoneNodeUid(const ufbx_node& Node) const
{
	// #ufbx_todo: need make sure not dup
	return GetElementNameDeduplicated(Node.element, true);
}

FName FUfbxParser::GetMaterialLabel(const ufbx_material& Material) const
{
	return *GetElementName(Material.element);
}

FString FUfbxParser::GetMaterialUid(const ufbx_material& Material) const
{
	// Since materials names are not unique use its ufbx type_id and keep original name in the Uid for simpler debugging
	return  FString::Printf(TEXT("\\Material\\%d_%s"), Material.element.typed_id, *GetElementNameRaw(Material.element));
}

FString FUfbxParser::GetMeshUid(const ufbx_element& Mesh) const
{
	return GetElementNameDeduplicated(Mesh);
}

void FUfbxParser::ConvertProperty(const ufbx_prop& Prop, UInterchangeBaseNode* SceneNode, const TOptional<FString>& PayloadKey)
{
	if (Prop.flags & UFBX_PROP_FLAG_USER_DEFINED)
	{
		FString PropertyName = Convert::ToUnrealString(Prop.name);

		switch (Prop.type)
		{
		case UFBX_PROP_UNKNOWN:
			break;
		case UFBX_PROP_BOOLEAN:
			{
				const bool PropertyValue = Prop.value_int != 0;
				UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(SceneNode, PropertyName, PropertyValue, PayloadKey);
			}
			break;
		case UFBX_PROP_INTEGER:
			{
				const int32 PropertyValue = Prop.value_int;
				UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(SceneNode, PropertyName, PropertyValue, PayloadKey);
			}
			break;
		case UFBX_PROP_NUMBER:
			{
				const float PropertyValue = Prop.value_real;
				UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(SceneNode, PropertyName, PropertyValue, PayloadKey);
			}
			break;
		case UFBX_PROP_VECTOR:
			{
				const FVector4d PropertyValue = Convert::ConvertVec4(Prop.value_vec4);
				UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(SceneNode, PropertyName, PropertyValue, PayloadKey);
			}
			break;
		case UFBX_PROP_COLOR:
			{
				const FVector PropertyValue = FVector(Convert::ConvertColor(Prop.value_vec4));
				UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(SceneNode, PropertyName, PropertyValue, PayloadKey);
			}
			break;
		case UFBX_PROP_COLOR_WITH_ALPHA:
			{
				const FLinearColor PropertyValue = Convert::ConvertColor(Prop.value_vec4);
				UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(SceneNode, PropertyName, PropertyValue, PayloadKey);
			}
			break;
		case UFBX_PROP_STRING:
			{
				const FString PropertyValue = Convert::ToUnrealString(Prop.value_str);
				UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(SceneNode, PropertyName, PropertyValue, PayloadKey);
			}
			break;
		case UFBX_PROP_DATE_TIME:
			{
				const FDateTime PropertyValue(Prop.value_int);
				UInterchangeUserDefinedAttributesAPI::CreateUserDefinedAttribute(SceneNode, PropertyName, PropertyValue, PayloadKey);
			}
			break;
		// #ufbx_todo: support other useful custom attributes among these types
		case UFBX_PROP_TRANSLATION:
			break;
		case UFBX_PROP_ROTATION:
			break;
		case UFBX_PROP_SCALING:
			break;
		case UFBX_PROP_DISTANCE:
			break;
		case UFBX_PROP_COMPOUND:
			break;
		case UFBX_PROP_BLOB:
			break;
		case UFBX_PROP_REFERENCE:
			break;
		case UFBX_PROP_TYPE_FORCE_32BIT:
			break;
		}
	}
}



void FUfbxParser::SetResultContainer(UInterchangeResultsContainer* Result)
{
	ResultsContainer = Result;
}

void FUfbxParser::Reset()
{
	NameToElement.Reset();
	ElementToName.Reset();

	ElementIdToSceneNode.Reset();

	PayloadContexts.Reset();

	ufbx_free_scene(Scene);
	Scene = nullptr;
}

bool FUfbxParser::LoadFbxFile(const FString& Filename, UInterchangeBaseNodeContainer& NodeContainer)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FUfbxParser::FillContainerWithFbxScene)

	SourceFilename = Filename;

	ufbx_load_opts LoadOpts = { 0 };
	LoadOpts.evaluate_skinning = false;
	LoadOpts.load_external_files = true;
	// this option allows to override root transform with provided LoadOpts.root_transform
	LoadOpts.use_root_transform = false;

	// #ufbx_todo: LoadOpts.allow_nodes_out_of_root= true; // might just to this(to reparent all out-of-root nodes under root_node) -
	// LoadOpts.allow_empty_faces = true;

	// #ufbx_todo: progress reporting. In case someday we want to report detailed progress from Worker/Parser
	// LoadOpts.progress_cb

	// #ufbx_todo: handle geometry transform - UInterchangeSceneNode::SetCustomGeometricTransform
	// LoadOpts.geometry_transform_handling

	// #ufbx_todo:
	// LoadOpts.inherit_mode_handling

	// #ufbx_todo:
	// LoadOpts.pivot_handling

	LoadOpts.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;

	LoadOpts.handedness_conversion_axis = UFBX_MIRROR_AXIS_Y;
	LoadOpts.target_axes = ufbx_axes_left_handed_z_up;
	LoadOpts.target_unit_meters = 0.01; // Centimeters
	LoadOpts.reverse_winding = true;

	ufbx_error Error;
	Scene = ufbx_load_file(TCHAR_TO_UTF8(*Filename), &LoadOpts, &Error);

	if (!Scene)
	{
		FFormatNamedArguments FilenameText
		{
			{ TEXT("Filename"), FText::FromString(Filename) }
		};
		UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
		Message->Text = FText::Format(LOCTEXT("CannotOpenFBXFile", "Cannot open FBX file '{Filename}'."), FilenameText);
		return false;
	}
	// #ufbx_todo: read scene converter to UE's

	UInterchangeSourceNode* SourceNode = UInterchangeSourceNode::FindOrCreateUniqueInstance(&NodeContainer);
	// #ufbx_todo:
	// SourceNode->SetCustomSourceFrameRateNumerator(FrameRate);
	// SourceNode->SetCustomSourceFrameRateDenominator(Denominator);

	// Fbx legacy has a special way to bake the skeletal mesh that do not fit the interchange standard
	// The interchange skeletal mesh factory will read this to use the proper bake transform so it match legacy behavior.
	// This fix the issue with blender armature bone skip
	SourceNode->SetCustomUseLegacySkeletalMeshBakeTransform(true);

	//Fbx legacy does not allow Scene Root Nodes to be part of the skeletons (to be joints).
	SourceNode->SetCustomAllowSceneRootAsJoint(false);

	// #ufbx_todo: FileDetails

	return true;
}

void FUfbxParser::FillContainerWithFbxScene(UInterchangeBaseNodeContainer& NodeContainer)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FUfbxParser::FillContainerWithFbxScene)

	FUfbxScene SceneConverter(*this);
	SceneConverter.InitHierarchy(NodeContainer);

	// #ufbx_todo: CleanupFbxData();
	FUfbxMaterial(*this).AddMaterials(NodeContainer);

	FUfbxMesh Meshes(*this);
	// Get meshes first, nodes reference them later in ProcessNodes
	Meshes.AddAllMeshes(NodeContainer);

	SceneConverter.ProcessNodes(NodeContainer);
	FUfbxAnimation::AddAnimation(*this, SceneConverter, Meshes, NodeContainer);
}

bool FUfbxParser::FetchPayloadData(const FString& PayloadKey, const FString& PayloadFilepath)
{
	// #ufbx_todo: defence - see UE::Interchange::Private::FFbxParser::FetchPayloadData

	const FPayloadContext* PayloadContextFound = PayloadContexts.Find(PayloadKey);
	if (!PayloadContextFound)
	{
		return false;
	}

	const FPayloadContext& PayloadContext = *PayloadContextFound;
	switch (PayloadContext.Kind)
	{
	case FPayloadContext::AnimationMorph:
		{
			FMorphAnimationPayloadContext& Animation = const_cast<FMorphAnimationPayloadContext&>(PayloadContexts.GetMorphAnimation(PayloadContext));

			TArray<FInterchangeCurve> InterchangeCurves;
			if (FUfbxAnimation::FetchMorphTargetAnimation(*this, Animation, InterchangeCurves))
			{
				TArray64<uint8> Buffer;
				FMemoryWriter64 Ar(Buffer);
				Ar << InterchangeCurves;
				Ar << Animation.InbetweenCurveNames;
				Ar << Animation.InbetweenFullWeights;
				FFileHelper::SaveArrayToFile(Buffer, *PayloadFilepath);
				return true;
			}
		}
		break;
	case FPayloadContext::AnimationRigid:
		{
			const FRigidAnimationPayloadContext& Animation = PayloadContexts.GetRigidAnimation(PayloadContext);

			TArray<FInterchangeCurve> InterchangeCurves;

			if (FUfbxAnimation::FetchRigidAnimation(*this, Animation, InterchangeCurves))
			{
				TArray64<uint8> Buffer;
				FMemoryWriter64 Ar(Buffer);
				Ar << InterchangeCurves;
				FFileHelper::SaveArrayToFile(Buffer, *PayloadFilepath);
				return true;
			}
		}
		break;
	case FPayloadContext::AnimationProperty:
		{
			const FPropertyAnimationPayloadContext& Animation = PayloadContexts.GetPropertyAnimation(PayloadContext);

			if (Animation.bIsStepAnimation)
			{
				TArray<FInterchangeStepCurve> InterchangeStepCurves;

				if (FUfbxAnimation::FetchPropertyAnimationStepCurves(*this, Animation, InterchangeStepCurves))
				{
					TArray64<uint8> Buffer;
					FMemoryWriter64 Ar(Buffer);
					Ar << InterchangeStepCurves;
					FFileHelper::SaveArrayToFile(Buffer, *PayloadFilepath);
					return true;
				}
			}
			else
			{
				TArray<FInterchangeCurve> InterchangeCurves;

				if (FUfbxAnimation::FetchPropertyAnimationCurves(*this, Animation, InterchangeCurves))
				{
					TArray64<uint8> Buffer;
					FMemoryWriter64 Ar(Buffer);
					Ar << InterchangeCurves;
					FFileHelper::SaveArrayToFile(Buffer, *PayloadFilepath);
					return true;
				}
				
			}
		}
		break;
	}
	
	return false;
}

bool FUfbxParser::FetchMeshPayloadData(const FString& PayloadKey, const FTransform& MeshGlobalTransform, const FString& PayloadFilepath)
{
	return false;	
}

#if WITH_ENGINE
bool FUfbxParser::FetchMeshPayloadData(const FString& PayloadKey, const FTransform& MeshGlobalTransform, FMeshPayloadData& OutMeshPayloadData)
{
	FMeshDescription& MeshDescription = OutMeshPayloadData.MeshDescription;

	if (FPayloadContext* Found = PayloadContexts.Find(PayloadKey))
	{
		switch (Found->Kind)
		{
		case FPayloadContext::Element:
			{
				ufbx_element* Element = Scene->elements[Found->Index];

				constexpr bool bDebugSpecificElement = false;

				if (bDebugSpecificElement)
				{
					uint32_t SpecificElement = 2096;
					if (Element->element_id != SpecificElement)
					{
						return false;
					}
				}

				if (Element->type == UFBX_ELEMENT_MESH)
				{
					return FUfbxMesh::FetchMesh(*this, MeshDescription, Element, MeshGlobalTransform);
				}
				else
				{
					ensure(false); // #ufbx_todo: implement
				}
			}
			break;
		case FPayloadContext::SkinnedMesh:
			{
				ufbx_element* Element = Scene->elements[Found->Index];
				
				return FUfbxMesh::FetchSkinnedMesh(*this, MeshDescription, Element, MeshGlobalTransform, OutMeshPayloadData.JointNames);
			}
			break;
		case FPayloadContext::Morph:
			{
				if (const FMorph* MorphPayload = PayloadContexts.GetMorph(Found))
				{
					return FUfbxMesh::FetchBlendShape(*this, MeshDescription, 
					   *Scene->meshes[MorphPayload->MeshElement],
					   *Scene->blend_shapes[MorphPayload->BlendShapeElement], 
					   MeshGlobalTransform);
				}
			}
			break;
		default: ;
		}
	}
	else
	{
		UInterchangeResultError_Generic* Message = AddMessage<UInterchangeResultError_Generic>();
		Message->Text = LOCTEXT("CannotRetrievePayload", "Cannot retrieve payload; payload key doesn't have any context.");
	}

	// need this?
	MeshDescription.Empty();
	return false;
}

bool FUfbxParser::FetchTexturePayload(const FString& PayloadKey, TOptional<TArray64<uint8>>& OutTexturePayloadData)
{
	if (FPayloadContext* Found = PayloadContexts.Find(PayloadKey))
	{
		if (!ensure(Found->Kind == FPayloadContext::Element))
		{
			return false;
		}

		ufbx_element* Element = Scene->elements[Found->Index];

		if (Element->type == UFBX_ELEMENT_TEXTURE)
		{
			const ufbx_texture* Texture = ufbx_as_texture(Element);
			if (!ensure(Texture))
			{
				return false;
			}
			if (Texture->content.data && (Texture->content.size > 0))
			{
				OutTexturePayloadData = TArray64<uint8>(static_cast<const uint8*>(Texture->content.data), Texture->content.size);
				return true;
			}
			return false;
		}

		AddMessage<UInterchangeResultError_Generic>()->Text = LOCTEXT("CannotRetrieveTexturePayloadNonTextureKey", "Cannot retrieve payload; FUfbxParser::FetchTexturePayload was called with non-texture payload key.");
	}

	return false;
}

#endif

bool FUfbxParser::FetchAnimationBakeTransformPayload(const TArray<UE::Interchange::FAnimationPayloadQuery>& PayloadQueries,
                                                     const FString& ResultFolder, FCriticalSection* ResultPayloadsCriticalSection, TAtomic<int64>& UniqueIdCounter,
                                                     TMap<FString, FString>& ResultPayloads)
{
	// #ufbx_todo: resolve concurrent access to ResultPayloads; or, maybe replace ResultPayloads with TQueue? Not clear why it's a map and why it's tested that keys already exist there...

	for (const FAnimationPayloadQuery& PayloadQuery : PayloadQueries)
	{
		FPayloadContext* PayloadContext = PayloadContexts.Find(PayloadQuery.PayloadKey.UniqueId);
		if (ensure(PayloadContext && (PayloadContext->Kind == FPayloadContext::AnimationSkinned)))
		{
			const FSkeletalAnimationPayloadContext& SkeletalAnimationPayloadContext = PayloadContexts.GetAnimation(*PayloadContext);
			FAnimationPayloadData PayloadData = FAnimationPayloadData(SkeletalAnimationPayloadContext.NodeUid, PayloadQuery.PayloadKey);
			FUfbxAnimation::FetchSkinnedAnimation(PayloadQuery, SkeletalAnimationPayloadContext, PayloadData);

			FString QueryHashString = PayloadQuery.GetHashString();

			// #ufbx_todo: check, FBX SDK parser somehow checks that ResultPayloads already has QueryHashString(i.e. same query already processed) Why is this?
			FString PayloadFilepathCopy;
			{
				// #ufbx_todo: guard concurrent access to ResultPayloads, see UE::Interchange::Private::FFbxAnimation::FetchAnimationBakeTransformPayload

				FString& PayloadFilepath = ResultPayloads.FindOrAdd(QueryHashString);
				//To avoid file path with too many character, we hash the payloadKey so we have a deterministic length for the file path.
				PayloadFilepath = ResultFolder + TEXT("/") + QueryHashString + FString::FromInt(UniqueIdCounter.IncrementExchange()) + TEXT(".payload");

				//Copy the map filename key because we are multithreaded and the TMap can be reallocated
				PayloadFilepathCopy = PayloadFilepath;

			}
			FString PayloadFilepath = PayloadFilepathCopy;

			TArray64<uint8> Buffer;
			FMemoryWriter64 Ar(Buffer);
			PayloadData.SerializeBaked(Ar);
			FFileHelper::SaveArrayToFile(Buffer, *PayloadFilepath);
		}
	}

	return false;
}

}

#undef LOCTEXT_NAMESPACE

