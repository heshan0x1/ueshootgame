// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_EDITOR

#include "Curves/CurveLinearColorAtlas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "MaterialIRInternal.h"
#include "MaterialDomain.h"
#include "MaterialExpressionIO.h"
#include "Materials/Material.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionAbsorptionMediumMaterialOutput.h"
#include "Materials/MaterialExpressionActorPositionWS.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAntialiasedTextureMask.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionArccosine.h"
#include "Materials/MaterialExpressionArccosineFast.h"
#include "Materials/MaterialExpressionArcsine.h"
#include "Materials/MaterialExpressionArcsineFast.h"
#include "Materials/MaterialExpressionArctangent.h"
#include "Materials/MaterialExpressionArctangent2.h"
#include "Materials/MaterialExpressionArctangent2Fast.h"
#include "Materials/MaterialExpressionArctangentFast.h"
#include "Materials/MaterialExpressionAtmosphericLightColor.h"
#include "Materials/MaterialExpressionAtmosphericLightVector.h"
#include "Materials/MaterialExpressionBentNormalCustomOutput.h"
#include "Materials/MaterialExpressionBlackBody.h"
#include "Materials/MaterialExpressionBlend.h"
#include "Materials/MaterialExpressionBlendMaterialAttributes.h"
#include "Materials/MaterialExpressionBreakMaterialAttributes.h"
#include "Materials/MaterialExpressionBumpOffset.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionCameraVectorWS.h"
#include "Materials/MaterialExpressionCeil.h"
#include "Materials/MaterialExpressionChannelMaskParameter.h"
#include "Materials/MaterialExpressionClamp.h"
#include "Materials/MaterialExpressionClearCoatNormalCustomOutput.h"
#include "Materials/MaterialExpressionCloudLayer.h"
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionCollectionTransform.h"
#include "Materials/MaterialExpressionColorRamp.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionConstantBiasScale.h"
#include "Materials/MaterialExpressionConvert.h"
#include "Materials/MaterialExpressionCosine.h"
#include "Materials/MaterialExpressionCrossProduct.h"
#include "Materials/MaterialExpressionCurveAtlasRowParameter.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDataDrivenShaderPlatformInfoSwitch.h"
#include "Materials/MaterialExpressionDBufferTexture.h"
#include "Materials/MaterialExpressionDDX.h"
#include "Materials/MaterialExpressionDDY.h"
#include "Materials/MaterialExpressionDecalColor.h"
#include "Materials/MaterialExpressionDecalDerivative.h"
#include "Materials/MaterialExpressionDecalLifetimeOpacity.h"
#include "Materials/MaterialExpressionDecalMipmapLevel.h"
#include "Materials/MaterialExpressionDeltaTime.h"
#include "Materials/MaterialExpressionDepthFade.h"
#include "Materials/MaterialExpressionDepthOfFieldFunction.h"
#include "Materials/MaterialExpressionDeriveNormalZ.h"
#include "Materials/MaterialExpressionDesaturation.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionDistanceCullFade.h"
#include "Materials/MaterialExpressionDistanceFieldApproxAO.h"
#include "Materials/MaterialExpressionDistanceFieldGradient.h"
#include "Materials/MaterialExpressionDistanceFieldsRenderingSwitch.h"
#include "Materials/MaterialExpressionDistanceToNearestSurface.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionDoubleVectorParameter.h"
#include "Materials/MaterialExpressionDynamicParameter.h"
#include "Materials/MaterialExpressionExponential.h"
#include "Materials/MaterialExpressionExponential2.h"
#include "Materials/MaterialExpressionEyeAdaptation.h"
#include "Materials/MaterialExpressionEyeAdaptationInverse.h"
#include "Materials/MaterialExpressionFeatureLevelSwitch.h"
#include "Materials/MaterialExpressionFloor.h"
#include "Materials/MaterialExpressionFmod.h"
#include "Materials/MaterialExpressionAtmosphericFogColor.h"
#include "Materials/MaterialExpressionFontSample.h"
#include "Materials/MaterialExpressionFontSampleParameter.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionGenericConstant.h"
#include "Materials/MaterialExpressionGetMaterialAttributes.h"
#include "Materials/MaterialExpressionGIReplace.h"
#include "Materials/MaterialExpressionHairAttributes.h"
#include "Materials/MaterialExpressionHairColor.h"
#include "Materials/MaterialExpressionHsvToRgb.h"
#include "Materials/MaterialExpressionIf.h"
#include "Materials/MaterialExpressionIfThenElse.h"
#include "Materials/MaterialExpressionInverseLinearInterpolate.h"
#include "Materials/MaterialExpressionIsOrthographic.h"
#include "Materials/MaterialExpressionLength.h"
#include "Materials/MaterialExpressionLightmapUVs.h"
#include "Materials/MaterialExpressionLightVector.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionLightmassReplace.h"
#include "Materials/MaterialExpressionLocalPosition.h"
#include "Materials/MaterialExpressionLogarithm.h"
#include "Materials/MaterialExpressionLogarithm10.h"
#include "Materials/MaterialExpressionLogarithm2.h"
#include "Materials/MaterialExpressionMakeMaterialAttributes.h"
#include "Materials/MaterialExpressionMapARPassthroughCameraUV.h"
#include "Materials/MaterialExpressionMaterialAttributeLayers.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionMaterialProxyReplace.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialExpressionMeshPaintTextureReplace.h"
#include "Materials/MaterialExpressionMin.h"
#include "Materials/MaterialExpressionModulo.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialExpressionNaniteReplace.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionNormalize.h"
#include "Materials/MaterialExpressionNeuralPostProcessNode.h"
#include "Materials/MaterialExpressionObjectBounds.h"
#include "Materials/MaterialExpressionObjectLocalBounds.h"
#include "Materials/MaterialExpressionBounds.h"
#include "Materials/MaterialExpressionObjectOrientation.h"
#include "Materials/MaterialExpressionObjectPositionWS.h"
#include "Materials/MaterialExpressionObjectRadius.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionPanner.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionParticleColor.h"
#include "Materials/MaterialExpressionParticleDirection.h"
#include "Materials/MaterialExpressionParticleMacroUV.h"
#include "Materials/MaterialExpressionParticleMotionBlurFade.h"
#include "Materials/MaterialExpressionParticlePositionWS.h"
#include "Materials/MaterialExpressionParticleRadius.h"
#include "Materials/MaterialExpressionParticleRandom.h"
#include "Materials/MaterialExpressionParticleRelativeTime.h"
#include "Materials/MaterialExpressionParticleSize.h"
#include "Materials/MaterialExpressionParticleSpeed.h"
#include "Materials/MaterialExpressionParticleSubUV.h"
#include "Materials/MaterialExpressionParticleSubUVProperties.h"
#include "Materials/MaterialExpressionPathTracingBufferTexture.h"
#include "Materials/MaterialExpressionPathTracingQualitySwitch.h"
#include "Materials/MaterialExpressionPathTracingRayTypeSwitch.h"
#include "Materials/MaterialExpressionPerInstanceCustomData.h"
#include "Materials/MaterialExpressionPerInstanceFadeAmount.h"
#include "Materials/MaterialExpressionPerInstanceRandom.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionPixelNormalWS.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionPrecomputedAOMask.h"
#include "Materials/MaterialExpressionPreSkinnedLocalBounds.h"
#include "Materials/MaterialExpressionPreSkinnedNormal.h"
#include "Materials/MaterialExpressionPreSkinnedPosition.h"
#include "Materials/MaterialExpressionPreviousFrameSwitch.h"
#include "Materials/MaterialExpressionSamplePhysicsField.h"
#include "Materials/MaterialExpressionSphericalParticleOpacity.h"
#include "Materials/MaterialExpressionQualitySwitch.h"
#include "Materials/MaterialExpressionRayTracingQualitySwitch.h"
#include "Materials/MaterialExpressionReflectionCapturePassSwitch.h"
#include "Materials/MaterialExpressionReflectionVectorWS.h"
#include "Materials/MaterialExpressionRequiredSamplersSwitch.h"
#include "Materials/MaterialExpressionReroute.h"
#include "Materials/MaterialExpressionRgbToHsv.h"
#include "Materials/MaterialExpressionRotateAboutAxis.h"
#include "Materials/MaterialExpressionRotator.h"
#include "Materials/MaterialExpressionRound.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureOutput.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureReplace.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureSample.h"
#include "Materials/MaterialExpressionRuntimeVirtualTextureSampleParameter.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneColor.h"
#include "Materials/MaterialExpressionSceneDepth.h"
#include "Materials/MaterialExpressionSceneDepthWithoutWater.h"
#include "Materials/MaterialExpressionSceneTexelSize.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionSetMaterialAttributes.h"
#include "Materials/MaterialExpressionShaderStageSwitch.h"
#include "Materials/MaterialExpressionShadingModel.h"
#include "Materials/MaterialExpressionShadingPathSwitch.h"
#include "Materials/MaterialExpressionShadowReplace.h"
#include "Materials/MaterialExpressionSingleLayerWaterMaterialOutput.h"
#include "Materials/MaterialExpressionSign.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionSkyAtmosphereLightDirection.h"
#include "Materials/MaterialExpressionSkyAtmosphereLightIlluminance.h"
#include "Materials/MaterialExpressionSkyAtmosphereViewLuminance.h"
#include "Materials/MaterialExpressionSkyLightEnvMapSample.h"
#include "Materials/MaterialExpressionSmoothStep.h"
#include "Materials/MaterialExpressionSobol.h"
#include "Materials/MaterialExpressionSpeedTree.h"
#include "Materials/MaterialExpressionSphereMask.h"
#include "Materials/MaterialExpressionSquareRoot.h"
#include "Materials/MaterialExpressionSRGBColorToWorkingColorSpace.h"
#include "Materials/MaterialExpressionStaticBool.h"
#include "Materials/MaterialExpressionStaticComponentMaskParameter.h"
#include "Materials/MaterialExpressionStaticSwitch.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionStep.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionSwitch.h"
#include "Materials/MaterialExpressionTangent.h"
#include "Materials/MaterialExpressionTangentOutput.h"
#include "Materials/MaterialExpressionTemporalSobol.h"
#include "Materials/MaterialExpressionTextureCollection.h"
#include "Materials/MaterialExpressionTextureCollectionParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureObject.h"
#include "Materials/MaterialExpressionTextureObjectFromCollection.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionTextureProperty.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter2DArray.h"
#include "Materials/MaterialExpressionTextureSampleParameterCube.h"
#include "Materials/MaterialExpressionTextureSampleParameterSubUV.h"
#include "Materials/MaterialExpressionTextureSampleParameterVolume.h"
#include "Materials/MaterialExpressionThinTranslucentMaterialOutput.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionTransform.h"
#include "Materials/MaterialExpressionTransformPosition.h"
#include "Materials/MaterialExpressionTruncate.h"
#include "Materials/MaterialExpressionTruncateLWC.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionUserSceneTexture.h"
#include "Materials/MaterialExpressionVectorNoise.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVertexInterpolator.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionVertexTangentWS.h"
#include "Materials/MaterialExpressionViewProperty.h"
#include "Materials/MaterialExpressionViewSize.h"
#include "Materials/MaterialExpressionVirtualTextureFeatureSwitch.h"
#include "Materials/MaterialExpressionVolumetricAdvancedMaterialInput.h"
#include "Materials/MaterialExpressionVolumetricAdvancedMaterialOutput.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionFloatToUInt.h"
#include "Materials/MaterialExternalCodeRegistry.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialExpressionOperator.h"
#include "Materials/MaterialExpressionAggregate.h"
#include "Materials/MaterialSharedPrivate.h"
#include "MaterialShared.h"
#include "Misc/MemStackUtility.h"
#include "RenderUtils.h"
#include "TextureResource.h"
#include "ColorManagement/ColorSpace.h"

#include "Materials/MaterialIREmitter.h"

using FValueRef = MIR::FValueRef;

static FName NAME_CameraVector("CameraVector");

/* Constants */

void UMaterialExpression::Build(MIR::FEmitter& Em)
{
	Em.Error(TEXT("Unsupported material expression."));
} 

void UMaterialExpressionFunctionInput::Build(MIR::FEmitter& Em)
{
	FValueRef OutputValue = Em.TryInput(&Preview);
	if (OutputValue)
	{
		Em.Output(0, OutputValue);
		return;
	}

	switch (InputType)
	{
		case FunctionInput_Scalar:
			OutputValue = Em.ConstantFloat(PreviewValue.X);
			break;

		case FunctionInput_Vector2:
			OutputValue = Em.ConstantFloat2({ PreviewValue.X, PreviewValue.Y });
			break;

		case FunctionInput_Vector3:
			OutputValue = Em.ConstantFloat3({ PreviewValue.X, PreviewValue.Y, PreviewValue.Z });
			break;

		case FunctionInput_Vector4:
			OutputValue = Em.ConstantFloat4(PreviewValue);
			break;

		case FunctionInput_Bool:
		case FunctionInput_StaticBool:
			OutputValue = Em.ConstantBool(PreviewValue.X != 0.0f);

		case FunctionInput_Texture2D:
		case FunctionInput_TextureCube:
		case FunctionInput_Texture2DArray:
		case FunctionInput_VolumeTexture:
		case FunctionInput_MaterialAttributes:
		case FunctionInput_TextureExternal:
		case FunctionInput_Substrate:
			Em.Error(TEXT("Function input of object type requires preview input to be provided."));
			return;

		default:
			UE_MIR_UNREACHABLE();
	}

	Em.Output(0, OutputValue);
}

void UMaterialExpressionFunctionOutput::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Input(&A));
}

void UMaterialExpressionConstant::Build(MIR::FEmitter& Em)
{
	FValueRef Value = Em.ConstantFloat(R);
	Em.Output(0, Value);
}

void UMaterialExpressionConstant2Vector::Build(MIR::FEmitter& Em)
{
	FValueRef Value = Em.ConstantFloat2({ R, G });
	Em.Output(0, Value);
	for (int i = 0; i < 2; ++i)
	{
		Em.Output(i + 1, Em.Subscript(Value, i));
	}
}

void UMaterialExpressionConstant3Vector::Build(MIR::FEmitter& Em)
{
	FValueRef Value = Em.ConstantFloat3({ Constant.R, Constant.G, Constant.B });
	Em.Output(0, Value);
	for (int i = 0; i < 3; ++i)
	{
		Em.Output(i + 1, Em.Subscript(Value, i));
	}
}

void UMaterialExpressionConstant4Vector::Build(MIR::FEmitter& Em)
{
	FValueRef Value = Em.ConstantFloat4(Constant);
	Em.Output(0, Value);
	for (int i = 0; i < 4; ++i)
	{
		Em.Output(i + 1, Em.Subscript(Value, i));
	}
}

void UMaterialExpressionGenericConstant::Build(MIR::FEmitter& Em)
{
	FValueRef Value = Em.ConstantFromShaderValue(GetConstantValue());
	Em.Output(0, Value);
}

void UMaterialExpressionConstantBiasScale::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Multiply(Em.Add(Em.ConstantFloat(Bias), Em.Input(&Input)), Em.ConstantFloat(Scale)));
}

void UMaterialExpressionStaticBool::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.ConstantBool(Value));
}

static FValueRef BuildMaterialExpressionParameter(MIR::FEmitter& Em, UMaterialExpressionParameter* ParameterExpr)
{
	FMaterialParameterMetadata Metadata;
	if (!ParameterExpr->GetParameterValue(Metadata))
	{
		Em.Error(TEXT("Could not get parameter value."));
		return Em.Poison();
	}

	return Em.Parameter(ParameterExpr->GetParameterName(), Metadata);
}

void UMaterialExpressionParameter::Build(MIR::FEmitter& Em)
{
	Em.Output(0, BuildMaterialExpressionParameter(Em, this));
}

void UMaterialExpressionVectorParameter::Build(MIR::FEmitter& Em)
{
	FValueRef Value = BuildMaterialExpressionParameter(Em, this);
	Em.Output(0, Em.Swizzle(Value, MIR::FSwizzleMask::XYZ()));
	Em.Output(1, Em.Subscript(Value, 0));
	Em.Output(2, Em.Subscript(Value, 1));
	Em.Output(3, Em.Subscript(Value, 2));
	Em.Output(4, Em.Subscript(Value, 3));
	Em.Output(5, Value);
}

void UMaterialExpressionDoubleVectorParameter::Build(MIR::FEmitter& Em)
{
	FValueRef Value = BuildMaterialExpressionParameter(Em, this);
	Em.Output(0, Em.Cast(Value, MIR::FType::MakeDoubleVector(3)));
	Em.Output(1, Em.Subscript(Value, 0));
	Em.Output(2, Em.Subscript(Value, 1));
	Em.Output(3, Em.Subscript(Value, 2));
	Em.Output(4, Em.Subscript(Value, 3));
}

void UMaterialExpressionChannelMaskParameter::Build(MIR::FEmitter& Em)
{
	FValueRef DotResult = Em.Dot(
		Em.CastToFloat(Em.Input(&Input), 4),
		BuildMaterialExpressionParameter(Em, this));

	Em.Output(0, DotResult);
	Em.Output(1, Em.Subscript(DotResult, 1));
	Em.Output(2, Em.Subscript(DotResult, 2));
	Em.Output(3, Em.Subscript(DotResult, 3));
	Em.Output(4, Em.Subscript(DotResult, 4));
}

void UMaterialExpressionStaticBoolParameter::Build(MIR::FEmitter& Em)
{
	FValueRef Value = BuildMaterialExpressionParameter(Em, this);
	Em.ToConstantBool(Value); // Check that it is a constant boolean
	Em.Output(0, Value);
}

void UMaterialExpressionStaticComponentMaskParameter::Build(MIR::FEmitter& Em)
{
	FValueRef Value = BuildMaterialExpressionParameter(Em, this);
	const MIR::FComposite* Composite = Value->As<MIR::FComposite>();

	// BuildMaterialExpressionParameter should return this as a constant 4 dimensional bool.
	check(Composite);
	check(Composite->AreComponentsConstant());

	MIR::FSwizzleMask Mask;

	TConstArrayView<MIR::FValue*> Components = Composite->GetComponents();
	check(Components.Num() == 4);

	if (Components[0]->IsTrue())
	{
		Mask.Append(MIR::EVectorComponent::X);
	}
	if (Components[1]->IsTrue())
	{
		Mask.Append(MIR::EVectorComponent::Y);
	}
	if (Components[2]->IsTrue())
	{
		Mask.Append(MIR::EVectorComponent::Z);
	}
	if (Components[3]->IsTrue())
	{
		Mask.Append(MIR::EVectorComponent::W);
	}
	
	Em.Output(0, Em.Swizzle(Em.Input(&Input), Mask));
}

void UMaterialExpressionStaticSwitch::Build(MIR::FEmitter& Em)
{
	bool bCondition = Em.ToConstantBool(Em.InputDefaultBool(&Value, DefaultValue));
	UE_MIR_CHECKPOINT(Em); // Make sure that evaluating bCondition didn't raise an error

	Em.Output(0, Em.Input(bCondition ? &A : &B));
}

void UMaterialExpressionStaticSwitchParameter::Build(MIR::FEmitter& Em)
{
	bool bCondition = Em.ToConstantBool(BuildMaterialExpressionParameter(Em, this));
	UE_MIR_CHECKPOINT(Em); // Make sure that evaluating bCondition didn't raise an error

	// Deliberately fetch both inputs, so we throw an error to the user if either is not set, even though we are only returning one of the inputs.
	FValueRef AValue = Em.Input(&A);
	FValueRef BValue = Em.Input(&B);

	Em.Output(0, bCondition ? AValue : BValue);
}

static void EmitEffectiveInputOrError(MIR::FEmitter& Em, FExpressionInput* EffectiveInput, FString& Error)
{
	if (!EffectiveInput)
	{
		Em.Error(Error);
		return;
	}

	Em.Output(0, Em.Input(EffectiveInput));
}

void UMaterialExpressionDataDrivenShaderPlatformInfoSwitch::Build(MIR::FEmitter& Em)
{
	FString Error;
	EmitEffectiveInputOrError(Em, GetEffectiveInput(Em.GetShaderPlatform(), Error), Error);
}

void UMaterialExpressionDistanceFieldsRenderingSwitch::Build(MIR::FEmitter& Em)
{
	EShaderPlatform ShaderPlatform = Em.GetShaderPlatform();
	bool bDistanceFieldsEnabled = IsMobilePlatform(ShaderPlatform) ? IsMobileDistanceFieldEnabled(ShaderPlatform) : IsUsingDistanceFields(ShaderPlatform);

	// Deliberately fetch both inputs, so we throw an error to the user if either is not set, even though we are only returning one of the inputs.
	FValueRef YesValue = Em.Input(&Yes);
	FValueRef NoValue = Em.Input(&No);

	Em.Output(0, bDistanceFieldsEnabled ? YesValue : NoValue);
}

void UMaterialExpressionFeatureLevelSwitch::Build(MIR::FEmitter& Em)
{
	// Always fetch the Default input, so we throw an error to the user if it's not set, even if it doesn't end up being used.
	FValueRef DefaultValue = Em.Input(&Default);
	FValueRef Result = Em.TryInput(&Inputs[GetFeatureLevelToCompile(Em.GetShaderPlatform(), Em.GetFeatureLevel())]);

	if (!Result)
	{
		Result = DefaultValue;
	}
	Em.Output(0, Result);
}

void UMaterialExpressionQualitySwitch::Build(MIR::FEmitter& Em)
{
	EMaterialQualityLevel::Type QualityLevelToCompile = Em.GetQualityLevel();
	if (QualityLevelToCompile != EMaterialQualityLevel::Num)
	{
		check(QualityLevelToCompile < UE_ARRAY_COUNT(Inputs));

		FValueRef Result = Em.TryInput(&Inputs[QualityLevelToCompile]);
		if (!Result)
		{
			Result = Em.Input(&Default);
		}
		else
		{
			// Deliberately fetch the Default input, so we throw an error to the user if it's not set, even when it's not being used.
			Em.Input(&Default);
		}
		Em.Output(0, Result);
	}
	else
	{
		Em.Output(0, Em.Input(&Default));
	}
}

void UMaterialExpressionRequiredSamplersSwitch::Build(MIR::FEmitter& Em)
{
	const EShaderPlatform ShaderPlatform = Em.GetShaderPlatform();
	const bool bCheck = RequiredSamplers <= FDataDrivenShaderPlatformInfo::GetMaxSamplers(ShaderPlatform);

	Em.Output(0, Em.Input(bCheck ? &InputTrue : &InputFalse));
}

void UMaterialExpressionShaderStageSwitch::Build(MIR::FEmitter& Em)
{
	FValueRef PixelValue = Em.Input(&PixelShader);
	FValueRef VertexValue = Em.Input(&VertexShader);
	UE_MIR_CHECKPOINT(Em);

	MIR::FType CommonType = Em.GetCommonType(PixelValue->Type, VertexValue->Type);
	UE_MIR_CHECKPOINT(Em);

	PixelValue = Em.Cast(PixelValue, CommonType);
	VertexValue = Em.Cast(VertexValue, CommonType);

	static_assert(MIR::EStage::NumStages == 3);
	TStaticArray<FValueRef, MIR::EStage::NumStages> ValuePerStage;
	ValuePerStage[MIR::EStage::Stage_Vertex] = VertexValue;
	ValuePerStage[MIR::EStage::Stage_Pixel] = PixelValue;
	ValuePerStage[MIR::EStage::Stage_Compute] = PixelValue;

	Em.Output(0, Em.StageSwitch(VertexValue->Type, ValuePerStage));
}

void UMaterialExpressionShadingPathSwitch::Build(MIR::FEmitter& Em)
{
	FValueRef Result = Em.TryInput(&Inputs[GetShadingPathToCompile(Em.GetShaderPlatform(), Em.GetFeatureLevel())]);
	if (!Result)
	{
		Result = Em.Input(&Default);
	}
	else
	{
		// Deliberately fetch the Default input, so we throw an error to the user if it's not set, even when it's not being used.
		Em.Input(&Default);
	}
	Em.Output(0, Result);
}

void UMaterialExpressionVirtualTextureFeatureSwitch::Build(MIR::FEmitter& Em)
{
	// Deliberately fetch both inputs, so we throw an error to the user if either is not set, even though we are only returning one of the inputs.
	FValueRef YesValue = Em.Input(&Yes);
	FValueRef NoValue = Em.Input(&No);

	Em.Output(0, UseVirtualTexturing(Em.GetShaderPlatform()) ? YesValue : NoValue);
}

