// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Materials/MaterialIRCommon.h"
#include "Materials/MaterialIRTypes.h"
#include "Materials/MaterialParameters.h"
#include "Materials/MaterialExpressionDBufferTexture.h"
#include "MaterialSceneTextureId.h"
#include "Engine/TextureDefines.h"
#include "Shader/Preshader.h"

#if WITH_EDITOR

struct FMaterialExternalCodeDeclaration;

class URuntimeVirtualTexture;

namespace MIR
{

// A simple string view without constructors, to be used inside FValues.
struct FSimpleStringView
{
	const TCHAR* Ptr; // The string characters
	int32        Len; // The string length (excluding null byte)

	FStringView ToView() const { return { Ptr, Len }; }
	FString ToString() const { return FString{ ToView() }; }
	operator FStringView() const { return ToView(); }
	FSimpleStringView& operator=(FStringView View) { Ptr = View.GetData(); Len = View.Len(); return *this; }
};

// Identifies the block of execution in which an instruction runs.
// Note: If you introduce a new stage, make sure you update EValueFlags accordingly.
enum EStage
{
	Stage_Vertex,
	Stage_Pixel,
	Stage_Compute,
	NumStages
};

// Returns the string representation of given stage.
const TCHAR* LexToString(EStage Stage);

// A collection of bit flags for a specific Value instance.
enum class EValueFlags : uint8
{
	None = 0,

	// Value has been analyzed for the vertex stage.
	AnalyzedForStageVertex = 1 << 0,

	// Value has been analyzed for the pixel stage.
	AnalyzedForStagePixel = 1 << 1,

	// Value has been analyzed for the compute stage.
	AnalyzedForStageCompute = 1 << 2,

	// Value has been analyzed in some stage.
	AnalyzedInAnyStage = 1 << 3,

	// Whether to substitute abstract tokens like "<PREV>" in code expressions of InlineHLSL instructions.
	SubstituteTags = 1 << 4,

	// Whether 'FInlineHLSL::Code' field is used. Otherwise, 'ExternalCodeDeclaration' is used.
	HasDynamicHLSLCode = 1 << 5, 

	// External code declaration must emit the secondary definition for the DDX derivative.
	DerivativeDDX = 1 << 6,

	// External code declaration must emit the secondary definition for the DDY derivative.
	DerivativeDDY = 1 << 7,
};

ENUM_CLASS_FLAGS(EValueFlags);

// A collection of graph properties used by a value.
//
// If a graph property is true, it means that either the value itself makes has of that
// property, or one of its dependencies (direct or indirect) has it. This entails these
// flags are automatically propagated to all dependant values (values that depend on a given one).
// As an example, if "ReadsPixelNormal" is true on a specific value it means that either
// that value itself or some other upstream value that value is dependent on reads
// the pixel normal.
enum class EGraphProperties : uint8
{
	None = 0,

	// Some value reads the pixel normal.
	ReadsPixelNormal = 1 << 0,
};

ENUM_CLASS_FLAGS(EGraphProperties);

// Enumeration of all different structs deriving from FValue. Used for dynamic casting.
enum EValueKind
{
	// Values

	VK_Poison,
	VK_Constant,
	VK_ExternalInput,
	VK_MaterialParameterCollection,
	VK_ScreenTexture,
	VK_ShadingModel,
	VK_TextureObject,
	VK_RuntimeVirtualTextureObject,
	VK_UniformParameter,

	// Instructions

	VK_InstructionBegin,

	VK_SetMaterialOutput,
	VK_Composite,
	VK_Operator,
	VK_Branch,
	VK_Subscript,
	VK_Scalar,
	VK_TextureRead,
	VK_VTPageTableRead,
	VK_InlineHLSL,
	VK_PromoteSubstrateParameter,
	VK_StageSwitch,
	VK_HardwarePartialDerivative,
	VK_Nop,
	VK_Call,
	VK_CallParameterOutput,
	VK_PreshaderParameter,

	VK_InstructionEnd,
};

// Returns the string representation of given value kind.
const TCHAR* LexToString(EValueKind Kind);

// Values

// Base entity of all IR graph nodes.
//
// An IR module is a graph of values, connected by their "uses" relations. The graph of IR
// values is built by the `MaterialIRModuleBuilder` as the result of crawling through and
// analyizing the MaterialExpression graph contained in the translated Material. During
// this processing, IR values are emitted, and linked together. After the graph is
// constructed, it is itself analyzed: the builder will call `FMaterialIRValueAnalyzer::Analyze()`
// in each active* (i.e. truly used) value in the graph, making sure a value is analyzed only
// after its dependencies have been analyzed.
// A few notes:
// - FValues are automatically zero-initialized.
// - FValues are intended to be simple and inert data records. They cannot have non-trivia
//   ctor, dtor or copy operators.
// - The ModuleBuilder relies on this property to efficiently hashing values so that it
//   will reuse the same value instance instead of creating multiple instances of the
//   same computation (for a more efficient output shader).
// - All values have a MIR type.
// - Pure FValue instances are values that do not have other dependencies (called "uses").
// - If a value has some other value as dependency, it means that it is the result of a
//   calculation on those values. Values that have dependencies are Instructions (they
//   derive from FInstruction).
struct FValue
{
	// The runtime type this value has.
	FType Type;

