// Copyright Epic Games, Inc. All Rights Reserved.

#include "UfbxAnimation.h"
#include "UfbxParser.h"
#include "UfbxScene.h"
#include "UfbxConvert.h"

#include "InterchangeCommonAnimationPayload.h"
#include "InterchangeMeshNode.h"
#include "UfbxMesh.h"
#include "Algo/AnyOf.h"
#include "Nodes/InterchangeBaseNodeContainer.h"

#define LOCTEXT_NAMESPACE "InterchangeFbxParser"

namespace UE::Interchange::Private
{

	namespace AnimUtils
	{

		template<typename ValueList>
		auto GetAnimationKeyValue(const ValueList& Keys, const bool& IsConstant, const int32 TransformIndex, const double& Frequency) -> decltype(Keys[0].value)
		{
			// #ufbx_todo: investigate key start time handling, e.g. TransformIndex-FMath::CeilToInt(Keys[0].time*Frequency - UE_KINDA_SMALL_NUMBER)
			int32 KeyIndex = IsConstant ? 0
			: FMath::Clamp<int32>(TransformIndex, 0, Keys.count-1);

			return Keys[KeyIndex].value;
		};

		// inspired by UE::Interchange::Private::ImportCurve - Engine/Plugins/Interchange/Runtime/Source/Parsers/Fbx/Private/FbxAnimation.cpp
		void ConvertCurve(const ufbx_anim_curve& SourceFloatCurves, const float ScaleValue, TArray<FInterchangeCurveKey>& DestinationFloatCurve)
		{
			const float DefaultCurveWeight = FbxAnimCurveDef::sDEFAULT_WEIGHT;
			
			int32 KeyCount = SourceFloatCurves.keyframes.count;
			for (int32 KeyIndex = 0; KeyIndex < KeyCount; ++KeyIndex)
			{
				
				const ufbx_keyframe& Key = SourceFloatCurves.keyframes[KeyIndex];
				
				const float KeyTimeValue = static_cast<float>(Key.time);
				const float Value = Key.value * ScaleValue;
				FInterchangeCurveKey& InterchangeCurveKey = DestinationFloatCurve.AddDefaulted_GetRef();
				InterchangeCurveKey.Time = KeyTimeValue;
				InterchangeCurveKey.Value = Value;

				const bool bIncludeOverrides = true;

				// #ufbx_todo: 
				// FbxAnimCurveDef::EWeightedMode KeyTangentWeightMode = Key.GetTangentWeightMode();

				EInterchangeCurveInterpMode NewInterpMode = EInterchangeCurveInterpMode::Linear;
				EInterchangeCurveTangentMode NewTangentMode = EInterchangeCurveTangentMode::Auto;
				EInterchangeCurveTangentWeightMode NewTangentWeightMode = EInterchangeCurveTangentWeightMode::WeightedNone;

				//#ufbx_todo:  tangents - investigate, implement and refine how tangents are done in ufbx compared to FBX SDK
				float RightTangent = [Key, ScaleValue]
				{
					if (FMath::Abs(Key.right.dx) > UE_SMALL_NUMBER)
					{
						return Key.right.dy/Key.right.dx * ScaleValue;
					}

					return 0.f;
				}();

				float LeftTangent = [Key, ScaleValue]
				{
					if (FMath::Abs(Key.left.dx) > UE_SMALL_NUMBER)
					{
						 return Key.left.dy/Key.left.dx * ScaleValue; 
					}

					return 0.f;
				}();

				float RightTangentWeight = 0.0f;
				float LeftTangentWeight = 0.0f; //This one is dependent on the previous key.
				bool bLeftWeightActive = false;
				bool bRightWeightActive = false;

				const bool bPreviousKeyValid = KeyIndex > 0;
				const bool bNextKeyValid = KeyIndex < KeyCount - 1;
				float PreviousValue = 0.0f;
				float PreviousKeyTimeValue = 0.0f;
				float NextValue = 0.0f;
				float NextKeyTimeValue = 0.0f;
				if (bPreviousKeyValid)
				{
					const ufbx_keyframe& PreviousKey = SourceFloatCurves.keyframes[KeyIndex - 1];
					FbxTime PreviousKeyTime = PreviousKey.time;
					PreviousKeyTimeValue = static_cast<float>(PreviousKeyTime.GetSecondDouble());
					PreviousValue = PreviousKey.value * ScaleValue;
					//The left tangent is driven by the previous key. If the previous key have a the NextLeftweight or both flag weighted mode, it mean the next key is weighted on the left side

					// #ufbx_todo:
					// bLeftWeightActive = (PreviousKey.GetTangentWeightMode() & FbxAnimCurveDef::eWeightedNextLeft) > 0;
					// if (bLeftWeightActive)
					// {
					// 	LeftTangentWeight = PreviousKey.GetDataFloat(FbxAnimCurveDef::eNextLeftWeight);
					// }
				}
				if (bNextKeyValid)
				{
					const ufbx_keyframe& NextKey = SourceFloatCurves.keyframes[KeyIndex + 1];
					FbxTime NextKeyTime = NextKey.time;
					NextKeyTimeValue = static_cast<float>(NextKeyTime.GetSecondDouble());
					NextValue = NextKey.value * ScaleValue;

					// #ufbx_todo:
					// bRightWeightActive = (KeyTangentWeightMode & FbxAnimCurveDef::eWeightedRight) > 0;
					if (bRightWeightActive)
					{
						//The right tangent weight should be use only if we are not the last key since the last key do not have a right tangent.
						//Use the current key to gather the right tangent weight
						// #ufbx_todo: 
						// RightTangentWeight = Key.GetDataFloat(FbxAnimCurveDef::eRightWeight);
					}
				}

				// When this flag is true, the tangent is flat if the value has the same value as the previous or next key.
				// FbxAnimCurveDef::ETangentMode KeyTangentMode = Key.GetTangentMode(bIncludeOverrides);
				const bool bTangentGenericClamp = false; //(KeyTangentMode & FbxAnimCurveDef::eTangentGenericClamp);
				//Time independent tangent this is consider has a spline tangent key
				const bool bTangentGenericTimeIndependent = true; //(KeyTangentMode & FbxAnimCurveDef::ETangentMode::eTangentGenericTimeIndependent);
				// When this flag is true, the tangent is flat if the value is outside of the [previous key, next key] value range.
				//Clamp progressive is (eTangentGenericClampProgressive |eTangentGenericTimeIndependent)
				const bool bTangentGenericClampProgressive = true; //(KeyTangentMode & FbxAnimCurveDef::ETangentMode::eTangentGenericClampProgressive) == FbxAnimCurveDef::ETangentMode::eTangentGenericClampProgressive;
				// if (KeyTangentMode & FbxAnimCurveDef::eTangentGenericBreak)
				// {
				// 	NewTangentMode = EInterchangeCurveTangentMode::Break;
				// }
				// else if (KeyTangentMode & FbxAnimCurveDef::eTangentUser)
				// {
				// 	NewTangentMode = EInterchangeCurveTangentMode::User;
				// }

				
				switch (Key.interpolation)
				{
					// #ufbx_todo: distinguish two const interps
				case UFBX_INTERPOLATION_CONSTANT_NEXT://! Constant value until next key.
				case UFBX_INTERPOLATION_CONSTANT_PREV://! Constant value until next key.
					NewInterpMode = EInterchangeCurveInterpMode::Constant;
					break;
				case UFBX_INTERPOLATION_LINEAR://! Linear progression to next key.
					NewInterpMode = EInterchangeCurveInterpMode::Linear;
					break;
				case UFBX_INTERPOLATION_CUBIC://! Cubic progression to next key.
					NewInterpMode = EInterchangeCurveInterpMode::Cubic;
					// get tangents
					{
						bool bIsFlatTangent = false;
						bool bIsComputedTangent = false;
						if (bTangentGenericClampProgressive)
						{
							if (bPreviousKeyValid && bNextKeyValid)
							{
								const float PreviousNextHalfDelta = (NextValue - PreviousValue) * 0.5f;
								const float PreviousNextAverage = PreviousValue + PreviousNextHalfDelta;
								// If the value is outside of the previous-next value range, the tangent is flat.
								bIsFlatTangent = FMath::Abs(Value - PreviousNextAverage) >= FMath::Abs(PreviousNextHalfDelta);
							}
							else
							{
								//Start/End tangent with the ClampProgressive flag are flat.
								bIsFlatTangent = true;
							}
						}
						else if (bTangentGenericClamp && (bPreviousKeyValid || bNextKeyValid))
						{
							if (bPreviousKeyValid && PreviousValue == Value)
							{
								bIsFlatTangent = true;
							}
							if (bNextKeyValid)
							{
								bIsFlatTangent |= Value == NextValue;
							}
						}
						else if (bTangentGenericTimeIndependent)
						{
							//Spline tangent key, because bTangentGenericClampProgressive include bTangentGenericTimeIndependent, we must treat this case after bTangentGenericClampProgressive
							if (KeyCount == 1)
							{
								bIsFlatTangent = true;
							}
							else
							{
								//Spline tangent key must be User mode since we want to keep the tangents provide by the fbx key left and right derivatives
								NewTangentMode = EInterchangeCurveTangentMode::User;
							}
						}

						if (bIsFlatTangent)
						{
							RightTangent = 0;
							LeftTangent = 0;
							//To force flat tangent we need to set the tangent mode to user
							NewTangentMode = EInterchangeCurveTangentMode::User;
						}

					}
					break;
				}

				//auto with weighted give the wrong result, so when auto is weighted we set user mode and set the Right tangent equal to the left tangent.
				//Auto has only the left tangent set
				if (NewTangentMode == EInterchangeCurveTangentMode::Auto && (bLeftWeightActive || bRightWeightActive))
				{

					NewTangentMode = EInterchangeCurveTangentMode::User;
					RightTangent = LeftTangent;
				}

				if (NewTangentMode != EInterchangeCurveTangentMode::Auto)
				{
					const bool bEqualTangents = FMath::IsNearlyEqual(LeftTangent, RightTangent);
					//If tangents are different then broken.
					if (bEqualTangents)
					{
						NewTangentMode = EInterchangeCurveTangentMode::User;
					}
					else
					{
						NewTangentMode = EInterchangeCurveTangentMode::Break;
					}
				}

				//Only cubic interpolation allow weighted tangents
				if (Key.interpolation == UFBX_INTERPOLATION_CUBIC)
				{
					if (bLeftWeightActive && bRightWeightActive)
					{
						NewTangentWeightMode = EInterchangeCurveTangentWeightMode::WeightedBoth;
					}
					else if (bLeftWeightActive)
					{
						NewTangentWeightMode = EInterchangeCurveTangentWeightMode::WeightedArrive;
						RightTangentWeight = DefaultCurveWeight;
					}
					else if (bRightWeightActive)
					{
						NewTangentWeightMode = EInterchangeCurveTangentWeightMode::WeightedLeave;
						LeftTangentWeight = DefaultCurveWeight;
					}
					else
					{
						NewTangentWeightMode = EInterchangeCurveTangentWeightMode::WeightedNone;
						LeftTangentWeight = DefaultCurveWeight;
						RightTangentWeight = DefaultCurveWeight;
					}

					auto ComputeWeightInternal = [](float TimeA, float TimeB, const float TangentSlope, const float TangentWeight)
					{
						const float X = TimeA - TimeB;
						const float Y = TangentSlope * X;
						return FMath::Sqrt(X * X + Y * Y) * TangentWeight;
					};

					if (!FMath::IsNearlyZero(LeftTangentWeight))
					{
						if (bPreviousKeyValid)
						{
							LeftTangentWeight = ComputeWeightInternal(KeyTimeValue, PreviousKeyTimeValue, LeftTangent, LeftTangentWeight);
						}
						else
						{
							LeftTangentWeight = 0.0f;
						}
					}

					if (!FMath::IsNearlyZero(RightTangentWeight))
					{
						if (bNextKeyValid)
						{
							RightTangentWeight = ComputeWeightInternal(NextKeyTimeValue, KeyTimeValue, RightTangent, RightTangentWeight);
						}
						else
						{
							RightTangentWeight = 0.0f;
						}
					}
				}

				const bool bForceDisableTangentRecompute = false; //No need to recompute all the tangents of the curve every time we change de key.
				InterchangeCurveKey.InterpMode = NewInterpMode;
				InterchangeCurveKey.TangentMode = NewTangentMode;
				InterchangeCurveKey.TangentWeightMode = NewTangentWeightMode;

				InterchangeCurveKey.ArriveTangent = LeftTangent;
				InterchangeCurveKey.LeaveTangent = RightTangent;
				InterchangeCurveKey.ArriveTangentWeight = LeftTangentWeight;
				InterchangeCurveKey.LeaveTangentWeight = RightTangentWeight;
			}
		}

