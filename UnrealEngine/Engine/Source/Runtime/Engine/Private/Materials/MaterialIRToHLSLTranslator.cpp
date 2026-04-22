// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialIRToHLSLTranslator.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Engine/Font.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "VT/RuntimeVirtualTexture.h"
#include "MaterialDomain.h"
#include "MaterialIRInternal.h"
#include "Materials/Material.h"
#include "Materials/MaterialAggregate.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialExpressionVolumetricAdvancedMaterialOutput.h"
#include "Materials/MaterialExternalCodeRegistry.h"
#include "Materials/MaterialIR.h"
#include "Materials/MaterialIRModule.h"
#include "Materials/MaterialIRTypes.h"
#include "Materials/MaterialParameterCollection.h"
#include "MaterialShared.h"
#include "MaterialSharedPrivate.h"
#include "Misc/LargeWorldRenderPosition.h"
#include "ParameterCollection.h"
#include "RenderUtils.h"
#include "ShaderCore.h"

#include <inttypes.h>

#if WITH_EDITOR

enum ENoOp { NoOp };
enum ENewLine { NewLine };
enum EEndOfStatement { EndOfStatement };
enum EOpenBrace { OpenBrace };
enum ECloseBrace { CloseBrace };
enum EIndentation { Indentation };
enum EBeginArgs { BeginArgs };
enum EEndArgs { EndArgs };
enum EListSeparator { ListSeparator };

#define TAB "    "

struct FHLSLPrinter
{
	FString Buffer;
	bool bFirstListItem = false;
	int32 Tabs = 0;

	FHLSLPrinter& operator<<(FStringView Text)
	{
		Buffer.Append(Text);
		return *this;
	}

	FHLSLPrinter& operator<<(int32 Value)
	{
		Buffer.Appendf(TEXT("%d"), Value);
		return *this;
	}

	FHLSLPrinter& operator<<(uint32 Value)
	{
		Buffer.Appendf(TEXT("%u"), Value);
		return *this;
	}

	FHLSLPrinter& operator<<(float Value)
	{
		if (FGenericPlatformMath::IsNaN(Value))
		{
			Buffer.Append(TEXTVIEW("(0.0f / 0.0f)"));
		}
		else if (!FGenericPlatformMath::IsFinite(Value))
		{
			Buffer.Append(TEXTVIEW("INFINITE_FLOAT"));
		}
		else
		{
			Buffer.Appendf(TEXT("%#.8gf"), Value);
		}
		return *this;
	}

    FHLSLPrinter& operator<<(ENoOp)
	{
		return *this;
	}

    FHLSLPrinter& operator<<(ENewLine)
    {
		Buffer.AppendChar('\n');
		operator<<(Indentation);
        return *this;
    }

	FHLSLPrinter& operator<<(EIndentation)
	{
		for (int32 i = 0; i < Tabs; ++i)
		{
			Buffer.AppendChar('\t');
		}
		return *this;
	}

	FHLSLPrinter& operator<<(EEndOfStatement)
	{
		Buffer.AppendChar(';');
        *this << NewLine;
        return *this;
	}

    FHLSLPrinter& operator<<(EOpenBrace)
    {
		Buffer.AppendChar('{');
        ++Tabs;
        *this << NewLine;
        return *this;
    }

    FHLSLPrinter& operator<<(ECloseBrace)
    {
        --Tabs;
        Buffer.LeftChopInline(1); // undo tab
        Buffer.AppendChar('}');
        return *this;
    }
	
    FHLSLPrinter& operator<<(EBeginArgs)
    {
        Buffer.AppendChar('(');
		BeginList();
        return *this;
    }

    FHLSLPrinter& operator<<(EEndArgs)
    {
        Buffer.AppendChar(')');
        return *this;
    }

	FHLSLPrinter& operator<<(EListSeparator)
    {
		PrintListSeparator();
        return *this;
    }

	void BeginList()
	{
		bFirstListItem = true;
	}

	void PrintListSeparator()
	{
		if (!bFirstListItem)
		{
			Buffer.Append(TEXTVIEW(", "));
		}
		bFirstListItem = false;
	}
};

static const TCHAR* GetHLSLTypeString(EMaterialValueType Type)
{
	switch (Type)
	{
		case MCT_Float1: return TEXT("MaterialFloat");
		case MCT_Float2: return TEXT("MaterialFloat2");
		case MCT_Float3: return TEXT("MaterialFloat3");
		case MCT_Float4: return TEXT("MaterialFloat4");
		case MCT_Float: return TEXT("MaterialFloat");
		case MCT_Texture2D: return TEXT("texture2D");
		case MCT_TextureCube: return TEXT("textureCube");
		case MCT_Texture2DArray: return TEXT("texture2DArray");
		case MCT_VolumeTexture: return TEXT("volumeTexture");
		case MCT_StaticBool: return TEXT("static bool");
		case MCT_Bool:  return TEXT("bool");
		case MCT_MaterialAttributes: return TEXT("FMaterialAttributes");
		case MCT_TextureExternal: return TEXT("TextureExternal");
		case MCT_TextureVirtual: return TEXT("TextureVirtual");
		case MCT_VTPageTableResult: return TEXT("VTPageTableResult");
		case MCT_ShadingModel: return TEXT("uint");
		case MCT_UInt: return TEXT("uint");
		case MCT_UInt1: return TEXT("uint");
		case MCT_UInt2: return TEXT("uint2");
		case MCT_UInt3: return TEXT("uint3");
		case MCT_UInt4: return TEXT("uint4");
		case MCT_Substrate: return TEXT("FSubstrateData");
		case MCT_TextureCollection: return TEXT("FResourceCollection");
		default: return TEXT("unknown");
	};
}

static const TCHAR* GetShadingModelParameterName(EMaterialShadingModel InModel)
{
	switch (InModel)
	{
		case MSM_Unlit: return TEXT("MATERIAL_SHADINGMODEL_UNLIT");
		case MSM_DefaultLit: return TEXT("MATERIAL_SHADINGMODEL_DEFAULT_LIT");
		case MSM_Subsurface: return TEXT("MATERIAL_SHADINGMODEL_SUBSURFACE");
		case MSM_PreintegratedSkin: return TEXT("MATERIAL_SHADINGMODEL_PREINTEGRATED_SKIN");
		case MSM_ClearCoat: return TEXT("MATERIAL_SHADINGMODEL_CLEAR_COAT");
		case MSM_SubsurfaceProfile: return TEXT("MATERIAL_SHADINGMODEL_SUBSURFACE_PROFILE");
		case MSM_TwoSidedFoliage: return TEXT("MATERIAL_SHADINGMODEL_TWOSIDED_FOLIAGE");
		case MSM_Hair: return TEXT("MATERIAL_SHADINGMODEL_HAIR");
		case MSM_Cloth: return TEXT("MATERIAL_SHADINGMODEL_CLOTH");
		case MSM_Eye: return TEXT("MATERIAL_SHADINGMODEL_EYE");
		case MSM_SingleLayerWater: return TEXT("MATERIAL_SHADINGMODEL_SINGLELAYERWATER");
		case MSM_ThinTranslucent: return TEXT("MATERIAL_SHADINGMODEL_THIN_TRANSLUCENT");
		default: UE_MIR_UNREACHABLE();
	}
}

static bool InstructionUsesPhiValue(const MIR::FInstruction* Instr, int32 EntryPointIndex)
{
	if (auto Branch = Instr->As<MIR::FBranch>())
	{
		return Branch->TrueBlock[EntryPointIndex].Instructions || Branch->FalseBlock[EntryPointIndex].Instructions;
	}
	return false;
}

static bool IsFoldable(const MIR::FInstruction* Instr, int32 EntryPointIndex)
{
	// Instructions that use Phi values cannot be folded/inlined.
	if (InstructionUsesPhiValue(Instr, EntryPointIndex))
	{
		return false;
	}

	// Don't fold instructions that become very long to improve readability
	switch (Instr->Kind)
	{
		case MIR::VK_SetMaterialOutput:
		case MIR::VK_VTPageTableRead:
		case MIR::VK_Call:
			return false;
		default:
			return true;
	}
}

static FStringView GVector4SwizzleSubset[4] = { TEXTVIEW(".x"), TEXTVIEW(".xy"), TEXTVIEW(".xyz"), {} };

struct FPrivate : FMaterialIRToHLSLTranslation
{
	// Utility for emitting HLSL code
	FHLSLPrinter Printer{}; 

	// Number of local variables generated during translation.
	int32 NumLocals{};
	
	// Mapping from instructions to their corresponding local variable index
	// Used to map instructions that have more than one use to a local identifier like "_42".
	TMap<const MIR::FInstruction*, uint32> InstrToLocalIndex{};

	// The index of the current entry point being generated.
	int32 CurrentEntryPointIndex{};

	// Current material stage being translated (e.g., vertex, pixel)
	MIR::EStage CurrentStage{};

	// Set when generating world position offset HLSL using previous frame data.
	// Affects token replacement for <PREV> in external code, and code generation for certain external inputs.
	bool bCompilingPreviousFrame = false;

	// Generated HLSL for pixel attribute evaluation
	FString PixelAttributesHLSL{};
	
	// Generated HLSL for world position offset calculation
	FString WorldPositionOffsetHLSL{};

	// Generated HLSL for previous world position offset calculation
	FString PreviousWorldPositionOffsetHLSL;

	// HLSL code for evaluating normal material attributes per stage
	FString EvaluateNormalMaterialAttributeHLSL[MIR::NumStages];
	
	// HLSL code for evaluating non-normal material attributes per stage
	FString EvaluateOtherMaterialAttributesHLSL[MIR::NumStages];

	void GeneratePixelAttributesHLSL()
	{
		for (EMaterialProperty Property : UMaterialAggregate::GetMaterialAttributesProperties())
		{
			if (Property == MP_WorldPositionOffset)
			{
				continue;
			}
			
			if (const FMaterialAggregateAttribute* PropertyAggregate = UMaterialAggregate::GetMaterialAttribute(Property))
			{
				check(PropertyAggregate->Name.IsValid());
				PixelAttributesHLSL.Appendf(TEXT(TAB "%s %s;\n"), GetHLSLTypeString(PropertyAggregate->ToMaterialValueType()), *(PropertyAggregate->Name.ToString()));
			}
		}
	}
	
	// Generates the full HLSL of the specified entry point.
	const FMaterialIRModule::FEntryPoint& GenerateEntryPoint(int32 EntryPointIndex)
	{
		Printer = {};
		Printer.Tabs = 1;
		Printer << Indentation;

		const FMaterialIRModule::FEntryPoint& EntryPoint = Module->GetEntryPoint(EntryPointIndex);

		CurrentEntryPointIndex = EntryPointIndex;
		CurrentStage = EntryPoint.Stage;

		LowerBlock(EntryPoint.RootBlock);

		return EntryPoint;
	}
	
	// Generates the HLSL that sets the outputs of the vertex stage.	
	void GenerateVertexStageHLSL()
	{
		// Generate current frame vertex stage HLSL
		GenerateEntryPoint(MIR::Stage_Vertex);
		WorldPositionOffsetHLSL = MoveTemp(Printer.Buffer);

		// Generate previous frame vertex stage HLSL
		bCompilingPreviousFrame = true;
		GenerateEntryPoint(MIR::Stage_Vertex);
		PreviousWorldPositionOffsetHLSL = MoveTemp(Printer.Buffer);
		bCompilingPreviousFrame = false;
	}

	// Generates the HLSL that sets the outputs of a non-vertex stage.
	void GenerateNonVertexStageHLSL(MIR::EStage Stage)
	{
		GenerateEntryPoint(Stage);

		if (Stage == MIR::Stage_Pixel)
		{
			Printer << TEXTVIEW("PixelMaterialInputs.Subsurface = 0") << EndOfStatement;
		}

		EvaluateOtherMaterialAttributesHLSL[CurrentStage] = MoveTemp(Printer.Buffer);
	}
	
	ENoOp LowerBlock(const MIR::FBlock& Block)
	{
		int32 OldNumLocals = NumLocals;
		for (MIR::FInstruction* Instr = Block.Instructions; Instr; Instr = Instr->GetNext(CurrentEntryPointIndex))
		{
            if (Instr->GetNumUsers(CurrentEntryPointIndex) == 1 && IsFoldable(Instr, CurrentEntryPointIndex))
			{
                continue;
            }
            
			if (Instr->GetNumUsers(CurrentEntryPointIndex) >= 1 && !Instr->As<MIR::FCall>())
			{
				// Allocate a new local index for this instruction
				uint32 LocalIndex = NumLocals++;
				
				// Remember the mapping between this instruction and its local index
                InstrToLocalIndex.Add(Instr, LocalIndex);

				// Print the local declaration "<Type> _<LocalIndex>", e.g. "float4 _3"
                Printer << LowerType(Instr->Type) << TEXTVIEW(" _") << LocalIndex;
				
				// If this instruction doesn't use a phi value, we'll immediately assign its local to its result so output the "="
				// E.g., if Instr is a branch, we will set its value inside the generate "if" {} scopes, so no need for a "=" now.
                if (!InstructionUsesPhiValue(Instr, CurrentEntryPointIndex))
				{
                    Printer << TEXTVIEW(" = ");
                }
            }

			LowerInstruction(Instr);

			if (Printer.Buffer.EndsWith(TEXTVIEW("}")))
			{
				Printer << NewLine;
			}
			else
			{
				Printer << EndOfStatement;
			}

			// Store the code needed to evaluate the normal in a separate chunk than the other material attributes
			// since this needs to be emitted before the others in the material template.
			const MIR::FSetMaterialOutput* SetMaterialOutput = MIR::As<MIR::FSetMaterialOutput>(Instr);
			if (SetMaterialOutput && SetMaterialOutput->Property == MP_Normal) 
			{
				EvaluateNormalMaterialAttributeHLSL[CurrentStage] = MoveTemp(Printer.Buffer);
				Printer.Tabs = 1;
				Printer << Indentation;
			}
        }

        NumLocals = OldNumLocals;

		return NoOp;
	}
	
	ENoOp LowerValue(const MIR::FValue* Value)
	{
		// Instruction results may be shared among other dependent instructions.
		if (const MIR::FInstruction* Instr = MIR::AsInstruction(Value))
		{ 
			// If this instruction has only one user (dependant) and is foldable, then generate the full HLSL for the instruction in place.
			if (Instr->GetNumUsers(CurrentEntryPointIndex) <= 1 && IsFoldable(Instr, CurrentEntryPointIndex))
			{
				Printer << LowerInstruction(Instr);
			}
			else
			{
				// Otherwise, this instruction has already been generated before, we emit a reference to the local that stores its result.
				Printer << TEXTVIEW("_") << InstrToLocalIndex[Instr];
			}
			return NoOp;
		}

		switch (Value->Kind)
		{
			case MIR::VK_Constant: LowerConstant(static_cast<const MIR::FConstant*>(Value)); break;
			case MIR::VK_ExternalInput: LowerExternalInput(static_cast<const MIR::FExternalInput*>(Value)); break;
			case MIR::VK_MaterialParameterCollection: LowerMaterialParameterCollection(static_cast<const MIR::FMaterialParameterCollection*>(Value)); break;
			case MIR::VK_ScreenTexture: LowerScreenTexture(static_cast<const MIR::FScreenTexture*>(Value)); break;
			case MIR::VK_ShadingModel: LowerShadingModel(static_cast<const MIR::FShadingModel*>(Value)); break;
			case MIR::VK_TextureObject: LowerTextureObject(static_cast<const MIR::FTextureObject*>(Value)); break;
			case MIR::VK_RuntimeVirtualTextureObject: LowerRuntimeVirtualTextureObject(static_cast<const MIR::FRuntimeVirtualTextureObject*>(Value)); break;
			case MIR::VK_UniformParameter: LowerUniformParameter(static_cast<const MIR::FUniformParameter*>(Value)); break;
			default: UE_MIR_UNREACHABLE();
		}

		return NoOp;
	}

