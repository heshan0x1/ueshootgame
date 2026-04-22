// Copyright Epic Games, Inc. All Rights Reserved.

#include <robustfeaturematcher/Matches.h>
#include <iostream>
#include <nls/geometry/MetaShapeCamera.h>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

void computeMatches(const std::vector<Eigen::MatrixX<real_t>>& kptsMatrices,
                    const std::vector<Eigen::MatrixX<real_t>>& descriptors,
                    const std::vector<MetaShapeCamera<real_t>>& cameras,
                    const RobustFeatureMatcherParams& appData,
                    std::vector<calib::rfm::Match>& matches) {

    std::vector<std::string> cameraLabels = appData.getCameraLabels();
    real_t ratioThreshold = appData.getRatioThreshold();

    std::vector<int> visitedByRef;
    int currMaxId = 0;
    for (int refId = 0; refId < int(cameraLabels.size()); refId++)
    {
        visitedByRef.push_back(refId);
        int offset = currMaxId;
        for (int j = 0; j < int(cameraLabels.size()); j++)
        {
            int pairId = j;
            bool visited = std::find(std::begin(visitedByRef), std::end(visitedByRef), pairId) != std::end(visitedByRef);
            if ((refId == pairId) || (visited))
            {
                continue;
            }

            std::vector<calib::rfm::Match> maybeMatches = calib::rfm::featureMatching(descriptors[refId],
                descriptors[pairId],
                kptsMatrices[refId],
                kptsMatrices[pairId],
                cameras[refId],
                cameras[pairId],
                ratioThreshold,
                currMaxId,
                offset);
            matches.insert(matches.end(), maybeMatches.begin(), maybeMatches.end());
            std::cout << cameraLabels[refId] << " " << cameraLabels[pairId] << " potential matches: " << maybeMatches.size() << std::endl;
        }
    }
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
