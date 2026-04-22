// Copyright Epic Games, Inc. All Rights Reserved.

#include <robustfeaturematcher/Features.h>
#include <iostream>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

void computeImageFeatures(const std::vector<Eigen::MatrixX<real_t>>& images,
                          RobustFeatureMatcherParams appData,
                          std::vector<Eigen::MatrixX<real_t>>& kptsMatrices,
                          std::vector<Eigen::MatrixX<real_t>>& descriptors,
                          size_t ptsNumThreshold) {

    std::vector<std::string> cameraLabels = appData.getCameraLabels();
    for (int i = 0; i < int(cameraLabels.size()); i++)
    {
        std::cout << "Processing camera: " << cameraLabels[i] << std::endl;

        const Eigen::MatrixX<real_t>& image = images[i];
        Eigen::MatrixX<real_t> imageClahe = calib::rfm::applyClahe(image, 12);

        Eigen::MatrixX<real_t> kptsMatrix, desc;
        calib::rfm::featureDetection(imageClahe, kptsMatrix, desc, ptsNumThreshold);

        kptsMatrices.push_back(kptsMatrix);
        descriptors.push_back(desc);

        std::cout << "Detected: " << kptsMatrix.rows() << std::endl;
    }
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
