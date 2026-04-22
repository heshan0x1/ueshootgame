// Copyright Epic Games, Inc. All Rights Reserved.
#include "PoseSearch/Chooser/PoseSearchChooserColumn.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Chooser.h"
#include "ChooserIndexArray.h"
#include "ChooserPropertyAccess.h"
#include "ChooserTrace.h"
#include "ChooserTypes.h"
#include "IChooserParameterBool.h"
#include "IChooserParameterEnum.h"
#include "IChooserParameterFloat.h"
#include "PoseSearch/PoseSearchContext.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "PoseSearch/PoseSearchSchema.h"
#if WITH_EDITOR
#include "StructUtils/PropertyBag.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(PoseSearchChooserColumn)

#define LOCTEXT_NAMESPACE "FPoseSearchColumn"

namespace UE::PoseSearch
{
	int32 GetDatabaseAssetIndex(int32 RowIndex)
	{
		if (RowIndex == ChooserColumn_SpecialIndex_Fallback)
		{
			return 0;
		}

		check(RowIndex >= 0);

		return RowIndex + 1;
	}

	int32 GetRowIndex(int32 DatabaseAssetIndex)
	{
		check(DatabaseAssetIndex >= 0);

		if (DatabaseAssetIndex == 0)
		{
			return ChooserColumn_SpecialIndex_Fallback;
		}
		
		return DatabaseAssetIndex - 1;
	}

	// searching for FChooserPlayerSettings in the Context.Params
	static const FChooserPlayerSettings* GetChooserPlayerSettings(const FChooserEvaluationContext& Context)
	{
		for (const FStructView& Param : Context.Params)
		{
			if (const FChooserPlayerSettings* ChooserPlayerSettings = Param.GetPtr<FChooserPlayerSettings>())
			{
				return ChooserPlayerSettings;
			}
		}
		return nullptr;
	}

	// Experimental, this feature might be removed without warning, not for production use
	struct FPoseSearchColumnScratchArea
	{
		TArray<UE::PoseSearch::FSearchResult> SearchResults;
		const UE::PoseSearch::IPoseHistory* PoseHistory = nullptr;
		const FChooserPlayerSettings* ChooserPlayerSettings = nullptr;

#if DO_CHECK
		const FPoseSearchColumn* DebugOwner = nullptr;
#endif // DO_CHECK
	};

	FArchive& operator<<(FArchive& Ar, FActiveColumnCost& ActiveColumnCost)
	{
		Ar << ActiveColumnCost.RowIndex;
		Ar << ActiveColumnCost.RowCost;
		return Ar;
	}
} // namespace UE::PoseSearch

bool FPoseHistoryContextProperty::GetValue(FChooserEvaluationContext& Context, FPoseHistoryReference& OutResult) const
{
	return Binding.GetStructValue(Context, OutResult);
}

FPoseSearchColumn::FPoseSearchColumn()
{
}

void FPoseSearchColumn::CheckConsistency() const
{
#if DO_CHECK && WITH_EDITORONLY_DATA

	using namespace UE::PoseSearch;

	check(InternalDatabase);
	UChooserTable* OuterChooser = Cast<UChooserTable>(InternalDatabase->GetOuter());
	check(OuterChooser);

	const int32 NumAnimationAssets = InternalDatabase->GetNumAnimationAssets();
	const int32 NumRows = GetRowIndex(NumAnimationAssets);
	if (NumRows != OuterChooser->ResultsStructs.Num())
	{
		UE_LOG(LogPoseSearch, Error, TEXT("FPoseSearchColumn::CheckConsistency for Chooser Table '%s' failed because the number of rows (%d) differs from the internal database number of assets %d. Click 'AutoPopulate All' button in the Chooser Table Editor to try resynchronize the data"), *OuterChooser->GetName(), OuterChooser->ResultsStructs.Num(), NumRows);
	}

	for (int32 AnimationAssetIndex = 0; AnimationAssetIndex < NumAnimationAssets; ++AnimationAssetIndex)
	{
		const FPoseSearchDatabaseAnimationAsset* DatabaseAnimationAsset = InternalDatabase->GetDatabaseAnimationAsset(AnimationAssetIndex);
		check(DatabaseAnimationAsset);
		
		const UObject* AnimationAsset = DatabaseAnimationAsset->GetAnimationAsset();
		const int32 RowIndex = GetRowIndex(AnimationAssetIndex);
		const UObject* ReferencedObject = GetReferencedObject(RowIndex);
		if (AnimationAsset != ReferencedObject)
		{
			UE_LOG(LogPoseSearch, Error, TEXT("FPoseSearchColumn::CheckConsistency for Chooser Table '%s' failed for row %d: chooser table asset '%s' differs from internal database asset ' % s'. Click 'AutoPopulate All' button in the Chooser Table Editor to try resynchronize the data"), *OuterChooser->GetName(), RowIndex, *GetNameSafe(ReferencedObject), *GetNameSafe(AnimationAsset));
		}
	}

#endif // DO_CHECK && WITH_EDITORONLY_DATA 
}