static FValueRef EmitInlineHLSL(MIR::FEmitter& Em, FName ExternalCodeIdentifier, TConstArrayView<FValueRef> InArguments = {})
{
	const FMaterialExternalCodeDeclaration* ExternalCodeDeclaration = MaterialExternalCodeRegistry::Get().FindExternalCode(ExternalCodeIdentifier);
	if (!ExternalCodeDeclaration)
	{
		Em.Errorf(TEXT("Missing external code declaration for '%s'"), *ExternalCodeIdentifier.ToString());
		return {};
	}

	return Em.InlineHLSL(ExternalCodeDeclaration, InArguments);
}

static void EmitExternalCodeConditionalReplace(MIR::FEmitter& Em, FName ExternalCodeIdentifier, FExpressionInput& Default, FExpressionInput& Replace, const TCHAR* DefaultDesc, const TCHAR* ReplaceDesc)
{
	FValueRef DefaultValue = Em.Input(&Default);
	FValueRef ReplaceValue = Em.Input(&Replace);
	UE_MIR_CHECKPOINT(Em);

	MIR::FType CommonType = Em.GetCommonType(DefaultValue->Type, ReplaceValue->Type);
	UE_MIR_CHECKPOINT(Em);

	DefaultValue = Em.Cast(DefaultValue, CommonType);
	ReplaceValue = Em.Cast(ReplaceValue, CommonType);

	Em.Output(0, Em.Branch(EmitInlineHLSL(Em, ExternalCodeIdentifier), ReplaceValue, DefaultValue));
}

#define REPLACE_INPUTS(DEFAULT, REPLACE) DEFAULT, REPLACE, TEXT(#DEFAULT), TEXT(#REPLACE)

void UMaterialExpressionLightmassReplace::Build(MIR::FEmitter& Em)
{
	EmitExternalCodeConditionalReplace(Em, FName("LightmassReplace"), REPLACE_INPUTS(Realtime, Lightmass));
}

void UMaterialExpressionMeshPaintTextureReplace::Build(MIR::FEmitter& Em)
{
	EmitExternalCodeConditionalReplace(Em, FName("MeshPaintTextureReplace"), REPLACE_INPUTS(Default, MeshPaintTexture));
}

void UMaterialExpressionNaniteReplace::Build(MIR::FEmitter& Em)
{
	EmitExternalCodeConditionalReplace(Em, FName("NaniteReplace"), REPLACE_INPUTS(Default, Nanite));
}

void UMaterialExpressionReflectionCapturePassSwitch::Build(MIR::FEmitter& Em)
{
	EmitExternalCodeConditionalReplace(Em, FName("ReflectionCapturePassSwitch"), REPLACE_INPUTS(Default, Reflection));
}

void UMaterialExpressionShadowReplace::Build(MIR::FEmitter& Em)
{
	EmitExternalCodeConditionalReplace(Em, FName("ShadowReplace"), REPLACE_INPUTS(Default, Shadow));
}

#undef REPLACE_INPUTS

void UMaterialExpressionAppendVector::Build(MIR::FEmitter& Em)
{
	FValueRef AVal = Em.CheckIsScalarOrVector(Em.Input(&A));
	FValueRef BVal = Em.CheckIsScalarOrVector(Em.TryInput(&B));

	UE_MIR_CHECKPOINT(Em);

	MIR::FPrimitive AType = AVal->Type.GetPrimitive();
	MIR::FPrimitive BType = BVal ? BVal->Type.GetPrimitive() : MIR::FPrimitive{};

	int Dimensions = AType.NumColumns + (BVal ? BType.NumColumns : 0);
	if (Dimensions > 4)
	{
		Em.Errorf(TEXT("The resulting vector would have %d component (it can have at most 4)."), Dimensions);
		return;
	}

	check(Dimensions >= 2 && Dimensions <= 4);

	// Construct the output vector type.
	MIR::EScalarKind ResultKind = (AType.IsDouble() || (BVal && BType.IsDouble())) ? MIR::EScalarKind::Double : MIR::EScalarKind::Float;
	MIR::FType ResultType = MIR::FType::MakeVector(ResultKind, Dimensions);

	// Set up each output vector component.  These need CastToScalarKind in case we are appending LWC and non-LWC.
	FValueRef Components[4] = { nullptr, nullptr, nullptr, nullptr };
	int ComponentIndex = 0;
	for (int i = 0; i < AType.NumColumns; ++i, ++ComponentIndex)
	{
		Components[ComponentIndex] = Em.CastToScalarKind(Em.Subscript(AVal, i), ResultKind);
	}

	if (BVal)
	{
		for (int i = 0; i < BType.NumColumns; ++i, ++ComponentIndex)
		{
			Components[ComponentIndex] = Em.CastToScalarKind(Em.Subscript(BVal, i), ResultKind);
		}
	}

	// Create the vector value and output it.
	FValueRef Output{};
	if (Dimensions == 2)
	{
		Output = Em.Vector2(Components[0], Components[1]);
	}
	else if (Dimensions == 3)
	{
		Output = Em.Vector3(Components[0], Components[1], Components[2]);
	}
	else
	{
		Output = Em.Vector4(Components[0], Components[1], Components[2], Components[3]);
	}

	Em.Output(0, Output);
}

/* Unary Operators */

void UMaterialExpressionAbs::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Abs(Em.Input(&Input)));
}

void UMaterialExpressionCeil::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Ceil(Em.Input(&Input)));
}

void UMaterialExpressionFloor::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Floor(Em.Input(&Input)));
}

void UMaterialExpressionFrac::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Frac(Em.Input(&Input)));
}

void UMaterialExpressionLength::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Length(Em.Input(&Input)));
}

void UMaterialExpressionNormalize::Build(MIR::FEmitter& Em)
{
	FValueRef InputValue = Em.CastToFloatKind(Em.Input(&VectorInput));
	if (InputValue->Type.IsScalar())
	{
		Em.Output(0, Em.ConstantOne(MIR::EScalarKind::Float));
	}
	else
	{
		Em.Output(0, Em.Multiply(InputValue, Em.Rsqrt(Em.Dot(InputValue, InputValue))));
	}
}

void UMaterialExpressionRound::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Round(Em.Input(&Input)));
}

void UMaterialExpressionExponential::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Exponential(Em.Input(&Input)));
}

void UMaterialExpressionExponential2::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Exponential2(Em.Input(&Input)));
}

void UMaterialExpressionLogarithm::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Logarithm(Em.Input(&Input)));
}

void UMaterialExpressionLogarithm2::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Logarithm2(Em.Input(&X)));
}

void UMaterialExpressionLogarithm10::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Logarithm10(Em.Input(&X)));
}

void UMaterialExpressionTruncate::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Truncate(Em.Input(&Input)));
}

void UMaterialExpressionArccosine::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.ACos(Em.Input(&Input)));
}

void UMaterialExpressionArcsine::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.ASin(Em.Input(&Input)));
}

void UMaterialExpressionArctangent::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.ATan(Em.Input(&Input)));
}

void UMaterialExpressionArccosineFast::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Operator(MIR::UO_ACosFast, Em.Input(&Input)));
}

void UMaterialExpressionArcsineFast::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Operator(MIR::UO_ASinFast, Em.Input(&Input)));
}

void UMaterialExpressionArctangentFast::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Operator(MIR::UO_ATanFast, Em.Input(&Input)));
}

void UMaterialExpressionComponentMask::Build(MIR::FEmitter& Em)
{
	FValueRef Value = Em.Input(&Input);

	MIR::FSwizzleMask Mask;
	if (R)
	{
		Mask.Append(MIR::EVectorComponent::X);
	}
	if (G)
	{
		Mask.Append(MIR::EVectorComponent::Y);
	}
	if (B)
	{
		Mask.Append(MIR::EVectorComponent::Z);
	}
	if (A)
	{
		Mask.Append(MIR::EVectorComponent::W);
	}

	Em.Output(0, Em.Swizzle(Value, Mask));
}

static FValueRef PositiveClampedPow(MIR::FEmitter& Em, FValueRef Base, FValueRef Exponent)
{
	FValueRef PrimitiveBase = Em.CheckIsPrimitive(Base);
	if (!PrimitiveBase.IsValid())
	{
		return PrimitiveBase.ToPoison();
	}

	TOptional<MIR::FPrimitive> ValuePrimitiveType = Base->Type.AsPrimitive();
	return Em.Select(
				Em.LessThanOrEquals(Base, Em.ConstantFloat(2.980233e-8)),
				Em.ConstantZero(ValuePrimitiveType->ScalarKind),
				Em.Pow(Base, Exponent));
}

void UMaterialExpressionPower::Build(MIR::FEmitter& Em)
{
	Em.Output(0, PositiveClampedPow(Em, 
					 				Em.Input(&Base),
					 				Em.InputDefaultFloat(&Exponent, ConstExponent)));
}

static FValueRef GetTrigonometricInputWithPeriod(MIR::FEmitter& Em, const FExpressionInput* Input, float Period)
{
	// Get input after checking it has primitive type.
	FValueRef Value = Em.CheckIsArithmetic(Em.Input(Input));
	if (Period > 0.0f)
	{
		Value = Em.Multiply(Value, Em.ConstantFloat(2.0f * (float)UE_PI / Period));
	}
	return Value;
}

void UMaterialExpressionCosine::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Cos(GetTrigonometricInputWithPeriod(Em, &Input, Period)));
}

void UMaterialExpressionSine::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Sin(GetTrigonometricInputWithPeriod(Em, &Input, Period)));
}

void UMaterialExpressionTangent::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Tan(GetTrigonometricInputWithPeriod(Em, &Input, Period)));
}

void UMaterialExpressionSaturate::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Saturate(Em.Input(&Input)));
}

void UMaterialExpressionSign::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Sign(Em.Input(&Input)));
}

void UMaterialExpressionSquareRoot::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Sqrt(Em.Input(&Input)));
}

static FValueRef EmitInlineHLSL(MIR::FEmitter& Em, const UMaterialExpressionExternalCodeBase& InExternalCodeExpression, int32 InExternalCodeIdentifierIndex, TConstArrayView<FValueRef> InArguments = {}, MIR::EValueFlags ValueFlags = MIR::EValueFlags::None)
{
	checkf(InExternalCodeIdentifierIndex >= 0 && InExternalCodeIdentifierIndex < InExternalCodeExpression.ExternalCodeIdentifiers.Num(),
		TEXT("External code identifier index (%d) out of bounds; Upper bound is %d"), InExternalCodeIdentifierIndex, InExternalCodeExpression.ExternalCodeIdentifiers.Num());

	const FName ExternalCodeIdentifier = InExternalCodeExpression.ExternalCodeIdentifiers[InExternalCodeIdentifierIndex];
	const FMaterialExternalCodeDeclaration* ExternalCodeDeclaration = MaterialExternalCodeRegistry::Get().FindExternalCode(ExternalCodeIdentifier);
	if (!ExternalCodeDeclaration)
	{
		Em.Errorf(TEXT("Missing external code declaration for '%s' [Index=%d]"), *ExternalCodeIdentifier.ToString(), InExternalCodeIdentifierIndex);
		return {};
	}

	return Em.InlineHLSL(ExternalCodeDeclaration, InArguments, ValueFlags);
}

static void BuildInlineHLSLOutput(MIR::FEmitter& Em, const UMaterialExpressionExternalCodeBase& InExternalCodeExpression, TConstArrayView<FValueRef> InArguments)
{
	// If there are multiple output pins but only one external code identifier, use it for all outputs and let the emitter handle the swizzling.
	// This is used for output pins that map to component swizzling, e.g. R, G, B, RGB, RGBA
	if (InExternalCodeExpression.Outputs.Num() > 1 && InExternalCodeExpression.ExternalCodeIdentifiers.Num() == 1)
	{
		Em.Outputs(InExternalCodeExpression.Outputs, EmitInlineHLSL(Em, InExternalCodeExpression, 0, InArguments));
	}
	else
	{
		for (int32 OutputIndex = 0; OutputIndex < InExternalCodeExpression.ExternalCodeIdentifiers.Num(); ++OutputIndex)
		{
			Em.Output(OutputIndex, EmitInlineHLSL(Em, InExternalCodeExpression, OutputIndex, InArguments));
		}
	}
}

void UMaterialExpressionExternalCodeBase::Build(MIR::FEmitter& Em)
{
	BuildInlineHLSLOutput(Em, *this, {});
}

/* Binary Operators */

void UMaterialExpressionDesaturation::Build(MIR::FEmitter& Em)
{
	FValueRef ColorValue = Em.CastToFloat(Em.Input(&Input), 3);
	FValueRef GreyOrLerpValue = Em.Dot(ColorValue, Em.ConstantFloat3(FVector3f(LuminanceFactors))); // todo: check
	FValueRef FractionValue = Em.TryInput(&Fraction);
	if (FractionValue)
	{
		GreyOrLerpValue = Em.Lerp(ColorValue, GreyOrLerpValue, FractionValue);
	}
	Em.Output(0, GreyOrLerpValue);
}

void UMaterialExpressionDistance::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Length(Em.Subtract(Em.Input(&A), Em.Input(&B))));
}

void UMaterialExpressionFmod::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Fmod(Em.Input(&A), Em.Input(&B)));
}

static void BuildBinaryOperatorWithDefaults(MIR::FEmitter& Em, MIR::EOperator Op, const FExpressionInput* A, float ConstA, const FExpressionInput* B, float ConstB)
{
	FValueRef AVal = Em.InputDefaultFloat(A, ConstA);
	FValueRef BVal = Em.InputDefaultFloat(B, ConstB);
	Em.Output(0, Em.Operator(Op, AVal, BVal));
}

void UMaterialExpressionAdd::Build(MIR::FEmitter& Em)
{ 
	BuildBinaryOperatorWithDefaults(Em, MIR::BO_Add, &A, ConstA, &B, ConstB);
}

void UMaterialExpressionSubtract::Build(MIR::FEmitter& Em)
{
	BuildBinaryOperatorWithDefaults(Em, MIR::BO_Subtract, &A, ConstA, &B, ConstB);
}

void UMaterialExpressionMultiply::Build(MIR::FEmitter& Em)
{
	BuildBinaryOperatorWithDefaults(Em, MIR::BO_Multiply, &A, ConstA, &B, ConstB);
}

void UMaterialExpressionDivide::Build(MIR::FEmitter& Em)
{
	BuildBinaryOperatorWithDefaults(Em, MIR::BO_Divide, &A, ConstA, &B, ConstB);
}

void UMaterialExpressionMax::Build(MIR::FEmitter& Em)
{
	BuildBinaryOperatorWithDefaults(Em, MIR::BO_Max, &A, ConstA, &B, ConstB);
}

void UMaterialExpressionMin::Build(MIR::FEmitter& Em)
{ 
	BuildBinaryOperatorWithDefaults(Em, MIR::BO_Min, &A, ConstA, &B, ConstB);
}

void UMaterialExpressionModulo::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Operator(MIR::BO_Modulo, Em.Input(&A), Em.Input(&B)));
}

void UMaterialExpressionStep::Build(MIR::FEmitter& Em)
{
	BuildBinaryOperatorWithDefaults(Em, MIR::BO_Step, &Y, ConstY, &X, ConstX);
}

void UMaterialExpressionDotProduct::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Dot(Em.Input(&A), Em.Input(&B)));
}

void UMaterialExpressionCrossProduct::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Cross(Em.Input(&A), Em.Input(&B)));
}

void UMaterialExpressionArctangent2::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Atan2(Em.Input(&Y), Em.Input(&X)));
}

void UMaterialExpressionArctangent2Fast::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.Operator(MIR::BO_ATan2Fast, Em.Input(&Y), Em.Input(&X)));
}

void UMaterialExpressionEyeAdaptationInverse::Build(MIR::FEmitter& Em)
{
	check(ExternalCodeIdentifiers.Num() == 1);
	FValueRef LightValue = Em.CastToFloat(Em.InputDefaultFloat(&LightValueInput, 1.0f), 3);
	FValueRef AlphaValue = Em.CastToFloat(Em.InputDefaultFloat(&AlphaInput, 1.0f), 1);
	FValueRef MultiplierValue = EmitInlineHLSL(Em, *this, 0, { AlphaValue });
	Em.Output(0, Em.Multiply(LightValue, MultiplierValue));
}

void UMaterialExpressionOneMinus::Build(MIR::FEmitter& Em)
{
	// Default input to zero if not connected, then get it as a primitive.
	FValueRef Value = Em.InputDefaultFloat(&Input, 0.0f);

	UE_MIR_CHECKPOINT(Em); // verify the value is valid

	// Make a "One" value of the same type and dimension as input's.
	FValueRef One = Em.ConstantOne(Value->Type.AsPrimitive()->ScalarKind);

	// And flow the subtraction out of the expression's only output.
	Em.Output(0, Em.Subtract(One, Value));
}

void UMaterialExpressionIfThenElse::Build(MIR::FEmitter& Em)
{
	// Get the condition value checking it is a bool scalar
	FValueRef ConditionValue = Em.CastToBool(Em.InputDefaultBool(&Condition, false), 1);

	UE_MIR_CHECKPOINT(Em); // Make sure the condition value is valid
	
	// If condition boolean is constant, select which input is active and simply
	// bypass its value to our output.
	if (MIR::FConstant* Constant = ConditionValue->As<MIR::FConstant>())
	{
		FExpressionInput* ActiveInput = Constant->Boolean ? &True : &False;
		Em.Output(0, Em.Input(ActiveInput));
		return;
	}

	// The condition isn't static; Get the true and false values.
	// If any is disconnected, the emitter will report an error.
	FValueRef ThenValue = Em.Input(&True);
	FValueRef ElseValue = Em.Input(&False);
	
	MIR::FType CommonType = Em.GetCommonType(ThenValue->Type, ElseValue->Type);

	UE_MIR_CHECKPOINT(Em); // Make sure the common type is valid

	// Cast the "then" and "else" values to the common type.
	ThenValue = Em.Cast(ThenValue, CommonType);
	ElseValue = Em.Cast(ElseValue, CommonType);

	// Emit the branch instruction
	FValueRef OutputValue = Em.Branch(ConditionValue, ThenValue, ElseValue);

	Em.Output(0, OutputValue);
}

static FValueRef EmitAlmostEquals(MIR::FEmitter& Em, FValueRef A, FValueRef B, float Threshold)
{
	// abs(A - B) <= Threshold
	return Em.LessThanOrEquals(Em.Abs(Em.Subtract(A, B)), Em.ConstantFloat(Threshold));
}

void UMaterialExpressionIf::Build(MIR::FEmitter& Em)
{
	// Get input values
	FValueRef AValue = Em.InputDefaultFloat(&A, 0.f);
	FValueRef BValue = Em.InputDefaultFloat(&B, ConstB);
	FValueRef AGreaterThanBValue = Em.InputDefaultFloat(&AGreaterThanB, 0.f);
	FValueRef AEqualsBValue = Em.TryInput(&AEqualsB);
	FValueRef ALessThanBValue = Em.InputDefaultFloat(&ALessThanB, 0.f);

	FValueRef OutputValue;

	// Less than comparison -- if equals value isn't present (see below), AGreaterThanBValue will also be returned for the equal case.
	FValueRef ALessThanBConditionValue = Em.LessThan(AValue, BValue);

	if (ALessThanBConditionValue->Type.IsBoolScalar())
	{
		OutputValue = Em.Branch(ALessThanBConditionValue, ALessThanBValue, AGreaterThanBValue);
	}
	else
	{
		OutputValue = Em.Select(ALessThanBConditionValue, ALessThanBValue, AGreaterThanBValue);
	}

	// Equals value is optional -- if present, generate an additional conditional.
	if (AEqualsBValue.IsValid())
	{
		FValueRef AEqualsBConditionValue = EmitAlmostEquals(Em, AValue, BValue, EqualsThreshold);

		if (AEqualsBConditionValue->Type.IsBoolScalar())
		{
			OutputValue = Em.Branch(AEqualsBConditionValue, AEqualsBValue, OutputValue);
		}
		else
		{
			OutputValue = Em.Select(AEqualsBConditionValue, AEqualsBValue, OutputValue);
		}
	}

	Em.Output(0, OutputValue);
}

// If DefaultOffset is not null, Coordinates are treated as an offset (or DefaultOffset if unset), rather than absolute coordinates.  Clamping is
// automatically applied for custom or offset fetches -- the "bClamped" parameter only controls clamping for default texture coordinate fetches,
// and is only needed when fetching from lower resolution User Scene Textures.  A zero constant can be passed in for "SceneTextureInput" for
// cases where the default view rect should be used for UV calculations.
static FValueRef SceneTextureExpressionTexCoords(MIR::FEmitter& Em, FValueRef SceneTextureInput, const FExpressionInput& Coordinates, const FVector2D* DefaultOffset = nullptr, bool bClamped = false)
{
	FValueRef TexCoords;
	if (DefaultOffset)
	{
		TexCoords = Em.InputDefaultFloat2(&Coordinates, { (float)DefaultOffset->X, (float)DefaultOffset->Y });
		TexCoords = Em.InlineHLSL(MIR::FType::MakeFloatVector(2), TEXTVIEW("CalcScreenUVFromOffsetFraction(GetScreenPosition(Parameters), $0)"), { TexCoords });
	}
	else
	{
		TexCoords = Em.TryInput(&Coordinates);
		if (TexCoords)
		{
			// Convert raw TexCoords expression to Float2, then convert from viewport to scene texture space
			TexCoords = Em.Cast(TexCoords, MIR::FType::MakeFloatVector(2));
			TexCoords = Em.InlineHLSL(MIR::FType::MakeFloatVector(2), TEXTVIEW("ClampSceneTextureUV(ViewportUVToSceneTextureUV($0, $1), $1)"), { TexCoords, SceneTextureInput });
		}
		else
		{
			FStringView TexCoordsCode = bClamped ?
				TEXTVIEW("ClampSceneTextureUV(GetDefaultSceneTextureUV(Parameters, $0), $0)") :
				TEXTVIEW("GetDefaultSceneTextureUV(Parameters, $0)");

			TexCoords = Em.InlineHLSL(MIR::FType::MakeFloatVector(2), TexCoordsCode, { SceneTextureInput });
		}
	}
	return TexCoords;
}

static void SceneTextureExpressionBuild(MIR::FEmitter& Em, FValueRef SceneTextureInput, const FExpressionInput& Coordinates, const FVector2D* DefaultOffset, bool bClamped, bool bFiltered)
{
	FValueRef TexCoords = SceneTextureExpressionTexCoords(Em, SceneTextureInput, Coordinates, DefaultOffset, bClamped);

	FStringView SceneTextureLookupCode = bFiltered ?
		TEXTVIEW("SceneTextureLookup(Parameters, $0, $1, true)") :
		TEXTVIEW("SceneTextureLookup(Parameters, $0, $1, false)");

	Em.Output(0, Em.InlineHLSL(MIR::FType::MakeFloatVector(4), SceneTextureLookupCode, { TexCoords, SceneTextureInput }));
	Em.Output(1, Em.InlineHLSL(MIR::FType::MakeFloatVector(2), TEXTVIEW("GetSceneTextureViewSize($0).xy"), { SceneTextureInput }));
	Em.Output(2, Em.InlineHLSL(MIR::FType::MakeFloatVector(2), TEXTVIEW("GetSceneTextureViewSize($0).zw"), { SceneTextureInput }));
}

void UMaterialExpressionSceneTexture::Build(MIR::FEmitter& Em)
{
	const bool bClamped = false;
	SceneTextureExpressionBuild(Em, Em.SceneTexture(SceneTextureId), Coordinates, nullptr, bClamped, bFiltered);
}

void UMaterialExpressionUserSceneTexture::Build(MIR::FEmitter& Em)
{
	SceneTextureExpressionBuild(Em, Em.UserSceneTexture(UserSceneTexture), Coordinates, nullptr, bClamped, bFiltered);
}

void UMaterialExpressionSceneColor::Build(MIR::FEmitter& Em)
{
	FValueRef TexCoords = SceneTextureExpressionTexCoords(Em, Em.ConstantInt(0), Input, this->InputMode == EMaterialSceneAttributeInputMode::OffsetFraction ? &ConstInput : nullptr);

	// We need a dependency on ScreenTexture as a second argument, so the value analyzer can see it, even though it's technically not used in the code.
	FValueRef ScreenTexture = Em.ScreenTexture(MIR::EScreenTexture::SceneColor);
	Em.Output(0, Em.InlineHLSL(MIR::FType::MakeFloatVector(4), TEXTVIEW("DecodeSceneColorAndAlpharForMaterialNode($0)"), { TexCoords, ScreenTexture }));;
}

