// Copyright Epic Games, Inc. All Rights Reserved.

#include <rig/RBFLogic.h>
#include <nls/math/Math.h>
#include <nls/DiffData.h>
#include <nls/geometry/EulerAngles.h>
#include <carbon/common/Defs.h>
#include <rig/rbfs/RBFSolver.h>

#include <dna/layers/JointBehaviorMetadata.h>
#include <dna/layers/RBFBehavior.h>
#include <dna/Reader.h>
#include <dna/Writer.h>
#include <pma/resources/DefaultMemoryResource.h>
#include <tdm/TDM.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>


CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

namespace
{

using SolverPtr = pma::ScopedPtr<rbfs::RBFSolver>;
using dna::TranslationRepresentation;
using dna::RotationRepresentation;
using dna::ScaleRepresentation;

} // namespace

struct JointRepresentation
{
    TranslationRepresentation translation;
    RotationRepresentation rotation;
    ScaleRepresentation scale;
};

template <typename T>
struct RBFLogic<T>::Private
{
    Private() = default;
    Private(Private &&) = default;
    Private& operator=(Private&&) = default;

    Private(const Private& other) :
        driverJointControls{ other.driverJointControls },
    solvers{},
    poseInputControlIndices{ other.poseInputControlIndices },
    poseOutputControlIndices{ other.poseOutputControlIndices },
    poseOutputControlWeights{ other.poseOutputControlWeights },
    solverPoseIndices{ other.solverPoseIndices },
    solverDriverJointIndices{ other.solverDriverJointIndices },
    solverNames{ other.solverNames },
    poseNames{ other.poseNames },
    poseControlNames{ other.poseControlNames },
    poseScales{ other.poseScales },
    isAutomaticRadius{ other.isAutomaticRadius },
    rawTargetValues{ other.rawTargetValues },
    jointRepresentations{ other.jointRepresentations },
    rawControlCount{ other.rawControlCount },
    poseControlOffset{ other.poseControlOffset },
    poseControlCount{ other.poseControlCount },
    maxDrivingJointsCount{ other.maxDrivingJointsCount }
    {
        for (const auto& solver : other.solvers)
        {
            solvers.push_back(pma::makeScoped<rbfs::RBFSolver>(solver.get()));
        }
    }

    Private& operator=(const Private& rhs)
    {
        Private tmp{ rhs };
        *this = std::move(tmp);
        return *this;
    }

    DriverJointControls driverJointControls;
    std::vector<SolverPtr> solvers;
    std::vector<std::vector<std::uint16_t>> poseInputControlIndices;
    std::vector<std::vector<std::uint16_t>> poseOutputControlIndices;
    std::vector<std::vector<float>> poseOutputControlWeights;
    std::vector<std::vector<std::uint16_t>> solverPoseIndices;
    std::vector<std::vector<std::uint16_t>> solverDriverJointIndices;
    std::vector<std::string> solverNames;
    std::vector<std::string> poseNames;
    std::vector<std::string> poseControlNames;
    std::vector<float> poseScales;
    std::vector<bool> isAutomaticRadius;
    std::vector<std::vector<float>> rawTargetValues;
    std::vector<JointRepresentation> jointRepresentations;
    std::uint16_t rawControlCount;
    std::uint16_t poseControlOffset;
    std::uint16_t poseControlCount;
    std::uint16_t maxDrivingJointsCount;
};

template <typename T>
RBFLogic<T>::RBFLogic() : m{new Private()}
{}

template <typename T> RBFLogic<T>::~RBFLogic() = default;
template <typename T>
RBFLogic<T>::RBFLogic(const RBFLogic<T>& other) : m{new Private(*other.m)}
{}

template <typename T>
RBFLogic<T>& RBFLogic<T>::operator=(const RBFLogic<T>& other)
{
    RBFLogic<T> temp(other);
    std::swap(*this, temp);
    return *this;
}

template <typename T> RBFLogic<T>::RBFLogic(RBFLogic&&) = default;
template <typename T>
RBFLogic<T>& RBFLogic<T>::operator=(RBFLogic&&) = default;