#if WITH_EDITOR

UObject* FPoseSearchColumn::GetReferencedObject(int32 RowIndex) const
{
	using namespace UE::PoseSearch;

	UChooserTable* OuterChooser = Cast<UChooserTable>(InternalDatabase->GetOuter());
	check(OuterChooser);

	UObject* ReferencedObject = nullptr;

	if (RowIndex != ChooserColumn_SpecialIndex_Fallback)
	{
		check(RowIndex >= 0);
		const FInstancedStruct& Result = OuterChooser->ResultsStructs[RowIndex];
		if (Result.IsValid())
		{
			ReferencedObject = Result.Get<FObjectChooserBase>().GetReferencedObject();
		}
	}
	else
	{
		const FInstancedStruct& FallbackResult = OuterChooser->FallbackResult;
		if (FallbackResult.IsValid())
		{
			ReferencedObject = FallbackResult.Get<FObjectChooserBase>().GetReferencedObject();
		}
	}

	return ReferencedObject;
}

FName FPoseSearchColumn::RowValuesPropertyName()
{
	return "RowValues";
}

void FPoseSearchColumn::SetNumRows(int32 NumRows)
{
	using namespace UE::PoseSearch;

	check(InternalDatabase && NumRows >= 0);

	const int32 NumDatabaseEntries = GetDatabaseAssetIndex(NumRows);
	if (NumDatabaseEntries == InternalDatabase->GetNumAnimationAssets())
	{
		// nothing to do
	}
	else if (NumDatabaseEntries < InternalDatabase->GetNumAnimationAssets())
	{
		InternalDatabase->Modify();
		while (InternalDatabase->GetNumAnimationAssets() > 0 && InternalDatabase->GetNumAnimationAssets() > NumDatabaseEntries)
		{
			InternalDatabase->RemoveAnimationAssetAt(InternalDatabase->GetNumAnimationAssets() - 1);
		}
	}
	else
	{
		InternalDatabase->Modify();
		while (InternalDatabase->GetNumAnimationAssets() < NumDatabaseEntries)
		{
			FPoseSearchDatabaseAnimationAsset NewAsset;
			const int32 RowIndex = GetRowIndex(InternalDatabase->GetNumAnimationAssets());
			NewAsset.AnimAsset = GetReferencedObject(RowIndex);
			InternalDatabase->AddAnimationAsset(NewAsset);
		}
	}

	InternalDatabase->NotifySynchronizeWithExternalDependencies();

	// NumRows == 0 has a special meaning of resetting the column and usually make the column inconsistent with the chooser asset, so we don't CheckConsistency
	if (NumRows != 0)
	{
		CheckConsistency();
	}
}

void FPoseSearchColumn::InsertRows(int Index, int Count)
{
	using namespace UE::PoseSearch;

	check(InternalDatabase);
	if (Count > 0)
	{
		InternalDatabase->Modify();
		for (int i = 0; i < Count; i++)
		{
			FPoseSearchDatabaseAnimationAsset NewAsset;
			const int32 RowIndex = Index + i;
			const int32 DatabaseAssetIndex = GetDatabaseAssetIndex(RowIndex);
			NewAsset.AnimAsset = GetReferencedObject(RowIndex);
			InternalDatabase->InsertAnimationAssetAt(NewAsset, DatabaseAssetIndex);
		}
	}

	InternalDatabase->NotifySynchronizeWithExternalDependencies();
	CheckConsistency();
}