	ENoOp LowerInstruction(const MIR::FInstruction* Instr)
	{
		switch (Instr->Kind)
		{
			case MIR::VK_Composite: LowerComposite(static_cast<const MIR::FComposite*>(Instr)); break;
			case MIR::VK_SetMaterialOutput: LowerSetMaterialOutput(static_cast<const MIR::FSetMaterialOutput*>(Instr)); break;
			case MIR::VK_Operator: LowerOperator(static_cast<const MIR::FOperator*>(Instr)); break;
			case MIR::VK_Branch: LowerBranch(static_cast<const MIR::FBranch*>(Instr)); break;
			case MIR::VK_Subscript: LowerSubscript(static_cast<const MIR::FSubscript*>(Instr)); break;
			case MIR::VK_Scalar: LowerScalar(static_cast<const MIR::FScalar*>(Instr)); break;
			case MIR::VK_TextureRead: LowerTextureRead(static_cast<const MIR::FTextureRead*>(Instr)); break;
			case MIR::VK_VTPageTableRead: LowerVTPageTableRead(static_cast<const MIR::FVTPageTableRead*>(Instr)); break;
			case MIR::VK_InlineHLSL: LowerInlineHLSL(static_cast<const MIR::FInlineHLSL*>(Instr)); break;
			case MIR::VK_PromoteSubstrateParameter: LowerPromoteSubstrateParameter(static_cast<const MIR::FPromoteSubstrateParameter*>(Instr)); break;
			case MIR::VK_StageSwitch: LowerStageSwitch(static_cast<const MIR::FStageSwitch*>(Instr)); break;
			case MIR::VK_HardwarePartialDerivative: LowerHardwarePartialDerivative(static_cast<const MIR::FHardwarePartialDerivative*>(Instr)); break;
			case MIR::VK_Nop: LowerNop(static_cast<const MIR::FNop*>(Instr)); break;
			case MIR::VK_Call: LowerCall(static_cast<const MIR::FCall*>(Instr)); break;
			case MIR::VK_CallParameterOutput: LowerCallOutput(static_cast<const MIR::FCallParameterOutput*>(Instr)); break;
			case MIR::VK_PreshaderParameter: LowerPreshaderParameter(static_cast<const MIR::FPreshaderParameter*>(Instr)); break;
			default: UE_MIR_UNREACHABLE();
		}
	
		return NoOp;
	}

	void LowerConstant(const MIR::FConstant* Constant)
	{
		TOptional<MIR::FPrimitive> Primitive = Constant->Type.AsPrimitive();
		check(Primitive && Primitive->IsScalar());

		switch (Primitive->ScalarKind)
		{
			case MIR::EScalarKind::Bool:
				Printer << (Constant->Boolean ? TEXTVIEW("true") : TEXTVIEW("false"));
				break;
				
			case MIR::EScalarKind::Int:
				Printer.Buffer.Appendf(TEXT("%") PRId64, Constant->Integer);
				break;
			
			case MIR::EScalarKind::Float:
				Printer << Constant->Float;
				break;
			
			case MIR::EScalarKind::Double:
			{
				FLargeWorldRenderScalar ConstantLWC(Constant->Double);
				Printer << TEXTVIEW("MakeLWCScalar(") << ConstantLWC.GetTile() << TEXTVIEW(", ") << ConstantLWC.GetOffset() << TEXTVIEW(")");
				break;
			}
		}
	}

	void LowerExternalInput(const MIR::FExternalInput* ExternalInput)
	{
		int32 ExternalInputIndex = (int32)ExternalInput->Id;
		if (MIR::IsExternalInputTexCoord(ExternalInput->Id))
		{
			int32 Index = ExternalInputIndex - (int32)MIR::EExternalInput::TexCoord0;
			Printer << TEXTVIEW("Parameters.TexCoords[") << Index << TEXTVIEW("]");
		}
		else if (MIR::IsExternalInputTexCoordDdx(ExternalInput->Id))
		{
			int32 Index = ExternalInputIndex - (int32)MIR::EExternalInput::TexCoord0_Ddx;
			Printer << TEXTVIEW("Parameters.TexCoords_DDX[") << Index << TEXTVIEW("]");
		}
		else if (MIR::IsExternalInputTexCoordDdy(ExternalInput->Id))
		{
			int32 Index = ExternalInputIndex - (int32)MIR::EExternalInput::TexCoord0_Ddy;
			Printer << TEXTVIEW("Parameters.TexCoords_DDY[") << Index << TEXTVIEW("]");
		}
		else if (ExternalInput->Id >= MIR::EExternalInput::WorldPosition_Absolute && ExternalInput->Id <= MIR::EExternalInput::WorldPosition_CameraRelativeNoOffsets)
		{
			// Various function permutations exist to fetch world position.  "Prev" permutations are only available in the vertex shader, while
			// "MoMaterialOffsets" permutations are only available in the pixel shader, so we need to cobble together the permutation string
			// factoring in those limitations.  Logic adapted from FHLSLMaterialTranslator::WorldPosition.
			// 
			// Format:  Get[Prev][Translated]WorldPosition[_NoMaterialOffsets](Parameters)
			//
			Printer << TEXTVIEW("Get");

			// If compiling for the previous frame
			if (bCompilingPreviousFrame)
			{
				Printer << TEXTVIEW("Prev");
			}

			if (ExternalInput->Id == MIR::EExternalInput::WorldPosition_CameraRelative || ExternalInput->Id == MIR::EExternalInput::WorldPosition_CameraRelativeNoOffsets)
			{
				Printer << TEXTVIEW("Translated");
			}

			Printer << TEXTVIEW("WorldPosition");

			if (CurrentStage == MIR::EStage::Stage_Pixel && (ExternalInput->Id == MIR::EExternalInput::WorldPosition_AbsoluteNoOffsets || ExternalInput->Id == MIR::EExternalInput::WorldPosition_CameraRelativeNoOffsets))
			{
				Printer << TEXTVIEW("_NoMaterialOffsets");
			}

			Printer << TEXTVIEW("(Parameters)");
		}
		else if (ExternalInput->Id >= MIR::EExternalInput::WorldPosition_Absolute_Ddx && ExternalInput->Id <= MIR::EExternalInput::WorldPosition_CameraRelativeNoOffsets_Ddx)
		{
			// Expression emitter assumes these to be LWC for analytic derivative evaluation, so we promote them on load
			Printer << TEXTVIEW("WSPromote(Parameters.WorldPosition_DDX)");
		}
		else if (ExternalInput->Id >= MIR::EExternalInput::WorldPosition_Absolute_Ddy && ExternalInput->Id <= MIR::EExternalInput::WorldPosition_CameraRelativeNoOffsets_Ddy)
		{
			// Expression emitter assumes these to be LWC for analytic derivative evaluation, so we promote them on load
			Printer << TEXTVIEW("WSPromote(Parameters.WorldPosition_DDY)");
		}
		else if (ExternalInput->Id >= MIR::EExternalInput::LocalPosition_Instance && ExternalInput->Id <= MIR::EExternalInput::LocalPosition_PrimitiveNoOffsets)
		{
			// Various function permutations exist to fetch local position.  "Prev" permutations are only available in the vertex shader, while
			// "MoMaterialOffsets" permutations are only available in the pixel shader, so we need to cobble together the permutation string
			// factoring in those limitations.  Logic adapted from FHLSLMaterialTranslator::LocalPosition (and similar to WorldPosition above).
			// 
			// Format:  Get[Prev]Position[Instance|Primitive]Space[_NoMaterialOffsets](Parameters)
			//
			Printer << TEXTVIEW("Get");

			// If compiling for the previous frame
			if (bCompilingPreviousFrame)
			{
				Printer << TEXTVIEW("Prev");
			}

			Printer << TEXTVIEW("Position");
			Printer << (ExternalInput->Id <= MIR::EExternalInput::LocalPosition_InstanceNoOffsets ? TEXTVIEW("Instance") : TEXTVIEW("Primitive"));
			Printer << TEXTVIEW("Space");

			if (CurrentStage == MIR::EStage::Stage_Pixel && (ExternalInput->Id == MIR::EExternalInput::LocalPosition_InstanceNoOffsets || ExternalInput->Id == MIR::EExternalInput::LocalPosition_PrimitiveNoOffsets))
			{
				Printer << TEXTVIEW("_NoMaterialOffsets");
			}

			Printer << TEXTVIEW("(Parameters)");
		}
		else if (ExternalInput->Id >= MIR::EExternalInput::LocalPosition_Instance_Ddx && ExternalInput->Id <= MIR::EExternalInput::LocalPosition_PrimitiveNoOffsets_Ddx)
		{
			// Local position derivatives reuse the world position derivatives, transforming them to local space
			if (ExternalInput->Id <= MIR::EExternalInput::LocalPosition_InstanceNoOffsets_Ddx)
			{
				Printer << TEXTVIEW("mul(Parameters.WorldPosition_DDX, DFToFloat3x3(GetWorldToInstanceDF(Parameters)))");
			}
			else
			{
				Printer << (bCompilingPreviousFrame ?
					TEXTVIEW("mul(Parameters.WorldPosition_DDX, DFToFloat3x3(GetPrevWorldToLocalDF(Parameters)))") :
					TEXTVIEW("mul(Parameters.WorldPosition_DDX, DFToFloat3x3(GetWorldToLocalDF(Parameters)))"));
			}
		}
		else if (ExternalInput->Id >= MIR::EExternalInput::LocalPosition_Instance_Ddy && ExternalInput->Id <= MIR::EExternalInput::LocalPosition_PrimitiveNoOffsets_Ddy)
		{
			// Local position derivatives reuse the world position derivatives, transforming them to local space
			if (ExternalInput->Id <= MIR::EExternalInput::LocalPosition_InstanceNoOffsets_Ddy)
			{
				Printer << TEXTVIEW("mul(Parameters.WorldPosition_DDY, DFToFloat3x3(GetWorldToInstanceDF(Parameters)))");
			}
			else
			{
				Printer << (bCompilingPreviousFrame ?
					TEXTVIEW("mul(Parameters.WorldPosition_DDY, DFToFloat3x3(GetPrevWorldToLocalDF(Parameters)))") :
					TEXTVIEW("mul(Parameters.WorldPosition_DDY, DFToFloat3x3(GetWorldToLocalDF(Parameters)))"));
			}
		}
		else if (ExternalInput->Id == MIR::EExternalInput::ActorPosition_Absolute)
		{
			Printer << (bCompilingPreviousFrame ? TEXTVIEW("GetPreviousActorWorldPosition(Parameters)") : TEXTVIEW("GetActorWorldPosition(Parameters)"));
		}
		else if (ExternalInput->Id == MIR::EExternalInput::ActorPosition_CameraRelative)
		{
			Printer << (bCompilingPreviousFrame ? TEXTVIEW("GetPreviousActorTranslatedWorldPosition(Parameters)") : TEXTVIEW("GetActorTranslatedWorldPosition(Parameters)"));
		}
		else if (ExternalInput->Id == MIR::EExternalInput::DynamicParticleParameterIndex)
		{
			Printer << ExternalInput->UserData;
		}
		else if (ExternalInput->Id == MIR::EExternalInput::CompilingPreviousFrame)
		{
			Printer << (bCompilingPreviousFrame ? TEXTVIEW("true") : TEXTVIEW("false"));
		}
		else
		{
			FStringView Code;
			switch (ExternalInput->Id)
			{
				case MIR::EExternalInput::ObjectPosition_Absolute: Code = TEXTVIEW("GetObjectWorldPosition(Parameters)"); break;
				case MIR::EExternalInput::ObjectPosition_CameraRelative: Code = TEXTVIEW("GetObjectTranslatedWorldPosition(Parameters)"); break;
				case MIR::EExternalInput::ViewMaterialTextureMipBias: Code = TEXTVIEW("View.MaterialTextureMipBias"); break;
				case MIR::EExternalInput::ViewMaterialTextureDerivativeMultiply: Code = TEXTVIEW("View.MaterialTextureDerivativeMultiply"); break;
				case MIR::EExternalInput::GlobalDistanceField: Code = TEXTVIEW(""); break;			// Used as flag for value analyzer, doesn't generate code
				default: UE_MIR_UNREACHABLE();
			}
			Printer << Code;
		}
	}

	void LowerMaterialParameterCollection(const MIR::FMaterialParameterCollection* MaterialParameterCollection)
	{
		Printer << MaterialParameterCollection->Analysis_CollectionIndex;
	}

	void LowerInlineHLSLWithArgumentsInternal(FStringView Code, const TConstArrayView<MIR::FValue*>& InArguments)
	{
		// Substitute argument tokens with instruction arguments
		int32 ArgumentTokenStart = 0, ArgumentTokenEnd = 0;

		// Prints the pending substring of the code and moves the range forward.
		auto FlushCodeSubstring = [this, &ArgumentTokenEnd, &Code](int32 EndIndex) -> void
		{
			if (EndIndex > ArgumentTokenEnd)
			{
				Printer << Code.Mid(ArgumentTokenEnd, EndIndex - ArgumentTokenEnd);
				ArgumentTokenEnd = EndIndex;
			}
		};

		auto SubstituteNextArgument = [this, &ArgumentTokenStart, &ArgumentTokenEnd, &Code, &InArguments]() -> void
		{
			// Scan digits for argument index
			int32 ArgumentIndexValue = 0;
			int32 NumDigits = 0;

			while (ArgumentTokenStart + NumDigits < Code.Len())
			{
				TCHAR CodeCharacter = Code[ArgumentTokenStart + NumDigits];
				if (!FChar::IsDigit(CodeCharacter))
				{
					break;
				}
				ArgumentIndexValue *= 10;
				ArgumentIndexValue += (CodeCharacter - TEXT('0'));
				++NumDigits;
			}

			checkf(NumDigits > 0, TEXT("Failed to scan integer in inline-HLSL after token '$':\n\"%.*s\""), Code.Len(), Code.GetData());

			checkf(
				ArgumentIndexValue < InArguments.Num(), TEXT("Failed to substitute token $%d in inline-HLSL with given number of arguments (%d):\n\"%.*s\""),
				ArgumentIndexValue, InArguments.Num(), Code.Len(), Code.GetData()
			);

			LowerValue(InArguments[ArgumentIndexValue]);

			ArgumentTokenEnd = ArgumentTokenStart + NumDigits;
		};

		auto MatchCharacter = [&Code](int32& Position, TCHAR InCharacter) -> bool
		{
			if (Position < Code.Len() && Code[Position] == InCharacter)
			{
				++Position;
				return true;
			}
			return false;
		};

		// Find all argument token characters '$'. For example "MyFunction($1, $0.xxxw)" can be subsituted with "MyFunction(MySecondArgument, MyFirstArgument.xxxw)"
		while ((ArgumentTokenStart = Code.Find(TEXT("$"), ArgumentTokenEnd)) != INDEX_NONE)
		{
			FlushCodeSubstring(ArgumentTokenStart);

			++ArgumentTokenStart;
			if (MatchCharacter(ArgumentTokenStart, TEXT('{')))
			{
				SubstituteNextArgument();
				const bool bMatchedArgumentToken = MatchCharacter(ArgumentTokenEnd, TEXT('}'));
				checkf(bMatchedArgumentToken, TEXT("Failed to match argument token in inline-HLSL with syntax '${N}':\n\"%.*s\""), Code.Len(), Code.GetData());
			}
			else
			{
				SubstituteNextArgument();
			}
		}

		FlushCodeSubstring(Code.Len());
	}

	
	void LowerInlineHLSL(const MIR::FInlineHLSL* ExternalCode)
	{
		FStringView FinalCode;
		if (ExternalCode->HasFlags(MIR::EValueFlags::HasDynamicHLSLCode))
		{
			FinalCode = ExternalCode->Code;
		}
		else if (ExternalCode->HasFlags(MIR::EValueFlags::DerivativeDDX))
		{
			FinalCode = ExternalCode->ExternalCodeDeclaration->DefinitionDDX;
		}
		else if (ExternalCode->HasFlags(MIR::EValueFlags::DerivativeDDY))
		{
			FinalCode = ExternalCode->ExternalCodeDeclaration->DefinitionDDY;
		}
		else
		{
			FinalCode = ExternalCode->ExternalCodeDeclaration->Definition;
		}

		// Substitute placeholder tokens now unless disabled for custom nodes, if necessary.
		FString SubstitutedCode;
		if (ExternalCode->HasFlags(MIR::EValueFlags::SubstituteTags))
		{
			SubstitutedCode = FString{ FinalCode };
			SubstitutedCode.ReplaceInline(TEXT("<PREV>"), bCompilingPreviousFrame ? TEXT("Prev") : TEXT(""));
			SubstitutedCode.ReplaceInline(TEXT("<PREVIOUS>"), bCompilingPreviousFrame ? TEXT("Previous") : TEXT(""));
			SubstitutedCode.ReplaceInline(TEXT("<PREVFRAME>"), bCompilingPreviousFrame ? TEXT("PrevFrame") : TEXT(""));

			FinalCode = SubstitutedCode;
		}

		// Print the final HLSL code.
		if (ExternalCode->NumArguments > 0)
		{
			check(ExternalCode->Arguments);
			LowerInlineHLSLWithArgumentsInternal(FinalCode, TConstArrayView<MIR::FValue*>(ExternalCode->Arguments, ExternalCode->NumArguments));
		}
		else
		{
			Printer << FinalCode;
		}
	}

