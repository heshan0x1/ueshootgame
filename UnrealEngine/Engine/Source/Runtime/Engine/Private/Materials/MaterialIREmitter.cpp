// Copyright Epic Games, Inc. All Rights Reserved.

#include "Materials/MaterialIREmitter.h"
#include "Materials/Material.h"
#include "Materials/MaterialIRModule.h"
#include "Materials/MaterialIRModuleBuilder.h"
#include "Materials/MaterialIRInternal.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExternalCodeRegistry.h"
#include "Materials/MaterialAggregate.h"
#include "Shader/ShaderTypes.h"
#include "MaterialShared.h"
#include "MaterialSharedPrivate.h"
#include "MaterialExpressionIO.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "RenderUtils.h"

#if WITH_EDITOR

static TAutoConsoleVariable<bool> CVarMaterialIRDebugBreakOnPoison(
	TEXT("r.Material.Translator.DebugBreakOnPoison"),
	0,
	TEXT("Whether the material translator break in the debugger when hitting a poison value in the IR.\n"),
	ECVF_Default);

namespace MIR
{

// Converts a vector component enum to its string representation ("x", "y", "z", or "w").
const TCHAR* VectorComponentToString(EVectorComponent Component)
{
	static const TCHAR* Strings[] = { TEXT("x"), TEXT("y"), TEXT("z"), TEXT("w") };
	return Strings[(int32)Component];
}

FSwizzleMask::FSwizzleMask(EVectorComponent X)
: NumComponents{ 1 }
{
	Components[0] = X;
}

FSwizzleMask::FSwizzleMask(EVectorComponent X, EVectorComponent Y)
: NumComponents{ 2 }
{
	Components[0] = X;
	Components[1] = Y;
}

FSwizzleMask::FSwizzleMask(EVectorComponent X, EVectorComponent Y, EVectorComponent Z)
: NumComponents{ 3 }
{
	Components[0] = X;
	Components[1] = Y;
	Components[2] = Z;
}

FSwizzleMask::FSwizzleMask(EVectorComponent X, EVectorComponent Y, EVectorComponent Z, EVectorComponent W)
: NumComponents{ 4 }
{
	Components[0] = X;
	Components[1] = Y;
	Components[2] = Z;
	Components[3] = W;
}

FSwizzleMask::FSwizzleMask(bool bMaskX, bool bMaskY, bool bMaskZ, bool bMaskW)
{
	if (bMaskX)
	{
		Append(MIR::EVectorComponent::X);
	}
	if (bMaskY)
	{
		Append(MIR::EVectorComponent::Y);
	}
	if (bMaskZ)
	{
		Append(MIR::EVectorComponent::Z);
	}
	if (bMaskW)
	{
		Append(MIR::EVectorComponent::W);
	}
}

FSwizzleMask FSwizzleMask::XYZ()
{
	return { EVectorComponent::X, EVectorComponent::Y, EVectorComponent::Z };
}

void FSwizzleMask::Append(EVectorComponent Component)
{
	check(NumComponents < 4);
	Components[NumComponents++] = Component;
}

bool FSwizzleMask::IsXYZW() const
{
	return
		NumComponents == 4 &&
		Components[0] == EVectorComponent::X &&
		Components[1] == EVectorComponent::Y &&
		Components[2] == EVectorComponent::Z &&
		Components[3] == EVectorComponent::W
		;
}

struct FEmitter::FPrivate
{
	// Searches the emitter’s value set for an existing FValue matching Prototype, or returns nullptr.
	static FValue* FindValue(FEmitter& Emitter, const FValue* Prototype)
	{
		FValue** Value = Emitter.ValueSet.Find(Prototype);
		return Value ? *Value : nullptr;
	}

	// Allocates zero-initialized memory of given size and alignment in the module’s arena.
	static void* Allocate(FEmitter& Emitter, int32 Size, int32 Alignment)
	{
		return Emitter.Module->Allocator.PushBytes(Size, Alignment);
	}

