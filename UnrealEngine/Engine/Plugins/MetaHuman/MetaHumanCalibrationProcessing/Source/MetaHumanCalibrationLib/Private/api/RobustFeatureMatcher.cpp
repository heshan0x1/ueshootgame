// Copyright Epic Games, Inc. All Rights Reserved.

#include "RobustFeatureMatcher.h"
#include "Common.h"

#include "Internals/OpenCVCamera2MetaShapeCamera.h"

#include <robustfeaturematcher/RobustFeatureMatcherContext.h>
#include <iterator>

using namespace TITAN_NAMESPACE;

namespace TITAN_API_NAMESPACE
{

static std::vector<float> EigenToPointsVector(const Eigen::MatrixX<double>& InPoints)
{
    std::vector<float> Points;
    for (Eigen::MatrixX<double>::Index Row = 0; Row < InPoints.rows(); ++Row)
    {
        double X = InPoints(Row, 0);
        double Y = InPoints(Row, 1);
        Points.push_back((float)X);
        Points.push_back((float)Y);
    }

    return Points;
}

struct RobustFeatureMatcher::Private
{
    RobustFeatureMatcherParams Params;
    RobustFeatureMatcherContext Context;
};

RobustFeatureMatcher::RobustFeatureMatcher() 
    : pImpl(new Private())
{
}

RobustFeatureMatcher::~RobustFeatureMatcher()
{
    if (pImpl)
    {
        delete pImpl;
        pImpl = nullptr;
    }
}

RobustFeatureMatcher::RobustFeatureMatcher(RobustFeatureMatcher&& InOther) noexcept
    : pImpl(InOther.pImpl)
{
    InOther.pImpl = nullptr;
}

RobustFeatureMatcher& RobustFeatureMatcher::operator=(RobustFeatureMatcher&& InOther) noexcept
{
    if (this == &InOther)
    {
        return *this;
    }

    if (pImpl)
    {
        delete pImpl;
        pImpl = nullptr;
    }

    pImpl = InOther.pImpl;
    InOther.pImpl = nullptr;

    return *this;
}

bool RobustFeatureMatcher::Init(const std::string& InCalibrationFilePath, double InReprojectionThreshold, double InRatioThreshold)
{
    pImpl->Params.setMetaShapeCamerasFromFile(InCalibrationFilePath);
    pImpl->Params.setReprojectionThreshold(InReprojectionThreshold);
    pImpl->Params.setRatioThreshold(InRatioThreshold);

    pImpl->Context.setFeatureNumThreshold(100000);

    return true;
}

bool RobustFeatureMatcher::Init(const std::map<std::string, OpenCVCamera>& InCalibrationInformation, double InReprojectionThreshold, double InRatioThreshold)
{
    std::vector<MetaShapeCamera<real_t>> MetaShapeCalibrationInformation;

    std::transform(InCalibrationInformation.begin(), InCalibrationInformation.end(), std::back_inserter(MetaShapeCalibrationInformation), [](const std::pair<std::string, OpenCVCamera>& InElem) 
    {
        return OpenCVCamera2MetaShapeCamera<real_t>(InElem.first.c_str(), InElem.second);
    });
    
    pImpl->Params.setMetaShapeCameras(MetaShapeCalibrationInformation);
    pImpl->Params.setReprojectionThreshold(InReprojectionThreshold);
    pImpl->Params.setRatioThreshold(InRatioThreshold);

    pImpl->Context.setFeatureNumThreshold(100000);

    return true;
}

bool RobustFeatureMatcher::AddCamera(const std::string& InCameraName, int32_t InWidth, int32_t InHeight)
{
    RobustFeatureMatcherParams::Camera Camera;
    Camera.name = InCameraName;
    Camera.width = InWidth;
    Camera.height = InHeight;
    
    pImpl->Params.addCamera(Camera);

    return true;
}

bool RobustFeatureMatcher::GetDetectedFeatures(size_t InFrame,
                                               std::vector<float>& OutPoints3d,
                                               std::vector<std::vector<float>>& OutCameraPoints,
                                               std::vector<std::vector<float>>& OutPoints3dReprojected)
{
    try
    {
        TITAN_RESET_ERROR;

        Eigen::MatrixX<real_t> Points3d;
        std::vector<std::vector<bool>> Visibility;
        std::vector<Eigen::MatrixX<real_t>> CameraPoints;
        std::vector<Eigen::MatrixX<real_t>> Points3dReprojected;
        pImpl->Context.getScene(InFrame, Points3d, Visibility, CameraPoints, Points3dReprojected);

        OutPoints3d = EigenToPointsVector(Points3d);

        auto TransformLambda = [](const Eigen::MatrixX<real_t>& InElem)
        {
            return EigenToPointsVector(InElem);
        };

        std::transform(CameraPoints.begin(), CameraPoints.end(), std::inserter(OutCameraPoints, OutCameraPoints.end()), TransformLambda);
        std::transform(Points3dReprojected.begin(), Points3dReprojected.end(), std::inserter(OutPoints3dReprojected, OutPoints3dReprojected.end()), TransformLambda);

        return true;
    }
    catch (const std::exception& InException)
    {
        TITAN_HANDLE_EXCEPTION("Failed to obtain the detected features: {}", InException.what());
    }
}

bool RobustFeatureMatcher::GetDetectedFeatures(const std::vector<size_t>& InFrames,
                                               std::map<size_t, std::vector<float>>& OutPoints3d,
                                               std::map<size_t, std::vector<std::vector<float>>>& OutCameraPoints,
                                               std::map<size_t, std::vector<std::vector<float>>>& OutPoints3dReprojected)
{
    for (size_t Frame : InFrames)
    {
        bool bSuccess = GetDetectedFeatures(Frame, OutPoints3d[Frame], OutCameraPoints[Frame], OutPoints3dReprojected[Frame]);

        if (!bSuccess)
        {
            return false;
        }
    }

    return true;
}

bool RobustFeatureMatcher::DetectFeatures(size_t InFrame,
                                          const std::vector<const unsigned char*>& InImages)
{
    try
    {
        TITAN_RESET_ERROR;

        pImpl->Context.setParams(pImpl->Params);
        pImpl->Context.computeFeaturesAndMatches(InFrame, InImages);
        pImpl->Context.createScene(InFrame);

        return true;
    }
    catch (const std::exception& InException)
    {
        TITAN_HANDLE_EXCEPTION("Failed to create a scene: {}", InException.what());
    }
}

bool RobustFeatureMatcher::DetectFeatures(const std::map<size_t, std::vector<const unsigned char*>>& InImages)
{
    for (const auto& ImagePair : InImages)
    {
        bool bSuccess =  DetectFeatures(ImagePair.first, ImagePair.second);

        if (!bSuccess)
        {
            return false;
        }
    }

    return true;
}

} // namespace TITAN_API_NAMESPACE
