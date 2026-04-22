// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <carbon/Common.h>
#include <arrayview/ArrayView.h>
#include <carbon/common/Pimpl.h>
#include <rig/rbfs/RBFSolver.h>
#include <rig/DriverJointControls.h>

#include <vector>

namespace dna
{
class Reader;
class Writer;
}

using av::ArrayView;
using av::ConstArrayView;

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

template<class T>
class DiffData;

template <class T>
class RBFLogic
{
public:
    using SolverPtr = pma::ScopedPtr<rbfs::RBFSolver>;

    RBFLogic();
    ~RBFLogic();

    RBFLogic(RBFLogic&&);
    RBFLogic& operator=(RBFLogic&&);

    RBFLogic(const RBFLogic&);
    RBFLogic& operator=(const RBFLogic&);

    DiffData<T> EvaluatePoseControlsFromRawControls(const DiffData<T>& rawControls) const;
    DiffData<T> EvaluatePoseControlsFromJoints(const DiffData<T>& joints, bool withJointScaling = false) const;
    
    bool Init(const dna::Reader* reader);
    void Write(dna::Writer* writer);

    const SolverPtr& GetSolver(int solverIndex) const;
    const std::vector<SolverPtr>& RBFSolvers() const;
    std::vector<std::uint16_t> SolverRawControlIndices(int solverIndex) const;
    const std::vector<float>& SolverRawTargetValues(int solverIndex) const;
    const std::vector<std::string>& SolverNames() const;
    const std::vector<std::uint16_t>& SolverPoseIndices(int solverIndex) const;
    const std::vector<std::uint16_t>& PoseOutputControlIndices(int poseIndex) const;
    const std::string& PoseOutputControlName(int poseOutputControlIndex) const;
    std::uint16_t PoseControlCount() const;
    std::uint16_t PoseControlOffset() const;
    const std::vector<std::string>& PoseNames() const;
    const std::vector<std::string>& PoseControlNames() const;
    bool SolverAutomaticRadius(int solverIndex) const;
    const DriverJointControls& GetDriverJointControls() const;

    void SetSolver(int solverIndex, const std::string& solverName, const std::vector<std::uint16_t>& poseIndices, rbfs::RBFSolverRecipe recipe);

    void RemoveJoints(const std::vector<int>& newToOldJointMapping);

    private:
        struct Private;
        TITAN_NAMESPACE::Pimpl<Private> m;
};

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
