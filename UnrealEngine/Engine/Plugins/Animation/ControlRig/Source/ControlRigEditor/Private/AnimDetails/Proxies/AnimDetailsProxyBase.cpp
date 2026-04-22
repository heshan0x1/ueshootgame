// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimDetailsProxyBase.h"

#include "AnimDetails/AnimDetailsMultiEditUtil.h"
#include "AnimDetails/AnimDetailsProxyManager.h"
#include "AnimDetailsProxyTransform.h"
#include "AnimDetailsProxyVector2D.h"
#include "ConstraintsManager.h"
#include "ControlRig.h"
#include "EditMode/ControlRigEditMode.h"
#include "IDetailKeyframeHandler.h"
#include "ISequencer.h"
#include "LevelEditorViewport.h"
#include "MovieSceneCommonHelpers.h"
#include "MVVM/SectionModelStorageExtension.h"
#include "MVVM/ViewModels/ChannelModel.h"
#include "MVVM/ViewModels/SectionModel.h"
#include "MVVM/ViewModels/SequencerEditorViewModel.h"
#include "MVVM/ViewModels/TrackModel.h"
#include "PropertyHandle.h"
#include "Rigs/RigHierarchyDefines.h"
#include "ScopedTransaction.h"
#include "Sequencer/ControlRigSequencerHelpers.h"
#include "Sequencer/AnimLayers/AnimLayers.h"
#include "Sequencer/MovieSceneControlRigParameterSection.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "SequencerAddKeyOperation.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Units/RigUnitContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(AnimDetailsProxyBase)

namespace UE::ControlRigEditor
{
	namespace KeyUtils
	{
		/** Adds a key to the specified track */
		static void KeyTrack(const TSharedPtr<ISequencer>& Sequencer, UAnimDetailsProxyBase* Proxy, UMovieScenePropertyTrack* Track, EControlRigContextChannelToKey ChannelToKey)
		{
			using namespace UE::Sequencer;

			if (!Sequencer.IsValid() || !Proxy || !Track)
			{
				return;
			}

			const FFrameNumber Time = Sequencer->GetLocalTime().Time.FloorToFrame();
			float Weight = 0.0;

			UMovieSceneSection* Section = Track->FindOrExtendSection(Time, Weight);

			FScopedTransaction PropertyChangedTransaction(NSLOCTEXT("AnimDetailsProxyBase", "KeyProperty", "Key Property"), !GIsTransacting);
			if (!Section || !Section->TryModify())
			{
				PropertyChangedTransaction.Cancel();
				return;
			}

			const TSharedPtr<FSequencerEditorViewModel> EditorViewModel = Sequencer->GetViewModel();
			const FViewModelPtr RootModel = EditorViewModel.IsValid() ? EditorViewModel->GetRootModel() : nullptr;
			const FSectionModelStorageExtension* SectionModelStorage = RootModel.IsValid() ? RootModel->CastDynamic<FSectionModelStorageExtension>() : nullptr;
			const TSharedPtr<FSectionModel> SectionHandle = SectionModelStorage ? SectionModelStorage->FindModelForSection(Section) : nullptr;
			const TSharedPtr<FViewModel> ViewModel = SectionHandle.IsValid() ? SectionHandle->GetParentTrackModel().AsModel() : nullptr;
			if (!EditorViewModel.IsValid() ||
				!RootModel.IsValid() ||
				!SectionModelStorage || 
				!SectionHandle.IsValid() || 
				!ViewModel.IsValid())
			{
				return;
			}

			TArray<TSharedRef<IKeyArea>> KeyAreas;
			const TParentFirstChildIterator<FChannelGroupModel> KeyAreaNodes = ViewModel->GetDescendantsOfType<FChannelGroupModel>();
			
			for (const TViewModelPtr<FChannelGroupModel>& KeyAreaNode : KeyAreaNodes)
			{
				for (const TWeakViewModelPtr<FChannelModel>& Channel : KeyAreaNode->GetChannels())
				{
					if (const TSharedPtr<FChannelModel> ChannelModel = Channel.Pin())
					{
						const EControlRigContextChannelToKey ThisChannelToKey = Proxy->GetChannelToKeyFromChannelName(ChannelModel->GetChannelName().ToString());
						if ((int32)ChannelToKey & (int32)ThisChannelToKey)
						{
							KeyAreas.Add(ChannelModel->GetKeyArea().ToSharedRef());
						}
					}
				}
			}

			const TSharedPtr<FTrackModel> TrackModel = SectionHandle->FindAncestorOfType<FTrackModel>();
			FAddKeyOperation::FromKeyAreas(TrackModel->GetTrackEditor().Get(), KeyAreas).Commit(Time, *Sequencer);
		}

