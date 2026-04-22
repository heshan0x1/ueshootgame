// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialIRModuleBuilder.h"

#if WITH_EDITOR

#include "Materials/MaterialIRValueAnalyzer.h"
#include "Materials/MaterialIRModule.h"
#include "Materials/MaterialIRDebug.h"
#include "Materials/MaterialIR.h"
#include "Materials/MaterialIRTypes.h"
#include "Materials/MaterialIREmitter.h"
#include "Materials/MaterialIRInternal.h"
#include "Materials/MaterialAggregate.h"
#include "Materials/MaterialAttributeDefinitionMap.h"
#include "Materials/MaterialExpressionAbsorptionMediumMaterialOutput.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionNamedReroute.h"
#include "Materials/MaterialExpressionSingleLayerWaterMaterialOutput.h"
#include "Materials/MaterialExpressionThinTranslucentMaterialOutput.h"
#include "Materials/MaterialSharedPrivate.h"
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "MaterialExpressionIO.h"
#include "MaterialShared.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialInsights.h"
#include "Math/Color.h"
#include "Engine/Texture.h"
#include "Misc/FileHelper.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderUtils.h"

static TAutoConsoleVariable<int32> CVarMaterialIRDebugDumpLevel(
	TEXT("r.Material.Translator.DebugDump"),
	0,
	TEXT("Whether the material translator should dump debug information about the translated module IR.\n")
	TEXT("0 (Default): No debug dump generated.\n")
	TEXT("1: Dump the material IR instructions to readable a human readable textual representation (to '{SavedDir}/Materials/IRDump.txt').\n")
	TEXT("2: Everything above, plus dump the 'Uses' graph in Graphviz Dot syntax (to '{SavedDir}/Materials/IRDumpUseGraph.dot').\n"),
	ECVF_RenderThreadSafe);

struct FAnalysisContext
{
	UMaterialExpressionMaterialFunctionCall* Call{};
	TSet<UMaterialExpression*> BuiltExpressions{};
	TArray<UMaterialExpression*> ExpressionStack{};
	TMap<const FExpressionInput*, MIR::FValue*> InputValues;
	TMap<const FExpressionOutput*, MIR::FValue*> OutputValues;

	MIR::FValue* GetInputValue(const FExpressionInput* Input)
	{
		MIR::FValue** Value = InputValues.Find(Input);
		return Value ? *Value : nullptr;
	}

	void SetInputValue(const FExpressionInput* Input, MIR::FValue* Value)
	{
		InputValues.Add(Input, Value);
	}

	MIR::FValue* GetOutputValue(const FExpressionOutput* Output)
	{
		MIR::FValue** Value = OutputValues.Find(Output);
		return Value ? *Value : nullptr;
	}
	
	void SetOutputValue(const FExpressionOutput* Output, MIR::FValue* Value)
	{
		OutputValues.Add(Output, Value);
	}
};

struct FMaterialIRModuleBuilderImpl
{
	FMaterialIRModuleBuilder* Builder;
	FMaterialIRModule* Module;
	MIR::FEmitter Emitter;
	TArray<FAnalysisContext> AnalysisContextStack;
	FMaterialIRValueAnalyzer ValueAnalyzer;
	MIR::FValue* DefaultMaterialAggregate;
	FColorMaterialInput PreviewInput;

	void Step_Initialize(FMaterialIRModuleBuilder* InBuilder, FMaterialIRModule* InModule)
	{
		this->Builder = InBuilder;
		this->Module = InModule;

		// Setup the Builder implementation
		ValueAnalyzer.Setup(Builder->Material, Module, &Module->CompilationOutput,  Builder->TargetInsights);

		// Empty the module and set it up
		Module->Empty();
		Module->ShaderPlatform = Builder->ShaderPlatform;
		Module->TargetPlatform = Builder->TargetPlatform;
		Module->FeatureLevel = Builder->FeatureLevel;
		Module->QualityLevel = Builder->QualityLevel;
		Module->BlendMode = Builder->BlendMode;
		
		// Declare a entry point to evaluate the vertex stage.
		Module->AddEntryPoint(TEXTVIEW("VertexStage"), MIR::Stage_Vertex, 1);

		// Declare the entry points to evaluate both the pixel and compute stages.
		int32 Num = UMaterialAggregate::GetMaterialAttributesProperties().Num();
		Module->AddEntryPoint(TEXTVIEW("PixelStage"), MIR::Stage_Pixel, Num);
		Module->AddEntryPoint(TEXTVIEW("ComputeStage"), MIR::Stage_Compute, Num);

		// Setup the emitter and initialize it
		Emitter.BuilderImpl = this;
		Emitter.Material = Builder->Material;
		Emitter.Module = Module;
		Emitter.StaticParameterSet = &Builder->StaticParameters;
		Emitter.Initialize();

		// Create an IR value to hold the material attributes aggregate default
		DefaultMaterialAggregate = Emitter.Aggregate(UMaterialAggregate::GetMaterialAttributes());

		// Push the root analysis context
		AnalysisContextStack.Emplace();

		// Set the preview input expression
		PreviewInput.Expression = Builder->PreviewExpression;
	}

