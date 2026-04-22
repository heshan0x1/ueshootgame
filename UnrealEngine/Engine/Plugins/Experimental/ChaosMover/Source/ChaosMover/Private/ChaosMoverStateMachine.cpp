// Copyright Epic Games, Inc. All Rights Reserved.


#include "ChaosMover/ChaosMoverStateMachine.h"

#include "Backends/ChaosMoverSubsystem.h"
#include "ChaosMover/ChaosMoverConsoleVariables.h"
#include "ChaosMover/ChaosMoverLog.h"
#include "ChaosMover/ChaosMoverSimulation.h"
#include "MoveLibrary/MovementMixer.h"
#include "MoveLibrary/MovementUtils.h"
#include "MoveLibrary/RollbackBlackboard.h"
#include "MovementModeTransition.h"
#include "MoverComponent.h"
#include "MoverDeveloperSettings.h"
#include "MoverLog.h"
#include "Templates/SubclassOf.h"
#include "Framework/Threading.h"
#include "Engine/World.h"

namespace UE::ChaosMover
{
	FMoverStateMachine::FMoverStateMachine()
	{
	}

	FMoverStateMachine::~FMoverStateMachine()
	{
	}

	void FMoverStateMachine::Init(const FInitParams& Params)
	{
		Chaos::EnsureIsInGameThreadContext();

		// Careful, this is called from the GT
		ImmediateMovementModeTransitionWeakPtr = Params.ImmediateMovementModeTransition;
		NullMovementModeWeakPtr = Params.NullMovementMode;
		Simulation = Params.Simulation;

		ClearAllMovementModes();
		ClearAllGlobalTransitions();
	}

	void FMoverStateMachine::RegisterMovementMode(FName ModeName, TWeakObjectPtr<UBaseMovementMode> Mode, bool bIsDefaultMode)
	{
		// JAH TODO: add validation and warnings for overwriting modes
		// JAH TODO: add validation of Mode

		Modes.Add(ModeName, Mode);

		if (bIsDefaultMode)
		{
			//JAH TODO: add validation that we are only overriding the default null mode
			DefaultModeName = ModeName;
		}

		Mode->OnRegistered(ModeName);
	}

	void FMoverStateMachine::UnregisterMovementMode(FName ModeName)
	{
		TWeakObjectPtr<UBaseMovementMode> ModeToUnregisterWeakPtr = Modes.FindAndRemoveChecked(ModeName);
		TStrongObjectPtr<UBaseMovementMode> ModeToUnregister = ModeToUnregisterWeakPtr.Pin();

		if (ModeToUnregister)
		{
			ModeToUnregister->OnUnregistered();
		}
	}

	void FMoverStateMachine::ClearAllMovementModes()
	{
		Modes.Empty();

		for (TPair<FName,TWeakObjectPtr<UBaseMovementMode>>& Element : Modes)
		{
			TStrongObjectPtr<UBaseMovementMode> Mode = Element.Value.Pin();

			if (Mode)
			{
				Mode->OnUnregistered();
			}
		}

		ConstructDefaultModes();	// Note that we're resetting to our defaults so we keep the null movement mode
	}

	void FMoverStateMachine::SetDefaultMode(FName NewDefaultModeName)
	{
		check(Modes.Contains(NewDefaultModeName));

		DefaultModeName = NewDefaultModeName;
	}

	FName FMoverStateMachine::GetDefaultModeName() const
	{
		return DefaultModeName;
	}

	void FMoverStateMachine::RegisterGlobalTransition(TWeakObjectPtr<UBaseMovementModeTransition> Transition)
	{
		GlobalTransitions.Add(Transition);

		Transition->OnRegistered();
	}

	void FMoverStateMachine::UnregisterGlobalTransition(TWeakObjectPtr<UBaseMovementModeTransition> Transition)
	{
		Transition->OnUnregistered();

		GlobalTransitions.Remove(Transition);
	}

	void FMoverStateMachine::ClearAllGlobalTransitions()
	{
		for (TWeakObjectPtr<UBaseMovementModeTransition> TransitionWeakPtr : GlobalTransitions)
		{
			TStrongObjectPtr<UBaseMovementModeTransition> Transition = TransitionWeakPtr.Pin();
			if (Transition)
			{
				Transition->OnUnregistered();
			}
		}

		GlobalTransitions.Empty();
	}

	void FMoverStateMachine::QueueNextMode(FName DesiredNextModeName, bool bShouldReenter)
	{
		TStrongObjectPtr<UImmediateMovementModeTransition> QueuedModeTransition = QueuedModeTransitionWeakPtr.Pin();
		if (QueuedModeTransition)
		{
			if (DesiredNextModeName != NAME_None)
			{
				const FName NextModeName = QueuedModeTransition->GetNextModeName();
				const bool bShouldNextModeReenter = QueuedModeTransition->ShouldReenter();

				if ((NextModeName != NAME_None) &&
					(NextModeName != DesiredNextModeName || bShouldReenter != bShouldNextModeReenter))
				{
					UE_LOG(LogChaosMover, Log, TEXT("%s (%s) Overwriting of queued mode change (%s, reenter: %i) with (%s, reenter: %i)"), *OwnerActorName, *UEnum::GetValueAsString(OwnerActorLocalNetRole), *NextModeName.ToString(), bShouldNextModeReenter, *DesiredNextModeName.ToString(), bShouldReenter);
				}

				if (Modes.Contains(DesiredNextModeName))
				{
					QueuedModeTransition->SetNextMode(DesiredNextModeName, bShouldReenter);
				}
				else
				{
					UE_LOG(LogChaosMover, Warning, TEXT("Attempted to queue an unregistered movement mode: %s on owner %s"), *DesiredNextModeName.ToString(), *OwnerActorName);
				}
			}
		}
	}

