// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <robustfeaturematcher/RobustFeatureMatcherParams.h>
#include <robustfeaturematcher/ImageFeatures.h>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

void computeImageFeatures(const std::vector<Eigen::MatrixX<real_t>>& images,
                          RobustFeatureMatcherParams appData,
                          std::vector<Eigen::MatrixX<real_t>>& kptsMatrices,
                          std::vector<Eigen::MatrixX<real_t>>& descriptors,
                          size_t ptsNumThreshold = 100000);

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