	void Step_PushRootExpressionDependencies()
	{
		//If we are processing from the Material Attributes output node, push the dependency here.
		if (Builder->Material->bUseMaterialAttributes)
		{
			FMaterialInputDescription Input;
			GetExpressionInputDescription(MP_MaterialAttributes, Input);
			PushDependency(Input.Input->Expression);
		}
		else
		{
			for (EMaterialProperty Property : UMaterialAggregate::GetMaterialAttributesProperties())
			{
				// Read the material input associated to this property
				FMaterialInputDescription Input;
				GetExpressionInputDescription(Property, Input);

				// Push the connected expression to this material attribute input as a dependency, if any.
				PushDependency(Input.Input->Expression);
			}
		}
	}

	bool Step_BuildMaterialExpressionsToIRGraph()
	{
		while (true)
		{
			FMemMark MemMark { FMemStack::Get() };
			FAnalysisContext& Context = AnalysisContextStack.Last();

			if (!Context.ExpressionStack.IsEmpty())
			{
				// Some expression is on the expression stack of this context. Analyze it. This will
				// have the effect of either building the expression or pushing its other expression
				// dependencies onto the stack.
				BuildTopMaterialExpression();
			}
			else if (Context.Call)
			{
				// There are no more expressions to analyze on the stack, this analysis context is complete.
				// Context.Call isn't null so this context is for a function call, which has now been fully analyzed.
				// Pop the callee context from the stack and resume analyzing the parent context (the caller).
				PopFunctionCall();
			}
			else
			{
				// No other expressions on the stack to evaluate, nor this is a function
				// call context but the root context. Nothing left to do so simply quit.
				break;
			}
		}

		return Module->IsValid();
	}

	void BuildTopMaterialExpression()
	{
		FAnalysisContext& CurrContext = AnalysisContextStack.Last();
		Emitter.Expression = CurrContext.ExpressionStack.Last();

		// If expression is clean, nothing to be done.
		if (CurrContext.BuiltExpressions.Contains(Emitter.Expression))
		{
			CurrContext.ExpressionStack.Pop(EAllowShrinking::No);
			return;
		}

		// Push to the expression stack all dependencies that still need to be analyzed.
		for (FExpressionInputIterator It{ Emitter.Expression }; It; ++It)
		{
			PushDependency(It.Input->Expression);
		}

		// Named reroute usage nodes should make add their declaration as dependency, as they
		// do not have any direct input (they simply forward to the declaration input).
		if (auto NamedReroute = Cast<UMaterialExpressionNamedRerouteUsage>(Emitter.Expression))
		{
			if (NamedReroute->IsDeclarationValid())
			{
				PushDependency(NamedReroute->Declaration.Get());
			}
		}

		// If on top of the stack there's a different expression, we have a dependency to analyze first.
		if (CurrContext.ExpressionStack.Last() != Emitter.Expression)
		{
			return;
		}

		// Take the top expression out of the stack as ready for analysis. Also mark it as built.
		CurrContext.ExpressionStack.Pop();
		CurrContext.BuiltExpressions.Add(Emitter.Expression);

		// Flow the value into this expression's inputs from their connected outputs.
		for (FExpressionInputIterator It{ Emitter.Expression}; It; ++It)
		{
			FlowValueThroughConnection(It.Input);
		}
		
		// Usage reroute nodes should forward the value coming in from their declaration input (since
		// they don't have an input of their own).
		if (auto NamedReroute = Cast<UMaterialExpressionNamedRerouteUsage>(Emitter.Expression))
		{
			if (NamedReroute->IsDeclarationValid())
			{
				FlowValueThroughConnection(&NamedReroute->Declaration->Input);
			}
		}

		if (auto Call = Cast<UMaterialExpressionMaterialFunctionCall>(Emitter.Expression))
		{
			// Function calls are handled internally as they manipulate the analysis context stack.
			PushFunctionCall(Call);
		}
		else
		{
			// Invoke the expression build function. This will perform semantic analysis, error reporting and
			// emit IR values for its outputs (which will flow into connected expressions inputs).
			Emitter.Expression->Build(Emitter);

			// Populate the insight information about this expression pins.
			AddExpressionConnectionInsights(Emitter.Expression);
		}
	}
	
	// Pushes an expression dependency on this context expression stack.
	void PushDependency(UMaterialExpression* Expression)
	{
		FAnalysisContext& CurrContext = AnalysisContextStack.Last();

		// Ignore disconnected inputs and connected expressions already built.
		if (Expression && !CurrContext.BuiltExpressions.Contains(Expression))
		{
			CurrContext.ExpressionStack.Push(Expression);
		}
	}