	// Used to discern the concrete C++ type of this value (e.g. Subscript)
	EValueKind Kind : 8;

	// Set of fundamental flags true for this value.
	EValueFlags Flags;

	// The set of properties that are true for this value. Some flags might have been
	// set but some upstream dependency leading to this value and not this value directly.
	// See `EGraphProperties` for more information.
	EGraphProperties GraphProperties;

	// Returns whether this value has been analyzed for specified stage.
	bool IsAnalyzed(EStage State) const;

	// Returns whether specified flags are true for this value.
	bool HasFlags(EValueFlags InFlags) const;

	// Enables the specified flags without affecting others.
	void SetFlags(EValueFlags InFlags);

	// Disables the specified flags without affecting others.
	void ClearFlags(EValueFlags InFlags);

	// Returns whether specified properties are true for this value.
	bool HasSubgraphProperties(EGraphProperties Properties) const;

	// Enables specified properties for this value.
	void UseSubgraphProperties(EGraphProperties Properties);

	// Returns the size in bytes of this value instance.
	uint32 GetSizeInBytes() const;

	// Gets the immutable array of all this value uses.
	// An use is another value referenced by this one (e.g. the operands of a binary expression).
	// Returns the immutable array of uses.
	TConstArrayView<FValue*> GetUses() const;

	// Gets the immutable array of this value uses filtered for a specific stage stage.
	//
	// Returns the immutable array of uses.
	TConstArrayView<FValue*> GetUsesForStage(MIR::EStage Stage) const;

	// Returns whether this value is of specified kind.
	bool IsA(EValueKind InKind) const { return Kind == InKind; }

	// Returns whether this value is poison.
	bool IsPoison() const { return Kind == VK_Poison; }

	// Returns whether this value is a constant boolean with value true.
	bool IsTrue() const;

	// Returns whether this value is a constant boolean with value false.
	bool IsFalse() const;

	// Returns whether this value is boolean and all components are true.
	bool AreAllTrue() const;

	// Returns whether this value is boolean and all components are false.
	bool AreAllFalse() const;

	// Returns whether this value is arithmetic and all components are exactly zero.
	bool AreAllExactlyZero() const;

	// Returns whether this value is arithmetic and all components are approximately zero.
	bool AreAllNearlyZero() const;

	// Returns whether this value is arithmetic and all components are exactly one.
	bool AreAllExactlyOne() const;

	// Returns whether this value is arithmetic and all components are approximately one.
	bool AreAllNearlyOne() const;
	
	// Returns whether this value exactly equals Other.
	bool Equals(const FValue* Other) const;

	// Returns whether this value equals the given constant scalar.
	bool EqualsConstant(float Value) const;

	// Returns whether this value equals the given constant vector.
	bool EqualsConstant(FVector4f Value) const;

	// Returns the UTexture or URuntimeVirtualTexture object if this value is of type FTextureObject or FRuntimeVirtualTextureObject. Returns null otherwise.
	UObject* GetTextureObject() const;

	// Returns the uniform parameter index if this value is of type FTextureObject, FRuntimeVirtualTextureObject, or FUniformParameter. Returns INDEX_NONE otherwise.
	int32 GetUniformParameterIndex() const;

	// Tries to cast this value to specified type T and returns the casted pointer, if possible (nullptr otherwise).
	template <typename T>
	T* As() { return IsA(T::TypeKind) ? static_cast<T*>(this) : nullptr; }

	// Tries to cast this value to specified type T and returns the casted pointer, if possible (nullptr otherwise).
	template <typename T>
	const T* As() const { return IsA(T::TypeKind) ? static_cast<const T*>(this) : nullptr; }
};

// Tries to cast a value to a derived type.
// If specified value is not null, it tries to cast this value T and returns it. Otherwise, it returns null.
template <typename T>
T* As(FValue* Value)
{
	return Value && Value->IsA(T::TypeKind) ? static_cast<T*>(Value) : nullptr;
}

// Tries to cast a value to a derived type.
// If specified value is not null, it tries to cast this value T and returns it. Otherwise, it returns null.
template <typename T>
const T* As(const FValue* Value)
{
	return Value && Value->IsA(T::TypeKind) ? static_cast<const T*>(Value) : nullptr;
}

// Casts a value to an instruction.
// If specified value is not null, it tries to cast this value to an instruction and returns it. Otherwise, it returns null.
FInstruction* AsInstruction(FValue* Value);

// Casts a value to an instruction.
// If specified value is not null, it tries to cast this value to an instruction and returns it. Otherwise, it returns null.
const FInstruction* AsInstruction(const FValue* Value);

template <EValueKind TTypeKind>
struct TValue : FValue
{
	static constexpr EValueKind TypeKind = TTypeKind;
};

// A placeholder for an invalid value.
//
// A poison value represents an invalid value. It is produced by the emitter when an
// invalid operation is performed. Poison values can be passed as arguments to other operations,
// but they are "contagious": any instruction emitted with a poison value as an argument
// will itself produce a poison value.
struct FPoison : TValue<VK_Poison>
{
	static FPoison* Get();
};

// The integer type used inside MIR.
using TInteger = int64_t;

// The floating point type used inside MIR.
using TFloat = float;

// The double precision / LWC floating point type used inside MIR.
using TDouble = double;

// A constant value.
//
// A constant represents a translation-time known scalar primitive value. Operations on
// constant values can be folded by the builder, that is they can be evaluated statically
// while the builder constructs the IR graph of an input material.
struct FConstant : TValue<VK_Constant>
{
	union
	{
		bool  		Boolean;
		TInteger	Integer;
		TFloat 		Float;
		TDouble		Double;
	};

