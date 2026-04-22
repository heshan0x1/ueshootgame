// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialIRDebug.h"
#include "Materials/MaterialIRModule.h"
#include "Materials/MaterialIR.h"
#include "Materials/MaterialIRTypes.h"
#include "Materials/MaterialIRInternal.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialExternalCodeRegistry.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Engine/Texture.h"

#if WITH_EDITOR

static TAutoConsoleVariable<bool> CVarDumpMaterialIRUseGraph_EnableNext(
	TEXT("r.Material.Translator.DumpUseGraphOpts.EnableSuccessors"),
	false,
	TEXT("Whether the Material Module IR 'Uses' graph should also display 'Instruction Next' edges."),
	ECVF_RenderThreadSafe);

namespace MIR
{

struct FDebugDumpIRUseGraphState
{
	FString Out;
	TSet<const FValue*> Crawled;
	TArray<const FValue*> ValueStack;

	void DumpModule(const FMaterialIRModule& Module)
	{
		Out.Appendf(TEXT(
			"digraph G {\n\n"
			"rankdir=LR\n"
			"node [shape=box,fontname=\"Consolas\"]\n"
			"edge [fontname=\"Consolas\"]\n\n"
		));

		for (int32 EntryPointIndex = 0; EntryPointIndex < Module.GetNumEntryPoints(); ++EntryPointIndex)
		{
			const FMaterialIRModule::FEntryPoint& EntryPoint = Module.GetEntryPoint(EntryPointIndex);

			for (const FValue* Output : EntryPoint.Outputs)
			{
				if (Output)
				{
					ValueStack.Push(Output);
				}
			}

			while (!ValueStack.IsEmpty())
			{
				DumpValue(EntryPointIndex, EntryPoint.Stage, ValueStack.Pop());
			}
		}

		Out.Appendf(TEXT("\n}\n"));
	}

	void DumpValue(uint32 EntryPointIndex, MIR::EStage Stage, const FValue* Value)
	{
		const bool bDumpInstructionSequence = CVarDumpMaterialIRUseGraph_EnableNext.GetValueOnAnyThread();

		// Begin the node declaration
		Out.Appendf(TEXT("\"%p\" [label=< <b>%s</b>  (%s) <br/> "),
						Value,
						LexToString(Value->Kind),
						Value->Type ? *Value->Type.GetSpelling() : TEXT("???"));

		DumpValueInfo(Value);

		// End the node declaration
		Out.Append(TEXT(">]\n"));

		const FInstruction* Instr = AsInstruction(Value);
		if (bDumpInstructionSequence && Instr && Instr->Linkage[EntryPointIndex].Next)
		{
			Out.Appendf(TEXT("\"%p\" -> \"%p\" [color=\"red\"]\n"), Instr, Instr->Linkage[EntryPointIndex].Next);
		}

		int32 UseIndex = -1;
		for (const FValue* Use : Value->GetUsesForStage(Stage))
		{
			++UseIndex;

			if (!Use)
			{
				continue;
			}
			
			Out.Appendf(TEXT("\"%p\" -> \"%p\" [label=\""), Value, Use);

			DumpUseInfo(Value, Use, UseIndex);

			Out.Appendf(TEXT("\"]\n"));

			if (!Crawled.Contains(Use))
			{
				Crawled.Add(Use);
				ValueStack.Push(Use);
			}

			if (bDumpInstructionSequence && Instr)
			{
				const FInstruction* UseInstr = AsInstruction(Use);
				if (UseInstr && UseInstr->Linkage[EntryPointIndex].Block != Instr->Linkage[EntryPointIndex].Block)
				{
					Out.Appendf(TEXT("\"%p\" -> \"%p\" [color=\"red\", style=\"dashed\"]\n"), UseInstr, Instr);
				}
			}
		}
	}