	void FMoverStateMachine::SetModeImmediately(FName DesiredModeName, bool bShouldReenter)
	{
		QueueNextMode(DesiredModeName, bShouldReenter);
		AdvanceToNextMode();
	}

	void FMoverStateMachine::ClearQueuedMode()
	{
		TStrongObjectPtr<UImmediateMovementModeTransition> QueuedModeTransition = QueuedModeTransitionWeakPtr.Pin();
		if (QueuedModeTransition)
		{
			QueuedModeTransition->Clear();
		}
	}

	FMovementModifierHandle FMoverStateMachine::QueueMovementModifier(TSharedPtr<FMovementModifierBase> Modifier)
	{
		if (ensure(Modifier.IsValid()))
		{
			QueuedMovementModifiers.Add(Modifier);
			Modifier->GenerateHandle();

			return Modifier->GetHandle();
		}

		return 0;
	}

	void FMoverStateMachine::CancelModifierFromHandle(FMovementModifierHandle ModifierHandle)
	{
		QueuedMovementModifiers.RemoveAll([ModifierHandle, this] (const TSharedPtr<FMovementModifierBase>& Modifier)
			{
				if (Modifier.IsValid())
				{
					if (Modifier->GetHandle() == ModifierHandle)
					{
						return true;
					}
				}
				else
				{
					return true;
				}

				return false;
			});

		ModifiersToCancel.Add(ModifierHandle);
	}

	const FMovementModifierBase* FMoverStateMachine::FindQueuedModifier(FMovementModifierHandle ModifierHandle) const
	{
		for (const TSharedPtr<FMovementModifierBase>& QueuedModifier : QueuedMovementModifiers)
		{
			if (QueuedModifier->GetHandle() == ModifierHandle)
			{
				return QueuedModifier.Get();
			}
		}

		return nullptr;
	}

	const FMovementModifierBase* FMoverStateMachine::FindQueuedModifierByType(const UScriptStruct* ModifierType) const
	{
		for (const TSharedPtr<FMovementModifierBase>& QueuedModifier : QueuedMovementModifiers)
		{
			if (QueuedModifier->GetScriptStruct() == ModifierType)
			{
				return QueuedModifier.Get();
			}
		}

		return nullptr;
	}

	void FMoverStateMachine::FlushQueuedModifiersToGroup(FMovementModifierGroup& ModifierGroup)
	{
		if (!QueuedMovementModifiers.IsEmpty())
		{
			for (TSharedPtr<FMovementModifierBase>& QueuedModifier : QueuedMovementModifiers)
			{
				ModifierGroup.QueueMovementModifier(QueuedModifier);
			}

			QueuedMovementModifiers.Empty();
		}
	}

	void FMoverStateMachine::FlushModifierCancellationsToGroup(FMovementModifierGroup& ActiveModifierGroup)
	{
		for (FMovementModifierHandle HandleToCancel : ModifiersToCancel)
		{
			ActiveModifierGroup.CancelModifierFromHandle(HandleToCancel);
		}

		ModifiersToCancel.Empty();
	}

	bool FMoverStateMachine::HasAnyInstantEffectsQueued(const FMoverTimeStep& TimeStep) const
	{
		if (!QueuedInstantEffects.IsEmpty())
		{
			for (int EffectIndex = 0; EffectIndex < QueuedInstantEffects.Num(); EffectIndex++)
			{
				const FChaosScheduledInstantMovementEffect& QueuedEffect = QueuedInstantEffects[EffectIndex];
				if (QueuedEffect.ScheduledEffect.ShouldExecuteAtFrame(TimeStep.ServerFrame))
				{
					return true;
				}
			}
		}
		return false;
	}