void FPoseSearchColumn::DeleteRows(const TArray<uint32>& RowIndices)
{
	using namespace UE::PoseSearch;

	check(InternalDatabase);
	InternalDatabase->Modify();
	
	for (uint32 RowIndex : RowIndices)
	{
		const int32 DatabaseAssetIndex = GetDatabaseAssetIndex(RowIndex);
		InternalDatabase->RemoveAnimationAssetAt(DatabaseAssetIndex);
	}

	// resynchronize the fallback row asset
	if (FPoseSearchDatabaseAnimationAsset* FallbackDatabaseAnimationAsset = InternalDatabase->GetMutableDatabaseAnimationAsset(GetDatabaseAssetIndex(ChooserColumn_SpecialIndex_Fallback)))
	{
		FallbackDatabaseAnimationAsset->AnimAsset = GetReferencedObject(ChooserColumn_SpecialIndex_Fallback);
	}

	InternalDatabase->NotifySynchronizeWithExternalDependencies();
	CheckConsistency();
}

void FPoseSearchColumn::MoveRow(int SourceRowIndex, int TargetRowIndex)
{
	using namespace UE::PoseSearch;

	check(InternalDatabase);

	const int32 SourceDatabaseAssetIndex = GetDatabaseAssetIndex(SourceRowIndex);
	int32 TargetDatabaseAssetIndex = GetDatabaseAssetIndex(TargetRowIndex);

	if (const FPoseSearchDatabaseAnimationAsset* SourceAsset = InternalDatabase->GetDatabaseAnimationAsset(SourceDatabaseAssetIndex))
	{
		InternalDatabase->Modify();

		FPoseSearchDatabaseAnimationAsset CopySourceAsset = *SourceAsset;
		InternalDatabase->RemoveAnimationAssetAt(SourceDatabaseAssetIndex);

		if (SourceDatabaseAssetIndex < TargetDatabaseAssetIndex)
		{
			TargetDatabaseAssetIndex--;
		}
		InternalDatabase->InsertAnimationAssetAt(CopySourceAsset, TargetDatabaseAssetIndex);

		InternalDatabase->NotifySynchronizeWithExternalDependencies();
	}

	CheckConsistency();
}

void FPoseSearchColumn::CopyRow(FChooserColumnBase& SourceColumn, int SourceRowIndex, int TargetRowIndex)
{
	using namespace UE::PoseSearch;

	check(InternalDatabase);

	const FPoseSearchColumn& SourcePoseSearchColumn = static_cast<FPoseSearchColumn&>(SourceColumn);

	const int32 SourceDatabaseAssetIndex = GetDatabaseAssetIndex(SourceRowIndex);
	const int32 TargetDatabaseAssetIndex = GetDatabaseAssetIndex(TargetRowIndex);
	if (const FPoseSearchDatabaseAnimationAsset* SourceAsset = SourcePoseSearchColumn.InternalDatabase->GetDatabaseAnimationAsset(SourceDatabaseAssetIndex))
	{
		if (FPoseSearchDatabaseAnimationAsset* TargetAsset = InternalDatabase->GetMutableDatabaseAnimationAsset(TargetDatabaseAssetIndex))
		{
			InternalDatabase->Modify();
			*TargetAsset = *SourceAsset;

			InternalDatabase->NotifySynchronizeWithExternalDependencies();
		}
	}

	CheckConsistency();
}

UScriptStruct* FPoseSearchColumn::GetInputBaseType() const
{
	return FChooserParameterPoseHistoryBase::StaticStruct();
}

const UScriptStruct* FPoseSearchColumn::GetInputType() const
{
	return InputValue.IsValid() ? InputValue.GetScriptStruct() : nullptr;
}

void FPoseSearchColumn::SetInputType(const UScriptStruct* Type)
{
	InputValue.InitializeAs(Type);
}

void FPoseSearchColumn::AutoPopulate(int32 RowIndex, UObject* OutputObject)
{
	using namespace UE::PoseSearch;

	const int32 DatabaseAssetIndex = GetDatabaseAssetIndex(RowIndex);
	if (FPoseSearchDatabaseAnimationAsset* Asset = InternalDatabase->GetMutableDatabaseAnimationAsset(DatabaseAssetIndex))
	{
		if (Asset->AnimAsset != OutputObject)
		{
			InternalDatabase->Modify();
			Asset->AnimAsset = OutputObject;
		}
	}
	
	CheckConsistency();
}

bool FPoseSearchColumn::EditorTestFilter(int32 RowIndex) const
{
	using namespace UE::PoseSearch;

	for (FActiveColumnCost& ActiveColumnCost : ActiveColumnCosts)
	{
		if (ActiveColumnCost.RowIndex == RowIndex)
		{
			return true;
		}
	}
	return false;
}

