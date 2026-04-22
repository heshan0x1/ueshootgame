// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/ASTOpSwitch.h"

#include "MuT/ASTOpParameter.h"
#include "MuT/ASTOpConstantInt.h"
#include "MuT/ASTOpMeshAddMetadata.h"
#include "Containers/Map.h"
#include "Misc/AssertionMacros.h"
#include "MuR/ImagePrivate.h"
#include "MuR/ModelPrivate.h"
#include "MuR/MutableMath.h"
#include "MuR/RefCounted.h"
#include "MuR/Types.h"

namespace UE::Mutable::Private
{

	ASTOpSwitch::ASTOpSwitch()
		: Variable(this)
		, Default(this)
	{
	}


	ASTOpSwitch::~ASTOpSwitch()
	{
		// Explicit call needed to avoid recursive destruction
		ASTOp::RemoveChildren();
	}


	bool ASTOpSwitch::IsEqual(const ASTOp& OtherUntyped) const
	{
		if (GetOpType() == OtherUntyped.GetOpType())
		{
			const ASTOpSwitch* Other = static_cast<const ASTOpSwitch*>(&OtherUntyped);

			return Type     == Other->Type     && 
				   Variable == Other->Variable &&
				   Cases    == Other->Cases    && 
				   Default  == Other->Default;
		}

		return false;
	}


	UE::Mutable::Private::Ptr<ASTOp> ASTOpSwitch::Clone(MapChildFuncRef MapChild) const
	{
		Ptr<ASTOpSwitch> New = new ASTOpSwitch();

		New->Type = Type;
		New->Variable = MapChild(Variable.child());
		New->Default = MapChild(Default.child());
		for (const FCase& Case : Cases)
		{
			New->Cases.Emplace(Case.Condition, New, MapChild(Case.Branch.child()));
		}

		return New;
	}


	void ASTOpSwitch::Assert()
	{
		switch (Type)
		{
		case EOpType::NU_SWITCH:
		case EOpType::SC_SWITCH:
		case EOpType::CO_SWITCH:
		case EOpType::IM_SWITCH:
		case EOpType::ME_SWITCH:
		case EOpType::LA_SWITCH:
		case EOpType::IN_SWITCH:
		case EOpType::ED_SWITCH:
			break;
		default:
			// Unexpected Type
			check(false);
			break;
		}

		ASTOp::Assert();
	}


	uint64 ASTOpSwitch::Hash() const
	{
		uint64 Result = std::hash<uint64>()(uint64(Type));
		for (const FCase& Case : Cases)
		{
			hash_combine(Result, Case.Condition);
			hash_combine(Result, Case.Branch.child().get());
		}

		return Result;
	}


	UE::Mutable::Private::Ptr<ASTOp> ASTOpSwitch::GetFirstValidValue()
	{
		for (const FCase& Case : Cases)
		{
			if (Case.Branch)
			{
				return Case.Branch.child();
			}
		}

		return nullptr;
	}


	bool ASTOpSwitch::IsCompatibleWith(const ASTOpSwitch* Other) const
	{
		if (!Other)
		{
			return false;
		}

		if (Variable.child() != Other->Variable.child())
		{
			return false;
		}

		if (Cases.Num() != Other->Cases.Num())
		{
			return false;
		}

		for (const FCase& Case : Cases)
		{
			bool bFound = false;
			for (const FCase& OtherCase : Other->Cases)
			{
				if (Case.Condition == OtherCase.Condition)
				{
					bFound = true;
					break;
				}
			}

			if (!bFound)
			{
				return false;
			}
		}

		return true;
	}


	UE::Mutable::Private::Ptr<ASTOp> ASTOpSwitch::FindBranch(int32 Condition) const
	{
		for (const FCase& Case : Cases)
		{
			if (Case.Condition == Condition)
			{
				return Case.Branch.child();
			}
		}

		return Default.child();
	}