	void FMoverStateMachine::ReceiveInstantMovementEffects(const FMoverTimeStep& TimeStep, const FChaosNetInstantMovementEffectsQueue* InstantMovementEffectsQueue)
	{
		InstantMovementEffectsIDHistory.CullOldFrames(TimeStep.ServerFrame, CVars::InstantMovementEffectIDHistorySize);

		// Copy queued instant movement effects from the input to the state machine
		// After this, the state machine works on its own queue, into which it can enqueue instant movement effects while stepping
		for (const FChaosNetInstantMovementEffect& NetInstantMovementEffect : InstantMovementEffectsQueue->Effects)
		{
			if (ensure(NetInstantMovementEffect.Effect.IsValid()))
			{
				if (!InstantMovementEffectsIDHistory.WasIDAlreadySeen(NetInstantMovementEffect.UniqueID))
				{
					QueueInstantMovementEffect(FChaosScheduledInstantMovementEffect{ NetInstantMovementEffect.IssuanceServerFrame, NetInstantMovementEffect.bShouldRollBack, NetInstantMovementEffect.AsScheduledInstantMovementEffect() });

					InstantMovementEffectsIDHistory.AddID(TimeStep.ServerFrame, NetInstantMovementEffect.UniqueID);

#if !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
					if (const FChaosMoverSimulationDefaultInputs* SimInputs = Simulation->GetLocalSimInput().FindDataByType<FChaosMoverSimulationDefaultInputs>())
					{
						UE_LOG(LogChaosMover, Verbose, TEXT("(%s, %s, Current Frame = %d) FMoverStateMachine::ReceiveInstantMovementEffects: Received Instant Effect (ID=%d), issued at frame %d scheduled for frame %d: %s"),
							*ToString(SimInputs->OwningActor->GetNetMode()), SimInputs->bIsGeneratingInputsLocally ? TEXT("Input Source") : TEXT("Input Consumer"), TimeStep.ServerFrame,
							NetInstantMovementEffect.UniqueID, NetInstantMovementEffect.IssuanceServerFrame, NetInstantMovementEffect.ExecutionServerFrame, NetInstantMovementEffect.Effect.IsValid() ? *NetInstantMovementEffect.Effect.Get().ToSimpleString() : TEXT("INVALID INSTANT EFFECT"));
					}
#endif // !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
				}
				else
				{
#if !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
					if (const FChaosMoverSimulationDefaultInputs* SimInputs = Simulation->GetLocalSimInput().FindDataByType<FChaosMoverSimulationDefaultInputs>())
					{
						UE_LOG(LogChaosMover, Verbose, TEXT("(%s, %s, Current Frame = %d) FMoverStateMachine::ReceiveInstantMovementEffects: Skipping Instant Effect (ID=%d), ID already seen!"),
							*ToString(SimInputs->OwningActor->GetNetMode()), SimInputs->bIsGeneratingInputsLocally ? TEXT("Input Source") : TEXT("Input Consumer"), TimeStep.ServerFrame,
							NetInstantMovementEffect.UniqueID);
					}
#endif // !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
				}
			}
		}
	}

