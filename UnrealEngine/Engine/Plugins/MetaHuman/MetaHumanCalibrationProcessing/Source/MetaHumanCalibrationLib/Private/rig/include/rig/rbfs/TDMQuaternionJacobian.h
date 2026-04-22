// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4127 5054)
#endif
#include <Eigen/Core>
#include <Eigen/Dense>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "carbon/Common.h"
#include "tdm/TDM.h"

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

template <class T>
Eigen::Matrix<T, 3, 4> QuaternionToEulerXYZJacobian(const tdm::quat<T>& q)
{
    const T x = q.x;
    const T y = q.y;
    const T z = q.z;
    const T w = q.w;

    const T x2 = x + x;
    const T y2 = y + y;
    const T z2 = z + z;

    const T xx2 = x * x2;
    const T yy2 = y * y2;
    const T zz2 = z * z2;
    const T xy2 = x * y2;
    const T xz2 = x * z2;
    const T yz2 = y * z2;
    const T wx2 = w * x2;
    const T wy2 = w * y2;
    const T wz2 = w * z2;

    Eigen::Matrix<T, 3, 4> J;
    J.setZero();

    const T sy = xz2 + wy2;
    const T threshold = static_cast<T>(0.99999999999);
    const T s = sy;
    const T sq = std::sqrt(std::max(T(0), 1 - s * s));
    J(1, 0) = (2 * z) / sq;     // dp/dx
    J(1, 1) = (2 * w) / sq;     // dp/dy
    J(1, 2) = (2 * x) / sq;     // dp/dz
    J(1, 3) = (2 * y) / sq;     // dp/dw

    if (std::abs(sy) < threshold)
    {
        // Roll
        const T A = wx2 - yz2;
        const T B = 1 - (xx2 + yy2);
        const T denom_r = A * A + B * B;
        J(0, 0) = (B * 2 * w - A * -4 * x) / denom_r; // dr/dx
        J(0, 1) = (B * -2 * z - A * -4 * y) / denom_r; // dr/dy
        J(0, 2) = (B * -2 * y - A * 0) / denom_r;      // dr/dz
        J(0, 3) = (B * 2 * x - A * 0) / denom_r;       // dr/dw

        // Yaw
        const T C = wz2 - xy2;
        const T D = 1 - (yy2 + zz2);
        const T denom_yaw = C * C + D * D;
        J(2, 0) = (D * -2 * y - C * 0) / denom_yaw;         // dy/dx
        J(2, 1) = (D * -2 * x - C * -4 * y) / denom_yaw;    // dy/dy
        J(2, 2) = (D * 2 * w - C * -4 * z) / denom_yaw;     // dy/dz
        J(2, 3) = (D * 2 * z - C * 0) / denom_yaw;          // dy/dw
    }
    else
    {
        // ----- Gimbal lock branch -----
        // Roll
        // angles[0] = atan2(yz2 + wx2, 1 - (xx2 + zz2));
        const T A_gimbal = yz2 + wx2;
        const T B_gimbal = 1 - (xx2 + zz2);
        const T denom_r_gimbal = A_gimbal * A_gimbal + B_gimbal * B_gimbal;
        // dA/d[x y z w]: [0 + 2w, 2z + 0, 2y + 0, 0 + 2x]
        // dB/d[x y z w]: [-4x, 0, -4z, 0]
        J(0, 0) = (B_gimbal * 2 * w - A_gimbal * -4 * x) / denom_r_gimbal; // dr/dx
        J(0, 1) = (B_gimbal * 2 * z - A_gimbal * 0) / denom_r_gimbal;      // dr/dy
        J(0, 2) = (B_gimbal * 2 * y - A_gimbal * -4 * z) / denom_r_gimbal; // dr/dz
        J(0, 3) = (B_gimbal * 2 * x - A_gimbal * 0) / denom_r_gimbal;      // dr/dw

        J.row(2).setZero();
    }

    return J;
}

template <typename T>
Eigen::Matrix<T, 4, 4> QuaternionNormalizationJacobian(const tdm::quat<T>& quat)
{
    Eigen::Matrix<T, 4, 1> q { quat.x, quat.y, quat.z, quat.w };
    T n = q.norm();
    T n2 = n * n;
    Eigen::Matrix<T, 4, 4> I = Eigen::Matrix<T, 4, 4>::Identity();
    return (I - (q * q.transpose()) / n2) / n;
}