	// Returns the constant value of given type T. The type must be bool, integral or floating point.
	template <typename T>
	T Get() const
	{
		if constexpr (std::is_same_v<T, bool>)
		{
			return Boolean;
		}
		else if constexpr (std::is_integral_v<T>)
		{
			return Integer;
		}
		else if constexpr (std::is_floating_point_v<T>)
		{
			return Float;
		}
		else
		{
			check(false && "unexpected type T.");
		}
	}
};

// Specifies the axis for computing screen-space derivatives.
//
// This enum is used to indicate whether a partial derivative should be taken
// along the X or Y screen-space direction, corresponding to HLSL's ddx and ddy
// functions.
enum class EDerivativeAxis
{
	X, // Corresponding to d/dx
	Y, // Corresponding to d/dy
};

// Specify the source expression type of a derivative, useful for generating actionable user facing errors if derivatives
// are used where not allowed (vertex shader).
enum class EDerivativeSource
{
	Derivative,				// DDX or DDY expression
	TextureSampleBias,		// TextureSample expression with MipBias option selected
	AnalyticDerivative,		// Generated during calculation of an analytic derivative
};

// Enumeration of all supported material external inputs.
enum class EExternalInput
{
	None,

	TexCoord0,
	TexCoord1,
	TexCoord2,
	TexCoord3,
	TexCoord4,
	TexCoord5,
	TexCoord6,
	TexCoord7,
	WorldPosition_Absolute,					// (LWC)	WPT_Default						Absolute World Position, including Material Shader Offsets
	WorldPosition_AbsoluteNoOffsets,		// (LWC)	WPT_ExcludeAllShaderOffsets		Absolute World Position, excluding Material Shader Offsets
	WorldPosition_CameraRelative,			// (float)	WPT_CameraRelative				Camera Relative World Position, including Material Shader Offsets
	WorldPosition_CameraRelativeNoOffsets,	// (float)	WPT_CameraRelativeNoOffsets		Camera Relative World Position, excluding Material Shader Offsets
	LocalPosition_Instance,
	LocalPosition_InstanceNoOffsets,
	LocalPosition_Primitive,
	LocalPosition_PrimitiveNoOffsets,

	TexCoord0_Ddx,
	TexCoord1_Ddx,
	TexCoord2_Ddx,
	TexCoord3_Ddx,
	TexCoord4_Ddx,
	TexCoord5_Ddx,
	TexCoord6_Ddx,
	TexCoord7_Ddx,
	WorldPosition_Absolute_Ddx,				// LWC (cast from float on load)
	WorldPosition_AbsoluteNoOffsets_Ddx,	// LWC (cast from float on load)
	WorldPosition_CameraRelative_Ddx,
	WorldPosition_CameraRelativeNoOffsets_Ddx,
	LocalPosition_Instance_Ddx,
	LocalPosition_InstanceNoOffsets_Ddx,
	LocalPosition_Primitive_Ddx,
	LocalPosition_PrimitiveNoOffsets_Ddx,

	TexCoord0_Ddy,
	TexCoord1_Ddy,
	TexCoord2_Ddy,
	TexCoord3_Ddy,
	TexCoord4_Ddy,
	TexCoord5_Ddy,
	TexCoord6_Ddy,
	TexCoord7_Ddy,
	WorldPosition_Absolute_Ddy,				// LWC (cast from float on load)
	WorldPosition_AbsoluteNoOffsets_Ddy,	// LWC (cast from float on load)
	WorldPosition_CameraRelative_Ddy,
	WorldPosition_CameraRelativeNoOffsets_Ddy,
	LocalPosition_Instance_Ddy,
	LocalPosition_InstanceNoOffsets_Ddy,
	LocalPosition_Primitive_Ddy,
	LocalPosition_PrimitiveNoOffsets_Ddy,