	void DumpValueInfo(const FValue* Value)
	{
		if (const FConstant* Constant = Value->As<FConstant>())
		{
			switch (Constant->Type.GetPrimitive().ScalarKind)
			{
				case EScalarKind::Bool:  Out.Append(Constant->Boolean ? TEXT("true") : TEXT("false")); break;
				case EScalarKind::Int:	 Out.Appendf(TEXT("%lld"), Constant->Integer); break;
				case EScalarKind::Float: Out.Appendf(TEXT("%f"), Constant->Float); break;
				case EScalarKind::Double:   Out.Appendf(TEXT("%f"), Constant->Double); break;
				default: UE_MIR_UNREACHABLE();
			}
		}
		else if (const FExternalInput* ExternalInput = Value->As<FExternalInput>())
		{
			Out.Append(LexToString(ExternalInput->Id));
			if (ExternalInput->Id == EExternalInput::DynamicParticleParameterIndex)
			{
				Out.Appendf(TEXT("[%u]"), ExternalInput->UserData);
			}
		}
		else if (const FSetMaterialOutput* SetMaterailOutput = Value->As<FSetMaterialOutput>())
		{
			const FString& PropertyName = (SetMaterailOutput->Property == MP_SubsurfaceColor)
				? TEXT("Subsurface")
				: FMaterialAttributeDefinitionMap::GetAttributeName(SetMaterailOutput->Property);
		
			Out.Append(PropertyName);
		}
		else if (const FSubscript* Subscript = Value->As<FSubscript>())
		{
			if (Subscript->Arg->Type.IsVector())
			{
				static const TCHAR* Suffix[] = { TEXT(".x"), TEXT(".y"), TEXT(".z"), TEXT(".w") };
				check(Subscript->Index < 4); 
				Out.Append(Suffix[Subscript->Index]);
			}
			else
			{
				Out.Appendf(TEXT("Index: %d"), Subscript->Index);
			}
		}
		else if (const FOperator* Operator = Value->As<FOperator>())
		{
			Out.Append(LexToString(Operator->Op));
		}
	}

	void DumpUseInfo(const FValue* Value, const FValue* Use, int32 UseIndex)
	{
		if (const FComposite* Composite = Value->As<FComposite>())
		{
			if (Composite->Type.IsVector())
			{
				check(UseIndex < 4);
				Out.AppendChar(TEXT("xyzw")[UseIndex]);
			}
			else
			{
				Out.AppendInt(UseIndex);
			}
		}
		else if (const FBranch* If = Value->As<FBranch>())
		{
			static const TCHAR* Uses[] = { TEXT("condition"), TEXT("true"), TEXT("false") };
			Out.Append(Uses[UseIndex]);
		}
		else if (const FOperator* Operator = Value->As<FOperator>())
		{
			static const TCHAR* Uses[] = { TEXT("a"), TEXT("b"), TEXT("c") };
			Out.Append(Uses[UseIndex]);
		}
	}
};

void DebugDumpIRUseGraph(const FMaterialIRModule& Module)
{
	FDebugDumpIRUseGraphState State;
	State.DumpModule(Module);

	FString FilePath = FPaths::Combine(FPaths::ProjectSavedDir(), "Materials", TEXT("IRDumpUseGraph.dot"));
	FFileHelper::SaveStringToFile(State.Out, *FilePath);
}

/* Module IR to textual representation dumping */

/**
 * Returns whether given instruction kind has a dynamic number of arguments, such as the
 * Operator instruction which can have one, two or three arguments.
 */
static bool InstrHasVariableArgCount(MIR::EValueKind Kind)
{
	return Kind == MIR::VK_Operator;
}

// Helper struct to wrap the state used during IR to text dumping.
struct FDebugDumpIRState
{
	// The module we are printing the IR for.
	const FMaterialIRModule* Module{};

	// Output string containing the generated result.
	FString Out{}; 

	// String used for temporary operations. Clear before use.
	FString Temp{};

	// Maps values to an incrementing id. Used to give values a "name" for future referencing (e.g. "%6")
	TMap<const FValue*, uint32> ValueToIdMap{};

	// Counter used to assign an id to encountered values.
	uint32 InstrIdCounter{};

	// Array of encountered parameters. Used later on to generate the a recap of all referenced parameters.
	TArray<TPair<uint32, const FValue*>> ReferencedParameters{};
	
	// Array of encountered user HLSL functions.
	TArray<const MIR::FFunctionHLSL*> ReferencedFunctionHLSLs{};

	int32 CurrentEntryPointIndex{};

	// Stage we're currently emitting to.
	MIR::EStage CurrentStage{};

