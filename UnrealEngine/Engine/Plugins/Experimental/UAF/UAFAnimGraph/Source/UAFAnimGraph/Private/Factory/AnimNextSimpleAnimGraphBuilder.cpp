// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factory/AnimNextSimpleAnimGraphBuilder.h"

#include "Factory/AnimGraphBuilderContext.h"
#include "Misc/HashBuilder.h"
#include "TraitCore/NodeHandle.h"
#include "TraitCore/NodeTemplate.h"
#include "TraitCore/TraitRegistry.h"
#include "TraitCore/NodeTemplateBuilder.h"
#include "TraitCore/TraitWriter.h"
#include "Traits/ReferencePoseTrait.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimNextSimpleAnimGraphBuilder)

bool FAnimNextSimpleAnimGraphBuilder::Build(UE::UAF::FAnimGraphBuilderContext& InContext) const
{
	using namespace UE::UAF;

	ensure(Stacks.Num() == 1);	// Currently only support a single stack

	if (Stacks.Num() != 1 || Stacks[0].TraitStructs.Num() == 0)
	{
		return false;
	}

	const FTraitRegistry& TraitRegistry = FTraitRegistry::Get();

	InContext.VariableStructs.Reserve(Stacks[0].TraitStructs.Num());

	struct FTraitStackData
	{
		FNodeHandle NodeHandle;
		TArray<TInstancedStruct<FAnimNextTraitSharedData>, TInlineAllocator<4>> TraitStructs;
		TArray<FTraitUID, TInlineAllocator<4>> TraitUIDs;
		TMap<TPair<uint32, FName>, int32, TInlineSetAllocator<64>> VariableIndexMap;
	};

	TArray<FTraitStackData, TInlineAllocator<4>> TraitStacks;
	TraitStacks.Reserve(Stacks.Num());

	{
		FTraitStackData TraitStackData;
		TraitStackData.TraitStructs = Stacks[0].TraitStructs;
		TraitStackData.TraitUIDs.Reserve(Stacks[0].TraitStructs.Num());
		TraitStacks.Add(MoveTemp(TraitStackData));
	}

	for (int32 TraitStackIndex = 0; TraitStackIndex < TraitStacks.Num(); ++TraitStackIndex)
	{
		FTraitStackData& TraitStackData = TraitStacks[TraitStackIndex];
		int32 LatentPropertyIndex = 0;
		int32 TraitIndex = 0;
		for (TInstancedStruct<FAnimNextTraitSharedData>& TraitStruct : TraitStackData.TraitStructs)
		{
			const FTrait* Trait = TraitRegistry.Find(TraitStruct.GetScriptStruct());
			TraitStackData.TraitUIDs.Add(Trait->GetTraitUID());
			if (TraitStackIndex == 0)
			{
				// Add the variables of the single trait stack we support as 'public interface' structs
				InContext.VariableStructs.Add(TraitStruct.GetScriptStruct());
			}

			for (TFieldIterator<FProperty> It(TraitStruct.GetScriptStruct()); It; ++It)
			{
				TraitStackData.VariableIndexMap.Add({TraitIndex, It->GetFName()}, LatentPropertyIndex);
				LatentPropertyIndex++;
			}
			TraitIndex++;
		}
	}

	// Register nodes
	auto RegisterNode = [&InContext](FTraitStackData& InTraitStackData)
	{
		TArray<uint8> NodeTemplateBuffer;
		const FNodeTemplate* NodeTemplate = FNodeTemplateBuilder::BuildNodeTemplate(InTraitStackData.TraitUIDs, NodeTemplateBuffer);
		InTraitStackData.NodeHandle = InContext.TraitWriter.RegisterNode(*NodeTemplate);
	};

	const int32 NumTraitStacks = TraitStacks.Num();
	for (int32 TraitStackIndex = 0; TraitStackIndex < NumTraitStacks; ++TraitStackIndex)
	{
		FTraitStackData& TraitStackData = TraitStacks[TraitStackIndex];
		RegisterNode(TraitStackData);

		// Single trait we support is always the root
		if (TraitStackIndex == 0)
		{
			InContext.RootTraitHandle = FAnimNextEntryPointHandle(TraitStackData.NodeHandle);
		}

		// Add new trait stack with refpose trait to cover 'unlinked' child pins
		const FTrait* Trait = TraitRegistry.Find(FAnimNextReferencePoseTraitSharedData::StaticStruct());
		const FTraitUID RefPoseUID = Trait->GetTraitUID();
		for (TInstancedStruct<FAnimNextTraitSharedData>& TraitStruct : TraitStackData.TraitStructs)
		{
			for (TFieldIterator<FProperty> It(TraitStruct.GetScriptStruct()); It; ++It)
			{
				FStructProperty* StructProperty = CastField<FStructProperty>(*It);
				if (StructProperty && StructProperty->Struct == FAnimNextTraitHandle::StaticStruct())
				{
					FTraitStackData RefPoseTraitStackData;
					TInstancedStruct<FAnimNextTraitSharedData> InstancedStruct;
					InstancedStruct.InitializeAsScriptStruct(FAnimNextReferencePoseTraitSharedData::StaticStruct());

					RefPoseTraitStackData.TraitStructs.Add(MoveTemp(InstancedStruct));
					RefPoseTraitStackData.TraitUIDs.Add(RefPoseUID);

					RegisterNode(RefPoseTraitStackData);

					// Link to this new stack
					FAnimNextTraitHandle* Handle = It->ContainerPtrToValuePtr<FAnimNextTraitHandle>(TraitStruct.GetMutableMemory());
					*Handle = FAnimNextTraitHandle(RefPoseTraitStackData.NodeHandle);

					TraitStacks.Add(RefPoseTraitStackData);
				}
			}
		}
	}

	// Write traits
	InContext.TraitWriter.BeginNodeWriting();
	for (int32 TraitStackIndex = 0; TraitStackIndex < TraitStacks.Num(); ++TraitStackIndex)
	{
		const FTraitStackData& TraitStackData = TraitStacks[TraitStackIndex];
		InContext.TraitWriter.WriteNode(TraitStackData.NodeHandle,
			[&TraitStackData](uint32 InTraitIndex, FName InPropertyName)
			{
				return TraitStackData.VariableIndexMap.FindRef({InTraitIndex, InPropertyName});
			},
			[&TraitStackData](uint32 InTraitIndex)
			{
				return TConstStructView<FAnimNextTraitSharedData>(TraitStackData.TraitStructs[InTraitIndex]);
			});
	}
	InContext.TraitWriter.EndNodeWriting();

	ensure(InContext.TraitWriter.GetErrorState() == FTraitWriter::EErrorState::None);

	return true;
}