	void ASTOpSwitch::ForEachChild(const TFunctionRef<void(ASTChild&)> Func)
	{
		Func(Variable);
		Func(Default);

		for (FCase& Case : Cases)
		{
			Func(Case.Branch);
		}
	}


	void ASTOpSwitch::Link(FProgram& Program, FLinkerOptions*)
	{
		// Already linked?
		if (!linkedAddress)
		{
			linkedAddress = (OP::ADDRESS)Program.OpAddress.Num();
			Program.OpAddress.Add(Program.ByteCode.Num());

			OP::ADDRESS VarAddress = Variable ? Variable->linkedAddress : 0;
			OP::ADDRESS DefAddress = Default ? Default->linkedAddress : 0;

			AppendCode(Program.ByteCode, Type);
			AppendCode(Program.ByteCode, VarAddress);
			AppendCode(Program.ByteCode, DefAddress);

			const int32 CaseCount = Cases.Num();
			int32 CasesInRange = 0;
			for (int32 CaseIndex = 0; CaseIndex < CaseCount; ++CaseIndex)
			{
				for (; CaseIndex < CaseCount; ++CaseIndex)
				{
					int32 CandidateCaseIndex = CaseIndex + 1;

					if (Cases.IsValidIndex(CandidateCaseIndex) &&
						Cases[CandidateCaseIndex].Branch == Cases[CandidateCaseIndex - 1].Branch &&
						Cases[CandidateCaseIndex].Condition == Cases[CandidateCaseIndex - 1].Condition + 1)
					{
						++CasesInRange;
					}
					else
					{
						break;
					}
				}				
			}

			bool bUseRanges = CasesInRange >= CaseCount / 2;

			if (!bUseRanges)
			{
				OP::FSwitchCaseDescriptor CaseDesc;
				CaseDesc.Count = Cases.Num();
				CaseDesc.bUseRanges = false;
				AppendCode(Program.ByteCode, CaseDesc);

				for (const FCase& Case : Cases)
				{
					OP::ADDRESS CaseBranchAddress = Case.Branch ? Case.Branch->linkedAddress : 0;
					AppendCode(Program.ByteCode, Case.Condition);
					AppendCode(Program.ByteCode, CaseBranchAddress);
				}
			}
			else
			{
				OP::FSwitchCaseDescriptor CaseDesc;
				CaseDesc.Count = Cases.Num() - CasesInRange;
				CaseDesc.bUseRanges = true;

				AppendCode(Program.ByteCode, CaseDesc);

				for (int32 CaseIndex = 0; CaseIndex < CaseCount; ++CaseIndex)
				{
					int32 RangeStart = CaseIndex;
					uint32 RangeSize = 1;
					for (; CaseIndex < CaseCount; ++CaseIndex)
					{
						int32 CandidateCaseIndex = CaseIndex + 1;
						if (Cases.IsValidIndex(CandidateCaseIndex) &&
							Cases[CandidateCaseIndex].Branch == Cases[CandidateCaseIndex - 1].Branch &&
							Cases[CandidateCaseIndex].Condition == Cases[CandidateCaseIndex - 1].Condition + 1)
						{
							++RangeSize;
						}
						else
						{
							break;
						}
					}

					const FCase& FirstCase = Cases[RangeStart];
					OP::ADDRESS CaseBranchAddress = FirstCase.Branch ? FirstCase.Branch->linkedAddress : 0;
					AppendCode(Program.ByteCode, FirstCase.Condition);
					AppendCode(Program.ByteCode, RangeSize);
					AppendCode(Program.ByteCode, CaseBranchAddress);
				}
			}
		}
	}


