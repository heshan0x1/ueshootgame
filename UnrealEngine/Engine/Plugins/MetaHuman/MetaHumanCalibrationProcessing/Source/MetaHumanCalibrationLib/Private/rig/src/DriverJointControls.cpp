// Copyright Epic Games, Inc. All Rights Reserved.

#include "rig/DriverJointControls.h"
#include "nls/DiffData.h"

#include <dna/Reader.h>
#include <tdm/TDM.h>


CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

struct DriverJointControls::Private
{
    std::map<int, DriverJointControls::Mapping> mappings;
    std::vector<std::string> eulerControlNames;
    std::vector<std::uint16_t> rawControlIndices;
    std::vector<DriverJointControls::Mapping> removedMappings;
    int rawControlCount;
};

DriverJointControls::~DriverJointControls() = default;
DriverJointControls::DriverJointControls(DriverJointControls&& other) noexcept = default;
DriverJointControls& DriverJointControls::operator=(DriverJointControls&& other) noexcept = default;

DriverJointControls::DriverJointControls(const DriverJointControls& other) :
    m{new Private{ *other.m }}
{}

DriverJointControls& DriverJointControls::operator=(const DriverJointControls& other)
{
    DriverJointControls temp(other);
    std::swap(*this, temp);
    return *this;
}

DriverJointControls::DriverJointControls() :
    m{new Private}
{}

const std::vector<std::string>& DriverJointControls::EulerControlNames() const
{
    return m->eulerControlNames;
}

const std::map<int, DriverJointControls::Mapping>& DriverJointControls::Mappings() const
{
    return m->mappings;
}

void DriverJointControls::Init(const dna::Reader* reader)
{
    m->mappings.clear();
    m->eulerControlNames.clear();
    m->removedMappings.clear();
    m->rawControlIndices.clear();
    m->rawControlCount = reader->getRawControlCount();
    m->mappings = Mapping::Generate(reader);
    m->eulerControlNames = Mapping::GetEulerControlNames(m->mappings);
    m->rawControlIndices = Mapping::GetRawControlIndices(m->mappings); 
}

template <class T>
DiffData<T> DriverJointControls::EvaluateRawControlsFromEuler(const DiffData<T>& eulerControls) const
{
    if (eulerControls.Size() != static_cast<int>(m->eulerControlNames.size()))
    {
        CARBON_CRITICAL("DriverJointControls::EvaluateRawControlsFromEuler(): eulerControls count incorrect: {} instead of {}",
                        eulerControls.Size(),
                        m->eulerControlNames.size());
    }

    Vector<T> rawControls = Vector<T>::Zero(m->rawControlCount);
    for (const auto& [jIndex, mapping] : m->mappings)
    {
        using tdm::frad;

        const auto euler = tdm::frad3{
            frad{ static_cast<float>(eulerControls.Value()[mapping.eulerX]) },
            frad{ static_cast<float>(eulerControls.Value()[mapping.eulerY]) },
            frad{ static_cast<float>(eulerControls.Value()[mapping.eulerZ]) } };

        auto quaternion = tdm::quat<float>::fromEuler<tdm::rot_seq::xyz>(euler);

        rawControls[mapping.rawX] = quaternion.x;
        rawControls[mapping.rawY] = quaternion.y;
        rawControls[mapping.rawZ] = quaternion.z;
        rawControls[mapping.rawW] = quaternion.w;
    }

    return DiffData<T>{ std::move(rawControls) };
}

template DiffData<float> DriverJointControls::EvaluateRawControlsFromEuler(const DiffData<float>& diffData) const;
template DiffData<double> DriverJointControls::EvaluateRawControlsFromEuler(const DiffData<double>& diffData) const;

template <class T>
DiffData<T> DriverJointControls::EvaluateRawControlsFromJoints(const DiffData<T>& jointDiff) const
{
    Vector<T> rawControls = Vector<T>::Zero(m->rawControlCount);
    for (const auto& [jointIndex, mapping] : m->mappings)
    {
        using tdm::frad;
        const auto euler = tdm::frad3{
            frad{ static_cast<float>(jointDiff.Value()[jointIndex * 9 + 3]) },
            frad{ static_cast<float>(jointDiff.Value()[jointIndex * 9 + 4]) },
            frad{ static_cast<float>(jointDiff.Value()[jointIndex * 9 + 5]) } };

        auto quaternion = tdm::quat<float>::fromEuler<tdm::rot_seq::xyz>(euler);
        rawControls[mapping.rawX] = quaternion.x;
        rawControls[mapping.rawY] = quaternion.y;
        rawControls[mapping.rawZ] = quaternion.z;
        rawControls[mapping.rawW] = quaternion.w;
    }
    return DiffData<T>{std::move(rawControls)};
}

template DiffData<float> DriverJointControls::EvaluateRawControlsFromJoints(const DiffData<float>& diffData) const;
template DiffData<double> DriverJointControls::EvaluateRawControlsFromJoints(const DiffData<double>& diffData) const;

void DriverJointControls::RemoveJoints(const std::vector<int>& newToOldJointMapping)
{
    std::map<int, Mapping> updatedMappings;
    for (const auto& [jointIndex, mapping] : m->mappings)
    {
        int newJointIndex = -1;
        for (int newIdx = 0; newIdx < static_cast<int>(newToOldJointMapping.size()); ++newIdx)
        {
            if (newToOldJointMapping[newIdx] == jointIndex)
            {
                newJointIndex = newIdx;
                break;
            }
        }

        if (newJointIndex >= 0)
        {
            Mapping updatedMapping = mapping;
            updatedMappings.emplace(newJointIndex, std::move(updatedMapping));
        }
    }

    LOG_VERBOSE("Removed {} out of {} mappings", m->mappings.size() - updatedMappings.size(), m->mappings.size());
    m->mappings = std::move(updatedMappings);
    m->eulerControlNames = Mapping::GetEulerControlNames(updatedMappings);
}