void UMaterialExpressionSceneDepth::Build(MIR::FEmitter& Em)
{
	FValueRef TexCoords = SceneTextureExpressionTexCoords(Em, Em.ConstantInt(0), Input, this->InputMode == EMaterialSceneAttributeInputMode::OffsetFraction ? &ConstInput : nullptr);

	// We need a dependency on ScreenTexture as a second argument, so the value analyzer can see it, even though it's technically not used in the code.
	FValueRef ScreenTexture = Em.ScreenTexture(MIR::EScreenTexture::SceneDepth);
	Em.Output(0, Em.InlineHLSL(MIR::FType::MakeFloatScalar(), TEXTVIEW("CalcSceneDepth($0)"), { TexCoords, ScreenTexture }));;
}

void UMaterialExpressionSceneDepthWithoutWater::Build(MIR::FEmitter& Em)
{
	FValueRef TexCoords = SceneTextureExpressionTexCoords(Em, Em.ConstantInt(0), Input, this->InputMode == EMaterialSceneAttributeInputMode::OffsetFraction ? &ConstInput : nullptr);

	// We need a dependency on ScreenTexture as a third argument, so the value analyzer can see it, even though it's technically not used in the code.
	FValueRef ScreenTexture = Em.ScreenTexture(MIR::EScreenTexture::SceneDepthWithoutWater);
	Em.Output(0, Em.InlineHLSL(MIR::FType::MakeFloatScalar(), TEXTVIEW("MaterialExpressionSceneDepthWithoutWater($0, $1)"), { TexCoords, Em.ConstantFloat(FallbackDepth), ScreenTexture }));;
}

void UMaterialExpressionDBufferTexture::Build(MIR::FEmitter& Em)
{
	FValueRef TexCoords = SceneTextureExpressionTexCoords(Em, Em.ConstantInt(0), Coordinates);

	FValueRef ScreenTexture = Em.DBufferTexture(DBufferTextureId);
	Em.Output(0, Em.InlineHLSL(MIR::FType::MakeFloatVector(4), TEXTVIEW("MaterialExpressionDBufferTextureLookup(Parameters, $0, $1)"), { TexCoords, ScreenTexture }));;
}

void UMaterialExpressionSphericalParticleOpacity::Build(MIR::FEmitter& Em)
{
	FValueRef DensityValue = Em.InputDefaultFloat(&Density, ConstantDensity);
	UE_MIR_CHECKPOINT(Em); // Early out in case of errors
	BuildInlineHLSLOutput(Em, *this, { DensityValue });
}

void UMaterialExpressionShadingModel::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.ShadingModel(ShadingModel));
}

void UMaterialExpressionTextureObject::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.TextureObject(Texture, SamplerType));
}

static MIR::ETextureReadMode TextureGatherModeToMIR(::ETextureGatherMode Mode)
{
	switch (Mode)
	{
		case TGM_Red: return MIR::ETextureReadMode::GatherRed;
		case TGM_Green: return MIR::ETextureReadMode::GatherGreen;
		case TGM_Blue: return MIR::ETextureReadMode::GatherBlue;
		case TGM_Alpha: return MIR::ETextureReadMode::GatherAlpha;
		default: UE_MIR_UNREACHABLE();
	}
}

static FValueRef BuildTextureSample(MIR::FEmitter& Em, UMaterialExpressionTextureSample* Expr, FValueRef Texture, EMaterialValueType TextureType, FValueRef TexCoords, bool bAutomaticViewMipBias)
{
	FValueRef TextureRead{};
	if (Expr->GatherMode != TGM_None)
	{
		if (Expr->MipValueMode != TMVM_None)
		{
			Em.Errorf(TEXT("Texture gather does not support mipmap overrides (it implicitly accesses a specific mip)."));
			return TextureRead;
		}

		TextureRead = Em.TextureGather(Texture, TexCoords, TextureGatherModeToMIR(Expr->GatherMode), { Expr->SamplerSource });
	}
	else
	{
		// If not 2D texture, disable AutomaticViewMipBias.
		if (!(TextureType & (MCT_Texture2D | MCT_TextureVirtual | MCT_TextureMeshPaint)))
		{
			bAutomaticViewMipBias = false;
		}

		// Get the mip value level (either through the expression input or using the given constant if disconnected).
		FValueRef MipValue;
		if (Expr->MipValueMode == TMVM_MipLevel || Expr->MipValueMode == TMVM_MipBias)
		{
			MipValue = Em.CheckIsScalar(Em.InputDefaultInt(&Expr->MipValue, Expr->ConstMipValue));
		}

		switch (Expr->MipValueMode)
		{
			case TMVM_None:
				TextureRead = Em.TextureSample(Texture, TexCoords, bAutomaticViewMipBias, { Expr->SamplerSource });
				break;
		
			case TMVM_MipBias:
				TextureRead = Em.TextureSampleBias(Texture, TexCoords, MipValue, bAutomaticViewMipBias, { Expr->SamplerSource });
				break;
		
			case TMVM_MipLevel:
				TextureRead = Em.TextureSampleLevel(Texture, TexCoords, MipValue, bAutomaticViewMipBias, { Expr->SamplerSource });
				break;

			case TMVM_Derivative:
			{
				FValueRef TexCoordsDdx = Em.Cast(Em.Input(&Expr->CoordinatesDX), TexCoords->Type);
				FValueRef TexCoordsDdy = Em.Cast(Em.Input(&Expr->CoordinatesDY), TexCoords->Type);
				TextureRead = Em.TextureSampleGrad(Texture, TexCoords, TexCoordsDdx, TexCoordsDdy, bAutomaticViewMipBias, { Expr->SamplerSource });
				break;
			}
		}
	}

	return TextureRead;
}

static FValueRef BuildTextureValue(MIR::FEmitter& Em, UMaterialExpressionTextureSample* Expr)
{
	FValueRef TextureValue = Em.TryInput(&Expr->TextureObject);
	if (!TextureValue)
	{
		if (!Expr->Texture)
		{
			Em.Error(TEXT("No texture specified for this expression."));
			return Em.Poison();
		}

		TextureValue = Em.TextureObject(Expr->Texture.Get(), Expr->SamplerType);
	}
	return TextureValue;
}

static FValueRef BuildTextureObjectParameter(MIR::FEmitter& Em, UMaterialExpressionTextureSampleParameter* Expr)
{
	FMaterialParameterMetadata Param;
	if (!Expr->GetParameterValue(Param))
	{
		Em.Error(TEXT("Failed to get parameter value"));
		return nullptr;
	}

	if (!Expr->Texture)
	{
		Em.Error(TEXT("Requires valid texture"));
		return nullptr;
	}

	FString SamplerTypeError;
	if (!UMaterialExpressionTextureBase::VerifySamplerType(Em.GetShaderPlatform(), Em.GetTargetPlatform(), Expr->Texture, Expr->SamplerType, SamplerTypeError))
	{
		Em.Errorf(TEXT("%s"), *SamplerTypeError);
		return nullptr;
	}

	FValueRef ParameterValue = Em.Parameter(Expr->GetParameterName(), Param, Expr->SamplerType);
	if (!ParameterValue->Type.IsTexture())
	{
		Em.Error(TEXT("Parameter is not a texture"));
		return nullptr;
	}

	return ParameterValue;
}

static void BuildTextureSampleExpression(MIR::FEmitter& Em, UMaterialExpressionTextureSample* Expr, FValueRef Texture, EMaterialValueType TextureType)
{
	FValueRef TexCoords = Em.TryInput(&Expr->Coordinates);
	if (!TexCoords)
	{
		TexCoords = Em.ExternalInput(MIR::TexCoordIndexToExternalInput(Expr->ConstCoordinate));
	}

	// Determine if automatic view mip bias should be used, by trying to acquire its input as a static boolean.
	const bool bAutomaticViewMipBias = Em.ToConstantBool(Em.InputDefaultBool(&Expr->AutomaticViewMipBiasValue, Expr->AutomaticViewMipBias));

	FValueRef TextureRead = BuildTextureSample(Em, Expr, Texture, TextureType, TexCoords, bAutomaticViewMipBias);

	Em.Output(0, Em.Swizzle(TextureRead, MIR::FSwizzleMask::XYZ()));
	Em.Output(1, Em.Subscript(TextureRead, 0));
	Em.Output(2, Em.Subscript(TextureRead, 1));
	Em.Output(3, Em.Subscript(TextureRead, 2));
	Em.Output(4, Em.Subscript(TextureRead, 3));
	Em.Output(5, TextureRead); 
}

void UMaterialExpressionTextureSample::Build(MIR::FEmitter& Em)
{
	FValueRef TextureValue = Em.TryInput(&TextureObject);
	if (!TextureValue)
	{
		if (!Texture)
		{
			Em.Error(TEXT("No texture specified for this expression."));
			return;
		}

		TextureValue = Em.TextureObject(Texture.Get(), SamplerType);
	}

	UE_MIR_CHECKPOINT(Em);

	UObject* DefaultTexture = Em.GetTextureFromValue(TextureValue);
	if (!DefaultTexture)
	{
		Em.Error(TEXT("Missing texture object from input"));
		return;
	}

	BuildTextureSampleExpression(Em, this, TextureValue, MIR::Internal::GetTextureMaterialValueType(DefaultTexture));
}

static void EmitParticleSubUV(MIR::FEmitter& Em, UMaterialExpressionTextureSample* Expr, FValueRef TextureValue, bool bBlend, FValueRef DummyDependency)
{
	EMaterialValueType TextureType = MIR::Internal::GetTextureMaterialValueType(Em.GetTextureFromValue(TextureValue));

	// Although the parent UMaterialExpressionTextureSample class includes an automatic view mip bias flag, it is specifically ignored by ParticleSubUV.
	const bool bAutomaticViewMipBias = false;

	static FName NAME_ParticleSubUVCoords0("ParticleSubUVCoords0");
	static FName NAME_ParticleSubUVCoords1("ParticleSubUVCoords1");
	static FName NAME_ParticleSubUVLerp("ParticleSubUVLerp");

	FValueRef TexCoords0 = DummyDependency ? EmitInlineHLSL(Em, NAME_ParticleSubUVCoords0, { DummyDependency }) : EmitInlineHLSL(Em, NAME_ParticleSubUVCoords0);
	FValueRef Sample0 = BuildTextureSample(Em, Expr, TextureValue, TextureType, TexCoords0, bAutomaticViewMipBias);

	if (bBlend)
	{
		FValueRef TexCoords1 = EmitInlineHLSL(Em, NAME_ParticleSubUVCoords1);
		FValueRef Sample1 = BuildTextureSample(Em, Expr, TextureValue, TextureType, TexCoords1, bAutomaticViewMipBias);

		FValueRef SubImageLerp = EmitInlineHLSL(Em, NAME_ParticleSubUVLerp);

		Sample0 = Em.Lerp(Sample0, Sample1, SubImageLerp);
	}

	// Same outputs as UMaterialExpressionTextureSample
	Em.Output(0, Em.Swizzle(Sample0, MIR::FSwizzleMask::XYZ()));
	Em.Output(1, Em.Subscript(Sample0, 0));
	Em.Output(2, Em.Subscript(Sample0, 1));
	Em.Output(3, Em.Subscript(Sample0, 2));
	Em.Output(4, Em.Subscript(Sample0, 3));
	Em.Output(5, Sample0);
}

// Inherits from UMaterialExpressionTextureSample, but uses different particle specific UVs, and optionally supports blending two different texture samples.
void UMaterialExpressionParticleSubUV::Build(MIR::FEmitter& Em)
{
	FValueRef TextureValue = BuildTextureValue(Em, this);
	UE_MIR_CHECKPOINT(Em);

	EmitParticleSubUV(Em, this, TextureValue, bBlend, {});
}

// Similar to above, but texture comes from a parameter, rather than a local or object texture reference.
void UMaterialExpressionTextureSampleParameterSubUV::Build(MIR::FEmitter& Em)
{
	FValueRef ParameterValue = BuildTextureObjectParameter(Em, this);
	UE_MIR_CHECKPOINT(Em);

	// while this expression does provide a TextureCoordinate input pin, it is, and has always been, ignored.  And only
	// supports using UV0.  Further, in order to support non-vertex fetch implementations we need to be sure to register
	// the use of the first texture slot
	FValueRef DummyDependency = Em.ExternalInput(MIR::TexCoordIndexToExternalInput(0));

	EmitParticleSubUV(Em, this, ParameterValue, bBlend, DummyDependency);
}

// Inherits from UMaterialExpressionTextureSample, but does extra math on the sample afterwards.  Note that this was an HLSL utility function in the
// original translator, but uses ops here.  The main advantage of the ops version is that it uses the standard texture sampling code path, rather than
// sampling the texture in the utility function, meaning it supports all sampling features (the original would break if using non-standard sampling).
void UMaterialExpressionAntialiasedTextureMask::Build(MIR::FEmitter& Em)
{
	// Check if a texture is assigned and the right type.
	FString ErrorMessage;
	if (!TextureIsValid(Texture, ErrorMessage))
	{
		Em.Errorf(TEXT("%s"), *ErrorMessage);
		return;
	}

	FValueRef TexCoords = Em.TryInput(&Coordinates);
	if (!TexCoords)
	{
		TexCoords = Em.ExternalInput(MIR::TexCoordIndexToExternalInput(ConstCoordinate));
	}

	FValueRef TextureValue = BuildTextureValue(Em, this);
	UE_MIR_CHECKPOINT(Em);
	FValueRef Sample1 = BuildTextureSample(Em, this, TextureValue, MCT_Texture2D, TexCoords, /*bAutomaticViewMipBias=*/ false);

	FValueRef ThresholdConst = Em.ConstantFloat(Threshold);

	// Logic below ported from the AntialiasedTextureMask HLSL function.
	Sample1 = Em.Subscript(Sample1, FMath::Clamp(Channel, 0, 3));

	FValueRef TexDDLength = Em.Max(Em.Abs(Em.PartialDerivative(Sample1, MIR::EDerivativeAxis::X)), Em.Abs(Em.PartialDerivative(Sample1, MIR::EDerivativeAxis::Y)));
	FValueRef Top = Em.Subtract(Sample1, ThresholdConst);
	Em.Output(0, Em.Add(Em.Divide(Top, TexDDLength), ThresholdConst));
}

static void BuildTextureSampleParameter(MIR::FEmitter& Em, UMaterialExpressionTextureSampleParameter* Expr)
{
	FString ErrorMessage;
	if (!Expr->TextureIsValid(Expr->Texture, ErrorMessage))
	{
		Em.Error(ErrorMessage);
		return;
	}
	FValueRef ParameterValue = BuildTextureObjectParameter(Em, Expr);
	UE_MIR_CHECKPOINT(Em);
	BuildTextureSampleExpression(Em, Expr, ParameterValue, Expr->Texture->GetMaterialType());
}

void UMaterialExpressionTextureSampleParameter::Build(MIR::FEmitter& Em)
{
	BuildTextureSampleParameter(Em, this);
}

static void BuildTextureSampleParameterWithCoordinatesInput(MIR::FEmitter& Em, UMaterialExpressionTextureSampleParameter* Expr)
{
	Em.Input(&Expr->Coordinates); // Cubemap, 2DArray, and Volume sampling requires coordinates input specified
	UE_MIR_CHECKPOINT(Em);
	BuildTextureSampleParameter(Em, Expr);
}

void UMaterialExpressionTextureSampleParameterCube::Build(MIR::FEmitter& Em)
{
	BuildTextureSampleParameterWithCoordinatesInput(Em, this);
}

void UMaterialExpressionTextureSampleParameter2DArray::Build(MIR::FEmitter& Em)
{
	BuildTextureSampleParameterWithCoordinatesInput(Em, this);
}

void UMaterialExpressionTextureSampleParameterVolume::Build(MIR::FEmitter& Em)
{
	BuildTextureSampleParameterWithCoordinatesInput(Em, this);
}

void UMaterialExpressionTextureObjectParameter::Build(MIR::FEmitter& Em)
{
	Em.Output(0, BuildTextureObjectParameter(Em, this));
}

void UMaterialExpressionTextureCoordinate::Build(MIR::FEmitter& Em)
{
	if (UnMirrorU || UnMirrorV)
	{
		Em.Error(TEXT("Unmirroring unsupported"));
		return;
	}

	FValueRef OutputValue = Em.ExternalInput(MIR::TexCoordIndexToExternalInput(CoordinateIndex));
	
	// Multiply the UV input by the UV tiling constants
	OutputValue = Em.Multiply(OutputValue, Em.ConstantFloat2({ UTiling, VTiling }));
	
	Em.Output(0, OutputValue);
}

void UMaterialExpressionTextureProperty::Build(MIR::FEmitter& Em)
{
	MIR::FValueRef TextureValue = Em.Input(&TextureObject);
	UE_MIR_CHECKPOINT(Em);

	const bool bTexelSizeInUVSpace = Property == TMTM_TexelSize;

	const UE::Shader::EPreshaderOpcode PreshaderOpcode =
		bTexelSizeInUVSpace
			? UE::Shader::EPreshaderOpcode::TexelSize
			: UE::Shader::EPreshaderOpcode::TextureSize;

	UObject* SourceParameterTexture = Em.GetTextureFromValue(TextureValue);
	if (!SourceParameterTexture)
	{
		Em.Error(TEXT("Missing default texture from source parameter"));
		return;
	}

	const EMaterialValueType TextureType = MIR::Internal::GetTextureMaterialValueType(SourceParameterTexture);
	const EMaterialValueType PropertyType = UE::MaterialTranslatorUtils::GetTexturePropertyValueType(TextureType);

	Em.Output(0, Em.PreshaderParameter(MIR::FType::FromMaterialValueType(PropertyType), PreshaderOpcode, TextureValue));
}

#if WITH_EDITOR
void UMaterialExpressionFontSample::Build(MIR::FEmitter& Em)
{
#if PLATFORM_EXCEPTIONS_DISABLED
	// if we can't throw the error below, attempt to thwart the error by using the default font
	if (!Font)
	{
		UE_LOG(LogMaterial, Log, TEXT("Using default font instead of real font!"));
		Font = GEngine->GetMediumFont();
		FontTexturePage = 0;
	}
	else if (!Font->Textures.IsValidIndex(FontTexturePage))
	{
		UE_LOG(LogMaterial, Log, TEXT("Invalid font page %d. Max allowed is %d"), FontTexturePage, Font->Textures.Num());
		FontTexturePage = 0;
	}
#endif
	if (!Font)
	{
		Em.Error(TEXT("Missing input Font"));
	}
	else if (Font->FontCacheType == EFontCacheType::Runtime)
	{
		Em.Errorf(TEXT("Font '%s' is runtime cached, but only offline cached fonts can be sampled"), *Font->GetName());
	}
	else if (!Font->Textures.IsValidIndex(FontTexturePage))
	{
		Em.Errorf(TEXT("Invalid font page %d. Max allowed is %d"), FontTexturePage, Font->Textures.Num());
	}
	else
	{
		auto [bSuccess, Texture, ExpectedSamplerType, ErrorOutput] = ValidateAndGetTextureSampler(Em.GetShaderPlatform(), Em.GetTargetPlatform());
		if (!bSuccess)
		{
			Em.Error(*ErrorOutput);
			return;
		}

		Em.Outputs(Outputs,
			Em.TextureSample(
				Em.TextureObject(Texture, ExpectedSamplerType),
				Em.ExternalInput(MIR::TexCoordIndexToExternalInput(0)),
				false,
				{ SSM_FromTextureAsset, ExpectedSamplerType }
			)
		);
	}
}
#endif

#if WITH_EDITOR
void UMaterialExpressionFontSampleParameter::Build(MIR::FEmitter& Em)
{
	if (!ParameterName.IsValid() ||
		ParameterName.IsNone() ||
		!Font ||
		!Font->Textures.IsValidIndex(FontTexturePage))
	{
		UMaterialExpressionFontSample::Build(Em);
	}
	else
	{
		auto [bSuccess, Texture, ExpectedSamplerType, ErrorOutput] = ValidateAndGetTextureSampler(Em.GetShaderPlatform(), Em.GetTargetPlatform());
		if (!bSuccess)
		{
			Em.Error(*ErrorOutput);
			return;
		}

		FMaterialParameterMetadata ParameterMetaData;
		GetParameterValue(ParameterMetaData);

		FValueRef TextureParameter = Em.Parameter(ParameterName, ParameterMetaData, ExpectedSamplerType);
		if (!TextureParameter->Type.IsTexture())
		{
			Em.Error(TEXT("Parameter is not a texture"));
			return;
		}

		Em.Outputs(Outputs,
			Em.TextureSample(
				TextureParameter,
				Em.ExternalInput(MIR::TexCoordIndexToExternalInput(0)),
				false,
				{ SSM_FromTextureAsset, ExpectedSamplerType }
			)
		);
	}
}
#endif

#if WITH_EDITOR

static MIR::FValueRef BuildVirtualTextureWorldToUV(MIR::FEmitter& Em, MIR::FValueRef WorldPositionValue, MIR::FValueRef P0, MIR::FValueRef P1, MIR::FValueRef P2, EPositionOrigin PositionOrigin)
{
	return Em.InlineHLSL(MIR::FType::MakeFloatVector(2), TEXTVIEW("VirtualTextureWorldToUV($0, $1, $2, $3)"), { WorldPositionValue, P0, P1, P2 });
}

static MIR::FValueRef BuildConstantVector(MIR::FEmitter& Em, TArrayView<float> InConstants)
{
	switch (InConstants.Num())
	{
	case 1:
		return Em.ConstantFloat(InConstants[0]);
	case 2:
		return Em.ConstantFloat2(FVector2f{ InConstants[0], InConstants[1] });
	case 3:
		return Em.ConstantFloat3(FVector3f{ InConstants[0], InConstants[1], InConstants[2] });
	case 4:
		return Em.ConstantFloat4(FVector4f{ InConstants[0], InConstants[1], InConstants[2], InConstants[3] });
	default:
		UE_MIR_UNREACHABLE();
	}
}

static FValueRef EmitWorldPosition(MIR::FEmitter& Em, EWorldPositionIncludedOffsets WorldPositionShaderOffset);

static FValueRef EmitTransformVectorBase(
	MIR::FEmitter& Em, FValueRef InputValue, EMaterialCommonBasis TransformSourceBasis, EMaterialCommonBasis TransformDestBasis,
	bool bIsPositionTransform, MIR::FValueRef PeriodicWorldTileSizeValue, MIR::FValueRef FirstPersonInterpolationAlphaValue);

static FValueRef BuildVirtualTextureUnpack(MIR::FEmitter& Em, FValueRef SampleCode0, FValueRef SampleCode1, FValueRef SampleCode2, FValueRef P0, EVirtualTextureUnpackType UnpackType)
{
	switch (UnpackType)
	{
	case EVirtualTextureUnpackType::BaseColorYCoCg:		return Em.InlineHLSL(MIR::FType::MakeFloatVector(3), TEXTVIEW("VirtualTextureUnpackBaseColorYCoCg($0)"), { SampleCode0 });
	case EVirtualTextureUnpackType::NormalBC3:			return Em.InlineHLSL(MIR::FType::MakeFloatVector(3), TEXTVIEW("VirtualTextureUnpackNormalBC3($0)"), { SampleCode1 });
	case EVirtualTextureUnpackType::NormalBC5:			return Em.InlineHLSL(MIR::FType::MakeFloatVector(3), TEXTVIEW("VirtualTextureUnpackNormalBC5($0)"), { SampleCode1 });
	case EVirtualTextureUnpackType::NormalBC3BC3:		return Em.InlineHLSL(MIR::FType::MakeFloatVector(3), TEXTVIEW("VirtualTextureUnpackNormalBC3BC3($0, $1)"), { SampleCode0, SampleCode1 });
	case EVirtualTextureUnpackType::NormalBC5BC1:		return Em.InlineHLSL(MIR::FType::MakeFloatVector(3), TEXTVIEW("VirtualTextureUnpackNormalBC5BC1($0, $1)"), { SampleCode1, SampleCode2 });
	case EVirtualTextureUnpackType::HeightR16:			return Em.InlineHLSL(MIR::FType::MakeFloatScalar(), TEXTVIEW("VirtualTextureUnpackHeight($0, $1)"), { SampleCode0, P0 });
	case EVirtualTextureUnpackType::DisplacementR16:	return Em.Swizzle(SampleCode0, MIR::EVectorComponent::X);
	case EVirtualTextureUnpackType::NormalBGR565:		return Em.InlineHLSL(MIR::FType::MakeFloatVector(3), TEXTVIEW("VirtualTextureUnpackNormalBGR565($0)"), { SampleCode1 });
	case EVirtualTextureUnpackType::BaseColorSRGB:		return Em.InlineHLSL(MIR::FType::MakeFloatVector(3), TEXTVIEW("VirtualTextureUnpackBaseColorSRGB($0)"), { SampleCode0 });
	default:											UE_MIR_UNREACHABLE();
	}
}