template <typename T>
bool RBFLogic<T>::Init(const dna::Reader* reader)
{
    const auto jointCount = reader->getJointCount();
    std::map<std::string, std::uint16_t> jointNameToIdx{};
    m->jointRepresentations.clear();
    m->jointRepresentations.reserve(jointCount);
    m->poseControlOffset = reader->getRawControlCount() + reader->getPSDCount() + reader->getMLControlCount();

    for (std::uint16_t i = 0; i < jointCount; ++i)
    {
        std::string jointName = reader->getJointName(i).c_str();
        jointNameToIdx.insert({ jointName, i });
        m->jointRepresentations.push_back(JointRepresentation{ reader->getJointTranslationRepresentation(i),
                                                               reader->getJointRotationRepresentation(i),
                                                               reader->getJointScaleRepresentation(i) });
    }

    for (int i = 0; i < reader->getRBFPoseCount(); ++i)
    {
        m->poseNames.push_back(reader->getRBFPoseName(std::uint16_t(i)).c_str());
    }


    m->driverJointControls.Init(reader);
    m->solvers.clear();
    m->poseInputControlIndices.clear();
    m->poseOutputControlIndices.clear();
    m->poseOutputControlWeights.clear();
    m->solverPoseIndices.clear();
    m->solverDriverJointIndices.clear();
    m->solverNames.clear();
    m->poseControlNames.clear();
    m->poseNames.clear();
    m->poseScales.clear();
    m->isAutomaticRadius.clear();
    m->rawTargetValues.clear();
    m->jointRepresentations.clear();
    const auto solverCount = reader->getRBFSolverCount();
    const auto poseCount = reader->getRBFPoseCount();

    m->solverPoseIndices.resize(solverCount);
    m->solverDriverJointIndices.resize(solverCount);
    m->poseInputControlIndices.resize(poseCount);
    m->poseOutputControlIndices.resize(poseCount);
    m->poseOutputControlWeights.resize(poseCount);
    m->solverNames.reserve(solverCount);
    m->poseNames.reserve(poseCount);
    m->poseScales.reserve(poseCount);

    m->poseControlCount = reader->getRBFPoseControlCount();
    m->poseControlNames.reserve(m->poseControlNames.size());
    m->rawControlCount = reader->getRawControlCount();

    for (std::uint16_t pi = 0u; pi < poseCount; ++pi)
    {
        m->poseNames.push_back(reader->getRBFPoseName(pi).c_str());
        m->poseScales.push_back(reader->getRBFPoseScale(pi));
        const auto inputIndices = reader->getRBFPoseInputControlIndices(pi);
        const auto poseWeights = reader->getRBFPoseOutputControlWeights(pi);
        const auto outputIndices = reader->getRBFPoseOutputControlIndices(pi);
        m->poseInputControlIndices[pi].assign(inputIndices.begin(), inputIndices.end());
        m->poseOutputControlWeights[pi].assign(poseWeights.begin(), poseWeights.end());
        m->poseOutputControlIndices[pi].assign(outputIndices.begin(), outputIndices.end());
    }

    for (std::uint16_t pci = 0u; pci < m->poseControlCount; pci++)
    {
        m->poseControlNames.push_back(reader->getRBFPoseControlName(pci).c_str());
    }

    for (std::uint16_t si = 0u; si < solverCount; ++si)
    {
        m->solverNames.push_back(reader->getRBFSolverName(si).c_str());
        if (reader->getRBFSolverDistanceMethod(si) == static_cast<dna::RBFDistanceMethod>(rbfs::RBFDistanceMethod::Euclidean))
        {
            CARBON_CRITICAL("RBFLogic.cpp supports only quaternion based rbf solvers.");
        }

        auto solverRawControlIndices = reader->getRBFSolverRawControlIndices(si);
        for (std::size_t i = 0; i < solverRawControlIndices.size(); i += 4)
        {
            int drivingJointIndex = m->driverJointControls.JointIndexFromRawControl(solverRawControlIndices[i]); 
            if(drivingJointIndex < 0)
            {
              CARBON_CRITICAL("RBFLogic.cpp driving joint missing raw controls, solver index {}", si);   
            }
            m->solverDriverJointIndices[si].push_back(static_cast<std::uint16_t>(drivingJointIndex));
        }
        const std::uint16_t drivingJointCount = static_cast<std::uint16_t>(solverRawControlIndices.size() / 4u);

        if (drivingJointCount > m->maxDrivingJointsCount)
        {
            m->maxDrivingJointsCount = drivingJointCount;
        }

        std::vector<float> targetScales;
        auto poseIndices = reader->getRBFSolverPoseIndices(si);
        m->solverPoseIndices[si].assign(poseIndices.data(), poseIndices.data() + poseIndices.size());
        for (std::uint16_t pi : m->solverPoseIndices[si])
        {
            targetScales.push_back(m->poseScales[pi]);
        }

        rbfs::RBFSolverRecipe recipe;
        recipe.solverType = static_cast<rbfs::RBFSolverType>(reader->getRBFSolverType(si));
        recipe.distanceMethod = static_cast<rbfs::RBFDistanceMethod>(reader->getRBFSolverDistanceMethod(si));
        recipe.normalizeMethod = static_cast<rbfs::RBFNormalizeMethod>(reader->getRBFSolverNormalizeMethod(si));
        recipe.isAutomaticRadius = static_cast<rbfs::AutomaticRadius>(reader->getRBFSolverAutomaticRadius(si)) == rbfs::AutomaticRadius::On;
        m->isAutomaticRadius.push_back(recipe.isAutomaticRadius);
        recipe.twistAxis = static_cast<rbfs::TwistAxis>(reader->getRBFSolverTwistAxis(si));
        recipe.weightFunction = static_cast<rbfs::RBFFunctionType>(reader->getRBFSolverFunctionType(si));
        const auto radius = reader->getRBFSolverRadius(si);
        recipe.radius = reader->getRotationUnit() == dna::RotationUnit::degrees ? tdm::radians(radius) : radius;
        recipe.rawControlCount = static_cast<std::uint16_t>(solverRawControlIndices.size());
        recipe.weightThreshold = reader->getRBFSolverWeightThreshold(si);
        recipe.targetValues = reader->getRBFSolverRawControlValues(si);
        m->rawTargetValues.push_back(std::vector<float>{ recipe.targetValues.begin(), recipe.targetValues.end() });
        recipe.targetScales = targetScales;
        m->solvers.push_back(pma::makeScoped<rbfs::RBFSolver>(recipe));
    }

    return true;
}

