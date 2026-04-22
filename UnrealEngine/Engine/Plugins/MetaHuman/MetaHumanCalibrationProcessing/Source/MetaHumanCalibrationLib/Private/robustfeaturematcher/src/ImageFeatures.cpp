// Copyright Epic Games, Inc. All Rights Reserved.

#include <robustfeaturematcher/ImageFeatures.h>
#include <robustfeaturematcher/UtilsCamera.h>
#include <calib/Calibration.h>
#include <calib/Utilities.h>
#include <calib/CameraModel.h>
#include <carbon/io/CameraIO.h>

#include <calib/BeforeOpenCvHeaders.h>
CARBON_DISABLE_EIGEN_WARNINGS
#include <opencv2/opencv.hpp>
#include <opencv2/core/eigen.hpp>
#include <opencv2/features2d.hpp>
CARBON_RENABLE_WARNINGS
#include <calib/AfterOpenCvHeaders.h>

#define DEBUG_MODE

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

namespace calib {
namespace rfm {

std::vector<cv::DMatch> ratioTest(const std::vector<std::vector<cv::DMatch> >& matches, real_t ratio);
std::vector<cv::DMatch> symmetryTest(const std::vector<cv::DMatch>& goodMatches1, const std::vector<cv::DMatch>& goodMatches2);
real_t pointLineDistance(cv::Point2f point, cv::Vec3f line);

void nonMaxSupression(std::vector<cv::KeyPoint>& kpts, size_t winSize, size_t imgWidth, size_t imgHeight) {
    std::vector<cv::KeyPoint> suppresedKpts;

    for (int j = static_cast<int>(winSize) / 2; j < static_cast<int>(imgHeight - winSize); j += static_cast<int>(winSize)) {
        for (int i = static_cast<int>(winSize) / 2; i < static_cast<int>(imgWidth - winSize); i += static_cast<int>(winSize)) {
            cv::Point2i blockCenter = cv::Point2i(i, j);
            cv::KeyPoint blockKeypoint;
            bool hasKeypoint = false;
            real_t maxResponse = -1e-6;
            for (size_t k = 0; k < kpts.size(); ++k) {
                auto currentKeypoint = kpts[k];
                if ((abs(currentKeypoint.pt.x - blockCenter.x) <= winSize / 2) &&
                    (abs(currentKeypoint.pt.y - blockCenter.y) <= winSize / 2)) {
                    if (currentKeypoint.response > maxResponse) {
                        maxResponse = currentKeypoint.response;
                        blockKeypoint = currentKeypoint;
                        hasKeypoint = true;
                    }
                }
            }
            if (hasKeypoint) {
                suppresedKpts.push_back(blockKeypoint);
            }
        }
    }

    kpts = suppresedKpts;
}

bool compareKeypoints(cv::KeyPoint lhs, cv::KeyPoint rhs) {
    return lhs.response > rhs.response;
}

void featureDetection(const Eigen::MatrixX<real_t>& img, Eigen::MatrixX<real_t>& kptsMatrix, Eigen::MatrixX<real_t>& desc, size_t ptsNumThreshold) {
    cv::Mat cvImage, cvdesc;
    cv::eigen2cv(img, cvImage);
    cvImage.convertTo(cvImage, CV_8U);

    cv::Ptr<cv::Feature2D> detector;
    std::vector<cv::Point2f> kptsPoints;
    std::vector<cv::KeyPoint> kpts;
    cv::Ptr<cv::Feature2D> akaze = cv::AKAZE::create();
    akaze->detect(cvImage, kpts);
    nonMaxSupression(kpts, 8, cvImage.cols, cvImage.rows);
    if (kpts.size() > ptsNumThreshold) {
        std::sort(kpts.begin(), kpts.end(), compareKeypoints);
        int delta = static_cast<int>(kpts.size() - ptsNumThreshold);
        kpts.erase(kpts.end() - delta, kpts.end());
    }

    akaze->compute(cvImage, kpts, cvdesc);
    cv::KeyPoint::convert(kpts, kptsPoints, std::vector<int>());

    Eigen::MatrixX<real_t> pointmatrix(kptsPoints.size(), 2);
    for (size_t i = 0; i < kptsPoints.size(); ++i) {
        pointmatrix(i, 0) = (kptsPoints[i].x);
        pointmatrix(i, 1) = (kptsPoints[i].y);
    }
    cvdesc.convertTo(cvdesc, CV_64F);
    kptsMatrix = pointmatrix;
    cv::cv2eigen(cvdesc, desc);
}

std::vector<Match> featureMatching(const Eigen::MatrixX<real_t>& descriptor1,
                                   const Eigen::MatrixX<real_t>& descriptor2,
                                   const Eigen::MatrixX<real_t>& keypoints1,
                                   const Eigen::MatrixX<real_t>& keypoints2,
                                   const MetaShapeCamera<real_t>& refCamera,
                                   const MetaShapeCamera<real_t>& pairCamera,
                                   real_t ratioThreshold,
                                   int& currMaxId,
                                   int offset) {

    CARBON_ASSERT((keypoints1.size() != 0) && (keypoints2.size() != 0), "Input image container is empty." );
    cv::Mat descriptor1cv, descriptor2cv;
    cv::eigen2cv(descriptor1, descriptor1cv);
    cv::eigen2cv(descriptor2, descriptor2cv);
    descriptor1cv.convertTo(descriptor1cv, CV_8U);
    descriptor2cv.convertTo(descriptor2cv, CV_8U);

    std::vector<std::vector<cv::DMatch> > knnMatches12, knnMatches21;
    std::vector<cv::DMatch> newKnn12, newKnn21;
    std::vector<cv::Point2f> matches;
    std::vector<Match> matchesF;

    cv::Ptr<cv::DescriptorMatcher> matcher = cv::DescriptorMatcher::create("BruteForce-Hamming");

    matcher->knnMatch(descriptor1cv, descriptor2cv, knnMatches12, 4);
    matcher->knnMatch(descriptor2cv, descriptor1cv, knnMatches21, 4);

    newKnn12 = ratioTest(knnMatches12, ratioThreshold);
    newKnn21 = ratioTest(knnMatches21, ratioThreshold);

    std::vector<cv::DMatch> goodMatches = symmetryTest(newKnn12, newKnn21);
    // goodMatches = newKnn12;

    // TODO: BETTER WAY FOR FEATURE ID
    std::vector<cv::Point2f> srcPts, dstPts;
    for (auto it = goodMatches.begin(); it != goodMatches.end(); ++it) {
        Match ma;
        ma.keypointLeft = keypoints1.row(it->queryIdx);
        ma.keypointRight = keypoints2.row(it->trainIdx);
        ma.matchId = (it->queryIdx) + offset;
        ma.refCamera = refCamera;
        ma.pairCamera = pairCamera;
        matchesF.push_back(ma);
        if (int(ma.matchId) > currMaxId) {
            currMaxId = static_cast<int>(ma.matchId);
        }
    }

    return matchesF;
}

void points2dToCameraPoints(std::vector<std::vector<cv::Point2f> > points2d, size_t numCameras,
                            std::vector<Eigen::MatrixX<real_t>>& cameraPoints) {
    for (size_t j = 0; j < numCameras; ++j) {
        Eigen::MatrixX<real_t> p2d1(points2d.size(), 2);
        for (size_t i = 0; i < points2d.size(); ++i) {
            p2d1(i, 0) = (points2d[i][j].x);
            p2d1(i, 1) = (points2d[i][j].y);
        }
        cameraPoints.push_back(std::move(p2d1));
    }
}

void points3dFromCvToMat(std::vector<cv::Point3f> points3d, Eigen::MatrixX<real_t>& pts3d) {
    pts3d.resize(points3d.size(), 3);
    for (size_t i = 0; i < points3d.size(); ++i) {
        pts3d(i, 0) = (points3d[i].x);
        pts3d(i, 1) = (points3d[i].y);
        pts3d(i, 2) = (points3d[i].z);
    }
}

std::vector<calib::rfm::Match> findMatchById(size_t matchId, const std::vector<calib::rfm::Match>& matches)
{
    std::vector<calib::rfm::Match> matchVis;
    auto it = matches.begin();
    while (it != matches.end()) {
        if (it->matchId == matchId) {
            matchVis.push_back(*it);
            it++;
        } else {
            it++;
        }
    }
    return matchVis;
}

void getUniqueMatches(std::vector<calib::rfm::Match> matches, std::vector<size_t>& indices)
{
    for (size_t i = 0; i < matches.size(); ++i) {
        Match match = matches[i];
        size_t id = matches[i].matchId;
        indices.push_back(id);
    }

    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
}

bool checkMatchReprojection(const Match& match, real_t reprojectionThresh, Eigen::Vector3<real_t>& pt3d) {
    MetaShapeCamera<real_t> refCamera = match.refCamera;
    MetaShapeCamera<real_t> pairCamera = match.pairCamera;
    Eigen::MatrixX<real_t> p2d1 = match.keypointLeft;
    Eigen::MatrixX<real_t> p2d2 = match.keypointRight;
    pt3d = calib::rfm::triangulateMatch(refCamera, pairCamera, p2d1, p2d2);

    Eigen::Matrix3<real_t> refIntrinsics, pairIntrinsics;
    Eigen::VectorX<real_t> refDistortion, pairDistortion;

    calib::rfm::adjustNlsCamera(refCamera, refIntrinsics, refDistortion);
    calib::rfm::adjustNlsCamera(pairCamera, pairIntrinsics, pairDistortion);

    auto reproj2d1 = projectPointOnImagePlane(pt3d,
                                              refIntrinsics,
                                              refDistortion,
                                              refCamera.Extrinsics().Matrix());
    auto reproj2d2 = projectPointOnImagePlane(pt3d,
                                              pairIntrinsics,
                                              pairDistortion,
                                              pairCamera.Extrinsics().Matrix());

    real_t leftMse =
        sqrt((p2d1(0) - reproj2d1(0)) * (p2d1(0) - reproj2d1(0)) + (p2d1(1) - reproj2d1(1)) * (p2d1(1) - reproj2d1(1)));
    real_t rightMse =
        sqrt((p2d2(0) - reproj2d2(0)) * (p2d2(0) - reproj2d2(0)) + (p2d2(1) - reproj2d2(1)) * (p2d2(1) - reproj2d2(1)));

    if ((leftMse > reprojectionThresh) || (rightMse > reprojectionThresh)) {
        return false;
    }

    return true;
}

void createMultiViewScene(const std::vector<calib::rfm::Match>& matches, const std::vector<MetaShapeCamera<real_t>>& cameras,
                          std::vector<Eigen::MatrixX<real_t>>& cameraPoints, Eigen::MatrixX<real_t>& pts3d,
                          std::vector<Eigen::MatrixX<real_t>>& pts3dReprojected, 
                          std::vector<std::vector<bool>>& vis, real_t reprojFilterThresh) {
    std::vector<cv::Point3f> points3d;
    std::vector<std::vector<cv::Point2f>> points2d;
    std::vector<std::vector<cv::Point2f>> points3dReprojected;
    std::vector<std::vector<bool>> visibility;
    cv::Point3f p3d;
    std::vector<calib::rfm::Match> matchesById;
    std::vector<size_t> indices;
    std::map<std::string, size_t> camerasId = tagToId(cameras);
    getUniqueMatches(matches, indices);

    for (size_t i = 0; i < indices.size(); ++i) {
        std::vector<bool> matchVis(cameras.size(), false);
        std::vector<cv::Point2f> matchCoords(cameras.size());
        std::vector<cv::Point2f> reprojectedCoords(cameras.size());
        Eigen::Vector3<real_t> pt3d;
        Eigen::MatrixX<real_t> reproj2d1, reproj2d2;

        matchesById = findMatchById(indices[i], matches);
        if (!checkMatchReprojection(matchesById[0], reprojFilterThresh, pt3d)) {
            continue;
        }

        p3d.x = (float)pt3d[0];
        p3d.y = (float)pt3d[1];
        p3d.z = (float)pt3d[2];

        for (size_t j = 0; j < matchesById.size(); j++) {

            Match match = matchesById[j];
            auto refIt = camerasId.find(match.refCamera.Label());
            size_t ref = refIt->second;
            auto pairIt = camerasId.find(match.pairCamera.Label());
            size_t pair = pairIt->second;

            if (match.refCamera.Label() != matchesById[0].refCamera.Label()) {
                continue;
            }

            auto pairCamera = match.pairCamera;
            auto refCamera = match.refCamera;

            matchCoords[ref].x = (float)match.keypointLeft[0];
            matchCoords[ref].y = (float)match.keypointLeft[1];

            Eigen::MatrixX<real_t> p2d1 = match.keypointLeft;
            Eigen::MatrixX<real_t> p2d2 = match.keypointRight;

            Eigen::Matrix3<real_t> refIntrinsics;
            Eigen::VectorX<real_t> refDistortion;
            calib::rfm::adjustNlsCamera(refCamera, refIntrinsics, refDistortion);
            Eigen::Matrix3<real_t> pairIntrinsics;
            Eigen::VectorX<real_t> pairDistortion;
            calib::rfm::adjustNlsCamera(pairCamera, pairIntrinsics, pairDistortion);

            matchVis[ref] = true;
            reproj2d1 = projectPointOnImagePlane(pt3d,
                refIntrinsics,
                refDistortion,
                refCamera.Extrinsics().Matrix());

            reproj2d2 = projectPointOnImagePlane(pt3d,
                                                 pairIntrinsics,
                                                 pairDistortion,
                                                 pairCamera.Extrinsics().Matrix());

            reprojectedCoords[ref].x = (float)reproj2d1(0);
            reprojectedCoords[ref].y = (float)reproj2d1(1);

            real_t rightMse =
                sqrt((p2d2(0) - reproj2d2(0)) * (p2d2(0) - reproj2d2(0)) + (p2d2(1) - reproj2d2(1)) *
                     (p2d2(1) - reproj2d2(1)));

            if (rightMse < reprojFilterThresh) {
                matchVis[pair] = true;
                matchCoords[pair].x = (float)match.keypointRight[0];
                matchCoords[pair].y = (float)match.keypointRight[1];
                reprojectedCoords[pair].x = (float)reproj2d2(0);
                reprojectedCoords[pair].y = (float)reproj2d2(1);
            }
        }
        visibility.push_back(matchVis);
        points2d.push_back(matchCoords);
        points3d.push_back(p3d);
        points3dReprojected.push_back(reprojectedCoords);
    }

    points2dToCameraPoints(points2d, cameras.size(), cameraPoints);
    points2dToCameraPoints(points3dReprojected, cameras.size(), pts3dReprojected);
    points3dFromCvToMat(points3d, pts3d);
    vis = std::move(visibility);
}

Eigen::MatrixX<real_t> applyClahe(const Eigen::MatrixX<real_t>& image, int limit) {
    cv::Mat cvImage, dst;
    cv::eigen2cv(image, cvImage);
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE();
    clahe->setClipLimit(limit);
    cvImage.convertTo(cvImage, CV_8U);
    clahe->apply(cvImage, dst);
    Eigen::MatrixX<real_t> eigenImage;
    cv::cv2eigen(dst, eigenImage);

    return eigenImage;
}

std::vector<cv::DMatch> ratioTest(const std::vector<std::vector<cv::DMatch> >& matches, real_t ratio) {
    std::vector<cv::DMatch> goodMatches;
    for (size_t i = 0; i < matches.size(); i++) {
        if (matches[i][0].distance < ratio * matches[i][1].distance) {
            goodMatches.push_back(matches[i][0]);
        }
    }
    return goodMatches;
}

std::vector<cv::DMatch> symmetryTest(const std::vector<cv::DMatch>& goodMatches1, const std::vector<cv::DMatch>& goodMatches2) {
    std::vector<cv::DMatch> newMatches;
    for (size_t i = 0; i < goodMatches1.size(); i++) {
        for (size_t j = 0; j < goodMatches2.size(); j++) {
            if ((goodMatches1[i].queryIdx == goodMatches2[j].trainIdx) &&
                (goodMatches2[j].queryIdx == goodMatches1[i].trainIdx)) {
                newMatches.push_back(cv::DMatch(goodMatches1[i].queryIdx, goodMatches1[i].trainIdx, goodMatches1[i].distance));
                break;
            }
        }
    }
    return newMatches;
}

real_t pointLineDistance(cv::Point2f point, cv::Vec3f line) {
    real_t distance = std::fabs(line(0) * point.x + line(1) * point.y + line(2)) / std::sqrt(line(0) * line(0) + line(1) * line(
                                                                                                  1));
    return distance;
}

Eigen::Vector3<real_t> triangulateMatch(const MetaShapeCamera<real_t>& leftCamera,const MetaShapeCamera<real_t>& rightCamera, const Eigen::MatrixX<real_t>& p2d1,
                                 const Eigen::MatrixX<real_t>& p2d2) {

    Affine<real_t, 3, 3> T1a = leftCamera.Extrinsics();
    Affine<real_t, 3, 3> T2a = rightCamera.Extrinsics();
    Eigen::MatrixX<real_t> T1 = T1a.Matrix();
    Eigen::MatrixX<real_t> T2 = T2a.Matrix();
    Eigen::Matrix3<real_t> K1 = leftCamera.Intrinsics();
    Eigen::Matrix3<real_t> K2 = rightCamera.Intrinsics();
    // undistort the points
    Eigen::Vector2<real_t> p2d1Undistorted = leftCamera.Undistort(p2d1);
    Eigen::Vector2<real_t> p2d2Undistorted = leftCamera.Undistort(p2d2);
    std::optional<Eigen::MatrixX<real_t>> pt3D = calib::triangulatePoint(p2d1Undistorted, p2d2Undistorted, K1, K2, T1, T2);
    Eigen::Vector3<real_t> point3d = pt3D.value();

    return point3d;
}

}
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
