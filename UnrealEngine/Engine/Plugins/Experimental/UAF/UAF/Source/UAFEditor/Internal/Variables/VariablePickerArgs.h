// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextExports.h"
#include "Delegates/Delegate.h"

struct FAnimNextVariableReference;
struct FAnimNextSoftVariableReference;
struct FAnimNextParamType;

namespace UE::UAF::Editor
{

// Result of a filter operation via FOnFilterVariableType
enum class EFilterVariableResult : int32
{
	Include,
	Exclude
};

// Delegate called to filter variables by type for display to the user
using FOnFilterVariableType = TDelegate<EFilterVariableResult(const FAnimNextParamType& /*InType*/)>;

// Delegate called when a variable has been picked. Graph argument is invalid when an unbound variable is chosen.
using FOnVariablePicked = TDelegate<void(const FAnimNextSoftVariableReference& /*InSoftVariableReference*/, const FAnimNextParamType& /*InType*/)>;

// Delegate called to filter variables for display to the user
using FOnFilterVariable = TDelegate<EFilterVariableResult(const FAnimNextSoftVariableReference& /*InSoftVariableReference*/)>;

// Delegate called to determined context sensitivity
using FOnIsContextSensitive = TDelegate<bool()>;

// Delegate called when context sensitivity changes
using FOnContextSensitivityChanged = TDelegate<void(bool)>;

struct FVariablePickerArgs
{
	FVariablePickerArgs() = default;

	// Delegate used to signal whether selection has changed
	FSimpleDelegate OnSelectionChanged;

	// Delegate called when a single variable has been picked
	FOnVariablePicked OnVariablePicked;

	// Delegate called to filter variables for display to the user
	FOnFilterVariable OnFilterVariable;

	// Delegate called to filter variables by type for display to the user
	FOnFilterVariableType OnFilterVariableType;

	// Delegate called to determined context sensitivity
	FOnIsContextSensitive* OnIsContextSensitive = nullptr;

	// Delegate called when context sensitivity changes
	FOnContextSensitivityChanged OnContextSensitivityChanged;

	// Default filter for AnimNext export flags to be used for variable list population
	EAnimNextExportedVariableFlags FlagInclusionFilter = EAnimNextExportedVariableFlags::Declared | EAnimNextExportedVariableFlags::Public;
	EAnimNextExportedVariableFlags FlagExclusionFilter = EAnimNextExportedVariableFlags::Referenced;
	
	// Whether the search box should be focused on widget creation
	bool bFocusSearchWidget = true;
};

}