		static EPropertyKeyedStatus GetChannelKeyStatus(
			FMovieSceneChannel* InChannel, 
			EPropertyKeyedStatus InSectionKeyedStatus, 
			const TRange<FFrameNumber>& InRange, 
			int32& OutEmptyChannelCount)
		{
			if (!InChannel)
			{
				return InSectionKeyedStatus;
			}

			if (InChannel->GetNumKeys() == 0)
			{
				++OutEmptyChannelCount;
				return InSectionKeyedStatus;
			}

			InSectionKeyedStatus = FMath::Max(InSectionKeyedStatus, EPropertyKeyedStatus::KeyedInOtherFrame);

			TArray<FFrameNumber> KeyTimes;
			InChannel->GetKeys(InRange, &KeyTimes, nullptr);
			if (KeyTimes.IsEmpty())
			{
				++OutEmptyChannelCount;
			}
			else
			{
				InSectionKeyedStatus = FMath::Max(InSectionKeyedStatus, EPropertyKeyedStatus::PartiallyKeyed);
			}

			return InSectionKeyedStatus;
		}

		static EPropertyKeyedStatus GetKeyedStatusInSection(
			const UControlRig* ControlRig, 
			const FName& ControlName, 
			const UMovieSceneControlRigParameterSection* Section, 
			const TRange<FFrameNumber>& Range, 
			const EControlRigContextChannelToKey ChannelToKey)
		{
			int32 EmptyChannelCount = 0;
			EPropertyKeyedStatus SectionKeyedStatus = EPropertyKeyedStatus::NotKeyed;
			
			const FRigControlElement* ControlElement = ControlRig ? ControlRig->FindControl(ControlName) : nullptr;
			if (!ControlElement)
			{
				return SectionKeyedStatus;
			}
			
			switch (ControlElement->Settings.ControlType)
			{
			case ERigControlType::Bool:
			{
				const TArrayView<FMovieSceneBoolChannel*> BoolChannels = FControlRigSequencerHelpers::GetBoolChannels(ControlRig, ControlElement->GetKey().Name, Section);
				for (FMovieSceneChannel* Channel : BoolChannels)
				{
					SectionKeyedStatus = GetChannelKeyStatus(Channel, SectionKeyedStatus, Range, EmptyChannelCount);
				}

				break;
			}
			case ERigControlType::Integer:
			{
				const TArrayView<FMovieSceneIntegerChannel*> IntegarChannels = FControlRigSequencerHelpers::GetIntegerChannels(ControlRig, ControlElement->GetKey().Name, Section);
				for (FMovieSceneChannel* Channel : IntegarChannels)
				{
					SectionKeyedStatus = GetChannelKeyStatus(Channel, SectionKeyedStatus, Range, EmptyChannelCount);
				}

				const TArrayView<FMovieSceneByteChannel*>  EnumChannels = FControlRigSequencerHelpers::GetByteChannels(ControlRig, ControlElement->GetKey().Name, Section);
				for (FMovieSceneChannel* Channel : EnumChannels)
				{
					SectionKeyedStatus = GetChannelKeyStatus(Channel, SectionKeyedStatus, Range, EmptyChannelCount);
				}

				break;
			}
			case ERigControlType::Position:
			case ERigControlType::Transform:
			case ERigControlType::TransformNoScale:
			case ERigControlType::EulerTransform:
			case ERigControlType::Float:
			case ERigControlType::ScaleFloat:
			case ERigControlType::Vector2D:
			{
				const int32 IChannelToKey = (int32)ChannelToKey;
				const TArrayView<FMovieSceneFloatChannel*> FloatChannels = FControlRigSequencerHelpers::GetFloatChannels(ControlRig, ControlElement->GetKey().Name, Section);

				int32 Num = 0;
				if (FloatChannels.Num() > Num)
				{
					if (IChannelToKey & int32(EControlRigContextChannelToKey::TranslationX))
					{
						SectionKeyedStatus = GetChannelKeyStatus(FloatChannels[Num], SectionKeyedStatus, Range, EmptyChannelCount);
					}
				}
				else
				{
					break;
				}
				++Num;

				if (FloatChannels.Num() > Num)
				{
					if (IChannelToKey & int32(EControlRigContextChannelToKey::TranslationY))
					{
						SectionKeyedStatus = GetChannelKeyStatus(FloatChannels[Num], SectionKeyedStatus, Range, EmptyChannelCount);
					}
				}
				else
				{
					break;
				}
				++Num;

				if (FloatChannels.Num() > Num)
				{
					if (IChannelToKey & int32(EControlRigContextChannelToKey::TranslationZ))
					{
						SectionKeyedStatus = GetChannelKeyStatus(FloatChannels[Num], SectionKeyedStatus, Range, EmptyChannelCount);
					}
				}
				else
				{
					break;
				}
				++Num;

				if (FloatChannels.Num() > Num)
				{
					if (IChannelToKey & int32(EControlRigContextChannelToKey::RotationX))
					{
						SectionKeyedStatus = GetChannelKeyStatus(FloatChannels[Num], SectionKeyedStatus, Range, EmptyChannelCount);
					}
				}
				else
				{
					break;
				}
				++Num;

				if (FloatChannels.Num() > Num)
				{
					if (IChannelToKey & int32(EControlRigContextChannelToKey::RotationY))
					{
						SectionKeyedStatus = GetChannelKeyStatus(FloatChannels[Num], SectionKeyedStatus, Range, EmptyChannelCount);
					}
				}
				else
				{
					break;
				}
				++Num;

				if (FloatChannels.Num() > Num)
				{
					if (IChannelToKey & int32(EControlRigContextChannelToKey::RotationZ))
					{
						SectionKeyedStatus = GetChannelKeyStatus(FloatChannels[Num], SectionKeyedStatus, Range, EmptyChannelCount);
					}
				}
				else
				{
					break;
				}
				++Num;

				if (FloatChannels.Num() > Num)
				{
					if (IChannelToKey & int32(EControlRigContextChannelToKey::ScaleX))
					{
						SectionKeyedStatus = GetChannelKeyStatus(FloatChannels[Num], SectionKeyedStatus, Range, EmptyChannelCount);
					}
				}
				else
				{
					break;
				}
				++Num;

				if (FloatChannels.Num() > Num)
				{
					if (IChannelToKey & int32(EControlRigContextChannelToKey::ScaleY))
					{
						SectionKeyedStatus = GetChannelKeyStatus(FloatChannels[Num], SectionKeyedStatus, Range, EmptyChannelCount);
					}
				}
				else
				{
					break;
				}
				++Num;

				if (FloatChannels.Num() > Num)
				{
					if (IChannelToKey & int32(EControlRigContextChannelToKey::ScaleZ))
					{
						SectionKeyedStatus = GetChannelKeyStatus(FloatChannels[Num], SectionKeyedStatus, Range, EmptyChannelCount);
					}
				}
				break;
			}
			case ERigControlType::Scale:
			{

				int32 IChannelToKey = (int32)ChannelToKey;
				const TArrayView<FMovieSceneFloatChannel*> FloatChannels = FControlRigSequencerHelpers::GetFloatChannels(ControlRig, ControlElement->GetKey().Name, Section);

				int32 Num = 0;
				if (FloatChannels.Num() > Num)
				{
					if (IChannelToKey & int32(EControlRigContextChannelToKey::ScaleX))
					{
						SectionKeyedStatus = GetChannelKeyStatus(FloatChannels[Num], SectionKeyedStatus, Range, EmptyChannelCount);
					}
				}
				else
				{
					break;
				}
				++Num;

				if (FloatChannels.Num() > Num)
				{
					if (IChannelToKey & int32(EControlRigContextChannelToKey::ScaleY))
					{
						SectionKeyedStatus = GetChannelKeyStatus(FloatChannels[Num], SectionKeyedStatus, Range, EmptyChannelCount);
					}
				}
				else
				{
					break;
				}
				++Num;

				if (FloatChannels.Num() > Num)
				{
					if (IChannelToKey & int32(EControlRigContextChannelToKey::ScaleZ))
					{
						SectionKeyedStatus = GetChannelKeyStatus(FloatChannels[Num], SectionKeyedStatus, Range, EmptyChannelCount);
					}
				}
				break;
			}
			case ERigControlType::Rotator:
			{
				int32 IChannelToKey = (int32)ChannelToKey;
				TArrayView<FMovieSceneFloatChannel*> FloatChannels = FControlRigSequencerHelpers::GetFloatChannels(ControlRig, ControlElement->GetKey().Name, Section);
				int32 Num = 0;
				if (FloatChannels.Num() > Num)
				{
					if (IChannelToKey & int32(EControlRigContextChannelToKey::RotationX))
					{
						SectionKeyedStatus = GetChannelKeyStatus(FloatChannels[Num], SectionKeyedStatus, Range, EmptyChannelCount);
					}
				}
				else
				{
					break;
				}
				++Num;

				if (FloatChannels.Num() > Num)
				{
					if (IChannelToKey & int32(EControlRigContextChannelToKey::RotationY))
					{
						SectionKeyedStatus = GetChannelKeyStatus(FloatChannels[Num], SectionKeyedStatus, Range, EmptyChannelCount);
					}
				}
				else
				{
					break;
				}
				++Num;

				if (FloatChannels.Num() > Num)
				{
					if (IChannelToKey & int32(EControlRigContextChannelToKey::RotationZ))
					{
						SectionKeyedStatus = GetChannelKeyStatus(FloatChannels[Num], SectionKeyedStatus, Range, EmptyChannelCount);
					}
				}
				else
				{
					break;
				}
				break;
			}
			}
			if (EmptyChannelCount == 0 && SectionKeyedStatus == EPropertyKeyedStatus::PartiallyKeyed)
			{
				SectionKeyedStatus = EPropertyKeyedStatus::KeyedInFrame;
			}

			return SectionKeyedStatus;
		}

