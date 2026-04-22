// Copyright Epic Games, Inc. All Rights Reserved.

#include <robustfeaturematcher/RobustFeatureMatcherContext.h>
#include <nls/serialization/MetaShapeSerialization.h>
#include <nls/serialization/CameraSerialization.h>
#include <iostream>

#include <calib/BeforeOpenCvHeaders.h>
CARBON_DISABLE_EIGEN_WARNINGS
#include <opencv2/opencv.hpp>
#include <opencv2/core/eigen.hpp>
CARBON_RENABLE_WARNINGS
#include <calib/AfterOpenCvHeaders.h>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

int countNonZero(std::vector<bool> array) 
{
    int count = 0;
    for (size_t i = 0; i < array.size(); i++) 
    {
        if (array[i] != 0) 
        {
            count++;
        }
    }
    return count;
}

void reorganizeByVisibility(Eigen::MatrixX<real_t>& imagePoints, const std::vector<bool>& vis) 
{
    int visSize = countNonZero(vis);
    Eigen::MatrixX<real_t> newImgPoints(visSize, 2);
    int counter = 0;
    for (size_t i = 0; i < vis.size(); i++) 
    {
        if (vis[i] == 1) 
        {
            newImgPoints.row(counter) = imagePoints.row(i);
            counter++;
        }
    }
    imagePoints = newImgPoints;
}

void drawPoints(const Eigen::MatrixX<real_t>& image1,
    Eigen::MatrixX<real_t> p2d,
    Eigen::MatrixX<real_t> reproj2d,
    Eigen::MatrixX<real_t> reproj2dBa,
    const std::vector<bool>& vis,
    const std::string& filename) 
{
    cv::Mat cvImage1;
    cv::Mat cvImage1bgr;
    cv::eigen2cv(image1, cvImage1);
    cvImage1.convertTo(cvImage1, CV_8U);

    cv::cvtColor(cvImage1, cvImage1bgr, cv::COLOR_GRAY2BGR);

    reorganizeByVisibility(p2d, vis);
    reorganizeByVisibility(reproj2d, vis);
    reorganizeByVisibility(reproj2dBa, vis);

    for (int i = 0; i < reproj2d.rows(); ++i) 
    {
        // original
        cv::circle(cvImage1bgr, cv::Point2f((float)p2d(i, 0), (float)p2d(i, 1)), 2, cv::Scalar(0, 255, 0), cv::FILLED, 8, 0);
        // reprojected
        cv::circle(cvImage1bgr, cv::Point2f((float)reproj2d(i, 0), (float)reproj2d(i, 1)), 4, cv::Scalar(255, 0, 0), 1, 8, 0);
        // ba
        cv::circle(cvImage1bgr, cv::Point2f((float)reproj2dBa(i, 0), (float)reproj2dBa(i, 1)), 4, cv::Scalar(0, 0, 255), 1, 8, 0);
    }
    cv::imwrite(filename, cvImage1bgr);

}

bool Compare(const calib::rfm::Match& a, const calib::rfm::Match& b)
{
    return a.matchId < b.matchId;
}

void RobustFeatureMatcherContext::setParams(const RobustFeatureMatcherParams& parameters)
{
    params = parameters;

    const std::vector<MetaShapeCamera<real_t>>& allCameras = params.getMetaShapeCameras();
    std::vector<std::string> cameraLabels = params.getCameraLabels();

    std::vector<MetaShapeCamera<real_t>> selectedCameras;
    for (size_t i = 0; i < cameraLabels.size(); ++i) 
    {
        for (size_t j = 0; j < allCameras.size(); ++j) 
        {
            if (cameraLabels[i] == allCameras[j].Label()) 
            {
                selectedCameras.push_back(allCameras[j]);
            }
        }
    }

    cameras = selectedCameras;
}

std::vector<MetaShapeCamera<real_t>> RobustFeatureMatcherContext::getCameras()
{
    return cameras;
}

void RobustFeatureMatcherContext::setFeatureNumThreshold(size_t threshold)
{
    ptsNumThreshold = threshold;
}

size_t RobustFeatureMatcherContext::getFeatureNumThreshold()
{
    return ptsNumThreshold;
}

void RobustFeatureMatcherContext::computeFeaturesAndMatches(size_t frame, const std::vector<const unsigned char*>& images)
{
    std::vector<Eigen::MatrixX<real_t>> eigenImages;

    if (params.getCameras().empty())
    {
        throw std::runtime_error("Cameras are not initialized");
    }

    RobustFeatureMatcherParams::Camera firstCamera = params.getCameras()[0];
    int32_t height = firstCamera.height;
    int32_t width = firstCamera.width;

    for (const unsigned char* image : images)
    {
        cv::Mat cvImage(height, width, CV_8U, (void*)image);

        cvImage.convertTo(cvImage, CV_64F);

        Eigen::MatrixX<real_t> out;
        cv::cv2eigen(cvImage, out);

        eigenImages.push_back(std::move(out));
    }

    computeImageFeatures(eigenImages, params, kptsMatrices[frame], descriptors[frame], ptsNumThreshold);
    computeMatches(kptsMatrices[frame], descriptors[frame], cameras, params, matches[frame]);
}

void RobustFeatureMatcherContext::computeFeaturesAndMatches(const std::map<size_t, std::vector<const unsigned char*>>& images)
{
    for (const auto& stereoPairImage : images)
    {
        computeFeaturesAndMatches(stereoPairImage.first, stereoPairImage.second);
    }
}

std::map<size_t, std::vector<calib::rfm::Match>> RobustFeatureMatcherContext::getMatches() const
{
    return matches;
}

std::vector<calib::rfm::Match> RobustFeatureMatcherContext::getMatches(size_t frame) const
{
    return matches.at(frame);
}

void RobustFeatureMatcherContext::createScene()
{
    // calculate the 3D data and reprojected points for each frame
    for (const auto& matchesFrame : matches) 
    {
        const auto frame = matchesFrame.first;
        createScene(frame);
    }
}

void RobustFeatureMatcherContext::createScene(size_t frame)
{
    calib::rfm::createMultiViewScene(matches[frame],
                                     cameras,
                                     cameraPoints[frame],
                                     pts3d[frame],
                                     pts3dReprojected[frame],
                                     visibility[frame],
                                     params.getReprojectionThreshold());
}

void RobustFeatureMatcherContext::getScene(std::map<size_t, Eigen::MatrixX<real_t>>& outPts3d,
                                           std::map<size_t, std::vector<std::vector<bool>>>& outVisibility,
                                           std::map<size_t, std::vector<Eigen::MatrixX<real_t>>>& outCameraPoints,
                                           std::map<size_t, std::vector<Eigen::MatrixX<real_t>>>& outPts3dReprojected) const
{
    outPts3d = pts3d;
    outVisibility = visibility;
    outCameraPoints = cameraPoints;
    outPts3dReprojected = pts3dReprojected;
}

void RobustFeatureMatcherContext::getScene(size_t frame,
                                           Eigen::MatrixX<real_t>& outPts3d,
                                           std::vector<std::vector<bool>>& outVisibility,
                                           std::vector<Eigen::MatrixX<real_t>>& outCameraPoints,
                                           std::vector<Eigen::MatrixX<real_t>>& outPts3dReprojected) const
{
    outPts3d = pts3d.at(frame);
    outVisibility = visibility.at(frame);
    outCameraPoints = cameraPoints.at(frame);
    outPts3dReprojected = pts3dReprojected.at(frame);
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