	// Ranges of External inputs with derivatives.  There must be an equal number of value, ddx, and ddy variations.  TexCoord0 must remain first,
	// as code that translates texture coordinate indices to external input ID and back assumes this.
	WithDerivatives_First = TexCoord0,
	WithDerivatives_LastVal = LocalPosition_PrimitiveNoOffsets,
	WithDerivatives_LastDdx = LocalPosition_PrimitiveNoOffsets_Ddx,
	WithDerivatives_LastDdy = LocalPosition_PrimitiveNoOffsets_Ddy,
	WithDerivatives_Last = WithDerivatives_LastDdy,

	ActorPosition_Absolute,					// LWC
	ActorPosition_CameraRelative,
	ObjectPosition_Absolute,				// LWC
	ObjectPosition_CameraRelative,
	ViewMaterialTextureMipBias,
	ViewMaterialTextureDerivativeMultiply,
	GlobalDistanceField,					// Flag to mark that global distance fields are used, for value analyzer step.  Doesn't generate code.
	DynamicParticleParameterIndex,			// UserData stores the parameter index.
	CompilingPreviousFrame,					// Boolean value bCompilingPreviousFrame, set when compiling the vertex shader WorldPositionOffsets for the previous frame.

	Count,
};

// The number of external input derivative groups (value, ddx, ddy).
static constexpr int32 ExternalInputWithDerivativesGroups = 3;

// Number of external inputs with derivatives
static constexpr int32 ExternalInputWithDerivativesNum = (int32)EExternalInput::WithDerivatives_LastVal - (int32)EExternalInput::WithDerivatives_First + 1;

// Maximum number of supported texture coordinates.
static constexpr int32 TexCoordMaxNum = 8;

// Returns the string representation of given external input.
const TCHAR* LexToString(EExternalInput Input);

// Returns the given external input type.
FType GetExternalInputType(EExternalInput Id);

// Returns whether the given external input needs to generate non-trivial derivative expressions.
bool IsExternalInputWithDerivatives(EExternalInput Id);

// Translate an input ID to a derivative variation
EExternalInput GetExternalInputDerivative(EExternalInput Id, EDerivativeAxis Axis);

// Converts the given texture coordinate index to its corresponding external input mapping.
MIR::EExternalInput TexCoordIndexToExternalInput(int32 TexCoordIndex);

// Returns the index of the specified texture coordinates external input.
int32 ExternalInputToTexCoordIndex(EExternalInput Id);

// Returns whether the given external is a texture coordinate.
bool IsExternalInputTexCoord(EExternalInput Id);

// Returns whether the given external is a texture coordinate ddx.
bool IsExternalInputTexCoordDdx(EExternalInput Id);

// Returns whether the given external is a texture coordinate ddy.
bool IsExternalInputTexCoordDdy(EExternalInput Id);

// Returns whether the given external is a texture coordinate, a ddx or ddy.
bool IsExternalInputTexCoordOrPartialDerivative(EExternalInput Id);

// Returns whether the given external is a world position, including a ddx or ddy.
bool IsExternalInputWorldPosition(EExternalInput Id);

// It represents an external input (like texture coordinates) value.
struct FExternalInput : TValue<VK_ExternalInput>
{
	EExternalInput Id;
	uint32 UserData;				// Optional user data for certain external input types
};

// It represents a Material Parameter Collection object value.
struct FMaterialParameterCollection : TValue<VK_MaterialParameterCollection>
{
	UMaterialParameterCollection* Collection;

	// Index of this collection in the list of collections referenced by the material.
	// Note: This field is automatically set by the builder.
	uint32 Analysis_CollectionIndex;
};

// It represents a texture object value.
struct FTextureObject : TValue<VK_TextureObject>
{
	// The texture object.
	UTexture* Texture;

	// The sampler type associated to this texture object.
	EMaterialSamplerType SamplerType;

	// Index of this parameter in the uniform expression set.
	// Note: This field is automatically set by the builder.
	int32 Analysis_UniformParameterIndex;
};

// It represents a runtime virtual texture (RVT) object value. Those *do not* extend UTexture so they have their own MIR value.
struct FRuntimeVirtualTextureObject : TValue<VK_RuntimeVirtualTextureObject>
{
	// The runtime virtual texture object.
	URuntimeVirtualTexture* RVTexture;

	// The sampler type associated to this RVT object.
	EMaterialSamplerType SamplerType;

	// Field to explicitly assign a VT stack layer index (INDEX_NONE if unset). Regular VTs are assigned these indices automatically.
	int32 VTLayerIndex : 16;

	// Field to explicitly assign a VT page table index. Regular VTs are assigned these indices automatically.
	int32 VTPageTableIndex : 16;

	// Index of this parameter in the uniform expression set.
	// Note: This field is automatically set by the builder.
	int32 Analysis_UniformParameterIndex;
};

// It represents a material uniform parameter.
struct FUniformParameter: TValue<VK_UniformParameter>
{
	// Index of the parameter as registered in the module.
	uint32 ParameterIdInModule;

	// Eventual sampler type to use to sample this parameter's texture (if it is one).
	EMaterialSamplerType SamplerType;

	// Field to explicitly assign a VT stack layer index (INDEX_NONE if unset). Regular VTs are assigned these indices automatically.
	int32 VTLayerIndex : 16;