template <typename T>
void RBFLogic<T>::Write(dna::Writer* writer)
{
    for (std::uint16_t pci = 0u; pci < m->poseControlNames.size(); ++pci)
    {
        writer->setRBFPoseControlName(pci, m->poseControlNames[pci].c_str());
    }
    for (std::uint16_t pi = 0u; pi < m->poseNames.size(); ++pi)
    {
        writer->setRBFPoseName(pi, m->poseNames[pi].c_str());
        writer->setRBFPoseScale(pi, m->poseScales[pi]);
        writer->setRBFPoseInputControlIndices(pi, m->poseInputControlIndices[pi].data(), static_cast<std::uint16_t>(m->poseInputControlIndices[pi].size()));
        writer->setRBFPoseOutputControlIndices(pi, m->poseOutputControlIndices[pi].data(), static_cast<std::uint16_t>(m->poseOutputControlIndices[pi].size()));
        writer->setRBFPoseOutputControlWeights(pi, m->poseOutputControlWeights[pi].data(), static_cast<std::uint16_t>(m->poseOutputControlWeights[pi].size()));
    }
    for (std::uint16_t si = 0u; si < m->solverNames.size(); ++si)
    {
        writer->setRBFSolverName(si, m->solverNames[si].c_str());
        auto solverRawControlIndices = SolverRawControlIndices(si);
        writer->setRBFSolverRawControlIndices(si, solverRawControlIndices.data(), static_cast<std::uint16_t>(solverRawControlIndices.size()));
        writer->setRBFSolverPoseIndices(si, m->solverPoseIndices[si].data(), static_cast<std::uint16_t>(m->solverPoseIndices[si].size()));
        writer->setRBFSolverRawControlValues(si, m->rawTargetValues[si].data(), static_cast<std::uint16_t>(m->rawTargetValues[si].size()));
        writer->setRBFSolverType(si, static_cast<dna::RBFSolverType>(m->solvers[si]->getSolverType()));
        writer->setRBFSolverRadius(si, m->solvers[si]->getRadius());
        dna::AutomaticRadius automaticRadius = m->isAutomaticRadius[si] ? dna::AutomaticRadius::On : dna::AutomaticRadius::Off;
        writer->setRBFSolverAutomaticRadius(si, automaticRadius);
        writer->setRBFSolverWeightThreshold(si, m->solvers[si]->getWeightThreshold());
        writer->setRBFSolverDistanceMethod(si, static_cast<dna::RBFDistanceMethod>(m->solvers[si]->getDistanceMethod()));
        writer->setRBFSolverNormalizeMethod(si, static_cast<dna::RBFNormalizeMethod>(m->solvers[si]->getNormalizeMethod()));
        writer->setRBFSolverFunctionType(si, static_cast<dna::RBFFunctionType>(m->solvers[si]->getWeightFunction()));
        writer->setRBFSolverTwistAxis(si, static_cast<dna::TwistAxis>(m->solvers[si]->getTwistAxis()));
    }
}

