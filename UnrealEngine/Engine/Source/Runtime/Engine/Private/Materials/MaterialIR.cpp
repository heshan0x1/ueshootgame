// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialIR.h"
#include "Materials/MaterialIRTypes.h"
#include "Materials/MaterialAggregate.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Misc/LargeWorldRenderPosition.h"
#include "Engine/Texture.h"
#include "VT/RuntimeVirtualTexture.h"

#if WITH_EDITOR

namespace MIR
{

const TCHAR* LexToString(EStage Stage)
{
	switch (Stage)
	{
		case Stage_Vertex:	return TEXT("Vertex");
		case Stage_Pixel: 	return TEXT("Pixel");
		case Stage_Compute: return TEXT("Compute");
		case NumStages: UE_MIR_UNREACHABLE();
	}
	return nullptr;
}

const TCHAR* LexToString(EValueKind Kind)
{
	switch (Kind)
	{
		case VK_Poison: return TEXT("Poison");
		case VK_Constant: return TEXT("Constant");
		case VK_ExternalInput: return TEXT("ExternalInput");
		case VK_MaterialParameterCollection: return TEXT("MaterialParameterCollection");
		case VK_ScreenTexture: return TEXT("ScreenTexture");
		case VK_ShadingModel: return TEXT("ShadingModel");
		case VK_TextureObject: return TEXT("TextureObject");
		case VK_RuntimeVirtualTextureObject: return TEXT("RuntimeVirtualTextureObject");
		case VK_UniformParameter: return TEXT("UniformParameter");
		case VK_Composite: return TEXT("Composite");
		case VK_SetMaterialOutput: return TEXT("SetMaterialOutput");
		case VK_Operator: return TEXT("Operator");
		case VK_Branch: return TEXT("Branch");
		case VK_Subscript: return TEXT("Subscript");
		case VK_Scalar: return TEXT("Scalar");
		case VK_TextureRead: return TEXT("TextureRead");
		case VK_VTPageTableRead: return TEXT("VTPageTableRead");
		case VK_InlineHLSL: return TEXT("InlineHLSL");
		case VK_PromoteSubstrateParameter: return TEXT("PromoteSubstrateParameter");
		case VK_StageSwitch: return TEXT("StageSwitch");
		case VK_HardwarePartialDerivative: return TEXT("HardwarePartialDerivative");
		case VK_Nop: return TEXT("Nop");
		case VK_Call: return TEXT("Call");
		case VK_CallParameterOutput: return TEXT("CallParameterOutput");
		case VK_PreshaderParameter: return TEXT("PreshaderParameter");
		/* invalid entries */
		case VK_InstructionBegin:
		case VK_InstructionEnd:
			UE_MIR_UNREACHABLE();
	}
	return nullptr;
}

bool FValue::IsAnalyzed(EStage Stage) const
{
	return (Flags & EValueFlags(1 << Stage)) != EValueFlags::None;
}

bool FValue::HasFlags(EValueFlags InFlags) const
{
	return (Flags & InFlags) == InFlags;
}

void FValue::SetFlags(EValueFlags InFlags)
{
	Flags |= InFlags;
}

void FValue::ClearFlags(EValueFlags InFlags)
{
	Flags &= ~InFlags;
}

bool FValue::HasSubgraphProperties(EGraphProperties Properties) const
{
	return (GraphProperties & Properties) == Properties;
}

void FValue::UseSubgraphProperties(EGraphProperties Properties)
{
	GraphProperties |= Properties;
}

uint32 FValue::GetSizeInBytes() const
{
	switch (Kind)
	{
		case VK_Poison: return sizeof(FPoison);
		case VK_Constant: return sizeof(FConstant);
		case VK_ExternalInput: return sizeof(FExternalInput);
		case VK_MaterialParameterCollection: return sizeof(FMaterialParameterCollection);
		case VK_ScreenTexture: return sizeof(FScreenTexture);
		case VK_ShadingModel: return sizeof(FShadingModel);
		case VK_TextureObject: return sizeof(FTextureObject);
		case VK_RuntimeVirtualTextureObject: return sizeof(FRuntimeVirtualTextureObject);
		case VK_UniformParameter: return sizeof(FUniformParameter);
		case VK_Composite: return sizeof(FComposite) + sizeof(FValue*) * static_cast<const FComposite*>(this)->GetComponents().Num();
		case VK_SetMaterialOutput: return sizeof(FSetMaterialOutput);
		case VK_Operator: return sizeof(FOperator);
		case VK_Branch: return sizeof(FBranch);
		case VK_Subscript: return sizeof(FSubscript);
		case VK_Scalar: return sizeof(FScalar);
		case VK_TextureRead: return sizeof(FTextureRead);
		case VK_VTPageTableRead: return sizeof(FVTPageTableRead);
		case VK_InlineHLSL: return sizeof(FInlineHLSL);
		case VK_PromoteSubstrateParameter: return sizeof(FPromoteSubstrateParameter);
		case VK_StageSwitch: return sizeof(FStageSwitch);
		case VK_HardwarePartialDerivative: return sizeof(FHardwarePartialDerivative);
		case VK_Nop: return sizeof(FNop);
		case VK_Call: return sizeof(FCall);
		case VK_CallParameterOutput: return sizeof(FCallParameterOutput);
		case VK_PreshaderParameter: return sizeof(FPreshaderParameter);
			/* invalid entries */
		case VK_InstructionBegin:
		case VK_InstructionEnd:
			UE_MIR_UNREACHABLE();
	}
	return 0;
}

TConstArrayView<FValue*> FValue::GetUses() const
{
	// Values have no uses by definition.
	if (Kind < VK_InstructionBegin)
	{
		return {};
	}

	switch (Kind)
	{
		case VK_Composite:
		{
			auto This = static_cast<const FComposite*>(this);
			return This->GetComponents();
		}

		case VK_SetMaterialOutput:
		{
			auto This = static_cast<const FSetMaterialOutput*>(this);
			return { &This->Arg, FSetMaterialOutput::NumStaticUses };
		}

		case VK_Operator:
		{
			auto This = static_cast<const FOperator*>(this);
			return { &This->AArg, FOperator::NumStaticUses };
		}

		case VK_Branch:
		{
			auto This = static_cast<const FBranch*>(this);
			return { &This->ConditionArg, FBranch::NumStaticUses };
		}

		case VK_Subscript:
		{
			auto This = static_cast<const FSubscript*>(this);
			return { &This->Arg, FSubscript::NumStaticUses };
		}

		case VK_Scalar:
		{
			auto This = static_cast<const FScalar*>(this);
			return { &This->Arg, FScalar::NumStaticUses };
		}
			
		case VK_TextureRead:
		{
			auto This = static_cast<const FTextureRead*>(this);
			return { &This->TextureObject, FTextureRead::NumStaticUses };
		}

		case VK_VTPageTableRead:
		{
			auto This = static_cast<const FVTPageTableRead*>(this);
			return { &This->TextureObject, FVTPageTableRead::NumStaticUses };
		}

		case VK_InlineHLSL:
		{
			auto This = static_cast<const FInlineHLSL*>(this);
			return { This->Arguments, This->NumArguments };
		}

		case VK_PromoteSubstrateParameter:
		{
			auto This = static_cast<const FPromoteSubstrateParameter*>(this);
			return { This->WorldSpaceTangentsAndNormals, FPromoteSubstrateParameter::NumStaticUses };
		}

		case VK_StageSwitch:
		{
			auto This = static_cast<const FStageSwitch*>(this);
			return { This->Args, FStageSwitch::NumStaticUses * NumStages };
		}

		case VK_HardwarePartialDerivative:
		{
			auto This = static_cast<const FHardwarePartialDerivative*>(this);
			return { &This->Arg, FHardwarePartialDerivative::NumStaticUses };
		}

		case VK_Nop:
		{
			auto This = static_cast<const FNop*>(this);
			return { &This->Arg, FNop::NumStaticUses };
		}

		case VK_Call:
		{
			auto This = static_cast<const FCall*>(this);
			return { This->Arguments, This->NumArguments };
		}

		case VK_CallParameterOutput:
		{
			auto This = static_cast<const FCallParameterOutput*>(this);
			return { &This->Call, FCallParameterOutput::NumStaticUses };
		}

		case VK_PreshaderParameter:
		{
			auto This = static_cast<const FPreshaderParameter*>(this);
			return { &This->SourceParameter, FPreshaderParameter::NumStaticUses };
		}

		default: UE_MIR_UNREACHABLE();
	}
}

TConstArrayView<FValue*> FValue::GetUsesForStage(MIR::EStage Stage) const
{
	if (const FStageSwitch* This = As<FStageSwitch>())
	{
		return { &This->Args[int32(Stage)], FStageSwitch::NumStaticUses };
	}
	return GetUses();
}

bool FValue::IsTrue() const
{
	const MIR::FConstant* Constant = As<MIR::FConstant>();
	return Constant && Constant->Type.IsBoolean() && Constant->Boolean == true;
}

bool FValue::IsFalse() const
{
	const MIR::FConstant* Constant = As<MIR::FConstant>();
	return Constant && Constant->Type.IsBoolean() && Constant->Boolean == false;
}

bool FValue::AreAllTrue() const
{
	if (const MIR::FComposite* Composite = As<MIR::FComposite>())
	{
		for (const MIR::FValue* Component : Composite->GetComponents())
		{
			if (!Component->IsTrue())
			{
				return false;
			}
		}
		return true;
	}
	else
	{
		return IsTrue();
	}
}

bool FValue::AreAllFalse() const
{
	if (const MIR::FComposite* Composite = As<MIR::FComposite>())
	{
		for (const MIR::FValue* Component : Composite->GetComponents())
		{
			if (!Component->IsFalse())
			{
				return false;
			}
		}
		return true;
	}
	else
	{
		return IsFalse();
	}
}

bool FValue::AreAllExactlyZero() const
{
	if (const MIR::FComposite* Composite = As<MIR::FComposite>())
	{
		for (const MIR::FValue* Component : Composite->GetComponents())
		{
			if (!Component->AreAllExactlyZero())
			{
				return false;
			}
		}
		return true;
	}
	else if (const MIR::FConstant* Constant = As<MIR::FConstant>())
	{
		return (Constant->Type.IsInteger() && Constant->Integer == 0)
			|| (Constant->Type.IsFloat() && Constant->Float == 0.0f);
	}
	return false;
}

bool FValue::AreAllNearlyZero() const
{
	if (const MIR::FComposite* Composite = As<MIR::FComposite>())
	{
		for (const MIR::FValue* Component : Composite->GetComponents())
		{
			if (!Component->AreAllNearlyZero())
			{
				return false;
			}
		}
		return true;
	}
	else if (const MIR::FConstant* Constant = As<MIR::FConstant>())
	{
		return (Constant->Type.IsInteger() && Constant->Integer == 0)
			|| (Constant->Type.IsFloat() && FMath::IsNearlyZero(Constant->Float));
	}
	return false;
}

bool FValue::AreAllExactlyOne() const
{
	if (const MIR::FComposite* Composite = As<MIR::FComposite>())
	{
		for (const MIR::FValue* Component : Composite->GetComponents())
		{
			if (!Component->AreAllExactlyOne())
			{
				return false;
			}
		}
		return true;
	}
	else if (const MIR::FConstant* Constant = As<MIR::FConstant>())
	{
		return (Constant->Type.IsInteger() && Constant->Integer == 1)
			|| (Constant->Type.IsFloat() && Constant->Float == 1.0f);
	}
	return false;
}

bool FValue::AreAllNearlyOne() const
{
	if (const MIR::FComposite* Composite = As<MIR::FComposite>())
	{
		for (const MIR::FValue* Component : Composite->GetComponents())
		{
			if (!Component->AreAllNearlyOne())
			{
				return false;
			}
		}
		return true;
	}
	else if (const MIR::FConstant* Constant = As<MIR::FConstant>())
	{
		return (Constant->Type.IsInteger() && Constant->Integer == 1)
			|| (Constant->Type.IsFloat() && FMath::IsNearlyEqual(Constant->Float, 1.0f));
	}
	return false;
}

bool FValue::Equals(const FValue* Other) const
{
	if (this == Other)
	{
		return true;
	}

	// Get the size of this value in bytes. It should match that of Other, since the value kinds are the same.
	uint32 SizeInBytes = GetSizeInBytes();
	if (SizeInBytes != Other->GetSizeInBytes())
	{
		return false;
	}

	// Values are PODs by design, therefore simply comparing bytes is sufficient.
	return FMemory::Memcmp(this, Other, SizeInBytes) == 0;
}

bool FValue::EqualsConstant(float TestValue) const
{
	const MIR::FConstant* ValueConstant = this->As<MIR::FConstant>();

	if (ValueConstant)
	{
		switch (ValueConstant->Type.GetPrimitive().ScalarKind)
		{
			case MIR::EScalarKind::Bool:	return ValueConstant->Boolean == !!TestValue;
			case MIR::EScalarKind::Int:		return ValueConstant->Integer == (TInteger)TestValue;
			case MIR::EScalarKind::Float:	return ValueConstant->Float == TestValue;
			case MIR::EScalarKind::Double:	return ValueConstant->Double == TestValue;
			default: UE_MIR_UNREACHABLE();
		}
	}
	return false;
}

bool FValue::EqualsConstant(FVector4f TestValue) const
{
	if (this->Kind == MIR::VK_Constant)
	{
		return EqualsConstant(TestValue.X);
	}
	else if (this->Kind == MIR::VK_Composite)
	{
		TConstArrayView<FValue*> Components = static_cast<const FComposite*>(this)->GetComponents();
		int32 NumComponents = FMath::Min(Components.Num(), 4);

		for (int32 ComponentIndex = 0; ComponentIndex < NumComponents; ++ComponentIndex)
		{
			if (!Components[ComponentIndex]->EqualsConstant(TestValue[ComponentIndex]))
			{
				return false;
			}
		}
		return true;
	}
	else
	{
		return false;
	}
}

UObject* FValue::GetTextureObject() const
{
	if (const FTextureObject* TextureObject = As<FTextureObject>())
	{
		return TextureObject->Texture;
	}
	if (const FRuntimeVirtualTextureObject* RVTextureObject = As<FRuntimeVirtualTextureObject>())
	{
		return RVTextureObject->RVTexture;
	}
	return nullptr;
}

int32 FValue::GetUniformParameterIndex() const
{
	if (const MIR::FTextureObject* TextureObject = As<MIR::FTextureObject>())
	{
		return TextureObject->Analysis_UniformParameterIndex;
	}
	if (const MIR::FRuntimeVirtualTextureObject* RVTextureObject = As<MIR::FRuntimeVirtualTextureObject>())
	{
		return RVTextureObject->Analysis_UniformParameterIndex;
	}
	if (const MIR::FUniformParameter* UniformParameter = As<MIR::FUniformParameter>())
	{
		return UniformParameter->Analysis_UniformParameterIndex;
	}
	return INDEX_NONE;
}

FInstruction* AsInstruction(FValue* Value)
{
	return Value && (Value->Kind > VK_InstructionBegin && Value->Kind < VK_InstructionEnd) ? static_cast<FInstruction*>(Value) : nullptr;
}

const FInstruction* AsInstruction(const FValue* Value)
{
	return AsInstruction(const_cast<FValue*>(Value));
}

FPoison* FPoison::Get()
{
	static FPoison Poison = [] {
		FPoison P;
		P.Kind = VK_Poison;
		P.Type = FType::MakePoison();
		return P;
	} ();
	return &Poison;
}

const TCHAR* LexToString(EExternalInput Input)
{
	switch (Input)
	{
		case EExternalInput::TexCoord0:     return TEXT("TexCoord0");
		case EExternalInput::TexCoord1:     return TEXT("TexCoord1");
		case EExternalInput::TexCoord2:     return TEXT("TexCoord2");
		case EExternalInput::TexCoord3:     return TEXT("TexCoord3");
		case EExternalInput::TexCoord4:     return TEXT("TexCoord4");
		case EExternalInput::TexCoord5:     return TEXT("TexCoord5");
		case EExternalInput::TexCoord6:     return TEXT("TexCoord6");
		case EExternalInput::TexCoord7:     return TEXT("TexCoord7");
		case EExternalInput::WorldPosition_Absolute: return TEXT("WorldPosition_Absolute");
		case EExternalInput::WorldPosition_AbsoluteNoOffsets: return TEXT("WorldPosition_AbsoluteNoOffsets");
		case EExternalInput::WorldPosition_CameraRelative: return TEXT("WorldPosition_CameraRelative");
		case EExternalInput::WorldPosition_CameraRelativeNoOffsets: return TEXT("WorldPosition_CameraRelativeNoOffsets");
		case EExternalInput::LocalPosition_Instance:			return TEXT("LocalPosition_Instance");
		case EExternalInput::LocalPosition_InstanceNoOffsets:	return TEXT("LocalPosition_InstanceNoOffsets");
		case EExternalInput::LocalPosition_Primitive:			return TEXT("LocalPosition_Primitive");
		case EExternalInput::LocalPosition_PrimitiveNoOffsets:	return TEXT("LocalPosition_PrimitiveNoOffsets");

		case EExternalInput::TexCoord0_Ddx: return TEXT("TexCoord0_Ddx");
		case EExternalInput::TexCoord1_Ddx: return TEXT("TexCoord1_Ddx");
		case EExternalInput::TexCoord2_Ddx: return TEXT("TexCoord2_Ddx");
		case EExternalInput::TexCoord3_Ddx: return TEXT("TexCoord3_Ddx");
		case EExternalInput::TexCoord4_Ddx: return TEXT("TexCoord4_Ddx");
		case EExternalInput::TexCoord5_Ddx: return TEXT("TexCoord5_Ddx");
		case EExternalInput::TexCoord6_Ddx: return TEXT("TexCoord6_Ddx");
		case EExternalInput::TexCoord7_Ddx: return TEXT("TexCoord7_Ddx");
		case EExternalInput::WorldPosition_Absolute_Ddx: return TEXT("WorldPosition_Absolute_Ddx");
		case EExternalInput::WorldPosition_AbsoluteNoOffsets_Ddx: return TEXT("WorldPosition_AbsoluteNoOffsets_Ddx");
		case EExternalInput::WorldPosition_CameraRelative_Ddx: return TEXT("WorldPosition_CameraRelative_Ddx");
		case EExternalInput::WorldPosition_CameraRelativeNoOffsets_Ddx: return TEXT("WorldPosition_CameraRelativeNoOffsets_Ddx");
		case EExternalInput::LocalPosition_Instance_Ddx:			return TEXT("LocalPosition_Instance_Ddx");
		case EExternalInput::LocalPosition_InstanceNoOffsets_Ddx:	return TEXT("LocalPosition_InstanceNoOffsets_Ddx");
		case EExternalInput::LocalPosition_Primitive_Ddx:			return TEXT("LocalPosition_Primitive_Ddx");
		case EExternalInput::LocalPosition_PrimitiveNoOffsets_Ddx:	return TEXT("LocalPosition_PrimitiveNoOffsets_Ddx");

		case EExternalInput::TexCoord0_Ddy: return TEXT("TexCoord0_Ddy");
		case EExternalInput::TexCoord1_Ddy: return TEXT("TexCoord1_Ddy");
		case EExternalInput::TexCoord2_Ddy: return TEXT("TexCoord2_Ddy");
		case EExternalInput::TexCoord3_Ddy: return TEXT("TexCoord3_Ddy");
		case EExternalInput::TexCoord4_Ddy: return TEXT("TexCoord4_Ddy");
		case EExternalInput::TexCoord5_Ddy: return TEXT("TexCoord5_Ddy");
		case EExternalInput::TexCoord6_Ddy: return TEXT("TexCoord6_Ddy");
		case EExternalInput::TexCoord7_Ddy: return TEXT("TexCoord7_Ddy");
		case EExternalInput::WorldPosition_Absolute_Ddy: return TEXT("WorldPosition_Absolute_Ddy");
		case EExternalInput::WorldPosition_AbsoluteNoOffsets_Ddy: return TEXT("WorldPosition_AbsoluteNoOffsets_Ddy");
		case EExternalInput::WorldPosition_CameraRelative_Ddy: return TEXT("WorldPosition_CameraRelative_Ddy");
		case EExternalInput::WorldPosition_CameraRelativeNoOffsets_Ddy: return TEXT("WorldPosition_CameraRelativeNoOffsets_Ddy");
		case EExternalInput::LocalPosition_Instance_Ddy:			return TEXT("LocalPosition_Instance_Ddy");
		case EExternalInput::LocalPosition_InstanceNoOffsets_Ddy:	return TEXT("LocalPosition_InstanceNoOffsets_Ddy");
		case EExternalInput::LocalPosition_Primitive_Ddy:			return TEXT("LocalPosition_Primitive_Ddy");
		case EExternalInput::LocalPosition_PrimitiveNoOffsets_Ddy:	return TEXT("LocalPosition_PrimitiveNoOffsets_Ddy");

		case EExternalInput::ActorPosition_Absolute: return TEXT("ActorPosition_Absolute");
		case EExternalInput::ActorPosition_CameraRelative: return TEXT("ActorPosition_CameraRelative");
		case EExternalInput::ObjectPosition_Absolute: return TEXT("ObjectPosition_Absolute");
		case EExternalInput::ObjectPosition_CameraRelative: return TEXT("ObjectPosition_CameraRelative");
		case EExternalInput::ViewMaterialTextureMipBias: return TEXT("ViewMaterialTextureMipBias");
		case EExternalInput::ViewMaterialTextureDerivativeMultiply: return TEXT("ViewMaterialTextureDerivativeMultiply");
		case EExternalInput::GlobalDistanceField: return TEXT("GlobalDistanceField");
		case EExternalInput::DynamicParticleParameterIndex: return TEXT("DynamicParticleParameterIndex");
		case EExternalInput::CompilingPreviousFrame: return TEXT("CompilingPreviousFrame");
		default: UE_MIR_UNREACHABLE();
	}
}

FType GetExternalInputType(EExternalInput Id)
{
	if (IsExternalInputTexCoordOrPartialDerivative(Id))
	{
		return FType::MakeFloatVector(2);
	}
	
	switch (Id)
	{
		case EExternalInput::ViewMaterialTextureMipBias:
		case EExternalInput::ViewMaterialTextureDerivativeMultiply:
		case EExternalInput::GlobalDistanceField:						// Type is arbitrary, value not actually used, only as a flag for value analyzer
			 return FType::MakeFloatScalar();

		case EExternalInput::ActorPosition_Absolute:
		case EExternalInput::ObjectPosition_Absolute:
		case EExternalInput::WorldPosition_Absolute:
		case EExternalInput::WorldPosition_AbsoluteNoOffsets:
		case EExternalInput::WorldPosition_Absolute_Ddx:				// Technically these derivatives are not LWC, but the emitter needs them to be for evaluation of
		case EExternalInput::WorldPosition_AbsoluteNoOffsets_Ddx:		// analytic derivative expressions.  So we cast them to LWC on load.  The shader compiler should
		case EExternalInput::WorldPosition_Absolute_Ddy:				// be able to optimize the math (the tile offsets will all be zero, cancelling things out).
		case EExternalInput::WorldPosition_AbsoluteNoOffsets_Ddy:
			return FType::MakeDoubleVector(3);

		case EExternalInput::ActorPosition_CameraRelative:
		case EExternalInput::ObjectPosition_CameraRelative:
		case EExternalInput::WorldPosition_CameraRelative:
		case EExternalInput::WorldPosition_CameraRelativeNoOffsets:
		case EExternalInput::WorldPosition_CameraRelative_Ddx:
		case EExternalInput::WorldPosition_CameraRelativeNoOffsets_Ddx:
		case EExternalInput::WorldPosition_CameraRelative_Ddy:
		case EExternalInput::WorldPosition_CameraRelativeNoOffsets_Ddy:
		case EExternalInput::LocalPosition_Instance:
		case EExternalInput::LocalPosition_InstanceNoOffsets:
		case EExternalInput::LocalPosition_Primitive:
		case EExternalInput::LocalPosition_PrimitiveNoOffsets:
		case EExternalInput::LocalPosition_Instance_Ddx:
		case EExternalInput::LocalPosition_InstanceNoOffsets_Ddx:
		case EExternalInput::LocalPosition_Primitive_Ddx:
		case EExternalInput::LocalPosition_PrimitiveNoOffsets_Ddx:
		case EExternalInput::LocalPosition_Instance_Ddy:
		case EExternalInput::LocalPosition_InstanceNoOffsets_Ddy:
		case EExternalInput::LocalPosition_Primitive_Ddy:
		case EExternalInput::LocalPosition_PrimitiveNoOffsets_Ddy:
			return FType::MakeFloatVector(3);

		case EExternalInput::DynamicParticleParameterIndex:
			return FType::MakeIntScalar();

		case EExternalInput::CompilingPreviousFrame:
			return FType::MakeBoolScalar();

		default: UE_MIR_UNREACHABLE();
	}
}

// Validate derivative related assumptions in EExternalInput enum
static_assert(EExternalInput::WithDerivatives_First == EExternalInput::TexCoord0);
static_assert(EExternalInput::WithDerivatives_Last == EExternalInput::WithDerivatives_LastDdy);
static_assert((int32)EExternalInput::WithDerivatives_LastDdx - (int32)EExternalInput::WithDerivatives_LastVal == ExternalInputWithDerivativesNum);
static_assert((int32)EExternalInput::WithDerivatives_LastDdy - (int32)EExternalInput::WithDerivatives_LastDdx == ExternalInputWithDerivativesNum);

bool IsExternalInputWithDerivatives(EExternalInput Id)
{
	return Id >= EExternalInput::WithDerivatives_First && Id <= EExternalInput::WithDerivatives_Last;
}

EExternalInput GetExternalInputDerivative(EExternalInput Id, EDerivativeAxis Axis)
{
	check(IsExternalInputWithDerivatives(Id));

	int32 IntId = (int32(Id) - (int32)MIR::EExternalInput::WithDerivatives_First) % ExternalInputWithDerivativesNum + (int32)MIR::EExternalInput::WithDerivatives_First;

	// Add one to axis enum (X==0, Y==1) to produce a value with range 1 to 2 for computing offset to the derivative variation of a value
	return (EExternalInput)(IntId + ((int)Axis + 1) * ExternalInputWithDerivativesNum);
}

MIR::EExternalInput TexCoordIndexToExternalInput(int32 TexCoordIndex)
{
	check(TexCoordIndex < TexCoordMaxNum);
	return (MIR::EExternalInput)((int)MIR::EExternalInput::TexCoord0 + TexCoordIndex);
}

int32 ExternalInputToTexCoordIndex(EExternalInput Id)
{
	check(IsExternalInputTexCoordOrPartialDerivative(Id));
	return (int32(Id) - (int32)MIR::EExternalInput::WithDerivatives_First) % ExternalInputWithDerivativesNum;
}

bool IsExternalInputTexCoord(EExternalInput Id)
{
	return Id >= EExternalInput::TexCoord0 && Id <= EExternalInput::TexCoord7;
}

bool IsExternalInputTexCoordDdx(EExternalInput Id)
{
	return Id >= EExternalInput::TexCoord0_Ddx && Id <= EExternalInput::TexCoord7_Ddx;
}

bool IsExternalInputTexCoordDdy(EExternalInput Id)
{
	return Id >= EExternalInput::TexCoord0_Ddy && Id <= EExternalInput::TexCoord7_Ddy;
}

bool IsExternalInputTexCoordOrPartialDerivative(EExternalInput Id)
{
	return IsExternalInputTexCoord(Id) || IsExternalInputTexCoordDdx(Id) || IsExternalInputTexCoordDdy(Id);
}

bool IsExternalInputWorldPosition(EExternalInput Id)
{
	return
		(Id >= EExternalInput::WorldPosition_Absolute && Id <= EExternalInput::WorldPosition_CameraRelativeNoOffsets) ||
		(Id >= EExternalInput::WorldPosition_Absolute_Ddx && Id <= EExternalInput::WorldPosition_CameraRelativeNoOffsets_Ddx) ||
		(Id >= EExternalInput::WorldPosition_Absolute_Ddy && Id <= EExternalInput::WorldPosition_CameraRelativeNoOffsets_Ddy);
}

FBlock* FBlock::FindCommonParentWith(MIR::FBlock* Other)
{
	FBlock* A = this;
	FBlock* B = Other;

	if (A == B)
	{
		return A;
	}

	while (A->Level > B->Level)
	{
		A = A->Parent;
	}

	while (B->Level > A->Level)
	{
		B = B->Parent;
	}

	while (A != B)
	{
		A = A->Parent;
		B = B->Parent;
	}

	return A;
}

// This global is needed to allow the Visual Studio debugger (and subsequently Natvis) to have access to the "TComposite<1>" type identifer, as used in the function below.
TComposite<1> GCompositeNatvisPrototype;

TConstArrayView<FValue*> FComposite::GetComponents() const
{
	int32 NumComponents;
	if (TOptional<FPrimitive> Primitive = Type.AsPrimitive())
	{
		NumComponents = Primitive->NumComponents();
	}
	else if (const UMaterialAggregate* Aggregate = Type.AsAggregate())
	{
		NumComponents = Aggregate->Attributes.Num();
	}
	else
	{
		UE_MIR_UNREACHABLE();
	}

	auto Ptr = static_cast<const TComposite<1>*>(this)->Components;
	return { Ptr, NumComponents };
}

TArrayView<FValue*> FComposite::GetMutableComponents()
{
	// const_cast is okay here as it's used only to get the array of components
	TConstArrayView<FValue*> Components = static_cast<const FComposite*>(this)->GetComponents();
	return { const_cast<FValue**>(Components.GetData()), Components.Num() };
}

bool FComposite::AreComponentsConstant() const
{
	for (FValue const* Component : GetComponents())
	{
		if (!Component->As<FConstant>())
		{
			return false;
		}
	}
	return true;
}

FBlock* FInstruction::GetTargetBlockForUse(int32 EntryPointIndex, int32 UseIndex)
{
	if (auto Branch = As<FBranch>())
	{
		switch (UseIndex)
		{
			case 0: return Linkage[EntryPointIndex].Block; 		// ConditionArg goes into the same block as this instruction's
			case 1: return &Branch->TrueBlock[EntryPointIndex]; 	// TrueArg
			case 2: return &Branch->FalseBlock[EntryPointIndex]; // FalseArg
			default: UE_MIR_UNREACHABLE();
		}
	}

	// By default, dependencies can go in the same block as this instruction
	return Linkage[EntryPointIndex].Block;
}

bool IsComparisonOperator(EOperator Op)
{
	switch (Op)
	{
		case UO_Not:
		case UO_IsFinite:
		case UO_IsInf:
		case UO_IsNan:
		case BO_Equals:
		case BO_GreaterThan:
		case BO_GreaterThanOrEquals:
		case BO_LessThan:
		case BO_LessThanOrEquals:
		case BO_NotEquals:
			return true;
		default:
			return false;
	}
}

bool IsUnaryOperator(EOperator Op)
{
	return Op >= UO_FirstUnaryOperator && Op < BO_FirstBinaryOperator;
}

bool IsBinaryOperator(EOperator Op)
{
	return Op >= BO_FirstBinaryOperator && Op < TO_FirstTernaryOperator;
}

bool IsTernaryOperator(EOperator Op)
{
	return Op >= TO_FirstTernaryOperator;
}

int GetOperatorArity(EOperator Op)
{
	return IsUnaryOperator(Op) ? 1
		: IsBinaryOperator(Op) ? 2
		: 3;
}

const TCHAR* LexToString(EOperator Op)
{
	// Note: sorted alphabetically
	switch (Op)
	{
		/* Unary operators */
		case UO_Abs: return TEXT("Abs");
		case UO_ACos: return TEXT("ACos");
		case UO_ACosFast: return TEXT("ACosFast");
		case UO_ACosh: return TEXT("ACosh");
		case UO_ASin: return TEXT("ASin");
		case UO_ASinFast: return TEXT("ASinFast");
		case UO_ASinh: return TEXT("ASinh");
		case UO_ATan: return TEXT("ATan");
		case UO_ATanFast: return TEXT("ATanFast");
		case UO_ATanh: return TEXT("ATanh");
		case UO_BitwiseNot: return TEXT("BitwiseNot");
		case UO_Ceil: return TEXT("Ceil");
		case UO_Cos: return TEXT("Cos");
		case UO_Cosh: return TEXT("Cosh");
		case UO_Exponential: return TEXT("Exponential");
		case UO_Exponential2: return TEXT("Exponential2");
		case UO_Floor: return TEXT("Floor");
		case UO_Frac: return TEXT("Frac");
		case UO_IsFinite: return TEXT("IsFinite");
		case UO_IsInf: return TEXT("IsInf");
		case UO_IsNan: return TEXT("IsNan");
		case UO_Length: return TEXT("Length");
		case UO_Logarithm: return TEXT("Logarithm");
		case UO_Logarithm10: return TEXT("Logarithm10");
		case UO_Logarithm2: return TEXT("Logarithm2");
		case UO_LWCTile: return TEXT("LWCTile");
		case UO_Negate: return TEXT("Negate");
		case UO_Not: return TEXT("Not");
		case UO_Reciprocal: return TEXT("Reciprocal");
		case UO_Round: return TEXT("Round");
		case UO_Rsqrt: return TEXT("Rsqrt");
		case UO_Saturate: return TEXT("Saturate");
		case UO_Sign: return TEXT("Sign");
		case UO_Sin: return TEXT("Sin");
		case UO_Sinh: return TEXT("Sinh");
		case UO_Sqrt: return TEXT("Sqrt");
		case UO_Tan: return TEXT("Tan");
		case UO_Tanh: return TEXT("Tanh");
		case UO_Transpose: return TEXT("Transpose");
		case UO_Truncate: return TEXT("Truncate");

		/* Binary operators */
		case BO_Add: return TEXT("Add");
		case BO_And: return TEXT("And");
		case BO_ATan2: return TEXT("ATan2");
		case BO_ATan2Fast: return TEXT("ATan2Fast");
		case BO_BitShiftLeft: return TEXT("BitShiftLeft");
		case BO_BitShiftRight: return TEXT("BitShiftRight");
		case BO_BitwiseAnd: return TEXT("BitwiseAnd");
		case BO_BitwiseOr: return TEXT("BitwiseOr");
		case BO_Cross: return TEXT("Cross");
		case BO_Distance: return TEXT("Distance");
		case BO_Divide: return TEXT("Divide");
		case BO_Dot: return TEXT("Dot");
		case BO_Equals: return TEXT("Equals");
		case BO_Fmod: return TEXT("Fmod");
		case BO_GreaterThan: return TEXT("GreaterThan");
		case BO_GreaterThanOrEquals: return TEXT("GreaterThanOrEquals");
		case BO_LessThan: return TEXT("LessThan");
		case BO_LessThanOrEquals: return TEXT("LessThanOrEquals");
		case BO_Max: return TEXT("Max");
		case BO_Min: return TEXT("Min");
		case BO_Modulo: return TEXT("Modulo");
		case BO_Multiply: return TEXT("Multiply");
		case BO_MatrixMultiply: return TEXT("MatrixMultiply");
		case BO_NotEquals: return TEXT("NotEquals");
		case BO_Or: return TEXT("Or");
		case BO_Pow: return TEXT("Pow");
		case BO_Step: return TEXT("Step");
		case BO_Subtract: return TEXT("Subtract");

		/* Ternary operators */
		case TO_Clamp: return TEXT("Clamp");
		case TO_Lerp: return TEXT("Lerp");
		case TO_Select: return TEXT("Select");
		case TO_Smoothstep: return TEXT("Smoothstep");
		
		case O_Invalid: return TEXT("Invalid");
		case OperatorCount: UE_MIR_UNREACHABLE();
	}

	return TEXT("???");
}

const TCHAR* LexToString(ETextureReadMode Mode)
{
	switch (Mode)
	{
		case ETextureReadMode::GatherRed: return TEXT("GatherRed");
		case ETextureReadMode::GatherGreen: return TEXT("GatherGreen");
		case ETextureReadMode::GatherBlue: return TEXT("GatherBlue");
		case ETextureReadMode::GatherAlpha: return TEXT("GatherAlpha");
		case ETextureReadMode::MipAuto: return TEXT("MipAuto");
		case ETextureReadMode::MipLevel: return TEXT("MipLevel");
		case ETextureReadMode::MipBias: return TEXT("MipBias");
		case ETextureReadMode::Derivatives: return TEXT("Derivatives");
		default: UE_MIR_UNREACHABLE();
	}
}

void FStageSwitch::SetArgs(FValue* PixelStageArg, FValue* OtherStagesArg)
{
	for (uint32 i = 0; i < NumStages; ++i)
	{
		Args[i]= (i == Stage_Pixel) ? PixelStageArg : OtherStagesArg;
	}
}

bool FFunction::Equals(const FFunction* Other) const
{
	return Kind == Other->Kind
		&& Name == Other->Name
		&& ReturnType == Other->ReturnType
		&& NumInputOnlyParams == Other->NumInputOnlyParams
		&& NumInputAndOutputParams == Other->NumInputAndOutputParams
		&& NumParameters == Other->NumParameters
		&& FMemory::Memcmp(Parameters, Other->Parameters, NumParameters * sizeof(FFunctionParameter)) == 0;
}

bool FFunctionHLSL::Equals(const FFunctionHLSL* Other) const
{
	return FFunction::Equals(Other) && Code == Other->Code;
}

} // namespace MIR

#endif // #if WITH_EDITOR
