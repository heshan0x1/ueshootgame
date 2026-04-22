// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/AnimNextStateTreeGraphInstanceTask.h"

#include "AnimNextAnimGraphSettings.h"
#include "StateTreeLinker.h"
#include "StateTreeExecutionContext.h"
#include "Factory/AnimNextFactoryParams.h"
#include "AnimNextStateTreeContext.h"
#include "Factory/AnimGraphFactory.h"
#include "UObject/FortniteMainBranchObjectVersion.h"

#if WITH_EDITORONLY_DATA
bool FAnimNextGraphInstanceTaskInstanceData::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FFortniteMainBranchObjectVersion::GUID);
	return false;
}

void FAnimNextGraphInstanceTaskInstanceData::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading() && Ar.CustomVer(FFortniteMainBranchObjectVersion::GUID) < FFortniteMainBranchObjectVersion::AnimNextVariableReferences)
	{
		// Prior to this we only supported 'imprecise' payloads so try to do our best be existing content, but some data loss is possible
		if (Asset != nullptr && !Asset->IsA<UAnimNextAnimationGraph>())
		{
			Parameters = UE::UAF::FAnimGraphFactory::GetDefaultParamsForObject(Asset);

			// Match any legacy parameter values by name
			if(Payload_DEPRECATED.GetNumPropertiesInBag() > 0 && Parameters.Builder.Stacks.Num() > 0)
			{
				const UPropertyBag* PropertyBag = Payload_DEPRECATED.GetPropertyBagStruct();
				const uint8* SourceContainerPtr = Payload_DEPRECATED.GetValue().GetMemory();

				auto PatchInLegacyPropertyByName = [this, SourceContainerPtr](const FPropertyBagPropertyDesc& InDesc)
				{
					for (TInstancedStruct<FAnimNextTraitSharedData>& Struct : Parameters.Builder.Stacks[0].TraitStructs)
					{
						uint8* TargetContainerPtr = Struct.GetMutableMemory();
						for (TFieldIterator<FProperty> It(Struct.GetScriptStruct()); It; ++It)
						{
							auto NameMatches = [](const FProperty* InProperty, FName InNameA, FName InNameB)
							{
								if (InNameA == InNameB)
								{
									return true;
								}

								// Match bool properties with no 'b' prefix
								if (InProperty->IsA<FBoolProperty>())
								{
									FString StringA = InNameA.ToString();
									StringA.RemoveFromStart(TEXT("b"));
									FString StringB = InNameB.ToString();
									StringB.RemoveFromStart(TEXT("b"));
									return StringA.Equals(StringB, ESearchCase::IgnoreCase);
								}
								return false; 
							};

							if (NameMatches(*It, It->GetFName(), InDesc.Name) && It->GetClass() == InDesc.CachedProperty->GetClass())
							{
								const uint8* SourcePtr = InDesc.CachedProperty->ContainerPtrToValuePtr<uint8>(SourceContainerPtr);
								uint8* TargetPtr = It->ContainerPtrToValuePtr<uint8>(TargetContainerPtr);
								It->CopyCompleteValue(TargetPtr, SourcePtr);
								break;
							}
						}
					}
				};

				for (const FPropertyBagPropertyDesc& Desc : PropertyBag->GetPropertyDescs())
				{
					PatchInLegacyPropertyByName(Desc);
				}
			}
		}
	}
}
#endif

FAnimNextStateTreeGraphInstanceTask::FAnimNextStateTreeGraphInstanceTask()
{
	// Re-selecting the same state should not cause a re-trigger of EnterState()
	bShouldStateChangeOnReselect = false;
}

bool FAnimNextStateTreeGraphInstanceTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(TraitContextHandle);	
	return true;
}

EStateTreeRunStatus FAnimNextStateTreeGraphInstanceTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FAnimNextStateTreeTraitContext& ExecContext = Context.GetExternalData(TraitContextHandle);
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (ExecContext.PushAssetOntoBlendStack(InstanceData.Asset.Get(), InstanceData.BlendOptions, InstanceData.Parameters))
	{
		return EStateTreeRunStatus::Running;
	}
	
	return EStateTreeRunStatus::Failed;
}

EStateTreeRunStatus FAnimNextStateTreeGraphInstanceTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	if (InstanceData.bContinueTicking)
	{
		FAnimNextStateTreeTraitContext& ExecContext = Context.GetExternalData(TraitContextHandle);

		InstanceData.PlaybackRatio = ExecContext.QueryPlaybackRatio();
		InstanceData.Duration = ExecContext.QueryDuration();
		InstanceData.TimeLeft = ExecContext.QueryTimeLeft();
		InstanceData.bIsLooping = ExecContext.QueryIsLooping();

		// We can get a fallback timeline with duration of 0.0f during graph init / compile.
		// Avoid finishing the task for this edge case
		// @TODO: Later on consider making 0.0f duration invalid in timeline state.
		if (InstanceData.Duration != 0.0f)
		{
			if (InstanceData.TimeLeft - InstanceData.CompleteBlendOutTime <= 0.0f && !InstanceData.bIsLooping)
			{
				Context.FinishTask(*this, EStateTreeFinishTaskType::Succeeded);
			}
		}

		return EStateTreeRunStatus::Running;
	}
	
	return EStateTreeRunStatus::Succeeded;
}

void FAnimNextStateTreeGraphInstanceTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FAnimNextStateTreeTaskBase::ExitState(Context, Transition);
}

#if WITH_EDITOR
void FAnimNextStateTreeGraphInstanceTask::PostEditInstanceDataChangeChainProperty(const FPropertyChangedChainEvent& PropertyChangedEvent, FStateTreeDataView InstanceDataView)
{
	const FProperty* Property = PropertyChangedEvent.Property;
	if(Property == nullptr)
	{
		return;
	}

	if(Property->GetFName() == GET_MEMBER_NAME_CHECKED(FAnimNextGraphInstanceTaskInstanceData, Asset))
	{
		// Repopulate payload property bag if the selected asset changes
		FAnimNextGraphInstanceTaskInstanceData& InstanceData = InstanceDataView.GetMutable<FAnimNextGraphInstanceTaskInstanceData>();

		if (InstanceData.Asset == nullptr || InstanceData.Asset.IsA<UAnimNextAnimationGraph>())
		{
			InstanceData.Parameters.Reset();
		}
		else
		{
			// TODO: Copy over matching values before overwrite to avoid losing user edits to matching trait data
			InstanceData.Parameters = UE::UAF::FAnimGraphFactory::GetDefaultParamsForObject(InstanceData.Asset);
		}
	}
}

void FAnimNextStateTreeGraphInstanceTask::GetObjectReferences(TArray<const UObject*>& OutReferencedObjects, FStateTreeDataView InstanceDataView) const
{
	if (InstanceDataView.IsValid())
	{
		OutReferencedObjects.Add(InstanceDataView.Get<FAnimNextGraphInstanceTaskInstanceData>().Asset.Get());
	}	
}
#endif