	// Prints a block of instructions to `Out`. Indentation indicates how many levels of indentation to put
	// to the left of printed instruction.
	void AppendBlock(const FBlock& Block, int32 Indentation)
	{
		for (FInstruction* Instr = Block.Instructions; Instr; Instr = Instr->GetNext(CurrentEntryPointIndex))
		{
			// Format the left column (e.g. "%4 = ") string if this instruction is referenceable.
			Temp.Empty();
			if (Instr->Kind != VK_SetMaterialOutput)
			{
				Temp.Appendf(TEXT("%%%llu = "), ReferenceInstruction(Instr));
			}

			// Print indentation, then Temp aligned to the right.
			AppendLeftColumn(Indentation, Temp);

			// Print the kind of the instruction (the opcode, e.g. "Operator")
			Out.Append(LexToString(Instr->Kind));

			// Begin printing the arguments (used values)
			Out.Append(TEXT(" ("));

			bool bAddComma = false;
			TConstArrayView<FValue*> Uses = Instr->GetUsesForStage(CurrentStage);
			for (int32 UseIndex = 0; UseIndex < Uses.Num(); ++UseIndex)
			{
				const FValue* Use = Uses[UseIndex];
				if (!Use && InstrHasVariableArgCount(Instr->Kind))
				{
					continue;
				}

				if (bAddComma)
				{
					Out.Append(TEXT(", "));
				}
				bAddComma = true;
				
				if (!Use)
				{
					Out.Append(TEXT("null"));
					continue;
				}

				// First the type...
				Out.Appendf(TEXT("%s "), *Use->Type.GetSpelling());

				// If this use is in a block different from current's, dump the block in "{}" first.
				const FBlock* UseBlock = Instr->GetTargetBlockForUse(CurrentEntryPointIndex, UseIndex);
				if (UseBlock != Instr->GetBlock(CurrentEntryPointIndex) && UseBlock->Instructions)
				{
					Out.Append(TEXT("{\n"));
					AppendBlock(*UseBlock, Indentation + 1);

					AppendLeftColumn(Indentation, TEXT(""));
					Out.Append(TEXT("} "));
				}
				
				// Finally, reference the used value (this will print "%x" if it's an
				// instruction, or inline its information otherwise, like in constants).
				AppendValueReference(Use);
			}

			Out.Append(TEXT(")"));

			// Dump the instruction properties.
			AppendInstructionProperties(Instr);

			Out.Append(TEXT("\n"));
		}
	}

	// Appends extra information regarding the instruction.
	void AppendInstructionProperties(const FInstruction* Instr)
	{
		if (const FSetMaterialOutput* SetMaterialOutput = Instr->As<FSetMaterialOutput>())
		{
			Out.Appendf(TEXT(" \"%s\""), *FMaterialAttributeDefinitionMap::GetAttributeName(SetMaterialOutput->Property));
		}
		else if (const FOperator* Operator = Instr->As<FOperator>())
		{
			Out.Appendf(TEXT(" \"%s\""), LexToString(Operator->Op));
		}
		else if (const FSubscript* Subscript = Instr->As<FSubscript>())
		{
			if (Subscript->Arg->Type.IsVector())
			{
				check(Subscript->Index < 4);
				Out.Appendf(TEXT(" .%c"), TEXT("XYZW")[Subscript->Index]);
			}
			else
			{
				Out.Appendf(TEXT(" Index=%d"), Subscript->Index);
			}
		}
		else if (const FTextureRead* TextureRead = Instr->As<FTextureRead>())
		{
			Out.Appendf(TEXT(" Mode=\"%s\""), MIR::LexToString(TextureRead->Mode));
			Out.Appendf(TEXT(" SamplerSourceMode=\"%s\""), *StaticEnum<ESamplerSourceMode>()->GetDisplayNameTextByValue(TextureRead->SamplerSourceMode).ToString());
			Out.Appendf(TEXT(" SamplerType=\"%s\""), *StaticEnum<EMaterialSamplerType>()->GetDisplayNameTextByValue(TextureRead->SamplerType).ToString());
		}
		else if (const FPreshaderParameter* PreshaderParameter = Instr->As<FPreshaderParameter>())
		{
			Out.Appendf(TEXT(" TextureIndex=%d"), PreshaderParameter->TextureIndex);
			Out.Appendf(TEXT(" PreshaderOffset=%u"), PreshaderParameter->Analysis_PreshaderOffset);
		}
		else if (const FInlineHLSL* InlineHLSL = Instr->As<FInlineHLSL>())
		{
			if (InlineHLSL->HasFlags(EValueFlags::HasDynamicHLSLCode))
			{
				Out.Appendf(TEXT(" \"%.*s\""), InlineHLSL->Code.Len, InlineHLSL->Code.Ptr);
			}
			else
			{
				Out.Appendf(TEXT(" \"%s\""), *InlineHLSL->ExternalCodeDeclaration->Definition);
			}
		}
		else if (const FHardwarePartialDerivative* Derivative = Instr->As<FHardwarePartialDerivative>())
		{
			Out.Append(Derivative->Axis == MIR::EDerivativeAxis::X ? TEXT(" \"ddx\"") : TEXT(" \"ddy\""));
		}
		else if (const FCall* Call = Instr->As<FCall>())
		{
			if (Call->Function->Kind == MIR::FFunctionKind::HLSL)
			{
				ReferencedFunctionHLSLs.AddUnique(static_cast<const MIR::FFunctionHLSL*>(Call->Function));
				Out.Appendf(TEXT(" FunctionHLSL=\"%s\""), Call->Function->Name.GetData());
			}
		}
		else if (const FCallParameterOutput* CallOutput = Instr->As<FCallParameterOutput>())
		{
			Out.Appendf(TEXT(" Output=\"%s\""), *CallOutput->Call->As<FCall>()->Function->GetOutputParameter(CallOutput->Index).Name.ToString());
		}
	}