template <class T>
DiffData<T> RBFLogic<T>::EvaluatePoseControlsFromJoints(const DiffData<T>& joints, bool withJointScaling) const
{
    std::vector<float> rbfInput(m->maxDrivingJointsCount * 4u);
    const auto rbfSolverCount = m->poseNames.size();
    std::vector<float> solverIntermediateBuffer(rbfSolverCount);
    std::vector<float> solverOutputBuffer(rbfSolverCount);

    Vector<T> poseControls = Vector<T>::Zero(PoseControlCount());
    auto getJointQuaternion = [&](int index) -> Eigen::Quaternion<T> {
                return Eigen::AngleAxis<T>(joints.Value()[index * (withJointScaling ? 9 : 6) + 5u], Eigen::Vector3<T>::UnitZ()) *
                       Eigen::AngleAxis<T>(joints.Value()[index * (withJointScaling ? 9 : 6) + 4u], Eigen::Vector3<T>::UnitY()) *
                       Eigen::AngleAxis<T>(joints.Value()[index * (withJointScaling ? 9 : 6) + 3u], Eigen::Vector3<T>::UnitX());
            };

    for (std::uint16_t si = 0u; si < m->solvers.size(); ++si)
    {
        const auto& solver = m->solvers[si];
        const auto solverPoseCount = solver->getTargetCount();
        
        for(std::size_t i = 0; i < m->solverDriverJointIndices[si].size(); i+=4)
        {
            Eigen::Quaternion<T> q = getJointQuaternion(m->solverDriverJointIndices[si][i]);
            rbfInput[i + 0] = static_cast<float>(q.x());
            rbfInput[i + 1] = static_cast<float>(q.y());
            rbfInput[i + 2] = static_cast<float>(q.z());
            rbfInput[i + 3] = static_cast<float>(q.w());
        }

        solver->solve(av::ArrayView<float>{ rbfInput.data(), m->solverDriverJointIndices[si].size()*4 },
                      av::ArrayView<float>{ solverIntermediateBuffer.data(), solverPoseCount },
                      av::ArrayView<float>{ solverOutputBuffer.data(), solverPoseCount });

        for (std::size_t i = 0u; i < m->solverPoseIndices[si].size(); ++i)
        {
            const float poseWeight = solverOutputBuffer[i];
            const std::uint16_t pi = m->solverPoseIndices[si][i];
            const auto& controlOutputIndices = m->poseOutputControlIndices[pi];
            const auto& controlOutputWeights = m->poseOutputControlWeights[pi];
            for (std::size_t pci = 0u; pci < controlOutputWeights.size(); ++pci)
            {
                // we need to take into account offset as poseControlIndices are indices into total controls and we want only rbf controls to be returned
                const std::uint16_t controlIndex = controlOutputIndices[pci] - m->poseControlOffset;
                poseControls[controlIndex] += poseWeight * controlOutputWeights[pci];
            }
        }
    }

    return DiffData<T>(std::move(poseControls));
}

