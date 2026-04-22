// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "PoseSearch/PoseSearchDerivedDataKey.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Animation/Skeleton.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PoseSearchFeatureChannel)

namespace UE::PoseSearch
{

//////////////////////////////////////////////////////////////////////////
// FFeatureVectorHelper
int32 FFeatureVectorHelper::GetVectorCardinality(EComponentStrippingVector ComponentStrippingVector)
{
	switch (ComponentStrippingVector)
	{
	case EComponentStrippingVector::None:
		return 3;
	case EComponentStrippingVector::StripXY:
		return 1;
	case EComponentStrippingVector::StripZ:
		return 2;
	default:
		checkNoEntry();
		return 0;
	}
}

void FFeatureVectorHelper::EncodeVector(TArrayView<float> Values, int32 DataOffset, const FVector& Vector, EComponentStrippingVector ComponentStrippingVector, bool bNormalize)
{
	check(Values.GetData());

	if (bNormalize)
	{
		FVector NormalizedVector = Vector;
		switch (ComponentStrippingVector)
		{
		case EComponentStrippingVector::None:
			check(Values.Num() >= DataOffset + 2);
			NormalizedVector.Normalize();
			Values[DataOffset + 0] = NormalizedVector.X;
			Values[DataOffset + 1] = NormalizedVector.Y;
			Values[DataOffset + 2] = NormalizedVector.Z;
			break;
		case EComponentStrippingVector::StripXY:
			check(Values.Num() >= DataOffset + 0);			
			NormalizedVector.X = 0.f;
			NormalizedVector.Y = 0.f;
			NormalizedVector.Normalize();
			Values[DataOffset + 0] = NormalizedVector.Z;
			break;
		case EComponentStrippingVector::StripZ:
			check(Values.Num() >= DataOffset + 1);
			NormalizedVector.Z = 0.f;
			NormalizedVector.Normalize();
			Values[DataOffset + 0] = NormalizedVector.X;
			Values[DataOffset + 1] = NormalizedVector.Y;
			break;
		default:
			checkNoEntry();
			break;
		}
	}
	else
	{
		switch (ComponentStrippingVector)
		{
		case EComponentStrippingVector::None:
			check(Values.Num() >= DataOffset + 2);
			Values[DataOffset + 0] = Vector.X;
			Values[DataOffset + 1] = Vector.Y;
			Values[DataOffset + 2] = Vector.Z;
			break;
		case EComponentStrippingVector::StripXY:
			check(Values.Num() >= DataOffset + 0);
			Values[DataOffset + 0] = Vector.Z;
			break;
		case EComponentStrippingVector::StripZ:
			check(Values.Num() >= DataOffset + 1);
			Values[DataOffset + 0] = Vector.X;
			Values[DataOffset + 1] = Vector.Y;
			break;
		default:
			checkNoEntry();
			break;
		}
	}
}

FVector FFeatureVectorHelper::DecodeVector(TConstArrayView<float> Values, int32 DataOffset, EComponentStrippingVector ComponentStrippingVector)
{
	switch (ComponentStrippingVector)
	{
	case EComponentStrippingVector::None:
		check(Values.Num() >= DataOffset + 2);
		return FVector(Values[DataOffset + 0], Values[DataOffset + 1], Values[DataOffset + 2]);
	case EComponentStrippingVector::StripXY:
		check(Values.Num() >= DataOffset + 0);
		return FVector(0, 0, Values[DataOffset + 0]);
	case EComponentStrippingVector::StripZ:
		check(Values.Num() >= DataOffset + 1);
		return FVector(Values[DataOffset + 0], Values[DataOffset + 1], 0);
	default:
		checkNoEntry();
		return FVector::Zero();
	}
}

void FFeatureVectorHelper::EncodeVector2D(TArrayView<float> Values, int32 DataOffset, const FVector2D& Vector2D)
{
	check(Values.Num() >= DataOffset + 1);
	Values[DataOffset + 0] = Vector2D.X;
	Values[DataOffset + 1] = Vector2D.Y;
}

FVector2D FFeatureVectorHelper::DecodeVector2D(TConstArrayView<float> Values, int32 DataOffset)
{
	check(Values.Num() >= DataOffset + 1);
	return FVector2D(Values[DataOffset + 0], Values[DataOffset + 1]);
}

void FFeatureVectorHelper::EncodeFloat(TArrayView<float> Values, int32 DataOffset, const float Value)
{
	check(Values.Num() >= DataOffset + 0);
	Values[DataOffset + 0] = Value;
}

float FFeatureVectorHelper::DecodeFloat(TConstArrayView<float> Values, int32 DataOffset)
{
	check(Values.Num() >= DataOffset + 0);
	return Values[DataOffset];
}

void FFeatureVectorHelper::Copy(TArrayView<float> Values, int32 DataOffset, int32 DataCardinality, TConstArrayView<float> OriginValues)
{
	check(Values.GetData() && OriginValues.GetData());
	check(Values.Num() == OriginValues.Num() && DataOffset + DataCardinality <= Values.Num());

	float* RESTRICT ValuesData = Values.GetData() + DataOffset;
	const float* RESTRICT OriginValuesData = OriginValues.GetData() + DataOffset;

	check(ValuesData != OriginValuesData);

	for (int32 i = 0; i < DataCardinality; ++i, ++ValuesData, ++OriginValuesData)
	{
		*ValuesData = *OriginValuesData;
	}
}

} // namespace UE::PoseSearch

