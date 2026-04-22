// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include <IHasContext.h>

#include "CoreMinimal.h"
#include "ChooserPropertyAccess.h"
#include "IChooserColumn.h"
#include "PoseSearch/Chooser/ChooserParameterPoseHistoryBase.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include "Serialization/MemoryReader.h"
#include "StructUtils/InstancedStruct.h"
#include "Templates/SubclassOf.h"
#include "PoseSearchChooserColumn.generated.h"

#define UE_API POSESEARCH_API

class UAnimationAsset;

namespace UE::PoseSearch
{
	// Experimental, this feature might be removed without warning, not for production use
	struct FActiveColumnCost
	{
		int32 RowIndex = INDEX_NONE;
		float RowCost = 0.f;
	};
	FArchive& operator<<(FArchive& Ar, FActiveColumnCost& ActiveColumnCost);
} // namespace UE::PoseSearch


USTRUCT(Experimental, DisplayName = "Pose History Property Binding")
struct FPoseHistoryContextProperty :  public FChooserParameterPoseHistoryBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Meta = (BindingType = "FPoseHistoryReference", BindingAllowFunctions = "true", BindingColor = "StructPinTypeColor"), Category = "Binding")
	FChooserPropertyBinding Binding;
	
	UE_API virtual bool GetValue(FChooserEvaluationContext& Context, FPoseHistoryReference& OutResult) const override;
	virtual bool IsBound() const override
	{
		return Binding.IsBoundToRoot || !Binding.PropertyBindingChain.IsEmpty();
	}

	CHOOSER_PARAMETER_BOILERPLATE();
};

// reintroduced just to make the TRACE_CHOOSER_VALUE work, since it relies on ToCStr(InputValue.Get<FChooserParameterBase>().GetDebugName()) to work
USTRUCT(Experimental)
struct FPoseSearchColumnNullInputValue : public FChooserParameterBase
{
	GENERATED_BODY()

	virtual void GetDisplayName(FText& OutName) const override
	{
		OutName = FText::FromString("FPoseSearchColumn");
	}
};

USTRUCT(DisplayName = "Pose Match", Meta = (Experimental, Category = "Experimental", Tooltip = "This column filters out all assets except the one which is selected by motion matching query.  Results must be AnimationAssets with a PoseSearchBranchIn notify state.  It also outputs OutputStartTime to specify the frame which matched pose best.  To work as intended it must be placed last (furthest right) in the Chooser so that other filters are applied first."))
struct FPoseSearchColumn : public FChooserColumnBase
{
	GENERATED_BODY()
	
	UE_API FPoseSearchColumn();

private:
	UPROPERTY(VisibleAnywhere, Category = "Data")
	TObjectPtr<UPoseSearchDatabase> InternalDatabase;

public:
	// @todo: expose it as pinnable parameter
	// Prevent re-selection of poses that have been selected previously within this much time (in seconds) in the past. This is across all animation segments that have been selected within this time range.
	UPROPERTY(EditAnywhere, Category = Settings, meta = (PinHiddenByDefault, ClampMin = "0"))
	float PoseReselectHistory = 0.3f;

	// @todo: expose it as pinnable parameter
	// MaxNumberOfResults represent the maximum number of results from the motion matching search. if MaxNumberOfResults <= 0 the column will add ALL the results to the output
	// if bEnableMultiSelection is true, this FPoseSearchColumn will be set as cost column, retrieving multiple results from the MM search (one per chooser table entry)
	// and setting their relative costs to be used with subsequent column such as the FRandomizeColumn for additional results refinements.
	// if bEnableMultiSelection is false, this FPoseSearchColumn will return ONLY the best result, the one with the lowest cost
	UPROPERTY(EditAnywhere, Category = "Data")
	int32 MaxNumberOfResults = 1;

	// @todo: filter to allow only enums of type EPoseSearchInterruptMode
	UPROPERTY(EditAnywhere, NoClear, Meta = (ExcludeBaseStruct, BaseStruct = "/Script/Chooser.ChooserParameterEnumBase", ToolTip="Applied EPoseSearchInterruptMode controlling the continuing pose search evaluation. Defaulted to EPoseSearchInterruptMode::DoNotInterrupt if not set"), Category = "Data")
	FInstancedStruct InterruptMode;