	FImageDesc ASTOpSwitch::GetImageDesc(bool bReturnBestOption, class FGetImageDescContext* Context) const
	{
		FImageDesc Result;

		// Local context in case it is necessary
		FGetImageDescContext LocalContext;
		if (!Context)
		{
			Context = &LocalContext;
		}
		else
		{
			// Cached result?
			FImageDesc* PtrValue = Context->m_results.Find(this);
			if (PtrValue)
			{
				return *PtrValue;
			}
		}

		// In a switch we cannot guarantee the size and format.
		// We check all the options, and if they are the same we return that.
		// Otherwise, we return a descriptor with empty fields in the conflicting ones, size or format.
		// In some places this will force re-formatting of the image.
		// The code optimiser will take care then of moving the format operations down to each
		// Branch and remove the unnecessary ones.
		FImageDesc Candidate;

		bool bSameSize = true;
		bool bSameFormat = true;
		bool bSameLods = true;
		bool bFirst = true;

		if (Default)
		{
			FImageDesc ChildDesc = Default->GetImageDesc(bReturnBestOption, Context);
			Candidate = ChildDesc;
			bFirst = false;
		}

		for (int32 CaseIndex = 0; CaseIndex < Cases.Num(); ++CaseIndex)
		{
			if (Cases[CaseIndex].Branch)
			{
				FImageDesc ChildDesc = Cases[CaseIndex].Branch->GetImageDesc(bReturnBestOption, Context);
				if (bFirst)
				{
					Candidate = ChildDesc;
					bFirst = false;
				}
				else
				{
					bSameSize = bSameSize && (Candidate.m_size == ChildDesc.m_size);
					bSameFormat = bSameFormat && (Candidate.m_format == ChildDesc.m_format);
					bSameLods = bSameLods && (Candidate.m_lods == ChildDesc.m_lods);

					if (bReturnBestOption)
					{
						Candidate.m_format = GetMostGenericFormat(Candidate.m_format, ChildDesc.m_format);

						// Return the biggest size
						Candidate.m_size[0] = FMath::Max(Candidate.m_size[0], ChildDesc.m_size[0]);
						Candidate.m_size[1] = FMath::Max(Candidate.m_size[1], ChildDesc.m_size[1]);
					}
				}
			}
		}

		Result = Candidate;

		// In case of ReturnBestOption the first valid case will be used to determine size and lods.
		// Format will be the most generic from all Cases.
		if (!bSameFormat && !bReturnBestOption)
		{
			Result.m_format = EImageFormat::None;
		}

		if (!bSameSize && !bReturnBestOption)
		{
			Result.m_size = FImageSize(0, 0);
		}

		if (!bSameLods && !bReturnBestOption)
		{
			Result.m_lods = 0;
		}

		// Cache the result
		if (Context)
		{
			Context->m_results.Add(this, Result);
		}

		return Result;
	}


	void ASTOpSwitch::GetBlockLayoutSize(uint64 BlockId, int32* pBlockX, int32* pBlockY, FBlockLayoutSizeCache* cache)
	{
		switch (Type)
		{
		case EOpType::LA_SWITCH:
		{
			Ptr<ASTOp> child = GetFirstValidValue();
			if (!child)
			{
				child = Default.child();
			}

			if (child)
			{
				child->GetBlockLayoutSizeCached(BlockId, pBlockX, pBlockY, cache);
			}
			else
			{
				*pBlockX = 0;
				*pBlockY = 0;
			}
			break;
		}

		default:
			check(false);
			break;
		}
	}


	void ASTOpSwitch::GetLayoutBlockSize(int32* pBlockX, int32* pBlockY)
	{
		switch (Type)
		{
		case EOpType::IM_SWITCH:
		{
			Ptr<ASTOp> child = GetFirstValidValue();
			if (!child)
			{
				child = Default.child();
			}

			if (child)
			{
				child->GetLayoutBlockSize(pBlockX, pBlockY);
			}
			else
			{
				checkf(false, TEXT("Image switch had no options."));
			}
			break;
		}

		default:
			checkf(false, TEXT("Instruction not supported"));
		}
	}