	// Appends a reference to the specified value. This will look like "%x" if Value is an
	// instruction, otherwise it will inline information regarding the value.
	void AppendValueReference(const FValue* Value)
	{
		if (ValueToIdMap.Contains(Value))
		{
			Out.Appendf(TEXT("%%%llu"), ValueToIdMap[Value]);
			return;
		}

		if (Value->As<FPoison>())
		{
			Out.Append(TEXT("Poison"));
		}
		else if (const FConstant* Constant = Value->As<FConstant>())
		{
			if (Constant->Type.IsBoolScalar())
			{
				Out.Append(Constant->Boolean ? TEXT("true") : TEXT("false"));
			}
			else if (Constant->Type.IsInteger())
			{
				Out.Appendf(TEXT("%lld"), Constant->Integer);
			}
			else if (Constant->Type.IsFloat())
			{
				Out.Appendf(TEXT("%.5ff"), Constant->Float);
			}
			else if (Constant->Type.IsDouble())
			{
				Out.Appendf(TEXT("%.8f"), Constant->Double);
			}
			else
			{
				UE_MIR_UNREACHABLE();
			}
		}
		else if (const FExternalInput* ExternalInput = Value->As<FExternalInput>())
		{
			Out.Appendf(TEXT("[ExternalInput \"%s\"]"), LexToString(Value->As<FExternalInput>()->Id));
		}
		else if (const FTextureObject* TextureObject = Value->As<FTextureObject>())
		{
			Out.Appendf(TEXT("[TextureObject #%d SamplerType=\"%s\"]"), TextureObject->Analysis_UniformParameterIndex, *StaticEnum<EMaterialSamplerType>()->GetDisplayNameTextByValue(TextureObject->SamplerType).ToString());
			ReferencedParameters.AddUnique({ TextureObject->Analysis_UniformParameterIndex, TextureObject });
		}
		else if (const FUniformParameter* UniformParameter = Value->As<FUniformParameter>())
		{
			FName ParameterName = Module->GetParameterInfo(UniformParameter->ParameterIdInModule).Name;
			Out.Appendf(TEXT("[Parameter #%d \"%s\"]"), UniformParameter->Analysis_UniformParameterIndex, *ParameterName.ToString());
			ReferencedParameters.AddUnique({ UniformParameter->Analysis_UniformParameterIndex, UniformParameter });
		}
		else
		{
			Out.Appendf(TEXT("[%s]"), LexToString(Value->Kind));
		}
	}

	// Gets the instruction reference.
	uint32 ReferenceInstruction(const FInstruction* Instr)
	{
		uint32 Id;
		if (!Internal::Find(ValueToIdMap, static_cast<const FValue*>(Instr), Id))
		{
			Id = InstrIdCounter++;
			ValueToIdMap.Add(Instr, Id);
		}
		return Id;
	}
	
