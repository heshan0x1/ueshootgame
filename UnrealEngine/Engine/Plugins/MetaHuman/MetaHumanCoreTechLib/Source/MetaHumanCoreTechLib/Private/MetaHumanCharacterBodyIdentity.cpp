// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCharacterBodyIdentity.h"
#include "MetaHumanCoreTechLibGlobals.h"

#include "MetaHumanCreatorBodyAPI.h"
#include "MetaHumanCommonDataUtils.h"
#include "terse/archives/binary/InputArchive.h"
#include "terse/archives/binary/OutputArchive.h"
#include <nls/math/Math.h>
#include <rig/BodyGeometry.h>
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "DNAUtils.h"
#include "DNAReaderAdapter.h"
#include "Serialization/JsonSerializer.h"
#include "dna/Reader.h"
#include "MetaHumanRigEvaluatedState.h"

#include <string>
#include <vector>

#include "MetaHumanCharacterIdentity.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MetaHumanCharacterBodyIdentity)

struct FMetaHumanCharacterBodyIdentity::FImpl
{
	void Initialize(std::shared_ptr<const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI> InMHCBodyAPI,
		std::shared_ptr<const TMap<EMetaHumanBodyType, int32>> InBodyTypeLegacyIndexMap,
		const TArray<int32>& InRegionIndice)
	{
		MHCBodyAPI = InMHCBodyAPI;
		BodyTypeLegacyIndexMap = InBodyTypeLegacyIndexMap;
		RegionIndices = InRegionIndice;
		bIsInitialized = true;
	}
	std::shared_ptr<const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI> MHCBodyAPI;
	std::shared_ptr<const TMap<EMetaHumanBodyType, int32>> BodyTypeLegacyIndexMap;
	TArray<int32> RegionIndices;

	bool bIsInitialized = false;
};

struct FMetaHumanCharacterBodyIdentity::FState::FImpl
{
	std::shared_ptr<const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI> MHCBodyAPI;
	std::shared_ptr<const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> MHCBodyState;
	std::shared_ptr<const TMap<EMetaHumanBodyType, int32>> BodyTypeLegacyIndexMap;
	TArray<int32> RegionIndices;
	EMetaHumanBodyType MetaHumanBodyType = EMetaHumanBodyType::BlendableBody;
};

FMetaHumanCharacterBodyIdentity::FMetaHumanCharacterBodyIdentity()
	: Impl(MakePimpl<FImpl>())
{

}

bool FMetaHumanCharacterBodyIdentity::Init(const FString& InModelPath, const FString& InLegacyBodiesPath)
{
#if WITH_EDITORONLY_DATA

	FString BodyPCAModelPath = InModelPath + TEXT("/body_model.dna");
	FString BodySkinModelPath = InModelPath + TEXT("/skin_model.binary");
	FString BodyRBFModelPath = InModelPath + TEXT("/rbf_model.binary");
	TSharedPtr<IDNAReader> PCAModelReader = ReadDNAFromFile(BodyPCAModelPath);
	if (!PCAModelReader)
	{
		UE_LOG(LogMetaHumanCoreTechLib, Error, TEXT("failed to load MHC body model"));
		return false;
	}
	FString CombinedBodyArchetypeFilename = FMetaHumanCommonDataUtils::GetCombinedDNAFilesystemPath();
	TSharedPtr<IDNAReader> CombinedBodyArchetypeReader = ReadDNAFromFile(CombinedBodyArchetypeFilename);

	FString PhysicsBodiesConfigPath = InModelPath + TEXT("/physics_bodies.json");
	FString PhysicsBodiesMaskPath = InModelPath + TEXT("/bodies_mask.json");
	FString SkinningWeightGenerationConfigPath = InModelPath + TEXT("/body_joint_mapping.json");
	FString LodGenerationDataPath = InModelPath + TEXT("/combined_lod_generation.binary");
	FString RegionsLandmarksPath = InModelPath + TEXT("/region_landmarks.json");

	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI> MHCBodyAPI = TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::CreateMHCBodyApi(
		PCAModelReader->Unwrap(), 
		CombinedBodyArchetypeReader->Unwrap(),
		TCHAR_TO_UTF8(*BodyRBFModelPath),
		TCHAR_TO_UTF8(*BodySkinModelPath),
		TCHAR_TO_UTF8(*SkinningWeightGenerationConfigPath),
		TCHAR_TO_UTF8(*LodGenerationDataPath),
		TCHAR_TO_UTF8(*PhysicsBodiesConfigPath),
		TCHAR_TO_UTF8(*PhysicsBodiesMaskPath),
		TCHAR_TO_UTF8(*RegionsLandmarksPath),
		-1);

	TMap<EMetaHumanBodyType, int32> BodyTypeLegacyIndexMap;

	if (!MHCBodyAPI)
	{
		UE_LOG(LogMetaHumanCoreTechLib, Error, TEXT("failed to initialize MHC body API "));
		return false;
	}

	// Get indices of regions used to create gizmos
	TArray<int32> RegionIndices;
	const std::vector<std::string>& RegionNames = MHCBodyAPI->GetRegionNames();
	for (int32 RegionIndex = 0; RegionIndex < RegionNames.size(); ++RegionIndex)
	{
		if (RegionNames[RegionIndex].substr(0, 5) != std::string("joint"))
		{
			RegionIndices.Add(RegionIndex);
		}
	}

	// Add legacy bodies
	if (FPaths::DirectoryExists(InLegacyBodiesPath))
	{
		for (uint8 BodyTypeIndex = 0; BodyTypeIndex < uint8(EMetaHumanBodyType::BlendableBody); BodyTypeIndex++)
		{
			EMetaHumanBodyType BodyType = EMetaHumanBodyType(BodyTypeIndex);
			FString BodyTypeName = StaticEnum<EMetaHumanBodyType>()->GetAuthoredNameStringByValue(static_cast<int64>(BodyTypeIndex));
			FString LegacyCombinedDNAPath = InLegacyBodiesPath / BodyTypeName + TEXT(".dna");

			TSharedPtr<IDNAReader> LegacyCombinedDNAReader = ReadDNAFromFile(LegacyCombinedDNAPath, EDNADataLayer::Geometry);

			std::string StdStringBodyTypeName(TCHAR_TO_UTF8(*BodyTypeName));
			if (LegacyCombinedDNAReader)
			{
				MHCBodyAPI->AddLegacyBody(LegacyCombinedDNAReader->Unwrap(), StdStringBodyTypeName);
				BodyTypeLegacyIndexMap.Add(BodyType, MHCBodyAPI->NumLegacyBodies() - 1);
			}
			else
			{
				UE_LOG(LogMetaHumanCoreTechLib, Error, TEXT("failed to initialize MHC legacy body type %s"), *BodyTypeName);
			}
		}
	}

	std::shared_ptr<const TMap<EMetaHumanBodyType, int32>> BodyTypeMap = std::make_shared<const TMap<EMetaHumanBodyType, int32>>(BodyTypeLegacyIndexMap);
	std::shared_ptr<const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI> ConstBodyAPI = 
		std::static_pointer_cast<const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI>(MHCBodyAPI);
	Impl->Initialize(ConstBodyAPI, BodyTypeMap, RegionIndices);

	return true;
#else
	UE_LOG(LogMetaHumanCoreTechLib, Error, TEXT("body shape editor API only works with EditorOnly Data "));
	return false;
#endif
}

