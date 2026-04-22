// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/NodeLayout.h"
#include "MuCOE/CustomizableObjectLayout.h"

class UEdGraphPin;


extern UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeLayout> CreateDefaultLayout();

extern UE::Mutable::Private::Ptr<UE::Mutable::Private::NodeLayout> CreateMutableLayoutNode(const UCustomizableObjectLayout*, bool bIgnoreLayoutWarnings);

extern UE::Mutable::Private::FSourceLayoutBlock ToMutable(const FCustomizableObjectLayoutBlock&);
