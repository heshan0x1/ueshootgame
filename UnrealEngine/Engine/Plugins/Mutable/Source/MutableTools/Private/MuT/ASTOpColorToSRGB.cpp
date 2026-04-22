// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpColorToSRGB.h"

#include "HAL/PlatformMath.h"
#include "MuR/ModelPrivate.h"
#include "MuR/RefCounted.h"
#include "MuR/Types.h"

namespace UE::Mutable::Private
{

	ASTOpColorToSRGB::ASTOpColorToSRGB()
		: Color(this)
	{
	}


	ASTOpColorToSRGB::~ASTOpColorToSRGB()
	{
		// Explicit call needed to avoid recursive destruction
		ASTOp::RemoveChildren();
	}


	bool ASTOpColorToSRGB::IsEqual(const ASTOp& OtherUntyped) const
	{
		if (OtherUntyped.GetOpType() == GetOpType())
		{
			const ASTOpColorToSRGB* Other = static_cast<const ASTOpColorToSRGB*>(&OtherUntyped);
			return Color == Other->Color;
		}
		return false;
	}


	uint64 ASTOpColorToSRGB::Hash() const
	{
		uint64 Res = std::hash<void*>()(Color.child().get());
		return Res;
	}


	UE::Mutable::Private::Ptr<ASTOp> ASTOpColorToSRGB::Clone(MapChildFuncRef MapChild) const
	{
		UE::Mutable::Private::Ptr<ASTOpColorToSRGB> New = new ASTOpColorToSRGB();
		New->Color = MapChild(Color.child());
		return New;
	}


	void ASTOpColorToSRGB::ForEachChild(const TFunctionRef<void(ASTChild&)> Func)
	{
		Func(Color);
	}


	void ASTOpColorToSRGB::Link(FProgram& Program, FLinkerOptions*)
	{
		// Already linked?
		if (!linkedAddress)
		{
			OP::ColorArgs Args;
			FMemory::Memzero(Args);

			if (Color)
			{
				Args.Color = Color->linkedAddress;
			}

			linkedAddress = (OP::ADDRESS)Program.OpAddress.Num();
			Program.OpAddress.Add(Program.ByteCode.Num());
			AppendCode(Program.ByteCode, GetOpType());
			AppendCode(Program.ByteCode, Args);
		}
	}

}
