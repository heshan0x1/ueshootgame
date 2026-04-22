// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Operations.h"
#include "MuR/Ptr.h"
#include "MuT/AST.h"


namespace UE::Mutable::Private
{
	struct FProgram;

	/** Parameter operation. */
	class ASTOpMaterialBreak final : public ASTOp
	{
	public:

		//! Type of parameter
		EOpType Type = EOpType::NONE;

		ASTChild Material;

		FName ParameterName;

	public:

		ASTOpMaterialBreak();
		ASTOpMaterialBreak(const ASTOpMaterialBreak&) = delete;
		~ASTOpMaterialBreak() override;

		// ASTOp interface.
		virtual EOpType GetOpType() const override;
		virtual uint64 Hash() const override;
		virtual void ForEachChild(const TFunctionRef<void(ASTChild&)>) override;
		virtual bool IsEqual(const ASTOp&) const override;
		virtual Ptr<ASTOp> Clone(MapChildFuncRef ) const override;
		virtual void Assert() override;
		virtual void Link(FProgram&, FLinkerOptions*) override;
		virtual FImageDesc GetImageDesc(bool, FGetImageDescContext*) const override;
		virtual FSourceDataDescriptor GetSourceDataDescriptor(FGetSourceDataDescriptorContext*) const override;
	};

}

