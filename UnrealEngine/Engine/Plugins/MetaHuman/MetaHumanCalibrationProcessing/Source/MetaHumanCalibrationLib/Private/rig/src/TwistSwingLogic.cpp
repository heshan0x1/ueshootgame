// Copyright Epic Games, Inc. All Rights Reserved.

#include "rig/TwistSwingLogic.h"

#include <rig/DriverJointControls.h>
#include <rig/rbfs/TDMQuaternionJacobian.h>
#include <dna/Reader.h>
#include <dna/Writer.h>

#include <Eigen/Geometry>
#include <tdm/TDM.h>

#include <cstdint>
#include <vector>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)


namespace
{

using dna::TwistAxis;
using dna::ArrayView;
using dna::ConstArrayView;

struct TwistSwingSetup
{
    std::vector<uint16_t> twistOutputJointIndices;
    std::vector<float> twistWeights;
    std::vector<uint16_t> swingOutputJointIndices;
    std::vector<float> swingWeights;
    dna::TwistAxis twistAxis;
};

template <class T>
inline void getSwing(Eigen::Quaternion<T>& quat, TwistAxis twistAxis)
{
    const T x = quat.x();
    const T y = quat.y();
    const T z = quat.z();
    const T w = quat.w();
    switch (twistAxis)
    {
    case TwistAxis::X:
    {
        const T new_w = -std::sqrt(x * x + w * w);
        quat.z() = (w * z + x * y) / new_w;
        quat.y() = (w * y - x * z) / new_w;
        quat.x() = 0.0f;
        quat.w() = new_w;
        break;
    }
    case TwistAxis::Y:
    {
        const T new_w = -std::sqrt(y * y + w * w);
        quat.z() = (w * z - y * x) / new_w;
        quat.y() = 0.0f;
        quat.x() = (w * x + y * z) / new_w;
        quat.w() = new_w;
        break;
    }
    case TwistAxis::Z:
    {
        const T new_w = -std::sqrt(z * z + w * w);
        quat.z() = 0.0f;
        quat.y() = (w * y + z * x) / new_w;
        quat.x() = (w * x - z * y) / new_w;
        quat.w() = new_w;
        break;
    }
    }
}

template <class T>
constexpr T& coeff(tdm::quat<T>& q, int i)
{
    switch (i)
    {
    case 0:
        return q.x;
    case 1:
        return q.y;
    case 2:
        return q.z;
    case 3:
        return q.w;
    default:
        throw std::out_of_range("tdm::quat coeff index out of range");
    }
}

template <class T>
constexpr const T& coeff(const tdm::quat<T>& q, int i)
{
    switch (i)
    {
    case 0:
        return q.x;
    case 1:
        return q.y;
    case 2:
        return q.z;
    case 3:
        return q.w;
    default:
        throw std::out_of_range("tdm::quat coeff index out of range");
    }
}


template <class T>
inline void getTwist(tdm::quat<T>& q, TwistAxis twistAxis)
{
    const auto twistIndex = static_cast<std::uint16_t>(twistAxis);
    const auto notTwistIndex0 = (twistIndex + 1u) % 3u;
    const auto notTwistIndex1 = (twistIndex + 2u) % 3u;

    coeff(q, notTwistIndex0) = T(0);
    coeff(q, notTwistIndex1) = T(0);

    const T v = coeff(q, twistIndex);
    const T mag = std::sqrt(v * v + q.w * q.w);

    if (mag > T(0))
    {
        coeff(q, twistIndex) /= mag;
        q.w /= mag;
    }
    else
    {
        // fallback to identity twist
        coeff(q, twistIndex) = T(0);
        q.w = T(1);
    }
}


template <class T>
inline  Eigen::Matrix<T, 4, 4> getTwistJacobian(const tdm::quat<T>& q_in, TwistAxis twistAxis)
{
    Eigen::Matrix<T, 4, 4> J; 

    const int i = static_cast<int>(twistAxis); 

    const T v = (i == 0 ? q_in.x : i == 1 ? q_in.y
                                          : q_in.z);
    const T w = q_in.w;
    const T mag = std::sqrt(v * v + w * w);
    if (mag > T(0))
    {
        const T inv = T(1) / mag;
        const T inv3 = inv * inv * inv;

        // ∂(v/mag)/∂v and ∂(v/mag)/∂w
        const T dv_v = (w * w) * inv3;
        const T dv_w = -(v * w) * inv3;

        // ∂(w/mag)/∂v and ∂(w/mag)/∂w
        const T dw_v = -(v * w) * inv3;
        const T dw_w = (v * v) * inv3;

        // Fill Jacobian
        J.setZero();
        // d(out[i])/d(v) and d(out[i])/d(w)
        J(i, i) = dv_v; // out[i] wrt input[i]
        J(i, 3) = dv_w; // out[i] wrt input[w]

        // d(out.w)/d(v) and d(out.w)/d(w)
        J(3, i) = dw_v;
        J(3, 3) = dw_w;
    }

    return J;
}


} // anonymous namespace