	bool ASTOpSwitch::GetNonBlackRect(FImageRect& maskUsage) const
	{
		if (Type == EOpType::IM_SWITCH)
		{
			FImageRect local;
			bool localValid = false;
			if (Default)
			{
				localValid = Default->GetNonBlackRect(local);
				if (!localValid)
				{
					return false;
				}
			}

			for (const FCase& c : Cases)
			{
				if (c.Branch)
				{
					FImageRect branchRect;
					bool validBranch = c.Branch->GetNonBlackRect(branchRect);
					if (validBranch)
					{
						if (localValid)
						{
							local.Bound(branchRect);
						}
						else
						{
							local = branchRect;
							localValid = true;
						}
					}
					else
					{
						return false;
					}
				}
			}

			if (localValid)
			{
				maskUsage = local;
				return true;
			}
		}

		return false;
	}


	bool ASTOpSwitch::IsImagePlainConstant(FVector4f&) const
	{
		// We could check if every option is plain and exactly the same colour, but probably it is
		// not worth.
		return false;
	}


	UE::Mutable::Private::Ptr<ASTOp> ASTOpSwitch::OptimiseSemantic(const FModelOptimizationOptions&, int32 Pass) const
	{
		// Constant Condition?
		if (Variable->GetOpType() == EOpType::NU_CONSTANT)
		{
			Ptr<ASTOp> Branch = Default.child();

			const ASTOpConstantInt* typedCondition = static_cast<const ASTOpConstantInt*>(Variable.child().get());
			for (int32 o = 0; o < Cases.Num(); ++o)
			{
				if (Cases[o].Branch &&
					typedCondition->Value == (int32)Cases[o].Condition)
				{
					Branch = Cases[o].Branch.child();
					break;
				}
			}

			return Branch;
		}

		else if (Variable->GetOpType() == EOpType::NU_PARAMETER)
		{
			// If all the branches for the possible values are the same op remove the instruction
			const ASTOpParameter* ParamOp = static_cast<const ASTOpParameter*>(Variable.child().get());
			check(ParamOp);
			if(ParamOp->Parameter.PossibleValues.IsEmpty())
			{
				return nullptr;
			}

			bool bFirstValue = true;
			bool bAllSame = true;
			Ptr<ASTOp> SameBranch = nullptr;
			for (const FParameterDesc::FIntValueDesc& Value : ParamOp->Parameter.PossibleValues)
			{
				// Look for the switch Branch it would take
				Ptr<ASTOp> Branch = Default.child();
				for (const FCase& Case : Cases)
				{
					if (Case.Condition == Value.Value)
					{
						Branch = Case.Branch.child();
						break;
					}
				}

				if (bFirstValue)
				{
					bFirstValue = false;
					SameBranch = Branch;
				}
				else
				{
					if (SameBranch != Branch)
					{
						bAllSame = false;
						SameBranch = nullptr;
						break;
					}
				}
			}

	        if (bAllSame)
	        {
				return SameBranch;
	        }
		}

		// Ad-hoc logic optimization: check if all code paths leading to this operation have a switch with the same Variable
		// and the option on those switches for the path that connects to this one is always the same. In that case, we can 
		// remove this switch and replace it by the value it has for that option. 
		// This is something the generic logic optimizer should do whan re-enabled.
		{
			// List of parent operations that we have visited, and the child we have visited them from.
			TSet<TTuple<const ASTOp*, const ASTOp*>> Visited;
			Visited.Reserve(64);

			// First is parent, second is what child we are reaching the parent from. This is necessary to find out what 
			// switch Branch we reach the parent from, if it is a switch.
			TArray< TTuple<const ASTOp*, const ASTOp*>, TInlineAllocator<16>> Pending;
			ForEachParent([this,&Pending](ASTOp* Parent)
				{
					Pending.Add({ Parent,this});
				});

			bool bAllPathsHaveMatchingSwitch = true;

			// Switch option value of all parent compatible switches (if any)
			int32 MatchingSwitchOption = -1;

			while (!Pending.IsEmpty() && bAllPathsHaveMatchingSwitch)
			{
				TTuple<const ASTOp*, const ASTOp*> ParentPair = Pending.Pop();
				bool bAlreadyVisited = false;
				Visited.Add(ParentPair, &bAlreadyVisited);

				if (!bAlreadyVisited)
				{
					const ASTOp* Parent = ParentPair.Get<0>();
					const ASTOp* ParentChild = ParentPair.Get<1>();

					bool bIsMatchingSwitch = false;

					// TODO: Probably it could be a any switch, it doesn't need to be of the same Type.
					if (Parent->GetOpType() == GetOpType())
					{
						const ASTOpSwitch* ParentSwitch = static_cast<const ASTOpSwitch*>(Parent);
						check(ParentSwitch);

						// To be compatible the switch must be on the same Variable
						if (ParentSwitch->Variable==Variable)
						{
							bIsMatchingSwitch = true;
							
							// Find what switch option we are reaching it from
							bool bIsSingleOption = true;
							int OptionIndex = -1;
							for (int32 CaseIndex = 0; CaseIndex < ParentSwitch->Cases.Num(); ++CaseIndex)
							{
								if (ParentSwitch->Cases[CaseIndex].Branch.child().get() == ParentChild)
								{
									if (OptionIndex != -1)
									{
										// This means the same child is connected to more than one switch options
										// so we cannot optimize.
										// \TODO: We could if we track a "set of options" for all switches instead of just one.
										bIsSingleOption = false;
										break;
									}
									else
									{
										OptionIndex = CaseIndex;
									}
								}
							}

							// If we did reach it from one single option
							if (bIsSingleOption && OptionIndex!=-1)
							{
								if (MatchingSwitchOption<0)
								{
									MatchingSwitchOption = ParentSwitch->Cases[OptionIndex].Condition;
								}
								else if (MatchingSwitchOption!= ParentSwitch->Cases[OptionIndex].Condition)
								{
									bAllPathsHaveMatchingSwitch = false;
								}
							}
						}
					}
					
					if (!bIsMatchingSwitch)
					{
						// If it has no parents, then the optimization cannot be applied
						bool bHasParent = false;
						Parent->ForEachParent([&bHasParent,this,&Pending,Parent](ASTOp* ParentParent)
							{
								Pending.Add({ ParentParent,Parent });
								bHasParent = true;
							});

						if (!bHasParent)
						{
							// We reached a root without a matching switch along the path.
							bAllPathsHaveMatchingSwitch = false;
						}
					}
				}
			}

			if (bAllPathsHaveMatchingSwitch && MatchingSwitchOption>=0)
			{
				// We can remove this switch, all paths leading to it have the same Condition for this switches Variable.
				return FindBranch(MatchingSwitchOption);
			}

		}

		return nullptr;
	}