FMetaHumanCharacterBodyIdentity::~FMetaHumanCharacterBodyIdentity() = default;

TSharedPtr<FMetaHumanCharacterBodyIdentity::FState> FMetaHumanCharacterBodyIdentity::CreateState() const
{
	if (!Impl->bIsInitialized) return nullptr;

	TSharedPtr<FState, ESPMode::ThreadSafe> State = MakeShared<FState>();
	State->Impl->MHCBodyState = Impl->MHCBodyAPI->CreateState();
	State->Impl->MHCBodyAPI = Impl->MHCBodyAPI;
	State->Impl->BodyTypeLegacyIndexMap = Impl->BodyTypeLegacyIndexMap;
	State->Impl->RegionIndices = Impl->RegionIndices;

	return State;
}

FMetaHumanCharacterBodyIdentity::FState::FState()
{
	Impl = MakePimpl<FImpl>();
}

FMetaHumanCharacterBodyIdentity::FState::~FState() = default;

FMetaHumanCharacterBodyIdentity::FState::FState(const FState& InOther)
{
	Impl = MakePimpl<FImpl>(*InOther.Impl);
}

TArray<FMetaHumanCharacterBodyConstraint> FMetaHumanCharacterBodyIdentity::FState::GetBodyConstraints(bool bScaleMeasurementRangesWithHeight) const
{
	check(Impl->MHCBodyAPI);
	check(Impl->MHCBodyState);

	TArray<FMetaHumanCharacterBodyConstraint> BodyConstraints;

	int ConstraintsNum = Impl->MHCBodyState->GetConstraintNum();
	BodyConstraints.AddUninitialized( StaticCast<int32>(ConstraintsNum));
	av::ConstArrayView<float> Measurements =  Impl->MHCBodyState->GetMeasurements();

	std::vector<float> MinValues;
	std::vector<float> MaxValues;
	MinValues.resize(ConstraintsNum);
	MaxValues.resize(ConstraintsNum);
	Impl->MHCBodyAPI->EvaluateConstraintRange(*(Impl->MHCBodyState), MinValues, MaxValues, bScaleMeasurementRangesWithHeight);

	for (int ConstraintIndex = 0; ConstraintIndex < ConstraintsNum; ConstraintIndex++)
	{
		FMetaHumanCharacterBodyConstraint BodyConstraint;
		std::string StdConstraintName(Impl->MHCBodyState->GetConstraintName(ConstraintIndex));
		BodyConstraint.Name = UTF8_TO_TCHAR(StdConstraintName.c_str());

		float TargetMeasurement = 0.f;
		bool bIsActive = Impl->MHCBodyState->GetConstraintTarget(ConstraintIndex, TargetMeasurement);
		BodyConstraint.bIsActive = bIsActive;

		if (bIsActive)
		{
			BodyConstraint.TargetMeasurement = TargetMeasurement;
		}
		else
		{
			BodyConstraint.TargetMeasurement = Measurements[ConstraintIndex];
		}

		BodyConstraint.MinMeasurement = MinValues[ConstraintIndex];
		BodyConstraint.MaxMeasurement = MaxValues[ConstraintIndex];
		BodyConstraints[ConstraintIndex] = BodyConstraint;
	}

	return BodyConstraints;
}

void FMetaHumanCharacterBodyIdentity::FState::EvaluateBodyConstraints(const TArray<FMetaHumanCharacterBodyConstraint>& BodyConstraints)
{
	check(Impl->MHCBodyAPI);
	check(Impl->MHCBodyState);

	TArray<FVector3f> Out;

	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState;
	NewBodyShapeState = Impl->MHCBodyState->Clone();

	for (int32 ConstraintIndex = 0; ConstraintIndex < BodyConstraints.Num(); ConstraintIndex++)
	{
		if (BodyConstraints[ConstraintIndex].bIsActive)
		{
			NewBodyShapeState->SetConstraintTarget(StaticCast<int>(ConstraintIndex), BodyConstraints[ConstraintIndex].TargetMeasurement);
		}
		else
		{
			NewBodyShapeState->RemoveConstraintTarget(StaticCast<int>(ConstraintIndex));
		}
	}
	
	Impl->MHCBodyAPI->Evaluate(*NewBodyShapeState);
	Impl->MHCBodyState = NewBodyShapeState;

}

FMetaHumanRigEvaluatedState FMetaHumanCharacterBodyIdentity::FState::GetVerticesAndVertexNormals() const
{
	check(Impl->MHCBodyState);

	FMetaHumanRigEvaluatedState Out;

	int32 NumVertices = 0;
	for (int32 Lod = 0; Lod < Impl->MHCBodyAPI->NumLODs(); ++Lod)
	{
		NumVertices += Impl->MHCBodyState->GetMesh(Lod).size() / 3;
	}
	Out.Vertices.AddUninitialized(NumVertices);
	Out.VertexNormals.AddUninitialized(NumVertices);

	// concatenate the vertices from all lods
	float* VerticesDataPtr = (float*)(Out.Vertices.GetData());
	float* VertexNormalsDataPtr = (float*)(Out.VertexNormals.GetData());
	size_t Count = 0;
	for (int Lod = 0; Lod < Impl->MHCBodyAPI->NumLODs(); ++Lod) 
	{
		av::ConstArrayView<float> CurMesh = Impl->MHCBodyState->GetMesh(Lod);
		FMemory::Memcpy(VerticesDataPtr, CurMesh.data(), CurMesh.size() * sizeof(float));
		VerticesDataPtr += CurMesh.size();
		av::ConstArrayView<float> CurMeshVertexNormals = Impl->MHCBodyState->GetMeshNormals(Lod);
		FMemory::Memcpy(VertexNormalsDataPtr, CurMeshVertexNormals.data(), CurMeshVertexNormals.size() * sizeof(float));
		VertexNormalsDataPtr += CurMeshVertexNormals.size();
	}

	return Out;
}