	// Pose History
	UPROPERTY(EditAnywhere, DisplayName = "Pose History", NoClear, Meta = (ExcludeBaseStruct, BaseStruct = "/Script/PoseSearch.ChooserParameterPoseHistoryBase"), Category = "Data")
	FInstancedStruct InputValue;
	
	// Experimental, this feature might be removed without warning, not for production use
	// reintroduced just to make the TRACE_CHOOSER_VALUE work, since it relies on ToCStr(InputValue.Get<FChooserParameterBase>().GetDebugName()) to work
	UPROPERTY()
	FPoseSearchColumnNullInputValue NullInputValue;

	// Float output for the start time with the best matching pose
	UPROPERTY(EditAnywhere, NoClear, Meta = (ExcludeBaseStruct, BaseStruct = "/Script/Chooser.ChooserParameterFloatBase"), Category = "Output")
	FInstancedStruct OutputStartTime;
	
	// Bool output for if asset should be mirrored
	UPROPERTY(EditAnywhere, NoClear, Meta = (ExcludeBaseStruct, BaseStruct = "/Script/Chooser.ChooserParameterBoolBase"), Category = "Output")
	FInstancedStruct OutputMirror;

	// Bool output for suggesting a chooser player to force a blend into the newly selected asset
	UPROPERTY(EditAnywhere, NoClear, Meta = (ExcludeBaseStruct, BaseStruct = "/Script/Chooser.ChooserParameterBoolBase"), Category = "Output")
	FInstancedStruct OutputForceBlendTo;

	// Float output for the cost of the selected pose
	UPROPERTY(EditAnywhere, NoClear, Meta = (ExcludeBaseStruct, BaseStruct = "/Script/Chooser.ChooserParameterFloatBase"), Category = "Output")
	FInstancedStruct OutputCost;

	UE_API const UObject* GetDatabaseAsset(int32 RowIndex) const;
	UE_API const UPoseSearchSchema* GetDatabaseSchema() const;
	UE_API void SetDatabaseSchema(const UPoseSearchSchema* Schema);

private:
	virtual bool HasFilters() const override;
	virtual void Filter(FChooserEvaluationContext& Context, const FChooserIndexArray& IndexListIn, FChooserIndexArray& IndexListOut, TArrayView<uint8> ScratchArea) const override;
	virtual bool HasCosts() const override { return true; }
	virtual void SetOutputs(FChooserEvaluationContext& Context, int RowIndex, TArrayView<uint8> ScratchArea) const override;

	virtual int32 GetScratchAreaSize() const override;
	virtual void InitializeScratchArea(TArrayView<uint8> ScratchArea) const override;
	virtual void DeinitializeScratchArea(TArrayView<uint8> ScratchArea) const override;

#if WITH_EDITOR
	virtual void Initialize(UChooserTable* OuterChooser) override;
	
	mutable TArray<UE::PoseSearch::FActiveColumnCost> ActiveColumnCosts;

	virtual bool EditorTestFilter(int32 RowIndex) const override;
	virtual float EditorTestCost(int32 RowIndex) const override;
	virtual void SetTestValue(TArrayView<const uint8> Value) override;

	virtual void AddToDetails(FInstancedPropertyBag& PropertyBag, int32 ColumnIndex, int32 RowIndex) override;
	virtual void SetFromDetails(FInstancedPropertyBag& PropertyBag, int32 ColumnIndex, int32 RowIndex) override;

	virtual FName RowValuesPropertyName() override;
	virtual void SetNumRows(int32 NumRows) override;
	virtual void InsertRows(int Index, int Count) override;
	virtual void DeleteRows(const TArray<uint32>& RowIndices) override;
	virtual void MoveRow(int SourceRowIndex, int TargetRowIndex) override;
	virtual void CopyRow(FChooserColumnBase& SourceColumn, int SourceRowIndex, int TargetRowIndex) override;
	virtual UScriptStruct* GetInputBaseType() const override;
	virtual const UScriptStruct* GetInputType() const override;
	virtual void SetInputType(const UScriptStruct* Type) override;
	
	virtual bool AutoPopulates() const override { return true; }
	virtual void AutoPopulate(int32 RowIndex, UObject* OutputObject) override;

#endif // WITH_EDITOR
	virtual FChooserParameterBase* GetInputValue() override;
	const FChooserParameterBase* GetInputValue() const;

#if WITH_EDITOR
	UObject* GetReferencedObject(int32 RowIndex) const;
#endif // WITH_EDITOR
	
	void CheckConsistency() const;
};

#undef UE_API