	// Field to explicitly assign a VT page table index. Regular VTs are assigned these indices automatically.
	int32 VTPageTableIndex : 16;

	// Index of this parameter in the uniform expression set
	// Note: This field is automatically set by the builder.
	int32 Analysis_UniformParameterIndex;
};

enum class EScreenTexture : uint8
{
	SceneTexture,
	UserSceneTexture,
	SceneColor,
	SceneDepth,
	SceneDepthWithoutWater,
	DBufferTexture,
};

// Reference to various types of full screen textures accessible by expressions.  These
// generally involve custom code to set data in the compilation output, extensive validation
// logic, and HLSL generation deferred to the analyzer for the case of UserSceneTextures.
struct FScreenTexture : TValue<VK_ScreenTexture>
{
	EScreenTexture TextureKind;
	union
	{
		ESceneTextureId Id;
		EDBufferTextureId DBufferId;
	};
	FName UserSceneTexture;
};

struct FShadingModel : TValue<VK_ShadingModel>
{
	EMaterialShadingModel Id;
};

/*------------------------------------ Instructions ------------------------------------*/

// A block of instructions.
//
// A block is a sequence of instructions in order of execution. Blocks are organized in a
// tree-like structure, used to model blocks nested inside other blocks.
struct FBlock
{
	// This block's parent block, if any. If this is null, this is a *root block.
	FBlock* Parent;

	// The linked-list head of the instructions contained in this block.
	// Links are contained in the FInstruction::Next field.
	FInstruction* Instructions;

	// Depth of this block in the tree structure (root blocks have level zero).
	int32 Level;

	// Finds and returns the common block between this and Other, if any. It returns null
	// if no common block was found.
	// Note: This has O(n) complexity, where n is the maximum depth of the tree structure.
	FBlock* FindCommonParentWith(MIR::FBlock* Other);
};

//
struct FInstructionLinkage
{
	// The next instruction executing after this one.
	FInstruction* Next;

	// This instruction parent block.
	FBlock* Block;

	// How many users (i.e., dependencies) this instruction has.
	uint32 NumUsers;

	// The number of users processed during instruction graph linking in each entry point.
	// Note: This information combined with NumUsers is used by the builder to push instructions
	//      in the appropriate block automatically.
	uint32 NumProcessedUsers;
};

// Base struct of an instruction value.
//
// An instruction is a value in a well defined order of evaluation.
// Instructions have a parent block and are organized in a linked list: the Next field
// indicates the instruction that will execute immediately after this one.
// Since the material shader has multiple stages, the same instruction can belong to two
// different graphs of execution, which explains why all fields in this structure have
// a different possible value per stage.
//
// Note: All fields in this struct are not expected to be set by the user when emitting
//      an instruction, and are instead automatically populated by the builder.
struct FInstruction : FValue
{
	// Array of linkage information (how this instruction is connected inside the IR graph)
	// for each entry point registered in the module.
	TArrayView<FInstructionLinkage> Linkage;

	// Returns the block in which the dependency with specified index should execute.
	FBlock* GetTargetBlockForUse(int32 EntryPointIndex, int32 UseIndex);

	// Returns the next instruction in specified entry point. 
	FInstruction* GetNext(int32 EntryPointIndex) const { return Linkage[EntryPointIndex].Next; }

	// Returns the number of user sin specified entry point.
	uint32 GetNumUsers(int32 EntryPointIndex) const { return Linkage[EntryPointIndex].NumUsers; }

	// Returns the block this instruction belongs to in specified entry point.
	const FBlock* GetBlock(int32 EntryPointIndex) const { return Linkage[EntryPointIndex].Block; }

};

template <EValueKind TTypeKind, uint32 TNumStaticUses = 0>
struct TInstruction : FInstruction
{
	// The kind of this instruction.
	static constexpr EValueKind TypeKind = TTypeKind;

	// The number of values this instruction uses statically. Some instructions have a
	// dynamic number of uses, in which case NumStaticUses is 0.
	static constexpr uint32 NumStaticUses = TNumStaticUses;
};

// An aggregate of other values.
//
// A dimensional is a fixed array of other values. This value is used to model vectors and matrices.
struct FComposite : TInstruction<VK_Composite, 0>
{
	// Returns the constant array of component values.
	TConstArrayView<FValue*> GetComponents() const;

	// Returns the mutable array of component values.
	TArrayView<FValue*> GetMutableComponents();

	// Returns whether all components are constant (i.e., they're instances of FConstant).
	bool AreComponentsConstant() const;
};

template <int TSize>
struct TComposite : FComposite
{
	FValue* Components[TSize];
};

// Instruction that sets material attribute (i.e., "BaseColor") to its value.
struct FSetMaterialOutput : TInstruction<VK_SetMaterialOutput, 1>
{
	// The value this material attribute should be set to.
	FValue* Arg;