	void LowerScreenTexture(const MIR::FScreenTexture* ScreenTexture)
	{
		// Types besides the following aren't directly printed in HLSL, and don't need to do anything here
		switch (ScreenTexture->TextureKind)
		{
		case MIR::EScreenTexture::SceneTexture:
			Printer << UE::MaterialTranslatorUtils::SceneTextureIdToHLSLString(ScreenTexture->Id);
			break;
		case MIR::EScreenTexture::UserSceneTexture:
			Printer << UE::MaterialTranslatorUtils::SceneTextureIdToHLSLString((ESceneTextureId)Module->GetCompilationOutput().FindUserSceneTexture(ScreenTexture->UserSceneTexture));
			break;
		case MIR::EScreenTexture::DBufferTexture:
			Printer << (int32)ScreenTexture->DBufferId;
			break;
		}
	}

	void LowerShadingModel(const MIR::FShadingModel* ShadingModel)
	{
		Printer << (int32)ShadingModel->Id;
	}

	void LowerTextureObject(const MIR::FTextureObject* TextureObject)
	{
		LowerTextureReference(TextureObject->Texture->GetMaterialType(), TextureObject->Analysis_UniformParameterIndex);
	}

	void LowerRuntimeVirtualTextureObject(const MIR::FRuntimeVirtualTextureObject* RVTextureObject)
	{
		LowerTextureReference(MCT_TextureVirtual, RVTextureObject->Analysis_UniformParameterIndex);
	}

	void LowerUniformParameter(const MIR::FUniformParameter* UniformParameter)
	{
		if (UniformParameter->Type.IsTexture() || UniformParameter->Type.IsRuntimeVirtualTexture())
		{
			UObject* TextureObject = GetTextureFromUniformParameter(UniformParameter);
			check(TextureObject);
			LowerTextureReference(MIR::Internal::GetTextureMaterialValueType(TextureObject), UniformParameter->Analysis_UniformParameterIndex);
		}
		else
		{
			LowerPrimitiveUniformParameter(UniformParameter);
		}
	}

	struct FSubstrateLegacyArgument
	{
		MIR::FType ParameterType;
		FStringView ParameterName;
		EMaterialProperty MaterialProperty;
		FStringView DefaultInlineHLSL;
	};

	static TConstArrayView<FSubstrateLegacyArgument> GetSubstrateLegacyConversionArguments()
	{
		static const FSubstrateLegacyArgument LegacyArguments[] =
		{
			{ MIR::FType::MakeFloatVector(3), TEXTVIEW("BaseColor"), MP_BaseColor, {} },
			{ MIR::FType::MakeFloatScalar(),  TEXTVIEW("Specular"), MP_SpecularColor, TEXTVIEW("0.5f") },
			{ MIR::FType::MakeFloatScalar(),  TEXTVIEW("Metallic"), MP_Metallic, {} },
			{ MIR::FType::MakeFloatScalar(),  TEXTVIEW("Roughness"), MP_Roughness, {} },
			{ MIR::FType::MakeFloatScalar(),  TEXTVIEW("Anisotropy"), MP_Anisotropy, {} },
			{ MIR::FType::MakeFloatVector(3), TEXTVIEW("SubSurfaceColor"), MP_SubsurfaceColor, TEXTVIEW("(float3)1") },
			{ MIR::FType::MakeFloatScalar(),  TEXTVIEW("SubSurfaceProfileId"), MP_MAX, TEXTVIEW("0") },
			{ MIR::FType::MakeFloatScalar(),  TEXTVIEW("ClearCoat"), MP_MAX, TEXTVIEW("1.0f") },
			{ MIR::FType::MakeFloatScalar(),  TEXTVIEW("ClearCoatRoughness"), MP_MAX, TEXTVIEW("0.1f") },
			{ MIR::FType::MakeFloatVector(3), TEXTVIEW("Emissive"), MP_EmissiveColor, {} },
			{ MIR::FType::MakeFloatScalar(),  TEXTVIEW("Opacity"), MP_Opacity, {} },
			{ MIR::FType::MakeFloatVector(3), TEXTVIEW("ThinTranslucentTransmittanceColor"), MP_MAX, TEXTVIEW("(float3)0.5f") },
			{ MIR::FType::MakeFloatScalar(),  TEXTVIEW("ThinTranslucentSurfaceCoverage"), MP_MAX, TEXTVIEW("1.0f") },
			{ MIR::FType::MakeFloatVector(3), TEXTVIEW("WaterScatteringCoefficients"), MP_MAX, {} },
			{ MIR::FType::MakeFloatVector(3), TEXTVIEW("WaterAbsorptionCoefficients"), MP_MAX, {} },
			{ MIR::FType::MakeFloatScalar(),  TEXTVIEW("WaterPhaseG"), MP_MAX, {} },
			{ MIR::FType::MakeFloatVector(3), TEXTVIEW("ColorScaleBehindWater"), MP_MAX, TEXTVIEW("(float3)1.0f") },
			{ MIR::FType::MakeIntScalar(),    TEXTVIEW("ShadingModel"), MP_MAX, TEXTVIEW("1") },
			{ MIR::FType::MakeFloatVector(3), TEXTVIEW("RawNormal"), MP_Normal, TEXTVIEW("Parameters.TangentToWorld[2]") },
			{ MIR::FType::MakeFloatVector(3), TEXTVIEW("RawTangent"), MP_Tangent, TEXTVIEW("Parameters.TangentToWorld[0]") },
			{ MIR::FType::MakeFloatVector(3), TEXTVIEW("RawClearCoatNormal"), MP_Normal, TEXTVIEW("Parameters.TangentToWorld[2]") },
			{ MIR::FType::MakeFloatVector(3), TEXTVIEW("RawCustomTangent"), MP_Tangent, TEXTVIEW("Parameters.TangentToWorld[0]") },
			{ MIR::FType::MakeIntScalar(),    TEXTVIEW("SharedLocalBasisIndex"), MP_MAX, {} },
			{ MIR::FType::MakeIntScalar(),    TEXTVIEW("ClearCoatBottomNormal_SharedLocalBasisIndex"), MP_MAX, {} },
			{ MIR::FType::MakeIntScalar(),    TEXTVIEW("SharedLocalBasisTypes"), MP_MAX, TEXTVIEW("Parameters.SharedLocalBases.Types") },
		};
		return LegacyArguments;
	}

	static TConstArrayView<FSubstrateLegacyArgument> GetSubstrateUnlitArguments()
	{
		static const FSubstrateLegacyArgument LegacyArguments[] =
		{
			{ MIR::FType::MakeFloatVector(3), TEXTVIEW("Emissive"), MP_EmissiveColor, {} },
			{ MIR::FType::MakeFloatScalar(), TEXTVIEW("TransmittanceColor"), MP_MAX, TEXTVIEW("(float3)0.0f") },
			{ MIR::FType::MakeFloatVector(3), TEXTVIEW("RawNormal"), MP_Normal, TEXTVIEW("Parameters.TangentToWorld[2]") },
		};
		return LegacyArguments;
	}

	void LowerPromoteSubstrateParameter(const MIR::FPromoteSubstrateParameter* SubstrateParameter)
	{
		Printer << TEXTVIEW("Parameters.SubstrateTree.PromoteParameterBlendedBSDFToOperator") << BeginArgs;
		Printer.Tabs++;
		Printer << NewLine << (SubstrateParameter->bIsUnlit ? TEXTVIEW("GetSubstrateUnlitBSDF") : TEXTVIEW("SubstrateConvertLegacyMaterialStatic")) << BeginArgs;
		Printer.Tabs++;
		Printer << ListSeparator << NewLine << TEXTVIEW("/*PixelFootprint:*/ Parameters.SubstratePixelFootprint");

		TConstArrayView<FSubstrateLegacyArgument> Arguments = SubstrateParameter->bIsUnlit ? GetSubstrateUnlitArguments() : GetSubstrateLegacyConversionArguments();

		for (const FSubstrateLegacyArgument& Argument : Arguments)
		{
			Printer << ListSeparator << NewLine << TEXTVIEW("/*") << Argument.ParameterName << TEXTVIEW("*/ ");

			switch (Argument.MaterialProperty)
			{
				case MP_Normal: LowerValue(SubstrateParameter->WorldSpaceTangentsAndNormals[0]); break;
				case MP_Tangent: LowerValue(SubstrateParameter->WorldSpaceTangentsAndNormals[1]); break;
				
				default:
					if (Argument.MaterialProperty != MP_MAX && SubstrateParameter->PropertyArgs[Argument.MaterialProperty])
					{
						LowerValue(SubstrateParameter->PropertyArgs[Argument.MaterialProperty]);
					}
					else if (!Argument.DefaultInlineHLSL.IsEmpty())
					{
						Printer << Argument.DefaultInlineHLSL;
					}
					else if (TOptional<MIR::FPrimitive> PrimitiveType = Argument.ParameterType.AsPrimitive())
					{
						if (PrimitiveType->IsScalar())
						{
							Printer << TEXTVIEW("0.0f");
						}
						else
						{
							LowerType(Argument.ParameterType);
							Printer << BeginArgs;
							for (int32 ComponentIndex = 0; ComponentIndex < PrimitiveType->NumComponents(); ++ComponentIndex)
							{
								Printer << ListSeparator << TEXTVIEW("0.0f");
							}
							Printer << EndArgs;
						}
					}
					else
					{
						UE_MIR_UNREACHABLE();
					}
					break;
			}
		}

		Printer.Tabs--;
		Printer << NewLine << EndArgs << ListSeparator;
		Printer << NewLine << TEXTVIEW("0, 0, 0, 1");
		Printer.Tabs--;
		Printer << NewLine << EndArgs;
	}

	void EmitPreshaderBufferReadDoubleVector(const MIR::FPrimitive& PrimitiveType, uint32 GlobalComponentOffset)
	{
		const int32 NumComponents = PrimitiveType.NumComponents();

		// Index of the float4 slot.  LWC parameters always have four elements, so there won't be an offset.
		const uint32 BufferSlotIndex = GlobalComponentOffset / 4;

		Printer << TEXTVIEW("DFToWS(MakeDFVector") << NumComponents;
		Printer << TEXTVIEW("(Material.PreshaderBuffer[") << BufferSlotIndex << TEXTVIEW("]") << GVector4SwizzleSubset[NumComponents - 1];
		Printer << TEXTVIEW(", Material.PreshaderBuffer[") << BufferSlotIndex + 1 << TEXTVIEW("]") << GVector4SwizzleSubset[NumComponents - 1] << TEXTVIEW("))");
	}

	void EmitPreshaderBufferReadFloatVector(const MIR::FPrimitive& PrimitiveType, uint32 GlobalComponentOffset)
	{
		const int32 NumComponents = PrimitiveType.NumComponents();

		// Index of the float4 slot
		uint32 BufferSlotIndex = GlobalComponentOffset / 4;

		// Starting component of the float4 slot
		uint32 BufferSlotOffset = GlobalComponentOffset % 4;

		if (PrimitiveType.IsInteger())
		{
			Printer << TEXTVIEW("asint(");
		}

		Printer << TEXTVIEW("Material.PreshaderBuffer[") << BufferSlotIndex << TEXTVIEW("]");

		if (NumComponents < 4)
		{
			Printer << TEXTVIEW(".");

			const TCHAR* Components = TEXT("xyzw");
			for (int32 i = 0; i < NumComponents; ++i)
			{
				check(BufferSlotOffset + i < 4);
				Printer.Buffer.AppendChar(Components[BufferSlotOffset + i]);
			}
		}

		if (PrimitiveType.IsInteger())
		{
			Printer << TEXTVIEW(")"); // close the "asint(" bracket
		}
	}

	void EmitPreshaderBufferRead(const MIR::FPrimitive& PrimitiveType, uint32 GlobalComponentOffset)
	{
		check(PrimitiveType.IsScalar() || PrimitiveType.IsRowVector()); // no matrices yet

		// LWC parameters are handled differently, they have their own dedicated function.
		if (PrimitiveType.IsDouble())
		{
			EmitPreshaderBufferReadDoubleVector(PrimitiveType, GlobalComponentOffset);
		}
		else
		{
			EmitPreshaderBufferReadFloatVector(PrimitiveType, GlobalComponentOffset);
		}
	}

	ENoOp LowerPrimitiveUniformParameter(const MIR::FUniformParameter* UniformParameter)
	{
		// Get the global float4 component index (e.g. if this is 13, it refer to PreshaderBuffer[3].y)
		const FUniformExpressionSet& UniformExpressionSet = Module->GetCompilationOutput().UniformExpressionSet;
		const uint32 GlobalComponentOffset = UniformExpressionSet.GetNumericParameterEvaluationOffset(UniformParameter->Analysis_UniformParameterIndex);

		EmitPreshaderBufferRead(UniformParameter->Type.GetPrimitive(), GlobalComponentOffset);

		return NoOp;
	}

	ENoOp LowerPreshaderParameter(const MIR::FPreshaderParameter* PreshaderParameter)
	{
		// Get the global float4 component index (e.g. if this is 13, it refer to PreshaderBuffer[3].y)
		const uint32 GlobalComponentOffset = PreshaderParameter->Analysis_PreshaderOffset;

		EmitPreshaderBufferRead(PreshaderParameter->Type.GetPrimitive(), GlobalComponentOffset);

		return NoOp;
	}

	// Variation of LowerPrimitiveUniformParameter specific to LWC, to support fetching a subset of an LWC vector.
	ENoOp LowerPrimitiveUniformParameterLWC(const MIR::FUniformParameter* UniformParameter, int32 NumComponents)
	{
		check(NumComponents >= 2 && NumComponents <= 4);
		check(UniformParameter->Type.IsDouble());

		const FUniformExpressionSet& UniformExpressionSet = Module->GetCompilationOutput().UniformExpressionSet;

		// Get the global float4 component index (e.g. if this is 13, it refer to PreshaderBuffer[3].y)
		const uint32 GlobalComponentOffset = UniformExpressionSet.GetNumericParameterEvaluationOffset(UniformParameter->Analysis_UniformParameterIndex);

		EmitPreshaderBufferReadDoubleVector(UniformParameter->Type.GetPrimitive(), GlobalComponentOffset);
	
		return NoOp;
	}