		// UE::Interchange::Private::CreateTrackNodeUid
		FString CreateTrackNodeUid(const FString& JointUid, const int32 AnimationIndex)
		{
			FString TrackNodeUid = TEXT("\\SkeletalAnimation\\") + JointUid + TEXT("\\") + FString::FromInt(AnimationIndex);
			return TrackNodeUid;
		}

		template<typename T>
		void ConvertStepCurveKeyframes(const ufbx_anim_curve& Curve, TArray<float>& OutTimes, TOptional<TArray<T>>& OutValues)
		{
			const int32 Count = Curve.keyframes.count;
			OutTimes.Reserve(Count);
			TArray<T>& Values = OutValues.Emplace();
			Values.Reserve(Count);

			for (const ufbx_keyframe& Keyframe : Curve.keyframes)
			{
				OutTimes.Add(static_cast<float>(Keyframe.time));
				Values.Add(static_cast<T>(Keyframe.value));
			}
		}

	}

	void FUfbxAnimation::FetchSkinnedAnimation(const UE::Interchange::FAnimationPayloadQuery& PayloadQuery, const FSkeletalAnimationPayloadContext& SkeletalAnimationPayload, FAnimationPayloadData& PayloadData)
	{
		ufbx_shared_ptr<ufbx_baked_anim> BakedAnim = SkeletalAnimationPayload.BakedAnim;
		const ufbx_baked_node& BakedNode = BakedAnim->nodes[SkeletalAnimationPayload.BakedNodeIndex];

		double BakeFrequency = PayloadQuery.TimeDescription.BakeFrequency;

		PayloadData.BakeFrequency = BakeFrequency;
		PayloadData.RangeStartTime = PayloadQuery.TimeDescription.RangeStartSecond;
		PayloadData.RangeEndTime = PayloadQuery.TimeDescription.RangeStopSecond;

		const double AnimationDuration = PayloadQuery.TimeDescription.RangeStopSecond - PayloadQuery.TimeDescription.RangeStartSecond;
		int32 TotalKeyCount = FMath::RoundToInt32(AnimationDuration*BakeFrequency) + 1;
		PayloadData.Transforms.SetNum(TotalKeyCount);

		// Make transform from animation key index, 
		auto MakeTransform = [&BakedNode, BakeFrequency](const int32 TransformIndex)
		{
			FTransform Transform;
			FVector Translation = Convert::ConvertVec3(AnimUtils::GetAnimationKeyValue(BakedNode.translation_keys, BakedNode.constant_translation, TransformIndex, BakeFrequency));
			FQuat Rotation = Convert::ConvertQuat(AnimUtils::GetAnimationKeyValue(BakedNode.rotation_keys, BakedNode.constant_rotation, TransformIndex, BakeFrequency));
			FVector Scale = Convert::ConvertVec3(AnimUtils::GetAnimationKeyValue(BakedNode.scale_keys, BakedNode.constant_scale, TransformIndex, BakeFrequency));

			Transform.SetTranslation(Translation);
			Transform.SetRotation(Rotation);
			Transform.SetScale3D(Scale);
			return Transform;
		};

		for (int TransformIndex = 0; TransformIndex < TotalKeyCount; ++TransformIndex)
		{
			PayloadData.Transforms[TransformIndex] = MakeTransform(TransformIndex);
		}
	}