		static EPropertyKeyedStatus GetKeyedStatusInTrack(
			ISequencer& Sequencer,
			const UControlRig* ControlRig, 
			const FName& ControlName, 
			const UMovieSceneControlRigParameterTrack* Track, 
			const TRange<FFrameNumber>& Range,
			const EControlRigContextChannelToKey ChannelToKey)
		{
			EPropertyKeyedStatus SectionKeyedStatus = EPropertyKeyedStatus::NotKeyed;

			const FRigControlElement* ControlElement = ControlRig ? ControlRig->FindControl(ControlName) : nullptr;
			if (!ControlElement)
			{
				return SectionKeyedStatus;
			}

			// Calling GetAnimLayers without testing HasAnimLayers will create Anim Layers in current version, avoid this
			TArray<UMovieSceneSection*> SectionsToKey;
			UAnimLayers* AnimLayers = UAnimLayers::HasAnimLayers(&Sequencer) ?
				UAnimLayers::GetAnimLayers(&Sequencer) :
				nullptr;
			if (AnimLayers)
			{
				SectionsToKey = AnimLayers->GetSelectedLayerSections();
			}

			// If there's no layer selected, evaluate for the base layer only
			if (SectionsToKey.IsEmpty())
			{
				UMovieSceneSection* BaseSection = Track->GetSectionToKey(ControlElement->GetFName()) ?
					Track->GetSectionToKey(ControlElement->GetFName()) :
					Track->GetAllSections()[0];

				SectionsToKey.AddUnique(BaseSection);
			}

			for (const UMovieSceneSection* Section : SectionsToKey)
			{
				const UMovieSceneControlRigParameterSection* ParameterSection = Cast<UMovieSceneControlRigParameterSection>(Section);
				if (!Section)
				{
					continue;
				}

				const EPropertyKeyedStatus NewSectionKeyedStatus = GetKeyedStatusInSection(ControlRig, ControlName, ParameterSection, Range, ChannelToKey);
				SectionKeyedStatus = FMath::Max(SectionKeyedStatus, NewSectionKeyedStatus);

				// Maximum Status Reached no need to iterate further
				if (SectionKeyedStatus == EPropertyKeyedStatus::KeyedInFrame)
				{
					return SectionKeyedStatus;
				}
			}

			return SectionKeyedStatus;
		}

