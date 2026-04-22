// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Math/NumericLimits.h"
#include "MuR/Operations.h"
#include "MuR/Ptr.h"
#include "MuT/AST.h"


namespace UE::Mutable::Private
{
struct FProgram;


//---------------------------------------------------------------------------------------------
//! Adds a material to an Instance
//---------------------------------------------------------------------------------------------
class ASTOpAddMaterial final : public ASTOp
{
public:

	//! Type of switch
	EOpType Type = EOpType::NONE;

	ASTChild Instance;
	ASTChild Material;

public:

	ASTOpAddMaterial();
	ASTOpAddMaterial(const ASTOpAddMaterial&) = delete;
	~ASTOpAddMaterial();

	EOpType GetOpType() const override { return Type; }
	uint64 Hash() const override;
	void ForEachChild(const TFunctionRef<void(ASTChild&)> F) override;
	bool IsEqual(const ASTOp& OtherUntyped) const override;
	Ptr<ASTOp> Clone(MapChildFuncRef MapChild) const override;
	void Link(FProgram& Program, FLinkerOptions* Options) override;
};


}

