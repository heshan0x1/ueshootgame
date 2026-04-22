// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpMeshAddMetadata.h"

#include "MuT/ASTOpMeshMorph.h"
#include "HAL/PlatformMath.h"
#include "MuR/ModelPrivate.h"
#include "MuR/RefCounted.h"
#include "MuR/Types.h"


namespace UE::Mutable::Private
{

	ASTOpMeshAddMetadata::ASTOpMeshAddMetadata()
		: Source(this)
	{
	}


	ASTOpMeshAddMetadata::~ASTOpMeshAddMetadata()
	{
		// Explicit call needed to avoid recursive destruction
		ASTOp::RemoveChildren();
	}


	bool ASTOpMeshAddMetadata::IsEqual(const ASTOp& OtherUntyped) const
	{
		if (OtherUntyped.GetOpType() == GetOpType())
		{
			const ASTOpMeshAddMetadata* Other = static_cast<const ASTOpMeshAddMetadata*>(&OtherUntyped);
			return 
				Source      == Other->Source      && 
				Tags        == Other->Tags        && 
				ResourceIds == Other->ResourceIds &&
				SkeletonIds == Other->SkeletonIds;
		}

		return false;
	}


	UE::Mutable::Private::Ptr<ASTOp> ASTOpMeshAddMetadata::Clone(MapChildFuncRef MapChild) const
	{
		Ptr<ASTOpMeshAddMetadata> NewOp = new ASTOpMeshAddMetadata();
		NewOp->Source = MapChild(Source.child());
		NewOp->Tags = Tags;
		NewOp->ResourceIds = ResourceIds;
		NewOp->SkeletonIds = SkeletonIds;

		return NewOp;
	}


	void ASTOpMeshAddMetadata::ForEachChild(const TFunctionRef<void(ASTChild&)> Func)
	{
		Func(Source);
	}


	uint64 ASTOpMeshAddMetadata::Hash() const
	{
		uint64 Result = std::hash<ASTOp*>()(Source.child().get());
		return Result;
	}


	void ASTOpMeshAddMetadata::Link(FProgram& Program, FLinkerOptions*)
	{
		// Already linked?
		if (!linkedAddress)
		{
			linkedAddress = (OP::ADDRESS)Program.OpAddress.Num();
	
			OP::MeshAddMetadataArgs Args;
			FMemory::Memzero(Args);
		
			Args.Source = Source ? Source->linkedAddress : 0;
			
			using OpEnumFlags = OP::MeshAddMetadataArgs::EnumFlags;

			Args.Flags = OpEnumFlags::None;
			EnumAddFlags(Args.Flags, Tags.Num()        != 1 ? OpEnumFlags::IsTagList      : OpEnumFlags::None);
			EnumAddFlags(Args.Flags, ResourceIds.Num() != 1 ? OpEnumFlags::IsResourceList : OpEnumFlags::None);
			EnumAddFlags(Args.Flags, SkeletonIds.Num() != 1 ? OpEnumFlags::IsSkeletonList : OpEnumFlags::None);

			bool bIsTagList      = EnumHasAnyFlags(Args.Flags, OpEnumFlags::IsTagList);
			bool bIsResourceList = EnumHasAnyFlags(Args.Flags, OpEnumFlags::IsResourceList);
			bool bIsSkeletonList = EnumHasAnyFlags(Args.Flags, OpEnumFlags::IsSkeletonList);
			if (bIsTagList)
			{
				Args.Tags.ListAddress = Program.AddConstant(Tags);
			}
			else
			{
				check(Tags.Num() > 0);
				Args.Tags.TagAddress = Program.AddConstant(Tags[0]);
			}

			if (bIsResourceList)
			{
				Args.ResourceIds.ListAddress = Program.AddConstant(ResourceIds);
			}
			else
			{
				check(ResourceIds.Num() > 0);
				Args.ResourceIds.ResourceId = ResourceIds[0];
			}

			if (bIsSkeletonList)
			{
				Args.SkeletonIds.ListAddress = Program.AddConstant(SkeletonIds);
			}
			else
			{
				check(SkeletonIds.Num() > 0);
				Args.SkeletonIds.SkeletonId = SkeletonIds[0];
			}

			Program.OpAddress.Add((uint32)Program.ByteCode.Num());
			AppendCode(Program.ByteCode, EOpType::ME_ADDMETADATA);
			AppendCode(Program.ByteCode, Args);
		}
	}


	FSourceDataDescriptor ASTOpMeshAddMetadata::GetSourceDataDescriptor(FGetSourceDataDescriptorContext* Context) const
	{
		if (Source)
		{
			return Source->GetSourceDataDescriptor(Context);
		}

		return {};
	}

}
