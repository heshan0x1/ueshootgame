// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <calib/CameraModel.h>
#include <carbon/io/JsonIO.h>
#include <carbon/io/Utils.h>
#include <carbon/utils/StringFormat.h>

#include <nls/serialization/CameraSerialization.h>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

class RobustFeatureMatcherParams {

    public:
        RobustFeatureMatcherParams() = default;

        struct Camera
        {
            std::string name;
            int32_t width;
            int32_t height;
        };

        std::vector<std::string> getCameraLabels() const {
            std::vector<std::string> cameraLabels;
            std::transform(cameras.begin(), cameras.end(), std::back_inserter(cameraLabels), [](const Camera& camera) {
                return camera.name;
            });

            return cameraLabels;
        }

        std::vector<Camera> getCameras() const {
            return cameras;
        }

        void addCamera(const Camera& camera) {
            cameras.push_back(camera);
        }

        std::optional<Camera> getCamera(const std::string& cameraLabel) {
            std::vector<Camera>::const_iterator found = 
                std::find_if(cameras.begin(), cameras.end(), [&cameraLabel](const Camera& camera) {
                return camera.name == cameraLabel;
            });

            if (found == cameras.end()) {
                return std::nullopt;
            }

            return *found;
        }

        const std::vector<MetaShapeCamera<real_t>>& getMetaShapeCameras() const {
            return calibrations;
        }

        void setMetaShapeCameras(const std::vector<MetaShapeCamera<real_t>>& calibs) {
            calibrations = calibs;
        }

        void setMetaShapeCamerasFromFile(const std::string& inputFilePath) {
            ReadMetaShapeCamerasFromJsonFile<real_t>(inputFilePath, calibrations);
        }

        real_t getReprojectionThreshold() const {
            return reprojectionThreshold;
        }

        void setReprojectionThreshold(real_t reprojectionThresh) {
            reprojectionThreshold = reprojectionThresh;
        }

        real_t getRatioThreshold() const {
            return ratioThreshold;
        }

        void setRatioThreshold(real_t ratioThresh) {
            ratioThreshold = ratioThresh;
        }

    private:
        std::vector<Camera> cameras;
        std::vector<MetaShapeCamera<real_t>> calibrations;

        real_t reprojectionThreshold = 0.0;
        real_t ratioThreshold = 0.0;
};

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