	void FMoverStateMachine::OnSimulationTick(const FMoverTimeStep& TimeStep, const FMoverTickStartData& StartState, UMoverBlackboard* SimBlackboard, UMovementMixer* MovementMixer, FMoverTickEndData& OutputState)
	{
		FMoverTimeStep SubTimeStep = TimeStep;
		WorkingSubstepStartData = StartState;
		bool bIsWorkingStartStateReady = true;	// this flag is used to avoid unneeded data copying after substeps

		if (!ensure(MovementMixer))
		{
			return;
		}

		InternalSimTimeMs = TimeStep.BaseSimTimeMs;
		InternalServerFrame = TimeStep.ServerFrame;

		// Copy queued instant movement effects from the input to the state machine
		// After this, the state machine works on its own queue, into which it can enqueue instant movement effects while stepping
		if (const FChaosNetInstantMovementEffectsQueue* InstantMovementEffectsQueue = StartState.InputCmd.InputCollection.FindDataByType<FChaosNetInstantMovementEffectsQueue>())
		{
			ReceiveInstantMovementEffects(TimeStep, InstantMovementEffectsQueue);
		}

		TStrongObjectPtr<UImmediateMovementModeTransition> QueuedModeTransition = QueuedModeTransitionWeakPtr.Pin();
		if (QueuedModeTransition && !QueuedModeTransition->IsSet())
		{
			QueueNextMode(WorkingSubstepStartData.SyncState.MovementMode);
		}

		AdvanceToNextMode();

		int SubStepCount = 0;
		const int32 MaxConsecutiveFullRefundedSubsteps = GetDefault<UMoverDeveloperSettings>()->MaxTimesToRefundSubstep;
		int32 NumConsecutiveFullRefundedSubsteps = 0;

		float TotalUsedMs = 0.0f;
		while (TotalUsedMs < TimeStep.StepMs)
		{
			InternalSimTimeMs = SubTimeStep.BaseSimTimeMs;

			if (!bIsWorkingStartStateReady)
			{
				WorkingSubstepStartData.SyncState = OutputState.SyncState;
				WorkingSubstepStartData.AuxState = OutputState.AuxState;
				bIsWorkingStartStateReady = true;
			}

			WorkingSubstepStartData.SyncState.MovementMode = CurrentModeName;

			FMoverDefaultSyncState* OutputSyncState = &OutputState.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>();
			OutputState.SyncState.MovementMode = CurrentModeName;

			OutputState.MovementEndState.ResetToDefaults();

			SubTimeStep.StepMs = TimeStep.StepMs - TotalUsedMs;		// TODO: convert this to an overridable function that can support MaxStepTime, MaxIterations, etc.

			// Transfer any queued moves into the starting state. They'll be started during the move generation.
			FlushQueuedMovesToGroup(WorkingSubstepStartData.SyncState.LayeredMoves);
			OutputState.SyncState.LayeredMoves = WorkingSubstepStartData.SyncState.LayeredMoves;

			FlushQueuedModifiersToGroup(WorkingSubstepStartData.SyncState.MovementModifiers);
			OutputState.SyncState.MovementModifiers = WorkingSubstepStartData.SyncState.MovementModifiers;

			bool bModeSetFromInstantEffect = false;
			// Apply any instant effects that were queued up between ticks
			if (ApplyInstantEffects(WorkingSubstepStartData, SubTimeStep, OutputState.SyncState))
			{
				// Copying over our sync state collection to SubstepStartData so it is effectively the input sync state later for the movement mode. Doing this makes sure state modification from Instant Effects isn't overridden later by the movement mode
				for (auto SyncDataIt = OutputState.SyncState.SyncStateCollection.GetCollectionDataIterator(); SyncDataIt; ++SyncDataIt)
				{
					if (SyncDataIt->Get())
					{
						WorkingSubstepStartData.SyncState.SyncStateCollection.AddDataByCopy(SyncDataIt->Get());
					}
				}

				if (CurrentModeName != OutputState.SyncState.MovementMode)
				{
					bModeSetFromInstantEffect = true;
					SetModeImmediately(OutputState.SyncState.MovementMode);
					WorkingSubstepStartData.SyncState.MovementMode = CurrentModeName;
				}
			}

			FMovementModifierParams_Async ModifierParams(Simulation.Get(), &WorkingSubstepStartData.SyncState, &SubTimeStep);

			FMovementModifierGroup& CurrentModifiers = OutputState.SyncState.MovementModifiers;
			FlushModifierCancellationsToGroup(CurrentModifiers);
			TArray<TSharedPtr<FMovementModifierBase>> ActiveModifiers = CurrentModifiers.GenerateActiveModifiers_Async(ModifierParams);

			for (TSharedPtr<FMovementModifierBase> Modifier : ActiveModifiers)
			{
				Modifier->OnPreMovement_Async(ModifierParams);
			}

			FLayeredMoveGroup& CurrentLayeredMoves = OutputState.SyncState.LayeredMoves;

			// Gather any layered move contributions
			FProposedMove CombinedLayeredMove;
			CombinedLayeredMove.MixMode = EMoveMixMode::AdditiveVelocity;
			bool bHasLayeredMoveContributions = false;
			MovementMixer->ResetMixerState();

			TArray<TSharedPtr<FLayeredMoveBase>> ActiveMoves = CurrentLayeredMoves.GenerateActiveMoves_Async(SubTimeStep, SimBlackboard);

			// Tick and accumulate all active moves
			// Gather all proposed moves and distill this into a cumulative movement report. May include separate additive vs override moves.
			// TODO: may want to sort by priority or other factors
			for (TSharedPtr<FLayeredMoveBase>& ActiveMove : ActiveMoves)
			{
				FProposedMove MoveStep;
				MoveStep.MixMode = ActiveMove->MixMode;	// Initialize using the move's mixmode, but allow it to be changed in GenerateMove

				if (ActiveMove->GenerateMove_Async(WorkingSubstepStartData, SubTimeStep, SimBlackboard, MoveStep))
				{
					// If this active move is already past it's first tick we don't need to set the preferred mode again
					if (ActiveMove->StartSimTimeMs < SubTimeStep.BaseSimTimeMs)
					{
						MoveStep.PreferredMode = NAME_None;
					}

					bHasLayeredMoveContributions = true;
					MovementMixer->MixLayeredMove(*ActiveMove, MoveStep, CombinedLayeredMove);
				}
			}

			if (bHasLayeredMoveContributions && !CombinedLayeredMove.PreferredMode.IsNone() && !bModeSetFromInstantEffect)
			{
				SetModeImmediately(CombinedLayeredMove.PreferredMode);
				OutputState.SyncState.MovementMode = CurrentModeName;
			}

			// Merge proposed movement from the current mode with movement from layered moves
			if (!CurrentModeName.IsNone() && Modes.Contains(CurrentModeName))
			{
				TStrongObjectPtr<UBaseMovementMode> CurrentMode = Modes[CurrentModeName].Pin();
				FProposedMove CombinedMove;
				bool bHasModeMoveContribution = false;

				if (!CVars::bSkipGenerateMoveIfOverridden ||
					!(bHasLayeredMoveContributions && CombinedLayeredMove.MixMode == EMoveMixMode::OverrideAll))
				{
					QUICK_SCOPE_CYCLE_COUNTER(STAT_GenerateMoveFromMode);
					CurrentMode->GenerateMove(WorkingSubstepStartData, SubTimeStep, OUT CombinedMove);

					bHasModeMoveContribution = true;
				}

				if (bHasModeMoveContribution && bHasLayeredMoveContributions)
				{
					FVector UpDir = FVector::UpVector;
					if (const FChaosMoverSimulationDefaultInputs* DefaultSimInputs = Simulation->GetLocalSimInput().FindDataByType<FChaosMoverSimulationDefaultInputs>())
					{
						UpDir = DefaultSimInputs->UpDir;
					}

					MovementMixer->MixProposedMoves(CombinedLayeredMove, UpDir, CombinedMove);
				}
				else if (bHasLayeredMoveContributions && !bHasModeMoveContribution)
				{
					CombinedMove = CombinedLayeredMove;
				}

				// Apply any layered move finish velocity settings
				if (CurrentLayeredMoves.bApplyResidualVelocity)
				{
					CombinedMove.LinearVelocity = CurrentLayeredMoves.ResidualVelocity;
				}
				if (CurrentLayeredMoves.ResidualClamping >= 0.0f)
				{
					CombinedMove.LinearVelocity = CombinedMove.LinearVelocity.GetClampedToMaxSize(CurrentLayeredMoves.ResidualClamping);
				}
				CurrentLayeredMoves.ResetResidualVelocity();

				// We need to replace this with some async equivalent (calling back to FSimulation? an optional FinalMoveProcessor object, a bit like the optional MoveMixer?)
				// SyncTickParams->MoverComponent->ProcessGeneratedMovement.ExecuteIfBound(SubstepStartData, SubTimeStep, OUT CombinedMove);

				// Execute the combined proposed move
				{
					// WorkingSimTickParams.MovingComps is left empty in the async case, so we don't access resources used by the concurrent gameplay thread
					WorkingSimTickParams.StartState = WorkingSubstepStartData;
					WorkingSimTickParams.SimBlackboard = SimBlackboard;
					WorkingSimTickParams.TimeStep = SubTimeStep;
					WorkingSimTickParams.ProposedMove = CombinedMove;

					// Check for any transitions, first those registered with the current movement mode, then global ones that could occur from any mode
					FTransitionEvalResult EvalResult = FTransitionEvalResult::NoTransition;
					TStrongObjectPtr<UBaseMovementModeTransition> TransitionToTrigger;

					for (UBaseMovementModeTransition* Transition : CurrentMode->Transitions)
					{
						if (IsValid(Transition) && ((SubStepCount == 0) || !Transition->bFirstSubStepOnly))
						{
							EvalResult = Transition->Evaluate(WorkingSimTickParams);

							if (!EvalResult.NextMode.IsNone())
							{
								if (EvalResult.NextMode != CurrentModeName || Transition->bAllowModeReentry)
								{
									TransitionToTrigger = TStrongObjectPtr<UBaseMovementModeTransition>(Transition);
									break;
								}
							}
						}
					}

					if (TransitionToTrigger == nullptr)
					{
						for (TWeakObjectPtr<UBaseMovementModeTransition> TransitionWeakPtr : GlobalTransitions)
						{
							TStrongObjectPtr<UBaseMovementModeTransition> Transition = TransitionWeakPtr.Pin();
							if (Transition)
							{
								EvalResult = Transition->Evaluate(WorkingSimTickParams);

								if (!EvalResult.NextMode.IsNone())
								{
									if (EvalResult.NextMode != CurrentModeName || Transition->bAllowModeReentry)
									{
										TransitionToTrigger = Transition;
										break;
									}
								}
							}
						}
					}

					if (TransitionToTrigger && !EvalResult.NextMode.IsNone())
					{
						OutputState.MovementEndState.NextModeName = EvalResult.NextMode;
						OutputState.MovementEndState.RemainingMs = WorkingSimTickParams.TimeStep.StepMs; 	// Pass all remaining time to next mode
						TransitionToTrigger->Trigger(WorkingSimTickParams);
					}
					else
					{
						CurrentMode->SimulationTick(WorkingSimTickParams, OutputState);
					}

					OutputState.MovementEndState.RemainingMs = FMath::Clamp(OutputState.MovementEndState.RemainingMs, 0.0f, SubTimeStep.StepMs);
				}

				QueueNextMode(OutputState.MovementEndState.NextModeName);

				// Check if all of the time for this Substep was refunded
				if (FMath::IsNearlyEqual(SubTimeStep.StepMs, OutputState.MovementEndState.RemainingMs, UE_KINDA_SMALL_NUMBER))
				{
					NumConsecutiveFullRefundedSubsteps++;
					// if we've done this sub step a lot before go ahead and just advance time to avoid freezing editor
					if (NumConsecutiveFullRefundedSubsteps >= MaxConsecutiveFullRefundedSubsteps)
					{
						UE_LOG(LogChaosMover, Warning, TEXT("Movement mode %s and %s on %s are stuck giving time back to each other. Overriding to advance to next substep."),
							*CurrentModeName.ToString(),
							*OutputState.MovementEndState.NextModeName.ToString(),
							*OwnerActorName);
						TotalUsedMs += SubTimeStep.StepMs;
					}
				}
				else
				{
					NumConsecutiveFullRefundedSubsteps = 0;
				}

				//GEngine->AddOnScreenDebugMessage(-1, -0.1f, FColor::White, FString::Printf(TEXT("NextModeName: %s  Queued: %s"), *Output.MovementEndState.NextModeName.ToString(), *NextModeName.ToString()));
			}

			// Switch modes if necessary (note that this will allow exit/enter on the same state)
			AdvanceToNextMode();
			OutputState.SyncState.MovementMode = CurrentModeName;

			for (TSharedPtr<FMovementModifierBase> Modifier : ActiveModifiers)
			{
				Modifier->OnPostMovement_Async(ModifierParams);
			}

			const float RemainingMs = FMath::Clamp(OutputState.MovementEndState.RemainingMs, 0.0f, SubTimeStep.StepMs);
			const float SubstepUsedMs = (SubTimeStep.StepMs - RemainingMs);
			SubTimeStep.BaseSimTimeMs += SubstepUsedMs;
			TotalUsedMs += SubstepUsedMs;
			SubTimeStep.StepMs = RemainingMs;

			bIsWorkingStartStateReady = false;
			++SubStepCount;
		}

		InternalSimTimeMs = TimeStep.BaseSimTimeMs + TotalUsedMs;

		if (HasAnyInstantEffectsQueued(TimeStep))
		{
			if (!bIsWorkingStartStateReady)
			{
				WorkingSubstepStartData.SyncState = OutputState.SyncState;
				WorkingSubstepStartData.AuxState = OutputState.AuxState;
				bIsWorkingStartStateReady = true;
			}

			// Apply any instant effects that were queued up during this tick and didn't get handled in a substep
			if (ApplyInstantEffects(WorkingSubstepStartData, TimeStep, OutputState.SyncState))
			{
				if (CurrentModeName != OutputState.SyncState.MovementMode)
				{
					SetModeImmediately(OutputState.SyncState.MovementMode);
				}
			}
		}

		// We verify no effects are left that should have been applied
#if !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
		if (const FChaosMoverSimulationDefaultInputs* SimInputs = Simulation->GetLocalSimInput().FindDataByType<FChaosMoverSimulationDefaultInputs>())
		{
			for (const FChaosScheduledInstantMovementEffect& QueuedEffect : QueuedInstantEffects)
			{
				ensureMsgf(!QueuedEffect.ScheduledEffect.ShouldExecuteAtFrame(TimeStep.ServerFrame),
					TEXT("(%s) An Instant Movement Effect that should have been applied at ServerFrame %d was still in the queue after the simulation was ticked at ServerFrame %d. Effect: %s"),
					*ToString(SimInputs->OwningActor->GetNetMode()), QueuedEffect.ScheduledEffect.ExecutionServerFrame, TimeStep.ServerFrame, * QueuedEffect.ScheduledEffect.Effect->ToSimpleString());
			}
		}
#endif // !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
	}