		static EPropertyKeyedStatus GetKeyedStatusInSection(
			const UMovieSceneSection* Section, 
			const TRange<FFrameNumber>& Range, 
			const EControlRigContextChannelToKey ChannelToKey, 
			const int32 MaxNumIndices)
		{
			EPropertyKeyedStatus SectionKeyedStatus = EPropertyKeyedStatus::NotKeyed;

			FMovieSceneChannelProxy& ChannelProxy = Section->GetChannelProxy();

			TArray<int32, TFixedAllocator<3>> ChannelIndices;
			switch (ChannelToKey)
			{
			case EControlRigContextChannelToKey::Translation:
				ChannelIndices = { 0, 1, 2 };
				break;
			case EControlRigContextChannelToKey::TranslationX:
				ChannelIndices = { 0 };
				break;
			case EControlRigContextChannelToKey::TranslationY:
				ChannelIndices = { 1 };
				break;
			case EControlRigContextChannelToKey::TranslationZ:
				ChannelIndices = { 2 };
				break;
			case EControlRigContextChannelToKey::Rotation:
				ChannelIndices = { 3, 4, 5 };
				break;
			case EControlRigContextChannelToKey::RotationX:
				ChannelIndices = { 3 };
				break;
			case EControlRigContextChannelToKey::RotationY:
				ChannelIndices = { 4 };
				break;
			case EControlRigContextChannelToKey::RotationZ:
				ChannelIndices = { 5 };
				break;
			case EControlRigContextChannelToKey::Scale:
				ChannelIndices = { 6, 7, 8 };
				break;
			case EControlRigContextChannelToKey::ScaleX:
				ChannelIndices = { 6 };
				break;
			case EControlRigContextChannelToKey::ScaleY:
				ChannelIndices = { 7 };
				break;
			case EControlRigContextChannelToKey::ScaleZ:
				ChannelIndices = { 8 };
				break;
			}

			TSet<EPropertyKeyedStatus> PerChannelStatuses;
			for (const FMovieSceneChannelEntry& ChannelEntry : ChannelProxy.GetAllEntries())
			{
				if (ChannelEntry.GetChannelTypeName() != FMovieSceneDoubleChannel::StaticStruct()->GetFName() &&
					ChannelEntry.GetChannelTypeName() != FMovieSceneFloatChannel::StaticStruct()->GetFName() &&
					ChannelEntry.GetChannelTypeName() != FMovieSceneBoolChannel::StaticStruct()->GetFName() &&
					ChannelEntry.GetChannelTypeName() != FMovieSceneIntegerChannel::StaticStruct()->GetFName() &&
					ChannelEntry.GetChannelTypeName() != FMovieSceneByteChannel::StaticStruct()->GetFName())
				{
					continue;
				}

				const TConstArrayView<FMovieSceneChannel*> Channels = ChannelEntry.GetChannels();

				int32 ChannelIndex = 0;
				for (FMovieSceneChannel* Channel : Channels)
				{
					if (ChannelIndex >= MaxNumIndices)
					{
						break;
					}
					else if (ChannelIndices.Contains(ChannelIndex++) == false)
					{
						continue;
					}

					const int32 NumKeys = Channel->GetNumKeys();
					if (NumKeys == 0)
					{
						PerChannelStatuses.Add(EPropertyKeyedStatus::NotKeyed);
						continue;
					}

					TArray<FFrameNumber> KeyTimesInRange;
					Channel->GetKeys(Range, &KeyTimesInRange, nullptr);

					if (KeyTimesInRange.IsEmpty() && NumKeys > 0)
					{
						PerChannelStatuses.Add(EPropertyKeyedStatus::KeyedInOtherFrame);

						SectionKeyedStatus = FMath::Max(SectionKeyedStatus, EPropertyKeyedStatus::KeyedInOtherFrame);
					}
					else
					{
						PerChannelStatuses.Add(EPropertyKeyedStatus::KeyedInFrame);

						SectionKeyedStatus = EPropertyKeyedStatus::KeyedInFrame;
					}
				}

				break; // just do it for one type
			}

			// For structs, show partially keyed when some but not all channels are keyed this frame
			const bool bStructWithPerChannelStatuses = PerChannelStatuses.Num() > 1;
			if (bStructWithPerChannelStatuses)
			{
				const bool bKeyedInFrame = PerChannelStatuses.Contains(EPropertyKeyedStatus::KeyedInFrame);
				const bool bKeyedInOtherFrame = PerChannelStatuses.Contains(EPropertyKeyedStatus::KeyedInOtherFrame);
				const bool bNotKeyed = PerChannelStatuses.Contains(EPropertyKeyedStatus::NotKeyed);

				if (bKeyedInFrame && (bKeyedInOtherFrame || bNotKeyed))
				{
					SectionKeyedStatus = EPropertyKeyedStatus::PartiallyKeyed;
				}
			}

			return SectionKeyedStatus;
		}