	// Flows the value from connected output to specified input.
	void FlowValueThroughConnection(FExpressionInput* Input)
	{
		FAnalysisContext& CurrContext = AnalysisContextStack.Last();

		// Fetch the value flowing through connected output.
		FExpressionOutput* ConnectedOutput = Input->GetConnectedOutput();
		if (ConnectedOutput)
		{
			MIR::FValue** ValuePtr = CurrContext.OutputValues.Find(ConnectedOutput);
			if (ValuePtr)
			{
				// ...and flow it into this input.
				CurrContext.InputValues.Add(Input, *ValuePtr);
			}
		}
	}

	void PushFunctionCall(UMaterialExpressionMaterialFunctionCall* Call)
	{
		MIR::TTemporaryArray<MIR::FValue*> CallInputValues{ Call->FunctionInputs.Num() };

		// Make sure each required function input is connected and has a value.  If so, cache the values flowing into this
		// funcion call inside the auxiliary value array.  If the input is optional (bUsePreviewValueAsDefault set), we can
		// ignore the missing value, and the downstream UMaterialExpressionFunctionInput build function will return the default.
		for (int i = 0; i < Call->FunctionInputs.Num(); ++i)
		{
			FFunctionExpressionInput& FunctionInput = Call->FunctionInputs[i];
			MIR::FType Type = MIR::FType::FromMaterialValueType(FunctionInput.ExpressionInput->GetInputValueType(0));

			if (FunctionInput.ExpressionInput->bUsePreviewValueAsDefault)
			{
				MIR::FValue* Value = Emitter.TryInput(Call->GetInput(i));
				CallInputValues[i] = Value ? Emitter.Cast(Value, Type) : nullptr;
			}
			else
			{
				MIR::FValue* Value = Emitter.Input(Call->GetInput(i));
				CallInputValues[i] = Emitter.Cast(Value, Type);
			}
		}

		// If some error occurred (e.g. some function input wasn't linked in) early out.
		if (Emitter.CurrentExpressionHasErrors())
		{
			return;
		}

		// Push a new analysis context on the stack dedicated to this function call.
		AnalysisContextStack.Emplace();
		FAnalysisContext& ParentContext = AnalysisContextStack[AnalysisContextStack.Num() - 2];
		FAnalysisContext& NewContext = AnalysisContextStack[AnalysisContextStack.Num() - 1];

		// Set the function call. When the expressions stack in this new context is empty, this
		// will be used to wire all values flowing inside the function outputs to the function call outputs.
		NewContext.Call = Call;

		// Forward values flowing into call inputs to called function inputs
		for (int i = 0; i < Call->FunctionInputs.Num(); ++i)
		{
			if (CallInputValues[i])
			{
				FFunctionExpressionInput& FunctionInput = Call->FunctionInputs[i];

				// Bind the value flowing into the function call input to the function input
				// expression (inside the function) in the new context.
				NewContext.SetOutputValue(FunctionInput.ExpressionInput->GetOutput(0), CallInputValues[i]);

				// Mark the function input as built.
				NewContext.BuiltExpressions.Add(FunctionInput.ExpressionInput.Get());
			}
		}

		// Finally push the function outputs to the expression evaluation stack in the new context.
		for (FFunctionExpressionOutput& FunctionOutput : Call->FunctionOutputs)
		{
			NewContext.ExpressionStack.Push(FunctionOutput.ExpressionOutput.Get());
		}
	}

	void PopFunctionCall()
	{
		// Pull the values flowing into the function outputs out of the current
		// context and flow them into the Call outputs in the parent context so that
		// analysis can continue from the call expression.
		FAnalysisContext& ParentContext = AnalysisContextStack[AnalysisContextStack.Num() - 2];
		FAnalysisContext& CurrContext = AnalysisContextStack[AnalysisContextStack.Num() - 1];
		UMaterialExpressionMaterialFunctionCall* Call = CurrContext.Call;

		for (int i = 0; i < Call->FunctionOutputs.Num(); ++i)
		{
			FFunctionExpressionOutput& FunctionOutput = Call->FunctionOutputs[i];

			// Get the value flowing into the function output inside the function in the current context.
			MIR::FValue* Value = Emitter.Input(FunctionOutput.ExpressionOutput->GetInput(0));

			// And flow it to the relative function *call* output in the parent context.
			ParentContext.SetOutputValue(Call->GetOutput(i), Value);
		}

		// Finally pop this context (the function call) to return to the caller.
		AnalysisContextStack.Pop();

		// Populate the insight information about this expression pins.
		AddExpressionConnectionInsights(Call);
	}

