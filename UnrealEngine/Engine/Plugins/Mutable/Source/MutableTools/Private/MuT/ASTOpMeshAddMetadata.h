// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Operations.h"
#include "MuR/Ptr.h"
#include "MuT/AST.h"


namespace UE::Mutable::Private
{
	struct FProgram;

	/** Add tags to a mesh. */
	class ASTOpMeshAddMetadata final : public ASTOp
	{
	public:

		/** Source mesh to add tags to. */
		ASTChild Source;

		/** Tags to add. */
		TArray<FString> Tags;
	
		/** ResourceIds to add. */
		TArray<uint64> ResourceIds;

		/** SkeletonIds to add. */
		TArray<uint32> SkeletonIds;

	public:

		ASTOpMeshAddMetadata();
		ASTOpMeshAddMetadata(const ASTOpMeshAddMetadata&) = delete;
		virtual ~ASTOpMeshAddMetadata() override;

		// ASTOp interface
		virtual EOpType GetOpType() const override { return EOpType::ME_ADDMETADATA; }
		virtual uint64 Hash() const override;
		virtual void ForEachChild(const TFunctionRef<void(ASTChild&)>) override;
		virtual bool IsEqual(const ASTOp& OtherUntyped) const override;
		virtual Ptr<ASTOp> Clone(MapChildFuncRef MapChild) const override;
		virtual void Link(FProgram& Program, FLinkerOptions*) override;
		virtual FSourceDataDescriptor GetSourceDataDescriptor(FGetSourceDataDescriptorContext*) const override;
	};

}