template <class T>
DiffData<T> RBFLogic<T>::EvaluatePoseControlsFromRawControls(const DiffData<T>& rawControls) const
{
    std::vector<float> rbfInput(m->maxDrivingJointsCount * 4u);
    const auto rbfSolverCount = m->poseNames.size();
    std::vector<float> solverIntermediateBuffer(rbfSolverCount);
    std::vector<float> solverOutputBuffer(rbfSolverCount);

    Vector<T> poseControls = Vector<T>::Zero(PoseControlCount());
    for (std::uint16_t si = 0u; si < m->solvers.size(); ++si)
    {
        const auto& solver = m->solvers[si];
        const auto solverPoseCount = solver->getTargetCount();
        auto solverRawControlIndices = SolverRawControlIndices(si);
        for(std::size_t i = 0; i < solverRawControlIndices.size(); i+=4)
        {
            rbfInput[i + 0] = static_cast<float>(rawControls.Value()[solverRawControlIndices[i + 0]]);
            rbfInput[i + 1] = static_cast<float>(rawControls.Value()[solverRawControlIndices[i + 1]]);
            rbfInput[i + 2] = static_cast<float>(rawControls.Value()[solverRawControlIndices[i + 2]]);
            rbfInput[i + 3] = static_cast<float>(rawControls.Value()[solverRawControlIndices[i + 3]]);
        }

        solver->solve(av::ArrayView<float>{ rbfInput.data(), solverRawControlIndices.size() },
                      av::ArrayView<float>{ solverIntermediateBuffer.data(), solverPoseCount },
                      av::ArrayView<float>{ solverOutputBuffer.data(), solverPoseCount });

        for (std::size_t i = 0u; i < m->solverPoseIndices[si].size(); ++i)
        {
            const float poseWeight = solverOutputBuffer[i];
            const std::uint16_t pi = m->solverPoseIndices[si][i];
            const auto& controlOutputIndices = m->poseOutputControlIndices[pi];
            const auto& controlOutputWeights = m->poseOutputControlWeights[pi];
            const auto& controlInputIndices = m->poseInputControlIndices[pi];
            float inputWeight = 1.0f;
            for (std::uint16_t inputIndex : controlInputIndices)
            {
                inputWeight *= static_cast<float>(rawControls.Value()[inputIndex]);
            }
            for (std::size_t pci = 0u; pci < controlOutputWeights.size(); ++pci)
            {
                // we need to take into account offset as poseControlIndices are indices into total controls and we want only rbf controls to be returned
                const std::uint16_t controlIndex = controlOutputIndices[pci] - m->poseControlOffset;
                poseControls[controlIndex] += poseWeight * controlOutputWeights[pci] * inputWeight;
            }
        }
    }

    return DiffData<T>(std::move(poseControls));
}

template <class T>
const SolverPtr& RBFLogic<T>::GetSolver(int solverIndex) const
{
    return m->solvers[solverIndex];
}

template <typename T>
const std::vector<SolverPtr>& RBFLogic<T>::RBFSolvers() const
{
    return m->solvers;
}

template <class T>
const std::vector<std::string>& RBFLogic<T>::SolverNames() const
{
    return m->solverNames;
}

template <class T>
std::vector<std::uint16_t> RBFLogic<T>::SolverRawControlIndices(int solverIndex) const
{
    if (solverIndex >= static_cast<int>(m->solverDriverJointIndices.size()))
    {
        CARBON_CRITICAL("Solver index out of range: %s", solverIndex);
    }
    const auto& mappings =  m->driverJointControls.Mappings();
    const auto& driverJointIndices = m->solverDriverJointIndices[solverIndex];
    std::vector<std::uint16_t> solverRawControlIndices;
    for(std::uint16_t driverJoint : driverJointIndices)
    {
        solverRawControlIndices.push_back(mappings.at(driverJoint).rawX);
        solverRawControlIndices.push_back(mappings.at(driverJoint).rawY);
        solverRawControlIndices.push_back(mappings.at(driverJoint).rawZ);
        solverRawControlIndices.push_back(mappings.at(driverJoint).rawW);
    }
    return solverRawControlIndices;
}

template <class T>
const std::vector<std::uint16_t>& RBFLogic<T>::SolverPoseIndices(int solverIndex) const
{
    return m->solverPoseIndices[solverIndex];
}

template <class T>
std::uint16_t RBFLogic<T>::PoseControlOffset() const
{
    return m->poseControlOffset;
}

template <class T>
const std::vector<std::uint16_t>& RBFLogic<T>::PoseOutputControlIndices(int poseIndex) const
{
    return m->poseOutputControlIndices[poseIndex];
}