	bool Step_EmitSetMaterialPropertyInstructions()
	{
		// First, if the material is flagged to use the material attributes aggregate, read its value now
		// so that we can extract its individual attributes later.
		MIR::FValue* MaterialAttributesValue = nullptr;
		if (Builder->Material->bUseMaterialAttributes)
		{
			FMaterialInputDescription InputDesc;
			GetExpressionInputDescription(MP_MaterialAttributes, InputDesc);

			// Fetch the value from the material attributes input.
			FlowValueThroughConnection(InputDesc.Input);
			MaterialAttributesValue = MIR::Internal::FetchValueFromExpressionInput(this, InputDesc.Input);
			if (!MaterialAttributesValue)
			{
				MaterialAttributesValue = DefaultMaterialAggregate;
			}

			// Make sure a valid value is present and it is of the correct type.
			check(!MaterialAttributesValue->IsPoison() && MaterialAttributesValue->Type.AsAggregate() == UMaterialAggregate::GetMaterialAttributes());
		}

		for (EMaterialProperty Property : UMaterialAggregate::GetMaterialAttributesProperties())
		{
			// Get the input description of this material property (input, type, default value, etc).
			FMaterialInputDescription InputDesc;
			GetExpressionInputDescription(Property, InputDesc);

			// This holds the value being set to this property.
			MIR::FValue* PropertyValue = nullptr;

			// If the material attributes value is valid, extract this property attribute from the material attributes aggregate value
			// and manually flow it into the this property material expression input pin.
			if (MaterialAttributesValue)
			{
				PropertyValue = Emitter.Subscript(MaterialAttributesValue, UMaterialAggregate::MaterialPropertyToAttributeIndex(Property));
			}
			else
			{
				// Otherwise grab the value from the individual attribute pin.
				FlowValueThroughConnection(InputDesc.Input);
				PropertyValue = MIR::Internal::FetchValueFromExpressionInput(this, InputDesc.Input);
			}

			if (PropertyValue)
			{
				// If this property is the emissive color and we're previewing the material, apply gamma correction to the previewed value.
				if (Property == MP_EmissiveColor && PreviewInput.IsConnected())
				{
					MIR::FValue* Zero = Emitter.ConstantZero(MIR::EScalarKind::Float);

					// Get preview expression back into gamma corrected space, as DrawTile does not do this adjustment.
					PropertyValue = Emitter.Pow(Emitter.Max(PropertyValue, Zero), Emitter.ConstantFloat(1.0f / 2.2f));

					// Preview should display scalars as red, so if this is a scalar, create a vector padded with zeroes
					if (PropertyValue->Type.IsScalar())
					{
						PropertyValue = Emitter.Vector3(PropertyValue, Zero, Zero);
					}
				}
				else
				{
					// If a value is flowing in through the connection, cast it to this material attribute type and assign it.
					// Special case for shading model, because UE::Shader::EValueType doesn't include a native shading model type, and uses int instead.
					MIR::FType OutputArgType = Property == MP_ShadingModel ? MIR::FType::MakeShadingModel() : MIR::FType::FromShaderType(InputDesc.Type);
					PropertyValue = Emitter.Cast(PropertyValue, OutputArgType);
				}
			}
			else if (InputDesc.bUseConstant)
			{
				// If input is marked to use constant, assign this output to the specified constant value.
				PropertyValue = Emitter.ConstantFromShaderValue(InputDesc.ConstantValue);
			}
			else
			{
				// Otherwise, fallback to assigning this material output to its default value.
				PropertyValue = Emitter.Subscript(DefaultMaterialAggregate, UMaterialAggregate::MaterialPropertyToAttributeIndex(Property));
			}
			
			// Quit if some error occurred in the operations above.
			if (!Module->IsValid())
			{
				return false;
			}

			// The value being set to this material output is now valid.
			check(PropertyValue);

			// Add support for lerp to selection color for PC development builds.
			if (Property == MP_EmissiveColor &&
				Builder->Material->MaterialDomain != MD_Volume &&
				UE::MaterialTranslatorUtils::IsDevelopmentFeatureEnabled(NAME_SelectionColor, Module->GetShaderPlatform(), Builder->Material))
			{
				FMaterialParameterMetadata ParameterMetadata{ FMaterialParameterValue(FLinearColor::Transparent) };
				MIR::FValue* SelectionColor = Emitter.Parameter(NAME_SelectionColor, ParameterMetadata);
				PropertyValue = Emitter.Lerp(PropertyValue, Emitter.Swizzle(SelectionColor, MIR::FSwizzleMask::XYZ()), Emitter.Subscript(SelectionColor, 3));
			}

			// Set this property value onto the module
			Module->SetPropertyValue(Property, PropertyValue);

			// Emit the SetMaterialProperty instruction
			Emitter.SetMaterialOutput(Property, PropertyValue);
			
			// Finally, push this connection insight
			PushConnectionInsight(Builder->Material, (int32)Property, InputDesc.Input->Expression, InputDesc.Input->OutputIndex, PropertyValue->Type);
		}

		return Module->IsValid();
	}