	// The material attribute to set.
	EMaterialProperty Property;
};

// Inline HLSL instruction.
//
// A value that is the result of an arbitrary snippet of HLSL code.
//
// Note: See BaseMaterialExpressions.ini file.
struct FInlineHLSL : TInstruction<VK_InlineHLSL, 0>
{
	static constexpr int32 MaxNumArguments = 16;

	// Array of argument values.
	FValue* Arguments[MaxNumArguments];

	// Number of arguments from another expression node.
	int32 NumArguments;

	union
	{
		// The declaration this instruction refers to.
		const FMaterialExternalCodeDeclaration* ExternalCodeDeclaration;

		// The arbitrary inlined HLSL code snippet
		FSimpleStringView Code;
	};
};

// Material IR representation of the FSubstrateData shader parameter to transition legacy materials into Substrate.
struct FPromoteSubstrateParameter : TInstruction<VK_PromoteSubstrateParameter, 4>
{
	// Extra IR values for tangent and normal vectors that are transformed into world-space specifically for Substrate conversion.
	FValue* WorldSpaceTangentsAndNormals[4];

	// IR values for all property arguments that forwarded to the Substrate conversion function.
	FValue* PropertyArgs[MP_MAX];

	// Specifies what Substrate BSDF to select, default or unlit.
	bool bIsUnlit;
};

// Operator enumeration.
//
// Note: If you modify this enum, update the implementations of the helper functions below.
enum EOperator
{
	O_Invalid,

	// Unary
	UO_FirstUnaryOperator,

	// Unary operators
	UO_BitwiseNot = UO_FirstUnaryOperator, // ~(x)
	UO_Negate, // Arithmetic negation: negate(5) -> -5
	UO_Not, // Logical negation: not(true) -> false

	// Unary intrinsics
	UO_Abs,
	UO_ACos,
	UO_ACosFast,
	UO_ACosh,
	UO_ASin,
	UO_ASinFast,
	UO_ASinh,
	UO_ATan,
	UO_ATanFast,
	UO_ATanh,
	UO_Ceil,
	UO_Cos,
	UO_Cosh,
	UO_Exponential,
	UO_Exponential2,
	UO_Floor,
	UO_Frac,
	UO_IsFinite,
	UO_IsInf,
	UO_IsNan,
	UO_Length,
	UO_Logarithm,
	UO_Logarithm10,
	UO_Logarithm2,
	UO_LWCTile,
	UO_Reciprocal,
	UO_Round,
	UO_Rsqrt,
	UO_Saturate,
	UO_Sign,
	UO_Sin,
	UO_Sinh,
	UO_Sqrt,
	UO_Tan,
	UO_Tanh,
	UO_Transpose,
	UO_Truncate,
	
	BO_FirstBinaryOperator,

	// Binary comparisons
	BO_Equals = BO_FirstBinaryOperator,
	BO_GreaterThan,
	BO_GreaterThanOrEquals,
	BO_LessThan,
	BO_LessThanOrEquals,
	BO_NotEquals,

	// Binary logical
	BO_And,
	BO_Or,

	// Binary arithmetic
	BO_Add,
	BO_Subtract,
	BO_Multiply,
	BO_MatrixMultiply,
	BO_Divide,
	BO_Modulo,
	BO_BitwiseAnd,
	BO_BitwiseOr,
	BO_BitShiftLeft,
	BO_BitShiftRight,
	
	// Binary intrinsics
	BO_ATan2,
	BO_ATan2Fast,
	BO_Cross,
	BO_Distance,
	BO_Dot,
	BO_Fmod,
	BO_Max,
	BO_Min,
	BO_Pow,
	BO_Step,

	TO_FirstTernaryOperator,

	// Ternary intrinsics
	TO_Clamp = TO_FirstTernaryOperator,
	TO_Lerp,
	TO_Select,
	TO_Smoothstep,

	OperatorCount,
};

// Whether the given operator identifies a comparison operation (e.g., ">=", "==").
bool IsComparisonOperator(EOperator Op);

// Whether the given operator identifies a unary operator.
bool IsUnaryOperator(EOperator Op);

// Whether the given operator identifies a binary operator.
bool IsBinaryOperator(EOperator Op);

// Whether the given operator identifies a ternary operator.
bool IsTernaryOperator(EOperator Op);

// Returns the arity of the operator (the number of arguments it take, 1, 2 or 3).
int GetOperatorArity(EOperator Op);

// Returns the string representation of the given operator.
const TCHAR* LexToString(EOperator Op);

// A mathematical operator instruction.
//
// This instruction identifies a built-in operation on one, two or three argument values.
struct FOperator : TInstruction<VK_Operator, 3>
{
	// The first argument of the operation. This value is never null.
	FValue* AArg;

	// The second argument of the operation. This value is null for unary operators.
	FValue* BArg;

	// The third argument of the operation. This value is null for unary and binary operators.
	FValue* CArg;