		static EPropertyKeyedStatus GetKeyedStatusInTrack(
			ISequencer& Sequencer,
			const UMovieScenePropertyTrack* Track, 
			const TRange<FFrameNumber>& Range, 
			const EControlRigContextChannelToKey ChannelToKey, 
			const int32 MaxNumIndices)
		{
			EPropertyKeyedStatus SectionKeyedStatus = EPropertyKeyedStatus::NotKeyed;
			for (UMovieSceneSection* BaseSection : Track->GetAllSections())
			{
				if (!BaseSection)
				{
					continue;
				}
				const EPropertyKeyedStatus NewSectionKeyedStatus = GetKeyedStatusInSection(BaseSection, Range, ChannelToKey, MaxNumIndices);
				SectionKeyedStatus = FMath::Max(SectionKeyedStatus, NewSectionKeyedStatus);

				// Maximum Status Reached no need to iterate further
				if (SectionKeyedStatus == EPropertyKeyedStatus::KeyedInFrame)
				{
					return SectionKeyedStatus;
				}
			}

			return SectionKeyedStatus;
		}
	}
}

void UAnimDetailsProxyBase::SetControlFromControlRig(UControlRig* InControlRig, const FName& InName)
{
	SequencerItem.Reset();

	URigHierarchy* Hierarchy = InControlRig ? InControlRig->GetHierarchy() : nullptr;
	if (Hierarchy)
	{
		WeakControlRig = InControlRig;
		CachedRigElement = FCachedRigElement(FRigElementKey(InName, ERigElementType::Control), Hierarchy);

		// Test if this yields a valid proxy
		const FRigControlElement* ControlElement = GetControlElement();
		const bool bValidControlElement = ControlElement && GetSupportedControlTypes().Contains(ControlElement->Settings.ControlType);
		
		if (!ensureMsgf(bValidControlElement, TEXT("Created invalid anim details proxy, control element is invalid, or control type does not match proxy type")))
		{
			WeakControlRig.Reset();
			CachedRigElement.Reset();
		}
	}
}