	void FMoverStateMachine::OnSimulationRollback(const FMoverTimeStep& NewTimeStep, const FMoverSyncState& InvalidSyncState, const FMoverSyncState& NewSyncState)
	{
		float PreviousInternalSimTimeMs = InternalSimTimeMs;
		int32 PreviousInternalServerFrame = InternalServerFrame;
		InternalSimTimeMs = NewTimeStep.BaseSimTimeMs;
		InternalServerFrame = NewTimeStep.ServerFrame;

		ClearQueuedMode();

		if (CurrentModeName != NewSyncState.MovementMode)
		{
			QueueNextMode(NewSyncState.MovementMode);
		}

		QueuedLayeredMoves.Empty();
		QueuedMovementModifiers.Empty();
		InstantMovementEffectsIDHistory.Rollback(NewTimeStep.ServerFrame);

		for (int EffectIndex = 0; EffectIndex < QueuedInstantEffects.Num();)
		{
			const FChaosScheduledInstantMovementEffect& QueuedEffect = QueuedInstantEffects[EffectIndex];
			// We roll back all effects which were issued after PreviousInternalServerFrame, except those marked as not rolling back
			// (typically those coming from the game thread that is not being resimulated)
			if (QueuedEffect.IssuanceServerFrame >= PreviousInternalServerFrame && QueuedEffect.bShouldRollBack)
			{
#if !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
				if (const FChaosMoverSimulationDefaultInputs* SimInputs = Simulation->GetLocalSimInput().FindDataByType<FChaosMoverSimulationDefaultInputs>())
				{
					UE_LOG(LogChaosMover, Verbose, TEXT("(%s) Rolling back Instant Effect issued at frame %d scheduled for frame %d at frame %d: %s"),
						*ToString(SimInputs->OwningActor->GetNetMode()), QueuedEffect.IssuanceServerFrame, QueuedEffect.ScheduledEffect.ExecutionServerFrame, NewTimeStep.ServerFrame, QueuedEffect.ScheduledEffect.Effect.IsValid() ? *QueuedEffect.ScheduledEffect.Effect->ToSimpleString() : TEXT("INVALID INSTANT EFFECT"));
				}
#endif // !defined(BUILD_SHIPPING) || !BUILD_SHIPPING

				QueuedInstantEffects.RemoveAt(EffectIndex);
			}
			else
			{
				EffectIndex++;
			}
		}

		FMovementModifierParams_Async ModifierParams(Simulation.Get(), &NewSyncState, &NewTimeStep);

		// Check if we have a new modifier in the rolled back sync state
		for (auto ModifierFromRollbackIt = NewSyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierFromRollbackIt; ++ModifierFromRollbackIt)
		{
			bool bContainsModifier = false;
			for (auto ModifierFromCacheIt = InvalidSyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierFromCacheIt; ++ModifierFromCacheIt)
			{
				if (ModifierFromRollbackIt->Get()->Matches(ModifierFromCacheIt->Get()))
				{
					bContainsModifier = true;

					// Rolled back version of the modifier will be missing the handle; we fix that here
					ModifierFromRollbackIt->Get()->OverwriteHandleIfInvalid(ModifierFromCacheIt->Get()->GetHandle());
					break;
				}
			}

			// If modifier is not already present start the new one
			if (!bContainsModifier)
			{
				UE_LOG(LogChaosMover, Verbose, TEXT("Modifier(%s) was started after a rollback."), *ModifierFromRollbackIt->Get()->ToSimpleString());
				ModifierFromRollbackIt->Get()->OnStart_Async(ModifierParams);
			}
		}

		// Check if the previous state has an active modifier not in the rolled back state
		for (auto ModifierFromCacheIt = InvalidSyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierFromCacheIt; ++ModifierFromCacheIt)
		{
			bool bContainsModifier = false;
			for (auto ModifierFromRollbackIt = NewSyncState.MovementModifiers.GetActiveModifiersIterator(); ModifierFromRollbackIt; ++ModifierFromRollbackIt)
			{
				if (ModifierFromRollbackIt->Get()->Matches(ModifierFromCacheIt->Get()))
				{
					bContainsModifier = true;
					break;
				}
			}

			// If the modifier is not in the rolled back state end it
			if (!bContainsModifier)
			{
				UE_LOG(LogChaosMover, Log, TEXT("Modifier(%s) was ended after a rollback."), *ModifierFromCacheIt->Get()->ToSimpleString());
				ModifierFromCacheIt->Get()->OnEnd_Async(ModifierParams);
			}
		}
	}