	bool GetExpressionInputDescription(EMaterialProperty Property, FMaterialInputDescription& Input)
	{
		if (PreviewInput.IsConnected() && Property == MP_EmissiveColor)
		{
			Input.Type = UE::Shader::EValueType::Float3;
			Input.Input = &PreviewInput;
			return true;
		}
		else
		{
			bool bResult = Builder->Material->GetExpressionInputDescription(Property, Input);
			/**
			* MP_SubsurfaceColor is currently hacked in the old translator to float4, 
			* but we rely on default types (i.e.float3) for default values in the material editor output.
			* 
			* This hack resolves the default value to use float4 rather than float3 until we can 
			* implement a permanent float4 alternative method in the new translator 
			* (i.e. work towards deprecating the MP_SubsurfaceColor hacks scattered throughout UE).
			*/
			if (Property == MP_SubsurfaceColor)
			{
				Input.Type = UE::Shader::EValueType::Float4;
				Input.ConstantValue = UE::Shader::FValue(Input.ConstantValue.AsLinearColor());
			}

			return bResult;
		}
	}

	bool Step_AnalyzeIRGraph()
	{
		TArray<MIR::FValue*> ValueStack{};
		TSet<MIR::FValue*> VisitedValues{};

		// Analyze the nodes in each entry point.
		for (int32 EntryPointIndex = 0; EntryPointIndex < Module->GetNumEntryPoints(); ++EntryPointIndex)
		{
			// Reset bookkeeping to process new output subgraph
			ValueStack.Empty(ValueStack.Max());
			VisitedValues.Empty(Module->Values.Num());

			FMaterialIRModule::FEntryPoint& EntryPoint = Module->GetEntryPoint(EntryPointIndex);

			// Push this SetOutput instruction the value stack for processing.
			for (MIR::FValue* Output : EntryPoint.Outputs)
			{
				if (Output)
				{
					ValueStack.Push(Output);
				}
			}

			// Process until the value stack is empty.
			while (!ValueStack.IsEmpty())
			{
				MIR::FValue* Value = ValueStack.Last();
				
				// Module building should have interrupted before if poison values were generated.
				check(!Value->IsPoison());

				// If this instruction has already been analyzed for this entry point, nothing else is left to do for it. Continue.
				if (VisitedValues.Contains(Value))
				{
					ValueStack.Pop();
					continue;
				}

				// Before analyzing this value, make sure all used values are analyzed first.
				for (MIR::FValue* Use : Value->GetUsesForStage(EntryPoint.Stage))
				{
					if (Use && !VisitedValues.Contains(Use))
					{
						ValueStack.Push(Use);
					}
				}

				// If any other value has been pushed to the stack, it means we have a dependency to analyze first.
				if (ValueStack.Last() != Value)
				{
					continue;
				}

				// All dependencies of this value has been analyzed, we can proceed analyzing this value now.
				ValueStack.Pop();

				// Go through each use instruction and increment its counter of users (this instruction).
				for (MIR::FValue* Use : Value->GetUsesForStage(EntryPoint.Stage))
				{
					// If this used value is an instruction, update its counter of users (in current stage).
					if (MIR::FInstruction* UseInstr = MIR::AsInstruction(Use))
					{
						UseInstr->Linkage[EntryPointIndex].NumUsers += 1;
					}
				}

				// If this is the first time this value is analyzed in any entry point, let the analyzer process it.
				// Note that individual value processing is independent from the stage it runs on so we can perform it only once.
				if (!Value->HasFlags(MIR::EValueFlags::AnalyzedInAnyStage))
				{
					Value->SetFlags(MIR::EValueFlags::AnalyzedInAnyStage);

					// Flow the graph properties downstream from the value's uses into this value.
					for (MIR::FValue* Use : Value->GetUses())
					{
						if (Use)
						{
							Value->GraphProperties |= Use->GraphProperties;
						}
					}

					// Allocate the entry points linkage information for this instruction.
					if (MIR::FInstruction* Instr = MIR::AsInstruction(Value))
					{
						Instr->Linkage = Module->AllocateArray<MIR::FInstructionLinkage>(Module->GetNumEntryPoints());
						MIR::ZeroArray(Instr->Linkage);
					}

					// Then analyze the instruction based on its kind.
					ValueAnalyzer.Analyze(Value);
				}

				// Analyze this instruction in this entry point's stage if it's the first time it's encountered.
				MIR::EValueFlags StageFlag = MIR::EValueFlags(1 << (int32)EntryPoint.Stage);
				if (!Value->HasFlags(StageFlag))
				{
					Value->SetFlags(StageFlag);
					ValueAnalyzer.AnalyzeInStage(Value, EntryPoint.Stage);
				}

				// Mark the used instruction as analyzed for this entry point.
				VisitedValues.Add(Value);
			}
		}

		return Module->IsValid();
	}

