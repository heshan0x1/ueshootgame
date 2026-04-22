// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Operations.h"
#include "MuT/AST.h"

namespace UE::Mutable::Private { struct FProgram; }

namespace UE::Mutable::Private
{
	class CompilerOptions;

    /** Convert constant data to different formats, based on their usage. */
    extern void DataOptimise( const CompilerOptions*, ASTOpList& roots );

}
