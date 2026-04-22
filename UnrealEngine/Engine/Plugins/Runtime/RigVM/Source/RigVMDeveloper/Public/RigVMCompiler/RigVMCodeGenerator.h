// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RigVMCore/RigVM.h"
#include "RigVMModel/RigVMGraph.h"

#include "UObject/StrongObjectPtr.h"

#define UE_API RIGVMDEVELOPER_API

class URigVMGraph;


struct FRigVMCodeGenerator
{
public:

	FRigVMCodeGenerator(const FString& InClassName, const FString& InModuleName,
		URigVMGraph* InModelToNativize, URigVM* InVMToNativize, FRigVMExtendedExecuteContext& InVMContext,
		const UScriptStruct* PublicContextStruct, TMap<FString,FRigVMOperand> InPinToOperandMap,
		int32 InMaxInstructionsPerFunction = 100)
	{
		ParseVM(InClassName, InModuleName, InModelToNativize, InVMToNativize, InVMContext, PublicContextStruct, InPinToOperandMap, InMaxInstructionsPerFunction);
	}

	UE_API FString DumpIncludes(bool bLog = false);
	UE_DEPRECATED(5.4, "Please, use DumpExternalVariables with ExtendedExecuteContext parameter")
	FString DumpExternalVariables(bool bForHeader, bool bLog = false) { return FString(); }
	UE_API FString DumpExternalVariables(const FRigVMExtendedExecuteContext& Context, bool bForHeader, bool bLog = false);
	UE_API FString DumpEntries(bool bForHeader, bool bLog = false);
	UE_API FString DumpBlockNames(bool bForHeader, bool bLog = false);
	UE_DEPRECATED(5.4, "Please, use DumpProperties with ExtendedExecuteContext parameter")
	FString DumpProperties(bool bForHeader, int32 InInstructionGroup, bool bLog = false) { return FString(); }
	UE_API FString DumpProperties(const FRigVMExtendedExecuteContext& Context, bool bForHeader, int32 InInstructionGroup, bool bLog = false);
	UE_API FString DumpDispatches(bool bLog = false);
	UE_API FString DumpRequiredUProperties(bool bLog = false);
	UE_API FString DumpInitialize(bool bLog = false);
	UE_DEPRECATED(5.4, "Please, use DumpInstructions with ExtendedExecuteContext parameter")
	FString DumpInstructions(int32 InInstructionGroup, bool bLog = false) { return FString(); }
	UE_API FString DumpInstructions(const FRigVMExtendedExecuteContext& Context, int32 InInstructionGroup, bool bLog = false);
	UE_DEPRECATED(5.4, "Please, use DumpHeader with ExtendedExecuteContext parameter")
	FString DumpHeader(bool bLog = false) { return FString(); }
	UE_API FString DumpHeader(const FRigVMExtendedExecuteContext& Context, bool bLog = false);
	UE_DEPRECATED(5.4, "Please, use DumpSource with ExtendedExecuteContext parameter")
	FString DumpSource(bool bLog = false) { return FString(); }
	UE_API FString DumpSource(const FRigVMExtendedExecuteContext& Context, bool bLog = false);
	UE_API FString DumpLines(const TArray<FString>& InLines, bool bLog = false);
	
private:

	typedef TArray<FString> FStringArray;
	typedef TMap<FString, FString> FStringMap;
	typedef TArray<FRigVMPropertyDescription> FRigVMPropertyDescriptionArray;
	typedef TTuple<FString, FString> FMappedType;
	typedef TFunction<FString(const FString&)> TStructConstGenerator;

	enum ERigVMNativizedPropertyType
	{
		Literal,
		Work,
		Sliced,
		Invalid
	};

	struct FPropertyInfo
	{
		FRigVMPropertyDescription Description;
		int32 MemoryPropertyIndex;
		ERigVMNativizedPropertyType PropertyType;
		TArray<int32> Groups;
	};

	struct FInstructionGroup
	{
		FString Entry;
		int32 First;
		int32 Last;
		int32 Depth;
		int32 ParentGroup;
		TArray<int32> ChildGroups;
		TArray<int32> RequiredLabels;

		FInstructionGroup()
			: Entry()
			, First(INDEX_NONE)
			, Last(INDEX_NONE)
			, Depth(INDEX_NONE)
			, ParentGroup(INDEX_NONE)
		{}

		FInstructionGroup(const FInstructionGroup& InOther)
			: Entry(InOther.Entry)
			, First(InOther.First)
			, Last(InOther.Last)
			, Depth(InOther.Depth)
			, ParentGroup(InOther.ParentGroup)
			, ChildGroups(InOther.ChildGroups)
			, RequiredLabels(InOther.RequiredLabels)
		{}
	};