float FPoseSearchColumn::EditorTestCost(int32 RowIndex) const
{
	using namespace UE::PoseSearch;

	for (FActiveColumnCost& ActiveColumnCost : ActiveColumnCosts)
	{
		if (ActiveColumnCost.RowIndex == RowIndex)
		{
			return ActiveColumnCost.RowCost;
		}
	}
	return 0.f;
}

void FPoseSearchColumn::SetTestValue(TArrayView<const uint8> Value)
{
    FMemoryReaderView Reader(Value);
    Reader << ActiveColumnCosts;
}

#endif // WITH_EDITOR

const FChooserParameterBase* FPoseSearchColumn::GetInputValue() const
{
	if (const FChooserParameterBase* ChooserParameterBase = InputValue.GetPtr<FChooserParameterBase>())
	{
		return ChooserParameterBase;
	}
	return &NullInputValue;
}

FChooserParameterBase* FPoseSearchColumn::GetInputValue()
{
	if (FChooserParameterBase* ChooserParameterBase = InputValue.GetMutablePtr<FChooserParameterBase>())
	{
		return ChooserParameterBase;
	}
	return &NullInputValue;
}

const UObject* FPoseSearchColumn::GetDatabaseAsset(int32 RowIndex) const
{
	using namespace UE::PoseSearch;
	const int32 DatabaseAssetIndex = GetDatabaseAssetIndex(RowIndex);

	if (const FPoseSearchDatabaseAnimationAsset* Asset = InternalDatabase->GetDatabaseAnimationAsset(DatabaseAssetIndex))
	{
		return Asset->GetAnimationAsset();
	}
	return nullptr;
}

const UPoseSearchSchema* FPoseSearchColumn::GetDatabaseSchema() const
{
	check(InternalDatabase);
	return InternalDatabase->Schema;
}
	
void FPoseSearchColumn::SetDatabaseSchema(const UPoseSearchSchema* Schema)
{
	check(InternalDatabase);
	InternalDatabase->Modify();
	InternalDatabase->Schema = Schema;
}

bool FPoseSearchColumn::HasFilters() const
{
	return true;
}