	Ptr<ASTOp> ASTOpSwitch::OptimiseSink(const FModelOptimizationOptions&, FOptimizeSinkContext&) const
	{
		Ptr<ASTOp> NewOp;

		// Detect if all Cases are the same op Type or they are null (same op with some branches being null).
		EOpType BranchOpType = EOpType::NONE;
		bool bSameOpTypeOrNull = true;

		if (Default)
		{
			BranchOpType = Default->GetOpType();
		}

		for (const FCase& Case : Cases)
		{
			if (!Case.Branch)
			{
				continue;
			}

			if (BranchOpType==EOpType::NONE)
			{
				BranchOpType = Case.Branch->GetOpType();
			}
			else if (Case.Branch->GetOpType() != BranchOpType)
			{
				bSameOpTypeOrNull = false;
				break;
			}
		}

		if (bSameOpTypeOrNull)
		{
			switch (BranchOpType)
			{
			case EOpType::ME_ADDMETADATA:
			{
				// Move the add tags out of the switch if all tags are the same
				bool bAllMetadataIsTheSame = true;
				TArray<FString> Tags;
				TArray<uint64> ResourceIds;
				TArray<uint32> SkeletonIds;

				if (Default)
				{
					check(Default->GetOpType() == EOpType::ME_ADDMETADATA);
					const ASTOpMeshAddMetadata* Typed = static_cast<const ASTOpMeshAddMetadata*>(Default.child().get());
					Tags = Typed->Tags;
					ResourceIds = Typed->ResourceIds;
					SkeletonIds = Typed->SkeletonIds;
				}

				for (const FCase& Case : Cases)
				{
					if (!Case.Branch)
					{
						continue;
					}

					check(Case.Branch->GetOpType() == EOpType::ME_ADDMETADATA);
					const ASTOpMeshAddMetadata* Typed = static_cast<const ASTOpMeshAddMetadata*>(Case.Branch.child().get());
					if (Tags.IsEmpty() && ResourceIds.IsEmpty() && SkeletonIds.IsEmpty())
					{
						Tags = Typed->Tags;
						ResourceIds = Typed->ResourceIds;
						SkeletonIds = Typed->SkeletonIds;
					}
					else if (
							Typed->Tags != Tags || 
							Typed->ResourceIds != ResourceIds || 
							Typed->SkeletonIds != SkeletonIds)
					{
						bAllMetadataIsTheSame = false;
						break;
					}
				}

				if (bAllMetadataIsTheSame)
				{
					Ptr<ASTOpMeshAddMetadata> New = new ASTOpMeshAddMetadata();
					New->Tags = Tags;
					New->ResourceIds = ResourceIds;
					New->SkeletonIds = SkeletonIds;

					{
						Ptr<ASTOpSwitch> NewSwitch = UE::Mutable::Private::Clone<ASTOpSwitch>(this);

						// Replace all branches removing the "add tags" operation.
						if (Default)
						{
							check(Default->GetOpType() == EOpType::ME_ADDMETADATA);
							const ASTOpMeshAddMetadata* Typed = static_cast<const ASTOpMeshAddMetadata*>(Default.child().get());
							NewSwitch->Default = Typed->Source.child();
						}

						for (int32 CaseIndex = 0; CaseIndex < Cases.Num(); ++CaseIndex)
						{
							const FCase& SourceCase = Cases[CaseIndex];
							if (!SourceCase.Branch)
							{
								continue;
							}

							FCase& NewCase = NewSwitch->Cases[CaseIndex];

							check(SourceCase.Branch->GetOpType() == EOpType::ME_ADDMETADATA);
							const ASTOpMeshAddMetadata* Typed = static_cast<const ASTOpMeshAddMetadata*>(SourceCase.Branch.child().get());
							NewCase.Branch = Typed->Source.child();
						}

						New->Source = NewSwitch;
					}

					NewOp = New;
				}
				break;
			}

			default:
				break;
			}
		}

		return NewOp;
	}