	void AppendLeftColumn(int32 Indentation, FStringView LeftColumn)
	{
		for (int32 i = 0; i < Indentation; ++i)
		{
			Out.Append("        ");
		}
		
		// Put some padding so that all '=' are aligned to the right.
		for (int32 i = 0, Spaces = 8 - LeftColumn.Len(); i < Spaces; ++i)
		{
			Out.Append(TEXT(" "));
		}

		Out += LeftColumn;
	}

	// Prints the parameter recap section.
	void DumpReferencedParameters()
	{
		if (ReferencedParameters.IsEmpty())
		{
			return;
		}

		// Dump the list of referenced parameters.
		ReferencedParameters.Sort([](const auto& Param1, const auto& Param2)
		{
			return Param1.Key < Param2.Key;
		});

		Out.Append(TEXT("\n; Referenced material parameters\n"));
		for (const auto& ParamPair : ReferencedParameters)
		{
			const FValue* Value = ParamPair.Value;
			if (const FUniformParameter* Param = Value->As<FUniformParameter>())
			{
				FName ParameterName = Module->GetParameterInfo(Param->ParameterIdInModule).Name;
				EMaterialParameterType ParameterType = Module->GetParameterMetadata(Param->ParameterIdInModule).Value.Type;
				Out.Appendf(TEXT("#%d = Name=\"%s\" Type=\"%s\"\n"),
					Param->Analysis_UniformParameterIndex,
					*ParameterName.ToString(),
					*MaterialParameterTypeToString(ParameterType));
			}
			else if (const FTextureObject* TextureObject = Value->As<FTextureObject>())
			{
				Out.Appendf(TEXT("#%d = Name=\"%s\" Type=\"%s\"\n"),
					TextureObject->Analysis_UniformParameterIndex,
					*TextureObject->Texture->GetName(),
					TEXT("Texture"));
			}
		}
	}

	void DumpFunctionHLSLs()
	{
		if (ReferencedFunctionHLSLs.IsEmpty())
		{
			return;
		}

		Out.Append(TEXT("\n; Referenced user HLSL functions\n"));

		for (const MIR::FFunctionHLSL* Function : ReferencedFunctionHLSLs)
		{
			Out.Appendf(TEXT("FunctionHLSL Name=\"C%d_%s\" ReturnType=\"%s\"\n"), Function->UniqueId, Function->Name.GetData(), *Function->ReturnType.GetSpelling());
			for (int i = 0; i < Function->NumParameters; ++i)
			{
				const TCHAR* Keyword = (i < Function->NumInputOnlyParams) ? TEXT("In")
					: (i < Function->NumInputAndOutputParams) ? TEXT("InOut")
					: TEXT("Out");
				
				Out.Appendf(TEXT("\tParam %s Name=\"%s\" Type=\"%s\"\n"), Keyword, *Function->Parameters[i].Name.ToString(), *Function->Parameters[i].Type.GetSpelling());
			}
		}
	}
};

FString DebugDumpIR(FStringView MaterialName, const FMaterialIRModule& Module)
{
	FDebugDumpIRState State{};
	State.Module = &Module;
	State.Out.Append(TEXT("; Material IR module dump.\n"));
	State.Out.Appendf(TEXT(";    Material: %s\n"), MaterialName.GetData());

	// Dump the IR instructions in the root block.
	static const TCHAR* Header = TEXT("; Material \"%s\" translated IR dump\n\n");
	for (int32 EntryPointIndex = 0; EntryPointIndex < Module.GetNumEntryPoints(); ++EntryPointIndex)
	{
		const FMaterialIRModule::FEntryPoint& EntryPoint = Module.GetEntryPoint(EntryPointIndex);
		State.Out.Appendf(TEXT("\n; Entry Point %d \"%s\" (stage \"%s\")\n"), EntryPointIndex, EntryPoint.Name.GetData(), MIR::LexToString(EntryPoint.Stage));

		State.CurrentEntryPointIndex = EntryPointIndex;
		State.CurrentStage = EntryPoint.Stage;
		State.AppendBlock(EntryPoint.RootBlock, 0);
	}

	// Print referenced material parameters recap if any
	State.DumpReferencedParameters();
	State.DumpFunctionHLSLs();
	return State.Out;
}

} // namespace MIR

#endif // #if WITH_EDITOR
