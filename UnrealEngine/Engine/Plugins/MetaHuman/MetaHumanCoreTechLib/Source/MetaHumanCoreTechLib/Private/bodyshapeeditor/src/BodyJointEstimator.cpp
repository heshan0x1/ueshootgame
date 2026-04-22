// Copyright Epic Games, Inc. All Rights Reserved.

#include "bodyshapeeditor/BodyJointEstimator.h"

#include <nls/math/Math.h>
#include <carbon/io/JsonIO.h>
#include <set>


CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

struct BodyJointEstimator::Private
{
    SparseMatrix<float> jjm;
    SparseMatrix<float> jvm;
    std::vector<int> surfaceJoints;
    std::vector<int> dependentJoints;
    std::vector<int> coreJoints;
};

BodyJointEstimator::BodyJointEstimator(BodyJointEstimator&& other) noexcept
    : m(std::exchange(other.m, nullptr))
{
}

BodyJointEstimator& BodyJointEstimator::operator=(BodyJointEstimator&& other) noexcept
{
    if (this != &other)
    {
        m = std::exchange(other.m, nullptr);
    }
    return *this;
}

BodyJointEstimator::BodyJointEstimator(const BodyJointEstimator& other)
    : m(new Private(*other.m))
{
}

BodyJointEstimator& BodyJointEstimator::operator=(const BodyJointEstimator& other)
{
    if (this != &other)
    {
        *m = *other.m;
    }
    return *this;
}

BodyJointEstimator::~BodyJointEstimator()
{
    delete m;
}

BodyJointEstimator::BodyJointEstimator()
    : m { new Private() }
{
}

const std::vector<int>& BodyJointEstimator::SurfaceJoints() const
{
    return m->surfaceJoints;
}

const std::vector<int>& BodyJointEstimator::CoreJoints() const
{
    return m->coreJoints;
}

const std::vector<int>& BodyJointEstimator::DependentJoints() const
{
    return m->dependentJoints;
}

const SparseMatrix<float>& BodyJointEstimator::VertexJointMatrix() const
{
    return m->jvm;
}

const SparseMatrix<float>& BodyJointEstimator::JointJointMatrix() const
{
    return m->jjm;
}

void BodyJointEstimator::Init(const std::string& json)
{
    auto jointCorrespondence = TITAN_NAMESPACE::ReadJson(json);
    Init(jointCorrespondence);
}

void BodyJointEstimator::Init(const JsonElement& json)
{
    std::vector<int> jjR = json["jjR"].Get<std::vector<int>>();
    std::vector<int> jjC = json["jjC"].Get<std::vector<int>>();
    std::vector<float> jjV = json["jjV"].Get<std::vector<float>>();
    int jointCount = json["joint_count"].Get<int>();
    int vertexCount = json["vertex_count"].Get<int>();
    m->jjm = SparseMatrix<float>(jointCount, jointCount);
    std::set<int> dependentJoints;

    for (int i = 0; i < jjR.size(); ++i)
    {
        m->jjm.insert(jjR[i], jjC[i]) = jjV[i];
        if (jjC[i] != jjR[i])
        {
            dependentJoints.insert(jjC[i]);
        }
    }
    m->jjm.makeCompressed();

    std::vector<int> jvR = json["jvR"].Get<std::vector<int>>();
    std::vector<int> jvC = json["jvC"].Get<std::vector<int>>();
    std::vector<float> jvV = json["jvV"].Get<std::vector<float>>();
    m->jvm = SparseMatrix<float>(vertexCount, jointCount);
    std::set<int> coreJoints;
    std::set<int> surfaceJoints;
    coreJoints.insert(0);
    for (int i = 0; i < jvR.size(); ++i)
    {
        m->jvm.insert(jvR[i], jvC[i]) = jvV[i];
        if (jvV[i] == 1.0f)
        {
            // joint is laying on vertex
            surfaceJoints.insert(jvC[i]);
        }
        else
        {
            // joint is one of core joints that define the rig
            coreJoints.insert(jvC[i]);
        }
    }
    m->coreJoints = { coreJoints.begin(), coreJoints.end() };
    m->dependentJoints = { dependentJoints.begin(), dependentJoints.end() };
    m->surfaceJoints = { surfaceJoints.begin(), surfaceJoints.end() };
    m->jvm.makeCompressed();
}