	UE_API void Reset();
	UE_API void ParseVM(const FString& InClassName, const FString& InModuleName,
		URigVMGraph* InModelToNativize, URigVM* InVMToNativize, FRigVMExtendedExecuteContext& InVMContext,
		const UScriptStruct* PublicContextStruct, TMap<FString,FRigVMOperand> InPinToOperandMap,
		int32 InMaxInstructionsPerFunction);
	UE_API void ParseInclude(UStruct* InDependency, const FName& InMethodName = NAME_None);
	UE_API void ParseRequiredUProperties(const FRigVMExtendedExecuteContext& Context);
	UE_API void ParseMemory(const FRigVMExtendedExecuteContext& Context, FRigVMMemoryStorageStruct* InMemory);
	UE_API void ParseProperty(const FRigVMExtendedExecuteContext& Context, ERigVMMemoryType InMemoryType, const FProperty* InProperty, FRigVMMemoryStorageStruct* InMemory);
	UE_API void ParseInstructionGroups(const FRigVMExtendedExecuteContext& Context);
	UE_API FString DumpInstructions(const FRigVMExtendedExecuteContext& Context, const FString& InPrefix, int32 InFirstInstruction, int32 InLastInstruction, const FInstructionGroup& InGroup, bool bLog = false);
	UE_API FString DumpInstructions(const FRigVMExtendedExecuteContext& Context, const FString& InPrefix, const TArray<int32> InInstructionIndices, const FInstructionGroup& InGroup, bool bLog = false);
	UE_API FString GetOperandName(const FRigVMExtendedExecuteContext& Context, const FRigVMOperand& InOperand, bool bPerSlice = true, bool bAsInput = true) const;
	UE_API FString GetOperandCPPType(const FRigVMExtendedExecuteContext& Context, const FRigVMOperand& InOperand) const;
	UE_API FString GetOperandCPPBaseType(const FRigVMExtendedExecuteContext& Context, const FRigVMOperand& InOperand) const;
	static UE_API FString SanitizeName(const FString& InName, const FString& CPPType = FString()); 
	static UE_API FString SanitizeValue(const FString& InValue, const FString& CPPType, const UObject* CPPTypeObject); 
	UE_API FRigVMPropertyDescription GetPropertyDescForOperand(const FRigVMExtendedExecuteContext& Context, const FRigVMOperand& InOperand) const;
	UE_API FRigVMPropertyDescription GetPropertyForOperand(const FRigVMExtendedExecuteContext& Context, const FRigVMOperand& InOperand) const;
	UE_API const FRigVMPropertyPathDescription& GetPropertyPathForOperand(const FRigVMExtendedExecuteContext& Context, const FRigVMOperand& InOperand) const;
	UE_API int32 GetPropertyIndex(const FRigVMPropertyDescription& InProperty) const;
	UE_API int32 GetPropertyIndex(const FRigVMExtendedExecuteContext& Context, const FRigVMOperand& InOperand) const;
	UE_API ERigVMNativizedPropertyType GetPropertyType(const FRigVMPropertyDescription& InProperty) const;
	UE_API ERigVMNativizedPropertyType GetPropertyType(const FRigVMExtendedExecuteContext& Context, const FRigVMOperand& InOperand) const;
	UE_API void CheckOperand(const FRigVMExtendedExecuteContext& Context, const FRigVMOperand& InOperand) const;
	UE_API FString GetMappedType(const FString& InCPPType) const;
	UE_API const FString& GetMappedTypeSuffix(const FString& InCPPType) const;
	UE_API FString GetMappedArrayTypeName(const FString InBaseElementType) const;
	UE_API const FInstructionGroup& GetGroup(int32 InGroupIndex) const;
	UE_API bool IsInstructionPartOfGroup(int32 InInstructionIndex, int32 InGroupIndex, bool bIncludeChildGroups) const;
	UE_API bool IsPropertyPartOfGroup(int32 InPropertyIndex, int32 InGroupIndex) const;
	UE_API FString GetEntryParameters() const;
	static UE_API TArray<int32> GetInstructionIndicesFromRange(int32 First, int32 Last);

	static FString FormatArgs(const TCHAR* InFormatString, const FStringFormatOrderedArguments& InArgs)
	{
		return FString::Format(InFormatString, InArgs);
	}

