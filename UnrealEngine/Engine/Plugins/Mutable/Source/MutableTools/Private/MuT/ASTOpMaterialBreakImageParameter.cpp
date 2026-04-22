// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpMaterialBreakImageParameter.h"

#include "MuR/Types.h"


namespace UE::Mutable::Private
{
	ASTOpImageFromMaterialParameter::ASTOpImageFromMaterialParameter()
	{
	}

	ASTOpImageFromMaterialParameter::~ASTOpImageFromMaterialParameter()
	{
		// Explicit call needed to avoid recursive destruction
		ASTOp::RemoveChildren();
	}


	void ASTOpImageFromMaterialParameter::ForEachChild(const TFunctionRef<void(ASTChild&)> f)
	{
	}


	bool ASTOpImageFromMaterialParameter::IsEqual(const ASTOp& OtherUntyped) const
	{
		if (OtherUntyped.GetOpType() == GetOpType())
		{
			const ASTOpImageFromMaterialParameter* Other = static_cast<const ASTOpImageFromMaterialParameter*>(&OtherUntyped);
			return MaterialParameter == Other->MaterialParameter &&
				ParameterName == Other->ParameterName;
		}
		return false;
	}


	UE::Mutable::Private::Ptr<ASTOp> ASTOpImageFromMaterialParameter::Clone(MapChildFuncRef MapChild) const
	{
		Ptr<ASTOpImageFromMaterialParameter> Result = new ASTOpImageFromMaterialParameter();
		Result->ParameterName = ParameterName;
		Result->MaterialParameter = MaterialParameter;

		return Result;
	}


	EOpType ASTOpImageFromMaterialParameter::GetOpType() const
	{ 
		return EOpType::IM_PARAMETER_FROM_MATERIAL;
	}


	uint64 ASTOpImageFromMaterialParameter::Hash() const
	{
		uint64 res = std::hash<EOpType>()(GetOpType());
		hash_combine(res, GetTypeHash(ParameterName));
		hash_combine(res, GetTypeHash(MaterialParameter.Name));
		
		return res;
	}


	void ASTOpImageFromMaterialParameter::Assert()
	{
		ASTOp::Assert();
	}


	void ASTOpImageFromMaterialParameter::Link(FProgram& Program, FLinkerOptions*)
	{
		// Already linked?
		if (!linkedAddress)
		{
			int32 LinkedParameterIndex = Program.Parameters.Find(MaterialParameter);
			check(LinkedParameterIndex != INDEX_NONE); // check ASTOpParameter.cpp if this happens

			linkedAddress = (OP::ADDRESS)Program.OpAddress.Num();
			Program.OpAddress.Add((uint32)Program.ByteCode.Num());
			AppendCode(Program.ByteCode, GetOpType());

			OP::MaterialBreakImageParameterArgs Args;
			FMemory::Memzero(Args);

			Args.MaterialParameter = OP::ADDRESS(LinkedParameterIndex);
			Args.ParameterName = Program.AddConstant(ParameterName.ToString());

			AppendCode(Program.ByteCode, Args);
		}
	}


	FImageDesc ASTOpImageFromMaterialParameter::GetImageDesc(bool, FGetImageDescContext*) const
	{
		check(GetOpType() == EOpType::IM_PARAMETER_FROM_MATERIAL);
		return FImageDesc();
	}
}
