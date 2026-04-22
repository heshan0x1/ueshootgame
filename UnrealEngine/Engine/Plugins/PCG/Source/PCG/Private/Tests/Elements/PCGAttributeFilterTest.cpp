// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tests/PCGTestsCommon.h"

#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGParamData.h"
#include "PCGPoint.h"
#include "Algo/Count.h"
#include "Data/PCGPointData.h"
#include "Elements/PCGAttributeFilter.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttributeTpl.h"

#include "Math/RandomStream.h"


IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGPointFilterDensity, FPCGTestBaseClass, "Plugins.PCG.AttributeFilter.Points.Density", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGPointFilterDensityRange, FPCGTestBaseClass, "Plugins.PCG.AttributeFilter.Points.DensityRange", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGAttributeFilterInt, FPCGTestBaseClass, "Plugins.PCG.AttributeFilter.Params.Int", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGAttributeFilterIntRange, FPCGTestBaseClass, "Plugins.PCG.AttributeFilter.Params.IntRange", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGAttributeFilterSkipTestBug, FPCGTestBaseClass, "Plugins.PCG.AttributeFilter.SkipTestBug", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGAttributeFilter_Points_GenerateEmptyOutput, FPCGTestBaseClass, "Plugins.PCG.AttributeFilter.Points.GenerateEmptyOutput", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGAttributeFilter_PointsRange_GenerateEmptyOutput, FPCGTestBaseClass, "Plugins.PCG.AttributeFilter.PointsRange.GenerateEmptyOutput", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGAttributeFilter_Attributes_GenerateEmptyOutput, FPCGTestBaseClass, "Plugins.PCG.AttributeFilter.Params.GenerateEmptyOutput", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGAttributeFilter_AttributesRange_GenerateEmptyOutput, FPCGTestBaseClass, "Plugins.PCG.AttributeFilter.ParamsRange.GenerateEmptyOutput", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGAttributeFilter_Points_NoGenerateEmptyOutput, FPCGTestBaseClass, "Plugins.PCG.AttributeFilter.Points.NoGenerateEmptyOutput", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGAttributeFilter_PointsRange_NoGenerateEmptyOutput, FPCGTestBaseClass, "Plugins.PCG.AttributeFilter.PointsRange.NoGenerateEmptyOutput", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGAttributeFilter_Attributes_NoGenerateEmptyOutput, FPCGTestBaseClass, "Plugins.PCG.AttributeFilter.Params.NoGenerateEmptyOutput", PCGTestsCommon::TestFlags)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPCGAttributeFilter_AttributesRange_NoGenerateEmptyOutput, FPCGTestBaseClass, "Plugins.PCG.AttributeFilter.ParamsRange.NoGenerateEmptyOutput", PCGTestsCommon::TestFlags)

namespace PCGPointFilterTest
{
	const FName InsideFilterLabel = TEXT("InsideFilter");
	const FName OutsideFilterLabel = TEXT("OutsideFilter");
	const FName FilterLabel = TEXT("Filter");

	UPCGBasePointData* GeneratePointDataWithRandomDensity(int32 InNumPoints, int32 InRandomSeed)
	{
		UPCGBasePointData* PointData = PCGTestsCommon::CreateEmptyBasePointData();
		
		PointData->SetNumPoints(InNumPoints);
		PointData->AllocateProperties(EPCGPointNativeProperties::Transform | EPCGPointNativeProperties::Density | EPCGPointNativeProperties::Seed);

		FRandomStream RandomSource(InRandomSeed);

		FPCGPointValueRanges ValueRanges(PointData, /*bAllocate=*/false);
		for (int I = 0; I < InNumPoints; ++I)
		{
			ValueRanges.TransformRange[I] = FTransform();
			ValueRanges.DensityRange[I] = RandomSource.FRand();
			ValueRanges.SeedRange[I] = I;
		}

		return PointData;
	}

	UPCGBasePointData* GeneratePointDataWithSingleDensity(const float Density)
	{
		UPCGBasePointData* PointData = PCGTestsCommon::CreateEmptyBasePointData();

		PointData->SetNumPoints(1);
		PointData->AllocateProperties(EPCGPointNativeProperties::Density);

		FPCGPointValueRanges ValueRanges(PointData, /*bAllocate=*/false);
		ValueRanges.DensityRange[0] = Density;

		return PointData;
	}

	static const FName IntAttributeName = TEXT("Int");

	UPCGParamData* GenerateAttributeSetWithRandomInt(int32 InNumEntries, int32 InRandomSeed)
	{
		UPCGParamData* ParamData = NewObject<UPCGParamData>();
		check(ParamData && ParamData->Metadata);
		FPCGMetadataAttribute<int32>* Attribute = ParamData->Metadata->CreateAttribute<int32>(IntAttributeName, 0, true, false);
		check(Attribute)

		FRandomStream RandomSource(InRandomSeed);

		for (int I = 0; I < InNumEntries; ++I)
		{
			PCGMetadataEntryKey Key = ParamData->Metadata->AddEntry();
			Attribute->SetValue(Key, RandomSource.GetUnsignedInt());
		}

		return ParamData;
	}

	static const FName DoubleAttributeName = TEXT("Double");