template <class T>
struct TwistSwingLogic<T>::Private
{
    std::map<int, TwistSwingSetup> setups;
    DriverJointControls driverJointControls;
    int jointCount;
    bool withJointScaling;
};


template <class T>
TwistSwingLogic<T>::TwistSwingLogic()
    : m { new Private() }
{
}
template <class T>
TwistSwingLogic<T>::~TwistSwingLogic() = default;

template <class T>
TwistSwingLogic<T>::TwistSwingLogic(TwistSwingLogic&&) = default;

template <class T>
TwistSwingLogic<T>& TwistSwingLogic<T>::operator=(TwistSwingLogic&&) = default;

template <class T>
TwistSwingLogic<T>::TwistSwingLogic(const TwistSwingLogic& other)
    : m { new Private { *other.m } }
{
}

template <class T>
TwistSwingLogic<T>& TwistSwingLogic<T>::operator=(const TwistSwingLogic& other)
{
    TwistSwingLogic<T> temp { other };
    std::swap(*this, temp);
    return *this;
}

template <class T>
void TwistSwingLogic<T>::RemoveJoints(const std::vector<int>& newToOldJointMapping)
{
    std::map<int, TwistSwingSetup> newSetups;

    const auto oldToNew = [&](int oldJointIndex)
    {
        for (int newIdx = 0; newIdx < static_cast<int>(newToOldJointMapping.size()); ++newIdx)
        {
            if (newToOldJointMapping[newIdx] == oldJointIndex)
            {
                return newIdx;
            }
        }
        return -1;
    };

    for (const auto& [jointIndex, setup] : m->setups)
    {
        const int newJointIndex = oldToNew(jointIndex);
        if (newJointIndex == -1)
        {
            continue;
        }
        TwistSwingSetup newSetup;
        newSetup.twistAxis = setup.twistAxis;

        for (std::uint16_t i = 0u; i < setup.twistOutputJointIndices.size(); ++i)
        {
            if (oldToNew(setup.twistOutputJointIndices[i]) != -1)
            {
                newSetup.twistOutputJointIndices.push_back((std::uint16_t)oldToNew(int(setup.twistOutputJointIndices[i])));
                newSetup.twistWeights.push_back(setup.twistWeights[i]);
            }
        }
        for (std::uint16_t i = 0u; i < setup.swingOutputJointIndices.size(); ++i)
        {
            if (oldToNew(setup.swingOutputJointIndices[i]) != -1)
            {
                newSetup.swingOutputJointIndices.push_back((std::uint16_t)oldToNew(int(setup.swingOutputJointIndices[i])));
                newSetup.swingWeights.push_back(setup.swingWeights[i]);
            }
        }

        if (!newSetup.twistOutputJointIndices.empty() || !newSetup.swingOutputJointIndices.empty())
        {
            newSetups[newJointIndex] = std::move(newSetup);
        }
    }
    m->jointCount = static_cast<int>(newToOldJointMapping.size());
    m->driverJointControls.RemoveJoints(newToOldJointMapping);
}