TArray<int32> FMetaHumanCharacterBodyIdentity::FState::GetNumVerticesPerLOD() const
{
	TArray<int32> NumVerticesPerLOD;
	check(Impl->MHCBodyAPI);
	NumVerticesPerLOD.SetNumUninitialized(Impl->MHCBodyAPI->NumLODs());

	for (int32 Lod = 0; Lod < Impl->MHCBodyAPI->NumLODs(); ++Lod)
	{
		NumVerticesPerLOD[Lod] = Impl->MHCBodyState->GetMesh(Lod).size() / 3;
	}

	return NumVerticesPerLOD;
}


FVector3f FMetaHumanCharacterBodyIdentity::FState::GetVertex(const TArray<FVector3f>& InVertices, int32 InDNAMeshIndex, int32 InDNAVertexIndex) const
{
	check(Impl->MHCBodyAPI);

	float Out[3];
	const float* DataPtr = (const float*)(InVertices.GetData());

	for (int32 Lod = 0; Lod < InDNAMeshIndex; ++Lod)
	{
		DataPtr += Impl->MHCBodyState->GetMesh(Lod).size();
	}
	ensure(Impl->MHCBodyAPI->GetVertex(InDNAMeshIndex, DataPtr, InDNAVertexIndex, Out));
	return FVector3f{ Out[0], Out[2], Out[1] };
}

TArray<FVector3f> FMetaHumanCharacterBodyIdentity::FState::GetRegionGizmos() const
{
	TArray<FVector3f> Out;
	Out.AddUninitialized(Impl->MHCBodyAPI->NumGizmos());
	ensure(Impl->MHCBodyAPI->EvaluateGizmos(*(Impl->MHCBodyState), (float*)Out.GetData()));

	for (int32 I = 0; I < Out.Num(); ++I)
	{
		Out[I] = FVector3f{ Out[I].X, Out[I].Z, Out[I].Y };
	}

	return Out;
}

static coretechlib::titan::api::MetaHumanCreatorBodyAPI::BodyAttribute UEBodyBlendOptionsToTitanBodyAttribute(EBodyBlendOptions InBodyBlendOptions)
{
	switch (InBodyBlendOptions)
	{
	case EBodyBlendOptions::Skeleton:
			return coretechlib::titan::api::MetaHumanCreatorBodyAPI::BodyAttribute::Skeleton;
	case EBodyBlendOptions::Shape:
		return coretechlib::titan::api::MetaHumanCreatorBodyAPI::BodyAttribute::Shape;
	case EBodyBlendOptions::Both:
		return coretechlib::titan::api::MetaHumanCreatorBodyAPI::BodyAttribute::Both;
	default:
		check(false);
	}

	return coretechlib::titan::api::MetaHumanCreatorBodyAPI::BodyAttribute::Both;
}

void FMetaHumanCharacterBodyIdentity::FState::BlendPresets(int32 InGizmoIndex, const TArray<TPair<float, const FState*>>& InStates, EBodyBlendOptions InBodyBlendOptions)
{
	check(Impl->MHCBodyState);

	if (InStates.Num() > 0)
	{
		std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyState->Clone();

		std::vector<std::pair<float, const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State*>> InnerStates;
		for (int32 PresetIndex = 0; PresetIndex < InStates.Num(); PresetIndex++)
		{
			std::pair<float, const TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State*> Preset { InStates[PresetIndex].Key, InStates[PresetIndex].Value->Impl->MHCBodyState.get() };
			InnerStates.emplace_back(Preset);
		}
		int32 RegionIndex = (InGizmoIndex == INDEX_NONE) ? -1 : Impl->RegionIndices[InGizmoIndex]; 
		ensure(Impl->MHCBodyAPI->Blend(*NewBodyShapeState, RegionIndex, InnerStates,UEBodyBlendOptionsToTitanBodyAttribute(InBodyBlendOptions)));
		Impl->MHCBodyState = NewBodyShapeState;
	}
}

int32 FMetaHumanCharacterBodyIdentity::FState::GetNumberOfConstraints() const
{
	check(Impl->MHCBodyState);

	return StaticCast<int32>(Impl->MHCBodyState->GetConstraintNum());
}

float FMetaHumanCharacterBodyIdentity::FState::GetMeasurement(int32 ConstraintIndex) const
{
	check(Impl->MHCBodyState);

	return Impl->MHCBodyState->GetMeasurements()[ConstraintIndex];
}

void FMetaHumanCharacterBodyIdentity::FState::GetMeasurementsForFaceAndBody(TSharedRef<IDNAReader> InFaceDNA, TSharedRef<IDNAReader> InBodyDNA, TMap<FString, float>& OutMeasurements) const
{
	auto GetVerticesFromDNA = [](TSharedRef<IDNAReader> InDNA, uint16 InMeshIndex)
	{
		const uint32 VertexCount = InDNA->GetVertexPositionCount(InMeshIndex);
		TConstArrayView<float> Xs = InDNA->GetVertexPositionXs(InMeshIndex);
		TConstArrayView<float> Ys = InDNA->GetVertexPositionYs(InMeshIndex);
		TConstArrayView<float> Zs = InDNA->GetVertexPositionZs(InMeshIndex);

		// API expects Y up, but DNA reader swaps Y and Z, so we need to swap coordinates back
		Eigen::Matrix3Xf Result(3, VertexCount);
		Result.row(0) = Eigen::Map<const Eigen::RowVectorXf>(Xs.GetData(), Xs.Num());
		Result.row(1) = Eigen::Map<const Eigen::RowVectorXf>(Zs.GetData(), Zs.Num());
		Result.row(2) = Eigen::Map<const Eigen::RowVectorXf>(Ys.GetData(), Ys.Num());

		return Result;
	};

	constexpr uint16 MeshIndex = 0;
	const Eigen::Matrix3Xf FaceVertices = GetVerticesFromDNA(InFaceDNA, MeshIndex);
	const Eigen::Matrix3Xf BodyVertices = GetVerticesFromDNA(InBodyDNA, MeshIndex);

	Eigen::VectorXf Measurements;
	std::vector<std::string> MeasurementNames;

	Impl->MHCBodyAPI->GetMeasurements(FaceVertices, BodyVertices, Measurements, MeasurementNames);
	check(Measurements.size() == MeasurementNames.size());

	OutMeasurements.Reserve(static_cast<int32>(MeasurementNames.size()));

	for (size_t i = 0; i < MeasurementNames.size(); ++i)
	{
		OutMeasurements.Emplace(UTF8_TO_TCHAR(MeasurementNames[i].c_str()), Measurements[i]);
	}
}