	UPCGParamData* GenerateAttributeSetWithSingleValue(const double Value)
	{
		UPCGParamData* ParamData = NewObject<UPCGParamData>();
		check(ParamData && ParamData->Metadata);
		FPCGMetadataAttribute<double>* Attribute = ParamData->Metadata->CreateAttribute<double>(DoubleAttributeName, 0, true, false);
		check(Attribute)

		const PCGMetadataEntryKey Key = ParamData->Metadata->AddEntry();
		Attribute->SetValue(Key, Value);

		return ParamData;
	}

	int GetNumEmptyData(const TArray<FPCGTaggedData>& InDataArray)
	{
		return Algo::CountIf(InDataArray, [](const FPCGTaggedData& Data)
		{
			if (const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(Data.Data))
			{
				return PointData->IsEmpty();
			}
			else if (const UPCGParamData* ParamData = Cast<UPCGParamData>(Data.Data))
			{
				check(ParamData->Metadata)
				return ParamData->Metadata->GetAttributeCount() == 0 || ParamData->Metadata->GetItemCountForChild() == 0;
			}

			return false;
		});
	}
}

bool FPCGPointFilterDensity::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData;
	UPCGAttributeFilteringSettings* Settings = PCGTestsCommon::GenerateSettings<UPCGAttributeFilteringSettings>(TestData);
	check(Settings);

	FPCGElementPtr TestElement = TestData.Settings->GetElement();

	static const int32 NumPoints = 50;
	static const float DensityThreshold = 0.5f;

	Settings->Operator = EPCGAttributeFilterOperator::Lesser;
	Settings->TargetAttribute.SetPointProperty(EPCGPointProperties::Density);
	Settings->bUseConstantThreshold = true;
	Settings->AttributeTypes.FloatValue = DensityThreshold;
	Settings->AttributeTypes.Type = EPCGMetadataTypes::Float;

	FPCGTaggedData& TaggedData = TestData.InputData.TaggedData.Emplace_GetRef(FPCGTaggedData());
	TaggedData.Pin = PCGPinConstants::DefaultInputLabel;
	TaggedData.Data = PCGPointFilterTest::GeneratePointDataWithRandomDensity(NumPoints, TestData.Seed);

	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();

	while (!TestElement->Execute(Context.Get())) {}

	// We should have outputs on both
	TArray<FPCGTaggedData> InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	TArray<FPCGTaggedData> OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	UTEST_EQUAL(TEXT("InFilter pin has 1 output"), InFilterOutput.Num(), 1);
	UTEST_EQUAL(TEXT("OutFilter pin has 1 output"), OutFilterOutput.Num(), 1);

	// Making sure the output is a PointData
	const UPCGBasePointData* InFilterPointData = Cast<UPCGBasePointData>(InFilterOutput[0].Data);
	const UPCGBasePointData* OutFilterPointData = Cast<UPCGBasePointData>(OutFilterOutput[0].Data);

	UTEST_NOT_NULL(TEXT("InFilter data is a point data"), InFilterPointData);
	UTEST_NOT_NULL(TEXT("OutFilter data is a point data"), OutFilterPointData);

	const TConstPCGValueRange<float> InFilterDensityRange = InFilterPointData->GetConstDensityValueRange();

	// Verifying that all points have the right density
	for (float InFilterDensity : InFilterDensityRange)
	{
		UTEST_TRUE(*FString::Printf(TEXT("Point has a density (%f) lower than the threshold (%f)"), InFilterDensity, DensityThreshold), InFilterDensity < DensityThreshold);
	}

	const TConstPCGValueRange<float> OutFilterDensityRange = OutFilterPointData->GetConstDensityValueRange();

	for (float OutFilterDensity : OutFilterDensityRange)
	{
		UTEST_TRUE(*FString::Printf(TEXT("Point has a density (%f) higher or equal than the threshold (%f)"), OutFilterDensity, DensityThreshold), OutFilterDensity >= DensityThreshold);
	}

	return true;
}