void FPoseSearchColumn::Filter(FChooserEvaluationContext& Context, const FChooserIndexArray& IndexListIn, FChooserIndexArray& IndexListOut, TArrayView<uint8> ScratchArea) const
{
	using namespace UE::PoseSearch;

	CheckConsistency();

	check(ScratchArea.Num() == GetScratchAreaSize());
	FPoseSearchColumnScratchArea* PoseSearchColumnScratchArea = reinterpret_cast<FPoseSearchColumnScratchArea*>(ScratchArea.begin());
#if DO_CHECK
	check(PoseSearchColumnScratchArea->DebugOwner == this);
	check(PoseSearchColumnScratchArea->SearchResults.IsEmpty());
#endif // DO_CHECK

	const UE::PoseSearch::IPoseHistory* PoseHistory = nullptr;
	FPoseHistoryReference PoseHistoryReference;
	if (const FChooserParameterPoseHistoryBase* PoseHistoryParameter = InputValue.GetPtr<FChooserParameterPoseHistoryBase>())
	{
		if (PoseHistoryParameter->GetValue(Context, PoseHistoryReference))
		{
			PoseHistory = PoseHistoryReference.PoseHistory.Get();
		}
	}

	PoseSearchColumnScratchArea->ChooserPlayerSettings = GetChooserPlayerSettings(Context);
	if (!PoseHistory && PoseSearchColumnScratchArea->ChooserPlayerSettings && PoseSearchColumnScratchArea->ChooserPlayerSettings->AnimationUpdateContext)
	{
		if (const FPoseHistoryProvider* PoseHistoryProvider = PoseSearchColumnScratchArea->ChooserPlayerSettings->AnimationUpdateContext->GetMessage<FPoseHistoryProvider>())
		{
			PoseHistory = &PoseHistoryProvider->GetPoseHistory();
		}
	}

#if WITH_EDITOR
	TArray<FActiveColumnCost> TracedActiveColumnCosts;
#endif // WITH_EDITOR

	if (!PoseHistory)
	{
		UE_LOG(LogPoseSearch, Error, TEXT("FPoseSearchColumn::Filter, missing IPoseHistory"));
	}
	else
	{
		PoseSearchColumnScratchArea->PoseHistory = PoseHistory;

		FStackAssetSet AssetsToConsider;
		for (const FChooserIndexArray::FIndexData& IndexData : IndexListIn)
		{
			const int32 RowIndex = IndexData.Index;
			const int32 DatabaseAssetIndex = GetDatabaseAssetIndex(RowIndex);
			const FPoseSearchDatabaseAnimationAsset* Asset = InternalDatabase->GetDatabaseAnimationAsset(DatabaseAssetIndex);
			if (ensure(Asset))
			{
				const UObject* AnimationAsset = Asset->GetAnimationAsset();
#if DO_CHECK && WITH_EDITOR
				// making sure InternalDatabase and the OuterChooser are still in synch
				const UObject* ReferencedObject = GetReferencedObject(RowIndex);
				check(ReferencedObject == AnimationAsset);
#endif // DO_CHECK && WITH_EDITOR
				AssetsToConsider.Add(AnimationAsset);
			}
		}

		if (!AssetsToConsider.IsEmpty())
		{
			const FFloatInterval PoseJumpThresholdTime(0.f, 0.f);
			const UObject* DatabasesToSearch[] = { InternalDatabase.Get() };
			FSearchContext SearchContext(0.f, PoseJumpThresholdTime, FPoseSearchEvent());

			const FRole Role = InternalDatabase->Schema ? InternalDatabase->Schema->GetDefaultRole() : DefaultRole;
			SearchContext.AddRole(Role, &Context, PoseHistory);
			SearchContext.SetAssetsToConsiderSet(&AssetsToConsider);

			FPoseSearchContinuingProperties ContinuingProperties;
			if (PoseSearchColumnScratchArea->ChooserPlayerSettings)
			{
				ContinuingProperties.PlayingAsset = PoseSearchColumnScratchArea->ChooserPlayerSettings->PlayingAsset;
				ContinuingProperties.PlayingAssetAccumulatedTime = PoseSearchColumnScratchArea->ChooserPlayerSettings->PlayingAssetAccumulatedTime;
				ContinuingProperties.bIsPlayingAssetMirrored = PoseSearchColumnScratchArea->ChooserPlayerSettings->bIsPlayingAssetMirrored;
				ContinuingProperties.PlayingAssetBlendParameters = PoseSearchColumnScratchArea->ChooserPlayerSettings->PlayingAssetBlendParameters;

				uint8 InterruptModeValue = 0;
				if (InterruptMode.IsValid() && InterruptMode.Get<FChooserParameterEnumBase>().GetValue(Context, InterruptModeValue))
				{
					ContinuingProperties.InterruptMode = (EPoseSearchInterruptMode)InterruptModeValue;
				}
			}

			FSearchResult BestSearchResult;
			if (MaxNumberOfResults == 1)
			{
				FSearchResults_Single SearchResults;
				UPoseSearchLibrary::MotionMatch(SearchContext, DatabasesToSearch, ContinuingProperties, SearchResults);
				BestSearchResult = SearchResults.GetBestResult();

				if (const FSearchIndexAsset* SearchIndexAsset = BestSearchResult.GetSearchIndexAsset())
				{
					check(BestSearchResult.Database == InternalDatabase);

					// @todo: handle duplicate entries, by providing to the UPoseSearchLibrary::MotionMatch a SourceAssetIdxsToConsider rather than AssetsToConsider
					const int32 SearchResultSourceAssetIdx = SearchIndexAsset->GetSourceAssetIdx();
					check(SearchResultSourceAssetIdx >= 0);

					const int32 RowIndex = GetRowIndex(SearchResultSourceAssetIdx);
					check(RowIndex >= 0);
					PoseSearchColumnScratchArea->SearchResults.Add(BestSearchResult);
					IndexListOut.Push({ static_cast<uint32>(RowIndex), BestSearchResult.PoseCost });
#if WITH_EDITOR
					TracedActiveColumnCosts.Add({ RowIndex, BestSearchResult.PoseCost });
#endif // WITH_EDITOR
				}
			}
			else
			{
				FSearchResults_AssetBests SearchResults;
				UPoseSearchLibrary::MotionMatch(SearchContext, DatabasesToSearch, ContinuingProperties, SearchResults);
				BestSearchResult = SearchResults.GetBestResult();
				if (MaxNumberOfResults > 0)
				{
					// keeping only up to MaxNumberOfResults results
					SearchResults.Shrink(MaxNumberOfResults);
				}

				for (const FChooserIndexArray::FIndexData& IndexData : IndexListIn)
				{
					const uint32 RowIndex = IndexData.Index;
					const int32 DatabaseAssetIndex = GetDatabaseAssetIndex(RowIndex);
					if (const FSearchResult* FoundSearchResult = SearchResults.FindSearchResultFor(InternalDatabase.Get(), DatabaseAssetIndex))
					{
						PoseSearchColumnScratchArea->SearchResults.Add(*FoundSearchResult);
						IndexListOut.Push({ RowIndex, FoundSearchResult->PoseCost });
#if WITH_EDITOR
						TracedActiveColumnCosts.Add({ static_cast<int32>(RowIndex), FoundSearchResult->PoseCost });
#endif // WITH_EDITOR
					}
				}
			}
		}
	}

#if WITH_EDITOR
	TRACE_CHOOSER_VALUE(Context, ToCStr(GetInputValue()->GetDebugName()), TracedActiveColumnCosts);
	if (Context.DebuggingInfo.bCurrentDebugTarget)
	{
		ActiveColumnCosts = TracedActiveColumnCosts;
	}
#endif // WITH_EDITOR
}