	bool FUfbxAnimation::FetchMorphTargetAnimation(const FUfbxParser& Parser, const FMorphAnimationPayloadContext& Animation, TArray<FInterchangeCurve>& OutCurves)
	{
		if (Animation.Curve)
		{
			// #ufbx_todo: UE::Interchange::Private::FAnimationPayloadContext::InternalFetchMorphTargetCurvePayload uses 0.01f scale
			AnimUtils::ConvertCurve(*Animation.Curve, 0.01f, OutCurves.AddDefaulted_GetRef().Keys);
			return true;
		}
		
		return false;
	}

	bool FUfbxAnimation::FetchRigidAnimation(const FUfbxParser& Parser, const FRigidAnimationPayloadContext& Animation,
		TArray<FInterchangeCurve>& OutCurves)
	{
		auto AddCurve = [&OutCurves](const ufbx_anim_prop* Prop, int32 CurveIndex, double Scale=1.0)
		{
			const ufbx_anim_curve* Curve =  Prop ? Prop->anim_value->curves[CurveIndex] : nullptr;
			if (Curve)
			{
				AnimUtils::ConvertCurve(*Curve, Scale, OutCurves.AddDefaulted_GetRef().Keys);
			}
			else
			{
				OutCurves.AddDefaulted();
			}
		};

		OutCurves.Reserve(9);

		// #ufbx_todo: FAnimationPayloadContext::InternalFetchCurveNodePayloadToFile does some pivot handling, see ResetPivotsPrePostRotationsAndSetRotationOrder

		AddCurve(Animation.TranslationProp, 0);
		AddCurve(Animation.TranslationProp, 2, -1.0);
		AddCurve(Animation.TranslationProp, 1);
		AddCurve(Animation.RotationProp, 0);
		AddCurve(Animation.RotationProp, 1);
		AddCurve(Animation.RotationProp, 2);
		AddCurve(Animation.ScalingProp, 0);
		AddCurve(Animation.ScalingProp, 2);
		AddCurve(Animation.ScalingProp, 1);

		return true;
	}

