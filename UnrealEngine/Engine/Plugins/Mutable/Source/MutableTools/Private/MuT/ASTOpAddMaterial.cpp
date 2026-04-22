// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpAddMaterial.h"

#include "HAL/PlatformMath.h"
#include "MuR/ModelPrivate.h"
#include "MuR/RefCounted.h"
#include "MuT/CodeOptimiser.h"

namespace UE::Mutable::Private
{


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
	ASTOpAddMaterial::ASTOpAddMaterial()
	: Instance(this)
	, Material(this)
{
}


	ASTOpAddMaterial::~ASTOpAddMaterial()
{
	// Explicit call needed to avoid recursive destruction
	ASTOp::RemoveChildren();
}

bool ASTOpAddMaterial::IsEqual(const ASTOp& OtherUntyped) const
{
	if (OtherUntyped.GetOpType() == GetOpType())
	{
		const ASTOpAddMaterial* Other = static_cast<const ASTOpAddMaterial*>(&OtherUntyped);
		return Instance == Other->Instance
			&& Material == Other->Material;
	}

	return false;
}

uint64 ASTOpAddMaterial::Hash() const
{
	uint64 Result = std::hash<uint64>()(uint64(Type));
	
	hash_combine(Result, Instance.child().get());
	hash_combine(Result, Material.child().get());
	
	return Result;
}

UE::Mutable::Private::Ptr<ASTOp> ASTOpAddMaterial::Clone(MapChildFuncRef MapChild) const
{
	Ptr<ASTOpAddMaterial> NewInstance = new ASTOpAddMaterial();

	NewInstance->Type = Type;
	NewInstance->Instance = MapChild(Instance.child());
	NewInstance->Material = MapChild(Material.child());

	return NewInstance;
}

void ASTOpAddMaterial::ForEachChild(const TFunctionRef<void(ASTChild&)> F)
{
	F(Instance);
	F(Material);
}

void ASTOpAddMaterial::Link(FProgram& Program, FLinkerOptions*)
{
	check(Type != EOpType::NONE);

	// Already linked?
	if (linkedAddress)
	{
		return;
	}

	OP::InstanceAddMaterialArgs Args;
	FMemory::Memzero(Args);

	if (Instance)
	{
		Args.Instance = Instance->linkedAddress;
	}

	if (Material)
	{
		Args.Material = Material->linkedAddress;
	}

	// Find out relevant parameters. \todo: this may be optimised by reusing partial
	// values in a LINK_CONTEXT or similar
	SubtreeRelevantParametersVisitorAST visitor;
	visitor.Run(Material.child());

	TArray<uint16> params;
	for (const FString& paramName : visitor.Parameters)
	{
		for (int32 i = 0; i < Program.Parameters.Num(); ++i)
		{
			const auto& param = Program.Parameters[i];
			if (param.Name == paramName)
			{
				params.Add(uint16(i));
				break;
			}
		}
	}

	params.Sort();

	auto it = Program.ParameterLists.Find(params);

	if (it != INDEX_NONE)
	{
		Args.RelevantParametersListIndex = it;
	}
	else
	{
		Args.RelevantParametersListIndex = uint32_t(Program.ParameterLists.Num());
		Program.ParameterLists.Add(params);
	}

	
	linkedAddress = (OP::ADDRESS)Program.OpAddress.Num();
	Program.OpAddress.Add((uint32_t)Program.ByteCode.Num());
	AppendCode(Program.ByteCode, Type);
	AppendCode(Program.ByteCode, Args);
}

}