bool FPCGPointFilterDensityRange::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData;
	UPCGAttributeFilteringRangeSettings* Settings = PCGTestsCommon::GenerateSettings<UPCGAttributeFilteringRangeSettings>(TestData);
	check(Settings);

	FPCGElementPtr TestElement = TestData.Settings->GetElement();

	static const int32 NumPoints = 50;
	static const float DensityMinThreshold = 0.3f;
	static const float DensityMaxThreshold = 0.8f;

	Settings->TargetAttribute.SetPointProperty(EPCGPointProperties::Density);

	Settings->MinThreshold.bUseConstantThreshold = true;
	Settings->MinThreshold.AttributeTypes.FloatValue = DensityMinThreshold;
	Settings->MinThreshold.AttributeTypes.Type = EPCGMetadataTypes::Float;

	Settings->MaxThreshold.bUseConstantThreshold = true;
	Settings->MaxThreshold.AttributeTypes.FloatValue = DensityMaxThreshold;
	Settings->MaxThreshold.AttributeTypes.Type = EPCGMetadataTypes::Float;

	FPCGTaggedData& TaggedData = TestData.InputData.TaggedData.Emplace_GetRef(FPCGTaggedData());
	TaggedData.Pin = PCGPinConstants::DefaultInputLabel;
	TaggedData.Data = PCGPointFilterTest::GeneratePointDataWithRandomDensity(NumPoints, TestData.Seed);

	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();

	while (!TestElement->Execute(Context.Get())) {}

	// We should have outputs on both
	TArray<FPCGTaggedData> InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	TArray<FPCGTaggedData> OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	UTEST_EQUAL(TEXT("InFilter pin has 1 output"), InFilterOutput.Num(), 1);
	UTEST_EQUAL(TEXT("OutFilter pin has 1 output"), OutFilterOutput.Num(), 1);

	// Making sure the output is a PointData
	const UPCGBasePointData* InFilterPointData = Cast<UPCGBasePointData>(InFilterOutput[0].Data);
	const UPCGBasePointData* OutFilterPointData = Cast<UPCGBasePointData>(OutFilterOutput[0].Data);

	UTEST_NOT_NULL(TEXT("InFilter data is a point data"), InFilterPointData);
	UTEST_NOT_NULL(TEXT("OutFilter data is a point data"), OutFilterPointData);

	const TConstPCGValueRange<float> InFilterDensityRange = InFilterPointData->GetConstDensityValueRange();

	// Verifying that all points have the right density
	for (float InFilterDensity : InFilterDensityRange)
	{
		UTEST_TRUE(*FString::Printf(TEXT("Point has a density (%f) within the range ([%f, %f])"), InFilterDensity, DensityMinThreshold, DensityMaxThreshold), (InFilterDensity >= DensityMinThreshold) && (InFilterDensity <= DensityMaxThreshold));
	}

	const TConstPCGValueRange<float> OutFilterDensityRange = OutFilterPointData->GetConstDensityValueRange();

	for (float OutFilterDensity : OutFilterDensityRange)
	{
		UTEST_TRUE(*FString::Printf(TEXT("Point has a density (%f) outside the range ([%f, %f])"), OutFilterDensity, DensityMinThreshold, DensityMaxThreshold), (OutFilterDensity < DensityMinThreshold) || (OutFilterDensity > DensityMaxThreshold));
	}

	return true;
}

bool FPCGAttributeFilterInt::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData;
	UPCGAttributeFilteringSettings* Settings = PCGTestsCommon::GenerateSettings<UPCGAttributeFilteringSettings>(TestData);
	check(Settings);

	FPCGElementPtr TestElement = TestData.Settings->GetElement();

	static const int32 NumEntries = 50;
	static const int64 IntThreshold = 999999;

	Settings->Operator = EPCGAttributeFilterOperator::Lesser;
	Settings->TargetAttribute.SetAttributeName(PCGPointFilterTest::IntAttributeName);
	Settings->bUseConstantThreshold = true;
	Settings->AttributeTypes.IntValue = IntThreshold;
	Settings->AttributeTypes.Type = EPCGMetadataTypes::Integer64;

	FPCGTaggedData& TaggedData = TestData.InputData.TaggedData.Emplace_GetRef(FPCGTaggedData());
	TaggedData.Pin = PCGPinConstants::DefaultInputLabel;
	TaggedData.Data = PCGPointFilterTest::GenerateAttributeSetWithRandomInt(NumEntries, TestData.Seed);

	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();

	while (!TestElement->Execute(Context.Get())) {}

	// We should have outputs on both
	TArray<FPCGTaggedData> InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	TArray<FPCGTaggedData> OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	UTEST_EQUAL(TEXT("InFilter pin has 1 output"), InFilterOutput.Num(), 1);
	UTEST_EQUAL(TEXT("OutFilter pin has 1 output"), OutFilterOutput.Num(), 1);

	// Making sure the output is a ParamData, and has its attribute
	const UPCGParamData* InFilterParamData = Cast<UPCGParamData>(InFilterOutput[0].Data);
	const UPCGParamData* OutFilterParamData = Cast<UPCGParamData>(OutFilterOutput[0].Data);

	UTEST_NOT_NULL(TEXT("InFilter data is a param data"), InFilterParamData);
	UTEST_NOT_NULL(TEXT("OutFilter data is a param data"), OutFilterParamData);

	const FPCGMetadataAttribute<int32>* InFilterAttribute = InFilterParamData->Metadata->GetConstTypedAttribute<int32>(PCGPointFilterTest::IntAttributeName);
	const FPCGMetadataAttribute<int32>* OutFilterAttribute = OutFilterParamData->Metadata->GetConstTypedAttribute<int32>(PCGPointFilterTest::IntAttributeName);

	UTEST_NOT_NULL(TEXT("InFilter metadata has the int attribute"), InFilterAttribute);
	UTEST_NOT_NULL(TEXT("OutFilter metadata has the int attribute"), OutFilterAttribute);

	// Verifying that all points have the right int value
	for (int32 Key = 0; Key < InFilterParamData->Metadata->GetLocalItemCount(); ++Key)
	{
		int32 Value = InFilterAttribute->GetValueFromItemKey(Key);
		UTEST_TRUE(*FString::Printf(TEXT("Attribute has a value (%d) lower than the threshold (%d)"), Value, (int32)IntThreshold), Value < (int32)IntThreshold);
	}

	for (int32 Key = 0; Key < OutFilterParamData->Metadata->GetLocalItemCount(); ++Key)
	{
		int32 Value = OutFilterAttribute->GetValueFromItemKey(Key);
		UTEST_TRUE(*FString::Printf(TEXT("Attribute has a value (%d) higher than the threshold (%d)"), Value, (int32)IntThreshold), Value >= (int32)IntThreshold);
	}

	return true;
}

