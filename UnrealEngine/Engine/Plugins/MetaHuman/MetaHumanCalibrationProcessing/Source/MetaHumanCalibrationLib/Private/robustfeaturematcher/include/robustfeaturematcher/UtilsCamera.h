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

void adjustNlsCamera(const MetaShapeCamera<real_t>& camera, Eigen::Matrix3<real_t>& K, Eigen::VectorX<real_t>& D);
real_t computeEuclidDistance(Eigen::Vector3<real_t> point1, Eigen::Vector3<real_t> point2);
std::vector<std::vector<size_t>> getCameraPairs(const std::vector<Camera*>& cameras);
MetaShapeCamera<real_t> findCameraByLabel(std::vector<MetaShapeCamera<real_t>> cameras,
                                                     std::string cameraLabel,
                                                     int& id);
bool compareCalibrations(std::vector<MetaShapeCamera<real_t>> cameras,
                         std::vector<MetaShapeCamera<real_t>> priorCameras,
                         const real_t angleThreshold,
                         const real_t distanceThreshold);
std::map<std::string, size_t> tagToId(const std::vector<MetaShapeCamera<real_t>>& cameras);
void writePly(const Eigen::MatrixX<real_t>& points3d);
}
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