template <class T>
DiffData<T> TwistSwingLogic<T>::EvaluateJointsFromJoints(const DiffData<T>& jointDiff) const
{
    Vector<T> output(jointDiff.Value());
    auto getJointQuaternion = [&](int index) -> tdm::quat<T>
    {
        const tdm::rad<T> x(jointDiff.Value()[index * (m->withJointScaling ? 9 : 6) + 3u]);
        const tdm::rad<T> y(jointDiff.Value()[index * (m->withJointScaling ? 9 : 6) + 4u]);
        const tdm::rad<T> z(jointDiff.Value()[index * (m->withJointScaling ? 9 : 6) + 5u]);
        return tdm::quat<T>(tdm::rad3<T> { x, y, z }, tdm::rot_seq::xyz);
    };
    const auto identity = Eigen::Quaternion<T>::Identity();
    for (const auto& [jointIndex, setup] : m->setups)
    {
        tdm::quat<T> q = getJointQuaternion(jointIndex);

        if (setup.twistOutputJointIndices.size() > 0)
        {
            getTwist<T>(q, setup.twistAxis);
            for (std::size_t i = 0; i < setup.twistOutputJointIndices.size(); ++i)
            {
                const auto outputJointIndex = setup.twistOutputJointIndices[i];
                auto euler = tdm::slerp(q, tdm::quat<T> {}, static_cast<T>(setup.twistWeights[i])).euler(tdm::rot_seq::xyz);
                output[outputJointIndex * (m->withJointScaling ? 9 : 6) + 3u] = euler[0].value;
                output[outputJointIndex * (m->withJointScaling ? 9 : 6) + 4u] = euler[1].value;
                output[outputJointIndex * (m->withJointScaling ? 9 : 6) + 5u] = euler[2].value;
            }
        }

        if (setup.swingOutputJointIndices.size() > 0)
        {

            Eigen::Quaternion<T> swing = Eigen::AngleAxis<T>(jointDiff.Value()[jointIndex * (m->withJointScaling ? 9 : 6) + 5u], Eigen::Vector3<T>::UnitZ()) * Eigen::AngleAxis<T>(jointDiff.Value()[jointIndex * (m->withJointScaling ? 9 : 6) + 4u], Eigen::Vector3<T>::UnitY()) * Eigen::AngleAxis<T>(jointDiff.Value()[jointIndex * (m->withJointScaling ? 9 : 6) + 3u], Eigen::Vector3<T>::UnitX());
            getSwing<T>(swing, setup.twistAxis);
            for (std::uint16_t i = 0u; i < setup.swingOutputJointIndices.size(); i++)
            {
                const auto outputJointIndex = setup.swingOutputJointIndices[i];
                const Eigen::Vector3<T> euler = swing.inverse().slerp(setup.swingWeights[i], identity).toRotationMatrix().eulerAngles(0, 1, 2);
                output[outputJointIndex * (m->withJointScaling ? 9 : 6) + 3u] = euler[0];
                output[outputJointIndex * (m->withJointScaling ? 9 : 6) + 4u] = euler[1];
                output[outputJointIndex * (m->withJointScaling ? 9 : 6) + 5u] = euler[2];
            }
        }
    }
    JacobianConstPtr<T> Jacobian;
    if (jointDiff.HasJacobian())
    {
        using Triplet = Eigen::Triplet<T>;

        const auto& inVals = jointDiff.Value();
        const int N = static_cast<int>(inVals.size());
        const int stride = m->withJointScaling ? 9 : 6;

        auto readEuler = [&](int jointIdx) -> tdm::rad3<T>
        {
            const int base = jointIdx * stride + 3;
            return tdm::rad3<T> {
                tdm::rad<T>(inVals[base + 0]),
                tdm::rad<T>(inVals[base + 1]),
                tdm::rad<T>(inVals[base + 2])
            };
        };

        const tdm::quat<T> qId { T(0), T(0), T(0), T(1) };

        std::vector<Triplet> triplets;
        triplets.reserve(N + static_cast<int>(m->setups.size()) * 9);

        // identity baseline
        for (int i = 0; i < N; ++i)
        {
            triplets.emplace_back(i, i, T(1));
        }

        for (const auto& [jointIndex, setup] : m->setups)
        {
            const tdm::rad3<T> eIn = readEuler(jointIndex);
            const Eigen::Matrix<T, 4, 3> J_q_e = EulerXYZToQuaternionJacobian<T>(eIn);
            
            if (setup.twistOutputJointIndices.empty())
                continue;
            //TODO add swing jacobian

            const tdm::quat<T> qSrc(eIn, tdm::rot_seq::xyz);

            const Eigen::Matrix<T, 4, 4>& J_qt_q = getTwistJacobian<T>(qSrc, setup.twistAxis);
            tdm::quat<T> qT = qSrc;
            getTwist(qT, setup.twistAxis);

            for (std::size_t i = 0; i < setup.twistOutputJointIndices.size(); ++i)
            {
                const int outJoint = static_cast<int>(setup.twistOutputJointIndices[i]);
                const T w = static_cast<T>(setup.twistWeights[i]);
                
                const auto sl = SlerpJacobianAll<T>(qT, qId, w);
                const Eigen::Matrix<T, 4, 4>& J_qs_qt = sl.dqdq1;
                const tdm::quat<T>& qOut = sl.q;

                const Eigen::Matrix<T, 3, 4> J_e_q = QuaternionToEulerXYZJacobian<T>(qOut);

                const Eigen::Matrix<T, 3, 3> J_local = J_e_q * J_qs_qt * J_qt_q * J_q_e;

                const int inBase = jointIndex * stride + 3;
                const int outBase = outJoint * stride + 3;

                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c)
                        triplets.emplace_back(outBase + r, inBase + c, J_local(r, c));
            }
        }

        SparseMatrix<T> J_map (N, N);
        J_map.setFromTriplets(triplets.begin(), triplets.end());
        J_map.makeCompressed();
        Jacobian = jointDiff.Jacobian().Premultiply(J_map);
    }

    return { std::move(output), std::move(Jacobian) };
}

