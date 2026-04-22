// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetaHumanCalibrationAutoFrameSelection.h"
#include "Utils/MetaHumanChessboardPointCounter.h"

#include "Settings/MetaHumanCalibrationGeneratorSettings.h"

THIRD_PARTY_INCLUDES_START
#include "PreOpenCVHeaders.h"
#include "opencv2/opencv.hpp"
#include "PostOpenCVHeaders.h"
THIRD_PARTY_INCLUDES_END

#include "Misc/ScopedSlowTask.h"
#include "Templates/Greater.h"

#define LOCTEXT_NAMESPACE "MetaHumanCalibrationAutoFrameSelection"

namespace UE::MetaHuman::Private
{

struct FChessboardPose
{
	cv::Mat Rotation;
	cv::Mat Translation;
};

static FORCEINLINE double CalculateAngleBetween(const cv::Mat& InRotation1, const cv::Mat& InRotation2)
{
	cv::Mat RelativeRotation = InRotation2 * InRotation1.t();
	cv::Vec3d RotationVector;
	cv::Rodrigues(RelativeRotation, RotationVector);
	return cv::norm(RotationVector);
}

static FORCEINLINE double CalculateDistanceBetween(const cv::Mat& InTranslation1, const cv::Mat& InTranslation2)
{
	return cv::norm(InTranslation1 - InTranslation2);
}

static bool IsPoseValid(const cv::Mat& InR, const cv::Mat& InT, const std::vector<cv::Point3d>& InObjectPoints, const cv::Mat& InCameraMatrix)
{
	cv::Mat P = InCameraMatrix * (cv::Mat_<double>(3, 4) <<
								  InR.at<double>(0, 0), InR.at<double>(0, 1), InR.at<double>(0, 2), InT.at<double>(0),
								  InR.at<double>(1, 0), InR.at<double>(1, 1), InR.at<double>(1, 2), InT.at<double>(1),
								  InR.at<double>(2, 0), InR.at<double>(2, 1), InR.at<double>(2, 2), InT.at<double>(2));

	for (const cv::Point3d& Point : InObjectPoints)
	{
		cv::Mat PointMat = (cv::Mat_<double>(4, 1) << Point.x, Point.y, Point.z, 1.0);
		cv::Mat PointCalc = P * PointMat;
		if (PointCalc.at<double>(2, 0) <= 0)
		{
			return false; // behind camera
		}
	}
	return true;
}

}

class FMetaHumanCalibrationAutoFrameSelection::FImpl
{
public:

	FImpl(TPair<FString, FString> InCameraNames,
		  TPair<FIntVector2, FIntVector2> InImageSizes,
		  TPair<FBox2D, FBox2D> InAreaOfInterest);

	TArray<int32> RunSelection(const FPatternInfo& InPattern,
								const TMap<int32, FDetectedFrame>& InDetectedFrames);

private:

	TArray<int32> RunCoverageFrameSelection(const FPatternInfo& InPattern,
											const TMap<int32, FDetectedFrame>& InDetectedFrames);

	TArray<int32> RunCoverageFrameSelectionForCamera(const FString& InCamera,
													 const FIntVector2& InImageSize,
													 const FPatternInfo& InPattern,
													 const TMap<int32, FDetectedFrame>& InDetectedFrames,
													 const FBox2D& InAreaOfInterest);

	TArray<int32> RunPoseDiversityFrameSelection(const FPatternInfo& InPattern,
												 const TMap<int32, FDetectedFrame>& InDetectedFrames);

	static std::vector<cv::Point3d> GenerateObjectPoints(int32 InPatternWidth,
														 int32 InPatternHeight,
														 float InSquareSizeMillimeters);

	static std::vector<cv::Point2d> ConvertVector2DToCv(const TArray<FVector2D>& InPoints);

	const TPair<FString, FString> CameraNames;
	const TPair<FIntVector2, FIntVector2> ImageSizes;
	const TPair<FBox2D, FBox2D> AreaOfInterest;
};

FMetaHumanCalibrationAutoFrameSelection::FImpl::FImpl(TPair<FString, FString> InCameraNames,
													  TPair<FIntVector2, FIntVector2> InImageSizes,
													  TPair<FBox2D, FBox2D> InAreaOfInterest)
	: CameraNames(MoveTemp(InCameraNames))
	, ImageSizes(MoveTemp(InImageSizes))
	, AreaOfInterest(MoveTemp(InAreaOfInterest))
{
}