void FPoseSearchColumn::SetOutputs(FChooserEvaluationContext& Context, int RowIndex, TArrayView<uint8> ScratchArea) const
{
	using namespace UE::PoseSearch;

	// set an arbitrary "high" cost value so that cost threshold implementations can still wait for the pose search to find a good match
	float CostValue = 100.f;
	float StartTimeValue = 0.f;
	bool bMirrorValue = false;
	bool bForceBlendToValue = false;

	check(ScratchArea.Num() == GetScratchAreaSize());
	FPoseSearchColumnScratchArea* PoseSearchColumnScratchArea = reinterpret_cast<FPoseSearchColumnScratchArea*>(ScratchArea.begin());
#if DO_CHECK
	check(PoseSearchColumnScratchArea->DebugOwner == this);
#endif // DO_CHECK

	if (RowIndex == ChooserColumn_SpecialIndex_Fallback && PoseSearchColumnScratchArea->PoseHistory)
	{
		// making sure that if we search again with the fallback column we didn't find ANY previous result!
		check(PoseSearchColumnScratchArea->SearchResults.IsEmpty());

		// do the search again only with the fallback row..
		FStackAssetSet AssetsToConsider;
		const int32 DatabaseAssetIndex = GetDatabaseAssetIndex(RowIndex);
		const FPoseSearchDatabaseAnimationAsset* Asset = InternalDatabase->GetDatabaseAnimationAsset(DatabaseAssetIndex);
		if (ensure(Asset))
		{
			if (const UObject* AnimationAsset = Asset->GetAnimationAsset())
			{
#if DO_CHECK && WITH_EDITOR
				// making sure InternalDatabase and the OuterChooser are still in synch
				const UObject* ReferencedObject = GetReferencedObject(RowIndex);
				check(ReferencedObject == AnimationAsset);
#endif // DO_CHECK && WITH_EDITOR
				AssetsToConsider.Add(AnimationAsset);

				const FFloatInterval PoseJumpThresholdTime(0.f, 0.f);
				const UObject* DatabasesToSearch[] = { InternalDatabase.Get() };
				FSearchContext SearchContext(0.f, PoseJumpThresholdTime, FPoseSearchEvent());

				const FRole Role = InternalDatabase->Schema ? InternalDatabase->Schema->GetDefaultRole() : DefaultRole;
				SearchContext.AddRole(Role, &Context, PoseSearchColumnScratchArea->PoseHistory);
				SearchContext.SetAssetsToConsiderSet(&AssetsToConsider);

				FPoseSearchContinuingProperties ContinuingProperties;
				if (PoseSearchColumnScratchArea->ChooserPlayerSettings)
				{
					ContinuingProperties.PlayingAsset = PoseSearchColumnScratchArea->ChooserPlayerSettings->PlayingAsset;
					ContinuingProperties.PlayingAssetAccumulatedTime = PoseSearchColumnScratchArea->ChooserPlayerSettings->PlayingAssetAccumulatedTime;
					ContinuingProperties.bIsPlayingAssetMirrored = PoseSearchColumnScratchArea->ChooserPlayerSettings->bIsPlayingAssetMirrored;
					ContinuingProperties.PlayingAssetBlendParameters = PoseSearchColumnScratchArea->ChooserPlayerSettings->PlayingAssetBlendParameters;

					uint8 InterruptModeValue = 0;
					if (InterruptMode.IsValid() && InterruptMode.Get<FChooserParameterEnumBase>().GetValue(Context, InterruptModeValue))
					{
						ContinuingProperties.InterruptMode = (EPoseSearchInterruptMode)InterruptModeValue;
					}
				}

				FSearchResults_Single SearchResults;
				UPoseSearchLibrary::MotionMatch(SearchContext, DatabasesToSearch, ContinuingProperties, SearchResults);
				const FSearchResult BestSearchResult = SearchResults.GetBestResult();

				if (const FSearchIndexAsset* SearchIndexAsset = BestSearchResult.GetSearchIndexAsset())
				{
					check(BestSearchResult.Database == InternalDatabase);

					// @todo: handle duplicate entries, by providing to the UPoseSearchLibrary::MotionMatch a SourceAssetIdxsToConsider rather than AssetsToConsider
					const int32 SearchResultSourceAssetIdx = SearchIndexAsset->GetSourceAssetIdx();
					check(SearchResultSourceAssetIdx == 0 && RowIndex == GetRowIndex(SearchResultSourceAssetIdx));

					StartTimeValue = BestSearchResult.GetAssetTime();
					CostValue = BestSearchResult.PoseCost;
					bMirrorValue = SearchIndexAsset->IsMirrored();
					bForceBlendToValue = BestSearchResult.bIsContinuingPoseSearch;

					if (const FPoseIndicesHistory* PoseIndicesHistory = PoseSearchColumnScratchArea->PoseHistory->GetPoseIndicesHistory())
					{
						// @todo: integrate DeltaTime into SearchContext, and implement it for UAF as well
						float DeltaTime = FiniteDelta;
						if (const UAnimInstance* AnimInstance = Cast<UAnimInstance>(Context.GetFirstObjectParam()))
						{
							DeltaTime = AnimInstance->GetDeltaSeconds();
						}

						// const casting here is safe since we're in the thread owning the pose history, and it's the correct place to update the previously selected poses
						const_cast<FPoseIndicesHistory*>(PoseIndicesHistory)->Update(BestSearchResult, DeltaTime, PoseReselectHistory);
					}
				}
			}
		}
	}
	else
	{
		for (const FSearchResult& SearchResult : PoseSearchColumnScratchArea->SearchResults)
		{
			if (const FSearchIndexAsset* SearchIndexAsset = SearchResult.GetSearchIndexAsset())
			{
				const int32 SearchResultSourceAssetIdx = SearchIndexAsset->GetSourceAssetIdx();
				const int32 SearchResultRowIndex = GetRowIndex(SearchResultSourceAssetIdx);
				if (SearchResultRowIndex == RowIndex)
				{
					StartTimeValue = SearchResult.GetAssetTime();
					CostValue = SearchResult.PoseCost;
					bMirrorValue = SearchIndexAsset->IsMirrored();
					bForceBlendToValue = SearchResult.bIsContinuingPoseSearch;

					if (PoseSearchColumnScratchArea->PoseHistory)
					{
						if (const FPoseIndicesHistory* PoseIndicesHistory = PoseSearchColumnScratchArea->PoseHistory->GetPoseIndicesHistory())
						{
							// @todo: integrate DeltaTime into SearchContext, and implement it for UAF as well
							float DeltaTime = FiniteDelta;
							if (const UAnimInstance* AnimInstance = Cast<UAnimInstance>(Context.GetFirstObjectParam()))
							{
								DeltaTime = AnimInstance->GetDeltaSeconds();
							}

							// const casting here is safe since we're in the thread owning the pose history, and it's the correct place to update the previously selected poses
							const_cast<FPoseIndicesHistory*>(PoseIndicesHistory)->Update(SearchResult, DeltaTime, PoseReselectHistory);
						}
					}
					break;
				}
			}
		}
	}

	if (const FChooserParameterFloatBase* StartTime = OutputStartTime.GetPtr<FChooserParameterFloatBase>())
	{
		StartTime->SetValue(Context, StartTimeValue);
	}
		
	if (const FChooserParameterFloatBase* Cost = OutputCost.GetPtr<FChooserParameterFloatBase>())
	{
		Cost->SetValue(Context, CostValue);
	}

	if (const FChooserParameterBoolBase* Mirror = OutputMirror.GetPtr<FChooserParameterBoolBase>())
	{
		Mirror->SetValue(Context, bMirrorValue);
	}

	if (const FChooserParameterBoolBase* ForceBlendTo = OutputForceBlendTo.GetPtr<FChooserParameterBoolBase>())
	{
		ForceBlendTo->SetValue(Context, bForceBlendToValue);
	}
}