template <class T>
DiffData<T> TwistSwingLogic<T>::EvaluateJointsFromRawControls(const DiffData<T>& rawControls) const
{
    Vector<T> output(m->jointCount);

    const auto identity = Eigen::Quaternion<T>::Identity();
    for (const auto& [jointIndex, setup] : m->setups)
    {
        const auto& mapping = m->driverJointControls.Mappings().at(jointIndex);
        Eigen::Quaternion<T> q = { rawControls.Value()[mapping.rawX],
            rawControls.Value()[mapping.rawY],
            rawControls.Value()[mapping.rawZ],
            rawControls.Value()[mapping.rawW] };

        if (setup.swingOutputJointIndices.size() > 0)
        {
            Eigen::Quaternion<T> swing = q;
            getSwing<T>(swing, setup.twistAxis);
            for (std::uint16_t i = 0u; i < setup.swingOutputJointIndices.size(); i++)
            {
                const auto outputJointIndex = setup.swingOutputJointIndices[i];
                const Eigen::Vector3<T> euler = identity.slerp(setup.swingWeights[i], swing).toRotationMatrix().eulerAngles(0, 1, 2);
                output[outputJointIndex * (m->withJointScaling ? 9 : 6) + 3u] = euler[0];
                output[outputJointIndex * (m->withJointScaling ? 9 : 6) + 4u] = euler[1];
                output[outputJointIndex * (m->withJointScaling ? 9 : 6) + 5u] = euler[2];
            }
        }

        if (setup.twistOutputJointIndices.size() > 0)
        {
            tdm::quat<T> twist { q.x(), q.y(), q.z(), q.w() };
            getTwist<T>(twist, setup.twistAxis);
            for (std::size_t i = 0; i < setup.twistOutputJointIndices.size(); ++i)
            {
                const auto outputJointIndex = setup.twistOutputJointIndices[i];
                auto euler = tdm::slerp(twist, tdm::quat<T> {}, static_cast<T>(setup.twistWeights[i])).euler(tdm::rot_seq::xyz);
                output[outputJointIndex * (m->withJointScaling ? 9 : 6) + 3u] = euler[0].value;
                output[outputJointIndex * (m->withJointScaling ? 9 : 6) + 4u] = euler[1].value;
                output[outputJointIndex * (m->withJointScaling ? 9 : 6) + 5u] = euler[2].value;
            }
        }
    }
    JacobianConstPtr<T> Jacobian;
    if (rawControls.HasJacobian())
    {
        // TODO Premultiply with quaternion jacobian
        Jacobian = rawControls.JacobianPtr();
    }
    return { std::move(output), std::move(Jacobian) };
}