void FMetaHumanCharacterBodyIdentity::FState::GetMeasurementsForFaceAndBody(const TArray<FVector3f>& InFaceRawVertices, TMap<FString, float>& OutMeasurements) const
{
	check(Impl->MHCBodyState);

	constexpr uint16 MeshIndex = 0;
	av::ConstArrayView<float> CombinedMesh = Impl->MHCBodyState->GetMesh(MeshIndex);
	Eigen::Matrix<float, 3, -1> CombinedVertices = Eigen::Map<const Eigen::Matrix<float, 3, -1>>((const float*)CombinedMesh.data(), 3, CombinedMesh.size() / 3u);

	if (InFaceRawVertices.Num() > 0 && InFaceRawVertices.Num() <= CombinedVertices.cols())
	{
		const Eigen::Map<const Eigen::Matrix<float, 3, -1>> FaceVertices((const float*)InFaceRawVertices.GetData(), 3, InFaceRawVertices.Num());
		CombinedVertices.leftCols(FaceVertices.cols()) = FaceVertices;
	}
	Eigen::VectorXf Measurements;
	std::vector<std::string> MeasurementNames;
	Impl->MHCBodyAPI->GetMeasurements(CombinedVertices, Measurements, MeasurementNames);
	check(Measurements.size() == MeasurementNames.size());
	OutMeasurements.Reserve(static_cast<int32>(MeasurementNames.size()));
	for (size_t i = 0; i < MeasurementNames.size(); ++i)
	{
		OutMeasurements.Emplace(UTF8_TO_TCHAR(MeasurementNames[i].c_str()), Measurements[i]);
	}
}

TArray<FVector> FMetaHumanCharacterBodyIdentity::FState::GetContourVertices(int32 ConstraintIndex) const
{
	check(Impl->MHCBodyState);

	TArray<FVector> Out;

	const Eigen::Matrix3Xf ContourVertices = Impl->MHCBodyState->GetContourVertices(ConstraintIndex);;

	for (int32 ContourValueIndex = 0; ContourValueIndex < (int32)ContourVertices.cols(); ContourValueIndex++)
	{
		Out.Add({ ContourVertices(0, ContourValueIndex), ContourVertices(2, ContourValueIndex), ContourVertices(1, ContourValueIndex) });
	}
	
	return Out;
}

TArray<FMatrix44f> FMetaHumanCharacterBodyIdentity::FState::CopyBindPose() const
{
	check(Impl->MHCBodyState);

	TArray<FMatrix44f> Out;

	av::ConstArrayView<float> BindPose = Impl->MHCBodyState->GetBindPose();
	Out.AddUninitialized(BindPose.size() / 16);
	FMemory::Memcpy((float*)Out.GetData(), BindPose.data(), BindPose.size()*sizeof(float));

	return Out;
}

int32 FMetaHumanCharacterBodyIdentity::FState::GetNumberOfJoints() const
{
	check(Impl->MHCBodyAPI);

	return Impl->MHCBodyAPI->NumJoints();
}

void FMetaHumanCharacterBodyIdentity::FState::GetNeutralJointTransform(int32 JointIndex, FVector3f& OutJointTranslation, FRotator3f& OutJointRotation) const
{
	check(Impl->MHCBodyState);
	check(JointIndex == FMath::Clamp(JointIndex, 0, MAX_uint16));

	Eigen::Vector3f Translation;
	Eigen::Vector3f Rotation;
	Impl->MHCBodyAPI->GetNeutralJointTransform(*Impl->MHCBodyState, static_cast<uint16>(JointIndex), Translation, Rotation);

	OutJointTranslation = FVector3f(Translation.x(), Translation.y(), Translation.z());
	OutJointRotation = FRotator3f(Rotation.x(), Rotation.y(), Rotation.z());
}

void FMetaHumanCharacterBodyIdentity::FState::CopyCombinedModelVertexInfluenceWeights(TArray<TPair<int32, TArray<FFloatTriplet>>>& OutCombinedModelVertexInfluenceWeights) const
{
	check(Impl->MHCBodyState);

	std::vector<TITAN_NAMESPACE::SparseMatrix<float>> VertexInfluenceWeights;

	Impl->MHCBodyAPI->GetVertexInfluenceWeights(*Impl->MHCBodyState, VertexInfluenceWeights);
	OutCombinedModelVertexInfluenceWeights.SetNum(int32(VertexInfluenceWeights.size()));

	for (int32 Lod = 0; Lod < int32(VertexInfluenceWeights.size()); ++Lod)
	{
		OutCombinedModelVertexInfluenceWeights[Lod] = TPair<int32, TArray<FFloatTriplet>>(int32(VertexInfluenceWeights[size_t(Lod)].rows()), TArray<FFloatTriplet>());

		for (int k = 0; k < VertexInfluenceWeights[size_t(Lod)].outerSize(); ++k)
		{
			for (TITAN_NAMESPACE::SparseMatrix<float>::InnerIterator it(VertexInfluenceWeights[size_t(Lod)], k); it; ++it) 
			{
				OutCombinedModelVertexInfluenceWeights[Lod].Value.Add(FFloatTriplet(it.row(), it.col(), it.value()));
			}
		}
	}
}

void FMetaHumanCharacterBodyIdentity::FState::SetCombinedModelVertexInfluenceWeightsLOD0(TArray<FFloatTriplet> InCombinedModelVertexInfluenceWeightsLOD0) 
{
	check(Impl->MHCBodyState);
	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyState->Clone();
	int32 MaxRow = 0, MaxCol = 0;

	std::vector<Eigen::Triplet<float>> Triplets;
	Triplets.reserve(InCombinedModelVertexInfluenceWeightsLOD0.Num());

	for (const FFloatTriplet& T : InCombinedModelVertexInfluenceWeightsLOD0)
	{
		MaxRow = FMath::Max(MaxRow, T.Row);
		MaxCol = FMath::Max(MaxCol, T.Col);
		Triplets.emplace_back(T.Row, T.Col, T.Value);
	}

	TITAN_NAMESPACE::SparseMatrix<float> SparseMat(MaxRow + 1, MaxCol + 1);
	SparseMat.setFromTriplets(Triplets.begin(), Triplets.end());
	NewBodyShapeState->SetCustomVertexInfluenceWeightsLOD0(SparseMat);
	Impl->MHCBodyState = NewBodyShapeState;
}