bool FPCGAttributeFilterIntRange::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData;
	UPCGAttributeFilteringRangeSettings* Settings = PCGTestsCommon::GenerateSettings<UPCGAttributeFilteringRangeSettings>(TestData);
	check(Settings);

	FPCGElementPtr TestElement = TestData.Settings->GetElement();

	static const int32 NumEntries = 50;
	static const int64 IntMinThreshold = 0;
	static const int64 IntMaxThreshold = 1999999999;

	Settings->TargetAttribute.SetAttributeName(PCGPointFilterTest::IntAttributeName);

	Settings->MinThreshold.bUseConstantThreshold = true;
	Settings->MinThreshold.AttributeTypes.IntValue = IntMinThreshold;
	Settings->MinThreshold.AttributeTypes.Type = EPCGMetadataTypes::Integer64;

	Settings->MaxThreshold.bUseConstantThreshold = true;
	Settings->MaxThreshold.AttributeTypes.IntValue = IntMaxThreshold;
	Settings->MaxThreshold.AttributeTypes.Type = EPCGMetadataTypes::Integer64;

	FPCGTaggedData& TaggedData = TestData.InputData.TaggedData.Emplace_GetRef(FPCGTaggedData());
	TaggedData.Pin = PCGPinConstants::DefaultInputLabel;
	TaggedData.Data = PCGPointFilterTest::GenerateAttributeSetWithRandomInt(NumEntries, TestData.Seed);

	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();

	while (!TestElement->Execute(Context.Get())) {}

	// We should have outputs on both
	TArray<FPCGTaggedData> InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	TArray<FPCGTaggedData> OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	UTEST_EQUAL(TEXT("InFilter pin has 1 output"), InFilterOutput.Num(), 1);
	UTEST_EQUAL(TEXT("OutFilter pin has 1 output"), OutFilterOutput.Num(), 1);

	// Making sure the output is a ParamData, and has its attribute
	const UPCGParamData* InFilterParamData = Cast<UPCGParamData>(InFilterOutput[0].Data);
	const UPCGParamData* OutFilterParamData = Cast<UPCGParamData>(OutFilterOutput[0].Data);

	UTEST_NOT_NULL(TEXT("InFilter data is a param data"), InFilterParamData);
	UTEST_NOT_NULL(TEXT("OutFilter data is a param data"), OutFilterParamData);

	const FPCGMetadataAttribute<int32>* InFilterAttribute = InFilterParamData->Metadata->GetConstTypedAttribute<int32>(PCGPointFilterTest::IntAttributeName);
	const FPCGMetadataAttribute<int32>* OutFilterAttribute = OutFilterParamData->Metadata->GetConstTypedAttribute<int32>(PCGPointFilterTest::IntAttributeName);

	UTEST_NOT_NULL(TEXT("InFilter metadata has the int attribute"), InFilterAttribute);
	UTEST_NOT_NULL(TEXT("OutFilter metadata has the int attribute"), OutFilterAttribute);

	// Verifying that all points have the right int value
	for (int32 Key = 0; Key < InFilterParamData->Metadata->GetLocalItemCount(); ++Key)
	{
		int32 Value = InFilterAttribute->GetValueFromItemKey(Key);
		UTEST_TRUE(*FString::Printf(TEXT("Attribute has a value (%d) within the range [%d, %d]"), Value, (int32)IntMinThreshold, (int32)IntMaxThreshold), Value >= (int32)IntMinThreshold && Value <= (int32)IntMaxThreshold);
	}

	for (int32 Key = 0; Key < OutFilterParamData->Metadata->GetLocalItemCount(); ++Key)
	{
		int32 Value = OutFilterAttribute->GetValueFromItemKey(Key);
		UTEST_TRUE(*FString::Printf(TEXT("Attribute has a value (%d) outside the range [%d, %d]"), Value, (int32)IntMinThreshold, (int32)IntMaxThreshold), Value < (int32)IntMinThreshold || Value > (int32)IntMaxThreshold);
	}

	return true;
}