void UAnimDetailsProxyBase::SetControlFromSequencerBinding(UObject* InObject, const TWeakObjectPtr<UMovieSceneTrack>& InTrack, const TSharedPtr<FTrackInstancePropertyBindings>& InBinding)
{
	WeakControlRig.Reset();
	CachedRigElement.Reset();

	if (InObject &&
		InTrack.IsValid() &&
		InBinding.IsValid())
	{
		SequencerItem = FAnimDetailsSequencerProxyItem(*InObject, *InTrack.Get(), InBinding.ToSharedRef());
	}
	else
	{
		SequencerItem.Reset();
	}
}

UControlRig* UAnimDetailsProxyBase::GetControlRig() const
{
	return WeakControlRig.Get();
}

FRigControlElement* UAnimDetailsProxyBase::GetControlElement() const
{
	const URigHierarchy* Hierarchy = WeakControlRig.IsValid() ? WeakControlRig->GetHierarchy() : nullptr;
	const FRigControlElement* ControlElement = Hierarchy ? Cast<FRigControlElement>(CachedRigElement.GetElement(Hierarchy)) : nullptr;

	// @todo There is no specific reason to prevent from getting a non const ptr from FCachedRigElement. 
	// The related change turns out relatively large, so delay it for later and const cast here.
	return const_cast<FRigControlElement*>(ControlElement);
}

const FRigElementKey& UAnimDetailsProxyBase::GetControlElementKey() const
{
	return CachedRigElement.GetKey();
}

const FName& UAnimDetailsProxyBase::GetControlName() const
{
	return GetControlElementKey().Name;
}

void UAnimDetailsProxyBase::PropagonateValues()
{
	FRigControlModifiedContext Context;
	Context.SetKey = EControlRigSetKey::DoNotCare;

	FRigControlModifiedContext NotifyDrivenContext;

	UWorld* World = GCurrentLevelEditingViewportClient ? GCurrentLevelEditingViewportClient->GetWorld() : nullptr;
	const FConstraintsManagerController& Controller = FConstraintsManagerController::Get(World);
	Controller.EvaluateAllConstraints();

	UControlRig* ControlRig = GetControlRig();
	FRigControlElement* ControlElement = GetControlElement();
	if (ControlRig && ControlElement)
	{
		SetControlRigElementValueFromCurrent(Context);
		FControlRigEditMode::NotifyDrivenControls(ControlRig, ControlElement->GetKey(), NotifyDrivenContext);
			
		ControlRig->Evaluate_AnyThread();
	}
	else
	{
		SetSequencerBindingValueFromCurrent(Context);
	}
}

FText UAnimDetailsProxyBase::GetDisplayNameText(const EElementNameDisplayMode ElementNameDisplayMode) const
{
	if(!DisplayName.IsEmpty())
	{
		return FText::FromString(DisplayName);
	}
	
	const URigHierarchy* Hierarchy = WeakControlRig.IsValid() ? WeakControlRig->GetHierarchy() : nullptr;
	const FRigControlElement* ControlElement = WeakControlRig.IsValid() ? GetControlElement() : nullptr;
	if (Hierarchy && ControlElement)
	{
		return Hierarchy->GetDisplayNameForUI(ControlElement, ElementNameDisplayMode);
	}
	else if (const UObject* BoundObject = SequencerItem.GetBoundObject())
	{
		if (const AActor* Actor = Cast<AActor>(BoundObject))
		{
			return FText::FromString(Actor->GetActorLabel());
		}
		else if (const UActorComponent* Component = Cast<UActorComponent>(BoundObject))
		{
			return FText::FromString(*Component->GetName());
		}
	}

	return FText::GetEmpty();
}

void UAnimDetailsProxyBase::SetKey(const IPropertyHandle& KeyedPropertyHandle)
{
	using namespace UE::ControlRigEditor;

	const UAnimDetailsProxyManager* ProxyManager = GetTypedOuter<UAnimDetailsProxyManager>();
	const TSharedPtr<ISequencer> Sequencer = ProxyManager ? ProxyManager->GetSequencer() : nullptr;
	if (!Sequencer.IsValid() || !Sequencer->GetFocusedMovieSceneSequence())
	{
		return;
	}

	UControlRig* ControlRig = GetControlRig();
	FRigControlElement* ControlElement = GetControlElement();

	if (ControlRig && ControlElement)
	{
		if (ControlRig->GetHierarchy()->Contains(FRigElementKey(ControlElement->GetKey().Name, ERigElementType::Control)))
		{
			const FName PropertyName = KeyedPropertyHandle.GetProperty()->GetFName();

			FRigControlModifiedContext Context;
			Context.SetKey = EControlRigSetKey::Always;
			Context.KeyMask = (uint32)GetChannelToKeyFromPropertyName(PropertyName);

			SetControlRigElementValueFromCurrent(Context);

			ControlRig->Evaluate_AnyThread();

			FRigControlModifiedContext NotifyDrivenContext; // always key ever
			NotifyDrivenContext.SetKey = EControlRigSetKey::Always;

			FControlRigEditMode::NotifyDrivenControls(ControlRig, ControlElement->GetKey(), NotifyDrivenContext);
		}
	}
	else if (UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(SequencerItem.GetMovieSceneTrack()))
	{
		const FName PropertyName = KeyedPropertyHandle.GetProperty()->GetFName();
		const EControlRigContextChannelToKey ChannelToKey = GetChannelToKeyFromPropertyName(PropertyName);

		KeyUtils::KeyTrack(Sequencer, this, PropertyTrack, ChannelToKey);
	}
}

