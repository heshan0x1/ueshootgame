// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "carbon/common/Defs.h"
#include "carbon/common/Pimpl.h"
#include "nls/DiffData.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace dna
{

class Reader;

} // namespace dna

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)


class DriverJointControls
{
    public:
    struct Mapping
    {
        std::string jointName;
        
        std::uint16_t eulerX;
        std::uint16_t eulerY;
        std::uint16_t eulerZ;
        
        std::uint16_t rawX;
        std::uint16_t rawY;
        std::uint16_t rawZ;
        std::uint16_t rawW;
        static std::map<int, Mapping> Generate(const dna::Reader* reader);
        static std::vector<std::string> GetEulerControlNames(const std::map<int, Mapping>& mappings);
        static std::vector<std::uint16_t> GetRawControlIndices(const std::map<int, Mapping>& mappings);
    };
    public:
    DriverJointControls();
    ~DriverJointControls();
    DriverJointControls(DriverJointControls&& other) noexcept;
    DriverJointControls& operator=(DriverJointControls&& other) noexcept;
    DriverJointControls(const DriverJointControls& other);
    DriverJointControls& operator=(const DriverJointControls& other);

    template <class T>
    DiffData<T> EvaluateRawControlsFromEuler(const DiffData<T>& eulerControls) const;

    template <class T>
    DiffData<T> EvaluateRawControlsFromJoints(const DiffData<T>& jointDiff) const;


    const std::vector<std::string>& EulerControlNames() const;
    const std::vector<std::uint16_t>& RawControlIndices() const;
    
    const std::map<int, Mapping>& Mappings() const;
    
    int JointIndexFromEulerControl(std::uint16_t control) const;
    int JointIndexFromRawControl(std::uint16_t control) const;
    
    void Init(const dna::Reader* reader);
    void RemoveJoints(const std::vector<int>& newToOldJointMapping);

private:
    struct Private;
    TITAN_NAMESPACE::Pimpl<Private> m;

};

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
