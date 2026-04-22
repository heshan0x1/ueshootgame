// Copyright Epic Games, Inc. All Rights Reserved.

#include <robustfeaturematcher/UtilsCamera.h>
#include <math.h>
#include <iostream>
#include <fstream>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

namespace calib {
namespace rfm {

void adjustNlsCamera(const MetaShapeCamera<real_t>& camera, Eigen::Matrix3<real_t>& K, Eigen::VectorX<real_t>& D) {
    Eigen::Matrix3<real_t> intrinsics = camera.Intrinsics();
    Eigen::Vector2 <real_t> skew = camera.Skew();
    intrinsics(0, 0) += skew[0];
    intrinsics(0, 1) = skew[1];
    K = intrinsics;

    Eigen::VectorX<real_t> radialD = camera.RadialDistortion();
    Eigen::VectorX<real_t> tangentialD = camera.TangentialDistortion();
    Eigen::VectorX<real_t> distortionCV(5);
    distortionCV << radialD[0], radialD[1], tangentialD[1], tangentialD[0], radialD[2];
    D = distortionCV;
}

real_t computeEuclidDistance(Eigen::Vector3<real_t> point1, Eigen::Vector3<real_t> point2) {
    real_t estDistance =
        static_cast<real_t>(sqrt(std::pow((point1[0] - point2[0]), 2) + pow((point1[1] - point2[1]), 2) + std::pow((point1[2] - point2[2]), 2)));
    return estDistance;
}

bool compareCalibrations(std::vector<MetaShapeCamera<real_t> > cameras,
                         std::vector<MetaShapeCamera<real_t> > priorCameras,
                         const real_t angleThreshold,
                         const real_t distanceThreshold) {
    for (auto& cam : cameras) {
        for (auto& priorCam : priorCameras) {
            if (cam.Label() == priorCam.Label()) {
                Affine<real_t, 3, 3> extrinsics = cam.Extrinsics();
                Affine<real_t, 3, 3> priorExtrinsics = priorCam.Extrinsics();

                Eigen::MatrixX<real_t> T = extrinsics.Translation();
                Eigen::MatrixX<real_t> priorT = priorExtrinsics.Translation();
                real_t diff = computeEuclidDistance(T, priorT);
                if (diff > distanceThreshold) {
                    std::cout << "Camera " << cam.Label() << " has translation offset: " << diff <<
                    " compared to prior calibration" << std::endl;
                    return false;
                }

                Eigen::Matrix3<real_t> R = extrinsics.Linear();
                Eigen::Vector3<real_t> euler = R.eulerAngles(2, 1, 0);
                Eigen::Matrix3<real_t> priorR = priorExtrinsics.Linear();
                Eigen::Vector3<real_t> priorEuler = priorR.eulerAngles(2, 1, 0);
                real_t diffX = abs(euler[0] - priorEuler[0]);
                real_t diffY = abs(euler[1] - priorEuler[1]);
                real_t diffZ = abs(euler[2] - priorEuler[2]);

                if (diffX > angleThreshold) {
                    std::cout << "Camera " << cam.Label() << " has X rotation offset: " << diffX <<
                    " compared to prior calibration" << std::endl;
                    return false;
                }

                if (diffY > angleThreshold) {
                    std::cout << "Camera " << cam.Label() << " has Y rotation offset: " << diffY <<
                    " compared to prior calibration" << std::endl;
                    return false;
                }

                if (diffZ > angleThreshold) {
                    std::cout << "Camera " << cam.Label() << " has Z rotation offset: " << diffZ <<
                    " compared to prior calibration" << std::endl;
                    return false;
                }
            }
        }
    }
    return true;
}

MetaShapeCamera<real_t> findCameraByLabel(std::vector<MetaShapeCamera<real_t> > cameras,
                                                     std::string cameraLabel,
                                                     int& id) {
    MetaShapeCamera<real_t> camera;
    for (size_t i = 0; i < cameras.size(); i++) {
        std::string labelExt = cameras[i].Label();
        size_t extension = labelExt.find_last_of(".");
        std::string label = labelExt.substr(0, extension);
        if (label == cameraLabel) {
            id = int(i);
            camera = cameras[i];
        }
    }

    return camera;
}


real_t cameraDistance(MetaShapeCamera<real_t> camera1, MetaShapeCamera<real_t> camera2) {
    Eigen::Vector3<real_t> t1 = camera1.Extrinsics().Translation();
    Eigen::Vector3<real_t> t2 = camera2.Extrinsics().Translation();
    real_t distance = static_cast<real_t>(sqrt(pow((t1[0] - t2[0]), 2) + pow((t1[1] - t2[1]), 2) + pow((t1[2] - t2[2]), 2)));

    return distance;
}

std::vector<std::vector<size_t> > getCameraPairs(const std::vector<MetaShapeCamera<real_t>>& cameras) {
    std::vector<std::vector<size_t> > pairs;
    int firstId = -1;
    int secId = -1;
    int thirdId = -1;
    real_t first, sec, third;
    for (size_t j = 0; j < cameras.size(); j++) {
        std::vector<size_t> pairCameras;
        first = sec = third = 10000.;
        for (int i = 0; i < (int)cameras.size(); i++) {
            real_t distance = calib::rfm::cameraDistance(cameras[j], cameras[i]);
            if (distance != 0) {
                if (distance < first) {
                    third = sec;
                    thirdId = secId;
                    sec = first;
                    secId = firstId;
                    first = distance;
                    firstId = i;
                } else if (distance < sec) {
                    third = sec;
                    thirdId = secId;
                    sec = distance;
                    secId = i;
                } else if (distance < third) {
                    third = distance;
                    thirdId = i;
                } else {
                    break;
                }
            }
        }
        pairCameras.push_back(firstId);
        pairCameras.push_back(secId);
        pairCameras.push_back(thirdId);
        pairs.push_back(pairCameras);
    }
    return pairs;
}

std::map<std::string, size_t> tagToId(const std::vector<MetaShapeCamera<real_t>>& cameras) {
    std::map<std::string, size_t> camerasMap;
    for (size_t i = 0; i < cameras.size(); i++) {
        camerasMap[cameras[i].Label()] = i;
    }
    return camerasMap;
}

void writePly(const Eigen::MatrixX<real_t>& points3d) {
    std::ofstream file;
    file.open("file.ply");
    file << "ply\n";
    file << "format ascii 1.0\n";
    file << "element vertex " << points3d.rows() << "\n";
    file << "property float x\n";
    file << "property float y\n";
    file << "property float z\n";
    file << "end_header\n";
    for (int i = 0; i < points3d.rows(); ++i) {
        file << points3d.row(i).col(0) << " " << points3d.row(i).col(1) << " " << points3d.row(i).col(2) << "\n";
    }
    file.close();

}

}
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