	void Step_LinkInstructions()
	{
		TArray<MIR::FInstruction*> InstructionStack{};

		for (int32 EntryPointIndex = 0; EntryPointIndex < Module->GetNumEntryPoints(); ++EntryPointIndex)
		{
			// This function walks the instruction graph and puts each instruction into the inner most possible block.
			InstructionStack.Empty(InstructionStack.Max());

			FMaterialIRModule::FEntryPoint& Procedure = Module->GetEntryPoint(EntryPointIndex);

			// Push all entry point final outputs onto the instruction stack to begin.
			// Note: the first output on the stack will be the first to be evaluated in the entry point root block.
			for (MIR::FValue* Output : Procedure.Outputs)
			{
				if (MIR::FInstruction* Instr = MIR::AsInstruction(Output))
				{
					Instr->Linkage[EntryPointIndex].Block = &Procedure.RootBlock;
					InstructionStack.Push(Instr);
				}
			}

			while (!InstructionStack.IsEmpty())
			{
				MIR::FInstruction* Instr = InstructionStack.Pop();
				MIR::FBlock* InstrBlock = Instr->Linkage[EntryPointIndex].Block;

				// Push the instruction to its block in reverse order (push front)
				Instr->Linkage[EntryPointIndex].Next = InstrBlock->Instructions;
				InstrBlock->Instructions = Instr;

				//
				if (Instr->As<MIR::FNop>())
				{
					continue;
				}

				TConstArrayView<MIR::FValue*> Uses = Instr->GetUsesForStage(Procedure.Stage);
				for (int32 UseIndex = 0; UseIndex < Uses.Num(); ++UseIndex)
				{
					MIR::FInstruction* UseInstr = MIR::AsInstruction(Uses[UseIndex]);

					if (!UseInstr)
					{
						continue;
					}

					// Get the block into which the dependency instruction should go.
					MIR::FBlock* TargetBlock = Instr->GetTargetBlockForUse(EntryPointIndex, UseIndex);

					// Update dependency's block to be a child of current instruction's block.
					if (TargetBlock != InstrBlock)
					{
						TargetBlock->Parent = InstrBlock;
						TargetBlock->Level = InstrBlock->Level + 1;
					}

					// Set the dependency's block to the common block betwen its current block and this one.
					MIR::FInstructionLinkage& UseLinkage = UseInstr->Linkage[EntryPointIndex];

					UseLinkage.Block = UseLinkage.Block
						? UseLinkage.Block->FindCommonParentWith(TargetBlock)
						: TargetBlock;

					// Increase the number of times this dependency instruction has been considered.
					// When all of its users have processed, we can carry on visiting this instruction.
					++UseLinkage.NumProcessedUsers;
					check(UseLinkage.NumProcessedUsers <= UseLinkage.NumUsers);

					// If all dependants have been processed, we can carry the processing from this dependency.
					if (UseLinkage.NumProcessedUsers == UseLinkage.NumUsers)
					{
						InstructionStack.Push(UseInstr);
					}
				}
			}
		}
	}

	// This final step generates all output non-IR-graph data such as setting up the CompilationOutput and the shader environment defines.
	void Step_FinalizeArtifacts()
	{
		ConsolidateEnvironmentDefines();
		AnalyzeBuiltinDefines();
		WriteCompilationOutput();
		GenerateDebugInsights();
	}

	void ConsolidateEnvironmentDefines()
	{
		// Keep defines if a combined condition is met. Otherwise, remove them from the environemnt defines set.
		auto KeepDefineConditionally = [this](FName Name, bool bConditionToKeepDefine) -> void
		{
			if (!bConditionToKeepDefine)
			{
				ValueAnalyzer.EnvironmentDefines.Remove(Name);
			}
		};

		KeepDefineConditionally(TEXT("USES_PER_INSTANCE_CUSTOM_DATA"), ValueAnalyzer.Material->bUsedWithInstancedStaticMeshes);
		KeepDefineConditionally(TEXT("NEEDS_PER_INSTANCE_RANDOM_PS"), ValueAnalyzer.Material->bUsedWithInstancedStaticMeshes);
		KeepDefineConditionally(TEXT("USES_PER_INSTANCE_FADE_AMOUNT"), ValueAnalyzer.Material->bUsedWithInstancedStaticMeshes);

		// Derive additional defines from the final state of the IR analysis
		{
			// Virtual texturing defines
			int32 NumVirtualTextureFeedbackRequests = 0;
			for (const FMaterialIRValueAnalyzer::FVTStackEntry& VTStack : ValueAnalyzer.VTStacks)
			{
				if (VTStack.bGenerateFeedback)
				{
					++NumVirtualTextureFeedbackRequests;
				}
			}
			Module->CompilationOutput.NumVirtualTextureFeedbackRequests = NumVirtualTextureFeedbackRequests;
		}

		// Move final environment defines from analyzer into output module
		Module->EnvironmentDefines = MoveTemp(ValueAnalyzer.EnvironmentDefines);
	}