	// It identifies which supported operation to carry.
	EOperator Op;
};

// A branch instruction.
//
// This instruction evaluates to one or another argument based on whether a third boolean
// condition argument is true or false. The builder will automatically place as many
// instructions as possible in the true/false inner blocks whilst respecting dependency
// requirements. This is done in an effort to avoid the unnecessary computation of the
// input value that was not selected by the condition.
struct FBranch : TInstruction<VK_Branch, 3>
{
	// The boolean condition argument used to forward the "true" or "false" argument.
	FValue* ConditionArg;

	// Value this branch evaluates to when the condition is true.
	FValue* TrueArg;

	// Value this branch evaluates to when the condition is false.
	FValue* FalseArg;

	// The inner block (in each module entry point) the subgraph evaluating `TrueArg` should be placed in.
	TArrayView<FBlock> TrueBlock;

	// The inner block (in each module entry point) the subgraph evaluating `FalseArg` should be placed in.
	TArrayView<FBlock> FalseBlock;
};

// A subscript instruction.
//
// This instruction is used to pull the inner value making a compound one. For example,
// it is used to extract an individual component of a vector value.
struct FSubscript : TInstruction<VK_Subscript, 1>
{
	// The argument to subscript.
	FValue* Arg;

	// The subscript index, i.e. the index of the component to extract.
	int32 Index;
};

// A scalar construction instruction.
//
// This instruction constructs a scalar from another (of possibly a different scalar kind).
struct FScalar : TInstruction<VK_Scalar, 1>
{
	// The initializing argument value.
	FValue* Arg;
};

// What texture gather mode to use in a texture read instruction (none indicates a sample).
enum class ETextureReadMode
{
	// Gather the four red components in a 2x2 pixel block.
	GatherRed,

	// Gather the four green components in a 2x2 pixel block.
	GatherGreen,

	// Gather the four blue components in a 2x2 pixel block.
	GatherBlue,

	// Gather the four alpha components in a 2x2 pixel block.
	GatherAlpha,

	// Texture gather with automatically calculated mip level
	MipAuto,

	// Texture gather with user specified mip level
	MipLevel,

	// Texture gather with automatically calculated mip level plus user specified bias
	MipBias,

	// Texture gather using automatically caluclated mip level based on user provided partial derivatives
	Derivatives,
};

// Returns the string representation of given mode.
const TCHAR* LexToString(ETextureReadMode Mode);

// This instruction performs texture read operaation (sample or gather).
struct FTextureRead : TInstruction<VK_TextureRead, 6>
{
	// The texture object to sample. Can be FTextureObject or FUniformParameter.
	FValue* TextureObject;

	// The texture coordinate at which to sample.
	FValue* TexCoord;

	// Optional. The mip index to sample, if any provided.
	FValue* MipValue;

	// Optional. The analytical partial derivative of the coordinates along the X axis.
	FValue* TexCoordDdx;

	// Optional. The analytical partial derivative of the coordinates along the Y axis.
	FValue* TexCoordDdy;

	// Only required for virtual textures. Must point to FVTPageTableRead value.
	FValue* VTPageTable;

	// The mip value mode to use for sampling.
	ETextureReadMode Mode : 8;

	// The sampler source mode to use for sampling.
	ESamplerSourceMode SamplerSourceMode : 8;

	// The sampler type to use for sampling.
	EMaterialSamplerType SamplerType : 8;

	// Used for VTs whether anisotropic filtering is used or not.
	bool bUseAnisoSampler : 1;
};

// This instruction reads a virtual texture (VT) page table and is allocated alongside VT sampling instructions.
struct FVTPageTableRead : TInstruction<VK_VTPageTableRead, 5>
{
	// The texture object to load the page table for.
	FValue* TextureObject;

	// The texture coordinate at which to sample.
	FValue* TexCoord;

	// Optional. The analytical partial derivative of the coordinates along the X axis.
	FValue* TexCoordDdx;

	// Optional. The analytical partial derivative of the coordinates along the Y axis.
	FValue* TexCoordDdy;

	// Used with TMVM_MipBias and TMVM_MipLevel.
	FValue* MipValue;

	// Index of the virtual texture slot. This is determined in the analysis stage.
	int32 VTStackIndex : 16;

	// Index of the VT page table. This is determined in the analysis stage.
	int32 VTPageTableIndex : 16;

	// Texture address mode for U axis.
	TextureAddress AddressU : 8;

	// Texture address mode for V axis.
	TextureAddress AddressV : 8;

	// Specifies the sampler function to read the page table, i.e. TextureLoadVirtualPageTable(), TextureLoadVirtualPageTableGrad(), or TextureLoadVirtualPageTableLevel().
	ETextureMipValueMode MipValueMode : 4;

	// Enables VT texture sampling feedback
	bool bEnableFeedback : 1;

	// Determines whether adaptive or regular VT page table loads will be emitted, i.e. TextureLoadVirtualPageTable*() or TextureLoadVirtualPageTableAdaptive*().
	bool bIsAdaptive : 1;
};

// Utility value for selecting a different value based on the execution stage.
struct FStageSwitch : TInstruction<VK_StageSwitch, 1>
{
	// The argument for to be bypassed in each stage.
	FValue* Args[NumStages];