	const TWeakObjectPtr<UBaseMovementMode> FMoverStateMachine::GetCurrentMode() const
	{
		if (CurrentModeName != NAME_None && Modes.Contains(CurrentModeName))
		{
			return Modes[CurrentModeName];
		}

		return nullptr;
	}

	const TWeakObjectPtr<UBaseMovementMode> FMoverStateMachine::FindMovementMode(FName ModeName) const
	{
		if (ModeName != NAME_None && Modes.Contains(ModeName))
		{
			return Modes[ModeName];
		}

		return nullptr;
	}

	TWeakObjectPtr<UBaseMovementMode> FMoverStateMachine::FindMovementMode_Mutable(FName ModeName)
	{
		if (ModeName != NAME_None && Modes.Contains(ModeName))
		{
			return Modes[ModeName];
		}

		return nullptr;
	}

	void FMoverStateMachine::QueueLayeredMove(TSharedPtr<FLayeredMoveBase> Move)
	{
		QueuedLayeredMoves.Add(Move);
	}

	void FMoverStateMachine::QueueInstantMovementEffect(const FChaosScheduledInstantMovementEffect& ScheduledEffect)
	{
		QueuedInstantEffects.Add(ScheduledEffect);
	}

	void FMoverStateMachine::ConstructDefaultModes()
	{
		RegisterMovementMode(UNullMovementMode::NullModeName, TObjectPtr<UBaseMovementMode>(NullMovementModeWeakPtr.Get()), /*bIsDefaultMode =*/ true);
		DefaultModeName = NAME_None;
		CurrentModeName = UNullMovementMode::NullModeName;

		QueuedModeTransitionWeakPtr = ImmediateMovementModeTransitionWeakPtr;

		ClearQueuedMode();
	}