void UMaterialExpressionRuntimeVirtualTextureSample::Build(MIR::FEmitter& Em)
{
	if (!UseVirtualTexturing(Em.GetShaderPlatform()))
	{
		Em.Errorf(TEXT("Virtual texturing not supported on platform '%s'"), *ShaderPlatformToPlatformName(Em.GetShaderPlatform()).ToString());
		return;
	}

	// Is this a valid UMaterialExpressionRuntimeVirtualTextureSampleParameter?
	const bool bIsParameter = IsParameter();

	// Check validity of current virtual texture
	FString TextureValidityError;
	const bool bIsVirtualTextureValid = ValidateVirtualTextureParameters(TextureValidityError);
	if (!bIsVirtualTextureValid)
	{
		Em.Error(*TextureValidityError);
		if (!VirtualTexture)
		{
			return;
		}
	}

	// Compile the texture object references
	const int32 TextureLayerCount = URuntimeVirtualTexture::GetLayerCount(MaterialType);
	check(TextureLayerCount <= RuntimeVirtualTexture::MaxTextureLayers);

	MIR::FValueRef TextureObjects[RuntimeVirtualTexture::MaxTextureLayers];
	for (int32 TexureLayerIndex = 0; TexureLayerIndex < TextureLayerCount; TexureLayerIndex++)
	{
		const int32 PageTableLayerIndex = bSinglePhysicalSpace ? 0 : TexureLayerIndex;

		if (bIsParameter)
		{
			FMaterialParameterMetadata Metadata;
			GetParameterValue(Metadata);
			TextureObjects[TexureLayerIndex] = Em.Parameter(GetParameterName(), Metadata, SAMPLERTYPE_VirtualMasks, TexureLayerIndex, PageTableLayerIndex);
		}
		else
		{
			TextureObjects[TexureLayerIndex] = Em.RuntimeVirtualTextureObject(VirtualTexture, SAMPLERTYPE_VirtualMasks, TexureLayerIndex, PageTableLayerIndex);
		}
	}

	UE_MIR_CHECKPOINT(Em);

	// Compile the runtime virtual texture uniforms
	MIR::FValueRef Uniforms[ERuntimeVirtualTextureShaderUniform_Count];

	for (int32 UniformIndex = 0; UniformIndex < ERuntimeVirtualTextureShaderUniform_Count; ++UniformIndex)
	{
		const UE::Shader::EValueType UniformType = URuntimeVirtualTexture::GetUniformParameterType(UniformIndex);
		Uniforms[UniformIndex] = Em.PreshaderParameter(
			MIR::FType::FromShaderType(UniformType), UE::Shader::EPreshaderOpcode::RuntimeVirtualTextureUniform,
			TextureObjects[0], MIR::FPreshaderParameterPayload{ .UniformIndex = UniformIndex });
	}

	// Compile the coordinates
	// We use the virtual texture world space transform by default
	if (Coordinates.GetTracedInput().Expression != nullptr && WorldPosition.GetTracedInput().Expression != nullptr)
	{
		Em.Error(TEXT("Only one of 'Coordinates' and 'WorldPosition' can be used"));
	}

	MIR::FValueRef CoordinateValue = Em.TryInput(&Coordinates);
	if (!CoordinateValue)
	{
		MIR::FValueRef WorldPositionValue;
		if (WorldPosition.GetTracedInput().Expression != nullptr)
		{
			WorldPositionValue = Em.Input(&WorldPosition);
		}
		else
		{
			WorldPositionValue = EmitWorldPosition(Em, UE::MaterialTranslatorUtils::GetWorldPositionTypeWithOrigin(WorldPositionOriginType));
			ensure(WorldPositionValue);
		}

		if (WorldPositionValue)
		{
			if (WorldPositionOriginType == EPositionOrigin::Absolute)
			{
				MIR::FValueRef P0 = Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform0];
				MIR::FValueRef P1 = Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform1];
				MIR::FValueRef P2 = Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform2];
				CoordinateValue = BuildVirtualTextureWorldToUV(Em, WorldPositionValue, P0, P1, P2, EPositionOrigin::Absolute);
			}
			else if (WorldPositionOriginType == EPositionOrigin::CameraRelative)
			{
				//TODO: optimize by calculating translated world to VT directly.
				//This requires some more work as the transform is currently fed in through a preshader variable, which is cached.
				MIR::FValueRef AbsWorldPosIndex = EmitTransformVectorBase(Em, WorldPositionValue, EMaterialCommonBasis::MCB_TranslatedWorld, EMaterialCommonBasis::MCB_World, true, {}, {});

				MIR::FValueRef P0 = Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform0];
				MIR::FValueRef P1 = Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform1];
				MIR::FValueRef P2 = Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform2];
				CoordinateValue = BuildVirtualTextureWorldToUV(Em, AbsWorldPosIndex, P0, P1, P2, EPositionOrigin::Absolute);
			}
			else
			{
				checkNoEntry();
			}
		}
	}

	// Compile the mip level for the current mip value mode
	ETextureMipValueMode TextureMipLevelMode = TMVM_None;
	MIR::FValueRef MipValue0Value;
	MIR::FValueRef MipValue1Value;
	const bool bMipValueExpressionValid = MipValue.GetTracedInput().Expression != nullptr;

	if (MipValueMode == RVTMVM_MipLevel)
	{
		TextureMipLevelMode = TMVM_MipLevel;
		MipValue0Value = bMipValueExpressionValid ? Em.Input(&MipValue) : Em.ConstantFloat(0.0f);
	}
	else if (MipValueMode == RVTMVM_MipBias)
	{
		TextureMipLevelMode = TMVM_MipBias;
		MipValue0Value = bMipValueExpressionValid ? Em.Input(&MipValue) : Em.ConstantFloat(0.0f);
	}
	else if (MipValueMode == RVTMVM_DerivativeUV || MipValueMode == RVTMVM_DerivativeWorld)
	{
		if (DDX.GetTracedInput().Expression == nullptr || DDY.GetTracedInput().Expression == nullptr)
		{
			Em.Error(TEXT("Derivative MipValueMode requires connected DDX and DDY pins."));
		}

		TextureMipLevelMode = TMVM_Derivative;
		MIR::FValueRef Ddx = Em.Input(&DDX);
		MIR::FValueRef Ddy = Em.Input(&DDY);

		if (MipValueMode == RVTMVM_DerivativeUV)
		{
			MipValue0Value = Ddx;
			MipValue1Value = Ddy;
		}
		else if (MipValueMode == RVTMVM_DerivativeWorld)
		{
			MIR::FValueRef UDdx = Em.Dot(Ddx, Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform1]);
			MIR::FValueRef VDdx = Em.Dot(Ddx, Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform2]);
			MipValue0Value = Em.Vector2(UDdx, VDdx);

			MIR::FValueRef UDdy = Em.Dot(Ddy, Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform1]);
			MIR::FValueRef VDdy = Em.Dot(Ddy, Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform2]);
			MipValue1Value = Em.Vector2(UDdy, VDdy);
		}
	}
	else if (MipValueMode == RVTMVM_RecalculateDerivatives)
	{
		// Calculate derivatives from world position.
		// This is legacy/hidden, and is better implemented in the material graph using RVTMVM_DerivativeWorld.
		TextureMipLevelMode = TMVM_Derivative;

		MIR::FValueRef WorldPos = Em.ExternalInput(MIR::EExternalInput::WorldPosition_CameraRelative);
		MIR::FValueRef WorldPositionDdx = Em.AnalyticalPartialDerivative(WorldPos, MIR::EDerivativeAxis::X);
		MIR::FValueRef UDdx = Em.Dot(WorldPositionDdx, Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform1]);
		MIR::FValueRef VDdx = Em.Dot(WorldPositionDdx, Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform2]);
		MipValue0Value = Em.Vector2(UDdx, VDdx);

		MIR::FValueRef WorldPositionDdy = Em.AnalyticalPartialDerivative(WorldPos, MIR::EDerivativeAxis::Y);
		MIR::FValueRef UDdy = Em.Dot(WorldPositionDdy, Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform1]);
		MIR::FValueRef VDdy = Em.Dot(WorldPositionDdy, Uniforms[ERuntimeVirtualTextureShaderUniform_WorldToUVTransform2]);
		MipValue1Value = Em.Vector2(UDdy, VDdy);
	}

	// We can support disabling feedback for MipLevel mode.
	const bool bForceEnableFeedback = TextureMipLevelMode != TMVM_MipLevel;

	// Compile the texture sample code
	constexpr bool bAutomaticMipViewBias = true;

	const MIR::FTextureSampleBaseAttributes SampleAttributes
	{
		.SamplerSourceMode = GetSamplerSourceMode(),
		.SamplerType = SAMPLERTYPE_VirtualMasks,
		.bEnableFeedback = bEnableFeedback || bForceEnableFeedback,
		.bIsAdaptive = bAdaptive
	};

	MIR::FValueRef SampleCodeValues[RuntimeVirtualTexture::MaxTextureLayers];
	for (int32 TexureLayerIndex = 0; TexureLayerIndex < TextureLayerCount; TexureLayerIndex++)
	{
		switch (TextureMipLevelMode)
		{
			case TMVM_None:
				SampleCodeValues[TexureLayerIndex] = Em.TextureSample(TextureObjects[TexureLayerIndex], CoordinateValue, bAutomaticMipViewBias, SampleAttributes);
				break;

			case TMVM_MipBias:
				SampleCodeValues[TexureLayerIndex] = Em.TextureSampleBias(TextureObjects[TexureLayerIndex], CoordinateValue, MipValue0Value, bAutomaticMipViewBias, SampleAttributes);
				break;

			case TMVM_MipLevel:
				SampleCodeValues[TexureLayerIndex] = Em.TextureSampleLevel(TextureObjects[TexureLayerIndex], CoordinateValue, MipValue0Value, bAutomaticMipViewBias, SampleAttributes);
				break;

			case TMVM_Derivative:
				SampleCodeValues[TexureLayerIndex] = Em.TextureSampleGrad(TextureObjects[TexureLayerIndex], CoordinateValue, MipValue0Value, MipValue1Value, bAutomaticMipViewBias, SampleAttributes);
				break;

			default:
				UE_MIR_UNREACHABLE();
		}
	}

	UE_MIR_CHECKPOINT(Em);

	// Compile unpacking code
	for (int32 OutputIndex = 0; OutputIndex < 8; ++OutputIndex)
	{
		// Calculate the virtual texture layer and sampling/unpacking functions for this output
		// Fallback to a sensible default value if the output isn't valid for the bound virtual texture
		FRuntimeVirtualTextureUnpackProperties UnpackProperties;
		if (!GetRVTUnpackProperties(OutputIndex, bIsVirtualTextureValid, UnpackProperties))
		{
			Em.Errorf(TEXT("Failed to retrieve unpack properties from RuntimeVirtualTexture for output pin %d"), OutputIndex);
			return;
		}

		if (UnpackProperties.ConstantVector.IsEmpty())
		{
			if (UnpackProperties.UnpackType != EVirtualTextureUnpackType::None)
			{
				MIR::FValueRef P0 = Uniforms[ERuntimeVirtualTextureShaderUniform_WorldHeightUnpack];
				Em.Output(OutputIndex,
					BuildVirtualTextureUnpack(Em, SampleCodeValues[0], SampleCodeValues[1], SampleCodeValues[2], P0, UnpackProperties.UnpackType));
			}
			else
			{
				Em.Output(OutputIndex,
					Em.Swizzle(SampleCodeValues[UnpackProperties.UnpackTarget], MIR::FSwizzleMask(
						(UnpackProperties.UnpackMask     ) & 1,
						(UnpackProperties.UnpackMask >> 1) & 1,
						(UnpackProperties.UnpackMask >> 2) & 1,
						(UnpackProperties.UnpackMask >> 3) & 1)));
			}
		}
		else
		{
			Em.Output(OutputIndex, BuildConstantVector(Em, UnpackProperties.ConstantVector));
		}
	}
}

#endif // WITH_EDITOR

void UMaterialExpressionTime::Build(MIR::FEmitter& Em)
{
	MIR::FType ScalarFloatType = MIR::FType::MakeScalar(MIR::EScalarKind::Float);

	// When pausing the game is ignored for this time expression, use real-time instead of game-time.
	if (!bOverride_Period)
	{
		FStringView InlinedCode = bIgnorePause ? TEXTVIEW("View.<PREVFRAME>RealTime") : TEXTVIEW("View.<PREVFRAME>GameTime");
		Em.Output(0, Em.InlineHLSL(ScalarFloatType, InlinedCode, {}, MIR::EValueFlags::SubstituteTags));
	}
	else if (Period == 0.0f)
	{
		Em.Output(0, Em.ConstantFloat(0.0f));
	}
	else
	{
		// Note: Don't use IR intrinsic for Fmod() here to avoid conversion to fp16 on mobile.
		// We want full 32 bit float precision until the fmod when using a period.
		FValueRef PeriodValue = Em.ConstantFloat(Period);
		FStringView InlinedCode = bIgnorePause ? TEXTVIEW("fmod(View.<PREVFRAME>RealTime, $0)") : TEXTVIEW("fmod(View.<PREVFRAME>GameTime, $0)");
		Em.Output(0, Em.InlineHLSL(ScalarFloatType, InlinedCode, { PeriodValue }, MIR::EValueFlags::SubstituteTags));
	}
}

// Returns true if the specified value is a constant power of two (scalar or vector).
static bool IsConstFloatOfPow2Expression(FValueRef TileScaleIndexValue)
{
	using namespace UE::MaterialTranslatorUtils;
	if (MIR::FConstant* ConstIndex = TileScaleIndexValue->As<MIR::FConstant>())
	{
		return IsFloatPowerOfTwo(ConstIndex->Float);
	}
	else if (MIR::FComposite* Composite = TileScaleIndexValue->As<MIR::FComposite>())
	{
		for (MIR::FValue* Component : Composite->GetComponents())
		{
			MIR::FConstant* ConstComponent = Component->As<MIR::FConstant>();
			if (!ConstComponent || !IsFloatPowerOfTwo(ConstComponent->Float))
			{
				return false;
			}
		}
		return true;
	}
	return false;
}

static FValueRef EmitPeriodicWorldPositionOrigin(MIR::FEmitter& Em, FValueRef TileScaleIndexValue)
{
	FStringView PeriodicWorldOriginFunctionName = IsConstFloatOfPow2Expression(TileScaleIndexValue) ? TEXTVIEW("GetPeriodicWorldOrigin_Pow2($0)") : TEXTVIEW("GetPeriodicWorldOrigin($0)");
	return Em.InlineHLSL(TileScaleIndexValue->Type, PeriodicWorldOriginFunctionName, { TileScaleIndexValue });
}

// Emits inline HLSL from an external code declaration that does not take any parameters.
static FValueRef EmitFixedExternalCode(MIR::FEmitter& Em, FName InExternalCodeIdentifier)
{
	return Em.InlineHLSL(MaterialExternalCodeRegistry::Get().FindExternalCode(InExternalCodeIdentifier), {}, MIR::EValueFlags::SubstituteTags);
}

static FValueRef EmitMatrixCastTo3x3(MIR::FEmitter& Em, FValueRef MatrixValue)
{
	return Em.InlineHLSL(MIR::FType::MakeFloat(3, 3), MatrixValue->Type.IsDouble() ? TEXTVIEW("DFToFloat3x3($0)") : TEXTVIEW("(float3x3)$0"), { MatrixValue });
}

static FValueRef EmitMatrixMultiply(MIR::FEmitter& Em, FValueRef VectorValue, FValueRef MatrixValue, bool bHasWComponent)
{
	return bHasWComponent
		? Em.Swizzle(Em.MatrixMultiply(VectorValue, MatrixValue), MIR::FSwizzleMask::XYZ())  // mul(Float4(V, 1.0), V).xyz
		: Em.MatrixMultiply(VectorValue, EmitMatrixCastTo3x3(Em, MatrixValue)); // mul(V, (Float3x3)M)
}

static FValueRef EmitMultiplyTransposeMatrix(MIR::FEmitter& Em, FValueRef MatrixValue, FValueRef VectorValue, bool bHasWComponent)
{
	// TODO: this should be removed when the Transpose operator is added to the translator.
	return bHasWComponent
		? Em.Swizzle(Em.MatrixMultiply(MatrixValue, VectorValue), MIR::FSwizzleMask::XYZ())  // mul(M, Float4(V, 1.0)).xyz
		: Em.MatrixMultiply(EmitMatrixCastTo3x3(Em, MatrixValue), VectorValue); // mul((Float3x3)M, V)
}

static FValueRef EmitMultiplyTranslatedMatrix(MIR::FEmitter& Em, FValueRef VectorValue, FValueRef MatrixPreTranslation, bool bHasWComponent)
{
	if (bHasWComponent)
	{
		// mul(Float4(V, 1.0), DFFastToTranslatedWorld(M, ResolvedView.PreViewTranslation))
		MatrixPreTranslation = Em.InlineHLSL(MIR::FType::MakeFloat(4, 4), TEXTVIEW("DFFastToTranslatedWorld($0, ResolvedView.<PREV>PreViewTranslation)"), { MatrixPreTranslation }, MIR::EValueFlags::SubstituteTags);
		return Em.MatrixMultiply(Em.Vector4(VectorValue, Em.ConstantOne(MIR::EScalarKind::Float)), MatrixPreTranslation);
	}
	else
	{
		// mul(V, DFToFloat3x3(M))
		return Em.MatrixMultiply(VectorValue, EmitMatrixCastTo3x3(Em, MatrixPreTranslation));
	}
}

static FValueRef EmitMultiplyLWCMatrix(MIR::FEmitter& Em, FValueRef VectorValue, FValueRef MatrixValue, bool bHasWComponent, bool bDemote)
{
	return bHasWComponent
		? Em.InlineHLSL(VectorValue->Type, bDemote ? TEXTVIEW("WSMultiplyDemote($0, $1)") : TEXTVIEW("WSMultiply($0, $1)"), { VectorValue, MatrixValue })
		: Em.InlineHLSL(VectorValue->Type, TEXTVIEW("WSMultiplyVector($0, $1)"), { VectorValue, MatrixValue });
}

static FValueRef EmitTransformVectorBase(
	MIR::FEmitter& Em, FValueRef InputValue, EMaterialCommonBasis TransformSourceBasis, EMaterialCommonBasis TransformDestBasis,
	bool bIsPositionTransform, MIR::FValueRef PeriodicWorldTileSizeValue, MIR::FValueRef FirstPersonInterpolationAlphaValue)
{
	// Construct float3(0,0,x) out of the input if it is a scalar
	// This way artists can plug in a scalar and it will be treated as height, or a vector displacement
	if (TransformSourceBasis == MCB_Tangent && InputValue->Type.IsScalar())
	{
		FValueRef Zero = Em.ConstantZero(MIR::EScalarKind::Float);
		InputValue = Em.Vector3(Zero, Zero, InputValue);
	}
	else
	{
		InputValue = Em.CastToVector(InputValue, 3);
	}

	if (!InputValue.IsValid())
	{
		return InputValue.ToPoison();
	}

	MIR::FType ResultType = (TransformDestBasis == MCB_World && bIsPositionTransform) ? MIR::FType::MakeDoubleVector(3) : MIR::FType::MakeFloatVector(3);

	EMaterialCommonBasis IntermediaryBasis = MCB_World;

	switch (TransformSourceBasis)
	{
		case MCB_Tangent:
			check(!bIsPositionTransform);
			if (TransformDestBasis == MCB_World)
			{
				return Em.MatrixMultiply(InputValue, EmitFixedExternalCode(Em, TEXT("TangentToWorld")));
			}
			// else use MCB_World as intermediary basis
			break;

		case MCB_Local:
			switch (TransformDestBasis)
			{
				case MCB_World:
				{
					FStringView Code = bIsPositionTransform ? TEXTVIEW("TransformLocalPositionTo<PREV>World(Parameters, $0)") : TEXTVIEW("TransformLocalVectorTo<PREV>World(Parameters, $0)");
					return Em.InlineHLSL(ResultType, Code, { InputValue }, MIR::EValueFlags::SubstituteTags);
				}

				case MCB_TranslatedWorld:
					if (bIsPositionTransform)
					{
						return EmitMultiplyTranslatedMatrix(Em, InputValue, EmitFixedExternalCode(Em, TEXT("GetLocalToWorldDF")), bIsPositionTransform);
					}
					break;

				case MCB_PeriodicWorld:
				case MCB_FirstPerson:
					IntermediaryBasis = MCB_TranslatedWorld;
					break;

				default:
					// else use MCB_World as intermediary basis
					break;
			}
			break;

		case MCB_TranslatedWorld:
			switch (TransformDestBasis)
			{
				case MCB_World:
					return bIsPositionTransform
						? Em.Subscript(InputValue, EmitFixedExternalCode(Em, TEXT("GetPreViewTranslation")))
						: InputValue;

				case MCB_Camera:
					return EmitMatrixMultiply(Em, InputValue, EmitFixedExternalCode(Em, TEXT("TranslatedWorldToCameraView")), bIsPositionTransform);

				case MCB_View:
					return EmitMatrixMultiply(Em, InputValue, EmitFixedExternalCode(Em, TEXT("TranslatedWorldToView")), bIsPositionTransform);

				case MCB_Tangent:
					return EmitMultiplyTransposeMatrix(Em, EmitFixedExternalCode(Em, TEXT("TangentToWorld")), InputValue, bIsPositionTransform);

				case MCB_Local:
					return EmitMultiplyTranslatedMatrix(Em, InputValue, EmitFixedExternalCode(Em, TEXT("GetWorldToLocalDF")), bIsPositionTransform);

				case MCB_MeshParticle:
					return EmitMultiplyTranslatedMatrix(Em, InputValue, EmitFixedExternalCode(Em, TEXT("WorldToParticle")), bIsPositionTransform);

				case MCB_Instance:
					return EmitMultiplyTranslatedMatrix(Em, InputValue, EmitFixedExternalCode(Em, TEXT("GetWorldToInstanceDF")), bIsPositionTransform);

				case MCB_PeriodicWorld:
					if (!PeriodicWorldTileSizeValue.IsValid())
					{
						Em.Error(TEXT("Missing periodic world tile size"));
						return Em.Poison();
					}
					return Em.Subtract(InputValue, EmitPeriodicWorldPositionOrigin(Em, PeriodicWorldTileSizeValue));

				case MCB_FirstPerson:
				{
					if (!FirstPersonInterpolationAlphaValue.IsValid())
					{
						Em.Error(TEXT("Missing first person interpolation alpha"));
						return Em.Poison();
					}
					// The first person transform is actually a 3x3 matrix and can therefore be used for derivatives as well.
					MIR::FValueRef LerpAlphaClampedIndexValue = Em.Saturate(Em.CastToFloat(FirstPersonInterpolationAlphaValue, 1));
					return Em.InlineHLSL(ResultType, TEXT("TransformTo<PREVIOUS>FirstPerson($0, $1)"), { InputValue, LerpAlphaClampedIndexValue }, MIR::EValueFlags::SubstituteTags);
				}

				default:
					break; // else use MCB_World as intermediary basis
			}
			break;

		case MCB_World:
			switch (TransformDestBasis)
			{
				case MCB_Tangent:
					return EmitMultiplyTransposeMatrix(Em, EmitFixedExternalCode(Em, TEXT("TangentToWorld")), InputValue, bIsPositionTransform);

				case MCB_Local:
					return EmitMultiplyLWCMatrix(Em, InputValue, EmitFixedExternalCode(Em, TEXT("GetWorldToLocal")), bIsPositionTransform, true);

				case MCB_TranslatedWorld:
					return bIsPositionTransform
						? Em.Add(InputValue, EmitFixedExternalCode(Em, TEXT("GetPreViewTranslation")))
						: InputValue;

				case MCB_MeshParticle:
					return EmitMultiplyLWCMatrix(Em, InputValue, EmitFixedExternalCode(Em, TEXT("GetWorldToParticle")), bIsPositionTransform, true);

				case MCB_Instance:
					return EmitMultiplyLWCMatrix(Em, InputValue, EmitFixedExternalCode(Em, TEXT("GetWorldToInstance")), bIsPositionTransform, true);

				default:
					// else use MCB_TranslatedWorld as intermediary basis
					IntermediaryBasis = MCB_TranslatedWorld;
					break;
			}
			break;

		case MCB_Camera:
			if (TransformDestBasis == MCB_TranslatedWorld)
			{
				return EmitMatrixMultiply(Em, InputValue, EmitFixedExternalCode(Em, TEXT("CameraViewToTranslatedWorld")), bIsPositionTransform);
			}
			
			// else use MCB_TranslatedWorld as intermediary basis
			IntermediaryBasis = MCB_TranslatedWorld;
			break;

		case MCB_View:
			if (TransformDestBasis == MCB_TranslatedWorld)
			{
				return EmitMatrixMultiply(Em, InputValue, EmitFixedExternalCode(Em, TEXT("ViewToTranslatedWorld")), bIsPositionTransform);
			}

			// else use MCB_TranslatedWorld as intermediary basis
			IntermediaryBasis = MCB_TranslatedWorld;
			break;

		case MCB_MeshParticle:
			switch (TransformDestBasis)
			{
				case MCB_World:
					return EmitMultiplyLWCMatrix(Em, InputValue, EmitFixedExternalCode(Em, TEXT("GetParticleToWorld")), bIsPositionTransform, false);

				case MCB_TranslatedWorld:
					return EmitMultiplyTranslatedMatrix(Em, InputValue, EmitFixedExternalCode(Em, TEXT("ParticleToWorld")), bIsPositionTransform);

				case MCB_PeriodicWorld:
				case MCB_FirstPerson:
					IntermediaryBasis = MCB_TranslatedWorld;
					break;

				default:
					break; // use World as an intermediary base
			}
			break;

		case MCB_Instance:
			switch(TransformDestBasis)
			{
				case MCB_World:
					return EmitMultiplyLWCMatrix(Em, InputValue, EmitFixedExternalCode(Em, TEXT("GetInstanceToWorld")), bIsPositionTransform, false);

				case MCB_TranslatedWorld:
					return EmitMultiplyTranslatedMatrix(Em, InputValue, EmitFixedExternalCode(Em, TEXT("GetInstanceToWorldDF")), bIsPositionTransform);

				case MCB_PeriodicWorld:
				case MCB_FirstPerson:
					IntermediaryBasis = MCB_TranslatedWorld;
					break;

				default:
					break; // use World as an intermediary base
			}
			break;

		case MCB_PeriodicWorld:
			switch (TransformDestBasis)
			{
				case MCB_TranslatedWorld:
					if (!PeriodicWorldTileSizeValue.IsValid())
					{
						Em.Error(TEXT("Missing periodic world tile size"));
						return Em.Poison();
					}
					return Em.Add(InputValue, EmitPeriodicWorldPositionOrigin(Em, PeriodicWorldTileSizeValue));

				default:
					// else use MCB_TranslatedWorld as intermediary basis
					IntermediaryBasis = MCB_TranslatedWorld;
					break;
			}
			break;

		case MCB_FirstPerson:
			UE_MIR_UNREACHABLE(); // MCB_FirstPerson is not supported as a source basis. This should've been caught earlier in validation.

		default:
			UE_MIR_UNREACHABLE();
	}

	// Check intermediary basis so we don't have infinite recursion
	check(IntermediaryBasis != TransformSourceBasis);
	check(IntermediaryBasis != TransformDestBasis);

	// Use intermediary basis
	FValueRef IntermediaryBasisA = EmitTransformVectorBase(Em, InputValue, TransformSourceBasis, IntermediaryBasis, bIsPositionTransform, PeriodicWorldTileSizeValue, FirstPersonInterpolationAlphaValue);
	FValueRef IntermediaryBasisB = EmitTransformVectorBase(Em, IntermediaryBasisA, IntermediaryBasis, TransformDestBasis, bIsPositionTransform, PeriodicWorldTileSizeValue, FirstPersonInterpolationAlphaValue);

	return IntermediaryBasisB;
}