	void AnalyzeBuiltinDefines()
	{
		// Match various defines against the material configuration
		if (Module->EnvironmentDefines.Contains(TEXT("MIR.SceneDepth")))
		{
			// @todo-jason.hoerner - Support for material instance blend mode overrides needed.
			if (ValueAnalyzer.Material->MaterialDomain != MD_PostProcess && !IsTranslucentBlendMode(ValueAnalyzer.Material->BlendMode))
			{
				Module->AddError(nullptr, TEXT("Only transparent or postprocess materials can read from scene depth."));
			}
		}

		// Remove all environment defines that have the "MIR." prefix as they are not meant to propagate into the set of compiler environment defines.
		TCHAR DefineMIRPrefix[5] = {};
		for (auto Iter = Module->EnvironmentDefines.CreateIterator(); Iter; ++Iter)
		{
			Iter->ToStringTruncate(DefineMIRPrefix, UE_ARRAY_COUNT(DefineMIRPrefix));
			if (FCString::Strncmp(DefineMIRPrefix, TEXT("MIR."), UE_ARRAY_COUNT(DefineMIRPrefix)) == 0)
			{
				Iter.RemoveCurrent();
			}
		}
	}

	void WriteCompilationOutput()
	{
		FMaterialCompilationOutput& CompilationOutput = Module->CompilationOutput;

		CompilationOutput.NumUsedUVScalars = Module->Statistics.NumPixelTexCoords * 2;
		CompilationOutput.UniformExpressionSet.SetParameterCollections(Module->ParameterCollections);

		int32 NumPostProcessInputs = CompilationOutput.GetNumPostProcessInputsUsed();
		if (NumPostProcessInputs > kPostProcessMaterialInputCountMax)
		{
			Module->AddError(nullptr, FString::Printf(TEXT("Maximum Scene Texture post process inputs exceeded (%d > %d), between SceneTexture nodes with PostProcessInputs or UserSceneTexture nodes."), NumPostProcessInputs, kPostProcessMaterialInputCountMax));
		}

		CompilationOutput.bUsesPixelDepthOffset = MaterialPropertyHasNonZeroValue(MP_PixelDepthOffset);
		CompilationOutput.bUsesWorldPositionOffset = MaterialPropertyHasNonZeroValue(MP_WorldPositionOffset);

		// @todo-jason.hoerner - Fill in CompilationOutput.bUsesDisplacement.
		// @todo-jason.hoerner - Custom output support.
		const bool bHasFirstPersonOutput = false;  // (CustomOutputsBitmask & FMaterialAttributeDefinitionMap::GetBitmask(FirstPersonInterpolationAlphaGuid)) != 0;

		CompilationOutput.bModifiesMeshPosition =
			CompilationOutput.bUsesPixelDepthOffset ||
			CompilationOutput.bUsesWorldPositionOffset ||
			CompilationOutput.bUsesDisplacement ||
			bHasFirstPersonOutput;

		// If the material doesn't use expression shading models, or they aren't valid, initialize the shading models to the ones from the material.
		// Logic adapted from FHLSLMaterialTranslator::GetCompiledShadingModels, but done once, rather than for each call.
		if (!ValueAnalyzer.Material->IsShadingModelFromMaterialExpression() || !Module->ShadingModelsFromCompilation.IsValid())
		{
			Module->ShadingModelsFromCompilation = ValueAnalyzer.Material->GetShadingModels();
			UMaterialInterface::FilterOutPlatformShadingModels(Module->GetShaderPlatform(), Module->ShadingModelsFromCompilation);
		}

		// Final validation logic shared between old and new translator.
		TArray<FString> ValidationErrors;
		UE::MaterialTranslatorUtils::FinalCompileValidation(
			ValueAnalyzer.Material,
			CompilationOutput,
			Module->GetCompiledShadingModels(),
			Module->GetBlendMode(),
			Module->IsMaterialPropertyUsed(MP_FrontMaterial),
			Module->GetShaderPlatform(),
			ValidationErrors);

		for (const FString& ValidationError : ValidationErrors)
		{
			Module->AddError(nullptr, *ValidationError);
		}
	}

	void GenerateDebugInsights()
	{
		if (!Builder->Material->MaterialInsight)
		{
			return;
		}

		// Dump the module IR to string and store it inside the material insights.
		Builder->Material->MaterialInsight.Get()->IRString = MIR::DebugDumpIR(Builder->Material->GetFullName(), *Module);
		
		// Dump the requested debugging information
		switch (CVarMaterialIRDebugDumpLevel.GetValueOnGameThread())
		{
			case 2:
			{
				MIR::DebugDumpIRUseGraph(*Module);
				// fallthrough
			}

			case 1:
			{
				// Save the dump to file
				FString FilePath = FPaths::Combine(FPaths::ProjectSavedDir(), "Materials", TEXT("IRDump.txt"));
				FFileHelper::SaveStringToFile(Builder->Material->MaterialInsight.Get()->IRString, *FilePath);
				// fallthrough
			}
		}
	}
	