	// Registers a new value in the module.
	static void PushValueToModule(FEmitter& Emitter, FValue* Value)
	{
		Emitter.Module->Values.Add(Value);
		Emitter.ValueSet.Add(Value);
	}
};

// Creates a copy of specified array using the Module allocator and returns it.
template <typename T>
TConstArrayView<T> MakeArrayCopy(FEmitter& Emitter, TConstArrayView<T> Array)
{
	T* Data = (T*)FEmitter::FPrivate::Allocate(Emitter, sizeof(T) * Array.Num(), alignof(T));
	FMemory::Memcpy(Data, Array.GetData(), sizeof(T) * Array.Num());
	return { Data, Array.Num() };
}

// Creates a “prototype” value of type T with the specified MIR type.
// Emit this value later with a matching EmitPrototype() call, which returns the actual value instance
// after potential deduplication.
template <typename T>
static T MakePrototype(FType InType)
{
	 static_assert(std::is_trivially_destructible_v<T> && std::is_trivially_copy_constructible_v<T> && std::is_trivially_copy_assignable_v<T>,
		"FValues are expected to be trivial types.");

	T Value;
	FMemory::Memzero(Value);
	Value.Kind = T::TypeKind;
	Value.Type = InType;

	return Value;
}

// Allocates and initializes a temporary composite value with the given number of components.
static FComposite* MakeCompositePrototype(FEmitter& Emitter, FType Type, int NumComponents)
{
	// Compute the total size of this composite value.
	int32 SizeInBytes = sizeof(FComposite) + sizeof(FValue*) * NumComponents;

	// Allocate a temporary buffer for it.
	FComposite* Value = (FComposite*)FMemStack::Get().Alloc(SizeInBytes, alignof(FComposite));

	// Zero its memory and set it up
	FMemory::Memzero(Value, SizeInBytes);
	Value->Kind = VK_Composite;
	Value->Type = Type;

	return Value;
}

// Emits a prototype value into the module, deduplicating if an identical value was already emitted.
static FValueRef EmitPrototype(FEmitter& Emitter, const FValue& Prototype)
{
	// Optimization: See if we emitted this value before, and if so, since MIR is SSA, with
	// instructions having being the equivalent of "pure functions" with no side effects,
	// simply return the existing value which holds the already computed result.
	if (FValue* Existing = FEmitter::FPrivate::FindValue(Emitter, &Prototype))
	{
		return Existing;
	}

	// Otherwise, create a new value allocating the necessary memory in the module's arena.
	FValue* Value = (FValue*)FEmitter::FPrivate::Allocate(Emitter, Prototype.GetSizeInBytes(), alignof(FValue));

	// Bitcopy the prototype into the value memory.
	FMemory::Memcpy(Value, &Prototype, Prototype.GetSizeInBytes());

	// Push the value to the module.
	FEmitter::FPrivate::PushValueToModule(Emitter, Value);

	// Verify that value hashing is deterministic.
	checkSlow(Value == FEmitter::FPrivate::FindValue(Emitter, &Prototype));

	return Value;
}

// Finds the expression input index. Although the implementation has O(n) complexity, it is only used for error reporting.
static int32 SlowFindExpressionInputIndex(UMaterialExpression* Expression, const FExpressionInput* InInput)
{
	for (FExpressionInputIterator It{ Expression }; It; ++It)
	{
		if (It.Input == InInput)
		{
			return It.Index;
		}
	}
	return -1;
}

// Finds the expression input name. Although the implementation has O(n) complexity, it is only used for error reporting.
static FName SlowFindInputName(UMaterialExpression* Expression, const FExpressionInput* InInput)
{
	int32 InputIndex = SlowFindExpressionInputIndex(Expression, InInput);
	return InputIndex != INDEX_NONE ? Expression->GetInputName(InputIndex) : FName{};
}

/*------------------------------------- FValueRef --------------------------------------*/

static inline bool IsAnyNotValid()
{
	return false;
}

// Returns whether any of the values is invalid (null or poison).
template <typename... TTail>
static bool IsAnyNotValid(FValueRef Head, TTail... Tail)
{
	return !Head.IsValid() || IsAnyNotValid(Tail...);
}

// Returns whether any of the values is invalid (null or poison).
static inline bool IsAnyNotValid(TConstArrayView<FValueRef> Values)
{
	for (FValueRef Value : Values)
	{
		if (!Value.IsValid())
		{
			return true;
		}
	}
	return false;
}

bool FValueRef::IsValid() const
{
	return Value && !Value->IsPoison();
}

bool FValueRef::IsPoison() const
{
	return Value && Value->IsPoison();
}

FValueRef FValueRef::To(FValue* InValue) const
{
	return { InValue, Input };
}

FValueRef FValueRef::ToPoison() const
{
	return To(FPoison::Get());
}

/*--------------------------------- FFunctionHLSLDesc ---------------------------------*/

bool FFunctionHLSLDesc::PushInputOnlyParameter(FName InName, FType InType)
{
	// You must input-only parameters first, before the others.
	check(!NumInputOutputParams && !NumOutputOnlyParams);

	if (GetNumParameters() == MIR::MaxNumFunctionParameters)
	{
		return false;
	}

	Parameters[NumInputOnlyParams++] = { InName, InType };
	return true;
}
	
bool FFunctionHLSLDesc::PushInputOutputParameter(FName InName, FType InType)
{
	checkf(!NumOutputOnlyParams, TEXT("You must input-output parameters before you push any output-only parameters."));

	if (GetNumParameters() == MIR::MaxNumFunctionParameters)
	{
		return false;
	}

	Parameters[NumInputOnlyParams + NumInputOutputParams] = { InName, InType };
	++NumInputOutputParams;
	return true;
}
	
bool FFunctionHLSLDesc::PushOutputOnlyParameter(FName InName, FType InType)
{
	if (GetNumParameters() == MIR::MaxNumFunctionParameters)
	{
		return false;
	}

	Parameters[NumInputOnlyParams + NumInputOutputParams + NumOutputOnlyParams] = { InName, InType };
	++NumOutputOnlyParams;
	return true;
}

/*----------------------------------- Error handling -----------------------------------*/

void FEmitter::Error(FValueRef Source, FStringView Message)
{
	Source.Input
		? Error(FString::Printf(TEXT("From expression input '%s': %s"), *SlowFindInputName(Expression, Source.Input).ToString(),Message.GetData()))
		: Error(Message);
}

void FEmitter::Error(FStringView Message)
{
	FMaterialIRModule::FError Error;
	Error.Expression = Expression;

	// Add the node type to the error message
	const int32 ChopCount = FCString::Strlen(TEXT("MaterialExpression"));
	const FString& ErrorClassName = Expression->GetClass()->GetName();
	
	Error.Message.Appendf(TEXT("(Node %s) %s"), *ErrorClassName + ChopCount, Message.GetData());
	
	Module->Errors.Push(Error);
	bCurrentExpressionHasErrors = true;
}

/*--------------------------------- Type handling ----------------------------------*/

FType FEmitter::TryGetCommonType(FType A, FType B)
{
	// Trivial case: types are equal
	if (A == B)
	{
		return A;
	}

	// Primitive types can only be constructed from other primitive types.
	TOptional<FPrimitive> PrimitiveA = A.AsPrimitive();
	TOptional<FPrimitive> PrimitiveB = B.AsPrimitive();
	if (!PrimitiveA || !PrimitiveB)
	{
		return FType::MakePoison();
	}
	
	// No common type between row and column vectors
	if ((PrimitiveA->IsRowVector() && PrimitiveB->IsColumnVector()) || (PrimitiveA->IsColumnVector() && PrimitiveB->IsRowVector()))
	{
		return FType::MakePoison();
	}

	// Can't cast a vector to a matrix.
	if ((PrimitiveA->IsRowVector() && PrimitiveB->IsMatrix()) || (PrimitiveA->IsMatrix() && PrimitiveB->IsRowVector()))
	{
		return FType::MakePoison();
	}
	
	// Return the primitive type with the maximum number of rows and columns between the two types.
	EScalarKind ScalarKind = FMath::Max(PrimitiveA->ScalarKind, PrimitiveB->ScalarKind);
	int32 NumRows = FMath::Max(PrimitiveA->NumRows, PrimitiveB->NumRows);
	int32 NumColumns = FMath::Max(PrimitiveA->NumColumns, PrimitiveB->NumColumns);
	return FType::MakePrimitive(ScalarKind, NumRows, NumColumns);
}

FType FEmitter::GetCommonType(FType A, FType B)
{
	if (FType CommonType = TryGetCommonType(A, B))
	{
		return CommonType;
	}

	Errorf(TEXT("No common type between '%s' and '%s'."), *A.GetSpelling(), *B.GetSpelling());
	return FType::MakePoison();
}

FType FEmitter::GetCommonType(TConstArrayView<FValueRef> Values)
{
	check(!Values.IsEmpty() && Values[0]);

	// Find the common type among non-null values
	FType CommonType = Values[0]->Type;
	for (int32 i = 1; i < Values.Num(); ++i)
	{
		if (Values[i].IsPoison())
		{
			return FType::MakePoison();
		}
		if (Values[i])
		{
			CommonType = TryGetCommonType(CommonType, Values[i]->Type);
		}
	}

	// If common type is valid, return it
	if (!CommonType.IsPoison())
	{
		return CommonType;
	}

	// ...otherwise generate an error. This error message prints the input the values
	// come from, if available.

	// Search for the last valid index in the values array, so that we know when to print " and "
	int32 LastIndex = Values.Num() - 1;
	for (; LastIndex >= 0; --LastIndex)
	{
		if (Values[LastIndex])
		{
			break;
		}
	}

	// Whether some value has already been reported (used to print the comma ", ")
	bool bSomeValueAlreadyPrinted = false;

	FString ErrorMsg{ TEXTVIEW("No common type between ") };
	for (int32 i = 0; i < Values.Num(); ++i)
	{
		if (!Values[i])
		{
			continue;
		}

		FValueRef Value = Values[i];

		if (i == LastIndex)
		{
			ErrorMsg.Append(TEXTVIEW(" and "));
		}
		else if (bSomeValueAlreadyPrinted)
		{
			ErrorMsg.Append(TEXTVIEW(", "));
		}

		ErrorMsg.Appendf(TEXT("'%s'"), *Value->Type.GetSpelling());
		
		if (Value.Input)
		{
			ErrorMsg.Appendf(TEXT(" (from input '%s')"), *SlowFindInputName(Expression, Value.Input).ToString());
		}

		bSomeValueAlreadyPrinted = true;
	}

	ErrorMsg.AppendChar('.');
	Error(ErrorMsg);

	return FType::MakePoison();
}

FType FEmitter::GetMaterialAggregateAttributeType(const UMaterialAggregate* Aggregate, int32 AttributeIndex)
{
	check(AttributeIndex >= 0);

	if (AttributeIndex >= Aggregate->Attributes.Num())
	{
		Errorf(TEXT("Invalid attribute index %d for material aggregate '%s'. Index is out of range (Num = %d)."),
			   AttributeIndex, *Aggregate->GetName(), Aggregate->Attributes.Num());
		return FType::MakePoison();
	}

	switch (Aggregate->Attributes[AttributeIndex].Type)
	{
		case EMaterialAggregateAttributeType::Bool1: return FType::MakeVector(EScalarKind::Bool, 1);
		case EMaterialAggregateAttributeType::Bool2: return FType::MakeVector(EScalarKind::Bool, 2);
		case EMaterialAggregateAttributeType::Bool3: return FType::MakeVector(EScalarKind::Bool, 3);
		case EMaterialAggregateAttributeType::Bool4: return FType::MakeVector(EScalarKind::Bool, 4);
		case EMaterialAggregateAttributeType::ShadingModel: return FType::MakeShadingModel();
		case EMaterialAggregateAttributeType::UInt1: return FType::MakeVector(EScalarKind::Int, 1);
		case EMaterialAggregateAttributeType::UInt2: return FType::MakeVector(EScalarKind::Int, 2);
		case EMaterialAggregateAttributeType::UInt3: return FType::MakeVector(EScalarKind::Int, 3);
		case EMaterialAggregateAttributeType::UInt4: return FType::MakeVector(EScalarKind::Int, 4);
		case EMaterialAggregateAttributeType::Float1: return FType::MakeVector(EScalarKind::Float, 1);
		case EMaterialAggregateAttributeType::Float2: return FType::MakeVector(EScalarKind::Float, 2);
		case EMaterialAggregateAttributeType::Float3: return FType::MakeVector(EScalarKind::Float, 3);
		case EMaterialAggregateAttributeType::Float4: return FType::MakeVector(EScalarKind::Float, 4);
		case EMaterialAggregateAttributeType::MaterialAttributes: return FType::MakeAggregate(UMaterialAggregate::GetMaterialAttributes());
		case EMaterialAggregateAttributeType::Aggregate: return FType::MakeAggregate(Aggregate->Attributes[AttributeIndex].Aggregate.Get());
		default: UE_MIR_UNREACHABLE();
	}
}

/*-------------------------------- Input management --------------------------------*/

FValueRef FEmitter::TryInput(const FExpressionInput* InInput)
{
	return FValueRef{ Internal::FetchValueFromExpressionInput(BuilderImpl, InInput), InInput };
}

FValueRef FEmitter::Input(const FExpressionInput* InInput)
{
	FValueRef Value = TryInput(InInput);
	if (!Value)
	{
		Errorf(TEXT("Missing '%s' input value."), *SlowFindInputName(Expression, InInput).ToString());
		return Value.ToPoison();
	}
	return Value;
}

FValueRef FEmitter::InputDefaultBool(const FExpressionInput* Input, bool Default)
{
	FValueRef Value = TryInput(Input);
	return Value ? Value : Value.To(ConstantBool(Default));
}

FValueRef FEmitter::InputDefaultInt(const FExpressionInput* Input, TInteger Default)
{
	FValueRef Value = TryInput(Input);
	return Value ? Value : Value.To(ConstantInt(Default));
}

FValueRef FEmitter::InputDefaultInt2(const FExpressionInput* Input, UE::Math::TIntVector2<TInteger> Default)
{
	FValueRef Value = TryInput(Input);
	return Value ? Value : Value.To(ConstantInt2(Default));
}

FValueRef FEmitter::InputDefaultInt3(const FExpressionInput* Input, UE::Math::TIntVector3<TInteger> Default)
{
	FValueRef Value = TryInput(Input);
	return Value ? Value : Value.To(ConstantInt3(Default));
}

FValueRef FEmitter::InputDefaultInt4(const FExpressionInput* Input, UE::Math::TIntVector4<TInteger> Default)
{
	FValueRef Value = TryInput(Input);
	return Value ? Value : Value.To(ConstantInt4(Default));
}

FValueRef FEmitter::InputDefaultFloat(const FExpressionInput* Input, TFloat Default)
{
	FValueRef Value = TryInput(Input);
	return Value ? Value : Value.To(ConstantFloat(Default));
}

FValueRef FEmitter::InputDefaultFloat2(const FExpressionInput* Input, UE::Math::TVector2<TFloat> Default)
{
	FValueRef Value = TryInput(Input);
	return Value ? Value : Value.To(ConstantFloat2(Default));
}

FValueRef FEmitter::InputDefaultFloat3(const FExpressionInput* Input, UE::Math::TVector<TFloat> Default)
{
	FValueRef Value = TryInput(Input);
	return Value ? Value : Value.To(ConstantFloat3(Default));
}

FValueRef FEmitter::InputDefaultFloat4(const FExpressionInput* Input, UE::Math::TVector4<TFloat> Default)
{
	FValueRef Value = TryInput(Input);
	return Value ? Value : Value.To(ConstantFloat4(Default));
}

FValueRef FEmitter::CheckTypeIsKind(FValueRef Value, ETypeKind Kind)
{
	if (Value.IsValid() && Value->Type.Is(Kind))
	{
		Errorf(Value, TEXT("Expected a '%s' value, got a '%s' instead."), TypeKindToString(Kind), *Value->Type.GetSpelling());
		return Value.ToPoison();
	}
	return Value;
}

FValueRef FEmitter::CheckIsPrimitive(FValueRef Value)
{
	if (Value.IsValid() && !Value->Type.AsPrimitive())
	{
		Errorf(Value, TEXT("Expected a primitive value, got a '%s' instead."), *Value->Type.GetSpelling());
		return Value.ToPoison();
	}
	return Value;
}

FValueRef FEmitter::CheckIsArithmetic(FValueRef Value)
{
	if (Value.IsValid() && !Value->Type.IsArithmetic())
	{
		Errorf(Value, TEXT("Expected an arithmetic value, got a '%s' instead."), *Value->Type.GetSpelling());
		return Value.ToPoison();
	}
	return Value;
}

FValueRef FEmitter::CheckIsBoolean(FValueRef Value)
{
	if (Value.IsValid() && !Value->Type.IsBoolean())
	{
		Errorf(Value, TEXT("Expected a boolean value, got a '%s' instead."), *Value->Type.GetSpelling());
		return Value.ToPoison();
	}
	return Value;
}

FValueRef FEmitter::CheckIsInteger(FValueRef Value)
{
	if (Value.IsValid() && !Value->Type.IsInteger())
	{
		Errorf(Value, TEXT("Expected an integer value, got a '%s' instead."), *Value->Type.GetSpelling());
		return Value.ToPoison();
	}
	return Value;
}

FValueRef FEmitter::CheckIsScalar(FValueRef Value)
{
	if (Value.IsValid() && !Value->Type.IsScalar())
	{
		Errorf(Value, TEXT("Expected a scalar value, got a '%s' instead."), *Value->Type.GetSpelling());
		return Value.ToPoison();
	}
	return Value;
}

FValueRef FEmitter::CheckIsVector(FValueRef Value)
{
	if (Value.IsValid() && !Value->Type.IsVector())
	{
		Errorf(Value, TEXT("Expected a vector value, got a '%s' instead."), *Value->Type.GetSpelling());
		return Value.ToPoison();
	}
	return Value;
}

FValueRef FEmitter::CheckIsScalarOrVector(FValueRef Value)
{
	if (Value.IsValid() && (!Value->Type.AsPrimitive() || Value->Type.GetPrimitive().IsMatrix()))
	{
		Errorf(Value, TEXT("Expected a scalar or vector value, got a '%s' instead."), *Value->Type.GetSpelling());
		return Value.ToPoison();
	}
	return Value;
}

FValueRef FEmitter::CheckIsMatrix(FValueRef Value)
{
	if (Value.IsValid() && (!Value->Type.AsPrimitive() || !Value->Type.GetPrimitive().IsMatrix()))
	{
		Errorf(Value, TEXT("Expected a matrix value, got a '%s' instead."), *Value->Type.GetSpelling());
		return Value.ToPoison();
	}
	return Value;
}

FValueRef FEmitter::CheckIsTexture(FValueRef Value)
{
	if (Value.IsValid() && !Value->Type.IsTexture())
	{
		Errorf(Value, TEXT("Expected a texture value, got a '%s' instead."), *Value->Type.GetSpelling());
		return Value.ToPoison();
	}
	return Value;
}

FValueRef FEmitter::CheckIsAggregate(FValueRef Value, const UMaterialAggregate* Aggregate)
{
	if (Value.IsValid())
	{
		const UMaterialAggregate* ValueAggregate = Value->Type.AsAggregate();
		if (!ValueAggregate)
		{
			Errorf(Value, TEXT("Expected an aggregate value, got a '%s' instead."), *Value->Type.GetSpelling());
			return Value.ToPoison();
		}
		if (Aggregate && ValueAggregate != Aggregate)
		{
			Errorf(Value, TEXT("Expected a value of aggregate type '%s', got a value of aggregate type '%s' instead."), *Aggregate->GetName(), *Value->Type.AsAggregate()->GetName());
			return Value.ToPoison();
		}
	}
	return Value;
}

bool FEmitter::ToConstantBool(FValueRef Value)
{
	if (!Value.IsValid())
	{
		return false;
	}
	const FConstant* Constant = Value->As<FConstant>();
    if (!Constant)
    {
        Errorf(Value, TEXT("Expected a constant bool value, got a non-constant value instead."));
        return false;
    }
	if (Constant->Type != FType::MakeBoolScalar())
    {
        Errorf(Value, TEXT("Expected a constant bool value, got a '%s' instead."), *Constant->Type.GetSpelling());
        return false;
    }
	return Constant->Boolean;
 }

/*-------------------------------- Output management -------------------------------*/

FEmitter& FEmitter::Output(int32 OutputIndex, FValueRef Value)
{
	Output(Expression->GetOutput(OutputIndex), Value);
	return *this;
}

FEmitter& FEmitter::Output(const FExpressionOutput* ExpressionOutput, FValueRef Value)
{
	if (Value)
	{
		Internal::BindValueToExpressionOutput(BuilderImpl, ExpressionOutput, Value);
	}
	return *this;
}

static MIR::FSwizzleMask GetSwizzleMaskFromExpressionOutputMask(const FExpressionOutput& ExpressionOutput)
{
	MIR::FSwizzleMask SwizzleMask;
	if (ExpressionOutput.MaskR)
	{
		SwizzleMask.Append(MIR::EVectorComponent::X);
	}
	if (ExpressionOutput.MaskG)
	{
		SwizzleMask.Append(MIR::EVectorComponent::Y);
	}
	if (ExpressionOutput.MaskB)
	{
		SwizzleMask.Append(MIR::EVectorComponent::Z);
	}
	if (ExpressionOutput.MaskA)
	{
		SwizzleMask.Append(MIR::EVectorComponent::W);
	}
	return SwizzleMask;
}

FEmitter& FEmitter::Outputs(const TConstArrayView<FExpressionOutput>& ExpressionOutputs, FValueRef Value)
{
	for (const FExpressionOutput& CurrentOutput : ExpressionOutputs)
	{
		// Apply component swizzling for each expression output but use unmodified input value if swizzling is redundant (i.e. XYZW vector)
		const MIR::FSwizzleMask SwizzleMask = GetSwizzleMaskFromExpressionOutputMask(CurrentOutput);
		Output(&CurrentOutput, SwizzleMask.IsXYZW() ? Value : Swizzle(Value, SwizzleMask));
	}
	return *this;
}

/*------------------------------- Constants emission -------------------------------*/

FValueRef FEmitter::ConstantFromShaderValue(const UE::Shader::FValue& InValue)
{
	using namespace UE::Shader;

	switch (InValue.Type.ValueType)
	{
		case UE::Shader::EValueType::Int1: return ConstantInt(InValue.AsFloatScalar());
		case UE::Shader::EValueType::Int2: return ConstantInt2({ InValue.Component[0].Int, InValue.Component[1].Int });
		case UE::Shader::EValueType::Int3: return ConstantInt3({ InValue.Component[0].Int, InValue.Component[1].Int, InValue.Component[2].Int });
		case UE::Shader::EValueType::Int4: return ConstantInt4({ InValue.Component[0].Int, InValue.Component[1].Int, InValue.Component[2].Int, InValue.Component[3].Int });
	
		case UE::Shader::EValueType::Float1: return ConstantFloat(InValue.AsFloatScalar());
		case UE::Shader::EValueType::Float2: return ConstantFloat2(UE::Math::TVector2<TFloat>{ InValue.Component[0].Float, InValue.Component[1].Float });
		case UE::Shader::EValueType::Float3: return ConstantFloat3(UE::Math::TVector<TFloat>{ InValue.Component[0].Float, InValue.Component[1].Float, InValue.Component[2].Float });
		case UE::Shader::EValueType::Float4: return ConstantFloat4(UE::Math::TVector4<TFloat>{ InValue.Component[0].Float, InValue.Component[1].Float, InValue.Component[2].Float, InValue.Component[3].Float });

		case UE::Shader::EValueType::Double1: return ConstantDouble(InValue.Component[0].Double);
		case UE::Shader::EValueType::Double2: return Vector2(ConstantDouble(InValue.Component[0].Double), ConstantDouble(InValue.Component[1].Double));
		case UE::Shader::EValueType::Double3: return Vector3(ConstantDouble(InValue.Component[0].Double), ConstantDouble(InValue.Component[1].Double), ConstantDouble(InValue.Component[2].Double));
		case UE::Shader::EValueType::Double4: return Vector4(ConstantDouble(InValue.Component[0].Double), ConstantDouble(InValue.Component[1].Double), ConstantDouble(InValue.Component[2].Double), ConstantDouble(InValue.Component[3].Double));
	}

	UE_MIR_UNREACHABLE();
}

FValueRef FEmitter::ConstantDefault(FType Type)
{
	if (TOptional<FPrimitive> PrimitiveType = Type.AsPrimitive())
	{
		FValueRef Zero = ConstantZero(PrimitiveType->ScalarKind);
		if (PrimitiveType->IsScalar())
		{
			return Zero;
		}
		else
		{
			FComposite* Composite = MakeCompositePrototype(*this, Type, PrimitiveType->NumComponents());
			for (FValue*& Component : Composite->GetMutableComponents())
			{
				Component = Zero;
			}
			return EmitPrototype(*this, *Composite);
		}
	}
	else if (const UMaterialAggregate* TypeAggregate = Type.AsAggregate())
	{
		return Aggregate(TypeAggregate);
	}
	else
	{
		Errorf(TEXT("Type '%s' has no default. Expected primitive or aggregate type."), *Type.GetSpelling());
		return Poison();
	}
}

FValueRef FEmitter::ConstantZero(EScalarKind Kind)
{
	switch (Kind)
	{
		case EScalarKind::Bool: return ConstantFalse();
		case EScalarKind::Int: return ConstantInt(0);
		case EScalarKind::Float: return ConstantFloat(0.0f);
		case EScalarKind::Double: return ConstantDouble(0.0);
		default: UE_MIR_UNREACHABLE();
	}
}

FValueRef FEmitter::ConstantOne(EScalarKind Kind)
{
	switch (Kind)
	{
		case EScalarKind::Bool: return ConstantTrue();
		case EScalarKind::Int: return ConstantInt(1);
		case EScalarKind::Float: return ConstantFloat(1.0f);
		case EScalarKind::Double: return ConstantDouble(1.0);
		default: UE_MIR_UNREACHABLE();
	}
}

FValueRef FEmitter::ConstantScalar(EScalarKind Kind, TDouble Value)
{
	switch (Kind)
	{
		case EScalarKind::Bool: return ConstantBool(Value != 0.0f);
		case EScalarKind::Int: return ConstantInt(TInteger(Value));
		case EScalarKind::Float: return ConstantFloat(TFloat(Value));
		case EScalarKind::Double: return ConstantDouble(Value);
		default: UE_MIR_UNREACHABLE();
	}
}

FValueRef FEmitter::ConstantTrue()
{
	return TrueConstant;
}

FValueRef FEmitter::ConstantFalse()
{
	return FalseConstant;
}

FValueRef FEmitter::ConstantBool(bool InX)
{
	return InX ? ConstantTrue() : ConstantFalse();
}

FValueRef FEmitter::ConstantBool2(bool InX, bool InY)
{
	FValueRef X = ConstantBool(InX);
	FValueRef Y = ConstantBool(InY);
	return Vector2(X, Y);
}

FValueRef FEmitter::ConstantBool3(bool InX, bool InY, bool InZ)
{
	FValueRef X = ConstantBool(InX);
	FValueRef Y = ConstantBool(InY);
	FValueRef Z = ConstantBool(InZ);
	return Vector3(X, Y, Z);
}

FValueRef FEmitter::ConstantBool4(bool InX, bool InY, bool InZ, bool InW)
{
	FValueRef X = ConstantBool(InX);
	FValueRef Y = ConstantBool(InY);
	FValueRef Z = ConstantBool(InZ);
	FValueRef W = ConstantBool(InW);
	return Vector4(X, Y, Z, W);
}

FValueRef FEmitter::ConstantInt(TInteger InX)
{
	FConstant Scalar = MakePrototype<FConstant>(FType::MakeScalar(EScalarKind::Int));
	Scalar.Integer = InX;
	return EmitPrototype(*this, Scalar);
}

FValueRef FEmitter::ConstantInt2(UE::Math::TIntVector2<TInteger> InValue)
{
	FValueRef X = ConstantInt(InValue.X);
	FValueRef Y = ConstantInt(InValue.Y);
	return Vector2(X, Y);
}

FValueRef FEmitter::ConstantInt3(UE::Math::TIntVector3<TInteger> InValue)
{
	FValueRef X = ConstantInt(InValue.X);
	FValueRef Y = ConstantInt(InValue.Y);
	FValueRef Z = ConstantInt(InValue.Z);
	return Vector3(X, Y, Z);
}

FValueRef FEmitter::ConstantInt4(UE::Math::TIntVector4<TInteger> InValue)
{
	FValueRef X = ConstantInt(InValue.X);
	FValueRef Y = ConstantInt(InValue.Y);
	FValueRef Z = ConstantInt(InValue.Z);
	FValueRef W = ConstantInt(InValue.W);
	return Vector4(X, Y, Z, W);
}

FValueRef FEmitter::ConstantFloat(TFloat InX)
{
	FConstant Scalar = MakePrototype<FConstant>(FType::MakeScalar(EScalarKind::Float));
	Scalar.Float = InX;
	return EmitPrototype(*this, Scalar);
}

FValueRef FEmitter::ConstantFloat2(UE::Math::TVector2<TFloat> InValue)
{
	FValueRef X = ConstantFloat(InValue.X);
	FValueRef Y = ConstantFloat(InValue.Y);
	return Vector2(X, Y);
}

FValueRef FEmitter::ConstantFloat3(UE::Math::TVector<TFloat> InValue)
{
	FValueRef X = ConstantFloat(InValue.X);
	FValueRef Y = ConstantFloat(InValue.Y);
	FValueRef Z = ConstantFloat(InValue.Z);
	return Vector3(X, Y, Z);
}

FValueRef FEmitter::ConstantFloat4(UE::Math::TVector4<TFloat> InValue)
{
	FValueRef X = ConstantFloat(InValue.X);
	FValueRef Y = ConstantFloat(InValue.Y);
	FValueRef Z = ConstantFloat(InValue.Z);
	FValueRef W = ConstantFloat(InValue.W);
	return Vector4(X, Y, Z, W);
}

FValueRef FEmitter::ConstantDouble(TDouble InX)
{
	FConstant Scalar = MakePrototype<FConstant>(FType::MakeDoubleScalar());
	Scalar.Double = InX;
	return EmitPrototype(*this, Scalar);
}

/*--------------------- Other non-instruction values emission ---------------------*/

FValueRef FEmitter::Poison()
{
	if (CVarMaterialIRDebugBreakOnPoison.GetValueOnGameThread())
	{
		UE_DEBUG_BREAK();
	}
	return FPoison::Get();
}

FValueRef FEmitter::ExternalInput(EExternalInput Id, uint32 UserData)
{
	FExternalInput Prototype = MakePrototype<FExternalInput>(GetExternalInputType(Id));
	Prototype.Id = Id;
	Prototype.UserData = UserData;
	return EmitPrototype(*this, Prototype);
}

FValueRef FEmitter::MaterialParameterCollection(UMaterialParameterCollection* Collection)
{
	FMaterialParameterCollection Prototype = MakePrototype<FMaterialParameterCollection>(FType::MakeParameterCollection());
	Prototype.Collection = Collection;
	return EmitPrototype(*this, Prototype);
}

FValueRef FEmitter::ShadingModel(EMaterialShadingModel Id)
{
	// If the shading model is masked out, fallback to default shading model
	uint32 PlatformShadingModelsMask = GetPlatformShadingModelsMask(GetShaderPlatform());
	if ((PlatformShadingModelsMask & (1u << (uint32)Id)) == 0)
	{
		Id = MSM_DefaultLit;
	}

	FShadingModel Prototype = MakePrototype<FShadingModel>(FType::MakeShadingModel());
	Prototype.Id = Id;
	return EmitPrototype(*this, Prototype);
}

FValueRef FEmitter::TextureObject(UTexture* Texture, EMaterialSamplerType SamplerType)
{
	check(Texture);
	FString SamplerTypeError;
	if (!UMaterialExpressionTextureBase::VerifySamplerType(GetShaderPlatform(), GetTargetPlatform(), Texture, SamplerType, SamplerTypeError))
	{
		Errorf(TEXT("%s"), *SamplerTypeError);
		return Poison();
	}

	FTextureObject Proto = MakePrototype<FTextureObject>(FType::MakeTexture());
	Proto.Texture = Texture;
	Proto.SamplerType = SamplerType;
	Proto.Analysis_UniformParameterIndex = INDEX_NONE;
	return EmitPrototype(*this, Proto);
}

// Maps a sampler type from standard texture (ST) to virtual texture (VT).
static EMaterialSamplerType PromoteSamplerTypeFromSTtoVT(EMaterialSamplerType InSamplerType)
{
	switch (InSamplerType)
	{
		case SAMPLERTYPE_Color: return SAMPLERTYPE_VirtualColor;
		case SAMPLERTYPE_Grayscale: return SAMPLERTYPE_VirtualGrayscale;
		case SAMPLERTYPE_Alpha: return SAMPLERTYPE_VirtualAlpha;
		case SAMPLERTYPE_Normal: return SAMPLERTYPE_VirtualNormal;
		case SAMPLERTYPE_Masks: return SAMPLERTYPE_VirtualMasks;
		case SAMPLERTYPE_LinearColor: return SAMPLERTYPE_VirtualLinearColor;
		case SAMPLERTYPE_LinearGrayscale: return SAMPLERTYPE_VirtualLinearGrayscale;
	}
	return InSamplerType;
}

// Maps a sampler type from virtual texture (VT) to standard texture (ST).
static EMaterialSamplerType DemoteSamplerTypeFromVTtoST(EMaterialSamplerType InSamplerType)
{
	switch (InSamplerType)
	{
		case SAMPLERTYPE_VirtualColor: return SAMPLERTYPE_Color;
		case SAMPLERTYPE_VirtualGrayscale: return SAMPLERTYPE_Grayscale;
		case SAMPLERTYPE_VirtualAlpha: return SAMPLERTYPE_Alpha;
		case SAMPLERTYPE_VirtualNormal: return SAMPLERTYPE_Normal;
		case SAMPLERTYPE_VirtualMasks: return SAMPLERTYPE_Masks;
		case SAMPLERTYPE_VirtualLinearColor: return SAMPLERTYPE_LinearColor;
		case SAMPLERTYPE_VirtualLinearGrayscale: return SAMPLERTYPE_LinearGrayscale;
	}
	return InSamplerType;
}

static bool IsVirtualTexture(FValueRef Texture)
{
	if (FUniformParameter* UniformParameter = Texture->As<FUniformParameter>())
	{
		return IsVirtualSamplerType(UniformParameter->SamplerType);
	}
	if (FTextureObject* TextureObject = Texture->As<FTextureObject>())
	{
		return (TextureObject->Texture->GetMaterialType() & MCT_TextureVirtual) != 0;
	}
	if (Texture->IsA(VK_RuntimeVirtualTextureObject))
	{
		return true;
	}
	return false;
}

// Returns this value's texture sampler type if it has one (SAMPLERTYPE_MAX otherwise).
static EMaterialSamplerType GetValueMaterialSamplerType(FValueRef Value)
{
	if (MIR::FTextureObject* TextureObject = MIR::As<MIR::FTextureObject>(Value))
	{
		return TextureObject->SamplerType;
	}
	if (MIR::FRuntimeVirtualTextureObject* RVTextureObject = MIR::As<MIR::FRuntimeVirtualTextureObject>(Value))
	{
		return RVTextureObject->SamplerType;
	}
	if (MIR::FUniformParameter* UniformParameter = MIR::As<MIR::FUniformParameter>(Value))
	{
		return UniformParameter->SamplerType;
	}
	return SAMPLERTYPE_MAX;
}

static EMaterialSamplerType MapSamplerTypeForTexture(FValueRef InTexture, EMaterialSamplerType InSamplerType)
{
	// Can't sample with virtual texturing if input texture is not a virtual texture
	if (InSamplerType == SAMPLERTYPE_MAX)
	{
		InSamplerType = GetValueMaterialSamplerType(InTexture);
	}
	return IsVirtualTexture(InTexture) ? PromoteSamplerTypeFromSTtoVT(InSamplerType) : DemoteSamplerTypeFromVTtoST(InSamplerType);
}

static FValueRef VTPageTableLoadFromSamplerSource(
	FEmitter& Em, FValueRef Texture, const FTextureSampleBaseAttributes& BaseAttributes, FValueRef TexCoord,
	FValueRef TexCoordDdx = {}, FValueRef TexCoordDdy = {}, ETextureMipValueMode MipValueMode = TMVM_None, FValueRef MipValue = {})
{
	// Cast input texture to UTexture. If it's a URuntimeVirtualTexture, we accept the cast to be null when passed to GetTextureAddressForSamplerSource().
	TextureAddress StaticAddressX = TA_Wrap;
	TextureAddress StaticAddressY = TA_Wrap;
	TextureAddress StaticAddressZ = TA_Wrap;
	UE::MaterialTranslatorUtils::GetTextureAddressForSamplerSource(Cast<UTexture>(Texture->GetTextureObject()), BaseAttributes.SamplerSourceMode, StaticAddressX, StaticAddressY, StaticAddressZ);

	return Em.VTPageTableLoad(Texture, StaticAddressX, StaticAddressY, TexCoord, TexCoordDdx, TexCoordDdy, BaseAttributes.bEnableFeedback, BaseAttributes.bIsAdaptive, MipValueMode, MipValue);
}

FValueRef FEmitter::RuntimeVirtualTextureObject(URuntimeVirtualTexture* RVTexture, EMaterialSamplerType SamplerType, int32 VTLayerIndex, int32 VTPageTableIndex)
{
	check(RVTexture);
	check(IsVirtualSamplerType(SamplerType));

	FRuntimeVirtualTextureObject Prototype = MakePrototype<FRuntimeVirtualTextureObject>(FType::MakeRuntimeVirtualTexture());
	Prototype.RVTexture = RVTexture;
	Prototype.SamplerType = PromoteSamplerTypeFromSTtoVT(SamplerType);
	Prototype.VTLayerIndex = VTLayerIndex;
	Prototype.VTPageTableIndex = VTPageTableIndex;
	Prototype.Analysis_UniformParameterIndex = INDEX_NONE;
	return EmitPrototype(*this, Prototype);
}

FValueRef FEmitter::Parameter(FName Name, FMaterialParameterMetadata& Metadata, EMaterialSamplerType SamplerType, int32 VTLayerIndex, int32 VTPageTableIndex)
{
	// Helper local function that register a parameter (info and metadata) to the module, and returns some uint32 ID.
	auto RegisterParameter = [] (FMaterialIRModule* InModule, FMaterialParameterInfo InInfo, const FMaterialParameterMetadata& InMetadata) -> uint32
	{
		uint32 Id;
		if (Internal::Find(InModule->ParameterInfoToId, InInfo, Id))
		{
			check(InModule->ParameterIdToData[Id].Get<FMaterialParameterMetadata>().Value == InMetadata.Value);
			return Id;
		}

		Id = InModule->ParameterIdToData.Num();
		InModule->ParameterInfoToId.Add(InInfo, Id);
		InModule->ParameterIdToData.Push({ InInfo, InMetadata });
		return Id;
	};

	auto MakeUniformParameter = [this, &RegisterParameter](
		FType Type, FMaterialParameterInfo InInfo, const FMaterialParameterMetadata& InMetadata,
		EMaterialSamplerType InSamplerType = SAMPLERTYPE_Color, int32 VTLayerIndex = INDEX_NONE, int32 VTPageTableIndex = INDEX_NONE) -> FValueRef
	{
		FUniformParameter Prototype = MakePrototype<FUniformParameter>(MoveTemp(Type));
		Prototype.ParameterIdInModule = RegisterParameter(Module, MoveTemp(InInfo), InMetadata);
		Prototype.SamplerType = InSamplerType;
		Prototype.VTLayerIndex = VTLayerIndex;
		Prototype.VTPageTableIndex = VTPageTableIndex;
		Prototype.Analysis_UniformParameterIndex = INDEX_NONE;
		return EmitPrototype(*this, Prototype);
	};

	FMaterialParameterInfo Info{ Name };

	switch (Metadata.Value.Type)
	{
		case EMaterialParameterType::Scalar:
		{
			if (Metadata.PrimitiveDataIndex != INDEX_NONE)
			{
				return CustomPrimitiveData(Metadata.PrimitiveDataIndex);
			}
			return MakeUniformParameter(FType::MakeFloatScalar(), Info, Metadata);
		}

		case EMaterialParameterType::Vector:
		case EMaterialParameterType::DoubleVector:
		{
			if (Metadata.PrimitiveDataIndex != INDEX_NONE)
			{
				FValue* X = CustomPrimitiveData(Metadata.PrimitiveDataIndex + 0);
				FValue* Y = CustomPrimitiveData(Metadata.PrimitiveDataIndex + 1);
				FValue* Z = CustomPrimitiveData(Metadata.PrimitiveDataIndex + 2);
				FValue* W = CustomPrimitiveData(Metadata.PrimitiveDataIndex + 3);
				return Vector4(X, Y, Z, W);
			}

			const EScalarKind ScalarKind = Metadata.Value.Type == EMaterialParameterType::Vector ? EScalarKind::Float : EScalarKind::Double;
			return MakeUniformParameter(FType::MakeVector(ScalarKind, 4), Info, Metadata);
		}

		case EMaterialParameterType::Texture:
		case EMaterialParameterType::Font:
		{
			return MakeUniformParameter(FType::MakeTexture(), Info, Metadata, SamplerType);
		}

		case EMaterialParameterType::RuntimeVirtualTexture:
		{
			return MakeUniformParameter(FType::MakeRuntimeVirtualTexture(), Info, Metadata, PromoteSamplerTypeFromSTtoVT(SamplerType), VTLayerIndex, VTPageTableIndex);
		}

		case EMaterialParameterType::StaticSwitch:
		{
			// Apply eventual parameter override
			for (const FStaticSwitchParameter& Param : StaticParameterSet->GetRuntime().StaticSwitchParameters)
			{
				if (Param.IsOverride() && Param.ParameterInfo.Name == Name)
				{
					Metadata.Value.Bool[0] = Param.Value;
					break;
				}
			}
			return ConstantBool(Metadata.Value.Bool[0]);
		}

		case EMaterialParameterType::StaticComponentMask:
		{
			// Apply eventual parameter override
			for (const FStaticComponentMaskParameter& Param : StaticParameterSet->EditorOnly.StaticComponentMaskParameters)
			{
				if (Param.IsOverride() && Param.ParameterInfo.Name == Name)
				{
					Metadata.Value.Bool[0] = Param.R;
					Metadata.Value.Bool[1] = Param.G;
					Metadata.Value.Bool[2] = Param.B;
					Metadata.Value.Bool[3] = Param.A;
					break;
				}
			}

			return ConstantBool4(Metadata.Value.Bool[0], Metadata.Value.Bool[1], Metadata.Value.Bool[2], Metadata.Value.Bool[3]);
		}

		default:
			UE_MIR_TODO();
	}
}

UObject* FEmitter::GetTextureFromValue(MIR::FValueRef Texture)
{
	if (!Texture)
	{
		return nullptr;
	}
	// Handle case for uniform parameters here because their metadata is stored in the IR module, which the emitter has access to.
	if (FUniformParameter* UniformParameter = Texture->As<FUniformParameter>())
	{
		const FMaterialParameterMetadata& ParameterMetadata = Module->GetParameterMetadata(UniformParameter->ParameterIdInModule);
		return ParameterMetadata.Value.AsTextureObject();
	}
	return Texture->GetTextureObject();
}

// For now, only a small subset of opcodes are supported: TextureSize, TexelSize, and RuntimeVirtualTextureUniform.
FValueRef FEmitter::PreshaderParameter(FType Type, UE::Shader::EPreshaderOpcode Opcode, FValueRef SourceParameter, FPreshaderParameterPayload Payload)
{
	checkf(
		Opcode == UE::Shader::EPreshaderOpcode::TextureSize ||
		Opcode == UE::Shader::EPreshaderOpcode::TexelSize ||
		Opcode == UE::Shader::EPreshaderOpcode::RuntimeVirtualTextureUniform,
		TEXT("Preshader opcode (0x%X) not supported for parameters in new material translator"), (int32)Opcode
	);

	UObject* SourceParameterTexture = GetTextureFromValue(SourceParameter);
	if (!SourceParameterTexture)
	{
		Error(TEXT("Missing default texture from source parameter"));
		return Poison();
	}

	FPreshaderParameter Prototype = MakePrototype<FPreshaderParameter>(Type);
	Prototype.SourceParameter = SourceParameter;
	Prototype.Opcode = Opcode;
	Prototype.TextureIndex = Material->GetReferencedTextures().Find(SourceParameterTexture);
	Prototype.Payload = MoveTemp(Payload);
	return EmitPrototype(*this, Prototype);
}

FValueRef FEmitter::CustomPrimitiveData(uint32 PrimitiveDataIndex)
{
	// UE_MIR_TODO();
	return {};
}

FValueRef FEmitter::SceneTexture(ESceneTextureId SceneTextureId)
{
	FScreenTexture Proto = MakePrototype<FScreenTexture>(MIR::FType::MakeIntScalar());
	Proto.TextureKind = EScreenTexture::SceneTexture;
	Proto.Id = SceneTextureId;
	return EmitPrototype(*this, Proto);
}

FValueRef FEmitter::UserSceneTexture(FName UserSceneTexture)
{
	FScreenTexture Proto = MakePrototype<FScreenTexture>(MIR::FType::MakeIntScalar());
	Proto.TextureKind = EScreenTexture::UserSceneTexture;
	Proto.Id = PPI_UserSceneTexture0;				// Actual ID determined later during MIR lowering
	Proto.UserSceneTexture = UserSceneTexture;
	return EmitPrototype(*this, Proto);
}

FValueRef FEmitter::DBufferTexture(EDBufferTextureId DBufferId)
{
	FScreenTexture Proto = MakePrototype<FScreenTexture>(MIR::FType::MakeIntScalar());
	Proto.TextureKind = EScreenTexture::DBufferTexture;
	Proto.DBufferId = DBufferId;
	return EmitPrototype(*this, Proto);
}

FValueRef FEmitter::ScreenTexture(EScreenTexture TextureKind)
{
	check(TextureKind != EScreenTexture::SceneTexture && TextureKind != EScreenTexture::UserSceneTexture && TextureKind != EScreenTexture::DBufferTexture);
	FScreenTexture Proto = MakePrototype<FScreenTexture>(MIR::FType::MakeIntScalar());
	Proto.TextureKind = TextureKind;
	Proto.Id = PPI_SceneColor;					// Ignored
	return EmitPrototype(*this, Proto);
}

/*------------------------------ Instruction emission ------------------------------*/

FSetMaterialOutput* FEmitter::SetMaterialOutput(EMaterialProperty InProperty, FValue* Arg)
{
	FSetMaterialOutput Proto = MakePrototype<FSetMaterialOutput>(FType::MakeVoid());
	Proto.Property           = InProperty;
	Proto.Arg                = Arg;
	
	FSetMaterialOutput* Instr = static_cast<FSetMaterialOutput*>(EmitPrototype(*this, Proto).Value);

	// Add the instruction to list of outputs of the stages it is evaluated in.
	if (InProperty == MP_WorldPositionOffset)
	{
		Module->GetEntryPoint(MIR::Stage_Vertex).Outputs[0] = Instr;
	}
	else
	{
		int32 StageOutputIndex = UMaterialAggregate::MaterialPropertyToAttributeIndex(InProperty);
		check(StageOutputIndex != -1);
		Module->GetEntryPoint(MIR::Stage_Pixel).Outputs[StageOutputIndex] = Instr;
		Module->GetEntryPoint(MIR::Stage_Compute).Outputs[StageOutputIndex] = Instr;
	}

	return Instr;
}

FValueRef FEmitter::Vector2(FValueRef InX, FValueRef InY)
{
	if (IsAnyNotValid(InX, InY))
	{
		return Poison();
	}

	// TODO: replace these checks with casts instead
	check(InX->Type.IsScalar());
	check(InX->Type == InY->Type);

	TComposite<2> Vector = MakePrototype<TComposite<2>>(FType::MakeVector(InX->Type.GetPrimitive().ScalarKind, 2));

	TArrayView<FValue*> Components = Vector.GetMutableComponents();
	Components[0] = InX;
	Components[1] = InY;

	FValueRef Value = EmitPrototype(*this, Vector);
	
	// If all components source inputs match, refer to the same input in the vector value too (for debugging purposes).
	if (InX.Input == InY.Input)
	{
		Value.Input = InX.Input;
	}

	return Value;
}

FValueRef FEmitter::Vector3(FValueRef InXY, FValueRef InZ)
{
	if (IsAnyNotValid(InXY, InZ))
	{
		return Poison();
	}

	// TODO: replace these checks with casts instead
	check(InXY->Type.IsVector() && InXY->Type.GetPrimitive().NumComponents() == 2);
	check(InZ->Type.IsPrimitive() && InZ->Type.GetPrimitive().ScalarKind == InXY->Type.GetPrimitive().ScalarKind);

	TComposite<3> Vector = MakePrototype<TComposite<3>>(FType::MakeVector(InXY->Type.GetPrimitive().ScalarKind, 3));
	TArrayView<FValue*> Components = Vector.GetMutableComponents();
	Components[0] = Subscript(InXY, 0);
	Components[1] = Subscript(InXY, 1);
	Components[2] = InZ;

	FValueRef Value = EmitPrototype(*this, Vector);

	// If all components source inputs match, refer to the same input in the vector value too (for debugging purposes).
	if (InXY.Input == InZ.Input )
	{
		Value.Input = InXY.Input;
	}

	return Value;
}

FValueRef FEmitter::Vector3(FValueRef InX, FValueRef InY, FValueRef InZ)
{
	if (IsAnyNotValid(InX, InY, InZ))
	{
		return Poison();
	}
	
	// TODO: replace these checks with casts instead
	check(InX->Type.IsScalar());
	check(InX->Type == InY->Type);
	check(InY->Type == InZ->Type);

	TComposite<3> Vector = MakePrototype<TComposite<3>>(FType::MakeVector(InX->Type.GetPrimitive().ScalarKind, 3));
	TArrayView<FValue*> Components = Vector.GetMutableComponents();
	Components[0] = InX;
	Components[1] = InY;
	Components[2] = InZ;

	FValueRef Value = EmitPrototype(*this, Vector);

	// If all components source inputs match, refer to the same input in the vector value too (for debugging purposes).
	if (InX.Input == InY.Input && InX.Input == InZ.Input)
	{
		Value.Input = InX.Input;
	}

	return Value;
}

FValueRef FEmitter::Vector4(FValueRef InXYZ, FValueRef InW)
{
	if (IsAnyNotValid(InXYZ, InW))
	{
		return Poison();
	}

	// TODO: replace these checks with casts instead
	check(InXYZ->Type.IsVector() && InXYZ->Type.GetPrimitive().NumComponents() == 3);
	check(InW->Type.IsPrimitive() && InW->Type.GetPrimitive().ScalarKind == InXYZ->Type.GetPrimitive().ScalarKind);

	TComposite<4> Vector = MakePrototype<TComposite<4>>(FType::MakeVector(InXYZ->Type.GetPrimitive().ScalarKind, 4));
	TArrayView<FValue*> Components = Vector.GetMutableComponents();
	Components[0] = Subscript(InXYZ, 0);
	Components[1] = Subscript(InXYZ, 1);
	Components[2] = Subscript(InXYZ, 2);
	Components[3] = InW;

	FValueRef Value = EmitPrototype(*this, Vector);

	// If all components source inputs match, refer to the same input in the vector value too (for debugging purposes).
	if (InXYZ.Input == InW.Input)
	{
		Value.Input = InXYZ.Input;
	}

	return Value;
}

FValueRef FEmitter::Vector4(FValueRef InXY, FValueRef InZ, FValueRef InW)
{
	if (IsAnyNotValid(InXY, InZ, InW))
	{
		return Poison();
	}

	// TODO: replace these checks with casts instead
	check(InXY->Type.IsVector() && InXY->Type.GetPrimitive().NumComponents() == 2);
	check(InZ->Type.IsPrimitive() && InZ->Type.GetPrimitive().ScalarKind == InXY->Type.GetPrimitive().ScalarKind);
	check(InW->Type.IsPrimitive() && InW->Type.GetPrimitive().ScalarKind == InXY->Type.GetPrimitive().ScalarKind);

	TComposite<4> Vector = MakePrototype<TComposite<4>>(FType::MakeVector(InXY->Type.GetPrimitive().ScalarKind, 4));
	TArrayView<FValue*> Components = Vector.GetMutableComponents();
	Components[0] = Subscript(InXY, 0);
	Components[1] = Subscript(InXY, 1);
	Components[2] = InZ;
	Components[3] = InW;

	FValueRef Value = EmitPrototype(*this, Vector);

	// If all components source inputs match, refer to the same input in the vector value too (for debugging purposes).
	if (InXY.Input == InZ.Input && InXY.Input == InW.Input)
	{
		Value.Input = InXY.Input;
	}

	return Value;
}

FValueRef FEmitter::Vector4(FValueRef InX, FValueRef InY, FValueRef InZ, FValueRef InW)
{
	if (IsAnyNotValid(InX, InY, InZ, InW))
	{
		return Poison();
	}

	// TODO: replace these checks with casts instead
	check(InX->Type.IsScalar());
	check(InX->Type == InY->Type);
	check(InY->Type == InZ->Type);
	check(InZ->Type == InW->Type);

	TComposite<4> Vector = MakePrototype<TComposite<4>>(FType::MakeVector(InX->Type.GetPrimitive().ScalarKind, 4));
	TArrayView<FValue*> Components = Vector.GetMutableComponents();
	Components[0] = InX;
	Components[1] = InY;
	Components[2] = InZ;
	Components[3] = InW;

	FValueRef Value = EmitPrototype(*this, Vector);

	// If all components source inputs match, refer to the same input in the vector value too (for debugging purposes).
	if (InX.Input == InY.Input && InX.Input == InZ.Input && InX.Input == InW.Input)
	{
		Value.Input = InX.Input;
	}

	return Value;
}

FValueRef FEmitter::Aggregate(const UMaterialAggregate* InAggregate)
{
	return Aggregate(InAggregate, {}, TConstArrayView<FValueRef>{});
}

static FValueRef EmitAttributeDefaultValue(FEmitter& Emitter, const UMaterial* Material, const FMaterialAggregateAttribute& Attribute)
{
	switch (Attribute.Type)
	{
		case EMaterialAggregateAttributeType::Bool1: return Emitter.ConstantBool((bool)Attribute.DefaultValue.X);
		case EMaterialAggregateAttributeType::Bool2: return Emitter.ConstantBool2((bool)Attribute.DefaultValue.X, (bool)Attribute.DefaultValue.Y);
		case EMaterialAggregateAttributeType::Bool3: return Emitter.ConstantBool3((bool)Attribute.DefaultValue.X, (bool)Attribute.DefaultValue.Y, (bool)Attribute.DefaultValue.Z);
		case EMaterialAggregateAttributeType::Bool4: return Emitter.ConstantBool4((bool)Attribute.DefaultValue.X, (bool)Attribute.DefaultValue.Y, (bool)Attribute.DefaultValue.Z, (bool)Attribute.DefaultValue.W);
		case EMaterialAggregateAttributeType::UInt1: return Emitter.ConstantInt((TInteger)Attribute.DefaultValue.X);
		case EMaterialAggregateAttributeType::UInt2: return Emitter.ConstantInt2({ (TInteger)Attribute.DefaultValue.X, (TInteger)Attribute.DefaultValue.Y });
		case EMaterialAggregateAttributeType::UInt3: return Emitter.ConstantInt3({ (TInteger)Attribute.DefaultValue.X, (TInteger)Attribute.DefaultValue.Y, (TInteger)Attribute.DefaultValue.Z });
		case EMaterialAggregateAttributeType::UInt4: return Emitter.ConstantInt4({ (TInteger)Attribute.DefaultValue.X, (TInteger)Attribute.DefaultValue.Y, (TInteger)Attribute.DefaultValue.Z, (TInteger)Attribute.DefaultValue.W });
		case EMaterialAggregateAttributeType::Float1: return Emitter.ConstantFloat((TFloat)Attribute.DefaultValue.X);
		case EMaterialAggregateAttributeType::Float2: return Emitter.ConstantFloat2({ (TFloat)Attribute.DefaultValue.X, (TFloat)Attribute.DefaultValue.Y });
		case EMaterialAggregateAttributeType::Float3: return Emitter.ConstantFloat3({ (TFloat)Attribute.DefaultValue.X, (TFloat)Attribute.DefaultValue.Y, (TFloat)Attribute.DefaultValue.Z });
		case EMaterialAggregateAttributeType::Float4: return Emitter.ConstantFloat4(UE::Math::TVector4<TFloat>{ Attribute.DefaultValue.X, Attribute.DefaultValue.Y, Attribute.DefaultValue.Z, Attribute.DefaultValue.W });
		case EMaterialAggregateAttributeType::ShadingModel: return Emitter.ShadingModel(Material->GetShadingModels().GetFirstShadingModel());
		case EMaterialAggregateAttributeType::MaterialAttributes: return Emitter.Aggregate(UMaterialAggregate::GetMaterialAttributes());
		case EMaterialAggregateAttributeType::Aggregate: return Emitter.Aggregate(Attribute.Aggregate);
		default: UE_MIR_UNREACHABLE();
	}
}

FValueRef FEmitter::Aggregate(const UMaterialAggregate* InAggregate, FValueRef InPrototype, TConstArrayView<FValueRef> AttributeValues)
{
	// Check that the specified prototype (if any) aggregate matches the one provided.
	InPrototype = CheckIsAggregate(InPrototype, InAggregate);

	// If a prototype is provided and there are no attribute assignments, this is a no-op, return the prototype.
	if (InPrototype.IsValid() && AttributeValues.IsEmpty())
	{
		return InPrototype;
	}

	// Create the new composite to store the aggregate attribute values.
	FComposite* AggregateValue = MakeCompositePrototype(*this, FType::MakeAggregate(InAggregate), InAggregate->Attributes.Num());
	
	// Assign all components of the new composite value.
	TArrayView<FValue*> Components = AggregateValue->GetMutableComponents();
	for (int32 i = 0; i < Components.Num(); ++i)
	{
		// Get the the ith aggregate aggregate MIR type.
		FType AttributeType = GetMaterialAggregateAttributeType(InAggregate, i);

		if (i < AttributeValues.Num() && AttributeValues[i])
		{
			// Set the this aggregate component to the specified value cast to the attribute type, if present...
			Components[i] = Cast(AttributeValues[i], AttributeType);
		}
		else if (InPrototype)
		{
			// ... otherwise use the component value as in the prototype if provided
			Components[i] = Subscript(InPrototype, i);
		}
		else
		{
			// ...otherwise construct the default value as indicated in the attribute.
			Components[i] = EmitAttributeDefaultValue(*this, Material, InAggregate->Attributes[i]);
		}
	}

	return EmitPrototype(*this, *AggregateValue);
}

FValueRef FEmitter::Aggregate(const UMaterialAggregate* InAggregate, FValueRef InPrototype, TConstArrayView<FAttributeAssignment> AttributeAssignments)
{
	// Check that the specified prototype (if any) aggregate matches the one provided.
	InPrototype = CheckIsAggregate(InPrototype, InAggregate);

	// If prototype was provided and is poison, return it..
	if (InPrototype.IsPoison())
	{
		return InPrototype.ToPoison();
	}

	// Allocate temporary storage to hold the attribute values
	TTemporaryArray<FValueRef> AttributeValues { InAggregate->Attributes.Num() };
	AttributeValues.Zero();

	// Linearize the assignments into the array of attribute values.
	for (const FAttributeAssignment& Assignment : AttributeAssignments)
	{
		// Find the attribute index by name
		int32 AttrIndex = InAggregate->FindAttributeIndexByName(Assignment.Name);
		if (AttrIndex != INDEX_NONE)
		{
			// If found, set the slot to the assignment value.
			AttributeValues[AttrIndex] = Assignment.Value;
		}
	}

	return Aggregate(InAggregate, InPrototype, AttributeValues);
}

/*--------------------------------- Operator emission ---------------------------------*/

template <typename T>
static bool FoldComparisonOperatorScalar(EOperator Operator, T A, T B)
{
	if constexpr (std::is_floating_point_v<T>)
	{
		switch (Operator)
		{
			case UO_IsFinite: return FGenericPlatformMath::IsFinite(A);
			case UO_IsInf: return !FGenericPlatformMath::IsFinite(A);
			case UO_IsNan: return FGenericPlatformMath::IsNaN(A);
			default: break;
		}
	}

	switch (Operator)
	{
		case UO_Not: return !A;
		case BO_GreaterThan: return A > B;
		case BO_GreaterThanOrEquals: return A >= B;
		case BO_LessThan: return A < B;
		case BO_LessThanOrEquals: return A <= B;
		case BO_Equals: return A == B;
		case BO_NotEquals: return A != B;
		default: UE_MIR_UNREACHABLE();
	}
}

template <typename T>
static T ACosh(T x)
{
	static_assert(std::is_floating_point_v<T>);
	check(x >= 1);
	return FGenericPlatformMath::Loge(x + FGenericPlatformMath::Sqrt(x * x - 1));
}

template <typename T>
static T ASinh(T x)
{
	static_assert(std::is_floating_point_v<T>);
	return FGenericPlatformMath::Loge(x + FGenericPlatformMath::Sqrt(x * x + 1));
}

template <typename T>
static T ATanh(T x)
{
	static_assert(std::is_floating_point_v<T>);
    check(x > -1 && x < 1);
    return T(0.5) * FGenericPlatformMath::Loge((1 + x) / (1 - x));
}

template <typename T>
static T FoldScalarOperator(FEmitter& Emitter, EOperator Operator, T A, T B, T C)
{
	if constexpr (std::is_floating_point_v<T>)
	{
		switch (Operator)
		{
			case UO_ACos: return FGenericPlatformMath::Acos(A);
			case UO_ACosFast: return FGenericPlatformMath::Acos(A);
			case UO_ACosh: return ACosh(A);
			case UO_ASin: return FGenericPlatformMath::Asin(A);
			case UO_ASinFast: return FGenericPlatformMath::Asin(A);
			case UO_ASinh: return ASinh(A);
			case UO_ATan: return FGenericPlatformMath::Atan(A);
			case UO_ATanFast: return FGenericPlatformMath::Atan(A);
			case UO_ATanh: return ATanh(A);
			case UO_Ceil:
				if constexpr (std::is_same<T, float>::value)
				{
					return FGenericPlatformMath::CeilToFloat(A);
				}
				else
				{
					return FGenericPlatformMath::CeilToDouble(A);
				}
			case UO_Cos: return FGenericPlatformMath::Cos(A);
			case UO_Cosh: return FGenericPlatformMath::Cosh(A);
			case UO_Exponential: return FGenericPlatformMath::Pow(UE_EULERS_NUMBER, A);
			case UO_Exponential2: return FGenericPlatformMath::Pow(2.0f, A);
			case UO_Floor:
				if constexpr (std::is_same<T, float>::value)
				{
					return FGenericPlatformMath::FloorToFloat(A);
				}
				else
				{
					return FGenericPlatformMath::FloorToDouble(A);
				}
			case UO_Frac:
			{
				return FGenericPlatformMath::Fractional(A);
			}
			case UO_Logarithm:
			case UO_Logarithm2:
			case UO_Logarithm10:
			{
				const T Base = (Operator == UO_Logarithm) ? T(UE_EULERS_NUMBER)
					: Operator == UO_Logarithm2 ? T(2)
					: T(10);
				return FGenericPlatformMath::LogX(Base, A);
			}
			case UO_Reciprocal: return 1 / A;
			case UO_Round:
				if constexpr (std::is_same<T, float>::value)
				{
					return FGenericPlatformMath::RoundToFloat(A);
				}
				else
				{
					return FGenericPlatformMath::RoundToDouble(A);
				}
			case UO_Rsqrt: return FGenericPlatformMath::InvSqrt(A);
			case UO_Sin: return FGenericPlatformMath::Sin(A);
			case UO_Sinh: return FGenericPlatformMath::Sinh(A);
			case UO_Sqrt: return FGenericPlatformMath::Sqrt(A);
			case UO_Tan: return FGenericPlatformMath::Tan(A);
			case UO_Tanh: return FGenericPlatformMath::Tanh(A);
			case UO_Truncate:
				if constexpr (std::is_same<T, float>::value)
				{
					return FGenericPlatformMath::TruncToFloat(A);
				}
				else
				{
					return FGenericPlatformMath::TruncToDouble(A);
				}
			case BO_ATan2: return FGenericPlatformMath::Atan2(A, B);
			case BO_ATan2Fast: return FGenericPlatformMath::Atan2(A, B);
			case BO_Fmod: return FGenericPlatformMath::Fmod(A, B);
			// truncated division (A-B*(trunc(A/B))) where the result takes on the sign of operand 1 the dividend
			case BO_Modulo: return A - B * FGenericPlatformMath::TruncToFloat(A / B);
			case BO_Pow: return FGenericPlatformMath::Pow(A, B);
			case TO_Lerp: return FMath::Lerp<T>(A, B, C);
			case TO_Smoothstep: return FMath::SmoothStep<T>(A, B, C);
			default: break;
		}
	}

	if constexpr (std::is_integral_v<T>)
	{
		switch (Operator)
		{
			case UO_Not: return !A;
			case UO_BitwiseNot: return ~A;
			case BO_And: return A & B;
			case BO_Or: return A | B;
			case BO_BitwiseAnd: return A & B;
			case BO_BitwiseOr: return A | B;
			case BO_BitShiftLeft: return A << B;
			case BO_BitShiftRight: return A >> B;
			case BO_Modulo: return A % B;
			default: break;
		}
	}

	switch (Operator)
	{
		case UO_Abs: return FGenericPlatformMath::Abs<T>(A);
		case UO_Negate: return -A;
		case UO_Saturate: return FMath::Clamp(A, T(0), T(1));
		case BO_Add: return A + B;
		case BO_Subtract: return A - B;
		case BO_Multiply: return A * B;
		case BO_MatrixMultiply: return A * B; // mul() is also supported for scalars
		case BO_Divide: return A / B;
		case BO_Min: return FMath::Min<T>(A, B);
		case BO_Max: return FMath::Max<T>(A, B);
		case BO_Step: return B >= A ? 1.0f : 0.0f;
		case TO_Clamp: return FMath::Clamp(A, B, C);
		default: UE_MIR_UNREACHABLE();
	}
}

// It tries to apply a known identity of specified operator, e.g. "x + 0 = x ? x ? R".
// If it returns a value, the operation has been "folded" and the returned value is the
// result (in the example above, it would return "x").
// If it returns null, the end result could not be inferred, but the operator could have
// still been changed to some other (with lower complexity). For example "clamp(x, 0, 1)"
// will change to "saturate(x)".
static FValueRef TrySimplifyOperator(FEmitter& Emitter, EOperator& Op, FValueRef& A, FValueRef& B, FValueRef& C, FType ResultType)
{
	switch (Op)
	{
		/* Unary Operators */
		case UO_Length:
			if (A->Type.GetPrimitive().IsScalar())
			{
				Op = UO_Abs;
			}
			break;

		/* Binary Comparisons */
		case BO_GreaterThan:
		case BO_LessThan:
		case BO_NotEquals:
			if (A->Equals(B))
			{
				return Emitter.ConstantFalse();
			}
			break;

		case BO_GreaterThanOrEquals:
		case BO_LessThanOrEquals:
		case BO_Equals:
			if (A->Equals(B))
			{
				return Emitter.ConstantTrue();
			}
			break;

		/* Binary Arithmetic */
		case BO_Add:
			if (A->AreAllNearlyZero())
			{
				return B;
			}
			else if (B->AreAllNearlyZero())
			{
				return A;
			}
			break;

		case BO_Subtract:
			if (B->AreAllNearlyZero())
			{
				return A;
			}
			else if (A->AreAllNearlyZero())
			{
				return Emitter.Negate(B);
			}
			else if (A->Equals(B))
			{
				return Emitter.ConstantZero(A->Type.GetPrimitive().ScalarKind);
			}
			break;

		case BO_Multiply:
			if (A->AreAllNearlyZero() || B->AreAllNearlyOne())
			{
				return A;
			}
			else if (A->AreAllNearlyOne() || B->AreAllNearlyZero())
			{
				return B;
			}
			break;
		
		case BO_MatrixMultiply:
			if (ResultType.IsScalar())
			{
				Op = BO_Dot;

				// The dot could be simplified further, from dot to multiply if A and B are scalars.
				return TrySimplifyOperator(Emitter, Op, A, B, C, ResultType);
			}
			break;

		case BO_Divide:
			if (A->AreAllNearlyZero() || B->AreAllNearlyOne())
			{
				return A;
			}
			else if (A->Equals(B))
			{
				return Emitter.ConstantOne(A->Type.GetPrimitive().ScalarKind);
			}
			break;

		case BO_Modulo:
			if (A->AreAllNearlyZero() || B->AreAllNearlyOne())
			{
				return Emitter.ConstantZero(A->Type.GetPrimitive().ScalarKind);
			}
			break;

		case BO_BitwiseAnd:
			if (A->AreAllExactlyZero())
			{
				return A;
			}
			else if (B->AreAllExactlyZero())
			{
				return B;
			}
			break;

		case BO_BitwiseOr:
			if (A->AreAllExactlyZero())
			{
				return B;
			}
			else if (B->AreAllExactlyZero())
			{
				return A;
			}
			break;

		case BO_BitShiftLeft:
		case BO_BitShiftRight:
			if (A->AreAllExactlyZero() || B->AreAllExactlyZero())
			{
				return A;
			}
			break;
		
		case BO_Dot:
			if (A->Type.IsScalar())
			{
				check(B->Type.IsScalar());
				Op = BO_Multiply;
				return TrySimplifyOperator(Emitter, Op, A, B, C, ResultType);
			}
			if (A->AreAllNearlyZero() || B->AreAllNearlyZero())
			{
				return Emitter.ConstantZero(ResultType.GetPrimitive().ScalarKind);
			}
			break;

		case BO_Pow:
			if (A->AreAllNearlyZero())
			{
				// If the base is 0.
				return A;
			}
			else if (B->AreAllNearlyZero())
			{
				// If the exponent is 0.
				return Emitter.ConstantOne(A->Type.GetPrimitive().ScalarKind);
			}
			else if (B->AreAllExactlyOne())
			{
				// If the exponent is 1.
				return A;
			}
			break;

		case TO_Clamp:
			if (B->AreAllNearlyZero() && C->AreAllNearlyOne())
			{
				Op = UO_Saturate;
				B = {};
				C = {};
			}
			else if (B->Equals(C))
			{
				return B;
			}
			break;

		case TO_Lerp:
			if (C->AreAllNearlyZero())
			{
				return A;
			}
			else if (C->AreAllNearlyOne())
			{
				return B;
			}
			else if (A->Equals(B))
			{
				return A;
			}
			break;

		case TO_Select:
			if (A->AreAllTrue())
			{
				return B;
			}
			else if (A->AreAllFalse())
			{
				return C;
			}
			else if (B->Equals(C))
			{
				return B;
			}
			break;

		default:
			break;
	}

	return {};
}

// Tries to fold (statically evaluate) the operator, assuming that the arguments are all scalar.
// It returns either the result of the operator or null if it could not be folded.
static FValueRef TryFoldOperatorScalar(FEmitter& Emitter, EOperator Op, FValueRef A, FValueRef B, FValueRef C, FType ResultType)
{
	TOptional<FPrimitive> PrimitiveType = A->Type.AsPrimitive();

	// Try to simplify the operator. This could potentially change Op, A, B and C.
	if (FValueRef Simplified = TrySimplifyOperator(Emitter, Op, A, B, C, ResultType))
	{
		return Simplified;
	}

	// If TrySimplifyOperator did not already fold the `select` operator, there is nothing else to do.
	if (Op == TO_Select)
	{
		return {};
	}

	// Verify that both lhs and rhs are constants, otherwise we cannot fold the operation.
	const FConstant* AConstant = As<FConstant>(A.Value);
	const FConstant* BConstant = As<FConstant>(B.Value);
	const FConstant* CConstant = As<FConstant>(C.Value);
	if (!AConstant || (IsBinaryOperator(Op) && !BConstant) || (IsTernaryOperator(Op) && (!BConstant || !CConstant)))
	{
		return {};
	}

	// Call the appropriate helper function depending on what type of operator this is
	if (IsComparisonOperator(Op))
	{
		bool Result;
		switch (PrimitiveType->ScalarKind)
		{
			case EScalarKind::Int:
				Result = FoldComparisonOperatorScalar<TInteger>(Op, AConstant->Integer, BConstant->Integer);
				break;

			case EScalarKind::Float:
				Result = FoldComparisonOperatorScalar<TFloat>(Op, AConstant->Float, BConstant->Float);
				break;

			case EScalarKind::Double:
				Result = FoldComparisonOperatorScalar<TDouble>(Op, AConstant->Double, BConstant->Double);
				break;

			default:
				UE_MIR_UNREACHABLE();
		}
		return Emitter.ConstantBool(Result);
	}
	else
	{
		switch (PrimitiveType->ScalarKind)
		{
			case EScalarKind::Bool:
			{
				bool Result = FoldScalarOperator<TInteger>(Emitter, Op, AConstant->Boolean, BConstant ? BConstant->Boolean : 0, 0) & 0x1;
				return Emitter.ConstantBool(Result);
			}

			case EScalarKind::Int:
			{
				TInteger Result = FoldScalarOperator<TInteger>(Emitter, Op, AConstant->Integer, BConstant ? BConstant->Integer : 0, CConstant ? CConstant->Integer : 0);
				return Emitter.ConstantInt(Result);
			}

			case EScalarKind::Float:
			{
				TFloat Result = FoldScalarOperator<TFloat>(Emitter, Op, AConstant->Float, BConstant ? BConstant->Float : 0, CConstant ? CConstant->Float : 0);
				return Emitter.ConstantFloat(Result);
			}

			case EScalarKind::Double:
			{
				TDouble Result = FoldScalarOperator<TDouble>(Emitter, Op, AConstant->Double, BConstant ? BConstant->Double : 0, CConstant ? CConstant->Double : 0);
				return Emitter.ConstantDouble(Result);
			}

			default:
				UE_MIR_UNREACHABLE();
		}
	}
}

// Used to filter what parameter *primitive* types operators can take.
enum EOperatorParameterFlags
{
	OPF_Unknown = 0xff,                                         // Unspecified
	OPF_Any = 0,                                                // Any primitive type
	OPF_CheckIsBoolean = 1 << 1,                                // Check the type is boolean primitive of any dimension
	OPF_CheckIsInteger = 1 << 2,                                // Check the type is integer primitive of any dimension
	OPF_CheckIsArithmetic = 1 << 3,                             // Check the type is arithmetic primitive of any dimension (i.e. that supports arithmetic operations)
	OPF_CheckIsMatrix = 1 << 4,                                 // Check the type is any matrix type
	OPF_CheckIsNotMatrix = 1 << 5,                              // Check the type is any primitive type except matrices
	OPF_CheckIsVector3 = 1 << 6,                                // Check the type is a 3D vector of any scalar type
	OPF_CheckIsNonNegativeFloatConst = 1 << 7,                  // Check that if the argument is a constant float, it is not negative (x >= 0)
	OPF_CheckIsNonZeroFloatConst = 1 << 8,                      // Check that if the argument is a constant float, it is not zero    (x != 0)
	OPF_CheckIsOneOrGreaterFloatConst = 1 << 9,                 // Check that if the argument is a constant float, it is 1 or greater (xFloat >= 1)
	OPF_CheckIsBetweenMinusOneAndPlusOneFloatConst = 1 << 10,   // Check that if the argument is a constant float, it is between -1 and 1 (-1 < x < 1)
	OPF_CastToFirstArgumentType = 1 << 11,                      // Cast the argument to the first argument's type
	OPF_CastToAnyFloat = 1 << 12,                               // Cast the argument to the floating point primitive type of any dimension
	OPF_AllowDouble = 1 << 13,                                  // This argument is allowed to be a double
	OPF_CastToCommonScalarKind = 1 << 14,                       // Casts the argument to have the scalar kind in common with other arguments
	OPF_CastToCommonType = 1 << 15,                             // Cast the argument to the common arguments type
	OPF_CastToCommonArithmeticType = OPF_CheckIsArithmetic | OPF_CastToCommonType,
	OPF_CastToCommonArithmeticTypeAllowDouble = OPF_CheckIsArithmetic | OPF_CastToCommonType | OPF_AllowDouble,
	OPF_CastToCommonFloatType = OPF_CastToAnyFloat | OPF_CastToCommonType,
};

inline EOperatorParameterFlags operator|(EOperatorParameterFlags A, EOperatorParameterFlags B)
{
	return EOperatorParameterFlags(uint32(A) | uint32(B));
}

// Used to determine the operator result type based on argument types
enum EOperatorResult
{
	OR_Unknown,							    // Unspecified
	OR_FirstArgumentType,					// The same type as the first argument, LWC input produces float result
	OR_BooleanWithFirstArgumentDimensions,	// A boolean primitive type with the same dimensions (rows and columns) as the first argument type
	OR_FirstArgumentTypeToScalarLWC,		// A scalar primitive type with the same kind as the scalar type of the first argument, LWC results allowed
	OR_SecondArgumentType,					// The same type as the second argument
	OR_FirstArgumentTypeAllowDouble,		// The same type as the first argument, LWC results allowed
	OR_MatrixMultiplyResult,				// The result type of the matrix multiplication of first two arguments
};

// The signature of an operator consisting of its parameter and return type information.
struct FOperatorSignature
{
	EOperatorParameterFlags ParameterFlags[3] = { OPF_Unknown, OPF_Unknown, OPF_Unknown };
	EOperatorResult 		Result = OR_Unknown;
};

// Returns the signature of an operator.
static const FOperatorSignature* GetOperatorSignature(EOperator Op)
{
	static const FOperatorSignature* Signatures = [] ()
	{
		const FOperatorSignature UnaryFloat = { { OPF_CheckIsArithmetic | OPF_CastToAnyFloat }, OR_FirstArgumentType };
		const FOperatorSignature UnaryFloatOrDouble = { { OPF_CheckIsArithmetic | OPF_CastToAnyFloat | OPF_AllowDouble }, OR_FirstArgumentTypeAllowDouble };
		const FOperatorSignature UnaryFloatLWCDemote = { { OPF_CheckIsArithmetic | OPF_CastToAnyFloat | OPF_AllowDouble }, OR_FirstArgumentType };
		const FOperatorSignature UnaryFloatToBoolean = { { OPF_CheckIsArithmetic | OPF_CastToAnyFloat }, OR_BooleanWithFirstArgumentDimensions };
		const FOperatorSignature BinaryArithmetic = { { OPF_CastToCommonArithmeticType, OPF_CastToCommonArithmeticType }, OR_FirstArgumentType };
		const FOperatorSignature BinaryArithmeticAllowDouble = { { OPF_CastToCommonArithmeticTypeAllowDouble, OPF_CastToCommonArithmeticTypeAllowDouble }, OR_FirstArgumentTypeAllowDouble };
		const FOperatorSignature BinaryArithmeticLWCDemote = { { OPF_CastToCommonArithmeticTypeAllowDouble, OPF_CastToCommonArithmeticTypeAllowDouble }, OR_FirstArgumentType };
		const FOperatorSignature BinaryInteger = { { OPF_CheckIsInteger | OPF_CastToCommonArithmeticType, OPF_CheckIsInteger | OPF_CastToCommonArithmeticType }, OR_FirstArgumentType };
		const FOperatorSignature BinaryFloat = { { OPF_CastToCommonFloatType, OPF_CastToCommonFloatType }, OR_FirstArgumentType };
		const FOperatorSignature BinaryArithmeticComparisonAllowDouble = { { OPF_CastToCommonArithmeticType | OPF_AllowDouble, OPF_CastToCommonArithmeticType | OPF_AllowDouble }, OR_BooleanWithFirstArgumentDimensions };
		const FOperatorSignature BinaryLogical = { { OPF_CheckIsBoolean | OPF_CastToCommonType, OPF_CheckIsBoolean | OPF_CastToCommonType }, OR_FirstArgumentType };
		const FOperatorSignature TernaryArithmeticDouble = { { OPF_CastToCommonArithmeticTypeAllowDouble, OPF_CastToCommonArithmeticTypeAllowDouble, OPF_CastToCommonArithmeticTypeAllowDouble }, OR_FirstArgumentTypeAllowDouble };
		const FOperatorSignature TernaryFloatDoubleDemote = { { OPF_CastToCommonArithmeticTypeAllowDouble | OPF_CastToAnyFloat, OPF_CastToCommonArithmeticTypeAllowDouble, OPF_CastToCommonArithmeticTypeAllowDouble }, OR_FirstArgumentType };

		static FOperatorSignature S[OperatorCount];

		/* unary operators */
		S[UO_BitwiseNot] 				= { { OPF_CheckIsInteger }, OR_FirstArgumentType };
		S[UO_Negate] 					= { { OPF_CheckIsArithmetic | OPF_AllowDouble }, OR_FirstArgumentTypeAllowDouble };
		S[UO_Not] 						= { { OPF_CheckIsBoolean }, OR_FirstArgumentType };

		S[UO_Abs] 						= UnaryFloatOrDouble;
		S[UO_ACos] 						= UnaryFloatLWCDemote;
		S[UO_ACosFast]					= UnaryFloat;
		S[UO_ACosh] 					= { { OPF_CheckIsArithmetic | OPF_CastToAnyFloat | OPF_CheckIsOneOrGreaterFloatConst }, OR_FirstArgumentType };
		S[UO_ASin] 						= UnaryFloatLWCDemote;
		S[UO_ASinFast]					= UnaryFloat;
		S[UO_ASinh] 					= UnaryFloat;
		S[UO_ATan] 						= UnaryFloatLWCDemote;
		S[UO_ATanFast]					= UnaryFloat;
		S[UO_ATanh] 					= { { OPF_CheckIsArithmetic | OPF_CastToAnyFloat | OPF_CheckIsBetweenMinusOneAndPlusOneFloatConst }, OR_FirstArgumentType };
		S[UO_Ceil] 						= UnaryFloatOrDouble;
		S[UO_Cos] 						= UnaryFloatLWCDemote;
		S[UO_Exponential]				= UnaryFloat;
		S[UO_Exponential2]				= UnaryFloat;
		S[UO_Floor] 					= UnaryFloatOrDouble;
		S[UO_Frac]						= UnaryFloatLWCDemote;
		S[UO_IsFinite]					= UnaryFloatToBoolean;
		S[UO_IsInf]						= UnaryFloatToBoolean;
		S[UO_IsNan]						= UnaryFloatToBoolean;
		S[UO_Length]					= { { OPF_CheckIsArithmetic | OPF_CheckIsNotMatrix | OPF_CastToAnyFloat | OPF_AllowDouble }, OR_FirstArgumentTypeToScalarLWC };
		S[UO_Logarithm] 				= { { OPF_CheckIsArithmetic | OPF_CheckIsNonZeroFloatConst | OPF_CheckIsNonNegativeFloatConst | OPF_CastToAnyFloat }, OR_FirstArgumentType };
		S[UO_Logarithm10]				= { { OPF_CheckIsArithmetic | OPF_CheckIsNonZeroFloatConst | OPF_CheckIsNonNegativeFloatConst | OPF_CastToAnyFloat }, OR_FirstArgumentType };
		S[UO_Logarithm2] 				= { { OPF_CheckIsArithmetic | OPF_CheckIsNonZeroFloatConst | OPF_CheckIsNonNegativeFloatConst | OPF_CastToAnyFloat }, OR_FirstArgumentType };
		S[UO_LWCTile]					= { };		// UNUSED
		S[UO_Reciprocal]				= { { OPF_CheckIsArithmetic | OPF_CheckIsNonZeroFloatConst | OPF_CastToAnyFloat }, OR_FirstArgumentType };
		S[UO_Round] 					= UnaryFloatOrDouble;
		S[UO_Rsqrt]						= { { OPF_CheckIsArithmetic | OPF_CheckIsNonZeroFloatConst | OPF_CastToAnyFloat }, OR_FirstArgumentType };
		S[UO_Saturate]					= UnaryFloatLWCDemote;
		S[UO_Sign]						= UnaryFloatLWCDemote;
		S[UO_Sin] 						= UnaryFloatLWCDemote;
		S[UO_Sqrt]						= { { OPF_CheckIsArithmetic | OPF_CheckIsNonNegativeFloatConst | OPF_CastToAnyFloat | OPF_AllowDouble }, OR_FirstArgumentType };
		S[UO_Tan] 						= UnaryFloatLWCDemote;
		S[UO_Tanh] 						= UnaryFloat;
		S[UO_Truncate]					= UnaryFloatOrDouble;

		/* binary operators */
		S[BO_Equals]					= { { OPF_CastToCommonType | OPF_AllowDouble, OPF_CastToCommonType | OPF_AllowDouble }, OR_BooleanWithFirstArgumentDimensions };
		S[BO_GreaterThan]				= BinaryArithmeticComparisonAllowDouble;
		S[BO_GreaterThanOrEquals]		= BinaryArithmeticComparisonAllowDouble;
		S[BO_LessThan]					= BinaryArithmeticComparisonAllowDouble;
		S[BO_LessThanOrEquals]			= BinaryArithmeticComparisonAllowDouble;
		S[BO_NotEquals]					= { { OPF_CastToCommonType | OPF_AllowDouble, OPF_CastToCommonType | OPF_AllowDouble }, OR_BooleanWithFirstArgumentDimensions };
		
		S[BO_And] 						= BinaryLogical;
		S[BO_Or] 						= BinaryLogical;
		S[BO_Add] 						= BinaryArithmeticAllowDouble;
		S[BO_Subtract] 					= BinaryArithmeticAllowDouble;
		S[BO_Multiply] 					= BinaryArithmeticAllowDouble;
		S[BO_MatrixMultiply]			= { { OPF_CheckIsArithmetic | OPF_CastToCommonScalarKind, OPF_CheckIsArithmetic | OPF_CastToCommonScalarKind }, OR_MatrixMultiplyResult };
		S[BO_Divide] 					= BinaryArithmeticAllowDouble;
		S[BO_Modulo] 					= BinaryArithmetic;
		S[BO_BitwiseAnd]				= BinaryInteger;
		S[BO_BitwiseOr]					= BinaryInteger;
		S[BO_BitShiftLeft]				= BinaryInteger;
		S[BO_BitShiftRight]				= BinaryInteger;


		S[BO_ATan2]						= BinaryFloat;
		S[BO_ATan2Fast]					= BinaryFloat;
		S[BO_Cross] 					= { { OPF_CheckIsArithmetic | OPF_CheckIsVector3, OPF_CastToFirstArgumentType }, OR_FirstArgumentType };
		S[BO_Distance]					= { { OPF_CastToCommonFloatType | OPF_AllowDouble, OPF_CastToCommonFloatType | OPF_AllowDouble }, OR_FirstArgumentTypeToScalarLWC };
		S[BO_Dot] 						= { { OPF_CheckIsArithmetic | OPF_CheckIsNotMatrix | OPF_AllowDouble, OPF_CastToFirstArgumentType | OPF_AllowDouble }, OR_FirstArgumentTypeToScalarLWC };
		S[BO_Fmod] 						= { { OPF_CastToCommonFloatType | OPF_AllowDouble, OPF_CastToCommonFloatType }, OR_FirstArgumentType };		// First input can be LWC, but second and output are always demoted

		S[BO_Max] 						= BinaryArithmeticAllowDouble;
		S[BO_Min] 						= BinaryArithmeticAllowDouble;
		S[BO_Pow] 						= BinaryFloat;
		S[BO_Step] 						= BinaryArithmeticLWCDemote;

		/* ternary operators -- note lerp doesn't support LWC for the third argument! */
		S[TO_Clamp]						= TernaryArithmeticDouble;
		S[TO_Lerp] 						= { { OPF_CastToCommonArithmeticTypeAllowDouble | OPF_CastToAnyFloat, OPF_CastToCommonArithmeticTypeAllowDouble, OPF_CastToCommonArithmeticType }, OR_FirstArgumentTypeAllowDouble };;
		S[TO_Select]					= { { OPF_CheckIsBoolean | OPF_CheckIsNotMatrix, OPF_CheckIsNotMatrix | OPF_AllowDouble, OPF_CheckIsNotMatrix | OPF_AllowDouble}, OR_SecondArgumentType }; // Note: this is a special operator, which is handled manually in the Validate function
		S[TO_Smoothstep]				= TernaryFloatDoubleDemote;
		return S;
	} ();
	return &Signatures[Op];
}
 
// Validates that the types of the arguments are valid for specified operator.
// If valid, it returns the type of the result. Otherwise if it is not valid, it returns nullptr.
static FType ValidateOperatorAndGetResultType(FEmitter& Emitter, EOperator Op, FValueRef& A, FValueRef& B, FValueRef& C)
{
	// Argument A must have always been provided.
	check(A);

	// Assert that if C is specified, B must too.
	check(!C || B);

	// Verify that B argument has been provided if operator is binary.
	check(!IsBinaryOperator(Op) || B);

	// Verify that C argument has been provided if operator is ternary.
	check(!IsTernaryOperator(Op) || C);

	// Make sure the first argument has primitive type first, since the following operations assume this.
	if (!Emitter.CheckIsPrimitive(A).IsValid())
	{
		return FType::MakePoison();
	}

	// SPECIAL CASE: Given input float3, generates double3 type with the given tile value and zero offset. Caller must pass in float3.
	if (Op == UO_LWCTile)
	{
		if (!A->Type.GetPrimitive().IsFloat() || !A->Type.GetPrimitive().IsRowVector() || A->Type.GetPrimitive().NumComponents() != 3)
		{
			Emitter.Errorf(A, TEXT("Argument of LWCTile operator expected to be a 3D float vector."));
			return FType::MakePoison();
		}
		return FType::MakeDoubleVector(3);
	}

	// SPECIAL CASE: For Clamp, we do a special case and demote the first LWC argument if the second and third arguments (min / max) are non-LWC.
	// We want to do this before fetching FirstArgumentPrimitiveType.
	if (Op == TO_Clamp && A->Type.GetPrimitive().IsDouble() && !B->Type.IsDouble() && !C->Type.IsDouble())
	{
		A = Emitter.CastToFloatKind(A);
	}

	// Get the operator signature information.
	const FOperatorSignature* Signature = GetOperatorSignature(Op);

	// Handle automatic cast to float for operators that don't support LWC inputs
	FType FirstArgumentType = A->Type;
	if (FirstArgumentType.GetPrimitive().IsDouble() && !(Signature->ParameterFlags[0] & OPF_AllowDouble))
	{
		FirstArgumentType = A->Type.GetPrimitive().ToScalarKind(EScalarKind::Float);
	}

	// Verify that the first argument type is primitive.
	FValueRef Arguments[] = { A, B, C, nullptr };
	static const TCHAR* ArgumentsStr[] = { TEXT("first"), TEXT("second"), TEXT("third") };

	for (int32 i = 0; Arguments[i]; ++i)
	{
		// Check this argument type i primitive.
		Arguments[i] = Emitter.CheckIsPrimitive(Arguments[i]);
		if (!Arguments[i].IsValid())
		{
			return FType::MakePoison();
		}

		EOperatorParameterFlags Filter = Signature->ParameterFlags[i];
		check(Filter != OPF_Unknown); // No signature specified for this operator.

		if (Filter & OPF_CastToFirstArgumentType)
		{
			check(i > 0); // This check can't apply to the first argument.
			Arguments[i] = Emitter.Cast(Arguments[i], FirstArgumentType);
		}
		else if ( 
			// Cast argument to float when...
			// ...the argument should be cast to any float (and it's not a float already)
			((Filter & OPF_CastToAnyFloat) && !Arguments[i]->Type.GetPrimitive().IsAnyFloat())
			// ...or the argument is not allowed to be a double and it is.
			|| (!(Filter & OPF_AllowDouble) && Arguments[i]->Type.GetPrimitive().IsDouble()))
		{
			Arguments[i] = Emitter.CastToFloatKind(Arguments[i]);
			check(!Arguments[i].IsPoison());
		}
		
		if (Filter & OPF_CheckIsBoolean)
		{
			Arguments[i] = Emitter.CheckIsBoolean(Arguments[i]); 
		}
			
		if (Filter & OPF_CheckIsArithmetic)
		{
			Arguments[i] = Emitter.CheckIsArithmetic(Arguments[i]); 
		}

		if (Filter & OPF_CheckIsInteger)
		{
			Arguments[i] = Emitter.CheckIsInteger(Arguments[i]); 
		}
		
		if (Filter & OPF_CheckIsMatrix)
		{
			Arguments[i] = Emitter.CheckIsMatrix(Arguments[i]); 
		}

		if (Filter & OPF_CheckIsNotMatrix)
		{
			Arguments[i] = Emitter.CheckIsScalarOrVector(Arguments[i]); 
		}

		if (Filter & OPF_CheckIsVector3)
		{
			if (!Arguments[i]->Type.GetPrimitive().IsRowVector() || Arguments[i]->Type.GetPrimitive().NumComponents() != 3)
			{
				Emitter.Errorf(Arguments[i], TEXT("Expected a 3D vector."));
				Arguments[i] = Arguments[i].ToPoison();
			}
		}

		// The following checks are only applicable if the argument is constant.
		if (FConstant* Constant = Arguments[i]->As<FConstant>())
		{
			if (Filter & OPF_CheckIsNonZeroFloatConst)
			{
				check((Filter & OPF_CastToAnyFloat) || (Filter & OPF_CastToCommonFloatType));
				if (Constant->Float == 0)
				{
					Emitter.Errorf(Arguments[i], TEXT("Expected non-zero value."));
					Arguments[i] = Arguments[i].ToPoison();
				}
			}

			if (Filter & OPF_CheckIsNonNegativeFloatConst)
			{
				check((Filter & OPF_CastToAnyFloat) || (Filter & OPF_CastToCommonFloatType));
				if (Constant->Float < 0)
				{
					Emitter.Errorf(Arguments[i], TEXT("Expected non-negative value."));
					Arguments[i] = Arguments[i].ToPoison();
				}
			}

			if (Filter & OPF_CheckIsOneOrGreaterFloatConst)
			{
				check((Filter & OPF_CastToAnyFloat) || (Filter & OPF_CastToCommonFloatType));
				if (Constant->Float < 1)
				{
					Emitter.Errorf(Arguments[i], TEXT("Expected a value equal or greater than 1."));
					Arguments[i] = Arguments[i].ToPoison();
				}
			}

			if (Filter & OPF_CheckIsBetweenMinusOneAndPlusOneFloatConst)
			{
				check((Filter & OPF_CastToAnyFloat) || (Filter & OPF_CastToCommonFloatType));
				if (Constant->Float < -1 || Constant->Float > 1)
				{
					Emitter.Errorf(Arguments[i], TEXT("Expected a value greater than -1 and lower than 1."));
					Arguments[i] = Arguments[i].ToPoison();
				}
			}
		}
	}

	if (Arguments[0].IsPoison() || Arguments[1].IsPoison() || Arguments[2].IsPoison())
	{
		return FType::MakePoison();
	}
	
	// Whether any argument will require to be cast to common type/scalar kind.
	bool bRequiresArgumentsCommonType = (Signature->ParameterFlags[0] & OPF_CastToCommonType) || (Signature->ParameterFlags[1] & OPF_CastToCommonType) || (Signature->ParameterFlags[2] & OPF_CastToCommonType);
	bool bRequiresArgumentsCommonScalarKind = (Signature->ParameterFlags[0] & OPF_CastToCommonScalarKind) || (Signature->ParameterFlags[1] & OPF_CastToCommonScalarKind) || (Signature->ParameterFlags[2] & OPF_CastToCommonScalarKind);

	// SPECIAL CASE:  The select operator is special insofar as its first argument is a boolean, while the second and third can be any primitive type.
	if (Op == TO_Select)
	{
		// Cast the second and third argument types to primitive. This is safe as it was already checked earlier.
		const FPrimitive& BPrimitiveType = Arguments[1]->Type.GetPrimitive();
		const FPrimitive& CPrimitiveType = Arguments[2]->Type.GetPrimitive();

		// Compute the maximum number of vector components between all arguments. We know they're scalar or vectors, as it was checked before.
		int32 MaxNumComponents = FMath::Max3(FirstArgumentType.GetPrimitive().NumComponents(), BPrimitiveType.NumComponents(), CPrimitiveType.NumComponents());

		// Cast the first argument (the boolean condition) to a bool vector of the maximum number of components.
		Arguments[0] = Emitter.Cast(Arguments[0], FType::MakeVector(EScalarKind::Bool, MaxNumComponents));

		// Compute the common type between the second and third argument types with a number of components equal to the max of all three.
		FType CommonTypeBetweenSecondAndThirdArguments = Emitter.TryGetCommonType(
			FType::MakeVector(BPrimitiveType.ScalarKind, MaxNumComponents),
			FType::MakeVector(CPrimitiveType.ScalarKind, MaxNumComponents));
		
		// Getting the common type should always be possible.
		check(CommonTypeBetweenSecondAndThirdArguments);

		// Cast second and third arguments to their common type.
		Arguments[1] = Emitter.Cast(Arguments[1], CommonTypeBetweenSecondAndThirdArguments);
		Arguments[2] = Emitter.Cast(Arguments[2], CommonTypeBetweenSecondAndThirdArguments);
	}
	else if (bRequiresArgumentsCommonType || bRequiresArgumentsCommonScalarKind)
	{
		// Determine the common type and scalar kind (if needeed)
		// Note: these two cannot be unified, because we can always determine the common (biggest) scalar kind between two
		// primitive types, but not always can be determined a common type (e.g. a float3 with a float4x4)
		FType ArgumentsCommonType{};
		if (bRequiresArgumentsCommonType)
		{
			ArgumentsCommonType = Emitter.GetCommonType({ Arguments[0], Arguments[1], Arguments[2] });
			if (ArgumentsCommonType.IsPoison())
			{
				return FType::MakePoison();
			}
		}

		EScalarKind ArgumentsCommonScalarKind = EScalarKind::Bool;
		if (bRequiresArgumentsCommonScalarKind)
		{
			for (int32 i = 0; Arguments[i]; ++i)
			{
				ArgumentsCommonScalarKind = (EScalarKind)FMath::Max((int)ArgumentsCommonScalarKind, (int)Arguments[i]->Type.GetPrimitive().ScalarKind);
			}
		}

		// Cast every argument with the `CastToCommon` to the common type, if necessary.
		for (int32 i = 0; Arguments[i]; ++i)
		{
			EOperatorParameterFlags Filter = Signature->ParameterFlags[i];
			if (Filter & OPF_CastToCommonScalarKind)
			{
				Arguments[i] = Emitter.CastToScalarKind(Arguments[i], ArgumentsCommonScalarKind);
			}
			else if (Filter & OPF_CastToCommonType)
			{
				check(bRequiresArgumentsCommonType);

				// Lerp doesn't accept double for its third input, so we need to check if double is allowed per input when casting to the common type.
				FType ToType = (!(Filter & OPF_AllowDouble) && ArgumentsCommonType.IsDouble()) 
					? ArgumentsCommonType.GetPrimitive().ToScalarKind(EScalarKind::Float)
					: ArgumentsCommonType;

				Arguments[i] = Emitter.Cast(Arguments[i], ToType);
			}
		}
	}
	
	if (Arguments[0].IsPoison() || Arguments[1].IsPoison() || Arguments[2].IsPoison())
	{
		return FType::MakePoison();
	}

	// Arguments might have changed, update the references.
	A = Arguments[0];
	B = Arguments[1];
	C = Arguments[2];

	// Finally, determine operator result type.
	const FPrimitive& FirstArgumentPrimitiveType = Arguments[0]->Type.GetPrimitive();
	switch (Signature->Result)
	{
		case OR_Unknown:
			UE_MIR_UNREACHABLE(); // missing operator signature declaration

		case OR_FirstArgumentType:
			// Some operators accept LWC as input, but always output non-LWC (examples: modulo, sign, sine, cosine, saturate, frac). Anything that may output LWC should use OR_FirstArgumentTypeLWC.
			return FirstArgumentPrimitiveType.IsDouble() ? FirstArgumentPrimitiveType.ToScalarKind(EScalarKind::Float) : Arguments[0]->Type;

		case OR_FirstArgumentTypeAllowDouble:
			return Arguments[0]->Type;

		case OR_BooleanWithFirstArgumentDimensions:
			return FType::MakePrimitive(EScalarKind::Bool, FirstArgumentPrimitiveType.NumRows, FirstArgumentPrimitiveType.NumColumns);

 		case OR_FirstArgumentTypeToScalarLWC:
			return FirstArgumentPrimitiveType.ToScalar();

		case OR_SecondArgumentType:
			return B->Type;

		case OR_MatrixMultiplyResult:
		{
			FType SecondArgumentType = Arguments[1]->Type;

			FPrimitive LhsPrimitiveType = FirstArgumentType.GetPrimitive();
			FPrimitive RhsPrimitiveType = SecondArgumentType.GetPrimitive();

			int32 RhsRows = RhsPrimitiveType.NumRows;
			int32 RhsColumns = RhsPrimitiveType.NumColumns;
			int32 OutputRows = LhsPrimitiveType.NumRows;
			int32 OutputColumns = RhsPrimitiveType.NumColumns;

			// When multiplying matrix * vector, we reinterpret the input as a column vector (NumColumns == 1), even though by default our vectors are row vectors.
			// And the output is reinterpreted back as a row vector.
			if (SecondArgumentType.IsVector())
			{
				RhsRows = RhsPrimitiveType.NumColumns;
				RhsColumns = 1;
				OutputRows = 1;
				OutputColumns = LhsPrimitiveType.NumRows;
			}

			if (FirstArgumentType.GetPrimitive().NumColumns != RhsRows)
			{
				Emitter.Errorf({}, TEXT("Cannot matrix multiply a '%s' with a '%s'."), *FirstArgumentType.GetSpelling(), *SecondArgumentType.GetSpelling());
				return FType::MakePoison();
			}

			return FType::MakePrimitive(LhsPrimitiveType.ScalarKind, OutputRows, OutputColumns);
		}
	}

	UE_MIR_UNREACHABLE();
}

// Returns whether the operator supports componentwise application. In other words, if the following is true:
// 	op(v, w) == [op(v_0, w_0), ..., op(v_n, w_n)]
static bool IsComponentwiseOperator(EOperator Op)
{
	return Op != BO_Dot && Op != BO_Cross && Op != BO_MatrixMultiply;
}

// Tries to fold the operator by applying the operator componentwise on arguments components.
// If a value is returned, it will be a composite with some component folded to a constant. If some argument
// isn't a composite, or all arguments components are non-constant, the folding will not be carried out.
// If no folding is carried out, this function simply returns nullptr.
static FValue* TryFoldComponentwiseOperator(FEmitter& Emitter, EOperator Op, FValue* A, FValue* B, FValue* C, FType ResultType)
{
	// Check that at least one component of the resulting composite value would folded.
	// If all components of resulting composite value are not folded, then instead of emitting
	// an individual operator instruction for each component, simply emit a single binary operator
	// instruction applied between lhs and rhs as a whole. (v1 + v2 rather than float2(v1.x + v2.x, v1.y + v2.y)
	bool bSomeResultComponentWasFolded = false;
	bool bResultIsIdenticalToA = true;
	bool bResultIsIdenticalToB = true;
	bool bResultIsIdenticalToC = true;

	// Allocate the temporary array to store the folded component results
	TTemporaryArray<FValue*> TempResultComponents{ ResultType.GetPrimitive().NumComponents() };
	
	for (int32 i = 0; i < TempResultComponents.Num(); ++i)
	{
		// Extract the arguments individual components
		FValue* AComponent = Emitter.Subscript(A, i);
		FValue* BComponent = B ? Emitter.Subscript(B, i).Value : nullptr;
		FValue* CComponent = C ? Emitter.Subscript(C, i).Value : nullptr;

		// Try folding the operation, it may return null
		FValue* ResultComponent = TryFoldOperatorScalar(Emitter, Op, AComponent, BComponent, CComponent, ResultType);

		// Update the flags
		bSomeResultComponentWasFolded |= (bool)ResultComponent;
		bResultIsIdenticalToA &= ResultComponent && ResultComponent->Equals(AComponent);
		bResultIsIdenticalToB &= BComponent && ResultComponent && ResultComponent->Equals(BComponent);
		bResultIsIdenticalToC &= CComponent && ResultComponent && ResultComponent->Equals(CComponent);

		// Cache the results
		TempResultComponents[i] = ResultComponent;
	}

	// If result is identical to either lhs or rhs, simply return it
	if (bResultIsIdenticalToA)
	{
		return A;
	}
	else if (bResultIsIdenticalToB)
	{
		return B;
	}
	else if (bResultIsIdenticalToC)
	{
		return C;
	}

	// If some component was folded (it is either constant or the operation was a NOP), it is worth
	// build the operation as a separate operation for each component, that is like
	//    float2(a.x + b.x, a.y + b.y)
	// rather than
	//    a + b
	// so that we retain as much compile-time information as possible.
	if (bSomeResultComponentWasFolded)
	{
		// If result type is scalar, simply return the single folded result (instead of creating a composite value)
		if (ResultType.GetPrimitive().IsScalar())
		{
			check(TempResultComponents[0]);
			return TempResultComponents[0];
		}

		// Make the new composite value
		FComposite* Result = MakeCompositePrototype(Emitter, ResultType, TempResultComponents.Num());

		// Fetch the components array from the result composite
		TArrayView<FValue*> ResultComponents = Result->GetMutableComponents();

		// Also cache the type of a single component
		FType ComponentType = ResultType.GetPrimitive().ToScalar();

		// Create the operator instruction for each component pair
		for (int32 i = 0; i < TempResultComponents.Num(); ++i)
		{
			// Reuse cached result if possible
			ResultComponents[i] = TempResultComponents[i];

			// Otherwise emit the binary operation between the two components (this will create a new instruction)
			if (!ResultComponents[i])
			{
				FOperator Proto = MakePrototype<FOperator>(ComponentType);
				Proto.Op = Op;
				Proto.AArg = Emitter.Subscript(A, i);
				Proto.BArg = B ? Emitter.Subscript(B, i).Value : nullptr;
				Proto.CArg = C ? Emitter.Subscript(C, i).Value : nullptr;
				ResultComponents[i] = EmitPrototype(Emitter, Proto);
			}
		}
		
		return EmitPrototype(Emitter, *Result);
	}

	return {};
}

// If V is a composite and all its components are constants, it unpacks the components into OutComponents and returns true.
// If this is not possible for any reason, it returns false.
static bool TryUnpackConstantScalarOrVector(FValue* V, TArrayView<FConstant*> OutComponents, int32& OutNumComponents)
{
	// V not specified? Or not a scalar/vector?
	FComposite* Composite = As<FComposite>(V);
	if (!Composite || V->Type.AsPrimitive()->IsMatrix())
	{
		return false;
	}

	TConstArrayView<FValue*> Components = Composite->GetComponents();
	for (int32 i = 0; i < Components.Num(); ++i)
	{
		OutComponents[i] = As<FConstant>(Components[i]);
		if (!OutComponents[i])
		{
			return false;
		}
	}

	OutNumComponents = Components.Num();
	return true;
}

// Computes the dot product on two arrays of constant float components.
static TFloat ConstantDotFloat(TArrayView<FConstant*> AComponents, TArrayView<FConstant*> BComponents, int32 NumComponents)
{
	float Result = 0.0f;
	for (int32 i = 0; i < NumComponents; ++i)
	{
		Result += AComponents[i]->Float * BComponents[i]->Float;
	}
	return Result;
}

static MIR::TDouble ConstantDotDouble(TArrayView<FConstant*> AComponents, TArrayView<FConstant*> BComponents, int32 NumComponents)
{
	double Result = 0.0;
	for (int32 i = 0; i < NumComponents; ++i)
	{
		Result += AComponents[i]->Double * BComponents[i]->Double;
	}
	return Result;
}

// Tries to fold the operator, that is to evaluate its result now at translation time if its arguments are constant.
// If the operator could not be folded in any way, it returns nullptr.
static FValueRef TryFoldOperator(FEmitter& Emitter, EOperator Op, FValueRef A, FValueRef B, FValueRef C, FType ResultType)
{
	// First, try to apply some operator identity to simplify the operator.
	if (FValueRef Simplified = TrySimplifyOperator(Emitter, Op, A, B, C, ResultType))
	{
		return Simplified;
	}

	FConstant* AComponents[4];
	int32      ANumComponents;
	
	// CASE 1: Some operations like Length, Dot and Cross are not defined on individual scalar components.
	// For instance length(V) is not the same as [length(V.x), ..., length(V.z)]. These operations
	// folding is handled here as special cases.
	// First, try to unpack the first argument to an array of constants.
	if (TryUnpackConstantScalarOrVector(A, AComponents, ANumComponents))
	{
		FConstant* BComponents[4];
		int32      BNumComponents;
	
		if (Op == UO_Length)
		{
			if (ResultType.GetPrimitive().IsFloat())
			{
				float Result = FMath::Sqrt(ConstantDotFloat(AComponents, AComponents, ANumComponents));
				return Emitter.ConstantFloat(Result);
			}
			else if (ResultType.GetPrimitive().IsDouble())
			{
				double Result = FMath::Sqrt(ConstantDotDouble(AComponents, AComponents, ANumComponents));
				return Emitter.ConstantDouble(Result);
			}
			else
			{
				UE_MIR_UNREACHABLE();
			}
		}
		else if ((Op == BO_Dot || Op == BO_Cross) && TryUnpackConstantScalarOrVector(B, BComponents, BNumComponents))
		{
			// Verified before the operation is folded, here as a safety check.
			check(ANumComponents == BNumComponents);

			if (Op == BO_Dot)
			{
				if (ResultType.GetPrimitive().IsFloat())
				{
					float Result = ConstantDotFloat(AComponents, BComponents, ANumComponents);
					return Emitter.ConstantFloat(Result);
				}
				else if (ResultType.GetPrimitive().IsDouble())
				{
					double Result = ConstantDotDouble(AComponents, BComponents, ANumComponents);
					return Emitter.ConstantDouble(Result);
				}
				else
				{
					UE_MIR_UNREACHABLE();
				}
			}
			else
			{
				check(Op == BO_Cross);
				if (ResultType.GetPrimitive().IsFloat())
				{
					FVector3f AVector{ AComponents[0]->Float, AComponents[1]->Float, AComponents[2]->Float };
					FVector3f BVector{ BComponents[0]->Float, BComponents[1]->Float, BComponents[2]->Float };
					FVector3f Result = AVector.Cross(BVector);
					return Emitter.ConstantFloat3(Result);
				}
				else if (ResultType.GetPrimitive().IsDouble())
				{
					UE_MIR_TODO();
				}
				else
				{
					UE_MIR_UNREACHABLE();
				}
			}
		}
	}

	// CASE 2: If the operation supports componentwise application, try folding the operator componentwise.
	if (IsComponentwiseOperator(Op))
	{
		return TryFoldComponentwiseOperator(Emitter, Op, A, B, C, ResultType);
	}

	// No folding was possible, simply return null to indicate this.
	return {};
}

FValueRef FEmitter::Operator(EOperator Op, FValueRef A, FValueRef B, FValueRef C)
{
	// Transpose is a translation-time operation only that never creates a runtime Operator instruction.
	if (Op == UO_Transpose)
	{
		return Transpose(A);
	}

	if (!A.IsValid() || (B && !B.IsValid()) || (C && !C.IsValid()))
	{
		return Poison();
	}

	// Validate the operation and retrieve the result type.
	FType ResultType = ValidateOperatorAndGetResultType(*this, Op, A, B, C);
	if (!ResultType)
	{
		return Poison();
	}

	FValueRef Result;

	// Try folding the operator first.
	if (FValueRef FoldedValue = TryFoldOperator(*this, Op, A, B, C, ResultType))
	{
		Result = FoldedValue;
	}
	else
	{
		// Otherwise, we must emit a new instruction that executes the operator.
		FOperator Proto = MakePrototype<FOperator>(ResultType);
		Proto.Op = Op;
		Proto.AArg = A;
		Proto.BArg = B;
		Proto.CArg = C;

		Result = EmitPrototype(*this, Proto);
	}

	// Subtract has a special case option to automatically truncate when subtracting two double-precision inputs from each other, assuming this is a
	// transition from double-precision space to relative space, and no longer needs to be double-precision.
	// We need to check that all arguments are double-precision before the call to ValidateOperatorAndGetResultType, as that may cast the inputs, changing them.
	if (Op == BO_Subtract && UE::MaterialTranslatorUtils::GetLWCTruncateMode() == 2 && A->Type.IsDouble() && B->Type.IsDouble())
	{
		Result = CastToFloatKind(Result);
	}

	return Result;
} 

FValueRef FEmitter::Branch(FValueRef Condition, FValueRef True, FValueRef False)
{
	if (IsAnyNotValid(Condition, True, False))
	{
		return Poison();
	}

	// Condition must be of type bool
	Condition = Cast(Condition, FType::MakeBoolScalar());
	if (!Condition)
	{
		return Poison();
	}	

	// If the condition is a scalar constant, then simply evaluate the result now.
	if (const FConstant* ConstCondition = As<FConstant>(Condition))
	{
		return ConstCondition->Boolean ? True : False;
	}

	// If the condition is not static, make both true and false arguments have the same type,
	// by casting false argument into the true's type.
	FType CommonType = GetCommonType({ True, False });
	if (!CommonType)
	{
		return Poison();
	}

	True = Cast(True, CommonType);
	False = Cast(False, CommonType);
	if (!True || !False)
	{
		return Poison();
	}

	// Create the branch instruction.
	FBranch Proto = MakePrototype<FBranch>(CommonType);
	Proto.ConditionArg = Condition;
	Proto.TrueArg = True;
	Proto.FalseArg = False;

	return EmitPrototype(*this, Proto);
}

FValueRef FEmitter::Subscript(FValueRef Value, int32 Index)
{
	if (!Value.IsValid())
	{
		return Value;
	}
	
	// Subscripting a composite by index is always possible and simply yields the i-th component.
	if (FComposite* Composite = As<FComposite>(Value))
	{
		check(Index < Composite->GetComponents().Num());
		return Value.To(Composite->GetComponents()[Index]);
	}
	
	// Other operations supported if value is primitive. Check it first.
	TOptional<FPrimitive> PrimitiveType = Value->Type.AsPrimitive();
	if (!PrimitiveType)
	{
		Errorf(Value, TEXT("Value of type '%s' cannot be subscripted."), *Value->Type.GetSpelling());
		return Value.ToPoison();
	}

	// Getting first component and Value is already a scalar, just return itself.
	if (Index == 0 && PrimitiveType->IsScalar())
	{
		return Value;
	}

	if (Index >= PrimitiveType->NumComponents())
	{
		Errorf(Value, TEXT("Value of type '%s' has fewer dimensions than subscript index `%d`."), *Value->Type.GetSpelling(), Index);
		return Value.ToPoison();
	}

	if (PrimitiveType->IsMatrix() && PrimitiveType->IsDouble())
	{
		Errorf(Value, TEXT("Cannot subscript a double-precision matrix."));
		return Value.ToPoison();
	}

	// Avoid subscripting a subscript (e.g. no value.xy.x)
	if (FSubscript* Subscript = As<FSubscript>(Value))
	{
		Value = Value.To(Subscript->Arg);
	}

	// We can't resolve it at compile time: emit subscript value.
	FSubscript Prototype = MakePrototype<FSubscript>(PrimitiveType->ToScalar());
	Prototype.Arg = Value;
	Prototype.Index = Index;

	return Value.To(EmitPrototype(*this, Prototype));
}

FValueRef FEmitter::Swizzle(FValueRef Value, FSwizzleMask Mask)
{
	if (!Value.IsValid())
	{
		return Value;
	}

	// At least one component must have been specified.
	if (Mask.NumComponents <= 0)
	{
		Errorf(Value, TEXT("Swizzle mask has no components."));
		return Value.ToPoison();
	}

	// We can only swizzle on non-matrix primitive types.
	if (!Value->Type.AsPrimitive() || Value->Type.GetPrimitive().IsMatrix())
	{
		Errorf(Value, TEXT("Cannot swizzle a '%s' value."), *Value->Type.GetSpelling());
		return Value.ToPoison();
	}
	
	// For brevity
	FPrimitive PrimitiveType = Value->Type.GetPrimitive();
	int NumComponents = PrimitiveType.NumComponents();

	// Make sure each component in the mask fits the number of components in Value.
	for (EVectorComponent Component : Mask)
	{
		if ((int32)Component >= NumComponents)
		{
			Errorf(Value, TEXT("Value of type '%s' has no component '%s'."), *Value->Type.GetSpelling(), VectorComponentToString(Component));
			return Value.ToPoison();
		}
	}

	// If the requested number of components is the same as Value and the order in which the components
	// are specified in the mask is sequential (e.g. x, y, z) then this is a no op, simply return Value as is.
	if (Mask.NumComponents == NumComponents)
	{
		bool InOrder = true;
		for (int32 i = 0; i < Mask.NumComponents; ++i)
		{
			if (Mask.Components[i] != (EVectorComponent)i)
			{
				InOrder = false;
				break;
			}
		}
		if (InOrder)
		{
			return Value;
		}
	}
	
	// If only one component is requested, we can use Subscript() to return the single component.
	if (Mask.NumComponents == 1)
	{
		return Value.To(Subscript(Value, (int32)Mask.Components[0]));
	}

	// Make the result vector type.
	FType ResultType = FType::MakeVector(PrimitiveType.ScalarKind, Mask.NumComponents);
	FComposite* Result = MakeCompositePrototype(*this, ResultType, Mask.NumComponents);
	for (int32 i = 0; i < Mask.NumComponents; ++i)
	{
		Result->GetMutableComponents()[i] = Subscript(Value, (int32)Mask.Components[i]);
	}

	return Value.To(EmitPrototype(*this, *Result));
}

FValueRef FEmitter::Transpose(FValueRef Value)
{
	if (!Value.IsValid())
	{
		return Value;
	}

	TOptional<FPrimitive> PrimitiveType = Value->Type.AsPrimitive();
	if (!PrimitiveType)
	{
		Errorf(Value, TEXT("Cannot transpose a non primitive value of type '%s'."), *Value->Type.GetSpelling());
		return Value.ToPoison();
	}

	// A transposed scalar is itself.
	if (PrimitiveType->IsScalar())
	{
		return Value;
	}

	// Build the result type (swap rows and columns)
	const int32 OrigRows = PrimitiveType->NumRows;
    const int32 OrigColumns = PrimitiveType->NumColumns;
    FType ResultType = FType::MakePrimitive(PrimitiveType->ScalarKind, OrigColumns, OrigRows);

    // Make a composite prototype with space for all components
    FComposite* Prototype = MakeCompositePrototype(*this, ResultType, OrigRows * OrigColumns);

	// Transpose the components
	TArrayView<FValue*> Components = Prototype->GetMutableComponents();
	for (int32 i = 0, NumComponents = OrigRows * OrigColumns; i < NumComponents; ++i) {
		int32 OrigRow = i % OrigRows;
		int32 OrigColumn = i / OrigRows;
		Components[i] = Subscript(Value, OrigRow * OrigColumns + OrigColumn);
	}

	return EmitPrototype(*this, *Prototype);
}

static FValue* CastConstant(FEmitter& Emitter, FConstant* Constant, EScalarKind ConstantScalarKind, EScalarKind TargetKind)
{
	if (ConstantScalarKind == TargetKind)
	{
		return Constant;
	}

	switch (ConstantScalarKind)
	{
		case EScalarKind::Bool:
		{
			switch (TargetKind)
			{
				case EScalarKind::Int: return Emitter.ConstantInt(Constant->Boolean ? 1 : 0);
				case EScalarKind::Float: return Emitter.ConstantFloat(Constant->Boolean ? 1.0f : 0.0f);
				case EScalarKind::Double: return Emitter.ConstantDouble(Constant->Boolean ? 1.0 : 0.0);
				default: UE_MIR_UNREACHABLE();
			}
		}

		case EScalarKind::Int:
		{
			switch (TargetKind)
			{
				case EScalarKind::Bool: return Emitter.ConstantBool(Constant->Integer != 0);
				case EScalarKind::Float: return Emitter.ConstantFloat((TFloat)Constant->Integer);
				case EScalarKind::Double: return Emitter.ConstantDouble(Constant->Integer);
				default: UE_MIR_UNREACHABLE();
			}
		}

		case EScalarKind::Float:
		{
			switch (TargetKind)
			{
				case EScalarKind::Bool: return Emitter.ConstantBool(Constant->Float != 0.0f);
				case EScalarKind::Int: return Emitter.ConstantInt((int32)Constant->Float);
				case EScalarKind::Double: return Emitter.ConstantDouble(Constant->Float);
				default: UE_MIR_UNREACHABLE();
			}
		}

		case EScalarKind::Double:
		{
			switch (TargetKind)
			{
				case EScalarKind::Bool: return Emitter.ConstantBool(Constant->Double != 0.0);
				case EScalarKind::Int: return Emitter.ConstantInt((int32)Constant->Double);
				case EScalarKind::Float: return Emitter.ConstantFloat((float)Constant->Double);
				default: UE_MIR_UNREACHABLE();
			}
		}

		default: break;
	}

	UE_MIR_UNREACHABLE();
}

static FValueRef CastToPrimitive(FEmitter& Emitter, FValueRef Value, FType TargetType)
{
	if (!Value->Type.AsPrimitive())
	{
		Emitter.Errorf(Value, TEXT("Cannot construct a '%s' from non primitive type '%s'."), *Value->Type.GetSpelling(), *TargetType.GetSpelling());
		return Value.ToPoison();
	}

	FPrimitive ValuePrimitiveType = Value->Type.GetPrimitive();
	FPrimitive TargetPrimitiveType = TargetType.GetPrimitive();

	// Construct a scalar from another scalar.
	if (TargetPrimitiveType.IsScalar())
	{
		// Get the first component of value. We already know value's type is primitive, so this will return a scalar.
		Value = Emitter.Subscript(Value, 0);

		ValuePrimitiveType = Value->Type.GetPrimitive();
		check(ValuePrimitiveType.IsScalar());
		
		if (ValuePrimitiveType == TargetPrimitiveType)
		{
			// If types are identical, return the component value as is.
			return Value;
		}
		else if (FConstant* ConstantInitializer = As<FConstant>(Value))
		{
			// If value is a constant, we can cast the constant now.
			return CastConstant(Emitter, ConstantInitializer, ValuePrimitiveType.ScalarKind, TargetPrimitiveType.ScalarKind);
		}
		else
		{
			// Otherwise emit a cast instruction of the subscript value to the target type.
			FScalar Prototype = MakePrototype<FScalar>(TargetType);
			Prototype.Arg = Value;
			return EmitPrototype(Emitter, Prototype);
		}
	}

	// Construct a vector or matrix from a scalar. E.g. 3.14f -> float3(3.14f, 3.14f, 3.14f)
	// Note: we know target isn't scalar as it's been handled above.
	if (ValuePrimitiveType.IsScalar())
	{
		// Create the result composite value.
		FComposite* Result = MakeCompositePrototype(Emitter, TargetType, TargetPrimitiveType.NumComponents());

		// Create a composite and initialize each of its components to the conversion
		// of initializer value to the single component type.
		FValue* Component = Emitter.Cast(Value, TargetPrimitiveType.ToScalar());

		// Get the mutable array of components.
		TArrayView<FValue*> ResultComponents = Result->GetMutableComponents();

		// Initialize all result components to the same scalar.
		for (int32 i = 0; i < TargetPrimitiveType.NumComponents(); ++i)
		{
			ResultComponents[i] = Component;
		}
		
		return EmitPrototype(Emitter, *Result);
	}

	// Construct a vector from another vector. If constructed vector is larger, initialize
	// remaining components to zero. If it's smaller, truncate initializer vector and only use
	// the necessary components.
	if (TargetPrimitiveType.IsRowVector() && ValuePrimitiveType.IsRowVector())
	{
		// #todo-massimo.tristano Use swizzle when scalartypes are the same, and target num components is less than initializer's.

		int32 TargetNumComponents = TargetPrimitiveType.NumComponents();
		int32 InitializerNumComponents = ValuePrimitiveType.NumComponents();

		// Create the result composite value.
		FComposite* Result = MakeCompositePrototype(Emitter, TargetType, TargetPrimitiveType.NumComponents());
		TArrayView<FValue*> ResultComponents = Result->GetMutableComponents();

		// Determine the result component type (scalar).
		FType ResultComponentType = TargetPrimitiveType.ToScalar();

		// For iterating over the components of the result composite value.
		int32 Index = 0;
		
		// Convert components from the initializer vector.
		const int32 MinNumComponents = FMath::Min(TargetNumComponents, InitializerNumComponents);
		for (; Index < MinNumComponents; ++Index)
		{
			ResultComponents[Index] = Emitter.Cast(Emitter.Subscript(Value, Index), ResultComponentType);
		}

		// Initialize remaining result composite components to zero.
		for (; Index < TargetNumComponents; ++Index)
		{
			ResultComponents[Index] = Emitter.ConstantZero(ResultComponentType.GetPrimitive().ScalarKind);
		}

		return EmitPrototype(Emitter, *Result);
	}
	
	// The two primitive types are identical matrices that differ only by their scalar type.
	if (TargetPrimitiveType.NumRows == ValuePrimitiveType.NumRows &&
		TargetPrimitiveType.NumColumns == ValuePrimitiveType.NumColumns)
	{
		check(TargetPrimitiveType.IsMatrix());

		// Create the result composite value.
		FComposite* Result = MakeCompositePrototype(Emitter, TargetType, TargetPrimitiveType.NumComponents());
		TArrayView<FValue*> ResultComponents = Result->GetMutableComponents();
			
		// Determine the result component type (scalar).
		FType ResultComponentType = TargetPrimitiveType.ToScalar();

		// Convert components from the initializer vector.
		for (int32 Index = 0, Num = ResultComponents.Num(); Index < Num; ++Index)
		{
			ResultComponents[Index] = Emitter.Cast(Emitter.Subscript(Value, Index), ResultComponentType);
		}

		return EmitPrototype(Emitter, *Result);
	}

	// Initializer value cannot be used to construct this primitive type.
	return Value.ToPoison();
}

FValueRef FEmitter::Cast(FValueRef Value, FType TargetType)
{
	if (!Value.IsValid())
	{
		return Value;
	}

	// If target type matches initializer's, simply return the same value.
	FType InitializerType = Value->Type;
	if (InitializerType == TargetType)
	{
		return Value;
	}
	
	FValueRef Result = FPoison::Get();
	if (TargetType.AsPrimitive())
	{
		Result = CastToPrimitive(*this, Value, TargetType);
	}

	if (Result->IsPoison())
	{
		// No other legal conversions applicable. Report error if we haven't converted the value.
		Errorf(Value, TEXT("Cannot construct a '%s' from a '%s'."), *TargetType.GetSpelling(), *Value->Type.GetSpelling());
		return Poison();
	}
	
	return Result;
}

FValueRef FEmitter::CastToScalar(FValueRef Value)
{
	Value = CheckIsPrimitive(Value);
	return Value.IsValid()
		? Cast(Value, FType::MakeScalar(Value->Type.GetPrimitive().ScalarKind))
		: Value;
}

FValueRef FEmitter::CastToVector(FValueRef Value, int32 NumColumns)
{
	Value = CheckIsPrimitive(Value);
	return Value.IsValid()
		? Cast(Value, FType::MakeVector(Value->Type.GetPrimitive().ScalarKind, NumColumns))
		: Value;
}

FValueRef FEmitter::CastToScalarKind(FValueRef Value, EScalarKind ToScalarKind)
{
	Value = CheckIsPrimitive(Value);
	return Value.IsValid()
		? Cast(Value, Value->Type.GetPrimitive().ToScalarKind(ToScalarKind))
		: Value;
}

FValueRef FEmitter::CastToBoolKind(FValueRef Value)
{
	return CastToScalarKind(Value, EScalarKind::Bool);
}

FValueRef FEmitter::CastToIntKind(FValueRef Value)
{
	return CastToScalarKind(Value, EScalarKind::Int);
}

FValueRef FEmitter::CastToFloatKind(FValueRef Value)
{
	return CastToScalarKind(Value, EScalarKind::Float);
}

FValueRef FEmitter::CastToBool(FValueRef Value, int NumColumns)
{
	return Cast(Value, FType::MakeVector(EScalarKind::Bool, NumColumns));
}

FValueRef FEmitter::CastToInt(FValueRef Value, int NumColumns)
{
	return Cast(Value, FType::MakeVector(EScalarKind::Int, NumColumns));
}

FValueRef FEmitter::CastToFloat(FValueRef Value, int NumColumns)
{
	return Cast(Value, FType::MakeVector(EScalarKind::Float, NumColumns));
}

FValueRef FEmitter::StageSwitch(FType Type, TConstArrayView<FValueRef> ValuePerStage)
{
	check(ValuePerStage.Num() <= NumStages);
	FStageSwitch Prototype = MakePrototype<FStageSwitch>(Type);
	for (int i = 0; i < ValuePerStage.Num(); ++i)
	{
		Prototype.Args[i] = ValuePerStage[i];
	}
	return EmitPrototype(*this, Prototype);
}

FValueRef FEmitter::Nop(FValueRef Arg)
{
	// Nop can only have primitive arguments
	Arg = CheckIsPrimitive(Arg);

	if (!Arg.IsValid())
	{
		return Arg;
	}

	MIR::FNop Prototype = MakePrototype<MIR::FNop>(Arg->Type);
	Prototype.Arg = Arg;

	return EmitPrototype(*this, Prototype);
}

FValueRef FEmitter::TextureGather(FValueRef Texture, FValueRef TexCoord, ETextureReadMode GatherMode, const FTextureSampleBaseAttributes& BaseAttributes)
{
	check(GatherMode >= ETextureReadMode::GatherRed && GatherMode <= ETextureReadMode::GatherAlpha);

	if (IsAnyNotValid(Texture, TexCoord))
	{
		return Poison();
	}

	const EMaterialSamplerType SamplerType = MapSamplerTypeForTexture(Texture, BaseAttributes.SamplerType);

	FTextureRead Prototype = MakePrototype<FTextureRead>(FType::MakeFloatVector(4));
	Prototype.TextureObject = Texture;
	Prototype.TexCoord = TexCoord;
	Prototype.Mode = GatherMode;
	Prototype.SamplerSourceMode = BaseAttributes.SamplerSourceMode;
	Prototype.SamplerType = SamplerType;

	if (IsVirtualSamplerType(SamplerType))
	{
		Prototype.VTPageTable = VTPageTableLoadFromSamplerSource(*this, Texture, BaseAttributes, Prototype.TexCoord);
	}

	return EmitPrototype(*this, Prototype);
}

FValueRef FEmitter::TextureSample(FValueRef Texture, FValueRef TexCoord, bool bAutomaticViewMipBias, const FTextureSampleBaseAttributes& BaseAttributes)
{
	if (IsAnyNotValid(Texture, TexCoord))
	{
		return Poison();
	}

	const EMaterialSamplerType SamplerType = MapSamplerTypeForTexture(Texture, BaseAttributes.SamplerType);

	FTextureRead PrototypePixel = MakePrototype<FTextureRead>(FType::MakeFloatVector(4));
	PrototypePixel.TextureObject = Texture;
	PrototypePixel.TexCoord = TexCoord;
	PrototypePixel.Mode = ETextureReadMode::MipAuto;
	PrototypePixel.SamplerSourceMode = BaseAttributes.SamplerSourceMode;
	PrototypePixel.SamplerType = SamplerType;

	FTextureRead PrototypeCompute = PrototypePixel;
	PrototypeCompute.Mode = ETextureReadMode::Derivatives;
	PrototypeCompute.TexCoordDdx = AnalyticalPartialDerivative(TexCoord, EDerivativeAxis::X);
	PrototypeCompute.TexCoordDdy = AnalyticalPartialDerivative(TexCoord, EDerivativeAxis::Y);

	FTextureRead PrototypeVertex = PrototypePixel;
	PrototypeVertex.Mode = ETextureReadMode::MipLevel;
	PrototypeVertex.MipValue = ConstantZero(EScalarKind::Float);

	if (bAutomaticViewMipBias)
	{
		FValueRef ViewMaterialTextureMipBias = ExternalInput(EExternalInput::ViewMaterialTextureMipBias);
		PrototypePixel.Mode = ETextureReadMode::MipBias;
		PrototypePixel.MipValue = ViewMaterialTextureMipBias;

		FValueRef Exp2ViewMaterialTextureMipBias = Operator(UO_Exponential2, ViewMaterialTextureMipBias);
		PrototypeCompute.TexCoordDdx = Operator(BO_Multiply, PrototypeCompute.TexCoordDdx, Exp2ViewMaterialTextureMipBias);
		PrototypeCompute.TexCoordDdy = Operator(BO_Multiply, PrototypeCompute.TexCoordDdy, Exp2ViewMaterialTextureMipBias);
	}

	if (IsVirtualSamplerType(SamplerType))
	{
		PrototypePixel.VTPageTable = VTPageTableLoadFromSamplerSource(*this, Texture, BaseAttributes, PrototypePixel.TexCoord);
		PrototypeCompute.VTPageTable = VTPageTableLoadFromSamplerSource(*this, Texture, BaseAttributes, PrototypeCompute.TexCoord, PrototypeCompute.TexCoordDdx, PrototypeCompute.TexCoordDdy);
		PrototypeVertex.VTPageTable = VTPageTableLoadFromSamplerSource(*this, Texture, BaseAttributes, PrototypeVertex.TexCoord);
	}

	FStageSwitch StageSwitch = MakePrototype<FStageSwitch>(PrototypePixel.Type);
	StageSwitch.Args[Stage_Vertex] = EmitPrototype(*this, PrototypeVertex);
	StageSwitch.Args[Stage_Pixel] = EmitPrototype(*this, PrototypePixel);
	StageSwitch.Args[Stage_Compute] = EmitPrototype(*this, PrototypeCompute);

	return EmitPrototype(*this, StageSwitch);
}

FValueRef FEmitter::TextureSampleLevel(FValueRef Texture, FValueRef TexCoord, FValueRef MipLevel, bool bAutomaticViewMipBias, const FTextureSampleBaseAttributes& BaseAttributes)
{
	if (IsAnyNotValid(Texture, TexCoord, MipLevel))
	{
		return Poison();
	}

	const EMaterialSamplerType SamplerType = MapSamplerTypeForTexture(Texture, BaseAttributes.SamplerType);

	FTextureRead Prototype = MakePrototype<FTextureRead>(FType::MakeFloatVector(4));
	Prototype.TextureObject = Texture;
	Prototype.TexCoord = TexCoord;
	Prototype.MipValue = MipLevel;
	Prototype.Mode = ETextureReadMode::MipLevel;
	Prototype.SamplerSourceMode = BaseAttributes.SamplerSourceMode;
	Prototype.SamplerType = SamplerType;

	if (bAutomaticViewMipBias)
	{
		Prototype.MipValue = Operator(BO_Add, MipLevel, ExternalInput(EExternalInput::ViewMaterialTextureMipBias));
	}

	if (IsVirtualSamplerType(SamplerType))
	{
		Prototype.VTPageTable = VTPageTableLoadFromSamplerSource(*this, Texture, BaseAttributes, Prototype.TexCoord, {}, {}, TMVM_MipLevel, Prototype.MipValue);
	}

	return EmitPrototype(*this, Prototype);
}

FValueRef FEmitter::TextureSampleBias(FValueRef Texture, FValueRef TexCoord, FValueRef MipBias, bool bAutomaticViewMipBias, const FTextureSampleBaseAttributes& BaseAttributes)
{
	if (IsAnyNotValid(Texture, TexCoord, MipBias))
	{
		return Poison();
	}

	if (bAutomaticViewMipBias)
	{
		MipBias = Operator(BO_Add, MipBias, ExternalInput(EExternalInput::ViewMaterialTextureMipBias));
	}

	const EMaterialSamplerType SamplerType = MapSamplerTypeForTexture(Texture, BaseAttributes.SamplerType);

	FTextureRead PrototypePixel = MakePrototype<FTextureRead>(FType::MakeFloatVector(4));
	PrototypePixel.TextureObject = Texture;
	PrototypePixel.TexCoord = TexCoord;
	PrototypePixel.MipValue = MipBias;
	PrototypePixel.Mode = ETextureReadMode::MipBias;
	PrototypePixel.SamplerSourceMode = BaseAttributes.SamplerSourceMode;
	PrototypePixel.SamplerType = SamplerType;

	FTextureRead PrototypeCompute = PrototypePixel;
	PrototypeCompute.Mode = ETextureReadMode::Derivatives;

	FValueRef Exp2MipBias = Operator(UO_Exponential2, MipBias);
	PrototypeCompute.TexCoordDdx = Operator(BO_Multiply, AnalyticalPartialDerivative(TexCoord, EDerivativeAxis::X), Exp2MipBias);
	PrototypeCompute.TexCoordDdy = Operator(BO_Multiply, AnalyticalPartialDerivative(TexCoord, EDerivativeAxis::Y), Exp2MipBias);

	// We throw a deliberate error downstream for this expression if referenced from a vertex shader.  We use the hardware
	// derivative value type (disallowed in vertex shader, already tracked by the value analyzer) to communicate the error.
	// We could alternately make a one-off type just to communicate this error, but this works, and is somewhat logical,
	// given the root cause of this sampler type not working is due to missing derivative support in vertex shaders.
	FHardwarePartialDerivative PrototypeVertex = MakePrototype<FHardwarePartialDerivative>(PrototypePixel.Type);
	PrototypeVertex.Arg = ConstantFloat(0.0f);
	PrototypeVertex.Axis = EDerivativeAxis::X;
	PrototypeVertex.Source = EDerivativeSource::TextureSampleBias;

	if (IsVirtualSamplerType(SamplerType))
	{
		PrototypePixel.VTPageTable = VTPageTableLoadFromSamplerSource(*this, Texture, BaseAttributes, PrototypePixel.TexCoord, {}, {}, TMVM_MipBias, PrototypePixel.MipValue);
		PrototypeCompute.VTPageTable = VTPageTableLoadFromSamplerSource(*this, Texture, BaseAttributes, PrototypeCompute.TexCoord, PrototypeCompute.TexCoordDdx, PrototypeCompute.TexCoordDdy, TMVM_MipBias, PrototypeCompute.MipValue);
	}

	FStageSwitch StageSwitch = MakePrototype<FStageSwitch>(PrototypePixel.Type);
	StageSwitch.Args[Stage_Vertex] = EmitPrototype(*this, PrototypeVertex);
	StageSwitch.Args[Stage_Pixel] = EmitPrototype(*this, PrototypePixel);
	StageSwitch.Args[Stage_Compute] = EmitPrototype(*this, PrototypeCompute);

	return EmitPrototype(*this, StageSwitch);
}

FValueRef FEmitter::TextureSampleGrad(FValueRef Texture, FValueRef TexCoord, FValueRef TexCoordDdx, FValueRef TexCoordDdy, bool bAutomaticViewMipBias, const FTextureSampleBaseAttributes& BaseAttributes)
{
	if (IsAnyNotValid(Texture, TexCoord, TexCoordDdx, TexCoordDdy))
	{
		return Poison();
	}

	const EMaterialSamplerType SamplerType = MapSamplerTypeForTexture(Texture, BaseAttributes.SamplerType);

	FTextureRead Prototype = MakePrototype<FTextureRead>(FType::MakeFloatVector(4));
	Prototype.TextureObject = Texture;
	Prototype.TexCoord = TexCoord;
	Prototype.TexCoordDdx = TexCoordDdx;
	Prototype.TexCoordDdy = TexCoordDdy;
	Prototype.Mode = ETextureReadMode::Derivatives;
	Prototype.SamplerSourceMode = BaseAttributes.SamplerSourceMode;
	Prototype.SamplerType = SamplerType;

	if (bAutomaticViewMipBias)
	{
		FValueRef ViewMaterialTextureDerivativeMultiply = ExternalInput(EExternalInput::ViewMaterialTextureDerivativeMultiply);
		Prototype.TexCoordDdx = Operator(BO_Multiply, Prototype.TexCoordDdx, ViewMaterialTextureDerivativeMultiply);
		Prototype.TexCoordDdy = Operator(BO_Multiply, Prototype.TexCoordDdy, ViewMaterialTextureDerivativeMultiply);
	}

	if (IsVirtualSamplerType(SamplerType))
	{
		Prototype.VTPageTable = VTPageTableLoadFromSamplerSource(*this, Texture, BaseAttributes, Prototype.TexCoord, Prototype.TexCoordDdx, Prototype.TexCoordDdy, TMVM_Derivative);
	}

	return EmitPrototype(*this, Prototype);
}

FValueRef FEmitter::VTPageTableLoad(
	FValueRef Texture, TextureAddress AddressU, TextureAddress AddressV, FValueRef TexCoord,
	FValueRef TexCoordDdx, FValueRef TexCoordDdy, bool bEnableFeedback, bool bIsAdaptive, ETextureMipValueMode MipValueMode, FValueRef MipValue)
{
	// VT stack and layer indices are initializes during IR analysis
	FVTPageTableRead Prototype = MakePrototype<FVTPageTableRead>(FType::MakeVTPageTableResult());
	Prototype.TextureObject = Texture;
	Prototype.TexCoord = TexCoord;
	Prototype.TexCoordDdx = TexCoordDdx;
	Prototype.TexCoordDdy = TexCoordDdy;
	Prototype.MipValue = MipValue;
	Prototype.AddressU = AddressU;
	Prototype.AddressV = AddressV;
	Prototype.MipValueMode = MipValueMode;
	Prototype.bEnableFeedback = bEnableFeedback;
	Prototype.bIsAdaptive = bIsAdaptive;
	return EmitPrototype(*this, Prototype);
}

FValueRef FEmitter::PartialDerivative(FValueRef Value, EDerivativeAxis Axis)
{
	// Any operation on poison arguments is a poison.
	if (!Value.IsValid())
	{
		return Value;
	}

	// Differentiation is only valid on primitive types.
	if (!Value->Type.IsAnyFloat())
	{
		Errorf(Value, TEXT("Trying to differentiate a value of type `%s` is invalid. Expected a float type."), *Value->Type.GetSpelling());
		return Poison();
	}

	// Make the hardware derivative instruction.
	FHardwarePartialDerivative HwDerivativeProto = MakePrototype<FHardwarePartialDerivative>(Value->Type);
	HwDerivativeProto.Arg = Value;
	HwDerivativeProto.Axis = Axis;
	HwDerivativeProto.Source = EDerivativeSource::Derivative;
	FValueRef HwDerivative = EmitPrototype(*this, HwDerivativeProto);

	// Compute the analytical derivative for stages that don't support hardware derivatives.
	FValueRef AnalyticalDerivative = AnalyticalPartialDerivative(Value, Axis);

	// Emit the stage switch instruction so that analytical derivatives are used on stages that support it and hardware
	// derivatives in the other stages.  Note that hardware derivatives throw errors on the vertex stage downstream, but
	// we don't know if the expression is reached in that stage until the ValueAnalyzer runs.
	FValueRef StageValues[NumStages] = {};
	for (int i = 0; i < NumStages; ++i)
	{
		StageValues[i] = (i == Stage_Compute) ? AnalyticalDerivative : HwDerivative;
	}
	return StageSwitch(Value->Type, StageValues);
}

static FValue* DifferentiateExternalInput(FEmitter& Emitter, FExternalInput* ExternalInput, EDerivativeAxis Axis)
{
	// External inputs with derivatives have their own matching DDX/DDY inputs.
	if (IsExternalInputWithDerivatives(ExternalInput->Id))
	{
		return Emitter.ExternalInput(GetExternalInputDerivative(ExternalInput->Id, Axis));
	}

	// All other inputs are assumed constant.
	return Emitter.Cast(Emitter.ConstantZero(ExternalInput->Type.GetPrimitive().ScalarKind), ExternalInput->Type);
}

static FValue* DifferentiateOperator(FEmitter& E, FOperator* Op, EDerivativeAxis Axis)
{
	EScalarKind OpScalarKind = Op->Type.GetPrimitive().ScalarKind;

	// Considering an operator acting on f(x), g(x) and h(x) arguments (e.g. "f(x) + g(x)"),
	// calculate base terms and their partial derivatives.
	FValue* F = Op->AArg;
	FValue* G = Op->BArg;
	FValue* H = Op->CArg;
	FValue* dF = F && !F->Type.IsBoolean() ? E.AnalyticalPartialDerivative(F, Axis).Value : nullptr; // Note: select's first argument is a boolean, avoid making the derivative then
	FValue* dG = E.AnalyticalPartialDerivative(G, Axis);
	FValue* dH = E.AnalyticalPartialDerivative(H, Axis);

	// Convenience local functions as multiplications and division operations are common in derivatives.
	auto Zero = [&E, OpScalarKind] () { return E.ConstantZero(OpScalarKind); };
	auto One = [&E, OpScalarKind] () { return E.ConstantOne(OpScalarKind); };
	auto Constant = [&E, OpScalarKind] (TDouble Scalar) { return E.ConstantScalar(OpScalarKind, Scalar); };

	// Some constants
	constexpr TDouble Ln2 = 0.69314718055994530941723212145818;
	constexpr TDouble Ln10 = 2.3025850929940456840179914546844;
	
	switch (Op->Op)
	{
		// d/dx -f(x) = -f'(x)
		case UO_Negate:
			return E.Negate(dF);

		// d/dx |f(x)| = f(x) f'(x) / |f(x)|
		case UO_Abs:
			return E.Divide(E.Multiply(F, dF), Op);

		// d/dx arccos(f(x)) = -1 / sqrt(1 - f(x)^2) * f'(x)
		case UO_ACos:
		case UO_ACosFast:
			return E.Negate(E.Divide(dF, E.Sqrt(E.Subtract(One(), E.Multiply(F, F)))));
			
		// d/dx acosh(f(x)) = 1 / sqrt(f(x)^2 - 1) * f'(x)
		case UO_ACosh:
			return E.Divide(dF, E.Sqrt(E.Subtract(E.Multiply(F, F), One())));
			
		// d/dx arcsin(f(x)) = 1 / sqrt(1 - f(x)^2) * f'(x)
		case UO_ASin:
		case UO_ASinFast:
			return E.Divide(dF, E.Sqrt(E.Subtract(One(), E.Multiply(F, F))));
		
		// d/dx asinh(f(x)) = 1 / sqrt(f(x)^2 + 1) * f'(x)
		case UO_ASinh:
			return E.Divide(dF, E.Sqrt(E.Add(E.Multiply(F, F), One())));

		// d/dx arctan(f(x)) = 1 / (1 + f(x)^2) * f'(x)
		case UO_ATan:
		case UO_ATanFast:
			return E.Divide(dF, E.Add(One(), E.Multiply(F, F)));

    	// d/dx atanh(f(x)) = f'(x) / (1 - f(x)^2)
		case UO_ATanh:
    		return E.Divide(dF, E.Subtract(One(), E.Multiply(F, F)));

		// d/dx cos(f(x)) = -sin(f(x)) * f'(x)
		case UO_Cos:
			return E.Negate(E.Multiply(E.Sin(F), dF));

		// d/dx cosh(f(x)) = sinh(f(x)) * f'(x)
		case UO_Cosh:
			return E.Multiply(E.Sinh(F), dF);

		// d/dx e^f(x) = e^f(x) * f'(x)
		case UO_Exponential:
			return E.Multiply(Op, dF);

		// d/dx 2^f(x) = ln(2) * 2^f(x) * f'(x)
		case UO_Exponential2:
			return E.Multiply(E.Multiply(Constant(Ln2), Op), dF);

		// d/dx frac(f(x)) = f'(x), since frac(x) = x - floor(x)
		case UO_Frac:
			return dF;

		// d/dx |f(x)| (length in vector case) = f(x) f'(x) / |f(x)|
		case UO_Length:
			return E.Divide(E.Multiply(F, dF), Op);

		// d/dx log(f(x)) = 1 / f(x) * f'(x)
		case UO_Logarithm:
			return E.Divide(dF, F);

		// d/dx log2(f(x)) = 1 / (f(x) * ln(2)) * f'(x)
		case UO_Logarithm2:
			return E.Divide(dF, E.Multiply(F, Constant(Ln2)));

		// d/dx log10(f(x)) = 1 / (f(x) * ln(10)) * f'(x)
		case UO_Logarithm10:
			return E.Divide(dF, E.Multiply(F, Constant(Ln10)));

		// d/dx saturate(f(x)) = f'(x) if f(x) is inside (0-1) range, 0 otherwise
		case UO_Saturate:
			return E.Select(E.And(
							E.LessThan(Zero(), F), // 0 < f(x)
							E.LessThan(F, One())), // f(x) < 1
						dF, Zero());

		// d/dx sin(f(x)) = cos(f(x)) * f'(x)
		case UO_Sin:
			return E.Multiply(E.Cos(F), dF);
		
		// d/dx sinh(f(x)) = cosh(f(x)) * f'(x)
		case UO_Sinh:
			return E.Multiply(E.Cosh(F), dF);

		// d/dx sqrt(f(x)) = 1 / (2 * sqrt(f(x))) * f'(x)
		case UO_Sqrt:
			return E.Divide(dF, E.Multiply(Constant(2), E.Sqrt(F)));

		// d/dx rcp(f(x)) = -1 / (f(x)^2) * f'(x) 
		case UO_Reciprocal:
		{
			FValueRef rcp = E.Reciprocal(F);
			return E.Multiply(E.Multiply(E.Negate(rcp), rcp), dF);
		}

		// d/dx rsqrt(f(x)) = -1 / (2 * sqrt(f(x)) * f(x)) * f'(x)
		case UO_Rsqrt:
			return E.Multiply(E.Multiply(E.ConstantFloat(-0.5f), E.Multiply(E.Rsqrt(F), E.Reciprocal(F))), dF);

		// d/dx tan(f(x)) = 1 / cos^2(f(x)) * f'(x)
		case UO_Tan:
		{
			FValue* CosVal = E.Cos(F);
			return E.Divide(dF, E.Multiply(CosVal, CosVal));
		}
		
		// d/dx tanh(f(x)) = (1 - tanh(f(x))^2) * f'(x)
		case UO_Tanh:
			return E.Multiply(E.Subtract(One(), E.Multiply(Op, Op)), dF);

		// These functions are piecewise constant, that is mostly constant with some
		// discontinuities. We assume they're always constant, as they're not differentiable
		// at the discontinuities.
		case UO_Ceil:
		case UO_Floor:
		case UO_Round:
		case UO_Truncate:
			return Zero();

		// d/dx (f(x) + g(x)) = f'(x) + g'(x)
		case BO_Add:
			return E.Add(dF, dG);

		// d/dx (f(x) - g(x)) = f'(x) - g'(x)
		case BO_Subtract:
			return E.Subtract(dF, dG);

		// d/dx (f(x) * g(x)) = f'(x) * g(x) + f(x) * g'(x)
		case BO_Multiply:
			return E.Add(E.Multiply(dF, G), E.Multiply(F, dG));

		// d/dx matmul(f(x), g(x)) = matmul(f'(x), g(x)) + matmul(f(x), g'(x))
		case BO_MatrixMultiply:
			return E.Add(E.MatrixMultiply(dF, G), E.MatrixMultiply(F, dG));

		// d/dx (f(x) / g(x)) = (f'(x) * g(x) - f(x) * g'(x)) / g(x)^2
		case BO_Divide:
			return E.Divide(E.Subtract(E.Multiply(dF, G), E.Multiply(F, dG)), E.Multiply(G, G));

		// fmod(f(x), g(x)) = f(x) - g(x) * floor(f(x) / g(x)).
		// Thus:
		//     d/dx fmod(f(x), g(x)) = f'(x) - g(x) * floor(f(x) / g(x))
		// since `floor` is piecewise constant.
		case BO_Fmod:
			return E.Subtract(dF, E.Multiply(dG, E.Operator(UO_Floor, E.Divide(F, G))));

		// d/dx max(f(x), g(x)) = f'(x) if f(x) > g(x), else g'(x)
		case BO_Max:
			return E.Select(E.Operator(BO_GreaterThan, F, G), dF, dG);

		// d/dx min(f(x), g(x)) = f'(x) if f(x) < g(x), else g'(x)
		case BO_Min:
			return E.Select(E.LessThan( F, G), dF, dG);

		// d/dx pow(f(x), g(x)) = f(x)^g(x) * (g'(x) * ln(f(x)) + g(x) * f'(x) / f(x))
		case BO_Pow:
		{
			FValue* Term1 = E.Multiply(dG, E.Logarithm(F)); // g'(x) * ln(f(x))
			FValue* Term2 = E.Divide(E.Multiply(G, dF), F); // g(x) * f'(x) / f(x)
			return E.Multiply(Op, E.Add(Term1, Term2));
		}

		// d/dx atan2(f(x), g(x)) = g(x) / (f(x)^2 + g(x)^2) * f'(x)  -  f(x) / (f(x)^2 + g(x)^2) * g'(x)
		case BO_ATan2:
		case BO_ATan2Fast:
		{
			FValue* Magnitude = E.Divide(One(), E.Add(E.Multiply(F, F), E.Multiply(G, G)));		// 1 / (f(x)^2 + g(x)^2)
			return E.Subtract(E.Multiply(E.Multiply(G, Magnitude), dF), E.Multiply(E.Multiply(F, Magnitude), dG));
		}

		// The multiplication rule applies for the dot product too.
		// d/dx (f(x) • g(x)) = f'(x) • g(x) + f(x) • g'(x)
		case BO_Dot:
			return E.Add(E.Operator(BO_Dot, dF, G), E.Operator(BO_Dot, F, dG));

		// The multiplication rule applies for the cross product too.
		// d/dx (f(x) × g(x)) = f'(x) × g(x) + f(x) × g'(x)
		case BO_Cross:
			return E.Add(E.Operator(BO_Cross, dF, G), E.Operator(BO_Cross, F, dG));

		// clamp(x, min, max) (F=x, min=G, max=H)
		// The derivative is defined when x is between min and max (f'(x)). At and outside
		// bounds, the clamp result is constant and thus the derivative is zero.
		case TO_Clamp:
			return E.Select(E.And(
					E.LessThan(G, F), 
					E.LessThan(F, H)),
				dF, Zero());

		// lerp(a, b, t) = a + t * (b - a)
		// d/dx lerp(f(x), g(x), h(x)) = f'(x) + d/dx (h(x) * ((g(x) - f(x)))
		// d/dx (h(x) * ((g(x) - f(x))) = h'(x) * ((g(x) - f(x))) + h(x) * (g'(x) - f'(x))
		case TO_Lerp:
			return E.Add(dF, 
					   E.Add(E.Multiply(dH, E.Subtract(G, F)), 
						   E.Multiply(H, E.Subtract(dG, dF))));

		// d/dx select(F, g(x), h(x)) ˜ select(F, g'(x), h'(x))
		case TO_Select:
			return E.Select(F, dG, dH);

		// smoothstep(f(x), g(x), h(x)) = 3 z^2 - 2 z^3  with z = saturate((h - f) / (g - f))
		case TO_Smoothstep:
		{
			FValue* Z  = E.Saturate(E.Divide(E.Subtract(H, F), E.Subtract(G, F)));
			FValue* dZ = E.AnalyticalPartialDerivative(Z, Axis);
			// d/dx 3 z(x)^2 - 2 z(x)^3 = 6 * z(x) * z'(x) - 6 * z(x)^2 * z'(x) = 6 * (z(x) - z(x)^2) * z'(x)
			return E.Multiply(dZ, E.Multiply(Constant(6), E.Subtract(Z, E.Multiply(Z, Z))));
		}

		// these are either invalid or constant
		case UO_BitwiseNot:
		case UO_IsFinite:
		case UO_IsInf:
		case UO_IsNan:
		case UO_LWCTile:
		case UO_Sign:
		case BO_Modulo:
		case BO_BitwiseAnd:
		case BO_BitwiseOr:
		case BO_BitShiftLeft:
		case BO_BitShiftRight:
		case BO_Step:
			return Zero();

		default:
			UE_MIR_UNREACHABLE();
	}
}

FValueRef FEmitter::AnalyticalPartialDerivative(FValueRef Value, EDerivativeAxis Axis)
{
	// Any operation on poison arguments is a poison.
	if (!Value.IsValid())
	{
		return Value;
	}

	// Differentiation is only valid on primitive types.
	if (!Value->Type.IsAnyFloat())
	{
		Errorf(Value, TEXT("Trying to differentiate a value of type `%s` is invalid. Expected a float type."), *Value->Type.GetSpelling());
		return Poison();
	}

	switch (Value->Kind)
	{
		case VK_ExternalInput:
			return DifferentiateExternalInput(*this, Value->As<FExternalInput>(), Axis);

		case VK_Composite:
		{
			// Make a prototype composite to hold the derivatives of all its components
			FComposite* Derivative = MakeCompositePrototype(*this, Value->Type, Value->Type.GetPrimitive().NumComponents());
			TArrayView<FValue*> DerivativeComponents = Derivative->GetMutableComponents();

			// Compute the derivative of each component
			TConstArrayView<FValue*> ValueComponents = Value->As<FComposite>()->GetComponents();
			for (int32 i = 0; i < ValueComponents.Num(); ++i)
			{
				DerivativeComponents[i] = AnalyticalPartialDerivative(ValueComponents[i], Axis);
			}

			return EmitPrototype(*this, *Derivative);
		}

		case VK_Operator:
			return DifferentiateOperator(*this, Value->As<FOperator>(), Axis);
		
		case VK_Branch:
		{
			FBranch* AsBranch = Value->As<FBranch>();
			return Branch(AsBranch->ConditionArg,
				AnalyticalPartialDerivative(AsBranch->TrueArg, Axis),
				AnalyticalPartialDerivative(AsBranch->FalseArg, Axis));
		}

		case VK_StageSwitch:
		{
			// For StageSwitch, we want to pass through and generate derivatives for its input.  We only need to do this for
			// the compute stage, because the analytic derivative code path is unreachable for the pixel and vertex stages.
			// To reach the analytic derivative code path in the first place, there will have been a higher level stage switch,
			// which will already have chosen a different hardware derivative path for the pixel shader, or thrown an error for
			// the vertex shader, where explicit derivatives are disallowed completely.
			// 
			// For the other stages, we can pass a poison value, to detect if this assumption is violated in the future.
			// Because this is a non-error unreachable poison, we don't call FEmitter::Poison, as that will trigger an unwanted
			// breakpoint when using the debug feature that breaks on poison values.
			FStageSwitch* AsStageSwitch = Value->As<FStageSwitch>();
			TStaticArray<FValueRef, NumStages> StageDerivatives;
			for (int32 StageIndex = 0; StageIndex < NumStages; StageIndex++)
			{
				StageDerivatives[StageIndex] = StageIndex == Stage_Compute ? AnalyticalPartialDerivative(AsStageSwitch->Args[StageIndex], Axis) : FPoison::Get();
			}
			return StageSwitch(Value->Type, StageDerivatives);
		}

		case VK_Subscript:
		{
			FSubscript* AsSubscript = Value->As<FSubscript>();
			return Subscript(AnalyticalPartialDerivative(AsSubscript->Arg, Axis), AsSubscript->Index);
		}

		case VK_Scalar:
		{
			FScalar* AsScalar = Value->As<FScalar>();
			return Cast(AnalyticalPartialDerivative(AsScalar->Arg, Axis), AsScalar->Type);
		}

		case VK_InlineHLSL:
		{
			FInlineHLSL* AsInlineHLSL = Value->As<FInlineHLSL>();
			if (!EnumHasAnyFlags(AsInlineHLSL->Flags, EValueFlags::HasDynamicHLSLCode) && AsInlineHLSL->ExternalCodeDeclaration->Derivative == EDerivativeStatus::Valid)
			{
				TArray<FValueRef> Arguments(TArrayView<FValue*>(AsInlineHLSL->Arguments, AsInlineHLSL->NumArguments));
				return InlineHLSL(AsInlineHLSL->ExternalCodeDeclaration, Arguments, Axis == EDerivativeAxis::X ? EValueFlags::DerivativeDDX : EValueFlags::DerivativeDDY);
			}
			return Cast(ConstantZero(Value->Type.GetPrimitive().ScalarKind), Value->Type);
		}

		// These values are uniform (constant), thus their value is always zero.
		case VK_Constant:
		case VK_UniformParameter:
		case VK_PreshaderParameter:
			return Cast(ConstantZero(Value->Type.GetPrimitive().ScalarKind), Value->Type);

		// These values don't work with analytic derivatives, and force hardware derivatives (or zero if the shader model doesn't support compute shader derivatives).
		case VK_TextureRead:
		case VK_Call:
		{
			if (GetFeatureLevel() >= ERHIFeatureLevel::Type::SM6)
			{
				// Make the hardware derivative instruction.
				FHardwarePartialDerivative HwDerivativeProto = MakePrototype<FHardwarePartialDerivative>(Value->Type);
				HwDerivativeProto.Arg = Value;
				HwDerivativeProto.Axis = Axis;
				HwDerivativeProto.Source = EDerivativeSource::AnalyticDerivative;
				return EmitPrototype(*this, HwDerivativeProto);
			}
			else
			{
				return Cast(ConstantZero(Value->Type.GetPrimitive().ScalarKind), Value->Type);
			}
		}

		default:
			UE_MIR_UNREACHABLE();
	}
}

static FValueRef EmitInlineHLSL(FEmitter& Emitter, FType Type, const FMaterialExternalCodeDeclaration* InExternalCodeDeclaration, FStringView Code, TConstArrayView<FValueRef> InputValues, EValueFlags ValueFlags, EGraphProperties UsedGraphProperties)
{
	FInlineHLSL Prototype = MakePrototype<FInlineHLSL>(Type);
	Prototype.Type = Type;
	Prototype.Flags = ValueFlags;
	Prototype.GraphProperties = UsedGraphProperties;
	
	if (InExternalCodeDeclaration)
	{
		check(Code.IsEmpty());
		Prototype.ExternalCodeDeclaration = InExternalCodeDeclaration;
	}
	else
	{
		Prototype.Code = Code;
	}

	if (!InputValues.IsEmpty())
	{
		checkf(InputValues.Num() < FInlineHLSL::MaxNumArguments, TEXT("Number of arguments for inline-HLSL out of bounds: %d was specified, but upper bound is %d"), InputValues.Num(), FInlineHLSL::MaxNumArguments);
		Prototype.NumArguments = InputValues.Num();
		for (int32 i = 0; i < InputValues.Num(); ++i)
		{
			checkf(InputValues[i], TEXT("InputValues[%d] must not be null when InlineHLSL-instruction is emitted"), i);
			Prototype.Arguments[i] = InputValues[i];
		}
	}

	return EmitPrototype(Emitter, Prototype);
}

FValueRef FEmitter::InlineHLSL(FType Type, FStringView Code, TConstArrayView<FValueRef> InputValues, EValueFlags ValueFlags, EGraphProperties UsedGraphProperties)
{
	if (IsAnyNotValid(InputValues))
	{
		return Poison();
	}
	
	return EmitInlineHLSL(*this, Type, nullptr, Module->InternString(Code), InputValues, ValueFlags | EValueFlags::HasDynamicHLSLCode, UsedGraphProperties);
}

FValueRef FEmitter::InlineHLSL(const FMaterialExternalCodeDeclaration* InExternalCodeDeclaration, TConstArrayView<FValueRef> InputValues, EValueFlags ValueFlags, EGraphProperties UsedGraphProperties)
{
	if (IsAnyNotValid(InputValues))
	{
		return Poison();
	}

	check(InExternalCodeDeclaration != nullptr);
	FType ReturnType = FType::FromMaterialValueType(InExternalCodeDeclaration->GetReturnTypeValue());
	return EmitInlineHLSL(*this, ReturnType, InExternalCodeDeclaration, {}, InputValues, ValueFlags, UsedGraphProperties);
}

FValueRef FEmitter::PromoteSubstrateParameter()
{
	FPromoteSubstrateParameter Prototype = MakePrototype<FPromoteSubstrateParameter>(FType::MakeSubstrateData());
	return EmitPrototype(*this, Prototype);
}

const FFunction* FEmitter::FunctionHLSL(const FFunctionHLSLDesc& Desc)
{
	check(Desc.GetNumParameters() <= MIR::MaxNumFunctionParameters);

	FFunctionHLSL Prototype = {};
	Prototype.Name = Desc.Name;
	Prototype.ReturnType = Desc.ReturnType;
	Prototype.Code = Desc.Code;
	Prototype.NumInputOnlyParams = Desc.NumInputOnlyParams;
	Prototype.NumInputAndOutputParams = Desc.NumInputOnlyParams + Desc.NumInputOutputParams;
	Prototype.NumParameters = Prototype.NumInputAndOutputParams + Desc.NumOutputOnlyParams;
	Prototype.UniqueId = Module->FunctionHLSLs.Num();
	Prototype.Defines = Desc.Defines;
	Prototype.Includes = Desc.Includes;

	// Copy over the parameter declarations from the description to the function prototype
	for (uint32 i = 0; i < Prototype.NumParameters; ++i)
	{
		Prototype.Parameters[i] = Desc.Parameters[i];
	}

	// TODO: Optimize the lookup to be constant
	for (const FFunctionHLSL* Function : Module->FunctionHLSLs)
	{
		if (Function->Equals(&Prototype))
		{
			return Function;
		}
	}

	// Create the new MIR HLSL function instance and set it up
	FFunctionHLSL* Function = new (FPrivate::Allocate(*this, sizeof(FFunctionHLSL), alignof(FFunctionHLSL))) FFunctionHLSL { Prototype };
	Function->Name = Module->InternString(Function->Name);
	Function->Code = Module->InternString(Desc.Code);
	Function->Defines = MakeArrayCopy(*this, Desc.Defines);
	Function->Includes = MakeArrayCopy(*this, Desc.Includes);

	// Add it to the module list
	Module->FunctionHLSLs.Push(Function);

	return Function;
}

FValueRef FEmitter::Call(const FFunction* Function, TConstArrayView<FValueRef> InputArguments)
{
	if (!Function)
	{
		return Poison();
	}

	if (InputArguments.Num() != Function->NumInputAndOutputParams)
	{
		Errorf(TEXT("Function called with incorrect number of arguments. Expected %d but got %d."), Function->NumInputAndOutputParams, InputArguments.Num());
		return Poison();
	}

	FCall Call = MakePrototype<FCall>(Function->ReturnType);
	Call.Function = Function;
	Call.NumArguments = InputArguments.Num();

	for (int i = 0; i < InputArguments.Num(); ++i)
	{
		Call.Arguments[i] = InputArguments[i];
	}

	return EmitPrototype(*this, Call);
}

FValueRef FEmitter::CallParameterOutput(FValueRef InCall, uint32 ParameterIndex)
{
	if (IsAnyNotValid(InCall))
	{
		return InCall.ToPoison();
	}

	FCall* Call = InCall->As<FCall>();
	if (!Call)
	{
		Errorf(TEXT("Expected function call, found a '%s' value instead."), MIR::LexToString(InCall->Kind));
		return InCall.ToPoison();
	}
	
	if (ParameterIndex >= Call->Function->GetNumOutputParameters())
	{
		Errorf(InCall, TEXT("Invalid output index %d. Function has %d outputs."), ParameterIndex, Call->Function->GetNumOutputParameters());
		return InCall.ToPoison();
	}

	FCallParameterOutput Proto = MakePrototype<FCallParameterOutput>(Call->Function->GetOutputParameter(ParameterIndex).Type);
	Proto.Call = Call;
	Proto.Index = ParameterIndex;

	return EmitPrototype(*this, Proto);
}

void FEmitter::Initialize()
{
	// Create and reference the true/false constants.
	FConstant Temp = MakePrototype<FConstant>(FType::MakeBoolScalar());

	Temp.Boolean = true;
	TrueConstant = EmitPrototype(*this, Temp);

	Temp.Boolean = false;
	FalseConstant = EmitPrototype(*this, Temp);
}

EShaderPlatform FEmitter::GetShaderPlatform() const
{
	return Module->GetShaderPlatform();
}

const ITargetPlatform* FEmitter::GetTargetPlatform() const
{
	return Module->GetTargetPlatform();
}

ERHIFeatureLevel::Type FEmitter::GetFeatureLevel() const
{
	return Module->GetFeatureLevel();
}

EMaterialQualityLevel::Type FEmitter::GetQualityLevel() const
{
	return Module->GetQualityLevel();
}

bool FEmitter::FValueKeyFuncs::Matches(KeyInitType A, KeyInitType B)
{
	return A->Equals(B);
}

uint32 FEmitter::FValueKeyFuncs::GetKeyHash(KeyInitType Key)
{
	return Internal::HashBytes(Key, Key->GetSizeInBytes());
}

} // namespace MIR

#endif // #if WITH_EDITOR
