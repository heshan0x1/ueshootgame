// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <robustfeaturematcher/RobustFeatureMatcherParams.h>
#include <robustfeaturematcher/ImageFeatures.h>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

void computeMatches(const std::vector<Eigen::MatrixX<real_t>>& kptsMatrices,
                    const std::vector<Eigen::MatrixX<real_t>>& descriptors,
                    const std::vector<MetaShapeCamera<real_t>>& cameras,
                    const RobustFeatureMatcherParams& appData,
                    std::vector<calib::rfm::Match>& matches);

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