	bool HasMatchingScalarComponentCastChain(MIR::FValue* FirstComponent, MIR::FValue* CurrentComponent, int32 Index, int32 NumComponents)
	{
		const MIR::FSubscript* FirstComponentAsSubscript = FirstComponent->As<MIR::FSubscript>();
		const MIR::FSubscript* CurrentComponentAsSubscript = CurrentComponent->As<MIR::FSubscript>();
		if (FirstComponentAsSubscript && CurrentComponentAsSubscript)
		{
			if (FirstComponentAsSubscript->Arg == CurrentComponentAsSubscript->Arg // Both subscripts have the same argument
				&& FirstComponentAsSubscript->Arg->Type.IsVector()
				&& FirstComponentAsSubscript->Index == 0 && CurrentComponentAsSubscript->Index == Index)
			{
				MIR::FPrimitive ArgPrimitiveType = FirstComponentAsSubscript->Arg->Type.GetPrimitive();

				// Exact match?
				if (ArgPrimitiveType.NumColumns == NumComponents)
				{
					return true;
				}

				// Subset of components? See if we can generate a swizzled vector.
				if (ArgPrimitiveType.NumColumns >= NumComponents)
				{
					// Allow swizzle for non-LWC values or uniform parameters (the latter having a special case for LWC)
					return !ArgPrimitiveType.IsDouble() || FirstComponentAsSubscript->Arg->As<MIR::FUniformParameter>();
				}
			}
		}
		else
		{
			const MIR::FScalar* FirstComponentAsScalar = FirstComponent->As<MIR::FScalar>();
			const MIR::FScalar* CurrentComponentAsScalar = CurrentComponent->As<MIR::FScalar>();
			if (FirstComponentAsScalar && CurrentComponentAsScalar && FirstComponentAsScalar->Type == CurrentComponentAsScalar->Type)
			{
				return HasMatchingScalarComponentCastChain(FirstComponentAsScalar->Arg, CurrentComponentAsScalar->Arg, Index, NumComponents);
			}
		}
		return false;
	}

	ENoOp LowerVectorCastChain(MIR::FValue* FirstComponent, int32 NumComponents)
	{
		if (const MIR::FScalar* FirstComponentAsScalar = FirstComponent->As<MIR::FScalar>())
		{
			MIR::FValue* Arg = FirstComponentAsScalar->Arg;
			MIR::FPrimitive PrimitiveType = FirstComponent->Type.GetPrimitive();
			MIR::FPrimitive ArgPrimitiveType = Arg->Type.GetPrimitive();

			if (ArgPrimitiveType.IsDouble())
			{
				// Cast from LWC
				if (PrimitiveType.IsBoolean())
				{
					// Cast to bool requires a comparison with zero, outside the WSDemote
					Printer << TEXTVIEW("(WSDemote(") << LowerVectorCastChain(Arg, NumComponents) << TEXTVIEW(") != 0)");
				}
				else if (!PrimitiveType.IsFloat())
				{
					// Cast to non-float (integer) requires a cast to the type, outside the WSDemote
					MIR::FType VectorType = MIR::FType::MakeVector(PrimitiveType.ScalarKind, NumComponents);
					Printer << LowerType(VectorType) << TEXTVIEW("(WSDemote(") << LowerVectorCastChain(Arg, NumComponents) << TEXTVIEW("))");
				}
				else
				{
					// Cast to float
					Printer << TEXTVIEW("WSDemote(") << LowerVectorCastChain(Arg, NumComponents) << TEXTVIEW(")");
				}
			}
			else if (PrimitiveType.IsDouble())
			{
				// Cast to LWC
				if (ArgPrimitiveType.IsBoolean())
				{
					// Cast from bool requires a select between 1.0f and 0.0f, inside the WSPromote
					Printer << TEXTVIEW("WSPromote(select(") << LowerVectorCastChain(Arg, NumComponents) << TEXTVIEW(", (float") << NumComponents << TEXTVIEW(")1.0f, (float") << NumComponents << TEXTVIEW(")0.0f))");
				}
				else if (!ArgPrimitiveType.IsFloat())
				{
					// Cast from non-float (integer) requires a cast to float, inside the WSPromote
					Printer << TEXTVIEW("WSPromote(float") << NumComponents << TEXTVIEW("(") << LowerVectorCastChain(Arg, NumComponents) << TEXTVIEW("))");
				}
				else
				{
					// Cast from float
					Printer << TEXTVIEW("WSPromote(") << LowerVectorCastChain(Arg, NumComponents) << TEXTVIEW(")");
				}
			}
			else
			{
				// Cast between intrinsic types
				if (PrimitiveType.IsBoolean())
				{
					// Cast to bool requires a comparison with zero
					Printer << TEXTVIEW("(") << LowerVectorCastChain(Arg, NumComponents) << TEXTVIEW(" != 0)");
				}
				else if (ArgPrimitiveType.IsBoolean())
				{
					// Cast from bool requires a select between 1 and 0
					MIR::FType VectorType = MIR::FType::MakeVector(PrimitiveType.ScalarKind, NumComponents);
					Printer << TEXTVIEW("select()") << LowerVectorCastChain(Arg, NumComponents) << TEXTVIEW(", (") << LowerType(VectorType) << TEXTVIEW(")1, (") << LowerType(VectorType) << TEXTVIEW(")0)");
				}
				else
				{
					// Cast between arithmetic types
					MIR::FType VectorType = MIR::FType::MakeVector(PrimitiveType.ScalarKind, NumComponents);
					Printer << LowerType(VectorType) << TEXTVIEW("(") << LowerVectorCastChain(Arg, NumComponents) << TEXTVIEW(")");
				}
			}
		}
		else if (auto FirstComponentAsSubscript = FirstComponent->As<MIR::FSubscript>())
		{
			// Finally reached the inner subscript, print its vector argument
			const MIR::FValue* Arg = FirstComponentAsSubscript->Arg;
			if (Arg->Type.IsVector() && Arg->Type.IsDouble() && Arg->As<MIR::FUniformParameter>())
			{
				// LWC types support special case swizzling logic for uniform parameters on initial fetch. See HasMatchingScalarComponentCastChain above.
				LowerPrimitiveUniformParameterLWC(static_cast<const MIR::FUniformParameter*>(Arg), NumComponents);
			}
			else
			{
				Printer << LowerValue(Arg);
				if (Arg->Type.GetPrimitive().NumColumns > NumComponents)
				{
					Printer << GVector4SwizzleSubset[NumComponents - 1];
				}
			}
		}
		else
		{
			UE_MIR_UNREACHABLE();
		}
		return NoOp;
	}

	// Check if we can tidy up casts from non-LWC scalar to LWC vectors (assuming we already know all components are the same and LWC)
	static bool IsScalarToLWCVectorCast(const MIR::FValue* FirstComponent)
	{
		if (const MIR::FScalar* AsScalar = FirstComponent->As<MIR::FScalar>())
		{
			// Check if this is a cast from an arithmetic non-LWC scalar type. If so, we can cast the scalar to a float vector, and then to LWC.
			return !AsScalar->Arg->Type.IsBoolean() && !AsScalar->Arg->Type.IsDouble();
		}
		else if (const MIR::FConstant* AsConstant = FirstComponent->As<MIR::FConstant>())
		{
			// Check if this is a cast from a constant representable exactly as a non-LWC float
			return AsConstant->Type.IsDouble() && FLargeWorldRenderScalar(AsConstant->Double).GetTile() == 0.0f;
		}
		return false;
	}

	void LowerComposite(const MIR::FComposite* Composite) 
	{
		TConstArrayView<MIR::FValue*> Components = Composite->GetComponents();
		MIR::FPrimitive PrimitiveType = Composite->Type.GetPrimitive();
		check(!PrimitiveType.IsScalar());
		
		// In order to generate smaller and tidier HLSL, first check whether all components
		// of this dimensional are actually the same. If so, we can simply emit the 
		// component and cast it to the type.  LWC doesn't support casting, and always needs
		// to call a function to convert types.
		bool bSameComponents = true;

		// Track if all components are part of a constant vector
		bool bConstantVector = Components[0]->As<MIR::FConstant>() && PrimitiveType.IsRowVector();
		
		// We can also generate tidier HLSL for cases where casts are done for whole vectors.  This is a frequent case for LWC, where casts happen
		// in both directions (LWC to float and back), due to operations automatically casting their inputs or outputs.  For example, BO_Sub
		// automatically downcasts LWC to float, but if you feed that into a BO_Add that has LWC for its other input, it will immediately cast it
		// back to LWC -- two consecutive casts.  These then get expanded into individual scalar casts, which we would like to do as whole vector
		// casts for readability.  We only need to consider this special case if the first component is a Scalar, and the type is a Vector.
		bool bWholeVectorCast = PrimitiveType.IsRowVector() && (Components[0]->As<MIR::FScalar>() || Components[0]->As<MIR::FSubscript>());

		for (int32 i = 1; i < Components.Num(); ++i)
		{
			bSameComponents &= (Components[i] == Components[0]);
			bConstantVector &= Components[i]->As<MIR::FConstant>() != nullptr;
			bWholeVectorCast = bWholeVectorCast && HasMatchingScalarComponentCastChain(Components[0], Components[i], i, Components.Num());
		}
		
		if (bSameComponents && !PrimitiveType.IsDouble())
		{
			Printer << TEXTVIEW("(") << LowerType(Composite->Type) << TEXTVIEW(")") << LowerValue(Components[0]);
		}
		else if (bSameComponents && PrimitiveType.IsRowVector() && IsScalarToLWCVectorCast(Components[0]))
		{
			// Cast scalar to float vector, then promote, for example:  "WSPromote((float3)1.0f)"
			Printer << TEXTVIEW("WSPromote") << TEXTVIEW("((float") << PrimitiveType.NumColumns << TEXTVIEW(")");
			if (const MIR::FScalar* Scalar = Components[0]->As<MIR::FScalar>())
			{
				// Print the inner non-LWC scalar value
				LowerValue(Scalar->Arg);
			}
			else if (const MIR::FConstant* Constant = Components[0]->As<MIR::FConstant>())
			{
				// Print the inner double constant in its non-LWC form
				Printer << (float)Constant->Double;
			}
			else
			{
				UE_MIR_UNREACHABLE();
			}
			Printer << TEXTVIEW(")");
		}
		else if (bWholeVectorCast)
		{
			LowerVectorCastChain(Components[0], Components.Num());
		}
		else if (bConstantVector && PrimitiveType.IsDouble())
		{
			// Special case for LWC constant vectors
			bool bAllTileValuesZero = true;
			float TileValues[4];
			float OffsetValues[4];
			for (int32 Index = 0; Index < Components.Num(); Index++)
			{
				FLargeWorldRenderScalar ConstantLWC(Components[Index]->As<MIR::FConstant>()->Double);
				TileValues[Index] = ConstantLWC.GetTile();
				OffsetValues[Index] = ConstantLWC.GetOffset();
				bAllTileValuesZero &= TileValues[Index] == 0.0f;
			}

			if (bAllTileValuesZero)
			{
				// Vector representable as regular floats (all tile values zero), call WSPromote on the offset vector
				Printer << TEXTVIEW("WSPromote(");
			}
			else
			{
				// Vector needs tile values, call LWC constructor and generate tile vector, before generating offset vector below
				Printer << TEXTVIEW("MakeLWCVector") << Components.Num() << TEXTVIEW("(float") << Components.Num() << BeginArgs;
				for (int32 Index = 0; Index < Components.Num(); Index++)
				{
					Printer << ListSeparator << TileValues[Index];
				}
				Printer << EndArgs << TEXTVIEW(", ");
			}

			// Generate offset vector, plus extra parentheses to close WSPromote or MakeLWCVector call
			Printer << TEXTVIEW("float") << Components.Num() << BeginArgs;
			for (int32 Index = 0; Index < Components.Num(); Index++)
			{
				Printer << ListSeparator << OffsetValues[Index];
			}
			Printer << EndArgs << TEXTVIEW(")");
		}
		else
		{
			if (PrimitiveType.IsDouble())
			{
				if (PrimitiveType.IsRowVector())
				{
					Printer << TEXTVIEW("MakeWSVector") << BeginArgs;
				}
				else
				{
					// @todo-jason.hoerner - LWC Matrix support.  I don't think there are any nodes that can build LWC matrices from scratch, these
					// generally can only come from external inputs or material parameter collections, so this isn't necessary at the moment.
					UE_MIR_UNREACHABLE();
				}
			}
			else
			{
				Printer << LowerType(Composite->Type) << BeginArgs;
			}

			for (const MIR::FValue* Component : Components)
			{
				Printer << ListSeparator << LowerValue(Component);
			}

			Printer << EndArgs;
		}
	}

	void LowerSetMaterialOutput(const MIR::FSetMaterialOutput* SetMaterialOutput)
	{
		if (SetMaterialOutput->Property == MP_WorldPositionOffset)
		{
			Printer << TEXTVIEW("return ");
		}
		else
		{
			if (const FMaterialAggregateAttribute* PropertyAggregate = UMaterialAggregate::GetMaterialAttribute(SetMaterialOutput->Property))
			{
				Printer << TEXTVIEW("PixelMaterialInputs.") << PropertyAggregate->Name.ToString() << TEXTVIEW(" = ");
			}
			else
			{
				UE_MIR_UNREACHABLE();
			}			
		}
			
		LowerValue(SetMaterialOutput->Arg);
	}