	bool FUfbxAnimation::FetchPropertyAnimationCurves(const FUfbxParser& Parser, const FPropertyAnimationPayloadContext& Animation, TArray<FInterchangeCurve>& OutCurves)
	{
		for (const ufbx_anim_curve* Curve : Animation.AnimValue->curves)
		{
			if (Curve)
			{
				AnimUtils::ConvertCurve(*Curve, 1.f, OutCurves.AddDefaulted_GetRef().Keys);
			}
		}
		return true;
	}

	bool FUfbxAnimation::FetchPropertyAnimationStepCurves(const FUfbxParser& Parser, const FPropertyAnimationPayloadContext& Animation, TArray<FInterchangeStepCurve>& OutCurves)
	{
		for (const ufbx_anim_curve* Curve : Animation.AnimValue->curves)
		{
			if (Curve)
			{
				

				switch (Animation.Prop->type)
				{
				case UFBX_PROP_BOOLEAN:
					{
						FInterchangeStepCurve& DestinationCurve = OutCurves.AddDefaulted_GetRef();
						AnimUtils::ConvertStepCurveKeyframes(*Curve, DestinationCurve.KeyTimes, DestinationCurve.BooleanKeyValues);
					}
					break;
				case UFBX_PROP_INTEGER:
					{
						FInterchangeStepCurve& DestinationCurve = OutCurves.AddDefaulted_GetRef();
						AnimUtils::ConvertStepCurveKeyframes(*Curve, DestinationCurve.KeyTimes, DestinationCurve.IntegerKeyValues);
					}
					break;
				case UFBX_PROP_NUMBER:
					{
						FInterchangeStepCurve& DestinationCurve = OutCurves.AddDefaulted_GetRef();
						AnimUtils::ConvertStepCurveKeyframes(*Curve, DestinationCurve.KeyTimes, DestinationCurve.FloatKeyValues);
					}
					break;
				}

			}
		}
		return true;
	}