static void BuildTransformVectorBase(
	MIR::FEmitter& Em, const FExpressionInput* Input, EMaterialCommonBasis TransformSourceBasis, EMaterialCommonBasis TransformDestBasis,
	bool bIsPositionTransform, MIR::FValueRef PeriodicWorldTileSizeValue, MIR::FValueRef FirstPersonInterpolationAlphaValue)
{
	FValueRef InputValue = Em.CheckIsPrimitive(Em.Input(Input));
	UE_MIR_CHECKPOINT(Em);

	FValueRef OutputValue = EmitTransformVectorBase(Em, InputValue, TransformSourceBasis, TransformDestBasis, bIsPositionTransform, PeriodicWorldTileSizeValue, FirstPersonInterpolationAlphaValue);

	if (TransformSourceBasis == MCB_World && bIsPositionTransform)
	{
		if (!OutputValue->Type.IsDouble())
		{
			OutputValue = Em.CastToScalarKind(OutputValue, MIR::EScalarKind::Double);
		}
	}
	else if (OutputValue->Type.IsDouble())
	{
		OutputValue = Em.CastToFloatKind(OutputValue);
	}

	Em.Output(0, OutputValue);
}

void UMaterialExpressionTransform::Build(MIR::FEmitter& Em)
{
	const EMaterialCommonBasis TransformSourceBasis = UE::MaterialTranslatorUtils::GetMaterialCommonBasis(TransformSourceType);
	const EMaterialCommonBasis TransformDestBasis = UE::MaterialTranslatorUtils::GetMaterialCommonBasis(TransformType);

	constexpr bool bIsPositionTransform = false;
	BuildTransformVectorBase(Em, &Input, TransformSourceBasis, TransformDestBasis, bIsPositionTransform, {}, {});
}

void UMaterialExpressionTransformPosition::Build(MIR::FEmitter& Em)
{
	MIR::FValueRef PeriodicWorldTileSizeValue, FirstPersonInterpolationAlphaValue;
	if (TransformSourceType == TRANSFORMPOSSOURCE_PeriodicWorld || TransformType == TRANSFORMPOSSOURCE_PeriodicWorld)
	{
		PeriodicWorldTileSizeValue = Em.InputDefaultFloat(&PeriodicWorldTileSize, ConstPeriodicWorldTileSize);
	}
	if (TransformSourceType == TRANSFORMPOSSOURCE_FirstPersonTranslatedWorld || TransformType == TRANSFORMPOSSOURCE_FirstPersonTranslatedWorld)
	{
		FirstPersonInterpolationAlphaValue = Em.InputDefaultFloat(&FirstPersonInterpolationAlpha, ConstFirstPersonInterpolationAlpha);
	}

	const EMaterialCommonBasis TransformSourceBasis = UE::MaterialTranslatorUtils::GetMaterialCommonBasis(TransformSourceType);
	const EMaterialCommonBasis TransformDestBasis = UE::MaterialTranslatorUtils::GetMaterialCommonBasis(TransformType);

	constexpr bool bIsPositionTransform = true;
	BuildTransformVectorBase(Em, &Input, TransformSourceBasis, TransformDestBasis, bIsPositionTransform, PeriodicWorldTileSizeValue, FirstPersonInterpolationAlphaValue);
}

void UMaterialExpressionReroute::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.TryInput(&Input));
}

void UMaterialExpressionNamedRerouteDeclaration::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.TryInput(&Input));
}

void UMaterialExpressionNamedRerouteUsage::Build(MIR::FEmitter& Em)
{
	if (!IsDeclarationValid())
	{
		Em.Error(TEXT("Named reroute expression does not have a valid declaration."));
	}
	Em.Output(0, Em.TryInput(&Declaration->Input));
}

void UMaterialExpressionClamp::Build(MIR::FEmitter& Em)
{
	FValueRef InputValue = Em.Input(&Input);
	FValueRef MinValue = Em.InputDefaultFloat(&Min, MinDefault);
	FValueRef MaxValue = Em.InputDefaultFloat(&Max, MaxDefault);

	FValueRef OutputValue = nullptr;
	if (ClampMode == CMODE_Clamp)
	{
		OutputValue = Em.Clamp(InputValue, MinValue, MaxValue);
	}
	else if (ClampMode == CMODE_ClampMin)
	{
		OutputValue = Em.Max(InputValue, MinValue);
	}
	else if (ClampMode == CMODE_ClampMax)
	{
		OutputValue = Em.Min(InputValue, MaxValue);
	}

	Em.Output(0, OutputValue);
}


void BuildTernaryArithmeticOperator(MIR::FEmitter& Em, MIR::EOperator Op, FExpressionInput* A, float ConstA, FExpressionInput* B, float ConstB, FExpressionInput* C, float ConstC)
{
	FValueRef ValueA = Em.InputDefaultFloat(A, ConstA);
	FValueRef ValueB = Em.InputDefaultFloat(B, ConstB);
	FValueRef ValueC = Em.InputDefaultFloat(C, ConstC);
	Em.Output(0, Em.Operator(Op, ValueA, ValueB, ValueC));
}

void UMaterialExpressionColorRamp::Build(MIR::FEmitter& Em)
{	
	// Check that the ColorCurve is set
	if (!ColorCurve)
	{
		Em.Errorf(TEXT("Missing ColorCurve"));
		return;
	}

	FValueRef InputValue = Em.CastToFloat(Em.InputDefaultFloat(&Input, ConstInput), 1);

	// If the input is constant, evaluate at compile time.
	if (const MIR::FConstant* Constant = MIR::As<MIR::FConstant>(InputValue))
	{
		FLinearColor ColorValue = ColorCurve->GetLinearColorValue(Constant->Float);
		Em.Output(0, Em.ConstantFloat4(ColorValue));
		return;
	}

	// Helper lambda to evaluate a curve
	auto EvaluateCurve = [&Em, InputValue](const FRichCurve& Curve) -> FValueRef
		{
			const int32 NumKeys = Curve.Keys.Num();

			switch (NumKeys)
			{
				case 0:
					return Em.ConstantFloat(0.0f);

				case 1:
					return Em.ConstantFloat(Curve.Keys[0].Value);

				case 2:
				{
					float StartTime = Curve.Keys[0].Time;
					float EndTime = Curve.Keys[1].Time;
					float StartValue = Curve.Keys[0].Value;
					float EndValue = Curve.Keys[1].Value;

					FValueRef TimeDelta = Em.ConstantFloat(EndTime - StartTime);
					FValueRef TimeDiff = Em.Subtract(InputValue, Em.ConstantFloat(StartTime));
					FValueRef Fraction = Em.Divide(TimeDiff, TimeDelta);

					return Em.Lerp(Em.ConstantFloat(StartValue), Em.ConstantFloat(EndValue), Fraction);
				}
			}

			FValueRef InValueVec = Em.Vector4(InputValue, InputValue, InputValue, InputValue);

			FValueRef Result = Em.ConstantFloat(Curve.Keys[0].Value);
			int32 i = 0;

			// Use vector operations for segments of 4
			for (; i < NumKeys - 4; i += 4)
			{
				FVector4f StartTimeVector(
					Curve.Keys[i].Time,
					Curve.Keys[i + 1].Time,
					Curve.Keys[i + 2].Time,
					Curve.Keys[i + 3].Time
				);
				FValueRef StartTimeVec = Em.ConstantFloat4(StartTimeVector);

				FVector4f EndTimeVector(
					Curve.Keys[i + 1].Time,
					Curve.Keys[i + 2].Time,
					Curve.Keys[i + 3].Time,
					Curve.Keys[i + 4].Time
				);
				FValueRef EndTimeVec = Em.ConstantFloat4(EndTimeVector);

				FVector4f StartValueVector(
					Curve.Keys[i].Value,
					Curve.Keys[i + 1].Value,
					Curve.Keys[i + 2].Value,
					Curve.Keys[i + 3].Value
				);
				FValueRef StartValueVec = Em.ConstantFloat4(StartValueVector);

				FVector4f EndValueVector(
					Curve.Keys[i + 1].Value,
					Curve.Keys[i + 2].Value,
					Curve.Keys[i + 3].Value,
					Curve.Keys[i + 4].Value
				);
				FValueRef EndValueVec = Em.ConstantFloat4(EndValueVector);

				FValueRef TimeDeltaVec = Em.Subtract(EndTimeVec, StartTimeVec);
				FValueRef ValueDeltaVec = Em.Subtract(EndValueVec, StartValueVec);

				FValueRef TimeDiffVec = Em.Subtract(InValueVec, StartTimeVec);
				FValueRef FractionVec = Em.Divide(TimeDiffVec, TimeDeltaVec);
				FValueRef SatFractionVec = Em.Saturate(FractionVec);
				FValueRef ContributionVec = Em.Multiply(ValueDeltaVec, SatFractionVec);

				FVector4f Ones(1.0f, 1.0f, 1.0f, 1.0f);
				FValueRef OnesVec = Em.ConstantFloat4(Ones);
				FValueRef ContributionSum = Em.Dot(ContributionVec, OnesVec);

				Result = Em.Add(Result, ContributionSum);
			}
			
			// Use scalar operations for the remaining keys
			for (; i < NumKeys - 1; i++)
			{
				float StartTime = Curve.Keys[i].Time;
				float EndTime = Curve.Keys[i + 1].Time;
				float StartValue = Curve.Keys[i].Value;
				float EndValue = Curve.Keys[i + 1].Value;

				FValueRef TimeDelta = Em.ConstantFloat(EndTime - StartTime);
				FValueRef ValueDelta = Em.ConstantFloat(EndValue - StartValue);
				FValueRef TimeDiff = Em.Subtract(InputValue, Em.ConstantFloat(StartTime));
				FValueRef Fraction = Em.Divide(TimeDiff, TimeDelta);
				FValueRef SatFraction = Em.Saturate(Fraction);
				FValueRef Contribution = Em.Multiply(ValueDelta, SatFraction);
				Result = Em.Add(Result, Contribution);
			}
			return Result;
		};

	FValueRef Red = EvaluateCurve(ColorCurve->FloatCurves[0]);
	FValueRef Green = EvaluateCurve(ColorCurve->FloatCurves[1]);
	FValueRef Blue = EvaluateCurve(ColorCurve->FloatCurves[2]);
	FValueRef Alpha = EvaluateCurve(ColorCurve->FloatCurves[3]);

	FValueRef FinalVector = Em.Vector4(Red, Green, Blue, Alpha);
	Em.Output(0, FinalVector);
}

void UMaterialExpressionInverseLinearInterpolate::Build(MIR::FEmitter& Em)
{
	FValueRef ValueA = Em.InputDefaultFloat(&A, ConstA);
	FValueRef ValueB = Em.InputDefaultFloat(&B, ConstB);
	FValueRef ValueC = Em.InputDefaultFloat(&Value, ConstValue);
	FValueRef Result = Em.Divide(Em.CastToFloatKind(Em.Subtract(ValueC, ValueA)), Em.CastToFloatKind(Em.Subtract(ValueB, ValueA)));
	if (bClampResult)
	{
		Result = Em.Saturate(Result);
	}
	Em.Output(0, Result);
}

void UMaterialExpressionLinearInterpolate::Build(MIR::FEmitter& Em)
{
	BuildTernaryArithmeticOperator(Em, MIR::TO_Lerp, &A, ConstA, &B, ConstB, &Alpha, ConstAlpha);
}

void UMaterialExpressionSmoothStep::Build(MIR::FEmitter& Em)
{
	BuildTernaryArithmeticOperator(Em, MIR::TO_Smoothstep, &Min, ConstMin, &Max, ConstMax, &Value, ConstValue);
}

void UMaterialExpressionConvert::Build(MIR::FEmitter& Em)
{
	TArray<FValueRef, TInlineAllocator<8>> InputValues;
	InputValues.Init(nullptr, ConvertInputs.Num());

	for (int32 OutputIndex = 0; OutputIndex < ConvertOutputs.Num(); ++OutputIndex)
	{
		const FMaterialExpressionConvertOutput& ConvertOutput = ConvertOutputs[OutputIndex];
		FValueRef OutComponents[4] = { nullptr, nullptr, nullptr, nullptr };

		for (const FMaterialExpressionConvertMapping& Mapping : ConvertMappings)
		{
			// We only care about mappings relevant to this output
			if (Mapping.OutputIndex != OutputIndex)
			{
				continue;
			}

			const int32 OutputComponentIndex = Mapping.OutputComponentIndex;
			if (!IsValidComponentIndex(OutputComponentIndex, ConvertOutput.Type))
			{
				Em.Errorf(TEXT("Convert mapping's output component `%d` is invalid."), OutputComponentIndex);
				continue;
			}

			const int32 InputIndex = Mapping.InputIndex;
			if (!ConvertInputs.IsValidIndex(InputIndex))
			{
				Em.Errorf(TEXT("Convert mapping's input `%d` is invalid."), InputIndex);
				continue;
			}

			FMaterialExpressionConvertInput& ConvertInput = ConvertInputs[InputIndex];
			const int32 InputComponentIndex = Mapping.InputComponentIndex;
			if (!IsValidComponentIndex(InputComponentIndex, ConvertInput.Type))
			{
				Em.Errorf(TEXT("Convert mapping's input component `%d` is invalid."), InputComponentIndex);
				continue;
			}

			// If not already emitted, read the input value, cast it to the specified input
			// type and cache it into an array, as each input could be used multiple times
			// by output values.
			if (!InputValues[InputIndex])
			{
				// Read the input's value (or read float zero if disconnected).
				InputValues[InputIndex] = Em.InputDefaultFloat4(&ConvertInput.ExpressionInput, ConvertInput.DefaultValue);

				// Expect type to be primitive.
				TOptional<MIR::FPrimitive> InputPrimitiveType = InputValues[InputIndex]->Type.AsPrimitive();
				if (!InputPrimitiveType)
				{
					Em.Errorf(TEXT("Input `%d` of type `%s` is not primitive."), InputComponentIndex, *InputValues[InputIndex]->Type.GetSpelling());
					continue;
				}

				// Determine the target type.
				MIR::FType InputType = MIR::FType::MakeVector(InputPrimitiveType->ScalarKind, MaterialExpressionConvertType::GetComponentCount(ConvertInput.Type));

				// Cast the input value to the target type.
				InputValues[InputIndex] = Em.Cast(InputValues[InputIndex], InputType);
			}

			// Subscript the input value to the specified component index.
			OutComponents[OutputComponentIndex] = Em.Subscript(InputValues[InputIndex], InputComponentIndex);
		}

		const int32 OutputNumComponents = MaterialExpressionConvertType::GetComponentCount(ConvertOutput.Type);

		// For any component still unset, give assign it to the default value.
		for (int32 OutputComponentIndex = 0; OutputComponentIndex < OutputNumComponents; ++OutputComponentIndex)
		{
			// If we don't have a compile result here, default it to a that component's default value
			if (!OutComponents[OutputComponentIndex])
			{
				OutComponents[OutputComponentIndex] = Em.ConstantFloat(ConvertOutput.DefaultValue.Component(OutputComponentIndex));
			}
		}

		// Finally create the output dimensional value by combining the output components.
		FValueRef OutValue;
		switch (OutputNumComponents)
		{
			case 1: OutValue = OutComponents[0]; break;
			case 2: OutValue = Em.Vector2(OutComponents[0], OutComponents[1]); break;
			case 3: OutValue = Em.Vector3(OutComponents[0], OutComponents[1], OutComponents[2]); break;
			case 4: OutValue = Em.Vector4(OutComponents[0], OutComponents[1], OutComponents[2], OutComponents[3]); break;
			
			default:
				OutValue = Em.Poison();
				Em.Errorf(TEXT("Convert node has an invalid component count of %d"), OutputNumComponents);
				break;
		}

		Em.Output(OutputIndex, OutValue);
	}
}

static FValueRef BuildViewProperty(MIR::FEmitter& Em, EMaterialExposedViewProperty InProperty, bool bInvProperty = false)
{
	check(InProperty < MEVP_MAX);

	const FMaterialExposedViewPropertyMeta& PropertyMeta = MaterialExternalCodeRegistry::Get().GetExternalViewPropertyCode(InProperty);
	const bool bHasCustomInverseCode = PropertyMeta.InvPropertyCode != nullptr;

	FStringView HLSLCode = bInvProperty && bHasCustomInverseCode ? PropertyMeta.InvPropertyCode : PropertyMeta.PropertyCode;
	MIR::FType HLSLCodeType = MIR::FType::FromMaterialValueType(PropertyMeta.Type);

	FValueRef Result = Em.InlineHLSL(HLSLCodeType, HLSLCode, {}, MIR::EValueFlags::SubstituteTags);

	// CastToNonLWCIfDisabled
	TOptional<MIR::FPrimitive> PrimitiveType = HLSLCodeType.AsPrimitive();
	if (PrimitiveType && PrimitiveType->IsDouble() && !UE::MaterialTranslatorUtils::IsLWCEnabled())
	{
		Result = Em.CastToFloatKind(Result);
	}

	// Fall back to compute the property's inverse from PropertyCode, if no custom inverse
	if (bInvProperty && !bHasCustomInverseCode)
	{
		Result = Em.Divide(Em.ConstantFloat(1.0f), Result);
	}

	return Result;
}

void UMaterialExpressionViewProperty::Build(MIR::FEmitter& Em)
{
	for (int32 OutputIndex = 0; OutputIndex < 2; ++OutputIndex)
	{
		const bool bInvProperty = OutputIndex == 1;
		Em.Output(OutputIndex, BuildViewProperty(Em, Property, bInvProperty));
	}
}

void UMaterialExpressionViewSize::Build(MIR::FEmitter& Em)
{
	Em.Output(0, BuildViewProperty(Em, MEVP_ViewSize));
}

void UMaterialExpressionSceneTexelSize::Build(MIR::FEmitter& Em)
{
	// To make sure any material that were correctly handling BufferUV != ViewportUV, we just lie to material
	// to make it believe ViewSize == BufferSize, so they are still compatible with SceneTextureLookup().
	Em.Output(0, BuildViewProperty(Em, MEVP_ViewSize, true));
}

void UMaterialExpressionCameraPositionWS::Build(MIR::FEmitter& Em)
{
	Em.Output(0, BuildViewProperty(Em, MEVP_WorldSpaceCameraPosition));
}

void UMaterialExpressionPixelNormalWS::Build(MIR::FEmitter& Em)
{
	FValueRef Output = Em.InlineHLSL(MIR::FType::MakeFloatVector(3), TEXTVIEW("Parameters.WorldNormal"), {}, MIR::EValueFlags::None, MIR::EGraphProperties::ReadsPixelNormal);
	Em.Output(0, Output);
}

void UMaterialExpressionDDX::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.PartialDerivative(Em.Input(&Value), MIR::EDerivativeAxis::X));
}

void UMaterialExpressionDDY::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.PartialDerivative(Em.Input(&Value), MIR::EDerivativeAxis::Y));
}

static constexpr MIR::EOperator MaterialExpressionOperatorToMIR(EMaterialExpressionOperatorKind Operator)
{
	return static_cast<MIR::EOperator>(static_cast<uint32>(Operator) + 1);
}

// Checks to make sure the two enums are aligned.
static_assert(MaterialExpressionOperatorToMIR(EMaterialExpressionOperatorKind::BitwiseNot) == MIR::UO_BitwiseNot);
static_assert(MaterialExpressionOperatorToMIR(EMaterialExpressionOperatorKind::Sign) == MIR::UO_Sign);
static_assert(MaterialExpressionOperatorToMIR(EMaterialExpressionOperatorKind::BitwiseAnd) == MIR::BO_BitwiseAnd);
static_assert(MaterialExpressionOperatorToMIR(EMaterialExpressionOperatorKind::Smoothstep) == MIR::TO_Smoothstep);

uint32 GetMaterialExpressionOperatorArity(EMaterialExpressionOperatorKind Operator)
{
	return MIR::GetOperatorArity(MaterialExpressionOperatorToMIR(Operator));
}