template <class T>
const std::string& RBFLogic<T>::PoseOutputControlName(int poseOutputControlIndex) const
{
    int actualIndex = poseOutputControlIndex - m->poseControlOffset;
    if (actualIndex < 0)
    {
        CARBON_CRITICAL("RBFLogic poseOutputControl index out of bound of actual indices {}, index should be in range of {}-{}",
                        poseOutputControlIndex,
                        m->poseControlOffset,
                        m->poseControlOffset + m->poseControlNames.size());
    }
    return m->poseControlNames[actualIndex];
}

template <class T>
const std::vector<std::string>& RBFLogic<T>::PoseNames() const
{
    return m->poseNames;
}

template <class T>
const std::vector<std::string>& RBFLogic<T>::PoseControlNames() const
{
    return m->poseControlNames;
}

template <class T>
const std::vector<float>& RBFLogic<T>::SolverRawTargetValues(int solverIndex) const
{
    return m->rawTargetValues[solverIndex];
}

template <class T>
bool RBFLogic<T>::SolverAutomaticRadius(int solverIndex) const
{
    return m->isAutomaticRadius[solverIndex];
}

template <class T>
const DriverJointControls& RBFLogic<T>::GetDriverJointControls() const 
{
    return m->driverJointControls;
}

template <class T>
void RBFLogic<T>::SetSolver(int solverIndex, const std::string& solverName, const std::vector<std::uint16_t>& poseIndices, rbfs::RBFSolverRecipe recipe)
{
    if (solverIndex > static_cast<int>(m->solvers.size()))
    {
        CARBON_CRITICAL("RBFLogic::SetSolver solverIndex={} out of bounds, solvers.size()={} ", solverIndex, m->solvers.size());
    }
    m->solverNames[solverIndex] = solverName;
    m->solverPoseIndices[solverIndex].assign(poseIndices.data(), poseIndices.data() + poseIndices.size());
    m->isAutomaticRadius[solverIndex] = recipe.isAutomaticRadius;
    m->rawTargetValues[solverIndex] = std::vector<float>{ recipe.targetValues.begin(), recipe.targetValues.end() };
    m->solvers[solverIndex] = pma::makeScoped<rbfs::RBFSolver>(recipe);
}

template <class T>
std::uint16_t RBFLogic<T>::PoseControlCount() const
{
    return m->poseControlCount;
}

template <class T>
void RBFLogic<T>::RemoveJoints(const std::vector<int>& newToOldJointMapping)
{
    std::vector<JointRepresentation> jointRepresentations;
    jointRepresentations.reserve(newToOldJointMapping.size());
    const auto oldToNew = [&](int oldJointIndex) {
            for (int newIdx = 0; newIdx < static_cast<int>(newToOldJointMapping.size()); ++newIdx)
            {
                if (newToOldJointMapping[newIdx] == oldJointIndex)
                {
                    return newIdx;
                }
            }
            return -1;
        };

    for (int newIdx = 0; newIdx < int(newToOldJointMapping.size()); ++newIdx)
    {
        const int oldIdx = newToOldJointMapping[newIdx];
        if (oldIdx < (int)m->jointRepresentations.size())
        {
            jointRepresentations.push_back(m->jointRepresentations[oldIdx]);
        }
    }

    for (std::size_t si = 0; si < m->solverDriverJointIndices.size(); ++si)
    {
        for (std::size_t i = 0; i < m->solverDriverJointIndices[si].size(); ++i)
        {
            int oldIndex = static_cast<int>(m->solverDriverJointIndices[si][i]);
            int newIndex = oldToNew(oldIndex);
            if (newIndex < 0)
            {
                const std::string jointName = m->driverJointControls.Mappings().at(oldIndex).jointName;  
                CARBON_CRITICAL("Removing joint {} that is bound to solver {}", jointName, si);
            }
            m->solverDriverJointIndices[si][i] = static_cast<std::uint16_t>(newIndex);
        }
    }

    m->driverJointControls.RemoveJoints(newToOldJointMapping);

    LOG_VERBOSE("remove {} out of {} RBFLogic::jointRepresentations",
                m->jointRepresentations.size() - jointRepresentations.size(),
                m->jointRepresentations.size());
    m->jointRepresentations = jointRepresentations;
}

template class RBFLogic<float>;
template class RBFLogic<double>;

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