TArray<int32> FMetaHumanCalibrationAutoFrameSelection::FImpl::RunSelection(const FPatternInfo& InPattern,
																		   const TMap<int32, FDetectedFrame>& InDetectedFrames)
{
	TArray<int32> DiverseSelectedFrames = RunPoseDiversityFrameSelection(InPattern, InDetectedFrames);

	const TMap<int32, FDetectedFrame> DiversityDetectedFrames =
		InDetectedFrames.FilterByPredicate([DiverseSelectedFrames](const TPair<int32, FDetectedFrame>& InElem)
										   {
											   return DiverseSelectedFrames.Contains(InElem.Key);
										   });

	TArray<int32> SelectedFrames = RunCoverageFrameSelection(InPattern, DiversityDetectedFrames);

	const UMetaHumanCalibrationGeneratorSettings* Settings =
		GetDefault<UMetaHumanCalibrationGeneratorSettings>();

	/*
	* Frames are sorted per Frame Score and we are extracting only the frames with the best score until maximum is reached.
	*/
	int32 MaximumFrames = Settings->TargetNumberOfFrames;
	if (SelectedFrames.Num() > MaximumFrames)
	{
		SelectedFrames.RemoveAt(MaximumFrames, SelectedFrames.Num() - MaximumFrames);
	}

	return SelectedFrames;
}

TArray<int32> FMetaHumanCalibrationAutoFrameSelection::FImpl::RunCoverageFrameSelection(const FPatternInfo& InPattern,
																						const TMap<int32, FDetectedFrame>& InDetectedFrames)
{
	TArray<int32> SelectedFrames;

	TArray<int32> SelectedFrames1 = RunCoverageFrameSelectionForCamera(CameraNames.Key, ImageSizes.Key, InPattern, InDetectedFrames, AreaOfInterest.Key);
	TArray<int32> SelectedFrames2 = RunCoverageFrameSelectionForCamera(CameraNames.Value, ImageSizes.Value, InPattern, InDetectedFrames, AreaOfInterest.Value);

	for (int32 FrameIndex : SelectedFrames1)
	{
		if (SelectedFrames2.Contains(FrameIndex))
		{
			SelectedFrames.Add(FrameIndex);
		}
	}

	return SelectedFrames;
}