// We do not reset SkipTests in the case of point sampling, resulting to accepting points that should have not been accepted.
// This fails before fixed CL of UE-201595.
bool FPCGAttributeFilterSkipTestBug::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData;
	UPCGAttributeFilteringSettings* Settings = PCGTestsCommon::GenerateSettings<UPCGAttributeFilteringSettings>(TestData);
	check(Settings);

	Settings->TargetAttribute.SetPointProperty(EPCGPointProperties::Density);
	Settings->ThresholdAttribute.SetPointProperty(EPCGPointProperties::Density);
	Settings->Operator = EPCGAttributeFilterOperator::Equal;
	Settings->bUseSpatialQuery = true;

	FPCGElementPtr TestElement = TestData.Settings->GetElement();
	UPCGBasePointData* InputPointData = PCGTestsCommon::CreateEmptyBasePointData();
	UPCGBasePointData* ThresholdPointData = PCGTestsCommon::CreateEmptyBasePointData();

	// Take a big number to make sure we go over the 256 default chunk size
	constexpr int32 NumPoints = 2048;
	constexpr int32 HalfNumPoints = NumPoints / 2;
	InputPointData->SetNumPoints(NumPoints);
	TPCGValueRange<FTransform> InputTransformRange = InputPointData->GetTransformValueRange();

	ThresholdPointData->SetNumPoints(NumPoints);
	TPCGValueRange<FTransform> ThresholdTransformRange = ThresholdPointData->GetTransformValueRange();
	TPCGValueRange<float> ThresholdDensityRange = ThresholdPointData->GetDensityValueRange();

	for (int32 i = 0; i < NumPoints; ++i)
	{
		// First half are very different so the sampling should fail, second half are the same the sampling to succeed but the filtering to fail.
		if (i >= HalfNumPoints)
		{
			InputTransformRange[i].SetLocation(FVector(10 * i, 10 * i, 10 * i));
			ThresholdTransformRange[i] = InputTransformRange[i];
			ThresholdDensityRange[i] = 0.5f;
		}
		else
		{
			InputTransformRange[i].SetLocation(FVector(i, i, i));
			ThresholdTransformRange[i].SetLocation(FVector(-10 * i - 1000, -10 * i - 1000, -10 * i - 1000));
		}
	}

	FPCGTaggedData& TaggedData = TestData.InputData.TaggedData.Emplace_GetRef();
	TaggedData.Pin = PCGPinConstants::DefaultInputLabel;
	TaggedData.Data = InputPointData;

	FPCGTaggedData& SecondTaggedData = TestData.InputData.TaggedData.Emplace_GetRef();
	SecondTaggedData.Pin = PCGPointFilterTest::FilterLabel;
	SecondTaggedData.Data = ThresholdPointData;

	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();

	while (!TestElement->Execute(Context.Get())) {}

	// We should have outputs on both
	TArray<FPCGTaggedData> InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	TArray<FPCGTaggedData> OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	UTEST_EQUAL(TEXT("InFilter pin has 1 output"), InFilterOutput.Num(), 1);
	UTEST_EQUAL(TEXT("OutFilter pin has 1 output"), OutFilterOutput.Num(), 1);

	// Making sure the output is a PointData
	const UPCGBasePointData* InFilterPointData = Cast<UPCGBasePointData>(InFilterOutput[0].Data);
	const UPCGBasePointData* OutFilterPointData = Cast<UPCGBasePointData>(OutFilterOutput[0].Data);

	UTEST_NOT_NULL(TEXT("InFilter data is a point data"), InFilterPointData);
	UTEST_NOT_NULL(TEXT("OutFilter data is a point data"), OutFilterPointData);

	// We should have the inside filter with half the points and the outside filter to the rest
	UTEST_EQUAL(TEXT("InFilter data has the right number of points"), InFilterPointData->GetNumPoints(), HalfNumPoints);
	UTEST_EQUAL(TEXT("OutFilter data has the right number of points"), OutFilterPointData->GetNumPoints(), HalfNumPoints);

	return true;
}

bool FPCGAttributeFilter_Points_GenerateEmptyOutput::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData;
	UPCGAttributeFilteringSettings* Settings = PCGTestsCommon::GenerateSettings<UPCGAttributeFilteringSettings>(TestData);
	check(Settings);

	const FPCGElementPtr TestElement = TestData.Settings->GetElement();

	Settings->Operator = EPCGAttributeFilterOperator::Greater;
	Settings->TargetAttribute.SetPointProperty(EPCGPointProperties::Density);
	Settings->bUseConstantThreshold = true;
	Settings->AttributeTypes.Type = EPCGMetadataTypes::Float;

	// Generate 4 point data, each with 1 point that has a normalized density based on its data index: .25, .5, .75, 1.0
	for (int32 I = 0; I < 4; ++I)
	{
		FPCGTaggedData& TaggedData = TestData.InputData.TaggedData.Emplace_GetRef(FPCGTaggedData());
		TaggedData.Pin = PCGPinConstants::DefaultInputLabel;
		TaggedData.Data = PCGPointFilterTest::GeneratePointDataWithSingleDensity((1.0 + I) / 4.0);
	}

	Settings->bGenerateOutputDataEvenIfEmpty = true;

	// All are > 0.0
	Settings->AttributeTypes.FloatValue = 0.f;
	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}

	// When always generating output, all data should exist in both
	TArray<FPCGTaggedData> InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	TArray<FPCGTaggedData> OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 4);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 4);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 0);
	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 4);

	// None are > 1.0
	Settings->AttributeTypes.FloatValue = 1.f;
	Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}

	InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 4);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 4);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 4);
	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 0);

	return true;
}