template <class T>
bool TwistSwingLogic<T>::Init(const dna::Reader* reader, bool withJointScaling)
{
    m->withJointScaling = withJointScaling;
    const std::uint16_t swingCount = reader->getSwingCount();
    const std::uint16_t twistCount = reader->getTwistCount();
    m->jointCount = reader->getJointCount();

    m->driverJointControls.Init(reader);

    for (std::uint16_t i = 0; i < swingCount; ++i)
    {
        ConstArrayView<std::uint16_t> swingInputJointControls = reader->getSwingInputControlIndices(i);
        int ji = m->driverJointControls.JointIndexFromRawControl(swingInputJointControls[0]);
        if (ji == -1)
        {
            CARBON_CRITICAL("Missing joint control mapping for raw control {} ", swingInputJointControls[0]);
        }
        auto& setup = m->setups[ji];
        ConstArrayView<std::uint16_t> swingOutputJointIndices = reader->getSwingOutputJointIndices(i);
        setup.swingOutputJointIndices = { swingOutputJointIndices.begin(), swingOutputJointIndices.end() };
        setup.twistAxis = reader->getSwingSetupTwistAxis(i);
        ConstArrayView<float> swingBlendWeights = reader->getSwingBlendWeights(i);
        setup.swingWeights = { swingBlendWeights.begin(), swingBlendWeights.end() };
    }

    for (std::uint16_t i = 0; i < twistCount; ++i)
    {
        ConstArrayView<std::uint16_t> twistInputJointControls = reader->getTwistInputControlIndices(i);
        int ji = m->driverJointControls.JointIndexFromRawControl(twistInputJointControls[0]);
        if (ji == -1)
        {
            CARBON_CRITICAL("Missing joint control mapping for raw control {} ", twistInputJointControls[0]);
        }

        auto& setup = m->setups[ji];
        ConstArrayView<std::uint16_t> twistOutputJointIndices = reader->getTwistOutputJointIndices(i);
        setup.twistOutputJointIndices = { twistOutputJointIndices.begin(), twistOutputJointIndices.end() };
        dna::TwistAxis twistAxis = reader->getTwistSetupTwistAxis(i);
        setup.twistAxis = twistAxis;
        ConstArrayView<float> twistBlendWeights = reader->getTwistBlendWeights(i);
        setup.twistWeights = { twistBlendWeights.begin(), twistBlendWeights.end() };
    }

    return true;
}

template <class T>
void TwistSwingLogic<T>::Write(dna::Writer* writer)
{
    std::uint16_t si = 0u;
    std::uint16_t ti = 0u;
    std::vector<std::uint16_t> rawControlBuffer(4u);

    for (const auto& [jointIndex, setup] : m->setups)
    {
        const auto& mapping = m->driverJointControls.Mappings().at(jointIndex);
        rawControlBuffer[0] = mapping.rawX;
        rawControlBuffer[1] = mapping.rawY;
        rawControlBuffer[2] = mapping.rawZ;
        rawControlBuffer[3] = mapping.rawW;

        if (setup.swingOutputJointIndices.size() > 0)
        {
            writer->setSwingOutputJointIndices(si, setup.swingOutputJointIndices.data(), static_cast<std::uint16_t>(setup.swingOutputJointIndices.size()));
            writer->setSwingBlendWeights(si, setup.swingWeights.data(), static_cast<std::uint16_t>(setup.swingWeights.size()));
            writer->setSwingSetupTwistAxis(si, setup.twistAxis);
            writer->setSwingInputControlIndices(si, rawControlBuffer.data(), static_cast<uint16_t>(rawControlBuffer.size()));
            si++;
        }
        if (setup.twistOutputJointIndices.size() > 0)
        {
            writer->setTwistInputControlIndices(ti, rawControlBuffer.data(), static_cast<std::uint16_t>(rawControlBuffer.size()));
            writer->setTwistOutputJointIndices(ti, setup.twistOutputJointIndices.data(), static_cast<std::uint16_t>(setup.twistOutputJointIndices.size()));
            writer->setTwistBlendWeights(ti, setup.twistWeights.data(), static_cast<std::uint16_t>(setup.twistWeights.size()));
            writer->setTwistSetupTwistAxis(ti, setup.twistAxis);
            ti++;
        }
    }
}

template class TwistSwingLogic<float>;
template class TwistSwingLogic<double>;


CARBON_NAMESPACE_END(TITAN_NAMESPACE)