void FMetaHumanCharacterBodyIdentity::FState::GetCombinedModelVertexInfluenceWeightsLOD0(TArray<FFloatTriplet> & OutCombinedModelVertexInfluenceWeightsLOD0) const
{
	check(Impl->MHCBodyState);
	std::vector<TITAN_NAMESPACE::SparseMatrix<float>> VertexInfluenceWeights;
	Impl->MHCBodyAPI->GetVertexInfluenceWeights(*Impl->MHCBodyState, VertexInfluenceWeights);
	OutCombinedModelVertexInfluenceWeightsLOD0.Reset();
	OutCombinedModelVertexInfluenceWeightsLOD0.Reserve(VertexInfluenceWeights.size());

	for (int k = 0; k < VertexInfluenceWeights[size_t(0)].outerSize(); ++k)
	{
		for (TITAN_NAMESPACE::SparseMatrix<float>::InnerIterator it(VertexInfluenceWeights[0], k); it; ++it) 
		{
			OutCombinedModelVertexInfluenceWeightsLOD0.Add(FFloatTriplet(it.row(), it.col(), it.value()));
		}
	}
}

void FMetaHumanCharacterBodyIdentity::FState::Reset()
{
	check(Impl->MHCBodyState);

	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyAPI->CreateState();
	Impl->MHCBodyState = NewBodyShapeState;
	Impl->MetaHumanBodyType = EMetaHumanBodyType::BlendableBody;
}

EMetaHumanBodyType FMetaHumanCharacterBodyIdentity::FState::GetMetaHumanBodyType() const
{
	return Impl->MetaHumanBodyType;
}

void FMetaHumanCharacterBodyIdentity::FState::SetMetaHumanBodyType(EMetaHumanBodyType InMetaHumanBodyType, bool bFitFromLegacy)
{
	EMetaHumanBodyType PreviousBodyType = Impl->MetaHumanBodyType;
	Impl->MetaHumanBodyType = InMetaHumanBodyType;

	if (InMetaHumanBodyType != EMetaHumanBodyType::BlendableBody)
	{
		if (const int32* LegacyBodyTypeIndex = Impl->BodyTypeLegacyIndexMap->Find(InMetaHumanBodyType))
		{
			std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyState->Clone();
			Impl->MHCBodyAPI->SelectLegacyBody(*NewBodyShapeState, static_cast<int>(*LegacyBodyTypeIndex), false);
			Impl->MHCBodyState = NewBodyShapeState;
		}
		else
		{
			FString BodyTypeName;
			UEnum::GetValueAsString(InMetaHumanBodyType).Split("::", nullptr, &BodyTypeName);
			UE_LOG(LogMetaHumanCoreTechLib, Warning, TEXT("failed to find legacy dna body type %s"), *BodyTypeName);
		}
	}
	else if (InMetaHumanBodyType == EMetaHumanBodyType::BlendableBody && bFitFromLegacy)
	{
		if (const int32* LegacyBodyTypeIndex = Impl->BodyTypeLegacyIndexMap->Find(PreviousBodyType))
		{
			std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyState->Clone();
			Impl->MHCBodyAPI->SelectLegacyBody(*NewBodyShapeState, static_cast<int>(*LegacyBodyTypeIndex), true);
			Impl->MHCBodyState = NewBodyShapeState;
		}
	}
}

#if WITH_EDITORONLY_DATA
bool FMetaHumanCharacterBodyIdentity::FState::FitToBodyDna(TSharedRef<class IDNAReader> InBodyDna, EMetaHumanCharacterBodyFitOptions InBodyFitOptions)
{
	check(Impl->MHCBodyState);

	TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::FitToTargetOptions FitToTargetOptions;
	FitToTargetOptions.enforceAnatomicalPose = false;

	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState = Impl->MHCBodyState->Clone();

	if (!Impl->MHCBodyAPI->FitToTarget(*NewBodyState, FitToTargetOptions, InBodyDna->Unwrap()))
	{
		return false;
	}
	if (InBodyFitOptions == EMetaHumanCharacterBodyFitOptions::FitFromMeshAndSkeleton)
	{
		Eigen::Matrix<float, 3, -1> JointsEigen;
		JointsEigen.conservativeResize(3, InBodyDna->GetJointCount());
		for (int32 I = 0; I < JointsEigen.cols(); ++I)
		{
			auto dnaJoint= InBodyDna->Unwrap()->getNeutralJointTranslation(I); 
			JointsEigen(0, I) = dnaJoint.x;
			JointsEigen(1, I) = dnaJoint.y;
			JointsEigen(2, I) = dnaJoint.z;
		}
		Impl->MHCBodyAPI->SetNeutralJointsTranslations(*NewBodyState, JointsEigen);
	}
	else if (InBodyFitOptions == EMetaHumanCharacterBodyFitOptions::FitFromMeshToFixedSkeleton)
	{
		auto JointsFromState =  Impl->MHCBodyState->GetBindPose();
		const Eigen::Map<const Eigen::Matrix<float, 3, -1>> JointsEigen((const float*)JointsFromState.data(), 3, JointsFromState.size() / 3u);
		Impl->MHCBodyAPI->SetNeutralJointsTranslations(*NewBodyState, JointsEigen);
	}

	Impl->MHCBodyAPI->SetVertexDeltaScale(*NewBodyState, 1.0f);
	Impl->MHCBodyState = NewBodyState;

	return true;
}