void UMaterialExpressionOperator::Build(MIR::FEmitter& Em)
{
	FValueRef AValue = Em.InputDefaultFloat(&DynamicInputs[0].ExpressionInput, DynamicInputs[0].ConstValue);
	if (bAllowAddPin)
	{
		MIR::EOperator OpMIR = MaterialExpressionOperatorToMIR(Operator);

		// Apply operation to iteratively to all input values
		FValueRef Value = AValue;
		for (int32 i = 1; i < DynamicInputs.Num(); i++)
		{
			FValueRef CurValue = Em.InputDefaultFloat(&DynamicInputs[i].ExpressionInput, DynamicInputs[i].ConstValue);
			Value = Em.Operator(OpMIR, Value, CurValue);
		}

		Em.Output(0, Value);
	}
	else
	{
		MIR::EOperator OpMIR = MaterialExpressionOperatorToMIR(Operator);
		int32 OperatorArity = MIR::GetOperatorArity(OpMIR);

		FValueRef BValue = OperatorArity >= 2 ? Em.InputDefaultFloat(&DynamicInputs[1].ExpressionInput, DynamicInputs[1].ConstValue) : FValueRef{};
		FValueRef CValue = OperatorArity >= 3 ? Em.InputDefaultFloat(&DynamicInputs[2].ExpressionInput, DynamicInputs[2].ConstValue) : FValueRef{};

		Em.Output(0, Em.Operator(OpMIR, AValue, BValue, CValue));
	}
}

void UMaterialExpressionFloatToUInt::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.CastToIntKind(Em.Input(&Input)));
}

void UMaterialExpressionUIntToFloat::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.CastToFloatKind(Em.Input(&Input)));
}

void UMaterialExpressionTruncateLWC::Build(MIR::FEmitter& Em)
{
	FValueRef InputValue = Em.Input(&Input);
	int32 LWCTruncateMode = UE::MaterialTranslatorUtils::GetLWCTruncateMode();

	if (LWCTruncateMode == 1 || LWCTruncateMode == 2)
	{
		TOptional<MIR::FPrimitive> Primitive = InputValue->Type.AsPrimitive();
		if (Primitive && Primitive->ScalarKind == MIR::EScalarKind::Double)
		{
			Em.Output(0, Em.CastToFloatKind(Em.Input(&Input)));
			return;
		}
	}

	Em.Output(0, InputValue);
}

void UMaterialExpressionActorPositionWS::Build(MIR::FEmitter& Em)
{
	if (OriginType == EPositionOrigin::CameraRelative)
	{
		Em.Output(0, Em.ExternalInput(MIR::EExternalInput::ActorPosition_CameraRelative));
	}
	else if (!UE::MaterialTranslatorUtils::IsLWCEnabled())
	{
		// CastToNonLWCIfDisabled
		Em.Output(0, Em.CastToFloatKind(Em.ExternalInput(MIR::EExternalInput::ActorPosition_Absolute)));
	}
	else
	{
		Em.Output(0, Em.ExternalInput(MIR::EExternalInput::ActorPosition_Absolute));
	}
}

void UMaterialExpressionObjectPositionWS::Build(MIR::FEmitter& Em)
{
	if (OriginType == EPositionOrigin::CameraRelative)
	{
		Em.Output(0, Em.ExternalInput(MIR::EExternalInput::ObjectPosition_CameraRelative));
	}
	else if (!UE::MaterialTranslatorUtils::IsLWCEnabled())
	{
		// CastToNonLWCIfDisabled
		Em.Output(0, Em.CastToFloatKind(Em.ExternalInput(MIR::EExternalInput::ObjectPosition_Absolute)));
	}
	else
	{
		Em.Output(0, Em.ExternalInput(MIR::EExternalInput::ObjectPosition_Absolute));
	}
}

static FValueRef EmitWorldPosition(MIR::FEmitter& Em, EWorldPositionIncludedOffsets WorldPositionShaderOffset)
{
	// Make sure EWorldPositionIncludedOffsets and corresponding elements of EExternalInput stay in sync, so this enum addition is valid
	static_assert(WPT_MAX == 4 && WPT_Default == 0);
	static_assert((int32)MIR::EExternalInput::WorldPosition_AbsoluteNoOffsets - (int32)MIR::EExternalInput::WorldPosition_Absolute == WPT_ExcludeAllShaderOffsets);
	static_assert((int32)MIR::EExternalInput::WorldPosition_CameraRelative - (int32)MIR::EExternalInput::WorldPosition_Absolute == WPT_CameraRelative);
	static_assert((int32)MIR::EExternalInput::WorldPosition_CameraRelativeNoOffsets - (int32)MIR::EExternalInput::WorldPosition_Absolute == WPT_CameraRelativeNoOffsets);

	FValueRef WorldPosition = Em.ExternalInput((MIR::EExternalInput)((int32)MIR::EExternalInput::WorldPosition_Absolute + FMath::Clamp(WorldPositionShaderOffset, 0, WPT_MAX - 1)));

	// CastToNonLWCIfDisabled
	if (!UE::MaterialTranslatorUtils::IsLWCEnabled())
	{
		WorldPosition = Em.CastToFloatKind(WorldPosition);
	}

	return WorldPosition;
}

static FValueRef EmitLocalPosition(MIR::FEmitter& Em, ELocalPositionOrigin LocalOrigin, EPositionIncludedOffsets LocalShaderOffset)
{
	// Make sure ELocalPositionOrigin / EPositionIncludedOffsets and corresponding elements of EExternalInput stay in sync, so math using enum is valid.
	static_assert((int32)ELocalPositionOrigin::Instance == 0);
	static_assert((int32)ELocalPositionOrigin::Primitive == 2);
	static_assert((int32)EPositionIncludedOffsets::IncludeOffsets == 0);
	static_assert((int32)EPositionIncludedOffsets::ExcludeOffsets == 1);
	static_assert((int32)MIR::EExternalInput::LocalPosition_InstanceNoOffsets - (int32)MIR::EExternalInput::LocalPosition_Instance == (int32)EPositionIncludedOffsets::ExcludeOffsets);
	static_assert((int32)MIR::EExternalInput::LocalPosition_Primitive - (int32)MIR::EExternalInput::LocalPosition_Instance == (int32)ELocalPositionOrigin::Primitive);

	// ELocalPositionOrigin::InstancePreSkinning just uses an external code declaration, and doesn't have variations for offsets.
	if (LocalOrigin == ELocalPositionOrigin::InstancePreSkinning)
	{
		static FName NAME_PreSkinnedPosition("PreSkinnedPosition");
		return EmitInlineHLSL(Em, NAME_PreSkinnedPosition);
	}

	// Otherwise, there's a 2x2 configuration of Origin and Offset type.  Given that ELocalPositionOrigin::InstancePreSkinning is unused, valid origin values are 0 or 2,
	// so we can just add the values together to get a unique index from [0..3]
	int32 OriginIndex = FMath::Clamp((int32)LocalOrigin, 0, 2);
	int32 OffsetIndex = FMath::Clamp((int32)LocalShaderOffset, 0, 1);
	return Em.ExternalInput((MIR::EExternalInput)((int32)MIR::EExternalInput::LocalPosition_Instance + OriginIndex + OffsetIndex));
}

// Adapted from FHLSLMaterialTranslator::GetWorldPositionOrDefault.  WorldPosition input is optional, a default is provided if not set.
static FValueRef EmitWorldPositionOrDefault(MIR::FEmitter& Em, FValueRef WorldPosition, EPositionOrigin PositionOrigin)
{
	if (PositionOrigin != EPositionOrigin::Absolute && PositionOrigin != EPositionOrigin::CameraRelative)
	{
		Em.Error(TEXT("Invalid EPositionOrigin enum value."));
		return Em.Poison();
	}
	if (WorldPosition)
	{
		// Sanitize the explicitly provided input to the correct vector type if needed.
		return Em.Cast(WorldPosition, PositionOrigin == EPositionOrigin::Absolute ? MIR::FType::MakeDoubleVector(3) : MIR::FType::MakeFloatVector(3));
	}
	else
	{
		// Return default world position.
		return EmitWorldPosition(Em, PositionOrigin == EPositionOrigin::CameraRelative ? EWorldPositionIncludedOffsets::WPT_CameraRelative : EWorldPositionIncludedOffsets::WPT_Default);
	}
}

void UMaterialExpressionWorldPosition::Build(MIR::FEmitter& Em)
{
	FValueRef WorldPosition = EmitWorldPosition(Em, WorldPositionShaderOffset);

	Em.Output(0, WorldPosition);
	Em.Output(1, Em.Swizzle(WorldPosition, MIR::FSwizzleMask(MIR::EVectorComponent::X, MIR::EVectorComponent::Y)));
	Em.Output(2, Em.Subscript(WorldPosition, 2));
}

void UMaterialExpressionLocalPosition::Build(MIR::FEmitter& Em)
{
	Em.Output(0, EmitLocalPosition(Em, LocalOrigin, IncludedOffsets));
}

void UMaterialExpressionMakeMaterialAttributes::Build(MIR::FEmitter& Em)
{
	MIR::TTemporaryArray<MIR::FAttributeAssignment> Assignments{ MP_MAX };
	int32 NumAssignments = 0;

	auto PushAttributeAssignment = [&](EMaterialProperty Property, FExpressionInput* Input)
	{
		if (FValueRef Value = Em.TryInput(Input))
		{
			Assignments[NumAssignments++] = { *FMaterialAttributeDefinitionMap::GetAttributeName(Property), Value };
		}
	};

	PushAttributeAssignment(MP_BaseColor, &BaseColor);
	PushAttributeAssignment(MP_Metallic, &Metallic);
	PushAttributeAssignment(MP_Specular, &Specular);
	PushAttributeAssignment(MP_Roughness, &Roughness);
	PushAttributeAssignment(MP_Anisotropy, &Anisotropy);
	PushAttributeAssignment(MP_EmissiveColor, &EmissiveColor);
	PushAttributeAssignment(MP_Opacity, &Opacity);
	PushAttributeAssignment(MP_OpacityMask, &OpacityMask);
	PushAttributeAssignment(MP_Normal, &Normal);
	PushAttributeAssignment(MP_Tangent, &Tangent);
	PushAttributeAssignment(MP_WorldPositionOffset, &WorldPositionOffset);
	PushAttributeAssignment(MP_SubsurfaceColor, &SubsurfaceColor);
	PushAttributeAssignment(MP_CustomData0, &ClearCoat);
	PushAttributeAssignment(MP_CustomData1, &ClearCoatRoughness);
	PushAttributeAssignment(MP_AmbientOcclusion, &AmbientOcclusion);
	PushAttributeAssignment(MP_Refraction, &Refraction);
	PushAttributeAssignment(MP_PixelDepthOffset, &PixelDepthOffset);
	PushAttributeAssignment(MP_ShadingModel, &ShadingModel);
	PushAttributeAssignment(MP_Displacement, &Displacement);

	for (int32 i = 0; i < 8; ++i)
	{
		PushAttributeAssignment(EMaterialProperty(MP_CustomizedUVs0 + i), &CustomizedUVs[i]);
	}

	Em.Output(0, Em.Aggregate(UMaterialAggregate::GetMaterialAttributes(), {}, Assignments.Left(NumAssignments)));
}

void UMaterialExpressionBreakMaterialAttributes::Build(MIR::FEmitter& Em)
{
	FValueRef Prototype = Em.CheckIsAggregate(Em.Input(&MaterialAttributes), UMaterialAggregate::GetMaterialAttributes());
	UE_MIR_CHECKPOINT(Em);

	static const EMaterialProperty Properties[] = {
		MP_BaseColor,
		MP_Metallic,
		MP_Specular,
		MP_Roughness,
		MP_Anisotropy,
		MP_EmissiveColor,
		MP_Opacity,
		MP_OpacityMask,
		MP_Normal,
		MP_Tangent,
		MP_WorldPositionOffset,
		MP_SubsurfaceColor,
		MP_CustomData0, // ClearColor
		MP_CustomData1, // ClearColorRoughness
		MP_AmbientOcclusion,
		MP_Refraction,
		MP_PixelDepthOffset,
		MP_ShadingModel,
		MP_Displacement
	};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Properties); ++Index)
	{
		Em.Output(Index, Em.Subscript(Prototype, UMaterialAggregate::MaterialPropertyToAttributeIndex(Properties[Index])));
	}
}

// Verifies that the attribute ids in the MaterialAttributes expression are valid (e.g. no duplicates, proper mapping).
static void CheckMaterialAttributesExpression(MIR::FEmitter& Em, TConstArrayView<FGuid> AttributeIds)
{
	for (int32 i = 0; i < AttributeIds.Num(); ++i)
	{
		for (int j = i + 1; j < AttributeIds.Num(); ++j)
		{
			if (AttributeIds[i] == AttributeIds[j])
			{
				Em.Error(TEXT("Duplicate attribute types."));
				return;
			}
		}

		if (FMaterialAttributeDefinitionMap::GetProperty(AttributeIds[i]) == MP_MAX)
		{
			Em.Error(TEXT("Property type doesn't exist, needs re-mapping?"));
			return;
		}
	}
}

void UMaterialExpressionGetMaterialAttributes::Build(MIR::FEmitter& Em)
{
	CheckMaterialAttributesExpression(Em, AttributeGetTypes);

	FValueRef Prototype = Em.CheckIsAggregate(Em.TryInput(&MaterialAttributes), UMaterialAggregate::GetMaterialAttributes());
	
	UE_MIR_CHECKPOINT(Em);
	
	Em.Output(0, Prototype);

	const UMaterialAggregate* MaterialAttributesAggregate = UMaterialAggregate::GetMaterialAttributes();
	for (int32 i = 0; i < AttributeGetTypes.Num(); ++i)
	{
		EMaterialProperty Property = FMaterialAttributeDefinitionMap::GetProperty(AttributeGetTypes[i]);
		check(Property != MP_MAX);

		int32 AttributeIndex = MaterialAttributesAggregate->FindAttributeIndexByName(*FMaterialAttributeDefinitionMap::GetAttributeName(Property));

		Em.Output(i + 1, Em.Subscript(Prototype, AttributeIndex));
	}
}

void UMaterialExpressionSetMaterialAttributes::Build(MIR::FEmitter& Em)
{
	CheckMaterialAttributesExpression(Em, AttributeSetTypes);
	
	FValueRef Prototype = Em.CheckIsAggregate(Em.TryInput(&Inputs[0]), UMaterialAggregate::GetMaterialAttributes());
	
	UE_MIR_CHECKPOINT(Em);

	const UMaterialAggregate* MaterialAttributesAggregate = UMaterialAggregate::GetMaterialAttributes();
	MIR::TTemporaryArray<MIR::FAttributeAssignment> Assignments{ AttributeSetTypes.Num() };
	int32 NumAssignments = 0;

	for (int32 i = 0; i < AttributeSetTypes.Num(); ++i)
	{
		EMaterialProperty Property = FMaterialAttributeDefinitionMap::GetProperty(AttributeSetTypes[i]);
		check(Property != MP_MAX);

		if (FValueRef Value = Em.TryInput(&Inputs[i + 1]))
		{
			Assignments[NumAssignments++] = { *FMaterialAttributeDefinitionMap::GetAttributeName(Property), Value };
		}
	}

	Prototype = Em.Aggregate(UMaterialAggregate::GetMaterialAttributes(), Prototype, Assignments.Left(NumAssignments));

	Em.Output(0, Prototype);
}

// Utility to input a MaterialAttributes value, or return the default instance (with each attribute set to zero). 
static FValueRef InputDefaultMaterialAttributes(MIR::FEmitter& Em, FExpressionInput* Input)
{
	const UMaterialAggregate* MaterialAttributes = UMaterialAggregate::GetMaterialAttributes();
	FValueRef Value = Em.CheckIsAggregate(Em.TryInput(Input), MaterialAttributes);
	return Value ? Value : Em.Aggregate(MaterialAttributes);
}

// Converts old EMaterialAttributeBlend::Type to EMaterialExpressionBlendMode.
static EMaterialExpressionBlendMode ConvertMaterialAttributeBlend(EMaterialAttributeBlend::Type InBlend)
{
	switch (InBlend)
	{
		case EMaterialAttributeBlend::Blend: return EMaterialExpressionBlendMode::Blend;
		case EMaterialAttributeBlend::UseA: return EMaterialExpressionBlendMode::UseA;
		case EMaterialAttributeBlend::UseB: return EMaterialExpressionBlendMode::UseB;
		default: UE_MIR_UNREACHABLE();
	}
}

// Forward declaration.
static FValueRef Blend(MIR::FEmitter& Em, EMaterialExpressionBlendMode PixelAttributeBlendMode, EMaterialExpressionBlendMode VertexAttributeBlendMode, FValueRef A, FValueRef B, FValueRef Alpha);

// Blends two argument aggregate values based on [0-1] alpha value. See Blend() below for more info.
static FValueRef BlendAggregate(MIR::FEmitter& Em,
						   EMaterialExpressionBlendMode PixelAttributesBlendMode,
						   EMaterialExpressionBlendMode VertexAttributesBlendMode,
						   FValueRef A,
						   FValueRef B,
						   FValueRef Alpha)
{
	const UMaterialAggregate* MaterialAggregate = A->Type.AsAggregate();
	MIR::TTemporaryArray<MIR::FValueRef> AttributeValues{ MaterialAggregate->Attributes.Num() };

	for (int32 i = 0; i < MaterialAggregate->Attributes.Num(); ++i)
	{
		EMaterialExpressionBlendMode BlendMode = PixelAttributesBlendMode;
		if (MaterialAggregate == UMaterialAggregate::GetMaterialAttributes())
		{
			EMaterialProperty Property = UMaterialAggregate::AttributeIndexToMaterialProperty(i);
			BlendMode = (Property == MP_WorldPositionOffset) ? VertexAttributesBlendMode : PixelAttributesBlendMode;
		}

		if (BlendMode == EMaterialExpressionBlendMode::UseA)
		{
			AttributeValues[i] = Em.Subscript(A, i);
		}
		if (BlendMode == EMaterialExpressionBlendMode::UseB)
		{
			AttributeValues[i] = Em.Subscript(B, i);
		}
		else if (BlendMode == EMaterialExpressionBlendMode::Blend)
		{
			AttributeValues[i] = Blend(Em, PixelAttributesBlendMode, VertexAttributesBlendMode, Em.Subscript(A, i), Em.Subscript(B, i), Alpha);
		}
	}

	return Em.Aggregate(MaterialAggregate, {}, AttributeValues);
}

// Blends two argument values based on [0-1] alpha value. If argument values are or contain MaterialAttributes aggregates,
// PixelAttributeBlendMode and VertexAttributeBlendMode instruct on how to blend the attributes depending on whether
// they're evaluated in pixel or vertex shaders.
// Note: VertexAttributeBlendMode are only used when blending MaterialAttributes. Otherwise, PixelAttributeBlendMode is used.
static FValueRef Blend(MIR::FEmitter& Em, EMaterialExpressionBlendMode PixelAttributeBlendMode, EMaterialExpressionBlendMode VertexAttributeBlendMode, FValueRef A, FValueRef B, FValueRef Alpha)
{
	// Find the common type between arguments
	if (MIR::FType CommonType = Em.GetCommonType(A->Type, B->Type))
	{
		// And cast both arguments to the common type
		A = Em.Cast(A, CommonType);
		B = Em.Cast(B, CommonType);
	}
	else
	{
		return Em.Poison();
	}

	if (A->Type.IsAnyFloat())
	{
		// Blend floating point values using linear interpolation
		return Em.Lerp(A, B, Alpha);
	}
	else if (A->Type.IsInteger())
	{
		// "Blend" integer values by selecting A or B based on whether alpha is less than 0.5.
		return Em.Select(Em.LessThan(Alpha, Em.ConstantFloat(0.5f)), A, B);
	}
	else if (A->Type.AsAggregate())
	{
		// Arguments are aggregates, so recursively blend each attribute pair.
		return BlendAggregate(Em, PixelAttributeBlendMode, VertexAttributeBlendMode, A, B, Alpha);
	}
	else
	{
		Em.Errorf(TEXT("Cannot blend values of type '%s'."), *A->Type.GetSpelling());
		return Em.Poison();
	}
}

void UMaterialExpressionBlendMaterialAttributes::Build(MIR::FEmitter& Em)
{
	const UMaterialAggregate* MaterialAttributes = UMaterialAggregate::GetMaterialAttributes();
	
	FValueRef AValue = InputDefaultMaterialAttributes(Em, &A);
	FValueRef BValue = InputDefaultMaterialAttributes(Em, &B);
	FValueRef AlphaValue = Em.CastToFloat(Em.InputDefaultFloat(&Alpha, 0.0f), 1);

	UE_MIR_CHECKPOINT(Em);

	FValueRef Result = BlendAggregate(
		Em, 
		ConvertMaterialAttributeBlend(PixelAttributeBlendType),
		ConvertMaterialAttributeBlend(VertexAttributeBlendType),
		AValue,
		BValue,
		AlphaValue);

	Em.Output(0, Result);
}

void UMaterialExpressionAggregate::Build(MIR::FEmitter& Em)
{
	// Get the material aggregate definition.
	const UMaterialAggregate* Aggregate = GetAggregate();
	if (!Aggregate)
	{
		Em.Error(TEXT("Unspecified material aggregate."));
		return;
	}

	// Read the aggregate prototype value, if present, and make sure it is of the right type.
	FValueRef Prototype = Em.CheckIsAggregate(Em.TryInput(&PrototypeInput), Aggregate);
	UE_MIR_CHECKPOINT(Em);

	// Collect the attribute assignments from the input pins.
	MIR::TTemporaryArray<MIR::FAttributeAssignment> Assignments{ Entries.Num() };
	int32 NumAssignments = 0;

	for (const FMaterialExpressionAggregateEntry& Entry : Entries)
	{
		// If value is present, push this attribute assignment.
		if (FValueRef AttributeValue = Em.TryInput(&Entry.Input))
		{
			Assignments[NumAssignments++] = { Aggregate->Attributes[Entry.AttributeIndex].Name, AttributeValue };
		}
	}
	
	// Make hte aggregate value using the optional prototype and assignments.
	Prototype = Em.Aggregate(Aggregate, Prototype, Assignments.Left(NumAssignments));

	// Output the aggregate value
	Em.Output(0, Prototype);

	// And output each individual aggregate attribute through the invidual output pins
	for (int i = 0; i < Entries.Num(); ++i)
	{
		Em.Output(i + 1, Em.Subscript(Prototype, Entries[i].AttributeIndex));
	}
}

void UMaterialExpressionBlend::Build(MIR::FEmitter& Em)
{
	// Try reading the input values (could be null).
	FValueRef AValue = Em.TryInput(&A);
	FValueRef BValue = Em.TryInput(&B);

	if (!AValue && !BValue)
	{
		Em.Error(TEXT("No input value provided."));
		return;
	}

	// Create a default value from the other input's type if any input is missing.
	if (!AValue)
	{
		AValue = Em.ConstantDefault(BValue->Type);
	}
	else if (!BValue)
	{
		BValue = Em.ConstantDefault(AValue->Type);
	}

	// Read the alpha value (defaulting it to 0.0f)
	FValueRef AlphaValue = Em.CastToFloat(Em.InputDefaultFloat(&Alpha, 0.0f), 1);

	// Make sure all previous operations went well.
	UE_MIR_CHECKPOINT(Em);

	// Blend the input values.
	FValueRef Result = Blend(Em, PixelAttributesBlendMode, VertexAttributesBlendMode, AValue, BValue, AlphaValue);
	
	Em.Output(0, Result);
}

static FValueRef EmitParameterCollectionVectorInlineHLSL(MIR::FEmitter& Em, MIR::FValueRef CollectionValue, int32 ParameterIndex)
{
	return Em.InlineHLSL(MIR::FType::MakeFloatVector(4), TEXTVIEW("MaterialCollection$0.Vectors[$1]"), { CollectionValue, Em.ConstantInt(ParameterIndex) });
}

static bool GetExpressionCollectionParameter(MIR::FEmitter& Em, UMaterialParameterCollection* Collection, FName ParameterName, const FGuid& ParameterId, int32& OutParamIndex, int32& OutComponentIndex)
{
	if (!Collection)
	{
		Em.Errorf(TEXT("CollectionParameter has invalid Collection!"));
		return false;
	}

	Collection->GetParameterIndex(ParameterId, OutParamIndex, OutComponentIndex);
	if (OutParamIndex == INDEX_NONE)
	{
		Em.Errorf(TEXT("CollectionParameter has invalid parameter %s"), *ParameterName.ToString());
		return false;
	}

	return true;
}

void UMaterialExpressionCollectionParameter::Build(MIR::FEmitter& Em)
{
	int32 ParameterIndex = INDEX_NONE;
	int32 ComponentIndex = INDEX_NONE;
	if (!GetExpressionCollectionParameter(Em, Collection, ParameterName, ParameterId, ParameterIndex, ComponentIndex))
	{
		return;
	}

	FValueRef Result = EmitParameterCollectionVectorInlineHLSL(Em, Em.MaterialParameterCollection(Collection), ParameterIndex);
	if (ComponentIndex != INDEX_NONE)
	{
		Result = Em.Subscript(Result, ComponentIndex % 4);
	}
	Em.Output(0, Result);
}