TArray<int32> FMetaHumanCalibrationAutoFrameSelection::FImpl::RunCoverageFrameSelectionForCamera(const FString& InCamera,
																								 const FIntVector2& InImageSize,
																								 const FPatternInfo& InPattern,
																								 const TMap<int32, FDetectedFrame>& InDetectedFrames,
																								 const FBox2D& InAreaOfInterest)
{
	const UMetaHumanCalibrationGeneratorSettings* Settings =
		GetDefault<UMetaHumanCalibrationGeneratorSettings>();

	FScopedSlowTask AutoFrameSelectionTask(InDetectedFrames.Num(), LOCTEXT("AutoFrameSelection_CheckingCoverage", "Checking frame coverage"));
	AutoFrameSelectionTask.MakeDialog(true);

	FVector2D CoverageMap = FVector2D(Settings->AutomaticFrameSelectionCoverageMap);

	// Input grid size is 0, calculate grid size using GCD
	if (CoverageMap.X == 0.0 || CoverageMap.Y == 0.0)
	{
		int32 GCD = FMath::GreatestCommonDivisor(InImageSize.X, InImageSize.Y);

		CoverageMap = FVector2D(InImageSize) / GCD;
	}

	FMetaHumanChessboardPointCounter GlobalCounter(CoverageMap, CameraNames, ImageSizes);
	GlobalCounter.Update(InDetectedFrames);

	TArray<int32> GlobalBlockIndices = GlobalCounter.GetBlockIndices(InCamera);

	TArray<float> BlockWeights;
	BlockWeights.Reserve(GlobalBlockIndices.Num());

	TArray<int32> MaxNumberOfPointsPerBlock;
	MaxNumberOfPointsPerBlock.AddZeroed(GlobalBlockIndices.Num());

	if (InAreaOfInterest.Min == FVector2D::ZeroVector &&
		InAreaOfInterest.Max == FVector2D(InImageSize))
	{
		/*
		* Calculating weights if the area of interest is the full image. 
		* 
		* The logic assigns weights to the blocks based on how far they are from the centre.
		* This gives an advantage to the blocks that are closer to the middle of the image over those outside of it.
		*/
		static constexpr float Alpha = 4.0f;
		for (int32 Y = 0; Y < CoverageMap.Y; ++Y)
		{
			for (int32 X = 0; X < CoverageMap.X; ++X)
			{
				float Dx = 2.0f * (X + 0.5f) / CoverageMap.X - 1.0f;
				float Dy = 2.0f * (Y + 0.5f) / CoverageMap.Y - 1.0f;
				float D = Dx * Dx + Dy * Dy;
				float Weight = FMath::Exp(-Alpha * D);
				BlockWeights.Add(Weight);
			}
		}
	}
	else
	{
		/*
		* Calculating weights if the area of interest is provided by the user.
		*
		* The weights are calculated by checking if the block belongs to the specified area of interest:
		*	The blocks which fully in the area of interest will have the weight of 1.0f
		*	The blocks which are completely outside will have the weight of 0.0
		*	The blocks which are intersecting with the area of interest, will have the weight based on how much they are intersecting.
		* 
		* This gives an advantage to the blocks that are in the area of interest over those outside of it.
		*/
		for (int32 Y = 0; Y < CoverageMap.Y; ++Y)
		{
			for (int32 X = 0; X < CoverageMap.X; ++X)
			{
				int32 Index = Y * CoverageMap.X + X;
				FBox2D Block = GlobalCounter.GetBlock(InCamera, Index);

				FBox2D Intersection = InAreaOfInterest.Overlap(Block);
				if (!Intersection.bIsValid)
				{
					BlockWeights.Add(0.0f);
					continue;
				}

				BlockWeights.Add(Intersection.GetArea() / Block.GetArea());
			}
		}
	}

	/*
	* The Frame Score is calculated as a sum of the number of points in a block for a frame, 
	* divided by the number of points in a block for the whole footage, multiplied by the block weight.
	* 
	* The points block is completely discarded if the frame doesn't contain more than the minimum number of points defined in the settings.
	*/
	auto FrameScore = [this, CoverageMap, InCamera, BlockWeights, &GlobalCounter, Settings](int32 InFrameIndex, const TArray<FVector2D>& InPoints) -> float
	{
		FMetaHumanChessboardPointCounter Counter(CoverageMap, CameraNames, ImageSizes);
		Counter.Update(InCamera, InPoints);

		TMap<int32, int32> BlockCount = Counter.GetOccupiedBlockIndicesAndCount(InCamera, Settings->MinimumPointsPerBlock);

		float Score = 0.0f;

		for (const TPair<int32, int32>& BlockCountPair : BlockCount)
		{
			int32 GlobalBlockCount = GlobalCounter.GetCountForBlock(InCamera, BlockCountPair.Key).GetValue();
			check(GlobalBlockCount > 0);

			Score += (static_cast<float>(BlockCountPair.Value) / GlobalBlockCount) * BlockWeights[BlockCountPair.Key];
		}

		return Score;
	};

	TMap<int32, float> AllFrameScores;
	for (const TPair<int32, FDetectedFrame>& DetectedFramePair : InDetectedFrames)
	{
		float Score = FrameScore(DetectedFramePair.Key, DetectedFramePair.Value[InCamera]);
		AllFrameScores.Add(DetectedFramePair.Key, Score);

		AutoFrameSelectionTask.EnterProgressFrame();
	}

	AllFrameScores.ValueSort(TGreater<>());

	float MinimumBlockWeight = *Algo::MinElementBy(BlockWeights, [](float InValue)
	{
		return InValue > 0.0f;
	});

	/*
	* Discarding frames which are not satisfying the minimum possible score.
	*/
	float MinimumFrameScore = (static_cast<float>(Settings->MinimumPointsPerBlock) / Settings->NumberOfPointsThreshold) * MinimumBlockWeight * BlockWeights.Num();
	TMap<int32, float> FrameScores = AllFrameScores.FilterByPredicate([MinimumFrameScore](const TPair<int32, float>& InFrameScore)
	{
		return InFrameScore.Key > MinimumFrameScore;
	});

	TArray<int32> Selected;
	FrameScores.GetKeys(Selected);
	
	return Selected;
}