	void LowerOperator(const MIR::FOperator* Operator)
	{
		// LWCTile operator is special in that it has an extra zero parameter, so it can't go through the normal operator code path
		if (Operator->Op == MIR::UO_LWCTile)
		{
			// Given input float3, generate LWC3 type with the given tile value and zero offset.
			Printer << TEXTVIEW("MakeLWCVector3(") << LowerValue(Operator->AArg) << TEXTVIEW(", 0)");
			return;
		}

		// Whether any of this operator's arguments has double type which requires special handling in the shader.
		bool bIsDouble = Operator->AArg->Type.IsDouble() || (Operator->BArg && Operator->BArg->Type.IsDouble()) || (Operator->CArg && Operator->CArg->Type.IsDouble());

		// Whether the operator in HLSL is infix between its arguments, e.g. "4 + 4"
		bool bOperatorIsInfix = false;
		if (!bIsDouble)
		{
			switch (Operator->Op)
			{
				case MIR::BO_GreaterThan:
				case MIR::BO_GreaterThanOrEquals:
				case MIR::BO_LessThan:
				case MIR::BO_LessThanOrEquals:
				case MIR::BO_Equals:
				case MIR::BO_NotEquals:
				case MIR::BO_Add:
				case MIR::BO_Multiply:
				case MIR::BO_Subtract:
				case MIR::BO_Divide:
				case MIR::BO_Modulo:
					bOperatorIsInfix = true;
					break;

				default: break;
			}
		}

		if (bOperatorIsInfix)
		{
			FStringView OpString;
			switch (Operator->Op)
			{
				case MIR::BO_Add: OpString = TEXTVIEW("+"); break;
				case MIR::BO_Divide: OpString = TEXTVIEW("/"); break;
				case MIR::BO_Modulo: OpString = TEXTVIEW("%"); break;
				case MIR::BO_Equals: OpString = TEXTVIEW("=="); break;
				case MIR::BO_GreaterThan: OpString = TEXTVIEW(">"); break;
				case MIR::BO_GreaterThanOrEquals: OpString = TEXTVIEW(">="); break;
				case MIR::BO_LessThan: OpString = TEXTVIEW("<"); break;
				case MIR::BO_LessThanOrEquals: OpString = TEXTVIEW("<="); break;
				case MIR::BO_Multiply: OpString = TEXTVIEW("*"); break;
				case MIR::BO_NotEquals: OpString = TEXTVIEW("!="); break;
				case MIR::BO_Subtract: OpString = TEXTVIEW("-"); break;
				default: UE_MIR_UNREACHABLE();
			}

			Printer << TEXTVIEW("(") << LowerValue(Operator->AArg) << TEXTVIEW(" ") << OpString << TEXTVIEW(" ") << LowerValue(Operator->BArg) << TEXTVIEW(")");
		}
		else
		{
			FStringView OpString;
			if (bIsDouble)
			{
				// "Demotes LWC" indicates the given operator returns a non-LWC float, even if the input is LWC.  Besides that, comparison
				// operators all return bool, instead of LWC, and a couple specific operators require specific inputs to always be non-LWC
				// (second argument of Fmod, and third argument of Lerp).
				switch (Operator->Op)
				{
					case MIR::UO_Abs: OpString = TEXTVIEW("WSAbs"); break;
					case MIR::UO_ACos: OpString = TEXTVIEW("WSACos"); break;						// Demotes LWC
					case MIR::UO_ASin: OpString = TEXTVIEW("WSASin"); break;						// Demotes LWC
					case MIR::UO_ATan: OpString = TEXTVIEW("WSATan"); break;						// Demotes LWC
					case MIR::UO_Ceil: OpString = TEXTVIEW("WSCeil"); break;
					case MIR::UO_Cos: OpString = TEXTVIEW("WSCos"); break;							// Demotes LWC
					case MIR::UO_Floor: OpString = TEXTVIEW("WSFloor"); break;
					case MIR::UO_Frac: OpString = TEXTVIEW("WSFracDemote"); break;					// Demotes LWC
					case MIR::UO_Length: OpString = TEXTVIEW("WSLength"); break;
					case MIR::UO_Negate: OpString = TEXTVIEW("WSNegate"); break;
					case MIR::UO_Round: OpString = TEXTVIEW("WSRound"); break;
					case MIR::UO_Saturate: OpString = TEXTVIEW("WSSaturateDemote"); break;			// Demotes LWC
					case MIR::UO_Sign: OpString = TEXTVIEW("WSSign"); break;						// Demotes LWC
					case MIR::UO_Sin: OpString = TEXTVIEW("WSSin"); break;							// Demotes LWC
					case MIR::UO_Sqrt: OpString = TEXTVIEW("WSSqrtDemote"); break;					// Demotes LWC
					case MIR::UO_Tan: OpString = TEXTVIEW("WSTan"); break;							// Demotes LWC
					case MIR::UO_Truncate: OpString = TEXTVIEW("WSTrunc"); break;

					case MIR::BO_Add: OpString = TEXTVIEW("WSAdd"); break;
					case MIR::BO_Distance: OpString = TEXTVIEW("WSDistance"); break;
					case MIR::BO_Divide: OpString = TEXTVIEW("WSDivide"); break;
					case MIR::BO_Dot: OpString = TEXTVIEW("WSDot"); break;
					case MIR::BO_Equals: OpString = TEXTVIEW("WSEquals"); break;					// Bool output
					case MIR::BO_Fmod: OpString = TEXTVIEW("WSFmodDemote"); break;					// Demotes LWC, second input must be float (not LWC)!
					case MIR::BO_GreaterThan: OpString = TEXTVIEW("WSGreater"); break;				// Bool output
					case MIR::BO_GreaterThanOrEquals: OpString = TEXTVIEW("WSGreaterEqual"); break;	// Bool output
					case MIR::BO_LessThan: OpString = TEXTVIEW("WSLess"); break;					// Bool output
					case MIR::BO_LessThanOrEquals: OpString = TEXTVIEW("WSLessEqual"); break;		// Bool output
					case MIR::BO_Max: OpString = TEXTVIEW("WSMax"); break;
					case MIR::BO_Min: OpString = TEXTVIEW("WSMin"); break;
					case MIR::BO_Multiply: OpString = TEXTVIEW("WSMultiply"); break;
					case MIR::BO_MatrixMultiply: OpString = TEXTVIEW("WSMultiply"); break;			// TODO: implement WSMultiplyVector, WSMultiplyDemote through tiling
					case MIR::BO_NotEquals: OpString = TEXTVIEW("WSNotEquals"); break;				// Bool output
					case MIR::BO_Step: OpString = TEXTVIEW("WSStep"); break;						// Demotes LWC
					case MIR::BO_Subtract: OpString = TEXTVIEW("WSSubtract"); break;

					case MIR::TO_Clamp: OpString = TEXTVIEW("WSClamp"); break;
					case MIR::TO_Lerp: OpString = TEXTVIEW("WSLerp"); break;						// Third input must be float (not LWC)!
					case MIR::TO_Select: OpString = TEXTVIEW("WSSelect"); break;
					case MIR::TO_Smoothstep: OpString = TEXTVIEW("WSSmoothStepDemote"); break;		// Demotes LWC

					default: UE_MIR_UNREACHABLE();
				}
			}
			else
			{
				switch (Operator->Op)
				{
					case MIR::UO_Abs: OpString = TEXTVIEW("abs"); break;
					case MIR::UO_ACos: OpString = TEXTVIEW("acos"); break;
					case MIR::UO_ACosFast: OpString = TEXTVIEW("acosFast"); break;
					case MIR::UO_ACosh: OpString = TEXTVIEW("acosh"); break;
					case MIR::UO_ASin: OpString = TEXTVIEW("asin"); break;
					case MIR::UO_ASinFast: OpString = TEXTVIEW("asinFast"); break;
					case MIR::UO_ASinh: OpString = TEXTVIEW("asinh"); break;
					case MIR::UO_ATan: OpString = TEXTVIEW("atan"); break;
					case MIR::UO_ATanFast: OpString = TEXTVIEW("atanFast"); break;
					case MIR::UO_ATanh: OpString = TEXTVIEW("atanh"); break;
					case MIR::UO_Ceil: OpString = TEXTVIEW("ceil"); break;
					case MIR::UO_Cos: OpString = TEXTVIEW("cos"); break;
					case MIR::UO_Cosh: OpString = TEXTVIEW("cosh"); break;
					case MIR::UO_Exponential: OpString = TEXTVIEW("exp"); break;
					case MIR::UO_Exponential2: OpString = TEXTVIEW("exp2"); break;
					case MIR::UO_Floor: OpString = TEXTVIEW("floor"); break;
					case MIR::UO_Frac: OpString = TEXTVIEW("frac"); break;
					case MIR::UO_IsFinite: OpString = TEXTVIEW("isfinite"); break;
					case MIR::UO_IsInf: OpString = TEXTVIEW("isinf"); break;
					case MIR::UO_IsNan: OpString = TEXTVIEW("isnan"); break;
					case MIR::UO_Length: OpString = TEXTVIEW("length"); break;
					case MIR::UO_Logarithm: OpString = TEXTVIEW("log"); break;
					case MIR::UO_Logarithm10: OpString = TEXTVIEW("log10"); break;
					case MIR::UO_Logarithm2: OpString = TEXTVIEW("log2"); break;
					case MIR::UO_Negate: OpString = TEXTVIEW("-"); break;
					case MIR::UO_Reciprocal: OpString = TEXTVIEW("rcp"); break;
					case MIR::UO_Round: OpString = TEXTVIEW("round"); break;
					case MIR::UO_Rsqrt: OpString = TEXTVIEW("rsqrt"); break;
					case MIR::UO_Saturate: OpString = TEXTVIEW("saturate"); break;
					case MIR::UO_Sign: OpString = TEXTVIEW("sign"); break;
					case MIR::UO_Sin: OpString = TEXTVIEW("sin"); break;
					case MIR::UO_Sinh: OpString = TEXTVIEW("sinh"); break;
					case MIR::UO_Sqrt: OpString = TEXTVIEW("sqrt"); break;
					case MIR::UO_Tan: OpString = TEXTVIEW("tan"); break;
					case MIR::UO_Tanh: OpString = TEXTVIEW("tanh"); break;
					case MIR::UO_Truncate: OpString = TEXTVIEW("trunc"); break;

					case MIR::BO_And: OpString = TEXTVIEW("and"); break;
					case MIR::BO_ATan2: OpString = TEXTVIEW("atan2"); break;
					case MIR::BO_ATan2Fast: OpString = TEXTVIEW("atan2Fast"); break;
					case MIR::BO_Cross: OpString = TEXTVIEW("cross"); break;
					case MIR::BO_Distance: OpString = TEXTVIEW("distance"); break;
					case MIR::BO_Dot: OpString = TEXTVIEW("dot"); break;
					case MIR::BO_Fmod: OpString = TEXTVIEW("fmod"); break;
					case MIR::BO_Max: OpString = TEXTVIEW("max"); break;
					case MIR::BO_MatrixMultiply: OpString = TEXTVIEW("mul"); break;
					case MIR::BO_Min: OpString = TEXTVIEW("min"); break;
					case MIR::BO_Or: OpString = TEXTVIEW("or"); break;
					case MIR::BO_Pow: OpString = TEXTVIEW("pow"); break;
					case MIR::BO_Step: OpString = TEXTVIEW("step"); break;

					case MIR::TO_Clamp: OpString = TEXTVIEW("clamp"); break;
					case MIR::TO_Lerp: OpString = TEXTVIEW("lerp"); break;
					case MIR::TO_Select: OpString = TEXTVIEW("select"); break;
					case MIR::TO_Smoothstep: OpString = TEXTVIEW("smoothstep"); break;
			
					default: UE_MIR_UNREACHABLE();
				}
			}

			// Unary
			Printer << OpString << TEXTVIEW("(") << LowerValue(Operator->AArg);

			// Binary
			if (Operator->BArg)
			{
				check(MIR::IsBinaryOperator(Operator->Op) || MIR::IsTernaryOperator(Operator->Op));
				Printer << TEXTVIEW(", ") << LowerValue(Operator->BArg);
			}

			// Ternary
			if (Operator->CArg)
			{
				check(MIR::IsTernaryOperator(Operator->Op));
				Printer << TEXTVIEW(", ") << LowerValue(Operator->CArg);
			}

			Printer << TEXTVIEW(")");
		}
	}

	void LowerBranch(const MIR::FBranch* Branch)
	{
		if (IsFoldable(Branch, CurrentEntryPointIndex))
		{
			if (Branch->TrueArg->Type.IsDouble())
			{
				Printer << TEXTVIEW("WSSelect(") << LowerValue(Branch->ConditionArg)
					<< TEXTVIEW(", ") << LowerValue(Branch->TrueArg)
					<< TEXTVIEW(", ") << LowerValue(Branch->FalseArg)
					<< TEXTVIEW(")");
			}
			else
			{
				Printer << LowerValue(Branch->ConditionArg)
					<< TEXTVIEW(" ? ") << LowerValue(Branch->TrueArg)
					<< TEXTVIEW(" : ") << LowerValue(Branch->FalseArg);
			}
		}
		else
		{
			Printer << EndOfStatement;
			Printer << TEXTVIEW("if (") << LowerValue(Branch->ConditionArg) << TEXTVIEW(")") << NewLine << OpenBrace;
			Printer << LowerBlock(Branch->TrueBlock[CurrentEntryPointIndex]);
			Printer << TEXTVIEW("_") << InstrToLocalIndex[Branch] << TEXTVIEW(" = ") << LowerValue(Branch->TrueArg) << EndOfStatement;
			Printer << CloseBrace << NewLine;
			Printer << TEXTVIEW("else") << NewLine << OpenBrace;
			Printer << LowerBlock(Branch->FalseBlock[CurrentEntryPointIndex]);
			Printer << TEXTVIEW("_") << InstrToLocalIndex[Branch] << TEXTVIEW(" = ") << LowerValue(Branch->FalseArg) << EndOfStatement;
			Printer << CloseBrace;
		}
	}
			
	void LowerSubscript(const MIR::FSubscript* Subscript)
	{
		if (TOptional<MIR::FPrimitive> ArgVectorType = Subscript->Arg->Type.AsVector())
		{
			if (ArgVectorType->IsDouble())
			{
				FStringView LwcComponentsStr[] = { TEXTVIEW("WSGetX("), TEXTVIEW("WSGetY("), TEXTVIEW("WSGetZ("), TEXTVIEW("WSGetW(") };
				check(Subscript->Index <= ArgVectorType->NumComponents());

				Printer << LwcComponentsStr[Subscript->Index] << LowerValue(Subscript->Arg) << TEXTVIEW(")");
			}
			else
			{
				LowerValue(Subscript->Arg);
				
				FStringView ComponentsStr[] = { TEXTVIEW(".x"), TEXTVIEW(".y"), TEXTVIEW(".z"), TEXTVIEW(".w") };
				check(Subscript->Index <= ArgVectorType->NumComponents());

				Printer << ComponentsStr[Subscript->Index];
			}
		}
		else if (TOptional<MIR::FPrimitive> MatrixType = Subscript->Arg->Type.AsMatrix())
		{
			check(!MatrixType->IsDouble()); // The emitter should have checked this

			LowerValue(Subscript->Arg);

			// Print matrix component swizzle, e.g. `M._m00`
			check(Subscript->Index < MatrixType->NumComponents());
			Printer << TEXTVIEW("._m") << (Subscript->Index % MatrixType->NumRows) << (Subscript->Index / MatrixType->NumRows);
		}
		else
		{
			// The builder should never emit subscripts of scalar types.
			UE_MIR_UNREACHABLE();
		}
	}

	void LowerScalar(const MIR::FScalar* Scalar)
	{
		MIR::FPrimitive PrimitiveType = Scalar->Type.GetPrimitive();
		MIR::FPrimitive ArgPrimitiveType = Scalar->Arg->Type.GetPrimitive();

		if (ArgPrimitiveType.IsDouble())
		{
			// Cast from LWC
			if (PrimitiveType.IsBoolean())
			{
				// Cast to bool requires a comparison with zero, outside the WSDemote
				Printer << TEXTVIEW("(WSDemote(") << LowerValue(Scalar->Arg) << TEXTVIEW(") != 0)");
			}
			else if (!PrimitiveType.IsFloat())
			{
				// Cast to non-float (integer) requires a cast to the type, outside the WSDemote
				Printer << LowerType(Scalar->Type) << TEXTVIEW("(WSDemote(") << LowerValue(Scalar->Arg) << TEXTVIEW("))");
			}
			else
			{
				// Cast to float
				Printer << TEXTVIEW("WSDemote(") << LowerValue(Scalar->Arg) << TEXTVIEW(")");
			}
		}
		else if (PrimitiveType.IsDouble())
		{
			// Cast to LWC
			if (ArgPrimitiveType.IsBoolean())
			{
				// Cast from bool requires a select between 1.0f and 0.0f, inside the WSPromote
				Printer << TEXTVIEW("WSPromote(") << LowerValue(Scalar->Arg) << TEXTVIEW(" ? 1.0f : 0.0f)");
			}
			else if (!ArgPrimitiveType.IsFloat())
			{
				// Cast from non-float (integer) requires a cast to float, inside the WSPromote
				Printer << TEXTVIEW("WSPromote(float(") << LowerValue(Scalar->Arg) << TEXTVIEW("))");
			}
			else
			{
				// Cast from float
				Printer << TEXTVIEW("WSPromote(") << LowerValue(Scalar->Arg) << TEXTVIEW(")");
			}
		}
		else
		{
			// Cast between intrinsic types
			if (PrimitiveType.IsBoolean())
			{
				// Cast to bool requires a comparison with zero
				Printer << TEXTVIEW("(") << LowerValue(Scalar->Arg) << TEXTVIEW(" != 0)");
			}
			else if (ArgPrimitiveType.IsBoolean())
			{
				// Cast from bool requires a select between 1 and 0
				Printer << LowerType(Scalar->Type) << TEXTVIEW("(") << LowerValue(Scalar->Arg) << TEXTVIEW(" ? 1 : 0)");
			}
			else
			{
				// Cast between arithmetic types
				Printer << LowerType(Scalar->Type) << TEXTVIEW("(") << LowerValue(Scalar->Arg) << TEXTVIEW(")");
			}
		}
	}