void UMaterialExpressionCollectionTransform::Build(MIR::FEmitter& Em)
{
	int32 ParameterIndex = INDEX_NONE;
	int32 ComponentIndex = INDEX_NONE;
	if (!GetExpressionCollectionParameter(Em, Collection, ParameterName, ParameterId, ParameterIndex, ComponentIndex))
	{
		return;
	}

	if (ComponentIndex != INDEX_NONE)
	{
		Em.Errorf(TEXT("CollectionTransform parameter %s is scalar, vectors are required"), *ParameterName.ToString());
		return;
	}

	FValueRef Value = Em.CheckIsPrimitive(Em.Input(&Input));
	
	UE_MIR_CHECKPOINT(Em);

	if (!Value->Type.IsAnyFloat() || Value->Type.GetPrimitive().NumRows != 1 || Value->Type.GetPrimitive().NumColumns < 3)
	{
		Em.Error(TEXT("CollectionTransform requires float3 vector input"));
		return;
	}

	int32 NumVectors = 0;
	if (TransformType == EParameterCollectionTransformType::Position || TransformType == EParameterCollectionTransformType::Projection)
	{
		if (ParameterIndex + 4 > Collection->GetTotalVectorStorage())
		{
			Em.Errorf(TEXT("CollectionTransform parameter %s requires 4 vectors for Position or Projection matrix"), *ParameterName.ToString());
			return;
		}
		NumVectors = 4;
	}
	else if (TransformType == EParameterCollectionTransformType::Vector)
	{
		if (ParameterIndex + 3 > Collection->GetTotalVectorStorage())
		{
			Em.Errorf(TEXT("CollectionTransform parameter %s requires 3 vectors for Vector matrix"), *ParameterName.ToString());
			return;
		}
		NumVectors = 3;
	}
	else
	{
		check(TransformType == EParameterCollectionTransformType::LocalToWorld || TransformType == EParameterCollectionTransformType::WorldToLocal);
		if (ParameterIndex + 5 > Collection->GetTotalVectorStorage())
		{
			Em.Errorf(TEXT("CollectionTransform parameter %s requires 5 vectors for LWC Matrix"), *ParameterName.ToString());
			return;
		}
		NumVectors = 5;
	}

	FValueRef CollectionValue = Em.MaterialParameterCollection(Collection);
	TArray<FValueRef, TFixedAllocator<5>> CollectionParameters;

	for (int32 i = 0; i < NumVectors; i++)
	{
		CollectionParameters.Add(EmitParameterCollectionVectorInlineHLSL(Em, CollectionValue, ParameterIndex + i));
	}

	MIR::FValueRef Result;

	// Matrix transforms cobbled together from primitive ops (rather than using mul or LWCMultiply), so analytic derivatives are supported for free
	if (TransformType == EParameterCollectionTransformType::Vector)
	{
		// Treat input as a direction vector (w = 0)
		Value = Em.Cast(Value, MIR::FType::MakeFloatVector(3));

		Result =        Em.Multiply(Em.Subscript(Value, 0), Em.Swizzle(CollectionParameters[0], MIR::FSwizzleMask::XYZ()));
		Result = Em.Add(Em.Multiply(Em.Subscript(Value, 1), Em.Swizzle(CollectionParameters[1], MIR::FSwizzleMask::XYZ())), Result);
		Result = Em.Add(Em.Multiply(Em.Subscript(Value, 2), Em.Swizzle(CollectionParameters[2], MIR::FSwizzleMask::XYZ())), Result);
	}
	else if (TransformType == EParameterCollectionTransformType::Projection)
	{
		// Optimized to save many ALU for a standard perspective or orthographic projection matrix, where most of the elements of the matrix are zero.
		Result = Em.Vector4(
			Em.Multiply(Em.Subscript(Value, 0), Em.Subscript(CollectionParameters[0], 0)),														// Value.x * Matrix._00
			Em.Multiply(Em.Subscript(Value, 1), Em.Subscript(CollectionParameters[1], 1)),														// Value.y * Matrix._11
			Em.Add(Em.Multiply(Em.Subscript(Value, 2), Em.Subscript(CollectionParameters[2], 2)), Em.Subscript(CollectionParameters[3], 2)),	// Value.z * Matrix._22 + Matrix._32
			Em.Add(Em.Multiply(Em.Subscript(Value, 2), Em.Subscript(CollectionParameters[2], 3)), Em.Subscript(CollectionParameters[3], 3)));	// Value.z * Matrix._23 + Matrix._33
	}
	else
	{
		// Position, LocalToWorld, WorldToLocal
		if (TransformType == EParameterCollectionTransformType::WorldToLocal)
		{
			// Pre subtract tile value, to convert this to float (LWC inverse matrices have their tile negated, so adding means we are subtracting the tile value).
			// The tile value only applies to XYZ -- if the input Value has a fourth component, the Add operation will pad the tile argument with zero if needed.
			Value = Em.Add(Em.CastToScalarKind(Value, MIR::EScalarKind::Double), Em.LWCTile(Em.Swizzle(CollectionParameters[4], MIR::FSwizzleMask::XYZ())));
		}

		Value = Em.CastToFloatKind(Value);

		// If a 3-element vector is provided as input, we want to generate a 3-element vector as output.  Swizzle the collection parameters to achieve this.
		if (Value->Type.GetPrimitive().NumColumns == 3)
		{
			for (int32 i = 0; i < 4; i++)
			{
				CollectionParameters[i] = Em.Swizzle(CollectionParameters[i], MIR::FSwizzleMask::XYZ());
			}
		}

		Result =        Em.Multiply(Em.Subscript(Value, 0), CollectionParameters[0]);
		Result = Em.Add(Em.Multiply(Em.Subscript(Value, 1), CollectionParameters[1]), Result);
		Result = Em.Add(Em.Multiply(Em.Subscript(Value, 2), CollectionParameters[2]), Result);

		if (Value->Type.GetPrimitive().NumColumns == 3)
		{
			// Treat input as a translation vector (w = 1)
			Result = Em.Add(CollectionParameters[3], Result);
		}
		else
		{
			// Treat input as a homogenous vector (w = user specified)
			Result = Em.Add(Em.Multiply(Em.Subscript(Value, 3), CollectionParameters[3]), Result);
		}

		if (TransformType == EParameterCollectionTransformType::LocalToWorld)
		{
			// Post add tile value, to convert this to LWC
			Result = Em.Add(Em.CastToScalarKind(Result, MIR::EScalarKind::Double), Em.LWCTile(Em.Swizzle(CollectionParameters[4], MIR::FSwizzleMask::XYZ())));
		}
	}

	Em.Output(0, Result);
}

void UMaterialExpressionAtmosphericFogColor::Build(MIR::FEmitter& Em)
{
	// This node is deprecated in favor of UMaterialExpressionSkyAtmosphereAerialPerspective, and falls through to the newer expression
	FValueRef PositionValue = EmitWorldPositionOrDefault(Em, Em.TryInput(&WorldPosition), WorldPositionOriginType);
	Em.Output(0, EmitInlineHLSL(Em, *this, 0, { PositionValue }));
}

void UMaterialExpressionBlackBody::Build(MIR::FEmitter& Em)
{
	Em.Output(0, Em.InlineHLSL(MIR::FType::MakeFloatVector(3), TEXTVIEW("MaterialExpressionBlackBody($0)"), { Em.Input(&Temp) }));
}

void UMaterialExpressionDepthFade::Build(MIR::FEmitter& Em)
{
	// Scales Opacity by a Linear fade based on SceneDepth, from 0 at PixelDepth to 1 at FadeDistance
	// Result = Opacity * saturate((SceneDepth - PixelDepth) / max(FadeDistance, DELTA))
	FValueRef OpacityValue = Em.InputDefaultFloat(&InOpacity, OpacityDefault);
	FValueRef FadeDistanceValue = Em.Max(Em.InputDefaultFloat(&FadeDistance, FadeDistanceDefault), Em.ConstantFloat(UE_DELTA));

	static FName NAME_PixelDepth("PixelDepth");
	FValueRef PixelDepth = EmitInlineHLSL(Em, NAME_PixelDepth);
	// On mobile scene depth is limited to 65500 
	// to avoid false fading on objects that are close or exceed this limit we clamp pixel depth to (65500 - FadeDistance)
	if (Em.GetFeatureLevel() <= ERHIFeatureLevel::ES3_1)
	{
		PixelDepth = Em.Min(PixelDepth, Em.Subtract(Em.ConstantFloat(65500.f), FadeDistanceValue));
	}

	// We need a dependency on EScreenTexture::SceneDepth, so the value analyzer can see it, even though it's technically not used in the code.
	FValueRef SceneDepth = Em.InlineHLSL(MIR::FType::MakeFloatScalar(), TEXTVIEW("CalcSceneDepth(ScreenAlignedPosition(GetScreenPosition(Parameters)))"), { Em.ScreenTexture(MIR::EScreenTexture::SceneDepth) });

	Em.Output(0, Em.Multiply(OpacityValue, Em.Saturate(Em.Divide(Em.Subtract(SceneDepth, PixelDepth), FadeDistanceValue))));
}

void UMaterialExpressionDeriveNormalZ::Build(MIR::FEmitter& Em)
{
	// z = sqrt(saturate(1 - ( x * x + y * y)));
	FValueRef InputVector = Em.Cast(Em.Input(&InXY), MIR::FType::MakeFloatVector(2));
	FValueRef DotResult = Em.Dot(InputVector, InputVector);
	FValueRef InnerResult = Em.Subtract(Em.ConstantFloat(1.0f), DotResult);
	FValueRef SaturatedInnerResult = Em.Saturate(InnerResult);
	FValueRef DerivedZ = Em.Sqrt(SaturatedInnerResult);
	
	Em.Output(0, Em.Vector3(Em.Subscript(InputVector, 0), Em.Subscript(InputVector, 1), DerivedZ));
}

void UMaterialExpressionDistanceFieldApproxAO::Build(MIR::FEmitter& Em)
{
	FValueRef PositionValue = EmitWorldPositionOrDefault(Em, Em.TryInput(&Position), WorldPositionOriginType);

	FValueRef NormalValue = Em.TryInput(&Normal);
	if (!NormalValue)
	{
		static FName NAME_VertexNormal("VertexNormal");
		NormalValue = EmitInlineHLSL(Em, NAME_VertexNormal);
	}

	FValueRef BaseDistanceValue = Em.InputDefaultFloat(&BaseDistance, BaseDistanceDefault);

	int32 NumStepsClamped = FMath::Clamp(NumSteps, 1, 4);
	float StepScaleClamped = FMath::Max(StepScaleDefault, 1.0f);

	FValueRef NumStepsConst = Em.ConstantInt(NumStepsClamped);
	FValueRef NumStepsMinus1Const = Em.ConstantInt(NumStepsClamped - 1);
	FValueRef StepScaleConst = Em.ConstantFloat(StepScaleClamped);

	FValueRef StepDistance;
	FValueRef DistanceBias;
	FValueRef MaxDistance;

	if (NumSteps == 1)
	{
		StepDistance = Em.ConstantFloat(0);
		DistanceBias = BaseDistanceValue;
		MaxDistance = BaseDistanceValue;
	}
	else
	{
		FValueRef RadiusValue = Em.InputDefaultFloat(&Radius, RadiusDefault);

		StepDistance = Em.Divide(Em.Subtract(RadiusValue, BaseDistanceValue), Em.Subtract(Em.Pow(StepScaleConst, NumStepsMinus1Const), Em.ConstantFloat(1.0f)));
		DistanceBias = Em.Subtract(BaseDistanceValue, StepDistance);
		MaxDistance = RadiusValue;
	}

	// Last input tells value analyzer that this expression uses the global distance field
	Em.Output(0, Em.InlineHLSL(MIR::FType::MakeFloatScalar(), TEXTVIEW("CalculateDistanceFieldApproxAO($0, $1, $2, $3, $4, $5, $6)"),
		{
			PositionValue,
			Em.Cast(NormalValue, MIR::FType::MakeFloatVector(3)),
			NumStepsConst,
			Em.Cast(StepDistance, MIR::FType::MakeFloatScalar()),
			StepScaleConst,
			Em.Cast(DistanceBias, MIR::FType::MakeFloatScalar()),
			Em.Cast(MaxDistance, MIR::FType::MakeFloatScalar()),
			Em.ExternalInput(MIR::EExternalInput::GlobalDistanceField)
		}));
}

void UMaterialExpressionDistanceFieldGradient::Build(MIR::FEmitter& Em)
{
	// Last input tells value analyzer that this expression uses the global distance field
	FValueRef PositionValue = EmitWorldPositionOrDefault(Em, Em.TryInput(&Position), WorldPositionOriginType);
	Em.Output(0, Em.InlineHLSL(MIR::FType::MakeFloatVector(3), TEXTVIEW("GetDistanceFieldGradientGlobal($0)"), { PositionValue, Em.ExternalInput(MIR::EExternalInput::GlobalDistanceField) }));
}

void UMaterialExpressionDistanceToNearestSurface::Build(MIR::FEmitter& Em)
{
	// Last input tells value analyzer that this expression uses the global distance field
	FValueRef PositionValue = EmitWorldPositionOrDefault(Em, Em.TryInput(&Position), WorldPositionOriginType);
	Em.Output(0, Em.InlineHLSL(MIR::FType::MakeFloatScalar(), TEXTVIEW("GetDistanceToNearestSurfaceGlobal($0)"), { PositionValue, Em.ExternalInput(MIR::EExternalInput::GlobalDistanceField) }));
}

void UMaterialExpressionFresnel::Build(MIR::FEmitter& Em)
{
	// pow(1 - max(0,Normal dot Camera),Exponent) * (1 - BaseReflectFraction) + BaseReflectFraction
	//
	FValueRef NormalArg = Em.TryInput(&Normal);
	if (NormalArg)
	{
		NormalArg = Em.Cast(NormalArg, MIR::FType::MakeFloatVector(3));
	}
	else
	{
		NormalArg = Em.InlineHLSL(MIR::FType::MakeFloatVector(3), TEXTVIEW("Parameters.WorldNormal"), {}, MIR::EValueFlags::None, MIR::EGraphProperties::ReadsPixelNormal);
	}

	FValueRef DotArg = Em.Dot(NormalArg, EmitInlineHLSL(Em, NAME_CameraVector));
	FValueRef MaxArg = Em.Max(Em.ConstantFloat(0.f), DotArg);
	FValueRef MinusArg = Em.Subtract(Em.ConstantFloat(1.f), MaxArg);
	FValueRef ExponentArg = Em.InputDefaultFloat(&ExponentIn, Exponent);
	// Compiler->Power got changed to call PositiveClampedPow instead of ClampedPow
	// Manually implement ClampedPow to maintain backwards compatibility in the case where the input normal is not normalized (length > 1)
	FValueRef AbsBaseArg = Em.Max(Em.Abs(MinusArg), Em.ConstantFloat(UE_KINDA_SMALL_NUMBER));
	FValueRef PowArg = Em.Pow(AbsBaseArg, ExponentArg);
	FValueRef BaseReflectFractionArg = Em.InputDefaultFloat(&BaseReflectFractionIn, BaseReflectFraction);
	FValueRef ScaleArg = Em.Multiply(PowArg, Em.Subtract(Em.ConstantFloat(1.f), BaseReflectFractionArg));
	
	Em.Output(0, Em.Add(ScaleArg, BaseReflectFractionArg));
}

void UMaterialExpressionReflectionVectorWS::Build(MIR::FEmitter& Em)
{
	FValueRef NormalValue = Em.TryInput(&CustomWorldNormal);

	if (NormalValue)
	{
		NormalValue = Em.Cast(NormalValue, MIR::FType::MakeFloatVector(3));

		// Ported from HLSL utility function ReflectionAboutCustomWorldNormal
		if (bNormalizeCustomWorldNormal)
		{
			NormalValue = Em.Multiply(NormalValue, Em.Rsqrt(Em.Dot(NormalValue, NormalValue)));
		}

		// Normal * dot(Normal, CameraVector) * 2.0 - CameraVector;
		FValueRef CameraVector = EmitInlineHLSL(Em, NAME_CameraVector);
		Em.Output(0, Em.Subtract(Em.Multiply(NormalValue, Em.Multiply(Em.Dot(NormalValue, CameraVector), Em.ConstantFloat(2.0f))), CameraVector));
	}
	else
	{
		static FName NAME_ReflectionVector("ReflectionVector");
		Em.Output(0, EmitInlineHLSL(Em, NAME_ReflectionVector));
	}
}

void UMaterialExpressionRotateAboutAxis::Build(MIR::FEmitter& Em)
{
	FValueRef Angle = Em.Multiply(Em.Subscript(Em.Input(&RotationAngle), 0), Em.ConstantFloat(2.0f * (float)UE_PI / Period));
	FValueRef Axis = Em.Cast(Em.Input(&NormalizedRotationAxis), MIR::FType::MakeFloatVector(3));
	FValueRef PosOnAxis = Em.Input(&PivotPoint);
	FValueRef Pos = Em.Input(&Position);

	// Math adapted from RotateAboutAxis, but simplified and optimized slightly.  Note that the function returns an offset to
	// the rotated position, not an absolute position, and so the offset will be non-LWC.  This initial subtraction is LWC aware,
	// but we can then use float operations for the remainder (the LWC RotateAboutAxis HLSL function does the same).
	FValueRef PosOffset = Em.Cast(Em.Subtract(Pos, PosOnAxis), MIR::FType::MakeFloatVector(3));

	// Construct orthogonal axes in the plane of rotation.  The UAxis is computed by subtracting the projection of
	// PosOffset along the Axis vector.
	FValueRef UAxis = Em.Subtract(PosOffset, Em.Multiply(Axis, Em.Dot(Axis, PosOffset)));
	FValueRef VAxis = Em.Cross(Axis, UAxis);

	// Rotate the orthogonal axes
	FValueRef CosAngle = Em.Cos(Angle);
	FValueRef SinAngle = Em.Sin(Angle);
	FValueRef R = Em.Add(Em.Multiply(UAxis, CosAngle), Em.Multiply(VAxis, SinAngle));

	// Return the offset from the original position to the rotated position.  The original position in this context
	// is the pre-rotation axis vector.
	Em.Output(0, Em.Subtract(R, UAxis));
}

void UMaterialExpressionRotator::Build(MIR::FEmitter& Em)
{
	FValueRef TimeValue = Em.TryInput(&Time);
	if (!TimeValue)
	{
		TimeValue = Em.InlineHLSL(MIR::FType::MakeFloatScalar(), TEXTVIEW("View.<PREVFRAME>GameTime"), {}, MIR::EValueFlags::SubstituteTags);
	}
	TimeValue = Em.Multiply(TimeValue, Em.ConstantFloat(Speed));

	FValueRef BaseCoordinate = Em.TryInput(&Coordinate);
	if (!BaseCoordinate)
	{
		BaseCoordinate = Em.ExternalInput(MIR::TexCoordIndexToExternalInput(ConstCoordinate));
	}
	BaseCoordinate = Em.Subtract(BaseCoordinate, Em.ConstantFloat2({ CenterX, CenterY }));

	FValueRef CosValue = Em.Cos(TimeValue);
	FValueRef SinValue = Em.Sin(TimeValue);

	FValueRef Arg1 = Em.Add(Em.Subtract(Em.Multiply(CosValue, Em.Subscript(BaseCoordinate, 0)), Em.Multiply(SinValue, Em.Subscript(BaseCoordinate, 1))), Em.ConstantFloat(CenterX));		// cos*U - sin*V + CenterX
	FValueRef Arg2 = Em.Add(Em.Add     (Em.Multiply(SinValue, Em.Subscript(BaseCoordinate, 0)), Em.Multiply(CosValue, Em.Subscript(BaseCoordinate, 1))), Em.ConstantFloat(CenterY));		// sin*U + cos*V + CenterY

	TOptional<MIR::FPrimitive> BaseType = BaseCoordinate->Type.AsPrimitive();
	if (BaseType && BaseType->NumColumns >= 3)
	{
		Em.Output(0, Em.Vector3(Arg1, Arg2, Em.Subscript(BaseCoordinate, 2)));
	}
	else
	{
		Em.Output(0, Em.Vector2(Arg1, Arg2));
	}
}

void UMaterialExpressionSkyAtmosphereAerialPerspective::Build(MIR::FEmitter& Em)
{
	FValueRef PositionValue = EmitWorldPositionOrDefault(Em, Em.TryInput(&WorldPosition), WorldPositionOriginType);
	Em.Output(0, EmitInlineHLSL(Em, *this, 0, { PositionValue }));
}

void UMaterialExpressionSkyAtmosphereLightDirection::Build(MIR::FEmitter& Em)
{
	Em.Output(0, EmitInlineHLSL(Em, *this, 0, { Em.ConstantInt(LightIndex) }));
}

void UMaterialExpressionSkyAtmosphereLightDiskLuminance::Build(MIR::FEmitter& Em)
{
	FValueRef CosHalfDiskRadius = Em.TryInput(&DiskAngularDiameterOverride);
	if (CosHalfDiskRadius)
	{
		// Convert from apex angle (angular diameter) to cosine of the disk radius.
		CosHalfDiskRadius = Em.Cos(Em.Multiply(Em.ConstantFloat(0.5f * float(UE_PI) / 180.0f), CosHalfDiskRadius));
	}
	else
	{
		CosHalfDiskRadius = Em.ConstantFloat(-1.0f);
	}
	Em.Output(0, EmitInlineHLSL(Em, *this, 0, { Em.ConstantInt(LightIndex), CosHalfDiskRadius }));
}

void UMaterialExpressionSkyAtmosphereLightIlluminance::Build(MIR::FEmitter& Em)
{
	FValueRef PositionValue = EmitWorldPositionOrDefault(Em, Em.TryInput(&WorldPosition), WorldPositionOriginType);
	Em.Output(0, EmitInlineHLSL(Em, *this, 0, { PositionValue, Em.ConstantInt(LightIndex) }));
}

void UMaterialExpressionSkyAtmosphereLightIlluminanceOnGround::Build(MIR::FEmitter& Em)
{
	Em.Output(0, EmitInlineHLSL(Em, *this, 0, { Em.ConstantInt(LightIndex) }));
}

void UMaterialExpressionSkyAtmosphereViewLuminance::Build(MIR::FEmitter& Em)
{
	FValueRef WorldDirectionValue = Em.TryInput(&WorldDirection);
	if (!WorldDirectionValue)
	{
		WorldDirectionValue = Em.Multiply(Em.ConstantFloat(-1.0f), EmitInlineHLSL(Em, NAME_CameraVector));
	}
	Em.Output(0, EmitInlineHLSL(Em, *this, 0, { WorldDirectionValue }));
}

void UMaterialExpressionSkyLightEnvMapSample::Build(MIR::FEmitter& Em)
{
	if (Material->bIsSky)
	{
		UE_LOG(LogMaterial, Warning, TEXT("Using SkyLightEnvMapSample from a IsSky material can result in visual artifact. For instance, if the previous frame capture was super bright, it might leak onto a new frame, e.g. transtion from menu to game."));
	}

	FValueRef DirectionValue = Em.InputDefaultFloat3(&Direction, FVector3f(0.0f, 0.0f, 1.0f));
	FValueRef RoughnessValue = Em.InputDefaultFloat(&Roughness, 0.0f);

	Em.Output(0, EmitInlineHLSL(Em, *this, 0, { DirectionValue, RoughnessValue }));
}

void UMaterialExpressionSphereMask::Build(MIR::FEmitter& Em)
{
	FValueRef Arg1 = Em.Input(&A);
	FValueRef Arg2 = Em.Input(&B);
	UE_MIR_CHECKPOINT(Em);

	// 1.0f / max(0.00001f, Radius)
	FValueRef ArgInvRadius = Em.Divide(Em.ConstantFloat(1.0f), Em.Max(Em.ConstantFloat(0.00001f), Em.InputDefaultFloat(&Radius, AttenuationRadius)));

	// 1.0f / max(0.00001fp4, 1.0f - Hardness)
	FValueRef ArgInvHardness = Em.Divide(Em.ConstantFloat(1.0f), Em.Max(Em.ConstantFloat(0.00001f), Em.Subtract(Em.ConstantFloat(1.0f), Em.InputDefaultFloat(&Hardness, HardnessPercent * 0.01f))));

	FValueRef Distance = Em.Length(Em.Subtract(Arg1, Arg2));
	FValueRef NormalizeDistance = Em.Multiply(Distance, ArgInvRadius);
	FValueRef NegNormalizedDistance = Em.Subtract(Em.ConstantFloat(1.0f), NormalizeDistance);
	FValueRef MaskUnclamped = Em.Multiply(NegNormalizedDistance, ArgInvHardness);
	Em.Output(0, Em.Saturate(MaskUnclamped));
}