int32 FPoseSearchColumn::GetScratchAreaSize() const
{
	return sizeof(UE::PoseSearch::FPoseSearchColumnScratchArea);
}

void FPoseSearchColumn::InitializeScratchArea(TArrayView<uint8> ScratchArea) const
{
	using namespace UE::PoseSearch;
	check(ScratchArea.Num() == GetScratchAreaSize());
	FPoseSearchColumnScratchArea* PoseSearchColumnScratchArea = new(ScratchArea.begin()) FPoseSearchColumnScratchArea();

#if DO_CHECK
	PoseSearchColumnScratchArea->DebugOwner = this;
#endif // DO_CHECK
}

void FPoseSearchColumn::DeinitializeScratchArea(TArrayView<uint8> ScratchArea) const
{
	using namespace UE::PoseSearch;
	check(ScratchArea.Num() == GetScratchAreaSize());
	FPoseSearchColumnScratchArea* PoseSearchColumnScratchArea = reinterpret_cast<FPoseSearchColumnScratchArea*>(ScratchArea.begin());
#if DO_CHECK
	check(PoseSearchColumnScratchArea->DebugOwner == this);
#endif // DO_CHECK
	PoseSearchColumnScratchArea->~FPoseSearchColumnScratchArea();
}

#if WITH_EDITOR
void FPoseSearchColumn::Initialize(UChooserTable* OuterChooser)
{
	FChooserColumnBase::Initialize(OuterChooser);
	InternalDatabase = NewObject<UPoseSearchDatabase>(OuterChooser, OuterChooser->GetFName(), RF_Transactional);
}