EPropertyKeyedStatus UAnimDetailsProxyBase::GetPropertyKeyedStatus(const IPropertyHandle& PropertyHandle) const
{
	using namespace UE::ControlRigEditor;

	EPropertyKeyedStatus KeyedStatus = EPropertyKeyedStatus::NotKeyed;

	const UAnimDetailsProxyManager* ProxyManager = GetTypedOuter<UAnimDetailsProxyManager>();
	const TSharedPtr<ISequencer> Sequencer = ProxyManager ? ProxyManager->GetSequencer() : nullptr;
	if (!Sequencer.IsValid() || !Sequencer->GetFocusedMovieSceneSequence())
	{
		return KeyedStatus;
	}

	const UControlRig* ControlRig = GetControlRig();
	const URigHierarchy* Hierarchy = ControlRig ? ControlRig->GetHierarchy() : nullptr;
	const FRigElementKey ControlElementKey = GetControlElementKey();

	const TRange<FFrameNumber> FrameRange = TRange<FFrameNumber>(Sequencer->GetLocalTime().Time.FrameNumber);
	const FName PropertyName = PropertyHandle.GetProperty()->GetFName();
	const EControlRigContextChannelToKey ChannelToKey = GetChannelToKeyFromPropertyName(PropertyName);

	UMovieSceneControlRigParameterTrack* ControlRigParameterTrack = ControlRig ? FControlRigSequencerHelpers::FindControlRigTrack(Sequencer->GetFocusedMovieSceneSequence(), ControlRig) : nullptr;
	if (ControlRig && ControlElementKey.IsValid() && ControlRigParameterTrack)
	{
		const EPropertyKeyedStatus NewKeyedStatus = KeyUtils::GetKeyedStatusInTrack(*Sequencer.Get(), ControlRig, ControlElementKey.Name, ControlRigParameterTrack, FrameRange, ChannelToKey);
		KeyedStatus = FMath::Max(KeyedStatus, NewKeyedStatus);
	}
	else if (UMovieScenePropertyTrack* MovieScenePropertyTrack = Cast<UMovieScenePropertyTrack>(SequencerItem.GetMovieSceneTrack()))
	{
		int32 MaxNumIndices = 1;
		if (IsA<UAnimDetailsProxyTransform>())
		{
			MaxNumIndices = 9;
		}
		else if (IsA<UAnimDetailsProxyVector2D>())
		{
			MaxNumIndices = 2;
		}

		const EPropertyKeyedStatus NewKeyedStatus = KeyUtils::GetKeyedStatusInTrack(*Sequencer.Get(), MovieScenePropertyTrack, FrameRange, ChannelToKey, MaxNumIndices);
		KeyedStatus = FMath::Max(KeyedStatus, NewKeyedStatus);
	}

	return KeyedStatus;
}

FName UAnimDetailsProxyBase::GetDetailRowID() const
{
	if (bIsIndividual)
	{
		const FRigControlElement* ControlElement = GetControlElement();
		if (ControlElement)
		{
			return ControlElement->GetKey().Name;
		}
		else if (const FProperty* Property = SequencerItem.GetProperty())
		{
			return *Property->GetPathName();
		}
		else
		{
			return NAME_None;
		}
	}
	else
	{
		return GetClass()->GetFName();
	}
}

void UAnimDetailsProxyBase::GetLocalizedPropertyName(const FName& InPropertyName, FText& OutPropertyDisplayName, TOptional<FText>& OutOptionalStructDisplayName) const
{
	// Proxies with one member in their struct only show inner properties
	OutOptionalStructDisplayName.Reset();

	const URigHierarchy* Hierarchy = WeakControlRig.IsValid() ? WeakControlRig->GetHierarchy() : nullptr;
	const FRigControlElement* ControlElement = WeakControlRig.IsValid() ? GetControlElement() : nullptr;
	if (Hierarchy && ControlElement)
	{
		OutPropertyDisplayName = Hierarchy->GetDisplayNameForUI(ControlElement);
	}
	else if (const TSharedPtr<FTrackInstancePropertyBindings>& Binding = SequencerItem.GetBinding())
	{
		OutPropertyDisplayName = FText::FromName(SequencerItem.GetBinding()->GetPropertyName());
	}
}