	ENoOp LowerTextureMaterialType(EMaterialValueType TextureType, bool bForResourceDeclarations = false)
	{
		switch (TextureType)
		{
		case MCT_Texture2D: Printer << TEXTVIEW("Texture2D"); break;
		case MCT_TextureCube: Printer << TEXTVIEW("TextureCube"); break;
		case MCT_Texture2DArray: Printer << TEXTVIEW("Texture2DArray"); break;
		case MCT_TextureCubeArray: Printer << TEXTVIEW("TextureCubeArray"); break;
		case MCT_VolumeTexture: Printer << (bForResourceDeclarations ? TEXTVIEW("VolumeTexture") : TEXTVIEW("Texture3D")); break;
		case MCT_TextureExternal: Printer << TEXTVIEW("Texture2D"); break;
		case MCT_TextureVirtual: Printer << TEXTVIEW("VirtualTexturePhysical"); break;
		default: UE_MIR_UNREACHABLE();
		}
		return NoOp;
	}

	void LowerTextureType(const MIR::FTextureObject* TextureObject)
	{
		check(TextureObject->Texture);
		LowerTextureMaterialType(TextureObject->Texture->GetMaterialType());
	}

	UObject* GetTextureFromUniformParameter(const MIR::FUniformParameter* UniformParameter) const
	{
		const FMaterialParameterValue& TextureParameter = Module->GetParameterMetadata(UniformParameter->ParameterIdInModule).Value;
		return TextureParameter.AsTextureObject();
	}

	void LowerTextureParameterType(const MIR::FUniformParameter* UniformParameter)
	{
		UObject* Texture = GetTextureFromUniformParameter(UniformParameter);
		check(Texture);
		LowerTextureMaterialType(MIR::Internal::GetTextureMaterialValueType(Texture));
	}

	void LowerStandardTextureRead(const MIR::FTextureRead* TextureRead)
	{
		switch (TextureRead->TextureObject->Kind)
		{
			case MIR::EValueKind::VK_TextureObject: LowerTextureType(TextureRead->TextureObject->As<MIR::FTextureObject>()); break;
			case MIR::EValueKind::VK_RuntimeVirtualTextureObject: LowerTextureMaterialType(MCT_TextureVirtual); break;
			case MIR::EValueKind::VK_UniformParameter: LowerTextureParameterType(TextureRead->TextureObject->As<MIR::FUniformParameter>()); break;
			default: UE_MIR_UNREACHABLE();
		}

		switch (TextureRead->Mode)
		{
			case MIR::ETextureReadMode::GatherRed: Printer << TEXTVIEW("GatherRed"); break;
			case MIR::ETextureReadMode::GatherGreen: Printer << TEXTVIEW("GatherGreen"); break;
			case MIR::ETextureReadMode::GatherBlue: Printer << TEXTVIEW("GatherBlue"); break;
			case MIR::ETextureReadMode::GatherAlpha: Printer << TEXTVIEW("GatherAlpha"); break;
			case MIR::ETextureReadMode::MipAuto: Printer << TEXTVIEW("Sample"); break;
			case MIR::ETextureReadMode::MipLevel: Printer << TEXTVIEW("SampleLevel"); break;
			case MIR::ETextureReadMode::MipBias: Printer << TEXTVIEW("SampleBias"); break;
			case MIR::ETextureReadMode::Derivatives: Printer << TEXTVIEW("SampleGrad"); break;
			default: UE_MIR_UNREACHABLE();
		}

		Printer << BeginArgs
				<< ListSeparator << LowerValue(TextureRead->TextureObject)
				<< ListSeparator << LowerTextureSamplerReference(TextureRead->TextureObject, TextureRead->SamplerSourceMode)
				<< ListSeparator << LowerValue(TextureRead->TexCoord);

		switch (TextureRead->Mode)
		{
			case MIR::ETextureReadMode::MipLevel: Printer << ListSeparator << LowerValue(TextureRead->MipValue); break;
			case MIR::ETextureReadMode::MipBias: Printer << ListSeparator << LowerValue(TextureRead->MipValue); break;
			case MIR::ETextureReadMode::Derivatives: Printer << ListSeparator << LowerValue(TextureRead->TexCoordDdx) << ListSeparator << LowerValue(TextureRead->TexCoordDdy); break;
			default: break;
		}

		Printer << EndArgs;
	}

	void LowerVirtualTextureRead(const MIR::FTextureRead* TextureRead)
	{
		checkf(TextureRead->VTPageTable, TEXT("Missing page table for virtual texture read instruction"));
		const MIR::FVTPageTableRead* VTPageTableResult = TextureRead->VTPageTable->As<MIR::FVTPageTableRead>();

		const int32 VirtualTextureIndex = TextureRead->TextureObject->GetUniformParameterIndex();
		check(VirtualTextureIndex >= 0);

		// Sampling function
		Printer << TEXTVIEW("TextureVirtualSample");

		// 'Texture name/sampler', 'PageTableResult', 'LayerIndex', 'PackedUniform'
		Printer
			<< BeginArgs
			<< ListSeparator << LowerValue(TextureRead->TextureObject)
			<< ListSeparator;
		
		// Sampling function argument list
		if (TextureRead->SamplerSourceMode != SSM_FromTextureAsset)
		{
			// VT doesn't care if the shared sampler is wrap or clamp. It only cares if it is aniso or not.
			// The wrap/clamp/mirror operation is handled in the shader explicitly.
			// This generates: GetMaterialSharedSampler(Material.VirtualTexturePhysical_<VirtualTextureIndex>Sampler, <SharedSamplerName>)
			FStringView SharedSamplerName = TextureRead->bUseAnisoSampler ? TEXTVIEW("View.SharedBilinearAnisoClampedSampler") : TEXTVIEW("View.SharedBilinearClampedSampler");
			Printer << TEXTVIEW("GetMaterialSharedSampler(Material.VirtualTexturePhysical_") << VirtualTextureIndex << TEXTVIEW("Sampler, ") << SharedSamplerName << TEXTVIEW(")");
		}
		else
		{
			Printer << TEXTVIEW("Material.VirtualTexturePhysical_") << VirtualTextureIndex << TEXTVIEW("Sampler");
		}

		Printer << ListSeparator << LowerValue(TextureRead->VTPageTable)
			<< ListSeparator << VTPageTableResult->VTPageTableIndex
			<< ListSeparator << TEXTVIEW("VTUniform_Unpack(") << TEXT("Material.VTPackedUniform[") << VirtualTextureIndex << TEXTVIEW("]") << TEXTVIEW(")")
			<< EndArgs;
	}

	void LowerTextureRead(const MIR::FTextureRead* TextureRead)
	{
		bool bSamplerNeedsBrackets = false;
		LowerSamplerType(TextureRead->SamplerType, bSamplerNeedsBrackets);
		if (bSamplerNeedsBrackets)
		{
			Printer << TEXTVIEW("(");
		}

		checkf(
			TextureRead->TextureObject->Type.IsTexture() ||
			TextureRead->TextureObject->Type.IsRuntimeVirtualTexture(),
			TEXT("Invalid texture object type")
		);

		if (IsVirtualSamplerType(TextureRead->SamplerType))
		{
			LowerVirtualTextureRead(TextureRead);
		}
		else
		{
			LowerStandardTextureRead(TextureRead);
		}

		if (bSamplerNeedsBrackets)
		{
			Printer << TEXTVIEW(")");
		}
	}

	void LowerVTPageTableRead(const MIR::FVTPageTableRead* VTPageTableRead)
	{
		using namespace UE::MaterialTranslatorUtils;

		const bool bHasDerivativeTexCoords = VTPageTableRead->TexCoordDdx != nullptr && VTPageTableRead->TexCoordDdy != nullptr;

		// Construct VT page table load function name 'TextureLoadVirtualPageTable [ Adaptive ] [ * | Grad | Level ]'
		Printer << TEXTVIEW("TextureLoadVirtualPageTable");
		if (VTPageTableRead->bIsAdaptive)
		{
			Printer << TEXTVIEW("Adaptive");
		}

		switch (VTPageTableRead->MipValueMode)
		{
			case TMVM_None: [[fallthrough]];
			case TMVM_MipBias: if (bHasDerivativeTexCoords) { Printer << TEXTVIEW("Grad"); } break;
			case TMVM_MipLevel: Printer << TEXTVIEW("Level"); break;
			case TMVM_Derivative: Printer << TEXTVIEW("Grad"); break;
			default: UE_MIR_UNREACHABLE();
		}

		// Lower common parameters that are shared across all VT page table load functions
		Printer
			<< BeginArgs
			<< ListSeparator << TEXTVIEW("VIRTUALTEXTURE_PAGETABLE_") << VTPageTableRead->VTStackIndex
			<< ListSeparator << TEXTVIEW("VTPageTableUniform_Unpack(VIRTUALTEXTURE_PAGETABLE_UNIFORM_") << VTPageTableRead->VTStackIndex << TEXTVIEW(")")
			<< ListSeparator << LowerValue(VTPageTableRead->TexCoord)
			<< ListSeparator << FStringView{ GetVTAddressMode(VTPageTableRead->AddressU) }
			<< ListSeparator << FStringView{ GetVTAddressMode(VTPageTableRead->AddressV) }
			;

		// Lower additional parameters depening on VT page table load function
		switch (VTPageTableRead->MipValueMode)
		{
			case TMVM_None:
				if (bHasDerivativeTexCoords)
				{
					Printer
						<< ListSeparator << LowerValue(VTPageTableRead->TexCoordDdx)
						<< ListSeparator << LowerValue(VTPageTableRead->TexCoordDdy);
				}
				else
				{
					constexpr float kZeroMipBias = 0.0f;
					Printer << ListSeparator << kZeroMipBias;
				}
				break;

			case TMVM_MipBias:
				if (bHasDerivativeTexCoords)
				{
					Printer
						<< ListSeparator << LowerValue(VTPageTableRead->TexCoordDdx)
						<< ListSeparator << LowerValue(VTPageTableRead->TexCoordDdy);
				}
				else
				{
					check(VTPageTableRead->MipValue);
					Printer << ListSeparator << LowerValue(VTPageTableRead->MipValue);
				}
				break;

			case TMVM_MipLevel:
				check(VTPageTableRead->MipValue);
				Printer << ListSeparator << LowerValue(VTPageTableRead->MipValue);
				break;

			case TMVM_Derivative:
				check(bHasDerivativeTexCoords);
				Printer
					<< ListSeparator << LowerValue(VTPageTableRead->TexCoordDdx)
					<< ListSeparator << LowerValue(VTPageTableRead->TexCoordDdy);
				break;
		}

		Printer << ListSeparator << TEXTVIEW("Parameters.SvPosition.xy");

		// Lower final arguments for VT feedback
		if (VTPageTableRead->bEnableFeedback && CurrentStage == MIR::EStage::Stage_Pixel)
		{
			Printer	<< ListSeparator << TEXTVIEW("Parameters.VirtualTextureFeedback");
		}

		Printer << EndArgs;
	}

	void LowerSamplerType(EMaterialSamplerType SamplerType, bool& bOutSamplerNeedsBrackets)
	{
		bOutSamplerNeedsBrackets = true;

		switch (SamplerType)
		{
			case SAMPLERTYPE_External:
				Printer << TEXTVIEW("ProcessMaterialExternalTextureLookup");
				break;

			case SAMPLERTYPE_Color:
				Printer << TEXTVIEW("ProcessMaterialColorTextureLookup");
				break;
			case SAMPLERTYPE_VirtualColor:
				// has a mobile specific workaround
				Printer << TEXTVIEW("ProcessMaterialVirtualColorTextureLookup");
				break;

			case SAMPLERTYPE_LinearColor:
			case SAMPLERTYPE_VirtualLinearColor:
				Printer << TEXTVIEW("ProcessMaterialLinearColorTextureLookup");
				break;

			case SAMPLERTYPE_Alpha:
			case SAMPLERTYPE_VirtualAlpha:
			case SAMPLERTYPE_DistanceFieldFont:
				Printer << TEXTVIEW("ProcessMaterialAlphaTextureLookup");
				break;

			case SAMPLERTYPE_Grayscale:
			case SAMPLERTYPE_VirtualGrayscale:
				Printer << TEXTVIEW("ProcessMaterialGreyscaleTextureLookup");
				break;

			case SAMPLERTYPE_LinearGrayscale:
			case SAMPLERTYPE_VirtualLinearGrayscale:
				Printer << TEXTVIEW("ProcessMaterialLinearGreyscaleTextureLookup");
				break;

			case SAMPLERTYPE_Normal:
			case SAMPLERTYPE_VirtualNormal:
				// Normal maps need to be unpacked in the pixel shader.
				Printer << TEXTVIEW("UnpackNormalMap");
				break;

			case SAMPLERTYPE_Masks:
			case SAMPLERTYPE_VirtualMasks:
			case SAMPLERTYPE_Data:
				bOutSamplerNeedsBrackets = false;
				break;

			default:
				UE_MIR_UNREACHABLE();
		}
	}

	ENoOp LowerTextureSamplerReference(MIR::FValue* TextureValue, ESamplerSourceMode SamplerSource)
	{
		if (SamplerSource != SSM_FromTextureAsset)
		{
			Printer << TEXTVIEW("GetMaterialSharedSampler(");
		}
		
		Printer << LowerValue(TextureValue) << TEXTVIEW("Sampler");
		
		if (SamplerSource == SSM_Wrap_WorldGroupSettings)
		{
			Printer << TEXTVIEW(", View.MaterialTextureBilinearWrapedSampler)");
		}
		else if (SamplerSource == SSM_Clamp_WorldGroupSettings)
		{
			Printer << TEXTVIEW(", View.MaterialTextureBilinearClampedSampler)");
		}
		else
		{
			// SSM_TerrainWeightmapGroupSettings unsupported yet
			check(SamplerSource == SSM_FromTextureAsset);
		}

		return NoOp;
	}

	ENoOp LowerTextureReference(EMaterialValueType TextureType, int32 TextureParameterIndex)
	{
		checkf(TextureParameterIndex != INDEX_NONE, TEXT("Texture uniform parameter not assigned! A texture is used for sampling but hasn't been properly registered during IR analysis."));
		Printer << TEXTVIEW("Material.") << LowerTextureMaterialType(TextureType, true) << TEXTVIEW("_") << TextureParameterIndex;
		return NoOp;
	}
	
	void LowerStageSwitch(const MIR::FStageSwitch* StageSwitch)
	{
		LowerValue(StageSwitch->Args[CurrentStage]);
	}

	void LowerHardwarePartialDerivative(const MIR::FHardwarePartialDerivative* StageSwitch)
	{
		if (StageSwitch->Arg->Type.IsDouble())
		{
			// Expression emitter assumes these to be LWC for analytic derivative evaluation, so we promote them on load
			Printer << (StageSwitch->Axis == MIR::EDerivativeAxis::X ? TEXTVIEW("WSPromote(WSDdxDemote(") : TEXTVIEW("WSPromote(WSDdyDemote(")) << LowerValue(StageSwitch->Arg) << TEXTVIEW("))");
		}
		else
		{
			Printer << (StageSwitch->Axis == MIR::EDerivativeAxis::X ? TEXTVIEW("DDX(") : TEXTVIEW("DDY(")) << LowerValue(StageSwitch->Arg) << TEXTVIEW(")");
		}
	}