bool FMetaHumanCharacterBodyIdentity::FState::FitToTarget(const TArray<FVector3f>& InVertices, const TArray<FVector3f>& InComponentJointTranslations, EMetaHumanCharacterBodyFitOptions InBodyFitOptions)
{
	check(Impl->MHCBodyState);

	TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::FitToTargetOptions FitToTargetOptions;
	FitToTargetOptions.enforceAnatomicalPose = false;

	TArray<FVector3f> VerticesDNASpace;
	VerticesDNASpace.AddUninitialized(InVertices.Num());
	for (int32 I = 0; I < InVertices.Num(); ++I)
	{
		VerticesDNASpace[I] = FVector3f{ InVertices[I].X, InVertices[I].Z, InVertices[I].Y };
	}
	const Eigen::Map<const Eigen::Matrix<float, 3, -1>> VerticesEigen((const float*)VerticesDNASpace.GetData(), 3, VerticesDNASpace.Num());
	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState = Impl->MHCBodyState->Clone();
	if (!Impl->MHCBodyAPI->FitToTarget(*NewBodyState, FitToTargetOptions, VerticesEigen))
	{
		return false;
	}
	if (InBodyFitOptions == EMetaHumanCharacterBodyFitOptions::FitFromMeshAndSkeleton)
	{
		TArray<FVector3f> JointTranslationsDNASpace;
		JointTranslationsDNASpace.AddUninitialized(InComponentJointTranslations.Num());
		for (int32 I = 0; I < InComponentJointTranslations.Num(); ++I)
		{
			JointTranslationsDNASpace[I] = FVector3f{ InComponentJointTranslations[I].X, InComponentJointTranslations[I].Z, InComponentJointTranslations[I].Y };
		}
		const Eigen::Map<const Eigen::Matrix<float, 3, -1>> JointsEigen((const float*)JointTranslationsDNASpace.GetData(), 3, JointTranslationsDNASpace.Num());
		Impl->MHCBodyAPI->SetNeutralJointsTranslations(*NewBodyState, JointsEigen);
	}
	else if (InBodyFitOptions == EMetaHumanCharacterBodyFitOptions::FitFromMeshToFixedSkeleton)
	{
		auto JointsFromState =  Impl->MHCBodyState->GetBindPose();
		const Eigen::Map<const Eigen::Matrix<float, 3, -1>> JointsEigen((const float*)JointsFromState.data(), 3, JointsFromState.size() / 3u);
		Impl->MHCBodyAPI->SetNeutralJointsTranslations(*NewBodyState, JointsEigen);
	}
	Impl->MHCBodyAPI->SetVertexDeltaScale(*NewBodyState, 1.0f);
	Impl->MHCBodyState = NewBodyState;

	return true;
}
#endif // WITH_EDITORONLY_DATA


bool FMetaHumanCharacterBodyIdentity::FState::Conform(const TArray<FVector3f>& InVertices, const TArray<FVector3f>& InJointRotations, bool bTargetIsInAPose, bool  bEstimateJointsFromMesh)
{
	TArray<FVector3f> VerticesDNASpace;
	VerticesDNASpace.AddUninitialized(InVertices.Num());
	for (int32 I = 0; I < InVertices.Num(); ++I)
	{
		VerticesDNASpace[I] = FVector3f{ InVertices[I].X, InVertices[I].Z, InVertices[I].Y };
	}
	const Eigen::Map<const Eigen::Matrix<float, 3, -1>> VerticesEigen((const float*)VerticesDNASpace.GetData(), 3, VerticesDNASpace.Num());
	TArray<float> JointRotationsView;
	if (InJointRotations.Num() > 0)
	{
		JointRotationsView.SetNumUninitialized(InJointRotations.Num() * 3);
		for (int32 i = 0; i < InJointRotations.Num(); ++i)
		{
			JointRotationsView[i * 3 + 0] = InJointRotations[i].X;
			JointRotationsView[i * 3 + 1] = -InJointRotations[i].Y;
			JointRotationsView[i * 3 + 2] = -InJointRotations[i].Z;
		}	
	}
	TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::FitToTargetOptions options;
	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState = Impl->MHCBodyState->Clone();
	options.isAPose = bTargetIsInAPose;
	NewBodyState->SetApplyFloorOffset(true);
	if (!Impl->MHCBodyAPI->FitToTarget(*NewBodyState, options, VerticesEigen, {JointRotationsView.GetData(), static_cast<std::size_t>(JointRotationsView.Num())}))
	{
		return false;
	}
	if (bEstimateJointsFromMesh)
	{
		Impl->MHCBodyAPI->VolumetricallyFitHandAndFeetJoints(*NewBodyState);
	}
	Impl->MHCBodyState = NewBodyState;
	return true;
}
	
TArray<FVector3f> FMetaHumanCharacterBodyIdentity::FState::GetJointTranslations() const
{
	auto extractTranslations = [](const av::ConstArrayView<float>& view) -> Eigen::Matrix<float, 3, Eigen::Dynamic> {
			using Matrix4f = Eigen::Matrix<float, 4, 4>; 
			constexpr int floats_per_transform = 16;

			size_t num_transforms = view.size() / floats_per_transform;
			Eigen::Matrix<float, 3, Eigen::Dynamic> translations(3, num_transforms);

			for (size_t i = 0; i < num_transforms; ++i) {
				const float* base_ptr = view.data() + i * floats_per_transform;
				Eigen::Map<const Matrix4f> mat(base_ptr);
				translations.col(i) = mat.block<3, 1>(0, 3);
			}
			return translations;
	};
	Eigen::Matrix<float, 3, -1> BindPose = extractTranslations(Impl->MHCBodyState->GetBindPose());
	TArray<FVector3f> JointTranslationsUESpace;
	JointTranslationsUESpace.AddUninitialized(BindPose.cols());
	for (int32 I = 0; I < BindPose.cols(); ++I)
	{
		JointTranslationsUESpace[I] = FVector3f{ BindPose(0, I), BindPose(1, I),  BindPose(2,I)};
	}
	return JointTranslationsUESpace;
}