bool FPCGAttributeFilter_PointsRange_GenerateEmptyOutput::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData;
	UPCGAttributeFilteringRangeSettings* Settings = PCGTestsCommon::GenerateSettings<UPCGAttributeFilteringRangeSettings>(TestData);
	check(Settings);

	const FPCGElementPtr TestElement = TestData.Settings->GetElement();

	Settings->TargetAttribute.SetPointProperty(EPCGPointProperties::Density);
	Settings->MinThreshold.bUseConstantThreshold = true;
	Settings->MaxThreshold.bUseConstantThreshold = true;
	Settings->MinThreshold.AttributeTypes.Type = EPCGMetadataTypes::Float;
	Settings->MaxThreshold.AttributeTypes.Type = EPCGMetadataTypes::Float;
	Settings->MinThreshold.bInclusive = true;
	Settings->MaxThreshold.bInclusive = true;

	// Generate 4 point data, each with 1 point that has a normalized density based on its data index: 0, .25, .5, 0.75
	for (int32 I = 0; I < 4; ++I)
	{
		FPCGTaggedData& TaggedData = TestData.InputData.TaggedData.Emplace_GetRef(FPCGTaggedData());
		TaggedData.Pin = PCGPinConstants::DefaultInputLabel;
		TaggedData.Data = PCGPointFilterTest::GeneratePointDataWithSingleDensity((1.0 + I) / 4.0);
	}

	// Check unfiltered empty data.
	Settings->bGenerateOutputDataEvenIfEmpty = true;

	Settings->MinThreshold.AttributeTypes.FloatValue = 0.0f;
	Settings->MaxThreshold.AttributeTypes.FloatValue = 1.0f;
	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}

	// When always generating output, all data should exist in both
	TArray<FPCGTaggedData> InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	TArray<FPCGTaggedData> OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// All, inclusive.
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 4);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 4);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 0);
	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 4);

	Settings->MinThreshold.AttributeTypes.FloatValue = 0.0f;
	Settings->MaxThreshold.AttributeTypes.FloatValue = 0.0f;
	Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}

	// When always generating output, all data should exist in both
	InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// None, inclusive.
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 4);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 4);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 4);
	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 0);

	return true;
}

bool FPCGAttributeFilter_Attributes_GenerateEmptyOutput::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData;
	UPCGAttributeFilteringSettings* Settings = PCGTestsCommon::GenerateSettings<UPCGAttributeFilteringSettings>(TestData);
	check(Settings);

	const FPCGElementPtr TestElement = TestData.Settings->GetElement();

	Settings->Operator = EPCGAttributeFilterOperator::Greater;
	Settings->TargetAttribute.SetAttributeName(PCGPointFilterTest::DoubleAttributeName);
	Settings->AttributeTypes.Type = EPCGMetadataTypes::Double;
	Settings->bUseConstantThreshold = true;

	// Generate 4 point data, each with 1 point that has a normalized value based on its data index: .25, .5, .75, 1.0
	for (int32 I = 0; I < 4; ++I)
	{
		FPCGTaggedData& TaggedData = TestData.InputData.TaggedData.Emplace_GetRef(FPCGTaggedData());
		TaggedData.Pin = PCGPinConstants::DefaultInputLabel;
		TaggedData.Data = PCGPointFilterTest::GenerateAttributeSetWithSingleValue((1.0 + I) / 4.0);
	}

	// Check unfiltered empty data.
	Settings->bGenerateOutputDataEvenIfEmpty = true;

	// All are > 0.0
	Settings->AttributeTypes.DoubleValue = 0.f;
	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}

	// When always generating output, all data should exist in both
	TArray<FPCGTaggedData> InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	TArray<FPCGTaggedData> OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 4);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 4);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 0);
	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 4);

	// None are > 1.0
	Settings->AttributeTypes.DoubleValue = 1.f;
	Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}

	// When always generating output, all data should exist in both
	InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 4);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 4);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 4);
	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 0);

	return true;
}

bool FPCGAttributeFilter_AttributesRange_GenerateEmptyOutput::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData;
	UPCGAttributeFilteringRangeSettings* Settings = PCGTestsCommon::GenerateSettings<UPCGAttributeFilteringRangeSettings>(TestData);
	check(Settings);

	const FPCGElementPtr TestElement = TestData.Settings->GetElement();

	Settings->TargetAttribute.SetAttributeName(PCGPointFilterTest::DoubleAttributeName);
	Settings->MinThreshold.bUseConstantThreshold = true;
	Settings->MaxThreshold.bUseConstantThreshold = true;
	Settings->MinThreshold.AttributeTypes.Type = EPCGMetadataTypes::Double;
	Settings->MaxThreshold.AttributeTypes.Type = EPCGMetadataTypes::Double;
	Settings->MinThreshold.bInclusive = true;
	Settings->MaxThreshold.bInclusive = true;

	// Generate 4 point data, each with 1 point that has a normalized value based on its data index: .25, .5, .75, 1.0
	for (int32 I = 0; I < 4; ++I)
	{
		FPCGTaggedData& TaggedData = TestData.InputData.TaggedData.Emplace_GetRef(FPCGTaggedData());
		TaggedData.Pin = PCGPinConstants::DefaultInputLabel;
		TaggedData.Data = PCGPointFilterTest::GenerateAttributeSetWithSingleValue((1.0 + I) / 4.0);
	}

	// Check unfiltered empty data.
	Settings->bGenerateOutputDataEvenIfEmpty = true;

	Settings->MinThreshold.AttributeTypes.DoubleValue = 0.0f;
	Settings->MaxThreshold.AttributeTypes.DoubleValue = 1.0f;
	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}

	// When always generating output, all data should exist in both
	TArray<FPCGTaggedData> InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	TArray<FPCGTaggedData> OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// All, inclusive.
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 4);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 4);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 0);
	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 4);

	Settings->MinThreshold.AttributeTypes.DoubleValue = 0.0f;
	Settings->MaxThreshold.AttributeTypes.DoubleValue = 0.0f;
	Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}

	// When always generating output, all data should exist in both
	InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// None, inclusive.
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 4);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 4);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 4);
	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 0);

	return true;
}