void FPoseSearchColumn::AddToDetails(FInstancedPropertyBag& PropertyBag, int32 ColumnIndex, int32 RowIndex)
{
	using namespace UE::PoseSearch;

	check(InternalDatabase);

	FText DisplayName = NSLOCTEXT("PoseSearchColumn","Pose Search", "Pose Search");
	FName PropertyName("RowData", ColumnIndex);
	FPropertyBagPropertyDesc PropertyDesc(PropertyName,  EPropertyBagPropertyType::Struct, FPoseSearchDatabaseAnimationAsset::StaticStruct());
	PropertyDesc.MetaData.Add(FPropertyBagPropertyDescMetaData("DisplayName", DisplayName.ToString()));
	PropertyBag.AddProperties({PropertyDesc});
		
	const int32 DatabaseAssetIndex = GetDatabaseAssetIndex(RowIndex);
	if (const FPoseSearchDatabaseAnimationAsset* Asset = InternalDatabase->GetDatabaseAnimationAsset(DatabaseAssetIndex))
	{
		PropertyBag.SetValueStruct(PropertyName, *Asset);
	}
}

void FPoseSearchColumn::SetFromDetails(FInstancedPropertyBag& PropertyBag, int32 ColumnIndex, int32 RowIndex)
{
	using namespace UE::PoseSearch;

	check(InternalDatabase);

	FName PropertyName("RowData", ColumnIndex);
		
   	TValueOrError<FStructView, EPropertyBagResult> Result = PropertyBag.GetValueStruct(PropertyName, FPoseSearchDatabaseAnimationAsset::StaticStruct());
	if (FStructView* StructView = Result.TryGetValue())
	{
		const int32 DatabaseAssetIndex = GetDatabaseAssetIndex(RowIndex);
		if (FPoseSearchDatabaseAnimationAsset* Asset = InternalDatabase->GetMutableDatabaseAnimationAsset(DatabaseAssetIndex))
		{
			InternalDatabase->Modify();
			*Asset = StructView->Get<FPoseSearchDatabaseAnimationAsset>();
			InternalDatabase->NotifySynchronizeWithExternalDependencies();
		}
	}

	CheckConsistency();
}
#endif

#undef LOCTEXT_NAMESPACE