	/* Auxiliary functions */

	// Adds an expression connection insight to the MaterialInsights instance, if any.
	void AddExpressionConnectionInsights(UMaterialExpression* Expression)
	{
		if (!Builder->TargetInsights)
		{
			return;
		}

		// Update expression inputs insight.
		for (FExpressionInputIterator It{ Expression }; It; ++It)
		{
			if (MIR::FValue* Value = MIR::Internal::FetchValueFromExpressionInput(this, It.Input))
			{
				PushConnectionInsight(Expression, It.Index, It->Expression, It->OutputIndex, Value->Type);
			}
		}
	}
	
	// Adds a connection insight to the MaterialInsights instance, if any.
	void PushConnectionInsight(const UObject* InputObject, int InputIndex, const UMaterialExpression* OutputExpression, int OutputIndex, MIR::FType Type)
	{
		if (!Builder->TargetInsights || Type.IsPoison())
		{
			return;
		}

		FMaterialInsights::FConnectionInsight Insight {
			.InputObject = InputObject,
			.OutputExpression = OutputExpression,
			.InputIndex = InputIndex,
			.OutputIndex = OutputIndex,
			.ValueType = Type.ToValueType(),
		};
		
		Builder->TargetInsights->ConnectionInsights.Push(Insight);
	}

	// Returns whether a material property (e.g. MP_BaseColor) has a value assigned that isn't a constant zero.
	// Used to determine if a property is being used.
	bool MaterialPropertyHasNonZeroValue(EMaterialProperty InProperty)
	{
		return Module->GetPropertyValue(InProperty) && !Module->GetPropertyValue(InProperty)->AreAllExactlyZero();
	}
};

namespace MIR::Internal {

	FValue* FetchValueFromExpressionInput(FMaterialIRModuleBuilderImpl* Builder, const FExpressionInput* Input)
	{
		return Builder->AnalysisContextStack.Last().GetInputValue(Input);
	}

	void BindValueToExpressionInput(FMaterialIRModuleBuilderImpl* Builder, const FExpressionInput* Input, FValue* Value)
	{
		Builder->AnalysisContextStack.Last().SetInputValue(Input, Value);
	}

	void BindValueToExpressionOutput(FMaterialIRModuleBuilderImpl* Builder, const FExpressionOutput* Output, FValue* Value)
	{
		Builder->AnalysisContextStack.Last().SetOutputValue(Output, Value);
	}

} // namespace MIR::Internal

bool FMaterialIRModuleBuilder::Build(FMaterialIRModule* TargetModule)
{
	FMaterialIRModuleBuilderImpl Impl;

	FMemMark MemMark { FMemStack::Get() };

	// Initialize the module to a blank slate, initialize the builder auxiliary data
	// and the emitter for IR values emission.
	Impl.Step_Initialize(this, TargetModule);

	// Identify the material property output pins and push their value-producing
	// expressions onto the analysis context stack to start crawling from them.
	Impl.Step_PushRootExpressionDependencies();

	// Main step. It crawls the expression graph and calls the Build() function on each
	// visited expression in order to emit the IR values that implement that expression
	// semantics. At the end of this step the IR values graph has been built, but is still
	// missing the root SetMaterialProperty instructions.
	if(!Impl.Step_BuildMaterialExpressionsToIRGraph())
	{
		return false;
	}

	// Materials dont have a final "output expression", so this step grabs the values flowing
	// into the material property output pins (if any) and generates SetMaterialProperty instructions
    // handling details such as default values, preview material expression, etc.
	if (!Impl.Step_EmitSetMaterialPropertyInstructions())
	{
		return false;
	}

	// Now that the full IR graph has been produced, starting from the output instructions of each
	// entry point, crawl the IR graph backwards in order to let each value analyze itself. A value
	// is analyzed only after all its dependencies (its uses) have been analyzed first, so that when
	// a value is analyzed it is guaranteed to have all the information to properly analyze itself.
	// In this step is performed semantic analysis, where a value can potentially throw new errors depending
	// on the semantic context it is placed in (for instance, an instruction can be executed only in
	// specific stages will throw an error if it finds itself being executed in an incorrect stage.
	if (!Impl.Step_AnalyzeIRGraph())
	{
		return false;
	}

	// The IR graph has now been been fully produced and is valid. Proceed to link instructions together,
	// placing each instruction into its own parent block. This is done in a way to put instructions in the
	// narrowest possible scope that still puts them in an execution order that will occur after its dependencies
	// have occurred.
	Impl.Step_LinkInstructions();

	// Finally, populate all other non IR-graph artifacts such as the CompilationOutput and the EnvironmentDefines
	// data structures.
	Impl.Step_FinalizeArtifacts();

	return true;
}

#endif // #if WITH_EDITOR