bool FPCGAttributeFilter_Points_NoGenerateEmptyOutput::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData;
	UPCGAttributeFilteringSettings* Settings = PCGTestsCommon::GenerateSettings<UPCGAttributeFilteringSettings>(TestData);
	check(Settings);

	const FPCGElementPtr TestElement = TestData.Settings->GetElement();

	Settings->Operator = EPCGAttributeFilterOperator::Greater;
	Settings->TargetAttribute.SetPointProperty(EPCGPointProperties::Density);
	Settings->bUseConstantThreshold = true;
	Settings->AttributeTypes.Type = EPCGMetadataTypes::Float;

	// Generate 4 point data, each with 1 point that has a normalized density based on its data index: .25, .5, .75, 1.0
	for (int32 I = 0; I < 4; ++I)
	{
		FPCGTaggedData& TaggedData = TestData.InputData.TaggedData.Emplace_GetRef(FPCGTaggedData());
		TaggedData.Pin = PCGPinConstants::DefaultInputLabel;
		TaggedData.Data = PCGPointFilterTest::GeneratePointDataWithSingleDensity((1.0 + I) / 4.0);
	}

	Settings->bGenerateOutputDataEvenIfEmpty = false;

	// All > 0.0
	Settings->AttributeTypes.FloatValue = 0.f;
	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}

	TArray<FPCGTaggedData> InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	TArray<FPCGTaggedData> OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// .25, .5, .75 and 1 > 0
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 4);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 0);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 0);

	Settings->AttributeTypes.FloatValue = 0.333f;
	Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}
	InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// .5, .75, 1 > 0.333
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 3);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 1);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 0);
	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 0);

	Settings->AttributeTypes.FloatValue = 1.f;
	Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}
	InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// None > 1
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 0);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 4);

	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 0);

	return true;
}

bool FPCGAttributeFilter_PointsRange_NoGenerateEmptyOutput::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData;
	UPCGAttributeFilteringRangeSettings* Settings = PCGTestsCommon::GenerateSettings<UPCGAttributeFilteringRangeSettings>(TestData);
	check(Settings);

	const FPCGElementPtr TestElement = TestData.Settings->GetElement();

	Settings->TargetAttribute.SetPointProperty(EPCGPointProperties::Density);
	Settings->MinThreshold.bUseConstantThreshold = true;
	Settings->MaxThreshold.bUseConstantThreshold = true;
	Settings->MinThreshold.AttributeTypes.Type = EPCGMetadataTypes::Float;
	Settings->MaxThreshold.AttributeTypes.Type = EPCGMetadataTypes::Float;
	Settings->MinThreshold.bInclusive = true;
	Settings->MaxThreshold.bInclusive = true;

	// Generate 4 point data, each with 1 point that has a normalized density based on its data index: 0, .25, .5, 0.75
	for (int32 I = 0; I < 4; ++I)
	{
		FPCGTaggedData& TaggedData = TestData.InputData.TaggedData.Emplace_GetRef(FPCGTaggedData());
		TaggedData.Pin = PCGPinConstants::DefaultInputLabel;
		TaggedData.Data = PCGPointFilterTest::GeneratePointDataWithSingleDensity((1.0 + I) / 4.0);
	}

	// Check unfiltered empty data.
	Settings->bGenerateOutputDataEvenIfEmpty = false;

	Settings->MinThreshold.AttributeTypes.FloatValue = 0.0f;
	Settings->MaxThreshold.AttributeTypes.FloatValue = 1.0f;
	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}

	// When always generating output, all data should exist in both
	TArray<FPCGTaggedData> InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	TArray<FPCGTaggedData> OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// All, inclusive
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 4);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 0);

	Settings->MinThreshold.AttributeTypes.FloatValue = 0.2f;
	Settings->MaxThreshold.AttributeTypes.FloatValue = 0.6f;
	Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}
	InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// .2f <= .25, .5 <= .6
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 2);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 2);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 0);
	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 0);

	Settings->MinThreshold.AttributeTypes.FloatValue = 1.0f;
	Settings->MaxThreshold.AttributeTypes.FloatValue = 1.0f;
	Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}
	InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// 1 >= 1, inclusive
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 1);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 3);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 0);
	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 0);

	Settings->MinThreshold.bInclusive = false;
	Settings->MaxThreshold.bInclusive = false;

	Settings->MinThreshold.AttributeTypes.FloatValue = 1.0f;
	Settings->MaxThreshold.AttributeTypes.FloatValue = 1.0f;
	Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}
	InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// None > 1, exclusive.
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 0);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 4);

	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 0);

	return true;
}

