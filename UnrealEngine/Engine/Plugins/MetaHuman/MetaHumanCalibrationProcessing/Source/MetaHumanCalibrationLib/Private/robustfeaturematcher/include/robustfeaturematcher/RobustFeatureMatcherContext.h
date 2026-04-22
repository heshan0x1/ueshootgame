// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

/**
    Robust Feature Matcher - RobustFeatureMatcherContext class

    Header contains the C++ implementation of the robust feature matcher context and related structures and functionalities.
 */

#include <calib/BundleAdjustmentPerformer.h>
#include <calib/DHInputOutput.h>
#include <robustfeaturematcher/Features.h>
#include <robustfeaturematcher/Matches.h>
#include <robustfeaturematcher/UtilsCamera.h>
#include <robustfeaturematcher/RobustFeatureMatcherParams.h>
#include <iostream>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

 /**
     @brief
      RobustFeatureMatcherContext is high-level API class which unifies objects and functionalities
      and makes it easy to create a scene for bundle adjustment.
   */
class RobustFeatureMatcherContext
{
public:
    RobustFeatureMatcherContext() = default;
    ~RobustFeatureMatcherContext() = default;

    // --------------------------------------------------------------------
    // API
    // --------------------------------------------------------------------
    /**
        @brief Sets the data description object, which contains all neccessary data: cameras, calibration and images.

        @param parameters
            Object representing data description
     */
    void setParams(const RobustFeatureMatcherParams& parameters);

    /**
        @brief Get all camera models

        @return
            Array of CameraModel objects.
     */
    std::vector<TITAN_NAMESPACE::MetaShapeCamera<real_t> > getCameras();

    size_t getFeatureNumThreshold();

    void setFeatureNumThreshold(size_t threshold);

    /**
        @brief Compute matches between stereo pair images.

        @return
            Vector of matches.
    */
    void computeFeaturesAndMatches(size_t frame, const std::vector<const unsigned char*>& images);

    /**
        @brief Compute matches between list of stereo pair images.

        @return
            Vector of matches.
    */
    void computeFeaturesAndMatches(const std::map<size_t, std::vector<const unsigned char*>>& images);

    /**
        @brief Get matches between images computed in computeFeaturesAndMatches() method.

        @return
            Vector of matches.
    */
    std::map<size_t,std::vector<calib::rfm::Match>> getMatches() const;

    /**
        @brief Get matches between images computed in computeFeaturesAndMatches(size_t frame) method.

        @return
            Vector of matches.
    */
    std::vector<calib::rfm::Match> getMatches(size_t frame) const;

    /**
        @brief Create multi-view scene for a frame (image points, 3d points and visibility information)
    */
    void createScene(size_t frame);

    /**
        @brief Create multi-view scene for all the matched frames (image points, 3d points and visibility information)
    */
    void createScene();

    /**
        @brief get the scene of specified frame: image points, 3d reconstructions, reprojectd reconstructions and visibility
    */
    void getScene(size_t frame,
                  Eigen::MatrixX<real_t>& outPts3d,
                  std::vector<std::vector<bool>>& outVisibility,
                  std::vector<Eigen::MatrixX<real_t>>& outCameraPoints,
                  std::vector<Eigen::MatrixX<real_t>>& outPts3dReprojected) const;

    /**
        @brief get the scene of all matched frames: image points, 3d reconstructions, reprojectd reconstructions and visibility
    */
    void getScene(std::map<size_t, Eigen::MatrixX<real_t>>& outPts3d,
                  std::map<size_t, std::vector<std::vector<bool>>>& outVisibility,
                  std::map<size_t, std::vector<Eigen::MatrixX<real_t>>>& outCameraPoints,
                  std::map<size_t, std::vector<Eigen::MatrixX<real_t>>>& outPts3dReprojected) const;

private:
    RobustFeatureMatcherParams params;
    std::vector<TITAN_NAMESPACE::MetaShapeCamera<real_t>> cameras;
    std::map<size_t, std::vector<Eigen::MatrixX<real_t>>> kptsMatrices;
    std::map<size_t, std::vector<Eigen::MatrixX<real_t>>> descriptors;
    std::map<size_t, std::vector<calib::rfm::Match>> matches;
    std::map<size_t, Eigen::MatrixX<real_t>> pts3d;
    std::map<size_t, std::vector<std::vector<bool>>> visibility;
    std::map<size_t, std::vector<Eigen::MatrixX<real_t>>> cameraPoints;
    std::map<size_t, std::vector<Eigen::MatrixX<real_t>>> pts3dReprojected;
    size_t ptsNumThreshold{ 200000 };
};

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