	void LowerNop(const MIR::FNop* Nop)
	{
		// NOP instructions are only used to analyze their argument, but have no effect, thus we compile it to a default
		// value based on its type.
		checkf(!Nop->Type.IsDouble(), TEXT("NOPs do not support LWC primitive type yet"));
		Printer << TEXTVIEW("((") << LowerType(Nop->Type) << TEXTVIEW(")0)");
	}

	void LowerCall(const MIR::FCall* Call)
	{
		int32 ParamLocal = NumLocals;
		
		// Generate locals to store the output and input-output parameters
		for (int32 i = Call->Function->NumInputOnlyParams; i < Call->Function->NumParameters; ++i)
		{
			Printer << LowerType(Call->Function->Parameters[i].Type) << TEXT(" _") << (NumLocals++);
			
			if (i < Call->NumArguments)
			{
				Printer << TEXT(" = ") << LowerValue(Call->Arguments[i]);
			}

			Printer << EndOfStatement;
		}

		// Print the local that will store the result and assign it to the call to the custom function.
		Printer << LowerType(Call->Function->ReturnType) << TEXT(" _") << NumLocals << TEXT(" = ") << TEXT("C") << Call->Function->UniqueId << TEXT("_") << Call->Function->Name;
		
		Printer << BeginArgs << TEXT("Parameters");

		// Print function call arguments
		for (int32 i = 0; i < Call->Function->NumParameters; ++i)
		{
			Printer << TEXT(", ");

			// Outputs and input-output parameters are stored in special locals. Refer to them
			if (i >= Call->Function->NumInputOnlyParams)
			{
				Printer << TEXT("_") << ParamLocal++;
			}
			else
			{
				// Input-only parameters instead can inline their value instead
				Printer << LowerValue(Call->Arguments[i]);
			}
		}
		Printer << EndArgs;

		// Assign a local to the call result
		InstrToLocalIndex.Add(Call, NumLocals++);
	}

	void LowerCallOutput(const MIR::FCallParameterOutput* CallOutput)
	{
		const MIR::FCall* Call = CallOutput->Call->As<MIR::FCall>();
		int32 ParamIndex = InstrToLocalIndex[Call];

		// Void functions don't use a local
		if (!Call->Function->ReturnType.IsVoid())
		{
			ParamIndex -= 1;
		}

		// Compute the index of the local that stores the additional output parameter 
		ParamIndex = ParamIndex - Call->Function->NumInputAndOutputParams + Call->Function->NumInputOnlyParams + CallOutput->Index;

		Printer << TEXT("_") << ParamIndex;
	}

	ENoOp LowerType(const MIR::FType& Type)
	{
		if (TOptional<MIR::FPrimitive> PrimitiveType = Type.AsPrimitive())
		{
			if (PrimitiveType->IsDouble())
			{
				if (PrimitiveType->IsScalar())
				{
					Printer << TEXTVIEW("FWSScalar");
				}
				else if (PrimitiveType->IsRowVector())
				{
					Printer << TEXTVIEW("FWSVector") << PrimitiveType->NumComponents();
				}
				else if (PrimitiveType->bIsLWCInverseMatrix)
				{
					Printer << TEXTVIEW("FWSInverseMatrix");
				}
				else
				{
					Printer << TEXTVIEW("FWSMatrix");
				}
			}
			else
			{
				switch (PrimitiveType->ScalarKind)
				{
					case MIR::EScalarKind::Bool:  Printer << TEXTVIEW("bool"); break;
					case MIR::EScalarKind::Int:   Printer << TEXTVIEW("int"); break;
					case MIR::EScalarKind::Float: Printer << TEXTVIEW("MaterialFloat"); break;
				}

				if (PrimitiveType->NumRows == 1 && PrimitiveType->NumColumns > 1)
				{
					Printer << PrimitiveType->NumColumns;
				}
				else if (PrimitiveType->IsMatrix())
				{
					Printer << PrimitiveType->NumRows << TEXTVIEW("x") << PrimitiveType->NumColumns;
				}
			}
		}
		else if (Type.IsVoid())
		{
			Printer << TEXTVIEW("void");
		}
		else if (Type.IsSubstrateData())
		{
			Printer << TEXTVIEW("FSubstrateData");
		}
		else if (Type.IsVTPageTableResult())
		{
			Printer << TEXTVIEW("VTPageTableResult");
		}
		else
		{
			UE_MIR_UNREACHABLE();
		}

		return NoOp;
	}
	
	/* Finalization */

	void GenerateTemplateStringParameters(TMap<FString, FString>& Params)
	{
		const FMaterialIRModule::FStatistics ModuleStatistics = Module->GetStatistics();
		Params.Add(TEXT("pixel_material_inputs"), MoveTemp(PixelAttributesHLSL));

		// "Normal" is treated in a special way because the rest of the attributes may lead back to reading it.
		// Therefore, in the way MaterialTemplate.ush is structured, it needs to be evaluated before other attributes.
		Params.Add(TEXT("calc_pixel_material_inputs_analytic_derivatives_normal"), EvaluateNormalMaterialAttributeHLSL[MIR::Stage_Compute]);
		Params.Add(TEXT("calc_pixel_material_inputs_normal"), EvaluateNormalMaterialAttributeHLSL[MIR::Stage_Pixel]);

		// Then the other other attributes.
		Params.Add(TEXT("calc_pixel_material_inputs_analytic_derivatives_other_inputs"), EvaluateOtherMaterialAttributesHLSL[MIR::Stage_Compute]);
		Params.Add(TEXT("calc_pixel_material_inputs_other_inputs"), EvaluateOtherMaterialAttributesHLSL[MIR::Stage_Pixel]);
		
		// MaterialAttributes
		FString MaterialDeclarations;
		MaterialDeclarations.Append(TEXTVIEW("struct FMaterialAttributes\n{\n"));
		for (const FGuid& AttributeID : FMaterialAttributeDefinitionMap::GetOrderedVisibleAttributeList())
		{
			const FString& PropertyName = FMaterialAttributeDefinitionMap::GetAttributeName(AttributeID);
			const EMaterialValueType PropertyType = FMaterialAttributeDefinitionMap::GetValueType(AttributeID);
			MaterialDeclarations.Appendf(TEXT(TAB "%s %s;\n"), GetHLSLTypeString(PropertyType), *PropertyName);
		}
		MaterialDeclarations.Append(TEXTVIEW("};"));
		Params.Add(TEXT("material_declarations"), MoveTemp(MaterialDeclarations));
		
		Params.Add(TEXT("num_material_texcoords_vertex"), FString::FromInt(ModuleStatistics.NumVertexTexCoords));
		Params.Add(TEXT("num_material_texcoords"), FString::FromInt(ModuleStatistics.NumPixelTexCoords));
		Params.Add(TEXT("num_custom_vertex_interpolators"), FString::FromInt(0));
		Params.Add(TEXT("num_tex_coord_interpolators"), FString::FromInt(ModuleStatistics.NumPixelTexCoords));
		
		FString GetMaterialCustomizedUVS;
		for (int32 CustomUVIndex = 0; CustomUVIndex < ModuleStatistics.NumPixelTexCoords; CustomUVIndex++)
		{
			const FMaterialAggregateAttribute* PropertyAggregate = UMaterialAggregate::GetMaterialAttribute((EMaterialProperty)(MP_CustomizedUVs0 + CustomUVIndex));
			if (!PropertyAggregate)
			{
				continue;
			}
			GetMaterialCustomizedUVS.Appendf(TEXT(TAB "OutTexCoords[%u] = Parameters.MaterialAttributes.%s;\n"), CustomUVIndex, *(PropertyAggregate->Name.ToString()));
		}
		Params.Add(TEXT("get_material_customized_u_vs"), MoveTemp(GetMaterialCustomizedUVS));
		
		#define SET_PARAM_RETURN_FLOAT(ParamName, Value) Params.Add(TEXT(ParamName), FString::Printf(TEXT(TAB "return %.5f"), Value))
		SET_PARAM_RETURN_FLOAT("get_material_emissive_for_cs", 0.f);
		SET_PARAM_RETURN_FLOAT("get_material_translucency_directional_lighting_intensity", Material->GetTranslucencyDirectionalLightingIntensity());
		SET_PARAM_RETURN_FLOAT("get_material_translucent_shadow_density_scale", Material->GetTranslucentShadowDensityScale());
		SET_PARAM_RETURN_FLOAT("get_material_translucent_self_shadow_density_scale", Material->GetTranslucentSelfShadowDensityScale());
		SET_PARAM_RETURN_FLOAT("get_material_translucent_self_shadow_second_density_scale", Material->GetTranslucentSelfShadowSecondDensityScale());
		SET_PARAM_RETURN_FLOAT("get_material_translucent_self_shadow_second_opacity", Material->GetTranslucentSelfShadowSecondOpacity());
		SET_PARAM_RETURN_FLOAT("get_material_translucent_backscattering_exponent", Material->GetTranslucentBackscatteringExponent());
		SET_PARAM_RETURN_FLOAT("get_material_opacity_mask_clip_value", Material->GetOpacityMaskClipValue());

		FLinearColor Extinction = Material->GetTranslucentMultipleScatteringExtinction();
		Params.Add(TEXT("get_material_translucent_multiple_scattering_extinction"), FString::Printf(TEXT(TAB "return MaterialFloat3(%.5f, %.5f, %.5f)"), Extinction.R, Extinction.G, Extinction.B));

		Params.Add(TEXT("get_material_world_position_offset_raw"), WorldPositionOffsetHLSL);
		Params.Add(TEXT("get_material_previous_world_position_offset_raw"), PreviousWorldPositionOffsetHLSL);
	
		FString EvaluateMaterialDeclaration;
		EvaluateMaterialDeclaration.Append(TEXT("void EvaluateVertexMaterialAttributes(in out FMaterialVertexParameters Parameters)\n{\n"));
		for (int32 CustomUVIndex = 0; CustomUVIndex < ModuleStatistics.NumPixelTexCoords; CustomUVIndex++)
		{
			EvaluateMaterialDeclaration.Appendf(TEXT(TAB "Parameters.MaterialAttributes.CustomizedUV%d = Parameters.TexCoords[%d].xy;\n"), CustomUVIndex, CustomUVIndex);
		}
		EvaluateMaterialDeclaration.Append(TEXT("\n}\n"));
		Params.Add(TEXT("evaluate_material_attributes"), MoveTemp(EvaluateMaterialDeclaration));

		FString UniformMaterialExpressions;
		if (Substrate::IsSubstrateEnabled())
		{
			// Adde default Substrate functions
			UniformMaterialExpressions.Append(TEXTVIEW(
				"// Substrate: HiddenMaterialAssetConversion\n"
				"#if TEMPLATE_USES_SUBSTRATE\n"
				"void FSubstratePixelHeader::PreUpdateAllBSDFWithBottomUpOperatorVisit(float3 V) {}\n"
				"void FSubstratePixelHeader::UpdateAllBSDFsOperatorCoverageTransmittance(FSubstrateIntegrationSettings Settings, float3 V)\n"
				"{\n"
				"#if SUBSTRATE_COMPILER_SUPPORTS_STRUCT_FORWARD_DECLARATION\n"
				"\tSubstrateTree.UpdateSingleBSDFOperatorCoverageTransmittance(this, 0, Settings, V);\n"
				"#else\n"
				"\tUpdateSingleBSDFOperatorCoverageTransmittance(SubstrateTree, this, 0, Settings, V);\n"
				"#endif\n"
				"}\n"
				"void FSubstratePixelHeader::UpdateAllOperatorsCoverageTransmittance() {}\n"
				"void FSubstratePixelHeader::UpdateAllBSDFWithBottomUpOperatorVisit() {}\n"
				"#endif // TEMPLATE_USES_SUBSTRATE\n"
				"\n")
			);
		}
		Params.Add(TEXT("uniform_material_expressions"), MoveTemp(UniformMaterialExpressions));
		Params.Add(TEXT("user_scene_texture_remap"), UE::MaterialTranslatorUtils::GenerateUserSceneTextureRemapHLSLDefines(Module->GetCompilationOutput()));
	}
	
	// Generates the definitions of all the custom HLSL functions in the module and puts the resulting string
	// into the "custom_functions" source template parameter.
	void GenerateCustomFunctionsHLSL(TMap<FString, FString>& Params)
	{
		Printer.Tabs = 0;

		for (const MIR::FFunctionHLSL* Function : Module->GetFunctionHLSLs())
		{
			// Print the user-specified defines
			for (const MIR::FFunctionHLSLDefine& Define : Function->Defines)
			{
				Printer << TEXTVIEW("#ifndef ") << Define.Name << NewLine;
				Printer << TEXTVIEW("\t#define ") << Define.Name << TEXTVIEW(" ") << Define.Value << NewLine;
				Printer << TEXTVIEW("#endif") << NewLine;
			}

			// Print the user-specified include directives
			for (FStringView Include : Function->Includes)
			{
				Printer << TEXTVIEW("#include \"") << Include << TEXTVIEW("\"") << NewLine;
			}

			// Write the custom function signature, for example "C5_MyCustomNode".
			// - 'C' is only a "namespace" for custom functions
			// - '5' is a unique id used to disambiguate distinct custom functions with the same name.
			Printer << LowerType(Function->ReturnType) << TEXTVIEW(" C") << Function->UniqueId << TEXT("_") << Function->Name << BeginArgs << TEXT("FMaterialPixelParameters Parameters");
			
			// Write the parameter declarations
			for (uint16 i = 0; i < Function->NumParameters; ++i)
			{
				Printer << TEXTVIEW(", ");

				// Print the io keyword
				Printer << (i < Function->NumInputOnlyParams ? TEXTVIEW("")
						:  i < Function->NumInputAndOutputParams ? TEXTVIEW("inout ")
						: TEXTVIEW("out "));

				// Type and name
				Printer << LowerType(Function->Parameters[i].Type) << TEXTVIEW(" ") << *Function->Parameters[i].Name.ToString();
			}

			Printer << EndArgs << NewLine << OpenBrace;
			
			// If the function does not contain a "return" keyword, add one.
			bool bContainsReturn = Function->Code.Contains(TEXT("return"));
			if (!bContainsReturn)
			{
				Printer << TEXT("return") << NewLine;
			}
			
			// Write the function code
			Printer << Function->Code << NewLine;

			if (!bContainsReturn)
			{
				Printer << TEXT(";");
			}

			Printer << NewLine << CloseBrace << TEXTVIEW("\n\n");
		}

		Params.Add(TEXT("custom_functions"), MoveTemp(Printer.Buffer));
	}

