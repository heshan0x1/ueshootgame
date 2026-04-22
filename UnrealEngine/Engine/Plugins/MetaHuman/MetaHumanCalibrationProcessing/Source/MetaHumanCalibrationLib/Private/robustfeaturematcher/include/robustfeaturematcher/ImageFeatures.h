// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <carbon/Common.h>
#include <calib/CameraModel.h>
#include <nls/geometry/MetaShapeCamera.h>
CARBON_DISABLE_EIGEN_WARNINGS
#include <Eigen/Dense>
CARBON_RENABLE_WARNINGS

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)


namespace calib {
namespace rfm {
struct Match {
    Eigen::Vector2<real_t> keypointLeft;
    Eigen::Vector2<real_t> keypointRight;
    size_t matchId;
    MetaShapeCamera<real_t> refCamera;
    MetaShapeCamera<real_t> pairCamera;
};

std::vector<Match> featureMatching(const Eigen::MatrixX<real_t>& descriptor1,
                                   const Eigen::MatrixX<real_t>& descriptor2,
                                   const Eigen::MatrixX<real_t>& keypoints1,
                                   const Eigen::MatrixX<real_t>& keypoints2,
                                   const MetaShapeCamera<real_t>& refCamera,
                                   const MetaShapeCamera<real_t>& pairCamera,
                                   real_t ratioThreshold,
                                   int& currMaxId,
                                   int offset);
void featureDetection(const Eigen::MatrixX<real_t>& img,
                      Eigen::MatrixX<real_t>& kptsMatrix,
                      Eigen::MatrixX<real_t>& desc,
                      size_t ptsNumThreshold = 200000);
Eigen::Vector3<real_t> triangulateMatch(const MetaShapeCamera<real_t>& leftCamera,
                                        const MetaShapeCamera<real_t>& rightCamera,
                                        const Eigen::MatrixX<real_t>& leftPt,
                                        const Eigen::MatrixX<real_t>& rightPt);
void createMultiViewScene(const std::vector<calib::rfm::Match>& matches,
                          const std::vector<MetaShapeCamera<real_t>>& cameras,
                          std::vector<Eigen::MatrixX<real_t>>& cameraPoints,
                          Eigen::MatrixX<real_t>& pts3d,
                          std::vector<Eigen::MatrixX<real_t>>& pts3dReprojected, 
                          std::vector<std::vector<bool>>& vis,
                          real_t reprojFilterThresh);
Eigen::MatrixX<real_t>applyClahe(const Eigen::MatrixX<real_t>& image, int limit);

}
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