	void FMoverStateMachine::AdvanceToNextMode()
	{
		TStrongObjectPtr<UImmediateMovementModeTransition> QueuedModeTransition = QueuedModeTransitionWeakPtr.Pin();
		if (QueuedModeTransition)
		{
			const FName NextModeName = QueuedModeTransition->GetNextModeName();

			if (NextModeName != NAME_None)
			{
				TWeakObjectPtr<UBaseMovementMode>* FoundNextMovementMode = Modes.Find(NextModeName);
				if (FoundNextMovementMode)
				{
					const bool bShouldNextModeReenter = QueuedModeTransition->ShouldReenter();
					if ((CurrentModeName != NextModeName) || bShouldNextModeReenter)
					{
						UE_LOG(LogChaosMover, Verbose, TEXT("AdvanceToNextMode: %s (%s) from %s to %s"),
							*OwnerActorName, *UEnum::GetValueAsString(OwnerActorLocalNetRole), *CurrentModeName.ToString(), *NextModeName.ToString());

						const FName PreviousModeName = CurrentModeName;
						CurrentModeName = NextModeName;

						if (PreviousModeName != NAME_None && Modes.Contains(PreviousModeName))
						{
							Modes[PreviousModeName]->Deactivate();
						}

						// Track last mode change in the blackboard
						if (URollbackBlackboard_InternalWrapper* RollbackBlackboard = Simulation->GetRollbackBlackboard_Mutable())
						{
							FMovementModeChangeRecord ModeChangeRecord;
							ModeChangeRecord.ModeName = CurrentModeName;
							ModeChangeRecord.PrevModeName = PreviousModeName;
							ModeChangeRecord.Frame = InternalServerFrame;
							ModeChangeRecord.SimTimeMs = InternalSimTimeMs;

							RollbackBlackboard->TrySet(CommonBlackboard::LastModeChangeRecord, ModeChangeRecord);
						}

						Modes[CurrentModeName]->Activate();

						// Notify the simulation of a mode change so it can react accordingly
						if (TStrongObjectPtr<UChaosMoverSimulation> SimStrongObjPtr = Simulation.Pin())
						{
							SimStrongObjPtr->AddEvent(MakeShared<FMovementModeChangedEventData>(InternalSimTimeMs, PreviousModeName, NextModeName));
						}
					}
				}
			}

			ClearQueuedMode();
		}
	}