void FAnimNextSimpleAnimGraphBuilder::ValidateTraitStruct(int32 InStackIndex, TConstStructView<FAnimNextTraitSharedData> InStruct)
{
	using namespace UE::UAF;

	ensure(InStackIndex == 0);	// Currently only support a single stack
	if (InStackIndex != 0)
	{
		return;
	}

	if (Stacks.Num() == 0)
	{
		Stacks.SetNum(InStackIndex + 1);
	}

	const FTraitRegistry& TraitRegistry = FTraitRegistry::Get();
	const FTrait* Trait = TraitRegistry.Find(InStruct.GetScriptStruct());
	if (Trait == nullptr)
	{
		ensureMsgf(false, TEXT("FAnimNextSimpleAnimGraphBuilder: Could not find registered trait"));
		return;
	}

	if (Stacks[InStackIndex].TraitStructs.Num() == 0 && Trait->GetTraitMode() != ETraitMode::Base)
	{
		ensureMsgf(false, TEXT("FAnimNextSimpleAnimGraphBuilder: First trait should always be a base trait"));
		return;
	}
}

void FAnimNextSimpleAnimGraphBuilder::PushTraitStructViewToStack(int32 InStackIndex, TConstStructView<FAnimNextTraitSharedData> InStruct)
{
	using namespace UE::UAF;

	ValidateTraitStruct(InStackIndex, InStruct);

	auto FindExistingTrait = [&InStruct](const TInstancedStruct<FAnimNextTraitSharedData>& InExistingStruct)
	{
		return InStruct.GetScriptStruct() == InExistingStruct.GetScriptStruct();
	};

	if (TInstancedStruct<FAnimNextTraitSharedData>* ExistingStruct = Stacks[InStackIndex].TraitStructs.FindByPredicate(FindExistingTrait))
	{
		*ExistingStruct = InStruct;
	}
	else
	{
		Stacks[InStackIndex].TraitStructs.Emplace(InStruct);
	}

	InvalidateKey();
}

void FAnimNextSimpleAnimGraphBuilder::PushTraitInstancedStructToStack(int32 InStackIndex, TInstancedStruct<FAnimNextTraitSharedData>&& InStruct)
{
	using namespace UE::UAF;

	ValidateTraitStruct(InStackIndex, InStruct);

	auto FindExistingTrait = [&InStruct](const TInstancedStruct<FAnimNextTraitSharedData>& InExistingStruct)
	{
		return InStruct.GetScriptStruct() == InExistingStruct.GetScriptStruct();
	};

	if (TInstancedStruct<FAnimNextTraitSharedData>* ExistingStruct = Stacks[InStackIndex].TraitStructs.FindByPredicate(FindExistingTrait))
	{
		*ExistingStruct = InStruct;
	}
	else
	{
		Stacks[InStackIndex].TraitStructs.Emplace(MoveTemp(InStruct));
	}

	InvalidateKey();
}

uint64 FAnimNextSimpleAnimGraphBuilder::RecalculateKey() const
{
	return UE::StructUtils::GetStructHash64(TConstStructView<FAnimNextSimpleAnimGraphBuilder>(*this));
}