	void FUfbxAnimation::AddAnimation(FUfbxParser& Parser, FUfbxScene& Scene, const FUfbxMesh& Meshes, UInterchangeBaseNodeContainer& NodeContainer)
	{
		const TMap<const ufbx_blend_shape*, const ufbx_mesh*> MeshPerBlendShape = Meshes.GetBlendShapes();
		double FrameRate = Parser.Scene->settings.frames_per_second;

		TArray<FString> TransformAnimTrackNodeUids;

		int32 AnimStackCount = Parser.Scene->anim_stacks.count;
		for (int32 AnimStackIndex = 0; AnimStackIndex < AnimStackCount; ++AnimStackIndex)
		{
			ufbx_anim_stack* Stack = Parser.Scene->anim_stacks[AnimStackIndex];

			// keep track nodes for current AnimStackIndex only (not all TrackNodes!)
			TMap<FString, UInterchangeSkeletalAnimationTrackNode*> TrackNodePerSkeletonRoot;

			ufbx_bake_opts BakeOptions = {};
			BakeOptions.maximum_sample_rate = FrameRate;
			BakeOptions.minimum_sample_rate = FrameRate;
			BakeOptions.resample_rate = FrameRate;

			ufbx_error ErrorResult;

			ufbx_shared_ptr<ufbx_baked_anim> BakedAnim(ufbx_bake_anim(Parser.Scene, Stack->anim, &BakeOptions, &ErrorResult));
			if (!BakedAnim)
			{
				UInterchangeResultError_Generic* Message = Parser.AddMessage<UInterchangeResultError_Generic>();

				TArray<char> ErrorBuf;
				ErrorBuf.SetNum(ErrorResult.info_length+64);
				ufbx_format_error(ErrorBuf.GetData(), ErrorBuf.Num(), &ErrorResult);

				Message->Text = FText::Format(LOCTEXT("AnimBakeError", "UFBX couldn't bake animation: '{0}'."), FText::FromString(UTF8_TO_TCHAR(ErrorBuf.GetData())));
				continue;
			}

			TConstArrayView<ufbx_baked_node> BakedNodes(BakedAnim->nodes.data, BakedAnim->nodes.count);

			TConstArrayView<ufbx_node*> FbxNodes(Parser.Scene->nodes.data, Parser.Scene->nodes.count);

			FString DisplayString = Convert::ToUnrealString(Stack->name);

			auto GetTrackNode = [&](FString SkeletonRootNodeUid)
			{
				UInterchangeSkeletalAnimationTrackNode*& SkeletalAnimationTrackNode = TrackNodePerSkeletonRoot.FindOrAdd(SkeletonRootNodeUid);
				if (!SkeletalAnimationTrackNode)
				{
					FString TrackNodeUid = AnimUtils::CreateTrackNodeUid(SkeletonRootNodeUid, AnimStackIndex);

					SkeletalAnimationTrackNode = NewObject< UInterchangeSkeletalAnimationTrackNode >(&NodeContainer);
					SkeletalAnimationTrackNode->InitializeNode(TrackNodeUid, DisplayString, EInterchangeNodeContainerType::TranslatedAsset);
					SkeletalAnimationTrackNode->AddBooleanAttribute(TEXT("RenameLikeLegacyFbx"), true);
					SkeletalAnimationTrackNode->SetCustomAnimationSampleRate(Parser.Scene->settings.frames_per_second);

					SkeletalAnimationTrackNode->SetCustomSkeletonNodeUid(SkeletonRootNodeUid);
				
					SkeletalAnimationTrackNode->SetCustomAnimationStartTime(Stack->anim->time_begin);
					SkeletalAnimationTrackNode->SetCustomAnimationStopTime(Stack->anim->time_end);

					SkeletalAnimationTrackNode->SetCustomSourceTimelineAnimationStartTime(Stack->time_begin);
					SkeletalAnimationTrackNode->SetCustomSourceTimelineAnimationStopTime(Stack->time_end);
					NodeContainer.AddNode(SkeletalAnimationTrackNode);
				}
				return SkeletalAnimationTrackNode;
			};

			for (int32 BakedNodeIndex = 0; BakedNodeIndex < BakedNodes.Num(); ++BakedNodeIndex)
			{
				const ufbx_baked_node& BakedNode = BakedNodes[BakedNodeIndex];

				if (!ensureMsgf(FbxNodes.IsValidIndex(BakedNode.typed_id), TEXT("Unexpected type_id encountered in ufbx baked anim nodes")))
				{
					continue;
				}

				ufbx_node* Node = FbxNodes[BakedNode.typed_id];
				if (Node->bone)
				{
					FString NodeUid = Parser.GetNodeUid(*Node);

					UInterchangeSkeletalAnimationTrackNode* SkeletalAnimationTrackNode = GetTrackNode(Scene.GetSkeletonRoot(Node));

					// #ufbx_todo: Need check animated parents - whole subtree has to be animated too?
					// see UE::Interchange::Private::FFbxAnimation::AddSkeletalTransformAnimation
					// or maybe there's an option in ufbx baking that already does that?

					FString PayLoadKey = NodeUid + TEXT("_") + FString::FromInt(AnimStackIndex) + TEXT("_SkeletalAnimationPayloadKey");

					Parser.PayloadContexts.Add(PayLoadKey, FSkeletalAnimationPayloadContext{NodeUid, AnimStackIndex, BakedNodeIndex, BakedAnim});

					SkeletalAnimationTrackNode->SetAnimationPayloadKeyForSceneNodeUid(NodeUid, PayLoadKey, EInterchangeAnimationPayLoadType::BAKED);
				}

			}

			// Collect curves for other animated properties - morph, ..
			for (int32 LayerIndex = 0; LayerIndex < Stack->layers.count; ++LayerIndex)
			{
				ufbx_anim_layer* Layer = Stack->layers[LayerIndex];

				TMap<const ufbx_node*, FRigidAnimationPayloadContext> TransformationPayloadContexts;

				for (int32 AnimPropIndex = 0; AnimPropIndex < Layer->anim_props.count; ++AnimPropIndex)
				{
					const ufbx_anim_prop& Prop = Layer->anim_props[AnimPropIndex];

					// Morph animation is DeformPercent
					if (FCStringAnsi::Strcmp(UFBX_DeformPercent, Prop.prop_name.data)==0)
					{
						if (ufbx_blend_channel* Channel = ufbx_as_blend_channel(Prop.element))
						{
							ufbx_blend_shape* TargetShape = Channel->target_shape;

							ufbx_anim_curve* MorphCurve = Prop.anim_value->curves[0];

							Convert::FMeshNameAndUid MorphTargetNameId(Parser, *TargetShape);

							// from UE::Interchange::Private::FFbxAnimation::AddMorphTargetCurvesAnimation
							// #ufbx_todo: do we need different PayloadKey for same curve used to animate different meshes?
							// (MorphTargetAnimationBuildingData.InterchangeMeshNode ? MorphTargetAnimationBuildingData.InterchangeMeshNode->GetUniqueID() : FString()) //Same shape can be animated on different mesh node
							// If not - maybe we could just create key based on curve's global(within scene) index - ufbx_scene::anim_curves?

							FString PayloadKey = MorphTargetNameId.UniqueID
								+ TEXT("\\") + FString::FromInt(AnimStackIndex)
								+ TEXT("\\") + FString::FromInt(LayerIndex)
								+ TEXT("\\") + FString::FromInt(AnimPropIndex)
								+ TEXT("_CurveAnimationPayloadKey");

							const ufbx_mesh*const* MeshFound = MeshPerBlendShape.Find(TargetShape);

							const ufbx_mesh* Mesh = *MeshFound;
							TSet<UInterchangeSkeletalAnimationTrackNode*> Tracks;

							const UInterchangeMeshNode*const* MeshNodeFound = Meshes.MeshToMeshNode.Find(Mesh);
							if (MeshNodeFound)
							{
								const UInterchangeMeshNode* MeshNode = *MeshNodeFound;

								if (MeshNode->IsSkinnedMesh())
								{
									for (ufbx_skin_deformer* Deformer : Mesh->skin_deformers)
									{
										for (ufbx_skin_cluster* Cluster : Deformer->clusters)
										{
											Tracks.Add(GetTrackNode(Scene.GetSkeletonRoot(Cluster->bone_node)));
										}
									}
								}
								else
								{
									//Find MeshInstances: where CustomAssetInstanceUid == MeshNode->GetUniqueID
									// For every occurence create a Track node MeshNode->GetUniqueID
									// #ufbx_todo: maybe better to keep map of instances per mesh, instead of iterating...
									TSet<UInterchangeSceneNode*> SkeletonNodes;
									NodeContainer.IterateNodesOfType<UInterchangeSceneNode>([&](const FString& NodeUid, UInterchangeSceneNode* SceneNode)
										{
											FString AssetInstanceUid;
											if (SceneNode->GetCustomAssetInstanceUid(AssetInstanceUid) && AssetInstanceUid == MeshNode->GetUniqueID())
											{
												
												SkeletonNodes.Add(SceneNode);
											}
										});
									// Make track nodes separately to prevent modifying NodeContainer while iterating it
									for (UInterchangeSceneNode* SceneNode : SkeletonNodes)
									{
										Tracks.Add(GetTrackNode(SceneNode->GetUniqueID()));
									}
								}
							}

							for (UInterchangeSkeletalAnimationTrackNode* Track : Tracks)
							{
								Track->SetAnimationPayloadKeyForMorphTargetNodeUid(MorphTargetNameId.UniqueID, PayloadKey, EInterchangeAnimationPayLoadType::MORPHTARGETCURVE);
							}

							FMorphAnimationPayloadContext MorphPayloadContext;
							MorphPayloadContext.Curve = MorphCurve;

							for (const ufbx_blend_keyframe& Keyframe : Channel->keyframes)
							{
								MorphPayloadContext.InbetweenFullWeights.Add(Keyframe.target_weight);
								MorphPayloadContext.InbetweenCurveNames.Add(Parser.GetElementNameDeduplicated(Keyframe.shape->element));
							}

							Parser.PayloadContexts.Add(PayloadKey, MorphPayloadContext);
						}
					}

					const ufbx_node* Node = ufbx_as_node(Prop.element);
					// #ufbx_todo: ignore bones? Btw, why bones are not here?
					if (Node && !Node->bone)
					{
						if (FCStringAnsi::Strcmp(UFBX_Lcl_Translation, Prop.prop_name.data)==0)
						{
							TransformationPayloadContexts.FindOrAdd(Node).TranslationProp = &Prop;
						}
						if (FCStringAnsi::Strcmp(UFBX_Lcl_Rotation, Prop.prop_name.data)==0)
						{
							TransformationPayloadContexts.FindOrAdd(Node).RotationProp = &Prop;
						}
						if (FCStringAnsi::Strcmp(UFBX_Lcl_Scaling, Prop.prop_name.data)==0)
						{
							TransformationPayloadContexts.FindOrAdd(Node).ScalingProp = &Prop;
						}
					}
				}

				for (const TPair<const ufbx_node*, FRigidAnimationPayloadContext>& Pair  : TransformationPayloadContexts)
				{
					const ufbx_node* Node = Pair.Key;
					const FRigidAnimationPayloadContext& RigidAnimationPayloadContext = Pair.Value;

					// inspired by UE::Interchange::Private::FFbxAnimation::AddRigidTransformAnimation

					constexpr int32 TranslationChannel = 0x0001 | 0x0002 | 0x0004;
					constexpr int32 RotationChannel = 0x0008 | 0x0010 | 0x0020;
					constexpr int32 ScaleChannel = 0x0040 | 0x0080 | 0x0100;

					int32 UsedChannels = 0;
					if (RigidAnimationPayloadContext.TranslationProp && Algo::AnyOf(MakeConstArrayView(RigidAnimationPayloadContext.TranslationProp->anim_value->curves)))
					{
						UsedChannels |= TranslationChannel;
					}
					if (RigidAnimationPayloadContext.RotationProp && Algo::AnyOf(MakeConstArrayView(RigidAnimationPayloadContext.RotationProp->anim_value->curves)))
					{
						UsedChannels |= RotationChannel;
					}
					if (RigidAnimationPayloadContext.ScalingProp && Algo::AnyOf(MakeConstArrayView(RigidAnimationPayloadContext.ScalingProp->anim_value->curves)))
					{
						UsedChannels |= ScaleChannel;
					}

					if (UsedChannels != 0)
					{
						if (UInterchangeSceneNode** Found = Parser.ElementIdToSceneNode.Find(Node->element_id))
						{
							UInterchangeSceneNode* SceneNode = *Found; 

							const FString PayloadKey = Parser.GetNodeUid(*Node) + TEXT("_RigidAnimationPayloadKey");

							Parser.PayloadContexts.Add(PayloadKey, RigidAnimationPayloadContext);

							UInterchangeTransformAnimationTrackNode* TransformAnimTrackNode = NewObject< UInterchangeTransformAnimationTrackNode >(&NodeContainer);

							const FString TransformAnimTrackNodeName = FString::Printf(TEXT("%s"), *SceneNode->GetDisplayLabel());
							const FString TransformAnimTrackNodeUid = TEXT("\\AnimationTrack\\") + TransformAnimTrackNodeName;

							NodeContainer.SetupNode(TransformAnimTrackNode, TransformAnimTrackNodeUid, TransformAnimTrackNodeName, EInterchangeNodeContainerType::TranslatedAsset);
							TransformAnimTrackNode->SetCustomActorDependencyUid(*SceneNode->GetUniqueID());
							TransformAnimTrackNode->SetCustomAnimationPayloadKey(PayloadKey, EInterchangeAnimationPayLoadType::CURVE);
							TransformAnimTrackNode->SetCustomUsedChannels(UsedChannels);

							// #ufbx_todo: UE::Interchange::Private::ProcessCustomAttributes
							// ProcessCustomAttributes(Parser, Node, TransformAnimTrackNode);

							TransformAnimTrackNodeUids.Add(TransformAnimTrackNode->GetUniqueID());
						}

					}

				}
			}
		}

		//Only one Track Set Node per fbx file:
		if (TransformAnimTrackNodeUids.Num() > 0)
		{
			FString Name;
			if (UInterchangeSceneNode** RootSceneNodeFound = Parser.ElementIdToSceneNode.Find(Parser.Scene->root_node->element_id))
			{
				UInterchangeSceneNode* RootSceneNode = *RootSceneNodeFound;
				Name = RootSceneNode->GetName();
			}

			UInterchangeAnimationTrackSetNode* TrackSetNode = NewObject< UInterchangeAnimationTrackSetNode >(&NodeContainer);

			TrackSetNode->SetCustomFrameRate(FrameRate);

			const FString AnimTrackSetNodeUid = TEXT("\\Animation\\") + Name;
			const FString AnimTrackSetNodeDisplayLabel = Name + TEXT("_TrackSetNode");

			NodeContainer.SetupNode(TrackSetNode, AnimTrackSetNodeUid, AnimTrackSetNodeDisplayLabel, EInterchangeNodeContainerType::TranslatedAsset);

			for (const FString& TransformAnimTrackNodeUid : TransformAnimTrackNodeUids)
			{
				TrackSetNode->AddCustomAnimationTrackUid(TransformAnimTrackNodeUid);
			}
		}

	}