	template<typename TypeA>
	static FString Format(const TCHAR* InFormatString, const TypeA& InArgA)
	{
		return FormatArgs(InFormatString, {
			FStringFormatArg(InArgA)
		});
	}

	template<typename TypeA, typename TypeB>
	static FString Format(const TCHAR* InFormatString, const TypeA& InArgA, const TypeB& InArgB)
	{
		return FormatArgs(InFormatString, {
			FStringFormatArg(InArgA),
			FStringFormatArg(InArgB)
		});
	}

	template<typename TypeA, typename TypeB, typename TypeC>
	static FString Format(const TCHAR* InFormatString, const TypeA& InArgA, const TypeB& InArgB, const TypeC& InArgC)
	{
		return FormatArgs(InFormatString, {
			FStringFormatArg(InArgA),
			FStringFormatArg(InArgB),
			FStringFormatArg(InArgC)
		});
	}

	template<typename TypeA, typename TypeB, typename TypeC, typename TypeD>
	static FString Format(const TCHAR* InFormatString,
		const TypeA& InArgA,
		const TypeB& InArgB,
		const TypeC& InArgC,
		const TypeD& InArgD
	)
	{
		return FormatArgs(InFormatString, {
			FStringFormatArg(InArgA),
			FStringFormatArg(InArgB),
			FStringFormatArg(InArgC),
			FStringFormatArg(InArgD)
		});
	}

	template<typename TypeA, typename TypeB, typename TypeC, typename TypeD, typename TypeE>
	static FString Format(const TCHAR* InFormatString,
		const TypeA& InArgA,
		const TypeB& InArgB,
		const TypeC& InArgC,
		const TypeD& InArgD,
		const TypeE& InArgE
	)
	{
		return FormatArgs(InFormatString, {
			FStringFormatArg(InArgA),
			FStringFormatArg(InArgB),
			FStringFormatArg(InArgC),
			FStringFormatArg(InArgD),
			FStringFormatArg(InArgE)
		});
	}

	template<typename TypeA, typename TypeB, typename TypeC, typename TypeD, typename TypeE, typename TypeF>
	static FString Format(const TCHAR* InFormatString,
		const TypeA& InArgA,
		const TypeB& InArgB,
		const TypeC& InArgC,
		const TypeD& InArgD,
		const TypeE& InArgE,
		const TypeF& InArgF
	)
	{
		return FormatArgs(InFormatString, {
			FStringFormatArg(InArgA),
			FStringFormatArg(InArgB),
			FStringFormatArg(InArgC),
			FStringFormatArg(InArgD),
			FStringFormatArg(InArgE),
			FStringFormatArg(InArgF)
		});
	}

	template<typename TypeA, typename TypeB, typename TypeC, typename TypeD, typename TypeE, typename TypeF, typename TypeG>
	static FString Format(const TCHAR* InFormatString,
		const TypeA& InArgA,
		const TypeB& InArgB,
		const TypeC& InArgC,
		const TypeD& InArgD,
		const TypeE& InArgE,
		const TypeF& InArgF,
		const TypeG& InArgG
	)
	{
		return FormatArgs(InFormatString, {
			FStringFormatArg(InArgA),
			FStringFormatArg(InArgB),
			FStringFormatArg(InArgC),
			FStringFormatArg(InArgD),
			FStringFormatArg(InArgE),
			FStringFormatArg(InArgF),
			FStringFormatArg(InArgG)
		});
	}

	TStrongObjectPtr<URigVMGraph> Model;
	TStrongObjectPtr<URigVM> VM;
	TMap<FString,FRigVMOperand> PinToOperandMap;
	TMap<FRigVMOperand, FString> OperandToPinMap;
	int32 MaxInstructionsPerFunction;
	FString ClassName;
	FString ModuleName;
	FString ExecuteContextType;
	
	FStringArray Libraries;
	FStringArray Includes;

	struct FRigVMDispatchInfo
	{
		FString Name;
		const FRigVMFunction* Function;
		FRigVMDispatchContext Context;
	};
	
	TMap<FString, FRigVMDispatchInfo> Dispatches;
	TMap<TRigVMTypeIndex,TTuple<FString,FString>> RequiredUProperties;
	TArray<FInstructionGroup> InstructionGroups;
	TMap<FString, FMappedType> MappedCPPTypes;
	TArray<FPropertyInfo> Properties;
	TMap<FName, int32> PropertyNameToIndex;
	TMap<FName, FName> MappedPropertyNames;
	TMap<FString, FString> OverriddenOperatorNames;

	static UE_API const TMap<FName, TStructConstGenerator>& GetStructConstGenerators();
};

#undef UE_API