Eigen::Matrix<float, 3, -1> BodyJointEstimator::EstimateJointWorldTranslations(const Eigen::Ref<const Eigen::Matrix<float, 3, -1>>& vertices) const
{
    return vertices * m->jvm * m->jjm;
}

void BodyJointEstimator::FixJointOrients(const BodyGeometry<float>& archetypeGeometry, std::vector<Eigen::Transform<float, 3, Eigen::Affine>>& targetJoints, const Eigen::Matrix3Xf& targetVertices) const
{

    const auto aimShortestArc = [](
                                    const Eigen::Affine3f& T_current,
                                    const Eigen::Vector3f& targetPosWorld,
                                    bool aimOpposite = false)
    {
        constexpr float kEps = 1e-6f;

        Eigen::Quaternionf qR0(T_current.linear());
        if (qR0.squaredNorm() < 1e-12f)
            return T_current;
        qR0.normalize();
        const Eigen::Matrix3f R0 = qR0.toRotationMatrix();
        const Eigen::Vector3f x0 = R0.col(0);
        const Eigen::Vector3f y0 = R0.col(1);
        const Eigen::Vector3f z0 = R0.col(2);

        Eigen::Vector3f b = targetPosWorld - T_current.translation();
        if (b.squaredNorm() < kEps)
        {
            Eigen::Affine3f out = T_current;
            out.linear() = R0;
            return out;
        }
        if (aimOpposite)
            b = -b;
        b.normalize();

        const float dot = std::max(-1.0f, std::min(1.0f, x0.dot(b)));
        Eigen::Quaternionf qSwing;
        if (dot > 1.0f - 1e-6f)
        {
            qSwing = Eigen::Quaternionf::Identity();
        }
        else if (dot < -1.0f + 1e-6f)
        {
            const Eigen::Vector3f axis = x0.unitOrthogonal().normalized();
            qSwing = Eigen::AngleAxisf(static_cast<float>(CARBON_PI), axis);
        }
        else
        {
            qSwing = Eigen::Quaternionf::FromTwoVectors(x0, b);
        }

        Eigen::Matrix3f R1 = qSwing.toRotationMatrix() * R0;

        const Eigen::Vector3f x1 = R1.col(0);
        Eigen::Vector3f y1 = R1.col(1);
        Eigen::Vector3f z1 = R1.col(2);

        Eigen::Vector3f u = y0 - x1 * (x1.dot(y0));
        if (u.squaredNorm() < kEps)
        {
            u = z0 - x1 * (x1.dot(z0));
        }
        if (u.squaredNorm() < kEps)
        {
            Eigen::Affine3f out = T_current;
            out.linear() = R1;
            return out;
        }
        u.normalize();

        const float a0 = y1.dot(u);
        const float a1 = z1.dot(u);
        const float theta = std::atan2(a1, a0);

        Eigen::AngleAxisf twist(theta, x1);
        Eigen::Matrix3f R_out = twist.toRotationMatrix() * R1;

        Eigen::Affine3f T_out = T_current;
        T_out.linear() = R_out;
        return T_out;
    };

    const auto findSpecialChild = [this, &targetVertices, &archetypeGeometry, &targetJoints](int jointIndex) -> std::pair<Eigen::Vector3f, bool>
    {
        const auto& jointNames = archetypeGeometry.GetJointNames();
        if (jointIndex < 0 || jointIndex >= (int)jointNames.size())
            return { {}, false };

        const std::unordered_map<std::string, int> joint_vertex_ids = {
            { "thumb_03_l", 40259 },
            { "index_03_l", 39994 },
            { "middle_03_l", 39712 },
            { "ring_03_l", 40461 },
            { "pinky_03_l", 24213 },
            { "thumb_03_r", 32698 },
            { "index_03_r", 32431 },
            { "middle_03_r", 32149 },
            { "ring_03_r", 32898 },
            { "pinky_03_r", 27999 },
            { "bigtoe_02_l", 25947 },
            { "indextoe_02_l", 43919 },
            { "middletoe_02_l", 26137 },
            { "ringtoe_02_l", 24757 },
            { "littletoe_02_l", 40604 },
            { "bigtoe_02_r", 30419 },
            { "indextoe_02_r", 36344 },
            { "middletoe_02_r", 30678 },
            { "ringtoe_02_r", 28747 },
            { "littletoe_02_r", 33041 },
        };
        const std::unordered_map<std::string, std::vector<std::string>> joint_joints_ids = {
            { "hand_l", { "middle_metacarpal_l", "ring_metacarpal_l" } },
            { "hand_r", { "middle_metacarpal_r", "middle_metacarpal_r" } },
        };
        //Define aiming positions candidates
        Eigen::Vector3f targetPosition = { 0.0f, 0.0f, 0.0f };
        Eigen::Vector3f archPosition = {0.0f, 0.0f, 0.0f}; 
        auto vertexMapperIt = joint_vertex_ids.find(jointNames[jointIndex]);
        if (vertexMapperIt != joint_vertex_ids.end())
        {
            targetPosition = targetVertices.col(vertexMapperIt->second);
            archPosition = archetypeGeometry.GetMesh(0).Vertices().col(vertexMapperIt->second);
        }

        auto jointMapperIt = joint_joints_ids.find(jointNames[jointIndex]);
        if (jointMapperIt != joint_joints_ids.end())
        {
            targetPosition = { 0.0f, 0.0f, 0.0f };
            archPosition = { 0.0f, 0.0f, 0.0f };
            for (const auto& jointName : jointMapperIt->second)
            {
                int aimJointIndex = std::distance(jointNames.begin(), std::find(jointNames.begin(), jointNames.end(), jointName));
                targetPosition += targetJoints[aimJointIndex].translation();
                archPosition += archetypeGeometry.GetBindMatrices()[aimJointIndex].translation();
            }
            targetPosition /= jointMapperIt->second.size();
            archPosition /= jointMapperIt->second.size();
        }

        const auto& archJoint = archetypeGeometry.GetBindMatrices()[jointIndex];
        Eigen::Vector3f p = archJoint.translation();
        Eigen::Vector3f aim = (archJoint.linear() * Eigen::Vector3f::UnitX()).normalized();
        float bestScore = -std::numeric_limits<float>::infinity();
        float bestSigned = 0.0f;

        Eigen::Vector3f d = archPosition - p;
        float n = d.norm();
        if (n > 1e-6f)
        {
            d /= n;
            float signedDot = aim.dot(d);
            float score = std::abs(signedDot);
            bestScore = score;
            bestSigned = signedDot;
        } else
        {
            targetPosition = {0.0f, 0.0f, 0.0f};
        }


        if (targetPosition.squaredNorm() == 0.0f)
        {
            const auto& jointHierarchy = archetypeGeometry.GetJointParentIndices();
            for (int i = 0; i < (int)jointHierarchy.size(); ++i)
            {
                if (jointHierarchy[i] != jointIndex)
                    continue;
                if (std::find(CoreJoints().begin(), CoreJoints().end(), i) == CoreJoints().end())
                {
                    continue;
                }
                d = archetypeGeometry.GetBindMatrices()[i].translation() - p;
                n = d.norm();
                if (n < 1e-6f)
                    continue;
                d /= n;
                float signedDot = aim.dot(d);
                float score = std::abs(signedDot);
                if (score > bestScore && score > 0.9397)
                {
                    bestScore = score;
                    bestSigned = signedDot;
                    targetPosition = targetJoints[i].translation();
                }
            }
        }

        return { targetPosition, bestSigned < 0.0f };
    };

    for (int ji : CoreJoints())
    {
        if (ji == 0)
        {
            continue;
        }

        auto [aimingPosition, aimOpposite] = findSpecialChild(ji);
        if (aimingPosition.squaredNorm() == 0.0f)
        {
            continue;
        }
        targetJoints[ji] = aimShortestArc(targetJoints[ji], aimingPosition, aimOpposite);
    }
    const auto& jointHierarchy = archetypeGeometry.GetJointParentIndices();
    for (int ji : DependentJoints())
    {
        int parentIndex = jointHierarchy[ji];
        targetJoints[ji].linear() = targetJoints[parentIndex].linear();
    }
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