	bool FUfbxAnimation::AddNodeAttributeCurvesAnimation(FUfbxParser& Parser
		, const FString NodeUid
		, const ufbx_prop& Prop
		, const ufbx_anim_value& AnimValue
		, TOptional<FString>& OutPayloadKey
		, TOptional<bool>& OutIsStepCurve
		, FString& OutCurveName)
	{
		FString PropertyName = Convert::ToUnrealString(Prop.name);
		const FString PayLoadKey = NodeUid + PropertyName + TEXT("_AnimationPayloadKey");
		if (!ensure(!Parser.PayloadContexts.Contains(PayLoadKey)))
		{
			return false;
		}

		bool bIsStepCurve;
		switch (Prop.type)
		{
		case UFBX_PROP_NUMBER:
		case UFBX_PROP_VECTOR:
		case UFBX_PROP_COLOR:
		case UFBX_PROP_COLOR_WITH_ALPHA:
		case UFBX_PROP_TRANSLATION:
		case UFBX_PROP_ROTATION:
		case UFBX_PROP_SCALING:
		case UFBX_PROP_DISTANCE:
		case UFBX_PROP_COMPOUND:
			bIsStepCurve = false;
			break;
		default:
			bIsStepCurve = true;
		};

		bool bHasCurve = false;
		//Only curves with Constant interpolation on all keys are deemed as StepCurves.
		for (const ufbx_anim_curve* Curve: AnimValue.curves)
		{
			if (Curve)
			{
				bHasCurve = true;
				for (const ufbx_keyframe& Keyframe : Curve->keyframes)
				{
					if (Keyframe.interpolation != UFBX_INTERPOLATION_CONSTANT_PREV 
						&& Keyframe.interpolation != UFBX_INTERPOLATION_CONSTANT_NEXT)
					{
						bIsStepCurve = false;
					}
				}
			}
		}

		if (!bHasCurve)
		{
			// Property even though has an anim_value but still not animated
			return false;
		}

		OutIsStepCurve = bIsStepCurve;
		Parser.PayloadContexts.Add(PayLoadKey, FPropertyAnimationPayloadContext{
			.Prop = &Prop,
			.AnimValue = &AnimValue,
			.bIsStepAnimation = bIsStepCurve
		});
		OutPayloadKey = PayLoadKey;
		OutCurveName = Convert::ToUnrealString(AnimValue.name);

		return true;
	}
}

#undef LOCTEXT_NAMESPACE