	void FMoverStateMachine::FlushQueuedMovesToGroup(FLayeredMoveGroup& Group)
	{
		if (!QueuedLayeredMoves.IsEmpty())
		{
			for (TSharedPtr<FLayeredMoveBase>& QueuedMove : QueuedLayeredMoves)
			{
				Group.QueueLayeredMove(QueuedMove);
			}

			QueuedLayeredMoves.Empty();
		}
	}

	bool FMoverStateMachine::ApplyInstantEffects(const FMoverTickStartData& SubstepStartData, const FMoverTimeStep& TimeStep, FMoverSyncState& OutputState)
	{
		FApplyMovementEffectParams_Async EffectParams_Async;
		EffectParams_Async.StartState = &SubstepStartData;
		EffectParams_Async.TimeStep = &TimeStep;
		EffectParams_Async.Simulation = Simulation.Get();

		bool bInstantMovementEffectApplied = false;

		for (int EffectIndex = 0; EffectIndex < QueuedInstantEffects.Num(); )
		{
			const FScheduledInstantMovementEffect& ScheduledEffect = QueuedInstantEffects[EffectIndex].ScheduledEffect;
			if (ScheduledEffect.ShouldExecuteAtFrame(TimeStep.ServerFrame))
			{
				bInstantMovementEffectApplied |= ScheduledEffect.Effect->ApplyMovementEffect_Async(EffectParams_Async, OutputState);

#if !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
				if (const FChaosMoverSimulationDefaultInputs* SimInputs = Simulation->GetLocalSimInput().FindDataByType<FChaosMoverSimulationDefaultInputs>())
				{
					UE_LOG(LogChaosMover, Verbose, TEXT("(%s) Applying Instant Effect scheduled for frame %d at frame %d: %s"),
						*ToString(SimInputs->OwningActor->GetNetMode()), ScheduledEffect.ExecutionServerFrame, TimeStep.ServerFrame, ScheduledEffect.Effect.IsValid() ? *ScheduledEffect.Effect->ToSimpleString() : TEXT("INVALID INSTANT EFFECT"));
				}
#endif // !defined(BUILD_SHIPPING) || !BUILD_SHIPPING

				// We remove this effect since it has been applied
				QueuedInstantEffects.RemoveAt(EffectIndex);
			}
#if !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
			else
			{
				EffectIndex++;
				if (const FChaosMoverSimulationDefaultInputs* SimInputs = Simulation->GetLocalSimInput().FindDataByType<FChaosMoverSimulationDefaultInputs>())
				{
					UE_LOG(LogChaosMover, Verbose, TEXT("(%s) SKIPPING Instant Effect scheduled for frame %d at frame %d: %s"),
						*ToString(SimInputs->OwningActor->GetNetMode()), ScheduledEffect.ExecutionServerFrame, TimeStep.ServerFrame, ScheduledEffect.Effect.IsValid() ? *ScheduledEffect.Effect->ToSimpleString() : TEXT("INVALID INSTANT EFFECT"));
				}
			}
#endif // !defined(BUILD_SHIPPING) || !BUILD_SHIPPING
		}

		return bInstantMovementEffectApplied;
	}

	void FMoverStateMachine::SetOwnerActorName(const FString& InOwnerActorName)
	{
		OwnerActorName = InOwnerActorName;
	}
	
	void FMoverStateMachine::SetOwnerActorLocalNetRole(ENetRole InOwnerActorLocalNetRole)
	{
		OwnerActorLocalNetRole = InOwnerActorLocalNetRole;
	}

	void FMoverStateMachine::FIDHistory::AddID(int32 Frame, uint8 ID)
	{
		TSet<uint8>& IDs = IDsByFrame.FindOrAdd(Frame);
		IDs.Add(ID);
	}

	void FMoverStateMachine::FIDHistory::CullOldFrames(int32 CurrentFrame, int32 MaxAge)
	{
		const int32 OldestFrameToKeep = CurrentFrame - MaxAge;

		for (TMap<int32, TSet<uint8>>::TIterator It = IDsByFrame.CreateIterator(); It; ++It)
		{
			if (It.Key() < OldestFrameToKeep)
			{
				It.RemoveCurrent();
			}
		}
	}
	
	void FMoverStateMachine::FIDHistory::Rollback(int32 RollbackToFrameInclusive)
	{
		for (TMap<int32, TSet<uint8>>::TIterator It = IDsByFrame.CreateIterator(); It; ++It)
		{
			if (It.Key() >= RollbackToFrameInclusive)
			{
				It.RemoveCurrent();
			}
		}
	}

	bool FMoverStateMachine::FIDHistory::WasIDAlreadySeen(uint8 ID) const
	{
		for (TMap<int32, TSet<uint8>>::TConstIterator It = IDsByFrame.CreateConstIterator(); It; ++It)
		{
			if (It.Value().Contains(ID))
			{
				return true;
			}
		}
		return false;
	}
} // End of namespace UE::ChaosMover