// Takes a description user string and turns it into a valid C/HLSL identifier.
static FString DescriptionToIdentifier(FStringView Source)
{
	FString Out;
	Out.Reserve(Source.Len());
	// Append an underscore if the source starts by a digit
	if (!Source.IsEmpty() && FChar::IsDigit(Source[0]))
	{
		Out.AppendChar('_');
	}
	for (TCHAR Ch : Source)
	{
		Out.AppendChar(FChar::IsAlnum(Ch) ? Ch : '_');
	}
	return Out;
}

// Custom material output to MIR type conversion.
static MIR::FType CustomMaterialOutputTypeToMIR(ECustomMaterialOutputType Type)
{
	switch (Type) 
	{
		case CMOT_Float1: return MIR::FType::MakeFloatScalar();
		case CMOT_Float2: return MIR::FType::MakeFloatVector(2);
		case CMOT_Float3: return MIR::FType::MakeFloatVector(3);
		case CMOT_Float4: return MIR::FType::MakeFloatVector(4);
		case CMOT_MaterialAttributes: return MIR::FType::MakeAggregate(UMaterialAggregate::GetMaterialAttributes());
		default: UE_MIR_UNREACHABLE();
	}
}

void UMaterialExpressionCustom::Build(MIR::FEmitter& Em)
{
	// Convert the description to a valid HLSL identifier
	FString Name = DescriptionToIdentifier(Description);

	MIR::TTemporaryArray<MIR::FValueRef> InputArgs { Inputs.Num() };

	// Prepare a description of the user-defined HLSL function for the emitter.
	MIR::FFunctionHLSLDesc FuncDesc;
	FuncDesc.Name = Name;
	FuncDesc.ReturnType = CustomMaterialOutputTypeToMIR(OutputType);
	
	// Fixup the scene texture identifiers in the source string
	TArray<int8> SceneTextureInfo;
	FString FixedCode = UE::MaterialTranslatorUtils::CustomExpressionSceneTextureInputFixup(this, *Code, SceneTextureInfo);
	FuncDesc.Code = FixedCode;
	if (FuncDesc.Code.IsEmpty())
	{
		FuncDesc.Code = Code;
	}

	// Turn each expression input into an input-only parameter.
	for (int32 i = 0; i < Inputs.Num(); ++i)
	{
		FCustomInput const& Input = Inputs[i];

		if (Input.InputName.IsNone())
		{
			// Ignore this input parameters with "None" name.
			continue;
		}
		
		// Read the input argument
		InputArgs[FuncDesc.NumInputOnlyParams] = Em.Input(&Input.Input);

		// Is this argument an unused scene texture sample?
		if (SceneTextureInfo.IsValidIndex(i) && SceneTextureInfo[i] == -1)
		{
			// I this parameter samples an unused scene texture, skip the parameter, but still
			// make sure the scene-texture sample is analyzed.
			InputArgs[FuncDesc.NumInputOnlyParams] = Em.Nop(InputArgs[FuncDesc.NumInputOnlyParams]);
		}
		
		// Declare an input-only parameter
		if (!FuncDesc.PushInputOnlyParameter(Input.InputName, InputArgs[FuncDesc.NumInputOnlyParams]->Type))
		{
			Em.Errorf(TEXT("Too many inputs. Custom expressions can have at most %d input/output pins."), MIR::MaxNumFunctionParameters);
			return;
		}
	}

	// Some Input() call might have generated an error
	UE_MIR_CHECKPOINT(Em);

	// Turn each expression additional output into a output-only parameter.
	for (FCustomOutput const& AdditionalOutput : AdditionalOutputs)
	{
		// Ignore output parameters with "None" name.
		if (AdditionalOutput.OutputName.IsNone())
		{
			continue;
		}

		if (!FuncDesc.PushOutputOnlyParameter(AdditionalOutput.OutputName, CustomMaterialOutputTypeToMIR(AdditionalOutput.OutputType)))
		{
			Em.Errorf(TEXT("Too many input/outputs. Custom expressions can have at most %d input/output pins."), MIR::MaxNumFunctionParameters);
			return;
		}
	}

	// Generate the array of additional defines
	MIR::TTemporaryArray<MIR::FFunctionHLSLDefine> Defines{ AdditionalDefines.Num() };
	for (int32 i = 0; i < AdditionalDefines.Num(); ++i)
	{
		if (AdditionalDefines[i].DefineName.IsEmpty())
		{
			Em.Errorf(TEXT("Define with index '%d' has no valid name."), i);
		}

		if (AdditionalDefines[i].DefineValue.IsEmpty())
		{
			Em.Errorf(TEXT("Define with index '%d' has no valid value."), i);
		}

		Defines[i] = { AdditionalDefines[i].DefineName, AdditionalDefines[i].DefineValue };
	}
	FuncDesc.Defines = Defines;

	// Generate the array of additional includes
	MIR::TTemporaryArray<FStringView> Includes{ IncludeFilePaths.Num() };
	for (int32 i = 0; i < IncludeFilePaths.Num(); ++i)
	{
		if (IncludeFilePaths[i].IsEmpty())
		{
			Em.Errorf(TEXT("Include with index '%d' has no valid value."), i);
		}

		Includes[i] = IncludeFilePaths[i];
	}

	FuncDesc.Includes = Includes;

	UE_MIR_CHECKPOINT(Em); // Make sure checks above did not fail

	// Declare the HLSL function with the description we generated
	const MIR::FFunction* Func = Em.FunctionHLSL(FuncDesc);
	
	UE_MIR_CHECKPOINT(Em); // To guarantee a function was succesfully emitted.

	FValueRef Call = Em.Call(Func, { InputArgs.GetData(), (int32)FuncDesc.NumInputOnlyParams });

	// Output the call return value through the first output pin
	Em.Output(0, Call);

	// Output the additional outputs through subsequent output pins
	for (uint32 i = 0; i < Func->GetNumOutputParameters(); ++i)
	{
		Em.Output(i + 1, Em.CallParameterOutput(Call, i));
	}
}

void UMaterialExpressionBounds::Build(MIR::FEmitter& Em)
{
	// Select between 3 different sets of 4 outputs (half, full, min, max), depending on bounds type.  Check that enum matches order in BaseMaterialExpressions.ini.
	static_assert(MEILB_InstanceLocal == 0);
	static_assert(MEILB_ObjectLocal == 1);
	static_assert(MEILB_PreSkinnedLocal == 2);

	int32 OutputOffset = Type * 4;
	Em.Output(0, EmitInlineHLSL(Em, *this, OutputOffset + 0));
	Em.Output(1, EmitInlineHLSL(Em, *this, OutputOffset + 1));
	Em.Output(2, EmitInlineHLSL(Em, *this, OutputOffset + 2));
	Em.Output(3, EmitInlineHLSL(Em, *this, OutputOffset + 3));
}

void UMaterialExpressionBumpOffset::Build(MIR::FEmitter& Em)
{
	FValueRef HeightRatioArg = Em.Cast(Em.InputDefaultFloat(&HeightRatioInput, HeightRatio), MIR::FType::MakeFloatScalar());

	FValueRef TexCoordArg = Em.TryInput(&Coordinate);
	if (!TexCoordArg)
	{
		TexCoordArg = Em.ExternalInput(MIR::TexCoordIndexToExternalInput(ConstCoordinate));
	}

	Em.Output(0,
		Em.Add(
			Em.Multiply(
				Em.Swizzle(EmitTransformVectorBase(Em, EmitInlineHLSL(Em, NAME_CameraVector), MCB_World, MCB_Tangent, false, nullptr, nullptr), MIR::FSwizzleMask(MIR::EVectorComponent::X, MIR::EVectorComponent::Y)),
				Em.Add(
					Em.Multiply(
						HeightRatioArg,
						Em.Cast(Em.Input(&Height), MIR::FType::MakeFloatScalar())
					),
					Em.Multiply(Em.ConstantFloat(-ReferencePlane), HeightRatioArg)
				)
			),
			TexCoordArg
		)
	);
}

void UMaterialExpressionDynamicParameter::Build(MIR::FEmitter& Em)
{
	FValueRef Default = Em.ConstantFloat4(FVector4f(DefaultValue.R, DefaultValue.G, DefaultValue.B, DefaultValue.A));
	FValueRef DynamicParameterIndex = Em.ExternalInput(MIR::EExternalInput::DynamicParticleParameterIndex, ParameterIndex);
	FValueRef Result = Em.InlineHLSL(MIR::FType::MakeFloatVector(4), TEXTVIEW("GetDynamicParameter(Parameters.Particle, $0, $1)"), { Default, DynamicParameterIndex });

	Em.Output(0, Em.Subscript(Result, 0));
	Em.Output(1, Em.Subscript(Result, 1));
	Em.Output(2, Em.Subscript(Result, 2));
	Em.Output(3, Em.Subscript(Result, 3));
	Em.Output(4, Em.Swizzle(Result, MIR::FSwizzleMask::XYZ()));		// RGB
	Em.Output(5, Result);											// RGBA
}

void UMaterialExpressionNoise::Build(MIR::FEmitter& Em)
{
	FValueRef PositionInput = EmitWorldPositionOrDefault(Em, Em.TryInput(&Position), WorldPositionOriginType);

	if (WorldPositionOriginType == EPositionOrigin::CameraRelative)
	{
		// LWC_TODO: add support for translated world positions in the corresponding HLSL function
		PositionInput = EmitTransformVectorBase(Em, PositionInput, MCB_TranslatedWorld, MCB_World, true, nullptr, nullptr);
	}

	FValueRef FilterWidthInput = Em.InputDefaultFloat(&FilterWidth, 0.0f);
	FValueRef ScaleValue = Em.ConstantFloat(Scale);
	FValueRef QualityValue = Em.ConstantInt(Quality);
	FValueRef NoiseFunctionValue = Em.ConstantInt(NoiseFunction);
	FValueRef TurbulenceValue = Em.ConstantBool(bTurbulence);
	FValueRef LevelsValue = Em.ConstantInt(FMath::Clamp(Levels, 1, 10));		// to limit performance problems due to values outside reasonable range
	FValueRef OutputMinValue = Em.ConstantFloat(OutputMin);
	FValueRef OutputMaxValue = Em.ConstantFloat(OutputMax);
	FValueRef LevelScaleValue = Em.ConstantFloat(LevelScale);
	FValueRef TilingValue = Em.ConstantBool(bTiling);
	FValueRef RepeatSizeValue = Em.ConstantFloat(RepeatSize);

	Em.Output(0, Em.InlineHLSL(MIR::FType::MakeFloatScalar(),
		TEXTVIEW("MaterialExpressionNoise($0,$1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11)"), {
			PositionInput,
			ScaleValue,
			QualityValue,
			NoiseFunctionValue,
			TurbulenceValue,
			LevelsValue,
			OutputMinValue,
			OutputMaxValue,
			LevelScaleValue,
			FilterWidthInput,
			TilingValue,
			RepeatSizeValue
		}));
}

void UMaterialExpressionVectorNoise::Build(MIR::FEmitter& Em)
{
	FValueRef PositionInput = EmitWorldPositionOrDefault(Em, Em.TryInput(&Position), WorldPositionOriginType);

	if (WorldPositionOriginType == EPositionOrigin::CameraRelative)
	{
		// LWC_TODO: add support for translated world positions in the corresponding HLSL function
		PositionInput = EmitTransformVectorBase(Em, PositionInput, MCB_TranslatedWorld, MCB_World, true, nullptr, nullptr);
	}

	// LWC_TODO - maybe possible/useful to add LWC-aware noise functions
	PositionInput = Em.Cast(PositionInput, MIR::FType::MakeFloatVector(3));

	FValueRef QualityValue = Em.ConstantInt(Quality);
	FValueRef NoiseFunctionValue = Em.ConstantInt(NoiseFunction);
	FValueRef TilingValue = Em.ConstantBool(bTiling);
	FValueRef TileSizeValue = Em.ConstantFloat(TileSize);

	FValueRef NoiseResult = Em.InlineHLSL(MIR::FType::MakeFloatVector(4),
		TEXTVIEW("MaterialExpressionVectorNoise($0,$1,$2,$3,$4)"), {
			PositionInput,
			QualityValue,
			NoiseFunctionValue,
			TilingValue,
			TileSizeValue
		});

	// Function returns float4, but only certain noise functions fill in all four elements, so downcast to float3 if not those cases.
	if (NoiseFunction != VNF_GradientALU && NoiseFunction != VNF_VoronoiALU)
	{
		NoiseResult = Em.Cast(NoiseResult, MIR::FType::MakeFloatVector(3));
	}

	Em.Output(0, NoiseResult);
}

void UMaterialExpressionPanner::Build(MIR::FEmitter& Em)
{
	FValueRef TimeArg = Em.TryInput(&Time);
	if (TimeArg)
	{
		TimeArg = Em.Cast(TimeArg, MIR::FType::MakeFloatScalar());
	}
	else
	{
		TimeArg = Em.InlineHLSL(MIR::FType::MakeFloatScalar(), TEXTVIEW("View.<PREVFRAME>GameTime"), {}, MIR::EValueFlags::SubstituteTags);
	}
	
	FValueRef SpeedVectorArg = Em.InputDefaultFloat2(&Speed, FVector2f(SpeedX, SpeedY));

	// TODO:  When preshaders get implemented, the original translator generates a unique "PeriodicHint" preshader op for this expression,
	//        which attempts to do math at higher precision to avoid accuracy issues as GameTime increases.  We'll want to add that logic here,
	//        or consider making preshader math involving game time automatically run at high precision across the board (naturally solving
	//        precision issues even outside this specific expression).

	SpeedVectorArg = Em.Multiply(TimeArg, SpeedVectorArg);
	if (bFractionalPart)
	{
		SpeedVectorArg = Em.Frac(SpeedVectorArg);
	}

	FValueRef TexCoordArg = Em.TryInput(&Coordinate);
	if (!TexCoordArg)
	{
		TexCoordArg = Em.ExternalInput(MIR::TexCoordIndexToExternalInput(0));
	}

	Em.Output(0, Em.Add(SpeedVectorArg, TexCoordArg));
}

void UMaterialExpressionParticlePositionWS::Build(MIR::FEmitter& Em)
{
	int32 ExternalCodeIndex = OriginType == EPositionOrigin::Absolute ? 0 : 1;
	MIR::FType ResultType = OriginType == EPositionOrigin::Absolute ? MIR::FType::MakeDoubleVector(3) : MIR::FType::MakeFloatVector(3);

	Em.Output(0, EmitInlineHLSL(Em, *this, ExternalCodeIndex, {}, MIR::EValueFlags::SubstituteTags));
}

void UMaterialExpressionPerInstanceCustomData::Build(MIR::FEmitter& Em)
{
	FValueRef DataIndexArgument = Em.ConstantInt(DataIndex);
	FValueRef DefaultArgument = Em.InputDefaultFloat(&DefaultValue, ConstDefaultValue);

	Em.Output(0, EmitInlineHLSL(Em, *this, 0, { DataIndexArgument, DefaultArgument }));
}

void UMaterialExpressionPerInstanceCustomData3Vector::Build(MIR::FEmitter& Em)
{
	FValueRef DataIndexArgument = Em.ConstantInt(DataIndex);
	FValueRef DefaultArgument = Em.InputDefaultFloat3(&DefaultValue, FVector3f(ConstDefaultValue.R, ConstDefaultValue.G, ConstDefaultValue.B));

	Em.Output(0, EmitInlineHLSL(Em, *this, 0, { DataIndexArgument, DefaultArgument }));
}

void UMaterialExpressionPreviousFrameSwitch::Build(MIR::FEmitter& Em)
{
	FValueRef CurrentFrameValue = Em.Input(&CurrentFrame);
	FValueRef PreviousFrameValue = Em.Input(&PreviousFrame);
	UE_MIR_CHECKPOINT(Em);

	Em.Output(0, Em.Branch(Em.ExternalInput(MIR::EExternalInput::CompilingPreviousFrame), PreviousFrameValue, CurrentFrameValue));
}

void UMaterialExpressionHairAttributes::Build(MIR::FEmitter& Em)
{
	FValueRef HairUV = Em.InlineHLSL(MIR::FType::MakeFloatVector(2), TEXTVIEW("MaterialExpressionGetHairUV(Parameters)"));
	Em.Output(0, Em.Subscript(HairUV, 0));
	Em.Output(1, Em.Subscript(HairUV, 1));

	FValueRef HairDimensions = Em.InlineHLSL(MIR::FType::MakeFloatVector(2), TEXTVIEW("MaterialExpressionGetHairDimensions(Parameters)"));
	Em.Output(2, Em.Subscript(HairDimensions, 0));		// Length
	Em.Output(3, Em.Subscript(HairDimensions, 1));		// Radius

	Em.Output(4, Em.InlineHLSL(MIR::FType::MakeFloatScalar(), TEXTVIEW("MaterialExpressionGetHairSeed(Parameters)")));
	Em.Output(5, Em.InlineHLSL(MIR::FType::MakeFloatVector(3), TEXTVIEW("MaterialExpressionGetHairTangent(Parameters, $0)"), { Em.ConstantBool(bUseTangentSpace) }));
	Em.Output(6, Em.InlineHLSL(MIR::FType::MakeFloatVector(2), TEXTVIEW("MaterialExpressionGetHairRootUV(Parameters)")));
	Em.Output(7, Em.InlineHLSL(MIR::FType::MakeFloatVector(3), TEXTVIEW("MaterialExpressionGetHairBaseColor(Parameters)")));
	Em.Output(8, Em.InlineHLSL(MIR::FType::MakeFloatScalar(), TEXTVIEW("MaterialExpressionGetHairRoughness(Parameters)")));
	Em.Output(9, Em.InlineHLSL(MIR::FType::MakeFloatScalar(), TEXTVIEW("MaterialExpressionGetHairDepth(Parameters)")));
	Em.Output(10, Em.InlineHLSL(MIR::FType::MakeFloatScalar(), TEXTVIEW("MaterialExpressionGetHairCoverage(Parameters)")));
	Em.Output(11, Em.InlineHLSL(MIR::FType::MakeFloatVector(4), TEXTVIEW("MaterialExpressionGetHairAuxilaryData(Parameters)")));
	Em.Output(12, Em.InlineHLSL(MIR::FType::MakeFloatVector(2), TEXTVIEW("MaterialExpressionGetAtlasUVs(Parameters)")));
	Em.Output(13, Em.InlineHLSL(MIR::FType::MakeFloatScalar(), TEXTVIEW("MaterialExpressionGetHairGroupIndex(Parameters)")));
	Em.Output(14, Em.InlineHLSL(MIR::FType::MakeFloatScalar(), TEXTVIEW("MaterialExpressionGetHairAO(Parameters)")));
	Em.Output(15, Em.Subscript(Em.InlineHLSL(MIR::FType::MakeFloatVector(3), TEXTVIEW("MaterialExpressionGetHairClumpID(Parameters)")), 0));
}

void UMaterialExpressionHairColor::Build(MIR::FEmitter& Em)
{
	FValueRef MelaninInput = Em.InputDefaultFloat(&Melanin, 0.5f);
	FValueRef RednessInput = Em.InputDefaultFloat(&Redness, 0.0f);
	FValueRef DyeColorInput = Em.InputDefaultFloat3(&DyeColor, FVector3f(1.f, 1.f, 1.f));

	Em.Output(0, Em.InlineHLSL(MIR::FType::MakeFloatVector(3), TEXTVIEW("MaterialExpressionGetHairColorFromMelanin($0, $1, $2)"), { MelaninInput, RednessInput, DyeColorInput }));
}

void UMaterialExpressionMapARPassthroughCameraUV::Build(MIR::FEmitter& Em)
{
	FValueRef UV = Em.Input(&Coordinates);
	UE_MIR_CHECKPOINT(Em);

	FValueRef UVPair0 = Em.InlineHLSL(MIR::FType::MakeFloatVector(4), TEXTVIEW("ResolvedView.XRPassthroughCameraUVs[0]"));
	FValueRef UVPair1 = Em.InlineHLSL(MIR::FType::MakeFloatVector(4), TEXTVIEW("ResolvedView.XRPassthroughCameraUVs[1]"));

	FValueRef ULerp = Em.Lerp(UVPair0, UVPair1, Em.Subscript(UV, 0));
	Em.Output(0, Em.Lerp(Em.Swizzle(ULerp, MIR::FSwizzleMask(MIR::EVectorComponent::X, MIR::EVectorComponent::Y)), Em.Swizzle(ULerp, MIR::FSwizzleMask(MIR::EVectorComponent::Z, MIR::EVectorComponent::W)), Em.Subscript(UV, 1)));
}

void UMaterialExpressionSwitch::Build(MIR::FEmitter& Em)
{
	FValueRef CompiledDefault = Em.InputDefaultFloat(&Default, ConstDefault);

	// If no other inputs, just return the default
	if (Inputs.Num() == 0)
	{
		Em.Output(0, CompiledDefault);
		return;
	}

	// Only the "x" component of the switch value is used.
	FValueRef CompiledSwitchValue = Em.InputDefaultFloat(&SwitchValue, ConstSwitchValue);
	if (CompiledSwitchValue->Type.IsVector())
	{
		CompiledSwitchValue = Em.Subscript(CompiledSwitchValue, 0);
	}

	// Compile the inputs.
	TArray<FValueRef> CompiledInputs;
	CompiledInputs.SetNumUninitialized(Inputs.Num());

	for (int32 i = 0; i < Inputs.Num(); i++)
	{
		CompiledInputs[i] = Em.Input(&Inputs[i].Input);
	}
	UE_MIR_CHECKPOINT(Em);		// Make sure inputs are connected.

	// Get common type of inputs.  Done as a separate loop, to avoid spurious errors for unconnected inputs, which otherwise also produce "No common type" errors.
	MIR::FType CommonType = CompiledDefault->Type;
	for (int32 i = 0; i < Inputs.Num(); i++)
	{
		CommonType = Em.GetCommonType(CommonType, CompiledInputs[i]->Type);
	}
	UE_MIR_CHECKPOINT(Em);		// Make sure inputs have a valid common type.

	// If the switch value is a constant, we can directly pass the corresponding input as the result.
	const MIR::FConstant* CompiledSwitchValueConstant = CompiledSwitchValue->As<MIR::FConstant>();
	if (CompiledSwitchValueConstant)
	{
		int32 InputIndex = 0;
		switch (CompiledSwitchValue->Type.AsPrimitive()->ScalarKind)
		{
		case MIR::EScalarKind::Bool:	InputIndex = CompiledSwitchValueConstant->Boolean ? 1 : 0;  break;
		case MIR::EScalarKind::Int:		InputIndex = CompiledSwitchValueConstant->Integer;  break;
		case MIR::EScalarKind::Float:	InputIndex = FMath::FloorToInt(CompiledSwitchValueConstant->Float);  break;
		case MIR::EScalarKind::Double:	InputIndex = (int32)FMath::FloorToInt(CompiledSwitchValueConstant->Double);  break;
		default:  UE_MIR_UNREACHABLE();
		}

		if (Inputs.IsValidIndex(InputIndex))
		{
			Em.Output(0, Em.Cast(CompiledInputs[InputIndex], CommonType));
		}
		else
		{
			Em.Output(0, Em.Cast(CompiledDefault, CommonType));
		}
		return;
	}

	// Floor the switch value if it's a float, to prepare for comparisons.
	if (CompiledSwitchValue->Type.IsAnyFloat())
	{
		CompiledSwitchValue = Em.Floor(CompiledSwitchValue);
	}

	// Generate a switch statement as a chain of if..else branches.  We scan backwards, so the comparisons end up in order,
	// factoring in that each Branch is a parent of the previous Branch, and so the last Branch added is the first that gets
	// executed.  The first previous Branch (final else case) starts out as the default.
	FValueRef PreviousBranch = Em.Cast(CompiledDefault, CommonType);

	for (int32 i = Inputs.Num() - 1; i >= 0; i--)
	{
		PreviousBranch = Em.Branch(Em.Equals(CompiledSwitchValue, Em.ConstantInt(i)), Em.Cast(CompiledInputs[i], CommonType), PreviousBranch);
	}

	Em.Output(0, PreviousBranch);
}

#endif // WITH_EDITOR
