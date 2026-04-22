// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeModifier.h"

class UEdGraphPin;
struct FMutableGraphGenerationContext;

UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeModifier> GenerateMutableSourceModifier(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext);
