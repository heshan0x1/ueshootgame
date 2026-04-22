// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpMeshMorph.h"

#include "MuT/ASTOpMeshAddMetadata.h"
#include "MuR/ModelPrivate.h"
#include "MuR/RefCounted.h"
#include "MuR/Types.h"
#include "HAL/PlatformMath.h"


namespace UE::Mutable::Private
{

	//---------------------------------------------------------------------------------------------
	ASTOpMeshMorph::ASTOpMeshMorph()
		: Factor(this), Base(this), Target(this)
	{
	}


	//---------------------------------------------------------------------------------------------
	ASTOpMeshMorph::~ASTOpMeshMorph()
	{
		// Explicit call needed to avoid recursive destruction
		ASTOp::RemoveChildren();
	}


	//---------------------------------------------------------------------------------------------
	bool ASTOpMeshMorph::IsEqual(const ASTOp& otherUntyped) const
	{
		if (otherUntyped.GetOpType() == GetOpType())
		{
			const ASTOpMeshMorph* other = static_cast<const ASTOpMeshMorph*>(&otherUntyped);
			return Factor == other->Factor && Base == other->Base && Target == other->Target;
		}
		return false;
	}


	//---------------------------------------------------------------------------------------------
	UE::Mutable::Private::Ptr<ASTOp> ASTOpMeshMorph::Clone(MapChildFuncRef mapChild) const
	{
		Ptr<ASTOpMeshMorph> n = new ASTOpMeshMorph();
		n->Name = Name;
		n->Factor = mapChild(Factor.child());
		n->Base = mapChild(Base.child());
		n->Target = mapChild(Target.child());
		return n;
	}


	//---------------------------------------------------------------------------------------------
	void ASTOpMeshMorph::ForEachChild(const TFunctionRef<void(ASTChild&)> f)
	{
		f(Factor);
		f(Base);
		f(Target);
	}


	//---------------------------------------------------------------------------------------------
	uint64 ASTOpMeshMorph::Hash() const
	{
		uint64 res = GetTypeHash(Name);
		hash_combine(res, Factor.child().get());
		hash_combine(res, Base.child().get());
		hash_combine(res, Target.child().get());
		return res;
	}


	//---------------------------------------------------------------------------------------------
	void ASTOpMeshMorph::Link(FProgram& program, FLinkerOptions*)
	{
		// Already linked?
		if (!linkedAddress)
		{
			linkedAddress = (OP::ADDRESS)program.OpAddress.Num();

			program.OpAddress.Add((uint32_t)program.ByteCode.Num());
			AppendCode(program.ByteCode, EOpType::ME_MORPH);

			OP::ADDRESS NameAt = program.AddConstant(Name.ToString());
			AppendCode(program.ByteCode, NameAt);
			
			OP::ADDRESS FactorAt = Factor ? Factor->linkedAddress : 0;
			AppendCode(program.ByteCode, FactorAt);

			OP::ADDRESS BaseAt = Base ? Base->linkedAddress : 0;
			AppendCode(program.ByteCode, BaseAt);

			OP::ADDRESS TargetAt = Target ? Target->linkedAddress : 0;
			AppendCode(program.ByteCode, TargetAt);
		}
	}


	//-------------------------------------------------------------------------------------------------
	UE::Mutable::Private::Ptr<ASTOp> ASTOpMeshMorph::OptimiseSink(const FModelOptimizationOptions&, FOptimizeSinkContext&) const
	{
		Ptr<ASTOp> NewOp;

		if (!Base.child())
		{
			return nullptr;
		}

		// Base optimizations
		EOpType BaseType = Base.child()->GetOpType();
		switch (BaseType)
		{

		case EOpType::ME_ADDMETADATA:
		{
			// Add the base tags after morphing
			Ptr<ASTOpMeshAddMetadata> NewAddMetadata = UE::Mutable::Private::Clone<ASTOpMeshAddMetadata>(Base.child());

			if (NewAddMetadata->Source)
			{
				Ptr<ASTOpMeshMorph> New = UE::Mutable::Private::Clone<ASTOpMeshMorph>(this);
				New->Base = NewAddMetadata->Source.child();
				NewAddMetadata->Source = New;
			}

			NewOp = NewAddMetadata;
			break;
		}

		default:
			break;

		}

		// If not optimized yet
		if (!NewOp && Target)
		{
			// Target optimizations
			EOpType MorphType = Target.child()->GetOpType();
			switch (MorphType)
			{

			case EOpType::ME_ADDMETADATA:
			{
				// Ignore the morph target tags
				const ASTOpMeshAddMetadata* AddMetadata = static_cast<const ASTOpMeshAddMetadata*>(Target.child().get());

				Ptr<ASTOpMeshMorph> New = UE::Mutable::Private::Clone<ASTOpMeshMorph>(this);
				New->Target = AddMetadata->Source.child();
				NewOp = New;

				break;
			}

			default:
				break;

			}
		}

		return NewOp;
	}


	//-------------------------------------------------------------------------------------------------
	FSourceDataDescriptor ASTOpMeshMorph::GetSourceDataDescriptor(FGetSourceDataDescriptorContext* Context) const
	{
		if (Base)
		{
			return Base->GetSourceDataDescriptor(Context);
		}

		return {};
	}

}