	// Use the specified value argument in the pixel stage, and another specified
	// argument for other stages.
	void SetArgs(FValue* PixelStageArg, FValue* OtherStagesArg);
};

// Instruction that maps to hardware ddx()/ddy().
//
// Note: This is only available in stages that support hardware derivatives (e.g. pixel shader)
struct FHardwarePartialDerivative : TInstruction<VK_HardwarePartialDerivative, 1>
{
	// The value argument of ddx()/ddy()
	FValue* Arg;

	// The direction of partial derivative
	EDerivativeAxis Axis;

	// The source expression that generated this partial derivative (for error reporting)
	EDerivativeSource Source;
};

// A "no operation" instruction is a dummy instruction used to perform analysis on a value (and its dependencies)
// but not push it to the list of instructions in a block.
// A NOP instructions should effectively bahave as the default value of Arg type.
struct FNop : TInstruction<VK_Nop, 1>
{
	FValue* Arg;
};

// Kind of user-specified function.
enum class FFunctionKind
{
	HLSL,
};

// Describes a function parameter.
struct FFunctionParameter
{
	// The parameter name (used as identifier).
	FName Name;
	
	// The parameter type.
	FType Type;
};

// Maximum number of parameters a user function can have.
constexpr uint32 MaxNumFunctionParameters = 32;

// Defines a user-specified function.
struct FFunction
{
	// The kind of function this is.
	FFunctionKind Kind;

	// This function's name (could be used as identifier).
	FStringView Name;

	// The function return value type
	FType ReturnType;

	// Number of input-only parameters.
	uint16 NumInputOnlyParams;

	// Number of input-output parameters.
	uint16 NumInputAndOutputParams;

	// Total number of parameters (input-only + input-output + output-only)
	uint16 NumParameters;

	// A module-unique id assigned to this function to disambiguate it with other functions with identical name.
	uint16 UniqueId;

	// The parameters this function has (in order: input-only, then input-output, finally output-only).
	FFunctionParameter Parameters[MaxNumFunctionParameters];

	// Returns the number of output parameters (input-output + output-only).
	uint32 GetNumOutputParameters() const { return NumParameters - NumInputOnlyParams; }

	// Returns the input-output/output-only parameter with given index.
	const FFunctionParameter& GetOutputParameter(uint32 Index) const { return Parameters[Index + NumInputOnlyParams]; }

	// Returns whether parameter with specified index is input.
	bool IsInputParameter(uint32 Index) const { return Index < NumInputAndOutputParams; }
	
	// Returns whether parameter with specified index is output.
	bool IsOutputParameter(uint32 Index) const { return Index >= NumInputOnlyParams; }

	// Whether this function equals specified (they have the same data).
	bool Equals(const FFunction* Other) const;
};

// Describes an HLSL preprocessor define, .e.g "#define MyDefine 123"
struct FFunctionHLSLDefine
{
	// The define name.
	FStringView Name;
	
	// The define value.
	FStringView Value;
};

// A user-function defined with user-provided HLSL code.
struct FFunctionHLSL : FFunction
{
	// The HLSL code of the body of this function.
	FStringView Code;

	// Array of #defines to declare in the material shader.
	TConstArrayView<FFunctionHLSLDefine> Defines;

	// Array of include directives to declare in the material shader.
	TConstArrayView<FStringView> Includes;

	// Whether this function equals specified (they have the same data).
	bool Equals(const FFunctionHLSL* Other) const;
};

// A call instruction to a user-function.
struct FCall : TInstruction<VK_Call, 0>
{
	// The user-function to call.
	const FFunction* Function;

	// The arguments to pass to the function parameters.
	FValue* Arguments[MaxNumFunctionParameters];
	
	// The number of arguments.
	int NumArguments;
};

// Instruction that evaluates some user-function call output parameter result.
struct FCallParameterOutput : TInstruction<VK_CallParameterOutput, 1>
{
	// The function call.
	FValue* Call;
	
	// The index of the output parameter to fetch the value from.
	int32 	Index;
};

struct FPreshaderParameterPayload
{
	// Uniform index for RVTs. Only used for UE::Shader::EPreshaderOpcode::RuntimeVirtualTextureUniform.
	int32 UniformIndex;
};

struct FPreshaderParameter : TInstruction<VK_PreshaderParameter, 1>
{
	// Argument that points to the source parameter this dynamic parameter depends on.
	FValue* SourceParameter;

	// Specifies the opcode that needs to be encoded for this parameter. For now, only a small subset of opcodes are supported for material preshader parameters.
	UE::Shader::EPreshaderOpcode Opcode;

	// Index into UMaterialInterface::GetReferencedTextures() pointing to the source parameter's texture.
	int32 TextureIndex;

	// Payload data that depends on the opcode.
	FPreshaderParameterPayload Payload;

	// Offset into the preshader buffer of the material resource.
	uint32 Analysis_PreshaderOffset;
};

} // namespace MIR
#endif // WITH_EDITOR