	UE::Mutable::Private::Ptr<ImageSizeExpression> ASTOpSwitch::GetImageSizeExpression() const
	{
		Ptr<ImageSizeExpression> pRes = new ImageSizeExpression;

		bool first = true;
		for (const FCase& c : Cases)
		{
			if (c.Branch)
			{
				if (first)
				{
					pRes = c.Branch->GetImageSizeExpression();
				}
				else
				{
					Ptr<ImageSizeExpression> pOther = c.Branch->GetImageSizeExpression();
					if (!(*pOther == *pRes))
					{
						pRes->type = ImageSizeExpression::ISET_UNKNOWN;
						break;
					}
				}
			}
		}

		return pRes;
	}


	FSourceDataDescriptor ASTOpSwitch::GetSourceDataDescriptor(FGetSourceDataDescriptorContext* Context) const
	{
		// Cache management
		TUniquePtr<FGetSourceDataDescriptorContext> LocalContext;
		if (!Context)
		{
			LocalContext.Reset(new FGetSourceDataDescriptorContext);
			Context = LocalContext.Get();
		}

		FSourceDataDescriptor* Found = Context->Cache.Find(this);
		if (Found)
		{
			return *Found;
		}

		// Not cached: calculate
		FSourceDataDescriptor Result;

		for (const FCase& Case : Cases)
		{
			if (Case.Branch)
			{
				FSourceDataDescriptor SourceDesc = Case.Branch->GetSourceDataDescriptor(Context);
				Result.CombineWith(SourceDesc);
			}
		}

		Context->Cache.Add(this, Result);

		return Result;
	}


}