	void GetShaderCompilerEnvironment(FShaderCompilerEnvironment& OutEnvironment)
	{
		const FMaterialCompilationOutput& CompilationOutput = Module->GetCompilationOutput();
		EShaderPlatform ShaderPlatform = Module->GetShaderPlatform();

		OutEnvironment.TargetPlatform = TargetPlatform;
		OutEnvironment.SetDefine(TEXT("ENABLE_NEW_HLSL_GENERATOR"), 1);
		OutEnvironment.SetDefine(TEXT("MATERIAL_ATMOSPHERIC_FOG"), false);
		OutEnvironment.SetDefine(TEXT("MATERIAL_SKY_ATMOSPHERE"), false);
		OutEnvironment.SetDefine(TEXT("INTERPOLATE_VERTEX_COLOR"), false);
		OutEnvironment.SetDefine(TEXT("NEEDS_PARTICLE_COLOR"), false);
		OutEnvironment.SetDefine(TEXT("NEEDS_PARTICLE_LOCAL_TO_WORLD"), false);
		OutEnvironment.SetDefine(TEXT("NEEDS_PARTICLE_WORLD_TO_LOCAL"), false);
		OutEnvironment.SetDefine(TEXT("NEEDS_PER_INSTANCE_RANDOM_PS"), false);
		OutEnvironment.SetDefine(TEXT("USES_EYE_ADAPTATION"), false);
		OutEnvironment.SetDefine(TEXT("USES_PER_INSTANCE_CUSTOM_DATA"), false);
		OutEnvironment.SetDefine(TEXT("USES_PER_INSTANCE_FADE_AMOUNT"), false);
		OutEnvironment.SetDefine(TEXT("USES_TRANSFORM_VECTOR"), false);
		OutEnvironment.SetDefine(TEXT("WANT_PIXEL_DEPTH_OFFSET"), CompilationOutput.bUsesPixelDepthOffset);
		OutEnvironment.SetDefineAndCompileArgument(TEXT("USES_WORLD_POSITION_OFFSET"), (bool)CompilationOutput.bUsesWorldPositionOffset);
		OutEnvironment.SetDefineAndCompileArgument(TEXT("USES_DISPLACEMENT"), false);
		OutEnvironment.SetDefine(TEXT("USES_EMISSIVE_COLOR"), false);
		OutEnvironment.SetDefine(TEXT("USES_DISTORTION"), Material->IsDistorted());
		OutEnvironment.SetDefine(TEXT("MATERIAL_ENABLE_TRANSLUCENCY_FOGGING"), Material->ShouldApplyFogging());
		OutEnvironment.SetDefine(TEXT("MATERIAL_ENABLE_TRANSLUCENCY_CLOUD_FOGGING"), Material->ShouldApplyCloudFogging());
		OutEnvironment.SetDefine(TEXT("MATERIAL_IS_SKY"), Material->IsSky());
		OutEnvironment.SetDefine(TEXT("MATERIAL_COMPUTE_FOG_PER_PIXEL"), Material->ComputeFogPerPixel());
		OutEnvironment.SetDefine(TEXT("MATERIAL_FULLY_ROUGH"), false);
		OutEnvironment.SetDefine(TEXT("MATERIAL_USES_ANISOTROPY"), false);
		OutEnvironment.SetDefine(TEXT("MATERIAL_NEURAL_POST_PROCESS"), (CompilationOutput.bUsedWithNeuralNetworks || Material->IsUsedWithNeuralNetworks()) && Material->IsPostProcessMaterial());
		OutEnvironment.SetDefine(TEXT("NUM_VIRTUALTEXTURE_SAMPLES"), CompilationOutput.UniformExpressionSet.GetVTStacks().Num());
		OutEnvironment.SetDefine(TEXT("NUM_VIRTUALTEXTURE_FEEDBACK_REQUESTS"), CompilationOutput.NumVirtualTextureFeedbackRequests);
		OutEnvironment.SetDefine(TEXT("MATERIAL_VIRTUALTEXTURE_FEEDBACK"), CompilationOutput.NumVirtualTextureFeedbackRequests > 0);
		OutEnvironment.SetDefine(TEXT("IS_MATERIAL_SHADER"), true);
		OutEnvironment.SetDefine(TEXT("VIRTUAL_TEXTURE_OUTPUT"), CompilationOutput.bHasRuntimeVirtualTextureOutputNode != 0);

		uint32 DynamicParametersMask = Module->GetStatistics().DynamicParticleParameterMask;
		if (DynamicParametersMask)
		{
			OutEnvironment.SetDefine(TEXT("USE_DYNAMIC_PARAMETERS"), 1);
			OutEnvironment.SetDefine(TEXT("DYNAMIC_PARAMETERS_MASK"), DynamicParametersMask);
		}

		// Set all defines that are defined by the module.
		// Any conditional exemption via material properties, such as 'Material->IsUsedWithInstancedStaticMeshes()', are handled during the material IR analysis.
		for (const FName& EnvironmentDefine : Module->GetEnvironmentDefines())
		{
			OutEnvironment.SetDefine(EnvironmentDefine, true);
		}

		FMaterialShadingModelField ShadingModels = Module->GetCompiledShadingModels();
		ensure(ShadingModels.IsValid());

		// Logic from FHLSLMaterialTranslator::TranslateMaterial
		bool bOpacityPropertyIsUsed = Module->IsMaterialPropertyUsed(MP_Opacity);

		bool bUsesCurvature = Module->GetFeatureLevel() == ERHIFeatureLevel::ES3_1 &&
			((ShadingModels.HasShadingModel(MSM_SubsurfaceProfile) && Module->IsMaterialPropertyUsed(MP_CustomData0))
			|| (ShadingModels.HasShadingModel(MSM_Eye) && bOpacityPropertyIsUsed));

		int32 NumActiveShadingModels = 0;
		if (ShadingModels.IsLit())
		{
			// This is to have platforms use the simple single layer water shading similar to mobile: no dynamic lights, only sun and sky, no distortion, no colored transmittance on background, no custom depth read.
			const bool bSingleLayerWaterUsesSimpleShading = FDataDrivenShaderPlatformInfo::GetWaterUsesSimpleForwardShading(ShaderPlatform) && IsForwardShadingEnabled(ShaderPlatform);

			for (int32 i = 0; i < MSM_NUM; ++i)
			{
				EMaterialShadingModel Model = (EMaterialShadingModel)i;
				if (Model == MSM_Strata || !ShadingModels.HasShadingModel(Model))
				{
					continue;
				}

				if (Model == MSM_SingleLayerWater && !FDataDrivenShaderPlatformInfo::GetRequiresDisableForwardLocalLights(ShaderPlatform))
				{
					continue;
				}

				if (Model == MSM_SingleLayerWater && bSingleLayerWaterUsesSimpleShading)
				{
					// Value must match SINGLE_LAYER_WATER_SHADING_QUALITY_MOBILE_WITH_DEPTH_TEXTURE in SingleLayerWaterCommon.ush!
					OutEnvironment.SetDefine(TEXT("SINGLE_LAYER_WATER_SHADING_QUALITY"), true);
				}

				OutEnvironment.SetDefine(GetShadingModelParameterName(Model), true);
				NumActiveShadingModels += 1;
			}

			if (ShadingModels.HasShadingModel(MSM_SubsurfaceProfile) && bUsesCurvature)
			{
				OutEnvironment.SetDefine(TEXT("MATERIAL_SUBSURFACE_PROFILE_USE_CURVATURE"), true);
			}

			if (ShadingModels.HasShadingModel(MSM_Eye) && bUsesCurvature)
			{
				OutEnvironment.SetDefine(TEXT("MATERIAL_SHADINGMODEL_EYE_USE_CURVATURE"), true);
			}

			if (ShadingModels.HasShadingModel(MSM_SingleLayerWater) && FDataDrivenShaderPlatformInfo::GetRequiresDisableForwardLocalLights(ShaderPlatform))
			{
				OutEnvironment.SetDefine(TEXT("DISABLE_FORWARD_LOCAL_LIGHTS"), true);
			}

			const bool bIsWaterDistanceFieldShadowEnabled = IsWaterDistanceFieldShadowEnabled(ShaderPlatform);
			const bool bIsWaterVSMFilteringEnabled = IsWaterVirtualShadowMapFilteringEnabled(ShaderPlatform);
			if (ShadingModels.HasShadingModel(MSM_SingleLayerWater) && (bIsWaterDistanceFieldShadowEnabled || bIsWaterVSMFilteringEnabled))
			{
				OutEnvironment.SetDefine(TEXT("SINGLE_LAYER_WATER_SEPARATED_MAIN_LIGHT"), TEXT("1"));
			}
		}
		else
		{
			// Unlit shading model can only exist by itself
			OutEnvironment.SetDefine(GetShadingModelParameterName(MSM_Unlit), true);
			NumActiveShadingModels += 1;
		}

		if (NumActiveShadingModels == 1)
		{
			OutEnvironment.SetDefine(TEXT("MATERIAL_SINGLE_SHADINGMODEL"), true);
		}
		else if (!ensure(NumActiveShadingModels > 0))
		{
			UE_LOG(LogMaterial, Warning, TEXT("Unknown material shading model(s). Setting to MSM_DefaultLit"));
			OutEnvironment.SetDefine(GetShadingModelParameterName(MSM_DefaultLit), true);
		}

		OutEnvironment.SetDefine(TEXT("MATERIAL_LWC_ENABLED"), UE::MaterialTranslatorUtils::IsLWCEnabled() ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("WSVECTOR_IS_TILEOFFSET"), true);
		OutEnvironment.SetDefine(TEXT("WSVECTOR_IS_DOUBLEFLOAT"), false);

		if (Material->GetMaterialDomain() == MD_Volume)
		{
			TArray<const UMaterialExpressionVolumetricAdvancedMaterialOutput*> VolumetricAdvancedExpressions;
			Material->GetMaterialInterface()->GetMaterial()->GetAllExpressionsOfType(VolumetricAdvancedExpressions);
			if (VolumetricAdvancedExpressions.Num() > 0)
			{
				if (VolumetricAdvancedExpressions.Num() > 1)
				{
					UE_LOG(LogMaterial, Fatal, TEXT("Only a single UMaterialExpressionVolumetricAdvancedMaterialOutput node is supported."));
				}

				const UMaterialExpressionVolumetricAdvancedMaterialOutput* VolumetricAdvancedNode = VolumetricAdvancedExpressions[0];
				const TCHAR* Param = VolumetricAdvancedNode->GetEvaluatePhaseOncePerSample() ? TEXT("MATERIAL_VOLUMETRIC_ADVANCED_PHASE_PERSAMPLE") : TEXT("MATERIAL_VOLUMETRIC_ADVANCED_PHASE_PERPIXEL");
				OutEnvironment.SetDefine(Param, true);

				OutEnvironment.SetDefine(TEXT("MATERIAL_VOLUMETRIC_ADVANCED"), true);
				OutEnvironment.SetDefine(TEXT("MATERIAL_VOLUMETRIC_ADVANCED_GRAYSCALE_MATERIAL"), VolumetricAdvancedNode->bGrayScaleMaterial);
				OutEnvironment.SetDefine(TEXT("MATERIAL_VOLUMETRIC_ADVANCED_RAYMARCH_VOLUME_SHADOW"), VolumetricAdvancedNode->bRayMarchVolumeShadow);
				OutEnvironment.SetDefine(TEXT("MATERIAL_VOLUMETRIC_ADVANCED_CLAMP_MULTISCATTERING_CONTRIBUTION"), VolumetricAdvancedNode->bClampMultiScatteringContribution);
				OutEnvironment.SetDefine(TEXT("MATERIAL_VOLUMETRIC_ADVANCED_MULTISCATTERING_OCTAVE_COUNT"), VolumetricAdvancedNode->GetMultiScatteringApproximationOctaveCount());
				OutEnvironment.SetDefine(TEXT("MATERIAL_VOLUMETRIC_ADVANCED_CONSERVATIVE_DENSITY"), VolumetricAdvancedNode->ConservativeDensity.IsConnected());
				OutEnvironment.SetDefine(TEXT("MATERIAL_VOLUMETRIC_ADVANCED_OVERRIDE_AMBIENT_OCCLUSION"), Material->HasAmbientOcclusionConnected());
				OutEnvironment.SetDefine(TEXT("MATERIAL_VOLUMETRIC_ADVANCED_GROUND_CONTRIBUTION"), VolumetricAdvancedNode->bGroundContribution);
			}
		}

		const bool bIsSubstrate = Substrate::IsSubstrateEnabled();

		OutEnvironment.SetDefine(TEXT("MATERIAL_IS_SUBSTRATE"), bIsSubstrate);
		OutEnvironment.SetDefine(TEXT("DUAL_SOURCE_COLOR_BLENDING_ENABLED"), false);
		OutEnvironment.SetDefine(TEXT("TEXTURE_SAMPLE_DEBUG"), false);

		if (bIsSubstrate)
		{
			OutEnvironment.SetDefine(TEXT("SUBSTRATE_USE_FULLYSIMPLIFIED_MATERIAL"), false);
			OutEnvironment.SetDefine(TEXT("SUBSTRATE_CLAMPED_CLOSURE_COUNT"), 1);
		}

		for (int32 VTStackIndex = 0; VTStackIndex < CompilationOutput.UniformExpressionSet.GetVTStacks().Num(); ++VTStackIndex)
		{
			const FMaterialVirtualTextureStack& VTStack = CompilationOutput.UniformExpressionSet.GetVTStack(VTStackIndex);

			// Setup page table defines to map each VT stack to either 1 or 2 page table textures, depending on how many layers it uses
			FString PageTableValue = FString::Printf(TEXT("Material.VirtualTexturePageTable0_%d"), VTStackIndex);
			OutEnvironment.SetDefine(*FString::Printf(TEXT("VIRTUALTEXTURE_PAGETABLE_%d"), VTStackIndex), *PageTableValue);

			// Setup page table uniform defines.
			FString PageTableUniformValue = FString::Printf(TEXT("Material.VTPackedPageTableUniform[%d*2], Material.VTPackedPageTableUniform[%d*2+1]"), VTStackIndex, VTStackIndex);
			OutEnvironment.SetDefine(*FString::Printf(TEXT("VIRTUALTEXTURE_PAGETABLE_UNIFORM_%d"), VTStackIndex), *PageTableUniformValue);
		}

		TConstArrayView<UMaterialParameterCollection*> ParameterCollections = Module->GetParameterCollections();
		for (int32 CollectionIndex = 0; CollectionIndex < ParameterCollections.Num(); CollectionIndex++)
		{
			// Add uniform buffer declarations for any parameter collections referenced
			static_assert(MaxNumParameterCollectionsPerMaterial == 2);
			static const TCHAR* CollectionNames[MaxNumParameterCollectionsPerMaterial] =
			{
				TEXT("MaterialCollection0"),
				TEXT("MaterialCollection1"),
			};

			// Check that the parameter collection loaded successfully.
			UMaterialParameterCollection* ParameterCollection = ParameterCollections[CollectionIndex];
			if (!ParameterCollection)
			{
				UE_LOG(LogMaterial, Warning, TEXT("Null parameter collection found in environment defines while translating material."));
				continue;
			}

			// Ensure PostLoad is called so the uniform buffers are created in case the parameter collection was loaded async 
			ParameterCollection->ConditionalPostLoad();

			// Check that the parameter collection uniform buffer structure is valid
			if (!ParameterCollection->HasValidUniformBufferStruct())
			{
				UE_LOG(LogMaterial, Warning, TEXT("Invalid parameter collection uniform buffer struct found in environment defines while translating material."));
				continue;
			}

			// This can potentially become an issue for MaterialCollection Uniform Buffers if they ever get non-numeric resources (eg Textures), as
			// OutEnvironment.ResourceTableMap has a map by name, and the N ParameterCollection Uniform Buffers ALL are names "MaterialCollection"
			// (and the hlsl cbuffers are named MaterialCollection0, etc, so the names don't match the layout)
			FShaderUniformBufferParameter::ModifyCompilationEnvironment(CollectionNames[CollectionIndex], ParameterCollection->GetUniformBufferStruct(), Module->GetShaderPlatform(), OutEnvironment);
		}
	}
};

void FMaterialIRToHLSLTranslation::Run(TMap<FString, FString>& OutParameters, FShaderCompilerEnvironment& OutEnvironment)
{
	OutParameters.Empty();

	FPrivate Private{ *this };
	Private.GeneratePixelAttributesHLSL();
	Private.GenerateVertexStageHLSL();
	Private.GenerateNonVertexStageHLSL(MIR::Stage_Pixel);
	Private.GenerateNonVertexStageHLSL(MIR::Stage_Compute);
	Private.GenerateTemplateStringParameters(OutParameters);
	Private.GenerateCustomFunctionsHLSL(OutParameters);
	Private.GetShaderCompilerEnvironment(OutEnvironment);
}

#undef TAB
#endif // #if WITH_EDITOR