template <class T>
Eigen::Matrix<T, 4, 3> EulerXYZToQuaternionJacobian(const tdm::rad3<T>& rot)
{
    // half-angles
    tdm::rad3<T> h{ rot * static_cast<T>(0.5) };

    // sines/cosines of half-angles
    auto sx = std::sin(h[0].value);
    auto sy = std::sin(h[1].value);
    auto sz = std::sin(h[2].value);
    auto cx = std::cos(h[0].value);
    auto cy = std::cos(h[1].value);
    auto cz = std::cos(h[2].value);

    // quaternion components (same as your forward function)
    const T x = sx*cy*cz + cx*sy*sz;
    const T y = cx*sy*cz - sx*cy*sz;
    const T z = cx*cy*sz + sx*sy*cz;
    const T w = cx*cy*cz - sx*sy*sz;

    Eigen::Matrix<T, 4, 3> J;

    const T half = static_cast<T>(0.5);

    // d/d alpha (roll)
    J(0,0) = half * ( w);
    J(1,0) = half * (-z);
    J(2,0) = half * ( y);
    J(3,0) = half * (-x);

    // d/d beta (pitch) – explicit (s,c) form
    J(0,1) = half * (-sx*sy*cz + cx*cy*sz);
    J(1,1) = half * ( cx*cy*cz + sx*sy*sz);
    J(2,1) = half * (-cx*sy*sz + sx*cy*cz);
    J(3,1) = half * (-cx*sy*cz - sx*cy*sz);

    // d/d gamma (yaw)
    J(0,2) = half * ( y);
    J(1,2) = half * (-x);
    J(2,2) = half * ( w);
    J(3,2) = half * (-z);

    return J;
}

template <class T>
struct SlerpJacobian {
    Eigen::Matrix<T,4,4> dqdq1; // derivative of q wrt q1
    Eigen::Matrix<T,4,4> dqdq2; // derivative of q wrt q2
    Eigen::Matrix<T,4,1> dqdt;  // derivative of q wrt t
    tdm::quat<T> q;             // slerp result (x,y,z,w)
};

template <class T>
inline SlerpJacobian<T> SlerpJacobianAll(const tdm::quat<T>& q1, const tdm::quat<T>& q2, T t) {
    SlerpJacobian<T> J;
    const T c_raw = dot(q1, q2);
    T c = c_raw;
    T sgn = T(1);
    tdm::quat<T> tmp = q2;

    // Flip sign if dot product is negative to make sure we take the shortest path
    if (c_raw < T(0)) {
        sgn = T(-1);
        tmp = q2; 
        tmp.negate(); 
        c = -c_raw;  // make positive so theta is in [0, pi]
    }

    auto asEigen = [](const tdm::quat<T> q){
        Eigen::Matrix<T,4,1> v; 
        v << q.x,q.y,q.z,q.w;
        return v;
    };

    const T one = T(1);
    const T eps = std::numeric_limits<T>::epsilon();
    const T thresh = one - eps;

    // If the quaternions are almost colinear, fall back to linear interpolation
    if (c > thresh) {
        const T t1 = one - t;
        const T t2 = t;

        // interpolated quaternion
        J.q = q1 * t1 + tmp * t2;

        // derivatives in the lerp case
        J.dqdq1 = Eigen::Matrix<T,4,4>::Identity() * t1;
        J.dqdq2 = Eigen::Matrix<T,4,4>::Identity() * (sgn * t2); 
        J.dqdt  = asEigen((-q1) + tmp); 

        return J;
    }

    // Standard slerp branch
    const T theta = std::acos(c);
    const T S     = std::sin(theta);
    const T rs    = one / S;
    const T ct    = std::cos(theta);

    const T a = one - t;
    const T b = t;

    const T A = std::sin(a * theta);
    const T B = std::sin(b * theta);

    const T t1 = A * rs;
    const T t2 = B * rs;

    // slerp result
    J.q = q1 * t1 + tmp * t2;

    // derivative wrt t
    const T dt1_dt = -(theta * std::cos(a * theta)) * rs;
    const T dt2_dt =  (theta * std::cos(b * theta)) * rs;
    J.dqdt = asEigen(q1 * dt1_dt + tmp * dt2_dt);

    // derivative wrt theta
    const T dt1_dtheta = (a * std::cos(a * theta)) * rs - A * ct * (rs * rs);
    const T dt2_dtheta = (b * std::cos(b * theta)) * rs - B * ct * (rs * rs);

    // derivative of theta wrt q1 and q2
    const T dtheta_dc = -rs;  
    Eigen::Matrix<T,4,1> dtheta_dq1 = (dtheta_dc * sgn) * asEigen(q2);
    Eigen::Matrix<T,4,1> dtheta_dq2 = (dtheta_dc * sgn) * asEigen(q1);

    // build the Jacobians
    J.dqdq1 = Eigen::Matrix<T,4,4>::Identity() * t1
            + (asEigen(q1) * (dtheta_dq1.transpose())) * dt1_dtheta
            + (asEigen(tmp) * (dtheta_dq1.transpose())) * dt2_dtheta;

    J.dqdq2 = Eigen::Matrix<T,4,4>::Identity() * (sgn * t2)
            + (asEigen(q1) * (dtheta_dq2.transpose())) * dt1_dtheta
            + (asEigen(tmp) * (dtheta_dq2.transpose())) * dt2_dtheta;

    return J;
}

         
CARBON_NAMESPACE_END(TITAN_NAMESPACE)