bool FPCGAttributeFilter_Attributes_NoGenerateEmptyOutput::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData;
	UPCGAttributeFilteringSettings* Settings = PCGTestsCommon::GenerateSettings<UPCGAttributeFilteringSettings>(TestData);
	check(Settings);

	const FPCGElementPtr TestElement = TestData.Settings->GetElement();

	Settings->Operator = EPCGAttributeFilterOperator::Greater;
	Settings->TargetAttribute.SetAttributeName(PCGPointFilterTest::DoubleAttributeName);
	Settings->AttributeTypes.Type = EPCGMetadataTypes::Double;
	Settings->bUseConstantThreshold = true;

	// Generate 4 point data, each with 1 point that has a normalized value based on its data index: .25, .5, .75, 1.0
	for (int32 I = 0; I < 4; ++I)
	{
		FPCGTaggedData& TaggedData = TestData.InputData.TaggedData.Emplace_GetRef(FPCGTaggedData());
		TaggedData.Pin = PCGPinConstants::DefaultInputLabel;
		TaggedData.Data = PCGPointFilterTest::GenerateAttributeSetWithSingleValue((1.0 + I) / 4.0);
	}

	Settings->bGenerateOutputDataEvenIfEmpty = false;

	Settings->AttributeTypes.DoubleValue = 0.f;
	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}

	// When always generating output, all data should exist in both
	TArray<FPCGTaggedData> InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	TArray<FPCGTaggedData> OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// All
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 4);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 0);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 0);

	Settings->AttributeTypes.DoubleValue = 0.333f;
	Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}
	InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// .5, .75, 1 > .333
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 3);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 1);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 0);
	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 0);

	Settings->AttributeTypes.DoubleValue = 1.f;
	Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}
	InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// None > 1
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 0);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 4);

	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 0);

	return true;
}

bool FPCGAttributeFilter_AttributesRange_NoGenerateEmptyOutput::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData;
	UPCGAttributeFilteringRangeSettings* Settings = PCGTestsCommon::GenerateSettings<UPCGAttributeFilteringRangeSettings>(TestData);
	check(Settings);

	const FPCGElementPtr TestElement = TestData.Settings->GetElement();

	Settings->TargetAttribute.SetAttributeName(PCGPointFilterTest::DoubleAttributeName);
	Settings->MinThreshold.bUseConstantThreshold = true;
	Settings->MaxThreshold.bUseConstantThreshold = true;
	Settings->MinThreshold.AttributeTypes.Type = EPCGMetadataTypes::Double;
	Settings->MaxThreshold.AttributeTypes.Type = EPCGMetadataTypes::Double;
	Settings->MinThreshold.bInclusive = true;
	Settings->MaxThreshold.bInclusive = true;

	// Generate 4 point data, each with 1 point that has a normalized value based on its data index: .25, .5, .75, 1.0
	for (int32 I = 0; I < 4; ++I)
	{
		FPCGTaggedData& TaggedData = TestData.InputData.TaggedData.Emplace_GetRef(FPCGTaggedData());
		TaggedData.Pin = PCGPinConstants::DefaultInputLabel;
		TaggedData.Data = PCGPointFilterTest::GenerateAttributeSetWithSingleValue((1.0 + I) / 4.0);
	}

	// Check unfiltered empty data.
	Settings->bGenerateOutputDataEvenIfEmpty = false;

	Settings->MinThreshold.AttributeTypes.DoubleValue = 0.0f;
	Settings->MaxThreshold.AttributeTypes.DoubleValue = 1.0f;
	TUniquePtr<FPCGContext> Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}

	// When always generating output, all data should exist in both
	TArray<FPCGTaggedData> InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	TArray<FPCGTaggedData> OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// All, inclusive.
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 4);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 0);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 0);

	Settings->MinThreshold.AttributeTypes.DoubleValue = 0.2f;
	Settings->MaxThreshold.AttributeTypes.DoubleValue = 0.6f;
	Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}
	InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// 0.2 < .25, .5 < .6
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 2);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 2);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 0);
	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 0);

	Settings->MinThreshold.AttributeTypes.DoubleValue = 1.0f;
	Settings->MaxThreshold.AttributeTypes.DoubleValue = 1.0f;
	Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}
	InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// 1 >= 1, inclusive.
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 1);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 3);

	UTEST_EQUAL(TEXT("InFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(InFilterOutput), 0);
	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 0);

	Settings->MinThreshold.bInclusive = false;
	Settings->MaxThreshold.bInclusive = false;

	Settings->MinThreshold.AttributeTypes.DoubleValue = 1.0f;
	Settings->MaxThreshold.AttributeTypes.DoubleValue = 1.0f;
	Context = TestData.InitializeTestContext();
	while (!TestElement->Execute(Context.Get())) {}
	InFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::InsideFilterLabel);
	OutFilterOutput = Context->OutputData.GetInputsByPin(PCGPointFilterTest::OutsideFilterLabel);

	// None >= 1, exclusive.
	UTEST_EQUAL(TEXT("InFilter pin has correct number of outputs"), InFilterOutput.Num(), 0);
	UTEST_EQUAL(TEXT("OutFilter pin has correct number of outputs"), OutFilterOutput.Num(), 4);

	UTEST_EQUAL(TEXT("OutFilter data has correct number of empty outputs"), PCGPointFilterTest::GetNumEmptyData(OutFilterOutput), 0);

	return true;
}