//////////////////////////////////////////////////////////////////////////
// UPoseSearchFeatureChannel
void UPoseSearchFeatureChannel::GetPermutationTimeOffsets(EPermutationTimeType PermutationTimeType, float DesiredPermutationTimeOffset, float& OutPermutationSampleTimeOffset, float& OutPermutationOriginTimeOffset)
{
	switch (PermutationTimeType)
	{
	case EPermutationTimeType::UseSampleTime:
		OutPermutationSampleTimeOffset = 0.f;
		OutPermutationOriginTimeOffset = 0.f;
		break;
	case EPermutationTimeType::UsePermutationTime:
		OutPermutationSampleTimeOffset = DesiredPermutationTimeOffset;
		OutPermutationOriginTimeOffset = DesiredPermutationTimeOffset;
		break;
	case EPermutationTimeType::UseSampleToPermutationTime:
		OutPermutationSampleTimeOffset = DesiredPermutationTimeOffset;
		OutPermutationOriginTimeOffset = 0.f;
		break;
	default:
		checkNoEntry();
		OutPermutationSampleTimeOffset = 0.f;
		OutPermutationOriginTimeOffset = 0.f;
		break;
	}
}

#if WITH_EDITOR
void UPoseSearchFeatureChannel::GetOuterLabel(UE::PoseSearch::TLabelBuilder& LabelBuilder, UE::PoseSearch::ELabelFormat LabelFormat) const
{
	if (LabelFormat != UE::PoseSearch::ELabelFormat::Compact_Horizontal)
	{
		if (const UPoseSearchFeatureChannel* OuterChannel = Cast<UPoseSearchFeatureChannel>(GetOuter()))
		{
			if (LabelBuilder.Len() > 0)
			{
				LabelBuilder.Append(TEXT("_"));
			}
			OuterChannel->GetLabel(LabelBuilder, UE::PoseSearch::ELabelFormat::Full_Horizontal);
		}
	}
}

void UPoseSearchFeatureChannel::AppendLabelSeparator(UE::PoseSearch::TLabelBuilder& LabelBuilder, UE::PoseSearch::ELabelFormat LabelFormat, bool bTryUsingSpace)
{
	if (LabelBuilder.Len() == 0)
	{
		// do nothing
	}
	else if (LabelFormat == UE::PoseSearch::ELabelFormat::Full_Vertical)
	{
		LabelBuilder.Append(TEXT("\n"));
	}
	else if (bTryUsingSpace)
	{
		LabelBuilder.Append(TEXT(" "));
	}
	else
	{
		LabelBuilder.Append(TEXT("_"));
	}
}

UE::PoseSearch::TLabelBuilder& UPoseSearchFeatureChannel::GetLabel(UE::PoseSearch::TLabelBuilder& LabelBuilder, UE::PoseSearch::ELabelFormat LabelFormat) const
{
	GetOuterLabel(LabelBuilder, LabelFormat);
	AppendLabelSeparator(LabelBuilder, LabelFormat);
	LabelBuilder.Append(GetName());
	return LabelBuilder;
}

bool UPoseSearchFeatureChannel::CanBeNormalizedWith(const UPoseSearchFeatureChannel* Other) const
{
	using namespace UE::PoseSearch;

	if (this == Other)
	{
		return true;
	}

	if (ChannelCardinality != Other->ChannelCardinality)
	{
		return false;
	}

	if (GetClass() != Other->GetClass())
	{
		return false;
	}

	if (!GetSchema()->AreSkeletonsCompatible(Other->GetSchema()))
	{
		return false;
	}

	if (!GetNormalizationGroup().IsNone() && GetNormalizationGroup() == Other->GetNormalizationGroup())
	{
		return true;
	}

	TLabelBuilder ThisLabelBuilder, OtherLabelBuilder;
	if (GetLabel(ThisLabelBuilder).ToString() != Other->GetLabel(OtherLabelBuilder).ToString())
	{
		return false;
	}

	return true;
}

const UPoseSearchSchema* UPoseSearchFeatureChannel::GetSchema() const
{
	UObject* Outer = GetOuter();
	while (Outer != nullptr)
	{
		if (const UPoseSearchSchema* Schema = Cast<UPoseSearchSchema>(Outer))
		{
			return Schema;
		}
		Outer = Outer->GetOuter();
	}
	return nullptr;
}

const UE::PoseSearch::FRole UPoseSearchFeatureChannel::GetDefaultRole() const
{
	if (const UPoseSearchSchema* Schema = GetSchema())
	{
		Schema->GetDefaultRole();
	}
	return UE::PoseSearch::DefaultRole;
}

#endif // WITH_EDITOR

USkeleton* UPoseSearchFeatureChannel::GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle)
{
	bInvalidSkeletonIsError = false;
#if WITH_EDITOR
	// blueprint generated classes don't have a schema, until they're instanced by the schema
	if (const UPoseSearchSchema* Schema = GetSchema())
	{
		return Schema->GetSkeleton(GetDefaultRole());
	}
#else
	checkNoEntry();
#endif
	return nullptr;
}