int DriverJointControls::JointIndexFromEulerControl(std::uint16_t control) const
{
    for (const auto& [jointIndex, mapping] : m->mappings)
    {
        if (mapping.eulerX == control || mapping.eulerY == control || mapping.eulerZ == control)
        {
            return jointIndex;
        }
    }
    return -1;
}

int DriverJointControls::JointIndexFromRawControl(std::uint16_t control) const
{
    for (const auto& [jointIndex, mapping] : m->mappings)
    {
        if (mapping.rawX == control || mapping.rawY == control || mapping.rawZ == control || mapping.rawW == control)
        {
            return jointIndex;
        }
    }
    return -1; 
}

const std::vector<std::uint16_t>& DriverJointControls::RawControlIndices() const
{
    return m->rawControlIndices;
}

std::map<int, DriverJointControls::Mapping> DriverJointControls::Mapping::Generate(const dna::Reader* reader)
{
    std::map<int, DriverJointControls::Mapping> mappings;
    #ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable: 4702)
    #endif
    const auto getJointIndex = [reader](const std::string& jointName) {
            for (std::uint16_t i = 0; i < reader->getJointCount(); ++i)
            {
                if (reader->getJointName(i) == jointName)
                {
                    return static_cast<int>(i);
                }
            }
            CARBON_CRITICAL("Joint with name {} doesn't exist in dna file.", jointName);
            return -1;
        };
    #ifdef _MSC_VER
    #pragma warning(pop)
    #endif
    std::uint16_t eulerControlCount = 0u;
    const auto addMapping = [&](av::ConstArrayView<std::uint16_t> quaternionControls) {
            for (std::uint16_t qi = 0; qi < quaternionControls.size(); qi += 4u)
            {
                // Control names are in form   {jointName}.{quaternionAttribute}
                // e.g. "calf_l.x", "calf_l.y",  "calf_l.z", " calf_l.w"
                const std::string fullControlName = reader->getRawControlName(quaternionControls[qi]).c_str();
                const size_t dotPos = fullControlName.find('.');
                const std::string jointName = fullControlName.substr(0, dotPos);
                const auto drivingJointIndex = getJointIndex(jointName);
                if (drivingJointIndex == -1)
                {
                    continue;
                }
                if (mappings.contains(drivingJointIndex))
                {
                    if ((mappings[drivingJointIndex].rawX != quaternionControls[qi + 0]) ||
                        (mappings[drivingJointIndex].rawY != quaternionControls[qi + 1]) ||
                        (mappings[drivingJointIndex].rawZ != quaternionControls[qi + 2]) ||
                        (mappings[drivingJointIndex].rawW != quaternionControls[qi + 3]))
                    {
                        CARBON_CRITICAL("Raw control indices do not match for driving joint %s", jointName.c_str());
                    }
                    continue;
                }
                DriverJointControls::Mapping mapping{};
                mapping.jointName = jointName;
                mapping.rawX = quaternionControls[qi + 0];
                mapping.rawY = quaternionControls[qi + 1];
                mapping.rawZ = quaternionControls[qi + 2];
                mapping.rawW = quaternionControls[qi + 3];
                mapping.eulerX = eulerControlCount + 0u;
                mapping.eulerY = eulerControlCount + 1u;
                mapping.eulerZ = eulerControlCount + 2u;
                eulerControlCount += 3u;
                mappings[drivingJointIndex] = mapping;
            }
        };

    std::uint16_t solverCount = reader->getRBFSolverCount();
    for (std::uint16_t si = 0u; si < solverCount; ++si)
    {
        auto solverRawControlIndices = reader->getRBFSolverRawControlIndices(si);
        addMapping(solverRawControlIndices);
    }
    for (std::uint16_t ti = 0; ti < reader->getTwistCount(); ++ti)
    {
        auto inputControlIndices = reader->getTwistInputControlIndices(ti);
        addMapping(inputControlIndices);
    }
    for (std::uint16_t si = 0u; si < reader->getSwingCount(); ++si)
    {
        auto inputControlIndices = reader->getSwingInputControlIndices(si);
        addMapping(inputControlIndices);
    }

    return mappings;
}

std::vector<std::string> DriverJointControls::Mapping::GetEulerControlNames(const std::map<int, DriverJointControls::Mapping>& mappings)
{
    std::vector<std::string> eulerJointControls;
    eulerJointControls.resize(mappings.size() * 3u);

    for (const auto& [jointIndex, mapping] : mappings)
    {
        eulerJointControls[mapping.eulerX] = mapping.jointName + ".x";
        eulerJointControls[mapping.eulerY] = mapping.jointName + ".y";
        eulerJointControls[mapping.eulerZ] = mapping.jointName + ".z";
    }
    return eulerJointControls;
}

std::vector<std::uint16_t> DriverJointControls::Mapping::GetRawControlIndices(const std::map<int, DriverJointControls::Mapping>& mappings)
{
    std::vector<std::uint16_t> rawControlIndices;
    for (const auto& [jointIndex, mapping] : mappings)
    {
        rawControlIndices.push_back(mapping.rawX);
        rawControlIndices.push_back(mapping.rawY);
        rawControlIndices.push_back(mapping.rawZ);
        rawControlIndices.push_back(mapping.rawW);
    }
    return rawControlIndices;
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
