// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpMeshTransformWithBone.h"

#include "ASTOpImageTransform.h"


UE::Mutable::Private::ASTOpMeshTransformWithBone::ASTOpMeshTransformWithBone()
	: SourceMesh(this)
	, Matrix(this)
{
}

UE::Mutable::Private::ASTOpMeshTransformWithBone::~ASTOpMeshTransformWithBone()
{
	// Explicit call needed to avoid recursive destruction
	ASTOp::RemoveChildren();
}

uint64 UE::Mutable::Private::ASTOpMeshTransformWithBone::Hash() const
{
	uint64 res = std::hash<EOpType>()(GetOpType());
	hash_combine(res, SourceMesh.child());
	hash_combine(res, Matrix.child());
	hash_combine(res, BoneName.Id);
	hash_combine(res, ThresholdFactor);

	return res;
}

bool UE::Mutable::Private::ASTOpMeshTransformWithBone::IsEqual(const ASTOp& OtherUntyped) const
{
	if (GetOpType() == OtherUntyped.GetOpType())
	{
		const ASTOpMeshTransformWithBone& Other = static_cast<const ASTOpMeshTransformWithBone&>(OtherUntyped);
		return SourceMesh == Other.SourceMesh
			&& BoneName == Other.BoneName
			&& Matrix == Other.Matrix
			&& ThresholdFactor == Other.ThresholdFactor;
	}
	return false;
}

UE::Mutable::Private::Ptr<UE::Mutable::Private::ASTOp> UE::Mutable::Private::ASTOpMeshTransformWithBone::Clone(MapChildFuncRef MapChild) const
{
	Ptr<ASTOpMeshTransformWithBone> NewASTOp = new ASTOpMeshTransformWithBone();
	NewASTOp->SourceMesh = MapChild(SourceMesh.child());
	NewASTOp->Matrix = MapChild(Matrix.child());
	NewASTOp->BoneName = BoneName;
	NewASTOp->ThresholdFactor = ThresholdFactor;
	return NewASTOp;
}

void UE::Mutable::Private::ASTOpMeshTransformWithBone::ForEachChild(const TFunctionRef<void(ASTChild&)> Function)
{
	Function(SourceMesh);
	Function(Matrix);
}

void UE::Mutable::Private::ASTOpMeshTransformWithBone::Link(FProgram& Program, FLinkerOptions* Options)
{
	if (!linkedAddress)
	{
		OP::MeshTransformWithBoneArgs Args;
		FMemory::Memzero(Args);
		if (SourceMesh)
		{
			Args.SourceMesh = SourceMesh->linkedAddress;
		}

		if (Matrix)
		{
			Args.Matrix = Matrix->linkedAddress;
		}

		Args.BoneId = BoneName.Id;
		Args.ThresholdFactor = ThresholdFactor;

		linkedAddress = static_cast<OP::ADDRESS>(Program.OpAddress.Num());
		Program.OpAddress.Add(Program.ByteCode.Num());
		AppendCode(Program.ByteCode, EOpType::ME_TRANSFORMWITHBONE);
		AppendCode(Program.ByteCode, Args);
	}
}

UE::Mutable::Private::FSourceDataDescriptor UE::Mutable::Private::ASTOpMeshTransformWithBone::GetSourceDataDescriptor(FGetSourceDataDescriptorContext* Context) const
{
	if (SourceMesh)
	{
		return SourceMesh->GetSourceDataDescriptor(Context);
	}

	return {};
}