void UAnimDetailsProxyBase::UpdateOverrideableProperties()
{
	if(const FRigControlElement* ControlElement = GetControlElement())
	{
		DisplayName = ControlElement->GetDisplayName().ToString();
		Shape.ConfigureFrom(ControlElement, ControlElement->Settings);
	}
}

FName UAnimDetailsProxyBase::GetPropertyID(const FName& PropertyName) const
{
	return *(GetDetailRowID().ToString() + TEXT(".") + PropertyName.ToString());
}

void UAnimDetailsProxyBase::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::ToggleEditable) // hack so we can clear the reset cache for this property and not actually send this to our controls
	{
		return;
	}
	
	// Reset to default is handled in the anim details value customizations
	if (PropertyChangedEvent.ChangeType & EPropertyChangeType::ResetToDefault)
	{
		return;
	}
	
	if (const FProperty* Property = PropertyChangedEvent.Property)
	{
		const FProperty* MemberProperty = PropertyChangedEvent.PropertyChain.GetActiveMemberNode() ? 
			PropertyChangedEvent.PropertyChain.GetActiveMemberNode()->GetValue() : 
			nullptr;

		if (PropertyIsOnProxy(Property, MemberProperty))
		{

			// Let the control rig know it's interacted with
			EControlRigContextChannelToKey ChannelToKeyContext = GetChannelToKeyFromPropertyName(Property->GetFName());
			AddControlRigInteractionScope(ChannelToKeyContext, PropertyChangedEvent.ChangeType);

			// Propagonate the values to the control rig
			PropagonateValues();

			if (PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
			{
				InteractionScopes.Reset();
			}

			// Adopt values from the rig or sequencer binding. 
			// They may be different than what was propagonated, e.g. due to constraints.
			AdoptValues(ERigControlValueType::Current);
		}
	}
}

bool UAnimDetailsProxyBase::Modify(bool bAlwaysMarkDirty)
{
	// IPropertyHandle::SetPerObjectValues which the multi edit util uses always modifies the object, 
	// hence we avoid modificiation by testing for interactive changes here.
	if (!UE::ControlRigEditor::FAnimDetailsMultiEditUtil::Get().IsInteractive())
	{
		return Super::Modify(bAlwaysMarkDirty);
	}

	return true;
}

void UAnimDetailsProxyBase::AddControlRigInteractionScope(EControlRigContextChannelToKey ChannelsToKey, EPropertyChangeType::Type ChangeType)
{
	if (ChangeType == EPropertyChangeType::Interactive || ChangeType == EPropertyChangeType::ValueSet)
	{
		EControlRigInteractionType InteractionType = EControlRigInteractionType::None;
		if (EnumHasAnyFlags(ChannelsToKey, EControlRigContextChannelToKey::TranslationX) ||
			EnumHasAnyFlags(ChannelsToKey, EControlRigContextChannelToKey::TranslationY) ||
			EnumHasAnyFlags(ChannelsToKey, EControlRigContextChannelToKey::TranslationZ))
		{
			EnumAddFlags(InteractionType, EControlRigInteractionType::Translate);
		}
		if (EnumHasAnyFlags(ChannelsToKey, EControlRigContextChannelToKey::RotationX) ||
			EnumHasAnyFlags(ChannelsToKey, EControlRigContextChannelToKey::RotationY) ||
			EnumHasAnyFlags(ChannelsToKey, EControlRigContextChannelToKey::RotationZ))
		{
			EnumAddFlags(InteractionType, EControlRigInteractionType::Rotate);
		}
		if (EnumHasAnyFlags(ChannelsToKey, EControlRigContextChannelToKey::ScaleX) ||
			EnumHasAnyFlags(ChannelsToKey, EControlRigContextChannelToKey::ScaleY) ||
			EnumHasAnyFlags(ChannelsToKey, EControlRigContextChannelToKey::ScaleZ))
		{
			EnumAddFlags(InteractionType, EControlRigInteractionType::Scale);
		}

		UControlRig* ControlRig = GetControlRig();
		FRigControlElement* ControlElement = GetControlElement();

		if (ControlRig && ControlElement && !InteractionScopes.Contains(ControlElement))
		{
			const TSharedRef<FControlRigInteractionScope> InteractionScope = MakeShared<FControlRigInteractionScope>(ControlRig, ControlElement->GetKey(), InteractionType);
			InteractionScopes.Add(ControlElement, InteractionScope);
		}
	}
}
