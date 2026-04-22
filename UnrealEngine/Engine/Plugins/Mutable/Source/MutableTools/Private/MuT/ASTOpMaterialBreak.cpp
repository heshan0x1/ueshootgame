// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpMaterialBreak.h"

#include "MuT/ASTOpBoolEqualIntConst.h"
#include "HAL/PlatformMath.h"
#include "Misc/AssertionMacros.h"
#include "MuR/ModelPrivate.h"
#include "MuR/Parameters.h"
#include "MuR/RefCounted.h"
#include "MuR/Types.h"


namespace UE::Mutable::Private
{
	ASTOpMaterialBreak::ASTOpMaterialBreak()
		: Material(this)
	{
	}

	ASTOpMaterialBreak::~ASTOpMaterialBreak()
	{
		// Explicit call needed to avoid recursive destruction
		ASTOp::RemoveChildren();
	}


	void ASTOpMaterialBreak::ForEachChild(const TFunctionRef<void(ASTChild&)> f)
	{
		f(Material);
	}


	bool ASTOpMaterialBreak::IsEqual(const ASTOp& OtherUntyped) const
	{
		if (OtherUntyped.GetOpType() == GetOpType())
		{
			const ASTOpMaterialBreak* Other = static_cast<const ASTOpMaterialBreak*>(&OtherUntyped);
			return Type == Other->Type &&
				Material == Other->Material &&
				ParameterName == Other->ParameterName;
		}
		return false;
	}


	UE::Mutable::Private::Ptr<ASTOp> ASTOpMaterialBreak::Clone(MapChildFuncRef MapChild) const
	{
		Ptr<ASTOpMaterialBreak> Result = new ASTOpMaterialBreak();
		Result->Type = Type;
		Result->ParameterName = ParameterName;
		Result->Material = MapChild(Material.child());

		return Result;
	}


	EOpType ASTOpMaterialBreak::GetOpType() const
	{ 
		return Type; 
	}


	uint64 ASTOpMaterialBreak::Hash() const
	{
		uint64 res = std::hash<uint64>()(uint64(Type));
		hash_combine(res, GetTypeHash(ParameterName));
		hash_combine(res, Material.child().get());
		
		return res;
	}


	void ASTOpMaterialBreak::Assert()
	{
		ASTOp::Assert();
	}


	void ASTOpMaterialBreak::Link(FProgram& Program, FLinkerOptions*)
	{
		// Already linked?
		if (!linkedAddress)
		{
			linkedAddress = (OP::ADDRESS)Program.OpAddress.Num();
			Program.OpAddress.Add((uint32)Program.ByteCode.Num());
			AppendCode(Program.ByteCode, Type);

			OP::MaterialBreakArgs Args;
			FMemory::Memzero(Args);

			if (Material)
			{
				Args.Material = Material->linkedAddress;
			}

			Args.ParameterName = Program.AddConstant(ParameterName.ToString());

			AppendCode(Program.ByteCode, Args);
		}
	}


	FImageDesc ASTOpMaterialBreak::GetImageDesc(bool returnBestOption, FGetImageDescContext* Context) const
	{
		check(Type == EOpType::IM_MATERIAL_BREAK);

		FImageDesc Result;

		if (Material)
		{
			Result = Material->GetImageDesc();
		}

		return Result;
	}

	FSourceDataDescriptor ASTOpMaterialBreak::GetSourceDataDescriptor(FGetSourceDataDescriptorContext* Context) const
	{
		if (Material)
		{
			return Material->GetSourceDataDescriptor(Context);
		}

		return {};
	}
}
