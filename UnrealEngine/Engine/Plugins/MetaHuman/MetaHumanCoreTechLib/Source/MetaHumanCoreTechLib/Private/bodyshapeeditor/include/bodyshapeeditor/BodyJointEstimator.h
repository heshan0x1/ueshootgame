// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <arrayview/ArrayView.h>
#include <carbon/io/JsonIO.h>
#include <carbon/common/Defs.h>
#include <nls/math/Math.h>
#include <rig/BodyGeometry.h>

#include <vector>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

class BodyJointEstimator
{
public:
    BodyJointEstimator();
    ~BodyJointEstimator();
    BodyJointEstimator(const BodyJointEstimator& other);
    BodyJointEstimator(BodyJointEstimator&& other) noexcept;
    BodyJointEstimator& operator=(const BodyJointEstimator& other);
    BodyJointEstimator& operator=(BodyJointEstimator&& other) noexcept;

    void Init(const std::string& json);
    void Init(const JsonElement& json);

    const std::vector<int>& SurfaceJoints() const;
    const std::vector<int>& CoreJoints() const;
    const std::vector<int>& DependentJoints() const;
    const SparseMatrix<float>& VertexJointMatrix() const;
    const SparseMatrix<float>& JointJointMatrix() const;

    Eigen::Matrix<float, 3, -1> EstimateJointWorldTranslations(const Eigen::Ref<const Eigen::Matrix<float, 3, -1>>& vertices) const;
    void FixJointOrients(const BodyGeometry<float>& archetypeJoints, std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& targetJoints, const Eigen::Matrix3Xf& vertices)  const;

private:
    struct Private;
    Private* m;
};


CARBON_NAMESPACE_END(TITAN_NAMESPACE)