bool FMetaHumanCharacterBodyIdentity::FState::SetJointTranslations(const TArray<FVector3f>& InComponentJointTranslations, bool bImportHelperJoints)
{
	if (InComponentJointTranslations.Num() != Impl->MHCBodyAPI->NumJoints())
	{
		return false;
	}
	auto extractTranslations = [](const av::ConstArrayView<float>& view) -> Eigen::Matrix<float, 3, Eigen::Dynamic> {
			using Matrix4f = Eigen::Matrix<float, 4, 4>; 
			constexpr int floats_per_transform = 16;

			size_t num_transforms = view.size() / floats_per_transform;
			Eigen::Matrix<float, 3, Eigen::Dynamic> translations(3, num_transforms);

			for (size_t i = 0; i < num_transforms; ++i) {
				const float* base_ptr = view.data() + i * floats_per_transform;
				Eigen::Map<const Matrix4f> mat(base_ptr);
				translations.col(i) = mat.block<3, 1>(0, 3);
			}
			return translations;
	};
	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState = Impl->MHCBodyState->Clone();
	Eigen::Matrix<float, 3, -1> bindPose = extractTranslations(NewBodyState->GetBindPose());
	TArray<FVector3f> JointTranslationsDNASpace;
	// Fill joint translations if fitting from skeleton
	JointTranslationsDNASpace.AddUninitialized(InComponentJointTranslations.Num());
	for (int32 I = 0; I < InComponentJointTranslations.Num(); ++I)
	{
		JointTranslationsDNASpace[I] = FVector3f{ InComponentJointTranslations[I].X, InComponentJointTranslations[I].Z, InComponentJointTranslations[I].Y };
	}
	const Eigen::Map<const Eigen::Matrix<float, 3, -1>> JointsEigen((const float*)JointTranslationsDNASpace.GetData(), 3, JointTranslationsDNASpace.Num());
	if(JointTranslationsDNASpace.Num() != bindPose.cols())
	{
		return false;
	} 
	for(int ji : Impl->MHCBodyAPI->CoreJoints())
	{
		bindPose.col(ji) = JointsEigen.col(ji);
	}
	if(bImportHelperJoints)
	{
		for(int ji : Impl->MHCBodyAPI->HelperJoints())
		{
			bindPose.col(ji) = JointsEigen.col(ji);
		}
	}
	Impl->MHCBodyAPI->SetNeutralJointsTranslations(*NewBodyState, bindPose);
	Impl->MHCBodyState = NewBodyState;
	return true;
}

bool FMetaHumanCharacterBodyIdentity::FState::SetJointRotations(const TArray<FVector3f>& InJointRotations, bool bImportHelperJoints)
{
	if (InJointRotations.Num() != Impl->MHCBodyAPI->NumJoints())
	{
		return false;
	}

	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState = Impl->MHCBodyState->Clone();
	TArray<float> JointRotationsView;
	JointRotationsView.SetNumUninitialized(InJointRotations.Num() * 3);
	for (int32 i = 0; i < InJointRotations.Num(); ++i)
	{
		Eigen::Vector3f JointRotation;
		Eigen::Vector3f JointTranslation;
		constexpr float ToRadians = PI / 180.0f;	
		Impl->MHCBodyAPI->GetNeutralJointTransform(*NewBodyState, static_cast<uint16_t>(i), JointTranslation, JointRotation);
		JointRotation *= ToRadians;
		JointRotationsView[i * 3 + 0] = JointRotation(0);
		JointRotationsView[i * 3 + 1] = JointRotation(1);
		JointRotationsView[i * 3 + 2] = JointRotation(2);
	
	}	
	for (int i : Impl->MHCBodyAPI->CoreJoints())
	{
		if (i == 0)
		{
			//This is to account for baked dna root rotation in skel mesh
			continue;
		}
		JointRotationsView[i * 3 + 0] = InJointRotations[i].X;
		JointRotationsView[i * 3 + 1] = -InJointRotations[i].Y;
		JointRotationsView[i * 3 + 2] = -InJointRotations[i].Z;	
	}
	if ( bImportHelperJoints )
	{
		for (int i : Impl->MHCBodyAPI->HelperJoints())
		{
			JointRotationsView[i * 3 + 0] = InJointRotations[i].X;  	
			JointRotationsView[i * 3 + 1] = -InJointRotations[i].Y;
			JointRotationsView[i * 3 + 2] = -InJointRotations[i].Z;
		}
	}
	Impl->MHCBodyAPI->SetNeutralJointRotations(*NewBodyState, {JointRotationsView.GetData(), static_cast<std::size_t>(JointRotationsView.Num())});
	Impl->MHCBodyState = NewBodyState;
	return true;
}

bool FMetaHumanCharacterBodyIdentity::FState::SetMesh(const TArray<FVector3f>& InVertices, bool bRepositionHelperJoint)
{
	TArray<FVector3f> VerticesDNASpace;
	VerticesDNASpace.AddUninitialized(InVertices.Num());
	for (int32 I = 0; I < InVertices.Num(); ++I)
	{
		VerticesDNASpace[I] = FVector3f{ InVertices[I].X, InVertices[I].Z, InVertices[I].Y };
	}
	const Eigen::Map<const Eigen::Matrix<float, 3, -1>> VerticesEigen((const float*)VerticesDNASpace.GetData(), 3, VerticesDNASpace.Num());
	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState = Impl->MHCBodyState->Clone();
	auto extractTranslations = [](const av::ConstArrayView<float>& view) -> Eigen::Matrix<float, 3, Eigen::Dynamic> {
			using Matrix4f = Eigen::Matrix<float, 4, 4>; 
			constexpr int floats_per_transform = 16;

			size_t num_transforms = view.size() / floats_per_transform;
			Eigen::Matrix<float, 3, Eigen::Dynamic> translations(3, num_transforms);

			for (size_t i = 0; i < num_transforms; ++i) {
				const float* base_ptr = view.data() + i * floats_per_transform;
				Eigen::Map<const Matrix4f> mat(base_ptr);
				translations.col(i) = mat.block<3, 1>(0, 3);
			}
			return translations;
	};		
	Eigen::Matrix<float, 3, -1> bindPose = extractTranslations(NewBodyState->GetBindPose());
	Impl->MHCBodyAPI->SetNeutralMesh(*NewBodyState, VerticesEigen);
	if (bRepositionHelperJoint)
	{
		Impl->MHCBodyAPI->FixJoints(*NewBodyState);
		Eigen::Matrix<float, 3, -1> bindPoseFixed = extractTranslations(NewBodyState->GetBindPose());
		for (int32 i : Impl->MHCBodyAPI->HelperJoints())
		{
			bindPose.col(i) = bindPoseFixed.col(i);
		}
	}
	Impl->MHCBodyAPI->SetNeutralJointsTranslations(*NewBodyState, bindPose);
	Impl->MHCBodyState = NewBodyState;
	return true;
}

void FMetaHumanCharacterBodyIdentity::FState::SetGlobalDeltaScale(float InVertexDelta)
{
	check(Impl->MHCBodyState);

	std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyState = Impl->MHCBodyState->Clone();
	Impl->MHCBodyAPI->SetVertexDeltaScale(*NewBodyState, InVertexDelta);
	Impl->MHCBodyState = NewBodyState;
}

float FMetaHumanCharacterBodyIdentity::FState::GetGlobalDeltaScale() const
{
	check(Impl->MHCBodyState);
	return Impl->MHCBodyState->VertexDeltaScale();
}