TArray<int32> FMetaHumanCalibrationAutoFrameSelection::FImpl::RunPoseDiversityFrameSelection(const FPatternInfo& InPattern,
																							 const TMap<int32, FDetectedFrame>& InDetectedFrames)
{
	TArray<int32> SelectedFrames;

	using namespace UE::MetaHuman::Private;

	std::vector<FChessboardPose> CurrentPoses1;
	std::vector<FChessboardPose> CurrentPoses2;

	static constexpr double MinAngleRad = 0.1;
	static constexpr double MinTranslation = 0.1;

	const std::vector<cv::Point3d> ObjectPoints =
		GenerateObjectPoints(InPattern.Width, InPattern.Height, InPattern.SquareSize);

	auto CreateRoughIntrinsics = [this]() -> cv::Mat
		{
			return (
				cv::Mat_<double>(3, 3) <<
				ImageSizes.Key.X, 0, (double) ImageSizes.Key.X / 2,
				0, ImageSizes.Key.X, (double) ImageSizes.Key.Y / 2,
				0, 0, 1
				);
		};

	TPair<cv::Mat, cv::Mat> RoughCameraIntrinsics;
	RoughCameraIntrinsics = { CreateRoughIntrinsics(), CreateRoughIntrinsics() };

	FScopedSlowTask AutoFrameSelectionTask(InDetectedFrames.Num(), LOCTEXT("AutoFrameSelection_CheckingFrames", "Checking chessboard orientation and location"));
	AutoFrameSelectionTask.MakeDialog(true);

	for (const TPair<int32, FDetectedFrame>& DetectedFramePair : InDetectedFrames)
	{
		if (AutoFrameSelectionTask.ShouldCancel())
		{
			return TArray<int32>();
		}

		int32 ValidIndex = INDEX_NONE;

		std::vector<cv::Mat> R1s, R2s, Ns;
		std::vector<cv::Mat> T1s, T2s;

		std::vector<cv::Point2d> CvDetectedPoints1 = ConvertVector2DToCv(DetectedFramePair.Value[CameraNames.Key]);
		std::vector<cv::Point2d> CvDetectedPoints2 = ConvertVector2DToCv(DetectedFramePair.Value[CameraNames.Value]);

		cv::Mat H1 = cv::findHomography(ObjectPoints, CvDetectedPoints1);
		cv::Mat H2 = cv::findHomography(ObjectPoints, CvDetectedPoints2);

		int32 NumberOfSolutions1 = cv::decomposeHomographyMat(H1, RoughCameraIntrinsics.Key, R1s, T1s, Ns);
		int32 NumberOfSolutions2 = cv::decomposeHomographyMat(H2, RoughCameraIntrinsics.Value, R2s, T2s, Ns);

		for (int32 Index = 0; Index < std::min(NumberOfSolutions1, NumberOfSolutions2); ++Index)
		{
			if (IsPoseValid(R1s[Index], T1s[Index], ObjectPoints, RoughCameraIntrinsics.Key))
			{
				ValidIndex = Index;
				break;
			}
		}

		if (ValidIndex == INDEX_NONE)
		{
			continue;
		}

		bool bShouldAdd = true;
		for (int32 Index = 0; Index < CurrentPoses1.size(); ++Index)
		{
			double Angle1 = CalculateAngleBetween(R1s[ValidIndex], CurrentPoses1[Index].Rotation);
			double Distance1 = CalculateDistanceBetween(T1s[ValidIndex], CurrentPoses1[Index].Translation);

			double Angle2 = CalculateAngleBetween(R2s[ValidIndex], CurrentPoses2[Index].Rotation);
			double Distance2 = CalculateDistanceBetween(T2s[ValidIndex], CurrentPoses2[Index].Translation);

			if (Angle1 < MinAngleRad && Angle2 < MinAngleRad &&
				Distance1 < MinTranslation && Distance2 < MinTranslation)
			{
				bShouldAdd = false;
				break;
			}
		}

		if (bShouldAdd)
		{
			CurrentPoses1.emplace_back(R1s[ValidIndex], T1s[ValidIndex]);
			CurrentPoses2.emplace_back(R2s[ValidIndex], T2s[ValidIndex]);
			SelectedFrames.Add(DetectedFramePair.Key);
		}

		AutoFrameSelectionTask.EnterProgressFrame();
	}

	return SelectedFrames;
}

std::vector<cv::Point3d> FMetaHumanCalibrationAutoFrameSelection::FImpl::GenerateObjectPoints(int32 InPatternWidth,
																							  int32 InPatternHeight,
																							  float InSquareSizeMillimeters)
{
	std::vector<cv::Point3d> Points;
	Points.reserve(InPatternWidth * InPatternHeight);

	for (int32 X = 0; X < InPatternWidth; X++)
	{
		for (int32 Y = 0; Y < InPatternHeight; Y++)
		{
			Points.push_back(cv::Point3d(X * InSquareSizeMillimeters, Y * InSquareSizeMillimeters, 0));
		}
	}

	return Points;
}

std::vector<cv::Point2d> FMetaHumanCalibrationAutoFrameSelection::FImpl::ConvertVector2DToCv(const TArray<FVector2D>& InArray)
{
	std::vector<cv::Point2d> OutputArray;
	OutputArray.reserve(InArray.Num());

	for (const FVector2D& Point : InArray)
	{
		OutputArray.emplace_back(Point.X, Point.Y);
	}

	return OutputArray;
}

FMetaHumanCalibrationAutoFrameSelection::FMetaHumanCalibrationAutoFrameSelection(TPair<FString, FString> InCameraNames,
																				 TPair<FIntVector2, FIntVector2> InImageSize,
																				 TPair<FBox2D, FBox2D> InAreaOfInterest)
	: Impl(MakePimpl<FImpl>(MoveTemp(InCameraNames), MoveTemp(InImageSize), MoveTemp(InAreaOfInterest)))
{
}

TArray<int32> FMetaHumanCalibrationAutoFrameSelection::RunSelection(const FPatternInfo& InPattern,
																	const TMap<int32, FDetectedFrame>& InDetectedFrames)
{
	return Impl->RunSelection(InPattern, InDetectedFrames);
}

#undef LOCTEXT_NAMESPACE