bool FMetaHumanCharacterBodyIdentity::FState::Serialize(FSharedBuffer& OutArchive) const
{
	check(Impl->MHCBodyState);

	pma::ScopedPtr<dna::MemoryStream> MemStream = pma::makeScoped<dna::MemoryStream>();
	Impl->MHCBodyAPI->DumpState(*(Impl->MHCBodyState), MemStream.get());
	terse::BinaryOutputArchive<trio::BoundedIOStream> archive{MemStream.get()};
	archive(static_cast<uint8>(Impl->MetaHumanBodyType));

	MemStream->seek(0);

	FUniqueBuffer UniqueBuffer = FUniqueBuffer::Alloc(MemStream->size());
	MemStream->read((char*)UniqueBuffer.GetData(), MemStream->size());
	OutArchive = UniqueBuffer.MoveToShared();

	return true;
}

bool FMetaHumanCharacterBodyIdentity::FState::Deserialize(const FSharedBuffer& InArchive)
{
	check(Impl->MHCBodyState);

	if (!InArchive.IsNull())
	{
		pma::ScopedPtr<dna::MemoryStream> MemStream = pma::makeScoped<dna::MemoryStream>();
		MemStream->write((char*)InArchive.GetData(), InArchive.GetSize());
		MemStream->seek(0);

		std::shared_ptr<TITAN_API_NAMESPACE::MetaHumanCreatorBodyAPI::State> NewBodyShapeState = Impl->MHCBodyAPI->CreateState();
		if (Impl->MHCBodyAPI->RestoreState(MemStream.get(), NewBodyShapeState))
		{
			terse::BinaryInputArchive<trio::BoundedIOStream> archive{MemStream.get()};
			uint8 BodyType = 0;
			archive(BodyType);

			Impl->MHCBodyState = NewBodyShapeState;
			EMetaHumanBodyType MetaHumanBodyType = static_cast<EMetaHumanBodyType>(BodyType);
			SetMetaHumanBodyType(MetaHumanBodyType);

			return true;
		}
	}

	return false;
}

TSharedRef<IDNAReader> FMetaHumanCharacterBodyIdentity::FState::StateToDna(dna::Reader* InDnaReader, bool bIsCombine) const
{
	check(Impl->MHCBodyState);

	pma::ScopedPtr<dna::MemoryStream> OutputStream = pma::makeScoped<dna::MemoryStream>();
	pma::ScopedPtr<dna::BinaryStreamWriter> DnaWriter = pma::makeScoped<dna::BinaryStreamWriter>(OutputStream.get());
	DnaWriter.get()->setFrom(InDnaReader);

	Impl->MHCBodyAPI->StateToDna(*(Impl->MHCBodyState), DnaWriter.get(), bIsCombine);
	DnaWriter->write();

	pma::ScopedPtr<dna::BinaryStreamReader> StateDnaReader = pma::makeScoped<dna::BinaryStreamReader>(OutputStream.get());
	StateDnaReader->read();

	return MakeShared<FDNAReader<dna::BinaryStreamReader>>(StateDnaReader.release());
}

TSharedRef<IDNAReader> FMetaHumanCharacterBodyIdentity::FState::StateToDna(UDNAAsset* InBodyDna) const
{
	pma::ScopedPtr<dna::MemoryStream> MemoryStream = pma::makeScoped<dna::MemoryStream>();
	pma::ScopedPtr<dna::BinaryStreamWriter> DnaWriter = pma::makeScoped<dna::BinaryStreamWriter>(MemoryStream.get());

	DnaWriter->setFrom(InBodyDna->GetBehaviorReader()->Unwrap(), dna::DataLayer::All);
#if WITH_EDITORONLY_DATA
	DnaWriter->setFrom(InBodyDna->GetGeometryReader()->Unwrap(), dna::DataLayer::Geometry);
#endif
	DnaWriter->write();

	pma::ScopedPtr<dna::BinaryStreamReader> BinaryDnaReader = pma::makeScoped<dna::BinaryStreamReader>(MemoryStream.get());
	BinaryDnaReader->read();

	return StateToDna(BinaryDnaReader.get());
}

TArray<PhysicsBodyVolume> FMetaHumanCharacterBodyIdentity::FState::GetPhysicsBodyVolumes(const FName& InJointName) const
{
	TArray<PhysicsBodyVolume> OutPhysicsBodyVolumes;

	std::string jointName = TCHAR_TO_UTF8(*InJointName.ToString());

	for (int VolumeIndex = 0; VolumeIndex < Impl->MHCBodyAPI->NumPhysicsBodyVolumes(jointName); VolumeIndex++)
	{
		Eigen::Vector3f BoundingBoxCenter;
		Eigen::Vector3f BoundingBoxExtents;
		Impl->MHCBodyAPI->GetPhysicsBodyBoundingBox(*Impl->MHCBodyState, jointName, VolumeIndex, BoundingBoxCenter, BoundingBoxExtents);

		// Extract transform and extents in UE coordinates
		PhysicsBodyVolume OutPhysicsVolume;
		OutPhysicsVolume.Center = FVector{BoundingBoxCenter[0], -BoundingBoxCenter[1], BoundingBoxCenter[2]};
		OutPhysicsVolume.Extent = FVector{BoundingBoxExtents[0], BoundingBoxExtents[1], BoundingBoxExtents[2]};

		OutPhysicsBodyVolumes.Add(OutPhysicsVolume);
	}

	return OutPhysicsBodyVolumes;
}


int32 FMetaHumanCharacterBodyIdentity::GetNumLOD0MeshVertices(bool bInCombined) const
{
	check(Impl);
	check(Impl->MHCBodyAPI);

	int NumMeshVertices;
	bool bRes = Impl->MHCBodyAPI->GetNumLOD0MeshVertices(NumMeshVertices, bInCombined);

	if (!bRes)
	{
		return -1;
	}
	return static_cast<int32>(NumMeshVertices);
}

TArray<int32> FMetaHumanCharacterBodyIdentity::GetBodyToCombinedMapping() const
{
	check(Impl);
	check(Impl->MHCBodyAPI);
	TArray<int32> BodyToCombinedMapping;
	auto MappingView = Impl->MHCBodyAPI->GetBodyToCombinedMapping(0);
	BodyToCombinedMapping.Append(MappingView.data(), MappingView.size());
	return BodyToCombinedMapping;
}
